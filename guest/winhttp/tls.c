/* HTTPS through Schannel: handshake, EncryptMessage and DecryptMessage. */
#include "winhttp_private.h"
#include <mime/win32/wincrypt.h>

#define ISC_REQ_SEQUENCE_DETECT 0x00000008
#define ISC_REQ_REPLAY_DETECT 0x00000004
#define ISC_REQ_CONFIDENTIALITY 0x00000010
#define ISC_REQ_ALLOCATE_MEMORY 0x00000100
#define ISC_REQ_EXTENDED_ERROR 0x00004000
#define ISC_REQ_STREAM 0x00008000
#define ISC_REQ_MANUAL_CRED_VALIDATION 0x00080000

#define SCH_CRED_NO_DEFAULT_CREDS 0x00000010
#define SCH_CRED_MANUAL_CRED_VALIDATION 0x00000008
#define SCH_CRED_AUTO_CRED_VALIDATION 0x00000020
#define SCH_CRED_IGNORE_NO_REVOCATION_CHECK 0x00000800
#define SCH_CRED_IGNORE_REVOCATION_OFFLINE 0x00001000

#define SP_PROT_TLS1_CLIENT 0x00000080
#define SP_PROT_TLS1_1_CLIENT 0x00000200
#define SP_PROT_TLS1_2_CLIENT 0x00000800
#define SP_PROT_TLS1_3_CLIENT 0x00002000

#define WH_TLS_RAW 65536
#define ISC_REQ_USE_SUPPLIED_CREDS_ 0x00000080
#define ERROR_WINHTTP_CLIENT_CERT_NO_ACCESS_PRIVATE_KEY_ 12186

/* SCH_CREDENTIALS and TLS_PARAMETERS: only this form gets tls 1.3 from schannel. */
typedef struct {
    DWORD cAlpnIds;
    void *rgstrAlpnIds;
    DWORD grbitDisabledProtocols;
    DWORD cDisabledCrypto;
    void *pDisabledCrypto;
    DWORD dwFlags;
} WH_TLS_PARAMETERS;

typedef struct {
    DWORD dwVersion;
    DWORD dwCredFormat;
    DWORD cCreds;
    void *paCred;
    void *hRootStore;
    DWORD cMappers;
    void *aphMappers;
    DWORD dwSessionLifespan;
    DWORD dwFlags;
    DWORD cTlsParameters;
    WH_TLS_PARAMETERS *pTlsParameters;
} WH_SCH_CREDENTIALS;

#define SCH_CREDENTIALS_VERSION_ 5

typedef struct WH_CRED {
    LONG volatile Refs;
    CredHandle Handle;
    const CERT_CONTEXT *Cert;
    DWORD Protocols;
    DWORD Flags;
} WH_CRED;

typedef struct WH_CRED_SLOT {
    struct WH_CRED_SLOT *Next;
    WCHAR Host[INTERNET_MAX_HOST_NAME_LENGTH];
    INTERNET_PORT Port;
    WH_CRED *Current;
} WH_CRED_SLOT;

static SRWLOCK CredLock = SRWLOCK_INIT;

typedef struct {
    ULONG cbHeader;
    ULONG cbTrailer;
    ULONG cbMaximumMessage;
    ULONG cBuffers;
    ULONG cbBlockSize;
} WH_STREAM_SIZES;

static INIT_ONCE TableOnce = INIT_ONCE_STATIC_INIT;
static PSecurityFunctionTableW Table;

static BOOL CALLBACK LoadTable(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    HMODULE module = LoadLibraryW(L"secur32.dll");
    PSecurityFunctionTableW(SEC_ENTRY * init)(void) =
        module ? (PSecurityFunctionTableW(SEC_ENTRY *)(void))GetProcAddress(module, "InitSecurityInterfaceW") : nullptr;
    Table = init ? init() : nullptr;
    return TRUE;
}

/* map handshake failures to SECURE_FAILURE flags and WinHTTP errors. */
static DWORD MapFailure(SECURITY_STATUS status, DWORD *failure)
{
    switch (status) {
    case SEC_E_UNTRUSTED_ROOT:
        *failure = WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CA;
        return ERROR_WINHTTP_SECURE_FAILURE;
    case SEC_E_CERT_EXPIRED:
        *failure = WINHTTP_CALLBACK_STATUS_FLAG_CERT_DATE_INVALID;
        return ERROR_WINHTTP_SECURE_FAILURE;
    case SEC_E_WRONG_PRINCIPAL:
        *failure = WINHTTP_CALLBACK_STATUS_FLAG_CERT_CN_INVALID;
        return ERROR_WINHTTP_SECURE_FAILURE;
    case SEC_E_CERT_UNKNOWN:
        *failure = WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CERT;
        return ERROR_WINHTTP_SECURE_FAILURE;
    }
    *failure = WINHTTP_CALLBACK_STATUS_FLAG_SECURITY_CHANNEL_ERROR;
    return ERROR_WINHTTP_SECURE_CHANNEL_ERROR;
}

static DWORD RawFill(WH_CONN *conn, int timeout)
{
    if (conn->RawUsed == conn->RawSize)
        return ERROR_WINHTTP_SECURE_CHANNEL_ERROR;
    DWORD got;
    DWORD error = WhConnRecv(conn, conn->Raw + conn->RawUsed, conn->RawSize - conn->RawUsed, &got, timeout);
    if (error)
        return error;
    if (!got)
        return ERROR_WINHTTP_CONNECTION_ERROR;
    conn->RawUsed += got;
    return ERROR_SUCCESS;
}

static DWORD SendToken(WH_CONN *conn, SecBuffer *token, int timeout)
{
    DWORD error = ERROR_SUCCESS;
    if (token->cbBuffer && token->pvBuffer) {
        const char *p = token->pvBuffer;
        DWORD left = token->cbBuffer;
        while (left && !error) {
            int sent = send(conn->Socket, p, (int)left, 0);
            if (sent <= 0)
                error = ERROR_WINHTTP_CONNECTION_ERROR;
            else {
                p += sent;
                left -= (DWORD)sent;
            }
        }
    }
    if (token->pvBuffer)
        Table->FreeContextBuffer(token->pvBuffer);
    token->pvBuffer = nullptr;
    token->cbBuffer = 0;
    return error;
}

/* continue InitializeSecurityContext with the bytes already received until the handshake ends. */
static DWORD Negotiate(WH_CONN *conn, LPCWSTR host, BOOL initial, BOOL cert_set, int timeout, DWORD *failure)
{
    ULONG flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_EXTENDED_ERROR |
                  ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;
    BOOL first = initial;
    while (TRUE) {
        SecBuffer in[2] = {{conn->RawUsed, SECBUFFER_TOKEN, conn->Raw}, {0, SECBUFFER_EMPTY, nullptr}};
        SecBufferDesc in_desc = {SECBUFFER_VERSION, 2, in};
        SecBuffer out[1] = {{0, SECBUFFER_TOKEN, nullptr}};
        SecBufferDesc out_desc = {SECBUFFER_VERSION, 1, out};
        ULONG attributes = 0;
        SECURITY_STATUS status = Table->InitializeSecurityContextW(
            &conn->Credentials, first ? nullptr : &conn->Context, (SEC_WCHAR *)host, flags, 0, 0,
            first ? nullptr : &in_desc, 0, first ? &conn->Context : nullptr, &out_desc, &attributes, nullptr);
        if (first && status != SEC_I_CONTINUE_NEEDED && status != SEC_E_OK) {
            if (out[0].pvBuffer)
                Table->FreeContextBuffer(out[0].pvBuffer);
            return MapFailure(status, failure);
        }
        first = FALSE;
        conn->Tls = TRUE;
        if (status == SEC_E_INCOMPLETE_MESSAGE) {
            DWORD error = RawFill(conn, timeout);
            if (error)
                return error;
            continue;
        }
        DWORD error = SendToken(conn, &out[0], timeout);
        if (error)
            return error;
        /*
         * the server asked a certificate and the request has none: the program decides, as windows makes it, unless
         * it said no certificate already. then the handshake goes on with an empty one.
         */
        if (status == SEC_I_INCOMPLETE_CREDENTIALS) {
            if (!cert_set) {
                *failure = 0;
                return ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED;
            }
            if (in[1].BufferType == SECBUFFER_EXTRA && in[1].cbBuffer) {
                memmove(conn->Raw, conn->Raw + conn->RawUsed - in[1].cbBuffer, in[1].cbBuffer);
                conn->RawUsed = in[1].cbBuffer;
            } else
                conn->RawUsed = 0;
            flags |= ISC_REQ_USE_SUPPLIED_CREDS_;
            continue;
        }
        if (status != SEC_E_OK && status != SEC_I_CONTINUE_NEEDED)
            return MapFailure(status, failure);
        if (in[1].BufferType == SECBUFFER_EXTRA && in[1].cbBuffer) {
            memmove(conn->Raw, conn->Raw + conn->RawUsed - in[1].cbBuffer, in[1].cbBuffer);
            conn->RawUsed = in[1].cbBuffer;
        } else {
            conn->RawUsed = 0;
        }
        if (status == SEC_E_OK)
            return ERROR_SUCCESS;
        if ((error = RawFill(conn, timeout)))
            return error;
    }
}

static INIT_ONCE CryptOnce = INIT_ONCE_STATIC_INIT;
static struct {
    typeof(CertGetCertificateChain) *GetChain;
    typeof(CertFreeCertificateChain) *FreeChain;
    typeof(CertVerifyCertificateChainPolicy) *VerifyPolicy;
    typeof(CertFreeCertificateContext) *FreeContext;
    typeof(CertDuplicateCertificateContext) *Duplicate;
    BOOL(WINAPI *AcquireKey)(const void *cert, DWORD flags, void *reserved, ULONG_PTR *key, DWORD *spec, BOOL *release);
    LONG(WINAPI *NFree)(ULONG_PTR key);
    BOOL(WINAPI *LRelease)(ULONG_PTR key, DWORD flags);
} Crypt;

static BOOL CALLBACK LoadCrypt(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    HMODULE module = LoadLibraryW(L"crypt32.dll");
    if (module) {
        Crypt.GetChain = (void *)GetProcAddress(module, "CertGetCertificateChain");
        Crypt.FreeChain = (void *)GetProcAddress(module, "CertFreeCertificateChain");
        Crypt.VerifyPolicy = (void *)GetProcAddress(module, "CertVerifyCertificateChainPolicy");
        Crypt.FreeContext = (void *)GetProcAddress(module, "CertFreeCertificateContext");
        Crypt.Duplicate = (void *)GetProcAddress(module, "CertDuplicateCertificateContext");
        Crypt.AcquireKey = (void *)GetProcAddress(module, "CryptAcquireCertificatePrivateKey");
    }
    HMODULE ncrypt = LoadLibraryW(L"ncrypt.dll"), advapi = LoadLibraryW(L"advapi32.dll");
    if (ncrypt)
        Crypt.NFree = (void *)GetProcAddress(ncrypt, "NCryptFreeObject");
    if (advapi)
        Crypt.LRelease = (void *)GetProcAddress(advapi, "CryptReleaseContext");
    return TRUE;
}

const void *WhTlsCertDuplicate(const void *cert)
{
    InitOnceExecuteOnce(&CryptOnce, LoadCrypt, nullptr, nullptr);
    return Crypt.Duplicate ? Crypt.Duplicate(cert) : nullptr;
}

void WhTlsCertFree(const void *cert)
{
    InitOnceExecuteOnce(&CryptOnce, LoadCrypt, nullptr, nullptr);
    if (Crypt.FreeContext)
        Crypt.FreeContext(cert);
}

/* a client certificate is only one with a private key the process can use, which windows checks before it connects. */
static BOOL CertHasKey(const void *cert)
{
    InitOnceExecuteOnce(&CryptOnce, LoadCrypt, nullptr, nullptr);
    if (!Crypt.AcquireKey)
        return FALSE;
    ULONG_PTR key = 0;
    DWORD spec = 0;
    BOOL release = FALSE;
    /* CRYPT_ACQUIRE_SILENT_FLAG | CRYPT_ACQUIRE_ALLOW_NCRYPT_KEY_FLAG */
    if (!Crypt.AcquireKey(cert, 0x40 | 0x10000, nullptr, &key, &spec, &release))
        return FALSE;
    if (release) {
        if (spec == 0xFFFFFFFF) {
            if (Crypt.NFree)
                Crypt.NFree(key);
        } else if (Crypt.LRelease)
            Crypt.LRelease(key, 0);
    }
    return TRUE;
}

static DWORD PolicyFailure(DWORD error)
{
    switch (error) {
    case (DWORD)CERT_E_UNTRUSTEDROOT:
    case (DWORD)CERT_E_CHAINING:
        return WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CA;
    case (DWORD)CERT_E_EXPIRED:
    case (DWORD)CERT_E_VALIDITYPERIODNESTING:
        return WINHTTP_CALLBACK_STATUS_FLAG_CERT_DATE_INVALID;
    case (DWORD)CERT_E_CN_NO_MATCH:
        return WINHTTP_CALLBACK_STATUS_FLAG_CERT_CN_INVALID;
    case (DWORD)CERT_E_WRONG_USAGE:
        return WINHTTP_CALLBACK_STATUS_FLAG_CERT_WRONG_USAGE;
    case (DWORD)CRYPT_E_REVOKED:
        return WINHTTP_CALLBACK_STATUS_FLAG_CERT_REVOKED;
    case (DWORD)CRYPT_E_REVOCATION_OFFLINE:
        return WINHTTP_CALLBACK_STATUS_FLAG_CERT_REV_FAILED;
    }
    return WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CERT;
}

/*
 * when Schannel delegates chain validation, apply SSL policy with only the requested checks disabled. ignoring an
 * unknown authority must still check the hostname.
 */
static DWORD VerifyServer(WH_CONN *conn, LPCWSTR host, DWORD security_flags, DWORD *failure)
{
    InitOnceExecuteOnce(&CryptOnce, LoadCrypt, nullptr, nullptr);
    if (!Crypt.GetChain || !Crypt.FreeChain || !Crypt.VerifyPolicy || !Crypt.FreeContext) {
        *failure = WINHTTP_CALLBACK_STATUS_FLAG_SECURITY_CHANNEL_ERROR;
        return ERROR_WINHTTP_SECURE_CHANNEL_ERROR;
    }
    PCCERT_CONTEXT cert = nullptr;
    if (Table->QueryContextAttributesW(&conn->Context, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &cert) != SEC_E_OK || !cert) {
        *failure = WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CERT;
        return ERROR_WINHTTP_SECURE_FAILURE;
    }
    LPSTR usage = (LPSTR)szOID_PKIX_KP_SERVER_AUTH;
    CERT_CHAIN_PARA chain_para = {sizeof(chain_para)};
    chain_para.RequestedUsage.dwType = USAGE_MATCH_TYPE_OR;
    chain_para.RequestedUsage.Usage.cUsageIdentifier = 1;
    chain_para.RequestedUsage.Usage.rgpszUsageIdentifier = &usage;
    PCCERT_CHAIN_CONTEXT chain = nullptr;
    DWORD error = ERROR_SUCCESS;
    if (!Crypt.GetChain(nullptr, cert, nullptr, cert->hCertStore, &chain_para, 0, nullptr, &chain)) {
        *failure = WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CERT;
        error = ERROR_WINHTTP_SECURE_FAILURE;
    } else {
        SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl = {};
        ssl.cbSize = sizeof(ssl);
        ssl.dwAuthType = AUTHTYPE_SERVER;
        ssl.fdwChecks = security_flags & SECURITY_FLAG_IGNORE_ALL_CERT_ERRORS;
        ssl.pwszServerName = (WCHAR *)host;
        CERT_CHAIN_POLICY_PARA policy = {sizeof(policy), 0, &ssl};
        CERT_CHAIN_POLICY_STATUS status = {sizeof(status)};
        if (!Crypt.VerifyPolicy(CERT_CHAIN_POLICY_SSL, chain, &policy, &status) || status.dwError) {
            *failure = PolicyFailure(status.dwError);
            error = ERROR_WINHTTP_SECURE_FAILURE;
        }
        Crypt.FreeChain(chain);
    }
    Crypt.FreeContext(cert);
    return error;
}

/* the certificate the server presented, a reference the caller frees. */
DWORD WhTlsServerCert(WH_CONN *conn, const void **out)
{
    PCCERT_CONTEXT *cert = (PCCERT_CONTEXT *)out;
    *cert = nullptr;
    if (!conn || !conn->Tls || !Table)
        return ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
    if (Table->QueryContextAttributesW(&conn->Context, SECPKG_ATTR_REMOTE_CERT_CONTEXT, cert) != SEC_E_OK || !*cert)
        return ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
    return ERROR_SUCCESS;
}

/* the chain of the server's certificate, built for server authentication as the handshake checks it. */
DWORD WhTlsServerChain(WH_CONN *conn, const void **out)
{
    PCCERT_CHAIN_CONTEXT *chain = (PCCERT_CHAIN_CONTEXT *)out;
    *chain = nullptr;
    InitOnceExecuteOnce(&CryptOnce, LoadCrypt, nullptr, nullptr);
    if (!Crypt.GetChain || !Crypt.FreeContext)
        return ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
    PCCERT_CONTEXT cert;
    DWORD error = WhTlsServerCert(conn, (const void **)&cert);
    if (error)
        return error;
    LPSTR usage = (LPSTR)szOID_PKIX_KP_SERVER_AUTH;
    CERT_CHAIN_PARA chain_para = {sizeof(chain_para)};
    chain_para.RequestedUsage.dwType = USAGE_MATCH_TYPE_OR;
    chain_para.RequestedUsage.Usage.cUsageIdentifier = 1;
    chain_para.RequestedUsage.Usage.rgpszUsageIdentifier = &usage;
    if (!Crypt.GetChain(nullptr, cert, nullptr, cert->hCertStore, &chain_para, 0, nullptr, chain))
        error = GetLastError() ? GetLastError() : ERROR_WINHTTP_INCORRECT_HANDLE_STATE;
    Crypt.FreeContext(cert);
    return error;
}

static void ReleaseCred(WH_CRED *cred)
{
    if (!cred || InterlockedDecrement(&cred->Refs))
        return;
    Table->FreeCredentialsHandle(&cred->Handle);
    if (cred->Cert)
        WhTlsCertFree(cred->Cert);
    WhFree(cred);
}

static BOOL SameCert(const CERT_CONTEXT *a, const CERT_CONTEXT *b)
{
    return a == b || (a->cbCertEncoded == b->cbCertEncoded && !memcmp(a->pbCertEncoded, b->pbCertEncoded, a->cbCertEncoded));
}

/* the new credential, with SCH_CREDENTIALS as webio's WapSslAcquireCredentialsHandle builds it. */
static WH_CRED *NewCred(const CERT_CONTEXT *cert, DWORD protocols, DWORD flags)
{
    WH_CRED *cred = WhAllocZero(sizeof(*cred));
    if (!cred)
        return nullptr;
    WH_TLS_PARAMETERS parameters = {0};
    parameters.grbitDisabledProtocols = ~protocols;
    WH_SCH_CREDENTIALS credentials = {0};
    credentials.dwVersion = SCH_CREDENTIALS_VERSION_;
    credentials.dwFlags = flags;
    credentials.cTlsParameters = 1;
    credentials.pTlsParameters = &parameters;
    if (cert) {
        credentials.cCreds = 1;
        credentials.paCred = (void *)&cert;
    }
    if (Table->AcquireCredentialsHandleW(nullptr, (SEC_WCHAR *)UNISP_NAME_W, SECPKG_CRED_OUTBOUND, nullptr,
                                         &credentials, nullptr, nullptr, &cred->Handle, nullptr) != SEC_E_OK) {
        WhFree(cred);
        return nullptr;
    }
    cred->Refs = 1;
    cred->Cert = cert ? WhTlsCertDuplicate(cert) : nullptr;
    cred->Protocols = protocols;
    cred->Flags = flags;
    return cred;
}

/*
 * webio's WaSslRetrieveCredentialsHandle: each server of a session has one current credential. a request with no
 * certificate takes it, certificate and all, so the server sees the last certificate again and the session resumes. a
 * request with another certificate, or other protocols, makes a new one that becomes the current one.
 */
static WH_CRED *TakeCred(WH_OBJECT *session, WH_CONN *conn, const CERT_CONTEXT *cert, DWORD protocols, DWORD flags)
{
    AcquireSRWLockExclusive(&CredLock);
    WH_CRED_SLOT *slot = session->CredSlots;
    while (slot && (slot->Port != conn->Port || _wcsicmp(slot->Host, conn->Host)))
        slot = slot->Next;
    if (!slot && (slot = WhAllocZero(sizeof(*slot)))) {
        wcscpy(slot->Host, conn->Host);
        slot->Port = conn->Port;
        slot->Next = session->CredSlots;
        session->CredSlots = slot;
    }
    WH_CRED *current = slot ? slot->Current : nullptr, *taken = nullptr;
    const CERT_CONTEXT *carried = nullptr;
    if (current) {
        BOOL fits = !(current->Protocols & ~protocols) && current->Flags == flags;
        if (fits && (!cert || (current->Cert && SameCert(cert, current->Cert)))) {
            InterlockedIncrement(&current->Refs);
            taken = current;
        } else if (!cert && current->Cert) {
            carried = WhTlsCertDuplicate(current->Cert);
        }
    }
    ReleaseSRWLockExclusive(&CredLock);
    if (taken)
        return taken;
    WH_CRED *cred = NewCred(cert ? cert : carried, protocols, flags);
    if (carried)
        WhTlsCertFree(carried);
    if (!cred || !slot)
        return cred;
    InterlockedIncrement(&cred->Refs);
    AcquireSRWLockExclusive(&CredLock);
    WH_CRED *old = slot->Current;
    slot->Current = cred;
    ReleaseSRWLockExclusive(&CredLock);
    ReleaseCred(old);
    return cred;
}

void WhTlsSessionCleanup(WH_OBJECT *session)
{
    AcquireSRWLockExclusive(&CredLock);
    WH_CRED_SLOT *slot = session->CredSlots;
    session->CredSlots = nullptr;
    ReleaseSRWLockExclusive(&CredLock);
    while (slot) {
        WH_CRED_SLOT *next = slot->Next;
        ReleaseCred(slot->Current);
        WhFree(slot);
        slot = next;
    }
}

DWORD WhTlsHandshake(WH_OBJECT *session, WH_CONN *conn, LPCWSTR host, DWORD security_flags, DWORD protocols,
                     BOOL cert_set, const void *cert, int timeout, DWORD *failure)
{
    *failure = 0;
    InitOnceExecuteOnce(&TableOnce, LoadTable, nullptr, nullptr);
    if (!Table) {
        *failure = WINHTTP_CALLBACK_STATUS_FLAG_SECURITY_CHANNEL_ERROR;
        return ERROR_WINHTTP_SECURE_CHANNEL_ERROR;
    }
    conn->Raw = WhAlloc(WH_TLS_RAW);
    if (!conn->Raw)
        return ERROR_NOT_ENOUGH_MEMORY;
    conn->RawSize = WH_TLS_RAW;
    conn->RawUsed = 0;
    /* the winhttp protocol flags are the schannel client ones, among which mime speaks tls 1.2 and 1.3. */
    protocols &= SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_3_CLIENT;
    if (!protocols)
        protocols = SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_3_CLIENT;
    if (cert && !CertHasKey(cert)) {
        *failure = WINHTTP_CALLBACK_STATUS_FLAG_SECURITY_CHANNEL_ERROR;
        return ERROR_WINHTTP_CLIENT_CERT_NO_ACCESS_PRIVATE_KEY_;
    }
    DWORD flags = SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_IGNORE_NO_REVOCATION_CHECK | SCH_CRED_IGNORE_REVOCATION_OFFLINE |
                  ((security_flags & SECURITY_FLAG_IGNORE_ALL_CERT_ERRORS) ? SCH_CRED_MANUAL_CRED_VALIDATION
                                                                           : SCH_CRED_AUTO_CRED_VALIDATION);
    BOOL check = (security_flags & SECURITY_FLAG_IGNORE_ALL_CERT_ERRORS) &&
                 (security_flags & SECURITY_FLAG_IGNORE_ALL_CERT_ERRORS) != SECURITY_FLAG_IGNORE_ALL_CERT_ERRORS;
    conn->Cred = TakeCred(session, conn, cert, protocols, flags);
    if (!conn->Cred) {
        *failure = WINHTTP_CALLBACK_STATUS_FLAG_SECURITY_CHANNEL_ERROR;
        return ERROR_WINHTTP_SECURE_CHANNEL_ERROR;
    }
    conn->Credentials = conn->Cred->Handle;
    DWORD value = timeout <= 0 ? 0 : (DWORD)timeout;
    setsockopt(conn->Socket, SOL_SOCKET, SO_SNDTIMEO, (const char *)&value, sizeof(value));
    DWORD error = Negotiate(conn, host, TRUE, cert_set, timeout, failure);
    if (error) {
        if (!conn->Tls) {
            ReleaseCred(conn->Cred);
            conn->Cred = nullptr;
        } else
            WhTlsClose(conn);
        conn->Tls = FALSE;
        return error;
    }
    if (check && (error = VerifyServer(conn, host, security_flags, failure))) {
        WhTlsClose(conn);
        conn->Tls = FALSE;
        return error;
    }
    WH_STREAM_SIZES sizes = {0};
    if (Table->QueryContextAttributesW(&conn->Context, SECPKG_ATTR_STREAM_SIZES, &sizes) != SEC_E_OK) {
        WhTlsClose(conn);
        conn->Tls = FALSE;
        *failure = WINHTTP_CALLBACK_STATUS_FLAG_SECURITY_CHANNEL_ERROR;
        return ERROR_WINHTTP_SECURE_CHANNEL_ERROR;
    }
    conn->HeaderSize = sizes.cbHeader;
    conn->TrailerSize = sizes.cbTrailer;
    conn->MaxMessage = sizes.cbMaximumMessage ? sizes.cbMaximumMessage : 16384;
    conn->CipherStrength = 256;
    return ERROR_SUCCESS;
}

DWORD WhTlsSend(WH_CONN *conn, const void *data, DWORD size, int timeout)
{
    BYTE *record = WhAlloc(conn->HeaderSize + conn->MaxMessage + conn->TrailerSize);
    if (!record)
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD value = timeout <= 0 ? 0 : (DWORD)timeout;
    setsockopt(conn->Socket, SOL_SOCKET, SO_SNDTIMEO, (const char *)&value, sizeof(value));
    const BYTE *p = data;
    DWORD error = ERROR_SUCCESS;
    while (size && !error) {
        DWORD chunk = size > conn->MaxMessage ? conn->MaxMessage : size;
        memcpy(record + conn->HeaderSize, p, chunk);
        SecBuffer buffers[4] = {{conn->HeaderSize, SECBUFFER_STREAM_HEADER, record},
                                {chunk, SECBUFFER_DATA, record + conn->HeaderSize},
                                {conn->TrailerSize, SECBUFFER_STREAM_TRAILER, record + conn->HeaderSize + chunk},
                                {0, SECBUFFER_EMPTY, nullptr}};
        SecBufferDesc desc = {SECBUFFER_VERSION, 4, buffers};
        if (Table->EncryptMessage(&conn->Context, 0, &desc, 0) != SEC_E_OK) {
            error = ERROR_WINHTTP_SECURE_CHANNEL_ERROR;
            break;
        }
        DWORD total = buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;
        const char *q = (const char *)record;
        while (total) {
            int sent = send(conn->Socket, q, (int)total, 0);
            if (sent <= 0) {
                error = WSAGetLastError() == WSAETIMEDOUT ? ERROR_WINHTTP_TIMEOUT : ERROR_WINHTTP_CONNECTION_ERROR;
                break;
            }
            q += sent;
            total -= (DWORD)sent;
        }
        p += chunk;
        size -= chunk;
    }
    WhFree(record);
    return error;
}

/* decrypt at least one record into the plaintext buffer, or mark EOF. */
DWORD WhTlsFill(WH_CONN *conn, int timeout)
{
    while (TRUE) {
        if (conn->RawUsed) {
            SecBuffer buffers[4] = {{conn->RawUsed, SECBUFFER_DATA, conn->Raw},
                                    {0, SECBUFFER_EMPTY, nullptr},
                                    {0, SECBUFFER_EMPTY, nullptr},
                                    {0, SECBUFFER_EMPTY, nullptr}};
            SecBufferDesc desc = {SECBUFFER_VERSION, 4, buffers};
            SECURITY_STATUS status = Table->DecryptMessage(&conn->Context, &desc, 0, nullptr);
            if (status == SEC_E_OK || status == SEC_I_RENEGOTIATE || status == SEC_I_CONTEXT_EXPIRED) {
                DWORD extra = 0;
                BYTE *extra_data = nullptr;
                for (DWORD i = 0; i < 4; i++) {
                    if (buffers[i].BufferType == SECBUFFER_DATA && buffers[i].cbBuffer &&
                        !WhConnAppend(conn, buffers[i].pvBuffer, buffers[i].cbBuffer))
                        return ERROR_NOT_ENOUGH_MEMORY;
                    if (buffers[i].BufferType == SECBUFFER_EXTRA) {
                        extra = buffers[i].cbBuffer;
                        extra_data = buffers[i].pvBuffer;
                    }
                }
                if (extra)
                    memmove(conn->Raw, extra_data ? extra_data : conn->Raw + conn->RawUsed - extra, extra);
                conn->RawUsed = extra;
                if (status == SEC_I_CONTEXT_EXPIRED) {
                    conn->Eof = TRUE;
                    return ERROR_SUCCESS;
                }
                if (status == SEC_I_RENEGOTIATE) {
                    DWORD failure;
                    DWORD error = Negotiate(conn, nullptr, FALSE, TRUE, timeout, &failure);
                    if (error)
                        return error;
                }
                if (conn->End > conn->Start)
                    return ERROR_SUCCESS;
                continue;
            }
            /* a tls 1.3 server refuses the client's certificate after the handshake, as an alert of the first read. */
            if (status != SEC_E_INCOMPLETE_MESSAGE) {
                DWORD failure;
                return MapFailure(status, &failure);
            }
        }
        DWORD got;
        if (conn->RawUsed == conn->RawSize)
            return ERROR_WINHTTP_SECURE_CHANNEL_ERROR;
        DWORD error = WhConnRecv(conn, conn->Raw + conn->RawUsed, conn->RawSize - conn->RawUsed, &got, timeout);
        if (error)
            return error;
        if (!got) {
            conn->Eof = TRUE;
            return ERROR_SUCCESS;
        }
        conn->RawUsed += got;
    }
}

void WhTlsClose(WH_CONN *conn)
{
    if (!conn->Tls || !Table)
        return;
    Table->DeleteSecurityContext(&conn->Context);
    ReleaseCred(conn->Cred);
    conn->Cred = nullptr;
    conn->Tls = FALSE;
}
