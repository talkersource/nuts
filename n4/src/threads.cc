/***************************************************************************
 FILE: threads.cc
 LVU : 1.3.8

 DESC:
 This contains the thread creation & deletion functions and entry points.
 Due to problems with using C++ methods as thread entry points for the
 pthread_create() function (there may be a way of doing it but I couldn't
 figure it out) I created these entry functions that pthread_create() calls
 that in turn simply call the thread entry methods in the specific objects.

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


/*** Create a detached thread. I'm unable to prevent the Solaris Forte compiler
     bitching about the pthread_create() call so the makefile has a filter
     that removes its pointless gripe. ***/
int create_thread(int type,pthread_t *tid,void *((*func)(void *)),void *object)
{
pthread_attr_t attr;
int ret;

// Make all threads detached because we're not interested in joining on them
pthread_attr_init(&attr);
pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);

/* Put mutex here so if thread fails just after its created the thread can't
   try to delete itself from the list before the main thread has added it. 
   Ie: prevents race conditions */
pthread_mutex_lock(&threadlist_mutex);
if ((ret = pthread_create(tid,&attr,func,object))) {
	*tid = 0;
	pthread_mutex_unlock(&threadlist_mutex);
	return ret;
	}

// Add to threads list
add_thread_entry(type,*tid);

pthread_mutex_unlock(&threadlist_mutex);

return OK;
}




/*** Create and add a thread entry to the list ***/
void add_thread_entry(int type, pthread_t tid)
{
st_threadentry *te;

te = new st_threadentry;
te->type = type;
te->id = tid;
te->created = server_time;
te->prev = NULL;
te->next = NULL;

add_list_item(first_thread,last_thread,te);
thread_count++;
}




/*** Exit a thread ***/
void exit_thread(pthread_t *tid)
{
int status;

/* Wait until we've finished being added to list else *tid may still be
   zero in this thread and we may bugger up list too as its a shared variable
   with the main thread. */
pthread_mutex_lock(&threadlist_mutex);

if (*tid != pthread_self()) {
	log(1,"INTERNAL ERROR: tid != pthread_self() in exit_thread(). tid = %u, expected %u.\n",*tid,pthread_self());
	pthread_mutex_unlock(&threadlist_mutex);
	return;
	}
remove_thread_from_list(*tid);
thread_count--;
*tid = 0;

pthread_mutex_unlock(&threadlist_mutex);
pthread_exit(&status);
}




/*** Cancel a thread. We lock the log mutex too as we don't want the thread
     we're cancelling to quit halfway through the log() function and leave
     the mutex locked as this will freeze the whole server very quickly ***/
void cancel_thread(pthread_t *tid)
{
pthread_mutex_lock(&threadlist_mutex);
pthread_mutex_lock(&log_mutex);

// Do this check after the lock for same reasons as above.
if (*tid == pthread_self()) 
	log(1,"INTERNAL ERROR: tid == pthread_self() in cancel_thread(). tid = %u\n",*tid);
else
if (*tid) {
	remove_thread_from_list(*tid);
	pthread_cancel(*tid);
	thread_count--;
	*tid = 0;
	}

pthread_mutex_unlock(&log_mutex);
pthread_mutex_unlock(&threadlist_mutex);
}




/*** Remove entry from list ***/
void remove_thread_from_list(pthread_t tid)
{
st_threadentry *te;

FOR_ALL_THREADS(te) {
	if (te->id == tid) {
		remove_list_item(first_thread,last_thread,te);
		delete te;
		return;
		}
	}
log(1,"INTERNAL ERROR: Unable to find thread %u in list in remove_thread_from_list()\n",tid);
}




/*** Entry point for user thread. Can't call class method directly as C++
     compilers don't like passing class methods as function pointers ***/
void *user_resolve_thread_entry(void *user)
{
int dummy;

// Want thread to exit immediately when we cancel it
pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS,&dummy);
((cl_user *)user)->resolve_ip_thread();
return NULL;
}




/*** Entry point for server resolve thread ***/
void *server_resolve_thread_entry(void *svr)
{
int dummy;

pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS,&dummy);
((cl_server *)svr)->resolve_ip_thread();
return NULL;
}




/*** Entry point for server connect thread ***/
void *server_connect_thread_entry(void *svr)
{
int dummy;

pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS,&dummy);
((cl_server *)svr)->connect_thread();
return NULL;
}
