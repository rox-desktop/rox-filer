/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _TYPE_H
#define _TYPE_H

typedef struct _MIME_type MIME_type;

struct _MIME_type
{
	char	*name;
};

/* Prototypes */
void type_init();
MIME_type *type_from_path(char *path);
gboolean type_open(char *path, MIME_type *type);

#endif /* _TYPE_H */
