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

#include "global.h"

#include "diritem.h"
#include "support.h"
#include "gui_support.h"
#include "mount.h"
#include "pixmaps.h"
#include "type.h"
#include "usericons.h"
#include "options.h"
#include "fscache.h"

static gboolean o_ignore_exec = FALSE;

/* Static prototypes */
static void set_ignore_exec(guchar *new);
static void examine_dir(guchar *path, DirItem *item);


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void diritem_init(void)
{
	option_add_int("display_ignore_exec", o_ignore_exec, set_ignore_exec);

	read_globicons();
}

/* Bring this item's structure uptodate.
 * Use diritem_stat() instead the first time to initialise the item.
 */
void diritem_restat(guchar *path, DirItem *item, gboolean make_thumb)
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

	if (item->base_type == TYPE_DIRECTORY)
		examine_dir(path, item);
	else if (item->base_type == TYPE_FILE)
	{
		/* Type determined from path before checking for executables
		 * because some mounts show everything as executable and we
		 * still want to use the file name to set the mime type.
		 */
		item->mime_type = type_from_path(path);

		/* Note: for symlinks we need the mode of the target */
		if (info.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
		{
			/* Note that the flag is set for ALL executable
			 * files, but the mime_type is only special_exec
			 * if the file doesn't have a known extension when
			 * that option is in force.
			 */
			item->flags |= ITEM_FLAG_EXEC_FILE;

			if (!o_ignore_exec)
				item->mime_type = special_exec;
		}

		if (!item->mime_type)
			item->mime_type = item->flags & ITEM_FLAG_EXEC_FILE
						? special_exec
						: text_plain;

		if (make_thumb)
			item->image = g_fscache_lookup(pixmap_cache, path);
		else
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
			pixmap_ref(im_error);
		}
		else
			item->image = type_to_icon(item->mime_type);
	}
}

DirItem *diritem_new(guchar *leafname)
{
	DirItem *item;

	item = g_new(DirItem, 1);
	item->leafname = g_strdup(leafname);
	item->may_delete = FALSE;
	item->image = NULL;
	item->base_type = TYPE_UNKNOWN;
	item->flags = 0;
	item->mime_type = NULL;

	return item;
}

void diritem_free(DirItem *item)
{
	g_return_if_fail(item != NULL);

	pixmap_unref(item->image);
	item->image = NULL;
	g_free(item->leafname);
	item->leafname = NULL;
	g_free(item);
}


/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/


static void set_ignore_exec(guchar *new)
{
	o_ignore_exec = atoi(new);
}

/* Fill in more details of the DirItem for a directory item.
 * - Looks for an image (but maybe still NULL on error)
 * - Updates ITEM_FLAG_APPDIR
 */
static void examine_dir(guchar *path, DirItem *item)
{
	uid_t	uid = item->uid;
	int	path_len;
	guchar	*tmp;
	struct stat info;

	check_globicon(path, item);

	if (item->flags & ITEM_FLAG_MOUNT_POINT)
		return;		/* Try to avoid automounter problems */

	/* Finding the icon:
	 *
	 * - If it contains a .DirIcon.png then that's the icon
	 * - If it contains an AppRun then it's an application
	 * - If it contains an AppRun but no .DirIcon then try to
	 *   use AppIcon.xpm as the icon.
	 *
	 * .DirIcon.png and AppRun must have the same owner as the
	 * directory itself, to prevent abuse of /tmp, etc.
	 * For symlinks, we want the symlink's owner.
	 */

	path_len = strlen(path);
	tmp = g_strconcat(path, "/.DirIcon.png", NULL); /* MUST alloc this */

	if (item->image)
		goto no_diricon;	/* Already got an icon */

	if (mc_lstat(tmp, &info) != 0 || info.st_uid != uid)
		goto no_diricon;	/* Missing, or wrong owner */

	if (S_ISLNK(info.st_mode) && mc_stat(tmp, &info) != 0)
		goto no_diricon;	/* Bad symlink */

	if (info.st_size > MAX_ICON_SIZE || !S_ISREG(info.st_mode))
		goto no_diricon;	/* Too big, or non-regular file */

	/* Try to load image; may still get NULL... */
	item->image = g_fscache_lookup(pixmap_cache, tmp);

no_diricon:

	/* Try to find AppRun... */
	strcpy(tmp + path_len + 1, "AppRun");

	if (mc_lstat(tmp, &info) != 0 || info.st_uid != uid)
		goto out;	/* Missing, or wrong owner */
		
	if (!(info.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)))
		goto out;	/* Not executable */

	item->flags |= ITEM_FLAG_APPDIR;

	/* Try to load AppIcon.xpm... */

	if (item->image)
		goto out;	/* Already got an icon */

	strcpy(tmp + path_len + 4, "Icon.xpm");

	/* Note: since AppRun is valid we don't need to check AppIcon.xpm
	 *	 so carefully.
	 */

	if (mc_stat(tmp, &info) != 0)
		goto out;	/* Missing, or broken symlink */

	if (info.st_size > MAX_ICON_SIZE || !S_ISREG(info.st_mode))
		goto out;	/* Too big, or non-regular file */

	/* Try to load image; may still get NULL... */
	item->image = g_fscache_lookup(pixmap_cache, tmp);

out:

	if ((item->flags & ITEM_FLAG_APPDIR) && !item->image)
	{
		/* This is an application without an icon */
		item->image = im_appdir;
		pixmap_ref(item->image);
	}

	g_free(tmp);
}
