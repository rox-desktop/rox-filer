/*
 * $Id$
 *
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef __VIEW_DETAILS_H__
#define __VIEW_DETAILS_H__

#include <gtk/gtk.h>

typedef struct _ViewDetailsClass ViewDetailsClass;

#define VIEW_DETAILS(obj) \
	(GTK_CHECK_CAST((obj), view_details_get_type(), ViewDetails))

GtkWidget *view_details_new(FilerWindow *filer_window);
GType view_details_get_type(void);

#endif /* __VIEW_DETAILS_H__ */
