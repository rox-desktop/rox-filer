/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _TYPE_H
#define _TYPE_H

#include <gtk/gtk.h>

extern MIME_type *text_plain;		/* Often used as a default type */
extern MIME_type *inode_directory;
extern MIME_type *inode_pipe;
extern MIME_type *inode_socket;
extern MIME_type *inode_block_dev;
extern MIME_type *inode_char_dev;
extern MIME_type *application_executable;
extern MIME_type *inode_unknown;
extern MIME_type *inode_door;

enum
{
	/* Base types - this also determines the sort order */
	TYPE_ERROR,
	TYPE_UNKNOWN,		/* Not scanned yet */
	TYPE_DIRECTORY,
	TYPE_PIPE,
	TYPE_SOCKET,
	TYPE_FILE,
	TYPE_CHAR_DEVICE,
	TYPE_BLOCK_DEVICE,
	TYPE_DOOR,

	/* These are purely for colour allocation */
	TYPE_EXEC,
	TYPE_APPDIR,
};

struct _MIME_type
{
	char		*media_type;
	char		*subtype;
	MaskedPixmap 	*image;		/* NULL => not loaded yet */
	time_t		image_time;	/* When we loaded the image */
};

/* Prototypes */
void type_init(void);
const char *basetype_name(DirItem *item);
MIME_type *type_get_type(const guchar *path);

MIME_type *type_from_path(const char *path);
gboolean type_open(const char *path, MIME_type *type);
MaskedPixmap *type_to_icon(MIME_type *type);
GdkAtom type_to_atom(MIME_type *type);
MIME_type *mime_type_from_base_type(int base_type);
int mode_to_base_type(int st_mode);
void type_set_handler_dialog(MIME_type *type);
gboolean can_set_run_action(DirItem *item);
gchar *describe_current_command(MIME_type *type);
GdkColor *type_get_colour(DirItem *item, GdkColor *normal);

#endif /* _TYPE_H */
