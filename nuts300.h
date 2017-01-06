/****************** Header file for NUTS version 3.0.0a ******************/

#define DATAFILES "datafiles"
#define USERFILES "userfiles"
#define HELPFILES "helpfiles"
#define MAILSPOOL "mailspool"
#define NEWSFILE "newsfile"
#define MAPFILE "mapfile"
#define MOTD1 "motd1"
#define MOTD2 "motd2"

#define OUT_BUFF_SIZE 80
#define MAX_WORDS 10
#define WORD_LEN 20
#define ARR_SIZE 1000
#define MAX_LINES 10

#define USER_NAME_LEN 12
#define USER_DESC_LEN 30
#define PHRASE_LEN 40
#define PASS_LEN 8 /* 8 chars is all the crypt command uses anyway */
#define BUFSIZE 1000
#define ROOM_NAME_LEN 20
#define ROOM_LABEL_LEN 5
#define ROOM_DESC_LEN 811 /* 10 lines of 80 chars each + 10 nl + 1 \0*/
#define TOPIC_LEN 60
#define MAX_LINKS 10
#define SERV_NAME_LEN 20
#define SITE_NAME_LEN 80
#define CONV_LINES 10
/* DNL (Date Number Length) will have to become 12 on Sun Sep 9 02:46:40 2001 
   when all the unix timers will flip to 1000000000 :) */
#define DNL 11 

#define PUBLIC 0
#define PRIVATE 1
#define FIXED_PUBLIC 2
#define FIXED_PRIVATE 3

#define NEW 0
#define USER 1
#define WIZ 2
#define ARCH 3
#define GOD 4

char *level_name[]={
"NEW","USER","WIZ","ARCH","GOD","*"
};

/* The elements vis, listen, prompt and command_mode could all be
   bits in one flag var. as they're only ever 0 or 1 but I tried it and it
   made the code unreadable. Better to waste a few bytes */
struct user_struct {
     /* general (used by 2 or more types) */
     char name[USER_NAME_LEN+1];
     char desc[USER_DESC_LEN+1];
	char pass[PASS_LEN+6];
	char in_phrase[PHRASE_LEN+1],out_phrase[PHRASE_LEN+1];
	char buff[BUFSIZE],site[81],last_site[81],page_file[81];
	char mail_to[USER_NAME_LEN+1];
	struct room_struct *room,*invite_room;
	int port,login,socket,attempts,buffpos,filepos;
	int vis,listen,prompt,command_mode,muzzled,charmode_echo; 
	int level,misc_op,remote_com,edit_line,charcnt,warned,accreq;
	time_t last_input,last_login,total_login,read_mail;
	char *malloc_start,*malloc_end;
	struct netlink_struct *netlink;
	struct user_struct *prev,*next;
	};

typedef struct user_struct* UR_OBJECT;
UR_OBJECT user_first,user_last;

struct room_struct {
	char name[ROOM_NAME_LEN+1];
	char label[ROOM_LABEL_LEN+1];
	char desc[ROOM_DESC_LEN+1];
	char topic[TOPIC_LEN+1];
	char conv_line[CONV_LINES][161];
	int inlink; /* 1 if room accepts incoming net links */
	int access; /* public , private etc */
	int cln; /* conversation line number for recording */
	int mesg_cnt;
	char netlink_name[SERV_NAME_LEN+1]; /* temp store for config parse */
	char link_label[MAX_LINKS][ROOM_LABEL_LEN+1]; /* temp store for parse */
	struct netlink_struct *netlink; /* for net links, 1 per room */
	struct room_struct *link[MAX_LINKS];
	struct room_struct *next;
	};

typedef struct room_struct *RM_OBJECT;
RM_OBJECT room_first=NULL,room_last=NULL;
RM_OBJECT create_room();

/* Netlink stuff */
#define UNCONNECTED 0 
#define INCOMING 1 
#define OUTGOING 2
#define ALL 0
#define IN 1
#define OUT 2

/* Structure for net links, ie server initiates them */
struct netlink_struct {
	char service[SERV_NAME_LEN+1];
	char site[SITE_NAME_LEN+1];
	char verification[SERV_NAME_LEN+1];
	char remote_ver[20];
	char buffer[ARR_SIZE*2];
	char mail_to[USER_NAME_LEN+1];
	char mail_from[USER_NAME_LEN+1];
	FILE *mailfile;
	time_t last_recvd; 
	int port,socket,type,connected;
	int stage,lastcom,allow,warned,keepalive_cnt;
	struct user_struct *mesg_user;
	struct room_struct *connect_room;
	struct netlink_struct *prev,*next;
	};

typedef struct netlink_struct *NL_OBJECT;
NL_OBJECT nl_first=NULL,nl_last=NULL;
NL_OBJECT create_netlink();

char *syserror="Sorry, a system error has occured";
char *nosuchroom="There is no such room.\n";
char *nosuchuser="There is no such user.\n";
char *notloggedon="There is no one of that name logged on.\n";
char *invisenter="A presence enters the room...\n";
char *invisleave="A presence leaves the room.\n";

char *command[]={
"quit","look","mode","say","shout",
"tell","emote","semote","pemote","echo",
"go","listen","prompt","desc","inphr",
"outphr","public","private","letmein","invite",
"topic","move","bcast","who","people",
"home","shutdown","news","read","write",
"wipe","search","review","help","status",
"version","rmail","smail","dmail","entpro",
"examine","rmst","rmsn","netstat","netdata",
"connect","disconnect","passwd","kill","promote",
"demote","lban","ban","unban","vis",
"invis","site","wake","wizshout","muzzle",
"unmuzzle","map","logging","minlogin","system",
"charecho","clearline","fix","unfix","viewlog",
"accreq","*"
};

/* These are the minimum levels at which the commands can be executed. 
   Alter to suit. */
int com_level[]={
NEW,NEW,NEW,USER,USER,
USER,USER,USER,USER,USER,
USER,USER,USER,USER,USER,
USER,USER,USER,USER,USER,
USER,WIZ,ARCH,NEW,WIZ,
USER,GOD,USER,USER,USER,
WIZ,USER,USER,NEW,USER,
NEW,USER,USER,USER,USER,
USER,NEW,USER,WIZ,ARCH,
GOD,GOD,USER,WIZ,ARCH,
ARCH,WIZ,ARCH,ARCH,ARCH,
ARCH,WIZ,WIZ,WIZ,WIZ,
WIZ,USER,GOD,GOD,WIZ,
NEW,WIZ,GOD,GOD,ARCH,
NEW
};

enum comvals {
QUIT,LOOK,MODE,SAY,SHOUT,
TELL,EMOTE,SEMOTE,PEMOTE,ECHO,
GO,LISTEN,PROMPT,DESC,INPHRASE,
OUTPHRASE,PUBCOM,PRIVCOM,LETMEIN,INVITE,
TOPIC,MOVE,BCAST,WHO,PEOPLE,
HOME,SHUTDOWN,NEWS,READ,WRITE,
WIPE,SEARCH,REVIEW,HELP,STATUS,
VER,RMAIL,SMAIL,DMAIL,ENTPRO,
EXAMINE,RMST,RMSN,NETSTAT,NETDATA,
CON,DISCON,PASSWD,KILL,PROMOTE,
DEMOTE,LBAN,BAN,UNBAN,VIS,
INVIS,SITE,WAKE,WIZSHOUT,MUZZLE,
UNMUZZLE,MAP,LOGGING,MINLOGIN,SYSTEM,
CHARECHO,CLEARLINE,FIX,UNFIX,VIEWLOG,
ACCREQ
} com_num;

char *month[12]={
"January","February","March","April","May","June",
"July","August","September","October","November","December"
};

char *day[7]={
"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"
};

char *noyes1[]={ " NO","YES" };
char *noyes2[]={ "NO ","YES" };

char verification[SERV_NAME_LEN+1];
char text[ARR_SIZE];
char word[MAX_WORDS][WORD_LEN+1];
char wrd[8][81];
time_t boot_time;
int mainport,wizport,linkport,wizport_level,minlogin_level;
int password_echo,dos_newline,ignore_sigterm,listen_sock[3];
int max_users,num_of_users,num_of_logins,heartbeat;
int login_idle_time,user_idle_time,config_line,word_count;
int tyear,tmonth,tday,tmday,twday,thour,tmin,tsec;
int mesg_life,system_logging,prompt_def,no_prompt;
int force_listen,gatecrash_level,min_private_users;
int ignore_mp_level,rem_user_maxlevel,rem_user_deflevel;
int destructed,mesg_check_hour,mesg_check_min,net_idle_time;
int keepalive_interval,auto_connect;
extern char *sys_errlist[];

