/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _GUI_SUPPORT_H
#define _GUI_SUPPORT_H

#include <gtk/gtk.h>

#define WIN_STATE_STICKY          (1<<0) /* Fixed relative to screen */
#define WIN_STATE_HIDDEN          (1<<4) /* Not on taskbar but window visible */
#define WIN_STATE_FIXED_POSITION  (1<<8) /* Window is fixed in position even */
#define WIN_STATE_ARRANGE_IGNORE  (1<<9) /* Ignore for auto arranging */

#define WIN_HINTS_SKIP_FOCUS      (1<<0) /* Do not focus */
#define WIN_HINTS_SKIP_WINLIST    (1<<1) /* Not in win list */
#define WIN_HINTS_SKIP_TASKBAR    (1<<2) /* Not on taskbar */

extern GdkFont	   	*fixed_font;
extern gint		screen_width, screen_height;

typedef void (*HelpFunc)(gpointer data);
typedef const char *ParseFunc(gchar *line);

void gui_support_init();
int get_choice(const char *title,
	       const char *message,
	       int number_of_buttons, ...);
void report_error(const char *message, ...);
void info_message(const char *message, ...);
void set_cardinal_property(GdkWindow *window, GdkAtom prop, guint32 value);
void make_panel_window(GtkWidget *widget);
void delayed_error(const char *error, ...);
gboolean load_file(const char *pathname, char **data_out, long *length_out);
GtkWidget *new_help_button(HelpFunc show_help, gpointer data);
void parse_file(const char *path, ParseFunc *parse_line);
gboolean setup_xdnd_proxy(guint32 xid, GdkWindow *proxy_window);
void release_xdnd_proxy(guint32 xid);
GdkWindow *find_click_proxy_window(void);
gboolean get_pointer_xy(int *x, int *y);
void centre_window(GdkWindow *window, int x, int y);
void wink_widget(GtkWidget *widget);
void destroy_on_idle(GtkWidget *widget);
gboolean rox_spawn(const gchar *dir, const gchar **argv);
GtkWidget *button_new_mixed(const char *stock, const char *message);
void entry_set_error(GtkWidget *entry, gboolean error);

#endif /* _GUI_SUPPORT_H */
