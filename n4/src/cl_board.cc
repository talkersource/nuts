/***************************************************************************
 FILE: cl_board.cc
 LVU : 1.3.9

 DESC:
 This contains the code to maintain and run the message boards belonging to
 the various groups. A lot of the code here is identical to cl_board.cc but
 there is enough difference to make code sharing difficult.
 
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


/*** Constructor ***/
cl_board::cl_board(cl_group *grp)
{
cl_msginfo *minfo;
cl_splitline *sl;
struct tm *tms;
FILE *fp;
char path[MAXPATHLEN];
char line[ARR_SIZE];
char *comlist[] = {
	"msg",
	"expire",
	"write level",
	"admin level" 
	};
enum {
	B_MSG,
	B_EXPIRE,
	B_WRITE_LEVEL,
	B_ADMIN_LEVEL,

	B_END
	};
int com,linenum,level,err;

group = grp;
msgcnt = 0;
todays_msgcnt = 0;
write_level = USER_LEVEL_USER;
admin_level = USER_LEVEL_OPERATOR;
expire_secs = 0;
first_msg = NULL;
last_msg = NULL;
err = 0;

// Create directory. It its already there this will error so it does no harm.
sprintf(path,"%s/%04X/",BOARD_DIR,group->id);
mkdir(path,0700);

// Open board info file. If its not there there are no messages.
strcat(path,BOARD_INFO_FILE);
if (!(fp = fopen(path,"r"))) return;

// Loop through info file. See cl_mail.cc for comments.
minfo = NULL;
linenum = 0;
fgets(line,ARR_SIZE-1,fp);

while(!feof(fp) && !(err = ferror(fp))) {
	linenum++; 
	if (!minfo) minfo = new cl_msginfo;
	sl = new cl_splitline(1);

	if (sl->parse(line) != OK) goto ERROR;
	if (sl->wcnt) {
		// Find info command
		for(com=0;com != B_END;++com) 
			if (!strcasecmp(sl->word[0],comlist[com])) break;
		if (com == B_END) goto ERROR;

		switch(com) {
			case B_MSG:
			if (sl->wcnt < 7) goto ERROR;
			minfo->set(msgcnt+1,sl);
			msgcnt++;
			tms = localtime(&minfo->create_time);
			todays_msgcnt += (tms->tm_year == server_time_tms.tm_year &&
			                  tms->tm_yday == server_time_tms.tm_yday);

			// Add to list
			add_list_item(first_msg,last_msg,minfo);
			minfo = NULL;

			// We don't want to delete splitline object as its
			// used for message storage.
			fgets(line,ARR_SIZE-1,fp);
			continue;


			case B_EXPIRE:
			if (sl->wcnt != 2 || !is_integer(sl->word[1]))
				goto ERROR;
			expire_secs = atoi(sl->word[1]);
			break;


			case B_WRITE_LEVEL:
			if (sl->wcnt != 2 || 
			    (level = get_level(sl->word[1])) == -1) goto ERROR;
			write_level = level;
			break;
		

			case B_ADMIN_LEVEL:
			if (sl->wcnt != 2 || 
			    (level = get_level(sl->word[1])) == -1) goto ERROR;
			admin_level = level;
			}
		}
	delete sl;
	fgets(line,ARR_SIZE-1,fp);
	continue;

	ERROR:
	if (sl->wcnt) {
		// Owner will be NULL if its a persistent group
		if (group->type == GROUP_TYPE_USER) {
			if (group->owner)
				group->owner->warnprintf("Invalid configuration on boardinfo file line %d.\n",linenum);
			log(1,"ERROR: Invalid configuration for user board %04X on info file line %d.\n",group->id,linenum);
			}
		else
		// Just print since we're booting if this is public group
		printf("   ERROR: Invalid configuration for board %04X on info file line %d.\n",group->id,linenum);
		fgets(line,ARR_SIZE-1,fp);
		}
	}
fclose(fp);
if (err) log(1,"ERROR: Read failure while loading board %04X configuration: %s\n",strerror(err));

if (minfo) delete minfo;

if (write_level > admin_level) {
	log(1,"ERROR: Write level for board %04X was greater than admin level, changing it.\n",group->id);
	write_level = admin_level;
	}
}




/*** Destructor ***/
cl_board::~cl_board()
{
cl_msginfo *minfo,*next;

save();

// Destroy all msginfo structures in list
for(minfo = first_msg;minfo;minfo = next) {
	next = minfo->next;
	delete minfo;
	}
}




/*** List the messages ***/
void cl_board::list(cl_user *u)
{
cl_msginfo *minfo;
struct tm *tms;
char sft[15];
int flag,lcnt,mcnt;

if (!msgcnt) {
	u->uprintf("There are no messages on the board.\n");  return;
	}
if (u->com_page_line == -1) 
	u->uprintf("\n~BB*** Message board of group %04X, %s~RS~BB ***\n\n",
		group->id,group->name);

lcnt = 0;
mcnt = 0;
flag = 0;
FOR_ALL_MSGS(minfo) {
	if (!flag && u->com_page_line == -1) {
		u->uprintf("~OL~ULMsg From                               Posted on    Bytes Subject\n");
		flag = 1;
		}

	++mcnt;
	if (u->com_page_line != -1 && mcnt <= u->com_page_line) continue;

	tms = localtime(&minfo->create_time);
	strftime(sft,sizeof(sft),"%b %d %H:%M",tms);
	sprintf(text,"%s, %s",minfo->id,minfo->name);

	// If id and name too long put some dots at end 
	if (strlen(text) > 34) {
		text[31]='.';  text[32]='.';  text[33]='.';  text[34]='\0';
		}

	u->uprintf("~FY%3d~RS ~FT%-34s~RS %s %5d %s\n",
		minfo->mnum,text,sft,minfo->size,minfo->subject);

	if (O_FLAGISSET(u,USER_FLAG_PAGING) &&
		++lcnt == u->term_rows - 2) {
		u->com_page_line = mcnt;  return;
		}
	}

if (O_FLAGISSET(u,USER_FLAG_PAGING) &&
    lcnt && lcnt >= u->term_rows - 4) {
	u->com_page_line = mcnt;  return;
	}

u->uprintf("\nTotal of %d messages.\n\n",msgcnt);
u->com_page_line = -1;
}




/*** Read the given message ***/
int cl_board::mread(cl_user *u, cl_msginfo *minfo)
{
struct stat ss;
char path[MAXPATHLEN];
int ret;

if (u->stage == USER_STAGE_BOARD_READ_FROM) {
	u->uprintf("\n~FYMesg num:~RS %d\n",minfo->mnum);
	u->page_header_lines = 5;
	}
else {
	u->uprintf("\n");
	u->page_header_lines = 4;
	}
u->uprintf("~FYFrom    :~RS %s, %s\n",minfo->id,minfo->name);
u->uprintf("~FYSubject :~RS %s\n",minfo->subject);
u->uprintf("~FYPosted  :~RS %s",ctime(&minfo->create_time));
u->uprintf("~FYBytes   :~RS %d",minfo->size);

// Check if expected and actual file sizes match
sprintf(path,"%s/%04X/%s",BOARD_DIR,group->id,minfo->filename);
ss.st_size = 0;
stat(path,&ss);
if (ss.st_size != minfo->size)
	u->uprintf(" (~OL~FRFile corrupted!~RS Actual file size = %d bytes)\n\n",ss.st_size);
else
	u->uprintf("\n\n");

ret = u->page_file(path,1);
if (!u->page_pos) u->uprintf("\n");
return ret;
}




/*** Write new message ***/
int cl_board::mwrite(cl_user *u, char *subj, char *msg)
{
cl_msginfo *minfo;
cl_remote_user *ru;
char filename[MAXPATHLEN];
char path[MAXPATHLEN];
char *subj2;
int cnt,nl,ret;
FILE *fp;

if (!user_can_post(u)) return ERR_WRITE;

if (O_FLAGISSET(u,USER_FLAG_MUZZLED)) return ERR_MUZZLED;

if ((int)strlen(msg) > max_board_chars) return ERR_MSG_TOO_LONG;

// Pick a filename based on message count
cnt = msgcnt+1;
do {
	sprintf(filename,"msg_%d",cnt);
	FOR_ALL_MSGS(minfo)
		if (!strcmp(minfo->filename,filename)) break;
	cnt++;
	} while(minfo);

// Write message file
sprintf(path,"%s/%04X/%s",BOARD_DIR,group->id,filename);
if (!(fp=fopen(path,"w"))) return ERR_CANT_OPEN_FILE;
fputs(msg,fp);
if (msg[strlen(msg)-1] != '\n') {
	nl=1;  fputc('\n',fp);
	}
else nl=0;
fclose(fp);

// Add entry to list.
if (u->type == USER_TYPE_REMOTE) {
	ru = (cl_remote_user *)u;
	// If remote user is on hop 1 then use their id and server name
	// else use id and server ip (since we won't have the name). 
	if (ru->hop_count == 1)
		sprintf(text,"%04X (%04X@%s)",
			u->id,ru->remote_id,ru->server_from->name);
	else {
		if (ru->home_svrname) 
			sprintf(text,"%04X (%04X@%s)",
				u->id,ru->orig_id,ru->home_svrname);
		else
			sprintf(text,"%04X (%04X@%s:%d)",
				u->id,
				ru->orig_id,
				u->ipnumstr,
				ntohs(u->ip_addr.sin_port));
		}
	}
else sprintf(text,"%04X",u->id);

subj2 = subj ? subj : (char *)"<No subject>";
minfo = new cl_msginfo();

if ((ret=minfo->set(
	last_msg ? last_msg->mnum+1 : 1,
	text,
	u->name,
	subj2,
	filename,
	strlen(msg) + nl)) != OK) {
	u->errprintf("Unable to set message!\n");
	delete minfo;
	return ret;
	}
msgcnt++;
todays_msgcnt++;

add_list_item(first_msg,last_msg,minfo);
if ((ret = save()) != OK) return ret;

// Inform everyone of success
u->uprintf("Message posted as ~OL~FM#%d\n",minfo->mnum);
group->geprintf(
	MSG_MISC,u,NULL,
	"~FM~OLBOARD POST #%d:~RS ~FYFrom:~RS ~FT%04X~RS (%s), ~FYSubject:~RS %s\n",
	minfo->mnum,u->id,u->name,subj2);
return OK;
}




/*** Delete a message ***/
int cl_board::mdelete(cl_msginfo *minfo)
{
struct tm *tms;
char path[MAXPATHLEN];

sprintf(path,"%s/%04X/%s",BOARD_DIR,group->id,minfo->filename);
unlink(path);
remove_list_item(first_msg,last_msg,minfo);

tms = localtime(&minfo->create_time);
todays_msgcnt -= (tms->tm_year == server_time_tms.tm_year &&
                  tms->tm_yday == server_time_tms.tm_yday);
msgcnt--;

delete minfo;
return save();
}




/*** Save message info to info file ***/
int cl_board::save()
{
char path[MAXPATHLEN];
cl_msginfo *minfo;
FILE *fp;

sprintf(path,"%s/%04X/%s",BOARD_DIR,group->id,BOARD_INFO_FILE);
if (!(fp=fopen(path,"w"))) return ERR_CANT_OPEN_FILE;

fprintf(fp,"\"admin level\" = %s\n",user_level[admin_level]);
fprintf(fp,"\"write level\" = %s\n",user_level[write_level]);
fprintf(fp,"expire = %d\n",expire_secs);

FOR_ALL_MSGS(minfo) {
	fprintf(fp,"msg = \"%s\", \"%s\", \"%s\", %s, %u, %u\n",
		minfo->id,
		minfo->name,
		minfo->subject,
		minfo->filename,
		(uint)minfo->create_time,
		minfo->size);
	}
fclose(fp);

return OK;
}




/*** Renumber the messages ***/
void cl_board::renumber()
{
cl_msginfo *minfo;
int i=1;

FOR_ALL_MSGS(minfo) minfo->mnum = i++;
}




/*** Set the admin or write level ***/
int cl_board::set_level(int levtype, int lev)
{
cl_user *u;

switch(levtype) {
	case ADMIN_LEVEL:
	admin_level = lev;
	if (lev < write_level) write_level = lev;
	break;

	case WRITE_LEVEL:
	write_level = lev;
	if (lev > admin_level) admin_level = lev;

	// Any users editing message for board who's level is now less
	// than write level must be stopped! ;)
	FOR_ALL_USERS(u) {
		if (u->group == group &&
		    u->level < lev &&
		    (u->stage == USER_STAGE_BOARD_SUBJECT ||
		     (u->stage == USER_STAGE_BOARD && u->editor))) {
			u->uprintf("~NP\n\n~FRYour message is being cancelled.\n\n");
			if (u->editor) delete u->editor;
			u->stage = USER_STAGE_BOARD;
			u->prompt();
			}
		}
	break;

	default:
	return ERR_INTERNAL;
	}
return save();
}




/*** Set the expiry time ***/
int cl_board::set_expire(int secs)
{
expire_secs = secs;
return save();
}




/*** Check if user can modify this board ***/
int cl_board::user_can_modify(cl_user *user)
{
return (user->level >= admin_level ||
         (group->type == GROUP_TYPE_USER && group == user->home_group));
}




/*** Check if user can post messages on this board ***/
int cl_board::user_can_post(cl_user *user)
{
return (user->level >= write_level ||
         (group->type == GROUP_TYPE_USER && group == user->home_group));
}
