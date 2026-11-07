#include "cryptnet_private.h"

#define DEFAULT_TIMEOUT 15000

/* the lists this process fetched or read, by url, with the issuer key whose signature was checked. */
typedef struct cached {
    struct cached *next;
    WCHAR *url;
    PCCRL_CONTEXT crl;
    BYTE issuer[20];
} cached;

static SRWLOCK lock = SRWLOCK_INIT;
static cached *memory;

static const char *const supported_critical[] = {
    szOID_DELTA_CRL_INDICATOR, szOID_ISSUING_DIST_POINT,   szOID_FRESHEST_CRL,       szOID_CRL_NUMBER,
    szOID_AUTHORITY_KEY_IDENTIFIER2, "1.3.6.1.5.5.7.48.1.1", "1.3.6.1.4.1.311.10.11.107",
};

static BOOL unsupported_critical(PCCRL_CONTEXT crl)
{
    const CRL_INFO *info = crl->pCrlInfo;
    for (DWORD i = 0; i < info->cExtension; i++) {
        if (!info->rgExtension[i].fCritical)
            continue;
        BOOL known = FALSE;
        for (DWORD j = 0; j < RTL_NUMBER_OF(supported_critical) && !known; j++)
            known = !strcmp(info->rgExtension[i].pszObjId, supported_critical[j]);
        if (!known)
            return TRUE;
    }
    return FALSE;
}

static void key_hash(PCCERT_CONTEXT cert, BYTE *out)
{
    DWORD size = 20;
    const CRYPT_BIT_BLOB *key = &cert->pCertInfo->SubjectPublicKeyInfo.PublicKey;
    CryptHashCertificate(0, CALG_SHA1, 0, key->pbData, key->cbData, out, &size);
}

/* an issuer with a key usage extension must be allowed to sign lists. */
static BOOL may_sign_crls(PCCERT_CONTEXT issuer)
{
    const CERT_INFO *info = issuer->pCertInfo;
    if (!CertFindExtension(szOID_KEY_USAGE, info->cExtension, info->rgExtension))
        return TRUE;
    BYTE usage[2] = {};
    return CertGetIntendedKeyUsage(X509_ASN_ENCODING, (PCERT_INFO)info, usage, sizeof(usage)) &&
           (usage[0] & CERT_CRL_SIGN_KEY_USAGE);
}

/* a base list the issuer signed, which covers the certificate. */
static BOOL usable(PCCRL_CONTEXT crl, PCCERT_CONTEXT cert, PCCERT_CONTEXT issuer, BOOL signature_known)
{
    const CRL_INFO *info = crl->pCrlInfo;
    if (CertFindExtension(szOID_DELTA_CRL_INDICATOR, info->cExtension, info->rgExtension))
        return FALSE;
    if (!CertCompareCertificateName(X509_ASN_ENCODING, (PCERT_NAME_BLOB)&info->Issuer,
                                    &issuer->pCertInfo->Subject))
        return FALSE;
    if (!signature_known &&
        !CryptVerifyCertificateSignatureEx(0, X509_ASN_ENCODING, CRYPT_VERIFY_CERT_SIGN_SUBJECT_CRL, (void *)crl,
                                           CRYPT_VERIFY_CERT_SIGN_ISSUER_CERT, (void *)issuer, 0, nullptr))
        return FALSE;
    return CertIsValidCRLForCertificate(cert, crl, 0, nullptr);
}

typedef struct {
    PCCERT_CONTEXT cert;
    PCCERT_CONTEXT issuer;
    BYTE issuer_key[20];
    ULONGLONG now;
    BOOL freshness;
    DWORD freshness_seconds;
} subject;

/* time valid: the current time is before the next update, and the list is as fresh as asked. */
static BOOL time_valid(const subject *s, PCCRL_CONTEXT crl)
{
    const CRL_INFO *info = crl->pCrlInfo;
    ULONGLONG next = CnTime(&info->NextUpdate);
    if (next && s->now > next)
        return FALSE;
    if (s->freshness && s->now > CnTime(&info->ThisUpdate) + 10000000ULL * s->freshness_seconds)
        return FALSE;
    return TRUE;
}

/* keeps the newer of two candidates, which are references. */
static void prefer(PCCRL_CONTEXT *best, PCCRL_CONTEXT crl)
{
    if (*best && CompareFileTime(&(*best)->pCrlInfo->ThisUpdate, &crl->pCrlInfo->ThisUpdate) >= 0) {
        CertFreeCRLContext(crl);
        return;
    }
    CertFreeCRLContext(*best);
    *best = crl;
}

static void remember(LPCWSTR url, PCCRL_CONTEXT crl, const BYTE *issuer)
{
    AcquireSRWLockExclusive(&lock);
    cached *c = memory;
    while (c && _wcsicmp(c->url, url))
        c = c->next;
    if (!c && (c = CnAlloc(sizeof(*c)))) {
        SIZE_T n = (wcslen(url) + 1) * sizeof(WCHAR);
        if ((c->url = CnAlloc(n))) {
            memcpy(c->url, url, n);
            c->next = memory;
            memory = c;
        } else {
            CnFree(c);
            c = nullptr;
        }
    }
    if (c && (!c->crl || CompareFileTime(&c->crl->pCrlInfo->ThisUpdate, &crl->pCrlInfo->ThisUpdate) <= 0)) {
        CertFreeCRLContext(c->crl);
        c->crl = CertDuplicateCRLContext(crl);
        memcpy(c->issuer, issuer, 20);
    }
    ReleaseSRWLockExclusive(&lock);
}

static PCCRL_CONTEXT recall(LPCWSTR url, const BYTE *issuer)
{
    PCCRL_CONTEXT crl = nullptr;
    AcquireSRWLockShared(&lock);
    for (cached *c = memory; c; c = c->next)
        if (!_wcsicmp(c->url, url)) {
            if (c->crl && !memcmp(c->issuer, issuer, 20))
                crl = CertDuplicateCRLContext(c->crl);
            break;
        }
    ReleaseSRWLockShared(&lock);
    return crl;
}

/* a list from this process, the url cache or the caller's store; with need_time, only a time valid one. */
static PCCRL_CONTEXT stored_crl(const subject *s, const CRYPT_URL_ARRAY *urls, HCERTSTORE store, BOOL need_time)
{
    PCCRL_CONTEXT best = nullptr;
    for (DWORD i = 0; urls && i < urls->cUrl; i++) {
        PCCRL_CONTEXT crl = recall(urls->rgwszUrl[i], s->issuer_key);
        if (crl && (!usable(crl, s->cert, s->issuer, TRUE) || (need_time && !time_valid(s, crl)))) {
            CertFreeCRLContext(crl);
            crl = nullptr;
        }
        if (!crl) {
            PCCRL_CONTEXT disk = nullptr;
            if (CryptRetrieveObjectByUrlW(urls->rgwszUrl[i], CONTEXT_OID_CRL, CRYPT_CACHE_ONLY_RETRIEVAL, 0,
                                          (void **)&disk, nullptr, nullptr, nullptr, nullptr)) {
                if (usable(disk, s->cert, s->issuer, FALSE)) {
                    remember(urls->rgwszUrl[i], disk, s->issuer_key);
                    if (!need_time || time_valid(s, disk))
                        crl = disk;
                }
                if (crl != disk)
                    CertFreeCRLContext(disk);
            }
        }
        if (crl)
            prefer(&best, crl);
    }
    if (store) {
        CRL_FIND_ISSUED_FOR_PARA para = {s->cert, s->issuer};
        PCCRL_CONTEXT crl = nullptr;
        while ((crl = CertFindCRLInStore(store, X509_ASN_ENCODING,
                                         CRL_FIND_ISSUED_BY_AKI_FLAG | CRL_FIND_ISSUED_BY_SIGNATURE_FLAG |
                                             CRL_FIND_ISSUED_BY_BASE_FLAG,
                                         CRL_FIND_ISSUED_FOR, &para, crl)))
            if (usable(crl, s->cert, s->issuer, TRUE) && (!need_time || time_valid(s, crl)))
                prefer(&best, CertDuplicateCRLContext(crl));
    }
    return best;
}

/* brings each url's list from the wire until one serves; the error says whether any server answered. */
static PCCRL_CONTEXT wire_crl(const subject *s, const CRYPT_URL_ARRAY *urls, DWORD timeout, const ULONGLONG *deadline,
                              DWORD *error)
{
    *error = (DWORD)CRYPT_E_REVOCATION_OFFLINE;
    BOOL found_none = TRUE;
    PCCRL_CONTEXT best = nullptr;
    for (DWORD i = 0; i < urls->cUrl && !best; i++) {
        DWORD left = timeout;
        if (deadline) {
            ULONGLONG now = CnNow();
            if (now >= *deadline)
                break;
            left = (DWORD)((*deadline - now) / 10000);
            if (!left)
                left = 1;
        }
        PCCRL_CONTEXT crl = nullptr;
        if (!CryptRetrieveObjectByUrlW(urls->rgwszUrl[i], CONTEXT_OID_CRL, CRYPT_WIRE_ONLY_RETRIEVAL, left,
                                       (void **)&crl, nullptr, nullptr, nullptr, nullptr)) {
            if (GetLastError() != (DWORD)CRYPT_E_NOT_FOUND)
                found_none = FALSE;
            continue;
        }
        found_none = FALSE;
        if (usable(crl, s->cert, s->issuer, FALSE)) {
            remember(urls->rgwszUrl[i], crl, s->issuer_key);
            best = crl;
        } else {
            CertFreeCRLContext(crl);
        }
    }
    if (!best && found_none && urls->cUrl)
        *error = (DWORD)CRYPT_E_NO_REVOCATION_CHECK;
    return best;
}

static PCCERT_CONTEXT find_issuer_in(HCERTSTORE store, PCCERT_CONTEXT cert)
{
    PCCERT_CONTEXT issuer = nullptr;
    while ((issuer = CertFindCertificateInStore(store, X509_ASN_ENCODING, 0, CERT_FIND_SUBJECT_NAME,
                                                &cert->pCertInfo->Issuer, issuer)))
        if (CryptVerifyCertificateSignatureEx(0, X509_ASN_ENCODING, CRYPT_VERIFY_CERT_SIGN_SUBJECT_CERT, (void *)cert,
                                              CRYPT_VERIFY_CERT_SIGN_ISSUER_CERT, (void *)issuer, 0, nullptr))
            return issuer;
    return nullptr;
}

/* the issuer the caller gave, the certificate itself when it issued itself, or one from the stores. */
static PCCERT_CONTEXT find_issuer(PCCERT_CONTEXT cert, const CERT_REVOCATION_PARA *para)
{
    if (para->pIssuerCert)
        return CertDuplicateCertificateContext(para->pIssuerCert);
    if (CertCompareCertificateName(X509_ASN_ENCODING, &cert->pCertInfo->Subject, &cert->pCertInfo->Issuer))
        return CertDuplicateCertificateContext(cert);
    for (DWORD i = 0; i < para->cCertStore && para->rgCertStore; i++) {
        PCCERT_CONTEXT issuer = find_issuer_in(para->rgCertStore[i], cert);
        if (issuer)
            return issuer;
    }
    static const PCWSTR names[] = {L"CA", L"ROOT", L"MY"};
    for (DWORD i = 0; i < RTL_NUMBER_OF(names); i++) {
        HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
                                         CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_READONLY_FLAG, names[i]);
        if (!store)
            continue;
        PCCERT_CONTEXT issuer = find_issuer_in(store, cert);
        CertCloseStore(store, 0);
        if (issuer)
            return issuer;
    }
    return nullptr;
}

static DWORD entry_reason(const CRL_ENTRY *entry)
{
    PCERT_EXTENSION ext = CertFindExtension(szOID_CRL_REASON_CODE, entry->cExtension, entry->rgExtension);
    if (!ext)
        return CRL_REASON_UNSPECIFIED;
    int reason = 0;
    DWORD size = sizeof(reason);
    if (!CryptDecodeObjectEx(X509_ASN_ENCODING, X509_CRL_REASON_CODE, ext->Value.pbData, ext->Value.cbData, 0, nullptr,
                             &reason, &size))
        return CRL_REASON_UNSPECIFIED;
    return (DWORD)reason;
}

/*
 * the default revocation provider, for certificates with lists: a time valid list from this process, the url cache
 * or the caller's store, else from the distribution points, else an expired one, which leaves the answer offline
 * unless it names the certificate. a list that does not name it says it is good. one certificate is checked per
 * call: past the first, dwIndex says where the caller goes on.
 */
WINCRYPTNETAPI BOOL WINAPI CertDllVerifyRevocation(DWORD dwEncodingType, DWORD dwRevType, DWORD cContext,
                                                   PVOID rgpvContext[], DWORD dwFlags, PCERT_REVOCATION_PARA pRevPara,
                                                   PCERT_REVOCATION_STATUS pRevStatus)
{
    CERT_REVOCATION_PARA para = {sizeof(para)};
    if (pRevPara)
        memcpy(&para, pRevPara, pRevPara->cbSize < sizeof(para) ? pRevPara->cbSize : sizeof(para));
    DWORD timeout = para.dwUrlRetrievalTimeout ? para.dwUrlRetrievalTimeout : DEFAULT_TIMEOUT;
    ULONGLONG deadline = 0;
    if (dwFlags & CERT_VERIFY_REV_ACCUMULATIVE_TIMEOUT_FLAG)
        deadline = CnNow() + 10000ULL * timeout;
    DWORD error = 0, reason = 0, index = 0;
    BOOL has_fresh = FALSE;
    DWORD freshness = 0;
    PCCERT_CONTEXT issuer = nullptr;
    PCCRL_CONTEXT crl = nullptr;
    PCRYPT_URL_ARRAY urls = nullptr;
    PCRL_ENTRY entry = nullptr;
    if (!cContext || !rgpvContext) {
        error = (DWORD)E_INVALIDARG;
        goto done;
    }
    if (GET_CERT_ENCODING_TYPE(dwEncodingType) != X509_ASN_ENCODING || dwRevType != CERT_CONTEXT_REVOCATION_TYPE) {
        error = (DWORD)CRYPT_E_NO_REVOCATION_CHECK;
        goto done;
    }
    PCCERT_CONTEXT cert = rgpvContext[0];
    if (!(issuer = find_issuer(cert, &para))) {
        error = (DWORD)CRYPT_E_REVOCATION_OFFLINE;
        goto done;
    }
    subject s = {cert, issuer};
    key_hash(issuer, s.issuer_key);
    s.now = para.pftCurrentTime ? CnTime(para.pftCurrentTime) : CnNow();
    s.freshness = para.fCheckFreshnessTime;
    s.freshness_seconds = para.dwFreshnessTime;
    urls = CnCrlUrls(cert);
    if (!may_sign_crls(issuer)) {
        error = (DWORD)CRYPT_E_NO_REVOCATION_CHECK;
        goto done;
    }
    BOOL valid_time = TRUE;
    crl = stored_crl(&s, urls, para.hCrlStore, TRUE);
    if (!crl && !urls) {
        error = (DWORD)CRYPT_E_NO_REVOCATION_CHECK;
        goto done;
    }
    if (!crl && !(dwFlags & CERT_VERIFY_CACHE_ONLY_BASED_REVOCATION)) {
        DWORD wire_error;
        crl = wire_crl(&s, urls, timeout, deadline ? &deadline : nullptr, &wire_error);
        if (crl && !time_valid(&s, crl))
            valid_time = FALSE;
        if (!crl)
            error = wire_error;
    }
    if (!crl) {
        crl = stored_crl(&s, urls, para.hCrlStore, FALSE);
        if (!crl) {
            if (!error)
                error = (DWORD)CRYPT_E_REVOCATION_OFFLINE;
            goto done;
        }
        error = 0;
        valid_time = FALSE;
    }
    if (unsupported_critical(crl)) {
        error = (DWORD)CRYPT_E_NO_REVOCATION_CHECK;
        goto done;
    }
    has_fresh = TRUE;
    ULONGLONG issued = CnTime(&crl->pCrlInfo->ThisUpdate);
    freshness = s.now > issued ? (DWORD)((s.now - issued) / 10000000) : 0;
    CertFindCertificateInCRL(cert, crl, 0, nullptr, &entry);
    if (entry && (reason = entry_reason(entry)) == CRL_REASON_REMOVE_FROM_CRL) {
        entry = nullptr;
        reason = 0;
    }
    if (entry && (!para.pftTimeToUse || CompareFileTime(para.pftTimeToUse, &entry->RevocationDate) >= 0))
        error = (DWORD)CRYPT_E_REVOKED;
    else if (!valid_time)
        error = (DWORD)CRYPT_E_REVOCATION_OFFLINE;
    if (!entry)
        reason = 0;
    if (pRevPara && pRevPara->cbSize >= FIELD_OFFSET(CERT_REVOCATION_PARA, pftCacheResync) && pRevPara->pCrlInfo &&
        pRevPara->pCrlInfo->cbSize >= sizeof(CERT_REVOCATION_CRL_INFO)) {
        PCERT_REVOCATION_CRL_INFO info = pRevPara->pCrlInfo;
        CertFreeCRLContext(info->pBaseCrlContext);
        CertFreeCRLContext(info->pDeltaCrlContext);
        info->pBaseCrlContext = CertDuplicateCRLContext(crl);
        info->pDeltaCrlContext = nullptr;
        info->pCrlEntry = entry;
        info->fDeltaCrlEntry = FALSE;
    }
    if (!error && cContext > 1) {
        index = 1;
        error = (DWORD)CRYPT_E_NO_REVOCATION_CHECK;
    }
done:
    CertFreeCRLContext(crl);
    CertFreeCertificateContext(issuer);
    CnFree(urls);
    if (pRevStatus) {
        pRevStatus->dwIndex = index;
        pRevStatus->dwError = error;
        pRevStatus->dwReason = reason;
        if (pRevStatus->cbSize >= sizeof(CERT_REVOCATION_STATUS)) {
            pRevStatus->fHasFreshnessTime = has_fresh;
            pRevStatus->dwFreshnessTime = freshness;
        }
    }
    SetLastError(error);
    return !error;
}
