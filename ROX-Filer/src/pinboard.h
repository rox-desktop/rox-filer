/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _PINBOARD_H
#define _PINBOARD_H

void pinboard_init(void);
void pinboard_activate(guchar *name);
void pinboard_pin(guchar *path, guchar *name, int x, int y);
void pinboard_unpin(PinIcon *icon);
void pinboard_unpin_selection(void);
void pinboard_wink_item(PinIcon *icon, gboolean timeout);
void pinboard_clear(void);
void pinboard_may_update(guchar *path);
void pinboard_clear_selection(void);

gboolean pinboard_has(guchar *path);

PinIcon *pinboard_selected_icon(void);
void pinboard_set_selected(PinIcon *icon, gboolean selected);
GList *pinboard_get_selected(void);
void pinboard_select_only(PinIcon *icon);

#endif /* _PINBOARD_H */
