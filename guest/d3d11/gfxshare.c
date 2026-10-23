#include "gfxshare.h"
#include <mime/ntuser.h>
#include <mime/ntrtl.h>

HRESULT MimeShareHResult(NTSTATUS status)
{
    switch (status) {
    case STATUS_SUCCESS:
        return S_OK;
    case STATUS_TIMEOUT:
        return DXGI_ERROR_WAIT_TIMEOUT;
    case STATUS_OBJECT_TYPE_MISMATCH:
        return HRESULT_FROM_WIN32(ERROR_INVALID_HANDLE);
    case STATUS_INVALID_PARAMETER:
    case STATUS_OBJECT_NAME_INVALID:
    case STATUS_BUFFER_TOO_SMALL:
        return E_INVALIDARG;
    case STATUS_OBJECT_NAME_COLLISION:
        return DXGI_ERROR_NAME_ALREADY_EXISTS;
    case STATUS_INSUFFICIENT_RESOURCES:
    case STATUS_NO_MEMORY:
        return E_OUTOFMEMORY;
    case STATUS_ACCESS_VIOLATION:
        return E_POINTER;
    default:
        return NT_SUCCESS(status) ? S_OK : HRESULT_FROM_NT(status);
    }
}

/* names live in the session's BaseNamedObjects directory. */
static BOOL full_name(LPCWSTR name, WCHAR *out, UINT chars)
{
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    UINT n = 0;
    static const WCHAR prefix[] = L"\\Sessions\\";
    for (const WCHAR *p = prefix; *p; p++)
        out[n++] = *p;
    char digits[12];
    UINT d = 0;
    do {
        digits[d++] = (char)('0' + session % 10);
        session /= 10;
    } while (session);
    while (d)
        out[n++] = (WCHAR)digits[--d];
    for (const WCHAR *p = L"\\BaseNamedObjects\\"; *p; p++)
        out[n++] = *p;
    /* the Local prefix names the directory itself. */
    if (!_wcsnicmp(name, L"Local\\", 6))
        name += 6;
    for (; *name; name++) {
        if (n + 1 >= chars)
            return FALSE;
        out[n++] = *name;
    }
    out[n] = 0;
    return TRUE;
}

static NTSTATUS call(UINT op, MimeGfxShare *a) { return NtGdiMimeGfxShare(op, a); }

HRESULT MimeShareCreate(MimeShare *share, UINT kind, const void *data, UINT size, UINT flags, LPCWSTR name,
                        const SECURITY_ATTRIBUTES *attributes)
{
    WCHAR path[512];
    MimeGfxShare a = {};
    a.Kind = kind;
    a.Data = (ULONG_PTR)data;
    a.Size = size;
    a.Flags = flags | ((attributes && attributes->bInheritHandle) ? MIME_GFXSHARE_INHERIT : 0);
    if (name) {
        if (!full_name(name, path, RTL_NUMBER_OF(path)))
            return E_INVALIDARG;
        a.Name = (ULONG_PTR)path;
    }
    NTSTATUS status = call(MIME_GFXSHARE_CREATE, &a);
    if (!NT_SUCCESS(status))
        return MimeShareHResult(status);
    share->handle = (HANDLE)(ULONG_PTR)a.Handle;
    share->id = a.Id;
    share->kind = kind;
    share->flags = flags;
    share->legacy = a.Legacy;
    return S_OK;
}

HRESULT MimeShareHandle(const MimeShare *share, DWORD access, const SECURITY_ATTRIBUTES *attributes, HANDLE *out)
{
    BOOL inherit = attributes && attributes->bInheritHandle;
    if (!DuplicateHandle(GetCurrentProcess(), share->handle, GetCurrentProcess(), out, 0, inherit,
                         DUPLICATE_SAME_ACCESS))
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

HRESULT MimeShareOpen(HANDLE handle, UINT kind, MimeShare *share, void *data, UINT capacity, UINT *size)
{
    MimeGfxShare a = {};
    a.Handle = (ULONG_PTR)handle;
    a.Kind = kind;
    a.Data = (ULONG_PTR)data;
    a.Size = capacity;
    NTSTATUS status = call(MIME_GFXSHARE_OPEN, &a);
    if (!NT_SUCCESS(status))
        return MimeShareHResult(status);
    HANDLE own;
    if (!DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &own, 0, FALSE, DUPLICATE_SAME_ACCESS))
        return HRESULT_FROM_WIN32(GetLastError());
    share->handle = own;
    share->id = a.Id;
    share->kind = a.Kind;
    share->flags = a.Flags;
    /* the legacy handle belongs to the creator: closing an opened object does not take it away. */
    share->legacy = 0;
    if (size)
        *size = a.Size;
    return S_OK;
}

HRESULT MimeShareKind(HANDLE handle, UINT *kind)
{
    MimeGfxShare a = {};
    a.Handle = (ULONG_PTR)handle;
    NTSTATUS status = call(MIME_GFXSHARE_OPEN, &a);
    /* no room is given for the description, but the kind comes back even when it does not fit. */
    if (!NT_SUCCESS(status) && status != STATUS_BUFFER_TOO_SMALL)
        return MimeShareHResult(status);
    *kind = a.Kind;
    return S_OK;
}

HRESULT MimeShareOpenName(LPCWSTR name, UINT kind, HANDLE *out)
{
    WCHAR path[512];
    MimeGfxShare a = {};
    if (!name || !full_name(name, path, RTL_NUMBER_OF(path)))
        return E_INVALIDARG;
    a.Kind = kind;
    a.Name = (ULONG_PTR)path;
    NTSTATUS status = call(MIME_GFXSHARE_OPEN_NAME, &a);
    if (!NT_SUCCESS(status))
        return MimeShareHResult(status);
    *out = (HANDLE)(ULONG_PTR)a.Handle;
    return S_OK;
}

HRESULT MimeShareOpenLegacy(UINT legacy, HANDLE *out)
{
    MimeGfxShare a = {};
    a.Legacy = legacy;
    NTSTATUS status = call(MIME_GFXSHARE_OPEN_LEGACY, &a);
    if (!NT_SUCCESS(status))
        return MimeShareHResult(status);
    *out = (HANDLE)(ULONG_PTR)a.Handle;
    return S_OK;
}

void MimeShareClose(MimeShare *share)
{
    if (!share->handle)
        return;
    if (share->legacy) {
        MimeGfxShare a = {};
        a.Legacy = share->legacy;
        call(MIME_GFXSHARE_RETIRE, &a);
    }
    CloseHandle(share->handle);
    memset(share, 0, sizeof(*share));
}

NTSTATUS MimeShareAcquire(HANDLE handle, UINT64 key, UINT timeout)
{
    MimeGfxShare a = {};
    a.Handle = (ULONG_PTR)handle;
    a.Key = key;
    a.Timeout = timeout;
    return call(MIME_GFXSHARE_ACQUIRE, &a);
}

NTSTATUS MimeShareRelease(HANDLE handle, UINT64 key)
{
    MimeGfxShare a = {};
    a.Handle = (ULONG_PTR)handle;
    a.Key = key;
    return call(MIME_GFXSHARE_RELEASE, &a);
}

void MimeShareAbandon(HANDLE handle)
{
    MimeGfxShare a = {};
    a.Handle = (ULONG_PTR)handle;
    call(MIME_GFXSHARE_ABANDON, &a);
}
