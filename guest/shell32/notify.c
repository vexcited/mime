/* shell change notifications, the user notification state, revealing items and IApplicationAssociationRegistration. */
#include "shell32_private.h"

/* with no shell to relay them, events never leave this process and SHChangeNotify posts nothing. */
typedef struct SH_REGISTRATION {
    ULONG Id;
    HWND Window;
    UINT Message;
} SH_REGISTRATION;

static SH_REGISTRATION g_registrations[64];
static ULONG g_next_registration = 1;
static SRWLOCK g_registration_lock = SRWLOCK_INIT;

SHSTDAPI ULONG WINAPI SHChangeNotifyRegister(HWND hwnd, int sources, LONG events, UINT message, int count,
                                             const void *entries)
{
    if (!hwnd || count < 1 || !entries)
        return 0;
    ULONG id = 0;
    AcquireSRWLockExclusive(&g_registration_lock);
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_registrations); i++) {
        if (!g_registrations[i].Id) {
            id = g_next_registration++;
            g_registrations[i] = (SH_REGISTRATION){id, hwnd, message};
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_registration_lock);
    return id;
}

SHSTDAPI BOOL WINAPI SHChangeNotifyDeregister(ULONG id)
{
    BOOL found = FALSE;
    AcquireSRWLockExclusive(&g_registration_lock);
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_registrations) && id; i++) {
        if (g_registrations[i].Id == id) {
            g_registrations[i].Id = 0;
            found = TRUE;
        }
    }
    ReleaseSRWLockExclusive(&g_registration_lock);
    return found;
}

/* no shell relays events, so no message ever carries an event handle to lock. exported by ordinal. */
SHSTDAPI HANDLE WINAPI SHChangeNotification_Lock(HANDLE change, DWORD process, PIDLIST_ABSOLUTE **pidls, LONG *event)
{
    if (pidls)
        *pidls = nullptr;
    if (event)
        *event = 0;
    SetLastError(ERROR_INVALID_HANDLE);
    return nullptr;
}

SHSTDAPI BOOL WINAPI SHChangeNotification_Unlock(HANDLE lock)
{
    SetLastError(ERROR_INVALID_HANDLE);
    return FALSE;
}

#define QUNS_BUSY 1
#define WM_QUERY_NOTIFICATION_STATE (WM_USER + 0x3EF)

/* asks the taskbar like windows, and reports busy when there is none or it does not answer. */
SHSTDAPI HRESULT WINAPI SHQueryUserNotificationState(int *state)
{
    if (!state)
        return E_INVALIDARG;
    *state = QUNS_BUSY;
    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    DWORD_PTR result = 0;
    if (tray && SendMessageTimeoutW(tray, WM_QUERY_NOTIFICATION_STATE, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 7000,
                                    &result))
        *state = (int)result;
    return S_OK;
}

/* windows has explorer open the folder. without explorer this fails like a missing handler. */
SHSTDAPI HRESULT WINAPI SHOpenFolderAndSelectItems(PCIDLIST_ABSOLUTE folder, UINT count, PCUITEMID_CHILD *items,
                                                   DWORD flags)
{
    if (!folder)
        return E_INVALIDARG;
    return HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION);
}

typedef enum {
    AT_FILEEXTENSION,
    AT_URLPROTOCOL,
    AT_STARTMENUCLIENT,
    AT_MIMETYPE,
} ASSOCIATIONTYPE;

typedef struct IApplicationAssociationRegistration IApplicationAssociationRegistration;

typedef struct IApplicationAssociationRegistrationVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IApplicationAssociationRegistration *, REFIID, void **);
    ULONG(STDMETHODCALLTYPE *AddRef)(IApplicationAssociationRegistration *);
    ULONG(STDMETHODCALLTYPE *Release)(IApplicationAssociationRegistration *);
    HRESULT(STDMETHODCALLTYPE *QueryCurrentDefault)(IApplicationAssociationRegistration *, LPCWSTR, ASSOCIATIONTYPE,
                                                    int, LPWSTR *);
    HRESULT(STDMETHODCALLTYPE *QueryAppIsDefault)(IApplicationAssociationRegistration *, LPCWSTR, ASSOCIATIONTYPE,
                                                  int, LPCWSTR, BOOL *);
    HRESULT(STDMETHODCALLTYPE *QueryAppIsDefaultAll)(IApplicationAssociationRegistration *, int, LPCWSTR, BOOL *);
    HRESULT(STDMETHODCALLTYPE *SetAppAsDefault)(IApplicationAssociationRegistration *, LPCWSTR, LPCWSTR,
                                                ASSOCIATIONTYPE);
    HRESULT(STDMETHODCALLTYPE *SetAppAsDefaultAll)(IApplicationAssociationRegistration *, LPCWSTR);
    HRESULT(STDMETHODCALLTYPE *ClearUserAssociations)(IApplicationAssociationRegistration *);
} IApplicationAssociationRegistrationVtbl;

struct IApplicationAssociationRegistration {
    const IApplicationAssociationRegistrationVtbl *lpVtbl;
};

typedef struct ASSOC_REG {
    IApplicationAssociationRegistration Interface;
    LONG Refs;
} ASSOC_REG;

static const IID IID_IApplicationAssociationRegistration = {
    0x4e530b0a, 0xe611, 0x4c77, {0xa3, 0xac, 0x90, 0x31, 0xd0, 0x22, 0x28, 0x1b}};

static HRESULT STDMETHODCALLTYPE aar_query_interface(IApplicationAssociationRegistration *self, REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IApplicationAssociationRegistration)) {
        *out = self;
        self->lpVtbl->AddRef(self);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE aar_add_ref(IApplicationAssociationRegistration *self)
{
    return InterlockedIncrement(&((ASSOC_REG *)self)->Refs);
}

static ULONG STDMETHODCALLTYPE aar_release(IApplicationAssociationRegistration *self)
{
    ULONG refs = InterlockedDecrement(&((ASSOC_REG *)self)->Refs);
    if (!refs)
        ShFree(self);
    return refs;
}

/* the user's UserChoice first, then the class registered for the extension. */
static HRESULT current_default(LPCWSTR query, ASSOCIATIONTYPE type, LPWSTR *result)
{
    WCHAR path[512], value[256];
    DWORD size = sizeof(value);
    if (type == AT_FILEEXTENSION) {
        if (query[0] != L'.')
            return E_INVALIDARG;
        _snwprintf(path, RTL_NUMBER_OF(path),
                   L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\%s\\UserChoice", query);
    } else if (type == AT_URLPROTOCOL) {
        _snwprintf(path, RTL_NUMBER_OF(path),
                   L"Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\%s\\UserChoice", query);
    } else if (type == AT_STARTMENUCLIENT) {
        _snwprintf(path, RTL_NUMBER_OF(path), L"Software\\Clients\\%s", query);
        if (!RegGetValueW(HKEY_CURRENT_USER, path, nullptr, RRF_RT_REG_SZ, nullptr, value, &size) ||
            (size = sizeof(value), !RegGetValueW(HKEY_LOCAL_MACHINE, path, nullptr, RRF_RT_REG_SZ, nullptr, value,
                                                 &size))) {
            *result = ShDuplicate(value);
            return *result ? S_OK : E_OUTOFMEMORY;
        }
        return HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION);
    } else if (type == AT_MIMETYPE) {
        _snwprintf(path, RTL_NUMBER_OF(path), L"MIME\\Database\\Content Type\\%s", query);
        if (RegGetValueW(HKEY_CLASSES_ROOT, path, L"Extension", RRF_RT_REG_SZ, nullptr, value, &size))
            return HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION);
        return current_default(value, AT_FILEEXTENSION, result);
    } else {
        return E_INVALIDARG;
    }
    if (!RegGetValueW(HKEY_CURRENT_USER, path, L"ProgId", RRF_RT_REG_SZ, nullptr, value, &size)) {
        *result = ShDuplicate(value);
        return *result ? S_OK : E_OUTOFMEMORY;
    }
    size = sizeof(value);
    if (type == AT_FILEEXTENSION &&
        !RegGetValueW(HKEY_CLASSES_ROOT, query, nullptr, RRF_RT_REG_SZ, nullptr, value, &size) && value[0]) {
        *result = ShDuplicate(value);
        return *result ? S_OK : E_OUTOFMEMORY;
    }
    return HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION);
}

static HRESULT STDMETHODCALLTYPE aar_query_current_default(IApplicationAssociationRegistration *self, LPCWSTR query,
                                                           ASSOCIATIONTYPE type, int level, LPWSTR *result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!query || !query[0])
        return E_INVALIDARG;
    return current_default(query, type, result);
}

/* the class the application's registered capabilities give for the query. */
static BOOL app_class(LPCWSTR app, LPCWSTR query, ASSOCIATIONTYPE type, LPWSTR out, DWORD size)
{
    WCHAR capabilities[512], path[600];
    DWORD n = sizeof(capabilities);
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", app, RRF_RT_REG_SZ, nullptr,
                     capabilities, &n)) {
        n = sizeof(capabilities);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"Software\\RegisteredApplications", app, RRF_RT_REG_SZ, nullptr,
                         capabilities, &n))
            return FALSE;
    }
    LPCWSTR sub = type == AT_URLPROTOCOL ? L"URLAssociations" : type == AT_MIMETYPE ? L"MIMEAssociations"
                                                                                    : L"FileAssociations";
    _snwprintf(path, RTL_NUMBER_OF(path), L"%s\\%s", capabilities, sub);
    DWORD bytes = size * sizeof(WCHAR);
    if (!RegGetValueW(HKEY_CURRENT_USER, path, query, RRF_RT_REG_SZ, nullptr, out, &bytes))
        return TRUE;
    bytes = size * sizeof(WCHAR);
    return !RegGetValueW(HKEY_LOCAL_MACHINE, path, query, RRF_RT_REG_SZ, nullptr, out, &bytes);
}

static HRESULT STDMETHODCALLTYPE aar_query_app_is_default(IApplicationAssociationRegistration *self, LPCWSTR query,
                                                          ASSOCIATIONTYPE type, int level, LPCWSTR app,
                                                          BOOL *is_default)
{
    if (!query || !app || !is_default)
        return E_INVALIDARG;
    *is_default = FALSE;
    WCHAR wanted[256];
    if (!app_class(app, query, type, wanted, RTL_NUMBER_OF(wanted)))
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    LPWSTR current = nullptr;
    if (SUCCEEDED(current_default(query, type, &current))) {
        *is_default = !_wcsicmp(current, wanted);
        ShFree(current);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE aar_query_app_is_default_all(IApplicationAssociationRegistration *self, int level,
                                                              LPCWSTR app, BOOL *is_default)
{
    if (!app || !is_default)
        return E_INVALIDARG;
    *is_default = FALSE;
    return S_OK;
}

/* since windows 8 programs can no longer make themselves the default. */
static HRESULT STDMETHODCALLTYPE aar_set_app_as_default(IApplicationAssociationRegistration *self, LPCWSTR app,
                                                        LPCWSTR query, ASSOCIATIONTYPE type)
{
    return E_ACCESSDENIED;
}

static HRESULT STDMETHODCALLTYPE aar_set_app_as_default_all(IApplicationAssociationRegistration *self, LPCWSTR app)
{
    return E_ACCESSDENIED;
}

static HRESULT STDMETHODCALLTYPE aar_clear_user_associations(IApplicationAssociationRegistration *self)
{
    return E_ACCESSDENIED;
}

static const IApplicationAssociationRegistrationVtbl g_aar_vtbl = {
    aar_query_interface,      aar_add_ref,         aar_release,
    aar_query_current_default, aar_query_app_is_default, aar_query_app_is_default_all,
    aar_set_app_as_default,   aar_set_app_as_default_all, aar_clear_user_associations,
};

SHSTDAPI HRESULT WINAPI SHCreateAssociationRegistration(REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    ASSOC_REG *reg = ShAllocZero(sizeof(*reg));
    if (!reg)
        return E_OUTOFMEMORY;
    reg->Interface.lpVtbl = &g_aar_vtbl;
    reg->Refs = 1;
    HRESULT hr = aar_query_interface(&reg->Interface, iid, out);
    aar_release(&reg->Interface);
    return hr;
}
