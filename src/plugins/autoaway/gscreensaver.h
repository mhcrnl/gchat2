/* gscreensaver.h
 *
 * Copyright (C) 2005 Isak Savo <isak.savo@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */
#ifndef GSCREENSAVER_H
#define GSCREENSAVER_H
#include <glib.h>

gboolean get_gs_has_ipc (void);
void init_gs_connection (void);
void close_gs_connection (void);
gboolean get_gs_screensaver_active (void);


#endif

