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

/* diritem.c - get details about files */

/* Don't load icons larger than 400K (this is rather excessive, basically
 * we just want to stop people crashing the filer with huge icons).
 */
#define MAX_ICON_SIZE (400 * 1024)

#include "config.h"

#include <gtk/gtk.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "global.h"

#include "diritem.h"
#include "support.h"
#include "gui_support.h"
#include "mount.h"
#include "type.h"
#include "usericons.h"
#include "options.h"
#include "fscache.h"
#include "pixmaps.h"
#include "xtypes.h"

static Option o_ignore_exec;

#define RECENT_DELAY (5 * 60)	/* Time in seconds to consider a file recent */
#define ABOUT_NOW(time) (diritem_recent_time - time < RECENT_DELAY)
/* If you want to make use of the RECENT flag, make sure this is set to
 * the current time before calling diritem_restat().
 */
time_t diritem_recent_time;

/* Static prototypes */
static void examine_dir(const guchar *path, DirItem *item, uid_t uid);

/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void diritem_init(void)
{
	option_add_int(&o_ignore_exec, "display_ignore_exec", FALSE);

	read_globicons();
}

/* Bring this item's structure uptodate.
 * 'parent' is optional; it saves one stat() for directories.
 */
void diritem_restat(const guchar *path, DirItem *item, struct stat *parent)
{
	struct stat	info;

	if (item->image)
	{
		g_object_unref(item->image);
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
		guchar *target_path;

		item->lstat_errno = 0;
		item->size = info.st_size;
		item->mode = info.st_mode;
		item->atime = info.st_atime;
		item->ctime = info.st_ctime;
		item->mtime = info.st_mtime;
		item->uid = info.st_uid;
		item->gid = info.st_gid;
		if (ABOUT_NOW(item->mtime) || ABOUT_NOW(item->ctime))
			item->flags |= ITEM_FLAG_RECENT;

		if (S_ISLNK(info.st_mode))
		{
			if (mc_stat(path, &info))
				item->base_type = TYPE_ERROR;
			else
				item->base_type =
					mode_to_base_type(info.st_mode);

			item->flags |= ITEM_FLAG_SYMLINK;

			target_path = readlink_dup(path);
		}
		else
		{
			item->base_type = mode_to_base_type(info.st_mode);
			target_path = (guchar *) path;
		}

		if (item->base_type == TYPE_DIRECTORY)
		{
			if (mount_is_mounted(target_path, &info,
					target_path == path ? parent : NULL))
				item->flags |= ITEM_FLAG_MOUNT_POINT
						| ITEM_FLAG_MOUNTED;
			else if (g_hash_table_lookup(fstab_mounts,
							target_path))
				item->flags |= ITEM_FLAG_MOUNT_POINT;
		}

		if (path != target_path)
			g_free(target_path);
	}

	if (item->base_type == TYPE_DIRECTORY)
	{
		/* KRJW: info.st_uid will be the uid of the dir, regardless
		 * of whether `path' is a dir or a symlink to one.  Note that
		 * if path is a symlink to a dir, item->uid will be the uid
		 * of the *symlink*, but we really want the uid of the dir
		 * to which the symlink points.
		 */
		examine_dir(path, item, info.st_uid);
	}
	else if (item->base_type == TYPE_FILE)
	{
		/* Type determined from path before checking for executables
		 * because some mounts show everything as executable and we
		 * still want to use the file name to set the mime type.
		 */
		if (item->flags & ITEM_FLAG_SYMLINK)
		{
			guchar *link_path;
			link_path = readlink_dup(path);
			item->mime_type = type_from_path(link_path
								? link_path
								: path);
			g_free(link_path);
		}
		else
			item->mime_type = xtype_get(path);

		/* Note: for symlinks we need the mode of the target */
		if (info.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
		{
			/* Note that the flag is set for ALL executable
			 * files, but the mime_type is only
			 * application_executable if the file doesn't have a
			 * known extension when that option is in force.
			 */
			item->flags |= ITEM_FLAG_EXEC_FILE;

			if (!o_ignore_exec.int_value)
				item->mime_type = application_executable;
		}

		if (!item->mime_type)
			item->mime_type = item->flags & ITEM_FLAG_EXEC_FILE
						? application_executable
						: text_plain;

		check_globicon(path, item);
	}
	else
		check_globicon(path, item);


	if (!item->mime_type)
		item->mime_type = mime_type_from_base_type(item->base_type);

	if (!item->image)
	{
		if (item->base_type == TYPE_ERROR)
		{
			item->image = im_error;
			g_object_ref(im_error);
		}
		else
			item->image = type_to_icon(item->mime_type);
	}
}

DirItem *diritem_new(const guchar *leafname)
{
	DirItem		*item;

	item = g_new(DirItem, 1);
	item->leafname = g_strdup(leafname);
	item->may_delete = FALSE;
	item->image = NULL;
	item->base_type = TYPE_UNKNOWN;
	item->flags = 0;
	item->mime_type = NULL;
	item->leafname_collate = collate_key_new(leafname);

	return item;
}

void diritem_free(DirItem *item)
{
	g_return_if_fail(item != NULL);

	if (item->image)
		g_object_unref(item->image);
	item->image = NULL;
	collate_key_free(item->leafname_collate);
	g_free(item->leafname);
	g_free(item);
}


/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/

/* Fill in more details of the DirItem for a directory item.
 * - Looks for an image (but maybe still NULL on error)
 * - Updates ITEM_FLAG_APPDIR
 *
 * KRJW (26 Jan 2003): Pass extra uid var, which will be the uid of
 * the dir given by `path' (or the uid of the dir pointed to by `path'
 * if `path' is a symlink).
 */
static void examine_dir(const guchar *path, DirItem *item, uid_t uid)
{
	struct stat info;
	static GString *tmp = NULL;

	if (!tmp)
		tmp = g_string_new(NULL);

	check_globicon(path, item);

	if (item->flags & ITEM_FLAG_MOUNT_POINT)
	{
		item->mime_type = inode_mountpoint;
		return;		/* Try to avoid automounter problems */
	}

	/* Finding the icon:
	 *
	 * - If it contains a .DirIcon then that's the icon
	 * - If it contains an AppRun then it's an application
	 * - If it contains an AppRun but no .DirIcon then try to
	 *   use AppIcon.xpm as the icon.
	 *
	 * .DirIcon and AppRun must have the same owner as the
	 * directory itself, to prevent abuse of /tmp, etc.
	 * For symlinks, we want the symlink's owner.
	 */

	g_string_printf(tmp, "%s/.DirIcon", path);

	if (item->image)
		goto no_diricon;	/* Already got an icon */

	if (mc_lstat(tmp->str, &info) != 0 || info.st_uid != uid)
		goto no_diricon;	/* Missing, or wrong owner */

	if (S_ISLNK(info.st_mode) && mc_stat(tmp->str, &info) != 0)
		goto no_diricon;	/* Bad symlink */

	if (info.st_size > MAX_ICON_SIZE || !S_ISREG(info.st_mode))
		goto no_diricon;	/* Too big, or non-regular file */

	/* Try to load image; may still get NULL... */
	item->image = g_fscache_lookup(pixmap_cache, tmp->str);

no_diricon:

	/* Try to find AppRun... */
	g_string_truncate(tmp, tmp->len - 8);
	g_string_append(tmp, "AppRun");

	if (mc_lstat(tmp->str, &info) != 0 || info.st_uid != uid)
		goto out;	/* Missing, or wrong owner */
		
	if (!(info.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)))
		goto out;	/* Not executable */

	item->flags |= ITEM_FLAG_APPDIR;

	/* Try to load AppIcon.xpm... */

	if (item->image)
		goto out;	/* Already got an icon */

	g_string_truncate(tmp, tmp->len - 3);
	g_string_append(tmp, "Icon.xpm");

	/* Note: since AppRun is valid we don't need to check AppIcon.xpm
	 *	 so carefully.
	 */

	if (mc_stat(tmp->str, &info) != 0)
		goto out;	/* Missing, or broken symlink */

	if (info.st_size > MAX_ICON_SIZE || !S_ISREG(info.st_mode))
		goto out;	/* Too big, or non-regular file */

	/* Try to load image; may still get NULL... */
	item->image = g_fscache_lookup(pixmap_cache, tmp->str);

out:

	if ((item->flags & ITEM_FLAG_APPDIR) && !item->image)
	{
		/* This is an application without an icon */
		item->image = im_appdir;
		g_object_ref(item->image);
	}
}
