/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _DND_H
#define _DND_H

#include <gtk/gtk.h>
#include "collection.h"
#include "filer.h"

extern gboolean o_no_hostnames;
void drag_selection(GtkWidget *widget, GdkEventMotion *event, guchar *uri_list);
void drag_one_item(GtkWidget		*widget,
		   GdkEventMotion	*event,
		   guchar		*full_path,
		   DirItem		*item,
		   gboolean		set_run_action);
void drag_data_get(GtkWidget      	*widget,
		   GdkDragContext     	*context,
		   GtkSelectionData   	*selection_data,
		   guint               	info,
		   guint32             	time,
		   gpointer	       	data);
void drag_set_dest(FilerWindow *filer_window);
void drag_set_pinboard_dest(GtkWidget *widget);
void dnd_init();
GtkWidget *create_dnd_options();

#endif /* _DND_H */
