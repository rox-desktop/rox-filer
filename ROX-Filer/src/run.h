/*
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _RUN_H
#define _RUN_H

#include <gtk/gtk.h>

void run_app(const char *path);
void run_app_with_arg(const char *path, const char *arg);
void run_with_files(const char *path, GList *uri_list);
void run_with_data(const char *path, gpointer data, gulong length);
gboolean run_by_path(const guchar *full_path);
gboolean run_diritem(const guchar *full_path,
		     DirItem *item,
		     FilerWindow *filer_window,
		     FilerWindow *src_window,
		     gboolean edit);
gboolean run_diritem_with_arg(const guchar *full_path,
			      DirItem *item,
			      const gchar *arg,
			      FilerWindow *filer_window,
			      FilerWindow *src_window,
			      gboolean edit);
void open_to_show(const guchar *path);
void examine(const guchar *path);
void show_help_files(const char *dir);

#endif /* _RUN_H */
