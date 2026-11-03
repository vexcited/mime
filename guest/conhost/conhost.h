/*
 * conhost.exe: the window of a console. the console driver keeps the screen buffers, the input and the programs; this
 * program draws the part of a buffer a window shows, turns the keyboard and the mouse into input records, and holds the
 * selection, the scroll bar and the clipboard. it learns what happened to the console from a handle to the driver, see
 * mime/condrv.h, and ends when the last program of the console is gone.
 */
#ifndef MIME_CONHOST_H
#define MIME_CONHOST_H

#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/wingdi.h>
#include <mime/ntapi.h>
#include <mime/condrv.h>

#define CONSOLE_CLASS L"ConsoleWindowClass"

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#define ENABLE_MOUSE_INPUT 0x0010
#define ENABLE_WINDOW_INPUT 0x0008

typedef MIME_CONDRV_HOST_CELL Cell;

/* the selection, in cells of the buffer. a stream runs from the anchor to the end through the rows between, a block is
 * the rectangle they span. */
typedef struct Selection {
    BOOL Active;   /* something is selected or being selected */
    BOOL Dragging; /* the mouse button is down */
    BOOL Block;
    POINT Anchor;
    POINT End;
} Selection;

typedef struct Host {
    HANDLE Device;
    HANDLE Event; /* set by the console while it has something to tell */
    HWND Window;
    ULONG64 Console;
    MIME_CONDRV_HOST_STATE State;
    Cell *View; /* the cells of the window, State.Window columns by rows */
    int ViewCols, ViewRows;
    int ViewLeft, ViewTop; /* where in the buffer the cells in View are from */
    int CellWidth, CellHeight;
    int Dpi;
    BOOL Focused;
    BOOL CursorOn;       /* the blink phase */
    BOOL Sizing;         /* the window is being moved to the size the console asked for */
    BOOL UserSizing;     /* the user holds a border */
    BOOL Maximized, Minimized;
    Selection Select;
    HDC BackDc;          /* the cells as drawn, so a repaint is a copy */
    HBITMAP BackBitmap;
    HBITMAP BackOld;
    int BackWidth, BackHeight;
    ULONG LastScrolls;
    BOOL Closing;
} Host;

extern Host g;

/* main.c */
BOOL HostControl(ULONG code, void *in, ULONG in_size, void *out, ULONG out_size);
void HostUpdate(void);
void HostSendRecords(const MIME_CONDRV_RECORD *records, ULONG count);
void HostResize(int columns, int rows);
void HostScroll(int left, int top);
void HostClose(void);
void HostSetSelection(void);
void HostFetchRows(int top, int rows, int left, int width, Cell *out);
void HostFont(void);

/* window.c */
BOOL WindowCreate(void);
void WindowUpdate(const MIME_CONDRV_HOST_STATE *old, ULONG changed, BOOL first);
void WindowRefill(BOOL all);
void WindowRun(void);
void WindowPaste(void);
BOOL WindowCopy(void);
void WindowSelectAll(void);
void WindowClearSelection(void);

/* font.c */
BOOL FontOpen(int dpi);
void FontClose(void);
void FontDrawRun(HDC dc, int x, int y, const Cell *cells, int count, COLORREF fg, COLORREF bg, UINT style);
extern int g_font_cell_w, g_font_cell_h;
extern WCHAR g_font_face[32];
extern BOOL g_font_raster;

/* properties.c */
void PropertiesShow(HWND owner);

#endif
