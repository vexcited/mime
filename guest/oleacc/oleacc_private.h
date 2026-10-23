/* oleacc.dll internals shared by its source files. */
#ifndef MIME_OLEACC_PRIVATE_H
#define MIME_OLEACC_PRIVATE_H

#include <mime/win32/oleacc.h>

/* text.c: role and state words and the proxies' strings, by the ids of windows' resource file. */
const WCHAR *OaRoleName(DWORD role);
const WCHAR *OaStateName(DWORD state);
const WCHAR *OaString(DWORD id);

/* proxy.c: the proxy for one object of a window. */
HRESULT OaCreateProxy(HWND hwnd, LONG object, REFIID riid, void **object_out);

#endif
