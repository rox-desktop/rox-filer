/*
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

typedef struct _Radios Radios;

extern GdkFont	   	*fixed_font;
extern gint		screen_width, screen_height;

/* For Xinerama */
extern gint		n_monitors;
extern GdkRectangle	*monitor_geom;
/* Smallest monitor - use for sizing windows */
extern gint		monitor_width, monitor_height;

typedef void (*HelpFunc)(gpointer data);
typedef const char *ParseFunc(gchar *line);

void gui_store_screen_geometry(GdkScreen *screen);

void gui_support_init(void);
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
gboolean get_pointer_xy(int *x, int *y);
void centre_window(GdkWindow *window, int x, int y);
void wink_widget(GtkWidget *widget);
void destroy_on_idle(GtkWidget *widget);
gint rox_spawn(const gchar *dir, const gchar **argv);
GtkWidget *button_new_mixed(const char *stock, const char *message);
GtkWidget *button_new_image_text(GtkWidget *image, const char *message);
void entry_set_error(GtkWidget *entry, gboolean error);
void window_put_just_above(GdkWindow *higher, GdkWindow *lower);
void fixed_move_fast(GtkFixed *fixed, GtkWidget *widget, int x, int y);
void tooltip_show(guchar *text);
void tooltip_prime(GtkFunction callback, GObject *object);
void widget_modify_font(GtkWidget *widget, PangoFontDescription *font_desc);
gboolean confirm(const gchar *message, const gchar *stock, const gchar *action);

Radios *radios_new(void (*changed)(gpointer data), gpointer data);
void radios_add(Radios *radios, const gchar *tip, gint value,
		const gchar *label, ...);
void radios_pack(Radios *radios, GtkBox *box);
void radios_set_value(Radios *radios, gint value);
gint radios_get_value(Radios *radios);
GList *uri_list_to_glist(const char *uri_list);
GtkWidget *simple_image_new(GdkPixbuf *pixbuf);
void render_pixbuf(GdkPixbuf *pixbuf, GdkDrawable *target, GdkGC *gc,
		   int x, int y, int width, int height);

#endif /* _GUI_SUPPORT_H */
