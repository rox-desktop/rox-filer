/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _MOUNT_H
#define _MOUNT_H

#  if defined(HAVE_MNTENT_H) || defined(HAVE_SYS_UCRED_H) || \
						defined(HAVE_SYS_MNTENT_H)
#    define DO_MOUNT_POINTS
#  endif

extern GHashTable *fstab_mounts;

typedef struct _MountPoint MountPoint;

struct _MountPoint
{
	char	*name;		/* eg: /dev/hda4 */
	char	*dir;		/* eg: /home */
};

/* Prototypes */
void mount_init();
void mount_update(gboolean force);
gboolean mount_is_mounted(guchar *path);

#endif /* _MOUNT_H */
