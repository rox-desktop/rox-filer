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
void panel_may_update(guchar *path);
gboolean panel_has(guchar *path);

#endif /* _PANEL_H */
