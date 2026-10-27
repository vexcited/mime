/* the menu objects, shared by menu.c and the menu bar in menubar.c. */
#ifndef MIME_WIN32K_MENU_H
#define MIME_WIN32K_MENU_H

#include "win32k.h"

typedef struct MENU_ITEM {
    UINT Type;
    UINT State;
    BOOLEAN StateExplicit;
    UINT Id;
    HMENU SubMenu;
    HBITMAP Checked;
    HBITMAP Unchecked;
    HBITMAP Bitmap;
    ULONG_PTR Data;
    PWSTR Text;
    ULONG TextLength;
} MENU_ITEM;

typedef struct MENU {
    USER_OBJECT Header;
    LIST_ENTRY Links;
    HANDLE OwnerPid;
    BOOLEAN Popup;
    WND *SystemOf; /* window that owns this system menu. */
    ULONG Count;
    ULONG Capacity;
    MENU_ITEM *Items;
    DWORD Style;
    UINT MaxHeight;
    HBRUSH Background;
    DWORD HelpId;
    ULONG_PTR MenuData;
    ULONG Serial; /* changes with every edit, so a menu bar layout knows it is stale. */
    LONG Users;   /* windows that have it as their menu bar. */
} MENU;

MENU *MenuLookup(HMENU handle);

/* menubar.c. */

/* the layout of a menu bar for one window, relative to the window. */
typedef struct MENUBAR {
    HMENU Menu;
    ULONG Serial;
    UINT Dpi;
    LONG X, Y, Width; /* the bar rectangle without its rows, relative to the window. */
    LONG Rows;        /* pixels of the rows, the line under them not counted. */
    ULONG Count;
    RECT *Items;      /* relative to the window. */
} MENUBAR;

#endif
