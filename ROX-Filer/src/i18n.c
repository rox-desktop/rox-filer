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

#include <gtk/gtk.h>

#include "support.h"
#include "choices.h"
#include "options.h"
#include "i18n.h"
#include "gui_support.h"

/* Static prototypes */
static GtkWidget *create_options();
static void update_options();
static void set_options();
static void save_options();
static char *parse_lang(guchar *line);

static OptionsSection options =
{
	N_("Locale options"),
	create_options,
	update_options,
	set_options,
	save_options
};

static guchar *o_default_lang = NULL;


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/


/* Set things up for internationalisation */
void i18n_init(void)
{
	o_default_lang = g_strdup("");

#ifdef HAVE_GETTEXT
	{
		guchar	*lang_path;

		lang_path = choices_find_path_load("Locale", "ROX-Filer");
		if (lang_path)
			parse_file(lang_path, parse_lang);

		if (*o_default_lang)
			setenv("LANG", o_default_lang, 1);
		setlocale(LC_ALL, "");
		bindtextdomain("ROX-Filer",
				make_path(getenv("APP_DIR"), "po")->str);
		textdomain("ROX-Filer");
	}
#endif
	options_sections = g_slist_prepend(options_sections, &options);
}


/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/


/*			OPTIONS 				*/

#ifdef HAVE_GETTEXT
static GtkWidget *w_lang_entry = NULL;
#endif

/* Build up some option widgets to go in the options dialog, but don't
 * fill them in yet.
 */
static GtkWidget *create_options()
{
#ifdef HAVE_GETTEXT
	GtkWidget	*vbox, *hbox, *label;

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);

	hbox = gtk_hbox_new(TRUE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

	label = gtk_label_new("Language to use");
	gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 0);

	w_lang_entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hbox), w_lang_entry, TRUE, TRUE, 0);

	return vbox;
#else
	label = gtk_label_new("Not compiled with gettext support");
	return label;
#endif
}

/* Reflect current state by changing the widgets in the options box */
static void update_options()
{
#ifdef HAVE_GETTEXT
	gtk_entry_set_text(GTK_ENTRY(w_lang_entry), o_default_lang);
#endif
}

/* Set current values by reading the states of the widgets in the options box */
static void set_options()
{
#ifdef HAVE_GETTEXT
	g_free(o_default_lang);

	o_default_lang = gtk_editable_get_chars(GTK_EDITABLE(w_lang_entry),
						0, -1);
#endif
}

static void save_options()
{
	guchar	*lang_path;

	lang_path = choices_find_path_save("Locale", "ROX-Filer", TRUE);

	if (lang_path)
	{
		FILE	*file;
		int	count;

		file = fopen(lang_path, "wb");
		if (!file)
		{
			delayed_error(PROJECT, g_strerror(errno));
			return;
		}

		count = strlen(o_default_lang);
		if (fwrite(o_default_lang, 1, count, file) < count)
		{
			delayed_error(PROJECT, g_strerror(errno));
			fclose(file);
		}
		else if (fclose(file))
			delayed_error(PROJECT, g_strerror(errno));
	}
}

static char *parse_lang(guchar *line)
{
	o_default_lang = g_strdup(line);

	return NULL;
}

/* These two stolen from dia :-) */
GtkItemFactoryEntry *translate_entries(GtkItemFactoryEntry *entries, gint n)
{
	gint i;
	GtkItemFactoryEntry *ret;

	ret = g_malloc(sizeof(GtkItemFactoryEntry) * n);
	for (i = 0; i < n; i++)
	{
		/* Translation. Note the explicit use of gettext(). */
		ret[i].path = g_strdup(gettext(entries[i].path));
		/* accelerator and item_type are not duped, only referenced */
		ret[i].accelerator = entries[i].accelerator;
		ret[i].callback = entries[i].callback;
		ret[i].callback_action = entries[i].callback_action;
		ret[i].item_type = entries[i].item_type;
	}
	return ret;
}

void free_translated_entries(GtkItemFactoryEntry *entries, gint n)
{
	gint i;

	for (i=0; i<n; i++)
		g_free(entries[i].path);
	g_free(entries);
}

