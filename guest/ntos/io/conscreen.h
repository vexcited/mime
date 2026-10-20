/*
 * the screen buffers of a console and the terminal that shows them. the
 * console driver keeps what programs wrote as cells, as conhost does, and
 * this module turns what happens to them into the escape sequences a host
 * terminal needs to show the same: text and escape sequences programs write
 * are parsed and applied, calls on the cells are applied, and the terminal
 * is brought up to date with the bytes in `out`. answers that the terminal
 * would send back, such as the cursor position report, are in `reply`.
 *
 * the module needs nothing of the kernel, so a host program can test it.
 */
#ifndef MIME_CONSCREEN_H
#define MIME_CONSCREEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef MIME_KERNEL
#include "../../rtl/rtl.h"
#else
#include <string.h>
#endif

/* the attribute word of a character cell, as CHAR_INFO has it. */
#define CS_FOREGROUND_MASK 0x000F
#define CS_BACKGROUND_MASK 0x00F0
#define CS_FOREGROUND_INTENSITY 0x0008
#define CS_BACKGROUND_INTENSITY 0x0080
#define CS_LEADING_BYTE 0x0100
#define CS_TRAILING_BYTE 0x0200
#define CS_GRID_HORIZONTAL 0x0400
#define CS_GRID_LVERTICAL 0x0800
#define CS_GRID_RVERTICAL 0x1000
#define CS_REVERSE_VIDEO 0x4000
#define CS_UNDERSCORE 0x8000
#define CS_DBCS_MASK 0x0300
#define CS_DEFAULT_ATTRIBUTES 0x0007

/* the output mode of a screen buffer. */
#define CS_PROCESSED_OUTPUT 0x1
#define CS_WRAP_AT_EOL_OUTPUT 0x2
#define CS_VIRTUAL_TERMINAL_PROCESSING 0x4
#define CS_DISABLE_NEWLINE_AUTO_RETURN 0x8
#define CS_LVB_GRID_WORLDWIDE 0x10
#define CS_OUTPUT_MODE_MASK 0x1F

typedef struct CsCoord {
    int16_t X, Y;
} CsCoord;

/* a rectangle with its right and bottom edges inside, as SMALL_RECT. */
typedef struct CsRect {
    int16_t Left, Top, Right, Bottom;
} CsRect;

/* a character cell as CHAR_INFO has it. */
typedef struct CsCharInfo {
    uint16_t Char;
    uint16_t Attributes;
} CsCharInfo;

/* cell flags */
#define CS_CELL_LEAD 0x1  /* the left half of a wide character */
#define CS_CELL_TRAIL 0x2 /* the right half */
#define CS_CELL_PAIR 0x4  /* the halves hold the two units of a surrogate pair */

typedef struct CsCell {
    uint16_t Char;
    uint16_t Attr; /* legacy attributes incl. the line and reverse bits */
    uint16_t Ext;  /* index of the terminal's table of extended attributes, 0 for none */
    uint16_t Flags;
} CsCell;

/* how a color of an attribute is given: the terminal's default, an index of the 16 colors of a terminal, of its 256, or red, green and blue. */
enum { CS_COLOR_DEFAULT = 0, CS_COLOR_16 = 1, CS_COLOR_256 = 2, CS_COLOR_RGB = 3 };

/*
 * the flags of an attribute, as the console keeps them: the bits of an
 * attribute word that the console lets a program set are in the same place.
 */
#define CS_STYLE_BOLD 0x0001
#define CS_STYLE_ITALIC 0x0002
#define CS_STYLE_BLINK 0x0004
#define CS_STYLE_CONCEAL 0x0008
#define CS_STYLE_STRIKE 0x0010
#define CS_STYLE_FAINT 0x0020
#define CS_STYLE_UNDERLINE_SHIFT 6
#define CS_STYLE_UNDERLINE_MASK 0x01C0 /* 0 none, 1 single, 2 double, 3 curly, 4 dotted, 5 dashed */
#define CS_STYLE_OVERLINE 0x0400       /* the bit of COMMON_LVB_GRID_HORIZONTAL */
#define CS_STYLE_LVB_MASK 0xDC00       /* the flags an attribute word can carry */

typedef struct CsExt {
    uint8_t FgKind, BgKind;
    uint16_t Style;
    uint32_t Fg, Bg; /* an index, or 0x00BBGGRR when the kind is rgb */
} CsExt;

/* an attribute: the legacy word that reads of the cells report, and the full attribute it was made from. */
typedef struct CsAttr {
    uint16_t Attr;
    uint16_t Ext;
} CsAttr;

typedef struct CsRow {
    CsCell *Cells;
    int16_t DirtyLeft, DirtyRight; /* the span the terminal has not been told, empty when Left is above Right */
    bool Wrapped;                  /* the text goes on in the next row */
    bool Force;                    /* paint the span even where it equals what the terminal shows */
    bool Touched;                  /* a program wrote here, so the row is more than what the terminal had before */
    bool Known;                    /* of the terminal's copy: the console has painted the whole row, so the cells are what it shows */
} CsRow;

typedef struct CsGrid {
    int Width, Height;
    CsRow *Rows;
} CsGrid;

struct CsTerminal;

/* the mouse and other modes a program sets with escape sequences, which decide what the terminal sends. */
#define CS_MODE_MOUSE_X10 0x1       /* 9 */
#define CS_MODE_MOUSE_BUTTON 0x2    /* 1000 */
#define CS_MODE_MOUSE_DRAG 0x4      /* 1002 */
#define CS_MODE_MOUSE_ANY 0x8       /* 1003 */
#define CS_MODE_MOUSE_SGR 0x10      /* 1006 */
#define CS_MODE_BRACKETED_PASTE 0x20 /* 2004 */
#define CS_MODE_FOCUS 0x40          /* 1004 */
#define CS_MODE_APP_CURSOR 0x80     /* 1 */
#define CS_MODE_APP_KEYPAD 0x100    /* ESC = */
#define CS_MODE_WIN32_INPUT 0x200   /* 9001 */
#define CS_MODE_MOUSE_URXVT 0x400   /* 1015 */
#define CS_MODE_MOUSE_UTF8 0x800    /* 1005 */
#define CS_MODE_MOUSE_ALTSCROLL 0x1000 /* 1007 */

#define CS_MAX_PARAMS 32
#define CS_MAX_OSC 2048
#define CS_MAX_EXT 4096

typedef struct CsBuffer {
    struct CsTerminal *Term;
    CsGrid Grid;
    struct CsBuffer *Next;
    struct CsBuffer *Main; /* the buffer an alternate one belongs to */
    struct CsBuffer *Alt;  /* the alternate buffer a sequence made while this one is shown */
    unsigned Refs;         /* handles and the like, kept by the driver */
    bool Detached;         /* closed while shown, gone when it is not */

    int CursorX, CursorY; /* in the buffer */
    bool WrapPending;     /* the last character took the last column and the cursor has not moved on */
    int CursorSize;       /* percent of a cell, 1 to 100 */
    bool CursorVisible;
    CsRect Window; /* the viewport, in the buffer */
    CsCoord MaxWindow;

    CsAttr Attr;        /* attributes of the text written */
    CsAttr DefaultAttr; /* what an erase and ESC [ 0 m return to */
    uint16_t PopupAttr;
    uint32_t ColorTable[16]; /* 0x00BBGGRR */
    uint32_t Mode;           /* the output mode: CS_..._OUTPUT and so on */

    /* escape sequence state */
    int Top, Bottom; /* scroll margins, viewport rows */
    bool OriginMode;
    bool InsertMode;
    bool NewlineMode;
    bool ReverseScreen;
    bool EraseDefault; /* erasing and scrolling fill with the default attributes, not the colors of the text */
    bool Charset0Graphics, Charset1Graphics;
    bool ShiftOut; /* the G1 set is in use */
    bool SavedValid;
    int SavedX, SavedY; /* viewport relative */
    CsAttr SavedAttr;
    bool SavedWrapPending, SavedOrigin, SavedG0, SavedG1, SavedShiftOut;
    uint8_t *TabStops; /* one byte per column, 1 for a stop */
    int ParserState;
    int ParserParams[CS_MAX_PARAMS];
    int ParserParamCount;
    uint8_t ParserParamSub[CS_MAX_PARAMS]; /* the value was after a colon */
    char ParserPrefix;                     /* ? < = > */
    char ParserIntermediate;
    char ParserIntermediate2;
    bool ParserHasParam;
    char *Osc;
    int OscLength;
    int OscCode;
    uint16_t HighSurrogate; /* a pair split between two writes */
    uint32_t LastChar;      /* for REP */
    int LastWidth;
    bool Dirty; /* cells changed that the terminal has not been told */
    int DirtyTop, DirtyBottom; /* viewport rows */
    unsigned Scrolls; /* rows that scrolled off the top of the buffer, for whoever tracks positions in it */
} CsBuffer;

/* what the terminal needs from the driver. the module calls these. */
void *CsAllocate(size_t size);
void CsFree(void *memory);

typedef struct CsTerminal {
    int Cols, Rows; /* the window of the host terminal */
    CsBuffer *Buffers;
    CsBuffer *Shown; /* the buffer the terminal shows, a main or created one */
    CsBuffer *Main;  /* the buffer of the screen the host terminal had before: others are shown in its alternate screen */

    CsExt *Ext;
    unsigned ExtCount, ExtCap;

    /* what the host terminal is believed to show: the viewport of the shown buffer */
    CsGrid Shadow;
    bool ShadowValid;
    int HostX, HostY;
    bool HostKnown;
    bool HostWrapPending;
    bool HostWrapMode;
    bool HostCursorVisible;
    bool HostAlt;
    CsAttr HostAttr;
    bool HostAttrKnown;
    uint32_t HostModes;   /* the CS_MODE_ bits sent to the terminal */
    uint32_t Modes;       /* the CS_MODE_ bits the programs asked for */
    uint32_t ApiModes;    /* the ones the input mode of the console asks for: mouse input */
    int HostCursorStyle;
    int PendingScroll;

    uint8_t *Out;
    size_t OutLength, OutCap;
    bool OutFailed;
    uint8_t *Reply;
    size_t ReplyLength, ReplyCap;

    /* a terminal that is no terminal: nothing is sent, the cells are kept */
    bool Headless;

    uint16_t Title[260];
    unsigned TitleLength;
    unsigned Changed; /* CS_CHANGED_ bits for the driver to look at */
    unsigned BellCount;
    uint32_t Palette[16]; /* the table colors are shown with, as the host knows them */

    bool Passthrough;
    bool OemGlyphs; /* the output code page has a glyph for each control character */
    bool TerminalSized; /* the buffer and the window are one size, as in a console that is a terminal's */
    bool ResizableHost; /* the terminal can be asked to change its size */
} CsTerminal;

#define CS_CHANGED_TITLE 0x1
#define CS_CHANGED_MODES 0x2
#define CS_CHANGED_BELL 0x4

/* ---------------------------------------------------------------- terminal */

bool CsTerminalInit(CsTerminal *term, int cols, int rows);
void CsTerminalDestroy(CsTerminal *term);

/* the host terminal changed size. */
void CsTerminalResize(CsTerminal *term, int cols, int rows);

/* takes the bytes for the host terminal; the pointer is valid until the next call into the module. */
const uint8_t *CsTakeOut(CsTerminal *term, size_t *length);
const uint8_t *CsTakeReply(CsTerminal *term, size_t *length);

/* bytes sent to the host terminal as they are, which the module cannot know the effect of. */
void CsEmit(CsTerminal *term, const char *text);

/* the host terminal shows a blank screen. */
void CsShadowBlank(CsTerminal *term);

/* brings the host terminal up to date with the cells; called after every call that changed them. */
void CsRender(CsTerminal *term);

/* paints the whole viewport again, for a terminal that knows nothing of what is shown. */
void CsRepaint(CsTerminal *term);

/* what to leave a host terminal as before it is given back: modes and screen set by programs. */
void CsRestoreHost(CsTerminal *term);

/* ---------------------------------------------------------------- buffers */

CsBuffer *CsBufferCreate(CsTerminal *term, int width, int height);
void CsBufferDestroy(CsBuffer *buffer);

/* the buffer whose cells the terminal shows: the alternate one while a program has one. */
CsBuffer *CsDisplayed(const CsTerminal *term);

/* makes a buffer the one the terminal shows. */
void CsShow(CsTerminal *term, CsBuffer *buffer);

/* the buffer calls act on: the alternate one while a program has one. */
CsBuffer *CsEffective(CsBuffer *buffer);

/* the output of the program: UTF-16 text with escape sequences and controls. */
void CsWrite(CsBuffer *buffer, const uint16_t *text, size_t count);

/* forgets an escape sequence that was cut off. */
void CsParserReset(CsBuffer *buffer);

/* text as a program without escape sequences writes it, whatever the mode: for what the console shows of its own. */
void CsPutLegacy(CsBuffer *buffer, const uint16_t *text, size_t count);

/* ---------------------------------------------------------------- the calls */

typedef enum CsResult {
    CS_OK = 0,
    CS_INVALID_PARAMETER,
    CS_NO_MEMORY,
} CsResult;

typedef struct CsInfo {
    CsCoord Size;
    CsCoord Cursor;
    uint16_t Attributes;
    CsRect Window;
    CsCoord MaxWindow;
    uint16_t PopupAttributes;
    uint32_t ColorTable[16];
    int CursorSize;
    bool CursorVisible;
} CsInfo;

void CsGetInfo(CsBuffer *buffer, CsInfo *info);
CsResult CsSetInfo(CsBuffer *buffer, const CsInfo *info);
CsResult CsSetSize(CsBuffer *buffer, CsCoord size);
CsResult CsResizeBuffer(CsBuffer *buffer, int width, int height);
CsResult CsSetCursorPosition(CsBuffer *buffer, CsCoord position);
CsResult CsSetCursorInfo(CsBuffer *buffer, int size, bool visible);
CsResult CsSetWindow(CsBuffer *buffer, bool absolute, CsRect window);
void CsSetAttributes(CsBuffer *buffer, uint16_t attributes);

/*
 * the rectangle is clipped to the buffer; the data have the width of the
 * rectangle as it was asked for and the cells clipped from the left and top
 * are left out of them. the clipped rectangle is stored back.
 */
CsResult CsReadOutput(CsBuffer *buffer, CsCharInfo *data, CsRect *rect);
CsResult CsWriteOutput(CsBuffer *buffer, const CsCharInfo *data, CsRect *rect);

size_t CsReadChars(CsBuffer *buffer, CsCoord start, uint16_t *out, size_t count);
size_t CsReadAttributes(CsBuffer *buffer, CsCoord start, uint16_t *out, size_t count);
CsResult CsWriteChars(CsBuffer *buffer, CsCoord start, const uint16_t *text, size_t count, size_t *written);
CsResult CsWriteAttributes(CsBuffer *buffer, CsCoord start, const uint16_t *attributes, size_t count, size_t *written);
size_t CsFillChars(CsBuffer *buffer, CsCoord start, uint16_t ch, size_t count);
size_t CsFillAttributes(CsBuffer *buffer, CsCoord start, uint16_t attribute, size_t count);

CsResult CsScroll(CsBuffer *buffer, CsRect source, const CsRect *clip, CsCoord destination, CsCharInfo fill);

/* the resolved color of a legacy index for the table of the buffer. */
uint32_t CsColor(const CsBuffer *buffer, unsigned index);

/* the title of the terminal's window: kept, and sent as the escape sequence that sets it. */
void CsSetTitle(CsTerminal *term, const uint16_t *title, size_t count);

/* the terminal's cursor is known to be here, for a console that starts below what the terminal shows. */
void CsSetHostCursor(CsTerminal *term, int x, int y);

/* the width a character takes: 0, 1 or 2 columns. */
int CsCharWidth(uint32_t codepoint);

/* the sequences a key stands for are made by the input side; the modes decide which. */
uint32_t CsInputModes(const CsTerminal *term);

#endif
