/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * Copyright (C) 1999, Thomas Leonard, <tal197@ecs.soton.ac.uk>.
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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/param.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>

#include <glib.h>

#include "main.h"
#include "support.h"

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
		fcntl(STDERR_FILENO, F_SETFD, 0);
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
	struct passwd   *passwd;
	GString	*tmp;
	char	*retval;

	passwd = getpwuid(uid);
	if (passwd)
		return passwd->pw_name;
	tmp = g_string_new(NULL);
	g_string_sprintf(tmp, "[%d]", (int) uid);
	retval = tmp->str;
	g_string_free(tmp, FALSE);
	return retval;
}

char *group_name(gid_t gid)
{
	struct group 	*group;
	GString	*tmp;
	char	*retval;
	
	group = getgrgid(gid);
	if (group)
		return group->gr_name;
	tmp = g_string_new(NULL);
	g_string_sprintf(tmp, "[%d]", (int) gid);
	retval = tmp->str;
	g_string_free(tmp, FALSE);
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

/* Converts a file's mode to a string. Result is a pointer
 * to a static buffer, valid until the next call.
 */
char *pretty_permissions(mode_t m)
{
	static char buffer[] = "rwx:rwx:rwx:UGT";
	
	sprintf(buffer, "%c%c%c:%c%c%c:%c%c%c:%c%c"
#ifdef S_ISVTX
			"%c"
#endif
			,

			m & S_IRUSR ? 'r' : '-',
			m & S_IWUSR ? 'w' : '-',
			m & S_IXUSR ? 'x' : '-',

			m & S_IRGRP ? 'r' : '-',
			m & S_IWGRP ? 'w' : '-',
			m & S_IXGRP ? 'x' : '-',

			m & S_IROTH ? 'r' : '-',
			m & S_IWOTH ? 'w' : '-',
			m & S_IXOTH ? 'x' : '-',

			m & S_ISUID ? 'U' : '-',
			m & S_ISGID ? 'G' : '-'
#ifdef S_ISVTX
			, m & S_ISVTX ? 'T' : '-'
#endif
			);
	return buffer;
}

