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

extern GdkFont	   	*fixed_font;
extern GdkFont	   	*item_font;
extern GtkStyle   	*fixed_style;
extern gint		fixed_width;
extern GdkColor 	red;
extern GdkGC 		*red_gc;
extern gint		screen_width, screen_height;

typedef void (*HelpFunc)(gpointer data);
typedef char *ParseFunc(guchar *line);

void gui_support_init();
int get_choice(char *title,
	       char *message,
	       int number_of_buttons, ...);
void report_error(char *message, ...);
void set_cardinal_property(GdkWindow *window, GdkAtom prop, guint32 value);
void make_panel_window(GdkWindow *window);
gint hide_dialog_event(GtkWidget *widget, GdkEvent *event, gpointer window);
void delayed_error(char *error, ...);
gboolean load_file(char *pathname, char **data_out, long *length_out);
GtkWidget *new_help_button(HelpFunc show_help, gpointer data);
void parse_file(char *path, ParseFunc *parse_line);
gboolean setup_xdnd_proxy(guint32 xid, GdkWindow *proxy_window);
void release_xdnd_proxy(guint32 xid);
GdkWindow *find_click_proxy_window(void);
gboolean get_pointer_xy(int *x, int *y);
void centre_window(GdkWindow *window, int x, int y);
void make_colour_patch(GtkWidget *button);
void button_patch_set_colour(GtkWidget *button, GdkColor *color);
GdkColor *button_patch_get_colour(GtkWidget *button);
void wink_widget(GtkWidget *widget);
void destroy_on_idle(GtkWidget *widget);
gboolean rox_spawn(gchar *dir, gchar **argv);

#endif /* _GUI_SUPPORT_H */
