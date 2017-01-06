/***************************************************************************
 FILE: cl_remote_user.cc
 LVU : 1.3.8

 DESC:
 This file contains the code for remote user specific actions. It also defines
 some command functions declared virtual in cl_user. 

 Copyright (C) Neil Robertson 2003,2004

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

 ***************************************************************************/

#include "globals.h"


/*** Constructor ***/
cl_remote_user::cl_remote_user(
	uint16_t uid,cl_server *svr,pkt_user_info *pkt): cl_user(0)
{
cl_server *svr2;
cl_group *grp;
cl_user *u;
cl_friend *frnd;
st_user_ban *gb;
int ret,dlen,snlen;
char *tmp,*sname;

// Set id's
id = uid;
orig_id = pkt->orig_uid; // ntohs() done in cl_server::recv_join()
remote_id = ntohs(pkt->uid);
desc = NULL;
full_name = NULL;
full_desc = NULL;
home_svrname = NULL;

stage = USER_STAGE_NEW;

if (svr->proto_rev > 5) {
	sname = (char *)pkt->name_desc_svr + pkt->namelen + pkt->desclen;
	snlen = pkt->len - (PKT_USER_INFO_SIZE-1) - pkt->namelen - pkt->desclen;
	dlen = pkt->desclen;
	}
else {
	sname = NULL;
	snlen = 0;
	dlen = pkt->len - pkt->namelen - (PKT_USER_INFO_SIZE-1);
	}

// Set name, description & home server name
if ((error = set_name((char *)pkt->name_desc_svr,pkt->namelen)) != OK ||
    (error = set_desc((char *)pkt->name_desc_svr + pkt->namelen,dlen)) != OK ||
    (error = set_svrname(sname,snlen)) != OK) 
	return;

stage = USER_STAGE_CMD_LINE;

// Set flags. Ignore muzzled, invis & net_bcast flags. These should never
// be sent by the other end but just in case someone has a hacked version...
prev_flags = flags = ntohl(pkt->user_flags) & REMOTE_IGNORE_FLAGS_MASK;

// Set misc. 
server_from = svr;
hop_count = pkt->hop_count+1;
orig_level = (pkt->orig_level > USER_LEVEL_ADMIN ? USER_LEVEL_ADMIN : pkt->orig_level);
term_cols = pkt->term_cols;
term_rows = pkt->term_rows;

group = NULL;
home_group = remotes_home_group;

// Set home ip. Data stored in network byte order. If we're first remote
// server then fill in ip address.
bzero((char *)&ip_addr,sizeof(ip_addr));
ip_addr.sin_family = AF_INET;
ip_addr.sin_port = pkt->home_port;
if (FLAGISSET(USER_FLAG_IP6_ADDR)) {
	log(1,"WARNING: User %04X (%04X@%s) has unsupported IP6 home address",
		id,remote_id,svr->name);
	ip_addr.sin_addr.s_addr = 0;
	ipnumstr = strdup("0.0.0.0");
	}
else {
	ip_addr.sin_addr.s_addr = pkt->home_addr.ip4;
	ipnumstr = strdup((char *)inet_ntoa(ip_addr.sin_addr));

	// See if user or server already has this address 
	if ((tmp = get_ipnamestr(ip_addr.sin_addr.s_addr,this))) {
		free(ipnamestr);
		ipnamestr = tmp;
		log(1,"User address %s = %s",ipnumstr,ipnamestr);
 		}
	else {
	// Create ip name resolve thread
	if (create_thread(
		THREAD_TYPE_USER_RESOLVE,
		&ip_thrd,user_resolve_thread_entry,(void *)this) == -1) {
			errprintf("Unable to create thread: %s\n\n",
				strerror(errno));
			log(1,"ERROR: UID %04X: Unable to create thread: %s",
				id,strerror(errno));
		}
}
	}

// Do logon message before level set to user
allprintf(
	MSG_MISC,
	USER_LEVEL_NOVICE,
	this,"~BGREMLOGON:~RS ~FT%04X~RS, %s %s\n",id,name,desc);

// Set some variables
level = (pkt->orig_level > remote_user_max_level) ?
         remote_user_max_level : pkt->orig_level;
type = USER_TYPE_REMOTE;
login_time = server_time;

// Send logon notify
FOR_ALL_SERVERS(svr2) {
	if (svr2->stage == SERVER_STAGE_CONNECTED) {
		if ((ret=svr2->send_logon_notify(id)) != OK)
			log(1,"ERROR: cl_remote_user::cl_remote_user() -> cl_server::send_logon_notify(): %s",err_string[ret]);
		}
	}

// See if we're a friend of anyone and if so then announce our logon.
FOR_ALL_USERS(u) {
	if (u == this) continue;

	FOR_ALL_USERS_FRIENDS(u,frnd) {
		// svr_name not set since the id is for a remote user on the
		// local server
		if (frnd->utype == USER_TYPE_LOCAL && frnd->id == id) {
			u->infoprintf("Your friend ~FT%04X~RS has logged on.\n",id);
			frnd->stage = FRIEND_ONLINE;
			frnd->local_user = this;
			}
		}
	}

// See if we're on any groups banned list and if so set user pointer. This
// is reset in ~cl_user().
FOR_ALL_GROUPS(grp) {
	for(gb=grp->first_ban;gb;gb=gb->next) {
		// gb->user may be set already is user has looped back
		if (gb->utype == USER_TYPE_REMOTE && gb->uid == orig_id) {
			if (!gb->user) gb->user = this;
			break;
			}
		}
	}

// Log it
log(1,"Remote user %04X (%s) from server '%s' joined.\n",
	id,name,server_from->name);

remote_user_count++;
}




/*** Destructor ***/
cl_remote_user::~cl_remote_user()
{
char *str;

// If stage still new then object creation failed, do nothing.
if (stage == USER_STAGE_NEW) return;

free(full_name);
FREE(home_svrname);
if (group) group->leave(this);

// Just in case thread is still running
cancel_thread(&ip_thrd);

allprintf(
	MSG_MISC,
	USER_LEVEL_NOVICE,
	this,"~BRREMLOGOFF:~RS ~FT%04X~RS, %s %s\n",id,name,desc);

str = (char *)(FLAGISSET(USER_FLAG_TIMEOUT) ? "timed out" : "left");
log(1,"Remote user %04X (%s) from server '%s' %s.\n",
	id,name,server_from->name,str);

remote_user_count--;
}



//////////////////////////////// I/O FUNCTIONS ///////////////////////////////

/*** Got input off the net ***/
int cl_remote_user::uread()
{
last_input_time = server_time;
UNSETFLAG(USER_FLAG_TIMEOUT_WARNING);

try {
	parse_line();
	}
catch(enum user_stages stg) {
	if (stg == USER_STAGE_DISCONNECT) disconnect();
	else {
		log(1,"INTERNAL ERROR: Caught unexpected user_stage %d in cl_remote_user::uread()",stg);
		return ERR_INTERNAL;
		}
	}
return OK;
}




/*** Write formatted string to remote user ***/
void cl_remote_user::uprintf(char *fmtstr,...)
{
char str[ARR_SIZE];
va_list args;

va_start(args,fmtstr);
vsnprintf(str,ARR_SIZE,fmtstr,args);
va_end(args);

if (server_from->send_print(remote_id,0,str) != OK)
	log(1,"ERROR: Failed to send remote print to %04X@%s",id,server_from->name);
}



///////////////////////////// MISCELLANIOUS FUNCTIONS  ///////////////////////


/*** Set users name and full_name ***/
int cl_remote_user::set_name(char *nme)
{
char *tmp,*s;

if (!nme) nme = "";

// Don't allow user to call themselves SYSTEM or allow print codes in the name
if (!strcmp(nme,SYS_USER_NAME) || has_printcode(nme)) return ERR_INVALID_NAME;

// Don't allow invalid characters
for(s = nme;*s;++s) if (*s < 32 || *s == '@') return ERR_INVALID_NAME;

if (!(tmp = strdup(nme))) return ERR_MALLOC;
FREE(full_name);
full_name = tmp;

// Allocate again , dont' want name and full_name pointing to same address
if (!(tmp = strdup(nme))) return ERR_MALLOC;
if ((int)strlen(tmp) > max_name_len) tmp[max_name_len] = '\0';
FREE(name);
name = tmp;
name_key = generate_key(name);
return OK;
}




/*** Set the users name (limited to max_name_len) and full_name (used when
     passing info to next hop servers) using non-null terminated string ***/
int cl_remote_user::set_name(char *nme, int nlen)
{
char *tmp,*s;

if (!(tmp = (char *)malloc(nlen + 1))) return ERR_MALLOC;
FREE(full_name);
full_name = tmp;
memcpy(full_name,nme,nlen);
full_name[nlen]='\0';

if (!strcmp(full_name,SYS_USER_NAME) || has_printcode(full_name)) {
	free(full_name);
	return ERR_INVALID_NAME;
	}
for(s = full_name;*s;++s) {
	if (*s < 32 || *s == '@') {
		free(full_name);
		return ERR_INVALID_NAME;
		}
	}

if (nlen > max_name_len) nlen = max_name_len;
if (!(tmp = (char *)malloc(nlen + 1))) return ERR_MALLOC;
FREE(name);
name = tmp;
memcpy(name,nme,nlen);
name[nlen]='\0';
name_key = generate_key(name);
return OK;
}




/*** Set desc using given length ***/
int cl_remote_user::set_desc(char *dsc, int dlen)
{
char *tmp;

if (!(tmp = (char *)malloc(dlen + 1))) return ERR_MALLOC;
FREE(full_desc);
full_desc = tmp;
memcpy(full_desc,dsc,dlen);
full_desc[dlen]='\0';

if (dlen > max_desc_len) dlen = max_desc_len;
if (!(tmp = (char *)malloc(dlen + 1))) return ERR_MALLOC;
free(desc);
desc = tmp;
memcpy(desc,dsc,dlen);
desc[dlen]='\0';
return OK;
}




/*** The the users original server name ***/
int cl_remote_user::set_svrname(char *snme, int snlen)
{
char *tmp;

if (!snlen) {
	FREE(home_svrname);  return OK;
	}
if (!(tmp = (char *)malloc(snlen + 1))) return ERR_MALLOC;
FREE(home_svrname);
home_svrname = tmp;
memcpy(home_svrname,snme,snlen);
home_svrname[snlen] = '\0';
return OK;
}




/*** Send leave packet and delete ourself. This is used by local server to
     get rid of a remote user object (unless we received LEFT packet in which
     case object can just be deleted) ***/
void cl_remote_user::disconnect()
{
int ret;

uprintf("\n~FTLeaving this server...\n\n");

if ((ret=server_from->send_left(remote_id)) != OK)
	log(1,"ERROR: cl_remote_user::disconnect() -> cl_server::send_left(): %s",
		err_string[ret]);

delete this;
}





///////////////////////// VIRTUAL FUNCTION COMMANDS ////////////////////////

/*** Leave the server/group ***/
void cl_remote_user::com_leave()
{
throw USER_STAGE_DISCONNECT;
}




void cl_remote_user::com_profile()
{
uprintf("Remote users cannot enter a profile.\n");
}




void cl_remote_user::com_mail()
{
uprintf("Remote users cannot use the mail system.\n");
}




void cl_remote_user::com_setemail()
{
uprintf("Remote users do not need to set an email address.\n");
}




void cl_remote_user::com_stgroup()
{
uprintf("Remotes users cannot set a starting group.\n");
}




void cl_remote_user::com_suicide()
{
uprintf("Remote users cannot suicide.\n");
}




void cl_remote_user::com_batch()
{
uprintf("Remote users do not have command batches.\n");
}




void cl_remote_user::com_lsbatches()
{
uprintf("Remote users do not have command batches.\n");
}
