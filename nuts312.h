/****************** Header file for NUTS version 3.1.2 ******************/

#define DATAFILES "datafiles"
#define USERFILES "userfiles"
#define HELPFILES "helpfiles"
#define MAILSPOOL "mailspool"
#define NEWSFILE "newsfile"
#define MAPFILE "mapfile"
#define SITEBAN "siteban"
#define USERBAN "userban"
#define SYSLOG "syslog"
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
#define PASS_LEN 20 /* only the 1st 8 chars will be used by crypt() though */
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

#define USER_TYPE 0
#define CLONE_TYPE 1
#define REMOTE_TYPE 2
#define CLONE_HEAR_NOTHING 0
#define CLONE_HEAR_SWEARS 1
#define CLONE_HEAR_ALL 2

/* The elements vis, listen, prompt, command_mode etc could all be bits in 
   one flag variable as they're only ever 0 or 1, but I tried it and it
   made the code unreadable. Better to waste a few bytes */
struct user_struct {
	char name[USER_NAME_LEN+1];
	char desc[USER_DESC_LEN+1];
	char pass[PASS_LEN+6];
	char in_phrase[PHRASE_LEN+1],out_phrase[PHRASE_LEN+1];
	char buff[BUFSIZE],site[81],last_site[81],page_file[81];
	char mail_to[WORD_LEN+1];
	struct room_struct *room,*invite_room;
	int type,port,login,socket,attempts,buffpos,filepos;
	int vis,listen,prompt,command_mode,muzzled,charmode_echo; 
	int level,misc_op,remote_com,edit_line,charcnt,warned;
	int accreq,last_login_len,listen_store,clone_hear,afk;
	time_t last_input,last_login,total_login,read_mail;
	char *malloc_start,*malloc_end;
	struct netlink_struct *netlink;
	struct user_struct *prev,*next,*owner;
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
RM_OBJECT room_first,room_last;
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
	char remote_ver[11];
	char buffer[ARR_SIZE*2];
	char mail_to[WORD_LEN+1];
	char mail_from[WORD_LEN+1];
	FILE *mailfile;
	time_t last_recvd; 
	int port,socket,type,connected;
	int stage,lastcom,allow,warned,keepalive_cnt;
	struct user_struct *mesg_user;
	struct room_struct *connect_room;
	struct netlink_struct *prev,*next;
	};

typedef struct netlink_struct *NL_OBJECT;
NL_OBJECT nl_first,nl_last;
NL_OBJECT create_netlink();

char *syserror="Sorry, a system error has occured";
char *nosuchroom="There is no such room.\n";
char *nosuchuser="There is no such user.\n";
char *notloggedon="There is no one of that name logged on.\n";
char *invisenter="A presence enters the room...\n";
char *invisleave="A presence leaves the room.\n";
char *invisname="A presence";
char *noswearing="Swearing is not allowed here.\n";

char *level_name[]={
"NEW","USER","WIZ","ARCH","GOD","*"
};

char *command[]={
"quit",    "look",     "mode",      "say",    "shout",
"tell",    "emote",    "semote",    "pemote", "echo",
"go",      "listen",   "prompt",    "desc",   "inphr",
"outphr",  "public",   "private",   "letmein","invite",
"topic",   "move",     "bcast",     "who",     "people",
"home",    "shutdown", "news",      "read",    "write",
"wipe",    "search",   "review",    "help",    "status",
"version", "rmail",    "smail",     "dmail",   "from",
"entpro",  "examine",  "rmst",      "rmsn",    "netstat",
"netdata", "connect",  "disconnect","passwd",  "kill",
"promote", "demote",   "lban",      "ban",     "unban",
"vis",     "invis",    "site",      "wake",    "wizshout",
"muzzle",  "unmuzzle", "map",       "logging", "minlogin",
"system",  "charecho", "clearline", "fix",     "unfix",
"viewlog", "accreq",   "revclr",    "clone",   "destroy",
"myclones","allclones","switch","clsay","clhear",
"rstat",   "swban",    "afk","*"
};


/* Values of commands , used in switch in exec_com() */
enum comvals {
QUIT,     LOOK,     MODE,     SAY,    SHOUT,
TELL,     EMOTE,    SEMOTE,   PEMOTE, ECHO,
GO,       LISTEN,   PROMPT,   DESC,   INPHRASE,
OUTPHRASE,PUBCOM,   PRIVCOM,  LETMEIN,INVITE,
TOPIC,    MOVE,     BCAST,    WHO,    PEOPLE,
HOME,     SHUTDOWN, NEWS,     READ,   WRITE,
WIPE,     SEARCH,   REVIEW,   HELP,   STATUS,
VER,      RMAIL,    SMAIL,    DMAIL,  FROM,
ENTPRO,   EXAMINE,  RMST,     RMSN,   NETSTAT,
NETDATA,  CONN,     DISCONN,  PASSWD, KILL,
PROMOTE,  DEMOTE,   LBAN,     BAN,    UNBAN,
VIS,      INVIS,    SITE,     WAKE,   WIZSHOUT,
MUZZLE,   UNMUZZLE, MAP,      LOGGING,MINLOGIN,
SYSTEM,   CHARECHO, CLEARLINE,FIX,    UNFIX,
VIEWLOG,  ACCREQ,   REVCLR,   CREATE, DESTROY,
MYCLONES, ALLCLONES,SWITCH,   CLSAY,  CLHEAR,
RSTAT,    SWBAN,    AFK
} com_num;


/* These are the minimum levels at which the commands can be executed. 
   Alter to suit. */
int com_level[]={
NEW, NEW, NEW, NEW, USER,
USER,USER,USER,USER,USER,
USER,USER,USER,USER,USER,
USER,USER,USER,USER,USER,
USER,WIZ, ARCH,NEW, WIZ,
USER,GOD, USER,NEW, USER,
WIZ, USER,USER,NEW, NEW,
NEW, USER,USER,USER,USER,
USER,USER,NEW, NEW, WIZ,
ARCH,GOD, GOD, USER,WIZ,
ARCH,ARCH,WIZ, ARCH,ARCH,
ARCH,ARCH,WIZ, WIZ, WIZ,
WIZ, WIZ, USER,GOD, GOD,
WIZ, NEW, WIZ, GOD, GOD,
ARCH,NEW, USER,ARCH,ARCH,
ARCH,USER,ARCH,ARCH,ARCH,
GOD, ARCH,USER
};

char *month[12]={
"January","February","March","April","May","June",
"July","August","September","October","November","December"
};

char *day[7]={
"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"
};

char *noyes1[]={ " NO","YES" };
char *noyes2[]={ "NO ","YES" };

/* These MUST be in upper case - the contains_swearing() function converts
   the string to be checked to upper case before it compares it against
   these */
char *swear_words[]={
"FUCK","SHIT","CUNT","BASTARD","*"
};

char verification[SERV_NAME_LEN+1];
char text[ARR_SIZE];
char word[MAX_WORDS][WORD_LEN+1];
char wrd[8][81];
time_t boot_time;
jmp_buf jmpvar;

int mainport,wizport,linkport,wizport_level,minlogin_level;
int password_echo,dos_newline,ignore_sigterm,listen_sock[3];
int max_users,max_clones,num_of_users,num_of_logins,heartbeat;
int login_idle_time,user_idle_time,config_line,word_count;
int tyear,tmonth,tday,tmday,twday,thour,tmin,tsec;
int mesg_life,system_logging,prompt_def,no_prompt;
int force_listen,gatecrash_level,min_private_users;
int ignore_mp_level,rem_user_maxlevel,rem_user_deflevel;
int destructed,mesg_check_hour,mesg_check_min,net_idle_time;
int keepalive_interval,auto_connect,ban_swearing,crash_recovery;

extern char *sys_errlist[];
