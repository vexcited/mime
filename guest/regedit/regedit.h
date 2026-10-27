#ifndef MIME_REGEDIT_H
#define MIME_REGEDIT_H

#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>

#define REGEDIT_TITLE L"Registry Editor"

/* a box with the title of the program; the answer, or 0. */
int RegeditBox(HWND window, PCWSTR text, UINT flags);

/* merges a script file; without silent a box tells how it went, and confirm asks first. */
void RegeditImportFile(PCWSTR name, BOOL silent, BOOL confirm);

/* writes a key and everything below it, or the whole registry when the name is NULL. */
void RegeditExport(PCWSTR file, PCWSTR key_name, BOOL ansi);

/* true after /m: the program does not hand its work to an editor that is already running. */
extern BOOL g_allow_multiple;

/* an editor window that is open reads the registry again. */
void RegeditRefresh(void);

/* the editor window, until it is closed. */
void RegeditRun(void);

#endif
