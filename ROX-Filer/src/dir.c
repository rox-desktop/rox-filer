/*
 * $Id$
 *
 * dir.c - Caches and updates directories
 * Copyright (C) 1999, Thomas Leonard, <tal197@ecs.soton.ac.uk>.
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

#include <gtk/gtk.h>
#include <errno.h>

#include "support.h"
#include "gui_support.h"
#include "dir.h"
#include "fscache.h"
#include "mount.h"
#include "pixmaps.h"
#include "filer.h"

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
		delayed_error("ROX-Filer", dir->error);
	
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
					closedir(dir->dir_handle);
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

void refresh_dirs(char *path)
{
	g_fscache_update(dir_cache, path);
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
		
		g_free(item->leafname);
		g_free(item);
	}

	g_ptr_array_free(array, TRUE);
}

/* Scanning has finished. Remove all the old items that have gone.
 * Notify everyone who is watching us of the removed items and tell
 * them that the scan is over.
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
	
	for (i = 0; i < dir->items->len; i++)
		((DirItem *) dir->items->pdata[i])->may_delete = TRUE;
}

static void start_scanning(Directory *dir, char *pathname)
{
	if (dir->dir_handle)
	{
		dir->needs_update = TRUE;
		return;
	}

	mount_update(FALSE);

	if (dir->error)
	{
		g_free(dir->error);
		dir->error = NULL;
	}

	dir->dir_handle = opendir(pathname);

	if (!dir->dir_handle)
	{
		dir->error = g_strdup_printf("Can't open directory: %s",
				g_strerror(errno));
		return;		/* Report on attach */
	}

	init_for_scan(dir);

	dir->idle = gtk_idle_add((GtkFunction) idle_callback, dir);
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
	struct stat	info;
	DirItem		new;
	gboolean	is_new = FALSE;

	new.flags = 0;
	new.mime_type = NULL;
	new.image = NULL;
	new.size = 0;
	new.mode = 0;
	new.mtime = 0;

	tmp = make_path(dir->pathname, ent->d_name);
	
	if (lstat(tmp->str, &info) == -1)
		new.base_type = TYPE_ERROR;
	else
	{
		new.size = info.st_size;
		new.mode = info.st_mode;
		new.mtime = info.st_mtime;
		if (S_ISREG(info.st_mode))
			new.base_type = TYPE_FILE;
		else if (S_ISDIR(info.st_mode))
		{
			new.base_type = TYPE_DIRECTORY;

			if (g_hash_table_lookup(mtab_mounts, tmp->str))
				new.flags |= ITEM_FLAG_MOUNT_POINT
						| ITEM_FLAG_MOUNTED;
			else if (g_hash_table_lookup(fstab_mounts, tmp->str))
				new.flags |= ITEM_FLAG_MOUNT_POINT;
		}
		else if (S_ISBLK(info.st_mode))
			new.base_type = TYPE_BLOCK_DEVICE;
		else if (S_ISCHR(info.st_mode))
			new.base_type = TYPE_CHAR_DEVICE;
		else if (S_ISFIFO(info.st_mode))
			new.base_type = TYPE_PIPE;
		else if (S_ISSOCK(info.st_mode))
			new.base_type = TYPE_SOCKET;
		else if (S_ISLNK(info.st_mode))
		{
			if (stat(tmp->str, &info))
				new.base_type = TYPE_ERROR;
			else
			{
				if (S_ISREG(info.st_mode))
					new.base_type = TYPE_FILE;
				else if (S_ISDIR(info.st_mode))
					new.base_type = TYPE_DIRECTORY;
				else if (S_ISBLK(info.st_mode))
					new.base_type = TYPE_BLOCK_DEVICE;
				else if (S_ISCHR(info.st_mode))
					new.base_type = TYPE_CHAR_DEVICE;
				else if (S_ISFIFO(info.st_mode))
					new.base_type = TYPE_PIPE;
				else if (S_ISSOCK(info.st_mode))
					new.base_type = TYPE_SOCKET;
				else
					new.base_type = TYPE_UNKNOWN;
			}

			new.flags |= ITEM_FLAG_SYMLINK;
		}
		else
			new.base_type = TYPE_UNKNOWN;
	}
	if (new.base_type == TYPE_DIRECTORY &&
			!(new.flags & ITEM_FLAG_MOUNT_POINT))
	{
		uid_t	uid = info.st_uid;
		
		/* Might be an application directory - better check...
		 * AppRun must have the same owner as the directory
		 * (to stop people putting an AppRun in, eg, /tmp)
		 */
		g_string_append(tmp, "/AppRun");
		if (!stat(tmp->str, &info) && info.st_uid == uid)
		{
			MaskedPixmap *app_icon;

			new.flags |= ITEM_FLAG_APPDIR;
			
			g_string_truncate(tmp, tmp->len - 3);
			g_string_append(tmp, "Icon.xpm");
			app_icon = g_fscache_lookup(pixmap_cache, tmp->str);
			if (app_icon)
			{
				new.image = app_icon;
				new.flags |= ITEM_FLAG_TEMP_ICON;
			}
			else
				new.image = default_pixmap + TYPE_APPDIR;
		}
		else
			new.image = default_pixmap + TYPE_DIRECTORY;
	}
	else if (new.base_type == TYPE_FILE)
	{
		/* Note: for symlinks we use need the mode of the target */
		if (info.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
		{
			new.image = default_pixmap + TYPE_EXEC_FILE;
			new.flags |= ITEM_FLAG_EXEC_FILE;
		}
		else
		{
			new.mime_type = type_from_path(tmp->str);
			new.image = type_to_icon(new.mime_type);
			new.flags |= ITEM_FLAG_TEMP_ICON;
		}
	}
	else
		new.image = default_pixmap + new.base_type;

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
	new.details_width = gdk_string_width(fixed_font, details(&new));

	if (is_new == FALSE
	 && item->base_type == new.base_type
	 && item->flags == new.flags
	 && item->size == new.size
	 && item->mode == new.mode
	 && item->mtime == new.mtime
	 && item->image == new.image
	 && item->mime_type == new.mime_type
	 && item->name_width == new.name_width
	 && item->details_width == new.details_width)
		return;

	item->base_type = new.base_type;
	item->flags = new.flags;
	item->size = new.size;
	item->mode = new.mode;
	item->mtime = new.mtime;
	item->image = new.image;
	item->mime_type = new.mime_type;
	item->name_width = new.name_width;
	item->details_width = new.details_width;

	if (!is_new)
	{
		g_ptr_array_add(dir->up_items, item);
		delayed_notify(dir);
	}
}

static gint idle_callback(Directory *dir)
{
	struct dirent *ent;
	
	do
	{
		ent = readdir(dir->dir_handle);

		if (!ent)
		{
			merge_new(dir);
			sweep_deleted(dir);

			if (dir->needs_update)
			{
				rewinddir(dir->dir_handle);
				init_for_scan(dir);
				return TRUE;
			}

			closedir(dir->dir_handle);
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
		closedir(dir->dir_handle);
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
		delayed_error("ROX-Filer", dir->error);
}
