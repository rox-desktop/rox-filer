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

void drag_selection(Collection 		*collection,
		    GdkEventMotion 	*event,
		    gint		number_selected,
		    FilerWindow		*filer_window);
void drag_data_get(GtkWidget          *widget,
		   GdkDragContext     *context,
		   GtkSelectionData   *selection_data,
		   guint               info,
		   guint32             time,
		   FilerWindow	      *filer_window);
void drag_set_dest(FilerWindow *filer_window);
void dnd_init();
GtkWidget *create_dnd_options();

#endif /* _DND_H */
