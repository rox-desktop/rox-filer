/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#include <gtk/gtk.h>

#include <dir.h>

void run_app(char *path);
void run_with_files(char *path, GSList *uri_list);
void run_with_data(char *path, gpointer data, gulong length);
gboolean run_diritem(guchar *full_path,
		     DirItem *item,
		     FilerWindow *filer_window,
		     gboolean edit);
