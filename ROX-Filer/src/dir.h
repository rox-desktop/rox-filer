/*
 * $Id$
 *
 * Thomas Leonard, <tal197@users.sourceforge.net>
 */


#ifndef _DIR_H
#define _DIR_H

#include <sys/types.h>
#include <dirent.h>

typedef enum {
	DIR_START_SCAN,	/* Set 'scanning' indicator */
	DIR_END_SCAN,	/* Clear 'scanning' indicator */
	DIR_ADD,	/* Add the listed items to the display */
	DIR_REMOVE,	/* Remove listed items from display */
	DIR_UPDATE,	/* Redraw these items */
} DirAction;

typedef struct _DirUser DirUser;
typedef void (*DirCallback)(Directory *dir,
			DirAction action,
			GPtrArray *items,
			gpointer data);

typedef enum
{
	ITEM_FLAG_SYMLINK 	= 0x01,	/* Is a symlink */
	ITEM_FLAG_APPDIR  	= 0x02,	/* Contains /AppInfo */
	ITEM_FLAG_MOUNT_POINT  	= 0x04,	/* Is in mtab or fstab */
	ITEM_FLAG_MOUNTED  	= 0x08,	/* Is in /etc/mtab */
	ITEM_FLAG_TEMP_ICON  	= 0x10,	/* Free icon after use */
	ITEM_FLAG_EXEC_FILE  	= 0x20,	/* File, and has an X bit set */
	ITEM_FLAG_MAY_DELETE	= 0x40, /* Delete on finishing scan */
} ItemFlags;

#include "fscache.h"

extern GFSCache *dir_cache;

struct _DirItem
{
	char		*leafname;
	gboolean	may_delete;	/* Not yet found, this scan */
	int		base_type;
	int		flags;
	mode_t		mode;
	off_t		size;
	time_t		mtime;
	MaskedPixmap	*image;
	MIME_type	*mime_type;
	int		name_width;
	uid_t		uid;
	gid_t		gid;
	int		lstat_errno;	/* 0 if details are valid */
};

struct _DirUser
{
	DirCallback	callback;
	gpointer	data;
};

struct _Directory
{
	char	*pathname;	/* Internal use only */
	int	ref;
	GList	*users;		/* Functions to call on update */
	char	*error;		/* NULL => no error */

	gboolean	needs_update;	/* When scan is finished, rescan */
	gboolean	notify_active;	/* Notify timeout is running */
	gboolean	done_some_scanning;	/* Read any items this scan? */
	gint		idle;		/* Idle callback ID */
	DIR		*dir_handle;	/* NULL => not scanning */
	off_t		dir_start;	/* For seekdir() to beginning */

	GPtrArray 	*items;		/* What our users know about */
	GPtrArray	*new_items;	/* New items to add in */
	GPtrArray	*up_items;	/* Items to redraw */
};

void dir_init(void);
void dir_attach(Directory *dir, DirCallback callback, gpointer data);
void dir_detach(Directory *dir, DirCallback callback, gpointer data);
void dir_update(Directory *dir, gchar *pathname);
int dir_item_cmp(const void *a, const void *b);
void refresh_dirs(char *path);
void dir_stat(guchar *path, DirItem *item);
void dir_restat(guchar *path, DirItem *item);
void dir_item_clear(DirItem *item);
void dir_check_this(guchar *path);

#endif /* _DIR_H */
