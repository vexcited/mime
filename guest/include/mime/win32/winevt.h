/* the windows event log api, winevt.h. */
#ifndef MIME_WIN32_WINEVT_H
#define MIME_WIN32_WINEVT_H

#include <mime/win32/winbase.h>

#ifndef WINEVTAPI
#define WINEVTAPI DECLSPEC_IMPORT
#endif

typedef HANDLE EVT_HANDLE, *PEVT_HANDLE;

#define ERROR_EVT_INVALID_CHANNEL_PATH 15000L
#define ERROR_EVT_INVALID_QUERY 15001L
#define ERROR_EVT_CHANNEL_NOT_FOUND 15007L

typedef enum _EVT_QUERY_FLAGS {
    EvtQueryChannelPath = 0x1,
    EvtQueryFilePath = 0x2,
    EvtQueryForwardDirection = 0x100,
    EvtQueryReverseDirection = 0x200,
    EvtQueryTolerateQueryErrors = 0x1000,
} EVT_QUERY_FLAGS;

typedef enum _EVT_RENDER_CONTEXT_FLAGS {
    EvtRenderContextValues = 0,
    EvtRenderContextSystem = 1,
    EvtRenderContextUser = 2,
} EVT_RENDER_CONTEXT_FLAGS;

typedef enum _EVT_RENDER_FLAGS {
    EvtRenderEventValues = 0,
    EvtRenderEventXml = 1,
    EvtRenderBookmark = 2,
} EVT_RENDER_FLAGS;

typedef enum _EVT_SUBSCRIBE_FLAGS {
    EvtSubscribeToFutureEvents = 1,
    EvtSubscribeStartAtOldestRecord = 2,
    EvtSubscribeStartAfterBookmark = 3,
    EvtSubscribeOriginMask = 3,
    EvtSubscribeTolerateQueryErrors = 0x1000,
    EvtSubscribeStrict = 0x10000,
} EVT_SUBSCRIBE_FLAGS;

typedef DWORD(WINAPI *EVT_SUBSCRIBE_CALLBACK)(int action, PVOID context, EVT_HANDLE event);

WINEVTAPI EVT_HANDLE WINAPI EvtQuery(EVT_HANDLE session, LPCWSTR path, LPCWSTR query, DWORD flags);
WINEVTAPI BOOL WINAPI EvtNext(EVT_HANDLE results, DWORD size, PEVT_HANDLE events, DWORD timeout, DWORD flags,
                              PDWORD returned);
WINEVTAPI BOOL WINAPI EvtClose(EVT_HANDLE object);
WINEVTAPI EVT_HANDLE WINAPI EvtCreateRenderContext(DWORD count, LPCWSTR *paths, DWORD flags);
WINEVTAPI BOOL WINAPI EvtRender(EVT_HANDLE context, EVT_HANDLE fragment, DWORD flags, DWORD size, PVOID buffer,
                                PDWORD used, PDWORD count);
WINEVTAPI EVT_HANDLE WINAPI EvtSubscribe(EVT_HANDLE session, HANDLE event, LPCWSTR path, LPCWSTR query,
                                         EVT_HANDLE bookmark, PVOID context, EVT_SUBSCRIBE_CALLBACK callback,
                                         DWORD flags);
WINEVTAPI EVT_HANDLE WINAPI EvtCreateBookmark(LPCWSTR xml);

#endif
