/*
 * https through the schannel package of secur32, loaded at run time as windows' wininet does: the handshake,
 * records sealed with EncryptMessage and opened with DecryptMessage, and the validation of the server's
 * certificate through crypt32, where each SECURITY_FLAG_IGNORE_* flag lifts its own check.
 */
#include "wininet_private.h"

#define ISC_REQ_REPLAY_DETECT 0x00000004
#define ISC_REQ_SEQUENCE_DETECT 0x00000008
#define ISC_REQ_CONFIDENTIALITY 0x00000010
#define ISC_REQ_ALLOCATE_MEMORY 0x00000100
#define ISC_REQ_EXTENDED_ERROR 0x00004000
#define ISC_REQ_STREAM 0x00008000
#define ISC_REQ_MANUAL_CRED_VALIDATION 0x00080000

#define SCH_CRED_MANUAL_CRED_VALIDATION 0x00000008
#define SCH_CRED_NO_DEFAULT_CREDS 0x00000010

#define SP_PROT_TLS1_2_CLIENT 0x00000800
#define SP_PROT_TLS1_3_CLIENT 0x00002000

#define INET_TLS_RAW 65536

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
} INET_SCHANNEL_CRED;

typedef struct {
    ULONG cbHeader;
    ULONG cbTrailer;
    ULONG cbMaximumMessage;
    ULONG cBuffers;
    ULONG cbBlockSize;
} INET_STREAM_SIZES;

typedef struct {
    DWORD dwProtocol;
    ALG_ID aiCipher;
    DWORD dwCipherStrength;
    ALG_ID aiHash;
    DWORD dwHashStrength;
    ALG_ID aiExch;
    DWORD dwExchStrength;
} INET_CONNECTION_INFO;

static INIT_ONCE LoadOnce = INIT_ONCE_STATIC_INIT;
static PSecurityFunctionTableW Table;
static struct {
    typeof(CertGetCertificateChain) *GetChain;
    typeof(CertVerifyCertificateChainPolicy) *VerifyPolicy;
    typeof(CertFreeCertificateChain) *FreeChain;
    typeof(CertFreeCertificateContext) *FreeCertificate;
    typeof(CertDuplicateCertificateContext) *DuplicateCertificate;
    typeof(CertNameToStrW) *NameToStr;
    typeof(CertGetPublicKeyLength) *KeyLength;
} Crypt32;

static BOOL CALLBACK Load(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    HMODULE secur32 = LoadLibraryW(L"secur32.dll");
    PSecurityFunctionTableW(SEC_ENTRY * init)(void) =
        secur32 ? (PSecurityFunctionTableW(SEC_ENTRY *)(void))GetProcAddress(secur32, "InitSecurityInterfaceW")
                : nullptr;
    Table = init ? init() : nullptr;
    HMODULE crypt32 = LoadLibraryW(L"crypt32.dll");
    if (crypt32) {
        Crypt32.GetChain = (void *)GetProcAddress(crypt32, "CertGetCertificateChain");
        Crypt32.VerifyPolicy = (void *)GetProcAddress(crypt32, "CertVerifyCertificateChainPolicy");
        Crypt32.FreeChain = (void *)GetProcAddress(crypt32, "CertFreeCertificateChain");
        Crypt32.FreeCertificate = (void *)GetProcAddress(crypt32, "CertFreeCertificateContext");
        Crypt32.DuplicateCertificate = (void *)GetProcAddress(crypt32, "CertDuplicateCertificateContext");
        Crypt32.NameToStr = (void *)GetProcAddress(crypt32, "CertNameToStrW");
        Crypt32.KeyLength = (void *)GetProcAddress(crypt32, "CertGetPublicKeyLength");
    }
    return TRUE;
}

static void LoadLibraries(void)
{
    InitOnceExecuteOnce(&LoadOnce, Load, nullptr, nullptr);
}

static DWORD RawFill(INET_CONN *conn, DWORD timeout)
{
    if (conn->RawUsed == conn->RawSize)
        return ERROR_INTERNET_SECURITY_CHANNEL_ERROR;
    DWORD got;
    DWORD error = InetConnRecv(conn, conn->Raw + conn->RawUsed, conn->RawSize - conn->RawUsed, &got, timeout);
    if (error)
        return error;
    if (!got)
        return ERROR_INTERNET_CONNECTION_ABORTED;
    conn->RawUsed += got;
    return ERROR_SUCCESS;
}

static DWORD SendToken(INET_CONN *conn, SecBuffer *token)
{
    DWORD error = ERROR_SUCCESS;
    const char *p = token->pvBuffer;
    DWORD left = token->pvBuffer ? token->cbBuffer : 0;
    while (left && !error) {
        int sent = send(conn->Socket, p, (int)left, 0);
        if (sent <= 0) {
            error = ERROR_INTERNET_CONNECTION_ABORTED;
        } else {
            p += sent;
            left -= (DWORD)sent;
        }
    }
    if (token->pvBuffer)
        Table->FreeContextBuffer(token->pvBuffer);
    token->pvBuffer = nullptr;
    token->cbBuffer = 0;
    return error;
}

/* runs InitializeSecurityContext until the handshake ends, from the raw bytes already read. */
static DWORD Negotiate(INET_CONN *conn, LPCWSTR host, BOOL initial, DWORD timeout)
{
    ULONG flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_EXTENDED_ERROR |
                  ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM | ISC_REQ_MANUAL_CRED_VALIDATION;
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
            return ERROR_INTERNET_SECURITY_CHANNEL_ERROR;
        }
        first = FALSE;
        conn->Tls = TRUE;
        if (status == SEC_E_INCOMPLETE_MESSAGE) {
            DWORD error = RawFill(conn, timeout);
            if (error)
                return error;
            continue;
        }
        DWORD error = SendToken(conn, &out[0]);
        if (error)
            return error;
        if (status != SEC_E_OK && status != SEC_I_CONTINUE_NEEDED)
            return ERROR_INTERNET_SECURITY_CHANNEL_ERROR;
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

/* a failed check: its error, the _SECURITY_FLAG_CERT_* bit it reports and the flag that ignores it. */
typedef struct {
    HRESULT Status;
    DWORD Error;
    DWORD Bit;
    DWORD Ignore;
} INET_CERT_CHECK;

static const INET_CERT_CHECK Checks[] = {
    {CERT_E_UNTRUSTEDROOT, ERROR_INTERNET_INVALID_CA, _SECURITY_FLAG_CERT_INVALID_CA, SECURITY_FLAG_IGNORE_UNKNOWN_CA},
    {CERT_E_CHAINING, ERROR_INTERNET_INVALID_CA, _SECURITY_FLAG_CERT_INVALID_CA, SECURITY_FLAG_IGNORE_UNKNOWN_CA},
    {CERT_E_EXPIRED, ERROR_INTERNET_SEC_CERT_DATE_INVALID, _SECURITY_FLAG_CERT_DATE_INVALID,
     SECURITY_FLAG_IGNORE_CERT_DATE_INVALID},
    {CERT_E_VALIDITYPERIODNESTING, ERROR_INTERNET_SEC_CERT_DATE_INVALID, _SECURITY_FLAG_CERT_DATE_INVALID,
     SECURITY_FLAG_IGNORE_CERT_DATE_INVALID},
    {CERT_E_CN_NO_MATCH, ERROR_INTERNET_SEC_CERT_CN_INVALID, _SECURITY_FLAG_CERT_CN_INVALID,
     SECURITY_FLAG_IGNORE_CERT_CN_INVALID},
    {CERT_E_WRONG_USAGE, ERROR_INTERNET_SEC_INVALID_CERT, 0, SECURITY_FLAG_IGNORE_WRONG_USAGE},
    {CRYPT_E_NO_REVOCATION_CHECK, ERROR_INTERNET_SEC_CERT_REV_FAILED, _SECURITY_FLAG_CERT_REV_FAILED,
     SECURITY_FLAG_IGNORE_REVOCATION},
    {CRYPT_E_REVOCATION_OFFLINE, ERROR_INTERNET_SEC_CERT_REV_FAILED, _SECURITY_FLAG_CERT_REV_FAILED,
     SECURITY_FLAG_IGNORE_REVOCATION},
};

/*
 * validates the server's chain for host. the first check the flags do not lift fails the request with its
 * error; every failed check is reported in the security flags.
 */
static DWORD Validate(INET_CONN *conn, LPCWSTR host, DWORD security_flags)
{
    if (!Crypt32.GetChain || !Crypt32.VerifyPolicy || !Crypt32.FreeChain)
        return ERROR_INTERNET_SEC_INVALID_CERT;
    static char server_auth[] = szOID_PKIX_KP_SERVER_AUTH;
    LPSTR usages[] = {server_auth};
    CERT_CHAIN_PARA para = {sizeof(para)};
    para.RequestedUsage.dwType = USAGE_MATCH_TYPE_OR;
    para.RequestedUsage.Usage.cUsageIdentifier = 1;
    para.RequestedUsage.Usage.rgpszUsageIdentifier = usages;
    PCCERT_CHAIN_CONTEXT chain = nullptr;
    if (!Crypt32.GetChain(nullptr, conn->Certificate, nullptr, conn->Certificate->hCertStore, &para, 0, nullptr,
                          &chain))
        return ERROR_INTERNET_SEC_INVALID_CERT;
    /* an ip literal has no brackets in the certificate. */
    WCHAR name[INTERNET_MAX_HOST_NAME_LENGTH];
    lstrcpynW(name, host, ARRAYSIZE(name));
    DWORD length = (DWORD)lstrlenW(name);
    if (name[0] == '[' && length > 2 && name[length - 1] == ']') {
        memmove(name, name + 1, (length - 2) * sizeof(WCHAR));
        name[length - 2] = 0;
    }
    DWORD ignored = 0, error = ERROR_SUCCESS;
    for (DWORD round = 0; round < ARRAYSIZE(Checks) + 1; round++) {
        SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl = {0};
        ssl.cbSize = sizeof(ssl);
        ssl.dwAuthType = AUTHTYPE_SERVER;
        ssl.fdwChecks = ignored | SECURITY_FLAG_IGNORE_REVOCATION;
        ssl.pwszServerName = name;
        CERT_CHAIN_POLICY_PARA policy = {sizeof(policy)};
        policy.pvExtraPolicyPara = &ssl;
        CERT_CHAIN_POLICY_STATUS status = {sizeof(status)};
        if (!Crypt32.VerifyPolicy(CERT_CHAIN_POLICY_SSL, chain, &policy, &status)) {
            error = ERROR_INTERNET_SEC_INVALID_CERT;
            break;
        }
        if (!status.dwError)
            break;
        const INET_CERT_CHECK *check = nullptr;
        for (DWORD i = 0; i < ARRAYSIZE(Checks); i++)
            if (Checks[i].Status == (HRESULT)status.dwError)
                check = &Checks[i];
        if (!check || ignored & check->Ignore) {
            if (!error)
                error = status.dwError == (DWORD)CRYPT_E_REVOKED ? ERROR_INTERNET_SEC_CERT_REVOKED
                                                                  : ERROR_INTERNET_SEC_INVALID_CERT;
            break;
        }
        conn->CertErrors |= check->Bit;
        if (!(security_flags & check->Ignore) && !error)
            error = check->Error;
        ignored |= check->Ignore;
    }
    Crypt32.FreeChain(chain);
    return error;
}

DWORD InetTlsHandshake(INET_CONN *conn, LPCWSTR host, DWORD security_flags, DWORD timeout)
{
    LoadLibraries();
    if (!Table)
        return ERROR_INTERNET_SECURITY_CHANNEL_ERROR;
    conn->Raw = InetAlloc(INET_TLS_RAW);
    if (!conn->Raw)
        return ERROR_NOT_ENOUGH_MEMORY;
    conn->RawSize = INET_TLS_RAW;
    conn->RawUsed = 0;
    INET_SCHANNEL_CRED credentials = {0};
    credentials.dwVersion = SCHANNEL_CRED_VERSION;
    credentials.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_3_CLIENT;
    credentials.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_MANUAL_CRED_VALIDATION;
    SECURITY_STATUS status = Table->AcquireCredentialsHandleW(nullptr, (SEC_WCHAR *)UNISP_NAME_W, SECPKG_CRED_OUTBOUND,
                                                              nullptr, &credentials, nullptr, nullptr,
                                                              &conn->Credentials, nullptr);
    if (status != SEC_E_OK)
        return ERROR_INTERNET_SECURITY_CHANNEL_ERROR;
    /* sni carries names only, so an ip literal goes without one. */
    BOOL literal = host[0] == '[';
    if (!literal) {
        literal = TRUE;
        for (LPCWSTR p = host; *p; p++)
            if (!((*p >= '0' && *p <= '9') || *p == '.'))
                literal = FALSE;
    }
    DWORD error = Negotiate(conn, literal ? nullptr : host, TRUE, timeout);
    if (error) {
        if (!conn->Tls)
            Table->FreeCredentialsHandle(&conn->Credentials);
        else
            InetTlsClose(conn);
        conn->Tls = FALSE;
        return error;
    }
    INET_STREAM_SIZES sizes = {0};
    if (Table->QueryContextAttributesW(&conn->Context, SECPKG_ATTR_STREAM_SIZES, &sizes) != SEC_E_OK) {
        InetTlsClose(conn);
        return ERROR_INTERNET_SECURITY_CHANNEL_ERROR;
    }
    conn->HeaderSize = sizes.cbHeader;
    conn->TrailerSize = sizes.cbTrailer;
    conn->MaxMessage = sizes.cbMaximumMessage ? sizes.cbMaximumMessage : 16384;
    INET_CONNECTION_INFO info = {0};
    conn->CipherStrength = 128;
    if (Table->QueryContextAttributesW(&conn->Context, SECPKG_ATTR_CONNECTION_INFO, &info) == SEC_E_OK) {
        conn->Protocol = info.dwProtocol;
        if (info.dwCipherStrength)
            conn->CipherStrength = info.dwCipherStrength;
    }
    PCCERT_CONTEXT certificate = nullptr;
    if (Table->QueryContextAttributesW(&conn->Context, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &certificate) != SEC_E_OK ||
        !certificate) {
        InetTlsClose(conn);
        return ERROR_INTERNET_SEC_INVALID_CERT;
    }
    conn->Certificate = certificate;
    if ((error = Validate(conn, host, security_flags))) {
        InetTlsClose(conn);
        return error;
    }
    return ERROR_SUCCESS;
}

DWORD InetTlsSend(INET_CONN *conn, const void *data, DWORD size, DWORD timeout)
{
    BYTE *record = InetAlloc(conn->HeaderSize + conn->MaxMessage + conn->TrailerSize);
    if (!record)
        return ERROR_NOT_ENOUGH_MEMORY;
    DWORD value = !timeout || timeout == INFINITE ? 0 : timeout;
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
            error = ERROR_INTERNET_SECURITY_CHANNEL_ERROR;
            break;
        }
        DWORD total = buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;
        const char *q = (const char *)record;
        while (total) {
            int sent = send(conn->Socket, q, (int)total, 0);
            if (sent <= 0) {
                error = WSAGetLastError() == WSAETIMEDOUT ? ERROR_INTERNET_TIMEOUT : ERROR_INTERNET_CONNECTION_ABORTED;
                break;
            }
            q += sent;
            total -= (DWORD)sent;
        }
        p += chunk;
        size -= chunk;
    }
    InetFree(record);
    return error;
}

/* decrypts at least one record into the plaintext buffer, or marks the end of the stream. */
DWORD InetTlsFill(INET_CONN *conn, DWORD timeout)
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
                DWORD before = InetConnBuffered(conn), extra = 0;
                BYTE *extra_data = nullptr;
                for (DWORD i = 0; i < 4; i++) {
                    if (buffers[i].BufferType == SECBUFFER_DATA && buffers[i].cbBuffer &&
                        !InetConnAppend(conn, buffers[i].pvBuffer, buffers[i].cbBuffer))
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
                    DWORD error = Negotiate(conn, nullptr, FALSE, timeout);
                    if (error)
                        return error;
                }
                if (InetConnBuffered(conn) > before)
                    return ERROR_SUCCESS;
                continue;
            }
            if (status != SEC_E_INCOMPLETE_MESSAGE)
                return ERROR_INTERNET_SECURITY_CHANNEL_ERROR;
        }
        if (conn->RawUsed == conn->RawSize)
            return ERROR_INTERNET_SECURITY_CHANNEL_ERROR;
        DWORD got;
        DWORD error = InetConnRecv(conn, conn->Raw + conn->RawUsed, conn->RawSize - conn->RawUsed, &got, timeout);
        if (error)
            return error;
        if (!got) {
            conn->Eof = TRUE;
            return ERROR_SUCCESS;
        }
        conn->RawUsed += got;
    }
}

void InetTlsClose(INET_CONN *conn)
{
    if (conn->Certificate && Crypt32.FreeCertificate)
        Crypt32.FreeCertificate(conn->Certificate);
    conn->Certificate = nullptr;
    if (!conn->Tls || !Table)
        return;
    Table->DeleteSecurityContext(&conn->Context);
    Table->FreeCredentialsHandle(&conn->Credentials);
    conn->Tls = FALSE;
}

PCCERT_CONTEXT InetCertificateDuplicate(PCCERT_CONTEXT certificate)
{
    LoadLibraries();
    return certificate && Crypt32.DuplicateCertificate ? Crypt32.DuplicateCertificate(certificate) : nullptr;
}

void InetCertificateFree(PCCERT_CONTEXT certificate)
{
    if (certificate && Crypt32.FreeCertificate)
        Crypt32.FreeCertificate(certificate);
}

static LPWSTR LocalString(LPCWSTR text)
{
    LPWSTR copy = LocalAlloc(LMEM_FIXED, ((SIZE_T)lstrlenW(text) + 1) * sizeof(WCHAR));
    if (copy)
        lstrcpyW(copy, text);
    return copy;
}

/* a certificate name with its parts on lines of their own. */
static LPWSTR NameText(PCERT_NAME_BLOB name)
{
    if (!Crypt32.NameToStr)
        return nullptr;
    DWORD type = CERT_SIMPLE_NAME_STR | CERT_NAME_STR_CRLF_FLAG;
    DWORD chars = Crypt32.NameToStr(X509_ASN_ENCODING, name, type, nullptr, 0);
    LPWSTR text = chars ? LocalAlloc(LMEM_FIXED, chars * sizeof(WCHAR)) : nullptr;
    if (text)
        Crypt32.NameToStr(X509_ASN_ENCODING, name, type, text, chars);
    return text;
}

BOOL InetCertificateInfo(PCCERT_CONTEXT certificate, DWORD strength, INTERNET_CERTIFICATE_INFOW *info)
{
    LoadLibraries();
    memset(info, 0, sizeof(*info));
    const CERT_INFO *cert = certificate->pCertInfo;
    info->ftStart = cert->NotBefore;
    info->ftExpiry = cert->NotAfter;
    info->lpszSubjectInfo = NameText((PCERT_NAME_BLOB)&cert->Subject);
    info->lpszIssuerInfo = NameText((PCERT_NAME_BLOB)&cert->Issuer);
    info->dwKeySize = strength;
    /* windows leaves the algorithm names empty. */
    info->lpszProtocolName = nullptr;
    info->lpszSignatureAlgName = nullptr;
    info->lpszEncryptionAlgName = nullptr;
    if (!info->lpszSubjectInfo)
        info->lpszSubjectInfo = LocalString(L"");
    if (!info->lpszIssuerInfo)
        info->lpszIssuerInfo = LocalString(L"");
    return info->lpszSubjectInfo && info->lpszIssuerInfo;
}
