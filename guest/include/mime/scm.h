/*
 * the transport between the service control manager and what talks to it. windows speaks the svcctl rpc
 * interface to services.exe, over alpc on a machine and over the named pipe \pipe\svcctl from another;
 * mime has no rpc marshaling, so the manager and advapi32 exchange their own messages on that same pipe.
 * every message is a header and a body of little endian fields, none of which depends on the size of a
 * pointer, so a 32-bit client and a 64-bit manager understand each other. see docs/services.md.
 *
 * a request is: { magic, size, operation, flags } and its arguments. a reply is { magic, size, error, flags }
 * and its results. strings are a count of characters and the characters, utf-16, without a terminator;
 * 0xFFFFFFFF stands for a null string.
 */
#ifndef MIME_SCM_H
#define MIME_SCM_H

#include <mime/win32/winbase.h>

#define SCM_PIPE_NAME L"\\\\.\\pipe\\svcctl"
#define SCM_START_EVENT L"Global\\SvcctrlStartEvent_A3752DX"
#define SCM_AUTOSTART_EVENT L"Global\\SC_AutoStartComplete"
#define SCM_MAGIC 0x314D4353 /* "SCM1" */
#define SCM_MAX_MESSAGE (4u << 20)
#define SCM_NULL_STRING 0xFFFFFFFFu

/* the operations of the manager, what the client functions turn into. */
enum {
    SCM_OP_OPEN_MANAGER = 1,
    SCM_OP_OPEN_SERVICE,
    SCM_OP_CLOSE_HANDLE,
    SCM_OP_CREATE_SERVICE,
    SCM_OP_DELETE_SERVICE,
    SCM_OP_CHANGE_CONFIG,
    SCM_OP_CHANGE_CONFIG2,
    SCM_OP_QUERY_CONFIG,
    SCM_OP_QUERY_CONFIG2,
    SCM_OP_QUERY_STATUS,
    SCM_OP_ENUM_SERVICES,
    SCM_OP_ENUM_DEPENDENTS,
    SCM_OP_START_SERVICE,
    SCM_OP_CONTROL_SERVICE,
    SCM_OP_NOTIFY_STATUS_CHANGE,
    SCM_OP_QUERY_SECURITY,
    SCM_OP_SET_SECURITY,
    SCM_OP_LOCK_DATABASE,
    SCM_OP_UNLOCK_DATABASE,
    SCM_OP_QUERY_LOCK_STATUS,
    SCM_OP_GET_DISPLAY_NAME,
    SCM_OP_GET_KEY_NAME,
    SCM_OP_BOOT_CONFIG_STATUS,
    SCM_OP_NOTIFY_ATTACH, /* the connection stays open for completions of notifications */
    /* from a service process. */
    SCM_OP_OPEN_CHANNEL = 64,
    SCM_OP_SET_STATUS,
    SCM_OP_SET_BITS,
    /* what mime adds: the launch of an elevated process, which windows asks of the appinfo service. */
    SCM_OP_LAUNCH_ELEVATED = 96,
};

/* the controls the manager sends down a service process' channel, besides SERVICE_CONTROL_*. */
#define SCM_CTRL_START_SHARE 80 /* start a service of a process that hosts several: name, arguments */
#define SCM_CTRL_START_OWN 81   /* start the service of a process that hosts one, whatever it is called */
#define SCM_CTRL_QUIT 32        /* with no name: every service is done, the dispatcher returns */

/* the header of every message. */
typedef struct SCM_HEADER {
    UINT32 Magic;
    UINT32 Size; /* of the whole message */
    UINT32 Code; /* the operation of a request, the win32 error of a reply */
    UINT32 Flags;
} SCM_HEADER;

/* ------------------------------------------------------------------ writing and reading fields */

typedef struct SCM_WRITER {
    UCHAR *Data;
    UINT32 Size, Capacity;
    BOOLEAN Failed;
} SCM_WRITER;

typedef struct SCM_READER {
    const UCHAR *Data;
    UINT32 Size, Position;
    BOOLEAN Failed;
} SCM_READER;

#define SCM_ALLOC(size) HeapAlloc(GetProcessHeap(), 0, (size))
#define SCM_FREE(pointer) HeapFree(GetProcessHeap(), 0, (pointer))
#define SCM_REALLOC(pointer, size) HeapReAlloc(GetProcessHeap(), 0, (pointer), (size))

static inline void ScmWriterInit(SCM_WRITER *w, UINT32 code)
{
    w->Capacity = 256;
    w->Data = SCM_ALLOC(w->Capacity);
    w->Size = sizeof(SCM_HEADER);
    w->Failed = w->Data == nullptr;
    if (w->Data) {
        SCM_HEADER *h = (SCM_HEADER *)w->Data;
        h->Magic = SCM_MAGIC;
        h->Size = sizeof(*h);
        h->Code = code;
        h->Flags = 0;
    }
}

static inline UCHAR *ScmWriterReserve(SCM_WRITER *w, UINT32 bytes)
{
    if (w->Failed)
        return nullptr;
    if (bytes > SCM_MAX_MESSAGE || w->Size > SCM_MAX_MESSAGE - bytes) {
        w->Failed = TRUE;
        return nullptr;
    }
    if (w->Size + bytes > w->Capacity) {
        UINT32 capacity = w->Capacity;
        while (capacity < w->Size + bytes)
            capacity *= 2;
        UCHAR *grown = SCM_REALLOC(w->Data, capacity);
        if (!grown) {
            w->Failed = TRUE;
            return nullptr;
        }
        w->Data = grown;
        w->Capacity = capacity;
    }
    UCHAR *at = w->Data + w->Size;
    w->Size += bytes;
    return at;
}

static inline void ScmPutU32(SCM_WRITER *w, UINT32 value)
{
    UCHAR *at = ScmWriterReserve(w, 4);
    if (at)
        memcpy(at, &value, 4);
}

static inline void ScmPutU64(SCM_WRITER *w, UINT64 value)
{
    UCHAR *at = ScmWriterReserve(w, 8);
    if (at)
        memcpy(at, &value, 8);
}

static inline void ScmPutBytes(SCM_WRITER *w, const void *bytes, UINT32 length)
{
    ScmPutU32(w, length);
    UCHAR *at = ScmWriterReserve(w, (length + 3) & ~3u);
    if (at && length) {
        memcpy(at, bytes, length);
        for (UINT32 i = length; i & 3; i++)
            at[i] = 0;
    }
}

/* characters of a string, null for a null pointer. */
static inline void ScmPutString(SCM_WRITER *w, const WCHAR *s)
{
    if (!s) {
        ScmPutU32(w, SCM_NULL_STRING);
        return;
    }
    UINT32 n = (UINT32)wcslen(s);
    ScmPutU32(w, n);
    UCHAR *at = ScmWriterReserve(w, (n * 2 + 3) & ~3u);
    if (at) {
        memcpy(at, s, n * 2);
        if ((n * 2) & 3)
            at[n * 2] = at[n * 2 + 1] = 0;
    }
}

/* a string of a given length, which may hold nulls, as a multi sz does. */
static inline void ScmPutChars(SCM_WRITER *w, const WCHAR *s, UINT32 chars)
{
    if (!s) {
        ScmPutU32(w, SCM_NULL_STRING);
        return;
    }
    ScmPutU32(w, chars);
    UCHAR *at = ScmWriterReserve(w, (chars * 2 + 3) & ~3u);
    if (at) {
        memcpy(at, s, chars * 2);
        if ((chars * 2) & 3)
            at[chars * 2] = at[chars * 2 + 1] = 0;
    }
}

/* seals the message: its size goes in the header. */
static inline UCHAR *ScmWriterFinish(SCM_WRITER *w, UINT32 *size)
{
    if (w->Failed || !w->Data)
        return nullptr;
    ((SCM_HEADER *)w->Data)->Size = w->Size;
    *size = w->Size;
    return w->Data;
}

static inline void ScmReaderInit(SCM_READER *r, const void *message, UINT32 size)
{
    r->Data = message;
    r->Size = size;
    r->Position = sizeof(SCM_HEADER);
    r->Failed = size < sizeof(SCM_HEADER);
}

static inline UINT32 ScmGetU32(SCM_READER *r)
{
    UINT32 v = 0;
    if (r->Failed || r->Size - r->Position < 4) {
        r->Failed = TRUE;
        return 0;
    }
    memcpy(&v, r->Data + r->Position, 4);
    r->Position += 4;
    return v;
}

static inline UINT64 ScmGetU64(SCM_READER *r)
{
    UINT64 v = 0;
    if (r->Failed || r->Size - r->Position < 8) {
        r->Failed = TRUE;
        return 0;
    }
    memcpy(&v, r->Data + r->Position, 8);
    r->Position += 8;
    return v;
}

/* a blob in place; null with a length of 0 when it is empty. */
static inline const void *ScmGetBytes(SCM_READER *r, UINT32 *length)
{
    UINT32 n = ScmGetU32(r);
    *length = 0;
    if (r->Failed)
        return nullptr;
    UINT32 padded = (n + 3) & ~3u;
    if (padded < n || r->Size - r->Position < padded) {
        r->Failed = TRUE;
        return nullptr;
    }
    const void *at = r->Data + r->Position;
    r->Position += padded;
    *length = n;
    return at;
}

/* a string in place, not terminated: its characters and their count. a null string has a null pointer. */
static inline const WCHAR *ScmGetChars(SCM_READER *r, UINT32 *chars)
{
    UINT32 n = ScmGetU32(r);
    *chars = 0;
    if (r->Failed)
        return nullptr;
    if (n == SCM_NULL_STRING)
        return nullptr;
    UINT32 bytes = n * 2;
    UINT32 padded = (bytes + 3) & ~3u;
    if (n > SCM_MAX_MESSAGE / 2 || r->Size - r->Position < padded) {
        r->Failed = TRUE;
        return nullptr;
    }
    const WCHAR *at = (const WCHAR *)(r->Data + r->Position);
    r->Position += padded;
    *chars = n;
    return at;
}

/* the header of a message read whole, checked. */
static inline BOOLEAN ScmCheckMessage(const void *message, UINT32 received)
{
    const SCM_HEADER *h = message;
    return received >= sizeof(*h) && h->Magic == SCM_MAGIC && h->Size == received;
}

#endif
