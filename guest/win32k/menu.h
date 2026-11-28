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
    /* the fields that are const are the ones the desktop heap shows: they are written through MenuEdit. */
    const BOOLEAN Popup;
    WND *SystemOf; /* window that owns this system menu. */
    const ULONG Count;
    ULONG Capacity;
    MENU_ITEM *const Items; /* what changes an item marks the menu too, see touch of menu.c. */
    const DWORD Style;
    const UINT MaxHeight;
    const HBRUSH Background;
    const DWORD HelpId;
    const ULONG_PTR MenuData;
    ULONG Serial; /* changes with every edit, so a menu bar layout knows it is stale. */
    LONG Users;   /* windows that have it as their menu bar. */
    ULONG DeskItems, DeskItemsSize; /* its items and their text in the desktop heap, deskheap.c. */
} MENU;

MENU *MenuLookup(HMENU handle);
#define MenuEdit(m, field) (DeskTouchObject(&(m)->Header), (typeof_unqual((m)->field) *)&(m)->field)

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
