/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

/* directory.c - code for handling directories (non-GUI) */

#include <stdlib.h>
#include <stdio.h>

#include <glib.h>
#include <sys/types.h>
#include <dirent.h>

#include "support.h"
#include "directory.h"

/* Calls the callback function for each object in the directory.
 * '.' and '..' are ignored.
 * Returns FALSE on error (errno is set).
 */
gboolean directory_scan(Directory *directory,
			void (*callback)(char *name, gpointer user_data),
			gpointer user_data)
{
	DIR		*dir;
	struct dirent   *next;
	
	g_return_val_if_fail(directory != NULL, FALSE);

	dir = opendir(directory->path);
	if (!dir)
		return FALSE;

	while ((next = readdir(dir)))
	{
		char	*name = next->d_name;

		/* Ignore '.' and '..' */
		if (*name == '.' && (name[1] == '\0'
					|| (name[1] == '.' && name[2] == '\0')))
			continue;

		callback(name, user_data);
	}

	/* An error here should be ignored - NFS seems to generate
	 * unnecessary errors.
	 */
	closedir(dir);

	return TRUE;
}

/* Public methods */

/* Create a new directory for the given path.
 * Path will be pathdup'd. The directory is NOT scanned yet.
 */
Directory *directory_new(char *path)
{
	Directory	*dir;

	g_return_val_if_fail(path != NULL, NULL);

	dir = g_malloc(sizeof(Directory));

	dir->path = pathdup(path);
	dir->files = NULL;		/* Mark as unscanned */
	dir->number_of_files = 0;

	return dir;
}

void directory_destroy(Directory *dir)
{
	g_return_if_fail(dir != NULL);

	printf("[ destroy '%s' ]\n", dir->path);
	g_free(dir->path);
	g_free(dir);
}
