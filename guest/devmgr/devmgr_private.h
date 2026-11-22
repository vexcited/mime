#ifndef MIME_DEVMGR_PRIVATE_H
#define MIME_DEVMGR_PRIVATE_H

#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/commctrl.h>
#include <mime/win32/setupapi.h>
#include <mime/win32/cfgmgr32.h>
#include <mime/win32/uxtheme.h>
#include <mime/win32/prsht.h>

/* a control of a dialog template: a class atom, or the name of a class when the atom is 0. */
typedef struct DM_ITEM {
    WORD Id, Class;
    DWORD Style;
    short X, Y, Width, Height;
    const WCHAR *Text;
    const WCHAR *ClassName;
} DM_ITEM;

#define DM_BUTTON 0x0080
#define DM_EDIT 0x0081
#define DM_STATIC 0x0082
#define DM_COMBOBOX 0x0085

extern HINSTANCE g_instance;

/* a DLGTEMPLATEEX in MS Shell Dlg 8, freed with HeapFree. */
void *DmBuildTemplate(DWORD style, short cx, short cy, const WCHAR *title, const DM_ITEM *items, DWORD count);
int WINAPI DevicePropertiesExW(HWND parent, const WCHAR *machine, const WCHAR *id, DWORD flags, BOOL show_tree);

#endif
