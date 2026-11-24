#ifndef MIME_HARDERROR_H
#define MIME_HARDERROR_H

#include <mime/nt.h>

#define MIME_HARD_ERROR_PARAMETERS 5

/*
 * a hard error as the csrss of the session gets it, which the kernel hands harderror.exe in the runtime data of its
 * process parameters. strings follow the block, and a string parameter is its offset in the block. the exit code of
 * the program is the response.
 */
typedef struct MIME_HARD_ERROR {
    ULONG Size;
    NTSTATUS Status; /* without HARDERROR_OVERRIDE_ERRORMODE. */
    ULONG Option;
    ULONG Count;
    ULONG StringMask;
    ULONG ProcessId;
    ULONG ThreadId;
    ULONG ImageName; /* the file name of the image, none for a process without one. */
    ULONG ImageNameLength;
    ULONG StringLength[MIME_HARD_ERROR_PARAMETERS];
    ULONG64 Parameters[MIME_HARD_ERROR_PARAMETERS];
} MIME_HARD_ERROR;

#endif
