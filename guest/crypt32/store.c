/*
 * certificate stores: memory, system, collection, file, pkcs #7 and
 * serialized stores, and the functions that add, find and enumerate their
 * certificates.
 *
 * system stores are empty memory stores: nothing reads the certificates of
 * the registry yet, and what a program adds is not persisted.
 */
#include "crypt32_private.h"

#define SERIALIZED_MAGIC 0x54524543

cr_store *CrStore(HCERTSTORE handle)
{
    cr_store *store = handle;
    return store && store->magic == CR_STORE_MAGIC ? store : nullptr;
}

static cr_store *new_store(DWORD flags)
{
    cr_store *store = CrAllocZero(sizeof(cr_store));
    if (!store) {
        SetLastError(ERROR_OUTOFMEMORY);
        return nullptr;
    }
    store->magic = CR_STORE_MAGIC;
    store->refs = 1;
    store->opens = 1;
    store->flags = flags;
    return store;
}

void CrReleaseStore(cr_store *store)
{
    if (InterlockedDecrement(&store->refs))
        return;
    for (DWORD i = 0; i < store->child_count; i++)
        CertCloseStore(store->children[i], 0);
    CrFree(store->children);
    CrFree(store->child_flags);
    CrFree(store->child_priorities);
    CrFree(store->localized_name);
    store->magic = 0;
    CrFree(store);
}

HCERTSTORE CrOpenMemoryStore(DWORD flags)
{
    return new_store(flags);
}

/* links a new context into the store, which keeps one reference. */
static void link_cert(cr_store *store, cr_cert *cert)
{
    InterlockedIncrement(&store->refs);
    cert->store = store;
    cert->context.hCertStore = store;
    CrLock();
    cert->next = nullptr;
    cert->prev = store->tail;
    if (store->tail)
        store->tail->next = cert;
    else
        store->head = cert;
    store->tail = cert;
    CrUnlock();
}

/* removes the store's own reference; the context lives on while the program holds it. */
static void unlink_cert(cr_cert *cert)
{
    CrLock();
    BOOL release = !cert->deleted;
    cert->deleted = TRUE;
    CrUnlock();
    if (release)
        CrReleaseCert(cert);
}

static cr_cert *find_same(cr_store *store, PCCERT_CONTEXT cert)
{
    cr_cert *found = nullptr;
    CrLock();
    for (cr_cert *c = store->head; c; c = c->next)
        if (!c->deleted && CrSameCert(&c->context, cert)) {
            found = c;
            InterlockedIncrement(&c->refs);
            break;
        }
    CrUnlock();
    return found;
}

static cr_store *add_target(cr_store *store)
{
    if (!store->collection)
        return store;
    for (DWORD i = 0; i < store->child_count; i++)
        if (store->child_flags[i] & CERT_PHYSICAL_STORE_ADD_ENABLE_FLAG)
            return add_target(store->children[i]);
    return nullptr;
}

/* adds a copy of a certificate following the disposition rules of CertAddCertificateContextToStore. */
static BOOL add_cert(cr_store *store, PCCERT_CONTEXT source, DWORD disposition, PCCERT_CONTEXT *result)
{
    if (result)
        *result = nullptr;
    if (disposition < CERT_STORE_ADD_NEW || disposition > CERT_STORE_ADD_NEWER_INHERIT_PROPERTIES)
        return CrFail(E_INVALIDARG);
    cr_cert *existing = nullptr;
    if (store) {
        store = add_target(store);
        if (!store || (store->flags & CERT_STORE_READONLY_FLAG))
            return CrFail(E_ACCESSDENIED);
        if (disposition != CERT_STORE_ADD_ALWAYS)
            existing = find_same(store, source);
    }
    BOOL inherit = FALSE;
    if (existing) {
        switch (disposition) {
        case CERT_STORE_ADD_NEW:
            CrReleaseCert(existing);
            return CrFail(CRYPT_E_EXISTS);
        case CERT_STORE_ADD_USE_EXISTING:
            CrCopyProps(existing, CrCert(source), TRUE);
            if (result)
                *result = &existing->context;
            else
                CrReleaseCert(existing);
            return TRUE;
        case CERT_STORE_ADD_NEWER:
        case CERT_STORE_ADD_NEWER_INHERIT_PROPERTIES:
            if (CompareFileTime(&existing->context.pCertInfo->NotBefore, &source->pCertInfo->NotBefore) >= 0) {
                CrReleaseCert(existing);
                return CrFail(CRYPT_E_EXISTS);
            }
            inherit = disposition == CERT_STORE_ADD_NEWER_INHERIT_PROPERTIES;
            break;
        case CERT_STORE_ADD_REPLACE_EXISTING_INHERIT_PROPERTIES:
            inherit = TRUE;
            break;
        }
    }
    cr_cert *copy = CrCreateCert(source->pbCertEncoded, source->cbCertEncoded);
    if (!copy) {
        if (existing)
            CrReleaseCert(existing);
        return FALSE;
    }
    copy->context.dwCertEncodingType = source->dwCertEncodingType;
    CrCopyProps(copy, CrCert(source), FALSE);
    if (existing) {
        if (inherit)
            CrCopyProps(copy, existing, TRUE);
        unlink_cert(existing);
        CrReleaseCert(existing);
    }
    if (store)
        link_cert(store, copy);
    if (result) {
        if (store)
            InterlockedIncrement(&copy->refs);
        *result = &copy->context;
    } else if (!store) {
        CrReleaseCert(copy);
    }
    return TRUE;
}

BOOL WINAPI CertAddCertificateContextToStore(HCERTSTORE hCertStore, PCCERT_CONTEXT pCertContext,
                                             DWORD dwAddDisposition, PCCERT_CONTEXT *ppStoreContext)
{
    if (!pCertContext || (hCertStore && !CrStore(hCertStore)))
        return CrFail(E_INVALIDARG);
    return add_cert(CrStore(hCertStore), pCertContext, dwAddDisposition, ppStoreContext);
}

BOOL WINAPI CertAddCertificateLinkToStore(HCERTSTORE hCertStore, PCCERT_CONTEXT pCertContext, DWORD dwAddDisposition,
                                          PCCERT_CONTEXT *ppStoreContext)
{
    return CertAddCertificateContextToStore(hCertStore, pCertContext, dwAddDisposition, ppStoreContext);
}

BOOL WINAPI CertAddEncodedCertificateToStore(HCERTSTORE hCertStore, DWORD dwCertEncodingType,
                                             const BYTE *pbCertEncoded, DWORD cbCertEncoded, DWORD dwAddDisposition,
                                             PCCERT_CONTEXT *ppCertContext)
{
    if (ppCertContext)
        *ppCertContext = nullptr;
    PCCERT_CONTEXT cert = CertCreateCertificateContext(dwCertEncodingType, pbCertEncoded, cbCertEncoded);
    if (!cert)
        return FALSE;
    BOOL ok = CertAddCertificateContextToStore(hCertStore, cert, dwAddDisposition, ppCertContext);
    CertFreeCertificateContext(cert);
    return ok;
}

BOOL CrAddEncoded(HCERTSTORE store, const BYTE *data, DWORD size)
{
    return CertAddEncodedCertificateToStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, data, size,
                                            CERT_STORE_ADD_ALWAYS, nullptr);
}

BOOL WINAPI CertDeleteCertificateFromStore(PCCERT_CONTEXT pCertContext)
{
    if (!pCertContext)
        return CrFail(E_INVALIDARG);
    cr_cert *cert = CrCert(pCertContext);
    BOOL ok = TRUE;
    if (cert->store) {
        if (cert->store->flags & CERT_STORE_READONLY_FLAG)
            ok = CrFail(E_ACCESSDENIED);
        else
            unlink_cert(cert);
    }
    CrReleaseCert(cert);
    return ok;
}

/* ------------------------------------------------------------------ enumeration */

/* the next live certificate of a plain store after prev, referenced. */
static cr_cert *next_in(cr_store *store, cr_cert *prev)
{
    cr_cert *found = nullptr;
    CrLock();
    cr_cert *c = prev ? prev->next : store->head;
    for (; c; c = c->next)
        if (!c->deleted) {
            InterlockedIncrement(&c->refs);
            found = c;
            break;
        }
    CrUnlock();
    return found;
}

static BOOL store_contains(cr_store *store, cr_store *target)
{
    if (store == target)
        return TRUE;
    for (DWORD i = 0; store->collection && i < store->child_count; i++)
        if (store_contains(store->children[i], target))
            return TRUE;
    return FALSE;
}

/* the next certificate after prev, walking the members of collections in order. */
static cr_cert *enum_store(cr_store *store, cr_cert *prev)
{
    if (!store->collection) {
        if (prev && prev->store != store)
            return nullptr;
        return next_in(store, prev);
    }
    cr_cert *found = nullptr;
    CrLock();
    DWORD i = 0;
    if (prev) {
        while (i < store->child_count && !(prev->store && store_contains(store->children[i], prev->store)))
            i++;
        if (i < store->child_count)
            found = enum_store(store->children[i++], prev);
        else
            i = store->child_count;
    }
    for (; !found && i < store->child_count; i++)
        found = enum_store(store->children[i], nullptr);
    CrUnlock();
    return found;
}

PCCERT_CONTEXT WINAPI CertEnumCertificatesInStore(HCERTSTORE hCertStore, PCCERT_CONTEXT pPrevCertContext)
{
    cr_store *store = CrStore(hCertStore);
    if (!store) {
        if (pPrevCertContext)
            CertFreeCertificateContext(pPrevCertContext);
        SetLastError(E_INVALIDARG);
        return nullptr;
    }
    cr_cert *found = enum_store(store, pPrevCertContext ? CrCert(pPrevCertContext) : nullptr);
    if (pPrevCertContext)
        CertFreeCertificateContext(pPrevCertContext);
    if (!found) {
        SetLastError(CRYPT_E_NOT_FOUND);
        return nullptr;
    }
    return &found->context;
}

/* ------------------------------------------------------------------ finding */

static BOOL name_contains(const CERT_NAME_BLOB *name, LPCWSTR text)
{
    if (!text || !*text)
        return TRUE;
    DWORD size;
    CERT_NAME_INFO *info = CrDecodeAlloc(X509_UNICODE_NAME, name->pbData, name->cbData, 0, &size);
    if (!info)
        return FALSE;
    DWORD n = (DWORD)wcslen(text);
    BOOL found = FALSE;
    for (DWORD i = 0; i < info->cRDN && !found; i++)
        for (DWORD j = 0; j < info->rgRDN[i].cRDNAttr && !found; j++) {
            CERT_RDN_ATTR *attr = &info->rgRDN[i].rgRDNAttr[j];
            DWORD type = attr->dwValueType & CERT_RDN_TYPE_MASK;
            if (type == CERT_RDN_ENCODED_BLOB || type == CERT_RDN_OCTET_STRING)
                continue;
            const WCHAR *value = (const WCHAR *)attr->Value.pbData;
            DWORD length = attr->Value.cbData / sizeof(WCHAR);
            for (DWORD k = 0; k + n <= length && !found; k++)
                found = !_wcsnicmp(value + k, text, n);
        }
    CrFree(info);
    return found;
}

static BOOL attrs_match(const CERT_NAME_BLOB *name, const CERT_RDN *rdn, DWORD flags)
{
    DWORD size;
    BOOL unicode = flags & CERT_UNICODE_IS_RDN_ATTRS_FLAG;
    CERT_NAME_INFO *info = CrDecodeAlloc(unicode ? X509_UNICODE_NAME : X509_NAME, name->pbData, name->cbData, 0,
                                         &size);
    if (!info)
        return FALSE;
    BOOL all = TRUE;
    for (DWORD a = 0; a < rdn->cRDNAttr && all; a++) {
        const CERT_RDN_ATTR *want = &rdn->rgRDNAttr[a];
        BOOL found = FALSE;
        for (DWORD i = 0; i < info->cRDN && !found; i++)
            for (DWORD j = 0; j < info->rgRDN[i].cRDNAttr && !found; j++) {
                const CERT_RDN_ATTR *have = &info->rgRDN[i].rgRDNAttr[j];
                if (want->pszObjId && strcmp(want->pszObjId, have->pszObjId))
                    continue;
                if (want->dwValueType && (want->dwValueType & CERT_RDN_TYPE_MASK) !=
                                             (have->dwValueType & CERT_RDN_TYPE_MASK))
                    continue;
                DWORD n = want->Value.cbData;
                if (unicode && !n && want->Value.pbData)
                    n = (DWORD)wcslen((const WCHAR *)want->Value.pbData) * sizeof(WCHAR);
                if (n != have->Value.cbData)
                    continue;
                if (unicode && (flags & CERT_CASE_INSENSITIVE_IS_RDN_ATTRS_FLAG))
                    found = !_wcsnicmp((const WCHAR *)want->Value.pbData, (const WCHAR *)have->Value.pbData,
                                       n / sizeof(WCHAR));
                else
                    found = !n || !memcmp(want->Value.pbData, have->Value.pbData, n);
            }
        all = found;
    }
    CrFree(info);
    return all;
}

static BOOL hash_matches(PCCERT_CONTEXT cert, DWORD prop, const CRYPT_HASH_BLOB *want)
{
    BYTE hash[CR_MAX_HASH];
    DWORD size = sizeof(hash);
    if (!want || !CertGetCertificateContextProperty(cert, prop, hash, &size))
        return FALSE;
    return size == want->cbData && !memcmp(hash, want->pbData, size);
}

static BOOL usage_matches(PCCERT_CONTEXT cert, DWORD flags, const CERT_ENHKEY_USAGE *want)
{
    DWORD size = 0;
    if (!CertGetEnhancedKeyUsage(cert, flags & 0x6, nullptr, &size))
        return FALSE;
    CERT_ENHKEY_USAGE *have = CrAlloc(size);
    if (!have)
        return FALSE;
    BOOL ok = CertGetEnhancedKeyUsage(cert, flags & 0x6, have, &size);
    BOOL none = ok && GetLastError() == (DWORD)CRYPT_E_NOT_FOUND && !have->cUsageIdentifier;
    BOOL result;
    if (!ok)
        result = FALSE;
    else if (flags & CERT_FIND_NO_ENHKEY_USAGE_FLAG)
        result = none;
    else if (none)
        result = (flags & CERT_FIND_OPTIONAL_ENHKEY_USAGE_FLAG) != 0;
    else if (!want || !want->cUsageIdentifier)
        result = TRUE;
    else {
        DWORD matched = 0;
        for (DWORD i = 0; i < want->cUsageIdentifier; i++)
            for (DWORD j = 0; j < have->cUsageIdentifier; j++)
                if (!strcmp(want->rgpszUsageIdentifier[i], have->rgpszUsageIdentifier[j])) {
                    matched++;
                    break;
                }
        result = (flags & CERT_FIND_OR_ENHKEY_USAGE_FLAG) ? matched > 0 : matched == want->cUsageIdentifier;
    }
    CrFree(have);
    return result;
}

static BOOL cert_matches(PCCERT_CONTEXT cert, DWORD type, DWORD flags, const void *para)
{
    const CERT_INFO *info = cert->pCertInfo;
    DWORD compare = type >> CERT_COMPARE_SHIFT;
    const CERT_NAME_BLOB *name = (type & 0xF) == CERT_INFO_ISSUER_FLAG ? &info->Issuer : &info->Subject;
    switch (compare) {
    case CERT_COMPARE_ANY:
        return TRUE;
    case CERT_COMPARE_SHA1_HASH:
        return hash_matches(cert, CERT_SHA1_HASH_PROP_ID, para);
    case CERT_COMPARE_MD5_HASH:
        return hash_matches(cert, CERT_MD5_HASH_PROP_ID, para);
    case CERT_COMPARE_SIGNATURE_HASH:
        return hash_matches(cert, CERT_SIGNATURE_HASH_PROP_ID, para);
    case CERT_COMPARE_KEY_IDENTIFIER:
        return hash_matches(cert, CERT_KEY_IDENTIFIER_PROP_ID, para);
    case CERT_COMPARE_PUBKEY_MD5_HASH:
        return hash_matches(cert, CERT_SUBJECT_PUBLIC_KEY_MD5_HASH_PROP_ID, para);
    case CERT_COMPARE_HASH_STR: {
        BYTE want[20];
        DWORD size = sizeof(want);
        if (!para || !CryptStringToBinaryW(para, 0, CRYPT_STRING_HEX, want, &size, nullptr, nullptr) || size != 20)
            return FALSE;
        CRYPT_HASH_BLOB blob = {size, want};
        return hash_matches(cert, CERT_SHA1_HASH_PROP_ID, &blob);
    }
    case CERT_COMPARE_PROPERTY: {
        DWORD size = 0;
        return para && CertGetCertificateContextProperty(cert, *(const DWORD *)para, nullptr, &size);
    }
    case CERT_COMPARE_PUBLIC_KEY:
        return para && CertComparePublicKeyInfo(cert->dwCertEncodingType, (PCERT_PUBLIC_KEY_INFO)para,
                                                (PCERT_PUBLIC_KEY_INFO)&info->SubjectPublicKeyInfo);
    case CERT_COMPARE_NAME:
        return para && CertCompareCertificateName(cert->dwCertEncodingType, (PCERT_NAME_BLOB)name,
                                                  (PCERT_NAME_BLOB)para);
    case CERT_COMPARE_ATTR:
        return para && attrs_match(name, para, flags);
    case CERT_COMPARE_NAME_STR_W:
        return name_contains(name, para);
    case CERT_COMPARE_NAME_STR_A: {
        if (!para)
            return FALSE;
        int n = MultiByteToWideChar(CP_ACP, 0, para, -1, nullptr, 0);
        WCHAR *w = CrAlloc(n * sizeof(WCHAR));
        if (!w)
            return FALSE;
        MultiByteToWideChar(CP_ACP, 0, para, -1, w, n);
        BOOL found = name_contains(name, w);
        CrFree(w);
        return found;
    }
    case CERT_COMPARE_KEY_SPEC: {
        DWORD spec, size = sizeof(spec);
        return para && CertGetCertificateContextProperty(cert, CERT_KEY_SPEC_PROP_ID, &spec, &size) &&
               spec == *(const DWORD *)para;
    }
    case CERT_COMPARE_ENHKEY_USAGE:
        return usage_matches(cert, flags, para);
    case CERT_COMPARE_SUBJECT_CERT: {
        const CERT_INFO *want = para;
        return want && CertCompareCertificate(cert->dwCertEncodingType, (PCERT_INFO)want, (PCERT_INFO)info);
    }
    case CERT_COMPARE_ISSUER_OF: {
        PCCERT_CONTEXT subject = para;
        return subject &&
               CertCompareCertificateName(cert->dwCertEncodingType, (PCERT_NAME_BLOB)&subject->pCertInfo->Issuer,
                                          (PCERT_NAME_BLOB)&info->Subject);
    }
    case CERT_COMPARE_EXISTING:
        return para && CrSameCert(cert, para);
    case CERT_COMPARE_CERT_ID: {
        const CERT_ID *id = para;
        if (!id)
            return FALSE;
        switch (id->dwIdChoice) {
        case CERT_ID_ISSUER_SERIAL_NUMBER:
            return CertCompareIntegerBlob((PCRYPT_INTEGER_BLOB)&id->IssuerSerialNumber.SerialNumber,
                                          (PCRYPT_INTEGER_BLOB)&info->SerialNumber) &&
                   CertCompareCertificateName(cert->dwCertEncodingType,
                                              (PCERT_NAME_BLOB)&id->IssuerSerialNumber.Issuer,
                                              (PCERT_NAME_BLOB)&info->Issuer);
        case CERT_ID_KEY_IDENTIFIER:
            return hash_matches(cert, CERT_KEY_IDENTIFIER_PROP_ID, &id->KeyId);
        case CERT_ID_SHA1_HASH:
            return hash_matches(cert, CERT_SHA1_HASH_PROP_ID, &id->HashId);
        }
        return FALSE;
    }
    case CERT_COMPARE_HAS_PRIVATE_KEY:
        return CrHasPrivateKey(cert);
    }
    return FALSE;
}

static BOOL find_type_known(DWORD type)
{
    DWORD compare = type >> CERT_COMPARE_SHIFT;
    if (compare == CERT_COMPARE_NAME || compare == CERT_COMPARE_ATTR || compare == CERT_COMPARE_NAME_STR_A ||
        compare == CERT_COMPARE_NAME_STR_W)
        return (type & 0xFFFF) == CERT_INFO_ISSUER_FLAG || (type & 0xFFFF) == CERT_INFO_SUBJECT_FLAG;
    if (type & 0xFFFF)
        return FALSE;
    return compare <= CERT_COMPARE_HAS_PRIVATE_KEY && compare != CERT_COMPARE_CROSS_CERT_DIST_POINTS &&
           compare != CERT_COMPARE_SUBJECT_INFO_ACCESS;
}

PCCERT_CONTEXT WINAPI CertFindCertificateInStore(HCERTSTORE hCertStore, DWORD dwCertEncodingType, DWORD dwFindFlags,
                                                 DWORD dwFindType, const void *pvFindPara,
                                                 PCCERT_CONTEXT pPrevCertContext)
{
    if (!find_type_known(dwFindType)) {
        if (pPrevCertContext)
            CertFreeCertificateContext(pPrevCertContext);
        SetLastError(E_INVALIDARG);
        return nullptr;
    }
    PCCERT_CONTEXT cert = pPrevCertContext;
    while ((cert = CertEnumCertificatesInStore(hCertStore, cert)))
        if (cert_matches(cert, dwFindType, dwFindFlags, pvFindPara))
            return cert;
    SetLastError(CRYPT_E_NOT_FOUND);
    return nullptr;
}

PCCERT_CONTEXT WINAPI CertGetSubjectCertificateFromStore(HCERTSTORE hCertStore, DWORD dwCertEncodingType,
                                                         PCERT_INFO pCertId)
{
    if (!pCertId) {
        SetLastError(E_INVALIDARG);
        return nullptr;
    }
    return CertFindCertificateInStore(hCertStore, dwCertEncodingType, 0, CERT_FIND_SUBJECT_CERT, pCertId, nullptr);
}

#define CERT_STORE_SIGNATURE_FLAG 0x00000001
#define CERT_STORE_TIME_VALIDITY_FLAG 0x00000002
#define CERT_STORE_REVOCATION_FLAG 0x00000004
#define CERT_STORE_NO_CRL_FLAG 0x00010000
#define CERT_STORE_NO_ISSUER_FLAG 0x00020000
#define CRYPT_E_SELF_SIGNED ((HRESULT)0x80092007L)

PCCERT_CONTEXT WINAPI CertGetIssuerCertificateFromStore(HCERTSTORE hCertStore, PCCERT_CONTEXT pSubjectContext,
                                                        PCCERT_CONTEXT pPrevIssuerContext, DWORD *pdwFlags)
{
    if (!pSubjectContext || !pdwFlags || (*pdwFlags & ~(CERT_STORE_SIGNATURE_FLAG | CERT_STORE_TIME_VALIDITY_FLAG |
                                                        CERT_STORE_REVOCATION_FLAG))) {
        if (pPrevIssuerContext)
            CertFreeCertificateContext(pPrevIssuerContext);
        SetLastError(E_INVALIDARG);
        return nullptr;
    }
    if (CrIsSelfIssued(pSubjectContext)) {
        if (pPrevIssuerContext)
            CertFreeCertificateContext(pPrevIssuerContext);
        SetLastError(CRYPT_E_SELF_SIGNED);
        return nullptr;
    }
    PCCERT_CONTEXT issuer = CertFindCertificateInStore(hCertStore, pSubjectContext->dwCertEncodingType, 0,
                                                       CERT_FIND_ISSUER_OF, pSubjectContext, pPrevIssuerContext);
    if (!issuer)
        return nullptr;
    DWORD flags = *pdwFlags;
    if ((flags & CERT_STORE_SIGNATURE_FLAG) &&
        !CrVerifySignedBlob(pSubjectContext->pbCertEncoded, pSubjectContext->cbCertEncoded,
                            &issuer->pCertInfo->SubjectPublicKeyInfo))
        flags &= ~CERT_STORE_SIGNATURE_FLAG;
    if ((flags & CERT_STORE_TIME_VALIDITY_FLAG) && !CertVerifyTimeValidity(nullptr, pSubjectContext->pCertInfo))
        flags &= ~CERT_STORE_TIME_VALIDITY_FLAG;
    if (flags & CERT_STORE_REVOCATION_FLAG)
        flags |= CERT_STORE_NO_CRL_FLAG;
    *pdwFlags = flags;
    return issuer;
}

/* ------------------------------------------------------------------ serialized and pkcs #7 content */

static BOOL read_dword(const BYTE *p, DWORD *value)
{
    *value = p[0] | p[1] << 8 | p[2] << 16 | (DWORD)p[3] << 24;
    return TRUE;
}

typedef struct {
    DWORD id;
    DWORD size;
    const BYTE *data;
} pending_prop;

/* adds the elements of a serialized store, properties first, then the certificate they belong to. */
static DWORD add_elements(HCERTSTORE store, const BYTE *data, DWORD size, DWORD disposition, PCCERT_CONTEXT *last)
{
    pending_prop props[64];
    DWORD prop_count = 0, offset = 0;
    while (offset + 12 <= size) {
        DWORD id, encoding, length;
        read_dword(data + offset, &id);
        read_dword(data + offset + 4, &encoding);
        read_dword(data + offset + 8, &length);
        offset += 12;
        if (!id && !length)
            break;
        if (length > size - offset)
            return CRYPT_E_FILE_ERROR;
        if (id == CERT_CERT_PROP_ID) {
            PCCERT_CONTEXT cert = CertCreateCertificateContext(encoding ? encoding : X509_ASN_ENCODING,
                                                               data + offset, length);
            if (!cert)
                return GetLastError();
            for (DWORD i = 0; i < prop_count; i++) {
                CRYPT_DATA_BLOB blob = {props[i].size, (BYTE *)props[i].data};
                const void *value = &blob;
                if (props[i].id == CERT_KEY_PROV_INFO_PROP_ID)
                    continue;
                if (props[i].id == CERT_KEY_SPEC_PROP_ID || props[i].id == CERT_DATE_STAMP_PROP_ID)
                    value = props[i].data;
                if (props[i].id != CERT_KEY_CONTEXT_PROP_ID && props[i].id != CERT_KEY_PROV_HANDLE_PROP_ID)
                    CertSetCertificateContextProperty(cert, props[i].id, 0, value);
            }
            prop_count = 0;
            PCCERT_CONTEXT added = nullptr;
            BOOL ok = store ? CertAddCertificateContextToStore(store, cert, disposition, last ? &added : nullptr) : TRUE;
            if (last) {
                if (!store) {
                    added = CertDuplicateCertificateContext(cert);
                }
                if (*last)
                    CertFreeCertificateContext(*last);
                *last = added;
            }
            CertFreeCertificateContext(cert);
            if (!ok)
                return GetLastError();
        } else if (prop_count < RTL_NUMBER_OF(props)) {
            props[prop_count].id = id;
            props[prop_count].size = length;
            props[prop_count].data = data + offset;
            prop_count++;
        }
        offset += length;
    }
    return 0;
}

BOOL CrLoadSerialized(HCERTSTORE store, const BYTE *data, DWORD size)
{
    DWORD zero, magic;
    if (size < 8 || !read_dword(data, &zero) || !read_dword(data + 4, &magic) || zero || magic != SERIALIZED_MAGIC)
        return CrFail(CRYPT_E_FILE_ERROR);
    DWORD error = add_elements(store, data + 8, size - 8, CERT_STORE_ADD_ALWAYS, nullptr);
    return error ? CrFail(error) : TRUE;
}

BOOL CrLoadPkcs7(HCERTSTORE store, const BYTE *data, DWORD size)
{
    der_reader r = {data, size};
    der_item info, oid, wrapper, signed_data, item;
    char text[64];
    if (DerExpect(&r, ASN_SEQUENCE, &info))
        return CrFail(CRYPT_E_ASN1_BADTAG);
    DerOpen(&r, &info);
    if (DerExpect(&r, ASN_OID, &oid) || DerParseOid(&oid, text, sizeof(text)) || strcmp(text, szOID_RSA_signedData))
        return CrFail(CRYPT_E_INVALID_MSG_TYPE);
    if (DerExpect(&r, ASN_CONSTRUCTED_CONTEXT(0), &wrapper))
        return CrFail(CRYPT_E_ASN1_BADTAG);
    DerOpen(&r, &wrapper);
    if (DerExpect(&r, ASN_SEQUENCE, &signed_data))
        return CrFail(CRYPT_E_ASN1_BADTAG);
    DerOpen(&r, &signed_data);
    /* version, digest algorithms and the encapsulated content come before the certificates. */
    for (int i = 0; i < 3; i++)
        if (DerRead(&r, &item))
            return CrFail(CRYPT_E_ASN1_EOD);
    if (DerPeek(&r, ASN_CONSTRUCTED_CONTEXT(0))) {
        der_reader certs;
        DerRead(&r, &item);
        DerOpen(&certs, &item);
        while (certs.n) {
            der_item cert;
            if (DerRead(&certs, &cert))
                return CrFail(CRYPT_E_ASN1_EOD);
            if (cert.tag == ASN_SEQUENCE && !CrAddEncoded(store, cert.start, cert.total))
                return FALSE;
        }
    }
    return TRUE;
}

static BOOL load_any(HCERTSTORE store, const BYTE *data, DWORD size)
{
    if (size >= 8 && !data[0] && !data[1] && !data[2] && !data[3])
        return CrLoadSerialized(store, data, size);
    if (CrLoadPkcs7(store, data, size))
        return TRUE;
    if (CrAddEncoded(store, data, size))
        return TRUE;
    /* base64 text, with or without a pem header. */
    DWORD n = 0;
    if (!CryptStringToBinaryA((LPCSTR)data, size, CRYPT_STRING_BASE64_ANY, nullptr, &n, nullptr, nullptr))
        return CrFail(CRYPT_E_FILE_ERROR);
    BYTE *decoded = CrAlloc(n);
    if (!decoded)
        return CrFail(ERROR_OUTOFMEMORY);
    BOOL ok = CryptStringToBinaryA((LPCSTR)data, size, CRYPT_STRING_BASE64_ANY, decoded, &n, nullptr, nullptr) &&
              (CrLoadPkcs7(store, decoded, n) || CrAddEncoded(store, decoded, n));
    CrFree(decoded);
    return ok ? TRUE : CrFail(CRYPT_E_FILE_ERROR);
}

static BOOL load_file(HCERTSTORE store, HANDLE file)
{
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size))
        return FALSE;
    if (size.QuadPart > 0x10000000)
        return CrFail(CRYPT_E_FILE_ERROR);
    BYTE *data = CrAlloc(size.LowPart ? size.LowPart : 1);
    if (!data)
        return CrFail(ERROR_OUTOFMEMORY);
    DWORD read = 0;
    BOOL ok = ReadFile(file, data, size.LowPart, &read, nullptr);
    if (ok)
        ok = read ? load_any(store, data, read) : TRUE;
    CrFree(data);
    return ok;
}

/* ------------------------------------------------------------------ opening and closing */

HCERTSTORE WINAPI CertOpenStore(LPCSTR lpszStoreProvider, DWORD dwEncodingType, HCRYPTPROV_LEGACY hCryptProv,
                                DWORD dwFlags, const void *pvPara)
{
    ULONG_PTR provider = 0;
    if (IS_INTRESOURCE(lpszStoreProvider)) {
        provider = (ULONG_PTR)lpszStoreProvider;
    } else {
        static const struct {
            LPCSTR name;
            ULONG_PTR id;
        } names[] = {{"Memory", 2},      {"File", 3},     {"PKCS7", 5},         {"Serialized", 6},
                     {"System", 10},     {"Collection", 11}, {"SystemRegistry", 13}, {"Physical", 14}};
        for (DWORD i = 0; i < RTL_NUMBER_OF(names); i++)
            if (!_stricmp(names[i].name, lpszStoreProvider))
                provider = names[i].id;
    }
    if (dwFlags & CERT_STORE_DELETE_FLAG) {
        SetLastError(provider == 9 || provider == 10 || provider == 12 || provider == 13 || provider == 14
                         ? ERROR_SUCCESS
                         : ERROR_CALL_NOT_IMPLEMENTED);
        return nullptr;
    }
    cr_store *store = nullptr;
    switch (provider) {
    case 2:
        store = new_store(dwFlags);
        break;
    case 9:
    case 10:
    case 12:
    case 13:
    case 14: {
        if (!pvPara) {
            SetLastError(E_INVALIDARG);
            return nullptr;
        }
        DWORD location = (dwFlags & CERT_SYSTEM_STORE_LOCATION_MASK) >> CERT_SYSTEM_STORE_LOCATION_SHIFT;
        if (location > 9 || location == 3) {
            SetLastError(E_INVALIDARG);
            return nullptr;
        }
        store = new_store(dwFlags & ~CERT_SYSTEM_STORE_LOCATION_MASK);
        if (store)
            store->system = TRUE;
        break;
    }
    case 11:
        store = new_store(dwFlags);
        if (store)
            store->collection = TRUE;
        break;
    case 5:
    case 6: {
        const CRYPT_DATA_BLOB *blob = pvPara;
        if (!blob) {
            SetLastError(E_INVALIDARG);
            return nullptr;
        }
        store = new_store(dwFlags & ~CERT_STORE_READONLY_FLAG);
        if (store && !(provider == 5 ? CrLoadPkcs7(store, blob->pbData, blob->cbData)
                                     : CrLoadSerialized(store, blob->pbData, blob->cbData))) {
            DWORD error = GetLastError();
            CertCloseStore(store, 0);
            SetLastError(error);
            return nullptr;
        }
        break;
    }
    case 3: {
        if (!pvPara || pvPara == INVALID_HANDLE_VALUE) {
            SetLastError(ERROR_INVALID_HANDLE);
            return nullptr;
        }
        store = new_store(dwFlags & ~CERT_STORE_READONLY_FLAG);
        if (store && !load_file(store, (HANDLE)pvPara)) {
            DWORD error = GetLastError();
            CertCloseStore(store, 0);
            SetLastError(error);
            return nullptr;
        }
        break;
    }
    case 7:
    case 8: {
        if (!pvPara) {
            SetLastError(E_INVALIDARG);
            return nullptr;
        }
        HANDLE file;
        if (provider == 7)
            file = CreateFileA(pvPara, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        else
            file = CreateFileW(pvPara, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            if (dwFlags & CERT_STORE_OPEN_EXISTING_FLAG)
                return nullptr;
            return new_store(dwFlags & ~CERT_STORE_READONLY_FLAG);
        }
        store = new_store(dwFlags & ~CERT_STORE_READONLY_FLAG);
        BOOL ok = store && load_file(store, file);
        DWORD error = GetLastError();
        CloseHandle(file);
        if (store && !ok) {
            CertCloseStore(store, 0);
            SetLastError(error);
            return nullptr;
        }
        if (store)
            store->flags |= dwFlags & CERT_STORE_READONLY_FLAG;
        break;
    }
    default:
        SetLastError(ERROR_FILE_NOT_FOUND);
        return nullptr;
    }
    if (store && provider >= 5 && provider != 11)
        store->flags |= dwFlags & CERT_STORE_READONLY_FLAG;
    return store;
}

HCERTSTORE WINAPI CertOpenSystemStoreW(HCRYPTPROV_LEGACY hProv, LPCWSTR szSubsystemProtocol)
{
    if (!szSubsystemProtocol) {
        SetLastError(E_INVALIDARG);
        return nullptr;
    }
    return CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, hProv,
                         CERT_STORE_NO_CRYPT_RELEASE_FLAG | CERT_SYSTEM_STORE_CURRENT_USER, szSubsystemProtocol);
}

HCERTSTORE WINAPI CertOpenSystemStoreA(HCRYPTPROV_LEGACY hProv, LPCSTR szSubsystemProtocol)
{
    if (!szSubsystemProtocol) {
        SetLastError(E_INVALIDARG);
        return nullptr;
    }
    return CertOpenStore(CERT_STORE_PROV_SYSTEM_A, 0, hProv,
                         CERT_STORE_NO_CRYPT_RELEASE_FLAG | CERT_SYSTEM_STORE_CURRENT_USER, szSubsystemProtocol);
}

HCERTSTORE WINAPI CertDuplicateStore(HCERTSTORE hCertStore)
{
    cr_store *store = CrStore(hCertStore);
    if (!store) {
        SetLastError(E_INVALIDARG);
        return nullptr;
    }
    InterlockedIncrement(&store->opens);
    InterlockedIncrement(&store->refs);
    return store;
}

BOOL WINAPI CertCloseStore(HCERTSTORE hCertStore, DWORD dwFlags)
{
    if (!hCertStore)
        return TRUE;
    cr_store *store = CrStore(hCertStore);
    if (!store)
        return CrFail(E_INVALIDARG);
    BOOL pending = FALSE;
    if (!InterlockedDecrement(&store->opens)) {
        /* releases the store's own references; contexts the program still holds keep it alive. */
        for (;;) {
            cr_cert *victim = nullptr;
            CrLock();
            for (cr_cert *c = store->head; c; c = c->next)
                if (!c->deleted) {
                    victim = c;
                    break;
                }
            if (victim) {
                victim->deleted = TRUE;
                if (victim->refs > 1)
                    pending = TRUE;
            }
            CrUnlock();
            if (!victim)
                break;
            CrReleaseCert(victim);
        }
    } else if (dwFlags & CERT_CLOSE_STORE_CHECK_FLAG) {
        pending = TRUE;
    }
    CrReleaseStore(store);
    if (pending && (dwFlags & CERT_CLOSE_STORE_CHECK_FLAG))
        return CrFail(CRYPT_E_PENDING_CLOSE);
    return TRUE;
}

BOOL WINAPI CertAddStoreToCollection(HCERTSTORE hCollectionStore, HCERTSTORE hSiblingStore, DWORD dwUpdateFlags,
                                     DWORD dwPriority)
{
    cr_store *collection = CrStore(hCollectionStore), *sibling = CrStore(hSiblingStore);
    if (!collection || !sibling || !collection->collection)
        return CrFail(E_INVALIDARG);
    DWORD count = collection->child_count;
    cr_store **children = CrAlloc((count + 1) * sizeof(cr_store *));
    DWORD *flags = CrAlloc((count + 1) * sizeof(DWORD)), *priorities = CrAlloc((count + 1) * sizeof(DWORD));
    if (!children || !flags || !priorities) {
        CrFree(children);
        CrFree(flags);
        CrFree(priorities);
        return CrFail(ERROR_OUTOFMEMORY);
    }
    CertDuplicateStore(sibling);
    CrLock();
    /* members are kept by decreasing priority, a new one after those of the same priority. */
    DWORD at = 0;
    while (at < count && collection->child_priorities[at] >= dwPriority)
        at++;
    for (DWORD i = 0, j = 0; i <= count; i++) {
        if (i == at) {
            children[i] = sibling;
            flags[i] = dwUpdateFlags;
            priorities[i] = dwPriority;
        } else {
            children[i] = collection->children[j];
            flags[i] = collection->child_flags[j];
            priorities[i] = collection->child_priorities[j];
            j++;
        }
    }
    CrFree(collection->children);
    CrFree(collection->child_flags);
    CrFree(collection->child_priorities);
    collection->children = children;
    collection->child_flags = flags;
    collection->child_priorities = priorities;
    collection->child_count = count + 1;
    CrUnlock();
    return TRUE;
}

void WINAPI CertRemoveStoreFromCollection(HCERTSTORE hCollectionStore, HCERTSTORE hSiblingStore)
{
    cr_store *collection = CrStore(hCollectionStore);
    if (!collection)
        return;
    cr_store *removed = nullptr;
    CrLock();
    for (DWORD i = 0; i < collection->child_count; i++)
        if (collection->children[i] == hSiblingStore) {
            removed = collection->children[i];
            DWORD rest = collection->child_count - i - 1;
            memmove(&collection->children[i], &collection->children[i + 1], rest * sizeof(cr_store *));
            memmove(&collection->child_flags[i], &collection->child_flags[i + 1], rest * sizeof(DWORD));
            memmove(&collection->child_priorities[i], &collection->child_priorities[i + 1], rest * sizeof(DWORD));
            collection->child_count--;
            break;
        }
    CrUnlock();
    if (removed)
        CertCloseStore(removed, 0);
}

#define CERT_STORE_CTRL_RESYNC 1
#define CERT_STORE_CTRL_NOTIFY_CHANGE 2
#define CERT_STORE_CTRL_COMMIT 3
#define CERT_STORE_CTRL_AUTO_RESYNC 4
#define CERT_STORE_CTRL_CANCEL_NOTIFY 5

BOOL WINAPI CertControlStore(HCERTSTORE hCertStore, DWORD dwFlags, DWORD dwCtrlType, void const *pvCtrlPara)
{
    if (!CrStore(hCertStore))
        return CrFail(E_INVALIDARG);
    switch (dwCtrlType) {
    case CERT_STORE_CTRL_RESYNC:
    case CERT_STORE_CTRL_COMMIT:
    case CERT_STORE_CTRL_AUTO_RESYNC:
        return TRUE;
    }
    return CrFail(ERROR_CALL_NOT_IMPLEMENTED);
}

BOOL WINAPI CertGetStoreProperty(HCERTSTORE hCertStore, DWORD dwPropId, void *pvData, DWORD *pcbData)
{
    cr_store *store = CrStore(hCertStore);
    if (!store || !pcbData)
        return CrFail(E_INVALIDARG);
    if (dwPropId != CERT_STORE_LOCALIZED_NAME_PROP_ID || !store->localized_name)
        return CrFail(CRYPT_E_NOT_FOUND);
    DWORD size = ((DWORD)wcslen(store->localized_name) + 1) * sizeof(WCHAR);
    if (!pvData) {
        *pcbData = size;
        return TRUE;
    }
    if (*pcbData < size) {
        *pcbData = size;
        return CrFail(ERROR_MORE_DATA);
    }
    memcpy(pvData, store->localized_name, size);
    *pcbData = size;
    return TRUE;
}

BOOL WINAPI CertSetStoreProperty(HCERTSTORE hCertStore, DWORD dwPropId, DWORD dwFlags, const void *pvData)
{
    cr_store *store = CrStore(hCertStore);
    if (!store || dwPropId != CERT_STORE_LOCALIZED_NAME_PROP_ID)
        return CrFail(E_INVALIDARG);
    WCHAR *copy = nullptr;
    const CRYPT_DATA_BLOB *blob = pvData;
    if (blob) {
        copy = CrAllocZero(blob->cbData + sizeof(WCHAR));
        if (!copy)
            return CrFail(ERROR_OUTOFMEMORY);
        memcpy(copy, blob->pbData, blob->cbData);
    }
    CrLock();
    CrFree(store->localized_name);
    store->localized_name = copy;
    CrUnlock();
    return TRUE;
}

/* ------------------------------------------------------------------ saving */

static void put_dword(der_writer *w, DWORD v)
{
    BYTE b[4] = {(BYTE)v, (BYTE)(v >> 8), (BYTE)(v >> 16), (BYTE)(v >> 24)};
    DerWriteBytes(w, b, 4);
}

static void serialize_cert(der_writer *w, PCCERT_CONTEXT cert)
{
    for (DWORD id = CertEnumCertificateContextProperties(cert, 0); id;
         id = CertEnumCertificateContextProperties(cert, id)) {
        if (id == CERT_KEY_CONTEXT_PROP_ID || id == CERT_KEY_PROV_HANDLE_PROP_ID || id == CERT_KEY_PROV_INFO_PROP_ID)
            continue;
        DWORD size = 0;
        if (!CertGetCertificateContextProperty(cert, id, nullptr, &size))
            continue;
        BYTE *data = CrAlloc(size ? size : 1);
        if (!data)
            continue;
        if (CertGetCertificateContextProperty(cert, id, data, &size)) {
            put_dword(w, id);
            put_dword(w, 1);
            put_dword(w, size);
            DerWriteBytes(w, data, size);
        }
        CrFree(data);
    }
    put_dword(w, CERT_CERT_PROP_ID);
    put_dword(w, cert->dwCertEncodingType);
    put_dword(w, cert->cbCertEncoded);
    DerWriteBytes(w, cert->pbCertEncoded, cert->cbCertEncoded);
}

BOOL WINAPI CertSerializeCertificateStoreElement(PCCERT_CONTEXT pCertContext, DWORD dwFlags, BYTE *pbElement,
                                                 DWORD *pcbElement)
{
    if (!pCertContext || !pcbElement)
        return CrFail(E_INVALIDARG);
    der_writer w;
    DerWriteInit(&w);
    serialize_cert(&w, pCertContext);
    if (w.error) {
        DerWriteFree(&w);
        return CrFail(ERROR_OUTOFMEMORY);
    }
    BOOL ok = TRUE;
    if (pbElement) {
        if (*pcbElement < w.size)
            ok = CrFail(ERROR_MORE_DATA);
        else
            memcpy(pbElement, w.data, w.size);
    }
    *pcbElement = w.size;
    DerWriteFree(&w);
    return ok;
}

#define CERT_STORE_ALL_CONTEXT_FLAG (~0U)
#define CERT_STORE_CERTIFICATE_CONTEXT_FLAG (1 << CERT_STORE_CERTIFICATE_CONTEXT)

BOOL WINAPI CertAddSerializedElementToStore(HCERTSTORE hCertStore, const BYTE *pbElement, DWORD cbElement,
                                            DWORD dwAddDisposition, DWORD dwFlags, DWORD dwContextTypeFlags,
                                            DWORD *pdwContextType, const void **ppvContext)
{
    if (!pbElement || !cbElement)
        return CrFail(E_INVALIDARG);
    if (!(dwContextTypeFlags & CERT_STORE_CERTIFICATE_CONTEXT_FLAG))
        return CrFail(E_INVALIDARG);
    PCCERT_CONTEXT last = nullptr;
    DWORD error = add_elements(hCertStore, pbElement, cbElement, dwAddDisposition, &last);
    if (error)
        return CrFail(error);
    if (!last)
        return CrFail(E_INVALIDARG);
    if (pdwContextType)
        *pdwContextType = CERT_STORE_CERTIFICATE_CONTEXT;
    if (ppvContext)
        *ppvContext = last;
    else
        CertFreeCertificateContext(last);
    return TRUE;
}

static void write_pkcs7(der_writer *w, HCERTSTORE store)
{
    DWORD info = DerBegin(w, ASN_SEQUENCE);
    DerWriteOid(w, szOID_RSA_signedData);
    DWORD wrapper = DerBegin(w, ASN_CONSTRUCTED_CONTEXT(0));
    DWORD signed_data = DerBegin(w, ASN_SEQUENCE);
    DerWriteInt(w, 1);
    DerWriteTlv(w, ASN_SET, nullptr, 0);
    DWORD content = DerBegin(w, ASN_SEQUENCE);
    DerWriteOid(w, szOID_RSA_data);
    DerEnd(w, content);
    DWORD certs = DerBegin(w, ASN_CONSTRUCTED_CONTEXT(0));
    PCCERT_CONTEXT cert = nullptr;
    while ((cert = CertEnumCertificatesInStore(store, cert)))
        DerWriteBytes(w, cert->pbCertEncoded, cert->cbCertEncoded);
    DerEnd(w, certs);
    DerWriteTlv(w, ASN_SET, nullptr, 0);
    DerEnd(w, signed_data);
    DerEnd(w, wrapper);
    DerEnd(w, info);
}

BOOL WINAPI CertSaveStore(HCERTSTORE hCertStore, DWORD dwEncodingType, DWORD dwSaveAs, DWORD dwSaveTo,
                          void *pvSaveToPara, DWORD dwFlags)
{
    if (!CrStore(hCertStore) || !pvSaveToPara)
        return CrFail(E_INVALIDARG);
    der_writer w;
    DerWriteInit(&w);
    if (dwSaveAs == CERT_STORE_SAVE_AS_STORE) {
        put_dword(&w, 0);
        put_dword(&w, SERIALIZED_MAGIC);
        PCCERT_CONTEXT cert = nullptr;
        while ((cert = CertEnumCertificatesInStore(hCertStore, cert)))
            serialize_cert(&w, cert);
        put_dword(&w, 0);
        put_dword(&w, 0);
        put_dword(&w, 0);
    } else if (dwSaveAs == CERT_STORE_SAVE_AS_PKCS7) {
        write_pkcs7(&w, hCertStore);
    } else {
        return CrFail(E_INVALIDARG);
    }
    if (w.error) {
        DWORD error = w.error;
        DerWriteFree(&w);
        return CrFail(error);
    }
    BOOL ok = TRUE;
    switch (dwSaveTo) {
    case CERT_STORE_SAVE_TO_MEMORY: {
        CRYPT_DATA_BLOB *blob = pvSaveToPara;
        if (blob->pbData) {
            if (blob->cbData < w.size)
                ok = CrFail(ERROR_MORE_DATA);
            else
                memcpy(blob->pbData, w.data, w.size);
        }
        blob->cbData = w.size;
        break;
    }
    case CERT_STORE_SAVE_TO_FILE: {
        DWORD written;
        ok = WriteFile((HANDLE)pvSaveToPara, w.data, w.size, &written, nullptr);
        break;
    }
    case CERT_STORE_SAVE_TO_FILENAME_A:
    case CERT_STORE_SAVE_TO_FILENAME_W: {
        HANDLE file = dwSaveTo == CERT_STORE_SAVE_TO_FILENAME_A
                          ? CreateFileA(pvSaveToPara, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr)
                          : CreateFileW(pvSaveToPara, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            ok = FALSE;
            break;
        }
        DWORD written;
        ok = WriteFile(file, w.data, w.size, &written, nullptr);
        CloseHandle(file);
        break;
    }
    default:
        ok = CrFail(E_INVALIDARG);
        break;
    }
    DerWriteFree(&w);
    return ok;
}
