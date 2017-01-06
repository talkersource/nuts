/***************************************************************************
 FILE: cl_group.cc
 LVU : 1.4.1

 DESC:
 Object for a group including code to join/leave users, set to private/public
 and so on.

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


/*** Given id load config file. This is for public and user groups ***/
cl_group::cl_group(uint16_t gid, cl_local_user *own)
{
FILE *fp;
cl_group *grp;
struct stat fs;
int linenum,ban_level,i,err;
uint16_t uid;
cl_splitline sl(1);
char path[MAXPATHLEN];
char line[ARR_SIZE];
char *config_option[]={
	"name",
	"private",
	"fixed",
	"ban"
	};
enum {
	OPT_NAME,
	OPT_PRIVATE,
	OPT_FIXED,
	OPT_BAN,

	OPT_END
	};

name = NULL;

/* Its inefficient having to call init first but if theres an error on
   group loading everything has to be set up ok for the destructor to
   work properly */
init(gid,own ? GROUP_TYPE_USER : GROUP_TYPE_PUBLIC,own);

if ((type == GROUP_TYPE_USER && id < MIN_LOCAL_USER_ID) ||
    (type == GROUP_TYPE_PUBLIC && id >= MIN_LOCAL_USER_ID)) {
	error = ERR_INVALID_ID;  return;
	}

// See if group with this id already exists
if ((grp = get_group(gid)) && grp != this) {
	error = ERR_DUPLICATE_ID;  return;
	}

// User group config
if (own) {
	sprintf(path,"%s/%04X/%s",USER_DIR,id,USER_GROUP_CONFIG_FILE);
	sprintf(glogfile,"%s/%04X/%s",USER_DIR,id,USER_GROUP_LOG_FILE);
	}
else {
	// Public group config
	sprintf(path,"%s/%04X/",PUB_GROUP_DIR,gid);
	if (stat(path,&fs) == -1 || (fs.st_mode & S_IFMT) != S_IFDIR) {
		error = ERR_NO_DIR;  return;
		}
	strcat(path,PUB_GROUP_CONFIG_FILE);

	sprintf(glogfile,"%s/%04X/%s",PUB_GROUP_DIR,gid,PUB_GROUP_LOG_FILE);
	}

// Open config file
if (!(fp=fopen(path,"r"))) {
	error = ERR_CANT_OPEN_FILE;  return;
	}

err = 0;
linenum = 1;
fgets(line,ARR_SIZE-1,fp);

while(!feof(fp) && !(err = ferror(fp))) {
	if (sl.parse(line) != OK) goto ERROR;
	if (sl.wcnt) {
		for(i=0;i < OPT_END;++i)
			if (!strcmp(sl.word[0],config_option[i])) break;

		switch(i) {
			case OPT_NAME:
			if (sl.wcnt > 2 || !sl.word[1][0]) goto ERROR;
			set_name(sl.word[1]);
			break;


			case OPT_PRIVATE:
			if (!strcasecmp(sl.word[1],"YES")) 
				SETFLAG(GROUP_FLAG_PRIVATE);
			else
			if (strcasecmp(sl.word[1],"NO")) goto ERROR;
			break;
		

			case OPT_FIXED:
			if (!strcasecmp(sl.word[1],"YES")) 
				SETFLAG(GROUP_FLAG_FIXED);
			else
			if (strcasecmp(sl.word[1],"NO")) goto ERROR;
			break;


			case OPT_BAN:
			if (sl.wcnt < 3 ||
			    (ban_level = get_level(sl.word[1])) == -1 ||
			    !(uid = idstr_to_id(sl.word[2])))
				goto ERROR;

			switch(sl.wcnt) {
				case 3:
				ban(ban_level,uid,0,0);
				break;
			
				case 5:
				ban(ban_level,uid,
				    (uint32_t)inet_addr(sl.word[3]),
				    htons((uint16_t)atoi(sl.word[4])));
				break;
	
				default: goto ERROR;
				}
			break;


			default:
			if (own) own->warnprintf("Unknown option '%s' on group config file line %d.\n",sl.word[0],linenum);
			else {
				if (booting)
					printf("   WARNING: Unknown option '%s' on config file line %d.\n",sl.word[0],linenum);
				else
					log(1,"WARNING: Unknown option '%s' for group %04X on config file line %d.\n",sl.word[0],id,linenum);
				}
			}
		}

	fgets(line,ARR_SIZE-1,fp);
	++linenum;
	continue;

	ERROR:
	if (own) own->warnprintf("Invalid configuration for option '%s' on group config file line %d.\n",config_option[i],linenum);
	else {
		if (booting) 
			printf("   WARNING: Invalid configuration for option '%s' on config file line %d.\n",config_option[i],linenum);
		else
			log(1,"WARNING: Invalid configuration for option '%s' for group %04X on config file line %d.\n",config_option[i],id,linenum);
		}
	fgets(line,ARR_SIZE-1,fp);
	++linenum;
	}
fclose(fp);

grouplog(false,"~BG******************** GROUP LOADED/CREATED *******************\n");

if (err) log(1,"ERROR: Read failure while reading config file: %s\n",strerror(err));

if (!name) error = ERR_NAME_NOT_SET;
}




/*** Constructor for hard coded and user groups if user group config file
     doesn't exist for some reason and for public groups created by the
     "group create" command. ***/
cl_group::cl_group(uint16_t gid, char *nme, int typ, cl_local_user *own)
{
cl_group *grp;
char path[MAXPATHLEN];

name = NULL;
glogfile[0] = 0;
set_name(nme);
init(gid,typ,own);

// Errors checked after init since init sets up stuff that destructor
// will undo so it must be run regardless...
if ((grp = get_group(gid)) && grp != this) {
	error = ERR_DUPLICATE_ID;  return;
	}
switch(type) {
	case GROUP_TYPE_SYSTEM:
	if (id >= MIN_LOCAL_USER_ID) error = ERR_INVALID_ID; 
	return;

	case GROUP_TYPE_PUBLIC:
	if (id >= MIN_LOCAL_USER_ID) {
		error = ERR_INVALID_ID;  return;
		}

	// Create directory. Don't check result since any errors will become 
	// apparent on save.
	sprintf(path,"%s/%04X",PUB_GROUP_DIR,gid);
	mkdir(path,0700);
	error = save();
	sprintf(glogfile,"%s/%04X/%s",PUB_GROUP_DIR,gid,PUB_GROUP_LOG_FILE);
	break;

	case GROUP_TYPE_USER:
	if (id < MIN_LOCAL_USER_ID) error = ERR_INVALID_ID;
	}
grouplog(false,"~BG*********************** GROUP CREATED ***********************\n");
}




/*** Initialise everything except the name ***/
void cl_group::init(uint16_t gid, int typ, cl_local_user *own)
{
int i;

id = gid;
type = typ;
error = OK;
ucnt = 0;
flags = 0;
owner = own;
revpos = 0;
first_ban = NULL;
last_ban = NULL;

board  = ((id != GONE_REMOTE_GROUP_ID) ? new cl_board(this) : NULL);

revbuff = new revline[num_review_lines];
for(i=0;i < num_review_lines;++i) {
	revbuff[i].line = NULL;
	revbuff[i].alloc = 0;
	}

switch(type) {
	case GROUP_TYPE_SYSTEM: system_group_count++;  break;
	case GROUP_TYPE_PUBLIC: public_group_count++;  break;
	case GROUP_TYPE_USER  : user_group_count++;
	}

add_list_item(first_group,last_group,this);
}




/*** Destructor ***/
cl_group::~cl_group()
{
cl_user *u;
st_user_ban *gb,*gbn;
int i;

// Return all users to their own group and if they were using the board
// reader terminate it etc
FOR_ALL_USERS(u) {
	/* If user is being prompted about deleting this group then reset.
	   User who is actually deleting group will have had this set to NULL
	   already */
	if (u->del_group == this) u->reset_to_cmd_stage();

	// If user is in group then bung him back to his home group unless it
	// is his home group in which case hes logging off so dont bother
	if (u->group == this && this != u->home_group) {
		u->uprintf("\n~FYYour current group has been deleted. Returning to your home group...\n");
		u->home_group->join(u);
		u->prompt();
		}

	// Delete any invites for this group
	for(i=0;i < MAX_INVITES;++i) {
		if (u->invite[i].grp == this) {
			u->infoprintf("Your invite into group %04X (%s~RS) has been revoked because the group has been deleted.\n",id,name);
			u->invite[i].grp = NULL;
			break;
			}
		}

	// If user is paging info on this group then reset pointer
	if (u->com_page_ptr == (void *)this) u->com_page_ptr = NULL;

	// If user was monitoring this group then reset
	if (u->mon_group == this) {
		u->infoprintf("You can no longer monitor group ~FT%04X~RS (%s~RS)\n",id,name);

		u->mon_group = NULL;
		}
	if (u->prev_group == this) u->prev_group = NULL;
	}

FREE(name);

// Free review lines & delete board 
for(i=0;i < num_review_lines;++i) FREE(revbuff[i].line);
delete revbuff;
if (board) delete board;

// Delete bans
for(gb = first_ban;gb;gb = gbn) {
	gbn = gb->next;
	delete gb;
	}

// Write messge to log
grouplog(false,"~BY******************* GROUP UNLOADED/DELETED ******************\n");

switch(type) {
	case GROUP_TYPE_SYSTEM: system_group_count--;  break;
	case GROUP_TYPE_PUBLIC: public_group_count--;  break;
	case GROUP_TYPE_USER  : user_group_count--;
	}

remove_list_item(first_group,last_group,this);
}



//////////////////// METHODS ///////////////////////

/*** Set the name ***/
int cl_group::set_name(char *nme)
{
char *tmp;

if (!nme) nme = "";
if (!(tmp = strdup(nme))) return ERR_MALLOC;
if ((int)strlen(tmp) > max_group_name_len) tmp[max_group_name_len] = '\0';
FREE(name);
name = tmp;

return OK;
}




/*** Set group to private ***/
int cl_group::set_private()
{
cl_user *u;

if (type == GROUP_TYPE_SYSTEM) return ERR_SYSTEM_GROUP;
if (FLAGISSET(GROUP_FLAG_PRIVATE)) return ERR_GROUP_ACCESS_SAME;
if (FLAGISSET(GROUP_FLAG_FIXED)) return ERR_GROUP_FIXED;
SETFLAG(GROUP_FLAG_PRIVATE);

// If anyone monitoring this group then switch it off. Can't be arsed to
// check if they would still have permission or not, let em just reset it.
FOR_ALL_USERS(u) {
	if (u->mon_group == this) {
		u->infoprintf("Group ~FT%04X~RS (%s~RS) has been set to private so you are no longer monitoring the group.\n",id,name);
		u->mon_group = NULL;
		}
	}

// Only user groups can be loaded as private if they're not fixed
return (type != GROUP_TYPE_SYSTEM) ? save() : OK;
}




/*** Set group to public ***/
int cl_group::set_public()
{
cl_user *u;
int i;

if (type == GROUP_TYPE_SYSTEM) return ERR_SYSTEM_GROUP;
if (!FLAGISSET(GROUP_FLAG_PRIVATE)) return ERR_GROUP_ACCESS_SAME;
if (FLAGISSET(GROUP_FLAG_FIXED)) return ERR_GROUP_FIXED;
UNSETFLAG(GROUP_FLAG_PRIVATE);

// Revoke any invites
FOR_ALL_USERS(u) {
	if (u->level != USER_LEVEL_LOGIN) {
		for(i=0;i < MAX_INVITES;++i) {
			if (u->invite[i].grp == this) {
				u->infoprintf("Your invite to group %04X (%s~RS) has been revoked because the group has returned to public access.\n",id,name);
				u->invite[i].grp = NULL;
				break;
				}
			}
		}
	}
return (type != GROUP_TYPE_SYSTEM) ? save() : OK;
}




/*** Set group to fixed ***/
int cl_group::set_fixed()
{
if (type == GROUP_TYPE_SYSTEM) return ERR_SYSTEM_GROUP;
if (FLAGISSET(GROUP_FLAG_FIXED)) return ERR_GROUP_ACCESS_SAME;
SETFLAG(GROUP_FLAG_FIXED);
return (type != GROUP_TYPE_SYSTEM) ? save() : OK;
}




/*** Set group to unfixed ***/
int cl_group::set_unfixed()
{
if (type == GROUP_TYPE_SYSTEM) return ERR_SYSTEM_GROUP;
if (!FLAGISSET(GROUP_FLAG_FIXED)) return ERR_GROUP_ACCESS_SAME;
UNSETFLAG(GROUP_FLAG_FIXED);
return (type != GROUP_TYPE_SYSTEM) ? save() : OK;
}




/*** Save the groups config ***/
int cl_group::save()
{
char path[MAXPATHLEN];
char path2[MAXPATHLEN];
FILE *fp;
st_user_ban *gb;

switch(type) {
	case GROUP_TYPE_SYSTEM: return OK;

	case GROUP_TYPE_USER: 
	sprintf(path,"%s/%04X/groupconfig.tmp",USER_DIR,id);
	break;

	case GROUP_TYPE_PUBLIC:
	sprintf(path,"%s/%04X/config.tmp",PUB_GROUP_DIR,id);
	}


if (!(fp = fopen(path,"w"))) return ERR_CANT_OPEN_FILE;

fprintf(fp,"name = \"%s\"\n",name);
fprintf(fp,"private = %s\n",noyes[FLAGISSET(GROUP_FLAG_PRIVATE)]);
fprintf(fp,"fixed = %s\n",noyes[FLAGISSET(GROUP_FLAG_FIXED)]);

FOR_ALL_GROUP_BANS(gb) {
	if (gb->utype == USER_TYPE_LOCAL) 
		fprintf(fp,"ban = %s, %04X\n",user_level[gb->level],gb->uid);
	else fprintf(fp,"ban = %s, %04X, %s, %u\n",
		user_level[gb->level],
		gb->uid,
		inet_ntoa(gb->home_addr.sin_addr),
		ntohs(gb->home_addr.sin_port));
	}
fclose(fp);

if (type == GROUP_TYPE_USER)
	sprintf(path2,"%s/%04X/%s",USER_DIR,id,USER_GROUP_CONFIG_FILE);
else 
	sprintf(path2,"%s/%04X/%s",PUB_GROUP_DIR,id,PUB_GROUP_CONFIG_FILE);

if (rename(path,path2)) {
	unlink(path);  return ERR_CANT_RENAME_FILE;
	}
return OK;
}




/** Save the description to a file ***/
int cl_group::save_desc(char *desc)
{
char path[MAXPATHLEN];
FILE *fp;
int ret;

if (type == GROUP_TYPE_USER) 
	sprintf(path,"%s/%04X/%s",USER_DIR,id,USER_GROUP_DESC_FILE);
else 
	sprintf(path,"%s/%04X/%s",PUB_GROUP_DIR,id,PUB_GROUP_DESC_FILE);

if (!(fp = fopen(path,"w"))) return ERR_CANT_OPEN_FILE;	
ret=fputs(desc,fp);
fclose(fp);
return (ret == EOF ? ERR_WRITE : OK);
}




/*** User joins the group ***/
void cl_group::join(cl_user *u, cl_server *server, uint16_t remgid)
{
cl_server *svr;

// Leaves old group. Will be null if new remote user
if (u->group &&
    !O_FLAGISSET(u,USER_FLAG_LEFT) &&
    (u->group != gone_remote_group ||
    (u->group == gone_remote_group && u->server_to != server))) 
	u->group->leave(u);

u->prev_group = u->group;
u->group=this;
u->server_to = server;

if (this == gone_remote_group) 
	// Joining remote group
	u->uprintf("You join group %04X@%s\n",remgid,server->name);
else {
	// Joining this local group
	if (!O_FLAGISSET(u,USER_FLAG_INVISIBLE))
		geprintf(
			MSG_MISC,
			u,
			NULL,"~BTJOINED:~RS ~FT%04X~RS, %s %s\n",u->id,u->name,u->desc);

	// Don't look if we're a new login joining start group because thats
	// done by do_login_messages()
	if (!O_FLAGISSET(u,USER_FLAG_NEW_LOGIN)) u->look(u->group,0);
	}
ucnt++;

// Send out group change notification
FOR_ALL_SERVERS(svr) 
	if (svr->stage == SERVER_STAGE_CONNECTED) svr->send_group_change(u);
}




/*** User leaves a group. This just resets some stuff. ***/
void cl_group::leave(cl_user *u)
{
if (this == gone_remote_group) {
	u->server_to->send_leave(u->id);
	u->server_to = NULL;
	u->group = NULL;
	}
else 
if (!O_FLAGISSET(u,USER_FLAG_INVISIBLE))
	geprintf(MSG_MISC,u,NULL,"~BMLEFT:~RS ~FT%04X~RS, %s %s\n",
		u->id,u->name,u->desc);

// If doing anything on the board then quit as user could have been forced
// to leave group.
switch(u->stage) {
	case USER_STAGE_BOARD:
	case USER_STAGE_BOARD_DEL:
	case USER_STAGE_BOARD_SUBJECT:
	case USER_STAGE_BOARD_READ_FROM:
	u->uprintf("~NP\n\n~FYForcing exit of board reader.\n\n");
	FREE(u->msg_subject);
	u->page_pos = 0;
	u->stage = USER_STAGE_CMD_LINE;
	if (u->editor) delete u->editor;
	u->flags = u->prev_flags;
	u->prompt();
	}

// If no one left and group is private, return it to public.
if (!--ucnt && type == GROUP_TYPE_PUBLIC) set_public();
}




/*** Send text to all users in the group ***/
void cl_group::gprintf(int mtype, char *fmtstr,...)
{
char str[ARR_SIZE],str2[ARR_SIZE],*s;
va_list args;
cl_user *u;
int ret;

va_start(args,fmtstr);
vsnprintf(str,ARR_SIZE,fmtstr,args);
va_end(args);

FOR_ALL_USERS(u) { 
	if (u->group == this || u->mon_group == this) {
		if (u->mon_group == this) {
			sprintf(str2,"~OLMONITOR %04X: ~RS%s",id,str);
			s = str2;
			}
		else s = str;

		switch(mtype) {
			case MSG_SPEECH:
			if (!O_FLAGISSET(u,USER_FLAG_NO_SPEECH)) u->uprintf(s);
			break;

			case MSG_SHOUT:
			// This will never be used here but include
			// for the sake of completeness.
			if (!O_FLAGISSET(u,USER_FLAG_NO_SHOUTS)) u->uprintf(s);
			break;

			case MSG_INFO:
			u->infoprintf(s);
			break;

			case MSG_MISC:
			if (!O_FLAGISSET(u,USER_FLAG_NO_MISC)) u->uprintf(s);
			break;

			case MSG_SYSTEM:
			u->sysprintf(s);
			break;

			case MSG_BCAST:
			u->uprintf(s);
			}
		}
	}

grouplog(false,str);

if ((ret=add_review_line(revbuff,&revpos,str)) != OK) 
	log(1,"ERROR: cl_group::gprintf() -> add_revline_line(): %s",
		err_string[ret]);
}




/*** Send text to all users in group except those specified ***/
void cl_group::geprintf(int mtype, cl_user *u1, cl_user *u2, char *fmtstr, ...)
{
char str[ARR_SIZE],str2[ARR_SIZE],*s;
va_list args;
cl_user *u;
int ret;

va_start(args,fmtstr);
vsnprintf(str,ARR_SIZE,fmtstr,args);
va_end(args);

FOR_ALL_USERS(u) {
	if (u != u1 &&
	    u != u2 && u->group == this) {
		if (u->mon_group == this) {
			sprintf(str2,"~OLMONITOR %04X: ~RS%s",id,str);
			s = str2;
			}
		else s = str;

		switch(mtype) {
			case MSG_SPEECH:
			if (!O_FLAGISSET(u,USER_FLAG_NO_SPEECH)) u->uprintf(s);
			break;

			case MSG_SHOUT:
			// This will never be used here but include
			// for the sake of completeness.
			if (!O_FLAGISSET(u,USER_FLAG_NO_SHOUTS)) u->uprintf(s);
			break;

			case MSG_INFO:
			u->infoprintf(s);
			break;

			case MSG_MISC:
			if (!O_FLAGISSET(u,USER_FLAG_NO_MISC)) u->uprintf(s);
			break;

			case MSG_SYSTEM:
			u->sysprintf(s);
			break;

			case MSG_BCAST:
			u->uprintf(s);
			}
		}
	}

grouplog(false,str);

if ((ret=add_review_line(revbuff,&revpos,str)) != OK) 
	log(1,"ERROR: cl_group::gprintf() -> add_revline_line(): %s",
		err_string[ret]);
}




/*** User speaks ***/
int cl_group::speak(int comnum, cl_user *u, char *txt)
{
// Cheat a bit. Don't return an error here but print a message.
if (O_FLAGISSET(u,USER_FLAG_MUZZLED)) {
	if (u->muzzle_end_time) 
		u->uprintf("You cannot %s because you are muzzled for another %s.\n",
			command[comnum],
			time_period(u->muzzle_end_time - server_time));
	else 
		u->uprintf("You cannot %s because you are muzzled indefinately.\n",command[comnum]);
	return OK;
	}

if (this == gone_remote_group) return ERR_RESTRICTED_GROUP;
if (O_FLAGISSET(u,USER_FLAG_NO_SPEECH)) return ERR_NOSPEECH;

switch(comnum) {
	case COM_SAY:
	gprintf(MSG_SPEECH,"~FT%04X,%s~FG:~RS %s\n",u->id,u->name,txt);
	break;

	case COM_EMOTE:
	gprintf(MSG_SPEECH,"~FT%04X~FG:~RS %s %s\n",u->id,u->name,txt);
	break;

	case COM_THINK:
	gprintf(MSG_SPEECH,"~FT%04X~FG:~RS %s thinks . o O ( %s )\n",
		u->id,u->name,txt);
	}
return OK;
}




/*** Returns whether user can modify anything about the group ***/
int cl_group::user_can_modify(cl_user *u)
{
return (type != GROUP_TYPE_SYSTEM && 
        (this == u->home_group || u->level >= group_modify_level));
}




/*** Returns whether user can join the group. Return invite number in inv
     parameter if only let it because of this ***/
int cl_group::user_can_join(cl_user *u, int *inv)
{
int i;

*inv = -1;

if (this == u->home_group) return 1;

if (this == gone_remote_group || this == prison_group || user_is_banned(u))
	return 0;

if (!FLAGISSET(GROUP_FLAG_PRIVATE) || u->level >= group_gatecrash_level) 
	return 1;

// Group is private. See if user has an invite
for(i=0;i < MAX_INVITES;++i) {
	if (u->invite[i].grp == this) {
		*inv = i;  return 1;
		}
	}
return 0;
}




/*** Evict someone from the group ***/
void cl_group::evict(cl_user *evictor, cl_user *evictee)
{
evictor->uprintf("You evict user ~FT%04X~RS (%s).\n",evictee->id,evictee->name);

geprintf(
	MSG_INFO,evictor,evictee,
	"User ~FT%04X~RS (%s) is evicted from this group by ~FT%04X~RS (%s)\n",
	evictee->id,evictee->name,evictor->id,evictor->name);

evictee->uprintf(
	"\n~OL~FRYou have been evicted from this group by user ~FT%04X~RS (%s)!\n\n",
	evictor->id,evictor->name);

evictee->home_group->join(evictee);
evictee->prompt();
}




/*** Ban user using id or ip info. Port must be in network order. ***/
void cl_group::ban(int ban_level, uint16_t uid, uint32_t addr, uint16_t port)
{
st_user_ban *gb;

gb = new st_user_ban;
gb->level = ban_level;
gb->uid = uid; // For remote users this will be orig_id
bzero(&gb->home_addr,sizeof(sockaddr_in));

if (addr) { 
	gb->utype = USER_TYPE_REMOTE;
	gb->user = get_remote_user(uid,addr,port);
	gb->home_addr.sin_addr.s_addr = addr;
	gb->home_addr.sin_port = port;
	}
else {
	gb->utype = USER_TYPE_LOCAL;
	gb->user = get_user(uid,0);
	}

add_list_item(first_ban,last_ban,gb);
}




/*** Ban a user. This saves the info. ***/
int cl_group::ban(int ban_level, cl_user *u)
{
st_user_ban *gb;
int i;

if (type == GROUP_TYPE_SYSTEM) return ERR_SYSTEM_GROUP;
if (u->mon_group == this) u->mon_group = NULL;

// Remove invite to group if it exists and add to banned list
for(i=0;i < MAX_INVITES;++i) {
	if (u->invite[i].grp == this) {
		u->invite[i].grp = NULL;  break;
		}
	}

gb = new st_user_ban;
gb->level = ban_level;
gb->utype = u->type;
gb->user = u;
if (u->type == USER_TYPE_REMOTE) {
	gb->uid = ((cl_remote_user *)u)->orig_id;
	gb->home_addr = u->ip_addr;
	}
else gb->uid = u->id;

add_list_item(first_ban,last_ban,gb);
return save();
}




/*** Return whether user is banned. For logged on users. ***/
int cl_group::user_is_banned(cl_user *u)
{
st_user_ban *gb;

if (this == u->home_group || u->level >= group_gatecrash_level) return 0;

// Can't just check against the user pointer in the structure since the
// user may have left/logged off then returned.
if (u->type == USER_TYPE_LOCAL) {
	FOR_ALL_GROUP_BANS(gb) 
		if (gb->utype == USER_TYPE_LOCAL &&
		    u->id == gb->uid) return 1;
	return 0;
	}

FOR_ALL_GROUP_BANS(gb) {
	// Check original id matches as they may have come over multiple hops
	if (gb->utype == USER_TYPE_REMOTE &&
	    gb->uid == ((cl_remote_user *)u)->orig_id &&
	    gb->home_addr.sin_addr.s_addr == u->ip_addr.sin_addr.s_addr &&
	    gb->home_addr.sin_port == u->ip_addr.sin_port) return 1;
	}
return 0;
}




/*** Return whether user is banned. For remote users trying to connect ***/
int cl_group::user_is_banned(pkt_user_info *pkt)
{
st_user_ban *gb;

FOR_ALL_GROUP_BANS(gb) {
	if (gb->utype == USER_TYPE_REMOTE &&
	    gb->uid == pkt->orig_uid &&
	    gb->home_addr.sin_addr.s_addr == pkt->home_addr.ip4 &&
	    gb->home_addr.sin_port == pkt->home_port) return 1;
	}
return 0;
}




/*** Log some text. We can't error in this since we'd get an error in the
     main log every time someone spoke etc which would be a pain. The 
     file pointer is re-opened each time so that A) we use up less descriptors
     and B) so that the current file can be removed or renamed without 
     affecting subsequent writes. ***/
void cl_group::grouplog(bool force, char *str)
{
FILE *fp;
char logstr[ARR_SIZE];
char tstr[20];

if ((force || SYS_FLAGISSET(SYS_FLAG_LOG_GROUPS)) &&
    (fp = fopen(glogfile,"a"))) {
	strftime(tstr,sizeof(tstr),"%d/%m %H:%M:%S: ",&server_time_tms);
	snprintf(logstr,ARR_SIZE-20,"%s%s",tstr,str);
	fputs(logstr,fp);
	fclose(fp);
	}
}
