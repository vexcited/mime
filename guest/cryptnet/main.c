#include "cryptnet_private.h"

#define TIME_VALID_OID_GET_CRL_FROM_CERT ((LPCSTR)4)
#define CRYPT_E_NO_VERIFY_USAGE_CHECK ((HRESULT)0x80092019L)

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(instance);
    return TRUE;
}

DECLSPEC_EXPORT HRESULT WINAPI DllRegisterServer(void)
{
    BOOL ok = CryptRegisterDefaultOIDFunction(X509_ASN_ENCODING, CRYPT_OID_VERIFY_REVOCATION_FUNC,
                                              CRYPT_REGISTER_FIRST_INDEX, L"cryptnet.dll") ||
              GetLastError() == ERROR_FILE_EXISTS;
    ok = (CryptRegisterDefaultOIDFunction(X509_ASN_ENCODING, CRYPT_OID_VERIFY_CTL_USAGE_FUNC,
                                          CRYPT_REGISTER_FIRST_INDEX, L"cryptnet.dll") ||
          GetLastError() == ERROR_FILE_EXISTS) &&
         ok;
    return ok ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

DECLSPEC_EXPORT HRESULT WINAPI DllUnregisterServer(void)
{
    CryptUnregisterDefaultOIDFunction(X509_ASN_ENCODING, CRYPT_OID_VERIFY_REVOCATION_FUNC, L"cryptnet.dll");
    CryptUnregisterDefaultOIDFunction(X509_ASN_ENCODING, CRYPT_OID_VERIFY_CTL_USAGE_FUNC, L"cryptnet.dll");
    return S_OK;
}

/* trust lists are not checked: the caller goes on to the next provider. */
DECLSPEC_EXPORT BOOL WINAPI CertDllVerifyCTLUsage(DWORD dwEncodingType, DWORD dwSubjectType, void *pvSubject,
                                                  void *pSubjectUsage, DWORD dwFlags, void *pVerifyUsagePara,
                                                  void *pVerifyUsageStatus)
{
    return CnFail(CRYPT_E_NO_VERIFY_USAGE_CHECK);
}

/* retrievals are synchronous, so there is never one to cancel. */
DECLSPEC_EXPORT BOOL WINAPI CryptCancelAsyncRetrieval(HANDLE hAsyncRetrieval)
{
    return CnFail(E_INVALIDARG);
}

DECLSPEC_EXPORT BOOL WINAPI CryptInstallCancelRetrieval(void *pfnCancel, const void *pvArg, DWORD dwFlags,
                                                        void *pvReserved)
{
    return TRUE;
}

DECLSPEC_EXPORT BOOL WINAPI CryptUninstallCancelRetrieval(DWORD dwFlags, void *pvReserved)
{
    return TRUE;
}

/* a list for a certificate, from its distribution points, valid at the time given and signed by the issuer. */
DECLSPEC_EXPORT BOOL WINAPI CryptGetTimeValidObject(LPCSTR pszTimeValidOid, LPVOID pvPara, PCCERT_CONTEXT pIssuer,
                                                    LPFILETIME pftValidFor, DWORD dwFlags, DWORD dwTimeout,
                                                    LPVOID *ppvObject, PCRYPT_CREDENTIALS pCredentials,
                                                    void *pExtraInfo)
{
    if (!ppvObject || !pvPara || !pIssuer)
        return CnFail(E_INVALIDARG);
    *ppvObject = nullptr;
    if (pszTimeValidOid != TIME_VALID_OID_GET_CRL_FROM_CERT)
        return CnFail(CRYPT_E_NOT_FOUND);
    PCRYPT_URL_ARRAY urls = CnCrlUrls(pvPara);
    if (!urls)
        return CnFail(CRYPT_E_NOT_FOUND);
    FILETIME now;
    GetSystemTimeAsFileTime(&now);
    if (!pftValidFor)
        pftValidFor = &now;
    DWORD error = (DWORD)CRYPT_E_NOT_FOUND;
    for (DWORD pass = 0; pass < 2 && !*ppvObject; pass++) {
        DWORD flags = pass ? CRYPT_WIRE_ONLY_RETRIEVAL : CRYPT_CACHE_ONLY_RETRIEVAL;
        if (pass && (dwFlags & CRYPT_CACHE_ONLY_RETRIEVAL))
            break;
        if (!pass && (dwFlags & CRYPT_WIRE_ONLY_RETRIEVAL))
            continue;
        for (DWORD i = 0; i < urls->cUrl && !*ppvObject; i++) {
            PCCRL_CONTEXT crl = nullptr;
            if (!CryptRetrieveObjectByUrlW(urls->rgwszUrl[i], CONTEXT_OID_CRL, flags | CRYPT_VERIFY_CONTEXT_SIGNATURE,
                                           dwTimeout, (void **)&crl, nullptr, pCredentials, (void *)pIssuer,
                                           nullptr)) {
                error = GetLastError();
                continue;
            }
            if ((dwFlags & CRYPT_DONT_CHECK_TIME_VALIDITY) || !CertVerifyCRLTimeValidity(pftValidFor, crl->pCrlInfo))
                *ppvObject = (void *)crl;
            else
                CertFreeCRLContext(crl);
        }
    }
    CnFree(urls);
    return *ppvObject ? TRUE : CnFail(error);
}

DECLSPEC_EXPORT BOOL WINAPI CryptFlushTimeValidObject(LPCSTR pszFlushTimeValidOid, LPVOID pvPara,
                                                      PCCERT_CONTEXT pIssuer, DWORD dwFlags, LPVOID pvReserved)
{
    if (pszFlushTimeValidOid != TIME_VALID_OID_GET_CRL_FROM_CERT || !pvPara)
        return CnFail(CRYPT_E_NOT_FOUND);
    PCRYPT_URL_ARRAY urls = CnCrlUrls(pvPara);
    if (!urls)
        return CnFail(CRYPT_E_NOT_FOUND);
    for (DWORD i = 0; i < urls->cUrl; i++)
        CnCacheDelete(urls->rgwszUrl[i]);
    CnFree(urls);
    return TRUE;
}
