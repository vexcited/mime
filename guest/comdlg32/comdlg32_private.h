/* comdlg32 internals shared by its source files. */
#ifndef MIME_COMDLG32_PRIVATE_H
#define MIME_COMDLG32_PRIVATE_H

#define CDAPI DECLSPEC_EXPORT
#include <mime/win32/commdlg.h>
#include <mime/win32/winbase.h>
#include <mime/win32/shobjidl.h>
#include <rtl.h>

#ifndef CLASS_E_CLASSNOTAVAILABLE
#define CLASS_E_CLASSNOTAVAILABLE ((HRESULT)0x80040111L)
#endif

extern HINSTANCE g_comdlg32;

HRESULT ItemDialogCreate(BOOL save, IUnknown *outer, REFIID riid, void **object);

void DlgProcessAttach(void);
void DlgSetError(DWORD error);

#endif
