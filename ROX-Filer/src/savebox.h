/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _SAVEBOX_H
#define _SAVEBOX_H

#include "filer.h"

/* Prototypes */

void savebox_init(void);
void savebox_show(FilerWindow *filer_window,
		char *title, char *path, char *leaf);

#endif /* _SAVEBOX_H */
