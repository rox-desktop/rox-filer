/*
 * $Id$
 *
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _CHOICES_H
#define _CHOICES_H

typedef struct _ChoicesList ChoicesList;	/* Singly linked list */

struct _ChoicesList
{
	char		*path;
	ChoicesList	*next;
};

void choices_init(char *prog_name);
ChoicesList *choices_find_load_all(char *leaf, char *dir_name);
char *choices_find_path_load_shared(char *leaf, char *shared_name);
char *choices_find_path_load(char *leaf);
char *choices_find_path_save(char *leaf);

#endif /* _CHOICES_H */
