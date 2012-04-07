/*
 * libconspire: lightweight advanced irc client library
 * Copyright (c) 2007 William Pitcock <nenolod@sacredspiral.co.uk>
 * Portions copyright (C) 1998-2007 Peter Zelezny.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#ifndef _CONSPIRE_DEBUG_H__GUARD
#define _CONSPIRE_DEBUG_H__GUARD

#undef CONSPIRE_DEBUG

#define _ENTER _DEBUG("enter")
#define _LEAVE _DEBUG("leave"); return 
#define _MESSAGE(tag, string, ...) do { fprintf(stderr, "conspire: %s: %s:%d (%s): " string "\n", \
    tag, __FILE__, __LINE__, __PRETTY_FUNCTION__, ##__VA_ARGS__); fflush(stderr); } while(0)

#define _ERROR(...) _MESSAGE("ERROR", __VA_ARGS__)
#define _WARN(...)  _MESSAGE("WARN", __VA_ARGS__)

#ifdef CONSPIRE_DEBUG
# define _DEBUG(...) _MESSAGE("DEBUG",  __VA_ARGS__)
#else
# define _DEBUG(...) {}
#endif

#endif
