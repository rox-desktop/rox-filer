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
#include <gdk/gdkx.h>
#include <gdk/gdkprivate.h> /* XXX - find another way to do this */
#include <gdk/gdkkeysyms.h>
#include <collection.h>

#include "support.h"
#include "gui_support.h"
#include "filer.h"
#include "pixmaps.h"
#include "menu.h"
#include "dnd.h"
#include "apps.h"
#include "mount.h"

FilerWindow 	*window_with_focus = NULL;

/* When a child process that changes a directory dies we need to know
 * which filer window to update. Use this table.
 */
GHashTable	*child_to_filer = NULL;

static int number_of_windows = 0;
static FilerWindow *window_with_selection = NULL;

/* Static prototypes */
static void filer_window_destroyed(GtkWidget    *widget,
				   FilerWindow	*filer_window);
static gboolean idle_scan_dir(gpointer data);
static void draw_item(GtkWidget *widget,
			CollectionItem *item,
			GdkRectangle *area);
void show_menu(Collection *collection, GdkEventButton *event,
		int number_selected, gpointer user_data);
static int sort_by_name(const void *item1, const void *item2);
static void add_item(FilerWindow *filer_window, char *leafname);
static gboolean test_point(Collection *collection,
				int point_x, int point_y,
				CollectionItem *item,
				int width, int height);
static void stop_scanning(FilerWindow *filer_window);
static gint focus_in(GtkWidget *widget,
			GdkEventFocus *event,
			FilerWindow *filer_window);
static gint focus_out(GtkWidget *widget,
			GdkEventFocus *event,
			FilerWindow *filer_window);


void filer_init()
{
	child_to_filer = g_hash_table_new(NULL, NULL);
}

/* When a filer window is destroyed we call this for each entry in the
 * child_to_filer hash table to remove old entries.
 */
static gboolean child_eq(gpointer key, gpointer data, gpointer filer_window)
{
	return data == filer_window;
}

static void filer_window_destroyed(GtkWidget 	*widget,
				   FilerWindow 	*filer_window)
{
	if (window_with_selection == filer_window)
		window_with_selection = NULL;
	if (window_with_focus == filer_window)
		window_with_focus = NULL;

	g_hash_table_foreach_remove(child_to_filer, child_eq, filer_window);

	if (filer_window->dir)
		stop_scanning(filer_window);
	g_free(filer_window->path);
	g_free(filer_window);

	if (--number_of_windows < 1)
		gtk_main_quit();
}

static void stop_scanning(FilerWindow *filer_window)
{
	g_return_if_fail(filer_window->dir != NULL);

	closedir(filer_window->dir);
	gtk_idle_remove(filer_window->idle_scan_id);
	filer_window->dir = NULL;
}

/* This is called while we are scanning the directory */
static gboolean idle_scan_dir(gpointer data)
{
	struct dirent	*next;
	FilerWindow 	*filer_window = (FilerWindow *) data;

	do
	{
		next = readdir(filer_window->dir);
		if (!next)
		{
			closedir(filer_window->dir);
			filer_window->dir = NULL;

			collection_qsort(filer_window->collection,
					sort_by_name);
			return FALSE;		/* Finished */
		}

		add_item(filer_window, next->d_name);
	} while (!gtk_events_pending());

	return TRUE;
}
	
/* Add a single object to a directory display */
static void add_item(FilerWindow *filer_window, char *leafname)
{
	FileItem	*item;
	int		item_width;
	struct stat	info;
	int		base_type;
	GString		*path;

	/* Ignore dot files (should be an option) */
	if (leafname[0] == '.')
		return;

	item = g_malloc(sizeof(FileItem));
	item->leafname = g_strdup(leafname);
	item->flags = 0;

	path = make_path(filer_window->path, leafname);
	if (lstat(path->str, &info))
		base_type = TYPE_ERROR;
	else
	{
		if (S_ISREG(info.st_mode))
			base_type = TYPE_FILE;
		else if (S_ISDIR(info.st_mode))
		{
			base_type = TYPE_DIRECTORY;

			if (g_hash_table_lookup(mtab_mounts, path->str))
				item->flags |= ITEM_FLAG_MOUNT_POINT
						| ITEM_FLAG_MOUNTED;
			else if (g_hash_table_lookup(fstab_mounts, path->str))
				item->flags |= ITEM_FLAG_MOUNT_POINT;
		}
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
			if (stat(path->str, &info))
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

	item->base_type = base_type;

	if (base_type == TYPE_DIRECTORY)
	{
		/* Might be an application directory - better check... */
		path = g_string_append(path, "/AppRun");
		if (!stat(path->str, &info))
			item->flags |= ITEM_FLAG_APPDIR;
	}

	if (item->flags & ITEM_FLAG_APPDIR)
		item->image = default_pixmap + TYPE_APPDIR;
	else
		item->image = default_pixmap + base_type;

	item->text_width = gdk_string_width(filer_window->window->style->font,
			leafname);
	
	/* XXX: Must be a better way... */
	item->pix_width = ((GdkPixmapPrivate *) item->image->pixmap)->width;

	item_width = MAX(item->pix_width, item->text_width) + 4;

	if (item_width > filer_window->collection->item_width)
		collection_set_item_size(filer_window->collection,
					 item_width,
					 filer_window->collection->item_height);

	collection_insert(filer_window->collection, item);
}

static gboolean test_point(Collection *collection,
				int point_x, int point_y,
				CollectionItem *item,
				int width, int height)
{
	FileItem	*fileitem = (FileItem *) item->data;
	GdkFont		*font = GTK_WIDGET(collection)->style->font;
	int		text_height = font->ascent + font->descent;
	int		x_off = ABS(point_x - (width >> 1));
	
	if (x_off <= (fileitem->pix_width >> 1) + 2 &&
		point_y < height - text_height - 2 &&
		point_y > 6)
		return TRUE;
	
	if (x_off <= (fileitem->text_width >> 1) + 2 &&
		point_y > height - text_height - 2)
		return TRUE;

	return FALSE;
}

static void draw_item(GtkWidget *widget,
			CollectionItem *colitem,
			GdkRectangle *area)
{
	FileItem	*item = (FileItem *) colitem->data;
	GdkGC		*gc = colitem->selected ? widget->style->white_gc
				       		: widget->style->black_gc;
	int	image_x = area->x + ((area->width - item->pix_width) >> 1);
	GdkFont	*font = widget->style->font;
	int	text_x = area->x + ((area->width - item->text_width) >> 1);
	int	text_y = area->y + area->height - font->descent - 2;
	int	text_height = font->ascent + font->descent;

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
					0, 0,		/* Source x,y */
					image_x, area->y + 8,	/* Dest x,y */
					-1, -1);
		}
		else if (item->flags & ITEM_FLAG_MOUNT_POINT)
		{
			int	type = item->flags & ITEM_FLAG_MOUNTED
					? TYPE_MOUNTED
					: TYPE_UNMOUNTED;
			gdk_gc_set_clip_mask(gc,
					default_pixmap[type].mask);
			gdk_draw_pixmap(widget->window, gc,
					default_pixmap[type].pixmap,
					0, 0,		/* Source x,y */
					image_x, area->y + 8, /* Dest x,y */
					-1, -1);
		}
		
		gdk_gc_set_clip_mask(gc, NULL);
		gdk_gc_set_clip_origin(gc, 0, 0);
	}
	
	if (colitem->selected)
		gtk_paint_flat_box(widget->style, widget->window, 
				GTK_STATE_SELECTED, GTK_SHADOW_NONE,
				NULL, widget, "text",
				text_x, text_y - font->ascent,
				item->text_width,
				text_height);

	gdk_draw_text(widget->window,
			widget->style->font,
			colitem->selected ? widget->style->white_gc
					  : widget->style->black_gc,
			text_x, text_y,
			item->leafname, strlen(item->leafname));
}

void show_menu(Collection *collection, GdkEventButton *event,
		int item, gpointer user_data)
{
	show_filer_menu((FilerWindow *) user_data, event);
}

void scan_dir(FilerWindow *filer_window)
{
	if (filer_window->dir)
		stop_scanning(filer_window);

	mount_update();
	
	collection_set_item_size(filer_window->collection, 64, 64);
	collection_clear(filer_window->collection);
	gtk_window_set_title(GTK_WINDOW(filer_window->window),
			filer_window->path);

	filer_window->dir = opendir(filer_window->path);
	if (!filer_window->dir)
	{
		report_error("Error scanning directory:", g_strerror(errno));
		return;
	}

	filer_window->idle_scan_id = gtk_idle_add(idle_scan_dir, filer_window);
}

static void gain_selection(Collection 	*collection,
			   gint		number_selected,
			   gpointer 	user_data)
{
	FilerWindow *filer_window = (FilerWindow *) user_data;

	if (window_with_selection && window_with_selection != filer_window)
		collection_clear_selection(window_with_selection->collection);
	
	window_with_selection = filer_window;
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
	GdkEventButton 	*event;
	char		*full_path;

	event = (GdkEventButton *) gtk_get_current_event();
	full_path = make_path(filer_window->path, item->leafname)->str;

	switch (item->base_type)
	{
		case TYPE_DIRECTORY:
			if (item->flags & ITEM_FLAG_APPDIR &&
					(event->type != GDK_2BUTTON_PRESS ||
					(event->state & GDK_SHIFT_MASK) == 0))
			{
				run_app(make_path(filer_window->path,
							item->leafname)->str);
				break;
			}
			if (filer_window->panel == FALSE &&
				(event->type != GDK_2BUTTON_PRESS ||
					event->button == 1))
			{
				filer_window->path = pathdup(full_path);
				scan_dir(filer_window);
			}
			else
				filer_opendir(full_path, FALSE, BOTTOM);
			break;
		default:
			report_error("open_item",
					"I don't know how to open that");
			break;
	}
}

static gint focus_in(GtkWidget *widget,
			GdkEventFocus *event,
			FilerWindow *filer_window)
{
	window_with_focus = filer_window;

	return FALSE;
}

static gint focus_out(GtkWidget *widget,
			GdkEventFocus *event,
			FilerWindow *filer_window)
{
	/* TODO: Shade the cursor */

	return FALSE;
}

/* Handle keys that can't be bound with the menu */
static gint key_press_event(GtkWidget	*widget,
			GdkEventKey	*event,
			FilerWindow	*filer_window)
{
	switch (event->keyval)
	{
		/*
		   case GDK_Left:
		   move_cursor(-1, 0);
		   break;
		   case GDK_Right:
		   move_cursor(1, 0);
		   break;
		   case GDK_Up:
		   move_cursor(0, -1);
		   break;
		   case GDK_Down:
		   move_cursor(0, 1);
		   break;
		   case GDK_Return:
		 */
		case GDK_BackSpace:
			filer_window->path = pathdup(make_path(
						filer_window->path,
						"..")->str);
			scan_dir(filer_window);
			return TRUE;
	}

	return FALSE;
}


void filer_opendir(char *path, gboolean panel, Side panel_side)
{
	GtkWidget	*hbox, *scrollbar, *collection;
	FilerWindow	*filer_window;

	filer_window = g_malloc(sizeof(FilerWindow));
	filer_window->path = pathdup(path);
	filer_window->dir = NULL;	/* Not scanning */
	filer_window->panel = panel;
	filer_window->panel_side = panel_side;

	filer_window->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	collection = collection_new(NULL);
	gtk_object_set_data(GTK_OBJECT(collection),
			"filer_window", filer_window);
	filer_window->collection = COLLECTION(collection);
	collection_set_functions(filer_window->collection,
			draw_item, test_point);

	gtk_signal_connect(GTK_OBJECT(filer_window->window), "focus_in_event",
			GTK_SIGNAL_FUNC(focus_in), filer_window);
	gtk_signal_connect(GTK_OBJECT(filer_window->window), "focus_out_event",
			GTK_SIGNAL_FUNC(focus_out), filer_window);
	gtk_signal_connect(GTK_OBJECT(filer_window->window), "key_press_event",
			GTK_SIGNAL_FUNC(key_press_event), filer_window);
	gtk_signal_connect(GTK_OBJECT(filer_window->collection), "open_item",
			open_item, filer_window);
	gtk_signal_connect(GTK_OBJECT(filer_window->window), "destroy",
			filer_window_destroyed, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "show_menu",
			show_menu, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "gain_selection",
			gain_selection, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "drag_selection",
			drag_selection, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "drag_data_get",
			drag_data_get, filer_window);
	drag_set_dest(collection);

	if (panel)
	{
		int		swidth, sheight, iwidth, iheight;
		GtkWidget	*frame, *win = filer_window->window;

		collection_set_panel(filer_window->collection, TRUE);

		gdk_window_get_size(GDK_ROOT_PARENT(), &swidth, &sheight);
		iwidth = filer_window->collection->item_width;
		iheight = filer_window->collection->item_height;
		
		if (panel_side == TOP || panel_side == BOTTOM)
		{
			int	y = panel_side == TOP ? 0 : sheight - iheight;

			gtk_widget_set_usize(collection, swidth, iheight);
			gtk_widget_set_uposition(win, 0, y);
		}
		else
		{
			int	x = panel_side == LEFT ? 0 : swidth - iwidth;

			gtk_widget_set_usize(collection, iwidth, sheight);
			gtk_widget_set_uposition(win, x, 0);
		}

		frame = gtk_frame_new(NULL);
		gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_OUT);
		gtk_container_add(GTK_CONTAINER(frame), collection);
		gtk_container_add(GTK_CONTAINER(win), frame);

		gtk_widget_realize(win);
		add_panel_properties(win);
	}
	else
	{
		gtk_window_set_default_size(GTK_WINDOW(filer_window->window),
					400, 200);

		hbox = gtk_hbox_new(FALSE, 0);
		gtk_container_add(GTK_CONTAINER(filer_window->window), hbox);
		
		gtk_box_pack_start(GTK_BOX(hbox), collection, TRUE, TRUE, 0);

		scrollbar = gtk_vscrollbar_new(COLLECTION(collection)->vadj);
		gtk_box_pack_start(GTK_BOX(hbox), scrollbar, FALSE, TRUE, 0);
	}

	gtk_widget_show_all(filer_window->window);
	number_of_windows++;

	load_default_pixmaps(collection->window);

	gtk_accel_group_attach(filer_keys, GTK_OBJECT(filer_window->window));

	scan_dir(filer_window);
}
