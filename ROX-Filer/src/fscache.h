/*
 * $Id$
 *
 * Thomas Leonard, <tal197@ecs.soton.ac.uk>
 */


#ifndef _FSCACHE_H
#define _FSCACHE_H

#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>

#include <glib.h>

typedef struct _GFSCache GFSCache;
typedef struct _GFSCacheKey GFSCacheKey;
typedef struct _GFSCacheData GFSCacheData;
typedef void (*GFSFunc)(GFSCacheData *stats, gpointer user_data);
typedef gpointer (*GFSLoadFunc)(char *pathname, gpointer user_data);

struct _GFSCache
{
	
	GHashTable	*inode_to_stats;
	GFSLoadFunc	load;
	GFSFunc		ref;
	GFSFunc		unref;
	gpointer	user_data;
};

struct _GFSCacheKey
{
	dev_t		device;
	ino_t		inode;
};

struct _GFSCacheData
{
	gpointer	data;		/* The object from the file */

	/* Details of the file last time we checked it */
	time_t		m_time;
	off_t		length;
	mode_t		mode;
};

GFSCache *g_fscache_new(GFSLoadFunc load,
			GFSFunc ref,
			GFSFunc unref,
			gpointer user_data);
void g_fscache_destroy(GFSCache *cache);
gpointer g_fscache_lookup(GFSCache *cache, char *pathname);
void g_fscache_data_ref(GFSCache *cache, gpointer data);
void g_fscache_data_unref(GFSCache *cache, gpointer data);

#endif /* _FSCACHE_H */
