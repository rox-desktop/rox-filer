/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2001, the ROX-Filer team.
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

#include <stdlib.h>

#include "global.h"

#include "options.h"
#include "bind.h"

gboolean o_new_window_on_1 = FALSE;
static gboolean o_menu_button = 3;
static gboolean o_single_click = TRUE;

/* Static prototypes */
static void update_options(void);

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void bind_init(void)
{
	option_add_int("bind_new_button_1", o_new_window_on_1, NULL);
	option_add_int("bind_menu_button_2", FALSE, NULL);
	option_add_int("bind_single_click", o_single_click, NULL);

	option_add_notify(update_options);
}

/* Call this when a button event occurrs and you want to know what
 * to do.
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
	gboolean press = event->type == GDK_BUTTON_PRESS;
	gboolean release = event->type == GDK_BUTTON_RELEASE;
	gboolean select = event->button == 1; /* (old RISC OS names) */
	gboolean adjust = event->button != 1;

	gboolean dclick = event->type == GDK_2BUTTON_PRESS;
	gboolean dclick_mode =
		(context == BIND_DIRECTORY_ICON && !o_single_click);

	if (b > 3)
		return ACT_IGNORE;

	if (b == o_menu_button)
		return press ? ACT_POPUP_MENU : ACT_IGNORE;

	if (item && dclick && dclick_mode)
		return shift ? ACT_EDIT_ITEM : ACT_OPEN_ITEM;

	if (!press)
	{
		if (release && item && (!dclick_mode) && (!ctrl) &&
			(select || (adjust && context == BIND_DIRECTORY_ICON)))
			return shift ? ACT_EDIT_ITEM : ACT_OPEN_ITEM;
		return ACT_IGNORE;
	}

	if (background)
	{
		gboolean clear = (!ctrl) && select;

		if (context == BIND_DIRECTORY)
			return clear ? ACT_LASSO_CLEAR : ACT_LASSO_MODIFY;

		if (context == BIND_PANEL)
			return clear ? ACT_SLIDE_CLEAR_PANEL : ACT_SLIDE_PANEL;
				
		return clear ? ACT_CLEAR_SELECTION : ACT_IGNORE;
	}

	if (ctrl || (adjust && dclick_mode))
		return ACT_PRIME_AND_TOGGLE;

	if (icon && adjust)
		return ACT_MOVE_ICON;

	return dclick_mode ? ACT_PRIME_AND_SELECT : ACT_PRIME_FOR_DND;
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static void update_options(void)
{
	o_new_window_on_1 = option_get_int("bind_new_button_1");
	o_menu_button = option_get_int("bind_menu_button_2") ? 2 : 3;
	o_single_click = option_get_int("bind_single_click");
}

