/*
 * shell helpers of shlwapi that call through interfaces of the shell: restrictions, shell keys, folders, context
 * menus, drop targets, the browser and the zone checks.
 */
#include "shlwapi_dynamic.h"
#include <mime/win32/oleidl.h>

HRESULT WINAPI SHCoCreateInstanceAC(REFCLSID clsid, IUnknown *outer, DWORD context, REFIID riid, void **out);
HRESULT WINAPI IUnknown_QueryStatus(IUnknown *unknown, const GUID *group, ULONG count, OLECMD *commands,
                                    OLECMDTEXT *text);
HRESULT WINAPI IUnknown_Exec(IUnknown *unknown, const GUID *group, DWORD id, DWORD options, VARIANT *in, VARIANT *out);
HRESULT WINAPI SHStringFromGUIDW(const GUID *guid, LPWSTR out, int size);
LONG WINAPI SHGlobalCounterGetValue(int counter);
HRESULT WINAPI IUnknown_GetClassID(IUnknown *unknown, CLSID *clsid);

#define GLOBALCOUNTER_RESTRICTIONS 5

/* a method of a raw interface by its slot. */
#define SLOT(object, index) ((*(void ***)(object))[index])
#define RELEASE(object) ((ULONG(STDMETHODCALLTYPE *)(void *))SLOT(object, 2))(object)
#define CALL(type, object, index, ...) ((type)SLOT(object, index))(object __VA_OPT__(, ) __VA_ARGS__)

MIME_DEFINE_GUID(IID_ShlQueryInfo, 0x00021500, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_ShlContextMenu, 0x000214e4, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_ShlContextMenu2, 0x000214f4, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_ShlContextMenu3, 0xbcfce0a0, 0xec17, 0x11d0, 0x8d, 0x10, 0x00, 0xa0, 0xc9, 0x0f, 0x27, 0x19);
MIME_DEFINE_GUID(IID_ShlContextMenuSite, 0x0811aebe, 0x0b87, 0x4c54, 0x9e, 0x72, 0x54, 0x8c, 0xf6, 0x49, 0x01, 0x6b);
MIME_DEFINE_GUID(IID_ShlContextMenuVerbs, 0x649e2263, 0xdc09, 0x466f, 0x9d, 0x66, 0x3e, 0xb1, 0x33, 0xee, 0x8f, 0x81);
MIME_DEFINE_GUID(IID_ShlPersistPropertyBag, 0x37d84f60, 0x42cb, 0x11ce, 0x81, 0x35, 0x00, 0xaa, 0x00, 0x4b, 0xb8, 0x51);
MIME_DEFINE_GUID(IID_ShlRestrict, 0xd12f26b1, 0xd90a, 0x11d0, 0x83, 0x0d, 0x00, 0xaa, 0x00, 0x5b, 0x43, 0x83);
MIME_DEFINE_GUID(SID_ShlRestrict, 0xd12f26b2, 0xd90a, 0x11d0, 0x83, 0x0d, 0x00, 0xaa, 0x00, 0x5b, 0x43, 0x83);
MIME_DEFINE_GUID(SID_ShlTopLevelBrowser, 0x4c96be40, 0x915c, 0x11cf, 0x99, 0xd3, 0x00, 0xaa, 0x00, 0x4a, 0xe8, 0x37);
MIME_DEFINE_GUID(IID_ShlWebBrowserApp, 0x0002df05, 0x0000, 0x0000, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_ShlWebBrowser2, 0xd30c1661, 0xcdaf, 0x11d0, 0x8a, 0x3e, 0x00, 0xc0, 0x4f, 0xc9, 0xe2, 0x6e);
MIME_DEFINE_GUID(IID_ShlBrowserGlobalKeys, 0x6915c085, 0x510b, 0x44cd, 0x94, 0xaf, 0x28, 0xdf, 0xa5, 0x6c, 0xf9, 0x2b);
MIME_DEFINE_GUID(IID_ShlBrowserBag, 0xdfbc7e30, 0xf9e5, 0x455f, 0x88, 0xf8, 0xfa, 0x98, 0xc1, 0xe4, 0x94, 0xca);
MIME_DEFINE_GUID(IID_ShlSecurityManager, 0x79eac9ee, 0xbaf9, 0x11ce, 0x8c, 0x82, 0x00, 0xaa, 0x00, 0x4b, 0xa9, 0x0b);
MIME_DEFINE_GUID(CLSID_ShlSecurityManager, 0x7b8a2d94, 0x0ac9, 0x11d1, 0x89, 0x6c, 0x00, 0xc0, 0x4f, 0xb6, 0xbf, 0xc4);
MIME_DEFINE_GUID(CLSID_ShlZoneManager, 0x7b8a2d95, 0x0ac9, 0x11d1, 0x89, 0x6c, 0x00, 0xc0, 0x4f, 0xb6, 0xbf, 0xc4);
MIME_DEFINE_GUID(IID_ShlZoneManager, 0x79eac9ef, 0xbaf9, 0x11ce, 0x8c, 0x82, 0x00, 0xaa, 0x00, 0x4b, 0xa9, 0x0b);
MIME_DEFINE_GUID(IID_ShlObjectWithSite, 0xfc4801a3, 0x2ba9, 0x11cf, 0xa2, 0x29, 0x00, 0xaa, 0x00, 0x3d, 0x73, 0x52);

#define TOKEN_QUERY 0x0008
#define TOKEN_IMPERSONATE 0x0004

static HRESULT query_service(IUnknown *unknown, REFGUID service, REFIID riid, void **out)
{
    HRESULT(WINAPI * query)(IUnknown *, REFGUID, REFIID, void **) =
        (HRESULT(WINAPI *)(IUnknown *, REFGUID, REFIID, void **))ShlProc(L"shcore.dll", "IUnknown_QueryService");
    *out = nullptr;
    return query ? query(unknown, service, riid, out) : E_FAIL;
}

static void set_site(IUnknown *object, IUnknown *site)
{
    HRESULT(WINAPI * set)(IUnknown *, IUnknown *) =
        (HRESULT(WINAPI *)(IUnknown *, IUnknown *))ShlProc(L"shcore.dll", "IUnknown_SetSite");
    if (set)
        set(object, site);
}

/* ------------------------------------------------------------------ restrictions and shell keys */

/* the rows the callers hand over: the id, then the key and value names. */
typedef struct {
    DWORD id;
    LPCWSTR key;
    LPCWSTR value;
} RESTRICTION;

static SRWLOCK restrictions_lock = SRWLOCK_INIT;

DWORD WINAPI SHRestrictionLookup(int id, LPCWSTR policies, const RESTRICTION *table, DWORD *cache, DWORD count,
                                 LONG *generation);
DWORD WINAPI SHRestrictionLookup(int id, LPCWSTR policies, const RESTRICTION *table, DWORD *cache, DWORD count,
                                 LONG *generation)
{
    LONG now = SHGlobalCounterGetValue(GLOBALCOUNTER_RESTRICTIONS);
    if (*generation != now) {
        AcquireSRWLockExclusive(&restrictions_lock);
        now = SHGlobalCounterGetValue(GLOBALCOUNTER_RESTRICTIONS);
        if (*generation != now) {
            *generation = now;
            memset(cache, 0xff, count * sizeof(DWORD));
        }
        ReleaseSRWLockExclusive(&restrictions_lock);
    }
    for (DWORD i = 0; i < count; i++) {
        if (table[i].id != (DWORD)id)
            continue;
        if (cache[i] != 0xffffffff)
            return cache[i];
        WCHAR path[MAX_PATH];
        DWORD data = 0, size = sizeof(data);
        if (!policies)
            policies = L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies";
        PathCombineW(path, policies, table[i].key);
        if (SHGetValueW(HKEY_LOCAL_MACHINE, path, table[i].value, nullptr, &data, &size) != ERROR_SUCCESS) {
            size = sizeof(data);
            SHGetValueW(HKEY_CURRENT_USER, path, table[i].value, nullptr, &data, &size);
        }
        AcquireSRWLockExclusive(&restrictions_lock);
        if (*generation == SHGlobalCounterGetValue(GLOBALCOUNTER_RESTRICTIONS))
            cache[i] = data;
        ReleaseSRWLockExclusive(&restrictions_lock);
        return data;
    }
    return 0;
}

#define SHELLKEY_HKCU_EXPLORER 0x01
#define SHELLKEY_HKLM_EXPLORER 0x02
#define SHELLKEY_HKCU_SHELL 0x11
#define SHELLKEY_HKLM_SHELL 0x12
#define SHELLKEY_HKCU_FILEEXTS 0x6001
#define SHELLKEY_HKCULS_SHELL 0x1ffff
#define SHELLKEY_HKCULS_MUICACHE 0x5021

#ifndef HKEY_CURRENT_USER_LOCAL_SETTINGS
#define HKEY_CURRENT_USER_LOCAL_SETTINGS ((HKEY)(ULONG_PTR)((LONG)0x80000007))
#endif

/* the language of the cache is the one of the user, or the cache is thrown away. */
static void validate_mui_cache(HKEY key)
{
    HKEY cache = nullptr;
    if (RegOpenKeyExW(key, nullptr, 0, KEY_ALL_ACCESS, &cache) != ERROR_SUCCESS)
        return;
    WORD language = 0, current = GetUserDefaultUILanguage();
    DWORD size = sizeof(language);
    SHGetValueW(cache, nullptr, L"LangID", nullptr, &language, &size);
    if (language != current) {
        SHDeleteKeyW(cache, nullptr);
        SHSetValueW(cache, nullptr, L"LangID", REG_DWORD, &current, sizeof(current));
    }
    RegCloseKey(cache);
}

static struct {
    DWORD id;
    HKEY root;
    LPCWSTR path;
    void (*open)(HKEY);
    HKEY volatile cached;
} shell_keys[] = {
    {SHELLKEY_HKCU_EXPLORER, HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer"},
    {SHELLKEY_HKLM_EXPLORER, HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer"},
    {SHELLKEY_HKCU_SHELL, HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\Shell"},
    {SHELLKEY_HKLM_SHELL, HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\Shell"},
    {SHELLKEY_HKCU_FILEEXTS, HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts"},
    {SHELLKEY_HKCULS_SHELL, HKEY_CURRENT_USER_LOCAL_SETTINGS, L"Software\\Microsoft\\Windows\\Shell"},
    {SHELLKEY_HKCULS_MUICACHE, HKEY_CURRENT_USER_LOCAL_SETTINGS, L"Software\\Microsoft\\Windows\\Shell\\MuiCache",
     validate_mui_cache},
};

/* whether the local settings hive can be opened: 0 not known, 1 yes, 2 no. */
static volatile LONG local_settings_state;

static void set_status(LSTATUS status)
{
    SetLastError(status);
}

HKEY WINAPI SHGetShellKeyEx(DWORD key_id, LPCWSTR subkey, BOOL create, REGSAM access);
HKEY WINAPI SHGetShellKeyEx(DWORD key_id, LPCWSTR subkey, BOOL create, REGSAM access)
{
    if (key_id == 0x21)
        key_id = SHELLKEY_HKCULS_SHELL;
    int index = -1;
    for (int i = 0; i < 7; i++)
        if (shell_keys[i].id == key_id)
            index = i;
    if (index < 0)
        return nullptr;
    HKEY root = shell_keys[index].root;
    BOOL owned = TRUE;
    if (root == HKEY_CURRENT_USER_LOCAL_SETTINGS) {
        if (!local_settings_state) {
            HKEY probe = nullptr;
            LSTATUS status = RegOpenKeyExW(HKEY_CURRENT_USER_LOCAL_SETTINGS, nullptr, 0, KEY_READ, &probe);
            if (status == ERROR_SUCCESS) {
                local_settings_state = 1;
                RegCloseKey(probe);
            } else if (status == ERROR_INVALID_HANDLE) {
                local_settings_state = 2;
            }
        }
        if (local_settings_state == 2)
            root = HKEY_CURRENT_USER;
    }
    if (root == HKEY_CURRENT_USER) {
        HANDLE token = nullptr;
        HKEY user = HKEY_CURRENT_USER;
        if (OpenThreadToken(GetCurrentThread(), TOKEN_QUERY | TOKEN_IMPERSONATE, TRUE, &token)) {
            RegOpenCurrentUser(create ? 0x2001d : 0x20019, &user);
            CloseHandle(token);
        }
        root = user;
        if (root == HKEY_CURRENT_USER)
            owned = FALSE;
        if (!root) {
            SetLastError(ERROR_ACCESS_DENIED);
            return nullptr;
        }
    } else {
        owned = FALSE;
    }
    HKEY parent = shell_keys[index].cached, to_close = nullptr, result = nullptr;
    LSTATUS status;
    if (!parent || owned) {
        HKEY opened = nullptr;
        /* the key is kept for the next caller, which may ask for other rights than this one. */
        REGSAM held = access | KEY_READ;
        if (create && shell_keys[index].path)
            status = RegCreateKeyExW(root, shell_keys[index].path, 0, nullptr, 0, held, nullptr, &opened, nullptr);
        else
            status = RegOpenKeyExW(root, shell_keys[index].path, 0, held, &opened);
        if (!opened) {
            set_status(status);
            if (owned)
                RegCloseKey(root);
            return nullptr;
        }
        parent = to_close = opened;
        if (shell_keys[index].open)
            shell_keys[index].open(opened);
        if (!owned && InterlockedCompareExchangePointer((void *volatile *)&shell_keys[index].cached, opened,
                                                         nullptr) == nullptr)
            to_close = nullptr;
    }
    if (create && subkey)
        status = RegCreateKeyExW(parent, subkey, 0, nullptr, 0, access, nullptr, &result, nullptr);
    else
        status = RegOpenKeyExW(parent, subkey, 0, access, &result);
    if (!result)
        set_status(status);
    if (to_close)
        RegCloseKey(to_close);
    if (owned)
        RegCloseKey(root);
    return result;
}

HKEY WINAPI SHGetShellKey(DWORD key_id, LPCWSTR subkey, BOOL create);
HKEY WINAPI SHGetShellKey(DWORD key_id, LPCWSTR subkey, BOOL create)
{
    return SHGetShellKeyEx(key_id, subkey, create, MAXIMUM_ALLOWED);
}

static HRESULT result_from_status(LSTATUS status)
{
    return status > 0 ? (HRESULT)((status & 0xffff) | 0x80070000) : (HRESULT)status;
}

static HRESULT result_from_last_error(void)
{
    HRESULT hr = result_from_status((LSTATUS)GetLastError());
    return hr >= 0 ? E_FAIL : hr;
}

HRESULT WINAPI SKGetValueW(DWORD key_id, LPCWSTR subkey, LPCWSTR value, DWORD *type, void *data, DWORD *size);
HRESULT WINAPI SKGetValueW(DWORD key_id, LPCWSTR subkey, LPCWSTR value, DWORD *type, void *data, DWORD *size)
{
    HKEY key = SHGetShellKeyEx(key_id, subkey, FALSE, KEY_READ);
    if (!key)
        return result_from_last_error();
    LSTATUS (WINAPI * query)(HKEY, LPCWSTR, DWORD *, DWORD *, void *, DWORD *) =
        (LSTATUS(WINAPI *)(HKEY, LPCWSTR, DWORD *, DWORD *, void *, DWORD *))ShlProc(L"shcore.dll",
                                                                                    "SHQueryValueExW");
    LSTATUS status = query ? query(key, value, nullptr, type, data, size) : ERROR_PROC_NOT_FOUND;
    RegCloseKey(key);
    return result_from_status(status);
}

HRESULT WINAPI SKSetValueW(DWORD key_id, LPCWSTR subkey, LPCWSTR value, DWORD type, const void *data, DWORD size);
HRESULT WINAPI SKSetValueW(DWORD key_id, LPCWSTR subkey, LPCWSTR value, DWORD type, const void *data, DWORD size)
{
    HKEY key = SHGetShellKeyEx(key_id, subkey, TRUE, KEY_SET_VALUE);
    if (!key)
        return result_from_last_error();
    LSTATUS status = RegSetValueExW(key, value, 0, type, data, size);
    RegCloseKey(key);
    return result_from_status(status);
}

HRESULT WINAPI SKDeleteValueW(DWORD key_id, LPCWSTR subkey, LPCWSTR value);
HRESULT WINAPI SKDeleteValueW(DWORD key_id, LPCWSTR subkey, LPCWSTR value)
{
    HKEY key = SHGetShellKeyEx(key_id, subkey, TRUE, KEY_SET_VALUE);
    if (!key)
        return result_from_last_error();
    LSTATUS status = RegDeleteValueW(key, value);
    RegCloseKey(key);
    return result_from_status(status);
}

HRESULT WINAPI SKAllocValueW(DWORD key_id, LPCWSTR subkey, LPCWSTR value, DWORD *type, void **data, DWORD *size);
HRESULT WINAPI SKAllocValueW(DWORD key_id, LPCWSTR subkey, LPCWSTR value, DWORD *type, void **data, DWORD *size)
{
    HKEY key = SHGetShellKeyEx(key_id, subkey, FALSE, KEY_READ);
    if (!key)
        return result_from_last_error();
    LSTATUS (WINAPI * query)(HKEY, LPCWSTR, DWORD *, DWORD *, void *, DWORD *) =
        (LSTATUS(WINAPI *)(HKEY, LPCWSTR, DWORD *, DWORD *, void *, DWORD *))ShlProc(L"shcore.dll",
                                                                                    "SHQueryValueExW");
    DWORD bytes = 0;
    LSTATUS status = query ? query(key, value, nullptr, nullptr, nullptr, &bytes) : ERROR_PROC_NOT_FOUND;
    if (status == ERROR_SUCCESS) {
        *data = LocalAlloc(LPTR, bytes + 2);
        if (*data) {
            status = query(key, value, nullptr, type, *data, &bytes);
            if (status != ERROR_SUCCESS) {
                LocalFree(*data);
                *data = nullptr;
            } else if (size) {
                *size = bytes;
            }
        } else {
            status = ERROR_NOT_ENOUGH_MEMORY;
        }
    }
    RegCloseKey(key);
    return result_from_status(status);
}

/* ------------------------------------------------------------------ folders and drop targets */

/* the arguments of a name request that are tried again for a folder that does not know the flags. */
static const struct {
    DWORD keep, add, applies;
} name_retries[] = {
    {0xffffefff, 0, 0x80000000}, {0xffffbfff, 0, 0x80000000}, {0xffffffff, 0x8000, 0x80000000},
    {0xffff7fff, 0, 1},          {0xfffffffe, 0, 0x80000000},
};

static BOOL is_bogus(HRESULT hr)
{
    return hr == E_FAIL || hr == E_INVALIDARG || hr == E_NOTIMPL;
}

HRESULT WINAPI IShellFolder_GetDisplayNameOf(void *folder, const void *pidl, DWORD flags, void *name, DWORD retry);
HRESULT WINAPI IShellFolder_GetDisplayNameOf(void *folder, const void *pidl, DWORD flags, void *name, DWORD retry)
{
    typedef HRESULT(STDMETHODCALLTYPE * GET)(void *, const void *, DWORD, void *);
    HRESULT hr = CALL(GET, folder, 11, pidl, flags, name);
    if (!is_bogus(hr))
        return hr;
    DWORD mask = ((flags & 0x8000) ? retry : (retry | 1)) | 0x80000000;
    for (int i = 0; i < 5; i++) {
        if (!(mask & name_retries[i].applies))
            continue;
        DWORD changed = (flags & name_retries[i].keep) | name_retries[i].add;
        if (changed == flags)
            continue;
        flags = changed;
        hr = CALL(GET, folder, 11, pidl, flags, name);
        if (!is_bogus(hr))
            break;
    }
    return hr;
}

HRESULT WINAPI IShellFolder_ParseDisplayName(void *folder, HWND window, void *context, LPWSTR name, ULONG *eaten,
                                             void **pidl, ULONG *attributes);
HRESULT WINAPI IShellFolder_ParseDisplayName(void *folder, HWND window, void *context, LPWSTR name, ULONG *eaten,
                                             void **pidl, ULONG *attributes)
{
    typedef HRESULT(STDMETHODCALLTYPE * PARSE)(void *, HWND, void *, LPWSTR, ULONG *, void **, ULONG *);
    if (pidl)
        *pidl = nullptr;
    return CALL(PARSE, folder, 3, window, context, name, eaten, pidl, attributes);
}

BOOL WINAPI SHIsExpandableFolder(void *folder, const void *pidl);
BOOL WINAPI SHIsExpandableFolder(void *folder, const void *pidl)
{
    typedef HRESULT(STDMETHODCALLTYPE * GETUI)(void *, HWND, UINT, const void **, REFIID, UINT *, void **);
    void *info = nullptr;
    BOOL expandable = TRUE;
    if (SUCCEEDED(CALL(GETUI, folder, 10, nullptr, 1, &pidl, &IID_ShlQueryInfo, nullptr, &info))) {
        DWORD flags = 0;
        if (SUCCEEDED(CALL(HRESULT(STDMETHODCALLTYPE *)(void *, DWORD *), info, 4, &flags)))
            expandable = !(flags & 2);
        RELEASE(info);
    }
    return expandable;
}

HRESULT WINAPI SHSimulateDrop(void *target, void *data, DWORD keys, POINTL *point, DWORD *effect);
HRESULT WINAPI SHSimulateDrop(void *target, void *data, DWORD keys, POINTL *point, DWORD *effect)
{
    typedef HRESULT(STDMETHODCALLTYPE * DRAG)(void *, void *, DWORD, POINTL, DWORD *);
    POINTL origin = {};
    DWORD fallback = 0;
    if (!point)
        point = &origin;
    if (!effect) {
        fallback = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
        effect = &fallback;
    }
    DWORD wanted = *effect;
    CALL(DRAG, target, 3, data, keys, *point, effect);
    if (*effect) {
        *effect = wanted;
        return CALL(DRAG, target, 6, data, keys, *point, effect);
    }
    CALL(HRESULT(STDMETHODCALLTYPE *)(void *), target, 5);
    return S_FALSE;
}

HRESULT WINAPI SHLoadFromPropertyBag(IUnknown *object, IPropertyBag *bag);
HRESULT WINAPI SHLoadFromPropertyBag(IUnknown *object, IPropertyBag *bag)
{
    void *persist;
    HRESULT hr = object->lpVtbl->QueryInterface(object, &IID_ShlPersistPropertyBag, &persist);
    if (SUCCEEDED(hr)) {
        hr = CALL(HRESULT(STDMETHODCALLTYPE *)(void *, IPropertyBag *, void *), persist, 5, bag, nullptr);
        RELEASE(persist);
    }
    return hr;
}

HRESULT WINAPI IUnknown_HandleIRestrict(IUnknown *site, const GUID *group, DWORD id, void *in, DWORD *result);
HRESULT WINAPI IUnknown_HandleIRestrict(IUnknown *site, const GUID *group, DWORD id, void *in, DWORD *result)
{
    void *restrict_object;
    *result = 3;
    HRESULT hr = query_service(site, &SID_ShlRestrict, &IID_ShlRestrict, &restrict_object);
    if (SUCCEEDED(hr)) {
        hr = CALL(HRESULT(STDMETHODCALLTYPE *)(void *, const GUID *, DWORD, void *, DWORD *), restrict_object, 3,
                  group, id, in, result);
        RELEASE(restrict_object);
    }
    return hr;
}

/* ------------------------------------------------------------------ command target forwarding */

#define OLECMDERR_E_NOTSUPPORTED ((HRESULT)0x80040100)
#define OLECMDERR_E_UNKNOWNGROUP ((HRESULT)0x80040104)

/* tells what a forward may carry: a bit set for the commands that are, and the errors otherwise. */
HRESULT WINAPI IsQSForward(const GUID *group, int count, const OLECMD *commands);
HRESULT WINAPI IsQSForward(const GUID *group, int count, const OLECMD *commands)
{
    DWORD forward = 0;
    if (!group) {
        if (count <= 0)
            return OLECMDERR_E_NOTSUPPORTED;
        for (int i = 0; i < count; i++) {
            ULONG id = commands[i].cmdID;
            if (id == 11 || id == 12 || id == 13 || id == 17)
                forward |= 1;
            else if (id == 22 || id == 23 || id == 36)
                forward |= 2;
            else
                forward |= 4;
        }
    } else {
        static const GUID known = {0x00021450, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
        if (memcmp(group, &known, sizeof(GUID)))
            return OLECMDERR_E_UNKNOWNGROUP;
        if (count <= 0)
            return OLECMDERR_E_NOTSUPPORTED;
        for (int i = 0; i < count; i++) {
            ULONG id = commands[i].cmdID;
            if (id == 17 || id == 42 || id == 43 || id == 44)
                forward |= 1;
        }
    }
    if (!forward || (forward & 4))
        return OLECMDERR_E_NOTSUPPORTED;
    return (HRESULT)forward;
}

HRESULT WINAPI MayQSForward(IUnknown *target, int ok, const GUID *group, ULONG count, OLECMD *commands,
                            OLECMDTEXT *text);
HRESULT WINAPI MayQSForward(IUnknown *target, int ok, const GUID *group, ULONG count, OLECMD *commands,
                            OLECMDTEXT *text)
{
    HRESULT hr = IsQSForward(group, (int)count, commands);
    if (FAILED(hr))
        return hr == OLECMDERR_E_UNKNOWNGROUP ? OLECMDERR_E_UNKNOWNGROUP : OLECMDERR_E_NOTSUPPORTED;
    if ((WORD)hr && ok > 0)
        return IUnknown_QueryStatus(target, group, count, commands, text);
    return OLECMDERR_E_NOTSUPPORTED;
}

HRESULT WINAPI MayExecForward(IUnknown *target, int ok, const GUID *group, DWORD id, DWORD options, VARIANT *in,
                              VARIANT *out);
HRESULT WINAPI MayExecForward(IUnknown *target, int ok, const GUID *group, DWORD id, DWORD options, VARIANT *in,
                              VARIANT *out)
{
    OLECMD command = {id, 0};
    HRESULT hr = IsQSForward(group, 1, &command);
    if (FAILED(hr))
        return hr == OLECMDERR_E_UNKNOWNGROUP ? OLECMDERR_E_UNKNOWNGROUP : OLECMDERR_E_NOTSUPPORTED;
    if ((WORD)hr && ok > 0)
        return IUnknown_Exec(target, group, id, options, in, out);
    return OLECMDERR_E_NOTSUPPORTED;
}

/* ------------------------------------------------------------------ context menus */

#define CMIC_MASK_UNICODE 0x4000
#define CMIC_MASK_PTINVOKE 0x20000000

typedef struct {
    DWORD size, mask;
    HWND window;
    const char *verb, *parameters, *directory;
    int show;
    DWORD hotkey;
    HANDLE icon;
    const char *title;
    LPCWSTR verb_w, parameters_w, directory_w, title_w;
    POINT point;
} INVOKE_EX;

static HRESULT invoke_on_menu(HWND window, IUnknown *site, IUnknown *menu_object, DWORD mask, DWORD query_flags,
                              const char *verb, LPCWSTR directory, const POINT *point)
{
    typedef HRESULT(STDMETHODCALLTYPE * QUERY)(void *, HMENU, UINT, UINT, UINT, UINT);
    typedef HRESULT(STDMETHODCALLTYPE * INVOKE)(void *, INVOKE_EX *);
    HMENU popup = CreatePopupMenu();
    if (!popup)
        return E_OUTOFMEMORY;
    if (site)
        set_site(menu_object, site);
    LPWSTR wide_verb = nullptr;
    if (verb) {
        HRESULT(WINAPI * dup)(LPCSTR, LPWSTR *) =
            (HRESULT(WINAPI *)(LPCSTR, LPWSTR *))ShlProc(L"shcore.dll", "SHStrDupA");
        if (dup && SUCCEEDED(dup(verb, &wide_verb))) {
            void *verbs;
            if (SUCCEEDED(menu_object->lpVtbl->QueryInterface(menu_object, &IID_ShlContextMenuVerbs, &verbs))) {
                CALL(HRESULT(STDMETHODCALLTYPE *)(void *, LPWSTR *, UINT), verbs, 3, &wide_verb, 1);
                RELEASE(verbs);
            }
        }
    }
    HRESULT hr = CALL(QUERY, menu_object, 3, popup, 0, 1, 0x7fff, query_flags | (verb ? 0x800 : 1));
    if (SUCCEEDED(hr)) {
        ULONG_PTR command = (ULONG_PTR)verb;
        BOOL go = TRUE;
        if (!verb) {
            UINT item = GetMenuDefaultItem(popup, 0, 0);
            if (item == (UINT)-1) {
                hr = E_FAIL;
                go = FALSE;
            } else {
                command = (WORD)(item - 1);
            }
        }
        if (go) {
            char ansi[MAX_PATH];
            INVOKE_EX invoke = {};
            BOOL unicode = wide_verb || directory;
            invoke.size = sizeof(invoke);
            invoke.mask = mask | (unicode ? CMIC_MASK_UNICODE : 0);
            invoke.window = window;
            invoke.verb = (const char *)command;
            invoke.show = SW_SHOWNORMAL;
            invoke.verb_w = wide_verb;
            invoke.directory_w = directory;
            if (point) {
                invoke.point = *point;
                invoke.mask = mask | (unicode ? CMIC_MASK_UNICODE : 0) | CMIC_MASK_PTINVOKE;
            }
            if (directory && WideCharToMultiByte(CP_ACP, 0, directory, -1, ansi, sizeof(ansi), nullptr, nullptr))
                invoke.directory = ansi;
            hr = CALL(INVOKE, menu_object, 4, &invoke);
        }
    }
    if (site)
        set_site(menu_object, nullptr);
    void (*free_task)(void *) = (void (*)(void *))ShlProc(L"ole32.dll", "CoTaskMemFree");
    if (free_task)
        free_task(wide_verb);
    DestroyMenu(popup);
    return hr;
}

HRESULT WINAPI SHInvokeCommandWithFlagsAndSite(HWND window, IUnknown *site, void *folder, const void *pidl,
                                               DWORD flags, const char *verb, const POINT *point);
HRESULT WINAPI SHInvokeCommandWithFlagsAndSite(HWND window, IUnknown *site, void *folder, const void *pidl,
                                               DWORD flags, const char *verb, const POINT *point)
{
    typedef HRESULT(STDMETHODCALLTYPE * GETUI)(void *, HWND, UINT, const void **, REFIID, UINT *, void **);
    if (!folder)
        return E_FAIL;
    IUnknown *menu = nullptr;
    HRESULT hr = CALL(GETUI, folder, 10, window, 1, &pidl, &IID_ShlContextMenu, nullptr, (void **)&menu);
    if (SUCCEEDED(hr)) {
        hr = invoke_on_menu(window, site, menu, flags | 0x4000000, 0, verb, nullptr, point);
        menu->lpVtbl->Release(menu);
    }
    return hr;
}

HRESULT WINAPI SHInvokeDefaultCommand(HWND window, void *folder, const void *pidl);
HRESULT WINAPI SHInvokeDefaultCommand(HWND window, void *folder, const void *pidl)
{
    return SHInvokeCommandWithFlagsAndSite(window, nullptr, folder, pidl, 0, nullptr, nullptr);
}

HRESULT WINAPI SHInvokeCommand(HWND window, void *folder, const void *pidl, const char *verb);
HRESULT WINAPI SHInvokeCommand(HWND window, void *folder, const void *pidl, const char *verb)
{
    return SHInvokeCommandWithFlagsAndSite(window, nullptr, folder, pidl, 0, verb, nullptr);
}

HRESULT WINAPI SHInvokeCommandOnContextMenu(HWND window, IUnknown *site, IUnknown *menu, DWORD mask, const char *verb);
HRESULT WINAPI SHInvokeCommandOnContextMenu(HWND window, IUnknown *site, IUnknown *menu, DWORD mask, const char *verb)
{
    return invoke_on_menu(window, site, menu, mask, 0x100, verb, nullptr, nullptr);
}

HRESULT WINAPI SHInvokeCommandsOnContextMenu(HWND window, IUnknown *site, IUnknown *menu, DWORD mask,
                                             const char **verbs, int count);
HRESULT WINAPI SHInvokeCommandsOnContextMenu(HWND window, IUnknown *site, IUnknown *menu, DWORD mask,
                                             const char **verbs, int count)
{
    return invoke_on_menu(window, site, menu, mask, 0, count ? *verbs : nullptr, nullptr, nullptr);
}

HRESULT WINAPI SHInvokeCommandOnContextMenuEx(HWND window, IUnknown *site, IUnknown *menu, DWORD mask,
                                              DWORD query_flags, const char *verb, LPCWSTR directory);
HRESULT WINAPI SHInvokeCommandOnContextMenuEx(HWND window, IUnknown *site, IUnknown *menu, DWORD mask,
                                              DWORD query_flags, const char *verb, LPCWSTR directory)
{
    return invoke_on_menu(window, site, menu, mask, query_flags, verb, directory, nullptr);
}

HRESULT WINAPI SHInvokeCommandsOnContextMenuEx(HWND window, IUnknown *site, IUnknown *menu, DWORD mask,
                                               DWORD query_flags, const char **verbs, int count, LPCWSTR directory);
HRESULT WINAPI SHInvokeCommandsOnContextMenuEx(HWND window, IUnknown *site, IUnknown *menu, DWORD mask,
                                               DWORD query_flags, const char **verbs, int count, LPCWSTR directory)
{
    return invoke_on_menu(window, site, menu, mask, query_flags, count ? *verbs : nullptr, directory, nullptr);
}

/* the message goes to the handler of the third interface, or the second when the caller wants a result. */
HRESULT WINAPI SHForwardContextMenuMsg(IUnknown *menu, UINT message, WPARAM wparam, LPARAM lparam, LRESULT *result,
                                       BOOL want_result);
HRESULT WINAPI SHForwardContextMenuMsg(IUnknown *menu, UINT message, WPARAM wparam, LPARAM lparam, LRESULT *result,
                                       BOOL want_result)
{
    if (!menu)
        return E_FAIL;
    void *handler;
    if (SUCCEEDED(menu->lpVtbl->QueryInterface(menu, &IID_ShlContextMenu3, &handler))) {
        HRESULT hr = CALL(HRESULT(STDMETHODCALLTYPE *)(void *, UINT, WPARAM, LPARAM, LRESULT *), handler, 7, message,
                          wparam, lparam, result);
        RELEASE(handler);
        return hr;
    }
    if (want_result && SUCCEEDED(menu->lpVtbl->QueryInterface(menu, &IID_ShlContextMenu2, &handler))) {
        HRESULT hr = CALL(HRESULT(STDMETHODCALLTYPE *)(void *, UINT, WPARAM, LPARAM), handler, 6, message, wparam,
                          lparam);
        RELEASE(handler);
        if (result)
            *result = 0;
        if (SUCCEEDED(hr))
            return S_FALSE;
        return hr;
    }
    return E_FAIL;
}

HRESULT WINAPI IUnknown_DoContextMenuPopup(IUnknown *site, IUnknown *menu, UINT flags, POINT point);
HRESULT WINAPI IUnknown_DoContextMenuPopup(IUnknown *site, IUnknown *menu, UINT flags, POINT point)
{
    void *context_site;
    HRESULT hr = query_service(site, &IID_ShlContextMenuSite, &IID_ShlContextMenuSite, &context_site);
    if (SUCCEEDED(hr)) {
        hr = CALL(HRESULT(STDMETHODCALLTYPE *)(void *, IUnknown *, UINT, POINT), context_site, 3, menu, flags, point);
        RELEASE(context_site);
    }
    return hr;
}

/* ------------------------------------------------------------------ the browser */

static HRESULT query_browser_app(IUnknown *site, REFIID riid, void **out)
{
    void *services;
    *out = nullptr;
    HRESULT hr = query_service(site, &SID_ShlTopLevelBrowser, &IID_IServiceProvider, &services);
    if (SUCCEEDED(hr)) {
        hr = CALL(HRESULT(STDMETHODCALLTYPE *)(void *, REFGUID, REFIID, void **), services, 3, &IID_ShlWebBrowserApp,
                  riid, out);
        RELEASE(services);
    }
    return hr;
}

HRESULT WINAPI IUnknown_QueryServiceForWebBrowserApp(IUnknown *site, REFIID riid, void **out);
HRESULT WINAPI IUnknown_QueryServiceForWebBrowserApp(IUnknown *site, REFIID riid, void **out)
{
    return query_browser_app(site, riid, out);
}

HRESULT WINAPI IUnknown_ShowBrowserBar(IUnknown *site, REFCLSID clsid, BOOL show);
HRESULT WINAPI IUnknown_ShowBrowserBar(IUnknown *site, REFCLSID clsid, BOOL show)
{
    void *browser;
    HRESULT hr = query_browser_app(site, &IID_ShlWebBrowser2, &browser);
    if (FAILED(hr))
        return hr;
    /* a counted string on the stack stands in for the bstr. */
    struct {
        DWORD length;
        WCHAR text[43];
    } guid = {76, {}};
    SHStringFromGUIDW(clsid, guid.text, 39);
    VARIANT id = {}, state = {}, size = {};
    V_VT(&id) = VT_BSTR;
    V_BSTR(&id) = guid.text;
    V_VT(&state) = VT_BOOL;
    V_BOOL(&state) = show ? VARIANT_TRUE : VARIANT_FALSE;
    hr = CALL(HRESULT(STDMETHODCALLTYPE *)(void *, VARIANT *, VARIANT *, VARIANT *), browser, 55, &id, &state, &size);
    RELEASE(browser);
    return hr;
}

HRESULT WINAPI IUnknown_TranslateAcceleratorGlobal(IUnknown *site, MSG *message);
HRESULT WINAPI IUnknown_TranslateAcceleratorGlobal(IUnknown *site, MSG *message)
{
    if (!site)
        return E_FAIL;
    void *keys;
    HRESULT hr = site->lpVtbl->QueryInterface(site, &IID_ShlBrowserGlobalKeys, &keys);
    if (SUCCEEDED(hr)) {
        hr = CALL(HRESULT(STDMETHODCALLTYPE *)(void *, MSG *), keys, 6, message);
        RELEASE(keys);
    }
    return hr;
}

HRESULT WINAPI IWebBrowser_Navigate2(IUnknown *browser_object, LPCOLESTR url, VARIANT *flags, VARIANT *target,
                                     VARIANT *post, VARIANT *headers);
HRESULT WINAPI IWebBrowser_Navigate2(IUnknown *browser_object, LPCOLESTR url, VARIANT *flags, VARIANT *target,
                                     VARIANT *post, VARIANT *headers)
{
    void *browser;
    HRESULT hr = browser_object->lpVtbl->QueryInterface(browser_object, &IID_ShlWebBrowser2, &browser);
    if (FAILED(hr))
        return hr;
    BSTR text = SysAllocString(url);
    if (!text) {
        RELEASE(browser);
        return E_OUTOFMEMORY;
    }
    VARIANT address = {};
    V_VT(&address) = VT_BSTR;
    V_BSTR(&address) = text;
    hr = CALL(HRESULT(STDMETHODCALLTYPE *)(void *, VARIANT *, VARIANT *, VARIANT *, VARIANT *, VARIANT *), browser,
              52, &address, flags, target, post, headers);
    SysFreeString(text);
    RELEASE(browser);
    return hr;
}

HRESULT WINAPI IUnknown_QueryServicePropertyBag(IUnknown *site, DWORD flags, REFIID riid, void **out);
HRESULT WINAPI IUnknown_QueryServicePropertyBag(IUnknown *site, DWORD flags, REFIID riid, void **out)
{
    void *browser;
    HRESULT hr = query_service(site, &SID_ShlTopLevelBrowser, &IID_ShlBrowserBag, &browser);
    if (SUCCEEDED(hr)) {
        hr = CALL(HRESULT(STDMETHODCALLTYPE *)(void *, DWORD, REFIID, void **), browser, 19, flags, riid, out);
        RELEASE(browser);
    }
    return hr;
}

/* ------------------------------------------------------------------ junctions and telemetry */

static BOOL is_class(IUnknown *object, const CLSID *clsid)
{
    CLSID found = {};
    return SUCCEEDED(IUnknown_GetClassID(object, &found)) && !memcmp(&found, clsid, sizeof(CLSID));
}

/* the bind context names the handler to skip, by one object or by an array of them. */
BOOL WINAPI SHSkipJunction(IBindCtx *context, const CLSID *clsid)
{
    static const GUID IID_ShlObjectArray = {0x5632b1a4, 0xe38a, 0x400a, {0x92, 0x8a, 0xd4, 0xcd, 0x63, 0x23, 0x02, 0x95}};
    BOOL skip = FALSE;
    if (!context)
        return FALSE;
    IUnknown *one = nullptr;
    if (SUCCEEDED(context->lpVtbl->GetObjectParam(context, L"Skip Binding CLSID", &one))) {
        skip = is_class(one, clsid);
        one->lpVtbl->Release(one);
    }
    if (skip)
        return TRUE;
    IUnknown *list = nullptr;
    void *array = nullptr;
    if (SUCCEEDED(context->lpVtbl->GetObjectParam(context, L"SkipBindingCLSIDArray", &list))) {
        HRESULT hr = list->lpVtbl->QueryInterface(list, &IID_ShlObjectArray, &array);
        list->lpVtbl->Release(list);
        if (SUCCEEDED(hr)) {
            DWORD count = 0;
            if (SUCCEEDED(CALL(HRESULT(STDMETHODCALLTYPE *)(void *, DWORD *), array, 3, &count))) {
                for (DWORD i = 0; i < count && !skip; i++) {
                    IUnknown *item = nullptr;
                    if (SUCCEEDED(CALL(HRESULT(STDMETHODCALLTYPE *)(void *, DWORD, REFIID, void **), array, 4, i,
                                       &IID_IUnknown, (void **)&item))) {
                        skip = is_class(item, clsid);
                        item->lpVtbl->Release(item);
                    }
                }
            }
            RELEASE(array);
        }
    }
    return skip;
}

/* the programs that start the shell tell the usage statistics, which are not kept here. */
BOOL WINAPI SHRegisterValidateTemplate(LPCWSTR path, DWORD flags);
BOOL WINAPI SHRegisterValidateTemplate(LPCWSTR path, DWORD flags)
{
    return TRUE;
}

/* ------------------------------------------------------------------ the about text */

/* the strength of the strongest cipher the secure channel provider has, found once. */
static LONG cipher_strength = -1;

static DWORD get_cipher_strength(void)
{
    typedef struct {
        DWORD minimum, maximum;
    } CIPHER_STRENGTHS;
    typedef struct {
        void *table[8];
    } SECURITY_TABLE;
    if (cipher_strength != -1)
        return cipher_strength;
    cipher_strength = 0;
    HMODULE security = LoadLibraryW(L"security.dll");
    if (!security)
        return 0;
    SECURITY_TABLE *(WINAPI * initialize)(void) = (SECURITY_TABLE * (WINAPI *)(void)) GetProcAddress(security, "InitSecurityInterfaceA");
    SECURITY_TABLE *table = initialize ? initialize() : nullptr;
    /* the outbound credential of schannel tells the strongest cipher it has. */
    if (table && table->table[3] && table->table[2]) {
        LONG(WINAPI * acquire)(char *, char *, ULONG, void *, void *, void *, void *, ULONG_PTR *, LARGE_INTEGER *) =
            table->table[3];
        LONG(WINAPI * query)(ULONG_PTR *, ULONG, void *) = table->table[2];
        LONG(WINAPI * free_handle)(ULONG_PTR *) = table->table[4];
        CIPHER_STRENGTHS strengths = {};
        ULONG_PTR handle[2] = {};
        LARGE_INTEGER expiry;
        char provider[] = "Microsoft Unified Security Protocol Provider";
        if (acquire(nullptr, provider, 2, nullptr, nullptr, nullptr, nullptr, handle, &expiry) == 0) {
            if (query(handle, 87, &strengths) == 0)
                cipher_strength = strengths.maximum;
            if (free_handle)
                free_handle(handle);
        }
    }
    FreeLibrary(security);
    return cipher_strength;
}

static void read_machine_string(LPCWSTR key, LPCWSTR value, WCHAR *out, DWORD chars)
{
    DWORD size = chars * sizeof(WCHAR);
    SHRegGetValueW(HKEY_LOCAL_MACHINE, key, value, SRRF_RT_REG_SZ | SRRF_NOEXPAND, nullptr, out, &size);
}

static BOOL about_info(WCHAR *out, DWORD size)
{
    static const WCHAR ie[] = L"SOFTWARE\\Microsoft\\Internet Explorer";
    static const WCHAR nt[] = L"Software\\Microsoft\\Windows NT\\Current Version";
    WCHAR *text = LocalAlloc(LPTR, 0x1954);
    if (!size || !text)
        return FALSE;
    WCHAR *version = text, *build = text + 64, *customized = text + 128, *owner = text + 131,
          *organization = text + 387, *product = text + 646, *update = text + 902, *help = text + 2986;
    *out = 0;
    if (GetModuleHandleW(L"EXPLORER.EXE") || GetModuleHandleW(L"IEXPLORE.EXE")) {
        read_machine_string(ie, L"Version", version, 64);
        read_machine_string(nt, L"BuildLab", build, 64);
        int length = lstrlenW(build);
        if (length <= 12)
            *build = 0;
        else
            memmove(build, build + 4, (length - 3) * sizeof(WCHAR));
    } else {
        lstrcpynW(version, L"Side-by-Side Mode", 64);
    }
    read_machine_string(ie, L"CustomizedVersion", customized, 3);
    read_machine_string(nt, L"RegisteredOwner", owner, 256);
    read_machine_string(nt, L"RegisteredOrganization", organization, 256);
    read_machine_string(L"SOFTWARE\\Microsoft\\Internet Explorer\\Registration", L"ProductId", product, 256);
    read_machine_string(L"Software\\Policies\\Microsoft\\Windows\\CurrentVersion", L"IEAKUpdateUrl", update, 2084);
    read_machine_string(L"SOFTWARE\\Microsoft\\Internet Explorer\\Registration", L"IEAKHelpString", help, 256);
    int written = wnsprintfW(out, (int)size, L"%s%s%s~%s~%s~%d~%s~%s~%s", version, build, customized, owner,
                             organization, (int)get_cipher_strength(), product, update, help);
    LocalFree(text);
    return written >= 0;
}

BOOL WINAPI SHAboutInfoA(LPSTR out, UINT size);
BOOL WINAPI SHAboutInfoA(LPSTR out, UINT size)
{
    BOOL ok = FALSE;
    if (!size)
        return FALSE;
    *out = 0;
    WCHAR *wide = LocalAlloc(LPTR, size * sizeof(WCHAR));
    if (wide) {
        if (about_info(wide, size))
            ok = WideCharToMultiByte(CP_ACP, 0, wide, -1, out, (int)size, nullptr, nullptr) != 0;
        LocalFree(wide);
    }
    return ok;
}

/* ------------------------------------------------------------------ keys and streams of the shell core */

HRESULT WINAPI SHRegGetCLSIDKey(const GUID *clsid, LPCWSTR subkey, BOOL user_specific, BOOL create, REGSAM access,
                                HKEY *key);
HRESULT WINAPI SHRegGetCLSIDKey(const GUID *clsid, LPCWSTR subkey, BOOL user_specific, BOOL create, REGSAM access,
                                HKEY *key)
{
    WCHAR text[40], path[300];
    SHStringFromGUIDW(clsid, text, 39);
    LPCWSTR prefix = user_specific ? L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\" : L"";
    int length = subkey ? wnsprintfW(path, ARRAYSIZE(path), L"%sCLSID\\%s\\%s", prefix, text, subkey)
                        : wnsprintfW(path, ARRAYSIZE(path), L"%sCLSID\\%s", prefix, text);
    if (length < 0)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    HKEY root = user_specific ? HKEY_CURRENT_USER : HKEY_CLASSES_ROOT;
    LSTATUS status = create ? RegCreateKeyExW(root, path, 0, nullptr, 0, access, nullptr, key, nullptr)
                            : RegOpenKeyExW(root, path, 0, access, key);
    return result_from_status(status);
}

/*
 * the source of queries over a registry key opens the key as the real one does, and the object that answers the
 * queries is not made.
 */
HRESULT WINAPI QuerySourceCreateFromKeyEx(HKEY root, LPCWSTR subkey, BOOL create, REGSAM access, REFIID riid,
                                          void **out);
HRESULT WINAPI QuerySourceCreateFromKeyEx(HKEY root, LPCWSTR subkey, BOOL create, REGSAM access, REFIID riid,
                                          void **out)
{
    HKEY key = nullptr;
    *out = nullptr;
    access |= KEY_READ;
    LSTATUS status = create ? RegCreateKeyExW(root, subkey, 0, nullptr, 0, access, nullptr, &key, nullptr)
                            : RegOpenKeyExW(root, subkey, 0, access, &key);
    if (status != ERROR_SUCCESS)
        return result_from_status(status);
    RegCloseKey(key);
    return E_NOTIMPL;
}

HRESULT WINAPI QuerySourceCreateFromKey(HKEY root, LPCWSTR subkey, BOOL create, REFIID riid, void **out);
HRESULT WINAPI QuerySourceCreateFromKey(HKEY root, LPCWSTR subkey, BOOL create, REFIID riid, void **out)
{
    return QuerySourceCreateFromKeyEx(root, subkey, create, 0, riid, out);
}

/* a file stream with the options of the shell, of which the plain ones are kept. */
HRESULT WINAPI SHManagedCreateStreamOnFile(LPCWSTR path, DWORD mode, DWORD attributes, BOOL create, void *reserved,
                                           DWORD flags, IStream **stream);
HRESULT WINAPI SHManagedCreateStreamOnFile(LPCWSTR path, DWORD mode, DWORD attributes, BOOL create, void *reserved,
                                           DWORD flags, IStream **stream)
{
    HRESULT(WINAPI * create_stream)(LPCWSTR, DWORD, DWORD, BOOL, IStream *, IStream **) =
        (HRESULT(WINAPI *)(LPCWSTR, DWORD, DWORD, BOOL, IStream *, IStream **))ShlProc(L"shcore.dll",
                                                                                      "SHCreateStreamOnFileEx");
    *stream = nullptr;
    if (!create_stream)
        return E_FAIL;
    return create_stream(path, mode, attributes, create != 0, nullptr, stream);
}

/* the file handle object of the shell, with its oplocks and its change journal source, is not made. */
HRESULT WINAPI SHManagedCreateFile(LPCWSTR path, DWORD access, DWORD share, DWORD disposition, DWORD attributes,
                                   DWORD flags, void **handle);
HRESULT WINAPI SHManagedCreateFile(LPCWSTR path, DWORD access, DWORD share, DWORD disposition, DWORD attributes,
                                   DWORD flags, void **handle)
{
    *handle = nullptr;
    return E_NOTIMPL;
}
