/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#include <gtk/gtk.h>
#include <collection.h>

void drag_selection(Collection 		*collection,
		    GdkEventMotion 	*event,
		    gint		number_selected,
		    gpointer 		user_data);
void drag_data_get(GtkWidget          *widget,
		   GdkDragContext     *context,
		   GtkSelectionData   *selection_data,
		   guint               info,
		   guint32             time);
