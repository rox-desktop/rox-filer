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

/* bookmarks.c - handles the bookmarks menu */

#include "config.h"

#include <stdlib.h>
#include <gtk/gtk.h>
#include <string.h>

#include "global.h"

#include "bookmarks.h"
#include "choices.h"
#include "filer.h"
#include "xml.h"
#include "support.h"
#include "gui_support.h"

static XMLwrapper *bookmarks = NULL;
static GtkWidget *bookmarks_window = NULL;

/* Static prototypes */
static void update_bookmarks(void);
static xmlNode *bookmark_find(const gchar *mark);
static void bookmarks_save(void);
static void bookmarks_add(GtkMenuItem *menuitem, gpointer user_data);
static void bookmarks_activate(GtkMenuShell *item, FilerWindow *filer_window);
static GtkWidget *bookmarks_build_menu(FilerWindow *filer_window);
static void position_menu(GtkMenu *menu, gint *x, gint *y,
		   	  gboolean *push_in, gpointer data);
static void cell_edited(GtkCellRendererText *cell,
	     const gchar *path_string,
	     const gchar *new_text,
	     gpointer data);
static void reorder_up(GtkButton *button, GtkTreeView *view);
static void reorder_down(GtkButton *button, GtkTreeView *view);
static void edit_response(GtkWidget *window, gint response,
			  GtkTreeModel *model);
static void edit_delete(GtkButton *button, GtkTreeView *view);


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

/* Shows the bookmarks menu */
void bookmarks_show_menu(FilerWindow *filer_window)
{
	GdkEvent *event;
	GtkMenu *menu;
	int	button = 0;

	event = gtk_get_current_event();
	if (event->type == GDK_BUTTON_RELEASE ||
	    event->type == GDK_BUTTON_PRESS)
		button = ((GdkEventButton *) event)->button;

	menu = GTK_MENU(bookmarks_build_menu(filer_window));
	gtk_menu_popup(menu, NULL, NULL, position_menu, filer_window,
			button, gtk_get_current_event_time());
}

/* Show the Edit Bookmarks dialog */
void bookmarks_edit(void)
{
	GtkListStore *model;
	GtkWidget *list, *frame, *hbox, *button;
	GtkTreeSelection *selection;
	GtkCellRenderer *cell;
	xmlNode *node;
	GtkTreeIter iter;

	if (bookmarks_window)
	{
		gtk_window_present(GTK_WINDOW(bookmarks_window));
		return;
	}

	update_bookmarks();

	bookmarks_window = gtk_dialog_new();

	gtk_dialog_add_button(GTK_DIALOG(bookmarks_window),
			GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
	gtk_dialog_add_button(GTK_DIALOG(bookmarks_window),
			GTK_STOCK_OK, GTK_RESPONSE_OK);

	g_signal_connect(bookmarks_window, "destroy",
			 G_CALLBACK(gtk_widget_destroyed), &bookmarks_window);

	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(bookmarks_window)->vbox),
			frame, TRUE, TRUE, 0);

	model = gtk_list_store_new(1, G_TYPE_STRING);

	list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));
	
	cell = gtk_cell_renderer_text_new();
	g_signal_connect(G_OBJECT(cell), "edited",
		    G_CALLBACK(cell_edited), model);
	g_object_set(G_OBJECT(cell), "editable", TRUE, NULL);
	gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(list), -1,
		NULL, cell, "text", 0, NULL);
	gtk_tree_view_set_reorderable(GTK_TREE_VIEW(list), TRUE);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(list), FALSE);

	node = xmlDocGetRootElement(bookmarks->doc);
	for (node = node->xmlChildrenNode; node; node = node->next)
	{
		GtkTreeIter iter;
		guchar *mark;

		if (node->type != XML_ELEMENT_NODE)
			continue;
		if (strcmp(node->name, "bookmark") != 0)
			continue;

		mark = xmlNodeListGetString(bookmarks->doc,
					    node->xmlChildrenNode, 1);
		if (!mark)
			continue;

		gtk_list_store_append(model, &iter);
		gtk_list_store_set(model, &iter, 0, mark, -1);

		xmlFree(mark);
	}
	
	gtk_widget_set_size_request(list, 300, 100);
	gtk_container_add(GTK_CONTAINER(frame), list);

	hbox = gtk_hbutton_box_new();
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(bookmarks_window)->vbox),
			hbox, FALSE, TRUE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);

	button = gtk_button_new_from_stock(GTK_STOCK_DELETE);
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, TRUE, 0);
	g_signal_connect(button, "clicked", G_CALLBACK(edit_delete), list);

	button = gtk_button_new_from_stock(GTK_STOCK_GO_UP);
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, TRUE, 0);
	g_signal_connect(button, "clicked", G_CALLBACK(reorder_up), list);
	button = gtk_button_new_from_stock(GTK_STOCK_GO_DOWN);
	gtk_box_pack_start(GTK_BOX(hbox), button, FALSE, TRUE, 0);
	g_signal_connect(button, "clicked", G_CALLBACK(reorder_down), list);

	selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(list));
	gtk_tree_selection_set_mode(selection, GTK_SELECTION_MULTIPLE);

	/* Select the first item, otherwise the first click starts edit
	 * mode, which is very confusing!
	 */
	if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(model), &iter))
		gtk_tree_selection_select_iter(selection, &iter);

	g_signal_connect(bookmarks_window, "response",
			 G_CALLBACK(edit_response), model);

	gtk_widget_show_all(bookmarks_window);
}


/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

/* Initialise the bookmarks document to be empty. Does not save. */
static void bookmarks_new(void)
{
	if (bookmarks)
		g_object_unref(G_OBJECT(bookmarks));
	bookmarks = xml_new(NULL);
	bookmarks->doc = xmlNewDoc("1.0");
	xmlDocSetRootElement(bookmarks->doc,
		xmlNewDocNode(bookmarks->doc, NULL, "bookmarks", NULL));
}

static void position_menu(GtkMenu *menu, gint *x, gint *y,
		   	  gboolean *push_in, gpointer data)
{
	FilerWindow *filer_window = (FilerWindow *) data;
	
	gdk_window_get_origin(GTK_WIDGET(filer_window->view)->window, x, y);
}

/* Makes sure that 'bookmarks' is up-to-date, reloading from file if it has
 * changed. If no bookmarks were loaded and there is no file then initialise
 * bookmarks to an empty document.
 */
static void update_bookmarks()
{
	gchar *path;

	/* Update the bookmarks, if possible */
	path = choices_find_path_load("Bookmarks.xml", PROJECT);
	if (path)
	{
		XMLwrapper *wrapper;
		wrapper = xml_cache_load(path);
		if (wrapper)
		{
			if (bookmarks)
				g_object_unref(bookmarks);
			bookmarks = wrapper;
		}

		g_free(path);
	}

	if (!bookmarks)
		bookmarks_new();
}

/* Return the node for the 'mark' bookmark */
static xmlNode *bookmark_find(const gchar *mark)
{
	xmlNode *node;

	update_bookmarks();

	node = xmlDocGetRootElement(bookmarks->doc);

	for (node = node->xmlChildrenNode; node; node = node->next)
	{
		gchar *path;
		gboolean same;

		if (node->type != XML_ELEMENT_NODE)
			continue;
		if (strcmp(node->name, "bookmark") != 0)
			continue;

		path = xmlNodeListGetString(bookmarks->doc,
					node->xmlChildrenNode, 1);
		if (!path)
			continue;

		same = strcmp(mark, path) == 0;
		xmlFree(path);

		if (same)
			return node;
	}

	return NULL;
}

/* Save the bookmarks to a file */
static void bookmarks_save()
{
	guchar	*save_path;

	save_path = choices_find_path_save("Bookmarks.xml", PROJECT, TRUE);
	if (save_path)
	{
		save_xml_file(bookmarks->doc, save_path);
		g_free(save_path);
	}

	if (bookmarks_window)
		gtk_widget_destroy(bookmarks_window);
}

/* Add a bookmark if it doesn't already exist, and save the
 * bookmarks.
 */
static void bookmarks_add(GtkMenuItem *menuitem, gpointer user_data)
{
	FilerWindow *filer_window = (FilerWindow *) user_data;
	xmlNode	*bookmark;
	guchar	*name;

	name = filer_window->sym_path;
	if (bookmark_find(name))
		return;

	bookmark = xmlNewChild(xmlDocGetRootElement(bookmarks->doc),
				NULL, "bookmark", name);

	bookmarks_save();
}

/* Called when a bookmark has been selected (right or left click) */
static void bookmarks_activate(GtkMenuShell *item, FilerWindow *filer_window)
{
	const gchar *mark;
	GtkLabel *label;

	label = GTK_LABEL(GTK_BIN(item)->child);
	mark = gtk_label_get_text(label);

	if (strcmp(mark, filer_window->sym_path) != 0)
		filer_change_to(filer_window, mark, NULL);
}

static void edit_delete(GtkButton *button, GtkTreeView *view)
{
	GtkTreeModel *model;
	GtkListStore *list;
	GtkTreeSelection *selection;
	GtkTreeIter iter;
	gboolean more, any = FALSE;

	model = gtk_tree_view_get_model(view);
	list = GTK_LIST_STORE(model);

	selection = gtk_tree_view_get_selection(view);

	more = gtk_tree_model_get_iter_first(model, &iter);

	while (more)
	{
		GtkTreeIter old = iter;

		more = gtk_tree_model_iter_next(model, &iter);

		if (gtk_tree_selection_iter_is_selected(selection, &old))
		{
			any = TRUE;
			gtk_list_store_remove(list, &old);
		}
	}

	if (!any)
	{
		report_error(_("You should first select some rows to delete"));
		return;
	}
}

static void reorder(GtkTreeView *view, int dir)
{
	GtkTreeModel *model;
	GtkListStore *list;
	GtkTreePath *cursor = NULL;
	GtkTreeIter iter, old, new;
	GValue value = {0};
	gboolean    ok;

	g_return_if_fail(view != NULL);
	g_return_if_fail(dir == 1 || dir == -1);

	model = gtk_tree_view_get_model(view);
	list = GTK_LIST_STORE(model);

	gtk_tree_view_get_cursor(view, &cursor, NULL);
	if (!cursor)
	{
		report_error(_("Put the cursor on an entry in the "
			       "list to move it"));
		return;
	}

	gtk_tree_model_get_iter(model, &old, cursor);
	if (dir > 0)
	{
		gtk_tree_path_next(cursor);
		ok = gtk_tree_model_get_iter(model, &iter, cursor);
	}
	else
	{
		ok = gtk_tree_path_prev(cursor);
		if (ok)
			gtk_tree_model_get_iter(model, &iter, cursor);
	}
	if (!ok)
	{
		gtk_tree_path_free(cursor);
		report_error(_("This item is already at the end"));
		return;
	}

	gtk_tree_model_get_value(model, &old, 0, &value);
	if (dir > 0)
		gtk_list_store_insert_after(list, &new, &iter);
	else
		gtk_list_store_insert_before(list, &new, &iter);
	gtk_list_store_set(list, &new, 0, g_value_get_string(&value), -1);
	gtk_list_store_remove(list, &old);

	g_value_unset(&value);

	gtk_tree_view_set_cursor(view, cursor, 0, FALSE);
	gtk_tree_path_free(cursor);
}

static void reorder_up(GtkButton *button, GtkTreeView *view)
{
	reorder(view, -1);
}

static void reorder_down(GtkButton *button, GtkTreeView *view)
{
	reorder(view, 1);
}

static void edit_response(GtkWidget *window, gint response, GtkTreeModel *model)
{
	GtkTreeIter iter;

	if (response != GTK_RESPONSE_OK)
	{
		gtk_widget_destroy(window);
		return;
	}

	bookmarks_new();

	if (gtk_tree_model_get_iter_first(model, &iter))
	{
		GValue value = {0};
		xmlNode *root = xmlDocGetRootElement(bookmarks->doc);

		do
		{
			xmlNode *bookmark;

			gtk_tree_model_get_value(model, &iter, 0, &value);
			bookmark = xmlNewChild(root, NULL, "bookmark",
					g_value_get_string(&value));
			g_value_unset(&value);
		} while (gtk_tree_model_iter_next(model, &iter));
	}

	bookmarks_save();

	gtk_widget_destroy(window);
}

static void cell_edited(GtkCellRendererText *cell,
	     const gchar *path_string,
	     const gchar *new_text,
	     gpointer data)
{
	GtkTreeModel *model = (GtkTreeModel *) data;
	GtkTreePath *path;
	GtkTreeIter iter;

	path = gtk_tree_path_new_from_string(path_string);
	gtk_tree_model_get_iter(model, &iter, path);
	gtk_tree_path_free(path);

	gtk_list_store_set(GTK_LIST_STORE(model), &iter, 0, new_text, -1);
}

static void activate_edit(GtkMenuShell *item, gpointer data)
{
	bookmarks_edit();
}

/* Builds the bookmarks' menu. Done whenever the bookmarks icon has been
 * clicked.
 */
static GtkWidget *bookmarks_build_menu(FilerWindow *filer_window)
{
	GtkWidget *menu;
	GtkWidget *item;
	xmlNode *node;
	gboolean need_separator = TRUE;

	menu = gtk_menu_new();

	item = gtk_menu_item_new_with_label(_("Add new bookmark"));
	g_signal_connect(item, "activate",
			 G_CALLBACK(bookmarks_add), filer_window);
	gtk_widget_show(item);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
	gtk_menu_shell_select_item(GTK_MENU_SHELL(menu), item);

	item = gtk_menu_item_new_with_label(_("Edit bookmarks"));
	g_signal_connect(item, "activate", G_CALLBACK(activate_edit), NULL);
	gtk_widget_show(item);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	/* Now add all the bookmarks to the menu */

	update_bookmarks();

	node = xmlDocGetRootElement(bookmarks->doc);

	for (node = node->xmlChildrenNode; node; node = node->next)
	{
		gchar *mark;

		if (node->type != XML_ELEMENT_NODE)
			continue;
		if (strcmp(node->name, "bookmark") != 0)
			continue;

		mark = xmlNodeListGetString(bookmarks->doc,
					    node->xmlChildrenNode, 1);
		if (!mark)
			continue;
		item = gtk_menu_item_new_with_label(mark);
		xmlFree(mark);

		g_signal_connect(item, "activate",
				G_CALLBACK(bookmarks_activate),
				filer_window);

		gtk_widget_show(item);

		if (need_separator)
		{
			GtkWidget *sep;
			sep = gtk_separator_menu_item_new();
			gtk_widget_show(sep);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), sep);
			need_separator = FALSE;
		}

		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	}

	return menu;
}
