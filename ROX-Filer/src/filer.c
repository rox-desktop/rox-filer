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
#include <sys/stat.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <gdk/gdkprivate.h> /* XXX - find another way to do this */
#include <collection.h>

#include "support.h"
#include "directory.h"
#include "gui_support.h"
#include "filer.h"
#include "pixmaps.h"

static int number_of_windows = 0;

/* Static prototypes */
static void filer_window_destroyed(GtkWidget    *widget,
				   FilerWindow	*filer_window);
static void scan_callback(char *leafname, gpointer data);
static void draw_item(GtkWidget *widget,
			gpointer data,
			gboolean selected,
			GdkRectangle *area);
static int sort_by_name(const void *item1, const void *item2);

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
	int		item_width;
	struct stat	info;
	int		base_type;
	char		*path;

	/* Ignore dot files (should be an option) */
	if (leafname[0] == '.')
		return;

	item = g_malloc(sizeof(FileItem));
	item->leafname = g_strdup(leafname);
	item->flags = 0;

	path = make_path(filer_window->dir->path, leafname)->str;
	if (lstat(path, &info))
		base_type = TYPE_ERROR;
	else
	{
		if (S_ISREG(info.st_mode))
			base_type = TYPE_FILE;
		else if (S_ISDIR(info.st_mode))
			base_type = TYPE_DIRECTORY;
		else if (S_ISBLK(info.st_mode))
			base_type = TYPE_BLOCK_DEVICE;
		else if (S_ISCHR(info.st_mode))
			base_type = TYPE_CHAR_DEVICE;
		else if (S_ISFIFO(info.st_mode))
			base_type = TYPE_PIPE;
		else if (S_ISSOCK(info.st_mode))
			base_type = TYPE_SOCKET;
		else if (S_ISLNK(info.st_mode))
		{
			if (stat(path, &info))
			{
				base_type = TYPE_ERROR;
			}
			else
			{
				if (S_ISREG(info.st_mode))
					base_type = TYPE_FILE;
				else if (S_ISDIR(info.st_mode))
					base_type = TYPE_DIRECTORY;
				else if (S_ISBLK(info.st_mode))
					base_type = TYPE_BLOCK_DEVICE;
				else if (S_ISCHR(info.st_mode))
					base_type = TYPE_CHAR_DEVICE;
				else if (S_ISFIFO(info.st_mode))
					base_type = TYPE_PIPE;
				else if (S_ISSOCK(info.st_mode))
					base_type = TYPE_SOCKET;
				else
					base_type = TYPE_UNKNOWN;
			}

			item->flags |= ITEM_FLAG_SYMLINK;
		}
		else
			base_type = TYPE_UNKNOWN;
	}

	item->text_width = gdk_string_width(filer_window->window->style->font,
			leafname);
	item->image = default_pixmap + base_type;
	
	/* XXX: Must be a better way... */
	item->pix_width = ((GdkPixmapPrivate *) item->image->pixmap)->width;

	item_width = MAX(item->pix_width, item->text_width) + 4;

	if (item_width > filer_window->collection->item_width)
		collection_set_item_size(filer_window->collection,
					 item_width,
					 filer_window->collection->item_height);

	collection_insert(filer_window->collection, item);

	/* XXX: Think about this */
	g_main_iteration(FALSE);
}

static void draw_item(GtkWidget *widget,
			gpointer data,
			gboolean selected,
			GdkRectangle *area)
{
	FileItem	*item = (FileItem *) data;
	GdkGC		*gc = selected ? widget->style->white_gc
				       : widget->style->black_gc;
	int	image_x = area->x + ((area->width - item->pix_width) >> 1);

	/*
	gdk_draw_rectangle(widget->window,
			widget->style->black_gc,
			FALSE,
			area->x, area->y,
			area->width - 1, area->height - 1);
	*/

	if (item->image)
	{
		gdk_gc_set_clip_mask(gc, item->image->mask);
		gdk_gc_set_clip_origin(gc, image_x, area->y + 8);
		gdk_draw_pixmap(widget->window, gc,
				item->image->pixmap,
				0, 0,			/* Source x,y */
				image_x, area->y + 8,	/* Dest x,y */
				-1, -1);

		if (item->flags & ITEM_FLAG_SYMLINK)
		{
			gdk_gc_set_clip_mask(gc,
					default_pixmap[TYPE_SYMLINK].mask);
			gdk_draw_pixmap(widget->window, gc,
					default_pixmap[TYPE_SYMLINK].pixmap,
					0, 0,			/* Source x,y */
					image_x, area->y + 8,	/* Dest x,y */
					-1, -1);
		}
		
		gdk_gc_set_clip_mask(gc, NULL);
		gdk_gc_set_clip_origin(gc, 0, 0);
	}
	
	gdk_draw_text(widget->window,
			widget->style->font,
			selected ? widget->style->white_gc
				 : widget->style->black_gc,
			area->x + ((area->width - item->text_width) >> 1),
			area->y + area->height -
				widget->style->font->descent - 2,
		 	item->leafname, strlen(item->leafname));
}

static int sort_by_name(const void *item1, const void *item2)
{
	return strcmp((*((FileItem **)item1))->leafname,
		      (*((FileItem **)item2))->leafname);
}

void open_item(Collection *collection,
		gpointer item_data, int item_number,
		gpointer user_data)
{
	FilerWindow	*filer_window = (FilerWindow *) user_data;
	FileItem	*item = (FileItem *) item_data;

	filer_opendir(make_path(filer_window->dir->path, item->leafname)->str);
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

	gtk_signal_connect(GTK_OBJECT(filer_window->collection), "open_item",
			open_item, filer_window);
	gtk_signal_connect(GTK_OBJECT(filer_window->window), "destroy",
			filer_window_destroyed, filer_window);

	gtk_widget_show_all(filer_window->window);
	number_of_windows++;

	load_default_pixmaps(collection->window);

	/* Note - scan may call g_main_iteration */
	if (!directory_scan(filer_window->dir, scan_callback, filer_window))
		report_error("Error opening directory", g_strerror(errno));

	collection_qsort(filer_window->collection, sort_by_name);
}
