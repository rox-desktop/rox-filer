/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2000, Thomas Leonard, <tal197@users.sourceforge.net>.
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

#include <gtk/gtk.h>

#include "global.h"

#include "support.h"
#include "choices.h"
#include "options.h"
#include "i18n.h"
#include "gui_support.h"
#include "main.h"

/* Static Prototypes */
static char *load_trans(guchar *lang);
static int lang_to_index(guchar *lang);

/* Options bits */
static GtkWidget *create_options();
static void update_options();
static void set_options();
static void save_options();

static GtkWidget *menu_translation;

static OptionsSection options =
{
	N_("Translation options"),
	create_options,
	update_options,
	set_options,
	save_options
};

/* List of strings that are to be saved if the corresponding
 * item in the menu is chosen (eg, '-', '', 'fr', 'it', etc).
 */
static GList *trans_list = NULL;
static gint  trans_index = 1;	/* Currently active index */

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/


/* Set things up for internationalisation */
void i18n_init(void)
{
	guchar		*trans_dir;
	DIR		*dir;
	struct dirent	*ent;
	guchar		*trans;

	options_sections = g_slist_prepend(options_sections, &options);

#ifdef HAVE_LOCALE_H
	setlocale(LC_ALL, "");
#endif
	
	trans_list = g_list_append(NULL, "None");
	trans_list = g_list_append(trans_list, "From LANG");
	
	trans_dir = make_path(app_dir, "Messages")->str;
	dir = opendir(trans_dir);

	while (dir && (ent = readdir(dir)))
	{
		char	*ext, *name;

		ext = strstr(ent->d_name, ".gmo");
		if (!ext)
			continue;

		name = g_strndup(ent->d_name, ext - ent->d_name);
		trans_list = g_list_append(trans_list, name);
	}
	if (dir)
		closedir(dir);

	trans = choices_find_path_load("Translation", "ROX-Filer");

	if (trans)
		parse_file(trans, load_trans);
	else
		load_trans("From LANG");
}

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


/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/


/* Load the 'Messages/<name>.gmo' translation. */
static char *load_trans(guchar *lang)
{
	struct stat info;
	guchar	*path;

	g_return_val_if_fail(lang != NULL, NULL);

	trans_index = lang_to_index(lang);

	if (trans_index == 0)
	{
		rox_clear_translation();
		trans_index = 0;
		return NULL;
	}
	else if (trans_index == 1)
	{
		lang = getenv("LANG");
		if (!lang)
			return NULL;
	}

	path = g_strdup_printf("%s/Messages/%s.gmo", app_dir, lang);
	if (stat(path, &info) == 0)
		rox_add_translations(path);
	g_free(path);

	return NULL;	/* No error (for parse_file()) */
}

/* Build up some option widgets to go in the options dialog, but don't
 * fill them in yet.
 */
static GtkWidget *create_options(void)
{
	GtkWidget	*vbox, *hbox, *menu;
	GList		*next;

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);

	hbox = gtk_hbox_new(FALSE, 4);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

	gtk_box_pack_start(GTK_BOX(hbox),
			gtk_label_new(_("Translation")),
			FALSE, TRUE, 0);

	menu_translation = gtk_option_menu_new();
	menu = gtk_menu_new();

	gtk_option_menu_set_menu(GTK_OPTION_MENU(menu_translation), menu);
	gtk_box_pack_start(GTK_BOX(hbox), menu_translation, TRUE, TRUE, 0);

	for (next = trans_list; next; next = next->next)
	{
		guchar	*this = (guchar *) next->data;
		
		gtk_menu_append(GTK_MENU(menu),
				gtk_menu_item_new_with_label(this));
	}

	return vbox;
}

/* Reflect current state by changing the widgets in the options box */
static void update_options()
{
	gtk_option_menu_set_history(GTK_OPTION_MENU(menu_translation),
			trans_index);
}

/* Set current values by reading the states of the widgets in the options box */
static void set_options()
{
	GtkWidget	*menu, *item;
	GList		*list;
	int		i;

	menu = gtk_option_menu_get_menu(GTK_OPTION_MENU(menu_translation));
	item = gtk_menu_get_active(GTK_MENU(menu));

	list = gtk_container_children(GTK_CONTAINER(menu));
	i = g_list_index(list, item);
	g_list_free(list);

	if (trans_index == i)
		return;

	load_trans((guchar *) g_list_nth(trans_list, i)->data);

	delayed_error(PROJECT,
			_("You must restart the filer for the new language "
			  "setting to take full effect"));
}

static void save_options()
{
	guchar	*path, *name;
	int	len, err;
	FILE	*f;

	path = choices_find_path_save("Translation", "ROX-Filer", TRUE);
	if (!path)
		return;

	f = fopen(path, "wb");
	if (!f)
	{
		delayed_error(PROJECT, g_strerror(errno));
		return;
	}

	name = (guchar *) g_list_nth(trans_list, trans_index)->data;
	len = strlen(name);

	err = (fwrite(name, 1, len, f) < len) |
		(fwrite("\n", 1, 1, f) < 1)   |
		  (fclose(f) != 0);

	if (err)
		delayed_error(PROJECT, g_strerror(errno));
}

/* Returns index in trans_list */
static int lang_to_index(guchar *lang)
{
	GList	*next;
	int	i = 0;

	for (next = trans_list; next; next = next->next)
	{
		guchar	*this = (guchar *) next->data;

		if (strcmp(this, lang) == 0)
			return i;

		i++;
	}

	return 0;
}
