/***************************************************************************
 FILE: cl_splitline.cc
 LVU : 1.3.8

 DESC:
 A splitline object takes a line of text and splits it into words that are
 used by the various system parsers such as config loading and user input.

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


/*** Constructor ***/
cl_splitline::cl_splitline(int icl)
{
word = NULL;
wordptr = NULL;
is_config_line = icl;
wcnt = 0;
}




/*** Destructor ***/
cl_splitline::~cl_splitline()
{
reset();
}




/*** Set this object based on the contents of another ***/
int cl_splitline::set(cl_splitline *sl)
{
int i,alloc;

reset();

if (sl->wcnt) {
	alloc=(sl->wcnt/WORD_ALLOC) * WORD_ALLOC + 
	       WORD_ALLOC * (sl->wcnt%WORD_ALLOC > 0);
	if (!(word=(char **)malloc(sizeof(char *) * alloc))) 
		return ERR_MALLOC;

	wcnt=sl->wcnt;
	for(i=0;i < sl->wcnt;++i) word[i] = strdup(sl->word[i]);

	if (!(wordptr=(char **)malloc(sizeof(char *) * alloc)))
		return ERR_MALLOC;
	}
return OK;
}




/*** Reset the object ***/
void cl_splitline::reset()
{
int i;

if (wcnt) {
	for(i=0;i < wcnt;++i) free(word[i]);
	wcnt=0;
	free(word);
	free(wordptr);
	word=NULL;
	wordptr=NULL;
	}
}




/*** Parse the line. The stages are only needed for a config file line where
     we have to check for = and , in the right place. ***/
int cl_splitline::parse(char *line)
{
char *p1,*p2,c;
int stage,i;

reset();

// Empty line or comment
if (!line || !strlen(line) || line[0] == '#') return OK;

stage=0;
p1=line;

for(i=0;;++i) {
	if (!(p2=get_word(&p1))) break;
	c=*p2;
	*p2='\0';
	switch(stage) {
		case 0: 
		add_word(p1);
		stage=1 + !is_config_line;
		break;

		case 1:
		if (strcmp(p1,"=")) return ERR_MISSING_EQUALS;
		stage=2;
		break;

		case 2:
		if (!is_config_line) add_word(p1);
		else {
			if (!strcmp(p1,",")) add_word("");
			else {
				add_word(p1);  stage=3;
				}
			}
		break;

		case 3:
		if (strcmp(p1,",")) return ERR_MISSING_COMMA;
		stage=2;
		}

	p1=p2;
	*p2=c;
	}
if (is_config_line && wcnt) {
	if (stage == 1) return ERR_MISSING_EQUALS;
	if (wcnt < 2 || stage==2) return ERR_MISSING_VALUE;
	}
return OK;
}




/*** Get an individual word either in or not in quotes. = and , also count
     as words in their own right unless inside quotes ***/
char *cl_splitline::get_word(char **ptr)
{
char *p1,*p2,*prev;

p1=*ptr;
for(;*p1 && *p1 < 33;++p1);
if (!*p1) return NULL;
start_quote=(*p1 == '"');
end_quote=0;

for(p2=prev=p1+start_quote;*p2;++p2) {
	if (start_quote) {
		if (*p2 == '"' && *prev != '\\') {
			++p2;
			end_quote=1;
			break;
			}
		}
	else {
		if (is_config_line && (*p2 == '=' || *p2 == ',')) {
			if (p2 == p1) p2++;
			break;
			}
		if (*p2 < 33) break;
		}
	prev=p2;
	}
if (p2 == p1 || (start_quote && p2 == p1+1)) return NULL;
*ptr=p1;
return p2;
}




/*** Add a word into the list ***/
void cl_splitline::add_word(char *str)
{
char *s,*s2,*prev,c;
int slash;

if (!(wcnt%WORD_ALLOC)) {
	word=(char **)realloc(word,sizeof(char *) * (wcnt+WORD_ALLOC));
	wordptr=(char **)realloc(wordptr,sizeof(char *) * (wcnt+WORD_ALLOC));
	}

// Set wordptr to point to the start of the word in the actual string
wordptr[wcnt] = str;

if (start_quote) {
	// Remove quotes
	++str;
	if (end_quote) {
		s=str + strlen(str) - 1;
		c=*s;
		*s='\0';
		}
	else s=NULL;
	}

word[wcnt++] = s2 = strdup(str);
if (end_quote) *s=c;

if (start_quote) {
	// Go through string and remove any slashes that are escaping
	// a quote if we have words inside quotes. Can't work on the 
	// original string because its part of a larger one.
	for(s=prev=s2;*s;++s) {
		if (*s == '\\') slash=1;
		else {
			if (*s == '"' && slash) strcpy(s-1,s);  
			slash=0;
			}
		}
	}
}




/*** Shift all words up by one. This is equivalent to the unix shell shift
     command ***/
void cl_splitline::shift()
{
int i;

switch(wcnt) {
	case 0: return;

	case 1: reset();  return;

	default:
	free(word[0]);
	for(i=0;i < wcnt-1;++i) {
		word[i] = word[i+1];
		wordptr[i] = wordptr[i+1];
		}
	wcnt--;
	}
}
