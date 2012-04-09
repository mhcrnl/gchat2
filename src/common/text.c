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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "stdinc.h"
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "xchat.h"
#if 0
#include <glib/ghash.h>
#endif
#include "cfgfiles.h"
#include "fe.h"
#include "server.h"
#include "util.h"
#include "outbound.h"
#include "xchatc.h"
#include "text.h"

static void mkdir_p (char *dir);


static char *
scrollback_get_filename (session *sess, char *buf, int max)
{
	char *net;

	net = server_get_network (sess->server, FALSE);
	if (!net)
		return NULL;

	snprintf (buf, max, "%s/scrollback/%s/%s.txt", get_xdir_fs (), net, sess->channel);
	mkdir_p (buf);

	return buf;
}

#if 0

static void
scrollback_unlock (session *sess)
{
	char buf[1024];

	if (scrollback_get_filename (sess, buf, sizeof (buf) - 6) == NULL)
		return;

	strcat (buf, ".lock");
	unlink (buf);
}

static gboolean
scrollback_lock (session *sess)
{
	char buf[1024];
	int fh;

	if (scrollback_get_filename (sess, buf, sizeof (buf) - 6) == NULL)
		return FALSE;

	strcat (buf, ".lock");

	if (access (buf, F_OK) == 0)
		return FALSE;	/* can't get lock */

	fh = open (buf, O_CREAT | O_TRUNC | O_APPEND | O_WRONLY, 0644);
	if (fh == -1)
		return FALSE;

	return TRUE;
}

#endif

void
scrollback_close (session *sess)
{
	if (sess->scrollfd != -1)
	{
		close (sess->scrollfd);
		sess->scrollfd = -1;
	}
}

static char *
file_to_buffer (char *file, int *len)
{
	int fh;
	char *buf;
	struct stat st;

	fh = open (file, O_RDONLY | OFLAGS);
	if (fh == -1)
		return NULL;

	fstat (fh, &st);

	buf = malloc (st.st_size);
	if (!buf)
	{
		close (fh);
		return NULL;
	}

	if (read (fh, buf, st.st_size) != st.st_size)
	{
		free (buf);
		close (fh);
		return NULL;
	}

	*len = st.st_size;
	close(fh);

	return buf;
}

/* shrink the file to roughly prefs.max_lines */

static void
scrollback_shrink (session *sess)
{
	char file[1024];
	char *buf;
	int fh;
	int lines;
	int line;
	int len;
	char *p;

	scrollback_close (sess);
	sess->scrollwritten = 0;
	lines = 0;

	if (scrollback_get_filename (sess, file, sizeof (file)) == NULL)
		return;

	buf = file_to_buffer (file, &len);
	if (!buf)
		return;

	/* count all lines */
	p = buf;
	while (p != buf + len)
	{
		if (*p == '\n')
			lines++;
		p++;
	}

	fh = open (file, O_CREAT | O_TRUNC | O_APPEND | O_WRONLY, 0644);
	if (fh == -1)
	{
		free (buf);
		return;
	}

	line = 0;
	p = buf;
	while (p != buf + len)
	{
		if (*p == '\n')
		{
			line++;
			if (line >= lines - prefs.max_lines &&
				 p + 1 != buf + len)
			{
				p++;
				write (fh, p, len - (p - buf));
				break;
			}
		}
		p++;
	}

	close (fh);
	free (buf);
}

static void
scrollback_save (session *sess, char *text)
{
	char buf[1024];
	time_t stamp;
	int len;

	if (sess->type == SESS_SERVER)
		return;

	if (sess->scrollfd == -1)
	{
		if (scrollback_get_filename (sess, buf, sizeof (buf)) == NULL)
			return;

		sess->scrollfd = open (buf, O_CREAT | O_APPEND | O_WRONLY, 0644);
		if (sess->scrollfd == -1)
			return;
	}

	stamp = time (0);
	if (sizeof (stamp) == 4)	/* gcc will optimize one of these out */
		write (sess->scrollfd, buf, snprintf (buf, sizeof (buf), "T %d ", (int)stamp));
	else
		write (sess->scrollfd, buf, snprintf (buf, sizeof (buf), "T %"G_GINT64_FORMAT" ", (gint64)stamp));

	len = strlen (text);
	write (sess->scrollfd, text, len);
	if (len && text[len - 1] != '\n')
		write (sess->scrollfd, "\n", 1);

	sess->scrollwritten++;

	if ((sess->scrollwritten * 2 > prefs.max_lines && prefs.max_lines > 0) ||
       sess->scrollwritten > 32000)
		scrollback_shrink (sess);
}

void
scrollback_load (session *sess)
{
	int fh;
	char buf[1024];
	char *text;
	time_t stamp;
	int lines;
	char *map, *end_map;
	struct stat statbuf;
	const char *begin, *eol;

	if (scrollback_get_filename (sess, buf, sizeof (buf)) == NULL)
		return;

	fh = open (buf, O_RDONLY | OFLAGS);
	if (fh == -1)
		return;

	if (fstat (fh, &statbuf) < 0)
		return;

	map = mmap (NULL, statbuf.st_size, PROT_READ, MAP_PRIVATE, fh, 0);
	if (map == MAP_FAILED)
		return;

	end_map = map + statbuf.st_size;
	
	lines = 0;
	begin = map;
	while (begin < end_map)
	{
		int n_bytes;

		eol = memchr (begin, '\n', end_map - begin);

		if (!eol)
			eol = end_map;

		n_bytes = MIN (eol - begin, sizeof (buf) - 1);
		
		strncpy (buf, begin, n_bytes);

		buf[n_bytes] = 0;
		
		if (buf[0] == 'T')
		{
			if (sizeof (time_t) == 4)
				stamp = strtoul (buf + 2, NULL, 10);
			else
				stamp = BIG_STR_TO_INT(buf + 2); /* just incase time_t is 64 bits */
			text = strchr (buf + 3, ' ');
			if (text)
				fe_print_text (sess, text, stamp);
			lines++;
		}

		begin = eol + 1;
	}

	if (lines)
	{
		text = ctime (&stamp);
		text[24] = 0;	/* get rid of the \n */
		snprintf (buf, sizeof (buf), "\n*\t%s %s\n\n", _("Loaded log from"), text);
		fe_print_text (sess, buf, 0);
		/*EMIT_SIGNAL (XP_TE_GENMSG, sess, "*", buf, NULL, NULL, NULL, 0);*/
	}

	munmap (map, statbuf.st_size);
	close (fh);
}

void
log_close (session *sess)
{
	char obuf[512];
	time_t currenttime;

	if (sess->logfd != -1)
	{
		currenttime = time (NULL);
		write (sess->logfd, obuf,
			 snprintf (obuf, sizeof (obuf) - 1, _("**** ENDING LOGGING AT %s\n"),
						  ctime (&currenttime)));
		close (sess->logfd);
		sess->logfd = -1;
	}
}

static void
mkdir_p (char *dir)	/* like "mkdir -p" from a shell, FS encoding */
{
	char *start = dir;

	/* the whole thing already exists? */
	if (access (dir, F_OK) == 0)
		return;

	while (*dir)
	{
		if (dir != start && *dir == '/')
		{
			*dir = 0;
			mkdir (start, S_IRUSR | S_IWUSR | S_IXUSR);
			*dir = '/';
		}
		dir++;
	}
}

static char *
log_create_filename (char *channame)
{
	char *tmp, *ret;
	int mbl;

	ret = tmp = strdup (channame);
	while (*tmp)
	{
		mbl = g_utf8_skip[((unsigned char *)tmp)[0]];
		if (mbl == 1)
		{
			*tmp = rfc_tolower (*tmp);
			if (*tmp == '/')
				*tmp = '_';
		}
		tmp += mbl;
	}

	return ret;
}

/* like strcpy, but % turns into %% */

static char *
log_escape_strcpy (char *dest, char *src, char *end)
{
	while (*src)
	{
		*dest = *src;
		if (dest + 1 == end)
			break;
		dest++;
		src++;

		if (*src == '%')
		{
			if (dest + 1 == end)
				break;
			dest[0] = '%';
			dest++;
		}
	}

	dest[0] = 0;
	return dest - 1;
}

/* substitutes %c %n %s into buffer */

static void
log_insert_vars (char *buf, int bufsize, char *fmt, char *c, char *n, char *s)
{
	char *end = buf + bufsize;

	while (1)
	{
		switch (fmt[0])
		{
		case 0:
			buf[0] = 0;
			return;

		case '%':
			fmt++;
			switch (fmt[0])
			{
			case 'c':
				buf = log_escape_strcpy (buf, c, end);
				break;
			case 'n':
				buf = log_escape_strcpy (buf, n, end);
				break;
			case 's':
				buf = log_escape_strcpy (buf, s, end);
				break;
			default:
				buf[0] = '%';
				buf++;
				buf[0] = fmt[0];
				break;
			}
			break;

		default:
			buf[0] = fmt[0];
		}
		fmt++;
		buf++;
		/* doesn't fit? */
		if (buf == end)
		{
			buf[-1] = 0;
			return;
		}
	}
}

static char *
log_create_pathname (char *servname, char *channame, char *netname)
{
	char fname[384];
	char fnametime[384];
	char *fs;
	struct tm *tm;
	time_t now;

	if (!netname)
		netname = "NETWORK";

	/* first, everything is in UTF-8 */
	if (!rfc_casecmp (channame, servname))
		channame = strdup ("server");
	else
		channame = log_create_filename (channame);
	log_insert_vars (fname, sizeof (fname), prefs.logmask, channame, netname, servname);
	free (channame);

	/* insert time/date */
	now = time (NULL);
	tm = localtime (&now);
	strftime (fnametime, sizeof (fnametime), fname, tm);

	/* create final path/filename */
	if (fnametime[0] == '/')	/* is it fullpath already? */
		snprintf (fname, sizeof (fname), "%s", fnametime);
	else
		snprintf (fname, sizeof (fname), "%s/xchatlogs/%s", get_xdir_utf8 (), fnametime);

	/* now we need it in FileSystem encoding */
	fs = xchat_filename_from_utf8 (fname, -1, 0, 0, 0);

	/* create all the subdirectories */
	if (fs)
		mkdir_p (fs);

	return fs;
}

static int
log_open_file (char *servname, char *channame, char *netname)
{
	char buf[512];
	int fd;
	char *file;
	time_t currenttime;

	file = log_create_pathname (servname, channame, netname);
	if (!file)
		return -1;

	fd = open (file, O_CREAT | O_APPEND | O_WRONLY, 0644);
	g_free (file);

	if (fd == -1)
		return -1;
	currenttime = time (NULL);
	write (fd, buf,
			 snprintf (buf, sizeof (buf), _("**** BEGIN LOGGING AT %s\n"),
						  ctime (&currenttime)));

	return fd;
}

void
log_open (session *sess)
{
	static gboolean log_error = FALSE;

	log_close (sess);
	sess->logfd = log_open_file (sess->server->servername, sess->channel,
										  server_get_network (sess->server, FALSE));

	if (!log_error && sess->logfd == -1)
	{
		char message[512];
		snprintf (message, sizeof (message),
					_("* Can't open log file(s) for writing. Check the\n" \
					  "  permissions on %s/xchatlogs"), get_xdir_utf8 ());
		fe_message (message, FE_MSG_WAIT | FE_MSG_ERROR);

		log_error = TRUE;
	}
}

int
get_stamp_str (char *fmt, time_t tim, char **ret)
{
	char *loc = NULL;
	char dest[128];
	gsize len;

	/* strftime wants the format string in LOCALE! */
	if (!prefs.utf8_locale)
	{
		const gchar *charset;

		g_get_charset (&charset);
		loc = g_convert_with_fallback (fmt, -1, charset, "UTF-8", "?", 0, 0, 0);
		if (loc)
			fmt = loc;
	}

	len = strftime (dest, sizeof (dest), fmt, localtime (&tim));
	if (len)
	{
		if (prefs.utf8_locale)
			*ret = g_strdup (dest);
		else
			*ret = g_locale_to_utf8 (dest, len, 0, &len, 0);
	}

	if (loc)
		g_free (loc);

	return len;
}

static void
log_write (session *sess, char *text)
{
	char *temp;
	char *stamp;
	char *file;
	int len;

	if (sess->logfd != -1 && prefs.logging)
	{
		/* change to a different log file? */
		file = log_create_pathname (sess->server->servername, sess->channel,
											 server_get_network (sess->server, FALSE));
		if (file)
		{
			if (access (file, F_OK) != 0)
			{
				close (sess->logfd);
				sess->logfd = log_open_file (sess->server->servername, sess->channel,
													  server_get_network (sess->server, FALSE));
			}
			g_free (file);
		}

		if (prefs.timestamp_logs)
		{
			len = get_stamp_str (prefs.timestamp_log_format, time (0), &stamp);
			if (len)
			{
				write (sess->logfd, stamp, len);
				g_free (stamp);
			}
		}
		temp = strip_color (text, -1, STRIP_ALL);
		len = strlen (temp);
		write (sess->logfd, temp, len);
		/* lots of scripts/plugins print without a \n at the end */
		if (temp[len - 1] != '\n')
			write (sess->logfd, "\n", 1);	/* emulate what xtext would display */
		free (temp);
	}
}

/* converts a CP1252/ISO-8859-1(5) hybrid to UTF-8                           */
/* Features: 1. It never fails, all 00-FF chars are converted to valid UTF-8 */
/*           2. Uses CP1252 in the range 80-9f because ISO doesn't have any- */
/*              thing useful in this range and it helps us receive from mIRC */
/*           3. The five undefined chars in CP1252 80-9f are replaced with   */
/*              ISO-8859-15 control codes.                                   */
/*           4. Handles 0xa4 as a Euro symbol ala ISO-8859-15.               */
/*           5. Uses ISO-8859-1 (which matches CP1252) for everything else.  */
/*           6. This routine measured 3x faster than g_convert :)            */

static unsigned char *
iso_8859_1_to_utf8 (unsigned char *text, int len, gsize *bytes_written)
{
	unsigned int idx;
	unsigned char *res, *output;
	static const unsigned short lowtable[] = /* 74 byte table for 80-a4 */
	{
	/* compressed utf-8 table: if the first byte's 0x20 bit is set, it
	   indicates a 2-byte utf-8 sequence, otherwise prepend a 0xe2. */
		0x82ac, /* 80 Euro. CP1252 from here on... */
		0xe281, /* 81 NA */
		0x809a, /* 82 */
		0xe692, /* 83 */
		0x809e, /* 84 */
		0x80a6, /* 85 */
		0x80a0, /* 86 */
		0x80a1, /* 87 */
		0xeb86, /* 88 */
		0x80b0, /* 89 */
		0xe5a0, /* 8a */
		0x80b9, /* 8b */
		0xe592, /* 8c */
		0xe28d, /* 8d NA */
		0xe5bd, /* 8e */
		0xe28f, /* 8f NA */
		0xe290, /* 90 NA */
		0x8098, /* 91 */
		0x8099, /* 92 */
		0x809c, /* 93 */
		0x809d, /* 94 */
		0x80a2, /* 95 */
		0x8093, /* 96 */
		0x8094, /* 97 */
		0xeb9c, /* 98 */
		0x84a2, /* 99 */
		0xe5a1, /* 9a */
		0x80ba, /* 9b */
		0xe593, /* 9c */
		0xe29d, /* 9d NA */
		0xe5be, /* 9e */
		0xe5b8, /* 9f */
		0xe2a0, /* a0 */
		0xe2a1, /* a1 */
		0xe2a2, /* a2 */
		0xe2a3, /* a3 */
		0x82ac  /* a4 ISO-8859-15 Euro. */
	};

	if (len == -1)
		len = strlen (text);

	/* worst case scenario: every byte turns into 3 bytes */
	res = output = g_malloc ((len * 3) + 1);
	if (!output)
		return NULL;

	while (len)
	{
		if (G_LIKELY (*text < 0x80))
		{
			*output = *text;	/* ascii maps directly */
		}
		else if (*text <= 0xa4)	/* 80-a4 use a lookup table */
		{
			idx = *text - 0x80;
			if (lowtable[idx] & 0x2000)
			{
				*output++ = (lowtable[idx] >> 8) & 0xdf; /* 2 byte utf-8 */
				*output = lowtable[idx] & 0xff;
			}
			else
			{
				*output++ = 0xe2;	/* 3 byte utf-8 */
				*output++ = (lowtable[idx] >> 8) & 0xff;
				*output = lowtable[idx] & 0xff;
			}
		}
		else if (*text < 0xc0)
		{
			*output++ = 0xc2;
			*output = *text;
		}
		else
		{
			*output++ = 0xc3;
			*output = *text - 0x40;
		}
		output++;
		text++;
		len--;
	}
	*output = 0;	/* terminate */
	*bytes_written = output - res;

	return res;
}

char *
text_validate (char **text, int *len)
{
	char *utf;
	gsize utf_len;

	/* valid utf8? */
	if (g_utf8_validate (*text, *len, 0))
		return NULL;

	if (prefs.utf8_locale)
		/* fallback to iso-8859-1 */
		utf = iso_8859_1_to_utf8 (*text, *len, &utf_len);
	else
	{
		/* fallback to locale */
		utf = g_locale_to_utf8 (*text, *len, 0, &utf_len, NULL);
		if (!utf)
			utf = iso_8859_1_to_utf8 (*text, *len, &utf_len);
	}

	if (!utf)
	{
		*text = g_strdup ("%INVALID%");
		*len = 9;
	} else
	{
		*text = utf;
		*len = utf_len;
	}

	return utf;
}

void
PrintText (session *sess, char *text)
{
	char *conv;

	if (!sess)
	{
		if (!sess_list)
			return;
		sess = (session *) sess_list->data;
	}

	/* make sure it's valid utf8 */
	if (text[0] == 0)
	{
		text = "\n";
		conv = NULL;
	} else
	{
		int len = -1;
		conv = text_validate ((char **)&text, &len);
	}

	log_write (sess, text);
	if (prefs.text_replay)
		scrollback_save (sess, text);
	fe_print_text (sess, text, 0);

	if (conv)
		g_free (conv);
}

void
PrintTextf (session *sess, char *format, ...)
{
	va_list args;
	char *buf;

	va_start (args, format);
	buf = g_strdup_vprintf (format, args);
	va_end (args);

	PrintText (sess, buf);
	g_free (buf);
}

#include "textevents.h"

static void
pevent_load_defaults ()
{
	gint i;

	for (i = 0; i < G_N_ELEMENTS(te); i++)
		formatter_register(te[i].name, te[i].def, te[i].num_args);
}

/* Loading happens at 2 levels:
   1) File is read into blocks
   2) Pe block is parsed and loaded

   --AGL */

int
pevent_load (char *filename)
{
	/* AGL, I've changed this file and pevent_save, could you please take a look at
	 *      the changes and possibly modify them to suit you
	 *      //David H
	 */
	char *buf, *ibuf;
	int fd, pnt = 0;
	struct stat st;
	char *ofs;
	Formatter *f = NULL;

	if (filename == NULL)
		fd = xchat_open_file ("pevents.conf", O_RDONLY, 0, 0);
	else
		fd = xchat_open_file (filename, O_RDONLY, 0, XOF_FULLPATH);

	if (fd == -1)
		return 1;
	if (fstat (fd, &st) != 0)
		return 1;
	ibuf = malloc (st.st_size);
	read (fd, ibuf, st.st_size);
	close (fd);

	while (buf_get_line (ibuf, &buf, &pnt, st.st_size))
	{
		if (buf[0] == '#')
			continue;
		if (strlen (buf) == 0)
			continue;

		ofs = strchr (buf, '=');
		if (!ofs)
			continue;
		*ofs = 0;
		ofs++;
		/*if (*ofs == 0)
			continue;*/

		if (strcmp (buf, "event_name") == 0)
		{
			f = formatter_get(ofs);
			continue;
		}
		else if (strcmp (buf, "event_text") == 0)
		{
			if (f == NULL)
				continue;

			if (f->format)
				g_free(f->format);

			f->format = g_strdup(ofs);

			continue;
		}

		continue;
	}

	free (ibuf);
	return 0;
}

void
load_text_events ()
{
	pevent_load_defaults();
	pevent_load(NULL);
}

/* black n white(0/1) are bad colors for nicks, and we'll use color 2 for us */
/* also light/dark gray (14/15) */
/* 5,7,8 are all shades of yellow which happen to look dman near the same */

static char rcolors[] = { 2, 3, 4, 5, 6, 7, 10, 12, 13, 18, 19, 20, 21, 22, 23, 26, 27, 28, 29 };

int
color_of (char *name)
{
	int i = 0, sum = 0;

	while (name[i])
		sum += name[i++];
	sum %= sizeof (rcolors) / sizeof (char);
	return rcolors[sum];
}

/* called by EMIT_SIGNAL macro */
void
pevent_save (char *fn)
{
	int fd;
	char buf[1024];
        int i = 0;

	if (!fn)
		fd = xchat_open_file ("pevents.conf", O_CREAT | O_TRUNC | O_WRONLY,
									 0x180, XOF_DOMODE);
	else
		fd = xchat_open_file (fn, O_CREAT | O_TRUNC | O_WRONLY, 0x180,
									 XOF_FULLPATH | XOF_DOMODE);
	if (fd == -1)
	{
		/*
		   fe_message ("Error opening config file\n", FALSE);
		   If we get here when X-Chat is closing the fe-message causes a nice & hard crash
		   so we have to use perror which doesn't rely on GTK
		 */

		perror ("Error opening config file\n");
		return;
	}

        while (te[i].name != NULL)
	{
		write (fd, buf, snprintf (buf, sizeof (buf), "event_name=%s\n", te[i].name));
		write (fd, buf, snprintf (buf, sizeof (buf), "event_text=%s\n\n", te[i].def));
                i++;
	}

	close (fd);
}

/* =========================== */
/* ========== SOUND ========== */
/* =========================== */

void
sound_beep (session *sess)
{
	fe_beep ();
}

