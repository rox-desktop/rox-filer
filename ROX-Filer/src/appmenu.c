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

/* appmenu.c - handles application-specific menus */

#include "config.h"

#include <gtk/gtk.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "global.h"

#include "fscache.h"
#include "gui_support.h"
#include "support.h"
#include "menu.h"
#include "filer.h"
#include "appmenu.h"
#include "dir.h"
#include "type.h"

GFSCache *appmenu_cache = NULL;

static AppMenus *cur_appmenu = NULL;

/* Static prototypes */
static AppMenus *load(char *pathname, gpointer data);
static void load_appmenu(AppMenus *appmenu, char *file);
static void ref(AppMenus *dir, gpointer data);
static void unref(AppMenus *dir, gpointer data);
static int getref(AppMenus *dir, gpointer data);

static char *process_appmenu_line(guchar *line);
static void apprun_menu(GtkWidget *item, AppMenus *menu);
static void destroy_widget(gpointer data, gpointer user_data);
static void destroy_contents(AppMenus *dir);
static GtkWidget *appmenu_item_new(AppMenus *menu, char *text, char *entryid);

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void appmenu_init(void)
{
	appmenu_cache = g_fscache_new((GFSLoadFunc) load,
				      (GFSRefFunc) ref,
				      (GFSRefFunc) unref,
				      (GFSGetRefFunc) getref,
				      NULL, NULL);
}

/* Remove the appmenu stored in the given menu, if any */
/* This function modifies the menu stored in "menu" */
void appmenu_remove(GtkWidget *menu)
{
	AppMenus *last_appmenu;
	GList *next;

	/* Get the last appmenu posted on this menu, if any */
	last_appmenu = gtk_object_get_data(GTK_OBJECT(menu), "last_appmenu");
	if (!last_appmenu)
		return;

	for (next = last_appmenu->items; next; next = next->next)
		gtk_container_remove(GTK_CONTAINER(menu),
				     GTK_WIDGET(next->data));

	gtk_object_set_data(GTK_OBJECT(menu), "last_appmenu", NULL);

	/* Lose the ref count returned from appmenu_query() */
	g_fscache_data_unref(appmenu_cache, last_appmenu);
}

/* Add AppMenu entries to a standard menu, if necessary */
/* This function modifies the menu stored in "menu" */
void appmenu_add(AppMenus *dir, GtkWidget *menu)
{
	GList *next;

	if (dir == NULL || menu == NULL)
		return;

	/* Remove the previous appmenu, if any */
	appmenu_remove(menu);

	/* If this AppMenu had been posted on a different menu, remove it from
	 * there.
	 */
	if (dir->last_menu) 
		appmenu_remove(dir->last_menu);

	/* The dir we get must have been obtained from the cache, so that its
	 * items element is already properly populated. We check anyway.
	 */
	if (dir->items == NULL)
		return;

	/* Add the menu entries */
	for (next = dir->items; next; next = next->next)
		gtk_menu_prepend(GTK_MENU(menu), GTK_WIDGET(next->data));

	/* Store a pointer to the current AppMenus entry in the menu itself */
	gtk_object_set_data(GTK_OBJECT(menu), "last_appmenu", dir);

	/* Store a pointer to the current menu in the AppMenus object too, in
	 * case we later try to post this same AppMenu to a different menu.
	 */
	dir->last_menu = menu;
}

/* Return the AppMenus object corresponding to a certain item, if there
 * is one.
 * This is the approved interface to appmenu_cache. g_fscache_lookup should
 * preferably never be called on appmenu_cache other than through this
 * function.
 * 'app_dir' is the full path of 'app'.
 * Returned value will unref automatically on appmenu_remove().
 */
AppMenus *appmenu_query(guchar *app_dir, DirItem *app)
{
	AppMenus *retval;
	guchar	*tmp;

	/* Is it even an application directory? */
	if (app->base_type != TYPE_DIRECTORY ||
			!(app->flags & ITEM_FLAG_APPDIR))
		return NULL;

	tmp = g_strconcat(app_dir, "/" APPMENU_FILENAME, NULL);
	
	retval = g_fscache_lookup(appmenu_cache, tmp);
	/* We set the path on every query because the app dir can move
	 * without the cache causing a reload.
	 */
	if (retval)
	{
		g_free(retval->app_dir);
		retval->app_dir = g_strdup(app_dir);
	}

	g_free(tmp);

	return retval;
}

/****************************************************************
 *                      INTERNAL FUNCTIONS                      *
 ****************************************************************/

/* The pathname is the full path of the AppMenu file.
 * If we get here, assume that this really is an application.
 */
static AppMenus *load(char *pathname, gpointer data)
{
	AppMenus *appmenu = NULL;

	appmenu = g_new(AppMenus, 1);
	appmenu->ref = 1;
	appmenu->app_dir = NULL;
	appmenu->last_menu = NULL;
	appmenu->items = NULL;

	load_appmenu(appmenu, pathname);

	return appmenu;
}

static void ref(AppMenus *dir, gpointer data)
{
	if (dir)
		dir->ref++;
}

static void unref(AppMenus *dir, gpointer data)
{
	if (dir && --dir->ref == 0)
		destroy_contents(dir);
}
	
static int getref(AppMenus *dir, gpointer data)
{
	return dir ? dir->ref : 0;
}

/* This function is the one that actually loads an appmenu from a file.
 * It doesn't do any permissions checking, that must have been done
 * before...
 */
static void load_appmenu(AppMenus *appmenu, char *file)
{
	GtkWidget *sep;

	g_return_if_fail(appmenu != NULL);
	
	cur_appmenu = appmenu;
	/* TODO: Make sure the file isn't too big... */
	parse_file(file, process_appmenu_line);
	cur_appmenu = NULL;

	sep = gtk_menu_item_new();
	/* When separator is added to a container widget for the first time,
	 * that widget takes our reference count from us. Therefore, we need an
	 * extra one...
	 */
	gtk_widget_ref(sep);
	gtk_object_sink(GTK_OBJECT(sep));
	gtk_widget_show(sep);
	appmenu->items = g_list_prepend(appmenu->items, sep);
}

/* Process lines from an AppMenu file. The format is:
 * entryid	Entry text
 * The corresponding entry is created, and when it is invoked, the
 * corresponding AppRun is invoked with option --entryid.
 */
static char *process_appmenu_line(guchar *line)
{
	guchar *entryid, *text;

	entryid = strtok(line, " \t");
	/* Silently ignore empty lines and those that start with '#' */
	if (entryid == NULL || entryid[0] == '#')
		return NULL;

	text = strtok(NULL, "");

	/* Noisily ignore lines with no text */
	if (!text)
		text = _("[ missing text ]");

	/* Skip white space */
	while (isspace(*text))
		text++;

	/* Create a new entry and store the appropriate information */
	cur_appmenu->items = g_list_prepend(cur_appmenu->items, 
				  appmenu_item_new(cur_appmenu, text, entryid));

	return NULL;
}

/* Create and return a menu item */
static GtkWidget *appmenu_item_new(AppMenus *menu, char *text, char *entryid)
{
	GtkWidget *item;

	g_return_val_if_fail(text != NULL && entryid != NULL, NULL);

	/* Create the new item and tie it to the appropriate callback */
	item = gtk_menu_item_new_with_label(text);

	gtk_object_set_data_full(GTK_OBJECT(item), "entryid",
					g_strconcat("--", entryid, NULL),
					debug_free_string);

	gtk_signal_connect(GTK_OBJECT(item), "activate",
			   GTK_SIGNAL_FUNC(apprun_menu),
			   (gpointer) menu);

	gtk_widget_ref(item);
	gtk_object_sink(GTK_OBJECT(item));
	gtk_widget_show(item);

	return item;
}

/* Function called to execute an AppMenu item */
static void apprun_menu(GtkWidget *item, AppMenus *menu)
{
	guchar	*entryid;
	char *argv[3];

	entryid = gtk_object_get_data(GTK_OBJECT(item), "entryid");
	g_return_if_fail(entryid != NULL);
	
	argv[0] = g_strconcat(menu->app_dir, "/AppRun", NULL);
	argv[1] = entryid;
	argv[2] = NULL;

	if (!spawn(argv))
		report_error(_("fork() failed"), g_strerror(errno));

	g_free(argv[0]);
}

/* Destroy the contents of an AppMenus structure */
static void destroy_contents(AppMenus *dir)
{
	g_return_if_fail(dir != NULL);

	if (dir->items)
	{
		g_list_foreach(dir->items, destroy_widget, NULL);
		g_list_free(dir->items);
	}

	dir->items = NULL;
	dir->last_menu = NULL;
}

/* Free a GTK widget */
static void destroy_widget(gpointer data, gpointer user_data)
{
	if (data)
		gtk_widget_unref((GtkWidget *)data);
}
