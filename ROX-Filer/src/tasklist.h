/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _TASKLIST_H
#define _TASKLIST_H

#include <gtk/gtk.h>

struct _IconWindow {
	GtkWidget *widget;	/* Widget displayed on pinboard */
	Window	xwindow;
	gboolean iconified;
};

void tasklist_update(gboolean to_empty);
void tasklist_uniconify(IconWindow *win);

#endif /* _TASKLIST_H */
