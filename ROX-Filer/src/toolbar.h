/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _TOOLBAR_H
#define _TOOLBAR_H

#include <gtk/gtk.h>

/* The values correspond to the menu indexes in the option widget */
typedef enum {
	TOOLBAR_NONE 	= 0,
	TOOLBAR_NORMAL 	= 1,
	TOOLBAR_LARGE 	= 2,
} ToolbarType;

extern ToolbarType o_toolbar;

/* Prototypes */
void toolbar_init(void);
GtkWidget *toolbar_new(FilerWindow *filer_window);
GtkWidget *toolbar_tool_option(int i);

#endif /* _TOOLBAR_H */
