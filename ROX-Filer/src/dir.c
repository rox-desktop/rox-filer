/*
 * $Id$
 *
 * Copyright (C) 2003, the ROX-Filer team.
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
 * - This list is compared to the current DirItems, removing any that are now
 *   missing.
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
#include "diritem.h"
#include "support.h"
#include "gui_support.h"
#include "dir.h"
#include "fscache.h"
#include "mount.h"
#include "pixmaps.h"
#include "type.h"
#include "usericons.h"
#include "main.h"

#ifdef USE_DNOTIFY
/* Newer Linux kernels can tell us when the directories we are watching
 * change, using the dnotify system.
 */
static GHashTable *dnotify_fd_to_dir = NULL;
gboolean dnotify_wakeup_flag = FALSE;
static int dnotify_last_fd = -1;
#endif

GFSCache *dir_cache = NULL;

/* Static prototypes */
static void update(Directory *dir, gchar *pathname, gpointer data);
static void set_idle_callback(Directory *dir);
static DirItem *insert_item(Directory *dir, const guchar *leafname);
static void remove_missing(Directory *dir, GPtrArray *keep);
static void dir_recheck(Directory *dir,
			const guchar *path, const guchar *leafname);
static GPtrArray *hash_to_array(GHashTable *hash);
static void dir_force_update_item(Directory *dir, const gchar *leaf);
static Directory *dir_new(const char *pathname);
static void dir_rescan(Directory *dir);
#ifdef USE_DNOTIFY
static void dir_rescan_soon(Directory *dir);
static void dnotify_handler(int sig, siginfo_t *si, void *data);
#endif

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void dir_init(void)
{
	dir_cache = g_fscache_new((GFSLoadFunc) dir_new,
				(GFSUpdateFunc) update, NULL);

	/* Check for dnotify support in the kernel */
#ifdef USE_DNOTIFY
	{
		struct sigaction act;

		act.sa_sigaction = dnotify_handler;
		sigemptyset(&act.sa_mask);
		act.sa_flags = SA_SIGINFO;
		sigaction(SIGRTMIN, &act, NULL);

		/* Sometimes we get this instead of SIGRTMIN.
		 * Don't know why :-( but don't crash...
		 */
		act.sa_handler = SIG_IGN;
		sigemptyset(&act.sa_mask);
		act.sa_flags = 0;
		sigaction(SIGIO, &act, NULL);

		dnotify_fd_to_dir = g_hash_table_new(NULL, NULL);
	}
#endif
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
	GPtrArray *items;

	g_return_if_fail(dir != NULL);
	g_return_if_fail(callback != NULL);

	user = g_new(DirUser, 1);
	user->callback = callback;
	user->data = data;

#ifdef USE_DNOTIFY
	if (!dir->users)
	{
		int fd;
		
		if (dir->dnotify_fd != -1)
			g_warning("dir_attach: dnotify error\n");
		
		fd = open(dir->pathname, O_RDONLY);
		g_return_if_fail(g_hash_table_lookup(dnotify_fd_to_dir,
				 GINT_TO_POINTER(fd)) == NULL);
		if (fd != -1)
		{
			dir->dnotify_fd = fd;
			g_hash_table_insert(dnotify_fd_to_dir,
					GINT_TO_POINTER(fd), dir);
			fcntl(fd, F_SETSIG, SIGRTMIN);
			fcntl(fd, F_NOTIFY, DN_CREATE | DN_DELETE | DN_RENAME |
					    DN_ATTRIB | DN_MULTISHOT);
		}
	}
#endif
	
	dir->users = g_list_prepend(dir->users, user);

	g_object_ref(dir);

	items = hash_to_array(dir->known_items);
	if (items->len)
		callback(dir, DIR_ADD, items, data);
	g_ptr_array_free(items, TRUE);

	if (dir->needs_update && !dir->scanning)
		dir_rescan(dir);

	/* May start scanning if noone was watching before */
	set_idle_callback(dir);

	if (!dir->scanning)
		callback(dir, DIR_END_SCAN, NULL, data);
}

/* Undo the effect of dir_attach */
void dir_detach(Directory *dir, DirCallback callback, gpointer data)
{
	DirUser	*user;
	GList	*list;

	g_return_if_fail(dir != NULL);
	g_return_if_fail(callback != NULL);

	for (list = dir->users; list; list = list->next)
	{
		user = (DirUser *) list->data;
		if (user->callback == callback && user->data == data)
		{
			g_free(user);
			dir->users = g_list_remove(dir->users, user);
			g_object_unref(dir);

			/* May stop scanning if noone's watching */
			set_idle_callback(dir);

#ifdef USE_DNOTIFY
			if (!dir->users && dir->dnotify_fd != -1)
			{
				close(dir->dnotify_fd);
				g_hash_table_remove(dnotify_fd_to_dir,
					GINT_TO_POINTER(dir->dnotify_fd));
				dir->dnotify_fd = -1;
			}
#endif
			return;
		}
	}

	g_warning("dir_detach: Callback/data pair not attached!\n");
}

void dir_update(Directory *dir, gchar *pathname)
{
	update(dir, pathname, NULL);
}

/* Rescan this directory */
void refresh_dirs(const char *path)
{
	g_fscache_update(dir_cache, path);
}

/* When something has happened to a particular object, call this
 * and all appropriate changes will be made.
 */
void dir_check_this(const guchar *path)
{
	guchar	*real_path;
	guchar	*dir_path;
	Directory *dir;

	dir_path = g_path_get_dirname(path);
	real_path = pathdup(dir_path);
	g_free(dir_path);

	dir = g_fscache_lookup_full(dir_cache, real_path,
					FSCACHE_LOOKUP_PEEK, NULL);
	if (dir)
	{
		dir_recheck(dir, real_path, g_basename(path));
		g_object_unref(dir);
	}
	
	g_free(real_path);
}

#ifdef USE_DNOTIFY
static void drop_dnotify(gpointer key, gpointer value, gpointer data)
{
	close(GPOINTER_TO_INT(key));
}
#endif

/* Used when we fork an action child, otherwise we can't delete or unmount
 * any directory which we're watching!
 */
void dir_drop_all_dnotifies(void)
{
#ifdef USE_DNOTIFY
	g_hash_table_foreach(dnotify_fd_to_dir, drop_dnotify, NULL);
#endif
}

/* Tell watchers that this item has changed, but don't rescan.
 * (used when thumbnail has been created for an item)
 */
void dir_force_update_path(const gchar *path)
{
	gchar	*dir_path;
	Directory *dir;

	g_return_if_fail(path[0] == '/');

	dir_path = g_path_get_dirname(path);

	dir = g_fscache_lookup_full(dir_cache, dir_path, FSCACHE_LOOKUP_PEEK,
			NULL);
	if (dir)
	{
		dir_force_update_item(dir, g_basename(path));
		g_object_unref(dir);
	}
	
	g_free(dir_path);
}

/* Ensure that 'leafname' is up-to-date. Returns the new/updated
 * DirItem, or NULL if the file no longer exists.
 */
DirItem *dir_update_item(Directory *dir, const gchar *leafname)
{
	DirItem *item;
	
	time(&diritem_recent_time);
	item = insert_item(dir, leafname);
	dir_merge_new(dir);

	return item;
}

static int sort_names(const void *a, const void *b)
{
	return strcmp(*((char **) a), *((char **) b));
}

static void free_recheck_list(Directory *dir)
{
	destroy_glist(&dir->recheck_list);
}

/* If scanning state has changed then notify all filer windows */
static void dir_set_scanning(Directory *dir, gboolean scanning)
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

/* Notify everyone that the error status of the directory has changed */
static void dir_error_changed(Directory *dir)
{
	GList	*next;

	for (next = dir->users; next; next = next->next)
	{
		DirUser *user = (DirUser *) next->data;

		user->callback(dir, DIR_ERROR_CHANGED, NULL, user->data);
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

	/* usleep(800); */

	insert_item(dir, leaf);

	g_free(leaf);

	if (dir->recheck_list)
		return TRUE;	/* Call again */

	/* The recheck_list list empty. Stop scanning, unless
	 * needs_update, in which case we start scanning again.
	 */

	dir_merge_new(dir);
	
	dir->have_scanned = TRUE;
	dir_set_scanning(dir, FALSE);
	gtk_idle_remove(dir->idle_callback);
	dir->idle_callback = 0;

	if (dir->needs_update)
		dir_rescan(dir);

	return FALSE;
}

/* Add all the new items to the items array.
 * Notify everyone who is watching us.
 */
void dir_merge_new(Directory *dir)
{
	GPtrArray *new = dir->new_items;
	GPtrArray *up = dir->up_items;
	GPtrArray *gone = dir->gone_items;
	GList	  *list;
	guint	  i;

	for (list = dir->users; list; list = list->next)
	{
		DirUser *user = (DirUser *) list->data;

		if (new->len)
			user->callback(dir, DIR_ADD, new, user->data);
		if (up->len)
			user->callback(dir, DIR_UPDATE, up, user->data);
		if (gone->len)
			user->callback(dir, DIR_REMOVE, gone, user->data);
	}

	for (i = 0; i < new->len; i++)
	{
		DirItem *item = (DirItem *) new->pdata[i];

		g_hash_table_insert(dir->known_items, item->leafname, item);
	}

	for (i = 0; i < gone->len; i++)
	{
		DirItem	*item = (DirItem *) gone->pdata[i];

		diritem_free(item);
	}
	
	g_ptr_array_set_size(gone, 0);
	g_ptr_array_set_size(new, 0);
	g_ptr_array_set_size(up, 0);
}

#ifdef USE_DNOTIFY
/* Called from the mainloop shortly after dnotify_handler */
void dnotify_wakeup(void)
{
	Directory *dir;

	dnotify_wakeup_flag = FALSE;

	dir = g_hash_table_lookup(dnotify_fd_to_dir,
				  GINT_TO_POINTER(dnotify_last_fd));

	if (dir)
		dir_rescan_soon(dir);
}
#endif

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

#ifdef USE_DNOTIFY
static gint rescan_soon_timeout(gpointer data)
{
	Directory *dir = (Directory *) data;

	dir->rescan_timeout = -1;
	if (dir->scanning)
		dir->needs_update = TRUE;
	else
		dir_rescan(dir);
	return FALSE;
}

/* Wait a fraction of a second and then rescan. If already waiting,
 * this function does nothing.
 */
static void dir_rescan_soon(Directory *dir)
{
	if (dir->rescan_timeout != -1)
		return;
	dir->rescan_timeout = gtk_timeout_add(500, rescan_soon_timeout, dir);
}
#endif

static void free_items_array(GPtrArray *array)
{
	guint	i;

	for (i = 0; i < array->len; i++)
	{
		DirItem	*item = (DirItem *) array->pdata[i];

		diritem_free(item);
	}

	g_ptr_array_free(array, TRUE);
}

/* Tell everyone watching that these items have gone */
static void notify_deleted(Directory *dir, GPtrArray *deleted)
{
	GList	*next;
	
	if (!deleted->len)
		return;

	for (next = dir->users; next; next = next->next)
	{
		DirUser *user = (DirUser *) next->data;

		user->callback(dir, DIR_REMOVE, deleted, user->data);
	}
}

static void mark_unused(gpointer key, gpointer value, gpointer data)
{
	DirItem	*item = (DirItem *) value;

	item->may_delete = TRUE;
}

static void keep_deleted(gpointer key, gpointer value, gpointer data)
{
	DirItem	*item = (DirItem *) value;
	GPtrArray *deleted = (GPtrArray *) data;

	if (item->may_delete)
		g_ptr_array_add(deleted, item);
}

static gboolean check_unused(gpointer key, gpointer value, gpointer data)
{
	DirItem	*item = (DirItem *) value;

	return item->may_delete;
}

/* Remove all the old items that have gone.
 * Notify everyone who is watching us of the removed items.
 */
static void remove_missing(Directory *dir, GPtrArray *keep)
{
	GPtrArray	*deleted;
	guint		i;

	deleted = g_ptr_array_new();

	/* Mark all current items as may_delete */
	g_hash_table_foreach(dir->known_items, mark_unused, NULL);

	/* Unmark all items also in 'keep' */
	for (i = 0; i < keep->len; i++)
	{
		guchar	*leaf = (guchar *) keep->pdata[i];
		DirItem *item;

		item = g_hash_table_lookup(dir->known_items, leaf);

		if (item)
			item->may_delete = FALSE;
	}

	/* Add each item still marked to 'deleted' */
	g_hash_table_foreach(dir->known_items, keep_deleted, deleted);

	/* Remove all items still marked */
	g_hash_table_foreach_remove(dir->known_items, check_unused, NULL);

	notify_deleted(dir, deleted);

	free_items_array(deleted);
}

static gint notify_timeout(gpointer data)
{
	Directory	*dir = (Directory *) data;

	g_return_val_if_fail(dir->notify_active == TRUE, FALSE);

	dir_merge_new(dir);

	dir->notify_active = FALSE;
	g_object_unref(dir);

	return FALSE;
}

/* Call dir_merge_new() after a while. */
static void delayed_notify(Directory *dir)
{
	if (dir->notify_active)
		return;
	g_object_ref(dir);
	gtk_timeout_add(1500, notify_timeout, dir);
	dir->notify_active = TRUE;
}

/* Stat this item and add, update or remove it.
 * Returns the new/updated item, if any.
 * (leafname may be from the current DirItem item)
 * Ensure diritem_recent_time is reasonably up-to-date before calling this.
 */
static DirItem *insert_item(Directory *dir, const guchar *leafname)
{
	const gchar  	*full_path;
	DirItem		*item;
	DirItem		old;
	gboolean	do_compare = FALSE;	/* (old is filled in) */

	if (leafname[0] == '.' && (leafname[1] == '\n' ||
			(leafname[1] == '.' && leafname[2] == '\n')))
		return NULL;		/* Ignore '.' and '..' */

	full_path = make_path(dir->pathname, leafname);
	item = g_hash_table_lookup(dir->known_items, leafname);

	if (item)
	{
		if (item->base_type != TYPE_UNKNOWN)
		{
			/* Preserve the old details so we can compare */
			old = *item;
			if (old.image)
				g_object_ref(old.image);
			do_compare = TRUE;
		}
		diritem_restat(full_path, item, &dir->stat_info);
	}
	else
	{
		/* Item isn't already here. This won't normally happen,
		 * because blank items are added when scanning, before
		 * we get here.
		 */
		item = diritem_new(leafname);
		diritem_restat(full_path, item, &dir->stat_info);
		if (item->base_type == TYPE_ERROR &&
				item->lstat_errno == ENOENT)
		{
			diritem_free(item);
			return NULL;
		}
		g_ptr_array_add(dir->new_items, item);

	}

	if (item->base_type == TYPE_ERROR && item->lstat_errno == ENOENT)
	{
		/* Item has been deleted */
		g_hash_table_remove(dir->known_items, item->leafname);
		g_ptr_array_add(dir->gone_items, item);
		if (do_compare && old.image)
			g_object_unref(old.image);
		delayed_notify(dir);
		return NULL;
	}

	if (do_compare)
	{
		if (item->lstat_errno == old.lstat_errno
		 && item->base_type == old.base_type
		 && item->flags == old.flags
		 && item->size == old.size
		 && item->mode == old.mode
		 && item->atime == old.atime
		 && item->ctime == old.ctime
		 && item->mtime == old.mtime
		 && item->uid == old.uid
		 && item->gid == old.gid
		 && item->image == old.image
		 && item->mime_type == old.mime_type)
		{
			if (old.image)
				g_object_unref(old.image);
			return item;
		}
		if (old.image)
			g_object_unref(old.image);
	}

	g_ptr_array_add(dir->up_items, item);
	delayed_notify(dir);

	return item;
}

static void update(Directory *dir, gchar *pathname, gpointer data)
{
	g_free(dir->pathname);
	dir->pathname = pathdup(pathname);

	if (dir->scanning)
		dir->needs_update = TRUE;
	else
		dir_rescan(dir);
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
		time(&diritem_recent_time);
		dir->idle_callback = gtk_idle_add(recheck_callback, dir);
		/* Do the first call now (will remove the callback itself) */
		recheck_callback(dir);
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

/* See dir_force_update_path() */
static void dir_force_update_item(Directory *dir, const gchar *leaf)
{
	GList *list;
	GPtrArray *items;
	DirItem *item;

	items = g_ptr_array_new();

	item = g_hash_table_lookup(dir->known_items, leaf);
	if (!item)
		goto out;

	g_ptr_array_add(items, item);

	for (list = dir->users; list; list = list->next)
	{
		DirUser *user = (DirUser *) list->data;

		user->callback(dir, DIR_UPDATE, items, user->data);
	}

out:
	g_ptr_array_free(items, TRUE);
}

static void dir_recheck(Directory *dir,
			const guchar *path, const guchar *leafname)
{
	guchar *old = dir->pathname;

	dir->pathname = g_strdup(path);
	g_free(old);

	time(&diritem_recent_time);
	insert_item(dir, leafname);
}

static void to_array(gpointer key, gpointer value, gpointer data)
{
	GPtrArray *array = (GPtrArray *) data;

	g_ptr_array_add(array, value);
}

/* Convert a hash table to an unsorted GPtrArray.
 * g_ptr_array_free() the result.
 */
static GPtrArray *hash_to_array(GHashTable *hash)
{
	GPtrArray *array;

	array = g_ptr_array_new();

	g_hash_table_foreach(hash, to_array, array);

	return array;
}

static gpointer parent_class;

/* Note: dir_cache is never purged, so this shouldn't get called */
static void dir_finialize(GObject *object)
{
	GPtrArray *items;
	Directory *dir = (Directory *) object;

	g_return_if_fail(dir->users == NULL);

	g_print("[ dir finalize ]\n");

	free_recheck_list(dir);
	set_idle_callback(dir);
	if (dir->rescan_timeout != -1)
		gtk_timeout_remove(dir->rescan_timeout);

	dir_merge_new(dir);	/* Ensures new, up and gone are empty */

	g_ptr_array_free(dir->up_items, TRUE);
	g_ptr_array_free(dir->new_items, TRUE);
	g_ptr_array_free(dir->gone_items, TRUE);

	items = hash_to_array(dir->known_items);
	free_items_array(items);
	g_hash_table_destroy(dir->known_items);
	
	g_free(dir->error);
	g_free(dir->pathname);

	G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void directory_class_init(gpointer gclass, gpointer data)
{
	GObjectClass *object = (GObjectClass *) gclass;

	parent_class = g_type_class_peek_parent(gclass);

	object->finalize = dir_finialize;
}

static void directory_init(GTypeInstance *object, gpointer gclass)
{
	Directory *dir = (Directory *) object;

	dir->known_items = g_hash_table_new(g_str_hash, g_str_equal);
	dir->recheck_list = NULL;
	dir->idle_callback = 0;
	dir->scanning = FALSE;
	dir->have_scanned = FALSE;
	
	dir->users = NULL;
	dir->needs_update = TRUE;
	dir->notify_active = FALSE;
	dir->pathname = NULL;
	dir->error = NULL;
	dir->rescan_timeout = -1;
#ifdef USE_DNOTIFY
	dir->dnotify_fd = -1;
#endif

	dir->new_items = g_ptr_array_new();
	dir->up_items = g_ptr_array_new();
	dir->gone_items = g_ptr_array_new();
}

static GType dir_get_type(void)
{
	static GType type = 0;

	if (!type)
	{
		static const GTypeInfo info =
		{
			sizeof (DirectoryClass),
			NULL,			/* base_init */
			NULL,			/* base_finalise */
			directory_class_init,
			NULL,			/* class_finalise */
			NULL,			/* class_data */
			sizeof(Directory),
			0,			/* n_preallocs */
			directory_init
		};

		type = g_type_register_static(G_TYPE_OBJECT, "Directory",
					      &info, 0);
	}

	return type;
}

static Directory *dir_new(const char *pathname)
{
	Directory *dir;

	dir = g_object_new(dir_get_type(), NULL);

	dir->pathname = g_strdup(pathname);
	
	return dir;
}

/* Get the names of all files in the directory.
 * Remove any DirItems that are no longer listed.
 * Replace the recheck_list with the items found.
 */
static void dir_rescan(Directory *dir)
{
	GPtrArray	*names;
	DIR		*d;
	struct dirent	*ent;
	guint		i;
	const char	*pathname;
	int		longest_len = -1;
	GList		*longest = NULL;

	g_return_if_fail(dir != NULL);

	pathname = dir->pathname;

	dir->needs_update = FALSE;

	names = g_ptr_array_new();

	read_globicons();
	mount_update(FALSE);
	if (dir->error)
	{
		null_g_free(&dir->error);
		dir_error_changed(dir);
	}

	/* Saves statting the parent for each item... */
	if (mc_stat(pathname, &dir->stat_info))
	{
		dir->error = g_strdup_printf(_("Can't stat directory: %s"),
				g_strerror(errno));
		dir_error_changed(dir);
		return;		/* Report on attach */
	}

	d = mc_opendir(pathname);
	if (!d)
	{
		dir->error = g_strdup_printf(_("Can't open directory: %s"),
				g_strerror(errno));
		dir_error_changed(dir);
		return;		/* Report on attach */
	}

	dir_set_scanning(dir, TRUE);
	dir_merge_new(dir);
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
	mc_closedir(d);

	/* Sort, so the names are scanned in a sensible order */
	qsort(names->pdata, names->len, sizeof(guchar *), sort_names);

	/* Compare the list with the current DirItems, removing
	 * any that are missing.
	 */
	remove_missing(dir, names);

	free_recheck_list(dir);

	/* For each name found, add it to the recheck_list.
	 * If the item is new, put a blank place-holder item in the directory.
	 */
	for (i = 0; i < names->len; i++)
	{
		guchar *name = names->pdata[i];
		int len = strlen(name);
		
		dir->recheck_list = g_list_prepend(dir->recheck_list, name);
		if (!g_hash_table_lookup(dir->known_items, name))
		{
			DirItem *new;

			new = diritem_new(name);
			g_ptr_array_add(dir->new_items, new);
		}

		if (name[0] != '.' && len > longest_len)
		{
			longest_len = len;
			longest = dir->recheck_list;
		}
	}

	dir_merge_new(dir);
	
	dir->recheck_list = g_list_reverse(dir->recheck_list);

	if (longest)
	{
		gpointer data = longest->data;

		/* Move the longest item to the start. Helps with
		 * auto sizing.
		 */
		dir->recheck_list = g_list_remove_link(dir->recheck_list,
							longest);
		dir->recheck_list = g_list_prepend(dir->recheck_list, data);
	}

	g_ptr_array_free(names, TRUE);
		
	set_idle_callback(dir);
	dir_merge_new(dir);
}

#ifdef USE_DNOTIFY
/* Signal handler - don't do anything dangerous here */
static void dnotify_handler(int sig, siginfo_t *si, void *data)
{
	/* Note: there is currently only one place to store the fd,
	 * so we'll miss updates to several directories if they happen
	 * close together.
	 */
	dnotify_last_fd = si->si_fd;
	dnotify_wakeup_flag = TRUE;
	write(to_wakeup_pipe, "\0", 1);	/* Wake up! */
}
#endif
