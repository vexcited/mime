/*
 * protocol between the mime cli and the background machine process,
 * spoken over a unix stream socket in the runtime directory.
 */
#ifndef MIME_IPC_H
#define MIME_IPC_H

#include "mime_host.h"

enum {
    MIME_IPC_RUN = 1,    /* cwd\0 then argc strings\0 */
    MIME_IPC_STOP = 2,
    MIME_IPC_STATUS = 3,

    MIME_IPC_OK = 100,
    MIME_IPC_ERROR = 101,  /* utf-8 message */
    MIME_IPC_OUTPUT = 102, /* utf-8 text printed by the program */
    MIME_IPC_EXIT = 103,   /* int32 exit code */
    MIME_IPC_STATUS_REPLY = 104,
};

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
