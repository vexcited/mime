#include "rpcss.h"
#include <stdarg.h>
#include <mime/ntrtl.h>

static SERVICE_STATUS_HANDLE g_rpcss_handle, g_launch_handle;
static SERVICE_STATUS g_rpcss_status, g_launch_status;

void RsLog(const char *format, ...)
{
    char text[512] = "rpcss: ";
    va_list args;
    va_start(args, format);
    int n = _vsnprintf(text + 7, sizeof(text) - 10, format, args);
    va_end(args);
    if (n < 0)
        n = (int)sizeof(text) - 10;
    text[7 + n] = '\n';
    text[8 + n] = 0;
    /* through the kernel's debug print: genuine kernelbase's OutputDebugString only reaches a debugger. */
    DbgPrintEx(101 /* DPFLTR_DEFAULT_ID */, 0 /* DPFLTR_ERROR_LEVEL, always shown */, "%s", text);
}

void *RsAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
}

void *RsAllocZero(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void RsFree(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

WCHAR *RsDupString(const WCHAR *s)
{
    if (!s)
        return nullptr;
    SIZE_T n = (wcslen(s) + 1) * sizeof(WCHAR);
    WCHAR *d = RsAlloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}

/* ids of oxids, oids and the reserved ones are random, as rpcss's AllocateId makes them; never 0 or all ones. */
UINT64 RsNewId(void)
{
    UINT64 id = 0;
    while (!id || id == ~0ull)
        SystemFunction036(&id, sizeof(id));
    return id;
}

void *WINAPI MIDL_user_allocate(size_t size)
{
    return RsAlloc(size);
}

void WINAPI MIDL_user_free(void *p)
{
    RsFree(p);
}

static DWORD WINAPI control_handler(DWORD control, DWORD type, LPVOID data, LPVOID context)
{
    if (control != SERVICE_CONTROL_INTERROGATE)
        return ERROR_CALL_NOT_IMPLEMENTED;
    if (context == &g_rpcss_status)
        SetServiceStatus(g_rpcss_handle, &g_rpcss_status);
    else
        SetServiceStatus(g_launch_handle, &g_launch_status);
    return NO_ERROR;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

/*
 * rpcss.dll serves two services, as on windows: RpcSs in the RPCSS group, next to the endpoint mapper, which resolves
 * oxids and activates, and DcomLaunch in its own group, which starts the servers. neither can be stopped.
 */
void WINAPI ServiceMain(DWORD argc, LPWSTR *argv)
{
    if (!argc)
        return;
    BOOL rpcss = !_wcsicmp(argv[0], L"RpcSs");
    SERVICE_STATUS *status = rpcss ? &g_rpcss_status : &g_launch_status;
    SERVICE_STATUS_HANDLE handle = RegisterServiceCtrlHandlerExW(argv[0], control_handler, status);
    if (!handle)
        return;
    if (rpcss)
        g_rpcss_handle = handle;
    else
        g_launch_handle = handle;
    status->dwServiceType = SERVICE_WIN32_SHARE_PROCESS;
    DWORD error = rpcss ? RsStartResolver() : ERROR_SUCCESS;
    if (error) {
        RsLog("the resolver did not start: %u", error);
        status->dwCurrentState = SERVICE_STOPPED;
        status->dwWin32ExitCode = error;
    } else {
        status->dwCurrentState = SERVICE_RUNNING;
    }
    SetServiceStatus(handle, status);
}
