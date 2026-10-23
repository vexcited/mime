/*
 * what the color, font and print dialogs share: in-memory dialog templates, program supplied templates,
 * registered messages and ANSI to wide string helpers.
 */
#ifndef MIME_COMDLG32_DLGBASE_H
#define MIME_COMDLG32_DLGBASE_H

#include "comdlg32_private.h"

/* dialog template class atoms. */
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

/* a captioned dialog template in Segoe UI 9 with the given controls. free it with HeapFree. */
LPDLGTEMPLATEW DlgBuild(LPCWSTR title, DWORD style, int width, int height, const CTL *controls, UINT count);

/* the template a program names with the flags. failures are reported as CDERR_ codes. */
typedef struct _DLGSOURCE {
    DWORD Flags;
    DWORD TemplateFlag, HandleFlag;
    HINSTANCE Instance;
    const void *Name;
    BOOL Ansi;
} DLGSOURCE;

/* returns the locked template, or nullptr with the extended error in error. */
const void *DlgFindTemplate(const DLGSOURCE *source, DWORD *error);

/* registered on first use and cached. */
UINT DlgMessage(UINT *cache, LPCWSTR name);

/* FALSE with CDERR_NOHOOK when the hook flag is set without a hook. */
BOOL DlgHookValid(DWORD flags, DWORD hook_flag, const void *hook, DWORD *error);

/* ANSI to wide for a fixed buffer, and back. */
void DlgToWide(LPCSTR ansi, LPWSTR wide, int chars);
void DlgToAnsi(LPCWSTR wide, LPSTR ansi, int chars);

void DlgFillRect(HDC dc, const RECT *rect, COLORREF color);

#endif
