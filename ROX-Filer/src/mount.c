/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

/* mount.c - code for handling mount points */

#include <stdio.h>
#include <mntent.h>
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
time_t mtab_time;

/* Static prototypes */
static GHashTable *read_table(GHashTable *mount_points, char *path);
static time_t read_time(char *path);
static gboolean free_mp(gpointer key, gpointer value, gpointer data);
static void clear_table(GHashTable *table);
static time_t read_time(char *path);

void mount_init()
{
	fstab_mounts = g_hash_table_new(g_str_hash, g_str_equal);
	mtab_mounts = g_hash_table_new(g_str_hash, g_str_equal);

	fstab_time = read_time("/etc/fstab");
	read_table(fstab_mounts, "/etc/fstab");
	mtab_time = read_time("/etc/mtab");
	read_table(mtab_mounts, "/etc/mtab");
}

void mount_update()
{
	time_t	time;
	
	/* Ensure everything is uptodate */

	time = read_time("/etc/fstab");
	if (time != fstab_time)
	{
		printf("[ reread fstab ]\n");
		fstab_time = time;
		read_table(fstab_mounts, "/etc/fstab");
	}

	time = read_time("/etc/mtab");
	if (time != mtab_time)
	{
		printf("[ reread mtab ]\n");
		mtab_time = time;
		read_table(mtab_mounts, "/etc/mtab");
	}
}

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
