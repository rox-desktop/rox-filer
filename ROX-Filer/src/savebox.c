/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

/* savebox.c - code for handling those Newdir/Copy/Rename boxes */

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <glib.h>
#include <gtk/gtk.h>

#include "support.h"
#include "gui_support.h"
#include "savebox.h"
#include "filer.h"

static GtkWidget *window, *pathname_entry;
static FilerWindow *filer_window;

/* Static prototypes */
static void create_dir(GtkWidget *widget, gpointer data);

void savebox_init()
{
	GtkWidget	*table, *label, *sep;
	GtkWidget	*button;
	
	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(window), 300, 0);
	gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_MOUSE);
	gtk_signal_connect(GTK_OBJECT(window), "delete_event",
			GTK_SIGNAL_FUNC(hide_dialog_event), window);
	gtk_container_set_border_width(GTK_CONTAINER(window), 8);

	table = gtk_table_new(4, 2, TRUE);
	gtk_container_add(GTK_CONTAINER(window), table);

	label = gtk_label_new("Enter pathname:");
	gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 2, 0, 1);

	pathname_entry = gtk_entry_new();
	gtk_table_attach_defaults(GTK_TABLE(table),
			pathname_entry, 0, 2, 1, 2);
	gtk_signal_connect(GTK_OBJECT(pathname_entry), "activate",
			   GTK_SIGNAL_FUNC(create_dir), NULL);

	sep = gtk_hseparator_new();
	gtk_table_attach_defaults(GTK_TABLE(table), sep, 0, 2, 2, 3);

	button = gtk_button_new_with_label("OK");
	gtk_table_attach_defaults(GTK_TABLE(table), button, 0, 1, 3, 4);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			   GTK_SIGNAL_FUNC(create_dir), NULL);

	button = gtk_button_new_with_label("Cancel");
	gtk_table_attach_defaults(GTK_TABLE(table), button, 1, 2, 3, 4);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(gtk_widget_hide), GTK_OBJECT(window));
}

void savebox_show(FilerWindow *fw, char *title, char *path, char *leaf)
{
	GString	*tmp;
	filer_window = fw;

	tmp = make_path(path, leaf);

	if (GTK_WIDGET_MAPPED(window))
		gtk_widget_hide(window);
	gtk_window_set_title(GTK_WINDOW(window), title);

	gtk_entry_set_text(GTK_ENTRY(pathname_entry), tmp->str);
	gtk_entry_select_region(GTK_ENTRY(pathname_entry),
			tmp->len - strlen(leaf), -1);
	
	gtk_widget_grab_focus(pathname_entry);
	gtk_widget_show_all(window);
}

static void create_dir(GtkWidget *widget, gpointer data)
{
	char *path;
	
	path = gtk_entry_get_text(GTK_ENTRY(pathname_entry));

	if (mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO))
		report_error("mkdir", g_strerror(errno));
	else
	{
		gtk_widget_hide(window);
		scan_dir(filer_window);
	}
}
