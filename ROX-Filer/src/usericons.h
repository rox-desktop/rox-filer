/*
 * $Id$
 *
 * Diego Zamboni, Feb 7, 2001
 */

#ifndef _USERICONS_H_
#define _USERICONS_H_

/* Public interface */
void read_globicons();
void check_globicon(guchar *path, DirItem *item);
void icon_set_handler_dialog(DirItem *item, guchar *path);

gboolean set_icon_path(guchar *path, guchar *icon);
#endif
