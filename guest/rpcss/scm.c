#include "rpcss.h"
#include <stdarg.h>

#include "rpcss/scm.h"

typedef struct RS_CLASS_ENTRY {
    struct RS_CLASS_ENTRY *Next;
    RS_CLASS Class;
} RS_CLASS_ENTRY;

/* an interface a window carries for drag and drop, kept for the processes that look it up by the window. */
typedef struct RS_WINDOW_PROP {
    struct RS_WINDOW_PROP *Next;
    UINT_PTR Window, Cookie;
    DWORD Pid;
    STDOBJREF Std;
    OXID_INFO Info;
} RS_WINDOW_PROP;

static SRWLOCK g_scm_lock = SRWLOCK_INIT;
static RS_CLASS_ENTRY *g_classes;
static RS_WINDOW_PROP *g_props;
static ULONG g_next_key = 1;
static UINT_PTR g_next_cookie = 1;

void RsProcessGone(DWORD pid)
{
    AcquireSRWLockExclusive(&g_scm_lock);
    for (RS_CLASS_ENTRY **q = &g_classes; *q;) {
        RS_CLASS_ENTRY *e = *q;
        if (e->Class.Pid == pid) {
            *q = e->Next;
            RsFree(e);
        } else {
            q = &e->Next;
        }
    }
    for (RS_WINDOW_PROP **q = &g_props; *q;) {
        RS_WINDOW_PROP *w = *q;
        if (w->Pid == pid) {
            *q = w->Next;
            RsFree(w->Info.psa);
            RsFree(w);
        } else {
            q = &w->Next;
        }
    }
    ReleaseSRWLockExclusive(&g_scm_lock);
}

/* the newest registration of a class wins, as a list with the newest first gives. */
BOOL RsScmFindClass(const GUID *clsid, RS_CLASS *out)
{
    BOOL found = FALSE;
    AcquireSRWLockShared(&g_scm_lock);
    for (RS_CLASS_ENTRY *e = g_classes; e && !found; e = e->Next) {
        if (!memcmp(&e->Class.Clsid, clsid, sizeof(GUID))) {
            *out = e->Class;
            found = TRUE;
        }
    }
    ReleaseSRWLockShared(&g_scm_lock);
    return found;
}

long WINAPI ISCM_NotifyStarted_s(handle_t hRpc, PHPROCESS phProcess, unsigned long cPackages, RPCSS_HSTRING *packages,
                                 REG_INPUT *pRegInput, REG_OUTPUT **ppRegOutput, error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    *ppRegOutput = nullptr;
    DWORD pid = RsCallerOf(hRpc, phProcess);
    if (!pid)
        return E_ACCESSDENIED;
    ULONG count = pRegInput->dwSize;
    REG_OUTPUT *out = MIDL_user_allocate(FIELD_OFFSET(REG_OUTPUT, RegKeys) + count * sizeof(ULONG));
    if (!out)
        return E_OUTOFMEMORY;
    out->dwSize = count;
    AcquireSRWLockExclusive(&g_scm_lock);
    for (ULONG i = 0; i < count; i++) {
        const REG_INPUT_ENTRY *in = &pRegInput->rginent[i];
        RS_CLASS_ENTRY *e = RsAllocZero(sizeof(*e));
        if (!e) {
            out->RegKeys[i] = 0;
            continue;
        }
        memcpy(&e->Class.Clsid, &in->clsid, sizeof(GUID));
        memcpy(&e->Class.Ipid, &in->ipid, sizeof(GUID));
        e->Class.Oxid = in->oxid;
        e->Class.Flags = in->dwFlags;
        e->Class.Pid = pid;
        e->Class.Key = g_next_key++;
        e->Next = g_classes;
        g_classes = e;
        out->RegKeys[i] = e->Class.Key;
    }
    ReleaseSRWLockExclusive(&g_scm_lock);
    *ppRegOutput = out;
    return S_OK;
}

void WINAPI ISCM_NotifyStopped_s(handle_t hRpc, PHPROCESS phProcess, REVOKE_INPUT *pRevInput, error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    DWORD pid = RsCallerOf(hRpc, phProcess);
    if (!pid)
        return;
    AcquireSRWLockExclusive(&g_scm_lock);
    for (ULONG i = 0; i < pRevInput->dwSize; i++) {
        const REVOKE_ENTRY *r = &pRevInput->revent[i];
        for (RS_CLASS_ENTRY **q = &g_classes; *q; q = &(*q)->Next) {
            RS_CLASS_ENTRY *e = *q;
            if (e->Class.Pid == pid && e->Class.Key == r->dwReg && !memcmp(&e->Class.Clsid, &r->clsid, sizeof(GUID))) {
                *q = e->Next;
                RsFree(e);
                break;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_scm_lock);
}

/* windows runtime classes of packaged servers: mime has no packages, so none registers. */
long WINAPI ISCM_NotifyActivatableClassStarted_s(handle_t hRpc, PHPROCESS phProcess,
                                                 ACTIVATABLE_CLASS_REG_INPUT *pRegInput, REG_OUTPUT **ppRegOutput,
                                                 error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    *ppRegOutput = nullptr;
    if (!RsCallerOf(hRpc, phProcess))
        return E_ACCESSDENIED;
    RsLog("ISCM NotifyActivatableClassStarted is not served");
    return E_NOTIMPL;
}

void WINAPI ISCM_NotifyActivatableClassStopped_s(handle_t hRpc, PHPROCESS phProcess,
                                                 ACTIVATABLE_CLASS_STOP_INPUT *pRevInput, error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
}

void WINAPI ISCM_GetThreadID_s(handle_t hRpc, PHPROCESS phProcess, unsigned long *pThreadID, error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    *pThreadID = RsCallerOf(hRpc, phProcess) ? RsNextThreadId() : 0;
}

void WINAPI ISCM_UpdateActivationSettings_s(handle_t hRpc, PHPROCESS phProcess, error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
}

static DUALSTRINGARRAY *dsa_copy(const DUALSTRINGARRAY *d)
{
    if (!d)
        return nullptr;
    SIZE_T size = FIELD_OFFSET(DUALSTRINGARRAY, aStringArray) + d->wNumEntries * sizeof(USHORT);
    DUALSTRINGARRAY *c = MIDL_user_allocate(size);
    if (c)
        memcpy(c, d, size);
    return c;
}

/*
 * RegisterDragDrop puts a marshaled IDropTarget on the window: the process keeps the object reference and rpcss the oxid
 * it lives in, so another process unmarshals it without a resolution. only the parts of the oxid a local client needs are
 * kept, the hstrings and the container extensions are not.
 */
long WINAPI ISCM_RegisterWindowPropInterface_s(handle_t hRpc, PHPROCESS phProcess, UINT_PTR hWnd, STDOBJREF *pStd,
                                               OXID_INFO *pOxidInfo, unsigned long fFlags, UINT_PTR *pdwCookie,
                                               error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    *pdwCookie = 0;
    DWORD pid = RsCallerOf(hRpc, phProcess);
    if (!pid)
        return E_ACCESSDENIED;
    RS_WINDOW_PROP *w = RsAllocZero(sizeof(*w));
    if (!w)
        return E_OUTOFMEMORY;
    w->Window = hWnd;
    w->Pid = pid;
    w->Std = *pStd;
    w->Info = *pOxidInfo;
    w->Info.containerVersion.extensions = nullptr;
    w->Info.packageFullName = w->Info.userSid = w->Info.appcontainerSid = nullptr;
    w->Info.psa = dsa_copy(pOxidInfo->psa);
    AcquireSRWLockExclusive(&g_scm_lock);
    w->Cookie = g_next_cookie++;
    w->Next = g_props;
    g_props = w;
    *pdwCookie = w->Cookie;
    ReleaseSRWLockExclusive(&g_scm_lock);
    return S_OK;
}

long WINAPI ISCM_GetWindowPropInterface_s(handle_t hRpc, PHPROCESS phProcess, UINT_PTR hWnd, UINT_PTR dwCookie,
                                          unsigned long fRevoke, STDOBJREF *pStd, OXID_INFO *pOxidInfo,
                                          error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    memset(pStd, 0, sizeof(*pStd));
    memset(pOxidInfo, 0, sizeof(*pOxidInfo));
    if (!RsCallerOf(hRpc, phProcess))
        return E_ACCESSDENIED;
    HRESULT hr = E_INVALIDARG;
    AcquireSRWLockExclusive(&g_scm_lock);
    for (RS_WINDOW_PROP **q = &g_props; *q; q = &(*q)->Next) {
        RS_WINDOW_PROP *w = *q;
        if (w->Window != hWnd || w->Cookie != dwCookie)
            continue;
        *pStd = w->Std;
        *pOxidInfo = w->Info;
        pOxidInfo->psa = dsa_copy(w->Info.psa);
        hr = S_OK;
        if (fRevoke) {
            *q = w->Next;
            RsFree(w->Info.psa);
            RsFree(w);
        }
        break;
    }
    ReleaseSRWLockExclusive(&g_scm_lock);
    return hr;
}

long WINAPI ISCM_EnableDisableDynamicIPTracking_s(handle_t hRpc, PHPROCESS phProcess, unsigned long fEnable,
                                                  error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    return RsCallerOf(hRpc, phProcess) ? S_OK : E_ACCESSDENIED;
}

long WINAPI ISCM_GetCurrentAddrExclusionList_s(handle_t hRpc, PHPROCESS phProcess, unsigned long *pdwNumStrings,
                                               unsigned short ***pppszStrings, error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    *pdwNumStrings = 0;
    *pppszStrings = nullptr;
    return RsCallerOf(hRpc, phProcess) ? S_OK : E_ACCESSDENIED;
}

/* the exclusion list only filters the addresses of remote bindings, which mime does not publish. */
long WINAPI ISCM_SetAddrExclusionList_s(handle_t hRpc, PHPROCESS phProcess, unsigned long dwNumStrings,
                                        unsigned short **ppszStrings, error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    return RsCallerOf(hRpc, phProcess) ? S_OK : E_ACCESSDENIED;
}

long WINAPI ISCM_FlushSCMBindings_s(handle_t hRpc, PHPROCESS phProcess, unsigned short *pszMachineName,
                                    error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    return RsCallerOf(hRpc, phProcess) ? S_OK : E_ACCESSDENIED;
}

long WINAPI ISCM_RetireServer_s(handle_t hRpc, PHPROCESS phProcess, GUID *pguidProcessIdentifier,
                                error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    return RsCallerOf(hRpc, phProcess) ? S_OK : E_ACCESSDENIED;
}

long WINAPI ISCM_NotifyDDStartOrStop_s(handle_t hRpc, PHPROCESS phProcess, unsigned long fStart,
                                       error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    return RsCallerOf(hRpc, phProcess) ? S_OK : E_ACCESSDENIED;
}

long WINAPI ISCM_IsObjectCreationAllowed_s(handle_t hRpc, PHPROCESS phProcess, GUID *clsid, unsigned long cPackages,
                                           RPCSS_HSTRING *packages, unsigned long cMains, RPCSS_HSTRING *mains,
                                           unsigned long dwFlags, unsigned long dwContext, unsigned long *pfAllowed,
                                           unsigned long *pfRegistered, unsigned long *pdwProcessId,
                                           error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    *pfAllowed = TRUE;
    RS_CLASS c;
    *pfRegistered = RsScmFindClass(clsid, &c);
    *pdwProcessId = *pfRegistered ? c.Pid : 0;
    return RsCallerOf(hRpc, phProcess) ? S_OK : E_ACCESSDENIED;
}

long WINAPI ISCM_ControlTracingForProcess_s(handle_t hRpc, PHPROCESS phProcess, unsigned long dwPID,
                                            unsigned long fEnable, error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    return RsCallerOf(hRpc, phProcess) ? S_OK : E_ACCESSDENIED;
}

long WINAPI ISCM_QueryPIDForActivation_s(handle_t hRpc, PHPROCESS phProcess, GUID *clsid, unsigned long dwClsCtx,
                                         unsigned long *pdwPID, error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    *pdwPID = 0;
    if (!RsCallerOf(hRpc, phProcess))
        return E_ACCESSDENIED;
    RS_CLASS c;
    if (!RsScmFindClass(clsid, &c))
        return REGDB_E_CLASSNOTREG;
    *pdwPID = c.Pid;
    return S_OK;
}

long WINAPI ISCM_NotifyWinRTActivationStoreChanged_s(handle_t hRpc, PHPROCESS phProcess, error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    return RsCallerOf(hRpc, phProcess) ? S_OK : E_ACCESSDENIED;
}

/* a debugger asks which server a proxy of a client talks to; it would read the client's memory with the offsets Connect
 * received. not done. */
long WINAPI ISCM_DecodeProxy_s(handle_t hRpc, PHPROCESS phProcess, unsigned long dwClientPid,
                               unsigned long long ui64ProxyAddress, unsigned long *pdwServerPid,
                               unsigned long *pdwServerTid, unsigned long long *pui64ServerAddress,
                               error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    *pdwServerPid = *pdwServerTid = 0;
    *pui64ServerAddress = 0;
    RsLog("ISCM DecodeProxy is not served");
    return E_NOTIMPL;
}

long WINAPI ISCM_NotifyPsmResume_s(handle_t hRpc, PHPROCESS phProcess, error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    return RsCallerOf(hRpc, phProcess) ? S_OK : E_ACCESSDENIED;
}

long WINAPI ISCM_QueryServerProcessHandleHeld_s(handle_t hRpc, PHPROCESS phProcess, GUID *pguidProcess,
                                                unsigned long *pfHeld, error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    *pfHeld = FALSE;
    return RsCallerOf(hRpc, phProcess) ? S_OK : E_ACCESSDENIED;
}

long WINAPI ISCM_RegisterRacActivationToken_s(handle_t hRpc, PHPROCESS phProcess, unsigned long hToken,
                                              unsigned long long *pId, error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    *pId = 0;
    RsLog("ISCM RegisterRacActivationToken is not served");
    return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
}

long WINAPI ISCM_RevokeRacActivationToken_s(handle_t hRpc, PHPROCESS phProcess, unsigned long long id,
                                            error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
}

long WINAPI ISCM_RegisterConsoleHandles_s(handle_t hRpc, PHPROCESS phProcess, unsigned long hInput,
                                          unsigned long hOutput, unsigned long hError, unsigned long long *pId,
                                          error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    *pId = 0;
    RsLog("ISCM RegisterConsoleHandles is not served");
    return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
}

long WINAPI ISCM_RevokeConsoleHandles_s(handle_t hRpc, PHPROCESS phProcess, unsigned long long id,
                                        error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
}

long WINAPI ISCM_NotifyComClassChangesFromDeployment_s(handle_t hRpc, PHPROCESS phProcess, error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    return RsCallerOf(hRpc, phProcess) ? S_OK : E_ACCESSDENIED;
}

long WINAPI ISCM_LogMachineClassesRootPermissions_s(handle_t hRpc, PHPROCESS phProcess, error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    return RsCallerOf(hRpc, phProcess) ? S_OK : E_ACCESSDENIED;
}

/* ------------------------------------------------------------------ ISCMLocalActivator */

void WINAPI ISCMLocalActivator_QueryInterface_s(handle_t hRpc)
{
}

void WINAPI ISCMLocalActivator_AddRef_s(handle_t hRpc)
{
}

void WINAPI ISCMLocalActivator_Release_s(handle_t hRpc)
{
}

static void log_request(const char *what, const MInterfacePointer *p)
{
    char hex[3 * 96 + 1];
    ULONG n = p ? (p->ulCntData < 96 ? p->ulCntData : 96) : 0;
    for (ULONG i = 0; i < n; i++)
        _snprintf(hex + 3 * i, 4, "%02x ", p->abData[i]);
    hex[3 * n] = 0;
    RsLog("%s: activation properties of %lu bytes not understood: %s", what, p ? p->ulCntData : 0, hex);
}

#define GetExceptionCode() __exception_code()
#define RPC_C_QOS_CAPABILITIES_DEFAULT_ 0
#define RPC_C_QOS_IDENTITY_DYNAMIC_ 1
#define CO_E_SERVER_EXEC_FAILURE ((HRESULT)0x80080005L)
#define RPC_E_DISCONNECTED_ ((HRESULT)0x80010108L)
#define CLSCTX_LOCAL_SERVER_ 0x4
#define CLSCTX_ACTIVATE_32_BIT_SERVER_ 0x40000
#define CLSCTX_ACTIVATE_64_BIT_SERVER_ 0x80000
#define ORPCF_LOCAL 0x1
/* how long a server rpcss started has to register its class, as the launch timeout of windows' rpcss. */
#define LAUNCH_WAIT_MS 120000

/* the manager routines of the activator of a server's apartment, which rpcss only calls. */
void WINAPI ILocalSystemActivator_QueryInterface_s(handle_t hRpc)
{
}

void WINAPI ILocalSystemActivator_AddRef_s(handle_t hRpc)
{
}

void WINAPI ILocalSystemActivator_Release_s(handle_t hRpc)
{
}

long WINAPI ILocalSystemActivator_GetClassObject_s(handle_t hRpc, ORPCTHIS *orpcthis, LOCALTHIS *localthis,
                                                   ORPCTHAT *orpcthat, LOCALTHAT *localthat,
                                                   MInterfacePointer *pActProperties, MInterfacePointer **ppActProperties)
{
    return E_NOTIMPL;
}

long WINAPI ILocalSystemActivator_CreateInstance_s(handle_t hRpc, ORPCTHIS *orpcthis, LOCALTHIS *localthis,
                                                   ORPCTHAT *orpcthat, LOCALTHAT *localthat, MInterfacePointer *pUnkOuter,
                                                   MInterfacePointer *pActProperties, MInterfacePointer **ppActProperties)
{
    return E_NOTIMPL;
}

/* how a class is served out of its client's process: by a service or by a program of its own. */
typedef struct SERVER {
    WCHAR Service[256], Parameters[512];
    WCHAR Command[1024];
} SERVER;

static void clsid_text(const GUID *g, WCHAR *out, SIZE_T chars)
{
    _snwprintf(out, chars, L"{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", g->Data1, g->Data2, g->Data3,
               g->Data4[0], g->Data4[1], g->Data4[2], g->Data4[3], g->Data4[4], g->Data4[5], g->Data4[6], g->Data4[7]);
}

static BOOL read_text(HKEY key, const WCHAR *sub, const WCHAR *name, WCHAR *out, DWORD chars)
{
    DWORD bytes = chars * sizeof(WCHAR), type = 0;
    WCHAR raw[1024];
    if (RegGetValueW(key, sub, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, &type, raw, &bytes) ||
        !raw[0])
        return FALSE;
    if (type == REG_EXPAND_SZ)
        return ExpandEnvironmentStringsW(raw, out, chars) && out[0];
    lstrcpynW(out, raw, chars);
    return TRUE;
}

/* the server of a class in one view of the registry, the caller's classes before the machine's. */
static BOOL server_in_view(const WCHAR *clsid, BOOL wow, HKEY user, SERVER *s)
{
    WCHAR path[128];
    _snwprintf(path, RTL_NUMBER_OF(path), wow ? L"Software\\Classes\\WOW6432Node\\CLSID\\%ls"
                                              : L"Software\\Classes\\CLSID\\%ls", clsid);
    HKEY roots[] = {user, HKEY_LOCAL_MACHINE};
    for (ULONG r = 0; r < RTL_NUMBER_OF(roots); r++) {
        HKEY k;
        if (!roots[r] || RegOpenKeyExW(roots[r], path, 0, KEY_READ, &k))
            continue;
        WCHAR appid[64] = {0};
        BOOL found = FALSE;
        memset(s, 0, sizeof(*s));
        if (read_text(k, nullptr, L"AppID", appid, RTL_NUMBER_OF(appid))) {
            WCHAR a[128];
            _snwprintf(a, RTL_NUMBER_OF(a), L"Software\\Classes\\AppID\\%ls", appid);
            found = read_text(HKEY_LOCAL_MACHINE, a, L"LocalService", s->Service, RTL_NUMBER_OF(s->Service));
            if (found)
                read_text(HKEY_LOCAL_MACHINE, a, L"ServiceParameters", s->Parameters, RTL_NUMBER_OF(s->Parameters));
        }
        if (!found)
            found = read_text(k, L"LocalServer32", nullptr, s->Command, RTL_NUMBER_OF(s->Command)) ||
                    read_text(k, L"LocalServer", nullptr, s->Command, RTL_NUMBER_OF(s->Command));
        RegCloseKey(k);
        if (found)
            return TRUE;
    }
    return FALSE;
}

/*
 * the server that serves a class: the view of the client's kind first unless the activation asks for one, as a class
 * only registered by a program of the other kind is still served by it.
 */
static BOOL find_server(handle_t binding, const GUID *clsid, ULONG clsctx, DWORD pid, SERVER *s)
{
    WCHAR text[64];
    clsid_text(clsid, text, RTL_NUMBER_OF(text));
    BOOL wow = FALSE;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (process) {
        IsWow64Process(process, &wow);
        CloseHandle(process);
    }
    /* the classes of 32-bit programs, under WOW6432Node, are the view of a 32-bit client. */
    BOOL views[2] = {wow, !wow};
    ULONG count = 2;
    if (clsctx & CLSCTX_ACTIVATE_32_BIT_SERVER_)
        views[0] = TRUE, count = 1;
    else if (clsctx & CLSCTX_ACTIVATE_64_BIT_SERVER_)
        views[0] = FALSE, count = 1;
    HKEY user = nullptr;
    BOOL impersonating = RpcImpersonateClient(binding) == RPC_S_OK;
    if (impersonating)
        RegOpenCurrentUser(KEY_READ, &user);
    BOOL found = FALSE;
    for (ULONG i = 0; i < count && !found; i++)
        found = server_in_view(text, views[i], user, s);
    if (user)
        RegCloseKey(user);
    if (impersonating)
        RpcRevertToSelf();
    return found;
}

/* the class registered by the server that was started, or none once it ended or the time ran out. */
static BOOL wait_registration(const GUID *clsid, HANDLE process, RS_CLASS *out)
{
    for (ULONG waited = 0; waited < LAUNCH_WAIT_MS; waited += 25) {
        if (RsScmFindClass(clsid, out))
            return TRUE;
        if (process && !WaitForSingleObject(process, 25))
            return RsScmFindClass(clsid, out);
        if (!process)
            Sleep(25);
    }
    return FALSE;
}

/* the activation goes on in the apartment that registered the class, through its activator. */
static HRESULT forward(handle_t client, const RS_CLASS *c, BOOL create, MInterfacePointer *outer,
                       MInterfacePointer *props, MInterfacePointer **out)
{
    WCHAR text[160];
    if (!RsOxidBinding(c->Oxid, text, RTL_NUMBER_OF(text)))
        return RPC_E_DISCONNECTED_;
    RPC_BINDING_HANDLE b = nullptr;
    RPC_STATUS status = RpcBindingFromStringBindingW((RPC_WSTR)text, &b);
    UUID ipid = c->Ipid;
    if (!status)
        status = RpcBindingSetObject(b, &ipid);
    /* the server's access check sees the client: rpcss calls as the client it activates for. */
    RPC_SECURITY_QOS qos = {RPC_C_SECURITY_QOS_VERSION, RPC_C_QOS_CAPABILITIES_DEFAULT_, RPC_C_QOS_IDENTITY_DYNAMIC_,
                            RPC_C_IMP_LEVEL_IMPERSONATE};
    if (!status)
        status = RpcBindingSetAuthInfoExW(b, nullptr, RPC_C_AUTHN_LEVEL_PKT_PRIVACY, RPC_C_AUTHN_WINNT, nullptr,
                                          RPC_C_AUTHZ_NONE, &qos);
    HRESULT hr = status ? HRESULT_FROM_WIN32(status) : S_OK;
    if (!status) {
        ORPCTHIS orpcthis = {{5, 7}, ORPCF_LOCAL, 0, {}, nullptr};
        UuidCreate((UUID *)&orpcthis.cid);
        LOCALTHIS localthis = {};
        localthis.dwClientThread = GetCurrentThreadId();
        UuidCreate((UUID *)&localthis.callId);
        ORPCTHAT orpcthat = {};
        LOCALTHAT localthat = {};
        BOOL impersonating = RpcImpersonateClient(client) == RPC_S_OK;
        __try {
            hr = create ? ILocalSystemActivator_CreateInstance(b, &orpcthis, &localthis, &orpcthat, &localthat, outer,
                                                               props, out)
                        : ILocalSystemActivator_GetClassObject(b, &orpcthis, &localthis, &orpcthat, &localthat, props,
                                                               out);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            DWORD code = GetExceptionCode();
            hr = code == RPC_S_SERVER_UNAVAILABLE || code == RPC_S_CALL_FAILED_DNE ? RPC_E_DISCONNECTED_
                                                                                     : HRESULT_FROM_WIN32(code);
        }
        if (impersonating)
            RpcRevertToSelf();
    }
    if (b)
        RpcBindingFree(&b);
    return hr;
}

/*
 * an activation combase could not serve in its own process: a class a local server registered is served by it, one
 * no server registered yet gets its server started, a service or a program, which registers it.
 */
static long activate(handle_t binding, ORPCTHAT *orpcthat, LOCALTHAT *localthat, MInterfacePointer *outer,
                     MInterfacePointer *props, MInterfacePointer **out, BOOL create, const char *what)
{
    /* a machine local answer says ORPCF_LOCAL, the only flag combase takes for a normal one. */
    orpcthat->flags = 1;
    orpcthat->extensions = nullptr;
    memset(localthat, 0, sizeof(*localthat));
    *out = nullptr;
    ULONG pid = 0;
    if (I_RpcBindingInqLocalClientPID(binding, &pid) != RPC_S_OK || !pid)
        return E_ACCESSDENIED;
    GUID clsid;
    ULONG clsctx = 0, flags = 0;
    if (!props || !RsActivationRequest(props->abData, props->ulCntData, &clsid, &clsctx, &flags)) {
        log_request(what, props);
        return E_INVALIDARG;
    }
    RS_CLASS running;
    if (!RsScmFindClass(&clsid, &running)) {
        SERVER server;
        if (!(clsctx & CLSCTX_LOCAL_SERVER_) || !find_server(binding, &clsid, clsctx, pid, &server))
            return REGDB_E_CLASSNOTREG;
        HANDLE process = nullptr;
        HRESULT hr = RsLaunch(pid, server.Service[0] ? server.Service : nullptr, server.Parameters,
                              server.Service[0] ? nullptr : server.Command, &process);
        if (FAILED(hr))
            return hr;
        BOOL registered = wait_registration(&clsid, process, &running);
        if (process)
            CloseHandle(process);
        if (!registered) {
            RsLog("%s: the server of {%08lx-...} did not register it", what, clsid.Data1);
            return CO_E_SERVER_EXEC_FAILURE;
        }
    }
    HRESULT hr = forward(binding, &running, create, outer, props, out);
    /* a server that went away without revoking its class is started again the next time. */
    if (hr == RPC_E_DISCONNECTED_) {
        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, running.Pid);
        if (!process || !WaitForSingleObject(process, 0))
            RsProcessGone(running.Pid);
        if (process)
            CloseHandle(process);
    }
    return hr;
}

long WINAPI ISCMLocalActivator_GetClassObject_s(handle_t hRpc, ORPCTHIS *orpcthis, LOCALTHIS *localthis,
                                                ORPCTHAT *orpcthat, LOCALTHAT *localthat,
                                                MInterfacePointer *pActProperties, MInterfacePointer **ppActProperties)
{
    return activate(hRpc, orpcthat, localthat, nullptr, pActProperties, ppActProperties, FALSE, "GetClassObject");
}

long WINAPI ISCMLocalActivator_CreateInstance_s(handle_t hRpc, ORPCTHIS *orpcthis, LOCALTHIS *localthis,
                                                ORPCTHAT *orpcthat, LOCALTHAT *localthat, MInterfacePointer *pUnkOuter,
                                                MInterfacePointer *pActProperties, MInterfacePointer **ppActProperties)
{
    return activate(hRpc, orpcthat, localthat, pUnkOuter, pActProperties, ppActProperties, TRUE, "CreateInstance");
}

RPC_STATUS RsStartScm(void)
{
    RPC_STATUS status = RsRegisterInterface(ISCM_s_ifspec);
    if (!status)
        status = RsRegisterInterface(ISCMLocalActivator_s_ifspec);
    return status;
}
