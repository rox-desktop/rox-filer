/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2000, Thomas Leonard, <tal197@users.sourceforge.net>.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* bind.c - converts user gestures (clicks, etc) into actions */

#include "config.h"

#include "global.h"

#include "bind.h"


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

/* Call this when a button event occurrs and you want to know what
 * to do.
 *
 * NOTE: Currently, this is only used for the panels.
 */
BindAction bind_lookup_bev(BindContext context, GdkEventButton *event)
{
	gint	b = event->button;
	gboolean shift = (event->state & GDK_SHIFT_MASK) != 0;
	gboolean ctrl = (event->state & GDK_CONTROL_MASK) != 0;
	gboolean icon  = context == BIND_PINBOARD_ICON ||
				context == BIND_PANEL_ICON;
	gboolean item = icon || context == BIND_DIRECTORY_ICON;
	gboolean background = context == BIND_PINBOARD ||
				context == BIND_PANEL ||
				context == BIND_DIRECTORY;

	if (b > 3)
		return ACT_IGNORE;

	if (b == 3)
		return ACT_POPUP_MENU;

	if (background && b == 1)
		return ACT_CLEAR_SELECTION;

	if (item && ctrl)
		return ACT_TOGGLE_SELECTED;

	if (icon && b == 2)
		return ACT_MOVE_ICON;

	if (item)
		return shift ? ACT_EDIT_ITEM : ACT_OPEN_ITEM;

	return ACT_IGNORE;
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

