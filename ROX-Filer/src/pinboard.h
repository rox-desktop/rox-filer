/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _PINBOARD_H
#define _PINBOARD_H

extern Pinboard	*current_pinboard;

extern Icon *pinboard_drag_in_progress;

extern GdkColor pin_text_fg_col, pin_text_bg_col;

typedef enum {
	BACKDROP_NONE,
	BACKDROP_PROGRAM,
	BACKDROP_CENTRE, BACKDROP_SCALE, BACKDROP_TILE
} BackdropStyle;

void pinboard_init(void);
void pinboard_activate(const gchar *name);
void pinboard_pin(const gchar *path, const gchar *name, int x, int y);
void pinboard_move_icons(void);
const gchar *pinboard_get_name(void);
void pinboard_set_backdrop(void);
void pinboard_set_backdrop_app(const gchar *app);
GdkWindow *pinboard_get_window(void);
void pinboard_add_widget(GtkWidget *widget, int x, int y);

#endif /* _PINBOARD_H */
