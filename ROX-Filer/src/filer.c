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
static void scan_callback(char *leafname, gpointer data);
static void draw_item(GtkWidget *widget,
			gpointer data,
			gboolean selected,
			GdkRectangle *area);


static void filer_window_destroyed(GtkWidget 	*widget,
				   FilerWindow 	*filer_window)
{
	directory_destroy(filer_window->dir);
	g_free(filer_window);

	if (--number_of_windows < 1)
		gtk_main_quit();
}

static void scan_callback(char *leafname, gpointer data)
{
	FilerWindow 	*filer_window = (FilerWindow *) data;
	FileItem	*item;

	item = g_malloc(sizeof(FileItem));
	item->leafname = g_strdup(leafname);

	collection_insert(filer_window->collection, item);

	item->text_width = gdk_string_width(filer_window->window->style->font,
			leafname);

	if (item->text_width + 4 > filer_window->collection->item_width)
		collection_set_item_size(filer_window->collection,
					 item->text_width + 4,
					 filer_window->collection->item_height);
}

static void draw_item(GtkWidget *widget,
			gpointer data,
			gboolean selected,
			GdkRectangle *area)
{
	FileItem	*item = (FileItem *) data;

	gdk_draw_rectangle(widget->window,
			widget->style->black_gc,
			FALSE,
			area->x, area->y,
			area->width - 1, area->height - 1);
	
	gdk_draw_text(widget->window,
			widget->style->font,
			selected ? widget->style->white_gc
				 : widget->style->black_gc,
			area->x + ((area->width - item->text_width) >> 1),
			area->y + area->height -
				widget->style->font->descent - 2,
		 	item->leafname, strlen(item->leafname));
}

void filer_opendir(char *path)
{
	GtkWidget	*hbox, *scrollbar, *collection;
	FilerWindow	*filer_window;

	filer_window = g_malloc(sizeof(FilerWindow));
	filer_window->dir = directory_new(path);

	collection = collection_new(NULL);
	filer_window->collection = COLLECTION(collection);
	collection_set_functions(filer_window->collection,
			draw_item, NULL);

	filer_window->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(filer_window->window),
				filer_window->dir->path);
	gtk_window_set_default_size(GTK_WINDOW(filer_window->window),
				400, 200);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(filer_window->window), hbox);
	
	gtk_box_pack_start(GTK_BOX(hbox), collection, TRUE, TRUE, 0);

	scrollbar = gtk_vscrollbar_new(COLLECTION(collection)->vadj);
	gtk_box_pack_start(GTK_BOX(hbox), scrollbar, FALSE, TRUE, 0);

	gtk_signal_connect(GTK_OBJECT(filer_window->window), "destroy",
			filer_window_destroyed, filer_window);

	gtk_widget_show_all(filer_window->window);
	number_of_windows++;

	if (!directory_scan(filer_window->dir, scan_callback, filer_window))
		report_error("Error opening directory", g_strerror(errno));
}
