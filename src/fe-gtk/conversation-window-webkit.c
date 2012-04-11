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

#include <webkit/webkit.h>

static int buf_id = 0;

typedef struct {
	ConversationWindow public_info;
	WebKitWebView *view;
	WebKitWebFrame *frame;
	JSGlobalContextRef *js;
} ConversationWindowPriv;

typedef struct cbuffer {
	int id;
	ConversationWindow *win;
} cbuffer;

ConversationWindow *
conversation_window_new(void)
{
	ConversationWindowPriv *priv_win = g_slice_new0(ConversationWindowPriv);
	ConversationWindow *win = (ConversationWindow *) priv_win;
	GtkWidget *sw = GTK_WIDGET(gtk_scrolled_window_new(NULL, NULL));
	priv_win->view = WEBKIT_WEB_VIEW(webkit_web_view_new());
	priv_win->frame = webkit_web_view_get_main_frame(priv_win->view);
	priv_win->js = webkit_web_frame_get_global_context(priv_win->frame);
	win->widget = sw;

	gtk_container_add(GTK_CONTAINER(sw), GTK_WIDGET(priv_win->view));
	gtk_widget_show_all(sw);
	webkit_web_view_load_uri(priv_win->view, CONSPIRE_SHAREDIR "/conspire/www/text.html");

	return win;
}

void
conversation_window_update_preferences(ConversationWindow *win)
{

}

void
conversation_window_set_urlcheck_function(ConversationWindow *win, int (*urlcheck_function) (GtkWidget *, char *, int))
{

}

void
conversation_window_set_contextmenu_function(ConversationWindow *win, void (*callback)(GtkWidget *xtext, char *word, GdkEventButton *event))
{

}

gpointer
conversation_window_get_opaque_buffer(ConversationWindow *win)
{
	return NULL;
}

void
conversation_window_set_opaque_buffer(ConversationWindow *win, gpointer buf)
{
}

gpointer
conversation_buffer_new(ConversationWindow *win, gboolean timestamp)
{
	cbuffer *buf;

	buf = malloc (sizeof (struct cbuffer));
	memset (buf, 0, sizeof (struct cbuffer));
	buf->id = buf_id++;
	buf->win = win;

	return buf;
}

void
conversation_buffer_set_time_stamp(gpointer buf, gboolean timestamp)
{
}

void
conversation_buffer_append_text(gpointer buf, guchar *text, time_t stamp)
{
}

void
conversation_buffer_clear(gpointer buf)
{
}

void
conversation_window_append_text(ConversationWindow *win, guchar *text, time_t stamp)
{

}

void
conversation_window_clear(ConversationWindow *win)
{
}

void
conversation_window_set_transparency(ConversationWindow *win, double trans)
{
}
