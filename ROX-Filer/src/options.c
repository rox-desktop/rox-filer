/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

/* options.c - code for handling user choices */

#include <glib.h>
#include <gtk/gtk.h>

#include "gui_support.h"
#include "choices.h"
#include "menu.h"
#include "options.h"

static GtkWidget *window;

/* Static prototypes */
static void save_options(GtkWidget *widget, gpointer data);

void options_init()
{
	GtkWidget	*tl_vbox, *scrolled_area, *vbox;
	GtkWidget	*group, *border, *label;
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

	vbox = gtk_vbox_new(FALSE, 4);
	gtk_container_add(GTK_CONTAINER(border), vbox);
	
	group = gtk_frame_new("Copy options");
	gtk_box_pack_start(GTK_BOX(vbox), group, FALSE, TRUE, 0);
	label = gtk_label_new("There are no copy options yet...");
	gtk_container_add(GTK_CONTAINER(group), label);

	group = gtk_frame_new("External programs");
	gtk_box_pack_start(GTK_BOX(vbox), group, FALSE, TRUE, 0);
	label = gtk_label_new("There are no external program options yet...");
	gtk_container_add(GTK_CONTAINER(group), label);

	group = gtk_frame_new("Short-cut keys");
	gtk_box_pack_start(GTK_BOX(vbox), group, FALSE, TRUE, 0);
	label = gtk_label_new("To set the keyboard short-cuts you simply open "
			"the menu over a filer window, move the pointer over "
			"the item you want to use and press a key. The key "
			"will appear next to the menu item and you can just "
			"press that key without opening the menu in future. "
			"To save the current menu short-cuts for next time, "
			"click the Save button at the bottom of this window.");
	gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
	gtk_container_add(GTK_CONTAINER(group), label);

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
			GTK_SIGNAL_FUNC(save_options), NULL);

	button = gtk_button_new_with_label("Dismiss");
	gtk_box_pack_start(GTK_BOX(actions), button, FALSE, TRUE, 0);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(gtk_widget_hide), GTK_OBJECT(window));
}

static void save_options(GtkWidget *widget, gpointer data)
{
	menu_save();
	gtk_widget_hide(window);
}

void options_show(FilerWindow *filer_window)
{
	if (GTK_WIDGET_MAPPED(window))
		gtk_widget_hide(window);
	gtk_widget_show_all(window);
}
