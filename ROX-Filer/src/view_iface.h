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

enum {
	VIEW_ITER_SELECTED = 1 << 0,
};

typedef struct _ViewIfaceClass	ViewIfaceClass;
typedef struct _ViewIter	ViewIter;

struct _ViewIter {
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
	void (*clear_selection)(ViewIface *obj);
	int (*count_items)(ViewIface *obj);
	int (*count_selected)(ViewIface *obj);
	void (*show_cursor)(ViewIface *obj);

	void (*get_iter)(ViewIface *obj, ViewIter *iter);
	void (*cursor_to_iter)(ViewIface *obj, ViewIter *iter);
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
void view_clear_selection(ViewIface *obj);
int view_count_items(ViewIface *obj);
int view_count_selected(ViewIface *obj);
void view_show_cursor(ViewIface *obj);

void view_get_iter(ViewIface *obj, ViewIter *iter, int flags);
void view_cursor_to_iter(ViewIface *obj, ViewIter *iter);

#endif /* __VIEW_IFACE_H__ */
