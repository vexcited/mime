#include "rpcss.h"
#include "rpcss/irot.h"

#define MK_S_MONIKERALREADYREGISTERED 0x000401E7
#define MK_E_UNAVAILABLE 0x800401E3

/*
 * the running object table: what CoGetRunningObjectTable registers, by the comparison data of the moniker. the object
 * and its name are the marshaled interfaces combase hands in; rpcss only keeps and returns them.
 */
typedef struct RS_ROT_ENTRY {
    struct RS_ROT_ENTRY *Next;
    SCMREGKEY Key;
    DWORD Pid;
    ULONG Flags;
    RPCSS_FILETIME Time;
    WCHAR *Desktop;
    InterfaceData Object, Name;
    ULONG MonikerSize;
    BYTE Moniker[];
} RS_ROT_ENTRY;

static SRWLOCK g_rot_lock = SRWLOCK_INIT;
static RS_ROT_ENTRY *g_rot;
static UINT64 g_next_location = 1;
static ULONG g_scm_id;

static ULONG moniker_hash(const MnkEqBuf *m)
{
    ULONG h = 0;
    for (ULONG i = 0; i < m->cdwSize; i++)
        h = h * 31 + m->abEqData[i];
    return h;
}

static BOOL same_desktop(const WCHAR *a, const unsigned short *b)
{
    if (!a || !b)
        return TRUE;
    return !_wcsicmp(a, (const WCHAR *)b);
}

static RS_ROT_ENTRY *find_moniker(const unsigned short *desktop, const MnkEqBuf *m)
{
    for (RS_ROT_ENTRY *e = g_rot; e; e = e->Next)
        if (e->MonikerSize == m->cdwSize && !memcmp(e->Moniker, m->abEqData, m->cdwSize) &&
            same_desktop(e->Desktop, desktop))
            return e;
    return nullptr;
}

static BOOL same_key(const SCMREGKEY *a, const SCMREGKEY *b)
{
    return a->dwEntryLoc == b->dwEntryLoc && a->dwHash == b->dwHash && a->dwScmId == b->dwScmId;
}

static RS_ROT_ENTRY *find_key(const SCMREGKEY *key)
{
    for (RS_ROT_ENTRY *e = g_rot; e; e = e->Next)
        if (same_key(&e->Key, key))
            return e;
    return nullptr;
}

static InterfaceData *ifd_copy(const InterfaceData *d)
{
    InterfaceData *c = MIDL_user_allocate(sizeof(*c));
    if (c)
        memcpy(c, d, sizeof(*c));
    return c;
}

static void entry_free(RS_ROT_ENTRY *e)
{
    RsFree(e->Desktop);
    RsFree(e);
}

void RsRotProcessGone(DWORD pid)
{
    AcquireSRWLockExclusive(&g_rot_lock);
    for (RS_ROT_ENTRY **q = &g_rot; *q;) {
        RS_ROT_ENTRY *e = *q;
        if (e->Pid == pid) {
            *q = e->Next;
            entry_free(e);
        } else {
            q = &e->Next;
        }
    }
    ReleaseSRWLockExclusive(&g_rot_lock);
}

long WINAPI IROT_IrotRegister_s(handle_t hRpc, PHPROCESS phProcess, unsigned short *pwszWinstaDesktop,
                                MnkEqBuf *pmkeqbuf, InterfaceData *pifdObject, InterfaceData *pifdObjectName,
                                RPCSS_FILETIME *pfiletime, unsigned long dwProcessID, unsigned long grfFlags,
                                unsigned long cPackages, RPCSS_HSTRING *packages, SCMREGKEY *psrkRegister,
                                error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    memset(psrkRegister, 0, sizeof(*psrkRegister));
    DWORD pid = RsCallerOf(hRpc, phProcess);
    if (!pid)
        return E_ACCESSDENIED;
    RS_ROT_ENTRY *e = RsAllocZero(FIELD_OFFSET(RS_ROT_ENTRY, Moniker) + pmkeqbuf->cdwSize);
    if (!e)
        return E_OUTOFMEMORY;
    e->Pid = pid;
    e->Flags = grfFlags;
    e->Time = *pfiletime;
    e->Desktop = RsDupString((const WCHAR *)pwszWinstaDesktop);
    e->Object = *pifdObject;
    e->Name = *pifdObjectName;
    e->MonikerSize = pmkeqbuf->cdwSize;
    memcpy(e->Moniker, pmkeqbuf->abEqData, pmkeqbuf->cdwSize);
    AcquireSRWLockExclusive(&g_rot_lock);
    HRESULT hr = find_moniker(pwszWinstaDesktop, pmkeqbuf) ? MK_S_MONIKERALREADYREGISTERED : S_OK;
    e->Key.dwEntryLoc = g_next_location++;
    e->Key.dwHash = moniker_hash(pmkeqbuf);
    e->Key.dwScmId = g_scm_id;
    e->Next = g_rot;
    g_rot = e;
    *psrkRegister = e->Key;
    ReleaseSRWLockExclusive(&g_rot_lock);
    return hr;
}

long WINAPI IROT_IrotRevoke_s(handle_t hRpc, PHPROCESS phProcess, SCMREGKEY *psrkRegister, InterfaceData **ppifdObject,
                              InterfaceData **ppifdName, unsigned long *pfServerOnly, error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    *ppifdObject = *ppifdName = nullptr;
    *pfServerOnly = FALSE;
    DWORD pid = RsCallerOf(hRpc, phProcess);
    if (!pid)
        return E_ACCESSDENIED;
    HRESULT hr = E_INVALIDARG;
    AcquireSRWLockExclusive(&g_rot_lock);
    for (RS_ROT_ENTRY **q = &g_rot; *q; q = &(*q)->Next) {
        RS_ROT_ENTRY *e = *q;
        if (!same_key(&e->Key, psrkRegister))
            continue;
        if (e->Pid != pid) {
            hr = E_ACCESSDENIED;
            break;
        }
        *q = e->Next;
        *ppifdObject = ifd_copy(&e->Object);
        *ppifdName = ifd_copy(&e->Name);
        entry_free(e);
        hr = S_OK;
        break;
    }
    ReleaseSRWLockExclusive(&g_rot_lock);
    return hr;
}

long WINAPI IROT_IrotIsRunning_s(handle_t hRpc, PHPROCESS phProcess, unsigned short *pwszWinstaDesktop,
                                 MnkEqBuf *pmkeqbuf, error_status_t *prpcstat, unsigned long bTrustedOnly)
{
    *prpcstat = RPC_S_OK;
    if (!RsCallerOf(hRpc, phProcess))
        return E_ACCESSDENIED;
    AcquireSRWLockShared(&g_rot_lock);
    HRESULT hr = find_moniker(pwszWinstaDesktop, pmkeqbuf) ? S_OK : S_FALSE;
    ReleaseSRWLockShared(&g_rot_lock);
    return hr;
}

long WINAPI IROT_IrotGetObject_s(handle_t hRpc, PHPROCESS phProcess, unsigned short *pwszWinstaDesktop,
                                 unsigned long dwProcessID, MnkEqBuf *pmkeqbuf, SCMREGKEY *psrkRegister,
                                 InterfaceData **ppifdObject, unsigned long *pfRegisteredByAppContainer,
                                 unsigned long *pRegisteringPid, error_status_t *prpcstat, unsigned long bTrustedOnly)
{
    *prpcstat = RPC_S_OK;
    memset(psrkRegister, 0, sizeof(*psrkRegister));
    *ppifdObject = nullptr;
    *pfRegisteredByAppContainer = FALSE;
    *pRegisteringPid = 0;
    if (!RsCallerOf(hRpc, phProcess))
        return E_ACCESSDENIED;
    HRESULT hr = MK_E_UNAVAILABLE;
    AcquireSRWLockShared(&g_rot_lock);
    RS_ROT_ENTRY *e = find_moniker(pwszWinstaDesktop, pmkeqbuf);
    if (e) {
        *psrkRegister = e->Key;
        *pRegisteringPid = e->Pid;
        hr = (*ppifdObject = ifd_copy(&e->Object)) ? S_OK : E_OUTOFMEMORY;
    }
    ReleaseSRWLockShared(&g_rot_lock);
    return hr;
}

long WINAPI IROT_IrotNoteChangeTime_s(handle_t hRpc, PHPROCESS phProcess, SCMREGKEY *psrkRegister,
                                      RPCSS_FILETIME *pfiletime, error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    if (!RsCallerOf(hRpc, phProcess))
        return E_ACCESSDENIED;
    AcquireSRWLockExclusive(&g_rot_lock);
    RS_ROT_ENTRY *e = find_key(psrkRegister);
    if (e)
        e->Time = *pfiletime;
    ReleaseSRWLockExclusive(&g_rot_lock);
    return e ? S_OK : E_INVALIDARG;
}

long WINAPI IROT_IrotGetTimeOfLastChange_s(handle_t hRpc, PHPROCESS phProcess, unsigned short *pwszWinstaDesktop,
                                           MnkEqBuf *pmkeqbuf, RPCSS_FILETIME *pfiletime, error_status_t *prpcstat,
                                           unsigned long bTrustedOnly)
{
    *prpcstat = RPC_S_OK;
    memset(pfiletime, 0, sizeof(*pfiletime));
    if (!RsCallerOf(hRpc, phProcess))
        return E_ACCESSDENIED;
    AcquireSRWLockShared(&g_rot_lock);
    RS_ROT_ENTRY *e = find_moniker(pwszWinstaDesktop, pmkeqbuf);
    if (e)
        *pfiletime = e->Time;
    ReleaseSRWLockShared(&g_rot_lock);
    return e ? S_OK : MK_E_UNAVAILABLE;
}

long WINAPI IROT_IrotEnumRunning_s(handle_t hRpc, PHPROCESS phProcess, unsigned short *pwszWinstaDesktop,
                                   MkInterfaceList **ppMkIFList, error_status_t *prpcstat, unsigned long bTrustedOnly)
{
    *prpcstat = RPC_S_OK;
    *ppMkIFList = nullptr;
    if (!RsCallerOf(hRpc, phProcess))
        return E_ACCESSDENIED;
    AcquireSRWLockShared(&g_rot_lock);
    ULONG count = 0;
    for (RS_ROT_ENTRY *e = g_rot; e; e = e->Next)
        count += same_desktop(e->Desktop, pwszWinstaDesktop);
    MkInterfaceList *list = MIDL_user_allocate(FIELD_OFFSET(MkInterfaceList, apIFDList) +
                                               count * sizeof(MkInterfaceListEntry));
    if (list) {
        list->dwSize = 0;
        for (RS_ROT_ENTRY *e = g_rot; e; e = e->Next) {
            if (!same_desktop(e->Desktop, pwszWinstaDesktop))
                continue;
            MkInterfaceListEntry *x = &list->apIFDList[list->dwSize++];
            x->pIFD = ifd_copy(&e->Name);
            x->dwHash = e->Key.dwHash;
        }
    }
    ReleaseSRWLockShared(&g_rot_lock);
    *ppMkIFList = list;
    return list ? S_OK : E_OUTOFMEMORY;
}

/*
 * the table of objects registered by class for packaged and activation based callers. mime has no packaged process, and
 * these four are not served yet.
 */
long WINAPI IROT_IMgotRegister_s(handle_t hRpc, PHPROCESS phProcess, MGOT_SECURITY_MODEL securityModel,
                                 unsigned short *pwszWinstaDesktop, GUID *clsid, unsigned long cPackages,
                                 RPCSS_HSTRING *packages, MnkEqBuf *pmkeqbuf, InterfaceData *pifdObject,
                                 unsigned long dwFlags, MgotScmToken *pToken, error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    memset(pToken, 0, sizeof(*pToken));
    RsLog("IROT IMgotRegister is not served");
    return E_NOTIMPL;
}

long WINAPI IROT_IMgotRevoke_s(handle_t hRpc, PHPROCESS phProcess, MGOT_SECURITY_MODEL securityModel,
                               MgotScmToken *pToken, InterfaceData **ppifdObject, unsigned long *pfServerOnly,
                               error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    *ppifdObject = nullptr;
    *pfServerOnly = FALSE;
    RsLog("IROT IMgotRevoke is not served");
    return E_NOTIMPL;
}

long WINAPI IROT_IMgotGetObject_s(handle_t hRpc, PHPROCESS phProcess, MGOT_SECURITY_MODEL securityModel,
                                  unsigned short *pwszWinstaDesktop, GUID *clsid, unsigned long cPackages,
                                  RPCSS_HSTRING *packages, unsigned long cMains, RPCSS_HSTRING *mains,
                                  MnkEqBuf *pmkeqbuf, InterfaceData **ppifdObject,
                                  unsigned long *pfRegisteredByAppContainer, unsigned long *pRegisteringPid,
                                  error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    *ppifdObject = nullptr;
    *pfRegisteredByAppContainer = FALSE;
    *pRegisteringPid = 0;
    RsLog("IROT IMgotGetObject is not served");
    return E_NOTIMPL;
}

long WINAPI IROT_IMgotEnumRunning_s(handle_t hRpc, PHPROCESS phProcess, unsigned short *pwszWinstaDesktop, GUID *clsid,
                                    unsigned long cPackages, RPCSS_HSTRING *packages, unsigned long cMains,
                                    RPCSS_HSTRING *mains, MgotNameList **ppNames, error_status_t *prpcstat)
{
    *prpcstat = RPC_S_OK;
    *ppNames = nullptr;
    RsLog("IROT IMgotEnumRunning is not served");
    return E_NOTIMPL;
}

RPC_STATUS RsStartRot(void)
{
    g_scm_id = GetCurrentProcessId();
    return RsRegisterInterface(IROT_s_ifspec);
}
