/*
 * certificate contexts: creation, reference counting, properties and the
 * comparison helpers.
 */
#include "crypt32_private.h"

static RTL_CRITICAL_SECTION lock;

void CrInitLock(void)
{
    InitializeCriticalSection(&lock);
}

void CrLock(void)
{
    EnterCriticalSection(&lock);
}

void CrUnlock(void)
{
    LeaveCriticalSection(&lock);
}

cr_cert *CrCert(PCCERT_CONTEXT context)
{
    return (cr_cert *)context;
}

cr_cert *CrCreateCert(const BYTE *encoded, DWORD size)
{
    DWORD info_size;
    CERT_SIGNED_CONTENT_INFO *signed_info = CrDecodeAlloc(X509_CERT, encoded, size, 0, &info_size);
    if (!signed_info)
        return NULL;
    CERT_INFO *info = CrDecodeAlloc(X509_CERT_TO_BE_SIGNED, signed_info->ToBeSigned.pbData,
                                    signed_info->ToBeSigned.cbData, 0, &info_size);
    DWORD encoded_size = signed_info->ToBeSigned.pbData - encoded + signed_info->ToBeSigned.cbData;
    CrFree(signed_info);
    if (!info)
        return NULL;
    /* keep the whole outer sequence, not trailing bytes after it. */
    der_reader r = {encoded, size};
    der_item outer;
    if (!DerRead(&r, &outer))
        encoded_size = outer.total;
    cr_cert *cert = CrAllocZero(sizeof(cr_cert) + encoded_size);
    if (!cert) {
        CrFree(info);
        SetLastError(ERROR_OUTOFMEMORY);
        return NULL;
    }
    BYTE *copy = (BYTE *)(cert + 1);
    memcpy(copy, encoded, encoded_size);
    cert->context.dwCertEncodingType = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
    cert->context.pbCertEncoded = copy;
    cert->context.cbCertEncoded = encoded_size;
    cert->context.pCertInfo = info;
    cert->refs = 1;
    return cert;
}

void CrFreeProps(cr_cert *cert)
{
    cr_prop *p = cert->props;
    while (p) {
        cr_prop *next = p->next;
        CrFree(p);
        p = next;
    }
    cert->props = NULL;
}

void CrReleaseCert(cr_cert *cert)
{
    if (InterlockedDecrement(&cert->refs))
        return;
    cr_store *store = cert->store;
    if (store) {
        CrLock();
        if (cert->prev)
            cert->prev->next = cert->next;
        else if (store->head == cert)
            store->head = cert->next;
        if (cert->next)
            cert->next->prev = cert->prev;
        else if (store->tail == cert)
            store->tail = cert->prev;
        CrUnlock();
        CrReleaseStore(store);
    }
    CrFreeProps(cert);
    CrFree(cert->context.pCertInfo);
    CrFree(cert);
}

PCCERT_CONTEXT WINAPI CertCreateCertificateContext(DWORD dwCertEncodingType, const BYTE *pbCertEncoded,
                                                   DWORD cbCertEncoded)
{
    if (GET_CERT_ENCODING_TYPE(dwCertEncodingType) != X509_ASN_ENCODING) {
        SetLastError(E_INVALIDARG);
        return NULL;
    }
    if (!pbCertEncoded || !cbCertEncoded) {
        SetLastError(CRYPT_E_ASN1_EOD);
        return NULL;
    }
    cr_cert *cert = CrCreateCert(pbCertEncoded, cbCertEncoded);
    if (!cert)
        return NULL;
    cert->context.dwCertEncodingType = dwCertEncodingType;
    return &cert->context;
}

PCCERT_CONTEXT WINAPI CertDuplicateCertificateContext(PCCERT_CONTEXT pCertContext)
{
    if (!pCertContext)
        return NULL;
    InterlockedIncrement(&CrCert(pCertContext)->refs);
    return pCertContext;
}

BOOL WINAPI CertFreeCertificateContext(PCCERT_CONTEXT pCertContext)
{
    if (pCertContext)
        CrReleaseCert(CrCert(pCertContext));
    return TRUE;
}

/* ------------------------------------------------------------------ properties */

static cr_prop *find_prop(cr_cert *cert, DWORD id)
{
    for (cr_prop *p = cert->props; p; p = p->next)
        if (p->id == id)
            return p;
    return NULL;
}

static void remove_prop(cr_cert *cert, DWORD id)
{
    for (cr_prop **p = &cert->props; *p; p = &(*p)->next)
        if ((*p)->id == id) {
            cr_prop *dead = *p;
            *p = dead->next;
            CrFree(dead);
            return;
        }
}

static BOOL store_prop(cr_cert *cert, DWORD id, const void *data, DWORD size)
{
    cr_prop *p = CrAlloc(sizeof(cr_prop) + size);
    if (!p)
        return CrFail(ERROR_OUTOFMEMORY);
    p->id = id;
    p->size = size;
    if (size)
        memcpy(p->data, data, size);
    CrLock();
    remove_prop(cert, id);
    cr_prop **tail = &cert->props;
    while (*tail)
        tail = &(*tail)->next;
    p->next = NULL;
    *tail = p;
    CrUnlock();
    return TRUE;
}

/* copies a key provider info and everything it points to into one block; returns its size. */
static DWORD copy_key_prov_info(const CRYPT_KEY_PROV_INFO *src, BYTE *dst)
{
    DWORD size = sizeof(CRYPT_KEY_PROV_INFO);
    CRYPT_KEY_PROV_INFO *out = (CRYPT_KEY_PROV_INFO *)dst;
    if (out) {
        *out = *src;
        out->rgProvParam = NULL;
        out->pwszContainerName = out->pwszProvName = NULL;
    }
    if (src->cProvParam) {
        size = (size + 7) & ~7;
        if (out)
            out->rgProvParam = (PCRYPT_KEY_PROV_PARAM)(dst + size);
        size += src->cProvParam * sizeof(CRYPT_KEY_PROV_PARAM);
        for (DWORD i = 0; i < src->cProvParam; i++) {
            if (out) {
                out->rgProvParam[i] = src->rgProvParam[i];
                out->rgProvParam[i].pbData = src->rgProvParam[i].cbData ? dst + size : NULL;
                if (src->rgProvParam[i].cbData)
                    memcpy(dst + size, src->rgProvParam[i].pbData, src->rgProvParam[i].cbData);
            }
            size += src->rgProvParam[i].cbData;
        }
    }
    size = (size + 1) & ~1;
    if (src->pwszContainerName) {
        DWORD n = ((DWORD)wcslen(src->pwszContainerName) + 1) * sizeof(WCHAR);
        if (out) {
            out->pwszContainerName = (LPWSTR)(dst + size);
            memcpy(dst + size, src->pwszContainerName, n);
        }
        size += n;
    }
    if (src->pwszProvName) {
        DWORD n = ((DWORD)wcslen(src->pwszProvName) + 1) * sizeof(WCHAR);
        if (out) {
            out->pwszProvName = (LPWSTR)(dst + size);
            memcpy(dst + size, src->pwszProvName, n);
        }
        size += n;
    }
    return size;
}

static BOOL return_data(const void *data, DWORD size, void *out, DWORD *out_size)
{
    if (!out) {
        *out_size = size;
        return TRUE;
    }
    if (*out_size < size) {
        *out_size = size;
        return CrFail(ERROR_MORE_DATA);
    }
    memcpy(out, data, size);
    *out_size = size;
    return TRUE;
}

/* computes a hash property; returns FALSE when the id is not one. */
BOOL CrCertHash(PCCERT_CONTEXT cert, DWORD prop, BYTE *out, DWORD *size)
{
    const CERT_INFO *info = cert->pCertInfo;
    switch (prop) {
    case CERT_SHA1_HASH_PROP_ID:
        CrHash(CALG_SHA1, cert->pbCertEncoded, cert->cbCertEncoded, out);
        *size = 20;
        return TRUE;
    case CERT_MD5_HASH_PROP_ID:
        CrHash(CALG_MD5, cert->pbCertEncoded, cert->cbCertEncoded, out);
        *size = 16;
        return TRUE;
    case CERT_SHA256_HASH_PROP_ID:
        CrHash(CALG_SHA_256, cert->pbCertEncoded, cert->cbCertEncoded, out);
        *size = 32;
        return TRUE;
    case CERT_SIGNATURE_HASH_PROP_ID: {
        der_reader r = {cert->pbCertEncoded, cert->cbCertEncoded};
        der_item outer, tbs;
        if (DerRead(&r, &outer))
            return FALSE;
        DerOpen(&r, &outer);
        if (DerRead(&r, &tbs))
            return FALSE;
        CrHash(CALG_SHA1, tbs.start, tbs.total, out);
        *size = 20;
        return TRUE;
    }
    case CERT_KEY_IDENTIFIER_PROP_ID: {
        PCERT_EXTENSION ext = CertFindExtension(szOID_SUBJECT_KEY_IDENTIFIER, info->cExtension, info->rgExtension);
        if (ext) {
            der_reader r = {ext->Value.pbData, ext->Value.cbData};
            der_item id;
            if (!DerExpect(&r, ASN_OCTETSTRING, &id) && id.length <= CR_MAX_HASH) {
                memcpy(out, id.content, id.length);
                *size = id.length;
                return TRUE;
            }
        }
        CrHash(CALG_SHA1, info->SubjectPublicKeyInfo.PublicKey.pbData, info->SubjectPublicKeyInfo.PublicKey.cbData,
               out);
        *size = 20;
        return TRUE;
    }
    case CERT_SUBJECT_PUBLIC_KEY_MD5_HASH_PROP_ID:
        CrHash(CALG_MD5, info->SubjectPublicKeyInfo.PublicKey.pbData, info->SubjectPublicKeyInfo.PublicKey.cbData,
               out);
        *size = 16;
        return TRUE;
    case CERT_SUBJECT_NAME_MD5_HASH_PROP_ID:
        CrHash(CALG_MD5, info->Subject.pbData, info->Subject.cbData, out);
        *size = 16;
        return TRUE;
    case CERT_ISSUER_SERIAL_NUMBER_MD5_HASH_PROP_ID: {
        DWORD n = info->Issuer.cbData + info->SerialNumber.cbData;
        BYTE *data = CrAlloc(n ? n : 1);
        if (!data)
            return FALSE;
        memcpy(data, info->Issuer.pbData, info->Issuer.cbData);
        memcpy(data + info->Issuer.cbData, info->SerialNumber.pbData, info->SerialNumber.cbData);
        CrHash(CALG_MD5, data, n, out);
        CrFree(data);
        *size = 16;
        return TRUE;
    }
    }
    return FALSE;
}

BOOL WINAPI CertGetCertificateContextProperty(PCCERT_CONTEXT pCertContext, DWORD dwPropId, void *pvData,
                                              DWORD *pcbData)
{
    if (!pCertContext || !pcbData)
        return CrFail(E_INVALIDARG);
    cr_cert *cert = CrCert(pCertContext);
    CrLock();
    cr_prop *p = find_prop(cert, dwPropId);
    if (p) {
        BOOL ok;
        if (dwPropId == CERT_KEY_PROV_INFO_PROP_ID) {
            const CRYPT_KEY_PROV_INFO *info = (const CRYPT_KEY_PROV_INFO *)p->data;
            DWORD size = copy_key_prov_info(info, NULL);
            if (!pvData) {
                *pcbData = size;
                ok = TRUE;
            } else if (*pcbData < size) {
                *pcbData = size;
                ok = CrFail(ERROR_MORE_DATA);
            } else {
                copy_key_prov_info(info, pvData);
                *pcbData = size;
                ok = TRUE;
            }
        } else {
            ok = return_data(p->data, p->size, pvData, pcbData);
        }
        CrUnlock();
        return ok;
    }
    CrUnlock();
    BYTE hash[CR_MAX_HASH];
    DWORD size;
    switch (dwPropId) {
    case CERT_ACCESS_STATE_PROP_ID: {
        DWORD state = 0;
        cr_store *store = cert->store;
        if (store && store->system) {
            state |= CERT_ACCESS_STATE_SYSTEM_STORE_FLAG;
            if (!(store->flags & CERT_STORE_READONLY_FLAG))
                state |= CERT_ACCESS_STATE_WRITE_PERSIST_FLAG;
        }
        return return_data(&state, sizeof(state), pvData, pcbData);
    }
    case CERT_KEY_SPEC_PROP_ID: {
        DWORD spec = 0;
        CrLock();
        cr_prop *prov = find_prop(cert, CERT_KEY_PROV_INFO_PROP_ID);
        cr_prop *key = find_prop(cert, CERT_KEY_CONTEXT_PROP_ID);
        if (prov)
            spec = ((const CRYPT_KEY_PROV_INFO *)prov->data)->dwKeySpec;
        else if (key)
            spec = ((const CERT_KEY_CONTEXT *)key->data)->dwKeySpec;
        CrUnlock();
        if (!prov && !key)
            return CrFail(CRYPT_E_NOT_FOUND);
        return return_data(&spec, sizeof(spec), pvData, pcbData);
    }
    }
    if (!CrCertHash(pCertContext, dwPropId, hash, &size))
        return CrFail(CRYPT_E_NOT_FOUND);
    /* windows keeps the hashes it computes as properties. */
    if (dwPropId == CERT_SHA1_HASH_PROP_ID || dwPropId == CERT_MD5_HASH_PROP_ID ||
        dwPropId == CERT_KEY_IDENTIFIER_PROP_ID || dwPropId == CERT_SIGNATURE_HASH_PROP_ID)
        store_prop(cert, dwPropId, hash, size);
    return return_data(hash, size, pvData, pcbData);
}

BOOL WINAPI CertSetCertificateContextProperty(PCCERT_CONTEXT pCertContext, DWORD dwPropId, DWORD dwFlags,
                                              const void *pvData)
{
    if (!pCertContext || !dwPropId || dwPropId == CERT_ACCESS_STATE_PROP_ID ||
        (dwPropId >= CERT_CERT_PROP_ID && dwPropId <= CERT_CTL_PROP_ID))
        return CrFail(E_INVALIDARG);
    cr_cert *cert = CrCert(pCertContext);
    if (!pvData) {
        CrLock();
        remove_prop(cert, dwPropId);
        CrUnlock();
        return TRUE;
    }
    switch (dwPropId) {
    case CERT_KEY_PROV_INFO_PROP_ID: {
        const CRYPT_KEY_PROV_INFO *info = pvData;
        DWORD size = copy_key_prov_info(info, NULL);
        /* the copy points into itself, so it is built at its final address. */
        cr_prop *p = CrAlloc(sizeof(cr_prop) + size);
        if (!p)
            return CrFail(ERROR_OUTOFMEMORY);
        copy_key_prov_info(info, p->data);
        p->id = dwPropId;
        p->size = size;
        CrLock();
        remove_prop(cert, dwPropId);
        cr_prop **tail = &cert->props;
        while (*tail)
            tail = &(*tail)->next;
        p->next = NULL;
        *tail = p;
        CrUnlock();
        return TRUE;
    }
    case CERT_KEY_CONTEXT_PROP_ID: {
        const CERT_KEY_CONTEXT *key = pvData;
        if (key->cbSize != sizeof(CERT_KEY_CONTEXT))
            return CrFail(E_INVALIDARG);
        return store_prop(cert, dwPropId, key, sizeof(*key));
    }
    case CERT_KEY_PROV_HANDLE_PROP_ID:
        return store_prop(cert, dwPropId, pvData, sizeof(HCRYPTPROV));
    case CERT_KEY_SPEC_PROP_ID:
        return store_prop(cert, dwPropId, pvData, sizeof(DWORD));
    case CERT_DATE_STAMP_PROP_ID:
        return store_prop(cert, dwPropId, pvData, sizeof(FILETIME));
    default: {
        const CRYPT_DATA_BLOB *blob = pvData;
        return store_prop(cert, dwPropId, blob->pbData, blob->cbData);
    }
    }
}

DWORD WINAPI CertEnumCertificateContextProperties(PCCERT_CONTEXT pCertContext, DWORD dwPropId)
{
    if (!pCertContext)
        return 0;
    cr_cert *cert = CrCert(pCertContext);
    DWORD result = 0;
    CrLock();
    cr_prop *p = cert->props;
    if (dwPropId) {
        while (p && p->id != dwPropId)
            p = p->next;
        p = p ? p->next : NULL;
    }
    if (p)
        result = p->id;
    CrUnlock();
    if (!result)
        SetLastError(CRYPT_E_NOT_FOUND);
    return result;
}

/* copies the properties of src into dst, keeping the ones dst already has when asked. */
BOOL CrCopyProps(cr_cert *dst, const cr_cert *src, BOOL keep_existing)
{
    BOOL ok = TRUE;
    CrLock();
    for (const cr_prop *p = src->props; p && ok; p = p->next) {
        if (keep_existing && find_prop(dst, p->id))
            continue;
        if (p->id == CERT_KEY_PROV_INFO_PROP_ID) {
            CrUnlock();
            ok = CertSetCertificateContextProperty(&dst->context, p->id, 0, p->data);
            CrLock();
        } else {
            CrUnlock();
            ok = store_prop(dst, p->id, p->data, p->size);
            CrLock();
        }
    }
    CrUnlock();
    return ok;
}

BOOL CrHasPrivateKey(PCCERT_CONTEXT cert)
{
    cr_cert *c = CrCert(cert);
    CrLock();
    BOOL has = find_prop(c, CERT_KEY_PROV_INFO_PROP_ID) || find_prop(c, CERT_KEY_CONTEXT_PROP_ID) ||
               find_prop(c, CERT_KEY_PROV_HANDLE_PROP_ID);
    CrUnlock();
    return has;
}

BOOL CrSameCert(PCCERT_CONTEXT a, PCCERT_CONTEXT b)
{
    return a->cbCertEncoded == b->cbCertEncoded && !memcmp(a->pbCertEncoded, b->pbCertEncoded, a->cbCertEncoded);
}

/* ------------------------------------------------------------------ comparisons */

BOOL WINAPI CertCompareIntegerBlob(PCRYPT_INTEGER_BLOB pInt1, PCRYPT_INTEGER_BLOB pInt2)
{
    DWORD n1 = pInt1->cbData, n2 = pInt2->cbData;
    const BYTE *a = pInt1->pbData, *b = pInt2->pbData;
    /* drops the sign extension bytes at the most significant, little endian, end. */
    while (n1 > 1 && ((a[n1 - 1] == 0 && !(a[n1 - 2] & 0x80)) || (a[n1 - 1] == 0xFF && (a[n1 - 2] & 0x80))))
        n1--;
    while (n2 > 1 && ((b[n2 - 1] == 0 && !(b[n2 - 2] & 0x80)) || (b[n2 - 1] == 0xFF && (b[n2 - 2] & 0x80))))
        n2--;
    if (n1 != n2)
        return FALSE;
    return !n1 || !memcmp(a, b, n1);
}

BOOL WINAPI CertCompareCertificateName(DWORD dwCertEncodingType, PCERT_NAME_BLOB pCertName1,
                                       PCERT_NAME_BLOB pCertName2)
{
    if (pCertName1->cbData != pCertName2->cbData)
        return FALSE;
    return !pCertName1->cbData || !memcmp(pCertName1->pbData, pCertName2->pbData, pCertName1->cbData);
}

BOOL WINAPI CertCompareCertificate(DWORD dwCertEncodingType, PCERT_INFO pCertId1, PCERT_INFO pCertId2)
{
    return CertCompareIntegerBlob(&pCertId1->SerialNumber, &pCertId2->SerialNumber) &&
           CertCompareCertificateName(dwCertEncodingType, &pCertId1->Issuer, &pCertId2->Issuer);
}

BOOL WINAPI CertComparePublicKeyInfo(DWORD dwCertEncodingType, PCERT_PUBLIC_KEY_INFO pPublicKey1,
                                     PCERT_PUBLIC_KEY_INFO pPublicKey2)
{
    const CRYPT_BIT_BLOB *a = &pPublicKey1->PublicKey, *b = &pPublicKey2->PublicKey;
    if (a->cbData != b->cbData || a->cUnusedBits != b->cUnusedBits)
        return FALSE;
    return !a->cbData || !memcmp(a->pbData, b->pbData, a->cbData);
}

PCERT_EXTENSION WINAPI CertFindExtension(LPCSTR pszObjId, DWORD cExtensions, CERT_EXTENSION rgExtensions[])
{
    for (DWORD i = 0; i < cExtensions; i++)
        if (!strcmp(rgExtensions[i].pszObjId, pszObjId))
            return &rgExtensions[i];
    return NULL;
}

PCERT_RDN_ATTR WINAPI CertFindRDNAttr(LPCSTR pszObjId, PCERT_NAME_INFO pName)
{
    if (!pszObjId || !pName) {
        SetLastError(E_INVALIDARG);
        return NULL;
    }
    for (DWORD i = 0; i < pName->cRDN; i++)
        for (DWORD j = 0; j < pName->rgRDN[i].cRDNAttr; j++)
            if (!strcmp(pName->rgRDN[i].rgRDNAttr[j].pszObjId, pszObjId))
                return &pName->rgRDN[i].rgRDNAttr[j];
    return NULL;
}

LONG WINAPI CertVerifyTimeValidity(LPFILETIME pTimeToVerify, PCERT_INFO pCertInfo)
{
    FILETIME now;
    if (!pTimeToVerify) {
        GetSystemTimeAsFileTime(&now);
        pTimeToVerify = &now;
    }
    if (CompareFileTime(pTimeToVerify, &pCertInfo->NotBefore) < 0)
        return -1;
    if (CompareFileTime(pTimeToVerify, &pCertInfo->NotAfter) > 0)
        return 1;
    return 0;
}

DWORD WINAPI CertGetPublicKeyLength(DWORD dwCertEncodingType, PCERT_PUBLIC_KEY_INFO pPublicKey)
{
    if (!pPublicKey) {
        SetLastError(E_INVALIDARG);
        return 0;
    }
    DWORD bits = CrRsaModulusBits(pPublicKey);
    if (bits)
        return bits;
    if (pPublicKey->Algorithm.pszObjId && !strcmp(pPublicKey->Algorithm.pszObjId, szOID_ECC_PUBLIC_KEY)) {
        /* an uncompressed point: 04, x, y. */
        DWORD n = pPublicKey->PublicKey.cbData;
        if (n > 1 && pPublicKey->PublicKey.pbData[0] == 4)
            return (n - 1) / 2 * 8 == 528 ? 521 : (n - 1) / 2 * 8;
    }
    SetLastError(NTE_BAD_ALGID);
    return 0;
}

BOOL WINAPI CertGetIntendedKeyUsage(DWORD dwCertEncodingType, PCERT_INFO pCertInfo, BYTE *pbKeyUsage,
                                    DWORD cbKeyUsage)
{
    if (!pCertInfo || !pbKeyUsage)
        return CrFail(E_INVALIDARG);
    memset(pbKeyUsage, 0, cbKeyUsage);
    PCERT_EXTENSION ext = CertFindExtension(szOID_KEY_USAGE, pCertInfo->cExtension, pCertInfo->rgExtension);
    if (!ext) {
        SetLastError(0);
        return FALSE;
    }
    DWORD size;
    CRYPT_BIT_BLOB *bits = CrDecodeAlloc(X509_KEY_USAGE, ext->Value.pbData, ext->Value.cbData, 0, &size);
    if (!bits)
        return FALSE;
    memcpy(pbKeyUsage, bits->pbData, bits->cbData < cbKeyUsage ? bits->cbData : cbKeyUsage);
    CrFree(bits);
    return TRUE;
}

static void add_usages(const CERT_ENHKEY_USAGE *src, LPSTR *list, DWORD *count, BYTE *strings, DWORD *string_size,
                       BOOL write)
{
    for (DWORD i = 0; i < src->cUsageIdentifier; i++) {
        DWORD n = (DWORD)strlen(src->rgpszUsageIdentifier[i]) + 1;
        if (write) {
            list[*count] = (LPSTR)(strings + *string_size);
            memcpy(strings + *string_size, src->rgpszUsageIdentifier[i], n);
        }
        (*count)++;
        *string_size += n;
    }
}

BOOL WINAPI CertGetEnhancedKeyUsage(PCCERT_CONTEXT pCertContext, DWORD dwFlags, PCERT_ENHKEY_USAGE pUsage,
                                    DWORD *pcbUsage)
{
    if (!pCertContext || !pcbUsage)
        return CrFail(E_INVALIDARG);
    const DWORD ext_only = 0x2, prop_only = 0x4;
    CERT_ENHKEY_USAGE *from_ext = NULL, *from_prop = NULL;
    DWORD size;
    if (!(dwFlags & prop_only)) {
        PCERT_EXTENSION ext = CertFindExtension(szOID_ENHANCED_KEY_USAGE, pCertContext->pCertInfo->cExtension,
                                                pCertContext->pCertInfo->rgExtension);
        if (ext)
            from_ext = CrDecodeAlloc(X509_ENHANCED_KEY_USAGE, ext->Value.pbData, ext->Value.cbData, 0, &size);
    }
    if (!(dwFlags & ext_only)) {
        DWORD n = 0;
        if (CertGetCertificateContextProperty(pCertContext, CERT_ENHKEY_USAGE_PROP_ID, NULL, &n)) {
            BYTE *data = CrAlloc(n);
            if (data && CertGetCertificateContextProperty(pCertContext, CERT_ENHKEY_USAGE_PROP_ID, data, &n))
                from_prop = CrDecodeAlloc(X509_ENHANCED_KEY_USAGE, data, n, 0, &size);
            CrFree(data);
        }
    }
    if (!from_ext && !from_prop) {
        CrFree(from_ext);
        size = sizeof(CERT_ENHKEY_USAGE);
        if (pUsage) {
            if (*pcbUsage < size) {
                *pcbUsage = size;
                return CrFail(ERROR_MORE_DATA);
            }
            pUsage->cUsageIdentifier = 0;
            pUsage->rgpszUsageIdentifier = NULL;
        }
        *pcbUsage = size;
        SetLastError(CRYPT_E_NOT_FOUND);
        return TRUE;
    }
    /* with both present windows reports the usages of the extension that the property also lists. */
    const CERT_ENHKEY_USAGE *use = from_ext ? from_ext : from_prop;
    DWORD count = 0, strings = 0;
    add_usages(use, NULL, &count, NULL, &strings, FALSE);
    size = sizeof(CERT_ENHKEY_USAGE) + count * sizeof(LPSTR) + strings;
    BOOL ok = TRUE;
    if (pUsage) {
        if (*pcbUsage < size) {
            ok = CrFail(ERROR_MORE_DATA);
        } else {
            LPSTR *list = (LPSTR *)(pUsage + 1);
            BYTE *text = (BYTE *)(list + count);
            DWORD c = 0, s = 0;
            add_usages(use, list, &c, text, &s, TRUE);
            pUsage->cUsageIdentifier = count;
            pUsage->rgpszUsageIdentifier = count ? list : NULL;
        }
    }
    *pcbUsage = size;
    CrFree(from_ext);
    CrFree(from_prop);
    return ok;
}
