/***************************************************************************
 FILE: main.cc
 LVU : 1.4.0

 DESC:
 This file contains the main function, the initialisation and system 
 configuration parse code, and the main loop (for both user and server input).

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

#define MAINFILE
#include "globals.h"


// Module variables
static fd_set readmask; 
static int suppress_config_info;
static struct sigaction sigact;


// Forward declarations
void parse_command_line(int argc, char **argv);
void init();
void parse_config_file();
int  parse_config_integer(cl_splitline *sl, int linenum);
int  parse_config_time(cl_splitline *sl,int linenum, int gzero);
int  parse_config_level(cl_splitline *sl,int linenum);
int  parse_config_yn(cl_splitline *sl,int linenum);
void parse_remote_server_line(cl_splitline *sl,int linenum);
void parse_signal_ignore(cl_splitline *sl, int linenum);
void load_bans();
int load_user_ban(int ban_level,cl_splitline *sl);
int load_site_ban(int ban_level,cl_splitline *sl);
void create_system_groups();
void load_public_groups();
void spawn_all_threads();
void mainloop();
void setup_readmask();
void get_input();

extern "C" { // Stops Solaris compiler whining
void signal_handler(int signum, siginfo_t *siginfo, void *notused);
}



/******** Start here *********/
int main(int argc, char **argv)
{
// Set for reboot execvp()
global_argv = argv;

strcpy(build,"<default>");

#ifdef LINUX
strcpy(build,"LINUX");
#endif

#ifdef BSD
if (build[0] != '<') {
	puts("ERROR: Multiple -D <OS> lines passed to compiler! Fix Makefile and recompile.");
	exit(1);
	}
strcpy(build,"BSD");
#endif

#ifdef SOLARIS
if (build[0] != '<') {
	puts("ERROR: Multiple -D <OS> lines passed to compiler! Fix Makefile and recompile.");
	exit(1);
	}
strcpy(build,"SOLARIS");
#endif

#ifdef _AIX
// This is defined by the OS , not the makefile
strcpy(build,"AIX");
#endif

parse_command_line(argc,argv);

log(0,"\n@~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~)\n");
log(0," | NUTS-IV version   : %s              |\n",VERSION);
log(0," | NIVN protocol rev.: %d                  |\n",PROTOCOL_REVISION);
log(0," | Build             : %-9s          |\n",build);
log(0," | Process id        : %-5d              |\n",main_pid);
log(0," |                                        |\n");
log(0," | Copyright (C) Neil Robertson 2003-2005 |\n");
log(0,"@__________________________________________)\n\n");

init();
spawn_all_threads();
mainloop();
}




/*** Go through the command line arguments ***/
void parse_command_line(int argc, char **argv)
{
struct stat fs;
char *cf;
int i,len;

config_file = CONFIG_FILE;
be_daemon = 0;
logfile = NULL;
suppress_config_info = 0;

for(i=1;i < argc;++i) {
	if (argv[i][0] != '-' || strlen(argv[i]) != 2) goto USAGE;

	switch(argv[i][1]) {
		case 'c':
		if (i == argc - 1) goto USAGE;
		cf = argv[++i];

		// If we don't have a config extension on the end then add it
		len = strlen(cf);
		config_file = (char *)malloc(len + 5);
		if (len > 3 && !strcmp(cf+len-4,CONFIG_EXT))
			strcpy(config_file,cf);
		else sprintf(config_file,"%s%s",cf,CONFIG_EXT);
		break;

		case 'd': be_daemon = 1;  break;

		case 'l':
		if (i == argc - 1) goto USAGE;
		logfile = argv[++i];
		break;
	
		case 'V':
		printf("NUTS-IV version   : %s\n",VERSION);
		printf("NIVN protocol rev.: %d\n",PROTOCOL_REVISION);
		printf("Build             : %s\n",build);
		exit(0);

		case 's':
		suppress_config_info = 1;
		break;


		default: goto USAGE;
		}
	}

/* Make sure logfile if it already exists is a normal file as it might hang
   the talker in the viewlog command if it isn't - eg /dev/tty. Make an
   exception for /dev/null. */
if (logfile && strcmp(logfile,"/dev/null")) {
	if (stat(logfile,&fs) != -1 && (fs.st_mode & S_IFMT) != S_IFREG) {
		puts("ERROR: The log file must either be a regular file or /dev/null.");
		exit(1);
		}
	// Clear old logfile
	unlink(logfile);
	}

// Fork off. So to speak. More tea vicar?
if (be_daemon) {
	if (!logfile) {
		puts("ERROR: You must specify a log file if the server is to be run as a background daemon.");
		exit(1);
		}
	// Fork here before we spawn threads since POSIX threads are not
	// duplicated in a fork call , plus it would get messy anyway.
	switch(fork()) {
		case -1:
		perror("ERROR: fork()");  exit(1);

		case 0:  break;

		default: exit(0);
		}
	}

main_pid = getpid();
return;

USAGE:
printf("Usage: %s [-h] [-V] [-c <config file>] [-l logfile [-d]] [-s]\n",
	argv[0]);
exit(1);
}




/*** Initialise (almost) everything ***/
void init()
{
sigset_t set;
int i,ret;
char *v;

booting = 1;

// 'v' is a hack since gcc bombs out with internal error if we use the macro.
// Solaris compilers don't care though. Oh well.
v=VERSION;
svr_version = (v[0]-'0')*100 + (v[2]-'0')*10 + (v[4]-'0');

// Set locale to default unix behaviour just in case this is 
// being run in some johnny foreigner place ;)
setlocale(LC_ALL, "C");

// Initialise globals
bzero((char *)&main_bind_addr,sizeof(main_bind_addr));
main_bind_addr.sin_family = AF_INET;
main_bind_addr.sin_addr.s_addr = INADDR_ANY;

if (!(original_dir = getcwd(NULL,256))) {
	perror("ERROR: getcwd()");  exit(1);
	}
working_dir = WORKING_DIR;
system_flags = DEFAULT_SYSTEM_FLAGS;
first_user = NULL;
last_user = NULL;
first_user_ban = NULL;
last_user_ban = NULL;
first_site_ban = NULL;
last_site_ban = NULL;
first_group = NULL;
last_group = NULL;
first_server = NULL;
last_server = NULL;
first_thread = NULL;
last_thread = NULL;
boot_time = server_time = time(0);
localtime_r(&server_time,&server_time_tms);
server_count = 0;
connected_server_count = 0;
shutdown_time = 0;
shutdown_type = SHUTDOWN_INACTIVE;
ansi_terms = NULL;
tcp_port[PORT_TYPE_USER] = USER_PORT;
tcp_port[PORT_TYPE_SERVER] = SERVER_PORT;
listen_sock[PORT_TYPE_USER] = -1;
listen_sock[PORT_TYPE_SERVER] = -1;
local_connect_key = 0;
server_name[0] = '\0';
srandom((u_int)time(0));
thread_count = 0;
max_local_users = MAX_LOCAL_USERS;
max_remote_users = MAX_REMOTE_USERS;
max_user_ign_level = MAX_USER_IGN_LEVEL;
local_user_count = 0;
remote_user_count = 0;
max_name_len = MAX_NAME_LEN;
max_desc_len = MAX_DESC_LEN;
min_pwd_len = MIN_PWD_LEN;
send_ping_interval = PING_INTERVAL;
server_timeout = SERVER_TIMEOUT;
connect_timeout = CONNECT_TIMEOUT;
linkdead_timeout = LINKDEAD_TIMEOUT;
login_timeout = LOGIN_TIMEOUT;
idle_timeout = IDLE_TIMEOUT;
idle_timeout_ign_level = IDLE_TIMEOUT_IGN_LEVEL;
max_hop_cnt = MAX_HOP_CNT;
max_tx_errors = MAX_TX_ERR_CNT;
max_rx_errors = MAX_RX_ERR_CNT;
max_packet_rate = MAX_PACKET_RATE;
max_local_user_data_rate = MAX_LOCAL_USER_DATA_RATE;
max_svr_data_rate = MAX_SVR_DATA_RATE;
max_profile_chars = MAX_PROFILE_CHARS;
max_mail_chars = MAX_MAIL_CHARS;
max_board_chars = MAX_BOARD_CHARS;
max_group_desc_chars = MAX_GROUP_DESC_CHARS;
max_broadcast_chars = MAX_BROADCAST_CHARS;
max_subject_len = MAX_SUBJECT_LEN;
max_group_name_len = MAX_GROUP_NAME_LEN;
max_batch_name_len = MAX_BATCH_NAME_LEN;
max_login_batch_lines = MAX_LOGIN_BATCH_LINES;
max_logout_batch_lines = MAX_LOGOUT_BATCH_LINES;
max_session_batch_lines = MAX_SESSION_BATCH_LINES;
max_include_lines = MAX_INCLUDE_LINES;
default_desc = strdup(DEFAULT_DESC);
default_pwd = strdup(DEFAULT_PWD);
lockout_level = LOCKOUT_LEVEL;
group_modify_level = GROUP_MODIFY_LEVEL;
group_gatecrash_level = GROUP_GATECRASH_LEVEL;
group_invite_expire = GROUP_INVITE_EXPIRE;
remote_user_max_level = REMOTE_USER_MAX_LEVEL;
num_review_lines = NUM_REVIEW_LINES;
board_msg_expire = BOARD_MSG_EXPIRE;
go_invis_level = GO_INVIS_LEVEL;
autosave_interval = AUTOSAVE_INTERVAL;
autosave_time = server_time;
recv_net_bcast_level = RECV_NET_BCAST_LEVEL;
incoming_encryption_policy = ENCRYPT_EITHER;
soft_max_servers = SOFT_MAX_SERVERS;

/* Parse data files and create system groups. System groups must be created
   after config file is parses since they require the working directory path
   to load their data files just like public & user groups */
parse_config_file();
load_bans();
create_system_groups();
load_public_groups();

// Init sockets 
log(0,"\nSetting up TCP ports...");

log(0,"   Initialising user port %d...\n",tcp_port[PORT_TYPE_USER]);
if ((ret = create_listen_socket(PORT_TYPE_USER)) != OK) {
	printf("ERROR: Unable to open port: %s: %s\n",
		err_string[ret],strerror(errno));
	exit(1);
	}
if (SYS_FLAGISSET(SYS_FLAG_SVR_LISTEN)) {
	log(0,"   Initialising server port %d...\n",tcp_port[PORT_TYPE_SERVER]);
	if ((ret = create_listen_socket(PORT_TYPE_SERVER)) != OK) {
		printf("ERROR: Unable to open port: %s: %s\n",
			err_string[ret],strerror(errno));
		exit(1);
		}
	}
else log(0,"   Server port disabled.\n");
log(0,"Done.\n");

// Set up mutexes
pthread_mutex_init(&log_mutex,NULL);
pthread_mutex_init(&events_mutex,NULL);
pthread_mutex_init(&threadlist_mutex,NULL);

/* Set up signal handlers. Always ignore sigpipes since we can get these from
   duff write()'s and we don't want the server crashing out just because of
   that. Ignore SIGWINCH since its irrelevant to us but could cause problems
   if not done. */
signal(SIGPIPE,SIG_IGN);
signal(SIGWINCH,SIG_IGN);

sigact.sa_handler = NULL;
sigact.sa_sigaction = signal_handler;
sigemptyset(&set);
sigact.sa_mask = set;
sigact.sa_flags = SA_SIGINFO; // So we use sa_sigaction func, not handler func.

for(i=0;i < NUM_SIGNALS;++i) sigaction(siglist[i].num,&sigact,NULL);
}




/*** Go through the server config file and set the params accordingly ***/
void parse_config_file()
{
cl_splitline sl(1);
FILE *fp;
int i,ret,len,linenum,err;
char line[ARR_SIZE];
char path[MAXPATHLEN];
char *config_option[]={ 
	"server name",
	"working dir",
	"user port",
	"server port",
	"server port listening",
	"connect key",
	"ansi terminals",
	"max name length",
	"max desc length",
	"min pwd length",
	"ping interval",
	"server timeout",
	"connect timeout",
	"server",
	"linkdead timeout",
	"login timeout",
	"idle timeout",
	"idle timeout ignore level",
	"max local users",
	"max remote users",
	"max user ignore level",
	"max hop",
	"max tx errors",
	"max rx errors",
	"max profile chars",
	"max mail chars",
	"max board chars",
	"max group desc chars",
	"max group name length",
	"max include lines",
	"max broadcast chars",
	"allow loopback",
	"random remote ids",
	"default desc",
	"default pwd",
	"group modify level",
	"group gatecrash level",
	"group invite expire",
	"lockout level",
	"remote user max level",
	"review lines",
	"board msg expire",
	"board renumber",
	"max subject length",
	"signal ignore",
	"max login batch lines",
	"max logout batch lines",
	"max session batch lines",
	"max batch name length",
	"go invis level",
	"prisoners ret home",
	"bind interface",
	"autosave interval",
	"log net broadcasts",
	"recv net bcast level",
	"hexdump packets",
	"strip printcodes",
	"allow who at login",
	"allow remote batch runs",
	"allow new accounts",
	"delete disconnected incoming",
	"log unexpected packets",
	"max packet rate",
	"incoming encryption policy",
	"outgoing encryption enforce",
	"soft max servers",
	"resolve ip name internally",
	"max local user data rate",
	"max server data rate",
	"save novice accounts",
	"really delete accounts",
	"log groups"
	};

// Not really need but makes the code easier to read
enum {
	OPT_SERVER_NAME,
	OPT_WORKING_DIR,
	OPT_USER_PORT,
	OPT_SERVER_PORT,
	OPT_SERVER_PORT_LISTENING,
	OPT_CONNECT_KEY,
	OPT_ANSI_TERMS,
	OPT_MAX_NAME_LEN,
	OPT_MAX_DESC_LEN,
	OPT_MIN_PWD_LEN,
	OPT_PING_INTERVAL,
	OPT_SERVER_TIMEOUT,
	OPT_CONNECT_TIMEOUT,
	OPT_REMOTE_SERVER,
	OPT_LINKDEAD_TIMEOUT,
	OPT_LOGIN_TIMEOUT,
	OPT_IDLE_TIMEOUT,
	OPT_IDLE_TIMEOUT_IGN_LEVEL,
	OPT_MAX_LOCAL_USERS,
	OPT_MAX_REMOTE_USERS,
	OPT_MAX_USER_IGN_LEVEL,
	OPT_MAX_HOP,
	OPT_MAX_TX_ERRS,
	OPT_MAX_RX_ERRS,
	OPT_MAX_PROFILE_CHARS,
	OPT_MAX_MAIL_CHARS,
	OPT_MAX_BOARD_CHARS,
	OPT_MAX_GROUP_DESC_CHARS,
	OPT_MAX_GROUP_NAME_LEN,
	OPT_MAX_INCLUDE_LINES,
	OPT_MAX_BROADCAST_CHARS,
	OPT_ALLOW_LB_USERS,
	OPT_ALLOW_RR_ID,
	OPT_DEFAULT_DESC,
	OPT_DEFAULT_PWD,
	OPT_GROUP_MODIFY_LEVEL,
	OPT_GATECRASH_LEVEL,
	OPT_INVITE_EXPIRE,
	OPT_LOCKOUT_LEVEL,
	OPT_REMOTE_USER_MAX_LEVEL,
	OPT_REVIEW_LINES,
	OPT_BOARD_MSG_EXPIRE,
	OPT_BOARD_RENUMBER,
	OPT_MAX_SUBJECT_LEN,
	OPT_SIGNAL_IGNORE,
	OPT_MAX_LOGIN_LINES,
	OPT_MAX_LOGOUT_LINES,
	OPT_MAX_SESSION_LINES,
	OPT_MAX_BATCH_NAME_LEN,
	OPT_GO_INVIS_LEVEL,
	OPT_PRISONERS_RET_HOME,
	OPT_BIND_INTERFACE,
	OPT_AUTOSAVE_INTERVAL,
	OPT_LOG_NET_BROADCASTS,
	OPT_RECV_NET_BCAST_LEVEL,
	OPT_HEXDUMP_PACKETS,
	OPT_STRIP_PRINTCODES,
	OPT_ALLOW_WHO_AT_LOGIN,
	OPT_ALLOW_REM_BATCH_RUNS,
	OPT_ALLOW_NEW_ACCOUNTS,
	OPT_DEL_DISC_INCOMING,
	OPT_LOG_UX_PACKETS,
	OPT_MAX_PACKET_RATE,
	OPT_INC_ENCRYPT_POL,
	OPT_OUT_ENFORCE_ENCRYPTION,
	OPT_SOFT_MAX_SERVERS,
	OPT_INTERNAL_RESOLVE,
	OPT_MAX_USER_DATA_RATE,
	OPT_MAX_SERVER_DATA_RATE,
	OPT_SAVE_NOVICE_ACCOUNTS,
	OPT_REALLY_DELETE_ACCOUNTS,
	OPT_LOG_GROUPS,

	OPT_END
	};

log(0,"Loading configuration data from file \"%s\"...\n",config_file);

if (!(fp=fopen(config_file,"r"))) {
	log(0,"ERROR: Cannot open config file '%s': %s\n",
		config_file,strerror(errno));
	exit(1);
	}

err = 0;
linenum=1;
fgets(line,ARR_SIZE-1,fp);

while(!feof(fp) && !(err = ferror(fp))) {
	if ((ret=sl.parse(line)) != OK) {
		log(0,"ERROR: %s on line %d.\n",err_string[ret],linenum);
		exit(1);
		}
	if (sl.wcnt) {
		for(i=0;i < OPT_END;++i)
			if (!strcmp(sl.word[0],config_option[i])) break;

		switch(i) {
			case OPT_SERVER_NAME:
			if (sl.wcnt > 2) goto EXTRA;
			if (!(len=strlen(sl.word[1]))) {
				log(0,"ERROR: Server name is null on line %d.\n",linenum);
				exit(1);
				}
			if (len > MAX_SERVER_NAME_LEN) {
				log(0,"ERROR: Server name is too long on line %d.\n",linenum);
				exit(1);
				}
			// Server name can only be one word
			if (has_whitespace(sl.word[1])) {
				log(0,"ERROR: Server name cannot contain whitespace on line %d.\n",linenum);
				exit(1);
				}
			strcpy(server_name,sl.word[1]);
			break;


			case OPT_WORKING_DIR:
			if (sl.wcnt > 2) goto EXTRA;
			working_dir = strdup(sl.word[1]);
			len = strlen(working_dir)-1;
			if (working_dir[len] == '/') working_dir[len] = '\0';
			break;


			case OPT_USER_PORT:
			if (sl.wcnt > 2) goto EXTRA;
			if (!is_integer(sl.word[1]) || 
			    (tcp_port[PORT_TYPE_USER]=atoi(sl.word[1])) < 1 || tcp_port[PORT_TYPE_USER] > 65535) {
				log(0,"ERROR: Invalid user port number '%s' on line %d. Range is 1 to 65535.\n",sl.word[1],linenum);
				exit(1);
				}
			break;


			case OPT_SERVER_PORT:
			if (sl.wcnt > 2) goto EXTRA;
			if (!is_integer(sl.word[1]) || 
			    (tcp_port[PORT_TYPE_SERVER]=atoi(sl.word[1])) < 1 || tcp_port[PORT_TYPE_SERVER] > 65535) {
				log(0,"ERROR: Invalid server port number '%s' on line %d. Range is 1 to 65535.\n",sl.word[1],linenum);
				exit(1);
				}
			break;


			case OPT_SERVER_PORT_LISTENING:
			if (parse_config_yn(&sl,linenum))
				SYS_SETFLAG(SYS_FLAG_SVR_LISTEN);
			else 
				SYS_UNSETFLAG(SYS_FLAG_SVR_LISTEN);
			break;


			case OPT_CONNECT_KEY:
			local_connect_key = parse_config_integer(&sl,linenum);
			break;


			case OPT_ANSI_TERMS:
			if (ansi_terms) 
				log(0,"WARNING: Ansi terminals redefined on line %d.\n",linenum);
			else ansi_terms=new cl_splitline(0);

			if ((ret=ansi_terms->set(&sl)) != OK) {
				log(0,"ERROR: parse_config_file() -> cl_splitline::set(): %s\n",err_string[ret]);
				exit(1);
				}
			break;


			case OPT_MAX_NAME_LEN:
			if (sl.wcnt > 2) goto EXTRA;

			// 3 seems a realistic min value
			if (!is_integer(sl.word[1]) ||
			    (max_name_len=atoi(sl.word[1])) < 3 || 
			    max_name_len > 255) {
				log(0,"ERROR: Invalid name length '%s' on line %d. Range is 3 to 255 characters.\n",sl.word[1],linenum);
				exit(1);
				}
			break;


			case OPT_MAX_DESC_LEN:
			if (sl.wcnt > 2) goto EXTRA;

			if (!is_integer(sl.word[1]) ||
			    (max_desc_len=atoi(sl.word[1])) < 0 ||
			    max_desc_len > 255) {
				log(0,"ERROR: Invalid description length '%s' on line %d. Range is 0 to 255 characters.\n",sl.word[1],linenum);
				exit(1);
				}
			break;


			case OPT_MIN_PWD_LEN:
			if (sl.wcnt > 2) goto EXTRA;
			if (!is_integer(sl.word[1]) ||
			    (min_pwd_len=atoi(sl.word[1])) < 3) {
				log(0,"ERROR: Invalid length '%s' on line %d.\n",sl.word[1],linenum);
				exit(1);
				}
			break;


			case OPT_PING_INTERVAL:
			if (!(send_ping_interval = parse_config_time(&sl,linenum,1))) {
				log(0,"ERROR: Ping interval must be greater than zero.");
				exit(1);
				}
			break;


			case OPT_SERVER_TIMEOUT:
			server_timeout = parse_config_time(&sl,linenum,1);
			break;


			case OPT_CONNECT_TIMEOUT:
			connect_timeout = parse_config_time(&sl,linenum,1);
			break;


			case OPT_REMOTE_SERVER:
			if (sl.wcnt > 7) goto EXTRA;
			parse_remote_server_line(&sl,linenum);
			break;


			case OPT_LINKDEAD_TIMEOUT:
			linkdead_timeout = parse_config_time(&sl,linenum,1);
			break;


			case OPT_LOGIN_TIMEOUT:
			login_timeout = parse_config_time(&sl,linenum,0);
			break;
			

			case OPT_IDLE_TIMEOUT:
			idle_timeout = parse_config_time(&sl,linenum,0);
			break;
			

			case OPT_IDLE_TIMEOUT_IGN_LEVEL:
			idle_timeout_ign_level = parse_config_level(&sl,linenum);
			break;


			case OPT_MAX_LOCAL_USERS:
			max_local_users = parse_config_integer(&sl,linenum);
			break;


			case OPT_MAX_REMOTE_USERS:
			max_remote_users = parse_config_integer(&sl,linenum);
			break;


			case OPT_MAX_USER_IGN_LEVEL:
			max_user_ign_level = parse_config_level(&sl,linenum);
			break;


			case OPT_MAX_HOP:
			if (sl.wcnt > 2) goto EXTRA;
			/* Max is 254 since if it was 255 then when 1 is added
			   to it it rolls back to zero since in packet its an
			   unsigned char */
			if (!is_integer(sl.word[1]) || 
			    (max_hop_cnt=atoi(sl.word[1])) < 1 || max_hop_cnt > 254) {
				log(0,"ERROR: Invalid max hop count '%s' on line %d. Range is 1 to 254.\n",sl.word[1],linenum);
				exit(1);
				}
			break;


			case OPT_MAX_TX_ERRS:
			max_tx_errors = parse_config_integer(&sl,linenum);
			break;


			case OPT_MAX_RX_ERRS:
			max_rx_errors = parse_config_integer(&sl,linenum);
			break;


			case OPT_MAX_PROFILE_CHARS:
			max_profile_chars = parse_config_integer(&sl,linenum);
			break;


			case OPT_MAX_MAIL_CHARS:
			max_mail_chars = parse_config_integer(&sl,linenum);
			break;


			case OPT_MAX_BOARD_CHARS:
			max_board_chars = parse_config_integer(&sl,linenum);
			break;


			case OPT_MAX_GROUP_DESC_CHARS:
			max_group_desc_chars = parse_config_integer(&sl,linenum);
			break;


			case OPT_MAX_GROUP_NAME_LEN:
			max_group_name_len = parse_config_integer(&sl,linenum);
			break;


			case OPT_ALLOW_LB_USERS:
			if (parse_config_yn(&sl,linenum))
				SYS_SETFLAG(SYS_FLAG_LOOPBACK_USERS);
			else 
				SYS_UNSETFLAG(SYS_FLAG_LOOPBACK_USERS);
			break;


			case OPT_ALLOW_RR_ID:
			if (parse_config_yn(&sl,linenum))
				SYS_SETFLAG(SYS_FLAG_RANDOM_REM_IDS);
			else
				SYS_UNSETFLAG(SYS_FLAG_RANDOM_REM_IDS);
			break;


			case OPT_DEFAULT_DESC:
			if (sl.wcnt > 2) goto EXTRA;
			free(default_desc);
			default_desc = strdup(sl.word[1]);
			break;


			case OPT_DEFAULT_PWD:
			if (sl.wcnt > 2) goto EXTRA;
			free(default_pwd);
			default_pwd = strdup(sl.word[1]);
			break;


			case OPT_GROUP_MODIFY_LEVEL:
			group_modify_level = parse_config_level(&sl,linenum);
			break;


			case OPT_GATECRASH_LEVEL:
			group_gatecrash_level = parse_config_level(&sl,linenum);
			break;


			case OPT_INVITE_EXPIRE:
			group_invite_expire = parse_config_time(&sl,linenum,0);
			break;


			case OPT_LOCKOUT_LEVEL:
			lockout_level = parse_config_level(&sl,linenum);
			break;


			case OPT_REMOTE_USER_MAX_LEVEL:
			remote_user_max_level = parse_config_level(&sl,linenum);
			break;


			case OPT_REVIEW_LINES:
			num_review_lines = parse_config_integer(&sl,linenum);
			break;


			case OPT_BOARD_MSG_EXPIRE:
			board_msg_expire = parse_config_time(&sl,linenum,0);
			break;


			case OPT_BOARD_RENUMBER:
			if (parse_config_yn(&sl,linenum))
				SYS_SETFLAG(SYS_FLAG_BOARD_RENUM);
			else
				SYS_UNSETFLAG(SYS_FLAG_BOARD_RENUM);
			break;


			case OPT_MAX_SUBJECT_LEN:
			if (sl.wcnt > 2) goto EXTRA;

			if (!is_integer(sl.word[1]) ||
			    (max_subject_len=atoi(sl.word[1])) < 1 ||
			    max_subject_len > 255) {
				log(0,"ERROR: Invalid subject length '%s' on line %d. Range is 1 to 255.\n",sl.word[1],linenum);
				exit(1);
				}
			break;


			case OPT_SIGNAL_IGNORE:
			parse_signal_ignore(&sl,linenum);
			break;


			case OPT_MAX_LOGIN_LINES:
			max_login_batch_lines = parse_config_integer(&sl,linenum);
			break;


			case OPT_MAX_LOGOUT_LINES:
			max_logout_batch_lines = parse_config_integer(&sl,linenum);
			break;


			case OPT_MAX_SESSION_LINES:
			max_session_batch_lines = parse_config_integer(&sl,linenum);
			break;


			case OPT_MAX_BATCH_NAME_LEN:
			max_batch_name_len = parse_config_integer(&sl,linenum);
			break;


			case OPT_MAX_INCLUDE_LINES:
			max_include_lines = parse_config_integer(&sl,linenum);
			if (max_include_lines < 1) {
				log(0,"ERROR: Invalid value '%s' on line %d. It must be greater than zero.\n",sl.word[1],linenum);
				exit(1);
				}
			break;


			case OPT_MAX_BROADCAST_CHARS:
			max_broadcast_chars = parse_config_integer(&sl,linenum);
			break;


			case OPT_GO_INVIS_LEVEL:
			go_invis_level = parse_config_level(&sl,linenum);
			break;


			case OPT_PRISONERS_RET_HOME:
			if (parse_config_yn(&sl,linenum))
				SYS_SETFLAG(SYS_FLAG_PRS_RET_HOME);
			else
				SYS_UNSETFLAG(SYS_FLAG_PRS_RET_HOME);
			break;


			case OPT_BIND_INTERFACE:
			if (sl.wcnt > 2) goto EXTRA;
			if (!strcasecmp(sl.word[1],"ALL")) break;
			if ((int)(main_bind_addr.sin_addr.s_addr = inet_addr(sl.word[1])) == -1) {
				log(0,"ERROR: Invalid interface address '%s' on line %d.\n",sl.word[1],linenum);
				exit(1);
				}
			break;


			case OPT_AUTOSAVE_INTERVAL:
			autosave_interval = parse_config_time(&sl,linenum,1);
			break;


			case OPT_LOG_NET_BROADCASTS:
			if (parse_config_yn(&sl,linenum))
				SYS_SETFLAG(SYS_FLAG_LOG_NETBCAST);
			else 
				SYS_UNSETFLAG(SYS_FLAG_LOG_NETBCAST);
			break;


			case OPT_RECV_NET_BCAST_LEVEL:
			recv_net_bcast_level = parse_config_level(&sl,linenum);
			break;


			case OPT_HEXDUMP_PACKETS:
			if (parse_config_yn(&sl,linenum))
				SYS_SETFLAG(SYS_FLAG_HEXDUMP_PACKETS);
			else
				SYS_UNSETFLAG(SYS_FLAG_HEXDUMP_PACKETS);
			break;


			case OPT_STRIP_PRINTCODES:
			if (parse_config_yn(&sl,linenum))
				SYS_SETFLAG(SYS_FLAG_STRIP_PRINTCODES);
			else
				SYS_UNSETFLAG(SYS_FLAG_STRIP_PRINTCODES);
			break;


			case OPT_ALLOW_WHO_AT_LOGIN:
			if (parse_config_yn(&sl,linenum))
				SYS_SETFLAG(SYS_FLAG_ALLOW_WHO_AT_LOGIN);
			else
				SYS_UNSETFLAG(SYS_FLAG_ALLOW_WHO_AT_LOGIN);
			break;


			case OPT_ALLOW_REM_BATCH_RUNS:
			if (parse_config_yn(&sl,linenum))
				SYS_SETFLAG(SYS_FLAG_ALLOW_REM_BATCH_RUNS);
			else
				SYS_UNSETFLAG(SYS_FLAG_ALLOW_REM_BATCH_RUNS);
			break;


			case OPT_ALLOW_NEW_ACCOUNTS:
			if (parse_config_yn(&sl,linenum))
				SYS_SETFLAG(SYS_FLAG_ALLOW_NEW_ACCOUNTS);
			else
				SYS_UNSETFLAG(SYS_FLAG_ALLOW_NEW_ACCOUNTS);
			break;


			case OPT_DEL_DISC_INCOMING:
			if (parse_config_yn(&sl,linenum))
				SYS_SETFLAG(SYS_FLAG_DEL_DISC_INCOMING);
			else
				SYS_UNSETFLAG(SYS_FLAG_DEL_DISC_INCOMING);
			break;


			case OPT_LOG_UX_PACKETS:
			if (parse_config_yn(&sl,linenum))
				SYS_SETFLAG(SYS_FLAG_LOG_UNEXPECTED_PKTS);
			else
				SYS_UNSETFLAG(SYS_FLAG_LOG_UNEXPECTED_PKTS);
			break;


			case OPT_MAX_PACKET_RATE:
			max_packet_rate = parse_config_integer(&sl,linenum);
			break;


			case OPT_INC_ENCRYPT_POL:
			if (sl.wcnt > 2) goto EXTRA;
			if (!strcasecmp(sl.word[1],"NEVER"))
				incoming_encryption_policy = ENCRYPT_NEVER;
			else
			if (!strcasecmp(sl.word[1],"ALWAYS"))
				incoming_encryption_policy = ENCRYPT_ALWAYS;
			else
			if (!strcasecmp(sl.word[1],"EITHER"))
				incoming_encryption_policy = ENCRYPT_EITHER;
			else {
				log(0,"ERROR: Unknown incoming encryption policy '%s' on line %d.\n",sl.word[1],linenum);
				exit(1);
				}
			break;
			

			case OPT_OUT_ENFORCE_ENCRYPTION:
			if (parse_config_yn(&sl,linenum))
				SYS_SETFLAG(SYS_FLAG_OUT_ENF_ENCRYPTION);
			else 
				SYS_UNSETFLAG(SYS_FLAG_OUT_ENF_ENCRYPTION);
			break;


			case OPT_SOFT_MAX_SERVERS:
			// Hard max is 15
			if ((soft_max_servers = parse_config_integer(&sl,linenum)) > 15) {
				log(0,"ERROR: Soft max server limit greater than hard limit of 15 on line %d.\n",linenum);
				exit(1);
				}
			break;

			
			case OPT_INTERNAL_RESOLVE:
			if (parse_config_yn(&sl,linenum))
				SYS_SETFLAG(SYS_FLAG_INTERNAL_RESOLVE);
			else
				SYS_UNSETFLAG(SYS_FLAG_INTERNAL_RESOLVE);
			break;


			case OPT_MAX_USER_DATA_RATE:
			max_local_user_data_rate =  parse_config_integer(&sl,linenum);
			break;


			case OPT_MAX_SERVER_DATA_RATE:
			max_svr_data_rate =  parse_config_integer(&sl,linenum);
			break;


			case OPT_SAVE_NOVICE_ACCOUNTS:
			if (parse_config_yn(&sl,linenum))
				SYS_SETFLAG(SYS_FLAG_SAVE_NOVICE_ACCOUNTS);
			else
				SYS_UNSETFLAG(SYS_FLAG_SAVE_NOVICE_ACCOUNTS);
			break;


			case OPT_REALLY_DELETE_ACCOUNTS:
			if (parse_config_yn(&sl,linenum))
				SYS_SETFLAG(SYS_FLAG_REALLY_DEL_ACCOUNTS);
			else
				SYS_UNSETFLAG(SYS_FLAG_REALLY_DEL_ACCOUNTS);
			break;
			
			case OPT_LOG_GROUPS:
			if (parse_config_yn(&sl,linenum))
				SYS_SETFLAG(SYS_FLAG_LOG_GROUPS);
			else
				SYS_UNSETFLAG(SYS_FLAG_LOG_GROUPS);
			break;


			default:
			log(0,"ERROR: Unknown option '%s' on line %d.\n",sl.word[0],linenum);
			exit(1);
			}
		}

	fgets(line,ARR_SIZE-1,fp);
	linenum++;
	}
fclose(fp);

if (err) {
	log(0,"ERROR: Read failure of configuration file: %s\n",strerror(err));
	exit(1);
	}
if (!server_name[0]) {
	log(0,"ERROR: Server name not specified.");  exit(1);
	}
if (server_timeout < send_ping_interval) {
	puts("ERROR: Server timeout is less than server ping time.");
	exit(1);
	}

if ((int)strlen(default_desc) > max_desc_len) {
	log(0,"ERROR: Max description length is less than default description string length.\n");
	exit(1);
	}

// Print information if not suppressed
if (!suppress_config_info) {
	log(0,"   Server name            : %s\n",server_name);
	log(0,"   Original directory     : %s/\n",original_dir);
	log(0,"   Working directory      : %s/\n",working_dir);
	if (logfile) log(0,"   Log file               : \"%s\"\n",logfile);
	else log(0,"   Log file               : <stdout>");

	log(0,"   Bind interface         : %s\n",
		main_bind_addr.sin_addr.s_addr == INADDR_ANY ?
		"<ALL>" : inet_ntoa(main_bind_addr.sin_addr));

	log(0,"   User port              : %d\n",tcp_port[PORT_TYPE_USER]);
	log(0,"   Server port            : %d\n",tcp_port[PORT_TYPE_SERVER]);
	log(0,"   Server port listening  : %s\n",
		noyes[SYS_FLAGISSET(SYS_FLAG_SVR_LISTEN)]);
	log(0,"   Connect key            : %u\n",local_connect_key);
	if (soft_max_servers)
		log(0,"   Soft max servers       : %d\n",soft_max_servers);
	else
		log(0,"   Soft max servers       : <unlimited>\n");

	if (ansi_terms) {
		strcpy(text,"   Ansi terminals         : ");
		for(i=1;i < ansi_terms->wcnt;++i) 
			sprintf(text,"%s%s,",text,ansi_terms->word[i]);
		if (i > 1) text[strlen(text) - 1] ='\0';
		log(0,"%s\b",text);
		}
	else log(0,"   Ansi terminals         : <none>\n");

	// Ignoring SIGPIPE & SIGWINCH is hardcoded in
	strcpy(text,"   Ignoring signals       : PIPE,WINCH");
	for(i=0;i < NUM_SIGNALS;++i) {
		if (siglist[i].ignoring)
			sprintf(text,"%s,%s",text,siglist[i].name);
		}
	log(0,text);

	log(0,"   Max name length        : %d chars\n",max_name_len);
	log(0,"   Max desc length        : %d chars\n",max_desc_len);
	log(0,"   Max subject length     : %d chars\n",max_subject_len);
	if (max_local_users)
		log(0,"   Max local users        : %d",max_local_users);
	else
		log(0,"   Max local users        : <unlimited>");
	log(0,"   Max remote users       : %d\n",max_remote_users);
	log(0,"   Max user ignore level  : %s\n",user_level[max_user_ign_level]);
	log(0,"   Default description    : \"%s\"\n",default_desc);
	log(0,"   Default password       : \"%s\"\n",default_pwd);
	log(0,"   Min pwd length         : %d chars\n",min_pwd_len);
	log(0,"   Ping interval          : %s\n",time_period(send_ping_interval));
	log(0,"   Autosave interval      : %s\n",time_period(autosave_interval));
	log(0,"   Server timeout         : %s\n",time_period(server_timeout));
	log(0,"   Connect timeout        : %s\n",time_period(connect_timeout));
	log(0,"   Login timeout          : %s\n",
		login_timeout ? time_period(login_timeout) : "<unlimited>");
	log(0,"   Idle timeout           : %s\n",
		idle_timeout ? time_period(idle_timeout) : "<unlimited>");
	log(0,"   Idle timeout ign level : %s\n",
		user_level[idle_timeout_ign_level]);
	log(0,"   Linkdead timeout       : %s\n",time_period(linkdead_timeout));
	log(0,"   Board message expire   : %s\n",
		board_msg_expire ? time_period(board_msg_expire) : "<unlimited>");
	log(0,"   Board renumber         : %s\n",
		noyes[SYS_FLAGISSET(SYS_FLAG_BOARD_RENUM)]);
	log(0,"   Max hop count          : %d\n",max_hop_cnt);
	log(0,"   Max TX errors          : %d\n",max_tx_errors);
	log(0,"   Max RX errors          : %d\n",max_rx_errors);
	if (max_packet_rate)
		log(0,"   Max rx packet rate     : %d per second\n",max_packet_rate);
	else
		log(0,"   Max rx packet rate     : <unlimited>\n");
	if (max_svr_data_rate)
		log(0,"   Max server rx data rate: %d bytes/sec\n",max_svr_data_rate);
	else
		log(0,"   Max server rx data rate: <unlimited>\n");
	if (max_local_user_data_rate)
		log(0,"   Max l. u. rx data rate : %d bytes/sec\n",max_local_user_data_rate);
	else
		log(0,"   Max l. u. rx data rate : <unlimited>\n");
	if (max_profile_chars)
		log(0,"   Max profile chars      : %d\n",max_profile_chars);
	else
		log(0,"   Max profile chars      : <unlimited>");
	if (max_mail_chars)
		log(0,"   Max mail characters    : %d\n",max_mail_chars);
	else
		log(0,"   Max mail characters    : <unlimited>");
	if (max_board_chars) 
		log(0,"   Max board characters   : %d\n",max_board_chars);
	else
		log(0,"   Max board characters   : <unlimited>");
	if (max_broadcast_chars)
		log(0,"   Max broadcast chars    : %d\n",max_broadcast_chars);
	else
		log(0,"   Max broadcast chars    : <unlimited>");
	log(0,"   Max include lines      : %d\n",max_include_lines);
	log(0,"   Max login batch lines  : %d\n",max_login_batch_lines);
	log(0,"   Max logout batch lines : %d\n",max_logout_batch_lines);
	log(0,"   Max session batch lines: %d\n",max_session_batch_lines);
	log(0,"   Max batch name length  : %d\n",max_batch_name_len);
	log(0,"   Num. of review lines   : %d\n",num_review_lines); 
	log(0,"   Loopback users         : %s\n",
		noyes[SYS_FLAGISSET(SYS_FLAG_LOOPBACK_USERS)]);
	log(0,"   Random remote ids      : %s\n",
		noyes[SYS_FLAGISSET(SYS_FLAG_RANDOM_REM_IDS)]);
	log(0,"   Max group name length  : %d\n",max_group_name_len);
	if (max_group_desc_chars) 
		log(0,"   Max group desc chars   : %d\n",max_group_desc_chars);
	else
		log(0,"   Max group desc chars   : <unlimited>");
	log(0,"   Group modify level     : %s\n",user_level[group_modify_level]);
	log(0,"   Group gatecrash level  : %s\n",user_level[group_modify_level]);
	log(0,"   Group invite expire    : %s\n",
		group_invite_expire ?
		time_period(group_invite_expire) : "<unlimited>");
	log(0,"   Log groups             : %s\n",
		noyes[SYS_FLAGISSET(SYS_FLAG_LOG_GROUPS)]);
	log(0,"   Lockout level          : %s\n",user_level[lockout_level]);
	log(0,"   Allow new accounts     : %s\n",
		noyes[SYS_FLAGISSET(SYS_FLAG_ALLOW_NEW_ACCOUNTS)]);
	log(0,"   Save novice accounts   : %s\n",
		noyes[SYS_FLAGISSET(SYS_FLAG_SAVE_NOVICE_ACCOUNTS)]);
	log(0,"   Really delete accounts : %s\n",
		noyes[SYS_FLAGISSET(SYS_FLAG_REALLY_DEL_ACCOUNTS)]);
	log(0,"   Remote user max level  : %s\n",user_level[remote_user_max_level]);
	log(0,"   Go invisible level     : %s\n",user_level[go_invis_level]);
	log(0,"   Prisoners returned home: %s\n",
		noyes[SYS_FLAGISSET(SYS_FLAG_PRS_RET_HOME)]);
	log(0,"   Log net broadcasts     : %s\n",
		noyes[SYS_FLAGISSET(SYS_FLAG_LOG_NETBCAST)]);
	log(0,"   Receive net bcast level: %s\n",user_level[recv_net_bcast_level]);
	log(0,"   Hexdump packets        : %s\n",
		noyes[SYS_FLAGISSET(SYS_FLAG_HEXDUMP_PACKETS)]);
	log(0,"   Strip rsrvd printcodes : %s\n",
		noyes[SYS_FLAGISSET(SYS_FLAG_STRIP_PRINTCODES)]);
	log(0,"   Allow 'who' at login   : %s\n",
		noyes[SYS_FLAGISSET(SYS_FLAG_ALLOW_WHO_AT_LOGIN)]);
	log(0,"   Allow remote batch runs: %s\n",
		noyes[SYS_FLAGISSET(SYS_FLAG_ALLOW_REM_BATCH_RUNS)]);
	log(0,"   Delete discon. incoming: %s\n",
		noyes[SYS_FLAGISSET(SYS_FLAG_DEL_DISC_INCOMING)]);
	log(0,"   Log unexpected packets : %s\n",
		noyes[SYS_FLAGISSET(SYS_FLAG_LOG_UNEXPECTED_PKTS)]);
	log(0,"   Incoming encrpyt policy: %s\n",
		inc_encrypt_polstr[incoming_encryption_policy]);
	log(0,"   Outgoing encryption enf: %s\n",
		noyes[SYS_FLAGISSET(SYS_FLAG_OUT_ENF_ENCRYPTION)]);
	log(0,"   Resolve IP internally  : %s\n",
		noyes[SYS_FLAGISSET(SYS_FLAG_INTERNAL_RESOLVE)]);

	if (lockout_level > remote_user_max_level)
		log(0,"\nNOTE: lockout_level > remote_user_max_level. This will prevent remote users\n      connecting.\n\n");
	}
log(0,"Done.\n");

// Move log file with us if its not a hardcode path else it'll create a new
// log file with the same name in the new directory
if (logfile && !strchr(logfile,'/')) {
	sprintf(path,"%s/%s",working_dir,logfile);
	if (rename(logfile,path) == -1) {
		log(0,"ERROR: Failed to move log file to working directory: %s\n",strerror(errno));
		exit(1);
		}
	}

// Now cd to working dir
if (chdir(working_dir) == -1) {
	log(0,"ERROR: Failed to cd to working directory: %s\n",
		strerror(errno));
	exit(1);
	}
return;


EXTRA:
log(0,"ERROR: Too many values for option '%s' on line %d.\n",sl.word[0],linenum);
exit(1);
}




/*** Value must be integer and > 0 ***/
int parse_config_integer(cl_splitline *sl, int linenum)
{
int val;

if (sl->wcnt > 2) {
	log(0,"ERROR: Too many values for option '%s' on line %d.\n",
		sl->word[0],linenum);
	exit(1);
	}
if (!is_integer(sl->word[1]) || (val = atoi(sl->word[1])) < 0) {
	log(0,"ERROR: Invalid value '%s' on line %d.\n",sl->word[1],linenum);
	exit(1);
	}
return val;
}




/*** Parse a time line in a config file line ***/
int parse_config_time(cl_splitline *sl,int linenum, int gzero)
{
int secs;

if (sl->wcnt > 3) {
	log(0,"ERROR: Too many values for option '%s' on line %d.\n",
		sl->word[0],linenum);
	exit(1);
	}
if ((secs=get_seconds(sl->word[1],sl->wcnt == 3 ? sl->word[2] : NULL)) < 0) {
	log(0,"ERROR: Invalid time or time period on line %d.\n",linenum);
	exit(1);
	}	
if (gzero && !secs) {
	log(0,"ERROR: Time must be greater than zero on line %d.\n",linenum);
	exit(1);
	}
return secs;
}




/*** Parse a level ***/
int parse_config_level(cl_splitline *sl,int linenum)
{
int lev;

if (sl->wcnt > 2) {
	log(0,"ERROR: Too many values for option '%s' on line %d.\n",
		sl->word[0],linenum);
	exit(1);
	}
if ((lev = get_level(sl->word[1])) == -1 || lev < USER_LEVEL_NOVICE) {
	log(0,"ERROR: Invalid user level on line %d.\n",linenum);
	exit(1);
	}
return lev;
}




/*** Parse the remote server lines ***/
void parse_remote_server_line(cl_splitline *sl,int linenum)
{
cl_server *svr;
int svr_port,loc_port;
uint32_t conkey;
u_char enclev;

svr_port = SERVER_PORT;
loc_port = 0;
conkey = 0;
enclev = 0;

switch(sl->wcnt) {
	case 2:
	log(0,"ERROR: Missing ip address for server '%s' on line %d.\n",sl->word[1],linenum);
	exit(1);

	case 3:
	/* Remote address, do nothing with it here */
	break;

	case 7:
	if (!strcasecmp(sl->word[6],"encrypt")) enclev = MAX_ENCRYPTION_LEVEL;
	// Fall through
		
	case 6:
	// Check connect key 
	if (sl->word[5][0] &&
	    (!is_integer(sl->word[5]) || (conkey = atoi(sl->word[5])) < 0)) {
		log(0,"ERROR: Invalid connect key '%s' on line %d.\n",
			sl->word[5],linenum);
		exit(1);
		}
	// Fall through

	case 5:
	// Check local port 
	if (sl->word[4][0] &&
	    (!is_integer(sl->word[4]) ||
	    (loc_port = atoi(sl->word[4])) < 1 || loc_port > 65535)) {
		log(0,"ERROR: Invalid local port number '%s' on line %d. Range is 1 to 65535.\n",sl->word[4],linenum);
		exit(1);
		}
	// Fall through

	case 4:
	// Check remote port 
	if (sl->word[3][0] &&
	    (!is_integer(sl->word[3]) ||
	    (svr_port = atoi(sl->word[3])) < 1 || svr_port > 65535)) {
		log(0,"ERROR: Invalid remote port number '%s' on line %d. Range is 1 to 65535.\n",sl->word[3],linenum);
		exit(1);
		}
	}

if ((svr=new cl_server(
	sl->word[1],
	sl->word[2],
	(uint16_t)svr_port,
	(uint16_t)loc_port,conkey,enclev,NULL))->create_error != OK) {
	log(1,"ERROR: Unable to create new server object on line %d: %s\n",
		linenum,err_string[svr->create_error]);
	exit(1);
	}
}




/*** Parse yes or no on config line ***/
int parse_config_yn(cl_splitline *sl,int linenum)
{
if (sl->wcnt > 2) {
	log(0,"ERROR: Too many values for option '%s' on line %d.\n",
		sl->word[0],linenum);
	exit(1);
	}
if (!strcasecmp(sl->word[1],"NO")) return 0;
else 
if (!strcasecmp(sl->word[1],"YES")) return 1;

log(0,"ERROR: Value must be YES or NO on line %d.\n",linenum);
exit(1);
return 0; // Stops some compilers whinging
}




/*** Parse list of signals to ignore ***/
void parse_signal_ignore(cl_splitline *sl, int linenum)
{
int i,j;

for(i=1;i < sl->wcnt;++i) {
	for(j=0;j < NUM_SIGNALS;++j) 
		if (!strcasecmp(sl->word[i],siglist[j].name)) break;
	if (j == NUM_SIGNALS) {
		log(0,"ERROR: Invalid signal name '%s' on line %d.\n",
			sl->word[i],linenum);
		exit(1);
		}
	siglist[j].ignoring = 1;
	}
SYS_SETFLAG(SYS_FLAG_IGNORING_SIGS);
}




/*** Load banned user & sites list ***/
void load_bans()
{
cl_splitline sl(1);
FILE *fp;
int linenum,ret,ban_level;
int ucnt,scnt;
char line[ARR_SIZE];
char path[MAXPATHLEN];

log(0,"\nLoading bans...");
sprintf(path,"%s/%s",ETC_DIR,BAN_FILE);
if (!(fp=fopen(path,"r"))) {
	log(0,"   No bans found.\nDone.\n");  return;
	}

linenum=1;
fgets(line,ARR_SIZE-1,fp);

for(ucnt=scnt=0;!feof(fp);) {
	if ((ret=sl.parse(line)) != OK) {
		log(0,"ERROR: %s on line %d.\n",err_string[ret],linenum);
		exit(1);
		}
	if (sl.wcnt && sl.word[0][0] != '#') {
		if (sl.wcnt < 3) {
			log(0,"ERROR: Missing arguments on line %d.\n",
				linenum);
			exit(1);
			}
		if ((ban_level = get_level(sl.word[1])) == -1) {
			log(0,"ERROR: Invalid level on line %d.\n",linenum);
			exit(1);
		}
		if (!strcasecmp(sl.word[0],"user ban")) {
			ret=load_user_ban(ban_level,&sl);
			ucnt++;
			}
		else
		if (!strcasecmp(sl.word[0],"site ban")) {
			ret=load_site_ban(ban_level,&sl);
			scnt++;
			}
		else {
			log(0,"ERROR: Invalid option on line %d.\n",linenum);
			exit(1);
			}
		if (ret != OK) {
			log(0,"ERROR: Cannot set ban on line %d: %s\n",
				linenum,err_string[ret]);
			exit(1);
			}
		}
	fgets(line,ARR_SIZE-1,fp);
	linenum++;
	}
fclose(fp);
if (ucnt || scnt)
	log(0,"   %d user bans, %d site bans loaded.\nDone.\n\n",ucnt,scnt);
else log(0,"   Empty file.\nDone.\n");
}




/*** Set user ban ***/
int load_user_ban(int ban_level, cl_splitline *sl)
{
st_user_ban *ub,*ub2;
uint16_t uid,port;
int addr;

if (!(uid = idstr_to_id(sl->word[2]))) return ERR_INVALID_VALUE;

ub = new st_user_ban;
ub->uid = uid;
ub->level = ban_level;
ub->user = NULL;
bzero(&ub->home_addr,sizeof(sockaddr_in));

switch(sl->wcnt) {
	case 3:
	FOR_ALL_USER_BANS(ub2) {
		if (ub2->utype == USER_TYPE_LOCAL && ub2->uid == uid) 
			return ERR_ALREADY_BANNED;
		}
	ub->utype = USER_TYPE_LOCAL;

	log(0,"   Local user : Id = %04X, Ban level = %s\n",
		uid,user_level[ban_level]);
	break;

	case 5:
	if ((addr = inet_addr(sl->word[3])) == -1 ||
	    !(port = htons((uint16_t)atoi(sl->word[4]))))
		return ERR_INVALID_VALUE;

	FOR_ALL_USER_BANS(ub2) {
		if (ub2->utype == USER_TYPE_REMOTE &&
		    ub2->uid == uid &&
		    ub2->home_addr.sin_addr.s_addr == (uint32_t)addr &&
		    ub2->home_addr.sin_port == port)
			return ERR_ALREADY_BANNED;
		}

	ub->utype = USER_TYPE_REMOTE;
	ub->home_addr.sin_addr.s_addr = (uint32_t)addr;
	ub->home_addr.sin_port = port;

	log(0,"   Remote user: Id = %04X, Ban level = %s, IP = %s:%s\n",
		uid,user_level[ban_level],sl->word[3],sl->word[4]);
	break;


	default: return ERR_INVALID_VALUE;
	}
add_list_item(first_user_ban,last_user_ban,ub);	
return OK;
}




/*** Ban a site (for user logon and server connect) ***/
int load_site_ban(int ban_level, cl_splitline *sl)
{
st_site_ban *sb,*sb2;
int addr;

if (sl->wcnt != 4 ||
    (addr = inet_addr(sl->word[2])) == -1) return ERR_INVALID_VALUE;

sb = new st_site_ban;
if (!strcasecmp(sl->word[3],"USER")) sb->type = SITEBAN_TYPE_USER;
else
if (!strcasecmp(sl->word[3],"SERVER")) sb->type = SITEBAN_TYPE_SERVER;
else
if (!strcasecmp(sl->word[3],"ALL")) sb->type = SITEBAN_TYPE_ALL;
else
return ERR_INVALID_VALUE;

FOR_ALL_SITE_BANS(sb2) {
	if (sb2->addr.sin_addr.s_addr == (uint32_t)addr &&
	    (sb2->type == sb->type || 
	     sb2->type == SITEBAN_TYPE_ALL ||
	     sb->type == SITEBAN_TYPE_ALL)) return ERR_ALREADY_BANNED;
	}
sb->level = ban_level;
bzero(&sb->addr,sizeof(sockaddr_in));
sb->addr.sin_addr.s_addr = (uint32_t)addr;
add_list_item(first_site_ban,last_site_ban,sb);

log(0,"   Site ban   : IP = %s, Type = %s, Ban level = %s\n",
	sl->word[2],siteban_type[sb->type],user_level[ban_level]);
return OK;
}




/*** Create system groups ***/
void create_system_groups()
{
system_group_count = 0;
public_group_count = 0;
user_group_count = 0;

gone_remote_group = new cl_group(
		(uint16_t)GONE_REMOTE_GROUP_ID,
		GONE_REMOTE_GROUP_NAME,GROUP_TYPE_SYSTEM,NULL);

remotes_home_group = new cl_group(
		(uint16_t)REMOTES_HOME_GROUP_ID,
		REMOTES_HOME_GROUP_NAME,GROUP_TYPE_SYSTEM,NULL);

prison_group = new cl_group (
		(uint16_t)PRISON_GROUP_ID,
		PRISON_GROUP_NAME,GROUP_TYPE_SYSTEM,NULL);
}




/*** Scan the public groups directory and load them in ***/
void load_public_groups()
{
DIR *dir;
struct dirent *ds;
cl_group *grp;
uint16_t gid;

log(0,"\nLoading public groups...");
if (!(dir=opendir(PUB_GROUP_DIR))) {
	log(0,"WARNING: Unable to scan public groups directory.");
	return;
	}

// Loop through directory entries. Only process subdirectories whos name
// is a valid id and is < MIN_LOCAL_USER_ID.
while((ds=readdir(dir))) {
	if ((gid = idstr_to_id(ds->d_name)) && gid < MIN_LOCAL_USER_ID) {
		if ((grp = new cl_group(gid,NULL))->error != OK) {
			log(0,"   ERROR: Can't create group %04X: %s\n",
				gid,err_string[grp->error]);
			delete grp;
			}
		else log(0,"   Loaded: %04X, %s\n",gid,grp->name);
		}
	}
closedir(dir);
log(0,"Done.\n");
}




/*** Create the events & connect threads. We don't spawn the connect thread
     in the server constructor as this could cause problems when the program
     is still booting. ***/
void spawn_all_threads()
{
cl_server *svr;
pthread_t thrd;

// Add main thread (this thread) to threads list just for info purposes
add_thread_entry(THREAD_TYPE_MAIN,pthread_self());

// Create events thread
if (create_thread(THREAD_TYPE_EVENTS,&thrd,do_events,NULL) == -1) {
	perror("ERROR: pthread()");
	exit(1);
	}

// Spawn server connect threads
if (first_server) {
	log(0,"\nSpawning connect threads...\n");
	FOR_ALL_SERVERS(svr) svr->spawn_thread(THREAD_TYPE_CONNECT);
	}
log(0,"\nBoot complete on: %s\n",ctime(&server_time));
booting = 0;

// Dissociate from tty if running as daemon. Can't do this earlier or we'll
// lose boot/error messages
if (be_daemon) dissociate_from_tty();
}




/*** Self explanatory ***/
void mainloop()
{
struct timeval tv;
int selerr;

selerr=0;

while(1) {
	pthread_mutex_lock(&events_mutex);
	setup_readmask();
	pthread_mutex_unlock(&events_mutex);

	/* Have timeout for select because server connect threads may connect
	   while we're waiting here but the readmask won't be set up on their
	   sockets. This timeout ensures we'll always see them eventually. */
	tv.tv_sec = HEARTBEAT;
	tv.tv_usec = 0;
	switch(select(FD_SETSIZE,&readmask,0,0,&tv)) {
		case -1:
		if (++selerr == MAX_SELECT_ERRORS) {
			log(1,"\nPANIC: select() function keeps erroring.\n");
			exit(1);
			}

		case 0: 
		// Timeout
		selerr = 0;
		continue;
		}

	// Deal with the user input. Set mutex so events can't run while
	// we're doing this
	pthread_mutex_lock(&events_mutex);
	get_input();
	pthread_mutex_unlock(&events_mutex);

	selerr=0;
	}
}




/*** Set up the select readmask ***/
void setup_readmask()
{
cl_user *u;
cl_server *svr;
cl_local_user *lu;

FD_ZERO(&readmask);

FOR_ALL_USERS(u) {
	lu = (cl_local_user *)u;
	if (u->type != USER_TYPE_REMOTE &&
	    !(u->flags & USER_FLAG_LINKDEAD) && lu->sock != -1) 
		FD_SET(lu->sock,&readmask);
	}

FOR_ALL_SERVERS(svr) {
	// SERVER_STAGE_CONNECTING is dealt with in a connect thread
	if (svr->stage == SERVER_STAGE_INCOMING ||
	    svr->stage == SERVER_STAGE_CONNECTED) 
		FD_SET(svr->sock,&readmask);
	}

FD_SET(listen_sock[PORT_TYPE_USER],&readmask);
if (SYS_FLAGISSET(SYS_FLAG_SVR_LISTEN))
	FD_SET(listen_sock[PORT_TYPE_SERVER],&readmask);
}




/*** Get input from users/servers ***/
void get_input()
{
cl_user *u,*nu;
cl_local_user *lu;
cl_server *svr;

if (FD_ISSET(listen_sock[PORT_TYPE_USER],&readmask)) {
	lu = new cl_local_user(0);
	if (lu->error != OK) {
		delete lu;  return;
		}
	// Can't throw exception in constructor so check here
	if (site_is_banned(SITEBAN_TYPE_USER,lu->ip_addr.sin_addr.s_addr)) {
		log(1,"BANNED site, disconnecting...\n");
		// Can't put colour in message 'cos telopt not done
		lu->uprintf("*** CONNECTIONS FROM YOUR SITE ARE NOT ALLOWED ***\n\n");
		lu->disconnect();
		}
	}
if (SYS_FLAGISSET(SYS_FLAG_SVR_LISTEN) &&
    FD_ISSET(listen_sock[PORT_TYPE_SERVER],&readmask)) new cl_server;

// Go through users
for (u=first_user;u;u=nu) {
	lu = (cl_local_user *)u;
	// sock = -1 if linkdead. More efficient than checking flag.
	if (lu->type != USER_TYPE_REMOTE &&
	    lu->sock != -1 && FD_ISSET(lu->sock,&readmask)) {
		try {
			lu->uread();

			/* We set this here because the user could have killed
			   or banned user(s) who are the next one in the linked
			   list and thus would render the pointer invalid if
			   already set. */
			nu=u->next; 
			}
		catch(enum user_stages stg) {
			if (stg == USER_STAGE_DISCONNECT) {
				nu=u->next;
				lu->disconnect();
				}
			else 
			log(1,"INTERNAL ERROR: Caught unexpected user_stage %d in get_input()",stg);
			}
		}
	else nu=u->next;
	}

// Go through servers.
FOR_ALL_SERVERS(svr) {
	// SERVER_STAGE_CONNECTING is dealt with in a connect thread
	if ((svr->stage == SERVER_STAGE_INCOMING ||
	     svr->stage == SERVER_STAGE_CONNECTED) &&
	    FD_ISSET(svr->sock,&readmask)) svr->sread();
	}
}




/*** Signal handler for signals in globals list ***/
void signal_handler(int signum, siginfo_t *siginfo, void *notused)
{
#ifndef BSD
struct passwd *pwd;
#endif
char *astr;
int i;

for(i=0;i < NUM_SIGNALS;++i) {
	if (siglist[i].num == signum) {
		/* If keyboard signal (how does the shell send its signals??)
		   or alarm signal then siginfo ptr not set in Solaris and 
		   si_pid not set in Linux. The less said about BSD the better
		   as it doesn't set anything ever it seems. */
#ifdef BSD
		sprintf(text,"Received signal %d (SIG%s)",signum,siglist[i].name);
#else
		if (siginfo && siginfo->si_pid) {
			pwd=getpwuid(siginfo->si_uid);
			sprintf(text,"Received signal %d (SIG%s) from pid %d (unix uid %d (%s))",
				signum,
				siglist[i].name,
				siginfo->si_pid,
				siginfo->si_uid,
				pwd->pw_name);
			}
		else
		sprintf(text,"Received signal %d (SIG%s) from keyboard",signum,siglist[i].name);
#endif

		if (siglist[i].ignoring) {
			// Reset handler and return
			log(1,"%s but ignoring.\n",text);
			allprintf(MSG_SYSTEM,USER_LEVEL_ADMIN,NULL,"%s but ignoring.\n",text);
			sigaction(signum,&sigact,NULL);	
			return;
			}

		// Reboot if ^C else shutdown.
		shutdown_time = server_time;
		shutdown_type = siglist[i].num == SIGINT ?
		                SHUTDOWN_REBOOT : SHUTDOWN_STOP;

		astr = (siglist[i].num == SIGINT ?
		       (char *)"reboot" : (char *)"shutdown");
		log(1,"%s and initiating %s...\n",text,astr);
		allprintf(MSG_SYSTEM,USER_LEVEL_ADMIN,NULL,"%s and initiating %s...\n",text,astr);

		// Ignore all signals now
		for(i=0;i < NUM_SIGNALS;++i) signal(siglist[i].num,SIG_IGN);
		return;
		}
	}
log(1,"WARNING: Caught unhandled signal %d.\n",signum);
}
