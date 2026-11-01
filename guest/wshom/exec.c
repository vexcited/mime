/*
 * WshExec: a process started by WshShell.Exec, with the pipes to its standard input, output and error.
 */
#include "wshom_private.h"

static const WSH_CLASS g_exec_class = {&IID_IWshExec, &CLSID_WshExec, {&IID_IWshExec, nullptr}};

typedef struct WSH_EXEC {
    WSH_OBJECT Object;
    ITextStream *In, *Out, *Err;
    DWORD ProcessId;
    HANDLE Process;
    /* running while the process has not ended, then the exit code it had. */
    volatile LONG Running;
    volatile LONG ExitCode;
} WSH_EXEC;

WSH_COMMON_METHODS(exec, IWshExec)

static void exec_destroy(WSH_OBJECT *object)
{
    WSH_EXEC *exec = (WSH_EXEC *)object;
    if (exec->Process)
        CloseHandle(exec->Process);
    if (exec->In)
        exec->In->lpVtbl->Release(exec->In);
    if (exec->Out)
        exec->Out->lpVtbl->Release(exec->Out);
    if (exec->Err)
        exec->Err->lpVtbl->Release(exec->Err);
}

static void update_status(WSH_EXEC *exec)
{
    if (!exec->Running)
        return;
    DWORD code = 0;
    GetExitCodeProcess(exec->Process, &code);
    if (code != STILL_ACTIVE) {
        InterlockedExchange(&exec->Running, 0);
        InterlockedExchange(&exec->ExitCode, (LONG)code);
    }
}

static HRESULT STDMETHODCALLTYPE exec_get_Status(IWshExec *This, WshExecStatus *status)
{
    if (!status)
        return E_POINTER;
    WSH_EXEC *exec = (WSH_EXEC *)This;
    update_status(exec);
    *status = exec->Running ? WshRunning : WshFinished;
    return S_OK;
}

#define EXEC_STREAM(name, field)                                                                                       \
    static HRESULT STDMETHODCALLTYPE exec_get_##name(IWshExec *This, ITextStream **stream)                             \
    {                                                                                                                  \
        if (!stream)                                                                                                   \
            return E_POINTER;                                                                                          \
        *stream = ((WSH_EXEC *)This)->field;                                                                           \
        (*stream)->lpVtbl->AddRef(*stream);                                                                            \
        return S_OK;                                                                                                   \
    }

EXEC_STREAM(StdIn, In)
EXEC_STREAM(StdOut, Out)
EXEC_STREAM(StdErr, Err)

static HRESULT STDMETHODCALLTYPE exec_get_ProcessID(IWshExec *This, LONG *pid)
{
    if (!pid)
        return E_POINTER;
    *pid = (LONG)((WSH_EXEC *)This)->ProcessId;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE exec_get_ExitCode(IWshExec *This, LONG *code)
{
    if (!code)
        return E_POINTER;
    WSH_EXEC *exec = (WSH_EXEC *)This;
    update_status(exec);
    *code = exec->Running ? 0 : exec->ExitCode;
    return S_OK;
}

static BOOL CALLBACK close_window_of(HWND window, LPARAM pid)
{
    DWORD owner = 0;
    GetWindowThreadProcessId(window, &owner);
    if (owner == (DWORD)pid)
        PostMessageA(window, WM_CLOSE, 0, 0);
    return TRUE;
}

static HRESULT STDMETHODCALLTYPE exec_Terminate(IWshExec *This)
{
    WSH_EXEC *exec = (WSH_EXEC *)This;
    update_status(exec);
    if (!exec->Running)
        return S_OK;
    EnumWindows(close_window_of, (LPARAM)exec->ProcessId);
    if (WaitForSingleObject(exec->Process, 2000) == WAIT_OBJECT_0 || !TerminateProcess(exec->Process, 0))
        return S_OK;
    return HrFromLastError();
}

static const IWshExecVtbl g_exec_vtbl = {
    exec_QueryInterface, exec_AddRef, exec_Release, exec_GetTypeInfoCount, exec_GetTypeInfo, exec_GetIDsOfNames,
    exec_Invoke, exec_get_Status, exec_get_StdIn, exec_get_StdOut, exec_get_StdErr, exec_get_ProcessID,
    exec_get_ExitCode, exec_Terminate,
};

HRESULT WshExecCreate(HANDLE out_read, HANDLE in_write, HANDLE err_read, IWshExec **out)
{
    *out = nullptr;
    WSH_EXEC *exec = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*exec));
    if (!exec) {
        CloseHandle(out_read);
        CloseHandle(in_write);
        CloseHandle(err_read);
        return E_OUTOFMEMORY;
    }
    WshObjectInit(&exec->Object, &g_exec_vtbl, &g_exec_class, exec_destroy);
    /* a handle the stream took is closed with it, the others here. */
    HRESULT hr = DoOpenPipeStream(out_read, ForReading, &exec->Out);
    if (SUCCEEDED(hr)) {
        out_read = nullptr;
        hr = DoOpenPipeStream(in_write, ForWriting, &exec->In);
    }
    if (SUCCEEDED(hr)) {
        in_write = nullptr;
        hr = DoOpenPipeStream(err_read, ForReading, &exec->Err);
    }
    if (FAILED(hr)) {
        WshRelease(exec);
        if (out_read)
            CloseHandle(out_read);
        if (in_write)
            CloseHandle(in_write);
        if (err_read)
            CloseHandle(err_read);
        return hr;
    }
    *out = (IWshExec *)exec;
    return S_OK;
}

HRESULT WshCreateProcess(IWshExec *interface_pointer, WCHAR *command, HANDLE out_write, HANDLE in_read, HANDLE err_write)
{
    WSH_EXEC *exec = (WSH_EXEC *)interface_pointer;
    STARTUPINFOW startup;
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = out_write;
    startup.hStdInput = in_read;
    startup.hStdError = err_write;
    PROCESS_INFORMATION info;
    memset(&info, 0, sizeof(info));
    SIZE_T length = wcslen(command);
    WCHAR *line = HeapAlloc(GetProcessHeap(), 0, (length + 1) * sizeof(WCHAR));
    if (!line)
        return E_OUTOFMEMORY;
    memcpy(line, command, (length + 1) * sizeof(WCHAR));
    BOOL ok = CreateProcessW(nullptr, line, nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &info);
    HeapFree(GetProcessHeap(), 0, line);
    if (!ok)
        return HrFromLastError();
    exec->ProcessId = info.dwProcessId;
    exec->Process = info.hProcess;
    InterlockedExchange(&exec->Running, 1);
    CloseHandle(info.hThread);
    return S_OK;
}
