/*
 * $Id$
 *
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

/* dir.c - directory scanning and caching */

/* How it works:
 *
 * A Directory contains a list DirItems, each having a name and some details
 * (size, image, owner, etc).
 *
 * There is a list of file names that need to be rechecked. While this
 * list is non-empty, items are taken from the list in an idle callback
 * and checked. Missing items are removed from the Directory, new items are
 * added and existing items are updated if they've changed.
 *
 * When a whole directory is to be rescanned:
 * 
 * - A list of all filenames in the directory is fetched, without any
 *   of the extra details.
 * - This list is sorted, and compared to the current DirItems, removing
 *   any that are now missing.
 * - The recheck list is replaced with this new list.
 *
 * This system is designed to get the number of items and their names quickly,
 * so that the auto-sizer can make a good guess.
 *
 * To get the Directory object, use dir_cache, which will automatically
 * trigger a rescan if needed.
 *
 * To get notified when the Directory changes, use the dir_attach() and
 * dir_detach() functions.
 */

#include "config.h"

#include <gtk/gtk.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "global.h"

#include "dir.h"
#include "support.h"
#include "gui_support.h"
#include "dir.h"
#include "fscache.h"
#include "mount.h"
#include "pixmaps.h"
#include "type.h"
#include "usericons.h"

GFSCache *dir_cache = NULL;

/* Static prototypes */
static Directory *load(char *pathname, gpointer data);
static void ref(Directory *dir, gpointer data);
static void unref(Directory *dir, gpointer data);
static int getref(Directory *dir, gpointer data);
static void update(Directory *dir, gchar *pathname, gpointer data);
static void destroy(Directory *dir);
static void set_idle_callback(Directory *dir);
static void insert_item(Directory *dir, guchar *leafname);
static void remove_missing(Directory *dir, GPtrArray *keep);
static void dir_recheck(Directory *dir, guchar *path, guchar *leafname);

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void dir_init(void)
{
	dir_cache = g_fscache_new((GFSLoadFunc) load,
				(GFSRefFunc) ref,
				(GFSRefFunc) unref,
				(GFSGetRefFunc) getref,
				(GFSUpdateFunc) update, NULL);
}

/* Periodically calls callback to notify about changes to the contents
 * of the directory.
 * Before this function returns, it calls the callback once to add all
 * the items currently in the directory (unless the dir is empty).
 * If we are not scanning, it also calls update(DIR_END_SCAN).
 */
void dir_attach(Directory *dir, DirCallback callback, gpointer data)
{
	DirUser	*user;

	g_return_if_fail(dir != NULL);
	g_return_if_fail(callback != NULL);

	user = g_new(DirUser, 1);
	user->callback = callback;
	user->data = data;
	
	dir->users = g_list_prepend(dir->users, user);

	ref(dir, NULL);

	if (dir->items->len)
		callback(dir, DIR_ADD, dir->items, data);

	if (dir->needs_update)
		dir_rescan(dir, dir->pathname);

	/* May start scanning if noone was watching before */
	set_idle_callback(dir);

	if (!dir->scanning)
		callback(dir, DIR_END_SCAN, NULL, data);

	if (dir->error)
		delayed_error(PROJECT, dir->error);
}

/* Undo the effect of dir_attach */
void dir_detach(Directory *dir, DirCallback callback, gpointer data)
{
	DirUser	*user;
	GList	*list = dir->users;

	g_return_if_fail(dir != NULL);
	g_return_if_fail(callback != NULL);

	while (list)
	{
		user = (DirUser *) list->data;
		if (user->callback == callback && user->data == data)
		{
			g_free(user);
			dir->users = g_list_remove(dir->users, user);
			unref(dir, NULL);

			/* May stop scanning if noone's watching */
			set_idle_callback(dir);

			return;
		}
		list = list->next;
	}

	g_warning("dir_detach: Callback/data pair not attached!\n");
}

void dir_update(Directory *dir, gchar *pathname)
{
	update(dir, pathname, NULL);
}

int dir_item_cmp(const void *a, const void *b)
{
	DirItem *aa = *((DirItem **) a);
	DirItem *bb = *((DirItem **) b);

	return strcmp(aa->leafname, bb->leafname);
}

/* Rescan this directory */
void refresh_dirs(char *path)
{
	g_fscache_update(dir_cache, path);
}

/* When something has happened to a particular object, call this
 * and all appropriate changes will be made.
 */
void dir_check_this(guchar *path)
{
	guchar	*slash;
	guchar	*real_path;

	real_path = pathdup(path);

	slash = strrchr(real_path, '/');

	if (slash)
	{
		Directory *dir;

		*slash = '\0';

		dir = g_fscache_lookup_full(dir_cache, real_path,
						FSCACHE_LOOKUP_PEEK);
		if (dir)
			dir_recheck(dir, real_path, slash + 1);
	}
	
	g_free(real_path);
}

void dir_rescan_with_thumbs(Directory *dir, gchar *pathname)
{
	g_return_if_fail(dir != NULL);
	g_return_if_fail(pathname != NULL);

	dir->do_thumbs = TRUE;
	update(dir, pathname, NULL);
}

static int sort_names(const void *a, const void *b)
{
	return strcmp(*((char **) a), *((char **) b));
}

static void free_recheck_list(Directory *dir)
{
	GList	*next;

	for (next = dir->recheck_list; next; next = next->next)
		g_free(next->data);

	g_list_free(dir->recheck_list);

	dir->recheck_list = NULL;
}

/* If scanning state has changed then notify all filer windows */
void dir_set_scanning(Directory *dir, gboolean scanning)
{
	GList	*next;

	if (scanning == dir->scanning)
		return;

	dir->scanning = scanning;

	for (next = dir->users; next; next = next->next)
	{
		DirUser *user = (DirUser *) next->data;

		user->callback(dir,
				scanning ? DIR_START_SCAN : DIR_END_SCAN,
				NULL, user->data);
	}
}

/* This is called in the background when there are items on the
 * dir->recheck_list to process.
 */
static gboolean recheck_callback(gpointer data)
{
	Directory *dir = (Directory *) data;
	GList	*next;
	guchar	*leaf;
	
	g_return_val_if_fail(dir != NULL, FALSE);
	g_return_val_if_fail(dir->recheck_list != NULL, FALSE);

	/* Remove the first name from the list */
	next = dir->recheck_list;
	dir->recheck_list = g_list_remove_link(dir->recheck_list, next);
	leaf = (guchar *) next->data;
	g_list_free_1(next);

	insert_item(dir, leaf);

	g_free(leaf);

	if (dir->recheck_list)
		return TRUE;	/* Call again */

	dir_merge_new(dir);
	dir_set_scanning(dir, FALSE);
	dir->idle_callback = 0;
	return FALSE;
}

/* Get the names of all files in the directory.
 * Remove any DirItems that are no longer listed.
 * Replace the recheck_list with the items found.
 */
void dir_rescan(Directory *dir, guchar *pathname)
{
	GList		*next;
	GPtrArray	*names;
	DIR		*d;
	struct dirent	*ent;
	int		i;

	g_return_if_fail(dir != NULL);
	g_return_if_fail(pathname != NULL);

	names = g_ptr_array_new();

	read_globicons();
	mount_update(FALSE);
	if (dir->error)
	{
		g_free(dir->error);
		dir->error = NULL;
	}

	d = mc_opendir(pathname);
	if (!d)
	{
		dir->error = g_strdup_printf(_("Can't open directory: %s"),
				g_strerror(errno));
		return;		/* Report on attach */
	}

	dir_set_scanning(dir, TRUE);
	gdk_flush();

	/* Make a list of all the names in the directory */
	while ((ent = mc_readdir(d)))
	{
		if (ent->d_name[0] == '.')
		{
			if (ent->d_name[1] == '\0')
				continue;		/* Ignore '.' */
			if (ent->d_name[1] == '.' && ent->d_name[2] == '\0')
				continue;		/* Ignore '..' */
		}
		
		g_ptr_array_add(names, g_strdup(ent->d_name));
	}

	/* Give everyone the list of names, so they can resize if needed */
	for (next = dir->users; next; next = next->next)
	{
		DirUser *user = (DirUser *) next->data;

		user->callback(dir, DIR_NAMES, names, user->data);
	}

	/* Compare the (sorted) list with the current DirItems, removing
	 * any that are missing.
	 */
	qsort(names->pdata, names->len, sizeof(guchar *), sort_names);
	remove_missing(dir, names);

	free_recheck_list(dir);

	for (i = 0; i < names->len; i++)
		dir->recheck_list = g_list_prepend(dir->recheck_list,
						   names->pdata[i]);

	dir->recheck_list = g_list_reverse(dir->recheck_list);

	g_ptr_array_free(names, TRUE);
	mc_closedir(d);

	set_idle_callback(dir);
}

/* Add all the new items to the items array.
 * Notify everyone who is watching us.
 */
void dir_merge_new(Directory *dir)
{
	GList *list = dir->users;
	GPtrArray	*new = dir->new_items;
	GPtrArray	*up = dir->up_items;
	int	i;

	while (list)
	{
		DirUser *user = (DirUser *) list->data;

		if (new->len)
			user->callback(dir, DIR_ADD, new, user->data);
		if (up->len)
			user->callback(dir, DIR_UPDATE, up, user->data);
		
		list = list->next;
	}


	for (i = 0; i < new->len; i++)
		g_ptr_array_add(dir->items, new->pdata[i]);
	qsort(dir->items->pdata, dir->items->len, sizeof(DirItem *),
			dir_item_cmp);

	g_ptr_array_set_size(new, 0);
	g_ptr_array_set_size(up, 0);
}


/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

static void free_items_array(GPtrArray *array)
{
	int	i;

	for (i = 0; i < array->len; i++)
	{
		DirItem	*item = (DirItem *) array->pdata[i];

		diritem_clear(item);
		g_free(item);
	}

	g_ptr_array_free(array, TRUE);
}

/* Remove all the old items that have gone.
 * Notify everyone who is watching us of the removed items.
 */
static void remove_missing(Directory *dir, GPtrArray *keep)
{
	int		kept_items = 0;
	GPtrArray	*deleted;
	GList		*next;
	int		old, new;

	deleted = g_ptr_array_new();

	old = 0;
	new = 0;

	while (old < dir->items->len && new < keep->len)
	{
		DirItem	*old_item = (DirItem *) dir->items->pdata[old];
		guchar	*new_name = (guchar *) keep->pdata[new];
		int cmp;

		cmp = strcmp(old_item->leafname, new_name);

		if (cmp < 0)
		{
			/* old_item would have appeared before new_name
			 * if it was still there - kill it!
			 */
			old++;
			g_print("[ remove item '%s' ]\n", old_item->leafname);
			g_ptr_array_add(deleted, old_item);
		}
		else if (cmp == 0)
		{
			/* Item is in old and new lists */
			old++;
			new++;

			dir->items->pdata[kept_items] = old_item;
			kept_items++;
		}
		else
		{
			/* new_name wasn't here before. Skip it */
			new++;
			g_print("[ new item '%s' ]\n", new_name);
		}
	}

	/* If we didn't get to the end of the old items, then we've
	 * hit the end of the new items. Thus, we can just forget the
	 * remaining old items.
	 */
	for (; old < dir->items->len; old++)
		g_ptr_array_add(deleted, dir->items->pdata[old]);
	
	g_ptr_array_set_size(dir->items, kept_items);

	/* Tell everyone about the removals */
	if (deleted->len)
	{
		for (next = dir->users; next; next = next->next)
		{
			DirUser *user = (DirUser *) next->data;

			user->callback(dir, DIR_REMOVE, deleted, user->data);
		}
	}

	free_items_array(deleted);
}

static gint notify_timeout(gpointer data)
{
	Directory	*dir = (Directory *) data;

	g_return_val_if_fail(dir->notify_active == TRUE, FALSE);

	dir_merge_new(dir);

	dir->notify_active = FALSE;
	unref(dir, NULL);

	return FALSE;
}

/* Call merge_new() after a while. */
static void delayed_notify(Directory *dir)
{
	if (dir->notify_active)
		return;
	ref(dir, NULL);
	gtk_timeout_add(500, notify_timeout, dir);
	dir->notify_active = TRUE;
}

/* Stat this item and add, update or remove it */
/* XXX: Doesn't cope with deletions yet! */
static void insert_item(Directory *dir, guchar *leafname)
{
	static GString  *tmp = NULL;

	GdkFont		*font;
	GPtrArray	*array = dir->items;
	DirItem		*item;
	int		i;
	DirItem		new;
	gboolean	is_new = FALSE;

	tmp = make_path(dir->pathname, leafname);
	diritem_stat(tmp->str, &new, dir->do_thumbs);

	/* Is an item with this name already listed? */
	for (i = 0; i < array->len; i++)
	{
		item = (DirItem *) array->pdata[i];

		if (strcmp(item->leafname, leafname) == 0)
			goto update;
	}

	item = g_new(DirItem, 1);
	item->leafname = g_strdup(leafname);
	g_ptr_array_add(dir->new_items, item);
	delayed_notify(dir);
	is_new = TRUE;
update:
	item->may_delete = FALSE;

	font = item_font;
	new.name_width = gdk_string_measure(font, item->leafname);
	new.leafname = item->leafname;

	if (is_new == FALSE)
	{
		if (item->lstat_errno == new.lstat_errno
		 && item->base_type == new.base_type
		 && item->flags == new.flags
		 && item->size == new.size
		 && item->mode == new.mode
		 && item->atime == new.atime
		 && item->ctime == new.ctime
		 && item->mtime == new.mtime
		 && item->uid == new.uid
		 && item->gid == new.gid
		 && item->image == new.image
		 && item->mime_type == new.mime_type
		 && item->name_width == new.name_width)
		{
			pixmap_unref(new.image);
			return;
		}
	}

	item->image = new.image;
	item->lstat_errno = new.lstat_errno;
	item->base_type = new.base_type;
	item->flags = new.flags;
	item->size = new.size;
	item->mode = new.mode;
	item->uid = new.uid;
	item->gid = new.gid;
	item->atime = new.atime;
	item->ctime = new.ctime;
	item->mtime = new.mtime;
	item->mime_type = new.mime_type;
	item->name_width = new.name_width;

	if (!is_new)
	{
		g_ptr_array_add(dir->up_items, item);
		delayed_notify(dir);
	}
}

static Directory *load(char *pathname, gpointer data)
{
	Directory *dir;

	dir = g_new(Directory, 1);
	dir->ref = 1;
	dir->items = g_ptr_array_new();
	dir->recheck_list = NULL;
	dir->idle_callback = 0;
	dir->scanning = FALSE;
	
	dir->users = NULL;
	dir->dir_handle = NULL;
	dir->needs_update = TRUE;
	dir->notify_active = FALSE;
	dir->pathname = g_strdup(pathname);
	dir->error = NULL;
	dir->do_thumbs = FALSE;

	dir->new_items = g_ptr_array_new();
	dir->up_items = g_ptr_array_new();
	
	return dir;
}

static void destroy(Directory *dir)
{
	g_return_if_fail(dir->users == NULL);

	g_print("[ destroy %p ]\n", dir);

	free_recheck_list(dir);
	set_idle_callback(dir);

	g_ptr_array_free(dir->up_items, TRUE);
	free_items_array(dir->items);
	free_items_array(dir->new_items);
	g_free(dir->error);
	g_free(dir->pathname);
	g_free(dir);
}

static void ref(Directory *dir, gpointer data)
{
	dir->ref++;
}
	
static void unref(Directory *dir, gpointer data)
{
	if (--dir->ref == 0)
		destroy(dir);
}

static int getref(Directory *dir, gpointer data)
{
	return dir->ref;
}

static void update(Directory *dir, gchar *pathname, gpointer data)
{
	g_free(dir->pathname);
	dir->pathname = pathdup(pathname);

	dir_rescan(dir, pathname);

	if (dir->error)
		delayed_error(PROJECT, dir->error);
}

/* If there is work to do, set the idle callback.
 * Otherwise, stop scanning and unset the idle callback.
 */
static void set_idle_callback(Directory *dir)
{
	if (dir->recheck_list && dir->users)
	{
		/* Work to do, and someone's watching */
		dir_set_scanning(dir, TRUE);
		if (dir->idle_callback)
			return;
		dir->idle_callback = gtk_idle_add(recheck_callback, dir);
	}
	else
	{
		dir_set_scanning(dir, FALSE);
		if (dir->idle_callback)
		{
			gtk_idle_remove(dir->idle_callback);
			dir->idle_callback = 0;
		}
	}
}

static void dir_recheck(Directory *dir, guchar *path, guchar *leafname)
{
	g_free(dir->pathname);
	dir->pathname = g_strdup(path);

	g_print("[ recheck %s - %s ]\n", path, leafname);

	insert_item(dir, leafname);
	dir_merge_new(dir);
}
