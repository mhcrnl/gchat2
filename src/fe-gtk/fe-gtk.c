/* X-Chat
 * Copyright (C) 1998 Peter Zelezny.
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

#define GTK_DISABLE_DEPRECATED

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../common/stdinc.h"

#include "fe-gtk.h"

#include <gtk/gtk.h>

#include "../common/xchat.h"
#include "../common/fe.h"
#include "../common/util.h"
#include "../common/text.h"
#include "../common/cfgfiles.h"
#include "../common/xchatc.h"
#include "gtkutil.h"
#include "maingui.h"
#include "pixmaps.h"
#include "xtext.h"
#include "palette.h"
#include "menu.h"
#include "notifygui.h"
#include "textgui.h"
#include "fkeys.h"
#include "tray.h"
#include "urlgrab.h"
#include "conversation-window.h"

GdkPixmap *channelwin_pix;

/* === command-line parameter parsing : requires glib 2.6 === */

static char *arg_cfgdir = NULL;
static gint arg_show_autoload = 0;
static gint arg_show_autoloadx = 0;
static gint arg_show_config = 0;
static gint arg_show_version = 0;
static gint arg_minimize = 0;

static const GOptionEntry gopt_entries[] =
{
 {"no-auto",	'a', 0, G_OPTION_ARG_NONE,	&arg_dont_autoconnect, N_("Don't auto connect to servers"), NULL},
 {"cfgdir",	'd', 0, G_OPTION_ARG_STRING,	&arg_cfgdir, N_("Use a different config directory"), "PATH"},
 {"no-plugins",	'n', 0, G_OPTION_ARG_NONE,	&arg_skip_plugins, N_("Don't auto load any plugins"), NULL},
 #ifndef GDK_WINDOWING_QUARTZ
 {"plugindir",	'p', 0, G_OPTION_ARG_NONE,	&arg_show_autoload, N_("Show plugin auto-load directory"), NULL},
 #else
 {"osx-plugindir",	'x', 0, G_OPTION_ARG_NONE,	&arg_show_autoloadx, N_("Show plugin auto-load directory"), NULL},
 #endif
 {"configdir",	'u', 0, G_OPTION_ARG_NONE,	&arg_show_config, N_("Show user config directory"), NULL},
 {"url",	 0,  0, G_OPTION_ARG_STRING,	&arg_url, N_("Open an irc://server:port/channel URL"), "URL"},
 {"existing",	'e', 0, G_OPTION_ARG_NONE,	&arg_existing, N_("Open URL in an existing Conspire instance"), NULL},
 {"minimize",	 0,  0, G_OPTION_ARG_INT,	&arg_minimize, N_("Begin minimized. Level 0=Normal 1=Iconified 2=Tray"), N_("level")},
 {"version",	'v', 0, G_OPTION_ARG_NONE,	&arg_show_version, N_("Show version information"), NULL},
 {NULL}
};

int
fe_args (int argc, char *argv[])
{
	GError *error = NULL;
	GOptionContext *context;

#ifdef ENABLE_NLS
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);
#endif

	context = g_option_context_new (NULL);
	g_option_context_add_main_entries (context, gopt_entries, GETTEXT_PACKAGE);
	g_option_context_add_group (context, gtk_get_option_group (FALSE));
	//OS X .app bundles shoves args into app, fuck this.
	g_option_context_set_ignore_unknown_options(context, TRUE);
	g_option_context_parse (context, &argc, &argv, &error);

	if (error)
	{
		if (error->message)
			printf ("%s\n", error->message);
		return 1;
	}

	g_option_context_free (context);

	if (arg_show_version)
	{
		printf (PACKAGE_NAME" "PACKAGE_VERSION"\n");
		return 0;
	}

	if (arg_show_autoload)
	{
		printf ("%s\n", CONSPIRE_LIBDIR"/plugins");
		return 0;
	}

	if (arg_show_autoloadx)
	{
		printf ("%s\n", CONSPIRE_LIBDIR"/plugins");
		return 0;
	}

	if (arg_show_config)
	{
		printf ("%s\n", get_xdir_fs ());
		return 0;
	}

	if (arg_cfgdir)	/* we want filesystem encoding */
	{
		xdir_fs = strdup (arg_cfgdir);
		if (xdir_fs[strlen (xdir_fs) - 1] == '/')
			xdir_fs[strlen (xdir_fs) - 1] = 0;
		g_free (arg_cfgdir);
	}

	gtk_init (&argc, &argv);

	return -1;
}

const char cursor_color_rc[] =
	"style \"xc-ib-st\""
	"{"
		"GtkEntry::cursor-color=\"#%02x%02x%02x\""
	"}"
	"widget \"*.xchat-inputbox\" style : application \"xc-ib-st\"";

GtkStyle *
create_input_style (GtkStyle *style)
{
	char buf[256];
	static int done_rc = FALSE;

	pango_font_description_free (style->font_desc);
	style->font_desc = pango_font_description_from_string (prefs.font_normal);

	/* fall back */
	if (pango_font_description_get_size (style->font_desc) == 0)
	{
		snprintf (buf, sizeof (buf), _("Failed to open font:\n\n%s"), prefs.font_normal);
		fe_message (buf, FE_MSG_ERROR);
		pango_font_description_free (style->font_desc);
		style->font_desc = pango_font_description_from_string ("sans 11");
	}

	if (prefs.style_inputbox && !done_rc)
	{
		done_rc = TRUE;
		sprintf (buf, cursor_color_rc, (colors[COL_FG].red >> 8),
			(colors[COL_FG].green >> 8), (colors[COL_FG].blue >> 8));
		gtk_rc_parse_string (buf);
	}

	style->bg[GTK_STATE_NORMAL] = colors[COL_FG];
	style->base[GTK_STATE_NORMAL] = colors[COL_BG];
	style->text[GTK_STATE_NORMAL] = colors[COL_FG];

	return style;
}

void
fe_init (void)
{
	palette_load ();
	key_init ();
	pixmaps_init ();

	channelwin_pix = pixmap_load_from_file (prefs.background);
	input_style = create_input_style (gtk_style_new ());
}

#ifdef _MSC_VER
#pragma comment(linker, "/ENTRY:mainCRTStartup")
#endif

void
fe_main (void)
{
	gtk_main ();

	/* sleep for 3 seconds so any QUIT messages are not lost. The  */
	/* GUI is closed at this point, so the user doesn't even know! */
	if (prefs.wait_on_exit)
		g_usleep(3000);
}

void
fe_cleanup (void)
{
	/* it's saved when pressing OK in setup.c */
	/*palette_save ();*/
}

void
fe_exit (void)
{
	gtk_main_quit ();
}

/* install tray stuff */

static int
fe_idle (gpointer data)
{
	session *sess = sess_list->data;

	/* initialize tray */
	tray_init();
	tray_apply_setup();

	if (arg_minimize == 1)
		gtk_window_iconify (GTK_WINDOW (sess->gui->window));
	else if (arg_minimize == 2)
		tray_toggle_visibility (FALSE);

	return 0;
}

void
fe_new_window (session *sess, int focus)
{
	int tab = FALSE;

	if (sess->type == SESS_DIALOG)
	{
		if (prefs.privmsgtab)
			tab = TRUE;
	} else
	{
		if (prefs.tabchannels)
			tab = TRUE;
	}

	mg_changui_new (sess, NULL, tab, focus);

	if (!sess_list->next)
		g_idle_add (fe_idle, NULL);
}

void
fe_new_server (struct server *serv)
{
	serv->gui = malloc (sizeof (struct server_gui));
	memset (serv->gui, 0, sizeof (struct server_gui));
}

void
fe_message (char *msg, int flags)
{
	GtkWidget *dialog;
	int type = GTK_MESSAGE_WARNING;

	if (flags & FE_MSG_ERROR)
		type = GTK_MESSAGE_ERROR;
	if (flags & FE_MSG_INFO)
		type = GTK_MESSAGE_INFO;

	dialog = gtk_message_dialog_new (GTK_WINDOW (parent_window), 0, type,
												GTK_BUTTONS_OK, "%s", msg);
	if (flags & FE_MSG_MARKUP)
		gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG (dialog), msg);
	g_signal_connect (G_OBJECT (dialog), "response",
							G_CALLBACK (gtk_widget_destroy), 0);
	gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);
	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_MOUSE);
	gtk_widget_show (dialog);

	if (flags & FE_MSG_WAIT)
		gtk_dialog_run (GTK_DIALOG (dialog));
}

int
fe_input_add (int sok, int flags, void *func, void *data)
{
	int tag, type = 0;
	GIOChannel *channel;

#ifndef _WIN32
	channel = g_io_channel_unix_new (sok);
#else
	channel = g_io_channel_win32_new_socket (sok);
#endif

	if (flags & FIA_READ)
		type |= G_IO_IN | G_IO_HUP | G_IO_ERR;
	if (flags & FIA_WRITE)
		type |= G_IO_OUT | G_IO_ERR;
	if (flags & FIA_EX)
		type |= G_IO_PRI;

	tag = g_io_add_watch (channel, type, (GIOFunc) func, data);
	g_io_channel_unref (channel);

	return tag;
}

void
fe_set_topic (session *sess, char *topic)
{
	if (!sess->gui->is_tab || sess == current_tab)
	{
		gtk_entry_set_text (GTK_ENTRY (sess->gui->topic_entry), topic);
		mg_set_topic_tip (sess);
	} else
	{
		if (sess->res->topic_text)
			free (sess->res->topic_text);
		sess->res->topic_text = strdup (topic);
	}
}

void
fe_set_hilight (struct session *sess)
{
	if (sess->gui->is_tab)
		fe_set_tab_color (sess, 3);	/* set tab to blue */

	if (prefs.input_flash_hilight)
		fe_flash_window (sess); /* taskbar flash */
}

static void
fe_update_mode_entry (session *sess, GtkWidget *entry, char **text, char *new_text)
{
	if (!sess->gui->is_tab || sess == current_tab)
	{
		if (sess->gui->flag_wid[0])	/* channel mode buttons enabled? */
			gtk_entry_set_text (GTK_ENTRY (entry), new_text);
	} else
	{
		if (sess->gui->is_tab)
		{
			if (*text)
				free (*text);
			*text = strdup (new_text);
		}
	}
}

void
fe_update_channel_key (struct session *sess)
{
	fe_update_mode_entry (sess, sess->gui->key_entry,
								 &sess->res->key_text, sess->channelkey);
	fe_set_title (sess);
}

void
fe_update_channel_limit (struct session *sess)
{
	char tmp[16];

	sprintf (tmp, "%d", sess->limit);
	fe_update_mode_entry (sess, sess->gui->limit_entry,
								 &sess->res->limit_text, tmp);
	fe_set_title (sess);
}

int
fe_is_chanwindow (struct server *serv)
{
	if (!serv->gui->chanlist_window)
		return 0;
	return 1;
}

int
fe_is_banwindow (struct session *sess)
{
   if (!sess->res->banlist_window)
     return 0;
   return 1;
}

void
fe_notify_update (char *name)
{
	if (!name)
		notify_gui_update ();
}

void
fe_text_clear (struct session *sess)
{
	conversation_buffer_clear(sess->res->buffer);
}

void
fe_close_window (struct session *sess)
{
	if (sess->gui->is_tab)
		mg_tab_close (sess);
	else
		gtk_widget_destroy (sess->gui->window);
}

void
fe_progressbar_start (session *sess)
{
}

void
fe_progressbar_end (server *serv)
{
}

void
fe_print_text (struct session *sess, char *text, time_t stamp)
{
	conversation_buffer_append_text(sess->res->buffer, (unsigned char *)text, stamp);

	if (!sess->new_data && sess != current_tab &&
		 sess->gui->is_tab && !sess->nick_said && stamp == 0)
	{
		sess->new_data = TRUE;
		lastact_update(sess);
		if (sess->msg_said)
			fe_set_tab_color (sess, 2);
		else
			fe_set_tab_color (sess, 1);
	}
}

void
fe_beep (void)
{
	gdk_beep ();
}

#if 0
static int
lastlog_regex_cmp (char *a, regex_t *reg)
{
	return !regexec (reg, a, 1, NULL, REG_NOTBOL);
}
#endif

void
fe_lastlog (session *sess, session *lastlog_sess, char *sstr, gboolean regexp)
{
#if 0
	regex_t reg;

	if (gtk_xtext_is_empty (sess->res->buffer))
	{
		PrintText (lastlog_sess, _("Search buffer is empty.\n"));
		return;
	}

	if (!regexp)
	{
		gtk_xtext_lastlog (lastlog_sess->res->buffer, sess->res->buffer,
								 (void *) nocasestrstr, sstr);
		return;
	}

	if (regcomp (&reg, sstr, REG_ICASE | REG_EXTENDED | REG_NOSUB) == 0)
	{
		gtk_xtext_lastlog (lastlog_sess->res->buffer, sess->res->buffer,
								 (void *) lastlog_regex_cmp, &reg);
		regfree (&reg);
	}
#endif
}

void
fe_set_lag (server *serv, int lag)
{
	GSList *list = sess_list;
	session *sess;
	gdouble per;
	char lagtext[64];
	char lagtip[64];
	unsigned long nowtim;

	if (lag == -1)
	{
		if (!serv->lag_sent)
			return;
		nowtim = make_ping_time ();
		lag = (nowtim - serv->lag_sent) / 100000;
	}

	per = (double)((double)lag / (double)40);
	if (per > 1.0)
		per = 1.0;

	snprintf (lagtext, sizeof (lagtext) - 1, "%s%d.%ds",
				 serv->lag_sent ? "+" : "", lag / 10, lag % 10);
	snprintf (lagtip, sizeof (lagtip) - 1, "Lag: %s%d.%d seconds",
				 serv->lag_sent ? "+" : "", lag / 10, lag % 10);

	while (list)
	{
		sess = list->data;
		if (sess->server == serv)
		{
			if (sess->res->lag_tip)
				free (sess->res->lag_tip);
			sess->res->lag_tip = strdup (lagtip);

			if (!sess->gui->is_tab || current_tab == sess)
			{
				if (sess->gui->lagometer)
				{
					gtk_progress_bar_set_fraction ((GtkProgressBar *) sess->gui->lagometer, per);
					add_tip (sess->gui->lagometer->parent, lagtip);
				}
				if (sess->gui->laginfo)
					gtk_label_set_text ((GtkLabel *) sess->gui->laginfo, lagtext);
			} else
			{
				sess->res->lag_value = per;
				if (sess->res->lag_text)
					free (sess->res->lag_text);
				sess->res->lag_text = strdup (lagtext);
			}
		}
		list = list->next;
	}
}

void
fe_set_throttle (server *serv)
{
	GSList *list = sess_list;
	struct session *sess;
	float per;
	char tbuf[64];
	char tip[64];

	per = (float) linequeue_size(serv->lq) / ((double) 40);
	if (per > 1.0)
		per = 1.0;

	while (list)
	{
		sess = list->data;
		if (sess->server == serv)
		{
			snprintf (tbuf, sizeof (tbuf) - 1, _("%d lines"), linequeue_size(serv->lq));
			snprintf (tip, sizeof (tip) - 1, _("Network send queue: %d lines"), linequeue_size(serv->lq));

			if (sess->res->queue_tip)
				free (sess->res->queue_tip);
			sess->res->queue_tip = strdup (tip);

			if (!sess->gui->is_tab || current_tab == sess)
			{
				if (sess->gui->throttlemeter)
				{
					gtk_progress_bar_set_fraction ((GtkProgressBar *) sess->gui->throttlemeter, per);
					add_tip (sess->gui->throttlemeter->parent, tip);
				}
				if (sess->gui->throttleinfo)
					gtk_label_set_text ((GtkLabel *) sess->gui->throttleinfo, tbuf);
			} else
			{
				sess->res->queue_value = per;
				if (sess->res->queue_text)
					free (sess->res->queue_text);
				sess->res->queue_text = strdup (tbuf);
			}
		}
		list = list->next;
	}
}

void
fe_ctrl_gui (session *sess, int action, int arg)
{
	switch (action)
	{
	case 0:
		gtk_widget_hide (sess->gui->window); break;
	case 1:
		gtk_widget_show (sess->gui->window);
		gtk_window_present (GTK_WINDOW (sess->gui->window));
		break;
	case 2:
		mg_bring_tofront_sess (sess); break;
	case 3:
		fe_flash_window (sess); break;
	case 4:
		fe_set_tab_color (sess, arg); break;
	case 5:
		gtk_window_iconify (GTK_WINDOW (sess->gui->window)); break;
	case 6:
		menu_bar_toggle ();	/* toggle menubar on/off */
		break;
	case 7:
		mg_detach (sess, arg);	/* arg: 0=toggle 1=detach 2=attach */
		break;
	case 8:
		setup_apply_real (TRUE, TRUE, TRUE);
	}
}

static void
dcc_saveas_cb (struct DCC *dcc, char *file)
{
	if (is_dcc (dcc))
	{
		if (dcc->dccstat == STAT_QUEUED)
		{
			if (file)
				dcc_get_with_destfile (dcc, file);
			else if (dcc->resume_sent == 0)
				dcc_abort (dcc->serv->front_session, dcc);
		}
	}
}

void
fe_confirm (const char *message, void (*yesproc)(void *), void (*noproc)(void *), void *ud)
{
	/* warning, assuming fe_confirm is used by DCC only! */
	struct DCC *dcc = ud;

	if (dcc->file)
		gtkutil_file_req (message, dcc_saveas_cb, ud, dcc->file,
								FRF_WRITE|FRF_FILTERISINITIAL|FRF_NOASKOVERWRITE);
}

int
fe_gui_info (session *sess, int info_type)
{
	switch (info_type)
	{
	case 0:	/* window status */
#if GTK_CHECK_VERSION(2,18,0)
		if (!gtk_widget_get_visible(GTK_WIDGET(sess->gui->window)))
#else
		if (!GTK_WIDGET_VISIBLE (GTK_WINDOW (sess->gui->window)))
#endif
			return 2;	/* hidden (iconified or systray) */
		if (gtk_window_is_active (GTK_WINDOW (sess->gui->window)))
			return 1;	/* active/focused */

		return 0;		/* normal (no keyboard focus or behind a window) */
	}

	return -1;
}

void *
fe_gui_info_ptr (session *sess, int info_type)
{
	switch (info_type)
	{
	case 0:	/* native window pointer (for plugins) */
		return sess->gui->window;
	}
	return NULL;
}

char *
fe_get_inputbox_contents (session *sess)
{
	/* not the current tab */
	if (sess->res->input_text)
		return sess->res->input_text;

	/* current focused tab */
	return SPELL_ENTRY_GET_TEXT (sess->gui->input_box);
}

int
fe_get_inputbox_cursor (session *sess)
{
	/* not the current tab (we don't remember the cursor pos) */
	if (sess->res->input_text)
		return 0;

	/* current focused tab */
	return SPELL_ENTRY_GET_POS (sess->gui->input_box);
}

void
fe_set_inputbox_cursor (session *sess, int delta, int pos)
{
	if (!sess->gui->is_tab || sess == current_tab)
	{
		if (delta)
			pos += SPELL_ENTRY_GET_POS (sess->gui->input_box);
		SPELL_ENTRY_SET_POS (sess->gui->input_box, pos);
	} else
	{
		/* we don't support changing non-front tabs yet */
	}
}

void
fe_set_inputbox_contents (session *sess, char *text)
{
	if (!sess->gui->is_tab || sess == current_tab)
	{
		SPELL_ENTRY_SET_TEXT (sess->gui->input_box, text);
	} else
	{
		if (sess->res->input_text)
			free (sess->res->input_text);
		sess->res->input_text = strdup (text);
	}
}

#ifndef _WIN32

static gboolean
try_browser (const char *browser, const char *arg, const char *url)
{
	const char *argv[4];
	char *path;

	path = g_find_program_in_path (browser);
	if (!path)
		return 0;

	argv[0] = path;
	argv[1] = url;
	argv[2] = NULL;
	if (arg)
	{
		argv[1] = arg;
		argv[2] = url;
		argv[3] = NULL;
	}
	xchat_execv (argv);
	g_free (path);
	return 1;
}

#endif

static void
fe_open_url_inner (const char *url)
{
	/* universal desktop URL opener (from xdg-utils). Supports gnome,kde,xfce4. */
	if (try_browser ("xdg-open", NULL, url))
		return;

	/* try to detect GNOME */
	if (g_getenv ("GNOME_DESKTOP_SESSION_ID"))
	{
		if (try_browser ("gnome-open", NULL, url)) /* Gnome 2.4+ has this */
			return;
	}

	/* try to detect KDE */
	if (g_getenv ("KDE_FULL_SESSION"))
	{
		if (try_browser ("kfmclient", "exec", url))
			return;
	}

	/* everything failed, what now? just try firefox */
	if (try_browser ("firefox", NULL, url))
		return;

	/* fresh out of ideas... */
	try_browser ("mozilla", NULL, url);
}

static void
fe_open_url_locale (const char *url)
{
#ifdef _WIN32
	ShellExecuteA (NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#else
	if (url[0] != '/' && strchr (url, ':') == NULL)
	{
		url = g_strdup_printf ("http://%s", url);
		fe_open_url_inner (url);
		g_free ((char *)url);
		return;
	}
	fe_open_url_inner (url);
#endif
}

void
fe_open_url (const char *url)
{
	char *loc;

	if (prefs.utf8_locale)
	{
		fe_open_url_locale (url);
		return;
	}

	/* the OS expects it in "locale" encoding. This makes it work on
	   unix systems that use ISO-8859-x and Win32. */
	loc = g_locale_from_utf8 (url, -1, 0, 0, 0);
	if (loc)
	{
		fe_open_url_locale (loc);
		g_free (loc);
	}
}

void
fe_server_event (server *serv, int type, int arg)
{
	GSList *list = sess_list;
	session *sess;

	while (list)
	{
		sess = list->data;
		if (sess->server == serv && (current_tab == sess || !sess->gui->is_tab))
		{
			switch (type)
			{
			default:
				break;
			}
		}
		list = list->next;
	}
}

void
fe_get_file (const char *title, char *initial,
				 void (*callback) (void *userdata, char *file), void *userdata,
				 int flags)

{
	/* OK: Call callback once per file, then once more with file=NULL. */
	/* CANCEL: Call callback once with file=NULL. */
	gtkutil_file_req (title, callback, userdata, initial, flags | FRF_FILTERISINITIAL);
}