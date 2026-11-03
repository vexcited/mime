/*
 * the console driver, \Device\ConDrv: the kernel side of a console, conhost's role on Windows. a console has an
 * input buffer and screen buffers. a console program's standard handles are files of the driver: \CurrentIn for
 * input and \CurrentOut for the active screen buffer, as are the handles CreateConsoleScreenBuffer opens with
 * \ScreenBuffer. kernelbase drives them with these private device controls plus ReadFile and WriteFile.
 *
 * a console shows on the terminal of a host run, on a pseudo console \Pty whose owner carries its bytes, in a window of
 * conhost.exe, which attaches through \Host, or nowhere.
 */
#ifndef MIME_CONDRV_H
#define MIME_CONDRV_H

#include <mime/nt.h>

#define MIME_CONDRV_DEVICE L"\\Device\\ConDrv"
#define MIME_CONDRV_STREAM_DEVICE L"\\Device\\ConDrvStream" /* \Input, \Output, \Error: redirected streams as pipe ends */

/* names under the device that open handles. */
#define MIME_CONDRV_CURRENT_IN L"CurrentIn"       /* the input of the process's console */
#define MIME_CONDRV_CURRENT_OUT L"CurrentOut"     /* its active screen buffer */
#define MIME_CONDRV_SCREEN_BUFFER L"ScreenBuffer" /* a new screen buffer of that console */
#define MIME_CONDRV_PTY L"Pty"                    /* a new pseudo console */
#define MIME_CONDRV_HOST L"Host"                  /* the window host of a console that shows in a window */

#define MIME_CONDRV_IOCTL(n) (((ULONG)0x50 << 16) | ((ULONG)(n) << 2)) /* FILE_DEVICE_CONSOLE, METHOD_BUFFERED */

/* on any handle: what the process is attached to. */
#define IOCTL_CONDRV_STATE MIME_CONDRV_IOCTL(0x800)    /* out MIME_CONDRV_STATE */
#define IOCTL_CONDRV_REGISTER MIME_CONDRV_IOCTL(0x805) /* in MIME_CONDRV_REGISTER */
#define IOCTL_CONDRV_ATTACH MIME_CONDRV_IOCTL(0x806)   /* in ULONG process id, ATTACH_PARENT_PROCESS for the run's */
#define IOCTL_CONDRV_FREE MIME_CONDRV_IOCTL(0x807)
#define IOCTL_CONDRV_GENERATE MIME_CONDRV_IOCTL(0x808)  /* in ULONG event, ULONG process group */
#define IOCTL_CONDRV_PROCESSES MIME_CONDRV_IOCTL(0x809) /* out ULONG count, then process ids */
#define IOCTL_CONDRV_ALLOC MIME_CONDRV_IOCTL(0x810)     /* in MIME_CONDRV_ALLOC: a console for a process without one */
#define IOCTL_CONDRV_TITLE MIME_CONDRV_IOCTL(0x811)     /* in MIME_CONDRV_TITLE, out the title: get or set */
#define IOCTL_CONDRV_CODE_PAGES MIME_CONDRV_IOCTL(0x812) /* in MIME_CONDRV_CODE_PAGES with Set, out the pages */
#define IOCTL_CONDRV_SELECTION MIME_CONDRV_IOCTL(0x814) /* out MIME_CONDRV_HOST_SELECTION: what the window has selected */
#define IOCTL_CONDRV_INPUT_EXE MIME_CONDRV_IOCTL(0x813) /* in the exe name whose history and aliases reads use, or nothing to get it */

/* on an input handle. */
#define IOCTL_CONDRV_IN_MODE MIME_CONDRV_IOCTL(0x830)       /* in MIME_CONDRV_MODE, out the mode */
#define IOCTL_CONDRV_IN_READ MIME_CONDRV_IOCTL(0x831)       /* in MIME_CONDRV_READ, out the text read */
#define IOCTL_CONDRV_IN_PEEK MIME_CONDRV_IOCTL(0x803)       /* out bytes without taking them or waiting */
#define IOCTL_CONDRV_IN_RECORDS MIME_CONDRV_IOCTL(0x80D)    /* in ULONG MIME_CONDRV_READ_..., out ULONG total then records */
#define IOCTL_CONDRV_IN_WRITE MIME_CONDRV_IOCTL(0x80C)      /* in MIME_CONDRV_WRITE_INPUT then records, out ULONG written */
#define IOCTL_CONDRV_IN_FLUSH MIME_CONDRV_IOCTL(0x80A)
#define IOCTL_CONDRV_IN_ANSI_CARRY MIME_CONDRV_IOCTL(0x80E) /* in the record to keep or none, out the one kept or none */
#define IOCTL_CONDRV_IN_HISTORY MIME_CONDRV_IOCTL(0x832)    /* in MIME_CONDRV_HISTORY, command history and its sizes */
#define IOCTL_CONDRV_IN_ALIAS MIME_CONDRV_IOCTL(0x833)      /* in MIME_CONDRV_ALIAS: aliases */

/* on a screen buffer handle. */
#define IOCTL_CONDRV_OUT_WRITE MIME_CONDRV_IOCTL(0x840)       /* in MIME_CONDRV_WRITE then the text */
#define IOCTL_CONDRV_OUT_MODE MIME_CONDRV_IOCTL(0x841)        /* in MIME_CONDRV_MODE, out the mode */
#define IOCTL_CONDRV_OUT_INFO MIME_CONDRV_IOCTL(0x842)        /* in MIME_CONDRV_INFO_REQUEST, out MIME_CONDRV_INFO */
#define IOCTL_CONDRV_OUT_SIZE MIME_CONDRV_IOCTL(0x843)        /* in MIME_CONDRV_COORD */
#define IOCTL_CONDRV_OUT_CURSOR MIME_CONDRV_IOCTL(0x844)      /* in MIME_CONDRV_CURSOR */
#define IOCTL_CONDRV_OUT_WINDOW MIME_CONDRV_IOCTL(0x845)      /* in MIME_CONDRV_WINDOW */
#define IOCTL_CONDRV_OUT_ATTRIBUTE MIME_CONDRV_IOCTL(0x846)   /* in USHORT: the text attributes */
#define IOCTL_CONDRV_OUT_READ_RECT MIME_CONDRV_IOCTL(0x847)   /* in MIME_CONDRV_RECT_IO, out the cells */
#define IOCTL_CONDRV_OUT_WRITE_RECT MIME_CONDRV_IOCTL(0x848)  /* in MIME_CONDRV_RECT_IO then the cells */
#define IOCTL_CONDRV_OUT_READ_RUN MIME_CONDRV_IOCTL(0x849)    /* in MIME_CONDRV_RUN, out the characters or attributes */
#define IOCTL_CONDRV_OUT_WRITE_RUN MIME_CONDRV_IOCTL(0x84A)   /* in MIME_CONDRV_RUN then the data */
#define IOCTL_CONDRV_OUT_FILL MIME_CONDRV_IOCTL(0x84B)        /* in MIME_CONDRV_FILL */
#define IOCTL_CONDRV_OUT_SCROLL MIME_CONDRV_IOCTL(0x84C)      /* in MIME_CONDRV_SCROLL */
#define IOCTL_CONDRV_OUT_ACTIVATE MIME_CONDRV_IOCTL(0x84D)    /* makes this the shown buffer */
#define IOCTL_CONDRV_OUT_FONT MIME_CONDRV_IOCTL(0x84E)        /* out MIME_CONDRV_FONT */

/* on a window host handle, the way conhost.exe serves a console window. */
#define IOCTL_CONDRV_HOST_ATTACH MIME_CONDRV_IOCTL(0x870)    /* in MIME_CONDRV_HOST_ATTACH, out MIME_CONDRV_HOST_STATE */
#define IOCTL_CONDRV_HOST_QUERY MIME_CONDRV_IOCTL(0x871)     /* out MIME_CONDRV_HOST_STATE, clears what is pending */
#define IOCTL_CONDRV_HOST_CELLS MIME_CONDRV_IOCTL(0x872)     /* in MIME_CONDRV_HOST_CELLS, out the cells of rows of the shown buffer */
#define IOCTL_CONDRV_HOST_INPUT MIME_CONDRV_IOCTL(0x873)     /* in MIME_CONDRV_RECORD records the window made */
#define IOCTL_CONDRV_HOST_WINDOW MIME_CONDRV_IOCTL(0x874)    /* in MIME_CONDRV_HOST_WINDOW: the user resized or scrolled the window */
#define IOCTL_CONDRV_HOST_FONT MIME_CONDRV_IOCTL(0x875)      /* in MIME_CONDRV_FONT_SET: the font and the limits of the screen */
#define IOCTL_CONDRV_HOST_CLOSE MIME_CONDRV_IOCTL(0x876)     /* the user closed the window */
#define IOCTL_CONDRV_HOST_SELECTION MIME_CONDRV_IOCTL(0x877) /* in MIME_CONDRV_HOST_SELECTION: what the window has selected */
#define IOCTL_CONDRV_HOST_SETTINGS MIME_CONDRV_IOCTL(0x878)  /* in MIME_CONDRV_HOST_SETTINGS: the properties dialog's choices */

/* on a pseudo console handle. */
#define IOCTL_CONDRV_PTY_CREATE MIME_CONDRV_IOCTL(0x860)  /* in MIME_CONDRV_PTY_CREATE: size and flags */
#define IOCTL_CONDRV_PTY_OUTPUT MIME_CONDRV_IOCTL(0x861)  /* out the terminal bytes the console made; waits for some */
#define IOCTL_CONDRV_PTY_INPUT MIME_CONDRV_IOCTL(0x862)   /* in the terminal bytes the owner got from its terminal */
#define IOCTL_CONDRV_PTY_RESIZE MIME_CONDRV_IOCTL(0x863)  /* in MIME_CONDRV_COORD */
#define IOCTL_CONDRV_PTY_CLOSE MIME_CONDRV_IOCTL(0x864)   /* the owner is done: the programs get CTRL_CLOSE_EVENT */

#define MIME_CONDRV_WRITE_APPEND 0x1 /* else the records go ahead of the waiting ones */
#define MIME_CONDRV_READ_PEEK 0x1
#define MIME_CONDRV_READ_NOWAIT 0x2

#define MIME_CONDRV_STDOUT 1
#define MIME_CONDRV_STDERR 2

#define MIME_CONDRV_ATTACHED 0x1  /* the process has a console */
#define MIME_CONDRV_TTY_INPUT 0x2 /* input comes from a terminal, not a pipe or file */
#define MIME_CONDRV_TTY_OUTPUT 0x4
#define MIME_CONDRV_CLOSED 0x8 /* the terminal went away */
#define MIME_CONDRV_TTY_ERROR 0x10
#define MIME_CONDRV_PTY_HOST 0x20 /* the console belongs to a pseudo console */
#define MIME_CONDRV_HEADLESS 0x40 /* nothing shows it */
#define MIME_CONDRV_HAS_WINDOW 0x80 /* a window of conhost.exe shows it */

typedef struct MIME_CONDRV_REGISTER {
    ULONG64 CtrlRoutine; /* started on a new thread with the event for each ctrl event */
    ULONG64 Reserved;
} MIME_CONDRV_REGISTER;

#define MIME_CONDRV_ALLOC_RUN_TERMINAL 0x1 /* the terminal of the run that started the program, if there is one and it is free */
#define MIME_CONDRV_ALLOC_PREFER_NEW 0x2

typedef struct MIME_CONDRV_ALLOC {
    ULONG Flags;
    ULONG Reserved;
} MIME_CONDRV_ALLOC;

typedef struct MIME_CONDRV_MODE {
    ULONG Set; /* 1 to set Mode */
    ULONG Mode;
} MIME_CONDRV_MODE;

typedef struct MIME_CONDRV_TITLE {
    ULONG Set;     /* 1 to set the title to the text that follows */
    ULONG Original; /* 1 to get the title the console started with */
    ULONG Length;  /* characters that follow when setting */
} MIME_CONDRV_TITLE;

typedef struct MIME_CONDRV_CODE_PAGES {
    ULONG Set; /* bit 0 sets Input, bit 1 sets Output */
    ULONG Input;
    ULONG Output;
} MIME_CONDRV_CODE_PAGES;

/* ReadConsole and ReadFile of the input. */
#define MIME_CONDRV_READ_WIDE 0x1  /* text is UTF-16, else bytes of the input code page */
#define MIME_CONDRV_READ_NOEDIT 0x2 /* ReadFile: line input as the mode says, none of the ReadConsole control */
typedef struct MIME_CONDRV_READ {
    ULONG Flags;
    ULONG Length;        /* characters when wide, else bytes, that the caller has room for */
    ULONG InitialChars;  /* characters of the buffer that start the line */
    ULONG WakeupMask;    /* control characters that end a line read */
    ULONG Control;       /* 1 when WakeupMask and InitialChars are meant */
    ULONG ExeNameLength; /* characters of the name of the exe that follow the initial text */
    /* then InitialChars UTF-16 characters, then the name */
} MIME_CONDRV_READ;
typedef struct MIME_CONDRV_READ_RESULT {
    ULONG ControlKeyState; /* of the key that ended the line */
    ULONG Reserved;
    /* then the text */
} MIME_CONDRV_READ_RESULT;

typedef struct MIME_CONDRV_COORD {
    SHORT X, Y;
} MIME_CONDRV_COORD;

typedef struct MIME_CONDRV_RECT {
    SHORT Left, Top, Right, Bottom;
} MIME_CONDRV_RECT;

typedef struct MIME_CONDRV_CELL {
    USHORT Char;
    USHORT Attributes;
} MIME_CONDRV_CELL;

typedef struct MIME_CONDRV_WRITE {
    ULONG Flags; /* 1: bytes in the output code page, else UTF-16 */
    ULONG Length;
} MIME_CONDRV_WRITE;
#define MIME_CONDRV_WRITE_BYTES 0x1

typedef struct MIME_CONDRV_INFO_REQUEST {
    ULONG Set; /* then MIME_CONDRV_INFO follows */
} MIME_CONDRV_INFO_REQUEST;

typedef struct MIME_CONDRV_INFO {
    MIME_CONDRV_COORD Size;
    MIME_CONDRV_COORD Cursor;
    MIME_CONDRV_RECT Window;
    MIME_CONDRV_COORD MaxWindow;
    USHORT Attributes;
    USHORT PopupAttributes;
    ULONG ColorTable[16]; /* 0x00BBGGRR */
    ULONG CursorSize;
    ULONG CursorVisible;
    ULONG Mode;
} MIME_CONDRV_INFO;

typedef struct MIME_CONDRV_CURSOR {
    ULONG Set;
    MIME_CONDRV_COORD Position; /* set: the position; the size and visibility are set with Size */
    ULONG Size;                 /* 0 when only the position is set */
    ULONG Visible;
} MIME_CONDRV_CURSOR;

typedef struct MIME_CONDRV_WINDOW {
    ULONG Absolute;
    MIME_CONDRV_RECT Rect;
} MIME_CONDRV_WINDOW;

/* a rectangle of cells: width times height cells follow in a write and come back in a read. */
typedef struct MIME_CONDRV_RECT_IO {
    MIME_CONDRV_RECT Rect;
} MIME_CONDRV_RECT_IO;

#define MIME_CONDRV_RUN_CHARS 0x1
#define MIME_CONDRV_RUN_ATTRIBUTES 0x2
typedef struct MIME_CONDRV_RUN {
    ULONG Kind;
    MIME_CONDRV_COORD Start;
    ULONG Count;
} MIME_CONDRV_RUN;

typedef struct MIME_CONDRV_FILL {
    ULONG Kind;
    MIME_CONDRV_COORD Start;
    ULONG Count;
    USHORT Value;
} MIME_CONDRV_FILL;

typedef struct MIME_CONDRV_SCROLL {
    MIME_CONDRV_RECT Source;
    MIME_CONDRV_RECT Clip;
    ULONG HasClip;
    MIME_CONDRV_COORD Destination;
    MIME_CONDRV_CELL Fill;
} MIME_CONDRV_SCROLL;

typedef struct MIME_CONDRV_FONT {
    MIME_CONDRV_COORD Size;
    ULONG Weight;
    ULONG Family;
    WCHAR Face[32];
} MIME_CONDRV_FONT;

typedef struct MIME_CONDRV_PTY_CREATE {
    MIME_CONDRV_COORD Size;
    ULONG Flags; /* PSEUDOCONSOLE_... */
} MIME_CONDRV_PTY_CREATE;

typedef struct MIME_CONDRV_HISTORY {
    ULONG Op;
    ULONG Value;
    ULONG Value2;
    ULONG ExeNameLength; /* characters of the name that follow, for the operations of one exe */
} MIME_CONDRV_HISTORY;
enum {
    MIME_CONDRV_HISTORY_GET_INFO = 1,  /* out MIME_CONDRV_HISTORY_INFO */
    MIME_CONDRV_HISTORY_SET_INFO,      /* Value is the buffer size, Value2 the count of buffers, and the flags follow */
    MIME_CONDRV_HISTORY_GET,           /* the commands of the exe, separated by NUL */
    MIME_CONDRV_HISTORY_LENGTH,
    MIME_CONDRV_HISTORY_EXPUNGE,
    MIME_CONDRV_HISTORY_SET_COUNT,
};
typedef struct MIME_CONDRV_HISTORY_INFO {
    ULONG BufferSize;
    ULONG NumberOfBuffers;
    ULONG Flags;
} MIME_CONDRV_HISTORY_INFO;

typedef struct MIME_CONDRV_ALIAS {
    ULONG Op;
    ULONG SourceLength; /* characters that follow: the source, the target, the name of the exe */
    ULONG TargetLength;
    ULONG ExeNameLength;
} MIME_CONDRV_ALIAS;
enum {
    MIME_CONDRV_ALIAS_ADD = 1,
    MIME_CONDRV_ALIAS_GET,
    MIME_CONDRV_ALIAS_LIST,       /* all aliases of an exe as source=target, each ended by NUL */
    MIME_CONDRV_ALIAS_EXES,       /* all exe names, each ended by NUL */
    MIME_CONDRV_ALIAS_EXPUNGE,
};

/* an input record, laid out as INPUT_RECORD. */
typedef struct MIME_CONDRV_RECORD {
    USHORT EventType;
    USHORT Reserved;
    union {
        struct {
            ULONG KeyDown;
            USHORT RepeatCount;
            USHORT VirtualKey;
            USHORT ScanCode;
            WCHAR Char;
            ULONG ControlKeyState;
        } Key;
        struct {
            SHORT X;
            SHORT Y;
            ULONG ButtonState;
            ULONG ControlKeyState;
            ULONG EventFlags;
        } Mouse;
        ULONG Value; /* the buffer size of a window buffer size event, the command of a menu event, the focus */
    };
} MIME_CONDRV_RECORD;

typedef struct MIME_CONDRV_WRITE_INPUT {
    ULONG Flags; /* MIME_CONDRV_WRITE_... */
    ULONG Reserved;
} MIME_CONDRV_WRITE_INPUT;

typedef struct MIME_CONDRV_STATE {
    ULONG Flags;
    ULONG InputMode;
    ULONG OutputMode;
    USHORT Columns;
    USHORT Rows;
    ULONG InputBytes; /* waiting to be read */
    ULONG ProcessGroup;
    ULONG InputEvents; /* records waiting to be read */
    ULONG64 Window;    /* the window of a console that has one, else 0 */
} MIME_CONDRV_STATE;

/* ---- the window host. */

#define MIME_CONDRV_ALLOC_WINDOW 0x4 /* AllocConsole: a window of conhost.exe when no run terminal is taken */

typedef struct MIME_CONDRV_HOST_ATTACH {
    ULONG64 Console; /* the id on the command line of conhost.exe */
    ULONG64 Window;  /* the window handle of the host, which GetConsoleWindow returns */
    ULONG64 Event;   /* a handle of the host's to an event that is set while the console has changes to tell */
} MIME_CONDRV_HOST_ATTACH;

/* what changed since the host last asked. */
#define MIME_CONDRV_CHANGED_TITLE 0x01
#define MIME_CONDRV_CHANGED_CELLS 0x02
#define MIME_CONDRV_CHANGED_WINDOW 0x04 /* the window or the buffer size, or the position of the viewport */
#define MIME_CONDRV_CHANGED_MODE 0x08
#define MIME_CONDRV_CHANGED_BELL 0x10
#define MIME_CONDRV_CHANGED_PALETTE 0x20
#define MIME_CONDRV_CHANGED_FONT 0x40
#define MIME_CONDRV_CHANGED_END 0x80   /* no process is attached any more: the host closes its window and exits */
#define MIME_CONDRV_CHANGED_ALL 0x7F

#define MIME_CONDRV_HOST_STARTUP_POSITION 0x1 /* StartX and StartY are given, in pixels */
#define MIME_CONDRV_HOST_STARTUP_SIZE 0x2     /* SizeX and SizeY are given, in pixels */
#define MIME_CONDRV_HOST_STARTUP_SHOW 0x4     /* ShowWindow is given */

typedef struct MIME_CONDRV_HOST_STATE {
    ULONG Changed;
    ULONG Flags;           /* MIME_CONDRV_... flags of the console */
    ULONG64 Generation;    /* counts the changes */
    MIME_CONDRV_COORD Size;   /* the buffer */
    MIME_CONDRV_RECT Window;  /* the viewport in the buffer */
    MIME_CONDRV_COORD Cursor;
    ULONG CursorSize;
    ULONG CursorVisible;
    ULONG InputMode;
    ULONG OutputMode;
    ULONG Scrolls;         /* rows that scrolled off the top, for positions that must follow the text */
    ULONG BellCount;
    USHORT Attributes;
    USHORT PopupAttributes;
    ULONG ColorTable[16];  /* 0x00BBGGRR */
    ULONG HistoryBufferSize, HistoryBuffers, HistoryFlags;
    ULONG Alternate;       /* 1 while the shown buffer is a program's alternate screen: no scrollback */
    ULONG QuickEdit;       /* the mouse selects instead of reporting */
    ULONG InsertMode;
    ULONG StartupFlags;
    ULONG ShowWindow;
    LONG StartX, StartY;
    ULONG SizeX, SizeY;
    ULONG TitleLength;
    WCHAR Title[260];
} MIME_CONDRV_HOST_STATE;

/* a window of cells of the shown buffer, left to right in rows, top to bottom. */
typedef struct MIME_CONDRV_HOST_CELLS {
    LONG Top;
    ULONG Rows;
    LONG Left;
    ULONG Width;
} MIME_CONDRV_HOST_CELLS;

#define MIME_CONDRV_CELL_LEAD 0x1  /* the left half of a wide character */
#define MIME_CONDRV_CELL_TRAIL 0x2 /* the right half, with no character of its own */
#define MIME_CONDRV_CELL_BOLD 0x0001
#define MIME_CONDRV_CELL_ITALIC 0x0002
#define MIME_CONDRV_CELL_BLINK 0x0004
#define MIME_CONDRV_CELL_CONCEAL 0x0008
#define MIME_CONDRV_CELL_STRIKE 0x0010
#define MIME_CONDRV_CELL_FAINT 0x0020
#define MIME_CONDRV_CELL_UNDERLINE_MASK 0x01C0 /* 0 none, 1 single, 2 double, 3 curly, 4 dotted, 5 dashed */
#define MIME_CONDRV_CELL_UNDERLINE_SHIFT 6
#define MIME_CONDRV_CELL_OVERLINE 0x0400
#define MIME_CONDRV_CELL_REVERSE 0x4000
#define MIME_CONDRV_CELL_UNDERSCORE 0x8000 /* the single underline of the attribute word */

/* the colors are resolved and reverse video is left to the host. */
typedef struct MIME_CONDRV_HOST_CELL {
    ULONG Char; /* a code point, 0 for the trail half of a wide character */
    ULONG Foreground; /* 0x00BBGGRR */
    ULONG Background;
    USHORT Style;
    USHORT Flags;
} MIME_CONDRV_HOST_CELL;

#define MIME_CONDRV_WINDOW_RESIZE 1 /* the user made the window Columns by Rows cells */
#define MIME_CONDRV_WINDOW_SCROLL 2 /* the viewport moves to Left and Top */
typedef struct MIME_CONDRV_HOST_WINDOW {
    ULONG Op;
    LONG Columns, Rows;
    LONG Left, Top;
} MIME_CONDRV_HOST_WINDOW;

typedef struct MIME_CONDRV_FONT_SET {
    MIME_CONDRV_COORD Size;    /* the cell, in pixels */
    ULONG Weight;
    ULONG Family;
    ULONG MaxColumns, MaxRows; /* the largest window the screen has room for */
    WCHAR Face[32];
} MIME_CONDRV_FONT_SET;

typedef struct MIME_CONDRV_HOST_SELECTION {
    ULONG Flags; /* CONSOLE_SELECTION_IN_PROGRESS and the others of GetConsoleSelectionInfo */
    MIME_CONDRV_COORD Anchor;
    MIME_CONDRV_RECT Rect;
} MIME_CONDRV_HOST_SELECTION;

/* the choices of the properties dialog that belong to the console. */
typedef struct MIME_CONDRV_HOST_SETTINGS {
    ULONG Set;      /* bit 0 the colors and attributes, bit 1 the quick edit and insert modes, bit 2 the history, bit 3 the size */
    ULONG ColorTable[16];
    USHORT Attributes, PopupAttributes;
    ULONG QuickEdit, Insert;
    ULONG HistoryBufferSize, HistoryBuffers, HistoryFlags;
    MIME_CONDRV_COORD Size;
} MIME_CONDRV_HOST_SETTINGS;

#endif
