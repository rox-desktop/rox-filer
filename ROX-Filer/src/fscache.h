/*
 * $Id$
 *
 * Thomas Leonard, <tal197@users.sourceforge.net>
 */


#ifndef _FSCACHE_H
#define _FSCACHE_H

#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <glib.h>

typedef struct _GFSCache GFSCache;
typedef struct _GFSCacheKey GFSCacheKey;
typedef struct _GFSCacheData GFSCacheData;
typedef gpointer (*GFSLoadFunc)(char *pathname, gpointer user_data);
typedef void (*GFSRefFunc)(gpointer object, gpointer user_data);
typedef int (*GFSGetRefFunc)(gpointer object, gpointer user_data);
typedef void (*GFSUpdateFunc)(gpointer object,
			      char *pathname,
			      gpointer user_data);

struct _GFSCache
{
	
	GHashTable	*inode_to_stats;
	GFSLoadFunc	load;
	GFSRefFunc	ref;
	GFSRefFunc	unref;
	GFSGetRefFunc	getref;
	GFSUpdateFunc	update;
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
	time_t		last_lookup;

	/* Details of the file last time we checked it */
	time_t		m_time;
	off_t		length;
	mode_t		mode;
};

typedef enum {
	FSCACHE_LOOKUP_CREATE,	/* Load if missing. Update as needed. */
	FSCACHE_LOOKUP_ONLY_NEW,/* Return NULL if not present AND uptodate */
	FSCACHE_LOOKUP_PEEK,	/* Lookup; don't load or update */
} FSCacheLookup;

GFSCache *g_fscache_new(GFSLoadFunc load,
			GFSRefFunc ref,
			GFSRefFunc unref,
			GFSGetRefFunc getref,
			GFSUpdateFunc update,
			gpointer user_data);
void g_fscache_destroy(GFSCache *cache);
gpointer g_fscache_lookup(GFSCache *cache, char *pathname);
gpointer g_fscache_lookup_full(GFSCache *cache, char *pathname,
				FSCacheLookup lookup_type);
void g_fscache_may_update(GFSCache *cache, char *pathname);
void g_fscache_update(GFSCache *cache, char *pathname);
void g_fscache_purge(GFSCache *cache, gint age);

void g_fscache_data_ref(GFSCache *cache, gpointer data);
void g_fscache_data_unref(GFSCache *cache, gpointer data);

#endif /* _FSCACHE_H */
