/* strdup.c : still no description */
/*
    Copyright (C) 1994 1995 1996 Frank Baumgart, Uwe Ohse

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    Contact: uwe@tirka.gun.de, Uwe Ohse @ DU3 (mausnet)
       Snail Mail (don't expect me to answer):
             Uwe Ohse
             Drosselstraﬂe 2
             47055 Duisburg
             Germany
    Contact2: godot@uni-paderborn.de, Frank Baumgart @ PB3 (mausnet)
       (might be the wrong person except for offline)

    Contact3: MausNet Newsgroups Quark & Q-Sysops.
*/
#include "config.h"

#include <stdlib.h>
#include <string.h>

char *strdup(const char *s)
{
	char *p;
	size_t l=strlen(s)+1;
	p=malloc(l);
	if (!p)
		return NULL;
	return memcpy(p,s,l);
}
