/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _ICON_H
#define _ICON_H

#include <glib.h>

#include "dir.h"

typedef void (*RenameFn)(Icon *icon);

typedef enum {ICON_PANEL, ICON_PINBOARD} IconType;

struct _Icon {
	IconType	type;

	GtkWidget	*widget;	/* The drawing area for the icon */
	gboolean	selected;
	guchar		*src_path;	/* Eg: ~/Apps */
	guchar		*path;		/* Eg: /home/fred/Apps */
	DirItem		item;

	/* Only used on the pinboard... */
	GtkWidget	*win;
	GdkBitmap	*mask;
	int		x, y;

	/* Only used on the panel... */
	Panel		*panel;		/* Panel containing this icon */
	GtkWidget	*socket;	/* For applets */
};

void icon_init(void);
void show_rename_box(GtkWidget	*widget,
		     Icon	*icon,
		     RenameFn	callback);
guchar *icon_convert_path(guchar *path);
void icon_hash_path(Icon *icon);
void icon_unhash_path(Icon *icon);
gboolean icons_require(guchar *path);
void icons_may_update(guchar *path);
void update_all_icons(void);

#endif /* _ICON_H */
