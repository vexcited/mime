#ifndef WINMM_PRIVATE_H
#define WINMM_PRIVATE_H

#include <mime/nt.h>
#include <mime/ntuser.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/mmsystem.h>
#include <mime/win32/objbase.h>

/* error text for MMSYSERR_, WAVERR_ and MIDIERR_ values. */
MMRESULT MmErrorTextW(MMRESULT error, LPWSTR text, UINT size);
MMRESULT MmErrorTextA(MMRESULT error, LPSTR text, UINT size);

/* the module, which DirectInput is created for. */
extern HINSTANCE WinmmModule;
void WinmmJoyDetach(void);

#endif
