/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2002, the ROX-Filer team.
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

#include "global.h"

#include "support.h"
#include "choices.h"
#include "options.h"
#include "i18n.h"
#include "gui_support.h"
#include "main.h"

char *current_lang = NULL;	/* Two-char country code, or NULL */

static Option o_translation;

/* Static Prototypes */
static void set_trans(guchar *lang);
static void trans_changed(void);

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/


/* Set things up for internationalisation */
void i18n_init(void)
{
	gtk_set_locale();

	option_add_string(&o_translation, "i18n_translation", "From LANG");
	set_trans(o_translation.value);

	option_add_notify(trans_changed);
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

static void trans_changed(void)
{
	if (!o_translation.has_changed)
		return;

	set_trans(o_translation.value);
	delayed_error(
		_("You must restart the filer for the new language "
		  "setting to take full effect"));
}

/* Load the 'Messages/<name>.gmo' translation.
 * Special values 'None' and 'From LANG' are also allowed.
 */
static void set_trans(guchar *lang)
{
	struct stat info;
	guchar	*path;
	gchar	*lang2 = NULL;

	g_return_if_fail(lang != NULL);

	if (current_lang)
	{
		g_free(current_lang);
		current_lang = NULL;
	}
	
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

	current_lang = lang2 ? lang2 : g_strdup(lang);

	path = g_strdup_printf("%s/Messages/%s.gmo", app_dir, current_lang);
	if (stat(path, &info) == 0)
		rox_add_translations(path);
	g_free(path);
}
