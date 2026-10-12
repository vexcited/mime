/*
 * the single machine instance: a per user lock that the machine process
 * holds for its whole life, and the handshake that launches it.
 */
#ifndef MIME_INSTANCE_H
#define MIME_INSTANCE_H

#include "mime_host.h"

#include <sys/types.h>

/* what the lock holder published about itself. */
typedef struct MimeInstanceHolder {
    pid_t pid;
    char socket[MIME_PATH_MAX];
} MimeInstanceHolder;

/*
 * takes the instance lock. returns the locked fd, or -1 when another
 * machine holds it, with holder filled in, or -2 on error.
 */
int mime_instance_lock(const MimePaths *paths, MimeInstanceHolder *holder);

/* reports the running machine, if any, without keeping the lock. holder may be NULL. */
bool mime_instance_running(const MimePaths *paths, MimeInstanceHolder *holder);

/*
 * starts the machine process with the locked fd. it reuses the mime
 * binary but enters through an inherited socket instead of a command,
 * so it cannot be started by hand.
 */
pid_t mime_instance_launch(const MimePaths *paths, int lock_fd);

/*
 * in the machine process: true when this process was started by
 * mime_instance_launch. stores the inherited lock fd and publishes the
 * holder information into it.
 */
bool mime_instance_accept_launch(int *lock_fd);

/* records the calling process and the machine socket in the lock file. */
void mime_instance_publish(int lock_fd, const MimePaths *paths);

#endif
