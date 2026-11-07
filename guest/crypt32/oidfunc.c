#include "crypt32_private.h"

#define OID_KEY L"SOFTWARE\\Microsoft\\Cryptography\\OID"
#define DLL_MAGIC 0x4C4C444F

/* a function a module installed for an encoding and an object identifier, or an integer id. */
typedef struct installed {
    struct installed *next;
    DWORD encoding;
    LPSTR oid;
    ULONG_PTR id;
    void *address;
} installed;

typedef struct func_set {
    struct func_set *next;
    LPSTR name;
    installed *functions;
} func_set;

/* the handle of a function found in the registry or a default library: the library stays loaded. */
typedef struct {
    DWORD magic;
    HMODULE module;
} dll_ref;

static SRWLOCK lock = SRWLOCK_INIT;
static func_set *sets;

static LPSTR copy_string(LPCSTR s)
{
    SIZE_T n = strlen(s) + 1;
    LPSTR copy = CrAlloc(n);
    if (copy)
        memcpy(copy, s, n);
    return copy;
}

/* an encoding names its key by the certificate encoding, or by the message encoding when there is none. */
static DWORD key_encoding(DWORD encoding)
{
    DWORD cert = GET_CERT_ENCODING_TYPE(encoding);
    return cert ? cert : encoding >> 16;
}

static BOOL same_oid(const installed *f, LPCSTR oid)
{
    if (IS_INTRESOURCE(oid))
        return !f->oid && f->id == (ULONG_PTR)oid;
    return f->oid && !_stricmp(f->oid, oid);
}

HCRYPTOIDFUNCSET WINAPI CryptInitOIDFunctionSet(LPCSTR pszFuncName, DWORD dwFlags)
{
    if (!pszFuncName) {
        SetLastError(E_INVALIDARG);
        return nullptr;
    }
    AcquireSRWLockExclusive(&lock);
    func_set *set = sets;
    while (set && strcmp(set->name, pszFuncName))
        set = set->next;
    if (!set && (set = CrAllocZero(sizeof(*set)))) {
        if ((set->name = copy_string(pszFuncName))) {
            set->next = sets;
            sets = set;
        } else {
            CrFree(set);
            set = nullptr;
        }
    }
    ReleaseSRWLockExclusive(&lock);
    if (!set)
        SetLastError(ERROR_OUTOFMEMORY);
    return set;
}

BOOL WINAPI CryptInstallOIDFunctionAddress(HMODULE hModule, DWORD dwEncodingType, LPCSTR pszFuncName,
                                           DWORD cFuncEntry, const CRYPT_OID_FUNC_ENTRY rgFuncEntry[], DWORD dwFlags)
{
    func_set *set = CryptInitOIDFunctionSet(pszFuncName, 0);
    if (!set)
        return FALSE;
    for (DWORD i = 0; i < cFuncEntry; i++) {
        installed *f = CrAllocZero(sizeof(*f));
        if (!f)
            return CrFail(ERROR_OUTOFMEMORY);
        f->encoding = key_encoding(dwEncodingType);
        f->address = rgFuncEntry[i].pvFuncAddr;
        if (IS_INTRESOURCE(rgFuncEntry[i].pszOID))
            f->id = (ULONG_PTR)rgFuncEntry[i].pszOID;
        else if (!(f->oid = copy_string(rgFuncEntry[i].pszOID))) {
            CrFree(f);
            return CrFail(ERROR_OUTOFMEMORY);
        }
        AcquireSRWLockExclusive(&lock);
        if (dwFlags & CRYPT_INSTALL_OID_FUNC_BEFORE_FLAG || !set->functions) {
            f->next = set->functions;
            set->functions = f;
        } else {
            installed *last = set->functions;
            while (last->next)
                last = last->next;
            last->next = f;
        }
        ReleaseSRWLockExclusive(&lock);
    }
    return TRUE;
}

/* the key of a function for an object identifier, as "#<n>" for an integer one. */
static HKEY open_function_key(DWORD encoding, LPCSTR func, LPCSTR oid, BOOL create)
{
    WCHAR path[512];
    int n;
    if (IS_INTRESOURCE(oid))
        n = _snwprintf(path, RTL_NUMBER_OF(path), OID_KEY L"\\EncodingType %u\\%hs\\#%u", key_encoding(encoding), func,
                       (DWORD)(ULONG_PTR)oid);
    else
        n = _snwprintf(path, RTL_NUMBER_OF(path), OID_KEY L"\\EncodingType %u\\%hs\\%hs", key_encoding(encoding), func,
                       oid);
    if (n < 0)
        return nullptr;
    HKEY key = nullptr;
    LSTATUS error = create ? RegCreateKeyExW(HKEY_LOCAL_MACHINE, path, 0, nullptr, 0, KEY_READ | KEY_WRITE, nullptr,
                                             &key, nullptr)
                           : RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ, &key);
    if (error) {
        SetLastError(error);
        return nullptr;
    }
    return key;
}

static dll_ref *load_dll(LPCWSTR name)
{
    HMODULE module = LoadLibraryW(name);
    if (!module)
        return nullptr;
    dll_ref *ref = CrAlloc(sizeof(*ref));
    if (!ref) {
        FreeLibrary(module);
        SetLastError(ERROR_OUTOFMEMORY);
        return nullptr;
    }
    ref->magic = DLL_MAGIC;
    ref->module = module;
    return ref;
}

/* a registered function: the Dll value of its key, and its FuncName value or the name of the set. */
static BOOL registered_function(func_set *set, DWORD encoding, LPCSTR oid, void **address, HCRYPTOIDFUNCADDR *handle)
{
    HKEY key = open_function_key(encoding, set->name, oid, FALSE);
    if (!key)
        return FALSE;
    WCHAR dll[MAX_PATH];
    char name[256];
    DWORD type, size = sizeof(dll) - sizeof(WCHAR);
    LSTATUS error = RegQueryValueExW(key, L"Dll", nullptr, &type, (BYTE *)dll, &size);
    if (!error && type != REG_SZ && type != REG_EXPAND_SZ && type != REG_MULTI_SZ)
        error = ERROR_FILE_NOT_FOUND;
    dll[size / sizeof(WCHAR)] = 0;
    LPCSTR func = set->name;
    size = sizeof(name) - 1;
    if (!error && !RegQueryValueExA(key, "FuncName", nullptr, &type, (BYTE *)name, &size) && type == REG_SZ) {
        name[size] = 0;
        func = name;
    }
    RegCloseKey(key);
    if (error)
        return CrFail(ERROR_FILE_NOT_FOUND);
    WCHAR expanded[MAX_PATH];
    if (ExpandEnvironmentStringsW(dll, expanded, RTL_NUMBER_OF(expanded)))
        memcpy(dll, expanded, sizeof(dll));
    dll_ref *ref = load_dll(dll);
    if (!ref)
        return FALSE;
    *address = GetProcAddress(ref->module, func);
    if (!*address) {
        CrFree(ref);
        return CrFail(ERROR_FILE_NOT_FOUND);
    }
    *handle = ref;
    return TRUE;
}

BOOL WINAPI CryptGetOIDFunctionAddress(HCRYPTOIDFUNCSET hFuncSet, DWORD dwEncodingType, LPCSTR pszOID, DWORD dwFlags,
                                       void **ppvFuncAddr, HCRYPTOIDFUNCADDR *phFuncAddr)
{
    func_set *set = hFuncSet;
    *ppvFuncAddr = nullptr;
    *phFuncAddr = nullptr;
    if (!set || !pszOID)
        return CrFail(E_INVALIDARG);
    DWORD encoding = key_encoding(dwEncodingType);
    AcquireSRWLockShared(&lock);
    for (installed *f = set->functions; f; f = f->next)
        if (f->encoding == encoding && same_oid(f, pszOID)) {
            *ppvFuncAddr = f->address;
            *phFuncAddr = f;
            break;
        }
    ReleaseSRWLockShared(&lock);
    if (*ppvFuncAddr)
        return TRUE;
    if (!(dwFlags & CRYPT_GET_INSTALLED_OID_FUNC_FLAG) &&
        registered_function(set, dwEncodingType, pszOID, ppvFuncAddr, phFuncAddr))
        return TRUE;
    return CrFail(ERROR_FILE_NOT_FOUND);
}

BOOL WINAPI CryptFreeOIDFunctionAddress(HCRYPTOIDFUNCADDR hFuncAddr, DWORD dwFlags)
{
    dll_ref *ref = hFuncAddr;
    /* a library stays loaded, a provider may still run on another thread. */
    if (ref && ref->magic == DLL_MAGIC) {
        ref->magic = 0;
        CrFree(ref);
    }
    return TRUE;
}

/* the Dll list of a function's DEFAULT key: names separated and ended by a null, as REG_MULTI_SZ. */
static WCHAR *default_dlls(LPCSTR func, DWORD encoding, DWORD *chars)
{
    *chars = 0;
    HKEY key = open_function_key(encoding, func, CRYPT_DEFAULT_OID, FALSE);
    if (!key)
        return nullptr;
    DWORD type, size = 0;
    WCHAR *list = nullptr;
    if (!RegQueryValueExW(key, L"Dll", nullptr, &type, nullptr, &size) && type == REG_MULTI_SZ && size &&
        (list = CrAllocZero(size + 2 * sizeof(WCHAR)))) {
        if (RegQueryValueExW(key, L"Dll", nullptr, &type, (BYTE *)list, &size)) {
            CrFree(list);
            list = nullptr;
        } else {
            DWORD n = size / sizeof(WCHAR);
            while (n && !list[n - 1])
                n--;
            *chars = n + 2;
        }
    }
    RegCloseKey(key);
    return list;
}

BOOL WINAPI CryptGetDefaultOIDDllList(HCRYPTOIDFUNCSET hFuncSet, DWORD dwEncodingType, WCHAR *pwszDllList,
                                      DWORD *pcchDllList)
{
    func_set *set = hFuncSet;
    if (!set || !pcchDllList)
        return CrFail(E_INVALIDARG);
    DWORD chars;
    WCHAR *list = default_dlls(set->name, dwEncodingType, &chars);
    static const WCHAR empty[2] = {};
    const WCHAR *source = list ? list : empty;
    if (!list)
        chars = 1;
    BOOL ok = TRUE;
    if (pwszDllList) {
        if (*pcchDllList < chars)
            ok = CrFail(ERROR_MORE_DATA);
        else
            memcpy(pwszDllList, source, chars * sizeof(WCHAR));
    }
    *pcchDllList = chars;
    CrFree(list);
    return ok;
}

BOOL WINAPI CryptGetDefaultOIDFunctionAddress(HCRYPTOIDFUNCSET hFuncSet, DWORD dwEncodingType, LPCWSTR pwszDll,
                                              DWORD dwFlags, void **ppvFuncAddr, HCRYPTOIDFUNCADDR *phFuncAddr)
{
    func_set *set = hFuncSet;
    if (!set || !ppvFuncAddr || !phFuncAddr)
        return CrFail(E_INVALIDARG);
    *ppvFuncAddr = nullptr;
    if (!pwszDll) {
        /* the installed defaults, one after the other as the previous handle says. */
        installed *prev = *phFuncAddr;
        DWORD encoding = key_encoding(dwEncodingType);
        AcquireSRWLockShared(&lock);
        installed *f = prev ? prev->next : set->functions;
        while (f && !(f->encoding == encoding && same_oid(f, CRYPT_DEFAULT_OID)))
            f = f->next;
        ReleaseSRWLockShared(&lock);
        *phFuncAddr = f;
        if (!f)
            return CrFail(ERROR_FILE_NOT_FOUND);
        *ppvFuncAddr = f->address;
        return TRUE;
    }
    CryptFreeOIDFunctionAddress(*phFuncAddr, 0);
    *phFuncAddr = nullptr;
    dll_ref *ref = load_dll(pwszDll);
    if (!ref)
        return FALSE;
    *ppvFuncAddr = GetProcAddress(ref->module, set->name);
    if (!*ppvFuncAddr) {
        CrFree(ref);
        return CrFail(ERROR_FILE_NOT_FOUND);
    }
    *phFuncAddr = ref;
    return TRUE;
}

/* ------------------------------------------------------------------ registration */

BOOL WINAPI CryptRegisterOIDFunction(DWORD dwEncodingType, LPCSTR pszFuncName, LPCSTR pszOID, LPCWSTR pwszDll,
                                     LPCSTR pszOverrideFuncName)
{
    if (!pszFuncName || !pszOID)
        return CrFail(E_INVALIDARG);
    if (!GET_CERT_ENCODING_TYPE(dwEncodingType))
        return TRUE;
    HKEY key = open_function_key(dwEncodingType, pszFuncName, pszOID, TRUE);
    if (!key)
        return FALSE;
    LSTATUS error = 0;
    if (pwszDll)
        error = RegSetValueExW(key, L"Dll", 0, REG_SZ, (const BYTE *)pwszDll,
                               ((DWORD)wcslen(pwszDll) + 1) * sizeof(WCHAR));
    if (!error && pszOverrideFuncName)
        error = RegSetValueExA(key, "FuncName", 0, REG_SZ, (const BYTE *)pszOverrideFuncName,
                               (DWORD)strlen(pszOverrideFuncName) + 1);
    RegCloseKey(key);
    return error ? CrFail(error) : TRUE;
}

BOOL WINAPI CryptUnregisterOIDFunction(DWORD dwEncodingType, LPCSTR pszFuncName, LPCSTR pszOID)
{
    if (!pszFuncName || !pszOID)
        return CrFail(E_INVALIDARG);
    if (!GET_CERT_ENCODING_TYPE(dwEncodingType))
        return TRUE;
    WCHAR path[512], name[64];
    _snwprintf(path, RTL_NUMBER_OF(path), OID_KEY L"\\EncodingType %u\\%hs", key_encoding(dwEncodingType),
               pszFuncName);
    if (IS_INTRESOURCE(pszOID))
        _snwprintf(name, RTL_NUMBER_OF(name), L"#%u", (DWORD)(ULONG_PTR)pszOID);
    else
        _snwprintf(name, RTL_NUMBER_OF(name), L"%hs", pszOID);
    HKEY key;
    LSTATUS error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_READ | KEY_WRITE, &key);
    if (!error) {
        error = RegDeleteKeyW(key, name);
        RegCloseKey(key);
    }
    return error ? CrFail(error) : TRUE;
}

/* a library joins the DEFAULT list at an index, once. */
BOOL WINAPI CryptRegisterDefaultOIDFunction(DWORD dwEncodingType, LPCSTR pszFuncName, DWORD dwIndex, LPCWSTR pwszDll)
{
    if (!pszFuncName || !pwszDll)
        return CrFail(E_INVALIDARG);
    if (!GET_CERT_ENCODING_TYPE(dwEncodingType))
        return CrFail(E_INVALIDARG);
    DWORD chars;
    WCHAR *list = default_dlls(pszFuncName, dwEncodingType, &chars);
    DWORD add = (DWORD)wcslen(pwszDll) + 1;
    WCHAR *out = CrAllocZero((chars + add + 2) * sizeof(WCHAR));
    if (!out) {
        CrFree(list);
        return CrFail(ERROR_OUTOFMEMORY);
    }
    DWORD used = 0, index = 0;
    BOOL placed = FALSE;
    for (WCHAR *p = list; p && *p; p += wcslen(p) + 1, index++) {
        if (!_wcsicmp(p, pwszDll)) {
            CrFree(list);
            CrFree(out);
            return CrFail(ERROR_FILE_EXISTS);
        }
        if (index == dwIndex) {
            memcpy(out + used, pwszDll, add * sizeof(WCHAR));
            used += add;
            placed = TRUE;
        }
        DWORD n = (DWORD)wcslen(p) + 1;
        memcpy(out + used, p, n * sizeof(WCHAR));
        used += n;
    }
    if (!placed) {
        memcpy(out + used, pwszDll, add * sizeof(WCHAR));
        used += add;
    }
    out[used++] = 0;
    CrFree(list);
    HKEY key = open_function_key(dwEncodingType, pszFuncName, CRYPT_DEFAULT_OID, TRUE);
    LSTATUS error = key ? RegSetValueExW(key, L"Dll", 0, REG_MULTI_SZ, (const BYTE *)out, used * sizeof(WCHAR))
                        : (LSTATUS)GetLastError();
    if (key)
        RegCloseKey(key);
    CrFree(out);
    return error ? CrFail(error) : TRUE;
}

BOOL WINAPI CryptUnregisterDefaultOIDFunction(DWORD dwEncodingType, LPCSTR pszFuncName, LPCWSTR pwszDll)
{
    if (!pszFuncName || !pwszDll)
        return CrFail(E_INVALIDARG);
    DWORD chars;
    WCHAR *list = default_dlls(pszFuncName, dwEncodingType, &chars);
    if (!list)
        return CrFail(ERROR_FILE_NOT_FOUND);
    WCHAR *out = CrAllocZero((chars + 2) * sizeof(WCHAR));
    DWORD used = 0;
    BOOL found = FALSE;
    for (WCHAR *p = list; out && *p; p += wcslen(p) + 1) {
        DWORD n = (DWORD)wcslen(p) + 1;
        if (!_wcsicmp(p, pwszDll)) {
            found = TRUE;
            continue;
        }
        memcpy(out + used, p, n * sizeof(WCHAR));
        used += n;
    }
    CrFree(list);
    if (!out)
        return CrFail(ERROR_OUTOFMEMORY);
    LSTATUS error = found ? 0 : ERROR_FILE_NOT_FOUND;
    if (found) {
        out[used++] = 0;
        HKEY key = open_function_key(dwEncodingType, pszFuncName, CRYPT_DEFAULT_OID, TRUE);
        error = key ? RegSetValueExW(key, L"Dll", 0, REG_MULTI_SZ, (const BYTE *)out, used * sizeof(WCHAR))
                    : (LSTATUS)GetLastError();
        if (key)
            RegCloseKey(key);
    }
    CrFree(out);
    return error ? CrFail(error) : TRUE;
}

/* ------------------------------------------------------------------ revocation */

typedef BOOL(WINAPI *verify_fn)(DWORD, DWORD, DWORD, PVOID *, DWORD, PCERT_REVOCATION_PARA, PCERT_REVOCATION_STATUS);

typedef struct {
    DWORD error;
    DWORD reason;
    BOOL has_freshness;
    DWORD freshness;
} rev_result;

/*
 * calls one provider with a fresh status. TRUE when its answer is final: it verified every context, found one
 * revoked, or stopped at one past the first. a provider that could not tell leaves the error of the first that
 * had no dll or no check, so a later provider's reason replaces those two.
 */
static BOOL call_provider(verify_fn fn, DWORD encoding, DWORD type, DWORD count, PVOID *contexts, DWORD flags,
                          PCERT_REVOCATION_PARA para, const ULONGLONG *deadline, PCERT_REVOCATION_STATUS status,
                          rev_result *kept, BOOL *ok)
{
    DWORD size = status->cbSize;
    memset(status, 0, size);
    status->cbSize = size;
    if (deadline && para) {
        FILETIME now;
        GetSystemTimeAsFileTime(&now);
        ULONGLONG current = (ULONGLONG)now.dwHighDateTime << 32 | now.dwLowDateTime;
        DWORD left = current < *deadline ? (DWORD)((*deadline - current) / 10000) : 0;
        para->dwUrlRetrievalTimeout = left ? left : 1;
    }
    *ok = fn(encoding, type, count, contexts, flags, para, status);
    if (*ok || status->dwError == (DWORD)CRYPT_E_REVOKED || status->dwIndex)
        return TRUE;
    if (status->dwError && (kept->error == (DWORD)CRYPT_E_NO_REVOCATION_DLL ||
                            kept->error == (DWORD)CRYPT_E_NO_REVOCATION_CHECK)) {
        kept->error = status->dwError;
        kept->reason = status->dwReason;
        if (size >= sizeof(CERT_REVOCATION_STATUS)) {
            kept->has_freshness = status->fHasFreshnessTime;
            kept->freshness = status->dwFreshnessTime;
        }
    }
    return FALSE;
}

/* the installed defaults first, then the libraries the registry lists. */
static BOOL verify_default(DWORD encoding, DWORD type, DWORD count, PVOID *contexts, DWORD flags,
                           PCERT_REVOCATION_PARA para, const ULONGLONG *deadline, PCERT_REVOCATION_STATUS status)
{
    static func_set *set;
    if (!set && !(set = CryptInitOIDFunctionSet(CRYPT_OID_VERIFY_REVOCATION_FUNC, 0)))
        return FALSE;
    rev_result kept = {(DWORD)CRYPT_E_NO_REVOCATION_DLL};
    BOOL ok = FALSE;
    void *address;
    HCRYPTOIDFUNCADDR handle = nullptr;
    while (CryptGetDefaultOIDFunctionAddress(set, encoding, nullptr, 0, &address, &handle))
        if (call_provider(address, encoding, type, count, contexts, flags, para, deadline, status, &kept, &ok))
            return ok;
    DWORD chars;
    WCHAR *list = default_dlls(set->name, encoding, &chars);
    for (WCHAR *p = list; p && *p; p += wcslen(p) + 1) {
        handle = nullptr;
        if (!CryptGetDefaultOIDFunctionAddress(set, encoding, p, 0, &address, &handle))
            continue;
        BOOL final = call_provider(address, encoding, type, count, contexts, flags, para, deadline, status, &kept,
                                   &ok);
        DWORD error = GetLastError();
        CryptFreeOIDFunctionAddress(handle, 0);
        SetLastError(error);
        if (final) {
            CrFree(list);
            return ok;
        }
    }
    CrFree(list);
    DWORD size = status->cbSize;
    status->dwIndex = 0;
    status->dwError = kept.error;
    status->dwReason = kept.reason;
    if (size >= sizeof(CERT_REVOCATION_STATUS)) {
        status->fHasFreshnessTime = kept.has_freshness;
        status->dwFreshnessTime = kept.freshness;
    }
    return FALSE;
}

BOOL WINAPI CertVerifyRevocation(DWORD dwEncodingType, DWORD dwRevType, DWORD cContext, PVOID rgpvContext[],
                                 DWORD dwFlags, PCERT_REVOCATION_PARA pRevPara, PCERT_REVOCATION_STATUS pRevStatus)
{
    if (!pRevStatus || pRevStatus->cbSize < FIELD_OFFSET(CERT_REVOCATION_STATUS, fHasFreshnessTime))
        return CrFail(E_INVALIDARG);
    /* an accumulated timeout gives each provider what is left of the whole. */
    CERT_REVOCATION_PARA para = {};
    PCERT_REVOCATION_PARA used = pRevPara;
    ULONGLONG deadline, *limit = nullptr;
    if (dwFlags & CERT_VERIFY_REV_ACCUMULATIVE_TIMEOUT_FLAG) {
        if (pRevPara)
            memcpy(&para, pRevPara, pRevPara->cbSize < sizeof(para) ? pRevPara->cbSize : sizeof(para));
        para.cbSize = sizeof(para);
        if (para.dwUrlRetrievalTimeout) {
            FILETIME now;
            GetSystemTimeAsFileTime(&now);
            deadline = ((ULONGLONG)now.dwHighDateTime << 32 | now.dwLowDateTime) +
                       10000ULL * para.dwUrlRetrievalTimeout;
            limit = &deadline;
            used = &para;
        } else {
            dwFlags &= ~CERT_VERIFY_REV_ACCUMULATIVE_TIMEOUT_FLAG;
        }
    }
    DWORD done = 0;
    while (done < cContext) {
        if (verify_default(dwEncodingType, dwRevType, cContext - done, rgpvContext + done, dwFlags, used, limit,
                           pRevStatus))
            return TRUE;
        if (pRevStatus->dwError == (DWORD)CRYPT_E_REVOKED || !pRevStatus->dwIndex) {
            pRevStatus->dwIndex += done;
            return CrFail(pRevStatus->dwError);
        }
        done += pRevStatus->dwIndex;
    }
    pRevStatus->dwIndex = 0;
    pRevStatus->dwError = 0;
    pRevStatus->dwReason = 0;
    return TRUE;
}
