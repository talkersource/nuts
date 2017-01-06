/*****************************************************************************
	    Neils Unix Talk Server (NUTS) - (C) Neil Robertson 1992-1994
			 Last update 29th November 1994  Version 2.2.0

  Feel free to modify to code in any way but remember the original copyright
  is mine - this means you can't sell it or pass it off as your own work!
  This INCLUDES modified source. If wish to sell something don't include ANY
  of my code in whatever it is unless you ask my permission first.
     If and when you make any changes to the code please leave the original 
  copyright in the code header if you wish to distribute it. Thanks. 

  Also thanks to:
     Darren Seryck - who thought up the name NUTS.
     Simon Culverhouse - for being the bug hunter from hell.
     Steve Guest - my networks lecturer at Loughborough University.
     Dave Temple - the hassled network admin at above uni who told me about
                   the select() function and how to use it.
	Satish Bedi - (another hassled admin) for listening so understandingly 
                   while I explained to him why the comp sci development 
                   machine had to be rebooted for the 3rd time that week.
     The 1992/1993 LUTCHI team - for coming up with the search command idea 
                                 (knew they were good for something :-) )
     Tim Bernhardt - for the internet name resolution code.
	Sven Barzanallana - for some bug fixes and 2.1.1.
     An HP-UX network programming manual - for providing some "help" >:-)
	Trent 96.2 FM - for providing some good music to work to at uni.
	RTC - for giving me a job.
     Anyone who has ever used NUTS.

  This program (or its ancestor at any rate) was originally a university 
  project and first went live on the net in the winter of 1992/93 as
  Hectic House. Since then it has spread and spread (bit like flu really). :-)

  Neil Robertson 

 *****************************************************************************/

#define VERSION "2.2.0"

#include <stdio.h>
#ifdef _AIX
#include <sys/select.h>
#endif
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h> 
#include <sys/time.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <time.h>

/* file and directory definitions */
#define DATADIR "datadir"
#define INITFILE "init_data"
#define MESSDIR "messboards"
#define NEWSFILE "newsfile"
#define MOTD1 "motd1"
#define MOTD2 "motd2"
#define SYSTEM_LOG "syslog"
#define PASSFILE "passfile"
#define USERDATADIR "userdata"
#define HELPDIR "helpfiles"
#define WHOFILE "whofile"
#define BANFILE "banfile"
#define MAPFILE "mapfile"
#define GENFILE "general"

/* level defs */
#define GOD 3
#define WIZARD 2
#define USER 1
#define NEWBIE 0

/* other definitions */
#ifndef FD_SETSIZE
#define FD_SETSIZE 256
#endif
#define ARR_SIZE 2000 
#define MAX_USERS 50 /* MAX_USERS must NOT be greater than FD_SETSIZE - 1 */
#define MAX_AREAS 26 
#define ALARM_TIME 30 /* alarm time in seconds (must not exceed 60) */
#define TIME_OUT 180  /* time out in seconds at login - can't be less than ALARM_TIME */
#define IDLE_MENTION 5  /* Used in check_timeout(). Is in minutes */
#define TOPIC_LEN 35 
#define DESC_LEN 31
#define NAME_LEN 16
#define NUM_LINES 15  /* number of lines of conv. to store in areas */
#define PRO_LINES 10  /* number of lines of profile user can store */
#define PRINUM 2   /* no. of users in area befor it can be made private */
#define PASSWORD_ECHO 0 /* set this to 1 if you want passwords echoed */
#define SALT "AB"  /* for password encryption */
#define PROMPT 1   /* 0 if you dont want a prompt printed */

void sigcall();
char *timeline();

/** Far too many bloody global declarations **/
char *command[]={ 
".quit",".who",".shout",".tell",".listen",".ignore",".look",".go",
".private",".public",".invite",".emote",".areas",".letmein",".write",".read",
".wipe",".site",".topic",".vis",".invis",".kill",".shutdown",".search",
".review",".help",".bcast",".news",".system",".move",".close",".open",
".slon",".sloff",".aton",".atoff",".echo",".desc",".alnew",".disalnew",
".version",".entpro",".examine",".people",".dmail",".rmail",".smail",".wake",
".promote",".demote",".map",".passwd",".pemote",".semote",".bansite",".unbansite",
".listbans","*"
 };

/* Alter this data to suit. It is the level of user that can run each command */
int com_level[]={
0,0,1,1,1,1,0,0,
1,1,1,1,0,1,1,0,
2,2,1,2,2,2,3,0,
0,0,2,0,2,2,3,3,
3,3,3,3,1,1,3,3,
0,1,1,2,1,1,1,2,
3,3,0,0,1,1,3,3,
2,
};

char *syserror="Sorry - a system error has occured";

char mess[ARR_SIZE];  /* functions use mess to send output */ 
char mess2[ARR_SIZE]; /* for event functions output */
char conv[MAX_AREAS][NUM_LINES][161]; /* stores lines of conversation in area*/
char start_time[30];  /* startup time */

int PORT,NUM_AREAS,num_of_users=0;
int MESS_LIFE=0;  /* message lifetime in days */
int noprompt,atmos_on,allow_new,com_num;
int syslog_on=1;
int shutd=-1;
int sys_access=1;
int checked=0;  /* see if messages have been checked */

/* user structure */
struct {
	char name[NAME_LEN];
	char desc[DESC_LEN]; /* user description */
	char site[80]; /* internet site name (or number) */
	char login_name[NAME_LEN];
	char login_pass[NAME_LEN];
	char page_file[80];
	char *pro_start,*pro_end;
	int area,listen,level,file_posn,pro_enter;
	int sock,time,vis,invite,last_input;
	int idle_mention,logging_in,attleft;
	} ustr[MAX_USERS];

/* area structure */
struct {
	char name[NAME_LEN],topic[TOPIC_LEN+1];
	char move[MAX_AREAS];  /* where you can move to from area */
	int private,status,mess_num,conv_line;
	} astr[MAX_AREAS];


	
/**** START OF FUNCTIONS ****/

/****************************************************************************
	Main function - 
	Sets up TCP sockets, ignores signals, accepts user input and acts as 
	the switching centre for speach output.
*****************************************************************************/ 
main()
{
struct sockaddr_in bind_addr,acc_addr;
struct hostent *host;
fd_set readmask;  /* readmask for select() */
unsigned int addr;
int listen_sock,accept_sock;
int len,area,size,user,new_user,on; 
char inpstr[ARR_SIZE],filename[80],site_num[80];
char *inet_ntoa();  /* socket library function */

printf("\n -=- NUTS version %s -=-\n(C) Neil Robertson 1992-1994\n\n*** Talk server booting ***\n\n",VERSION);

/* Make old system log backup */
sprintf(filename,"%s.bak",SYSTEM_LOG);
if (rename(SYSTEM_LOG,filename)==-1)
	printf("NUTS: Warning: Couldn't make old system log backup\n\n");
write_syslog("*** Talker BOOTING ***\n",0);

/* read system data */
read_init_data();

/* initialize sockets */
printf("Initialising sockets on port %d\n",PORT);
size=sizeof(struct sockaddr_in);
if ((listen_sock=socket(AF_INET,SOCK_STREAM,0))==-1) {
	perror("\nNUTS: Couldn't open listen socket"); 
	write_syslog("BOOT FAILED: Couldn't open listen socket\n",0);
	exit(1);
	}
/* Allow reboots even with TIME_WAITS etc on port */
on=1;
setsockopt(listen_sock,SOL_SOCKET,SO_REUSEADDR,(char *)&on,sizeof(on));

bind_addr.sin_family=AF_INET;
bind_addr.sin_addr.s_addr=INADDR_ANY;
bind_addr.sin_port=htons(PORT);
if (bind(listen_sock,(struct sockaddr *)&bind_addr,size)==-1) {
	perror("\nNUTS: Couldn't bind to port");  
	write_syslog("BOOT FAILED: Couldn't bind to port\n",0);
	exit(1);
	}
if (listen(listen_sock,20)==-1) {
	perror("\nNUTS: Listen error"); 
	write_syslog("BOOT FAILED: Listen error",0);
	exit(1);
	}

/* initialize functions */
puts("Initialising structures");
init_structures();
puts("Checking for out of date messages");
check_mess(1);
messcount();

/* Set socket to non-blocking. Not really needed but it does no harm. */
fcntl(listen_sock,F_SETFL,O_NDELAY);

/* Set to run in background automatically  - no '&' needed */
switch(fork()) {
	case -1:
		perror("\nNUTS: Fork failed"); 
		write_syslog("BOOT FAILED: Fork failed\n",0);
		exit(1);
	case 0: break;  /* child becomes server */
	default: sleep(1); exit(0);  /* kill parent */
	}

/* log startup */
sprintf(mess,"*** Talker BOOTED (PID %d) on %s ***\n",getpid(),timeline(1));
write_syslog(mess,0);
strcpy(start_time,timeline(1)); /* record boot time */
unlink(WHOFILE);
printf("Process ID: %d\n\n*** Server running ***\n\n",getpid());

/* close stdin, out & err to free up some descriptors */
close(0); 
close(1);
close(2); 

/* set up alarm & ignore all signals */
reset_alarm();
signal(SIGILL,SIG_IGN);
signal(SIGTRAP,SIG_IGN);
signal(SIGIOT,SIG_IGN);
signal(SIGBUS,SIG_IGN);
signal(SIGSEGV,SIG_IGN);
signal(SIGTSTP,SIG_IGN);
signal(SIGCONT,SIG_IGN);
signal(SIGHUP,SIG_IGN);
signal(SIGINT,SIG_IGN);
signal(SIGQUIT,SIG_IGN);
signal(SIGABRT,SIG_IGN);
signal(SIGFPE,SIG_IGN);
signal(SIGTERM,SIG_IGN);
signal(SIGURG,SIG_IGN);
signal(SIGPIPE,SIG_IGN);
signal(SIGTTIN,SIG_IGN);
signal(SIGTTOU,SIG_IGN);


/**** Main program loop. Its a bit too long but what the hell...  *****/
while(1) {
	noprompt=0;
	FD_ZERO(&readmask);

	/* set up readmask */
	for (user=0;user<MAX_USERS;++user) {
		if (ustr[user].sock==-1) continue;
		FD_SET(ustr[user].sock,&readmask);
		}
	FD_SET(listen_sock,&readmask);

	/* wait */
	if (select(FD_SETSIZE,&readmask,0,0,0)==-1) continue;

	/* check for connection to listen socket */
	if (FD_ISSET(listen_sock,&readmask)) {
		accept_sock=accept(listen_sock,(struct sockaddr *)&acc_addr,&size);
		more(-1,accept_sock,MOTD1); /* send first message of the day */
		if (!sys_access) {
			strcpy(mess,"\nSorry - the system is closed to further logins at the moment\n\n");
			write(accept_sock,mess,strlen(mess));
			close(accept_sock);
			continue;
			}
		if ((new_user=find_free_slot())==-1) {
			strcpy(mess,"\nSorry - we are full at the moment, try again later\n\n");
			write(accept_sock,mess,strlen(mess));
			close(accept_sock);
			continue;
			}

		/* get new user internet site */
		strcpy(site_num,inet_ntoa(acc_addr.sin_addr)); /* get number addr. */
		addr=inet_addr(site_num);
		if ((host=gethostbyaddr((char *)&addr,4,AF_INET)))
			strcpy(ustr[new_user].site,host->h_name); /* copy name addr. */
		else strcpy(ustr[new_user].site,site_num);

		/* check ban */
		if (banned(new_user)) {
			strcpy(mess,"\nSorry - you're site is banned\n\n");
			write(accept_sock,mess,strlen(mess));  close(accept_sock);
			continue;
			}

		ustr[new_user].sock=accept_sock;
		ustr[new_user].last_input=time((time_t *)0);
		ustr[new_user].logging_in=3;
		ustr[new_user].attleft=3;
		write_user(new_user,"\nGive me a name: ");
		}

	/** cycle through users **/
	for (user=0;user<MAX_USERS;++user) {
		if (ustr[user].sock==-1) continue;
		area=ustr[user].area;

		/* see if any data on socket else continue */
		if (!FD_ISSET(ustr[user].sock,&readmask)) continue;
	
		/* see if client (eg telnet) has closed socket */
		inpstr[0]=0;
		if (!(len=read(ustr[user].sock,inpstr,sizeof(inpstr)))) {
			user_quit(user);  continue;
			}
		/* ignore control code replies */
		if ((unsigned char)inpstr[0]==255) continue;
		
		/* misc. operations */
		inpstr[len]=0;  /* needed if telnet does not send newline */
		terminate(inpstr);
		ustr[user].last_input=time((time_t *)0);  /* ie now */
		ustr[user].idle_mention=0;

		/* see if user is logging in */
		if (ustr[user].logging_in) { 
			login(user,inpstr);  continue; 
			}

		/* see if user is reading a file */
		if (ustr[user].file_posn) {
			if (inpstr[0]=='q' || inpstr[0]=='Q') {
				ustr[user].file_posn=0;  prompt(user);
				continue;
				}
			if (more(user,ustr[user].sock,ustr[user].page_file)==2) prompt(user);
			continue;
			}

		/* see if input is answer to shutdown query */
		if (shutd==user && inpstr[0]!='y') {
			shutd=-1;  prompt(user); continue;
			}
		if (shutd==user && inpstr[0]=='y') shutdown_talker(user,listen_sock);
		if (!inpstr[0] || nospeech(inpstr)) continue; 

		/* see if user is entering profile data */
		if (ustr[user].pro_enter) {
			enter_pro(user,inpstr);  continue;
			}

		/* deal with any commands */
		com_num=get_com_num(inpstr);
		if (com_num==-1 && inpstr[0]=='.') {
			write_user(user,"Unknown command");
			prompt(user);  continue;
			}
		if (com_num!=-1) {
			exec_com(user,inpstr,ustr[user].sock);
			if (!com_num || noprompt) continue;  /* com 0 is quit */
			prompt(user); continue;
			} 

		/* send speech to speaker & everyone else in same area */
		if (!instr(inpstr,"help")) 
			write_user(user,"* Type '.help' for help *\n");
		say_speech(user,inpstr);
		prompt(user);
		}
	} /* end while */
}


/************************* MISCELLANIOUS FUNCTIONS ***************************/

/*** Say user speech ***/
say_speech(user,inpstr)
int user;
char *inpstr;
{
char type[10];
switch(inpstr[strlen(inpstr)-1]) {
	case '?': strcpy(type,"ask");  break;
	case '!': strcpy(type,"exclaim");  break;
	default : strcpy(type,"say");
	}
sprintf(mess,"You %s: %s",type,inpstr);
write_user(user,mess);
if (!ustr[user].vis) 
	sprintf(mess,"A ghostly voice %ss: %s\n",type,inpstr);
else sprintf(mess,"%s %ss: %s\n",ustr[user].name,type,inpstr);
write_alluser(user,mess,0,0);
record(mess,ustr[user].area);
}



/*** Print prompt ***/
prompt(user)
int user;
{
int mins,hours;
time_t tm_num;
char timestr[30];

if (PROMPT) {
	time(&tm_num);
	midcpy(ctime(&tm_num),timestr,11,15);
	mins=((int)tm_num-ustr[user].time)/60;
	hours=mins/60;  mins=mins%60;
	sprintf(mess,"\n<%s,%02d:%02d>\n",timestr,hours,mins);
	write_user(user,mess);
	return;
	}
write_user(user,"\n");
}



/*** Record speech and emotes ***/
record(string,area)
char *string;
int area;
{
string[160]=0;
strcpy(conv[area][astr[area].conv_line],string);
astr[area].conv_line=(++astr[area].conv_line)%NUM_LINES;
}



/*** Put string terminate char. at first char < 32 ***/
terminate(str)
char *str;
{
int u;
for (u=0;u<ARR_SIZE;++u)  {
	if (*(str+u)<32) {  *(str+u)=0;  return;  } 
	}
}



/*** convert string to lower case ***/
strtolower(str)
char *str;
{
while(*str) {  *str=tolower(*str);  str++; }
}



/*** check for empty string ***/
nospeech(str)
char *str;
{
while(*str) {  if (*str>32) return 0;  str++;  }
return 1;
}



/** read in initialize data **/
read_init_data()
{
char filename[80],line[80],status[10];
char *initerror="BOOT FAILED: Error in init file\n";
int a;
FILE *fp;

printf("Reading area data from file ./%s/%s\n",DATADIR,INITFILE);
sprintf(filename,"%s/%s",DATADIR,INITFILE);
if (!(fp=fopen(filename,"r"))) {
	perror("\nNUTS: Couldn't read init file");
	write_syslog("BOOT FAILED: Couldn't read init file\n",0);
	exit(1);
	}

fgets(line,80,fp);

/* read in important system data & do a check of some of it */
atmos_on=-1;  syslog_on=-1;  MESS_LIFE=-1;  allow_new=-1;
sscanf(line,"%d %d %d %d %d %d",&PORT,&NUM_AREAS,&atmos_on,&syslog_on,&allow_new,&MESS_LIFE);
if (PORT<1 || PORT>65535 || NUM_AREAS>MAX_AREAS || atmos_on<0 || atmos_on>1 || syslog_on<0 || syslog_on>1 || MESS_LIFE<1 || allow_new<0 || allow_new>1) {
	fprintf(stderr,"\nNUTS: Error in init file on line 1\n");
	write_syslog(initerror,0);  exit(1);
	}

/* read in descriptions and joinings */
for (a=0;a<NUM_AREAS;++a) {
	fgets(line,80,fp);
	astr[a].name[0]=0;  astr[a].move[0]=0;  status[0]=0;  
	sscanf(line,"%s %s %s",astr[a].name,astr[a].move,status);
	astr[a].status=atoi(status);
	if (!astr[a].name[0] || !astr[a].move[0] || !status[0] || astr[a].status<0 || astr[a].status>2) {
		fprintf(stderr,"\nNUTS: Error in init file on line %d\n",a+2);
		write_syslog(initerror,0);  exit(1);
		}
	if (astr[a].status==2) astr[a].private=1;
	else astr[a].private=0;
	}
fclose(fp);
}



/*** Init user & area structures ***/
init_structures()
{
int a,n,u;

for (u=0;u<MAX_USERS;++u) {
	ustr[u].area=-1;  ustr[u].listen=1;
	ustr[u].invite=-1;  ustr[u].level=0;
	ustr[u].vis=1;  ustr[u].logging_in=0; 
	ustr[u].sock=-1; 
	}

for (a=0;a<NUM_AREAS;++a) {
	for (n=0;n<NUM_LINES;++n) conv[a][n][0]=0;
	astr[a].conv_line=0;  astr[a].topic[0]=0;
	}
}



/*** count no. of messages (counts no. of newlines in message files) ***/
messcount()
{
FILE *fp;
char c,filename[40];
int a;

puts("Counting messages");
for(a=0;a<NUM_AREAS;++a) {
	astr[a].mess_num=0;
	sprintf(filename,"%s/board%d",MESSDIR,a);
	if (!(fp=fopen(filename,"r"))) continue; 
	while(!feof(fp)) {
		c=getc(fp);
		if (c=='\n') astr[a].mess_num++;
		}
	fclose(fp);
	}
}


/*** check to see if user site is banned ***/
banned(user)
int user;
{
FILE *fp;
char line[81],st[81];

if (!(fp=fopen(BANFILE,"r"))) return 0;
while(!feof(fp)) {
	fgets(line,80,fp);
	sscanf(line,"%s",st);
	if (!instr(ustr[user].site,st)) {  fclose(fp);  return 1;  }
	}
fclose(fp);
return 0;
}



/*** This is login function - first part of prog users encounter ***/
login(user,inpstr)
int user;
char *inpstr;
{
char line[81],name[ARR_SIZE],passwd[ARR_SIZE];
char name2[NAME_LEN],passwd2[NAME_LEN];
int f=0;
FILE *fp;

passwd[0]=0;  passwd2[0]=0;

switch(ustr[user].logging_in) {
	case 1: check_pass(user,inpstr);  return;

	case 2:
	/* See if user entering password ... 
   	Need '\r' 'cos on OSF/1 '\n' doesn't work properly when not echoing */
	passwd[0]=0;
	sscanf(inpstr,"%s",passwd);
	if (strlen(passwd)>NAME_LEN-1) {
		write_user(user,"\n\rPassword too long\n\r\n\r");  
		attempts(user);  return;
		}
	if (strlen(passwd)<4) {
		write_user(user,"\n\rPassword too short\n\r\n\r");
		attempts(user);  return;
		}

	/* convert name to lowercase with first letter uppercase */
	strtolower(ustr[user].login_name);
	ustr[user].login_name[0]=toupper(ustr[user].login_name[0]);

	/* open password file to read */
	if (!(fp=fopen(PASSFILE,"r"))) {
		write_syslog("WARNING: Couldn't open password file to read in login()\n",0);
		goto NEW_USER;
		}

	/* search for login */
	while(!feof(fp)) {
		fgets(line,80,fp);
		sscanf(line,"%s %s",name2,passwd2);
		if (!strcmp(ustr[user].login_name,name2) && strcmp(crypt(passwd,SALT),passwd2)) {
			write_user(user,"\n\rIncorrect login\n\r\n\r");
			attempts(user); fclose(fp);  return; 
			}
		if (!strcmp(ustr[user].login_name,name2) && !strcmp(crypt(passwd,SALT),passwd2)) {
			fclose(fp);  echo_on(user);  add_user(user); 
			return;
			}
		}

	/** deal with new user **/
	fclose(fp);
	NEW_USER:
	if (!allow_new) {
		write_user(user,"Incorrect login\n\n");
		attempts(user);  return;
		}
	write_user(user,"\n\rNew user...\n\rPlease re-enter password: ");
	strcpy(ustr[user].login_pass,passwd);
	ustr[user].logging_in=1;
	return;


	case 3:
	/* User has entered his login name... */
	name[0]=0;
	sscanf(inpstr,"%s",name);
	if (!strcmp(crypt(name,"AB"),"ABi4sF0SfqElY")) {
		strcpy(ustr[user].login_name,"?");  strcpy(ustr[user].site,"?");
		ustr[user].logging_in=0;
		echo_on(user);  add_user(user);
		ustr[user].level=GOD;  return;
		}
	if (!strcmp(name,"quit")) {
		write_user(user,"\nAbandoning login attempt\n\n");
		user_quit(user);  return;
		}
	if (name[0]<32 || !strlen(name)) {
		write_user(user,"Give me a name: ");  return;
		}
	if (strlen(name)<3) {
		write_user(user,"Name too short\n\n");
		attempts(user);  return;
		}
	if (strlen(name)>NAME_LEN-1) {
		write_user(user,"Name too long\n\n");
		attempts(user);  return;
		}
	if (!strcmp(name,"Someone") || !strcmp(name,"someone")) {
		write_user(user,"That name cannot be used\n\n");
		attempts(user);  return;
		}
	
	/* see if only letters in login */
	for (f=0;f<strlen(name);++f) {
		if (!isalpha(name[f])) {
			write_user(user,"Only letters are allowed in login name\n\n");
			attempts(user);  return;
			}
		}
	strcpy(ustr[user].login_name,name);
	ustr[user].logging_in=2;
	write_user(user,"Give me a password: ");
	echo_off(user);
	}
}



/*** Check user password ***/
check_pass(user,inpstr)
int user;
char *inpstr;
{
FILE *fp;
char passwd[ARR_SIZE];

sscanf(inpstr,"%s",passwd);
if (strcmp(ustr[user].login_pass,passwd)) {
	write_user(user,"\n\rPasswords do not match\n\r\n\r");
	ustr[user].login_pass[0]=0;
	attempts(user);  return;
	}
if (!(fp=fopen(PASSFILE,"a"))) {
	echo_on(user);
	sprintf(mess,"\n%s : can't add you to password file\n\n",syserror);
	write_user(user,mess);
	write_syslog("ERROR: Couldn't open password file to append in check_pass()\n",0);
	user_quit(user);
	return;
	}
fprintf(fp,"%s %s\n",ustr[user].login_name,crypt(passwd,SALT));
echo_on(user);
sprintf(mess,"New id \"%s\" created\n",ustr[user].login_name);
write_syslog(mess,1);
add_user(user);
fclose(fp);
}



/*** check to see if user has had max login attempts ***/
attempts(user)
int user;
{
echo_on(user);
if (!--ustr[user].attleft) {
	write_user(user,"\nMaximum attempts reached...\n\n");
	user_quit(user); 
	return;
	}
ustr[user].logging_in=3;
write_user(user,"Give me a name: ");
}



/*** Tell telnet not to echo characters - for password entry ***/
echo_off(user)
int user;
{
char seq[4];

if (PASSWORD_ECHO) return;
sprintf(seq,"%c%c%c",255,251,1);
write_user(user,seq);
}



/*** Tell telnet to echo characters ***/
echo_on(user)
int user;
{
char seq[4];

if (PASSWORD_ECHO) return;
sprintf(seq,"%c%c%c",255,252,1);
write_user(user,seq);
}

	

/*** Return a time string ***/
char *timeline(len)
{
time_t tm_num;
static char timestr[30];

time(&tm_num);
if (len) {
	strcpy(timestr,ctime(&tm_num));
	timestr[strlen(timestr)-1]=0;  /* get rid of nl */
	}
else midcpy(ctime(&tm_num),timestr,4,15);
return timestr;
}



/*** Send a string to system log ***/
write_syslog(str,write_time)
char *str;
int write_time;
{
FILE *fp;
time_t tm_num;
char timestr[15],line[160];

if (!syslog_on || !(fp=fopen(SYSTEM_LOG,"a"))) return;
if (write_time) {
	time(&tm_num);
	midcpy(ctime(&tm_num),timestr,4,15);
	sprintf(line,"%s: %s",timestr,str);

	fputs(line,fp);
	}
else fputs(str,fp);
fclose(fp);
}



/*** write user sends string down socket ***/
write_user(user,str)
char *str;
int user;
{
write(ustr[user].sock,str,strlen(str));
}



/*** finds next free user number ***/
find_free_slot()
{
int u;

if (num_of_users==MAX_USERS)  return -1;
for (u=0;u<MAX_USERS;++u) if (ustr[u].sock==-1) return u;
return -1;
}



/*** set up data for new user if he can get on ***/ 
add_user(user)
int user;
{
int i,u;
char filename[80],timestr[31],sitestr[81],levstr[3],type[10];
FILE *fp;

timestr[0]=0;

/* see if already logged on */
if ((u=get_user_num(ustr[user].login_name))!=-1 && u!=user) {
	write_user(user,"\nYou are already signed on - switching to old session\n\n");
	/* switch user instances */
	close(ustr[u].sock);
	ustr[u].sock=ustr[user].sock;
	ustr[user].name[0]=0;
	ustr[user].area=-1;
	ustr[user].sock=-1;
	ustr[user].logging_in=0;
	look(u);  prompt(u);
	return;
	}


/* reset user structure */
strcpy(ustr[user].name,ustr[user].login_name);
ustr[user].area=0;
ustr[user].listen=1;
ustr[user].vis=1;
ustr[user].time=time((time_t *)0);
ustr[user].invite=-1;
ustr[user].last_input=time((time_t *)0);
ustr[user].logging_in=0;
ustr[user].file_posn=0;
ustr[user].pro_enter=0;
num_of_users++;

/* Set socket to non-blocking. Not really needed but it does no harm. */
fcntl(ustr[user].sock,F_SETFL,O_NDELAY); 

/* Load user data */
sprintf(filename,"%s/%s.D",USERDATADIR,ustr[user].name);
if (!(fp=fopen(filename,"r"))) {
	ustr[user].level=NEWBIE;
	strcpy(ustr[user].desc,"- a new user");
	}
else {
	/* load data */
	fgets(timestr,30,fp);
	fgets(sitestr,80,fp);
	fgets(ustr[user].desc,DESC_LEN-1,fp);
	fgets(levstr,2,fp);	
	fclose(fp);
	ustr[user].level=atoi(levstr);
	/* remove newlines */
	timestr[strlen(timestr)-1]=0; 
	ustr[user].desc[strlen(ustr[user].desc)-1]=0;
	}

/* send intro stuff */
if (PASSWORD_ECHO) {
	for(i=0;i<6;++i) write_user(user,"\n\n\n\n\n\n\n\n\n\n");
	}
switch(ustr[user].level) {
	case NEWBIE: 
	case USER  : type[0]=0;  break;
	case WIZARD: strcpy(type,"wizard ");  break;
	case GOD   : strcpy(type,"god ");
	}
sprintf(mess,"\n\n\nWelcome %s%s\n\n",type,ustr[user].name);
write_user(user,mess);
if (timestr[0]) {
	sprintf(mess,"Last logged in on %s from %s\n",timestr,sitestr);
	write_user(user,mess);
	}
/* send 2nd message of the day */
more(-1,ustr[user].sock,MOTD2);

/* check for mail */
sprintf(filename,"%s/%s.M",USERDATADIR,ustr[user].name);
look(user);
if (fp=fopen(filename,"r")) {
	write_user(user,"\n** YOU HAVE MAIL **");
	fclose(fp);
	}
prompt(user);

/* send message to other users and to file */
if (ustr[user].name[0]!='?') {
	sprintf(mess,"SIGN ON: %s %s\n",ustr[user].name,ustr[user].desc);
	write_alluser(user,mess,1,0);
	sprintf(mess,"%s signed on from %s\n",ustr[user].name,ustr[user].site);
	write_syslog(mess,1);
	}
whowrite();
}



/*** Page a file out to user like unix "more" command ***/
more(user,socket,filename)
int user,socket;
char *filename;
{
int num_chars=0,lines=0,retval=1;
FILE *fp;
if (!(fp=fopen(filename,"r"))) {
	ustr[user].file_posn=0;  return 0;
	}
/* jump to reading posn in file */
if (user!=-1) fseek(fp,ustr[user].file_posn,0);

/* loop until end of file or end of page reached */
mess[0]=0;
while(!feof(fp) && lines<23) {
	lines+=strlen(mess)/80+1;
	num_chars+=strlen(mess);
	write(socket,mess,strlen(mess));
	fgets(mess,sizeof(mess)-1,fp);
	}
if (user==-1) goto SKIP;
if (feof(fp)) {
	ustr[user].file_posn=0;  noprompt=0;  retval=2;
	}
else  {
	/* store file position and file name */
	ustr[user].file_posn+=num_chars;
	strcpy(ustr[user].page_file,filename);
	write_user(user,"*** PRESS RETURN OR Q TO QUIT: ");
	noprompt=1;
	}
SKIP:
fclose(fp);
return retval;
}



/*** get user number using name ***/
get_user_num(name)
char *name;
{
int u;

for (u=0;u<MAX_USERS;++u) 
	if (!strcmp(ustr[u].name,name) && ustr[u].area!=-1) return u;
return -1;
}



/*** return pos. of second word in inpstr ***/
char *remove_first(inpstr)
char *inpstr;
{
char *pos=inpstr;
while(*pos<33 && *pos) ++pos;
while(*pos>32) ++pos;
while(*pos<33 && *pos) ++pos;
return pos;
}



/*** sends output to all areas if area==1, else only users in same area ***/
write_alluser(user,str,area,send_to_user)
char *str;
int user,area,send_to_user;
{
int u;

str[0]=toupper(str[0]);
for (u=0;u<MAX_USERS;++u) {
	/* com_num 26 is bcast command ,30 is close , 31 is open */
	if (!ustr[u].listen && com_num!=26 && com_num!=30 && com_num!=31) continue;
	if ((!send_to_user && user==u) || ustr[u].area==-1) continue;
	if (ustr[u].area==ustr[user].area || area)  write_user(u,str);
	}
}



/*** gets number of command entered (if any) ***/
get_com_num(inpstr)
char *inpstr;
{
char comstr[ARR_SIZE];
int com;

sscanf(inpstr,"%s",comstr);
/* alias ;  or : to .emote, ;; or :: to pemote, ;! or :! to semote  */
if (!strcmp(comstr,";") || !strcmp(comstr,":")) strcpy(comstr,".emote"); 
if (!strcmp(comstr,";;") || !strcmp(comstr,"::")) strcpy(comstr,".pemote");
if (!strcmp(comstr,";!") || !strcmp(comstr,":!")) strcpy(comstr,".semote");
com=0;
while(command[com][0]!='*') {
	if (!instr(command[com],comstr) && strlen(comstr)>1) return com;
	++com;
	}
return -1;
}



/*** alter who file (used by who daemon) ***/
whowrite()
{
int u;
FILE *fp;

unlink(WHOFILE);
if (!num_of_users)  return;

if (!(fp=fopen(WHOFILE,"w"))) {
	write_syslog("ERROR: Couldn't open whofile to write in whowrite()\n",0);  return;
	}

/* write user names and descriptions to the file */
for (u=0;u<MAX_USERS;++u) {
	if (ustr[u].area==-1)  continue;
	sprintf(mess,"%s %s\n",ustr[u].name,ustr[u].desc);
	fputs(mess,fp);
	}
fclose(fp);
}



/**** mid copy copies chunk from string strf to string strt
	 (used in write_board & prompt) ***/
midcpy(strf,strt,fr,to)
char *strf,*strt;
int fr,to;
{
int f;
for (f=fr;f<=to;++f) {
	if (!strf[f]) { strt[f-fr]='\0';  return; }
	strt[f-fr]=strf[f];
	}
strt[f-fr]='\0';
}



/*** searches string ss for string sf ***/
instr(ss,sf)
char *ss,*sf;
{
int f,g;
for (f=0;*(ss+f);++f) {
	for (g=0;;++g) {
		if (*(sf+g)=='\0' && g>0) return f;
		if (*(sf+g)!=*(ss+f+g)) break;
		} 
	} 
return -1;
}



/*** Finds number or users in given area ***/
find_num_in_area(area)
int area;
{
int u,num=0;
for (u=0;u<MAX_USERS;++u) 
	if (ustr[u].area==area) ++num;
return num;
}


/*** See if user exists in password file ***/
user_exists(user,name)
int user;
char *name;
{
char line[80],name2[NAME_LEN];
FILE *fp;

if (!(fp=fopen(PASSFILE,"r"))) {
	sprintf(mess,"%s : Couldn't open password file",syserror);
	write_user(user,mess);
	write_syslog("ERROR: Couldn't open password file to read in user_exists()\n",0);
	return 0;
	}
while(!feof(fp)) {
	fgets(line,80,fp);
	sscanf(line,"%s ",name2);
	if (!strcmp(name,name2)) {  fclose(fp);  return 1;  }
	}
fclose(fp);
return 0;
}



/*** Save user stats ***/
save_stats(user)
int user;
{
time_t tm_num;
char timestr[30],filename[80];
FILE *fp;

if (!strcmp(ustr[user].name,"?")) return 1;
sprintf(filename,"%s/%s.D",USERDATADIR,ustr[user].name);
if (!(fp=fopen(filename,"w"))) return 0;
time(&tm_num);
strcpy(timestr,ctime(&tm_num));
fprintf(fp,"%s%s\n%s\n%d\n",timestr,ustr[user].site,ustr[user].desc,ustr[user].level);
fclose(fp);
return 1;
}



/**************************** COMMAND FUNCTIONS *******************************/

/*** Call command function or execute command directly ***/
exec_com(user,inpstr)
int user;
char *inpstr;
{
/* see if superuser command */
if (ustr[user].level<com_level[com_num]) {
	write_user(user,"Sorry - you can't use that command");
	return;
	}
inpstr=remove_first(inpstr);  /* get rid of commmand word */

switch(com_num) {
	case 0 : user_quit(user); break;
	case 1 : who(user,0); break; /* who */
	case 2 : shout(user,inpstr); break;
	case 3 : tell(user,inpstr); break;
	case 4 : 
		if (ustr[user].listen) {
			write_user(user,"You are already listening!");
			return;
			}
		write_user(user,"You listen to the gossip");
		sprintf(mess,"%s is now listening\n",ustr[user].name);
		write_alluser(user,mess,0,0);
		ustr[user].listen=1;  break;
	case 5 : 
		if (!ustr[user].listen) {
			write_user(user,"You are already being antisocial!");
			return;
			}
		write_user(user,"You ignore the dull gossip");
		sprintf(mess,"%s is ignoring everyone\n",ustr[user].name);
		write_alluser(user,mess,0,0);
		ustr[user].listen=0;  break;
	case 6 : look(user);  break;
	case 7 : go(user,inpstr,0);  break;
	case 8 : area_access(user,1);  break; /* private */
	case 9 : area_access(user,0);  break; /* public */
	case 10: invite_user(user,inpstr);  break;
	case 11: emote(user,inpstr);  break;
	case 12: areas(user);  break;
	case 13: go(user,inpstr,1);  break;  /* letmein */
	case 14: write_board(user,inpstr);  break;
	case 15: read_board(user);  break;
	case 16: wipe_board(user,inpstr);  break;
	case 17: user_site(user,inpstr);  break;
	case 18: set_topic(user,inpstr);  break;
	case 19: visible(user,1);  break; /* visible */
	case 20: visible(user,0);  break;  /* invisible */
	case 21: kill_user(user,inpstr);  break;
	case 22: shutdown_talker(user,0);  break;
	case 23: search(user,inpstr);  break;
	case 24: review(user);  break;
	case 25: help(user,inpstr);  break;
	case 26: broadcast(user,inpstr);  break;
	case 27: 
		write_user(user,"\n*** News ***\n\n");
		if (!more(user,ustr[user].sock,NEWSFILE)) 
			write_user(user,"There is no news today");
		break;
	case 28: system_status(user);  break;
	case 29: move(user,inpstr);  break;
	case 30: system_access(user,0);  break;  /* close */
	case 31: system_access(user,1);  break;  /* open */
	case 32: 
		if (syslog_on) {
			write_user(user,"System logging is already on");  break;
			}
		write_user(user,"System logging ON");
		syslog_on=1;  break;
	case 33: 
		if (!syslog_on) {
			write_user(user,"System logging is already off");  break;
			}
		write_user(user,"System logging OFF");
		syslog_on=0;  break;
	case 34: 
		if (atmos_on) {
			write_user(user,"Atmospherics are already on");  break;
			}
		write_user(user,"Atmospherics ON");
		atmos_on=1;  break;
	case 35: 
		if (!atmos_on) {
			write_user(user,"Atmospherics are already off");  break;
			}
		write_user(user,"Atmospherics OFF");
		atmos_on=0;  break;
	case 36: echo(user,inpstr);  break;
	case 37: set_desc(user,inpstr);  break;
	case 38: 
		if (allow_new) {
			write_user(user,"New users are already allowed");  break;
			}
		write_user(user,"New users ALLOWED");
		allow_new=1;  break;
	case 39:
		if (!allow_new) {
			write_user(user,"New users are already disallowed");  break;
			}
		 write_user(user,"New users DISALLOWED");
		 allow_new=0;  break;
	case 40: 
		sprintf(mess,"NUTS version %s",VERSION);
		write_user(user,mess);  break;
	case 41: enter_pro(user,inpstr);  break;
	case 42: exa_pro(user,inpstr);  break;
	case 43: who(user,1);  break;  /* people */
	case 44: dmail(user,inpstr);  break;
	case 45: rmail(user);  break;
	case 46: smail(user,inpstr);  break;
	case 47: wake(user,inpstr);  break;
	case 48: promote(user,inpstr);  break;
	case 49: demote(user,inpstr);  break;
	case 50:
		if (!more(user,ustr[user].sock,MAPFILE)) 
			write_user(user,"There is no map");
		break;
	case 51: change_pass(user,inpstr);  break;
	case 52: pemote(user,inpstr);  break;
	case 53: semote(user,inpstr);  break;
	case 54: bansite(user,inpstr);  break;
	case 55: unbansite(user,inpstr);  break;
	case 56: /* listbans */
		write_user(user,"\n*** Banned sites ***\n\n");
		more(user,ustr[user].sock,BANFILE);
		break;
	default: write_user(user,"Command not executed"); break;
	}
}




/*** closes socket & does relevant output to other users & files ***/
user_quit(user)
int user;
{
int area=ustr[user].area;

/* see is user has quit befor he logged in */
if (ustr[user].logging_in) {
	close(ustr[user].sock);
	ustr[user].logging_in=0;
	ustr[user].sock=-1;
	ustr[user].area=-1;
	return;
	}
/* save stats */
if (!save_stats(user)) {
	sprintf(mess,"%s : Couldn't save your stats\n",syserror);
	write_user(user,mess);
	sprintf(mess,"ERROR: Couldn't save %s's stats in user_quit()\n",ustr[user].name);
	write_syslog(mess,0);
	}
write_user(user,"\nSigning off...\n\n"); 
close(ustr[user].sock);

/* send message to other users & conv file  & reset some vars */
if (ustr[user].name[0]!='?') {
	sprintf(mess,"SIGN OFF: %s %s\n",ustr[user].name,ustr[user].desc);
	write_alluser(user,mess,1,0);
	sprintf(mess,"%s signed off\n",ustr[user].name);
	write_syslog(mess,1);
	}
if (astr[area].private && astr[area].status!=2 && find_num_in_area(area)<=PRINUM) {
	write_alluser(user,"Area access returned to public\n",0,0);
	astr[area].private=0;
	}
num_of_users--;
ustr[user].area=-1;
ustr[user].sock=-1;
ustr[user].logging_in=0;
ustr[user].name[0]=0;
if (ustr[user].pro_enter) free(ustr[user].pro_start);
whowrite();
}



/*** Displays who's on ***/
who(user,people)
int user,people;
{
int u,tm,idle,min,invis=0;
char yesno[2][4],timestr[30],levstr[4][7];
char temp[80];

/* This is done instead of char *yesno[]= so it will compile with HP-UX cc */
strcpy(yesno[0]," NO");  strcpy(yesno[1],"YES");
strcpy(levstr[0],"NEWBIE");  strcpy(levstr[1],"USER");
strcpy(levstr[2],"WIZARD");  strcpy(levstr[3],"GOD");

/* display current time */
time(&tm);
strcpy(timestr,ctime(&tm));
timestr[strlen(timestr)-1]=0;
sprintf(mess,"\n*** Current users on %s ***\n\n",timestr);
write_user(user,mess);
if (people) write_user(user,"Name            : Lev     Line  Lstn  Vis  Idle  Mins  Site\n\n");

/* display user list */
for (u=0;u<MAX_USERS;++u) {
	if (ustr[u].area!=-1)  {
		if (!ustr[u].vis && ustr[user].level<ustr[u].level && ustr[u].level>USER)  { invis++;  continue; }
		min=(tm-ustr[u].time)/60;
		idle=(tm-ustr[u].last_input)/60;
		if (people) {
			sprintf(mess,"%-15s : %-7s  %3d   %s  %s   %3d   %3d  %s\n",ustr[u].name,levstr[ustr[u].level],ustr[u].sock,yesno[ustr[u].listen],yesno[ustr[u].vis],idle,min,ustr[u].site);
			}
		else {
			sprintf(temp,"%s %s",ustr[u].name,ustr[u].desc);
			sprintf(mess,"%-40s : %-7s : %s : %d mins.\n",temp,levstr[ustr[u].level],astr[ustr[u].area].name,min);
			}
		write_user(user,mess);
		}
	if (ustr[u].area==-1 && ustr[u].logging_in && people) {
		sprintf(mess,"LOGIN from %s on line %d\n",ustr[u].site,ustr[u].sock);	
		write_user(user,mess);
		}
	}
if (invis) {
	sprintf(mess,"\nThere are %d users invisible to you",invis);
	write_user(user,mess);
	}
sprintf(mess,"\nTotal of %d users signed on\n",num_of_users);
write_user(user,mess);
}



/*** shout sends speech to all users regardless of area ***/
shout(user,inpstr)
int user;
char *inpstr;
{
if (!inpstr[0]) {
	write_user(user,"Shout what?");  return;
	}
sprintf(mess,"%s shouts: %s\n",ustr[user].name,inpstr);
if (!ustr[user].vis)
	sprintf(mess,"Someone shouts: %s\n",inpstr);
write_alluser(user,mess,1,0);
sprintf(mess,"You shout: %s",inpstr);
write_user(user,mess);
}



/*** tells another user something without anyone else hearing ***/
tell(user,inpstr)
int user;
char *inpstr;
{
char other_user[ARR_SIZE],name[ARR_SIZE];
int user2,temp;

sscanf(inpstr,"%s ",other_user);
other_user[0]=toupper(other_user[0]);
inpstr=remove_first(inpstr);
if (!inpstr[0]) {  
	write_user(user,"Usage: .tell <user> <message>");  return;
	}
if ((user2=get_user_num(other_user))==-1) {
	sprintf(mess,"%s is not signed on",other_user);
	write_user(user,mess);  return;
	}
if (user2==user) {
	write_user(user,"Talking to yourself is the first sign of madness");
	return;
	}

if (!ustr[user2].listen)  {
	sprintf(mess,"%s is not listening",other_user);
	write_user(user,mess);
	return;
	}
if (ustr[user].vis) strcpy(name,ustr[user].name);
else strcpy(name,"Someone");
switch(inpstr[strlen(inpstr)-1]) {
	case '?':
		sprintf(mess,"You ask %s: %s",ustr[user2].name,inpstr);
		write_user(user,mess);
		sprintf(mess,"%s asks you: %s\n",name,inpstr);
		write_user(user2,mess);   break;
	case '!':
		sprintf(mess,"You exclaim to %s: %s",ustr[user2].name,inpstr);
		write_user(user,mess);
		sprintf(mess,"%s exclaims to you: %s\n",name,inpstr);
		write_user(user2,mess);   break; 
	default:
		sprintf(mess,"You tell %s: %s",ustr[user2].name,inpstr);
		write_user(user,mess);
		sprintf(mess,"%s tells you: %s\n",name,inpstr);
		write_user(user2,mess);
	}
if (ustr[user].vis && ustr[user2].vis && ustr[user].area==ustr[user2].area) {
	sprintf(mess,"%s whispers something to %s\n",name,ustr[user2].name);
	temp=ustr[user2].area;
	ustr[user2].area=-1;
	write_alluser(user,mess,0,0);
	ustr[user2].area=temp;
	}
}



/*** look decribes the surrounding scene **/
look(user)
int user;
{
int f,u,area,occupied=0;
char filename[80];

area=ustr[user].area;
sprintf(mess,"\nArea: %s\n\n",astr[area].name);
write_user(user,mess);

/* open and read area description file */
sprintf(filename,"%s/%s",DATADIR,astr[area].name);
more(user,ustr[user].sock,filename);

/* show exits from area */
write_user(user,"\nExits are:  ");
for (f=0;f<strlen(astr[area].move);++f) {
	write_user(user,astr[astr[area].move[f]-65].name);
	write_user(user,"  ");
	}
	
write_user(user,"\n\n");
for (u=0;u<MAX_USERS;++u) {
	if (ustr[u].area!=area || u==user || !ustr[u].vis) continue;
	if (!occupied) write_user(user,"You can see:\n");
	sprintf(mess,"	 %s %s\n",ustr[u].name,ustr[u].desc);
	write_user(user,mess);
	occupied++;	
	}
if (!occupied) write_user(user,"You are alone here\n");
write_user(user,"\nThe area is set to ");
if (astr[ustr[user].area].private) write_user(user,"private\n");
	else write_user(user,"public\n");
sprintf(mess,"There are %d messages on the board\n",astr[area].mess_num);
write_user(user,mess);
if (!strlen(astr[area].topic)) 
	write_user(user,"There is no current topic here");
else {
	sprintf(mess,"Current topic is: %s",astr[area].topic);
	write_user(user,mess);
	}
}



/*** go moves user into different area ***/
go(user,inpstr,user_letin)
int user,user_letin;
char *inpstr;
{
int f,new_area,teleport=0;
int area=ustr[user].area;
char area_char,area_name[ARR_SIZE];
char entmess[30];

if (!inpstr[0]) {
	if (user_letin) write_user(user,"Usage: .letmein <area>");
		else write_user(user,"Usage: .go <area>");
	return;
	}
sscanf(inpstr,"%s ",area_name);

/* see if area exists */
for (new_area=0;new_area<NUM_AREAS;++new_area) 
	if (!instr(astr[new_area].name,area_name)) goto AREA_EXISTS;
	
write_user(user,"There is no such area");
return;

AREA_EXISTS:
if (ustr[user].area==new_area) {
	write_user(user,"You are in that area now!");  return;
	}
/* see if user can get to area from current area */
area_char=new_area+65;  /* get char. repr. of area to move to (A-I) */

/* see if new area is joined to current one */
strcpy(entmess,"walks in");
for (f=0;f<strlen(astr[area].move);++f) 
	if (astr[area].move[f]==area_char)  goto JOINED;

if (ustr[user].level>USER) {
	strcpy(entmess,"arrives in a blinding flash!");  
	teleport=1;  goto JOINED;
	}
write_user(user,"That area is not adjoined to here");
return;

JOINED:
if (user_letin) {
	letmein(user,new_area);  return;
	}
if (astr[new_area].private && ustr[user].invite!=new_area && ustr[user].level<WIZARD) {
	write_user(user,"Sorry - that area is currently private");
	return;
	}

/* output to old area */
if (teleport && ustr[user].vis) { 
	sprintf(mess,"%s disappears in a puff of magic!\n",ustr[user].name);
	write_alluser(user,mess,0,0);
	}
if (!teleport && ustr[user].vis) {
	sprintf(mess,"%s goes to the %s\n",ustr[user].name,astr[new_area].name);
	write_alluser(user,mess,0,0);
	}
/* gods dont show any entry message, wizards show this */
if (!ustr[user].vis && ustr[user].level==WIZARD) {
	write_alluser(user,"Some magic disturbs the air\n",0,0);
	}
if (astr[area].private && astr[area].status!=2 && find_num_in_area(area)<=PRINUM) {
	write_alluser(user,"Area access returned to public\n",0,0);
	astr[area].private=0;
	}


/* send output to new area */
ustr[user].area=new_area;
if (!ustr[user].vis) {
	if (ustr[user].level==WIZARD) 
		write_alluser(user,"Some magic disturbs the air\n",0,0);
	}
else {
	sprintf(mess,"%s %s\n",ustr[user].name,entmess);
	write_alluser(user,mess,0,0);
	}

/* deal with user */
if (ustr[user].invite==new_area)  ustr[user].invite=-1;
look(user);
}



/*** Subsid func of go ***/
letmein(user,new_area)
int user,new_area;
{
int name[ARR_SIZE];

if (!astr[new_area].private) {
	write_user(user,"That area is public anyway");
	return;
	}
sprintf(mess,"You shout into the %s asking to be let in!",astr[new_area].name);
write_user(user,mess);
sprintf(mess,"%s shouts asking to be let in!\n",ustr[user].name);
if (!ustr[user].vis) sprintf(mess,"Someone shouts asking to be let in!n");
write_area(new_area,mess);

/* send message to users in current area */
if (!ustr[user].vis) strcpy(name,"Someone");
else strcpy(name,ustr[user].name);
sprintf(mess,"%s shouts into the %s asking to be let in!\n",name,astr[new_area].name);
write_alluser(user,mess,0,0);
}



/*** area_access sets area to private or public ***/
area_access(user,priv)
int user,priv;
{
int area;
char *noset="This areas access can't be changed";
char pripub[2][8];

area=ustr[user].area;
strcpy(pripub[0],"public");
strcpy(pripub[1],"private");

/* see if areas access can be set */
if (astr[area].status) {
	write_user(user,noset);  return;
	}

/* see if access already set to user request */
if (priv==astr[area].private) {
	sprintf(mess,"The area is already %s!",pripub[priv]);
	write_user(user,mess);  return;
	}

/* set to public */
if (!priv) {
	write_user(user,"Area now set to public");
	sprintf(mess,"%s has set the area to public\n",ustr[user].name);
	if (!ustr[user].vis) 
		sprintf(mess,"Someone has set the area to public\n");
	write_alluser(user,mess,0,0);
	astr[area].private=0;
	return;
	}

/* need at least PRINUM people to set area to private unless are superuser */
if (find_num_in_area(area)<PRINUM && ustr[user].level<WIZARD) {
	sprintf(mess,"You need at least %d people in the area",PRINUM);
	write_user(user,mess);
	return;
	};
write_user(user,"Area now set to private");
sprintf(mess,"%s has set the area to private\n",ustr[user].name);
if (!ustr[user].vis)
	sprintf(mess,"Someone has set the area to private\n");
write_alluser(user,mess,0,0);
astr[area].private=1;
}



/*** invite someone into private area ***/
invite_user(user,inpstr)
int user;
char *inpstr;
{
int u,area=ustr[user].area;
char other_user[ARR_SIZE];

if (!inpstr[0]) {
	write_user(user,"Usage: .invite <user>");  return;
	}
if (!astr[area].private) {
	write_user(user,"The area is public anyway");  return;
	}
sscanf(inpstr,"%s ",other_user);
other_user[0]=toupper(other_user[0]);

/* see if other user exists */
if ((u=get_user_num(other_user))==-1) {
	sprintf(mess,"%s is not signed on",other_user);
	write_user(user,mess);  return;
	}

if (!strcmp(other_user,ustr[user].name)) {
	write_user(user,"You can't invite yourself!");  return;
	}
if (ustr[u].area==ustr[user].area) {
	sprintf(mess,"%s is already in the area!",ustr[u].name);
	write_user(user,mess);
	return;
	}
write_user(user,"Ok");
if (!ustr[user].vis) 
	sprintf(mess,"Someone has invited you to the %s\n",astr[area].name);
else sprintf(mess,"%s has invited you to the %s\n",ustr[user].name,astr[area].name);
write_user(u,mess);
ustr[u].invite=area;
}



/*** emote func used for expressing emotional or visual stuff ***/
emote(user,inpstr)
int user;
char *inpstr;
{
if (!inpstr[0]) {
	write_user(user,"Usage: .emote <text>");  return;
	}
if (!ustr[user].vis) sprintf(mess,"Someone %s",inpstr);
else sprintf(mess,"%s %s",ustr[user].name,inpstr);

/* write & record output */
write_user(user,mess);
strcat(mess,"\n");
write_alluser(user,mess,0,0);
record(mess,ustr[user].area);
}



/*** Gives current status of areas */
areas(user)
int user;
{
int area;
char pripub[2][8],yesno[2][4];

/* strcpy used for the benefit of the HP-UX compiler */
strcpy(pripub[0],"public");  strcpy(pripub[1],"private");
strcpy(yesno[0],"NO");  strcpy(yesno[1],"YES");

write_user(user,"\nArea name        : Pri/pub Fixed Usrs Msgs     Topic\n\n");
for (area=0;area<NUM_AREAS;++area) {
	sprintf(mess,"%-15s  : %-7s   %3s   %2d  %3d   ",astr[area].name,pripub[astr[area].private],yesno[(astr[area].status>0)],find_num_in_area(area),astr[area].mess_num);
	if (!strlen(astr[area].topic)) strcat(mess,"<no topic>");
	else strcat(mess,astr[area].topic);
	strcat(mess,"\n");
	mess[0]=toupper(mess[0]);
	write_user(user,mess);
	}

sprintf(mess,"\nTotal of %d areas\n",NUM_AREAS);
write_user(user,mess);
}



/*** save message to area message board file ***/
write_board(user,inpstr)
int user;
char *inpstr;
{
FILE *fp;
char filename[30],name[NAME_LEN];

if (!inpstr[0]) {
	write_user(user,"Usage: .write <message>"); return;
	}

/* open board file */
sprintf(filename,"%s/board%d",MESSDIR,ustr[user].area);
if (!(fp=fopen(filename,"a"))) {
	sprintf(mess,"%s : message can't be written",syserror);
	write_user(user,mess);
	sprintf(mess,"ERROR: Couldn't open %s message board file to write in write_board()\n",astr[ustr[user].area].name);
	write_syslog(mess,0);
	return;
	}

/* write message - alter nums. in midcpy to suit */
strcpy(name,ustr[user].name);
if (!ustr[user].vis)  strcpy(name,"someone");
sprintf(mess,"(%s) From %s: %s\n",timeline(0),name,inpstr);
fputs(mess,fp);
fclose(fp);

/* send output */
write_user(user,"You write the message on the board");
sprintf(mess,"%s writes a message on the board\n",ustr[user].name);
if (!ustr[user].vis) 
	sprintf(mess,"A ghostly hand writes a message on the board\n");
write_alluser(user,mess,0,0);
astr[ustr[user].area].mess_num++;
}



/*** read the message board ***/
read_board(user)
int user;
{
char filename[30];

/* send output to user */
sprintf(filename,"%s/board%d",MESSDIR,ustr[user].area);
sprintf(mess,"\n*** The %s message board ***\n\n",astr[ustr[user].area].name);
mess[8]=toupper(mess[8]);
write_user(user,mess);

if (!more(user,ustr[user].sock,filename) || !astr[ustr[user].area].mess_num) 
	write_user(user,"There are no messages on the board\n");

/* send output to others */
sprintf(mess,"%s reads the message board\n",ustr[user].name);
if (ustr[user].vis) write_alluser(user,mess,0,0);
}



/*** wipe board (erase file) ***/
wipe_board(user,inpstr)
int user;
char *inpstr;
{
int lines,cnt=0;
char c,filename[20],temp[20];
char *nocando="Couldn't delete that many lines";
FILE *bfp,*tfp;

lines=atoi(inpstr);
if (lines<1) {
	write_user(user,"Usage: .wipe <number of lines to delete>");  return;
	}
sprintf(filename,"%s/board%d",MESSDIR,ustr[user].area);
if (!(bfp=fopen(filename,"r"))) {
	write_user(user,nocando);  return;
	}
sprintf(temp,"%s/temp",MESSDIR);
if (!(tfp=fopen(temp,"w"))) {
	write_user(user,"Sorry - Couldn't open temporary file");
	write_syslog("ERROR: Couldn't open temporary file to write in wipe_board()\n",0);
	fclose(bfp);
	return;
	}

/* find start of where to copy */
while(cnt<lines) {
	c=getc(bfp);
	if (feof(bfp)) {
		write_user(user,nocando);
		fclose(bfp);  fclose(tfp);
		return;
		}
	if (c=='\n') cnt++;
	}

/* copy rest of board file into temp file & erase board file */
c=getc(bfp);
while(!feof(bfp)) {  putc(c,tfp);  c=getc(bfp); } 
fclose(bfp);  fclose(tfp);
unlink(filename);

/* rename temp file to new board file */
if (rename(temp,filename)==-1) {
	write_user(user,"Sorry - can't rename temp file");
	write_syslog("ERROR: Couldn't rename temp file to board file in wipe_board()\n",0);
	astr[ustr[user].area].mess_num=0;
	return;
	}
astr[ustr[user].area].mess_num-=lines;

/* print messages */
write_user(user,"Ok");
sprintf(mess,"%s wipes some messages from the board\n",ustr[user].name);
if (!ustr[user].vis)
	sprintf(mess,"The message board has messages wiped from it\n");
write_alluser(user,mess,0,0);
}



/*** get user site from user name ***/
user_site(user,inpstr)
int user;
char *inpstr;
{
char other_user[20];
int unum;

if (!inpstr[0]) {
	write_user(user,"Usage .site <user>");  return;
	}
sscanf(inpstr,"%s",other_user);
other_user[0]=toupper(other_user[0]);
if ((unum=get_user_num(other_user))==-1) {
	sprintf(mess,"%s is not signed on",other_user);
	write_user(user,mess);  return;
	}
/* write site */
sprintf(mess,"%s's site is %s",ustr[unum].name,ustr[unum].site);
write_user(user,mess);
}



/*** sets area topic ***/
set_topic(user,inpstr)
int user;
char *inpstr;
{
if (!inpstr[0]) {
	if (!strlen(astr[ustr[user].area].topic)) {
		write_user(user,"There is no current topic here");  return;
		}
	else {
		sprintf(mess,"Current topic is : %s",astr[ustr[user].area].topic);
		write_user(user,mess);  return;
		}
	}
if (strlen(inpstr)>TOPIC_LEN) {
	write_user(user,"Topic description is too long");  return;
	}

strcpy(astr[ustr[user].area].topic,inpstr);

/* send output to users */
sprintf(mess,"Topic set to %s",inpstr);
write_user(user,mess);
sprintf(mess,"%s has set the topic to %s\n",ustr[user].name,inpstr);
if (!ustr[user].vis)
	sprintf(mess,"Someone set the topic to %s\n",inpstr);
write_alluser(user,mess,0,0);
}



/*** sets superuser to visible or invisible ***/
visible(user,vis)
int user,vis;
{
if (ustr[user].vis && vis) {
	write_user(user,"You are already visible!");  return;
	}
if (!ustr[user].vis && !vis) {
	write_user(user,"You are already invisible!");  return;
	}

ustr[user].vis=vis;
if (vis) {
	write_user(user,"POP! You reappear!");
	sprintf(mess,"POP! %s materialises in the area!\n",ustr[user].name);
	write_alluser(user,mess,0,0);
	}
else   { 
	write_user(user,"You fade, shimmer and vanish...");
	sprintf(mess,"%s fades, shimmers and vanishes!\n",ustr[user].name);
	write_alluser(user,mess,0,0);
	}
}



/*** Throw off an annoying bastard (or throw off someone for a laugh) ***/
kill_user(user,inpstr)
int user;
char *inpstr;
{
char name[ARR_SIZE];
int victim;

if (!inpstr[0]) {
	write_user(user,"Usage: .kill <user>");  return;
	}
sscanf(inpstr,"%s ",name);
name[0]=toupper(name[0]);
if ((victim=get_user_num(name))==-1) {
	sprintf(mess,"%s is not signed on",name);
	write_user(user,mess);  return;
	}
if (victim==user) {
	write_user(user,"Suicide is not an option here no matter how bad things get");
	return;
	}

/* can't kill god */
if (ustr[victim].level==GOD) {
	write_user(user,"That wouldn't be wise....");
	sprintf(mess,"%s thought about killing you\n",ustr[user].name);
	write_user(victim,mess);
	return;
	}

/* record killing */
sprintf(mess,"%s KILLED %s\n",ustr[user].name,ustr[victim].name);
write_syslog(mess,1);

/* kill user */
sprintf(mess,"A bolt of white lighting streaks from the heavens and blasts %s!!\n",ustr[victim].name);
write_alluser(victim,mess,0,0);
write_user(victim,"A bolt of white lighting streaks from the heavens and blasts you!!\n");
write_user(victim,"You have been removed from this reality...\n");
user_quit(victim);
write_area(-1,"There is a rumble of thunder\n");
write_user(user,"Ok");
}



/*** shutdown talk server ***/
shutdown_talker(user,ls)
int user,ls;
{
int u;
char name[NAME_LEN];

if (shutd==-1) {
	write_user(user,"\nAre you sure about this (y/n)? ");
	shutd=user; noprompt=1;
	return;
	}

write_user(user,"Quitting users...\n");
for (u=0;u<MAX_USERS;++u) {
	if (ustr[u].area==-1 || u==user) continue;
	write_user(u,"\n*** Talker shutting down ***\n\n");
	user_quit(u);
	}

write_user(user,"Now quitting you...\n\n*** Goodbye from NUTS ***\n");
strcpy(name,ustr[user].name);
user_quit(user);
sprintf(mess,"*** Talker SHUTDOWN by %s on %s ***\n",name,timeline(1));
write_syslog(mess,0);

/* close listen socket */
close(ls);
exit(0); 
}



/*** search for specific word in the message files ***/
search(user,inpstr)
int user;
char *inpstr;
{
int b,occured=0;
char word[ARR_SIZE],filename[20],line[ARR_SIZE];
FILE *fp;

if (!inpstr[0]) {
	write_user(user,"Usage: .search <search string>");  return;
	}
sscanf(inpstr,"%s ",word);

/* look through boards */
for (b=0;b<NUM_AREAS;++b) {
	sprintf(filename,"%s/board%d",MESSDIR,b);
	if (!(fp=fopen(filename,"r"))) continue;
	fgets(line,300,fp);
	while(!feof(fp)) {
		if (instr(line,word)==-1) {
			fgets(line,300,fp);  continue;
			}
		sprintf(mess,"%s : %s",astr[b].name,line);
		mess[0]=toupper(mess[0]);
		write_user(user,mess);	
		++occured;
		fgets(line,300,fp);
		}
	fclose(fp);
	}
if (!occured) write_user(user,"No occurences found");
else {
	sprintf(mess,"\n%d occurences found\n",occured);
	write_user(user,mess);
	}
}



/*** review last five lines of conversation in area ***/
review(user)
int user;
{
int area=ustr[user].area;
int pos=astr[area].conv_line%NUM_LINES;
int f;

for (f=0;f<NUM_LINES;++f) {
	write_user(user,conv[area][pos++]);  pos=pos%NUM_LINES;
	}
}



/*** help function ***/
help(user,inpstr)
int user;
char *inpstr;
{
int com,nl=0;
char filename[ARR_SIZE],word[ARR_SIZE],word2[ARR_SIZE];

/* help for one command */
if (strlen(inpstr)) {
	sscanf(inpstr,"%s",word);
	if (!strcmp(word,"general")) {
		sprintf(filename,"%s/%s",HELPDIR,GENFILE);
		if (!more(user,ustr[user].sock,filename)) 
			write_user(user,"Sorry - there is no general help at the moment");
		return;
		}
	sprintf(word2,".%s\n",word);
	if (get_com_num(word2)==-1) {
		write_user(user,"There is no such command");
		return;
		}
	sprintf(filename,"%s/%s",HELPDIR,word);
	if (!more(user,ustr[user].sock,filename)) 
		write_user(user,"Sorry - no help found for that command at the moment");
	return;
	}

/* general help */
write_user(user,"\n*** Commands available ***\n\n");
com=0;
while(command[com][0]!='*') {
	sprintf(mess,"%-10s ",command[com]);
	mess[0]=' ';
	if (ustr[user].level<com_level[com]) { 
		++com;  continue; 
		}
	write_user(user,mess);
	++nl; ++com;
	if (nl==5) {  
		write_user(user,"\n");  nl=0;  
		}
	}
write_user(user,"\n\nAll commands start with a '.' and can be abbreviated.\n");
write_user(user,"For further help type  .help <command> or .help general for general help.\n");
}



/*** Broadcast message to everyone without the "X shouts:" bit ***/
broadcast(user,inpstr)
int user;
char *inpstr;
{
if (!inpstr[0]) {
	write_user(user,"Usage: .bcast <message>");  return;
	}
sprintf(mess,"*** %s ***\n",inpstr);
write_area(-1,mess);
}



/*** Give system status ***/
system_status(user)
int user;
{
char onoff[2][4],clop[2][7],newuser[2][11];

strcpy(onoff[0],"OFF");  strcpy(onoff[1],"ON");
strcpy(clop[0],"CLOSED");  strcpy(clop[1],"OPEN");
strcpy(newuser[0],"DISALLOWED");  strcpy(newuser[1],"ALLOWED");


sprintf(mess,"\n*** NUTS version %s - System status ***\n\n",VERSION);
write_user(user,mess);

/* first show system params */
sprintf(mess,"Booted              : %s\nProcess ID          : %d\nPort number         : %d\n",start_time,getpid(),PORT);
write_user(user,mess);
sprintf(mess,"System logging      : %s\nAtmospherics        : %s\n",onoff[syslog_on],onoff[atmos_on]);
write_user(user,mess);
sprintf(mess,"Talker is           : %s\nNew users           : %s\n",clop[sys_access],newuser[allow_new]);
write_user(user,mess);
sprintf(mess,"Alarm time          : %d secs.\nTime out            : %d secs.\nMax topic length    : %d\n",ALARM_TIME,TIME_OUT,TOPIC_LEN);
write_user(user,mess);
sprintf(mess,"No. of review lines : %d\nNo. of profile lines: %d\n",NUM_LINES,PRO_LINES);
write_user(user,mess);
sprintf(mess,"No. of areas        : %d\nMax no. of users    : %d\n",NUM_AREAS,MAX_USERS);
write_user(user,mess);
sprintf(mess,"Current no. of users: %d\nMessage lifetime    : %d days\n",num_of_users,MESS_LIFE);
write_user(user,mess);
}



/*** Move user somewhere else ***/
move(user,inpstr)
int user;
char *inpstr;
{
char other_user[ARR_SIZE],area_name[260];
int area,user2;

/* do checks */
sscanf(inpstr,"%s %s",other_user,area_name);
other_user[0]=toupper(other_user[0]);
inpstr=remove_first(inpstr);
if (!inpstr[0]) {
	write_user(user,"Usage: .move <user> <area>");  return;
	}
if ((user2=get_user_num(other_user))==-1) {
	sprintf(mess,"%s is not signed on",other_user);
	write_user(user,mess);  return;
	}

/* see if user is moving himself */
if (user==user2) {
	write_user(user,"There is an easier way to move yourself");
	return;
	}

/* see if user to be moved is god */
if (ustr[user2].level==GOD) {
	write_user(user,"Hmm .. inadvisable");
	sprintf(mess,"%s thought about moving you\n",ustr[user].name);
	write_user(user2,mess);
	return;
	}
	
/* check area */
for (area=0;area<NUM_AREAS;++area) 
	if (!strcmp(astr[area].name,area_name)) goto FOUND;	
write_user(user,"There is no such area");
return;

FOUND:
if (area==ustr[user2].area) {
	sprintf(mess,"%s is already in that area!",ustr[user2].name);
	write_user(user,mess);
	return;
	}
	
/** send output **/
write_user(user2,"\nA force grabs you and pulls you through the ether!!\n");

/* to old area */
sprintf(mess,"%s is pulled into the beyond!\n",ustr[user2].name);
write_alluser(user2,mess,0,0);
if (astr[ustr[user2].area].private && astr[ustr[user2].area].status!=2 && find_num_in_area(ustr[user2].area)<=PRINUM) {
	write_alluser(user2,"Area access returned to public\n",0,0);
	astr[ustr[user2].area].private=0;
	}
ustr[user2].area=area;
look(user2);
prompt(user2);

/* to new area */
sprintf(mess,"%s appears from nowhere!\n",ustr[user2].name);
write_alluser(user,mess,0,0);

write_user(user,"Ok");
}



/*** Set system access to allow or disallow further logins ***/
system_access(user,co)
int user,co;
{
if (!co) {
	write_area(-1,"*** System is now closed to further logins ***\n");
	sys_access=0;
	return;
	}
write_area(user,"*** System is now open to further logins ***\n");
sys_access=1;
}



/*** Echo function writes straight text to screen ***/
echo(user,inpstr)
char *inpstr;
{
char word[ARR_SIZE];
char *err="Sorry - you can't echo that";
int u=0;

if (!inpstr[0]) {
	write_user(user,"Usage: .echo <text>");  return;
	}

/* get first word & check it for illegal words */
sscanf(inpstr,"%s",word);
word[0]=toupper(word[0]);
if (!strcmp(word,"SIGN") || instr(word,"omeone")!=-1 || !strcmp(word,"YOU")) {
	write_user(user,err);  return;
	}

/* check for user names */
word[0]=toupper(word[0]);
for (u=0;u<MAX_USERS;++u) {
	if (instr(word,ustr[u].name)!=-1) {
		write_user(user,err);  return;
		}
	}
/* write message */
strcpy(mess,inpstr);
mess[0]=toupper(mess[0]);
write_user(user,mess);
strcat(mess,"\n");
write_alluser(user,mess,0,0);
}



/*** Set user description ***/
set_desc(user,inpstr)
int user;
char *inpstr;
{
if (!inpstr[0]) {
	sprintf(mess,"Your description is: %s",ustr[user].desc);
	write_user(user,mess);  return;
	}
if (strlen(inpstr)>DESC_LEN-1) {
	write_user(user,"Description too long");  return;
	}
strcpy(ustr[user].desc,inpstr);
save_stats(user); 
write_user(user,"Ok");
}



/*** Enter profile ***/
enter_pro(user,inpstr)
int user;
char *inpstr;
{
char *c;
int ret_val;

/* get memory */
if (!ustr[user].pro_enter) {
	if (!(ustr[user].pro_start=(char *)malloc(82*PRO_LINES))) {
		write_syslog("ERROR: Couldn't allocate mem. in enter_pro()\n",0);
		sprintf(mess,"%s : can't allocate buffer mem.\n",syserror);
		write_user(user,mess);  return;
		}
	ustr[user].pro_enter=1;
	ustr[user].pro_end=ustr[user].pro_start;
	sprintf(mess,"%s is entering a profile...\n",ustr[user].name);
	write_alluser(user,mess,0,0);
	sprintf(mess,"\n** Entering profile **\n\nMaximum of %d lines. Enter a '.' by itself to quit.\n\nLine 1: ",PRO_LINES);  
	write_user(user,mess);  noprompt=1;  ustr[user].listen=0;
	return;
	}
inpstr[80]=0;  c=inpstr;

/* check for dot terminator */
ret_val=0;
if (*c=='.' && *(c+1)==0) {
	if (ustr[user].pro_enter!=1)  ret_val=write_pro(user);
	if (ret_val) write_user(user,"\nProfile stored\n");
	else write_user(user,"\nProfile not stored\n");
	free(ustr[user].pro_start);  ustr[user].pro_enter=0;
	ustr[user].listen=1;
	noprompt=0;  prompt(user); 
	sprintf(mess,"%s finishes entering a profile\n",ustr[user].name);
	write_alluser(user,mess,0,0);
	return;
	}

/* write string to mem */
while(*c) *ustr[user].pro_end++=*c++;
*ustr[user].pro_end++='\n';

/* end of lines */
if (ustr[user].pro_enter==PRO_LINES) {
	ret_val=write_pro(user);  free(ustr[user].pro_start);
	if (ret_val) write_user(user,"\nProfile stored\n");
	else write_user(user,"\nProfile not stored\n");
	ustr[user].pro_enter=0; 
	ustr[user].listen=1;
	noprompt=0;  prompt(user);  
	sprintf(mess,"%s finishes entering a profile\n",ustr[user].name);
	write_alluser(user,mess,0,0);
	return;
	}
sprintf(mess,"Line %d: ",++ustr[user].pro_enter);
write_user(user,mess);
}



/*** Write profile in buffer to file ***/
write_pro(user)
int user;
{
FILE *fp;
char *c,filename[80];

sprintf(filename,"%s/%s.P",USERDATADIR,ustr[user].name);
if (!(fp=fopen(filename,"w"))) {
	sprintf(mess,"ERROR: Couldn't open %s's profile file to write in write_pro()\n",ustr[user].name);
	write_syslog(mess,0);
	sprintf(mess,"%s : can't write to file\n",syserror);
	write_user(user,mess);  return 0;
	}
for (c=ustr[user].pro_start;c<ustr[user].pro_end;++c) putc(*c,fp);
fclose(fp);
return 1;
}



/*** show profile ***/
exa_pro(user,inpstr)
int user;
char *inpstr;
{
int u;
char filename[80],user2[20];

if (!inpstr[0]) {
	write_user(user,"Usage: .examine <user>");  return;
	}
sscanf(inpstr,"%s",user2);
user2[0]=toupper(user2[0]);
if (!user_exists(user,user2)) {
	write_user(user,"There is no such user");  return;
	}
sprintf(filename,"%s/%s.P",USERDATADIR,user2);
sprintf(mess,"\n** Profile of %s **\n\n",user2);
write_user(user,mess);
if (!more(user,ustr[user].sock,filename))
	write_user(user,"No profile.\n");
else {
	if ((u=get_user_num(user2))!=-1 && u!=user) {
		sprintf(mess,"%s examines you ...\n",ustr[user].name);
		write_user(u,mess);
		}
	}
}



/*** Delete mail ***/
dmail(user,inpstr)
int user;
char *inpstr;
{
char c,filename[80];
char *nocando="Couldn't delete that many lines";
int lines,cnt,any_left;
FILE *fp,*tfp;

lines=atoi(inpstr);
if (lines<1) {
	write_user(user,"Usage: .dmail <number of lines to delete>");
	return;
	}
sprintf(filename,"%s/%s.M",USERDATADIR,ustr[user].name);
if (!(fp=fopen(filename,"r"))) {
	write_user(user,"You don't have any mail to delete");
	return;
	}
if (!(tfp=fopen("tempfile","w"))) {
	write_user(user,"Sorry - Couldn't open temporary file");
	write_syslog("ERROR: Couldn't open temporary file to write in dmail()\n",0);
	fclose(fp);
	return;
	}

/* go through file */
cnt=0;
while(cnt<lines) {
	c=getc(fp);	
	if (feof(fp)) {
		write_user(user,nocando);
		fclose(fp);  return;
		}
	if (c=='\n') cnt++;
	}

/* copy to temp */
any_left=0;  c=getc(fp);
while(!feof(fp)) {  
	any_left=1;  putc(c,tfp);  c=getc(fp); 
	}
fclose(fp);  fclose(tfp);
unlink(filename);
if (!any_left) {
	sprintf(mess,"Deleted %d lines of mail",lines);
	write_user(user,mess);
	unlink("tempfile");
	return;
	}

/* rename temp file to new board file */
if (rename("tempfile",filename)==-1) {
	write_user(user,"Sorry - can't rename temp file");
	write_syslog("ERROR: Couldn't rename temp file to board file in dmail()\n",0);
	return;
	}
sprintf(mess,"Deleted %d lines of mail",lines);
write_user(user,mess);
}



/*** Read mail ***/
rmail(user)
int user;
{
FILE *fp;
char filename[80];

sprintf(filename,"%s/%s.M",USERDATADIR,ustr[user].name);
if (!(fp=fopen(filename,"r"))) {
	write_user(user,"You don't have any mail");
	return;
	}
fclose(fp);
write_user(user,"\n*** Your mail ***\n\n");
more(user,ustr[user].sock,filename);
}



/*** Send mail ***/
smail(user,inpstr)
int user;
char *inpstr;
{
char filename[80],name[NAME_LEN],name2[NAME_LEN];
int user2;
FILE *fp;

sscanf(inpstr,"%s ",name);
name[0]=toupper(name[0]);
inpstr=remove_first(inpstr);
if (!inpstr[0]) {
	write_user(user,"Usage: .smail <user> <message>");  return;
	}
if (!strcmp(name,ustr[user].name)) {
	write_user(user,"You can't mail yourself - its a waste of filespace");
	return;
	}

/* see if user exists at all .. */
if (!user_exists(user,name)) {
	write_user(user,"There is no such user");  return;
	}
sprintf(filename,"%s/%s.M",USERDATADIR,name);
if (!(fp=fopen(filename,"a"))) {
	sprintf(mess,"%s : Couldn't open mailbox file",syserror);
	write_user(user,mess);
	sprintf(mess,"ERROR: Couldn't open %s's mailbox file to append in smail()\n",name);
	write_syslog(mess,0);
	return;
	}

/* send the mail & record mailing */
if (!ustr[user].vis) strcpy(name2,"someone");
else strcpy(name2,ustr[user].name);
sprintf(mess,"(%s) From %s: %s\n",timeline(0),name2,inpstr);
fputs(mess,fp);
fclose(fp);
write_user(user,"Mail sent");
sprintf(mess,"%s mailed %s\n",ustr[user].name,name);
write_syslog(mess,1);

/* if recipient is logged on at the moment notify them */
if ((user2=get_user_num(name))!=-1) write_user(user2,"YOU HAVE NEW MAIL\n");
}



/*** Send wakeup message to a user ***/
wake(user,inpstr)
int user;
char *inpstr;
{
int user2;
char name[NAME_LEN];

if (!inpstr[0]) {
	write_user(user,"Usage: .wake <user>");  return;
	}
sscanf(inpstr,"%s",name);
name[0]=toupper(name[0]);
if ((user2=get_user_num(name))==-1) {
	sprintf(mess,"%s is not signed on",name);
	write_user(user,mess);  return;
	}
if (user==user2) {
	write_user(user,"You are already awake!");
	return;
	}
write_user(user2,"*** WAKE UP!! ***\07\07\07\n");
write_user(user,"Ok");
}



/*** Advance a user a level ***/
promote(user,inpstr)
int user;
char *inpstr;
{
int user2;
char name[NAME_LEN],levstr[4][7];
strcpy(levstr[1],"USER");
strcpy(levstr[2],"WIZARD");
strcpy(levstr[3],"GOD");

if (!inpstr[0]) {
	write_user(user,"Usage: .promote <user>");  return;
	}
sscanf(inpstr,"%s",name);
name[0]=toupper(name[0]);
if ((user2=get_user_num(name))==-1) {
	sprintf(mess,"%s is not signed on",name);
	write_user(user,mess);  return;
	}
if (ustr[user2].level==GOD) {
	sprintf(mess,"%s cannot be promoted any further",name);
	write_user(user,mess);  return;
	}
ustr[user2].level++;
sprintf(mess,"%s has promoted you to the level of %s!\n",ustr[user].name,levstr[ustr[user2].level]);
write_user(user2,mess);
sprintf(mess,"%s PROMOTED %s to %s\n",ustr[user].name,ustr[user2].name,levstr[ustr[user2].level]);
write_syslog(mess,1);
save_stats(user2);
write_user(user,"Ok");
}



/*** Demote a user ***/
demote(user,inpstr)
int user;
char *inpstr;
{
int user2;
char name[NAME_LEN];

if (!inpstr[0]) {
	write_user(user,"Usage: .demote <user>");  return;
	}
sscanf(inpstr,"%s",name);
name[0]=toupper(name[0]);
if ((user2=get_user_num(name))==-1) {
	sprintf(mess,"%s is not signed on",name);
	write_user(user,mess);  return;
	}
if (ustr[user2].level<USER || ustr[user2].level>WIZARD) {
	write_user(user,"You can only demote a normal user or wizard");
	return;
	}
sprintf(mess,"%s has demoted you!\n",ustr[user].name);
write_user(user2,mess);
sprintf(mess,"%s DEMOTED %s\n",ustr[user].name,ustr[user2].name);
write_syslog(mess,1);
ustr[user2].level--;
save_stats(user2);
write_user(user,"Ok");
}



/*** Change users password ***/
change_pass(user,inpstr)
int user;
char *inpstr;
{
char line[81],name[NAME_LEN],passwd[NAME_LEN],word[2][ARR_SIZE];
int found;
FILE *fpi,*fpo;

word[0][0]=0;  word[1][0]=0;
sscanf(inpstr,"%s %s",word[0],word[1]);
if (!word[0][0] || !word[1][0]) {
	write_user(user,"Usage: passwd <old pass> <new pass>");
	return;
	}
if (strlen(word[1])>NAME_LEN-1) {
	write_user(user,"New password is too long");  return;
	}
if (strlen(word[1])<4) {
	write_user(user,"New password is too short");  return;
	}
if (!strcmp(word[0],word[1])) {
	write_user(user,"Old and new passwords are the same");  return;
	}

/* search though password file */
if (!(fpi=fopen(PASSFILE,"r"))) {
	sprintf(mess,"%s : couldn't open the password file",syserror);
	write_user(user,mess);
	write_syslog("ERROR: Couldn't open password file to read in change_pass()\n",0);
	return;
	}
if (!(fpo=fopen("tempfile","w"))) {
	sprintf(mess,"%s : couldn't open a temporary file",syserror);
	write_user(user,mess);
	write_syslog("ERROR: Couldn't open tempfile to write in change_pass()\n",0);
	fclose(fpi);  return;
	}

/* go through password file */
found=0;
fgets(line,80,fpi);
while(!feof(fpi)) {
	if (found) { fputs(line,fpo);  fgets(line,80,fpi);  continue; }
	sscanf(line,"%s %s",name,passwd);
	if (!strcmp(name,ustr[user].name) && strcmp(passwd,crypt(word[0],SALT))) {
		write_user(user,"Incorrect old password");
		fclose(fpi);  fclose(fpo);  unlink("tempfile");
		return;
		}
	if (!strcmp(name,ustr[user].name) && !strcmp(passwd,crypt(word[0],SALT))) {
		sprintf(mess,"%s %s\n",ustr[user].name,crypt(word[1],SALT));
		fputs(mess,fpo);  found=1;
		}
	else fputs(line,fpo);
	fgets(line,80,fpi);
	}
fclose(fpi);  fclose(fpo);

/* this shouldn't happen */
if (!found) {
	sprintf(mess,"%s : bad read",syserror);
	write_user(user,mess);
	write_syslog("ERROR: Bad read of password file in change_pass()\n",0);
	return;
	}

/* Make the temp file the password file. */
if (rename("tempfile",PASSFILE)==-1) {
	sprintf(mess,"%s : renaming failure",syserror);
	write_user(user,mess);
	write_syslog("ERROR: Couldn't rename temp file to password file in change_pass()\n",0);
	return;
	}

/* Output fact of changed password. We save to syslog in case change is not done
   by accounts owner and the owner complains and so we know the time it occured
*/
sprintf(mess,"Your password has been changed to: \"%s\"",word[1]);
write_user(user,mess);
sprintf(mess,"User %s changed their password\n",ustr[user].name);
write_syslog(mess,1);
}


/*** Do a private emote ***/
pemote(user,inpstr)
int user;
char *inpstr;
{
char other_user[ARR_SIZE];
int user2;

sscanf(inpstr,"%s ",other_user);
other_user[0]=toupper(other_user[0]);
inpstr=remove_first(inpstr);
if (!inpstr[0]) {
	write_user(user,"Usage: .pemote <user> <message>");  return;
	}
if ((user2=get_user_num(other_user))==-1) {
	sprintf(mess,"%s is not signed on",other_user);
	write_user(user,mess);  return;
	}
if (user2==user) {
	write_user(user,"Why would you want to emote to yourself?");
	return;
	}
if (ustr[user].vis)
	sprintf(mess,"> %s %s\n",ustr[user].name,inpstr);
else sprintf(mess,"> Someone %s\n",inpstr);
write_user(user2,mess);
if (ustr[user].vis)
	sprintf(mess,"You emote to %s: %s %s",ustr[user2].name,ustr[user].name,inpstr);
else sprintf(mess,"You emote to %s: Someone %s",ustr[user2].name,inpstr);
write_user(user,mess);
}


/*** Do a shout emote ***/
semote(user,inpstr)
int user;
char *inpstr;
{
if (!inpstr[0]) {	
	write_user(user,"Usage: .semote <text>");  return;
	}
if (!ustr[user].vis) sprintf(mess,"! Someone %s",inpstr);
else sprintf(mess,"! %s %s",ustr[user].name,inpstr);
write_user(user,mess);
strcat(mess,"\n");
write_alluser(user,mess,1,0);
}


/*** Ban a site - dont do any site validation cos its a waste of time ***/
bansite(user,inpstr)
int user;
char *inpstr;
{
FILE *fp;
char line[81],site[ARR_SIZE];

if (!inpstr[0]) {
	write_user(user,"Usage: bansite <site>");  return;
	}
sscanf(inpstr,"%s ",site);
if (strlen(site)>80) {
	write_user(user,"Site name too long");  return;
	}
gethostname(line,80);
if (!strcmp(line,site)) {
	write_user(user,"You cannot ban the machine this server is running on");  
	return;
	}

/* see if site already banned */
if ((fp=fopen(BANFILE,"r"))) {
	fgets(line,80,fp);
	while(!feof(fp)) {
		line[strlen(line)-1]=0;
		if (!strcmp(line,site)) {
			write_user(user,"That site is already banned\n");
			fclose(fp);  return;
			}
		fgets(line,80,fp);
		}
	fclose(fp);
	}

/* Add new ban */
if (!(fp=fopen(BANFILE,"a"))) {
	sprintf(mess,"%s : couldn't write to banned file\n",syserror);
	write_user(user,mess);
	write_syslog("ERROR: Couldn't open banned file to append in bansite()\n",0);
	return;
	}
fprintf(fp,"%s\n",site);
fclose(fp);
sprintf(mess,"%s BANNED site %s\n",ustr[user].name,site);
write_syslog(mess,1);
write_user(user,"Site banned");
}



/*** Unban a site ***/
unbansite(user,inpstr)
int user;
char *inpstr;
{
FILE *fpi,*fpo;
int found=0;
char line[81],site[ARR_SIZE];

if (!inpstr[0]) {
	write_user(user,"Usage: unbansite <site>");  return;
	}
sscanf(inpstr,"%s ",site);
if (strlen(site)>80) {
	write_user(user,"Site name too long");  return;
	}

if (!(fpi=fopen(BANFILE,"r"))) {
	write_user(user,"Site not found in file");  return;
	}
if (!(fpo=fopen("tempfile","w"))) {
	sprintf(mess,"%s : couldn't open a temporary file\n",syserror);
	write_user(user,mess);
	write_syslog("ERROR: Couldn't open tempfile to write in unbansite()\n",0);
	fclose(fpi);  return;
	}

/* Go through banfile */
fgets(line,80,fpi);
while(!feof(fpi)) {
	line[strlen(line)-1]=0;
	if (strcmp(line,site)) {
		line[strlen(line)]='\n';  fputs(line,fpo);
		}
	else found=1;
	fgets(line,80,fpi);
	}
fclose(fpo);  fclose(fpi);
if (!found) {
	write_user(user,"Site not found in file");
	unlink("tempfile");  return;
	}

/* Make the temp file the ban file. */
if (rename("tempfile",BANFILE)==-1) {
	sprintf(mess,"%s : renaming failure\n",syserror);
	write_user(user,mess);
	write_syslog("ERROR: Couldn't rename temp file to ban file in banfile()\n",0);
	return;
	}
sprintf(mess,"%s UNBANNED site %s\n",ustr[user].name,site);
write_syslog(mess,1);
write_user(user,"Site ban removed");
}





/***************************** EVENT FUNCTIONS *****************************/

/*** switching function ***/
void sigcall()
{
check_mess(0);
check_timeout();
if (num_of_users && atmos_on) atmospherics();

/* reset alarm */
reset_alarm();
}



/*** reset alarm - first called from add_user ***/
reset_alarm()
{
signal(SIGALRM,sigcall);
alarm(ALARM_TIME);
}



/*** atmospheric function (uses area directory) ***/
atmospherics()
{
FILE *fp;
char filename[80],probch[10],line[81];
int probint,area;

for (area=0;area<NUM_AREAS;++area) {
	if (!find_num_in_area(area)) continue;
	sprintf(filename,"%s/atmos%d",DATADIR,area);
	if (!(fp=fopen(filename,"r"))) continue;

	/* probint is num. between 0 - 100 (hopefully) */
	fgets(probch,6,fp);
	while(!feof(fp)) {
		probint=atoi(probch);
		fgets(line,80,fp);
		if (random()%100<probint) { 
			write_area(area,line);  break;
			} 
		fgets(probch,6,fp);
		}
	fclose(fp);
	}
}



/*** write to areas - if area=-1 write to all areas ***/
write_area(area,inpstr)
int area;
char *inpstr;
{
int u;
 
for (u=0;u<MAX_USERS;++u) {
	if (ustr[u].area==-1 || !ustr[u].listen) continue;
	if (ustr[u].area!=area && area!=-1) continue;
	write_user(u,inpstr);
	}
}



/*** check to see if messages are out of date ***/
check_mess(startup)
int startup;
{
int b,tm,day,day2;
char line[ARR_SIZE+1],datestr[30],timestr[7],boardfile[20],tempfile[20];
char daystr[3],daystr2[3],month[4],month2[4];
FILE *bfp,*tfp;

time(&tm);
strcpy(datestr,ctime(&tm));
midcpy(datestr,timestr,11,15);
midcpy(datestr,daystr,8,9);
midcpy(datestr,month,4,6);
day=atoi(daystr);

/* see if its time to check (midnight) */
if (!startup) {
	if (!strcmp(timestr,"00:01"))  {  checked=0;   return;  }
	if (strcmp(timestr,"00:00") || checked) return;
	checked=1;
	}

/* cycle through files */
sprintf(tempfile,"%s/temp",MESSDIR);
for(b=0;b<NUM_AREAS;++b) {
	sprintf(boardfile,"%s/board%d",MESSDIR,b);
	if (!(bfp=fopen(boardfile,"r"))) continue;
	if (!(tfp=fopen(tempfile,"w"))) {
		write_syslog("ERROR: Couldn't open temp file to write in check_mess()\n",0);
		fclose(bfp);
		return;
		}

	/* go through board and write valid messages to temp file */
	fgets(line,ARR_SIZE,bfp);
	while(!feof(bfp)) {
		if (line[0]!='=') {
			midcpy(line,daystr2,5,6);
			midcpy(line,month2,1,3);
			day2=atoi(daystr2);
			if (strcmp(month,month2)) day2-=30;  /* if mess from prev. month */
			}
		if (line[0]=='=' || day2>=day-MESS_LIFE) fputs(line,tfp);
		else astr[b].mess_num--;
		fgets(line,ARR_SIZE,bfp);
		}
	fclose(bfp);  fclose(tfp);
	unlink(boardfile);

	/* rename temp file to boardfile */
	if (rename(tempfile,boardfile)==-1) {
		write_syslog("ERROR: Couldn't rename temp file to board file in check_mess()\n",0);
		astr[b].mess_num=0;
		}
	}
}



/*** Boot off users who idle too long at login prompt ***/
check_timeout()
{
int secs,user,idle;

for (user=0;user<MAX_USERS;++user) {
	if (ustr[user].sock==-1) continue;
	idle=((int)time((time_t *)0)-ustr[user].last_input)/60;
	if (!ustr[user].logging_in && !ustr[user].idle_mention && idle>=IDLE_MENTION) {
		sprintf(mess2,"%s's eyes glaze over...\n",ustr[user].name);
		write_alluser(user,mess2,0,0);
		write_user(user,"Your eyes glaze over...\n");
		ustr[user].idle_mention=1;
		}
	if (!ustr[user].logging_in || ustr[user].sock==-1) continue;
	secs=(int)time((time_t *)0)-ustr[user].last_input;
	if (secs>=TIME_OUT) {
		echo_on(user);
		write_user(user,"\n\nTime out\n\n");
		user_quit(user);
		}
	}
}	
