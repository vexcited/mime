/*
 * the rest of the tpm base services. the tpm is the firmware tpm 2.0 of the main file, which has no endorsement
 * owner, no attestation key and no measured boot log, so the calls that read those from the registry or the logs
 * find nothing and answer as windows does when they are missing.
 */
#define TBSAPI
#include <mime/win32/tbs.h>

#define TBS_E_OWNERAUTH_NOT_FOUND 0x80284015u
#define STATUS_INVALID_PARAMETER_ 0xC000000Du
#define STATUS_NOT_SUPPORTED_ 0xC00000BBu
#define HRESULT_INSUFFICIENT_BUFFER 0x8007007Au

BOOL tbs_context_valid(TBS_HCONTEXT context);

#define valid_context tbs_context_valid

static ULONG hresult_from_win32(LONG error)
{
    return error > 0 ? (ULONG)((USHORT)error | 0x80070000u) : (ULONG)error;
}

/* the value of a key: the type must be the one asked for, a missing key or value is ERROR_FILE_NOT_FOUND. */
static LONG read_value(const WCHAR *path, const WCHAR *name, DWORD want, void *data, DWORD *size)
{
    HKEY key;
    LONG error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, KEY_QUERY_VALUE, &key);
    if (error)
        return error;
    DWORD type = 0;
    error = RegQueryValueExW(key, name, nullptr, &type, data, size);
    RegCloseKey(key);
    if (!error && type != want)
        return ERROR_UNSUPPORTED_TYPE;
    return error;
}

/* ------------------------------------------------------------------ hashes */

typedef LONG(WINAPI *BCRYPT_OPEN)(PVOID *, const WCHAR *, const WCHAR *, ULONG);
typedef LONG(WINAPI *BCRYPT_PROPERTY)(PVOID, const WCHAR *, PUCHAR, ULONG, ULONG *, ULONG);
typedef LONG(WINAPI *BCRYPT_CREATE)(PVOID, PVOID *, PUCHAR, ULONG, PUCHAR, ULONG, ULONG);
typedef LONG(WINAPI *BCRYPT_DATA)(PVOID, PUCHAR, ULONG, ULONG);
typedef LONG(WINAPI *BCRYPT_FINISH)(PVOID, PUCHAR, ULONG, ULONG);
typedef LONG(WINAPI *BCRYPT_DESTROY)(PVOID);
typedef LONG(WINAPI *BCRYPT_CLOSE)(PVOID, ULONG);

/* a hash or hmac of sha1, sha256 or sha384; the size of the digest is reported when there is no room for it. */
ULONG WINAPI Tbsi_ShaHash(const WCHAR *algorithm, PUCHAR secret, ULONG secret_size, PUCHAR input, ULONG input_size,
                          PUCHAR output, ULONG output_size, PULONG digest_size)
{
    if (lstrcmpW(algorithm, L"SHA1") && lstrcmpW(algorithm, L"SHA256") && lstrcmpW(algorithm, L"SHA384"))
        return STATUS_INVALID_PARAMETER_;
    HMODULE module = LoadLibraryW(L"bcrypt.dll");
    BCRYPT_OPEN open = module ? (BCRYPT_OPEN)GetProcAddress(module, "BCryptOpenAlgorithmProvider") : nullptr;
    BCRYPT_PROPERTY property = module ? (BCRYPT_PROPERTY)GetProcAddress(module, "BCryptGetProperty") : nullptr;
    BCRYPT_CREATE create = module ? (BCRYPT_CREATE)GetProcAddress(module, "BCryptCreateHash") : nullptr;
    BCRYPT_DATA data = module ? (BCRYPT_DATA)GetProcAddress(module, "BCryptHashData") : nullptr;
    BCRYPT_FINISH finish = module ? (BCRYPT_FINISH)GetProcAddress(module, "BCryptFinishHash") : nullptr;
    BCRYPT_DESTROY destroy = module ? (BCRYPT_DESTROY)GetProcAddress(module, "BCryptDestroyHash") : nullptr;
    BCRYPT_CLOSE close = module ? (BCRYPT_CLOSE)GetProcAddress(module, "BCryptCloseAlgorithmProvider") : nullptr;
    if (!open || !property || !create || !data || !finish || !destroy || !close)
        return 0xC0000135u;
    PVOID provider = nullptr, hash = nullptr;
    ULONG length = 0, returned;
    LONG status = open(&provider, algorithm, L"Microsoft Primitive Provider", secret_size && secret ? 8 : 0);
    if (status >= 0) {
        status = property(provider, L"HashDigestLength", (PUCHAR)&length, 4, &returned, 0);
        if (status >= 0) {
            if (output && output_size) {
                if (output_size < length) {
                    status = (LONG)0xC0000023;
                    *digest_size = length;
                } else {
                    status = create(provider, &hash, nullptr, 0, secret, secret_size, 0);
                    if (status >= 0)
                        status = data(hash, input, input_size, 0);
                    if (status >= 0)
                        status = finish(hash, output, output_size, 0);
                }
            } else {
                *digest_size = length;
            }
            if (status >= 0 && output && output_size)
                *digest_size = length;
        }
    }
    if (hash)
        destroy(hash);
    if (provider)
        close(provider, 0);
    return (ULONG)status;
}

/* ------------------------------------------------------------------ the owner authorization */

static int base64_value(WCHAR c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

static BOOL base64_decode(const WCHAR *text, BYTE *out, ULONG *size)
{
    ULONG at = 0;
    int bits = 0;
    ULONG value = 0;
    for (; *text && *text != '='; text++) {
        int digit = base64_value(*text);
        if (digit < 0)
            return FALSE;
        value = value << 6 | (ULONG)digit;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (at >= *size)
                return FALSE;
            out[at++] = (BYTE)(value >> bits);
        }
    }
    *size = at;
    return TRUE;
}

/* the authorization values that were kept in the registry when a tpm 1.2 or 2.0 was taken ownership of. */
TBS_RESULT WINAPI Tbsi_Get_OwnerAuth(TBS_HCONTEXT context, UINT32 type, PBYTE auth, PUINT32 size)
{
    if (!size || (*size && !auth))
        return TBS_E_INVALID_OUTPUT_POINTER;
    if (!valid_context(context))
        return TBS_E_INVALID_CONTEXT_PARAM;
    TPM_DEVICE_INFO info;
    TBS_RESULT result = Tbsi_GetDeviceInfo(sizeof(info), &info);
    if (result)
        return result;
    static const WCHAR admin[] = L"System\\CurrentControlSet\\Services\\TPM\\WMI\\Admin";
    static const WCHAR endorsement[] = L"System\\CurrentControlSet\\Services\\TPM\\WMI\\Endorsement";
    static const WCHAR user[] = L"System\\CurrentControlSet\\Services\\TPM\\WMI\\User";
    const WCHAR *path, *name;
    DWORD kind;
    switch (type) {
    case 1: path = admin; name = L"OwnerAuthFull"; kind = REG_SZ; break;
    case 2: if (info.tpmVersion == 2) return TBS_E_BAD_PARAMETER; path = admin; name = L"OwnerAuthAdmin"; kind = REG_BINARY; break;
    case 3: if (info.tpmVersion == 2) return TBS_E_BAD_PARAMETER; path = user; name = L"OwnerAuthUser"; kind = REG_BINARY; break;
    case 4: if (info.tpmVersion == 2) return TBS_E_BAD_PARAMETER; path = endorsement; name = L"OwnerAuthEndorsement"; kind = REG_BINARY; break;
    case 12: if (info.tpmVersion == 1) return TBS_E_BAD_PARAMETER; path = endorsement; name = L"EndorsementAuth"; kind = REG_SZ; break;
    case 13: if (info.tpmVersion == 1) return TBS_E_BAD_PARAMETER; path = admin; name = L"StorageOwnerAuth"; kind = REG_SZ; break;
    default: return TBS_E_BAD_PARAMETER;
    }
    DWORD length = 0;
    LONG error = read_value(path, name, kind, nullptr, &length);
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        return TBS_E_OWNERAUTH_NOT_FOUND;
    if (error)
        return TBS_E_INTERNAL_ERROR;
    BYTE *value = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, length + 2);
    if (!value)
        return 0x8007000E;
    if (read_value(path, name, kind, value, &length)) {
        HeapFree(GetProcessHeap(), 0, value);
        return TBS_E_INTERNAL_ERROR;
    }
    ULONG out_size = length;
    BOOL empty = FALSE;
    result = TBS_SUCCESS;
    if (kind == REG_SZ) {
        /* twenty bytes in base64 are 28 characters and the terminator. */
        if (length != 58) {
            if ((length & ~2u) != 0 || (type - 12) > 1) {
                HeapFree(GetProcessHeap(), 0, value);
                return TBS_E_INTERNAL_ERROR;
            }
            empty = TRUE;
        }
        out_size = 20;
    }
    if (auth) {
        BOOL small = *size < out_size;
        *size = out_size;
        if (small) {
            result = TBS_E_INSUFFICIENT_BUFFER;
        } else if (kind == REG_SZ) {
            ULONG decoded = out_size;
            if (empty)
                memset(auth, 0, out_size);
            else if (!base64_decode((const WCHAR *)value, auth, &decoded) || decoded != 20)
                result = TBS_E_INTERNAL_ERROR;
        } else {
            memcpy(auth, value, out_size);
        }
    } else {
        *size = out_size;
    }
    HeapFree(GetProcessHeap(), 0, value);
    return result;
}

/* ------------------------------------------------------------------ the device id */

#define DEVICE_ID_KEY L"System\\CurrentControlSet\\Services\\TPM\\WMI"

/* the hash of the attestation key is left in the registry when the key is made, the call waits for it. */
ULONG WINAPI GetDeviceIDWithTimeout(PBYTE buffer, UINT32 size, PUINT32 needed, UINT32 timeout, PUINT32 backed)
{
    if (!needed)
        return 0x80070057u;
    *needed = 0;
    ULONGLONG until = GetTickCount64() + timeout;
    DWORD length = size;
    LONG error = read_value(DEVICE_ID_KEY, L"WindowsAIKHash", REG_BINARY, buffer, &length);
    while (error && error != ERROR_MORE_DATA && GetTickCount64() < until) {
        ULONGLONG left = until - GetTickCount64();
        Sleep(left > 200 ? 200 : (DWORD)left);
        length = size;
        error = read_value(DEVICE_ID_KEY, L"WindowsAIKHash", REG_BINARY, buffer, &length);
    }
    if (error == ERROR_MORE_DATA) {
        *needed = length;
        return HRESULT_INSUFFICIENT_BUFFER;
    }
    if (error)
        return hresult_from_win32(error);
    *needed = length;
    if (buffer && backed) {
        *backed = 0;
        DWORD value = 0, bytes = 4, public_size = 0;
        LONG missing = read_value(DEVICE_ID_KEY, L"WindowsAIKPub", REG_BINARY, nullptr, &public_size);
        if (missing == ERROR_FILE_NOT_FOUND) {
            error = read_value(DEVICE_ID_KEY, L"TpmBackedDeviceID", REG_DWORD, &value, &bytes);
            if (!error)
                *backed = value != 0;
            else if (error != ERROR_FILE_NOT_FOUND)
                return hresult_from_win32(error);
        } else if (missing) {
            return hresult_from_win32(missing);
        }
    }
    return 0;
}

ULONG WINAPI GetDeviceID(PBYTE buffer, UINT32 size, PUINT32 needed, PUINT32 backed)
{
    return GetDeviceIDWithTimeout(buffer, size, needed, 0, backed);
}

/* the id in base64, 3 bytes into 4 characters. */
ULONG WINAPI GetDeviceIDString(WCHAR *text, UINT32 chars, PUINT32 needed, PUINT32 backed)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    UINT32 size = 0;
    BYTE id[64];
    if (backed)
        *backed = 0;
    if (needed)
        *needed = 0;
    if (text)
        *text = 0;
    BYTE *bytes = id;
    ULONG result = GetDeviceIDWithTimeout(id, sizeof(id), &size, 0, backed);
    if (result == HRESULT_INSUFFICIENT_BUFFER) {
        bytes = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
        if (!bytes)
            return 0x8007000E;
        result = GetDeviceID(bytes, size, &size, backed);
    }
    if (!result) {
        UINT32 want = 4 * ((size + 2) / 3) + 1;
        if (chars < want) {
            if (text && chars)
                result = HRESULT_INSUFFICIENT_BUFFER;
        } else {
            UINT32 at = 0;
            for (UINT32 i = 0; i < size; i += 3) {
                UINT32 b0 = bytes[i], b1 = i + 1 < size ? bytes[i + 1] : 0, b2 = i + 2 < size ? bytes[i + 2] : 0;
                text[at++] = alphabet[b0 >> 2];
                text[at++] = alphabet[(b0 & 3) << 4 | b1 >> 4];
                text[at++] = i + 1 < size ? alphabet[(b1 & 15) << 2 | b2 >> 6] : '=';
                text[at++] = i + 2 < size ? alphabet[b2 & 63] : '=';
            }
            text[at] = 0;
        }
        if (needed)
            *needed = want;
    }
    if (bytes != id)
        HeapFree(GetProcessHeap(), 0, bytes);
    return result;
}

/* ------------------------------------------------------------------ the measured boot log */

/* the logs of the boot are files the measured boot driver writes, named by the boot count the tpm service keeps. */
TBS_RESULT WINAPI Tbsi_Get_TCG_Logs(UINT32 start, PUINT32 next, PBYTE buffer, UINT32 size, PUINT32 needed, UINT32 flags)
{
    if (!needed)
        return TBS_E_INVALID_OUTPUT_POINTER;
    *needed = 0;
    if (flags & ~1u)
        return TBS_E_BAD_PARAMETER;
    DWORD boots = 0, bytes = 4;
    if (read_value(L"System\\CurrentControlSet\\services\\TPM", L"OsBootCount", REG_DWORD, &boots, &bytes))
        return TBS_E_INTERNAL_ERROR;
    return TBS_E_NO_EVENT_LOG;
}

/*
 * a boot log of the old format holds events of a 32 byte header with a 20 byte digest and the size of the event
 * data. the first event of a crypto agile log names the digests, which this iterator does not read.
 */
static ULONG log_check(const BYTE *log, ULONG size)
{
    if (size < 0x20)
        return 0xC0070057u;
    ULONG at = 0;
    while (at < size) {
        if (size - at < 32)
            return 0xC007000Du;
        ULONG event = *(const ULONG *)(log + at + 28);
        if (event > size - at - 32)
            return 0xC007000Du;
        at += 32 + event;
    }
    return 0;
}

ULONG WINAPI Tbsi_FilterLog(const BYTE *log, ULONG size, ULONG flags, PBYTE output, ULONG output_size, PULONG needed)
{
    if (!log || !size || !flags || !needed)
        return STATUS_INVALID_PARAMETER_;
    *needed = 0;
    ULONG status = log_check(log, size);
    if (status)
        return status;
    /* bit zero asks for every event as it is. */
    ULONG total = (flags & 1) ? size : 0;
    if (!output || !output_size) {
        *needed = total;
        return 0;
    }
    if (output_size < total) {
        *needed = total;
        return 0xC0000023u;
    }
    memcpy(output, log, total);
    *needed = total;
    return 0;
}

/* the attestation of a log is made from the pcr values it replays, which needs the quoting of the tpm. */
ULONG WINAPI Tbsi_Create_Attestation_From_Log(const BYTE *log, ULONG size, PVOID a3, PVOID a4, PVOID a5, PVOID a6,
                                              ULONG a7, PULONG a8, ULONG flags)
{
    if (!log || !size || !a8 || (flags & ~1u))
        return STATUS_INVALID_PARAMETER_;
    ULONG status = log_check(log, size);
    if (status)
        return status;
    return STATUS_NOT_SUPPORTED_;
}

/* ------------------------------------------------------------------ the tests of the driver */

/* the driver answers the number of the interrupt and the state of the memory overwrite bit, a firmware tpm has neither. */
ULONG WINAPI Tbsip_TestInterruptInformation(PUINT32 value)
{
    struct {
        UINT32 version;
        UINT32 flags;
    } params = {2, 6};
    TBS_HCONTEXT context = nullptr;
    TBS_RESULT result = Tbsi_Context_Create((PCTBS_CONTEXT_PARAMS)&params, &context);
    if (result)
        return result;
    Tbsip_Context_Close(context);
    *value = 0;
    return TBS_SUCCESS;
}

ULONG WINAPI Tbsip_TestMorBit(PUINT32 value)
{
    struct {
        UINT32 version;
        UINT32 flags;
    } params = {2, 6};
    TBS_HCONTEXT context = nullptr;
    TBS_RESULT result = Tbsi_Context_Create((PCTBS_CONTEXT_PARAMS)&params, &context);
    if (result)
        return result;
    Tbsip_Context_Close(context);
    if (value)
        *value = 0;
    return TBS_SUCCESS;
}
