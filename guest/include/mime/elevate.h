/*
 * the elevation broker of services.exe, for mime's own programs. advapi32 is microsoft's file and has no door to it,
 * and no AppInfo service answers ShellExecute's runas yet, so features.exe and openwith.exe ask the broker themselves
 * over the pipe of mime/scm.h. error 0 means a process was created, ERROR_CANCELLED that the machine policy refused or
 * that the token has no administrator behind it.
 */
#pragma once
#include <mime/scm.h>

static inline DWORD mime_elevate_read(HANDLE pipe, void *data, DWORD size)
{
    BYTE *p = data;
    while (size) {
        DWORD done = 0;
        if (!ReadFile(pipe, p, size, &done, nullptr))
            return GetLastError();
        if (!done)
            return ERROR_BROKEN_PIPE;
        p += done;
        size -= done;
    }
    return ERROR_SUCCESS;
}

static inline DWORD mime_elevate_write(HANDLE pipe, const void *data, DWORD size)
{
    const BYTE *p = data;
    while (size) {
        DWORD done = 0;
        if (!WriteFile(pipe, p, size, &done, nullptr))
            return GetLastError();
        p += done;
        size -= done;
    }
    return ERROR_SUCCESS;
}

static inline DWORD MimeLaunchElevated(LPCWSTR application, LPCWSTR command_line, LPCWSTR directory, DWORD creation_flags,
                                       const STARTUPINFOW *startup, PROCESS_INFORMATION *info, DWORD *elevation)
{
    if (!command_line || !info)
        return ERROR_INVALID_PARAMETER;
    memset(info, 0, sizeof(*info));
    /* services.exe signals this event once its pipe listens. it exists by the time a program of the user runs. */
    HANDLE ready = OpenEventW(SYNCHRONIZE, FALSE, SCM_START_EVENT);
    if (ready) {
        WaitForSingleObject(ready, 180000);
        CloseHandle(ready);
    }
    HANDLE pipe = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 40 && pipe == INVALID_HANDLE_VALUE; attempt++) {
        pipe = CreateFileW(SCM_PIPE_NAME, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) {
            if (GetLastError() != ERROR_PIPE_BUSY)
                return 1722; /* RPC_S_SERVER_UNAVAILABLE */
            WaitNamedPipeW(SCM_PIPE_NAME, 1000);
        }
    }
    if (pipe == INVALID_HANDLE_VALUE)
        return 1722;
    SCM_WRITER w;
    ScmWriterInit(&w, SCM_OP_LAUNCH_ELEVATED);
    ScmPutString(&w, application);
    ScmPutString(&w, command_line);
    ScmPutString(&w, directory);
    ScmPutString(&w, startup ? startup->lpDesktop : nullptr);
    ScmPutString(&w, startup ? startup->lpTitle : nullptr);
    ScmPutU32(&w, creation_flags);
    ScmPutU32(&w, startup ? startup->dwFlags : 0);
    ScmPutU32(&w, startup ? startup->wShowWindow : 1);
    UINT32 size;
    UCHAR *message = ScmWriterFinish(&w, &size);
    DWORD error = message ? mime_elevate_write(pipe, message, size) : ERROR_NOT_ENOUGH_MEMORY;
    HANDLE heap = GetProcessHeap();
    SCM_HEADER header;
    BYTE *data = nullptr;
    if (!error)
        error = mime_elevate_read(pipe, &header, sizeof(header));
    if (!error && (header.Magic != SCM_MAGIC || header.Size < sizeof(header) || header.Size > SCM_MAX_MESSAGE))
        error = ERROR_INVALID_DATA;
    if (!error && !(data = HeapAlloc(heap, 0, header.Size)))
        error = ERROR_NOT_ENOUGH_MEMORY;
    if (!error) {
        memcpy(data, &header, sizeof(header));
        if (header.Size > sizeof(header))
            error = mime_elevate_read(pipe, data + sizeof(header), header.Size - (DWORD)sizeof(header));
    }
    if (!error)
        error = header.Code;
    if (!error) {
        SCM_READER r;
        ScmReaderInit(&r, data, header.Size);
        info->dwProcessId = ScmGetU32(&r);
        info->dwThreadId = ScmGetU32(&r);
        DWORD kind = ScmGetU32(&r);
        info->hProcess = (HANDLE)(ULONG_PTR)ScmGetU64(&r);
        info->hThread = (HANDLE)(ULONG_PTR)ScmGetU64(&r);
        if (r.Failed)
            error = ERROR_INVALID_DATA;
        else if (elevation)
            *elevation = kind;
    }
    if (data)
        HeapFree(heap, 0, data);
    if (w.Data)
        SCM_FREE(w.Data);
    CloseHandle(pipe);
    return error;
}
