/*
 * the calls of crypt32 that are not made yet. they are here because other libraries import them by name, as the security
 * forwarder of the database engine does, and a library cannot load without them. each one says it is not implemented:
 * a call that answers a certificate revocation list, a trust list, an object identifier function, a signature provider
 * or a pkcs 8 key does not get a made up answer.
 */
#include "crypt32_private.h"

#define NOT_YET(error) (SetLastError(ERROR_CALL_NOT_IMPLEMENTED), error)

WINCRYPT32API BOOL WINAPI CryptDecodeMessage(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptCreateKeyIdentifierFromCSP(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptDecryptMessage(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertVerifyValidityNesting(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertVerifySubjectCertificateContext(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertVerifyRevocation(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptDecryptAndVerifyMessageSignature(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertVerifyCTLUsage(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI PFXExportCertStoreEx(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI PFXExportCertStore(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptVerifyTimeStampSignature(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptVerifyMessageSignatureWithKey(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptVerifyMessageSignature(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptVerifyMessageHash(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptVerifyDetachedMessageHash(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptUnregisterOIDInfo(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptUnregisterOIDFunction(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptUnregisterDefaultOIDFunction(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptUninstallDefaultContext(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptSignMessageWithKey(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptSignAndEncryptMessage(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptSetOIDFunctionValue(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptSetKeyIdentifierProperty(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptSIPVerifyIndirectData(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptSIPRetrieveSubjectGuidForCatalogFile(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptSIPRetrieveSubjectGuid(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptSIPRemoveSignedDataMsg(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptSIPRemoveProvider(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptSIPPutSignedDataMsg(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptSIPLoad(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptSIPGetSignedDataMsg(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptSIPGetCaps(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptSIPCreateIndirectData(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptSIPAddProvider(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptRetrieveTimeStamp(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptRegisterOIDInfo(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptRegisterOIDFunction(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptRegisterDefaultOIDFunction(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptMsgVerifyCountersignatureEncodedEx(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptMsgVerifyCountersignatureEncoded(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptMsgSignCTL(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API PVOID WINAPI CryptMsgOpenToEncode(void)
{
    return NOT_YET(nullptr);
}

WINCRYPT32API BOOL WINAPI CryptMsgEncodeAndSignCTL(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API PVOID WINAPI CryptMsgDuplicate(void)
{
    return NOT_YET(nullptr);
}

WINCRYPT32API BOOL WINAPI CryptMsgCountersignEncoded(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptMsgCountersign(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptMsgControl(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API DWORD WINAPI CryptMsgCalculateEncodedLength(void)
{
    return NOT_YET(0);
}

WINCRYPT32API BOOL WINAPI CryptInstallOIDFunctionAddress(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptInstallDefaultContext(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API PVOID WINAPI CryptInitOIDFunctionSet(void)
{
    return NOT_YET(nullptr);
}

WINCRYPT32API BOOL WINAPI CryptImportPublicKeyInfoEx2(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptImportPKCS8(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptHashMessage(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptGetOIDFunctionValue(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptGetOIDFunctionAddress(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API DWORD WINAPI CryptGetMessageSignerCount(void)
{
    return NOT_YET(0);
}

WINCRYPT32API BOOL WINAPI CryptGetKeyIdentifierProperty(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptGetDefaultOIDFunctionAddress(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptGetDefaultOIDDllList(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptFreeOIDFunctionAddress(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptFormatObject(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API PVOID WINAPI CryptFindLocalizedName(void)
{
    return NOT_YET(nullptr);
}

WINCRYPT32API BOOL WINAPI CryptFindCertificateKeyProvInfo(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptExportPKCS8(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptEnumOIDInfo(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptEnumOIDFunction(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptEnumKeyIdentifierProperties(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertAddCRLContextToStore(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertAddCRLLinkToStore(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertAddCTLContextToStore(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertAddCTLLinkToStore(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertAddEncodedCRLToStore(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertAddEncodedCTLToStore(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertAddEnhancedKeyUsageIdentifier(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API void WINAPI CertAddRefServerOcspResponse(void)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
}

WINCRYPT32API void WINAPI CertAddRefServerOcspResponseContext(void)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
}

WINCRYPT32API void WINAPI CertCloseServerOcspResponse(void)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
}

WINCRYPT32API PVOID WINAPI CertCreateCRLContext(void)
{
    return NOT_YET(nullptr);
}

WINCRYPT32API PVOID WINAPI CertCreateCTLContext(void)
{
    return NOT_YET(nullptr);
}

WINCRYPT32API BOOL WINAPI CertCreateCTLEntryFromCertificateContextProperties(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API PVOID WINAPI CertCreateContext(void)
{
    return NOT_YET(nullptr);
}

WINCRYPT32API BOOL WINAPI CertDeleteCRLFromStore(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertDeleteCTLFromStore(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API PVOID WINAPI CertDuplicateCRLContext(void)
{
    return NOT_YET(nullptr);
}

WINCRYPT32API PVOID WINAPI CertDuplicateCTLContext(void)
{
    return NOT_YET(nullptr);
}

WINCRYPT32API DWORD WINAPI CertEnumCRLContextProperties(void)
{
    return NOT_YET(0);
}

WINCRYPT32API DWORD WINAPI CertEnumCTLContextProperties(void)
{
    return NOT_YET(0);
}

WINCRYPT32API PVOID WINAPI CertEnumCTLsInStore(void)
{
    return NOT_YET(nullptr);
}

WINCRYPT32API BOOL WINAPI CertEnumSubjectInSortedCTL(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertEnumSystemStoreLocation(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API PVOID WINAPI CertFindAttribute(void)
{
    return NOT_YET(nullptr);
}

WINCRYPT32API PVOID WINAPI CertFindCRLInStore(void)
{
    return NOT_YET(nullptr);
}

WINCRYPT32API PVOID WINAPI CertFindCTLInStore(void)
{
    return NOT_YET(nullptr);
}

WINCRYPT32API BOOL WINAPI CertFindCertificateInCRL(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API PVOID WINAPI CertFindSubjectInCTL(void)
{
    return NOT_YET(nullptr);
}

WINCRYPT32API BOOL WINAPI CertFindSubjectInSortedCTL(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertFreeCTLContext(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API void WINAPI CertFreeCertificateChainList(void)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
}

WINCRYPT32API void WINAPI CertFreeServerOcspResponseContext(void)
{
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
}

WINCRYPT32API BOOL WINAPI CertGetCRLContextProperty(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API PVOID WINAPI CertGetCRLFromStore(void)
{
    return NOT_YET(nullptr);
}

WINCRYPT32API BOOL WINAPI CertGetCTLContextProperty(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API PVOID WINAPI CertGetServerOcspResponseContext(void)
{
    return NOT_YET(nullptr);
}

WINCRYPT32API DWORD WINAPI CertVerifyCRLTimeValidity(void)
{
    return NOT_YET(0);
}

WINCRYPT32API BOOL WINAPI CertIsStrongHashToSign(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertIsValidCRLForCertificate(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API PVOID WINAPI CertOpenServerOcspResponse(void)
{
    return NOT_YET(nullptr);
}

WINCRYPT32API BOOL WINAPI CertRegisterPhysicalStore(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertRemoveEnhancedKeyUsageIdentifier(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertRetrieveLogoOrBiometricInfo(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertSelectCertificateChains(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertSerializeCRLStoreElement(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertSerializeCTLStoreElement(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertSetCRLContextProperty(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertSetCTLContextProperty(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertSetCertificateContextPropertiesFromCTLEntry(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertSetEnhancedKeyUsage(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertUnregisterPhysicalStore(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CertVerifyCRLRevocation(void)
{
    return NOT_YET(FALSE);
}

WINCRYPT32API BOOL WINAPI CryptEncryptMessage(void)
{
    return NOT_YET(FALSE);
}
