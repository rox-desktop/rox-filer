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

GFSCache *appmenu_cache = NULL;

static GList *cur_list;

/* Internal types */
struct _AppMenu_check {
	char *dirname;
	struct stat info;
};

static struct _AppMenu_check cur_check;

/* Static prototypes */
static AppMenus *load(char *pathname, gpointer data);
static void update(AppMenus *dir, gchar *pathname, gpointer data);
static char *process_appmenu_line(guchar *line);
static void load_appmenu(AppMenus *appmenu, char *file);
static gboolean check_appmenu_file(char *pathname);
static void apprun_menu(gchar *string);
static void destroy_widget(gpointer data, gpointer user_data);
static void destroy_contents(AppMenus *dir);
static GtkWidget *appmenu_item_new(char *path, char *text, char *entryid);

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void appmenu_init(void)
{
	appmenu_cache = g_fscache_new((GFSLoadFunc) load,
				      NULL, NULL, NULL,
				      (GFSUpdateFunc) update, NULL);
	cur_check.dirname=NULL;
}

/* Remove the appmenu stored in the given menu, if any */
/* This function modifies the menu stored in "menu" */
void appmenu_remove(GtkWidget *menu)
{
	AppMenus *last_appmenu;

	/* Get the last appmenu posted on this menu, if any */
	last_appmenu = gtk_object_get_data(GTK_OBJECT(menu), "last_appmenu");

	/* If the last appmenu was a different one, we remove its items before adding
	 * ours. */
	if (last_appmenu)
	{
		GList *next=last_appmenu->items;
		while(next) {
			gtk_container_remove(GTK_CONTAINER(menu),
					     GTK_WIDGET(next->data));
			next=next->next;
		}
	}

	gtk_object_set_data(GTK_OBJECT(menu), "last_appmenu", NULL);
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

	/* If this AppMenu had been posted on a different menu, remove it from there */
	if (dir->last_menu) 
		appmenu_remove(dir->last_menu);

	/* The dir we get must have been obtained from the cache, so
	   that its items element is already properly populated. We check
	   anyway.
	*/
	if (dir->items == NULL)
		return;

	/* Add the menu entries */
	next = dir->items;
	while(next) {
		gtk_menu_prepend(GTK_MENU(menu), GTK_WIDGET(next->data));
		next=next->next;
	}
	/* Store a pointer to the current AppMenus entry in the menu itself */
	gtk_object_set_data(GTK_OBJECT(menu), "last_appmenu", dir);
	/* Store a pointer to the current menu in the AppMenus object too, in case
	   we later try to post this same AppMenu to a different menu */
	dir->last_menu=menu;
}

/* Return the AppMenus object corresponding to a certain path, if there
 * is one. The path must be a directory name, and the AppMenu file is
 * searched for in there.
 * This is the approved interface to appmenu_cache. g_fscache_lookup_full should
 * preferably never be called on appmenu_cache other than through this function.
 */
AppMenus *appmenu_query(char *path)
{
	GString *tmp=g_string_new(path);
	struct stat info;

	g_string_sprintfa(tmp, "/%s", APPMENU_FILENAME);
	if (mc_stat(tmp->str, &info)==0)
		return g_fscache_lookup_full(appmenu_cache, tmp->str, TRUE);
	else
		return NULL;
}

/****************************************************************
 *                      INTERNAL FUNCTIONS                      *
 ****************************************************************/

/* The pathname is the full path of the AppMenu file.
 * We check for an AppRun file, and all the appropriate permissions,
 * like in dir.c
 */
static AppMenus *load(char *pathname, gpointer data)
{
	AppMenus *appmenu=NULL;

	g_return_val_if_fail(pathname != NULL, NULL);

	if (!check_appmenu_file(pathname))
		return NULL;

	appmenu = g_new(AppMenus, 1);
	load_appmenu(appmenu, pathname);

	return appmenu;
}

/* Check that:
   - The given file exists
   - An AppRun file exists in the same directory
   - The given file, the AppRun file and the directory all belong to the same user

   If everything is correct, it fills cur_check with the directory name and
   the stat information of the file passed as argument.

   Returns TRUE if everything is correct, FALSE otherwise.
*/
static gboolean check_appmenu_file(char *pathname)
{
	struct stat info;
	guchar *dirname, *slash;
	GString *tmp;
	uid_t uid;
	gboolean result=FALSE;

	dirname=g_strdup(pathname);
	slash = strrchr(dirname, '/');
	if (slash)
		*slash='\0';
	else
		/* We need an absolute path. If there's no slash, something is wrong */
		goto out;

	/* Store the dirname */
	if(cur_check.dirname)
		g_free(cur_check.dirname);
	cur_check.dirname=g_strdup(dirname);

	/* Check that the AppRun file exists, and that it has the correct owner */

	/* Get the directory's owner */
	if (mc_stat(dirname, &info) == 0)
		uid=info.st_uid;
	else
		goto out;

	tmp=g_string_new(dirname);
	g_string_sprintfa(tmp, "/%s", "AppRun");

	if (mc_lstat(tmp->str, &info) == 0 && info.st_uid == uid)
	{
		/* See if there's a static menu file, and that it belongs to the
		   same user that owns AppRun.
		   We do the stat on the cur_check structure so that other
		   functions can access the information without having to do
		   another stat
		*/
		if (mc_stat(pathname, &cur_check.info) == 0 &&
		    S_ISREG(cur_check.info.st_mode) &&
		    cur_check.info.st_uid == uid)
		{
			result=TRUE;
		}
		/* If the menu file doesn't exist or does not belong
		   to the same user as AppRun, do nothing */
	}
	g_string_free(tmp, TRUE);
 out:
	g_free(dirname);
	return result;
}

/* This function is the one that actually loads an appmenu from a file.
 * It doesn't do any permissions checking, that must have been done
 * before (see load() and update()).
 */
static void load_appmenu(AppMenus *appmenu, char *file)
{
	GtkWidget *sep;

	g_return_if_fail(appmenu != NULL);
	
	cur_list = NULL;
	parse_file(file, process_appmenu_line);
	sep=gtk_menu_item_new();
	/* I don't know why, but if you don't manually increase the ref of a separator,
	 * it goes out of existence when it is removed from the menu, and then it can't
	 * be re-added later.
	 */
	gtk_widget_ref(sep);
	gtk_widget_show(sep);
	cur_list=g_list_prepend(cur_list, sep);
	appmenu->items = cur_list;
	appmenu->ref = 1;
	appmenu->last_read = time(NULL);
	appmenu->last_menu=NULL;
}

/* Process lines from an AppMenu file. The format is:
   entryid	Entry text
   The corresponding entry is created, and when it is invoked, the
   corresponding AppRun is invoked with option --entryid.
*/
static char *process_appmenu_line(guchar *line)
{
	guchar *entryid, *text;

	entryid=strtok(line, " \t");
	/* Silently ignore empty lines and those that start with '#' */
	if (entryid == NULL || entryid[0]=='#') {
		return NULL;
	}
	text=strtok(NULL, "");
	/* Noisily ignore lines with no text */
	g_return_val_if_fail(text != NULL, "Invalid line in AppMenu specification: no entry text");
	/* Skip white space */
	while(isspace(*text)) text++;
	/* Create a new entry and store the appropriate information */
	cur_list=g_list_prepend(cur_list, 
				appmenu_item_new(cur_check.dirname,
						 text, entryid));

	return NULL;
}

/* Create and return a menu item */
static GtkWidget *appmenu_item_new(char *path, char *text, char *entryid)
{
	GtkWidget *item;
	GString *fullpath;

	g_return_val_if_fail(path!=NULL && text!=NULL && entryid!=NULL, NULL);

	/* Create the new item and tie it to the appropriate callback */
	item=gtk_menu_item_new_with_label(text);
	fullpath=g_string_new(path);
	g_string_sprintfa(fullpath, "/AppRun --%s", entryid);
	gtk_signal_connect_object(GTK_OBJECT(item), "activate",
				  GTK_SIGNAL_FUNC(apprun_menu),
				  (gpointer) g_strdup(fullpath->str));
	g_string_free(fullpath, TRUE);
	gtk_widget_show(item);
	return item;
}

/* Function called to execute an AppMenu item */
static void apprun_menu(gchar *string)
{
	char *argv[3];
	char *sep;
	
	/* Temporarily split the string in two */
	sep=strchr(string, ' ');
	if (sep) {
		*sep='\0';
		argv[0]=string;
		argv[1]=sep+1;
		argv[2]=NULL;
		if(!spawn(argv))
			report_error(_("fork() failed"), g_strerror(errno));
		*sep=' ';
	}
	else
	{
 		fprintf(stderr, "Internal error: string '%s' does not contain a space.\n", string);
	}
}

/* Destroy the contents of an AppMenus structure */
static void destroy_contents(AppMenus *dir)
{
	if (dir) {
		if (dir->items) {
			g_list_foreach(dir->items, destroy_widget, NULL);
			g_list_free(dir->items);
		}

		dir->items=NULL;
		dir->last_read=(time_t)0;
		dir->last_menu=NULL;
		dir->ref=0;
	}
}


/* Check if the file has been modified and reload it if necessary */
static void update(AppMenus *dir, gchar *pathname, gpointer data)
{
	if (dir) {
		/* We check the file owners every time, just to be safe.
		   check_appmenu_file() fills cur_check.info */
		if (check_appmenu_file(pathname))
		{
			if (cur_check.info.st_mtime > dir->last_read)
			{
				/* Reload the file */
				destroy_contents(dir);
				load_appmenu(dir, pathname);
			}
		}
		else {
			/* Most likely file doesn't exist anymore. There
			 doesn't seem to be a way to remove the entry
			 from here, so we just empty it */
			destroy_contents(dir);
		}
	}
}

/* Free a GTK widget */
static void destroy_widget(gpointer data, gpointer user_data)
{
	if (data)
		gtk_widget_unref((GtkWidget *)data);
}
