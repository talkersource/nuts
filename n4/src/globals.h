/***************************************************************************
 FILE: globals.h
 LVU : 1.4.1

 DESC:
 This includes all the appropriate header files and defines all the global
 macros, classes , structures and other variables required by the system.

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

#ifdef SOLARIS
// Define extensions or C++ compiler won't see the localtime_r() function.
// Better ask Sun why cos I sure as hell have no idea.
#define __EXTENSIONS__
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <signal.h>
#ifndef BSD
#include <crypt.h>
#endif
#include <time.h>
#include <ctype.h>
#include <termios.h>
#include <dirent.h>
#include <fcntl.h>
#include <netdb.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h> 
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <locale.h>
#include <pwd.h>

#include "version.h"
#include "templates.h"

#ifndef MAINFILE
#define EXTERN extern
#else
#define EXTERN
#endif


/*** DEFINITIONS ***/
#define PROTOCOL_REVISION 6

#ifndef MAXPATHLEN
#define MAXPATHLEN 256
#endif
#define ARR_SIZE 3000
#define TEL_SIZE 20
#define LISTEN_QSIZE 20
#define USER_PORT 1413
#define SERVER_PORT 1414
#define IP6_ADDR_SIZE 16
#define SERVER_BUFF_SIZE 65535
#define INLINE_PROMPT_SIZE 100
#define DEL1 8
#define DEL2 127
#define NUM_PRINT_CODES 29
#define WORD_ALLOC 5
#define MAX_SELECT_ERRORS 10
#define MAX_SERVER_NAME_LEN 10
#define MIN_PWD_LEN 4
#define MAX_NAME_LEN 15
#define MAX_DESC_LEN 30
#define DEFAULT_TERM_COLS 80
#define DEFAULT_TERM_ROWS 25
#define DEFAULT_DESC "the novice."
#define DEFAULT_PWD "changeme"
#define NUM_COMMANDS 88
#define NUM_LEVELS 6
#define NUM_SIGNALS 10
#define HEARTBEAT 2
#define MAX_LOCAL_USERS 0  // zero = unlimited
#define MAX_REMOTE_USERS 100
#define NUM_ERRORS 73
#define NUM_PKT_TYPES 28
#define PING_INTERVAL 60
#define SERVER_TIMEOUT 180
#define CONNECT_TIMEOUT 30
#define ID_NOT_SET 255
#define MAX_TX_ERR_CNT 1
#define MAX_RX_ERR_CNT 5
#define MAX_PACKET_RATE 1000 // 1000 per sec seems more than reasonable
#define MAX_LOCAL_USER_DATA_RATE 1000  // 1K/sec
#define MAX_SVR_DATA_RATE 50000  // 50K/sec
#define MAX_HOP_CNT 254
#define MIN_LOCAL_USER_ID 0x0100
#define MAX_LOCAL_USER_ID 0x0FFF
#define MAX_WRITE_RETRY 3
#define LINKDEAD_TIMEOUT 120
#define LOGIN_TIMEOUT 60
#define IDLE_TIMEOUT 300
#define MAX_PROFILE_CHARS 800 // 10 lines
#define MAX_MAIL_CHARS 1600 // 20 lines
#define MAX_BOARD_CHARS 800
#define MAX_GROUP_DESC_CHARS 800
#define MAX_BROADCAST_CHARS 400
#define MAX_LOGIN_BATCH_LINES 5
#define MAX_LOGOUT_BATCH_LINES 5
#define MAX_SESSION_BATCH_LINES 5
#define MAX_INCLUDE_LINES 5
#define MAX_SUBJECT_LEN 40
#define MAX_GROUP_NAME_LEN 30
#define MAX_BATCH_NAME_LEN 20
#define EDITOR_MALLOC 1600 // 20 lines
#define NUM_REVIEW_LINES 20
#define REVIEW_LINE_LEN 160
#define MAX_INVITES 10
#define GROUP_INVITE_EXPIRE 300
#define BOARD_MSG_EXPIRE 432000 // 432000 seconds = 5 days
#define AUTOSAVE_INTERVAL 900
#define NUM_SITEBAN_TYPES 3
#define MAX_ENCRYPTION_LEVEL 1
#define SOFT_MAX_SERVERS 0 // Unlimited

#define UNRESOLVED_STR "<unresolved>"
#define COM_NO_ARGS "This command takes no arguments.\n"
#define NO_SUCH_GROUP "No such group.\n"
#define NO_SUCH_MESSAGE "No such message.\n"

#define GONE_REMOTE_GROUP_ID 1
#define GONE_REMOTE_GROUP_NAME "~BYGone remote group"
#define REMOTES_HOME_GROUP_ID 2
#define REMOTES_HOME_GROUP_NAME "~BGRemotes home group"
#define PRISON_GROUP_ID 3
#define PRISON_GROUP_NAME "~BRPrison group"

#define CONFIG_FILE "default.n4c"
#define CONFIG_EXT ".n4c"
#define BATCH_PRE "batch_"
#define WORKING_DIR "."
#define SYS_USER_NAME "SYSTEM"
#define USER_DIR "users"
#define HELP_DIR "help"
#define ETC_DIR "etc"
#define BOARD_DIR "boards"
#define BAN_FILE "banfile"
#define NEWS_FILE "newsfile"
#define PRELOGIN_SCREEN "prelogin_screen"
#define POSTLOGIN_SCREEN "postlogin_screen"
#define BOARD_INFO_FILE "boardinfo"
#define PUB_GROUP_DIR "pubgroups"
#define PUB_GROUP_CONFIG_FILE "config"
#define PUB_GROUP_DESC_FILE "desc"
#define PUB_GROUP_LOG_FILE "log"
#define MAIN_HELP_FILE "mainhelp"
#define USER_CONFIG_FILE "config"
#define USER_PROFILE_FILE "profile"
#define USER_MAILINFO_FILE "mailinfo"
#define USER_GROUP_CONFIG_FILE "groupconfig"
#define USER_GROUP_DESC_FILE "groupdesc"
#define USER_GROUP_LOG_FILE "grouplog"
#define PAGE_QUIT_KEY 'Q'

// Flag set macros
#define SYS_FLAGISSET(X) ((system_flags & (X)) > 0)
#define SYS_SETFLAG(X) (system_flags |= (X))
#define SYS_UNSETFLAG(X) (system_flags &= ~(X))

#define FLAGISSET(X) ((flags & (X)) > 0)
#define SETFLAG(X) (flags |= (X))
#define UNSETFLAG(X) (flags &= ~(X))
#define UNSETPREVFLAG(X) (prev_flags &= ~(X))

#define O_FLAGISSET(O,X) ((O->flags & (X)) > 0)
#define O_SETFLAG(O,X) (O->flags |= (X))
#define O_UNSETFLAG(O,X) (O->flags &= ~(X))
#define O_SETPREVFLAG(O,X) (O->prev_flags |= (X))
#define O_UNSETPREVFLAG(O,X) (O->prev_flags &= ~(X))

#define FREE(X) if (X) free(X),X=NULL

#define FOR_ALL_USERS(U) for(U=first_user;U;U=U->next)
#define FOR_ALL_GROUPS(G) for(G=first_group;G;G=G->next)
#define FOR_ALL_FRIENDS(F) for(F=first_friend;F;F=F->next)
#define FOR_ALL_USERS_FRIENDS(U,F) for(F=U->first_friend;F;F=F->next)
#define FOR_ALL_SERVERS(S) for(S=first_server;S;S=S->next)
#define FOR_ALL_MSGS(M) for(M=first_msg;M;M=M->next)
#define FOR_ALL_USER_BANS(B) for(B=first_user_ban;B;B=B->next)
#define FOR_ALL_GROUP_BANS(B) for(B=first_ban;B;B=B->next)
#define FOR_ALL_SITE_BANS(B) for(B=first_site_ban;B;B=B->next)
#define FOR_ALL_THREADS(T) for(T=first_thread;T;T=T->next)


/*** ENUMS ***/

enum {
	PORT_TYPE_USER,
	PORT_TYPE_SERVER
	};

enum { 
	TELNET_IS,
	TELNET_SEND
	};

enum {
	TELNET_ECHO=1,
	TELNET_SGA=3,
	TELNET_TERM=24,
	TELNET_NAWS=31,
	TELNET_SE=240,
	TELNET_SB=250,
	TELNET_WILL,
	TELNET_WONT,
	TELNET_DO,
	TELNET_DONT,
	TELNET_IAC
	};

// System flags
enum {
	SYS_FLAG_PRS_RET_HOME         = 1,
	SYS_FLAG_RANDOM_REM_IDS       = (1 << 1),
	SYS_FLAG_LOOPBACK_USERS       = (1 << 2),
	SYS_FLAG_BOARD_RENUM          = (1 << 3),
	SYS_FLAG_IGNORING_SIGS        = (1 << 4),
	SYS_FLAG_LOG_NETBCAST         = (1 << 5),
	SYS_FLAG_HEXDUMP_PACKETS      = (1 << 6),
	SYS_FLAG_STRIP_PRINTCODES     = (1 << 7),
	SYS_FLAG_ALLOW_WHO_AT_LOGIN   = (1 << 8),
	SYS_FLAG_ALLOW_REM_BATCH_RUNS = (1 << 9),
	SYS_FLAG_ALLOW_NEW_ACCOUNTS   = (1 << 10),
	SYS_FLAG_DEL_DISC_INCOMING    = (1 << 11),
	SYS_FLAG_LOG_UNEXPECTED_PKTS  = (1 << 12),
	SYS_FLAG_SVR_LISTEN           = (1 << 13),
	SYS_FLAG_OUT_ENF_ENCRYPTION   = (1 << 14),
	SYS_FLAG_INTERNAL_RESOLVE     = (1 << 15),
	SYS_FLAG_SAVE_NOVICE_ACCOUNTS = (1 << 16),
	SYS_FLAG_REALLY_DEL_ACCOUNTS  = (1 << 17),
	SYS_FLAG_LOG_GROUPS           = (1 << 18)
	};

// Default flags settings
#define DEFAULT_SYSTEM_FLAGS \
	(SYS_FLAG_PRS_RET_HOME | \
	 SYS_FLAG_LOG_NETBCAST | \
	 SYS_FLAG_STRIP_PRINTCODES | \
	 SYS_FLAG_ALLOW_WHO_AT_LOGIN | \
	 SYS_FLAG_ALLOW_NEW_ACCOUNTS | \
	 SYS_FLAG_LOG_UNEXPECTED_PKTS | \
	 SYS_FLAG_SVR_LISTEN | \
	 SYS_FLAG_DEL_DISC_INCOMING | \
	 SYS_FLAG_OUT_ENF_ENCRYPTION | \
	 SYS_FLAG_INTERNAL_RESOLVE | \
	 SYS_FLAG_SAVE_NOVICE_ACCOUNTS)

// Server class flags
enum {
	SERVER_FLAG_RX_WRAPPED = 1,
	SERVER_FLAG_TX_WRAPPED = (1 << 1)
	};

// Command enums. Only ones that are required are defined.
enum {
	COM_QUIT,
	COM_TOGGLE,
	COM_SAY,
	COM_EMOTE,
	COM_PEMOTE,
	COM_SEMOTE,
	COM_TELL,
	COM_NTELL,
	COM_NEMOTE,
	COM_FTELL,
	COM_FEMOTE,
	COM_SHOUT,
	COM_SHUTDOWN = 59,
	COM_REBOOT,
	COM_THINK = 86
	};

// User enums follow, they're mostly self explanatory.
enum {
	USER_TYPE_LOCAL,
	USER_TYPE_REMOTE
	};

enum {
	USER_LEVEL_LOGIN,
	USER_LEVEL_NOVICE,
	USER_LEVEL_USER,
	USER_LEVEL_MONITOR,
	USER_LEVEL_OPERATOR,
	USER_LEVEL_ADMIN
	};

#define MAX_USER_IGN_LEVEL USER_LEVEL_ADMIN
#define IDLE_TIMEOUT_IGN_LEVEL USER_LEVEL_MONITOR
#define LOCKOUT_LEVEL USER_LEVEL_NOVICE
#define GROUP_MODIFY_LEVEL USER_LEVEL_OPERATOR
#define GROUP_GATECRASH_LEVEL USER_LEVEL_ADMIN
#define REMOTE_USER_MAX_LEVEL USER_LEVEL_USER
#define GO_INVIS_LEVEL USER_LEVEL_ADMIN
#define RECV_NET_BCAST_LEVEL USER_LEVEL_ADMIN

enum {
	USER_LOAD_1,
	USER_LOAD_2
	};

enum user_stages {
	// 0
	USER_STAGE_DISCONNECT,
	USER_STAGE_NEW,
	USER_STAGE_LOGIN_ID,
	USER_STAGE_LOGIN_PWD,
	USER_STAGE_LOGIN_NAME,

	// 5
	USER_STAGE_LOGIN_NEW_PWD,
	USER_STAGE_LOGIN_REENTER_PWD,
	USER_STAGE_CMD_LINE,
	USER_STAGE_MAILER,
	USER_STAGE_MAILER_SUBJECT1,

	// 10
	USER_STAGE_MAILER_SUBJECT2,
	USER_STAGE_MAILER_DEL,
	USER_STAGE_MAILER_READ_FROM,
	USER_STAGE_BOARD,
	USER_STAGE_BOARD_SUBJECT,

	// 15
	USER_STAGE_BOARD_DEL,
	USER_STAGE_BOARD_READ_FROM,
	USER_STAGE_OLD_PWD,
	USER_STAGE_NEW_PWD,
	USER_STAGE_REENTER_PWD,

	// 20
	USER_STAGE_SUICIDE,
	USER_STAGE_EXAMINE,
	USER_STAGE_SHUTDOWN,
	USER_STAGE_REBOOT,
	USER_STAGE_AFK,

	// 25
	USER_STAGE_AFK_LOCK,
	USER_STAGE_DELETE_GROUP,
	USER_STAGE_DELETE_GROUP_LOG,
	USER_STAGE_DELETE_USER,
	USER_STAGE_DELETE_BATCH_FILE,

	// 30
	USER_STAGE_OVERWRITE_BATCH_FILE,
	USER_STAGE_OVERWRITE_CONFIG_FILE
	};

// NOTE TO SELF: when adding new flags remember propagation and user
// save/load and current level at which flag allowed
enum {
	USER_FLAG_ECHO             = 1,
	USER_FLAG_SGA              = (1 << 1),
	USER_FLAG_TERMTYPE         = (1 << 2),
	USER_FLAG_TERMSIZE         = (1 << 3),
	USER_FLAG_GOT_TELOPT_INFO  = (1 << 4),
	USER_FLAG_ANSI_TERM        = (1 << 5),
	USER_FLAG_LINKDEAD         = (1 << 6),
	USER_FLAG_CONVERSE         = (1 << 7),
	USER_FLAG_PROMPT           = (1 << 8),
	USER_FLAG_LEFT             = (1 << 9),
	USER_FLAG_IP6_ADDR         = (1 << 10),
	USER_FLAG_TIMEOUT_WARNING  = (1 << 11),
	USER_FLAG_TIMEOUT          = (1 << 12),
	USER_FLAG_NEW_LOGIN        = (1 << 13),
	USER_FLAG_NEW_USER         = (1 << 14),
	USER_FLAG_PAGING           = (1 << 15),
	USER_FLAG_NO_SPEECH        = (1 << 16),
	USER_FLAG_NO_TELLS         = (1 << 17),
	USER_FLAG_NO_SHOUTS        = (1 << 18),
	USER_FLAG_NO_INFO          = (1 << 19),
	USER_FLAG_NO_MISC          = (1 << 20),
	USER_FLAG_PUIP             = (1 << 21),
	USER_FLAG_DELETE           = (1 << 22),
	USER_FLAG_MUZZLED          = (1 << 23),
	USER_FLAG_PRISONER         = (1 << 24),
	USER_FLAG_INVISIBLE        = (1 << 25),
	USER_FLAG_TEMP_OBJECT      = (1 << 26),
	USER_FLAG_HOME_GRP_PERSIST = (1 << 27),
	USER_FLAG_RECV_NET_BCAST   = (1 << 28),
	USER_FLAG_AUTOSILENCE      = (1 << 29)
	};

#define REMOTE_IGNORE_FLAGS_MASK (uint32_t)\
	(~((uint32_t)USER_FLAG_MUZZLED | \
	   (uint32_t)USER_FLAG_INVISIBLE | \
	   (uint32_t)USER_FLAG_RECV_NET_BCAST))

// These are the types the editor object can assume. NOTE TO SELF: If these
// are modified change array in cl_user::run_editor()
enum {
	EDITOR_TYPE_PROFILE,
	EDITOR_TYPE_LOGIN_BATCH,
	EDITOR_TYPE_LOGOUT_BATCH,
	EDITOR_TYPE_SESSION_BATCH,
	EDITOR_TYPE_MAIL,
	EDITOR_TYPE_BOARD,
	EDITOR_TYPE_GROUP_DESC,
	EDITOR_TYPE_BCAST,
	EDITOR_TYPE_NET_BCAST
	};

// Editor stages
enum {
	EDITOR_STAGE_INPUT,
	EDITOR_STAGE_SRC,
	EDITOR_STAGE_CANCEL,
	EDITOR_STAGE_COMPLETE
	};

// User friend flags
enum {
	FRIEND_UNKNOWN,
	FRIEND_LOCATING,
	FRIEND_ONLINE,
	FRIEND_OFFLINE
	};

// Group types. Update group_type[] array if this changes
enum {
	GROUP_TYPE_SYSTEM,
	GROUP_TYPE_PUBLIC,
	GROUP_TYPE_USER
	};

enum {
	GROUP_FLAG_PRIVATE=1,
	GROUP_FLAG_FIXED
	};

// Server stages. Modify svr_stage_str[] array if this is updated.
enum {
	SERVER_STAGE_UNCONNECTED,
	SERVER_STAGE_INCOMING,
	SERVER_STAGE_CONNECTING,
	SERVER_STAGE_CONNECTED,
	SERVER_STAGE_CONNECT_REFUSED,
	SERVER_STAGE_CONNECT_FAILED,
	SERVER_STAGE_DISCONNECTED,
	SERVER_STAGE_NOT_FOUND,
	SERVER_STAGE_TIMEOUT,
	SERVER_STAGE_NETWORK_ERROR,
	SERVER_STAGE_PACKET_OR_DATA_OVERLOAD,
	SERVER_STAGE_MANUAL_DISCONNECT,
	SERVER_STAGE_SHUTDOWN,
	SERVER_STAGE_DELETE
	};

// Server connect types
enum {
	SERVER_TYPE_INCOMING,
	SERVER_TYPE_OUTGOING
	};
 
// Flags for printf functions
enum {
	MSG_SPEECH,
	MSG_SHOUT,
	MSG_INFO,
	MSG_MISC,
	MSG_SYSTEM,
	MSG_BCAST,
	MSG_NETBCAST
	};

// Thread types. If this is modified then modify array in 
// cl_user::com_lsthreads()
enum {
	THREAD_TYPE_MAIN,
	THREAD_TYPE_EVENTS,
	THREAD_TYPE_CONNECT,
	THREAD_TYPE_SVR_RESOLVE,
	THREAD_TYPE_USER_RESOLVE
	};

// Site ban types
enum {
	SITEBAN_TYPE_USER,
	SITEBAN_TYPE_SERVER,
	SITEBAN_TYPE_ALL
	};

/* There are 3 types of packets, command packets which force the server to
   do something, reply packets which are replies to command packets and
   informtion packets which expect no reply and are purely for informational
   purposes. NOTE TO SELF: If this gets added to change NUM_PKT_TYPES def. */
enum {
	// 0
	PKT_COM_CONNECT, 
	PKT_COM_PING,
	PKT_COM_TELL,
	PKT_COM_PEMOTE,
	PKT_COM_JOIN,

	// 5
	PKT_COM_INPUT,  
	PKT_COM_PRINT,     
	PKT_COM_FIND_USER,
	PKT_COM_REQ_USER_INFO,
	PKT_COM_EXAMINE,

	// 10
	PKT_COM_MAIL,
	PKT_COM_UJOIN,
	PKT_REP_CONNECT,
	PKT_REP_PING,  
	PKT_REP_JOIN,

	// 15
	PKT_REP_FIND_USER,
	PKT_REP_MAIL,
	PKT_INF_SVR_INFO,
	PKT_INF_LEAVE,    
	PKT_INF_LEFT, 

	// 20
	PKT_INF_USER_INFO,
	PKT_INF_TERMSIZE,
	PKT_INF_USER_FLAGS,
	PKT_INF_LOGON_NOTIFY,  
	PKT_INF_LOGOFF_NOTIFY,

	// 25
	PKT_INF_GROUP_CHANGE, 
	PKT_INF_DISCONNECTING,
	PKT_INF_BCAST
	};

#ifdef MAINFILE
char *packet_type[] = {
	"COM_CONNECT",
	"COM_PING",
	"COM_TELL",
	"COM_PEMOTE",
	"COM_JOIN",

	// 5
	"COM_INPUT",
	"COM_PRINT",
	"COM_FIND_USER",
	"COM_REQ_USER_INFO",
	"COM_EXAMINE",

	// 10
	"COM_MAIL",
	"COM_UJOIN",
	"REP_CONNECT",
	"REP_PING",  
	"REP_JOIN",

	// 15
	"REP_FIND_USER",
	"REP_MAIL",
	"INF_SVR_INFO",
	"INF_LEAVE",    
	"INF_LEFT", 

	// 20
	"INF_USER_INFO",
	"INF_TERMSIZE",
	"INF_USER_FLAGS",
	"INF_LOGON_NOTIFY",  
	"INF_LOGOFF_NOTIFY",

	// 25
	"INF_GROUP_CHANGE", 
	"INF_DISCONNECTING",
	"INF_BCAST"
	};
#else
extern char *packet_type[];
#endif

// Program and network errors
enum {
	OK,
	ERR_INTERNAL,
	ERR_MISSING_EQUALS,
	ERR_MISSING_VALUE,
	ERR_MISSING_COMMA,
	ERR_NO_DIR,
	ERR_CANT_CREATE_DIR,
	ERR_CANT_RENAME_FILE,
	ERR_CANT_OPEN_FILE,
	ERR_CANT_SEEK,
	ERR_CANT_SPAWN_THREAD,
	ERR_CONFIG,
	ERR_NAME_NOT_SET,
	ERR_MALLOC,
	ERR_WRITE,
	ERR_INVALID_VALUE,
	ERR_INVALID_ID,
	ERR_INVALID_SERVER,
	ERR_INVALID_STAGE,
	ERR_INVALID_TYPE,
	ERR_INVALID_ADDRESS,
	ERR_INVALID_NAME,
	ERR_NO_SUCH_USER,
	ERR_NO_SUCH_SERVER,
	ERR_NO_FREE_IDS,
	ERR_DUPLICATE_NAME,
	ERR_WHITESPACE,
	ERR_SERVER_NOT_CONNECTED,
	ERR_RECEIVE,
	ERR_NAME_TOO_LONG,
	ERR_MSG_TOO_LONG,
	ERR_ALREADY_GONE_REMOTE,
	ERR_ALREADY_IN_LIST,
	ERR_NOT_IN_LIST,
	ERR_USER_NO_TELLS,
	ERR_USER_AFK,
	ERR_RESTRICTED_GROUP,
	ERR_BANNED_FROM_GROUP,
	ERR_NOSPEECH,
	ERR_MUZZLED,
	ERR_MMAP_FAILED,
	ERR_INCLUDE_FILE_TOO_BIG,
	ERR_GROUP_ACCESS_SAME,
	ERR_GROUP_FIXED,
	ERR_SYSTEM_GROUP,
	ERR_ALREADY_BANNED,
	ERR_USER_GONE_REMOTE,
	ERR_SOCKET,

	// Reserve space for any future errors so we don't upset positions
	// of network errors
	ERR_RESERVED1,
	ERR_RESERVED2,

	// Network specific errors/reasons
	ERR_MAX_SERVERS,
	ERR_INVALID_CONNECT_KEY,
	ERR_REVISION_TOO_LOW,
	ERR_SVR_NAME_HAS_WHITESPACE,
	ERR_PACKET_ERROR,
	ERR_USER_ALREADY_EXISTS,
	ERR_NO_SUCH_GROUP,
	ERR_PRIVATE_GROUP,
	ERR_DUPLICATE_ID,
	ERR_LOOPBACK,
	ERR_MAX_HOP_CNT,
	ERR_LOCKED_OUT,
	ERR_TIMEOUT,
	ERR_NETWORK_ERROR,
	ERR_MANUAL_DISCONNECT,
	ERR_SHUTDOWN,
	ERR_MAX_REMOTE_USERS,
	ERR_CANT_CREATE_OBJECT,
	ERR_USER_BANNED,
	ERR_SITE_BANNED,
	ERR_LEVEL_TOO_LOW,
	ERR_PACKET_OR_DATA_OVERLOAD,
	ERR_ENCRYPTED_ONLY
	};

// Result in find_user packet
enum {
	USER_NOT_FOUND,
	USER_FOUND,
	USER_FOUND_REMOTE
	};

// Print codes whose number we need to know 
enum {
	PRINT_CODE_IP=(NUM_PRINT_CODES-3),
	PRINT_CODE_NP,
	PRINT_CODE_PR
	};

// Tell types
enum {
	TELL_TYPE_TELL,
	TELL_TYPE_PEMOTE,
	TELL_TYPE_FRIENDS_TELL,
	TELL_TYPE_FRIENDS_PEMOTE
	};

// Shutdown action
enum {
	SHUTDOWN_INACTIVE,
	SHUTDOWN_STOP,
	SHUTDOWN_REBOOT
	};

// Batch types
enum {
	BATCH_TYPE_NOBATCH,
	BATCH_TYPE_LOGIN,
	BATCH_TYPE_LOGOUT,
	BATCH_TYPE_SESSION 
	};

// Incoming link encryption policy options. Change inc_encrypt_polstr[] if this
// is modified
enum {
	ENCRYPT_NEVER,
	ENCRYPT_ALWAYS,
	ENCRYPT_EITHER
	};

/*** STRING ARRAYS ***/


#ifdef MAINFILE

// Error messages
char *err_string[NUM_ERRORS]={
	"OK",
	"Internal error",
	"Missing equals",
	"Missing value",
	"Missing comma",
	"Directory does not exist",
	"Can't create directory",
	"Can't rename file",
	"Can't open/stat file",
	"Can't seek in file",
	"Can't spawn thread",
	"Error in config file",
	"Name not set",
	"Memory allocation error",
	"Write error",
	"Invalid value",
	"Invalid id",
	"Invalid server",
	"Invalid stage",
	"Invalid type",
	"Invalid address",
	"Invalid or reserved name",
	"No such user",
	"No such server",
	"No free ids",
	"Duplicate name",
	"Name has whitespace",
	"Server not connected",
	"Receive error",
	"Name too long",
	"Message too long",
	"You have already gone remote",
	"Already in list",
	"Not in list",
	"User is not receiving tells/pemotes",
	"User is AFK",
	"Restricted group",
	"You are banned from that group",
	"You have NOSPEECH on",
	"You are muzzled",
	"Memory map failed",
	"Include file too big",
	"Group access/fixing already set to that",
	"Group access is fixed",
	"System group",
	"Already banned",
	"User gone remote",
	"Socket creation/operation failure",

	"Reserved1",
	"Reserved2",

	"Maximum soft server limit reached",
	"Invalid connect key",
	"Protocol revision too low",
	"Server name has whitespace",
	"Packet error",
	"User already exists",
	"No such group",
	"Private group",
	"Duplicate id",
	"Loopback denied",
	"Maximum hop count exceeded",
	"Remote users locked out",
	"Timed out",
	"Network error(s)",
	"Manual disconnect",
	"Shutdown/Reboot",
	"Maximum remote user limit reached",
	"Object creation failure",
	"You are banned from this server",
	"Site is banned",
	"Your level is too low",
	"Packet or data overload",
	"Only encrypted links permitted"
	};

// Server stage strings
char *svr_stage_str[]={
	"~FTUNCONNECTED  ",
	"~FYINCOMING     ",
	"~FYCONNECTING   ",
	"~FGCONNECTED    ",
	"~FRCON REFUSED  ",
	"~FRCON FAILED   ",
	"~FRDISCONNECTED ",
	"~FRNOT FOUND    ",
	"~FRTIMED OUT    ",
	"~FRNETWORK ERROR",
	"~FRPKT OVERLOAD ",
	"~FRMANUAL DISCON",
	"~FRSHUTDOWN     ",
	"~FTDELETING     "
	};


/* Print codes to produce colours and other effects when prepended with 
   a '~' and put in a string to be printed. NOTE TO SELF: If this is 
   modified, check for any mods needed in clean_string() and the easter
   egg. */
char *printcode[NUM_PRINT_CODES]={
	"RS","OL","UL","LI","SN",
	"RV","SC","SH","LC","CU",
	"FK","FR","FG","FY",
	"FB","FM","FT","FW",
	"BK","BR","BG","BY",
	"BB","BM","BT","BW",
	"IP","NP","PR"
	};


// Ansi terminal codes
char *ansicode[NUM_PRINT_CODES]={
	// Non-colour codes: 
	// reset, bold, underline, blink, beep (sound)
	"\033[0m", "\033[1m", "\033[4m", "\033[5m","\07",

	// reverse, screen clear, screen home, line clear, cursor up 1 line
	"\033[7m","\033[2J","\033[H\033[J","\033[K","\033[1A",

	// Foreground colours
	// Black, red, green, yellow
	"\033[30m","\033[31m","\033[32m","\033[33m",
	// Blue, magenta, turquoise, white
	"\033[34m","\033[35m","\033[36m","\033[37m",

	// Background colours in same order
	"\033[40m","\033[41m","\033[42m","\033[43m",
	"\033[44m","\033[45m","\033[46m","\033[47m",

	// For print commands that are NUTS internal & have no ansi code 
	"","",""
	};


// User level names
char *user_level[NUM_LEVELS]={
	"LOGIN",
	"NOVICE",
	"USER", 
	"MONITOR",
	"OPERATOR",
	"ADMIN"
	};

// User type names 
char *user_type[]={ "LOCAL", "REMOTE" };

// Misc
char *noyes[] = { "NO", "YES" };
char *colnoyes[] = { "~FGNO~RS", "~FRYES~RS" };
char *rcolnoyes[] = { "~FRNO~RS", "~FGYES~RS" };
char *scolnoyes[] = { " ~FGNO~RS", "~FRYES~RS" };
char *srcolnoyes[] = { " ~FRNO~RS", "~FGYES~RS" };
char *offon[]= { "~FG~OLOFF~RS","~FR~OLON~RS" };

char *group_type[] = {
	"SYSTEM",
	"PUBLIC",
	"USER  "
	};

char *siteban_type[NUM_SITEBAN_TYPES] = {
	"USER",
	"SERVER",
	"ALL"
	};

char *inc_encrypt_polstr[3] = {
	"NEVER",
	"ALWAYS",
	"EITHER"
	};
#else

extern char *svr_stage_str[];
extern char *err_string[];
extern char *printcode[NUM_PRINT_CODES];
extern char *ansicode[NUM_PRINT_CODES];
extern char *user_level[NUM_LEVELS];
extern char *user_type[];
extern char *noyes[];
extern char *colnoyes[];
extern char *scolnoyes[];
extern char *rcolnoyes[];
extern char *srcolnoyes[];
extern char *offon[];
extern char *group_type[];
extern char *siteban_type[];
extern char *inc_encrypt_polstr[3];

#endif


/*** PACKET STRUCTURES.
     These are the packets used in the NIVN (NUTS 4 Network, or Niven for 
     short) protocol. ***/

// Generic packet header. Used for OK , and ERRORs and general array mapping
struct pkt_hdr {
	uint16_t len;
	u_char type;  // Command or reply type. 
	u_char data[1];
	};

// Used for connect requests, replies & general info. Size doesn't
// include name field.
#define PKT_CONNECT_ERROR_SIZE 4
#define PKT_SVR_INFO_SIZE 22

struct pkt_svr_info {
	uint16_t len;
	u_char type;
	u_char error;
	uint32_t connect_key; 
	u_char proto_rev;
	u_char lockout_level;
	u_char svr_version;
	u_char flags_el;  // Flags & encryption level (2 high bits)
	uint16_t local_user_cnt;
	uint16_t remote_user_cnt;
	u_char sys_group_cnt;
	u_char pub_group_cnt;
	u_char svr_links_cnt;
	u_char rem_user_max_level;
	uint16_t ping_interval;
	u_char name[1];
	};


// For user tells, tell errors, join group replies etc. 
#define PKT_GENERIC1_SIZE1 4
#define PKT_GENERIC1_SIZE2 8
#define PKT_GENERIC1_SIZE3 10

struct pkt_generic1 {
	uint16_t len; 
	u_char type;
	u_char erf;  // Error, reason/result or friends tell 
	uint16_t id1; 
	uint16_t id2; 
	u_char namelen; // Required as name_mesg contains name & mesg
	u_char name_mesg[1];
	};

// For sending user input, prints and other stuff. 
#define PKT_GENERIC2_SIZE 6  // Not including data[1] in this

struct pkt_generic2 {
	uint16_t len;
	u_char type;
	u_char add_prefix; // Used in send_print() & recv_print() only
	uint16_t id;
	u_char data[1];
	};

/* For joining (logging on) to a remote group and user info. 
   The home elements are for storing the users home server which gets passed
   along with every hop. This is so remote users who cause trouble can be
   traced. Have a union here since only one or other other will be needed,
   never both at the same time. ip6 not supported at the moment but I'm 
   including it in the structure for when (if) I update the code to support 
   it */
#define PKT_USER_INFO_SIZE 40

struct pkt_user_info {
	uint16_t len;
	u_char type;
	u_char hop_count;
	uint16_t uid;
	uint16_t orig_uid;
	uint16_t gid_ruid; // Only ruid if a ujoin packet
	u_char orig_level;
	u_char term_cols;
	u_char term_rows;
	u_char desclen; // In NIVN 6 and above
	char pad[2];   // For future use
	uint32_t user_flags;

	// This union must be 32 bit aligned!
	union {
		uint32_t ip4;
		u_char ip6[IP6_ADDR_SIZE];
		} home_addr;
	uint16_t home_port;

	u_char namelen;
	u_char name_desc_svr[1];
	};

// For when user has changed terminal size. The uid has to be between the
// term cols and rows for alignment purposes. 
#define PKT_TERMSIZE_SIZE 7

struct pkt_user_termsize {
	uint16_t len;
	u_char type;
	u_char term_cols;
	uint16_t uid;
	u_char term_rows;
	};


/* When a remote user changes flags using toggle command we send new
   flags int all the way back to local server so it updates all servers
   that they're on. */
#define PKT_USER_FLAGS_SIZE 10

struct pkt_user_flags {
	uint16_t len;
	u_char type;
	u_char pad;
	uint32_t flags;
	uint16_t uid;
	};


// For sending mail. 
#define PKT_MAIL_SIZE 10 // Not including data[1] 

struct pkt_mail {
	uint16_t len;
	u_char type;
	u_char flags; // Unused for now.
	uint16_t uid_from;
	uint16_t uid_to;
	u_char namelen;
	u_char subjlen;
	char data[1];
	};


// For pings
#define PKT_PING_SIZE1 3
#define PKT_PING_SIZE2 10

struct pkt_ping {
	uint16_t len;
	u_char type;
	u_char flags;  // Unused for now
	uint32_t send_time;
	uint16_t uid;
	};


/*** CLASS DEFINITIONS ***/

class cl_user;
class cl_local_user;
class cl_group;
class cl_server;
class cl_splitline;
class cl_friend;
class cl_editor;
class cl_mail;
class cl_board;

// Used in tells and group reviews
struct revline {
	char *line;
	int alloc;
	};

// User base class 
class cl_user {
	public:
	uint16_t id; 
	uint32_t name_key;
	char *name;
	char *desc;
	char *ipnumstr;
	char *ipnamestr;
	sockaddr_in ip_addr;
	pthread_t ip_thrd;
	int type;
	int stage;
	int level;
	uint32_t flags;
	uint32_t prev_flags;
	time_t login_time;

	// Input
	time_t last_input_time;
	int input_len;
	int buffnum;
	u_char *tbuff; // Text buffer pointer
	u_char text_buffer[2][ARR_SIZE]; // Text buffers
	cl_splitline *com;

	// Terminal info
	uint16_t term_cols;
	uint16_t term_rows;

	// Groups
	cl_group *group;
	cl_group *home_group;
	cl_group *prev_group;
	cl_group *del_group;
	cl_group *look_group;
	cl_group *mon_group;

	// Muzzle vars
	int muzzle_level;
	time_t muzzle_start_time;
	time_t muzzle_end_time;

	// For modifying passwords
	char *inpwd;
	cl_local_user *new_pwd_user;

	// For page file and command
	int page_pos;
	char *page_filename;
	int page_header_lines;
	int last_com;
	int com_page_line;
	void *com_page_ptr; // To store pointers to objects;
	int current_msg;
	int to_msg;

	// For tell review
	revline *revbuff;
	int revpos;
	int afk_tell_cnt;
	struct {
		cl_group *grp;
		time_t time;
		} invite[MAX_INVITES];

	// Prison vars
	int imprison_level;
	time_t imprisoned_time;
	time_t release_time;

	// Misc
	cl_friend *first_friend,*last_friend;
	cl_server *server_to;
	cl_server *ping_svr;
	cl_editor *editor;
	char *msg_subject;
	char *afk_msg;
	cl_user *exa_user;
	cl_user *del_user;
	int shutdown_secs;
	char *com_filename;
	int eecnt;

	// Command functions stored as pointers
	static void (cl_user::* comfunc[NUM_COMMANDS])();

	cl_user *prev,*next;

	// Methods
	cl_user(int tmp);
	virtual ~cl_user();

	virtual int set_name(char *nme)=0;
	virtual void uprintf(char *fmtstr,...)=0;
	virtual void disconnect()=0;

	void resolve_ip_thread();

	void parse_line();
	int run_afk();
	int page_file(char *filename, int do_page);
	void run_editor();
	void run_cmd_line();
	void run_board_reader();
	void run_new_password();
	void run_examine();
	void run_shutdown();
	void run_reboot();
	void run_delete_group();
	void run_delete_group_log();
	void run_delete_user();
	void run_save_config(int at_prompt);

	int tell(int ttype,uint16_t uid,char *uname,char *svrname,char *msg);

	void infoprintf(char *fmtstr, ...);
	void warnprintf(char *fmtstr, ...);
	void errprintf(char *fmtstr, ...);
	void sysprintf(char *fmtstr, ...);
	void prompt();
	void clear_inline_prompt();
	int set_desc(char *dsc);
	int add_friend(char *idstr, int linenum);
	void look(cl_group *grp, int do_page);
	void show_details(cl_user *u);
	void show_gmi_details(cl_user *u);
	char *flags_list();
	int release_from_prison(cl_user *uby);
	int unmuzzle(cl_user *uby);
	int is_afk();
	void send_broadcast(char *mesg);
	void send_net_broadcast(char *mesg);
	void reset_to_cmd_stage();

	void com_quit();
	void com_toggle();
	void com_say();
	void com_emote();
	void com_pemote();
	void com_semote();
	void shout_semote(int comnum);
	void com_tell();
	void tell_pemote(int comnum);
	void com_ntell();
	void com_nemote();
	void ntell_nemote(int comnum);
	void com_ftell();
	void com_femote();	
	void ftell_femote(int comnum);
	void femote_ftell();
	void com_shout();
	void com_review();
	void com_revtell();
	void page_review(revline *rbuff,int rpos);
	void com_who();
	void com_whois();
	void com_people();
	void com_lsgroups();
	void com_lsfriends();
	void com_friend();
	void com_join();
	void com_ujoin();
	void join_ujoin(cl_group *grp);
	virtual void com_leave()=0;
	void com_help();
	void help_show_commands();
	void help_credits();
	void help_command_or_topic();
	void com_version();
	void com_look();
	void com_lsservers();
	void com_svrinfo();
	void com_server();
	void server_add(int do_connect, int do_encrypt);
	void server_delete();
	void server_connect(int do_encrypt);
	void server_disconnect();
	void server_ping(int continuous);
	virtual void com_profile()=0;
	void com_name();
	void com_desc();
	void com_gname();
	void com_gdesc();
	void com_private();
	void com_public();
	void com_invite();
	void com_ninvite();
	void com_uninvite();
	int invite_ninvite(cl_user *u);
	void com_lsinvites();
	void com_evict();
	void com_gban();
	void com_gunban();
	void com_lsgbans();
	void com_examine();
	virtual void com_mail()=0;
	void com_afk();
	void com_board();
	virtual void com_setemail()=0;
	void com_passwd();
	void com_user();
	void user_create();
	void user_delete();
	void user_set_level();
	void com_level();
	void level_lockout(char *usage, int new_level);
	void level_remusermax(int new_level);
	void level_gatecrash(int new_level);
	void level_invis(int new_level);
	void level_grpmod(int new_level);
	void level_maxuserign(int new_level);
	void level_idleign(int new_level);
	void level_netbcast(int new_level);
	virtual void com_stgroup()=0;
	virtual void com_suicide()=0;
	void com_cls();
	void com_muzzle();
	void com_unmuzzle();
	void com_kill();
	void com_nkill();
	void kill_nkill(cl_user *u);
	void com_shutdown();
	void com_reboot();
	void shutdown_reboot(int comnum);
	virtual void com_batch()=0;
	virtual void com_lsbatches()=0;
	void com_imprison();
	void com_release();
	void com_lsthreads();
	void com_sysinfo();
	void com_fix();
	void com_unfix();
	void com_group();
	void com_lsprison();
	void com_ban();
	void ban_user();
	void ban_site(int sbtype);
	void com_unban();
	void unban_user(int num);
	void unban_site(int num);
	void com_lsubans();
	void com_lssbans();
	void com_savecfg();
	void com_wake();
	void com_move();
	void com_bcast();
	void com_admshout();
	void com_viewlog();
	void com_revclr();
	void com_news();
	void com_systoggle();
	void com_netbcast();
	void com_lsip();
	void com_think();
	void com_syset();
	};


class cl_local_user: public cl_user {
	public:
	int error;
	u_char buff[ARR_SIZE];  // Main input buffer
	char inline_prompt[INLINE_PROMPT_SIZE];
	char *pwd;
	int sock;
	int bpos;
	int tbpos;
	int login_attempts;
	int login_errors;
	int linenum;
	time_t logoff_time;
	time_t session_duration; 
	time_t linkdead_time;
	char *term_type;
	char *email_address;
	uint16_t start_group_id;
	char *prev_ip_addr;

	// For batch command execution
	char *batch_name;
	FILE *batchfp;
	int batch_type;
	int batch_line;
	int batch_max_lines;
	int batch_create_type;

	// For mailing
	cl_mail *mail;
	char *mail_to_idstr;
	uint16_t mail_to_id;
	cl_server *mail_to_svr;

	// Data rate checking
	u_int rx_period_data;
	uint32_t rx_period_data_start;

	cl_local_user(int tmp);
	~cl_local_user();

	void uprintf(char *fmtstr,...);
	void uread();
	int uwritestr(char *str);
	int uwrite(char *str,int len);

	int parse_telopt();
	int get_termsize();
	int get_termtype();
	void login();
	void inc_attempts();
	void inc_errors();
	void complete_login();
	void uconnect();
	void disconnect();
	void do_login_messages();

	int load(int load_stage);
	int save();
	int save_text_file(int ftype, char *str);

	void run_mailer();
	void send_mail(
		char *idstr, char *subj, int use_editor, char *inc_path);
	int send_system_mail(char *subj, char *mesg);

	void run_batch(int btype);
	void run_suicide();
	void run_delete_batch();
	void run_create_batch(int at_prompt, char *path);

	int set_name(char *nme);

	void com_leave();
	void com_profile();
	void com_mail();
	void com_setemail();
	void com_passwd();
	void com_stgroup();
	void com_suicide();
	void com_batch();
	void com_lsbatches();
	};


// Remote user 
class cl_remote_user: public cl_user {
	public:
	int error;
	uint16_t remote_id;
	uint16_t orig_id;
	char *full_name;
	char *full_desc;
	cl_server *server_from;
	char *home_svrname;
	u_char hop_count;
	int orig_level;

	cl_remote_user(uint16_t uid,cl_server *svr,pkt_user_info *pkt);
	~cl_remote_user();

	int uread();
	void uprintf(char *fmtstr,...);

	int set_name(char *nme);
	int set_name(char *nme, int nlen);
	int set_desc(char *dsc, int dlen);
	int set_svrname(char *snme, int snlen);

	void disconnect();

	void com_leave();
	void com_profile();
	void com_mail();
	void com_setemail();
	void com_stgroup();
	void com_suicide();
	void com_batch();
	void com_lsbatches();
	};



// User friend
class cl_friend {
	public:
	uint16_t id;
	int utype;
	int stage;

	// Local fields
	cl_user *local_user;

	// Remote fields
	char *name;
	char *desc;
	cl_server *server;
	char *svr_name;
	uint16_t remote_gid;
	uint32_t home_ip4_addr;
	uint16_t home_port;

	cl_friend *prev,*next;

	cl_friend(cl_user *owner, uint16_t fid, char *svrname, int linenum);
	~cl_friend();

	int set_info(cl_server *svr, pkt_user_info *pkt);
	};
	

// Editor class
class cl_editor {
	public:
	cl_user *owner;
	int error;
	int type;
	int stage;
	int isbatch;
	char *buff;
	int bpos;
	int line;
	int maxchars;
	int maxlines;
	int malloced;
	char *inc_path;
	int inc_size;
	char *inc_text;

	cl_editor(cl_user *own,int typ, char *path);
	~cl_editor();
	int run();
	int append(char *str);
	int include_text();
	void print_buffer();
	};


// Used in mail & board classes
class cl_msginfo {
	public:
	int mnum;
	char *id;
	char *name;
	char *subject;
	char *filename;
	time_t create_time;
	int size;
	int read;
	cl_splitline *sl;
	cl_msginfo *prev,*next;

	cl_msginfo();
	~cl_msginfo();
	void set(int mn, cl_splitline *split);
	int set(int mn,char *idstr,char *nme,char *subj,char *fname,int sz);
	};


// Mail class
class cl_mail {
	public:
	cl_user *owner;
	uint16_t uid;
	int error;
	int msgcnt;
	int todays_msgcnt;
	int unread_msgcnt;
	cl_msginfo *first_msg,*last_msg;

	cl_mail(cl_user *own,uint16_t id);
	~cl_mail();
	
	void list();
	int mread(cl_msginfo *minfo);
	int mwrite(
		uint16_t uid_from,
		char *name_from, cl_server *svr, char *subj, char *msg);
	int mdelete(cl_msginfo *minfo);
	int save();
	void renumber();
	};


// Class for splitting config and user input lines into words
class cl_splitline {
	public:
	char **word;
	char **wordptr;
	int is_config_line;
	int wcnt;
	int start_quote;
	int end_quote;

	cl_splitline(int icl);
	~cl_splitline();

	int set(cl_splitline *sl);
	void reset();
	int parse(char *line);
	char *get_word(char **ptr);
	void add_word(char *str);
	void shift();
	};



// Server class 
class cl_server {
	public:
	int create_error;
	int connection_type;
	pthread_t con_thrd;
	pthread_t ip_thrd;
	sockaddr_in ip_addr;
	sockaddr_in local_ip_addr;
	uint16_t local_port; // Only used for OUTGOING connections
	cl_user *user;  // Manually connected or disconnected
	u_char flags; 

	u_char id;
	char name[MAX_SERVER_NAME_LEN+1];

	// Addresses & keys 
	char *ipstr;
	char *ipnumstr;
	char *ipnamestr;
	uint32_t connect_key;
	char connect_key_str[11];
	int connect_key_len;

	// Socket & packet stuff
	int sock;
	int stage;
	u_char start_encryption_level;
	u_char encryption_level;
	u_char enprv;
	u_char decprv;
	u_char enkpos;
	u_char deckpos;
	u_int rx_packets;
	u_int tx_packets;
	u_int rx_period_packets;
	uint32_t rx_period_pkt_start; // Down to 1/100ths of a second
	u_int rx_period_data;
	uint32_t rx_period_data_start;
	
	// This must follow an int in the class definition since it MUST be
	// word aligned as packet structures are mapped onto it
	u_char buff[SERVER_BUFF_SIZE]; 
	int bpos;
	time_t connect_time;
	time_t last_rx_time;
	time_t last_tx_time;
	time_t last_ping_time;
	
	// Version and protocol revision of remote server
	char version[10];
	u_char proto_rev;

	u_char sys_group_cnt;
	u_char pub_group_cnt;
	u_char svr_links_cnt;
	int local_user_cnt;
	int remote_user_cnt;
	int svr_lockout_level;
	int svr_rem_user_max_level;
	uint16_t ping_interval;

	int rx_err_cnt;
	int tx_err_cnt;


	// Packet receive functions stored as pointers
	static int (cl_server::* pkt_recv_func[NUM_PKT_TYPES])();

	cl_server *prev,*next;

	// Methods
	cl_server();
	cl_server(
		char *nme,
		char *ipaddr,
		uint16_t port,
		uint16_t loc_port,
		uint32_t conkey,
		u_char enctype,
		cl_user *u);
	void init();
	void reset();
	~cl_server();

	int set_id();
	void set_name(char *nme);
	void set_name(char *nme,int nlen);
	void sread();
	int swrite(u_char *pkt, int len);
	int encrypt_packet(u_char *pkt, uint16_t len);
	int decrypt_packet(u_char *pkt, uint16_t len);
	void hexdump_packet(int recv, u_char *pkt, int len);
	void disconnect(int stg);
	void delete_remotes();
	void set_friends_to_unknown();
	void send_friends_requests();
	cl_remote_user *svr_get_remote_user(uint16_t uid);

	int spawn_thread(int thread_type);
	void resolve_ip_thread();
	void connect_thread();

	int send_uid(u_char ptype, uint16_t uid);
	int send_id_pair(u_char ptype, u_char erf, uint16_t id1, uint16_t id2);
	int send_user_info_packet(u_char ptype, cl_user *u, uint16_t ngid);
	int send_mesg(
		u_char ptype,
		u_char ftell,
		uint16_t uid_from, uint16_t uid_to, char *nme, char *msg);

	int send_svr_info(u_char ptype);
	int recv_svr_info();

	int send_connect_error(u_char error);
	int recv_connect();
	int recv_connect_reply();
	void set_and_log_connect_info(pkt_svr_info *pkt);

	int send_disconnecting(u_char reason);
	int recv_disconnecting();

	int send_ping(cl_user *u);
	int recv_ping();
	int recv_ping_reply();

	int recv_tell();

	int send_join(cl_user *u, uint16_t gid);
	int send_ujoin(cl_user *u, uint16_t uid);
	int recv_join();
	int send_join_reply(u_char error, uint16_t nuid, uint16_t ngid);
	int recv_join_reply();

	int send_leave(uint16_t uid);
	int recv_leave();

	int send_left(uint16_t uid);
	int recv_left();

	int send_input(uint16_t uid, char *str);
	int recv_input();

	int send_print(uint16_t ruid, u_char add_prefix, char *str);
	int svrprintf(
		int throw_on_error,
		uint16_t ruid, u_char add_prefix, char *fmtstr, ...);
	int recv_print();

	int send_find_user(uint16_t requesting_uid, uint16_t search_uid);
	int recv_find_user();
	int send_find_user_reply(
		u_char res,uint16_t requesting_uid, uint16_t search_uid);
	int recv_find_user_reply();

	int send_logon_notify(uint16_t uid);
	int recv_logon_notify();
	int send_logoff_notify(uint16_t uid);
	int recv_logoff_notify();

	int send_req_user_info(uint16_t uid);
	int recv_req_user_info();
	int send_user_info(cl_user *u);
	int recv_user_info();

	int send_termsize(cl_user *u);
	int recv_termsize();

	int send_user_flags(cl_user *u);
	int recv_user_flags();

	int send_group_change(cl_user *u);
	int recv_group_change();

	int send_examine(uint16_t requesting_uid, uint16_t exa_uid);
	int recv_examine();
	void show_gmi_details(cl_user *u, uint16_t req_uid);
	int send_examine_reply(uint16_t requesting_uid, cl_user *u);

	int send_mail(cl_user *ufrom, uint16_t uid_to, char *subj, char *msg);
	int recv_mail();
	int send_mail_reply(u_char error, uint16_t uid_from, uint16_t uid_to);
	int recv_mail_reply();

	int recv_net_bcast();
	};


// User ban structure. 
struct st_user_ban {
	int level;
	uint16_t uid;  // Will be orig_id if remote user
	int utype; // User type
	cl_user *user;  // Set whilst user logged on
	sockaddr_in home_addr; // Set if remote user
	st_user_ban *prev, *next;
	};

// Group class
class cl_group {
	public:
	uint16_t id;
	char *name;
	int type;
	int error;

	int ucnt;
	cl_local_user *owner;
	cl_group *prev,*next;
	u_char flags;
	revline *revbuff;
	int revpos;
	cl_board *board;
	st_user_ban *first_ban,*last_ban;
	char glogfile[MAXPATHLEN];

	cl_group(uint16_t gid, cl_local_user *own);
	cl_group(uint16_t gid, char *nme, int typ, cl_local_user *own);
	void init(uint16_t gid, int typ, cl_local_user *own);
	virtual ~cl_group();

	int set_name(char *nme);
	int set_private();
	int set_public();
	int set_fixed();
	int set_unfixed();
	int save();
	int save_desc(char *desc);
	void join(cl_user *u, cl_server *server=NULL, uint16_t remgid=0);
	void leave(cl_user *u);
	void gprintf(int mtype, char *fmtstr,...);
	void geprintf(int mtype, cl_user *u1, cl_user *u2, char *fmtstr,...);
	int speak(int comnum, cl_user *u, char *txt);
	int user_can_modify(cl_user *u);
	int user_can_join(cl_user *u, int *inv);
	void ban(int ban_level, uint16_t uid, uint32_t addr, uint16_t port);
	int ban(int ban_level, cl_user *u);
	int user_is_banned(cl_user *u);
	int user_is_banned(pkt_user_info *pkt);
	void evict(cl_user *evictor, cl_user *evictee);
	void grouplog(bool force, char *str);
	};



// Message boards
class cl_board {
	public:
	cl_group *group;
	int msgcnt;
	int todays_msgcnt;
	int write_level;
	int admin_level;
	int expire_secs;
	cl_msginfo *first_msg,*last_msg;

	enum { ADMIN_LEVEL, WRITE_LEVEL };

	cl_board(cl_group *grp);
	~cl_board();

	void list(cl_user *u);
	int mread(cl_user *u, cl_msginfo *minfo);
	int mwrite(cl_user *u, char *subj, char *msg);
	int mdelete(cl_msginfo *minfo);
	int save();
	void renumber();
	int set_level(int levtype, int lev);
	int set_expire(int secs);
	int user_can_modify(cl_user *user);
	int user_can_post(cl_user *user);
	};


/*** MISC STRUCTURES ***/

// Site ban
struct st_site_ban {
	int type; // user, server or both
	int level;
	sockaddr_in addr;
	st_site_ban *prev,*next;
	};

// Threads list
struct st_threadentry {
	int type;
	pthread_t id;
	time_t created;
	st_threadentry *prev,*next;
	};

// Signal list
struct st_siglist {
	char *name;
	int num;
	int ignoring;
	};

/*** COMMAND ARRAYS ***/

#ifdef MAINFILE

// NOTE TO SELF: If commands are changed check command_enum too!!
void (cl_user::* cl_user::comfunc[NUM_COMMANDS])() = {
	// 0
	&cl_user::com_quit,
	&cl_user::com_toggle,
	&cl_user::com_say,
	&cl_user::com_emote,
	&cl_user::com_pemote,

	// 5
	&cl_user::com_semote,
	&cl_user::com_tell,
	&cl_user::com_ntell,
	&cl_user::com_nemote,
	&cl_user::com_ftell,

	// 10
	&cl_user::com_femote,
	&cl_user::com_shout,
	&cl_user::com_review,
	&cl_user::com_revtell,
	&cl_user::com_who,

	// 15
	&cl_user::com_whois,
	&cl_user::com_people,
	&cl_user::com_lsgroups,
	&cl_user::com_lsfriends,
	&cl_user::com_friend,

	// 20
	&cl_user::com_join,
	&cl_user::com_ujoin,
	&cl_user::com_leave,
	&cl_user::com_help,
	&cl_user::com_version,

	// 25
	&cl_user::com_look,
	&cl_user::com_lsservers,
	&cl_user::com_svrinfo,
	&cl_user::com_server,
	&cl_user::com_profile,

	// 30
	&cl_user::com_name,
	&cl_user::com_desc,
	&cl_user::com_examine,
	&cl_user::com_mail,
	&cl_user::com_gname,

	// 35
	&cl_user::com_gdesc,
	&cl_user::com_private,
	&cl_user::com_public,
	&cl_user::com_invite,
	&cl_user::com_ninvite,

	// 40
	&cl_user::com_uninvite,
	&cl_user::com_lsinvites,
	&cl_user::com_evict,
	&cl_user::com_gban,
	&cl_user::com_gunban,

	// 45
	&cl_user::com_lsgbans,
	&cl_user::com_afk,
	&cl_user::com_board,
	&cl_user::com_setemail,
	&cl_user::com_passwd,

	// 50
	&cl_user::com_user,
	&cl_user::com_level,
	&cl_user::com_stgroup,
	&cl_user::com_suicide,
	&cl_user::com_cls,

	// 55
	&cl_user::com_muzzle,
	&cl_user::com_unmuzzle,
	&cl_user::com_kill,
	&cl_user::com_nkill,
	&cl_user::com_shutdown,

	// 60
	&cl_user::com_reboot,
	&cl_user::com_batch,
	&cl_user::com_lsbatches,
	&cl_user::com_imprison,
	&cl_user::com_release,

	// 65
	&cl_user::com_lsthreads,
	&cl_user::com_sysinfo,
	&cl_user::com_fix,
	&cl_user::com_unfix,
	&cl_user::com_group,

	// 70
	&cl_user::com_lsprison,
	&cl_user::com_ban,
	&cl_user::com_unban,
	&cl_user::com_lsubans,
	&cl_user::com_lssbans,

	// 75
	&cl_user::com_savecfg,
	&cl_user::com_wake,
	&cl_user::com_move,
	&cl_user::com_bcast,
	&cl_user::com_admshout,

	// 80
	&cl_user::com_viewlog,
	&cl_user::com_revclr,
	&cl_user::com_news,
	&cl_user::com_systoggle,
	&cl_user::com_netbcast,

	// 85
	&cl_user::com_lsip,
	&cl_user::com_think,
	&cl_user::com_syset
	};

char *command[NUM_COMMANDS]={
	// 0
	"quit",
	"toggle",
	"say",
	"emote",
	"pemote",

	// 5
	"semote",
	"tell",
	"ntell",
	"nemote",
	"ftell",

	// 10
	"femote",
	"shout",
	"review",
	"revtell",
	"who",

	// 15
	"whois",
	"people",
	"lsgroups",
	"lsfriends",
	"friend",

	// 20 
	"join",
	"ujoin",
	"leave",
	"help",
	"version",

	// 25
	"look",
	"lsservers",
	"svrinfo",
	"server",
	"profile",

	// 30
	"name",
	"desc",
	"examine",
	"mail",
	"gname",

	// 35
	"gdesc",
	"private",
	"public",
	"invite",
	"ninvite",

	// 40
	"uninvite",
	"lsinvites",
	"evict",
	"gban",
	"gunban",

	// 45
	"lsgbans",
	"afk",
	"board",
	"setemail",
	"passwd",

	// 50
	"user",
	"level",
	"stgroup",
	"suicide",
	"cls",

	// 55
	"muzzle",
	"unmuzzle",
	"kill",
	"nkill",
	"shutdown",

	// 60
	"reboot",
	"batch",
	"lsbatches",
	"imprison",
	"release",

	// 65
	"lsthreads",
	"sysinfo",
	"fix",
	"unfix",
	"group",

	// 70
	"lsprison",
	"ban",
	"unban",
	"lsubans",
	"lssbans",

	// 75
	"savecfg",
	"wake",
	"move",
	"bcast",
	"admshout",

	// 80
	"viewlog",
	"revclr",
	"news",
	"systoggle",
	"netbcast",

	// 85
	"lsip",
	"think",
	"syset"
	};

int command_level[NUM_COMMANDS]={
	// 0
	USER_LEVEL_NOVICE,
	USER_LEVEL_NOVICE,
	USER_LEVEL_NOVICE,
	USER_LEVEL_USER,
	USER_LEVEL_USER,

	// 5
	USER_LEVEL_USER,
	USER_LEVEL_USER,
	USER_LEVEL_USER,
	USER_LEVEL_USER,
	USER_LEVEL_USER,

	// 10
	USER_LEVEL_USER,
	USER_LEVEL_USER,
	USER_LEVEL_NOVICE,
	USER_LEVEL_USER,
	USER_LEVEL_NOVICE,

	// 15
	USER_LEVEL_USER,
	USER_LEVEL_MONITOR,
	USER_LEVEL_NOVICE,
	USER_LEVEL_USER,
	USER_LEVEL_USER,

	// 20 
	USER_LEVEL_NOVICE,
	USER_LEVEL_USER,
	USER_LEVEL_NOVICE,
	USER_LEVEL_NOVICE,
	USER_LEVEL_NOVICE,

	// 25
	USER_LEVEL_NOVICE,
	USER_LEVEL_NOVICE,
	USER_LEVEL_OPERATOR,
	USER_LEVEL_ADMIN,
	USER_LEVEL_USER,

	// 30
	USER_LEVEL_NOVICE,
	USER_LEVEL_USER,
	USER_LEVEL_NOVICE,
	USER_LEVEL_USER,
	USER_LEVEL_USER,

	// 35
	USER_LEVEL_USER,
	USER_LEVEL_USER,
	USER_LEVEL_USER,
	USER_LEVEL_USER,
	USER_LEVEL_USER,

	// 40
	USER_LEVEL_USER,
	USER_LEVEL_NOVICE,
	USER_LEVEL_USER,
	USER_LEVEL_USER,
	USER_LEVEL_USER,

	// 45
	USER_LEVEL_USER,
	USER_LEVEL_NOVICE,
	USER_LEVEL_USER,
	USER_LEVEL_NOVICE,
	USER_LEVEL_NOVICE,

	// 50
	USER_LEVEL_OPERATOR,
	USER_LEVEL_ADMIN,
	USER_LEVEL_USER,
	USER_LEVEL_NOVICE,
	USER_LEVEL_NOVICE,

	// 55
	USER_LEVEL_MONITOR,
	USER_LEVEL_MONITOR,
	USER_LEVEL_OPERATOR,
	USER_LEVEL_OPERATOR,
	USER_LEVEL_ADMIN,

	// 60
	USER_LEVEL_ADMIN,
	USER_LEVEL_USER,
	USER_LEVEL_USER,
	USER_LEVEL_MONITOR,
	USER_LEVEL_MONITOR,

	// 65
	USER_LEVEL_ADMIN,
	USER_LEVEL_MONITOR,
	USER_LEVEL_USER,
	USER_LEVEL_USER,
	USER_LEVEL_OPERATOR,

	// 70
	USER_LEVEL_MONITOR,
	USER_LEVEL_OPERATOR,
	USER_LEVEL_OPERATOR,
	USER_LEVEL_MONITOR,
	USER_LEVEL_MONITOR,

	// 75
	USER_LEVEL_ADMIN,
	USER_LEVEL_MONITOR,
	USER_LEVEL_OPERATOR,
	USER_LEVEL_OPERATOR,
	USER_LEVEL_MONITOR,

	// 80
	USER_LEVEL_OPERATOR,
	USER_LEVEL_USER,
	USER_LEVEL_NOVICE,
	USER_LEVEL_ADMIN,
	USER_LEVEL_ADMIN,

	// 85
	USER_LEVEL_MONITOR,
	USER_LEVEL_USER,
	USER_LEVEL_ADMIN
	};

int (cl_server::* cl_server::pkt_recv_func[NUM_PKT_TYPES])() = {
	// 0
	&cl_server::recv_connect,	
	&cl_server::recv_ping,
	&cl_server::recv_tell,
	&cl_server::recv_tell,
	&cl_server::recv_join,

	// 5
	&cl_server::recv_input,
	&cl_server::recv_print,
	&cl_server::recv_find_user,
	&cl_server::recv_req_user_info,
	&cl_server::recv_examine,

	// 10
	&cl_server::recv_mail,
	&cl_server::recv_join,
	&cl_server::recv_connect_reply,
	&cl_server::recv_ping_reply,
	&cl_server::recv_join_reply,

	// 15
	&cl_server::recv_find_user_reply,
	&cl_server::recv_mail_reply,
	&cl_server::recv_svr_info,
	&cl_server::recv_leave,
	&cl_server::recv_left,

	// 20
	&cl_server::recv_user_info,
	&cl_server::recv_termsize,
	&cl_server::recv_user_flags,
	&cl_server::recv_logon_notify,
	&cl_server::recv_logoff_notify,

	// 25
	&cl_server::recv_group_change,
	&cl_server::recv_disconnecting,
	&cl_server::recv_net_bcast
	};


/*** Signals list. This isn't comprehensive but just what I think are
     the important ones. ***/
struct st_siglist siglist[NUM_SIGNALS] = {
	{ "HUP",  SIGHUP, 0 },
	{ "INT",  SIGINT, 0 },
	{ "ILL",  SIGILL, 0 },
	{ "QUIT", SIGQUIT, 0 },
	{ "TSTP", SIGTSTP, 0 },
	{ "TERM", SIGTERM, 0 },
	{ "CHLD", SIGCHLD, 0 },
	{ "TSTP", SIGTSTP, 0 },
	{ "TTIN", SIGTTIN, 0 },
	{ "TTOU", SIGTTOU, 0 }
	};

#else
extern char *command[NUM_COMMANDS];
extern int command_level[NUM_COMMANDS];
extern struct st_siglist siglist[NUM_SIGNALS];
#endif



/*** GLOBAL VARIABLES ***/
EXTERN u_char svr_version;  // Numeric version of version string for packets
EXTERN char *config_file;
EXTERN char *logfile;
EXTERN char *original_dir;
EXTERN char *working_dir;
EXTERN uint32_t system_flags;
EXTERN int be_daemon;
EXTERN pid_t main_pid;
EXTERN struct sockaddr_in main_bind_addr;
EXTERN char **global_argv;
EXTERN char build[20];
EXTERN time_t boot_time;
EXTERN int booting;
EXTERN cl_user *first_user,*last_user;
EXTERN st_user_ban *first_user_ban,*last_user_ban;
EXTERN st_site_ban *first_site_ban,*last_site_ban;
EXTERN cl_group *first_group,*last_group;
EXTERN cl_group *gone_remote_group;
EXTERN cl_group *remotes_home_group;
EXTERN cl_group *prison_group;
EXTERN cl_server *first_server,*last_server;
EXTERN int system_group_count;
EXTERN int public_group_count;
EXTERN int user_group_count;
EXTERN st_threadentry *first_thread,*last_thread;
EXTERN int thread_count;
EXTERN uint32_t max_packet_rate;
EXTERN uint32_t max_local_user_data_rate;
EXTERN uint32_t max_svr_data_rate;
EXTERN int max_local_users;
EXTERN int max_remote_users;
EXTERN int max_user_ign_level;
EXTERN int tcp_port[2];
EXTERN int listen_sock[2];
EXTERN uint32_t local_connect_key;
EXTERN char text[ARR_SIZE]; 
EXTERN char server_name[MAX_SERVER_NAME_LEN];
EXTERN int server_count;
EXTERN int connected_server_count;
EXTERN time_t server_time;
EXTERN struct tm server_time_tms;
EXTERN time_t shutdown_time;
EXTERN int shutdown_type;
EXTERN cl_splitline *ansi_terms;
EXTERN int max_name_len;
EXTERN int max_desc_len;
EXTERN int min_pwd_len;
EXTERN uint16_t send_ping_interval;
EXTERN int server_timeout;
EXTERN int connect_timeout;
EXTERN uint16_t local_user_count;
EXTERN uint16_t remote_user_count;
EXTERN int max_hop_cnt;
EXTERN int linkdead_timeout;
EXTERN int login_timeout;
EXTERN int idle_timeout;
EXTERN int idle_timeout_ign_level;
EXTERN int max_tx_errors;
EXTERN int max_rx_errors;
EXTERN int max_profile_chars;
EXTERN int max_mail_chars;
EXTERN int max_board_chars;
EXTERN int max_group_desc_chars;
EXTERN int max_broadcast_chars;
EXTERN int max_subject_len;
EXTERN int max_group_name_len;
EXTERN int max_batch_name_len;
EXTERN char *default_desc;
EXTERN char *default_pwd;
EXTERN int lockout_level;
EXTERN int group_modify_level;
EXTERN int group_gatecrash_level;
EXTERN int group_invite_expire;
EXTERN int go_invis_level;
EXTERN int remote_user_max_level;
EXTERN int num_review_lines;
EXTERN int board_msg_expire;
EXTERN int max_login_batch_lines;
EXTERN int max_logout_batch_lines;
EXTERN int max_session_batch_lines;
EXTERN int max_include_lines;
EXTERN int autosave_interval;
EXTERN time_t autosave_time;
EXTERN int recv_net_bcast_level;
EXTERN int incoming_encryption_policy;
EXTERN int soft_max_servers;

EXTERN pthread_mutex_t events_mutex;
EXTERN pthread_mutex_t threadlist_mutex;
EXTERN pthread_mutex_t log_mutex;


/*** FORWARD DECLARATIONS ***/
int create_thread(
	int type,pthread_t *tid,void *((*func)(void *)),void *object);
void add_thread_entry(int type, pthread_t tid);
void exit_thread(pthread_t *tid);
void cancel_thread(pthread_t *tid);
void remove_thread_from_list(pthread_t tid);
void *user_resolve_thread_entry(void *user);
void *server_resolve_thread_entry(void *svr);
void *server_connect_thread_entry(void *svr);
void *do_events(void *arg);

int create_listen_socket(int port_type);
int deliver_mail_from_local(
	cl_user *ufrom,
	uint16_t uid_to, cl_server *svrto, char *subj, char *msg);
int deliver_mail(
	uint16_t uid_from, 
	char *uname, cl_server *svrfrom, uint16_t uid_to, char *subj,char *msg);

void log(int timestamp, char *fmtstr, ...);
void allprintf(int mtype, int level, cl_user *uign, char *fmtstr, ...);
char *trim(char *str);
int is_integer(char *str);
char *crypt_str(char *pwd);
uint16_t idstr_to_id(char *idstr);
int idstr_to_id_and_svr(char *idstr, uint16_t *id, cl_server **svr);
cl_user *get_user(uint16_t id, int check_logins=0);
cl_user *get_user_by_name(char *name, int start_from);
cl_user *get_remote_user(uint16_t orig_id, uint32_t addr, uint16_t port);
int get_level(char *levstr);
cl_group *get_group(uint16_t id);
cl_server *get_server(char *name);
int has_whitespace(char *s);
void send_server_info_to_servers(cl_server *svr_dont_send);
void send_user_info_to_servers(cl_user *u);
int has_printcode(char *str);
void clean_string(char *str);
int add_review_line(revline *revbuff,int *revpos,char *str);
cl_msginfo *get_message(cl_msginfo *first_msg, int num);
char *time_period(int secs);
int memory_map(char *path, char **ptr, int *size);
cl_local_user *create_temp_user(uint16_t uid);
int get_seconds(char *numstr, char *timeunit);
int user_is_banned(uint16_t uid, uint32_t addr, uint16_t port);
int site_is_banned(int sbtype, uint32_t addr);
int valid_filename(char *filename);
void dissociate_from_tty();
int write_ban_file();
uint32_t generate_key(char *str);
int match_pattern(char *str, char *pat);
int delete_account(uint16_t id);
void delete_dir(char *dirname);
uint16_t generate_id();
char *get_ipnamestr(uint32_t ipnum, void *obj);
uint32_t get_hsec_time();
