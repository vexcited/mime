/*
 * the console driver, \Device\ConDrv: the kernel side of the console of a
 * host run, which is the terminal of `mime run`. kernelbase opens it once
 * per process and drives it with these private device controls.
 */
#ifndef MIME_CONDRV_H
#define MIME_CONDRV_H

#include <mime/nt.h>

#define MIME_CONDRV_DEVICE L"\\Device\\ConDrv"
#define MIME_CONDRV_STREAM_DEVICE L"\\Device\\ConDrvStream" /* \Input, \Output, \Error: redirected streams as pipe ends */

#define MIME_CONDRV_IOCTL(n) (((ULONG)0x50 << 16) | ((ULONG)(n) << 2)) /* FILE_DEVICE_CONSOLE, METHOD_BUFFERED */

#define IOCTL_CONDRV_STATE MIME_CONDRV_IOCTL(0x800)        /* out MIME_CONDRV_STATE */
#define IOCTL_CONDRV_WRITE MIME_CONDRV_IOCTL(0x801)        /* in ULONG stream, then the bytes */
#define IOCTL_CONDRV_READ MIME_CONDRV_IOCTL(0x802)         /* out bytes; waits for input, 0 bytes is end of file */
#define IOCTL_CONDRV_PEEK MIME_CONDRV_IOCTL(0x803)         /* out bytes without taking them or waiting */
#define IOCTL_CONDRV_SET_MODE MIME_CONDRV_IOCTL(0x804)     /* in ULONG input mode, ULONG output mode */
#define IOCTL_CONDRV_REGISTER MIME_CONDRV_IOCTL(0x805)     /* in MIME_CONDRV_REGISTER */
#define IOCTL_CONDRV_ATTACH MIME_CONDRV_IOCTL(0x806)       /* in ULONG process id, ATTACH_PARENT_PROCESS for the run's */
#define IOCTL_CONDRV_FREE MIME_CONDRV_IOCTL(0x807)
#define IOCTL_CONDRV_GENERATE MIME_CONDRV_IOCTL(0x808)     /* in ULONG event, ULONG process group */
#define IOCTL_CONDRV_PROCESSES MIME_CONDRV_IOCTL(0x809)    /* out ULONG process ids */
#define IOCTL_CONDRV_FLUSH_INPUT MIME_CONDRV_IOCTL(0x80A)
#define IOCTL_CONDRV_SYNC_EVENT MIME_CONDRV_IOCTL(0x80B) /* sets the input event when input waits, else resets it */

#define MIME_CONDRV_STDOUT 1
#define MIME_CONDRV_STDERR 2

#define MIME_CONDRV_ATTACHED 0x1  /* the process has a console */
#define MIME_CONDRV_TTY_INPUT 0x2 /* input comes from a terminal, not a pipe or file */
#define MIME_CONDRV_TTY_OUTPUT 0x4
#define MIME_CONDRV_CLOSED 0x8 /* the terminal went away */
#define MIME_CONDRV_TTY_ERROR 0x10

typedef struct MIME_CONDRV_REGISTER {
    ULONG64 CtrlRoutine; /* started on a new thread with the event for each ctrl event */
    ULONG64 InputEvent;  /* event handle the console keeps set while input waits */
} MIME_CONDRV_REGISTER;

typedef struct MIME_CONDRV_STATE {
    ULONG Flags;
    ULONG InputMode;
    ULONG OutputMode;
    USHORT Columns;
    USHORT Rows;
    ULONG InputBytes; /* waiting to be read */
    ULONG ProcessGroup;
} MIME_CONDRV_STATE;

#endif
