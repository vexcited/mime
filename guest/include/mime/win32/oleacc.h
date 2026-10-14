/*
 * active accessibility: the object proxies of oleacc.dll.
 */
#ifndef MIME_WIN32_OLEACC_H
#define MIME_WIN32_OLEACC_H

#include <mime/win32/objbase.h>

#ifndef OLEACCAPI
#define OLEACCAPI DECLSPEC_IMPORT
#endif

#define OBJID_WINDOW ((LONG)0x00000000)
#define OBJID_CLIENT ((LONG)0xFFFFFFFC)

OLEACCAPI LRESULT WINAPI LresultFromObject(REFIID riid, WPARAM wparam, IUnknown *object);
OLEACCAPI HRESULT WINAPI AccessibleObjectFromWindow(HWND hwnd, DWORD object_id, REFIID riid, void **object);
OLEACCAPI HRESULT WINAPI CreateStdAccessibleObject(HWND hwnd, LONG object_id, REFIID riid, void **object);

#endif
