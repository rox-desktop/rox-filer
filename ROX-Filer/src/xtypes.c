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
static ssize_t (*dyn_listxattr)(const char *path, char *list,
			 size_t size) = NULL;

void xtype_init(void)
{
	void *libc;
	
	libc = dlopen("libc.so.6", RTLD_LAZY | RTLD_NOLOAD);
	if (!libc)
		return;	/* Give up on xattr support */

	dyn_setxattr = (void *) dlsym(libc, "setxattr");
	dyn_getxattr = (void *) dlsym(libc, "getxattr");
	dyn_listxattr = (void *) dlsym(libc, "listxattr");
}

int xtype_supported(const char *path)
{
	char buf[1];
	ssize_t nent;
	
	if (!dyn_getxattr)
		return FALSE;

	if(path) {
		errno=0;
		nent=dyn_getxattr(path, XTYPE_ATTR, buf, sizeof(buf));

		if(nent<0 && errno==ENOTSUP)
			return FALSE;
	}

	return TRUE;
}

int xtype_have_attr(const char *path)
{
	char buf[1];
	ssize_t nent;
	
	if (!dyn_listxattr)
		return FALSE;

	errno=0;
	nent=dyn_listxattr(path, buf, sizeof(buf));

	if(nent<0 && errno==ERANGE)
		return TRUE;
	
	return (nent>0);
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

/* Solaris 9 implementation */

void xtype_init(void)
{
}

int xtype_supported(const char *path)
{
#ifdef _PC_XATTR_EXISTS
	if(!path)
		return TRUE;
	
	errno=0;
	if(pathconf(path, _PC_XATTR_EXISTS)==-1 && errno)
		return FALSE;
	return TRUE;
#else
	return FALSE;
#endif
}

int xtype_have_attr(const char *path)
{
#ifdef _PC_XATTR_EXISTS
	return pathconf(path, _PC_XATTR_EXISTS);
#else
	return FALSE;
#endif
}

MIME_type *xtype_get(const char *path)
{
	int fd;
	char buf[1024];
	int nb;
	MIME_type *type=NULL;

#ifdef _PC_XATTR_EXISTS
	if(!pathconf(path, _PC_XATTR_EXISTS))
		return type_from_path(path);
#endif

	fd=attropen(path, XTYPE_ATTR, O_RDONLY);
  
	if(fd>=0) {
		nb=read(fd, buf, sizeof(buf));
		if(nb>0) {
			buf[nb]=0;
			type=mime_type_lookup(buf);
		}
		close(fd);
	}

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
		ttext=g_strdup_printf("%s/%s",
				      type->media_type, type->subtype);
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
/* No extended attributes available */

void xtype_init(void)
{
}

int xtype_supported(const char *path)
{
	return FALSE;
}

int xtype_have_attr(const char *path)
{
	return FALSE;
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

