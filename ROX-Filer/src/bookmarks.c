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

#include "global.h"

#include "bookmarks.h"
#include "choices.h"
#include "filer.h"
#include "xml.h"
#include "support.h"

static XMLwrapper *bookmarks = NULL;

/* Static prototypes */
static void update_bookmarks(void);
static xmlNode *bookmark_find(const gchar *mark);
static void bookmarks_save(void);
static void bookmarks_add(GtkMenuItem *menuitem, gpointer user_data);
static void bookmarks_delete(GtkMenuItem *menuitem, const gchar *mark);
static GtkWidget *bookmarks_build_submenu(const gchar *mark);
static void bookmarks_show_submenu(const gchar *mark, GdkEventButton *event);
static void bookmarks_activate(GtkMenuShell *menushell,
			       FilerWindow *filer_window);
static GtkWidget *bookmarks_build_menu(FilerWindow *filer_window);
static void position_menu(GtkMenu *menu, gint *x, gint *y,
		   	  gboolean *push_in, gpointer data);


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


/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

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
	{
		bookmarks = xml_new(NULL);
		bookmarks->doc = xmlNewDoc("1.0");

		xmlDocSetRootElement(bookmarks->doc,
			xmlNewDocNode(bookmarks->doc, NULL, "bookmarks", NULL));
		return;
	}
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

/* Delete a bookmark and save the bookmarks */
static void bookmarks_delete(GtkMenuItem *menuitem, const gchar *mark)
{
	xmlNode	*bookmark;

	bookmark = bookmark_find(mark);

	g_return_if_fail(bookmark != NULL);

	xmlUnlinkNode(bookmark);
	xmlFreeNode(bookmark);

	bookmarks_save();
}

/* Builds the submenu that appears when right clicking a bookmark.
 * Currently only Delete available...
 */
static GtkWidget *bookmarks_build_submenu(const gchar *mark)
{
	GtkWidget *menu;
	GtkWidget *item;

	menu = gtk_menu_new();
	item = gtk_menu_item_new_with_label(_("Delete"));
	g_signal_connect(item, "activate",
			G_CALLBACK(bookmarks_delete),
			(gpointer) mark);
	gtk_widget_show(item);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	return menu;
}

/* Shows the submenu that appears when right clicking a bookmark. */
static void bookmarks_show_submenu(const gchar *mark, GdkEventButton *event)
{
	GtkMenu *submenu;

	submenu = GTK_MENU(bookmarks_build_submenu(mark));

	gtk_menu_popup(submenu, NULL, NULL, NULL, NULL,
			event->button, event->time);
}

/* Called when a bookmark has been selected (right or left click) */
static void bookmarks_activate(GtkMenuShell *menushell,
			       FilerWindow *filer_window)
{
	GdkEvent *event;
	const gchar *mark;
	GtkLabel *label;

	label = GTK_LABEL(GTK_BIN(menushell)->child);
	mark = gtk_label_get_text(label);
	event = gtk_get_current_event();

	if (event->type == GDK_BUTTON_RELEASE &&
			((GdkEventButton *) event)->button == 3)
	{
		/* Right click */
		bookmarks_show_submenu(mark, (GdkEventButton *) event);
	}
	else
	{
		/* Left click, simply change the path  */
		if (strcmp(mark, filer_window->sym_path) != 0)
			filer_change_to(filer_window, mark, NULL);
	}
}

/* Builds the bookmarks' menu. Done whenever the bookmarks icon has been
 * clicked.
 */
static GtkWidget *bookmarks_build_menu(FilerWindow *filer_window)
{
	GtkWidget *menu;
	GtkWidget *item;
	xmlNode *node;

	menu = gtk_menu_new();
	item = gtk_menu_item_new_with_label(_("Add new bookmark"));
	g_signal_connect(item, "activate",
			 G_CALLBACK(bookmarks_add), filer_window);
	gtk_widget_show(item);
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	item = gtk_separator_menu_item_new();
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
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	}

	return menu;
}
