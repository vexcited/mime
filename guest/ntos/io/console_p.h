/* the console driver's state, shared by console.c (the consoles) and condev.c (the device that programs open). */
#ifndef MIME_CONSOLE_P_H
#define MIME_CONSOLE_P_H

#include "../ntos.h"
#include "coninput.h"
#include "conline.h"
#include "conscreen.h"

#define CTRL_C_EVENT 0
#define CTRL_BREAK_EVENT 1
#define CTRL_CLOSE_EVENT 2
#define STATUS_CONTROL_C_EXIT ((NTSTATUS)0xC000013AL)
#define STATUS_PIPE_BROKEN ((NTSTATUS)0xC000014BL)
#define ATTACH_PARENT_PROCESS 0xFFFFFFFFu
#define CLOSE_TIMEOUT_MS 5000
#define MAX_TARGETS 64

#define ENABLE_PROCESSED_INPUT 0x1
#define ENABLE_LINE_INPUT 0x2
#define ENABLE_ECHO_INPUT 0x4
#define ENABLE_WINDOW_INPUT 0x8
#define ENABLE_MOUSE_INPUT 0x10
#define ENABLE_INSERT_MODE 0x20
#define ENABLE_QUICK_EDIT_MODE 0x40
#define ENABLE_EXTENDED_FLAGS 0x80
#define ENABLE_AUTO_POSITION 0x100
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x200
#define INPUT_MODE_MASK 0x3FF
#define DEFAULT_INPUT_MODE 0x17 /* processed, line, echo, mouse */
#define INPUT_MODE_EXTENDED (ENABLE_INSERT_MODE | ENABLE_QUICK_EDIT_MODE | ENABLE_EXTENDED_FLAGS | ENABLE_AUTO_POSITION)

/*
 * input waiting to be read: the bytes of a redirected input, or records,
 * which is what a terminal's input is decoded into and what programs
 * write. a chunk of no bytes is an end of file.
 */
typedef struct CON_CHUNK {
    LIST_ENTRY Links;
    BOOLEAN Records;
    ULONG Length;    /* bytes or records */
    ULONG First;     /* the first not read */
    ULONG Offset;    /* bytes of the first record's character already read */
    ULONG HostBytes; /* acknowledged to the host once read */
    union {
        UCHAR Data[1];
        MIME_CONDRV_RECORD Record[1];
    };
} CON_CHUNK;

typedef enum {
    CON_BACKEND_RUN,  /* the terminal of a host run */
    CON_BACKEND_PTY,  /* a pseudo console: the owner carries the bytes */
    CON_BACKEND_NONE, /* nothing shows it */
} CON_BACKEND;

typedef struct CON_PTY_CHUNK {
    LIST_ENTRY Links;
    ULONG Length;
    UCHAR Data[1];
} CON_PTY_CHUNK;

struct _MIME_CONSOLE {
    LIST_ENTRY Links;
    LONG References; /* attached processes, handles, the host run and waiting readers */
    ULONG64 RequestId;
    CON_BACKEND Backend;
    ULONG Flags; /* MIME_CONDRV_TTY_... and MIME_CONDRV_CLOSED */
    ULONG InputMode; /* without the bits of the extended flags, which are kept below */
    BOOLEAN QuickEdit, InsertDefault, AutoPosition, ExtendedGiven;
    ULONG AbortGeneration; /* counts the ctrl+c and ctrl+break that end the reads that wait */
    UINT InputCp, OutputCp;
    WCHAR Title[260];
    WCHAR OriginalTitle[260];
    BOOLEAN TitleSet;

    /* the screen buffers and the terminal they are shown on */
    CsTerminal Term;
    CsBuffer *Main;
    ULONG OutStream;
    PWCHAR Scratch; /* wide text of one write */
    ULONG ScratchLength;
    UCHAR WriteCarry[4]; /* a utf-8 character cut between two writes */
    ULONG WriteCarryLength;

    /* input */
    LIST_ENTRY Input;
    ULONG InputBytes;  /* in byte chunks */
    ULONG InputEvents; /* records not read */
    KEVENT InputReady; /* set while input, an end of file or the closing waits */
    LIST_ENTRY InputHandles;
    UCHAR Carry[16]; /* terminal input that does not make a whole key yet */
    ULONG CarryLength;
    ULONG PendingAck; /* host bytes that made no record yet */
    BOOLEAN HasAnsiCarry;
    MIME_CONDRV_RECORD AnsiCarry; /* the lead byte WriteConsoleInputA got without its trail byte */
    BOOLEAN OutputPaused;         /* by the pause key */
    KEVENT OutputResumed;
    PWCHAR LinePending; /* characters of a line that the reader had no room for */
    ULONG LinePendingLength, LinePendingFirst;
    ULONG LinePendingLines; /* lines of that, when an alias made several: each read takes one */
    PUCHAR BytePending; /* bytes of the input code page of the same */
    ULONG BytePendingLength, BytePendingFirst;
    ULONG MouseButtons; /* the buttons the terminal reported down */
    ULONG64 LastClick;  /* time and place of the last press, for the double click flag */
    SHORT LastClickX, LastClickY;
    ULONG LastClickButton;
    ConLineState *Lines;

    /* a pseudo console */
    LIST_ENTRY PtyOut;
    ULONG PtyQueued;
    KEVENT PtyReady;
    KEVENT PtySpace;
    PKPROCESS PtyOwner;
    ULONG PtyFlags;
    BOOLEAN AwaitCursor; /* the console waits for the report of the cursor of the terminal */
};

typedef enum {
    CH_CONTROL,
    CH_INPUT,
    CH_OUTPUT,
    CH_PTY,
} CON_HANDLE_KIND;

typedef struct CON_HANDLE {
    CON_HANDLE_KIND Kind;
    MIME_CONSOLE *Console;
    CsBuffer *Buffer; /* an output handle's screen buffer */
    PFILE_OBJECT File;
    LIST_ENTRY Links;
} CON_HANDLE;

extern FAST_MUTEX g_con_lock;

/* console.c */
MIME_CONSOLE *ConProcessConsole(PEPROCESS process);
void ConRelease(MIME_CONSOLE *c);
void ConReference(MIME_CONSOLE *c);
MIME_CONSOLE *ConCreateConsole(CON_BACKEND backend, ULONG64 request, ULONG tty_flags, ULONG size);
void ConFlush(MIME_CONSOLE *c);
void ConUpdateInputEvents(MIME_CONSOLE *c);
BOOLEAN ConInputWaiting(MIME_CONSOLE *c);
NTSTATUS ConSetInputMode(MIME_CONSOLE *c, ULONG mode);
ULONG ConGetInputMode(MIME_CONSOLE *c);
void ConAbortReads(MIME_CONSOLE *c, BOOLEAN flush);
ULONG ConDispatchEvent(MIME_CONSOLE *c, ULONG event, ULONG group);
void ConPtyStart(MIME_CONSOLE *c, ULONG size, ULONG flags);
MIME_CONSOLE *ConPtyFromFile(PFILE_OBJECT file);
void ConHostInputLocked(MIME_CONSOLE *c, const UCHAR *data, ULONG length, BOOLEAN *ctrl_c);
void ConResize(MIME_CONSOLE *c, ULONG size);
void ConClose(MIME_CONSOLE *c);
NTSTATUS ConAttachProcess(PEPROCESS self, ULONG pid);
ULONG ConListProcesses(MIME_CONSOLE *c, PULONG out, ULONG max);
NTSTATUS ConAllocFor(PEPROCESS self, ULONG flags);
void ConSendOutput(MIME_CONSOLE *c, const void *data, ULONG length, ULONG stream);
void ConWriteInputText(MIME_CONSOLE *c, const WCHAR *text, ULONG count);
void ConSetTitle(MIME_CONSOLE *c, const WCHAR *title, ULONG count);

/* the input queue, with the lock held */
CON_CHUNK *ConNewChunk(BOOLEAN records, ULONG length);
void ConQueueChunk(MIME_CONSOLE *c, CON_CHUNK *k, BOOLEAN head);
void ConFreeChunk(MIME_CONSOLE *c, CON_CHUNK *k, ULONG *acked);
ULONG ConTakeInput(MIME_CONSOLE *c, PUCHAR out, ULONG length, BOOLEAN peek, BOOLEAN *eof, ULONG *acked);
ULONG ConTakeRecords(MIME_CONSOLE *c, MIME_CONDRV_RECORD *out, ULONG room, BOOLEAN peek, BOOLEAN *eof, ULONG *acked);
NTSTATUS ConWriteRecords(MIME_CONSOLE *c, MIME_CONDRV_RECORD *r, ULONG count, BOOLEAN head, ULONG *written);
void ConAfterRead(MIME_CONSOLE *c, ULONG acked, BOOLEAN clear);
void ConFlushInput(MIME_CONSOLE *c);

/* code pages, condev.c */
ULONG ConBytesToWide(UINT cp, const UCHAR *in, ULONG length, PWCHAR out, ULONG room, UCHAR carry[4], PULONG carry_length,
                     PULONG consumed);
ULONG ConWideToBytes(UINT cp, const WCHAR *in, ULONG count, PUCHAR out, ULONG room, PULONG consumed);

#endif
