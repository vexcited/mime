#include "qmgr.h"
#include <mime/win32/wincrypt.h>
#include <mime/win32/dpapi.h>

#define AGENT L"Microsoft BITS/7.8"
#define RESOLVE_TIMEOUT 0
#define CONNECT_TIMEOUT 60000
#define SEND_TIMEOUT 120000
#define RECEIVE_TIMEOUT 300000
#define WINHTTP_OPTION_TCP_PRIORITY_HINT_ 128
#define CERT_STORE_PROV_SYSTEM_W_ ((LPCSTR)10)

static HINTERNET g_session;
static SRWLOCK g_session_lock = SRWLOCK_INIT;

/* CWinHttpInterface::OpenSession: no proxy at the session, every request gets its own. */
HINTERNET TransferSession(void)
{
    AcquireSRWLockExclusive(&g_session_lock);
    if (!g_session) {
        g_session = XferOpenSession(AGENT, WINHTTP_ACCESS_TYPE_NO_PROXY, 0);
        if (g_session) {
            DWORD conns = 0xFFFFFFFE;
            WinHttpSetOption(g_session, WINHTTP_OPTION_MAX_CONNS_PER_SERVER, &conns, sizeof(conns));
            WinHttpSetOption(g_session, WINHTTP_OPTION_MAX_CONNS_PER_1_0_SERVER, &conns, sizeof(conns));
            WinHttpSetTimeouts(g_session, RESOLVE_TIMEOUT, CONNECT_TIMEOUT, SEND_TIMEOUT, RECEIVE_TIMEOUT);
            if (g_config.low_priority_tcp) {
                DWORD low = 1;
                WinHttpSetOption(g_session, WINHTTP_OPTION_TCP_PRIORITY_HINT_, &low, sizeof(low));
            }
        }
    }
    HINTERNET session = g_session;
    ReleaseSRWLockExclusive(&g_session_lock);
    return session;
}

void TransferCloseSession(void)
{
    AcquireSRWLockExclusive(&g_session_lock);
    if (g_session)
        WinHttpCloseHandle(g_session);
    g_session = nullptr;
    ReleaseSRWLockExclusive(&g_session_lock);
}

/* ------------------------------------------------------------------ classification */

HRESULT TransferStatus(DWORD status)
{
    if (status == 200 || status == 206)
        return S_OK;
    return (HRESULT)(0x80190000u | (status & 0xFFFF));
}

BOOL TransferFatalWin32(DWORD code)
{
    switch (code) {
    case ERROR_WINHTTP_INVALID_URL:
    case 12037:
    case 12038:
    case 12045:
    case 12057:
    case 12157:
    case 12169:
    case 12170:
    case 12179:
    case 12185:
    case 12186:
        return TRUE;
    default:
        return FALSE;
    }
}

/* CHttpReader::CategorizeStatus: which errors wait for a retry and which end the job. */
BOOL TransferFatal(HRESULT hr, DWORD status)
{
    if (SUCCEEDED(hr) && status) {
        if (status >= 400 && status < 500)
            return status != 408 && status != 409 && status != 416;
        if (status >= 500)
            return status == 505;
        return TRUE;
    }
    if (HRESULT_FACILITY(hr) == FACILITY_WIN32 && TransferFatalWin32(HRESULT_CODE(hr)))
        return TRUE;
    switch (hr) {
    case BG_E_MISSING_FILE_SIZE:
    case BG_E_INSUFFICIENT_HTTP_SUPPORT:
    case BG_E_INSUFFICIENT_RANGE_SUPPORT:
    case BG_E_INVALID_SERVER_RESPONSE:
    case BG_E_INVALID_RANGE:
    case BG_E_INVALID_PROXY_INFO:
    case BG_E_INVALID_CREDENTIALS:
    case BG_E_SERVER_CERT_VALIDATION_INTERFACE_REQUIRED:
    case BG_E_REMOTE_FILE_CHANGED:
    case BG_E_MAX_DOWNLOAD_SIZE_LIMIT_REACHED:
        return TRUE;
    default:
        return FALSE;
    }
}

/* CHttpReader::InitialResponseValidation: an HTTP/1.x status line. */
HRESULT TransferCheckResponse(const XFER_RESULT *r)
{
    if (!r->headers)
        return S_OK;
    if (!wcsncmp(r->headers, L"HTTP/1.", 7))
        return S_OK;
    return !wcsncmp(r->headers, L"HTTP/", 5) ? BG_E_INSUFFICIENT_HTTP_SUPPORT : BG_E_INVALID_SERVER_RESPONSE;
}

void TransferKeepResponse(BFILE *file, const XFER_RESULT *r)
{
    if (r->headers)
        QmReplace(&file->response, r->headers);
}

/* ------------------------------------------------------------------ the server certificate callback */

static BOOL trusted(BJOB *job, const BYTE *hash)
{
    for (DWORD i = 0; i < job->trusted_count; i++)
        if (!memcmp(job->trusted[i], hash, 20))
            return TRUE;
    return FALSE;
}

static void trust(BJOB *job, const BYTE *hash)
{
    BYTE(*list)[20] = LocalAlloc(LMEM_FIXED, (job->trusted_count + 1) * 20);
    if (!list)
        return;
    if (job->trusted_count)
        memcpy(list, job->trusted, job->trusted_count * 20);
    memcpy(list[job->trusted_count], hash, 20);
    LocalFree(job->trusted);
    job->trusted = list;
    job->trusted_count++;
}

/*
 * CJob::ValidateServerCertificate: each new server certificate goes to the client's callback with the leaf and the
 * serialized store, and the answer is kept by the hash of the certificate.
 */
static void headers_ready(void *context, const XFER_RESULT *r)
{
    XREQ *x = context;
    x->status = r->status;
    if (!x->validate || !x->q.active)
        return;
    HINTERNET request = *x->q.active;
    PCCERT_CONTEXT cert = nullptr;
    DWORD size = sizeof(cert);
    if (!request || !WinHttpQueryOption(request, WINHTTP_OPTION_SERVER_CERT_CONTEXT, &cert, &size) || !cert)
        return;
    BYTE hash[20];
    DWORD hash_size = sizeof(hash);
    if (!CertGetCertificateContextProperty(cert, CERT_SHA1_HASH_PROP_ID, hash, &hash_size)) {
        CertFreeCertificateContext(cert);
        return;
    }
    BJOB *job = x->job;
    IBackgroundCopyServerCertificateValidationCallback *callback = nullptr;
    IBackgroundCopyJob *job_ext = nullptr;
    IBackgroundCopyFile *file_ext = nullptr;
    Lock();
    BOOL known = trusted(job, hash);
    HRESULT hr = known ? S_OK : NotifyGetValidator(job, &callback);
    if (!known && SUCCEEDED(hr)) {
        JobExtFor(job, &job_ext);
        if (x->file)
            FileExtFor(x->file, &file_ext);
    }
    Unlock();
    if (!known && SUCCEEDED(hr)) {
        CRYPT_DATA_BLOB store = {0};
        if (cert->hCertStore && CertSaveStore(cert->hCertStore, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                              CERT_STORE_SAVE_AS_STORE, CERT_STORE_SAVE_TO_MEMORY, &store, 0)) {
            store.pbData = LocalAlloc(LMEM_FIXED, store.cbData ? store.cbData : 1);
            if (store.pbData && !CertSaveStore(cert->hCertStore, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                               CERT_STORE_SAVE_AS_STORE, CERT_STORE_SAVE_TO_MEMORY, &store, 0))
                store.cbData = 0;
        }
        hr = callback->lpVtbl->ValidateServerCertificate(callback, job_ext, file_ext, cert->cbCertEncoded,
                                                          cert->pbCertEncoded, cert->dwCertEncodingType, store.cbData,
                                                          store.pbData);
        LocalFree(store.pbData);
        if (hr == RPC_E_DISCONNECTED_ || hr == (HRESULT)0x800706BAL)
            hr = BG_E_SERVER_CERT_VALIDATION_INTERFACE_REQUIRED;
        if (SUCCEEDED(hr)) {
            Lock();
            trust(job, hash);
            Unlock();
        }
    }
    if (callback)
        callback->lpVtbl->Release(callback);
    if (job_ext)
        QM_RELEASE(job_ext);
    if (file_ext)
        QM_RELEASE(file_ext);
    CertFreeCertificateContext(cert);
    if (FAILED(hr)) {
        x->cert_error = hr;
        XferAbort(x->q.active);
    }
}

/* ------------------------------------------------------------------ the request of a job */

static DWORD winhttp_scheme(BG_AUTH_SCHEME scheme)
{
    switch (scheme) {
    case BG_AUTH_SCHEME_BASIC:
        return WINHTTP_AUTH_SCHEME_BASIC;
    case BG_AUTH_SCHEME_DIGEST:
        return WINHTTP_AUTH_SCHEME_DIGEST;
    case BG_AUTH_SCHEME_NTLM:
        return WINHTTP_AUTH_SCHEME_NTLM;
    case BG_AUTH_SCHEME_NEGOTIATE:
        return WINHTTP_AUTH_SCHEME_NEGOTIATE;
    default:
        return WINHTTP_AUTH_SCHEME_PASSPORT;
    }
}

static WCHAR *join(const WCHAR *a, const WCHAR *b)
{
    size_t la = a ? wcslen(a) : 0, lb = b ? wcslen(b) : 0;
    WCHAR *text = LocalAlloc(LMEM_FIXED, (la + lb + 2) * sizeof(WCHAR));
    if (!text)
        return nullptr;
    if (la)
        memcpy(text, a, la * sizeof(WCHAR));
    text[la] = 0;
    if (lb)
        memcpy(text + la + 1, b, lb * sizeof(WCHAR));
    text[la + 1 + lb] = 0;
    return text;
}

/* the two strings of a WINHTTP_PROXY_INFO kept in one block, the list then the bypass list. */
static void named_proxy(XREQ *x, const WCHAR *list, const WCHAR *bypass)
{
    x->proxy_text = join(list, bypass);
    if (!x->proxy_text)
        return;
    x->proxy.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
    x->proxy.lpszProxy = x->proxy_text;
    x->proxy.lpszProxyBypass = bypass && *bypass ? x->proxy_text + wcslen(x->proxy_text) + 1 : nullptr;
    x->q.proxy = &x->proxy;
}

static BOOL auto_proxy(XREQ *x, const WCHAR *url, const WCHAR *script, BOOL detect)
{
    WINHTTP_AUTOPROXY_OPTIONS options = {0};
    options.dwFlags = (detect ? WINHTTP_AUTOPROXY_AUTO_DETECT : 0) | (script ? WINHTTP_AUTOPROXY_CONFIG_URL : 0);
    options.dwAutoDetectFlags = detect ? WINHTTP_AUTO_DETECT_TYPE_DHCP | WINHTTP_AUTO_DETECT_TYPE_DNS_A : 0;
    options.lpszAutoConfigUrl = script;
    options.fAutoLogonIfChallenged = TRUE;
    WINHTTP_PROXY_INFO info = {0};
    if (!WinHttpGetProxyForUrl(TransferSession(), url, &options, &info))
        return FALSE;
    if (info.dwAccessType == WINHTTP_ACCESS_TYPE_NAMED_PROXY && info.lpszProxy)
        named_proxy(x, info.lpszProxy, info.lpszProxyBypass);
    if (info.lpszProxy)
        GlobalFree(info.lpszProxy);
    if (info.lpszProxyBypass)
        GlobalFree(info.lpszProxyBypass);
    return TRUE;
}

/*
 * CWinHttpRequest::SetProxy and CHttpReader::ProxyResolutionRoutine: PRECONFIG takes the owner's settings and their
 * script or detection, AUTODETECT detection alone, OVERRIDE the job's lists.
 */
static void resolve_proxy(XREQ *x, const WCHAR *url, BG_JOB_PROXY_USAGE usage, const WCHAR *list, const WCHAR *bypass,
                          HANDLE owner)
{
    x->proxy.dwAccessType = WINHTTP_ACCESS_TYPE_NO_PROXY;
    x->q.proxy = &x->proxy;
    switch (usage) {
    case BG_JOB_PROXY_USAGE_OVERRIDE:
        named_proxy(x, list, bypass);
        break;
    case BG_JOB_PROXY_USAGE_AUTODETECT:
        auto_proxy(x, url, nullptr, TRUE);
        break;
    case BG_JOB_PROXY_USAGE_PRECONFIG: {
        WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ie = {0};
        BOOL impersonating = owner && SetThreadToken(nullptr, owner);
        BOOL have = WinHttpGetIEProxyConfigForCurrentUser(&ie);
        if (impersonating)
            SetThreadToken(nullptr, nullptr);
        if (have) {
            BOOL done = (ie.fAutoDetect || ie.lpszAutoConfigUrl) && auto_proxy(x, url, ie.lpszAutoConfigUrl, ie.fAutoDetect);
            if (!done && ie.lpszProxy && *ie.lpszProxy)
                named_proxy(x, ie.lpszProxy, ie.lpszProxyBypass);
            if (ie.lpszAutoConfigUrl)
                GlobalFree(ie.lpszAutoConfigUrl);
            if (ie.lpszProxy)
                GlobalFree(ie.lpszProxy);
            if (ie.lpszProxyBypass)
                GlobalFree(ie.lpszProxyBypass);
        }
        break;
    }
    default:
        break;
    }
}

static const DWORD g_store_locations[] = {
    CERT_SYSTEM_STORE_CURRENT_USER,
    CERT_SYSTEM_STORE_LOCAL_MACHINE,
    CERT_SYSTEM_STORE_CURRENT_SERVICE,
    CERT_SYSTEM_STORE_SERVICES,
    CERT_SYSTEM_STORE_USERS,
    CERT_SYSTEM_STORE_CURRENT_USER_GROUP_POLICY,
    CERT_SYSTEM_STORE_LOCAL_MACHINE_GROUP_POLICY,
    CERT_SYSTEM_STORE_LOCAL_MACHINE_ENTERPRISE,
};

/* SetClientCertificate: the certificate is looked up in its store, as the owner, each time a request goes out. */
static const void *client_cert(BG_CERT_STORE_LOCATION location, const WCHAR *name, const BYTE *hash,
                               const WCHAR *subject, HANDLE owner)
{
    BOOL impersonating = owner && SetThreadToken(nullptr, owner);
    HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W_, 0, 0, g_store_locations[location] | CERT_STORE_READONLY_FLAG,
                                     name);
    PCCERT_CONTEXT cert = nullptr;
    if (store) {
        if (hash) {
            CRYPT_HASH_BLOB blob = {20, (BYTE *)hash};
            cert = CertFindCertificateInStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_FIND_SHA1_HASH, &blob,
                                              nullptr);
        } else
            cert = CertFindCertificateInStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_FIND_SUBJECT_STR_W,
                                              subject, nullptr);
        CertCloseStore(store, 0);
    }
    if (impersonating)
        SetThreadToken(nullptr, nullptr);
    return cert;
}


/*
 * CHttpReader::CreateRequest: identity encoding, high autologon, the job's security and redirect flags, credentials,
 * proxy and client certificate. What the job holds is copied under the lock, the slow parts run without it.
 */
void TransferRequest(RUN *run, BFILE *file, const WCHAR *url, const WCHAR *extra, XREQ *x)
{
    memset(x, 0, sizeof(*x));
    BJOB *job = run->job;
    x->job = job;
    x->file = file;
    BYTE *secrets[MAX_CREDS] = {0};
    DWORD sizes[MAX_CREDS] = {0};
    WCHAR *users[MAX_CREDS] = {0};
    Lock();
    DWORD flags = job->security_flags;
    WCHAR *headers = QmDup(job->headers);
    BG_JOB_PROXY_USAGE usage = job->proxy_usage;
    WCHAR *list = QmDup(job->proxy_list), *bypass = QmDup(job->proxy_bypass);
    HANDLE owner = nullptr, network = nullptr;
    if (job->owner_token)
        DuplicateHandle(GetCurrentProcess(), job->owner_token, GetCurrentProcess(), &owner, 0, FALSE, DUPLICATE_SAME_ACCESS);
    HANDLE source = (job->helper_flags & 2) && job->helper_token ? job->helper_token : job->owner_token;
    if (source)
        DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &network, 0, FALSE, DUPLICATE_SAME_ACCESS);
    DWORD count = job->cred_count;
    for (DWORD i = 0; i < count; i++) {
        x->creds[i].target = job->creds[i].target == BG_AUTH_TARGET_PROXY ? WINHTTP_AUTH_TARGET_PROXY
                                                                          : WINHTTP_AUTH_TARGET_SERVER;
        x->creds[i].scheme = winhttp_scheme(job->creds[i].scheme);
        users[i] = QmDup(job->creds[i].user);
        if (job->creds[i].secret && (secrets[i] = LocalAlloc(LMEM_FIXED, job->creds[i].secret_size))) {
            memcpy(secrets[i], job->creds[i].secret, job->creds[i].secret_size);
            sizes[i] = job->creds[i].secret_size;
        }
    }
    BOOL has_cert = job->has_cert, by_hash = job->cert_by_hash;
    BG_CERT_STORE_LOCATION location = job->cert_location;
    WCHAR *store = QmDup(job->cert_store), *subject = QmDup(job->cert_subject);
    BYTE hash[20];
    memcpy(hash, job->cert_hash, sizeof(hash));
    x->validate = job->has_validator && QmProtocol(url) == PROTOCOL_HTTPS;
    Unlock();

    XFER_REQUEST *q = &x->q;
    q->url = url;
    q->cancel = run->cancel;
    q->active = &run->active;
    q->identity = TRUE;
    q->has_autologon = TRUE;
    q->autologon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    q->revocation = (flags & BG_SSL_ENABLE_CRL_CHECK) != 0;
    if (flags & BG_SSL_IGNORE_CERT_CN_INVALID)
        q->security_flags |= SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
    if (flags & BG_SSL_IGNORE_CERT_DATE_INVALID)
        q->security_flags |= SECURITY_FLAG_IGNORE_CERT_DATE_INVALID;
    if (flags & BG_SSL_IGNORE_UNKNOWN_CA)
        q->security_flags |= SECURITY_FLAG_IGNORE_UNKNOWN_CA;
    if (flags & BG_SSL_IGNORE_CERT_WRONG_USAGE)
        q->security_flags |= SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    if ((flags & 0x700) == BG_HTTP_REDIRECT_POLICY_DISALLOW)
        q->no_redirects = TRUE;
    if (flags & BG_HTTP_REDIRECT_POLICY_ALLOW_HTTPS_TO_HTTP) {
        q->has_redirect_policy = TRUE;
        q->redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    }
    /* custom headers go last, each line ended, after what the transfer itself adds. */
    size_t length = (extra ? wcslen(extra) : 0) + (headers ? wcslen(headers) : 0) + 4;
    x->headers = LocalAlloc(LMEM_FIXED, length * sizeof(WCHAR));
    if (x->headers) {
        x->headers[0] = 0;
        if (extra)
            wcscat(x->headers, extra);
        if (headers && *headers) {
            wcscat(x->headers, headers);
            size_t n = wcslen(x->headers);
            if (n < 2 || x->headers[n - 2] != '\r' || x->headers[n - 1] != '\n')
                wcscat(x->headers, L"\r\n");
        }
        q->headers = x->headers;
    }
    for (DWORD i = 0; i < count; i++) {
        x->creds[i].user = users[i];
        if (secrets[i]) {
            DATA_BLOB in = {sizes[i], secrets[i]}, out = {0};
            if (CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
                x->passwords[i] = (WCHAR *)out.pbData;
                x->creds[i].password = x->passwords[i];
            }
            LocalFree(secrets[i]);
        }
    }
    q->credentials = x->creds;
    q->credential_count = count;
    resolve_proxy(x, url, usage, list, bypass, owner);
    if (has_cert && store)
        x->cert = client_cert(location, store, by_hash ? hash : nullptr, subject, owner);
    if (x->cert) {
        q->client_cert = x->cert;
        q->client_cert_size = sizeof(CERT_CONTEXT);
    }
    x->network_token = network;
    q->context = x;
    q->headers_ready = headers_ready;
    LocalFree(headers);
    LocalFree(list);
    LocalFree(bypass);
    LocalFree(store);
    LocalFree(subject);
    if (owner)
        CloseHandle(owner);
}

/* the request as the owner, or the helper token with BG_TOKEN_NETWORK, so that NTLM and Negotiate log on as them. */
HRESULT TransferRun(XREQ *x, XFER_RESULT *r)
{
    HINTERNET session = TransferSession();
    if (!session) {
        memset(r, 0, sizeof(*r));
        return HRESULT_FROM_WIN32(GetLastError());
    }
    BOOL impersonating = x->network_token && SetThreadToken(nullptr, x->network_token);
    HRESULT hr = XferRun(session, &x->q, r);
    if (impersonating)
        SetThreadToken(nullptr, nullptr);
    if (FAILED(x->cert_error))
        return x->cert_error;
    if (SUCCEEDED(hr))
        hr = TransferCheckResponse(r);
    return hr;
}

void TransferFree(XREQ *x)
{
    for (DWORD i = 0; i < MAX_CREDS; i++) {
        LocalFree((void *)x->creds[i].user);
        if (x->passwords[i]) {
            SecureZeroMemory(x->passwords[i], wcslen(x->passwords[i]) * sizeof(WCHAR));
            LocalFree(x->passwords[i]);
        }
    }
    if (x->cert)
        CertFreeCertificateContext(x->cert);
    if (x->network_token)
        CloseHandle(x->network_token);
    LocalFree(x->proxy_text);
    LocalFree(x->headers);
    memset(x, 0, sizeof(*x));
}
