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

typedef struct _ViewIface      ViewIface;
typedef struct _ViewIfaceClass ViewIfaceClass;
struct _ViewIfaceClass
{
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

#endif /* __VIEW_IFACE_H__ */
