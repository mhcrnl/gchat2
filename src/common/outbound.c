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

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>

#define WANTSOCKET
#define WANTARPA
#include "inet.h"

#include "stdinc.h"
#include <time.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "conspire-config.h"
#include "xchat.h"
#include "ignore-ng.h"
#include "util.h"
#include "fe.h"
#include "cfgfiles.h"			  /* xchat_fopen_file() */
#include "network.h"				/* net_ip() */
#include "modes.h"
#include "notify.h"
#include "inbound.h"
#include "text.h"
#include "xchatc.h"
#include "servlist.h"
#include "server.h"
#include "tree.h"
#include "outbound.h"
#include "command_factory.h"
#include "command_option.h"
#include "plugin.h"

#ifdef USE_DEBUG
extern int current_mem_usage;
#endif
#define TBUFSIZE 4096

#define IRC_MAX_LENGTH 512

static void help (session *sess, char *tbuf, char *helpcmd, int quiet);
static CommandResult cmd_server (session *sess, char *tbuf, char *word[], char *word_eol[]);
static void handle_say(session *sess, char *text, gboolean check_spch, gboolean check_lastlog, gboolean check_completion);


static void
notj_msg (struct session *sess)
{
	PrintText (sess, _("No channel joined. Try /join #<channel>\n"));
}

void
notc_msg (struct session *sess)
{
	PrintText (sess, _("Not connected. Try /server <host> [<port>]\n"));
}

static char *
random_line (char *file_name)
{
	FILE *fh;
	char buf[512];
	int lines, ran;

	if (!file_name[0])
		return strdup (file_name);

	fh = xchat_fopen_file (file_name, "r", 0);
	if (!fh)
	{
		/* reason is not a file, an actual reason! */
		return strdup (file_name);
	}

	/* count number of lines in file */
	lines = 0;
	while (fgets (buf, sizeof (buf), fh))
		lines++;

	if (lines < 1)
		return strdup (file_name);

	/* go down a random number */
	rewind (fh);
	ran = RAND_INT (lines);
	do
	{
		fgets (buf, sizeof (buf), fh);
		lines--;
	}
	while (lines > ran);
	fclose (fh);
	buf[strlen (buf) - 1] = 0;	  /* remove the trailing '\n' */
	return strdup (buf);
}

void
server_sendpart (server * serv, char *channel, char *reason)
{
	if (!reason)
	{
		reason = random_line (prefs.partreason);
		serv->p_part (serv, channel, reason);
		free (reason);
	} else
	{
		/* reason set by /quit, /close argument */
		serv->p_part (serv, channel, reason);
	}
}

void
server_sendquit (session * sess)
{
	char *rea, *colrea;

	if (!sess->quitreason)
	{
		colrea = strdup (prefs.quitreason);
		check_special_chars (colrea, FALSE);
		rea = random_line (colrea);
		free (colrea);
		sess->server->p_quit (sess->server, rea);
		free (rea);
	} else
	{
		/* reason set by /quit, /close argument */
		sess->server->p_quit (sess->server, sess->quitreason);
	}
}

void
process_data_init (char *buf, char *cmd, char *word[],
						 char *word_eol[], gboolean handle_quotes,
						 gboolean allow_escape_quotes)
{
	int wordcount = 2;
	int space = FALSE;
	int quote = FALSE;
	int j = 0;
	int len;

	word[0] = "\000\000";
	word_eol[0] = "\000\000";
	word[1] = (char *)buf;
	word_eol[1] = (char *)cmd;

	while (1)
	{
		switch (*cmd)
		{
		case 0:
		 jump:
			buf[j] = 0;
			for (j = wordcount; j < PDIWORDS; j++)
			{
				word[j] = "\000\000";
				word_eol[j] = "\000\000";
			}
			return;
		case '\042':
			if (!handle_quotes)
				goto def;
			/* two quotes turn into 1 */
			if (allow_escape_quotes && cmd[1] == '\042')
			{
				cmd++;
				goto def;
			}
			if (quote)
			{
				quote = FALSE;
				space = FALSE;
			} else
				quote = TRUE;
			cmd++;
			break;
		case ' ':
			if (!quote)
			{
				if (!space)
				{
					buf[j] = 0;
					j++;

					word[wordcount] = &buf[j];
					word_eol[wordcount] = cmd + 1;
					wordcount++;

					if (wordcount == PDIWORDS - 1)
						goto jump;

					space = TRUE;
				}
				cmd++;
				break;
			}
		default:
def:
			space = FALSE;
			len = g_utf8_skip[((unsigned char *)cmd)[0]];
			if (len == 1)
			{
				buf[j] = *cmd;
				j++;
				cmd++;
			} else
			{
				/* skip past a multi-byte utf8 char */
				memcpy (buf + j, cmd, len);
				j += len;
				cmd += len;
			}
		}
	}
}

static CommandResult
cmd_foreach (session *sess, char *tbuf, char *word[], char *word_eol[])
{
	GSList *list = sess_list;
	server *serv;

	if (!*word_eol[3])
		return CMD_EXEC_FAIL;

	if (!g_ascii_strcasecmp(word[2], "channel"))
	{
		while (list)
		{
			sess = list->data;
			list = list->next;
			if (sess->type == SESS_CHANNEL && sess->channel[0] && sess->server->connected)
				handle_command(sess, word_eol[3], FALSE);
		}
	}
	else if (!g_ascii_strcasecmp(word[2], "local-channel"))
	{
		serv = sess->server;
		while (list)
		{
			sess = list->data;
			list = list->next;
			if (sess->type == SESS_CHANNEL && sess->channel[0] && sess->server->connected && (sess->server == serv))
				handle_command(sess, word_eol[3], FALSE);
		}
	}
	else if (!g_ascii_strcasecmp(word[2], "server"))
	{
		list = serv_list;
		while (list)
		{
			serv = list->data;
			list = list->next;
			if (serv->connected)
				handle_command(serv->front_session, word_eol[3], FALSE);
		}
	}
	else if (!g_ascii_strcasecmp(word[2], "query"))
	{
		while (list)
		{
			sess = list->data;
			list = list->next;
			if (sess->type == SESS_DIALOG && sess->channel[0] && sess->server->connected)
				handle_command(sess, word_eol[3], FALSE);
		}
	}
	else if (!g_ascii_strcasecmp(word[2], "local-query"))
	{
		serv = sess->server;
		while (list)
		{
			sess = list->data;
			list = list->next;
			if (sess->type == SESS_DIALOG && sess->channel[0] && sess->server->connected && (sess->server == serv))
				handle_command(sess, word_eol[3], FALSE);
		}
	}
	else
	{
		PrintText(sess, "Invalid parameter for foreach");
		return CMD_EXEC_FAIL;
	}
	return CMD_EXEC_OK;
}

static CommandResult
cmd_away (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	GSList *list;
	char *reason = word_eol[2];

	if (!(*reason) && sess->server->is_away)
	{
		unsigned int gone;

		sess->server->p_set_back (sess->server);

		if (prefs.show_away_message)
		{
			gone = time (NULL) - sess->server->away_time;
			sprintf (tbuf, "me is back (gone %.2d:%.2d:%.2d)", gone / 3600,
						(gone / 60) % 60, gone % 60);
			for (list = sess_list; list; list = list->next)
			{
				/* am I the right server and not a dialog box */
				if (((struct session *) list->data)->server == sess->server
					 && ((struct session *) list->data)->type == SESS_CHANNEL
					 && ((struct session *) list->data)->channel[0])
				{
					handle_command ((session *) list->data, tbuf, TRUE);
				}
			}
		}

		if (sess->server->last_away_reason)
			free (sess->server->last_away_reason);
		sess->server->last_away_reason = NULL;

		return CMD_EXEC_OK;
	}
	else if (!(*reason))
		reason = g_strdup(prefs.awayreason);

	sess->server->p_set_away (sess->server, reason);

	if (prefs.show_away_message)
	{
		snprintf (tbuf, TBUFSIZE, "me is away: %s", reason);
		for (list = sess_list; list; list = list->next)
		{
			/* am I the right server and not a dialog box */
			if (((struct session *) list->data)->server == sess->server
				 && ((struct session *) list->data)->type == SESS_CHANNEL
				 && ((struct session *) list->data)->channel[0])
			{
				handle_command ((session *) list->data, tbuf, TRUE);
			}
		}
	}

	if (sess->server->last_away_reason != reason)
	{
		if (sess->server->last_away_reason)
			free (sess->server->last_away_reason);

		if (reason == word_eol[2])
			sess->server->last_away_reason = strdup (reason);
		else
			sess->server->last_away_reason = reason;
	}

	return CMD_EXEC_OK;
}

void
banlike (session * sess, char *modechar, char *tbuf, char *mask, gint type, int deop)
{
	struct User *user;
	char *at, *dot, *lastdot;
	char username[64], fullhost[128], domain[128], *mode, *p2;
	server *serv = sess->server;

	user = userlist_find (sess, mask);
	if (user && user->hostname)  /* it's a nickname, let's find a proper ban mask */
	{
		if (deop)
		{
			mode = g_strconcat("-o+", modechar, " ", NULL);
			p2 = user->nick;
		} else
		{
			mode = g_strconcat("+", modechar, NULL);
			p2 = "";
		}

		mask = user->hostname;

		at = strchr (mask, '@');	/* FIXME: utf8 */
		if (!at)
			return;					  /* can't happen? */
		*at = 0;

		if (mask[0] == '~' || mask[0] == '+' ||
		    mask[0] == '=' || mask[0] == '^' || mask[0] == '-')
		{
			/* the ident is prefixed with something, we replace that sign with an * */
			g_strlcpy (username+1, mask+1, sizeof (username)-1);
			username[0] = '*';
		} else if (at - mask < USERNAMELEN)
		{
			/* we just add an * in the begining of the ident */
			g_strlcpy (username+1, mask, sizeof (username)-1);
			username[0] = '*';
		} else
		{
			/* ident might be too long, we just ban what it gives and add an * in the end */
			g_strlcpy (username, mask, sizeof (username));
		}
		*at = '@';
		g_strlcpy (fullhost, at + 1, sizeof (fullhost));

		dot = strchr (fullhost, '.');
		if (dot)
		{
			g_strlcpy (domain, dot, sizeof (domain));
		} else
		{
			g_strlcpy (domain, fullhost, sizeof (domain));
		}

		if (type == -1)
			type = prefs.bantype;

		tbuf[0] = 0;
		if (inet_addr (fullhost) != -1)	/* "fullhost" is really a IP number */
		{
			lastdot = strrchr (fullhost, '.');
			if (!lastdot)
				return;				  /* can't happen? */

			*lastdot = 0;
			strcpy (domain, fullhost);
			*lastdot = '.';

			switch (type)
			{
			case 0:
				snprintf (tbuf, TBUFSIZE, "%s%s *!*@%s.*", mode, p2, domain);
				break;

			case 1:
				snprintf (tbuf, TBUFSIZE, "%s%s *!*@%s", mode, p2, fullhost);
				break;

			case 2:
				snprintf (tbuf, TBUFSIZE, "%s%s *!%s@%s.*", mode, p2, username, domain);
				break;

			case 3:
				snprintf (tbuf, TBUFSIZE, "%s%s *!%s@%s", mode, p2, username, fullhost);
				break;
			}
		} else
		{
			switch (type)
			{
			case 0:
				snprintf (tbuf, TBUFSIZE, "%s%s *!*@*%s", mode, p2, domain);
				break;

			case 1:
				snprintf (tbuf, TBUFSIZE, "%s%s *!*@%s", mode, p2, fullhost);
				break;

			case 2:
				snprintf (tbuf, TBUFSIZE, "%s%s *!%s@*%s", mode, p2, username, domain);
				break;

			case 3:
				snprintf (tbuf, TBUFSIZE, "%s%s *!%s@%s", mode, p2, username, fullhost);
				break;
			}
		}

	} else
	{
		snprintf (tbuf, TBUFSIZE, "+%s %s", modechar, mask);
	}
	serv->p_mode (serv, sess->channel, tbuf);
}

static CommandResult
cmd_ban (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
    gboolean except = FALSE;
    gint type = -1;
    gchar *mask;
    gchar *bantype = "b";
    CommandOption options[] = {
        {"except", TYPE_BOOLEAN, &except, N_("Place mask on the ban exemption list.")},
        {"type", TYPE_INTEGER, &type, N_("Specify a mask type; only valid with nicks.")},
        {NULL}
    };
    gint len = g_strv_length(word);

    command_option_parse(sess, &len, &word, options);

    if (except)
    {
        bantype = "e";
    }

    mask = word[0];

    if (*mask && strncmp(mask, "", 1))
    {
        banlike(sess, bantype, tbuf, mask, type, FALSE);
    } else
    {
        gchar *mode = g_strconcat("+", bantype, NULL);
        sess->server->p_mode(sess->server, sess->channel, mode);
        g_free(mode);
    }
    return CMD_EXEC_OK;
}

static CommandResult
cmd_unban (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
    gboolean except = FALSE;
    gchar bantype = 'b';
    CommandOption options[] = {
        {"except", TYPE_BOOLEAN, &except, N_("Place mask on the ban exemption list.")},
        {NULL}
    };
    gint len = g_strv_length(word);
    gint i;

    command_option_parse(sess, &len, &word, options);

    if (except)
    {
        bantype = 'e';
    }
    if (word[0] == NULL)
        return CMD_EXEC_FAIL;

    len = 0;

    for (i = 0; i < PDIWORDS && word[i] != NULL; i++)
    {
        if (!strncmp(word[i], "", 1))
            break;
        len++;
    }

    send_channel_modes (sess, tbuf, word, 0, len, '-', bantype, 0);
    return CMD_EXEC_OK;
}

static CommandResult
cmd_charset (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	server *serv = sess->server;
	const char *locale = NULL;
	int offset = 0;

	if (strcmp (word[2], "-quiet") == 0)
		offset++;

	if (!word[2 + offset][0])
	{
		g_get_charset (&locale);
		PrintTextf (sess, "Current charset: %s\n",
						serv->encoding ? serv->encoding : locale);
		return CMD_EXEC_OK;
	}

	if (servlist_check_encoding (word[2 + offset]))
	{
		server_set_encoding (serv, word[2 + offset]);
		if (offset < 1)
			PrintTextf (sess, "Charset changed to: %s\n", word[2 + offset]);
	} else
	{
		PrintTextf (sess, "\0034Unknown charset:\017 %s\n", word[2 + offset]);
	}

	return CMD_EXEC_OK;
}

static CommandResult
cmd_clear (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	GSList *list = sess_list;
	char *reason = word_eol[2];

	if (reason[0] == 0)
	{
		fe_text_clear (sess);
		return CMD_EXEC_OK;
	}

	if (g_ascii_strcasecmp (reason, "HISTORY") == 0)
	{
		history_free (&sess->history);
		return CMD_EXEC_OK;
	}

	if (g_ascii_strncasecmp (reason, "all", 3) == 0)
	{
		while (list)
		{
			sess = list->data;
			if (!sess->nick_said)
				fe_text_clear (list->data);
			list = list->next;
		}
		return CMD_EXEC_OK;
	}

	return CMD_EXEC_FAIL;
}

static CommandResult
cmd_close (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	GSList *list;

	if (strcmp (word[2], "-m") == 0)
	{
		list = sess_list;
		while (list)
		{
			sess = list->data;
			list = list->next;
			if (sess->type == SESS_DIALOG)
				fe_close_window (sess);
		}
	} else
	{
		if (*word_eol[2])
			sess->quitreason = word_eol[2];
		fe_close_window (sess);
	}

	return CMD_EXEC_OK;
}

static CommandResult
cmd_ctcp (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	int mbl;
	char *to = word[2];
	if (*to)
	{
		char *msg = word_eol[3];
		if (*msg)
		{
			unsigned char *cmd = (unsigned char *)msg;

			/* make the first word upper case (as per RFC) */
			while (1)
			{
				if (*cmd == ' ' || *cmd == 0)
					break;
				mbl = g_utf8_skip[*cmd];
				if (mbl == 1)
					*cmd = toupper (*cmd);
				cmd += mbl;
			}

			sess->server->p_ctcp (sess->server, to, msg);

			signal_emit("ctcp send", 3, sess, to, msg);

			return CMD_EXEC_OK;
		}
	}
	return CMD_EXEC_FAIL;
}

static CommandResult
cmd_hop (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	char *key = sess->channelkey;
	char *chan = word[2];
	if (!*chan)
		chan = sess->channel;
	if (*chan && sess->type == SESS_CHANNEL)
	{
		sess->server->p_cycle (sess->server, chan, key);
		return CMD_EXEC_OK;
	}
	return CMD_EXEC_FAIL;
}

static CommandResult
cmd_dcc (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	int goodtype;
	struct DCC *dcc = 0;
	char *type = word[2];
	if (*type)
	{
		if (!g_ascii_strcasecmp (type, "HELP"))
			return CMD_EXEC_FAIL;
		if (!g_ascii_strcasecmp (type, "CLOSE"))
		{
			if (*word[3] && *word[4])
			{
				goodtype = 0;
				if (!g_ascii_strcasecmp (word[3], "SEND"))
				{
					dcc = find_dcc (word[4], word[5], TYPE_SEND);
					dcc_abort (sess, dcc);
					goodtype = TRUE;
				}
				if (!strcasecmp (word[3], "GET"))
				{
					dcc = find_dcc (word[4], word[5], TYPE_RECV);
					dcc_abort (sess, dcc);
					goodtype = TRUE;
				}
				if (!strcasecmp (word[3], "CHAT"))
				{
					dcc = find_dcc (word[4], "", TYPE_CHATRECV);
					if (!dcc)
						dcc = find_dcc (word[4], "", TYPE_CHATSEND);
					dcc_abort (sess, dcc);
					goodtype = TRUE;
				}

				if (!goodtype)
					return CMD_EXEC_FAIL;

				if (!dcc)
					signal_emit("dcc not found", 1, sess);

				return CMD_EXEC_OK;

			}
			return CMD_EXEC_FAIL;
		}
		if ((!strcasecmp (type, "CHAT")) || (!g_ascii_strcasecmp (type, "PCHAT")))
		{
			char *nick = word[3];
			int passive = (!g_ascii_strcasecmp(type, "PCHAT")) ? 1 : 0;
			if (*nick)
				dcc_chat (sess, nick, passive);
			return CMD_EXEC_OK;
		}
		if (!g_ascii_strcasecmp (type, "LIST"))
		{
			dcc_show_list (sess);
			return CMD_EXEC_OK;
		}
		if (!g_ascii_strcasecmp (type, "GET"))
		{
			char *nick = word[3];
			char *file = word[4];
			if (!*file)
			{
				if (*nick)
					dcc_get_nick (sess, nick);
			} else
			{
				dcc = find_dcc (nick, file, TYPE_RECV);
				if (dcc)
					dcc_get (dcc);
				else
					signal_emit("dcc not found", 1, sess);
			}
			return CMD_EXEC_OK;
		}
		if ((!g_ascii_strcasecmp (type, "SEND")) || (!g_ascii_strcasecmp (type, "PSEND")))
		{
			int i = 3, maxcps;
			char *nick, *file;
			int passive = (!g_ascii_strcasecmp(type, "PSEND")) ? 1 : 0;

			nick = word[i];
			if (!*nick)
				return CMD_EXEC_FAIL;

			maxcps = prefs.dcc_max_send_cps;
			if (!g_ascii_strncasecmp(nick, "-maxcps=", 8))
			{
				maxcps = atoi(nick + 8);
				i++;
				nick = word[i];
				if (!*nick)
					return CMD_EXEC_FAIL;
			}

			i++;

			file = word[i];
			if (!*file)
			{
				fe_dcc_send_filereq (sess, nick, maxcps, passive);
				return CMD_EXEC_OK;
			}

			do
			{
				dcc_send (sess, nick, file, maxcps, passive);
				i++;
				file = word[i];
			}
			while (*file);

			return CMD_EXEC_OK;
		}

		return CMD_EXEC_FAIL;
	}

	dcc_show_list (sess);
	return CMD_EXEC_OK;
}

static CommandResult
cmd_debug (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	struct session *s;
	struct server *v;
	GSList *list = sess_list;

	PrintText (sess, "Session   T Channel    WaitChan  WillChan  Server\n");
	while (list)
	{
		s = (struct session *) list->data;
		sprintf (tbuf, "%p %1x %-10.10s %-10.10s %-10.10s %p\n",
					s, s->type, s->channel, s->waitchannel,
					s->willjoinchannel, s->server);
		PrintText (sess, tbuf);
		list = list->next;
	}

	list = serv_list;
	PrintText (sess, "Server    Sock  Name\n");
	while (list)
	{
		v = (struct server *) list->data;
		sprintf (tbuf, "%p %-5d %s\n",
					v, v->sok, v->servername);
		PrintText (sess, tbuf);
		list = list->next;
	}

	sprintf (tbuf,
				"\nfront_session: %p\n"
				"current_tab: %p\n\n",
				sess->server->front_session, current_tab);
	PrintText (sess, tbuf);
#ifdef USE_DEBUG
	sprintf (tbuf, "current mem: %d\n\n", current_mem_usage);
	PrintText (sess, tbuf);
#endif  /* !MEMORY_DEBUG */

	return CMD_EXEC_OK;
}

static CommandResult
cmd_dehalfop (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	int i = 2;

	while (1)
	{
		if (!*word[i])
		{
			if (i == 2)
				return CMD_EXEC_FAIL;
			send_channel_modes (sess, tbuf, word, 2, i, '-', 'h', 0);
			return CMD_EXEC_OK;
		}
		i++;
	}
}

static CommandResult
cmd_deop (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	int i = 2;

	while (1)
	{
		if (!*word[i])
		{
			if (i == 2)
				return CMD_EXEC_FAIL;
			send_channel_modes (sess, tbuf, word, 2, i, '-', 'o', 0);
			return CMD_EXEC_OK;
		}
		i++;
	}
}

typedef struct
{
	char **nicks;
	int i;
	session *sess;
	char *reason;
	char *tbuf;
} multidata;

GSList *menu_list = NULL;

#if 0
static void
menu_free (menu_entry *me)
{
	free (me->path);
	if (me->label)
		free (me->label);
	if (me->cmd)
		free (me->cmd);
	if (me->ucmd)
		free (me->ucmd);
	if (me->group)
		free (me->group);
	if (me->icon)
		free (me->icon);
	free (me);
}
#endif

/* strings equal? but ignore underscores */

int
menu_streq (const char *s1, const char *s2, int def)
{
	/* for separators */
	if (s1 == NULL && s2 == NULL)
		return 0;
	if (s1 == NULL || s2 == NULL)
		return 1;
	while (*s1)
	{
		if (*s1 == '_')
			s1++;
		if (*s2 == '_')
			s2++;
		if (*s1 != *s2)
			return 1;
		s1++;
		s2++;
	}
	if (!*s2)
		return 0;
	return def;
}

#if 0
static menu_entry *
menu_entry_find (char *path, char *label)
{
	GSList *list;
	menu_entry *me;

	list = menu_list;
	while (list)
	{
		me = list->data;
		if (!strcmp (path, me->path))
		{
			if (me->label && label && !strcmp (label, me->label))
				return me;
		}
		list = list->next;
	}
	return NULL;
}

static void
menu_del_children (char *path, char *label)
{
	GSList *list, *next;
	menu_entry *me;
	char buf[512];

	if (!label)
		label = "";
	if (path[0])
		snprintf (buf, sizeof (buf), "%s/%s", path, label);
	else
		snprintf (buf, sizeof (buf), "%s", label);

	list = menu_list;
	while (list)
	{
		me = list->data;
		next = list->next;
		if (!menu_streq (buf, me->path, 0))
		{
			menu_list = g_slist_remove (menu_list, me);
			menu_free (me);
		}
		list = next;
	}
}

static CommandResult
menu_del (char *path, char *label)
{
	GSList *list;
	menu_entry *me;

	list = menu_list;
	while (list)
	{
		me = list->data;
		if (!menu_streq (me->label, label, 1) && !menu_streq (me->path, path, 1))
		{
			menu_list = g_slist_remove (menu_list, me);
			fe_menu_del (me);
			menu_free (me);
			/* delete this item's children, if any */
			menu_del_children (path, label);
			return 1;
		}
		list = list->next;
	}

	return 0;
}

static char
menu_is_mainmenu_root (char *path, gint16 *offset)
{
	static const char *menus[] = {"\x4$TAB","\x5$TRAY","\x4$URL","\x5$NICK","\x5$CHAN"};
	int i;

	for (i = 0; i < 5; i++)
	{
		if (!strncmp (path, menus[i] + 1, menus[i][0]))
		{
			*offset = menus[i][0] + 1;	/* number of bytes to offset the root */
			return 0;	/* is not main menu */
		}
	}

	*offset = 0;
	return 1;	/* is main menu */
}

static void
menu_add (char *path, char *label, char *cmd, char *ucmd, int pos, int state, int markup, int enable, int mod, int key, char *group, char *icon)
{
	menu_entry *me;

	/* already exists? */
	me = menu_entry_find (path, label);
	if (me)
	{
		/* update only */
		me->state = state;
		me->enable = enable;
		fe_menu_update (me);
		return;
	}

	me = malloc (sizeof (menu_entry));
	me->pos = pos;
	me->modifier = mod;
	me->is_main = menu_is_mainmenu_root (path, &me->root_offset);
	me->state = state;
	me->markup = markup;
	me->enable = enable;
	me->key = key;
	me->path = strdup (path);
	me->label = NULL;
	me->cmd = NULL;
	me->ucmd = NULL;
	me->group = NULL;
	me->icon = NULL;

	if (label)
		me->label = strdup (label);
	if (cmd)
		me->cmd = strdup (cmd);
	if (ucmd)
		me->ucmd = strdup (ucmd);
	if (group)
		me->group = strdup (group);
	if (icon)
		me->icon = strdup (icon);

	menu_list = g_slist_append (menu_list, me);
	label = fe_menu_add (me);
	if (label)
	{
		/* FE has given us a stripped label */
		free (me->label);
		me->label = strdup (label);
		g_free (label); /* this is from pango */
	}
}
#endif

static CommandResult
cmd_devoice (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	int i = 2;

	while (1)
	{
		if (!*word[i])
		{
			if (i == 2)
				return CMD_EXEC_FAIL;
			send_channel_modes (sess, tbuf, word, 2, i, '-', 'v', 0);
			return CMD_EXEC_OK;
		}
		i++;
	}
}

static CommandResult
cmd_discon (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	sess->server->disconnect (sess, TRUE, -1);
	return CMD_EXEC_OK;
}

static CommandResult
cmd_echo (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	PrintText (sess, word_eol[2]);
	return CMD_EXEC_OK;
}

static CommandResult
cmd_flushq (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	PrintTextf (sess, "Flushing server send queue, %d lines.\n", linequeue_size(sess->server->lq));
	linequeue_erase(sess->server->lq);
	return CMD_EXEC_OK;
}

static CommandResult
cmd_quit (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	if (*word_eol[2])
		sess->quitreason = word_eol[2];
	sess->server->disconnect (sess, TRUE, -1);
	return 2;
}

static CommandResult
cmd_gate (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	char *server_name = word[2];
	server *serv = sess->server;
	if (*server_name)
	{
		char *port = word[3];
#ifdef GNUTLS
		serv->use_ssl = FALSE;
#endif
		server_fill_her_up (serv);
		if (*port)
			serv->connect (serv, server_name, atoi (port), TRUE);
		else
			serv->connect (serv, server_name, 23, TRUE);
		return CMD_EXEC_OK;
	}
	return CMD_EXEC_FAIL;
}

static CommandResult
cmd_ghost (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	if (word[3][0])
	{
		sess->server->p_ns_ghost (sess->server, word[2], word[3]);
		return CMD_EXEC_OK;
	}

	return CMD_EXEC_FAIL;
}

typedef struct
{
	int longfmt;
	int i, t;
	char *buf;
} help_list;

static void
show_help_line (session *sess, help_list *hl, const char *name, const char *usage)
{
	int j, len, max;
	const char *p;

	if (name[0] == '.')	/* hidden command? */
		return;

	if (hl->longfmt)	/* long format for /HELP -l */
	{
		if (!usage || usage[0] == 0)
			PrintTextf (sess, "   \0034%s\003 :\n", name);
		else
			PrintTextf (sess, "   \0034%s\003 : %s\n", name, _(usage));
		return;
	}

	/* append the name into buffer, but convert to uppercase */
	len = strlen (hl->buf);
	p = name;
	while (*p)
	{
		hl->buf[len] = toupper ((unsigned char) *p);
		len++;
		p++;
	}
	hl->buf[len] = 0;

	hl->t++;
	if (hl->t == 5)
	{
		hl->t = 0;
		strcat (hl->buf, "\n");
		PrintText (sess, hl->buf);
		hl->buf[0] = ' ';
		hl->buf[1] = ' ';
		hl->buf[2] = 0;
	} else
	{
		/* append some spaces after the command name */
		max = strlen (name);
		if (max < 10)
		{
			max = 10 - max;
			for (j = 0; j < max; j++)
			{
				hl->buf[len] = ' ';
				len++;
				hl->buf[len] = 0;
			}
		}
	}
}

static CommandResult
cmd_help (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	int i = 0, longfmt = 0;
	char *helpcmd = "";
	GSList *list;
	mowgli_dictionary_iteration_state_t state;

	if (tbuf)
		helpcmd = word[2];
	if (*helpcmd && strcmp (helpcmd, "-l") == 0)
		longfmt = 1;

	if (*helpcmd && !longfmt)
	{
		help (sess, tbuf, helpcmd, FALSE);
	} else
	{
		struct popup *pop;
		char *buf = malloc (4096);
		help_list hl;
		Command *cmd;

		hl.longfmt = longfmt;
		hl.buf = buf;

		PrintTextf (sess, "\n%s\n\n", _("Commands available:"));
		buf[0] = ' ';
		buf[1] = ' ';
		buf[2] = 0;
		hl.t = 0;
		hl.i = 0;
		MOWGLI_DICTIONARY_FOREACH(cmd, &state, cmd_dict_)
		{
			show_help_line (sess, &hl, state.cur->key, cmd->helptext);
			i++;
		}
		strcat (buf, "\n");
		PrintText (sess, buf);

		PrintTextf (sess, "\n%s\n\n", _("Aliases:"));
		buf[0] = ' ';
		buf[1] = ' ';
		buf[2] = 0;
		hl.t = 0;
		hl.i = 0;
		list = command_list;
		while (list)
		{
			pop = list->data;
			show_help_line (sess, &hl, pop->name, pop->cmd);
			list = list->next;
		}
		strcat (buf, "\n");
		PrintText (sess, buf);

		PrintTextf (sess, "\n%s\n\n", _("Type /HELP <command> for more information, or /HELP -l"));
	}
	return CMD_EXEC_OK;
}

static CommandResult
cmd_id (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	if (word[2][0])
	{
		sess->server->p_ns_identify (sess->server, word[2]);
		return CMD_EXEC_OK;
	}

	return CMD_EXEC_FAIL;
}


static CommandResult
cmd_invite (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	if (!*word[2])
		return CMD_EXEC_FAIL;
	if (*word[3])
		sess->server->p_invite (sess->server, word[3], word[2]);
	else
		sess->server->p_invite (sess->server, sess->channel, word[2]);
	return CMD_EXEC_OK;
}

static CommandResult
cmd_join (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	char *chan = word[2];
	if (*chan)
	{
		char *po, *pass = word[3];
		sess->server->p_join (sess->server, chan, pass);
		if (sess->channel[0] == 0 && sess->waitchannel[0])
		{
			po = strchr (chan, ',');
			if (po)
				*po = 0;
			g_strlcpy (sess->waitchannel, chan, CHANLEN);
		}
		return CMD_EXEC_OK;
	}
	return CMD_EXEC_FAIL;
}

static CommandResult
cmd_kick (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	char *nick = word[2];
	char *reason = word_eol[3];
	if (*nick)
	{
		sess->server->p_kick (sess->server, sess->channel, nick, reason);
		return CMD_EXEC_OK;
	}
	return CMD_EXEC_FAIL;
}

static CommandResult
cmd_kickban (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
    gint type = -1;
    gchar *nick;
    gchar *reason;
    struct User *user;
    CommandOption options[] = {
        {"type", TYPE_INTEGER, &type, "Mask type"},
        {NULL}
    };
    gint len = g_strv_length(word);

    command_option_parse(sess, &len, &word, options);

    nick = word[0];
    if (!strncmp(word_eol[4], "", 1))
        reason = word_eol[3];
    else
        reason = word_eol[5];

    if (nick != NULL)
    {
        user = userlist_find(sess, nick);

        banlike(sess, "b", tbuf, nick, type, (user && user->op));

        sess->server->p_kick(sess->server, sess->channel, nick, reason);

        return CMD_EXEC_OK;
    }
    return CMD_EXEC_FAIL;
}

static CommandResult
cmd_killall (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	GSList *list;
	server *serv;

	if (*word_eol[2])
		sess->quitreason = word_eol[2];

	list = serv_list;
	while (list) {
		serv = list->data;
		sess = serv->server_session;
		if (serv->connected)
			serv->disconnect (sess, TRUE, -1);
		list = list->next;
	}
	xchat_exit();
	return 2;
}

static CommandResult
cmd_lagcheck (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	lag_check ();
	return CMD_EXEC_OK;
}

static void
lastlog (session *sess, char *search, gboolean regexp)
{
	session *lastlog_sess;

	if (!is_session (sess))
		return;

	lastlog_sess = find_dialog (sess->server, "(lastlog)");
	if (!lastlog_sess)
		lastlog_sess = new_ircwindow (sess->server, "(lastlog)", SESS_DIALOG, 0);

	lastlog_sess->lastlog_sess = sess;
	lastlog_sess->lastlog_regexp = regexp;	/* remember the search type */

	fe_text_clear (lastlog_sess);
	fe_lastlog (sess, lastlog_sess, search, regexp);
}

static CommandResult
cmd_lastlog (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	if (*word_eol[2])
	{
		if (!strcmp (word[2], "-r"))
			lastlog (sess, word_eol[3], TRUE);
		else
			lastlog (sess, word_eol[2], FALSE);
		return CMD_EXEC_OK;
	}

	return CMD_EXEC_FAIL;
}

static CommandResult
cmd_list (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	sess->server->p_list_channels (sess->server, word_eol[2], 1);

	return CMD_EXEC_OK;
}

static CommandResult
cmd_load (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	FILE *fp;
	char *nl, *file;
	int len;

	if (!word[2][0])
		return CMD_EXEC_FAIL;

	if (strcmp (word[2], "-e") == 0)
	{
		file = expand_homedir (word[3]);
		fp = xchat_fopen_file (file, "r", XOF_FULLPATH);
		if (!fp)
		{
			PrintTextf (sess, _("Cannot access %s\n"), file);
			PrintText (sess, errorstring (errno));
			free (file);
			return CMD_EXEC_OK;
		}
		free (file);

		tbuf[1024] = 0;
		while (fgets (tbuf, 1024, fp))
		{
			nl = strchr (tbuf, '\n');
			if (nl == tbuf) /* skip empty commands */
				continue;
			if (nl)
				*nl = 0;
			if (tbuf[0] == prefs.cmdchar[0])
				handle_command (sess, tbuf + 1, TRUE);
			else
				handle_command (sess, tbuf, TRUE);
		}
		fclose (fp);
		return CMD_EXEC_OK;
	}

#ifdef USE_PLUGIN
	len = strlen (word[2]);
#if defined(__hpux)
	if (len > 3 && g_ascii_strcasecmp (".sl", word[2] + len - 3) == 0)
#else
	if (len > 3 && g_ascii_strcasecmp (".so", word[2] + len - 3) == 0)
#endif
	{
		file = expand_homedir (word[2]);
		plugin_load(file);
		free (file);

		return CMD_EXEC_OK;
	}
#endif

	sprintf (tbuf, "Unknown file type %s. Maybe you need to install the Perl or Python plugin?\n", word[2]);
	PrintText (sess, tbuf);

	return CMD_EXEC_FAIL;
}

static
GQueue *split_message(const struct session *sess, const gchar *text, const gchar *event) {
    GQueue *list   = g_queue_new();
    gchar *nick    = g_strdup(sess->server->nick);
    gchar *target  = g_strdup(sess->channel);
    gchar *host, *temp;
    gchar *tempstr = "\0";
    gint len;
    gchar *note_stop = g_strdup(prefs.text_overflow_stop);
    gint stop_len    = strlen(note_stop);

    /*
     * build the base string so we know how many bytes to subtract from the
     * absolute maximum imposed by the IRC standard
     */
    if (sess->me && sess->me->hostname) {
        host = g_strdup(sess->me->hostname);
        temp = g_strdup_printf(":%s!%s@%s %s %s :", nick, prefs.username, host, event, target);
        len  = strlen(temp) + 9; /* this is for CTCP ACTION */
    } else {
        /*
         * we don't have a hostname, for some reason, so just assume
         * it's a maximum of 64 chars
         */
        temp = g_strdup_printf(":%s!%s@%s %s %s :", nick, prefs.username, "", event, target);
        len  = strlen(temp) + 9 + 64; /* this is for CTCP ACTION */
    }

    g_free(temp);

    /*
     * iterate through the string and push each segment onto a list so we can
     * later send them out one at a time.
     */
    while ((strlen(text) + len + stop_len) > IRC_MAX_LENGTH-1) {
        tempstr = g_strrstr_len(text, IRC_MAX_LENGTH - (len + stop_len), " ");
        temp = g_strndup(text, tempstr-text);
        g_queue_push_tail(list, g_strconcat(temp, " ", note_stop, NULL));
        text = tempstr;
    }
    g_queue_push_tail(list, g_strdup(text));

    return list;
}

static CommandResult
cmd_me (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	char *act = word_eol[2];
	GQueue *acts = split_message(sess, act, "PRIVMSG");

	if (!(*act))
		return CMD_EXEC_FAIL;

	if (sess->type == SESS_SERVER)
	{
		notj_msg (sess);
		return CMD_EXEC_OK;
	}

	snprintf (tbuf, TBUFSIZE, "\001ACTION %s\001\r", act);
	/* first try through DCC CHAT */
	if (dcc_write_chat (sess->channel, tbuf))
	{
		/* print it to screen */
		inbound_action (sess, sess->channel, sess->server->nick, act, TRUE, FALSE);
	} else
	{
		/* DCC CHAT failed, try through server */
		if (sess->server->connected)
		{
			while (!g_queue_is_empty(acts)) {
				act = (gchar *)g_queue_pop_head(acts);
				sess->server->p_action (sess->server, sess->channel, act);
				/* print it to screen */
				inbound_action (sess, sess->channel, sess->server->nick, act, TRUE, FALSE);
			}
			g_queue_free(acts);
		} else
		{
			notc_msg (sess);
		}
	}

	return CMD_EXEC_OK;
}

static CommandResult
cmd_describe (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
    char *target = word[2];
    char *act = word_eol[3];
    struct session *newsess;
    GQueue *acts = split_message(sess, act, "PRIVMSG");
    GQueue *acopy = g_queue_copy(acts);

    if (!(*target) || !(*act))
        return CMD_EXEC_FAIL;

    switch (target[0]) {
        case '.':
            if (sess->lastnick[0])
                target = sess->lastnick;
            break;
        case '=':
            target++;
            snprintf (tbuf, TBUFSIZE, "\001ACTION %s\001\r", act);
            if (!dcc_write_chat(target, tbuf))
            {
                signal_emit("dcc not found", 1, sess);
                return CMD_EXEC_OK;
            } else {
		inbound_action (sess, target, sess->server->nick, act, TRUE, FALSE);
                break;
            }
        default:
            g_strlcpy(sess->lastnick, target, NICKLEN);
    }

    if (!sess->server->connected)
    {
        notc_msg (sess);
        return CMD_EXEC_OK;
    }
    while (!g_queue_is_empty(acts))
    {
        act = (gchar *)g_queue_pop_head(acts);
        sess->server->p_action (sess->server, target, act);
    }
    acts = g_queue_copy(acopy);

    newsess = find_dialog (sess->server, target);
    if (!newsess)
        newsess = find_channel (sess->server, target);

    if (newsess)
    {
        while (!g_queue_is_empty(acts))
        {
            act = (gchar *)g_queue_pop_head(acts);
            inbound_action (newsess, newsess->channel, newsess->server->nick, act, TRUE, FALSE);
        }
    } else
    {
        while (!g_queue_is_empty(acts))
        {
            act = (gchar *)g_queue_pop_head(acts);
            signal_emit("user action", 3, sess, target, act);
        }
    }

    g_queue_free(acts);
    g_queue_free(acopy);

    return CMD_EXEC_OK;
}

static CommandResult
cmd_mode (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	/* +channel channels are dying, let those servers whine about modes.
	 * return info about current channel if available and no info is given */
	if ((*word[2] == '+') || (*word[2] == 0) || (!is_channel(sess->server, word[2]) &&
				!(rfc_casecmp(sess->server->nick, word[2]) == 0)))
	{
		if(sess->channel[0] == 0)
			return CMD_EXEC_FAIL;
		sess->server->p_mode (sess->server, sess->channel, word_eol[2]);
	}
	else
		sess->server->p_mode (sess->server, word[2], word_eol[3]);
	return CMD_EXEC_OK;
}

static CommandResult
cmd_msg (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	char *nick = word[2];
	char *msg = word_eol[3];
	struct session *newsess;
	GQueue *msgs = split_message(sess, msg, "PRIVMSG");
	GQueue *mcopy = g_queue_copy(msgs);

	if (*nick)
	{
		if (*msg)
		{
			if (strcmp (nick, ".") == 0)
			{							  /* /msg the last nick /msg'ed */
				if (sess->lastnick[0])
					nick = sess->lastnick;
			} else
			{
				g_strlcpy (sess->lastnick, nick, NICKLEN);	/* prime the last nick memory */
			}

			if (*nick == '=')
			{
				nick++;
				if (!dcc_write_chat (nick, msg))
				{
					signal_emit("dcc not found", 1, sess);
					return CMD_EXEC_OK;
				}
			} else
			{
				if (!sess->server->connected)
				{
					notc_msg (sess);
					return CMD_EXEC_OK;
				}
				while (!g_queue_is_empty(msgs)) {
					msg = (gchar *)g_queue_pop_head(msgs);
					sess->server->p_message (sess->server, nick, msg);
				}
				msgs = g_queue_copy(mcopy);
			}
			newsess = find_dialog (sess->server, nick);
			if (!newsess)
				newsess = find_channel (sess->server, nick);
			if (newsess) {
				while (!g_queue_is_empty(msgs)) {
					msg = (gchar *)g_queue_pop_head(msgs);
					inbound_chanmsg (newsess->server, NULL, newsess->channel, newsess->server->nick, msg, TRUE, FALSE);
				}
			} else {
				while (!g_queue_is_empty(msgs)) {
					msg = (gchar *)g_queue_pop_head(msgs);
                                        signal_emit("user message private", 3, sess, nick, msg);
				}
			}

			g_queue_free(msgs);
			g_queue_free(mcopy);

			return CMD_EXEC_OK;
		}
	}
	return CMD_EXEC_FAIL;
}

static CommandResult
cmd_names (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	if (*word[2])
	  	sess->server->p_names (sess->server, word[2]);
	else
		sess->server->p_names (sess->server, sess->channel);
	return CMD_EXEC_OK;
}

static CommandResult
cmd_nctcp (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	if (*word_eol[3])
	{
		sess->server->p_nctcp (sess->server, word[2], word_eol[3]);
		return CMD_EXEC_OK;
	}
	return CMD_EXEC_FAIL;
}

static CommandResult
cmd_newserver (struct session *sess, char *tbuf, char *word[],
					char *word_eol[])
{
	if (strcmp (word[2], "-noconnect") == 0)
	{
		new_ircwindow (NULL, word[3], SESS_SERVER, 0);
		return CMD_EXEC_OK;
	}

	sess = new_ircwindow (NULL, NULL, SESS_SERVER, 0);
	cmd_server (sess, tbuf, word, word_eol);
	return CMD_EXEC_OK;
}

static CommandResult
cmd_nick (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	char *nick = word[2];
	if (*nick)
	{
		if (sess->server->connected)
			sess->server->p_change_nick (sess->server, nick);
		else
			inbound_newnick (sess->server, sess->server->nick, nick, TRUE);
		return CMD_EXEC_OK;
	}
	return CMD_EXEC_FAIL;
}

static CommandResult
cmd_notice (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	if (*word[2] && *word_eol[3])
	{
		sess->server->p_notice (sess->server, word[2], word_eol[3]);
		signal_emit("user notice", 3, sess, word, word_eol);
		return CMD_EXEC_OK;
	}
	return CMD_EXEC_FAIL;
}

static CommandResult
cmd_notify (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	int i = 1;
	char *net = NULL;

	if (*word[2])
	{
		if (strcmp (word[2], "-n") == 0)	/* comma sep network list */
		{
			net = word[3];
			i += 2;
		}

		while (1)
		{
			i++;
			if (!*word[i])
				break;
			if (notify_deluser (word[i]))
			{
				signal_emit("notify removed", 2, sess, word[i]);
				return CMD_EXEC_OK;
			}
			notify_adduser (word[i], net);
			signal_emit("notify added", 2, sess, word[i]);
		}
	} else
		notify_showlist (sess);
	return CMD_EXEC_OK;
}

static CommandResult
cmd_op (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	int i = 2;

	while (1)
	{
		if (!*word[i])
		{
			if (i == 2)
				return CMD_EXEC_FAIL;
			send_channel_modes (sess, tbuf, word, 2, i, '+', 'o', 0);
			return CMD_EXEC_OK;
		}
		i++;
	}
}

static CommandResult
cmd_part (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	char *chan = word[2];
	char *reason = word_eol[3];
	if (!*chan)
		chan = sess->channel;
	if ((*chan) && is_channel (sess->server, chan))
	{
		if (reason[0] == 0)
			reason = NULL;
		server_sendpart (sess->server, chan, reason);
		return CMD_EXEC_OK;
	}
	else if (*chan && sess->channel != NULL)
	{
		chan = sess->channel;
		reason = word_eol[2];

		server_sendpart(sess->server, chan, reason);
		return CMD_EXEC_OK;
	}

	return CMD_EXEC_FAIL;
}

static CommandResult
cmd_ping (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	char timestring[64];
	unsigned long tim;
	char *to = word[2];

	tim = make_ping_time ();

	snprintf (timestring, sizeof (timestring), "%lu", tim);
	sess->server->p_ping (sess->server, to, timestring);

	return CMD_EXEC_OK;
}

void
open_query (server *serv, char *nick, gboolean focus_existing)
{
	session *sess;

	sess = find_dialog (serv, nick);
	if (!sess)
		new_ircwindow (serv, nick, SESS_DIALOG, 1);
	else if (focus_existing)
		fe_ctrl_gui (sess, 2, 0);	/* bring-to-front */
}

static CommandResult
cmd_query (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	char *nick = word[2];
	gboolean focus = TRUE;
        gboolean send_msg = FALSE;

	if (strcmp (word[2], "-nofocus") == 0)
	{
		nick = word[3];
		focus = FALSE;
		if (word[4] != NULL)
		{
			send_msg = TRUE;
                        word_eol[3] = g_strdup(word_eol[4]);
                }
	} else if (word[3] != NULL)
        {
		send_msg = TRUE;
	}

	if (*nick && !is_channel (sess->server, nick))
	{
		open_query (sess->server, nick, focus);
		if (send_msg)
		{
			cmd_msg(sess, tbuf, word, word_eol);
		}
		return CMD_EXEC_OK;
	}
	return CMD_EXEC_FAIL;
}

static CommandResult
cmd_quote (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	char *raw = word_eol[2];

	if (!raw)
		return CMD_EXEC_FAIL;

	sess->server->p_raw (sess->server, raw);

	return CMD_EXEC_OK;
}

static CommandResult
cmd_reconnect (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	int tmp = prefs.recon_delay;
	GSList *list;
	server *serv = sess->server;

	prefs.recon_delay = 0;

	if (!g_ascii_strcasecmp (word[2], "ALL"))
	{
		list = serv_list;
		while (list)
		{
			serv = list->data;
			if (serv->connected)
				serv->auto_reconnect (serv, TRUE, -1);
			list = list->next;
		}
	}
	/* If it isn't "ALL" and there is something
	there it *should* be a server they are trying to connect to*/
	else if (*word[2])
	{
		int offset = 0;

		if (*word[4+offset])
			g_strlcpy (serv->password, word[4+offset], sizeof (serv->password));
		if (*word[3+offset])
			serv->port = atoi (word[3+offset]);
		g_strlcpy (serv->hostname, word[2+offset], sizeof (serv->hostname));
		serv->auto_reconnect (serv, TRUE, -1);
	}
	else
	{
		serv->auto_reconnect (serv, TRUE, -1);
	}
	prefs.recon_delay = tmp;

	return CMD_EXEC_OK;
}

static CommandResult
cmd_recv (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	if (*word_eol[2])
	{
		sess->server->p_inline (sess->server, word_eol[2], strlen (word_eol[2]));
		return CMD_EXEC_OK;
	}

	return CMD_EXEC_FAIL;
}

static CommandResult
cmd_say(struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	char *speech = word_eol[2];
	if (*speech)
	{
		handle_say(sess, speech, FALSE, TRUE, TRUE);
		return CMD_EXEC_OK;
	}
	return CMD_EXEC_FAIL;
}

static CommandResult
cmd_send (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	guint32 addr;
	socklen_t len;
	struct sockaddr_in SAddr;

	if (!word[2][0])
		return CMD_EXEC_FAIL;

	addr = dcc_get_my_address ();
	if (addr == 0)
	{
		/* use the one from our connected server socket */
		memset (&SAddr, 0, sizeof (struct sockaddr_in));
		len = sizeof (SAddr);
		getsockname (sess->server->sok, (struct sockaddr *) &SAddr, &len);
		addr = SAddr.sin_addr.s_addr;
	}
	addr = ntohl (addr);

	if ((addr & 0xffff0000) == 0xc0a80000 ||	/* 192.168.0.0/24  */
            (addr & 0xfff00000) == 0xac100000 ||        /* 172.16.0.0/12   */
	    (addr & 0xff000000) == 0x0a000000)		/* 10.0.0.0/8      */
		/* we got a private net address, let's PSEND or it'll fail */
		snprintf (tbuf, 512, "DCC PSEND %s", word_eol[2]);
	else
		snprintf (tbuf, 512, "DCC SEND %s", word_eol[2]);

	handle_command (sess, tbuf, FALSE);

	return CMD_EXEC_OK;
}

static int
parse_irc_url (char *url, char *server_name[], char *port[], char *channel[], int *use_ssl)
{
	char *co;
#ifdef GNUTLS
	if (g_ascii_strncasecmp ("ircs://", url, 7) == 0)
	{
		*use_ssl = TRUE;
		*server_name = url + 7;
		goto urlserv;
	}
#endif

	if (g_ascii_strncasecmp ("irc://", url, 6) == 0)
	{
		*server_name = url + 6;
#ifdef GNUTLS
urlserv:
#endif
		/* check for port */
		co = strchr (*server_name, ':');
		if (co)
		{
			*port = co + 1;
			*co = 0;
		} else
			co = *server_name;
		/* check for channel - mirc style */
		co = strchr (co + 1, '/');
		if (co)
		{
			*co = 0;
			co++;
			if (*co == '#')
				*channel = co+1;
			else
				*channel = co;

		}
		return TRUE;
	}
	return FALSE;
}

static CommandResult
cmd_server (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
    gchar *servername;
    gint port = -1;
    gchar *pass = NULL;
    gchar *channel = NULL;
#ifdef GNUTLS
    gint use_ssl = FALSE;
#endif
    gboolean use_uri = FALSE;
    server *serv = sess->server;
    CommandOption options[] = {
#ifdef GNUTLS
        {"ssl", TYPE_BOOLEAN, &use_ssl, "Enable SSL; if passing this, do not pass -e."},
        {"e", TYPE_BOOLEAN, &use_ssl, "Enable SSL; if passing this, do not pass -ssl."},
#endif
        {"channel", TYPE_STRING, &channel, "Join channel(s) after connecting; if passing this, do not pass -j."},
        {"j", TYPE_STRING, &channel, "Join channel(s) after connecting; if passing this, do not pass -channel."},
        {"port", TYPE_INTEGER, &port, "Connect on this port."},
        {"pass", TYPE_STRING, &pass, "Use this password."},
        {NULL}
    };
    gint len = g_strv_length(word);

    if (!strncasecmp(word[2], "irc://", 6) || !strncasecmp(word[2], "ircs://", 7))
    {
        gchar *uri_port = "-1";
        gint i;

        if (!parse_irc_url (word[2], &servername, &uri_port, &channel, &use_ssl))
            return CMD_EXEC_FAIL;

        if (uri_port[0] != '-')
        {
            port = 0;
            for (i = 0; i < strlen(uri_port) && g_ascii_isdigit(uri_port[i]); i++)
            {
                port *= 10;
                port += g_ascii_digit_value(uri_port[i]);
            }
        }
        /*
         * URI doesn't include a channel prefix because zed and khaled are
         * first-rate idiots.
         */
        if ((channel[0] != '#') && channel[0] != '\0')
            channel = g_strdup_printf("#%s", channel);

        use_uri = TRUE;
    } else
    {
        command_option_parse(sess, &len, &word, options);

        servername = word[0];
    }

    if (!servername)
        return CMD_EXEC_FAIL;

    sess->server->network = NULL;

    if (channel)
        g_strlcpy(sess->willjoinchannel, channel, sizeof(sess->willjoinchannel));

    if (pass)
    {
        g_strlcpy(serv->password, pass, sizeof(serv->password));
    }
#ifdef GNUTLS
    serv->use_ssl = use_ssl;
    serv->accept_invalid_cert = TRUE;
#endif

    /* try to connect by Network name */
    if (servlist_connect_by_netname (sess, servername, TRUE))
        return CMD_EXEC_OK;

    serv->connect(serv, servername, port, FALSE);

    /* try to associate this connection with a listed network */
    if (!serv->network)
        /* search for this hostname in the entire server list */
        serv->network = servlist_net_find_from_server (servername);
    /* may return NULL, but that's OK */

    /* clean up after URI-specific anti-stupidity measures. */
    if (use_uri && channel[0] != '\0')
        g_free(channel);

    return CMD_EXEC_OK;
}

static CommandResult
cmd_topic (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	if (word[2][0] && is_channel (sess->server, word[2]))
		sess->server->p_topic (sess->server, word[2], word_eol[3]);
	else
		sess->server->p_topic (sess->server, sess->channel, word_eol[2]);
	return CMD_EXEC_OK;
}

static CommandResult
cmd_unload (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
#if 0
	int len, by_file = FALSE;

	len = strlen (word[2]);
#if defined(__hpux)
	if (len > 3 && g_ascii_strcasecmp (word[2] + len - 3, ".sl") == 0)
#else
	if (len > 3 && g_ascii_strcasecmp (word[2] + len - 3, ".so") == 0)
#endif
		by_file = TRUE;

	switch (plugin_kill (word[2], by_file))
	{
	case 0:
			PrintText (sess, _("No such plugin found.\n"));
			break;
	case 1:
			return CMD_EXEC_OK;
	case 2:
			PrintText (sess, _("That plugin is refusing to unload.\n"));
			break;
	}
#endif

	return CMD_EXEC_FAIL;
}

static server *
find_server_from_hostname (char *hostname)
{
	GSList *list = serv_list;
	server *serv;

	while (list)
	{
		serv = list->data;
		if (!g_ascii_strcasecmp (hostname, serv->hostname) && serv->connected)
			return serv;
		list = list->next;
	}

	return NULL;
}

static server *
find_server_from_net (void *net)
{
	GSList *list = serv_list;
	server *serv;

	while (list)
	{
		serv = list->data;
		if (serv->network == net && serv->connected)
			return serv;
		list = list->next;
	}

	return NULL;
}

static void
url_join_only (server *serv, char *tbuf, char *channel)
{
	/* already connected, JOIN only. FIXME: support keys? */
	tbuf[0] = '#';
	/* tbuf is 4kb */
	g_strlcpy ((tbuf + 1), channel, 256);
	serv->p_join (serv, tbuf, "");
}

static CommandResult
cmd_url (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	if (word[2][0])
	{
		char *server_name = NULL;
		char *port = NULL;
		char *channel = NULL;
		char *url = g_strdup (word[2]);
		int use_ssl = FALSE;
		void *net;
		server *serv;

		if (parse_irc_url (url, &server_name, &port, &channel, &use_ssl))
		{
			/* maybe we're already connected to this net */

			/* check for "FreeNode" */
			net = servlist_net_find (server_name, NULL, g_ascii_strcasecmp);
			/* check for "irc.eu.freenode.net" */
			if (!net)
				net = servlist_net_find_from_server (server_name);

			if (net)
			{
				/* found the network, but are we connected? */
				serv = find_server_from_net (net);
				if (serv)
				{
					url_join_only (serv, tbuf, channel);
					g_free (url);
					return CMD_EXEC_OK;
				}
			}
			else
			{
				/* an un-listed connection */
				serv = find_server_from_hostname (server_name);
				if (serv)
				{
					url_join_only (serv, tbuf, channel);
					g_free (url);
					return CMD_EXEC_OK;
				}
			}

			/* not connected to this net, open new window */
			cmd_newserver (sess, tbuf, word, word_eol);

		} else
			fe_open_url (word[2]);
		g_free (url);
		return CMD_EXEC_OK;
	}

	return CMD_EXEC_FAIL;
}

static CommandResult
userlist_cb (struct User *user, session *sess)
{
	time_t lt;

	if (!user->lasttalk)
		lt = 0;
	else
		lt = time (0) - user->lasttalk;
	PrintTextf (sess,
				"\00306%s\t\00314[\00310%-38s\00314] \017ov\0033=\017%d%d away=%u lt\0033=\017%d\n",
				user->nick, user->hostname, user->op, user->voice, user->away, lt);

	return TRUE;
}

static CommandResult
cmd_userlist (struct session *sess, char *tbuf, char *word[],
				  char *word_eol[])
{
	tree_foreach (sess->usertree, (tree_traverse_func *)userlist_cb, sess);
	return CMD_EXEC_OK;
}

static int
wallchop_cb (struct User *user, multidata *data)
{
	if (user->op)
	{
		if (data->i)
			strcat (data->tbuf, ",");
		strcat (data->tbuf, user->nick);
		data->i++;
	}
	if (data->i == 5)
	{
		data->i = 0;
		sprintf (data->tbuf + strlen (data->tbuf),
					" :[@%s] %s", data->sess->channel, data->reason);
		data->sess->server->p_raw (data->sess->server, data->tbuf);
		strcpy (data->tbuf, "NOTICE ");
	}

	return TRUE;
}

static CommandResult
cmd_wallchop (struct session *sess, char *tbuf, char *word[],
				  char *word_eol[])
{
	multidata data;

	if (!(*word_eol[2]))
		return CMD_EXEC_FAIL;

	strcpy (tbuf, "NOTICE ");

	data.reason = word_eol[2];
	data.tbuf = tbuf;
	data.i = 0;
	data.sess = sess;
	tree_foreach (sess->usertree, (tree_traverse_func*)wallchop_cb, &data);

	if (data.i)
	{
		sprintf (tbuf + strlen (tbuf),
					" :[@%s] %s", sess->channel, word_eol[2]);
		sess->server->p_raw (sess->server, tbuf);
	}

	return CMD_EXEC_OK;
}

static CommandResult
cmd_halfop (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	int i = 2;

	while (1)
	{
		if (!*word[i])
		{
			if (i == 2)
				return CMD_EXEC_FAIL;
			send_channel_modes (sess, tbuf, word, 2, i, '+', 'h', 0);
			return CMD_EXEC_OK;
		}
		i++;
	}
}

static CommandResult
cmd_voice (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	int i = 2;

	while (1)
	{
		if (!*word[i])
		{
			if (i == 2)
				return CMD_EXEC_FAIL;
			send_channel_modes (sess, tbuf, word, 2, i, '+', 'v', 0);
			return CMD_EXEC_OK;
		}
		i++;
	}
}

/* this is now a table used to fill the command dictionary now with our builtins.
   it doesn't matter if it's properly sorted or not. --nenolod */
struct commands xc_cmds[] = {
	{"AWAY", cmd_away, 1, 0, 1, N_("AWAY [<reason>]\ntoggles away status")},
	{"BAN", cmd_ban, 1, 1, 1,
	 N_("BAN [-except] [-type <0-3>] <mask>\n"
            "Without the -except flag, this command bans the supplied mask. The -type flag\n"
            "specifies the masking mechanism used when the mask is a nick.")},
	{"CHARSET", cmd_charset, 0, 0, 1, 0},
	{"CLEAR", cmd_clear, 0, 0, 1, N_("CLEAR [ALL|HISTORY]\nClears the current text window or command history")},
	{"CLOSE", cmd_close, 0, 0, 1, N_("CLOSE\nCloses the current window/tab")},
	{"CONNECT", cmd_newserver, 0, 0, 1,
	 N_("CONNECT [-noconnect] <hostname>\nCreates a new server tab, wrapping SERVER if -noconnect is not specified.")},
	{"CTCP", cmd_ctcp, 1, 0, 1,
	 N_("CTCP <nick> <message>\nSend the CTCP message to nick; common messages are VERSION and USERINFO")},
	{"CYCLE", cmd_hop, 1, 1, 1,
	 N_("CYCLE [<channel>]\nParts the current or given channel and immediately rejoins")},
	{"DCC", cmd_dcc, 0, 0, 1,
	 N_("\n"
	 "DCC GET <nick>                      - accept an offered file\n"
	 "DCC SEND [-maxcps=#] <nick> [file]  - send a file to someone\n"
	 "DCC PSEND [-maxcps=#] <nick> [file] - send a file using passive mode\n"
	 "DCC LIST                            - show DCC list\n"
	 "DCC CHAT <nick>                     - offer DCC CHAT to someone\n"
	 "DCC PCHAT <nick>                    - offer DCC CHAT using passive mode\n"
	 "DCC CLOSE <type> <nick> <file>         example:\n"
	 "         /dcc close send johnsmith file.tar.gz")},
	{"DEBUG", cmd_debug, 0, 0, 1, 0},

	{"DEHALFOP", cmd_dehalfop, 1, 1, 1,
	 N_("DEHALFOP <nick>\nremoves halfop status from the nick on the current channel (needs op)")},
	{"DEOP", cmd_deop, 1, 1, 1,
	 N_("DEOP <nick>\nremoves op status from the nick on the current channel (needs op)")},
        {"DESCRIBE", cmd_describe, 1, 0, 1,
	 N_("DESCRIBE <target> <text>\nPerforms an action in the target channel/query with the specified text.")},
	{"DEVOICE", cmd_devoice, 1, 1, 1,
	 N_("DEVOICE <nick>\nRemoves voice status from the nick on the current channel (needs chanop)")},
	{"DISCONNECT", cmd_discon, 0, 0, 1, N_("DISCONNECT\nDisconnects from server")},
	{"ECHO", cmd_echo, 0, 0, 1, N_("ECHO <text>\nPrints text locally")},
	{"EXIT", cmd_killall, 0, 0, 1, N_("EXIT\nTerminates all connections and closes Conspire.")},
	{"FLUSHQ", cmd_flushq, 0, 0, 1, N_("FLUSHQ\nFlushes the current server's send queue")},
	{"FOREACH", cmd_foreach, 0, 0, 1, N_("FOREACH <[local-]channel|server|[local-]query>\nPerforms a given command for all items of the specified type.")},
	{"GATE", cmd_gate, 0, 0, 1,
	 N_("GATE <host> [<port>]\nProxies through a host, port defaults to 23")},
	{"GHOST", cmd_ghost, 1, 0, 1, N_("GHOST <nick> <password>\nKills a ghosted nickname")},
	{"HALFOP", cmd_halfop, 1, 1, 1,
	 N_("HALFOP <nick>\nGives halfop status to the nick (needs op)")},
	{"HELP", cmd_help, 0, 0, 1, 0},
	{"HOP", cmd_hop, 1, 1, 1,
	 N_("HOP [<channel>]\nParts the current or given channel and immediately rejoins")},
	{"ID", cmd_id, 1, 0, 1, N_("ID <password>\nIdentifies yourself to nickserv")},
	{"IGNORE", cmd_ignore, 0, 0, 1,
	 N_("IGNORE [flags] <mask>\n"
            "Ignores messages carrying the specific flags from the specified mask.\n\n"
            "Supported flags:\n"
            "-except   User will %Bnot%B be ignored.\n"
            "-private  Private messages from the user will be ignored.\n"
            "-public   Public messages from the user will be ignored.\n"
            "-notice   Notices from the user will be ignored.\n"
            "-ctcp     CTCPs from the user will be ignored.\n"
            "-action   Actions, both public & private, from the user will be ignored.\n"
            "-join     Join messages from the user will be ignored.\n"
            "-part     Part messages from the user will be ignored.\n"
            "-quit     Quit messages from the user will be ignored.\n"
            "-kick     Kicks from the user will be ignored.\n"
            "-mode     Mode changes from the user will be ignored.\n"
            "-topic    Topic changes from the user will be ignored.\n"
            "-invite   Invitations from the user will be ignored.\n"
            "-nick     Nick changes from the user will be ignored.\n"
            "-dcc      DCC file transfers from the user will be ignored.\n"
            "-hilight  Messages from the user that would otherwise hilight, won't.\n"
            "-all      All messages from the user will be ignored.\n\n"
            "Passing -all with other flags will pass through messages of those types. In addition, using /ignore again on an ignored mask replaces the old ignore with the new one."
            )},

	{"INVITE", cmd_invite, 1, 0, 1,
	 N_("INVITE <nick> [<channel>]\nInvites someone to a channel, by default the current channel (needs chanop)")},
	{"JOIN", cmd_join, 1, 0, 0, N_("JOIN <channel>\nJoins the channel")},
	{"KICK", cmd_kick, 1, 1, 1,
	 N_("KICK <nick>\nKicks the nick from the current channel (needs op)")},
	{"KICKBAN", cmd_kickban, 1, 1, 1,
	 N_("KICKBAN [-type <n>] <nick> [reason]\nBans then kicks the nick from the current channel (needs op)")},
	{"LAGCHECK", cmd_lagcheck, 0, 0, 1,
	 N_("LAGCHECK\nForces a new lag check")},
	{"LASTLOG", cmd_lastlog, 0, 0, 1,
	 N_("LASTLOG <string>\nSearches for a string in the buffer")},
	{"LIST", cmd_list, 1, 0, 1, 0},
	{"LOAD", cmd_load, 0, 0, 1, N_("LOAD [-e] <file>\nLoads a plugin")},

	{"ME", cmd_me, 0, 0, 1,
	 N_("ME <action>\nSends the action to the current channel (actions are written in the 3rd person, like /me jumps)")},
	{"MODE", cmd_mode, 1, 0, 1, 0},
	{"MSG", cmd_msg, 0, 0, 1, N_("MSG <nick> <message>\nSends a private message")},

	{"NAMES", cmd_names, 1, 0, 1,
	 N_("NAMES\nLists the nicks on the current channel")},
	{"CTCPREPLY", cmd_nctcp, 1, 0, 1,
	 N_("CTCPREPLY <nick> <message>\nSends a CTCP reply")},
	{"NICK", cmd_nick, 0, 0, 1, N_("NICK <nickname>\nSets your nick")},

	{"NOTICE", cmd_notice, 1, 0, 1,
	 N_("NOTICE <nick/channel> <message>\nSends a notice. Notices should not elicit a reply.")},
	{"NOTIFY", cmd_notify, 0, 0, 1,
	 N_("NOTIFY [-n network1[,network2,...]] [<nick>]\nDisplays your notify list or adds someone to it")},
	{"OP", cmd_op, 1, 1, 1,
	 N_("OP <nick>\nGives op status to the nick (needs op)")},
	{"PART", cmd_part, 1, 1, 1,
	 N_("PART [<channel>] [<reason>]\nLeaves the channel, by default the current one")},
	{"PING", cmd_ping, 1, 0, 1,
	 N_("PING <nick | channel>\nSends a CTCP PING to a nick or channel")},
	{"QUERY", cmd_query, 0, 0, 1,
	 N_("QUERY [-nofocus] <nick>\nOpens a new query window with someone")},
	{"QUIT", cmd_quit, 0, 0, 1,
	 N_("QUIT [<reason>]\nDisconnects from the current server")},
	{"QUOTE", cmd_quote, 1, 0, 1,
	 N_("QUOTE <text>\nSends the text in raw form to the server")},
#ifdef GNUTLS
	{"RECONNECT", cmd_reconnect, 0, 0, 1,
	 N_("RECONNECT [-ssl] [<host>] [<port>] [<password>]\nCan be called just as /RECONNECT to reconnect to the current server or with /RECONNECT ALL to reconnect to all the open servers")},
#else
	{"RECONNECT", cmd_reconnect, 0, 0, 1,
	 N_("RECONNECT [<host>] [<port>] [<password>]\nCan be called just as /RECONNECT to reconnect to the current server or with /RECONNECT ALL to reconnect to all the open servers")},
#endif
	{"RECV", cmd_recv, 1, 0, 1, N_("RECV <text>\nSend raw text to Conspire, as if it were received from the server")},

	{"SAY", cmd_say, 0, 0, 1,
	 N_("SAY <text>\nSends the text to the object in the current window")},
	{"SEND", cmd_send, 0, 0, 1, N_("SEND <nick> [<file>]")},
#ifdef GNUTLS
	{"SERVER", cmd_server, 0, 0, 1,
	 N_("SERVER [flags] <host>\nConnects to a server.\n\n"
		"Supported flags:\n"
		"-ssl or -e      Connect via SSL.\n"
		"-channel or -j <channel>  Join channel(s) on connect.\n"
		"-port <port>              Connect on the specified port. (default: 6667)\n"
		"-pass <password>          Connect with the specified password.\n\n"
                "This command also supports irc://server:port/channel and ircs://server:port/channel URIs.\n"
                "Note: The network editor uses a variant of this scheme which requires that SSL ports be prefixed with a +.\n"
		)},
#else
	{"SERVER", cmd_server, 0, 0, 1,
	 N_("SERVER [flags] <host>\nConnects to a server.\n\n"
		"Supported flags:\n"
		"-channel or -j <channel>  Join channel(s) on connect.\n"
		"-port <port>              Connect on the specified port. (default: 6667)\n"
		"-pass <password>          Connect with the specified password.\n\n"
                "This command also supports irc://server:port/channel URIs.\n"
		)},
#endif
	{"SET", cmd_set, 0, 0, 1, N_("SET [-e] [-or] [-quiet] <variable> [<value>]")},
	{"TOPIC", cmd_topic, 1, 1, 1,
	 N_("TOPIC [<topic>]\nsets the topic if one is given, else shows the current topic")},
	{"UNBAN", cmd_unban, 1, 1, 1,
	 N_("UNBAN [-except] <masks>\nRemoves all masks from the ban list (or ban-exception list, with -except).")},
	{"UNIGNORE", cmd_unignore, 0, 0, 1, N_("UNIGNORE <mask>\nUnignores the given mask.")},
	{"UNLOAD", cmd_unload, 0, 0, 1, N_("UNLOAD <name>\nUnloads a plugin")},
	{"URL", cmd_url, 0, 0, 1, N_("URL <url>\nOpens a URL in your browser")},
	{"USERLIST", cmd_userlist, 1, 1, 1, 0},
	{"VOICE", cmd_voice, 1, 1, 1,
	 N_("VOICE <nick>\nGives voice status to someone (needs op)")},
	{"WALLCHOP", cmd_wallchop, 1, 1, 1,
	 N_("WALLCHOP <message>\nSends a message to all ops on the current channel")},
	{0, 0, 0, 0, 0, 0}
};

void
command_init(void)
{
	struct commands *command;
	int i = 0;

	/* XXX: doesn't use flags */
	for (command = &xc_cmds[i]; command->name != NULL; i++, command = &xc_cmds[i])
		command_register(xc_cmds[i].name, xc_cmds[i].help, CMD_HANDLE_QUOTES, xc_cmds[i].callback);
}

static void
help (session *sess, char *tbuf, char *helpcmd, int quiet)
{
	Command *cmd;

	cmd = command_lookup(helpcmd);

	if (cmd)
	{
		if (cmd->helptext)
		{
			snprintf (tbuf, TBUFSIZE, _("Usage: %s\n"), _(cmd->helptext));
			PrintText (sess, tbuf);
		}
		else
		{
			if (!quiet)
				PrintText (sess, _("\nNo help available on that command.\n"));
		}
		return;
	}

	if (!quiet)
		PrintText (sess, _("No such command.\n"));
}

/* inserts %a, %c, %d etc into buffer. Also handles &x %x for word/word_eol. *
 *   returns 2 on buffer overflow
 *   returns 1 on success                                                    *
 *   returns 0 on bad-args-for-user-command                                  *
 * - word/word_eol args might be NULL                                        *
 * - this beast is used for UserCommands, UserlistButtons and CTCP replies   */

gint
auto_insert (gchar *dest, gint destlen, guchar *src, gchar *word[],
         gchar *word_eol[], gchar *a, gchar *c, gchar *d, gchar *e, gchar *h,
         gchar *n, gchar *s)
{
    const gchar *p;
    gchar *vp;
    gchar *i = dest;
    gchar *temp = NULL;
    gchar buf[32];

    g_return_val_if_fail(src != NULL, 0);

    for (p = src; *p != '\0' && (dest - i) < destlen; p++)
    {
        switch (*p)
        {
            case '%':
                switch (*(p + 1))
                {
                    case 'a':
                        temp = a;
                        break;
                    case 'c':
                        temp = c;
                        break;
                    case 'd':
                        temp = d;
                        break;
                    case 'e':
                        temp = e;
                        break;
                    case 'h':
                        temp = h;
                        break;
                    case 'm':
                        temp = get_cpu_str();
                        break;
                    case 'n':
                        temp = n;
                        break;
                    case 's':
                        temp = s;
                        break;
                    case 't':
                        {
                            time_t now = time(0);
                            temp = ctime(&now);
                        }
                        break;
                    case 'u':
                        {
                            gint arg = 0;
                            gint arglen = 0;
                            p++;
                            while (isxdigit(*(p)) && arglen < 7)
                            {
                                arg = arg << 1;
                                arg += *p - '0';
                                if (*p >= 'A')
                                    arg -= 7;
                                p++;
                                arglen++;
                            }
                            if (arglen == 5) {
                                p--;
                                arg >>= 1;
                            }
                            g_unichar_to_utf8(arg, temp);
                        }
                        break;
                    case 'v':
                        temp = PACKAGE_VERSION;
                        break;
                    case 'x':
                        {
                            gint arg = 0;
                            p++;
                            while (isxdigit(*p) && arg < 255)
                            {
                                arg = arg << 1;
                                arg += *p - '0';
                                if (*p >= 'A')
                                    arg -= 7;
                                p++;
                            }
                            g_unichar_to_utf8(arg, temp);
                        }
                        break;
                    case 'y':
                        {
                            time_t now = time(0);
                            struct tm *diem = localtime(&now);
                            snprintf (buf, sizeof (buf), "%4d%02d%02d", 1900 + diem->tm_year, 1 + diem->tm_mon, diem->tm_mday);

                            temp = buf;
                        }
                        break;
                }
                if (temp != NULL && *temp)
                {
                    for (vp = temp; *vp != '\0' && (dest - i) < destlen; vp++)
                    {
                        *i++ = *vp;
                    }
                    p++;
                } else {
                    *i++ = *p;
                }
                break;
            case '\\':
                p++;
                *i++ = *p;
                break;
            case '$':
                {
                    gchar *vp;
                    gint arg = 0;
                    p++;

                    while (isdigit(*p))
                    {
                        arg *= 10;
                        arg += (*p - '0');
                        p++;
                    }
                    if (*p == '-')
                    {
                        for (vp = word_eol[arg]; *vp != '\0' && (dest - i) < destlen; vp++)
                        {
                            *i++ = *vp;
                        }
                    } else
                    {
                        p--;
                        for (vp = word[arg]; *vp != '\0' && (dest - i) < destlen; vp++)
                        {
                            *i++ = *vp;
                        }
                    }
                }

                break;
            default:
                *i++ = *p;
        }
    }
    *i++ = '\0';

    return 1;
}

void
check_special_chars (char *cmd, int do_ascii) /* check for %X */
{
	int occur = 0;
	int len = strlen (cmd);
	char *buf, *utf;
	char tbuf[4];
	int i = 0, j = 0;
	gsize utf_len;

	if (!len)
		return;

	buf = malloc (len + 1);

	if (buf)
	{
		while (cmd[j])
		{
			switch (cmd[j])
			{
			case '%':
				occur++;
				if (	do_ascii &&
						j + 3 < len &&
						(isdigit ((unsigned char) cmd[j + 1]) && isdigit ((unsigned char) cmd[j + 2]) &&
						isdigit ((unsigned char) cmd[j + 3])))
				{
					tbuf[0] = cmd[j + 1];
					tbuf[1] = cmd[j + 2];
					tbuf[2] = cmd[j + 3];
					tbuf[3] = 0;
					buf[i] = atoi (tbuf);
					utf = g_locale_to_utf8 (buf + i, 1, 0, &utf_len, 0);
					if (utf)
					{
						memcpy (buf + i, utf, utf_len);
						g_free (utf);
						i += (utf_len - 1);
					}
					j += 3;
				} else
				{
					switch (cmd[j + 1])
					{
					case 'R':
						buf[i] = '\026';
						break;
					case 'U':
						buf[i] = '\037';
						break;
					case 'B':
						buf[i] = '\002';
						break;
					case 'C':
						buf[i] = '\003';
						break;
					case 'O':
						buf[i] = '\017';
						break;
					case 'H':	/* CL: invisible text code */
						buf[i] = HIDDEN_CHAR;
						break;
					case '%':
						buf[i] = '%';
						break;
					default:
						buf[i] = '%';
						j--;
						break;
					}
					j++;
					break;
			default:
					buf[i] = cmd[j];
				}
			}
			j++;
			i++;
		}
		buf[i] = 0;
		if (occur)
			strcpy (cmd, buf);
		free (buf);
	}
}

typedef struct
{
	char *nick;
	int len;
	struct User *best;
	int bestlen;
	char *space;
	char *tbuf;
} nickdata;

static int
nick_comp_cb (struct User *user, nickdata *data)
{
	int lenu;

	if (!rfc_ncasecmp (user->nick, data->nick, data->len))
	{
		lenu = strlen (user->nick);
		if (lenu == data->len)
		{
			snprintf (data->tbuf, TBUFSIZE, "%s%s", user->nick, data->space);
			data->len = -1;
			return FALSE;
		} else if (lenu < data->bestlen)
		{
			data->bestlen = lenu;
			data->best = user;
		}
	}

	return TRUE;
}

static void
perform_nick_completion (struct session *sess, char *cmd, char *tbuf)
{
	int len;
	char *space = strchr (cmd, ' ');
	if (space && space != cmd)
	{
		if (space[-1] == prefs.nick_suffix[0] && space - 1 != cmd)
		{
			len = space - cmd - 1;
			if (len < NICKLEN)
			{
				char nick[NICKLEN];
				nickdata data;

				memcpy (nick, cmd, len);
				nick[len] = 0;

				data.nick = nick;
				data.len = len;
				data.bestlen = INT_MAX;
				data.best = NULL;
				data.tbuf = tbuf;
				data.space = space - 1;
				tree_foreach (sess->usertree, (tree_traverse_func *)nick_comp_cb, &data);

				if (data.len == -1)
					return;

				if (data.best)
				{
					snprintf (tbuf, TBUFSIZE, "%s%s", data.best->nick, space - 1);
					return;
				}
			}
		}
	}

	strcpy (tbuf, cmd);
}

static void
user_command (session * sess, char *tbuf, char *cmd, char *word[],
				  char *word_eol[])
{
	if (!auto_insert (tbuf, 2048, cmd, word, word_eol, "", sess->channel, "",
							server_get_network (sess->server, TRUE), "",
							sess->server->nick, ""))
	{
		PrintText (sess, _("Bad arguments for alias.\n"));
		return;
	}

	handle_command (sess, tbuf, TRUE);
}

/* This seems some serious refactoring. -impl */
static void
handle_say(session *sess, char *text, gboolean check_spch, gboolean check_lastlog, gboolean check_completion)
{
	struct DCC *dcc;
	char *word[PDIWORDS];
	char *word_eol[PDIWORDS];
	char pdibuf_static[1024];
	char newcmd_static[1024];
	char *pdibuf = pdibuf_static;
	char *newcmd = newcmd_static;
	int len;
	int newcmdlen = sizeof newcmd_static;

	if(check_lastlog && strcmp(sess->channel, "(lastlog)") == 0)
	{
		lastlog(sess->lastlog_sess, text, sess->lastlog_regexp);
		return;
	}

	len = strlen (text);
	if(len >= sizeof pdibuf_static)
		pdibuf = malloc(len + 1);

	if(len + NICKLEN >= newcmdlen)
		newcmd = malloc(newcmdlen = len + NICKLEN + 1);

	if(check_spch && prefs.perc_color)
		check_special_chars(text, prefs.perc_ascii);

	/* split the text into words and word_eol */
	process_data_init(pdibuf, text, word, word_eol, TRUE, FALSE);

	/* incase a plugin did /close */
	if (!is_session(sess))
		goto xit;

	if (!sess->channel[0] || sess->type == SESS_SERVER || sess->type == SESS_NOTICES || sess->type == SESS_SNOTICES)
	{
		notj_msg(sess);
		goto xit;
	}

	if(check_completion && prefs.nickcompletion)
		perform_nick_completion(sess, text, newcmd);
	else
		g_strlcpy(newcmd, text, newcmdlen);

	text = newcmd;

	if (sess->type == SESS_DIALOG)
	{
		/* try it via dcc, if possible */
		dcc = dcc_write_chat (sess->channel, text);
		if (dcc)
		{
			inbound_chanmsg (sess->server, NULL, sess->channel,
								  sess->server->nick, text, TRUE, FALSE);
			set_topic (sess, net_ip (dcc->addr));
			goto xit;
		}
	}

	if (sess->server->connected)
	{
		GQueue *msgs = split_message(sess, text, "PRIVMSG");
		while (!g_queue_is_empty(msgs)) {
			text = (gchar *)g_queue_pop_head(msgs);
			inbound_chanmsg (sess->server, sess, sess->channel, sess->server->nick, text, TRUE, FALSE);
			sess->server->p_message (sess->server, sess->channel, text);
		}
        g_queue_free(msgs);
	} else
	{
		notc_msg (sess);
	}

xit:
	if (pdibuf != pdibuf_static)
		free (pdibuf);

	if (newcmd != newcmd_static)
		free (newcmd);
}

/* handle a command, without the '/' prefix */

int
handle_command(session *sess, char *cmd, int check_spch)
{
	struct popup *pop;
	int user_cmd = FALSE;
	GSList *list;
	char *word[PDIWORDS];
	char *word_eol[PDIWORDS];
	static int command_level = 0;
	Command *int_cmd;
	char pdibuf_static[1024];
	char tbuf_static[TBUFSIZE];
	char *pdibuf;
	char *tbuf;
	int len;
	int ret = TRUE;
	CommandExecResult exec_stat_;

	if (command_level > 99)
	{
		fe_message (_("Too many recursive aliases, aborting."), FE_MSG_ERROR);
		return TRUE;
	}
	command_level++;
	/* anything below MUST DEC command_level before returning */

	len = strlen (cmd);
	if (len >= sizeof (pdibuf_static))
		pdibuf = malloc (len + 1);
	else
		pdibuf = pdibuf_static;

	if ((len * 2) >= sizeof (tbuf_static))
		tbuf = malloc ((len * 2) + 1);
	else
		tbuf = tbuf_static;

	process_data_init (pdibuf, cmd, word, word_eol, TRUE, TRUE);
	int_cmd = command_lookup(word[1]);

	/* if the command does not have the CMD_HANDLE_QUOTES flag, split this using
	   traditional rules, otherwise split it like a shell does. --nenolod */
	if (int_cmd && !(int_cmd->flags & CMD_HANDLE_QUOTES))
		process_data_init (pdibuf, cmd, word, word_eol, FALSE, FALSE);

	if (check_spch && prefs.perc_color)
		check_special_chars (cmd, prefs.perc_ascii);

	/* incase a plugin did /close */
	if (!is_session (sess))
		goto xit;

	/* XXX: UGLY ALIAS HACK YUCK YUCK YUCK --nenolod */
	list = command_list;
	while (list)
	{
		pop = (struct popup *) list->data;
		if (!g_ascii_strcasecmp (pop->name, word[1]))
		{
			user_command (sess, tbuf, pop->cmd, word, word_eol);
			user_cmd = TRUE;
		}
		list = list->next;
	}

	if (user_cmd)
		goto xit;

	/* now check internal commands */
	exec_stat_ = command_execute(sess, word[1], tbuf, word, word_eol);

	switch (exec_stat_)
	{
	case COMMAND_EXEC_NOCMD:
		/* unknown command, just send it to the server and hope */
		if (!sess->server->connected)
			PrintText (sess, _("Unknown command. Try /help\n"));
		else
			sess->server->p_raw (sess->server, cmd);
		break;

	case COMMAND_EXEC_FAILED:
		ret = FALSE;
		help (sess, tbuf, word[1], TRUE);
		break;

	default:
		break;
	}

xit:
	command_level--;

	if (pdibuf != pdibuf_static)
		free (pdibuf);

	if (tbuf != tbuf_static)
		free (tbuf);

	return ret;
}

/* handle one line entered into the input box */

static int
handle_user_input (session *sess, char *text, int history, int nocommand)
{
#ifdef REGEX_SUBSTITUTION
	GSList *list = regex_replace_list;
	struct regex_entry *pop;
	GError *error = NULL;
#endif

	if (*text == '\0')
		return 1;

	if (history)
		history_add (&sess->history, text);

#ifdef REGEX_SUBSTITUTION
	if (prefs.text_regex_replace) {
		MOWGLI_ITER_FOREACH(list, regex_replace_list)
		{
			pop = (struct regex_entry *) list->data;
			text = g_regex_replace(pop->regex, text, strlen(text), 0, pop->cmd, 0, &error);
			if (error) {
				g_print("outbound.c: handle_user_input: %s", error->message);
				g_clear_error(&error);
			}
		}
	}
#endif

	/* is it NOT a command, just text? */
	if (nocommand || text[0] != prefs.cmdchar[0])
	{
		handle_say(sess, text, TRUE, TRUE, TRUE);
		return 1;
	}

	/* check for // */
	if (text[0] == prefs.cmdchar[0] && text[1] == prefs.cmdchar[0])
	{
		handle_say(sess, text + 1, TRUE, TRUE, TRUE);
		return 1;
	}

	if (prefs.cmdchar[0] == '/')
	{
		int i;
		const char *unix_dirs [] = {
			"/bin",        "/boot",  "/dev", "/etc", "/home",  "/lib",
			"/lost+found", "/media", "/mnt", "/opt", "/proc",  "/root",
			"/sbin",       "/tmp",   "/usr", "/var", "/gnome", NULL};
		for (i = 0; unix_dirs[i] != NULL; i++)
			if (strncmp (text, unix_dirs[i], strlen (unix_dirs[i]))==0)
			{
				handle_say(sess, text, TRUE, TRUE, TRUE);
				return 1;
			}
	}

	return handle_command(sess, text + 1, TRUE);
}

/* changed by Steve Green. Macs sometimes paste with imbedded \r */
void
handle_multiline (session *sess, char *cmd, int history, int nocommand)
{
	while (*cmd)
	{
		char *cr = cmd + strcspn(cmd, "\n\r");
		int end_of_string = *cr == 0;
		*cr = 0;
		if(!handle_user_input(sess, cmd, history, nocommand))
			return;
		if(end_of_string)
			break;
		cmd = cr + 1;
	}
}

void
handle_multiline_raw(session *sess, char *text)
{
	while(*text)
	{
		char *cr = text + strcspn(text, "\n\r");
		int end_of_string = (*cr == 0);
		*cr = 0;

		handle_say(sess, text, TRUE, FALSE, FALSE);
		if(end_of_string)
			break;
		text = cr + 1;
	}
}
