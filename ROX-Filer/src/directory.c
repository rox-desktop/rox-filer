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

/* Creates and fills in the files list for a directory.
 * Returns TRUE on success, else FALSE and errno gives error.
 */
gboolean directory_scan(Directory *directory)
{
	FileInfo	*files;
	int		files_size, n_files;
	DIR		*dir;
	struct dirent   *next;
	gboolean	retval;
	
	g_return_val_if_fail(directory != NULL, FALSE);
	g_return_val_if_fail(directory->files == NULL, FALSE);

	files = g_malloc(sizeof(FileInfo) * 8);
	files_size = 8;
	n_files = 0;

	dir = opendir(directory->path);
	if (dir)
	{
		while ((next = readdir(dir)))
		{
			char	*name = next->d_name;

			/* Ignore '.' and '..' */
			if (*name == '.' && (name[1] == '\0'
					|| (name[1] == '.' && name[2] == '\0')))
				continue;

			if (n_files >= files_size)
			{
				files_size += files_size / 2;
				files = g_realloc(files,
						sizeof(FileInfo) * files_size);
				printf("[ resized to %d items ]\n", files_size);
			}
			files[n_files++].name = g_strdup(name);
		}

		/* An error here should be ignored - NFS seems to generate
		 * unnecessary errors.
		 */

		closedir(dir);

		retval = TRUE;
	}
	else
		retval = FALSE;

	directory->files = files;
	directory->number_of_files = n_files;

	return retval;
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
