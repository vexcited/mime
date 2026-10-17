/*
 * what winmm keeps besides devices: installable drivers, auxiliary
 * devices, the rest of wave input, versions.
 */
#define MIME_BUILD_WINMM
#include "winmm_private.h"

/* ------------------------------------------------------------------ installable drivers */

typedef struct DRIVER {
    struct DRIVER *next;
    HMODULE module;
    DRIVERPROC proc;
    DWORD_PTR id; /* what DRV_OPEN returned */
} DRIVER;

static RTL_CRITICAL_SECTION g_lock = {nullptr, -1, 0, nullptr, nullptr, 0};
static DRIVER *g_drivers;

static DRIVER *find_driver(HDRVR h)
{
    RtlEnterCriticalSection(&g_lock);
    DRIVER *d = g_drivers;
    while (d && d != (DRIVER *)h)
        d = d->next;
    RtlLeaveCriticalSection(&g_lock);
    return d;
}

static BOOL module_open(HMODULE module)
{
    for (DRIVER *d = g_drivers; d; d = d->next)
        if (d->module == module)
            return TRUE;
    return FALSE;
}

/*
 * the driver's file is the value the name has in the section, Drivers32 by
 * default, of the registry's drivers, or the name itself. the first
 * instance of a module loads and enables it.
 */
WINMMAPI HDRVR WINAPI OpenDriver(LPCWSTR name, LPCWSTR section, LPARAM param)
{
    if (!name)
        return nullptr;
    WCHAR file[MAX_PATH], key[128];
    DWORD size = sizeof(file);
    lstrcpyW(key, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\");
    lstrcatW(key, section && lstrlenW(section) < 64 ? section : L"Drivers32");
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, name, RRF_RT_REG_SZ, nullptr, file, &size))
        lstrcpynW(file, name, MAX_PATH);
    HMODULE module = LoadLibraryW(file);
    /* a path the installation does not have, such as setup's X: drive, falls back to the file's name. */
    const WCHAR *base = file;
    for (const WCHAR *p = file; *p; p++)
        if (*p == '\\' || *p == '/')
            base = p + 1;
    if (!module && base != file)
        module = LoadLibraryW(base);
    DRIVERPROC proc = module ? (DRIVERPROC)(void *)GetProcAddress(module, "DriverProc") : nullptr;
    if (!proc) {
        if (module)
            FreeLibrary(module);
        return nullptr;
    }
    DRIVER *d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*d));
    if (!d) {
        FreeLibrary(module);
        return nullptr;
    }
    d->module = module;
    d->proc = proc;
    RtlEnterCriticalSection(&g_lock);
    BOOL first = !module_open(module);
    d->next = g_drivers;
    g_drivers = d;
    RtlLeaveCriticalSection(&g_lock);
    HDRVR h = (HDRVR)d;
    if (first && (!proc(0, h, DRV_LOAD, 0, 0) || !proc(0, h, DRV_ENABLE, 0, 0)))
        goto fail;
    d->id = (DWORD_PTR)proc(0, h, DRV_OPEN, (LPARAM)L"", param);
    if (!d->id)
        goto fail;
    return h;
fail:
    RtlEnterCriticalSection(&g_lock);
    for (DRIVER **p = &g_drivers; *p; p = &(*p)->next)
        if (*p == d) {
            *p = d->next;
            break;
        }
    RtlLeaveCriticalSection(&g_lock);
    HeapFree(GetProcessHeap(), 0, d);
    FreeLibrary(module);
    return nullptr;
}

WINMMAPI LRESULT WINAPI SendDriverMessage(HDRVR h, UINT message, LPARAM param1, LPARAM param2)
{
    DRIVER *d = find_driver(h);
    return d ? d->proc(d->id, h, message, param1, param2) : 0;
}

/* the last instance of a module disables and frees it. */
WINMMAPI LRESULT WINAPI CloseDriver(HDRVR h, LPARAM param1, LPARAM param2)
{
    DRIVER *d = find_driver(h);
    if (!d)
        return 0;
    LRESULT r = d->proc(d->id, h, DRV_CLOSE, param1, param2);
    RtlEnterCriticalSection(&g_lock);
    for (DRIVER **p = &g_drivers; *p; p = &(*p)->next)
        if (*p == d) {
            *p = d->next;
            break;
        }
    BOOL last = !module_open(d->module);
    RtlLeaveCriticalSection(&g_lock);
    if (last) {
        d->proc(d->id, h, DRV_DISABLE, 0, 0);
        d->proc(d->id, h, DRV_FREE, 0, 0);
    }
    FreeLibrary(d->module);
    HeapFree(GetProcessHeap(), 0, d);
    return r;
}

WINMMAPI LRESULT WINAPI DefDriverProc(DWORD_PTR id, HDRVR driver, UINT message, LPARAM param1, LPARAM param2)
{
    switch (message) {
    case DRV_LOAD:
    case DRV_FREE:
    case DRV_ENABLE:
    case DRV_DISABLE:
        return 1;
    case DRV_INSTALL:
    case DRV_REMOVE:
        return DRVCNF_OK;
    default:
        return 0;
    }
}

WINMMAPI HMODULE WINAPI DrvGetModuleHandle(HDRVR h)
{
    DRIVER *d = find_driver(h);
    return d ? d->module : nullptr;
}

WINMMAPI HMODULE WINAPI GetDriverModuleHandle(HDRVR h)
{
    return DrvGetModuleHandle(h);
}

/* ------------------------------------------------------------------ auxiliary devices */

WINMMAPI MMRESULT WINAPI auxGetDevCapsW(UINT_PTR id, void *caps, UINT size)
{
    return MMSYSERR_BADDEVICEID;
}

WINMMAPI MMRESULT WINAPI auxGetDevCapsA(UINT_PTR id, void *caps, UINT size)
{
    return MMSYSERR_BADDEVICEID;
}

WINMMAPI MMRESULT WINAPI auxGetVolume(UINT id, LPDWORD volume)
{
    return MMSYSERR_BADDEVICEID;
}

WINMMAPI MMRESULT WINAPI auxSetVolume(UINT id, DWORD volume)
{
    return MMSYSERR_BADDEVICEID;
}

WINMMAPI MMRESULT WINAPI auxOutMessage(UINT id, UINT message, DWORD_PTR param1, DWORD_PTR param2)
{
    return MMSYSERR_BADDEVICEID;
}

/* ------------------------------------------------------------------ wave input */

WINMMAPI MMRESULT WINAPI waveInGetErrorTextW(MMRESULT error, LPWSTR text, UINT size)
{
    return MmErrorTextW(error, text, size);
}

WINMMAPI MMRESULT WINAPI waveInGetErrorTextA(MMRESULT error, LPSTR text, UINT size)
{
    return MmErrorTextA(error, text, size);
}

WINMMAPI MMRESULT WINAPI waveInPrepareHeader(HWAVEIN wave, LPWAVEHDR header, UINT size)
{
    return MMSYSERR_INVALHANDLE;
}

WINMMAPI MMRESULT WINAPI waveInUnprepareHeader(HWAVEIN wave, LPWAVEHDR header, UINT size)
{
    return MMSYSERR_INVALHANDLE;
}

WINMMAPI MMRESULT WINAPI waveInAddBuffer(HWAVEIN wave, LPWAVEHDR header, UINT size)
{
    return MMSYSERR_INVALHANDLE;
}

WINMMAPI MMRESULT WINAPI waveInStart(HWAVEIN wave)
{
    return MMSYSERR_INVALHANDLE;
}

WINMMAPI MMRESULT WINAPI waveInStop(HWAVEIN wave)
{
    return MMSYSERR_INVALHANDLE;
}

WINMMAPI MMRESULT WINAPI waveInReset(HWAVEIN wave)
{
    return MMSYSERR_INVALHANDLE;
}

WINMMAPI MMRESULT WINAPI waveInGetPosition(HWAVEIN wave, LPMMTIME time, UINT size)
{
    return MMSYSERR_INVALHANDLE;
}

WINMMAPI MMRESULT WINAPI waveInGetID(HWAVEIN wave, LPUINT id)
{
    return MMSYSERR_INVALHANDLE;
}

WINMMAPI MMRESULT WINAPI waveInMessage(HWAVEIN wave, UINT message, DWORD_PTR param1, DWORD_PTR param2)
{
    return wave ? MMSYSERR_INVALHANDLE : MMSYSERR_BADDEVICEID;
}

/* ------------------------------------------------------------------ versions and tasks */

WINMMAPI UINT WINAPI mmsystemGetVersion(void)
{
    return 0x030A;
}

WINMMAPI UINT WINAPI mmTaskCreate(LPTHREAD_START_ROUTINE routine, HANDLE *thread, DWORD_PTR param)
{
    HANDLE h = CreateThread(nullptr, 0, routine, (void *)param, 0, nullptr);
    if (!h)
        return MMSYSERR_NOMEM;
    if (thread)
        *thread = h;
    else
        CloseHandle(h);
    return MMSYSERR_NOERROR;
}

WINMMAPI void WINAPI mmTaskBlock(DWORD thread)
{
    MSG msg;
    do
        GetMessageW(&msg, nullptr, 0, 0);
    while (msg.message < WM_USER);
}

WINMMAPI BOOL WINAPI mmTaskSignal(DWORD thread)
{
    return PostThreadMessageW(thread, WM_USER, 0, 0);
}

WINMMAPI void WINAPI mmTaskYield(void)
{
}

WINMMAPI DWORD WINAPI mmGetCurrentTask(void)
{
    return GetCurrentThreadId();
}

WINMMAPI BOOL WINAPI mmDrvInstall(HDRVR driver, LPCWSTR name, DRIVERPROC proc, UINT flags)
{
    return FALSE;
}

WINMMAPI void WINAPI WOWAppExit(HANDLE task)
{
}
