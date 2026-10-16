/*
 * what d3d10core.dll and d3d10_1core.dll share: the version, the layers,
 * the driver entry points d3d11.dll answers for, and the kernel thunks the
 * reference rasterizer once called, which do nothing and succeed.
 */
#include <mime/win32/winbase.h>

static HMODULE module;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        module = instance;
    return TRUE;
}

DECLSPEC_EXPORT UINT64 WINAPI D3D10CoreGetVersion(void) { return 0x000A000100041770ull; }

DECLSPEC_EXPORT HRESULT WINAPI D3D10CoreRegisterLayers(void) { return E_NOTIMPL; }

DECLSPEC_EXPORT HRESULT WINAPI D3D10CoreGetSupportedVersions(void *adapter, UINT *count, void *versions)
{
    return E_NOTIMPL;
}

/* ------------------------------------------------------------------ driver entry points */

typedef HRESULT(APIENTRY *OpenAdapter)(void *args);

static HRESULT open_adapter(const char *name, void *args)
{
    HMODULE d3d11 = GetModuleHandleA("D3D11.DLL");
    OpenAdapter open = d3d11 ? (OpenAdapter)(void *)GetProcAddress(d3d11, name) : nullptr;
    return open ? open(args) : E_NOTIMPL;
}

DECLSPEC_EXPORT HRESULT APIENTRY OpenAdapter10(void *args) { return open_adapter("OpenAdapter10", args); }

DECLSPEC_EXPORT HRESULT APIENTRY OpenAdapter10_2(void *args) { return open_adapter("OpenAdapter10_2", args); }

/* ------------------------------------------------------------------ kernel thunks */

#define KMT_NOP(name)                                                                                                  \
    DECLSPEC_EXPORT LONG APIENTRY name(void *args) { return 0; }

KMT_NOP(D3DKMTCloseAdapter)
KMT_NOP(D3DKMTCreateAllocation)
KMT_NOP(D3DKMTCreateContext)
KMT_NOP(D3DKMTCreateDevice)
KMT_NOP(D3DKMTCreateSynchronizationObject)
KMT_NOP(D3DKMTDestroyAllocation)
KMT_NOP(D3DKMTDestroyContext)
KMT_NOP(D3DKMTDestroyDevice)
KMT_NOP(D3DKMTDestroySynchronizationObject)
KMT_NOP(D3DKMTEscape)
KMT_NOP(D3DKMTGetContextSchedulingPriority)
KMT_NOP(D3DKMTGetDisplayModeList)
KMT_NOP(D3DKMTGetMultisampleMethodList)
KMT_NOP(D3DKMTGetRuntimeData)
KMT_NOP(D3DKMTGetSharedPrimaryHandle)
KMT_NOP(D3DKMTLock)
KMT_NOP(D3DKMTPresent)
KMT_NOP(D3DKMTQueryAllocationResidency)
KMT_NOP(D3DKMTRender)
KMT_NOP(D3DKMTSetAllocationPriority)
KMT_NOP(D3DKMTSetContextSchedulingPriority)
KMT_NOP(D3DKMTSetDisplayMode)
KMT_NOP(D3DKMTSetDisplayPrivateDriverFormat)
KMT_NOP(D3DKMTSetGammaRamp)
KMT_NOP(D3DKMTSetVidPnSourceOwner)
KMT_NOP(D3DKMTSignalSynchronizationObject)
KMT_NOP(D3DKMTUnlock)
KMT_NOP(D3DKMTWaitForSynchronizationObject)
KMT_NOP(D3DKMTWaitForVerticalBlankEvent)

DECLSPEC_EXPORT LONG APIENTRY D3DKMTOpenResource(void *args) { return (LONG)STATUS_INVALID_PARAMETER; }

DECLSPEC_EXPORT LONG APIENTRY D3DKMTQueryResourceInfo(void *args) { return (LONG)STATUS_INVALID_PARAMETER; }

typedef struct KmtQueryAdapterInfo {
    UINT adapter;
    UINT type;
    void *data;
    UINT size;
} KmtQueryAdapterInfo;

/* the user mode driver's name is this dll's path; the other kinds answer zeros or nothing. */
DECLSPEC_EXPORT LONG APIENTRY D3DKMTQueryAdapterInfo(KmtQueryAdapterInfo *info)
{
    if (info->type == 1) {
        UINT *name = info->data;
        if (name[0] != 1)
            return (LONG)STATUS_NOT_IMPLEMENTED;
        if (!GetModuleFileNameW(module, (WCHAR *)(name + 1), MAX_PATH))
            return (LONG)STATUS_UNSUCCESSFUL;
    } else if (info->type == 11) {
        *(UINT *)info->data = 0;
    }
    return 0;
}

typedef struct KmtDeviceState {
    UINT device;
    UINT type;
    UINT64 state[5];
} KmtDeviceState;

/* the device executes normally and has presented nothing. */
DECLSPEC_EXPORT LONG APIENTRY D3DKMTGetDeviceState(KmtDeviceState *state)
{
    if (state->type == 1)
        *(UINT *)state->state = 1;
    else if (state->type == 2)
        memset(state->state, 0, sizeof(state->state));
    else
        return (LONG)STATUS_UNSUCCESSFUL;
    return 0;
}

typedef struct KmtOpenAdapterFromHdc {
    HDC dc;
    UINT adapter;
    LUID luid;
    UINT source;
} KmtOpenAdapterFromHdc;

/* every display is the same fake adapter, under a new identifier each time. */
DECLSPEC_EXPORT LONG APIENTRY D3DKMTOpenAdapterFromHdc(KmtOpenAdapterFromHdc *open)
{
    static LONG next;
    open->adapter = 0xF0F;
    open->luid.LowPart = (DWORD)InterlockedIncrement(&next) + 0x10000;
    open->luid.HighPart = 0;
    return 0;
}
