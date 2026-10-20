/*
 * protocol between the mime cli and the background machine process,
 * spoken over a unix stream socket in the runtime directory.
 */
#ifndef MIME_IPC_H
#define MIME_IPC_H

#include "mime_host.h"

enum {
    MIME_IPC_RUN = 1, /* MimeIpcRun, cwd\0 then argc strings\0 */
    MIME_IPC_STOP = 2,
    MIME_IPC_STATUS = 3,
    MIME_IPC_INPUT = 4,  /* bytes for the console of the run, none at end of file */
    MIME_IPC_SIGNAL = 5, /* uint32 ctrl event: CTRL_C_EVENT, CTRL_BREAK_EVENT */
    MIME_IPC_RESIZE = 6, /* uint32 columns | rows << 16 */

    MIME_IPC_OK = 100,
    MIME_IPC_ERROR = 101,  /* utf-8 message */
    MIME_IPC_OUTPUT = 102, /* bytes the program wrote to its standard output */
    MIME_IPC_EXIT = 103,   /* int32 exit code */
    MIME_IPC_STATUS_REPLY = 104,
    MIME_IPC_STDERR = 105,       /* bytes the program wrote to its standard error */
    MIME_IPC_STARTED = 106,      /* uint32 subsystem of the program */
    MIME_IPC_CONSOLE_MODE = 107, /* uint32 console input mode the terminal follows */
    MIME_IPC_DEBUG = 108,        /* debug output of the program, utf-8 */
};

/* the payload of MIME_IPC_STOP, which older clients leave out: what `mime down` was asked. */
typedef struct MimeIpcStop {
    uint32_t flags;      /* MIME_SHUTDOWN_FORCE */
    uint32_t timeout_ms; /* the guest's limit for the whole shutdown, 0 for its default */
} MimeIpcStop;

typedef struct MimeIpcRun {
    uint32_t flags;        /* MIME_RUN_TTY_... */
    uint32_t console_size;   /* columns | rows << 16 */
    uint32_t console_cursor; /* column + 1 | row + 1 << 16 of the cursor, 0 when unknown */
} MimeIpcRun;

typedef struct MimeIpcHeader {
    uint32_t type;
    uint32_t size; /* payload bytes */
} MimeIpcHeader;

typedef struct MimeIpcStatus {
    uint32_t ready;
    uint32_t pid;
    uint64_t uptime_ms;
    uint32_t running_programs;
    uint32_t pad;
} MimeIpcStatus;

#define MIME_IPC_MAX_PAYLOAD (1u << 20)

bool mime_ipc_send(int fd, uint32_t type, const void *payload, uint32_t size);
/* returns a malloc'd payload (NUL terminated) or NULL on eof/error. */
bool mime_ipc_recv(int fd, MimeIpcHeader *header, void **payload);
int mime_ipc_connect(const char *socket_path);

#endif
