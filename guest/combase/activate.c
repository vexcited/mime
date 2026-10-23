/*
 * toast activation through an application's COM server: the activator class is found from the application's identity,
 * and its INotificationActivationCallback gets the toast or button arguments and any typed text. ole32 starts the
 * server when it is not running.
 */
#include <mime/win32/winbase.h>
#include <mime/win32/shobjidl.h>
#include <mime/win32/propsys.h>
#include <mime/win32/notificationactivationcallback.h>
#include <rtl.h>

BOOL CbActivateApplication(const WCHAR *aumid, const WCHAR *arguments, const WCHAR *key, const WCHAR *value);

typedef HRESULT(WINAPI *CO_INITIALIZE_EX)(LPVOID, DWORD);
typedef HRESULT(WINAPI *CO_CREATE_INSTANCE)(REFCLSID, IUnknown *, DWORD, REFIID, void **);
typedef HRESULT(WINAPI *CLSID_FROM_STRING)(LPCWSTR, LPCLSID);
typedef HRESULT(WINAPI *PROP_VARIANT_CLEAR)(PROPVARIANT *);

typedef struct OLE_API {
    CO_INITIALIZE_EX Initialize;
    CO_CREATE_INSTANCE CreateInstance;
    CLSID_FROM_STRING ClsidFromString;
    PROP_VARIANT_CLEAR Clear;
} OLE_API;

static BOOL ole_api(OLE_API *api)
{
    HMODULE ole32 = LoadLibraryW(L"ole32.dll");
    if (!ole32)
        return FALSE;
    api->Initialize = (CO_INITIALIZE_EX)(void *)GetProcAddress(ole32, "CoInitializeEx");
    api->CreateInstance = (CO_CREATE_INSTANCE)(void *)GetProcAddress(ole32, "CoCreateInstance");
    api->ClsidFromString = (CLSID_FROM_STRING)(void *)GetProcAddress(ole32, "CLSIDFromString");
    api->Clear = (PROP_VARIANT_CLEAR)(void *)GetProcAddress(ole32, "PropVariantClear");
    return api->Initialize && api->CreateInstance && api->ClsidFromString && api->Clear;
}

/*
 * the CustomActivator value of the registered identity, else the activator named by a start menu shortcut with the
 * same identity. the order is a guess: windows keeps desktop programs' registered identities in the shortcut.
 */
static BOOL activator_from_registry(const OLE_API *api, const WCHAR *aumid, CLSID *clsid)
{
    WCHAR key[300], value[64];
    if (wcslen(aumid) > 200)
        return FALSE;
    wcscpy(key, L"Software\\Classes\\AppUserModelId\\");
    wcscat(key, aumid);
    HKEY hkey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_READ, &hkey) != ERROR_SUCCESS)
        return FALSE;
    DWORD type, size = sizeof(value) - sizeof(WCHAR);
    LSTATUS status = RegQueryValueExW(hkey, L"CustomActivator", nullptr, &type, (BYTE *)value, &size);
    RegCloseKey(hkey);
    if (status != ERROR_SUCCESS || type != REG_SZ)
        return FALSE;
    value[size / sizeof(WCHAR)] = 0;
    return SUCCEEDED(api->ClsidFromString(value, clsid));
}

static BOOL activator_of_shortcut(const OLE_API *api, const WCHAR *path, const WCHAR *aumid, CLSID *clsid)
{
    IShellLinkW *link;
    if (FAILED(api->CreateInstance(&CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&link)))
        return FALSE;
    BOOL found = FALSE;
    IPersistFile *file;
    if (SUCCEEDED(link->lpVtbl->QueryInterface(link, &IID_IPersistFile, (void **)&file))) {
        IPropertyStore *store;
        if (SUCCEEDED(file->lpVtbl->Load(file, path, STGM_READ)) &&
            SUCCEEDED(link->lpVtbl->QueryInterface(link, &IID_IPropertyStore, (void **)&store))) {
            PROPVARIANT id, activator;
            memset(&id, 0, sizeof(id));
            memset(&activator, 0, sizeof(activator));
            if (SUCCEEDED(store->lpVtbl->GetValue(store, &PKEY_AppUserModel_ID, &id)) && id.vt == VT_LPWSTR &&
                id.pwszVal && !_wcsicmp(id.pwszVal, aumid) &&
                SUCCEEDED(store->lpVtbl->GetValue(store, &PKEY_AppUserModel_ToastActivatorCLSID, &activator)) &&
                activator.vt == VT_CLSID && activator.puuid) {
                *clsid = *activator.puuid;
                found = TRUE;
            }
            api->Clear(&id);
            api->Clear(&activator);
            store->lpVtbl->Release(store);
        }
        file->lpVtbl->Release(file);
    }
    link->lpVtbl->Release(link);
    return found;
}

static BOOL activator_in_folder(const OLE_API *api, const WCHAR *folder, const WCHAR *aumid, CLSID *clsid, int depth)
{
    WCHAR pattern[MAX_PATH];
    if (wcslen(folder) + 3 >= MAX_PATH)
        return FALSE;
    wcscpy(pattern, folder);
    wcscat(pattern, L"\\*");
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return FALSE;
    BOOL found = FALSE;
    do {
        if (data.cFileName[0] == L'.' && (!data.cFileName[1] || (data.cFileName[1] == L'.' && !data.cFileName[2])))
            continue;
        WCHAR path[MAX_PATH];
        if (wcslen(folder) + wcslen(data.cFileName) + 2 >= MAX_PATH)
            continue;
        wcscpy(path, folder);
        wcscat(path, L"\\");
        wcscat(path, data.cFileName);
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (depth > 0)
                found = activator_in_folder(api, path, aumid, clsid, depth - 1);
        } else {
            SIZE_T n = wcslen(data.cFileName);
            if (n > 4 && !_wcsicmp(data.cFileName + n - 4, L".lnk"))
                found = activator_of_shortcut(api, path, aumid, clsid);
        }
    } while (!found && FindNextFileW(find, &data));
    FindClose(find);
    return found;
}

static BOOL find_activator(const OLE_API *api, const WCHAR *aumid, CLSID *clsid)
{
    if (activator_from_registry(api, aumid, clsid))
        return TRUE;
    static const WCHAR *const roots[] = {L"%APPDATA%\\Microsoft\\Windows\\Start Menu\\Programs",
                                         L"%ProgramData%\\Microsoft\\Windows\\Start Menu\\Programs"};
    for (ULONG i = 0; i < RTL_NUMBER_OF(roots); i++) {
        WCHAR folder[MAX_PATH];
        DWORD n = ExpandEnvironmentStringsW(roots[i], folder, MAX_PATH);
        if (n && n <= MAX_PATH && activator_in_folder(api, folder, aumid, clsid, 3))
            return TRUE;
    }
    return FALSE;
}


BOOL CbActivateApplication(const WCHAR *aumid, const WCHAR *arguments, const WCHAR *key, const WCHAR *value)
{
    OLE_API api;
    CLSID clsid;
    if (!aumid || !arguments || !ole_api(&api) || FAILED(api.Initialize(nullptr, COINIT_MULTITHREADED)) ||
        !find_activator(&api, aumid, &clsid))
        return FALSE;
    INotificationActivationCallback *callback;
    if (FAILED(api.CreateInstance(&clsid, nullptr, CLSCTX_LOCAL_SERVER, &IID_INotificationActivationCallback,
                                  (void **)&callback)))
        return FALSE;
    NOTIFICATION_USER_INPUT_DATA data = {key, value};
    HRESULT hr = callback->lpVtbl->Activate(callback, aumid, arguments, key ? &data : nullptr, key ? 1 : 0);
    callback->lpVtbl->Release(callback);
    return SUCCEEDED(hr);
}
