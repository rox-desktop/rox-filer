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
void pinboard_unpin(Icon *icon);
void pinboard_unpin_selection(void);
void pinboard_wink_item(Icon *icon, gboolean timeout);
void pinboard_clear(void);
void pinboard_clear_selection(void);

gboolean pinboard_has(guchar *path);

void pinboard_icon_may_update(Icon *icon);
Icon *pinboard_selected_icon(void);
void pinboard_set_selected(Icon *icon, gboolean selected);
GList *pinboard_get_selected(void);
void pinboard_select_only(Icon *icon);

#endif /* _PINBOARD_H */
