/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _PANEL_H
#define _PANEL_H

typedef enum {
	PANEL_TOP,
	PANEL_BOTTOM,
	PANEL_LEFT,
	PANEL_RIGHT,

	PANEL_NUMBER_OF_SIDES	/* This goes last! */
} PanelSide;

void panel_init(void);
Panel *panel_new(guchar *name, PanelSide side);
void panel_icon_may_update(Icon *icon);

#endif /* _PANEL_H */
