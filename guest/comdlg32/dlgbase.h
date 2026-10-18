/*
 * what the color, font and print dialogs share: dialog templates made in memory, the templates a program
 * supplies, registered messages and strings between the ANSI and the wide versions.
 */
#ifndef MIME_COMDLG32_DLGBASE_H
#define MIME_COMDLG32_DLGBASE_H

#include "comdlg32_private.h"

/* the classes of a dialog template. */
#define CTL_BUTTON 0x0080
#define CTL_EDIT 0x0081
#define CTL_STATIC 0x0082
#define CTL_LISTBOX 0x0083
#define CTL_SCROLLBAR 0x0084
#define CTL_COMBOBOX 0x0085

typedef struct _CTL {
    WORD Class;
    LPCWSTR Text;
    WORD Id;
    DWORD Style;
    short X, Y, W, H;
} CTL;

/* a template of a captioned dialog in Segoe UI 9 with the given controls; free it with HeapFree. */
LPDLGTEMPLATEW DlgBuild(LPCWSTR title, DWORD style, int width, int height, const CTL *controls, UINT count);

/*
 * the template of a dialog: the one a program names with the flags (an error from the CDERR_ codes when it
 * cannot be had), or the one built by the caller. built is set when the result has to be freed.
 */
typedef struct _DLGSOURCE {
    DWORD Flags;
    DWORD TemplateFlag, HandleFlag;
    HINSTANCE Instance;
    const void *Name;
    BOOL Ansi;
} DLGSOURCE;

/* returns the locked template or nullptr with the extended error in error. */
const void *DlgFindTemplate(const DLGSOURCE *source, DWORD *error);

/* a message registered on first use. */
UINT DlgMessage(UINT *cache, LPCWSTR name);

/* the hook procedure a dialog calls before the dialog's own procedure: nullptr when the flag is off. */
BOOL DlgHookValid(DWORD flags, DWORD hook_flag, const void *hook, DWORD *error);

/* ANSI to wide for a fixed buffer, and back. */
void DlgToWide(LPCSTR ansi, LPWSTR wide, int chars);
void DlgToAnsi(LPCWSTR wide, LPSTR ansi, int chars);

/* a bit of GDI the dialogs share. */
void DlgFillRect(HDC dc, const RECT *rect, COLORREF color);

#endif
