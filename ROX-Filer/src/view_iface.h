/*
 * $Id$
 *
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef __VIEW_IFACE_H__
#define __VIEW_IFACE_H__

#include <glib-object.h>

typedef enum {
	/* iter->next moves to selected items only */
	VIEW_ITER_SELECTED	= 1 << 0,

	/* iteration starts from cursor (first call to next() returns
	 * iter AFTER cursor). If there is no cursor, flag is ignored
	 * (will iterate over everything).
	 */
	VIEW_ITER_FROM_CURSOR	= 1 << 1,

	/* next() moves backwards */
	VIEW_ITER_BACKWARDS	= 1 << 2,

	/* next() always returns NULL and has no effect */
	VIEW_ITER_ONE_ONLY	= 1 << 3,
} IterFlags;

typedef struct _ViewIfaceClass	ViewIfaceClass;

struct _ViewIter {
	/* Returns the value last returned by next() */
	DirItem	   *(*peek)(ViewIter *iter);

	DirItem	   *(*next)(ViewIter *iter);

	/* private fields */
	Collection *collection;
	int	   i, n_remaining;
	int	   flags;
};

struct _ViewIfaceClass {
	GTypeInterface base_iface;

	void (*sort)(ViewIface *obj);
	void (*style_changed)(ViewIface *obj, int flags);
	gboolean (*autoselect)(ViewIface *obj, const gchar *leaf);
	void (*add_items)(ViewIface *obj, GPtrArray *items);
	void (*update_items)(ViewIface *obj, GPtrArray *items);
	void (*delete_if)(ViewIface *obj,
			gboolean (*test)(gpointer item, gpointer data),
			gpointer data);
	void (*clear)(ViewIface *obj);
	void (*select_all)(ViewIface *obj);
	void (*clear_selection)(ViewIface *obj);
	int (*count_items)(ViewIface *obj);
	int (*count_selected)(ViewIface *obj);
	void (*show_cursor)(ViewIface *obj);

	void (*get_iter)(ViewIface *obj, ViewIter *iter, IterFlags flags);
	void (*cursor_to_iter)(ViewIface *obj, ViewIter *iter);

	void (*set_selected)(ViewIface *obj, ViewIter *iter, gboolean selected);
	gboolean (*get_selected)(ViewIface *obj, ViewIter *iter);
	void (*set_frozen)(ViewIface *obj, gboolean frozen);
	void (*select_only)(ViewIface *obj, ViewIter *iter);
	void (*wink_item)(ViewIface *obj, ViewIter *iter);
	void (*autosize)(ViewIface *obj);
	gboolean (*cursor_visible)(ViewIface *obj);
};

#define VIEW_TYPE_IFACE           (view_iface_get_type())

#define VIEW(obj)		  (G_TYPE_CHECK_INSTANCE_CAST((obj), \
				   VIEW_TYPE_IFACE, ViewIface))

#define VIEW_IS_IFACE(obj)	  (G_TYPE_CHECK_INSTANCE_TYPE((obj), \
				   VIEW_TYPE_IFACE))

#define VIEW_IFACE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_INTERFACE((obj), \
				   VIEW_TYPE_IFACE, ViewIfaceClass))

/* Flags for view_style_changed() */
enum {
	VIEW_UPDATE_VIEWDATA	= 1 << 0,
	VIEW_UPDATE_NAME	= 1 << 1,
};

GType view_iface_get_type(void);
void view_sort(ViewIface *obj);
void view_style_changed(ViewIface *obj, int flags);
gboolean view_autoselect(ViewIface *obj, const gchar *leaf);
void view_add_items(ViewIface *obj, GPtrArray *items);
void view_update_items(ViewIface *obj, GPtrArray *items);
void view_delete_if(ViewIface *obj,
		    gboolean (*test)(gpointer item, gpointer data),
		    gpointer data);
void view_clear(ViewIface *obj);
void view_select_all(ViewIface *obj);
void view_clear_selection(ViewIface *obj);
int view_count_items(ViewIface *obj);
int view_count_selected(ViewIface *obj);
void view_show_cursor(ViewIface *obj);

void view_get_iter(ViewIface *obj, ViewIter *iter, IterFlags flags);
void view_get_cursor(ViewIface *obj, ViewIter *iter);
void view_cursor_to_iter(ViewIface *obj, ViewIter *iter);

void view_set_selected(ViewIface *obj, ViewIter *iter, gboolean selected);
gboolean view_get_selected(ViewIface *obj, ViewIter *iter);
void view_select_only(ViewIface *obj, ViewIter *iter);
void view_freeze(ViewIface *obj);
void view_thaw(ViewIface *obj);
void view_select_if(ViewIface *obj,
		    gboolean (*test)(ViewIter *iter, gpointer data),
		    gpointer data);

void view_wink_item(ViewIface *obj, ViewIter *iter);
void view_autosize(ViewIface *obj);
gboolean view_cursor_visible(ViewIface *obj);

#endif /* __VIEW_IFACE_H__ */
