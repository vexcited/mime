/*
 * query.dll: the binding of IFilter objects to files. the filter of a file is found through the persistent handler of
 * its extension, and no handler is registered, so the class that would give the filter is not registered either.
 */
#include <mime/win32/winbase.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

#define E_INVALIDARG_ ((HRESULT)0x80070057)
#define E_NOTIMPL_ ((HRESULT)0x80004001)
#define REGDB_E_CLASSNOTREG_ ((HRESULT)0x80040154)
#define CLASS_E_CLASSNOTAVAILABLE_ ((HRESULT)0x80040111)

HRESULT WINAPI LoadIFilter(PCWSTR path, void *outer, void **filter)
{
    if (!path)
        return E_INVALIDARG_;
    if (filter)
        *filter = nullptr;
    return REGDB_E_CLASSNOTREG_;
}

HRESULT WINAPI LoadIFilterEx(PCWSTR path, DWORD flags, const GUID *interface_id, void **filter)
{
    if (!path || !filter)
        return E_INVALIDARG_;
    *filter = nullptr;
    return REGDB_E_CLASSNOTREG_;
}

HRESULT WINAPI BindIFilterFromStorage(void *storage, void *outer, void **filter)
{
    if (!storage)
        return E_INVALIDARG_;
    if (filter)
        *filter = nullptr;
    return REGDB_E_CLASSNOTREG_;
}

HRESULT WINAPI BindIFilterFromStream(void *stream, void *outer, void **filter)
{
    if (!stream)
        return E_INVALIDARG_;
    if (filter)
        *filter = nullptr;
    return REGDB_E_CLASSNOTREG_;
}

HRESULT WINAPI LoadBinaryFilter(void *filter)
{
    return E_NOTIMPL_;
}

HRESULT WINAPI LoadTextFilter(PCWSTR path, void **filter)
{
    if (filter)
        *filter = nullptr;
    return E_NOTIMPL_;
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return 0;
}

HRESULT WINAPI DllGetClassObject(const GUID *clsid, const GUID *iid, void **object)
{
    if (object)
        *object = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE_;
}

HRESULT WINAPI DllRegisterServer(void)
{
    return 0;
}

HRESULT WINAPI DllUnregisterServer(void)
{
    return 0;
}
