/***************************************************************************
 FILE: events.cc
 LVU : 1.4.1

 DESC:
 This is procedural code that is called every "heartbeat" seconds and does
 various housekeeping and system maintenance duties such as various timeouts
 etc.

 Copyright (C) Neil Robertson 2003,2004

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

// Local functions
void do_user_events();
void do_server_events();
void do_board_msg_expiry();
void do_autosave();
void do_shutdown_or_reboot();
void do_clock_events();


/*** Timed events ***/
void *do_events(void *arg)
{
// Linux threads are actually processes and this has to be done for each of
// them *sigh*.
#ifdef LINUX
if (be_daemon) dissociate_from_tty();
#endif

while(1) {
	// Set mutex so user input can't be dealt with while we're doing this
	pthread_mutex_lock(&events_mutex);
	time(&server_time);
	localtime_r(&server_time,&server_time_tms);

	do_user_events();
	do_server_events();
	do_board_msg_expiry();
	do_autosave();
	do_shutdown_or_reboot();
	do_clock_events();

	pthread_mutex_unlock(&events_mutex);
	sleep(HEARTBEAT);
	}
return NULL;
}




/*** Do lots of user related timed stuff ***/
void do_user_events()
{
cl_user *u,*nu;
cl_local_user *lu;
int i,secs,ret;

for(u=first_user;u;u=nu) {
	nu=u->next;

	// Check for timeouts
	if (u->type == USER_TYPE_LOCAL) {
		lu = (cl_local_user *)u;

		if (O_FLAGISSET(u,USER_FLAG_LINKDEAD) &&
		    server_time - lu->linkdead_time >= linkdead_timeout) {
			O_SETFLAG(u,USER_FLAG_TIMEOUT);
			lu->disconnect();
			continue;
			}

		if (login_timeout &&
		    u->level == USER_LEVEL_LOGIN &&
		    server_time - u->last_input_time >= login_timeout) {
			u->uprintf("\n\n~SN~BR*** TIMED OUT ***\n\n");
			O_SETFLAG(u,USER_FLAG_TIMEOUT);
			lu->disconnect();
			continue;
			}
		}

	/* Check for idle timeout. This doesn't apply if user has gone remote
	   or if they're >= idle timeout ignore level.
	   If we're 1 minute or less from a timeout then warn them. */
	if (idle_timeout &&
	    u->level < idle_timeout_ign_level &&
	    !u->server_to && 
	    u->level > USER_LEVEL_LOGIN &&
	    !O_FLAGISSET(u,USER_FLAG_LINKDEAD)) {
		secs = server_time - u->last_input_time;

		if (secs >= idle_timeout) {
			u->uprintf("\n\n~SN~BR*** TIMED OUT ***\n\n");
			allprintf(
				MSG_MISC,
				USER_LEVEL_NOVICE,
				u,
				"User ~FT%04X~RS (%s) has been timed out.\n",
				u->id,u->name);

			O_SETFLAG(u,USER_FLAG_TIMEOUT);
			u->disconnect();
			continue;
			}

		if (!O_FLAGISSET(u,USER_FLAG_TIMEOUT_WARNING) &&
		    secs >= idle_timeout - 60) {
			u->warnprintf("~SN~LIYou will be timed out in %d seconds if you do not enter some input!\n",idle_timeout - secs);
			O_SETFLAG(u,USER_FLAG_TIMEOUT_WARNING);
			}
		}

	// Expire invites
	if (group_invite_expire) {
		for(i=0;i < MAX_INVITES;++i) {
			if (u->invite[i].grp && 
			    server_time - u->invite[i].time >= group_invite_expire) {
				u->infoprintf("Your invite to group %04X (%s~RS) has expired.\n",u->invite[i].grp->id,u->invite[i].grp->name);
				u->invite[i].grp = NULL;
				}
			}
		}

	// Check for prison release. release_time should never be set if user
	// is not a prisoner but check prisoner flag too just to be safe.
	if (O_FLAGISSET(u,USER_FLAG_PRISONER) &&
	    u->release_time && server_time >= u->release_time)
		u->release_from_prison(NULL);

	// Check for unmuzzling
	if (O_FLAGISSET(u,USER_FLAG_MUZZLED) &&
	    u->muzzle_end_time && server_time >= u->muzzle_end_time)
		u->unmuzzle(NULL);

	// Do server pings
	if (u->ping_svr && (ret = u->ping_svr->send_ping(u)) != OK) {
		u->errprintf("Unable to ping server: %s\n",err_string[ret]);
		u->ping_svr = NULL;
		}
	}
}




/*** Delete any servers pending deletion and check for timeouts ***/
void do_server_events()
{
cl_server *svr,*next_svr;
time_t tdiff;

for(svr=first_server;svr;svr=next_svr) {
	next_svr = svr->next;

	switch(svr->stage) {
		case SERVER_STAGE_DELETE:
		delete svr;  continue;

		case SERVER_STAGE_INCOMING:
		case SERVER_STAGE_CONNECTING:
		if (server_time - svr->last_rx_time >= connect_timeout) {
			log(1,"Server '%s' connect has been timed out, DISCONNECTING.",svr->name);
			svr->disconnect(SERVER_STAGE_TIMEOUT);
			continue;
			}
		break;

		case SERVER_STAGE_CONNECTED: 
		/* If we didn't receive last 3 pings (and any type of TX) or
		   server didn't send ping interval and we're over our timeout
		   period then disconnect. */
		tdiff = server_time - svr->last_rx_time;
		if ((svr->ping_interval &&
		     tdiff > (time_t)svr->ping_interval * 3) ||
		    (!svr->ping_interval && tdiff >= server_timeout)) {
			log(1,"Server '%s' has been timed out, DISCONNECTING.",
				svr->name);
			svr->disconnect(SERVER_STAGE_TIMEOUT);
			continue;
			}

		// Don't use last_tx time and hence limit the number of pings
		// since the packet type sent may not have required a reply.
	    	if (server_time - svr->last_ping_time >= send_ping_interval) 
			svr->send_ping(NULL);
		break;

		case SERVER_STAGE_DISCONNECTED:
		// Server might not have been deleted if flag was toggled off
		// when it disconnected so check here
		if (svr->connection_type == SERVER_TYPE_INCOMING &&
		    SYS_FLAGISSET(SYS_FLAG_DEL_DISC_INCOMING)) {
			delete svr;  continue;
			}
		break;
			
		default: continue;
		}

	if (max_tx_errors && svr->tx_err_cnt >= max_tx_errors) {
		log(1,"Server '%s' has had %d TX errors, DISCONNECTING.",
			svr->name,svr->tx_err_cnt);
		svr->disconnect(SERVER_STAGE_NETWORK_ERROR);
		continue;
		}

	if (max_rx_errors && svr->rx_err_cnt >= max_rx_errors) {
		log(1,"Server '%s' has had %d RX errors, DISCONNECTING.",svr->name,svr->rx_err_cnt);
		svr->disconnect(SERVER_STAGE_NETWORK_ERROR);
		continue;
		}
	}
}




/*** Checks for and deletes any expired message in system & public groups.
     This won't delete anything in users own groups, thats up to them. ***/
void do_board_msg_expiry()
{
cl_group *grp;
cl_board *board;
cl_msginfo *msg,*nextmsg;
int flag,expsecs;

if (!board_msg_expire) return; // Never expire

FOR_ALL_GROUPS(grp) {
	if ((board = grp->board)) {
		// For user groups a time of zero = indefinite, for other
		// groups it means use the global lifetime.
		if (!board->expire_secs) {
			if (grp->type == GROUP_TYPE_USER) continue;
			expsecs = board_msg_expire;
			}
		else expsecs = board->expire_secs;

		// Loop through the messages
		for(msg = board->first_msg,flag=0;msg;msg = nextmsg) {
			nextmsg = msg->next;
			if (server_time - msg->create_time >= expsecs) {
				grp->gprintf(MSG_INFO,"Board message #%d (~FYFrom:~RS %s (%s), ~FYSubject:~RS %s~RS) has expired.\n",
					msg->mnum,
					msg->id,msg->name,msg->subject);
				log(1,"Board message %04X:%d (From: %s, Subject: %s) has expired.\n",
					grp->id,
					msg->mnum,msg->name,msg->subject);
				board->mdelete(msg);
				flag = 1;
				}
			}	
		if (flag && SYS_FLAGISSET(SYS_FLAG_BOARD_RENUM)) {
			grp->gprintf(MSG_INFO,"Message board renumbered.\n");
			log(1,"Board %04X renumbered.\n",grp->id);
			board->renumber();
			}
		}
	}
}




/*** Save all user info for logged on users. It might save other stuff in the
     future too but for now... ***/
void do_autosave()
{
cl_user *u;
cl_local_user *lu;
int ret;

// Don't bother if no users on or not reached time interval yet.
if (!local_user_count || server_time - autosave_time < autosave_interval)
	return;

log(1,"Autosave...");

FOR_ALL_USERS(u) {
	if (u->type == USER_TYPE_LOCAL && u->level > USER_LEVEL_LOGIN) {
		lu = (cl_local_user *)u;
		if ((ret = lu->save()) != OK) 
			log(1,"ERROR: Failed to save details of user %04X (%s): %s\n",u->id,u->name,err_string[ret]);	
		}
	}
autosave_time = server_time;

log(1,"Autosave complete.");
}




/*** Shut system down or reboot if The Time Has Come! ***/
void do_shutdown_or_reboot()
{
cl_server *svr;
cl_user *u,*nu;
cl_group *grp;
int diff;

if (shutdown_type == SHUTDOWN_INACTIVE) return;

if (server_time >= shutdown_time) {
	log(1,"%s STARTED...",shutdown_type == SHUTDOWN_STOP ? "SHUTDOWN" : "REBOOT");
	allprintf(
		MSG_SYSTEM,
		USER_LEVEL_NOVICE,
		NULL,"~OL~LI~FR%s NOW!\n",
		shutdown_type == SHUTDOWN_STOP ? "SHUTTING DOWN" : "REBOOTING");


	// Send net broadcast then disconnect remote servers.
	FOR_ALL_SERVERS(svr) {
		if (svr->stage == SERVER_STAGE_CONNECTED) {
			// We don't care about return value since not much we
			// can do anyway if it fails.
			svr->send_mesg(
				PKT_INF_BCAST,
				0,0,0,
				SYS_USER_NAME,"System shutting down NOW!");

			allprintf(
				MSG_SYSTEM,
				USER_LEVEL_NOVICE,NULL,"Disconnecting server '%s'...\n",
				svr->name);
			svr->disconnect(SERVER_STAGE_SHUTDOWN);
			}
		}

	allprintf(
		MSG_SYSTEM,
		USER_LEVEL_NOVICE,NULL,"Logging off users...\n");

	// Write shutdown to group logs
	FOR_ALL_GROUPS(grp) 
		grp->grouplog(false,"~BR********************** SYSTEM SHUTDOWN **********************\n");

	// Disconnect all users 
	for(u=first_user;u;u=nu) {
		nu = u->next;
		u->sysprintf("You are being disconnected...\n");

		// Skip anything in disconnect stage and just delete
		delete u;
		}

	// Delete any remaining public groups (just so we get messages in 
	// the group logs)
	FOR_ALL_GROUPS(grp) delete grp;

	if (shutdown_type == SHUTDOWN_STOP) {
		// Just stop
		log(1,"SHUTDOWN COMPLETE.");
		exit(0);
		}
	else {
		/* Close listen sockets and pause briefly to let them be
		   free'd so we don't get any port is in use error messages
		   upon reboot */
		close(listen_sock[PORT_TYPE_USER]);   
		if (SYS_FLAGISSET(SYS_FLAG_SVR_LISTEN))
			close(listen_sock[PORT_TYPE_SERVER]);
		sleep(2);

		// Attempt reboot
		log(1,"REBOOTING...\n");   
		if (chdir(original_dir) == -1)
			log(1,"ERROR: Failed to cd to original directory: %s\n",strerror(errno));
		execvp(global_argv[0],global_argv);
		log(1,"INTERNAL ERROR: REBOOT FAILED: %s\n",strerror(errno));
		exit(1);
		}
	}

// Display some warnings
diff = shutdown_time - server_time;

// <= 10 seconds
if (diff <= 10) goto USER_WARNING;

// <= 1 minute
if (diff <= 60) {
	if (diff == 60) goto WARNING;
	if (diff % 10 < HEARTBEAT) goto USER_WARNING;
	return;
	}

// <= 10 minutes
if (diff <= 600) {
	if (diff % 60 < HEARTBEAT) goto USER_WARNING;
	return;
	}

// <= 1 hour
if (diff <= 3600) {
	if (diff % 600 < HEARTBEAT) goto WARNING;
	return;
	}

// <= 1 day 
if (diff <= 86400 && diff % 3600 < HEARTBEAT) goto WARNING;
return;

WARNING:
// Warn remote servers
sprintf(text,"System %s in %s.",
	shutdown_type == SHUTDOWN_STOP ? "shutdown" : "reboot",
	time_period(diff));

FOR_ALL_SERVERS(svr) {
	if (svr->stage == SERVER_STAGE_CONNECTED) 
		svr->send_mesg(PKT_INF_BCAST,0,0,0,SYS_USER_NAME,text);
	}

USER_WARNING:
// Warn logged on users
allprintf(
	MSG_SYSTEM,
	USER_LEVEL_NOVICE,
	NULL,
	"%s in %s.\n",
	shutdown_type == SHUTDOWN_STOP ? "Shutdown" : "Reboot",
	time_period(diff));
}




/*** This function will carry out events that need to happen at a certain
     time. Currently all we do is to reset the board & mail daily message
     counters at midnight ***/
void do_clock_events()
{
cl_group *grp;
cl_user *u;
static int done = 0;

if (!server_time_tms.tm_hour && !server_time_tms.tm_min) {
	if (!done) {
		FOR_ALL_GROUPS(grp)
			if (grp->board) grp->board->todays_msgcnt = 0;

		FOR_ALL_USERS(u) {
			// ->mail will not be set if user is logging in
			if (u->type == USER_TYPE_LOCAL && 
			    ((cl_local_user *)u)->mail) 
				((cl_local_user *)u)->mail->todays_msgcnt = 0;
			}
		done = 1;
		}
	}
else done = 0;
}
