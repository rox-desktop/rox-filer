/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#include <gtk/gtk.h>

void i18n_init(void);
GtkItemFactoryEntry *translate_entries(GtkItemFactoryEntry *entries, gint n);
void free_translated_entries(GtkItemFactoryEntry *entries, gint n);
