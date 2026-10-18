/*
 * the sid of an app container, derived from its name: the sha-256 of the
 * lower cased utf-16 name, whose first 28 bytes become seven sub
 * authorities after S-1-15-2.
 */
#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>
#include <mime/win32/wincrypt.h>

#define USERENV DECLSPEC_EXPORT
#define APP_CONTAINER_NAME_MAX 64

typedef BOOL(WINAPI *HASH_CERTIFICATE)(HCRYPTPROV_LEGACY, ALG_ID, DWORD, const BYTE *, DWORD, BYTE *, DWORD *);

static BOOL sha256(const void *data, DWORD size, BYTE *hash)
{
    static HASH_CERTIFICATE hash_certificate;
    if (!hash_certificate) {
        HMODULE crypt32 = LoadLibraryW(L"crypt32.dll");
        hash_certificate = crypt32 ? (HASH_CERTIFICATE)GetProcAddress(crypt32, "CryptHashCertificate") : nullptr;
        if (!hash_certificate)
            return FALSE;
    }
    DWORD length = 32;
    return hash_certificate(0, CALG_SHA_256, 0, data, size, hash, &length) && length == 32;
}

USERENV HRESULT WINAPI DeriveAppContainerSidFromAppContainerName(PCWSTR name, PSID *sid)
{
    if (!sid)
        return E_INVALIDARG;
    *sid = nullptr;
    size_t length = name ? wcslen(name) : 0;
    if (!length || length > APP_CONTAINER_NAME_MAX)
        return E_INVALIDARG;
    WCHAR lower[APP_CONTAINER_NAME_MAX + 1];
    if (!LCMapStringW(LOCALE_INVARIANT, LCMAP_LOWERCASE, name, (int)length, lower, APP_CONTAINER_NAME_MAX))
        return HRESULT_FROM_WIN32(GetLastError());
    BYTE hash[32];
    if (!sha256(lower, (DWORD)(length * sizeof(WCHAR)), hash))
        return E_FAIL;
    /* revision 1, eight sub authorities, authority 15, then 2 and the hash. */
    BYTE *result = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, 8 + 4 * 8);
    if (!result)
        return E_OUTOFMEMORY;
    static const BYTE header[12] = {1, 8, 0, 0, 0, 0, 0, 15, 2, 0, 0, 0};
    memcpy(result, header, sizeof(header));
    memcpy(result + sizeof(header), hash, 28);
    *sid = result;
    return S_OK;
}
