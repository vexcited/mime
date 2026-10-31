/*
 * the schannel side of ncrypt. Every call but the opening of a provider takes a handle that SslOpenProvider gave out. Its
 * providers are registered with bcrypt's provider registry, which names none here, so no handle exists and every call
 * that takes one refuses it as Windows refuses a handle that is none of theirs.
 */
#define NCRYPTAPI
#include <mime/stubexport.h>
#include <mime/win32/ncrypt.h>

#define STATUS_INVALID_HANDLE_ ((LONG)0xC0000008L)
#define ERROR_PROC_NOT_FOUND_HRESULT ((LONG)0x8007007FL)

#define SSL_NO_HANDLE(name, args) DECLSPEC_EXPORT LONG WINAPI name(MIME_STUB_ARGS_##args) { return NTE_INVALID_HANDLE; }

/* the isolation, key storage and schannel interfaces are not provided by a library that is not here. */
DECLSPEC_EXPORT LONG WINAPI GetIsolationServerInterface(ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3)
{
    return ERROR_PROC_NOT_FOUND_HRESULT;
}

DECLSPEC_EXPORT LONG WINAPI GetKeyStorageInterface(ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3)
{
    return ERROR_PROC_NOT_FOUND_HRESULT;
}

DECLSPEC_EXPORT LONG WINAPI GetSChannelInterface(ULONG_PTR a1, ULONG_PTR a2, ULONG_PTR a3)
{
    return ERROR_PROC_NOT_FOUND_HRESULT;
}

DECLSPEC_EXPORT LONG WINAPI SslChangeNotify(ULONG_PTR a1, ULONG_PTR a2)
{
    return NTE_NOT_SUPPORTED;
}

DECLSPEC_EXPORT LONG WINAPI SslIncrementProviderReferenceCount(ULONG_PTR provider)
{
    return STATUS_INVALID_HANDLE_;
}

DECLSPEC_EXPORT LONG WINAPI SslDecrementProviderReferenceCount(ULONG_PTR provider)
{
    return STATUS_INVALID_HANDLE_;
}

/* a buffer no provider gave out is the caller's own heap block. */
DECLSPEC_EXPORT LONG WINAPI SslFreeBuffer(PVOID buffer)
{
    HeapFree(GetProcessHeap(), 0, buffer);
    return 0;
}

DECLSPEC_EXPORT LONG WINAPI SslFreeObject(ULONG_PTR object, ULONG_PTR flags)
{
    return NTE_INVALID_HANDLE;
}

/* the status of bcrypt's provider lookup in the form of an NTE code. */
static LONG ssl_status(LONG status)
{
    switch ((ULONG)status) {
    case 0xC000000D: return NTE_INVALID_PARAMETER;
    case 0xC0000008: return NTE_INVALID_HANDLE;
    case 0xC0000017: case 0xC0000095: return NTE_NO_MEMORY;
    }
    return status;
}

typedef LONG(WINAPI *RESOLVE)(LPCWSTR, ULONG, LPCWSTR, LPCWSTR, ULONG, ULONG, ULONG *, void **);

static LONG resolve_providers(LPCWSTR name, ULONG flags, ULONG *size, void **buffer)
{
    HMODULE module = LoadLibraryW(L"bcrypt.dll");
    RESOLVE resolve = module ? (RESOLVE)GetProcAddress(module, "BCryptResolveProviders") : nullptr;
    if (!resolve)
        return (LONG)0xC0000135;
    return resolve(nullptr, 0x10002, nullptr, name, 1, flags, size, buffer);
}

DECLSPEC_EXPORT LONG WINAPI SslOpenProvider(void **provider, LPCWSTR name, DWORD flags)
{
    if (!provider || flags)
        return NTE_INVALID_PARAMETER;
    ULONG size = 0;
    void *buffer = nullptr;
    LONG status = resolve_providers(name ? name : L"Microsoft SSL Protocol Provider", 0, &size, &buffer);
    if (status >= 0)
        status = NTE_NOT_SUPPORTED;
    return ssl_status(status);
}

DECLSPEC_EXPORT LONG WINAPI SslEnumProtocolProviders(DWORD *count, void **list, DWORD flags)
{
    if (!count || !list)
        return NTE_INVALID_PARAMETER;
    if (flags)
        return NTE_BAD_FLAGS;
    *count = 0;
    *list = nullptr;
    ULONG size = 0;
    void *buffer = nullptr;
    LONG status = resolve_providers(nullptr, 3, &size, &buffer);
    if (status < 0)
        return ssl_status(status);
    return NTE_NOT_SUPPORTED;
}

SSL_NO_HANDLE(SslCombineKeys, 6)
SSL_NO_HANDLE(SslComputeClientAuthHash, 8)
SSL_NO_HANDLE(SslComputeEapKeyBlock, 8)
SSL_NO_HANDLE(SslComputeFinishedHash, 6)
SSL_NO_HANDLE(SslComputeSessionHash, 7)
SSL_NO_HANDLE(SslCreateClientAuthHash, 6)
SSL_NO_HANDLE(SslCreateEphemeralKey, 9)
SSL_NO_HANDLE(SslCreateHandshakeHash, 5)
SSL_NO_HANDLE(SslDecryptPacket, 10)
SSL_NO_HANDLE(SslDeriveSessionTicketKey, 8)
SSL_NO_HANDLE(SslDuplicateTranscriptHash, 4)
SSL_NO_HANDLE(SslEncryptPacket, 11)
SSL_NO_HANDLE(SslEnumCipherSuites, 5)
SSL_NO_HANDLE(SslEnumCipherSuitesEx, 5)
SSL_NO_HANDLE(SslEnumEccCurves, 4)
SSL_NO_HANDLE(SslEnumGroups, 4)
SSL_NO_HANDLE(SslExpandBinderKey, 5)
SSL_NO_HANDLE(SslExpandExporterMasterKey, 6)
SSL_NO_HANDLE(SslExpandNextGenTrafficKey, 5)
SSL_NO_HANDLE(SslExpandPreSharedKey, 7)
SSL_NO_HANDLE(SslExpandResumptionMasterKey, 6)
SSL_NO_HANDLE(SslExpandTrafficKeys, 7)
SSL_NO_HANDLE(SslExpandWriteKey, 5)
SSL_NO_HANDLE(SslExportKey, 7)
SSL_NO_HANDLE(SslExportKeyingMaterial, 10)
SSL_NO_HANDLE(SslExtractEarlyKey, 7)
SSL_NO_HANDLE(SslExtractHandshakeKey, 7)
SSL_NO_HANDLE(SslExtractMasterKey, 5)
SSL_NO_HANDLE(SslGenerateMasterKey, 11)
SSL_NO_HANDLE(SslGeneratePreMasterKey, 10)
SSL_NO_HANDLE(SslGenerateSessionKeys, 6)
SSL_NO_HANDLE(SslGetCipherSuitePRFHashAlgorithm, 6)
SSL_NO_HANDLE(SslGetKeyProperty, 5)
SSL_NO_HANDLE(SslGetProviderProperty, 6)
SSL_NO_HANDLE(SslGetSessionTicketProtectionHeaderSize, 4)
SSL_NO_HANDLE(SslHashHandshake, 5)
SSL_NO_HANDLE(SslImportKey, 6)
SSL_NO_HANDLE(SslImportMasterKey, 9)
SSL_NO_HANDLE(SslLookupCipherLengths, 7)
SSL_NO_HANDLE(SslLookupCipherSuiteInfo, 6)
SSL_NO_HANDLE(SslOpenPrivateKey, 4)
SSL_NO_HANDLE(SslProtectSessionTicket, 7)
SSL_NO_HANDLE(SslSignHash, 8)
SSL_NO_HANDLE(SslUnprotectSessionTicket, 7)
SSL_NO_HANDLE(SslVerifySignature, 7)
