/*
 * xolehlp.dll: the helper that hands out the transaction manager of the distributed transaction coordinator. this system
 * has no coordinator, so every request for one says it is not available, as it does on a machine whose coordinator is
 * stopped.
 */
#include <mime/win32/winbase.h>

#define XOLEHLP_EXPORT DECLSPEC_EXPORT
#define XACT_E_TMNOTAVAILABLE ((HRESULT)0x8004D01B)

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

XOLEHLP_EXPORT HRESULT WINAPI DtcGetTransactionManagerExW(LPWSTR host, LPWSTR name, const GUID *iid, DWORD options, void *config,
                                                          void **manager)
{
    if (manager)
        *manager = nullptr;
    return XACT_E_TMNOTAVAILABLE;
}

XOLEHLP_EXPORT HRESULT WINAPI DtcGetTransactionManagerExA(LPSTR host, LPSTR name, const GUID *iid, DWORD options, void *config,
                                                          void **manager)
{
    if (manager)
        *manager = nullptr;
    return XACT_E_TMNOTAVAILABLE;
}

XOLEHLP_EXPORT HRESULT WINAPI DtcGetTransactionManager(LPSTR host, LPSTR name, const GUID *iid, DWORD reserved1, WORD reserved2,
                                                       void *reserved3, void **manager)
{
    if (manager)
        *manager = nullptr;
    return XACT_E_TMNOTAVAILABLE;
}

XOLEHLP_EXPORT HRESULT WINAPI DtcGetTransactionManagerC(LPSTR host, LPSTR name, const GUID *iid, DWORD reserved1, WORD reserved2,
                                                        void *reserved3, void **manager)
{
    if (manager)
        *manager = nullptr;
    return XACT_E_TMNOTAVAILABLE;
}

XOLEHLP_EXPORT HRESULT WINAPI DtcGetTransactionManagerEx(LPSTR host, LPSTR name, const GUID *iid, DWORD options, void *config,
                                                         void **manager)
{
    if (manager)
        *manager = nullptr;
    return XACT_E_TMNOTAVAILABLE;
}

XOLEHLP_EXPORT HINSTANCE WINAPI GetDtcLocaleResourceHandle(void)
{
    return nullptr;
}

XOLEHLP_EXPORT HRESULT WINAPI FreezeLocalTransactionManagers(void)
{
    return XACT_E_TMNOTAVAILABLE;
}

XOLEHLP_EXPORT HRESULT WINAPI ThawLocalTransactionManagers(void)
{
    return XACT_E_TMNOTAVAILABLE;
}
