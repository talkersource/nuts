/***************************************************************************
 FILE: cl_msginfo.cc
 LVU : 0.1.0

 DESC:
 Objects of this class hold information about a single message on the board
 or in user mail. This code just does initialisation and deletion.
 
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
cl_msginfo::cl_msginfo()
{
mnum = 0;
sl = NULL;
id = NULL;
name = NULL;
subject = NULL;
filename = NULL;
create_time = 0;
size = 0;
read =0;
prev = NULL;
next = NULL;
}




/*** Destructor ***/
cl_msginfo::~cl_msginfo()
{
if (sl) delete sl;
else {
	FREE(id);
	FREE(name);
	FREE(subject);
	FREE(filename);
	}	
}




/*** Set using a splitline object. This is used for loaded messages. ***/
void cl_msginfo::set(int mn, cl_splitline *split)
{
sl = split;

if (sl->wcnt < 7) {
	id = name = subject = filename = "<not set>";
	return;
	}
mnum = mn;
id = sl->word[1];
name = sl->word[2];
subject = sl->word[3];
filename = sl->word[4];
create_time = (time_t)atoi(sl->word[5]);
size = atoi(sl->word[6]);
read = (sl->wcnt >= 8 && !strcmp(sl->word[7],"YES"));
}




/*** Set using passed strings. This is used for new messages. ***/
int cl_msginfo::set(
	int mn, char *idstr, char *nme, char *subj, char *fname, int sz)
{
if (!(id = strdup(idstr)) ||
    !(name = strdup(nme)) ||
    !(subject = strdup(subj)) ||
    !(filename = strdup(fname))) return ERR_MALLOC;

mnum = mn;
create_time = server_time;
size = sz;
read = 0;

return OK;
}
