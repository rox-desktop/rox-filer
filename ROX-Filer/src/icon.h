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

struct _Icon {
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
};

void show_rename_box(GtkWidget	*widget,
		     Icon	*icon,
		     RenameFn	callback);
guchar *icon_convert_path(guchar *path);

#endif /* _ICON_H */
