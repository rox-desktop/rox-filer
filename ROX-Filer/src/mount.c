/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

/* mount.c - code for handling mount points */

#include <config.h>
#include <stdio.h>
#ifdef HAVE_MNTENT_H
  /* Linux, etc */
# include <mntent.h>
#elif HAVE_SYS_UCRED_H
  /* NetBSD, etc */
# include <fstab.h>
# include <sys/param.h>
# include <sys/ucred.h>
# include <sys/mount.h>
# include <stdlib.h>
#endif
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <glib.h>
#include <gtk/gtk.h>

#include "mount.h"

/* Map mount points to mntent structures */
GHashTable *fstab_mounts = NULL;
GHashTable *mtab_mounts = NULL;
time_t fstab_time;
#ifdef HAVE_MNTENT_H
time_t mtab_time;
#endif

/* Static prototypes */
#ifdef HAVE_SYS_UCRED_H
static GHashTable *build_mtab_table(GHashTable *mount_points);
#endif
#ifdef DO_MOUNT_POINTS
static GHashTable *read_table(GHashTable *mount_points, char *path);
static void clear_table(GHashTable *table);
static time_t read_time(char *path);
static gboolean free_mp(gpointer key, gpointer value, gpointer data);
#endif

void mount_init()
{
	fstab_mounts = g_hash_table_new(g_str_hash, g_str_equal);
	mtab_mounts = g_hash_table_new(g_str_hash, g_str_equal);

#ifdef DO_MOUNT_POINTS
	fstab_time = read_time("/etc/fstab");
	read_table(fstab_mounts, "/etc/fstab");
#  ifdef HAVE_MNTENT_H
	mtab_time = read_time("/etc/mtab");
	read_table(mtab_mounts, "/etc/mtab");
#  elif HAVE_SYS_UCRED_H
	/* mtab_time not used */
	build_mtab_table(mtab_mounts);
#  endif
#endif /* DO_MOUNT_POINTS */
}

void mount_update()
{
#ifdef DO_MOUNT_POINTS
	time_t	time;
	/* Ensure everything is uptodate */

	time = read_time("/etc/fstab");
	if (time != fstab_time)
	{
		fstab_time = time;
		read_table(fstab_mounts, "/etc/fstab");
	}
#  ifdef HAVE_MNTENT_H
	time = read_time("/etc/mtab");
	if (time != mtab_time)
	{
		mtab_time = time;
		read_table(mtab_mounts, "/etc/mtab");
	}
#  else
	build_mtab_table(mtab_mounts);
#  endif
#endif /* DO_MOUNT_POINTS */
}


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

#elif HAVE_SYS_UCRED_H	/* We don't have /etc/mtab */

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


/* build table of mounted file systems */
static GHashTable *build_mtab_table(GHashTable *mount_points)
{
	int		fsstat_index;
	int		fsstat_entries;
	struct statfs	*mntbufp;
	MountPoint	*mp;

	clear_table( mount_points);

	/* we could use getfsstat twice and do the memory allocation
	 * ourselves, but I feel lazy today. we can't free memory after use
	 * though.
	 */
	fsstat_entries = getmntinfo( &mntbufp, MNT_WAIT);
	/* wait for mount entries to be updated by each file system.
	 * Use MNT_NOWAIT if you don't want this to block, but results
	 * may not be up to date.
	 */
	g_return_val_if_fail(fsstat_entries >= 0, NULL);
	if (fsstat_entries == 0) return NULL;
		/* not a failure but nothing mounted! */

	for (fsstat_index = 0; fsstat_index < fsstat_entries; fsstat_index++)
	{
		if(strcmp(mntbufp[fsstat_index].f_fstypename, "swap") == 0)
			continue;
		if(strcmp(mntbufp[fsstat_index].f_fstypename, "kernfs") == 0)
			continue;

		mp = g_malloc(sizeof(MountPoint));
		mp->name = g_strdup( mntbufp[fsstat_index].f_mntfromname);
		mp->dir  = g_strdup( mntbufp[fsstat_index].f_mntonname);

		g_hash_table_insert(mount_points, mp->dir, mp);
	}

	return mount_points;
}

#endif
