/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 1999, Thomas Leonard, <tal197@ecs.soton.ac.uk>.
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

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <glib.h>
#include <gtk/gtk.h>

#include "gui_support.h"
#include "choices.h"
#include "options.h"

/* Add OptionsSection structs to this list in your _init() functions */
GSList *options_sections = NULL;

static GtkWidget *window, *sections_vbox;
static FILE *save_file = NULL;
static GHashTable *option_hash = NULL;

/* Static prototypes */
static void save_options(GtkWidget *widget, gpointer data);
static char *process_option_line(char *line);

void options_init()
{
	GtkWidget	*tl_vbox, *scrolled_area;
	GtkWidget	*border, *label;
	GtkWidget	*actions, *button;
	char		*string, *save_path;
	
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "ROX-Filer options");
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
	
	save_path = choices_find_path_save("...");
	if (save_path)
	{
		string = g_strconcat("Choices will be saved as ",
					save_path,
					NULL);
		label = gtk_label_new(string);
		g_free(string);
	}
	else
		label = gtk_label_new("Choices saving is disabled by "
					"CHOICESPATH variable");
	gtk_box_pack_start(GTK_BOX(tl_vbox), label, FALSE, TRUE, 0);

	actions = gtk_hbox_new(TRUE, 16);
	gtk_box_pack_start(GTK_BOX(tl_vbox), actions, FALSE, TRUE, 0);
	
	button = gtk_button_new_with_label("Save");
	gtk_box_pack_start(GTK_BOX(actions), button, FALSE, TRUE, 0);
	if (!save_path)
		gtk_widget_set_sensitive(button, FALSE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(save_options), (gpointer) TRUE);

	button = gtk_button_new_with_label("OK");
	gtk_box_pack_start(GTK_BOX(actions), button, FALSE, TRUE, 0);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(save_options), (gpointer) FALSE);

	button = gtk_button_new_with_label("Cancel");
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

			group = gtk_frame_new(section->name);
			gtk_box_pack_start(GTK_BOX(sections_vbox), group,
					FALSE, TRUE, 0);
			gtk_container_add(GTK_CONTAINER(group),
					section->create());
			next = next->next;
		}

		need_init = FALSE;
	}

	path = choices_find_path_load("options");
	if (!path)
		return;		/* Nothing to load */

	parse_file(path, process_option_line);
}

void parse_file(char *path, ParseFunc *parse_line)
{
	char		*data;
	long		length;

	if (load_file(path, &data, &length))
	{
		char *eol, *error;
		char *line = data;
		int  line_number = 1;

		while (line && *line)
		{
			eol = strchr(line, '\n');
			if (eol)
				*eol = '\0';

			error = parse_line(line);

			if (error)
			{
				GString *message;

				message = g_string_new(NULL);
				g_string_sprintf(message,
						"Error in options file at "
						"line %d: %s", line_number,
						error);
				delayed_error("ROX-Filer", message->str);
				g_string_free(message, TRUE);
				break;
			}

			if (!eol)
				break;
			line = eol + 1;
			line_number++;
		}
		g_free(data);
	}
}

/* Call this on init to register a handler for a key (thing before the = in
 * the config file).
 * The function returns a pointer to an error messages (which will
 * NOT be free()d), or NULL on success.
 */
void option_register(char *key, OptionFunc *func)
{
	if (!option_hash)
		option_hash = g_hash_table_new(g_str_hash, g_str_equal);
	g_hash_table_insert(option_hash, key, func);
}

/* Process one line from the options file (\0 term'd).
 * Returns NULL on success, or a pointer to an error message.
 * The line is modified.
 */
static char *process_option_line(char *line)
{
	char 		*eq, *c;
	OptionFunc 	*func;

	g_return_val_if_fail(option_hash != NULL, "No registered functions!");

	eq = strchr(line, '=');
	if (!eq)
		return "Missing '='";

	c = eq - 1;
	while (c > line && (*c == ' ' || *c == '\t'))
		c--;
	c[1] = '\0';
	c = eq + 1;
	while (*c == ' ' || *c == '\t')
		c++;

	func = (OptionFunc *) g_hash_table_lookup(option_hash, line);
	if (!func)
		return "Bad key (no such option name)";

	return func(c);
}

static void save_options(GtkWidget *widget, gpointer data)
{
	gboolean	save = (gboolean) data;
	GSList		*next = options_sections;
	
	while (next)
	{
		OptionsSection *section = (OptionsSection *) next->data;
		section->set();
		next = next->next;
	}
	
	if (save)
	{
		char 		*path;
		
		path = choices_find_path_save("options");
		g_return_if_fail(path != NULL);

		save_file = fopen(path, "wb");
		if (!save_file)
		{
			char *str;
			str = g_strconcat("Unable to open '", path,
					  "' for writing: ",
					  g_strerror(errno),
					  NULL);
			report_error("ROX-Filer", str);
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
			report_error("ROX-Filer", g_strerror(errno));
			return;
		}
	}

	gtk_widget_hide(window);
}

void options_show(FilerWindow *filer_window)
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

void option_write(char *name, char *value)
{
	char    *string;
	int	len;
	
	if (!save_file)
		return;	/* Error already reported hopefully */

	string = g_strconcat(name, " = ", value, "\n", NULL);
	len = strlen(string);
	if (fwrite(string, sizeof(char), len, save_file) < len)
	{
		delayed_error("Saving options", g_strerror(errno));
		fclose(save_file);
		save_file = NULL;
	}
	g_free(string);
}
