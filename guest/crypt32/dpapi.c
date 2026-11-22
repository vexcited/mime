/*
 * data protection: CryptProtectData and CryptUnprotectData, which the security authority serves over
 * ncalrpc:[protected_storage] as it does for dpapi.dll of windows, see guest/services/dpapi.c, and CryptProtectMemory.
 */
#define DPAPI_CLIENT_ONLY
#include "crypt32_private.h"
#include <crypto/crypto.h>
#include <mime/win32/rpc.h>
#include "services/dpapi.h"

#ifndef TOKEN_QUERY
#define TOKEN_QUERY 0x0008
#endif
#define TOKEN_STATISTICS_CLASS 10


/* ------------------------------------------------------------------ the crypto library's hooks */

typedef BOOLEAN(WINAPI *RTL_GEN_RANDOM)(PVOID, ULONG);

static INIT_ONCE g_random_once = INIT_ONCE_STATIC_INIT;
static RTL_GEN_RANDOM g_random;

static BOOL WINAPI load_random(INIT_ONCE *once, void *param, void **context)
{
    HMODULE advapi32 = LoadLibraryW(L"advapi32.dll");
    if (advapi32)
        g_random = (RTL_GEN_RANDOM)(void *)GetProcAddress(advapi32, "SystemFunction036");
    return TRUE;
}

void *mime_crypto_alloc(size_t size)
{
    return CrAlloc(size);
}

void mime_crypto_free(void *p)
{
    CrFree(p);
}

bool mime_crypto_random(void *out, size_t size)
{
    InitOnceExecuteOnce(&g_random_once, load_random, nullptr, nullptr);
    return g_random && g_random(out, (ULONG)size);
}

/* ------------------------------------------------------------------ identity */

static BOOL machine_guid(WCHAR *out, DWORD capacity)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY,
                      &key))
        return FALSE;
    DWORD size = capacity * sizeof(WCHAR), type;
    LONG error = RegQueryValueExW(key, L"MachineGuid", nullptr, &type, (BYTE *)out, &size);
    RegCloseKey(key);
    if (error || type != REG_SZ || size < sizeof(WCHAR))
        return FALSE;
    out[size / sizeof(WCHAR) - 1] = 0;
    return TRUE;
}

/* ------------------------------------------------------------------ the data protection service */

void *WINAPI MIDL_user_allocate(size_t size)
{
    return LocalAlloc(LMEM_FIXED, size);
}

void WINAPI MIDL_user_free(void *p)
{
    LocalFree(p);
}

#define SESSION_SAME_LOGON 2

typedef NTSTATUS(WINAPI *SESSION_CRYPT)(PVOID memory, ULONG size, ULONG options);

/*
 * the data crosses to the service sealed for the caller's logon session by the kernel, which RtlEncryptMemory of
 * cryptbase asks, padded to eight bytes with the count of the padding in each of its bytes, as dpapi.dll sends it.
 */
static BOOL session_crypt(BYTE *data, DWORD size, BOOL seal)
{
    HMODULE cryptbase = LoadLibraryW(L"cryptbase.dll");
    SESSION_CRYPT function =
        cryptbase ? (SESSION_CRYPT)(void *)GetProcAddress(cryptbase, seal ? "SystemFunction040" : "SystemFunction041") : nullptr;
    return function && NT_SUCCESS(function(data, size, SESSION_SAME_LOGON));
}

static DWORD service_binding(handle_t *binding)
{
    RPC_WSTR text = nullptr;
    RPC_STATUS status = RpcStringBindingComposeW(nullptr, (RPC_WSTR)L"ncalrpc", nullptr, (RPC_WSTR)L"protected_storage",
                                                 nullptr, &text);
    if (status)
        return status;
    status = RpcBindingFromStringBindingW(text, binding);
    RpcStringFreeW(&text);
    return status;
}

static DWORD prompt_flags(const CRYPTPROTECT_PROMPTSTRUCT *prompt)
{
    return prompt ? prompt->dwPromptFlags & (CRYPTPROTECT_PROMPT_ON_UNPROTECT | CRYPTPROTECT_PROMPT_ON_PROTECT |
                                             CRYPTPROTECT_PROMPT_STRONG | CRYPTPROTECT_PROMPT_REQUIRE_STRONG)
                  : 0;
}

BOOL WINAPI CryptProtectData(DATA_BLOB *pDataIn, LPCWSTR szDataDescr, DATA_BLOB *pOptionalEntropy, PVOID pvReserved,
                             CRYPTPROTECT_PROMPTSTRUCT *pPromptStruct, DWORD dwFlags, DATA_BLOB *pDataOut)
{
    if (!pDataOut || !pDataIn || !pDataIn->pbData)
        return CrFail(ERROR_INVALID_PARAMETER);
    pDataOut->cbData = 0;
    pDataOut->pbData = nullptr;
    DWORD prompt = prompt_flags(pPromptStruct);
    if ((prompt & (CRYPTPROTECT_PROMPT_ON_PROTECT | CRYPTPROTECT_PROMPT_ON_UNPROTECT)) &&
        (dwFlags & CRYPTPROTECT_UI_FORBIDDEN))
        return CrFail(ERROR_PASSWORD_RESTRICTION);
    if (!szDataDescr)
        szDataDescr = L"";
    if (pDataIn->cbData > 0x40000000)
        return CrFail(ERROR_INVALID_PARAMETER);
    DWORD sealed_size = (pDataIn->cbData / 8 + 1) * 8;
    BYTE *sealed = LocalAlloc(LMEM_FIXED, sealed_size);
    if (!sealed)
        return CrFail(ERROR_OUTOFMEMORY);
    memcpy(sealed, pDataIn->pbData, pDataIn->cbData);
    memset(sealed + pDataIn->cbData, (int)(sealed_size - pDataIn->cbData), sealed_size - pDataIn->cbData);
    handle_t binding = nullptr;
    DWORD error = session_crypt(sealed, sealed_size, TRUE) ? service_binding(&binding) : (DWORD)NTE_FAIL;
    byte *blob = nullptr;
    unsigned long blob_size = 0;
    if (!error) {
        BOOL entropy = pOptionalEntropy && pOptionalEntropy->pbData;
        __try {
            error = dpapi_SSCryptProtectData(binding, &blob, &blob_size, sealed, sealed_size, (unsigned short *)szDataDescr,
                                             entropy ? pOptionalEntropy->pbData : nullptr,
                                             entropy ? pOptionalEntropy->cbData : 0, nullptr, dwFlags, nullptr, 0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            error = __exception_code();
        }
        RpcBindingFree(&binding);
    }
    SecureZeroMemory(sealed, sealed_size);
    LocalFree(sealed);
    if (error)
        return CrFail(error);
    pDataOut->cbData = blob_size;
    pDataOut->pbData = blob;
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

BOOL WINAPI CryptUnprotectData(DATA_BLOB *pDataIn, LPWSTR *ppszDataDescr, DATA_BLOB *pOptionalEntropy,
                               PVOID pvReserved, CRYPTPROTECT_PROMPTSTRUCT *pPromptStruct, DWORD dwFlags,
                               DATA_BLOB *pDataOut)
{
    if (!pDataOut || !pDataIn || !pDataIn->pbData || pDataIn->cbData < 0x34)
        return CrFail(ERROR_INVALID_PARAMETER);
    pDataOut->cbData = 0;
    pDataOut->pbData = nullptr;
    if (ppszDataDescr)
        *ppszDataDescr = nullptr;
    /* the flags of the blob follow the provider, the version and the id of the master key. */
    DWORD flags;
    memcpy(&flags, pDataIn->pbData + 40, 4);
    DWORD prompt = flags & (CRYPTPROTECT_PROMPT_ON_UNPROTECT | CRYPTPROTECT_PROMPT_ON_PROTECT |
                            CRYPTPROTECT_PROMPT_STRONG | CRYPTPROTECT_PROMPT_REQUIRE_STRONG);
    if (prompt & (CRYPTPROTECT_PROMPT_ON_UNPROTECT | CRYPTPROTECT_PROMPT_ON_PROTECT)) {
        if (dwFlags & CRYPTPROTECT_UI_FORBIDDEN)
            return CrFail(ERROR_PASSWORD_RESTRICTION);
    } else if (prompt & CRYPTPROTECT_PROMPT_STRONG) {
        return CrFail(ERROR_INVALID_PARAMETER);
    }
    handle_t binding = nullptr;
    DWORD error = service_binding(&binding);
    byte *sealed = nullptr;
    unsigned long sealed_size = 0;
    unsigned short *description = nullptr;
    if (!error) {
        BOOL entropy = pOptionalEntropy && pOptionalEntropy->pbData;
        __try {
            error = dpapi_SSCryptUnprotectData(binding, &sealed, &sealed_size, pDataIn->pbData, pDataIn->cbData, &description,
                                               entropy ? pOptionalEntropy->pbData : nullptr,
                                               entropy ? pOptionalEntropy->cbData : 0, nullptr, dwFlags, nullptr, 0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            error = __exception_code();
        }
        RpcBindingFree(&binding);
    }
    if (!error && (!sealed || !sealed_size || sealed_size % 8 || !session_crypt(sealed, sealed_size, FALSE) ||
                   !sealed[sealed_size - 1] || sealed[sealed_size - 1] > 8))
        error = ERROR_INVALID_DATA;
    if (error) {
        if (sealed)
            LocalFree(sealed);
        if (description)
            LocalFree(description);
        return CrFail(error);
    }
    if (ppszDataDescr)
        *ppszDataDescr = (LPWSTR)description;
    else if (description)
        LocalFree(description);
    pDataOut->cbData = sealed_size - sealed[sealed_size - 1];
    pDataOut->pbData = sealed;
    SetLastError(ERROR_SUCCESS);
    return TRUE;
}

/* master keys never change owner in mime: there is no password to change. */
BOOL WINAPI CryptUpdateProtectedState(PSID pOldSid, LPCWSTR pwszOldPassword, DWORD dwFlags, DWORD *pdwSuccessCount,
                                      DWORD *pdwFailureCount)
{
    if (pdwSuccessCount)
        *pdwSuccessCount = 0;
    if (pdwFailureCount)
        *pdwFailureCount = 0;
    return TRUE;
}

/* ------------------------------------------------------------------ memory */

/*
 * windows encrypts these blocks in the kernel with per process, per logon session or per boot keys. mime derives
 * all three from a boot secret, SHA-256 of the machine guid and boot time, mixed with the process id and creation
 * time or the logon id: as stable as windows' keys, with no state to keep.
 */
static BOOL memory_key(DWORD flags, MimeAes *aes, BYTE iv[16])
{
    struct {
        LARGE_INTEGER BootTime;
        LARGE_INTEGER CurrentTime;
        BYTE rest[32];
    } day;
    WCHAR guid[64] = {};
    if (!NT_SUCCESS(NtQuerySystemInformation(SystemTimeOfDayInformation, &day, sizeof(day), nullptr)))
        return FALSE;
    machine_guid(guid, RTL_NUMBER_OF(guid));
    BYTE boot[32], key[32], context[32] = {};
    MimeHashCtx ctx;
    mime_sha256.init(&ctx);
    mime_sha256.update(&ctx, "mime memory protection", 22);
    mime_sha256.update(&ctx, guid, sizeof(guid));
    mime_sha256.update(&ctx, &day.BootTime, sizeof(day.BootTime));
    mime_sha256.final(&ctx, boot);
    DWORD context_size = 4;
    memcpy(context, &flags, 4);
    if (flags == CRYPTPROTECTMEMORY_SAME_PROCESS) {
        struct {
            LARGE_INTEGER CreateTime, ExitTime, KernelTime, UserTime;
        } times = {};
        ULONG64 pid = GetCurrentProcessId();
        NtQueryInformationProcess(NtCurrentProcess(), ProcessTimes, &times, sizeof(times), nullptr);
        memcpy(context + 4, &pid, 8);
        memcpy(context + 12, &times.CreateTime, 8);
        context_size = 20;
    } else if (flags == CRYPTPROTECTMEMORY_SAME_LOGON) {
        HANDLE token;
        BYTE statistics[64] = {};
        ULONG size;
        if (NT_SUCCESS(NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY, &token))) {
            NtQueryInformationToken(token, TOKEN_STATISTICS_CLASS, statistics, sizeof(statistics), &size);
            NtClose(token);
        }
        /* the authentication id, the logon session's luid. */
        memcpy(context + 4, statistics + 8, 8);
        context_size = 12;
    }
    mime_hmac(&mime_sha256, boot, sizeof(boot), context, context_size, key);
    mime_aes_init(aes, key, sizeof(key));
    mime_hash(&mime_sha256, key, sizeof(key), boot);
    memcpy(iv, boot, 16);
    SecureZeroMemory(key, sizeof(key));
    SecureZeroMemory(boot, sizeof(boot));
    return TRUE;
}

static BOOL protect_memory(LPVOID data, DWORD size, DWORD flags, BOOL encrypt)
{
    if (flags > CRYPTPROTECTMEMORY_SAME_LOGON || size % CRYPTPROTECTMEMORY_BLOCK_SIZE || (!data && size))
        return CrFail(ERROR_INVALID_PARAMETER);
    if (!size)
        return TRUE;
    MimeAes aes;
    BYTE iv[16];
    if (!memory_key(flags, &aes, iv))
        return CrFail(ERROR_INVALID_PARAMETER);
    (encrypt ? mime_cbc_encrypt : mime_cbc_decrypt)(&mime_cipher_aes, &aes, iv, data, data, size);
    SecureZeroMemory(&aes, sizeof(aes));
    return TRUE;
}

BOOL WINAPI CryptProtectMemory(LPVOID pDataIn, DWORD cbDataIn, DWORD dwFlags)
{
    return protect_memory(pDataIn, cbDataIn, dwFlags, TRUE);
}

BOOL WINAPI CryptUnprotectMemory(LPVOID pDataIn, DWORD cbDataIn, DWORD dwFlags)
{
    return protect_memory(pDataIn, cbDataIn, dwFlags, FALSE);
}
