/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2002, the ROX-Filer team.
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

/* view_details.c - display a list of files in a TreeView */

#include "config.h"

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "global.h"

#include "view_iface.h"
#include "view_details.h"
#include "diritem.h"
#include "support.h"
#include "type.h"
#include "filer.h"
#include "display.h"
#include "pixmaps.h"
#include "dnd.h"
#include "bind.h"
#include "gui_support.h"
#include "menu.h"
#include "options.h"

/* These are the column numbers in the ListStore */
#define COL_LEAF 0
#define COL_TYPE 1
#define COL_PERM 2
#define COL_OWNER 3
#define COL_GROUP 4
#define COL_SIZE 5
#define COL_MTIME 6
#define COL_ITEM 7
#define COL_COLOUR 8
#define COL_ICON 9
#define COL_BG_COLOUR 10
#define N_COLUMNS 11

static gpointer parent_class = NULL;

struct _ViewDetailsClass {
	GtkTreeViewClass parent;
};

typedef struct _ViewItem ViewItem;

struct _ViewItem {
	DirItem *item;
	GdkPixbuf *image;
	int	old_pos;	/* Used while sorting */
	gboolean selected;
};

typedef struct _ViewDetails ViewDetails;

struct _ViewDetails {
	GtkTreeView treeview;

	FilerWindow *filer_window;	/* Used for styles, etc */

	GPtrArray   *items;		/* ViewItem */
	
	gint	    sort_column_id;
	GtkSortType order;
	int	    (*sort_fn)(const void *, const void *);

	int	    cursor_base;	/* Cursor when minibuffer opened */
};

/* Static prototypes */
static void view_details_finialize(GObject *object);
static void view_details_class_init(gpointer gclass, gpointer data);
static void view_details_init(GTypeInstance *object, gpointer gclass);

static void view_details_iface_init(gpointer giface, gpointer iface_data);

static void view_details_sort(ViewIface *view);
static void view_details_style_changed(ViewIface *view, int flags);
static gboolean view_details_autoselect(ViewIface *view, const gchar *leaf);
static void view_details_add_items(ViewIface *view, GPtrArray *items);
static void view_details_update_items(ViewIface *view, GPtrArray *items);
static void view_details_delete_if(ViewIface *view,
			  gboolean (*test)(gpointer item, gpointer data),
			  gpointer data);
static void view_details_clear(ViewIface *view);
static void view_details_select_all(ViewIface *view);
static void view_details_clear_selection(ViewIface *view);
static int view_details_count_items(ViewIface *view);
static int view_details_count_selected(ViewIface *view);
static void view_details_show_cursor(ViewIface *view);
static void view_details_get_iter(ViewIface *view,
				     ViewIter *iter, IterFlags flags);
static void view_details_cursor_to_iter(ViewIface *view, ViewIter *iter);
static void view_details_set_selected(ViewIface *view,
					 ViewIter *iter,
					 gboolean selected);
static gboolean view_details_get_selected(ViewIface *view, ViewIter *iter);
static void view_details_select_only(ViewIface *view, ViewIter *iter);
static void view_details_set_frozen(ViewIface *view, gboolean frozen);
static void view_details_wink_item(ViewIface *view, ViewIter *iter);
static void view_details_autosize(ViewIface *view);
static gboolean view_details_cursor_visible(ViewIface *view);
static void view_details_set_base(ViewIface *view, ViewIter *iter);
static DirItem *iter_peek(ViewIter *iter);
static DirItem *iter_prev(ViewIter *iter);
static DirItem *iter_next(ViewIter *iter);
static void make_iter(ViewDetails *view_details, ViewIter *iter,
		      IterFlags flags);
static void make_item_iter(ViewDetails *view_details, ViewIter *iter, int i);
static void view_details_tree_model_init(GtkTreeModelIface *iface);
static gboolean details_get_sort_column_id(GtkTreeSortable *sortable,
					   gint            *sort_column_id,
					   GtkSortType     *order);
static void details_set_sort_column_id(GtkTreeSortable     *sortable,
				       gint                sort_column_id,
				       GtkSortType         order);
static void details_set_sort_func(GtkTreeSortable          *sortable,
			          gint                    sort_column_id,
			          GtkTreeIterCompareFunc  func,
			          gpointer                data,
			          GtkDestroyNotify        destroy);
static void details_set_default_sort_func(GtkTreeSortable        *sortable,
				          GtkTreeIterCompareFunc  func,
				          gpointer                data,
				          GtkDestroyNotify        destroy);
static gboolean details_has_default_sort_func(GtkTreeSortable *sortable);
static void view_details_sortable_init(GtkTreeSortableIface *iface);
static void set_selected(ViewDetails *view_details, int i, gboolean selected);
static gboolean get_selected(ViewDetails *view_details, int i);


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

GtkWidget *view_details_new(FilerWindow *filer_window)
{
	ViewDetails *view_details;

	view_details = g_object_new(view_details_get_type(), NULL);
	view_details->filer_window = filer_window;

	gtk_range_set_adjustment(GTK_RANGE(filer_window->scrollbar),
		gtk_tree_view_get_vadjustment(GTK_TREE_VIEW(view_details)));

	return GTK_WIDGET(view_details);
}

GType view_details_get_type(void)
{
	static GType type = 0;

	if (!type)
	{
		static const GTypeInfo info =
		{
			sizeof (ViewDetailsClass),
			NULL,			/* base_init */
			NULL,			/* base_finalise */
			view_details_class_init,
			NULL,			/* class_finalise */
			NULL,			/* class_data */
			sizeof(ViewDetails),
			0,			/* n_preallocs */
			view_details_init
		};
		static const GInterfaceInfo view_iface_info = {
			view_details_iface_init,
			NULL, NULL
		};
		static const GInterfaceInfo tree_model_info = {
			(GInterfaceInitFunc) view_details_tree_model_init,
			NULL, NULL
		};
		static const GInterfaceInfo sortable_info = {
			(GInterfaceInitFunc) view_details_sortable_init,
			NULL, NULL
		};


		type = g_type_register_static(gtk_tree_view_get_type(),
						"ViewDetails", &info, 0);

		g_type_add_interface_static(type, VIEW_TYPE_IFACE,
				&view_iface_info);
		g_type_add_interface_static(type, GTK_TYPE_TREE_MODEL,
				&tree_model_info);
		g_type_add_interface_static(type, GTK_TYPE_TREE_SORTABLE,
				&sortable_info);
	}

	return type;
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

/* Fulfill the GtkTreeModel requirements */
static guint details_get_flags(GtkTreeModel *tree_model)
{
	return GTK_TREE_MODEL_LIST_ONLY;
}

static gint details_get_n_columns(GtkTreeModel *tree_model)
{
	return N_COLUMNS;
}

static GType details_get_column_type(GtkTreeModel *tree_model, gint index)
{
	g_return_val_if_fail(index < N_COLUMNS && index >= 0, G_TYPE_INVALID);

	if (index == COL_COLOUR || index == COL_BG_COLOUR)
		return GDK_TYPE_COLOR;
	else if (index == COL_ITEM)
		return G_TYPE_POINTER;
	else if (index == COL_ICON)
		return GDK_TYPE_PIXBUF;
	return G_TYPE_STRING;
}

static gboolean details_get_iter(GtkTreeModel *tree_model,
				 GtkTreeIter  *iter,
				 GtkTreePath  *path)
{
	ViewDetails *view_details = (ViewDetails *) tree_model;
	gint i;

	g_return_val_if_fail(gtk_tree_path_get_depth (path) > 0, FALSE);

	i = gtk_tree_path_get_indices(path)[0];

	if (i >= view_details->items->len)
		return FALSE;

	iter->user_data = GINT_TO_POINTER(i);

	return TRUE;
}

static GtkTreePath *details_get_path(GtkTreeModel *tree_model,
				     GtkTreeIter  *iter)
{
	GtkTreePath *retval;

	retval = gtk_tree_path_new();
	gtk_tree_path_append_index(retval, GPOINTER_TO_INT(iter->user_data));

	return retval;
}

static void details_get_value(GtkTreeModel *tree_model,
			      GtkTreeIter  *iter,
			      gint         column,
			      GValue       *value)
{
	ViewDetails *view_details = (ViewDetails *) tree_model;
	GtkStyle *style = ((GtkWidget *) tree_model)->style;
	gint i;
	GPtrArray *items = view_details->items;
	ViewItem *view_item;
	DirItem *item;
	mode_t m;

	g_return_if_fail(column >= 0 && column < N_COLUMNS);

	i = GPOINTER_TO_INT(iter->user_data);
	g_return_if_fail(i >= 0 && i < items->len);
	view_item = (ViewItem *) items->pdata[i];
	item = view_item->item;

	/* g_print("[ get %d ]\n", column); */

	if (column == COL_LEAF)
	{
		g_value_init(value, G_TYPE_STRING);
		g_value_set_string(value, item->leafname);
		return;
	}
	else if (column == COL_ITEM)
	{
		g_value_init(value, G_TYPE_POINTER);
		g_value_set_pointer(value, item);
		return;
	}

	if (item->base_type == TYPE_UNKNOWN)
	{
		GType type;
		type = details_get_column_type(tree_model, column);
		g_value_init(value, type);
		if (type == G_TYPE_STRING)
			g_value_set_string(value, "");
		else if (type == GDK_TYPE_COLOR)
			g_value_set_boxed(value, NULL);
		else
			g_value_set_object(value, NULL);
			     
		return;
	}
	m = item->mode;

	switch (column)
	{
		case COL_LEAF:
			g_value_init(value, G_TYPE_STRING);
			g_value_set_string(value, item->leafname);
			break;
		case COL_ICON:
			g_value_init(value, GDK_TYPE_PIXBUF);
			if (!item->image->sm_pixbuf)
				pixmap_make_small(item->image);
			g_value_set_object(value, item->image->sm_pixbuf);
			break;
		case COL_COLOUR:
			g_value_init(value, GDK_TYPE_COLOR);
			g_value_set_boxed(value, type_get_colour(item, NULL));
			break;
		case COL_BG_COLOUR:
			g_value_init(value, GDK_TYPE_COLOR);
			g_value_set_boxed(value, view_item->selected
					? &style->base[GTK_STATE_SELECTED]
					: &style->base[GTK_STATE_NORMAL]);
			break;
		case COL_OWNER:
			g_value_init(value, G_TYPE_STRING);
			g_value_set_string(value, user_name(item->uid));
			break;
		case COL_GROUP:
			g_value_init(value, G_TYPE_STRING);
			g_value_set_string(value, group_name(item->gid));
			break;
		case COL_MTIME:
		{
			gchar *time;
			time = pretty_time(&item->mtime);
			g_value_init(value, G_TYPE_STRING);
			g_value_set_string(value, time);
			g_free(time);
			break;
		}
		case COL_PERM:
			g_value_init(value, G_TYPE_STRING);
			g_value_set_string(value, pretty_permissions(m));
			break;
		case COL_SIZE:
			g_value_init(value, G_TYPE_STRING);
			if (item->base_type != TYPE_DIRECTORY)
				g_value_set_string(value,
						   format_size(item->size));
			break;
		case COL_TYPE:
			g_value_init(value, G_TYPE_STRING);
			g_value_set_string(value, 
				item->flags & ITEM_FLAG_APPDIR? "App" :
				S_ISDIR(m) ? "Dir" :
				S_ISCHR(m) ? "Char" :
				S_ISBLK(m) ? "Blck" :
				S_ISLNK(m) ? "Link" :
				S_ISSOCK(m) ? "Sock" :
				S_ISFIFO(m) ? "Pipe" :
				S_ISDOOR(m) ? "Door" :
				"File");
			break;
		default:
			g_value_init(value, G_TYPE_STRING);
			g_value_set_string(value, "Hello");
			break;
	}
}

static gboolean details_iter_next(GtkTreeModel *tree_model, GtkTreeIter *iter)
{
	ViewDetails *view_details = (ViewDetails *) tree_model;
	int i;

	i = GPOINTER_TO_INT(iter->user_data) + 1;
	iter->user_data = GINT_TO_POINTER(i);

	return i < view_details->items->len;
}

static gboolean details_iter_children(GtkTreeModel *tree_model,
				      GtkTreeIter  *iter,
				      GtkTreeIter  *parent)
{
	ViewDetails *view_details = (ViewDetails *) tree_model;

	/* this is a list, nodes have no children */
	if (parent)
		return FALSE;

	/* but if parent == NULL we return the list itself as children of the
	 * "root"
	 */

	if (view_details->items->len)
	{
		iter->user_data = GINT_TO_POINTER(0);
		return TRUE;
	}
	else
		return FALSE;
}

static gboolean details_iter_has_child(GtkTreeModel *tree_model,
				       GtkTreeIter  *iter)
{
	return FALSE;
}

static gint details_iter_n_children(GtkTreeModel *tree_model, GtkTreeIter *iter)
{
	ViewDetails *view_details = (ViewDetails *) tree_model;

	if (iter == NULL)
		return view_details->items->len;

	return 0;
}

static gboolean details_iter_nth_child(GtkTreeModel *tree_model,
				       GtkTreeIter  *iter,
				       GtkTreeIter  *parent,
				       gint          n)
{
	ViewDetails *view_details = (ViewDetails *) tree_model;

	if (parent)
		return FALSE;

	if (n >= 0 && n < view_details->items->len)
	{
		iter->user_data = GINT_TO_POINTER(n);
		return TRUE;
	}
	else
		return FALSE;
}

static gboolean details_iter_parent(GtkTreeModel *tree_model,
				    GtkTreeIter  *iter,
				    GtkTreeIter  *child)
{
	return FALSE;
}

/* A ViewDetails is both a GtkTreeView and a GtkTreeModel.
 * The following functions implement the model interface...
 */

static void view_details_tree_model_init(GtkTreeModelIface *iface)
{
	iface->get_flags = details_get_flags;
	iface->get_n_columns = details_get_n_columns;
	iface->get_column_type = details_get_column_type;
	iface->get_iter = details_get_iter;
	iface->get_path = details_get_path;
	iface->get_value = details_get_value;
	iface->iter_next = details_iter_next;
	iface->iter_children = details_iter_children;
	iface->iter_has_child = details_iter_has_child;
	iface->iter_n_children = details_iter_n_children;
	iface->iter_nth_child = details_iter_nth_child;
	iface->iter_parent = details_iter_parent;
}

static void view_details_sortable_init(GtkTreeSortableIface *iface)
{
	iface->get_sort_column_id = details_get_sort_column_id;
	iface->set_sort_column_id = details_set_sort_column_id;
	iface->set_sort_func = details_set_sort_func;
	iface->set_default_sort_func = details_set_default_sort_func;
	iface->has_default_sort_func = details_has_default_sort_func;
}

static gboolean details_get_sort_column_id(GtkTreeSortable *sortable,
					   gint            *sort_column_id,
					   GtkSortType     *order)
{
	ViewDetails *view_details = (ViewDetails *) sortable;

	if (view_details->sort_column_id == -1)
		return FALSE;

	if (sort_column_id)
		*sort_column_id = view_details->sort_column_id;
	if (order)
		*order = view_details->order;
	return TRUE;
}

static void details_set_sort_column_id(GtkTreeSortable     *sortable,
				       gint                sort_column_id,
				       GtkSortType         order)
{
	ViewDetails *view_details = (ViewDetails *) sortable;

	if (view_details->sort_column_id == sort_column_id &&
	    view_details->order == order)
		return;

	view_details->sort_column_id = sort_column_id;
	view_details->order = order;

	switch (sort_column_id)
	{
		case COL_LEAF:
			view_details->sort_fn = sort_by_name;
			break;
		case COL_SIZE:
			view_details->sort_fn = sort_by_size;
			break;
		case COL_MTIME:
			view_details->sort_fn = sort_by_date;
			break;
		case COL_TYPE:
			view_details->sort_fn = sort_by_type;
			break;
		case COL_OWNER:
			view_details->sort_fn = sort_by_owner;
			break;
		case COL_GROUP:
			view_details->sort_fn = sort_by_group;
			break;
		default:
			g_assert_not_reached();
	}

	view_details_sort((ViewIface *) view_details);

	gtk_tree_sortable_sort_column_changed(sortable);
}

static void details_set_sort_func(GtkTreeSortable          *sortable,
			          gint                    sort_column_id,
			          GtkTreeIterCompareFunc  func,
			          gpointer                data,
			          GtkDestroyNotify        destroy)
{
	g_assert_not_reached();
}

static void details_set_default_sort_func(GtkTreeSortable        *sortable,
				          GtkTreeIterCompareFunc  func,
				          gpointer                data,
				          GtkDestroyNotify        destroy)
{
	g_assert_not_reached();
}

static gboolean details_has_default_sort_func(GtkTreeSortable *sortable)
{
	return FALSE;
}


/* End of model implementation */

static gboolean is_selected(ViewDetails *view_details, int i)
{
	ViewIter iter;
	iter.i = i;
	return view_details_get_selected((ViewIface *) view_details, &iter);
}

static void perform_action(ViewDetails *view_details, GdkEventButton *event)
{
	BindAction	action;
	FilerWindow	*filer_window = view_details->filer_window;
	ViewIface	*view = (ViewIface *) view_details;
	DirItem		*item = NULL;
	GtkTreeView	*tree = (GtkTreeView *) view_details;
	GtkTreePath	*path = NULL;
	GtkTreeIter	iter;
	GtkTreeModel	*model;
	gboolean	press = event->type == GDK_BUTTON_PRESS;
	int		i = -1;
	ViewIter	viter;
	OpenFlags	flags = 0;

	model = gtk_tree_view_get_model(tree);

	if (gtk_tree_view_get_path_at_pos(tree, event->x, event->y,
					&path, NULL, NULL, NULL))
	{
		g_return_if_fail(path != NULL);

		i = gtk_tree_path_get_indices(path)[0];
		gtk_tree_path_free(path);
	}

	if (i != -1)
		item = ((ViewItem *) view_details->items->pdata[i])->item;
	make_item_iter(view_details, &viter, i);
	iter.user_data = GINT_TO_POINTER(i);

	/* TODO: Cancel slow DnD */

	if (filer_window->target_cb)
	{
		dnd_motion_ungrab();
		if (item && press && event->button == 1)
			filer_window->target_cb(filer_window, &viter,
					filer_window->target_data);

		filer_target_mode(filer_window, NULL, NULL, NULL);

		return;
	}

	action = bind_lookup_bev(
			item ? BIND_DIRECTORY_ICON : BIND_DIRECTORY,
			event);

	switch (action)
	{
		case ACT_CLEAR_SELECTION:
			view_details_clear_selection(view);
			break;
		case ACT_TOGGLE_SELECTED:
			set_selected(view_details, i,
					!get_selected(view_details, i));
			break;
		case ACT_SELECT_EXCL:
			view_details_select_only(view, &viter);
			break;
		case ACT_EDIT_ITEM:
			flags |= OPEN_SHIFT;
			/* (no break) */
		case ACT_OPEN_ITEM:
			if (event->button != 1 || event->state & GDK_MOD1_MASK)
				flags |= OPEN_CLOSE_WINDOW;
			else
				flags |= OPEN_SAME_WINDOW;
			if (o_new_button_1.int_value)
				flags ^= OPEN_SAME_WINDOW;
			if (event->type == GDK_2BUTTON_PRESS)
				view_details_set_selected(view, &viter, FALSE);
			dnd_motion_ungrab();

			filer_openitem(filer_window, &viter, flags);
			break;
		case ACT_POPUP_MENU:
			
			dnd_motion_ungrab();
			tooltip_show(NULL);

			show_filer_menu(filer_window,
					(GdkEvent *) event, &viter);
			break;
		case ACT_PRIME_AND_SELECT:
			if (item && !is_selected(view_details, i))
				view_details_select_only(view, &viter);
			dnd_motion_start(MOTION_READY_FOR_DND);
			break;
		case ACT_PRIME_AND_TOGGLE:
			set_selected(view_details, i,
					!get_selected(view_details, i));
			dnd_motion_start(MOTION_READY_FOR_DND);
			break;
		case ACT_PRIME_FOR_DND:
			dnd_motion_start(MOTION_READY_FOR_DND);
			break;
		case ACT_IGNORE:
			if (press && event->button < 4)
			{
				if (item)
					view_details_wink_item(view, &viter);
				dnd_motion_start(MOTION_NONE);
			}
			break;
		case ACT_LASSO_CLEAR:
			view_details_clear_selection(view);
			/* (no break) */
		case ACT_LASSO_MODIFY:
#if 0
			collection_lasso_box(collection, event->x, event->y);
#endif
			break;
		case ACT_RESIZE:
			filer_window_autosize(filer_window);
			break;
		default:
			g_warning("Unsupported action : %d\n", action);
			break;
	}
}

static gboolean view_details_button_press(GtkWidget *widget,
					  GdkEventButton *bev)
{
	if (dnd_motion_press(widget, bev))
		perform_action((ViewDetails *) widget, bev);

	return TRUE;
}

static gboolean view_details_button_release(GtkWidget *widget,
					    GdkEventButton *bev)
{
	if (!dnd_motion_release(bev))
		perform_action((ViewDetails *) widget, bev);

	return TRUE;
}

static gint view_details_key_press_event(GtkWidget *widget, GdkEventKey *event)
{
	if (event->keyval == ' ')
	{
		ViewDetails *view_details = (ViewDetails *) widget;
		FilerWindow *filer_window = view_details->filer_window;

		filer_window_toggle_cursor_item_selected(filer_window);
		return TRUE;
	}

	return GTK_WIDGET_CLASS(parent_class)->key_press_event(widget, event);
}

static void view_details_destroy(GtkObject *view_details)
{
	VIEW_DETAILS(view_details)->filer_window = NULL;
}

static void view_details_finialize(GObject *object)
{
	ViewDetails *view_details = (ViewDetails *) object;

	g_ptr_array_free(view_details->items, TRUE);
	view_details->items = NULL;

	G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void view_details_class_init(gpointer gclass, gpointer data)
{
	GObjectClass *object = (GObjectClass *) gclass;
	GtkWidgetClass *widget = (GtkWidgetClass *) gclass;

	parent_class = g_type_class_peek_parent(gclass);

	object->finalize = view_details_finialize;
	GTK_OBJECT_CLASS(object)->destroy = view_details_destroy;

	widget->button_press_event = view_details_button_press;
	widget->button_release_event = view_details_button_release;
	widget->key_press_event = view_details_key_press_event;
}

static void view_details_init(GTypeInstance *object, gpointer gclass)
{
	GtkTreeView *treeview = (GtkTreeView *) object;
	GtkTreeViewColumn *column;
	GtkCellRenderer *cell;
	GtkTreeSortable *sortable_list;
	GtkTreeSelection *selection;
	ViewDetails *view_details = (ViewDetails *) object;

	view_details->items = g_ptr_array_new();
	view_details->cursor_base = -1;

	selection = gtk_tree_view_get_selection(treeview);
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_NONE);

	/* Sorting */
	view_details->sort_column_id = -1;
	view_details->sort_fn = NULL;
	sortable_list = GTK_TREE_SORTABLE(object);
	gtk_tree_sortable_set_sort_column_id(sortable_list, COL_LEAF,
			GTK_SORT_ASCENDING);

	gtk_tree_view_set_model(treeview, GTK_TREE_MODEL(view_details));

	/* Icon */
	cell = gtk_cell_renderer_pixbuf_new();
	column = gtk_tree_view_column_new_with_attributes(NULL, cell,
					    "pixbuf", COL_ICON, NULL);
	gtk_tree_view_append_column(treeview, column);

	/* Name */
	cell = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(_("Name"), cell,
					    "text", COL_LEAF,
					    "foreground-gdk", COL_COLOUR,
					    "background-gdk", COL_BG_COLOUR,
					    NULL);
	gtk_tree_view_append_column(treeview, column);
	gtk_tree_view_column_set_sort_column_id(column, COL_LEAF);

	/* Type */
	cell = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(_("Type"), cell,
					    "text", COL_TYPE,
					    "foreground-gdk", COL_COLOUR,
					    "background-gdk", COL_BG_COLOUR,
					    NULL);
	gtk_tree_view_append_column(treeview, column);
	gtk_tree_view_column_set_sort_column_id(column, COL_TYPE);

	/* Perm */
	cell = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(_("Permissions"),
					cell, "text", COL_PERM,
					"foreground-gdk", COL_COLOUR,
					"background-gdk", COL_BG_COLOUR,
					NULL);
	gtk_tree_view_append_column(treeview, column);

	/* Owner */
	cell = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(_("Owner"), cell,
					    "text", COL_OWNER,
					    "foreground-gdk", COL_COLOUR,
					    "background-gdk", COL_BG_COLOUR,
					    NULL);
	gtk_tree_view_append_column(treeview, column);
	gtk_tree_view_column_set_sort_column_id(column, COL_OWNER);

	/* Group */
	cell = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(_("Group"), cell,
					    "text", COL_GROUP,
					    "foreground-gdk", COL_COLOUR,
					    "background-gdk", COL_BG_COLOUR,
					    NULL);
	gtk_tree_view_append_column(treeview, column);
	gtk_tree_view_column_set_sort_column_id(column, COL_GROUP);

	/* Size */
	cell = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(_("Size"), cell,
					    "text", COL_SIZE,
					    "foreground-gdk", COL_COLOUR,
					    "background-gdk", COL_BG_COLOUR,
					    NULL);
	gtk_tree_view_append_column(treeview, column);
	gtk_tree_view_column_set_sort_column_id(column, COL_SIZE);

	/* MTime */
	cell = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes(_("M-Time"), cell,
					    "text", COL_MTIME,
					    "foreground-gdk", COL_COLOUR,
					    "background-gdk", COL_BG_COLOUR,
					    NULL);
	gtk_tree_view_append_column(treeview, column);
	gtk_tree_view_column_set_sort_column_id(column, COL_MTIME);

	gtk_widget_set_size_request(GTK_WIDGET(treeview), -1, 50);
}

/* Create the handers for the View interface */
static void view_details_iface_init(gpointer giface, gpointer iface_data)
{
	ViewIfaceClass *iface = giface;

	g_assert(G_TYPE_FROM_INTERFACE(iface) == VIEW_TYPE_IFACE);

	/* override stuff */
	iface->sort = view_details_sort;
	iface->style_changed = view_details_style_changed;
	iface->autoselect = view_details_autoselect;
	iface->add_items = view_details_add_items;
	iface->update_items = view_details_update_items;
	iface->delete_if = view_details_delete_if;
	iface->clear = view_details_clear;
	iface->select_all = view_details_select_all;
	iface->clear_selection = view_details_clear_selection;
	iface->count_items = view_details_count_items;
	iface->count_selected = view_details_count_selected;
	iface->show_cursor = view_details_show_cursor;
	iface->get_iter = view_details_get_iter;
	iface->cursor_to_iter = view_details_cursor_to_iter;
	iface->set_selected = view_details_set_selected;
	iface->get_selected = view_details_get_selected;
	iface->set_frozen = view_details_set_frozen;
	iface->select_only = view_details_select_only;
	iface->wink_item = view_details_wink_item;
	iface->autosize = view_details_autosize;
	iface->cursor_visible = view_details_cursor_visible;
	iface->set_base = view_details_set_base;
}

/* Implementations of the View interface. See view_iface.c for comments. */

static void view_details_style_changed(ViewIface *view, int flags)
{
}

static gint wrap_sort(gconstpointer a, gconstpointer b,
		      ViewDetails *view_details)
{
	ViewItem *ia = *(ViewItem **) a;
	ViewItem *ib = *(ViewItem **) b;

	if (view_details->order == GTK_SORT_ASCENDING)
		return view_details->sort_fn(ia->item, ib->item);
	else
		return -view_details->sort_fn(ia->item, ib->item);
}

static void view_details_sort(ViewIface *view)
{
	ViewDetails *view_details = (ViewDetails *) view;
	ViewItem **items = (ViewItem **) view_details->items->pdata;
	GArray *new_order;
	gint i, len = view_details->items->len;
	GtkTreePath *path;

	g_return_if_fail(view_details->sort_fn != NULL);

	if (!len)
		return;

	for (i = len - 1; i >= 0; i--)
		items[i]->old_pos = i;
	
	g_ptr_array_sort_with_data(view_details->items,
				   (GCompareDataFunc) wrap_sort,
				   view_details);

	new_order = g_array_sized_new(FALSE, FALSE, sizeof(gint), len);
	g_array_set_size(new_order, len);

	for (i = len - 1; i >= 0; i--)
		g_array_insert_val(new_order, items[i]->old_pos, i);

	path = gtk_tree_path_new();
	gtk_tree_model_rows_reordered((GtkTreeModel *) view,
					path, NULL,
					(gint *) new_order->data);
	gtk_tree_path_free(path);
	g_array_free(new_order, TRUE);
}

static gboolean view_details_autoselect(ViewIface *view, const gchar *leaf)
{
	return FALSE;
}

static void view_details_add_items(ViewIface *view, GPtrArray *new_items)
{
	ViewDetails *view_details = (ViewDetails *) view;
	FilerWindow *filer_window = view_details->filer_window;
	gboolean show_hidden = filer_window->show_hidden;
	GPtrArray *items = view_details->items;
	GtkTreeIter iter;
	int i;
	GtkTreePath *path;
	GtkTreeModel *model = (GtkTreeModel *) view;

	iter.user_data = GINT_TO_POINTER(items->len);
	path = details_get_path(model, &iter);

	for (i = 0; i < new_items->len; i++)
	{
		DirItem *item = (DirItem *) new_items->pdata[i];
		char	*leafname = item->leafname;
		ViewItem *vitem;
	
		if (leafname[0] == '.')
		{
			if (!show_hidden)
				continue;

			if (leafname[1] == '\0')
				continue; /* Never show '.' */

			if (leafname[1] == '.' &&
					leafname[2] == '\0')
				continue; /* Never show '..' */
		}

		vitem = g_new(ViewItem, 1);
		vitem->item = item;
		vitem->image = NULL;
		vitem->selected = FALSE;
		
		g_ptr_array_add(items, vitem);

		iter.user_data = GINT_TO_POINTER(items->len - 1);
		gtk_tree_model_row_inserted(model, path, &iter);
		gtk_tree_path_next(path);
	}

	gtk_tree_path_free(path);

	view_details_sort(view);
}

/* Find an item in the sorted array.
 * Returns the item number, or -1 if not found.
 */
static int details_find_item(ViewDetails *view_details, DirItem *item)
{
	ViewItem **items, tmp, *tmpp;
	int	lower, upper;
	int (*compar)(const void *, const void *);

	g_return_val_if_fail(view_details != NULL, -1);
	g_return_val_if_fail(item != NULL, -1);

	tmp.item = item;
	tmpp = &tmp;

	items = (ViewItem **) view_details->items->pdata;
	compar = view_details->sort_fn;

	g_return_val_if_fail(compar != NULL, -1);

	/* If item is here, then: lower <= i < upper */
	lower = 0;
	upper = view_details->items->len;

	while (lower < upper)
	{
		int	i, cmp;

		i = (lower + upper) >> 1;

		cmp = wrap_sort(&items[i], &tmpp, view_details);
		if (cmp == 0)
			return i;

		if (cmp > 0)
			upper = i;
		else
			lower = i + 1;
	}

	return -1;
}

static void view_details_update_items(ViewIface *view, GPtrArray *items)
{
	ViewDetails	*view_details = (ViewDetails *) view;
	FilerWindow	*filer_window = view_details->filer_window;
	int		i;
	GtkTreeModel	*model = (GtkTreeModel *) view_details;

	g_return_if_fail(items->len > 0);
	
	/* The item data has already been modified, so this gives the
	 * final sort order...
	 */
	view_details_sort(view);

	for (i = 0; i < items->len; i++)
	{
		DirItem *item = (DirItem *) items->pdata[i];
		const gchar *leafname = item->leafname;
		int j;

		if (leafname[0] == '.' && filer_window->show_hidden == FALSE)
			continue;

		j = details_find_item(view_details, item);

		if (j < 0)
			g_warning("Failed to find '%s'\n", leafname);
		else
		{
			GtkTreePath *path;
			GtkTreeIter iter;
			path = gtk_tree_path_new();
			gtk_tree_path_append_index(path, j);
			iter.user_data = GINT_TO_POINTER(j);
			gtk_tree_model_row_changed(model, path, &iter);
		}
	}
}

static void view_details_delete_if(ViewIface *view,
			  gboolean (*test)(gpointer item, gpointer data),
			  gpointer data)
{
	GtkTreePath *path;
	ViewDetails *view_details = (ViewDetails *) view;
	int	    i = 0;
	GPtrArray   *items = view_details->items;
	GtkTreeModel *model = (GtkTreeModel *) view;

	path = gtk_tree_path_new();

	gtk_tree_path_append_index(path, i);

	while (i < items->len)
	{
		ViewItem *item = items->pdata[i];

		if (test(item->item, data))
		{
			g_free(items->pdata[i]);
			g_ptr_array_remove_index(items, i);
			gtk_tree_model_row_deleted(model, path);
		}
		else
		{
			i++;
			gtk_tree_path_next(path);
		}
	}

	gtk_tree_path_free(path);
}

static void view_details_clear(ViewIface *view)
{
	GtkTreePath *path;
	GPtrArray *items = ((ViewDetails *) view)->items;
	GtkTreeModel *model = (GtkTreeModel *) view;

	path = gtk_tree_path_new();
	gtk_tree_path_append_index(path, items->len);

	while (gtk_tree_path_prev(path))
		gtk_tree_model_row_deleted(model, path);

	g_ptr_array_set_size(items, 0);
	gtk_tree_path_free(path);
}

static void view_details_select_all(ViewIface *view)
{
	int i;
	int n = ((ViewDetails *) view)->items->len;

	for (i = 0; i < n; i++)
	{
		ViewIter iter;
		iter.i = i;
		view_details_set_selected(view, &iter, TRUE);
	}
}

static void view_details_clear_selection(ViewIface *view)
{
	int i;
	int n = ((ViewDetails *) view)->items->len;

	for (i = 0; i < n; i++)
	{
		ViewIter iter;
		iter.i = i;
		view_details_set_selected(view, &iter, FALSE);
	}
}

static int view_details_count_items(ViewIface *view)
{
	ViewDetails *view_details = (ViewDetails *) view;

	return view_details->items->len;
}

static int view_details_count_selected(ViewIface *view)
{
	ViewDetails *view_details = (ViewDetails *) view;
	int n = view_details->items->len;
	ViewItem **items = (ViewItem **) view_details->items->pdata;
	int count = 0;
	int i;

	for (i = 0; i < n; i++)
		if (items[i]->selected)
			count++;

	return count;
}

static void view_details_show_cursor(ViewIface *view)
{
}

static void view_details_get_iter(ViewIface *view,
				  ViewIter *iter, IterFlags flags)
{
	make_iter((ViewDetails *) view, iter, flags);
}

static void view_details_cursor_to_iter(ViewIface *view, ViewIter *iter)
{
	GtkTreePath *path;

	if (!iter)
	{
		/* XXX: How do we get rid of the cursor? */
		g_print("FIXME: Remove cursor\n");
		return;
	}
		
	path = gtk_tree_path_new();

	gtk_tree_path_append_index(path, iter->i);
	gtk_tree_view_set_cursor((GtkTreeView *) view, path, NULL, FALSE);
	gtk_tree_path_free(path);
}

static void set_selected(ViewDetails *view_details, int i, gboolean selected)
{
	GtkTreeModel *model = (GtkTreeModel *) view_details;
	GtkTreeIter t_iter;
	GtkTreePath *path;
	GPtrArray *items = view_details->items;
	ViewItem *view_item;

	g_return_if_fail(i >= 0 && i < items->len);
	view_item = (ViewItem *) items->pdata[i];

	if (view_item->selected == selected)
		return;

	view_item->selected = selected;

	path = gtk_tree_path_new();
	gtk_tree_path_append_index(path, i);
	t_iter.user_data = GINT_TO_POINTER(i);
	gtk_tree_model_row_changed(model, path, &t_iter);
	gtk_tree_path_free(path);
}

static void view_details_set_selected(ViewIface *view,
					 ViewIter *iter,
					 gboolean selected)
{
	set_selected((ViewDetails *) view, iter->i, selected);
}

static gboolean get_selected(ViewDetails *view_details, int i)
{
	GPtrArray *items = view_details->items;

	g_return_val_if_fail(i >= 0 && i < items->len, FALSE);

	return ((ViewItem *) items->pdata[i])->selected;
}

static gboolean view_details_get_selected(ViewIface *view, ViewIter *iter)
{
	return get_selected((ViewDetails *) view, iter->i);
}

static void view_details_select_only(ViewIface *view, ViewIter *iter)
{
	ViewDetails *view_details = (ViewDetails *) view;
	int i = iter->i;
	int n = view_details->items->len;

	set_selected(view_details, i, TRUE);

	while (n > 0)
	{
		n--;

		if (n != i)
			set_selected(view_details, n, FALSE);
	}
}

static void view_details_set_frozen(ViewIface *view, gboolean frozen)
{
}

static void view_details_wink_item(ViewIface *view, ViewIter *iter)
{
}

static void view_details_autosize(ViewIface *view)
{
}

static gboolean view_details_cursor_visible(ViewIface *view)
{
	return FALSE;
}

static void view_details_set_base(ViewIface *view, ViewIter *iter)
{
	ViewDetails *view_details = (ViewDetails *) view;

	view_details->cursor_base = iter->i;
}

static DirItem *iter_init(ViewIter *iter)
{
	ViewDetails *view_details = (ViewDetails *) iter->view;
	int i = -1;
	int n = view_details->items->len;
	int flags = iter->flags;

	iter->peek = iter_peek;

	if (iter->n_remaining == 0)
		return NULL;

	if (flags & VIEW_ITER_FROM_CURSOR)
	{
		GtkTreePath *path;
		gtk_tree_view_get_cursor((GtkTreeView *) view_details,
					 &path, NULL);
		if (!path)
			return NULL;	/* No cursor */
		i = gtk_tree_path_get_indices(path)[0];
		gtk_tree_path_free(path);
	}
	else if (flags & VIEW_ITER_FROM_BASE)
		i = view_details->cursor_base;
	
	if (i < 0 || i >= n)
	{
		/* Either a normal iteration, or an iteration from an
		 * invalid starting point.
		 */
		if (flags & VIEW_ITER_BACKWARDS)
			i = n - 1;
		else
			i = 0;
	}

	if (i < 0 || i >= n)
		return NULL;	/* No items at all! */

	iter->next = flags & VIEW_ITER_BACKWARDS ? iter_prev : iter_next;
	iter->n_remaining--;
	iter->i = i;

	if (flags & VIEW_ITER_SELECTED && !is_selected(view_details, i))
		return iter->next(iter);
	return iter->peek(iter);
}

static DirItem *iter_prev(ViewIter *iter)
{
	ViewDetails *view_details = (ViewDetails *) iter->view;
	int n = view_details->items->len;
	int i = iter->i;

	g_return_val_if_fail(iter->n_remaining >= 0, NULL);

	/* i is the last item returned (always valid) */

	g_return_val_if_fail(i >= 0 && i < n, NULL);

	while (iter->n_remaining)
	{
		i--;
		iter->n_remaining--;

		if (i == -1)
			i = n - 1;

		g_return_val_if_fail(i >= 0 && i < n, NULL);

		if (iter->flags & VIEW_ITER_SELECTED &&
		    !is_selected(view_details, i))
			continue;

		iter->i = i;
		return ((ViewItem *) view_details->items->pdata[i])->item;
	}
	
	iter->i = -1;
	return NULL;
}

static DirItem *iter_next(ViewIter *iter)
{
	ViewDetails *view_details = (ViewDetails *) iter->view;
	int n = view_details->items->len;
	int i = iter->i;

	g_return_val_if_fail(iter->n_remaining >= 0, NULL);

	/* i is the last item returned (always valid) */

	g_return_val_if_fail(i >= 0 && i < n, NULL);
	
	while (iter->n_remaining)
	{
		i++;
		iter->n_remaining--;

		if (i == n)
			i = 0;

		g_return_val_if_fail(i >= 0 && i < n, NULL);

		if (iter->flags & VIEW_ITER_SELECTED &&
		    !is_selected(view_details, i))
			continue;

		iter->i = i;
		return ((ViewItem *) view_details->items->pdata[i])->item;
	}
	
	iter->i = -1;
	return NULL;
}

static DirItem *iter_peek(ViewIter *iter)
{
	ViewDetails *view_details = (ViewDetails *) iter->view;
	int n = view_details->items->len;
	int i = iter->i;

	if (i == -1)
		return NULL;
	
	g_return_val_if_fail(i >= 0 && i < n, NULL);

	return ((ViewItem *) view_details->items->pdata[i])->item;
}

/* Set the iterator to return 'i' on the next peek() */
static void make_item_iter(ViewDetails *view_details, ViewIter *iter, int i)
{
	make_iter(view_details, iter, 0);

	g_return_if_fail(i >= -1 && i < (int) view_details->items->len);

	iter->i = i;
	iter->next = iter_next;
	iter->peek = iter_peek;
	iter->n_remaining = 0;
}

static void make_iter(ViewDetails *view_details, ViewIter *iter,
		      IterFlags flags)
{
	iter->view = (ViewIface *) view_details;
	iter->next = iter_init;
	iter->peek = NULL;
	iter->i = -1;

	iter->flags = flags;

	if (flags & VIEW_ITER_ONE_ONLY)
	{
		iter->n_remaining = 1;
		iter->next(iter);
	}
	else
		iter->n_remaining = view_details->items->len;
}
