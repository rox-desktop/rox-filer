/*
 * $Id$
 *
 * Diego Zamboni, Feb 7, 2001
 */

#ifndef _USERICONS_H_
#define _USERICONS_H_

/* Public interface */
void read_globicons();
void check_globicon(const guchar *path, DirItem *item);
void icon_set_handler_dialog(DirItem *item, const guchar *path);

gboolean set_icon_path(const guchar *path, const guchar *icon);
#endif
