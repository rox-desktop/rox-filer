/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _ICON_H
#define _ICON_H

#include <glib.h>

#include <pango/pango.h>

extern GList *icon_selection;
extern gboolean tmp_icon_selected;

struct _Icon {
	Panel		*panel;		/* NULL => Pinboard icon */
	GtkWidget	*widget;	/* The drawing area for the icon */
	gboolean	selected;
	guchar		*src_path;	/* Eg: ~/Apps */
	guchar		*path;		/* Eg: /home/fred/Apps */
	DirItem		*item;

	/* Only used on the pinboard... */
	GtkWidget	*win;
	GdkBitmap	*mask;
	int		x, y;
	int		width, height;
	int		name_width;
	PangoLayout	*layout;	/* The label */

	/* Only used on the panel... */
	GtkWidget	*label;
	GtkWidget	*socket;	/* For applets */
};

void icon_init(void);
void icon_hash_path(Icon *icon);
void icon_unhash_path(Icon *icon);
gboolean icons_require(const gchar *path);
void icon_may_update(Icon *icon);
void icons_may_update(const gchar *path);
void update_all_icons(void);
void icon_show_menu(GdkEventButton *event, Icon *icon, Panel *panel);
void icon_set_selected(Icon *icon, gboolean selected);
void icon_select_only(Icon *select);
void icon_destroyed(Icon *icon);
void icon_set_tip(Icon *icon);
void icons_update_tip(void);
gchar *icon_convert_path(const gchar *path);

#endif /* _ICON_H */
