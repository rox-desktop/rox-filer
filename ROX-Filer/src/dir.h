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

extern GFSCache *dir_cache;

struct _DirUser
{
	DirCallback	callback;
	gpointer	data;
};

typedef struct _DirectoryClass DirectoryClass;

struct _DirectoryClass {
	GObjectClass parent;
};

struct _Directory
{
	GObject object;

	char	*pathname;	/* Internal use only */
	GList	*users;		/* Functions to call on update */
	char	*error;		/* NULL => no error */

	gboolean	notify_active;	/* Notify timeout is running */
	gint		idle_callback;	/* Idle callback ID */

	GHashTable 	*known_items;	/* What our users know about */
	GPtrArray	*new_items;	/* New items to add in */
	GPtrArray	*up_items;	/* Items to redraw */
	GPtrArray	*gone_items;	/* Items removed */

	GList		*recheck_list;	/* Items to check on callback */

	gboolean	have_scanned;	/* TRUE after first complete scan */
	gboolean	scanning;	/* TRUE if we sent DIR_START_SCAN */

	/* Indicates that the directory needs to be rescanned.
	 * This is cleared when scanning starts, and set when the fscache
	 * detects that the directory needs to be rescanned and is already
	 * scanning.
	 *
	 * If scanning finishes when this is set, or if someone attaches
	 * and scanning is not in progress, a rescan is triggered.
	 */
	gboolean	needs_update;
};

void dir_init(void);
void dir_attach(Directory *dir, DirCallback callback, gpointer data);
void dir_detach(Directory *dir, DirCallback callback, gpointer data);
void dir_update(Directory *dir, gchar *pathname);
int dir_item_cmp(const void *a, const void *b);
void refresh_dirs(const char *path);
void dir_check_this(const guchar *path);
DirItem *dir_update_item(Directory *dir, const gchar *leafname);
void dir_rescan(Directory *dir, const guchar *pathname);
void dir_merge_new(Directory *dir);
void dir_force_update_path(const gchar *path);

#endif /* _DIR_H */
