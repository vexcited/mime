#ifndef MIME_WINBRAND_PRIVATE_H
#define MIME_WINBRAND_PRIVATE_H

#include <mime/nt.h>
#include <mime/ntapi.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>
#include <mime/win32/wingdi.h>
#include <mime/win32/winnls.h>
#include <mime/win32/winuser.h>

#define BRAND_STRING_LONG 12
#define BRAND_STRING_PRODUCT 13
#define BRAND_RT_STRING MAKEINTRESOURCEW(6)
#define BRAND_RT_RCDATA MAKEINTRESOURCEW(10)

extern HINSTANCE g_instance;

/* the edition the kernel's licence names for branding, 0 when it names none. */
DWORD brand_edition(void);
HMODULE brand_open(const WCHAR *brand, ULONG_PTR type, ULONG_PTR id);
HRESULT brand_remap(HMODULE module, const WCHAR *brand, UINT id, DWORD edition, UINT *mapped);

int WINAPI BrandingLoadStringForEdition(const WCHAR *brand, UINT id, DWORD edition, WCHAR *buffer, int count,
                                        DWORD flags);
HRESULT WINAPI GetInstalledEULAPath(WCHAR **path);

#endif
