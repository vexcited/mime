/* what the parts of winmm share. */
#ifndef WINMM_PRIVATE_H
#define WINMM_PRIVATE_H

#include <mime/nt.h>
#include <mime/ntuser.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/mmsystem.h>
#include <mime/win32/objbase.h>

/* the text of an MMSYSERR_, WAVERR_ or MIDIERR_ value, as winmm's error functions give it. */
MMRESULT MmErrorTextW(MMRESULT error, LPWSTR text, UINT size);
MMRESULT MmErrorTextA(MMRESULT error, LPSTR text, UINT size);

#endif
