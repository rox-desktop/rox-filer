/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2003, the ROX-Filer team.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */


/* 
 * xtypes.c - Extended filesystem attribute support for MIME types
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <glib.h>

#include "global.h"
#include "type.h"
#include "xtypes.h"

#define XTYPE_ATTR "user.mime_type"

#if defined(HAVE_GETXATTR)
/* Linux implementation */

#include <dlfcn.h>

static int (*dyn_setxattr)(const char *path, const char *name,
		     const void *value, size_t size, int flags) = NULL;
static ssize_t (*dyn_getxattr)(const char *path, const char *name,
			 void *value, size_t size) = NULL;

void xtype_init(void)
{
	void *libc;
	
	libc = dlopen("libc.so.6", RTLD_LAZY | RTLD_NOLOAD);
	if (!libc)
		return;	/* Give up on xattr support */

	dyn_setxattr = (void *) dlsym(libc, "setxattr");
	dyn_getxattr = (void *) dlsym(libc, "getxattr");
}

MIME_type *xtype_get(const char *path)
{
	ssize_t size;
	gchar *buf;
	MIME_type *type = NULL;

	if (!dyn_getxattr)
		return type_from_path(path);	/* Old libc */

	size = dyn_getxattr(path, XTYPE_ATTR, "", 0);
	if (size > 0)
	{
		int new_size;

		buf = g_new(gchar, size + 1);
		new_size = dyn_getxattr(path, XTYPE_ATTR, buf, size);

		if (size == new_size)
		{
			buf[size] = '\0';
			type = mime_type_lookup(buf);
		}
		g_free(buf);

	}
	if (type)
		return type;

	/* Fall back to non-extended */
	return type_from_path(path);
}

/* 0 on success */
int xtype_set(const char *path, const MIME_type *type)
{
	int res;
	gchar *ttext;

	if (!dyn_setxattr)
	{
		errno = ENOSYS;
		return 1; /* Set type failed */
	}

	ttext = g_strdup_printf("%s/%s", type->media_type, type->subtype);
	res = dyn_setxattr(path, XTYPE_ATTR, ttext, strlen(ttext), 0);
	g_free(ttext);

	return res;
}

#elif defined(HAVE_ATTROPEN)

void xtype_init(void)
{
}

/* Solaris 9 implementation */

MIME_type *xtype_get(const char *path)
{
  int fd;
  char buf[1024];
  int nb;
  MIME_type *type=NULL;

  fd=attropen(path, XTYPE_ATTR, O_RDONLY);
  
  /*printf("%s: fd=%d ", path, fd);*/
  if(fd>0) {
    nb=read(fd, buf, sizeof(buf));
    /*printf("nb=%d ", nb);*/
    if(nb>0) {
      buf[nb]=0;
      /*printf("buf=%s ", buf);*/
      type=mime_type_lookup(buf);
    }
    close(fd);
  }
  /*printf("%s -> %s\n", path, type? mime_type_comment(type): "Unknown");*/
  if(type)
    return type;
  
  /* Fall back to non-extended */
  return type_from_path(path);
}

int xtype_set(const char *path, const MIME_type *type)
{
  int fd;
  gchar *ttext;
  int nb;

  fd=attropen(path, XTYPE_ATTR, O_WRONLY|O_CREAT, 0644);
  if(fd>0) {
    ttext=g_strdup_printf("%s/%s", type->media_type, type->subtype);
    nb=write(fd, ttext, strlen(ttext));
    if(nb==strlen(ttext))
      ftruncate(fd, (off_t) nb);
    g_free(ttext);

    close(fd);

    if(nb>0)
      return 0;
  }
  
  return 1; /* Set type failed */
}

#else
/* No extended attricutes available */

void xtype_init(void)
{
}

MIME_type *xtype_get(const char *path)
{
	/* Fall back to non-extended */
	return type_from_path(path);
}

int xtype_set(const char *path, const MIME_type *type)
{
	errno = ENOSYS;
	return 1; /* Set type failed */
}

#endif

