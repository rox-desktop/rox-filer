/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _PANEL_H
#define _PANEL_H

#define MENU_MARGIN 32

typedef enum {
	PANEL_TOP,
	PANEL_BOTTOM,
	PANEL_LEFT,
	PANEL_RIGHT,

	PANEL_NUMBER_OF_SIDES	/* This goes last! */
} PanelSide;

struct _Panel {
	GtkWidget	*window;
	GtkAdjustment	*adj;		/* Scroll position of the bar */
	PanelSide	side;
	guchar		*name;		/* Leaf name */

	GtkWidget	*before;	/* Icons at the left/top end */
	GtkWidget	*after;		/* Icons at the right/bottom end */

	GtkWidget	*gap;		/* Event box between sides */
};

extern Panel *current_panel[PANEL_NUMBER_OF_SIDES];

void panel_init(void);
Panel *panel_new(guchar *name, PanelSide side);
void panel_icon_may_update(Icon *icon);
void panel_save(Panel *panel);
gboolean panel_want_show_text(Icon *icon);
void panel_icon_renamed(Icon *icon);

#endif /* _PANEL_H */
