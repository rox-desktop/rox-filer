/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

/* support.c - (non-GUI) useful routines */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/param.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>

#include <glib.h>

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
int spawn(char **argv)
{
	return spawn_full(argv, NULL, 0);
}

/* As spawn(), but cd to dir first (if dir is non-NULL) */
int spawn_full(char **argv, char *dir, SpawnFlags flags)
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
	static char	buffer[40];
	struct passwd   *passwd;

	passwd = getpwuid(uid);
	if (passwd)
		return passwd->pw_name;
	snprintf(buffer, sizeof(buffer), "[%d]", uid);
	return buffer;
}

char *group_name(gid_t gid)
{
	static char	buffer[40];
	struct group 	*group;
	
	group = getgrgid(gid);
	if (group)
		return group->gr_name;
	snprintf(buffer, sizeof(buffer), "[%d]", gid);
	return buffer;
}
