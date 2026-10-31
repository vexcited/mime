/* wevtapi internals shared by its source files. */
#ifndef MIME_WEVTAPI_PRIVATE_H
#define MIME_WEVTAPI_PRIVATE_H

#define WINEVTAPI
#include <mime/win32/winevt.h>

#define RPC_S_SERVER_UNAVAILABLE 1722L
#define ERROR_INVALID_OPERATION 4317L
#define MAX_OBJECTS 4096

typedef enum {
    OBJ_FREE,
    OBJ_RESULTS,
    OBJ_CONTEXT,
    OBJ_SUBSCRIPTION,
    OBJ_BOOKMARK,
    OBJ_LOG,
    OBJ_SESSION,
    OBJ_CHANNEL_ENUM,
    OBJ_PUBLISHER_ENUM,
    OBJ_CHANNEL_CONFIG
} KIND;

typedef struct {
    KIND kind;
    BOOL remote;
    /* the path a query, subscription or log was opened on, and whether it is a file. */
    LPWSTR path;
    BOOL file;
    /* the position of an enumeration, the channel of a configuration. */
    DWORD index;
    int channel;
    void *data;
} OBJECT;

EVT_HANDLE EvtCreateObject(KIND kind, BOOL remote);
OBJECT *EvtLookupObject(EVT_HANDLE handle, KIND kind);
BOOL EvtFail(DWORD error);
EVT_HANDLE EvtFailHandle(DWORD error);
/* the index of a channel the machine has, or -1. */
int EvtFindChannel(LPCWSTR path);
DWORD EvtCheckSession(EVT_HANDLE session);
DWORD EvtCheckSource(LPCWSTR path, BOOL file);

#define CHANNEL_COUNT 5
extern const LPCWSTR g_evt_channels[CHANNEL_COUNT];

typedef struct {
    union {
        ULONGLONG UInt64Val;
        BOOL BooleanVal;
        ULONG UInt32Val;
        PVOID Pointer;
    };
    DWORD Count;
    DWORD Type;
} EVT_VARIANT;

#endif
