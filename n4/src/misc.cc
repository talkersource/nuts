/***************************************************************************
 FILE: misc.cc
 LVU : 1.3.9

 DESC:
 This contains miscellanious functions that don't really belong anywhere else.

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

/*** Create a listen socket ***/
int create_listen_socket(int port_type)
{
int on;

if (port_type != PORT_TYPE_USER && port_type != PORT_TYPE_SERVER)
	return ERR_INTERNAL;

if ((listen_sock[port_type] = socket(AF_INET,SOCK_STREAM,0)) == -1) 
	return ERR_SOCKET;

on = 1;
if (setsockopt(
	listen_sock[port_type],
	SOL_SOCKET,SO_REUSEADDR,(char *)&on,sizeof(on)) == -1) 
	return ERR_SOCKET;

main_bind_addr.sin_port = htons(tcp_port[port_type]);
if (bind(
	listen_sock[port_type],
	(sockaddr *)&main_bind_addr,sizeof(sockaddr_in)) == -1)
	return ERR_SOCKET;

if (listen(listen_sock[port_type],LISTEN_QSIZE) == -1) return ERR_SOCKET;

return OK;
}




/*** Deliver mail from local user to local or remote user ***/
int deliver_mail_from_local(
	cl_user *ufrom,
	uint16_t uid_to, cl_server *svrto, char *subj, char *msg)
{
// Send to remote
if (svrto) return svrto->send_mail(ufrom,uid_to,subj,msg);

return deliver_mail(ufrom->id,ufrom->name,NULL,uid_to,subj,msg);
}




/*** Deliver mail to a local user ***/
int deliver_mail(
	uint16_t uid_from, 
	char *uname,
	cl_server *svrfrom, uint16_t uid_to, char *subj, char *msg)
{
cl_mail *mail;
cl_local_user *uto;
char svrstr[MAX_SERVER_NAME_LEN+2];
int ret;

// Deliver locally. If user is logged on use their mail object else 
// create a temp one.
if ((uto = (cl_local_user *)get_user(uid_to))) {
	if (uto->type != USER_TYPE_LOCAL) return ERR_INVALID_ID;
	mail = uto->mail;
	}
else {
	mail = new cl_mail(NULL,uid_to);
	if ((ret=mail->error) != OK) {
		log(1,"ERROR: deliver_mail(): Mail object creation failed: %s\n",
			err_string[ret]);
		delete mail;
		return ret;
		}
	}

if ((ret = mail->mwrite(uid_from,uname,svrfrom,subj,msg)) != OK) {
	log(1,"ERROR: deliver_mail(): Mail write failed: %s\n",err_string[ret]);
	if (!uto) delete mail;  
	return ret;
	}
if (uto) {
	if (svrfrom) sprintf(svrstr,"@%s",svrfrom->name);
	else svrstr[0]='\0';
	uto->infoprintf("~SN~FT~LI~OLYou have new mail from %04X%s (%s), mesg #%d\n",
		uid_from,svrstr,uname,uto->mail->last_msg->mnum);

	uto->infoprintf("You have %d mail messages, ~OL~FT%d~RS are unread.\n",
		mail->msgcnt,mail->unread_msgcnt);
	}
else delete mail;

return OK;
}




/*** Write out some info with the time prepended. We don't keep an open file
     descriptor for the log file since if its accidentaly deleted then unix
     won't recreate the file via an open descriptor to the old file. Opening
     and closing it (though inefficient) avoids this issue. ***/
void log(int timestamp, char *fmtstr, ...)
{
char str[ARR_SIZE];
char tstr[20];
va_list args;
FILE *fp;

va_start(args,fmtstr);
vsnprintf(str,ARR_SIZE - 20,fmtstr,args);
va_end(args);

if (timestamp) strftime(tstr,sizeof(tstr),"%d/%m %H:%M:%S: ",&server_time_tms);
else tstr[0] = '\0';

// If printing to stdout ...
if (!logfile) {
	if (str[strlen(str)-1] == '\n') printf("%s%s",tstr,str);
	else printf("%s%s\n",tstr,str);
	return;
	}

/* Use a mutex here so different threads don't write over each others
   data in the file. I've no idea if this would actually happen , I
   guess it depends how write() is implemented by the OS, but I'm not
   taking any chances. */
pthread_mutex_lock(&log_mutex);

// If this doesn't work where do we print out the error?? We can't , so
// just return. 
if (!(fp = fopen(logfile,"a"))) {
	pthread_mutex_unlock(&log_mutex);
	return;
	}

if (str[strlen(str)-1] == '\n') fprintf(fp,"%s%s",tstr,str);
else fprintf(fp,"%s%s\n",tstr,str);
fclose(fp);

pthread_mutex_unlock(&log_mutex);
}




/*** Send a print to all the users of the given level and above ***/
void allprintf(int mtype,int level,cl_user *uign, char *fmtstr,...)
{
char str[ARR_SIZE];
va_list args;
cl_user *u;

va_start(args,fmtstr);
vsnprintf(str,ARR_SIZE,fmtstr,args);
va_end(args);

// Check stage here because this is used for LOGON when user is connecting
// and level is already set 
FOR_ALL_USERS(u) {
	if (u != uign && u->level >= level) {
		switch(mtype) {
			case MSG_SPEECH:
			if (!O_FLAGISSET(u,USER_FLAG_NO_SPEECH))
				u->uprintf(str);
			break;

			case MSG_SHOUT:
			if (!O_FLAGISSET(u,USER_FLAG_NO_SHOUTS))
				u->uprintf(str);
			break;

			case MSG_INFO:
			u->infoprintf(str);  break;

			case MSG_MISC:
			if (!O_FLAGISSET(u,USER_FLAG_NO_MISC))
				u->uprintf(str); 
			break;

			case MSG_SYSTEM:
			u->sysprintf(str);
			break;

			case MSG_BCAST:
			u->uprintf(str);
			break;

			case MSG_NETBCAST:
			if (O_FLAGISSET(u,USER_FLAG_RECV_NET_BCAST))
				u->uprintf(str);
			}
		}
	}
}




/*** Trim the whitespace of the head & tail of a string ***/
char *trim(char *str)
{
char *s,*s2;

if (!str) return str;

for(s=str;*s && *s < 33;++s);
if (*s) {
	for(s2=str+strlen(str)-1;s2 > s && *s2 < 33;--s2);
	*(s2+1)='\0';
	}
return s;
}




/*** Returns 1 if string is positive integer else 0 ***/
int is_integer(char *str)
{
char *s;

if (!str) return 0;
for(s=str;*s;++s) if (!isdigit(*s)) return 0;
return 1;
}




/*** Return an encrypted password ***/
char *crypt_str(char *pwd)
{
char salt[3];

// Add 2 as we don't want to see the salt itself
strncpy(salt,pwd,2);
return crypt(pwd,salt)+2;
}




/*** Return the 16 bit id number. Returns zero on error. ***/
uint16_t idstr_to_id(char *idstr)
{
// sscanf doesn't work with shorts
int id;
char *s;

if (strlen(idstr) != 4) return 0;

// Do manual check for allowed digits as sscanf will return a number
// even for duff values
for(s=idstr;*s;++s) 
	if (!isdigit(*s) && (toupper(*s) < 'A' || toupper(*s) > 'F')) return 0;

sscanf(idstr,"%04X",(uint *)&id);
return (uint16_t)id;
}




/*** Return the 16 bit id number and the server. ***/
int idstr_to_id_and_svr(char *idstr, uint16_t *id, cl_server **svr)
{
int tmpid;  // sscanf doesn't work with shorts
char *s,*ap,*cp;

*svr = NULL;

// Check if remote id
if ((ap = strchr(idstr,'@'))) {
	cp = ap;
	*ap = '\0';
	if (!*(++ap)) {
		*cp = '@';  return ERR_INVALID_SERVER;
		}
	if (!(*svr = get_server(ap))) {
		*cp = '@';  return ERR_NO_SUCH_SERVER;
		}
	}

if (strlen(idstr) != 4) {
	if (*svr) *cp = '@';
	return ERR_INVALID_ID;
	}
	
// Do manual check for allowed digits as sscanf will return a number
// even for duff values
for(s=idstr;*s;++s) {
	if (!isdigit(*s) && (toupper(*s) < 'A' || toupper(*s) > 'F')) {
		if (*svr) *cp = '@';
		return ERR_INVALID_ID;
		}
	}

sscanf(idstr,"%04X",(uint *)&tmpid);
*id = (uint16_t)tmpid;
if (*svr) *cp = '@';

return OK;
}




/*** Return pointer to a user object based on id ***/
cl_user *get_user(uint16_t id, int check_logins)
{
cl_user *u;

FOR_ALL_USERS(u) 
	if (u->id == id && (u->level != USER_LEVEL_LOGIN || check_logins))
		return u;
return NULL;
}




/*** Get local user by name. User start from is in case there is more than
     one user with the same name and it goes from zero. This function uses
     the name key to speed up searches unless name supplied is a pattern. ***/
cl_user *get_user_by_name(char *name, int start_from)
{
cl_user *u;
int is_pattern,cnt;
uint32_t key;

if (strpbrk(name,"?*!=")) is_pattern = 1;
else {
	key = generate_key(name);
	is_pattern = 0;
	}
cnt = 0;

FOR_ALL_USERS(u) {
	if (u->level > USER_LEVEL_LOGIN) {
		if (is_pattern) {
			if (match_pattern(u->name,name) && cnt++ == start_from)
				return u;
			}
		else 
		if (u->name_key == key &&
		    !strcmp(u->name,name) && cnt++ == start_from) return u;
		}
	}
return NULL;
}



     
/*** Return pointer to user object of remote user ***/
cl_user *get_remote_user(uint16_t orig_id, uint32_t addr, uint16_t port)
{
cl_user *u;

FOR_ALL_USERS(u) {
	if (u->type == USER_TYPE_REMOTE) {
		if (((cl_remote_user *)u)->orig_id == orig_id &&
		    u->ip_addr.sin_addr.s_addr == addr &&
		    u->ip_addr.sin_port == port) return u;
		}
	}
return NULL;
}




/*** Return the level number given the name ***/
int get_level(char *levstr)
{
int i,len;

if (!levstr) return -1;

len = strlen(levstr);
for(i=0;i < NUM_LEVELS;++i)
	if (!strncasecmp(user_level[i],levstr,len)) return i;
return -1;
}




/*** Return pointer to a group object that has given id ***/
cl_group *get_group(uint16_t id)
{
cl_group *grp;

FOR_ALL_GROUPS(grp) if (grp->id == id) return grp;
return NULL;
}




/*** Return pointer to server object that has given name ***/
cl_server *get_server(char *name)
{
cl_server *svr;

FOR_ALL_SERVERS(svr) 
	if (!strcasecmp(svr->name,name)) return svr;
return NULL;
}




/*** Returns true if string has whitespace ***/
int has_whitespace(char *str)
{
for(;*str;++str) if (*str < 33) return 1;
return 0;
}




/*** Send info to all the connected servers if they require it ***/
void send_server_info_to_servers(cl_server *svr_dont_send)
{
cl_server *svr;
int ret;

FOR_ALL_SERVERS(svr) {
	if (svr != svr_dont_send &&
	    svr->stage == SERVER_STAGE_CONNECTED) {
		if ((ret=svr->send_svr_info(PKT_INF_SVR_INFO)) != OK) 
			log(1,"ERROR: send_server_info_to_servers() -> cl_server::send_svr_info(): %s\n",err_string[ret]);
		}
	}
}




/*** Send user info to all connected servers ***/
void send_user_info_to_servers(cl_user *u)
{
cl_server *svr;
int ret;

FOR_ALL_SERVERS(svr) {
	if (svr->stage == SERVER_STAGE_CONNECTED &&
	    (ret=svr->send_user_info(u)) != OK)
		log(1,"ERROR: send_user_info_to_servers() -> cl_server::send_user_info(): %s\n",err_string[ret]);
	}
}




/*** Returns 1 if there is a printcode in the string else 0 ***/
int has_printcode(char *str)
{
char *s;
int esc,i;

for(s=str,esc=0;*s;++s) {
	switch(*s) {
		case '/': esc=1; break;
		case '~':
		if (esc) { esc=0;  break; }
		for(i=0;i < NUM_PRINT_CODES;++i) 
			if (!strncmp(s+1,printcode[i],2)) return 1;
		break;

		default: esc=0;
		}
	}
return 0;
}




/*** Strip out stuff in the string that shouldn't be there such as
     percents and certain print codes ***/
void clean_string(char *str)
{
char *s,*s2;
char *pcom[8] = { "SC","SH","LC","IP","NP","PR","SN","CU" };
int pcesc,i;

// Remove any printf() percent codes and any NUTS print codes that can't
// be used by users. Also remove certain ASCII control codes.
for(s=(char *)str,s2=(char *)str+1,pcesc=0;*s;++s) {
	// Percents
	if (*s == '%') {
		if (*s2 != '%') *s = ' '; else s++;
		}
	else
	if (*s < 32) {
		switch(*s) {
			case '\n':
			case '\r':
			case '\t': break;

			*s = ' ';
			}
		}

	// Printcodes
	if (SYS_FLAGISSET(SYS_FLAG_STRIP_PRINTCODES)) {
		switch(*s) {
			case '/':
			pcesc = !pcesc;
			break;

			case '~':
			if (pcesc) {  pcesc=0;  break;  }
			s2 = s+1;
			for(i=0;i < 8;++i) {
				if (!strncmp(s2,pcom[i],2)) {
					strcpy(s,s+3);  s--;  break;
					}
				}
			break;

			default:
			pcesc=0;
			}
		}

	s2 = s+2;
	}
}




/*** Adds a line to review buffer for group or user ***/
int add_review_line(struct revline *revbuff,int *revpos,char *str)
{
char *tmp;
int len;

len = strlen(str);
if (len >= revbuff[*revpos].alloc) {
	// Allocate new memory if string is longer than string currently
	// stored in present buffer location
	if (!(tmp = (char *)malloc(len + 1))) return ERR_MALLOC;
	FREE(revbuff[*revpos].line);
	revbuff[*revpos].line = tmp;
	revbuff[*revpos].alloc = len+1;
	}
strcpy(revbuff[*revpos].line,str);
*revpos = (*revpos + 1) % num_review_lines;
return OK;
}




/*** Return message object pointer based on number ***/
cl_msginfo *get_message(cl_msginfo *first_msg, int num)
{
cl_msginfo *minfo;

FOR_ALL_MSGS(minfo) if (minfo->mnum == num) return minfo;
return NULL;
}




/*** Return a string displaying a length of time in days, hours, minutes, secs
     based upon the value in seconds passed ***/
char *time_period(int secs)
{
static char str[50];
int mins;
int hours;
int days;
int len;

days = secs / 86400;
hours = secs / 3600;
if (days) hours %= 24;
mins = secs / 60;
if (days || hours) mins %= 60;
if (days || hours || mins) secs %= 60;

str[0] = '\0';

if (days == 1) sprintf(str,"%d day, ",days);
else
if (days > 1) sprintf(str,"%d days, ",days);

if (hours == 1) sprintf(str,"%s%d hour, ",str,hours);
else
if (hours > 1) sprintf(str,"%s%d hours, ",str,hours);

if (mins == 1) sprintf(str,"%s%d minute, ",str,mins);
else
if (mins > 1) sprintf(str,"%s%d minutes, ",str,mins);

if (secs || !str[0]) {
	if (secs == 1) sprintf(str,"%s%d second",str,secs);
	else sprintf(str,"%s%d seconds",str,secs);
	}

// Remove any trailing comma-space
len = strlen(str);
if (str[len-1] == ' ') str[len-2] = '\0';
return str;
}




/*** Create a temporary local user object ***/
cl_local_user *create_temp_user(uint16_t uid)
{
cl_local_user *lu;

lu = new cl_local_user(1);
if (lu->error != OK) {
	delete lu;  return NULL;
	}
lu->id = uid;
if (lu->load(USER_LOAD_2) != OK) {
	delete lu;  return NULL;
	}
return lu;
}




/*** Get seconds from 2 words the first of which is a number and the
     2nd is a time unit eg: 100 minutes, 2 hour, etc. Returns -1 on error ***/
int get_seconds(char *numstr, char *timeunit)
{
int num,len;

if (!is_integer(numstr) || (num=atoi(numstr)) < 0) return -1;
if (!timeunit) return num;  // Default is seconds

len = strlen(timeunit);
if (!strncasecmp(timeunit,"seconds",len)) return num;
else
if (!strncasecmp(timeunit,"minutes",len)) return num*60;
else
if (!strncasecmp(timeunit,"hours",len)) return num*3600;
else
if (!strncasecmp(timeunit,"days",len)) return num*86400;

return -1;
}




/*** Return 1 if a user is banned. ***/
int user_is_banned(uint16_t uid, uint32_t addr, uint16_t port)
{
st_user_ban *ub;

FOR_ALL_USER_BANS(ub) {
	if (ub->uid == uid && 
	    ub->home_addr.sin_addr.s_addr == addr &&
	    ub->home_addr.sin_port == port) return 1;
	}
return 0;
}




/*** Returns 1 is site is banned for given type ***/
int site_is_banned(int sbtype, uint32_t addr)
{
st_site_ban *sb;

FOR_ALL_SITE_BANS(sb) {
	if ((sb->type == SITEBAN_TYPE_ALL || sb->type == sbtype) &&
	    sb->addr.sin_addr.s_addr == addr)  return 1;
	}
return 0;
}




/*** Returns 1 if filename is value , ie doesn't have .. or / or other
     nonsense in it ***/
int valid_filename(char *filename)
{
char *s,prev;

for(s=filename,prev='\0';*s;++s) {
	if ((prev == '.' && *s == '.') || *s < 32 || strchr("/\\%~",*s))
		return 0;
	prev = *s;
	}
return 1;
}




/**** Disconnect from tty device for when running as a daemon ***/
void dissociate_from_tty()
{
#ifdef TIOCNOTTY
ioctl(fileno(stdin),TIOCNOTTY,NULL);
ioctl(fileno(stdout),TIOCNOTTY,NULL);
ioctl(fileno(stderr),TIOCNOTTY,NULL);
#endif
setsid();
}




/*** Write out the ban file from the data in memory ***/
int write_ban_file()
{
FILE *fp;
st_user_ban *ub;
st_site_ban *sb;
char path[MAXPATHLEN];
char path2[MAXPATHLEN];

sprintf(path,"%s/%s",ETC_DIR,BAN_FILE);

// If no bans just delete file
if (!first_user_ban && !first_site_ban) {
	unlink(path);
	return OK;
	}

sprintf(path2,"%s/%s.tmp",ETC_DIR,BAN_FILE);
if (!(fp = fopen(path2,"a"))) return ERR_CANT_OPEN_FILE;

FOR_ALL_USER_BANS(ub) {
	if (ub->utype == USER_TYPE_LOCAL)
		fprintf(fp,"\"user ban\" = %s, %04X\n",
			user_level[ub->level],ub->uid);
	else
		fprintf(fp,"\"user ban\" = %s, %04X, %s, %d\n",
			user_level[ub->level],
			ub->uid,
			inet_ntoa(ub->home_addr.sin_addr),
			ntohs(ub->home_addr.sin_port));
	}
FOR_ALL_SITE_BANS(sb) 
	fprintf(fp,"\"site ban\" = %s, %s, %s\n",
		user_level[sb->level],
		inet_ntoa(sb->addr.sin_addr),siteban_type[sb->type]);

fclose(fp);

if (rename(path2,path)) return ERR_CANT_RENAME_FILE;
return OK;
}




/*** A simple key generator. For names of less that 5 characters the key
     will be unique for every name (since its basically mapping the string
     characters into the integer) ***/
uint32_t generate_key(char *str)
{
int len,pos,shift;
uint32_t res;

if (!(len = strlen(str))) return 0;

for(pos=0,shift=0,res=0;pos < len;++pos) {
	res += ((uint32_t)str[pos]) << shift;
	shift = (shift == 24 ? 0 : shift+8);
	}
return res;
}




/*** Match a pattern ***/
int match_pattern(char *str, char *pat)
{
char *s,*p,s2,p2,prev,*starpos;
int escaped,igncase,seq;

/* Attempt to match empty or null strings */
if (str==NULL) return (pat==NULL || !strlen(pat));
if (pat==NULL) return (str==NULL || !strlen(str));

s=str;
p=pat;
starpos=NULL;
escaped=0;
igncase=0;
seq=0;
prev = '\0';

/* Loop through string to be matched to pattern */
while(*s) {
	switch(*p) {
		case '\0': 
		/* If we've hit the end of the pattern go back to just after
		   the last star if there was one */
		if (starpos) {
			if (seq) break; else p=starpos+1;
			}
		else return 0;
		break; 

		case '?': 
		if (escaped) goto DEFAULT;
		++p;
		break;

		case '!':
		if (escaped) goto DEFAULT;
		igncase = 1;
		++p;
		continue;

		case '=':
		if (escaped) goto DEFAULT;
		igncase = 0;
		++p;
		continue;

		case '*': 
		if (escaped) goto DEFAULT;
		starpos=p++;
		if (s==str && p==pat+1) continue;
		break;

		case '\\': 
		if (!escaped) {  escaped=1;  --s;  ++p;  break;  }
		/* Fall through */

		default:
		DEFAULT:
		s2 = igncase ? toupper(*s) : *s;
		p2 = igncase ? toupper(*p) : *p;

		if (starpos) {
			if (s2 == p2) ++p;
			else {
				p = starpos+1;
				p2 = igncase ? toupper(*p) : *p;
				if (s2 == p2) s--;
				}	
			escaped=0;
			break;
			}
		if (s2 == p2) {
			escaped=0;  ++p;
			}
		else return 0;
		}
	prev = *s;
	++s;
	seq = (*s == prev);
	}
if (*p) return (*p=='*' && !*(p+1));
return 1;
}




/*** Delete or rename the directory of a user being deleted ***/
int delete_account(uint16_t id)
{
struct stat fs;
char user_dir[MAXPATHLEN];
char board_dir[MAXPATHLEN];
char new_dir[MAXPATHLEN];
int i,ret;

ret = OK;

sprintf(user_dir,"%s/%04X",USER_DIR,id);
sprintf(board_dir,"%s/%04X",BOARD_DIR,id);

if (SYS_FLAGISSET(SYS_FLAG_REALLY_DEL_ACCOUNTS)) {
	log(1,"DELETING user account %04X...\n",id);
	delete_dir(user_dir);
	delete_dir(board_dir);
	return OK;
	}

log(1,"RENAMING user account %04X...\n",id);

// Rename user dir
for(i=1;;++i) {
	sprintf(new_dir,"%s/%04X.del%d",USER_DIR,id,i);
	if (stat(new_dir,&fs)) break;
	}
if (rename(user_dir,new_dir)) {
	log(1,"ERROR: cl_local_user::~cl_local_user(): Failed to rename user dir %s to %s!\n",user_dir,new_dir);
	ret = ERR_CANT_RENAME_FILE;
	}

// Rename user board dir
for(i=1;;++i) {
	sprintf(new_dir,"%s/%04X.del%d",BOARD_DIR,id,i);
	if (stat(new_dir,&fs)) break;
	}
if (rename(board_dir,new_dir)) {
	log(1,"ERROR: cl_local_user::~cl_local_user(): Failed to rename user board dir %s to %s!\n",board_dir,new_dir);
	return ERR_CANT_RENAME_FILE;
	}
return ret;
}




/*** Delete directory and the files in it. This doesn't recurse down any
     subdirectories as there shouldn't be any in the dirs we delete (plus
     recursive deletes can be seriously bad news if they go out of control
     for whatever reason) though it would be trivial to add this ability. ***/
void delete_dir(char *dirname)
{
DIR *dir;
struct dirent *ds;
struct stat fs;
char path[MAXPATHLEN];

if ((dir=opendir(dirname))==NULL) {
	log(1,"ERROR: delete_dir(): Can't open dir '%s': %s\n",strerror(errno));
	return;
	}
while((ds = readdir(dir))) {
	if (!strcmp(ds->d_name,".") || !strcmp(ds->d_name,"..")) continue;

	sprintf(path,"%s/%s",dirname,ds->d_name);
	if (lstat(path,&fs) == -1) {
		log(1,"ERROR: delete_dir(): Can't stat file '%s': %s\n",
			strerror(errno));
		continue;
		}
	if ((fs.st_mode & S_IFMT) != S_IFREG) {
		log(1,"ERROR: delete_dir(): File '%s' is not a regular file.\n",path);
		continue;
		}
	if (unlink(path) == -1) 
		log(1,"ERROR: delete_dir(): unlink() failed: %s\n",strerror(errno));
	}
closedir(dir);
if (rmdir(dirname) == -1) 
	log(1,"ERROR: delete_dir(): rmdir() failed: %s\n",strerror(errno));
}




/*** Generate a new unused id. This must be from 0x100 to 0xFFF. The range to
     0x100 is reserved for public group ids and the high 4 bits are reserved
     for remote users to and they distinguish which server they are from. This
     gives a maximum of 16 remote servers which can be connected. ***/
uint16_t generate_id()
{
cl_user *u;
struct stat fs;
char path[MAXPATHLEN];
uint16_t start_id,new_id;

// Find random start point
start_id = new_id =
	(random() % (MAX_LOCAL_USER_ID-MIN_LOCAL_USER_ID)) + MIN_LOCAL_USER_ID;

// Loop until we find a free id or until we're back where we started
do {
	// Make sure it hasn't been given to another new user
	if (!(u=get_user(new_id,1))) {
		// Make sure it hasn't been given to an existing user
		sprintf(path,"%s/%04X",USER_DIR,new_id);
		if (stat(path,&fs)) return new_id;
		}

	if (++new_id > MAX_LOCAL_USER_ID) new_id = MIN_LOCAL_USER_ID;

	// If we're back where we started then no free id's
	} while(new_id != start_id);

return 0;
}




/*** Look through all users and servers to see if we've already 
     resolved the given ip number and if we have the return an allocated
     char string with the ip name. ***/
char *get_ipnamestr(uint32_t ipnum, void *obj)
{
cl_user *u;
cl_server *svr;

if (SYS_FLAGISSET(SYS_FLAG_INTERNAL_RESOLVE)) {
	FOR_ALL_USERS(u) {
		if (u != obj &&
		    u->level > USER_LEVEL_LOGIN &&
		    u->ip_addr.sin_addr.s_addr == ipnum &&
		    u->ipnamestr) return strdup(u->ipnamestr);
		}
	FOR_ALL_SERVERS(svr) {
		if (svr != obj &&
		    svr->stage == SERVER_STAGE_CONNECTED &&
		    svr->ip_addr.sin_addr.s_addr == ipnum &&
		    svr->ipnamestr) return strdup(svr->ipnamestr);
		}
	}
return NULL;
}




/*** Get the time down to 1/100ths of a second ***/
uint32_t get_hsec_time()
{
struct timeval tv;

gettimeofday(&tv,NULL);
return (uint32_t)((tv.tv_sec % 10000000) * 100 + tv.tv_usec / 10000);
}

