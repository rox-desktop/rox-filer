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

/* toolbar.c - for the button bars that go along the tops of windows */

#include "config.h"

#include <string.h>

#include "global.h"

#include "toolbar.h"
#include "options.h"
#include "support.h"
#include "main.h"
#include "menu.h"
#include "dnd.h"
#include "filer.h"
#include "pixmaps.h"
#include "bind.h"
#include "type.h"
#include "dir.h"

typedef struct _Tool Tool;

typedef enum {DROP_NONE, DROP_TO_PARENT, DROP_TO_HOME} DropDest;

struct _Tool {
	guchar		*label;
	guchar		*name;
	guchar		*tip;		/* Tooltip */
	void		(*clicked)(GtkWidget *w, FilerWindow *filer_window);
	DropDest	drop_action;
	gboolean	enabled;
	MaskedPixmap	*icon;
	GtkWidget	**menu;		/* Right-click menu widget addr */
};

ToolbarType o_toolbar = TOOLBAR_NORMAL;
gint o_toolbar_info = TRUE;

static GtkTooltips *tooltips = NULL;

/* TRUE if the button presses (or released) should open a new window,
 * rather than reusing the existing one.
 */
#define NEW_WIN_BUTTON(button_event)	\
  (o_new_window_on_1 ? ((GdkEventButton *) button_event)->button == 1	\
		     : ((GdkEventButton *) button_event)->button != 1)

/* Static prototypes */
static void toolbar_close_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_up_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_home_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_help_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_refresh_clicked(GtkWidget *widget,
				    FilerWindow *filer_window);
static void toolbar_size_clicked(GtkWidget *widget, FilerWindow *filer_window);
static GtkWidget *add_button(GtkWidget *box, Tool *tool,
				FilerWindow *filer_window);
static GtkWidget *create_toolbar(FilerWindow *filer_window);
static gboolean drag_motion(GtkWidget		*widget,
                            GdkDragContext	*context,
                            gint		x,
                            gint		y,
                            guint		time,
			    FilerWindow		*filer_window);
static void drag_leave(GtkWidget	*widget,
                       GdkDragContext	*context,
		       guint32		time,
		       FilerWindow	*filer_window);
static void handle_drops(FilerWindow *filer_window,
			 GtkWidget *button,
			 DropDest dest);
static void coll_selection_changed(Collection *collection, guint time,
					gpointer user_data);
static void recreate_toolbar(FilerWindow *filer_window);
static void toggle_shaded(GtkWidget *widget);
static void option_notify(void);

static Tool all_tools[] = {
	{N_("Close"), "close", N_("Close filer window"),
	 toolbar_close_clicked, DROP_NONE, FALSE,
	 NULL, NULL},
	 
	{N_("Up"), "up", N_("Change to parent directory"),
	 toolbar_up_clicked, DROP_TO_PARENT, TRUE,
	 NULL, NULL},
	 
	{N_("Home"), "home", N_("Change to home directory"),
	 toolbar_home_clicked, DROP_TO_HOME, TRUE,
	 NULL, NULL},
	
	{N_("Scan"), "refresh", N_("Rescan directory contents"),
	 toolbar_refresh_clicked, DROP_NONE, TRUE,
	 NULL, NULL},
	
	{N_("Size"), "zoom", N_("Change icon size"),
	 toolbar_size_clicked, DROP_NONE, TRUE,
	 NULL, NULL},
	
	{N_("Help"), "help", N_("Show ROX-Filer help"),
	 toolbar_help_clicked, DROP_NONE, TRUE,
	 NULL, NULL},
};


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void toolbar_init(void)
{
	int	i;
	
	option_add_int("toolbar_type", o_toolbar, NULL);
	option_add_int("toolbar_show_info", o_toolbar_info, NULL);
	option_add_string("toolbar_disable", "close", NULL);
	option_add_notify(option_notify);
	
	tooltips = gtk_tooltips_new();

	for (i = 0; i < sizeof(all_tools) / sizeof(*all_tools); i++)
	{
		Tool	*tool = &all_tools[i];

		if (!tool->icon)
		{
			guchar	*path;

			path = g_strconcat("pixmaps/",
					tool->name, ".xpm", NULL);
			tool->icon = load_pixmap(path);
			g_free(path);
		}
	}
}

/* Returns a button which can be used to turn a tool on and off.
 * Button has 'tool_name' set to the tool's name.
 * NULL if tool number i is too big.
 */
GtkWidget *toolbar_tool_option(int i)
{
	Tool		*tool = &all_tools[i];
	GtkWidget	*button;
	GtkWidget	*icon_widget, *vbox, *text;

	g_return_val_if_fail(i >= 0, NULL);

	if (i >= sizeof(all_tools) / sizeof(*all_tools))
		return NULL;

	button = gtk_button_new();
	GTK_WIDGET_UNSET_FLAGS(button, GTK_CAN_FOCUS);
	gtk_tooltips_set_tip(tooltips, button, _(tool->tip), NULL);

	icon_widget = gtk_pixmap_new(tool->icon->pixmap,
				     tool->icon->mask);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), icon_widget, TRUE, TRUE, 0);

	text = gtk_label_new(_(tool->label));
	gtk_box_pack_start(GTK_BOX(vbox), text, FALSE, TRUE, 0);

	gtk_container_add(GTK_CONTAINER(button), vbox);

	gtk_container_set_border_width(GTK_CONTAINER(button), 1);
	gtk_misc_set_padding(GTK_MISC(icon_widget), 16, 1);

	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(toggle_shaded), GTK_OBJECT(vbox));

	gtk_object_set_data(GTK_OBJECT(button), "tool_name", tool->name);

	return button;
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

void toolbar_update_info(FilerWindow *filer_window)
{
	if (o_toolbar != TOOLBAR_NONE && o_toolbar_info)
		coll_selection_changed(filer_window->collection,
				GDK_CURRENT_TIME, filer_window);
}


/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static void toolbar_help_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	filer_opendir(make_path(app_dir, "Help")->str);
}

static void toolbar_refresh_clicked(GtkWidget *widget,
				    FilerWindow *filer_window)
{
	GdkEvent	*event;

	event = gtk_get_current_event();
	if (event->type == GDK_BUTTON_RELEASE &&
			((GdkEventButton *) event)->button != 1)
	{
		filer_opendir(filer_window->path);
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
		filer_opendir(home_dir);
	}
	else
		filer_change_to(filer_window, home_dir, NULL);
}

static void toolbar_close_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	GdkEvent	*event;

	g_return_if_fail(filer_window != NULL);

	event = gtk_get_current_event();
	if (event->type == GDK_BUTTON_RELEASE &&
			((GdkEventButton *) event)->button != 1)
	{
		filer_opendir(filer_window->path);
	}
	else
		gtk_widget_destroy(filer_window->window);
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

static void toolbar_size_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	GdkEventButton	*bev;

	bev = (GdkEventButton *) gtk_get_current_event();

	display_change_size(filer_window, 
		bev->type == GDK_BUTTON_RELEASE && bev->button == 1);
}

static GtkWidget *create_toolbar(FilerWindow *filer_window)
{
	GtkWidget	*box;
	GtkWidget	*b;
	int		i;

	box = gtk_hbox_new(FALSE, 0);

	for (i = 0; i < sizeof(all_tools) / sizeof(*all_tools); i++)
	{
		Tool	*tool = &all_tools[i];

		if (!tool->enabled)
			continue;

		b = add_button(box, tool, filer_window);
		if (tool->drop_action != DROP_NONE)
			handle_drops(filer_window, b, tool->drop_action);
	}

	filer_window->toolbar_text = gtk_label_new("");
	gtk_misc_set_alignment(GTK_MISC(filer_window->toolbar_text), 0, 0.5);
	gtk_widget_set_usize(filer_window->toolbar_text, 8, 1);
	gtk_box_pack_start(GTK_BOX(box), filer_window->toolbar_text,
			TRUE, TRUE, 4);

	if (o_toolbar_info)
		gtk_signal_connect(GTK_OBJECT(filer_window->collection),
				"selection_changed",
				GTK_SIGNAL_FUNC(coll_selection_changed),
				(gpointer) filer_window);

	return box;
}

/* This is used to simulate a click when button 3 is used (GtkButton
 * normally ignores this).
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

static gint menu_pressed(GtkWidget *button,
			 GdkEventButton *event,
			 FilerWindow *filer_window)
{
	GtkWidget	*menu;

	if (event->button != 3 && event->button != 2)
		return FALSE;

	menu = gtk_object_get_data(GTK_OBJECT(button), "popup_menu");
	g_return_val_if_fail(menu != NULL, TRUE);

	show_style_menu(filer_window, event, menu);

	return TRUE;
}

static GtkWidget *add_button(GtkWidget *box, Tool *tool,
				FilerWindow *filer_window)
{
	GtkWidget 	*button, *icon_widget;
	GtkSignalFunc	cb = GTK_SIGNAL_FUNC(tool->clicked);

	button = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	GTK_WIDGET_UNSET_FLAGS(button, GTK_CAN_FOCUS);

	if (tool->menu)
	{
		gtk_object_set_data(GTK_OBJECT(button), "popup_menu",
				*tool->menu);
		gtk_signal_connect(GTK_OBJECT(button), "button_press_event",
			GTK_SIGNAL_FUNC(menu_pressed), filer_window);
	}
	else
	{
		gtk_signal_connect(GTK_OBJECT(button), "button_press_event",
			GTK_SIGNAL_FUNC(toolbar_adjust_pressed), filer_window);
		gtk_signal_connect(GTK_OBJECT(button), "button_release_event",
			GTK_SIGNAL_FUNC(toolbar_adjust_released), filer_window);
	}

	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			cb, filer_window);

	gtk_tooltips_set_tip(tooltips, button, _(tool->tip), NULL);

	icon_widget = gtk_pixmap_new(tool->icon->pixmap, tool->icon->mask);

	if (o_toolbar == TOOLBAR_LARGE)
	{
		GtkWidget	*vbox, *text;

		vbox = gtk_vbox_new(FALSE, 0);
		gtk_box_pack_start(GTK_BOX(vbox), icon_widget, TRUE, TRUE, 0);

		text = gtk_label_new(_(tool->label));
		gtk_box_pack_start(GTK_BOX(vbox), text, FALSE, TRUE, 0);

		gtk_container_add(GTK_CONTAINER(button), vbox);
	}
	else
		gtk_container_add(GTK_CONTAINER(button), icon_widget);

	gtk_container_set_border_width(GTK_CONTAINER(button), 1);
	gtk_misc_set_padding(GTK_MISC(icon_widget),
			o_toolbar == TOOLBAR_LARGE ? 16 : 8, 1);
	gtk_box_pack_start(GTK_BOX(box), button, FALSE, TRUE, 0);

	return button;
}

static void toggle_shaded(GtkWidget *widget)
{
	gtk_widget_set_sensitive(widget, !GTK_WIDGET_SENSITIVE(widget));
}

/* Called during the drag when the mouse is in a widget registered
 * as a drop target. Returns TRUE if we can accept the drop.
 */
static gboolean drag_motion(GtkWidget		*widget,
                            GdkDragContext	*context,
                            gint		x,
                            gint		y,
                            guint		time,
			    FilerWindow		*filer_window)
{
	GdkDragAction	action = context->suggested_action;
	DropDest	dest;

	dest = (DropDest) gtk_object_get_data(GTK_OBJECT(widget),
							"toolbar_dest");

	if (dest == DROP_TO_HOME)
		g_dataset_set_data(context, "drop_dest_path", home_dir);
	else
	{
		guchar	*slash, *path;

		slash = strrchr(filer_window->path, '/');
		if (slash == NULL || slash == filer_window->path)
			path = g_strdup("/");
		else
			path = g_strndup(filer_window->path,
					slash - filer_window->path);
		g_dataset_set_data_full(context, "drop_dest_path",
						path, g_free);
	}
	
	g_dataset_set_data(context, "drop_dest_type", drop_dest_dir);
	gdk_drag_status(context, action, time);
	
	dnd_spring_load(context);
	gtk_button_set_relief(GTK_BUTTON(widget), GTK_RELIEF_NORMAL);

	return TRUE;
}

static void drag_leave(GtkWidget	*widget,
                       GdkDragContext	*context,
		       guint32		time,
		       FilerWindow	*filer_window)
{
	gtk_button_set_relief(GTK_BUTTON(widget), GTK_RELIEF_NONE);
	dnd_spring_abort();
}

static void handle_drops(FilerWindow *filer_window,
			 GtkWidget *button,
			 DropDest dest)
{
	make_drop_target(button, 0);
	gtk_signal_connect(GTK_OBJECT(button), "drag_motion",
			GTK_SIGNAL_FUNC(drag_motion), filer_window);
	gtk_signal_connect(GTK_OBJECT(button), "drag_leave",
			GTK_SIGNAL_FUNC(drag_leave), filer_window);
	gtk_object_set_data(GTK_OBJECT(button), "toolbar_dest",
			(gpointer) dest);
}

static void coll_selection_changed(Collection *collection, guint time,
					gpointer user_data)
{
	FilerWindow	*filer_window = (FilerWindow *) user_data;
	gchar		*label;

	if (filer_window->target_cb)
		return;

	if (collection->number_selected == 0)
	{
		guint num_hidden = 0;
		gchar *s = NULL;

		if (filer_window->scanning)
		{
			gtk_label_set_text(
				GTK_LABEL(filer_window->toolbar_text), "");
			return;
		}

		if (!filer_window->show_hidden)
		{
			guint	i = filer_window->directory->items->len;
			DirItem	**items = (DirItem **)
				filer_window->directory->items->pdata;

			while (i--)
				if (items[i]->leafname[0] == '.')
					num_hidden++;
			if (num_hidden)
				s = g_strdup_printf(_(" (%u hidden)"),
						num_hidden);
		}
		label = g_strdup_printf("%d %s%s",
				collection->number_of_items,
				collection->number_of_items != 1
					? _("items") : _("item"),
					s ? s : "");
		g_free(s);
	}
	else
	{
		gint	i = collection->number_of_items;
		double	size = 0;

		while (i--)
		{
			DirItem *item = (DirItem *) collection->items[i].data;
			
			if (collection->items[i].selected 
			    && item->base_type != TYPE_DIRECTORY)
					size += (double) item->size;
		}
		label = g_strdup_printf(_("%u selected (%s)"),
				collection->number_selected,
				format_double_size(size));
	}

	gtk_label_set_text(GTK_LABEL(filer_window->toolbar_text), label);
	g_free(label);
}

static void recreate_toolbar(FilerWindow *filer_window)
{
	GtkWidget	*frame = filer_window->toolbar_frame;
	
	if (GTK_BIN(frame)->child)
	{
		filer_window->toolbar_text = NULL;
		gtk_widget_destroy(((GtkBin *) frame)->child);
	}
	
	if (o_toolbar == TOOLBAR_NONE)
		gtk_widget_hide(frame);
	else
	{
		GtkWidget	*toolbar;

		toolbar = toolbar_new(filer_window);
		gtk_container_add(GTK_CONTAINER(filer_window->toolbar_frame),
				toolbar);
		gtk_widget_show_all(frame);
	}

	filer_target_mode(filer_window, NULL, NULL, NULL);
	toolbar_update_info(filer_window);
}

static void option_notify(void)
{
	ToolbarType	old_type = o_toolbar;
	gint		old_info = o_toolbar_info;
	guchar		*list;
	int		i;
	gboolean	changed = FALSE;

	list = option_get_static_string("toolbar_disable");

	for (i = 0; i < sizeof(all_tools) / sizeof(*all_tools); i++)
	{
		Tool	*tool = &all_tools[i];
		gboolean old = tool->enabled;

		tool->enabled = !in_list(tool->name, list);

		if (old != tool->enabled)
			changed = TRUE;
	}
	
	o_toolbar = option_get_int("toolbar_type");
	o_toolbar_info = option_get_int("toolbar_show_info");

	if (changed || old_type != o_toolbar || old_info != o_toolbar_info)
	{
		GList	*next;

		for (next = all_filer_windows; next; next = next->next)
		{
			FilerWindow *filer_window = (FilerWindow *) next->data;
			
			if (old_info && old_type != TOOLBAR_NONE)
				gtk_signal_disconnect_by_func(
					GTK_OBJECT(filer_window->collection),
					GTK_SIGNAL_FUNC(coll_selection_changed),
					(gpointer) filer_window);

			recreate_toolbar(filer_window);
		}
	}
}
