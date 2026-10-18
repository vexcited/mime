/*
 * named and anonymous pipes over the \Device\NamedPipe file system.
 */
#include "kernelbase_private.h"

#ifndef ACCESS_SYSTEM_SECURITY
#define ACCESS_SYSTEM_SECURITY 0x01000000
#endif

#define FSCTL_PIPE_DISCONNECT 0x00110004
#define FSCTL_PIPE_LISTEN 0x00110008
#define FSCTL_PIPE_PEEK 0x0011400C
#define FSCTL_PIPE_WAIT 0x00110018

#define FILE_PIPE_MESSAGE_TYPE 1

typedef struct KB_FILE_PIPE_INFORMATION {
    ULONG ReadMode;
    ULONG CompletionMode;
} KB_FILE_PIPE_INFORMATION;

typedef struct KB_FILE_PIPE_LOCAL_INFORMATION {
    ULONG NamedPipeType;
    ULONG NamedPipeConfiguration;
    ULONG MaximumInstances;
    ULONG CurrentInstances;
    ULONG InboundQuota;
    ULONG ReadDataAvailable;
    ULONG OutboundQuota;
    ULONG WriteQuotaAvailable;
    ULONG NamedPipeState;
    ULONG NamedPipeEnd;
} KB_FILE_PIPE_LOCAL_INFORMATION;

typedef struct KB_FILE_PIPE_PEEK_BUFFER {
    ULONG NamedPipeState;
    ULONG ReadDataAvailable;
    ULONG NumberOfMessages;
    ULONG MessageLength;
    CHAR Data[1];
} KB_FILE_PIPE_PEEK_BUFFER;

typedef struct KB_FILE_PIPE_WAIT_FOR_BUFFER {
    LARGE_INTEGER Timeout;
    ULONG NameLength;
    BOOLEAN TimeoutSpecified;
    WCHAR Name[1];
} KB_FILE_PIPE_WAIT_FOR_BUFFER;

static PVOID port_context(LPOVERLAPPED ov)
{
    return ((ULONG_PTR)ov->hEvent & 1) ? nullptr : ov;
}

static HANDLE ov_event(LPOVERLAPPED ov)
{
    return (HANDLE)((ULONG_PTR)ov->hEvent & ~(ULONG_PTR)1);
}

/* a file system control that waits for completion unless the caller supplied an OVERLAPPED. */
static NTSTATUS pipe_control(HANDLE pipe, LPOVERLAPPED ov, ULONG code, PVOID in, ULONG in_len, PVOID out,
                             ULONG out_len, ULONG_PTR *information)
{
    NTSTATUS status;
    if (ov) {
        PIO_STATUS_BLOCK iosb = (PIO_STATUS_BLOCK)ov;
        iosb->Status = STATUS_PENDING;
        iosb->Information = 0;
        status = NtFsControlFile(pipe, ov_event(ov), nullptr, port_context(ov), iosb, code, in, in_len, out, out_len);
        if (information)
            *information = iosb->Information;
        return status;
    }
    IO_STATUS_BLOCK iosb = {};
    status = NtFsControlFile(pipe, nullptr, nullptr, nullptr, &iosb, code, in, in_len, out, out_len);
    if (status == STATUS_PENDING) {
        NtWaitForSingleObject(pipe, FALSE, nullptr);
        status = iosb.Status;
    }
    if (information)
        *information = iosb.Information;
    return status;
}

WINBASEAPI HANDLE WINAPI CreateNamedPipeW(LPCWSTR name, DWORD open_mode, DWORD pipe_mode, DWORD max_instances,
                                          DWORD out_size, DWORD in_size, DWORD timeout, LPSECURITY_ATTRIBUTES sa)
{
    if (!name || !max_instances || max_instances > PIPE_UNLIMITED_INSTANCES) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    ACCESS_MASK access = SYNCHRONIZE | (open_mode & (WRITE_DAC | WRITE_OWNER | ACCESS_SYSTEM_SECURITY));
    ULONG share = 0;
    switch (open_mode & PIPE_ACCESS_DUPLEX) {
    case PIPE_ACCESS_INBOUND:
        access |= GENERIC_READ | FILE_WRITE_ATTRIBUTES;
        share = FILE_SHARE_WRITE;
        break;
    case PIPE_ACCESS_OUTBOUND:
        access |= GENERIC_WRITE | FILE_READ_ATTRIBUTES;
        share = FILE_SHARE_READ;
        break;
    case PIPE_ACCESS_DUPLEX:
        access |= GENERIC_READ | GENERIC_WRITE;
        share = FILE_SHARE_READ | FILE_SHARE_WRITE;
        break;
    default:
        KbSetError(ERROR_INVALID_PARAMETER);
        return INVALID_HANDLE_VALUE;
    }
    ULONG options = 0;
    if (open_mode & FILE_FLAG_WRITE_THROUGH)
        options |= FILE_WRITE_THROUGH;
    if (!(open_mode & FILE_FLAG_OVERLAPPED))
        options |= FILE_SYNCHRONOUS_IO_NONALERT;
    ULONG disposition = (open_mode & FILE_FLAG_FIRST_PIPE_INSTANCE) ? FILE_CREATE : FILE_OPEN_IF;

    UNICODE_STRING nt;
    if (!KbDosPathToNt(name, &nt))
        return INVALID_HANDLE_VALUE;
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &nt, OBJ_CASE_INSENSITIVE | (sa && sa->bInheritHandle ? OBJ_INHERIT : 0), nullptr,
                               sa ? sa->lpSecurityDescriptor : nullptr);
    LARGE_INTEGER default_timeout;
    default_timeout.QuadPart = timeout ? -(LONGLONG)timeout * 10000 : -500000;
    IO_STATUS_BLOCK iosb = {};
    HANDLE handle;
    NTSTATUS status = NtCreateNamedPipeFile(
        &handle, access, &oa, &iosb, share, disposition, options, (pipe_mode & PIPE_TYPE_MESSAGE) ? 1 : 0,
        (pipe_mode & PIPE_READMODE_MESSAGE) ? 1 : 0, (pipe_mode & PIPE_NOWAIT) ? 1 : 0,
        max_instances == PIPE_UNLIMITED_INSTANCES ? 0xFFFFFFFF : max_instances, in_size, out_size, &default_timeout);
    RtlFreeUnicodeString(&nt);
    if (!NT_SUCCESS(status)) {
        KbSetStatus(status);
        return INVALID_HANDLE_VALUE;
    }
    KbSetError(ERROR_SUCCESS);
    return handle;
}

WINBASEAPI HANDLE WINAPI CreateNamedPipeA(LPCSTR name, DWORD open_mode, DWORD pipe_mode, DWORD max_instances,
                                          DWORD out_size, DWORD in_size, DWORD timeout, LPSECURITY_ATTRIBUTES sa)
{
    PWSTR w = name ? KbAnsiToWide(name) : nullptr;
    if (name && !w)
        return INVALID_HANDLE_VALUE;
    HANDLE h = CreateNamedPipeW(w, open_mode, pipe_mode, max_instances, out_size, in_size, timeout, sa);
    KbFree(w);
    return h;
}

WINBASEAPI BOOL WINAPI ConnectNamedPipe(HANDLE pipe, LPOVERLAPPED overlapped)
{
    NTSTATUS status = pipe_control(pipe, overlapped, FSCTL_PIPE_LISTEN, nullptr, 0, nullptr, 0, nullptr);
    if (status == STATUS_PENDING) {
        KbSetError(ERROR_IO_PENDING);
        return FALSE;
    }
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

WINBASEAPI BOOL WINAPI DisconnectNamedPipe(HANDLE pipe)
{
    NTSTATUS status = pipe_control(pipe, nullptr, FSCTL_PIPE_DISCONNECT, nullptr, 0, nullptr, 0, nullptr);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

WINBASEAPI BOOL WINAPI PeekNamedPipe(HANDLE pipe, LPVOID buffer, DWORD size, LPDWORD read, LPDWORD available,
                                     LPDWORD left)
{
    ULONG length = FIELD_OFFSET(KB_FILE_PIPE_PEEK_BUFFER, Data) + (buffer ? size : 0);
    KB_FILE_PIPE_PEEK_BUFFER *peek = KbAlloc(length);
    if (!peek) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    ULONG_PTR info = 0;
    NTSTATUS status = pipe_control(pipe, nullptr, FSCTL_PIPE_PEEK, nullptr, 0, peek, length, &info);
    if (status == STATUS_BUFFER_OVERFLOW)
        status = STATUS_SUCCESS;
    if (!NT_SUCCESS(status)) {
        KbFree(peek);
        return KbSetStatus(status);
    }
    DWORD copied = info > FIELD_OFFSET(KB_FILE_PIPE_PEEK_BUFFER, Data)
                       ? (DWORD)(info - FIELD_OFFSET(KB_FILE_PIPE_PEEK_BUFFER, Data))
                       : 0;
    if (buffer && copied)
        RtlCopyMemory(buffer, peek->Data, copied);
    if (read)
        *read = copied;
    if (available)
        *available = peek->ReadDataAvailable;
    if (left)
        *left = peek->NumberOfMessages ? peek->MessageLength - copied : 0;
    KbFree(peek);
    return TRUE;
}

WINBASEAPI BOOL WINAPI SetNamedPipeHandleState(HANDLE pipe, LPDWORD mode, LPDWORD max_collection,
                                               LPDWORD collect_timeout)
{
    if (mode) {
        KB_FILE_PIPE_INFORMATION info;
        info.ReadMode = (*mode & PIPE_READMODE_MESSAGE) ? 1 : 0;
        info.CompletionMode = (*mode & PIPE_NOWAIT) ? 1 : 0;
        IO_STATUS_BLOCK iosb;
        NTSTATUS status = NtSetInformationFile(pipe, &iosb, &info, sizeof(info), FilePipeInformation);
        if (!NT_SUCCESS(status))
            return KbSetStatus(status);
    }
    /* collection only applies to remote pipes. */
    (void)max_collection;
    (void)collect_timeout;
    return TRUE;
}

WINBASEAPI BOOL WINAPI GetNamedPipeInfo(HANDLE pipe, LPDWORD flags, LPDWORD out_size, LPDWORD in_size,
                                        LPDWORD max_instances)
{
    KB_FILE_PIPE_LOCAL_INFORMATION info;
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtQueryInformationFile(pipe, &iosb, &info, sizeof(info), FilePipeLocalInformation);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    if (flags)
        *flags = (info.NamedPipeType == FILE_PIPE_MESSAGE_TYPE ? PIPE_TYPE_MESSAGE : 0) |
                 (info.NamedPipeEnd ? PIPE_SERVER_END : 0);
    if (out_size)
        *out_size = info.OutboundQuota;
    if (in_size)
        *in_size = info.InboundQuota;
    if (max_instances)
        *max_instances = info.MaximumInstances == 0xFFFFFFFF ? PIPE_UNLIMITED_INSTANCES : info.MaximumInstances;
    return TRUE;
}

WINBASEAPI BOOL WINAPI GetNamedPipeHandleStateW(HANDLE pipe, LPDWORD state, LPDWORD instances, LPDWORD max_collection,
                                                LPDWORD collect_timeout, LPWSTR user, DWORD user_size)
{
    IO_STATUS_BLOCK iosb;
    NTSTATUS status;
    if (state) {
        KB_FILE_PIPE_INFORMATION info;
        status = NtQueryInformationFile(pipe, &iosb, &info, sizeof(info), FilePipeInformation);
        if (!NT_SUCCESS(status))
            return KbSetStatus(status);
        *state = (info.ReadMode ? PIPE_READMODE_MESSAGE : 0) | (info.CompletionMode ? PIPE_NOWAIT : 0);
    }
    if (instances) {
        KB_FILE_PIPE_LOCAL_INFORMATION local;
        status = NtQueryInformationFile(pipe, &iosb, &local, sizeof(local), FilePipeLocalInformation);
        if (!NT_SUCCESS(status))
            return KbSetStatus(status);
        *instances = local.CurrentInstances;
    }
    if (max_collection)
        *max_collection = 0;
    if (collect_timeout)
        *collect_timeout = 0;
    if (user && user_size)
        user[0] = 0;
    return TRUE;
}

#define FSCTL_PIPE_GET_CONNECTION_ATTRIBUTE 0x00110030

static BOOL pipe_attribute(HANDLE pipe, const char *name, PULONG value)
{
    IO_STATUS_BLOCK iosb;
    NTSTATUS status = NtFsControlFile(pipe, nullptr, nullptr, nullptr, &iosb, FSCTL_PIPE_GET_CONNECTION_ATTRIBUTE, (PVOID)name,
                                      (ULONG)strlen(name) + 1, value, sizeof(*value));
    return NT_SUCCESS(status) ? TRUE : KbSetStatus(status);
}

WINBASEAPI BOOL WINAPI GetNamedPipeClientProcessId(HANDLE pipe, PULONG id)
{
    return pipe_attribute(pipe, "ClientProcessId", id);
}

WINBASEAPI BOOL WINAPI GetNamedPipeServerProcessId(HANDLE pipe, PULONG id)
{
    return pipe_attribute(pipe, "ServerProcessId", id);
}

WINBASEAPI BOOL WINAPI GetNamedPipeClientSessionId(HANDLE pipe, PULONG id)
{
    return pipe_attribute(pipe, "ClientSessionId", id);
}

WINBASEAPI BOOL WINAPI GetNamedPipeServerSessionId(HANDLE pipe, PULONG id)
{
    return pipe_attribute(pipe, "ServerSessionId", id);
}

WINBASEAPI BOOL WINAPI GetNamedPipeHandleStateA(HANDLE pipe, LPDWORD state, LPDWORD instances, LPDWORD max_collection,
                                                LPDWORD collect_timeout, LPSTR user, DWORD user_size)
{
    if (!GetNamedPipeHandleStateW(pipe, state, instances, max_collection, collect_timeout, nullptr, 0))
        return FALSE;
    if (user && user_size)
        user[0] = 0;
    return TRUE;
}

WINBASEAPI BOOL WINAPI WaitNamedPipeW(LPCWSTR name, DWORD timeout)
{
    static const WCHAR prefix[] = L"\\\\.\\pipe\\";
    if (!name) {
        KbSetError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    SIZE_T i = 0;
    for (; prefix[i]; i++) {
        if (KbUpcase(name[i]) != KbUpcase(prefix[i])) {
            KbSetError(ERROR_BAD_PATHNAME);
            return FALSE;
        }
    }
    LPCWSTR pipe_name = name + i;
    SIZE_T chars = 0;
    while (pipe_name[chars])
        chars++;
    ULONG length = FIELD_OFFSET(KB_FILE_PIPE_WAIT_FOR_BUFFER, Name) + (ULONG)(chars * sizeof(WCHAR));
    KB_FILE_PIPE_WAIT_FOR_BUFFER *wait = KbAllocZero(length);
    if (!wait) {
        KbSetError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    wait->NameLength = (ULONG)(chars * sizeof(WCHAR));
    RtlCopyMemory(wait->Name, pipe_name, chars * sizeof(WCHAR));
    if (timeout == NMPWAIT_USE_DEFAULT_WAIT) {
        wait->TimeoutSpecified = FALSE;
    } else {
        wait->TimeoutSpecified = TRUE;
        wait->Timeout.QuadPart = timeout == NMPWAIT_WAIT_FOREVER ? (LONGLONG)0x8000000000000000ULL
                                                                 : -(LONGLONG)timeout * 10000;
    }

    UNICODE_STRING root;
    RtlInitUnicodeString(&root, L"\\Device\\NamedPipe\\");
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &root, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    IO_STATUS_BLOCK iosb;
    HANDLE device;
    NTSTATUS status = NtOpenFile(&device, FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa, &iosb,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_SYNCHRONOUS_IO_NONALERT);
    if (NT_SUCCESS(status)) {
        status = pipe_control(device, nullptr, FSCTL_PIPE_WAIT, wait, length, nullptr, 0, nullptr);
        NtClose(device);
    }
    KbFree(wait);
    if (status == STATUS_OBJECT_NAME_NOT_FOUND) {
        KbSetError(ERROR_FILE_NOT_FOUND);
        return FALSE;
    }
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    return TRUE;
}

WINBASEAPI BOOL WINAPI WaitNamedPipeA(LPCSTR name, DWORD timeout)
{
    PWSTR w = name ? KbAnsiToWide(name) : nullptr;
    if (name && !w)
        return FALSE;
    BOOL ok = WaitNamedPipeW(w, timeout);
    KbFree(w);
    return ok;
}

static volatile LONG g_pipe_serial;

/* an anonymous pipe is a byte stream named pipe with a unique name and a single instance. */
WINBASEAPI BOOL WINAPI CreatePipe(PHANDLE read_pipe, PHANDLE write_pipe, LPSECURITY_ATTRIBUTES sa, DWORD size)
{
    WCHAR buffer[64];
    KbFormat(buffer, RTL_NUMBER_OF(buffer), L"\\Device\\NamedPipe\\Win32Pipes.%08x.%08x", GetCurrentProcessId(),
             (ULONG)_InterlockedIncrement(&g_pipe_serial));
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, buffer);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | (sa && sa->bInheritHandle ? OBJ_INHERIT : 0), nullptr,
                               sa ? sa->lpSecurityDescriptor : nullptr);
    LARGE_INTEGER timeout;
    timeout.QuadPart = -1200000000LL;
    if (!size)
        size = 4096;
    IO_STATUS_BLOCK iosb;
    HANDLE reader, writer;
    NTSTATUS status = NtCreateNamedPipeFile(&reader, GENERIC_READ | FILE_WRITE_ATTRIBUTES | SYNCHRONIZE, &oa, &iosb,
                                            FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_CREATE,
                                            FILE_SYNCHRONOUS_IO_NONALERT, 0, 0, 0, 1, size, size, &timeout);
    if (!NT_SUCCESS(status))
        return KbSetStatus(status);
    status = NtOpenFile(&writer, GENERIC_WRITE | FILE_READ_ATTRIBUTES | SYNCHRONIZE, &oa, &iosb, FILE_SHARE_READ,
                        FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE);
    if (!NT_SUCCESS(status)) {
        NtClose(reader);
        return KbSetStatus(status);
    }
    *read_pipe = reader;
    *write_pipe = writer;
    return TRUE;
}

/* writes a message and reads the answer in one request, FSCTL_PIPE_TRANSCEIVE. */
WINBASEAPI BOOL WINAPI TransactNamedPipe(HANDLE pipe, LPVOID in, DWORD in_size, LPVOID out, DWORD out_size,
                                         LPDWORD read, LPOVERLAPPED overlapped)
{
    NTSTATUS status;
    if (overlapped) {
        overlapped->Internal = STATUS_PENDING;
        HANDLE event = overlapped->hEvent;
        status = NtFsControlFile(pipe, event, nullptr, ((ULONG_PTR)event & 1) ? nullptr : overlapped,
                                 (PIO_STATUS_BLOCK)overlapped, 0x11C017, in, in_size, out, out_size);
        if (NT_SUCCESS(status) && status != STATUS_PENDING) {
            if (read)
                *read = (DWORD)overlapped->InternalHigh;
            return TRUE;
        }
        if ((status & 0xC0000000) == 0x80000000 && read)
            *read = (DWORD)overlapped->InternalHigh;
        return KbSetStatus(status);
    }
    IO_STATUS_BLOCK iosb = {};
    status = NtFsControlFile(pipe, nullptr, nullptr, nullptr, &iosb, 0x11C017, in, in_size, out, out_size);
    if (status == STATUS_PENDING) {
        status = NtWaitForSingleObject(pipe, FALSE, nullptr);
        if (NT_SUCCESS(status))
            status = iosb.Status;
    }
    if (!NT_SUCCESS(status)) {
        /* a message longer than the buffer still reports what was read. */
        if ((status & 0xC0000000) == 0x80000000)
            *read = (DWORD)iosb.Information;
        return KbSetStatus(status);
    }
    *read = (DWORD)iosb.Information;
    return TRUE;
}
