#ifndef MIME_EXPLORERFRAME_PRIVATE_H
#define MIME_EXPLORERFRAME_PRIVATE_H

#include <mime/win32/shobjidl_ui.h>

/* user32 exports that winuser.h does not declare yet. */
WINUSERAPI HICON WINAPI CopyIcon(HICON icon);

/* the factory of the taskbar list class. */
IClassFactory *TaskbarListFactory(void);

#endif
