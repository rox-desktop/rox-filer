/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

/* options.c - code for handling user choices */

#include <glib.h>
#include <gtk/gtk.h>

#include "options.h"

static GtkWidget *window;

static gint hide_options(GtkWidget *options, gpointer data);

void options_init()
{
	GtkWidget	*tl_vbox, *scrolled_area, *vbox;
	GtkWidget	*group, *label;
	GtkWidget	*actions, *button;
	
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "ROX-Filer options");
	gtk_signal_connect(GTK_OBJECT(window), "delete_event",
			GTK_SIGNAL_FUNC(hide_options), NULL);
	gtk_container_set_border_width(GTK_CONTAINER(window), 4);
	gtk_window_set_default_size(GTK_WINDOW(window), 400, 400);

	tl_vbox = gtk_vbox_new(FALSE, 4);
	gtk_container_add(GTK_CONTAINER(window), tl_vbox);

	scrolled_area = gtk_scrolled_window_new(NULL, NULL);
	gtk_container_set_border_width(GTK_CONTAINER(scrolled_area), 4);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_area),
			GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	gtk_box_pack_start(GTK_BOX(tl_vbox), scrolled_area, TRUE, TRUE, 0);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_scrolled_window_add_with_viewport(
			GTK_SCROLLED_WINDOW(scrolled_area), vbox);
	
	group = gtk_frame_new("Copy options");
	gtk_box_pack_start(GTK_BOX(vbox), group, FALSE, TRUE, 0);
	label = gtk_label_new("There are no copy options yet...");
	gtk_container_add(GTK_CONTAINER(group), label);

	group = gtk_frame_new("External programs");
	gtk_box_pack_start(GTK_BOX(vbox), group, FALSE, TRUE, 0);
	label = gtk_label_new("There are no external program options yet...");
	gtk_container_add(GTK_CONTAINER(group), label);

	actions = gtk_hbox_new(TRUE, 0);
	gtk_box_pack_start(GTK_BOX(tl_vbox), actions, FALSE, TRUE, 0);
	
	button = gtk_button_new_with_label("OK");
	gtk_box_pack_start(GTK_BOX(actions), button, FALSE, TRUE, 0);

	button = gtk_button_new_with_label("Cancel");
	gtk_box_pack_start(GTK_BOX(actions), button, FALSE, TRUE, 0);
}

static gint hide_options(GtkWidget *options, gpointer data)
{
	gtk_widget_hide(window);

	return TRUE;
}

void options_edit(FilerWindow *filer_window)
{
	gtk_widget_show_all(window);
}
