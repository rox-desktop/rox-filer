/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

/* filer.c - code for handling filer windows */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <gtk/gtk.h>
#include <collection.h>

#include "support.h"
#include "directory.h"
#include "gui_support.h"
#include "filer.h"

static int number_of_windows = 0;

/* Static prototypes */
static void filer_window_destroyed(GtkWidget    *widget,
				   FilerWindow	*filer_window);


static void filer_window_destroyed(GtkWidget 	*widget,
				   FilerWindow 	*filer_window)
{
	directory_destroy(filer_window->dir);
	g_free(filer_window);
	
	if (--number_of_windows < 1)
		gtk_main_quit();
}

void filer_opendir(char *path)
{
	GtkWidget	*hbox, *scrollbar, *collection;
	FilerWindow	*filer_window;

	filer_window = g_malloc(sizeof(FilerWindow));
	filer_window->dir = directory_new(path);

	if (!directory_scan(filer_window->dir))
		report_error("Error opening directory", g_strerror(errno));

	filer_window->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(filer_window->window),
				filer_window->dir->path);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(filer_window->window), hbox);
	
	collection = collection_new(NULL);
	gtk_box_pack_start(GTK_BOX(hbox), collection, TRUE, TRUE, 0);
	filer_window->collection = COLLECTION(collection);

	scrollbar = gtk_vscrollbar_new(COLLECTION(collection)->vadj);
	gtk_box_pack_start(GTK_BOX(hbox), scrollbar, FALSE, TRUE, 0);

	gtk_widget_show_all(filer_window->window);
	number_of_windows++;

	gtk_signal_connect(GTK_OBJECT(filer_window->window), "destroy",
			filer_window_destroyed, filer_window);
}
