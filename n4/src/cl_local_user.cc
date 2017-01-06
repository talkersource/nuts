/***************************************************************************
 FILE: cl_local_user.cc
 LVU : 1.4.0

 DESC:
 This file contains the code for doing stuff specific to a local user such
 as reading from their socket, logging in, mail, loading & saving user info
 and the definitions of some command functions declared virtual in cl_user.

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

/*** Create local user, send telopt and add to list ***/
cl_local_user::cl_local_user(int is_temp_obj): cl_user(is_temp_obj)
{
struct linger lin;
int size;
char *tmp;

error = OK;
desc = strdup(default_desc);
type = USER_TYPE_LOCAL;
// Set up default flags
flags = prev_flags = 
	((is_temp_obj ? USER_FLAG_TEMP_OBJECT : 0) | 
	USER_FLAG_PROMPT | USER_FLAG_PAGING | USER_FLAG_AUTOSILENCE);
bpos = 0;
tbpos = 0;
sock = -1;
pwd = NULL;
login_attempts = 0;
login_errors = 0;
logoff_time = 0;
session_duration = 0;
linkdead_time = 0;
inline_prompt[0] = '\0';
term_type = NULL;
mail = NULL;
mail_to_idstr = NULL;
mail_to_id = 0;
mail_to_svr = NULL;
email_address = NULL;
start_group_id = 0;
batch_name = NULL;
batchfp = NULL;
batch_type = BATCH_TYPE_NOBATCH;
batch_create_type = BATCH_TYPE_NOBATCH;
batch_line = 0;
batch_max_lines = 0;
rx_period_data = 0;
rx_period_data_start = get_hsec_time();
prev_ip_addr = strdup("<unknown>");

// Nothing else needs to be done if temporary object
if (is_temp_obj) return;

/* C++ compilers seem to have a disagreement over what the type of size
   should be. After 2 decades you'd assume it would have been standardised
   by now. Pathetic. */
size = sizeof(sockaddr_in);
#ifdef SOLARIS
if ((sock=accept(
	listen_sock[PORT_TYPE_USER],(sockaddr *)&ip_addr,&size)) == -1) {
#else
if ((sock=accept(
	listen_sock[PORT_TYPE_USER],
	(sockaddr *)&ip_addr,(socklen_t *)&size)) == -1) {
#endif
	log(1,"ERROR: cl_local_user::cl_local_user(): accept(): %s",
		strerror(errno));
	goto ERROR;
	}

// Linger on close 
lin.l_onoff=1;
lin.l_linger=1; // Seconds according to posix
if (setsockopt(sock,SOL_SOCKET,SO_LINGER,(char *)&lin,sizeof(lin)) == -1) {
	log(1,"ERROR: cl_local_user::cl_local_user(): setsockopt(): %s\n",
		strerror(errno));
	goto ERROR;
	}

// Set ip number and log
ipnumstr = strdup((char *)inet_ntoa(ip_addr.sin_addr));
log(1,"User connection from %s",ipnumstr);

// See if user or server already has this address 
if ((tmp = get_ipnamestr(ip_addr.sin_addr.s_addr,this))) {
	free(ipnamestr);
	ipnamestr = tmp;
	log(1,"User address %s = %s",ipnumstr,ipnamestr);
	}
else
/* Spawn thread to get ip address. If we did this inline it could
   hang the server until it returned. Make thread detached since we're
   not interested in joining on it to get a status */
if (create_thread(
	THREAD_TYPE_USER_RESOLVE,
	&ip_thrd,user_resolve_thread_entry,(void *)this) == -1) {
	errprintf("Unable to create thread: %s\n\n",strerror(errno));
        log(1,"ERROR: UID %04X: Unable to create thread: %s",
		id,strerror(errno));
	}

// Print server version then send telopt data
uprintf("\nNUTS-IV version %s\n\n",VERSION);

// Send telopt requests
sprintf(text,"%c%c%c%c%c%c%c%c%c%c%c%c",
	TELNET_IAC,TELNET_WILL,TELNET_SGA,
	TELNET_IAC,TELNET_WILL,TELNET_ECHO,
	TELNET_IAC,TELNET_DO,TELNET_TERM,
	TELNET_IAC,TELNET_DO,TELNET_NAWS);
uwrite(text,12);

local_user_count++;
return;


ERROR:
error = ERR_SOCKET;
if (sock != -1) {
	close(sock);  sock = -1;
	}
}




/*** Close socket and remove from list ***/
cl_local_user::~cl_local_user()
{
cl_friend *frnd,*nfrnd;
char *str;
int ret;

FREE(prev_ip_addr);
if (FLAGISSET(USER_FLAG_TEMP_OBJECT) || error != OK) return;

uprintf("\n~BY*** DISCONNECTING ***\n\n");

/* If we're suiciding or user is novice and system is set not to save accounts
   then rename user directory to a .del one. ie we don't actually delete 
   account but just make it inaccessable in case user changes their mind. */
if (FLAGISSET(USER_FLAG_DELETE)) delete_account(id);
else
if (level == USER_LEVEL_NOVICE && 
    !SYS_FLAGISSET(SYS_FLAG_SAVE_NOVICE_ACCOUNTS)) {
	warnprintf("Your novice account has been ~OL~FR~LIDELETED!\n");
	delete_account(id);
	}
else 
if (level > USER_LEVEL_LOGIN && (ret=save()) != OK) 
	errprintf("Failed to save (all) your details: %s\n",err_string[ret]);

// Just in case thread is still running
cancel_thread(&ip_thrd);

if (sock != -1 || FLAGISSET(USER_FLAG_LINKDEAD)) {
	// Gcc complains if casts not here. Fuck knows why.
	str = (FLAGISSET(USER_FLAG_TIMEOUT) ? 
		(char *)"timed out" : (char *)"disconnected");

	if (sock != -1) {
		close(sock);  sock = -1;
		}

	if (level == USER_LEVEL_LOGIN)
		log(1,"User from %s (%s) %s at login.",ipnumstr,ipnamestr,str);
	else {
		allprintf(
			MSG_MISC,
			USER_LEVEL_NOVICE,
			NULL,
			"~BRLOGOFF:~RS ~FT%04X~RS, %s %s\n",
			id,name ? name : "<no name>",desc);
		log(1,"User %04X (%s) %s.",id,name ? name : "<no name>",str);
		}
	}

FREE(pwd);
FREE(term_type);
FREE(mail_to_idstr);
if (mail) delete mail;
FREE(email_address);
if (batchfp) fclose(batchfp);
FREE(batch_name);

// Delete friends list 
for(frnd=first_friend;frnd;frnd=nfrnd) {
	nfrnd=frnd->next;  delete frnd;
	}

// Leave group
if (group) group->leave(this);

// Home group will not be set if we errored on construction
if (home_group) {
	if (!FLAGISSET(USER_FLAG_HOME_GRP_PERSIST)) delete home_group;
	else home_group->owner = NULL;
	}

local_user_count--;
}


//////////////////////////////// I/O FUNCTIONS ////////////////////////////////

/*** Write formatted string data to user ***/
void cl_local_user::uprintf(char *fmtstr,...)
{
char str[ARR_SIZE];
char str2[ARR_SIZE*2];
int i,j,k,do_prompt;
int len,pcesc,inl_prom_start;
va_list args;

if (sock == -1 ||
    FLAGISSET(USER_FLAG_LINKDEAD) || FLAGISSET(USER_FLAG_TEMP_OBJECT)) return;

va_start(args,fmtstr);
vsnprintf(str,ARR_SIZE,fmtstr,args);
va_end(args);

pcesc = 0;
inl_prom_start = -1;
do_prompt = 0;

// Add \r to any \n and remove and convert colour codes 
for(i=0,j=0;j < ARR_SIZE && str[i];++i) {
	switch(str[i]) {
		case '\n': 
		if (pcesc) str2[j++]='/';

		// Reset any colour at the end of a line
		if (FLAGISSET(USER_FLAG_ANSI_TERM)) {
			len = strlen(ansicode[0]);
			memcpy(str2+j,ansicode[0],len);
			j += len;
			}	
		str2[j++] = '\r';
		break;


		case '/':
		if (pcesc) str2[j++]='/'; else pcesc=1;
		continue;


		case '~':
		// If pcesc we leave in everything if ~XX not colour
		// command, else we leave out / but leave in colour command
		if (ARR_SIZE - i < 3) {
			if (pcesc) str2[j++]='/';
			break;
			}
		for(k=0;k < NUM_PRINT_CODES;++k) 
			if (!strncmp((char *)str+i+1,printcode[k],2)) break;

		if (k == NUM_PRINT_CODES) {
			// Not a valid print command
			if (pcesc) str2[j++]='/';
			break;
			}
		if (pcesc) break;

		// Exchange colour command for vt colour code
		i+=2;

		switch(k) {
			case PRINT_CODE_IP:
			inl_prom_start = j;  continue;

			case PRINT_CODE_NP:
			// Delete inline prompt and any text user was typing
			inline_prompt[0] = '\0';
			tbpos = 0;
			continue;

			case PRINT_CODE_PR:
			do_prompt = 1;  continue;

			default:
			if (FLAGISSET(USER_FLAG_ANSI_TERM)) {
				len = strlen(ansicode[k]);
				memcpy(str2+j,ansicode[k],len);
				j += len;
				}
			}
		continue;


		default:
		if (pcesc) str2[j++]='/';
		}
	str2[j++] = str[i];
	pcesc=0;
	}
str2[j] = '\0';

// If the user was halfway through writing something then overwrite that
// and reprint their input on the line below unless they've written more
// than one line in which case don't overwrite but put message below.
if (tbpos) {
	if (FLAGISSET(USER_FLAG_ANSI_TERM)) {
		// Scroll up however many lines user has typed in so far
		j=tbpos / (int)term_cols;
		for(i=0;i < j;++i) uwritestr("\033M");

		// Clear screen from current line down. cannot use a NUTS code
		// for this with uprintf as we'll get recursion.
		uwritestr("\r\033[J");

		// Do print
		uwritestr(str2);
		}
	else {
		// If not ansi term just go to next line
		uwritestr("\r\n");
		uwritestr(str2);
		}

	// Write any last print on same line then write current input. If
	// we're in a password stage just printf stars
	uwritestr(inline_prompt);

	switch(stage) {
		case USER_STAGE_LOGIN_PWD:
		case USER_STAGE_LOGIN_NEW_PWD:
		case USER_STAGE_LOGIN_REENTER_PWD:
		case USER_STAGE_OLD_PWD:
		case USER_STAGE_NEW_PWD:
		case USER_STAGE_REENTER_PWD:
		case USER_STAGE_SUICIDE:
		for(i=0;i < tbpos;++i) uwrite("*",1);
		break;

		default:
		uwrite((char *)tbuff,tbpos);
		}
	}
else {
	if (inline_prompt[0] && FLAGISSET(USER_FLAG_PUIP)) uwritestr("\r\n");
	uwritestr(str2);
	uwritestr(inline_prompt);
	}

// Set inline prompt if required. It is requested by putting ~IP in the 
// print string. Reset in uread(). 
if (inl_prom_start != -1) {
	if (j - inl_prom_start >= INLINE_PROMPT_SIZE) 
		inl_prom_start = j - INLINE_PROMPT_SIZE + 1;
	strcpy(inline_prompt,str2 + inl_prom_start);
	}

/* If we haven't displayed an inline prompt and the we had a ~PR in the
   string then display prompt. This will cause a bit of recursion as ::prompt()
   calls uprintf but it doesn't matter. */
if (!inline_prompt[0] && do_prompt) prompt();
}




/*** Read the users input ***/
void cl_local_user::uread()
{
u_char str[ARR_SIZE];
int i,len,remain,hsec;

if (!(len=read(sock,(char *)str,ARR_SIZE-1))) {
	if (level == USER_LEVEL_LOGIN) throw USER_STAGE_DISCONNECT;

	// Suspend user session for when they log in again
	SETFLAG(USER_FLAG_LINKDEAD);
	linkdead_time=server_time;
	allprintf(
		MSG_MISC,
		USER_LEVEL_NOVICE,
		this,"~BYLINKDEAD:~RS ~FT%04X~RS: %s %s\n",id,name,desc);
	log(1,"User %04X (%s) has gone linkdead.",id,name);
	close(sock);
	sock = -1;
	return;
	}

// See if we've exceeded incoming data rate
if (level > USER_LEVEL_LOGIN && max_local_user_data_rate) {
	if ((hsec = get_hsec_time()) - rx_period_data_start >= 100) {
		rx_period_data = 0;
		rx_period_data_start = get_hsec_time();
		}

	if ((rx_period_data += len) > max_local_user_data_rate) {
		log(1,"ERROR: Max data rate exceeded by user %04X (%s) with %d bytes in 0.%02d seconds\n",id,name,rx_period_data,hsec - rx_period_data_start);
		uprintf("\n~BR*** Maximum data rate exceeded! ***\n\n");
		throw USER_STAGE_DISCONNECT;
		}
	}

str[len]='\0';

// Loop through input
for(i=0;i < len;++i) {
	if (str[i] == TELNET_IAC || bpos) {
		/* Deal with telnet commands. If a command was incomplete
		   it will have been stored in the buffer. I tried to do it
		   a neater way than copying buffers back and forth but I
		   always had irritating little bugs appear so to hell with
		   it, this method is clunky but it seems to work properly. */
		remain = len-i;

		// If its too long just dump the rest 
		if (remain + bpos > ARR_SIZE) remain = ARR_SIZE;

		memcpy(buff+bpos,str+i,remain);
		bpos += remain;

		// If incomplete telopt or nothing left then return 
		if (!parse_telopt() || !bpos) return;
		len=bpos;

		// Copy remains of buffer back into string, reset i & bpos 
		memcpy(str,buff,len);
		i=0;
		bpos=0;
		}

	// Don't progress any further if users client is still in the process
	// of connecting and doing telopt negotiations.
	if (stage == USER_STAGE_NEW) continue;

	// Time user last sent something to us
	last_input_time = server_time;
	UNSETFLAG(USER_FLAG_TIMEOUT_WARNING);

	// Deal with ordinary data 
	switch(str[i]) {
		case '\r':
		tbuff[tbpos]='\0';
		inline_prompt[0]='\0';
		tbpos=0; // Set to zero as tested in uprintf(). See code.
		uwritestr("\r\n");
		if (level == USER_LEVEL_LOGIN) {
			login();
			if (level == USER_LEVEL_LOGIN) prompt();
			}
		else {
			parse_line();
			// If we're running a batch and are not paging or in
			// the editor etc then continue
			if (batch_type != BATCH_TYPE_NOBATCH && 
			    !page_pos &&
			    com_page_line == -1 &&
			    !editor && stage == USER_STAGE_CMD_LINE)
				run_batch(batch_type);
			}
		break;

		case '\0':
		case '\n':
		break;

		case DEL1:
		case DEL2:
		if (tbpos > 0) {
			tbpos--;
			tbuff[tbpos] = '\0';
			uwritestr("\b \b");
			}
		break;

		default:
		tbuff[tbpos++]=str[i];
		switch(stage) {
			case USER_STAGE_LOGIN_PWD:
			case USER_STAGE_LOGIN_NEW_PWD:
			case USER_STAGE_LOGIN_REENTER_PWD:
			case USER_STAGE_OLD_PWD:
			case USER_STAGE_NEW_PWD:
			case USER_STAGE_REENTER_PWD:
			case USER_STAGE_SUICIDE:
			case USER_STAGE_AFK_LOCK:
			uwritestr("*");
			break;

			default:
			uwrite((char *)str+i,1);
			}
		}
	}
}




/*** Write a string to the socket ***/
int cl_local_user::uwritestr(char *str)
{
return str ? uwrite(str,strlen(str)) : OK;
}




/*** Write data direct to the socket ***/
int cl_local_user::uwrite(char *str, int len)
{
int r,prev_wlen,wlen;
char *name2;

if (sock == -1 || !len) return OK;

name2 = name ? name : (char *)"<unknown>";

wlen = prev_wlen = 0;
do {
	for(r=0;;++r) {
		switch((wlen=write(sock,(char *)(str+prev_wlen),len))) {
			case -1:
			// Catch fatal errors
			switch(errno) {
				case EBADF :
				case EINVAL:
				case ENXIO :
				case EFAULT:
				case EPIPE :
				log(1,"ERROR: User %04X (%s), socket %d in cl_local_user::uwrite(): write(): %s",id,name2,sock,strerror(errno));
				return ERR_WRITE;
				}
			// Fall through

			case 0:
			if (r == MAX_WRITE_RETRY) {
				log(1,"ERROR: User %04X (%s), socket %d in cl_local_user::uwrite(): Maximum retries.",id,name2,sock);
				return ERR_WRITE;
				}
			break;

			default: goto WROTE;
			}
		}
	WROTE:
	len -= wlen;
	prev_wlen = wlen;
	} while(len > 0);
return OK;
}




/*** Parse the telopt stuff by going through the buffer. Returns 1 if
     found complete telopt code else returns 0 ***/
int cl_local_user::parse_telopt()
{
int shift,ret;
char path[MAXPATHLEN];

while(bpos && buff[0] == TELNET_IAC) {
	if (bpos < 2) return 0;
	shift=0;

	switch(buff[1]) {
		case TELNET_SB:
		switch(buff[2]) {
			case TELNET_NAWS:
			if (!(shift = get_termsize())) return 0;
			break;

			case TELNET_TERM:
			if (!(shift = get_termtype())) return 0;
			break;

			default:
			warnprintf("Your client sent unexpected sub option %d\n",buff[2]);
			}
		break;

		case TELNET_WILL:
		if (bpos < 3) return 0;
		switch(buff[2]) {
			case TELNET_NAWS: break;

			case TELNET_TERM:
			sprintf(text,"%c%c%c%c%c%c",
				TELNET_IAC,TELNET_SB,TELNET_TERM,
				TELNET_SEND,TELNET_IAC,TELNET_SE);
			uwrite(text,6);
			break;
		
			default:
			warnprintf("Your client sent unexpected TELNET_WILL\n",this);
			}
		shift=3;
		break;

		case TELNET_WONT:	
		if (bpos < 3) return 0;
		switch(buff[2]) {
			case TELNET_NAWS:
			uprintf("Unable to get your terminal size, defaulting to %dx%d.\n",term_cols,term_rows);
			SETFLAG(USER_FLAG_TERMSIZE);
			break;

			case TELNET_TERM:
			uprintf("Unable to get your terminal type.\n");
			SETFLAG(USER_FLAG_TERMTYPE);
			term_type = strdup(UNRESOLVED_STR);
			break;
		
			default:
			warnprintf("Your client sent unexpected TELNET_WILL\n",this);
			}
		shift=3;
		break;

		case TELNET_DO:
		if (bpos < 3) return 0;
		switch(buff[2]) {
			case TELNET_SGA:
			SETFLAG(USER_FLAG_SGA);
			break;

			case TELNET_ECHO:
			SETFLAG(USER_FLAG_ECHO);
			break;

			default:
			warnprintf("Your client sent unexpected option %d for TELNET_DO\n",buff[2]);
			}	
		shift=3;
		break;

		case TELNET_DONT:
		if (bpos < 3) return 0;
		switch(buff[2]) {
			case TELNET_SGA:
			warnprintf("Your client does not support character mode. This will cause I/O\n         problems with your session.\n");
			SETFLAG(USER_FLAG_SGA);
			break;

			case TELNET_ECHO:
			warnprintf("Your client refused to switch off input echoing.\n");
			// Have to set or check at bottom will never be true
			SETFLAG(USER_FLAG_ECHO); 
			break;

			default:
			warnprintf("Your client sent unexpected option %d for TELNET_DONT\n",buff[2]);
			}
		shift=3;
		break;


		case TELNET_IAC:
		// Write 255 
		uwrite((char *)&buff[1],1);
		// Fall through 

		default:
		shift=2;
		}	

	bpos -= shift;
	memcpy(buff,buff+shift,bpos);
	}

if (!FLAGISSET(USER_FLAG_GOT_TELOPT_INFO) &&
    FLAGISSET(USER_FLAG_ECHO) && 
    FLAGISSET(USER_FLAG_SGA) && 
    FLAGISSET(USER_FLAG_TERMTYPE) &&
    FLAGISSET(USER_FLAG_TERMSIZE)) {
	// Got all telopt info so send prelogin screen and print initial prompt 
	sprintf(path,"%s/%s",ETC_DIR,PRELOGIN_SCREEN);
	if ((ret=page_file(path,0)) != OK) 
		errprintf("cannot page pre-login screen: %s\n\n",
			err_string[ret]);
	SETFLAG(USER_FLAG_GOT_TELOPT_INFO);
	stage = USER_STAGE_LOGIN_ID;
	prompt();
	}
return 1;
}




/*** Get the terminal size from the telopt sub command ***/
int cl_local_user::get_termsize()
{
int pos1,pos2,def;

if (bpos < 9) return 0;

term_cols = 0;
term_rows = 0;

// If one of the sizes involves 255 (TELNET_IAC) then 255 gets sent twice
// as per telnet spec. Numeric order is high byte , low byte.
pos1 = 3 + (buff[3] == TELNET_IAC);
pos2 = pos1 + 1 + (buff[pos1+1] == TELNET_IAC);
if (pos2 > bpos) return 0;
term_cols = (uint16_t)(buff[pos1] << 8) + buff[pos2];

pos1 = pos2 + 1 + (buff[pos2+1] == TELNET_IAC);
pos2 = pos1 + 1 + (buff[pos1+1] == TELNET_IAC);
term_rows = (uint16_t)(buff[pos1] << 8) + buff[pos2];

// Make sure we have a full command sequence. If not then use default.
if (pos2 > bpos - 3) return 0;
if (buff[pos2+1] != TELNET_IAC || buff[pos2+2] != TELNET_SE) {
	warnprintf("Your client sent a corrupt terminal size option!\n");
	term_cols = 0;
	term_rows = 0;
	}

// Will be zero if corrupt above or some dumb terminals will cause zeros.
def = (!term_cols || !term_rows);
if (!term_cols) term_cols = DEFAULT_TERM_COLS;
if (!term_rows) term_rows = DEFAULT_TERM_ROWS;

if (level == USER_LEVEL_LOGIN) {
	if (def) uprintf("Your terminal has been set to a default size of %dx%d\n",term_cols,term_rows);
	else
	uprintf("Terminal size: %dx%d\n",term_cols,term_rows);
	SETFLAG(USER_FLAG_TERMSIZE);
	}
else {
	/* We can receive this even when user is 
	   connected as telnet will send it when user
	   resizes xterm. */
	infoprintf("Terminal size now: %dx%d\n",
		term_cols,term_rows);
	if (server_to)
		server_to->send_termsize(this);
	}
return pos2 + 3;
}




/*** Get the terminal type from the telopt sub command. This code doesn't take
     into account the possibility of an IAC followed by an SE in the terminal
     name as its unlikely to happen and won't cause a crash even if it does,
     it'll just mess up the user session with some rubbish following after 
     the prompt. ***/
int cl_local_user::get_termtype()
{
int i,sh;

if (bpos < 6) return 0;
if (buff[3] != TELNET_IS) {
	warnprintf("Your client sent a corrupt terminal type option!\n");
	term_type = strdup(UNRESOLVED_STR);
	sh = 4;
        }
else {
	for(i=4;i < bpos;++i) {
		if (buff[i] == TELNET_IAC &&
		    i < bpos-1 && buff[i+1] == TELNET_SE) break;
		}
	if (i == bpos) return 0;

	// Term type could be null if user did 'export TERM=""' on command line.
	if (i == 4) term_type = strdup(UNRESOLVED_STR);
	else {
		buff[i] = '\0';
		term_type = strdup((char *)buff+4);
		}
	sh = i+2;
	}
// Unset ansi flag in case we've had more than a type option sent before. It
// shouldn't happen but...
SETFLAG(USER_FLAG_TERMTYPE);
UNSETFLAG(USER_FLAG_ANSI_TERM); 

// Set colour flag if user has compatable terminal. 
//  Start at 1 cos word[0] is config option name.
for(i=1;ansi_terms && i < ansi_terms->wcnt;++i) {
	if (!strcasecmp(
		term_type,ansi_terms->word[i])) {
		SETFLAG(USER_FLAG_ANSI_TERM);
		break;
		}
	}
uprintf("Terminal type: %s\n",term_type);

if (!FLAGISSET(USER_FLAG_ANSI_TERM))
	warnprintf("Your terminal is not recognised as ANSI code compatable.\n");

return sh;
}



/////////////////////////////// LOGIN FUNCTIONS ////////////////////////////////

/*** Do login steps ***/
void cl_local_user::login()
{
int len,i,ret;
char *inpstr;
char *login_com[]= {
	"new","quit","who","version"
	};
enum {
	SC_NEW, SC_QUIT, SC_WHO, SC_VERSION
	};

inpstr=trim((char *)tbuff);
len=strlen(inpstr);

switch(stage) {
	case USER_STAGE_LOGIN_ID:
	if (!len) return;

	for(i=0;i < 4;++i) if (!strcasecmp(login_com[i],inpstr)) break;

	switch(i) {
		case SC_NEW:
		if (!SYS_FLAGISSET(SYS_FLAG_ALLOW_NEW_ACCOUNTS) ||
		    (max_local_users && local_user_count > max_local_users)) {
			uprintf("\n~FYSorry, new accounts cannot be created at this time.\n");
			throw USER_STAGE_DISCONNECT;
			}
		if (!(id = generate_id())) {
			errprintf("Unable to generate a new id at this time.\n\n");
			throw USER_STAGE_DISCONNECT;
			}
		uprintf("\nYour new id is ~FT~OL%04X\n\n",id);
		if (!SYS_FLAGISSET(SYS_FLAG_SAVE_NOVICE_ACCOUNTS)) 
			warnprintf("Novice accounts are currently deleted at session end.\n\n");
		stage=USER_STAGE_LOGIN_NAME;
		SETFLAG(USER_FLAG_NEW_USER);
		return;

		case SC_QUIT:
		uprintf("\n\n~BY*** Login abandoned ***\n\n");
		throw USER_STAGE_DISCONNECT;

		case SC_WHO:
		if (SYS_FLAGISSET(SYS_FLAG_ALLOW_WHO_AT_LOGIN)) com_who();
		else uprintf("\n~FYSorry, current user listing is disabled.\n\n");
		return;

		case SC_VERSION:
		uprintf("\n");
		com_version();
		uprintf("\n");
		return;
		}

	i=0;
	if (!(id=idstr_to_id(inpstr)) || 
	    id < MIN_LOCAL_USER_ID || id > MAX_LOCAL_USER_ID) {
		uprintf("\n~FYInvalid id.\n\n");
		inc_attempts();
		return;
		}

	// Check if banned
	if (user_is_banned(id,0,0)) {
		uprintf("\n~OL~FRYou are banned from this server.\n\n");
		throw USER_STAGE_DISCONNECT;
		}

	switch((ret=load(USER_LOAD_1))) {
		case OK: break;

		case ERR_CANT_OPEN_FILE:
		uprintf("\n~FYNo such id.\n\n");
		inc_attempts();
		return;

		default:
		errprintf("Load 1 error: %s\n",err_string[ret]);
		throw USER_STAGE_DISCONNECT;
		}

	if (level < lockout_level) {
		uprintf("\n~FYSorry, the talker is locked out up to level ~FT%s~FY at the moment.\n",user_level[lockout_level]);
		level = USER_LEVEL_LOGIN;  // reset for destructor
		throw USER_STAGE_DISCONNECT;
		}

	if (level < max_user_ign_level &&
	    (max_local_users && local_user_count > max_local_users)) {
		uprintf("\n~FYSorry, the maximum user count has been reached.\n");
		level = USER_LEVEL_LOGIN;
		throw USER_STAGE_DISCONNECT;
		}

	level = USER_LEVEL_LOGIN;  // reset

	/* If password not set log user straight in. This is a safety measure
	   so if an admin forgets their password they can modify their config 
	   in vi, comment out the password field and then login and use the
	   passwd command to reset it. */
	if (!pwd || !pwd[0]) {
		warnprintf("No password set, logging straight in...\n\n");
		complete_login();
		return;
		}
	stage = USER_STAGE_LOGIN_PWD;
	return;


	case USER_STAGE_LOGIN_PWD:
	if (len) {
		if (strcmp(crypt_str(inpstr),pwd)) {
			uprintf("\n~FRIncorrect login.\n\n");
			inc_attempts();
			stage=USER_STAGE_LOGIN_ID;
			return;
			}
		complete_login();
		}
	return;


	case USER_STAGE_LOGIN_NAME:
	if (!len) return;
	if (len > max_name_len) {
		uprintf("\n~FYName is too long, maximum length is %d characters.\n\n",max_name_len);
		inc_errors();
		return;
		}
	if ((ret = set_name(trim(inpstr))) != OK) {
		uprintf("\n~FYCannot set name: %s\n\n",err_string[ret]);
		return;
		}
	stage=USER_STAGE_LOGIN_NEW_PWD;
	return;


	case USER_STAGE_LOGIN_NEW_PWD:
	if (len) {
		if (len < min_pwd_len) {
			uprintf("\n~FYPassword is too short, minimum is %d characters.\n\n",min_pwd_len);
			inc_errors();
			return;
			}
		pwd = strdup(crypt_str(inpstr));
		stage = USER_STAGE_LOGIN_REENTER_PWD;
		}
	return;


	case USER_STAGE_LOGIN_REENTER_PWD:
	if (len) {
		if (strcmp(crypt_str(inpstr),pwd)) {
			uprintf("\n~FYPasswords do not match\n\n");
			free(pwd);
			inc_errors();
			stage=USER_STAGE_LOGIN_NEW_PWD;
			return;
			}
		level = USER_LEVEL_NOVICE;
		uconnect();
		}
	}
}




/*** Check the number of login attempts a user has made and boot him off
     if too many ***/
void cl_local_user::inc_attempts()
{
id = 0;  // Reset so search routines don't find id entered

if (++login_attempts > 2) {
	uprintf("~BR*** Maximum login attempts ***\n\n");
	throw USER_STAGE_DISCONNECT;
	}
}




/*** Check if too many login errors ***/
void cl_local_user::inc_errors()
{
if (++login_errors > 4) {
	uprintf("~BR*** Maximum account creation errors ***\n\n");
	throw USER_STAGE_DISCONNECT;
	}
}




/*** Finish login for user already setup ***/
void cl_local_user::complete_login()
{
cl_user *u;
cl_local_user *lu;
int tmp,ret,disc;

/* Everything is ok , check if user is already logged on though and
   if they are keep the old object but just swap the sockets then
   delete the new one */
disc = 0;
FOR_ALL_USERS(u) {
	if (u != this && u->id == id) {
		uprintf("\n\nYou are already logged on, re-connecting to old session...\n\n");
		lu=(cl_local_user *)u;

		/* Give old session this socket so user is effectively
		   reconnected to old session then copy across new address
		   info & stuff then close this one */
		tmp=lu->sock;
		lu->sock = sock;
		sock = tmp;

		// Set addresses
		memcpy(&u->ip_addr,&ip_addr,sizeof(ip_addr));
		FREE(u->ipnumstr);
		free(u->ipnamestr);
		u->ipnumstr = strdup(ipnumstr);
		u->ipnamestr = strdup(ipnamestr);

		// New terminal might be different size
		if (lu->term_cols != term_cols ||
		    lu->term_rows != term_rows) {
			lu->term_cols = term_cols;
			lu->term_rows = term_rows;
			if (lu->server_to)
				lu->server_to->send_termsize(lu);
			}

		if (O_FLAGISSET(lu,USER_FLAG_LINKDEAD)) {
			O_UNSETFLAG(lu,USER_FLAG_LINKDEAD);
			allprintf(
				MSG_MISC,
				USER_LEVEL_NOVICE,
				u,
				"~BGRECONNECT:~RS ~FT%04X~RS: %s %s\n",
				id,u->name,u->desc);
			log(1,"User %04X (%s) has reconnected.\n",u->id,u->name);
			}
		else {
			uprintf("\n\n~BR*** Terminating this session ***\n\n");
			log(1,"User %04X (%s) connected from %s (%s) and switched sessions.\n",u->id,u->name,u->ipnumstr,u->ipnamestr);
			}

		lu->prompt();
		disc = 1;
		}

	// If anyone was trying to delete us before we logged on reset them
	if (u->del_user && u->del_user->id == id) u->reset_to_cmd_stage();
	}
if (disc) throw USER_STAGE_DISCONNECT;

// Load the remaining info then connect
if ((ret=load(USER_LOAD_2)) != OK) {
	errprintf("Load 2 error: %s\n",err_string[ret]);
	level = USER_LEVEL_LOGIN; // Set in load(). Reset for destructor
	throw USER_STAGE_DISCONNECT;
	}

uconnect();
}




/*** Do final connect for user by creating his group and doing various
     other things ***/
void cl_local_user::uconnect()
{
cl_friend *frnd;
cl_user *u;
cl_server *svr;
cl_group *grp;
st_user_ban *gb;
char path[MAXPATHLEN];
int ret,inv;

SETFLAG(USER_FLAG_NEW_LOGIN);
prev_flags = flags;

allprintf(
	MSG_MISC,
	USER_LEVEL_NOVICE,this,"~BGLOGON:~RS ~FT%04X~RS, %s %s\n",id,name,desc);

// Give login message
uprintf("\n\n~BB~FG*** Welcome to server: %s ***\n\n",server_name);

if (logoff_time)
	uprintf("~FTYou were last logged in from ~FY%s~FT on ~FM%s\n",
		prev_ip_addr,ctime(&logoff_time));

login_time=server_time;
stage=USER_STAGE_CMD_LINE;

// Create own group unless its persistent and already exists
if (!(home_group = get_group(id))) {
	home_group = new cl_group(id,this);

	// If any errors in loading group then create group with default
	// details. Group load will fail if we're a new user obviously.
	if (home_group->error != OK) {
		if (!FLAGISSET(USER_FLAG_NEW_USER))
			errprintf("1st attempt at home group creation failed: %s\n",
				err_string[home_group->error]);

		home_group->owner = NULL;  // Avoid any problems in destructor
		delete home_group;
		sprintf(text,"%s's group",name);
		home_group = new cl_group(id,text,GROUP_TYPE_USER,this);

		// This is fatal but should never happen anyway
		if (home_group->error != OK) {
			errprintf("2nd attempt at home group creation failed: %s\n",
				err_string[home_group->error]);
			throw USER_STAGE_DISCONNECT;
			}
		}
	}
else home_group->owner = this;

// If not set (eg if new user) then set to our home group
if (!start_group_id) start_group_id = id;

// If a new account has been created this test will only happen now. Has to
// be done at this point so group is created for save.
if (lockout_level > level) {
	uprintf("\n~FYAccount created but the talker is locked out up to level ~FT%s~FY at the\n~FYmoment. Try logging in later.\n",
		user_level[lockout_level]);
	throw USER_STAGE_DISCONNECT;
	}

// Join chosen start group if possible unless imprisoned
if (FLAGISSET(USER_FLAG_PRISONER)) {
	uprintf("\n~BR*** You are sent straight to prison! ***\n\n");
	prison_group->join(this);
	}
else {
	if (!(grp = get_group(start_group_id))) {
		warnprintf("Start group %04X does not exist.\n",start_group_id);
		home_group->join(this);
		}
	else
	// Don't check invite as we won't have any having just logged on
	if (!grp->user_can_join(this,&inv)) {
		warnprintf("You cannot join group %04X.\n",start_group_id);
		home_group->join(this);
		}
	else grp->join(this);
	}

// See if we're a friend of anyone and if so then announce our logon.
FOR_ALL_USERS(u) {
	if (u == this) continue;

	FOR_ALL_USERS_FRIENDS(u,frnd) {
		if (frnd->utype == USER_TYPE_LOCAL && frnd->id == id) {
			u->infoprintf("Your friend ~FT%04X~RS has logged on.\n",id);
			frnd->stage = FRIEND_ONLINE;	
			frnd->local_user = this;
			}
		}
	}

// Send logon notify to remote servers
FOR_ALL_SERVERS(svr) {
	if (svr->stage == SERVER_STAGE_CONNECTED) {
		if ((ret=svr->send_logon_notify(id)) != OK)
			log(1,"ERRROR: cl_local_user::uconnect() -> cl_server::send_logon_notify(): %s",err_string[ret]);
		}
	}

// See if we're on any group ban lists and if so set the user pointer. This
// is reset in ~cl_user()
FOR_ALL_GROUPS(grp) {
	for(gb=grp->first_ban;gb;gb=gb->next) {
		if (gb->utype == USER_TYPE_LOCAL && gb->uid == id) {
			gb->user = this;  break;
			}
		}
	}

// If user is new save his account. Do this before we create mail object or
// it will fail.
if (FLAGISSET(USER_FLAG_NEW_USER) && (ret=save()) != OK) 
	errprintf("Cannot save your details: %s\n", err_string[ret]);

// Create mail object
mail = new cl_mail(this,id);
if (mail->error != OK) {
	log(1,"ERROR: cl_local_user::uconnect(): Mail object creation failed.\n");
	errprintf("Cannot create mail object: %s\n",err_string[mail->error]);
	throw USER_STAGE_DISCONNECT;
	}

// Page post login screen
sprintf(path,"%s/%s",ETC_DIR,POSTLOGIN_SCREEN);
if ((ret=page_file(path,1)) != OK) 
	errprintf("Cannot page post-login screen: %s\n\n",err_string[ret]);

// If paging done show mail 
if (!page_pos) do_login_messages();
prompt();

// Log it
if (FLAGISSET(USER_FLAG_NEW_USER))
	log(1,"NEW USER: %04X (%s) connected from %s (%s).\n",
		id,name,ipnumstr,ipnamestr);
else
log(1,"User %04X (%s) connected from %s (%s).\n",
	id,name,ipnumstr,ipnamestr);

// Start to run any login batch commands
run_batch(BATCH_TYPE_LOGIN);
}





/*** Disconnect a user ***/
void cl_local_user::disconnect()
{
// If we're on a remote server return to our home group first in case
// we have a batch to run.
if (server_to) home_group->join(this);

/* If user invokes mailer or board reader in interactive mode it'll
   immediately be exited. Also don't run if we've recursed or we're
   not fully logged in */
if (!batchfp && level > USER_LEVEL_LOGIN) run_batch(BATCH_TYPE_LOGOUT);
delete this;
}




/*** Do some connect stuff after the post login file was paged ***/
void cl_local_user::do_login_messages()
{
UNSETFLAG(USER_FLAG_NEW_LOGIN);

look(group,0);

// Do mail info
if (!mail->msgcnt) uprintf("You have no mail.\n\n");
else
if (!mail->unread_msgcnt) 
	uprintf("You have ~FM%d~RS mails, they have all been read.\n\n",
		mail->msgcnt);
else
uprintf("~SNYou have ~FM%d~RS mails, ~OL~LI~FT%d~RS are unread.\n\n",
	mail->msgcnt,mail->unread_msgcnt);
}



///////////////////////////// LOAD & SAVE FUNCTIONS ////////////////////////////

/*** Load the users data ***/
int cl_local_user::load(int load_stage)
{
cl_splitline sl(1);
char path[MAXPATHLEN];
char line[ARR_SIZE];
FILE *fp;
int i,ret,secs,err;

char *tmstr1="Too many values with option '%s' on config file line %d.\n\n";
char *tmstr2="WARNING: UID %04X: Too many values with option '%s' on config file line %d.";

char *config_option[]={
	"password",
	"level",
	"name",
	"desc",
	"prompt",

	"paging",
	"group persist",
	"muzzled",
	"muzzle level",
	"muzzle time",

	"muzzle remaining",
	"prisoner",
	"imprison level",
	"imprisoned time",
	"sentence remaining",

	"friends",
	"logoff time",
	"session duration",
	"email address",
	"start group",

	"converse",
	"puip",
	"recv net bcast",
	"autosilence",
	"prev ip addr"
	};
enum {
	OPT_PWD,
	OPT_LEVEL,
	OPT_NAME,
	OPT_DESC,
	OPT_PROMPT,

	OPT_PAGING,
	OPT_HOME_GRP_PERSIST,
	OPT_MUZZLED,
	OPT_MUZZLE_LEVEL,
	OPT_MUZZLE_TIME,

	OPT_MUZZLE_REMAINING,
	OPT_PRISONER,
	OPT_IMPRISON_LEVEL,
	OPT_IMPRISONED_TIME,
	OPT_SENTENCE_REMAINING,

	OPT_FRIENDS,
	OPT_LOGOFF_TIME,
	OPT_SESSION_DURATION,
	OPT_EMAIL,
	OPT_START_GROUP,

	OPT_CONVERSE,
	OPT_PUIP,
	OPT_RECV_NET_BCAST,
	OPT_AUTOSILENCE,
	OPT_PREV_IP_ADDR,

	OPT_END
	};
int flagslist[] = {
	0,
	0,
	0,
	0,
	USER_FLAG_PROMPT,

	USER_FLAG_PAGING,
	USER_FLAG_HOME_GRP_PERSIST,
	USER_FLAG_MUZZLED,
	0,
	0,

	0,	
	USER_FLAG_PRISONER,
	0,
	0,
	0,

	0,
	0,
	0,
	0,
	0,

	USER_FLAG_CONVERSE,
	USER_FLAG_PUIP,
	USER_FLAG_RECV_NET_BCAST,
	USER_FLAG_AUTOSILENCE,
	0
	};
	
sprintf(path,"%s/%04X/%s",USER_DIR,id,USER_CONFIG_FILE);
if (!(fp=fopen(path,"r"))) return ERR_CANT_OPEN_FILE;

err = 0;
linenum=1;
fgets(line,ARR_SIZE-1,fp);

while(!feof(fp) && !(err = ferror(fp))) {
	if ((ret=sl.parse(line)) != OK) {
		fclose(fp);
		errprintf("%s on config file line %d.\n\n",
			err_string[ret],linenum);
		log(1,"ERROR: UID %04X: %s on config file line %d.",id,err_string[ret],linenum);
		return ERR_CONFIG;
		}
	if (!sl.wcnt) goto NEXT_LINE;

	for(i=0;i < OPT_END;++i) 
		if (!strcmp(sl.word[0],config_option[i])) break;

	// If its the 1st stage load we only need the password and level
	if (load_stage == USER_LOAD_1) {
		switch(i) {
			case OPT_PWD:
			if (sl.wcnt > 2) {
				warnprintf(tmstr1,sl.word[0],linenum);
				log(1,tmstr2,id,sl.word[0],linenum);
				}
			FREE(pwd);
			pwd=strdup(sl.word[1]);
			break;

			case OPT_LEVEL:
			if (sl.wcnt > 2) {
				warnprintf(tmstr1,sl.word[0],linenum);
				log(1,tmstr2,id,sl.word[0],linenum);
				}
			if ((level = get_level(sl.word[1])) == -1 ||
			    level == USER_LEVEL_LOGIN) goto INVALID;
			break;
			}
		}
	else {
		// 2nd stage load after user has entered correct password
		switch(i) {
			// Loaded in stage 1 , ignore unless we're a temp obj
			case OPT_PWD:
			if (FLAGISSET(USER_FLAG_TEMP_OBJECT)) {
				if (sl.wcnt > 2) {
					warnprintf(tmstr1,sl.word[0],linenum);
					log(1,tmstr2,id,sl.word[0],linenum);
					}
				FREE(pwd);
				pwd=strdup(sl.word[1]);
				}
			break;


			case OPT_LEVEL:
			if (sl.wcnt > 2) {
				warnprintf(tmstr1,sl.word[0],linenum);
				log(1,tmstr2,id,sl.word[0],linenum);
				}
			if ((level = get_level(sl.word[1])) == -1 ||
			     level == USER_LEVEL_LOGIN)
				goto INVALID;
			break;


			case OPT_NAME:
			if (sl.wcnt > 2) {
				warnprintf(tmstr1,sl.word[0],linenum);
				log(1,tmstr2,id,sl.word[0],linenum);
				}
			if ((ret = set_name(sl.word[1])) != OK) 
				goto INVALID;
			break;


			case OPT_DESC:
			if (sl.wcnt > 2) {
				warnprintf(tmstr1,sl.word[0],linenum);
				log(1,tmstr2,id,sl.word[0],linenum);
				}
			set_desc(sl.word[1]);
			break;


			case OPT_PROMPT:
			case OPT_PAGING:
			case OPT_HOME_GRP_PERSIST:
			case OPT_MUZZLED:
			case OPT_PRISONER:
			case OPT_CONVERSE:
			case OPT_PUIP:
			case OPT_RECV_NET_BCAST:
			case OPT_AUTOSILENCE:
			// These options all set flags
			if (sl.wcnt > 2) {
				warnprintf(tmstr1,sl.word[0],linenum);
				log(1,tmstr2,id,sl.word[0],linenum);
				}

			// Set flags
			if (!strcasecmp(sl.word[1],"NO")) 
				UNSETFLAG(flagslist[i]);
			else
			if (!strcasecmp(sl.word[1],"YES")) {
				SETFLAG(flagslist[i]);
				}
			else goto INVALID;
			break;


			case OPT_MUZZLE_LEVEL:
			if (sl.wcnt > 2) {
				warnprintf(tmstr1,sl.word[0],linenum);
				log(1,tmstr2,id,sl.word[0],linenum);
				}
			if ((muzzle_level=get_level(sl.word[1]))== -1 ||
			     muzzle_level == USER_LEVEL_LOGIN)
				goto INVALID;
			break;


			case OPT_MUZZLE_TIME:
			if (sl.wcnt > 2) {
				warnprintf(tmstr1,sl.word[0],linenum);
				log(1,tmstr2,id,sl.word[0],linenum);
				}
			if (!is_integer(sl.word[1])) goto INVALID;
			muzzle_start_time = atoi(sl.word[1]);
			break;


			case OPT_MUZZLE_REMAINING:
			if (sl.wcnt > 2) {
				warnprintf(tmstr1,sl.word[0],linenum);
				log(1,tmstr2,id,sl.word[0],linenum);
				}
			if (!is_integer(sl.word[1])) goto INVALID;
			if ((secs = atoi(sl.word[1])))
				muzzle_end_time = server_time + secs;
			break;


			case OPT_IMPRISON_LEVEL:
			if (sl.wcnt > 2) {
				warnprintf(tmstr1,sl.word[0],linenum);
				log(1,tmstr2,id,sl.word[0],linenum);
				}
			if ((imprison_level=get_level(sl.word[1]))== -1 ||
			     imprison_level == USER_LEVEL_LOGIN)
				goto INVALID;
			break;


			case OPT_IMPRISONED_TIME:
			if (sl.wcnt > 2) {
				warnprintf(tmstr1,sl.word[0],linenum);
				log(1,tmstr2,id,sl.word[0],linenum);
				}
			if (!is_integer(sl.word[1])) goto INVALID;
			imprisoned_time = atoi(sl.word[1]);
			break;


			case OPT_FRIENDS:
			for(i=1;i < sl.wcnt;++i) {
				if ((ret=add_friend(sl.word[i],linenum)) != OK)
					errprintf("Unable to add friend %s on line %d: %s\n",sl.word[i],linenum,err_string[ret]);
				}
			break;


			case OPT_LOGOFF_TIME:
			if (sl.wcnt > 2) {
				warnprintf(tmstr1,sl.word[0],linenum);
				log(1,tmstr2,id,sl.word[0],linenum);
				}
			if (!is_integer(sl.word[1])) goto INVALID;
			logoff_time = atoi(sl.word[1]);
			break;


			case OPT_SESSION_DURATION:
			if (sl.wcnt > 2) {
				warnprintf(tmstr1,sl.word[0],linenum);
				log(1,tmstr2,id,sl.word[0],linenum);
				}
			if (!is_integer(sl.word[1])) goto INVALID;
			session_duration = atoi(sl.word[1]);
			break;


			case OPT_EMAIL:
			if (sl.wcnt > 2) {
				warnprintf(tmstr1,sl.word[0],linenum);
				log(1,tmstr2,id,sl.word[0],linenum);
				}
			email_address = strdup(sl.word[1]);
			break;


			case OPT_START_GROUP:
			if (sl.wcnt > 2) {
				warnprintf(tmstr1,sl.word[0],linenum);
				log(1,tmstr2,id,sl.word[0],linenum);
				}
			if (!(start_group_id = idstr_to_id(sl.word[1]))) 	
				goto INVALID;
			break;


			case OPT_SENTENCE_REMAINING:
			if (sl.wcnt > 2) {
				warnprintf(tmstr1,sl.word[0],linenum);
				log(1,tmstr2,id,sl.word[0],linenum);
				}
			if (!is_integer(sl.word[1])) goto INVALID;
			if ((secs = atoi(sl.word[1])))
				release_time = server_time + secs;
			break;


			case OPT_PREV_IP_ADDR:
			if (sl.wcnt > 2) {
				warnprintf(tmstr1,sl.word[0],linenum);
				log(1,tmstr2,id,sl.word[0],linenum);
				}
			FREE(prev_ip_addr);
			prev_ip_addr = strdup(sl.word[1]);
			break;
		
			default:
			warnprintf("Unknown option '%s' on config file line %d.\n\n",sl.word[0],linenum);
			log(1,"WARNING: UID %04X: Unknown option '%s' on config file line %d.",id,sl.word[0],linenum);
			}
		}

	NEXT_LINE:
	fgets(line,ARR_SIZE-1,fp);
	linenum++;
	}
fclose(fp);
if (err) {
	log(1,"ERROR: UID %04X: Read failure with config file: %s\n",strerror(err));
	return ERR_CONFIG;
	}

// Silently unset recv net bcast if level too low
if (FLAGISSET(USER_FLAG_RECV_NET_BCAST) && level < recv_net_bcast_level)
	UNSETFLAG(USER_FLAG_RECV_NET_BCAST);

if (load_stage == USER_LOAD_1) return OK;

return (!name || !strlen(name)) ? ERR_NAME_NOT_SET : OK;

// Invalid value for a config option
INVALID:
errprintf("Invalid value for '%s' option on config file line %d.\n\n",
	sl.word[0],linenum);
log(1,"ERROR: UID %04X: Invalid value for '%s' option on config file line %d.",
	id,sl.word[0],linenum);	
return ERR_INVALID_VALUE;
}




/*** Save the users data ***/
int cl_local_user::save()
{
cl_friend *frnd;
struct stat fs;
FILE *fp;
char dirname[MAXPATHLEN];
char fstr[ARR_SIZE];
char path[MAXPATHLEN];
char path2[MAXPATHLEN];
int cnt;

if (level == USER_LEVEL_LOGIN) return OK;

// See if user directory exists , if not then create if
sprintf(dirname,"%s/%04X",USER_DIR,id);
if (stat(dirname,&fs) && mkdir(dirname,0700)) return ERR_CANT_CREATE_DIR;

// Open temp file
sprintf(path,"%s/%04X/config.tmp",USER_DIR,id);
if (!(fp=fopen(path,"w"))) return ERR_CANT_OPEN_FILE;

// Save info. Password & level are required in stage 1 load so put them
// first to reduce file scan
fprintf(fp,"password = \"%s\"\n",pwd ? pwd : "");
fprintf(fp,"level = %s\n",user_level[level]);
fprintf(fp,"name = \"%s\"\n",name);
fprintf(fp,"desc = \"%s\"\n",desc);
fprintf(fp,"prompt = %s\n",noyes[FLAGISSET(USER_FLAG_PROMPT)]);
fprintf(fp,"paging = %s\n",noyes[FLAGISSET(USER_FLAG_PAGING)]);
fprintf(fp,"\"group persist\" = %s\n",noyes[FLAGISSET(USER_FLAG_HOME_GRP_PERSIST)]);
fprintf(fp,"muzzled = %s\n",noyes[FLAGISSET(USER_FLAG_MUZZLED)]);
fprintf(fp,"\"muzzle level\" = %s\n",user_level[muzzle_level]);
fprintf(fp,"\"muzzle time\" = %u\n",(uint)muzzle_start_time);
if (muzzle_end_time) 
	fprintf(fp,"\"muzzle remaining\" = %u\n",
		(uint)(muzzle_end_time - server_time));
else
	fprintf(fp,"\"muzzle remaining\" = 0\n");

fprintf(fp,"prisoner = %s\n",noyes[FLAGISSET(USER_FLAG_PRISONER)]);
fprintf(fp,"\"imprison level\" = %s\n",user_level[imprison_level]);
fprintf(fp,"\"imprisoned time\" = %u\n",(u_int)imprisoned_time);
if (release_time)
	fprintf(fp,"\"sentence remaining\" = %u\n",
		(uint)(release_time - server_time));
else
	fprintf(fp,"\"sentence remaining\" = 0\n");

fprintf(fp,"\"logoff time\" = %u\n",(uint32_t)server_time);
fprintf(fp,"\"session duration\" = %u\n",(uint32_t)(server_time - login_time));
fprintf(fp,"\"start group\" = %04X\n",start_group_id);
fprintf(fp,"converse = %s\n",noyes[FLAGISSET(USER_FLAG_CONVERSE)]);
fprintf(fp,"puip = %s\n",noyes[FLAGISSET(USER_FLAG_PUIP)]);
fprintf(fp,"\"recv net bcast\" = %s\n",
	noyes[FLAGISSET(USER_FLAG_RECV_NET_BCAST)]);
fprintf(fp,"autosilence = %s\n",noyes[FLAGISSET(USER_FLAG_AUTOSILENCE)]);
if (FLAGISSET(USER_FLAG_TEMP_OBJECT))
	fprintf(fp,"\"prev ip addr\" = \"%s\"\n",prev_ip_addr);
else
	fprintf(fp,"\"prev ip addr\" = \"%s (%s)\"\n",ipnumstr,ipnamestr);
if (email_address) fprintf(fp,"\"email address\" = \"%s\"\n",email_address);

cnt=0;
FOR_ALL_FRIENDS(frnd) {
	if (frnd->svr_name) 
		sprintf(fstr,"%04X@%s",frnd->id,frnd->svr_name);
	else sprintf(fstr,"%04X",frnd->id);

	// 5 friends on each line
	if (!(cnt++ % 5)) fprintf(fp,"\nfriends = %s",fstr);
	else fprintf(fp,",%s",fstr);
	}
fputs("\n",fp);
fclose(fp);

// Rename temp file to config file
sprintf(path2,"%s/%04X/%s",USER_DIR,id,USER_CONFIG_FILE);
if (rename(path,path2)) {
	unlink(path);  return ERR_CANT_RENAME_FILE;
	}

// Save group info and return. Will be NULL if temp object
return (FLAGISSET(USER_FLAG_TEMP_OBJECT) ? OK : home_group->save());
}




/** Save a text file such :  profile, login or logout batch ***/
int cl_local_user::save_text_file(int ftype, char *str)
{
FILE *fp;
char path[MAXPATHLEN];
int ret;

// Put a newline on the end of batches simply so they lok neater when
// listing them in the lsbatches command
switch(ftype) {
	case EDITOR_TYPE_PROFILE:
	sprintf(path,"%s/%04X/%s",USER_DIR,id,USER_PROFILE_FILE);
	break;

	case EDITOR_TYPE_LOGIN_BATCH:
	sprintf(path,"%s/%04X/%slogin",USER_DIR,id,BATCH_PRE);
	break;

	case EDITOR_TYPE_LOGOUT_BATCH:
	sprintf(path,"%s/%04X/%slogout",USER_DIR,id,BATCH_PRE);
	break;

	case EDITOR_TYPE_SESSION_BATCH:
	sprintf(path,"%s/%04X/%s%s",USER_DIR,id,BATCH_PRE,com_filename);
	FREE(com_filename);
	break;

	default: return ERR_INVALID_TYPE;
	}
if (!(fp=fopen(path,"w"))) return ERR_CANT_OPEN_FILE;
ret=fputs(str,fp);
fclose(fp);
return (ret == EOF ? ERR_WRITE : OK);
}



/////////////////////////////// MAIL FUNCTIONS ////////////////////////////////


/*** Function that runs the mail enviroment ***/
void cl_local_user::run_mailer()
{
cl_msginfo *minfo,*mnext;
char *mcom[] = { 
	"read",
	"write",
	"reply",
	"forward",
	"delete",
	"list",
	"info",
	"renumber",
	"help",
	"quit"
	};
enum mail_coms {
	MCOM_READ,
	MCOM_WRITE,
	MCOM_REPLY,
	MCOM_FORWARD,
	MCOM_DELETE,
	MCOM_LIST,
	MCOM_INFO,
	MCOM_RENUM,
	MCOM_HELP,
	MCOM_QUIT,

	MCOM_END
	};
uint16_t uid;
cl_server *svr;
char subjline[260]; // 255+5
char path[MAXPATHLEN];
int comnum,i,ret,len,inc,cnt;

// Do different things depending on what mailer stage we're in.
switch(stage) {
	case USER_STAGE_MAILER: break;

	case USER_STAGE_MAILER_SUBJECT1:
	case USER_STAGE_MAILER_SUBJECT2:
	if (!com->wcnt) {
		uprintf("\n<No subject>\n");
		send_mail(mail_to_idstr,NULL,1,NULL);
		}
	else {
		if ((int)strlen(com->wordptr[0]) > max_subject_len) {
			uprintf("\nSubject is too long, maximum is %d characters.\n",max_subject_len);
			return;
			}
		send_mail(mail_to_idstr,com->wordptr[0],1,NULL);
		}
	FREE(mail_to_idstr);
	stage = (stage == USER_STAGE_MAILER_SUBJECT1 ? 
		USER_STAGE_CMD_LINE : USER_STAGE_MAILER);
	return;


	case USER_STAGE_MAILER_DEL:
	if (!com->wcnt) return;
	switch(toupper(com->word[0][0])) {
		case 'N':
		stage = USER_STAGE_MAILER;  return;

		case 'Y':
		for(minfo=mail->first_msg,cnt=0;minfo;minfo=mnext) {
			mnext = minfo->next;
			i = minfo->mnum;
			if (current_msg && (i < current_msg || i > to_msg))
				continue;

			if ((ret=mail->mdelete(minfo)) != OK) {
				errprintf("Error during deletion of message %d: %s\n",i,err_string[ret]);
				stage = USER_STAGE_MAILER;
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
			}
		else uprintf("\nNo messages deleted.\n");

		stage = USER_STAGE_MAILER;
		}
	return;


	case USER_STAGE_MAILER_READ_FROM:
	clear_inline_prompt();

	// Check for quit
	if (com->wcnt && toupper(com->word[0][0]) == PAGE_QUIT_KEY) {
		stage = USER_STAGE_MAILER;  return;
		}

	// Get next message to read
	current_msg++;
	for(minfo = mail->first_msg;minfo;minfo=minfo->next)
		if (minfo->mnum >= current_msg) break;
	if (minfo) {
		uprintf("~FT------------------------------------------------------------------------------\n");
		if ((ret = mail->mread(minfo)) != OK) 
			errprintf("Cannot display message: %s\n\n",
				err_string[ret]); 

		// If this is the last message reset stage
		if (!minfo->next) stage = USER_STAGE_MAILER;
		}
	else stage = USER_STAGE_MAILER;
	return;


	default:
	log(1,"INTERNAL ERROR: User %04X in invalid stage %d in cl_local_user::run_mailer()!\n",id,stage);
	uprintf("~OL~FRINTERNAL ERROR:~RS Invalid stage in cl_local_user::run_mailer()!\n");
	stage = USER_STAGE_MAILER;
	return;
	}


// If paging mail list check for quit key
if (com_page_line != -1) {
	if (toupper(tbuff[0]) == PAGE_QUIT_KEY) {
		com_page_line = -1;  return;
		}
	clear_inline_prompt();
	mail->list();
	return;
	}

if (!com->wcnt) return;

// Get command
len = strlen(com->word[0]);
for(comnum=0;comnum < MCOM_END;++comnum) 
	if (!strncasecmp(com->word[0],mcom[comnum],len)) break;
if (comnum == MCOM_END) {
	uprintf("Unknown mailer command.\n");  return;
	}

// Do action
switch(comnum) {
	case MCOM_READ:
	switch(com->wcnt) {
		case 1:
		if (!mail->first_msg) {
			uprintf("You have no mail.\n");  return;
			}
		minfo = mail->first_msg;
		current_msg = minfo->mnum;
		stage = USER_STAGE_MAILER_READ_FROM;
		break;

		case 2:
		if (!strncasecmp(com->word[1],"from",strlen(com->word[1]))) 
			goto USAGE;
		if (!is_integer(com->word[1])) {
			uprintf("Invalid message number.\n");  return;
			}
		if (!(minfo = get_message(
			mail->first_msg,atoi(com->word[1])))) {
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
				mail->first_msg,current_msg))) {
				uprintf("No such start message.\n");
				return;
				}
			stage = USER_STAGE_MAILER_READ_FROM;
			break;
			}
		// Fall through

		default:
		USAGE:
		uprintf("Usage: read [from] <msg number>\n");
		return;
		}
	if ((ret = mail->mread(minfo)) != OK) 
		errprintf("Cannot display message: %s\n\n",err_string[ret]); 
	return;


	case MCOM_WRITE:
	if (FLAGISSET(USER_FLAG_MUZZLED)) {
		uprintf("You cannot send a message while you are muzzled.\n");
		return;
		}
	if (com->wcnt < 2) {
		uprintf("Usage: write <user id[@<server>]> [<text>]\n");
		return;
		}
	if (com->wcnt == 2) {
		// Check address is ok
		if ((ret=idstr_to_id_and_svr(com->word[1],&uid,&svr)) != OK) {
			errprintf("Invalid address: %s\n",err_string[ret]);
			return;
			}
		stage = USER_STAGE_MAILER_SUBJECT2;
		mail_to_idstr = strdup(com->word[1]); // Free'd at top
		}
	else send_mail(com->word[1],NULL,0,NULL);
	return;


	case MCOM_REPLY:
	if (FLAGISSET(USER_FLAG_MUZZLED)) {
		uprintf("You cannot send a message while you are muzzled.\n");
		return;
		}
	switch(com->wcnt) {
		case 2: inc = 0;  break;
		case 3:
		if (!strncasecmp(com->word[2],"include",strlen(com->word[2]))) {
			inc = 1;  break;
			}
		// Fall through

		default:
		uprintf("Usage: reply <msg number> [include]\n");  return;
		}

	if (!is_integer(com->word[1])) {
		uprintf("Invalid message number.\n");  return;
		}
	if (!(minfo = get_message(mail->first_msg,atoi(com->word[1])))) {
		uprintf(NO_SUCH_MESSAGE);  return;
		}
	// Put Re: into reply subject if its not there already
	if (!strncmp(minfo->subject,"Re: ",4))
		strcpy(subjline,minfo->subject);
	else sprintf(subjline,"Re: %s",minfo->subject);

	uprintf("\n~FYReply to:~RS %s, %s  (#%d)\n",
		minfo->id,minfo->name,minfo->mnum);
	uprintf("~FYSubject :~RS %s\n",subjline);

	if (inc) {
		sprintf(path,"%s/%04X/%s",USER_DIR,id,minfo->filename);
		send_mail(minfo->id,subjline,1,path);
		}
	else send_mail(minfo->id,subjline,1,NULL);
	return;


	case MCOM_FORWARD:
	if (com->wcnt != 3) {
		uprintf("Usage: forward <msg number> <user id[@<server>]>\n");
		return;
		}
	if (!is_integer(com->word[1])) {
		uprintf("Invalid message number.\n");  return;
		}
	if (!(minfo = get_message(mail->first_msg,atoi(com->word[1])))) {
		uprintf(NO_SUCH_MESSAGE);  return;
		}

	// Put Fw: into subject if its not there already
	if (!strncmp(minfo->subject,"Fw: ",4))
		strcpy(subjline,minfo->subject);
	else sprintf(subjline,"Fw: %s",minfo->subject);

	uprintf("\n~FYForward to:~RS %s\n",com->word[2]);
	uprintf("~FYSubject   : %s\n",subjline);

	sprintf(path,"%s/%04X/%s",USER_DIR,id,minfo->filename);
	send_mail(com->word[2],subjline,1,path);
	return;


	case MCOM_DELETE:
	if (!mail->first_msg) {
		uprintf("You have no messages to delete.\n");  return;
		}
	switch(com->wcnt) {
		case 2:
		if (!strcasecmp(com->word[1],"all")) {
			current_msg = 0;
			stage = USER_STAGE_MAILER_DEL;
			return;
			}
		break;

		case 4:
		if (strcasecmp(com->word[2],"to")) goto USAGE2;
		if (!is_integer(com->word[1]) ||
		    !(current_msg = atoi(com->word[1])) ||
		    !is_integer(com->word[3]) ||
		    !(to_msg = atoi(com->word[3]))) {
			uprintf("Invalid message number(s).\n");  return;
			}
		if (current_msg > to_msg) {
			uprintf("The second message number must be greater than the first.\n");
			return;
			}
		if (!get_message(mail->first_msg,current_msg) ||
		    !get_message(mail->first_msg,to_msg)) {
			uprintf("No such message(s).\n");  return;
			}
		stage = USER_STAGE_MAILER_DEL;
		return;

		default:
		USAGE2:
		uprintf("Usage: delete <msg number> [to <msg number>]\n              all\n");
		return;
		}

	// Delete a specific message
	if (!is_integer(com->word[1]) || !(current_msg = atoi(com->word[1]))) {
		uprintf("Invalid message number.\n");  return;
		}
	if (!(minfo = get_message(mail->first_msg,current_msg))) {
		uprintf(NO_SUCH_MESSAGE);  return;
		}
	to_msg = current_msg;
	stage = USER_STAGE_MAILER_DEL;
	return;


	case MCOM_LIST:
	if (com->wcnt != 1) {
		uprintf(COM_NO_ARGS);  return;
		}
	mail->list();
	return;


	case MCOM_INFO:
	if (com->wcnt != 1) {
		uprintf(COM_NO_ARGS);  return;
		}
	uprintf("\nTotal mail count : %d\n",mail->msgcnt);
	uprintf("Todays mail count: %d\n",mail->todays_msgcnt);
	uprintf("Unread count     : %d\n",mail->unread_msgcnt);
	if (mail->msgcnt) {
		uprintf("Most recent mail : ~FY#%d~RS from ~FT%s~RS, %s\n",
			mail->last_msg->mnum,
			mail->last_msg->id,
			mail->last_msg->name);
		uprintf(" ... and subject : %s\n\n",mail->last_msg->subject);
		}
	else uprintf("\n");
	return;


	case MCOM_RENUM:
	if (com->wcnt != 1) {
		uprintf(COM_NO_ARGS);  return;
		}
	if (!mail->first_msg)
		uprintf("There are no messages to renumber.\n"); 
	else {
		mail->renumber();
		uprintf("Messages renumbered.\n");
		}
	return;


	case MCOM_HELP:
	if (com->wcnt != 1) {
		uprintf(COM_NO_ARGS);  return;
		}
	uprintf("\n~FYMailer commands:\n");
	uprintf("    read    [[from]              <msg number>]\n");
	uprintf("    write   <user id[@<server>]> [<text>]\n");
	uprintf("    reply   <msg number>         [include]\n");
	uprintf("    forward <msg number>         <user id[@<server>]>\n");
	uprintf("    delete  <msg number>         [to <msg number>]\n");
	uprintf("            all\n");
	uprintf("    list\n");
	uprintf("    info\n");
	uprintf("    renumber\n");
	uprintf("    help\n");
	uprintf("    quit\n\n");
	return;


	case MCOM_QUIT:
	if (com->wcnt != 1) {
		uprintf(COM_NO_ARGS);  return;
		}
	mail->save();
	uprintf("\n~FYExiting mailer.\n\n");
	flags = prev_flags;
	stage = USER_STAGE_CMD_LINE;

	group->geprintf(MSG_MISC,this,NULL,
		"User ~FT%04X~RS (%s) has finished using the mailer.\n",
		id,name);
	}
}




/*** Send some mail or if we require the editor just send up id vars and
     invoke editor where cl_user::run_editor() will do actual sending of 
     mail. ***/
void cl_local_user::send_mail(
	char *idstr, char *subj, int use_editor, char *inc_path)
{
uint16_t uid;
cl_server *svr;
int ret;

if ((ret=idstr_to_id_and_svr(idstr,&uid,&svr)) != OK) {
	errprintf("Invalid address: %s\n",err_string[ret]);  return;
	}
if (uid == id && !svr) {
	uprintf("You want to send mail to yourself? Are you really that lonely? Buy a dog.\n");
	return;
	}
if (uid < MIN_LOCAL_USER_ID) {
	uprintf("Mail cannot be sent to that user id.\n");  return;
	}
if (uid & 0xF000) {
	uprintf("Mail can only be sent to users who are local on the given server.\n");
	return;
	}
// Check server connect first to save the user the grief of writing a long
// email then finding out it isn't...
if (svr && svr->stage != SERVER_STAGE_CONNECTED) {
	errprintf("Server '%s' is not connected.\n",svr->name);
	return;
	}

// User wants to use editor
if (use_editor) {
	if (subj) msg_subject = strdup(subj); // Free'd in cl_user::run_editor()
	mail_to_id = uid;
	mail_to_svr = svr;
	editor = new cl_editor(this,EDITOR_TYPE_MAIL,inc_path);
	if (editor->error != OK) {
		errprintf("Editor failure: %s\n",err_string[editor->error]);
		delete editor;
		FREE(msg_subject);
		prompt();
		}
	return;
	}

// Message on command line
if ((int)strlen(com->wordptr[2]) > max_mail_chars) {
	uprintf("Message too long, maximum length is %d characters.\n",
		max_mail_chars);
	return;
	}
if ((ret = deliver_mail_from_local(this,uid,svr,subj,com->wordptr[2])) != OK) 
	errprintf("Unable to deliver mail: %s\n",err_string[ret]);
else uprintf("Mail sent.\n");
}




/*** Send a SYSTEM email to the (un)lucky user ***/
int cl_local_user::send_system_mail(char *subj, char *mesg)
{
cl_mail *tmail;
int ret;

/* At the time of writing this will only be for a temp user object and they
   won't have a mail object created but for the future check for existence of
   mail object anyway. */
if (!mail) {
	tmail = new cl_mail(NULL,id);
	if (tmail->error != OK) {
		ret = tmail->error;  goto RET;
		}
	}
else tmail = mail;

ret = tmail->mwrite(0,SYS_USER_NAME,NULL,subj,mesg);
	
RET:
if (tmail != mail) delete tmail;
return ret;
}



//////////////////////////// MISCELLANIOUS FUNCTIONS //////////////////////////


/*** Run the login & logout batch commands ***/
void cl_local_user::run_batch(int btype)
{
char path[MAXPATHLEN];
char *filename;
int paging,i;

if (!batchfp) {
	switch(btype) {
		case BATCH_TYPE_LOGIN:
		batch_max_lines = max_login_batch_lines;
		filename = "login";
		break;

		case BATCH_TYPE_LOGOUT:
		batch_max_lines = max_logout_batch_lines;
		filename = "logout";
		break;

		case BATCH_TYPE_SESSION:
		batch_max_lines = max_session_batch_lines;
		filename = batch_name;
		break;

		default:
		errprintf("Unexpected batch stage %d!\n",btype);
		log(1,"ERROR: Unexpected batch stage %d for user %04X in cl_local_user::run_batch()\n",btype,id);
		fclose(batchfp);
		return;
		}

	// Open file, if we can't then silently switch back to no batch
	sprintf(path,"%s/%04X/%s%s",USER_DIR,id,BATCH_PRE,filename);
	if (!(batchfp = fopen(path,"r"))) {
		FREE(batch_name);
		batch_type = BATCH_TYPE_NOBATCH;
		return;
		}

	if (!batch_max_lines) {
		sysprintf("~FYCannot run that batch at this time.\n");
		fclose(batchfp);
		FREE(batch_name);
		batch_type = BATCH_TYPE_NOBATCH;
		return;
		}
	sysprintf("~FYRunning batch \"%s\"...\n",filename);
	batch_type = btype;
	batch_line = 0;
	prev_flags = flags;
	}

/* Loop while we're in command line mode. If we're in another mode
   then a batch command has started up the mailer, board reader etc
   and this function will be called again in cl_local_user::uread() if in
   login batch, in logout batch it'll be ignored. */
do {
	tbpos = 0;
	fgets((char *)tbuff,ARR_SIZE,batchfp);
	if (feof(batchfp) || batch_line == batch_max_lines) {
		fclose(batchfp);
		batchfp = NULL; 
		batch_type = BATCH_TYPE_NOBATCH;
		sysprintf("~FYBatch run ended after %d commands.\n",
			batch_line);
		return;
		}
	if (!(i = strlen((char *)tbuff))) continue;
	if (tbuff[i-1] == '\n') {
		tbuff[--i] = '\0';
		if (!i) continue;
		}

	// Reset anything possible set by interactive sub systems, eg mailer
	// or paging. In login batch we won't re-enter this function until 
	// these have completed but in logout batch we will.
	if (editor) delete editor;
	stage = USER_STAGE_CMD_LINE; 
	inline_prompt[0] = '\0';

	/* Only set flags to prev on logout because mailer/board etc set
	   nospeech on and if we force exit them this will still be the case.
	   Also stop paging if logging out */
	if (batch_type == BATCH_TYPE_LOGOUT) {
		flags = prev_flags;
		paging = FLAGISSET(USER_FLAG_PAGING);
		UNSETFLAG(USER_FLAG_PAGING);
		}

	sysprintf("~FYCommand:~RS %s\n",tbuff);
	parse_line();
	++batch_line;

	// Switch paging back on if it was previously set so save() is correct.
	if (batch_type == BATCH_TYPE_LOGOUT && paging)
		SETFLAG(USER_FLAG_PAGING);

	} while(batch_type == BATCH_TYPE_LOGOUT || 
	        (!page_pos && 
	          com_page_line == -1 && 
	          !editor && stage == USER_STAGE_CMD_LINE));

tbpos = 0;
}




/*** Run the suicide stage ***/
void cl_local_user::run_suicide()
{
if (!com->wcnt) {
	uprintf("\n~FGSuicide abandoned, your account lives a while longer.\n\n");
	stage = USER_STAGE_CMD_LINE;
	return;
	}
if (strcmp(crypt_str(com->wordptr[0]),pwd)) {
	uprintf("\n~FYIncorrect password. Is this suicide or murder??\n\n");
	stage = USER_STAGE_CMD_LINE;
	return;
	}

uprintf("\n\n~OL~LI~FRDELETING ACCOUNT!\n\n");
allprintf(MSG_INFO,USER_LEVEL_NOVICE,this,
	"User ~FT%04X~RS (%s) has ~OL~FRSUICIDED!\n",id,name);
log(1,"User %04X (%s) SUICIDED.\n",id,name);

// Set flat and delete user. Local user destructor will do the work.
SETFLAG(USER_FLAG_DELETE);
throw USER_STAGE_DISCONNECT;
}




/*** Delete a batch file. ***/
void cl_local_user::run_delete_batch()
{
char path[MAXPATHLEN];

if (!com->wcnt) return;
switch(toupper(com->word[0][0])) {
	case 'N':
	uprintf("\n~FYBatch delete cancelled.\n\n");
	stage = USER_STAGE_CMD_LINE;
	return;

	case 'Y': break;

	default:  return;
	}

sprintf(path,"%s/%04X/%s%s",USER_DIR,id,BATCH_PRE,com_filename);
if (unlink(path))
	errprintf("Unable to delete file: %s\n",strerror(errno));
else uprintf("\nBatch file ~OL~FRDELETED.\n");

FREE(com_filename);
stage = USER_STAGE_CMD_LINE;
}




/*** Kick off the editor to create a batch file. ***/
void cl_local_user::run_create_batch(int at_prompt, char *path)
{
if (at_prompt) {
	if (!com->wcnt) return;

	switch(toupper(com->word[0][0])) {
		case 'N':
		uprintf("\n~FYBatch create cancelled.\n\n");
		stage = USER_STAGE_CMD_LINE;
		FREE(com_filename);
		return;

		case 'Y': break;

		default:  return;
		}
	}

editor = new cl_editor(this,batch_create_type,path);

if (editor->error != OK) {
	errprintf("Editor failure: %s\n",err_string[editor->error]);
	delete editor;
	FREE(com_filename);
	}

stage = USER_STAGE_CMD_LINE;
}




/*** Set the users name ***/
int cl_local_user::set_name(char *nme)
{
char *tmp,*s;

if (!nme) nme = "";

// Don't allow user to call themselves SYSTEM or allow print codes in the name
if (!strcmp(nme,SYS_USER_NAME) || has_printcode(nme)) return ERR_INVALID_NAME;

// Don't allow invalid characters
for(s = nme;*s;++s) if (*s < 32 || *s == '@') return ERR_INVALID_NAME;

if (!(tmp = strdup(nme))) return ERR_MALLOC;
if ((int)strlen(tmp) > max_name_len) tmp[max_name_len] = '\0';
FREE(name);
name = tmp;

name_key = generate_key(name);
return OK;
}



////////////////////////// VIRTUAL FUNCTION COMMANDS /////////////////////////

/*** Leave the server/group ***/
void cl_local_user::com_leave()
{
if (com->wcnt != 1) {
	uprintf(COM_NO_ARGS);  return;
	}
if (server_to) home_group->join(this);
else uprintf("You are not on a remote server.\n"); 
}




/*** Enter a profile ***/
void cl_local_user::com_profile()
{
if (com->wcnt != 1) {
	uprintf(COM_NO_ARGS);  return;
	}
if (server_to) {
	uprintf("You cannot enter a new profile whilst on a remote server.\n");
	return;
	}
editor = new cl_editor(this,EDITOR_TYPE_PROFILE,NULL);
if (editor->error != OK) {
	errprintf("Editor failure: %s\n",err_string[editor->error]);
	delete editor;
	}
}




/*** Do emails ***/
void cl_local_user::com_mail()
{
if (server_to) {
	uprintf("You cannot use the mail system whilst on a remote server.\n");
	return;
	}

// Reset stage & flags
stage = USER_STAGE_MAILER;
prev_flags = flags;
if (FLAGISSET(USER_FLAG_AUTOSILENCE)) {
	SETFLAG(USER_FLAG_NO_SPEECH);
	SETFLAG(USER_FLAG_NO_SHOUTS);
	}

group->geprintf(
	MSG_MISC,this,NULL,"User ~FT%04X~RS (%s) is using the mailer.\n",id,name);

// Renumber messages first
mail->renumber();

if (com->wcnt == 1) mail->list();
else {
	// Shift command line up by one word and call mailer so it executes
	// the command immediately
	com->shift();
	run_mailer();
	}
}




/*** Set the users email address ***/
void cl_local_user::com_setemail()
{
if (com->wcnt != 2) {
	uprintf("Usage: setemail <your email address>\n");  return;
	}
if (!strchr(com->word[1],'@')) {
	uprintf("Address format must be <name>@<address>\n");  return;
	}

FREE(email_address);
email_address = strdup(com->word[1]);

if (level < USER_LEVEL_USER)
	allprintf(
		MSG_INFO,
		USER_LEVEL_OPERATOR,
		NULL,
		"Novice %04X (%s) has set their email address to: %s.\n",
		id,name,email_address);

uprintf("Email address set.\n");
}




/*** Set users start group. This doesn't check if the user is banned as this
     may change in the future. ***/
void cl_local_user::com_stgroup()
{
uint16_t gid;
cl_group *grp;
int ret;

if (com->wcnt != 2) {
	uprintf("Usage: stgroup <group id>/home\n");  return;
	}
if (!strncasecmp(com->word[1],"home",strlen(com->word[1]))) {
	if (home_group->id == start_group_id) {
		uprintf("Your start group is already set to that.\n");
		return;
		}
	grp = home_group;
	}
else {
	if (!(gid = idstr_to_id(com->word[1]))) {
		uprintf("Invalid id.\n");  return;
		}
	if (gid == start_group_id) {
		uprintf("Your start group is already set to that.\n");  return;
		}
	if (!(grp = get_group(gid))) {
		uprintf(NO_SUCH_GROUP);  return;
		}
	if (grp == gone_remote_group || grp == prison_group) {
		uprintf("You cannot start in that group.\n");  return;
		}
    	if (grp->user_is_banned(this)) {
		uprintf("You are banned from that group!\n");  return;
		}
	}

start_group_id = grp->id;
if ((ret = save()) != OK) 
	errprintf("Unable to save your details: %s\n",err_string[ret]);
else uprintf("Start group set to: %04X, %s\n",grp->id,grp->name);
}




/*** User deletes their account ***/
void cl_local_user::com_suicide()
{
if (com->wcnt != 1) {
	uprintf(COM_NO_ARGS);  return;
	}
if (server_to) {
	uprintf("You cannot suicide whilst on a remote server.\n");
	return;
	}
uprintf("\n~SN~LI~OL~FRWARNING: This will delete your account!\n\n");
uprintf("~OL~FREnter your password to comfirm deletion or just press return to cancel.\n\n");
stage = USER_STAGE_SUICIDE;
}




/*** Setup or view a login/logout command batch ***/
void cl_local_user::com_batch()
{
char path[MAXPATHLEN];
struct stat fs;
int sc,ret,bt,stat_res;
int len,inc;
char *subcom[] = {
	"create","list","run","delete"
	};
enum {
	SC_CREATE,SC_LIST,SC_RUN,SC_DEL
	};

if (com->wcnt < 3) goto USAGE;
len = strlen(com->word[1]);
for(sc=0;sc < 4;++sc) 
	if (!strncasecmp(com->word[1],subcom[sc],len)) break;

if (sc != SC_CREATE && com->wcnt != 3) goto USAGE;

if (!strcasecmp(com->word[2],"login")) bt = BATCH_TYPE_LOGIN;
else
if (!strcasecmp(com->word[2],"logout")) bt = BATCH_TYPE_LOGOUT;
else {
	if (!valid_filename(com->word[2])) {
		uprintf("Invalid filename. Do not use double dots, slashes, percents, tildes or control\ncharacters.\n");
		return;
		}
	bt = BATCH_TYPE_SESSION;
	}

sprintf(path,"%s/%04X/%s%s",USER_DIR,id,BATCH_PRE,com->word[2]);
stat_res = stat(path,&fs);

switch(sc) {
	case SC_CREATE:
	switch(com->wcnt) {
		case 3:  inc = 0;  break;
		case 4:
		if (!strncasecmp(com->word[3],"include",strlen(com->word[3]))) {
			inc = 1;  break;
			}
		// Fall through

		default: goto USAGE;
		}
	if (server_to) {
		uprintf("You cannot create a batch whilst on a remote server.\n");
		return;
		}

	// Cannot create a batch if we're running one
	if (batch_type != BATCH_TYPE_NOBATCH) {
		errprintf("Cannot create a batch from within a batch.\n");
		return;
		}

	if ((int)strlen(com->word[2]) > max_batch_name_len) {
		uprintf("Batch name too long, maximum length is %d characters.\n\n%s",max_batch_name_len);
		return;
		}
	switch(bt) {
		case BATCH_TYPE_LOGIN:
		if (!max_login_batch_lines) {
			uprintf("Login batches are not currently allowed.\n");
			return;
			}
		break;

		case BATCH_TYPE_LOGOUT:
		if (!max_logout_batch_lines) {
			uprintf("Logout batches are not currently allowed.\n");
			return;
			}
		break;

		case BATCH_TYPE_SESSION:
		if (!max_session_batch_lines) {
			uprintf("Session batches are not currently allowed.\n");
			return;
			}
		}
	batch_create_type = bt;
	com_filename = strdup(com->word[2]);

	if (!inc && !stat_res) {
		warnprintf("Batch file '%s' already exists...\n\n",com->word[2]);
		stage = USER_STAGE_OVERWRITE_BATCH_FILE;
		return;
		}
	run_create_batch(0,inc ? path : NULL);
	return;


	case SC_LIST:
	if (stat_res) {
		uprintf("You don't have a batch of that name.\n");
		return;
		}

	uprintf("\n~BB*** Batch \"%s\" ***\n\n",com->word[2]);
	if ((ret=page_file(path,1)) != OK)
		errprintf("cannot page batch file: %s\n\n",err_string[ret]);
	if (!page_pos) uprintf("\n");
	return;


	case SC_RUN:
	if (batch_type != BATCH_TYPE_NOBATCH) {
		// This would cause seriously nasty and possibly infinite
		// recursion
		errprintf("Cannot run a batch from within a batch.\n");
		return;
		}
	if (server_to && !SYS_FLAGISSET(SYS_FLAG_ALLOW_REM_BATCH_RUNS)) {
		uprintf("You cannot run a batch while on a remote server.\n");
		return;
		}
	if (!max_session_batch_lines) {
		uprintf("Running batches in session is not currently allowed.\n");
		return;
		}
	if (stat_res) {
		uprintf("You don't have a batch of that name.\n");
		return;
		}

	prev_flags = flags;
	batch_type = bt;
	FREE(batch_name);
	batch_name = strdup(com->word[2]);
	return;


	case SC_DEL:
	if (batch_type != BATCH_TYPE_NOBATCH) {
		errprintf("Cannot delete a batch from within a batch.\n");
		return;
		}
	if (stat_res) {
		uprintf("You don't have a batch of that name.\n");
		return;
		}
	com_filename = strdup(com->word[2]);
	stage = USER_STAGE_DELETE_BATCH_FILE;
	uprintf("\n");
	return;
	}
// Fall through

USAGE:
uprintf("Usage: batch list/run/delete <batch name>\n");
uprintf("       batch create <batch name> [include]\n");
}




/*** List the command batches ***/
void cl_local_user::com_lsbatches()
{
DIR *dir;
struct dirent *ds;
struct stat fs;
char dirname[MAXPATHLEN];
char path[MAXPATHLEN];
int lcnt,bcnt;

if (com_page_line == -1 && com->wcnt != 1) {
	uprintf(COM_NO_ARGS);  return;
	}
	
// Just look through the users directory for any file beginning with batch_
sprintf(dirname,"%s/%04X",USER_DIR,id);
if (!(dir=opendir(dirname))) {
	errprintf("Unable to open directory to read: %s\n",strerror(errno));
	log(1,"ERROR: Unable to open user directory %04X to read: %s\n",
		strerror(errno));
	com_page_line = -1;
	return;
	}

lcnt = 0;
bcnt = 0;
while((ds=readdir(dir))) {
	sprintf(path,"%s/%s",dirname,ds->d_name);
	if (!stat(path,&fs) &&
	    (fs.st_mode & S_IFMT) == S_IFREG &&
	    !strncmp(ds->d_name,BATCH_PRE,6)) {
		if (!bcnt++ && com_page_line == -1) {
			uprintf("\n~BB*** Your batches ***\n\n");
			uprintf("~OL~ULName%*sBytes\n",max_batch_name_len-4,"");
			}
		if (bcnt > com_page_line) {
			uprintf("%-*s%4d\n",
				max_batch_name_len+1,ds->d_name+6,fs.st_size);
			if (FLAGISSET(USER_FLAG_PAGING) &&
			    ++lcnt == term_rows - 2) {
				com_page_line = bcnt;  return;
				}
			}
		}
	}
closedir(dir);

if (FLAGISSET(USER_FLAG_PAGING) && lcnt && lcnt >= term_rows - 5) {
	com_page_line = bcnt;  return;
	}
if (bcnt) uprintf("\n~FTTotal of %d batches.\n\n",bcnt);
else uprintf("You have no batches.\n");
com_page_line = -1;
}
