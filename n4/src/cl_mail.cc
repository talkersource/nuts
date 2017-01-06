/***************************************************************************
 FILE: cl_mail.cc
 LVU : 1.3.9

 DESC:
 This code runs the mail object that every local user has while logged in
 which keeps a list of their mail and can write and delete mails also. A
 temporary instance of this class is also created when someone mails a user
 but they are not logged in at the time.
 
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
cl_mail::cl_mail(cl_user *own, uint16_t id)
{
cl_msginfo *minfo;
cl_splitline *sl;
char path[MAXPATHLEN];
char line[ARR_SIZE];
struct stat fs;
struct tm *tms;
FILE *fp;
int linenum,err;

owner = own;
uid = id;
error = OK;
msgcnt = 0;
todays_msgcnt = 0;
unread_msgcnt = 0;
first_msg = NULL;
last_msg = NULL;

// Check user exists first
sprintf(path,"%s/%04X/%s",USER_DIR,uid,USER_CONFIG_FILE);
if (stat(path,&fs)) {
	error = ERR_NO_SUCH_USER;  return;
	}

// Open mail file. If its not there its a new user who has no mail.
sprintf(path,"%s/%04X/%s",USER_DIR,uid,USER_MAILINFO_FILE);
if (!(fp=fopen(path,"r"))) return;

err = 0;
minfo = NULL;
linenum = 1;
fgets(line,ARR_SIZE-1,fp);

// Loop through file
while(!feof(fp) && !(err = ferror(fp))) {
	// Create new msginfo if we don't have one spare from last iteration
	if (!minfo) minfo = new cl_msginfo;
	sl = new cl_splitline(1);

	// Currently only parsing lines of the "msg = " format and if theres
	// an error on the line or its blank just ignore it.
	if (sl->parse(line) != OK ||
	    sl->wcnt < 8 ||
	    strcasecmp(sl->word[0],"msg")) {
		if (sl->wcnt && owner)
			owner->warnprintf("Invalid configuration on mailinfo file line %d.\n",linenum);
		delete sl;
		}
	else {
		minfo->set(msgcnt+1,sl);
		unread_msgcnt += !minfo->read;
		msgcnt++;

		/* Check if message was posted today. Since this means since
		   midnight, not just in last 24 hours can't do simple
		   subtraction on server_time */
		tms = localtime(&minfo->create_time);
		todays_msgcnt += (tms->tm_year == server_time_tms.tm_year &&
		                  tms->tm_yday == server_time_tms.tm_yday);
		add_list_item(first_msg,last_msg,minfo);
		minfo = NULL;
		}

	fgets(line,ARR_SIZE-1,fp);
	++linenum;
	}
fclose(fp);
if (err) {
	error = ERR_CONFIG;  return;
	}

// If last line errored we could have an unused minfo object left over.
if (minfo) delete minfo;
}




/*** Destructor ***/
cl_mail::~cl_mail()
{
cl_msginfo *minfo,*next;

save();

// Destroy all msginfo structures in list
for(minfo = first_msg;minfo;minfo = next) {
	next = minfo->next;
	delete minfo;
	}
}




/*** List all the users mails ***/
void cl_mail::list()
{
cl_msginfo *minfo;
struct tm *tms;
char sft[15],namestr[40];
char *str;
int flag,lcnt,mcnt;

if (!owner) return;

if (!msgcnt) {
	owner->uprintf("You have no mail.\n");  return;
	}
if (owner->com_page_line == -1) owner->uprintf("\n~BB*** Your mails ***\n\n");

lcnt = 0;
mcnt = 0;
flag = 0;
FOR_ALL_MSGS(minfo) {
	if (!flag && owner->com_page_line == -1) {
		owner->uprintf("  ~OL~ULMsg From                           Recvd        Bytes Subject\n");
		flag = 1;
		}
        ++mcnt;

	// If we're not at paging start point skip
        if (owner->com_page_line != -1 && mcnt <= owner->com_page_line)
		continue;

	// Print message info
	str = (char *)(minfo->read ? "   " : "~OL~FTU~RS  ");
	tms = localtime(&minfo->create_time);
	strftime(sft,sizeof(sft),"%b %d %H:%M",tms);
	sprintf(namestr,"%s, %s",minfo->id,minfo->name);
	if (strlen(namestr) > 30) {
		namestr[27]='.';
		namestr[28]='.';
		namestr[29]='.';
		namestr[30]='\0';
		}
	owner->uprintf("%s~FY%2d~RS ~FT%-30s~RS %s %5d %s\n",
		str,minfo->mnum,namestr,sft,minfo->size,minfo->subject);

	if (O_FLAGISSET(owner,USER_FLAG_PAGING) &&
	    ++lcnt == owner->term_rows - 2) {
		owner->com_page_line = mcnt;  return;
		}
	}
if (O_FLAGISSET(owner,USER_FLAG_PAGING) && 
    lcnt && lcnt >= owner->term_rows - 4) {
	owner->com_page_line = mcnt;  return;
	}

owner->uprintf("\nTotal of %d mails, %d are unread.\n\n",msgcnt,unread_msgcnt);
owner->com_page_line = -1;
}




/*** Read the given mail ***/
int cl_mail::mread(cl_msginfo *minfo)
{
char path[MAXPATHLEN];
struct stat ss;
int ret;

if (!owner) return OK;

if (owner->stage == USER_STAGE_MAILER_READ_FROM) {
	owner->uprintf("\n~FYMesg num:~RS %d\n",minfo->mnum);
	owner->page_header_lines = 5;
	}
else {
	owner->uprintf("\n");
	owner->page_header_lines = 4;
	}

owner->uprintf("~FYFrom    :~RS %s, %s\n",minfo->id,minfo->name);
owner->uprintf("~FYSubject :~RS %s\n",minfo->subject);
owner->uprintf("~FYReceived:~RS %s",ctime(&minfo->create_time));
owner->uprintf("~FYBytes   :~RS %d",minfo->size);

// Check if expected and actual file sizes match
sprintf(path,"%s/%04X/%s",USER_DIR,uid,minfo->filename);
ss.st_size = 0;
stat(path,&ss);
if (ss.st_size != minfo->size)
	owner->uprintf(" (~OL~FRFile corrupted!~RS Actual file size = %d bytes)\n\n",ss.st_size);
else
	owner->uprintf("\n\n");

if (!minfo->read) {
	minfo->read = 1;
	unread_msgcnt--;
	}

ret = owner->page_file(path,1);
if (!owner->page_pos) owner->uprintf("\n");
return ret;
}




/*** Write new mail ***/
int cl_mail::mwrite(
	uint16_t uid_from,
	char *name_from, cl_server *svr, char *subj, char *msg)
{
cl_msginfo *minfo;
char filename[MAXPATHLEN];
char path[MAXPATHLEN];
char fromstr[100];
int cnt,nl,ret;
FILE *fp;

if (!name_from || !msg) return ERR_MISSING_VALUE;
if ((int)strlen(msg) > max_mail_chars) return ERR_MSG_TOO_LONG;

// Pick a filename based on mail count. If it already exists pick the
// next one along.
cnt = msgcnt+1;
do {
	sprintf(filename,"mail_%d",cnt);
	FOR_ALL_MSGS(minfo) 
		if (!strcmp(minfo->filename,filename)) break;
	cnt++;
	} while(minfo);

// Write file
sprintf(path,"%s/%04X/%s",USER_DIR,uid,filename);
if (!(fp=fopen(path,"w"))) return ERR_CANT_OPEN_FILE;
fputs(msg,fp);
if (msg[strlen(msg)-1] != '\n') {
	nl=1;  fputc('\n',fp);
	}
else nl=0;
fclose(fp);

// Add entry to list. 
if (svr) sprintf(fromstr,"%04X@%s",uid_from,svr->name);
else sprintf(fromstr,"%04X",uid_from);

minfo = new cl_msginfo();
if ((ret=minfo->set(
	last_msg ? last_msg->mnum+1 : 1,
	fromstr,
	name_from,
	subj ? subj : (char *)"<No subject>",
	filename,
	strlen(msg) + nl)) != OK) {
	if (owner) owner->errprintf("Unable to set message!\n");
	delete minfo;
	return ret;
	}

msgcnt++;
todays_msgcnt++;
unread_msgcnt++;

add_list_item(first_msg,last_msg,minfo);

// Save to mailinfo file
return save();
}




/*** Delete an entry from the list ***/
int cl_mail::mdelete(cl_msginfo *minfo)
{
struct tm *tms;
char path[MAXPATHLEN];

sprintf(path,"%s/%04X/%s",USER_DIR,uid,minfo->filename);
unlink(path);
remove_list_item(first_msg,last_msg,minfo);

tms = localtime(&minfo->create_time);
todays_msgcnt -= (tms->tm_year == server_time_tms.tm_year &&
                  tms->tm_yday == server_time_tms.tm_yday);
if (!minfo->read) unread_msgcnt--;
msgcnt--;

delete minfo;
return save();
}




/*** Save the info to the mailinfo file ***/
int cl_mail::save()
{
char path[MAXPATHLEN];
cl_msginfo *minfo;
FILE *fp;

sprintf(path,"%s/%04X/%s",USER_DIR,uid,USER_MAILINFO_FILE);

// If no messages then just unlink
if (!first_msg) {
	unlink(path);  return OK;
	}

if (!(fp=fopen(path,"w"))) return ERR_CANT_OPEN_FILE;

FOR_ALL_MSGS(minfo) {
	fprintf(fp,"msg = %s, \"%s\", \"%s\", %s, %u, %u, %s\n",
		minfo->id,
		minfo->name,
		minfo->subject,
		minfo->filename,
		(uint)minfo->create_time,
		minfo->size,
		noyes[minfo->read]);
	}
fclose(fp);
return OK;
}




/*** Renumbers all the mail messages back to an ordered sequence from 1 ***/
void cl_mail::renumber()
{
cl_msginfo *minfo;
int i=1;

FOR_ALL_MSGS(minfo) minfo->mnum = i++;
}
