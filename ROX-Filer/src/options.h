/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _OPTIONS_H
#define _OPTIONS_H

typedef char *OptionFunc(char *value);

#include "filer.h"

/* Prototypes */

void options_init(void);
void option_register(char *key, OptionFunc *func);
void options_load(void);
void options_show(FilerWindow *filer_window);
void option_write(char *name, char *value);

#endif /* _OPTIONS_H */
