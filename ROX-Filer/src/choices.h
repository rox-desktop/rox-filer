/*
 * $Id$
 *
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _CHOICES_H
#define _CHOICES_H

#include <glib.h>

void 		choices_init	       (void);
GPtrArray	*choices_list_dirs     (char *dir);
void		choices_free_list      (GPtrArray *list);
guchar 		*choices_find_path_load(char *leaf, char *dir);
guchar	   	*choices_find_path_save(char *leaf, char *dir, gboolean create);

#endif /* _CHOICES_H */
