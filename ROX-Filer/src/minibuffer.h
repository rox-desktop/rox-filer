/*
 * $Id*
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@users.sourceforge.net>.
 */

#ifndef _MINIBUFFER_H
#define _MINIBUFFER_H

typedef enum {
	MINI_NONE,
	MINI_PATH,
	MINI_SHELL,
	MINI_SELECT_IF,
} MiniType;

#include <gtk/gtk.h>

void create_minibuffer(FilerWindow *filer_window);
void minibuffer_show(FilerWindow *filer_window, MiniType mini_type);
void minibuffer_hide(FilerWindow *filer_window);
void minibuffer_add(FilerWindow *filer_window, guchar *leafname);

#endif /* _MINIBUFFER_H */
