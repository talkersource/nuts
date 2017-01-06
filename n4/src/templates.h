/***************************************************************************
 FILE: templates.h
 LVU : 1.3.2

 DESC:
 Template functions. Since these can't be compiled into a .o file (how would
 the compiler know what parameter types would be required without reference 
 to all the other code in the other modules?) these have to go in a global 
 header file.

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

/*** Add an item to a linked list ***/
template <class C>
void add_list_item(C *&first, C *&last, C *item)
{
if (first) {
	last->next = item;
	item->prev = last;
	}
else {
	first = item;
	item->prev = NULL;
	}
last = item;
item->next = NULL;
}




/*** Remove an item from a linked list ***/
template <class C>
void remove_list_item(C *&first, C *&last, C *item)
{
if (item->next) item->next->prev = item->prev;
if (item->prev) item->prev->next = item->next;
if (item == first) first = item->next;
if (item == last) last = item->prev;
}
