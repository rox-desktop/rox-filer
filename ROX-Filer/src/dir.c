/*
 * $Id$
 *
 * dir.c - Caches and updates directories
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

/* Don't load icons larger than 400K (this is rather excessive, basically
 * we just want to stop people crashing the filer with huge icons).
 */
#define MAX_ICON_SIZE (400 * 1024)

#include "config.h"

#include <gtk/gtk.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "global.h"

#include "support.h"
#include "gui_support.h"
#include "dir.h"
#include "fscache.h"
#include "mount.h"
#include "pixmaps.h"
#include "type.h"

GFSCache *dir_cache = NULL;

/* Static prototypes */
static Directory *load(char *pathname, gpointer data);
static void ref(Directory *dir, gpointer data);
static void unref(Directory *dir, gpointer data);
static int getref(Directory *dir, gpointer data);
static void update(Directory *dir, gchar *pathname, gpointer data);
static void destroy(Directory *dir);
static void start_scanning(Directory *dir, char *pathname);
static gint idle_callback(Directory *dir);
static void init_for_scan(Directory *dir);
static void merge_new(Directory *dir);


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
		start_scanning(dir, dir->pathname);

	if (dir->error)
		delayed_error(PROJECT, dir->error);
	
	if (!dir->dir_handle)
		callback(dir, DIR_END_SCAN, NULL, data);
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
			if (!dir->users)
			{
				if (dir->dir_handle)
				{
					mc_closedir(dir->dir_handle);
					dir->dir_handle = NULL;
					gtk_idle_remove(dir->idle);
					merge_new(dir);
					dir->needs_update = TRUE;
				}
			}
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

/* Bring this item's structure uptodate */
void dir_restat(guchar *path, DirItem *item)
{
	struct stat	info;

	if (item->image)
	{
		pixmap_unref(item->image);
		item->image = NULL;
	}
	item->flags = 0;
	item->mime_type = NULL;

	if (mc_lstat(path, &info) == -1)
	{
		item->lstat_errno = errno;
		item->base_type = TYPE_ERROR;
		item->size = 0;
		item->mode = 0;
		item->mtime = item->ctime = item->atime = 0;
		item->uid = (uid_t) -1;
		item->gid = (gid_t) -1;
	}
	else
	{
		item->lstat_errno = 0;
		item->size = info.st_size;
		item->mode = info.st_mode;
		item->atime = info.st_atime;
		item->ctime = info.st_ctime;
		item->mtime = info.st_mtime;
		item->uid = info.st_uid;
		item->gid = info.st_gid;

		if (S_ISLNK(info.st_mode))
		{
			if (mc_stat(path, &info))
				item->base_type = TYPE_ERROR;
			else
				item->base_type =
					mode_to_base_type(info.st_mode);

			item->flags |= ITEM_FLAG_SYMLINK;
		}
		else
		{
			item->base_type = mode_to_base_type(info.st_mode);

			if (item->base_type == TYPE_DIRECTORY)
			{
				if (mount_is_mounted(path))
					item->flags |= ITEM_FLAG_MOUNT_POINT
							| ITEM_FLAG_MOUNTED;
				else if (g_hash_table_lookup(fstab_mounts,
								path))
					item->flags |= ITEM_FLAG_MOUNT_POINT;
			}
		}
	}

	if (item->base_type == TYPE_DIRECTORY &&
			!(item->flags & ITEM_FLAG_MOUNT_POINT))
	{
		uid_t	uid = info.st_uid;
		int	path_len;
		guchar	*tmp;
		
		/* Might be an application directory - better check...
		 * AppRun must have the same owner as the directory
		 * (to stop people putting an AppRun in, eg, /tmp)
		 * If AppRun is a symlink, we want the symlink's owner.
		 */
		path_len = strlen(path);
		/* (sizeof(string) includes \0) */
		tmp = g_malloc(path_len + sizeof("/AppIcon.xpm"));
		sprintf(tmp, "%s/%s", path, "AppRun");

		if (mc_lstat(tmp, &info) == 0 && info.st_uid == uid)
		{
			struct stat	icon;

			item->flags |= ITEM_FLAG_APPDIR;
			
			strcpy(tmp + path_len + 4, "Icon.xpm");

			if (mc_stat(tmp, &icon) == 0 &&
					icon.st_size <= MAX_ICON_SIZE)
			{
				item->image = g_fscache_lookup(pixmap_cache,
								tmp);
			}
			else
			{
				item->image = im_appdir;
				pixmap_ref(item->image);
			}
		}
		g_free(tmp);
	}
	else if (item->base_type == TYPE_FILE)
	{
		/* Note: for symlinks we use need the mode of the target */
		if (info.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
		{
			item->flags |= ITEM_FLAG_EXEC_FILE;
			item->mime_type = &special_exec;
		}
		else
			item->mime_type = type_from_path(path);
	}

	if (!item->mime_type)
		item->mime_type = mime_type_from_base_type(item->base_type);

	if (!item->image)
	{
		if (item->base_type == TYPE_ERROR)
		{
			item->image = im_error;
			pixmap_ref(im_error);
		}
		else
			item->image = type_to_icon(item->mime_type);
	}
}

/* Fill in the item structure with the appropriate details.
 * 'leafname' field is set to NULL; text_width is unset.
 */
void dir_stat(guchar *path, DirItem *item)
{
	item->leafname = NULL;
	item->may_delete = FALSE;
	item->image = NULL;

	dir_restat(path, item);
}

/* Frees all fields in the icon, but does not free the icon structure
 * itself (because it might be part of a larger structure).
 */
void dir_item_clear(DirItem *item)
{
	g_return_if_fail(item != NULL);

	pixmap_unref(item->image);
	g_free(item->leafname);
}

/* When something has happened to a particular object, call this
 * and all appropriate changes will be made.
 * Currently, we just extract the dirname and rescan...
 */
void dir_check_this(guchar *path)
{
	guchar	*slash;
	guchar	*real_path;

	real_path = pathdup(path);

	slash = strrchr(real_path, '/');

	if (slash)
	{
		*slash = '\0';
		refresh_dirs(real_path);
	}
	
	g_free(real_path);
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

		dir_item_clear(item);
		g_free(item);
	}

	g_ptr_array_free(array, TRUE);
}

/* Scanning has finished. Remove all the old items that have gone.
 * Notify everyone who is watching us of the removed items and tell
 * them that the scan is over, unless 'needs_update'.
 */
static void sweep_deleted(Directory *dir)
{
	GPtrArray	*array = dir->items;
	int		items = array->len;
	int		new_items = 0;
	GPtrArray	*old;
	DirItem	**from = (DirItem **) array->pdata;
	DirItem	**to = (DirItem **) array->pdata;
	GList *list = dir->users;

	old = g_ptr_array_new();

	while (items--)
	{
		if (from[0]->may_delete)
		{
			g_ptr_array_add(old, *from);
			from++;
			continue;
		}

		if (from > to)
			*to = *from;
		to++;
		from++;
		new_items++;
	}

	g_ptr_array_set_size(array, new_items);

	while (list)
	{
		DirUser *user = (DirUser *) list->data;

		if (old->len)
			user->callback(dir, DIR_REMOVE, old, user->data);
		if (!dir->needs_update)
			user->callback(dir, DIR_END_SCAN, NULL, user->data);
		
		list = list->next;
	}

	free_items_array(old);
}


/* Add all the new items to the items array.
 * Notify everyone who is watching us.
 */
static void merge_new(Directory *dir)
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

static void init_for_scan(Directory *dir)
{
	int	i;

	dir->needs_update = FALSE;
	dir->done_some_scanning = FALSE;
	
	for (i = 0; i < dir->items->len; i++)
		((DirItem *) dir->items->pdata[i])->may_delete = TRUE;
}

static void start_scanning(Directory *dir, char *pathname)
{
	GList	*next;

	if (dir->dir_handle)
	{
		/* We are already scanning */
		if (dir->done_some_scanning)
			dir->needs_update = TRUE;
		return;
	}

	mount_update(FALSE);

	if (dir->error)
	{
		g_free(dir->error);
		dir->error = NULL;
	}

	dir->dir_handle = mc_opendir(pathname);

	if (!dir->dir_handle)
	{
		dir->error = g_strdup_printf(_("Can't open directory: %s"),
				g_strerror(errno));
		return;		/* Report on attach */
	}

	dir->dir_start = mc_telldir(dir->dir_handle);

	init_for_scan(dir);

	dir->idle = gtk_idle_add((GtkFunction) idle_callback, dir);

	/* Let all the filer windows know we're scanning */
	for (next = dir->users; next; next = next->next)
	{
		DirUser *user = (DirUser *) next->data;

		user->callback(dir, DIR_START_SCAN, NULL, user->data);
	}
}

static gint notify_timeout(gpointer data)
{
	Directory	*dir = (Directory *) data;

	g_return_val_if_fail(dir->notify_active == TRUE, FALSE);

	merge_new(dir);

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

static void insert_item(Directory *dir, struct dirent *ent)
{
	static GString  *tmp = NULL;

	GdkFont		*font;
	GPtrArray	*array = dir->items;
	DirItem		*item;
	int		i;
	DirItem		new;
	gboolean	is_new = FALSE;

	if (ent->d_name[0] == '.')
	{
		/* Hidden file */
		
		if (ent->d_name[1] == '\0')
			return;		/* Ignore '.' */
		if (ent->d_name[1] == '.' && ent->d_name[2] == '\0')
			return;		/* Ignore '..' */

		/* Other hidden files are still cached, but the directory
		 * viewers may filter them out on a per-display basis...
		 */
	}

	tmp = make_path(dir->pathname, ent->d_name);
	dir_stat(tmp->str, &new);

	/* Is an item with this name already listed? */
	for (i = 0; i < array->len; i++)
	{
		item = (DirItem *) array->pdata[i];

		if (strcmp(item->leafname, ent->d_name) == 0)
			goto update;
	}

	item = g_new(DirItem, 1);
	item->leafname = g_strdup(ent->d_name);
	g_ptr_array_add(dir->new_items, item);
	delayed_notify(dir);
	is_new = TRUE;
update:
	item->may_delete = FALSE;

	font = gtk_widget_get_default_style()->font;
	new.name_width = gdk_string_width(font, item->leafname);
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

static gint idle_callback(Directory *dir)
{
	struct dirent *ent;

	dir->done_some_scanning = TRUE;
	
	do
	{
		ent = mc_readdir(dir->dir_handle);

		if (!ent)
		{
			merge_new(dir);
			sweep_deleted(dir);

			if (dir->needs_update)
			{
				mc_seekdir(dir->dir_handle, dir->dir_start);
				init_for_scan(dir);
				return TRUE;
			}

			mc_closedir(dir->dir_handle);
			dir->dir_handle = NULL;

			return FALSE;		/* Stop */
		}

		insert_item(dir, ent);

	} while (!gtk_events_pending());

	return TRUE;
}

static Directory *load(char *pathname, gpointer data)
{
	Directory *dir;

	dir = g_new(Directory, 1);
	dir->ref = 1;
	dir->items = g_ptr_array_new();
	dir->new_items = g_ptr_array_new();
	dir->up_items = g_ptr_array_new();
	dir->users = NULL;
	dir->dir_handle = NULL;
	dir->needs_update = TRUE;
	dir->notify_active = FALSE;
	dir->pathname = g_strdup(pathname);
	dir->error = NULL;
	
	return dir;
}

static void destroy(Directory *dir)
{
	g_return_if_fail(dir->users == NULL);

	g_print("[ destroy %p ]\n", dir);
	if (dir->dir_handle)
	{
		mc_closedir(dir->dir_handle);
		gtk_idle_remove(dir->idle);
	}
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
	dir->pathname = g_strdup(pathname);

	start_scanning(dir, pathname);

	if (dir->error)
		delayed_error(PROJECT, dir->error);
}

