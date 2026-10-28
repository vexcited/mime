/*
 * DDEML, client side only: instances, string handles as global atoms, data handles, and conversations run by a
 * hidden window speaking WM_DDE_* to a server window. WM_DDE_INITIATE goes to every top level window and the first
 * server to acknowledge it is kept. DdeNameService fails, so no program can serve yet.
 */
#include "user32_private.h"

#define WM_DDE_INITIATE 0x03E0
#define WM_DDE_TERMINATE 0x03E1
#define WM_DDE_ADVISE 0x03E2
#define WM_DDE_UNADVISE 0x03E3
#define WM_DDE_ACK 0x03E4
#define WM_DDE_DATA 0x03E5
#define WM_DDE_REQUEST 0x03E6
#define WM_DDE_POKE 0x03E7
#define WM_DDE_EXECUTE 0x03E8

#define DMLERR_NO_ERROR 0
#define DMLERR_ADVACKTIMEOUT 0x4000
#define DMLERR_BUSY 0x4001
#define DMLERR_DATAACKTIMEOUT 0x4002
#define DMLERR_DLL_NOT_INITIALIZED 0x4003
#define DMLERR_DLL_USAGE 0x4004
#define DMLERR_EXECACKTIMEOUT 0x4005
#define DMLERR_INVALIDPARAMETER 0x4006
#define DMLERR_LOW_MEMORY 0x4007
#define DMLERR_MEMORY_ERROR 0x4008
#define DMLERR_NOTPROCESSED 0x4009
#define DMLERR_NO_CONV_ESTABLISHED 0x400A
#define DMLERR_POKEACKTIMEOUT 0x400B
#define DMLERR_POSTMSG_FAILED 0x400C
#define DMLERR_REENTRANCY 0x400D
#define DMLERR_SERVER_DIED 0x400E
#define DMLERR_SYS_ERROR 0x400F
#define DMLERR_UNADVACKTIMEOUT 0x4010
#define DMLERR_UNFOUND_QUEUE_ID 0x4011

#define XTYP_EXECUTE 0x4050
#define XTYP_POKE 0x4090
#define XTYP_REQUEST 0x20B0
#define TIMEOUT_ASYNC 0xFFFFFFFF
#define DDE_FACK 0x8000
#define DDE_FBUSY 0x4000
#define CP_WINUNICODE 1200

DECLARE_HANDLE(HCONV);
DECLARE_HANDLE(HSZ);
DECLARE_HANDLE(HDDEDATA);

typedef HDDEDATA(CALLBACK *PFNCALLBACK)(UINT type, UINT format, HCONV conv, HSZ hsz1, HSZ hsz2, HDDEDATA data,
                                         ULONG_PTR data1, ULONG_PTR data2);

typedef struct DDE_INSTANCE {
    DWORD Id;
    PFNCALLBACK Callback;
    DWORD Flags;
    UINT LastError;
    BOOL Unicode;
} DDE_INSTANCE;

typedef struct DDE_CONV {
    DWORD Magic;
    DDE_INSTANCE *Instance;
    HWND Client, Server;
    ATOM Service, Topic;
    /* the last answer the client window received. */
    UINT Message;
    LPARAM Lparam;
    BOOL Answered;
} DDE_CONV;

typedef struct DDE_DATA {
    DWORD Magic;
    UINT Format;
    ATOM Item;
    DWORD Size;
    BYTE Bytes[];
} DDE_DATA;

#define CONV_MAGIC 0x564E4F43
#define DATA_MAGIC 0x41544144
#define MAX_INSTANCES 32

static DDE_INSTANCE g_instances[MAX_INSTANCES];
static SRWLOCK g_lock = SRWLOCK_INIT;
static ATOM g_client_class;

static DDE_INSTANCE *instance_of(DWORD id)
{
    if (!id || id > MAX_INSTANCES || g_instances[id - 1].Id != id)
        return nullptr;
    return &g_instances[id - 1];
}

static UINT fail(DDE_INSTANCE *instance, UINT error)
{
    if (instance)
        instance->LastError = error;
    return error;
}

static UINT initialize(LPDWORD id, PFNCALLBACK callback, DWORD flags, BOOL unicode)
{
    if (!id)
        return DMLERR_INVALIDPARAMETER;
    AcquireSRWLockExclusive(&g_lock);
    UINT result = DMLERR_NO_ERROR;
    if (*id) {
        /* reinitializing changes the flags of an existing instance. */
        DDE_INSTANCE *i = instance_of(*id);
        if (!i)
            result = DMLERR_INVALIDPARAMETER;
        else
            i->Flags = flags;
    } else {
        result = DMLERR_SYS_ERROR;
        for (DWORD n = 0; n < MAX_INSTANCES; n++) {
            if (!g_instances[n].Id) {
                g_instances[n] = (DDE_INSTANCE){n + 1, callback, flags, DMLERR_NO_ERROR, unicode};
                *id = n + 1;
                result = DMLERR_NO_ERROR;
                break;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    return result;
}

WINUSERAPI UINT WINAPI DdeInitializeW(LPDWORD id, PFNCALLBACK callback, DWORD flags, DWORD reserved)
{
    return initialize(id, callback, flags, TRUE);
}

WINUSERAPI UINT WINAPI DdeInitializeA(LPDWORD id, PFNCALLBACK callback, DWORD flags, DWORD reserved)
{
    return initialize(id, callback, flags, FALSE);
}

WINUSERAPI BOOL WINAPI DdeUninitialize(DWORD id)
{
    AcquireSRWLockExclusive(&g_lock);
    DDE_INSTANCE *i = instance_of(id);
    if (i)
        i->Id = 0;
    ReleaseSRWLockExclusive(&g_lock);
    return i != nullptr;
}

/* transactions complete before the call returns, so there is never one to abandon. */
WINUSERAPI BOOL WINAPI DdeAbandonTransaction(DWORD id, HCONV conversation, DWORD transaction)
{
    AcquireSRWLockShared(&g_lock);
    DDE_INSTANCE *i = instance_of(id);
    ReleaseSRWLockShared(&g_lock);
    return i != nullptr;
}

WINUSERAPI UINT WINAPI DdeGetLastError(DWORD id)
{
    DDE_INSTANCE *i = instance_of(id);
    if (!i)
        return DMLERR_INVALIDPARAMETER;
    UINT error = i->LastError;
    i->LastError = DMLERR_NO_ERROR;
    return error;
}

/* ------------------------------------------------------------------ string handles */

WINUSERAPI HSZ WINAPI DdeCreateStringHandleW(DWORD id, LPCWSTR text, int codepage)
{
    DDE_INSTANCE *i = instance_of(id);
    if (!i)
        return nullptr;
    if (!text || !*text)
        return nullptr;
    ATOM atom = GlobalAddAtomW(text);
    if (!atom)
        fail(i, DMLERR_SYS_ERROR);
    return (HSZ)(ULONG_PTR)atom;
}

WINUSERAPI HSZ WINAPI DdeCreateStringHandleA(DWORD id, LPCSTR text, int codepage)
{
    WCHAR wide[256];
    if (!text)
        return DdeCreateStringHandleW(id, nullptr, CP_WINUNICODE);
    MultiByteToWideChar(CP_ACP, 0, text, -1, wide, RTL_NUMBER_OF(wide));
    return DdeCreateStringHandleW(id, wide, CP_WINUNICODE);
}

WINUSERAPI BOOL WINAPI DdeFreeStringHandle(DWORD id, HSZ hsz)
{
    if (!instance_of(id) || !hsz)
        return FALSE;
    return GlobalDeleteAtom((ATOM)(ULONG_PTR)hsz) == 0;
}

WINUSERAPI BOOL WINAPI DdeKeepStringHandle(DWORD id, HSZ hsz)
{
    WCHAR name[256];
    if (!instance_of(id) || !hsz || !GlobalGetAtomNameW((ATOM)(ULONG_PTR)hsz, name, RTL_NUMBER_OF(name)))
        return FALSE;
    return GlobalAddAtomW(name) != 0;
}

WINUSERAPI DWORD WINAPI DdeQueryStringW(DWORD id, HSZ hsz, LPWSTR text, DWORD max, int codepage)
{
    WCHAR name[256];
    if (!instance_of(id) || !hsz)
        return 0;
    UINT n = GlobalGetAtomNameW((ATOM)(ULONG_PTR)hsz, name, RTL_NUMBER_OF(name));
    if (!text)
        return n;
    if (!max)
        return 0;
    lstrcpynW(text, name, (int)max);
    return (DWORD)lstrlenW(text);
}

WINUSERAPI int WINAPI DdeCmpStringHandles(HSZ a, HSZ b)
{
    WCHAR x[256] = L"", y[256] = L"";
    if (a)
        GlobalGetAtomNameW((ATOM)(ULONG_PTR)a, x, RTL_NUMBER_OF(x));
    if (b)
        GlobalGetAtomNameW((ATOM)(ULONG_PTR)b, y, RTL_NUMBER_OF(y));
    int c = lstrcmpiW(x, y);
    return c < 0 ? -1 : c > 0;
}

/* ------------------------------------------------------------------ data handles */

WINUSERAPI HDDEDATA WINAPI DdeCreateDataHandle(DWORD id, LPBYTE source, DWORD size, DWORD offset, HSZ item,
                                               UINT format, UINT flags)
{
    DDE_INSTANCE *i = instance_of(id);
    if (!i)
        return nullptr;
    DDE_DATA *d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d) + size);
    if (!d) {
        fail(i, DMLERR_MEMORY_ERROR);
        return nullptr;
    }
    d->Magic = DATA_MAGIC;
    d->Format = format;
    d->Item = (ATOM)(ULONG_PTR)item;
    d->Size = size;
    if (source)
        memcpy(d->Bytes, source + offset, size);
    return (HDDEDATA)d;
}

static DDE_DATA *data_of(HDDEDATA handle)
{
    DDE_DATA *d = (DDE_DATA *)handle;
    return d && d->Magic == DATA_MAGIC ? d : nullptr;
}

WINUSERAPI DWORD WINAPI DdeGetData(HDDEDATA handle, LPBYTE out, DWORD max, DWORD offset)
{
    DDE_DATA *d = data_of(handle);
    if (!d || offset > d->Size)
        return 0;
    DWORD n = d->Size - offset;
    if (!out)
        return n;
    if (n > max)
        n = max;
    memcpy(out, d->Bytes + offset, n);
    return n;
}

WINUSERAPI LPBYTE WINAPI DdeAccessData(HDDEDATA handle, LPDWORD size)
{
    DDE_DATA *d = data_of(handle);
    if (!d)
        return nullptr;
    if (size)
        *size = d->Size;
    return d->Bytes;
}

WINUSERAPI BOOL WINAPI DdeUnaccessData(HDDEDATA handle)
{
    return data_of(handle) != nullptr;
}

WINUSERAPI BOOL WINAPI DdeFreeDataHandle(HDDEDATA handle)
{
    DDE_DATA *d = data_of(handle);
    if (!d)
        return FALSE;
    d->Magic = 0;
    HeapFree(GetProcessHeap(), 0, d);
    return TRUE;
}

/* ------------------------------------------------------------------ packed lparams */

/* like 64-bit windows, messages carrying a handle and an atom pack them into global memory, the others keep two words. */
static BOOL packs(UINT message)
{
    return message == WM_DDE_ACK || message == WM_DDE_DATA || message == WM_DDE_POKE || message == WM_DDE_ADVISE;
}

WINUSERAPI LPARAM WINAPI PackDDElParam(UINT message, UINT_PTR low, UINT_PTR high)
{
    if (!packs(message))
        return MAKELPARAM(low, high);
    HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, 2 * sizeof(UINT_PTR));
    UINT_PTR *pair = block ? GlobalLock(block) : nullptr;
    if (!pair)
        return 0;
    pair[0] = low;
    pair[1] = high;
    GlobalUnlock(block);
    return (LPARAM)block;
}

WINUSERAPI BOOL WINAPI UnpackDDElParam(UINT message, LPARAM lparam, UINT_PTR *low, UINT_PTR *high)
{
    if (!packs(message)) {
        if (low)
            *low = LOWORD(lparam);
        if (high)
            *high = HIWORD(lparam);
        return TRUE;
    }
    UINT_PTR *pair = lparam ? GlobalLock((HGLOBAL)lparam) : nullptr;
    if (!pair)
        return FALSE;
    if (low)
        *low = pair[0];
    if (high)
        *high = pair[1];
    GlobalUnlock((HGLOBAL)lparam);
    return TRUE;
}

/* the block of a packed lparam can carry the next message's pair. */
WINUSERAPI LPARAM WINAPI ReuseDDElParam(LPARAM lparam, UINT in, UINT out, UINT_PTR low, UINT_PTR high)
{
    if (packs(in) && packs(out) && lparam) {
        UINT_PTR *pair = GlobalLock((HGLOBAL)lparam);
        if (pair) {
            pair[0] = low;
            pair[1] = high;
            GlobalUnlock((HGLOBAL)lparam);
            return lparam;
        }
    }
    if (packs(in) && lparam)
        GlobalFree((HGLOBAL)lparam);
    return PackDDElParam(out, low, high);
}

WINUSERAPI BOOL WINAPI FreeDDElParam(UINT message, LPARAM lparam)
{
    if (packs(message) && lparam)
        return GlobalFree((HGLOBAL)lparam) == nullptr;
    return TRUE;
}

/* ------------------------------------------------------------------ conversations */

static LRESULT CALLBACK client_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
{
    DDE_CONV *c = (DDE_CONV *)GetWindowLongPtrW(hwnd, 0);
    if (c && message >= WM_DDE_INITIATE && message <= WM_DDE_EXECUTE) {
        if (message == WM_DDE_ACK && !c->Server) {
            /* the first server to acknowledge the initiate is the partner. */
            c->Server = (HWND)wparam;
            return 0;
        }
        if (message == WM_DDE_TERMINATE && (HWND)wparam == c->Server) {
            c->Server = nullptr;
            c->Answered = TRUE;
            c->Message = message;
            return 0;
        }
        c->Message = message;
        c->Lparam = lparam;
        c->Answered = TRUE;
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static HWND client_window(DDE_CONV *c)
{
    if (!g_client_class) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = client_proc;
        wc.cbWndExtra = sizeof(LONG_PTR);
        wc.hInstance = User32Instance;
        wc.lpszClassName = L"DDEMLUnicodeClient";
        g_client_class = RegisterClassW(&wc);
    }
    HWND hwnd = CreateWindowExW(0, L"DDEMLUnicodeClient", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                User32Instance, nullptr);
    if (hwnd)
        SetWindowLongPtrW(hwnd, 0, (LONG_PTR)c);
    return hwnd;
}

typedef struct INITIATE {
    HWND Client;
    LPARAM Names;
    DDE_CONV *Conv;
} INITIATE;

static BOOL CALLBACK initiate_one(HWND hwnd, LPARAM param)
{
    INITIATE *in = (INITIATE *)param;
    if (hwnd == in->Client)
        return TRUE;
    DWORD_PTR result;
    SendMessageTimeoutW(hwnd, WM_DDE_INITIATE, (WPARAM)in->Client, in->Names, SMTO_ABORTIFHUNG, 2000, &result);
    return in->Conv->Server == nullptr;
}

WINUSERAPI HCONV WINAPI DdeConnect(DWORD id, HSZ service, HSZ topic, void *context)
{
    DDE_INSTANCE *i = instance_of(id);
    if (!i)
        return nullptr;
    DDE_CONV *c = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*c));
    if (!c) {
        fail(i, DMLERR_MEMORY_ERROR);
        return nullptr;
    }
    c->Magic = CONV_MAGIC;
    c->Instance = i;
    c->Service = (ATOM)(ULONG_PTR)service;
    c->Topic = (ATOM)(ULONG_PTR)topic;
    c->Client = client_window(c);
    if (!c->Client) {
        HeapFree(GetProcessHeap(), 0, c);
        fail(i, DMLERR_SYS_ERROR);
        return nullptr;
    }
    INITIATE in = {c->Client, MAKELPARAM(c->Service, c->Topic), c};
    EnumWindows(initiate_one, (LPARAM)&in);
    if (!c->Server) {
        DestroyWindow(c->Client);
        HeapFree(GetProcessHeap(), 0, c);
        fail(i, DMLERR_NO_CONV_ESTABLISHED);
        return nullptr;
    }
    return (HCONV)c;
}

static DDE_CONV *conv_of(HCONV handle)
{
    DDE_CONV *c = (DDE_CONV *)handle;
    return c && c->Magic == CONV_MAGIC ? c : nullptr;
}

WINUSERAPI BOOL WINAPI DdeDisconnect(HCONV handle)
{
    DDE_CONV *c = conv_of(handle);
    if (!c)
        return FALSE;
    if (c->Server)
        PostMessageW(c->Server, WM_DDE_TERMINATE, (WPARAM)c->Client, 0);
    c->Magic = 0;
    DestroyWindow(c->Client);
    HeapFree(GetProcessHeap(), 0, c);
    return TRUE;
}

/* pumps the client's messages until the server answers or the time runs out. */
static BOOL wait_answer(DDE_CONV *c, DWORD timeout)
{
    DWORD start = GetTickCount();
    while (!c->Answered) {
        DWORD elapsed = GetTickCount() - start;
        if (elapsed >= timeout)
            return FALSE;
        MsgWaitForMultipleObjects(0, nullptr, FALSE, timeout - elapsed, QS_ALLINPUT);
        MSG msg;
        while (PeekMessageW(&msg, c->Client, 0, 0, PM_REMOVE))
            DispatchMessageW(&msg);
    }
    return TRUE;
}

WINUSERAPI HDDEDATA WINAPI DdeClientTransaction(LPBYTE data, DWORD size, HCONV handle, HSZ item, UINT format,
                                                UINT type, DWORD timeout, LPDWORD result)
{
    DDE_CONV *c = conv_of(handle);
    if (!c)
        return nullptr;
    if (timeout == TIMEOUT_ASYNC) {
        fail(c->Instance, DMLERR_INVALIDPARAMETER);
        return nullptr;
    }
    if (!c->Server) {
        fail(c->Instance, DMLERR_SERVER_DIED);
        return nullptr;
    }
    UINT message, error;
    HGLOBAL memory = nullptr;
    LPARAM lparam;
    switch (type) {
    case XTYP_EXECUTE:
        memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, size ? size : 1);
        if (memory && data)
            memcpy(GlobalLock(memory), data, size), GlobalUnlock(memory);
        message = WM_DDE_EXECUTE;
        lparam = (LPARAM)memory;
        error = DMLERR_EXECACKTIMEOUT;
        break;
    case XTYP_REQUEST:
        message = WM_DDE_REQUEST;
        lparam = MAKELPARAM(format, (ATOM)(ULONG_PTR)item);
        error = DMLERR_DATAACKTIMEOUT;
        break;
    case XTYP_POKE:
        memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, 4 + size);
        if (memory) {
            BYTE *p = GlobalLock(memory);
            *(WORD *)p = 0;
            *(WORD *)(p + 2) = (WORD)format;
            if (data)
                memcpy(p + 4, data, size);
            GlobalUnlock(memory);
        }
        message = WM_DDE_POKE;
        lparam = PackDDElParam(WM_DDE_POKE, (UINT_PTR)memory, (ATOM)(ULONG_PTR)item);
        error = DMLERR_POKEACKTIMEOUT;
        break;
    default:
        fail(c->Instance, DMLERR_INVALIDPARAMETER);
        return nullptr;
    }
    c->Answered = FALSE;
    if (!PostMessageW(c->Server, message, (WPARAM)c->Client, lparam)) {
        if (memory)
            GlobalFree(memory);
        fail(c->Instance, DMLERR_POSTMSG_FAILED);
        return nullptr;
    }
    if (!wait_answer(c, timeout)) {
        fail(c->Instance, error);
        return nullptr;
    }
    if (c->Message == WM_DDE_TERMINATE) {
        fail(c->Instance, DMLERR_SERVER_DIED);
        return nullptr;
    }
    UINT_PTR low = 0, high = 0;
    UnpackDDElParam(c->Message, c->Lparam, &low, &high);
    FreeDDElParam(c->Message, c->Lparam);
    if (c->Message == WM_DDE_DATA && type == XTYP_REQUEST) {
        HGLOBAL answer = (HGLOBAL)low;
        BYTE *p = answer ? GlobalLock(answer) : nullptr;
        HDDEDATA out = nullptr;
        if (p) {
            out = DdeCreateDataHandle(c->Instance->Id, p + 4, (DWORD)GlobalSize(answer) - 4, 0, item,
                                      *(WORD *)(p + 2), 0);
            GlobalUnlock(answer);
        }
        if (result)
            *result = DDE_FACK;
        return out;
    }
    WORD status = (WORD)low;
    if (result)
        *result = status;
    if (!(status & DDE_FACK)) {
        fail(c->Instance, (status & DDE_FBUSY) ? DMLERR_BUSY : DMLERR_NOTPROCESSED);
        return nullptr;
    }
    return (HDDEDATA)TRUE;
}

/* there is no server side, so a program cannot register a service. */
WINUSERAPI HDDEDATA WINAPI DdeNameService(DWORD id, HSZ service, HSZ reserved, UINT flags)
{
    DDE_INSTANCE *i = instance_of(id);
    if (i)
        fail(i, DMLERR_DLL_USAGE);
    return nullptr;
}

/* no conversation of this process is a server's, so no advise loop is there to post the new data to. */
WINUSERAPI BOOL WINAPI DdePostAdvise(DWORD id, HSZ topic, HSZ item)
{
    AcquireSRWLockShared(&g_lock);
    DDE_INSTANCE *i = instance_of(id);
    ReleaseSRWLockShared(&g_lock);
    return i != nullptr;
}
