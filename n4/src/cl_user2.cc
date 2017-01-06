/***************************************************************************
 FILE: cl_user2.cc
 LVU : 1.4.1

 DESC:
 This contains the user command methods. It is split from the rest of the
 cl_user class because of its size.

 Copyright (C) Neil Robertson 2003-2005

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


/*** Some local defs ***/
#define NO_SUCH_USER "There is no such user.\n"

#define NOT_LOGGED_ON "That user is not logged on.\n"

#define NO_USERS_WITH_NAME \
	"There are no users with that name. " \
	"Note that this command is case sensitive\n" \
	"and it can only be used for users currently logged onto this server.\n"

#define INVALID_USER_ID \
	"Invalid user id. Note that @<server> cannot be used in this command.\n"

#define NOT_HIGH_ENOUGH_LEVEL \
	"You are not a high enough level to use this option.\n"



/*** Just quit ***/
void cl_user::com_quit()
{
// Can't run this in a batch
if (type == USER_TYPE_LOCAL && ((cl_local_user *)this)->batchfp) 
	errprintf("Cannot execute a quit from within a batch.\n");	
else
throw USER_STAGE_DISCONNECT;
}




/*** Toggle some options on or off. 
     NOTE TO SELF: Remember to add any new flags to examine code in 
                   cl_server.cc & cl_user1.cc
 ***/
void cl_user::com_toggle()
{
char *opt[]={
	"Prompt",
	"Paging",
	"Converse",
	"Colour",
	"Nospeech",
	"Notells",
	"Noshouts",
	"Noinfo",
	"Nomisc",
	"Puip",
	"Invisible",
	"Persist",
	"Netbcast",
	"Autosilence"
	};
enum {
	OPT_PROMPT,
	OPT_PAGING,
	OPT_CONVERSE,
	OPT_COLOUR,
	OPT_NO_SPEECH,
	OPT_NO_TELLS,
	OPT_NO_SHOUTS,
	OPT_NO_INFO,
	OPT_NO_MISC,
	OPT_PUIP,
	OPT_INVISIBLE,
	OPT_HOME_GRP_PERSIST,
	OPT_NET_BCAST,
	OPT_AUTOSILENCE,

	OPT_END
	};
int flagslist[] = {
	USER_FLAG_PROMPT,
	USER_FLAG_PAGING,
	USER_FLAG_CONVERSE,
	USER_FLAG_ANSI_TERM,
	USER_FLAG_NO_SPEECH,
	USER_FLAG_NO_TELLS,
	USER_FLAG_NO_SHOUTS,
	USER_FLAG_NO_INFO,
	USER_FLAG_NO_MISC,
	USER_FLAG_PUIP,
	USER_FLAG_INVISIBLE,
	USER_FLAG_HOME_GRP_PERSIST,
	USER_FLAG_RECV_NET_BCAST,
	USER_FLAG_AUTOSILENCE
	};	
	
int i,on,ret,len;

if (com->wcnt != 2) goto USAGE;
len = strlen(com->word[1]);

for(i=0;i != OPT_END;++i) 
	if (!strncasecmp(com->word[1],opt[i],len)) break;
if (i == OPT_END) goto USAGE;

on=0;

// Check some special cases
switch(i) {
	case OPT_NO_SPEECH:
	if (FLAGISSET(USER_FLAG_NO_SPEECH)) {
		if (group != gone_remote_group)
			group->geprintf(
				MSG_MISC,this,NULL,
				"User ~FT%04X~RS (%s) is now listening.\n",
				id,name);
		}
	else 
	if (group != gone_remote_group) 
		group->geprintf(
			MSG_MISC,this,NULL,
			"User ~FT%04X~RS (%s) is no longer listening.\n",
			id,name);
	break;

	case OPT_INVISIBLE:
	if (level < go_invis_level && !FLAGISSET(flagslist[i])) {
		uprintf(NOT_HIGH_ENOUGH_LEVEL);  return;
		}
	if (FLAGISSET(USER_FLAG_INVISIBLE)) {
		if (group != gone_remote_group)
			group->geprintf(
				MSG_MISC,this,NULL,
				"~BBVISIBLE:~RS ~FT%04X~RS, %s %s\n",
				id,name,desc);
		}
	else
	if (group != gone_remote_group)
		group->geprintf(
			MSG_MISC,this,NULL,
			"~BMINVISIBLE:~RS ~FT%04X~RS, %s %s\n",id,name,desc);
	break;

	case OPT_HOME_GRP_PERSIST:
	if (type != USER_TYPE_LOCAL) {
		uprintf("This flag can only be toggled by local users.\n");
		return;
		}
	break;

	case OPT_NET_BCAST:
	if (level < recv_net_bcast_level && !FLAGISSET(flagslist[i])) {
		uprintf(NOT_HIGH_ENOUGH_LEVEL);  return;
		}
	}

if (FLAGISSET(flagslist[i])) UNSETFLAG(flagslist[i]);
else {
	SETFLAG(flagslist[i]);  on = 1;
	}
uprintf("%s mode %s\n",opt[i],offon[on]);

// If user has gone remote then send up the line
if (server_to && server_to->proto_rev > 5) {
	if ((ret = server_to->send_user_flags(this)) != OK) 
		errprintf("Unable to send flags packet: %s\n",err_string[ret]);
	}
return;

USAGE:
uprintf("Usage: toggle prompt\n");
uprintf("              paging\n");
uprintf("              converse\n");
uprintf("              colour\n");
uprintf("              nospeech\n");
uprintf("              notells\n");
uprintf("              noshouts\n");
uprintf("              noinfo\n");
uprintf("              nomisc\n");
uprintf("              puip        (Print Under Inline Prompt)\n");
uprintf("              invisible\n");
uprintf("              persist     (Home group persistence)\n");
uprintf("              netbcast\n");
uprintf("              autosilence (No speech etc heard when in mailer or board reader)\n");
}




/*** Say something ***/
void cl_user::com_say()
{
int ret;

if (com->wcnt < 2) {
	uprintf("Usage: say <text>\n");  return;
	}
if ((ret=group->speak(COM_SAY,this,com->wordptr[1])) != OK)
	errprintf("You cannot speak: %s\n",err_string[ret]);
}




/*** Do an emote ***/
void cl_user::com_emote()
{
int ret;

if (com->wcnt < 2) {
	uprintf("Usage: emote <text>\n");  return;
	}
if ((ret=group->speak(COM_EMOTE,this,com->wordptr[1])) != OK)
	errprintf("You cannot emote: %s\n",err_string[ret]);
}




/*** Do a private emote ***/
void cl_user::com_pemote()
{
tell_pemote(COM_PEMOTE);
}




/*** Do a shout emote ***/
void cl_user::com_semote()
{
shout_semote(COM_SEMOTE);
}




/*** Used by com_shout() & com_semote() ***/
void cl_user::shout_semote(int comnum)
{
cl_group *grp;
int ret;

if (com->wcnt < 2) {
	uprintf("Usage: %s <text>\n",command[comnum]);  return;
	}
if (FLAGISSET(USER_FLAG_MUZZLED)) {
	uprintf("You cannot %s while you are muzzled.\n",command[comnum]);
	return;
	}
if (FLAGISSET(USER_FLAG_NO_SHOUTS)) {
	uprintf("You cannot %s while you have NOSHOUTS on.\n",command[comnum]);
	return;
	}
if (FLAGISSET(USER_FLAG_PRISONER)) {
	uprintf("You cannot %s while you are a prisoner!\n",command[comnum]);
	return;
	}

if (comnum == COM_SEMOTE)
	sprintf(text,"~FT~OLSHOUT~RS ~FT%04X~FG:~RS %s %s\n",id,name,com->wordptr[1]);
else
	sprintf(text,"~FT~OLSHOUT~RS ~FT%04X,%s~FG:~RS %s\n",id,name,com->wordptr[1]);
allprintf(MSG_SHOUT,USER_LEVEL_NOVICE,NULL,text);

// Store in ALL group review buffers
FOR_ALL_GROUPS(grp) {
	if (grp != gone_remote_group) {
		grp->grouplog(false,text);
		if ((ret=add_review_line(grp->revbuff,&grp->revpos,text)) != OK) 
			log(1,"ERROR: Group %04X, cl_user::com_shout() -> add_revline_line(): %s",
				grp->id,err_string[ret]);
		}
	}
}




/*** Tell a specific user something ***/
void cl_user::com_tell()
{
tell_pemote(COM_TELL);
}




/*** Used by com_tell() & com_pemote() ***/
void cl_user::tell_pemote(int comnum)
{
uint16_t uid;
cl_user *u;
cl_server *svr;
int ret;

if (com->wcnt < 3) {
	uprintf("Usage: %s <user id[@<server>]> <text>\n",command[comnum]);
	return;
	}
if (FLAGISSET(USER_FLAG_NO_TELLS)) {
	uprintf("You cannot send a tell or private emote while you have NOTELLS on.\n");
	return;
	}
if (eecnt == 1 && !strcmp(com->word[1],"bunny@easter")) {
	uprintf("~BB~FG...~~~~==== ~FYHoppity hop! He's on his way!~FG ======>>>\n");
	eecnt = 2;  
	return;
	}
if ((ret=idstr_to_id_and_svr(com->word[1],&uid,&svr)) != OK) {
	errprintf("Unable to send %s: %s\n",command[comnum],err_string[ret]);
	return;
	}

// Remote pemote
if (svr) {
	if ((ret=svr->send_mesg(
		comnum == COM_TELL ? PKT_COM_TELL : PKT_COM_PEMOTE,
		0,id,uid,name,com->wordptr[2])) != OK) 
		errprintf("Unable to send %s: %s\n",
			command[comnum],err_string[ret]);
	else
	uprintf("%s sent.\n",comnum == COM_TELL ? "Tell" : "Private emote");
	return;
	}

// Local pemote
if (!(u = get_user(uid))) {
	uprintf(NOT_LOGGED_ON);  return;
	}
if (u == this) {
	uprintf("Talking to yourself? Not had your Clozapine yet today?\n");
	return;
	}

switch(u->tell(comnum == COM_TELL ? TELL_TYPE_TELL : TELL_TYPE_PEMOTE,
               id,name,NULL,com->wordptr[2])) {
	case OK:
	if (comnum == COM_TELL) 
		uprintf("You tell ~FT%04X~RS (%s): %s\n",
			u->id,u->name,com->wordptr[2]);
	else 
	uprintf("To ~FT%04X~RS (%s): %s %s\n",
		u->id,u->name,name,com->wordptr[2]);
	break;

	case ERR_USER_NO_TELLS:
	uprintf("User ~FT%04X~RS (%s) is not currently receiving tells or private emotes.\n",uid,u->name);
	break;

	case ERR_USER_AFK:
	uprintf("User ~FT%04X~RS (%s) is AFK at the moment but your %s has been stored.\n",uid,u->name,command[comnum]);
	if (u->afk_msg) uprintf("~FYAFK message:~RS %s\n",u->afk_msg);
	}
}




/*** Tell user looked up by name ***/
void cl_user::com_ntell()
{
ntell_nemote(COM_NTELL);
}



/*** Pemote user looked up by name ***/
void cl_user::com_nemote()
{
ntell_nemote(COM_NEMOTE);
}




/*** Used by com_ntell() & com_nemote() ***/
void cl_user::ntell_nemote(int comnum)
{
cl_user *u;
int i,cnt,eu;

if (com->wcnt < 3) {
	uprintf("Usage: %s \"<username>\" <text>\n",command[comnum]);
	return;
	}
if (FLAGISSET(USER_FLAG_MUZZLED)) {
	uprintf("You cannot send tells or private emotes while you are muzzled.\n");
	return;
	}
if (FLAGISSET(USER_FLAG_NO_TELLS)) {
	uprintf("You cannot send a tell or private emote while you have NOTELLS on.\n");
	return;
	}
for(i=0,cnt=0,eu=0;(u = get_user_by_name(com->word[1],i));++i) {
	if (u == this) {
		uprintf("Don't talk to yourself, you might not like what you hear!\n");
		eu = 1;
		continue;
		}
	switch(u->tell(comnum == COM_NTELL ? TELL_TYPE_TELL : TELL_TYPE_PEMOTE,
        	       id,name,NULL,com->wordptr[2])) {
		case OK:
		if (comnum == COM_NTELL) 
			uprintf("You tell ~FT%04X~RS (%s): %s\n",
				u->id,u->name,com->wordptr[2]);
		else uprintf("To ~FT%04X~RS (%s): %s %s\n",
			u->id,u->name,name,com->wordptr[2]);
		break;

		case ERR_USER_NO_TELLS:
		uprintf("User ~FT%04X~RS (%s) is not currently receiving tells or private emotes.\n",u->id,u->name);
		eu = 1;
		continue;

		case ERR_USER_AFK:
		uprintf("User ~FT%04X~RS (%s) is AFK at the moment but your %s has been stored.\n",u->id,u->name,command[comnum]);
		eu = 1;
		}
	++cnt;
	}
if (cnt) uprintf("%d tells/emotes sent.\n",cnt);
else 
if (!eu) uprintf(NO_USERS_WITH_NAME);
}




/*** Friends tell ***/
void cl_user::com_ftell()
{
ftell_femote(COM_FTELL);
}




/*** Friends emote ***/
void cl_user::com_femote()
{
ftell_femote(COM_FEMOTE);
}




/*** Used by com_ftell() & com_femote() ***/
void cl_user::ftell_femote(int comnum)
{
cl_friend *frnd;
int ret,cnt,ttype;
char *nme,idstr[MAX_SERVER_NAME_LEN+6];
u_char ptype;

if (com->wcnt < 2) {
	uprintf("Usage: %s <text>\n",command[comnum]);  return;
	}
if (!first_friend) {
	uprintf("Your friends list is empty.\n");  return;
	}
if (comnum == COM_FTELL) {
	ttype = TELL_TYPE_FRIENDS_TELL;
	ptype = PKT_COM_TELL;
	}
else {
	ttype = TELL_TYPE_FRIENDS_PEMOTE;
	ptype = PKT_COM_PEMOTE;
	}
cnt = 0;
FOR_ALL_FRIENDS(frnd) {
	if (frnd->stage == FRIEND_ONLINE) {
		if (frnd->utype == USER_TYPE_LOCAL) {
			nme = frnd->local_user->name;
			ret = frnd->local_user->tell(
				ttype,id,name,NULL,com->wordptr[1]);

			sprintf(idstr,"%04X",frnd->id);
			}
		else {
			nme = frnd->name;
			sprintf(idstr,"%04X@%s",frnd->id,frnd->svr_name);
			ret = frnd->server->send_mesg(
				ptype,1,id,frnd->id,name,com->wordptr[1]);
			}

		switch(ret) {
			case OK:
			uprintf("~FGTell/pemote sent to:~FT %s~RS, %s.\n",
				idstr,nme);
			++cnt;
			break;

			case ERR_USER_AFK:
			uprintf("User ~FT%s~RS (%s) is AFK at the moment but your tell/emote has been stored.\n",idstr,nme);
			++cnt;
			break;	

			case ERR_USER_NO_TELLS:
			uprintf("User ~FT%s~RS (%s) is not currently receiving tells or private emotes.\n",idstr,nme);
			break;

			default:
			errprintf("Unable to send tell/emote to %s, %s: %s\n",
				idstr,nme,err_string[ret]);
			}
		}
	}
uprintf("%d tells/emotes sent.\n",cnt);
}




/*** Do a shout ***/
void cl_user::com_shout()
{
shout_semote(COM_SHOUT);
}




/*** Review whats been said in the group. Paging command ***/
void cl_user::com_review()
{
if (com_page_line == -1) {
	if (com->wcnt != 1) {
		uprintf(COM_NO_ARGS);  return;
		}
	if (!group->revbuff[0].line) {
		uprintf("The review buffer is empty.\n");  return;
		}
	uprintf("\n~BB*** Review of group %04X, %s~RS~BB ***\n\n",
		group->id,group->name);
	}
page_review(group->revbuff,group->revpos);
}




/*** Show previous tells. Paging command. ***/
void cl_user::com_revtell()
{
if (com_page_line == -1) {
	if (com->wcnt != 1) {
		uprintf(COM_NO_ARGS);  return;
		}
	if (!revbuff[0].line) {
		uprintf("Your revtell buffer is empty.\n");  return;
		}
	uprintf("\n~BB*** Review of your tells & private emotes ***\n\n");
	}
page_review(revbuff,revpos);
}




/*** Used by com_review() and com_revtell() ***/
void cl_user::page_review(revline *rbuff,int rpos)
{
int i,lcnt;

/* If the current position of rpos has nothing there then that array
   element hasn't been used yet (ie it hasn't wrapped yet) so review must
   start from 0, else it starts from rpos unless com_page_line is set. */
if (com_page_line != -1) i = com_page_line;
else
i = (rbuff[rpos].line ? rpos : 0);

// Loop though lines
for(lcnt=0;com_page_line != num_review_lines;) {
	uprintf(rbuff[i].line);
	i=(i + 1) % num_review_lines;
	if (i == rpos) break;

	if (FLAGISSET(USER_FLAG_PAGING)) {
		lcnt += (strlen(rbuff[i].line) / term_cols) + 1;
		if (lcnt >= term_rows - 2) {
			com_page_line = i;  return;
			}
		}
	}

if (FLAGISSET(USER_FLAG_PAGING) && lcnt && lcnt >= term_rows - 5) {
	// Set to num_review_lines so that when we reenter we skip the loop
	com_page_line = num_review_lines;
	return;
	}

uprintf("\n~BB*** End ***\n\n");
com_page_line = -1;
}




/*** Show who's on. This command can be called from the login prompt hence
     the level checks in the paging code. Paging command. ***/
void cl_user::com_who()
{
cl_user *u;
int lcnt,ucnt;

if (com_page_line == -1) {
	if (level > USER_LEVEL_LOGIN && com->wcnt != 1) {
		uprintf(COM_NO_ARGS);  return;
		}
	uprintf("\n~BB*** Current users ***\n\n");
	uprintf("~UL~OLUID  Group Level    Name+desc. & Flags\n");
	}

lcnt = 0;
ucnt = 0;
FOR_ALL_USERS(u) {
	if (u->level > USER_LEVEL_LOGIN && ++ucnt > com_page_line) {
		if (level < u->level && O_FLAGISSET(u,USER_FLAG_INVISIBLE)) 
			uprintf("????  ???? ?      ? ?\n");
		else
		uprintf("%04X  %04X %-8s %s %s~RS   %s%s%s%s\n",
			u->id,
			u->group->id,
			user_level[u->level],
			u->name,
			u->desc,
                        u->is_afk() ? "~OL~BY(AFK)~RS " : "",
                        O_FLAGISSET(u,USER_FLAG_MUZZLED) ? "~OL~BR(MUZ)~RS " : "",
			O_FLAGISSET(u,USER_FLAG_INVISIBLE) ? "~OL~BM(INV)~RS " : "",
                        O_FLAGISSET(u,USER_FLAG_LINKDEAD) ? "~OL~BR(LINKDEAD)~RS " : "");


		if (level > USER_LEVEL_LOGIN && 
		    FLAGISSET(USER_FLAG_PAGING) && 
		    ++lcnt == term_rows - 2) {
			com_page_line = ucnt;  return;
			}
		}
	}

// Total & prompt could scroll lines in last page off top so pause at end
// if required.
if (level > USER_LEVEL_LOGIN &&
    FLAGISSET(USER_FLAG_PAGING) && lcnt && lcnt >= term_rows - 5) {
	com_page_line = ucnt;  return;
	}

uprintf("\n~FTTotal of %d users.\n\n",ucnt);
com_page_line = -1;
}




/*** Look up other user(s) by name. Multiple users may have the same name. 
     This command is not paged because its rather unlikely that dozens of
     people will have exactly the same name. ***/
void cl_user::com_whois()
{
cl_user *u;
int cnt;

if (com->wcnt != 2) {
	uprintf("Usage: whois \"<username>\"\n");  return;
	}
for(cnt=0;(u = get_user_by_name(com->word[1],cnt));++cnt) {
	if (!cnt) {
		uprintf("\n~BB*** Users matching: \"%s\" ***\n\n",com->word[1]);
		uprintf("~OL~ULUID   Name & desc\n");
		}
	uprintf("%04X  %s %s\n",u->id,u->name,u->desc);
	}
if (cnt) uprintf("\n~FTTotal of %d users.\n\n",cnt);
else
uprintf("There are no users with matching names. Note that this command can only be used\nfor users currently logged onto this server.\n");
}




/*** Show admin details of logins. Paging command. ***/
void cl_user::com_people()
{
cl_user *u;
cl_local_user *lu;
cl_remote_user *ru;
int lcnt,ucnt,invcnt,logcnt;

if (com_page_line == -1) {
	if (com->wcnt != 1) {
		uprintf(COM_NO_ARGS);  return;
		}
	uprintf("\n~BB*** User details ***\n\n");
	uprintf("~UL~OLUID  Stg Type   Level    Inv Server%*sIPnum            IPname\n",MAX_SERVER_NAME_LEN-4,"");
	}

lcnt = 0;
ucnt = 0;
logcnt = 0;
invcnt = 0;
FOR_ALL_USERS(u) {
	logcnt += (u->level == USER_LEVEL_LOGIN);
	invcnt += O_FLAGISSET(u,USER_FLAG_INVISIBLE);

	if (++ucnt <= com_page_line) continue;

	if (level < u->level && O_FLAGISSET(u,USER_FLAG_INVISIBLE))
		uprintf("????   ? ?      ?        %s ?           ?                ?\n",scolnoyes[1]);
	else
	if (u->type == USER_TYPE_LOCAL) { 
		lu = (cl_local_user *)u;
		uprintf("%04X  %2d LOCAL  %-8s %s -%*s  %-15s  %s\n",
			u->id,
			u->stage,
			user_level[u->level],
			scolnoyes[O_FLAGISSET(u,USER_FLAG_INVISIBLE)],
			MAX_SERVER_NAME_LEN-1,"",lu->ipnumstr,lu->ipnamestr);
		}
	else {
		ru = (cl_remote_user *)u;
		uprintf("%04X  %2d REMOTE %-8s %s %-*s  %-15s  %s\n",
			u->id,
			u->stage,
			user_level[u->level],
			scolnoyes[O_FLAGISSET(u,USER_FLAG_INVISIBLE)],
			MAX_SERVER_NAME_LEN,ru->server_from->name,
			u->ipnumstr,u->ipnamestr);
		}

	if (FLAGISSET(USER_FLAG_PAGING) && ++lcnt == term_rows - 2) {
		com_page_line = ucnt;  return;
		}
	}

if (FLAGISSET(USER_FLAG_PAGING) && lcnt && lcnt >= term_rows - 5) {
	com_page_line = ucnt;  return;
	}

uprintf("\n~FTTotal of %d users (%d local (%d logons), %d remote, %d invisible).\n\n",
	local_user_count + remote_user_count,
	local_user_count, logcnt, remote_user_count, invcnt);
com_page_line = -1;
}




/*** List all groups. Paging command. ***/
void cl_user::com_lsgroups()
{
cl_group *g;
int lcnt,gcnt;
char *priv,*fixed;

if (com_page_line == -1) {
	if (com->wcnt != 1) {
		uprintf(COM_NO_ARGS);  return;
		}
	uprintf("\n~BB*** Current groups ***\n\n");
	uprintf("~UL~OLGID  Type   Owner Prv Fxd Usrs Msgs Name\n");
	}

lcnt = 0;
gcnt = 0;
FOR_ALL_GROUPS(g) {
	if (++gcnt > com_page_line) {
		if (g->owner) sprintf(text,"%04X",g->owner->id);
		else strcpy(text," SYS");

		// Private/fixed is irrelevant for system groups
		priv = (char *)((g->type == GROUP_TYPE_SYSTEM) ?
		        "-" : noyes[O_FLAGISSET(g,GROUP_FLAG_PRIVATE)]);
		fixed = (char *)((g->type == GROUP_TYPE_SYSTEM) ?
		         "-" : noyes[O_FLAGISSET(g,GROUP_FLAG_FIXED)]);

		uprintf("%04X %s  %s %3s %3s %4d %4d %s\n",
			g->id,
			group_type[g->type],
			text,
			priv,fixed,
			g->ucnt,
			g->board ? g->board->msgcnt : 0,
			g->name);

		if (FLAGISSET(USER_FLAG_PAGING) && 
		    ++lcnt == term_rows - 2) {
			com_page_line = gcnt;  return;
			}
		}
	}

if (FLAGISSET(USER_FLAG_PAGING) && lcnt && lcnt >= term_rows - 5) {
	com_page_line = gcnt;  return;
	}

uprintf("\n~FTTotal of %d groups (%d system, %d public, %d user).\n\n",
	system_group_count + public_group_count + user_group_count,
	system_group_count,public_group_count,user_group_count);

com_page_line = -1;
}




/*** List friends. Paging command. ***/
void cl_user::com_lsfriends()
{
cl_friend *frnd;
char *status[]={
	"~FYUNKNOWN ",
	"~FTLOCATING",
	"~FGONLINE  ",
	"~FROFFLINE "
	};
char *fname,*fdesc;
char idstr[MAX_SERVER_NAME_LEN+6];
uint16_t gid;
int lcnt,fcnt;

if (com_page_line == -1) {
	if (com->wcnt != 1) {
		uprintf(COM_NO_ARGS);  return;
		}
	if (!first_friend) {
		uprintf("There are no entries in your friends list.\n");
		return;
		}
	uprintf("\n~BB*** Your friends ***\n\n");

	uprintf("~UL~OLUID%*sStatus   Group Name & desc.\n",MAX_SERVER_NAME_LEN+3,"");
	}

lcnt = 0;
fcnt = 0;
FOR_ALL_FRIENDS(frnd) {
	if (++fcnt <= com_page_line) continue; 

	if (frnd->utype == USER_TYPE_REMOTE) {
		// Remote friend
		sprintf(idstr,"%04X@%s",frnd->id,frnd->svr_name);
		fname = (frnd->name ? frnd->name : (char *)"<unknown>");
		fdesc = (frnd->desc ? frnd->desc : (char *)"");
		gid = frnd->remote_gid;
		}
	else {
		// Local friend
		sprintf(idstr,"%04X",frnd->id);
		if (frnd->local_user) {
			fname = frnd->local_user->name;
			fdesc = frnd->local_user->desc;
			gid = frnd->local_user->group->id;
			}
		else {
			fname = (char *)"<unknown>";
			fdesc = (char *)"";
			gid = 0;
			}
		}

	uprintf("%-*s%s~RS  %04X %s %s\n",
		MAX_SERVER_NAME_LEN+6,
		idstr,status[frnd->stage],gid,fname,fdesc);

	if (FLAGISSET(USER_FLAG_PAGING) && ++lcnt == term_rows - 2) {
		com_page_line = fcnt;  return;
		}
	}

if (FLAGISSET(USER_FLAG_PAGING) && lcnt && lcnt >= term_rows - 5) {
	com_page_line = fcnt;  return;
	}
	
uprintf("\n~FTTotal of %d friends.\n\n",fcnt);
com_page_line = -1;
}




/*** Add or delete friend(s) from users list ***/
void cl_user::com_friend()
{
cl_friend *frnd,*fnext;
int i,ret;

if (com->wcnt < 3) goto USAGE;

if (!strncasecmp(com->word[1],"add",strlen(com->word[1]))) {
	for(i=2;i < com->wcnt;++i) {
		if ((ret=add_friend(com->word[i],0)) != OK) 
			errprintf("Unable to add friend %s: %s\n",
				com->word[i],err_string[ret]);
		}
	return;
	}
if (!strncasecmp(com->word[1],"delete",strlen(com->word[1]))) {
	// Don't need to check each friend for valid id since if it wasn't
	// it wouldn't ever get on the list in the first place.
	for(i=2;i < com->wcnt;++i) {
		for(frnd=first_friend;frnd;frnd=fnext) {
			fnext=frnd->next;
			if (frnd->utype == USER_TYPE_REMOTE) 
				sprintf(text,"%04X@%s",frnd->id,frnd->svr_name);
			else sprintf(text,"%04X",frnd->id);

			if (!strcasecmp(text,com->word[i])) {
				remove_list_item(first_friend,last_friend,frnd);	
				uprintf("Friend ~FT%s ~OL~FRDELETED.\n",text);
				break;
				}
			}
		if (!frnd) 
			uprintf("Friend %s is not in your list.\n",com->word[i]);
		}
	return;
	}
// Fall through

USAGE:
uprintf("Usage: friend add/delete <friend id[@<server>]> [<friend id[@<server>]> ...]\n");
}




/*** Join a group locally or on a remote server ***/
void cl_user::com_join()
{
cl_group *grp;
cl_server *svr;
uint16_t gid;
int ret,len;
char *usage = "Usage: join <group id[@<server>]>/@<server>/home/prev\n";

if (com->wcnt != 2) {
	uprintf(usage);  return;
	}
if (FLAGISSET(USER_FLAG_PRISONER)) {
	if (release_time) 
		uprintf("You are in prison and have %s of your sentence left to serve, you cannot leave!\n",time_period(release_time - server_time));
	else uprintf("You are in prison indefinitely, you cannot leave!\n");
	return;
	}

len = strlen(com->word[1]);
if (!strncasecmp(com->word[1],"home",len)) {
	if (group == home_group) 
		uprintf("You are already in your home group.\n");
	else {
		uprintf("You return to your home group...\n");
		home_group->join(this);
		}
	return;
	}
if (!strncasecmp(com->word[1],"prev",len)) {
	if (!prev_group) 
		uprintf("You have no previous group to return to.\n");
	else join_ujoin(prev_group);
	return;
	}

// If just giving server then get its name
if (com->word[1][0] == '@') {
	if (!com->word[1][1]) {
		uprintf(usage);  return;
		}
	if (!(svr = get_server(com->word[1] + 1))) {
		uprintf("No such server.\n");  return;
		}
	gid = 0;
	}
else
if ((ret=idstr_to_id_and_svr(com->word[1],&gid,&svr)) != OK) {
	errprintf("Unable to join: %s\n",err_string[ret]);  return;
	}

// Remote group
if (svr) {
	// Don't allow remote joining in batch files as we probaby won't
	// read any confirmation packets until the batch has completed.
	if (type == USER_TYPE_LOCAL && ((cl_local_user *)this)->batchfp) {
		errprintf("Cannot execute a remote join from within a batch.\n");
		return;
		}
	if ((ret=svr->send_join(this,gid)) != OK) 
		errprintf("Unable to send join request: %s\n",err_string[ret]);
	else uprintf("Request sent.\n");
	return;
	}
if (!(grp = get_group(gid))) {
	uprintf(NO_SUCH_GROUP);  return;
	}
join_ujoin(grp);
}




/*** Join a specific user ***/
void cl_user::com_ujoin()
{
cl_server *svr;
cl_user *u;
uint16_t uid;
int ret;

if (com->wcnt != 2) {
	uprintf("Usage: ujoin <user id[@<server>]>\n");  return;
	}
if (FLAGISSET(USER_FLAG_PRISONER)) {
	if (release_time) 
		uprintf("You are in prison and have %s of your sentence left to serve, you cannot leave!\n",time_period(release_time - server_time));
	else uprintf("You are in prison indefinitely, you cannot leave!\n");
	return;
	}
if ((ret=idstr_to_id_and_svr(com->word[1],&uid,&svr)) != OK) {
	errprintf("Unable to join user: %s\n",err_string[ret]);  return;
	}

// If remote user join then send message to remote server
if (svr) {
	if (type == USER_TYPE_LOCAL && ((cl_local_user *)this)->batchfp) {
		errprintf("Cannot execute a remote join from within a batch.\n");
		return;
		}
	if ((ret=svr->send_ujoin(this,uid)) != OK) 
		errprintf("Unable to send join request: %s\n",err_string[ret]);
	else uprintf("Request sent.\n");
	return;
	}

if (!(u = get_user(uid))) {
	uprintf(NOT_LOGGED_ON);  return;
	}
join_ujoin(u->group);
}




/*** Used by join and ujoin commands ***/
void cl_user::join_ujoin(cl_group *grp)
{
int inv;

if (grp == group) {
	uprintf("You are already in group %04X.\n",grp->id);
	return;
	}
if (grp == gone_remote_group ||
    (grp == prison_group && level < group_gatecrash_level)) {
	uprintf("You cannot join group %04X.\n",grp->id);
	return;
	}
if (grp->user_is_banned(this)) {
	uprintf("You are banned from group %04X!\n",grp->id);
	return;
	}

// User can join a private group if it is their home group,
// they are >= gatecrash level or have an invite
if (!grp->user_can_join(this,&inv)) {
	uprintf("Group %04X is currently private.\n",grp->id);
	return;
	}
if (inv != -1) invite[inv].grp = NULL;
grp->join(this);
}




/*** Do help ***/
void cl_user::com_help()
{
char path[MAXPATHLEN];
int ret;

switch(com->wcnt) {
	case 1:
	sprintf(path,"%s/%s",HELP_DIR,MAIN_HELP_FILE);
	if ((ret=page_file(path,1)) != OK) 
		errprintf("Cannot display help: %s\n",err_string[ret]);
	return;

	case 2:
	if (!strcasecmp(com->word[1],"commands")) help_show_commands();
	else
	if (!strcasecmp(com->word[1],"credits")) help_credits();
	else 
	help_command_or_topic();

	return;
	}
uprintf("Usage: help [<command or topic name>/commands/credits]\n");
}




/*** Show the list of commands ***/
void cl_user::help_show_commands()
{
int lev,i,width,word_width;

// Max width of a command name first then add 1
for(i=0,word_width=0;i < NUM_COMMANDS;++i)
	if (command_level[i] <= level && (int)strlen(command[i]) >= word_width) 
		word_width = strlen(command[i])+1;

uprintf("\n~BB*** Commands available ***\n");

for(lev=USER_LEVEL_NOVICE;lev <= level;++lev) {
	/* Writing into text array then printing it out at the end of each
	   line is more efficient for when transmitting the result over a
	   network than printing out one word at a time */
	sprintf(text,"\n~FYLevel: ~FM%s\n",user_level[lev]);

	for(i=0,width=word_width;i < NUM_COMMANDS;++i) {
		if (command_level[i] == lev) {
			sprintf(text,"%s%-*s ",text,word_width-1,command[i]);

			if ((width += word_width) >= term_cols) {
				uprintf("%s\n",text);
				text[0] = '\0';
				width = word_width;
				}
			}
		}
	if (width != word_width) uprintf("%s\n",text);
	}
uprintf("\n");
}




/*** Show a specific command or topic help file ***/
void cl_user::help_command_or_topic()
{
char *s,path[MAXPATHLEN];
int ret;

// All files in the help directory must be lower case 
for(s=com->word[1];*s;++s) *s = tolower(*s);
if (!valid_filename(s)) {
	uprintf("Invalid command or topic.\n");  return;
	}
sprintf(path,"%s/%s",HELP_DIR,com->word[1]);
if ((ret=page_file(path,1)) != OK) 
	uprintf("Sorry, there is no help on that command or topic.\n");
}




/*** Display the credits. DO NOT DELETE or I'll get fucking pissed off with
     you. If you wish to ADD your own credits due to any mods you've made 
     please put them AFTER mine. ***/
void cl_user::help_credits()
{
uprintf("\n~BB                              ~BM NUTS-IV Credits ~BB                               \n\n");
uprintf("~OLNUTS-IV version %s\n",VERSION);
uprintf("~OLCopyright (C) Neil Robertson 2003-2005\n\n");
uprintf("This project was started in August 2002 and this version of the code you are\n");
uprintf("using was released... sometime later. Though vowing back in 1996 that I was\n");
uprintf("walking away from talker programming for good I had an idea for a method to\n");
uprintf("implement an (almost) network transparent talker system whereby you could\n");
uprintf("traverse a network of linked talkers (linked in either chain, hub or random\n");
uprintf("layout) from one to another (unlike on the NUTS 3 system where only 1 hop from\n");
uprintf("home was permitted) and use these talkers as if they were your home system\n");
uprintf("(with certain restrictions). This interested me enough to write a whole new\n");
uprintf("talker system to implement it and NUTS-IV is the result. I hope you like it!\n\n");
uprintf("~FW~BRNeil Robertson, ~FK~BWLondon, ~FW~BBEngland\n\n");
uprintf("~FMEmail     : ~FGneel@ogham.demon.co.uk~RS  (neil@ogham... disabled due to spam)\n");
uprintf("~FMWeb       : ~FYhttp://www.ogham.demon.co.uk/nuts4\n");
uprintf("~FMNewsgroups: ~FTalt.talkers.nuts, alt.talkers.programming\n");
uprintf("~BB                                                                              \n\n");
}




/*** Return version number & other stuff ***/
void cl_user::com_version()
{
uprintf("NUTS-IV version   : %s\n",VERSION);
uprintf("NIVN protocol rev.: %d\n",PROTOCOL_REVISION);
}




/*** Show whos in the same group. Just calls look() method. ***/
void cl_user::com_look()
{
uint16_t gid;

if (com_page_line == -1) {
	switch(com->wcnt) {
		case 1: look_group = group;  break;

		case 2:
		/* Let user view any group since they can see whos in what
		   group in "who" anyway whether they have permission to join
		   the group or not. */
		if (!(gid = idstr_to_id(com->word[1]))) {
			uprintf("Invalid group id.\n");  return;
			}
		if (!(look_group = get_group(gid))) {
			uprintf(NO_SUCH_GROUP);  return;
			}
		break;

		default:
		uprintf("Usage: look [<group id>]\n");
		return;
		}
	}
look(look_group,1);
}




/*** Show server list ***/
void cl_user::com_lsservers()
{
cl_server *svr;
char *inout[]={ " ~FMIN","~FTOUT" };

if (com->wcnt != 1) {
	uprintf(COM_NO_ARGS);  return;
	}
if (!first_server) {
	uprintf("There are no servers in the list.\n");  return;
	}

uprintf("\n~BB*** Server list ***\n\n");
uprintf("~UL~OLName%-*sId Typ EL State           LU   RU  PG Address\n",MAX_SERVER_NAME_LEN-3,"");

FOR_ALL_SERVERS(svr) 
	uprintf("%-*s %X %s~RS  %d %s~RS %4d %4d %3d %s:%d\n",
		MAX_SERVER_NAME_LEN+1,
		svr->name,
		svr->id,
		inout[svr->connection_type],
		svr->encryption_level,
		svr_stage_str[svr->stage],
		svr->local_user_cnt,
		svr->remote_user_cnt,
		svr->pub_group_cnt,
		svr->ipstr,
		ntohs(svr->ip_addr.sin_port));

uprintf("\n~FTTotal of %d servers.\n\n",server_count);
}




/*** Give details about a specific server ***/
void cl_user::com_svrinfo()
{
cl_server *svr;
char *inout[] = { "~FMINCOMING","~FTOUTGOING" };
int con;

if (com->wcnt !=2) {
	uprintf("Usage: svrinfo <server>\n");  return;
	}
if (!(svr = get_server(com->word[1]))) {
	uprintf("Server not in list.\n");  return;
	}
con = (svr->stage == SERVER_STAGE_CONNECTED);

uprintf("\n~BB*** Details of server '%s' ***\n\n",svr->name);

uprintf("Id                   : %X\n",svr->id);
uprintf("Type                 : %s\n",inout[svr->connection_type]);
uprintf("Stage                : %s\n",svr_stage_str[svr->stage]);
uprintf("IP name              : %s\n",svr->ipnamestr);
uprintf("IP address           : %s:%d\n",
	svr->ipnumstr,ntohs(svr->ip_addr.sin_port));
uprintf("Local IP address     : %s:%d\n",
	inet_ntoa(svr->local_ip_addr.sin_addr),svr->local_ip_addr.sin_port);
uprintf("Socket number        : %d\n",svr->sock);
uprintf("Version              : %s\n",svr->version);
uprintf("NIVN protocol rev.   : %d\n",svr->proto_rev);
uprintf("Encryption level     : %d%s\n",
	svr->encryption_level,
	svr->proto_rev && svr->proto_rev < 5 ? "  (not supported)" : "");
uprintf("Connected on         : %s",
	con ? ctime(&svr->connect_time) : "<not connected>\n");
if (level == USER_LEVEL_ADMIN)
	uprintf("Connect key          : %u\n",svr->connect_key);
uprintf("Total users          : %d (%d local, %d remote)\n",
	svr->local_user_cnt + svr->remote_user_cnt,
	svr->local_user_cnt,svr->remote_user_cnt);
uprintf("Lockout level        : %s\n",user_level[svr->svr_lockout_level]);
uprintf("Remote user max level: %s\n",user_level[svr->svr_rem_user_max_level]);
uprintf("System groups        : %d\n",svr->sys_group_cnt);
uprintf("Public groups        : %d\n",svr->pub_group_cnt);
uprintf("Server links         : %d\n",svr->svr_links_cnt);
uprintf("Ping interval        : %s\n",
	svr->ping_interval ? time_period(svr->ping_interval) : "<unknown>");
uprintf("Last RX              : %s ago\n",
	con ? time_period(server_time - svr->last_rx_time) : "0 seconds");
uprintf("Last TX              : %s ago\n",
	con ? time_period(server_time - svr->last_tx_time) : "0 seconds");

uprintf("RX / TX packets      : %u%s/ %u %s\n",
	svr->rx_packets,
	O_FLAGISSET(svr,SERVER_FLAG_RX_WRAPPED) ? " ~FY(wrapped)~RS " : " ",
	svr->tx_packets,
	O_FLAGISSET(svr,SERVER_FLAG_TX_WRAPPED) ? "~FY(wrapped)" : "");

uprintf("RX / TX errors       : %u / %u\n",svr->rx_err_cnt,svr->tx_err_cnt);
uprintf("RX period packets    : %u\n\n",svr->rx_period_packets);
}




/*** Maintain the server connections ***/
void cl_user::com_server()
{
char *subcom[] = {
	"add",
	"delete",
	"connect",
	"disconnect",
	"ping"
	};
enum {
	SC_ADD,
	SC_DELETE,
	SC_CONNECT,
	SC_DISCONNECT,
	SC_PING,

	SC_END
	};
int i,len,do_connect,do_encrypt;
	
if (com->wcnt < 3) goto USAGE;

len = strlen(com->word[1]);
for(i=0;i != SC_END;++i) 
	if (!strncasecmp(com->word[1],subcom[i],len)) break;

if (com->wcnt != 3 && (i == SC_DELETE || i == SC_DISCONNECT)) goto USAGE;

switch(i) {
	case SC_ADD: 
	do_connect = 0;
	do_encrypt = 0;

	// Add a server to the list.
	switch(com->wcnt) {
		case 7:  break;

		case 8:
		case 9:
		if (!strncasecmp(com->word[7],"connect",strlen(com->word[7])))
			do_connect = 1;
		else
		if (!strncasecmp(com->word[7],"encrypted",strlen(com->word[7])))
			do_encrypt = 1;
		else
		goto USAGE;

		if (com->wcnt == 9) {
			if (!strncasecmp(
				com->word[8],"connect",strlen(com->word[8])))
				do_connect = 1;
			else
			if (!strncasecmp(
				com->word[8],"encrypted",strlen(com->word[8])))
				do_encrypt = 1;
			else
			goto USAGE;
			}
		break;

		default: goto USAGE;
		}
	server_add(do_connect,do_encrypt);
	return;


	case SC_DELETE:
	server_delete();
	return;


	case SC_CONNECT:
	switch(com->wcnt) {
		case 3: do_encrypt = 2;  break; // Remain in current state
		case 4:
		if (!strncasecmp(
			com->word[3],"unencrypted",strlen(com->word[3]))) { 
			do_encrypt = 0;  break;
			}
		else
		if (!strncasecmp(
			com->word[3],"encrypted",strlen(com->word[3]))) {
			do_encrypt = 1;  break;
			}
		// Fall through

		default: goto USAGE;
		}
	server_connect(do_encrypt);
	return;


	case SC_DISCONNECT:
	server_disconnect();
	return;


	case SC_PING:
	switch(com->wcnt) {
		case 3:
		if (!strncasecmp(com->word[2],"cancel",strlen(com->word[2]))) {
			if (ping_svr) {
				uprintf("Continuous ping cancelled.\n");
				ping_svr = NULL;
				}
			else
			uprintf("You are not currently pinging any server.\n");
			return;
			}
		server_ping(0);
		return;

		case 4:
		if (!strncasecmp(
			com->word[3],"continuous",strlen(com->word[3]))) {
			server_ping(1);
			return;
			}
		// Fall through

		default: goto USAGE;
		}
	return;
	}

USAGE:
uprintf("Usage: server add        <server> <ip addr> <port> <local port> <connect key> \\\n              [encrypted] [connect]\n");
uprintf("       server delete     <server>\n");
uprintf("       server connect    <server> [encrypted | unencrypted]\n");
uprintf("       server disconnect <server>\n");
uprintf("       server ping       <server> [continuous]\n");
uprintf("                         cancel\n");
}




/*** Add a server to the list ***/
void cl_user::server_add(int do_connect, int do_encrypt)
{
cl_server *svr;
uint32_t conkey;
int svr_port,loc_port;

svr_port = SERVER_PORT;
loc_port = 0;
conkey = 0;

if (strlen(com->word[2]) > MAX_SERVER_NAME_LEN) {
	uprintf("Server name too long, maximum length is %d characters.\n",
		MAX_SERVER_NAME_LEN);
	return;
	}

// Port. Can be blank, in which case we default to macro value
if (com->word[4][0] &&
    (!is_integer(com->word[4]) || 
    (svr_port = atoi(com->word[4])) < 1 || svr_port > 65535)) {
	uprintf("Invalid port number, range is 1 to 65535.\n");
	return;
	}

// Local port.
if (com->word[5][0] &&
    (!is_integer(com->word[5]) || 
    (loc_port = atoi(com->word[5])) < 1 || loc_port > 65535)) {
	uprintf("Invalid local port number, range is 1 to 65535.\n");
	return;
	}

// Connect key
if (com->word[6][0] && 
    (!is_integer(com->word[6]) || (conkey = atoi(com->word[6])) < 0)) {
	uprintf("Invalid connect key.\n");
	return;
	}

if ((svr=new cl_server(
	com->word[2],
	com->word[3],
	(uint16_t)svr_port,
	(uint16_t)loc_port,
	conkey,
	do_encrypt ? MAX_ENCRYPTION_LEVEL : 0,
	this))->create_error != OK) {
	errprintf("Unable to create server object: %s\n",
		err_string[svr->create_error]);
	log(1,"ERROR: Unable to create new server object in cl_user::com_server(): %s\n",err_string[svr->create_error]);
	delete svr;
	return;
	}

log(1,"User %04X (%s) ADDED server '%s'.",id,name,svr->name);
uprintf("Server added.\n");

if (do_connect) {
	uprintf("Connect thread spawned.\n");
	svr->spawn_thread(THREAD_TYPE_CONNECT);
	}
}




/*** Delete a server from the list ***/
void cl_user::server_delete()
{
cl_server *svr;

// Delete a server from list
if (!(svr = get_server(com->word[2]))) {
	uprintf("Server not in list.\n");  return;
	}
if (svr->stage == SERVER_STAGE_CONNECTED) {
	uprintf("Disconnecting and deleting server '%s'.\n",svr->name);
	log(1,"User %04X (%s) DISCONNECTING and DELETING server '%s'.",
		id,name,svr->name);
	svr->disconnect(SERVER_STAGE_MANUAL_DISCONNECT);
	}
else {
	uprintf("Deleting server '%s'.\n",svr->name);
	log(1,"User %04X (%s) DELETING server '%s'.",id,name,svr->name);
	}
svr->stage = SERVER_STAGE_DELETE;
}




/*** Connect a server ***/
void cl_user::server_connect(int do_encrypt)
{
cl_server *svr;
int ret;

if (!(svr = get_server(com->word[2]))) {
	uprintf("Server not in list.\n");  return;
	}

switch(svr->stage) {
	case SERVER_STAGE_INCOMING:
	uprintf("Server is an incoming connection.\n");  return;

	case SERVER_STAGE_CONNECTING:
	uprintf("Server is already connecting.\n");  return;

	case SERVER_STAGE_CONNECTED:
	uprintf("Server is already connected.\n");  return;
	}

switch(do_encrypt) {
	case 0: svr->start_encryption_level = 0;  break;
	case 1: svr->start_encryption_level = MAX_ENCRYPTION_LEVEL;
	}
if (!svr->user) svr->user = this;
if ((ret=svr->spawn_thread(THREAD_TYPE_CONNECT)) != OK) {
	errprintf("Unable to spawn connect thread: %s\n",err_string[ret]);
	svr->user = NULL;
	}
else {
	log(1,"User %04X (%s) spawning CONNECT thread for server '%s'...",
		id,name,svr->name);
	uprintf("Connect thread spawned.\n");
	}
}




/*** Disconnect a server ***/
void cl_user::server_disconnect()
{
cl_server *svr;

if (!(svr = get_server(com->word[2]))) {
	uprintf("Server not in list.\n");  return;
	}
if (svr->stage != SERVER_STAGE_CONNECTED) {
	uprintf("Server is not connected.\n");  return;
	}
uprintf("Disconnecting server.\n");
log(1,"User %04X (%s) DISCONNECTED server '%s'.",id,name,svr->name);
svr->disconnect(SERVER_STAGE_MANUAL_DISCONNECT);
}




/*** Ping a server manually ***/
void cl_user::server_ping(int continuous)
{
cl_server *svr;
int ret;

if (!(svr = get_server(com->word[2]))) {
	uprintf("Server not in list.\n");  return;
	}
if (!continuous) {
	if ((ret = svr->send_ping(this)) != OK) 
		errprintf("Unable to ping server: %s\n",err_string[ret]);
	return;
	}
if (svr == ping_svr) {
	uprintf("You are already pinging that server.\n");  return;
	}
if (ping_svr) uprintf("Switching pinging to server: %s\n",svr->name);
else uprintf("Pinging server: %s\n",svr->name);

ping_svr = svr;
}




/*** Change the users name ***/
void cl_user::com_name()
{
char *nme;
int ret;

if (com->wcnt != 2) {
	uprintf("Usage: name \"<new name>\"\n");  return;
	}

// Trim head and tail spaces
nme = trim(com->word[1]);
if (!nme[0]) {
	uprintf("Name is too short.\n");  return;
	}
if ((int)strlen(nme) > max_name_len) {
	uprintf("Name is too long, maximum length is %d characters.\n",
		max_name_len);
	return;
	}

if ((ret=set_name(nme)) != OK) 
	errprintf("Unable to set name: %s\n",err_string[ret]);
else {
	uprintf("Name set to \"%s\"\n",name);

	// This won't happen much so I'm happy to waste a bit of bandwith
	// and send entire user info packet
	send_user_info_to_servers(this);
	}
}




/*** Change the users description ***/
void cl_user::com_desc()
{
int ret;

if (com->wcnt != 2) {
	uprintf("Usage: desc \"<new description>\"\n");  return;
	}

if ((int)strlen(com->word[1]) > max_desc_len) {
	uprintf("Description too long, maximum length is %d characters.\n",
		max_desc_len);
	return;
	}
if ((ret=set_desc(com->word[1])) != OK) 
	errprintf("Unable to set description: %s\n",err_string[ret]);
else {
	uprintf("Description set to \"%s~RS\"\n",desc);
	send_user_info_to_servers(this);
	}
}




/*** Set the name of the group the user is currently in ***/
void cl_user::com_gname()
{
int ret;

if (com->wcnt != 2) {
	uprintf("Usage: gname \"<new group name>\"\n");  return;
	}
if (!group->user_can_modify(this)) {
	uprintf("You cannot change the name of this group.\n");  return;
	}
if ((int)strlen(com->word[1]) > max_group_name_len) {
	uprintf("Name too long, maximum length is %d characters.\n",
		max_group_name_len);
	return;
	}
if ((ret=group->set_name(com->word[1])) != OK) {
	errprintf("Unable to set group name: %s\n",err_string[ret]);
	return;
	}

uprintf("Group name set to \"%s~RS\".\n",com->word[1]);
group->geprintf(MSG_INFO,this,NULL,
	"Group name set to \"%s~RS\" by %04X (%s).\n",
	com->word[1],id,name);

if ((ret = group->save()) != OK) 
	errprintf("Unable to save group info: %s\n",err_string[ret]);

if (group->type != GROUP_TYPE_USER) 
	log(1,"User %04X (%s) changed the name of group %04X to \"%s\"",
		id,name,group->id,group->name);
}




/*** Set group description ***/
void cl_user::com_gdesc()
{
if (com->wcnt != 1) {
	uprintf(COM_NO_ARGS);  return;
	}
if (!group->user_can_modify(this)) {
	uprintf("You cannot change the description of this group.\n");
	return;
	}
editor = new cl_editor(this,EDITOR_TYPE_GROUP_DESC,NULL);
if (editor->error != OK) {
	errprintf("Editor failure: %s\n",err_string[editor->error]);
	delete editor;
	}
}




/*** Set group to private ***/
void cl_user::com_private()
{
int ret;

if (com->wcnt != 1) {
	uprintf(COM_NO_ARGS);  return;
	}
if ((ret = group->set_private()) != OK) {
	errprintf("Can't set group to private: %s\n",err_string[ret]);
	return;
	}
group->geprintf(
	MSG_INFO,this,NULL,
	"User ~FT%04X~RS (%s) has set this group to ~OL~FRPRIVATE.\n",id,name);
uprintf("Group now ~OL~FRPRIVATE\n");
}




/*** Set group to public ***/
void cl_user::com_public()
{
int ret;

if (com->wcnt != 1) {
	uprintf(COM_NO_ARGS);  return;
	}
if ((ret = group->set_public()) != OK) {
	errprintf("Can't set group to public: %s\n",err_string[ret]);
	return;
	}
group->geprintf(
	MSG_INFO,this,NULL,
	"User ~FT%04X~RS (%s) has set this group to ~OL~FGPUBLIC.\n",id,name);
uprintf("Group now ~OL~FGPUBLIC\n");
}




/*** Invite a user into the group ***/
void cl_user::com_invite()
{
cl_friend *frnd;
uint16_t uid;
cl_user *u;
int cnt,flag;

if (com->wcnt != 2) {
	uprintf("Usage: invite <user id>/friends\n");  return;
	}
if (!group->user_can_modify(this)) {
	uprintf("You cannot invite people into this group.\n");  return;
	}
if (!strncasecmp(com->word[1],"friends",strlen(com->word[1]))) uid = 0;
else {
	if (!strcmp(com->word[1],"bunny")) {
		uprintf("~LI~BG*** ~BMHow thoughtful!~BG ***\n");
		eecnt = 1;
		return;
		}
	if (!(uid = idstr_to_id(com->word[1]))) {
		uprintf(INVALID_USER_ID);  return;
		}
	if (uid == id) {
		uprintf("Sorry, but Mr T says: You cannot invite yourself, fool!\n");
		return;
		}
	if (!(u = get_user(uid))) {
		uprintf(NOT_LOGGED_ON);  return;
		}
	}

if (!O_FLAGISSET(group,GROUP_FLAG_PRIVATE)) {
	uprintf("This group is not currently private.\n");  return;
	}

// Send invite
if (uid) {
	invite_ninvite(u);  return;
	}
cnt = 0;
flag =0;
FOR_ALL_FRIENDS(frnd) {
	// Can't invite offline or remote friends
	if (frnd->stage == FRIEND_ONLINE  && frnd->local_user) {
		if (!flag) uprintf("Inviting local, online friends...\n\n");
		flag = 1;
		cnt += invite_ninvite(frnd->local_user);
		}
	}

if (cnt) uprintf("%d friends invited.\n",cnt);
else {
	if (flag) uprintf("\nNone of your friends could be invited or needed inviting.\n");
	else uprintf("You have no local, online friends to invite.\n");
	}
}




/*** Invite user(s) by name ***/
void cl_user::com_ninvite()
{
cl_user *u;
int i,cnt;

if (com->wcnt != 2) {
	uprintf("Usage: invite <username>\n");  return;
	}
if (!group->user_can_modify(this)) {
	uprintf("You cannot invite people into this group.\n");  return;
	}
if (!O_FLAGISSET(group,GROUP_FLAG_PRIVATE)) {
	uprintf("This group is not currently private.\n");  return;
	}

// Loop through users by name. pos variable only gets increased if we miss
// out a user entry in list so we have to skip it next time.
for(i=0,cnt=0;(u = get_user_by_name(com->word[1],i));++i) {
	if (u == this) uprintf("Only schitzophrenics can invite themselves.\n");
	else cnt += invite_ninvite(u); 
	}
if (i) uprintf("%d users invited.\n",cnt);
else uprintf(NO_USERS_WITH_NAME);
}




/*** Used by com_invite() & com_ninvite() ***/
int cl_user::invite_ninvite(cl_user *u)
{
int i;

if (u->level >= group_gatecrash_level) {
	uprintf("User %04X (%s) does not need an invitation to join this group.\n",u->id,u->name);
	return 0;
	}
if (group->user_is_banned(u)) {
	uprintf("User %04X (%s) is currently banned from this group.\n",
		u->id,u->name);
	return 0;
	}

// Check for existing invite to group
for(i=0;i < MAX_INVITES;++i) {
	if (u->invite[i].grp == group) {
		uprintf("User %04X (%s) already has an invitation for this group.\n",u->id,u->name);
		return 0;
		}
	}

// Find free slot
for(i=0;i < MAX_INVITES;++i) {
	if (!u->invite[i].grp) {
		u->invite[i].grp = group;
		u->invite[i].time = server_time;
		uprintf("User ~FT%04X~RS (%s) invited into this group.\n",
			u->id,u->name);
		u->infoprintf("~BMINVITE:~RS To group ~FT%04X~RS (%s~RS) by user ~FT%04X~RS (%s).\n",group->id,group->name,id,name);
		return 1;
		}
	}
uprintf("User %04X (%s) has no free invitation slots left.\n",u->id,u->name);
return 0;
}




/*** Uninvite a user ***/
void cl_user::com_uninvite()
{
uint16_t uid;
cl_user *u;
int i;

if (com->wcnt != 2) {
	uprintf("Usage: uninvite <user id>\n");  return;
	}
if (!group->user_can_modify(this)) {
	uprintf("You cannot uninvite people to this group.\n");  return;
	}
if (!(uid = idstr_to_id(com->word[1]))) {
	uprintf(INVALID_USER_ID);   return;
	}
if (!(u = get_user(uid))) {
	uprintf(NOT_LOGGED_ON);  return;
	}
for(i=0;i < MAX_INVITES;++i) {
	if (u->invite[i].grp == group) {
		u->infoprintf("~BYREVOKED INVITE:~RS To group ~FT%04X~RS (%s~RS) by user ~FT%04X~RS (%s).\n",group->id,group->name,id,name);
		u->invite[i].grp = NULL;
		uprintf("User ~FT%04X~RS (%s) has had their invite revoked.\n",
			u->id,u->name);
		return;
		}
	}
uprintf("User %04X (%s) has not been invited to this group.\n",u->id,u->name);
}




/*** List the out invites to other groups. Not a paging function since
     invitation list is small so not worth it. ***/
void cl_user::com_lsinvites()
{
struct tm *tms;
time_t et;
int i,cnt;
char when[10],expire[10];

if (com->wcnt != 1) {
	uprintf(COM_NO_ARGS);  return;
	}
for(i=0,cnt=0;i < MAX_INVITES;++i) {
	if (invite[i].grp) {
		if (!cnt) {
			uprintf("\n~BB*** Invitations ***\n\n");
			uprintf("~OL~ULWhen  Expires Group Name\n");
			}
		tms = localtime(&invite[i].time);
		strftime(when,sizeof(when),"%H:%M",tms);

		et = invite[i].time + group_invite_expire;
		tms = localtime(&et);
		strftime(expire,sizeof(expire),"%H:%M",tms);

		uprintf("%s   %s  %04X %s\n",
			when,expire,invite[i].grp->id,invite[i].grp->name);
		cnt++;
		}
	}
if (cnt) uprintf("\n~FTTotal of %d invitations.\n\n",cnt);
else uprintf("You currently have no invitations.\n");
}




/*** Evict a user from a group. This does not ban them. ***/
void cl_user::com_evict()
{
cl_user *u;
uint16_t uid;
int cnt;

if (com->wcnt != 2) {
	uprintf("Usage: evict <user id>/all\n");  return;
	}
if (!group->user_can_modify(this)) {
	uprintf("You cannot evict people from this group.\n");  return;
	}
if (!strcasecmp(com->word[1],"all")) u = NULL;
else {
	if (!(uid = idstr_to_id(com->word[1]))) {
		uprintf(INVALID_USER_ID);  return;
		}
	if (uid == id) {
		uprintf("You cannot evict yourself!\n");  return;
		}
	if (!(u = get_user(uid))) {
		uprintf(NOT_LOGGED_ON);  return;
		}
	if (u->group != group) {
		uprintf("User %04X (%s) is not in this group.\n",u->id,u->name);
		return;
		}
	if (u->group == u->home_group) {
		// Because thats where they go to when they're evicted!
		uprintf("You cannot evict a user from their home group.\n");
		return;
		}
	}

// Evict single user
if (u) {
	if (u->level >= group_gatecrash_level) {
		uprintf("You cannot evict that user, their level is too high.\n");
		return;
		}
	group->evict(this,u);  return;
	}

// Evict all
cnt = 0;
FOR_ALL_USERS(u) {
	if (u->group == group && 
	    u != this && u->group != 
	    u->home_group && 
	    u->level < group_gatecrash_level) {
		group->evict(this,u);  ++cnt;
		}
	}
if (cnt) uprintf("%d users evicted.\n",cnt);
else uprintf("There is nobody here to evict or that can be evicted.\n");
}




/*** Ban a user from a group ***/
void cl_user::com_gban()
{
uint16_t uid,gid;
cl_group *grp;
cl_user *u;
cl_local_user *lu;
int ret;

switch(com->wcnt) {
	case 2:
	grp = group;  break;

	case 3:
	if (!(gid = idstr_to_id(com->word[2]))) {
		uprintf("Invalid group id.\n");  return;
		}
	if (!(grp = get_group(gid))) {
		uprintf(NO_SUCH_GROUP);  return;
		}
	break;
	
	default:
	uprintf("Usage: gban <user id> [<group id>]\n");
	return;
	}
if (!grp->user_can_modify(this)) {
	uprintf("You cannot ban users from group %04X, %s\n",grp->id,grp->name);
	return;
	}
if (!(uid = idstr_to_id(com->word[1]))) {
	uprintf(INVALID_USER_ID);  return;
	}
if (uid == grp->id) {
	uprintf("You cannot ban a user from their home group.\n");
	return;
	}
if (uid == id) {
	uprintf("You cannot ban yourself!\n");  return;
	}
if (!(u = get_user(uid)) &&  !(u = create_temp_user(uid))) {
	uprintf(NO_SUCH_USER);  return;
	}
if (u->level >= group_gatecrash_level) {
	uprintf("You cannot ban that user, their level is too high.\n");
	goto DEL;
	}
if (grp->user_is_banned(u)) {
	uprintf("User %04X (%s) is already banned from group %04X, %s\n",
		uid,u->name,grp->id,grp->name);
	goto DEL;
	}

sprintf(text,
	"~OLYou have been ~FYBANNED~RS~OL from group %04X~RS (%s) ~OLby %04X, %s.\n",
	grp->id,grp->name,id,name);

// If user isn't on send them an email else just send a print
if (O_FLAGISSET(u,USER_FLAG_TEMP_OBJECT)) {
	lu = (cl_local_user *)u;
	if ((ret = lu->send_system_mail("~FYGROUP BAN",text)) != OK)
		errprintf("Mail send failed: %s\n",err_string[ret]);

	grp->ban(level,uid,0,0);
	ret = grp->save();
	}
else {
	if (u->group == grp) {
		u->uprintf("\n~OL~FRYou have been banned from this group!\n\n");
		grp->evict(this,u);
		}
	else 
	u->infoprintf(text);

	ret = grp->ban(level,u);
	}

if (ret != OK)
	errprintf("Saving ban failed: %s\n",err_string[ret]);
else {
	uprintf("User ~FYBANNED~RS from group %04X, %s~RS.\n",
		grp->id,grp->name);
	log(1,"User %04X (%s) BANNED user %04X (%s) from group %04X, %s",
		id,name,u->id,u->name,grp->id,grp->name);
	}

DEL:
if (O_FLAGISSET(u,USER_FLAG_TEMP_OBJECT)) delete u;
}




/*** Unban a user from a group. Use entry numbers instead of user id's as it
     makes the code a LOT simpler and being the lazy bastard that I am... ***/
void cl_user::com_gunban()
{
uint16_t gid;
cl_group *grp;
cl_mail *tmail;
st_user_ban *gb;
int num,cnt,ret;

switch(com->wcnt) {
	case 2:
	grp = group;  break;

	case 3:
	if (!(gid = idstr_to_id(com->word[2]))) {
		uprintf("Invalid group id.\n");  return;
		}
	if (!(grp = get_group(gid))) {
		uprintf(NO_SUCH_GROUP);  return;
		}
	break;
	
	default:
	uprintf("Usage: gunban <entry number> [<group id>]\n");  return;
	return;
	}
if (!grp->user_can_modify(this)) {
	uprintf("You cannot unban users from group %04X, %s\n",
		grp->id,grp->name);
	return;
	}
if (!is_integer(com->word[1]) || (num=atoi(com->word[1])) < 1) {
	uprintf("Invalid entry number.\n");  return;
	}

for(cnt=1,gb=grp->first_ban;gb && cnt < num;gb=gb->next,++cnt);
if (!gb) {
	uprintf("There is no such entry.\n");  return;
	}

if (gb->level > level) {
	uprintf("That user was banned by a user of level %s. You cannnot unban them.\n",user_level[gb->level]);
	return;
	}

uprintf("User ~FGUNBANNED~RS, entry ~FT%d~RS deleted.\nPlease note that other entry numbers will now have changed.\n",num);

if (gb->utype == USER_TYPE_LOCAL)
	log(1,"User %04X (%s) DELETED ban entry %d from group %04X (%s): local user %04X",
		id,name,num,grp->id,grp->name,gb->uid);
else
	log(1,"User %04X (%s) DELETED ban entry %d from group %04X (%s): remote user %04X, %s, %d",
		id,name,num,grp->id,grp->name,gb->uid,
		inet_ntoa(gb->home_addr.sin_addr),
		ntohs(gb->home_addr.sin_port));

sprintf(text,
	"~OLYou have been ~FGUNBANNED~RS~OL from group %04X~RS (%s) ~OLby %04X, %s.\n",
	grp->id,grp->name,id,name);

// If user is online let them know else if they're local send an email
if (gb->user) gb->user->infoprintf(text);
else 
if (!(gb->uid & 0xF000)) {
	// Can't use send_system_mail() here because we have no user object.
	tmail = new cl_mail(NULL,gb->uid);
	if (tmail->error != OK)
		errprintf("Mail object creation failed: %s\n",
			err_string[tmail->error]);
	else
	if ((ret = tmail->mwrite(
		0,SYS_USER_NAME,NULL,"~FGGROUP UNBAN",text)) != OK)
		errprintf("Mail write failed: %s\n",err_string[ret]);

	delete tmail;
	}

remove_list_item(grp->first_ban,grp->last_ban,gb);

if ((ret = group->save()) != OK) 
	errprintf("Failed to save group info: %s\n",err_string[ret]);
}




/*** List bans for the given group or current one if not provided.
     Paging command. ***/
void cl_user::com_lsgbans()
{
cl_group *grp;
st_user_ban *gb;
uint16_t gid;
int lcnt,ucnt;

if (com_page_line == -1) {
	switch(com->wcnt) {
		case 1:
		grp = group;  break;

		case 2:
		if (!(gid = idstr_to_id(com->word[1]))) {
			uprintf("Invalid group id.\n");  return;
			}
		if (!(grp = get_group(gid))) {
			uprintf(NO_SUCH_GROUP);  return;
			}
		com_page_ptr = (void *)grp;
		break;

		default:
		uprintf("Usage: lsgbans [<group id>]\n");  return;
		}
	}
else {
	if (!com_page_ptr) {
		errprintf("Group has been deleted.\n");
		com_page_line = -1;
		return;
		}
	grp = (cl_group *)com_page_ptr;
	}

// Loop through banned users
lcnt = 0;
ucnt = 0;
for(gb=grp->first_ban;gb;gb=gb->next) {
	if (++ucnt <= com_page_line) continue;

	if (ucnt==1) {
		uprintf("\n~BB*** Users banned from group %04X, %s~RS~BB ***\n\n",grp->id,grp->name);
		uprintf("~OL~ULEntry Ban level Type   LID  RID  Orig IP               Name\n");
		}
	if (gb->utype == USER_TYPE_LOCAL) {
		if (gb->user) 
			uprintf("~FY%5d~RS %-8s  %-6s %04X N/A  N/A                   %s\n",
				ucnt,
				user_level[gb->level],
				user_type[gb->utype],gb->uid,gb->user->name);
		else 
			uprintf("~FY%5d~RS %-8s  %-6s %04X N/A  N/A                   <unknown>\n",
				ucnt,
				user_level[gb->level],
				user_type[gb->utype],gb->uid); 
		}
	else {
		sprintf(text,"%s:%u",
			inet_ntoa(gb->home_addr.sin_addr),
			ntohs(gb->home_addr.sin_port));

		if (gb->user) 
			uprintf("~FY%5d~RS %-8s  %-6s %04X %04X %-21s %s\n",
				ucnt,
				user_level[gb->level],
				user_type[gb->utype],
				gb->user->id,gb->uid,text,gb->user->name);
		else
			uprintf("~FY%5d~RS %-8s  %-6s ?    %04X %-21s <unknown>\n",
				ucnt,
				user_level[gb->level],
				user_type[gb->utype],gb->uid,text);
		}

	if (FLAGISSET(USER_FLAG_PAGING) && ++lcnt == term_rows - 2) {
		com_page_line = ucnt;
		com_page_ptr = (void *)grp;
		return;
		}
	}

if (FLAGISSET(USER_FLAG_PAGING) && lcnt && lcnt >= term_rows - 5) {
	com_page_line = ucnt;
	com_page_ptr = (void *)grp;
	return;
	}

if (ucnt) uprintf("\n~FTTotal of %d users banned.\n\n",ucnt);
else uprintf("There are no users banned from group %04X, %s~RS.\n",
	grp->id,grp->name);

com_page_line = -1;
}




/*** Examine a user ***/
void cl_user::com_examine()
{
cl_user *u;
cl_server *svr;
uint16_t uid;
char path[MAXPATHLEN];
int ret,tmpflags;

switch(com->wcnt) {
	case 1:
	u = this;  break;

	case 2:
	if ((ret=idstr_to_id_and_svr(com->word[1],&uid,&svr)) != OK) {
		errprintf("Unable to examine: %s\n",err_string[ret]);  return;
		}
	if (svr) {
		// Remote examine
		if ((ret=svr->send_examine(id,uid)) != OK) 
			errprintf("Unable to send examine request: %s\n",
				err_string[ret]);
		else uprintf("Request sent.\n");
		return;
		}
	// Local examine
	if (!(u=get_user(uid)) && !(u = create_temp_user(uid))) {
		uprintf(NO_SUCH_USER);  return;
		}
	break;

	default:
	uprintf("Usage: examine [<user id[@<server>]>]\n");  return;
	}
if (u == this) uprintf("You examine yourself...\n");

uprintf("\n~BB*** Details of user %04X ***\n\n",u->id);

sprintf(path,"%s/%04X/%s",USER_DIR,u->id,USER_PROFILE_FILE);

// If gone remote switch off paging temporarily
if (server_to) {
	tmpflags = flags;
	UNSETFLAG(USER_FLAG_PAGING);
	}
// If no profile or not paging or gone remote show details immediately
// else switch to examine stage
if ((ret = page_file(path,1)) != OK) uprintf("No profile.\n");
if (!server_to && ret == OK && FLAGISSET(USER_FLAG_PAGING)) {
	exa_user = u;
	stage = USER_STAGE_EXAMINE;
	return;
	}

if (server_to) flags = tmpflags;

uprintf("\n");
show_details(u);
if (O_FLAGISSET(u,USER_FLAG_TEMP_OBJECT)) delete u;
}




/*** Away From Keyboard command ***/
void cl_user::com_afk()
{
char *remerr = "Remote users cannot use the lock feature of AFK.\n";

if (server_to) {
	// Because being afk in the gone remotes group is meaningless
	uprintf("You cannot use the AFK command whilst on a remote server.\n");
	return;
	}

switch(com->wcnt) {
	case 1:
	uprintf("You are now AFK, press return key to return...\n");
	stage = USER_STAGE_AFK;
	goto DONE;

	case 2:
	if (strcasecmp(com->word[1],"lock")) break;
	// We don't have remote user password 
	if (type == USER_TYPE_REMOTE) {  uprintf(remerr);  return;  }
	uprintf("You are now AFK (~FYSession locked~RS)...\n\n");
	stage = USER_STAGE_AFK_LOCK;
	goto DONE;

	default:
	if (strcasecmp(com->word[1],"lock")) break;
	if (type == USER_TYPE_REMOTE) {  uprintf(remerr);  return;  }
	afk_msg = strdup(com->wordptr[2]);
	uprintf("You are now AFK (~FYSession locked~RS), message: %s ...\n\n",
		afk_msg);
	stage = USER_STAGE_AFK_LOCK;
	goto DONE;
	}

stage = USER_STAGE_AFK;
afk_msg = strdup(com->wordptr[1]);
uprintf("You are now AFK, message: %s\nPress return key to return...\n",
	afk_msg);

DONE:
if (afk_msg)
	group->geprintf(MSG_MISC,this,NULL,"~FY~OLAFK:~RS ~FT%04X,~RS %s: %s\n",
		id,name,afk_msg);
else
group->geprintf(MSG_MISC,this,NULL,"~FY~OLAFK:~RS ~FT%04X,~RS %s\n",id,name);
}




/*** Board command ***/
void cl_user::com_board()
{
if (server_to) {
	uprintf("You cannot use the message board whilst on a remote server.\n");
	return;
	}
if (!group->board) {
	uprintf("There is no message board for this group.\n");
	return;
	}

// Set stage and unset some flags 
stage = USER_STAGE_BOARD;
prev_flags = flags;
if (FLAGISSET(USER_FLAG_AUTOSILENCE)) {
	SETFLAG(USER_FLAG_NO_SPEECH);
	SETFLAG(USER_FLAG_NO_SHOUTS);
	}

// list board contents

group->geprintf(MSG_MISC,this,NULL,
	"User ~FT%04X~RS (%s) is using the board reader.\n",id,name);

// If board command on its own just list messages and enter reader else
// execute sub command passed.
if (com->wcnt == 1) group->board->list(this);
else {
	// Shift command line up by one word and call the board reader
	// so it executes command immediately
	com->shift();
	run_board_reader();
	}
}




/*** Create or delete a user or change their level ***/
void cl_user::com_user()
{
char *subcom[] = {
	"create","delete","level"
	};
enum {
	SC_CREATE, SC_DELETE, SC_LEVEL, SC_END
	};
int i,len;

if (com->wcnt > 2) {
	len = strlen(com->word[1]);
	for(i=0;i < SC_END;++i)
		if (!strncasecmp(com->word[1],subcom[i],len)) break;

	switch(i) {
		case SC_CREATE:
		if (com->wcnt < 4 || com->wcnt > 5) break;
		user_create();
		return;

		case SC_DELETE:
		if (com->wcnt != 3) break;
		user_delete();
		return;

		case SC_LEVEL:
		if (com->wcnt != 4) break;
		user_set_level();
		return;
		}
	}
uprintf("Usage: user create <id> <name> [<password>]\n");
uprintf("       user delete <id>\n");
uprintf("       user level  <id> <level>\n");
}




/*** Create a user ***/
void cl_user::user_create()
{
struct stat fs;
cl_user *u;
cl_local_user *lu;
uint16_t uid;
char path[MAXPATHLEN];
int len,ret;
char *nme,*pwd;

if (level < USER_LEVEL_ADMIN) {
	uprintf(NOT_HIGH_ENOUGH_LEVEL);  return;
	}

// If uid is blank or zero then create random id
if (!com->word[2][0] || !strcmp(com->word[2],"0")) {
	if (!(uid = generate_id())) {
		errprintf("Unable to generate a new id at this time.\n");
		return;
		}
	}
else {
	if (!(uid = idstr_to_id(com->word[2])) ||
	      uid > MAX_LOCAL_USER_ID || uid < MIN_LOCAL_USER_ID) {
		uprintf("Invalid user id. It must be between %04X and %04X inclusive.\n",MIN_LOCAL_USER_ID,MAX_LOCAL_USER_ID);
		return;
		}

	// Check user id isn't used.
	sprintf(path,"%s/%04X",USER_DIR,uid);
	if ((u=get_user(uid,1)) || !stat(path,&fs)) {
		uprintf("User id ~FT%04X~RS is already assigned.\n",uid);
		return;
		}
	}

// Check name
if ((len = strlen(com->word[3])) > max_name_len) {
	uprintf("Name is too long, maximum length is %d characters.\n",
		max_name_len);
	return;
	}
if (len < 1) {
	uprintf("Name too short.\n");  return;
	}
nme = trim(com->word[3]);

// Check password
if (com->wcnt == 5) {
	if ((int)strlen(com->word[4]) < min_pwd_len) {
		uprintf("Password is too short, minimum is %d characters.\n",
			min_pwd_len);
		return;
		}
	pwd = com->word[4];
	}
else pwd = default_pwd;

// Create the user object
lu = new cl_local_user(1);
if (lu->error != OK) {
	errprintf("User object creation failed: %s\n",err_string[lu->error]); 
	delete lu;
	return;
	}
	
// pwd is not strdup'd as it will not get free'd in destructor because temp
lu->id = uid;
lu->name = strdup(nme);
lu->pwd = crypt_str(pwd);
lu->desc = strdup(default_desc);
lu->level = USER_LEVEL_NOVICE;
lu->start_group_id = uid;

if ((ret = lu->save()) != OK) 
	errprintf("User object creation failed: %s\n",err_string[ret]);
else {
	if (com->wcnt == 5)
		uprintf("User ~FT%04X~RS (%s) ~FGCREATED.\n",uid,nme);
	else
		uprintf("User ~FT%04X~RS (%s) ~FGCREATED~RS with default password.\n",uid,nme);
	log(1,"User %04X (%s) CREATED user %04X.",id,name,uid);
	}

delete lu;
}




/*** Delete a user ***/
void cl_user::user_delete()
{
uint16_t uid;
cl_user *u;

if (level < USER_LEVEL_ADMIN) {
	uprintf(NOT_HIGH_ENOUGH_LEVEL);  return;
	}
if (!(uid = idstr_to_id(com->word[2]))) {
	uprintf(INVALID_USER_ID);  return;
	}
if (uid & 0xF000) {
	uprintf("Remote users do not have an account to delete.\n");  return;
	}
if (uid == id) {
	uprintf("Use the suicide command to delete your own account.\n");
	return;
	}
// If user is logged on just set delete flag and disconnect them
if (!(u = get_user(uid)) && !(u = create_temp_user(uid))) {
	uprintf(NO_SUCH_USER);  return;
	}
if (u->level == USER_LEVEL_ADMIN) {
	uprintf("You cannot delete another administrator account.\n");
	if (O_FLAGISSET(u,USER_FLAG_TEMP_OBJECT)) delete u;
	return;
	}
uprintf("\n~FR~OLWARNING: This will delete this users account!\n\n");
stage = USER_STAGE_DELETE_USER;
del_user = u;
}




/*** Set a users level ***/
void cl_user::user_set_level()
{
uint16_t uid;
cl_user *u;
cl_local_user *lu;
int old_level,new_level;
int ret;

if (!(uid = idstr_to_id(com->word[2]))) {
	uprintf(INVALID_USER_ID);  return;
	}
if ((new_level = get_level(com->word[3])) == -1 ||
     new_level == USER_LEVEL_LOGIN) {
	uprintf("Invalid level.\n");  return;
	}

// If user is not on then create temp object
if (!(u = get_user(uid)) && !(u = create_temp_user(uid))) {
	uprintf(NO_SUCH_USER);  return;
	}
if (u == this) {
	uprintf("You cannot change your own level!\n");  goto DEL;
	}
if (u->level >= level) {
	uprintf("You cannot modify the level of a user whose level is equal or greater than your\nown and to modify the level of an administrator you will have to manually edit\ntheir configuration file.\n");
	goto DEL;
	}
if (new_level > level) {
	uprintf("You cannot set a users level to greater than your own.\n");
	goto DEL;
	}
if (u->level == new_level) {
	uprintf("That is already their current level.\n");  goto DEL;
	}

old_level = u->level;
u->level = new_level;
if (u->type == USER_TYPE_LOCAL) ((cl_local_user *)u)->save();

uprintf("User ~FT%04X~RS (%s) is now level ~OL~FM%s.\n",
	u->id,u->name,user_level[new_level]);
log(1,"User %04X (%s) changed the level of user %04X (%s) from %s to %s.",
	id,name,u->id,u->name,user_level[old_level],user_level[new_level]);

sprintf(text,"~OLYou have been %s~RS~OL to level ~FM%s~RS~OL by %04X, %s.\n",
	new_level < old_level ? "~FRDEMOTED" : "~FGPROMOTED",
	user_level[new_level],id,name);

// If logged on user just do message then return
if (!O_FLAGISSET(u,USER_FLAG_TEMP_OBJECT)) {
	u->uprintf(text);  return;
	}

// Send mail to user so when they log on they'll know
lu = (cl_local_user *)u;
if ((ret = lu->send_system_mail("~FYLEVEL CHANGE",text)) != OK)
	errprintf("Mail send failed: %s\n",err_string[ret]);

DEL:
if (O_FLAGISSET(u,USER_FLAG_TEMP_OBJECT)) delete u;
}




/*** Change various system levels ***/
void cl_user::com_level()
{
int len,i,new_level;
char *usage="Usage: level lockout    <level> [logout]\n"
            "       level remusermax <level>\n"
            "       level gatecrash  <level>\n"
            "       level invisible  <level>\n"
            "       level grpmod     <level>\n"
            "       level maxuserign <level>\n"
            "       level idleign    <level>\n"
            "       level netbcast   <level>\n";
char *subcom[] = {
        "lockout",
	"remusermax",
	"gatecrash",
	"invisible",
	"grpmod",
	"maxuserign",
	"idleign",
	"netbcast"
        };
enum {
        SC_LOCKOUT,
	SC_RUM,
	SC_GC,
	SC_INVIS,
	SC_GRPMOD,
	SC_MUI,
	SC_IDLE,
	SC_NET,

	SC_END
        };

if (com->wcnt < 3) {
	uprintf(usage);  return;
	}
len = strlen(com->word[1]);
for(i=0;i < SC_END;++i) 
	if (!strncasecmp(com->word[1],subcom[i],len)) break;
if (i == SC_END || (i != SC_LOCKOUT && com->wcnt != 3)) {
	uprintf(usage);  return;
	}
if ((new_level = get_level(com->word[2])) == -1 ||
     new_level == USER_LEVEL_LOGIN) {
	uprintf("Invalid level.\n");  return;
	}

// This is a bit academic since only admin can use this command as I've set
// it up in globals.h but someone might change that.
if (new_level > level) {
	uprintf("You cannot set a level to be greater than your own level.\n");
	return;
	}

switch(i) {
	case SC_LOCKOUT:
	level_lockout(usage,new_level);
	break;

	case SC_RUM:
	level_remusermax(new_level);
	break;

	case SC_GC:
	level_gatecrash(new_level);
	return;  // gatecrash level info not sent to servers

	case SC_INVIS:
	level_invis(new_level);
	return;

	case SC_GRPMOD:
	level_grpmod(new_level);
	return;

	case SC_MUI:
	level_maxuserign(new_level);
	return;

	case SC_IDLE:
	level_idleign(new_level);
	return;

	case SC_NET:
	level_netbcast(new_level);
	return;
	}

// Send changed info to connected servers
send_server_info_to_servers(NULL);
}




/*** Level lockout sub option of com_level() ***/
void cl_user::level_lockout(char *usage, int new_level)
{
cl_user *u,*next_user;
int force_logout,cnt;

force_logout = 0;
switch(com->wcnt) {
	case 4:
	if (strncasecmp(com->word[3],"logout",strlen(com->word[3]))) {
		uprintf(usage);  return;
		}
	force_logout = 1;
	// Fall through

	case 3:
	break;

	default: 
	uprintf(usage);
	return;
	}
if (lockout_level == new_level && !force_logout) {
	uprintf("Lockout level is already set to that.\n");  return;
	}
lockout_level = new_level;

// Log and inform wizards
log(1,"User %04X (%s) set lockout level to %s.",
	id,name,user_level[lockout_level]);
allprintf(
	MSG_INFO,USER_LEVEL_MONITOR,this,
	"User ~FT%04X~RS (%s) has set the lockout level to: ~OL~FM%s\n",
		id,name,user_level[lockout_level]);
uprintf("Lockout level set to: ~OL~FM%s\n",user_level[lockout_level]);

if (force_logout) {
	allprintf(MSG_BCAST,USER_LEVEL_NOVICE,this,
		"\n~OL~FYWARNING:~RS~OL Lockout level has been set to ~FY%s~RS~OL and all users below this level\n         ~OLwill be logged out now!\n\n",
		user_level[lockout_level]);

	// Go through user list and bin the low level scum!
	for(u=first_user,cnt=0;u;u=next_user) {
		next_user = u->next;
		if (u->level != USER_LEVEL_LOGIN && u->level < lockout_level) {
			u->disconnect();  ++cnt;
			}
		}
	uprintf("%d users were logged out.\n",cnt);
	}
if (lockout_level > remote_user_max_level)
	warnprintf("lockout level is > remote user max level. This will prevent remote\n         users connecting.\n");
}




/*** Set remote user max level. Sub command of com_level() ***/
void cl_user::level_remusermax(int new_level)
{
if (remote_user_max_level == new_level) {
	uprintf("Remote user max level is already set to that.\n");
	return;
	}
remote_user_max_level = new_level;

log(1,"User %04X (%s) set remote user max level to %s.",
	id,name,user_level[new_level]);
allprintf(
	MSG_INFO,USER_LEVEL_MONITOR,this,
	"User ~FT%04X~RS (%s) has set the remote user max level to: ~OL~FM%s\n",
		id,name,user_level[new_level]);
uprintf("Remote user max level set to: ~OL~FM%s\n",user_level[new_level]);

if (lockout_level > remote_user_max_level)
	warnprintf("remote user max level is < lockout level. This will prevent remote\n         users connecting.\n");
}




/*** Set gatecrash level ***/
void cl_user::level_gatecrash(int new_level)
{
if (group_gatecrash_level == new_level) {
	uprintf("Gatecrash level is already set to that.\n");
	return;
	}
group_gatecrash_level = new_level;

log(1,"User %04X (%s) set gatecrash level to %s.",
	id,name,user_level[new_level]);
allprintf(
	MSG_INFO,USER_LEVEL_MONITOR,this,
	"User ~FT%04X~RS (%s) has set the gatecrash level to: ~OL~FM%s\n",
		id,name,user_level[new_level]);
uprintf("Gatecrash level set to: ~OL~FM%s\n",user_level[new_level]);
}




/*** Set go invisible level ***/
void cl_user::level_invis(int new_level)
{
cl_user *u;

if (go_invis_level == new_level) {
	uprintf("Go invisible level is already set to that.\n");
	return;
	}
// Force any invisible users below new level to become visible
if (new_level > go_invis_level) {
	FOR_ALL_USERS(u) {
		if (u->level < new_level &&
		    O_FLAGISSET(u,USER_FLAG_INVISIBLE)) {
			u->warnprintf("The go invisible level has been raised to %s. You have been forced to become visible.\n",user_level[new_level]);

			group->geprintf(
				MSG_MISC,u,NULL,
				"~BBVISIBLE:~RS ~FT%04X~RS, %s %s\n",
				u->id,u->name,u->desc);

			O_UNSETFLAG(u,USER_FLAG_INVISIBLE);
			}
		}
	}	
go_invis_level = new_level;

log(1,"User %04X (%s) set go invisible level to %s.",
	id,name,user_level[new_level]);
allprintf(
	MSG_INFO,USER_LEVEL_MONITOR,this,
	"User ~FT%04X~RS (%s) has set the go invisible level to: ~OL~FM%s\n",
		id,name,user_level[new_level]);
uprintf("Go invisible level set to: ~OL~FM%s\n",user_level[new_level]);
}




/*** Set group modify level ***/
void cl_user::level_grpmod(int new_level)
{
if (group_modify_level == new_level) {
	uprintf("Group modify level is already set to that.\n");
	return;
	}
group_modify_level = new_level;

log(1,"User %04X (%s) set group modify level to %s.",
	id,name,user_level[new_level]);
allprintf(
	MSG_INFO,USER_LEVEL_MONITOR,this,
	"User ~FT%04X~RS (%s) has set the group modify level has been set to: ~OL~FM%s\n",
		id,name,user_level[new_level]);
uprintf("Group modify level set to: ~OL~FM%s\n",user_level[new_level]);
}




/*** Set max user ignore level (Level and above at which the max user count
     is ignored at login so user can still log on) ***/
void cl_user::level_maxuserign(int new_level)
{
if (max_user_ign_level == new_level) {
	uprintf("Max user ignore level is already set to that.\n");
	return;
	}
max_user_ign_level = new_level;

log(1,"User %04X (%s) set max user ignore level to %s.",
	id,name,user_level[new_level]);
allprintf(
	MSG_INFO,USER_LEVEL_MONITOR,this,
	"User ~FT%04X~RS (%s) has set the max user ignore level to: ~OL~FM%s\n",
		id,name,user_level[new_level]);
uprintf("Max user ignore level set to: ~OL~FM%s\n",user_level[new_level]);
}




/*** Set idle timeout ignore level (level and above at which the idle timeout
     does not apply to a user) ***/
void cl_user::level_idleign(int new_level)
{
if (idle_timeout_ign_level == new_level) {
	uprintf("Idle timeout ignore level is already set to that.\n");
	return;
	}
idle_timeout_ign_level = new_level;

log(1,"User %04X (%s) set idle timeout ignore level to %s.",
	id,name,user_level[new_level]);
allprintf(
	MSG_INFO,USER_LEVEL_MONITOR,this,
	"User ~FT%04X~RS (%s) has set the idle timeout ignore level to: ~OL~FM%s\n",
		id,name,user_level[new_level]);
uprintf("Idle timeout ignore level set to: ~OL~FM%s\n",user_level[new_level]);
}




/*** Set receive network broadcast level ***/
void cl_user::level_netbcast(int new_level)
{
cl_user *u;

if (recv_net_bcast_level == new_level) {
	uprintf("Receive network broadcast level is already set to that.\n");
	return;
	}
if (recv_net_bcast_level < new_level) {
	// Go through all users and reset flags if they're lower than the
	// new level
	FOR_ALL_USERS(u) 
		if (u->level != USER_LEVEL_LOGIN && u->level < new_level)
			O_UNSETFLAG(u,USER_FLAG_RECV_NET_BCAST);
	}
recv_net_bcast_level = new_level;
	
log(1,"User %04X (%s) set receive network broadcast level to %s.",
	id,name,user_level[new_level]);
allprintf(
	MSG_INFO,USER_LEVEL_MONITOR,this,
	"User ~FT%04X~RS (%s) has set the receive network broadcast level to: ~OL~FM%s\n",
		id,name,user_level[new_level]);
uprintf("Receive network broadcast level set to: ~OL~FM%s\n",
	user_level[new_level]);
}




/*** Set appropriate stage. All the work is done in run_new_password() ***/
void cl_user::com_passwd()
{
uint16_t uid;

switch(com->wcnt) {
	case 1:
	if (type == USER_TYPE_REMOTE) {
		uprintf("Remote users do not have a password to change.\n");
		return;
		}
	new_pwd_user = (cl_local_user *)this;
	uprintf("\n~FYChanging your password...\n\n");
	stage = (new_pwd_user->pwd && new_pwd_user->pwd[0]) ? 
	         USER_STAGE_OLD_PWD : USER_STAGE_NEW_PWD;
	return;

	case 2:
	if (level < USER_LEVEL_ADMIN) {
		uprintf("Only an administrator can change another users password.\n");
		return;
		}
	if (!(uid = idstr_to_id(com->word[1])) || uid > MAX_LOCAL_USER_ID) {
		uprintf(INVALID_USER_ID);  return;
		}

	// Always use temp object even if user is logged on because user may
	// log off while we're modifying their password.
	if (!(new_pwd_user = create_temp_user(uid))) {
		uprintf(NO_SUCH_USER);  return;
		}
	if (new_pwd_user->level == USER_LEVEL_ADMIN) {
		uprintf("You cannot change the password of another administrator.\n");
		delete new_pwd_user;
		return;
		}
	uprintf("\n~FYChanging password for user ~FT%04X~FY (%s)...\n\n",
		uid,new_pwd_user->name);
	stage = USER_STAGE_NEW_PWD;
	return;

	default:
	uprintf("Usage: passwd [<user id>]>\n");
	}
}




/*** Clear the screen ***/
void cl_user::com_cls()
{
cl_user *u;
uint16_t uid;
int i;

switch(com->wcnt) {
	case 1:
	u = this;
	break;

	case 2:
	// Have hard coded level. This function is so trivial its not worth
	// the effort of having soft values.
	if (level < USER_LEVEL_MONITOR) {
		uprintf(NOT_HIGH_ENOUGH_LEVEL);  return;
		}
        if (!(uid = idstr_to_id(com->word[1]))) {
                uprintf(INVALID_USER_ID);  return;
                }
	if (!(u = get_user(uid))) {
		uprintf(NOT_LOGGED_ON);  return;
		}
	if (level <= u->level) {
		uprintf("You cannot clear the screen of a user of equal or higher level than yourself.\n");
		return;
		}
	break;

	default:
	uprintf("Usage: cls [<user id>]\n");
	return;
	}

// If user has an ansi term then use ansi code screen clear else loop
// around and print newlines
if (FLAGISSET(USER_FLAG_ANSI_TERM)) u->uprintf("~SC");
else {
	for(i=0;i < term_rows;i+=10) u->uprintf("\n\n\n\n\n\n\n\n\n\n");
	}
if (u != this) {
	uprintf("User ~FT%04X~RS (%s) screen was cleared.\n",u->id,u->name);
	u->infoprintf("Your screen was cleared by ~FT%04X~RS (%s).\n",id,name);
	}
}




/*** Muzzle a user. You can only muzzled a user who's logged on (for now). ***/
void cl_user::com_muzzle()
{
cl_user *u;
cl_local_user *lu;
uint16_t uid;
int ret,secs;
char *usage = "Usage: muzzle <user id> [<muzzle period> [seconds/minutes/hours/days]]\n";

switch(com->wcnt) {
	case 2:
	secs = 0;
	break;

	case 3:
	secs = get_seconds(com->word[2],NULL);
	break;

	case 4:
	secs = get_seconds(com->word[2],com->word[3]);
	break;

	default:
	uprintf(usage);
	return;
	}
if (secs < 0) {
	uprintf(usage);  return;
	}
if (!(uid = idstr_to_id(com->word[1]))) {
	uprintf(INVALID_USER_ID);  return;
	}
if (!(u = get_user(uid)) && !(u = create_temp_user(uid))) {
	uprintf(NO_SUCH_USER);  return;
	}
if (u == this) {
	uprintf("You cannot muzzle yourself.\n");
	goto DEL;
	}
if (O_FLAGISSET(u,USER_FLAG_MUZZLED)) {
	uprintf("User is already muzzled.\n");  goto DEL;
	}
if (u->level >= level) {
	uprintf("You cannot muzzle a user of equal or higher level than yourself.\n");
	goto DEL;
	}

O_SETFLAG(u,USER_FLAG_MUZZLED);
u->muzzle_level = level;
u->muzzle_start_time = server_time;

if (secs) {
	u->muzzle_end_time = server_time + secs;

	sprintf(text,"~OLYou have been ~FYMUZZLED~RS~OL by %04X (%s) for %s.\n",
		id,name,time_period(secs));
	log(1,"User %04X (%s) MUZZLED user %04X (%s) for %s.",
		id,name,u->id,u->name,time_period(secs));
	}
else {
	u->muzzle_end_time = 0;

	sprintf(text,"~OLYou have been ~FYMUZZLED~RS~OL by %04X (%s) indefinitely!\n",id,name);
	log(1,"User %04X (%s) MUZZLED user %04X (%s) indefinitely.",
		id,name,u->id,u->name);
	}

// If we're temp send an email else tell them direct.
if (O_FLAGISSET(u,USER_FLAG_TEMP_OBJECT)) {
	// Temp objects are always local users
	lu = (cl_local_user *)u;
	if ((ret = lu->send_system_mail("~FYMUZZLED",text)) != OK) 
		errprintf("Mail send failed: %s\n",err_string[ret]);
	}
else {
	O_SETPREVFLAG(u,USER_FLAG_MUZZLED);  // In case in reader etc
	u->uprintf(text);
	u->group->geprintf(
		MSG_MISC,
		this,u,
		"User ~FT%04X~RS (%s) has been ~FYMUZZLED.\n",u->id,u->name);
	}
	
if (u->type == USER_TYPE_LOCAL && (ret=((cl_local_user *)u)->save()) != OK)
	errprintf("Failed to save user details: %s\n",err_string[ret]);
if (secs) uprintf("User ~FYMUZZLED~RS for %s.\n",time_period(secs));
else uprintf("User ~FYMUZZLED~RS indefinately.\n");

DEL:
if (O_FLAGISSET(u,USER_FLAG_TEMP_OBJECT)) delete u;
}




/*** Unmuzzle a user ***/
void cl_user::com_unmuzzle()
{
cl_user *u;
uint16_t uid;
int ret;

if (com->wcnt != 2) {
	uprintf("Usage: unmuzzle <user id>\n");  return;
	}
if (!(uid = idstr_to_id(com->word[1]))) {
	uprintf(INVALID_USER_ID);  return;
	}
if (!(u = get_user(uid)) && !(u = create_temp_user(uid))) {
	uprintf(NO_SUCH_USER);  return;
	}
if (u == this) {
	uprintf("You cannot unmuzzle yourself. Nice try though.\n");
	goto DEL;
	}
if (!O_FLAGISSET(u,USER_FLAG_MUZZLED)) {
	uprintf("User is already unmuzzled.\n");  goto DEL;
	}
if (u->muzzle_level > level) {
	uprintf("User %04X (%s) was muzzled by a user of level %s. You cannnot unmuzzle them.\n",u->id,u->name,user_level[u->muzzle_level]);
	return;
	}

if ((ret = u->unmuzzle(this)) != OK) 
	errprintf("Failed to save user details: %s\n",err_string[ret]);
uprintf("User ~FGUNMUZZLED.\n");

DEL:
if (O_FLAGISSET(u,USER_FLAG_TEMP_OBJECT)) delete u;
}




/*** Kill a user ***/
void cl_user::com_kill()
{
cl_user *u;
uint16_t uid;

if (com->wcnt != 2) {
	uprintf("Usage: kill <user id>\n");  return;
	}
if (!(uid = idstr_to_id(com->word[1]))) {
	uprintf(INVALID_USER_ID);  return;
	}
if (!(u = get_user(uid))) {
	uprintf(NOT_LOGGED_ON);  return;
	}
if (u == this) {
	uprintf("Suicide is painless... but not if you use the wrong command!\n");
	return;
	}
if (u->level >= level) {
	uprintf("You cannot kill a user of equal or higher level than yourself.\n");
	return;
	}
kill_nkill(u);
}




/*** Kill named user(s) ***/
void cl_user::com_nkill()
{
int cnt,pos;
cl_user *u;

if (com->wcnt != 2) {
	uprintf("Usage: nkill <username>\n");  return;
	}

// Loop through users by name. pos variable only gets increased if we miss
// out a user entry in list so we have to skip it next time.
for(pos=0,cnt=0;(u = get_user_by_name(com->word[1],pos));) {
	if (u == this) {
		uprintf("Murder AND suicide?? You're one sick puppy! Sorry, not today.\n");
		pos++;
		}
	else
	if (u->level >= level) {
		uprintf("You cannot kill ~FT%04X~RS (%s), their level is equal or higher than your own.\n",u->id,u->name);
		pos++;
		}
	else {
		kill_nkill(u);  ++cnt;
		}
	}
if (cnt || pos) uprintf("%d users killed.\n",cnt);
else uprintf(NO_USERS_WITH_NAME);
}




/*** Used by above command functions ***/
void cl_user::kill_nkill(cl_user *u)
{
allprintf(
	MSG_MISC,
	USER_LEVEL_NOVICE,
	u,"User ~FY%04X~RS (%s) has been ~OL~FRKILLED.\n",u->id,u->name);

u->uprintf("\n~OLYou have been ~FRKILLED~RS by %04X (%s)...\n\n",id,name);
uprintf("User %04X (%s) ~FRKILLED.\n",u->id,u->name);
log(1,"User %04X (%s) KILLED user %04X (%s).",id,name,u->id,u->name);

u->disconnect();
}




/*** Shut everything down ***/
void cl_user::com_shutdown()
{
shutdown_reboot(COM_SHUTDOWN);
}




/*** Reboot the server ***/
void cl_user::com_reboot()
{
shutdown_reboot(COM_REBOOT);
}




/*** Do shutdown/reboot commands ***/
void cl_user::shutdown_reboot(int comnum)
{
char *usage = "Usage: %s now/cancel/<time delay> [seconds/minutes/hours/days]\n";
int len;

switch(com->wcnt) {
	case 2:
	len = strlen(com->word[1]);

	shutdown_secs = 0;
	if (!strncasecmp(com->word[1],"now",len)) {
		shutdown_secs = 0;  break;
		}
	if (!strncasecmp(com->word[1],"cancel",len)) {
		switch(shutdown_type) {
			case SHUTDOWN_INACTIVE:
			uprintf("The shutdown/reboot sequence is not currently active.\n");
			return;

			case SHUTDOWN_STOP:
			if (comnum == COM_REBOOT) {
				uprintf("Use the shutdown command to cancel a shutdown.\n");
				return;
				}
			break;

			case SHUTDOWN_REBOOT:
			if (comnum == COM_SHUTDOWN) {
				uprintf("Use the reboot command to cancel a reboot.\n");
				return;
				}
			}

		sprintf(text,"%s sequence ~OL~FGCANCELLED.\n",command[comnum]);
		text[0]=toupper(text[0]);
		allprintf(MSG_SYSTEM,USER_LEVEL_NOVICE,NULL,text);

		shutdown_type = SHUTDOWN_INACTIVE;
		log(1,"User %04X (%s) CANCELLED the %s sequence.\n",
			id,name,command[comnum]);
		return;
		}
	// Fall through

	case 3:
	if ((shutdown_secs = get_seconds(
	     com->word[1],com->wcnt == 3 ? com->word[2] : NULL)) > -1)
		break;
	// Fall through

	default:
	uprintf(usage,command[comnum]);
	return;
	}
if (shutdown_type != SHUTDOWN_INACTIVE) {
	uprintf("The shutdown/reboot sequence is already active and is set for\n%s time. You must cancel it first before starting it again.\n",
		time_period(shutdown_time - server_time));
	return;
	}

uprintf("\n~SN~LI~OL~FRWARNING: This will start the %s sequence!\n\n",
	command[comnum]);
stage = (comnum == COM_SHUTDOWN ? USER_STAGE_SHUTDOWN : USER_STAGE_REBOOT);
}





/*** Imprison a misbehaving user ***/
void cl_user::com_imprison()
{
uint16_t uid;
cl_user *u;
cl_local_user *lu;
int ret,secs;
char *usage = "Usage: imprison <user id> [<sentence length> [seconds/minutes/hours/days]]\n";

switch(com->wcnt) {
	case 2:
	secs = 0;
	break;

	case 3:
	secs = get_seconds(com->word[2],NULL);
	break;

	case 4:
	secs = get_seconds(com->word[2],com->word[3]);
	break;

	default:
	uprintf(usage);
	return;
	}
if (secs < 0) {
	uprintf(usage);  return;
	}
if (!(uid = idstr_to_id(com->word[1]))) {
	uprintf(INVALID_USER_ID);  return;
	}
if (!(u = get_user(uid)) && !(u = create_temp_user(uid))) {
	uprintf(NO_SUCH_USER);  return;
	}
if (u == this) {
	uprintf("You cannot imprison yourself!\n");  goto DEL;
	}
if (u->level >= level) {
	uprintf("You cannot imprison a user of an equal or higher level than yourself.\n");
	goto DEL;
	}
if (O_FLAGISSET(u,USER_FLAG_PRISONER)) {
	uprintf("User %04X (%s) is already a prisoner!\n",u->id,u->name);
	goto DEL;
	}

// Set prison variables
u->imprison_level = level;
u->imprisoned_time = server_time;

if (secs) {
	u->release_time = server_time + secs;

	sprintf(text,"~OLYou have been ~FYIMPRISONED~RS~OL by %04X (%s) for %s.\n",id,name,time_period(secs));
	log(1,"User %04X (%s) IMPRISONED user %04X (%s) for %s.",
		id,name,u->id,u->name,time_period(secs));
	}
else {
	u->release_time = 0;

	sprintf(text,"~OLYou have been ~FYIMPRISONED~RS~OL by %04X (%s) indefinitely!\n",id,name);
	log(1,"User %04X (%s) IMPRISONED user %04X (%s) indefinitely.",
		id,name,u->id,u->name);
	}

O_SETFLAG(u,USER_FLAG_PRISONER);

if (O_FLAGISSET(u,USER_FLAG_TEMP_OBJECT)) {
	lu = (cl_local_user *)u;
	if ((ret = lu->send_system_mail("~FYIMPRISONED",text)) != OK)
		errprintf("Mail send failed: %s\n",err_string[ret]);
	}
else {
	O_SETPREVFLAG(u,USER_FLAG_PRISONER);
	u->uprintf(text);

	// Inform everyone
	u->group->geprintf(
		MSG_MISC,
		this,u,"User ~FY%04X~RS (%s) has been ~OL~FYIMPRISONED.\n",
		u->id,u->name);

	// They may already be in the prison group
	if (u->group != prison_group) {
		prison_group->gprintf(MSG_MISC,"You hear a moan as another user is thrown in the prison...\n");
		prison_group->join(u);
		u->prompt();
		}
	}

if (u->type == USER_TYPE_LOCAL && (ret=((cl_local_user *)u)->save()) != OK)
	errprintf("Failed to save user details: %s\n",err_string[ret]);

if (secs) uprintf("User ~FYIMPRISONED~RS for %s.\n",time_period(secs));
else uprintf("User ~FYIMPRISONED~RS indefinately.\n");

DEL:
if (O_FLAGISSET(u,USER_FLAG_TEMP_OBJECT)) delete u;
}




/*** Release a user from prison ***/
void cl_user::com_release()
{
uint16_t uid;
cl_user *u;
int ret;

if (com->wcnt != 2) {
	uprintf("Usage: release <user id>\n");  return;
	}
if (!(uid = idstr_to_id(com->word[1]))) {
	uprintf(INVALID_USER_ID);  return;
	}
if (!(u = get_user(uid)) && !(u = create_temp_user(uid))) {
	uprintf(NO_SUCH_USER);  return;
	}
if (u == this) {
	uprintf("You cannot release yourself!\n");  goto DEL;
	}
if (!O_FLAGISSET(u,USER_FLAG_PRISONER)) {
	uprintf("User %04X (%s) is not a prisoner.\n",u->id,u->name);
	goto DEL;
	}
if (u->imprison_level > level) {
	uprintf("User %04X (%s) was imprisoned by a user of level %s. You cannot release them.\n",u->id,u->name,user_level[u->imprison_level]);
	goto DEL;
	}
if ((ret = u->release_from_prison(this)) != OK)
	errprintf("Failed to save user details: %s\n",err_string[ret]);
uprintf("User ~FGRELEASED.\n");

DEL:
if (O_FLAGISSET(u,USER_FLAG_TEMP_OBJECT)) delete u;
}




/*** List current server threads. This command doesn't page because there'll
     probably never be enough threads to make it worthwhile. Every bloody
     unix seems to do thread id differently. In Solaris its a lightweight
     process id, in Linux its god knows what (no its not the process id) and
     in BSD its a fucking memory address! So much for consistancy... ***/
void cl_user::com_lsthreads()
{
st_threadentry *te;
char *threadtype[] = {
	"MAIN",
	"EVENTS",
	"CONNECT",
	"SVR RESOLVE",
	"USER RESOLVE"
	};

if (com->wcnt != 1) {
	uprintf(COM_NO_ARGS);  return;
	}

uprintf("\n~BB*** Current threads ***\n\n");
uprintf("~OL~ULType           Thread ID  Spawned on\n");

FOR_ALL_THREADS(te) 
	uprintf("%-12s  %10d  %s",
		threadtype[te->type],te->id,ctime(&te->created));

uprintf("\n~FTTotal of %d threads.\n\n",thread_count);
}




/*** Give out some system info information. This command doesn't page 'cos
     it would be a pain in the arse to implement. ***/
void cl_user::com_sysinfo()
{
int i,len;
char *usage="Usage: sysinfo [general/groups/users/servers]\n";
char *category[] = {
	"general","groups","users","servers"
	};
enum {
	OPT_GENERAL, OPT_GROUPS, OPT_USERS, OPT_SERVERS
	};

switch(com->wcnt) {
	case 1:
	i = OPT_GENERAL;  break;

	case 2:
	len = strlen(com->word[1]);
	for(i=0;i < 4;++i)
		if (!strncasecmp(category[i],com->word[1],len)) break;
	if (i == 4) {
		uprintf(usage);  return;
		}
	break;

	default:
	uprintf(usage);
	return;
	}
uprintf("\n~BB*** System information: %s ***\n\n",category[i]);

switch(i) {
	case OPT_GENERAL:
	uprintf("Server name          : %s\n",server_name);
	uprintf("Server version       : %s\n",VERSION);
	uprintf("Protocol revision    : %d\n",PROTOCOL_REVISION);
	uprintf("Build                : %s\n",build);
	uprintf("Process id           : %d\n",main_pid);
	uprintf("Booted               : %s",ctime(&boot_time));
	uprintf("Uptime               : %s\n",
		time_period(server_time - boot_time));
	uprintf("Server time          : %s",ctime(&server_time));
	uprintf("Config file          : \"%s\"\n",config_file);

	if (logfile) uprintf("Log file             : \"%s\"\n",logfile);
	else uprintf("Log file             : <stdout>\n");

	// Only show dirs to admins as knowing the path the executable lies
	// in and uses could be a security risk
	if (level == USER_LEVEL_ADMIN) {
		uprintf("Original directory   : %s/\n",original_dir);
		uprintf("Working directory    : %s/\n",working_dir);
		}

	uprintf("Bind interface       : %s\n",
		main_bind_addr.sin_addr.s_addr == INADDR_ANY ?
			"ALL" : inet_ntoa(main_bind_addr.sin_addr));
		
	uprintf("User port            : %d\n",tcp_port[PORT_TYPE_USER]);
	uprintf("Server port          : %d\n",tcp_port[PORT_TYPE_SERVER]);
	uprintf("Server port listening: %s\n",
		rcolnoyes[SYS_FLAGISSET(SYS_FLAG_SVR_LISTEN)]);
	uprintf("Thread count         : %d\n",thread_count);
	uprintf("System flags         : 0x%04X\n",system_flags);
	if (shutdown_type != SHUTDOWN_INACTIVE) {
		uprintf("Shutdown type        : %s\n",
			shutdown_type == 1 ? "~FRSHUTDOWN" : "~FYREBOOT");
		uprintf("Shutdown time        : %s",ctime(&shutdown_time));
		uprintf("Shutdown in          : %s\n",
			time_period(shutdown_time - server_time));
		}

	// Ignoring  SIGPIPE & SIGWINCH is hardcoded in
	uprintf("Signals ignored      : PIPE,WINCH");
	if (SYS_FLAGISSET(SYS_FLAG_IGNORING_SIGS)) {
		for(i=0;i < NUM_SIGNALS;++i) 
			if (siglist[i].ignoring) uprintf(",%s",siglist[i].name);
		}
	uprintf("\nAllow 'who' at login : %s\n",
		colnoyes[SYS_FLAGISSET(SYS_FLAG_ALLOW_WHO_AT_LOGIN)]);
	uprintf("Resolve IP internally: %s\n",
		rcolnoyes[SYS_FLAGISSET(SYS_FLAG_INTERNAL_RESOLVE)]);
	uprintf("Autosave interval    : %s\n\n",
		time_period(autosave_interval));
	break;


	case OPT_GROUPS:
	uprintf("Groups count          : %d total (%d system, %d public, %d user)\n",
		system_group_count + public_group_count + user_group_count,
		system_group_count,public_group_count,user_group_count);

	uprintf("Gatecrash level       : %s\n",
		user_level[group_gatecrash_level]);
	uprintf("Modify level          : %s\n",user_level[group_modify_level]);
	uprintf("Invite expire         : %s\n",time_period(group_invite_expire));
	uprintf("Logging               : %s\n",rcolnoyes[SYS_FLAGISSET(SYS_FLAG_LOG_GROUPS)]);
	uprintf("Max name length       : %d characters\n",max_group_name_len);
	uprintf("Max description length: %d characters\n",max_group_desc_chars);
	uprintf("Number of review lines: %d\n",num_review_lines);
	uprintf("Max board message size: %d characters\n",max_board_chars);
	uprintf("Board message expire  : %s\n",time_period(board_msg_expire));
	uprintf("Board renumber        : %s\n\n",
		rcolnoyes[SYS_FLAGISSET(SYS_FLAG_BOARD_RENUM)]);
	break;


	case OPT_USERS:
	uprintf("User count              : %d total (%d local, %d remote)\n",
		local_user_count+remote_user_count,
		local_user_count,remote_user_count);
	if (max_local_users)
		uprintf("Max local users         : %d\n",max_local_users);
	else
		uprintf("Max local users         : <unlimited>\n");
	uprintf("Max remote users        : %d\n",max_remote_users);
	if (max_local_user_data_rate)
		uprintf("Max local user rx data  : %d bytes/sec\n",
			max_local_user_data_rate);
	else
		uprintf("Max local user rx data  : <unlimited>\n");
	uprintf("Max user ignore level   : %s\n",user_level[max_user_ign_level]);
	uprintf("Lockout level           : %s\n",user_level[lockout_level]);
	uprintf("Allow new accounts      : %s\n",
		rcolnoyes[SYS_FLAGISSET(SYS_FLAG_ALLOW_NEW_ACCOUNTS)]);
	uprintf("Save novice accounts    : %s\n",
		rcolnoyes[SYS_FLAGISSET(SYS_FLAG_SAVE_NOVICE_ACCOUNTS)]);
	uprintf("Really delete accounts  : %s\n",
		rcolnoyes[SYS_FLAGISSET(SYS_FLAG_REALLY_DEL_ACCOUNTS)]);
	uprintf("Login timeout           : %s\n",time_period(login_timeout));
	uprintf("Idle timeout            : %s\n",time_period(idle_timeout));
	uprintf("Idle timeout ign level  : %s\n",
		user_level[idle_timeout_ign_level]);
	uprintf("Linkdead timeout        : %s\n",time_period(linkdead_timeout));
	uprintf("Max name length         : %d characters\n",max_name_len);
	uprintf("Max description length  : %d characters\n",max_desc_len);
	uprintf("Default description     : \"%s\"\n",default_desc);
	uprintf("Default password        : \"%s\"\n",default_pwd);
	uprintf("Min password length     : %d\n",min_pwd_len);
	uprintf("Max profile length      : %d characters\n",max_profile_chars);
	uprintf("Max mail length         : %d characters\n",max_mail_chars);
	uprintf("Max broadcast length    : %d characters\n",max_broadcast_chars);
	uprintf("Max batch name length   : %d characters\n",max_batch_name_len);
	uprintf("Max login batch length  : %d lines\n",max_login_batch_lines);
	uprintf("Max logout batch length : %d lines\n",max_logout_batch_lines);
	uprintf("Max session batch length: %d lines\n"
		,max_session_batch_lines);
	uprintf("Go invisible level      : %s\n",user_level[go_invis_level]);
	uprintf("Prisoners returned home : %s\n",
		rcolnoyes[SYS_FLAGISSET(SYS_FLAG_PRS_RET_HOME)]);
	uprintf("Strip rsrvd printcodes  : %s\n\n",
		rcolnoyes[SYS_FLAGISSET(SYS_FLAG_STRIP_PRINTCODES)]);
	break;


	case OPT_SERVERS:
	uprintf("Server count                : %d (%d connected)\n",
		server_count,connected_server_count);
	if (level == USER_LEVEL_ADMIN)
		uprintf("Connect key                 : %u\n",local_connect_key);
	if (soft_max_servers)
		uprintf("Soft max servers            : %d\n",soft_max_servers);
	else
		uprintf("Soft max servers            : <unlimited>\n");
	uprintf("Connect timeout             : %s\n",
		time_period(connect_timeout));
	uprintf("Server timeout              : %s\n",
		time_period(server_timeout));
	uprintf("Max name len                : %d characters\n",
		MAX_SERVER_NAME_LEN);
	uprintf("Max hop count               : %d\n",max_hop_cnt);
	uprintf("Ping interval               : %s\n",
		time_period(send_ping_interval));
	uprintf("Max TX errors               : %d\n",max_tx_errors);
	uprintf("Max RX errors               : %d\n",max_rx_errors);
	if (max_packet_rate)
		uprintf("Max rx packet rate          : %d per second\n",
			max_packet_rate);
	else
		uprintf("Max rx packet rate          : <unlimited>\n");
	if (max_svr_data_rate)
		uprintf("Max rx data rate            : %d bytes/sec\n",
			max_svr_data_rate);
	else
		uprintf("Max rx data rate            : <unlimited>\n");
	uprintf("Delete disconnected incoming: %s\n",
		rcolnoyes[SYS_FLAGISSET(SYS_FLAG_DEL_DISC_INCOMING)]);
	uprintf("Incoming encryption policy  : %s\n",
		inc_encrypt_polstr[incoming_encryption_policy]);
	uprintf("Outgoing encryption enforce : %s\n",
		rcolnoyes[SYS_FLAGISSET(SYS_FLAG_OUT_ENF_ENCRYPTION)]);
	uprintf("Allow random rem ids        : %s\n",
		colnoyes[SYS_FLAGISSET(SYS_FLAG_RANDOM_REM_IDS)]);
	uprintf("Allow loopback              : %s\n",
		colnoyes[SYS_FLAGISSET(SYS_FLAG_LOOPBACK_USERS)]);
	uprintf("Allow remote batch run      : %s\n",
		colnoyes[SYS_FLAGISSET(SYS_FLAG_ALLOW_REM_BATCH_RUNS)]);
	uprintf("Remote user maximum level   : %s\n",
		user_level[remote_user_max_level]);
	uprintf("Receive net broadcast level : %s\n",
		user_level[recv_net_bcast_level]);
	uprintf("Log net broadcasts          : %s\n",
		rcolnoyes[SYS_FLAGISSET(SYS_FLAG_LOG_NETBCAST)]);
	uprintf("Log unexpected packets      : %s\n",
		rcolnoyes[SYS_FLAGISSET(SYS_FLAG_LOG_UNEXPECTED_PKTS)]);
	uprintf("Hexdump packets             : %s\n\n",
		colnoyes[SYS_FLAGISSET(SYS_FLAG_HEXDUMP_PACKETS)]);
	}
}




/*** Fix a group to private or public ***/
void cl_user::com_fix()
{
int ret;

if (com->wcnt != 1) {
	uprintf(COM_NO_ARGS);  return;
	}
if (!group->user_can_modify(this)) {
	uprintf("You cannot fix the access of this group.\n");  return;
	}
if ((ret = group->set_fixed()) != OK) {
	errprintf("Cannot fix access: %s\n",err_string[ret]);
	return;
	}
log(1,"User %04X (%s) FIXED access of group %04X (%s).",
	id,name,group->id,group->name);

group->geprintf(
	MSG_INFO,this,NULL,
	"User ~FT%04X~RS (%s) has ~OL~FRFIXED~RS this groups access.\n",
	id,name);
uprintf("Group access ~OL~FRFIXED.\n");
}




/*** Unfix a groups access ***/
void cl_user::com_unfix()
{
int ret;

if (com->wcnt != 1) {
	uprintf(COM_NO_ARGS);  return;
	}
if (!group->user_can_modify(this)) {
	uprintf("You cannot unfix the access of this group.\n");  return;
	}
if ((ret = group->set_unfixed()) != OK) {
	errprintf("Cannot unfix access: %s\n",err_string[ret]);
	return;
	}
log(1,"User %04X (%s) UNFIXED access of group %04X (%s).",
	id,name,group->id,group->name);
group->geprintf(
	MSG_INFO,this,NULL,
	"User ~FT%04X~RS (%s) has ~OL~FGUNFIXED~RS this groups access.\n",
	id,name);
uprintf("Group access ~OL~FGUNFIXED.\n");
}




/*** Add/delete/load/unload groups ***/
void cl_user::com_group()
{
cl_group *grp;
uint16_t gid;
int subcom,len;
u_char gflags;
char *scstr[] = {
	"create",
	"delete",
	"load",
	"unload",
	"monitor"
	};
enum {
	SC_CREATE,
	SC_DELETE,
	SC_LOAD,
	SC_UNLOAD,
	SC_MONITOR,
	SC_END
	};

if (com->wcnt < 3) goto USAGE;

len = strlen(com->word[1]);
for(subcom=0;subcom < SC_END;++subcom)
	if (!strncasecmp(com->word[1],scstr[subcom],len)) break;
if (subcom == SC_END) goto USAGE;

gid = idstr_to_id(com->word[2]);

switch(com->wcnt) {
	case 3:
	if (subcom == SC_CREATE) goto USAGE;

	if (subcom == SC_MONITOR && !strcasecmp(com->word[2],"none"))
		grp = NULL;
	else {
		if (!gid) {
			uprintf("Invalid group id.\n");  return;
			}
		grp = get_group(gid);

		if (subcom != SC_LOAD) {
			if (!grp) {
				uprintf("No such group or group not loaded.\n");
				return;
				}
			if (subcom != SC_MONITOR &&
			    grp->type != GROUP_TYPE_PUBLIC) {
				uprintf("You can only delete/unload public groups.\n");
				return;
				}
			}
		}

	switch(subcom) {
		case SC_DELETE:
		uprintf("\n~FR~OLWARNING: This will delete this group. Reloading will not be possible!\n\n");
		del_group = grp;
		stage = USER_STAGE_DELETE_GROUP;
		return;


		case SC_LOAD:
		if (grp) {
			uprintf("Group is already loaded.\n");  return;
			}
		if (gid & 0xF000) {
			uprintf("You can only load public groups.\n");  return;
			}
		if ((grp = new cl_group(gid,NULL))->error != OK) {
			errprintf("Can't load group: %s\n",
				err_string[grp->error]);
                        delete grp;
			return;
                        }
		allprintf(
			MSG_INFO,USER_LEVEL_NOVICE,this,
			"Group ~FT%04X~RS (%s~RS) has been ~OL~FGLOADED.\n",
			gid,grp->name);
		log(1,"User %04X (%s) LOADED GROUP %04X (%s).\n",
			id,name,grp->id,grp->name);
		uprintf("Group ~OL~FGLOADED.\n");
		break;


		case SC_UNLOAD:
		allprintf(
			MSG_INFO,USER_LEVEL_NOVICE,this,
			"Group ~FT%04X~RS (%s~RS) has been ~OL~FYUNLOADED.\n",
			gid,grp->name);
		log(1,"User %04X (%s) UNLOADED GROUP %04X (%s).\n",
			id,name,grp->id,grp->name);
		delete grp;
		uprintf("Group ~OL~FYUNLOADED.\n");
		break;


		case SC_MONITOR:
		if (grp) {
			if (grp == mon_group) {
				uprintf("You are already monitoring that group!\n");
				return;
				}
			if (grp->user_is_banned(this)) {
        			uprintf("You are banned from group ~FT%04X~RS (%s~RS), you cannot monitor it!\n",grp->id,grp->name);
				return;
				}
			if (grp != home_group &&
    			    level < group_gatecrash_level &&
    			    O_FLAGISSET(grp,GROUP_FLAG_PRIVATE)) {
				uprintf("Group ~FT%04X~RS (%s~RS) is currently private, you cannot monitor it.\n",grp->id,grp->name);
				return;
				}
			if (mon_group)
				uprintf("Now monitoring group ~FT%04X~RS (%s~RS) instead of group ~FT%04X~RS (%s~RS).\n",
					grp->id,
					grp->name,
					mon_group->id,mon_group->name);
			else
			uprintf("Monitoring group ~FT%04X~RS (%s~RS).\n",
				grp->id,grp->name);
			}
		else {
			if (!mon_group) uprintf("You are not currently monitoring anything.\n");
			else
			uprintf("No longer monitoring group ~FT%04X~RS (%s~RS)\n",
				mon_group->id,mon_group->name);
			}
		mon_group = grp;
		break;


		default: goto USAGE;
		}
	send_server_info_to_servers(NULL);
	return;


	case 4:
	if (subcom == SC_DELETE) {
		if (strcasecmp(com->word[3],"log")) goto USAGE;
		if (!gid) {
			uprintf("Invalid group id.\n");  return;
			}
		grp = get_group(gid);

		if (grp->type == GROUP_TYPE_SYSTEM) {
			uprintf("System groups do not have log files.\n",grp->id);
			return;
			}
		uprintf("\n~FR~OLWARNING: This will delete this groups log file.\n\n");
		del_group = grp;
		stage = USER_STAGE_DELETE_GROUP_LOG;
		return;
		}
	// Fall through


	case 5:
	case 6:
	if (subcom != SC_CREATE) goto USAGE;
	if (!gid) {
		uprintf("Invalid id.\n");  return;
		}
	if ((int)strlen(com->word[3]) > max_group_name_len) {
		uprintf("Name too long, maximum length is %d characters.\n",
			max_group_name_len);
		return;
		} 

	// Get any optional private/fixed flags
	gflags = 0;
	if (com->wcnt > 4) {
		if (!strncasecmp(com->word[4],"private",strlen(com->word[4])))
			gflags = GROUP_FLAG_PRIVATE;
		else
		if (!strncasecmp(com->word[4],"fixed",strlen(com->word[4])))
			gflags = GROUP_FLAG_FIXED;
		else
		goto USAGE;
		}
	if (com->wcnt == 6) {
		if (!strncasecmp(com->word[5],"private",strlen(com->word[5])))
			gflags |= GROUP_FLAG_PRIVATE;
		else
		if (!strncasecmp(com->word[5],"fixed",strlen(com->word[5])))
			gflags |= GROUP_FLAG_FIXED;
		else
		goto USAGE;
		}
	
	grp = new cl_group(gid,com->word[3],GROUP_TYPE_PUBLIC,NULL);
	if (grp->error != OK) {
		errprintf("Group creation failed: %s\n",err_string[grp->error]);
		delete grp;
		return;
		}
	grp->flags = gflags;

	log(1,"User %04X (%s) CREATED GROUP %04X (%s).\n",
		id,name,grp->id,grp->name);
	allprintf(
		MSG_INFO,USER_LEVEL_NOVICE,this,
		"New group ~FT%04X~RS (%s~RS) has been ~OL~FGCREATED.\n",
		grp->id,grp->name);
	uprintf("Group ~OL~FGCREATED.\n");

	send_server_info_to_servers(NULL);
	return;


	default: goto USAGE;
	}
return;

USAGE:
uprintf("Usage: group create  <group id> \"<group name>\" [private] [fixed]\n");
uprintf("       group delete  <group id> [log]\n");
uprintf("       group load    <group id>\n");
uprintf("       group unload  <group id>\n");
uprintf("       group monitor <group id>/none\n");
}




/*** List prisoners ***/
void cl_user::com_lsprison()
{
cl_user *u;
char sft[15],*remain;;
struct tm *tms;
int lcnt,ucnt;

if (com_page_line == -1 &&
    level > USER_LEVEL_LOGIN && com->wcnt != 1) {
	uprintf(COM_NO_ARGS);  return;
	}

lcnt = 0;
ucnt = 0;
FOR_ALL_USERS(u) {
	if (u->level <= USER_LEVEL_LOGIN ||
	    !O_FLAGISSET(u,USER_FLAG_PRISONER) || ++ucnt <= com_page_line)
		continue;

	if (ucnt == 1) {
		uprintf("\n~BB*** Current prisoners ***\n\n");
		uprintf("~UL~OLUID  Name%*s Imp_level Imprisoned   Remaining\n",
			max_name_len-3,"");
		}
	tms = localtime(&u->imprisoned_time);
	strftime(sft,sizeof(sft),"%b %d %H:%M",tms);

	remain = u->release_time ? time_period(u->release_time - server_time) :
	         (char *)"~FYINDEFINITE";

	if (level < u->level && O_FLAGISSET(u,USER_FLAG_INVISIBLE)) 
		uprintf("???? ?%*s %-8s  %s %s\n",max_name_len,"",
			user_level[u->imprison_level],sft,remain);
	else
		uprintf("%04X %-*s  %-8s  %s %s\n",
			u->id,
			max_name_len,u->name,
			user_level[u->imprison_level],sft,remain);

	if (level > USER_LEVEL_LOGIN && 
	    FLAGISSET(USER_FLAG_PAGING) && 
	    ++lcnt == term_rows - 2) {
		com_page_line = ucnt;  return;
		}
	}

// Total & prompt could scroll lines in last page off top so pause at end
// if required.
if (level > USER_LEVEL_LOGIN &&
    FLAGISSET(USER_FLAG_PAGING) && lcnt && lcnt >= term_rows - 5) {
	com_page_line = ucnt;  return;
	}

if (!ucnt) uprintf("There are currently no prisoners.\n");
else uprintf("\n~FTTotal of %d prisoners.\n\n",ucnt);

com_page_line = -1;
}




/*** Completely ban a user. Unlike in gban we don't send a mail to the user
     informing them of the ban since they won't be able to log on to read
     it!  ***/
void cl_user::com_ban()
{
int len,sbt;

if (com->wcnt < 3) goto ERROR;

if (!strcasecmp(com->word[1],"user")) {
	if (com->wcnt != 3) goto ERROR;
	ban_user();
	return;
	}
else
if (!strcasecmp(com->word[1],"site")) {
	switch(com->wcnt) {
		case 3: sbt = SITEBAN_TYPE_ALL;  break;

		case 4:
		len = strlen(com->word[3]);
		for(sbt=0;sbt < NUM_SITEBAN_TYPES;++sbt) {
			if (!strncasecmp(com->word[3],siteban_type[sbt],len))
				break;
			}
		if (sbt == NUM_SITEBAN_TYPES) goto ERROR;
		break;

		default: goto ERROR;
		}
	ban_site(sbt);
	return;
	}

ERROR:
uprintf("Usage: ban user <user id>\n"
        "       ban site <ip number> [user/server/all]\n");
}




/*** Ban a user ***/
void cl_user::ban_user()
{
uint16_t uid;
cl_user *u;
cl_remote_user *ru;
st_user_ban *ub;
FILE *fp;
char path[MAXPATHLEN];

if (!(uid = idstr_to_id(com->word[2]))) {
	uprintf(INVALID_USER_ID);  return;
	}
if (uid == id) {
	uprintf("You cannot ban yourself!\n");  return;
	}
if (!(u = get_user(uid)) &&  !(u = create_temp_user(uid))) {
	uprintf(NO_SUCH_USER);  return;
	}
if (level <= u->level) {
	uprintf("You cannot ban a user of an equal or greater level than yourself.\n");
	goto DEL;
	}
sprintf(path,"%s/%s",ETC_DIR,BAN_FILE);
if (!(fp = fopen(path,"a"))) {
	errprintf("Cannot open ban file to write: %s\n",strerror(errno));
	goto DEL;
	}

// Add new ban to list. Don't need to check if user already banned since you
// can only ban them if they're logged on and they get kicked off by this.
ub = new st_user_ban;
ub->level = level;
ub->utype = u->type;
ub->user = NULL;  // Always null since they're kicked off
bzero(&ub->home_addr,sizeof(sockaddr_in));

// Write user bans. Don't call write_ban_file() since we only need to 
// append new entry onto end , no point in rewriting whole file.
if (u->type == USER_TYPE_LOCAL) {
	ub->uid = uid;
	fprintf(fp,"\"user ban\" = %s, %04X\n",user_level[level],uid);
	}
else {
	ru = (cl_remote_user *)u;
	ub->uid = ru->orig_id;
	ub->home_addr.sin_addr.s_addr = u->ip_addr.sin_addr.s_addr;
	ub->home_addr.sin_port = u->ip_addr.sin_port;

	fprintf(fp,"\"user ban\" = %s, %04X, %s, %u\n",
		user_level[level],
		ru->orig_id,
		u->ipnumstr,
		ntohs(u->ip_addr.sin_port));
	}
fclose(fp);

add_list_item(first_user_ban,last_user_ban,ub);

log(1,"User %04X (%s) has BANNED user %04X (%s).",id,name,u->id,u->name);

if (!O_FLAGISSET(u,USER_FLAG_TEMP_OBJECT)) {
	allprintf(
		MSG_MISC,
		USER_LEVEL_NOVICE,
		u,"User ~FY%04X~RS (%s) has been ~OL~FRBANNED.\n",u->id,u->name);
	u->uprintf("\n~OLYou have been ~FRBANNED~RS by %04X (%s)...\n\n",
		id,name);

	u->disconnect();
	}

uprintf("User ~FT%04X ~FRBANNED.\n",u->id);

DEL:
if (O_FLAGISSET(u,USER_FLAG_TEMP_OBJECT)) delete u;
}




/*** Ban a site. This does NOT kick off any users or servers connected from 
     that site though I may add that functionality in the future (if I can
     be arsed). ***/
void cl_user::ban_site(int sbtype)
{
FILE *fp;
st_site_ban *sb;
uint32_t addr;
char path[MAXPATHLEN];
char *ip;

// For now we can only use numeric addresses since a name address would have
// to be resolved and that means I'd have to set up another thread type.
if ((int)(addr = inet_addr(com->word[2])) == -1) {
	uprintf("Invalid IP number. Please note that only numeric IP addresses can be used.\n");
	return;	
	}
FOR_ALL_SITE_BANS(sb) {
	if (sb->addr.sin_addr.s_addr == addr) {
		uprintf("That site is already banned. If you wish to change its type you must unban it\nfirst then reban it using the new type.\n");
		return;
		}
	}
sprintf(path,"%s/%s",ETC_DIR,BAN_FILE);
if (!(fp = fopen(path,"a"))) {
	errprintf("Cannot open ban file to write: %s\n",strerror(errno));
	return;
	}
fprintf(fp,"\"site ban\" = %s, %s, %s\n",
	user_level[level],com->word[2],siteban_type[sbtype]);
fclose(fp);

sb = new st_site_ban;
sb->type = sbtype;
sb->level = level;
bzero(&sb->addr,sizeof(sockaddr_in));
sb->addr.sin_addr.s_addr = addr;
add_list_item(first_site_ban,last_site_ban,sb);

ip = inet_ntoa(sb->addr.sin_addr);
log(1,"User %04X (%s) has BANNED site %s, type: %s.",
	id,name,ip,siteban_type[sbtype]);

allprintf(MSG_INFO,USER_LEVEL_MONITOR,this,
	"User ~FT%04X~RS (%s) has ~OL~FRBANNED~RS site %s, type: %s\n",
	id,name,ip,siteban_type[sbtype]);

uprintf("Site ~FT%s ~FRBANNED~RS for type: ~FM%s\n",ip,siteban_type[sbtype]);
}




/*** Unban a user. Unlike in gunban we don't send them an email if they're
     local as its pointless ***/
void cl_user::com_unban()
{
int num;

if (com->wcnt == 3) {
	if (!is_integer(com->word[2]) || (num=atoi(com->word[2])) < 1) {
		uprintf("Invalid entry number.\n");  return;
		}
	if (!strcasecmp(com->word[1],"user")) {
		unban_user(num);  return;
		}
	else
	if (!strcasecmp(com->word[1],"site")) {
		unban_site(num);  return;
		}
	}
uprintf("Usage: unban user/site <entry number>\n");
}




/*** Unban a user ***/
void cl_user::unban_user(int num)
{
st_user_ban *ub;
int cnt,ret;
uint16_t prt;
char *as;

for(cnt=1,ub=first_user_ban;ub && cnt < num;ub=ub->next,++cnt);
if (!ub) {
	uprintf("There is no such entry.\n");  return;
	}

if (ub->level > level) {
	uprintf("That user was banned by a user of level %s. You cannnot unban them.\n",user_level[ub->level]);
	return;
	}

uprintf("User ~FT%04X ~FGUNBANNED~RS, entry ~FT%d~RS deleted.\n",ub->uid,num);
if (ub->next) uprintf("Please note that other entry numbers will now have changed.\n");

if (ub->utype == USER_TYPE_LOCAL) {
	log(1,"User %04X (%s) DELETED user ban entry %d: local user %04X",
		id,name,num,ub->uid);

	allprintf(MSG_INFO,USER_LEVEL_MONITOR,this,
		"User ~FT%04X~RS (%s) has ~OL~FGUNBANNED~RS local user %04X.\n",

		id,name,ub->uid);
	}
else {
	as = inet_ntoa(ub->home_addr.sin_addr);
	prt = ntohs(ub->home_addr.sin_port);

	log(1,"User %04X (%s) DELETED user ban entry %d: remote user %04X, %s:%d",
		id,name,num,ub->uid,as,prt);

	allprintf(MSG_INFO,USER_LEVEL_MONITOR,this,
		"User ~FT%04X~RS (%s) has ~OL~FGUNBANNED~RS remote user %04X, %s:%d\n",
		id,name,ub->uid,as,prt);
	}

remove_list_item(first_user_ban,last_user_ban,ub);

if ((ret = write_ban_file()) != OK) 
	errprintf("Couldn't write banfile: %s\n",err_string[ret]);
}




/*** Unban a site ***/
void cl_user::unban_site(int num)
{
st_site_ban *sb;
char *ip;
int cnt,ret;

for(cnt=1,sb=first_site_ban;sb && cnt < num;sb=sb->next,++cnt);
if (!sb) {
	uprintf("There is no such entry.\n");  return;
	}
if (sb->level > level) {
	uprintf("That site was banned by a user of level %s. You cannnot unban it.\n",user_level[sb->level]);
	return;
	}
ip = inet_ntoa(sb->addr.sin_addr);
uprintf("Site ~FT%s ~FGUNBANNED~RS, entry ~FT%d~RS deleted.\n",ip,num);
if (sb->next) uprintf("Please note that other entry numbers will now have changed.\n");

log(1,"User %04X (%s) DELETED site ban entry %d, %s\n",id,name,num,ip);

allprintf(MSG_INFO,USER_LEVEL_MONITOR,this,
	"User ~FT%04X~RS (%s) has ~OL~FGUNBANNED~RS site %s\n",id,name,ip);

remove_list_item(first_site_ban,last_site_ban,sb);

if ((ret = write_ban_file()) != OK) 
	errprintf("Couldn't write banfile: %s\n",err_string[ret]);
}




/*** List current user bans. Paging command. ***/
void cl_user::com_lsubans()
{
st_user_ban *ub;
int lcnt,bcnt;

if (com_page_line == -1) {
	if (level > USER_LEVEL_LOGIN && com->wcnt != 1) {
		uprintf(COM_NO_ARGS);  return;
		}
	if (!first_user_ban) {
		uprintf("There are currently no banned users.\n");  return;
		}
	uprintf("\n~BB*** Current user bans ***\n\n");
	uprintf("~OL~ULEntry Ban level Type   UID  Orig IP\n");
	}

lcnt = 0;
bcnt = 0;
FOR_ALL_USER_BANS(ub) {
	if (++bcnt <= com_page_line) continue;

	if (ub->utype == USER_TYPE_LOCAL) 
		uprintf("~FY%5d~RS %-8s  %-6s %04X N/A\n",
			bcnt,
			user_level[ub->level],user_type[ub->utype],ub->uid); 
	else
		uprintf("~FY%5d~RS %-8s  %-6s %04X %s:%u\n",
			bcnt,
			user_level[ub->level],
			user_type[ub->utype],
			ub->uid,
			inet_ntoa(ub->home_addr.sin_addr),
			ntohs(ub->home_addr.sin_port));

	if (level > USER_LEVEL_LOGIN && 
	    FLAGISSET(USER_FLAG_PAGING) && ++lcnt == term_rows - 2) {
		com_page_line = bcnt;  return;
		}
	}

if (FLAGISSET(USER_FLAG_PAGING) && lcnt && lcnt >= term_rows - 5) {
	com_page_line = bcnt;
	return;
	}

uprintf("\n~FTTotal of %d users banned.\n\n",bcnt);
com_page_line = -1;
}




/*** List current site bans ***/
void cl_user::com_lssbans()
{
st_site_ban *sb;
int lcnt,bcnt;

if (com_page_line == -1) {
	if (level > USER_LEVEL_LOGIN && com->wcnt != 1) {
		uprintf(COM_NO_ARGS);  return;
		}
	if (!first_site_ban) {
		uprintf("There are currently no banned sites.\n");  return;
		}
	uprintf("\n~BB*** Current site bans ***\n\n");
	uprintf("~OL~ULEntry Ban level Type   IP\n");
	}

lcnt = 0;
bcnt = 0;
FOR_ALL_SITE_BANS(sb) {
	if (++bcnt <= com_page_line) continue;

	uprintf("~FY%5d~RS %-8s  %-6s %s\n",
		bcnt,
		user_level[sb->level],
		siteban_type[sb->type],inet_ntoa(sb->addr.sin_addr));

	if (level > USER_LEVEL_LOGIN && 
	    FLAGISSET(USER_FLAG_PAGING) && ++lcnt == term_rows - 2) {
		com_page_line = bcnt;  return;
		}
	}

if (FLAGISSET(USER_FLAG_PAGING) && lcnt && lcnt >= term_rows - 5) {
	com_page_line = bcnt;
	return;
	}

uprintf("\n~FTTotal of %d sites banned.\n\n",bcnt);
com_page_line = -1;
}




/*** Save the current system status out to a config file ***/
void cl_user::com_savecfg()
{
struct stat fs;
int len;

switch(com->wcnt) {
	case 1:
	com_filename = strdup(config_file);
	break;

	case 2:
	if (!valid_filename(com->word[1])) {
		uprintf("Invalid filename. Do not use double dots, slashes, percents, tildes or control\ncharacters.\n");
		return;
		}
	if (!(com_filename = (char *)malloc(strlen(com->word[1])+5))) {
		errprintf("Memory allocation error: %s\n",strerror(errno));
		return;
		}

	// Put config extension on the end of filename if not there already
	len = strlen(com->word[1]);
	if (len > 3 && !strcmp(com->word[1]+len-4,CONFIG_EXT)) 
		strcpy(com_filename,com->word[1]);
	else
		sprintf(com_filename,"%s%s",com->word[1],CONFIG_EXT);

	break;

	default:
	uprintf("Usage: savecfg [<filename>]\n");
	return;
	}

// See if file already exists and if it does give a warning
if (!stat(com_filename,&fs)) {
	warnprintf("File '%s' already exists...\n\n",com_filename);
	stage = USER_STAGE_OVERWRITE_CONFIG_FILE;
	}
else run_save_config(0);
}




/*** Wake up another user ***/
void cl_user::com_wake()
{
uint16_t uid;
cl_user *u;

if (com->wcnt != 2) {
	uprintf("Usage: wake <user id>\n");  return;
	}
if (!(uid = idstr_to_id(com->word[1]))) {
	uprintf(INVALID_USER_ID);  return;
	}
if (!(u = get_user(uid))) {
	uprintf(NOT_LOGGED_ON);  return;
	}
if (u == this) {
	uprintf("You're awake already arn't you?\n");  return;
	}
if (level <= u->level) {
	uprintf("You cannot send a wake up to a user of equal or higher level than yourself.\n");
	return;
	}
u->uprintf("~SN~FMPRIVATE ~FT%04X,%s~FG:~RS ~OL~LI~BBWAKE UP!!\n",id,name);
uprintf("Wake up sent.\n");
}




/*** Move a user to another group ***/
void cl_user::com_move()
{
cl_user *u;
cl_group *grp;
uint16_t uid,gid;

if (com->wcnt != 3) {
	uprintf("Usage: move <user id> <group id>/here\n");  return;
	}
if (!(uid = idstr_to_id(com->word[1]))) {
	uprintf(INVALID_USER_ID);  return;
	}
if (!(u = get_user(uid))) {
	uprintf(NOT_LOGGED_ON);  return;
	}
if (u == this) {
	uprintf("Use the \"join\" command to move yourself. Muppet.\n");
	return;
	}
if (level <= u->level) {
	uprintf("You cannot move a user of equal or higher level than yourself.\n");
	return;
	}
if (u->group == prison_group && O_FLAGISSET(u,USER_FLAG_PRISONER)) {
	uprintf("User %04X (%s) is currently a prisoner and cannot be moved.\n",
		u->id,u->name);
	return;
	}

if (!strcasecmp(com->word[2],"here")) grp = group;
else {
	if (!(gid = idstr_to_id(com->word[2]))) {
		uprintf("Invalid group id.\n");  return;
		}
	if (!(grp = get_group(gid))) {
		uprintf(NO_SUCH_GROUP);  return;
		}
	}
if (u->group == grp) {
	uprintf("User %04X (%s) is already in group %04X (%s~RS).\n",
		u->id,u->name,grp->id,grp->name);
	return;
	}

if (grp == gone_remote_group) {
	uprintf("User %04X (%s) cannot be moved to group %04X (%s~RS).\n",
		u->id,u->name,grp->id,grp->name);
	return;
	}

// If we're below group gatecrash level we can't force a user to go where
// we couldn't.
if (grp != home_group &&
    grp != u->home_group && level < group_gatecrash_level) {
	if (grp == prison_group) {
		uprintf("User %04X (%s) cannot be moved to group %04X (%s~RS) by you.\n",u->id,u->name,grp->id,grp->name);
		return;
		}
	if (grp->user_is_banned(u)) {
		uprintf("User %04X (%s) is banned from group %04X (%s~RS).\n",
			u->id,u->name,grp->id,grp->name);
		return;
		}
	if (O_FLAGISSET(grp,GROUP_FLAG_PRIVATE)) {
		uprintf("Group %04X (%s~RS) is currently private.\n",
			grp->id,grp->name);
		return;
		}
	}

log(1,"User %04X (%s) MOVED user %04X (%s) to group %04X (%s)",
	id,name,u->id,u->name,grp->id,grp->name);
u->group->geprintf(MSG_INFO,this,u,"User ~FT%04X~RS (%s) has been moved.\n",
	u->id,u->name);
u->uprintf("\n~LI~OL~BB~FT*** MOVED by user %04X (%s) ***\n",id,name);

grp->join(u);
u->prompt();

uprintf("User ~FGMOVED.\n");
}




/*** Send a broadcast message to the whole talker ***/
void cl_user::com_bcast()
{
if (FLAGISSET(USER_FLAG_MUZZLED)) {
	uprintf("You cannot broadcast while you are muzzled.\n");
	return;
	}
if (FLAGISSET(USER_FLAG_PRISONER)) {
	uprintf("You cannot broadcast while you are a prisoner!\n");
	return;
	}
if (com->wcnt > 1) {
	send_broadcast(com->wordptr[1]);  return;
	}
editor = new cl_editor(this,EDITOR_TYPE_BCAST,NULL);
if (editor->error != OK) {
	errprintf("Editor failure: %s\n",err_string[editor->error]);
	delete editor;
	}
}




/*** Send a shout to all users of monitor level and higher if they can hear
     shouts. ***/
void cl_user::com_admshout()
{
int lev;

if (com->wcnt < 3) {
	uprintf("Usage: admshout <level> <text>\n");  return;
	}
if (FLAGISSET(USER_FLAG_MUZZLED)) {
	uprintf("You cannot shout while you are muzzled.\n");
	return;
	}
if (FLAGISSET(USER_FLAG_NO_SHOUTS)) {
	uprintf("You cannot shout while you have NOSHOUTS on.\n");
	return;
	}
if (FLAGISSET(USER_FLAG_PRISONER)) {
	uprintf("You cannot shout while you are a prisoner!\n");
	return;
	}
if ((lev = get_level(com->word[1])) == -1) {
	uprintf("Invalid level.\n");  return;
	}
if (lev < USER_LEVEL_MONITOR) {
	uprintf("This command is only for messages to user of monitor level and above.\n");
	return;
	}
if (lev > level) {
	uprintf("The shout level cannot be greater than your own level.\n");
	return;
	}

// Use allprintf(). We don't check shout flags here as these messages are
// important and should not be missed.
allprintf(MSG_SHOUT,lev,NULL,
	"~OL~FYADMSHOUT~RS ~FT%04X,%s~FG:~RS %s\n",id,name,com->wordptr[2]);
}




/*** View the system log ***/
void cl_user::com_viewlog()
{
cl_group *grp;
uint16_t gid;
int ret,len;
char *lf;

if (com->wcnt < 2 || com->wcnt > 3) goto USAGE;

len = strlen(com->word[1]);

if (!strncasecmp(com->word[1],"system",len)) {
	if (!logfile) {
		uprintf("There is no log file to view, the system is logging to standard output.\n");
		return;
		}
	if (!strcmp(logfile,"/dev/null")) {
		uprintf("There is no log file to view, the system is logging to /dev/null.\n");
		return;
		}
	uprintf("\n~BB*** System logfile ***\n\n");
	if ((ret=page_file(logfile,1)) != OK) 
		errprintf("Cannot display logfile: %s\n",err_string[ret]);
	return;
	}

if (!strncasecmp(com->word[1],"group",len)) {
	if (com->wcnt == 2) {
		gid = group->id;
		lf = group->glogfile;
		}
	else {
		if (!(gid = idstr_to_id(com->word[2]))) {
			uprintf("Invalid group id.\n");  return;
			}
		if (!(grp = get_group(gid))) {
			uprintf(NO_SUCH_GROUP);  return;
			}
		lf = grp->glogfile;
		}

	uprintf("\n~BB*** Group %04X logfile ***\n\n",gid);

	if ((ret=page_file(lf,1)) != OK) 
		errprintf("Cannot display logfile: %s\n",err_string[ret]);
	return;
	}


USAGE:
uprintf("Usage: viewlog system\n"
        "               group  [<group id>]\n");
}




/*** Clear the review buffer of the group. A user can do this if it is their
     own group or group is public & level > USER or any group & level == ADMIN
 ***/
void cl_user::com_revclr()
{
int i;
char *err = "You cannot clear the buffer of this group.\n";

if (com->wcnt != 1) {
	uprintf(COM_NO_ARGS);  return;
	}
if (!group->revbuff[0].line) {
	uprintf("The review buffer is already cleared.\n");  return;
	}
switch(group->type) {
	case GROUP_TYPE_USER:
	if (group != home_group && level < USER_LEVEL_ADMIN) {
		uprintf(err);  return;
		}
	break;

	case GROUP_TYPE_PUBLIC:
	case GROUP_TYPE_SYSTEM:
	if (level < USER_LEVEL_MONITOR) {
		uprintf(err);  return;
		}
	}
for(i=0;i < num_review_lines;++i) {
	FREE(group->revbuff[i].line);
	group->revbuff[i].alloc = 0;
	}
group->geprintf(
	MSG_INFO,
	this,NULL,"User ~FT%04X~RS (%s) has cleared the review buffer.\n",
	id,name);
uprintf("Review buffer cleared.\n");
}




/*** Display the current news (if any) ***/
void cl_user::com_news()
{
char path[MAXPATHLEN];
int ret;

if (com->wcnt != 1) uprintf(COM_NO_ARGS); 
else {
	sprintf(path,"%s/%s",ETC_DIR,NEWS_FILE);
	if ((ret=page_file(path,1)) != OK) 
		uprintf("There is no news at the moment.\n");
	}
}




/*** Toggle various system flags on and off ***/
void cl_user::com_systoggle()
{
char *opt[]={
	"Rethome",
	"Rrids",
	"Loopback",
	"Renum",
	"Lognetbcast",
	"Pkthexdump",
	"Stripcodes",
	"Whoatlogin",
	"Rembatchruns",
	"Newaccounts",
	"Deldiscin",
	"Loguxpkts",
	"Svrlisten",
	"Outenforce",
	"Resolvint",
	"Savenovices",
	"Delaccounts",
	"Loggroups",
	"*"
	};
int flagslist[] = {
	SYS_FLAG_PRS_RET_HOME,
	SYS_FLAG_RANDOM_REM_IDS,
	SYS_FLAG_LOOPBACK_USERS,
	SYS_FLAG_BOARD_RENUM,
	SYS_FLAG_LOG_NETBCAST,
	SYS_FLAG_HEXDUMP_PACKETS,
	SYS_FLAG_STRIP_PRINTCODES,
	SYS_FLAG_ALLOW_WHO_AT_LOGIN,
	SYS_FLAG_ALLOW_REM_BATCH_RUNS,
	SYS_FLAG_ALLOW_NEW_ACCOUNTS,
	SYS_FLAG_DEL_DISC_INCOMING,
	SYS_FLAG_LOG_UNEXPECTED_PKTS,
	SYS_FLAG_SVR_LISTEN,
	SYS_FLAG_OUT_ENF_ENCRYPTION,
	SYS_FLAG_INTERNAL_RESOLVE,
	SYS_FLAG_SAVE_NOVICE_ACCOUNTS,
	SYS_FLAG_REALLY_DEL_ACCOUNTS,
	SYS_FLAG_LOG_GROUPS
	};	
int i,on,ret;
cl_group *grp;

if (com->wcnt == 2) {
	for(i=0;opt[i][0] != '*';++i) 
		if (!strncasecmp(com->word[1],opt[i],strlen(com->word[1])))
			break;

	if (opt[i][0] != '*') {
		on=0;
		if (SYS_FLAGISSET(flagslist[i])) SYS_UNSETFLAG(flagslist[i]);
		else {
			SYS_SETFLAG(flagslist[i]);  on = 1;
			}
		uprintf("%s mode %s\n",opt[i],offon[on]);
		log(1,"User %04X (%s) toggled system flag option %s %s\n",
			id,name,opt[i],on ? "ON" : "OFF");

		// Deal with special cases that require more than just a 
		// flag to be toggled
		switch(flagslist[i]) {
			case SYS_FLAG_SVR_LISTEN:
			// Have to open or close the port here.
			if (on) {
				if ((ret = create_listen_socket(PORT_TYPE_SERVER)) == OK) return;

				// Couldn't open it 
				errprintf("Unable to open port: %s: %s\n",
					err_string[ret],strerror(errno));
				log(1,"ERROR: cl_user::com_systoggle(): create_listen_socket(): %s: %s\n",err_string[ret],strerror(errno));

				SYS_UNSETFLAG(SYS_FLAG_SVR_LISTEN);
				}
			else close(listen_sock[PORT_TYPE_SERVER]);
			break;

			case SYS_FLAG_LOG_GROUPS:
			FOR_ALL_GROUPS(grp) {
				grp->grouplog(true,(char *)(on ? 
					"~BB********************** GROUP LOGGING ON *********************\n" : 
					"~BM********************** GROUP LOGGING OFF ********************\n"));
				}
			}
		return;
		}
	}

uprintf("Usage: systoggle rethome      (prisoners returned to home group on release)\n");
uprintf("                 rrids        (Random Remote user Ids)\n");
uprintf("                 loopback     (loopback users)\n");
uprintf("                 renum        (auto renumber public boards)\n");
uprintf("                 lognetbcast  (log net broadcast messages)\n");
uprintf("                 pkthexdump   (hexdump packets for net debug)\n");
uprintf("                 stripcodes   (strip reserved printcodes from user input)\n");
uprintf("                 whoatlogin   (current user listing at login prompt)\n");
uprintf("                 rembatchruns (allow batch runs when remote)\n");
uprintf("                 newaccounts  (allow new user accounts to be created at login)\n");
uprintf("                 deldiscin    (delete disconnected incoming server entries).\n");
uprintf("                 loguxpkts    (log unexpected packets)\n");
uprintf("                 svrlisten    (switch server port on or off)\n");
uprintf("                 outenforce   (enforce encryption on outgoing connections)\n");
uprintf("                 resolvint    (attempt to resolve ip names internally first)\n");
uprintf("                 savenovices  (save novice user accounts on logout)\n");
uprintf("                 delaccounts  (really delete user accounts, don't just rename)\n");
uprintf("                 loggroups    (set group logging on/off)\n");
}




/*** Send a network broadcast message ***/
void cl_user::com_netbcast()
{
if (FLAGISSET(USER_FLAG_MUZZLED)) {
	uprintf("You cannot broadcast while you are muzzled.\n");
	return;
	}
if (FLAGISSET(USER_FLAG_PRISONER)) {
	uprintf("You cannot broadcast while you are a prisoner!\n");
	return;
	}
if (com->wcnt > 1) {
	send_net_broadcast(com->wordptr[1]);  return;
	}
editor = new cl_editor(this,EDITOR_TYPE_NET_BCAST,NULL);
if (editor->error != OK) {
	errprintf("Editor failure: %s\n",err_string[editor->error]);
	delete editor;
	}
}




/*** Show a list of all the ip addresses for users and servers and their 
     resolved names ***/
void cl_user::com_lsip()
{
cl_user *u;
cl_server *svr;
int cnt,lcnt;

if (com_page_line == -1) {
	if (level > USER_LEVEL_LOGIN && com->wcnt != 1) {
		uprintf(COM_NO_ARGS);  return;
		}
	uprintf("\n~BB*** Current user & server IP addresses ***\n\n");
	uprintf("~UL~OLSource  IPnum            IPname\n");
	}

cnt = 0;
lcnt = 0;
FOR_ALL_USERS(u) {
	if (u->type == USER_TYPE_LOCAL && ++cnt > com_page_line) {
		uprintf("USER    %-15s  %s\n",u->ipnumstr,u->ipnamestr);

		if (FLAGISSET(USER_FLAG_PAGING) && ++lcnt == term_rows - 2) {
			com_page_line = cnt;  return;
			}
		}
	}

FOR_ALL_SERVERS(svr) {
	if ((svr->stage == SERVER_STAGE_CONNECTED ||
	     svr->stage == SERVER_STAGE_INCOMING) &&
	    ++cnt > com_page_line) {
		uprintf("SERVER  %-15s  %s\n",svr->ipnumstr,svr->ipnamestr);
		if (FLAGISSET(USER_FLAG_PAGING) && ++lcnt == term_rows - 2) {
			com_page_line = cnt;  return;
			}
		}
	}
// Total & prompt could scroll lines in last page off top so pause at end
// if required.
if (FLAGISSET(USER_FLAG_PAGING) && lcnt && lcnt >= term_rows - 5) {
	com_page_line = cnt;  return;
	}

uprintf("\n~FTTotal of %d addresses.\n\n",cnt);
com_page_line = -1;
}




/*** Think something ***/
void cl_user::com_think()
{
int ret;

if (com->wcnt < 2) {
	uprintf("Usage: think <text>\n");  return;
	}
if ((ret=group->speak(COM_THINK,this,com->wordptr[1])) != OK)
	errprintf("You cannot think: %s\n",err_string[ret]);
}




/*** Set a server config time value. This multi word option approach is
     inconsistent with the precedant set in systoggle but I don't care,
     its more intuitive. I'm not going to change the systoggle option names
     for the sake of it now but if writing systoggle now I'd have used this
     approach. ***/
void cl_user::com_syset()
{
char *opt[] = {
	"Ping interval",
	"Server timeout",
	"Connect timeout",
	"Linkdead timeout",
	"Login timeout",
	"Idle timeout",
	"Invite expire",
	"Board msg expire",
	"Autosave interval"
	};
enum {
	OPT_PING_INTERVAL,
	OPT_SERVER_TIMEOUT,
	OPT_CONNECT_TIMEOUT,
	OPT_LINKDEAD_TIMEOUT,
	OPT_LOGIN_TIMEOUT,
	OPT_IDLE_TIMEOUT,
	OPT_INVITE_EXPIRE,
	OPT_MSG_EXPIRE,
	OPT_AUTOSAVE_INT,

	OPT_END
	};
int secs,len,i;

switch(com->wcnt) {
	case 3:
	secs = get_seconds(com->word[2],NULL);
	break;

	case 4:
	secs = get_seconds(com->word[2],com->word[3]);
	break;

	default:
	goto USAGE;
	}
if (secs < 0) goto USAGE;

len = strlen(com->word[1]);
for(i=0;i != OPT_END;++i) if (!strncasecmp(opt[i],com->word[1],len)) break;
if (i == OPT_END) goto USAGE;

switch(i) {
	case OPT_PING_INTERVAL:
	if (!secs) {
		errprintf("Ping interval must be greater than zero.\n");
		return;
		}
	send_ping_interval = secs;
	break;

	case OPT_SERVER_TIMEOUT:
	server_timeout = secs;
	break;

	case OPT_CONNECT_TIMEOUT:
	connect_timeout = secs;
	break;

	case OPT_LINKDEAD_TIMEOUT:
	linkdead_timeout = secs;
	break;

	case OPT_LOGIN_TIMEOUT:
	login_timeout = secs;
	break;
	
	case OPT_IDLE_TIMEOUT:
	idle_timeout = secs;
	break;

	case OPT_INVITE_EXPIRE:
	group_invite_expire = secs;
	break;

	case OPT_MSG_EXPIRE:
	board_msg_expire = secs;
	break;

	case OPT_AUTOSAVE_INT:
	autosave_interval = secs;
	break;

	default: goto USAGE;
	}

uprintf("%s set to %s\n",opt[i],time_period(secs));
log(1,"User %04X (%s) set %s to %s\n",id,name,opt[i],time_period(secs));
return;

USAGE:
uprintf("Usage: syset \"ping interval\"    [seconds/minutes/hours/days]\n");
uprintf("             \"server timeout\"\n");
uprintf("             \"connect timeout\"\n");
uprintf("             \"linkdead timeout\"\n");
uprintf("             \"login timeout\"\n");
uprintf("             \"idle timeout\"\n");
uprintf("             \"invite expire\"\n");
uprintf("             \"board msg expire\"\n");
uprintf("             \"autosave interval\"\n");
}
