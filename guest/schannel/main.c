/*
 * schannel entry point and its own exports.
 *
 * the security support provider interface of schannel is forwarded to
 * secur32 by schannel.def. what remains are the lsa package entry points,
 * which only lsa calls and mime has no lsa to host, and the Ssl helpers.
 * tls itself is not implemented, so the helpers that need certificates or
 * the session cache fail; the ClientHello parsers and the constant answers
 * are real.
 */
#include <mime/win32/sspi.h>
#include <mime/win32/winnls.h>
#include <stdbool.h>

#define SECPKG_INTERFACE_VERSION 0x00010000

/* the certificate SslCrackCertificate returns, from schannel.h. */
typedef struct _X509Certificate {
    DWORD Version;
    DWORD SerialNumber[4];
    DWORD SignatureAlgorithm;
    FILETIME ValidFrom;
    FILETIME ValidUntil;
    PSTR pszIssuer;
    PSTR pszSubject;
    PVOID pPublicKey;
} X509Certificate, *PX509Certificate;

typedef struct _CRYPTOAPI_BLOB {
    DWORD cbData;
    BYTE *pbData;
} CRYPT_DATA_BLOB;

typedef struct _SCH_EXTENSION_DATA {
    WORD ExtensionType;
    const BYTE *pExtData;
    DWORD cbExtData;
} SCH_EXTENSION_DATA;

#define SCH_EXTENSIONS_OPTIONS_NONE 0
#define SCH_NO_RECORD_HEADER 1
#define TLS_HANDSHAKE 22
#define TLS_CLIENT_HELLO 1
#define TLS_SERVER_NAME 0

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return TRUE;
}

/* ------------------------------------------------------------------ lsa package entry points */

/* lsa loads schannel with this to get its two package tables; mime runs no lsa. */
__declspec(dllexport) NTSTATUS NTAPI SpLsaModeInitialize(ULONG lsa_version, PULONG package_version, PVOID *tables,
                                                         PULONG count)
{
    if (count)
        *count = 0;
    return STATUS_NOT_SUPPORTED;
}

/* the user mode half that runs in the client process after lsa established a context. */
__declspec(dllexport) NTSTATUS NTAPI SpUserModeInitialize(ULONG lsa_version, PULONG package_version, PVOID *tables,
                                                          PULONG count)
{
    if (!package_version || !tables || !count || lsa_version != SECPKG_INTERFACE_VERSION)
        return STATUS_INVALID_PARAMETER;
    return STATUS_NOT_SUPPORTED;
}

/* ------------------------------------------------------------------ session cache */

/*
 * SslEmptyCacheW asks the schannel package in lsa through LsaConnectUntrusted
 * and stores the nt status of a failure as the last error. without lsa the
 * connection fails the way it does there: the event lsa sets when it accepts
 * clients does not exist.
 */
__declspec(dllexport) BOOL WINAPI SslEmptyCacheW(LPWSTR target, DWORD flags)
{
    static const WCHAR ready[] = L"\\SECURITY\\LSA_AUTHENTICATION_INITIALIZED";
    UNICODE_STRING name = {sizeof(ready) - sizeof(WCHAR), sizeof(ready), (PWSTR)ready};
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    HANDLE event;
    NTSTATUS status = NtOpenEvent(&event, SYNCHRONIZE, &oa);
    if (NT_SUCCESS(status)) {
        NtClose(event);
        status = STATUS_NOT_SUPPORTED;
    }
    SetLastError((DWORD)status);
    return FALSE;
}

__declspec(dllexport) BOOL WINAPI SslEmptyCacheA(LPSTR target, DWORD flags)
{
    PWSTR wide = NULL;
    if (target) {
        int n = MultiByteToWideChar(CP_ACP, 0, target, -1, NULL, 0);
        wide = n > 0 ? LocalAlloc(LMEM_FIXED, n * sizeof(WCHAR)) : NULL;
        if (!wide) {
            SetLastError((DWORD)SEC_E_INSUFFICIENT_MEMORY);
            return FALSE;
        }
        MultiByteToWideChar(CP_ACP, 0, target, -1, wide, n);
    }
    BOOL ok = SslEmptyCacheW(wide, flags);
    if (wide) {
        DWORD error = GetLastError();
        LocalFree(wide);
        SetLastError(error);
    }
    return ok;
}

/* ------------------------------------------------------------------ certificates and keys */

/* decoding a certificate needs crypt32's certificate parser, which schannel cannot use yet. */
__declspec(dllexport) BOOL WINAPI SslCrackCertificate(PUCHAR certificate, DWORD size, DWORD flags,
                                                      PX509Certificate *result)
{
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}

/* schannel allocates the certificate and its key with LocalAlloc. */
__declspec(dllexport) void WINAPI SslFreeCertificate(PX509Certificate certificate)
{
    if (certificate) {
        LocalFree(certificate->pPublicKey);
        LocalFree(certificate);
    }
}

/* schannel returns FALSE without doing anything. */
__declspec(dllexport) BOOL WINAPI SslLoadCertificate(PUCHAR certificate, DWORD size, BOOL unused)
{
    return FALSE;
}

__declspec(dllexport) DWORD WINAPI SslGetMaximumKeySize(DWORD reserved)
{
    return 2048;
}

/* mime has no system random generator yet: a generator seeded from the performance counter, not for keys. */
__declspec(dllexport) void WINAPI SslGenerateRandomBits(PUCHAR data, LONG size)
{
    static ULONG seed;
    LARGE_INTEGER counter;
    NtQueryPerformanceCounter(&counter, NULL);
    ULONG s = seed ^ counter.LowPart ^ (ULONG)(ULONG_PTR)&counter;
    for (LONG i = 0; data && i < size; i++)
        data[i] = (UCHAR)(RtlRandomEx(&s) >> 7);
    seed = s;
}

/* lsa calls this on its own context records; any other caller gets FALSE, as for an unknown package id. */
__declspec(dllexport) BOOLEAN WINAPI SslFreeCustomBuffer(PVOID context, PVOID package, PVOID buffer, BOOLEAN remove)
{
    return FALSE;
}

/* the store is a serialized crypt32 store, which schannel cannot open yet. */
__declspec(dllexport) SECURITY_STATUS WINAPI SslDeserializeCertificateStore(CRYPT_DATA_BLOB store,
                                                                            const void **certificate)
{
    if (!certificate)
        return SEC_E_INVALID_PARAMETER;
    if (!store.pbData)
        return SEC_E_NO_CREDENTIALS;
    return SEC_E_UNSUPPORTED_FUNCTION;
}

/* ------------------------------------------------------------------ ClientHello parsing */

typedef struct READER {
    const BYTE *Data;
    DWORD Left;
} READER;

static bool take(READER *r, DWORD n, const BYTE **out)
{
    if (r->Left < n)
        return false;
    if (out)
        *out = r->Data;
    r->Data += n;
    r->Left -= n;
    return true;
}

static bool take_vector(READER *r, DWORD width, READER *out)
{
    const BYTE *p;
    if (!take(r, width, &p))
        return false;
    DWORD n = 0;
    for (DWORD i = 0; i < width; i++)
        n = n << 8 | p[i];
    out->Data = r->Data;
    out->Left = n;
    return take(r, n, NULL);
}

/* the extension block of a ClientHello handshake message, false when malformed. */
static bool hello_extensions(const BYTE *message, DWORD size, READER *extensions)
{
    READER r = {message, size}, body, skip;
    const BYTE *type;
    if (!take(&r, 1, &type) || *type != TLS_CLIENT_HELLO || !take_vector(&r, 3, &body))
        return false;
    /* version and random, then the session id, cipher suites and compression methods. */
    if (!take(&body, 34, NULL) || !take_vector(&body, 1, &skip) || !take_vector(&body, 2, &skip) ||
        !take_vector(&body, 1, &skip))
        return false;
    extensions->Data = NULL;
    extensions->Left = 0;
    if (!body.Left)
        return true;
    return take_vector(&body, 2, extensions);
}

/* the ClientHello message of the records, with the bytes still missing when it is cut short. */
static SECURITY_STATUS hello_message(const BYTE *data, DWORD size, DWORD options, const BYTE **message,
                                     DWORD *length, DWORD *missing)
{
    *missing = 0;
    if (options & SCH_NO_RECORD_HEADER) {
        *message = data;
        *length = size;
        return SEC_E_OK;
    }
    if (size < 5) {
        *missing = 5 - size;
        return SEC_E_INCOMPLETE_MESSAGE;
    }
    if (data[0] < 20 || data[0] > 23)
        return SEC_E_INVALID_TOKEN;
    DWORD record = (DWORD)data[3] << 8 | data[4];
    if (record + 5 > size) {
        *missing = record + 5 - size;
        return SEC_E_INCOMPLETE_MESSAGE;
    }
    *message = data[0] == TLS_HANDSHAKE ? data + 5 : NULL;
    *length = data[0] == TLS_HANDSHAKE ? record : 0;
    return SEC_E_OK;
}

static bool next_extension(READER *extensions, WORD *type, READER *data)
{
    const BYTE *p;
    if (!take(extensions, 2, &p))
        return false;
    *type = (WORD)(p[0] << 8 | p[1]);
    return take_vector(extensions, 2, data);
}

/* the host name of the server name extension of a ClientHello, NULL without one. */
__declspec(dllexport) SECURITY_STATUS WINAPI SslGetServerIdentity(PBYTE hello, DWORD size, PBYTE *identity,
                                                                  PDWORD identity_size, DWORD flags)
{
    if (!hello || !identity || !identity_size)
        return SEC_E_INVALID_PARAMETER;
    if (size < 5) {
        *identity_size = 5 - size;
        return SEC_E_INCOMPLETE_MESSAGE;
    }
    if (hello[0] != TLS_HANDSHAKE) {
        /* an ssl 2 hello carries no server name. */
        if (!(hello[0] & 0x80) || hello[2] != TLS_CLIENT_HELLO)
            return SEC_E_INVALID_TOKEN;
        *identity_size = 0;
        return SEC_E_OK;
    }
    const BYTE *message;
    DWORD length, missing;
    SECURITY_STATUS status = hello_message(hello, size, 0, &message, &length, &missing);
    if (status != SEC_E_OK) {
        if (status == SEC_E_INCOMPLETE_MESSAGE)
            *identity_size = missing;
        return status;
    }
    READER extensions, data, list, name;
    if (length >= 4 && ((DWORD)message[1] << 16 | message[2] << 8 | message[3]) + 4 > length) {
        *identity_size = ((DWORD)message[1] << 16 | message[2] << 8 | message[3]) + 4 - length;
        return SEC_E_INCOMPLETE_MESSAGE;
    }
    if (!hello_extensions(message, length, &extensions))
        return SEC_E_INVALID_TOKEN;
    *identity = NULL;
    *identity_size = 0;
    WORD type;
    while (next_extension(&extensions, &type, &data)) {
        if (type != TLS_SERVER_NAME)
            continue;
        const BYTE *kind;
        if (take_vector(&data, 2, &list) && take(&list, 1, &kind) && *kind == 0 && take_vector(&list, 2, &name)) {
            *identity = (PBYTE)name.Data;
            *identity_size = name.Left;
        }
        break;
    }
    return SEC_E_OK;
}

/* the extensions of the requested types in a ClientHello, pointing into it; absent ones stay empty. */
__declspec(dllexport) SECURITY_STATUS WINAPI SslGetExtensions(const BYTE *hello, DWORD size,
                                                              SCH_EXTENSION_DATA *wanted, BYTE count, DWORD *missing,
                                                              DWORD options)
{
    if (!hello || !wanted || !count || !missing || (options & ~SCH_NO_RECORD_HEADER))
        return SEC_E_INVALID_PARAMETER;
    if (!(options & SCH_NO_RECORD_HEADER) && size >= 5 && (hello[1] == 0xFE && (hello[2] == 0xFD || hello[2] == 0xFF)))
        return SEC_E_ILLEGAL_MESSAGE;
    const BYTE *message;
    DWORD length;
    SECURITY_STATUS status = hello_message(hello, size, options, &message, &length, missing);
    if (status != SEC_E_OK)
        return status;
    *missing = 0;
    for (BYTE i = 0; i < count; i++) {
        wanted[i].pExtData = NULL;
        wanted[i].cbExtData = 0;
    }
    if (!message)
        return SEC_E_OK;
    READER extensions, data;
    if (!hello_extensions(message, length, &extensions))
        return SEC_E_ILLEGAL_MESSAGE;
    WORD type;
    while (next_extension(&extensions, &type, &data)) {
        for (BYTE i = 0; i < count; i++) {
            if (wanted[i].ExtensionType == type && !wanted[i].pExtData) {
                wanted[i].pExtData = data.Data;
                wanted[i].cbExtData = data.Left;
            }
        }
    }
    return SEC_E_OK;
}

/* copying extensions sits behind a feature switch of schannel and needs its tls engine. */
__declspec(dllexport) SECURITY_STATUS WINAPI SslCopyTlsExtensions(const BYTE *hello, DWORD size, PVOID extensions,
                                                                  BYTE count, DWORD *missing, DWORD options)
{
    if (!hello || !extensions || !count || !missing || (options & ~SCH_NO_RECORD_HEADER))
        return SEC_E_INVALID_PARAMETER;
    *missing = 0;
    return SEC_E_UNSUPPORTED_FUNCTION;
}
