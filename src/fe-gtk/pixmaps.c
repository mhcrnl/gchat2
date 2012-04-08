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

#include "fe-gtk.h"
#include "../common/xchat.h"
#include "../common/fe.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk-pixbuf/gdk-pixdata.h>
#include <gtk/gtk.h>

GdkPixbuf *pix_128;
GdkPixbuf *pix_48;
GdkPixbuf *pix_32;
GdkPixbuf *pix_book;
GdkPixbuf *pix_hilite;

GdkPixbuf *pix_purple;
GdkPixbuf *pix_red;
GdkPixbuf *pix_op;
GdkPixbuf *pix_hop;
GdkPixbuf *pix_voice;

GdkPixbuf *pix_tray_blank;
GdkPixbuf *pix_tray_file;

static GdkPixmap *
pixmap_load_from_file_real (char *file)
{
	GdkPixbuf *img;
	GdkPixmap *pixmap;

	img = gdk_pixbuf_new_from_file (file, 0);
	if (!img)
		return NULL;
	gdk_pixbuf_render_pixmap_and_mask (img, &pixmap, NULL, 128);
	gdk_pixbuf_unref (img);

	return pixmap;
}

GdkPixmap *
pixmap_load_from_file (char *filename)
{
	char buf[256];
	GdkPixmap *pix;

	if (!filename)
		return NULL;

	pix = pixmap_load_from_file_real (filename);
	if (pix == NULL)
	{
		strcpy (buf, "Cannot open:\n\n");
		strncpy (buf + 14, filename, sizeof (buf) - 14);
		buf[sizeof (buf) - 1] = 0;
		fe_message (buf, FE_MSG_ERROR);
	}

	return pix;
}

void
pixmaps_init (void)
{
	pix_book = gdk_pixbuf_new_from_file (CONSPIRE_SHAREDIR "/conspire/pixmaps/book.png", NULL);
    pix_hilite = gdk_pixbuf_new_from_file (CONSPIRE_SHAREDIR "/conspire/pixmaps/32.png", NULL);
	pix_128 = gdk_pixbuf_new_from_file (CONSPIRE_SHAREDIR "/conspire/pixmaps/128.png", NULL);
	pix_48 = gdk_pixbuf_new_from_file (CONSPIRE_SHAREDIR "/conspire/pixmaps/48.png", NULL);
	pix_32 = gdk_pixbuf_new_from_file (CONSPIRE_SHAREDIR "/conspire/pixmaps/32.png", NULL);

	pix_hop = gdk_pixbuf_new_from_file (CONSPIRE_SHAREDIR "/conspire/pixmaps/status/hop.png", NULL);
	pix_op = gdk_pixbuf_new_from_file (CONSPIRE_SHAREDIR "/conspire/pixmaps/status/op.png", NULL);
	pix_purple = gdk_pixbuf_new_from_file (CONSPIRE_SHAREDIR "/conspire/pixmaps/status/purple.png", NULL);
	pix_red = gdk_pixbuf_new_from_file (CONSPIRE_SHAREDIR "/conspire/pixmaps/status/red.png", NULL);
	pix_voice = gdk_pixbuf_new_from_file (CONSPIRE_SHAREDIR "/conspire/pixmaps/status/voice.png", NULL);
}
