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

/* savebox.c - code for handling those Newdir/Copy/Rename boxes */

#include "config.h"

#include <glib.h>
#include <gtk/gtk.h>

#include "support.h"
#include "gui_support.h"
#include "savebox.h"
#include "filer.h"

static GtkWidget *window, *pathname_entry;
static GtkWidget *icon = NULL;
static FilerWindow *filer_window;
static SaveBoxCallback *callback;
static GtkWidget *icon_frame;
static char *initial = NULL;

/* Static prototypes */
static void do_callback(GtkWidget *widget, gpointer data);

void savebox_init()
{
	GtkWidget	*sep;
	GtkWidget	*button;
	GtkWidget	*table;
	
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(window), 300, 0);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_MOUSE);
	gtk_signal_connect(GTK_OBJECT(window), "delete_event",
			GTK_SIGNAL_FUNC(hide_dialog_event), window);
	gtk_container_set_border_width(GTK_CONTAINER(window), 8);

	table = gtk_table_new(4, 2, FALSE);
	gtk_container_add(GTK_CONTAINER(window), table);

	icon_frame = gtk_frame_new(NULL);
	gtk_container_set_border_width(GTK_CONTAINER(icon_frame), 8);
	gtk_frame_set_shadow_type(GTK_FRAME(icon_frame), GTK_SHADOW_NONE);
	gtk_table_attach_defaults(GTK_TABLE(table), icon_frame, 0, 2, 0, 1);

	pathname_entry = gtk_entry_new();
	gtk_table_attach_defaults(GTK_TABLE(table),
			pathname_entry, 0, 2, 1, 2);
	gtk_signal_connect(GTK_OBJECT(pathname_entry), "activate",
			   GTK_SIGNAL_FUNC(do_callback), NULL);

	sep = gtk_hseparator_new();
	gtk_table_attach(GTK_TABLE(table), sep, 0, 2, 2, 3,
			GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 4);

	button = gtk_button_new_with_label("OK");
	gtk_table_attach_defaults(GTK_TABLE(table), button, 0, 1, 3, 4);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			   GTK_SIGNAL_FUNC(do_callback), NULL);

	button = gtk_button_new_with_label("Cancel");
	gtk_table_attach_defaults(GTK_TABLE(table), button, 1, 2, 3, 4);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(gtk_widget_hide), GTK_OBJECT(window));
}

void savebox_show(FilerWindow *fw, char *title, char *path, char *leaf,
		  MaskedPixmap *image, SaveBoxCallback *cb)
{
	GString	*tmp;
	filer_window = fw;

	tmp = make_path(path, leaf);
	if (initial)
		g_free(initial);
	initial = g_strdup(tmp->str);

	if (GTK_WIDGET_MAPPED(window))
		gtk_widget_hide(window);
	gtk_window_set_title(GTK_WINDOW(window), title);

	if (icon)
		gtk_pixmap_set(GTK_PIXMAP(icon), image->pixmap, image->mask);
	else
	{
		icon = gtk_pixmap_new(image->pixmap, image->mask);
		gtk_container_add(GTK_CONTAINER(icon_frame), icon);
	}

	callback = cb;

	gtk_entry_set_text(GTK_ENTRY(pathname_entry), tmp->str);
	gtk_entry_select_region(GTK_ENTRY(pathname_entry),
			tmp->len - strlen(leaf), -1);
	
	gtk_widget_grab_focus(pathname_entry);
	gtk_widget_show_all(window);
}

static void do_callback(GtkWidget *widget, gpointer data)
{
	char *path;
	
	g_return_if_fail(callback != NULL);
	g_return_if_fail(initial != NULL);

	path = gtk_entry_get_text(GTK_ENTRY(pathname_entry));

	if (callback(initial, path))
	{
		gtk_widget_hide(window);
		update_dir(filer_window, TRUE);
	}
}
