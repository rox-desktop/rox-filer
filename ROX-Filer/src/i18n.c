/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2000, Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#ifdef HAVE_LOCALE_H
#  include <locale.h>
#endif
#include <sys/stat.h>
#include <unistd.h>

#include <gtk/gtk.h>

#include "support.h"
#include "choices.h"
#include "options.h"
#include "i18n.h"
#include "gui_support.h"


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/


/* Set things up for internationalisation */
void i18n_init(void)
{
	guchar	*lang;

	lang = getenv("LANG");

#ifdef HAVE_LOCALE_H
	setlocale(LC_ALL, "");
#endif
	
	if (lang)
	{
		struct stat info;
		guchar	*path;

		path = g_strdup_printf("%s/Messages/%s.gmo",
				getenv("APP_DIR"), lang);
		if (stat(path, &info) == 0)
			rox_add_translations(path);
		g_free(path);
	}
}


/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/


/* These two stolen from dia :-).
 * Slight modification though: '/%s/' means 'same as above' so that
 * if a translation is missing it doesn't muck up the whole menu structure!
 */
GtkItemFactoryEntry *translate_entries(GtkItemFactoryEntry *entries, gint n)
{
	guchar	*first = NULL, *second = NULL;	/* Previous menu, submenu */
	gint i;
	GtkItemFactoryEntry *ret;

	ret = g_malloc(sizeof(GtkItemFactoryEntry) * n);
	for (i = 0; i < n; i++)
	{
		guchar	*from = entries[i].path;
		guchar	*trans, *slash;
		int	indent;

		if (from[0] == '>')
		{
			if (from[1] == '>')
				indent = 2;
			else
				indent = 1;
		}
		else
			indent = 0;

		if (from[indent])
			from = _(from + indent);
		else
			from = "";

		if (indent == 0)
			trans = g_strdup_printf("/%s", from);
		else if (indent == 1)
			trans = g_strdup_printf("/%s/%s", first, from);
		else
			trans = g_strdup_printf("/%s/%s/%s",
					first, second, from);

		ret[i].path = trans;

		g_free(first);
		g_free(second);
		second = NULL;

		trans++;
		slash = strchr(trans, '/');
		if (slash)
		{
			first = g_strndup(trans, slash - trans);
			trans = slash + 1;

			slash = strchr(trans, '/');
			if (slash)
				second = g_strndup(trans, slash - trans);
			else
				second = g_strdup(trans);
		}
		else
			first = g_strdup(trans);

		/* accelerator and item_type are not duped, only referenced */
		ret[i].accelerator = entries[i].accelerator;
		ret[i].callback = entries[i].callback;
		ret[i].callback_action = entries[i].callback_action;
		ret[i].item_type = entries[i].item_type;
	}
	
	g_free(first);
	g_free(second);

	return ret;
}

void free_translated_entries(GtkItemFactoryEntry *entries, gint n)
{
	gint i;

	for (i=0; i<n; i++)
		g_free(entries[i].path);
	g_free(entries);
}

