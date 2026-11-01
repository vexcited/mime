/*
 * WScript.Network: the user and computer names and the connections to network drives and printers.
 */
#include "wshom_private.h"

DECLSPEC_IMPORT DWORD WINAPI WNetAddConnection2W(NETRESOURCEW *resource, LPCWSTR password, LPCWSTR user, DWORD flags);
DECLSPEC_IMPORT DWORD WINAPI WNetCancelConnection2W(LPCWSTR name, DWORD flags, BOOL force);
DECLSPEC_IMPORT DWORD WINAPI WNetOpenEnumW(DWORD scope, DWORD type, DWORD usage, NETRESOURCEW *resource, HANDLE *out);
DECLSPEC_IMPORT DWORD WINAPI WNetEnumResourceW(HANDLE handle, LPDWORD count, LPVOID buffer, LPDWORD size);
DECLSPEC_IMPORT DWORD WINAPI WNetCloseEnum(HANDLE handle);

static const WSH_CLASS g_network_class = {
    &IID_IWshNetwork2, &CLSID_WshNetwork, {&IID_IWshNetwork, &IID_IWshNetwork2, nullptr}};

typedef struct WSH_NETWORK {
    WSH_OBJECT Object;
} WSH_NETWORK;

WSH_COMMON_METHODS(network, IWshNetwork2)

static HRESULT win32_hr(DWORD status)
{
    return (LONG)status > 0 ? (HRESULT)(0x80070000u | (status & 0xFFFF)) : (HRESULT)status;
}

static HRESULT STDMETHODCALLTYPE network_get_UserName(IWshNetwork2 *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    WCHAR name[257];
    DWORD size = 257;
    if (!GetUserNameW(name, &size))
        return HrFromLastError();
    *out = SysAllocString(name);
    if (!size)
        return S_OK;
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE network_get_UserDomain(IWshNetwork2 *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    WCHAR domain[0x400];
    DWORD size = GetEnvironmentVariableW(L"USERDOMAIN", domain, 0x400);
    if (!size)
        domain[0] = 0;
    *out = SysAllocString(domain);
    if (!size)
        return S_OK;
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE network_get_ComputerName(IWshNetwork2 *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    WCHAR name[16];
    DWORD size = 16;
    if (!GetComputerNameW(name, &size))
        return HrFromLastError();
    *out = SysAllocString(name);
    if (!size)
        return S_OK;
    return *out ? S_OK : E_OUTOFMEMORY;
}

static HRESULT STDMETHODCALLTYPE network_get_UserProfile(IWshNetwork2 *This, BSTR *out)
{
    return out ? E_NOTIMPL : E_POINTER;
}

static HRESULT STDMETHODCALLTYPE network_get_Organization(IWshNetwork2 *This, BSTR *out)
{
    return out ? E_NOTIMPL : E_POINTER;
}

static HRESULT STDMETHODCALLTYPE network_get_Site(IWshNetwork2 *This, BSTR *out)
{
    return out ? E_NOTIMPL : E_POINTER;
}

/* a connection of a local name to a remote one, with the arguments a script may leave out. */
static HRESULT add_connection(DWORD type, BSTR local, BSTR remote, VARIANT *update, VARIANT *user, VARIANT *password)
{
    if (!local || !remote || !update || !user || !password)
        return E_POINTER;
    DWORD flags = 0;
    BSTR user_name = nullptr, secret = nullptr;
    HRESULT hr = S_OK;
    if (WshArgumentGiven(update)) {
        int value = 0;
        hr = WshVariantBool(update, &value);
        if (FAILED(hr))
            goto done;
        if (value)
            flags = CONNECT_UPDATE_PROFILE;
    }
    if (WshArgumentGiven(user) && user->vt != VT_EMPTY && user->vt != VT_NULL) {
        hr = WshVariantBstr(user, &user_name);
        if (FAILED(hr))
            goto done;
    }
    if (WshArgumentGiven(password) && password->vt != VT_EMPTY && password->vt != VT_NULL) {
        hr = WshVariantBstr(password, &secret);
        if (FAILED(hr))
            goto done;
    }
    NETRESOURCEW resource;
    memset(&resource, 0, sizeof(resource));
    resource.dwType = type;
    resource.lpLocalName = local;
    resource.lpRemoteName = remote;
    hr = win32_hr(WNetAddConnection2W(&resource, secret, user_name, flags));
done:
    SysFreeString(user_name);
    SysFreeString(secret);
    return hr;
}

static HRESULT STDMETHODCALLTYPE network_MapNetworkDrive(IWshNetwork2 *This, BSTR local, BSTR remote, VARIANT *update,
                                                         VARIANT *user, VARIANT *password)
{
    HRESULT hr = add_connection(RESOURCETYPE_DISK, local, remote, update, user, password);
    if (FAILED(hr))
        WshSignalHr(&IID_IWshNetwork, L"WSHNetwork.MapNetworkDrive", hr);
    return hr;
}

static HRESULT STDMETHODCALLTYPE network_AddPrinterConnection(IWshNetwork2 *This, BSTR local, BSTR remote,
                                                              VARIANT *update, VARIANT *user, VARIANT *password)
{
    HRESULT hr = add_connection(RESOURCETYPE_PRINT, local, remote, update, user, password);
    if (FAILED(hr))
        WshSignalHr(&IID_IWshNetwork, L"WSHNetwork.AddPrinterConnection", hr);
    return hr;
}

static HRESULT cancel_connection(BSTR name, VARIANT *force, VARIANT *update, const WCHAR *source)
{
    if (!name || !force || !update)
        return E_POINTER;
    int forced = 0;
    DWORD flags = 0;
    if (WshArgumentGiven(force)) {
        HRESULT hr = WshVariantBool(force, &forced);
        if (FAILED(hr))
            return hr;
    }
    if (WshArgumentGiven(update)) {
        int value = 0;
        HRESULT hr = WshVariantBool(update, &value);
        if (FAILED(hr))
            return hr;
        if (value)
            flags = CONNECT_UPDATE_PROFILE;
    }
    HRESULT hr = win32_hr(WNetCancelConnection2W(name, flags, forced != 0));
    if (FAILED(hr))
        WshSignalHr(&IID_IWshNetwork, source, hr);
    return hr;
}

static HRESULT STDMETHODCALLTYPE network_RemoveNetworkDrive(IWshNetwork2 *This, BSTR name, VARIANT *force, VARIANT *update)
{
    return cancel_connection(name, force, update, L"WSHNetwork.RemoveNetworkDrive");
}

static HRESULT STDMETHODCALLTYPE network_RemovePrinterConnection(IWshNetwork2 *This, BSTR name, VARIANT *force,
                                                                 VARIANT *update)
{
    /* the source names the other method, as it does in Windows. */
    return cancel_connection(name, force, update, L"WSHNetwork.RemoveNetworkDrive");
}

/* the local and remote names of the connections of a type, one after the other. */
static HRESULT enum_connections(DWORD type, IWshCollection **out)
{
    if (!out)
        return E_POINTER;
    HANDLE handle = nullptr;
    DWORD status = WNetOpenEnumW(RESOURCE_CONNECTED, type, 0, nullptr, &handle);
    if (status)
        return win32_hr(status);
    SAFEARRAYBOUND bound = {100, 0};
    SAFEARRAY *array = SafeArrayCreate(VT_VARIANT, 1, &bound);
    DWORD size = 0x4000;
    BYTE *buffer = HeapAlloc(GetProcessHeap(), 0, size);
    HRESULT hr = array && buffer ? S_OK : E_OUTOFMEMORY;
    LONG index = 0;
    while (SUCCEEDED(hr)) {
        DWORD count = 1, capacity = size;
        status = WNetEnumResourceW(handle, &count, buffer, &capacity);
        if (capacity > size && status == ERROR_MORE_DATA) {
            HeapFree(GetProcessHeap(), 0, buffer);
            size = capacity;
            buffer = HeapAlloc(GetProcessHeap(), 0, size);
            if (!buffer)
                hr = E_OUTOFMEMORY;
            continue;
        }
        if (status == ERROR_NO_MORE_ITEMS) {
            bound.cElements = index;
            hr = SafeArrayRedim(array, &bound);
            break;
        }
        if (status) {
            hr = win32_hr(status);
            break;
        }
        if (index + 2 > (LONG)bound.cElements) {
            bound.cElements += 100;
            hr = SafeArrayRedim(array, &bound);
            if (FAILED(hr))
                break;
        }
        NETRESOURCEW *resource = (NETRESOURCEW *)buffer;
        const WCHAR *names[2] = {resource->lpLocalName, resource->lpRemoteName};
        for (int i = 0; i < 2 && SUCCEEDED(hr); i++) {
            VARIANT item;
            VariantInit(&item);
            item.vt = VT_BSTR;
            if (names[i] && *names[i]) {
                item.bstrVal = SysAllocString(names[i]);
                if (!item.bstrVal)
                    hr = E_OUTOFMEMORY;
            }
            if (SUCCEEDED(hr))
                hr = SafeArrayPutElement(array, &index, &item);
            VariantClear(&item);
            index++;
        }
    }
    if (SUCCEEDED(hr))
        hr = WshCollectionCreate(array, nullptr, out);
    if (array)
        SafeArrayDestroy(array);
    if (buffer)
        HeapFree(GetProcessHeap(), 0, buffer);
    WNetCloseEnum(handle);
    return hr;
}

static HRESULT STDMETHODCALLTYPE network_EnumNetworkDrives(IWshNetwork2 *This, IWshCollection **out)
{
    return enum_connections(RESOURCETYPE_DISK, out);
}

static HRESULT STDMETHODCALLTYPE network_EnumPrinterConnections(IWshNetwork2 *This, IWshCollection **out)
{
    return enum_connections(RESOURCETYPE_PRINT, out);
}

/* the print spooler's calls, which the system may not have. */
static FARPROC spooler_call(const char *name)
{
    HMODULE spooler = LoadLibraryW(L"winspool.drv");
    return spooler ? GetProcAddress(spooler, name) : nullptr;
}

static HRESULT STDMETHODCALLTYPE network_SetDefaultPrinter(IWshNetwork2 *This, BSTR name)
{
    BOOL(WINAPI * set_default)(LPCWSTR) = (BOOL(WINAPI *)(LPCWSTR))spooler_call("SetDefaultPrinterW");
    if (set_default && name && *name && set_default(name))
        return S_OK;
    WshSignal(&IID_IWshNetwork, L"WSHNetwork.SetDefaultPrinter", MSG_NO_PRINTER, name);
    return DISP_E_EXCEPTION_HR;
}

static HRESULT STDMETHODCALLTYPE network_AddWindowsPrinterConnection(IWshNetwork2 *This, BSTR name, BSTR driver,
                                                                    BSTR port)
{
    BOOL(WINAPI * add)(LPWSTR) = (BOOL(WINAPI *)(LPWSTR))spooler_call("AddPrinterConnectionW");
    HRESULT hr;
    if (!add)
        hr = win32_hr(ERROR_PROC_NOT_FOUND);
    else
        hr = add(name) ? S_OK : HrFromLastError();
    return hr;
}

static const IWshNetwork2Vtbl g_network_vtbl = {
    network_QueryInterface, network_AddRef, network_Release, network_GetTypeInfoCount, network_GetTypeInfo,
    network_GetIDsOfNames, network_Invoke, network_get_UserDomain, network_get_UserName, network_get_UserProfile,
    network_get_ComputerName, network_get_Organization, network_get_Site, network_MapNetworkDrive,
    network_RemoveNetworkDrive, network_EnumNetworkDrives, network_AddPrinterConnection,
    network_RemovePrinterConnection, network_EnumPrinterConnections, network_SetDefaultPrinter,
    network_AddWindowsPrinterConnection,
};

HRESULT WshNetworkCreate(REFIID riid, void **object)
{
    WSH_NETWORK *network = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*network));
    if (!network)
        return E_OUTOFMEMORY;
    WshObjectInit(&network->Object, &g_network_vtbl, &g_network_class, nullptr);
    HRESULT hr = WshQueryInterface(network, riid, object);
    WshRelease(network);
    return hr;
}
