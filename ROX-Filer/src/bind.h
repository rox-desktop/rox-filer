/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _BIND_H
#define _BIND_H

#include <gtk/gtk.h>

typedef enum {
	BIND_DIRECTORY,
	BIND_DIRECTORY_ICON,
	BIND_PANEL,
	BIND_PANEL_ICON,
	BIND_PINBOARD,
	BIND_PINBOARD_ICON,
} BindContext;

typedef enum {
	ACT_IGNORE,		/* Do nothing */
	ACT_SELECT_EXCL,	/* Select just this one item */
	ACT_TOGGLE_SELECTED,	/* Toggle this item's selected state */
	ACT_OPEN_ITEM,		/* Run app, load file or open dir */
	ACT_EDIT_ITEM,		/* Open app, load text file or mount dir */
	ACT_POPUP_MENU,		/* Show the popup menu */
	ACT_CLEAR_SELECTION,	/* Unselect all icons in this area */
	ACT_MOVE_ICON,		/* Reposition a pinboard/panel icon */
} BindAction;

BindAction bind_lookup_bev(BindContext context, GdkEventButton *event);

#endif /* _BIND_H */
