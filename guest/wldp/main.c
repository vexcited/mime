/*
 * wldp.dll: the lockdown policy of the machine. Nothing here locks the machine down, so scripts and the classes they make
 * are allowed. The script hosts ask it before they run.
 */
#include <mime/win32/ole2.h>
#include <rtl.h>

int _fltused = 0x9875;

#define WLDP_LOCKDOWN_UNDEFINED 0x80000000

typedef struct WLDP_HOST_INFORMATION {
    DWORD dwRevision;
    DWORD dwHostId;
    PCWSTR szSource;
    HANDLE hSource;
} WLDP_HOST_INFORMATION;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}

DECLSPEC_EXPORT HRESULT WINAPI WldpGetLockdownPolicy(const WLDP_HOST_INFORMATION *host, DWORD *state, DWORD flags)
{
    if (!host || !state || flags)
        return E_INVALIDARG;
    if (host->dwRevision != 1)
        return E_INVALIDARG;
    *state = WLDP_LOCKDOWN_UNDEFINED;
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI WldpIsClassInApprovedList(REFCLSID clsid, const WLDP_HOST_INFORMATION *host, BOOL *approved, DWORD flags)
{
    if (!clsid || !host || !approved)
        return E_INVALIDARG;
    *approved = TRUE;
    return S_OK;
}

DECLSPEC_EXPORT HRESULT WINAPI WldpIsDynamicCodePolicyEnabled(BOOL *enabled)
{
    if (!enabled)
        return E_INVALIDARG;
    *enabled = FALSE;
    return S_OK;
}
