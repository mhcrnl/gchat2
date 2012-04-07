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

#include "xchat.h"
#include "xchatc.h"
#include "modes.h"
#include "server.h"
#include "text.h"
#include "fe.h"
#include "util.h"
#include "inbound.h"
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

typedef struct
{
	server *serv;
	char *op;
	char *deop;
	char *voice;
	char *devoice;
} mode_run;


/* word[] - list of nicks.
   wpos   - index into word[]. Where nicks really start.
   end    - index into word[]. Last entry plus one.
   sign   - a char, e.g. '+' or '-'
   mode   - a mode, e.g. 'o' or 'v'	*/
void
send_channel_modes (session *sess, char *tbuf, char *word[], int wpos,
						  int end, char sign, char mode, int modes_per_line)
{
	int usable_modes, orig_len, len, wlen, i, max;
	server *serv = sess->server;

	/* sanity check. IRC RFC says three per line. */
	if (serv->modes_per_line < 3)
		serv->modes_per_line = 3;
	if (modes_per_line < 1)
		modes_per_line = serv->modes_per_line;

	/* RFC max, minus length of "MODE %s " and "\r\n" and 1 +/- sign */
	/* 512 - 6 - 2 - 1 - strlen(chan) */
	max = 503 - strlen (sess->channel);

	while (wpos < end)
	{
		tbuf[0] = '\0';
		orig_len = len = 0;

		/* we'll need this many modechars too */
		len += modes_per_line;

		/* how many can we fit? */
		for (i = 0; i < modes_per_line; i++)
		{
			/* no more nicks left? */
			if (wpos + i >= end)
				break;
			wlen = strlen (word[wpos + i]) + 1;
			if (wlen + len > max)
				break;
			len += wlen; /* length of our whole string so far */
		}
		if (i < 1)
			return;
		usable_modes = i;	/* this is how many we'll send on this line */

		/* add the +/-modemodemodemode */
		len = orig_len;
		tbuf[len] = sign;
		len++;
		for (i = 0; i < usable_modes; i++)
		{
			tbuf[len] = mode;
			len++;
		}
		tbuf[len] = 0;	/* null terminate for the strcat() to work */

		/* add all the nicknames */
		for (i = 0; i < usable_modes; i++)
		{
			strcat (tbuf, " ");
			strcat (tbuf, word[wpos + i]);
		}
		serv->p_mode (serv, sess->channel, tbuf);

		wpos += usable_modes;
	}
}

/* does 'chan' have a valid prefix? e.g. # or & */

int
is_channel (server * serv, char *chan)
{
	if (strchr (serv->chantypes, chan[0]))
		return 1;
	return 0;
}

/* is the given char a valid nick mode char? e.g. @ or + */

static int
is_prefix_char (server * serv, char c)
{
	int pos = 0;
	char *np = serv->nick_prefixes;

	while (np[0])
	{
		if (np[0] == c)
			return pos;
		pos++;
		np++;
	}

	if (serv->bad_prefix)
	{
		if (strchr (serv->bad_nick_prefixes, c))
		/* valid prefix char, but mode unknown */
			return -2;
	}

	return -1;
}

/* returns '@' for ops etc... */

char
get_nick_prefix (server * serv, unsigned int access)
{
	int pos;
	char c;

	for (pos = 0; pos < USERACCESS_SIZE; pos++)
	{
		c = serv->nick_prefixes[pos];
		if (c == 0)
			break;
		if (access & (1 << pos))
			return c;
	}

	return 0;
}

/* returns the access bitfield for a nickname. E.g.
	@nick would return 000010 in binary
	%nick would return 000100 in binary
	+nick would return 001000 in binary */

unsigned int
nick_access (server * serv, char *nick, int *modechars)
{
	int i;
	unsigned int access = 0;
	char *orig = nick;

	while (*nick)
	{
		i = is_prefix_char (serv, *nick);
		if (i == -1)
			break;

		/* -2 == valid prefix char, but mode unknown */
		if (i != -2)
			access |= (1 << i);

		nick++;
	}

	*modechars = nick - orig;

	return access;
}

/* returns the access number for a particular mode. e.g.
	mode 'a' returns 0
	mode 'o' returns 1
	mode 'h' returns 2
	mode 'v' returns 3
	Also puts the nick-prefix-char in 'prefix' */

int
mode_access (server * serv, char mode, char *prefix)
{
	int pos = 0;

	while (serv->nick_modes[pos])
	{
		if (serv->nick_modes[pos] == mode)
		{
			*prefix = serv->nick_prefixes[pos];
			return pos;
		}
		pos++;
	}

	*prefix = 0;

	return -1;
}

static int
mode_timeout_cb (session *sess)
{
	if (is_session (sess))
	{
		sess->mode_timeout_tag = 0;
		sess->server->p_join_info (sess->server, sess->channel);
		sess->ignore_mode = TRUE;
		sess->ignore_date = TRUE;
	}
	return 0;
}

static void
record_chan_mode (session *sess)/*, char sign, char mode, char *arg)*/
{
	/* Should we write a routine to add sign,mode to sess->current_modes?
		nah! too hard. Lets just issue a MODE #channel and read it back.
		We need to find out the new modes for the titlebar, but let's not
		flood ourselves off when someone decides to change 100 modes/min. */
	if (!sess->mode_timeout_tag)
		sess->mode_timeout_tag = g_timeout_add (15000, (GSourceFunc) mode_timeout_cb, sess);
}

/* handle one mode, e.g.
   handle_single_mode (mr,'+','b',"elite","#warez","banneduser",) */

static void
handle_single_mode (mode_run *mr, char sign, char mode, char *nick,
						  char *chan, char *arg, int quiet, int is_324)
{
	session *sess;
	server *serv = mr->serv;
	char outbuf[4];

	outbuf[0] = sign;
	outbuf[1] = 0;
	outbuf[2] = mode;
	outbuf[3] = 0;

	sess = find_channel (serv, chan);
	if (!sess || !is_channel (serv, chan))
	{
		/* got modes for a chan we're not in! probably nickmode +isw etc */
		sess = serv->front_session;
		goto genmode;
	}

	/* is this a nick mode? */
	if (strchr (serv->nick_modes, mode))
	{
		/* update the user in the userlist */
		userlist_update_mode (sess, /*nickname */ arg, mode, sign);
	} else
	{
		if (!is_324 && !sess->ignore_mode)
			record_chan_mode (sess);/*, sign, mode, arg);*/
	}

	switch (sign)
	{
	case '+':
		switch (mode)
		{
		case 'k':
			g_strlcpy (sess->channelkey, arg, sizeof (sess->channelkey));
			fe_update_channel_key (sess);
			fe_update_mode_buttons (sess, mode, sign);
			return;
		case 'l':
			sess->limit = atoi (arg);
			fe_update_channel_limit (sess);
			fe_update_mode_buttons (sess, mode, sign);
			return;
		case 'o':
		case 'h':
		case 'v':
		case 'b':
		case 'e':
		case 'I':
			return;
		}
		break;
	case '-':
		switch (mode)
		{
		case 'k':
			sess->channelkey[0] = 0;
			fe_update_channel_key (sess);
			fe_update_mode_buttons (sess, mode, sign);
			return;
		case 'l':
			sess->limit = 0;
			fe_update_channel_limit (sess);
			fe_update_mode_buttons (sess, mode, sign);
			return;
		case 'o':
		case 'h':
		case 'v':
		case 'b':
		case 'e':
		case 'I':
			return;
		}
	}

	fe_update_mode_buttons (sess, mode, sign);

 genmode:
	/* Received umode +e. If we're waiting to send JOIN then send now! */
	if (mode == 'e' && sign == '+' && !serv->p_cmp (chan, serv->nick))
		inbound_identified (serv);
}

/* does this mode have an arg? like +b +l +o */

static int
mode_has_arg (server * serv, char sign, char mode)
{
	char *cm;
	int type;

	/* if it's a nickmode, it must have an arg */
	if (strchr (serv->nick_modes, mode))
		return 1;

	/* see what numeric 005 CHANMODES=xxx said */
	cm = serv->chanmodes;
	type = 0;
	while (*cm)
	{
		if (*cm == ',')
		{
			type++;
		} else if (*cm == mode)
		{
			switch (type)
			{
			case 0:					  /* type A */
			case 1:					  /* type B */
				return 1;
			case 2:					  /* type C */
				if (sign == '+')
					return 1;
			case 3:					  /* type D */
				return 0;
			}
		}
		cm++;
	}

	return 0;
}

/* handle a MODE or numeric 324 from server */

void
handle_mode (server * serv, char *word[], char *word_eol[],
				 char *nick, int numeric_324)
{
	session *sess;
	char *chan;
	char *modes;
	char *argstr;
	char sign;
	int len;
	int arg;
	int i, num_args;
	int num_modes;
	int offset = 3;
	int all_modes_have_args = FALSE;
	int using_front_tab = FALSE;
	gboolean fwd = FALSE;
	mode_run mr;

	mr.serv = serv;
	mr.op = mr.deop = mr.voice = mr.devoice = NULL;

	/* numeric 324 has everything 1 word later (as opposed to MODE) */
	if (numeric_324)
		offset++;

	chan = word[offset];
	modes = word[offset + 1];
	if (*modes == ':')
		modes++;

	if (*modes == 0)
		return;	/* beyondirc's blank modes */

	sess = find_channel (serv, chan);
	if (!sess)
	{
		sess = serv->front_session;
		using_front_tab = TRUE;
	}
	/* remove trailing space */
	len = strlen (word_eol[offset]) - 1;
	if (word_eol[offset][len] == ' ')
		word_eol[offset][len] = 0;

	if (*word_eol[offset + 1] == ':')
	{
		fwd = TRUE;
		word_eol[offset + 1]++;
	}

	if (!numeric_324)
		signal_emit("channel modes raw", 3, sess, nick, word_eol[offset + 1]);

	if (fwd)
		word_eol[offset + 1]--;

	if (numeric_324 && !using_front_tab)
	{
		if (sess->current_modes)
			free (sess->current_modes);
		sess->current_modes = strdup (*word_eol[offset+1] != ' ' ? word_eol[offset + 1] : (word_eol[offset + 1] + 1));
		fe_set_title (sess);
	}

	sign = *modes;
	modes++;
	arg = 1;

	/* count the number of arguments (e.g. after the -o+v) */
	num_args = 0;
	i = 1;
	while (i < PDIWORDS)
	{
		i++;
		if (!(*word[i + offset]))
			break;
		num_args++;
	}

	/* count the number of modes (without the -/+ chars */
	num_modes = 0;
	i = 0;
	while (i < strlen (modes))
	{
		if (modes[i] != '+' && modes[i] != '-')
			num_modes++;
		i++;
	}

	if (num_args == num_modes)
		all_modes_have_args = TRUE;

	while (*modes)
	{
		switch (*modes)
		{
		case '-':
		case '+':
			sign = *modes;
			break;
		default:
			argstr = "";
			if ((all_modes_have_args || mode_has_arg (serv, sign, *modes)) && arg < (num_args+1))
			{
				arg++;
				argstr = word[arg + offset];
			}
			handle_single_mode (&mr, sign, *modes, nick, chan,
									  argstr, numeric_324,
									  numeric_324);
		}

		modes++;
	}
}

/* handle the 005 numeric */

void
inbound_005 (server * serv, char *word[])
{
	int w;
	char *pre;

	w = 4;							  /* start at the 4th word */
	while (w < PDIWORDS && *word[w])
	{
		if (strncmp (word[w], "MODES=", 6) == 0)
		{
			serv->modes_per_line = atoi (word[w] + 6);
		} else if (strncmp (word[w], "CHANTYPES=", 10) == 0)
		{
			free (serv->chantypes);
			serv->chantypes = strdup (word[w] + 10);
		} else if (strncmp (word[w], "CHANMODES=", 10) == 0)
		{
			free (serv->chanmodes);
			serv->chanmodes = strdup (word[w] + 10);
		} else if (strncmp (word[w], "PREFIX=", 7) == 0)
		{
			pre = strchr (word[w] + 7, ')');
			if (pre)
			{
				pre[0] = 0;			  /* NULL out the ')' */
				free (serv->nick_prefixes);
				free (serv->nick_modes);
				serv->nick_prefixes = strdup (pre + 1);
				serv->nick_modes = strdup (word[w] + 8);
			} else
			{
				/* bad! some ircds don't give us the modes. */
				/* in this case, we use it only to strip /NAMES */
				serv->bad_prefix = TRUE;
				if (serv->bad_nick_prefixes)
					free (serv->bad_nick_prefixes);
				serv->bad_nick_prefixes = strdup (word[w] + 7);
			}
		} else if (strncmp (word[w], "WATCH=", 6) == 0)
		{
			serv->supports_watch = TRUE;
		} else if (strncmp (word[w], "MONITOR=", 8) == 0)
		{
			serv->supports_monitor = TRUE;
		} else if (strncmp (word[w], "NETWORK=", 8) == 0)
		{
			if (serv->server_session->type == SESS_SERVER)
			{
				g_strlcpy (serv->server_session->channel, word[w] + 8, CHANLEN);
				fe_set_channel (serv->server_session);
			}

			/* use /NICKSERV */
			if ((!strcasecmp(word[w] + 8, "RusNet")) ||
                                (!strcasecmp(word[w] + 8, "DALnet")) ||
                                (!strcasecmp(word[w] + 8, "BRASnet")))
				serv->nickservtype = 1;
                        else if (!strcasecmp(word[w] + 8, "FreeNode"))
                            serv->nickservtype = 2;
			else if (strcasecmp (word[w] + 8, "UniBG") == 0)
				serv->nickservtype = 3;
		} else if (strncmp (word[w], "CASEMAPPING=", 12) == 0)
		{
			if (strcmp (word[w] + 12, "ascii") == 0)	/* bahamut */
				serv->p_cmp = (void *)strcasecmp;
		} else if (strncmp (word[w], "CHARSET=", 8) == 0)
		{
			if (strcasecmp (word[w] + 8, "UTF-8") == 0)
			{
				server_set_encoding (serv, "UTF-8");
			}
		} else if (strcmp (word[w], "WHOX") == 0)
		{
			serv->have_whox = TRUE;
		} else if (strcmp (word[w], "EXCEPTS") == 0)
		{
			serv->have_except = TRUE;
		} else if (strncmp (word[w], "ELIST=", 6) == 0)
		{
			/* supports LIST >< min/max user counts? */
			if (strchr (word[w] + 6, 'U') || strchr (word[w] + 6, 'u'))
				serv->use_listargs = TRUE;
		}

		w++;
	}
}
