/*
 * urlmon: URL monikers. only URLDownloadToFileW is exported, and it needs the network.
 */
#include <mime/win32/objbase.h>
#include <mime/stub.h>

#define INET_E_DOWNLOAD_FAILURE ((HRESULT)0x800C0008L)

typedef struct IBindStatusCallback IBindStatusCallback, *LPBINDSTATUSCALLBACK;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

DECLSPEC_EXPORT HRESULT WINAPI URLDownloadToFileW(LPUNKNOWN caller, LPCWSTR url, LPCWSTR file, DWORD reserved,
                                                 LPBINDSTATUSCALLBACK callback)
{
    MIME_STUB("urlmon");
    if (!url || !file)
        return E_INVALIDARG;
    return INET_E_DOWNLOAD_FAILURE;
}
