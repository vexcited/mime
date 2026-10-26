#ifndef MIME_EXPLORERFRAME_PRIVATE_H
#define MIME_EXPLORERFRAME_PRIVATE_H

#include <mime/win32/shobjidl_ui.h>

/* not declared by winuser.h yet. */
WINUSERAPI HICON WINAPI CopyIcon(HICON icon);

IClassFactory *TaskbarListFactory(void);
IClassFactory *ShellWindowsFactory(void);

#endif
