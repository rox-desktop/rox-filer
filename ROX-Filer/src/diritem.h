/*
 * $Id$
 *
 * Thomas Leonard, <tal197@users.sourceforge.net>
 */


#ifndef _DIRITEM_H
#define _DIRITEM_H

#include <sys/types.h>

extern time_t diritem_recent_time;

typedef enum
{
	ITEM_FLAG_SYMLINK 	= 0x01,	/* Is a symlink */
	ITEM_FLAG_APPDIR  	= 0x02,	/* Contains an AppRun */
	ITEM_FLAG_MOUNT_POINT  	= 0x04,	/* Is mounted or in fstab */
	ITEM_FLAG_MOUNTED  	= 0x08,	/* Is mounted */
	ITEM_FLAG_EXEC_FILE  	= 0x20,	/* File, and has an X bit set */
	ITEM_FLAG_MAY_DELETE	= 0x40, /* Delete on finishing scan */
	ITEM_FLAG_RECENT	= 0x80, /* [MC]-time is around now */
} ItemFlags;

struct _DirItem
{
	char		*leafname;
	CollateKey	*leafname_collate; /* Preprocessed for sorting */
	gboolean	may_delete;	/* Not yet found, this scan */
	int		base_type;
	int		flags;
	mode_t		mode;
	off_t		size;
	time_t		atime, ctime, mtime;
	MaskedPixmap	*image;		/* NULL => leafname only so far */
	MIME_type	*mime_type;
	uid_t		uid;
	gid_t		gid;
	int		lstat_errno;	/* 0 if details are valid */
};

void diritem_init(void);
DirItem *diritem_new(const guchar *leafname);
void diritem_restat(const guchar *path, DirItem *item, struct stat *parent);
void diritem_free(DirItem *item);

#endif /* _DIRITEM_H */
