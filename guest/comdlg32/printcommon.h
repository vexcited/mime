/*
 * what the print and page setup dialogs share: the printers and their settings, reached through winspool.drv
 * which comdlg32 does not import, and the device mode and device names handles the structures carry.
 */
#ifndef MIME_COMDLG32_PRINTCOMMON_H
#define MIME_COMDLG32_PRINTCOMMON_H

#include "dlgbase.h"
#include "dlgids.h"

typedef struct _SPOOLFN {
    BOOL Loaded, Ok;
    BOOL(WINAPI *EnumPrinters)(DWORD, LPWSTR, DWORD, LPBYTE, DWORD, LPDWORD, LPDWORD);
    BOOL(WINAPI *OpenPrinter)(LPWSTR, LPHANDLE, LPVOID);
    BOOL(WINAPI *ClosePrinter)(HANDLE);
    BOOL(WINAPI *GetPrinter)(HANDLE, DWORD, LPBYTE, DWORD, LPDWORD);
    LONG(WINAPI *DocumentProperties)(HWND, HANDLE, LPWSTR, PDEVMODEW, PDEVMODEW, DWORD);
    int(WINAPI *DeviceCapabilities)(LPCWSTR, LPCWSTR, WORD, LPWSTR, const DEVMODEW *);
    BOOL(WINAPI *GetDefaultPrinter)(LPWSTR, LPDWORD);
} SPOOLFN;

/* winspool's functions, loaded on first use; the pointers are null when there is no winspool.drv. */
const SPOOLFN *PrnSpool(void);

typedef struct _PRN_ITEM {
    WCHAR Name[128];
    WCHAR Driver[128];
    WCHAR Port[64];
    WCHAR Comment[256];
    WCHAR Location[256];
    DWORD Attributes;
    DWORD Jobs;
} PRN_ITEM;

typedef struct _PRN_LIST {
    PRN_ITEM *Items;
    int Count;
    int Default;
} PRN_LIST;

BOOL PrnEnumerate(PRN_LIST *list);
void PrnFreeList(PRN_LIST *list);
int PrnFind(const PRN_LIST *list, LPCWSTR name);

/* the settings of a printer: the defaults, with a device mode merged in when one is given. nullptr on failure. */
DEVMODEW *PrnDevmode(HWND owner, const PRN_ITEM *printer, const DEVMODEW *input);
/* the size of a capability of a printer, and the capability as a number. */
int PrnCapability(const PRN_ITEM *printer, WORD capability);

/* handles of the structures: movable memory a program frees with GlobalFree. */
HGLOBAL PrnNewDevmodeHandle(const DEVMODEW *devmode);
HGLOBAL PrnNewDevnamesHandle(const PRN_ITEM *printer, BOOL is_default, BOOL to_file);
/* the device mode in a handle, as a copy to free with HeapFree; nullptr when there is none. */
DEVMODEW *PrnCopyDevmode(HGLOBAL handle);
/* the printer a device names handle stands for, FALSE when its strings are not a printer of the list. */
BOOL PrnDevnamesPrinter(HGLOBAL handle, const PRN_LIST *list, int *index, BOOL *to_file);

/* the same two in ANSI, for the functions of that character set. */
HGLOBAL PrnDevmodeToAnsi(HGLOBAL wide);
HGLOBAL PrnDevmodeFromAnsi(HGLOBAL ansi);
HGLOBAL PrnDevnamesToAnsi(HGLOBAL wide);
HGLOBAL PrnDevnamesFromAnsi(HGLOBAL ansi);

/* a device context of the printer with the settings; information only when asked. */
HDC PrnCreateDC(const PRN_ITEM *printer, const DEVMODEW *devmode, BOOL info);

#endif
