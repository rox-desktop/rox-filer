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
#include "view_iface.h"

typedef struct _Tool Tool;

typedef enum {DROP_NONE, DROP_TO_PARENT, DROP_TO_HOME} DropDest;

struct _Tool {
	const gchar	*label;
	const gchar	*name;
	const gchar	*tip;		/* Tooltip */
	void		(*clicked)(GtkWidget *w, FilerWindow *filer_window);
	DropDest	drop_action;
	gboolean	enabled;
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
static GtkWidget *add_button(GtkWidget *bar, Tool *tool,
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
static void toggle_shaded(GtkWidget *widget);
static void option_notify(void);
static GList *build_tool_options(Option *option, xmlNode *node, guchar *label);
static void tally_items(gpointer key, gpointer value, gpointer data);

static Tool all_tools[] = {
	{N_("Close"), GTK_STOCK_CLOSE, N_("Close filer window"),
	 toolbar_close_clicked, DROP_NONE, FALSE,
	 NULL},
	 
	{N_("Up"), GTK_STOCK_GO_UP, N_("Change to parent directory"),
	 toolbar_up_clicked, DROP_TO_PARENT, TRUE,
	 NULL},
	 
	{N_("Home"), GTK_STOCK_HOME, N_("Change to home directory"),
	 toolbar_home_clicked, DROP_TO_HOME, TRUE,
	 NULL},
	
	{N_("Scan"), GTK_STOCK_REFRESH, N_("Rescan directory contents"),
	 toolbar_refresh_clicked, DROP_NONE, TRUE,
	 NULL},
	
	{N_("Size"), GTK_STOCK_ZOOM_IN, N_("Change icon size"),
	 toolbar_size_clicked, DROP_NONE, TRUE,
	 NULL},
	
	{N_("Details"), GTK_STOCK_JUSTIFY_LEFT, N_("Show extra details"),
	 toolbar_details_clicked, DROP_NONE, TRUE,
	 NULL},
	
	{N_("Hidden"), GTK_STOCK_STRIKETHROUGH, N_("Show/hide hidden files"),
	 toolbar_hidden_clicked, DROP_NONE, TRUE,
	 NULL},
	
	{N_("Help"), GTK_STOCK_HELP, N_("Show ROX-Filer help"),
	 toolbar_help_clicked, DROP_NONE, TRUE,
	 NULL},
};


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void toolbar_init(void)
{
	option_add_int(&o_toolbar, "toolbar_type", TOOLBAR_NORMAL);
	option_add_int(&o_toolbar_info, "toolbar_show_info", 1);
	option_add_string(&o_toolbar_disable, "toolbar_disable",
					GTK_STOCK_CLOSE);
	option_add_notify(option_notify);
	
	tooltips = gtk_tooltips_new();

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
	gtk_tooltips_set_tip(tooltips, button, _(tool->tip), NULL);

	icon_widget = gtk_image_new_from_stock(tool->name,
						GTK_ICON_SIZE_LARGE_TOOLBAR);

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), icon_widget, TRUE, TRUE, 0);

	text = gtk_label_new(_(tool->label));
	gtk_box_pack_start(GTK_BOX(vbox), text, FALSE, TRUE, 0);

	gtk_container_add(GTK_CONTAINER(button), vbox);

	gtk_container_set_border_width(GTK_CONTAINER(button), 1);

	g_signal_connect_swapped(button, "clicked",
			G_CALLBACK(toggle_shaded), vbox);

	g_object_set_data(G_OBJECT(button), "tool_name",
				(gchar *) tool->name);

	return button;
}

void toolbar_update_info(FilerWindow *filer_window)
{
	gchar		*label;
	ViewIface	*view;
	int		n_selected;

	if (o_toolbar.int_value == TOOLBAR_NONE || !o_toolbar_info.int_value)
		return;		/* Not showing info */

	if (filer_window->target_cb)
		return;

	view = filer_window->view;

	n_selected = view_count_selected(view);

	if (n_selected == 0)
	{
		gchar *s = NULL;
		int   n_items;

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

		n_items = view_count_items(view);

		if (n_items)
			label = g_strdup_printf("%d %s%s",
					n_items,
					n_items != 1 ? _("items") : _("item"),
					s ? s : "");
		else /* (French plurals work differently for zero) */
			label = g_strdup_printf(_("No items%s"),
					s ? s : "");
		g_free(s);
	}
	else
	{
		double	size = 0;
		ViewIter iter;
		DirItem *item;

		view_get_iter(filer_window->view, &iter, VIEW_ITER_SELECTED);

		while ((item = iter.next(&iter)))
			if (item->base_type != TYPE_DIRECTORY)
				size += (double) item->size;

		label = g_strdup_printf(_("%u selected (%s)"),
				n_selected, format_double_size(size));
	}

	gtk_label_set_text(GTK_LABEL(filer_window->toolbar_text), label);
	g_free(label);
}

/* Create, destroy or recreate toolbar for this window so that it
 * matches the option setting.
 */
void toolbar_update_toolbar(FilerWindow *filer_window)
{
	g_return_if_fail(filer_window != NULL);

	if (filer_window->toolbar)
	{
		gtk_widget_destroy(filer_window->toolbar);
		filer_window->toolbar = NULL;
		filer_window->toolbar_text = NULL;
	}

	if (o_toolbar.int_value != TOOLBAR_NONE)
	{
		filer_window->toolbar = create_toolbar(filer_window);
		gtk_box_pack_start(filer_window->toplevel_vbox,
				filer_window->toolbar, FALSE, TRUE, 0);
		gtk_box_reorder_child(filer_window->toplevel_vbox,
				filer_window->toolbar, 0);
		gtk_widget_show_all(filer_window->toolbar);
	}

	filer_target_mode(filer_window, NULL, NULL, NULL);
	toolbar_update_info(filer_window);
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
		menu_rox_help(NULL, HELP_MANUAL, NULL);
	else
		filer_opendir(make_path(app_dir, "Help")->str, NULL, NULL);
}

static void toolbar_refresh_clicked(GtkWidget *widget,
				    FilerWindow *filer_window)
{
	GdkEvent	*event;

	event = gtk_get_current_event();
	if (event->type == GDK_BUTTON_RELEASE &&
			((GdkEventButton *) event)->button != 1)
	{
		filer_opendir(filer_window->sym_path, filer_window, NULL);
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
		filer_opendir(home_dir, filer_window, NULL);
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
		filer_opendir(filer_window->sym_path, filer_window, NULL);
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
	GtkWidget	*bar;
	GtkWidget	*b;
	int		i;

	bar = gtk_toolbar_new();
	gtk_widget_set_size_request(bar, 100, -1);

	if (o_toolbar.int_value == TOOLBAR_LARGE)
		gtk_toolbar_set_style(GTK_TOOLBAR(bar), GTK_TOOLBAR_BOTH);
	else if (o_toolbar.int_value == TOOLBAR_HORIZONTAL)
		gtk_toolbar_set_style(GTK_TOOLBAR(bar), GTK_TOOLBAR_BOTH_HORIZ);
	else
		gtk_toolbar_set_style(GTK_TOOLBAR(bar), GTK_TOOLBAR_ICONS);

	for (i = 0; i < sizeof(all_tools) / sizeof(*all_tools); i++)
	{
		Tool	*tool = &all_tools[i];

		if (!tool->enabled)
			continue;

		b = add_button(bar, tool, filer_window);
		if (tool->drop_action != DROP_NONE)
			handle_drops(filer_window, b, tool->drop_action);
	}

	filer_window->toolbar_text = gtk_label_new("");
	gtk_misc_set_alignment(GTK_MISC(filer_window->toolbar_text), 0, 0.5);
	gtk_toolbar_append_widget(GTK_TOOLBAR(bar),
				  filer_window->toolbar_text, NULL, NULL);

	return bar;
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

static GtkWidget *add_button(GtkWidget *bar, Tool *tool,
				FilerWindow *filer_window)
{
	GtkWidget 	*button, *icon_widget;

	icon_widget = gtk_image_new_from_stock(tool->name,
						GTK_ICON_SIZE_LARGE_TOOLBAR);

	button = gtk_toolbar_insert_element(GTK_TOOLBAR(bar),
				   GTK_TOOLBAR_CHILD_BUTTON,
				   NULL,
				   _(tool->label),
				   _(tool->tip), NULL,
				   icon_widget,
				   NULL, NULL,	/* CB, userdata */
				   GTK_TOOLBAR(bar)->num_children);
	
	if (o_toolbar.int_value == TOOLBAR_HORIZONTAL)
	{
		GtkWidget *hbox, *label;
		GList	  *kids;
		hbox = GTK_BIN(button)->child;
		kids = gtk_container_get_children(GTK_CONTAINER(hbox));
		label = g_list_nth_data(kids, 1);
		g_list_free(kids);
		
		if (label)
		{
			gtk_box_set_child_packing(GTK_BOX(hbox), label,
						TRUE, TRUE, 0, GTK_PACK_END);
			gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);
		}
	}

	g_signal_connect(button, "clicked",
			G_CALLBACK(tool->clicked), filer_window);

	g_signal_connect(button, "button_press_event",
		G_CALLBACK(toolbar_adjust_pressed), filer_window);
	g_signal_connect(button, "button_release_event",
		G_CALLBACK(toolbar_adjust_released), filer_window);

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
		g_dataset_set_data_full(context, "drop_dest_path",
				g_dirname(filer_window->sym_path), g_free);
	
	g_dataset_set_data(context, "drop_dest_type", (gpointer) drop_dest_dir);
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

static void option_notify(void)
{
	int		i;
	gboolean	changed = FALSE;
	guchar		*list = o_toolbar_disable.value;

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
			
			toolbar_update_toolbar(filer_window);
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
	guint		num_tools = G_N_ELEMENTS(all_tools);
	guint		rows = 2;
	guint		cols = (num_tools + rows - 1) / rows;
	int		i;
	GtkWidget	*hbox, *tool, *table;

	g_return_val_if_fail(option != NULL, NULL);

	hbox = gtk_hbox_new(FALSE, 0);
	table = gtk_table_new(rows, cols, TRUE);

	for (i = 0; (tool = toolbar_tool_option(i)) != NULL; i++)
	{
		guint left = i % cols;
		guint top = i / cols;
	
		gtk_table_attach_defaults(GTK_TABLE(table), tool,
				left, left + 1, top, top + 1);
	}

	gtk_box_pack_start(GTK_BOX(hbox), table, FALSE, FALSE, 0);

	option->update_widget = update_tools;
	option->read_widget = read_tools;
	option->widget = table;

	return g_list_append(NULL, hbox);
}
