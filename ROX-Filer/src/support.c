/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 2000, Thomas Leonard, <tal197@ecs.soton.ac.uk>.
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

/* support.c - (non-GUI) useful routines */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <sys/param.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>

#include <glib.h>

#include "main.h"
#include "support.h"
#include "my_vfs.h"

static GHashTable *uid_hash = NULL;	/* UID -> User name */
static GHashTable *gid_hash = NULL;	/* GID -> Group name */

/* Static prototypes */


/* Like g_strdup, but does realpath() too (if possible) */
char *pathdup(char *path)
{
	char real[MAXPATHLEN];

	g_return_val_if_fail(path != NULL, NULL);

	if (realpath(path, real))
		return g_strdup(real);

	return g_strdup(path);
}

/* Join the path to the leaf (adding a / between them) and
 * return a pointer to a buffer with the result. Buffer is valid until
 * the next call to make_path.
 */
GString *make_path(char *dir, char *leaf)
{
	static GString *buffer = NULL;

	if (!buffer)
		buffer = g_string_new(NULL);

	g_return_val_if_fail(dir != NULL, buffer);
	g_return_val_if_fail(leaf != NULL, buffer);

	g_string_sprintf(buffer, "%s%s%s",
			dir,
			dir[0] == '/' && dir[1] == '\0' ? "" : "/",
			leaf);

	return buffer;
}

/* Return our complete host name */
char *our_host_name()
{
	static char *name = NULL;

	if (!name)
	{
		char buffer[4096];

		g_return_val_if_fail(gethostname(buffer, 4096) == 0,
					"localhost");

		buffer[4095] = '\0';
		name = g_strdup(buffer);
	}

	return name;
}

/* fork() and run a new program.
 * Returns the new PID, or 0 on failure.
 */
pid_t spawn(char **argv)
{
	return spawn_full(argv, NULL);
}

/* As spawn(), but cd to dir first (if dir is non-NULL) */
pid_t spawn_full(char **argv, char *dir)
{
	int	child;
	
	child = fork();

	if (child == -1)
		return 0;	/* Failure */
	else if (child == 0)
	{
		/* We are the child process */
		if (dir)
			if (chdir(dir))
				fprintf(stderr, "chdir() failed: %s\n",
						g_strerror(errno));
		dup2(to_error_log, STDERR_FILENO);
		close_on_exec(STDERR_FILENO, FALSE);
		execvp(argv[0], argv);
		fprintf(stderr, "execvp(%s, ...) failed: %s\n",
				argv[0],
				g_strerror(errno));
		_exit(0);
	}

	/* We are the parent */
	return child;
}

void debug_free_string(void *data)
{
	g_print("Freeing string '%s'\n", (char *) data);
	g_free(data);
}

char *user_name(uid_t uid)
{
	char	*retval;
	
	if (!uid_hash)
		uid_hash = g_hash_table_new(NULL, NULL);

	retval = g_hash_table_lookup(uid_hash, (gpointer) uid);

	if (!retval)
	{
		struct passwd *passwd;

		passwd = getpwuid(uid);
		retval = passwd ? g_strdup(passwd->pw_name)
			       : g_strdup_printf("[%d]", (int) uid);
		g_hash_table_insert(uid_hash, (gpointer) uid, retval);
	}

	return retval;
}

char *group_name(gid_t gid)
{
	char	*retval;
	
	if (!gid_hash)
		gid_hash = g_hash_table_new(NULL, NULL);

	retval = g_hash_table_lookup(gid_hash, (gpointer) gid);

	if (!retval)
	{
		struct group *group;

		group = getgrgid(gid);
		retval = group ? g_strdup(group->gr_name)
			       : g_strdup_printf("[%d]", (int) gid);
		g_hash_table_insert(gid_hash, (gpointer) gid, retval);
	}

	return retval;
}

/* Return a string in the form '23Mb' in a static buffer valid until
 * the next call.
 */
char *format_size(unsigned long size)
{
	static	char *buffer = NULL;
	char	*units;
	
	if (size >= PRETTY_SIZE_LIMIT)
	{
		size += 1023;
		size >>= 10;
		if (size >= PRETTY_SIZE_LIMIT)
		{
			size += 1023;
			size >>= 10;
			if (size >= PRETTY_SIZE_LIMIT)
			{
				size += 1023;
				size >>= 10;
				units = "Gb";
			}
			else
				units = "Mb";
		}
		else
			units = "K";
	}
	else
		units = "bytes";

	if (buffer)
		g_free(buffer);
	buffer = g_strdup_printf("%ld %s", size, units);

	return buffer;
}

/* Return a string in the form '23Mb' in a static buffer valid until
 * the next call. Aligned to the right.
 */
char *format_size_aligned(unsigned long size)
{
	static	char *buffer = NULL;
	char	units;
	
	if (size >= PRETTY_SIZE_LIMIT)
	{
		size += 1023;
		size >>= 10;
		if (size >= PRETTY_SIZE_LIMIT)
		{
			size += 1023;
			size >>= 10;
			if (size >= PRETTY_SIZE_LIMIT)
			{
				size += 1023;
				size >>= 10;
				units = 'G';
			}
			else
				units = 'M';
		}
		else
			units = 'K';
	}
	else
		units = ' ';

	if (buffer)
		g_free(buffer);
	buffer = g_strdup_printf("%4ld%c", size, units);

	return buffer;
}

/* Fork and exec argv. Wait and return the child's exit status.
 * -1 if spawn fails.
 */
int fork_exec_wait(char **argv)
{
	pid_t	child;
	int	status = -1;

	child = spawn_full(argv, NULL);

	while (child)
	{
		if (waitpid(child, &status, 0) == -1)
		{
			if (errno != EINTR)
				return -1;
		}
		else
			break;
	};

	return status;
}

/* If a file has this UID and GID, which permissions apply to us?
 * 0 = User, 1 = Group, 2 = World
 */
gint applicable(uid_t uid, gid_t gid)
{
	int	i;

	if (uid == euid)
		return 0;

	if (gid == egid)
		return 1;

	for (i = 0; i < ngroups; i++)
	{
		if (supplemental_groups[i] == gid)
			return 1;
	}

	return 2;
}

/* Converts a file's mode to a string. Result is a pointer
 * to a static buffer, valid until the next call.
 */
char *pretty_permissions(mode_t m)
{
	static char buffer[] = "rwx,rwx,rwx/UGT";

	buffer[0]  = m & S_IRUSR ? 'r' : '-';
	buffer[1]  = m & S_IWUSR ? 'w' : '-';
	buffer[2]  = m & S_IXUSR ? 'x' : '-';

	buffer[4]  = m & S_IRGRP ? 'r' : '-';
	buffer[5]  = m & S_IWGRP ? 'w' : '-';
	buffer[6]  = m & S_IXGRP ? 'x' : '-';

	buffer[8]  = m & S_IROTH ? 'r' : '-';
	buffer[9]  = m & S_IWOTH ? 'w' : '-';
	buffer[10] = m & S_IXOTH ? 'x' : '-';

	buffer[12] = m & S_ISUID ? 'U' : '-';
	buffer[13] = m & S_ISGID ? 'G' : '-';
#ifdef S_ISVTX
        buffer[14] = m & S_ISVTX ? 'T' : '-';
        buffer[15] = 0;
#else
        buffer[14] = 0;
#endif

	return buffer;
}

/* Convert a URI to a local pathname (or NULL if it isn't local).
 * The returned pointer points inside the input string.
 * Possible formats:
 *	/path
 *	///path
 *	//host/path
 *	file://host/path
 */
char *get_local_path(char *uri)
{
	char	*host;

	host = our_host_name();

	if (*uri == '/')
	{
		char    *path;

		if (uri[1] != '/')
			return uri;	/* Just a local path - no host part */

		path = strchr(uri + 2, '/');
		if (!path)
			return NULL;	    /* //something */

		if (path - uri == 2)
			return path;	/* ///path */
		if (strlen(host) == path - uri - 2 &&
			strncmp(uri + 2, host, path - uri - 2) == 0)
			return path;	/* //myhost/path */

		return NULL;	    /* From a different host */
	}
	else
	{
		if (strncasecmp(uri, "file:", 5))
			return NULL;	    /* Don't know this format */

		uri += 5;

		if (*uri == '/')
			return get_local_path(uri);

		return NULL;
	}
}

/* Set the close-on-exec flag for this FD.
 * TRUE means that an exec()'d process will not get the FD.
 */
void close_on_exec(int fd, gboolean close)
{
	if (fcntl(fd, F_SETFD, close))
		g_warning("fcntl() failed: %s\n", g_strerror(errno));
}

void set_blocking(int fd, gboolean blocking)
{
	if (fcntl(fd, F_SETFL, blocking ? 0 : O_NONBLOCK))
		g_warning("fcntl() failed: %s\n", g_strerror(errno));
}

/* Format this time nicely. The result is a pointer to a static buffer,
 * valid until the next call.
 */
char *pretty_time(time_t *time)
{
        static char time_buf[32];

        if (strftime(time_buf, sizeof(time_buf),
			TIME_FORMAT, localtime(time)) == 0)
		time_buf[0]= 0;

	return time_buf;
}

#ifndef O_NOFOLLOW
#  define O_NOFOLLOW 0x0
#endif

#ifdef HAVE_LIBVFS
/* Copy data from 'read_fd' FD to 'write_fd' FD until EOF or error.
 * Returns 0 on success, -1 on error (and sets errno).
 */
static int copy_fd(int read_fd, int write_fd)
{
	char	buffer[4096];
	int	got;
	
	while ((got = mc_read(read_fd, buffer, sizeof(buffer))) > 0)
	{
		int sent = 0;
		
		while (sent < got)
		{
			int	c;
			
			c = mc_write(write_fd, buffer + sent, got - sent);
			if (c < 0)
				return -1;
			sent += c;
		}
	}

	return got;
}
#endif

/* 'from' and 'to' are complete pathnames of files (not dirs or symlinks).
 * This spawns 'cp' to do the copy if lstat() succeeds, otherwise we
 * do the copy manually using vfs.
 *
 * Returns an error string, or NULL on success.
 */
guchar *copy_file(guchar *from, guchar *to)
{
	char	*argv[] = {"cp", "-pRf", NULL, NULL, NULL};

#ifdef HAVE_LIBVFS
	struct	stat	info;

	if (lstat(from, &info))
	{
		int	read_fd = -1;
		int	write_fd = -1;
		int	error = 0;

		if (mc_lstat(from, &info))
			return g_strerror(errno);

		/* Regular lstat() can't find it, but mc_lstat() can,
		 * so try reading it with VFS.
		 */

		if (!S_ISREG(info.st_mode))
			goto err;

		read_fd = mc_open(from, O_RDONLY);
		if (read_fd == -1)
			goto err;
		write_fd = mc_open(to,
				O_NOFOLLOW | O_WRONLY | O_CREAT | O_TRUNC,
				info.st_mode & 0777);
		if (write_fd == -1)
			goto err;

		if (copy_fd(read_fd, write_fd))
			goto err;

		/* (yes, the single | is right) */
		if (mc_close(read_fd) | mc_close(write_fd))
			return g_strerror(errno);
		return NULL;
err:
		error = errno;
		if (read_fd != -1)
			mc_close(read_fd);
		if (write_fd != -1)
			mc_close(write_fd);
		return error ? g_strerror(error) : "Copy error";
	}
#endif

	argv[2] = from;
	argv[3] = to;

	if (fork_exec_wait(argv))
		return "Copy failed";
	return NULL;
}

/* 'word' has all special characters escaped so that it may be inserted
 * into a shell command.
 * Eg: 'My Dir?' becomes 'My\ Dir\?'. g_free() the result.
 */
guchar *shell_escape(guchar *word)
{
	GString	*tmp;
	guchar	*retval;

	tmp = g_string_new(NULL);

	while (*word)
	{
		if (strchr(" ?*['\"$~\\|();!`&", *word))
			g_string_append_c(tmp, '\\');
		g_string_append_c(tmp, *word);
		word++;
	}

	retval = tmp->str;
	g_string_free(tmp, FALSE);
	return retval;
}
