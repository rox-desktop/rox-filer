/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _ICON_H
#define _ICON_H

#include <glib.h>

extern GList *icon_selection;
extern GtkWidget *icon_menu;		/* The popup icon menu */
extern GtkWidget *icon_menu_remove_backdrop; /* 'Remove Backdrop' menu item */

typedef struct _IconClass IconClass;

struct _IconClass {
	GObjectClass parent;

	gboolean (*same_group)(Icon *icon, Icon *other);
	void (*destroy)(Icon *icon);
	void (*redraw)(Icon *icon);
	void (*update)(Icon *icon);

	/* Acts on selected items */
	void (*remove_items)(void);
};

struct _Icon {
	GObject		object;
	
	gboolean	selected;
	guchar		*src_path;	/* Eg: ~/Apps */
	guchar		*path;		/* Eg: /home/fred/Apps */
	DirItem		*item;

	GtkWidget	*dialog;	/* Current rename box, if any */
};

GType icon_get_type(void);
gboolean icons_require(const gchar *path);
void icon_may_update(Icon *icon);
void icons_may_update(const gchar *path);
void icon_prepare_menu(Icon *icon);
void icon_set_selected(Icon *icon, gboolean selected);
void icon_select_only(Icon *select);
void icon_set_path(Icon *icon, const char *pathname, const char *name);
gchar *icon_create_uri_list(void);
void icon_destroy(Icon *icon);

#endif /* _ICON_H */
