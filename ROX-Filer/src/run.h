/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _RUN_H
#define _RUN_H

#include <gtk/gtk.h>

void run_app(char *path);
void run_with_files(char *path, GList *uri_list);
void run_with_data(char *path, gpointer data, gulong length);
gboolean run_by_path(guchar *full_path);
gboolean run_diritem(guchar *full_path,
		     DirItem *item,
		     FilerWindow *filer_window,
		     FilerWindow *src_window,
		     gboolean edit);
void show_item_help(guchar *path, DirItem *item);
void open_to_show(guchar *path);
void examine(guchar *path);

#endif /* _RUN_H */
