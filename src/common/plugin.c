/* Conspire
 * Copyright (C) 2008 William Pitcock
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

#include "plugin.h"
#include "xchat.h"
#include "fe.h"
#include "util.h"

mowgli_dictionary_t *plugin_dict = NULL;

void
plugin_load(const gchar *filename)
{
	Plugin *p;
	GModule *m;
	PluginHeader *(*acquire_func)(void) = NULL;
	gpointer acquire_sym = NULL;

	if (plugin_dict == NULL)
		plugin_dict = mowgli_dictionary_create(g_ascii_strcasecmp);

	m = g_module_open(filename, G_MODULE_BIND_LOCAL);
	if (m == NULL) {
		signal_emit("plugin error", 2, filename, g_module_error());
		return;
	}

	if (!g_module_symbol(m, "conspire_get_plugin", &acquire_sym))
		return;

	/* avoid casting error. --nenolod */
	acquire_func = acquire_sym;

	p = g_slice_new0(Plugin);
	p->handle = m;
	p->header = acquire_func();

	if (p->header->init)
		p->header->init(p);

	mowgli_dictionary_add(plugin_dict, filename, p);

	signal_emit("plugin loaded", 2, p->header->name, p->header->version);
}

void
plugin_close(const gchar *filename)
{
	Plugin *p = mowgli_dictionary_retrieve(plugin_dict, filename);

	if (p == NULL)
		return;

	if (p->header->fini)
	{
		signal_emit("plugin unloaded", 1, p->header->name);
		p->header->fini(p);
	}

	g_module_close(p->handle);

	mowgli_dictionary_delete(plugin_dict, filename);
}

void
plugin_autoload(void)
{
	for_files(CONSPIRE_LIBDIR "/plugins", "*" SHARED_SUFFIX, (void(*)(char *)) plugin_load);
}
