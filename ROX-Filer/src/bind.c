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

#include <stdlib.h>

#include "global.h"

#include "options.h"
#include "bind.h"

/* Options bits */
static GtkWidget *create_options();
static void update_options();
static void set_options();
static void save_options();
static char *bind_single_click(char *data);
static char *bind_menu_button(char *data);
static char *bind_new_window_on_1(char *data);

static OptionsSection options =
{
	N_("Mouse bindings"),
	create_options,
	update_options,
	set_options,
	save_options
};

static gint o_menu_button = 3;
static gboolean o_single_click = TRUE;
gboolean o_new_window_on_1 = FALSE;	/* Button 1 => New window */
static GtkWidget *toggle_single_click;
static GtkWidget *toggle_new_window_on_1;
static GtkWidget *toggle_menu_on_2;


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void bind_init(void)
{
	options_sections = g_slist_prepend(options_sections, &options);
	option_register("bind_new_window_on_1", bind_new_window_on_1);
	option_register("bind_menu_button", bind_menu_button);
	option_register("bind_single_click", bind_single_click);
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

/* Build up some option widgets to go in the options dialog, but don't
 * fill them in yet.
 */
static GtkWidget *create_options(void)
{
	GtkWidget	*vbox;

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);

	toggle_new_window_on_1 =
		gtk_check_button_new_with_label(
			_("New window on button 1 (RISC OS style)"));
	OPTION_TIP(toggle_new_window_on_1,
			_("Clicking with mouse button 1 (usually the "
			"left button) opens a directory in a new window "
			"with this turned on. Clicking with the button-2 "
			"(middle) will reuse the current window."));
	gtk_box_pack_start(GTK_BOX(vbox), toggle_new_window_on_1,
			FALSE, TRUE, 0);

	toggle_menu_on_2 =
		gtk_check_button_new_with_label(
			_("Menu on button 2 (RISC OS style)"));
	OPTION_TIP(toggle_menu_on_2,
			_("Use button 2, the middle button (click both buttons "
			"at once on two button mice), to pop up the menu. "
			"If off, use button 3 (right) instead."));
	gtk_box_pack_start(GTK_BOX(vbox), toggle_menu_on_2, FALSE, TRUE, 0);

	toggle_single_click =
		gtk_check_button_new_with_label(_("Single-click navigation"));
	OPTION_TIP(toggle_single_click,
			_("Clicking on an item opens it with this on. Hold "
			"down Control to select the item instead. If off, "
			"clicking once selects an item; double click to open "
			"things."));
	gtk_box_pack_start(GTK_BOX(vbox), toggle_single_click, FALSE, TRUE, 0);

	return vbox;
}

/* Reflect current state by changing the widgets in the options box */
static void update_options()
{
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle_new_window_on_1),
			o_new_window_on_1);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle_menu_on_2),
			o_menu_button == 2 ? 1 : 0);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle_single_click),
			o_single_click);
}

/* Set current values by reading the states of the widgets in the options box */
static void set_options()
{
	o_new_window_on_1 = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(toggle_new_window_on_1));

	o_menu_button = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(toggle_menu_on_2)) ? 2 : 3;

	o_single_click = gtk_toggle_button_get_active(
			GTK_TOGGLE_BUTTON(toggle_single_click));
}

static void save_options()
{
	option_write("bind_new_window_on_1", o_new_window_on_1 ? "1" : "0");
	option_write("bind_menu_button", o_menu_button == 2 ? "2" : "3");
	option_write("bind_single_click", o_single_click ? "1" : "0");
}

static char *bind_new_window_on_1(char *data)
{
	o_new_window_on_1 = atoi(data) != 0;
	return NULL;
}

static char *bind_menu_button(char *data)
{
	o_menu_button = atoi(data) == 2 ? 2 : 3;
	return NULL;
}

static char *bind_single_click(char *data)
{
	o_single_click = atoi(data) != 0;
	return NULL;
}
