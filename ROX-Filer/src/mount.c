/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

/* mount.c - code for handling mount points */

#include <stdio.h>
#include <mntent.h>

#include <glib.h>
#include <gtk/gtk.h>

#include "mount.h"

/* Maps mount points to mntent structures */
GHashTable *mount_points = NULL;

void mount_init()
{
	FILE		*fstab;
	struct mntent	*ent;
	MountPoint	*mp;
	
	mount_points = g_hash_table_new(g_str_hash, g_str_equal);

	fstab = setmntent("/etc/fstab", "r");

	while ((ent = getmntent(fstab)))
	{
		if (strcmp(ent->mnt_dir, "swap") == 0)
			continue;

		mp = g_malloc(sizeof(MountPoint));
		mp->name = g_strdup(ent->mnt_fsname);
		mp->dir  = g_strdup(ent->mnt_dir);

		g_hash_table_insert(mount_points, mp->dir, mp);
	}

	endmntent(fstab);
	
	g_return_if_fail(fstab != NULL);
}
