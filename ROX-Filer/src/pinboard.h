/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _PINBOARD_H
#define _PINBOARD_H

#include <glib.h>

typedef struct _Pinboard Pinboard;
typedef struct _PinIcon PinIcon;

void pinboard_init(void);
void pinboard_activate(guchar *name);
void pinboard_pin(guchar *path, guchar *name, int x, int y);
void pinboard_unpin(PinIcon *icon);
void pinboard_unpin_selection(void);
void pinboard_wink_item(PinIcon *icon, gboolean timeout);
void pinboard_clear(void);
void pinboard_may_update(guchar *path);
void pinboard_show_help(PinIcon *icon);
void pinboard_clear_selection(void);

PinIcon *pinboard_selected_icon(void);
gboolean pinboard_is_selected(PinIcon *icon);
void pinboard_set_selected(PinIcon *icon, gboolean selected);
GList *pinboard_get_selected(void);
void pinboard_select_only(PinIcon *icon);

#endif /* _PINBOARD_H */
