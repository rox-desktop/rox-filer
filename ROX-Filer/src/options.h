/* vi: set cindent:
 * $Id$
 *
 * ROX-Filer, filer for the ROX desktop project
 * By Thomas Leonard, <tal197@ecs.soton.ac.uk>.
 */

#ifndef _OPTIONS_H
#define _OPTIONS_H

typedef char *OptionFunc(char *value);

/* Take a line from the file and process it. Return NULL on success,
 * or a pointer to an error string.
 */
typedef char *ParseFunc(char *line);

#include "filer.h"

/* Prototypes */

void options_init(void);
void option_register(char *key, OptionFunc *func);
void options_load(void);
void options_show(FilerWindow *filer_window);
void option_write(char *name, char *value);
void parse_file(char *path, ParseFunc *parse_line);

#endif /* _OPTIONS_H */
