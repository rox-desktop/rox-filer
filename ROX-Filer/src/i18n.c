/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2001, the ROX-Filer team.
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

#include "global.h"

#include "support.h"
#include "choices.h"
#include "options.h"
#include "i18n.h"
#include "gui_support.h"
#include "main.h"

/* Static Prototypes */
static char *load_trans(guchar *lang);
static void set_trans(guchar *lang);
static void save(void);

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/


/* Set things up for internationalisation */
void i18n_init(void)
{
	guchar		*trans;

#ifdef HAVE_LOCALE_H
	setlocale(LC_ALL, "");
#endif
	
	trans = choices_find_path_load("Translation", PROJECT);

	if (trans)
	{
		parse_file(trans, load_trans);
		g_free(trans);
	}
	else
		load_trans("From LANG");

	option_add_saver(save);
}

/* These two stolen from dia :-).
 * Slight modification though: '>' means 'same as above' so that
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


/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static void trans_changed(guchar *lang)
{
	set_trans(lang);
	delayed_error(_("You must restart the filer for the new language "
		  "setting to take full effect"));
}

/* Just read the Translation file on startup */
static char *load_trans(guchar *lang)
{
	static gboolean init = FALSE;

	if (!init)
	{
		init = TRUE;
		option_add_string("i18n_translation", lang, trans_changed);
		option_set_save("i18n_translation", FALSE);
	}

	set_trans(lang);

	return NULL;
}

/* Load the 'Messages/<name>.gmo' translation.
 * Special values 'None' and 'From LANG' are also allowed.
 */
static void set_trans(guchar *lang)
{
	struct stat info;
	guchar	*path;
	guchar	*lang2 = NULL;

	g_return_if_fail(lang != NULL);

	if (strcmp(lang, "None") == 0)
	{
		rox_clear_translation();
		return;
	}
	else if (strcmp(lang, "From LANG") == 0)
	{
		lang = getenv("LANG");
		if (!lang)
			return;
		/* Extract the language code from the locale name.
		 * language[_territory][.codeset][@modifier]
		 */
		if (lang[0] != '\0' && lang[1] != '\0'
		    && (lang[2] == '_' || lang[2] == '.' || lang[2] == '@'))
			lang2 = g_strndup((gchar *) lang, 2);
	}

	path = g_strdup_printf("%s/Messages/%s.gmo", app_dir,
			(lang2 != NULL) ? lang2 : lang);
	if (stat(path, &info) == 0)
		rox_add_translations(path);
	g_free(path);
	g_free(lang2);
}

static void save(void)
{
	guchar	*path;
	int	len, err;
	FILE	*f;
	guchar	*lang;

	path = choices_find_path_save("Translation", PROJECT, TRUE);
	if (!path)
		return;

	lang = option_get_static_string("i18n_translation");

	f = fopen(path, "wb");
	g_free(path);
	if (!f)
	{
		delayed_error(g_strerror(errno));
		return;
	}

	len = strlen(lang);

	err = (fwrite(lang, 1, len, f) < len) |
		(fwrite("\n", 1, 1, f) < 1)   |
		  (fclose(f) != 0);

	if (err)
		delayed_error(g_strerror(errno));
}
