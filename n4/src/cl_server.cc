/***************************************************************************
 FILE: cl_server.cc
 LVU : 1.4.0

 DESC:
 This contains all the code that implements the NIVN (NUTS 4 Network or Niven
 for short) protocol and maintains the list of connected and unconnected 
 servers which is stored as a global linked list.

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


/*** Constructor for incoming connections ***/
cl_server::cl_server()
{
int size,ret,do_res;

init();
connection_type = SERVER_TYPE_INCOMING;
stage = SERVER_STAGE_INCOMING;
strcpy(name,"<incoming>");
con_thrd = 0;

size = sizeof(sockaddr_in);
#ifdef SOLARIS
if ((sock=accept(
	listen_sock[PORT_TYPE_SERVER],(sockaddr *)&ip_addr,&size)) == -1) {
#else
if ((sock=accept(
	listen_sock[PORT_TYPE_SERVER],
	(sockaddr *)&ip_addr,(socklen_t *)&size)) == -1) {
#endif
	log(1,"ERROR: cl_server::cl_server(): accept(): %s",strerror(errno));
	delete this;
	return;
	}
ipstr = strdup((char *)inet_ntoa(ip_addr.sin_addr));
ipnumstr = strdup(ipstr);

if ((ipnamestr = get_ipnamestr(ip_addr.sin_addr.s_addr,this))) {
	log(1,"Server address %s = %s",ipnumstr,ipnamestr);
	do_res = 0;
	}
else {
	ipnamestr = strdup(UNRESOLVED_STR);
	do_res = 1;
	}

connect_key = 0;

// Get local port and interface address
#ifdef SOLARIS
if (getsockname(sock,(sockaddr *)&local_ip_addr,&size))
#else
if (getsockname(sock,(sockaddr *)&local_ip_addr,(socklen_t *)&size))
#endif
	log(1,"ERROR: cl_server::cl_server(): getsockname(): %s",strerror(errno));

log(1,"Server connect from %s",ipnumstr);

server_count++;
add_list_item(first_server,last_server,this);

// Do this here so everything is set up to reply to remote server and
// tell it why we can't connect if can't get id
if ((ret = set_id()) != OK) {
	log(1,"REFUSING connect: %s.",err_string[ret]);
	send_connect_error(ret);
	stage = SERVER_STAGE_DELETE;
	return;
	}

// Check if site is banned
if (site_is_banned(SITEBAN_TYPE_SERVER,ip_addr.sin_addr.s_addr)) {
	log(1,"REFUSING connect: Site banned.");
	send_connect_error(ERR_SITE_BANNED);
	stage = SERVER_STAGE_DELETE;
	return;
	}

if (do_res) spawn_thread(THREAD_TYPE_SVR_RESOLVE);
}




/*** Constructor for outgoing connections. We don't spawn a connect thread here
     since if this is created during the config load we could get race
     conditions.  ***/
cl_server::cl_server(
	char *nme,
	char *ipaddr,
	uint16_t port,
	uint16_t loc_port,
	uint32_t conkey,
	u_char enclev,
	cl_user *u)
{
// Initialise before checks so that destructor won't crash if checks fail
init();
connection_type = SERVER_TYPE_OUTGOING;
stage = SERVER_STAGE_UNCONNECTED;
set_name(nme);
ipstr = strdup(ipaddr);
ipnumstr = strdup(UNRESOLVED_STR);
ipnamestr = strdup(UNRESOLVED_STR);
local_port = loc_port;
connect_key = conkey;
sprintf(connect_key_str,"%u",connect_key);
connect_key_len = strlen(connect_key_str);
flags = 0;
start_encryption_level = encryption_level = enclev;
user = u;
sock = -1;

server_count++;

// Partially set up sockaddr_in struct. Name resolution to set up 
// ip_addr.sin_addr.s_addr gets done in thread so as not to hang process
ip_addr.sin_family=AF_INET;
ip_addr.sin_port=htons(port);

// Now do checks
if (!nme || !nme[0]) {
	create_error = ERR_NAME_NOT_SET;  return;
	}
if (strlen(nme) > MAX_SERVER_NAME_LEN) {
	create_error = ERR_NAME_TOO_LONG;  return;
	}
if (has_whitespace(nme)) {
	create_error = ERR_WHITESPACE;  return;
	}
if (!strcasecmp(nme,server_name) || get_server(nme)) {
	create_error = ERR_DUPLICATE_NAME;  return;
	}
if (!ipaddr || !ipaddr[0]) {
	create_error = ERR_INVALID_ADDRESS;  return;
	}
if ((create_error = set_id()) == OK)
	// Add last or we'll get a duplicate name error
	add_list_item(first_server,last_server,this);
}




/*** Initialise some stuff ***/
void cl_server::init()
{
start_encryption_level = 0;
create_error = OK;
flags = 0;
ip_thrd = 0;
strcpy(version,"<unknown>");
connect_key_str[0] = '\0';
connect_key_len = 0;
local_port = 0;
proto_rev = 0;
svr_links_cnt = 0;
sys_group_cnt = 0;
pub_group_cnt = 0;
local_user_cnt = 0;
remote_user_cnt = 0;
last_tx_time = server_time;
last_ping_time = server_time;
ping_interval = 0;
svr_lockout_level = USER_LEVEL_NOVICE;
svr_rem_user_max_level = USER_LEVEL_USER;
user = NULL;
bzero((char *)&ip_addr,sizeof(ip_addr));
bzero((char *)&local_ip_addr,sizeof(local_ip_addr));

reset();

prev = NULL;
next = NULL;
}




/*** Variables that need to be reset if we're reconnecting to a server ***/
void cl_server::reset()
{
flags = 0;
encryption_level = start_encryption_level;
bpos = 0;
rx_packets = 0;
tx_packets = 0;
rx_err_cnt = 0;
tx_err_cnt = 0;
rx_period_packets = 0;
rx_period_pkt_start = get_hsec_time();
rx_period_data = 0;
rx_period_data_start = get_hsec_time();
enprv = 0;
decprv = 0;
enkpos = 0;
deckpos = 0;
connect_time = 0;
last_rx_time = server_time;
}




/*** Destructor ***/
cl_server::~cl_server()
{
cl_user *u;

cancel_thread(&ip_thrd);
cancel_thread(&con_thrd);
delete_remotes();
set_friends_to_unknown();

FOR_ALL_USERS(u) if (u->ping_svr == this) u->ping_svr = NULL;

FREE(ipstr);
FREE(ipnumstr);
FREE(ipnamestr);
if (sock != -1) close(sock);

server_count--;

remove_list_item(first_server,last_server,this);
}



//////////////////////////////// MISC FUNCTIONS ////////////////////////////////

/*** Get a free id ***/
int cl_server::set_id()
{
cl_server *svr;

if (soft_max_servers && server_count > soft_max_servers) {
	id = ID_NOT_SET;  return ERR_MAX_SERVERS;
	}

// Id's go from 1 to F.
for(id=1;id <= 0xF;++id) {
	FOR_ALL_SERVERS(svr) if (svr != this && svr->id == id) break;
	if (!svr) return OK;
	}
// Failed to find free one
id = ID_NOT_SET;
return ERR_NO_FREE_IDS;
}




/*** Set the name ***/
void cl_server::set_name(char *nme)
{
if (strlen(nme) > MAX_SERVER_NAME_LEN) {
	strncpy(name,nme,MAX_SERVER_NAME_LEN);
	name[MAX_SERVER_NAME_LEN]='\0';
	}
else strcpy(name,nme);
}




/*** Set the name using non-null terminated string ***/
void cl_server::set_name(char *nme, int nlen)
{
if (nlen > MAX_SERVER_NAME_LEN) nlen = MAX_SERVER_NAME_LEN;
memcpy(name,nme,nlen);
name[nlen]='\0';
}




/*** Read something off the socket then deal with it ***/
void cl_server::sread()
{
pkt_hdr *ph;
int len,ret;
uint16_t dlen;
uint32_t hsec;

if (bpos >= SERVER_BUFF_SIZE) {
	// This will cause the link to screw up but thats better than having
	// the server crash. However it should never happen in the first place.
	log(1,"ERROR: cl_server::sread(): Buffer overflow!");
	bpos = 0;
	return;
	}
errno = 0;
if ((len=read(sock,(char *)buff+bpos,SERVER_BUFF_SIZE - bpos)) < 1) {
	// If -1 then we're trying to read off closed socket which can happen.
	if (!len) {
		log(1,"Server '%s' has done a remote DISCONNECT.",name);
		disconnect(SERVER_STAGE_DISCONNECTED);
		}
	return;
	}
bpos += len;
last_rx_time = server_time;

// Go through all stored commands in buffer
do {
	// Must have minimum of the header, 2 bytes length + 1 byte command
	if (bpos < 3) return;

	// Max sure we have the whole command
	ph=(pkt_hdr *)buff;
	if (ntohs(ph->len) > bpos) return; 

	// If length is zero the byte order makes no difference so we don't
	// need to do ntohs() inside the if().
	if (!ph->len) {
		log(1,"ERROR: Packet with zero length field from server '%s'; clearing buffer.",name);
		rx_err_cnt++;
		bpos = 0;
		break;
		}

	// Count all packets received , even duff ones
	rx_packets++;
	if (!rx_packets) SETFLAG(SERVER_FLAG_RX_WRAPPED);

	// Do data rate check. Reset count every second.
	if (max_svr_data_rate) {
		if ((hsec = get_hsec_time()) - rx_period_data_start >= 100) {
			rx_period_data = 0;
			rx_period_data_start = get_hsec_time();
			}
		if ((rx_period_data += len) > max_svr_data_rate) {
			log(1,"ERROR: Max data rate exceeded by server '%s' with %d bytes in 0.%02d seconds\n",name,rx_period_data,hsec - rx_period_data_start);
			disconnect(SERVER_STAGE_PACKET_OR_DATA_OVERLOAD);
			return;
			}
		}

	// Do packet rate check. 
	if (max_packet_rate) {
		if ((hsec = get_hsec_time()) - rx_period_pkt_start >= 100) {
			rx_period_packets = 0;
			rx_period_pkt_start = get_hsec_time();
			}
		if (++rx_period_packets > max_packet_rate) {
			log(1,"ERROR: Max packet rate exceeded by server '%s' with %d packets in 0.%02d seconds\n",name,rx_period_packets,hsec - rx_period_pkt_start);
			disconnect(SERVER_STAGE_PACKET_OR_DATA_OVERLOAD);
			return;
			}
		}

	// Log every packet regardless of whether its valid or not if
	// flag is set
	if (SYS_FLAGISSET(SYS_FLAG_HEXDUMP_PACKETS)) {
		// Don't set packet field byte order yet as we want to dump
		// packets contents as they are in the buffer.
		dlen = ntohs(ph->len);
		hexdump_packet(1,buff,dlen);
		ph->len = dlen;
		}
	else ph->len = ntohs(ph->len);

	// We expect certain packets depending on the stage
	switch(stage) {
		case SERVER_STAGE_CONNECTING:
		if (ph->type == PKT_REP_CONNECT) {
			recv_connect_reply();
			if (stage == SERVER_STAGE_CONNECT_FAILED) return;
			}
		else {
			log(1,"ERROR: Invalid service at server '%s' (%s:%d).",
				name,ipstr,ntohs(ip_addr.sin_port));
			disconnect(SERVER_STAGE_CONNECT_FAILED);
			return;
			}
		break;


		case SERVER_STAGE_INCOMING:
		if (ph->type != PKT_COM_CONNECT) {
			log(1,"ERROR: Incoming connection from %s did not send connect packet. Disconnecting.\n",ipnumstr);
			disconnect(SERVER_STAGE_CONNECT_FAILED);
			return;
			}
		// Return value is always OK , don't bother checking
		recv_connect();
		break;
	

		case SERVER_STAGE_CONNECTED:
		if (ph->type >= NUM_PKT_TYPES) {
			log(1,"ERROR: Unknown packet type %d from server '%s'",ph->type,name);
			rx_err_cnt++;
			break;
			}
		if (decrypt_packet((u_char *)ph,ph->len) != OK) {
			disconnect(SERVER_STAGE_NETWORK_ERROR);
			return;
			}

		// Call packet handling function
		if ((ret=(this->*pkt_recv_func[ph->type])()) != OK) {
			log(1,"ERROR: cl_server::sread(): dealing with packet type %d (%s): %s",ph->type,packet_type[ph->type],err_string[ret]);
			rx_err_cnt++;
			}
		break;


		case SERVER_STAGE_DISCONNECTED:
		// This may happen if a server has disconnected and we have
		// loopback users. Just ignore.
		break;


		default:
		// This should never happen
		log(1,"INTERNAL ERROR: Received packet for server connection in stage %d!\n",stage);
		disconnect(SERVER_STAGE_NETWORK_ERROR);
		return;
		}

	// Bpos will have been zeroed if we disconnected due to a disconnect
	// packet.
	if (!bpos) break;

	// Shift buffer down to next command. 
	bpos -= ph->len;
	if (bpos < 0) {
		// This should never happen
		log(1,"INTERNAL ERROR: cl_server::sread(): Network buffer underrun for server '%s', zeroing...",name);
		disconnect(SERVER_STAGE_NETWORK_ERROR);
		return;
		}
	else 
	if (bpos) memcpy(buff,buff+ph->len,bpos);
	} while(bpos);
}





/*** Write a packet to the socket , log any errors ***/
int cl_server::swrite(u_char *pkt,int len)
{
int r,prev_wlen,wlen,olen,ret;

if ((ret=encrypt_packet(pkt,len)) != OK) return ret;

olen = len;
wlen = prev_wlen = 0;
do {
	for(r=0;;++r) {
		switch((wlen=write(sock,(char *)(pkt+prev_wlen),len))) {
			case -1:
			// Catch fatal errors
			switch(errno) {
				case EBADF :
				case EINVAL:
				case ENXIO :
				case EFAULT:
				case EPIPE :
				log(1,"ERROR: Server '%s', socket %d in cl_server::swrite(): write(): %s",name,sock,strerror(errno));
				return ERR_WRITE;
				}
			// Fall through

			case 0:
			if (r == MAX_WRITE_RETRY) {
				log(1,"ERROR: Server '%s', socket %d in cl_server::swrite(): Maximum retries.",name,sock);
				tx_err_cnt++;
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

last_tx_time = server_time;
tx_packets++;
if (!tx_packets) SETFLAG(SERVER_FLAG_TX_WRAPPED);

if (SYS_FLAGISSET(SYS_FLAG_HEXDUMP_PACKETS)) hexdump_packet(0,pkt,olen);

return OK;
}




/*** Encrypt a packet. This is hardly the last word in encryption and won't
     be a problem for most crackers I suspect. Its main use is to discourage
     bored kiddies with packet sniffing programs. ***/
int cl_server::encrypt_packet(u_char *pkt, uint16_t len)
{
u_char tmp,tmp2,shift;
int i;

// We don't encrypt the 1st 3 bytes or error when not connected since this
// is called while connecting and sending server info
if (stage != SERVER_STAGE_CONNECTED || len < 4) return OK;

if (len < 4) return OK;

switch(encryption_level) {
	case 0: break;

	case 1:
	for(i=3;i < len;++i) {
		// Add key char plus previous pkt char (so a hacker needs the
		// whole message from the start to decode it) value plus i 
		tmp = pkt[i] + connect_key_str[enkpos] + enprv + i;

		// Split byte in 2 then swap the pieces positions & XOR with
		// prev 
		shift = connect_key_str[enkpos] % 8;
		tmp2 = tmp << shift;
		tmp2 |= tmp >> (8 - shift);
		tmp2 ^= enprv;

		// Set vars 
		enprv = pkt[i];
		pkt[i] = tmp2;

		// Increment along key 
		enkpos = (enkpos + 1) % connect_key_len;
		}
	break;

	default:
	// We can't get here unless someone hacks the code or some serious
	// memory fuckup occurs.
	log(1,"INTERNAL ERROR: cl_server::encrypt_packet(): Invalid encryption level!");
	return ERR_INTERNAL;
	}
return OK;
}




/*** Decrypt packet ***/
int cl_server::decrypt_packet(u_char *pkt, uint16_t len)
{
u_char tmp,shift;
int i;

if (len < 4) return OK;

switch(encryption_level) {
	case 0: break;

	case 1:
	for(i=3;i < len;++i) {
		shift = connect_key_str[deckpos] % 8;
		pkt[i] ^= decprv;
		tmp = pkt[i] >> shift;
		tmp |= pkt[i] << (8 - shift);
		pkt[i] = tmp - connect_key_str[deckpos] - decprv - i;
		decprv = pkt[i];
		deckpos = (deckpos + 1) % connect_key_len;
		}
	break;

	default:
	log(1,"INTERNAL ERROR: cl_server::decrypt_packet(): Invalid encryption level!");
	return ERR_INTERNAL;
	}
return OK;
}




/*** Do a hex dump of a packet to the log file. Used by sread() & swrite() ***/
void cl_server::hexdump_packet(int recv, u_char *pkt, int len)
{
int i,j;
char outstr[100],c;

// Print general packet info
log(1,"======================== %s PACKET =========================",
	recv ? "RECV" : "SEND");
log(1,"Server: %s (%s:%d)%s",
	name,
	ipstr,
	ntohs(ip_addr.sin_port),
	encryption_level ? " (ENCRYPTED LINK)" : "");
log(1,"%s#   : %d",recv ? "RX" : "TX",recv ? rx_packets: tx_packets);
log(1,"Bytes : %d",len);
log(1,"Type  : %d (%s)",
	pkt[2],pkt[2] >= NUM_PKT_TYPES ? "<unknown>" : packet_type[pkt[2]]);

// Do actual hexdump
log(1,"Dump  :");
for(i=0;i < len;i+=15) {
	outstr[0] = '\0';
	for(j = 0;j < 15;++j) {
		if (i + j < len)	
			sprintf(outstr,"%s%02X ",outstr,(unsigned char)pkt[i+j]);
		else
			strcat(outstr,"-- ");
		}

	strcat(outstr,": ");

	for(j = 0;j < 15 && i+j < len;++j) {
		// c is a signed char since some terminals still can't handle
		// ascii > 127. Pathetic.
		c = pkt[i+j];
		// Percent signs could be interpreted by print routines
		sprintf(outstr,"%s%c",outstr,c < 32 || c == '%' ? '.' : c);
		}
	log(1,outstr);
	}

log(1,"============================ END =============================");
}




/*** Disconnect from other server ***/
void cl_server::disconnect(int stg)
{
switch(stage) {
	case SERVER_STAGE_INCOMING:
	// Don't want pointless entries in server list
	stage = SERVER_STAGE_DELETE;
	break;


	case SERVER_STAGE_CONNECTING:
	// Dump the connect thread if we're not in it
	if (con_thrd != pthread_self()) cancel_thread(&con_thrd);

	allprintf(
		MSG_INFO,
		USER_LEVEL_NOVICE,NULL,"Server '%s' failed to connect.\n",name);
	stage = stg;
	break;


	case SERVER_STAGE_CONNECTED:
	connected_server_count--;
	allprintf(
		MSG_INFO,USER_LEVEL_NOVICE,NULL,
		"~OL~FRServer '%s' %s disconnected.\n",
		name,stg == SERVER_STAGE_DISCONNECTED ? "has" : "being");

	// Send reason packet if we've decided to disconnect
	switch(stg) {
		case SERVER_STAGE_TIMEOUT:
		send_disconnecting(ERR_TIMEOUT);
		break;

		case SERVER_STAGE_NETWORK_ERROR:
		send_disconnecting(ERR_NETWORK_ERROR);
		break;

		case SERVER_STAGE_PACKET_OR_DATA_OVERLOAD:
		send_disconnecting(ERR_PACKET_OR_DATA_OVERLOAD);
		break;

		case SERVER_STAGE_MANUAL_DISCONNECT:
		send_disconnecting(ERR_MANUAL_DISCONNECT);
		break;

		case SERVER_STAGE_SHUTDOWN:
		send_disconnecting(ERR_SHUTDOWN);
		break;

		/* Set stage now for this so deleting remote users won't send 
		   server info packets to disconnected server which will give 
		   write() error */
		case SERVER_STAGE_DISCONNECTED:
		stage = stg;
		}

	delete_remotes();
	set_friends_to_unknown();

	// Set stage to delete if flag is set to delete all disconnected 
	// incoming connections
	stage = (SYS_FLAGISSET(SYS_FLAG_DEL_DISC_INCOMING) &&
	         connection_type == SERVER_TYPE_INCOMING) ?
	         SERVER_STAGE_DELETE : stg;

	send_server_info_to_servers(NULL);
	break;


	default:
	log(1,"INTERNAL ERROR: Invalid stage %d in cl_server::disconnect()\n",
		stage);
	stage = SERVER_STAGE_DELETE;
	// Fall through
	}

if (sock != -1) close(sock);
sock = -1;
bpos = 0;
}




/*** Destroy all remote users and move all local users back to local server ***/
void cl_server::delete_remotes()
{
cl_user *u;
cl_remote_user *ru;

FOR_ALL_USERS(u) {
	if (u->type == USER_TYPE_REMOTE) {
		ru=(cl_remote_user *)u;
		if (ru->server_from == this) { delete u;  continue; }
		}
	if (u->server_to == this) {
		u->infoprintf("~NPYou are returned to this server (%s) because of a remote server disconnect.\n",server_name);

		u->home_group->join(u);
		u->prompt();
		}
	}
}




/*** Go through users friends list for this server and set all to unknown
     status since the server has disconnected. ***/
void cl_server::set_friends_to_unknown()
{
cl_user *u;
cl_friend *frnd;

FOR_ALL_USERS(u) {
	FOR_ALL_USERS_FRIENDS(u,frnd) {
		if (frnd->server == this) {
			frnd->server = NULL;
			if (frnd->stage == FRIEND_ONLINE) {
				u->infoprintf("The server of your friend ~FT%04X@%s~RS has disconnected.\n",frnd->id,frnd->svr_name);
				frnd->stage = FRIEND_UNKNOWN;
				}
			}
		}
	}	
}




/*** Send find user requests for all user friends for this server ***/
void cl_server::send_friends_requests()
{
cl_user *u;
cl_friend *frnd;
int ret;

FOR_ALL_USERS(u) {
	FOR_ALL_USERS_FRIENDS(u,frnd) {
		if (frnd->svr_name && !strcasecmp(frnd->svr_name,name)) {
			frnd->stage = FRIEND_LOCATING;
			frnd->server = this;
			if ((ret=send_find_user(u->id,frnd->id)) != OK) {
				log(1,"ERROR: cl_server::send_friends_requests() -> cl_server::send_find_user(): %s",err_string[ret]);
				return;
				}
			}
		}
	}
}




/*** Get a remote user based on their remote_id ***/
cl_remote_user *cl_server::svr_get_remote_user(uint16_t rid)
{
cl_user *u;
cl_remote_user *ru;

FOR_ALL_USERS(u) {
	if (u->type == USER_TYPE_REMOTE) {
		ru = (cl_remote_user *)u;
		if (ru->remote_id == rid && ru->server_from == this) return ru;
		}
	}
return NULL;
}




///////////////////////////// THREAD FUNCTIONS ////////////////////////////////

/*** Create a thread ***/
int cl_server::spawn_thread(int thread_type)
{
switch(thread_type) {
	case THREAD_TYPE_SVR_RESOLVE:
	if (create_thread(
		THREAD_TYPE_SVR_RESOLVE,
		&ip_thrd,server_resolve_thread_entry,(void *)this) == -1) {
		log(1,"ERROR: Unable to spawn resolver thread for server: %s",strerror(errno));
		return ERR_CANT_SPAWN_THREAD;
		}
	break;

	case THREAD_TYPE_CONNECT:
	if (connection_type == SERVER_TYPE_INCOMING) return ERR_INVALID_TYPE;

	if (stage == SERVER_STAGE_INCOMING ||
	    stage == SERVER_STAGE_CONNECTING ||
	    stage == SERVER_STAGE_CONNECTED ||
	    stage == SERVER_STAGE_DELETE) {
		log(1,"ERROR: Cannot spawn connect thread for server object in stage %d\n",stage);
		return ERR_INVALID_STAGE;
		}

	reset();

	if (create_thread(
		THREAD_TYPE_CONNECT,
		&con_thrd,server_connect_thread_entry,(void *)this) == -1) {
		log(1,"ERROR: Unable to spawn connect thread for server: %s",strerror(errno));
		return ERR_CANT_SPAWN_THREAD;
		}
	break;

	default:
	log(1,"INTERNAL ERROR: Unknown thread type %d in cl_server::spawn_thread()\n",thread_type);
	return ERR_INTERNAL;
	}
return OK;
}




/*** Resolve an ip address ***/
void cl_server::resolve_ip_thread()
{
hostent *host;
char *tmp1,*tmp2;

// Assigning the pointers is an atomic operation so don't need mutex lock
if ((host=gethostbyaddr((char *)&(ip_addr.sin_addr.s_addr),4,AF_INET))) {
	tmp1 = ipstr;
	tmp2 = ipnamestr;
	ipstr = strdup(host->h_name);
	ipnamestr = strdup(host->h_name);
	log(1,"Server address %s = %s",ipnumstr,ipnamestr);

	sleep(2);
	free(tmp1);
	free(tmp2);
	}
else log(1,"Server address %s does not resolve.",ipnumstr);

exit_thread(&ip_thrd);
}




/*** This sets up the socket and connection. This is the only place the
     protocol is synchronous. We MUST receive an OK or ERROR before we
     are considered connected (or not) and can send anything else. This is
     checked in sread(). ***/
void cl_server::connect_thread()
{
struct sockaddr_in bind_addr;
hostent *hp;
int ret,size,i;

log(1,"Connecting to server '%s' (%s:%d) ...",name,ipstr,ntohs(ip_addr.sin_port));
stage = SERVER_STAGE_CONNECTING;

// Resolve ip name if required
if ((int)(ip_addr.sin_addr.s_addr=inet_addr(ipstr)) == -1) {
	free(ipnamestr);
	ipnamestr = strdup(ipstr);

	if (!(hp=gethostbyname(ipstr))) {
		log(1,"Server '%s' (%s:%d) not found, connect failed: gethostbyname(): %s",
			name,ipstr,ntohs(ip_addr.sin_port),strerror(errno));
		stage = SERVER_STAGE_NOT_FOUND;
		return;
		}
	}
// Got IP number in config file , find the IP name
else {
	hp = NULL;
	spawn_thread(THREAD_TYPE_SVR_RESOLVE);
	}

// Set up socket, bind to a specific local port if required & then connect.
if ((sock=socket(AF_INET,SOCK_STREAM,0))==-1) {
	log(1,"ERROR: cl_server::connect_thread(): socket(): %s",strerror(errno));
	goto ERROR;
	}
if (local_port) {
	bzero((char *)&bind_addr,sizeof(bind_addr));
	bind_addr.sin_family = AF_INET;
	bind_addr.sin_addr.s_addr = INADDR_ANY;
	bind_addr.sin_port = htons(local_port);

	if (bind(sock,(struct sockaddr *)&bind_addr,sizeof(bind_addr)) == -1) {
		log(1,"ERROR: cl_server::connect_thread(): bind(): %s",
			strerror(errno));
		goto ERROR;
		}
	}
// If we have an IP name try to connect to all the returned addresses
// before giving up
if (hp) {
	for(i=0;hp->h_addr_list[i];++i) {
		memcpy((char *)&ip_addr.sin_addr.s_addr,
		       hp->h_addr_list[i],hp->h_length);

		free(ipnumstr);
		ipnumstr=strdup(inet_ntoa(ip_addr.sin_addr));
		log(1,"Trying '%s' address %d: %s ...\n",ipstr,i+1,ipnumstr);

		if (connect(sock,(sockaddr *)&ip_addr,sizeof(ip_addr)) != -1) 
			break;
		}
	if (!hp->h_addr_list[i]) {
		log(1,"ERROR: cl_server::connect_thread(): connect(): %s",
			strerror(errno));
		goto ERROR;
		}
	}
else {
	free(ipnumstr);
	ipnumstr=strdup(inet_ntoa(ip_addr.sin_addr));
	if (connect(sock,(sockaddr *)&ip_addr,sizeof(ip_addr))==-1) {
		log(1,"ERROR: cl_server::connect_thread(): connect(): %s",
			strerror(errno));
		goto ERROR;
		}
	}

// Get local interface address
size = sizeof(sockaddr_in);
#ifdef SOLARIS
if (getsockname(sock,(sockaddr *)&local_ip_addr,&size))
#else
if (getsockname(sock,(sockaddr *)&local_ip_addr,(socklen_t *)&size))
#endif
	log(1,"ERROR: cl_server::connect_thread(): getsockname(): %s",strerror(errno));

// Send connect packet & force read since main thread won't know we've 
// connected and readmask will not be set up to read off this socket yet.
if ((ret=send_svr_info(PKT_COM_CONNECT)) != OK) {
	log(1,"ERROR: cl_server::connect_thread() -> cl_server::send_svr_info(): %s",err_string[ret]);
	goto ERROR;
	}
sread();
exit_thread(&con_thrd);

// If we get here we're royally fucked
log(1,"PANIC: Thread did not exit in cl_server::connect_thread()!!");
exit(1);

ERROR:
log(1,"CONNECT to server '%s' (%s:%d) FAILED.",name,ipstr,ntohs(ip_addr.sin_port));
if (user) {
	user->infoprintf("Connection to server '%s' ~OL~FRFAILED~RS. See log for more information.\n",name);
	user = NULL;
	}
stage = SERVER_STAGE_CONNECT_FAILED;
if (sock != -1) close(sock);
sock = -1;
exit_thread(&con_thrd);
}



//////////////////////////// PACKET FUNCTIONS //////////////////////////////

///////////// GENERAL PURPOSE SEND FUNCTIONS /////////////

/*** A generic nunction that just sends a command and a user id ***/
int cl_server::send_uid(u_char ptype, uint16_t uid)
{
pkt_generic2 pkt;

if (stage != SERVER_STAGE_CONNECTED) return ERR_SERVER_NOT_CONNECTED;

pkt.len = htons(PKT_GENERIC2_SIZE);
pkt.type = ptype;
pkt.add_prefix = 0;  // Not used
pkt.id = htons(uid);
return swrite((u_char *)&pkt,PKT_GENERIC2_SIZE);
}




/*** This sends a paid of id's in a generic1 packet. Id's must be in 
     network byte order when passed. ***/
int cl_server::send_id_pair(
	u_char ptype, u_char erf, uint16_t id1, uint16_t id2)
{
pkt_generic1 pkt;

if (stage != SERVER_STAGE_CONNECTED) return ERR_SERVER_NOT_CONNECTED;

pkt.len = htons(PKT_GENERIC1_SIZE2);
pkt.type = ptype;
pkt.erf = erf;
pkt.id1 = id1;
pkt.id2 = id2;
return swrite((u_char *)&pkt,PKT_GENERIC1_SIZE2);
}




/*** This creates and sends a filled user info packet for the given user 
     and packet type ***/
int cl_server::send_user_info_packet(u_char ptype, cl_user *u, uint16_t gid)
{
cl_remote_user *ru;
pkt_user_info *pkt;
char *nme,*dsc,*snme;
uint16_t len,orig_uid;
int ret;
u_char nlen,dlen,snlen;

if (stage != SERVER_STAGE_CONNECTED) return ERR_SERVER_NOT_CONNECTED;

switch(ptype) {
	case PKT_COM_JOIN:
	case PKT_COM_UJOIN:
	case PKT_INF_USER_INFO:  break;

	default: return ERR_INTERNAL;
	}

if (u->type == USER_TYPE_LOCAL) {
	nme = u->name;
	dsc = u->desc;
	orig_uid = u->id;
	snme = server_name;
	}
else {
	ru = (cl_remote_user *)u;
	nme = ru->full_name;
	dsc = ru->full_desc;
	orig_uid = ru->orig_id;
	snme = ru->home_svrname;
	}
nlen = (u_char)strlen(nme);
dlen = (u_char)strlen(dsc);
if (proto_rev > 5) snlen = snme ? strlen(snme) : 0;
else snlen = 0;
len = PKT_USER_INFO_SIZE + nlen + dlen + snlen - 1;

if (!(pkt = (pkt_user_info *)malloc(len))) return ERR_MALLOC;
pkt->len = htons(len);
pkt->type = ptype;
pkt->hop_count = (u->type == USER_TYPE_LOCAL ? 0 : ru->hop_count);
pkt->uid = htons(u->id);
pkt->orig_uid = htons(orig_uid);
pkt->gid_ruid = htons(gid);
pkt->orig_level = (u->type == USER_TYPE_LOCAL ? 
                  (u_char)u->level : ru->orig_level);
pkt->term_cols = u->term_cols > 255 ? 255 : (u_char)u->term_cols;
pkt->term_rows = u->term_rows > 255 ? 255 : (u_char)u->term_rows;
pkt->desclen = dlen;
pkt->pad[0] = 0;
pkt->pad[1] = 0;

if (proto_rev > 5) pkt->user_flags = htonl(u->flags);
else
// Older versions didn't ignore these on the remote end so allowing a
// hacked server to make users invisible
pkt->user_flags = htonl(u->flags & REMOTE_IGNORE_FLAGS_MASK);

memset(pkt->home_addr.ip6,IP6_ADDR_SIZE,'\0');

if (u->type == USER_TYPE_LOCAL) {
	pkt->home_addr.ip4 = local_ip_addr.sin_addr.s_addr;
	pkt->home_port = htons((uint16_t)tcp_port[PORT_TYPE_USER]);
	}
else {
	pkt->home_addr.ip4 = u->ip_addr.sin_addr.s_addr;
	pkt->home_port = u->ip_addr.sin_port;
	}

pkt->namelen = nlen;
memcpy(pkt->name_desc_svr,nme,(int)nlen);
memcpy(pkt->name_desc_svr + (int)nlen,dsc,(int)dlen);
if (snlen) memcpy(pkt->name_desc_svr + (int)nlen + (int)dlen,snme,(int)snlen);

ret=swrite((u_char *)pkt,len);
free(pkt);
return ret;
}




/*** Send a message using the generic1 packet structure. Currently used for
     tell, pemote and svr_msg ***/
int cl_server::send_mesg(
	u_char ptype,
	u_char ftell,uint16_t uid_from, uint16_t uid_to, char *nme, char *msg)
{
pkt_generic1 *pkt;
uint16_t len;
u_char nlen;
int ret;

if (stage != SERVER_STAGE_CONNECTED) return ERR_SERVER_NOT_CONNECTED;

// Network broadcast packet appeared in revision 3
if (ptype == PKT_INF_BCAST && proto_rev < 3) return ERR_REVISION_TOO_LOW;

len = PKT_GENERIC1_SIZE3 - 1 + strlen(nme) + strlen(msg);
if (!(pkt = (pkt_generic1 *)malloc(len))) return ERR_MALLOC;

pkt->len = htons(len);
pkt->type = ptype;
pkt->erf = ftell; 
pkt->id1 = htons(uid_from);
pkt->id2 = htons(uid_to);
nlen = (u_char)strlen(nme);
pkt->namelen = nlen;
memcpy(pkt->name_mesg,nme,(int)nlen);
memcpy(pkt->name_mesg + (int)nlen,msg,strlen(msg));

ret=swrite((u_char *)pkt,len);
free(pkt);
return ret;
}



////////////// SVR INFO (also used for CONNECT) ////////////


/*** Send a server connect/connect reply/info packet ***/
int cl_server::send_svr_info(u_char ptype)
{
pkt_svr_info *pkt;
int nlen,len,ret;
u_char flags_el;

switch(ptype) {
	case PKT_COM_CONNECT:
	nlen = strlen(server_name);
	len = nlen + PKT_SVR_INFO_SIZE;
	// Top 2 bits are the encryption level allowing up to 4 encryption
	// levels. Flags are not currently used.
	flags_el = encryption_level << 6;
	break;

	case PKT_REP_CONNECT:
	len = PKT_SVR_INFO_SIZE;
	flags_el = encryption_level << 6;
	break;

	case PKT_INF_SVR_INFO:
	len = PKT_SVR_INFO_SIZE;
	flags_el = 0;
	break;

	default:
	return ERR_INTERNAL;
	}

if (!(pkt = (pkt_svr_info *)malloc(len))) return ERR_MALLOC;
pkt->len = htons(len);
pkt->type = ptype;
pkt->error = OK;
pkt->connect_key = htonl(connect_key);
pkt->proto_rev = PROTOCOL_REVISION;
pkt->lockout_level = (u_char)lockout_level;
pkt->svr_version = svr_version;
pkt->flags_el = flags_el;
pkt->local_user_cnt = htons(local_user_count);
pkt->remote_user_cnt = htons(remote_user_count);
pkt->sys_group_cnt = (u_char)system_group_count;
pkt->pub_group_cnt = (u_char)public_group_count;
pkt->svr_links_cnt = (u_char)connected_server_count;

pkt->rem_user_max_level = remote_user_max_level;
pkt->ping_interval = htons(send_ping_interval);

// We only send the name if its a connect packet
if (ptype == PKT_COM_CONNECT) memcpy((char *)pkt->name,server_name,nlen);

ret=swrite((u_char *)pkt,len);
free(pkt);
return ret;
}




/*** Receive a server info packet which can reset a subset of the values
     sent with a connect packet. ***/
int cl_server::recv_svr_info()
{
pkt_svr_info *pkt;

pkt = (pkt_svr_info *)buff;

if (pkt->len < PKT_SVR_INFO_SIZE) {
	log(1,"ERROR: Invalid length server info packet from server '%s'",name);
	return ERR_RECEIVE;
	}

local_user_cnt = ntohs(pkt->local_user_cnt);
remote_user_cnt = ntohs(pkt->remote_user_cnt);
sys_group_cnt = pkt->sys_group_cnt;
pub_group_cnt = pkt->pub_group_cnt;
svr_links_cnt = pkt->svr_links_cnt;
svr_lockout_level = pkt->lockout_level > USER_LEVEL_ADMIN ? 
                    USER_LEVEL_ADMIN : pkt->lockout_level;
svr_rem_user_max_level = pkt->rem_user_max_level > USER_LEVEL_ADMIN ?
                         USER_LEVEL_ADMIN : pkt->rem_user_max_level;
return OK;
}



////////////// CONNECT /////////////


/*** Send a packet error that requires no other info. ***/
int cl_server::send_connect_error(u_char error)
{
pkt_svr_info pkt;

pkt.len = htons(PKT_CONNECT_ERROR_SIZE);
pkt.type = PKT_REP_CONNECT;
pkt.error = error;
return swrite((u_char *)&pkt,PKT_CONNECT_ERROR_SIZE);
}




/*** Received connect from incoming connection. Always returns OK. ***/
int cl_server::recv_connect()
{
cl_server *svr;
pkt_svr_info *pkt;
u_char enclev;
int ret;

if (stage == SERVER_STAGE_CONNECTED) {
	log(1,"ERROR: Unexpected connect packet server '%s'",name);
	return OK;
	}

// Get name of remote server
pkt = (pkt_svr_info *)buff;
if (pkt->len < PKT_SVR_INFO_SIZE + 1) {
	log(1,"REFUSING connect: Packet too short");
	send_connect_error(ERR_PACKET_ERROR);
	stage = SERVER_STAGE_DELETE;
	return OK;
	}
set_name((char *)pkt->name,pkt->len - PKT_SVR_INFO_SIZE);
connect_key = ntohl(pkt->connect_key);
sprintf(connect_key_str,"%u",connect_key);
connect_key_len = strlen(connect_key_str);

log(1,"CONNECT request from '%s' (%s:%d)",name,ipnumstr,ntohs(ip_addr.sin_port));
set_and_log_connect_info(pkt);

if (proto_rev == 1) {
	log(1,"REFUSING connect: Protocol revision too low.");
	send_connect_error(ERR_REVISION_TOO_LOW);
	stage = SERVER_STAGE_DELETE;
	return OK;
	}
if (connect_key != local_connect_key) {
	log(1,"REFUSING connect: Invalid connect key.");
	send_connect_error(ERR_INVALID_CONNECT_KEY);
	stage = SERVER_STAGE_DELETE;
	return OK;
	}
if (has_whitespace(name)) {
	log(1,"REFUSING connect: Name has whitespace.");
	send_connect_error(ERR_SVR_NAME_HAS_WHITESPACE);
	stage = SERVER_STAGE_DELETE;
	return OK;
	}

// If same name as this server then disallow
if (!strcasecmp(name,server_name)) {
	log(1,"REFUSING connect: Name is the same as this server.");
	send_connect_error(ERR_DUPLICATE_NAME);
	stage = SERVER_STAGE_DELETE;
	return OK;
	}

// Set encryption level to the max we can handle which will we sent back in
// the reply packet unless we zero it below.
enclev = pkt->flags_el >> 6;
encryption_level = (enclev > MAX_ENCRYPTION_LEVEL ? MAX_ENCRYPTION_LEVEL : enclev);

switch(incoming_encryption_policy) {
	case ENCRYPT_ALWAYS:
	if (!enclev) {
		log(1,"REFUSING connect: Only encrypted links permitted.");
		send_connect_error(ERR_ENCRYPTED_ONLY);
		stage = SERVER_STAGE_DELETE;
		return OK;
		}
	break;

	case ENCRYPT_NEVER:
	if (enclev) {
		log(1,"WARNING: Encrypted link requested but setting to unencrypted.\n");
		encryption_level = 0;
		}
	break;

	case ENCRYPT_EITHER: break;

	default:
	log(1,"INTERNAL ERROR: cl_server::recv_connect(): Invalid incoming encryption policy %d\n",incoming_encryption_policy);
	incoming_encryption_policy = ENCRYPT_EITHER;
	}

if (encryption_level && encryption_level < enclev) 
	log(1,"WARNING: Encrypted level %d requested but setting to %d.\n",
		enclev,encryption_level);

/* Find server with the same name. If we already have one connected or
   connecting its an error then set this object for deletion, if we have one 
   disconnected or errored then set old object for deletion. */
FOR_ALL_SERVERS(svr) 
	if (svr != this && !strcasecmp(svr->name,name)) break;
if (svr) {
	if (svr->stage == SERVER_STAGE_CONNECTING || 
	    svr->stage == SERVER_STAGE_CONNECTED) {
		log(1,"REFUSING connect: Duplicate name");
		send_connect_error(ERR_DUPLICATE_NAME);
		stage = SERVER_STAGE_DELETE;
		return OK;
		}
	svr->stage = SERVER_STAGE_DELETE;
	}

// Everythings ok, send our reply and protocol revision
if ((ret=send_svr_info(PKT_REP_CONNECT)) != OK) {
	log(1,"ERROR: cl_server::recv_connect() -> cl_server::send_svr_info(): %s\n",err_string[ret]);
	stage = SERVER_STAGE_DELETE;
	}
else {
	log(1,"CONNECT GRANTED.");
	stage = SERVER_STAGE_CONNECTED;
	connect_time = server_time;
	connected_server_count++;
	send_server_info_to_servers(this);
	send_friends_requests();
	allprintf(
		MSG_INFO,
		USER_LEVEL_NOVICE,
		NULL,"~OL~FGNew connection to server '%s'.\n",name);
	}
return OK;
}




/*** Received reply for outgoing connection ***/
int cl_server::recv_connect_reply()
{
pkt_svr_info *pkt;
u_char enclev;
int eref;

pkt = (pkt_svr_info *)buff;
if (stage != SERVER_STAGE_CONNECTING) {
	log(1,"ERROR: Unexpected connect reply packet from server '%s'",name);
	return ERR_RECEIVE;
	}
if (pkt->len < PKT_CONNECT_ERROR_SIZE) {
	send_disconnecting(ERR_NETWORK_ERROR);
	goto LFAIL;
	}

if (pkt->error != OK) {
	log(1,"CONNECT to server '%s' (%s:%d) FAILED: Error %d: %s",
		name,
		ipstr,
		ntohs(ip_addr.sin_port),
		pkt->error,
		pkt->error < NUM_ERRORS ? err_string[pkt->error] : "Unknown error");
	if (user) 
		user->infoprintf("Connection to server '%s' ~OL~FRFAILED~RS: Error %d: %s\n",
			name,
			pkt->error,
			pkt->error < NUM_ERRORS ? err_string[pkt->error] : "Unknown error");
	goto FAIL;
	}

// ">=" not "==" in case future protocol revisions send the name in the
// connect reply
if (pkt->len >= PKT_SVR_INFO_SIZE) {
	enclev = pkt->flags_el >> 6;

	/* Check encryption. If we're enforcing it then disconnect if remote
	   server won't/can't do it , else just unset encrypt flag and give
	   warnings. */
	if (encryption_level && !enclev) {
		if (SYS_FLAGISSET(SYS_FLAG_OUT_ENF_ENCRYPTION)) {
			if (user)
				user->infoprintf("Connection to server '%s' ~OL~FRFAILED~RS: Encryption refused.\n",name);
			log(1,"CONNECT to server '%s' (%s:%d) FAILED: Encryption refused.\n",name,ipstr,ntohs(ip_addr.sin_port));

			send_disconnecting(ERR_ENCRYPTED_ONLY);
			goto FAIL;
			}
		eref = 1;
		}
	else eref = 0;

	// The encryption level must be set to whatever level the remote host can
	// handle which should never be more than we sent.
	encryption_level = enclev;

	if (eref) 
		log(1,"CONNECT request to server '%s' (%s:%d) SUCCEEDED but encryption was refused or is not supported.",
			name,ipstr,ntohs(ip_addr.sin_port));
	else
		log(1,"CONNECT request to server '%s' (%s:%d) SUCCEEDED.",
			name,ipstr,ntohs(ip_addr.sin_port));

	stage = SERVER_STAGE_CONNECTED;

	set_and_log_connect_info(pkt);
	connect_time = server_time;
	connected_server_count++;
	send_server_info_to_servers(this);
	send_friends_requests();

	if (user) {
		if (eref)
			user->infoprintf("Connection to server '%s' ~OL~FGSUCCEEDED~RS but encryption was refused or\n      is not supported.\n",name);
		else
			user->infoprintf("Connection to server '%s' ~OL~FGSUCCEEDED~RS.\n",name);
		}

	allprintf(
		MSG_INFO,
		USER_LEVEL_NOVICE,
		NULL,"~OL~FGNew connection to server '%s'.\n",name);
	return OK;
	}
LFAIL:
log(1,"CONNECT to server '%s' (%s:%d) FAILED: Packet length error",
	name,ipstr,ntohs(ip_addr.sin_port));
if (user)
	user->infoprintf("Connection to server '%s' ~OL~FRFAILED~RS: packet length error\n",name);

FAIL:
user = NULL;
disconnect(SERVER_STAGE_CONNECT_FAILED);
return OK;
}




/*** Sets and logs various info from connect packet. Encryption level is NOT
     set here. ***/
void cl_server::set_and_log_connect_info(pkt_svr_info *pkt)
{
sprintf(version,"%d.%d.%d",
	pkt->svr_version / 100,
	pkt->svr_version % 100 / 10,
	pkt->svr_version % 10);

proto_rev = pkt->proto_rev;
sys_group_cnt = pkt->sys_group_cnt;
pub_group_cnt = pkt->pub_group_cnt;
local_user_cnt = ntohs(pkt->local_user_cnt);
remote_user_cnt = ntohs(pkt->remote_user_cnt);
svr_links_cnt = pkt->svr_links_cnt+1; // +1 cos it won't include us yet
svr_lockout_level = pkt->lockout_level > USER_LEVEL_ADMIN ? 
                    USER_LEVEL_ADMIN : pkt->lockout_level;
svr_rem_user_max_level = pkt->rem_user_max_level > USER_LEVEL_ADMIN ?
                         USER_LEVEL_ADMIN : pkt->rem_user_max_level;
// Revisions less than 3 did not send this information
if (proto_rev > 2) ping_interval = ntohs(pkt->ping_interval);

// Put the name in front all the time since if 2 threads connect at the
// same time the output here will get intermingled and confused otherwise.
log(1,"%s -> Local IP address     : %s:%d",
	name,inet_ntoa(local_ip_addr.sin_addr),ntohs(local_ip_addr.sin_port));
log(1,"%s -> Version              : %s\n",name,version);
log(1,"%s -> NIVN protocol rev.   : %d",name,proto_rev);
log(1,"%s -> Encryption level req.: %d%s\n",
	name,pkt->flags_el >> 6,proto_rev < 5 ? "  (not supported)" : "");
log(1,"%s -> Total users          : %d (%d local, %d remote)",
	name,(local_user_cnt + remote_user_cnt),local_user_cnt,remote_user_cnt);
log(1,"%s -> System groups        : %d",name,sys_group_cnt);
log(1,"%s -> Public groups        : %d",name,pub_group_cnt);
log(1,"%s -> Server links         : %d",name,svr_links_cnt);
log(1,"%s -> Lockout level        : %s",name,user_level[svr_lockout_level]);
log(1,"%s -> Remote user max level: %s",name,user_level[svr_rem_user_max_level]);
log(1,"%s -> Ping interval        : %s",
	name,ping_interval ? time_period(ping_interval) : "<unknown>");
}




////////////// DISCONNECTING ////////////////

/*** Send a disconnecting packet telling remote server why we're doing it.
     Reason code is actually an error code for convenience. ***/
int cl_server::send_disconnecting(u_char reason)
{
pkt_generic1 pkt;

pkt.len = htons(PKT_GENERIC1_SIZE1);
pkt.type = PKT_INF_DISCONNECTING;
pkt.erf = reason;
return swrite((u_char *)&pkt,PKT_GENERIC1_SIZE1);
}




/*** Received a disconnecting packet. Even though the remote server will
     disconnect anyway do a disconnect ourselves just in case packet is from
     some hacked code that might keep sending disconnect packets over and
     over (so producing endless log messages) just for a laugh ***/
int cl_server::recv_disconnecting()
{
pkt_generic1 *pkt;

pkt = (pkt_generic1 *)buff;
if (pkt->len != PKT_GENERIC1_SIZE1) {
	log(1,"ERROR: Invalid length disconnecting packet from server '%s'",name);
	return ERR_RECEIVE;
	}
sprintf(text,"Server '%s' has DISCONNECTED: Reason %d: %s\n",
	name,
	pkt->erf,
	pkt->erf < NUM_ERRORS ? err_string[pkt->erf] : "Unknown error");

allprintf(MSG_INFO,USER_LEVEL_NOVICE,NULL,text);
log(1,text);

disconnect(SERVER_STAGE_DISCONNECTED);
return OK;
}



////////////// PING //////////////

/*** Send out a ping. Called in events thread and manually by "server ping"
     admin command. ***/
int cl_server::send_ping(cl_user *u)
{
pkt_ping pkt;
uint32_t tval;
int ret;

if (stage != SERVER_STAGE_CONNECTED) return ERR_SERVER_NOT_CONNECTED;

if (u) {
	if (proto_rev < 4) return ERR_REVISION_TOO_LOW;

	pkt.len = htons(PKT_PING_SIZE2);
	pkt.type = PKT_COM_PING;
	pkt.flags = 0; // Currently unused
	pkt.uid = htons(u->id);
	tval = get_hsec_time();
	pkt.send_time = htonl(tval);
	}
else {
	pkt.len = htons(PKT_PING_SIZE1);
	pkt.type = PKT_COM_PING;
	}

if ((ret = swrite((u_char *)&pkt,ntohs(pkt.len))) != OK) return ret;

if (u) u->uprintf("~BMPING TX:~RS Server '~FT%s~RS', tval = ~FM%u~RS\n",name,tval);
last_ping_time = server_time;
return OK;
}




/*** Received a ping, send a reply depending on what type it is and proto.
     revision level ***/
int cl_server::recv_ping()
{
pkt_ping send_pkt;
pkt_ping *recv_pkt;

recv_pkt = (pkt_ping *)buff;

switch(recv_pkt->len) {
	case PKT_PING_SIZE1:
	// Normal keepalive ping. If remote proto < 3 then ping back.
	if (proto_rev > 2) return OK;
	send_pkt.len = htons(PKT_PING_SIZE1);
	send_pkt.type = PKT_REP_PING;
	break;
	
	case PKT_PING_SIZE2:
	// Timer ping, always reflect. NIVN revisions less than 4 will never
	// send this.
	send_pkt.len = htons(PKT_PING_SIZE2);
	send_pkt.type = PKT_REP_PING;
	send_pkt.flags = 0;
	send_pkt.send_time = recv_pkt->send_time;
	send_pkt.uid = recv_pkt->uid;
	break;

	default:
	log(1,"ERROR: Invalid length ping packet from '%s'",name);
	return ERR_RECEIVE;
	}
return swrite((u_char *)&send_pkt,ntohs(send_pkt.len));
}




/*** Received ping reply ***/
int cl_server::recv_ping_reply()
{
pkt_ping *pkt;
cl_user *u;
uint32_t tval,tm;

pkt = (pkt_ping *)buff;

switch(pkt->len) {
	// Do nothing, just needed last_rx_time updating.
	case PKT_PING_SIZE1: return OK;

	case PKT_PING_SIZE2:
	tval = ntohl(pkt->send_time);
	tm = get_hsec_time() - tval;

	// Print info if user is there and ping_time matches.
	if ((u = get_user(ntohs(pkt->uid))))
		u->uprintf("~BBPING RX:~RS Server '~FT%s~RS', tval = ~FM%u~RS, round trip: ~FY%d.%02d~RS seconds.\n",name,tval,tm / 100, tm % 100);
	return OK;
	}
log(1,"ERROR: Invalid length ping reply packet from '%s'",name);
return ERR_RECEIVE;
}




////////////// TELL & PEMOTE //////////////

/*** Got a tell or private emote ***/
int cl_server::recv_tell()
{
cl_user *u;
pkt_generic1 *pkt;
char *uname,*mesg;
uint16_t uid_from,uid_to;
int mlen,ret,ttype;

pkt = (pkt_generic1 *)buff;

// Convert values
uid_from = ntohs(pkt->id1);
uid_to = ntohs(pkt->id2);

// Min length must be PKT_GENERIC1_SIZE3+1 since we need min 1 char name and
// min 1 char mesg and namelen can't be longer than the packet length minus
// header length
if (pkt->len < PKT_GENERIC1_SIZE3 + 1 || 
    pkt->namelen > pkt->len - (PKT_GENERIC1_SIZE3-1)) {
	log(1,"ERROR: Invalid length tell/pemote packet from server '%s'",name);
	svrprintf(0,uid_from,1,"~OL~FRERROR:~RS Packet error, cannot deliver tell/pemote to user %04X.\n~PR",uid_to);
	return ERR_RECEIVE;
	}

// Find user. 
if (!(u = get_user(uid_to))) 
	return svrprintf(0,uid_from,1,"User ~FT%04X~RS is not logged on, cannot deliver tell/pemote.\n~PR",uid_to);

// Set strings
if (!(uname = (char *)malloc(pkt->namelen+1))) return ERR_MALLOC;
memcpy(uname,pkt->name_mesg,pkt->namelen);
uname[pkt->namelen] = '\0';

mlen = pkt->len - pkt->namelen - (PKT_GENERIC1_SIZE3-2);
if (!(mesg = (char *)malloc(mlen))) {
	free(uname);  return ERR_MALLOC;
	}
memcpy(mesg,pkt->name_mesg + pkt->namelen,mlen-1);
mesg[mlen-1] = '\0';
clean_string(mesg);

// Send tell or pemote and check status reply
if (pkt->type == PKT_COM_TELL) 
	ttype = pkt->erf ? TELL_TYPE_FRIENDS_TELL : TELL_TYPE_TELL;
else
	ttype = pkt->erf ? TELL_TYPE_FRIENDS_PEMOTE : TELL_TYPE_PEMOTE;

switch(u->tell(ttype,uid_from,uname,name,mesg)) {
	case ERR_USER_NO_TELLS:
	ret=svrprintf(0,uid_from,1,"User ~FT%04X~RS (%s) is not receiving tells/pemotes at the moment.\n",u->id,u->name);
	break;

	case ERR_USER_AFK:
	ret = OK;
	try {
		svrprintf(1,uid_from,1,"User ~FT%04X~RS (%s) is AFK at the moment but your tell has been stored.\n",u->id,u->name);
		if (u->afk_msg)
			svrprintf(1,uid_from,1,"~FYAFK message:~RS %s\n",
				u->afk_msg);
		}
	catch(int err) { ret = err; }
	break;

	default: ret=OK;
	}

free(uname);
free(mesg);
return ret;
}



////////////// JOIN //////////////

/*** Send request to for user to join users on the remote server in
     a specific group ***/
int cl_server::send_join(cl_user *u, uint16_t gid)
{
return (u->server_to ? ERR_ALREADY_GONE_REMOTE : 
	send_user_info_packet(PKT_COM_JOIN,u,gid)); 
}




/*** User wants to join a user on a remote server ***/
int cl_server::send_ujoin(cl_user *u, uint16_t uid)
{
return (u->server_to ? ERR_ALREADY_GONE_REMOTE : 
	send_user_info_packet(PKT_COM_UJOIN,u,uid)); 
}




/*** Got request for a remote user to join a local group ***/
int cl_server::recv_join()
{
cl_server *svr;
pkt_user_info *pkt;
cl_group *grp;
cl_user *u;
cl_remote_user *ru;
uint16_t uid,remote_uid,gid,start_id;
int ret,ranid;
char path[MAXPATHLEN];

pkt=(pkt_user_info *)buff;

if (pkt->len < PKT_USER_INFO_SIZE ||
    pkt->namelen + pkt->desclen > pkt->len - (PKT_USER_INFO_SIZE - 1)) {
	log(1,"ERROR: Invalid length join packet from server '%s'",name);
	send_join_reply(ERR_PACKET_ERROR,pkt->uid,pkt->gid_ruid);
	return ERR_RECEIVE;
	}

remote_uid = ntohs(pkt->uid);

// User shouldn't exist on here yet.
if ((u=svr_get_remote_user(remote_uid))) {
	if (SYS_FLAGISSET(SYS_FLAG_LOG_UNEXPECTED_PKTS))
		log(1,"WARNING: Got join packet for pre-existing user %04X from server '%s'\n",remote_uid,name);
	return send_join_reply(ERR_USER_ALREADY_EXISTS,pkt->uid,pkt->gid_ruid);
	}

// If lockout level > remote_user_max_level then no remote users
if (lockout_level > remote_user_max_level) 
	return send_join_reply(ERR_LOCKED_OUT,pkt->uid,pkt->gid_ruid);

// Check user limit not reached
if (remote_user_count >= max_remote_users)
	return send_join_reply(ERR_MAX_REMOTE_USERS,pkt->uid,pkt->gid_ruid);

// Check if level high enough
if (pkt->orig_level < lockout_level) 
	return send_join_reply(ERR_LEVEL_TOO_LOW,pkt->uid,pkt->gid_ruid);

// Check if max hop count exceeded
if ((int)(pkt->hop_count)+1 > max_hop_cnt)
	return send_join_reply(ERR_MAX_HOP_CNT,pkt->uid,pkt->gid_ruid);

// If its to join a specific user get their group
if (pkt->type == PKT_COM_UJOIN) {
	if (!(u = get_user(ntohs(pkt->gid_ruid)))) 
		return send_join_reply(ERR_NO_SUCH_USER,pkt->uid,pkt->gid_ruid);
	gid = u->group->id;
	}
else gid = ntohs(pkt->gid_ruid);

// If its zero then it means join the remotes home group
if (!gid) {
	grp = remotes_home_group;
	gid = remotes_home_group->id;
	pkt->gid_ruid = htons(gid); // For sending any errors
	}
else {
	if (!(grp = get_group(gid))) 
		return send_join_reply(
			ERR_NO_SUCH_GROUP,pkt->uid,pkt->gid_ruid);

	if (grp == gone_remote_group || grp == prison_group)
		return send_join_reply(
			ERR_RESTRICTED_GROUP,pkt->uid,pkt->gid_ruid);

	if (O_FLAGISSET(grp,GROUP_FLAG_PRIVATE)) 
		return send_join_reply(
			ERR_PRIVATE_GROUP,pkt->uid,pkt->gid_ruid);
	}
pkt->orig_uid = ntohs(pkt->orig_uid);

// Check total ban
if (user_is_banned(pkt->orig_uid,pkt->home_addr.ip4,pkt->home_port))
	return send_join_reply(ERR_USER_BANNED,pkt->uid,pkt->gid_ruid);

// Check for group ban
if (grp->user_is_banned(pkt)) 
	return send_join_reply(ERR_BANNED_FROM_GROUP,pkt->uid,pkt->gid_ruid);

// Check for loopback if we're not allowing it. First check local users
// trying to do it.
if (!SYS_FLAGISSET(SYS_FLAG_LOOPBACK_USERS)) {
	// See if users home address matches an interface we're connected to
	// a remote server by. Ie if they're originally from here.
	if (tcp_port[PORT_TYPE_USER] == ntohs(pkt->home_port) &&
	    get_user(pkt->orig_uid)) {
		FOR_ALL_SERVERS(svr) {
			if (svr->stage == SERVER_STAGE_CONNECTED &&
			    svr->local_ip_addr.sin_addr.s_addr == pkt->home_addr.ip4) 
				return send_join_reply(
					ERR_LOOPBACK,pkt->uid,pkt->gid_ruid);
			}
		}

	// Check for users not originally from here trying to loopback
	FOR_ALL_USERS(u) {
		ru = (cl_remote_user *)u;
		if (u->type == USER_TYPE_REMOTE &&
		    get_remote_user(
			pkt->orig_uid,pkt->home_addr.ip4,pkt->home_port))
			return send_join_reply(
				ERR_LOOPBACK,pkt->uid,pkt->gid_ruid);
		}
	}

/* Can get users from same server that have the same low 12 bits 
   eg 0FFF, 1FFF. They would be given the same id number on here 
   which is an error so see if that uid is already in use. */
uid = (remote_uid & MAX_LOCAL_USER_ID) | (id << 12);
if (get_user(uid)) {
	if (!SYS_FLAGISSET(SYS_FLAG_RANDOM_REM_IDS)) 
		return send_join_reply(ERR_DUPLICATE_ID,pkt->uid,pkt->gid_ruid);
	// Create random id start point
	start_id = uid = ((random() % (MAX_LOCAL_USER_ID-MIN_LOCAL_USER_ID)) +
	                  MIN_LOCAL_USER_ID);

	// Loop until free one found
	while(get_user(uid | (id << 12))) {
		if (++uid > MAX_LOCAL_USER_ID) uid = MIN_LOCAL_USER_ID;
		if (uid == start_id)
			return send_join_reply(
				ERR_NO_FREE_IDS,pkt->uid,pkt->gid_ruid);
		}

	uid |= (id << 12);
	ranid = 1;
	}
else ranid = 0;

// Create new remote user
ru = new cl_remote_user(uid,this,pkt);
if (ru->error != OK) {
	delete ru;
	log(1,"ERROR: Failed to create remote user object %04X for %04X@%s: %s\n",
		uid,pkt->orig_uid,name,err_string[ru->error]);
	return send_join_reply(ERR_CANT_CREATE_OBJECT,pkt->uid,pkt->gid_ruid);
	}

if ((ret=send_join_reply(OK,pkt->uid,pkt->gid_ruid)) != OK) return ret;
if (ranid)
	ru->infoprintf("Due to duplicate ids you have been assigned the id %04X\n",uid);
	
// Display post login screen , join group and prompt
sprintf(path,"%s/%s",ETC_DIR,POSTLOGIN_SCREEN);
if ((ret=ru->page_file(path,0)) != OK)
	ru->errprintf("Can't page post-login screen: %s\n\n",err_string[ret]);

ru->uprintf("\n~FTYour level is:~RS %s\n",user_level[ru->level]);

grp->join(ru);
ru->prompt();
return OK;
}




/*** Send a join reply packet ***/
int cl_server::send_join_reply(u_char error, uint16_t nuid, uint16_t ngid)
{
return send_id_pair(PKT_REP_JOIN,error,nuid,ngid);
}




/*** Got reply about one of our users joining a group or user on the 
     remote server ***/
int cl_server::recv_join_reply()
{
pkt_generic1 *pkt;
cl_user *u;

pkt = (pkt_generic1 *)buff;
if (pkt->len != PKT_GENERIC1_SIZE2) {
	log(1,"ERROR: Invalid length join/ujoin reply packet from server '%s'",
		name);
	return ERR_RECEIVE;
	}

pkt->id1 = ntohs(pkt->id1);
pkt->id2 = ntohs(pkt->id2);

// Get user object.
if (!(u = get_user(pkt->id1))) {
	if (SYS_FLAGISSET(SYS_FLAG_LOG_UNEXPECTED_PKTS))
		log(1,"WARNING: Join/ujoin reply packet for unknown user %04X from server '%s'",pkt->id1,name);
	return OK;
	}

if (pkt->erf != OK) {
	u->uprintf("~FYYour request to join group/user ~FT%04X@%s~FY failed: Error %d: %s\n",
		pkt->id2,
		name,
		pkt->erf,
		pkt->erf < NUM_ERRORS ? err_string[pkt->erf] : "Unknown error");
	return OK;
	}

/* If user has already gone remote but we've got a reply packet then some
   server has been delayed and received a join packet after the user got
   bored and went somewhere else. Just tell remote server user has left. */ 
if (u->server_to) return send_leave(pkt->id1);

// User has joined a remote group so put him in the gone remote group
gone_remote_group->join(u,this,pkt->id2);

return OK;
}



////////////// LEAVE //////////////

/*** Sent by local server when user joins a local group, logs off or uses
     the leave command. If he joins another group on the remote server the 
     join group command takes care of leaving his current remote group. ***/
int cl_server::send_leave(uint16_t uid)
{
return send_uid(PKT_INF_LEAVE,uid);
}




/*** A remote user wants to leave this server. If user doesn't 
     exist just ignore it since remote server will consider them left
     anyway so nothing we send back makes any difference. ***/
int cl_server::recv_leave()
{
cl_remote_user *ru;
pkt_generic2 *pkt;

pkt = (pkt_generic2 *)buff;
pkt->id = ntohs(pkt->id);
if (pkt->len != PKT_GENERIC2_SIZE) {
	log(1,"ERROR: Invalid length leave packet from '%s'",name);
	return ERR_RECEIVE;
	}
if (!(ru = svr_get_remote_user(pkt->id))) {
	if (SYS_FLAGISSET(SYS_FLAG_LOG_UNEXPECTED_PKTS))
		log(1,"WARNING: Leave packet for unknown user %04X@%s",pkt->id,name);
	}
else delete ru;
return OK;
}



////////////// LEFT //////////////


/*** Sends a left packet ***/
int cl_server::send_left(uint16_t uid)
{
return send_uid(PKT_INF_LEFT,uid);
}




/*** Received from a remote server when a user has left it ***/
int cl_server::recv_left()
{
pkt_generic2 *pkt;
cl_user *u;
int inv;

pkt=(pkt_generic2 *)buff;
if (pkt->len != PKT_GENERIC2_SIZE) {
	log(1,"ERROR: Invalid length left packet from server '%s'",name);
	return ERR_RECEIVE;
	}

pkt->id = ntohs(pkt->id);
if (!(u = get_user(pkt->id))) {
	if (SYS_FLAGISSET(SYS_FLAG_LOG_UNEXPECTED_PKTS))
		log(1,"WARNING: Left packet for unknown user %04X from server '%s'",pkt->id,name);
	return OK;
	}

// Set flag so we don't send a leave packet back to server that just
// told us the user had left
O_SETFLAG(u,USER_FLAG_LEFT);
if (u->prev_group && 
    u->prev_group != u->home_group && u->prev_group->user_can_join(u,&inv)) {
	if (inv != -1) u->invite[inv].grp = NULL;
	u->uprintf("~NPYou are returned to group %04X.\n",u->prev_group->id);
	u->prev_group->join(u);
	}
else {
	u->uprintf("~NPYou are returned to your home group.\n");
	u->home_group->join(u);
	}
O_UNSETFLAG(u,USER_FLAG_LEFT);

// Set recv time so timeout doesn't cut in too early
u->last_input_time = server_time;

u->prompt();
return OK;
}



////////////// INPUT //////////////

/*** Send some user input ***/
int cl_server::send_input(uint16_t uid, char *str)
{
pkt_generic2 *pkt;
int len,dlen,ret;

if (stage != SERVER_STAGE_CONNECTED) return ERR_SERVER_NOT_CONNECTED;

if ((dlen=strlen(str)) > 65535 - PKT_GENERIC2_SIZE) 
	dlen = 65535 - PKT_GENERIC2_SIZE;
len = PKT_GENERIC2_SIZE + dlen;

if (!(pkt = (pkt_generic2 *)malloc(len))) return ERR_MALLOC;

pkt->len = htons((uint16_t)len);
pkt->type = PKT_COM_INPUT;
pkt->id = htons(uid);
memcpy(pkt->data,str,dlen);

ret=swrite((u_char *)pkt,len);
free(pkt);
return ret;
}




/*** Received some input for remote user. If user has hopped to another 
     server then forwarding the data is taken care of in cl_user::parse_line()
 ***/
int cl_server::recv_input()
{
pkt_generic2 *pkt;
cl_remote_user *ru;
int dlen;

pkt = (pkt_generic2 *)buff;
if (pkt->len < PKT_GENERIC2_SIZE) {
	log(1,"ERROR: Short input packet from server '%s'",name);
	return ERR_RECEIVE;
	}

dlen = pkt->len - PKT_GENERIC2_SIZE;
if (dlen > ARR_SIZE - 1) dlen = ARR_SIZE - 1;
pkt->id = ntohs(pkt->id);

// Find user
if (!(ru = svr_get_remote_user(pkt->id))) {
	if (SYS_FLAGISSET(SYS_FLAG_LOG_UNEXPECTED_PKTS))
		log(1,"WARNING: Input packet for unknown user %04X@%s",
			pkt->id,name);
	return OK;
	}
memcpy(ru->tbuff,pkt->data,dlen);
ru->tbuff[dlen]='\0';
return ru->uread();
}



////////////// PRINT //////////////

/*** Wrapper for the send_print() function. If an error occurs this function
     can either throw an exception or just return a value ***/
int cl_server::svrprintf(
	int throw_on_error, uint16_t ruid, u_char add_prefix, char *fmtstr, ...)
{
char str[ARR_SIZE];
va_list args;
int ret;

va_start(args,fmtstr);
vsnprintf(str,ARR_SIZE,fmtstr,args);
va_end(args);

if ((ret = send_print(ruid,add_prefix,str)) != OK) {
	if (throw_on_error) throw ret;
	return ret;
	}
return OK;
}




/*** Send a print statement to remote user ***/
int cl_server::send_print(uint16_t ruid, u_char add_prefix, char *str)
{
pkt_generic2 *pkt;
int len,dlen,ret;

if (stage != SERVER_STAGE_CONNECTED) return ERR_SERVER_NOT_CONNECTED;

if ((dlen=strlen(str)) > 65535 - PKT_GENERIC2_SIZE) 
	dlen = 65535 - PKT_GENERIC2_SIZE;
len = PKT_GENERIC2_SIZE + dlen;

if (!(pkt = (pkt_generic2 *)malloc(len))) return ERR_MALLOC;
pkt->len = htons((uint16_t)len);
pkt->type = PKT_COM_PRINT;
pkt->add_prefix = add_prefix; 
pkt->id = htons(ruid);
memcpy(pkt->data,str,dlen);

ret=swrite((u_char *)pkt,len);
free(pkt);
return ret;
}




/*** Received a print statement for user. If user is remote then remote users
     uprintf() method will take care of sending it back down the line ***/
int cl_server::recv_print()
{
pkt_generic2 *pkt;
cl_user *u;
int dlen;

pkt=(pkt_generic2 *)buff;

// Min length must be PKT_GENERIC2_SIZE + 1 char data
if (pkt->len < PKT_GENERIC2_SIZE + 1) {
	log(1,"ERROR: Short print packet from server '%s'",name);
	return ERR_RECEIVE;
	}

dlen = pkt->len - PKT_GENERIC2_SIZE;
if (dlen > ARR_SIZE - 1) dlen = ARR_SIZE - 1;
pkt->id = ntohs(pkt->id);
memcpy(text,pkt->data,dlen);
text[dlen]='\0';

if (!(u=get_user(pkt->id))) {
	if (SYS_FLAGISSET(SYS_FLAG_LOG_UNEXPECTED_PKTS))
		log(1,"WARNING: Print packet for unknown user %04X from server '%s'",pkt->id,name);
	}
else {
	// See if text is to be prefixed with origin
	if (pkt->add_prefix) u->uprintf("~FTSVR %s:~RS %s",name,text);
	else u->uprintf(text);
	}
return OK;
}



////////////// FIND USER //////////////

/*** Send a find request for a given user ***/
int cl_server::send_find_user(uint16_t requesting_uid, uint16_t search_uid)
{
return send_id_pair(PKT_COM_FIND_USER,0,htons(requesting_uid),htons(search_uid));
}




/*** Got a request to find a user , find them and send reply ***/
int cl_server::recv_find_user()
{
pkt_generic1 *pkt;
cl_user *u;
uint16_t gid;
u_char result;

pkt=(pkt_generic1 *)buff;
if (pkt->len != PKT_GENERIC1_SIZE2) {
	log(1,"ERROR: Invalid length find user packet from server '%s'",name);
	return ERR_RECEIVE;
	}
if (!(u=get_user(ntohs(pkt->id2)))) {
	result=USER_NOT_FOUND;  gid=0;
	}
else {
	gid=u->group->id;
	result=(u->server_to ? USER_FOUND_REMOTE : USER_FOUND);
	}
return send_find_user_reply(result,pkt->id1,pkt->id2);
}




/*** Send a reply ***/
int cl_server::send_find_user_reply(
	u_char res, uint16_t requesting_uid, uint16_t search_uid)
{
return send_id_pair(PKT_REP_FIND_USER,res,requesting_uid,search_uid);
}




/*** Received a reply ***/
int cl_server::recv_find_user_reply()
{
pkt_generic1 *pkt;
cl_friend *frnd;
cl_user *u;
uint16_t ruid,suid;
int ret;

pkt=(pkt_generic1 *)buff;
if (pkt->len != PKT_GENERIC1_SIZE2) {
	log(1,"ERROR: Invalid length find user reply packet from server '%s'",
		name);
	return ERR_RECEIVE;
	}

ruid = ntohs(pkt->id1);
suid = ntohs(pkt->id2);

// Get local user to send info to. If not found he may have logged off.
if (!(u=get_user(ruid))) return OK; 

// Check friend is still in users list
FOR_ALL_USERS_FRIENDS(u,frnd) 
	if (frnd->id == suid && frnd->server == this) break;
if (!frnd) return OK;

switch(pkt->erf) {
	case USER_NOT_FOUND:
	u->infoprintf("Your friend ~FT%04X@%s~RS is not logged on.\n",suid,name);
	frnd->stage = FRIEND_OFFLINE;
	return OK;

	case USER_FOUND:
	u->infoprintf("Your friend ~FT%04X@%s~RS is logged on.\n",suid,name);
	frnd->stage = FRIEND_ONLINE;
	break;

	case USER_FOUND_REMOTE:
	u->infoprintf("Your friend ~FT%04X@%s~RS is logged on but is on a remote server.\n",suid,name);
	frnd->stage = FRIEND_ONLINE;
	break;

	default:
	log(1,"ERROR: Invalid result type %d in find user reply packet from server '%s'.",pkt->erf,name);
	return ERR_RECEIVE;
	}

if ((ret=send_req_user_info(suid)) != OK) 
	log(1,"ERROR: cl_server::recv_find_user_reply() -> cl_server::send_req_user_info(): %s",err_string[ret]);
return OK;
}




////////////// LOGON/LOGOFF NOTIFY //////////////


/*** Send a logon notify ***/
int cl_server::send_logon_notify(uint16_t uid)
{
return send_uid(PKT_INF_LOGON_NOTIFY,uid);
}




/*** Received when a user logs on to a remote server ***/
int cl_server::recv_logon_notify()
{
pkt_generic2 *pkt;
cl_friend *frnd;
cl_user *u;
int ret,req_sent;

pkt=(pkt_generic2 *)buff;
if (pkt->len != PKT_GENERIC2_SIZE) {
	log(1,"ERROR: Invalid length logon notify packet from server '%s'",name);
	return ERR_RECEIVE;
	}
pkt->id = ntohs(pkt->id);

// Inc users counts
if ((pkt->id & 0xF000)) remote_user_cnt++; else local_user_cnt++;

req_sent=0;
FOR_ALL_USERS(u) {
	FOR_ALL_USERS_FRIENDS(u,frnd) {
		/* Don't check ->server directly since it may not be set for
		   offline friends. Friends at locating stage will be dealt
		   with by find user packets */
		if (frnd->id == pkt->id &&
		    (frnd->stage == FRIEND_OFFLINE || 
		     frnd->stage == FRIEND_UNKNOWN) &&
		    frnd->svr_name && 
		    !strcasecmp(frnd->svr_name,name)) {
			u->infoprintf("Your friend ~FT%04X@%s~RS has logged on to the remote server.\n",frnd->id,frnd->svr_name);
			frnd->server = this;
			frnd->stage = FRIEND_ONLINE;

			// Only need to send request for info of this user once
			if (!req_sent) {
				if ((ret=send_req_user_info(pkt->id)) != OK) 
					log(1,"ERROR: cl_server::recv_logon_notify() -> cl_server::send_req_user_info(): %s",err_string[ret]);
				req_sent=1;
				}
			}
		}
	}
return OK;
}




/*** Sends a logoff notify ***/
int cl_server::send_logoff_notify(uint16_t uid)
{
return send_uid(PKT_INF_LOGOFF_NOTIFY,uid);
}




/*** Received when a user logs off a remote server ***/
int cl_server::recv_logoff_notify()
{
pkt_generic2 *pkt;
cl_friend *frnd;
cl_user *u;

pkt=(pkt_generic2 *)buff;
if (pkt->len != PKT_GENERIC2_SIZE) {
	log(1,"ERROR: Invalid length logoff notify packet from server '%s'",name);
	return ERR_RECEIVE;
	}
pkt->id = ntohs(pkt->id);

// Decrement users counts
if ((pkt->id & 0xF000)) remote_user_cnt--; else local_user_cnt--;

FOR_ALL_USERS(u) {
	FOR_ALL_USERS_FRIENDS(u,frnd) {
		if (frnd->id == pkt->id &&
		    frnd->stage == FRIEND_ONLINE &&
		    frnd->server == this) {
			u->infoprintf("Your friend ~FT%04X@%s~RS has logged off from the remote server.\n",frnd->id,frnd->svr_name);
			frnd->server = NULL;
			frnd->remote_gid = 0;
			frnd->stage = FRIEND_OFFLINE;
			}
		}
	}
return OK;
}




////////////// USER INFO //////////////

/*** Send packet for info for a user ***/
int cl_server::send_req_user_info(uint16_t uid)
{
return send_uid(PKT_COM_REQ_USER_INFO,uid);
}




/*** Remote server wants info for a given user ***/
int cl_server::recv_req_user_info()
{
pkt_generic2 *pkt;
cl_user *u;

pkt=(pkt_generic2 *)buff;
if (pkt->len != PKT_GENERIC2_SIZE) {
	log(1,"ERROR: Invalid length request user info packet from server '%s'",name);
	return ERR_RECEIVE;
	}
pkt->id = ntohs(pkt->id);

// If user not there then ignore since we don't send error packet
if ((u=get_user(pkt->id))) send_user_info(u);
return OK;
}




/*** Send into about a given user ***/
int cl_server::send_user_info(cl_user *u)
{
return send_user_info_packet(PKT_INF_USER_INFO,u,u->group->id); 
}




/*** Got some info about a user. This is currently only used for getting 
     info on friends of users and setting a users name & desc if they 
     changed them on theif home servers. ***/
int cl_server::recv_user_info()
{
pkt_user_info *pkt;
uint16_t uid;
cl_user *u;
cl_friend *frnd;
cl_remote_user *ru;
int ret,dlen;

pkt=(pkt_user_info *)buff;

if (pkt->len < PKT_USER_INFO_SIZE) {
	log(1,"ERROR: Invalid length user info packet from server '%s'",name);
	return ERR_RECEIVE;
	}
uid = ntohs(pkt->uid);

// Find any friends this may apply to
FOR_ALL_USERS(u) {
	FOR_ALL_USERS_FRIENDS(u,frnd) {
		if (frnd->id == uid &&
		    frnd->server == this && 
		    frnd->stage == FRIEND_ONLINE) {
			if ((ret=frnd->set_info(this,pkt)) != OK)
				log(1,"ERROR: cl_server::recv_user_info() -> cl_friend::set_info(): %s\n",err_string[ret]);
			}
		}
	}

// If user info is about is logged on here remotely then set their name 
// & desc. 
if ((ru = svr_get_remote_user(uid))) {
	if (proto_rev > 5) dlen = pkt->desclen;
	else dlen = pkt->len - pkt->namelen - (PKT_USER_INFO_SIZE-1);

	if ((ret=ru->set_name((char *)pkt->name_desc_svr,pkt->namelen)) != OK)
		return ret;
	if ((ret=ru->set_desc((char *)pkt->name_desc_svr + pkt->namelen, dlen)) != OK)
		return ret;

	// Forward info
	send_user_info_to_servers(ru);
	}

return OK;
}



////////////// TERMSIZE ///////////////

/*** User has resized terminal ***/
int cl_server::send_termsize(cl_user *u)
{
pkt_user_termsize pkt;

if (stage != SERVER_STAGE_CONNECTED) return ERR_SERVER_NOT_CONNECTED;

pkt.len = htons(PKT_TERMSIZE_SIZE);
pkt.type = PKT_INF_TERMSIZE;
pkt.term_cols = u->term_cols > 255 ? 255 : (u_char)u->term_cols;
pkt.term_rows = u->term_rows > 255 ? 255 : (u_char)u->term_rows;
pkt.uid = htons(u->id);
return swrite((u_char *)&pkt,PKT_TERMSIZE_SIZE);
}



/*** Recieved resized terminal info ***/
int cl_server::recv_termsize()
{
pkt_user_termsize *pkt;
cl_remote_user *ru;

pkt=(pkt_user_termsize *)buff;

if (pkt->len != PKT_TERMSIZE_SIZE) {
	log(1,"ERROR: Invalid length user termsize packet from server '%s'",name);
	return ERR_RECEIVE;
	}
pkt->uid = ntohs(pkt->uid);

if (!(ru = svr_get_remote_user(pkt->uid))) {
	if (SYS_FLAGISSET(SYS_FLAG_LOG_UNEXPECTED_PKTS))
		log(1,"WARNING: User info packet for unknown user %04X@%s",
			pkt->uid,name);
	return OK;
	}

ru->term_cols = pkt->term_cols;
ru->term_rows = pkt->term_rows;

// If user has gone remote again forward details to next server
return (ru->server_to ? ru->server_to->send_termsize(ru) : OK);
}



//////////////// USER FLAGS ////////////////////

/*** Send user flags upstream to remote servers ***/
int cl_server::send_user_flags(cl_user *u)
{
pkt_user_flags pkt;

if (stage != SERVER_STAGE_CONNECTED) return ERR_SERVER_NOT_CONNECTED;

pkt.len = htons(PKT_USER_FLAGS_SIZE);
pkt.type = PKT_INF_USER_FLAGS;
pkt.pad = 0;
pkt.flags = htonl(u->flags);
pkt.uid = htons(u->id);
return swrite((u_char *)&pkt,PKT_USER_FLAGS_SIZE);
}




/*** Recieved info about changed user flags from downstream ***/
int cl_server::recv_user_flags()
{
pkt_user_flags *pkt;
cl_remote_user *ru;

// Don't do anything since I've swapped the way this operates and I can't
// be arsed to make this feature backwards compatible.
if (proto_rev < 6) return OK;

pkt = (pkt_user_flags *)buff;
if (pkt->len != PKT_USER_FLAGS_SIZE) {
	log(1,"ERROR: Short user flag packet from server '%s'",name);
	return ERR_RECEIVE;
	}
pkt->uid = ntohs(pkt->uid);
if (!(ru = svr_get_remote_user(pkt->uid))) {
	if (SYS_FLAGISSET(SYS_FLAG_LOG_UNEXPECTED_PKTS))
		log(1,"WARNING: Flags packet for unknown user %04X from server '%s'",pkt->uid,name);
	return OK;
	}

// Set flags.
ru->prev_flags = ru->flags;
ru->flags = ntohl(pkt->flags) & REMOTE_IGNORE_FLAGS_MASK;
ru->flags |= ru->prev_flags & ~REMOTE_IGNORE_FLAGS_MASK;

// Forward packet onwards if user has gone on further
if (ru->server_to && ru->server_to->proto_rev > 5)
	return ru->server_to->send_user_flags(ru);

return OK;
}



/////////////// USER CHANGED GROUP ////////////////

/*** Send group change packet ***/
int cl_server::send_group_change(cl_user *u)
{
return send_id_pair(PKT_INF_GROUP_CHANGE,0,htons(u->id),htons(u->group->id));
}




/*** Receive group change packet ***/
int cl_server::recv_group_change()
{
pkt_generic1 *pkt;
cl_user *u;
cl_friend *frnd;
 
pkt = (pkt_generic1 *)buff;
if (pkt->len != PKT_GENERIC1_SIZE2) {
	log(1,"ERROR: Invalid length group change packet from server '%s'",name);
	return ERR_RECEIVE;
	}
pkt->id1 = ntohs(pkt->id1);
pkt->id2 = ntohs(pkt->id2);

FOR_ALL_USERS(u) {
	FOR_ALL_USERS_FRIENDS(u,frnd) {
		if (frnd->id == pkt->id1 && 
		   frnd->server == this && frnd->stage == FRIEND_ONLINE)
			frnd->remote_gid = pkt->id2;
		}
	}
return OK;
}



////////////// EXAMINE //////////////

/*** Send examine request ***/
int cl_server::send_examine(uint16_t requesting_uid, uint16_t exa_uid)
{
return send_id_pair(PKT_COM_EXAMINE,0,htons(requesting_uid),htons(exa_uid));
}




/*** Receives request to examine a user and sends reply ***/
int cl_server::recv_examine()
{
pkt_generic1 *pkt;
cl_user *u;
cl_local_user *lu;
cl_remote_user *ru;
cl_group *grp;
cl_mail *tmail;
uint16_t req_uid, exa_uid;

pkt = (pkt_generic1 *)buff;
if (pkt->len != PKT_GENERIC1_SIZE2) {
	log(1,"ERROR: Invalid length examine packet from server '%s'",name);
	return ERR_RECEIVE;
	}
req_uid = ntohs(pkt->id1);
exa_uid = ntohs(pkt->id2);

if (!(u=get_user(exa_uid)) && !(u = create_temp_user(exa_uid))) {
	svrprintf(0,req_uid,1,"User %04X does not exist.\n~PR",exa_uid);
	return OK;
	}

try {
	svrprintf(1,req_uid,0,"\n");
	svrprintf(1,req_uid,1,"~BB*** Details of user %04X ***\n",exa_uid);
	svrprintf(1,req_uid,1,"\n");
	svrprintf(1,req_uid,1,"Name              : %s\n",u->name);
	svrprintf(1,req_uid,1,"Description       : %s\n",u->desc);
	svrprintf(1,req_uid,1,"User type         : %s\n",user_type[u->type]);
	svrprintf(1,req_uid,1,"User level        : %s\n",user_level[u->level]);


	if (O_FLAGISSET(u,USER_FLAG_TEMP_OBJECT)) {
		lu = (cl_local_user *)u;
		// Temp objects are always local users
		tmail = NULL; // set in case exception thrown
		svrprintf(1,req_uid,1,"Logged off at     : %s",
			ctime(&(lu->logoff_time)));
		svrprintf(1,req_uid,1,"Session duration  : %s\n",
			time_period(lu->session_duration));

		grp = get_group(lu->start_group_id);
		svrprintf(1,req_uid,1,"Start group       : %04X, %s\n",
			lu->start_group_id,grp ? grp->name : "<unknown>");

		show_gmi_details(u,req_uid);

		tmail = new cl_mail(NULL,u->id);
		svrprintf(1,req_uid,1,"Unread mail mesgs : %d\n\n~PR",
			tmail->unread_msgcnt);
		delete tmail;	

		delete u;
		return OK;
		}

	// Everything below only applies to logged in users
	if (u->type == USER_TYPE_REMOTE) {
		ru = (cl_remote_user *)u;
		svrprintf(1,req_uid,1,"Server from       : %s\n",
			ru->server_from->name);
		svrprintf(1,req_uid,1,"Hop number        : %d\n",ru->hop_count);
		svrprintf(1,req_uid,1,"Home Id           : %04X\n",ru->orig_id);
		svrprintf(1,req_uid,1,"Home server name  : %s\n",
			ru->home_svrname ? ru->home_svrname : "<unknown>");
		svrprintf(1,req_uid,1,"Home server IP    : %s:%d  (%s)\n",
			u->ipnumstr,ntohs(u->ip_addr.sin_port),u->ipnamestr);
		svrprintf(1,req_uid,1,"Home user level   : %s\n",
			user_level[ru->orig_level]);
		}
	else lu = (cl_local_user *)u;

	svrprintf(1,req_uid,1,"Logged on at      : %s",ctime(&u->login_time));
	svrprintf(1,req_uid,1,"Session duration  : %s\n",
		time_period(server_time - u->login_time));
	if (u->type == USER_TYPE_LOCAL)
		svrprintf(1,req_uid,1,"Prev. session dur.: %s\n",
			time_period(lu->session_duration));

	svrprintf(1,req_uid,1,"Current group     : %04X, %s\n",
		u->group->id,u->group->name);
	svrprintf(1,req_uid,1,"Idle time         : %s\n",
		time_period(server_time - u->last_input_time));

	if (u->ping_svr)
		svrprintf(1,req_uid,1,
			"Pinging a server  : ~FYYES ~FT->~RS %s\n",
			u->ping_svr->name);
	else
		svrprintf(1,req_uid,1,"Pinging a server  : ~FGNO\n");
		
	if (u->server_to)
		svrprintf(1,req_uid,1,
			"Gone remote       : ~FYYES ~FT->~RS %s\n",
			u->server_to->name);
	else
		svrprintf(1,req_uid,1,"Gone remote       : ~FGNO\n");

	svrprintf(1,req_uid,1,"AFK               : ");
	if (u->is_afk()) {
		svrprintf(1,req_uid,0,"~FYYES~RS");
		if (u->afk_msg) svrprintf(1,req_uid,0," -> %s\n",u->afk_msg);
		else svrprintf(1,req_uid,0,"\n");
		}
	else svrprintf(1,req_uid,0,"~FGNO\n");

	show_gmi_details(u,req_uid);

	svrprintf(1,req_uid,1,"Invisible         : %s\n",
		colnoyes[O_FLAGISSET(u,USER_FLAG_INVISIBLE)]);
	svrprintf(1,req_uid,1,"Other flags       : %s\n",u->flags_list());

	if (u->type == USER_TYPE_LOCAL) {
		grp = get_group(lu->start_group_id);
		svrprintf(1,req_uid,1,"Start group       : %04X, %s\n",
			lu->start_group_id,grp ? grp->name : "<unknown>");
		svrprintf(1,req_uid,1,"Unread mail mesgs : %d\n\n~PR",
			lu->mail->unread_msgcnt);
		}
	else svrprintf(1,req_uid,0,"\n~PR");
	}

catch(int err) {
	if (O_FLAGISSET(u,USER_FLAG_TEMP_OBJECT)) {
		if (tmail) delete tmail;
		delete u;
		}
	return err;
	}

return OK;
}




/*** This is used in 2 places in recv_examine() and I don't want to have
     to duplicate this much code ***/
void cl_server::show_gmi_details(cl_user *u, uint16_t req_uid)
{
if (u->type == USER_TYPE_LOCAL)
	svrprintf(1,req_uid,1,"Home group persist: %s\n",
		O_FLAGISSET(u,USER_FLAG_HOME_GRP_PERSIST) ? "~FYYES" : "~FGNO");

svrprintf(1,req_uid,1,"Muzzled           : ");
if (O_FLAGISSET(u,USER_FLAG_MUZZLED)) {
	svrprintf(1,req_uid,0,"~FRYES\n");
	svrprintf(1,req_uid,1,"Muzzled on        : %s",
		ctime(&u->muzzle_start_time));
	svrprintf(1,req_uid,1,"Muzzle time left  : ");
		if (u->muzzle_end_time)
		svrprintf(1,req_uid,0,"%s\n",
			time_period(u->muzzle_end_time - server_time));
	else svrprintf(1,req_uid,0,"~FYINDEFINITE\n");
	}
else svrprintf(1,req_uid,0,"~FGNO\n");

svrprintf(1,req_uid,1,"Prisoner          : ");
if (O_FLAGISSET(u,USER_FLAG_PRISONER)) {
	svrprintf(1,req_uid,0,"~FRYES\n");
	svrprintf(1,req_uid,1,"Imprisoned on     : %s",
		ctime(&u->imprisoned_time));
	svrprintf(1,req_uid,1,"Sentence remaining: ");
	if (u->release_time)
		svrprintf(1,req_uid,0,"%s\n",
			time_period(u->release_time - server_time));
	else svrprintf(1,req_uid,0,"~FYINDEFINITE\n");
	}
else svrprintf(1,req_uid,0,"~FGNO\n");
}




////////////// MAIL //////////////

/*** Send a mail to a remote server ***/
int cl_server::send_mail(cl_user *ufrom, uint16_t uid_to, char *subj, char *msg)
{
pkt_mail *pkt;
uint16_t len,mlen;
u_char nlen,slen;
int ret;

if (stage != SERVER_STAGE_CONNECTED) return ERR_SERVER_NOT_CONNECTED;

nlen = (u_char)strlen(ufrom->name);
slen = (u_char)(subj ? strlen(subj) : 0);
mlen = strlen(msg);
len = PKT_MAIL_SIZE + nlen + slen + mlen;

if (!(pkt = (pkt_mail *)malloc(len))) return ERR_MALLOC;
pkt->len = htons(len);
pkt->type = PKT_COM_MAIL;
pkt->flags = 0;  // Unused for now
pkt->uid_from = htons(ufrom->id);
pkt->uid_to = htons(uid_to);
pkt->namelen = nlen;
pkt->subjlen = slen;
memcpy(pkt->data,ufrom->name,nlen);
if (subj) memcpy(pkt->data+nlen,subj,slen);
memcpy(pkt->data+nlen+slen,msg,mlen);

ret = swrite((u_char *)pkt,len);
free(pkt);
return ret;
}




/*** Received a mail from a remote server. ***/
int cl_server::recv_mail()
{
pkt_mail *pkt;
char *uname,*subj,*mesg;
uint16_t uid_from,uid_to;
int ret,mlen,slen;

pkt = (pkt_mail *)buff;
uid_from = ntohs(pkt->uid_from);
uid_to = ntohs(pkt->uid_to);

if (pkt->len < PKT_MAIL_SIZE + 1 ||
    pkt->len - pkt->namelen - pkt->subjlen < 1) {
	log(1,"ERROR: Invalid length mail packet from server '%s'",name);
	send_mail_reply(ERR_PACKET_ERROR,pkt->uid_from,pkt->uid_to);
	return ERR_RECEIVE;
	}

// Set strings
if (!(uname = (char *)malloc(pkt->namelen+1))) {
	send_mail_reply(ERR_MALLOC,pkt->uid_from,pkt->uid_to);
	return ERR_MALLOC;
	}
memcpy(uname,pkt->data,pkt->namelen);
uname[pkt->namelen] = '\0';

if (pkt->subjlen) {
	if (!(subj = (char *)malloc(pkt->subjlen+1))) {
		send_mail_reply(ERR_MALLOC,pkt->uid_from,pkt->uid_to);
		free(uname);
		return ERR_MALLOC;
		}
	slen = (pkt->subjlen > max_subject_len ? max_subject_len : pkt->subjlen);
	memcpy(subj,pkt->data + pkt->namelen,slen);
	subj[slen] = '\0';
	clean_string(subj);
	}
else subj = NULL;

mlen = pkt->len - PKT_MAIL_SIZE - pkt->namelen - pkt->subjlen;
if (!(mesg = (char *)malloc(mlen+1))) {
	FREE(subj);
	free(uname);
	send_mail_reply(ERR_MALLOC,pkt->uid_from,pkt->uid_to);
	return ERR_MALLOC;
	}
memcpy(mesg,pkt->data + pkt->namelen + pkt->subjlen,mlen);
mesg[mlen] = '\0';
clean_string(mesg);

// Deliver mail
ret = send_mail_reply(
	deliver_mail(uid_from,uname,this,uid_to,subj,mesg),
	pkt->uid_from,pkt->uid_to);

FREE(subj);
free(uname);
free(mesg);
return ret;
}




/*** Send a mail reply ***/
int cl_server::send_mail_reply(
	u_char error, uint16_t uid_from, uint16_t uid_to)
{
return send_id_pair(PKT_REP_MAIL,error,uid_from,uid_to);
}




/*** Receive a mail reply ***/
int cl_server::recv_mail_reply()
{
pkt_generic1 *pkt;
cl_user *u;

pkt = (pkt_generic1 *)buff;
if (pkt->len != PKT_GENERIC1_SIZE2) {
	log(1,"ERROR: Invalid length mail reply packet from server '%s'",name);
	return ERR_RECEIVE;
	}

pkt->id1 = ntohs(pkt->id1);
pkt->id2 = ntohs(pkt->id2);

// Get user object. If null just ignore.
if (!(u = get_user(pkt->id1))) return OK;

if (pkt->erf == OK) {
	u->infoprintf("~FYYour mail to user ~FT%04X@%s~FY has been delivered.\n",
		pkt->id2,name);
	return OK;
	}

u->infoprintf("~FYYour mail to user ~FT%04X@%s~FY failed: Error %d: %s\n",
	pkt->id2,
	name,
	pkt->erf,
	pkt->erf < NUM_ERRORS ? err_string[pkt->erf] : "Unknown error");

return OK;
}




////////////// NET BROADCAST //////////////

/*** Network broadcast packet. A lot of this code is identical to the recieve
     tell/pemote code. ***/
int cl_server::recv_net_bcast()
{
pkt_generic1 *pkt;
char *uname,*mesg,*s;
uint16_t uid_from;
int mlen;

pkt = (pkt_generic1 *)buff;

uid_from = ntohs(pkt->id1);

// Check packet validity
if (pkt->len < PKT_GENERIC1_SIZE3 + 1 || 
    pkt->namelen > pkt->len - (PKT_GENERIC1_SIZE3-1)) {
	log(1,"ERROR: Invalid length net broadcast packet from server '%s'",name);
	svrprintf(0,uid_from,1,"~OL~FRERROR:~RS Packet error, cannot deliver net broadcast packet.\n");
	return ERR_RECEIVE;
	}

// Set strings
if (!(uname = (char *)malloc(pkt->namelen+1))) return ERR_MALLOC;
memcpy(uname,pkt->name_mesg,pkt->namelen);
uname[pkt->namelen] = '\0';

mlen = pkt->len - pkt->namelen - (PKT_GENERIC1_SIZE3-2);
if (!(mesg = (char *)malloc(mlen))) {
	free(uname);  return ERR_MALLOC;
	}
memcpy(mesg,pkt->name_mesg + pkt->namelen,mlen-1);
mesg[mlen-1] = '\0';
clean_string(mesg);

// Send message to the admins and/or log it
allprintf(MSG_NETBCAST,recv_net_bcast_level,NULL,
          "\n~SN~OL~BM*** Network broadcast message from ~FT%04X@%s~RS~OL~BM (%s) ***\n\n%s\n\n",
          uid_from,name,uname,mesg);

if (SYS_FLAGISSET(SYS_FLAG_LOG_NETBCAST)) {
	// Convert any newlines to slashes
	for(s=mesg;*s;++s) if (*s == '\n') *s = '/';
	log(1,"NETWORK BROADCAST from %04X@%s (%s): %s\n",
		uid_from,name,uname,mesg);
	}

free(uname);
free(mesg);
return OK;
}


