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

/* appmenu.c - handles application-specific menus read from AppInfo.xml */

#include "config.h"

#include <gtk/gtk.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <parser.h>
#include <tree.h>

#include "global.h"

#include "fscache.h"
#include "gui_support.h"
#include "support.h"
#include "menu.h"
#include "filer.h"
#include "appmenu.h"
#include "dir.h"
#include "type.h"
#include "appinfo.h"

/* Static prototypes */
static void apprun_menu(GtkWidget *item, gpointer data);
static GtkWidget *create_menu_item(char *label, char *option);

/* There can only be one menu open at a time... we store: */
static GtkWidget *current_menu = NULL;	/* The GtkMenu */
static GList *current_items = NULL;	/* The GtkMenuItems we added to it */
static guchar *current_app_path = NULL;	/* The path of the application */

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

/* Removes all appmenu menu items */
void appmenu_remove(void)
{
	GList *next;

	if (!current_menu)
		return;

	for (next = current_items; next; next = next->next)
		gtk_widget_destroy((GtkWidget *) next->data);

	g_free(current_app_path);
	current_app_path = NULL;
	current_menu = NULL;

	g_list_free(current_items);
	current_items = NULL;
}

/* Add AppMenu entries to 'menu', if appropriate.
 * This function modifies the menu stored in "menu".
 * 'app_dir' is the pathname of the application directory, and 'item'
 * is the corresponding DirItem.
 * Call appmenu_remove() to undo the effect.
 */
void appmenu_add(guchar *app_dir, DirItem *item, GtkWidget *menu)
{
	AppInfo	*ai;
	struct _xmlNode	*node;
	GList	*next;
	GtkWidget *sep;

	g_return_if_fail(menu != NULL);

	/* Should have called appmenu_remove() already... */
	g_return_if_fail(current_menu == NULL);

	ai = appinfo_get(app_dir, item);
	if (!ai)
		return;	/* No appmenu (or invalid XML) */

	/* OK, we've got a valid info file and parsed it.
	 * Now build the GtkMenuItem widgets and add them to the menu
	 * (if there are any).
	 */

	node = appinfo_get_section(ai, "AppMenu");
	if (!node)
		return;

	g_return_if_fail(current_items == NULL);

	/* Add the menu entries */
	for (node = node->xmlChildrenNode; node; node = node->next)
	{
		if (strcmp(node->name, "Item") == 0)
		{
			guchar	*label, *option;

			label = xmlGetProp(node, "label");
			option = xmlGetProp(node, "option");
			current_items = g_list_prepend(current_items,
					create_menu_item(label, option));

			g_free(label);
			g_free(option);
		}
	}

	sep = gtk_menu_item_new();
	current_items = g_list_prepend(current_items, sep);
	gtk_widget_show(sep);

	for (next = current_items; next; next = next->next)
		gtk_menu_prepend(GTK_MENU(menu), GTK_WIDGET(next->data));

	current_menu = menu;
	current_app_path = g_strdup(app_dir);
}

/****************************************************************
 *                      INTERNAL FUNCTIONS                      *
 ****************************************************************/

/* Create and return a menu item */
static GtkWidget *create_menu_item(char *label, char *option)
{
	GtkWidget *item;

	if (!label)
		label = "<missing label>";

	/* Create the new item and tie it to the appropriate callback */
	item = gtk_menu_item_new_with_label(label);

	if (option)
		gtk_object_set_data_full(GTK_OBJECT(item), "option",
						g_strdup(option),
						g_free);

	gtk_signal_connect(GTK_OBJECT(item), "activate",
			   GTK_SIGNAL_FUNC(apprun_menu),
			   NULL);

	gtk_widget_show(item);

	return item;
}

/* Function called to execute an AppMenu item */
static void apprun_menu(GtkWidget *item, gpointer data)
{
	guchar	*option;
	char *argv[3];

	g_return_if_fail(current_app_path != NULL);

	option = gtk_object_get_data(GTK_OBJECT(item), "option");
	
	argv[0] = g_strconcat(current_app_path, "/AppRun", NULL);
	argv[1] = option;	/* (may be NULL) */
	argv[2] = NULL;

	if (!spawn(argv))
		report_error(_("fork() failed"), g_strerror(errno));

	g_free(argv[0]);
}
