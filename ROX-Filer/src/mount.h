/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _MOUNT_H
#define _MOUNT_H

#  if defined(HAVE_MNTENT_H) || defined(HAVE_SYS_UCRED_H)
#    define DO_MOUNT_POINTS
#  endif

extern GHashTable *fstab_mounts;
extern GHashTable *mtab_mounts;

typedef struct _MountPoint MountPoint;

struct _MountPoint
{
	char	*name;		/* eg: /dev/hda4 */
	char	*dir;		/* eg: /home */
};

/* Prototypes */
void mount_init();
void mount_update(gboolean force);

#endif /* _MOUNT_H */
