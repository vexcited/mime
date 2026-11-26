#ifndef MIME_INVOKER_H
#define MIME_INVOKER_H

#include <stdbool.h>

/* under sudo, takes the environment of the user who ran it, so every path is theirs. */
void mime_invoker_adopt(void);

/* gives up root for the user who ran sudo. true when there is nothing to give up. */
bool mime_invoker_drop(void);

#endif
