/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#include <glib.h>

typedef struct _Pinboard Pinboard;
typedef struct _PinIcon PinIcon;

void pinboard_activate(guchar *name);
void pinboard_pin(guchar *path, guchar *name, int x, int y);
void pinboard_unpin(PinIcon *icon);
void pinboard_wink_item(PinIcon *icon);
void pinboard_clear(void);
void pinboard_may_update(guchar *path);
