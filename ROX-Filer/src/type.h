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
extern MIME_type special_directory;
extern MIME_type special_pipe;
extern MIME_type special_socket;
extern MIME_type special_block_dev;
extern MIME_type special_char_dev;
extern MIME_type special_unknown;

#include "pixmaps.h"
#include "filer.h"

enum
{
	/* Base types - this also determines the sort order */
	TYPE_ERROR,
	TYPE_UNKNOWN,
	TYPE_DIRECTORY,
	TYPE_PIPE,
	TYPE_SOCKET,
	TYPE_FILE,
	TYPE_CHAR_DEVICE,
	TYPE_BLOCK_DEVICE,
};

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
MIME_type *mime_type_from_base_type(int base_type);

#endif /* _TYPE_H */
