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

/* options.c - code for handling user choices */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <gtk/gtk.h>

#include "global.h"

#include "gui_support.h"
#include "choices.h"
#include "options.h"

/* Add OptionsSection structs to this list in your _init() functions */
GSList *options_sections = NULL;

/* Add all option tooltips to this group */
GtkTooltips *option_tooltips = NULL;

static GtkWidget *window, *sections_vbox;
static FILE *save_file = NULL;
static GHashTable *option_hash = NULL;

enum {BUTTON_SAVE, BUTTON_OK, BUTTON_APPLY};

/* Static prototypes */
static void save_options(GtkWidget *widget, gpointer data);
static char *process_option_line(guchar *line);

void options_init()
{
	GtkWidget	*tl_vbox, *scrolled_area;
	GtkWidget	*border, *label;
	GtkWidget	*actions, *button;
	char		*string, *save_path;

	option_tooltips = gtk_tooltips_new();
	
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);
	gtk_window_set_title(GTK_WINDOW(window), _("ROX-Filer options"));
	gtk_signal_connect(GTK_OBJECT(window), "delete_event",
			GTK_SIGNAL_FUNC(hide_dialog_event), window);
	gtk_container_set_border_width(GTK_CONTAINER(window), 4);
	gtk_window_set_default_size(GTK_WINDOW(window), 400, 400);

	tl_vbox = gtk_vbox_new(FALSE, 4);
	gtk_container_add(GTK_CONTAINER(window), tl_vbox);

	scrolled_area = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_set_border_width(GTK_CONTAINER(scrolled_area), 4);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_area),
			GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	gtk_box_pack_start(GTK_BOX(tl_vbox), scrolled_area, TRUE, TRUE, 0);

	border = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(border), GTK_SHADOW_NONE);
	gtk_container_set_border_width(GTK_CONTAINER(border), 4);
	gtk_scrolled_window_add_with_viewport(
			GTK_SCROLLED_WINDOW(scrolled_area), border);

	sections_vbox = gtk_vbox_new(FALSE, 4);
	gtk_container_add(GTK_CONTAINER(border), sections_vbox);
	
	save_path = choices_find_path_save("...", PROJECT, FALSE);
	if (save_path)
	{
		string = g_strconcat(_("Choices will be saved as "),
					save_path,
					NULL);
		label = gtk_label_new(string);
		g_free(string);
	}
	else
		label = gtk_label_new(_("Choices saving is disabled by "
					"CHOICESPATH variable"));
	gtk_box_pack_start(GTK_BOX(tl_vbox), label, FALSE, TRUE, 0);

	actions = gtk_hbox_new(TRUE, 16);
	gtk_box_pack_start(GTK_BOX(tl_vbox), actions, FALSE, TRUE, 0);
	
	button = gtk_button_new_with_label(_("Save"));
	gtk_box_pack_start(GTK_BOX(actions), button, FALSE, TRUE, 0);
	if (!save_path)
		gtk_widget_set_sensitive(button, FALSE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(save_options), (gpointer) BUTTON_SAVE);

	button = gtk_button_new_with_label(_("OK"));
	gtk_box_pack_start(GTK_BOX(actions), button, FALSE, TRUE, 0);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(save_options), (gpointer) BUTTON_OK);

	button = gtk_button_new_with_label(_("Apply"));
	gtk_box_pack_start(GTK_BOX(actions), button, FALSE, TRUE, 0);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(save_options), (gpointer) BUTTON_APPLY);

	button = gtk_button_new_with_label(_("Cancel"));
	gtk_box_pack_start(GTK_BOX(actions), button, FALSE, TRUE, 0);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(gtk_widget_hide), GTK_OBJECT(window));
}

void options_load(void)
{
	static gboolean need_init = TRUE;
	char	*path;

	if (need_init)
	{
		GtkWidget	*group;
		GSList		*next = options_sections;

		while (next)
		{
			OptionsSection *section = (OptionsSection *) next->data;

			group = gtk_frame_new(_(section->name));
			gtk_box_pack_start(GTK_BOX(sections_vbox), group,
					FALSE, TRUE, 4);
			gtk_container_add(GTK_CONTAINER(group),
					section->create());
			next = next->next;
		}

		need_init = FALSE;
	}

	path = choices_find_path_load("options", PROJECT);
	if (!path)
		return;		/* Nothing to load */

	parse_file(path, process_option_line);
}


/* Call this on init to register a handler for a key (thing before the = in
 * the config file).
 * The function returns a pointer to an error messages (which will
 * NOT be free()d), or NULL on success.
 */
void option_register(guchar *key, OptionFunc *func)
{
	if (!option_hash)
		option_hash = g_hash_table_new(g_str_hash, g_str_equal);
	g_hash_table_insert(option_hash, key, func);
}

/* Process one line from the options file (\0 term'd).
 * Returns NULL on success, or a pointer to an error message.
 * The line is modified.
 */
static char *process_option_line(guchar *line)
{
	guchar 		*eq, *c;
	OptionFunc 	*func;

	g_return_val_if_fail(option_hash != NULL, "No registered functions!");

	eq = strchr(line, '=');
	if (!eq)
		return _("Missing '='");

	c = eq - 1;
	while (c > line && (*c == ' ' || *c == '\t'))
		c--;
	c[1] = '\0';
	c = eq + 1;
	while (*c == ' ' || *c == '\t')
		c++;

	func = (OptionFunc *) g_hash_table_lookup(option_hash, line);
	if (!func)
		return _("Unknown option");

	return func(c);
}

static void save_options(GtkWidget *widget, gpointer data)
{
	int		button = (int) data;
	GSList		*next = options_sections;
	
	while (next)
	{
		OptionsSection *section = (OptionsSection *) next->data;
		section->set();
		next = next->next;
	}
	
	if (button == BUTTON_SAVE)
	{
		char 		*path;
		
		path = choices_find_path_save("options", PROJECT, TRUE);
		g_return_if_fail(path != NULL);

		save_file = fopen(path, "wb");
		if (!save_file)
		{
			char *str;
			str = g_strdup_printf(
				_("Unable to open '%s' for writing: %s"),
					path, g_strerror(errno));
			report_error(PROJECT, str);
			g_free(str);
			return;
		}

		next = options_sections;
		while (next)
		{
			OptionsSection *section = (OptionsSection *) next->data;
			section->save();
			next = next->next;
		}

		if (save_file && fclose(save_file) == EOF)
		{
			report_error(PROJECT, g_strerror(errno));
			return;
		}
	}

	if (button != BUTTON_APPLY)
		gtk_widget_hide(window);
}

void options_show(void)
{
	GSList		*next = options_sections;
	
	if (GTK_WIDGET_MAPPED(window))
		gtk_widget_hide(window);

	while (next)
	{
		OptionsSection *section = (OptionsSection *) next->data;
		section->update();
		next = next->next;
	}

	gtk_widget_show_all(window);
}

void option_write(guchar *name, guchar *value)
{
	char    *string;
	int	len;
	
	if (!save_file)
		return;	/* Error already reported hopefully */

	string = g_strconcat(name, " = ", value, "\n", NULL);
	len = strlen(string);
	if (fwrite(string, sizeof(char), len, save_file) < len)
	{
		delayed_error(_("Saving options"), g_strerror(errno));
		fclose(save_file);
		save_file = NULL;
	}
	g_free(string);
}
