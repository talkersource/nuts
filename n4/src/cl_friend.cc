/***************************************************************************
 FILE: cl_friend.cc
 LVU : 1.3.8

 DESC:
 Each instance of this class contains the info on a single user friend. A
 linked list of them is stored in a cl_user object.

 Copyright (C) Neil Robertson 2003

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


/*** Constructor ***/
cl_friend::cl_friend(cl_user *owner, uint16_t fid, char *svrname, int linenum)
{
cl_user *u;
int ret;

id = fid;
remote_gid = 0;
home_ip4_addr = 0;
home_port = 0;
server = NULL;
local_user = NULL;
name = NULL;
desc = NULL;
stage = FRIEND_UNKNOWN;
prev = NULL;
next = NULL;

// If remote friend then set up for locating
if (svrname) {
	utype = USER_TYPE_REMOTE;
	svr_name = strdup(svrname);

	if (!linenum) owner->uprintf("Friend ~FT%04X@%s~RS added.\n",fid,svrname);

	if (!(server = get_server(svrname)))
		owner->infoprintf("Unable to send find request for friend ~FT%04X@%s~RS: No such server.\n",id,svrname);
	else {
		if ((ret=server->send_find_user(owner->id,id)) == OK) {
			owner->uprintf("Locating friend ~FT%04X@%s~RS...\n",
				id,svrname);
			stage = FRIEND_LOCATING;
			}
		else owner->infoprintf("Unable to send find request for friend ~FT%04X@%s~RS: %s\n",id,svrname,err_string[ret]);
		}
	}
// Else see if they're on here. utype already set to local in constructor.
else {
	utype = USER_TYPE_LOCAL;
	svr_name = NULL;

	if (!linenum) owner->uprintf("Friend ~FT%04X~RS added.\n",fid);

	if ((u = get_user(id))) {
		stage = FRIEND_ONLINE;
		local_user = u;
		owner->infoprintf("Your friend ~FT%04X~RS is currently in group %04X.\n ",id,u->group->id);
		}
	else stage = FRIEND_OFFLINE;
	}

add_list_item(owner->first_friend,owner->last_friend,this);
}




/*** Destructor ***/
cl_friend::~cl_friend()
{
FREE(name);
FREE(svr_name);
}




/*** Set the info based on a user info packet ***/
int cl_friend::set_info(cl_server *svr, pkt_user_info *pkt)
{
char *tmp;
int nlen,dlen;

// Set stuff
remote_gid = htons(pkt->gid_ruid);
home_ip4_addr = pkt->home_addr.ip4;
home_port = ntohs(pkt->home_port);

// Set name
nlen = (pkt->namelen > max_name_len ? max_name_len : pkt->namelen);
if (!(tmp = (char *)malloc(nlen + 1))) return ERR_MALLOC;
FREE(name);
name = tmp;
memcpy(name,pkt->name_desc_svr,nlen);
name[nlen]='\0';

// Set description
if (svr->proto_rev > 5) dlen = pkt->desclen;
else dlen = pkt->len - pkt->namelen - (PKT_USER_INFO_SIZE-1);
if (dlen > max_desc_len) dlen = max_desc_len;

if (!(tmp = (char *)malloc(dlen + 1))) return ERR_MALLOC;
free(desc);
desc = tmp;
memcpy(desc,pkt->name_desc_svr + pkt->namelen,dlen);
desc[dlen]='\0';

return OK;
}
