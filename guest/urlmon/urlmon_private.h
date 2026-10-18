/*
 * urlmon internals: what the download, moniker, uri and parsing files share.
 */
#pragma once
#ifndef MIME_BUILD_URLMON
#define MIME_BUILD_URLMON
#endif
#include <mime/win32/urlmon.h>
#include <mime/win32/wininet.h>
#include <mime/win32/shlwapi.h>
#include <mime/win32/winnls.h>
#include <mime/win32/winuser.h>

void *UrlAlloc(SIZE_T size);
void *UrlAllocZero(SIZE_T size);
void *UrlRealloc(void *memory, SIZE_T size);
void UrlFree(void *memory);
PWSTR UrlDuplicate(LPCWSTR text);
PWSTR UrlDuplicateN(LPCWSTR text, DWORD length);
PWSTR UrlWiden(LPCSTR text);
/* the session's user agent, in the ansi code page. */
const char *UrlUserAgent(void);

/* download.c */
/* a growable stream over memory, read from the start and written at the end. */
IStream *UrlCreateDataStream(void);
HRESULT UrlStreamAppend(IStream *stream, const void *data, DWORD size);

enum { URL_SINK_FILE, URL_SINK_STREAM, URL_SINK_PUSH, URL_SINK_CACHE };

/* binds a url synchronously, reporting to callback, into the sink the kind names. */
HRESULT UrlDownload(LPCWSTR url, DWORD kind, DWORD bindf, IBindStatusCallback *callback, LPCWSTR file,
                    IStream **stream, PWSTR cache_file, DWORD cache_length);

/* moniker.c */
IBindStatusCallback *UrlBoundCallback(IBindCtx *context);

/* zone.c */
DWORD UrlZoneOf(LPCWSTR url);
