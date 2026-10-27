/*
 * What a downloaded package must be before it is run: a size within the limits of the feature, a signature that
 * verifies, a signer of the name the feature expects and a chain that ends in one of Microsoft's roots.
 */
#include "featurep.h"
#include <mime/win32/wintrust.h>

#define CERT_E_UNTRUSTEDROOT_ ((LONG)0x800B0109)
#define CERT_E_CN_NO_MATCH_ ((LONG)0x800B010F)
#define TRUST_E_SUBJECT_NOT_TRUSTED_ ((LONG)0x800B0004)

WINTRUSTAPI CRYPT_PROVIDER_DATA *WINAPI WTHelperProvDataFromStateData(HANDLE state);
WINTRUSTAPI CRYPT_PROVIDER_SGNR *WINAPI WTHelperGetProvSignerFromChain(CRYPT_PROVIDER_DATA *data, DWORD signer,
                                                                    BOOL counter, DWORD counter_index);
WINTRUSTAPI CRYPT_PROVIDER_CERT *WINAPI WTHelperGetProvCertFromChain(CRYPT_PROVIDER_SGNR *signer, DWORD index);

/* The names of the leaf and the thumbprint of the root, from what WinVerifyTrust built. */
static LONG read_chain(HANDLE state, const FEATURE_DEF *feature)
{
    CRYPT_PROVIDER_DATA *data = WTHelperProvDataFromStateData(state);
    CRYPT_PROVIDER_SGNR *signer = data ? WTHelperGetProvSignerFromChain(data, 0, FALSE, 0) : nullptr;
    if (!signer || !signer->csCertChain)
        return TRUST_E_NOSIGNATURE;
    CRYPT_PROVIDER_CERT *leaf = WTHelperGetProvCertFromChain(signer, 0);
    CRYPT_PROVIDER_CERT *root = WTHelperGetProvCertFromChain(signer, signer->csCertChain - 1);
    if (!leaf || !root || !leaf->pCert || !root->pCert)
        return TRUST_E_NOSIGNATURE;
    BYTE sha1[20];
    DWORD size = sizeof(sha1);
    if (!CertGetCertificateContextProperty(root->pCert, CERT_SHA1_HASH_PROP_ID, sha1, &size) || size != sizeof(sha1) ||
        !FeatureRootPinned(sha1))
        return CERT_E_UNTRUSTEDROOT_;
    char name[128];
    if (!CertGetNameStringA(leaf->pCert, CERT_NAME_ATTR_TYPE, 0, (void *)szOID_COMMON_NAME, name, sizeof(name)) ||
        !FeatureSignerMatches(feature, name))
        return TRUST_E_SUBJECT_NOT_TRUSTED_;
    return 0;
}

#define TRUST_E_BAD_DIGEST_ ((DWORD)0x80096010)

/* A file the catalog names the hash of: the whole file has it, whoever made the file. */
static DWORD verify_pinned(RUN *run, const FEATURE_DEF *feature, PCWSTR path)
{
    return FileMatches(path, feature->Sha256) ? ERROR_SUCCESS : TRUST_E_BAD_DIGEST_;
}

/* A pack of fonts that was read out of an archive: it has the shape, every font in it is checked when it is installed. */
static DWORD verify_pack(PCWSTR path)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return GetLastError();
    DWORD header[3] = {}, got = 0;
    BOOL ok = ReadFile(file, header, sizeof(header), &got, nullptr) && got == sizeof(header);
    CloseHandle(file);
    return ok && header[0] == 0x544E464Du && header[1] == 1 ? ERROR_SUCCESS : ERROR_INVALID_DATA;
}

DWORD VerifyPackage(RUN *run, const FEATURE_DEF *feature, PCWSTR path)
{
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &info))
        return GetLastError();
    ULONGLONG size = (ULONGLONG)info.nFileSizeHigh << 32 | info.nFileSizeLow;
    if (feature->Format == FEATURE_FORMAT_ZIP || feature->Format == FEATURE_FORMAT_WIM)
        return verify_pack(path);
    if (size < feature->MinSize || size > feature->MaxSize) {
        _snwprintf(run->Detail, RTL_NUMBER_OF(run->Detail), L"The file is %llu bytes, which is not a size this feature has.", size);
        run->Detail[RTL_NUMBER_OF(run->Detail) - 1] = 0;
        return ERROR_INVALID_DATA;
    }
    if (feature->Format == FEATURE_FORMAT_CABINET)
        return verify_pinned(run, feature, path);
    WINTRUST_FILE_INFO file = {sizeof(file), path, nullptr, nullptr};
    WINTRUST_DATA data = {.cbStruct = sizeof(data),
                          .dwUIChoice = WTD_UI_NONE,
                          .fdwRevocationChecks = WTD_REVOKE_NONE,
                          .dwUnionChoice = WTD_CHOICE_FILE,
                          .pFile = &file,
                          .dwStateAction = WTD_STATEACTION_VERIFY};
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG result = WinVerifyTrust(nullptr, &action, &data);
    /*
     * A root that the machine does not trust is no failure by itself: the root that counts is Microsoft's, pinned by
     * its thumbprint. Anything else is.
     */
    if (result == 0 || result == CERT_E_UNTRUSTEDROOT_) {
        LONG chain = data.hWVTStateData ? read_chain(data.hWVTStateData, feature) : TRUST_E_NOSIGNATURE;
        result = chain;
    }
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &action, &data);
    return (DWORD)result;
}
