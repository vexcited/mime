/* CLI-to-daemon protocol over a Unix stream socket in the runtime directory. */
#ifndef MIME_IPC_H
#define MIME_IPC_H

#include "mime_host.h"

enum {
    MIME_IPC_RUN = 1, /* MimeIpcRun, cwd\0 then argc strings\0. */
    MIME_IPC_STOP = 2,
    MIME_IPC_STATUS = 3,
    MIME_IPC_INPUT = 4,  /* bytes for the console of the run, none at end of file. */
    MIME_IPC_SIGNAL = 5, /* uint32 ctrl event: CTRL_C_EVENT, CTRL_BREAK_EVENT. */
    MIME_IPC_RESIZE = 6, /* uint32 columns | rows << 16. */
    MIME_IPC_CONFIG = 7, /* nothing to list the settings, name\0 to get one, name\0value\0 to set it. */

    MIME_IPC_OK = 100,
    MIME_IPC_ERROR = 101,  /* UTF-8 message. */
    MIME_IPC_OUTPUT = 102, /* bytes the program wrote to its standard output. */
    MIME_IPC_EXIT = 103,   /* int32 exit code. */
    MIME_IPC_STATUS_REPLY = 104,
    MIME_IPC_STDERR = 105,       /* bytes the program wrote to its standard error. */
    MIME_IPC_STARTED = 106,      /* uint32 subsystem of the program. */
    MIME_IPC_CONSOLE_MODE = 107, /* uint32 console input mode the terminal follows. */
    MIME_IPC_DEBUG = 108,        /* debug output of the program, UTF-8. */
    MIME_IPC_CONFIG_REPLY = 109, /* name\0value\0 of each setting asked. */
};

/* MIME_IPC_STOP payload; older clients omit it. */
typedef struct MimeIpcStop {
    uint32_t flags;      /* MIME_SHUTDOWN_FORCE */
    uint32_t timeout_ms; /* the guest's limit for the whole shutdown, 0 for its default. */
} MimeIpcStop;

typedef struct MimeIpcRun {
    uint32_t flags;        /* MIME_RUN_TTY_... */
    uint32_t console_size;   /* columns | rows << 16. */
    uint32_t console_cursor; /* column + 1 | row + 1 << 16 of the cursor, 0 when unknown. */
} MimeIpcRun;

typedef struct MimeIpcHeader {
    uint32_t type;
    uint32_t size; /* payload bytes. */
} MimeIpcHeader;

typedef struct MimeIpcStatus {
    uint32_t ready;
    uint32_t pid;
    uint64_t uptime_ms;
    uint32_t running_programs; /* runs a client still waits on. */
    uint32_t pad;
    /* older machines end here. */
    uint32_t user_processes; /* processes a run started and their descendants, MimeSharedPage.user_processes. */
    uint32_t build_id_size;
    uint8_t build_id[32]; /* the build of the machine's mime, see mime_build_id. */
} MimeIpcStatus;

/* the size of the replies of older machines, which tell neither their build nor the user's processes. */
#define MIME_IPC_STATUS_V1_SIZE offsetof(MimeIpcStatus, user_processes)

#define MIME_IPC_MAX_PAYLOAD (1u << 20)

bool mime_ipc_send(int fd, uint32_t type, const void *payload, uint32_t size);
/* returns a malloc-owned, NUL-terminated payload or NULL on EOF or error. */
bool mime_ipc_recv(int fd, MimeIpcHeader *header, void **payload);
int mime_ipc_connect(const char *socket_path);

#endif
