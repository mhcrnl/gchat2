/* GChat
 * Copyright (C) 2012 Mitchell Cooper
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

#include <glib.h>

#include "common/plugin.h"
#include "common/xchat.h"
#include "common/xchatc.h"
#include "common/signal_factory.h"
#include "common/text.h"

static session *my_sess;

static void
process_callback(gpointer *params, int action)
{

	if (!my_sess)
		my_sess = new_ircwindow_fake (main_sess->server, "Highlights", SESS_DIALOG, 0);

	session *sess   = params[0];
	gchar *from     = params[1];
	gchar *message  = params[2];
	gchar *nickchar = params[3];
	gchar *idtext   = params[4];
	gint nickcolor  = color_of(from);
	gint chancolor  = color_of(sess->channel);
	gchar *tempc = g_strdup_printf("\x03%d%s\x03 \2(\2\x03%d%s\x03\2)\2", nickcolor, from, chancolor, sess->channel);
	gchar *tempn = g_strdup_printf("%s \2(\2%s\2)\2", from, sess->channel);
	gchar *nick;

	if (prefs.colorednicks)
	{
		nick = g_strdup(tempc);
		g_free(tempc);
	}
	else
	{
		nick = g_strdup(tempn);
		g_free(tempn);
	}

	if (action)
		session_print_format(my_sess, "channel action", nick, nickchar, message);
	else
		session_print_format(my_sess, "channel message", nick, nickchar, idtext, message);

	g_free(nick);
}

static void
process_action(gpointer *params)
{
	process_callback(params, TRUE);
}

static void
process_message(gpointer *params)
{
	process_callback(params, FALSE);
}

gboolean
init(Plugin *p)
{
	signal_attach("action public hilight", process_action);
	signal_attach("message public hilight", process_message);

	return TRUE;
}

gboolean
fini(Plugin *p)
{
	signal_disconnect("action public hilight", process_action);
	signal_disconnect("message public hilight", process_message);

	return TRUE;
}

PLUGIN_DECLARE("Highlight Overview", PACKAGE_VERSION, 
	"An easy way to view your highlights and queries.",
	"Mitchell Cooper", init, fini);
