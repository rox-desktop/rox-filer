/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _TYPE_H
#define _TYPE_H

typedef struct _MIME_type MIME_type;
extern MIME_type text_plain;		/* Often used as a default type */

#include "pixmaps.h"
#include "filer.h"

struct _MIME_type
{
	char		*media_type;
	char		*subtype;
	MaskedPixmap 	*image;		/* NULL => not loaded yet */
	time_t		image_time;	/* When we loaded the image */
};

/* Prototypes */
void type_init();
char *basetype_name(DirItem *item);

MIME_type *type_from_path(char *path);
gboolean type_open(char *path, MIME_type *type);
MaskedPixmap *type_to_icon(MIME_type *type);
GdkAtom type_to_atom(MIME_type *type);
char *type_ask_which_action(guchar *media_type, guchar *subtype);

#endif /* _TYPE_H */
