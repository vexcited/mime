#pragma once
#include <mime/win32/winbase.h>

/*
 * what shell32 26300 sends the taskbar window, class Shell_TrayWnd, in a WM_COPYDATA: Shell_NotifyIconW with
 * dwData 1 and Shell_NotifyIconGetRect with dwData 3. the icon data is the NOTIFYICONDATAW of a 32-bit program
 * whatever the program is, so its handles are 32 bits wide.
 */
#define TRAY_SIGNATURE 0x34753423u
#define TRAY_COPYDATA_NOTIFY 1
#define TRAY_COPYDATA_RECT 3
#define TRAY_RECT_POSITION 1
#define TRAY_RECT_SIZE 2

typedef struct TRAY_DATA {
    ULONG Signature;
    ULONG Message;
    ULONG Size; /* 956, of the icon data that starts here. */
    ULONG Window;
    ULONG Id;
    ULONG Flags;
    ULONG Callback;
    ULONG Icon;
    WCHAR Tip[128];
    ULONG State;
    ULONG StateMask;
    WCHAR Info[256];
    ULONG Version; /* uTimeout of a balloon, uVersion of NIM_SETVERSION. */
    WCHAR InfoTitle[64];
    ULONG InfoFlags;
    GUID Guid;
    ULONG BalloonIcon;
    WCHAR Path[260]; /* of the program, for NIM_ADD. */
} TRAY_DATA;

typedef struct TRAY_RECT_DATA {
    ULONG Signature;
    ULONG Kind; /* TRAY_RECT_SIZE answers the width and height in a long, TRAY_RECT_POSITION the left and top. */
    ULONG Size; /* 32 */
    ULONG Reserved;
    ULONG Window;
    ULONG Id;
    GUID Guid;
} TRAY_RECT_DATA;
