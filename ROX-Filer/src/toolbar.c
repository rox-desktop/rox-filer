/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2000, Thomas Leonard, <tal197@ecs.soton.ac.uk>.
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

/* toolbar.c - for the button bars that go along the tops of windows */

#include "config.h"

#include "toolbar.h"
#include "options.h"
#include "support.h"
#include "main.h"

extern int collection_menu_button;

/* Options bits */
static GtkWidget *create_options();
static void update_options();
static void set_options();
static void save_options();
static char *toolbar_type(char *data);

static OptionsSection options =
{
	N_("Toolbar options"),
	create_options,
	update_options,
	set_options,
	save_options
};

ToolbarType o_toolbar = TOOLBAR_NORMAL;

static GtkWidget *menu_toolbar;
static GtkTooltips *tooltips = NULL;

/* TRUE if the button presses (or released) should open a new window,
 * rather than reusing the existing one.
 */
#define NEW_WIN_BUTTON(button_event)	\
  (o_new_window_on_1 ? ((GdkEventButton *) button_event)->button == 1	\
		     : ((GdkEventButton *) button_event)->button != 1)

/* Static prototypes */
static void toolbar_up_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_home_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void add_button(GtkWidget *box, MaskedPixmap *icon,
			GtkSignalFunc cb, FilerWindow *filer_window,
			char *label, char *tip);
static GtkWidget *create_toolbar(FilerWindow *filer_window);


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void toolbar_init(void)
{
	options_sections = g_slist_prepend(options_sections, &options);
	option_register("toolbar_type", toolbar_type);

	tooltips = gtk_tooltips_new();
}

/* Create a new toolbar widget, suitable for adding to a filer window,
 * and return it.
 */
GtkWidget *toolbar_new(FilerWindow *filer_window)
{
	g_return_val_if_fail(filer_window != NULL, NULL);
	g_return_val_if_fail(o_toolbar != TOOLBAR_NONE, NULL);

	return create_toolbar(filer_window);
}



/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static void toolbar_help_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	filer_opendir(make_path(app_dir, "Help")->str, PANEL_NO);
}

static void toolbar_refresh_clicked(GtkWidget *widget,
				    FilerWindow *filer_window)
{
	GdkEvent	*event;

	event = gtk_get_current_event();
	if (event->type == GDK_BUTTON_RELEASE &&
			((GdkEventButton *) event)->button != 1)
	{
		filer_opendir(filer_window->path, PANEL_NO);
	}
	else
	{
		full_refresh();
		filer_update_dir(filer_window, TRUE);
	}
}

static void toolbar_home_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	GdkEvent	*event;

	event = gtk_get_current_event();
	if (event->type == GDK_BUTTON_RELEASE && NEW_WIN_BUTTON(event))
	{
		filer_opendir(home_dir, PANEL_NO);
	}
	else
		filer_change_to(filer_window, home_dir, NULL);
}

static void toolbar_up_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	GdkEvent	*event;

	event = gtk_get_current_event();
	if (event->type == GDK_BUTTON_RELEASE && NEW_WIN_BUTTON(event))
	{
		filer_open_parent(filer_window);
	}
	else
		change_to_parent(filer_window);
}

static GtkWidget *create_toolbar(FilerWindow *filer_window)
{
	GtkWidget	*frame, *box;

	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_OUT);

	box = gtk_hbutton_box_new();
	gtk_button_box_set_child_size_default(16, 16);
	gtk_hbutton_box_set_spacing_default(0);
	gtk_button_box_set_layout(GTK_BUTTON_BOX(box), GTK_BUTTONBOX_START);

	gtk_container_add(GTK_CONTAINER(frame), box);

	add_button(box, im_up_icon,
			GTK_SIGNAL_FUNC(toolbar_up_clicked),
			filer_window,
			_("Up"), _("Change to parent directory"));
	add_button(box, im_home_icon,
			GTK_SIGNAL_FUNC(toolbar_home_clicked),
			filer_window,
			_("Home"), _("Change to home directory"));
	add_button(box, im_refresh_icon,
			GTK_SIGNAL_FUNC(toolbar_refresh_clicked),
			filer_window,
			_("Scan"), _("Rescan directory contents"));
	add_button(box, im_help,
			GTK_SIGNAL_FUNC(toolbar_help_clicked),
			filer_window,
			_("Help"), _("Show ROX-Filer help"));

	return frame;
}

/* This is used to simulate a click when button 3 is used (GtkButton
 * normally ignores this). Currently, this button does not pop in -
 * this may be fixed in future versions of GTK+.
 */
static gint toolbar_other_button = 0;
static gint toolbar_adjust_pressed(GtkButton *button,
				GdkEventButton *event,
				FilerWindow *filer_window)
{
	gint	b = event->button;

	if ((b == 2 || b == 3) && toolbar_other_button == 0)
	{
		toolbar_other_button = event->button;
		gtk_grab_add(GTK_WIDGET(button));
		gtk_button_pressed(button);
	}

	return TRUE;
}

static gint toolbar_adjust_released(GtkButton *button,
				GdkEventButton *event,
				FilerWindow *filer_window)
{
	if (event->button == toolbar_other_button)
	{
		toolbar_other_button = 0;
		gtk_grab_remove(GTK_WIDGET(button));
		gtk_button_released(button);
	}

	return TRUE;
}

static void add_button(GtkWidget *box, MaskedPixmap *icon,
			GtkSignalFunc cb, FilerWindow *filer_window,
			char *label, char *tip)
{
	GtkWidget 	*button, *icon_widget;

	button = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	GTK_WIDGET_UNSET_FLAGS(button, GTK_CAN_FOCUS);

	gtk_signal_connect(GTK_OBJECT(button), "button_press_event",
			GTK_SIGNAL_FUNC(toolbar_adjust_pressed), filer_window);
	gtk_signal_connect(GTK_OBJECT(button), "button_release_event",
			GTK_SIGNAL_FUNC(toolbar_adjust_released), filer_window);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			cb, filer_window);

	gtk_tooltips_set_tip(tooltips, button, tip, NULL);

	icon_widget = gtk_pixmap_new(icon->pixmap, icon->mask);

	if (o_toolbar == TOOLBAR_LARGE)
	{
		GtkWidget	*vbox, *text;

		vbox = gtk_vbox_new(FALSE, 0);
		gtk_box_pack_start(GTK_BOX(vbox), icon_widget, TRUE, TRUE, 0);

		text = gtk_label_new(label);
		gtk_box_pack_start(GTK_BOX(vbox), text, FALSE, TRUE, 0);

		gtk_container_add(GTK_CONTAINER(button), vbox);
	}
	else
		gtk_container_add(GTK_CONTAINER(button), icon_widget);

	gtk_container_add(GTK_CONTAINER(box), button);
}

/* Build up some option widgets to go in the options dialog, but don't
 * fill them in yet.
 */
static GtkWidget *create_options(void)
{
	GtkWidget	*vbox, *menu, *hbox;

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 4);

	hbox = gtk_hbox_new(FALSE, 4);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

	gtk_box_pack_start(GTK_BOX(hbox),
			gtk_label_new(_("Toolbar type for new windows")),
			FALSE, TRUE, 0);
	menu_toolbar = gtk_option_menu_new();
	menu = gtk_menu_new();
	gtk_menu_append(GTK_MENU(menu),
			gtk_menu_item_new_with_label(_("None")));
	gtk_menu_append(GTK_MENU(menu),
			gtk_menu_item_new_with_label(_("Normal")));
	gtk_menu_append(GTK_MENU(menu),
			gtk_menu_item_new_with_label(_("Large")));
	gtk_option_menu_set_menu(GTK_OPTION_MENU(menu_toolbar), menu);
	gtk_box_pack_start(GTK_BOX(hbox), menu_toolbar, TRUE, TRUE, 0);

	return vbox;
}

/* Reflect current state by changing the widgets in the options box */
static void update_options()
{
	gtk_option_menu_set_history(GTK_OPTION_MENU(menu_toolbar), o_toolbar);
}

/* Set current values by reading the states of the widgets in the options box */
static void set_options()
{
	GtkWidget 	*item, *menu;
	GList		*list;
	
	menu = gtk_option_menu_get_menu(GTK_OPTION_MENU(menu_toolbar));
	item = gtk_menu_get_active(GTK_MENU(menu));
	list = gtk_container_children(GTK_CONTAINER(menu));
	o_toolbar = (ToolbarType) g_list_index(list, item);
	g_list_free(list);
}

static void save_options()
{
	option_write("toolbar_type", o_toolbar == TOOLBAR_NONE ? "None" :
				     o_toolbar == TOOLBAR_NORMAL ? "Normal" :
				     o_toolbar == TOOLBAR_LARGE ? "Large" :
				     "Unknown");
}

static char *toolbar_type(char *data)
{
	if (g_strcasecmp(data, "None") == 0)
		o_toolbar = TOOLBAR_NONE;
	else if (g_strcasecmp(data, "Normal") == 0)
		o_toolbar = TOOLBAR_NORMAL;
	else if (g_strcasecmp(data, "Large") == 0)
		o_toolbar = TOOLBAR_LARGE;
	else
		return _("Unknown toolbar type");

	return NULL;
}

