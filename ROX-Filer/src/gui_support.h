/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _GUI_SUPPORT_H
#define _GUI_SUPPORT_H

#include <gtk/gtk.h>

void gui_support_init();
int get_choice(char *title,
	       char *message,
	       int number_of_buttons, ...);
void report_error(char *title, char *message);
void set_cardinal_property(GdkWindow *window, GdkAtom prop, guint32 value);
void add_panel_properties(GtkWidget *window);

#endif /* _GUI_SUPPORT_H */
