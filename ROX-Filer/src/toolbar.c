/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2002, the ROX-Filer team.
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

#include "collection.h"
#include "toolbar.h"
#include "options.h"
#include "support.h"
#include "main.h"
#include "menu.h"
#include "dnd.h"
#include "filer.h"
#include "display.h"
#include "pixmaps.h"
#include "bind.h"
#include "type.h"
#include "dir.h"
#include "diritem.h"

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

Option o_toolbar, o_toolbar_info, o_toolbar_disable;

static GtkTooltips *tooltips = NULL;

/* TRUE if the button presses (or released) should open a new window,
 * rather than reusing the existing one.
 */
#define NEW_WIN_BUTTON(button_event)	\
  (o_new_button_1.int_value		\
   	? ((GdkEventButton *) button_event)->button == 1	\
	: ((GdkEventButton *) button_event)->button != 1)

/* Static prototypes */
static void toolbar_close_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_up_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_home_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_help_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_refresh_clicked(GtkWidget *widget,
				    FilerWindow *filer_window);
static void toolbar_size_clicked(GtkWidget *widget, FilerWindow *filer_window);
static void toolbar_details_clicked(GtkWidget *widget,
				    FilerWindow *filer_window);
static void toolbar_hidden_clicked(GtkWidget *widget,
				   FilerWindow *filer_window);
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
static GList *build_tool_options(Option *option, xmlNode *node, guchar *label);

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
	
	{N_("Details"), "details", N_("Show extra details"),
	 toolbar_details_clicked, DROP_NONE, TRUE,
	 NULL, NULL},
	
	{N_("Hidden"), "hidden", N_("Show/hide hidden files"),
	 toolbar_hidden_clicked, DROP_NONE, TRUE,
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
	
	option_add_int(&o_toolbar, "toolbar_type", TOOLBAR_NORMAL);
	option_add_int(&o_toolbar_info, "toolbar_show_info", 1);
	option_add_string(&o_toolbar_disable, "toolbar_disable", "close");
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

	option_register_widget("tool-options", build_tool_options);
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

	icon_widget = gtk_image_new_from_pixmap(tool->icon->pixmap,
						tool->icon->mask);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), icon_widget, TRUE, TRUE, 0);

	text = gtk_label_new(_(tool->label));
	gtk_box_pack_start(GTK_BOX(vbox), text, FALSE, TRUE, 0);

	gtk_container_add(GTK_CONTAINER(button), vbox);

	gtk_container_set_border_width(GTK_CONTAINER(button), 1);
	gtk_misc_set_padding(GTK_MISC(icon_widget), 16, 1);

	g_signal_connect_swapped(button, "clicked",
			G_CALLBACK(toggle_shaded), vbox);

	g_object_set_data(G_OBJECT(button), "tool_name", tool->name);

	return button;
}

/* Create a new toolbar widget, suitable for adding to a filer window,
 * and return it.
 */
GtkWidget *toolbar_new(FilerWindow *filer_window)
{
	g_return_val_if_fail(filer_window != NULL, NULL);
	g_return_val_if_fail(o_toolbar.int_value != TOOLBAR_NONE, NULL);

	return create_toolbar(filer_window);
}

void toolbar_update_info(FilerWindow *filer_window)
{
	if (o_toolbar.int_value != TOOLBAR_NONE && o_toolbar_info.int_value)
		coll_selection_changed(filer_window->collection,
				GDK_CURRENT_TIME, NULL);
}


/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static void toolbar_help_clicked(GtkWidget *widget, FilerWindow *filer_window)
{
	GdkEvent	*event;

	event = gtk_get_current_event();
	if (event->type == GDK_BUTTON_RELEASE &&
			((GdkEventButton *) event)->button != 1)
		filer_change_to(filer_window,
				make_path(app_dir, "Help")->str, NULL);
	else
		filer_opendir(make_path(app_dir, "Help")->str, NULL);
}

static void toolbar_refresh_clicked(GtkWidget *widget,
				    FilerWindow *filer_window)
{
	GdkEvent	*event;

	event = gtk_get_current_event();
	if (event->type == GDK_BUTTON_RELEASE &&
			((GdkEventButton *) event)->button != 1)
	{
		filer_opendir(filer_window->path, filer_window);
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
		filer_opendir(home_dir, filer_window);
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
		filer_opendir(filer_window->path, filer_window);
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

static void toolbar_details_clicked(GtkWidget *widget,
				    FilerWindow *filer_window)
{
	if (filer_window->details_type == DETAILS_NONE)
		display_set_layout(filer_window,
				filer_window->display_style,
				DETAILS_SUMMARY);
	else
		display_set_layout(filer_window,
				filer_window->display_style,
				DETAILS_NONE);
}

static void toolbar_hidden_clicked(GtkWidget *widget,
				   FilerWindow *filer_window)
{
	display_set_hidden(filer_window, !filer_window->show_hidden);
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
	gtk_widget_set_size_request(filer_window->toolbar_text, 8, -1);
	gtk_box_pack_start(GTK_BOX(box), filer_window->toolbar_text,
			TRUE, TRUE, 4);

	if (o_toolbar_info.int_value)
		g_signal_connect_object(filer_window->collection,
				"selection_changed",
				G_CALLBACK(coll_selection_changed), box, 0);

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

		return TRUE;
	}

	return FALSE;
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

		return TRUE;
	}

	return FALSE;
}

#if 0
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
#endif

static GtkWidget *add_button(GtkWidget *box, Tool *tool,
				FilerWindow *filer_window)
{
	GtkWidget 	*button, *icon_widget;

	button = gtk_button_new();
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	GTK_WIDGET_UNSET_FLAGS(button, GTK_CAN_FOCUS);

	if (tool->menu)
	{
		g_warning("Not implemented");
#if 0
		gtk_object_set_data(GTK_OBJECT(button), "popup_menu",
				*tool->menu);
		gtk_signal_connect(GTK_OBJECT(button), "button_press_event",
			GTK_SIGNAL_FUNC(menu_pressed), filer_window);
#endif
	}
	else
	{
		g_signal_connect(button, "button_press_event",
			G_CALLBACK(toolbar_adjust_pressed), filer_window);
		g_signal_connect(button, "button_release_event",
			G_CALLBACK(toolbar_adjust_released), filer_window);
	}

	g_signal_connect(button, "clicked",
			G_CALLBACK(tool->clicked), filer_window);

	gtk_tooltips_set_tip(tooltips, button, _(tool->tip), NULL);

	icon_widget = gtk_image_new_from_pixmap(tool->icon->pixmap,
						tool->icon->mask);

	if (o_toolbar.int_value == TOOLBAR_LARGE)
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
			o_toolbar.int_value == TOOLBAR_LARGE ? 16 : 8, 1);
	gtk_box_pack_start(GTK_BOX(box), button, FALSE, TRUE, 0);

	return button;
}

static void toggle_shaded(GtkWidget *widget)
{
	gtk_widget_set_sensitive(widget, !GTK_WIDGET_SENSITIVE(widget));
	option_check_widget(&o_toolbar_disable);
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

	dest = (DropDest) g_object_get_data(G_OBJECT(widget), "toolbar_dest");

	if (dest == DROP_TO_HOME)
		g_dataset_set_data(context, "drop_dest_path",
				   (gchar *) home_dir);
	else
	{
		gchar	*slash, *path;

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
	
	dnd_spring_load(context, filer_window);
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
	g_signal_connect(button, "drag_motion",
			G_CALLBACK(drag_motion), filer_window);
	g_signal_connect(button, "drag_leave",
			G_CALLBACK(drag_leave), filer_window);
	g_object_set_data(G_OBJECT(button), "toolbar_dest", (gpointer) dest);
}

static void tally_items(gpointer key, gpointer value, gpointer data)
{
	guchar *leafname = (guchar *) key;
	int    *tally = (int *) data;

	if (leafname[0] == '.')
		(*tally)++;
}

static void coll_selection_changed(Collection *collection, guint time,
					gpointer user_data)
{
	FilerWindow	*filer_window;
	gchar		*label;

	filer_window = g_object_get_data(G_OBJECT(collection), "filer_window");

	g_return_if_fail(filer_window != NULL);

	if (filer_window->target_cb)
		return;

	if (collection->number_selected == 0)
	{
		gchar *s = NULL;

		if (filer_window->scanning)
		{
			gtk_label_set_text(
				GTK_LABEL(filer_window->toolbar_text), "");
			return;
		}

		if (!filer_window->show_hidden)
		{
			GHashTable *hash = filer_window->directory->known_items;
			int	   tally = 0;

			g_hash_table_foreach(hash, tally_items, &tally);

			if (tally)
				s = g_strdup_printf(_(" (%u hidden)"), tally);
		}

		if (collection->number_of_items)
			label = g_strdup_printf("%d %s%s",
					collection->number_of_items,
					collection->number_of_items != 1
						? _("items") : _("item"),
						s ? s : "");
		else /* (French plurals work differently for zero) */
			label = g_strdup_printf(_("No items%s"),
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
	
	if (o_toolbar.int_value == TOOLBAR_NONE)
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
	guchar		*list;
	int		i;
	gboolean	changed = FALSE;

	list = o_toolbar_disable.value;

	for (i = 0; i < sizeof(all_tools) / sizeof(*all_tools); i++)
	{
		Tool	*tool = &all_tools[i];
		gboolean old = tool->enabled;

		tool->enabled = !in_list(tool->name, list);

		if (old != tool->enabled)
			changed = TRUE;
	}

	if (changed || o_toolbar.has_changed || o_toolbar_info.has_changed)
	{
		GList	*next;

		for (next = all_filer_windows; next; next = next->next)
		{
			FilerWindow *filer_window = (FilerWindow *) next->data;
			
			recreate_toolbar(filer_window);
		}
	}
}

static void update_tools(Option *option)
{
	GList	*next, *kids;

	kids = gtk_container_get_children(GTK_CONTAINER(option->widget));

	for (next = kids; next; next = next->next)
	{
		GtkWidget	*kid = (GtkWidget *) next->data;
		guchar		*name;

		name = g_object_get_data(G_OBJECT(kid), "tool_name");

		g_return_if_fail(name != NULL);
		
		gtk_widget_set_sensitive(GTK_BIN(kid)->child,
					 !in_list(name, option->value));
	}

	g_list_free(kids);
}

static guchar *read_tools(Option *option)
{
	GList	*next, *kids;
	GString	*list;
	guchar	*retval;

	list = g_string_new(NULL);

	kids = gtk_container_get_children(GTK_CONTAINER(option->widget));

	for (next = kids; next; next = next->next)
	{
		GtkObject	*kid = (GtkObject *) next->data;
		guchar		*name;

		if (!GTK_WIDGET_SENSITIVE(GTK_BIN(kid)->child))
		{
			name = g_object_get_data(G_OBJECT(kid), "tool_name");
			g_return_val_if_fail(name != NULL, list->str);

			if (list->len)
				g_string_append(list, ", ");
			g_string_append(list, name);
		}
	}

	g_list_free(kids);
	retval = list->str;
	g_string_free(list, FALSE);

	return retval;
}

static GList *build_tool_options(Option *option, xmlNode *node, guchar *label)
{
	int		i = 0;
	GtkWidget	*hbox, *tool;

	g_return_val_if_fail(option != NULL, NULL);

	hbox = gtk_hbox_new(FALSE, 0);

	while ((tool = toolbar_tool_option(i++)))
		gtk_box_pack_start(GTK_BOX(hbox), tool, FALSE, TRUE, 0);

	option->update_widget = update_tools;
	option->read_widget = read_tools;
	option->widget = hbox;

	return g_list_append(NULL, hbox);
}
