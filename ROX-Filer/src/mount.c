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

/* mount.c - code for handling mount points */

#include <config.h>
#include <stdio.h>
#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_MNTENT_H
  /* Linux, etc */
# include <mntent.h>
#elif HAVE_SYS_UCRED_H
  /* NetBSD, OSF1, etc */
# include <fstab.h>
# include <sys/types.h>
# include <sys/param.h>
# include <sys/ucred.h>
# include <sys/mount.h>
# include <stdlib.h>
#elif HAVE_SYS_MNTENT_H
  /* SunOS */
# include <sys/mntent.h>
# include <sys/mnttab.h>
#endif
#include <sys/time.h>

#include <gtk/gtk.h>

#include "global.h"

#include "mount.h"

/* Map mount points to mntent structures */
GHashTable *fstab_mounts = NULL;
time_t fstab_time;

#ifdef HAVE_SYS_MNTENT_H
#define THE_FSTAB VFSTAB
#else
#define THE_FSTAB "/etc/fstab"
#endif

/* Static prototypes */
#ifdef DO_MOUNT_POINTS
static GHashTable *read_table(GHashTable *mount_points, char *path);
static void clear_table(GHashTable *table);
static time_t read_time(char *path);
static gboolean free_mp(gpointer key, gpointer value, gpointer data);
#endif


/****************************************************************
 *			EXTERNAL INTERFACE			*
 ****************************************************************/

void mount_init(void)
{
	fstab_mounts = g_hash_table_new(g_str_hash, g_str_equal);

#ifdef DO_MOUNT_POINTS
	fstab_time = read_time(THE_FSTAB);
	read_table(fstab_mounts, THE_FSTAB);
#endif
}

/* If force is true then ignore the timestamps */
void mount_update(gboolean force)
{
#ifdef DO_MOUNT_POINTS
	time_t	time;

	time = read_time(THE_FSTAB);
	if (force || time != fstab_time)
	{
		fstab_time = time;
		read_table(fstab_mounts, THE_FSTAB);
	}
#endif /* DO_MOUNT_POINTS */
}

/* TRUE iff this directory is a mount point. Uses python's method to
 * check:
 * The function checks whether path's parent, path/.., is on a different device
 * than path, or whether path/.. and path point to the same i-node on the same
 * device -- this should detect mount points for all Unix and POSIX variants.
 */
gboolean mount_is_mounted(guchar *path)
{
	struct stat info_path, info_parent;
	guchar *tmp;

	if (stat(path, &info_path))
		return FALSE; /*It doesn't exist -- so not a mount point :-) */

	tmp = g_strconcat(path, "/..", NULL);
	if (stat(tmp, &info_parent))
	{
		g_free(tmp);
		return FALSE;
	}
	g_free(tmp);

	if (info_path.st_dev != info_parent.st_dev)
		return TRUE;

	if (info_path.st_ino == info_parent.st_ino)
		return TRUE;	/* Same device and inode */
		
	return FALSE;
}

/****************************************************************
 *			INTERNAL FUNCTIONS			*
 ****************************************************************/


#ifdef DO_MOUNT_POINTS

static gboolean free_mp(gpointer key, gpointer value, gpointer data)
{
	MountPoint	*mp = (MountPoint *) value;

	g_free(mp->name);
	g_free(mp->dir);
	g_free(mp);

	return TRUE;
}

/* Remove all entries from a hash table, freeing them as we go */
static void clear_table(GHashTable *table)
{
	g_hash_table_foreach_remove(table, free_mp, NULL);
}

/* Return the mtime of a file */
static time_t read_time(char *path)
{
	struct stat info;

	g_return_val_if_fail(stat(path, &info) == 0, 0);

	return info.st_mtime;
}

#endif

#ifdef HAVE_MNTENT_H
static GHashTable *read_table(GHashTable *mount_points, char *path)
{
	FILE		*tab;
	struct mntent	*ent;
	MountPoint	*mp;

	clear_table(mount_points);

	tab = setmntent(path, "r");
	g_return_val_if_fail(tab != NULL, NULL);

	while ((ent = getmntent(tab)))
	{
		if (strcmp(ent->mnt_dir, "swap") == 0)
			continue;

		mp = g_malloc(sizeof(MountPoint));
		mp->name = g_strdup(ent->mnt_fsname);
		mp->dir  = g_strdup(ent->mnt_dir);

		g_hash_table_insert(mount_points, mp->dir, mp);
	}

	endmntent(tab);

	return mount_points;
}

#elif HAVE_SYS_MNTENT_H
static GHashTable *read_table(GHashTable *mount_points, char *path)
{
	FILE		*tab;
	struct mnttab	ent;
	MountPoint	*mp;
#ifdef HAVE_FCNTL_H
	struct flock	lb;
#endif

	tab = fopen(path, "r");
	g_return_val_if_fail(tab != NULL, NULL);
	clear_table(mount_points);

#ifdef HAVE_FCNTL_H
	lb.l_type = F_RDLCK;
	lb.l_whence = 0;
	lb.l_start = 0;
	lb.l_len = 0;
	fcntl(fileno(tab), F_SETLKW, &lb);
#endif

	while (getmntent(tab, &ent)==0)
	{
		if (strcmp(ent.mnt_special, "swap") == 0)
			continue;

		mp = g_malloc(sizeof(MountPoint));
		mp->dir = g_strdup(ent.mnt_mountp);
		mp->name = g_strdup(ent.mnt_special);

		g_hash_table_insert(mount_points, mp->dir, mp);
	}

	fclose(tab);

	return mount_points;
}

#elif HAVE_SYS_UCRED_H	/* We don't have /etc/mtab */

/* XXX: Don't use mtab anymore. Is this still needed? */

static GHashTable *read_table(GHashTable *mount_points, char *path)
{
	int		tab;
	struct fstab	*ent;
	MountPoint	*mp;

	clear_table(mount_points);

	tab = setfsent();
	g_return_val_if_fail(tab != 0, NULL);

	while ((ent = getfsent()))
	{
		if (strcmp(ent->fs_vfstype, "swap") == 0)
			continue;
		if (strcmp(ent->fs_vfstype, "kernfs") == 0)
			continue;

		mp = g_malloc(sizeof(MountPoint));
		mp->name = g_strdup(ent->fs_spec);	/* block special device name */
		mp->dir  = g_strdup(ent->fs_file);	/* file system path prefix */

		g_hash_table_insert(mount_points, mp->dir, mp);
	}

	endfsent();

	return mount_points;
}

#endif
