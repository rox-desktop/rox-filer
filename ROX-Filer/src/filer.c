/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

/* filer.c - code for handling filer windows */

#include <stdlib.h>

#include <gtk/gtk.h>
#include <collection.h>

#include "support.h"
#include "filer.h"

static int number_of_windows = 0;

/* Static prototypes */
static void filer_window_destroyed(GtkWidget *widget, gpointer data);


static void filer_window_destroyed(GtkWidget *widget, gpointer data)
{
	if (--number_of_windows < 1)
		gtk_main_quit();
}

void filer_opendir(char *path)
{
	GtkWidget	*window;
	char		*real_path;

	real_path = pathdup(path);	/* XXX: Store this somewhere */

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), real_path);

	gtk_widget_show_all(window);
	number_of_windows++;

	gtk_signal_connect(GTK_OBJECT(window), "destroy",
			filer_window_destroyed, NULL);
}
