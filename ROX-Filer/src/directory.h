/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _DIRECTORY_H
#define _DIRECTORY_H

typedef struct _Directory Directory;
typedef struct _FileInfo FileInfo;

struct _Directory
{
	char		*path;		/* A realpath */
	FileInfo	*files;
	int		number_of_files;
};

struct _FileInfo
{
	char	*name;
};

gboolean directory_scan(Directory *directory);
Directory *directory_new(char *path);
void directory_destroy(Directory *dir);

#endif /* _DIRECTORY_H */
