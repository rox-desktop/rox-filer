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

#define MESSAGE _("Note that you must save your choices "		\
		  "and restart the filer for the new language "		\
		  "setting to take full effect.")

char *current_lang = NULL;	/* Two-char country code, or NULL */

static Option o_translation;

static GtkWidget *i18n_message = NULL;

/* Static Prototypes */
static void set_trans(guchar *lang);
static void trans_changed(void);
static GList *build_i18n_message(Option *option, xmlNode *node, guchar *label);

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/


/* Set things up for internationalisation */
void i18n_init(void)
{
	gtk_set_locale();

	option_add_string(&o_translation, "i18n_translation", "From LANG");
	set_trans(o_translation.value);
	o_translation.has_changed = FALSE; /* Prevent warning about saving */

	option_add_notify(trans_changed);

	option_register_widget("i18n-message", build_i18n_message);
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
		const gchar	*from = entries[i].path;
		gchar	*trans, *slash;
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
		null_g_free(&second);

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

	if (i18n_message)
		gtk_label_set_text(GTK_LABEL(i18n_message), MESSAGE);
}

/* Load the 'Messages/<name>.gmo' translation.
 * Special values 'None' and 'From LANG' are also allowed.
 */
static void set_trans(guchar *lang)
{
	guchar	*path;
	gchar	*lang2 = NULL;

	g_return_if_fail(lang != NULL);

	rox_clear_translation();

	null_g_free(&current_lang);

	if (strcmp(lang, "None") == 0)
		return;
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
	if (file_exists(path))
		rox_add_translations(path);
	g_free(path);
}

static GList *build_i18n_message(Option *option, xmlNode *node, guchar *label)
{
	GtkWidget *hbox, *image, *align;

	g_return_val_if_fail(option == NULL, NULL);
	g_return_val_if_fail(label == NULL, NULL);
	g_return_val_if_fail(i18n_message == NULL, NULL);

	i18n_message = gtk_label_new(MESSAGE);
	g_signal_connect(i18n_message, "destroy",
			G_CALLBACK(gtk_widget_destroyed), &i18n_message);

	gtk_misc_set_alignment(GTK_MISC(i18n_message), 0, 0.5);
	gtk_label_set_justify(GTK_LABEL(i18n_message), GTK_JUSTIFY_LEFT);
	gtk_label_set_line_wrap(GTK_LABEL(i18n_message), TRUE);

	hbox = gtk_hbox_new(FALSE, 4);
	image = gtk_image_new_from_stock(GTK_STOCK_DIALOG_INFO,
			GTK_ICON_SIZE_BUTTON);
	align = gtk_alignment_new(0, 0, 0, 0);

	gtk_container_add(GTK_CONTAINER(align), image);
	gtk_box_pack_start(GTK_BOX(hbox), align, FALSE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(hbox), i18n_message, FALSE, TRUE, 0);

	return g_list_append(NULL, hbox);
}
