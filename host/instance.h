/* per-user VM instance lock, held for the process lifetime, and launch handshake. */
#ifndef MIME_INSTANCE_H
#define MIME_INSTANCE_H

#include "mime_host.h"

#include <sys/types.h>

typedef struct MimeInstanceHolder {
    pid_t pid;
    char socket[MIME_PATH_MAX];
} MimeInstanceHolder;

/* returns the locked fd, -1 with holder filled if another machine owns it, or -2 on error. */
int mime_instance_lock(const MimePaths *paths, MimeInstanceHolder *holder);

/* reports the running machine, if any, without keeping the lock. holder may be NULL. */
bool mime_instance_running(const MimePaths *paths, MimeInstanceHolder *holder);

/*
 * launches the same executable with the locked fd. the machine entry point requires an inherited socket instead of a
 * command-line argument.
 */
pid_t mime_instance_launch(const MimePaths *paths, int lock_fd);

/* validates launch through mime_instance_launch, stores the inherited lock fd and publishes holder information. */
bool mime_instance_accept_launch(int *lock_fd);

/* records the calling process and the machine socket in the lock file. */
void mime_instance_publish(int lock_fd, const MimePaths *paths);

#endif
