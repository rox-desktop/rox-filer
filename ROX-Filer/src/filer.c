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
#include <time.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkprivate.h> /* XXX - find another way to do this */
#include <gdk/gdkkeysyms.h>
#include <collection.h>

#include "main.h"
#include "support.h"
#include "gui_support.h"
#include "filer.h"
#include "pixmaps.h"
#include "menu.h"
#include "dnd.h"
#include "apps.h"
#include "mount.h"
#include "type.h"

#define MAX_ICON_HEIGHT 42
#define PANEL_BORDER 2

FilerWindow 	*window_with_focus = NULL;

/* When a child process that changes a directory dies we need to know
 * which filer window to update. Use this table.
 */
GHashTable	*child_to_filer = NULL;

/* Link paths to GLists of filer windows */
GHashTable	*path_to_window_list = NULL;

static FilerWindow *window_with_selection = NULL;
static FilerWindow *panel_with_timeout = NULL;
static gint panel_timeout;

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
static void add_view(FilerWindow *filer_window);
static void remove_view(FilerWindow *filer_window);

static GdkAtom xa_string;
enum
{
	TARGET_STRING,
	TARGET_URI_LIST,
};

void filer_init()
{
	xa_string = gdk_atom_intern("STRING", FALSE);

	child_to_filer = g_hash_table_new(NULL, NULL);
	path_to_window_list = g_hash_table_new(g_str_hash, g_str_equal);
}

/* When a filer window shows a directory, use this function to add
 * it to the list of directories to be updated when the contents
 * change.
 */
static void add_view(FilerWindow *filer_window)
{
	GList	*list, *newlist;

	g_return_if_fail(filer_window != NULL);
	
	list = g_hash_table_lookup(path_to_window_list, filer_window->path);
	newlist = g_list_prepend(list, filer_window);
	if (newlist != list)
		g_hash_table_insert(path_to_window_list, filer_window->path,
				newlist);
}

/* When a filer window no longer shows a directory, call this to reverse
 * the effect of add_view().
 */
static void remove_view(FilerWindow *filer_window)
{
	GList	*list, *newlist;

	g_return_if_fail(filer_window != NULL);

	list = g_hash_table_lookup(path_to_window_list, filer_window->path);
	newlist = g_list_remove(list, filer_window);
	if (newlist != list)
		g_hash_table_insert(path_to_window_list, filer_window->path,
				newlist);
}

/* When a filer window is destroyed we call this for each entry in the
 * child_to_filer hash table to remove old entries.
 */
static gboolean child_eq(gpointer key, gpointer data, gpointer filer_window)
{
	return data == filer_window;
}

/* Go though all the FileItems in a collection, freeing all the temp
 * icons.
 * TODO: Maybe we should cache icons?
 */
static void free_temp_icons(FilerWindow *filer_window)
{
	int		i;
	Collection	*collection = filer_window->collection;

	for (i = 0; i < collection->number_of_items; i++)
	{
		FileItem	*item = (FileItem *) collection->items[i].data;
		if (item->flags & ITEM_FLAG_TEMP_ICON)
		{
			gdk_pixmap_unref(item->image->pixmap);
			gdk_pixmap_unref(item->image->mask);
			g_free(item->image);
			item->image = default_pixmap + TYPE_ERROR;
		}
	}
}

static void filer_window_destroyed(GtkWidget 	*widget,
				   FilerWindow 	*filer_window)
{
	if (window_with_selection == filer_window)
		window_with_selection = NULL;
	if (window_with_focus == filer_window)
		window_with_focus = NULL;
	if (panel_with_timeout == filer_window)
	{
		/* Can this happen? */
		panel_with_timeout = NULL;
		gtk_timeout_remove(panel_timeout);
	}

	remove_view(filer_window);
	g_hash_table_foreach_remove(child_to_filer, child_eq, filer_window);

	free_temp_icons(filer_window);
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

			collection_set_item_size(filer_window->collection,
					filer_window->scan_min_width,
					filer_window->collection->item_height);
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

	if (leafname[0] == '.')
	{
		if (filer_window->show_hidden == FALSE || leafname[1] == '\0'
				|| (leafname[1] == '.' && leafname[2] == '\0'))
		return;
	}

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

	item->mime_type = type_from_path(path->str);

	if (base_type == TYPE_DIRECTORY)
	{
		/* Might be an application directory - better check... */
		g_string_append(path, "/AppRun");
		if (!stat(path->str, &info))
		{
			item->flags |= ITEM_FLAG_APPDIR;
		}
	}

	if (item->flags & ITEM_FLAG_APPDIR)	/* path still ends /AppRun */
	{
		MaskedPixmap *app_icon;
		
		g_string_truncate(path, path->len - 3);
		g_string_append(path, "Icon.xpm");
		app_icon = load_pixmap_from(filer_window->window, path->str);
		if (app_icon)
		{
			item->image = app_icon;
			item->flags |= ITEM_FLAG_TEMP_ICON;
		}
		else
			item->image = default_pixmap + TYPE_APPDIR;
	}
	else
	{
		if (base_type == TYPE_FILE &&
				(info.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)))
		{
			item->image = default_pixmap + TYPE_EXEC_FILE;
			item->flags |= ITEM_FLAG_EXEC_FILE;
		}
		else if (base_type == TYPE_FILE)
		{
			item->image = type_to_icon(filer_window->window, 
						   item->mime_type);
		}
		else
			item->image = default_pixmap + base_type;
	}

	item->text_width = gdk_string_width(filer_window->window->style->font,
			leafname);
	
	/* XXX: Must be a better way... */
	item->pix_width = ((GdkPixmapPrivate *) item->image->pixmap)->width;
	item->pix_height = ((GdkPixmapPrivate *) item->image->pixmap)->height;

	item_width = MAX(item->pix_width, item->text_width) + 4;

	if (item_width > filer_window->scan_min_width)
		filer_window->scan_min_width = item_width;

	if (item_width > filer_window->collection->item_width)
		collection_set_item_size(filer_window->collection,
					 item_width,
					 filer_window->collection->item_height);

	collection_insert(filer_window->collection, item);
}

static gboolean test_point(Collection *collection,
				int point_x, int point_y,
				CollectionItem *colitem,
				int width, int height)
{
	FileItem	*item = (FileItem *) colitem->data;
	GdkFont		*font = GTK_WIDGET(collection)->style->font;
	int		text_height = font->ascent + font->descent;
	int		x_off = ABS(point_x - (width >> 1));
	int		image_y = MAX(0, MAX_ICON_HEIGHT - item->pix_height);
	
	if (x_off <= (item->pix_width >> 1) + 2 &&
		point_y >= image_y && point_y <= image_y + item->pix_height)
		return TRUE;
	
	if (x_off <= (item->text_width >> 1) + 2 &&
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
		int	image_y;
		
		gdk_gc_set_clip_mask(gc, item->image->mask);

		image_y = MAX(0, MAX_ICON_HEIGHT - item->pix_height);
		gdk_gc_set_clip_origin(gc, image_x, area->y + image_y);
		gdk_draw_pixmap(widget->window, gc,
				item->image->pixmap,
				0, 0,			/* Source x,y */
				image_x, area->y + image_y, /* Dest x,y */
				-1, MIN(item->pix_height, MAX_ICON_HEIGHT));

		if (item->flags & ITEM_FLAG_SYMLINK)
		{
			gdk_gc_set_clip_origin(gc, image_x, area->y + 8);
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
			gdk_gc_set_clip_origin(gc, image_x, area->y + 8);
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
	show_filer_menu((FilerWindow *) user_data, event, item);
}

static void may_rescan(FilerWindow *filer_window)
{
	struct stat info;

	g_return_if_fail(filer_window != NULL);
	
	if (stat(filer_window->path, &info))
	{
		delayed_error("ROX-Filer", "Directory deleted");
		gtk_widget_destroy(filer_window->window);
	}
	else if (info.st_mtime > filer_window->m_time)
	{
		if (filer_window->dir)
			filer_window->flags |= FILER_NEEDS_RESCAN;
		else
			scan_dir(filer_window);
	}
}

void scan_dir(FilerWindow *filer_window)
{
	struct stat info;

	if (filer_window->dir)
		stop_scanning(filer_window);
	if (panel_with_timeout == filer_window)
	{
		panel_with_timeout = NULL;
		gtk_timeout_remove(panel_timeout);
	}

	mount_update();
	
	free_temp_icons(filer_window);
	collection_clear(filer_window->collection);
	gtk_window_set_title(GTK_WINDOW(filer_window->window),
			filer_window->path);

	if (stat(filer_window->path, &info))
	{
		report_error("Error statting directory", g_strerror(errno));
		return;
	}
	filer_window->m_time = info.st_mtime;

	filer_window->dir = opendir(filer_window->path);
	if (!filer_window->dir)
	{
		report_error("Error scanning directory", g_strerror(errno));
		return;
	}

	filer_window->scan_min_width = 64;

	filer_window->idle_scan_id = gtk_idle_add(idle_scan_dir, filer_window);
}

/* Another app has grabbed the selection */
static gint collection_lose_selection(GtkWidget *widget,
				      GdkEventSelection *event)
{
	if (window_with_selection &&
			window_with_selection->collection == COLLECTION(widget))
	{
		FilerWindow *filer_window = window_with_selection;
		window_with_selection = NULL;
		collection_clear_selection(filer_window->collection);
	}

	return TRUE;
}

/* Someone wants us to send them the selection */
static void selection_get(GtkWidget *widget, 
		       GtkSelectionData *selection_data,
		       guint      info,
		       guint      time,
		       gpointer   data)
{
	GString	*reply, *header;
	FilerWindow 	*filer_window;
	int		i;
	Collection	*collection;

	filer_window = gtk_object_get_data(GTK_OBJECT(widget), "filer_window");

	reply = g_string_new(NULL);
	header = g_string_new(NULL);

	switch (info)
	{
		case TARGET_STRING:
			g_string_sprintf(header, " %s",
					make_path(filer_window->path, "")->str);
			break;
		case TARGET_URI_LIST:
			g_string_sprintf(header, " file://%s%s",
					our_host_name(),
					make_path(filer_window->path, "")->str);
			break;
	}

	collection = filer_window->collection;
	for (i = 0; i < collection->number_of_items; i++)
	{
		if (collection->items[i].selected)
		{
			FileItem *item =
				(FileItem *) collection->items[i].data;
			
			g_string_append(reply, header->str);
			g_string_append(reply, item->leafname);
		}
	}
	g_string_append_c(reply, ' ');
	
	gtk_selection_data_set(selection_data, xa_string,
			8, reply->str + 1, reply->len - 1);
	g_string_free(reply, TRUE);
	g_string_free(header, TRUE);
}

/* No items are now selected. This might be because another app claimed
 * the selection or because the user unselected all the items.
 */
static void lose_selection(Collection 	*collection,
			   guint	time,
			   gpointer 	user_data)
{
	FilerWindow *filer_window = (FilerWindow *) user_data;

	if (window_with_selection == filer_window)
	{
		window_with_selection = NULL;
		gtk_selection_owner_set(NULL,
				GDK_SELECTION_PRIMARY,
				time);
	}
}

static void gain_selection(Collection 	*collection,
			   guint	time,
			   gpointer 	user_data)
{
	FilerWindow *filer_window = (FilerWindow *) user_data;

	if (gtk_selection_owner_set(GTK_WIDGET(collection),
				GDK_SELECTION_PRIMARY,
				time))
	{
		window_with_selection = filer_window;
	}
	else
		collection_clear_selection(filer_window->collection);
}

static int sort_by_name(const void *item1, const void *item2)
{
	return strcmp((*((FileItem **)item1))->leafname,
		      (*((FileItem **)item2))->leafname);
}

static gint clear_panel_hilight(gpointer data)
{
	collection_set_cursor_item(panel_with_timeout->collection, -1);
	panel_with_timeout = NULL;

	return FALSE;
}

/* It is possible to highlight an item briefly on a panel by calling this
 * function.
 */
void panel_set_timeout(FilerWindow *filer_window, gulong msec)
{
	if (panel_with_timeout)
	{
		/* Can't have two timeouts at once */
		gtk_timeout_remove(panel_timeout);
		clear_panel_hilight(NULL);
	}

	if (filer_window)
	{
		panel_with_timeout = filer_window;
		panel_timeout = gtk_timeout_add(msec,
					clear_panel_hilight, NULL);
	}
}

void open_item(Collection *collection,
		gpointer item_data, int item_number,
		gpointer user_data)
{
	FilerWindow	*filer_window = (FilerWindow *) user_data;
	FileItem	*item = (FileItem *) item_data;
	GdkEventButton 	*event;
	char		*full_path;
	GtkWidget	*widget;
	gboolean	shift, adjust;

	event = (GdkEventButton *) gtk_get_current_event();
	full_path = make_path(filer_window->path, item->leafname)->str;

	if (filer_window->panel)
	{
		panel_set_timeout(NULL, 0);
		collection_set_cursor_item(collection, item_number);
		gdk_flush();
		panel_set_timeout(filer_window, 200);
	}

	if (event->type == GDK_2BUTTON_PRESS || event->type == GDK_BUTTON_PRESS)
	{
		shift = event->state & GDK_SHIFT_MASK;
		adjust = event->button != 1 || event->state & GDK_CONTROL_MASK;
	}
	else
	{
		shift = FALSE;
		adjust = FALSE;
	}

	widget = filer_window->window;

	switch (item->base_type)
	{
		case TYPE_DIRECTORY:
			if (item->flags & ITEM_FLAG_APPDIR && !shift)
			{
				run_app(make_path(filer_window->path,
							item->leafname)->str);
				if (adjust && !filer_window->panel)
					gtk_widget_destroy(widget);
				break;
			}
			if (adjust || filer_window->panel)
				filer_opendir(full_path, FALSE, BOTTOM);
			else
			{
				remove_view(filer_window);
				filer_window->path = pathdup(full_path);
				add_view(filer_window);
				scan_dir(filer_window);
			}
			break;
		case TYPE_FILE:
			if (item->flags & ITEM_FLAG_EXEC_FILE)
			{
				char	*argv[] = {full_path, NULL};

				if (spawn_full(argv, getenv("HOME"), 0))
				{
					if (adjust && !filer_window->panel)
						gtk_widget_destroy(widget);
				}
				else
					report_error("ROX-Filer",
						"Failed to fork() child");
			}
			else
			{
				GString		*message;
				MIME_type	*type = item->mime_type;

				if (type && type_open(full_path, type))
				{
					if (adjust && !filer_window->panel)
						gtk_widget_destroy(widget);
				}
				else
				{
					message = g_string_new(NULL);
					g_string_sprintf(message, "No open "
						"action specified for files of "
						"this type (%s/%s)",
						type->media_type,
						type->subtype);
					report_error("ROX-Filer", message->str);
					g_string_free(message, TRUE);
				}
			}
			break;
		default:
			report_error("open_item",
					"I don't know how to open that");
			break;
	}
}

static gint pointer_in(GtkWidget *widget,
			GdkEventCrossing *event,
			FilerWindow *filer_window)
{
	may_rescan(filer_window);
	return FALSE;
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
			remove_view(filer_window);
			filer_window->path = pathdup(make_path(
						filer_window->path,
						"..")->str);
			add_view(filer_window);
			scan_dir(filer_window);
			return TRUE;
	}

	return FALSE;
}

FileItem *selected_item(Collection *collection)
{
	int	i;
	
	g_return_val_if_fail(collection != NULL, NULL);
	g_return_val_if_fail(IS_COLLECTION(collection), NULL);
	g_return_val_if_fail(collection->number_selected == 1, NULL);

	for (i = 0; i < collection->number_of_items; i++)
		if (collection->items[i].selected)
			return (FileItem *) collection->items[i].data;

	g_warning("selected_item: number_selected is wrong\n");

	return NULL;
}

/* Refresh all windows onto this directory */
void refresh_dirs(char *path)
{
	char 	*real;
	GList	*list, *next;

	real = pathdup(path);
	list = g_hash_table_lookup(path_to_window_list, real);
	g_free(real);

	while (list)
	{
		next = list->next;
		may_rescan((FilerWindow *) list->data);
		list = next;
	}
}

void filer_opendir(char *path, gboolean panel, Side panel_side)
{
	GtkWidget	*hbox, *scrollbar, *collection;
	FilerWindow	*filer_window;
	GtkTargetEntry 	target_table[] =
	{
		{"text/uri-list", 0, TARGET_URI_LIST},
		{"STRING", 0, TARGET_STRING},
	};

	filer_window = g_malloc(sizeof(FilerWindow));
	filer_window->path = pathdup(path);
	filer_window->dir = NULL;	/* Not scanning */
	filer_window->show_hidden = FALSE;
	filer_window->panel = panel;
	filer_window->panel_side = panel_side;
	filer_window->temp_item_selected = FALSE;

	filer_window->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);

	collection = collection_new(NULL);
	gtk_object_set_data(GTK_OBJECT(collection),
			"filer_window", filer_window);
	filer_window->collection = COLLECTION(collection);
	collection_set_item_size(filer_window->collection, 64, 64);
	collection_set_functions(filer_window->collection,
			draw_item, test_point);

	gtk_widget_add_events(filer_window->window, GDK_ENTER_NOTIFY);
	gtk_signal_connect(GTK_OBJECT(filer_window->window),
			"enter-notify-event",
			GTK_SIGNAL_FUNC(pointer_in), filer_window);
	gtk_signal_connect(GTK_OBJECT(filer_window->window), "focus_in_event",
			GTK_SIGNAL_FUNC(focus_in), filer_window);
	gtk_signal_connect(GTK_OBJECT(filer_window->window), "focus_out_event",
			GTK_SIGNAL_FUNC(focus_out), filer_window);
	gtk_signal_connect(GTK_OBJECT(filer_window->window), "destroy",
			filer_window_destroyed, filer_window);

	gtk_signal_connect(GTK_OBJECT(filer_window->collection), "open_item",
			open_item, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "show_menu",
			show_menu, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "gain_selection",
			gain_selection, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "lose_selection",
			lose_selection, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "drag_selection",
			drag_selection, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "drag_data_get",
			drag_data_get, filer_window);
	gtk_signal_connect(GTK_OBJECT(collection), "selection_clear_event",
			GTK_SIGNAL_FUNC(collection_lose_selection), NULL);
	gtk_signal_connect (GTK_OBJECT(collection), "selection_get",
			GTK_SIGNAL_FUNC(selection_get), NULL);
	gtk_selection_add_targets(collection, GDK_SELECTION_PRIMARY,
			target_table,
			sizeof(target_table) / sizeof(*target_table));

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
			int	height = iheight + PANEL_BORDER;
			int	y = panel_side == TOP 
					? -PANEL_BORDER
					: sheight - height - PANEL_BORDER;

			gtk_widget_set_usize(collection, swidth, height);
			gtk_widget_set_uposition(win, 0, y);
		}
		else
		{
			int	width = iwidth + PANEL_BORDER;
			int	x = panel_side == LEFT
					? -PANEL_BORDER
					: swidth - width - PANEL_BORDER;

			gtk_widget_set_usize(collection, width, sheight);
			gtk_widget_set_uposition(win, x, 0);
		}

		frame = gtk_frame_new(NULL);
		gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_OUT);
		gtk_container_add(GTK_CONTAINER(frame), collection);
		gtk_container_add(GTK_CONTAINER(win), frame);

		gtk_widget_realize(win);
		make_panel_window(win->window);
	}
	else
	{
		gtk_signal_connect(GTK_OBJECT(filer_window->window),
				"key_press_event",
				GTK_SIGNAL_FUNC(key_press_event), filer_window);
		gtk_window_set_default_size(GTK_WINDOW(filer_window->window),
					400, 200);

		hbox = gtk_hbox_new(FALSE, 0);
		gtk_container_add(GTK_CONTAINER(filer_window->window), hbox);
		
		gtk_box_pack_start(GTK_BOX(hbox), collection, TRUE, TRUE, 0);

		scrollbar = gtk_vscrollbar_new(COLLECTION(collection)->vadj);
		gtk_box_pack_start(GTK_BOX(hbox), scrollbar, FALSE, TRUE, 0);
		gtk_accel_group_attach(filer_keys,
				GTK_OBJECT(filer_window->window));
	}

	gtk_widget_show_all(filer_window->window);
	number_of_windows++;

	load_default_pixmaps(collection->window);

	add_view(filer_window);
	scan_dir(filer_window);
}
