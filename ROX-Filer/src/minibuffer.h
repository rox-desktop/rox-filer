/*
 * $Id*
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _MINIBUFFER_H
#define _MINIBUFFER_H

#include <gtk/gtk.h>
#include "filer.h"

GtkWidget *create_minibuffer(FilerWindow *filer_window);
void minibuffer_show(FilerWindow *filer_window);

#endif /* _MINIBUFFER_H */
