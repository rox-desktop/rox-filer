/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

extern GtkAccelGroup	*filer_keys;

void menu_init(void);
void menu_finish(void);
void show_filer_menu(FilerWindow *filer_window, GdkEventButton *event,
		     int item);
