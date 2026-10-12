/*
 * comctl32 internal interface shared by its source files.
 */
#ifndef MIME_COMCTL32_PRIVATE_H
#define MIME_COMCTL32_PRIVATE_H

#define WINCOMMCTRLAPI DECLSPEC_EXPORT
#include <mime/win32/commctrl.h>
#include <mime/win32/winbase.h>
#include <rtl.h>

/* user32 exports that winuser.h does not declare yet. */
WINUSERAPI DWORD WINAPI GetWindowThreadProcessId(HWND hwnd, LPDWORD process);

void SubclassProcessAttach(void);

#endif
