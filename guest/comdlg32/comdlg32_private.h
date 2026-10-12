/*
 * comdlg32 internal interface shared by its source files.
 */
#ifndef MIME_COMDLG32_PRIVATE_H
#define MIME_COMDLG32_PRIVATE_H

#define CDAPI DECLSPEC_EXPORT
#include <mime/win32/commdlg.h>
#include <mime/win32/winbase.h>
#include <rtl.h>

void DlgProcessAttach(void);

#endif
