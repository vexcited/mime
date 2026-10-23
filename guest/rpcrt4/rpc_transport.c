/*
 * the rpc transport: whole pdus over message mode named pipes, one pdu per message, so one read gets a pdu. ncalrpc
 * and ncacn_np both use pipes, under their own names, and ncalrpc endpoints stay out of the pipe namespace of programs.
 */
#include "rpcrt4_private.h"

/*
 * an ncacn_np endpoint "\pipe\name" is the pipe "name". local rpc endpoints are plain names, put in pipes with a prefix
 * program pipe names do not have.
 */
RPC_STATUS RpcpPipeName(PCWSTR protseq, PCWSTR endpoint, PWSTR name, SIZE_T count)
{
    static const WCHAR prefix[] = L"\\\\.\\pipe\\";
    if (!endpoint || !*endpoint)
        return RPC_S_NO_ENDPOINT_FOUND;
    SIZE_T n = wcslen(endpoint);
    if (n > 200)
        return RPC_S_INVALID_ENDPOINT_FORMAT;
    if (!_wcsicmp(protseq, L"ncalrpc")) {
        if (count < 9 + 10 + n + 1)
            return RPC_S_STRING_TOO_LONG;
        wcscpy(name, prefix);
        wcscat(name, L"mime-lrpc-");
        wcscat(name, endpoint);
        return RPC_S_OK;
    }
    if (!_wcsicmp(protseq, L"ncacn_np")) {
        PCWSTR pipe = endpoint;
        if (!_wcsnicmp(pipe, L"\\pipe\\", 6))
            pipe += 6;
        else if (!_wcsnicmp(pipe, L"pipe\\", 5))
            pipe += 5;
        else
            return RPC_S_INVALID_ENDPOINT_FORMAT;
        if (count < 9 + wcslen(pipe) + 1)
            return RPC_S_STRING_TOO_LONG;
        wcscpy(name, prefix);
        wcscat(name, pipe);
        return RPC_S_OK;
    }
    return RPC_S_PROTSEQ_NOT_SUPPORTED;
}

/* whether a network address names this machine. */
static BOOL address_is_local(PCWSTR address)
{
    if (!address || !*address)
        return TRUE;
    PCWSTR a = address;
    while (*a == L'\\')
        a++;
    if (!*a || !wcscmp(a, L".") || !_wcsicmp(a, L"localhost") || !wcscmp(a, L"127.0.0.1"))
        return TRUE;
    WCHAR name[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    return GetComputerNameW(name, &size) && !_wcsicmp(name, a);
}


static RPC_STATUS map_io_error(RPC_STREAM *stream, DWORD error)
{
    switch (error) {
    case ERROR_BROKEN_PIPE:
    case ERROR_NO_DATA:
    case ERROR_PIPE_NOT_CONNECTED:
    case ERROR_OPERATION_ABORTED:
    case ERROR_HANDLE_EOF:
    case ERROR_INVALID_HANDLE:
        stream->Broken = TRUE;
        return RPC_S_CALL_FAILED;
    case ERROR_MORE_DATA:
        stream->Broken = TRUE;
        return RPC_S_PROTOCOL_ERROR;
    default:
        stream->Broken = TRUE;
        return RPC_S_COMM_FAILURE;
    }
}

RPC_STATUS RpcpStreamConnect(const BINDING *b, RPC_STREAM *stream)
{
    memset(stream, 0, sizeof(*stream));
    stream->Kind = TRANSPORT_PIPE;
    if (!address_is_local(b->Address))
        return RPC_S_SERVER_UNAVAILABLE;
    WCHAR name[300];
    RPC_STATUS status = RpcpPipeName(b->Protseq, b->Endpoint, name, ARRAYSIZE(name));
    if (status)
        return status;
    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 20; attempt++) {
        pipe = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IMPERSONATION, nullptr);
        if (pipe != INVALID_HANDLE_VALUE)
            break;
        DWORD error = GetLastError();
        if (error == ERROR_PIPE_BUSY) {
            ULONG wait = b->ComTimeout ? (b->ComTimeout - 1) * 1000 : 5000;
            if (b->ComTimeout == RPC_C_BINDING_INFINITE_TIMEOUT + 1)
                wait = NMPWAIT_WAIT_FOREVER;
            WaitNamedPipeW(name, wait ? wait : 1);
            continue;
        }
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
            return RPC_S_SERVER_UNAVAILABLE;
        if (error == ERROR_ACCESS_DENIED)
            return RPC_S_ACCESS_DENIED;
        return RPC_S_SERVER_UNAVAILABLE;
    }
    if (pipe == INVALID_HANDLE_VALUE)
        return RPC_S_SERVER_TOO_BUSY;
    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);
    stream->Handle = pipe;
    stream->Event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stream->Event) {
        CloseHandle(pipe);
        stream->Handle = nullptr;
        return RPC_S_OUT_OF_RESOURCES;
    }
    return RPC_S_OK;
}

/* wraps a pipe the server accepted. */
RPC_STATUS RpcpStreamAdopt(RPC_STREAM *stream, HANDLE pipe)
{
    memset(stream, 0, sizeof(*stream));
    stream->Kind = TRANSPORT_PIPE;
    stream->Handle = pipe;
    stream->Event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    return stream->Event ? RPC_S_OK : RPC_S_OUT_OF_RESOURCES;
}

void RpcpStreamClose(RPC_STREAM *stream)
{
    if (stream->Handle)
        CloseHandle(stream->Handle);
    if (stream->Event)
        CloseHandle(stream->Event);
    stream->Handle = nullptr;
    stream->Event = nullptr;
}

/*
 * waits for an overlapped operation, the thread's cancellation or the stream timeout. a cancelled or timed out operation
 * is cancelled and the stream is unusable after.
 */
static RPC_STATUS wait_io(RPC_STREAM *stream, OVERLAPPED *ov, DWORD *bytes, BOOL alertable)
{
    RPC_THREAD *thread = alertable ? RpcpThread() : nullptr;
    HANDLE handles[2] = {stream->Event, thread ? thread->CancelEvent : nullptr};
    DWORD count = handles[1] ? 2 : 1;
    DWORD timeout = stream->Timeout ? stream->Timeout : INFINITE;
    DWORD wait = thread && thread->WaitFn ? thread->WaitFn(count, handles, timeout, thread->WaitContext)
                                          : WaitForMultipleObjects(count, handles, FALSE, timeout);
    if (wait == WAIT_OBJECT_0) {
        if (GetOverlappedResult(stream->Handle, ov, bytes, FALSE))
            return RPC_S_OK;
        return map_io_error(stream, GetLastError());
    }
    CancelIoEx(stream->Handle, ov);
    GetOverlappedResult(stream->Handle, ov, bytes, TRUE);
    stream->Broken = TRUE;
    /* a thread cancel and the binding's call timeout both cancel the call. */
    if (wait == WAIT_OBJECT_0 + 1) {
        InterlockedExchange(&thread->Cancelled, 0);
        ResetEvent(thread->CancelEvent);
    }
    return RPC_S_CALL_CANCELLED;
}

RPC_STATUS RpcpStreamSend(RPC_STREAM *stream, const void *data, ULONG length)
{
    OVERLAPPED ov = {};
    ov.hEvent = stream->Event;
    ResetEvent(stream->Event);
    DWORD written = 0;
    if (!WriteFile(stream->Handle, data, length, &written, &ov)) {
        DWORD error = GetLastError();
        if (error != ERROR_IO_PENDING)
            return map_io_error(stream, error);
        RPC_STATUS status = wait_io(stream, &ov, &written, FALSE);
        if (status)
            return status;
    }
    return written == length ? RPC_S_OK : map_io_error(stream, ERROR_BROKEN_PIPE);
}

RPC_STATUS RpcpStreamReceive(RPC_STREAM *stream, void *buffer, ULONG capacity, ULONG *received)
{
    OVERLAPPED ov = {};
    ov.hEvent = stream->Event;
    ResetEvent(stream->Event);
    DWORD got = 0;
    if (!ReadFile(stream->Handle, buffer, capacity, &got, &ov)) {
        DWORD error = GetLastError();
        if (error != ERROR_IO_PENDING)
            return map_io_error(stream, error);
        RPC_STATUS status = wait_io(stream, &ov, &got, TRUE);
        if (status)
            return status;
    }
    *received = got;
    return got ? RPC_S_OK : map_io_error(stream, ERROR_BROKEN_PIPE);
}

/* lets every user open the object with the given access: the interfaces decide who may call. */
PSECURITY_DESCRIPTOR RpcpEveryoneDescriptor(ULONG mask)
{
    unsigned char *sd = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 256);
    if (!sd)
        return nullptr;
    /* an absolute descriptor: header, then an acl with one allowed ace for the world sid. */
    static const unsigned char world_sid[12] = {1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0};
    struct {
        unsigned char Revision;
        unsigned char Sbz1;
        USHORT Control;
        PVOID Owner;
        PVOID Group;
        PVOID Sacl;
        PVOID Dacl;
    } *header = (void *)sd;
    unsigned char *acl = sd + 64;
    unsigned char *ace = acl + 8;
    header->Revision = 1;
    header->Control = 0x0004;
    header->Dacl = acl;
    acl[0] = 2;
    *(USHORT *)(acl + 2) = (USHORT)(8 + 8 + sizeof(world_sid));
    *(USHORT *)(acl + 4) = 1;
    ace[0] = 0;
    ace[1] = 0;
    *(USHORT *)(ace + 2) = (USHORT)(8 + sizeof(world_sid));
    *(ULONG *)(ace + 4) = mask;
    memcpy(ace + 8, world_sid, sizeof(world_sid));
    return sd;
}

void RpcpAppend(WCHAR *dst, SIZE_T count, const WCHAR *text)
{
    SIZE_T n = wcslen(dst);
    while (n + 1 < count && *text)
        dst[n++] = *text++;
    dst[n] = 0;
}

void RpcpAppendNumber(WCHAR *dst, SIZE_T count, ULONG number)
{
    WCHAR digits[12];
    int i = 0;
    do {
        digits[i++] = (WCHAR)(L'0' + number % 10);
        number /= 10;
    } while (number);
    WCHAR text[12];
    int j = 0;
    while (i)
        text[j++] = digits[--i];
    text[j] = 0;
    RpcpAppend(dst, count, text);
}

void RpcpCopy(WCHAR *dst, SIZE_T count, const WCHAR *text)
{
    dst[0] = 0;
    RpcpAppend(dst, count, text);
}

const RPC_ADVAPI *RpcpAdvapi(void)
{
    static RPC_ADVAPI table;
    static LONG state;
    if (state != 2) {
        if (InterlockedCompareExchange(&state, 1, 0) == 0) {
            HMODULE m = LoadLibraryW(L"advapi32.dll");
            if (m) {
                table.ImpersonateNamedPipeClient = (void *)GetProcAddress(m, "ImpersonateNamedPipeClient");
                table.RevertToSelf = (void *)GetProcAddress(m, "RevertToSelf");
                table.OpenThreadToken = (void *)GetProcAddress(m, "OpenThreadToken");
                table.GetTokenInformation = (void *)GetProcAddress(m, "GetTokenInformation");
                table.LookupAccountSidW = (void *)GetProcAddress(m, "LookupAccountSidW");
            }
            state = 2;
        } else {
            while (state != 2)
                Sleep(1);
        }
    }
    return &table;
}
