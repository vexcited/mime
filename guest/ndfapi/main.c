/*
 * ndfapi: network diagnostics incidents. creating one only records what to
 * diagnose, as on Windows. executing a web, connectivity or connection
 * incident sends the user to the contact support app, as Windows 11 does.
 * the scripted diagnostics of the others and repairs need a diagnostic service
 * mime does not run. the dialogs of the rundll32 entry points are not shown.
 */
#include <mime/win32/objbase.h>

#define NDF_E_BAD_PARAM ((HRESULT)0x80040205L)
#define NDF_E_NOHELPERCLASS ((HRESULT)0x80040201L)
#define INCIDENT_MAGIC 0x49EE979964EB97E9ULL
#define INCIDENT_MAGIC2 0xC1B5FAFE15B09CBBULL
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
    INCIDENT_PNRP = 7,
    INCIDENT_INBOUND = 8,
    INCIDENT_GROUPING = 9,
    INCIDENT_WINSOCK = 5,
} INCIDENT_KIND;

/* the handle starts with 16 bytes that mark it, then the kind. */
typedef struct INCIDENT {
    ULONGLONG magic;
    ULONGLONG magic2;
    INCIDENT_KIND kind;
    struct INCIDENT *next;
} INCIDENT;

static SRWLOCK g_lock = SRWLOCK_INIT;
static INCIDENT *g_incidents;

static HRESULT create(INCIDENT_KIND kind, void **handle)
{
    INCIDENT *incident = LocalAlloc(LMEM_ZEROINIT, sizeof(*incident));
    if (!incident)
        return E_OUTOFMEMORY;
    incident->magic = INCIDENT_MAGIC;
    incident->magic2 = INCIDENT_MAGIC2;
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
    incident->magic2 = 0;
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

/* ------------------------------------------------------------------ incidents of the other kinds */

#define E_INVALID_ARG ((HRESULT)0x80070057L)

/* an application path is copied into a buffer of 260 characters. */
static BOOL app_id_fits(LPCWSTR app_id)
{
    return !app_id || lstrlenW(app_id) < 260;
}

typedef struct {
    LPVOID address;
    INT length;
} SOCKET_ADDRESS_;

typedef struct {
    INT count;
    SOCKET_ADDRESS_ addresses[1];
} SOCKET_ADDRESS_LIST_;

HRESULT WINAPI NdfCreateGroupingIncident(LPCWSTR cloud, LPCWSTR group, LPCWSTR identity, LPCWSTR invitation,
                                         SOCKET_ADDRESS_LIST_ *addresses, LPCWSTR app_id, void **handle)
{
    if (!handle || (addresses && addresses->count <= 0))
        return NDF_E_BAD_PARAM;
    if (!app_id_fits(app_id))
        return E_INVALID_ARG;
    if (addresses) {
        ULONG total = (ULONG)(sizeof(SOCKET_ADDRESS_) * (ULONG)addresses->count + 8);
        for (INT i = 0; i < addresses->count; i++) {
            ULONG before = total;
            total += addresses->addresses[i].length;
            if (total < before)
                return NDF_E_BAD_PARAM;
        }
    }
    return create(INCIDENT_GROUPING, handle);
}

HRESULT WINAPI NdfCreateInboundIncident(LPCWSTR application, LPCWSTR service, PVOID user, const void *local_target,
                                        DWORD protocol, DWORD flags, void **handle)
{
    if ((!application && !service) || flags > 3)
        return E_INVALID_ARG;
    if (!handle)
        return NDF_E_BAD_PARAM;
    return create(INCIDENT_INBOUND, handle);
}

HRESULT WINAPI NdfCreatePnrpIncident(LPCWSTR cloud, LPCWSTR peer, BOOL publish, LPCWSTR app_id, void **handle)
{
    if (!handle)
        return NDF_E_BAD_PARAM;
    if (!app_id_fits(app_id))
        return E_INVALID_ARG;
    return create(INCIDENT_PNRP, handle);
}

/* a socket error becomes the hresult of its win32 code. */
static HRESULT socket_error(int error)
{
    return error > 0 ? (HRESULT)((USHORT)error | 0x80070000) : (HRESULT)error;
}

HRESULT WINAPI NdfCreateWinSockIncident(ULONG_PTR socket, LPCWSTR host, USHORT port, LPCWSTR app_id, PVOID user,
                                        void **handle)
{
    if (!socket || !handle)
        return NDF_E_BAD_PARAM;
    HMODULE ws2 = LoadLibraryW(L"ws2_32.dll");
    typedef int(WINAPI * GET_OPTION)(ULONG_PTR, int, int, char *, int *);
    typedef int(WINAPI * GET_NAME)(ULONG_PTR, void *, int *);
    typedef int(WINAPI * LAST_ERROR)(void);
    GET_OPTION get_option = ws2 ? (GET_OPTION)GetProcAddress(ws2, "getsockopt") : nullptr;
    GET_NAME get_name = ws2 ? (GET_NAME)GetProcAddress(ws2, "getsockname") : nullptr;
    GET_NAME get_peer = ws2 ? (GET_NAME)GetProcAddress(ws2, "getpeername") : nullptr;
    LAST_ERROR last_error = ws2 ? (LAST_ERROR)GetProcAddress(ws2, "WSAGetLastError") : nullptr;
    if (!get_option || !get_name || !get_peer || !last_error)
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    char protocol[628] = {0};
    BYTE name[128] = {0}, peer[128] = {0};
    int length = sizeof(protocol);
    /* SO_PROTOCOL_INFOW of SOL_SOCKET. */
    if (get_option(socket, 0xFFFF, 8197, protocol, &length))
        return socket_error(last_error());
    length = sizeof(name);
    if (get_name(socket, name, &length))
        return socket_error(last_error());
    length = sizeof(peer);
    if (get_peer(socket, peer, &length))
        return socket_error(last_error());
    if (*(USHORT *)peer != 2 && *(USHORT *)peer != 23)
        return E_INVALID_ARG;
    if (!app_id_fits(app_id))
        return E_INVALID_ARG;
    return create(INCIDENT_WINSOCK, handle);
}

/* a handle that is not an incident of this library. */
static BOOL is_incident(const void *handle)
{
    const ULONGLONG *marks = handle;
    return handle && marks[0] == INCIDENT_MAGIC && marks[1] == INCIDENT_MAGIC2;
}

/* repairing goes through the diagnostics service, which is not running. */
HRESULT WINAPI NdfRepairIncident(void *handle, void *repair, DWORD wait)
{
    if (!is_incident(handle))
        return E_HANDLE;
    return HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE);
}

HRESULT WINAPI NdfRepairIncidentEx(void *handle, void *repair, DWORD wait, ULONG_PTR context)
{
    if (!is_incident(handle))
        return E_HANDLE;
    return HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE);
}

/* ------------------------------------------------------------------ the rundll32 entry points */

void WINAPI NdfRunDllDiagnoseIncident(HWND window, HINSTANCE instance, const char *command, int show)
{
    void *handle = nullptr;
    if (NdfCreateConnectivityIncident(&handle) >= 0) {
        NdfExecuteDiagnosis(handle, nullptr);
        NdfCloseIncident(handle);
    }
}

static int hex_digit(WCHAR c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* the command line is the guid of the connection in braces. */
void WINAPI NdfRunDllDiagnoseNetConnectionIncident(HWND window, HINSTANCE instance, const char *command, int show)
{
    WCHAR text[64];
    if (!command || MultiByteToWideChar(CP_ACP, 0, command, -1, text, 64) <= 0 || lstrlenW(text) != 38 ||
        text[0] != '{' || text[37] != '}')
        return;
    static const BYTE starts[16] = {1, 3, 5, 7, 10, 12, 15, 17, 20, 22, 25, 27, 29, 31, 33, 35};
    BYTE bytes[16];
    for (int i = 0; i < 16; i++) {
        int high = hex_digit(text[starts[i]]), low = hex_digit(text[starts[i] + 1]);
        if (high < 0 || low < 0)
            return;
        bytes[i] = (BYTE)(high << 4 | low);
    }
    GUID id;
    id.Data1 = (ULONG)bytes[0] << 24 | (ULONG)bytes[1] << 16 | (ULONG)bytes[2] << 8 | bytes[3];
    id.Data2 = (USHORT)(bytes[4] << 8 | bytes[5]);
    id.Data3 = (USHORT)(bytes[6] << 8 | bytes[7]);
    memcpy(id.Data4, bytes + 8, 8);
    void *handle = nullptr;
    if (NdfCreateNetConnectionIncident(&handle, id) >= 0) {
        NdfExecuteDiagnosis(handle, nullptr);
        NdfCloseIncident(handle);
    }
}

/* the answer file is run through the support diagnostic tool, which windows 11 no longer ships. */
void WINAPI NdfRunDllDiagnoseWithAnswerFile(HWND window, HINSTANCE instance, const char *command, int show)
{
}

/* the message boxes of the duplicate address detection and the help topic window are not shown. */
void WINAPI NdfRunDllDuplicateIPDefendingSystem(HWND window, HINSTANCE instance, const char *command, int show)
{
}

void WINAPI NdfRunDllDuplicateIPOffendingSystem(HWND window, HINSTANCE instance, const char *command, int show)
{
}

void WINAPI NdfRunDllHelpTopic(HWND window, HINSTANCE instance, const char *command, int show)
{
}

/* ------------------------------------------------------------------ the com module */

/* no object is counted. */
HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_OK;
}

/* the library has no class object to give. */
HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}

/* it has no class to register either. */
HRESULT WINAPI DllRegisterServer(void)
{
    return S_OK;
}

HRESULT WINAPI DllUnregisterServer(void)
{
    return S_OK;
}
