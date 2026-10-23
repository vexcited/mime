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

typedef struct {
    DWORD dwVersion;
    DWORD cCreds;
    void *paCred;
    void *hRootStore;
    DWORD cMappers;
    void *aphMappers;
    DWORD cSupportedAlgs;
    void *palgSupportedAlgs;
    DWORD grbitEnabledProtocols;
    DWORD dwMinimumCipherStrength;
    DWORD dwMaximumCipherStrength;
    DWORD dwSessionLifespan;
    DWORD dwFlags;
    DWORD dwCredFormat;
} WH_SCHANNEL_CRED;

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
static DWORD Negotiate(WH_CONN *conn, LPCWSTR host, BOOL initial, int timeout, DWORD *failure)
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
} Crypt;

static BOOL CALLBACK LoadCrypt(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    HMODULE module = LoadLibraryW(L"crypt32.dll");
    if (module) {
        Crypt.GetChain = (void *)GetProcAddress(module, "CertGetCertificateChain");
        Crypt.FreeChain = (void *)GetProcAddress(module, "CertFreeCertificateChain");
        Crypt.VerifyPolicy = (void *)GetProcAddress(module, "CertVerifyCertificateChainPolicy");
        Crypt.FreeContext = (void *)GetProcAddress(module, "CertFreeCertificateContext");
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

DWORD WhTlsHandshake(WH_CONN *conn, LPCWSTR host, DWORD security_flags, int timeout, DWORD *failure)
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
    WH_SCHANNEL_CRED credentials = {0};
    credentials.dwVersion = SCHANNEL_CRED_VERSION;
    credentials.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_3_CLIENT;
    credentials.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_IGNORE_NO_REVOCATION_CHECK |
                          SCH_CRED_IGNORE_REVOCATION_OFFLINE |
                          ((security_flags & SECURITY_FLAG_IGNORE_ALL_CERT_ERRORS) ? SCH_CRED_MANUAL_CRED_VALIDATION
                                                                                    : SCH_CRED_AUTO_CRED_VALIDATION);
    BOOL check = (security_flags & SECURITY_FLAG_IGNORE_ALL_CERT_ERRORS) &&
                 (security_flags & SECURITY_FLAG_IGNORE_ALL_CERT_ERRORS) != SECURITY_FLAG_IGNORE_ALL_CERT_ERRORS;
    SECURITY_STATUS status = Table->AcquireCredentialsHandleW(nullptr, (SEC_WCHAR *)UNISP_NAME_W, SECPKG_CRED_OUTBOUND,
                                                              nullptr, &credentials, nullptr, nullptr,
                                                              &conn->Credentials, nullptr);
    if (status != SEC_E_OK) {
        *failure = WINHTTP_CALLBACK_STATUS_FLAG_SECURITY_CHANNEL_ERROR;
        return ERROR_WINHTTP_SECURE_CHANNEL_ERROR;
    }
    DWORD value = timeout <= 0 ? 0 : (DWORD)timeout;
    setsockopt(conn->Socket, SOL_SOCKET, SO_SNDTIMEO, (const char *)&value, sizeof(value));
    DWORD error = Negotiate(conn, host, TRUE, timeout, failure);
    if (error) {
        if (!conn->Tls)
            Table->FreeCredentialsHandle(&conn->Credentials);
        else
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
                    DWORD error = Negotiate(conn, nullptr, FALSE, timeout, &failure);
                    if (error)
                        return error;
                }
                if (conn->End > conn->Start)
                    return ERROR_SUCCESS;
                continue;
            }
            if (status != SEC_E_INCOMPLETE_MESSAGE)
                return ERROR_WINHTTP_SECURE_CHANNEL_ERROR;
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
    Table->FreeCredentialsHandle(&conn->Credentials);
    conn->Tls = FALSE;
}
