/*
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _SAVEBOX_H
#define _SAVEBOX_H

typedef gboolean SaveBoxCallback(char *initial, char *path);

#include "pixmaps.h"
#include "filer.h"

/* Prototypes */

void savebox_init(void);
void savebox_show(FilerWindow *fw, char *title, char *path, char *leaf,
		  MaskedPixmap *image, SaveBoxCallback *cb);

#endif /* _SAVEBOX_H */
