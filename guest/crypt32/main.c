/*
 * crypt32 entry point, and the pkcs #12 functions.
 */
#include "crypt32_private.h"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        CrInitLock();
    return TRUE;
}

#define szOID_PKCS_7_DATA "1.2.840.113549.1.7.1"

/* a pfx is a sequence of a version 3 integer, a content info and an optional mac. */
BOOL WINAPI PFXIsPFXBlob(CRYPT_DATA_BLOB *pPFX)
{
    if (!pPFX || !pPFX->pbData)
        return FALSE;
    der_reader r = {pPFX->pbData, pPFX->cbData};
    der_item pfx, version, content, oid;
    int value;
    char text[64];
    if (DerExpect(&r, ASN_SEQUENCE, &pfx))
        return FALSE;
    DerOpen(&r, &pfx);
    if (DerExpect(&r, ASN_INTEGER, &version) || DerParseInt(&version, &value) || value != 3 ||
        DerExpect(&r, ASN_SEQUENCE, &content))
        return FALSE;
    DerOpen(&r, &content);
    return !DerExpect(&r, ASN_OID, &oid) && !DerParseOid(&oid, text, sizeof(text)) &&
           (!strcmp(text, szOID_PKCS_7_DATA) || !strcmp(text, szOID_RSA_signedData));
}

/*
 * importing needs the pkcs #12 key derivation, rc2 and 3des or aes to open
 * the bags; none is implemented, so a valid blob fails as a bad password
 * does on windows.
 */
HCERTSTORE WINAPI PFXImportCertStore(CRYPT_DATA_BLOB *pPFX, LPCWSTR szPassword, DWORD dwFlags)
{
    if (!pPFX || !pPFX->pbData) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    if (!PFXIsPFXBlob(pPFX)) {
        SetLastError(CRYPT_E_BAD_ENCODE);
        return nullptr;
    }
    SetLastError(ERROR_INVALID_PASSWORD);
    return nullptr;
}

BOOL WINAPI PFXVerifyPassword(CRYPT_DATA_BLOB *pPFX, LPCWSTR szPassword, DWORD dwFlags)
{
    return FALSE;
}
