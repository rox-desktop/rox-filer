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
	DIR_NAMES,	/* Got a list of names (used for resizing) */
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

#include "fscache.h"
#include "diritem.h"

extern GFSCache *dir_cache;

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

	gboolean	do_thumbs;	/* Create thumbs while scanning */
	gboolean	notify_active;	/* Notify timeout is running */
	gint		idle_callback;	/* Idle callback ID */

	GHashTable 	*known_items;	/* What our users know about */
	GPtrArray	*new_items;	/* New items to add in */
	GPtrArray	*up_items;	/* Items to redraw */

	GList		*recheck_list;	/* Items to check on callback */

	gboolean	have_scanned;	/* TRUE after first complete scan */
	gboolean	scanning;	/* TRUE if we sent DIR_START_SCAN */

	/* Old stuff.. */

	gboolean	needs_update;	/* When scan is finished, rescan */
	gboolean	done_some_scanning;	/* Read any items this scan? */
	DIR		*dir_handle;	/* NULL => not scanning */
	off_t		dir_start;	/* For seekdir() to beginning */
};

void dir_init(void);
void dir_attach(Directory *dir, DirCallback callback, gpointer data);
void dir_detach(Directory *dir, DirCallback callback, gpointer data);
void dir_update(Directory *dir, gchar *pathname);
void dir_rescan_with_thumbs(Directory *dir, gchar *pathname);
int dir_item_cmp(const void *a, const void *b);
void refresh_dirs(char *path);
void dir_stat(guchar *path, DirItem *item, gboolean make_thumb);
void dir_restat(guchar *path, DirItem *item, gboolean make_thumb);
void dir_item_clear(DirItem *item);
void dir_check_this(guchar *path);
void dir_rescan(Directory *dir, guchar *pathname);
void dir_merge_new(Directory *dir);

#endif /* _DIR_H */
