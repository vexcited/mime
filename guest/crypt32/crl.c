#include "crypt32_private.h"

#define CERT_STORE_TIME_VALIDITY_FLAG 0x00000002
#define CERT_STORE_BASE_CRL_FLAG 0x00000100
#define CERT_STORE_DELTA_CRL_FLAG 0x00000200

static cr_cert *of(PCCRL_CONTEXT crl)
{
    return (cr_cert *)crl;
}

cr_cert *CrCreateCrl(const BYTE *encoded, DWORD size)
{
    der_reader r = {encoded, size};
    der_item outer;
    DWORD error = DerExpect(&r, ASN_SEQUENCE, &outer);
    if (error) {
        SetLastError(error);
        return nullptr;
    }
    DWORD info_size;
    CERT_SIGNED_CONTENT_INFO *signed_info = CrDecodeAlloc(X509_CERT, outer.start, outer.total, 0, &info_size);
    if (!signed_info)
        return nullptr;
    CRL_INFO *info = CrDecodeAlloc(X509_CERT_CRL_TO_BE_SIGNED, signed_info->ToBeSigned.pbData,
                                   signed_info->ToBeSigned.cbData, CRYPT_DECODE_TO_BE_SIGNED_FLAG, &info_size);
    CrFree(signed_info);
    if (!info)
        return nullptr;
    cr_cert *crl = CrAllocZero(sizeof(cr_cert) + outer.total);
    if (!crl) {
        CrFree(info);
        SetLastError(ERROR_OUTOFMEMORY);
        return nullptr;
    }
    BYTE *copy = (BYTE *)(crl + 1);
    memcpy(copy, outer.start, outer.total);
    crl->kind = CR_CRL;
    crl->context.dwCertEncodingType = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
    crl->context.pbCertEncoded = copy;
    crl->context.cbCertEncoded = outer.total;
    crl->context.pCertInfo = (PCERT_INFO)info;
    crl->refs = 1;
    return crl;
}

PCCRL_CONTEXT WINAPI CertCreateCRLContext(DWORD dwCertEncodingType, const BYTE *pbCrlEncoded, DWORD cbCrlEncoded)
{
    if (GET_CERT_ENCODING_TYPE(dwCertEncodingType) != X509_ASN_ENCODING) {
        SetLastError(E_INVALIDARG);
        return nullptr;
    }
    if (!pbCrlEncoded || !cbCrlEncoded) {
        SetLastError(CRYPT_E_ASN1_EOD);
        return nullptr;
    }
    cr_cert *crl = CrCreateCrl(pbCrlEncoded, cbCrlEncoded);
    if (!crl)
        return nullptr;
    crl->context.dwCertEncodingType = dwCertEncodingType;
    return (PCCRL_CONTEXT)&crl->context;
}

PCCRL_CONTEXT WINAPI CertDuplicateCRLContext(PCCRL_CONTEXT pCrlContext)
{
    if (pCrlContext)
        InterlockedIncrement(&of(pCrlContext)->refs);
    return pCrlContext;
}

BOOL WINAPI CertFreeCRLContext(PCCRL_CONTEXT pCrlContext)
{
    if (pCrlContext)
        CrReleaseCert(of(pCrlContext));
    return TRUE;
}

static BOOL is_delta(PCCRL_CONTEXT crl)
{
    const CRL_INFO *info = crl->pCrlInfo;
    return CertFindExtension(szOID_DELTA_CRL_INDICATOR, info->cExtension, info->rgExtension) != nullptr;
}

static BOOL same_extension(const CRL_INFO *a, const CRL_INFO *b, LPCSTR oid)
{
    PCERT_EXTENSION x = CertFindExtension(oid, a->cExtension, a->rgExtension);
    PCERT_EXTENSION y = CertFindExtension(oid, b->cExtension, b->rgExtension);
    if (!x || !y)
        return !x && !y;
    return x->Value.cbData == y->Value.cbData && !memcmp(x->Value.pbData, y->Value.pbData, x->Value.cbData);
}

BOOL CrSameCrl(PCCRL_CONTEXT a, PCCRL_CONTEXT b)
{
    if (a->cbCrlEncoded == b->cbCrlEncoded && !memcmp(a->pbCrlEncoded, b->pbCrlEncoded, a->cbCrlEncoded))
        return TRUE;
    return CertCompareCertificateName(X509_ASN_ENCODING, &a->pCrlInfo->Issuer, &b->pCrlInfo->Issuer) &&
           is_delta(a) == is_delta(b) && same_extension(a->pCrlInfo, b->pCrlInfo, szOID_ISSUING_DIST_POINT);
}

/* ------------------------------------------------------------------ stores */

BOOL WINAPI CertAddCRLContextToStore(HCERTSTORE hCertStore, PCCRL_CONTEXT pCrlContext, DWORD dwAddDisposition,
                                     PCCRL_CONTEXT *ppStoreContext)
{
    if (ppStoreContext)
        *ppStoreContext = nullptr;
    if (!pCrlContext || (hCertStore && !CrStore(hCertStore)))
        return CrFail(E_INVALIDARG);
    return CrAddContext(CrStore(hCertStore), CR_CRL, pCrlContext, dwAddDisposition, (const void **)ppStoreContext);
}

BOOL WINAPI CertAddCRLLinkToStore(HCERTSTORE hCertStore, PCCRL_CONTEXT pCrlContext, DWORD dwAddDisposition,
                                  PCCRL_CONTEXT *ppStoreContext)
{
    return CertAddCRLContextToStore(hCertStore, pCrlContext, dwAddDisposition, ppStoreContext);
}

BOOL WINAPI CertAddEncodedCRLToStore(HCERTSTORE hCertStore, DWORD dwCertEncodingType, const BYTE *pbCrlEncoded,
                                     DWORD cbCrlEncoded, DWORD dwAddDisposition, PCCRL_CONTEXT *ppCrlContext)
{
    if (ppCrlContext)
        *ppCrlContext = nullptr;
    PCCRL_CONTEXT crl = CertCreateCRLContext(dwCertEncodingType, pbCrlEncoded, cbCrlEncoded);
    if (!crl)
        return FALSE;
    BOOL ok = CertAddCRLContextToStore(hCertStore, crl, dwAddDisposition, ppCrlContext);
    CertFreeCRLContext(crl);
    return ok;
}

BOOL WINAPI CertDeleteCRLFromStore(PCCRL_CONTEXT pCrlContext)
{
    if (!pCrlContext)
        return CrFail(E_INVALIDARG);
    cr_cert *crl = of(pCrlContext);
    BOOL ok = TRUE;
    if (crl->store) {
        if (crl->store->flags & CERT_STORE_READONLY_FLAG)
            ok = CrFail(E_ACCESSDENIED);
        else if (crl->store->reg && !crl->deleted && !CrRegDeleteCert(crl->store, &crl->context))
            ok = FALSE;
        else
            CrUnlinkContext(crl);
    }
    CrReleaseCert(crl);
    return ok;
}

PCCRL_CONTEXT WINAPI CertEnumCRLsInStore(HCERTSTORE hCertStore, PCCRL_CONTEXT pPrevCrlContext)
{
    cr_store *store = CrStore(hCertStore);
    if (!store) {
        CertFreeCRLContext(pPrevCrlContext);
        SetLastError(E_INVALIDARG);
        return nullptr;
    }
    if (!pPrevCrlContext)
        CrAutoResync(store);
    cr_cert *found = CrEnumStore(store, pPrevCrlContext ? of(pPrevCrlContext) : nullptr, CR_CRL);
    CertFreeCRLContext(pPrevCrlContext);
    if (!found) {
        SetLastError(CRYPT_E_NOT_FOUND);
        return nullptr;
    }
    return (PCCRL_CONTEXT)&found->context;
}

/* whether the authority key identifier of a list names the issuer: its key id, or its issuer and serial number. */
static BOOL aki_matches(PCCRL_CONTEXT crl, PCCERT_CONTEXT issuer)
{
    const CRL_INFO *info = crl->pCrlInfo;
    PCERT_EXTENSION ext = CertFindExtension(szOID_AUTHORITY_KEY_IDENTIFIER2, info->cExtension, info->rgExtension);
    if (!ext)
        return TRUE;
    DWORD size;
    CERT_AUTHORITY_KEY_ID2_INFO *aki = CrDecodeAlloc(X509_AUTHORITY_KEY_ID2, ext->Value.pbData, ext->Value.cbData, 0,
                                                     &size);
    if (!aki)
        return FALSE;
    BOOL ok = TRUE;
    if (aki->KeyId.cbData) {
        BYTE id[CR_MAX_HASH];
        DWORD id_size = sizeof(id);
        ok = CertGetCertificateContextProperty(issuer, CERT_KEY_IDENTIFIER_PROP_ID, id, &id_size) &&
             id_size == aki->KeyId.cbData && !memcmp(id, aki->KeyId.pbData, id_size);
    } else if (aki->AuthorityCertSerialNumber.cbData) {
        ok = CertCompareIntegerBlob(&aki->AuthorityCertSerialNumber, &issuer->pCertInfo->SerialNumber);
        for (DWORD i = 0; ok && i < aki->AuthorityCertIssuer.cAltEntry; i++) {
            const CERT_ALT_NAME_ENTRY *e = &aki->AuthorityCertIssuer.rgAltEntry[i];
            if (e->dwAltNameChoice == CERT_ALT_NAME_DIRECTORY_NAME)
                ok = CertCompareCertificateName(X509_ASN_ENCODING, (PCERT_NAME_BLOB)&e->DirectoryName,
                                                &issuer->pCertInfo->Issuer);
        }
    }
    CrFree(aki);
    return ok;
}

static BOOL issued_by(PCCRL_CONTEXT crl, PCCERT_CONTEXT issuer, DWORD flags)
{
    if ((flags & CRL_FIND_ISSUED_BY_DELTA_FLAG) && !is_delta(crl))
        return FALSE;
    if ((flags & CRL_FIND_ISSUED_BY_BASE_FLAG) && is_delta(crl))
        return FALSE;
    if (!issuer)
        return TRUE;
    if (!CertCompareCertificateName(X509_ASN_ENCODING, &crl->pCrlInfo->Issuer, &issuer->pCertInfo->Subject))
        return FALSE;
    if ((flags & CRL_FIND_ISSUED_BY_AKI_FLAG) && !aki_matches(crl, issuer))
        return FALSE;
    if ((flags & CRL_FIND_ISSUED_BY_SIGNATURE_FLAG) &&
        CrVerifySignedBlob(crl->pbCrlEncoded, crl->cbCrlEncoded, &issuer->pCertInfo->SubjectPublicKeyInfo))
        return FALSE;
    return TRUE;
}

static BOOL crl_matches(PCCRL_CONTEXT crl, DWORD type, DWORD flags, const void *para)
{
    switch (type) {
    case CRL_FIND_ANY:
        return TRUE;
    case CRL_FIND_ISSUED_BY:
        return issued_by(crl, para, flags);
    case CRL_FIND_EXISTING:
        return para && CrSameCrl(crl, para);
    case CRL_FIND_ISSUED_FOR: {
        const CRL_FIND_ISSUED_FOR_PARA *f = para;
        if (!f || !f->pSubjectCert || !f->pIssuerCert)
            return FALSE;
        if (!CertCompareCertificateName(X509_ASN_ENCODING, &crl->pCrlInfo->Issuer, &f->pSubjectCert->pCertInfo->Issuer))
            return FALSE;
        return issued_by(crl, f->pIssuerCert, flags & ~CRL_FIND_ISSUED_FOR_SET_STRONG_PROPERTIES_FLAG);
    }
    }
    return FALSE;
}

PCCRL_CONTEXT WINAPI CertFindCRLInStore(HCERTSTORE hCertStore, DWORD dwCertEncodingType, DWORD dwFindFlags,
                                        DWORD dwFindType, const void *pvFindPara, PCCRL_CONTEXT pPrevCrlContext)
{
    if (dwFindType > CRL_FIND_ISSUED_FOR) {
        CertFreeCRLContext(pPrevCrlContext);
        SetLastError(E_INVALIDARG);
        return nullptr;
    }
    PCCRL_CONTEXT crl = pPrevCrlContext;
    while ((crl = CertEnumCRLsInStore(hCertStore, crl)))
        if (crl_matches(crl, dwFindType, dwFindFlags, pvFindPara))
            return crl;
    return nullptr;
}

PCCRL_CONTEXT WINAPI CertGetCRLFromStore(HCERTSTORE hCertStore, PCCERT_CONTEXT pIssuerContext,
                                         PCCRL_CONTEXT pPrevCrlContext, DWORD *pdwFlags)
{
    if (!pdwFlags || (*pdwFlags & ~(CERT_STORE_SIGNATURE_FLAG | CERT_STORE_TIME_VALIDITY_FLAG |
                                    CERT_STORE_BASE_CRL_FLAG | CERT_STORE_DELTA_CRL_FLAG))) {
        CertFreeCRLContext(pPrevCrlContext);
        SetLastError(E_INVALIDARG);
        return nullptr;
    }
    DWORD flags = *pdwFlags;
    DWORD kind = 0;
    if ((flags & (CERT_STORE_BASE_CRL_FLAG | CERT_STORE_DELTA_CRL_FLAG)) == CERT_STORE_BASE_CRL_FLAG)
        kind = CRL_FIND_ISSUED_BY_BASE_FLAG;
    else if ((flags & (CERT_STORE_BASE_CRL_FLAG | CERT_STORE_DELTA_CRL_FLAG)) == CERT_STORE_DELTA_CRL_FLAG)
        kind = CRL_FIND_ISSUED_BY_DELTA_FLAG;
    PCCRL_CONTEXT crl = CertFindCRLInStore(hCertStore, 0, kind, CRL_FIND_ISSUED_BY, pIssuerContext, pPrevCrlContext);
    if (!crl)
        return nullptr;
    if ((flags & CERT_STORE_SIGNATURE_FLAG) && pIssuerContext &&
        !CrVerifySignedBlob(crl->pbCrlEncoded, crl->cbCrlEncoded, &pIssuerContext->pCertInfo->SubjectPublicKeyInfo))
        flags &= ~CERT_STORE_SIGNATURE_FLAG;
    if ((flags & CERT_STORE_TIME_VALIDITY_FLAG) && !CertVerifyCRLTimeValidity(nullptr, crl->pCrlInfo))
        flags &= ~CERT_STORE_TIME_VALIDITY_FLAG;
    *pdwFlags = flags;
    return crl;
}

/* ------------------------------------------------------------------ properties */

BOOL WINAPI CertGetCRLContextProperty(PCCRL_CONTEXT pCrlContext, DWORD dwPropId, void *pvData, DWORD *pcbData)
{
    if (!pCrlContext || !pcbData)
        return CrFail(E_INVALIDARG);
    cr_cert *crl = of(pCrlContext);
    CrLock();
    cr_prop *p = CrFindProp(crl, dwPropId);
    if (p) {
        BOOL ok = CrReturnData(p->data, p->size, pvData, pcbData);
        CrUnlock();
        return ok;
    }
    CrUnlock();
    if (dwPropId == CERT_ACCESS_STATE_PROP_ID) {
        DWORD state = 0;
        if (crl->store && crl->store->system) {
            state |= CERT_ACCESS_STATE_SYSTEM_STORE_FLAG;
            if (!(crl->store->flags & CERT_STORE_READONLY_FLAG))
                state |= CERT_ACCESS_STATE_WRITE_PERSIST_FLAG;
        }
        return CrReturnData(&state, sizeof(state), pvData, pcbData);
    }
    if (dwPropId != CERT_SHA1_HASH_PROP_ID && dwPropId != CERT_MD5_HASH_PROP_ID &&
        dwPropId != CERT_SHA256_HASH_PROP_ID && dwPropId != CERT_SIGNATURE_HASH_PROP_ID)
        return CrFail(CRYPT_E_NOT_FOUND);
    BYTE hash[CR_MAX_HASH];
    DWORD size;
    if (!CrCertHash(&crl->context, dwPropId, hash, &size))
        return CrFail(CRYPT_E_NOT_FOUND);
    if (dwPropId != CERT_SHA256_HASH_PROP_ID)
        CrStoreProp(crl, dwPropId, hash, size);
    return CrReturnData(hash, size, pvData, pcbData);
}

BOOL WINAPI CertSetCRLContextProperty(PCCRL_CONTEXT pCrlContext, DWORD dwPropId, DWORD dwFlags, const void *pvData)
{
    if (!pCrlContext || !dwPropId || dwPropId == CERT_ACCESS_STATE_PROP_ID ||
        (dwPropId >= CERT_CERT_PROP_ID && dwPropId <= CERT_CTL_PROP_ID))
        return CrFail(E_INVALIDARG);
    cr_cert *crl = of(pCrlContext);
    cr_store *store = crl->store;
    BOOL persist = store && store->reg && !crl->deleted && !(dwFlags & CERT_SET_PROPERTY_INHIBIT_PERSIST_FLAG) &&
                   dwPropId != CERT_SHA1_HASH_PROP_ID;
    BOOL strict = !(dwFlags & CERT_SET_PROPERTY_IGNORE_PERSIST_ERROR_FLAG);
    if (persist && strict && !CrRegCanWrite(store))
        return CrFail(E_ACCESSDENIED);
    BOOL ok;
    if (!pvData) {
        CrLock();
        CrRemoveProp(crl, dwPropId);
        CrUnlock();
        ok = TRUE;
    } else if (dwPropId == CERT_DATE_STAMP_PROP_ID) {
        ok = CrStoreProp(crl, dwPropId, pvData, sizeof(FILETIME));
    } else {
        const CRYPT_DATA_BLOB *blob = pvData;
        ok = CrStoreProp(crl, dwPropId, blob->pbData, blob->cbData);
    }
    if (!ok)
        return FALSE;
    return !persist || CrRegWriteCert(store, &crl->context) || !strict;
}

DWORD WINAPI CertEnumCRLContextProperties(PCCRL_CONTEXT pCrlContext, DWORD dwPropId)
{
    return CertEnumCertificateContextProperties((PCCERT_CONTEXT)pCrlContext, dwPropId);
}

/* ------------------------------------------------------------------ checks */

BOOL WINAPI CertFindCertificateInCRL(PCCERT_CONTEXT pCert, PCCRL_CONTEXT pCrlContext, DWORD dwFlags,
                                     void *pvReserved, PCRL_ENTRY *ppCrlEntry)
{
    if (!pCert || !pCrlContext || !ppCrlEntry)
        return CrFail(E_INVALIDARG);
    *ppCrlEntry = nullptr;
    const CRL_INFO *info = pCrlContext->pCrlInfo;
    for (DWORD i = 0; i < info->cCRLEntry; i++)
        if (CertCompareIntegerBlob(&info->rgCRLEntry[i].SerialNumber, &pCert->pCertInfo->SerialNumber)) {
            *ppCrlEntry = &info->rgCRLEntry[i];
            break;
        }
    return TRUE;
}

BOOL WINAPI CertVerifyCRLRevocation(DWORD dwCertEncodingType, PCERT_INFO pCertId, DWORD cCrlInfo,
                                    PCRL_INFO rgpCrlInfo[])
{
    for (DWORD i = 0; i < cCrlInfo; i++)
        for (DWORD j = 0; j < rgpCrlInfo[i]->cCRLEntry; j++)
            if (CertCompareIntegerBlob(&rgpCrlInfo[i]->rgCRLEntry[j].SerialNumber, &pCertId->SerialNumber))
                return FALSE;
    return TRUE;
}

LONG WINAPI CertVerifyCRLTimeValidity(LPFILETIME pTimeToVerify, PCRL_INFO pCrlInfo)
{
    FILETIME now;
    if (!pTimeToVerify) {
        GetSystemTimeAsFileTime(&now);
        pTimeToVerify = &now;
    }
    if (CompareFileTime(pTimeToVerify, &pCrlInfo->ThisUpdate) < 0)
        return -1;
    if ((pCrlInfo->NextUpdate.dwLowDateTime || pCrlInfo->NextUpdate.dwHighDateTime) &&
        CompareFileTime(pTimeToVerify, &pCrlInfo->NextUpdate) > 0)
        return 1;
    return 0;
}

static BOOL same_alt_entry(const CERT_ALT_NAME_ENTRY *a, const CERT_ALT_NAME_ENTRY *b)
{
    if (a->dwAltNameChoice != b->dwAltNameChoice)
        return FALSE;
    switch (a->dwAltNameChoice) {
    case CERT_ALT_NAME_RFC822_NAME:
    case CERT_ALT_NAME_DNS_NAME:
    case CERT_ALT_NAME_URL:
        return a->pwszURL && b->pwszURL && !_wcsicmp(a->pwszURL, b->pwszURL);
    case CERT_ALT_NAME_DIRECTORY_NAME:
        return CertCompareCertificateName(X509_ASN_ENCODING, (PCERT_NAME_BLOB)&a->DirectoryName,
                                          (PCERT_NAME_BLOB)&b->DirectoryName);
    case CERT_ALT_NAME_IP_ADDRESS:
        return a->IPAddress.cbData == b->IPAddress.cbData &&
               !memcmp(a->IPAddress.pbData, b->IPAddress.pbData, a->IPAddress.cbData);
    case CERT_ALT_NAME_REGISTERED_ID:
        return !strcmp(a->pszRegisteredID, b->pszRegisteredID);
    case CERT_ALT_NAME_OTHER_NAME:
        return !strcmp(a->pOtherName->pszObjId, b->pOtherName->pszObjId) &&
               a->pOtherName->Value.cbData == b->pOtherName->Value.cbData &&
               !memcmp(a->pOtherName->Value.pbData, b->pOtherName->Value.pbData, a->pOtherName->Value.cbData);
    }
    return FALSE;
}

static BOOL is_ca(PCCERT_CONTEXT cert, BOOL *ca)
{
    *ca = FALSE;
    const CERT_INFO *info = cert->pCertInfo;
    PCERT_EXTENSION ext = CertFindExtension(szOID_BASIC_CONSTRAINTS2, info->cExtension, info->rgExtension);
    if (!ext)
        return TRUE;
    DWORD size;
    CERT_BASIC_CONSTRAINTS2_INFO *bc = CrDecodeAlloc(X509_BASIC_CONSTRAINTS2, ext->Value.pbData, ext->Value.cbData,
                                                     0, &size);
    if (!bc)
        return FALSE;
    *ca = bc->fCA;
    CrFree(bc);
    return TRUE;
}

/*
 * a list without an issuing distribution point covers every certificate of its issuer. one with a point covers the
 * user or authority certificates it says, and when it names itself, only the certificates whose distribution points
 * name it too. lists for some reasons only and indirect lists are refused as windows does.
 */
BOOL WINAPI CertIsValidCRLForCertificate(PCCERT_CONTEXT pCert, PCCRL_CONTEXT pCrl, DWORD dwFlags, void *pvReserved)
{
    const CRL_INFO *info = pCrl->pCrlInfo;
    PCERT_EXTENSION ext = CertFindExtension(szOID_ISSUING_DIST_POINT, info->cExtension, info->rgExtension);
    if (!ext)
        return TRUE;
    DWORD size;
    CRL_ISSUING_DIST_POINT *idp = CrDecodeAlloc(X509_ISSUING_DIST_POINT, ext->Value.pbData, ext->Value.cbData, 0,
                                                &size);
    if (!idp)
        return FALSE;
    BOOL ok = FALSE;
    DWORD error = 0;
    CRL_DIST_POINTS_INFO *cdp = nullptr;
    if (idp->OnlySomeReasonFlags.cbData || idp->fIndirectCRL ||
        idp->DistPointName.dwDistPointNameChoice > CRL_DIST_POINT_FULL_NAME) {
        error = E_NOTIMPL;
        goto done;
    }
    if (idp->fOnlyContainsUserCerts || idp->fOnlyContainsCACerts) {
        BOOL ca;
        if (!is_ca(pCert, &ca))
            goto done;
        if ((idp->fOnlyContainsUserCerts && ca) || (idp->fOnlyContainsCACerts && !ca)) {
            error = CRYPT_E_NO_MATCH;
            goto done;
        }
    }
    if (idp->DistPointName.dwDistPointNameChoice != CRL_DIST_POINT_FULL_NAME || !idp->DistPointName.FullName.cAltEntry) {
        ok = TRUE;
        goto done;
    }
    const CERT_INFO *cert = pCert->pCertInfo;
    PCERT_EXTENSION points = CertFindExtension(szOID_CRL_DIST_POINTS, cert->cExtension, cert->rgExtension);
    if (!points) {
        ok = TRUE;
        goto done;
    }
    if (!(cdp = CrDecodeAlloc(X509_CRL_DIST_POINTS, points->Value.pbData, points->Value.cbData, 0, &size)))
        goto done;
    const CERT_ALT_NAME_INFO *names = &idp->DistPointName.FullName;
    for (DWORD i = 0; i < names->cAltEntry && !ok; i++)
        for (DWORD j = 0; j < cdp->cDistPoint && !ok; j++) {
            const CRL_DIST_POINT *point = &cdp->rgDistPoint[j];
            if (point->DistPointName.dwDistPointNameChoice != CRL_DIST_POINT_FULL_NAME ||
                point->ReasonFlags.cbData || point->CRLIssuer.cAltEntry)
                continue;
            for (DWORD k = 0; k < point->DistPointName.FullName.cAltEntry && !ok; k++)
                ok = same_alt_entry(&names->rgAltEntry[i], &point->DistPointName.FullName.rgAltEntry[k]);
        }
    if (!ok)
        error = CRYPT_E_NO_MATCH;
done:
    CrFree(cdp);
    CrFree(idp);
    if (!ok && error)
        SetLastError(error);
    return ok;
}
