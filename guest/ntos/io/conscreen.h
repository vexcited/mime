/*
 * console screen buffers and the terminal that shows them. the driver keeps program output as cells, as conhost
 * does, and this module turns changes to them into the escape sequences a host terminal needs: program text and
 * sequences are parsed and applied, cell calls are applied, and the terminal is updated with the bytes in `out`.
 * answers a terminal would send back, such as cursor position reports, go to `reply`.
 *
 * the module does not depend on the kernel.
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

/* the attribute word of a CHAR_INFO cell. */
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

#define CS_PROCESSED_OUTPUT 0x1
#define CS_WRAP_AT_EOL_OUTPUT 0x2
#define CS_VIRTUAL_TERMINAL_PROCESSING 0x4
#define CS_DISABLE_NEWLINE_AUTO_RETURN 0x8
#define CS_LVB_GRID_WORLDWIDE 0x10
#define CS_OUTPUT_MODE_MASK 0x1F

typedef struct CsCoord {
    int16_t X, Y;
} CsCoord;

/* right and bottom edges are inclusive, as in SMALL_RECT. */
typedef struct CsRect {
    int16_t Left, Top, Right, Bottom;
} CsRect;

/* same layout as CHAR_INFO. */
typedef struct CsCharInfo {
    uint16_t Char;
    uint16_t Attributes;
} CsCharInfo;

#define CS_CELL_LEAD 0x1  /* the left half of a wide character. */
#define CS_CELL_TRAIL 0x2 /* the right half. */
#define CS_CELL_PAIR 0x4  /* the halves hold the two units of a surrogate pair. */

typedef struct CsCell {
    uint16_t Char;
    uint16_t Attr; /* legacy attributes, including the line and reverse bits. */
    uint16_t Ext;  /* index into the terminal's extended attribute table, 0 for none. */
    uint16_t Flags;
} CsCell;

/* a color is the terminal default, one of the 16 or 256 terminal colors, or rgb. */
enum { CS_COLOR_DEFAULT = 0, CS_COLOR_16 = 1, CS_COLOR_256 = 2, CS_COLOR_RGB = 3 };

/* attribute flags. the ones a program can set through the attribute word sit at the same bits. */
#define CS_STYLE_BOLD 0x0001
#define CS_STYLE_ITALIC 0x0002
#define CS_STYLE_BLINK 0x0004
#define CS_STYLE_CONCEAL 0x0008
#define CS_STYLE_STRIKE 0x0010
#define CS_STYLE_FAINT 0x0020
#define CS_STYLE_UNDERLINE_SHIFT 6
#define CS_STYLE_UNDERLINE_MASK 0x01C0 /* 0 none, 1 single, 2 double, 3 curly, 4 dotted, 5 dashed. */
#define CS_STYLE_OVERLINE 0x0400       /* COMMON_LVB_GRID_HORIZONTAL. */
#define CS_STYLE_LVB_MASK 0xDC00       /* the flags an attribute word can carry. */

typedef struct CsExt {
    uint8_t FgKind, BgKind;
    uint16_t Style;
    uint32_t Fg, Bg; /* an index, or 0x00BBGGRR for rgb. */
} CsExt;

/* the legacy word that cell reads report, and the full attribute it came from. */
typedef struct CsAttr {
    uint16_t Attr;
    uint16_t Ext;
} CsAttr;

typedef struct CsRow {
    CsCell *Cells;
    int16_t DirtyLeft, DirtyRight; /* the span not yet sent, empty when Left > Right. */
    bool Wrapped;                  /* the text continues on the next row. */
    bool Force;                    /* paint the span even where it matches the terminal. */
    bool Touched;                  /* a program wrote here, so the row holds more than the terminal had before. */
    bool Known;                    /* shadow rows: the console painted the whole row, so it matches the terminal. */
} CsRow;

typedef struct CsGrid {
    int Width, Height;
    CsRow *Rows;
} CsGrid;

struct CsTerminal;

/* modes programs set with escape sequences, which decide what the terminal sends. comments give the mode numbers. */
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
#define CS_MAX_EXT 60000

typedef struct CsBuffer {
    struct CsTerminal *Term;
    CsGrid Grid;
    struct CsBuffer *Next;
    struct CsBuffer *Main; /* the buffer an alternate one belongs to. */
    struct CsBuffer *Alt;  /* the alternate buffer a sequence made while this one is shown. */
    unsigned Refs;         /* handles and such, kept by the driver. */
    bool Detached;         /* closed while shown, freed once it is not. */

    int CursorX, CursorY; /* buffer coordinates. */
    bool WrapPending;     /* the last character filled the last column and the cursor has not moved on. */
    int CursorSize;       /* percent of a cell, 1 to 100. */
    bool CursorVisible;
    CsRect Window; /* the viewport, in buffer coordinates. */
    CsCoord MaxWindow;

    CsAttr Attr;        /* attributes of written text. */
    CsAttr DefaultAttr; /* what an erase and ESC [ 0 m return to. */
    uint16_t PopupAttr;
    uint32_t ColorTable[16]; /* 0x00BBGGRR. */
    uint32_t Mode;           /* the CS_..._OUTPUT mode bits. */

    /* escape sequence state. */
    int Top, Bottom; /* scroll margins, viewport rows. */
    bool OriginMode;
    bool InsertMode;
    bool NewlineMode;
    bool ReverseScreen;
    bool EraseDefault; /* erasing and scrolling fill with the default attributes, not the text colors. */
    bool Charset0Graphics, Charset1Graphics;
    bool ShiftOut; /* the G1 set is in use. */
    bool SavedValid;
    int SavedX, SavedY; /* viewport relative. */
    CsAttr SavedAttr;
    bool SavedWrapPending, SavedOrigin, SavedG0, SavedG1, SavedShiftOut;
    uint8_t *TabStops; /* one byte per column, 1 for a stop. */
    int ParserState;
    int ParserParams[CS_MAX_PARAMS];
    int ParserParamCount;
    uint8_t ParserParamSub[CS_MAX_PARAMS]; /* the value came after a colon. */
    char ParserPrefix;                     /* one of ? < = >. */
    char ParserIntermediate;
    char ParserIntermediate2;
    bool ParserHasParam;
    char *Osc;
    int OscLength;
    int OscCode;
    uint16_t HighSurrogate; /* a pair split between two writes. */
    uint32_t LastChar;      /* for REP. */
    int LastWidth;
    bool Dirty; /* cells changed that the terminal has not been sent. */
    int DirtyTop, DirtyBottom; /* viewport rows. */
    unsigned Scrolls; /* rows scrolled off the top, for whoever tracks positions in the buffer. */
} CsBuffer;

/* the driver provides these. */
void *CsAllocate(size_t size);
void CsFree(void *memory);

typedef struct CsTerminal {
    int Cols, Rows; /* the host terminal window. */
    CsBuffer *Buffers;
    CsBuffer *Shown; /* the buffer the terminal shows, a main or created one. */
    CsBuffer *Main;  /* the host terminal's original screen, the others are shown in its alternate screen. */

    CsExt *Ext;
    unsigned ExtCount, ExtCap;
    uint16_t *ExtHash; /* attribute index plus one, by hash of its fields. the size is a power of two. */
    unsigned ExtHashSize;

    /* what the host terminal is believed to show: the viewport of the shown buffer. */
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
    uint32_t HostModes;   /* the CS_MODE_ bits sent to the terminal. */
    uint32_t Modes;       /* the CS_MODE_ bits programs asked for. */
    uint32_t ApiModes;    /* the mouse input bits the console input mode asks for. */
    int HostCursorStyle;
    int PendingScroll;

    uint8_t *Out;
    size_t OutLength, OutCap;
    bool OutFailed;
    uint8_t *Reply;
    size_t ReplyLength, ReplyCap;

    /* no real terminal: nothing is sent, the cells are kept. */
    bool Headless;

    uint16_t Title[260];
    unsigned TitleLength;
    unsigned Changed; /* CS_CHANGED_ bits for the driver. */
    unsigned BellCount;
    uint32_t Palette[16]; /* the colors the host shows the table with. */

    bool Passthrough;
    bool OemGlyphs; /* the output code page has a glyph for each control character. */
    bool TerminalSized; /* buffer and window have one size, as in a console that belongs to a terminal. */
    bool ResizableHost; /* the terminal can be asked to resize. */

    /* a console window: the buffer is bigger than the window, which Cols and Rows then count. */
    bool Windowed;
    int MaxCols, MaxRows; /* the largest window the screen has room for, 0 when not known. */
    uint64_t Generation;  /* counts the calls that changed what the window shows. */
} CsTerminal;

#define CS_CHANGED_TITLE 0x1
#define CS_CHANGED_MODES 0x2
#define CS_CHANGED_BELL 0x4
#define CS_CHANGED_WINDOW 0x8 /* a program changed the size of the window or the buffer */

/* ---------------------------------------------------------------- terminal */

bool CsTerminalInit(CsTerminal *term, int cols, int rows);
void CsTerminalDestroy(CsTerminal *term);

void CsTerminalResize(CsTerminal *term, int cols, int rows);

/* makes the terminal a console window: nothing is sent, the window is part of a bigger buffer. */
void CsTerminalSetWindowed(CsTerminal *term, int max_cols, int max_rows);

/*
 * the user made the window cols by rows cells. the main buffer takes the new width and its text flows to it, the
 * alternate screen is the window, any other buffer grows to hold the window.
 */
void CsWindowResize(CsTerminal *term, int cols, int rows);

/* the viewport of the shown buffer moves, as far as the buffer allows. */
void CsWindowScroll(CsTerminal *term, int left, int top);

/* one cell as the window draws it. */
typedef struct CsDrawCell {
    uint32_t Char; /* 0 for the trail half of a wide character */
    uint32_t Foreground, Background; /* 0x00BBGGRR */
    uint16_t Style;
    uint16_t Flags; /* CS_DRAW_ */
} CsDrawCell;
#define CS_DRAW_LEAD 0x1
#define CS_DRAW_TRAIL 0x2
#define CS_DRAW_WRAPPED 0x4 /* on the last cell of a row whose text goes on in the next one */
void CsDrawRow(CsBuffer *buffer, int y, int left, int count, CsDrawCell *out);

/* the colors a new buffer starts with, which also fill it. */
void CsSetDefaultAttributes(CsBuffer *buffer, uint16_t attributes);

/* the pointer is valid until the next call into the module. */
const uint8_t *CsTakeOut(CsTerminal *term, size_t *length);
const uint8_t *CsTakeReply(CsTerminal *term, size_t *length);

/* raw bytes for the host terminal, whose effect the module cannot know. */
void CsEmit(CsTerminal *term, const char *text);

/* records that the host terminal shows a blank screen. */
void CsShadowBlank(CsTerminal *term);

/* call after every call that changed the cells. */
void CsRender(CsTerminal *term);

/* repaints the whole viewport, for a terminal whose contents are unknown. */
void CsRepaint(CsTerminal *term);

/* undoes the modes and screen programs set before the host terminal is given back. */
void CsRestoreHost(CsTerminal *term);

/* ---------------------------------------------------------------- buffers */

CsBuffer *CsBufferCreate(CsTerminal *term, int width, int height);
void CsBufferDestroy(CsBuffer *buffer);

/* the buffer the terminal shows: the alternate one while a program has one. */
CsBuffer *CsDisplayed(const CsTerminal *term);

void CsShow(CsTerminal *term, CsBuffer *buffer);

/* the buffer calls act on: the alternate one while a program has one. */
CsBuffer *CsEffective(CsBuffer *buffer);

/* program output: UTF-16 text with escape sequences and controls. */
void CsWrite(CsBuffer *buffer, const uint16_t *text, size_t count);

void CsParserReset(CsBuffer *buffer);

/* writes text without escape sequence handling whatever the mode, for the console's own output. */
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
 * the rectangle is clipped to the buffer and stored back. data keeps the width of the requested rectangle, and the
 * cells clipped from the left and top are left untouched in it.
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

/* the color a legacy index maps to in the buffer's table. */
uint32_t CsColor(const CsBuffer *buffer, unsigned index);

/* stores the window title and sends the escape sequence that sets it. */
void CsSetTitle(CsTerminal *term, const uint16_t *title, size_t count);

/* tells where the terminal cursor is, for a console that starts below what the terminal shows. */
void CsSetHostCursor(CsTerminal *term, int x, int y);

/* 0, 1 or 2 columns. */
int CsCharWidth(uint32_t codepoint);

/* the input side builds key sequences, these modes decide which. */
uint32_t CsInputModes(const CsTerminal *term);

#endif
