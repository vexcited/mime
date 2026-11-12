/*
 * the private protocol between user32, gdi32 and win32k.sys: service prototypes, multiplexer routine numbers and
 * kernel to user callbacks. services keep their windows names and numbers, with parameters close to the real ones
 * where known; the NtUserCall* multiplexers and NtGdiGetAndSetDCDword use mime's own routine numbers.
 */
#ifndef MIME_NTUSER_H
#define MIME_NTUSER_H

#include <mime/win32/winuser.h>

#ifndef W32KAPI
#ifdef MIME_BUILD_WIN32K
#define W32KAPI
#else
#define W32KAPI DECLSPEC_IMPORT
#endif
#endif

/* window manager errors missing from winerror.h. */
#ifndef ERROR_CLIPBOARD_NOT_OPEN
#define ERROR_CLIPBOARD_NOT_OPEN 1418L
#endif
#ifndef ERROR_MENU_ITEM_NOT_FOUND
#define ERROR_MENU_ITEM_NOT_FOUND 1456L
#endif
#ifndef ERROR_POPUP_ALREADY_ACTIVE
#define ERROR_POPUP_ALREADY_ACTIVE 1446L
#endif
#ifndef ERROR_CHILD_WINDOW_MENU
#define ERROR_CHILD_WINDOW_MENU 1436L
#endif
#ifndef ERROR_INCORRECT_SIZE
#define ERROR_INCORRECT_SIZE 1462L
#endif
#ifndef ERROR_INVALID_PIXEL_FORMAT
#define ERROR_INVALID_PIXEL_FORMAT 2000L
#endif

/* ------------------------------------------------------------------ kernel callbacks */

/* indices into PEB->KernelCallbackTable, which user32 fills. */
enum {
    MIME_CB_WINDOWPROC = 0,  /* MIME_WINDOWPROC_ARGS -> LRESULT, the output may carry updated lparam data. */
    MIME_CB_TIMERPROC = 1,   /* MIME_TIMERPROC_ARGS */
    MIME_CB_LOADSYSTEM = 2,  /* reserved. */
    MIME_CB_HOOKPROC = 3,    /* MIME_HOOKPROC_ARGS -> LRESULT, the output carries the updated lparam data. */
    MIME_CB_SENDASYNC = 4,   /* MIME_SENDASYNC_ARGS, the result of a SendMessageCallback. */
    MIME_CB_COUNT = 8,
};

/* how a callback's lparam was marshaled into the argument buffer. */
enum {
    MIME_LPARAM_VALUE = 0,       /* passed through unchanged. */
    MIME_LPARAM_IN = 1,          /* points to Data, read only. */
    MIME_LPARAM_INOUT = 2,       /* points to Data, copied back to the kernel on return. */
    MIME_LPARAM_CREATESTRUCT = 3, /* CREATESTRUCTW followed by the name and class strings. */
    MIME_LPARAM_PTR2 = 4,         /* Data holds two DWORDs, wparam and lparam point at them. */
    MIME_LPARAM_WPTR = 5          /* Data holds one DWORD, wparam points at it and lparam passes through. */
};

/*
 * a block too large for the callback stack is not in Data: the kernel put it in the receiver's memory, Lparam
 * points at it and DataSize is its size. the result then carries no data, the kernel reads the block itself.
 */
#define MIME_LPARAM_EXTERNAL 0x100
#define MIME_LPARAM_KIND(kind) ((kind) & 0xFF)

/*
 * in MIME_LPARAM_CREATESTRUCT data, lpszName and lpszClass are byte offsets from the start of Data, 0 for NULL.
 * for WM_NCCALCSIZE with wparam TRUE, the data is NCCALCSIZE_PARAMS followed by its WINDOWPOS, and lppos must be
 * pointed at it.
 */

typedef struct MIME_WINDOWPROC_ARGS {
    WNDPROC Proc;
    HWND Hwnd;
    UINT Msg;
    UINT LparamKind;
    WPARAM Wparam;
    LPARAM Lparam;
    ULONG Ansi;      /* the procedure expects ansi messages. */
    ULONG DataSize;
    UCHAR Data[];    /* marshaled lparam, 16 byte aligned. */
} MIME_WINDOWPROC_ARGS;

typedef struct MIME_CALLBACK_RESULT {
    LRESULT Result;
    ULONG DataSize; /* updated Data for MIME_LPARAM_INOUT. */
    ULONG Pad;
    UCHAR Data[];
} MIME_CALLBACK_RESULT;

typedef struct MIME_TIMERPROC_ARGS {
    TIMERPROC Proc;
    HWND Hwnd;
    UINT Msg;
    UINT_PTR Id;
    DWORD Time;
} MIME_TIMERPROC_ARGS;

/* runs in the thread that sent the message. */
typedef struct MIME_SENDASYNC_ARGS {
    SENDASYNCPROC Proc;
    HWND Hwnd;
    UINT Msg;
    ULONG_PTR Data;
    LRESULT Result;
} MIME_SENDASYNC_ARGS;

/*
 * with a DataSize, lparam points to Data, a copy of the hook structure. for HCBT_CREATEWND it is CBT_CREATEWNDW, then
 * the CREATESTRUCTW it points to, then its strings: lpcs is 0 and lpszName and lpszClass are byte offsets from the
 * CREATESTRUCTW, 0 for NULL.
 * for WH_CALLWNDPROC and WH_CALLWNDPROCRET it is a CWPSTRUCT or CWPRETSTRUCT of the receiver's size, and after it,
 * when its lparam points at data, a copy of that data, which the hook's lparam field points at.
 */
typedef struct MIME_HOOKPROC_ARGS {
    HOOKPROC Proc;
    int Id;
    int Code;
    WPARAM Wparam;
    LPARAM Lparam;
    ULONG Ansi;
    ULONG DataSize;
    UCHAR Data[];
} MIME_HOOKPROC_ARGS;

/* ------------------------------------------------------------------ client registration */

/* window procedures user32 implements for the system classes. */
enum {
    MIME_FNID_BUTTON,
    MIME_FNID_STATIC,
    MIME_FNID_EDIT,
    MIME_FNID_LISTBOX,
    MIME_FNID_COMBOBOX,
    MIME_FNID_SCROLLBAR,
    MIME_FNID_DIALOG,
    MIME_FNID_MESSAGE,
    MIME_FNID_DESKTOP,
    MIME_FNID_MENU,
    MIME_FNID_COMBOLBOX,
    MIME_FNID_COUNT
};

typedef struct MIME_CLIENT_INFO {
    ULONG Size;
    ULONG Flags;  /* MIME_CLIENT_* */
    HINSTANCE User32;
    WNDPROC Procs[MIME_FNID_COUNT];
    WNDPROC DefWindowProcW;
} MIME_CLIENT_INFO;

/* the program is 32-bit: win32k gives it callbacks in the 64-bit layouts, which its wow64 layer converts. */
#define MIME_CLIENT_WOW 0x1

/* NtUserRegisterClassExWOW flags. */
#define MIME_CLASS_ANSI 0x1
#define MIME_CLASS_SYSTEM 0x2

/* ------------------------------------------------------------------ multiplexers */

enum {
    MIME_NOPARAM_GETDESKTOPWINDOW = 1,
    MIME_NOPARAM_GETFOCUS,
    MIME_NOPARAM_GETACTIVEWINDOW,
    MIME_NOPARAM_GETCAPTURE,
    MIME_NOPARAM_RELEASECAPTURE,
    MIME_NOPARAM_GETMESSAGETIME,
    MIME_NOPARAM_GETMESSAGEEXTRAINFO,
    MIME_NOPARAM_GETDPIFORSYSTEM,
    MIME_NOPARAM_GETDIALOGBASEUNITS,
    MIME_NOPARAM_GETLASTINPUTTIME,
    MIME_NOPARAM_INSENDMESSAGE,
    MIME_NOPARAM_GETSYSTEMDPI,          /* the physical dpi of the system, whatever the thread is aware of. */
    MIME_NOPARAM_GETINPUTSOURCE,        /* IMDT_* | IMO_* << 8 of the input message the thread retrieved last. */
};

enum {
    MIME_ONEPARAM_GETSYSCOLOR = 1,
    MIME_ONEPARAM_GETSYSCOLORBRUSH,
    MIME_ONEPARAM_MESSAGEBEEP,
    MIME_ONEPARAM_POSTQUITMESSAGE,
    MIME_ONEPARAM_GETQUEUESTATUS,
    MIME_ONEPARAM_SHOWCURSOR,
    MIME_ONEPARAM_SETPROCESSDPIAWARENESS,
    MIME_ONEPARAM_GETPROCESSDPIAWARENESS,
    MIME_ONEPARAM_REPLYMESSAGE,
    MIME_ONEPARAM_SETMESSAGEEXTRAINFO,  /* the new value -> the old one. */
    MIME_ONEPARAM_GETPROCESSDPISET,     /* whether the awareness was chosen. */
    MIME_ONEPARAM_SETTHREADDPICONTEXT,  /* an awareness plus one, 0 for the process' own -> the awareness in effect before. */
    MIME_ONEPARAM_GETTHREADDPICONTEXT,  /* -> the awareness in effect, 4 is unaware with gdi scaling. */
    MIME_ONEPARAM_SETTHREADDPIHOSTING,  /* 0 default or 1 mixed -> the behavior before, -1 when it is none. */
    MIME_ONEPARAM_GETTHREADDPIHOSTING,
    MIME_ONEPARAM_GETKEYBOARDLAYOUT,    /* a MimeKeyboardLayout pointer -> its generation, 0 for the US layout. */
};

enum {
    MIME_TWOPARAM_GETSYSTEMMETRICS = 1, /* index, dpi */
    MIME_TWOPARAM_SETCURSORPOS,
    MIME_TWOPARAM_MONITORFROMPOINT,     /* unused. */
    MIME_TWOPARAM_GETMONITORINFO,       /* monitor, MONITORINFO pointer */
    MIME_TWOPARAM_WINDOWFROMPOINT,      /* x, y */
    MIME_TWOPARAM_GETMONITORS,          /* MIME_MONITOR_INFO array, its capacity -> the number of monitors. */
    MIME_TWOPARAM_HOSTMOVESIZE,         /* window, 0 or a WMSZ_* edge: the host moves or sizes it under the pointer -> TRUE when it does. */
    MIME_TWOPARAM_CBTSYSCOMMAND,        /* SC_* command, its lparam -> TRUE when a WH_CBT hook prevents it. */
    MIME_TWOPARAM_CBTMOVESIZE,          /* window, RECT pointer of the place a move or size loop ends at. */
    MIME_TWOPARAM_ALTALONE,             /* 0, 0 -> TRUE when Alt or F10 went down and nothing else was pressed or clicked since. */
    MIME_TWOPARAM_DISPLAYUNIQUENESS,    /* 0, 0 -> the count of display setting changes, never 0. */
    MIME_TWOPARAM_REGISTERHOTKEY,       /* window or 0, ULONG[3] of the identifier, the MOD_* and the key -> TRUE, or 0 with the error. */
    MIME_TWOPARAM_UNREGISTERHOTKEY,     /* window or 0, the identifier -> TRUE, or 0 with the error. */
    MIME_TWOPARAM_THREADWINDOWS,        /* thread id or 0 for the foreground thread, ULONG[3] of its active, focus and capture window -> 1, 2 for the calling thread, or 0 with the error. */
};

/* what MIME_TWOPARAM_GETMONITORS returns per monitor. */
#define MIME_USER_MAX_MONITORS 8
#define MIME_MONITORINFOF_HDR 0x80000000u /* the monitor shows high dynamic range content now. */

/* CIE xy chromaticities in millionths and luminances in ten thousandths of a nit, all 0 when unknown. */
typedef struct MIME_MONITOR_COLOR {
    ULONG RedX, RedY, GreenX, GreenY, BlueX, BlueY, WhiteX, WhiteY;
    ULONG MinNits, MaxNits, MaxFrameNits, WhiteNits; /* WhiteNits: where sdr white shows while in hdr. */
} MIME_MONITOR_COLOR;

typedef struct MIME_MONITOR_INFO {
    ULONG Flags; /* MONITORINFOF_PRIMARY, MIME_MONITORINFOF_HDR */
    ULONG Dpi;   /* the display's, whatever the process is aware of. */
    ULONG RefreshHz;
    RECT Rect;
    RECT Work;
    WCHAR Device[32]; /* \\.\DISPLAYn */
    WCHAR Name[32];   /* the host's name for the monitor. */
    MIME_MONITOR_COLOR Color;
} MIME_MONITOR_INFO;

enum {
    MIME_HWND_GETPARENT = 1,
    MIME_HWND_ISWINDOW,
    MIME_HWND_ISVISIBLE,
    MIME_HWND_ISENABLED,
    MIME_HWND_GETDPI,
    MIME_HWND_SETFOREGROUND,
    MIME_HWND_GETTEXTLENGTH,
    MIME_HWND_UPDATEWINDOW,
    MIME_HWND_ISICONIC,
    MIME_HWND_ISZOOMED,
    MIME_HWND_MONITOR,
    MIME_HWND_ISUNICODE,
    MIME_HWND_GETDPIAWARENESS,          /* the awareness the window was created with, 4 is unaware with gdi scaling. */
    MIME_HWND_GETDPIHOSTING,            /* the hosting behavior its thread had when it was created. */
    MIME_HWND_GETWINDOWDPI,             /* the dpi the controls of the window work at, GetWindowDPI of windows. */
    MIME_HWND_ISCHILDDPIMESSAGE,        /* whether the children of the window hear about dpi changes. */
    MIME_HWND_ISDIALOG,                 /* whether the dialog manager made the window. */
    MIME_HWND_GETCONTROLDPIBEHAVIOR,    /* DCDC_* of a control. */
    MIME_HWND_PAINTDISPATCHED,          /* after user32 dispatched WM_PAINT itself: the frame of a window left unpainted. */
};

enum {
    MIME_HWNDPARAM_GETWINDOWLONG = 1,   /* index */
    MIME_HWNDPARAM_GETCLASSLONG,        /* index */
    MIME_HWNDPARAM_GETWINDOW,           /* GW_* */
    MIME_HWNDPARAM_GETWINDOWRECT,       /* RECT pointer */
    MIME_HWNDPARAM_GETCLIENTRECT,       /* RECT pointer */
    MIME_HWNDPARAM_ISCHILD,             /* parent */
    MIME_HWNDPARAM_GETDLGITEM,          /* id */
    MIME_HWNDPARAM_GETTHREADPROCESSID,  /* DWORD pointer for the pid, returns the tid. */
    MIME_HWNDPARAM_MAPPOINTS,           /* MIME_MAP_POINTS pointer */
    MIME_HWNDPARAM_GETWINDOWINFO,       /* WINDOWINFO pointer */
    MIME_HWNDPARAM_SETICON,             /* MIME_SET_ICON pointer */
    MIME_HWNDPARAM_GETICON,             /* ICON_BIG or ICON_SMALL */
    MIME_HWNDPARAM_SETDIALOGINFO,       /* nonzero marks the window as a dialog. */
    MIME_HWNDPARAM_ADJUSTRECT,          /* MIME_ADJUST_RECT pointer, uses the window dpi. */
    MIME_HWNDPARAM_CHILDFROMPOINT,      /* packed client point */
    MIME_HWNDPARAM_SETCONTEXTHELPID,
    MIME_HWNDPARAM_ENABLECLOSE,         /* nonzero enables SC_CLOSE, zero greys the close button. */
    MIME_HWNDPARAM_SETTOUCH,            /* MIME_TOUCH_REGISTERED and TWF_* flags, 0 unregisters. */
    MIME_HWNDPARAM_SYSTIMER,            /* MIME_SYS_TIMER pointer, an elapse of 0 kills the timer. */
    MIME_HWNDPARAM_SETAFFINITY,         /* WDA_*: how screen captures show the window. */
    MIME_HWNDPARAM_GETAFFINITY,
    MIME_HWNDPARAM_GETCLASSWORD,        /* byte offset into the class extra bytes -> the word there, or ERROR_INVALID_INDEX. */
    MIME_HWNDPARAM_MENUBAR,             /* MIME_MENUBAR pointer: the layout of the window's menu bar and the state it is drawn in. */
    MIME_HWNDPARAM_GETCONTEXTHELPID,
    MIME_HWNDPARAM_SETWINDOWWORD,       /* the word in the low half and the byte offset into the extra bytes above it -> the old word, or -1. */
    MIME_HWNDPARAM_DPICONVERT,          /* MIME_DPI_CONVERT pointer, the window may be NULL for the thread ones. */
    MIME_HWNDPARAM_ENABLENCDPI,         /* EnableNonClientDpiScaling, the parameter is unused. */
    MIME_HWNDPARAM_DPICHILDREN,         /* EnableChildWindowDpiMessage: nonzero for the children to hear, zero for not. */
    MIME_HWNDPARAM_CONTROLDPIBEHAVIOR,  /* the DCDC_* mask in the low byte and their values above it. */
    MIME_HWNDPARAM_PRESENTLAYER,        /* MIME_PRESENT_LAYER pointer: where the image the window presents lies in its top level window. */
    MIME_HWNDPARAM_GETTITLEBARINFO,     /* TITLEBARINFO or TITLEBARINFOEX pointer, told apart by cbSize. */
    MIME_HWNDPARAM_DIRECTPROC,          /* MIME_DIRECT_PROC pointer: whether user32 calls the procedure itself. */
};

/* what MIME_HWNDPARAM_DPICONVERT converts a point between. */
enum {
    MIME_DPI_WINDOW_TO_PHYSICAL = 1, /* the pixels of the window to the physical ones. */
    MIME_DPI_PHYSICAL_TO_WINDOW,
    MIME_DPI_THREAD_TO_PHYSICAL,     /* the pixels of the thread, fails when the point is not in the window. */
    MIME_DPI_PHYSICAL_TO_THREAD,
};

typedef struct MIME_DPI_CONVERT {
    POINT Point;
    ULONG Op;
} MIME_DPI_CONVERT;

/* what MIME_HWNDPARAM_MENUBAR asks, with only fields of the same size in both processors. */
enum {
    MIME_MENUBAR_RECT = 1, /* Item 0 the bar, 1 and up an item, the rectangle in screen coordinates -> TRUE when there is one. */
    MIME_MENUBAR_HIT,      /* Point in screen coordinates -> TRUE when it is on the bar, Item the item or -1. */
    MIME_MENUBAR_STATE,    /* Item the selected item, Pushed the item whose popup is open, both -1 for none, an Item of -2 the icon of the system menu: the frame is drawn again. */
    MIME_MENUBAR_AREA,     /* the bar and its line, screen coordinates. */
    MIME_MENUBAR_CUES,     /* Item nonzero: Alt is down and the bar shows its mnemonics, zero: it stops. */
};

typedef struct MIME_MENUBAR {
    ULONG Op;
    LONG Item;
    LONG Pushed;
    ULONG Menu;  /* out: the bar's menu handle. */
    ULONG Count; /* out: its items. */
    POINT Point;
    RECT Rect;
} MIME_MENUBAR;

typedef struct MIME_SYS_TIMER {
    UINT_PTR Id;
    UINT Elapse;
} MIME_SYS_TIMER;

#define MIME_TOUCH_REGISTERED 0x80000000u

/* NtUserMessageCall types. */
enum {
    MIME_MSGCALL_SEND = 0,           /* SendMessage */
    MIME_MSGCALL_DEFWINDOWPROC = 1,  /* default processing in win32k. */
    MIME_MSGCALL_SENDNOTIFY = 2,     /* SendNotifyMessage */
    MIME_MSGCALL_SENDTIMEOUT = 3,    /* result_info points to a MIME_SEND_TIMEOUT. */
    MIME_MSGCALL_SENDCALLBACK = 4,   /* SendMessageCallback, result_info points to a MIME_SEND_CALLBACK. */
    MIME_MSGCALL_BROADCAST = 5,      /* BroadcastSystemMessage, result_info points to a MIME_BROADCAST. */
};

typedef struct MIME_SEND_CALLBACK {
    SENDASYNCPROC Proc;
    ULONG_PTR Data;
} MIME_SEND_CALLBACK;

/* BroadcastSystemMessageEx arguments after user32 checked them, and what comes back. */
typedef struct MIME_BROADCAST {
    ULONG Recipients;  /* BSM_*, in and out. */
    ULONG Flags;       /* BSF_* */
    HWND Hwnd;         /* out: the window that denied a query. */
    HDESK Desktop;     /* out: its desktop. */
} MIME_BROADCAST;

typedef struct MIME_SEND_TIMEOUT {
    UINT Flags;
    UINT Timeout;
    ULONG_PTR Result;
} MIME_SEND_TIMEOUT;

typedef struct MIME_SET_ICON {
    UINT Which; /* ICON_BIG or ICON_SMALL */
    HICON Icon;
} MIME_SET_ICON;

/* what MIME_HWNDPARAM_DIRECTPROC asks of a message and answers, with fields of the same size in both processors. */
#define MIME_DIRECT_SEND 0x1 /* a send, which a WH_CALLWNDPROC or WH_CALLWNDPROCRET hook keeps in the kernel. */
#define MIME_DIRECT_ANSI 0x2 /* answered: the procedure takes ansi. */
typedef struct MIME_DIRECT_PROC {
    ULONG Msg;
    ULONG Flags;
    ULONG64 Proc;
} MIME_DIRECT_PROC;

typedef struct MIME_MAP_POINTS {
    HWND To;
    UINT Count;
    POINT *Points;
} MIME_MAP_POINTS;

typedef struct MIME_ADJUST_RECT {
    RECT Rect;
    DWORD Style;
    DWORD ExStyle;
    BOOL Menu;
    UINT Dpi;
} MIME_ADJUST_RECT;

/* NtGdiGetAndSetDCDword and NtGdiGetDCDword indices. */
enum {
    MIME_DCDW_TEXTCOLOR = 1,
    MIME_DCDW_BKCOLOR,
    MIME_DCDW_BKMODE,
    MIME_DCDW_TEXTALIGN,
    MIME_DCDW_ROP2,
    MIME_DCDW_BRUSHCOLOR,
    MIME_DCDW_PENCOLOR,
    MIME_DCDW_STRETCHMODE,
    MIME_DCDW_GRAPHICSMODE,
    MIME_DCDW_MAPMODE,
    MIME_DCDW_LAYOUT,
    MIME_DCDW_ARCDIRECTION,
    MIME_DCDW_POLYFILLMODE,
    MIME_DCDW_TEXTCHAREXTRA,
    MIME_DCDW_PIXELFORMAT,  /* read only, set with NtGdiSetPixelFormat. */
    MIME_DCDW_PATHSTATE,    /* read only: 0 no path, 1 open, 2 closed. */
    MIME_DCDW_WINDOW,
};

/* NtGdiDoPalette functions on the color table of a dib section, numbered as on windows. */
#define MIME_PAL_GETDIBCOLORTABLE 4
#define MIME_PAL_SETDIBCOLORTABLE 5

/* NtGdiGetCharWidthW and NtGdiGetCharABCWidthsW flags. */
#define MIME_GCW_INDICES 0x0008      /* chars are glyph indices. */
#define MIME_GCABCW_INDICES 0x0002   /* chars are glyph indices. */
#define MIME_GCABCW_FLOAT 0x0100     /* the buffer receives ABCFLOAT values. */
#define MIME_GTEE_INDICES 0x0001     /* NtGdiGetTextExtentExW: the text is glyph indices. */

/* NtGdiPolyPolyDraw functions. */
enum {
    MIME_POLY_POLYGON = 1,
    MIME_POLY_POLYLINE = 2,
    MIME_POLY_BEZIER = 3,
    MIME_POLY_LINETO = 4,
    MIME_POLY_BEZIERTO = 5,
};

/* NtGdiGetTransform selectors, as on windows. */
#define MIME_XFORM_WORLD 0x203
#define MIME_XFORM_WORLD_TO_DEVICE 0x204
#define MIME_XFORM_PAGE_TO_DEVICE 0x304
#define MIME_XFORM_DEVICE_TO_WORLD 0x402

/* NtGdiMimeSetExtent targets. */
#define MIME_EXTENT_WINDOW 0
#define MIME_EXTENT_VIEWPORT 1

/*
 * NtGdiEnumFonts output: one record per matching face, back to back. the buffer size is in bytes and receives
 * the size needed.
 */
typedef struct MIME_FONT_ENUM_RECORD {
    ENUMLOGFONTEXW Font;
    NEWTEXTMETRICEXW Metrics;
    DWORD Type;
    DWORD Pad;
} MIME_FONT_ENUM_RECORD;

/* NtGdiGetDCPoint indices and the private NtGdiSetDCPoint routine. */
enum {
    MIME_DCPT_VIEWPORTORG = 1,
    MIME_DCPT_WINDOWORG,
    MIME_DCPT_CURRENTPOSITION,
    MIME_DCPT_DCORG,
    MIME_DCPT_WINDOWEXT,
    MIME_DCPT_VIEWPORTEXT,
};

/*
 * icons: the color bitmap is 32 bpp premultiplied and the mask optional. without a color bitmap the mask is
 * twice as tall, mask then image. MIME_CURSORF_SYSTEM marks a system cursor with its IDC_ value in the low word
 * of Flags, which the host shows as its native cursor.
 */
#define MIME_CURSORF_SYSTEM 0x80000000u
typedef struct MIME_CURSOR_DATA {
    ULONG Width;
    ULONG Height;
    ULONG HotspotX;
    ULONG HotspotY;
    BOOL IsIcon;
    ULONG Flags;
    HBITMAP Color;
    HBITMAP Mask;
} MIME_CURSOR_DATA;

/* ------------------------------------------------------------------ menus */

/* NtUserMimeCallMenu routines. */
enum {
    MIME_MENU_ISMENU = 1,
    MIME_MENU_ITEMCOUNT,
    MIME_MENU_DEFAULTITEM, /* param: GMDI_* flags in the low word, bit 16 for by position. */
    MIME_MENU_GETINFO,     /* param: MENUINFO pointer */
    MIME_MENU_SYSTEMWINDOW, /* the window whose system menu this is, NULL for other menus. */
    MIME_MENU_SERIAL,       /* a number that changes with every edit of the menu. */
    MIME_MENU_ISPOPUP,      /* nonzero for a popup menu, which a window cannot have as its menu bar. */
};

/* ------------------------------------------------------------------ clipboard */

/*
 * memory formats travel as bytes: NtUserSetClipboardData copies Size bytes from Data, NtUserGetClipboardData
 * copies up to Size bytes into Data and returns the full size in Size. gdi formats pass their handle and no data;
 * delayed rendering passes neither.
 */
typedef struct MIME_CLIPBOARD_DATA {
    PVOID Data;
    SIZE_T Size;
    DWORD Sequence; /* get: the sequence number the data belongs to. */
    BOOL Gdi;       /* get: the result is a gdi handle. */
} MIME_CLIPBOARD_DATA;

/* ------------------------------------------------------------------ window properties */

typedef struct PROPLISTITEM {
    ATOM Atom;
    HANDLE Data;
} PROPLISTITEM;

/* ------------------------------------------------------------------ user services */

W32KAPI BOOL NTAPI NtUserInitializeClientPfnArrays(const MIME_CLIENT_INFO *info);
W32KAPI ATOM NTAPI NtUserRegisterClassExWOW(const WNDCLASSEXW *wc, PUNICODE_STRING name, PUNICODE_STRING menu,
                                            DWORD fnid, DWORD flags);
W32KAPI BOOL NTAPI NtUserUnregisterClass(PUNICODE_STRING name, HINSTANCE instance);
W32KAPI BOOL NTAPI NtUserGetClassInfoEx(HINSTANCE instance, PUNICODE_STRING name, LPWNDCLASSEXW wc, LPWSTR *menu,
                                        BOOL ansi);
W32KAPI ULONG_PTR NTAPI NtUserSetClassLongPtr(HWND hwnd, int index, ULONG_PTR value, BOOL ansi);
W32KAPI ULONG NTAPI NtUserSetClassLong(HWND hwnd, int index, ULONG value, BOOL ansi);
W32KAPI USHORT NTAPI NtUserSetClassWord(HWND hwnd, int index, USHORT value);
W32KAPI int NTAPI NtUserGetClassName(HWND hwnd, BOOL real, PUNICODE_STRING name);
W32KAPI HWND NTAPI NtUserCreateWindowEx(DWORD ex_style, PUNICODE_STRING class_name, PUNICODE_STRING window_name,
                                        DWORD style, int x, int y, int width, int height, HWND parent, HMENU menu,
                                        HINSTANCE instance, LPVOID param, DWORD flags);
W32KAPI BOOL NTAPI NtUserDestroyWindow(HWND hwnd);
W32KAPI BOOL NTAPI NtUserShowWindow(HWND hwnd, int cmd);
W32KAPI BOOL NTAPI NtUserShowWindowAsync(HWND hwnd, int cmd);
W32KAPI BOOL NTAPI NtUserSetWindowPos(HWND hwnd, HWND after, int x, int y, int cx, int cy, UINT flags);
W32KAPI BOOL NTAPI NtUserEnableWindow(HWND hwnd, BOOL enable);
W32KAPI HWND NTAPI NtUserSetParent(HWND hwnd, HWND parent);
W32KAPI HWND NTAPI NtUserGetAncestor(HWND hwnd, UINT flags);
W32KAPI LONG_PTR NTAPI NtUserSetWindowLongPtr(HWND hwnd, int index, LONG_PTR value, BOOL ansi);
W32KAPI LONG NTAPI NtUserSetWindowLong(HWND hwnd, int index, LONG value, BOOL ansi);
W32KAPI BOOL NTAPI NtUserDefSetText(HWND hwnd, PUNICODE_STRING text);
W32KAPI int NTAPI NtUserInternalGetWindowText(HWND hwnd, LPWSTR buffer, int count);
W32KAPI HWND NTAPI NtUserFindWindowEx(HWND parent, HWND after, PUNICODE_STRING class_name, PUNICODE_STRING name,
                                      DWORD type);
W32KAPI BOOL NTAPI NtUserGetKeyboardState(PBYTE state);
W32KAPI DWORD NTAPI NtUserGetGuiResources(HANDLE process, DWORD flags);
W32KAPI BOOL NTAPI NtUserSetKeyboardState(PBYTE state);
W32KAPI HWINSTA NTAPI NtUserCreateWindowStation(POBJECT_ATTRIBUTES attributes, ACCESS_MASK access, HANDLE keyboard,
                                                DWORD offset, PVOID table, PUNICODE_STRING klid, DWORD flags);
W32KAPI HWINSTA NTAPI NtUserOpenWindowStation(POBJECT_ATTRIBUTES attributes, ACCESS_MASK access);
W32KAPI HDESK NTAPI NtUserCreateDesktopEx(POBJECT_ATTRIBUTES attributes, PUNICODE_STRING device, PVOID devmode,
                                          DWORD flags, ACCESS_MASK access, ULONG heap_kb);
W32KAPI HDESK NTAPI NtUserOpenDesktop(POBJECT_ATTRIBUTES attributes, DWORD flags, ACCESS_MASK access);
W32KAPI HDESK NTAPI NtUserOpenInputDesktop(DWORD flags, BOOL inherit, ACCESS_MASK access);
W32KAPI HWINSTA NTAPI NtUserGetProcessWindowStation(void);
W32KAPI BOOL NTAPI NtUserSetProcessWindowStation(HWINSTA handle);
W32KAPI HDESK NTAPI NtUserGetThreadDesktop(DWORD tid, HDESK console);
W32KAPI BOOL NTAPI NtUserSetThreadDesktop(HDESK handle);
W32KAPI BOOL NTAPI NtUserCloseWindowStation(HWINSTA handle);
W32KAPI BOOL NTAPI NtUserCloseDesktop(HDESK handle);
W32KAPI BOOL NTAPI NtUserSwitchDesktop(HDESK handle, DWORD flags, DWORD fade);
W32KAPI BOOL NTAPI NtUserGetObjectInformation(HANDLE handle, DWORD index, PVOID info, DWORD length, PDWORD needed);
W32KAPI BOOL NTAPI NtUserSetObjectInformation(HANDLE handle, DWORD index, PVOID info, DWORD length);
W32KAPI NTSTATUS NTAPI NtUserBuildNameList(HWINSTA handle, ULONG length, PVOID buffer, PULONG needed);
W32KAPI NTSTATUS NTAPI NtUserBuildHwndList(HDESK desktop, HWND parent, BOOL children, BOOL remove_immersive,
                                           ULONG thread, ULONG max, HWND *list, ULONG *count);
W32KAPI BOOL NTAPI NtUserGetWindowPlacement(HWND hwnd, WINDOWPLACEMENT *placement);
W32KAPI BOOL NTAPI NtUserSetWindowPlacement(HWND hwnd, const WINDOWPLACEMENT *placement);

W32KAPI LRESULT NTAPI NtUserMessageCall(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, ULONG_PTR result_info,
                                        DWORD type, BOOL ansi);
W32KAPI BOOL NTAPI NtUserPostMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
W32KAPI BOOL NTAPI NtUserPostThreadMessage(DWORD thread, UINT msg, WPARAM wparam, LPARAM lparam);
W32KAPI BOOL NTAPI NtUserGetMessage(MSG *msg, HWND hwnd, UINT min, UINT max);
W32KAPI BOOL NTAPI NtUserPeekMessage(MSG *msg, HWND hwnd, UINT min, UINT max, UINT flags);
W32KAPI LRESULT NTAPI NtUserDispatchMessage(const MSG *msg);
W32KAPI BOOL NTAPI NtUserTranslateMessage(const MSG *msg, UINT flags);
W32KAPI BOOL NTAPI NtUserWaitMessage(void);
W32KAPI UINT NTAPI NtUserRegisterWindowMessage(PUNICODE_STRING name);
W32KAPI ULONG_PTR NTAPI NtUserGetThreadState(ULONG routine);
W32KAPI BOOL NTAPI NtUserGetMessagePos(void);

W32KAPI HDC NTAPI NtUserBeginPaint(HWND hwnd, PAINTSTRUCT *ps);
W32KAPI BOOL NTAPI NtUserEndPaint(HWND hwnd, const PAINTSTRUCT *ps);
W32KAPI HDC NTAPI NtUserGetDC(HWND hwnd);
W32KAPI HDC NTAPI NtUserGetDCEx(HWND hwnd, HRGN clip, ULONG flags);
W32KAPI HDC NTAPI NtUserGetWindowDC(HWND hwnd);
W32KAPI int NTAPI NtUserReleaseDC(HWND hwnd, HDC dc);
W32KAPI BOOL NTAPI NtUserInvalidateRect(HWND hwnd, const RECT *rect, BOOL erase);
W32KAPI BOOL NTAPI NtUserValidateRect(HWND hwnd, const RECT *rect);
W32KAPI BOOL NTAPI NtUserRedrawWindow(HWND hwnd, const RECT *rect, HRGN region, UINT flags);
W32KAPI BOOL NTAPI NtUserGetUpdateRect(HWND hwnd, LPRECT rect, BOOL erase);

W32KAPI HWND NTAPI NtUserSetFocus(HWND hwnd);
W32KAPI HWND NTAPI NtUserSetActiveWindow(HWND hwnd);
W32KAPI HWND NTAPI NtUserGetForegroundWindow(void);
W32KAPI HWND NTAPI NtUserSetCapture(HWND hwnd);
W32KAPI SHORT NTAPI NtUserGetKeyState(int key);
W32KAPI UINT NTAPI NtUserSendInput(UINT count, const INPUT *inputs, int size);
W32KAPI HHOOK NTAPI NtUserSetWindowsHookEx(HINSTANCE module, PUNICODE_STRING module_name, DWORD thread, int id,
                                           HOOKPROC proc, BOOL ansi);
W32KAPI BOOL NTAPI NtUserUnhookWindowsHookEx(HHOOK hook);
W32KAPI LRESULT NTAPI NtUserCallNextHookEx(int code, WPARAM wparam, LPARAM lparam, BOOL ansi);
W32KAPI SHORT NTAPI NtUserGetAsyncKeyState(int key);
W32KAPI BOOL NTAPI NtUserGetCursorPos(LPPOINT point);
W32KAPI UINT_PTR NTAPI NtUserSetTimer(HWND hwnd, UINT_PTR id, UINT elapse, TIMERPROC proc, ULONG tolerance);
W32KAPI BOOL NTAPI NtUserKillTimer(HWND hwnd, UINT_PTR id);
W32KAPI BOOL NTAPI NtUserTrackMouseEvent(TRACKMOUSEEVENT *track);

W32KAPI BOOL NTAPI NtUserSystemParametersInfo(UINT action, UINT param, PVOID data, UINT ini);
struct _devicemodeW;
W32KAPI NTSTATUS NTAPI NtUserEnumDisplaySettings(PUNICODE_STRING device, DWORD index, struct _devicemodeW *mode,
                                                 DWORD flags);
W32KAPI LONG NTAPI NtUserChangeDisplaySettings(PUNICODE_STRING device, struct _devicemodeW *mode, DWORD flags,
                                               PVOID param);
W32KAPI BOOL NTAPI NtUserSystemParametersInfoForDpi(UINT action, UINT param, PVOID data, UINT ini, UINT dpi);
W32KAPI BOOL NTAPI NtUserForceWindowToDpiForTest(HWND hwnd, UINT dpi);
W32KAPI BOOL NTAPI NtUserDeferWindowDpiChanges(HWND hwnd);
W32KAPI BOOL NTAPI NtUserRestoreWindowDpiChanges(HWND hwnd);
W32KAPI BOOL NTAPI NtUserSetDpiForWindow(HWND hwnd, UINT dpi);
W32KAPI BOOL NTAPI NtUserEnableChildWindowDpiMessage(HWND hwnd, BOOL enable);
W32KAPI BOOL NTAPI NtUserIsChildWindowDpiMessageEnabled(HWND hwnd);
W32KAPI BOOL NTAPI NtUserIsWindowBroadcastingDpiToChildren(HWND hwnd);
W32KAPI BOOL NTAPI NtUserMessageBeep(UINT type);

W32KAPI HICON NTAPI NtUserCreateEmptyCurObject(BOOL icon);
W32KAPI BOOL NTAPI NtUserSetCursorIconData(HICON icon, PUNICODE_STRING module, PUNICODE_STRING resource,
                                           const MIME_CURSOR_DATA *data);
W32KAPI BOOL NTAPI NtUserDestroyCursor(HICON icon, BOOL force);
W32KAPI BOOL NTAPI NtUserGetIconInfo(HICON icon, ICONINFO *info, PUNICODE_STRING module, PUNICODE_STRING resource,
                                     LPDWORD bpp, BOOL internal);
W32KAPI BOOL NTAPI NtUserDrawIconEx(HDC dc, int x, int y, HICON icon, int cx, int cy, UINT step, HBRUSH brush,
                                    UINT flags, BOOL meta, PVOID reserved);
W32KAPI HCURSOR NTAPI NtUserSetCursor(HCURSOR cursor);

W32KAPI HMENU NTAPI NtUserCreateMenu(void);
W32KAPI HMENU NTAPI NtUserCreatePopupMenu(void);
W32KAPI BOOL NTAPI NtUserDestroyMenu(HMENU menu);
W32KAPI BOOL NTAPI NtUserThunkedMenuItemInfo(HMENU menu, UINT item, BOOL by_position, BOOL insert,
                                             LPMENUITEMINFOW info, PUNICODE_STRING text);
W32KAPI BOOL NTAPI NtUserThunkedMenuInfo(HMENU menu, LPCMENUINFO info);
W32KAPI BOOL NTAPI NtUserDeleteMenu(HMENU menu, UINT position, UINT flags);
W32KAPI BOOL NTAPI NtUserRemoveMenu(HMENU menu, UINT position, UINT flags);
W32KAPI UINT NTAPI NtUserEnableMenuItem(HMENU menu, UINT id, UINT flags);
W32KAPI DWORD NTAPI NtUserCheckMenuItem(HMENU menu, UINT id, UINT flags);
W32KAPI BOOL NTAPI NtUserSetMenuDefaultItem(HMENU menu, UINT item, UINT by_position);
W32KAPI BOOL NTAPI NtUserSetMenu(HWND hwnd, HMENU menu, BOOL repaint);
W32KAPI HMENU NTAPI NtUserGetSystemMenu(HWND hwnd, BOOL revert);
W32KAPI BOOL NTAPI NtUserDrawMenuBar(HWND hwnd);
W32KAPI BOOL NTAPI NtUserEndMenu(void);
W32KAPI BOOL NTAPI NtUserMimeGetMenuItemInfo(HMENU menu, UINT item, BOOL by_position, LPMENUITEMINFOW info);
W32KAPI ULONG_PTR NTAPI NtUserMimeCallMenu(HMENU menu, ULONG_PTR param, ULONG routine);

/* ------------------------------------------------------------------ shutdown */

/* sent to a thread during shutdown; its default processing asks the thread's windows. */
#define MX_WM_CLIENTSHUTDOWN 0x3B
/* how the shutdown goes, and what the windows get in the lparam of WM_QUERYENDSESSION. */
#define MX_WMCS_CLOSEAPP 0x00000001    /* ENDSESSION_CLOSEAPP */
#define MX_WMCS_QUERY 0x00000002       /* send WM_QUERYENDSESSION, else WM_ENDSESSION. */
#define MX_WMCS_SHUTDOWN 0x00000004
#define MX_WMCS_USERPROCESS 0x00000008 /* a user process, which ends once its windows were told. */
#define MX_WMCS_FORCEIFHUNG 0x00000040
#define MX_WMCS_NORETRY 0x00000080
#define MX_WMCS_ENDING 0x00000100      /* the wparam of WM_ENDSESSION. */
#define MX_WMCS_QUICK 0x00000200
#define MX_WMCS_REBOOT 0x00000400
#define MX_WMCS_CRITICAL 0x40000000    /* ENDSESSION_CRITICAL */
#define MX_WMCS_LOGOFF 0x80000000u     /* ENDSESSION_LOGOFF */

W32KAPI ULONG NTAPI NtUserShutdownBlockReasonCreate(HWND hwnd, LPCWSTR reason, ULONG length);
W32KAPI ULONG NTAPI NtUserShutdownReasonDestroy(HWND hwnd);
W32KAPI ULONG NTAPI NtUserShutdownBlockReasonQuery(HWND hwnd, LPWSTR buffer, PULONG count);
W32KAPI ULONG NTAPI NtUserPrepareForLogoff(void);
W32KAPI LONG NTAPI NtUserMimeExitWindows(ULONG flags, ULONG reason);

/*
 * the property dcomp.dll sets on a root window it composes on the cpu for want of a gpu, so win32k shows its surface
 * although the window has no redirection bitmap. an integer atom, the same in user32's and win32k's tables.
 */
#define MIME_PROP_COMPOSITION ((ATOM)0xBFFE)

/* ------------------------------------------------------------------ gpu */

/* what MIME_HWNDPARAM_PRESENTLAYER tells of a window, window pixels relative to its top level window. */
typedef struct MIME_PRESENT_LAYER {
    ULONG Window;  /* the host window: the top level window of the tree, 0 while it has none. */
    ULONG Order;   /* the place of the window in the paint order of the tree, the front ones last. */
    RECT Client;
    RECT Visible;  /* the part of the client area that shows, empty while the window is hidden or covered. */
} MIME_PRESENT_LAYER;

/* where a window dc renders in its host window, see mime/gpu.h. */
typedef struct MIME_GPU_SURFACE {
    HWND Window;
    ULONG HostWindow; /* 0 while the window is hidden. */
    LONG X, Y;        /* the client area in the host window's surface. */
    ULONG Width, Height;
    ULONG Order;      /* the window's place in the paint order of the host window's tree. */
    RECT Visible;     /* the part of the client area that shows, in the host window's surface. */
    ULONG Stamp;      /* counts what is told of the places of windows: the later has the greater. */
} MIME_GPU_SURFACE;

W32KAPI NTSTATUS NTAPI NtGdiMimeGpuOpen(PVOID *base, PULONG size);
W32KAPI NTSTATUS NTAPI NtGdiMimeGpuAddBlock(ULONG size, PVOID *base, PULONG index);
W32KAPI NTSTATUS NTAPI NtGdiMimeGpuKick(void);
W32KAPI NTSTATUS NTAPI NtGdiMimeGpuWait(ULONG64 fence, ULONG timeout_ms);
W32KAPI BOOL NTAPI NtGdiMimeGpuGetSurface(HDC dc, MIME_GPU_SURFACE *surface);

/* the process's audio output streams, see mime/audio.h. */
#define MIME_AUDIO_OPEN 0  /* plays the format written in the header. */
#define MIME_AUDIO_START 1
#define MIME_AUDIO_STOP 2
#define MIME_AUDIO_CLOSE 3 /* the slot and its memory stay with the process for a later stream. */
/* write the host's devices over the header of a stream that is not open, as a MimeAudioDeviceList. */
#define MIME_AUDIO_OUTPUTS 4
#define MIME_AUDIO_INPUTS 5
W32KAPI NTSTATUS NTAPI NtGdiMimeAudioCreate(PVOID *base, PULONG index);
W32KAPI NTSTATUS NTAPI NtGdiMimeAudioControl(ULONG index, ULONG op);

/* the process's vulkan connection and its shared memory blocks, see mime/mvk_wire.h. */
W32KAPI NTSTATUS NTAPI NtGdiMimeVkOpen(PVOID *base);
W32KAPI NTSTATUS NTAPI NtGdiMimeVkAlloc(SIZE_T size, PVOID *base, PULONG64 pa);
W32KAPI NTSTATUS NTAPI NtGdiMimeVkFree(PVOID base);
W32KAPI NTSTATUS NTAPI NtGdiMimeVkKick(void);
W32KAPI NTSTATUS NTAPI NtGdiMimeVkWait(ULONG64 serial, ULONG timeout_ms);

/* shared direct3d resources and fences, see mime/gfxshare.h. */
W32KAPI NTSTATUS NTAPI NtGdiMimeGfxShare(ULONG op, PVOID arguments);

/* display gamma ramps: three channels of 256 values, red first. */
#define MIME_GAMMA_GET 0    /* the session ramp gdi set, the identity until one is set. */
#define MIME_GAMMA_SET 1    /* gdi's ramp. */
#define MIME_GAMMA_DEVICE 2 /* the caller's fullscreen ramp over gdi's, NULL drops it. */
W32KAPI NTSTATUS NTAPI NtGdiMimeGamma(ULONG op, PUSHORT ramp);

/*
 * drag and drop with host programs, see win32k dnd.c. a target registers a window and its process's handler; a
 * drag over the window then posts WM_MIME_HOSTDND to its thread, wparam the handler and lparam the phase and
 * session. the handler is zero for a window that only takes files, which user32 handles itself. MIME_DND_STATE
 * and NtUserMimeDndData tell the target what the drag holds, MIME_DND_REPLY carries its answers. a source starts
 * a host drag and gets the host's data requests and the drag's end as the same message, on its thread.
 */
#define MIME_DND_PHASE_ENTER 1
#define MIME_DND_PHASE_OVER 2
#define MIME_DND_PHASE_LEAVE 3
#define MIME_DND_PHASE_DROP 4
#define MIME_DND_PHASE_REQUEST 5 /* lparam >> 4 is the request slot, see MIME_DND_REQUEST. */
#define MIME_DND_PHASE_END 6
#define MIME_DND_GOING 0xFFFFFFFFu /* MIME_DND_RESULT while the host drag goes on, else dropped << 8 | effect. */
#define MIME_DND_STATE_SIZE 16     /* kinds, allowed effects, keys, x, y, format count, then the formats. */
enum {
    MIME_DND_REGISTER = 1, /* hwnd, handler: the window takes drops. */
    MIME_DND_REVOKE,       /* hwnd */
    MIME_DND_REPLY,        /* session, phase << 8 | effect: the target's answer. */
    MIME_DND_STATE,        /* session, 0, out: ULONG[MIME_DND_STATE_SIZE]. returns 1 while the session is current. */
    MIME_DND_START,        /* kinds, allowed effects, handler: this thread starts a host drag. returns its session. */
    MIME_DND_ICON,         /* bytes, 0, out: the MimeDragIcon this thread's next MIME_DND_START shows. returns 1. */
    MIME_DND_STOP,         /* session */
    MIME_DND_REQUEST,      /* slot, 0, out: ULONG[2], the kind and request of a data request for the source. */
    MIME_DND_RESULT,       /* session: how the source's host drag ended. */
    MIME_DND_OUTSIDE,      /* whether the host says the pointer left the window of its last event. */
};
W32KAPI ULONG_PTR NTAPI NtUserMimeDnd(ULONG op, ULONG_PTR a, ULONG_PTR b, PULONG out);
/* a chunk, from offset, of what a drag over the guest holds in a clipboard format. total gets its size. */
W32KAPI ULONG NTAPI NtUserMimeDndData(ULONG session, ULONG format, PUCHAR buffer, ULONG capacity, ULONG offset,
                                      PULONG total);
/*
 * the host's input method, see win32k ime.c. the session has one composition, for the focus window of the
 * foreground thread. MIME_IME_GET returns part of it like ImmGetCompositionString. MIME_IME_AREA tells the host
 * where text goes in window a: buffer holds left, top, right, bottom and the caret x, in client coordinates.
 * MIME_IME_NOTIFY ends the composition, a being CPS_COMPLETE or CPS_CANCEL. MIME_IME_AVAILABLE says whether the
 * host has an input method at all.
 *
 * a program that composes through msctf.dll gets the host's input method from there. MIME_IME_TSF registers a
 * window of the calling thread, or NULL, as a; that window then gets MIME_TSF_MESSAGE instead of the input method
 * messages, with wparam MIME_IME_PREEDIT, MIME_IME_COMMIT or MIME_IME_CANCELLED, and lparam the selected part of a
 * preedit text as two words, each a position plus one, or 0 for none. the window answers 1 when the text service
 * took the event, otherwise the usual input method messages follow. MIME_IME_PENDING returns the text of the event
 * being sent. MIME_IME_RESET silently drops what the host's input method holds of a composition.
 */
enum {
    MIME_IME_GET = 1,
    MIME_IME_AREA,
    MIME_IME_NOTIFY,
    MIME_IME_AVAILABLE,
    MIME_IME_TSF,
    MIME_IME_PENDING,
    MIME_IME_RESET,
};
#define MIME_TSF_MESSAGE 0x0401
W32KAPI ULONG NTAPI NtUserMimeIme(ULONG op, ULONG_PTR a, ULONG_PTR b, PUCHAR buffer, ULONG size);

/*
 * the notification area, see win32k tray.c. shell32 keeps the Shell_NotifyIcon icons in win32k, which tells the
 * host's desktop and sends clicks back to the window as the icon's callback message. MIME_TRAYOP_MENU serves
 * TrackPopupMenu: when the desktop asked an icon for its context menu, the desktop shows the menu and the call
 * returns 1. MIME_TRAYOP_MENU_RESULT then returns 0 while the choice is pending, 1 for none, else 0x80000000 with
 * the item id. handles are 32 bits, as on windows.
 */
enum {
    MIME_TRAYOP_ADD = 1,
    MIME_TRAYOP_MODIFY,
    MIME_TRAYOP_DELETE,
    MIME_TRAYOP_MENU,
    MIME_TRAYOP_MENU_RESULT,
};

typedef struct MIME_TRAY_REQUEST {
    ULONG Window;
    ULONG Key; /* shell32's number for the icon, which the window and uID or GUID identify. */
    ULONG Id;  /* uID, the icon's number in its callback. */
    ULONG Flags; /* NIF_* */
    ULONG Callback;
    ULONG Icon;
    ULONG State, StateMask;
    ULONG Version;
    ULONG BalloonIcon;
    ULONG InfoFlags; /* NIIF_* */
    ULONG Timeout;
    ULONG Menu; /* HMENU, for MIME_TRAYOP_MENU. */
    WCHAR Tip[128];
    WCHAR InfoTitle[64];
    WCHAR Info[256];
} MIME_TRAY_REQUEST;

/*
 * NtUserMimeShell: MIME_SHELLOP_COMMAND takes a MimeShellToast, MimeShellWindow or MimeShellDialog of mime/abi.h as
 * request, with its own size. it returns the toast number for MIME_SHELL_TOAST_SHOW and the dialog number for a new
 * MIME_SHELL_DIALOG, 1 or 0 for MIME_SHELL_TOAST_PROBE, else 0 or a failure status. MIME_SHELLOP_WAIT waits id
 * milliseconds, 0xFFFFFFFF for no limit, for an event of the caller's toasts and dialogs, and returns 1 with the event
 * in request, 0 for none, or the status that woke it.
 */
#define MIME_SHELLOP_COMMAND 1
#define MIME_SHELLOP_WAIT 2
typedef struct MIME_SHELL_EVENT {
    ULONG Kind; /* MIME_SHELL_EVENT_* */
    ULONG Id;
    LONG Arg;
    ULONG ReplyLength;
    USHORT Reply[256];
} MIME_SHELL_EVENT;
W32KAPI ULONG NTAPI NtUserMimeTray(ULONG op, PUCHAR request, ULONG size);
W32KAPI LONG NTAPI NtUserMimeShell(ULONG op, ULONG id, PUCHAR request, ULONG size);
/* a chunk of the data the host asked the source for. total is 0xFFFFFFFF for none. */
W32KAPI ULONG NTAPI NtUserMimeDndProvide(ULONG request, PUCHAR data, ULONG length, ULONG offset, ULONG total);

/*
 * screen capture, see win32k capture.c: the kernel side of dxgi.dll's output duplication and of
 * Windows.Graphics.Capture for windows and monitors. a duplication of a monitor yields frames of the screen as mime
 * composes it, in bgra rows, with the rectangles changed since the last frame and the pointer kept separate. every
 * structure is made of 32-bit fields, so 32-bit programs share the layout. the calls return the statuses dxgi.dll
 * expects from the kernel: STATUS_TIMEOUT for no new frame, STATUS_NOT_FOUND when the monitors changed,
 * STATUS_INSTANCE_NOT_AVAILABLE past the duplication limit, STATUS_ACCESS_DENIED off the input desktop and
 * STATUS_BUFFER_TOO_SMALL with the needed size in the structure.
 */
enum {
    MIME_CAPOP_OPEN = 1, /* info: MIME_CAPTURE_OPEN */
    MIME_CAPOP_CLOSE,    /* id: the duplication */
    MIME_CAPOP_FRAME,    /* id, info: MIME_CAPTURE_FRAME, data: the monitor's rows, size: their bytes. */
    MIME_CAPOP_DIRTY,    /* id, data: RECTs of the last frame, size: their bytes, info: ULONG bytes needed. */
    MIME_CAPOP_MOVES,    /* the same for the move rectangles, DXGI_OUTDUPL_MOVE_RECT. */
    MIME_CAPOP_SHAPE,    /* id, info: MIME_CAPTURE_SHAPE, data: the pointer's image, size: its bytes. */
    MIME_CAPOP_WINDOW,   /* id: the window, info: MIME_CAPTURE_WINDOW, data: bgra rows, size: their bytes. */
};

/*
 * duplicates a window instead of a monitor: Monitor holds the window, and a frame is the window's own pixels,
 * whole window or client area, including what its process presented through the host.
 */
#define MIME_CAPTURE_WINDOW_TARGET 0x80000000u
#define MIME_CAPTURE_TARGET_CLIENT 0x40000000u
typedef struct MIME_CAPTURE_OPEN {
    ULONG Monitor; /* the monitor index, or the window. */
    ULONG Flags;
    ULONG Handle;
    ULONG Width, Height;
    LONG X, Y;
    ULONG RefreshNumerator, RefreshDenominator;
} MIME_CAPTURE_OPEN;

typedef struct MIME_CAPTURE_FRAME {
    ULONG Accumulated;
    ULONG LastPresentLow, LastPresentHigh;
    ULONG LastMouseLow, LastMouseHigh;
    ULONG RectsCoalesced;
    ULONG PointerVisible;
    LONG PointerX, PointerY;
    ULONG DirtyBytes, MoveBytes;
    ULONG ShapeBytes;
    /* the frame size, since a window's can change. also reported when the buffer is too small. */
    ULONG Width, Height;
} MIME_CAPTURE_FRAME;

typedef struct MIME_CAPTURE_SHAPE {
    ULONG Type; /* DXGI_OUTDUPL_POINTER_SHAPE_TYPE_* */
    ULONG Width, Height, Pitch;
    LONG HotspotX, HotspotY;
    ULONG Size;
} MIME_CAPTURE_SHAPE;

/* PW_CLIENTONLY, and PW_RENDERFULLCONTENT, which includes what the window's process presented through the host. */
#define MIME_CAPTURE_CLIENT_ONLY 0x1
#define MIME_CAPTURE_FULL_CONTENT 0x2
typedef struct MIME_CAPTURE_WINDOW {
    ULONG Flags;
    ULONG Width, Height;
    LONG ClientX, ClientY; /* where the client area starts in the window's pixels. */
} MIME_CAPTURE_WINDOW;
W32KAPI LONG NTAPI NtUserMimeCapture(ULONG op, ULONG id, PUCHAR info, PUCHAR data, ULONG size);

W32KAPI BOOL NTAPI NtUserOpenClipboard(HWND owner, ULONG reserved);
W32KAPI BOOL NTAPI NtUserCloseClipboard(void);
W32KAPI BOOL NTAPI NtUserEmptyClipboard(void);
W32KAPI HANDLE NTAPI NtUserSetClipboardData(UINT format, HANDLE handle, MIME_CLIPBOARD_DATA *data);
W32KAPI HANDLE NTAPI NtUserGetClipboardData(UINT format, MIME_CLIPBOARD_DATA *data);
W32KAPI BOOL NTAPI NtUserIsClipboardFormatAvailable(UINT format);
W32KAPI BOOL NTAPI NtUserGetUpdatedClipboardFormats(UINT *formats, UINT count, UINT *out);
W32KAPI int NTAPI NtUserCountClipboardFormats(void);
W32KAPI UINT NTAPI NtUserEnumClipboardFormats(UINT format);
W32KAPI int NTAPI NtUserGetClipboardFormatName(UINT format, LPWSTR name, int max);
W32KAPI HWND NTAPI NtUserGetClipboardOwner(void);
W32KAPI HWND NTAPI NtUserGetOpenClipboardWindow(void);
W32KAPI HWND NTAPI NtUserGetClipboardViewer(void);
W32KAPI HWND NTAPI NtUserSetClipboardViewer(HWND viewer);
W32KAPI BOOL NTAPI NtUserChangeClipboardChain(HWND remove, HWND next);
W32KAPI DWORD NTAPI NtUserGetClipboardSequenceNumber(void);
W32KAPI BOOL NTAPI NtUserAddClipboardFormatListener(HWND hwnd);
W32KAPI BOOL NTAPI NtUserRemoveClipboardFormatListener(HWND hwnd);

W32KAPI BOOL NTAPI NtUserSetLayeredWindowAttributes(HWND hwnd, COLORREF key, BYTE alpha, DWORD flags);
W32KAPI BOOL NTAPI NtUserGetLayeredWindowAttributes(HWND hwnd, COLORREF *key, BYTE *alpha, DWORD *flags);
W32KAPI BOOL NTAPI NtUserUpdateLayeredWindow(HWND hwnd, HDC dst, POINT *dst_pos, SIZE *size, HDC src,
                                             POINT *src_pos, COLORREF key, BLENDFUNCTION *blend, DWORD flags,
                                             RECT *dirty);
W32KAPI int NTAPI NtUserSetWindowRgn(HWND hwnd, HRGN region, BOOL redraw);
W32KAPI int NTAPI NtUserGetWindowRgnEx(HWND hwnd, HRGN region, UINT reserved);
W32KAPI BOOL NTAPI NtUserGetCursorInfo(PCURSORINFO info);
W32KAPI BOOL NTAPI NtUserClipCursor(const RECT *rect);
W32KAPI DWORD NTAPI NtUserMsgWaitForMultipleObjectsEx(DWORD count, const HANDLE *handles, DWORD timeout, DWORD mask,
                                                      DWORD flags);
W32KAPI BOOL NTAPI NtUserGetClipCursor(LPRECT rect);
W32KAPI BOOL NTAPI NtUserRegisterRawInputDevices(PCRAWINPUTDEVICE devices, UINT count, UINT size);
W32KAPI UINT NTAPI NtUserGetRegisteredRawInputDevices(PRAWINPUTDEVICE devices, PUINT count, UINT size);
W32KAPI UINT NTAPI NtUserGetRawInputData(HRAWINPUT input, UINT command, PVOID data, PUINT size, UINT header_size);
W32KAPI UINT NTAPI NtUserGetRawInputBuffer(PRAWINPUT data, PUINT size, UINT header_size);
W32KAPI UINT NTAPI NtUserGetRawInputDeviceList(PRAWINPUTDEVICELIST list, PUINT count, UINT size);
W32KAPI UINT NTAPI NtUserGetRawInputDeviceInfo(HANDLE device, UINT command, PVOID data, PUINT size);
W32KAPI BOOL NTAPI NtUserAttachThreadInput(DWORD attach, DWORD to, BOOL do_attach);
W32KAPI BOOL NTAPI NtUserIsTouchWindow(HWND hwnd, PULONG flags);
W32KAPI BOOL NTAPI NtUserSetProp(HWND hwnd, ATOM atom, HANDLE data);
W32KAPI HANDLE NTAPI NtUserGetProp(HWND hwnd, ATOM atom);
W32KAPI HANDLE NTAPI NtUserRemoveProp(HWND hwnd, ATOM atom);
W32KAPI NTSTATUS NTAPI NtUserBuildPropList(HWND hwnd, ULONG max, PROPLISTITEM *list, ULONG *count);

W32KAPI ULONG_PTR NTAPI NtUserCallNoParam(ULONG routine);
W32KAPI ULONG_PTR NTAPI NtUserCallOneParam(ULONG_PTR param, ULONG routine);
W32KAPI ULONG_PTR NTAPI NtUserCallTwoParam(ULONG_PTR param1, ULONG_PTR param2, ULONG routine);
W32KAPI ULONG_PTR NTAPI NtUserCallHwnd(HWND hwnd, ULONG routine);
W32KAPI ULONG_PTR NTAPI NtUserCallHwndParam(HWND hwnd, ULONG_PTR param, ULONG routine);

/* ------------------------------------------------------------------ gdi services */

W32KAPI HANDLE NTAPI NtGdiGetStockObject(int index);
W32KAPI BOOL NTAPI NtGdiDeleteObjectApp(HANDLE object);
W32KAPI int NTAPI NtGdiExtGetObjectW(HANDLE object, int size, PVOID buffer);
W32KAPI HANDLE NTAPI NtGdiGetDCObject(HDC dc, int type);
W32KAPI HDC NTAPI NtGdiCreateCompatibleDC(HDC dc);
W32KAPI HDC NTAPI NtGdiOpenDCW(PUNICODE_STRING device, PVOID mode, PUNICODE_STRING port, ULONG type, BOOL display,
                               HANDLE pdev, PVOID driver_info, PVOID umpd);
W32KAPI int NTAPI NtGdiSaveDC(HDC dc);
W32KAPI BOOL NTAPI NtGdiRestoreDC(HDC dc, int saved);
W32KAPI int NTAPI NtGdiGetDeviceCaps(HDC dc, int index);
W32KAPI HBITMAP NTAPI NtGdiSelectBitmap(HDC dc, HBITMAP bitmap);
W32KAPI HBRUSH NTAPI NtGdiSelectBrush(HDC dc, HBRUSH brush);
W32KAPI HPEN NTAPI NtGdiSelectPen(HDC dc, HPEN pen);
W32KAPI HFONT NTAPI NtGdiSelectFont(HDC dc, HFONT font);
W32KAPI BOOL NTAPI NtGdiGetAndSetDCDword(HDC dc, UINT index, DWORD value, DWORD *result);
W32KAPI BOOL NTAPI NtGdiGetDCDword(HDC dc, UINT index, DWORD *result);
W32KAPI BOOL NTAPI NtGdiGetDCPoint(HDC dc, UINT index, POINT *point);
W32KAPI BOOL NTAPI NtGdiMoveTo(HDC dc, int x, int y, POINT *old);
W32KAPI BOOL NTAPI NtGdiSetViewportOrgEx(HDC dc, int x, int y, LPPOINT old);
W32KAPI BOOL NTAPI NtGdiSetWindowOrgEx(HDC dc, int x, int y, LPPOINT old);
W32KAPI BOOL NTAPI NtGdiTransformPoints(HDC dc, POINT *in, POINT *out, int count, int mode);
W32KAPI BOOL NTAPI NtGdiFlush(void);

W32KAPI HBRUSH NTAPI NtGdiCreateSolidBrush(COLORREF color, HBRUSH reuse);
W32KAPI HPEN NTAPI NtGdiCreatePen(int style, int width, COLORREF color, HBRUSH reuse);
W32KAPI HFONT NTAPI NtGdiHfontCreate(const LOGFONTW *font, ULONG size, ULONG type, ULONG flags, PVOID reserved);
W32KAPI HBITMAP NTAPI NtGdiCreateBitmap(int width, int height, UINT planes, UINT bpp, const void *bits);
W32KAPI HBITMAP NTAPI NtGdiCreateCompatibleBitmap(HDC dc, int width, int height);
struct _D3DKMT_CREATEDCFROMMEMORY;
struct _D3DKMT_DESTROYDCFROMMEMORY;
W32KAPI NTSTATUS NTAPI NtGdiDdDDICreateDCFromMemory(struct _D3DKMT_CREATEDCFROMMEMORY *desc);
W32KAPI NTSTATUS NTAPI NtGdiDdDDIDestroyDCFromMemory(const struct _D3DKMT_DESTROYDCFROMMEMORY *desc);
W32KAPI HBITMAP NTAPI NtGdiCreateDIBSection(HDC dc, HANDLE section, DWORD offset, const BITMAPINFO *info,
                                            UINT usage, UINT header_size, ULONG flags, ULONG_PTR color_space,
                                            PVOID *bits);
W32KAPI ULONG NTAPI NtUserWaitForInputIdle(ULONG_PTR process_id, DWORD timeout, BOOL remote_handle);
W32KAPI ULONG NTAPI NtGdiDoPalette(HGDIOBJ object, WORD start, WORD count, PVOID entries, DWORD function, BOOL inbound);
W32KAPI int NTAPI NtGdiGetDIBitsInternal(HDC dc, HBITMAP bitmap, UINT start, UINT lines, PVOID bits,
                                         BITMAPINFO *info, UINT usage, UINT max_bits, UINT max_info);
W32KAPI int NTAPI NtGdiSetDIBitsToDeviceInternal(HDC dc, int x, int y, DWORD width, DWORD height, int src_x,
                                                 int src_y, DWORD start, DWORD lines, const void *bits,
                                                 const BITMAPINFO *info, UINT usage, UINT max_bits, UINT max_info,
                                                 BOOL transform, HANDLE transform_handle);
W32KAPI int NTAPI NtGdiStretchDIBitsInternal(HDC dc, int x, int y, int width, int height, int src_x, int src_y,
                                             int src_width, int src_height, const void *bits, const BITMAPINFO *info,
                                             UINT usage, DWORD rop, UINT max_info, UINT max_bits, HANDLE transform);

W32KAPI BOOL NTAPI NtGdiBitBlt(HDC dst, int x, int y, int width, int height, HDC src, int src_x, int src_y,
                               DWORD rop, DWORD bk_color, ULONG flags);
W32KAPI BOOL NTAPI NtGdiStretchBlt(HDC dst, int x, int y, int width, int height, HDC src, int src_x, int src_y,
                                   int src_width, int src_height, DWORD rop, DWORD bk_color);
W32KAPI BOOL NTAPI NtGdiPatBlt(HDC dc, int x, int y, int width, int height, DWORD rop);
W32KAPI BOOL NTAPI NtGdiAlphaBlend(HDC dst, int x, int y, int width, int height, HDC src, int src_x, int src_y,
                                   int src_width, int src_height, BLENDFUNCTION blend, HANDLE transform);
W32KAPI BOOL NTAPI NtGdiLineTo(HDC dc, int x, int y);
W32KAPI BOOL NTAPI NtGdiRectangle(HDC dc, int left, int top, int right, int bottom);
W32KAPI BOOL NTAPI NtGdiRoundRect(HDC dc, int left, int top, int right, int bottom, int width, int height);
W32KAPI BOOL NTAPI NtGdiEllipse(HDC dc, int left, int top, int right, int bottom);
W32KAPI ULONG NTAPI NtGdiPolyPolyDraw(HDC dc, POINT *points, ULONG *counts, ULONG count, int function);
W32KAPI COLORREF NTAPI NtGdiSetPixel(HDC dc, int x, int y, COLORREF color);
W32KAPI COLORREF NTAPI NtGdiGetPixel(HDC dc, int x, int y);

W32KAPI BOOL NTAPI NtGdiExtTextOutW(HDC dc, int x, int y, UINT options, const RECT *rect, LPCWSTR text, int count,
                                    const INT *dx, DWORD code_page);
W32KAPI BOOL NTAPI NtGdiGetTextExtentExW(HDC dc, LPCWSTR text, ULONG count, ULONG max_extent, ULONG *fit,
                                         ULONG *dx, LPSIZE size, ULONG flags);
W32KAPI BOOL NTAPI NtGdiGetTextMetricsW(HDC dc, TEXTMETRICW *metrics, ULONG size);
W32KAPI int NTAPI NtGdiGetTextFaceW(HDC dc, int count, LPWSTR name, BOOL alias);
W32KAPI BOOL NTAPI NtGdiGetCharWidthW(HDC dc, UINT first, UINT count, PWCHAR chars, ULONG flags, PVOID buffer);
W32KAPI BOOL NTAPI NtGdiGetCharABCWidthsW(HDC dc, UINT first, ULONG count, PWCHAR chars, ULONG flags, PVOID buffer);
W32KAPI int NTAPI NtGdiAddFontResourceW(LPCWSTR files, ULONG chars, ULONG files_count, ULONG flags, DWORD tid,
                                        PVOID dv);
W32KAPI ULONG NTAPI NtGdiGetFontData(HDC dc, DWORD table, DWORD offset, PVOID buffer, ULONG size);
W32KAPI BOOL NTAPI NtGdiRemoveFontResourceW(LPCWSTR files, ULONG chars, ULONG files_count, ULONG flags, DWORD tid,
                                            PVOID dv);
W32KAPI HANDLE NTAPI NtGdiAddFontMemResourceEx(PVOID data, DWORD size, PVOID dv, ULONG dv_size, DWORD *count);
W32KAPI BOOL NTAPI NtGdiRemoveFontMemResourceEx(HANDLE handle);
W32KAPI ULONG NTAPI NtGdiEnumFonts(HDC dc, ULONG type, ULONG flags, ULONG face_chars, LPCWSTR face, ULONG charset,
                                   ULONG *size, PVOID buffer);
W32KAPI ULONG NTAPI NtGdiGetOutlineTextMetricsInternalW(HDC dc, ULONG size, OUTLINETEXTMETRICW *otm, PVOID tmdiff);
W32KAPI DWORD NTAPI NtGdiGetFontUnicodeRanges(HDC dc, LPGLYPHSET set);
W32KAPI ULONG NTAPI NtGdiGetGlyphOutline(HDC dc, WCHAR ch, UINT format, LPGLYPHMETRICS gm, ULONG size, PVOID buffer,
                                         LPMAT2 mat, BOOL ignore_rotation);
W32KAPI DWORD NTAPI NtGdiGetGlyphIndicesW(HDC dc, LPCWSTR text, int count, LPWORD indices, DWORD flags);

W32KAPI BOOL NTAPI NtGdiBeginPath(HDC dc);
W32KAPI BOOL NTAPI NtGdiEndPath(HDC dc);
W32KAPI BOOL NTAPI NtGdiAbortPath(HDC dc);
W32KAPI BOOL NTAPI NtGdiCloseFigure(HDC dc);
W32KAPI BOOL NTAPI NtGdiFlattenPath(HDC dc);
W32KAPI BOOL NTAPI NtGdiFillPath(HDC dc);
W32KAPI BOOL NTAPI NtGdiStrokePath(HDC dc);
W32KAPI BOOL NTAPI NtGdiStrokeAndFillPath(HDC dc);
W32KAPI BOOL NTAPI NtGdiSelectClipPath(HDC dc, int mode);
W32KAPI HRGN NTAPI NtGdiPathToRegion(HDC dc);
W32KAPI int NTAPI NtGdiGetPath(HDC dc, POINT *points, BYTE *types, int count);
W32KAPI BOOL NTAPI NtGdiModifyWorldTransform(HDC dc, const XFORM *xform, DWORD mode);
W32KAPI BOOL NTAPI NtGdiGetTransform(HDC dc, DWORD which, XFORM *xform);
W32KAPI LONG NTAPI NtGdiGetBitmapBits(HBITMAP bitmap, ULONG size, PUCHAR bits);
W32KAPI LONG NTAPI NtGdiSetBitmapBits(HBITMAP bitmap, ULONG size, const UCHAR *bits);
W32KAPI BOOL NTAPI NtGdiSetPixelFormat(HDC dc, int format);

W32KAPI HRGN NTAPI NtGdiCreateRectRgn(int left, int top, int right, int bottom);
W32KAPI HRGN NTAPI NtGdiCreateRoundRectRgn(int left, int top, int right, int bottom, int width, int height);
W32KAPI HRGN NTAPI NtGdiExtCreateRegion(const XFORM *xform, DWORD size, const RGNDATA *data);
W32KAPI int NTAPI NtGdiCombineRgn(HRGN dst, HRGN a, HRGN b, int mode);
W32KAPI BOOL NTAPI NtGdiSetRectRgn(HRGN region, int left, int top, int right, int bottom);
W32KAPI int NTAPI NtGdiOffsetRgn(HRGN region, int x, int y);
W32KAPI int NTAPI NtGdiGetRgnBox(HRGN region, LPRECT rect);
W32KAPI BOOL NTAPI NtGdiPtInRegion(HRGN region, int x, int y);
W32KAPI BOOL NTAPI NtGdiRectInRegion(HRGN region, LPRECT rect);
W32KAPI BOOL NTAPI NtGdiEqualRgn(HRGN a, HRGN b);
W32KAPI DWORD NTAPI NtGdiGetRegionData(HRGN region, DWORD size, LPRGNDATA data);
W32KAPI BOOL NTAPI NtGdiFillRgn(HDC dc, HRGN region, HBRUSH brush);
W32KAPI BOOL NTAPI NtGdiFrameRgn(HDC dc, HRGN region, HBRUSH brush, int width, int height);
W32KAPI BOOL NTAPI NtGdiInvertRgn(HDC dc, HRGN region);
W32KAPI int NTAPI NtGdiExtSelectClipRgn(HDC dc, HRGN region, int mode);
W32KAPI int NTAPI NtGdiGetAppClipBox(HDC dc, LPRECT rect);
W32KAPI int NTAPI NtGdiGetRandomRgn(HDC dc, HRGN region, int which);
W32KAPI int NTAPI NtGdiIntersectClipRect(HDC dc, int left, int top, int right, int bottom);
W32KAPI int NTAPI NtGdiExcludeClipRect(HDC dc, int left, int top, int right, int bottom);
W32KAPI int NTAPI NtGdiSetMetaRgn(HDC dc);
W32KAPI HDC NTAPI NtGdiCreateMetafileDC(HDC ref);
W32KAPI BOOL NTAPI NtGdiMimeSetExtent(HDC dc, UINT which, int x, int y, LPSIZE old);
W32KAPI BOOL NTAPI NtGdiSetVirtualResolution(HDC dc, int px, int py, int mx, int my);
W32KAPI BOOL NTAPI NtGdiSetMiterLimit(HDC dc, DWORD limit, DWORD *old);
W32KAPI BOOL NTAPI NtGdiGetMiterLimit(HDC dc, DWORD *limit);
W32KAPI BOOL NTAPI NtGdiWidenPath(HDC dc);
W32KAPI HPEN NTAPI NtGdiExtCreatePen(ULONG style, ULONG width, ULONG brush_style, ULONG color, ULONG_PTR hatch,
                                     ULONG count, const DWORD *styles);

#endif
