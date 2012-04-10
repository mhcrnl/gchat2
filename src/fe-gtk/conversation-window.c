/* GChat
 * Copyright (c) 2012 Arinity development group
 *
 * Conspire
 * Copyright © 2009 William Pitcock <nenolod@dereferenced.org>.
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

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include "../common/stdinc.h"
#include <stdlib.h>

#include "fe-gtk.h"

#include <gtk/gtk.h>

#include "../common/xchat.h"
#include "../common/fe.h"
#include "../common/xchatc.h"
#include "../common/cfgfiles.h"
#include "../common/server.h"
#include "gtkutil.h"
#include "palette.h"
#include "maingui.h"
#include "rawlog.h"
#include "textgui.h"
#include "xtext.h"
#include "conversation-window.h"

#define CONVERSATION_WINDOW_XTEXT

typedef struct {
	ConversationWindow public_info;
	GtkWidget *xtext, *vs;
} ConversationWindowPriv;

#ifdef CONVERSATION_WINDOW_XTEXT
#include "conversation-window-xtext.c"
#endif

#ifdef CONVERSATION_WINDOW_PRINT
#include "conversation-window-print.c"
#endif
