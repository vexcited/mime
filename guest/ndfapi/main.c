/*
 * ndfapi: network diagnostics incidents. creating one only records what to
 * diagnose, as on Windows. executing a web, connectivity or connection
 * incident sends the user to the contact support app, as Windows 11 does.
 * the scripted diagnostics of the others need a diagnostic service mime
 * does not run.
 */
#include <mime/win32/objbase.h>

#define NDF_E_BAD_PARAM ((HRESULT)0x80040205L)
#define NDF_E_NOHELPERCLASS ((HRESULT)0x80040201L)
#define INCIDENT_MAGIC 0x49EE979964EB97E9ULL
#define ERROR_SERVICE_NOT_ACTIVE 1062L
#define SW_SHOWNORMAL 1

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

typedef enum {
    INCIDENT_GENERIC = 0,
    INCIDENT_CONNECTIVITY = 1,
    INCIDENT_WEB = 2,
    INCIDENT_SHARING = 3,
    INCIDENT_DNS = 4,
    INCIDENT_CONNECTION = 6,
} INCIDENT_KIND;

typedef struct INCIDENT {
    ULONGLONG magic;
    struct INCIDENT *next;
    INCIDENT_KIND kind;
} INCIDENT;

static SRWLOCK g_lock = SRWLOCK_INIT;
static INCIDENT *g_incidents;

static HRESULT create(INCIDENT_KIND kind, void **handle)
{
    INCIDENT *incident = LocalAlloc(LMEM_ZEROINIT, sizeof(*incident));
    if (!incident)
        return E_OUTOFMEMORY;
    incident->magic = INCIDENT_MAGIC;
    incident->kind = kind;
    AcquireSRWLockExclusive(&g_lock);
    incident->next = g_incidents;
    g_incidents = incident;
    ReleaseSRWLockExclusive(&g_lock);
    *handle = incident;
    return S_OK;
}

static INCIDENT *find(void *handle)
{
    INCIDENT *found = nullptr;
    AcquireSRWLockShared(&g_lock);
    for (INCIDENT *incident = g_incidents; incident; incident = incident->next)
        if (incident == handle)
            found = incident;
    ReleaseSRWLockShared(&g_lock);
    return found;
}

HRESULT WINAPI NdfCreateIncident(LPCWSTR helper, ULONG count, void *attributes, void **handle)
{
    if (!helper)
        return NDF_E_NOHELPERCLASS;
    if (!count != !attributes || !handle)
        return NDF_E_BAD_PARAM;
    return create(INCIDENT_GENERIC, handle);
}

HRESULT WINAPI NdfCreateWebIncident(LPCWSTR url, void **handle)
{
    if (!url || !handle)
        return NDF_E_BAD_PARAM;
    return create(INCIDENT_WEB, handle);
}

HRESULT WINAPI NdfCreateWebIncidentEx(LPCWSTR url, BOOL winhttp, LPWSTR module, void **handle)
{
    if (!url || !handle)
        return NDF_E_BAD_PARAM;
    return create(INCIDENT_WEB, handle);
}

HRESULT WINAPI NdfCreateConnectivityIncident(void **handle)
{
    if (!handle)
        return NDF_E_BAD_PARAM;
    return create(INCIDENT_CONNECTIVITY, handle);
}

HRESULT WINAPI NdfCreateNetConnectionIncident(void **handle, GUID id)
{
    if (!handle)
        return NDF_E_BAD_PARAM;
    return create(INCIDENT_CONNECTION, handle);
}

HRESULT WINAPI NdfCreateSharingIncident(LPCWSTR path, void **handle)
{
    if (!handle)
        return NDF_E_BAD_PARAM;
    return create(INCIDENT_SHARING, handle);
}

HRESULT WINAPI NdfCreateDNSIncident(LPCWSTR host, WORD type, void **handle)
{
    if (!host || !handle)
        return NDF_E_BAD_PARAM;
    return create(INCIDENT_DNS, handle);
}

HRESULT WINAPI NdfCloseIncident(void *handle)
{
    INCIDENT *incident = find(handle);
    if (!incident)
        return S_OK;
    AcquireSRWLockExclusive(&g_lock);
    for (INCIDENT **link = &g_incidents; *link; link = &(*link)->next)
        if (*link == incident) {
            *link = incident->next;
            break;
        }
    ReleaseSRWLockExclusive(&g_lock);
    incident->magic = 0;
    LocalFree(incident);
    return S_OK;
}

HRESULT WINAPI NdfExecuteDiagnosis(void *handle, HWND hwnd)
{
    INCIDENT *incident = find(handle);
    if (!incident)
        return E_HANDLE;
    LPCWSTR url;
    switch (incident->kind) {
    case INCIDENT_CONNECTIVITY: case INCIDENT_WEB: case INCIDENT_CONNECTION:
        url = L"ms-contact-support://?ActivationType=NetworkDiagnostics&invoker=NdfExecuteDiagnosis";
        break;
    case INCIDENT_SHARING:
        url = L"ms-contact-support://?Product=Windows&SearchKey=Network%20Share%20Issues";
        break;
    default:
        return HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE);
    }
    typedef HINSTANCE(WINAPI * SHELL_EXECUTE)(HWND, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, INT);
    HMODULE shell32 = LoadLibraryW(L"shell32.dll");
    SHELL_EXECUTE execute = shell32 ? (SHELL_EXECUTE)GetProcAddress(shell32, "ShellExecuteW") : nullptr;
    if (execute)
        execute(nullptr, nullptr, url, nullptr, nullptr, SW_SHOWNORMAL);
    return S_OK;
}

/* the diagnosis without a ui finds no root cause. */
HRESULT WINAPI NdfDiagnoseIncident(void *handle, ULONG *count, void **causes, DWORD wait, DWORD flags)
{
    if (!find(handle))
        return E_HANDLE;
    if (count)
        *count = 0;
    if (causes)
        *causes = nullptr;
    return S_OK;
}

HRESULT WINAPI NdfCancelIncident(void *handle)
{
    return find(handle) ? S_OK : E_HANDLE;
}

HRESULT WINAPI NdfGetTraceFile(void *handle, LPCWSTR *location)
{
    if (!find(handle))
        return E_HANDLE;
    if (location)
        *location = nullptr;
    return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
}
