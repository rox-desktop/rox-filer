/*
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _CHOICES_H
#define _CHOICES_H

void 		choices_init	       (void);
GPtrArray	*choices_list_dirs     (char *dir);
void		choices_free_list      (GPtrArray *list);
guchar 		*choices_find_path_load(const char *leaf, const char *dir);
guchar	   	*choices_find_path_save(const char *leaf, const char *dir,
					gboolean create);

#endif /* _CHOICES_H */
