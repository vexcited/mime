/*
 * schannel entry point and its own exports.
 *
 * the security support provider interface of schannel is forwarded to
 * secur32 by schannel.def. what remains are the lsa package entry points,
 * which only lsa calls and mime has no lsa to host, and the Ssl helpers.
 * the tls client lives in secur32 with the interface; the helpers that need
 * certificates or the session cache fail, while the ClientHello parsers,
 * the random bits and the constant answers are real.
 */
#include <mime/win32/sspi.h>
#include <mime/win32/winnls.h>

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
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE, nullptr, nullptr);
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
    PWSTR wide = nullptr;
    if (target) {
        int n = MultiByteToWideChar(CP_ACP, 0, target, -1, nullptr, 0);
        wide = n > 0 ? LocalAlloc(LMEM_FIXED, n * sizeof(WCHAR)) : nullptr;
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

typedef BOOLEAN(WINAPI *RTL_GEN_RANDOM)(PVOID, ULONG);

/* advapi32's generator, which reads the kernel's cng device, as schannel's comes from bcryptprimitives. */
__declspec(dllexport) void WINAPI SslGenerateRandomBits(PUCHAR data, LONG size)
{
    static RTL_GEN_RANDOM gen_random;
    if (!data || size <= 0)
        return;
    if (!gen_random) {
        HMODULE advapi32 = LoadLibraryW(L"advapi32.dll");
        gen_random = advapi32 ? (RTL_GEN_RANDOM)(void *)GetProcAddress(advapi32, "SystemFunction036") : nullptr;
    }
    if (gen_random && gen_random(data, (ULONG)size))
        return;
    /* without it, a generator seeded from the performance counter, not fit for keys. */
    static ULONG seed;
    LARGE_INTEGER counter;
    NtQueryPerformanceCounter(&counter, nullptr);
    ULONG s = seed ^ counter.LowPart ^ (ULONG)(ULONG_PTR)&counter;
    for (LONG i = 0; i < size; i++)
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
    return take(r, n, nullptr);
}

/* the extension block of a ClientHello handshake message, false when malformed. */
static bool hello_extensions(const BYTE *message, DWORD size, READER *extensions)
{
    READER r = {message, size}, body, skip;
    const BYTE *type;
    if (!take(&r, 1, &type) || *type != TLS_CLIENT_HELLO || !take_vector(&r, 3, &body))
        return false;
    /* version and random, then the session id, cipher suites and compression methods. */
    if (!take(&body, 34, nullptr) || !take_vector(&body, 1, &skip) || !take_vector(&body, 2, &skip) ||
        !take_vector(&body, 1, &skip))
        return false;
    extensions->Data = nullptr;
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
    *message = data[0] == TLS_HANDSHAKE ? data + 5 : nullptr;
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
    *identity = nullptr;
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
        wanted[i].pExtData = nullptr;
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

/* ------------------------------------------------------------------ SslCopyTlsExtensions */

/* SCH_COPY_EXTS_DATA as schannel fills it: whether the extension came, its size, then the caller's buffer. */
typedef struct _SCH_COPY_EXTS_DATA {
    WORD ExtensionType;
    BYTE Found;
    BYTE Reserved;
    WORD ExtensionSize;
    WORD BufferSize;
    PBYTE Buffer;
} SCH_COPY_EXTS_DATA;

/* schannel's defaults for MessageLimitServer and MessageLimitServerClientAuth. */
#define MESSAGE_LIMIT_SERVER 0x4000
#define MESSAGE_LIMIT_SERVER_CLIENT_AUTH 0x8000

/* reads a handshake message across the records that carry it, like schannel's TlsRecordParser. */
typedef struct RECORDS {
    const BYTE *Data;
    DWORD Left;
    DWORD Count;
    WORD RecordLeft;
    bool Multiple;
    DWORD Missing;
} RECORDS;

static SECURITY_STATUS next_record(RECORDS *r)
{
    if (r->Left < 5) {
        r->Missing = 5 - r->Left;
        return SEC_E_INCOMPLETE_MESSAGE;
    }
    const BYTE *h = r->Data;
    if (h[0] < 20 || h[0] > 23)
        return SEC_E_ILLEGAL_MESSAGE;
    WORD version = (WORD)(h[1] << 8 | h[2]);
    if (version == 0xFEFD || version == 0xFEFF)
        return SEC_E_ILLEGAL_MESSAGE;
    WORD length = (WORD)(h[3] << 8 | h[4]);
    if (length + 5u > r->Left) {
        r->Missing = length + 5 - r->Left;
        return SEC_E_INCOMPLETE_MESSAGE;
    }
    if (h[0] != TLS_HANDSHAKE || !length || (++r->Count > 1 && !r->Multiple))
        return SEC_E_ILLEGAL_MESSAGE;
    r->Data += 5;
    r->Left -= 5;
    r->RecordLeft = length;
    return SEC_E_OK;
}

/* size bytes of the message into out, or skipped without out. */
static SECURITY_STATUS read_field(RECORDS *r, BYTE *out, DWORD size)
{
    while (size) {
        if (!r->RecordLeft) {
            SECURITY_STATUS status = next_record(r);
            if (status)
                return status;
            continue;
        }
        DWORD n = size < r->RecordLeft ? size : r->RecordLeft;
        if (n > r->Left) {
            r->Missing = n - r->Left;
            return SEC_E_INCOMPLETE_MESSAGE;
        }
        if (out) {
            memcpy(out, r->Data, n);
            out += n;
        }
        r->Data += n;
        r->Left -= n;
        r->RecordLeft -= (WORD)n;
        size -= n;
    }
    return SEC_E_OK;
}

static SECURITY_STATUS read_number(RECORDS *r, DWORD width, DWORD *value)
{
    BYTE bytes[3];
    SECURITY_STATUS status = read_field(r, bytes, width);
    *value = 0;
    for (DWORD i = 0; !status && i < width; i++)
        *value = *value << 8 | bytes[i];
    return status;
}

/* the extension types schannel knows, which a ClientHello may carry once each; -1 for the others. */
static int known_extension(WORD type)
{
    static const WORD known[] = {0,  1,  2,  3,  4,  5,  6,  10, 11, 13, 14, 16,     23,
                                 24, 35, 41, 43, 44, 45, 47, 49, 51, 57, 0xFF01, 0xFFA5};
    for (int i = 0; i < (int)RTL_NUMBER_OF(known); i++)
        if (known[i] == type)
            return i;
    return -1;
}

static SECURITY_STATUS copy_extensions(RECORDS *r, SCH_COPY_EXTS_DATA *wanted, BYTE count, DWORD total)
{
    bool too_small = false;
    DWORD seen = 0;
    while (total) {
        DWORD type, length;
        SECURITY_STATUS status;
        if (total < 4)
            return SEC_E_ILLEGAL_MESSAGE;
        total -= 4;
        if ((status = read_number(r, 2, &type)))
            return status;
        int bit = known_extension((WORD)type);
        if (bit >= 0) {
            if (seen & (1u << bit))
                return SEC_E_ILLEGAL_MESSAGE;
            seen |= 1u << bit;
        }
        if ((status = read_number(r, 2, &length)))
            return status;
        if (total < length)
            return SEC_E_ILLEGAL_MESSAGE;
        total -= length;
        BYTE *target = nullptr;
        for (BYTE i = 0; i < count; i++) {
            if (wanted[i].ExtensionType != type)
                continue;
            if (wanted[i].BufferSize >= length)
                target = wanted[i].Buffer;
            else
                too_small = true;
            wanted[i].ExtensionSize = (WORD)length;
            wanted[i].Found = 1;
            break;
        }
        if ((status = read_field(r, target, length)))
            return status;
    }
    return too_small ? SEC_E_BUFFER_TOO_SMALL : SEC_E_OK;
}

/* a vector with a length of width bytes, skipped; its size must lie in [low, high] and within left. */
static SECURITY_STATUS skip_vector(RECORDS *r, DWORD *left, DWORD width, DWORD low, DWORD high, bool even)
{
    DWORD n;
    SECURITY_STATUS status;
    if (*left < width)
        return SEC_E_ILLEGAL_MESSAGE;
    if ((status = read_number(r, width, &n)))
        return status;
    *left -= width;
    if (n > *left || n < low || n > high || (even && (n & 1)))
        return SEC_E_ILLEGAL_MESSAGE;
    *left -= n;
    return read_field(r, nullptr, n);
}

/* a message other than a ClientHello has no extensions to copy; one that ends before its extensions neither. */
static SECURITY_STATUS copy_from_hello(RECORDS *r, SCH_COPY_EXTS_DATA *wanted, BYTE count, DWORD limit)
{
    DWORD type, left, value;
    SECURITY_STATUS status;
    if ((status = read_number(r, 1, &type)))
        return status;
    if (type != TLS_CLIENT_HELLO)
        return SEC_E_OK;
    if ((status = read_number(r, 3, &left)))
        return status;
    if (limit && left > limit)
        return SEC_E_ILLEGAL_MESSAGE;
    /* the version, below the dtls ones, and the random. */
    if (left < 2 + 32)
        return SEC_E_ILLEGAL_MESSAGE;
    if ((status = read_number(r, 2, &value)))
        return status;
    if ((WORD)(value - 0x300) > 0xFBFC)
        return SEC_E_ILLEGAL_MESSAGE;
    if ((status = read_field(r, nullptr, 32)))
        return status;
    left -= 2 + 32;
    /* the session id, the cipher suites and the compression methods. */
    if ((status = skip_vector(r, &left, 1, 0, 32, false)) || (status = skip_vector(r, &left, 2, 2, 0xFFFF, true)) ||
        (status = skip_vector(r, &left, 1, 1, 0xFF, false)))
        return status;
    if (left < 2)
        return SEC_E_OK;
    if ((status = read_number(r, 2, &value)))
        return status;
    if (left - 2 < value)
        return SEC_E_OK;
    return copy_extensions(r, wanted, count, value);
}

/*
 * copies the requested extensions of a ClientHello, which may span records,
 * into the caller's buffers. schannel runs this behind a servicing feature
 * switch, taken as enabled.
 */
__declspec(dllexport) SECURITY_STATUS WINAPI SslCopyTlsExtensions(const BYTE *hello, DWORD size,
                                                                  SCH_COPY_EXTS_DATA *wanted, BYTE count,
                                                                  DWORD *missing, DWORD options)
{
    SECURITY_STATUS status = SEC_E_INVALID_PARAMETER;
    if (hello && wanted && count && missing && !(options & ~1u)) {
        *missing = 0;
        bool duplicate = false;
        for (BYTE i = 0; i < count; i++) {
            wanted[i].Found = 0;
            for (BYTE j = 0; j < count; j++)
                if (i != j && wanted[i].ExtensionType == wanted[j].ExtensionType)
                    duplicate = true;
        }
        if (!duplicate) {
            /* option 1 is a server that asks for a client certificate, which allows the larger limit. */
            DWORD limit = MESSAGE_LIMIT_SERVER;
            if ((options & 1) && limit <= MESSAGE_LIMIT_SERVER_CLIENT_AUTH)
                limit = MESSAGE_LIMIT_SERVER_CLIENT_AUTH;
            RECORDS records = {hello, size, 0, 0, limit != 0, 0};
            status = copy_from_hello(&records, wanted, count, limit);
            if (status)
                *missing = records.Missing;
        }
    }
    if (!wanted)
        return status;
    /* what was not found, or everything after a failure, comes back empty. */
    for (BYTE i = 0; i < count; i++) {
        if (wanted[i].Found && (!status || status == SEC_E_BUFFER_TOO_SMALL))
            continue;
        if (wanted[i].Buffer && wanted[i].BufferSize)
            memset(wanted[i].Buffer, 0, wanted[i].BufferSize);
        wanted[i].ExtensionSize = 0;
        wanted[i].Found = 0;
    }
    return status;
}
