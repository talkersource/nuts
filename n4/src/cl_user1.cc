/***************************************************************************
 FILE: cl_user1.cc
 LVU : 1.4.1

 DESC:
 This is the main parent class for users and it does everything that is 
 applicable to local and remote users such as parsing user input. It does
 not contain the user command methods however, they have been hived off to
 cl_user2.cc simply for size reasons - its quicker to recompile when testing
 after adding a new command if the file size is smaller.

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
cl_user::cl_user(int is_temp_obj)
{
int i;

tbuff = text_buffer[0];
text_buffer[0][0] = '\0';
text_buffer[1][0] = '\0';
buffnum = 0;
flags = 0;
prev_flags = 0;
id = 0;
name_key = 0;
name = NULL;
desc = NULL;
ipnumstr = NULL;
ipnamestr = strdup(UNRESOLVED_STR);
ip_thrd = 0;
group = NULL;
home_group = NULL;
prev_group = NULL;
del_group = NULL;
look_group = NULL;
mon_group = NULL;
server_to = NULL;
ping_svr = NULL;
level = USER_LEVEL_LOGIN;
stage = USER_STAGE_NEW;
login_time = 0;
term_cols = DEFAULT_TERM_COLS;
term_rows = DEFAULT_TERM_ROWS;
page_pos = 0;
page_filename = NULL;
page_header_lines = 0;
last_com = 0;
com_page_line = -1;
com_page_ptr = NULL;
editor = NULL;
last_input_time = server_time;
input_len = 0;
msg_subject = NULL;
afk_msg = NULL;
inpwd = NULL;
new_pwd_user = NULL;
current_msg = 0;
to_msg = 0;
first_friend = NULL;
last_friend = NULL;
exa_user = NULL;
del_user = NULL;
shutdown_secs = 0;
imprison_level = USER_LEVEL_NOVICE;
imprisoned_time = 0;
release_time = 0;
muzzle_level = USER_LEVEL_NOVICE;
muzzle_start_time = 0;
muzzle_end_time = 0;
com_filename = NULL;

for(i=0;i < MAX_INVITES;++i) {
	invite[i].grp = NULL;
	invite[i].time = 0;
	}

revbuff = new revline[num_review_lines];
for(i=0;i < num_review_lines;++i) {
	revbuff[i].line = NULL;
	revbuff[i].alloc = 0;
	}
revpos = 0;
afk_tell_cnt = 0;
prev = NULL;
next = NULL;

if (!is_temp_obj) {
	com = new cl_splitline(0);
	add_list_item(first_user,last_user,this);
	}
}




/*** Destructor ***/
cl_user::~cl_user()
{
cl_user *u;
cl_friend *frnd;
cl_server *svr2;
cl_group *grp;
st_user_ban *gb;
int ret,i;

FREE(name);
free(desc);
FREE(ipnumstr);
free(ipnamestr);
FREE(page_filename);
FREE(msg_subject);
FREE(afk_msg);
FREE(inpwd);
FREE(com_filename);

// Do nothing else if temporary object
if (FLAGISSET(USER_FLAG_TEMP_OBJECT)) return;

group=NULL;

// Delete objects
delete com;
if (editor) delete editor;

// Delete review buffer
for(i=0;i < num_review_lines;++i) FREE(revbuff[i].line);
delete revbuff;

// Inform any people we're a friend of that we're leaving
if (level > USER_LEVEL_LOGIN) {
	FOR_ALL_USERS(u) {
		if (u == this) continue;

		// Clear other user if they are examining us or going to
		// delete us
		if (u->exa_user == this || u->del_user == this)
			u->reset_to_cmd_stage();

		FOR_ALL_USERS_FRIENDS(u,frnd) {
			if (frnd->id == id && 
			    frnd->stage == FRIEND_ONLINE &&
			    !frnd->svr_name) {
				u->infoprintf("Your friend ~FT%04X~RS has logged off.\n",id);
				frnd->stage = FRIEND_OFFLINE;
				frnd->local_user = NULL;
				}
			}
		}

	// Send logoff notify to remote servers and if user is due to be
	// informed about server connect remove them
	FOR_ALL_SERVERS(svr2) {
		if (svr2->stage == SERVER_STAGE_CONNECTED) {
			if ((ret=svr2->send_logoff_notify(id)) != OK)
				log(1,"ERROR: cl_user::~cl_user() -> cl_server::send_logoff_notify(): %s",err_string[ret]);
			}
		if (svr2->user == this) svr2->user = NULL;
		}

	// See if we're on any group banned lists and if so then null 
	// user pointer.
	FOR_ALL_GROUPS(grp) {
		for(gb=grp->first_ban;gb;gb=gb->next) {
			if (gb->user == this) {
				gb->user = NULL;  break;
				}
			}
		}
	}

remove_list_item(first_user,last_user,this);
}



//////////////////////////// THREAD FUNCTIONS ////////////////////////////////


/*** Thread method that gets the ip address of the user ***/
void cl_user::resolve_ip_thread()
{
hostent *host;
char *tmp;

// Assigning the ipnamestr pointer is an atomic operation from the point of
// view of this program so we don't need a mutex lock.
if ((host=gethostbyaddr((char *)&(ip_addr.sin_addr.s_addr),4,AF_INET))) {
	// Don't free ipnamestr before its reassigned just in case something
	// is reading it.
	tmp = ipnamestr;
	ipnamestr = strdup(host->h_name);
	log(1,"User address %s = %s",ipnumstr,ipnamestr);

	/* Sleep for a short time to make sure nothing is reading this
	   pointer any longer when we free it. Ok this isn't foolproof but
	   it saves me having to have a mutex lock every time ipnamestr is
	   read */
	sleep(HEARTBEAT*2);
	free(tmp);
	}
else log(1,"Address %s does not resolve.",ipnumstr);

exit_thread(&ip_thrd);
}




///////////////////////////// INPUT PROCESSING ////////////////////////////////


/*** We've got a line of text in the buffer so do something with it ***/
void cl_user::parse_line()
{
cl_local_user *lu;
int ret;

// For local user specific stuff
lu = (cl_local_user *)this;

// If user is PAGING A FILE just call page function
if (page_pos) {
	if ((ret=page_file(NULL,1)) != OK) 
		errprintf("Unable to page: %s\n",err_string[ret]);
	else 
	// Do stuff once finished paging
	if (!page_pos &&
	    type == USER_TYPE_LOCAL && FLAGISSET(USER_FLAG_NEW_LOGIN)) 
			lu->do_login_messages();

	prompt();
	return;
	}

// If user is PAGING A COMMAND call its function
if (com_page_line != -1) {
	if (toupper(tbuff[0]) == PAGE_QUIT_KEY) 
		com_page_line = -1;
	else {
		// Overwrite page prompt and call command function
		clear_inline_prompt();
		(this->*comfunc[last_com])();
		}
	prompt();
	return;
	}

// If afk call afk function
if (is_afk()) {
	if (run_afk()) {  prompt();  return;  }
	prompt();
	}

clean_string((char *)tbuff);
input_len = strlen((char *)tbuff);

// See if command is to be executed locally or remotely
if (tbuff[0] == '/') {
	if (!server_to) 
		infoprintf("You are already on your home server and do not need to use '/'\n");
	input_len--;
	memcpy(tbuff,tbuff+1,input_len);
	tbuff[input_len]='\0';
	}
else
if (server_to) {
	if ((ret=server_to->send_input(id,(char *)tbuff)) != OK) {
		errprintf("Unable to send input: %s\n",err_string[ret]);
		prompt();
		}
	return;
	}

/* Check if redo character. If it is then swap back to last text buffer.
   This can't be a command itself or we would get recursion. */
if (tbuff[0] == '=') {
	tbuff = text_buffer[!buffnum];
	// Swap buffnum so we overwrite = , not last proper input at next read
	buffnum = !buffnum;
	uprintf("~FTRepeating:~RS \"%s\"\n",tbuff);
	}

// Get words in the line
com->parse((char *)tbuff);

// If user is editing something just call editor
if (editor) {
	run_editor();  goto SWAP_BUFF;
	}

// Do different things depending in user stage. Only local users will ever be
// in mailer and suicide stages.
switch(stage) {
	case USER_STAGE_CMD_LINE:
	run_cmd_line();
	goto SWAP_BUFF;

	case USER_STAGE_MAILER:
	case USER_STAGE_MAILER_SUBJECT1:
	case USER_STAGE_MAILER_SUBJECT2:
	case USER_STAGE_MAILER_DEL:
	case USER_STAGE_MAILER_READ_FROM:
	if (type == USER_TYPE_REMOTE) goto INVALID_TYPE;
	lu->run_mailer();
	break;

	case USER_STAGE_BOARD:
	case USER_STAGE_BOARD_SUBJECT:
	case USER_STAGE_BOARD_DEL:
	case USER_STAGE_BOARD_READ_FROM:
	run_board_reader();
	break;

	case USER_STAGE_OLD_PWD:
	case USER_STAGE_NEW_PWD:
	case USER_STAGE_REENTER_PWD:
	run_new_password();
	break;

	case USER_STAGE_SUICIDE:
	if (type == USER_TYPE_REMOTE) goto INVALID_TYPE;
	lu->run_suicide();
	break;

	case USER_STAGE_EXAMINE:
	run_examine();
	break;

	case USER_STAGE_SHUTDOWN:
	run_shutdown();
	break;

	case USER_STAGE_REBOOT:
	run_reboot();
	break;

	case USER_STAGE_DELETE_GROUP:
	run_delete_group();
	break;

	case USER_STAGE_DELETE_GROUP_LOG:
	run_delete_group_log();
	break;

	case USER_STAGE_DELETE_USER:
	run_delete_user();
	break;

	case USER_STAGE_DELETE_BATCH_FILE:
	if (type == USER_TYPE_REMOTE) goto INVALID_TYPE;
	lu->run_delete_batch();
	break;

	case USER_STAGE_OVERWRITE_BATCH_FILE:
	if (type == USER_TYPE_REMOTE) goto INVALID_TYPE;
	lu->run_create_batch(1,NULL);
	break;

	case USER_STAGE_OVERWRITE_CONFIG_FILE:
	run_save_config(1);
	break;


	default:
	log(1,"INTERNAL ERROR: User %04X in invalid stage %d in cl_user::parse_line()!\n",id,stage);
	uprintf("~OL~FRINTERNAL ERROR:~RS Invalid stage in cl_user::parse_line()!\n");
	stage = USER_STAGE_CMD_LINE;
	}
prompt();

SWAP_BUFF:
buffnum = !buffnum;
tbuff = text_buffer[buffnum];
return;

INVALID_TYPE:
log(1,"INTERNAL ERROR: Remote user %04X in local user stage %d in cl_user::parse_line()!\n",id,stage);
uprintf("~OL~FRINTERNAL ERROR:~RS In local user stage in cl_user::parse_line()!\n");
stage = USER_STAGE_CMD_LINE;
}




/*** Check password if afk lock else just print out afk exit messages ***/
int cl_user::run_afk()
{
if (stage == USER_STAGE_AFK_LOCK) {
	if (type == USER_TYPE_REMOTE) {
		log(1,"INTERNAL ERROR: Remote user %04X was in USER_STAGE_AFK_LOCK!\n",id);
		stage = USER_STAGE_CMD_LINE;
		return 1;
		}
	if (!tbuff[0]) return 1;

	// Only local users should be in this stage
	if (strcmp(crypt_str((char *)tbuff),((cl_local_user *)this)->pwd)) {
		uprintf("\n~FRIncorrect password.\n\n");  return 1;
		}
	tbuff[0] = '\0';
	uprintf("\n~FGSession unlocked.\n\n");
	}
 uprintf("You return from being AFK.\n");

stage = USER_STAGE_CMD_LINE;
FREE(afk_msg);

if (afk_tell_cnt) {
	uprintf("You have ~OL~LI~FT%d~RS new tells/pemotes in your revtell buffer.\n",afk_tell_cnt);
	afk_tell_cnt = 0;
	}
group->geprintf(
	MSG_MISC,this,NULL,"~FG~OLRET:~RS ~FT%04X,~RS %s\n",id,name);
return 0;
}




/*** Page a file out to the user. The do_page parameter will be overridden
     by the users DO_PAGING flag if that is set off and do_page is set on. ***/
int cl_user::page_file(char *filename, int do_page)
{
FILE *fp=NULL;
int line_cnt,end_line,ret; 
int really_page;

really_page = (do_page && FLAGISSET(USER_FLAG_PAGING));

if (filename) {
	FREE(page_filename);
	if (!(page_filename = strdup(filename))) {
		ret=ERR_MALLOC;  goto END;
		}
	page_pos = 0;
	}
else 
// Set if user is quitting paging
if (really_page && toupper(tbuff[0]) == PAGE_QUIT_KEY) {
	ret=OK;  goto END;
	}

if (!page_filename || !(fp = fopen(page_filename,"r"))) {
	ret=ERR_CANT_OPEN_FILE;  goto END;
	}

if (page_pos) {
	clear_inline_prompt();

	// Go to position in file
	if (fseek(fp,page_pos,SEEK_SET) == -1) {
		ret=ERR_CANT_SEEK;  goto END;
		}
	}

/* Loop for the number of lines in the users terminal minus any header lines
   minus two, unless we're not pausing in which case do whole thing. Line
   counting might go a bit awry if the file has print codes in it since the
   printed line will be shorted that the read in one , can't be helped. */
end_line = term_rows - page_header_lines - 2;
if (end_line < 1) end_line = 1;
for(line_cnt=0;line_cnt < end_line || !really_page;++line_cnt) {
	if (!fgets(text,term_cols+1,fp)) {
		// EOF. Wrap things up.
		ret=OK;  goto END;
		}
	uprintf("%s",text);
	page_pos += strlen(text);
	}
fclose(fp);

// Header lines only set for initial page
page_header_lines = 0;
return OK;

END:
free(page_filename);
page_filename = NULL;
page_pos = 0;
page_header_lines = 0;
if (fp) fclose(fp);
return ret;
}




/*** Run the editor ***/
void cl_user::run_editor()
{
cl_local_user *lu;
int ret;
char *ftstr[] = { "Profile","Login batch","Logout batch","Session batch" };

if ((ret=editor->run()) != OK) {
	errprintf("Unable to edit: %s\n",err_string[ret]);
	prompt();
	}
else {
	lu = (cl_local_user *)this;
	switch(editor->stage) {
		case EDITOR_STAGE_COMPLETE:
		switch(editor->type) {
			case EDITOR_TYPE_PROFILE:
			case EDITOR_TYPE_LOGIN_BATCH:
			case EDITOR_TYPE_LOGOUT_BATCH:
			case EDITOR_TYPE_SESSION_BATCH:
			if (type == USER_TYPE_REMOTE) {
				log(1,"INTERNAL ERROR: Remote user %04X was in edit mode %d!\n",id,editor->type);
				break;
				}
			if ((ret=lu->save_text_file(
				editor->type,editor->buff)) != OK)
				errprintf("Unable to save file: %s\n",
					err_string[ret]);
			else uprintf("%s saved.\n",ftstr[editor->type]);
			FREE(com_filename);
			break;


			case EDITOR_TYPE_MAIL:
			if (type == USER_TYPE_REMOTE) {
				log(1,"INTERNAL ERROR: Remote user %04X was in edit mail mode!\n",id);
				break;
				}
			if ((ret = deliver_mail_from_local(
				this,
				lu->mail_to_id,
				lu->mail_to_svr,
				msg_subject,
				editor->buff)) != OK)
				errprintf("Unable to deliver mail: %s\n",
					err_string[ret]);
			else uprintf("Mail sent.\n");
			FREE(msg_subject);
			break;


			case EDITOR_TYPE_BOARD:
			if ((ret=group->board->mwrite(
				this,msg_subject,editor->buff)) != OK) 
				errprintf("Unable to post message: %s\n",
					err_string[ret]);
			FREE(msg_subject);
			break;


			case EDITOR_TYPE_GROUP_DESC:
			if ((ret = group->save_desc(editor->buff)) != OK) {
				errprintf("Unable to save group description: %s\n",err_string[ret]);
				break;
				}
			uprintf("Group description changed.\n");
			group->geprintf(MSG_INFO,this,NULL,"Group description has been changed by %04X, %s.\n",id,name);
			if (group->type != GROUP_TYPE_USER)
				log(1,"User %04X (%s) changed the description of group %04X (%s)\n",
					id,name,group->id,group->name);
			break;


			case EDITOR_TYPE_BCAST:
			send_broadcast(editor->buff);
			break;


			case EDITOR_TYPE_NET_BCAST:
			send_net_broadcast(editor->buff);
			break;


			default:
			log(1,"INTERNAL ERROR: Unknown editor type %d in cl_user::run_editor()!\n",editor->type);
			}
		break;


		case EDITOR_STAGE_CANCEL:
		FREE(msg_subject);
		break;

		default: return;
		}
	}
delete editor;
prompt();
}


#define NUM_SHORTCUTS 8


/*** Deal with normal command line input ***/
void cl_user::run_cmd_line()
{
int ret,dot,comnum,len,i;
char *w;

char *shortcut[NUM_SHORTCUTS] = {
	";",":",">","<",">>","<<","!","!!"
	};
int shortcut_com[NUM_SHORTCUTS] = {
	COM_EMOTE,
	COM_EMOTE,
	COM_TELL,
	COM_PEMOTE,
	COM_NTELL,
	COM_NEMOTE,
	COM_SHOUT,
	COM_SEMOTE
	};

// If nothing entered just return. Word[0] could be zero length if user
// entered "" on the command line
if (!com->wcnt || !strlen(com->word[0])) return;

// Check for dot command
if (com->word[0][0] == '.') {
	if (!com->word[0][1]) {
		uprintf("Missing command.\n");
		prompt();
		return;
		}
	dot=1;
	}
else dot=0;

// Check for shortcuts. 
comnum = -1;
for(i=0;i < NUM_SHORTCUTS;++i) {
	if (!strcmp(shortcut[i],com->word[0]+dot)) {
		comnum = shortcut_com[i];  break;
		}
	}

// If no shortcut then either do speech or search for command name
if (comnum == -1) {
	// If in converse mode and we don't have a dot in front just say the
	// text , else attempt to find command.
	if (!dot && FLAGISSET(USER_FLAG_CONVERSE)) {
		// Don't do prompt every time user speaks as it'll really
		// piss him off after a while.
		if ((ret=group->speak(COM_SAY,this,com->wordptr[0])) != OK) 
			errprintf("You cannot speak: %s\n",err_string[ret]);
		return;
		}

	// Find exact match for command first
	w = com->word[0]+dot;
	len = strlen(w);

	for(comnum=0;comnum < NUM_COMMANDS;++comnum) 
		if (!strcasecmp(w,command[comnum])) break;

	// If not found find first few letters match
	if (comnum == NUM_COMMANDS) {
		for(comnum=0;comnum < NUM_COMMANDS;++comnum) 
			if (!strncasecmp(w,command[comnum],len)) break;
		}

	// This is a safety measure so if theres lag they don't accidentaly
	// quit paging and quit the talker if they press "Q" too often.
	if (comnum == COM_QUIT && len != 4) {
		uprintf("To avoid accidental logouts you must type \"quit\" in full to exit the talker.\n");
		prompt();
		return;
		}
	}

// Call command func if its valid command and user is high enough level
if (comnum < NUM_COMMANDS && command_level[comnum] <= level) {
	(this->*comfunc[comnum])();
	last_com = comnum;
	}
else uprintf("Unknown command.\n");

// Prompt unless in converse mode and we just executed a SAY, EMOTE or
// SHOUT command
if (!(FLAGISSET(USER_FLAG_CONVERSE) &&
    (comnum == COM_SAY || comnum == COM_EMOTE || comnum == COM_SHOUT)))
	prompt();
}




/*** Run the board reading/writing enviroment. This function is far too long
     and should be broken up into parts , but I'm too lazy to bother right
     now. :) ***/
void cl_user::run_board_reader()
{
cl_msginfo *minfo,*mnext;
cl_board *board;
char *bcom[] = {
	"read",
	"write",
	"reply",
	"delete",
	"list",
	"info",
	"renumber",
	"alevel",
	"wlevel",
	"expire",
	"help",
	"quit"
	};
enum {
	BCOM_READ,
	BCOM_WRITE,
	BCOM_REPLY,
	BCOM_DELETE,
	BCOM_LIST,
	BCOM_INFO,
	BCOM_RENUM,
	BCOM_ALEVEL,
	BCOM_WLEVEL,
	BCOM_EXPIRE,
	BCOM_HELP,
	BCOM_QUIT,

	BCOM_END
	};
char repsubj[260]; // 255+5
char path[MAXPATHLEN];
char *str,*line,*btstr;
int comnum,ret,mn,lev,len,cnt;
int inc,i,pos1,pos2,col,secs;

board = group->board;

switch(stage) {
	case USER_STAGE_BOARD:  break;

	case USER_STAGE_BOARD_SUBJECT:
	if (!com->wcnt) {
		uprintf("\n<No subject>\n");
		msg_subject = NULL;
		}
	else {
		if ((int)strlen(com->wordptr[0]) > max_subject_len) {
			uprintf("\nSubject is too long, maximum is %d characters.\n",max_subject_len);
			return;
			}
		msg_subject = strdup(com->wordptr[0]); // Free'd in run_editor()
		}
	editor = new cl_editor(this,EDITOR_TYPE_BOARD,NULL);
	if (editor->error != OK) {
		errprintf("Editor failure: %s\n",err_string[editor->error]);
		delete editor;
		FREE(msg_subject);
		prompt();
		}
	stage = USER_STAGE_BOARD;
	return;


	case USER_STAGE_BOARD_DEL:
	if (!com->wcnt) return;
	switch(toupper(com->word[0][0])) {
		case 'N':
		stage = USER_STAGE_BOARD;  return;

		case 'Y':
		for(minfo=board->first_msg,cnt=0;minfo;minfo=mnext) {
			mnext = minfo->next;
			mn = minfo->mnum;
			if (current_msg && (mn < current_msg || mn > to_msg))
				continue;

			if ((ret=board->mdelete(minfo)) != OK) {
				errprintf("Error during deletion of message %d: %s\n",mn,err_string[ret]);
				stage = USER_STAGE_BOARD;
				return;
				}
			++cnt;
			}
		if (cnt) {
			if (!current_msg)
				uprintf("\n%d messages ~OL~FRDELETED.\n",cnt);
			else
			if (current_msg != to_msg)
				uprintf("\n%d messages (%d to %d) ~OL~FRDELETED.\n",cnt,current_msg,to_msg);
			else
			uprintf("\nMessage %d ~OL~FRDELETED.\n",current_msg);

			group->geprintf(MSG_INFO,this,NULL,"%d message(s) have been deleted from the message board by ~FT%04X~RS (%s).\n",cnt,id,name);
			log(1,"User %04X (%s) deleted %d message(s) from board %04X",
				id,name,cnt,group->id);
			}
		else uprintf("\nNo messages deleted.\n");

		stage = USER_STAGE_BOARD;
		}
	return;


	case USER_STAGE_BOARD_READ_FROM:
	clear_inline_prompt();

	// Check for quit
	if (com->wcnt && toupper(com->word[0][0]) == PAGE_QUIT_KEY) {
		stage = USER_STAGE_BOARD;  return;
		}

	// Get next message to read. If someone has deleted messages while
	// we've been reading we might not get a match here.
	current_msg++;
	for(minfo = board->first_msg;minfo;minfo=minfo->next)
		if (minfo->mnum >= current_msg) break;

	if (minfo) {
		uprintf("~FT------------------------------------------------------------------------------\n");
		if ((ret = board->mread(this,minfo)) != OK) 
			errprintf("Cannot display message: %s\n\n",
				err_string[ret]);

		// If last message then reset stage
		if (!minfo->next) stage = USER_STAGE_BOARD;
		}
	else stage = USER_STAGE_BOARD;
	return;


	default:
	log(1,"INTERNAL ERROR: User %04X in invalid stage %d in cl_user::run_board_reader()!\n",id,stage);
	uprintf("~OL~FRINTERNAL ERROR:~RS Invalid stage in cl_user::run_board_reader()!\n");
	stage = USER_STAGE_BOARD;
	return;
	}


// If paging message list check for quit
if (com_page_line != -1) {
	if (toupper(tbuff[0]) == PAGE_QUIT_KEY) {
		com_page_line = -1;  return;
		}
	clear_inline_prompt();
	board->list(this);
	return;
	}

if (!com->wcnt) return;

// USER_STAGE_BOARD
len = strlen(com->word[0]);
for(comnum=0;comnum < BCOM_END;++comnum)
	if (!strncasecmp(com->word[0],bcom[comnum],len)) break;
if (comnum == BCOM_END) {
	uprintf("Unknown board command.\n");  return;
	}

switch(comnum) {
	case BCOM_READ:
	switch(com->wcnt) {
		case 1:
		if (!board->first_msg) {
			uprintf("There are no messages on the board.\n");
			return;
			}
		minfo = board->first_msg;
		current_msg = minfo->mnum;
		stage = USER_STAGE_BOARD_READ_FROM;
		break;

		case 2:
		if (!strncasecmp(com->word[1],"from",strlen(com->word[1])))
			goto USAGE;
		if (!is_integer(com->word[1])) {
			uprintf("Invalid message number.\n");  return;
			}
		if (!(minfo = get_message(
			board->first_msg,atoi(com->word[1])))) {
			uprintf(NO_SUCH_MESSAGE);  return;
			}
		break;
					
		case 3:
		if (!is_integer(com->word[2])) {
			uprintf("Invalid message number.\n");  return;
			}
		if (!strncasecmp(com->word[1],"from",strlen(com->word[1]))) {
			current_msg = atoi(com->word[2]);
			if (!(minfo = get_message(
				board->first_msg,current_msg))) {
				uprintf("No such start message.\n");
				return;
				}
			stage = USER_STAGE_BOARD_READ_FROM;
			break;
			}
		// Fall through
				
		default:
		USAGE:
		uprintf("Usage: read [from] <msg number>\n");  return;
		}
	if ((ret = board->mread(this,minfo)) != OK)
		errprintf("Cannot display message: %s\n\n",err_string[ret]);
	return;
	

	case BCOM_WRITE:
	if (FLAGISSET(USER_FLAG_MUZZLED)) {
		uprintf("You cannot post a message while you are muzzled.\n");
		return;
		}
	if (!board->user_can_post(this)) {
		uprintf("You cannot post messages to this board.\n");
		return;
		}
	if (com->wcnt > 1) {
		if ((ret=board->mwrite(this,NULL,com->wordptr[1])) != OK) 
			errprintf("Unable to post message: %s\n",err_string[ret]);
		return;
		}
	stage = USER_STAGE_BOARD_SUBJECT;
	return;


	case BCOM_REPLY:
	if (FLAGISSET(USER_FLAG_MUZZLED)) {
		uprintf("You cannot post a message while you are muzzled.\n");
		return;
		}
	if (!board->user_can_post(this)) {
		uprintf("You cannot reply to messages on this board.\n");
		return;
		}
	switch(com->wcnt) {
		case 2: inc = 0;  break;
		case 3:
		if (!strncasecmp(com->word[2],"include",strlen(com->word[2]))) {			inc = 1;  break;
			}
		// Fall through

		default:
		uprintf("Usage: reply <msg number> [include]\n");  return;
		}
	if (!is_integer(com->word[1])) {
		uprintf("Invalid message number.\n");  return;
		}
	if (!(minfo = get_message(board->first_msg,atoi(com->word[1])))) {
		uprintf(NO_SUCH_MESSAGE);  return;
		}

	if (!strncmp(minfo->subject,"Re: ",4))
		strcpy(repsubj,minfo->subject);
	else sprintf(repsubj,"Re: %s",minfo->subject);

	uprintf("\n~FYReply to:~RS %s, %s  (#%d)\n",
		minfo->id,minfo->name,minfo->mnum);
	uprintf("~FYSubject :~RS %s\n",repsubj);

	if (inc) {
		sprintf(path,"%s/%04X/%s",BOARD_DIR,group->id,minfo->filename);
		editor = new cl_editor(this,EDITOR_TYPE_BOARD,path);
		}
	else editor = new cl_editor(this,EDITOR_TYPE_BOARD,NULL);

	if (editor->error != OK) {
		errprintf("Editor failure: %s\n",err_string[editor->error]);
		delete editor;
		}
	else msg_subject = strdup(repsubj);

	stage = USER_STAGE_BOARD;
	return;

	
	case BCOM_DELETE:
	if (!board->first_msg) {
		uprintf("There are no messages to delete.\n");  return;
		}
	if (!board->user_can_modify(this)) {
		uprintf("You cannot delete messages from this board.\n");
		return;
		}

	switch(com->wcnt) {
		case 2:
		if (!strcasecmp(com->word[1],"all")) {
			// Set to delete all. Comfirmation prompt done in
			// prompt()
			current_msg = 0;
			stage = USER_STAGE_BOARD_DEL;
			return;
			}
		break;

		case 4:
		if (strcasecmp(com->word[2],"to")) goto USAGE2;

		if (!is_integer(com->word[1]) || 
		    !(current_msg = atoi(com->word[1])) ||
		    !is_integer(com->word[3]) || 
		    !(to_msg=atoi(com->word[3]))) {
			uprintf("Invalid message number(s).\n");  return;
			}
		if (current_msg >= to_msg) {
			uprintf("The second message number must be greater than the first.\n");
			return;
			}
		if (!get_message(board->first_msg,current_msg) ||
		    !get_message(board->first_msg,to_msg)) {
			uprintf("No such message(s).\n");  return;
			}
		stage = USER_STAGE_BOARD_DEL;
		return;

		default:
		USAGE2:
		uprintf("Usage: delete <msg number> [to <msg number>]\n              all\n");
		return;
		}
	
	// Delete a single message. 
	if (!is_integer(com->word[1]) || !(current_msg = atoi(com->word[1]))) {
		uprintf("Invalid message number.\n");  return;
		}
	if (!(minfo = get_message(board->first_msg,current_msg))) {
		uprintf(NO_SUCH_MESSAGE);  return;
		}
	to_msg = current_msg;
	stage = USER_STAGE_BOARD_DEL;
	return;


	case BCOM_LIST:
	if (com->wcnt != 1) {
		uprintf(COM_NO_ARGS);  return;
		}
	board->list(this);
	return;


	case BCOM_INFO:
	if (com->wcnt != 1) {
		// A harmless little easter egg for users to find. Grep for 
		// eecnt in the code to see what has to be done to view it.
		if (eecnt == 2 && !strcmp(com->word[1],"egg")) {
			str = "HAPPY EASTER EGG!!";	
			len = strlen(str) + 3;
			line = (char *)malloc(term_cols);
			pos1 = 0;
			pos2 = term_cols - len - 1;

			for(i=0,col=0;pos2 >= 0;++i) {
				memset(line,' ',term_cols);
				do {
					col = (col+1) % NUM_PRINT_CODES;
				} while(printcode[col][0] != 'F' &&
				        printcode[col][0] != 'B' &&
				        printcode[col][0] != 'O' &&
				        printcode[col][0] != 'U' &&
				        printcode[col][0] != 'L' &&
				        strcmp(printcode[col],"RV"));

				sprintf(text,"~%s%s",printcode[col],str);
				memcpy(line+pos1,text,len);
				memcpy(line+pos2,text,len);
				if (pos1 > 2 && pos2 > 2)
					memcpy(line+(pos2 > pos1 ? pos2 : pos1)+len,"~RS",3);
				line[term_cols - 1] = '\0';
				uprintf("%s\n",line);
				pos1++;
				pos2--;
				}
			free(line);
			eecnt = 0;
			return;
			}
		uprintf(COM_NO_ARGS);  return;
		}
	uprintf("\nAdmin level     : %s\n",user_level[board->admin_level]);
	uprintf("Write level     : %s\n",user_level[board->write_level]);
	uprintf("Total msg count : %d\n",board->msgcnt);
	uprintf("Todays msg count: %d\n",board->todays_msgcnt);

	if (group->type == GROUP_TYPE_USER) 
		uprintf("Message lifetime: %s\n",
			board->expire_secs ? time_period(board->expire_secs) :
			                     "<indefinite>");
	else
		uprintf("Message lifetime: %s\n",
			board->expire_secs ? time_period(board->expire_secs) :
			                     "<global lifetime>");

	if (board->msgcnt) { 
		uprintf("Most recent post: ~FY#%d~RS from ~FT%s~RS, %s\n",
			board->last_msg->mnum,
			board->last_msg->id,
			board->last_msg->name);
		uprintf(" ... and subject: %s\n\n",board->last_msg->subject);
		}
	else uprintf("\n");
	return;


	case BCOM_RENUM:
	if (com->wcnt != 1) {
		uprintf(COM_NO_ARGS);  return;
		}
	if (!board->user_can_modify(this)) {
		uprintf("You cannot renumber this board.\n");  return;
		}
	if (!board->first_msg) uprintf("There are no messages to renumber.\n");
	else {
		board->renumber();
		uprintf("Message board renumbered.\n");
		group->geprintf(
			MSG_INFO,this,NULL,
			"The message board has been renumbered by ~FT%04X~RS (%s).\n",
			id,name);
		}
	return;


	case BCOM_ALEVEL:
	if (!board->user_can_modify(this)) {
		uprintf("You cannot set the admin level on this board.\n");
		return;
		}
	if (com->wcnt != 2) {
		uprintf("Usage: alevel <level>\n");  return;
		}
	if ((lev = get_level(com->word[1])) == -1) {
		uprintf("Invalid level.\n");  return;
		}
	if (lev == board->admin_level) {
		uprintf("The admin level is already set to that.\n");
		return;
		}
	if (lev > level) {
		uprintf("You cannot set the level higher than your own.\n");
		return;
		}
	if ((ret = board->set_level(cl_board::ADMIN_LEVEL,lev)) != OK) {
		errprintf("Failed to set board level: %s\n",err_string[ret]);
		return;
		}
	uprintf("Admin level changed to: ~OL~FM%s\n",user_level[lev]);
	group->geprintf(MSG_INFO,this,NULL,
		"Board admin level has been changed to ~OL~FM%s~RS by ~FT%04X~RS (%s).\n",
		user_level[lev],id,name);
	log(1,"User %04X (%s) changed admin level of board %04X to %s",
		id,name,group->id,user_level[lev]);
	return;


	case BCOM_WLEVEL:
	if (!board->user_can_modify(this)) {
		uprintf("You cannot set the write level on this board.\n");
		return;
		}
	if (com->wcnt != 2) {
		uprintf("Usage: wlevel <level>\n");  return;
		}
	if ((lev = get_level(com->word[1])) == -1) {
		uprintf("Invalid level.\n");  return;
		}
	if (lev == board->write_level) {
		uprintf("The write level is already set to that.\n");
		return;
		}
	if (lev > level) {
		uprintf("You cannot set the level higher than your own.\n");
		return;
		}
	if ((ret = board->set_level(cl_board::WRITE_LEVEL,lev)) != OK) {
		errprintf("Failed to set board level: %s\n",err_string[ret]);
		return;
		}
	uprintf("Write level changed to: ~OL~FM%s\n",user_level[lev]);
	group->geprintf(MSG_INFO,this,NULL,
		"Board write level has been changed to ~OL~FM%s~RS by ~FT%04X~RS (%s).\n",
		user_level[lev],id,name);
	log(1,"User %04X (%s) changed write level of board %04X to %s",
		id,name,group->id,user_level[lev]);
	return;


	case BCOM_EXPIRE:
	switch(com->wcnt) {
		case 2:
		if (!strncasecmp(
			com->word[1],"indefinite",strlen(com->word[1])))
			secs = 0;
		else 
		if ((secs = get_seconds(com->word[1],NULL)) < 0) goto USAGE3;
		break;

		case 3:
	    	if ((secs = get_seconds(com->word[1],com->word[2])) < 0)
			goto USAGE3;
		break;
	
		default:
		USAGE3:
		uprintf("Usage: expire <message lifetime> [seconds/minutes/hours/days]\n              indefinite\n");
		return;
		}
	if (!board->user_can_modify(this)) {
		uprintf("You cannot set the message lifetime on this board.\n");
		return;
		}
	if (secs == board->expire_secs) {
		uprintf("The message lifetime is already set to that.\n");
		return;
		}
	if ((ret = board->set_expire(secs)) != OK) {
		errprintf("Failed to set message lifetime: %s\n",err_string[ret]);
		return;
		}
	if (secs) {
		uprintf("Message lifetime changed to: %s\n",
			time_period(secs));
		group->geprintf(MSG_INFO,this,NULL,
			"Board message lifetime has been changed to %s by ~FT%04X~RS (%s).\n",
			time_period(secs),id,name);
		log(1,"User %04X (%s) changed message lifetime of board %04X to %s",
			id,name,group->id,time_period(secs));
		}
	else {
		btstr = (char *)(group->type == GROUP_TYPE_USER ?
				"indefinite" : "global lifetime");

		uprintf("Message lifetime set to %s.\n",btstr);
		group->geprintf(MSG_INFO,this,NULL,
			"Board message lifetime has been changed to %s by ~FT%04X~RS (%s).\n",
			btstr,id,name);
		log(1,"User %04X (%s) changed message lifetime of board %04X to %s",
			id,name,group->id,btstr);
		}
	return;


	case BCOM_HELP:
	if (com->wcnt != 1) {
		uprintf(COM_NO_ARGS);  return;
		}
	uprintf("\n~FYBoard commands:\n");
	uprintf("    read   [[from]      <msg number>]\n");
	uprintf("    write  [<text>]\n");
	uprintf("    reply  <msg number> [include]\n");
	uprintf("    delete <msg number> [to <msg number>]\n");
	uprintf("           all\n");
	uprintf("    list\n");
	uprintf("    info\n");
	uprintf("    renumber\n");
	uprintf("    alevel <level>\n");
	uprintf("    wlevel <level>\n");
	uprintf("    expire <message lifetime> [seconds/minutes/hours/days]\n");
	uprintf("           indefinite\n");
	uprintf("    help\n");
	uprintf("    quit\n\n");
	return;
	

	case BCOM_QUIT:
	if (com->wcnt != 1) {
		uprintf(COM_NO_ARGS);  return;
		}
	uprintf("\n~FYExiting board reader.\n\n");

	// Reset stage and flags
	stage = USER_STAGE_CMD_LINE;
	flags = prev_flags;

	group->geprintf(MSG_MISC,this,NULL,
		"User ~FT%04X~RS (%s) has finished using the board reader.\n",
		id,name);
	}
}




/*** Controls entering a new password. This is not specific to cl_local_user
     since a remote user could have been promoted to admin and be changing
     another users password. ***/
void cl_user::run_new_password()
{
int ret;
cl_local_user *u;

if (!com->wcnt) uprintf("\n~FRPassword change abandoned.\n\n");
else 
switch(stage) {
	case USER_STAGE_OLD_PWD:
	if (strcmp(crypt_str(com->wordptr[0]),new_pwd_user->pwd)) {
		uprintf("\n~FRIncorrect password.\n\n");  break;
		}
	stage = USER_STAGE_NEW_PWD;
	return;
	

	case USER_STAGE_NEW_PWD:
	if ((int)strlen(com->wordptr[0]) < min_pwd_len) {
		uprintf("\n~FYPassword is too short, minimum is %d characters.\n\n",min_pwd_len);
		return;
		}
	if (new_pwd_user->pwd && 
	    !strcmp(crypt_str(com->wordptr[0]),new_pwd_user->pwd)) {
		uprintf("\n~FYNew password is the same as the old one.\n\n");
		return;
		}
	FREE(inpwd);
	inpwd = strdup(com->wordptr[0]);
	stage = USER_STAGE_REENTER_PWD;
	return;


	case USER_STAGE_REENTER_PWD:
	if (strcmp(com->wordptr[0],inpwd)) {
		uprintf("\n~FYPasswords do not match.\n\n");  break;
		}

	/* See if user is logged on. If they are then let them know their
	   password has been changed. If they're not logged on then theres no
	   point mailing them about password change since obviously if they
	   don't know the new password they can't logon to read the mail! */
	if (O_FLAGISSET(new_pwd_user,USER_FLAG_TEMP_OBJECT) &&
	    (u = (cl_local_user *)get_user(new_pwd_user->id))) {
		u->warnprintf("Adminstrator %04X (%s) has changed your password to: \"%s\"\n",id,name,inpwd);

		// Set password and use their object to save() since they may
		// have changed other things while we changed their password
		FREE(u->pwd);
		u->pwd = strdup(crypt_str(inpwd));
		ret = u->save();
		}
	else {
		// Save using given object
		FREE(new_pwd_user->pwd);
		new_pwd_user->pwd = strdup(crypt_str(inpwd));
		ret = new_pwd_user->save();
		}

	if (ret != OK) 
		errprintf("Unable to save changes: %s\n",err_string[ret]);
	else {
		uprintf("\n~FYPassword changed.\n\n");
		if (new_pwd_user != this)
			log(1,"User %04X (%s) changed the password of user %04X (%s)\n",id,name,new_pwd_user->id,new_pwd_user->name);
		}
	}

FREE(inpwd);
if (O_FLAGISSET(new_pwd_user,USER_FLAG_TEMP_OBJECT)) delete new_pwd_user;
new_pwd_user = NULL;
stage = USER_STAGE_CMD_LINE;
}




/*** Examine is split between paging the profile and showing the details.
     This bit shows the details after the profile has been paged ***/
void cl_user::run_examine()
{
clear_inline_prompt();

if (!exa_user || (com->wcnt && toupper(com->word[0][0]) == PAGE_QUIT_KEY))
	uprintf("\n~FRExamine aborted.\n\n");
else {
	uprintf("\n");
	show_details(exa_user);
	}

if (exa_user && O_FLAGISSET(exa_user,USER_FLAG_TEMP_OBJECT)) delete exa_user;
stage = USER_STAGE_CMD_LINE;
exa_user = NULL;
}




/*** Shutdown stage for user ***/
void cl_user::run_shutdown()
{
if (!com->wcnt) return;

switch(toupper(com->word[0][0])) {
	case 'Y':
	uprintf("\n");

	shutdown_time = server_time + shutdown_secs;

	if (server_time < shutdown_time)
		allprintf(
			MSG_SYSTEM,USER_LEVEL_NOVICE,NULL,
			"Shutdown sequence ~OL~FRSTARTED~RS. Shutdown in: %s\n",
				time_period(shutdown_secs));
	else
	allprintf(
		MSG_SYSTEM,USER_LEVEL_NOVICE,NULL,
		"Shutdown sequence ~OL~FRSTARTED~RS. Shutting down immediately!\n");

	log(1,"User %04X (%s) STARTED the shutdown sequence for %s time.\n",
		id,name,time_period(shutdown_secs));

	shutdown_type = SHUTDOWN_STOP;
	break;

	case 'N':
	uprintf("\n~FGShutdown cancelled.\n\n");
	break;

	default: return;
	}
stage = USER_STAGE_CMD_LINE;
}




/*** Reboot stage. This is a duplicate of run_shutdown() except for the
     "reboot" string in it. Having one function do both would have been
     messy so lets waste some disk space instead! :) ***/
void cl_user::run_reboot()
{
if (!com->wcnt) return;

switch(toupper(com->word[0][0])) {
	case 'Y':
	uprintf("\n");

	shutdown_time = server_time + shutdown_secs;

	if (server_time < shutdown_time)
		allprintf(
			MSG_SYSTEM,USER_LEVEL_NOVICE,NULL,
			"Reboot sequence ~OL~FRSTARTED~RS. Reboot in: %s\n",
				time_period(shutdown_secs));
	else
	allprintf(
		MSG_SYSTEM,USER_LEVEL_NOVICE,NULL,
		"Reboot sequence ~OL~FRSTARTED~RS. Rebooting down immediately!\n");

	log(1,"User %04X (%s) STARTED the reboot sequence for %s time.\n",
		id,name,time_period(shutdown_secs));

	shutdown_type = SHUTDOWN_REBOOT;
	break;

	case 'N':
	uprintf("\n~FGReboot cancelled.\n\n");
	break;

	default: return;
	}
stage = USER_STAGE_CMD_LINE;
}




/*** Delete a group ... or not, the choice is yours! Oh the power trip! ***/
void cl_user::run_delete_group()
{
cl_group *tmp;
struct stat fs;
char old_dir[100],new_dir[100];
int i;

if (!com->wcnt) return;

switch(toupper(com->word[0][0])) {
	case 'Y':
	// Delete group object then rename directory to have .del%d on the end.
	sprintf(old_dir,"%s/%04X",PUB_GROUP_DIR,del_group->id);

	allprintf(
		MSG_INFO,USER_LEVEL_NOVICE,this,
		"Group ~FT%04X~RS (%s~RS) has been ~OL~FRDELETED.\n",
		del_group->id,del_group->name);

	log(1,"User %04X (%s) DELETED group %04X (%s).\n",
		id,name,del_group->id,del_group->name);

	// del_group set to NULL else group destructor will call 
	// reset_to_cmd_stage()
	tmp = del_group;
	del_group = NULL;
	delete tmp;

	// Find .del dir that doesn't exist
	for(i=1;;++i) {
		sprintf(new_dir,"%s.del%d",old_dir,i);
		if (stat(new_dir,&fs)) break;
		}
	if (rename(old_dir,new_dir)) {
		log(1,"ERROR: com_group(): Failed to rename group dir %s to %s!\n",old_dir,new_dir);
		errprintf("Group directory rename failed!\n");
		}
	else uprintf("\nGroup ~OL~FRDELETED.\n");

	send_server_info_to_servers(NULL);
	break;

	case 'N':
	uprintf("\n~FGGroup deletion abandoned.\n\n");
	del_group = NULL;
	break;

	default: return;
	}
stage = USER_STAGE_CMD_LINE;
}




/*** Delete a group log ***/
void cl_user::run_delete_group_log()
{
switch(toupper(com->word[0][0])) {
	case 'Y':
	if (unlink(del_group->glogfile) == -1) {
		log(1,"ERROR: com_group(): Failed to delete group log file: %s\n",strerror(errno));
		errprintf("Unable to delete log file: %s\n",strerror(errno));
		break;
		}
	log(1,"User %04X (%s) DELETED group %04X (%s) logfile.\n",id,name,del_group->id,del_group->name);
	uprintf("Group logfile ~OL~FRDELETED.\n");
	break;

	case 'N':
	uprintf("\n~FGGroup log file deletion abandoned.\n\n");
	}
del_group = NULL;
stage = USER_STAGE_CMD_LINE;
}




/*** Delete another users account ***/
void cl_user::run_delete_user()
{
int ret;

if (!com->wcnt) return;

switch(toupper(com->word[0][0])) {
	case 'Y':
	log(1,"User %04X (%s) DELETED user %04X (%s).",
		id,name,del_user->id,del_user->name);

	if (O_FLAGISSET(del_user,USER_FLAG_TEMP_OBJECT)) {
	        if ((ret = delete_account(del_user->id)) != OK) {
                	errprintf("Delete may have failed: %s\n",err_string[ret]);
			goto DELTEMP;
			}
		}
	del_user->uprintf("\n\n~OL~BR*** YOUR ACCOUNT IS BEING DELETED! ***\n\n");
	O_SETFLAG(del_user,USER_FLAG_DELETE);
	((cl_local_user *)del_user)->disconnect();
	uprintf("User ~OL~FRDELETED.\n");
	del_user = NULL;
	stage = USER_STAGE_CMD_LINE;
	return;

	case 'N':
	uprintf("\n~FGUser deletion abandoned.\n\n");
	break;

	default: return;
	}

DELTEMP:
if (O_FLAGISSET(del_user,USER_FLAG_TEMP_OBJECT)) delete del_user;
del_user = NULL;
stage = USER_STAGE_CMD_LINE;
}




/*** Save a new configuration file. ***/
void cl_user::run_save_config(int at_prompt)
{
cl_server *svr;
FILE *fp;
int i,first;
char locport[6];

if (at_prompt) {
	if (!com->wcnt) return;
	switch(toupper(com->word[0][0])) {
		case 'N':
		uprintf("\n~FYSave config cancelled.\n\n");
		stage = USER_STAGE_CMD_LINE;
		FREE(com_filename);
		return;

		case 'Y': break;

		default:  return;
		}
	}

if (!(fp = fopen(com_filename,"w"))) {
	errprintf("Cannot open file to write: %s\n",strerror(errno));
	FREE(com_filename);
	return;
	}

// Save config details
fprintf(fp,"#\n# NUTS-IV configuration file.\n#\n# Server version: %s\n",VERSION);
fprintf(fp,"# Generated by  : %04X (%s)\n# Date          : %s#\n\n",
	id,name,ctime(&server_time));
fprintf(fp,"\"server name\" = %s\n",server_name);
fprintf(fp,"\"working dir\" = \"%s\"\n",working_dir);
fprintf(fp,"\"bind interface\" = %s\n",
	main_bind_addr.sin_addr.s_addr == INADDR_ANY ? "ALL" : inet_ntoa(main_bind_addr.sin_addr));

fprintf(fp,"\"user port\" = %d\n",tcp_port[PORT_TYPE_USER]);
fprintf(fp,"\"server port\" = %d\n",tcp_port[PORT_TYPE_SERVER]);
fprintf(fp,"\"server port listening\" = %s\n",
	noyes[SYS_FLAGISSET(SYS_FLAG_SVR_LISTEN)]);
fprintf(fp,"\"connect key\" = %d\n",local_connect_key);
fprintf(fp,"\"soft max servers\" = %d\n",soft_max_servers);
if (ansi_terms->wcnt) {
	fprintf(fp,"\"ansi terminals\" = %s",ansi_terms->word[1]);
	for(i=2;i < ansi_terms->wcnt;++i)
		fprintf(fp,",%s",ansi_terms->word[i]);
	fprintf(fp,"\n");
	}
if (SYS_FLAGISSET(SYS_FLAG_IGNORING_SIGS)) {
	fprintf(fp,"\"signal ignore\" = ");
	for(i=0,first=1;i < NUM_SIGNALS;++i) {
		if (siglist[i].ignoring) {
			if (first) {
				fprintf(fp,"%s",siglist[i].name);  first=0;
				}
			else fprintf(fp,",%s",siglist[i].name);
			}
		}
	fprintf(fp,"\n");
	}
fprintf(fp,"\"max name length\" = %d\n",max_name_len);
fprintf(fp,"\"max desc length\" = %d\n",max_desc_len);
fprintf(fp,"\"default desc\" = \"%s\"\n",default_desc);
fprintf(fp,"\"default pwd\" = \"%s\"\n",default_pwd);
fprintf(fp,"\"min pwd length\" = %d\n",min_pwd_len);
fprintf(fp,"\"max local users\" = %d\n",max_local_users);
fprintf(fp,"\"max remote users\" = %d\n",max_remote_users);
fprintf(fp,"\"max user ignore level\" = %s\n",user_level[max_user_ign_level]);
fprintf(fp,"\"max hop\" = %d\n",max_hop_cnt);
fprintf(fp,"\"max profile chars\" = %d\n",max_profile_chars);
fprintf(fp,"\"max mail chars\" = %d\n",max_mail_chars);
fprintf(fp,"\"max board chars\" = %d\n",max_board_chars);
fprintf(fp,"\"max broadcast chars\" = %d\n",max_broadcast_chars);
fprintf(fp,"\"max subject length\" = %d\n",max_subject_len);
fprintf(fp,"\"max group name length\" = %d\n",max_group_name_len);
fprintf(fp,"\"max group desc chars\" = %d\n",max_group_desc_chars);
fprintf(fp,"\"max batch name length\" = %d\n",max_batch_name_len);
fprintf(fp,"\"max login batch lines\" = %d\n",max_login_batch_lines);
fprintf(fp,"\"max logout batch lines\" = %d\n",max_logout_batch_lines);
fprintf(fp,"\"max session batch lines\" = %d\n",max_session_batch_lines);
fprintf(fp,"\"linkdead timeout\" = %d,seconds\n",linkdead_timeout);
fprintf(fp,"\"login timeout\" = %d,seconds\n",login_timeout);
fprintf(fp,"\"idle timeout\" = %d,seconds\n",idle_timeout);
fprintf(fp,"\"idle timeout ignore level\" = %s\n",
	user_level[idle_timeout_ign_level]);
fprintf(fp,"\"board msg expire\" = %d,seconds\n",board_msg_expire);
fprintf(fp,"\"board renumber\" = %s\n",
	noyes[SYS_FLAGISSET(SYS_FLAG_BOARD_RENUM)]);
fprintf(fp,"\"ping interval\" = %d,seconds\n",send_ping_interval);
fprintf(fp,"\"autosave interval\" = %d,seconds\n",autosave_interval);
fprintf(fp,"\"server timeout\" = %d,seconds\n",server_timeout);
fprintf(fp,"\"connect timeout\" = %d,seconds\n",connect_timeout);
fprintf(fp,"\"max tx errors\" = %d\n",max_tx_errors);
fprintf(fp,"\"max rx errors\" = %d\n",max_rx_errors);
fprintf(fp,"\"max packet rate\" = %d\n",max_packet_rate);
fprintf(fp,"\"max local user data rate\" = %d\n",max_local_user_data_rate);
fprintf(fp,"\"max server data rate\" = %d\n",max_svr_data_rate);
fprintf(fp,"\"random remote ids\" = %s\n",
	noyes[SYS_FLAGISSET(SYS_FLAG_RANDOM_REM_IDS)]);
fprintf(fp,"\"allow loopback\" = %s\n",
	noyes[SYS_FLAGISSET(SYS_FLAG_LOOPBACK_USERS)]);
fprintf(fp,"\"group modify level\" = %s\n",user_level[group_modify_level]);
fprintf(fp,"\"group gatecrash level\" = %s\n",
	user_level[group_gatecrash_level]);
fprintf(fp,"\"group invite expire\" = %d,seconds\n",group_invite_expire);
fprintf(fp,"\"log groups\" = %s\n",
	noyes[SYS_FLAGISSET(SYS_FLAG_LOG_GROUPS)]);
fprintf(fp,"\"lockout level\" = %s\n",user_level[lockout_level]);
fprintf(fp,"\"remote user max level\" = %s\n",
	user_level[remote_user_max_level]);
fprintf(fp,"\"review lines\"= %d\n",num_review_lines);
fprintf(fp,"\"go invis level\" = %s\n",user_level[go_invis_level]);
fprintf(fp,"\"prisoners ret home\" = %s\n",
	noyes[SYS_FLAGISSET(SYS_FLAG_PRS_RET_HOME)]);
fprintf(fp,"\"log net broadcasts\" = %s\n",
	noyes[SYS_FLAGISSET(SYS_FLAG_LOG_NETBCAST)]);
fprintf(fp,"\"recv net bcast level\" = %s\n",user_level[recv_net_bcast_level]);
fprintf(fp,"\"hexdump packets\" = %s\n",
	noyes[SYS_FLAGISSET(SYS_FLAG_HEXDUMP_PACKETS)]);
fprintf(fp,"\"strip printcodes\" = %s\n",
	noyes[SYS_FLAGISSET(SYS_FLAG_STRIP_PRINTCODES)]);
fprintf(fp,"\"allow who at login\" = %s\n",
	noyes[SYS_FLAGISSET(SYS_FLAG_ALLOW_WHO_AT_LOGIN)]);
fprintf(fp,"\"allow remote batch runs\" = %s\n",
	noyes[SYS_FLAGISSET(SYS_FLAG_ALLOW_REM_BATCH_RUNS)]);
fprintf(fp,"\"allow new accounts\" = %s\n",
	noyes[SYS_FLAGISSET(SYS_FLAG_ALLOW_NEW_ACCOUNTS)]);
fprintf(fp,"\"save novice accounts\" = %s\n",
	noyes[SYS_FLAGISSET(SYS_FLAG_SAVE_NOVICE_ACCOUNTS)]);
fprintf(fp,"\"really delete accounts\" = %s\n",
	noyes[SYS_FLAGISSET(SYS_FLAG_REALLY_DEL_ACCOUNTS)]);
fprintf(fp,"\"delete disconnected incoming\" = %s\n",
	noyes[SYS_FLAGISSET(SYS_FLAG_DEL_DISC_INCOMING)]);
fprintf(fp,"\"log unexpected packets\" = %s\n",
	noyes[SYS_FLAGISSET(SYS_FLAG_LOG_UNEXPECTED_PKTS)]);
fprintf(fp,"\"incoming encryption policy\" = %s\n",
	inc_encrypt_polstr[incoming_encryption_policy]);
fprintf(fp,"\"outgoing encryption enforce\" = %s\n",
	noyes[SYS_FLAGISSET(SYS_FLAG_OUT_ENF_ENCRYPTION)]);
fprintf(fp,"\"resolve ip name internally\" = %s\n",
	noyes[SYS_FLAGISSET(SYS_FLAG_INTERNAL_RESOLVE)]);

fprintf(fp,"\n#\n# Servers\n#\n");
FOR_ALL_SERVERS(svr) {
	if (svr->connection_type == SERVER_TYPE_OUTGOING) {
		if (svr->local_port) sprintf(locport,"%d",svr->local_port);
		else locport[0] = '\0';

		fprintf(fp,"server = %s, %s, %d, %s, %d%s\n",
			svr->name,
			svr->ipstr,
			ntohs(svr->ip_addr.sin_port),
			locport,
			svr->connect_key,
			svr->encryption_level ? ", encrypt" : "");
		}
	}
fprintf(fp,"\n# END\n");

fclose(fp);
uprintf("Config file \"%s\" ~FGSAVED.\n",com_filename);

if (at_prompt) 
	log(1,"User %04X (%s) OVERWROTE old config file \"%s\"",
		id,name,com_filename);
else
	log(1,"User %04X (%s) wrote config file \"%s\"",id,name,com_filename);

FREE(com_filename);
stage = USER_STAGE_CMD_LINE;
}




///////////////////////// MISCELLANIOUS FUNCTIONS //////////////////////////


/*** Tell or pemote the user something. If they're not receiving tells
     return error but if they're AFK store tell in users buffer. ***/
int cl_user::tell(
	int ttype,uint16_t uid,char *uname,char *svrname,char *msg)
{
char sn[MAX_SERVER_NAME_LEN+2];

if (svrname) sprintf(sn,"@%s",svrname);
else sn[0]='\0';

switch(ttype) {
	case TELL_TYPE_TELL:
	sprintf(text,"~FMPRIVATE ~FT%04X%s,%s~FG:~RS %s\n",uid,sn,uname,msg);
	break;

	case TELL_TYPE_PEMOTE:
	sprintf(text,"~FMPRIVATE ~FT%04X%s~FG:~RS %s %s\n",uid,sn,uname,msg);
	break;

	case TELL_TYPE_FRIENDS_TELL:
	sprintf(text,"~FMFRIENDS ~FT%04X%s,%s~FG:~RS %s\n",uid,sn,uname,msg);
	break;

	case TELL_TYPE_FRIENDS_PEMOTE:
	sprintf(text,"~FMFRIENDS ~FT%04X%s~FG:~RS %s %s\n",uid,sn,uname,msg);
	break;

	default: return ERR_INVALID_TYPE;
	}

// No tell flag overrides AFK flag
if (FLAGISSET(USER_FLAG_NO_TELLS)) return ERR_USER_NO_TELLS;

add_review_line(revbuff,&revpos,text);
if (is_afk()) {
	afk_tell_cnt++;
	return ERR_USER_AFK;
	}

uprintf(text);
return OK;
}




/*** Print an info message ***/
void cl_user::infoprintf(char *fmtstr, ...)
{
char str[ARR_SIZE];
va_list args;

if (!FLAGISSET(USER_FLAG_NO_INFO)) {
	va_start(args,fmtstr);
	vsnprintf(str,ARR_SIZE,fmtstr,args);
	va_end(args);

	uprintf("~FTINFO:~RS %s",str);
	}
}




/*** Print a warning message ***/
void cl_user::warnprintf(char *fmtstr, ...)
{
char str[ARR_SIZE];
va_list args;

va_start(args,fmtstr);
vsnprintf(str,ARR_SIZE,fmtstr,args);
va_end(args);

uprintf("~OL~FYWARNING:~RS %s",str);
}




/*** Print an error message ***/
void cl_user::errprintf(char *fmtstr, ...)
{
char str[ARR_SIZE];
va_list args;

va_start(args,fmtstr);
vsnprintf(str,ARR_SIZE,fmtstr,args);
va_end(args);

uprintf("~OL~FRERROR:~RS %s",str);
}




/*** Print a system message ***/
void cl_user::sysprintf(char *fmtstr, ...)
{
char str[ARR_SIZE];
va_list args;

va_start(args,fmtstr);
vsnprintf(str,ARR_SIZE,fmtstr,args);
va_end(args);

uprintf("~OLSYSTEM:~RS %s",str);
}




/*** Print prompt ***/
void cl_user::prompt()
{
cl_remote_user *ru;
u_char hc;

// If editing then just return , editor does its own prompts.
if (editor) return;

// If paging a file or command output display page prompt
if (page_pos || com_page_line != -1) {
	uprintf("~IP~BB~FG*** MORE ***~RS");  return;
	}

switch(stage) {
	case USER_STAGE_LOGIN_ID:
	if (SYS_FLAGISSET(SYS_FLAG_ALLOW_NEW_ACCOUNTS))
		uprintf("Enter id or 'new': ");
	else
		uprintf("Enter id: ");
	return;

	case USER_STAGE_LOGIN_PWD:
	uprintf("Enter password: ");
	return;

	case USER_STAGE_LOGIN_NAME:
	uprintf("Enter a name: ");
	return;

	case USER_STAGE_LOGIN_NEW_PWD:
	uprintf("Enter a password: ");
	return;

	case USER_STAGE_LOGIN_REENTER_PWD:
	uprintf("Re-enter password: ");
	return;

	case USER_STAGE_AFK:
	case USER_STAGE_CMD_LINE:
	if (FLAGISSET(USER_FLAG_PROMPT)) {
		if (type == USER_TYPE_LOCAL) {
			text[0] = '\0';  hc = 0;
			}
		else {
			ru = (cl_remote_user *)this;
			sprintf(text,"~FY%04X@%s~RS",
				ru->remote_id,ru->server_from->name);
			hc = ru->hop_count;
			}

		uprintf("~FT[%04X@%s~RS,%s,~FM%04X~RS,~FG%d~FT]\n",
			id,server_name,text,group->id,hc);
		}
	return;

	case USER_STAGE_MAILER:
	uprintf("~IP~FTMail>~RS ");
	return;

	case USER_STAGE_MAILER_SUBJECT1:
	case USER_STAGE_MAILER_SUBJECT2:
	case USER_STAGE_BOARD_SUBJECT:
	uprintf("\n~IP~FYSubject:~RS ");
	return;

	case USER_STAGE_MAILER_READ_FROM:
	case USER_STAGE_BOARD_READ_FROM:
	uprintf("~IP~BB~FG*** END OF MESSAGE %d ***~RS",current_msg);
	return;

	case USER_STAGE_MAILER_DEL:
	case USER_STAGE_BOARD_DEL:
	uprintf("\n~IPDelete message(s), are you sure? ");
	return;

	case USER_STAGE_BOARD:
	uprintf("~IP~FTBoard %04X>~RS ",group->id); 
	return;

	case USER_STAGE_OLD_PWD:
	uprintf("~IPEnter old password: ");
	return;

	case USER_STAGE_NEW_PWD:
	uprintf("~IPEnter new password: ");
	return;

	case USER_STAGE_REENTER_PWD:
	uprintf("~IPRe-enter new password: ");
	return;

	case USER_STAGE_SUICIDE:
	uprintf("~IPEnter password: ");
	return;

	case USER_STAGE_EXAMINE:
	uprintf("~IP~BB~FG*** END OF PROFILE ***~RS");
	return;

	case USER_STAGE_SHUTDOWN:
	if (!shutdown_secs)
		uprintf("~IP~OL~FRShutdown the system immediately, are you sure?~RS ");
	else
	uprintf("~IP~OL~FRShutdown the system in %s time, are you sure?~RS ",
		time_period(shutdown_secs));
	break;
	
	case USER_STAGE_REBOOT:
	if (!shutdown_secs)
		uprintf("~IP~OL~FRReboot the system immediately, are you sure?~RS ");
	else
	uprintf("~IP~OL~FRReboot the system in %s time, are you sure?~RS ",
		time_period(shutdown_secs));
	break;

	case USER_STAGE_AFK_LOCK:
	uprintf("~IPEnter password to unlock session: ");
	break;

	case USER_STAGE_DELETE_GROUP:
	// If we're in the group thats being destructed then this will
	// get called by del_group will have been reset to NULL.
	if (del_group)
		uprintf("~IPDelete group ~FT%04X~RS (%s~RS), are you sure? ",
			del_group->id,del_group->name);
	break;

	case USER_STAGE_DELETE_GROUP_LOG:
	if (del_group)
		uprintf("~IPDelete group ~FT%04X~RS (%s~RS) logfile, are you sure? ",
			del_group->id,del_group->name);
	break;

	case USER_STAGE_DELETE_BATCH_FILE:
	uprintf("~IPDelete batch file '%s', are you sure? ",com_filename);
	break;

	case USER_STAGE_DELETE_USER:
	uprintf("~IPDelete user ~FT%04X~RS (%s), are you sure? ",
		del_user->id,del_user->name);
	break;

	case USER_STAGE_OVERWRITE_BATCH_FILE:
	case USER_STAGE_OVERWRITE_CONFIG_FILE:
	uprintf("~IPOverwrite file '%s', are you sure? ",com_filename);
	}
}




/*** Clear an inline prompt off the screen. Only works on ansi terminals. ***/
void cl_user::clear_inline_prompt()
{
if (FLAGISSET(USER_FLAG_ANSI_TERM)) uprintf("~CU\r~LC");
}




/*** Set user description ***/
int cl_user::set_desc(char *dsc)
{
char *tmp;

if (!dsc) dsc = "";

if ((int)strlen(dsc) > max_desc_len) dsc[max_desc_len] = '\0';
tmp = strdup(dsc);
if (!tmp) return ERR_MALLOC;
free(desc);
desc = tmp;
return OK;
}




/*** Add a friend to users list ***/
int cl_user::add_friend(char *idstr,int linenum)
{
cl_friend *frnd;
char *svrname;
uint16_t fid;

/* Check id is ok. We don't use idstr_to_id_and_svr() function because if 
   server is not in the server list we'll get an error and friend won't get
   added. We want it added even if no such server. */
svrname=strchr(idstr,'@');
if (svrname) *(svrname++)='\0';

// Invalid if no such id or its our own id
if (!(fid = idstr_to_id(idstr)) || 
    (!svrname && fid == id)) return ERR_INVALID_ID;

// Make sure we don't have friend in list already
FOR_ALL_FRIENDS(frnd) {
	if (frnd->id == fid &&
	    ((!svrname && !frnd->svr_name) ||
	    (svrname && frnd->svr_name && !strcasecmp(frnd->svr_name,svrname))))
			return ERR_ALREADY_IN_LIST;
	}
new cl_friend(this,fid,svrname,linenum);
return OK;
}




/*** Show whos in a group ***/
void cl_user::look(cl_group *grp, int do_page)
{
cl_user *u;
int lcnt,ucnt,paged;
char path[MAXPATHLEN];
char *acc,*fixed;

if (com_page_line == -1) {
	uprintf("\n~FYGroup :~RS %04X, %s\n",grp->id,grp->name);
	uprintf("~FYType  :~RS %s\n",group_type[grp->type]);
	if (grp->type == GROUP_TYPE_SYSTEM) {
		acc = "N/A";
		fixed = "N/A";
		}
	else {
		acc = (char *)(O_FLAGISSET(grp,GROUP_FLAG_PRIVATE) ?
			"~FRPRIVATE" : "~FGPUBLIC");
		fixed = colnoyes[O_FLAGISSET(grp,GROUP_FLAG_FIXED)];
		}
	uprintf("~FYAccess:~RS %s\n",acc);
	uprintf("~FYFixed :~RS %s\n\n",fixed);

	paged = 0;
	switch(grp->type) {
		case GROUP_TYPE_SYSTEM:
		if (grp == gone_remote_group) 
			uprintf("This is the group for users who are remote.\n");
		else
		if (grp == remotes_home_group)
			uprintf("This is the home group for remote users.\n");
		else
		if (grp == prison_group)
			uprintf("You are in the prison group. Feel free to contemplate your crime.\n");

		paged = 1;
		break;


		case GROUP_TYPE_PUBLIC:
		sprintf(path,"%s/%04X/%s",
			PUB_GROUP_DIR,grp->id,PUB_GROUP_DESC_FILE);
		paged = (page_file(path,0) == OK);
		break;

		case GROUP_TYPE_USER:
		sprintf(path,"%s/%04X/%s",
			USER_DIR,grp->id,USER_GROUP_DESC_FILE);
		paged = (page_file(path,0) == OK);
		}

	if (!paged) uprintf("<No group description>\n");
	}

lcnt = 0;
ucnt = 0;
FOR_ALL_USERS(u) {
	if (u->level < USER_LEVEL_NOVICE || 
	    !u->group || u->group->id != grp->id || u == this ||
	    (O_FLAGISSET(u,USER_FLAG_INVISIBLE) && level < u->level)) continue;

	++ucnt;
	if (!do_page || com_page_line == -1 || ucnt > com_page_line) {
		if (ucnt == 1)
			uprintf("\n~FYThe following users are in this group:\n");
		uprintf("   ~FT%04X,~RS %s %s   %s%s%s%s\n",
			u->id,u->name,u->desc,
			u->is_afk() ? "~OL~BY(AFK)~RS " : "",
			O_FLAGISSET(u,USER_FLAG_MUZZLED) ? "~OL~BR(MUZ)~RS " : "",
			O_FLAGISSET(u,USER_FLAG_INVISIBLE) ? "~OL~BM(INV)~RS " : "",
			O_FLAGISSET(u,USER_FLAG_LINKDEAD) ? "~OL~BR(LINKDEAD)~RS " : "");

		if (do_page &&
		    FLAGISSET(USER_FLAG_PAGING) && ++lcnt == term_rows - 2) {
			com_page_line = ucnt;  return;
			}
		}
	}

if (do_page && FLAGISSET(USER_FLAG_PAGING) && lcnt && lcnt >= term_rows - 5) {
	com_page_line = ucnt;  return;
	}

if (ucnt) uprintf("\nTotal of ~OL~FT%d~RS other users visible.\n",ucnt);
else uprintf("\nThere is no one visible in this group.\n");

if (grp->board) {
	if (grp->board->msgcnt) {
		if (grp->board->todays_msgcnt)
			uprintf("There are ~OL~FT%d~RS messages (~OL~FM%d~RS new today) on the board.\n\n",grp->board->msgcnt,grp->board->todays_msgcnt);
		else
			uprintf("There are ~OL~FT%d~RS messages on the board.\n\n",grp->board->msgcnt);
		}
	else
		uprintf("There are no messages on the board.\n\n");
	}
else uprintf("There is no message board in this group.\n\n");

com_page_line = -1;
}



/*** Show details of given user ***/
void cl_user::show_details(cl_user *u)
{
cl_local_user *lu;
cl_remote_user *ru;
cl_group *grp;
cl_mail *tmail;

uprintf("Name              : \"%s\"\n",u->name);
uprintf("Description       : \"%s\"\n",u->desc);
uprintf("User type         : %s\n",user_type[u->type]);
uprintf("User level        : %s\n",user_level[u->level]);

if (O_FLAGISSET(u,USER_FLAG_TEMP_OBJECT)) {
	// Temp objects will always be local user types
	lu = (cl_local_user *)u;
	uprintf("Logged off at     : %s",ctime(&(lu->logoff_time)));
	uprintf("Session duration  : %s\n",time_period(lu->session_duration));

	grp = get_group(lu->start_group_id);
	uprintf("Start group       : %04X, %s\n",
		lu->start_group_id,grp ? grp->name : "<unknown>");

	show_gmi_details(u);

	tmail = new cl_mail(NULL,u->id);
	uprintf("Unread mail mesgs : %d\n",tmail->unread_msgcnt);
	delete tmail;

	if (level == USER_LEVEL_ADMIN)
		uprintf("Prev. IP address  : %s\n",lu->prev_ip_addr);

	if (level > USER_LEVEL_MONITOR) 
		uprintf("Email address     : %s\n\n",
			lu->email_address ? lu->email_address : "<not set>");
	else uprintf("\n");
	return;
	}

// Everything below only applies to logged in users
if (u->type == USER_TYPE_LOCAL) {
	lu = (cl_local_user *)u;
	if (u == this || level == USER_LEVEL_ADMIN) {
		uprintf("IP address        : %s:%d  (%s)\n",
			u->ipnumstr,ntohs(u->ip_addr.sin_port),u->ipnamestr);
		uprintf("Prev. IP address  : %s\n",lu->prev_ip_addr);
		}
	uprintf("Socket number     : %d\n",lu->sock);
	uprintf("Terminal type     : %s\n",lu->term_type);
	}
else {
	ru=(cl_remote_user *)u;
	uprintf("Server from       : %s\n",ru->server_from->name);
	uprintf("Hop number        : %d\n",ru->hop_count);
	uprintf("Home Id           : %04X\n",ru->orig_id);
	uprintf("Home server name  : %s\n",
		ru->home_svrname ? ru->home_svrname : "<unknown>");
	uprintf("Home server IP    : %s:%d  (%s)\n",
		u->ipnumstr,ntohs(u->ip_addr.sin_port),u->ipnamestr);
	uprintf("Home user level   : %s\n",user_level[ru->orig_level]);
	}
uprintf("Terminal size     : %dx%d\n",u->term_cols,u->term_rows);
uprintf("Logged on         : %s",ctime(&u->login_time));
uprintf("Session duration  : %s\n",time_period(server_time - u->login_time));
if (u->type == USER_TYPE_LOCAL)
	uprintf("Prev. session dur.: %s\n",time_period(lu->session_duration));

uprintf("Current group     : %04X, %s\n",u->group->id,u->group->name);
if (u == this || level > USER_LEVEL_MONITOR) {
	uprintf("Monitoring group  : ");
	if (u->mon_group)
		uprintf("%04X, %s\n",u->mon_group->id,u->mon_group->name);
	else
		uprintf("<none>\n");
	}

uprintf("Idle time         : %s\n",time_period(server_time - u->last_input_time));
if (u->ping_svr)
	uprintf("Pinging a server  : ~FYYES ~FT->~RS %s\n",u->ping_svr->name);
else
	uprintf("Pinging a server  : ~FGNO\n");
if (u->server_to) 
	uprintf("Gone remote       : ~FYYES ~FT->~RS %s\n",u->server_to->name);
else
	uprintf("Gone remote       : ~FGNO\n");

uprintf("AFK               : ");
if (u->is_afk()) {
	uprintf("~FYYES~RS");
	if (u->afk_msg) uprintf(" -> %s\n",u->afk_msg);
	else uprintf("\n");
	}
else uprintf("~FGNO\n");

show_gmi_details(u);
	
uprintf("Invisible         : %s\n",
	colnoyes[O_FLAGISSET(u,USER_FLAG_INVISIBLE)]);
uprintf("Other flags       : %s\n",u->flags_list());

if (u->type == USER_TYPE_LOCAL) {
	grp = get_group(lu->start_group_id);
	uprintf("Start group       : %04X, %s\n",
		lu->start_group_id,grp ? grp->name : "<unknown>");

	uprintf("Unread mail mesgs : %d\n",lu->mail->unread_msgcnt);
	if (u == this || level > USER_LEVEL_MONITOR) 
		uprintf("Email address     : %s\n\n",
			lu->email_address ? lu->email_address : "<not set>");
	else uprintf("\n");
	}
else uprintf("\n");
}




/*** This is used in 2 places in show_details() and I don't want to have
     to duplicate this much code ***/
void cl_user::show_gmi_details(cl_user *u)
{
if (u->type == USER_TYPE_LOCAL)
	uprintf("Home group persist: %s\n",
		O_FLAGISSET(u,USER_FLAG_HOME_GRP_PERSIST) ? "~FYYES" : "~FGNO");

uprintf("Muzzled           : ");
if (O_FLAGISSET(u,USER_FLAG_MUZZLED)) {
	uprintf("~FYYES\n");
	uprintf("Muzzled on        : %s",ctime(&u->muzzle_start_time));
	uprintf("Muzzle time left  : ");
	if (u->muzzle_end_time)
		uprintf("%s\n",time_period(u->muzzle_end_time - server_time));
	else
		uprintf("~FYINDEFINITE\n");
	}
else uprintf("~FGNO\n");

uprintf("Prisoner          : ");
if (O_FLAGISSET(u,USER_FLAG_PRISONER)) {
	uprintf("~FRYES\n");
	uprintf("Imprisoned on     : %s",ctime(&u->imprisoned_time));
	uprintf("Sentence remaining: ");
	if (u->release_time) 
		uprintf("%s\n",time_period(u->release_time - server_time));
	else
		uprintf("~FYINDEFINITE\n");
	}
else uprintf("~FGNO\n");
}




/*** Creates a string of most the flags user has set. Some we're simply not
     interested in however such as prompt as that is self evident. ***/
char *cl_user::flags_list()
{
static char flagstr[100];

flagstr[0]='\0';
if (FLAGISSET(USER_FLAG_PROMPT)) strcat(flagstr,"PROMPT ");
if (FLAGISSET(USER_FLAG_PAGING)) strcat(flagstr,"PAGING ");
if (FLAGISSET(USER_FLAG_CONVERSE)) strcat(flagstr,"CONVERSE ");
if (FLAGISSET(USER_FLAG_ANSI_TERM)) strcat(flagstr,"COLOUR ");
if (FLAGISSET(USER_FLAG_NO_SPEECH)) strcat(flagstr,"NOSPEECH ");
if (FLAGISSET(USER_FLAG_NO_TELLS)) strcat(flagstr,"NOTELLS ");
if (FLAGISSET(USER_FLAG_NO_SHOUTS)) strcat(flagstr,"NOSHOUTS ");
if (FLAGISSET(USER_FLAG_NO_INFO)) strcat(flagstr,"NOINFO ");
if (FLAGISSET(USER_FLAG_NO_MISC)) strcat(flagstr,"NOMISC ");
if (FLAGISSET(USER_FLAG_PUIP)) strcat(flagstr,"PUIP ");
if (FLAGISSET(USER_FLAG_INVISIBLE)) strcat(flagstr,"INVISIBLE ");
if (FLAGISSET(USER_FLAG_RECV_NET_BCAST)) strcat(flagstr,"NETBCAST ");
if (FLAGISSET(USER_FLAG_LINKDEAD)) strcat(flagstr,"LINKDEAD ");
if (FLAGISSET(USER_FLAG_AUTOSILENCE)) strcat(flagstr,"AUTOSIL ");
if (!flagstr[0]) strcpy(flagstr,"<none>");

return flagstr;
}




/*** Release a user from prison ***/
int cl_user::release_from_prison(cl_user *uby)
{
cl_local_user *lu;
int ret;

UNSETFLAG(USER_FLAG_PRISONER);
imprison_level = USER_LEVEL_NOVICE;
release_time = 0;

if (uby)
	sprintf(text,"~SN~LI~OLYou have been ~FGRELEASED~RS~LI~OL from prison by %04X (%s)!\n",uby->id,uby->name);
else
	strcpy(text,"~SN~LI~OLYou have been ~FGRELEASED~RS~LI~OL from prison!\n");

if (FLAGISSET(USER_FLAG_TEMP_OBJECT)) {
	lu = (cl_local_user *)this;
	if ((ret = lu->send_system_mail("~FGRELEASED",text)) != OK)
		uby->errprintf("Mail send failed: %s\n",err_string[ret]);
	}
else {
	UNSETPREVFLAG(USER_FLAG_PRISONER);
	uprintf(text);
	prison_group->geprintf(
		MSG_MISC,this,NULL,
		"User ~FT%04X~RS (%s) is now ~FGFREE!\n",id,name);

	// Send convict home if thats how we're configured.
	if (SYS_FLAGISSET(SYS_FLAG_PRS_RET_HOME)) {
		uprintf("\nYou return to your home group...\n");
		home_group->join(this);
		prompt();
		}
	}

if (uby) log(1,"User %04X (%s) RELEASED user %04X (%s).",
	uby->id,uby->name,id,name);
else log(1,"User %04X (%s) has been RELEASED from prison.\n",id,name);

return (type == USER_TYPE_LOCAL ? 
        ((cl_local_user *)this)->save() : OK);
}




/*** Unmuzzle a user ***/
int cl_user::unmuzzle(cl_user *uby)
{
cl_local_user *lu;
int ret;

UNSETFLAG(USER_FLAG_MUZZLED);

// Reset just to be tidy. Don't reset start time as this is a record
// of when user was last muzzled and I might use it in the future
muzzle_level = USER_LEVEL_NOVICE; 
muzzle_end_time = 0;

if (uby)
	sprintf(text,"~OLYou have been ~FGUNMUZZLED~RS~OL by %04X, %s.\n",
		uby->id,uby->name);
else
	sprintf(text,"~OLYou have been ~FGUNMUZZLED~RS~OL.\n");

if (FLAGISSET(USER_FLAG_TEMP_OBJECT)) {
	lu = (cl_local_user *)this;
	if ((ret = lu->send_system_mail("~FGUNMUZZLED",text)) != OK)
		uby->errprintf("Mail send failed: %s\n",err_string[ret]);
	}
else {
	UNSETPREVFLAG(USER_FLAG_MUZZLED);
	uprintf(text);
	group->geprintf(
		MSG_MISC,uby,this,
		"User ~FT%04X~RS (%s) has been ~FGUNMUZZLED.\n",id,name);
        }

if (uby)
	log(1,"User %04X (%s) UNMUZZLED user %04X (%s).",
		uby->id,uby->name,id,name);
else 
	log(1,"User %04X (%s) has been UNMUZZLED.\n",id,name);

return (type == USER_TYPE_LOCAL ?
	((cl_local_user *)this)->save() : OK);
}




/*** Return position if user is afk ***/
int cl_user::is_afk()
{
return (stage == USER_STAGE_AFK || stage == USER_STAGE_AFK_LOCK);
}




/*** Send a broadcast message to everyone ***/
void cl_user::send_broadcast(char *mesg)
{
if ((int)strlen(mesg) > max_broadcast_chars) {
	warnprintf("Message is too long, truncating to %d characters.\n",
		max_broadcast_chars);
	mesg[max_broadcast_chars] = '\0';
	}
allprintf(MSG_BCAST,USER_LEVEL_NOVICE,NULL,
        "\n~SN~OL~BR*** Broadcast message from ~FT%04X~RS~OL~BR (%s) ***\n\n%s\n\n",
        id,name,mesg);
}




/*** Send a network broadcast message ***/
void cl_user::send_net_broadcast(char *mesg)
{
cl_server *svr;
int ret,cnt;
char *s;

if ((int)strlen(mesg) > max_broadcast_chars) {
	warnprintf("Message is too long, truncating to %d characters.\n",
		max_broadcast_chars);
	mesg[max_broadcast_chars] = '\0';
	}

// First send to all local admins
allprintf(MSG_NETBCAST,recv_net_bcast_level,NULL,
	"\n~SN~OL~BM*** Network broadcast message from ~FT%04X~RS~OL~BM (%s) ***\n\n%s\n\n",
	id,name,mesg);

cnt = 0;
FOR_ALL_SERVERS(svr) {
	if (svr->stage == SERVER_STAGE_CONNECTED) {
		if ((ret = svr->send_mesg(
			PKT_INF_BCAST,0,id,0,name,mesg)) == OK) {
			uprintf("~FGSent to server:~RS %s\n",svr->name);
			cnt++;
			}
		else
			errprintf("Unable to send broadcast to server '%s': %s\n",
				svr->name,err_string[ret]);
		}
	}
if (cnt) {
	uprintf("\n%d servers received the broadcast.\n\n",cnt);

	// Convert any newlines to slashes
	for(s=mesg;*s;++s) if (*s == '\n') *s = '/';
	log(1,"User %04X (%s) sent NETWORK BROADCAST: %s\n",id,name,mesg);
	}
else uprintf("No remote servers are connected.\n");
}




/*** Reset user back to command stage ***/
void cl_user::reset_to_cmd_stage()
{
exa_user = NULL;
del_user = NULL;
del_group = NULL;
uprintf("~NP\n\n~FYResetting to command line.\n\n");
stage = USER_STAGE_CMD_LINE;
prompt();
}
