#include "cryptnet_private.h"

#define DEFAULT_TIMEOUT 15000
#define DEFAULT_LIMIT (100u << 20)
#define CONFIG_KEY L"SOFTWARE\\Microsoft\\Cryptography\\OID\\EncodingType 0\\CertDllCreateCertificateChainEngine\\Config"

/* ------------------------------------------------------------------ the wire */

typedef struct {
    typeof(&WinHttpOpen) Open;
    typeof(&WinHttpConnect) Connect;
    typeof(&WinHttpOpenRequest) OpenRequest;
    typeof(&WinHttpSendRequest) SendRequest;
    typeof(&WinHttpReceiveResponse) ReceiveResponse;
    typeof(&WinHttpQueryHeaders) QueryHeaders;
    typeof(&WinHttpReadData) ReadData;
    typeof(&WinHttpCloseHandle) CloseHandle;
    typeof(&WinHttpCrackUrl) CrackUrl;
    typeof(&WinHttpSetTimeouts) SetTimeouts;
} http_api;

static http_api http;
static INIT_ONCE http_once = INIT_ONCE_STATIC_INIT;

static BOOL WINAPI load_http(INIT_ONCE *once, void *param, void **context)
{
    static const char *names[] = {"WinHttpOpen",         "WinHttpConnect",         "WinHttpOpenRequest",
                                  "WinHttpSendRequest",  "WinHttpReceiveResponse", "WinHttpQueryHeaders",
                                  "WinHttpReadData",     "WinHttpCloseHandle",     "WinHttpCrackUrl",
                                  "WinHttpSetTimeouts"};
    HMODULE module = LoadLibraryExW(L"winhttp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module)
        return FALSE;
    FARPROC *slots = (FARPROC *)&http;
    for (DWORD i = 0; i < RTL_NUMBER_OF(names); i++)
        if (!(slots[i] = GetProcAddress(module, names[i])))
            return FALSE;
    return TRUE;
}

static BYTE *grow(BYTE *data, DWORD used, DWORD capacity)
{
    BYTE *grown = CnAlloc(capacity);
    if (grown && data)
        memcpy(grown, data, used);
    CnFree(data);
    return grown;
}

static BYTE *fetch_http(LPCWSTR url, DWORD timeout, DWORD limit, DWORD *size)
{
    if (!InitOnceExecuteOnce(&http_once, load_http, nullptr, nullptr)) {
        SetLastError(ERROR_MOD_NOT_FOUND);
        return nullptr;
    }
    WCHAR host[256], path[2048];
    URL_COMPONENTS parts = {sizeof(parts)};
    parts.lpszHostName = host;
    parts.dwHostNameLength = RTL_NUMBER_OF(host);
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = RTL_NUMBER_OF(path);
    WCHAR extra[1024];
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = RTL_NUMBER_OF(extra);
    if (!http.CrackUrl(url, 0, 0, &parts))
        return nullptr;
    if (parts.dwExtraInfoLength && wcslen(path) + parts.dwExtraInfoLength < RTL_NUMBER_OF(path))
        wcscat(path, extra);
    BYTE *data = nullptr;
    DWORD used = 0, error = 0;
    BOOL ok = FALSE;
    HINTERNET session = http.Open(L"Microsoft-CryptoAPI/10.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    HINTERNET connection = nullptr, request = nullptr;
    if (session) {
        http.SetTimeouts(session, (int)timeout, (int)timeout, (int)timeout, (int)timeout);
        connection = http.Connect(session, host, parts.nPort, 0);
    }
    if (connection)
        request = http.OpenRequest(connection, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                   parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    DWORD status = 0, length = sizeof(status);
    if (request && http.SendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        http.ReceiveResponse(request, nullptr) &&
        http.QueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                          &status, &length, WINHTTP_NO_HEADER_INDEX)) {
        if (status != 200) {
            error = status == 404 ? ERROR_FILE_NOT_FOUND : ERROR_WINHTTP_INVALID_SERVER_RESPONSE;
        } else {
            DWORD capacity = 0;
            for (;;) {
                if (capacity - used < 16384) {
                    if (capacity >= limit) {
                        error = ERROR_INVALID_DATA;
                        break;
                    }
                    capacity = capacity ? capacity * 2 : 65536;
                    if (!(data = grow(data, used, capacity))) {
                        error = ERROR_OUTOFMEMORY;
                        break;
                    }
                }
                DWORD got = 0;
                if (!http.ReadData(request, data + used, capacity - used, &got)) {
                    error = GetLastError();
                    break;
                }
                if (!got) {
                    ok = used != 0;
                    if (!ok)
                        error = (DWORD)E_UNEXPECTED;
                    break;
                }
                used += got;
            }
        }
    } else {
        error = GetLastError();
    }
    if (request)
        http.CloseHandle(request);
    if (connection)
        http.CloseHandle(connection);
    if (session)
        http.CloseHandle(session);
    if (!ok) {
        CnFree(data);
        SetLastError(error ? error : ERROR_WINHTTP_INVALID_SERVER_RESPONSE);
        return nullptr;
    }
    *size = used;
    return data;
}

static int hex_value(WCHAR c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if ((c | 0x20) >= 'a' && (c | 0x20) <= 'f')
        return (c | 0x20) - 'a' + 10;
    return -1;
}

/* file:///C:/dir/name and file://server/share/name, with escapes, to a path. */
static BOOL file_path(LPCWSTR url, WCHAR *out, DWORD capacity)
{
    LPCWSTR p = url + 7;
    DWORD n = 0;
    if (p[0] == '/' && p[1] && p[2] == ':')
        p++;
    else if (p[0] != '/' && n + 2 < capacity) {
        out[n++] = '\\';
        out[n++] = '\\';
    }
    for (; *p && n + 1 < capacity; p++) {
        WCHAR c = *p;
        if (c == '%' && hex_value(p[1]) >= 0 && hex_value(p[2]) >= 0) {
            c = (WCHAR)(hex_value(p[1]) << 4 | hex_value(p[2]));
            p += 2;
        }
        out[n++] = c == '/' ? '\\' : c;
    }
    out[n] = 0;
    return !*p;
}

static BOOL file_allowed(DWORD flags)
{
    if (flags & CRYPT_ENABLE_FILE_RETRIEVAL)
        return TRUE;
    DWORD value = 0, size = sizeof(value);
    return !RegGetValueW(HKEY_LOCAL_MACHINE, CONFIG_KEY, L"AllowFileUrlScheme", RRF_RT_REG_DWORD, nullptr, &value,
                         &size) &&
           value;
}

static BYTE *fetch_file(LPCWSTR url, DWORD limit, DWORD *size)
{
    WCHAR path[MAX_PATH];
    if (!file_path(url, path, RTL_NUMBER_OF(path))) {
        SetLastError(ERROR_INVALID_NAME);
        return nullptr;
    }
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return nullptr;
    LARGE_INTEGER length;
    BYTE *data = nullptr;
    DWORD error = 0;
    if (!GetFileSizeEx(file, &length))
        error = GetLastError();
    else if (!length.QuadPart || length.QuadPart > limit)
        error = (DWORD)E_FAIL;
    else if (!(data = CnAlloc(length.LowPart)))
        error = ERROR_OUTOFMEMORY;
    else {
        DWORD read = 0;
        if (!ReadFile(file, data, length.LowPart, &read, nullptr) || read != length.LowPart)
            error = read != length.LowPart ? (DWORD)E_FAIL : GetLastError();
        *size = read;
    }
    CloseHandle(file);
    if (error) {
        CnFree(data);
        SetLastError(error);
        return nullptr;
    }
    return data;
}

static BOOL has_scheme(LPCWSTR url, LPCWSTR scheme)
{
    SIZE_T n = wcslen(scheme);
    return !_wcsnicmp(url, scheme, n) && url[n] == ':';
}

BYTE *CnFetch(LPCWSTR url, DWORD flags, DWORD timeout, DWORD limit, DWORD *size)
{
    if (!timeout)
        timeout = DEFAULT_TIMEOUT;
    if (!limit)
        limit = DEFAULT_LIMIT;
    if (has_scheme(url, L"http") || has_scheme(url, L"https"))
        return fetch_http(url, timeout, limit, size);
    if (has_scheme(url, L"file")) {
        if (!file_allowed(flags)) {
            SetLastError(ERROR_NOT_SUPPORTED);
            return nullptr;
        }
        return fetch_file(url, limit, size);
    }
    SetLastError(ERROR_FILE_NOT_FOUND);
    return nullptr;
}

/* ------------------------------------------------------------------ objects */

/* base64 text, with or without a pem header, is the binary it stands for. */
static BYTE *unwrap_text(const BYTE *data, DWORD size, DWORD *out_size)
{
    if (!size || data[0] == 0x30)
        return nullptr;
    DWORD n = 0;
    if (!CryptStringToBinaryA((LPCSTR)data, size, CRYPT_STRING_BASE64_ANY, nullptr, &n, nullptr, nullptr) || !n)
        return nullptr;
    BYTE *binary = CnAlloc(n);
    if (binary && !CryptStringToBinaryA((LPCSTR)data, size, CRYPT_STRING_BASE64_ANY, binary, &n, nullptr, nullptr)) {
        CnFree(binary);
        return nullptr;
    }
    *out_size = n;
    return binary;
}

/* the certificates and lists of a blob: one of them, or a pkcs #7 or serialized store of them. */
static BOOL add_blob(HCERTSTORE store, const BYTE *data, DWORD size)
{
    DWORD binary_size = 0;
    BYTE *binary = unwrap_text(data, size, &binary_size);
    if (binary) {
        data = binary;
        size = binary_size;
    }
    BOOL ok = CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING, data, size, CERT_STORE_ADD_ALWAYS, nullptr) ||
              CertAddEncodedCRLToStore(store, X509_ASN_ENCODING, data, size, CERT_STORE_ADD_ALWAYS, nullptr);
    if (!ok) {
        CRYPT_DATA_BLOB blob = {size, (BYTE *)data};
        HCERTSTORE inner = CertOpenStore(CERT_STORE_PROV_PKCS7, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0, &blob);
        if (!inner)
            inner = CertOpenStore(CERT_STORE_PROV_SERIALIZED, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0, &blob);
        if (inner) {
            PCCERT_CONTEXT cert = nullptr;
            while ((cert = CertEnumCertificatesInStore(inner, cert)))
                CertAddCertificateContextToStore(store, cert, CERT_STORE_ADD_ALWAYS, nullptr);
            PCCRL_CONTEXT crl = nullptr;
            while ((crl = CertEnumCRLsInStore(inner, crl)))
                CertAddCRLContextToStore(store, crl, CERT_STORE_ADD_ALWAYS, nullptr);
            CertCloseStore(inner, 0);
            ok = TRUE;
        }
    }
    CnFree(binary);
    if (!ok)
        SetLastError(CRYPT_E_NO_MATCH);
    return ok;
}

static BOOL verify_object(LPCSTR oid, void *object, PCCERT_CONTEXT signer)
{
    DWORD type = (ULONG_PTR)oid == (ULONG_PTR)CONTEXT_OID_CRL ? CRYPT_VERIFY_CERT_SIGN_SUBJECT_CRL
                                                              : CRYPT_VERIFY_CERT_SIGN_SUBJECT_CERT;
    return CryptVerifyCertificateSignatureEx(0, X509_ASN_ENCODING, type, object, CRYPT_VERIFY_CERT_SIGN_ISSUER_CERT,
                                             (void *)signer, 0, nullptr);
}

/* turns the bytes into what the identifier asks for: a context, a store of them, or a message. */
static BOOL make_object(LPCSTR oid, DWORD flags, const BYTE *data, DWORD size, PCCERT_CONTEXT signer, void **out)
{
    ULONG_PTR kind = (ULONG_PTR)oid;
    if (kind == (ULONG_PTR)CONTEXT_OID_PKCS7) {
        HCRYPTMSG message = CryptMsgOpenToDecode(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, 0, 0, nullptr, nullptr);
        if (!message)
            return FALSE;
        if (!CryptMsgUpdate(message, data, size, TRUE)) {
            DWORD error = GetLastError();
            CryptMsgClose(message);
            return CnFail(error);
        }
        *out = message;
        return TRUE;
    }
    if (kind != (ULONG_PTR)CONTEXT_OID_CERTIFICATE && kind != (ULONG_PTR)CONTEXT_OID_CRL &&
        kind != (ULONG_PTR)CONTEXT_OID_CAPI2_ANY)
        return CnFail(ERROR_FILE_NOT_FOUND);
    HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
    if (!store)
        return FALSE;
    if (!add_blob(store, data, size)) {
        DWORD error = GetLastError();
        CertCloseStore(store, 0);
        return CnFail(error);
    }
    if (kind == (ULONG_PTR)CONTEXT_OID_CAPI2_ANY || (flags & CRYPT_RETRIEVE_MULTIPLE_OBJECTS)) {
        *out = store;
        return TRUE;
    }
    void *object = kind == (ULONG_PTR)CONTEXT_OID_CRL ? (void *)CertEnumCRLsInStore(store, nullptr)
                                                      : (void *)CertEnumCertificatesInStore(store, nullptr);
    CertCloseStore(store, 0);
    if (!object)
        return CnFail(CRYPT_E_NOT_FOUND);
    if ((flags & CRYPT_VERIFY_CONTEXT_SIGNATURE) && !verify_object(oid, object, signer)) {
        DWORD error = GetLastError();
        if (kind == (ULONG_PTR)CONTEXT_OID_CRL)
            CertFreeCRLContext(object);
        else
            CertFreeCertificateContext(object);
        return CnFail(error);
    }
    *out = object;
    return TRUE;
}

/* the bytes as one blob array in one block, which the caller frees with CryptMemFree. */
static BOOL make_blobs(const BYTE *data, DWORD size, void **out)
{
    CRYPT_BLOB_ARRAY *array = CryptMemAlloc(sizeof(CRYPT_BLOB_ARRAY) + sizeof(CRYPT_DATA_BLOB) + size);
    if (!array)
        return CnFail(ERROR_OUTOFMEMORY);
    array->cBlob = 1;
    array->rgBlob = (PCRYPT_DATA_BLOB)(array + 1);
    array->rgBlob[0].cbData = size;
    array->rgBlob[0].pbData = (BYTE *)(array->rgBlob + 1);
    memcpy(array->rgBlob[0].pbData, data, size);
    *out = array;
    return TRUE;
}

/*
 * the cache answers first unless the wire is asked for, and what the wire brings is cached unless asked not to.
 * the caller's checks of time validity are its own: an object is cached as it came.
 */
WINCRYPTNETAPI BOOL WINAPI CryptRetrieveObjectByUrlW(LPCWSTR pszUrl, LPCSTR pszObjectOid, DWORD dwRetrievalFlags,
                                                     DWORD dwTimeout, LPVOID *ppvObject, HANDLE hAsyncRetrieve,
                                                     PCRYPT_CREDENTIALS pCredentials, LPVOID pvVerify,
                                                     PCRYPT_RETRIEVE_AUX_INFO pAuxInfo)
{
    DWORD flags = dwRetrievalFlags;
    if (!pszUrl || !ppvObject || (flags & CRYPT_ASYNC_RETRIEVAL) ||
        (flags & (CRYPT_CACHE_ONLY_RETRIEVAL | CRYPT_WIRE_ONLY_RETRIEVAL)) ==
            (CRYPT_CACHE_ONLY_RETRIEVAL | CRYPT_WIRE_ONLY_RETRIEVAL))
        return CnFail(E_INVALIDARG);
    if (flags & CRYPT_VERIFY_DATA_HASH)
        return CnFail(E_NOTIMPL);
    if ((flags & CRYPT_VERIFY_CONTEXT_SIGNATURE) && (!pvVerify || (flags & CRYPT_RETRIEVE_MULTIPLE_OBJECTS)))
        return CnFail(E_INVALIDARG);
    if (!IS_INTRESOURCE(pszObjectOid))
        return CnFail(ERROR_FILE_NOT_FOUND);
    *ppvObject = nullptr;
    BOOL is_file = !_wcsnicmp(pszUrl, L"file:", 5);
    BYTE *data = nullptr;
    DWORD size = 0;
    FILETIME synced = {};
    if (!(flags & CRYPT_WIRE_ONLY_RETRIEVAL) && !is_file)
        data = CnCacheRead(pszUrl, &size, &synced);
    if (!data) {
        if (flags & CRYPT_CACHE_ONLY_RETRIEVAL)
            return CnFail(ERROR_FILE_NOT_FOUND);
        DWORD limit = pAuxInfo && pAuxInfo->cbSize >= FIELD_OFFSET(CRYPT_RETRIEVE_AUX_INFO, pPreFetchInfo)
                          ? pAuxInfo->dwMaxUrlRetrievalByteCount
                          : 0;
        if (!(data = CnFetch(pszUrl, flags, dwTimeout, limit, &size)))
            return FALSE;
        GetSystemTimeAsFileTime(&synced);
        if (!(flags & CRYPT_DONT_CACHE_RESULT) && !is_file)
            CnCacheWrite(pszUrl, data, size);
    }
    if (pAuxInfo && pAuxInfo->cbSize >= FIELD_OFFSET(CRYPT_RETRIEVE_AUX_INFO, dwMaxUrlRetrievalByteCount) &&
        pAuxInfo->pLastSyncTime)
        *pAuxInfo->pLastSyncTime = synced;
    BOOL ok = !pszObjectOid || pszObjectOid == CONTEXT_OID_OCSP_RESP
                  ? make_blobs(data, size, ppvObject)
                  : make_object(pszObjectOid, flags, data, size, pvVerify, ppvObject);
    DWORD error = GetLastError();
    CnFree(data);
    SetLastError(ok ? 0 : error);
    return ok;
}

WINCRYPTNETAPI BOOL WINAPI CryptRetrieveObjectByUrlA(LPCSTR pszUrl, LPCSTR pszObjectOid, DWORD dwRetrievalFlags,
                                                     DWORD dwTimeout, LPVOID *ppvObject, HANDLE hAsyncRetrieve,
                                                     PCRYPT_CREDENTIALS pCredentials, LPVOID pvVerify,
                                                     PCRYPT_RETRIEVE_AUX_INFO pAuxInfo)
{
    if (!pszUrl)
        return CnFail(E_INVALIDARG);
    int n = MultiByteToWideChar(CP_ACP, 0, pszUrl, -1, nullptr, 0);
    WCHAR *url = n ? CnAlloc(n * sizeof(WCHAR)) : nullptr;
    if (!url)
        return CnFail(ERROR_OUTOFMEMORY);
    MultiByteToWideChar(CP_ACP, 0, pszUrl, -1, url, n);
    BOOL ok = CryptRetrieveObjectByUrlW(url, pszObjectOid, dwRetrievalFlags, dwTimeout, ppvObject, hAsyncRetrieve,
                                        pCredentials, pvVerify, pAuxInfo);
    DWORD error = GetLastError();
    CnFree(url);
    SetLastError(error);
    return ok;
}
