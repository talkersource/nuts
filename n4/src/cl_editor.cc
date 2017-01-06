/***************************************************************************
 FILE: cl_editor.cc
 LVU : 1.3.3

 DESC:
 This contains the code to create & run editor objects used in entering 
 profiles, mail etc.

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

#define SRC_PROMPT "\n~IP~FGSave, ~FYRedo, ~FRCancel~RS (s/r/c)?: "


/*** Constructor ***/
cl_editor::cl_editor(cl_user *own,int typ, char *path)
{
struct stat fs;
char *str;
int fd;

error = OK;
owner = own;
buff = NULL;
inc_size = 0;
inc_path = NULL;

// Can't use the editor when on a remote server since any input the user
// enters will go to the remote server and not the local editor.
if (owner->server_to) {
	error = ERR_USER_GONE_REMOTE;  return;
	}

type = typ;
stage = EDITOR_STAGE_INPUT;
bpos = 0;
line = 0;
malloced = 0;
maxchars = 0;
maxlines = 0;
isbatch = 0;

switch(type) {
	case EDITOR_TYPE_PROFILE:
	maxchars = max_profile_chars;
	str = "profile";
	break;

	case EDITOR_TYPE_LOGIN_BATCH:
	maxlines = max_login_batch_lines;
	str = "login batch";
	isbatch = 1;
	break;

	case EDITOR_TYPE_LOGOUT_BATCH:
	maxlines = max_logout_batch_lines;
	str = "logout batch";
	isbatch = 1;
	break;

	case EDITOR_TYPE_SESSION_BATCH:
	maxlines = max_session_batch_lines;
	str = "session batch";
	isbatch = 1;
	break;

	case EDITOR_TYPE_MAIL:
	maxchars = max_mail_chars;
	str = "mail";
	break;

	case EDITOR_TYPE_BOARD:
	maxchars = max_board_chars;
	str = "board message";
	break;

	case EDITOR_TYPE_GROUP_DESC:
	maxchars = max_group_desc_chars;
	str = "group description";
	break;

	case EDITOR_TYPE_BCAST:
	case EDITOR_TYPE_NET_BCAST:
	maxchars = max_broadcast_chars;
	str = "broadcast";
	break;


	default:
	owner->uprintf("~OL~FRINTERNAL ERROR:~RS Unknown editor type %d in cl_editor::cl_editor()!\n",type);
	log(1,"INTERNAL ERROR: Unknown editor type %d in cl_editor::cl_editor()!",type);
	error = ERR_INTERNAL;
	return;
	}

// Memory map file we wish to include in buffer
if (path) {
	inc_path = strdup(path);
	if ((fd = open(inc_path,O_RDONLY)) == -1) {
		error = ERR_CANT_OPEN_FILE;  return;
		}
	if (fstat(fd,&fs)) {
		close(fd);
		error = ERR_CANT_OPEN_FILE;
		return;
		}
	if ((inc_text = (char *)mmap(
		NULL,fs.st_size,PROT_READ,MAP_SHARED,fd,0)) == MAP_FAILED) {
		close(fd);
		error = ERR_MMAP_FAILED;
		return;
		}

	close(fd);
	inc_size = fs.st_size;

	if ((error = include_text()) != OK) return;
	error = OK;
	}


// Batches work in lines
if (isbatch) {
	if (maxlines) 
		owner->uprintf("\n~BB~FG*** Editing %s, maximum of %d lines ***\n\n",str,maxlines);
	else
		owner->uprintf("\n~BB~FG*** Editing %s, unlimited lines allowed ***\n\n");
	owner->uprintf("Enter a '.' on a line on its own to finish editing.\n");
	}
else {
	// Everything else works in characters
	if (maxchars)
		owner->uprintf("\n~BB~FG*** Editing %s, maximum of %d characters including newlines ***\n\n",str,maxchars);
	else 
		owner->uprintf("\n~BB~FG*** Editing %s, unlimited characters allowed ***\n\n",str);
	owner->uprintf("Enter a '.' on a line on its own to finish editing.\n");
	}
print_buffer();
}




/*** Destructor ***/
cl_editor::~cl_editor()
{
FREE(buff);
owner->editor = NULL;

if (inc_path) {
	// Test inc_size because if error occured on startup memory may
	// not have been mapped yet.
	if (inc_size) munmap(inc_text,inc_size);
	free(inc_path);
	}
}




/*** This does the actual business ***/
int cl_editor::run()
{
cl_splitline *com;
int i;

com = owner->com;
if (stage == EDITOR_STAGE_SRC) {
	// Choose what to do when done editing
	if (!com->wcnt || com->word[0][1]) {
		owner->uprintf(SRC_PROMPT);  return OK;
		}

	switch(toupper(com->word[0][0])) {
		case 'S':
		owner->uprintf("\n~FGEdit complete.\n\n");
		stage = EDITOR_STAGE_COMPLETE;
		return OK;

		case 'R':
		FREE(buff);
		bpos = 0;
		malloced = 0;
		line = 0;
		stage = EDITOR_STAGE_INPUT;
		if ((error = include_text()) != OK) return error;
		print_buffer();
		return OK;	

		case 'C':
		owner->uprintf("\n~FREdit cancelled.\n\n");
		stage = EDITOR_STAGE_CANCEL;
		return OK;

		default:
		owner->uprintf(SRC_PROMPT);
		}
	return OK;
	}

// Input stage. Check if we've finished editing and that user has 
// entered some input that contains more than whitespace.
if (com->wcnt && !strcmp(com->word[0],".")) {
	if (!malloced) {
		owner->uprintf("\n~FRNo input, edit cancelled.\n\n");
		stage = EDITOR_STAGE_CANCEL;
		return OK;
		}
	for(i=0;i < bpos;++i) if (buff[i] > 32) break;
	if (i == bpos) {
		owner->uprintf("\n~FRNo input, edit cancelled.\n\n");
		stage = EDITOR_STAGE_CANCEL;
		return OK;
		}
	owner->uprintf(SRC_PROMPT);
	stage = EDITOR_STAGE_SRC;
	return OK;
	}

if (append((char *)owner->tbuff) != OK) return ERR_MALLOC;
if (append("\n") != OK) return ERR_MALLOC;

// Batches are done by line
if (isbatch) {
	if (++line == maxlines) {
		owner->uprintf(SRC_PROMPT);
		stage = EDITOR_STAGE_SRC;
		return OK;
		}
	owner->uprintf("~IP~FT%02d>~RS ",line+1);
	return OK;
	}

// Everything else done by number of characters
if (maxchars && bpos >= maxchars) {
	if (bpos > maxchars) {
		buff[bpos-1] = '\0';  // Remove newline
		if (bpos - maxchars > 6) {
			buff[maxchars+3]='.';
			buff[maxchars+4]='.';
			buff[maxchars+5]='.';
			buff[maxchars+6]='\0';
			}
		owner->uprintf("\n~FYInput too long, truncated from:~RS \"%s\"\n",
			buff+maxchars);
		buff[maxchars] = '\0';
		}
	owner->uprintf(SRC_PROMPT);
	stage = EDITOR_STAGE_SRC;
	return OK;
	}
owner->uprintf("~IP~FT%03d>~RS ",bpos);
return OK;
}




/*** Append a string into the buffer ***/
int cl_editor::append(char *str)
{
int len;

len = strlen(str);
if (bpos + len >= malloced) {
	malloced = EDITOR_MALLOC * (((bpos + len) / EDITOR_MALLOC) + 1);
	if (!(buff = (char *)realloc(buff,malloced))) return ERR_MALLOC;
	if (!bpos) buff[0]='\0';
	}
strcat(buff + bpos,str);
bpos += len;
return OK;
}




/*** Include some text in the buffer prior to in the edit ***/
int cl_editor::include_text()
{
int len,start_line;
char *s;

if (!inc_text) return OK;

// Count lines
for(s=inc_text;*s;++s) line += (*s == '\n');
if (isbatch && maxlines && line > maxlines) return ERR_INCLUDE_FILE_TOO_BIG;

len = strlen(inc_text) + line + 1;
malloced = ((len / EDITOR_MALLOC) + 1) * EDITOR_MALLOC;

if (!(buff = (char *)malloc(malloced))) return ERR_MALLOC;

// start_line only used for NON batch files. Ie ones that have max lengths
// measured in characters. I know, its confusing. Deal.
if (!isbatch)
	start_line = line > max_include_lines ? line - max_include_lines : 0;

do {
	if (isbatch) bpos = 0;
	else {
		buff[0] = '>';  bpos = 1;
		}

	// Put included text in buffer
	for(s=inc_text,line=0;*s;++s) {
		if (*s == '\n') {
			line++;
			// If we don't do this we'll have a lone ">" at
			// top of edit
			if (!isbatch && line == start_line) ++s;
			}

		if (isbatch || line >= start_line) {
			// Put '>' at start of each line unless editing
			// batch file
			if (!isbatch && *s == '\n' && *(s+1)) {
				buff[bpos] = '\n';
				buff[++bpos] = '>';
				}
			else buff[bpos] = *s;
			++bpos;
			}
		}

	/* If bpos has only gone up by 1 (ie user has one big long line
	   which is too long to include and we've simply hit then end newline
	   then get rid of '>' */
	if (!isbatch) {
		if (bpos == 2) bpos=0;
		start_line++;
		}
	buff[bpos] = '\0';

	/* If its not a batch include and its too big loop around again 
	   and cut off first line. This is for message replies. Theres no
	   point doing the same for a batch file for obvious reasons. Make 
	   sure user has at least a third of "maxchars" characters to write
	   his reply in. */
	} while(!isbatch && maxchars && bpos >= maxchars - (maxchars / 3));

// Unmap file
if (inc_path) {
	munmap(inc_text,inc_size);
	FREE(inc_path);
	}

return (!isbatch && maxchars && bpos > maxchars) ? 
	ERR_INCLUDE_FILE_TOO_BIG : OK;
}




/*** Print prompts & contents of buffer so far. This is for when we have an 
     included message ***/
void cl_editor::print_buffer()
{
char *s,*s2,c;
int i;

// Print initial edit prompt
if (isbatch)
	owner->uprintf("\n~IP~FT01>~RS ");
else
	owner->uprintf("\n~IP~FT000>~RS ");

if (!bpos) return;

// Print buffer and further prompts
for(i=0,s2=buff;i < line;++i,s2=s+1) {
	for(s=s2;*s && *s != '\n';++s);
	c = *s;
	*s = '\0';
	if (isbatch)
		owner->uprintf("~NP%s\n~IP~FT%02d>~RS ",s2,i+2);
	else
		owner->uprintf("~NP%s\n~IP~FT%03d>~RS ",s2,s - buff + 1);
	*s = c;
	if (!c || !*(s+1)) break;
	}

// If included message has hit the or gone beyond limit then go straight
// to s/r/c prompt.
if ((isbatch && maxlines && line >= maxlines) ||
    (!isbatch && maxchars && bpos >= maxchars)) {
	owner->uprintf("\n~NP%s",SRC_PROMPT);
	stage = EDITOR_STAGE_SRC;
	}
}
