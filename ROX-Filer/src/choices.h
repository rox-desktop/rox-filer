/* vi: set cindent:
 * $Id$
 *
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _CHOICES_H
#define _CHOICES_H

void choices_init(char *prog_name);
char *choices_find_path_load(char *leaf);
char *choices_find_path_save(char *leaf);

#endif /* _CHOICES_H */
