/* vi: set cindent:
 * $Id$
 *
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

/* choices.c - code for handling loading and saving of user choices */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/param.h>
#include <fcntl.h>
#include <unistd.h>

#include "choices.h"

static char *program_name = NULL;
static char path_buffer[MAXPATHLEN];

/* Static prototypes */
static char *my_strdup(char *str);
static char *make_choices_path(char *path, char *leaf,
		int create, char *shared_name);
static char *default_user_choices();
static int exists(char *path);

/* Call this before using any of the other functions in this file -
 * it reads CHOICESPATH into an array for later use.
 * The program name you pass here will be used for the directory name.
 */
void choices_init(char *prog_name)
{
	program_name = my_strdup(prog_name);
}


/* Get the pathname of a choices file to load. Eg:
 * choices_find_path_load("menus") -> "/usr/local/Choices/ProgName/menus".
 * The return values may be NULL - use built-in defaults - otherwise
 * it points to a static buffer which is valid until the next call to
 * the choices system.
 */
char *choices_find_path_load(char *leaf)
{
	return choices_find_path_load_shared(leaf, program_name);
}
	
/* Like choices_find_path_load(), but use shared_name instead of
 * prog_name (as passed to choices_init).
 */
char *choices_find_path_load_shared(char *leaf, char *shared_name)
{
	char	*choices_path, *path;
	
	if (!shared_name)
	{
		fprintf(stderr, "choices_find_path_load_shared: "
				"bad program name (or not initialised)\n");
		return NULL;
	}

	choices_path = getenv("CHOICESPATH");
	if (!choices_path)
	{
		path = make_choices_path(default_user_choices(),
				leaf, 0, shared_name);
		if (path && exists(path))
			return path;
		choices_path = "/usr/local/Choices:/usr/Choices";
	}

	if (*choices_path == ':')
		choices_path++;

	while (choices_path)
	{
		path = make_choices_path(choices_path, leaf, 0, shared_name);
		if (path && exists(path))
			return path;
		choices_path = strchr(choices_path, ':');
		if (choices_path)
			choices_path++;
	}

	return NULL;
}

/* Return a pathname to save to, or NULL if saving isn't possible.
 * Otherwise, it points to a static buffer which is valid until the next call
 * to the choices system.
 * Missing directories are created automatically.
 */
char *choices_find_path_save(char *leaf)
{
	char *choices_path;

	if (!program_name)
	{
		fprintf(stderr, "choices_find_path_save: not initialised\n");
		return NULL;
	}

	choices_path = getenv("CHOICESPATH");

	if (choices_path)
		return make_choices_path(choices_path, leaf, 1, program_name);
	else
		return make_choices_path(default_user_choices(), leaf,
				1, program_name);
}

/* Like strdup(), but also reports an error to stderr on failure */
static char *my_strdup(char *str)
{
	char	*retval;

	retval = strdup(str);

	if (!retval)
		fprintf(stderr, "choices.c: strdup() failed\n");

	return retval;
}

/* Join these two strings together, with /ProgName/ between them, and return
 * the resulting pathname as a pointer to a static buffer, or NULL on
 * failure.
 * 'path' is \0 or : terminated and empty strings result in a NULL return.
 * If create is non-zero then the extra directories will be created if they
 * don't already exist.
 */
static char *make_choices_path(char 	*path,
			       char 	*leaf,
			       int 	create,
			       char 	*shared_name)
{
	char	*term;
	size_t	path_len, prog_len;

	term = path ? strchr(path, ':') : NULL;
	if (term == path || *path == '\0')
		return NULL;
	 
	if (term)
		path_len = term - path;
	else
		path_len = strlen(path);

	prog_len = strlen(shared_name);

	/* path/progname/leaf0 */
	if (path_len + prog_len + strlen(leaf) + 3 >= MAXPATHLEN)
		return NULL;		/* Path is too long */

	memcpy(path_buffer, path, path_len);
	if (create)
	{
		path_buffer[path_len] = '\0';
		mkdir(path_buffer, S_IRWXU | S_IRWXG | S_IRWXO);
	}

	path_buffer[path_len] = '/';
	strcpy(path_buffer + path_len + 1, shared_name);
	if (create)
		mkdir(path_buffer, S_IRWXU | S_IRWXG | S_IRWXO);

	path_buffer[path_len + 1 + prog_len] = '/';
	strcpy(path_buffer + path_len + prog_len + 2, leaf);

	return path_buffer;
}

/* Return ${HOME}/Choices (expanded), or NULL */
static char *default_user_choices()
{
	static char 	*retval = NULL;
	char		*home;

	if (retval)
		return retval;

	home = getenv("HOME");

	if (!home)
		retval = NULL;
	else if (strlen(home) + strlen("/Choices") >= MAXPATHLEN - 1)
		retval = NULL;
	else
	{
		sprintf(path_buffer, "%s/Choices", home);
		retval = my_strdup(path_buffer);
	}

	return retval;
}

/* Returns 1 if the object exists, 0 if it doesn't */
static int exists(char *path)
{
	struct stat info;

	return stat(path, &info) == 0;
}
