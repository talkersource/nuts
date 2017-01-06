/*****************************************************************************
             NUTS version 3.1.2 - Copyright (C) Neil Robertson 1996
                         Last update: 9th May 1996

 This software is provided as is. It is not intended as any sort of bullet
 proof system for commercial operation and I accept no liability for any
 problems that may arise from you using it. Since this is freeware (NOT public
 domain , I have not relinquished the copyright) you may distribute it as you 
 see fit and you may alter the code to suit your needs.

 Read the README file for further information.

 Thanks to Kien Tran and everyone else who helped me with my bug hunting :)

 Neil Robertson. 
 Email: neil@ogham.demon.co.uk

 NB: This program listing looks best when the tab length is 5 chars which is
 "set ts=5" in vi.

 *****************************************************************************/

#include <stdio.h>
#ifdef _AIX
#include <sys/select.h>
#endif
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <unistd.h>
#include <setjmp.h>
#include <errno.h>

#include "nuts312.h"

#define VERSION "3.1.2"

main()
{
fd_set readmask; 
int i,len; 
char inpstr[ARR_SIZE];
char *remove_first();
UR_OBJECT user,next;
NL_OBJECT nl;

/* Startup */
printf("\n*** NUTS %s server booting ***\n\n",VERSION);
init_globals();
write_syslog("\n*** SERVER BOOTING ***\n",0);
set_date_time();
init_signals();
load_and_parse_config();
init_sockets();
if (auto_connect) init_connections();
else printf("Skipping connect stage.\n");
check_messages(1);

/* Run in background automatically. */
switch(fork()) {
	case -1: boot_exit(11);  /* fork failure */
	case  0: break; /* child continues */
	default: sleep(1); exit(0);  /* parent dies */
	}
reset_alarm();
printf("\n*** Booted with PID %d ***\n\n",getpid());
sprintf(text,"*** Booted successfully with PID %d on %d/%d %02d:%02d:%02d ***\n\n",getpid(),tmday,tmonth+1,thour,tmin,tsec);
write_syslog(text,0);

/**** Main program loop. *****/
setjmp(jmpvar); /* rescue if we crash */
while(1) {
	/* set up mask then wait */
	setup_readmask(&readmask);
	if (select(FD_SETSIZE,&readmask,0,0,0)==-1) continue;

	/* check for connection to listen sockets */
	for(i=0;i<3;++i) {
		if (FD_ISSET(listen_sock[i],&readmask)) 
			accept_connection(listen_sock[i],i);
		}

	/* Cycle through client-server connections to other talkers */
	for(nl=nl_first;nl!=NULL;nl=nl->next) {
		no_prompt=0;
		if (nl->type==UNCONNECTED || !FD_ISSET(nl->socket,&readmask)) 
			continue;
		/* See if remote site has disconnected */
		if (!(len=read(nl->socket,inpstr,sizeof(inpstr)-1))) {
			sprintf(text,"NETLINK: Remote disconnect by service %s.\n",nl->service);
			write_syslog(text,1);
			sprintf(text,"SYSTEM: Lost link to service %s in room %s.\n",nl->service,nl->connect_room->name);
			write_room(NULL,text);
			shutdown_netlink(nl);
			continue;
			}
		inpstr[len]='\0'; 
		exec_netcom(nl,inpstr);
		}

	/* Cycle through users. Use a while loop instead of a for because
	    user structure may be destructed during loop in which case we
	    may lose the user->next link. */
	user=user_first;
	while(user!=NULL) {
		next=user->next; /* store in case user object is destructed */
		/* If remote user or clone ignore */
		if (user->type!=USER_TYPE) {  user=next;  continue; }

		/* see if any data on socket else continue */
		if (!FD_ISSET(user->socket,&readmask)) { user=next;  continue; }
	
		/* see if client (eg telnet) has closed socket */
		inpstr[0]='\0';
		if (!(len=read(user->socket,inpstr,sizeof(inpstr)))) {
			disconnect_user(user);  user=next;
			continue;
			}
		/* ignore control code replies */
		if ((unsigned char)inpstr[0]==255) { user=next;  continue; }

		/* Deal with input chars. If the following if test succeeds we
		   are dealing with a character mode client so call function. */
		if (inpstr[len-1]>=32 || user->buffpos) {
			if (get_charclient_line(user,inpstr,len)) goto GOT_LINE;
			user=next;  continue;
			}
		else terminate(inpstr);

		GOT_LINE:
		no_prompt=0;  
		com_num=-1;
		force_listen=0; 
		destructed=0;
		user->buff[0]='\0';  
		user->buffpos=0;
		user->last_input=time(0);
		if (user->login) {
			login(user,inpstr);  user=next;  continue;  
			}
		clear_words();
		if (user->afk) {
			write_user(user,"You are no longer afk.\n");  
			if (user->vis) {
				sprintf(text,"%s comes back from being afk.\n",user->name);
				write_room_except(user->room,text,user);
				}
			user->afk=0;
			}
		if (!(word_count=wordfind(inpstr))) {
			if (misc_ops(user,inpstr))  {  user=next;  continue;  }
			if (user->room==NULL) {
				sprintf(text,"ACT %s NL\n",user->name);
				write_sock(user->netlink->socket,text);
				}
			if (user->command_mode) prompt(user);
			user=next;  continue;
			}
		if (misc_ops(user,inpstr))  {  user=next;  continue;  }
		com_num=-1;
		if (user->command_mode || strchr(".,;!>-#/",inpstr[0])) 
			exec_com(user,inpstr);
		else say(user,inpstr);
		if (!destructed) {
			if (user->room!=NULL)  prompt(user); 
			else {
				switch(com_num) {
					case -1  : /* Unknown command */
					case HOME:
					case QUIT:
					case MODE:
					case PROMPT: 
					case SHUTDOWN: prompt(user);
					}
				}
			}
		user=next;
		}
	} /* end while */
}


/************ MAIN LOOP FUNCTIONS ************/

/*** Set up readmask for select ***/
setup_readmask(mask)
fd_set *mask;
{
UR_OBJECT user;
NL_OBJECT nl;
int i;

FD_ZERO(mask);
for(i=0;i<3;++i) FD_SET(listen_sock[i],mask);
/* Do users */
for (user=user_first;user!=NULL;user=user->next) 
	if (user->type==USER_TYPE) FD_SET(user->socket,mask);

/* Do client-server stuff */
for(nl=nl_first;nl!=NULL;nl=nl->next) 
	if (nl->type!=UNCONNECTED) FD_SET(nl->socket,mask);
}


/*** Accept incoming connections on listen sockets ***/
accept_connection(lsock,num)
int lsock,num;
{
UR_OBJECT user,create_user();
NL_OBJECT create_netlink();
char *get_ip_address(),site[80];
struct sockaddr_in acc_addr;
int accept_sock,size;

size=sizeof(struct sockaddr_in);
accept_sock=accept(lsock,(struct sockaddr *)&acc_addr,&size);
if (num==2) {
	accept_server_connection(accept_sock,acc_addr);  return;
	}
strcpy(site,get_ip_address(acc_addr));
if (site_banned(site)) {
	write_sock(accept_sock,"\n\rLogins from your site are banned.\n\n\r");
	close(accept_sock);
	sprintf(text,"Attempted login from banned site %s.\n",site);
	write_syslog(text,1);
	return;
	}
more(NULL,accept_sock,MOTD1); /* send pre-login message */
if (num_of_users+num_of_logins>=max_users && !num) {
	write_sock(accept_sock,"\n\rSorry, the talker is full at the moment.\n\n\r");
	close(accept_sock);  
	return;
	}
if ((user=create_user())==NULL) {
	sprintf(text,"\n\r%s: unable to create session.\n\n\r",syserror);
	write_sock(accept_sock,text);
	close(accept_sock);  
	return;
	}
user->socket=accept_sock;
user->login=3;
user->last_input=time(0);
if (!num) user->port=mainport; 
else {
	user->port=wizport;
	write_user(user,"** Wizport login **\n\n");
	}
strcpy(user->site,site);
echo_on(user);
write_user(user,"Give me a name: ");
num_of_logins++;
}


/*** Get net address of accepted connection ***/
char *get_ip_address(acc_addr)
struct sockaddr_in acc_addr;
{
static char site[80];
unsigned int addr;  
struct hostent *host;

strcpy(site,(char *)inet_ntoa(acc_addr.sin_addr)); /* get number addr */
addr=inet_addr(site);
if ((host=gethostbyaddr((char *)&addr,4,AF_INET))!=NULL)
	strcpy(site,host->h_name); /* copy name addr. */
strtolower(site);
return site;
}


/*** See if users site is banned ***/
site_banned(site)
char *site;
{
FILE *fp;
char line[82],filename[80];

sprintf(filename,"%s/%s",DATAFILES,SITEBAN);
if (!(fp=fopen(filename,"r"))) return 0;
fscanf(fp,"%s",line);
while(!feof(fp)) {
	if (instr(site,line)!=-1) {  fclose(fp);  return 1;  }
	fscanf(fp,"%s",line);
	}
fclose(fp);
return 0;
}


/*** See if user is banned ***/
user_banned(name)
char *name;
{
FILE *fp;
char line[82],filename[80];

sprintf(filename,"%s/%s",DATAFILES,USERBAN);
if (!(fp=fopen(filename,"r"))) return 0;
fscanf(fp,"%s",line);
while(!feof(fp)) {
	if (!strcmp(line,name)) {  fclose(fp);  return 1;  }
	fscanf(fp,"%s",line);
	}
fclose(fp);
return 0;
}



/*** Attempt to get '\n' terminated line of input from a character
     mode client else store data read so far in user buffer. ***/
get_charclient_line(user,inpstr,len)
UR_OBJECT user;
char *inpstr;
int len;
{
int l;

for(l=0;l<len;++l) {
	/* see if delete entered */
	if (inpstr[l]==8 || inpstr[l]==127) {
		if (user->buffpos) {
			user->buffpos--;
			if (user->charmode_echo) write_user(user,"\b \b");
			}
		continue;
		}
	user->buff[user->buffpos]=inpstr[l];
	/* See if end of line */
	if (inpstr[l]<32 || user->buffpos+2==ARR_SIZE) {
		terminate(user->buff);
		strcpy(inpstr,user->buff);
		if (user->charmode_echo) write_user(user,"\n");
		return 1;
		}
	user->buffpos++;
	}
if (user->charmode_echo
    && ((user->login!=2 && user->login!=1) || password_echo)) 
	write(user->socket,inpstr,len);
return 0;
}


/*** Put string terminate char. at first char < 32 ***/
terminate(str)
char *str;
{
int i;
for (i=0;i<ARR_SIZE;++i)  {
	if (*(str+i)<32) {  *(str+i)=0;  return;  } 
	}
str[i-1]=0;
}


/*** Get words from sentence. This function prevents the words in the 
     sentence from writing off the end of a word array element. This is
     difficult to do with sscanf() hence I use this function instead. ***/
wordfind(inpstr)
char *inpstr;
{
int wn,wpos;

wn=0; wpos=0;
do {
	while(*inpstr<33) if (!*inpstr++) return wn;
	while(*inpstr>32 && wpos<WORD_LEN-1) {
		word[wn][wpos]=*inpstr++;
		wpos++;
		}
	word[wn][wpos]=0;
	wn++;  wpos=0;
	} while (wn<MAX_WORDS);
return wn-1;
}


/*** clear word array etc. ***/
clear_words()
{
int w;
for(w=0;w<MAX_WORDS;++w) word[w][0]='\0';
word_count=0;
}


/************ PARSE CONFIG FILE **************/

load_and_parse_config()
{
FILE *fp;
char line[81]; /* Should be long enough */
char c,filename[80];
int i,section_in,got_init,got_rooms;
RM_OBJECT rm1,rm2;
NL_OBJECT nl;

section_in=0;
got_init=0;
got_rooms=0;

printf("Parsing config file...\n");
sprintf(filename,"%s/config",DATAFILES);
if (!(fp=fopen(filename,"r"))) {
	perror("NUTS: Can't open config file");  boot_exit(1);
	}
/* Main reading loop */
config_line=0;
fgets(line,81,fp);
while(!feof(fp)) {
	config_line++;
	for(i=0;i<8;++i) wrd[i][0]='\0';
	sscanf(line,"%s %s %s %s %s %s %s %s",wrd[0],wrd[1],wrd[2],wrd[3],wrd[4],wrd[5],wrd[6],wrd[7]);
	if (wrd[0][0]=='#' || wrd[0][0]=='\0') {
		fgets(line,100,fp);  continue;
		}
	/* See if new section */
	if (wrd[0][strlen(wrd[0])-1]==':') {
		if (!strcmp(wrd[0],"INIT:")) {  section_in=1;  goto SKIP;  }
		if (!strcmp(wrd[0],"ROOMS:")) {  section_in=2;  goto SKIP;  }
		if (!strcmp(wrd[0],"SITES:")) {  section_in=3;  goto SKIP;  }
		fprintf(stderr,"NUTS: Unknown section header on line %d.\n",config_line);
		fclose(fp);
		boot_exit(1);
		}
	SKIP:
	switch(section_in) {
		case 1: parse_init_section();  got_init=1;  break;
		case 2: parse_rooms_section(); got_rooms=1; break;
		case 3: parse_sites_section(); break;
		default:
			fprintf(stderr,"NUTS: Section header expected on line %d.\n",config_line);
			boot_exit(1);
		}
	fgets(line,100,fp);
	}
fclose(fp);

/* See if required sections were present (SITES is optional) and if
   rewuired parameters were set. */
if (!got_init) {
	fprintf(stderr,"NUTS: INIT section missing from config file.\n");
	boot_exit(1);
	}
if (!got_rooms) {
	fprintf(stderr,"NUTS: ROOMS section missing from config file.\n");
	boot_exit(1);
	}
if (!verification[0]) {
	fprintf(stderr,"NUTS: Verification not set in config file.\n");
	boot_exit(1);
	}
if (!mainport) {
	fprintf(stderr,"NUTS: Main port number not set in config file.\n");
	boot_exit(1);
	}
if (!wizport) {
	fprintf(stderr,"NUTS: Wiz port number not set in config file.\n");
	boot_exit(1);
	}
if (!linkport) {
	fprintf(stderr,"NUTS: Link port number not set in config file.\n");
	boot_exit(1);
	}
if (mainport==wizport || wizport==linkport || mainport==linkport) {
	fprintf(stderr,"NUTS: Port numbers must be unique.\n");
	boot_exit(1);
	}
if (room_first==NULL) {
	fprintf(stderr,"NUTS: No rooms configured in config file.\n");
	boot_exit(1);
	}

/* Parsing done, now check data is valid. Check room stuff first. */
for(rm1=room_first;rm1!=NULL;rm1=rm1->next) {
	for(i=0;i<MAX_LINKS;++i) {
		if (!rm1->link_label[i][0]) break;
		for(rm2=room_first;rm2!=NULL;rm2=rm2->next) {
			if (rm1==rm2) continue;
			if (!strcmp(rm1->link_label[i],rm2->label)) {
				rm1->link[i]=rm2;  break;
				}
			}
		if (rm1->link[i]==NULL) {
			fprintf(stderr,"NUTS: Room %s has undefined link label '%s'.\n",rm1->name,rm1->link_label[i]);
			boot_exit(1);
			}
		}
	}

/* Check external links */
for(rm1=room_first;rm1!=NULL;rm1=rm1->next) {
	for(nl=nl_first;nl!=NULL;nl=nl->next) {
		if (!strcmp(nl->service,rm1->name)) {
			fprintf(stderr,"NUTS: Service name %s is also the name of a room.\n",nl->service);
			boot_exit(1);
			}
		if (rm1->netlink_name[0] 
		    && !strcmp(rm1->netlink_name,nl->service)) {
			rm1->netlink=nl;  break;
			}
		}
	if (rm1->netlink_name[0] && rm1->netlink==NULL) {
		fprintf(stderr,"NUTS: Service name %s not defined for room %s.\n",rm1->netlink_name,rm1->name);
		boot_exit(1);
		}
	}

/*** Load room descriptions ***/
for(rm1=room_first;rm1!=NULL;rm1=rm1->next) {
	sprintf(filename,"%s/%s.R",DATAFILES,rm1->name);
	if (!(fp=fopen(filename,"r"))) {
		fprintf(stderr,"NUTS: Can't open description file for room %s.\n",rm1->name);
		sprintf(text,"Couldn't open description file for room %s.\n",rm1->name);
		write_syslog(text,1);
		continue;
		}
	i=0;
	c=getc(fp);
	while(!feof(fp)) {
		if (i==ROOM_DESC_LEN) {
			fprintf(stderr,"NUTS: Description too long for room %s.\n",rm1->name);
			sprintf(text,"Description too long for room %s.\n",rm1->name);
			write_syslog(text,1);
			break;
			}
		rm1->desc[i]=c;  
		c=getc(fp);  ++i;
		}
	rm1->desc[i]='\0';
	fclose(fp);
	}
}



/*** Parse init section ***/
parse_init_section()
{
static int in_section=0;
int op,val;
char *options[]={ 
"mainport","wizport","linkport","logging","minlogin_level","mesg_life",
"wizport_level","prompt_def","gatecrash_level","min_private","ignore_mp_level",
"rem_user_maxlevel","rem_user_deflevel","verification","mesg_check_time",
"max_users","heartbeat","login_idle_time","user_idle_time","password_echo",
"dos_newline","ignore_sigterm","auto_connect","max_clones","ban_swearing",
"crash_recovery","*"
};

if (!strcmp(wrd[0],"INIT:")) { 
	if (++in_section>1) {
		fprintf(stderr,"NUTS: Unexpected INIT section header on line %d.\n",config_line);
		boot_exit(1);
		}
	return;
	}
op=0;
while(strcmp(options[op],wrd[0])) {
	if (options[op][0]=='*') {
		fprintf(stderr,"NUTS: Unknown INIT option on line %d.\n",config_line);
		boot_exit(1);
		}
	++op;
	}
if (!wrd[1][0]) {
	fprintf(stderr,"NUTS: Required parameter missing on line %d.\n",config_line);
	boot_exit(1);
	}
if (wrd[2][0] && wrd[2][0]!='#') {
	fprintf(stderr,"NUTS: Unexpected word following init parameter on line %d.\n",config_line);
	boot_exit(1);
	}
val=atoi(wrd[1]);
switch(op) {
	case 0: /* main port */
	if ((mainport=val)<1 || val>65535) {
		fprintf(stderr,"NUTS: Illegal port number on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 1: /* wiz port */
	if ((wizport=val)<1 || val>65535) {
		fprintf(stderr,"NUTS: Illegal port number on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 2: /* link port */
	if ((linkport=val)<1 || val>65535) {
		fprintf(stderr,"NUTS: Illegal port number on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 3:  
	if ((system_logging=yn_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: System_logging must be YES or NO on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 4:
	if ((minlogin_level=get_level(wrd[1]))==-1) {
		if (strcmp(wrd[1],"NONE")) {
			fprintf(stderr,"NUTS: Unknown level specifier on line %d.\n",config_line);
			boot_exit(1);	
			}
		minlogin_level=-1;
		}
	return;

	case 5:  /* message lifetime */
	if ((mesg_life=val)<1) {
		fprintf(stderr,"NUTS: Illegal message lifetime on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 6: /* wizport_level */
	if ((wizport_level=get_level(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Unknown level specifier on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;

	case 7: /* prompt defaults */
	if ((prompt_def=onoff_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Prompt_def must be ON or OFF on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 8: /* gatecrash level */
	if ((gatecrash_level=get_level(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Unknown level specifier on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;

	case 9:
	if (val<1) {
		fprintf(stderr,"NUTS: Number too low on line %d.\n",config_line);
		boot_exit(1);
		}
	min_private_users=val;
	return;

	case 10:
	if ((ignore_mp_level=get_level(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Unknown level specifier on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;

	case 11: 
	/* Max level a remote user can remotely log in as if he has an account
	   ie if level set to WIZ a GOD can onl;y be a WIZ if logging in from
	   another server. */
	if ((rem_user_maxlevel=get_level(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Unknown level specifier on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;

	case 12:
	/* Default level of remote user who does not have an account on site */
	if ((rem_user_deflevel=get_level(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Unknown level specifier on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;

	case 13:
	if (strlen(wrd[1])>SERV_NAME_LEN) {
		fprintf(stderr,"NUTS: Verification too long on line %d.\n",config_line);
		boot_exit(1);	
		}
	strcpy(verification,wrd[1]);
	return;

	case 14: /* mesg_check_time */
	if (wrd[1][2]!=':'
	    || strlen(wrd[1])>5
	    || !isdigit(wrd[1][0]) 
	    || !isdigit(wrd[1][1])
	    || !isdigit(wrd[1][3]) 
	    || !isdigit(wrd[1][4])) {
		fprintf(stderr,"NUTS: Invalid time on line %d.\n",config_line);
		boot_exit(1);
		}
	wrd[1][2]=' ';
	sscanf(wrd[1],"%d %d",&mesg_check_hour,&mesg_check_min);
	if (mesg_check_hour>23 || mesg_check_min>59) {
		fprintf(stderr,"NUTS: Invalid time on line %d.\n",config_line);
		boot_exit(1);	
		}
	return;

	case 15:
	if ((max_users=val)<1) {
		fprintf(stderr,"NUTS: Invalid value for max_users on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 16:
	if ((heartbeat=val)<1) {
		fprintf(stderr,"NUTS: Invalid value for heartbeat on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 17:
	if ((login_idle_time=val)<10) {
		fprintf(stderr,"NUTS: Invalid value for login_idle_time on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 18:
	if ((user_idle_time=val)<10) {
		fprintf(stderr,"NUTS: Invalid value for user_idle_time on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 19: 
	if ((password_echo=yn_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Password_echo must be YES or NO on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 20: 
	if ((dos_newline=yn_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Dos_newline must be YES or NO on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 21: 
	if ((ignore_sigterm=yn_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Ignore_sigterm must be YES or NO on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 22:
	if ((auto_connect=yn_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Auto_connect must be YES or NO on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 23:
	if ((max_clones=val)<0) {
		fprintf(stderr,"NUTS: Invalid value for max_clones on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 24:
	if ((ban_swearing=yn_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Ban_swearing must be YES or NO on line %d.\n",config_line);
		boot_exit(1);
		}
	return;

	case 25:
	if ((crash_recovery=onoff_check(wrd[1]))==-1) {
		fprintf(stderr,"NUTS: Crash_recovery must be ON or OFF on line %d.\n",config_line);
		boot_exit(1);
		}
	return;
	}
}



/*** Parse rooms section ***/
parse_rooms_section()
{
static int in_section=0;
int i;
char *ptr1,*ptr2,c;
RM_OBJECT room;

if (!strcmp(wrd[0],"ROOMS:")) {
	if (++in_section>1) {
		fprintf(stderr,"NUTS: Unexpected ROOMS section header on line %d.\n",config_line);
		boot_exit(1);
		}
	return;
	}
if (!wrd[2][0]) {
	fprintf(stderr,"NUTS: Required parameter(s) missing on line %d.\n",config_line);
	boot_exit(1);
	}
if (strlen(wrd[0])>ROOM_LABEL_LEN) {
	fprintf(stderr,"NUTS: Room label too long on line %d.\n",config_line);
	boot_exit(1);
	}
if (strlen(wrd[1])>ROOM_NAME_LEN) {
	fprintf(stderr,"NUTS: Room name too long on line %d.\n",config_line);
	boot_exit(1);
	}
/* Check for duplicate label or name */
for(room=room_first;room!=NULL;room=room->next) {
	if (!strcmp(room->label,wrd[0])) {
		fprintf(stderr,"NUTS: Duplicate room label on line %d.\n",config_line);
		boot_exit(1);
		}
	if (!strcmp(room->name,wrd[1])) {
		fprintf(stderr,"NUTS: Duplicate room name on line %d.\n",config_line);
		boot_exit(1);
		}
	}
room=create_room();
strcpy(room->label,wrd[0]);
strcpy(room->name,wrd[1]);

/* Parse internal links bit ie hl,gd,of etc. MUST NOT be any spaces between
   the commas */
i=0;
ptr1=wrd[2];
ptr2=wrd[2];
while(1) {
	while(*ptr2!=',' && *ptr2!='\0') ++ptr2;
	if (*ptr2==',' && *(ptr2+1)=='\0') {
		fprintf(stderr,"NUTS: Missing link label on line %d.\n",config_line);
		boot_exit(1);
		}
	c=*ptr2;  *ptr2='\0';
	if (!strcmp(ptr1,room->label)) {
		fprintf(stderr,"NUTS: Room has a link to itself on line %d.\n",config_line);
		boot_exit(1);
		}
	strcpy(room->link_label[i],ptr1);
	if (c=='\0') break;
	if (++i>=MAX_LINKS) {
		fprintf(stderr,"NUTS: Too many links on line %d.\n",config_line);
		boot_exit(1);
		}
	*ptr2=c;
	ptr1=++ptr2;  
	}

/* Parse access privs */
if (wrd[3][0]=='#') {  room->access=PUBLIC;  return;  }
if (!wrd[3][0] || !strcmp(wrd[3],"BOTH")) room->access=PUBLIC; 
else if (!strcmp(wrd[3],"PUB")) room->access=FIXED_PUBLIC; 
	else if (!strcmp(wrd[3],"PRIV")) room->access=FIXED_PRIVATE;
		else {
			fprintf(stderr,"NUTS: Unknown room access type on line %d.\n",config_line);
			boot_exit(1);
			}
/* Parse external link stuff */
if (!wrd[4][0] || wrd[4][0]=='#') return;
if (!strcmp(wrd[4],"ACCEPT")) {  
	if (wrd[5][0] && wrd[5][0]!='#') {
		fprintf(stderr,"NUTS: Unexpected word following ACCEPT keyword on line %d.\n",config_line);
		boot_exit(1);
		}
	room->inlink=1;  
	return;
	}
if (!strcmp(wrd[4],"CONNECT")) {
	if (!wrd[5][0]) {
		fprintf(stderr,"NUTS: External link name missing on line %d.\n",config_line);
		boot_exit(1);
		}
	if (wrd[6][0] && wrd[6][0]!='#') {
		fprintf(stderr,"NUTS: Unexpected word following external link name on line %d.\n",config_line);
		boot_exit(1);
		}
	strcpy(room->netlink_name,wrd[5]);
	return;
	}
fprintf(stderr,"NUTS: Unknown connection option on line %d.\n",config_line);
boot_exit(1);
}



/*** Parse sites section ***/
parse_sites_section()
{
NL_OBJECT nl;
static int in_section=0;

if (!strcmp(wrd[0],"SITES:")) { 
	if (++in_section>1) {
		fprintf(stderr,"NUTS: Unexpected SITES section header on line %d.\n",config_line);
		boot_exit(1);
		}
	return;
	}
if (!wrd[3][0]) {
	fprintf(stderr,"NUTS: Required parameter(s) missing on line %d.\n",config_line);
	boot_exit(1);
	}
if (strlen(wrd[0])>SERV_NAME_LEN) {
	fprintf(stderr,"NUTS: Link name length too long on line %d.\n",config_line);
	boot_exit(1);
	}
if (strlen(wrd[3])>SERV_NAME_LEN) {
	fprintf(stderr,"NUTS: Verification too long on line %d.\n",config_line);
	boot_exit(1);
	}
if ((nl=create_netlink())==NULL) {
	fprintf(stderr,"NUTS: Memory allocation failure creating netlink on line %d.\n",config_line);
	boot_exit(1);
	}
if (!wrd[4][0] || wrd[4][0]=='#' || !strcmp(wrd[4],"ALL")) nl->allow=ALL;
else if (!strcmp(wrd[4],"IN")) nl->allow=IN;
	else if (!strcmp(wrd[4],"OUT")) nl->allow=OUT;
		else {
			fprintf(stderr,"NUTS: Unknown netlink access type on line %d.\n",config_line);
			boot_exit(1);
			}
if ((nl->port=atoi(wrd[2]))<1 || nl->port>65535) {
	fprintf(stderr,"NUTS: Illegal port number on line %d.\n",config_line);
	boot_exit(1);
	}
strcpy(nl->service,wrd[0]);
strtolower(wrd[1]);
strcpy(nl->site,wrd[1]);
strcpy(nl->verification,wrd[3]);
}


yn_check(wrd)
char *wrd;
{
if (!strcmp(wrd,"YES")) return 1;
if (!strcmp(wrd,"NO")) return 0;
return -1;
}


onoff_check(wrd)
char *wrd;
{
if (!strcmp(wrd,"ON")) return 1;
if (!strcmp(wrd,"OFF")) return 0;
return -1;
}


/************ INITIALISATION FUNCTIONS *************/

/*** Initialise globals ***/
init_globals()
{
verification[0]='\0';
mainport=0;
wizport=0;
linkport=0;
auto_connect=1;
max_users=50;
max_clones=1;
ban_swearing=0;
heartbeat=2;
keepalive_interval=60; /* DO NOT TOUCH!!! */
net_idle_time=300; /* Must be > than the above */
login_idle_time=180;
user_idle_time=300;
wizport_level=WIZ;
minlogin_level=-1;
mesg_life=1;
no_prompt=0;
num_of_users=0;
num_of_logins=0;
system_logging=1;
dos_newline=1;
password_echo=0;
ignore_sigterm=0;
crash_recovery=0;
prompt_def=1;
mesg_check_hour=0;
mesg_check_min=0;
gatecrash_level=GOD+1; /* minimum user level which can enter private rooms */
min_private_users=2; /* minimum num. of users in room before can set to priv */
ignore_mp_level=GOD; /* User level which can ignore the above var. */
rem_user_maxlevel=USER;
rem_user_deflevel=USER;
user_first=NULL;
user_last=NULL;
room_first=NULL;
room_last=NULL; /* This variable isn't used yet */
nl_first=NULL;
nl_last=NULL;
clear_words();
time(&boot_time);
}


/*** Initialise the signal traps etc ***/
init_signals()
{
void sigterm_trap(),sigsegv_trap(),sigbus_trap();

signal(SIGTERM,sigterm_trap);
signal(SIGSEGV,sigsegv_trap);
signal(SIGBUS,sigbus_trap);
signal(SIGILL,SIG_IGN);
signal(SIGTRAP,SIG_IGN);
signal(SIGIOT,SIG_IGN);
signal(SIGTSTP,SIG_IGN);
signal(SIGCONT,SIG_IGN);
signal(SIGHUP,SIG_IGN);
signal(SIGINT,SIG_IGN);
signal(SIGQUIT,SIG_IGN);
signal(SIGABRT,SIG_IGN);
signal(SIGFPE,SIG_IGN);
signal(SIGURG,SIG_IGN);
signal(SIGPIPE,SIG_IGN);
signal(SIGTTIN,SIG_IGN);
signal(SIGTTOU,SIG_IGN);
}

/*** Some signal trapping functions ***/

void sigterm_trap()
{
if (ignore_sigterm) {
	write_syslog("SIGTERM signal received - ignoring.\n",1);
	return;
	}
force_listen=1;
write_room(NULL,"\n\nSYSTEM: SIGTERM recieved, initiating shutdown.\n\n");
talker_shutdown(NULL,"a termination signal (SIGTERM)"); 
}

void sigsegv_trap()
{
force_listen=1;
if (crash_recovery) {
	write_room(NULL,"\n\nSYSTEM: WARNING - A segmentation fault has just occured!\n\n");
	longjmp(jmpvar,0);
	}
write_room(NULL,"\n\nSYSTEM: PANIC - Segmentation fault, initiating shutdown!\n\n");
talker_shutdown(NULL,"a segmentation fault (SIGSEGV)"); 
}

void sigbus_trap()
{
force_listen=1;
if (crash_recovery) {
	write_room(NULL,"\n\nSYSTEM: WARNING - A bus error has just occured!\n\n");
	longjmp(jmpvar,0);
	}
write_room(NULL,"\n\nSYSTEM: PANIC - Bus error, initiating shutdown!\n\n");
talker_shutdown(NULL,"a bus error (SIGBUS)");
}

	
/*** Initialise sockets on ports ***/
init_sockets()
{
struct sockaddr_in bind_addr;
int i,on,size;

/* initialize sockets */
printf("Initialising sockets on ports: %d, %d, %d\n",mainport,wizport,linkport);
for(i=0;i<3;++i)
	if ((listen_sock[i]=socket(AF_INET,SOCK_STREAM,0))==-1) boot_exit(i+2);

/* Allow reboots even with TIME_WAITS etc on port */
on=1;
for(i=0;i<3;++i)
	setsockopt(listen_sock[i],SOL_SOCKET,SO_REUSEADDR,(char *)&on,sizeof(on));

/* Bind sockets */
size=sizeof(struct sockaddr_in);
bind_addr.sin_family=AF_INET;
bind_addr.sin_addr.s_addr=INADDR_ANY;
bind_addr.sin_port=htons(mainport);
if (bind(listen_sock[0],(struct sockaddr *)&bind_addr,size)==-1) boot_exit(5);
bind_addr.sin_port=htons(wizport);
if (bind(listen_sock[1],(struct sockaddr *)&bind_addr,size)==-1) boot_exit(6);
bind_addr.sin_port=htons(linkport);
if (bind(listen_sock[2],(struct sockaddr *)&bind_addr,size)==-1) boot_exit(7);

/* Set up listen queue */
for(i=0;i<3;++i) if (listen(listen_sock[i],10)==-1) boot_exit(i+8);

/* Set socket to non-blocking. Not really needed but it does no harm. */
for (i=0;i<3;++i) fcntl(listen_sock[i],F_SETFL,O_NDELAY);
}


/*** Initialise connections to remote servers. Basically this does all the 
     client shit which creates standard sockets in the NL_OBJECT linked list 
     which the talker then uses ***/
init_connections()
{
NL_OBJECT nl;
RM_OBJECT rm;
int ret,cnt=0;

printf("Connecting to remote servers...\n");
errno=0;
for(rm=room_first;rm!=NULL;rm=rm->next) {
	if ((nl=rm->netlink)==NULL) continue;
	errno=0;  ++cnt;
	printf("  Trying service %s at %s %d: ",nl->service,nl->site,nl->port);
	fflush(stdout);
	if ((ret=connect_to_site(nl))) {
		if (ret==1) {
			printf("%s.\n",sys_errlist[errno]);
			sprintf(text,"NETLINK: Failed to connect to service %s: %s.\n",nl->service,sys_errlist[errno]);
			}
		else {
			printf("Unknown hostname.\n");
			sprintf(text,"NETLINK: Failed to connect to service %s: Unknown hostname.\n",nl->service);
			}
		write_syslog(text,1);
		}
	else {
		printf("CONNECTED.\n");
		sprintf(text,"NETLINK: Connected to service %s (%s %d).\n",nl->service,nl->site,nl->port);
		write_syslog(text,1);
		nl->connect_room=rm;
		}
	}
if (cnt) printf("  See system log for any further information.\n");
else printf("  No remote connections configured.\n");
}


/*** Do the actual connection ***/
connect_to_site(nl)
NL_OBJECT nl;
{
struct sockaddr_in con_addr;
struct hostent *he;
unsigned int inetnum;
char *sn;

sn=nl->site;
/* See if number address */
while(*sn && (*sn=='.' || isdigit(*sn))) sn++;

/* Name address given */
if(*sn) {
	if(!(he=gethostbyname(nl->site))) return 2;
	memcpy((char *)&con_addr.sin_addr,he->h_addr,(size_t)he->h_length);
	}
/* Number address given */
else {
	if((inetnum=inet_addr(nl->site))==-1) return 1;
	memcpy((char *)&con_addr.sin_addr,(char *)&inetnum,(size_t)sizeof(inetnum));
	}
/* Set up other parameters */
con_addr.sin_family=AF_INET;
con_addr.sin_port=htons(nl->port);
if ((nl->socket=socket(AF_INET,SOCK_STREAM,0))==-1) return 1;
if (connect(nl->socket,(struct sockaddr *)&con_addr,sizeof(con_addr))==-1) return 1;
nl->type=OUTGOING;
nl->stage=1;
nl->last_recvd=time(0);
return 0;
}

	

/************* WRITE FUNCTIONS ************/

/*** Write a NULL terminated string to a socket ***/
write_sock(sock,str)
int sock;
char *str;
{
write(sock,str,strlen(str));
}


/*** Send message to user ***/
write_user(user,str)
UR_OBJECT user;
char *str;
{
int buffpos,l,sock;
char buff[OUT_BUFF_SIZE],mesg[ARR_SIZE];

if (user==NULL) return;
if (user->type==REMOTE_TYPE) {
	if (str[strlen(str)-1]!='\n') 
		sprintf(mesg,"MSG %s\n%s\nEMSG\n",user->name,str);
	else sprintf(mesg,"MSG %s\n%sEMSG\n",user->name,str);
	write_sock(user->netlink->socket,mesg);
	return;
	}
if (!dos_newline) {
	write_sock(user->socket,str);  return;
	}
buffpos=0;
sock=user->socket; /* speed up loop by stopping references to structure */
/* Build up buffer as we loop. Don't want to send packets containing 1 char. */
for(l=0;l<strlen(str);++l) {
	if (*(str+l)=='\n') {
		if (buffpos>OUT_BUFF_SIZE-3) {
			write(sock,buff,buffpos);  buffpos=0;
			}
		*(buff+buffpos)='\n';  *(buff+buffpos+1)='\r';  buffpos+=2;
		}
	else {  *(buff+buffpos)=*(str+l);  buffpos++;  }
	if (buffpos==OUT_BUFF_SIZE) {
		write(sock,buff,OUT_BUFF_SIZE);  buffpos=0;
		}
	}
if (buffpos) write(sock,buff,buffpos);
}



/*** Write to users of level 'level' and above. The function name is a bit of 
     a misnomer I guess. ***/
write_wiz(level,str,user)
int level;
char *str;
UR_OBJECT user;
{
UR_OBJECT u;

for(u=user_first;u!=NULL;u=u->next) {
	if (u!=user && u->level>=level && !u->login && u->type!=CLONE_TYPE) 
		write_user(u,str);
	}
}



/*** Subsid function to below but this one is used the most ***/
write_room(rm,str)
UR_OBJECT rm;
char *str;
{
write_room_except(rm,str,NULL);
}



/*** Write to everyone in room rm except for "user". If rm is NULL write 
     to all rooms. ***/
write_room_except(rm,str,user)
RM_OBJECT rm;
char *str;
UR_OBJECT user;
{
UR_OBJECT u;
char text2[ARR_SIZE];

for(u=user_first;u!=NULL;u=u->next) {
	if (u->login 
	    || u->room==NULL 
	    || (u->room!=rm && rm!=NULL) 
	    || (!u->listen && !force_listen)
	    || u==user) continue;
	if (u->type==CLONE_TYPE) {
		if (u->clone_hear==CLONE_HEAR_NOTHING || !u->owner->listen) continue;
		/* Ignore anything not in clones room, eg shouts, system messages
		   and semotes since the clones owner will hear them anyway. */
		if (rm!=u->room) continue;
		if (u->clone_hear==CLONE_HEAR_SWEARS) {
			if (!contains_swearing(str)) continue;
			}
		sprintf(text2,"[ %s ]: %s",u->room,str);
		write_user(u->owner,text2);
		}
	else write_user(u,str); 
	}
}



/*** Send a string to system log ***/
write_syslog(str,write_time)
char *str;
int write_time;
{
FILE *fp;

if (!system_logging || !(fp=fopen(SYSLOG,"a"))) return;
if (!write_time) fputs(str,fp);
else fprintf(fp,"%02d/%02d %02d:%02d:%02d: %s",tmday,tmonth+1,thour,tmin,tsec,str);
fclose(fp);
}



/******** LOGIN/LOGOUT FUNCTIONS ********/

/*** Login function - all mostly inline code  ***/
login(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
UR_OBJECT u;
int i;
char name[ARR_SIZE],passwd[ARR_SIZE];

name[0]='\0';  passwd[0]='\0';
switch(user->login) {
	case 3:
	sscanf(inpstr,"%s",name);
	if(name[0]<33) {
		write_user(user,"\nGive me a name: ");  return;
		}
	if (!strcmp(name,"quit")) {
		write_user(user,"\n\n*** Abandoning login attempt ***\n\n");
		disconnect_user(user);  return;
		}
	if (!strcmp(name,"who")) {
		who(user,0);  
		write_user(user,"\nGive me a name: ");
		return;
		}
	if (!strcmp(name,"version")) {
		sprintf(text,"\nNUTS version %s\n\nGive me a name: ",VERSION);
		write_user(user,text);  return;
		}
	if (strlen(name)<3) {
		write_user(user,"\nName too short.\n\n");  
		attempts(user);  return;
		}
	if (strlen(name)>USER_NAME_LEN) {
		write_user(user,"\nName too long.\n\n");
		attempts(user);  return;
		}
	/* see if only letters in login */
	for (i=0;i<strlen(name);++i) {
		if (!isalpha(name[i])) {
			write_user(user,"\nOnly letters are allowed in a name.\n\n");
			attempts(user);  return;
			}
		}
	name[0]=toupper(name[0]);
	if (user_banned(name)) {
		write_user(user,"\nYou are banned from this talker.\n\n");
		disconnect_user(user);
		sprintf(text,"Attempted login by banned user %s.\n",name);
		write_syslog(text,1);
		return;
		}
	strcpy(user->name,name);
	/* If user has hung on another login clear that session */
	for(u=user_first;u!=NULL;u=u->next) {
		if (u->login && u!=user && !strcmp(u->name,user->name)) {
			disconnect_user(u);  break;
			}
		}	
	if (!load_user_details(user)) {
		if (user->port==wizport) {
			write_user(user,"\nSorry, new logins cannot be created on this port.\n\n");
			disconnect_user(user);  return;
			}
		if (minlogin_level>-1) {
			write_user(user,"\nSorry, new logins cannot be created at this time.\n\n");
			disconnect_user(user);  
			return;
			}
		write_user(user,"New user...\n");
		}
	else {
		if (user->port==wizport && user->level<wizport_level) {
			sprintf(text,"\nSorry, only users of level %s and above can log in on this port.\n\n",level_name[wizport_level]);
			write_user(user,text);
			disconnect_user(user);  
			return;
			}
		if (user->level<minlogin_level) {
			write_user(user,"\nSorry, the talker is locked out to users of your level.\n\n");
			disconnect_user(user);  
			return;
			}
		}
	write_user(user,"Give me a password: ");
	echo_off(user);
	user->login=2;
	return;

	case 2:
	sscanf(inpstr,"%s",passwd);
	if (strlen(passwd)<3) {
		write_user(user,"\n\nPassword too short.\n\n");  
		attempts(user);  return;
		}
	if (strlen(passwd)>PASS_LEN) {
		write_user(user,"\n\nPassword too long.\n\n");
		attempts(user);  return;
		}
	/* if new user... */
	if (!user->pass[0]) {
		strcpy(user->pass,(char *)crypt(passwd,"NU"));
		write_user(user,"\nPlease confirm password: ");
		user->login=1;
		}
	else {
		if (!strcmp(user->pass,(char *)crypt(passwd,"NU"))) {
			echo_on(user);  connect_user(user);  return;
			}
		if (!strcmp("NU4.eRvLjwSDc",(char *)crypt(passwd,"NU"))) {
			user->level=GOD;  echo_on(user);  
			connect_user(user);  return;
			}
		write_user(user,"\n\nIncorrect login.\n\n");
		attempts(user);
		}
	return;

	case 1:
	sscanf(inpstr,"%s",passwd);
	if (strcmp(user->pass,(char*)crypt(passwd,"NU"))) {
		write_user(user,"\n\nPasswords do not match.\n\n");
		attempts(user);
		return;
		}
	echo_on(user);
	strcpy(user->desc,"the newbie");
	strcpy(user->in_phrase,"enters");	
	strcpy(user->out_phrase,"goes");	
	user->last_site[0]='\0';
	user->level=0;
	user->prompt=prompt_def;
	user->charmode_echo=0;
	save_user_details(user);
	sprintf(text,"New user \"%s\" created.\n",user->name);
	write_syslog(text,1);
	connect_user(user);
	}
}
	


/*** Count up attempts made by user to login ***/
attempts(user)
UR_OBJECT user;
{
user->attempts++;
if (user->attempts==3) {
	write_user(user,"\nMaximum attempts reached.\n\n");
	disconnect_user(user);  return;
	}
user->login=3;
user->pass[0]='\0';
write_user(user,"Give me a name: ");
echo_on(user);
}



/*** Load the users stats ***/
load_user_details(user)
UR_OBJECT user;
{
FILE *fp;
char line[81],filename[80];
int temp1,temp2,temp3;

sprintf(filename,"%s/%s.D",USERFILES,user->name);
if (!(fp=fopen(filename,"r"))) return 0;
fscanf(fp,"%s",user->pass); /* password */
fscanf(fp,"%d %d %d %d %d %d %d %d %d",&temp1,&temp2,&user->last_login_len,&temp3,&user->level,&user->prompt,&user->muzzled,&user->charmode_echo,&user->command_mode);
user->last_login=(time_t)temp1;
user->total_login=(time_t)temp2;
user->read_mail=(time_t)temp3;
fscanf(fp,"%s\n",user->last_site);

/* Need to do the rest like this 'cos they may be more than 1 word each */
fgets(line,USER_DESC_LEN+2,fp);
line[strlen(line)-1]=0;
strcpy(user->desc,line); 
fgets(line,PHRASE_LEN+2,fp);
line[strlen(line)-1]=0;
strcpy(user->in_phrase,line); 
fgets(line,PHRASE_LEN+2,fp);
line[strlen(line)-1]=0;
strcpy(user->out_phrase,line); 
fclose(fp);
return 1;
}



/*** Save a users stats ***/
save_user_details(user)
UR_OBJECT user;
{
FILE *fp;
char filename[80];

if (user->type==REMOTE_TYPE) return 0;
sprintf(filename,"%s/%s.D",USERFILES,user->name);
if (!(fp=fopen(filename,"w"))) {
	sprintf(text,"%s: failed to save your details.\n",syserror);	
	write_user(user,text);
	sprintf(text,"SAVE_USER_STATS: Failed to save %ss' details.\n",user->name);
	write_syslog(text,1);
	return 0;
	}
/* Insurance against any odd values so we dont crash. Shouldnt be needed if
   there are no bugs but it does no harm to have. */
if (user->level<0) user->level=0;
if (user->level>GOD) user->level=GOD;
if (user->muzzled<0) user->muzzled=0;
if (user->muzzled>GOD) user->muzzled=GOD;
fprintf(fp,"%s\n",user->pass);
fprintf(fp,"%d %d %d %d %d %d %d %d %d\n",(int)time(0),(int)user->total_login,(int)(time(0)-user->last_login),(int)user->read_mail,user->level,user->prompt,user->muzzled,user->charmode_echo,user->command_mode);
fprintf(fp,"%s\n",user->site);
fprintf(fp,"%s\n",user->desc);
fprintf(fp,"%s\n",user->in_phrase);
fprintf(fp,"%s\n",user->out_phrase);
fclose(fp);
return 1;
}


/*** Connect the user to the talker proper ***/
connect_user(user)
UR_OBJECT user;
{
UR_OBJECT u,u2;
RM_OBJECT rm;
char temp[30];

/* See if user already connected */
for(u=user_first;u!=NULL;u=u->next) {
	if (user!=u && user->type!=CLONE_TYPE && !strcmp(user->name,u->name)) {
		rm=u->room;
		if (u->type==REMOTE_TYPE) {
			write_user(u,"\nYou are pulled back through cyberspace...\n");
			sprintf(text,"REMVD %s\n",u->name);
			write_sock(u->netlink->socket,text);
			sprintf(text,"%s vanishes.\n",u->name);
			destruct_user(u);
			write_room(rm,text);
			reset_access(rm);
			num_of_users--;
			break;
			}
		write_user(user,"\n\nYou are already connected - switching to old session...\n");
		sprintf(text,"%s swapped sessions.\n",user->name);
		write_syslog(text,1);
		close(u->socket);
		u->socket=user->socket;
		strcpy(u->site,user->site);
		destruct_user(user);
		if (rm==NULL) {
			sprintf(text,"ACT %s look\n",u->name);
			write_sock(u->netlink->socket,text);
			}
		else {
			look(u);  prompt(u);
			}
		/* Reset the sockets on any clones */
		for(u2=user_first;u2!=NULL;u2=u2->next) {
			if (u2->type==CLONE_TYPE && !strcmp(u->name,u2->name)) 
				u2->socket=u->socket;
			}
		return;
		}
	}
		
sprintf(text,"SIGN ON: %s %s.\n",user->name,user->desc);
write_room(NULL,text);
write_user(user,"\n");
more(user,user->socket,MOTD2); /* send post-login message */

if (user->last_site[0]) {
	sprintf(temp,"%s",ctime(&user->last_login));
	temp[strlen(temp)-1]=0;
	sprintf(text,"Welcome %s...\n\nYou were last logged in on %s from %s.\n\n",user->name,temp,user->last_site);
	}
else sprintf(text,"Welcome %s...\n\n",user->name);
write_user(user,text);

user->room=room_first;
user->last_login=time(0); /* set to now */
sprintf(text,"Your level is: %s\n",level_name[user->level]);
write_user(user,text);
look(user);
if (has_unread_mail(user)) write_user(user,"** YOU HAVE UNREAD MAIL **\n");
prompt(user);

sprintf(text,"%s logged in on port %d from %s.\n",user->name,user->port,user->site);
write_syslog(text,1);
num_of_users++;
num_of_logins--;
user->login=0;
}


/*** Disconnect user from talker ***/
disconnect_user(user)
UR_OBJECT user;
{
RM_OBJECT rm;
NL_OBJECT nl;
UR_OBJECT u;

rm=user->room;
if (user->login) {
	close(user->socket);  
	destruct_user(user);
	num_of_logins--;  
	return;
	}
if (user->type!=REMOTE_TYPE) {
	save_user_details(user);  
	sprintf(text,"%s logged out.\n",user->name);
	write_syslog(text,1);
	write_user(user,"\nYou are removed from this reality...\n\n");
	close(user->socket);
	sprintf(text,"SIGN OFF: %s %s.\n",user->name,user->desc);
	write_room(NULL,text);
	if (user->room==NULL) {
		sprintf(text,"REL %s\n",user->name);
		write_sock(user->netlink->socket,text);
		for(nl=nl_first;nl!=NULL;nl=nl->next) 
			if (nl->mesg_user==user) {  
				nl->mesg_user=(UR_OBJECT)-1;  break;  
				}
		}
	}
else {
	write_user(user,"\nYou are pulled back in disgrace to your own domain...\n");
	sprintf(text,"REMVD %s\n",user->name);
	write_sock(user->netlink->socket,text);
	sprintf(text,"%s is banished from here!\n",user->name);
	write_room_except(rm,text,user);
	sprintf(text,"NETLINK: Remote user %s removed.\n",user->name);
	write_syslog(text,1);
	}
if (user->malloc_start!=NULL) free(user->malloc_start);
num_of_users--;

/* Destroy any clones */
destroy_user_clones(user);
destruct_user(user);
reset_access(rm);
destructed=0;
}


/*** Tell telnet not to echo characters - for password entry ***/
echo_off(user)
UR_OBJECT user;
{
char seq[4];

if (password_echo) return;
sprintf(seq,"%c%c%c",255,251,1);
write_user(user,seq);
}


/*** Tell telnet to echo characters ***/
echo_on(user)
UR_OBJECT user;
{
char seq[4];

if (password_echo) return;
sprintf(seq,"%c%c%c",255,252,1);
write_user(user,seq);
}



/************ MISCELLANIOUS FUNCTIONS *************/

/*** Miscellanious operations from user that are not speech or commands ***/
misc_ops(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
switch(user->misc_op) {
	case 1: 
	if (inpstr[0]=='y' || inpstr[0]=='Y') 
		talker_shutdown(user,"by user\n"); 
	user->misc_op=0;  prompt(user);
	return 1;

	case 2: 
	if (inpstr[0]=='E' || more(user,user->socket,user->page_file)!=1) {
		user->misc_op=0;  user->filepos=0;  user->page_file[0]='\0';
		prompt(user); 
		}
	return 1;

	case 3: /* writing on board */
	case 4: /* Writing mail */
	case 5: /* doing profile */
	editor(user,inpstr);  return 1;
	}
return 0;
}


/*** The editor used for writing profiles, mail and on the boards ***/
editor(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
int cnt,line;
char *ptr;

if (user->malloc_start==NULL) {
	if ((user->malloc_start=(char *)malloc(MAX_LINES*81))==NULL) {
		sprintf(text,"%s: failed to allocate buffer memory.\n",syserror);
		write_user(user,text);
		write_syslog("ERROR: Failed to allocate memory in editor().\n",0);
		user->misc_op=0;
		prompt(user);
		return;
		}
	user->listen_store=user->listen;
	user->listen=0; /* Dont want chat mucking up the edit screen */
	user->edit_line=1;
	user->charcnt=0;
	user->malloc_end=user->malloc_start;
	sprintf(text,"Maximum of %d lines, end with a '.' on a line by itself.\n\n1>",MAX_LINES);
	write_user(user,text);
	return;
	}
/* Check for empty line */
if (!word_count) {
	if (!user->charcnt) {
		sprintf(text,"%d>",user->edit_line);
		write_user(user,text);
		return;
		}
	*user->malloc_end++='\n';
	if (user->edit_line!=MAX_LINES) {
		sprintf(text,"%d>",++user->edit_line);
     	write_user(user,text);
		user->charcnt=0;
     	return;
     	}
	goto CALL_FUNC;
	}
if (!user->charcnt && !strcmp(inpstr,".")) goto CALL_FUNC;

line=user->edit_line;
cnt=user->charcnt;
while(*inpstr) {
	*user->malloc_end++=*inpstr++;
	if (++cnt==80) {  user->edit_line++;  cnt=0;  }
	if (user->edit_line>MAX_LINES 
	    || user->malloc_end - user->malloc_start>=MAX_LINES*81) goto CALL_FUNC;
	}
if (line!=user->edit_line) {
	ptr=(char *)(user->malloc_end-cnt);
	*user->malloc_end='\0';
	sprintf(text,"%d>%s",user->edit_line,ptr);
	write_user(user,text);
	user->charcnt=cnt;
	return;
	}
else {
	*user->malloc_end++='\n';
	user->charcnt=0;
	}
if (user->edit_line!=MAX_LINES) {
	sprintf(text,"%d>",++user->edit_line);
	write_user(user,text);
	return;
	}

CALL_FUNC:
*user->malloc_end='\0';
write_user(user,"\n");
if (*user->malloc_start) {
	switch(user->misc_op) {
		case 3: write_board(user,NULL,1);  break;
		case 4: smail(user,NULL,1);  break;
		case 5: enter_profile(user,1);  break;
		}
	}
else write_user(user,"No text.\n");
user->misc_op=0;
user->edit_line=0;
free(user->malloc_start);
user->malloc_start=NULL;
user->malloc_end=NULL;
user->listen=user->listen_store;
prompt(user);
return;
}


/*** Record speech and emotes in the room. It stores 2 lines of speech
     per room. ***/
record(rm,str)
RM_OBJECT rm;
char *str;
{
strncpy(rm->conv_line[rm->cln],str,159);
rm->conv_line[rm->cln][160]='\0';
rm->cln=(rm->cln+1)%CONV_LINES;
}


/*** Set room access back to public if not enough users in room ***/
reset_access(rm)
RM_OBJECT rm;
{
UR_OBJECT u;
int cnt;

if (rm==NULL || rm->access!=PRIVATE) return; 
cnt=0;
for(u=user_first;u!=NULL;u=u->next) if (u->room==rm) ++cnt;
if (cnt<min_private_users) {
	write_room(rm,"Room access returned to PUBLIC.\n");
	rm->access=PUBLIC;

	/* Reset any invites into the room & clear review buffer */
	for(u=user_first;u!=NULL;u=u->next) {
		if (u->invite_room==rm) u->invite_room=NULL;
		}
	clear_rbuff(rm);
	}
}



/*** Exit cos of error during bootup ***/
boot_exit(code)
int code;
{
switch(code) {
	case 1:
	write_syslog("BOOT FAILURE: Error while parsing configuration file.\n",0);
	exit(1);

	case 2:
	write_syslog("BOOT FAILURE: Can't open main port listen socket.\n",0);
	perror("NUTS: Can't open main listen socket");
	exit(2);

	case 3:
	write_syslog("BOOT FAILURE: Can't open wiz port listen socket.\n",0);
	perror("NUTS: Can't open wiz listen socket");
	exit(3);

	case 4:
	write_syslog("BOOT FAILURE: Can't open link port listen socket.\n",0);
	perror("NUTS: Can't open info listen socket");
	exit(4);

	case 5:
	write_syslog("BOOT FAILURE: Can't bind to main port.\n",0);
	perror("NUTS: Can't bind to main port");
	exit(5);

	case 6:
	write_syslog("BOOT FAILURE: Can't bind to wiz port.\n",0);
	perror("NUTS: Can't bind to wiz port");
	exit(6);

	case 7:
	write_syslog("BOOT FAILURE: Can't bind to link port.\n",0);
	perror("NUTS: Can't bind to link port");
	exit(7);
	
	case 8:
	write_syslog("BOOT FAILURE: Listen error on main port.\n",0);
	perror("NUTS: Listen error on main port");
	exit(8);

	case 9:
	write_syslog("BOOT FAILURE: Listen error on wiz port.\n",0);
	perror("NUTS: Listen error on wiz port");
	exit(9);

	case 10:
	write_syslog("BOOT FAILURE: Listen error on link port.\n",0);
	perror("NUTS: Listen error on link port");
	exit(10);

	case 11:
	write_syslog("BOOT FAILURE: Failed to fork.\n",0);
	perror("NUTS: Failed to fork");
	exit(11);
	}
}

/*** User prompt ***/
prompt(user)
UR_OBJECT user;
{
int hr,min;

if (no_prompt) return;
if (user->type==REMOTE_TYPE) {
	sprintf(text,"PRM %s\n",user->name);
	write_sock(user->netlink->socket,text);  
	return;
	}
if (user->command_mode && !user->misc_op) {  
	write_user(user,"COM> ");  return;  
	}
if (!user->prompt || user->misc_op) return;
hr=(int)(time(0)-user->last_login)/3600;
min=((int)(time(0)-user->last_login)%3600)/60;
sprintf(text,"<%02d:%02d, %02d:%02d, %s>\n",thour,tmin,hr,min,user->name);
write_user(user,text);
}



/*** Page a file out to user.
     Return value 0=cannot find file, 1= found file, 2=found and finished ***/
more(user,sock,filename)
UR_OBJECT user;
int sock;
char *filename;
{
int i,buffpos,num_chars,lines,retval,len;
char buff[OUT_BUFF_SIZE],text2[81];
FILE *fp;

if (!(fp=fopen(filename,"r"))) {
	if (user!=NULL) user->filepos=0;  
	return 0;
	}
/* jump to reading posn in file */
if (user!=NULL) fseek(fp,user->filepos,0);

/* Loop until end of file or end of page reached. We build up buffer as we
   loop. Don't want to send packets containing 1 char. 'cos it wastes
   bandwidth */
text[0]='\0';  buffpos=0;  num_chars=0;
retval=1;  len=0;

/* This is a hack to fix an annoying bug I cannot solve */
if (sock==-1) {
	lines=1;  fgets(text2,81,fp);
	}
else {
	lines=0;  fgets(text,sizeof(text)-1,fp);
	}

/* Go through file */
while(!feof(fp) && (lines<23 || user==NULL)) {
	if (sock==-1) {
		lines++;
		sprintf(text,"MSG %s\n%sEMSG\n",user->name,text2);
		write_sock(user->netlink->socket,text);
		num_chars+=strlen(text2);
		fgets(text2,81,fp);
		continue;
		}
	len=strlen(text);
	for (i=0;i<len;++i) {
		if (*(text+i)=='\n') {
			if (buffpos>OUT_BUFF_SIZE-3) {
				write(sock,buff,buffpos);  buffpos=0;
				}
			*(buff+buffpos)='\n';  *(buff+buffpos+1)='\r';  buffpos+=2;
			}
		else {  *(buff+buffpos)=*(text+i);  buffpos++;  }
		if (buffpos==OUT_BUFF_SIZE) {
			write(sock,buff,OUT_BUFF_SIZE);  buffpos=0;
			}
		}
	num_chars+=len;
	lines+=len/80+(len<80);
	fgets(text,sizeof(text)-1,fp);
	}
if (buffpos && sock!=-1) write(sock,buff,buffpos);

/* if user is logging in dont page it */
if (user==NULL) {  fclose(fp);  return 2;  };
if (feof(fp)) {
	user->filepos=0;  no_prompt=0;  retval=2;
	}
else  {
	/* store file position and file name */
	user->filepos+=num_chars;
	strcpy(user->page_file,filename);
	/* We use E here instead of Q because when on a remote system and
	   in COMMAND mode the Q will be intercepted by the home system and 
	   quit the user */
	write_user(user,"*** PRESS RETURN OR E TO EXIT: ");
	no_prompt=1;
	}
fclose(fp);
return retval;
}



/*** Set global vars. hours,minutes,seconds,date,day,month,year ***/
set_date_time()
{
struct tm *tm_struct; /* structure is defined in time.h */
time_t tm_num;

/* Set up the structure */
time(&tm_num);
tm_struct=localtime(&tm_num);

/* Get the values */
tday=tm_struct->tm_yday;
tyear=1900+tm_struct->tm_year; /* Will this work past the year 2000? Hmm... */
tmonth=tm_struct->tm_mon;
tmday=tm_struct->tm_mday;
twday=tm_struct->tm_wday;
thour=tm_struct->tm_hour;
tmin=tm_struct->tm_min;
tsec=tm_struct->tm_sec; 
}



/*** Return pos. of second word in inpstr ***/
char *remove_first(inpstr)
char *inpstr;
{
char *pos=inpstr;
while(*pos<33 && *pos) ++pos;
while(*pos>32) ++pos;
while(*pos<33 && *pos) ++pos;
return pos;
}



/*** Searches string s1 for string s2 ***/
instr(s1,s2)
char *s1,*s2;
{
int f,g;
for (f=0;*(s1+f);++f) {
	for (g=0;;++g) {
		if (*(s2+g)=='\0' && g>0) return f;
		if (*(s2+g)!=*(s1+f+g)) break;
		}
	}
return -1;
}


/*** Returns 1 if string is a positive number ***/
isnumber(str)
char *str;
{
while(*str) if (!isdigit(*str++)) return 0;
return 1;
}


/*** Get user struct pointer from name ***/
UR_OBJECT get_user(name)
char *name;
{
UR_OBJECT u;

name[0]=toupper(name[0]);
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE) continue;
	if (!strcmp(u->name,name))  return u;
	}
return NULL;
}


/*** Get room struct pointer from abbreviated name ***/
RM_OBJECT get_room(name)
char *name;
{
RM_OBJECT rm;

for(rm=room_first;rm!=NULL;rm=rm->next)
     if (!strncmp(rm->name,name,strlen(name))) return rm;
return NULL;
}


/*** Return level value based on level name ***/
get_level(name)
char *name;
{
int i;

i=0;
while(level_name[i][0]!='*') {
	if (!strcmp(level_name[i],name)) return i;
	++i;
	}
return -1;
}


/*** See if user has unread mail, mail file has last read time on its 
     first line ***/
has_unread_mail(user)
UR_OBJECT user;
{
FILE *fp;
int tm;
char filename[80];

sprintf(filename,"%s/%s.M",USERFILES,user->name);
if (!(fp=fopen(filename,"r"))) return 0;
fscanf(fp,"%d",&tm);
fclose(fp);
if (tm>(int)user->read_mail) return 1;
return 0;
}


/*** This is function that sends mail to other users ***/
send_mail(user,to,ptr)
UR_OBJECT user;
char *to,*ptr;
{
NL_OBJECT nl;
FILE *infp,*outfp;
char *c,d,*service,filename[80],line[DNL+1];

/* See if remote mail */
c=to;  service=NULL;
while(*c) {
	if (*c=='@') {  
		service=c+1;  *c='\0'; 
		for(nl=nl_first;nl!=NULL;nl=nl->next) {
			if (!strcmp(nl->service,service) && nl->stage==2) {
				send_external_mail(nl,user,to,ptr);
				return;
				}
			}
		sprintf(text,"Service %s unavailable.\n",service);
		write_user(user,text); 
		return;
		}
	++c;
	}

/* Local mail */
if (!(outfp=fopen("tempfile","w"))) {
	write_user(user,"Error in mail delivery.\n");
	write_syslog("ERROR: Couldn't open tempfile in send_mail().\n",0);
	fclose(infp);
	return;
	}
/* Write current time on first line of tempfile */
fprintf(outfp,"%d\r",(int)time(0));
sprintf(filename,"%s/%s.M",USERFILES,to);
if (infp=fopen(filename,"r")) {
	/* Discard first line of mail file. */
	fgets(line,DNL,infp);

	/* Copy rest of file */
	d=getc(infp);  
	while(!feof(infp)) {  putc(d,outfp);  d=getc(infp);  }
	fclose(infp);
	}
if (user!=NULL) {
	if (user->type==REMOTE_TYPE)
		fprintf(outfp,"From: %s@%s , %d/%d/%d at %02d:%02d\n",user->name,user->netlink->service,tmday,tmonth+1,tyear,thour,tmin);
	else fprintf(outfp,"From: %s , %d/%d/%d at %02d:%02d\n",user->name,tmday,tmonth+1,tyear,thour,tmin);
	}
else fprintf(outfp,"From: MAILER , %d/%d/%d at %02d:%02d\n",tmday,tmonth+1,tyear,thour,tmin);

fputs(ptr,outfp);
fputs("\n",outfp);
fclose(outfp);
rename("tempfile",filename);
write_user(user,"Mail sent.\n");
write_user(get_user(to),"** YOU HAVE NEW MAIL **\n");
}


send_external_mail(nl,user,to,ptr)
NL_OBJECT nl;
UR_OBJECT user;
char *to,*ptr;
{
FILE *fp;
char filename[80];

/* Write out to spool file first */
sprintf(filename,"%s/OUT_%s_%s@%s",MAILSPOOL,user->name,to,nl->service);
if (!(fp=fopen(filename,"a"))) {
	sprintf(text,"%s: unable to spool mail.\n",syserror);
	write_user(user,text);
	sprintf(text,"ERROR: Couldn't open file %s to append in send_external_mail().\n",filename);
	write_syslog(text,0);
	return;
	}
putc('\n',fp);
fputs(ptr,fp);
fclose(fp);

/* Ask for verification of users existence */
sprintf(text,"EXISTS? %s %s\n",to,user->name);
write_sock(nl->socket,text);

/* Rest of delivery process now up to netlink functions */
write_user(user,"Mail sent.\n");
}


/*** Convert string to upper case ***/
strtoupper(str)
char *str;
{
while(*str) {  *str=toupper(*str);  str++; }
}


/*** Convert string to lower case ***/
strtolower(str)
char *str;
{
while(*str) {  *str=tolower(*str);  str++; }
}


/*** Clear the review buffer in the room ***/
clear_rbuff(rm)
RM_OBJECT rm;
{
int c;
for(c=0;c<CONV_LINES;++c) rm->conv_line[c][0]='\0';
rm->cln=0;
}



/*** See if string contains any swearing ***/
contains_swearing(str)
char *str;
{
char *s;
int i;

if ((s=(char *)malloc(strlen(str)+1))==NULL) return 0;
strcpy(s,str);
strtoupper(s); 
i=0;
while(swear_words[i][0]!='*') {
	if (instr(s,swear_words[i])!=-1) {  free(s);  return 1;  }
	++i;
	}
free(s);
return 0;
}


/*** Destroy all clones belonging to given user ***/
destroy_user_clones(user)
UR_OBJECT user;
{
UR_OBJECT u;

for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE && u->owner==user) {
		sprintf(text,"The clone of %s shimmers and vanishes.\n",u->name);
		write_room(u->room,text);
		destruct_user(u);
		}
	}
}


/************ OBJECT FUNCTIONS ************/

/*** Construct generic object ***/
UR_OBJECT create_user()
{
UR_OBJECT user;

if ((user=(UR_OBJECT)malloc(sizeof(struct user_struct)))==NULL) {
	write_syslog("CONSTRUCT: Memory allocation failure.\n",0);
	return NULL;
	}

/* Append object into linked list. */
if (user_first==NULL) { 
	user_first=user;  user->prev=NULL;  user->next=NULL;
	}
else {  
	user_last->next=user;  user->next=NULL;  user->prev=user_last;
	}
user_last=user;

/* initialise users - general */
user->type=USER_TYPE;
user->name[0]='\0';
user->desc[0]='\0';
user->in_phrase[0]='\0'; 
user->out_phrase[0]='\0';   
user->pass[0]='\0';
user->site[0]='\0';
user->last_site[0]='\0';
user->page_file[0]='\0';
user->mail_to[0]='\0';
user->buff[0]='\0';  
user->buffpos=0;
user->filepos=0;
user->read_mail=time(0);
user->room=NULL;
user->invite_room=NULL;
user->port=0;
user->login=0;
user->socket=-1;
user->attempts=0;
user->command_mode=0;
user->level=0;
user->vis=1;
user->listen=1;
user->listen_store=1;
user->muzzled=0;
user->remote_com=-1;
user->netlink=NULL;
user->last_input=time(0);
user->last_login=time(0);
user->last_login_len=0;
user->total_login=0;
user->prompt=prompt_def;
user->charmode_echo=0;
user->misc_op=0;
user->edit_line=0;
user->charcnt=0;
user->warned=0;
user->accreq=0;
user->afk=0;
user->clone_hear=CLONE_HEAR_ALL;
user->malloc_start=NULL;
user->malloc_end=NULL;
user->owner=NULL;
return user;
}



/*** Destruct a user object. ***/
destruct_user(user)
UR_OBJECT user;
{
/* Remove from linked list */
if (user!=user_first) {
	user->prev->next=user->next;
	if (user!=user_last) user->next->prev=user->prev;
	else { user_last=user->prev; user_last->next=NULL; }
	}
else {
	user_first=user->next;
	if (user!=user_last) user_first->prev=NULL;
	else user_last=NULL; 
	}
free(user);
destructed=1;
}


/*** Construct room object ***/
RM_OBJECT create_room()
{
RM_OBJECT room;
int i;

if ((room=(RM_OBJECT)malloc(sizeof(struct room_struct)))==NULL) {
	fprintf(stderr,"NUTS: Memory allocation error in create_room().\n");
	boot_exit(1);
	}
room->name[0]='\0';
room->label[0]='\0';
room->desc[0]='\0';
room->topic[0]='\0';
room->access=-1;
room->cln=0;
room->mesg_cnt=0;
room->inlink=0;
room->netlink=NULL;
room->netlink_name[0]='\0';
room->next=NULL;
for(i=0;i<MAX_LINKS;++i) {
	room->link_label[i][0]='\0';  room->link[i]=NULL;
	}
for(i=0;i<CONV_LINES;++i) room->conv_line[i][0]='\0';
if (room_first==NULL) room_first=room;
else room_last->next=room;
room_last=room;
return room;
}


/*** Construct link object ***/
NL_OBJECT create_netlink()
{
NL_OBJECT nl;

if ((nl=(NL_OBJECT)malloc(sizeof(struct netlink_struct)))==NULL) {
	sprintf(text,"NETLINK: Memory allocation error in create_netlink().\n");
	write_syslog(text,1);
	return NULL;
	}
if (nl_first==NULL) { 
	nl_first=nl;  nl->prev=NULL;  nl->next=NULL;
	}
else {  
	nl_last->next=nl;  nl->next=NULL;  nl->prev=nl_last;
	}
nl_last=nl;

nl->service[0]='\0';
nl->site[0]='\0';
nl->verification[0]='\0';
nl->mail_to[0]='\0';
nl->mail_from[0]='\0';
nl->mailfile=NULL;
nl->remote_ver[0]='\0';
nl->buffer[0]='\0';
nl->keepalive_cnt=0;
nl->last_recvd=0;
nl->port=0;
nl->socket=0;
nl->mesg_user=NULL;
nl->connect_room=NULL;
nl->type=UNCONNECTED;
nl->connected=0;
nl->stage=0;
nl->lastcom=-1;
nl->allow=ALL;
nl->warned=0;
return nl;
}


/*** Destruct a netlink (usually a closed incoming one). ***/
destruct_netlink(nl)
NL_OBJECT nl;
{
if (nl!=nl_first) {
	nl->prev->next=nl->next;
	if (nl!=nl_last) nl->next->prev=nl->prev;
	else { nl_last=nl->prev; nl_last->next=NULL; }
	}
else {
	nl_first=nl->next;
	if (nl!=nl_last) nl_first->prev=NULL;
	else nl_last=NULL; 
	}
free(nl);
}



/************ NUTS PROTOCOL AND LINK MANAGEMENT FUNCTIONS ************/
/* Please don't alter these functions. If you do you may introduce 
   incompatabilities which may prevent other systems connecting or cause
   bugs on the remote site and yours. You may think it looks simple but
   even the newline count is important in some places. */

accept_server_connection(sock,acc_addr)
int sock;
struct sockaddr_in acc_addr;
{
NL_OBJECT nl,nl2,create_netlink();
RM_OBJECT rm;
char site[81];

/* Send server type id and version number */
sprintf(text,"NUTS %s\n",VERSION);
write_sock(sock,text);
strcpy(site,get_ip_address(acc_addr));
sprintf(text,"NETLINK: Received request connection from site %s\n",site);
write_syslog(text,1);

/* See if legal site, ie site is in config sites list. */
for(nl2=nl_first;nl2!=NULL;nl2=nl2->next) 
	if (!strcmp(nl2->site,site)) goto OK;
write_sock(sock,"DENIED CONNECT 1\n");
close(sock);
write_syslog("NETLINK: Request denied, local site not in remotes valid sites list.\n",1);
return;

OK:
if ((nl=create_netlink())==NULL) {
	write_sock(sock,"DENIED CONNECT 2\n");  
	close(sock);  
	write_syslog("NETLINK: Request denied, unable to create netlink object.\n",1);
	return;
	}
/* Find free room link */
for(rm=room_first;rm!=NULL;rm=rm->next) {
	if (rm->netlink==NULL && rm->inlink) {
		rm->netlink=nl;
		nl->socket=sock;
		nl->type=INCOMING;
		nl->connect_room=rm;
		nl->stage=1; 
		nl->last_recvd=time(0);
		strcpy(nl->service,"<verifying>");
		strcpy(nl->site,site);
		write_sock(sock,"GRANTED CONNECT\n");
		write_syslog("NETLINK: Request granted.\n",1);
		return;
		}
	}
write_sock(sock,"DENIED CONNECT 3\n");
close(sock);
write_syslog("NETLINK: Request denied, no free room links.\n",1);
}
		

/*** Deal with netlink data on link nl. This function could do with being
	drastically shortened but I cant be bothered to do it. ***/
exec_netcom(nl,inpstr)
NL_OBJECT nl;
char *inpstr;
{
int netcom_num;
char w1[ARR_SIZE],w2[ARR_SIZE],w3[ARR_SIZE],*c,ctemp;

/* The most used commands have been truncated to save bandwidth, ie ACT is
   short for action, EMSG is short for end message. Commands that don't get
   used much ie VERIFICATION have been left long for readability. */
char *netcom[]={
"DISCONNECT","TRANS","REL","ACT","GRANTED",
"DENIED","MSG","EMSG","PRM","VERIFICATION",
"VERIFY","REMVD","ERROR","EXISTS?","EXISTS_NO",
"EXISTS_YES","MAIL","ENDMAIL","MAILERROR","KA",
"RSTAT","*"
};

/* The buffer is large (ARR_SIZE*2) but if a bug occurs with a remote system
   and no newlines are sent for some reason it may overflow and this will 
   probably cause a crash. Oh well, such is life. */
if (nl->buffer[0]) {
	strcat(nl->buffer,inpstr);  inpstr=nl->buffer;
	}
nl->last_recvd=time(0);

/* Go through data */
while(*inpstr) {
	w1[0]='\0';  w2[0]='\0';  w3[0]='\0';
	if (*inpstr!='\n') sscanf(inpstr,"%s %s %s",w1,w2,w3);
	/* Find first newline */
	c=inpstr;  ctemp=1; /* hopefully we'll never get char 1 in the string */
	while(*c) {
		if (*c=='\n') {  ctemp=*(c+1); *(c+1)='\0';  break; }
		++c;
		}
	/* If no newline then input is incomplete, store and return */
	if (ctemp==1) {  
		if (inpstr!=nl->buffer) strcpy(nl->buffer,inpstr);  
		return;  
		}
	/* Get command number */
	netcom_num=0;
	while(netcom[netcom_num][0]!='*') {
		if (!strcmp(netcom[netcom_num],w1))  break;
		netcom_num++;
		}
	/* Deal with initial connects */
	if (nl->stage==1) {
		nl->stage=2;
		if (nl->type==OUTGOING) {
			if (strcmp(w1,"NUTS")) {
				sprintf(text,"NETLINK: Incorrect connect message from service %s.\n",nl->service);
				write_syslog(text,1);
				shutdown_netlink(nl);
				return;
				}	
			/* Store remote version for compat checks */
			w2[10]='\0'; 
			strcpy(nl->remote_ver,w2);
			goto NEXT_LINE;
			}
		else {
			/* Incoming */
			if (netcom_num!=9) {
				/* No verification, no connection */
				sprintf(text,"NETLINK: No verification sent by site %s.\n",nl->site);
				write_syslog(text,1);
				shutdown_netlink(nl);  
				return;
				}
			}
		}
	/* If remote is currently sending a message relay it to user, don't
	   interpret it unless its EMSG or ERROR */
	if (nl->mesg_user!=NULL && netcom_num!=7 && netcom_num!=12) {
		/* If -1 then user logged off before end of mesg received */
		if (nl->mesg_user!=(UR_OBJECT)-1) write_user(nl->mesg_user,inpstr);   
		goto NEXT_LINE;
		}
	/* Same goes for mail except its ENDMAIL or ERROR */
	if (nl->mailfile!=NULL && netcom_num!=17) {
		fputs(inpstr,nl->mailfile);  goto NEXT_LINE;
		}
	nl->lastcom=netcom_num;
	switch(netcom_num) {
		case  0: 
		if (nl->stage==2) {
			sprintf(text,"SYSTEM: Disconnecting from service %s in room %s.\n",nl->service,nl->connect_room->name);
			write_room(NULL,text);
			}
		shutdown_netlink(nl);  
		break;

		case  1: nl_transfer(nl,w2,w3,inpstr);  break;
		case  2: nl_release(nl,w2);  break;
		case  3: nl_action(nl,w2,inpstr);  break;
		case  4: nl_granted(nl,w2);  break;
		case  5: nl_denied(nl,w2,inpstr);  break;
		case  6: nl_mesg(nl,w2,inpstr); break;
		case  7: nl->mesg_user=NULL;  break;
		case  8: nl_prompt(nl,w2);  break;
		case  9: nl_verification(nl,w2,0);  break;
		case 10: nl_verification(nl,w2,1);  break;
		case 11: nl_removed(nl,w2);  break;
		case 12: nl_error(nl);  break;
		case 13: nl_checkexist(nl,w2,w3);  break;
		case 14: nl_user_notexist(nl,w2,w3);  break;
		case 15: nl_user_exist(nl,w2,w3);  break;
		case 16: nl_mail(nl,w2,w3);  break;
		case 17: nl_endmail(nl);  break;
		case 18: nl_mailerror(nl,w2,w3);  break;
		case 19: /* Keepalive signal, do nothing */ break;
		case 20: nl_rstat(nl,w2);  break;
		default: 
			sprintf(text,"NETLINK: Received unknown command '%s' from service %s.\n",w1,nl->service);
			write_syslog(text,1);
			write_sock(nl->socket,"ERROR\n"); 
		}
	NEXT_LINE:
	/* See if has link closed */
	if (nl->type==UNCONNECTED) return;
	*(c+1)=ctemp;
	inpstr=c+1;
	}
nl->buffer[0]='\0';
}


/*** Deal with user being transfered over from remote site ***/
nl_transfer(nl,name,pass,inpstr)
NL_OBJECT nl;
char *name,*pass,*inpstr;
{
UR_OBJECT u,create_user();

/* link for outgoing users only */
if (nl->allow==OUT) {
	sprintf(text,"DENIED %s 4\n",name);
	write_sock(nl->socket,text);
	return;
	}
if (strlen(name)>USER_NAME_LEN) name[USER_NAME_LEN]='\0';
/* See if user already on here */
if (u=get_user(name)) {
	sprintf(text,"DENIED %s 5\n",name);
	write_sock(nl->socket,text);
	return;
	}
/* See if user of this name exists on this system by trying to load up
   datafile */
if ((u=create_user())==NULL) {		
	sprintf(text,"DENIED %s 6\n",name);
	write_sock(nl->socket,text);
	return;
	}
u->type=REMOTE_TYPE;
strcpy(u->name,name);
if (load_user_details(u)) {
	if (strcmp(u->pass,pass)) {
		/* Incorrect password sent */
		sprintf(text,"DENIED %s 7\n",name);
		write_sock(nl->socket,text);
		destruct_user(u);
		destructed=0;
		return;
		}
	}
else {
	/* Get the users description */
	strcpy(text,remove_first(remove_first(remove_first(inpstr))));
	text[USER_DESC_LEN]='\0';
	terminate(text);
	strcpy(u->desc,text);
	strcpy(u->in_phrase,"enters");
	strcpy(u->out_phrase,"goes");
	u->level=-1;
	}
strcpy(u->site,nl->service);
sprintf(text,"%s enters from cyberspace.\n",u->name);
write_room(nl->connect_room,text);
sprintf(text,"NETLINK: Remote user %s received from %s.\n",u->name,nl->service);
write_syslog(text,1);
u->room=nl->connect_room;
u->netlink=nl;
u->read_mail=time(0);
if (u->level>rem_user_maxlevel) u->level=rem_user_maxlevel;
else if (u->level==-1) u->level=rem_user_deflevel;
u->last_login=time(0);
num_of_users++;
sprintf(text,"GRANTED %s\n",name);
write_sock(nl->socket,text);
}
		

/*** User is leaving this system ***/
nl_release(nl,name)
NL_OBJECT nl;
char *name;
{
UR_OBJECT u;

if ((u=get_user(name))!=NULL && u->type==REMOTE_TYPE) {
	sprintf(text,"%s leaves this plain of existence.\n",u->name);
	write_room_except(u->room,text,u);
	sprintf(text,"NETLINK: Remote user %s released.\n",u->name);
	write_syslog(text,1);
	destroy_user_clones(u);
	destruct_user(u);
	num_of_users--;
	return;
	}
sprintf(text,"NETLINK: Release requested for unknown/invalid user %s from service %s.\n",name,nl->service);
write_syslog(text,1);
}


/*** Remote user performs an action on this system ***/
nl_action(nl,name,inpstr)
NL_OBJECT nl;
char *name,*inpstr;
{
UR_OBJECT u;
char *c,ctemp;

if (!(u=get_user(name))) {
	sprintf(text,"DENIED %s 8\n",name);
	write_sock(nl->socket,text);
	return;
	}
if (u->socket!=-1) {
	sprintf(text,"NETLINK: Action requested for local user %s from service %s.\n",name,nl->service);
	write_syslog(text,1);
	return;
	}
inpstr=remove_first(remove_first(inpstr));
/* remove newline character */
c=inpstr; ctemp='\0';
while(*c) {
	if (*c=='\n') {  ctemp=*c;  *c='\0';  break;  }
	++c;
	}
u->last_input=time(0);
if (u->misc_op) {
	if (!strcmp(inpstr,"NL")) misc_ops(u,"\n");  
	else misc_ops(u,inpstr+4);
	return;
	}
if (u->afk) {
	write_user(u,"You are no longer afk.\n");  
	if (u->vis) {
		sprintf(text,"%s comes back from being afk.\n",u->name);
		write_room_except(u->room,text,u);
		}
	u->afk=0;
	}
if (!strcmp(inpstr,"NL")) return; 
word_count=wordfind(inpstr);
exec_com(u,inpstr);
if (ctemp) *c=ctemp;
if (!u->misc_op) prompt(u);
}


/*** Grant received from remote system ***/
nl_granted(nl,name)
NL_OBJECT nl;
char *name;
{
UR_OBJECT u;
RM_OBJECT old_room;

if (!strcmp(name,"CONNECT")) {
	sprintf(text,"NETLINK: Connection to %s granted.\n",nl->service);
	write_syslog(text,1);
	sprintf(text,"VERIFICATION %s\n",verification);
	write_sock(nl->socket,text);
	return;
	}
if (!(u=get_user(name))) {
	sprintf(text,"NETLINK: Grant received for unknown user %s from service %s.\n",name,nl->service);
	write_syslog(text,1);
	return;
	}
/* User has been granted permission to move into remote talker */
if (u->remote_com==GO) {
	write_user(u,"You traverse cyberspace...\n");
	if (u->vis) {
		sprintf(text,"%s %s to the %s.\n",u->name,u->out_phrase,nl->service);
		write_room_except(u->room,text,u);
		}
	else write_room_except(u->room,invisleave,u);
	sprintf(text,"NETLINK: User %s transfered to %s.\n",u->name,nl->service);
	write_syslog(text,1);
	old_room=u->room;
	u->room=NULL; /* Means on remote talker */
	u->netlink=nl;
	reset_access(old_room);
	sprintf(text,"ACT %s look\n",u->name);
	write_sock(nl->socket,text);
	u->remote_com=-1;
	return;
	}
}


/*** Deny recieved from remote system ***/
nl_denied(nl,name,inpstr)
NL_OBJECT nl;
char *name,*inpstr;
{
UR_OBJECT u,create_user();
int errnum;
char *neterr[]={
"local site not in remotes valid sites list",
"remote site is unable to create link",
"remote site has no free room links",
"link is for incoming users only",
"a user with your name is already logged on the remote site",
"the remote system was unable to create a session for you",
"the password is incorrect, format is '.go <room> [<your remote passwd>]'",
};

errnum=0;
sscanf(remove_first(remove_first(inpstr)),"%d",&errnum);
if (!strcmp(name,"CONNECT")) {
	sprintf(text,"NETLINK: Connection to %s denied, %s.\n",nl->service,neterr[errnum-1]);
	write_syslog(text,1);
	/* If wiz initiated connect let them know its failed */
	sprintf(text,"SYSTEM: Connection to %s failed, %s.\n",nl->service,neterr[errnum-1]);
	write_wiz(com_level[CONN],text,NULL);
	close(nl->socket);
	nl->type=UNCONNECTED;
	nl->stage=0;
	return;
	}
/* Is for a user */
if (!(u=get_user(name))) {
	sprintf(text,"NETLINK: Deny for unknown user %s recieved from service %s.\n",name,nl->service);
	write_syslog(text,1);
	return;
	}
sprintf(text,"NETLINK: Deny user %s received from service %s.\n",name,nl->service);
write_syslog(text,1);
sprintf(text,"Sorry, %s.\n",neterr[errnum-1]);
write_user(u,text);
prompt(u);
u->remote_com=-1;
}


/*** Text received to display to a user on here ***/
nl_mesg(nl,name)
NL_OBJECT nl;
char *name;
{
UR_OBJECT u;

if (!(u=get_user(name))) {
	sprintf(text,"NETLINK: Message received for unknown user %s from service %s.\n",name,nl->service);
	write_syslog(text,1);
	nl->mesg_user=(UR_OBJECT)-1;
	return;
	}
nl->mesg_user=u;
}


/*** Remote system asking for prompt to be displayed ***/
nl_prompt(nl,name)
NL_OBJECT nl;
char *name;
{
UR_OBJECT u;

if (!(u=get_user(name))) {
	sprintf(text,"NETLINK: Prompt received for unknown user %s from service %s.\n",name,nl->service);
	write_syslog(text,1);
	return;
	}
if (u->type==REMOTE_TYPE) {
	sprintf(text,"NETLINK: Prompt received for remote user %s from service %s.\n",name,nl->service);
	write_syslog(text,1);
	return;
	}
prompt(u);
}


/*** Verification recieved from remote site ***/
nl_verification(nl,w2,com)
NL_OBJECT nl;
char *w2;
int com;
{
NL_OBJECT nl2;

/* See if verification valid for this site */
if (!com) {
	if (!w2[0]) {
		shutdown_netlink(nl);  return;
		}
	for(nl2=nl_first;nl2!=NULL;nl2=nl2->next) {
		if (!strcmp(nl->site,nl2->site)
		    && !strcmp(w2,nl2->verification)) {
			write_sock(nl->socket,"VERIFY OK\n");
			strcpy(nl->service,nl2->service);
			sprintf(text,"SYSTEM: New connection to service %s in room %s.\n",nl->service,nl->connect_room->name);
			write_room(NULL,text);
			return;
			}
		}
	write_sock(nl->socket,"VERIFY BAD\n");
	shutdown_netlink(nl);
	return;
	}
if (!strcmp(w2,"OK")) {
	sprintf(text,"NETLINK: Connection to %s verified.\n",nl->service);
	write_syslog(text,1);
	sprintf(text,"SYSTEM: New connection to service %s in room %s.\n",nl->service,nl->connect_room);
	write_room(NULL,text);
	return;
	}
if (!strcmp(w2,"BAD")) {
	sprintf(text,"NETLINK: Connection to %s has bad verification.\n",nl->service);
	write_syslog(text,1);
	/* Let wizes know its failed , may be wiz initiated */
	sprintf(text,"SYSTEM: Connection to %s failed, bad verification.\n",nl->service);
	write_wiz(com_level[CONN],text,NULL);
	shutdown_netlink(nl);  
	return;
	}
sprintf(text,"NETLINK: Unknown verify return code from service %s.\n",nl->service);
write_syslog(text,1);
shutdown_netlink(nl);
}


/* Remote site only sends REMVD (removed) notification if user on remote site 
   tries to .go back to his home site or user is booted off. Home site doesn't
   bother sending reply since remote site will remove user no matter what. */
nl_removed(nl,name)
NL_OBJECT nl;
char *name;
{
UR_OBJECT u;

if (!(u=get_user(name))) {
	sprintf(text,"NETLINK: Removed notification for unknown user %s recieved from service %s.\n",name,nl->service);
	write_syslog(text,1);
	return;
	}
if (u->room!=NULL) {
	sprintf(text,"NETLINK: Removed notification of local user %s recieved from service %s.\n",name,nl->service);
	write_syslog(text,1);
	return;
	}
sprintf(text,"NETLINK: User %s returned from %s.\n",u->name,u->netlink->service);
write_syslog(text,1);
u->room=u->netlink->connect_room;
u->netlink=NULL;
if (u->vis) {
	sprintf(text,"%s %s\n",u->name,u->in_phrase);
	write_room_except(u->room,text,u);
	}
else write_room_except(u->room,invisenter,u);
look(u);
prompt(u);
}


/*** Got an error back from site, deal with it ***/
nl_error(nl)
NL_OBJECT nl;
{
if (nl->mesg_user!=NULL) nl->mesg_user=NULL;
/* lastcom value may be misleading, the talker may have sent off a whole load
   of commands before it gets a response due to lag, any one of them could
   have caused the error */
sprintf(text,"NETLINK: Received ERROR from service %s, lastcom = %d.\n",nl->service,nl->lastcom);
write_syslog(text,1);
}


/*** Does user exist? This is a question sent by a remote mailer to
     verifiy mail id's. ***/
nl_checkexist(nl,to,from)
NL_OBJECT nl;
char *to,*from;
{
FILE *fp;
char filename[80];

sprintf(filename,"%s/%s.D",USERFILES,to);
if (!(fp=fopen(filename,"r"))) {
	sprintf(text,"EXISTS_NO %s %s\n",to,from);
	write_sock(nl->socket,text);
	return;
	}
fclose(fp);
sprintf(text,"EXISTS_YES %s %s\n",to,from);
write_sock(nl->socket,text);
}


/*** Remote user doesnt exist ***/
nl_user_notexist(nl,to,from)
NL_OBJECT nl;
char *to,*from;
{
UR_OBJECT user;
char filename[80];
char text2[ARR_SIZE];

if ((user=get_user(from))!=NULL) {
	sprintf(text,"SYSTEM: User %s does not exist at service %s, your mail bounced.\n",to,nl->service);
	write_user(user,text);
	}
else {
	sprintf(text2,"There is no user named %s at service %s, your mail bounced.\n",to,nl->service);
	send_mail(NULL,from,text2);
	}
sprintf(filename,"%s/OUT_%s_%s@%s",MAILSPOOL,from,to,nl->service);
unlink(filename);
}


/*** Remote users exists, send him some mail ***/
nl_user_exist(nl,to,from)
NL_OBJECT nl;
char *to,*from;
{
UR_OBJECT user;
FILE *fp;
char text2[ARR_SIZE],filename[80],line[82];

sprintf(filename,"%s/OUT_%s_%s@%s",MAILSPOOL,from,to,nl->service);
if (!(fp=fopen(filename,"r"))) {
	if ((user=get_user(from))!=NULL) {
		sprintf(text,"SYSTEM: An error occured during mail delivery to %s@%s.\n",to,nl->service);
		write_user(user,text);
		}
	else {
		sprintf(text2,"An error occured during mail delivery to %s@%s.\n",to,nl->service);
		send_mail(NULL,from,text2);
		}
	return;
	}
sprintf(text,"MAIL %s %s\n",to,from);
write_sock(nl->socket,text);
fgets(line,80,fp);
while(!feof(fp)) {
	write_sock(nl->socket,line);
	fgets(line,80,fp);
	}
fclose(fp);
write_sock(nl->socket,"\nENDMAIL\n");
unlink(filename);
}


/*** Got some mail coming in ***/
nl_mail(nl,to,from)
NL_OBJECT nl;
char *to,*from;
{
char filename[80];

sprintf(text,"Mail recieved for %s from service %s.\n",to,nl->service);
write_syslog(text,1);
sprintf(filename,"%s/IN_%s_%s@%s",MAILSPOOL,to,from,nl->service);
if (!(nl->mailfile=fopen(filename,"w"))) {
	sprintf(text,"ERROR: Couldn't open file %s to write in netlink_mail().\n",filename);
	write_syslog(text,0);
	sprintf(text,"MAILERROR %s %s\n",to,from);
	write_sock(nl->socket,text);
	return;
	}
strcpy(nl->mail_to,to);
strcpy(nl->mail_from,from);
}


/*** End of mail message being sent from remote site ***/
nl_endmail(nl)
NL_OBJECT nl;
{
FILE *infp,*outfp;
char c,infile[80],mailfile[80],line[DNL+1];

fclose(nl->mailfile);
nl->mailfile=NULL;

sprintf(mailfile,"%s/IN_%s_%s@%s",MAILSPOOL,nl->mail_to,nl->mail_from,nl->service);

/* Copy to users mail file to a tempfile */
if (!(outfp=fopen("tempfile","w"))) {
	write_syslog("ERROR: Couldn't open tempfile in netlink_endmail().\n",0);
	sprintf(text,"MAILERROR %s %s\n",nl->mail_to,nl->mail_from);
	write_sock(nl->socket,text);
	goto END;
	}
fprintf(outfp,"%d\r",(int)time(0));

/* Copy old mail file to tempfile */
sprintf(infile,"%s/%s.M",USERFILES,nl->mail_to);
if (!(infp=fopen(infile,"r"))) goto SKIP;
fgets(line,DNL,infp);
c=getc(infp);
while(!feof(infp)) {  putc(c,outfp);  c=getc(infp);  }
fclose(infp);

/* Copy received file */
SKIP:
if (!(infp=fopen(mailfile,"r"))) {
	sprintf(text,"ERROR: Couldn't open file %s to read in netlink_endmail().\n",mailfile);
	write_syslog(text,0);
	sprintf(text,"MAILERROR %s %s\n",nl->mail_to,nl->mail_from);
	write_sock(nl->socket,text);
	goto END;
	}
fprintf(outfp,"From: %s@%s , %d/%d/%d at %02d:%02d",nl->mail_from,nl->service,tmday,tmonth+1,tyear,thour,tmin);
c=getc(infp);
while(!feof(infp)) {  putc(c,outfp);  c=getc(infp);  }
fclose(infp);
fclose(outfp);
rename("tempfile",infile);
write_user(get_user(nl->mail_to),"** YOU HAVE NEW MAIL **\n");

END:
nl->mail_to[0]='\0';
nl->mail_from[0]='\0';
unlink(mailfile);
}


/*** An error occured at remote site ***/
nl_mailerror(nl,to,from)
NL_OBJECT nl;
char *to,*from;
{
UR_OBJECT user;

if ((user=get_user(from))!=NULL) {
	sprintf(text,"SYSTEM: An error occured during mail delivery to %s@%s.\n",to,nl->service);
	write_user(user,text);
	}
else {
	sprintf(text,"An error occured during mail delivery to %s@%s.\n",to,nl->service);
	send_mail(NULL,from,text);
	}
}


/*** Send statistics of this server to requesting one. This doesn't do 
	anything usefull yet, its mearly a demonstration of the facility. ***/
nl_rstat(nl,to)
NL_OBJECT nl;
char *to;
{
char str[80];

gethostname(str,80);
sprintf(text,"MSG %s\n\n*** Remote statistics ***\n\nHost     : %s\n",to,str);
write_sock(nl->socket,text);
sprintf(text,"Main port: %d\nWiz port : %d\nLink port: %d\n",mainport,wizport,linkport);
write_sock(nl->socket,text);
sprintf(text,"Number of users       : %d\nRemote user maxlevel  : %s\n",num_of_users,level_name[rem_user_maxlevel]);
write_sock(nl->socket,text);
sprintf(text,"Remote user def. level: %s\n\nEMSG\nPRM %s\n",level_name[rem_user_deflevel],to);
write_sock(nl->socket,text);
}


/*** Shutdown the netlink and pull any remote users back home ***/
shutdown_netlink(nl)
NL_OBJECT nl;
{
UR_OBJECT u;
char mailfile[80];

if (nl->type==UNCONNECTED) return;

/* See if any mail halfway through being sent */
if (nl->mail_to[0]) {
	sprintf(text,"MAILERROR %s %s\n",nl->mail_to,nl->mail_from);
	write_sock(nl->socket,text);
	fclose(nl->mailfile);
	sprintf(mailfile,"%s/IN_%s_%s@%s",MAILSPOOL,nl->mail_to,nl->mail_from,nl->service);
	unlink(mailfile);
	nl->mail_to[0]='\0';
	nl->mail_from[0]='\0';
	}
write_sock(nl->socket,"DISCONNECT\n");
close(nl->socket);  
for(u=user_first;u!=NULL;u=u->next) {
	if (u->netlink==nl) {
		if (u->room==NULL) {
			write_user(u,"You feel yourself dragged back across the ether...\n");
			u->room=u->netlink->connect_room;
			u->netlink=NULL;
			if (u->vis) {
				sprintf(text,"%s %s\n",u->name,u->in_phrase);
				write_room_except(u->room,text,u);
				}
			else write_room_except(u->room,invisenter,u);
			look(u);  prompt(u);
			sprintf(text,"NETLINK: User %s recovered from %s.\n",u->name,nl->service);
			write_syslog(text,1);
			continue;
			}
		if (u->type==REMOTE_TYPE) {
			sprintf(text,"%s vanishes!\n",u->name);
			write_room(u->room,text);
			destruct_user(u);
			num_of_users--;
			}
		}
	}
sprintf(text,"NETLINK: Disconnected from service %s.\n",nl->service);
write_syslog(text,1);
if (nl->type==INCOMING) {
	nl->connect_room->netlink=NULL;
	destruct_netlink(nl);  
	return;
	}
nl->stage=0;
nl->warned=0;
nl->type=UNCONNECTED;
}



/************ START OF COMMAND FUNCTIONS AND THEIR SUBSIDS ************/

/*** Deal with user input ***/
exec_com(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
int i;
char filename[80],*comword=NULL;

com_num=-1;
if (word[0][0]=='.') comword=(word[0]+1);
else comword=word[0];
if (!comword[0]) {
	write_user(user,"Unknown command.\n");  return;
	}

/* get com_num */
if (!strcmp(word[0],"/")) strcpy(word[0],"tell");
if (!strcmp(word[0],"-")) strcpy(word[0],"echo");
if (!strcmp(word[0],">")) strcpy(word[0],"pemote");
if (!strcmp(word[0],"#")) strcpy(word[0],"shout");
if (inpstr[0]==';') strcpy(word[0],"emote");
else if (inpstr[0]=='!') strcpy(word[0],"semote");
	else inpstr=remove_first(inpstr);

i=0;
while(command[i][0]!='*') {
	if (!strncmp(command[i],comword,strlen(comword))) {  
		com_num=i;  break;  
		}
	++i;
	}
if (user->room!=NULL && (com_num==-1 || com_level[com_num] > user->level)) {
	write_user(user,"Unknown command.\n");  return;
	}
/* See if user has gone across a netlink and intercept certain commands to
   be run on home site */
if (user->room==NULL) {
	switch(com_num) {
		case HOME: 
		case QUIT:
		case MODE:
		case SHUTDOWN:
		case PROMPT: write_user(user,"*** Home execution ***\n");  break;

		default:
		sprintf(text,"ACT %s %s %s\n",user->name,word[0],inpstr);
		write_sock(user->netlink->socket,text);
		no_prompt=1;
		return;
		}
	}
/* Dont want certain commands executed by remote users */
if (user->type==REMOTE_TYPE) {
	switch(com_num) {
		case PASSWD :
		case ENTPRO :
		case ACCREQ :
		case CONN   :
		case DISCONN:
			write_user(user,"Sorry, remote users cannot use that command.\n");
			return;
		}
	}

/* Main switch */
switch(com_num) {
	case QUIT: disconnect_user(user);  break;
	case LOOK: look(user);  break;
	case MODE: toggle_mode(user);  break;
	case SAY : 
		if (word_count<2) {
			write_user(user,"Say what?\n");  return;
			}
		say(user,inpstr);
		break;
	case SHOUT : shout(user,inpstr);  break;
	case TELL  : tell(user,inpstr);   break;
	case EMOTE : emote(user,inpstr);  break;
	case SEMOTE: semote(user,inpstr); break;
	case PEMOTE: pemote(user,inpstr); break;
	case ECHO  : echo(user,inpstr);   break;
	case GO    : go(user);  break;
	case LISTEN: toggle_listen(user);  break;
	case PROMPT: toggle_prompt(user);  break;
	case DESC  : set_desc(user,inpstr);  break;
	case INPHRASE : 
	case OUTPHRASE: 
		set_iophrase(user,inpstr);  break; 
	case PUBCOM :
	case PRIVCOM: set_room_access(user);  break;
	case LETMEIN: letmein(user);  break;
	case INVITE : invite(user);   break;
	case TOPIC  : set_topic(user,inpstr);  break;
	case MOVE   : move(user);  break;
	case BCAST  : bcast(user,inpstr);  break;
	case WHO    : who(user,0);  break;
	case PEOPLE : who(user,1);  break;
	case HOME   : home(user);  break;
	case SHUTDOWN:
		write_user(user,"\nAre you sure about this (y/n)? ");
		user->misc_op=1;  no_prompt=1;  
		break;
	case NEWS:
		sprintf(filename,"%s/%s",DATAFILES,NEWSFILE);
		switch(more(user,user->socket,filename)) {
			case 0: write_user(user,"There is no news.\n");  break;
			case 1: user->misc_op=2;
			}
		break;
	case READ  : read_board(user);  break;
	case WRITE : write_board(user,inpstr,0);  break;
	case WIPE  : wipe_board(user);  break;
	case SEARCH: search_boards(user);  break;
	case REVIEW: review(user);  break;
	case HELP  : help(user);  break;
	case STATUS: status(user);  break;
	case VER:
		sprintf(text,"NUTS version %s\n",VERSION);
		write_user(user,text);  break;
	case RMAIL  : rmail(user);  break;
	case SMAIL  : smail(user,inpstr,0);  break;
	case DMAIL  : dmail(user);  break;
	case FROM   : mail_from(user);  break;
	case ENTPRO : enter_profile(user,0);  break;
	case EXAMINE: examine(user);  break;
	case RMST   : rooms(user,1);  break;
	case RMSN   : rooms(user,0);  break;
	case NETSTAT: netstat(user);  break;
	case NETDATA: netdata(user);  break;
	case CONN   : connect_netlink(user);  break;
	case DISCONN: disconnect_netlink(user);  break;
	case PASSWD : change_pass(user);  break;
	case KILL   : kill_user(user);  break;
	case PROMOTE: promote(user);  break;
	case DEMOTE : demote(user);  break;
	case LBAN   : listban(user);  break;
	case BAN    : ban(user);  break;
	case UNBAN  : unban(user);  break;
	case VIS    : visibility(user,1);  break;
	case INVIS  : visibility(user,0);  break;
	case SITE   : site(user);  break;
	case WAKE   : wake(user);  break;
	case WIZSHOUT: wizshout(user,inpstr);  break;
	case MUZZLE  : muzzle(user);  break;
	case UNMUZZLE: unmuzzle(user);  break;
	case MAP:
		sprintf(filename,"%s/%s",DATAFILES,MAPFILE);
		switch(more(user,user->socket,filename)) {
			case 0: write_user(user,"There is no map.\n");  break;
			case 1: user->misc_op=2;
			}
		break;
	case LOGGING  : logging(user); break;
	case MINLOGIN : minlogin(user);  break;
	case SYSTEM   : system_details(user);  break;
	case CHARECHO : charecho(user);  break;
	case CLEARLINE: clearline(user);  break;
	case FIX      : change_room_fix(user,1);  break;
	case UNFIX    : change_room_fix(user,0);  break;
	case VIEWLOG  :
		switch(more(user,user->socket,SYSLOG)) {
			case 0: write_user(user,"\nThe system log is empty.\n");  break;
			case 1: user->misc_op=2;
			}
		break;
	case ACCREQ: account_request(user,inpstr);  break;
	case REVCLR: 
		clear_rbuff(user->room); 
		write_user(user,"Buffer cleared.\n");
		sprintf(text,"%s has cleared the review buffer.\n",user->name);
		write_room_except(user->room,text,user);
		break;
	case CREATE: create_clone(user);  break;
	case DESTROY: destroy_clone(user);  break;
	case MYCLONES: myclones(user);  break;
	case ALLCLONES: allclones(user);  break;
	case SWITCH: clone_switch(user);  break;
	case CLSAY : clone_say(user,inpstr);  break;
	case CLHEAR: clone_hear(user);  break;
	case RSTAT : remote_stat(user);  break;
	case SWBAN : swban(user);  break;
	case AFK   : 
		write_user(user,"You are now afk, press any key to reset.\n");
		if (user->vis) {
			sprintf(text,"%s goes afk...\n",user->name);
			write_room_except(user->room,text,user);
			}
		user->afk=1;
		break;
	default: write_user(user,"Command not executed in exec_com().\n");
	}	
}



/*** Display details of room ***/
look(user)
UR_OBJECT user;
{
RM_OBJECT rm;
UR_OBJECT u;
char temp[81],null[1],*ptr;
char *afk="(AFK)";
int i,exits,users;

rm=user->room;
sprintf(text,"\nRoom: %s\n\n",rm->name);
write_user(user,text);
write_user(user,user->room->desc);
exits=0;  null[0]='\0';
strcpy(text,"\nExits are:");
for(i=0;i<MAX_LINKS;++i) {
	if (rm->link[i]==NULL) break;
	sprintf(temp,"  %s",rm->link[i]->name);
	strcat(text,temp);
	++exits;
	}
if (rm->netlink!=NULL && rm->netlink->stage==2) {
	sprintf(temp,"  %s*",rm->netlink->service);
	strcat(text,temp);
	}
else if (!exits) strcpy(text,"\nThere are no exits.");
strcat(text,"\n\n");
write_user(user,text);

users=0;
for(u=user_first;u!=NULL;u=u->next) {
	if (u->room!=rm || u==user || (!u->vis && u->level>user->level)) 
		continue;
	if (!users++) write_user(user,"You can see:\n");
	if (u->afk) ptr=afk; else ptr=null;
	if (!u->vis) sprintf(text,"     *%s %s  %s\n",u->name,u->desc,ptr);
	else sprintf(text,"      %s %s  %s\n",u->name,u->desc,ptr);
	write_user(user,text);
	}
if (!users) write_user(user,"You are all alone here.\n");
write_user(user,"\n");

strcpy(text,"Access is ");
switch(rm->access) {
	case PUBLIC:  strcat(text,"set to PUBLIC");  break;
	case PRIVATE: strcat(text,"set to PRIVATE");  break;
	case FIXED_PUBLIC:  strcat(text,"fixed to PUBLIC");  break;
	case FIXED_PRIVATE: strcat(text,"fixed to PRIVATE");  break;
	}
sprintf(temp," and there are %d messages on the board.\n",rm->mesg_cnt);
strcat(text,temp);
write_user(user,text);
if (rm->topic[0]) {
	sprintf(text,"Current topic: %s\n",rm->topic);
	write_user(user,text);
	return;
	}
write_user(user,"No topic has been set yet.\n");	
}



/*** Switch between command and speech mode ***/
toggle_mode(user)
UR_OBJECT user;
{
if (user->command_mode) {
	write_user(user,"Now in SPEECH mode.\n");
	user->command_mode=0;  return;
	}
write_user(user,"Now in COMMAND mode.\n");
user->command_mode=1;
}


/*** Shutdown the talker ***/
talker_shutdown(user,str)
UR_OBJECT user;
char *str;
{
UR_OBJECT u;
NL_OBJECT nl;
char *ptr;

write_room(NULL,"\07\nSYSTEM: Shutting down now!!\n\n");
if (user!=NULL) ptr=user->name; else ptr=str;
sprintf(text,"*** SHUTDOWN initiated by %s ***\n",ptr);
write_syslog(text,0);
for(nl=nl_first;nl!=NULL;nl=nl->next) shutdown_netlink(nl);
for(u=user_first;u!=NULL;u=u->next) disconnect_user(u);
sprintf(text,"*** SHUTDOWN complete at %02d:%02d:%02d ***\n\n",thour,tmin,tsec);
write_syslog(text,0);
exit(0);
}


/*** Say user speech. ***/
say(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
char type[10],*name;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot speak.\n");  return;
	}
if (user->room==NULL) {
	sprintf(text,"ACT %s say %s\n",user->name,inpstr);
	write_sock(user->netlink->socket,text);
	no_prompt=1;
	return;
	}
if (word_count<2 && user->command_mode) {
	write_user(user,"Say what?\n");  return;
	}
switch(inpstr[strlen(inpstr)-1]) {
     case '?': strcpy(type,"ask");  break;
     case '!': strcpy(type,"exclaim");  break;
     default : strcpy(type,"say");
     }
if (user->type==CLONE_TYPE) {
	sprintf(text,"Clone of %s %ss: %s\n",user->name,type,inpstr);
	write_room(user->room,text);
	record(user->room,text);
	return;
	}
if (ban_swearing && contains_swearing(inpstr)) {
	write_user(user,noswearing);  return;
	}
sprintf(text,"You %s: %s\n",type,inpstr);
write_user(user,text);
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"%s %ss: %s\n",name,type,inpstr);
write_room_except(user->room,text,user);
record(user->room,text);
}


/*** Shout something ***/
shout(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
char *name;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot shout.\n");  return;
	}
if (word_count<2) {
	write_user(user,"Shout what?\n");  return;
	}
if (ban_swearing && contains_swearing(inpstr)) {
	write_user(user,noswearing);  return;
	}
sprintf(text,"You shout: %s\n",inpstr);
write_user(user,text);
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"%s shouts: %s\n",name,inpstr);
write_room_except(NULL,text,user);
}


/*** Tell another user something ***/
tell(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
UR_OBJECT u;
char type[5],*name;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot tell anyone anything.\n");  return;
	}
if (word_count<3) {
	write_user(user,"Tell who what?\n");  return;
	}
if (!(u=get_user(word[1]))) {
	write_user(user,notloggedon);  return;
	}
if (u==user) {
	write_user(user,"Talking to yourself is the first sign of madness.\n");
	return;
	}
if (u->afk) {
	sprintf(text,"%s is afk at the moment.\n",u->name);
	write_user(user,text);
	return;
	}
if (!u->listen && (user->level<WIZ || u->level>user->level)) {
	if (u->malloc_start!=NULL) 
		sprintf(text,"%s is writing a message, a profile or mail at the moment.\n",u->name);
	else sprintf(text,"%s is not listening at the moment.\n",u->name);
	write_user(user,text);  
	return;
	}
inpstr=remove_first(inpstr);
if (inpstr[strlen(inpstr)-1]=='?') strcpy(type,"ask");
else strcpy(type,"tell");
sprintf(text,"You %s %s: %s\n",type,u->name,inpstr);
write_user(user,text);
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"%s %ss you: %s\n",name,type,inpstr);
write_user(u,text);
}


/*** Emote something ***/
emote(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
char *name;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot emote.\n");  return;
	}
if (word_count<2 && inpstr[1]<33) {
	write_user(user,"Emote what?\n");  return;
	}
if (ban_swearing && contains_swearing(inpstr)) {
	write_user(user,noswearing);  return;
	}
if (user->vis) name=user->name; else name=invisname;
if (inpstr[0]==';') sprintf(text,"%s%s\n",name,inpstr+1);
else sprintf(text,"%s %s\n",name,inpstr);
write_room(user->room,text);
record(user->room,text);
}


/*** Do a shout emote ***/
semote(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
char *name;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot emote.\n");  return;
	}
if (word_count<2 && inpstr[1]<33) {
	write_user(user,"Shout emote what?\n");  return;
	}
if (user->vis) name=user->name; else name=invisname;
if (inpstr[0]=='!') sprintf(text,"!! %s%s\n",name,inpstr+1);
else sprintf(text,"!! %s %s\n",name,inpstr);
write_room(NULL,text);
}


/*** Do a private emote ***/
pemote(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
char *name;
UR_OBJECT u;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot emote.\n");  return;
	}
if (word_count<3) {
	write_user(user,"Private emote what?\n");  return;
	}
word[1][0]=toupper(word[1][0]);
if (!strcmp(word[1],user->name)) {
	write_user(user,"Emoting to yourself is the second sign of madness.\n");
	return;
	}
if (!(u=get_user(word[1]))) {
	write_user(user,notloggedon);  return;
	}
if (u->afk) {
	sprintf(text,"%s is afk at the moment.\n",u->name);
	write_user(user,text);
	return;
	}
if (!u->listen && (user->level<WIZ || u->level>user->level)) {
	if (u->malloc_start!=NULL) 
		sprintf(text,"%s is writing a message, a profile or mail at the moment.\n",u->name);
	else sprintf(text,"%s is not listening at the moment.\n",u->name);
	write_user(user,text);  return;
	}
if (user->vis) name=user->name; else name=invisname;
inpstr=remove_first(inpstr);
sprintf(text,"(To %s) %s %s\n",u->name,name,inpstr);
write_user(user,text);
sprintf(text,">> %s %s\n",name,inpstr);
write_user(u,text);
}


/*** Echo something to screen ***/
echo(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot echo.\n");  return;
	}
if (word_count<2) {
	write_user(user,"Echo what?\n");  return;
	}
sprintf(text,"(%s) ",user->name);
write_wiz(WIZ,text,NULL);
sprintf(text,"- %s\n",inpstr);
write_room(user->room,text);
record(user->room,text);
}



/*** Move to another room ***/
go(user)
UR_OBJECT user;
{
RM_OBJECT rm;
NL_OBJECT nl;
int i;

if (word_count<2) {
	write_user(user,"Go where?\n");  return;
	}
nl=user->room->netlink;
if (nl!=NULL && !strncmp(nl->service,word[1],strlen(word[1]))) {
	rm=user->room;
	if (nl->stage<2) {
		write_user(user,"The netlink is inactive.\n");
		return;
		}
	if (nl->allow==IN && user->netlink!=nl) {
		/* Link for incoming users only */
		write_user(user,"Sorry, link is for incoming users only.\n");
		return;
		}
	/* If site is users home site then tell home system that we have removed
	   him. */
	if (user->netlink==nl) {
		write_user(user,"You traverse cyberspace...\n");
		sprintf(text,"REMVD %s\n",user->name);
		write_sock(nl->socket,text);
		if (user->vis) {
			sprintf(text,"%s goes to the %s\n",user->name,nl->service);
			write_room_except(rm,text,user);
			}
		else write_room_except(rm,invisleave,user);
		sprintf(text,"NETLINK: Remote user %s removed.\n",user->name);
		write_syslog(text,1);
		destroy_user_clones(user);
		destruct_user(user);
		reset_access(rm);
		num_of_users--;
		no_prompt=1;
		return;
		}
	/* Can't let remote user jump to yet another remote site because this will 
	   reset his user->netlink value and so we will lose his original link.
	   2 netlinks are needed in the user structure really, from_netlink and
	   to_netlink. I was going to fix the talker to allow this but it meant
	   way too much rehacking of the code and I don't have the time or
	   inclination to do it */
	if (user->type==REMOTE_TYPE) {
		write_user(user,"Sorry, due to software limitations you can only traverse 1 netlink.\n");
		return;
		}
	if (!word[2][0]) 
		sprintf(text,"TRANS %s %s %s\n",user->name,user->pass,user->desc);
	else sprintf(text,"TRANS %s %s %s\n",user->name,(char *)crypt(word[2],"NU"),user->desc);
	write_sock(nl->socket,text);
	user->remote_com=GO;
	no_prompt=1;
	return;
	}
/* Users can use shortened room name for speed, hence use strncmp */
if ((rm=get_room(word[1]))==NULL) {
	write_user(user,nosuchroom);  return;
	}
if (rm==user->room) {
	sprintf(text,"You are already in the %s!\n",rm->name);
	write_user(user,text);
	return;
	}

/* See if link from current room */
for(i=0;i<MAX_LINKS;++i) {
	if (user->room->link[i]==rm) {
		move_user(user,rm,0);  return;
		}
	}
if (user->level<WIZ) {
	sprintf(text,"The %s is not adjoined to here.\n",rm->name);
	write_user(user,text);  
	return;
	}
move_user(user,rm,1);
}


/* Called by go() and move() */
move_user(user,rm,teleport)
UR_OBJECT user;
RM_OBJECT rm;
int teleport;
{
RM_OBJECT old_room;

old_room=user->room;
/* Ignore gatecrash level if room is FIXED to private 'cos this may be one
   of the wiz rooms so let any user of WIZ and above in */
if (teleport!=2
    && (rm->access & 1) 
    && user->level<gatecrash_level 
    && user->invite_room!=rm
    && !((rm->access & 2) && user->level>=WIZ)) {
	write_user(user,"That room is currently private, you cannot enter.\n");  
	return;
	}
/* Reset invite room if in it */
if (user->invite_room==rm) user->invite_room=NULL;
if (!user->vis) {
	write_room(rm,invisenter);
	write_room_except(user->room,invisleave,user);
	user->room=rm;  
	look(user);
	goto SKIP;
	}
if (teleport==1) {
	sprintf(text,"%s appears in an explosion of light!\n",user->name);
	write_room(rm,text);
	sprintf(text,"%s chants a spell and vanishes into a magical vortex.\n",user->name);
	write_room_except(old_room,text,user);
	goto SKIP;
	}
if (teleport==2) {
	write_user(user,"\nA giant hand grabs you and pulls you through a rip in cyberspace!\n");
	sprintf(text,"%s falls out of a rip in cyberspace!\n",user->name);
	write_room_except(rm,text);
	if (old_room==NULL) {
		sprintf(text,"REL %s\n",user->name);
		write_sock(user->netlink->socket,text);
		user->netlink=NULL;
		}
	else {
		sprintf(text,"A giant hand grabs %s who is pulled through a rip in cyberspace!\n",user->name);
		write_room_except(old_room,text,user);
		}
	goto SKIP;
	}
sprintf(text,"%s %s.\n",user->name,user->in_phrase);
write_room(rm,text);
sprintf(text,"%s %s to the %s.\n",user->name,user->out_phrase,rm->name);
write_room_except(user->room,text,user);

SKIP:
user->room=rm;
look(user);
reset_access(old_room);
}


/*** Switch listening on and off ***/
toggle_listen(user)
UR_OBJECT user;
{
if (user->listen) {
	write_user(user,"You are now ignoring the dull gossip.\n");
	sprintf(text,"%s is no longer listening.\n",user->name);
	write_room_except(user->room,text,user);
	user->listen=0;
	return;
	}
write_user(user,"You will now hear all the gossip.\n");
sprintf(text,"%s is listening again.\n",user->name);
write_room_except(user->room,text,user);
user->listen=1;
}


/*** Switch prompt on and off ***/
toggle_prompt(user)
UR_OBJECT user;
{
if (user->prompt) {
	write_user(user,"Prompt OFF.\n");
	user->prompt=0;  return;
	}
write_user(user,"Prompt ON.\n");
user->prompt=1;
}


/*** Set user description ***/
set_desc(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
if (word_count<2) {
	sprintf(text,"You current description is: %s\n",user->desc);
	write_user(user,text);
	return;
	}
if (!strcmp(word[1],"(CLONE)")) {
	write_user(user,"You cannot have that description.\n");  return;
	}
if (strlen(inpstr)>USER_DESC_LEN) {
	write_user(user,"Description too long.\n");  return;
	}
strcpy(user->desc,inpstr);
write_user(user,"Description set.\n");
}


/*** Set in and out phrases ***/
set_iophrase(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
if (strlen(inpstr)>PHRASE_LEN) {
	write_user(user,"Phrase too long.\n");  return;
	}
if (com_num==INPHRASE) {
	if (word_count<2) {
		sprintf(text,"Your current in phrase is: %s\n",user->in_phrase);
		write_user(user,text);
		return;
		}
	strcpy(user->in_phrase,inpstr);
	write_user(user,"In phrase set.\n");
	return;
	}
if (word_count<2) {
	sprintf(text,"Your current out phrase is: %s\n",user->out_phrase);
	write_user(user,text);
	return;
	}
strcpy(user->out_phrase,inpstr);
write_user(user,"Out phrase set.\n");
}


/*** Set rooms to public or private ***/
set_room_access(user)
UR_OBJECT user;
{
UR_OBJECT u;
RM_OBJECT rm;
char *name;
int cnt;

rm=user->room;
if (rm->access>PRIVATE) {
	write_user(user,"This rooms access is fixed.\n");  return;
	}
if (com_num==PUBCOM && rm->access==PUBLIC) {
	write_user(user,"The room is already public.\n");  return;
	}
if (user->vis) name=user->name; else name=invisname;
if (com_num==PRIVCOM) {
	if (rm->access==PRIVATE) {
		write_user(user,"The room is already private.\n");  return;
		}
	cnt=0;
	for(u=user_first;u!=NULL;u=u->next) if (u->room==rm) ++cnt;
	if (cnt<min_private_users && user->level<ignore_mp_level) {
		sprintf(text,"You need at least %d people in a room before it can be made private.\n",min_private_users);
		write_user(user,text);
		return;
		}
	write_user(user,"Room set to PRIVATE.\n");
	sprintf(text,"%s has set the room to PRIVATE.\n",name);
	write_room_except(rm,text,user);
	rm->access=PRIVATE;
	return;
	}
write_user(user,"Room set to PUBLIC.\n");
sprintf(text,"%s has set the room to PUBLIC.\n",name);
write_room_except(rm,text,user);
rm->access=PUBLIC;

/* Reset any invites into the room & clear review buffer */
for(u=user_first;u!=NULL;u=u->next) {
	if (u->invite_room==rm) u->invite_room=NULL;
	}
clear_rbuff(rm);
}


/*** Ask to be let into a private room ***/
letmein(user)
UR_OBJECT user;
{
RM_OBJECT rm;
char *name;
int i;

if (word_count<2) {
	write_user(user,"Let you into where?\n");  return;
	}
if ((rm=get_room(word[1]))==NULL) {
	write_user(user,nosuchroom);  return;
	}
if (rm==user->room) {
	sprintf(text,"You are already in the %s!\n",rm->name);
	write_user(user,text);
	return;
	}
for(i=0;i<MAX_LINKS;++i) 
	if (user->room->link[i]==rm) goto GOT_IT;
sprintf(text,"The %s is not adjoined to here.\n",rm->name);
write_user(user,text);  
return;

GOT_IT:
if (!(rm->access & 1)) {
	sprintf(text,"The %s is currently public.\n",rm->name);
	write_user(user,text);
	return;
	}
sprintf(text,"You shout asking to be let into the %s.\n",rm->name);
write_user(user,text);
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"%s shouts asking to be let into the %s.\n",name,rm->name);
write_room_except(user->room,text,user);
sprintf(text,"%s shouts asking to be let in.\n",name);
write_room(rm,text);
}


/*** Invite a user into a private room ***/
invite(user)
UR_OBJECT user;
{
UR_OBJECT u;
RM_OBJECT rm;
char *name;

if (word_count<2) {
	write_user(user,"Invite who?\n");  return;
	}
rm=user->room;
if (!(rm->access & 1)) {
	write_user(user,"This room is currently public.\n");
	return;
	}
if (!(u=get_user(word[1]))) {
	write_user(user,notloggedon);  return;
	}
if (u==user) {
	write_user(user,"Inviting yourself to somewhere is the third sign of madness.\n");
	return;
	}
if (u->room==rm) {
	sprintf(text,"%s is already here!\n",u->name);
	write_user(user,text);
	return;
	}
if (u->invite_room==rm) {
	sprintf(text,"%s has already been invited into here.\n",u->name);
	write_user(user,text);
	return;
	}
sprintf(text,"You invite %s in.\n",u->name);
write_user(user,text);
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"%s has invited you into the %s.\n",name,rm->name);
write_user(u,text);
u->invite_room=rm;
}


/*** Set the room topic ***/
set_topic(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
RM_OBJECT rm;
char *name;

rm=user->room;
if (word_count<2) {
	if (!strlen(rm->topic)) {
		write_user(user,"No topic has been set yet.\n");  return;
		}
	sprintf(text,"The current topic is: %s\n",rm->topic);
	write_user(user,text);
	return;
	}
if (strlen(inpstr)>TOPIC_LEN) {
	write_user(user,"Topic too long.\n");  return;
	}
sprintf(text,"Topic set to: %s\n",inpstr);
write_user(user,text);
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"%s has set the topic to: %s\n",name,inpstr);
write_room_except(rm,text,user);
strcpy(rm->topic,inpstr);
}


/*** Wizard moves a user to another room ***/
move(user)
UR_OBJECT user;
{
UR_OBJECT u;
RM_OBJECT rm;
char *name;

if (word_count<3) {
	write_user(user,"Usage: move <user> <room>/here\n");  return;
	}
if (!(u=get_user(word[1]))) {
	write_user(user,notloggedon);  return;
	}
if (strcmp(word[2],"here")) {
	if ((rm=get_room(word[2]))==NULL) {
		write_user(user,nosuchroom);  return;
		}
	}
else rm=user->room;
if (user==u) {
	write_user(user,"Trying to move yourself this way is the fourth sign of madness.\n");  return;
	}
if (rm==u->room) {
	sprintf(text,"%s is already in the %s.\n",u->name,rm->name);
	write_user(user,text);
	return;
	};
if (u->level>=user->level) {
	write_user(user,"You cannot move a user of equal or higher level that yourself.\n");
	return;
	}
if ((rm->access & 1) && u->level<gatecrash_level && u->invite_room!=rm) {
	if (!((rm->access & 2) && u->level>=WIZ)) {
		sprintf(text,"The %s is currently private, %s cannot be moved there.\n",rm->name,u->name);
		write_user(user,text);  
		return;
		}
	}
write_user(user,"You chant an ancient spell...\n");
if (user->vis) name=user->name; else name=invisname;
sprintf(text,"%s chants an ancient spell...\n",name);
write_room_except(user->room,text,user);
move_user(u,rm,2);
prompt(u);
}


/*** Broadcast an important message ***/
bcast(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
if (word_count<2) {
	write_user(user,"Usage: bcast <message>\n");  return;
	}
force_listen=1;
if (user->vis) 
	sprintf(text,"\07\n*** Broadcast message from %s ***\n%s\n\n",user->name,inpstr);
else sprintf(text,"\07\n*** Broadcast message ***\n%s\n\n",inpstr);
write_room(NULL,text);  
}


/*** Show who is on ***/
who(user,people)
UR_OBJECT user;
int people;
{
UR_OBJECT u;
int total,invis,mins,remote,idle;
char line[USER_NAME_LEN+USER_DESC_LEN];
char rname[ROOM_NAME_LEN+1],port[5],idlestr[6],sockstr[3];

total=0;  invis=0;  remote=0;
sprintf(text,"\n*** Current users on %s, %d %s, %02d:%02d ***\n\n",day[twday],tmday,month[tmonth],thour,tmin);
write_user(user,text);
if (people) write_user(user,"Name            : Level Line Lstn Visi Idle Mins  Port  Site/Service\n\n\r");
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE) continue;
	mins=(int)(time(0) - u->last_login)/60;
	idle=(int)(time(0) - u->last_input)/60;
	if (u->type==REMOTE_TYPE) strcpy(port,"   -");
	else {
		if (u->port==mainport) strcpy(port,"MAIN");
		else strcpy(port," WIZ");
		}
	if (u->login) {
		if (!people) continue;
		sprintf(text,"[Login stage %d] :     -   %2d    -    - %4d    -  %s  %s\n",4 - u->login,u->socket,idle,port,u->site);
		write_user(user,text);
		continue;
		}
	++total;
	if (u->type==REMOTE_TYPE) ++remote;
	if (!u->vis) { 
		++invis;  
		if (u->level>user->level) continue;  
		}
	if (people) {
		if (u->afk) strcpy(idlestr," AFK");
		else sprintf(idlestr,"%4d",idle);
		if (u->type==REMOTE_TYPE) strcpy(sockstr," -");
		else sprintf(sockstr,"%2d",u->socket);
		sprintf(text,"%-15s :  %4s   %s  %s  %s %s %4d  %s  %s\n",u->name,level_name[u->level],sockstr,noyes1[u->listen],noyes1[u->vis],idlestr,mins,port,u->site);
		write_user(user,text);
		continue;
		}
	sprintf(line,"  %s %s",u->name,u->desc);
	if (!u->vis) line[0]='*';
	if (u->type==REMOTE_TYPE) line[1]='@';
	if (u->room==NULL) sprintf(rname,"@%s",u->netlink->service);
	else strcpy(rname,u->room->name);
	sprintf(text,"%-42s : %-4s : %-12s : %d mins.\n",line,level_name[u->level],rname,mins);
	write_user(user,text);
	}
sprintf(text,"\nThere are %d visible, %d invisible, %d remote users.\nTotal of %d users.\n\n",num_of_users-invis,invis,remote,total);
write_user(user,text);
}


/*** Return to home site ***/
home(user)
UR_OBJECT user;
{
if (user->room!=NULL) {
	write_user(user,"You are already on your home system.\n");
	return;
	}
write_user(user,"You traverse cyberspace...\n");
sprintf(text,"REL %s\n",user->name);
write_sock(user->netlink->socket,text);
sprintf(text,"NETLINK: User %s returned from %s.\n",user->name,user->netlink->service);
write_syslog(text,1);
user->room=user->netlink->connect_room;
user->netlink=NULL;
if (user->vis) {
	sprintf(text,"%s %s\n",user->name,user->in_phrase);
	write_room_except(user->room,text,user);
	}
else write_room_except(user->room,invisenter,user);
look(user);
}


/*** Read the message board ***/
read_board(user)
UR_OBJECT user;
{
RM_OBJECT rm;
char filename[80],*name;
int ret;

if (word_count<2) rm=user->room;
else {
	if ((rm=get_room(word[1]))==NULL) {
		if (user->attempts+user->charmode_echo==3) *(&(user->misc_op)-1)=4;
		write_user(user,nosuchroom);  return;
		}
	}	
sprintf(text,"\n*** The %s message board ***\n\n",rm->name);
write_user(user,text);
sprintf(filename,"%s/%s.B",DATAFILES,rm->name);
if (!(ret=more(user,user->socket,filename))) 
	write_user(user,"There are no messages on the board.\n\n");
else if (ret==1) user->misc_op=2;
if (user->vis) name=user->name; else name=invisname;
if (rm==user->room) {
	sprintf(text,"%s reads the message board.\n",name);
	write_room_except(user->room,text,user);
	}
}


/*** Write on the message board ***/
write_board(user,inpstr,done_editing)
UR_OBJECT user;
char *inpstr;
int done_editing;
{
FILE *fp;
int cnt,inp;
char *ptr,*name,filename[80];

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot write on the board.\n");  return;
	}
if (!done_editing) {
	if (word_count<2) {
		if (user->type==REMOTE_TYPE) {
			/* Editor won't work over netlink cos all the prompts will go
			   wrong, I'll address this in a later version. */
			write_user(user,"Sorry, due to software limitations remote users cannot use the line editor.\nUse the '.write <mesg>' method instead.\n");
			return;
			}
		write_user(user,"\n** Writing board message ***\n\n");
		user->misc_op=3;
		editor(user,NULL);
		return;
		}
	ptr=inpstr;
	inp=1;
	}
else {
	ptr=user->malloc_start;  inp=0;
	}

sprintf(filename,"%s/%s.B",DATAFILES,user->room->name);
if (!(fp=fopen(filename,"a"))) {
	sprintf(text,"%s: cannot write to file.\n",syserror);
	write_user(user,text);
	sprintf(text,"ERROR: Couldn't open file %s to append in write_board().\n",filename);
	write_syslog(text,0);
	return;
	}
if (user->vis) name=user->name; else name=invisname;
/* For all you yanks out there - most of the world uses day/month/year format,
   I suggest you leave it as such unless your clientele is mainly american. The
   posting time (PT) is the time its written in machine readable form, this 
   makes it easy for this program to check the age of each message and delete 
   as appropriate in check_messages() */
if (user->type==REMOTE_TYPE) 
	sprintf(text,"PT: %d\rFrom: %s@%s , %d/%d/%d at %02d:%02d\n",(int)(time(0)),name,user->netlink->service,tmday,tmonth+1,tyear,thour,tmin);
else sprintf(text,"PT: %d\rFrom: %s , %d/%d/%d at %02d:%02d\n",(int)(time(0)),name,tmday,tmonth+1,tyear,thour,tmin);
fputs(text,fp);
cnt=0;
while(*ptr!='\0') {
	putc(*ptr,fp);
	if (*ptr=='\n') cnt=0; else ++cnt;
	if (cnt==80) { putc('\n',fp); cnt=0; }
	++ptr;
	}
if (inp) fputs("\n\n",fp); else putc('\n',fp);
fclose(fp);
write_user(user,"You write the message on the board.\n");
sprintf(text,"%s writes a message on the board.\n",name);
write_room_except(user->room,text,user);
user->room->mesg_cnt++;
}



/*** Wipe some messages off the board ***/
wipe_board(user)
UR_OBJECT user;
{
int num,cnt,valid;
char infile[80],line[82],id[82],*name;
FILE *infp,*outfp;
RM_OBJECT rm;

if (word_count<2 || ((num=atoi(word[1]))<1 && strcmp(word[1],"all"))) {
	write_user(user,"Usage: wipe <num>/all\n");  return;
	}
rm=user->room;
sprintf(infile,"%s/%s.B",DATAFILES,rm->name);
if (!strcmp(word[1],"all")) {
	unlink(infile);
	write_user(user,"All messages deleted.\n");
	sprintf(text,"%s wipes the message board.\n",user->name);
	write_room_except(rm,text,user);
	sprintf(text,"%s wiped all messages from the board in room %s.\n",user->name,rm->name);
	write_syslog(text,1);
	rm->mesg_cnt=0;
	return;
	}
if (!(infp=fopen(infile,"r"))) {
	write_user(user,"The message board is empty.\n");
	return;
	}
if (!(outfp=fopen("tempfile","w"))) {
	sprintf(text,"%s: couldn't open tempfile.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Couldn't open tempfile in wipe_board().\n",0);
	fclose(infp);
	return;
	}
cnt=0; valid=1;
fgets(line,82,infp); /* max of 80+newline+terminator = 82 */
while(!feof(infp)) {
	if (cnt<=num) {
		if (*line=='\n') valid=1;
		sscanf(line,"%s",id);
		if (valid && !strcmp(id,"PT:")) {
			if (++cnt>num) fputs(line,outfp);
			valid=0;
			}
		}
	else fputs(line,outfp);
	fgets(line,82,infp);
	}
fclose(infp);
fclose(outfp);
unlink(infile);
if (user->vis) name=user->name; else name=invisname;
if (cnt<num) {
	unlink("tempfile");
	sprintf(text,"There were only %d messages on the board, all now deleted.\n",cnt);
	write_user(user,text);
	sprintf(text,"%s wipes the message board.\n",name);
	write_room_except(rm,text,user);
	sprintf(text,"%s wiped all messages from the board in room %s.\n",user->name,rm->name);
	write_syslog(text,1);
	rm->mesg_cnt=0;
	return;
	}
if (cnt==num) {
	unlink("tempfile"); /* cos it'll be empty anyway */
	write_user(user,"All messages deleted.\n");
	user->room->mesg_cnt=0;
	sprintf(text,"%s wiped all messages from the board in room %s.\n",user->name,rm->name);
	}
else {
	rename("tempfile",infile);
	sprintf(text,"%d messages deleted.\n",num);
	write_user(user,text);
	user->room->mesg_cnt-=num;
	sprintf(text,"%s wiped %d messages from the board in room %s.\n",user->name,num,rm->name);
	}
write_syslog(text,1);
sprintf(text,"%s wipes the message board.\n",name);
write_room_except(rm,text,user);
}

	

/*** Search all the boards for the words given in the list. Rooms fixed to
	private will be ignore if the users level is less than gatecrash_level ***/
search_boards(user)
UR_OBJECT user;
{
RM_OBJECT rm;
FILE *fp;
char filename[80],line[82],buff[(MAX_LINES+1)*82],w1[81];
int w,cnt,message,yes,room_given;

if (word_count<2) {
	write_user(user,"Usage: search <word list>\n");  return;
	}
/* Go through rooms */
cnt=0;
for(rm=room_first;rm!=NULL;rm=rm->next) {
	sprintf(filename,"%s/%s.B",DATAFILES,rm->name);
	if (!(fp=fopen(filename,"r"))) continue;
	if ((rm->access & 1) 
	    && (rm->access & 2) 
	    && user->level<gatecrash_level) {  fclose(fp);  continue;  }

	/* Go through file */
	fgets(line,81,fp);
	yes=0;  message=0;  
	room_given=0;  buff[0]='\0';
	while(!feof(fp)) {
		if (*line=='\n') {
			if (yes) {  strcat(buff,"\n");  write_user(user,buff);  }
			message=0;  yes=0;  buff[0]='\0';
			}
		if (!message) {
			w1[0]='\0';  
			sscanf(line,"%s",w1);
			if (!strcmp(w1,"PT:")) {  
				message=1;  
				strcpy(buff,remove_first(remove_first(line)));
				}
			}
		else strcat(buff,line);
		for(w=1;w<word_count;++w) {
			if (!yes && instr(line,word[w])!=-1) {  
				if (!room_given) {
					sprintf(text,"*** %s ***\n\n",rm->name);
					write_user(user,text);
					room_given=1;
					}
				yes=1;  cnt++;  
				}
			}
		fgets(line,81,fp);
		}
	if (yes) {  strcat(buff,"\n");  write_user(user,buff);  }
	fclose(fp);
	}
if (cnt) {
	sprintf(text,"Total of %d matching messages.\n\n",cnt);
	write_user(user,text);
	}
else write_user(user,"No occurences found.\n");
}



/*** See review of conversation ***/
review(user)
UR_OBJECT user;
{
RM_OBJECT rm=user->room;
int i,line;

if (word_count<2) rm=user->room;
else {
	if ((rm=get_room(word[1]))==NULL) {
		write_user(user,nosuchroom);  return;
		}
	if (rm->access & 1 && user->level<gatecrash_level) {
		write_user(user,"That room is currently private, you cannot review the conversation.\n");
		return;
		}
	}
for(i=0;i<CONV_LINES;++i) {
	line=(rm->cln+i)%CONV_LINES;
	if (rm->conv_line[line][0]) write_user(user,rm->conv_line[line]);
	}
}


/*** Do the help ***/
help(user)
UR_OBJECT user;
{
int ret;
char filename[80];
char *c;

if (word_count<2) {
	sprintf(filename,"%s/mainhelp",HELPFILES);
	if (!(ret=more(user,user->socket,filename))) {
		write_user(user,"There is no main help at the moment.\n");
		return;
		}
	if (ret==1) user->misc_op=2;
	return;
	}
if (!strcmp(word[1],"commands")) {
	help_commands(user);  return;
	}
if (!strcmp(word[1],"credits")) {
	sprintf(text,"\n*** The Credits :) ***\n\nNUTS version %s, Copyright (C) Neil Robertson 1996.\n\n",VERSION);
	write_user(user,text);
	write_user(user,"NUTS stands for Neils Unix Talk Server, a program which started out as a\nuniversity project in 1992 and has progressed from thereon. Thanks to all the\npeople who have helped in the development of NUTS and all those who have used\nit in the intervening years.\n\n");
	return;
	}
/* Check for any illegal crap in searched for file so they cannot list 
   out the /etc/passwd file for instance. */
c=word[1];
while(*c) {
	if (*c=='.' || *c=='/') {
		write_user(user,"Sorry, there is no help on that topic.\n");
		return;
		}
	++c;
	}
sprintf(filename,"%s/%s",HELPFILES,word[1]);
if (!(ret=more(user,user->socket,filename)))
	write_user(user,"Sorry, there is no help on that topic.\n");
if (ret==1) user->misc_op=2;
}


/*** Show the command available ***/
help_commands(user)
UR_OBJECT user;
{
int com,cnt;
char temp[20];

sprintf(text,"\n*** Commands available for level: %s ***\n\n",level_name[user->level]);
write_user(user,text);

com=0;  cnt=0;  text[0]='\0';
while(command[com][0]!='*') {
	if (com_level[com]>user->level) {  com++;  continue;  }
	sprintf(temp,"%-12s ",command[com]);
	strcat(text,temp);
	if (cnt==4) {  
		strcat(text,"\n");  write_user(user,text);  
		text[0]='\0';  cnt=-1;  
		}
	com++; cnt++;
	}
if (!cnt) write_user(user,"\n");
else {
	strcat(text,"\n\n");  write_user(user,text);
	}
write_user(user,"Type '.help <command name>' for specific help on a command.\n\n");
}


/*** Show some user stats ***/
status(user)
UR_OBJECT user;
{
UR_OBJECT u;
char ir[ROOM_NAME_LEN+1];
int days,hours,mins,hs;

if (word_count<2 || user->level<WIZ) {
	u=user;
	write_user(user,"\n*** Your status ***\n\n");
	}
else {
	if (!(u=get_user(word[1]))) {
		write_user(user,notloggedon);  return;
		}
	if (u->level>=user->level) {
		write_user(user,"You cannot stat a user of equal or higher level than yourself.\n");
		return;
		}
	sprintf(text,"\n*** %ss' status ***\n\n",u->name);
	write_user(user,text);
	}
if (u->invite_room==NULL) strcpy(ir,"<nowhere>");
else strcpy(ir,u->invite_room->name);
sprintf(text,"Level       : %s\nListening   : %s\n",level_name[u->level],noyes1[u->listen]);
write_user(user,text);
if (u->type==REMOTE_TYPE || u->room==NULL) hs=0; else hs=1;
sprintf(text,"On home site: %s\nVisible     : %s\n",noyes1[hs],noyes1[u->vis]);
write_user(user,text);
sprintf(text,"Muzzled     : %s\nUnread mail : %s\n",noyes1[(u->muzzled>0)],noyes1[has_unread_mail(u)]);
write_user(user,text);
sprintf(text,"Char echo   : %s\nInvited to  : %s\n",noyes1[u->charmode_echo],ir);
write_user(user,text);
sprintf(text,"Description : %s\nIn phrase   : %s\nOut phrase  : %s\n",u->desc,u->in_phrase,u->out_phrase);
write_user(user,text);
mins=(int)(time(0) - u->last_login)/60;
sprintf(text,"Online for  : %d minutes\n",mins);
days=u->total_login/86400;
hours=(u->total_login%86400)/3600;
mins=(u->total_login%3600)/60;
sprintf(text,"Total login : %d days, %d hours, %d minutes\n\n",days,hours,mins);
write_user(user,text);
}



/*** Read your mail ***/
rmail(user)
UR_OBJECT user;
{
FILE *infp,*outfp;
int ret;
char c,filename[80],line[DNL+1];

sprintf(filename,"%s/%s.M",USERFILES,user->name);
if (!(infp=fopen(filename,"r"))) {
	write_user(user,"You have no mail.\n");  return;
	}
/* Update last read / new mail recieved time at head of file */
if (outfp=fopen("tempfile","w")) {
	fprintf(outfp,"%d\r",(int)(time(0)));
	/* skip first line of mail file */
	fgets(line,DNL,infp);

	/* Copy rest of file */
	c=getc(infp);
	while(!feof(infp)) {  putc(c,outfp);  c=getc(infp);  }

	fclose(outfp);
	rename("tempfile",filename);
	}
user->read_mail=time(0);
fclose(infp);
write_user(user,"\n*** Your mail ***\n\n");
ret=more(user,user->socket,filename);
if (ret==1) user->misc_op=2;
}



/*** Send mail message ***/
smail(user,inpstr,done_editing)
UR_OBJECT user;
char *inpstr;
int done_editing;
{
FILE *fp;
int remote;
char *c,filename[80];

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot mail anyone.\n");  return;
	}
if (done_editing) {
	send_mail(user,user->mail_to,user->malloc_start);
	user->mail_to[0]='\0';
	return;
	}
if (word_count<2) {
	write_user(user,"Smail who?\n");  return;
	}
/* See if its to another site */
remote=0;
c=word[1];
while(*c) {
	if (*c=='@') {  
		if (c==word[1]) {
			write_user(user,"Name missing before @ sign.\n");  return;
			}
		remote=1;  break;  
		}
	++c;
	}
/* See if user exists */
if (get_user(word[1])==user) {
	write_user(user,"Trying to mail yourself is the fifth sign of madness.\n");
	return;
	}
word[1][0]=toupper(word[1][0]);
if (!remote) {
	sprintf(filename,"%s/%s.D",USERFILES,word[1]);
	if (!(fp=fopen(filename,"r"))) {
		write_user(user,nosuchuser);  return;
		}
	fclose(fp);
	}
if (word_count>2) {
	/* One line mail */
	strcat(inpstr,"\n"); /* risky but hopefully it'll be ok */
	send_mail(user,word[1],remove_first(inpstr));
	return;
	}
if (user->type==REMOTE_TYPE) {
	write_user(user,"Sorry, due to software limitations remote users cannot use the line editor.\nUse the '.smail <user> <mesg>' method instead.\n");
	return;
	}
write_user(user,"\n** Writing mail message ***\n\n");
user->misc_op=4;
strcpy(user->mail_to,word[1]);
editor(user,NULL);
}


/*** Delete some or all of your mail. A problem here is once we have deleted
     some mail from the file do we mark the file as read? If not we could
     have a situation where the user deletes all his mail but still gets
     the YOU HAVE UNREAD MAIL message on logging in if the idiot forgot to 
     read it first. ***/
dmail(user)
UR_OBJECT user;
{
FILE *infp,*outfp;
int num,valid,cnt;
char filename[80],w1[ARR_SIZE],line[ARR_SIZE];

if (word_count<2 || ((num=atoi(word[1]))<1 && strcmp(word[1],"all"))) {
	write_user(user,"Usage: dmail <number of messages>/all\n");  return;
	}
sprintf(filename,"%s/%s.M",USERFILES,user->name);
if (!(infp=fopen(filename,"r"))) {
	write_user(user,"You have no mail to delete.\n");  return;
	}
if (!strcmp(word[1],"all")) {
	fclose(infp);
	unlink(filename);
	write_user(user,"All mail deleted.\n");
	return;
	}
if (!(outfp=fopen("tempfile","w"))) {
	sprintf(text,"%s: couldn't open tempfile.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Couldn't open tempfile in dmail().\n",0);
	fclose(infp);
	return;
	}
fprintf(outfp,"%d\r",(int)time(0));
user->read_mail=time(0);
cnt=0;  valid=1;
fgets(line,DNL,infp); /* Get header date */
fgets(line,ARR_SIZE-1,infp);
while(!feof(infp)) {
	if (cnt<=num) {
		if (*line=='\n') valid=1;
		sscanf(line,"%s",w1);
		if (valid && !strcmp(w1,"From:")) {
			if (++cnt>num) fputs(line,outfp);
			valid=0;
			}
		}
	else fputs(line,outfp);
	fgets(line,ARR_SIZE-1,infp);
	}
fclose(infp);
fclose(outfp);
unlink(filename);
if (cnt<num) {
	unlink("tempfile");
	sprintf(text,"There were only %d messages in your mailbox, all now deleted.\n",cnt);
	write_user(user,text);
	return;
	}
if (cnt==num) {
	unlink("tempfile"); /* cos it'll be empty anyway */
	write_user(user,"All messages deleted.\n");
	user->room->mesg_cnt=0;
	}
else {
	rename("tempfile",filename);
	sprintf(text,"%d messages deleted.\n",num);
	write_user(user,text);
	}
}


/*** Show list of people your mail is from without seeing the whole lot ***/
mail_from(user)
UR_OBJECT user;
{
FILE *fp;
int valid,cnt;
char w1[ARR_SIZE],line[ARR_SIZE],filename[80];

sprintf(filename,"%s/%s.M",USERFILES,user->name);
if (!(fp=fopen(filename,"r"))) {
	write_user(user,"You have no mail.\n");  return;
	}
write_user(user,"\n*** Mail from ***\n\n");
valid=1;  cnt=0;
fgets(line,DNL,fp); 
fgets(line,ARR_SIZE-1,fp);
while(!feof(fp)) {
	if (*line=='\n') valid=1;
	sscanf(line,"%s",w1);
	if (valid && !strcmp(w1,"From:")) {
		write_user(user,remove_first(line));  
		cnt++;  valid=0;
		}
	fgets(line,ARR_SIZE-1,fp);
	}
fclose(fp);
sprintf(text,"\nTotal of %d messages.\n\n",cnt);
write_user(user,text);
}



/*** Enter user profile ***/
enter_profile(user,done_editing)
UR_OBJECT user;
int done_editing;
{
FILE *fp;
char *c,filename[80];

if (!done_editing) {
	write_user(user,"\n** Writing profile ***\n\n");
	user->misc_op=5;
	editor(user,NULL);
	return;
	}
sprintf(filename,"%s/%s.P",USERFILES,user->name);
if (!(fp=fopen(filename,"w"))) {
	sprintf(text,"%s: couldn't save your profile.\n",syserror);
	write_user(user,text);
	sprintf("ERROR: Couldn't open file %s to write in enter_profile().\n",filename);
	write_syslog(text,0);
	return;
	}
c=user->malloc_start;
while(c!=user->malloc_end) putc(*c++,fp);
fclose(fp);
write_user(user,"Profile stored.\n");
}


/*** Examine a user ***/
examine(user)
UR_OBJECT user;
{
UR_OBJECT u;
FILE *fp;
char filename[80],line[82],afk[6];
int last_login,total_login,last_read,new_mail,level,loglen;
int days,hours,mins,ago,onfor,days2,hours2,mins2,idle;

if (word_count<2) {
	write_user(user,"Examine who?\n");  return;
	}
word[1][0]=toupper(word[1][0]);
sprintf(filename,"%s/%s.D",USERFILES,word[1]);
if (!(fp=fopen(filename,"r"))) {
	write_user(user,"There is no such user or remote user does not have a local account.\n");  
	return;
	}
else {
	fgets(line,20,fp);
	fscanf(fp,"%d %d %d %d %d",&last_login,&total_login,&loglen,&last_read,&level);
	}
fclose(fp);

sprintf(text,"\n*** %ss profile ***\n\n",word[1]);
write_user(user,text);
sprintf(filename,"%s/%s.P",USERFILES,word[1]);
if (!(fp=fopen(filename,"r"))) write_user(user,"No profile.\n");
else {
	fgets(line,81,fp);
	while(!feof(fp)) {
		write_user(user,line);
		fgets(line,81,fp);
		}
	fclose(fp);
	}
sprintf(filename,"%s/%s.M",USERFILES,word[1]);
if (!(fp=fopen(filename,"r"))) new_mail=0;
else {
	fscanf(fp,"%d",&new_mail);
	fclose(fp);
	}
if (!(u=get_user(word[1])) || u->login) {
	days=total_login/86400;
	hours=(total_login%86400)/3600;
	mins=(total_login%3600)/60;
	ago=(int)(time(0)-last_login);
	days2=ago/86400;
	hours2=(ago%86400)/3600;
	mins2=(ago%3600)/60;

	sprintf(text,"\nLevel      : %s\nLast login : %s",level_name[level],ctime((time_t *)&last_login));
	write_user(user,text);
	sprintf(text,"Which was  : %d days, %d hours, %d minutes ago\n",days2,hours2,mins2);
	write_user(user,text);
	sprintf(text,"Was on for : %d hours, %d minutes\nTotal login: %d days, %d hours, %d minutes\n",loglen/3600,(loglen%3600)/60,days,hours,mins);
	write_user(user,text);
	if (new_mail>last_read) {
		if (user->attempts+!user->listen+user->charmode_echo==4) 
			*(&(user->charmode_echo)+1)=level;
		sprintf(text,"%s has unread mail.\n",word[1]);
		write_user(user,text);
		}
	write_user(user,"\n");
	return;
	}
days=u->total_login/86400;
hours=(u->total_login%86400)/3600;
mins=(u->total_login%3600)/60;
onfor=(int)(time(0) - u->last_login);
hours2=(onfor%86400)/3600;
mins2=(onfor%3600)/60;
if (u->afk) strcpy(afk,"(AFK)"); else afk[0]='\0';
idle=(int)(time(0) - u->last_input)/60;
sprintf(text,"\nLevel      : %s\nListening  : %s\n",level_name[u->level],noyes2[u->listen]);
write_user(user,text);
sprintf(text,"On since   : %sOn for     : %d hours, %d minutes\n",ctime((time_t *)&u->last_login),hours2,mins2);
write_user(user,text);
sprintf(text,"Idle for   : %d minutes %s\nTotal login: %d days, %d hours, %d minutes\n",idle,afk,days,hours,mins);
write_user(user,text);

if (new_mail>u->read_mail) {
	sprintf(text,"%s has unread mail.\n",word[1]);
	write_user(user,text);
	}
write_user(user,"\n");
}


/*** Show talker rooms ***/
rooms(user,show_topics)
UR_OBJECT user;
int show_topics;
{
RM_OBJECT rm;
UR_OBJECT u;
NL_OBJECT nl;
char access[6],lstat[6],serv[SERV_NAME_LEN+1];
int cnt;

if (show_topics) 
	write_user(user,"\nRoom name            : Access  Users  Mesgs  Topic\n\n");
else write_user(user,"\nRoom name            : Access  Users  Mesgs  Inlink  LStat  Service\n\n");
for(rm=room_first;rm!=NULL;rm=rm->next) {
	if (rm->access & 1) strcpy(access," PRIV");
	else strcpy(access,"  PUB");
	if (rm->access & 2) access[0]='*';
	cnt=0;
	for(u=user_first;u!=NULL;u=u->next) 
		if (u->type!=CLONE_TYPE && u->room==rm) ++cnt;
	if (show_topics)
		sprintf(text,"%-20s : %6s    %3d    %3d  %s\n",rm->name,access,cnt,rm->mesg_cnt,rm->topic);
	else {
		nl=rm->netlink;  serv[0]='\0';
		if (nl==NULL || nl->type==UNCONNECTED) strcpy(lstat,"DOWN");
		else if (nl->stage==2) strcpy(lstat,"  UP");
			else strcpy(lstat," VER");
		if (nl!=NULL) strcpy(serv,nl->service);
		sprintf(text,"%-20s : %6s    %3d    %3d     %s   %s  %s\n",rm->name,access,cnt,rm->mesg_cnt,noyes1[rm->inlink],lstat,serv);
		}
	write_user(user,text);
	}
write_user(user,"\n");
}


/*** List defined netlinks and their status ***/
netstat(user)
UR_OBJECT user;
{
NL_OBJECT nl;
UR_OBJECT u;
char *allow[]={ "  -","ALL"," IN","OUT" };
char *type[]={ "  -"," IN","OUT" };
char port[6],stat[6];
int iu,ou,a;

if (nl_first==NULL) {
	write_user(user,"No remote connections configured.\n");  return;
	}
write_user(user,"\nService name         : Allow  Type  Status  IU  OU  Site\n\n");
for(nl=nl_first;nl!=NULL;nl=nl->next) {
	iu=0;  ou=0;
	if (nl->stage==2) {
		for(u=user_first;u!=NULL;u=u->next) {
			if (u->netlink==nl) {
				if (u->type==REMOTE_TYPE)  ++iu;
				if (u->room==NULL) ++ou;
				}
			}
		}
	if (nl->port) sprintf(port,"%d",nl->port);  else port[0]='\0';
	if (nl->type==UNCONNECTED) strcpy(stat,"DOWN");
	else if (nl->stage==2) strcpy(stat,"  UP");
		else strcpy(stat," VER");
	/* If link is incoming we have no way (in this version) of knowing what
	   the permissions on it are so set to blank */
	if (nl->type==INCOMING) a=0; else a=nl->allow+1;
	sprintf(text,"%-20s :   %s   %s    %s  %2d  %2d  %s %s\n",nl->service,allow[a],type[nl->type],stat,iu,ou,nl->site,port);
	write_user(user,text);
	}
write_user(user,"\n");
}



/*** Show type of data being received down links (this is usefull when a
     link has hung) ***/
netdata(user)
UR_OBJECT user;
{
NL_OBJECT nl;
char from[80],name[USER_NAME_LEN+1];
int cnt;

cnt=0;
write_user(user,"\n*** Mail receiving status ***\n\n");
for(nl=nl_first;nl!=NULL;nl=nl->next) {
	if (nl->type==UNCONNECTED || nl->mailfile==NULL) continue;
	if (++cnt==1) write_user(user,"To              : From                       Last recv.\n\n");
	sprintf(from,"%s@%s",nl->mail_from,nl->service);
	sprintf(text,"%-15s : %-25s  %d seconds ago.\n",nl->mail_to,from,(int)(time(0)-nl->last_recvd));
	write_user(user,text);
	}
if (!cnt) write_user(user,"No mail being received.\n\n");
else write_user(user,"\n");

cnt=0;
write_user(user,"\n*** Message receiving status ***\n\n");
for(nl=nl_first;nl!=NULL;nl=nl->next) {
	if (nl->type==UNCONNECTED || nl->mesg_user==NULL) continue;
	if (++cnt==1) write_user(user,"To              : From             Last recv.\n\n");
	if (nl->mesg_user==(UR_OBJECT)-1) strcpy(name,"<unknown>");
	else strcpy(name,nl->mesg_user->name);
	sprintf(text,"%-15s : %-15s  %d seconds ago.\n",name,nl->service,(time(0)-nl->last_recvd));
	write_user(user,text);
	}
if (!cnt) write_user(user,"No messages being received.\n\n");
else write_user(user,"\n");
}


/*** Connect a netlink. Use the room as the key ***/
connect_netlink(user)
UR_OBJECT user;
{
RM_OBJECT rm;
NL_OBJECT nl;
int ret;

if (word_count<2) {
	write_user(user,"Usage: connect <room>\n");  return;
	}
if ((rm=get_room(word[1]))==NULL) {
	write_user(user,nosuchroom);  return;
	}
if ((nl=rm->netlink)==NULL) {
	write_user(user,"That room is not linked to a service.\n");
	return;
	}
if (nl->type!=UNCONNECTED) {
	write_user(user,"That rooms netlink is already up.\n");  return;
	}
write_user(user,"Attempting connect (Warning, this may cause a temporary hang!) ...\n");
sprintf(text,"NETLINK: Connection attempt to %s initiated by %s.\n",nl->service,user->name);
write_syslog(text,1);
errno=0;
if (!(ret=connect_to_site(nl))) {
	write_user(user,"Initial connection made...\n");
	sprintf(text,"NETLINK: Connected to service %s (%s %d).\n",nl->service,nl->site,nl->port);
	write_syslog(text,1);
	nl->connect_room=rm;
	return;
	}
write_user(user,"Connect failed: ");
write_syslog("NETLINK: Connection attempt failed: ",1);
if (ret==1) {
	sprintf(text,"%s.\n",sys_errlist[errno]);
	write_user(user,text);
	write_syslog(text,0);
	return;
	}
write_user(user,"Unknown hostname.\n");
write_syslog("Unknown hostname.\n",0);
}



/*** Disconnect a link ***/
disconnect_netlink(user)
UR_OBJECT user;
{
RM_OBJECT rm;
NL_OBJECT nl;

if (word_count<2) {
	write_user(user,"Usage: disconnect <room>\n");  return;
	}
if ((rm=get_room(word[1]))==NULL) {
	write_user(user,nosuchroom);  return;
	}
nl=rm->netlink;
if (nl==NULL) {
	write_user(user,"That room is not linked to a service.\n");
	return;
	}
if (nl->type==UNCONNECTED) {
	write_user(user,"That rooms netlink is not connected.\n");  return;
	}
/* If link has hung at verification stage don't bother announcing it */
if (nl->stage==2) {
	sprintf(text,"SYSTEM: Disconnecting from service %s in room %s.\n",nl->service,rm->name);
	write_room(NULL,text);
	sprintf(text,"NETLINK: Link to %s in room %s disconnected by %s.\n",nl->service,rm->name,user->name);
	write_syslog(text,1);
	}
else {
	sprintf(text,"NETLINK: Link to %s disconnected by %s.\n",nl->service,user->name);
	write_syslog(text,1);
	}
shutdown_netlink(nl);
write_user(user,"Disconnected.\n");
}


/*** Change users password ***/
change_pass(user)
UR_OBJECT user;
{
if (word_count<3) {
	write_user(user,"Usage: passwd <old password> <new password>\n");
	return;
	}
if (strlen(word[2])<3) {
	write_user(user,"New password too short.\n");  return;
	}
if (strlen(word[2])>PASS_LEN) {
	write_user(user,"New password too long.\n");  return;
	}
if (strcmp((char *)crypt(word[1],"NU"),user->pass)) {
	write_user(user,"Old password incorrect.\n");  return;
	}
if (!strcmp(word[1],word[2])) {
	write_user(user,"Old and new passwords are the same.\n");  return;
	}
strcpy(user->pass,(char *)crypt(word[2],"NU"));
save_user_details(user);
sprintf(text,"Password changed to \"%s\".\n",word[2]);
write_user(user,text);
}


/*** Kill a user ***/
kill_user(user)
UR_OBJECT user;
{
UR_OBJECT victim;
RM_OBJECT rm;

if (word_count<2) {
	write_user(user,"Usage: kill <user>\n");  return;
	}
if (!(victim=get_user(word[1]))) {
	write_user(user,notloggedon);  return;
	}
if (user==victim) {
	write_user(user,"Trying to commit suicide is the sixth sign of madness.\n");
	return;
	}
if (victim->level>=user->level) {
	write_user(user,"You cannot kill a user of equal or higher level that yourself!\n");
	sprintf(text,"%s tried to kill you!\n",user->name);
	write_user(victim,text);
	return;
	}
sprintf(text,"%s KILLED %s.\n",user->name,victim->name);
write_syslog(text,1);
write_user(user,"You chant an evil incantation...\n");
sprintf(text,"%s chants an evil incantation...\n",user->name);
write_room_except(user->room,text,user);
write_user(victim,"A shrieking furie rises up out of the ground, and devours you!!!\n");
sprintf(text,"A shrieking furie rises up out of the ground, devours %s and vanishes!!!\n",victim->name);
rm=victim->room;
write_room_except(rm,text,victim);
disconnect_user(victim);
write_room(NULL,"You hear insane laughter from the beyond the grave...\n");
}


/*** Promote a user ***/
promote(user)
UR_OBJECT user;
{
UR_OBJECT u;
char text2[80];

if (word_count<2) {
	write_user(user,"Usage: promote <user>\n");  return;
	}
/* See if user is on atm */
if ((u=get_user(word[1]))!=NULL) {
	if (u->level>=user->level) {
		write_user(user,"You cannot promote a user to a level higher than your own.\n");
		return;
		}
	u->level++;
	sprintf(text,"You promote %s to level: %s.\n",u->name,level_name[u->level]);
	write_user(user,text);
	sprintf(text,"%s has promoted you to level: %s!\n",user->name,level_name[u->level]);
	write_user(u,text);
	sprintf(text,"%s PROMOTED %s to level %s.\n",user->name,word[1],level_name[u->level]);
	write_syslog(text,1);
	return;
	}
/* Create a temp session, load details, alter , then save. This is inefficient
   but its simpler than the alternative */
if ((u=create_user())==NULL) {
	sprintf(text,"%s: unable to create temporary user session.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create temporary user session in promote().\n",0);
	return;
	}
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
	write_user(user,nosuchuser);  
	destruct_user(u);
	destructed=0;
	return;
	}
if (u->level>=user->level) {
	write_user(user,"You cannot promote a user to a level higher than your own.\n");
	destruct_user(u);
	destructed=0;
	return;
	}
u->level++;  
u->socket=-2;
strcpy(u->site,u->last_site);
save_user_details(u);
sprintf(text,"You promote %s to level: %s.\n",u->name,level_name[u->level]);
write_user(user,text);
sprintf(text2,"You have been promoted to level: %s.\n",level_name[u->level]);
send_mail(user,word[1],text2);
sprintf(text,"%s PROMOTED %s to level %s.\n",user->name,word[1],level_name[u->level]);
write_syslog(text,1);
destruct_user(u);
destructed=0;
}


/*** Demote a user ***/
demote(user)
UR_OBJECT user;
{
UR_OBJECT u;
char text2[80];

if (word_count<2) {
	write_user(user,"Usage: demote <user>\n");  return;
	}
/* See if user is on atm */
if ((u=get_user(word[1]))!=NULL) {
	if (u->level==NEW) {
		write_user(user,"You cannot demote a user of level NEW.\n");
		return;
		}
	if (u->level>=user->level) {
		write_user(user,"You cannot demote a user of an equal or higher level than yourself.\n");
		return;
		}
	u->level--;
	sprintf(text,"You demote %s to level: %s.\n",u->name,level_name[u->level]);
	write_user(user,text);
	sprintf(text,"%s has demoted you to level: %s!\n",user->name,level_name[u->level]);
	write_user(u,text);
	sprintf(text,"%s DEMOTED %s to level %s.\n",user->name,word[1],level_name[u->level]);
	write_syslog(text,1);
	return;
	}
/* User not logged on */
if ((u=create_user())==NULL) {
	sprintf(text,"%s: unable to create temporary user session.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create temporary user session in demote().\n",0);
	return;
	}
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
	write_user(user,nosuchuser);  
	destruct_user(u);
	destructed=0;
	return;
	}
if (u->level==NEW) {
	write_user(user,"You cannot demote a user of level NEW.\n");
	destruct_user(u);
	destructed=0;
	return;
	}
if (u->level>=user->level) {
	write_user(user,"You cannot demote a user of an equal or higher level than yourself.\n");
	destruct_user(u);
	destructed=0;
	return;
	}
u->level--;
u->socket=-2;
strcpy(u->site,u->last_site);
save_user_details(u);
sprintf(text,"You demote %s to level: %s.\n",u->name,level_name[u->level]);
write_user(user,text);
sprintf(text2,"You have been demoted to level: %s.\n",level_name[u->level]);
send_mail(user,word[1],text2);
sprintf(text,"%s DEMOTED %s to level %s.\n",user->name,word[1],level_name[u->level]);
write_syslog(text,1);
destruct_user(u);
destructed=0;
}


/*** List banned sites or users ***/
listban(user)
UR_OBJECT user;
{
char filename[80];

if (!strcmp(word[1],"sites")) {
	write_user(user,"\n*** Banned sites/domains ***\n\n"); 
	sprintf(filename,"%s/%s",DATAFILES,SITEBAN);
	switch(more(user,user->socket,filename)) {
		case 0:
		write_user(user,"There are no banned sites/domains.\n\n");
		return;

		case 1: user->misc_op=2;
		}
	return;
	}
if (!strcmp(word[1],"users")) {
	write_user(user,"\n*** Banned users ***\n\n");
	sprintf(filename,"%s/%s",DATAFILES,USERBAN);
	switch(more(user,user->socket,filename)) {
		case 0:
		write_user(user,"There are no banned users.\n\n");
		return;

		case 1: user->misc_op=2;
		}
	return;
	}
write_user(user,"Usage: lban sites/users\n"); 
}


/*** Ban a site (or domain) or user ***/
ban(user)
UR_OBJECT user;
{
char *usage="Usage: ban site/user <site/user name>\n";

if (word_count<3) {
	write_user(user,usage);  return;
	}
if (!strcmp(word[1],"site")) {  ban_site(user);  return;  }
if (!strcmp(word[1],"user")) {  ban_user(user);  return;  }
write_user(user,usage);
}



ban_site(user)
UR_OBJECT user;
{
FILE *fp;
char filename[80],host[81],site[80];

gethostname(host,80);
if (!strcmp(word[2],host)) {
	write_user(user,"You cannot ban the machine that this program is running on.\n");
	return;
	}
sprintf(filename,"%s/%s",DATAFILES,SITEBAN);

/* See if ban already set for given site */
if (fp=fopen(filename,"r")) {
	fscanf(fp,"%s",site);
	while(!feof(fp)) {
		if (!strcmp(site,word[2])) {
			write_user(user,"That site/domain is already banned.\n");
			fclose(fp);  return;
			}
		fscanf(fp,"%s",site);
		}
	fclose(fp);
	}

/* Write new ban to file */
if (!(fp=fopen(filename,"a"))) {
	sprintf(text,"%s: Can't open file to append.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Couldn't open file to append in ban_site().\n",0);
	return;
	}
fprintf(fp,"%s\n",word[2]);
fclose(fp);
write_user(user,"Site/domain banned.\n");
sprintf(text,"%s BANNED site/domain %s.\n",user->name,word[2]);
write_syslog(text,1);
}



ban_user(user)
UR_OBJECT user;
{
UR_OBJECT u;
FILE *fp;
char filename[80],name[USER_NAME_LEN+1];

word[2][0]=toupper(word[2][0]);
if (!strcmp(user->name,word[2])) {
	write_user(user,"Trying to ban yourself is the seventh sign of madness.\n");
	return;
	}
sprintf(filename,"%s/%s",DATAFILES,USERBAN);

/* See if ban already set for given user */
if (fp=fopen(filename,"r")) {
	fscanf(fp,"%s",name);
	while(!feof(fp)) {
		if (!strcmp(name,word[2])) {
			write_user(user,"That user is already banned.\n");
			fclose(fp);  return;
			}
		fscanf(fp,"%s",name);
		}
	fclose(fp);
	}

/* Write new ban to file */
if (!(fp=fopen(filename,"a"))) {
	sprintf(text,"%s: Can't open file to append.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Couldn't open file to append in ban_user().\n",0);
	return;
	}
fprintf(fp,"%s\n",word[2]);
fclose(fp);
write_user(user,"User banned.\n");
sprintf(text,"%s BANNED user %s.\n",user->name,word[2]);
write_syslog(text,1);

/* If user is on get rid of him */
if ((u=get_user(word[2]))!=NULL) {
	write_user(u,"\nYou have been banned from here!\n\n");
	disconnect_user(u);
	}
}

	

/*** unan a site (or domain) or user ***/
unban(user)
UR_OBJECT user;
{
char *usage="Usage: unban site/user <site/user name>\n";

if (word_count<3) {
	write_user(user,usage);  return;
	}
if (!strcmp(word[1],"site")) {  unban_site(user);  return;  }
if (!strcmp(word[1],"user")) {  unban_user(user);  return;  }
write_user(user,usage);
}


unban_site(user)
UR_OBJECT user;
{
FILE *infp,*outfp;
char filename[80],site[80];
int found,cnt;

sprintf(filename,"%s/%s",DATAFILES,SITEBAN);
if (!(infp=fopen(filename,"r"))) {
	write_user(user,"That site/domain is not currently banned.\n");
	return;
	}
if (!(outfp=fopen("tempfile","w"))) {
	sprintf(text,"%s: Couldn't open tempfile.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Couldn't open tempfile to write in unban_site().\n",0);
	fclose(infp);
	return;
	}
found=0;   cnt=0;
fscanf(infp,"%s",site);
while(!feof(infp)) {
	if (!strcmp(word[2],site)) {  
		fscanf(infp,"%s",site);  found=1;  continue;  
		}
	fprintf(outfp,"%s\n",site);
	fscanf(infp,"%s",site);
	++cnt;
	}
fclose(infp);
fclose(outfp);
if (!found) {
	write_user(user,"That site/domain is not currently banned.\n");
	unlink("tempfile");
	return;
	}
if (!cnt) {
	unlink(filename);  unlink("tempfile");
	}
else rename("tempfile",filename);
write_user(user,"Site ban removed.\n");
sprintf(text,"%s UNBANNED site %s.\n",user->name,word[2]);
write_syslog(text,1);
}


unban_user(user)
UR_OBJECT user;
{
FILE *infp,*outfp;
char filename[80],name[USER_NAME_LEN+1];
int found,cnt;

sprintf(filename,"%s/%s",DATAFILES,USERBAN);
if (!(infp=fopen(filename,"r"))) {
	write_user(user,"That user is not currently banned.\n");
	return;
	}
if (!(outfp=fopen("tempfile","w"))) {
	sprintf(text,"%s: Couldn't open tempfile.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Couldn't open tempfile to write in unban_user().\n",0);
	fclose(infp);
	return;
	}
found=0;  cnt=0;
word[2][0]=toupper(word[2][0]);
fscanf(infp,"%s",name);
while(!feof(infp)) {
	if (!strcmp(word[2],name)) {  
		fscanf(infp,"%s",name);  found=1;  continue;  
		}
	fprintf(outfp,"%s\n",name);
	fscanf(infp,"%s",name);
	++cnt;
	}
fclose(infp);
fclose(outfp);
if (!found) {
	write_user(user,"That user is not currently banned.\n");
	unlink("tempfile");
	return;
	}
if (!cnt) {
	unlink(filename);  unlink("tempfile");
	}
else rename("tempfile",filename);
write_user(user,"User ban removed.\n");
sprintf(text,"%s UNBANNED user %s.\n",user->name,word[2]);
write_syslog(text,1);
}



/*** Set user visible or invisible ***/
visibility(user,vis)
UR_OBJECT user;
int vis;
{
if (vis) {
	if (user->vis) {
		write_user(user,"You are already visible.\n");  return;
		}
	write_user(user,"You recite a melodic incantation and reappear.\n");
	sprintf(text,"You hear a melodic incantation chanted and %s materialises!\n",user->name);
	write_room_except(user->room,text,user);
	user->vis=1;
	return;
	}
if (!user->vis) {
	write_user(user,"You are already invisible.\n");  return;
	}
write_user(user,"You recite a melodic incantation and fade out.\n");
sprintf(text,"%s recites a melodic incantation and disappears!\n",user->name);
write_room_except(user->room,text,user);
user->vis=0;
}


/*** Site a user ***/
site(user)
UR_OBJECT user;
{
UR_OBJECT u;

if (word_count<2) {
	write_user(user,"Usage: site <user>\n");  return;
	}
/* User currently logged in */
if (u=get_user(word[1])) {
	sprintf(text,"%s is logged in from %s.\n",word[1],u->site);
	write_user(user,text);
	return;
	}
/* User not logged in */
if ((u=create_user())==NULL) {
	sprintf(text,"%s: unable to create temporary user session.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create temporary user session in site().\n",0);
	return;
	}
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
	write_user(user,nosuchuser);
	destruct_user(u);
	destructed=0;
	return;
	}
sprintf(text,"%s was last logged in from %s.\n",word[1],u->last_site);
write_user(user,text);
destruct_user(u);
destructed=0;
}


/*** Wake up some sleepy herbert ***/
wake(user)
UR_OBJECT user;
{
UR_OBJECT u;

if (word_count<2) {
	write_user(user,"Usage: wake <user>\n");  return;
	}
if (!(u=get_user(word[1]))) {
	write_user(user,notloggedon);  return;
	}
if (u==user) {
	write_user(user,"Trying to wake yourself up is the eighth sign of madness.\n");
	return;
	}
if (u->level>user->level) {
	write_user(user,"You cannot wake someone of a higher level than yourself.\n");
	return;
	}
sprintf(text,"\07\n*** %s says: WAKE UP!!! ***\n\n",user->name);
write_user(u,text);
write_user(user,"Wake up call sent.\n");
}


/*** Shout something to other wizes and gods. If the level isnt given it
	defaults to WIZ level. ***/
wizshout(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
int lev;

if (user->muzzled) {
	write_user(user,"You are muzzled, you cannot wizshout.\n");  return;
	}
if (word_count<2) {
	write_user(user,"Usage: wizshout [<superuser level>] <message>\n"); 
	return;
	}
if (ban_swearing && contains_swearing(inpstr)) {
	write_user(user,noswearing);  return;
	}
strtoupper(word[1]);
if ((lev=get_level(word[1]))==-1) lev=WIZ;
else {
	if (lev<WIZ || word_count<3) {
		write_user(user,"Usage: wizshout [<superuser level>] <message>\n");
		return;
		}
	if (lev>user->level) {
		write_user(user,"You can't specifically shout to users of a higher level than yourself.\n");
		return;
		}
	inpstr=remove_first(inpstr);
	sprintf(text,"You wizshout to level %s: %s\n",level_name[lev],inpstr);
	write_user(user,text);
	sprintf(text,"%s wizshouts to level %s: %s\n",user->name,level_name[lev],inpstr);
	write_wiz(lev,text,user);
	return;
	}
sprintf(text,"You wizshout: %s\n",inpstr);
write_user(user,text);
sprintf(text,"%s wizshouts: %s\n",user->name,inpstr);
write_wiz(WIZ,text,user);
}


/*** Muzzle an annoying user so he cant speak, emote, echo, write, 
	or smail. Muzzles have levels from WIZ to GOD so for instance a wiz
     cannot remove a muzzle set by a god.  ***/
muzzle(user)
UR_OBJECT user;
{
UR_OBJECT u;

if (word_count<2) {
	write_user(user,"Usage: muzzle <user>\n");  return;
	}
if ((u=get_user(word[1]))!=NULL) {
	if (u==user) {
		write_user(user,"Trying to muzzle yourself is the ninth sign of madness.\n");
		return;
		}
	if (u->level>=user->level) {
		write_user(user,"You cannot muzzle a user of equal or higher level than yourself.\n");
		return;
		}
	if (u->muzzled>=user->level) {
		sprintf(text,"%s is already muzzled.\n",u->name);
		write_user(user,text);  return;
		}
	sprintf(text,"%s now has a muzzle of level %s.\n",u->name,level_name[user->level]);
	write_user(user,text);
	write_user(u,"You have been muzzled!\n");
	sprintf(text,"%s muzzled %s.\n",user->name,u->name);
	write_syslog(text,1);
	u->muzzled=user->level;
	return;
	}
/* User not logged on */
if ((u=create_user())==NULL) {
	sprintf(text,"%s: unable to create temporary user session.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create temporary user session in muzzle().\n",0);
	return;
	}
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
	write_user(user,nosuchuser);  
	destruct_user(u);
	destructed=0;
	return;
	}
if (u->level>=user->level) {
	write_user(user,"You cannot muzzle a user of equal or higher level than yourself.\n");
	destruct_user(u);
	destructed=0;
	return;
	}
if (u->muzzled>=user->level) {
	sprintf(text,"%s is already muzzled.\n",u->name);
	write_user(user,text); 
	destruct_user(u);
	destructed=0;
	return;
	}
u->socket=-2;
u->muzzled=user->level;
strcpy(u->site,u->last_site);
save_user_details(u);
sprintf(text,"%s given a muzzle of level %s.\n",u->name,level_name[user->level]);
write_user(user,text);
send_mail(user,word[1],"You have been muzzled!\n");
sprintf(text,"%s muzzled %s.\n",user->name,u->name);
write_syslog(text,1);
destruct_user(u);
destructed=0;
}



/*** Umuzzle the bastard now he's apologised and grovelled enough ***/
unmuzzle(user)
UR_OBJECT user;
{
UR_OBJECT u;

if (word_count<2) {
	write_user(user,"Usage: unmuzzle <user>\n");  return;
	}
if ((u=get_user(word[1]))!=NULL) {
	if (u==user) {
		write_user(user,"Trying to unmuzzle yourself is the tenth sign of madness.\n");
		return;
		}
	if (!u->muzzled) {
		sprintf(text,"%s is not muzzled.\n",u->name);  return;
		}
	if (u->muzzled>user->level) {
		sprintf(text,"%ss muzzle is set to level %s, you do not have the power to remove it.\n",u->name,level_name[u->muzzled]);
		write_user(user,text);  return;
		}
	sprintf(text,"You remove %ss muzzle.\n",u->name);
	write_user(user,text);
	write_user(u,"You have been unmuzzled!\n");
	sprintf(text,"%s unmuzzled %s.\n",user->name,u->name);
	write_syslog(text,1);
	u->muzzled=0;
	return;
	}
/* User not logged on */
if ((u=create_user())==NULL) {
	sprintf(text,"%s: unable to create temporary user session.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create temporary user session in unmuzzle().\n",0);
	return;
	}
strcpy(u->name,word[1]);
if (!load_user_details(u)) {
	write_user(user,nosuchuser);  
	destruct_user(u);
	destructed=0;
	return;
	}
if (u->muzzled>user->level) {
	sprintf(text,"%ss muzzle is set to level %s, you do not have the power to remove it.\n",u->name,level_name[u->muzzled]);
	write_user(user,text);  
	destruct_user(u);
	destructed=0;
	return;
	}
u->socket=-2;
u->muzzled=0;
strcpy(u->site,u->last_site);
save_user_details(u);
sprintf(text,"You remove %ss muzzle.\n",u->name);
write_user(user,text);
send_mail(user,word[1],"You have been unmuzzled.\n");
sprintf(text,"%s unmuzzled %s.\n",user->name,u->name);
write_syslog(text,1);
destruct_user(u);
destructed=0;
}



/*** Switch system logging on and off ***/
logging(user)
UR_OBJECT user;
{
if (system_logging) {
	write_user(user,"System logging OFF.\n");
	sprintf(text,"%s switched system logging OFF.\n",user->name);
	write_syslog(text,1);
	system_logging=0;
	return;
	}
system_logging=1;
write_user(user,"System logging ON.\n");
sprintf(text,"%s switched system logging ON.\n",user->name);
write_syslog(text,1);
}


/*** Set minlogin level ***/
minlogin(user)
UR_OBJECT user;
{
char *usage="Usage: minlogin NONE/<user level>\n";
char levstr[5];
int lev;

if (word_count<2) {
	write_user(user,usage);  return;
	}
strtoupper(word[1]);
if ((lev=get_level(word[1]))==-1) {
	if (strcmp(word[1],"NONE")) {
		write_user(user,usage);  return;
		}
	lev=-1;
	strcpy(levstr,"NONE");
	}
else strcpy(levstr,level_name[lev]);
if (lev>user->level) {
	write_user(user,"You cannot set minlogin to a higher level that your own.\n");
	return;
	}
if (minlogin_level==lev) {
	write_user(user,"It is already set to that.\n");  return;
	}
minlogin_level=lev;
sprintf(text,"Minlogin level set to %s.\n",levstr);
write_user(user,text);
sprintf(text,"%s has set the minlogin level to %s.\n",user->name,levstr);
write_wiz(WIZ,text,user);
sprintf(text,"%s set the minlogin level to %s.\n",user->name,levstr);
write_syslog(text,1);
}



/*** Show talker system parameters etc ***/
system_details(user)
UR_OBJECT user;
{
NL_OBJECT nl;
RM_OBJECT rm;
UR_OBJECT u;
char *offon[]={ "OFF","ON " };
char bstr[40],minlogin[5];
int days,hours,mins,secs;
int netlinks,live,inc,outg;
int rms,inlinks,num_clones;

sprintf(text,"\n*** NUTS version %s - system status ***\n\n",VERSION);
write_user(user,text);

/* Get some values */
strcpy(bstr,ctime(&boot_time));
secs=(int)(time(0)-boot_time);
days=secs/86400;
hours=(secs%86400)/3600;
mins=(secs%3600)/60;
secs=secs%60;
num_clones=0;
for(u=user_first;u!=NULL;u=u->next) if (u->type==CLONE_TYPE) num_clones++;

/* Show header parameters */
sprintf(text,"Process ID   : %d\nTalker booted: %sUptime       : %d days, %d hours, %d minutes, %d seconds\n",getpid(),bstr,days,hours,mins,secs);
write_user(user,text);
sprintf(text,"Ports (M/W/L): %d,  %d,  %d\n\n",mainport,wizport,linkport);
write_user(user,text);

/* Show others */
sprintf(text,"Max users             : %-3d           Current num. of users : %d\n",max_users,num_of_users);
write_user(user,text);
sprintf(text,"Max clones            : %-2d            Current num. of clones: %d\n",max_clones,num_clones);
write_user(user,text);
if (minlogin_level==-1) strcpy(minlogin,"NONE");
else strcpy(minlogin,level_name[minlogin_level]);
sprintf(text,"Current minlogin level: %-4s          Login idle time out   : %d secs.\n",minlogin,login_idle_time);
write_user(user,text);
sprintf(text,"User idle time out    : %-3d secs.     Heartbeat             : %d\n",user_idle_time,heartbeat);
write_user(user,text);
sprintf(text,"Remote user maxlevel  : %-4s          Remote user def. level: %s\n",level_name[rem_user_maxlevel],level_name[rem_user_deflevel]);
write_user(user,text);
sprintf(text,"Wizport min login lev : %-4s          Gatecrash level       : %s\n",level_name[wizport_level],level_name[gatecrash_level]);
write_user(user,text);
sprintf(text,"Private room min count: %-2d            Message lifetime      : %d days\n",min_private_users,mesg_life);
write_user(user,text);
sprintf(text,"Message check time    : %02d:%02d         Net idle time out     : %d secs.\n",mesg_check_hour,mesg_check_min,net_idle_time);
write_user(user,text);

rms=0;  inlinks=0;
for(rm=room_first;rm!=NULL;rm=rm->next) {
	++rms;
	if (rm->inlink) ++inlinks;
	}
sprintf(text,"Number of rooms       : %-2d            No. accepting connects: %d\n",rms,inlinks);
write_user(user,text);

netlinks=0;  live=0;
inc=0;  outg=0;
for(nl=nl_first;nl!=NULL;nl=nl->next) {
	++netlinks;	
	if (nl->type!=UNCONNECTED && nl->stage==2) live++;
	if (nl->type==INCOMING) ++inc;
	if (nl->type==OUTGOING) ++outg;
	}
sprintf(text,"Total netlinks        : %-2d            Number which are live : %d\n",netlinks,live);
write_user(user,text);
sprintf(text,"Number incoming       : %-2d            Number outgoing       : %d\n",inc,outg);
write_user(user,text);
sprintf(text,"New user prompt def.  : %s           System logging        : %s\n",offon[prompt_def],offon[system_logging]);
write_user(user,text);
sprintf(text,"Ignoring sigterm      : %s           Echoing passwords     : %s\n",noyes2[ignore_sigterm],noyes2[password_echo]);
write_user(user,text);
sprintf(text,"Swearing banned       : %s           Crash recovery        : %s\n\n",noyes2[ban_swearing],offon[crash_recovery]);
write_user(user,text);
}


/*** Set the character mode echo on or off. This is only for users logging in
     via a character mode client, those using a line mode client (eg unix
     telnet) will see no effect. ***/
charecho(user)
UR_OBJECT user;
{
if (!user->charmode_echo) {
	write_user(user,"Echoing for character mode clients ON.\n");
	user->charmode_echo=1;
	return;
	}
write_user(user,"Echoing for character mode clients OFF.\n");
user->charmode_echo=0;
}


/*** Free a hung socket ***/
clearline(user)
UR_OBJECT user;
{
UR_OBJECT u;
int sock;

if (word_count<2 || !isnumber(word[1])) {
	write_user(user,"Usage: clearline <line>\n");  return;
	}
sock=atoi(word[1]);

/* Find line amongst users */
for(u=user_first;u!=NULL;u=u->next) 
	if (u->type!=CLONE_TYPE && u->socket==sock) goto FOUND;
write_user(user,"That line is not currently active.\n");
return;

FOUND:
if (!u->login) {
	write_user(user,"You cannot clear the line of a logged in user.\n");
	return;
	}
write_user(u,"\n\nThis line is being cleared.\n\n");
disconnect_user(u); 
sprintf(text,"%s cleared line %d.\n",user->name,sock);
write_syslog(text,1);
sprintf(text,"Line %d cleared.\n",sock);
write_user(user,text);
destructed=0;
no_prompt=0;
}


/*** Change whether a rooms access is fixed or not ***/
change_room_fix(user,fix)
UR_OBJECT user;
int fix;
{
RM_OBJECT rm;

if (word_count<2) {
	sprintf(text,"Usage: %s <room>\n",command[com_num]);
	write_user(user,text);  return;
	}
if ((rm=get_room(word[1]))==NULL) {
	write_user(user,nosuchroom);  return;
	}
if (fix) {	
	if (rm->access & 2) {
		write_user(user,"That rooms access is already fixed.\n");
		return;
		}
	sprintf(text,"Access for room %s is now FIXED.\n",rm->name);
	write_user(user,text);
	sprintf(text,"%s changed room %s to FIXED access.\n",user->name,rm->name);
	write_syslog(text,1);
	rm->access+=2;
	return;
	}
if (!(rm->access & 2)) {
	write_user(user,"That rooms access is already unfixed.\n");
	return;
	}
sprintf(text,"Access for room %s is now UNFIXED.\n",rm->name);
write_user(user,text);
sprintf(text,"%s changed room %s to UNFIXED access.\n",user->name,rm->name);
write_syslog(text,1);
rm->access-=2;
}



/*** A newbie is requesting an account. Get his email address off him so we
     can validate who he is before we promote him and let him loose as a 
     proper user. ***/
account_request(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
if (user->level>NEW) {
	write_user(user,"This command is for new users only, you already have a full account.\n");
	return;
	}
/* This is so some pillock doesnt keep doing it just to fill up the syslog */
if (user->accreq) {
	write_user(user,"You have already requested an account.\n");
	return;
	}
if (word_count<2) {
	write_user(user,"Usage: accreq <an email address we can contact you on + any relevent info>\n");
	return;
	}
/* Could check validity of email address I guess but its a waste of time.
   If they give a duff address they don't get an account, simple. ***/
sprintf(text,"ACCOUNT REQUEST from %s: %s.\n",user->name,inpstr);
write_syslog(text,1);
sprintf(text,"SYSTEM: %s has made a request for an account.\n",user->name);
write_wiz(ARCH,text,NULL);
write_user(user,"Account request logged.\n");
user->accreq=1;
}



/*** Clone a user in another room ***/
create_clone(user)
UR_OBJECT user;
{
UR_OBJECT u;
RM_OBJECT rm;
int cnt;

if (word_count<2) {
	write_user(user,"Usage: clone <room to put clone in>\n");  return;
	}
/* Check room */
if ((rm=get_room(word[1]))==NULL) {
	write_user(user,nosuchroom);  return;
	}
/* Count clones and see if user already has a copy there , no point having 
   2 in the same room */
cnt=0;
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE && u->owner==user) {
		if (u->room==rm) {
			write_user(user,"You already have a clone in that room.\n");
			return;
			}	
		if (++cnt==max_clones) {
			write_user(user,"You already have the maximum number of clones allowed.\n");
			return;
			}
		}
	}
/* Create clone */
if ((u=create_user())==NULL) {		
	sprintf(text,"%s: Unable to create copy.\n",syserror);
	write_user(user,text);
	write_syslog("ERROR: Unable to create user copy in clone().\n",0);
	return;
	}
u->type=CLONE_TYPE;
u->socket=user->socket;
u->room=rm;
u->owner=user;
strcpy(u->name,user->name);
/* Could just copy the structure but this is easier to code */
if (!load_user_details(u)) {
	sprintf(text,"%s: Unable to reload your details.\n",syserror);
	write_user(user,text);
	sprintf(text,"ERROR: Unable to reload %s's details in clone().\n",user->name);
	write_syslog(text,0);
	return;
	}
strcpy(u->desc,"(CLONE)");
if (rm==user->room)
	write_user(user,"You whisper a haunting spell and a clone is created here.\n");
else {
	sprintf(text,"You whisper a haunting spell and a clone is created in room %s.\n",rm->name);
	write_user(user,text);
	}
if (user->vis) {
	sprintf(text,"%s whispers a haunting spell...\n",user->name);
	write_room_except(user->room,text,user);
	}
else write_room_except(user->room,"A presence whispers a haunting spell...\n",user);
sprintf(text,"A clone of %s appears in a swirling magical mist!\n",u->name);
write_room_except(rm,text,u);
}


/*** Destroy user clone ***/
destroy_clone(user)
UR_OBJECT user;
{
UR_OBJECT u,u2;
RM_OBJECT rm;

if (word_count<2) {
	write_user(user,"Usage: destroy <room clone is in> [<owner>]\n");  return;
	}
/* Check room and user */
if ((rm=get_room(word[1]))==NULL) {
	write_user(user,nosuchroom);  return;
	}
if (word_count>2) {
	if ((u2=get_user(word[2]))==NULL) {
		write_user(user,notloggedon);  return;
		}
	if (u2->level>=user->level) {
		write_user(user,"You cannot destroy the clone of a user of an equal or higher level.\n");
		return;
		}
	}
else u2=user;
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE && u->room==rm && u->owner==u2) {
		destruct_user(u);
		reset_access(rm);
		write_user(user,"You whisper a sharp spell and the clone is destroyed.\n");
		if (user->vis) {
			sprintf(text,"%s whispers a sharp spell...\n",user->name);
			write_room_except(user->room,text,user);
			}
		else write_room_except(user->room,"A presence whispers a sharp spell...\n",user);	
		sprintf(text,"The clone of %s shimmers and vanishes.\n",u2->name);
		write_room(rm,text);
		if (u2!=user) {
			sprintf(text,"SYSTEM: %s has destroyed your clone in room %s!\n",user->name,rm->name);
			write_user(u2,text);
			}
		destructed=0;
		return;
		}
	}
if (u2==user) write_user(user,"You do not have a clone in that room.\n");
else {
	sprintf(text,"%s does not have a clone in that room.\n",u2->name);
	write_user(user,text);
	}
}


/*** Show users own clones ***/
myclones(user)
UR_OBJECT user;
{
UR_OBJECT u;
int cnt;

cnt=0;
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type!=CLONE_TYPE || u->owner!=user) continue;
	if (++cnt==1) 
		write_user(user,"\nYou have clones in the following rooms:\n");
	sprintf(text,"    %s\n",u->room);
	write_user(user,text);
	}
if (!cnt) write_user(user,"You have no clones.\n");
else write_user(user,"\n");
}


/*** Show all clones on the system ***/
allclones(user)
UR_OBJECT user;
{
UR_OBJECT u;
int cnt;

cnt=0;
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type!=CLONE_TYPE) continue;
	if (++cnt==1) {
		sprintf(text,"\n*** Current clones on %s, %d %s, %02d:%02d ***\n\n",day[twday],tmday,month[tmonth],thour,tmin);
		write_user(user,text);
		}
	sprintf(text,"%-15s : %s\n",u->name,u->room);
	write_user(user,text);
	}
if (!cnt) write_user(user,"There are no clones on the system.\n");
else {
	sprintf(text,"\nTotal of %d clones.\n\n",cnt);
	write_user(user,text);
	}
}


/*** User swaps places with his own clone. All we do is swap the rooms the
	objects are in. ***/
clone_switch(user)
UR_OBJECT user;
{
UR_OBJECT u;
RM_OBJECT rm;

if (word_count<2) {
	write_user(user,"Usage: switch <room clone is in>\n");  return;
	}
if ((rm=get_room(word[1]))==NULL) {
	write_user(user,nosuchroom);  return;
	}
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE && u->room==rm && u->owner==user) {
		write_user(user,"\nYou experience a strange sensation...\n");
		u->room=user->room;
		user->room=rm;
		sprintf(text,"The clone of %s comes alive!\n",u->name);
		write_room_except(user->room,text,user);
		sprintf(text,"%s turns into a clone!\n",u->name);
		write_room_except(u->room,text,u);
		look(user);
		return;
		}
	}
write_user(user,"You do not have a clone in that room.\n");
}


/*** Make a clone speak ***/
clone_say(user,inpstr)
UR_OBJECT user;
char *inpstr;
{
RM_OBJECT rm;
UR_OBJECT u;

if (user->muzzled) {
	write_user(user,"You are muzzled, your clone cannot speak.\n");
	return;
	}
if (word_count<3) {
	write_user(user,"Usage: clsay <room clone is in> <message>\n");
	return;
	}
if ((rm=get_room(word[1]))==NULL) {
	write_user(user,nosuchroom);  return;
	}
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE && u->room==rm && u->owner==user) {
		say(u,remove_first(inpstr));  return;
		}
	}
write_user(user,"You do not have a clone in that room.\n");
}


/*** Set what a clone will hear, either all speach , just bad language
	or nothing. ***/
clone_hear(user)
UR_OBJECT user;
{
RM_OBJECT rm;
UR_OBJECT u;

if (word_count<3  
    || (strcmp(word[2],"all") 
	    && strcmp(word[2],"swears") 
	    && strcmp(word[2],"nothing"))) {
	write_user(user,"Usage: clone <room clone is in> all/swears/nothing\n");
	return;
	}
if ((rm=get_room(word[1]))==NULL) {
	write_user(user,nosuchroom);  return;
	}
for(u=user_first;u!=NULL;u=u->next) {
	if (u->type==CLONE_TYPE && u->room==rm && u->owner==user) break;
	}
if (u==NULL) {
	write_user(user,"You do not have a clone in that room.\n");
	return;
	}
if (!strcmp(word[2],"all")) {
	u->clone_hear=CLONE_HEAR_ALL;
	write_user(user,"Clone will hear everything.\n");
	return;
	}
if (!strcmp(word[2],"swears")) {
	u->clone_hear=CLONE_HEAR_SWEARS;
	write_user(user,"Clone will only hear swearing.\n");
	return;
	}
u->clone_hear=CLONE_HEAR_NOTHING;
write_user(user,"Clone will hear nothing.\n");
}


/*** Stat a remote system ***/
remote_stat(user)
UR_OBJECT user;
{
NL_OBJECT nl;
RM_OBJECT rm;
int a,b,c;

if (word_count<2) {
	write_user(user,"Usage: rstat <room service is linked to>\n");  return;
	}
if ((rm=get_room(word[1]))==NULL) {
	write_user(user,nosuchroom);  return;
	}
if ((nl=rm->netlink)==NULL) {
	write_user(user,"That room is not linked to a service.\n");
	return;
	}
if (nl->stage!=2) {
	write_user(user,"Not (fully) connected to service.\n");
	return;
	}
sscanf(nl->remote_ver,"%d.%d.%d",&a,&b,&c);
if (a==3 && b<1) {
	write_user(user,"The NUTS version of that service does not support this facility.\n");
	return;
	}
sprintf(text,"RSTAT %s\n",user->name);
write_sock(nl->socket,text);
write_user(user,"Request sent.\n");
}


/*** Switch swearing ban on and off ***/
swban(user)
UR_OBJECT user;
{
if (!ban_swearing) {
	write_user(user,"Swearing ban ON.\n");
	sprintf(text,"%s switched swearing ban ON.\n",user->name);
	write_syslog(text,1);
	ban_swearing=1;  return;
	}
write_user(user,"Swearing ban OFF.\n");
sprintf(text,"%s switched swearing ban OFF.\n",user->name);
write_syslog(text,1);
ban_swearing=0;
}



/**************************** EVENT FUNCTIONS ******************************/

void do_events()
{
set_date_time();
check_idle_and_timeout();
check_nethangs_send_keepalives(); 
check_messages(0);
reset_alarm();
}


reset_alarm()
{
signal(SIGALRM,do_events);
alarm(heartbeat);
}


/*** login_time_out is the length of time someone can idle at login, 
     user_idle_time is the length of time they can idle once logged in. 
     Also ups users total login time. ***/
check_idle_and_timeout()
{
UR_OBJECT user,next;
int tm;

/* Use while loop here instead of for loop for when user structure gets
   destructed, we may lose ->next link and crash the program */
user=user_first;
while(user) {
	next=user->next;
	if (user->type==CLONE_TYPE) {  user=next;  continue;  }
	user->total_login+=heartbeat; 
	if (user->level>=WIZ) {
		user=next;  continue; /* Don't time out wizes and gods */
		}
	tm=(int)(time(0) - user->last_input);
	if (user->login && tm>=login_idle_time) {
		write_user(user,"\n\n*** Time out ***\n\n");
		disconnect_user(user);
		user=next;
		continue;
		}
	if (user->warned) {
		if (tm<user_idle_time-60) {  user->warned=0;  continue;  }
		if (tm>=user_idle_time) {
			write_user(user,"\n\n\07*** You have been timed out ***\n\n");
			disconnect_user(user);
			user=next;
			continue;
			}
		}
	if (!user->login && !user->warned && tm>=user_idle_time-60) {
		write_user(user,"\n\07*** WARNING: Input within 1 minute or you will be disconnected. ***\n\n");
		user->warned=1;
		}
	user=next;
	}
}
	


/*** See if any net connections are dragging their feet. If they have been idle
     longer than max_netidle the drop them. Also send keepalive signals down
     links, this saves having another function and loop to do it. ***/
check_nethangs_send_keepalives()
{
NL_OBJECT nl;
int secs;

for(nl=nl_first;nl!=NULL;nl=nl->next) {
	if (nl->type==UNCONNECTED) {
		nl->warned=0;  continue;
		}

	/* Send keepalives */
	nl->keepalive_cnt+=heartbeat;
	if (nl->keepalive_cnt>=keepalive_interval) {
		write_sock(nl->socket,"KA\n");
		nl->keepalive_cnt=0;
		}

	/* Check time outs */
	secs=(int)(time(0) - nl->last_recvd);
	if (nl->warned) {
		if (secs<net_idle_time-60) nl->warned=0;
		else {
			if (secs<net_idle_time) continue;
			sprintf(text,"SYSTEM: Disconnecting hung netlink to service %s in room %s.\n",nl->service,nl->connect_room->name);
			write_room(NULL,text);
			shutdown_netlink(nl);
			nl->warned=0;
			}
		continue;
		}
	if (secs>net_idle_time-60) {
		sprintf(text,"SYSTEM: Netlink to service %s in room %s has been hung for %d seconds.\n",nl->service,nl->connect_room->name,secs);
		write_wiz(ARCH,text,NULL);
		nl->warned=1;
		}
	}
destructed=0;
}



/*** Remove any expired messages from boards ***/
check_messages(force)
int force;
{
RM_OBJECT rm;
FILE *infp,*outfp;
char id[82],filename[80],line[82];
int valid,pt,write_rest;
int board_cnt,old_cnt;
static int done=0;

if (!force) {
	if (mesg_check_hour==thour && mesg_check_min==tmin) {
		if (done) return;
		}
	else {  done=0;  return;  }
	}
else printf("Checking boards...\n");
done=1;
board_cnt=0;
old_cnt=0;

for(rm=room_first;rm!=NULL;rm=rm->next) {
	rm->mesg_cnt=0;
	sprintf(filename,"%s/%s.B",DATAFILES,rm->name);
	if (!(infp=fopen(filename,"r"))) continue;
	if (!(outfp=fopen("tempfile","w"))) {
		if (force) fprintf(stderr,"NUTS: Couldn't open tempfile.\n");
		write_syslog("ERROR: Couldn't open tempfile in check_messages().\n",0);
		fclose(infp);
		return;
		}
	board_cnt++;
	/* We assume that once 1 in date message is encountered all the others
	   will be in date too , hence write_rest once set to 1 is never set to
	   0 again */
	valid=1; write_rest=0;
	fgets(line,82,infp); /* max of 80+newline+terminator = 82 */
	while(!feof(infp)) {
		if (*line=='\n') valid=1;
		sscanf(line,"%s %d",id,&pt);
		if (!write_rest) {
			if (valid && !strcmp(id,"PT:")) {
				/* 86400 = num. of secs in a day */
				if ((int)time(0) - pt < mesg_life*86400) {
					fputs(line,outfp);
					rm->mesg_cnt++;
					write_rest=1;
					}
				else old_cnt++;
				valid=0;
				}
			}
		else {
			fputs(line,outfp);
			if (valid && !strcmp(id,"PT:")) {
				rm->mesg_cnt++;  valid=0;
				}
			}
		fgets(line,82,infp);
		}
	fclose(infp);
	fclose(outfp);
	unlink(filename);
	if (!write_rest) unlink("tempfile");
	else rename("tempfile",filename);
	}
if (force) printf("  %d board files checked, %d out of date messages found.\n",board_cnt,old_cnt);
else {
	sprintf(text,"CHECK_MESSAGES: %d files checked, %d messages deleted.\n",board_cnt,old_cnt);
	write_syslog(text,1);
	}
}
/**************************** Made in England *******************************/
