#include "cryptnet_private.h"

typedef struct {
    LPCWSTR *urls;
    DWORD count;
    DWORD capacity;
    DWORD chars;
    DWORD *groups;
    DWORD group_count;
    BOOL failed;
} url_list;

static void add_url(url_list *list, LPCWSTR url)
{
    if (!url || !*url)
        return;
    for (DWORD i = 0; i < list->count; i++)
        if (!_wcsicmp(list->urls[i], url))
            return;
    if (list->count == list->capacity) {
        DWORD capacity = list->capacity ? list->capacity * 2 : 8;
        LPCWSTR *grown = CnAlloc(capacity * sizeof(LPCWSTR));
        if (!grown) {
            list->failed = TRUE;
            return;
        }
        if (list->urls)
            memcpy(grown, list->urls, list->count * sizeof(LPCWSTR));
        CnFree(list->urls);
        list->urls = grown;
        list->capacity = capacity;
    }
    list->urls[list->count++] = url;
    list->chars += (DWORD)wcslen(url) + 1;
}

/* closes the group of the urls added since the last one. */
static void end_group(url_list *list, DWORD *first)
{
    if (list->count == *first)
        return;
    DWORD *grown = CnAlloc((list->group_count + 1) * sizeof(DWORD));
    if (!grown) {
        list->failed = TRUE;
        return;
    }
    if (list->groups)
        memcpy(grown, list->groups, list->group_count * sizeof(DWORD));
    CnFree(list->groups);
    list->groups = grown;
    list->groups[list->group_count++] = list->count - *first;
    *first = list->count;
}

static void add_names(url_list *list, const CERT_ALT_NAME_INFO *names)
{
    for (DWORD i = 0; i < names->cAltEntry; i++)
        if (names->rgAltEntry[i].dwAltNameChoice == CERT_ALT_NAME_URL)
            add_url(list, names->rgAltEntry[i].pwszURL);
}

/* the full name urls of a distribution points extension; the decoded structure stays alive in *decoded. */
static void add_dist_points(url_list *list, const CERT_EXTENSION *ext, void **decoded)
{
    if (!ext)
        return;
    CRL_DIST_POINTS_INFO *info = nullptr;
    DWORD size;
    if (!CryptDecodeObjectEx(X509_ASN_ENCODING, X509_CRL_DIST_POINTS, ext->Value.pbData, ext->Value.cbData,
                             CRYPT_DECODE_ALLOC_FLAG, nullptr, &info, &size))
        return;
    *decoded = info;
    DWORD first = list->count;
    for (DWORD i = 0; i < info->cDistPoint; i++) {
        const CRL_DIST_POINT_NAME *name = &info->rgDistPoint[i].DistPointName;
        if (name->dwDistPointNameChoice == CRL_DIST_POINT_FULL_NAME)
            add_names(list, &name->FullName);
        end_group(list, &first);
    }
}

static void add_access(url_list *list, const CERT_EXTENSION *ext, LPCSTR method, void **decoded)
{
    if (!ext)
        return;
    CERT_AUTHORITY_INFO_ACCESS *info = nullptr;
    DWORD size;
    if (!CryptDecodeObjectEx(X509_ASN_ENCODING, X509_AUTHORITY_INFO_ACCESS, ext->Value.pbData, ext->Value.cbData,
                             CRYPT_DECODE_ALLOC_FLAG, nullptr, &info, &size))
        return;
    *decoded = info;
    DWORD first = list->count;
    for (DWORD i = 0; i < info->cAccDescr; i++)
        if (!strcmp(info->rgAccDescr[i].pszAccessMethod, method) &&
            info->rgAccDescr[i].AccessLocation.dwAltNameChoice == CERT_ALT_NAME_URL)
            add_url(list, info->rgAccDescr[i].AccessLocation.pwszURL);
    end_group(list, &first);
}

/* the urls in one block: the array, its pointers, then the strings. */
static PCRYPT_URL_ARRAY pack(const url_list *list, DWORD *size)
{
    *size = sizeof(CRYPT_URL_ARRAY) + list->count * sizeof(LPWSTR) + list->chars * sizeof(WCHAR);
    CRYPT_URL_ARRAY *array = CnAlloc(*size);
    if (!array)
        return nullptr;
    array->cUrl = list->count;
    array->rgwszUrl = (LPWSTR *)(array + 1);
    WCHAR *text = (WCHAR *)(array->rgwszUrl + list->count);
    for (DWORD i = 0; i < list->count; i++) {
        SIZE_T n = wcslen(list->urls[i]) + 1;
        memcpy(text, list->urls[i], n * sizeof(WCHAR));
        array->rgwszUrl[i] = text;
        text += n;
    }
    return array;
}

static PCRYPT_URL_ARRAY collect(const CERT_EXTENSION *cdp, const CERT_EXTENSION *aia, LPCSTR method, BOOL aia_first)
{
    url_list list = {};
    void *a = nullptr, *b = nullptr;
    if (aia_first)
        add_access(&list, aia, method, &a);
    add_dist_points(&list, cdp, &b);
    if (!aia_first)
        add_access(&list, aia, method, &a);
    DWORD size;
    PCRYPT_URL_ARRAY array = list.count && !list.failed ? pack(&list, &size) : nullptr;
    LocalFree(a);
    LocalFree(b);
    CnFree(list.urls);
    CnFree(list.groups);
    return array;
}

PCRYPT_URL_ARRAY CnCrlUrls(PCCERT_CONTEXT cert)
{
    const CERT_INFO *info = cert->pCertInfo;
    return collect(CertFindExtension(szOID_CRL_DIST_POINTS, info->cExtension, info->rgExtension), nullptr, nullptr,
                   FALSE);
}

static BOOL copy_out(const void *data, DWORD size, void *out, DWORD *out_size)
{
    BOOL ok = TRUE;
    if (out) {
        if (*out_size < size)
            ok = CnFail(ERROR_MORE_DATA);
        else
            memcpy(out, data, size);
    }
    *out_size = size;
    return ok;
}

/*
 * the urls an object names for a purpose: its distribution points for lists, the access methods of its information
 * access for issuers and ocsp. a certificate's own property takes the place of the extension when asked.
 */
WINCRYPTNETAPI BOOL WINAPI CryptGetObjectUrl(LPCSTR pszUrlOid, LPVOID pvPara, DWORD dwFlags, PCRYPT_URL_ARRAY pUrlArray,
                                             DWORD *pcbUrlArray, PCRYPT_URL_INFO pUrlInfo, DWORD *pcbUrlInfo,
                                             LPVOID pvReserved)
{
    if (!pvPara || !IS_INTRESOURCE(pszUrlOid) || (pUrlArray && !pcbUrlArray) || (pUrlInfo && !pcbUrlInfo))
        return CnFail(E_INVALIDARG);
    if (!(dwFlags & (CRYPT_GET_URL_FROM_PROPERTY | CRYPT_GET_URL_FROM_EXTENSION |
                     CRYPT_GET_URL_FROM_UNAUTH_ATTRIBUTE | CRYPT_GET_URL_FROM_AUTH_ATTRIBUTE)))
        dwFlags |= CRYPT_GET_URL_FROM_PROPERTY | CRYPT_GET_URL_FROM_EXTENSION;
    const CERT_EXTENSION *cdp = nullptr, *aia = nullptr;
    LPCSTR method = szOID_PKIX_CA_ISSUERS;
    BOOL aia_first = FALSE;
    const CERT_EXTENSION *extensions;
    DWORD count;
    ULONG_PTR oid = (ULONG_PTR)pszUrlOid;
    if (oid == (ULONG_PTR)URL_OID_CRL_ISSUER || oid == (ULONG_PTR)URL_OID_CRL_FRESHEST_CRL) {
        PCCRL_CONTEXT crl = pvPara;
        extensions = crl->pCrlInfo->rgExtension;
        count = crl->pCrlInfo->cExtension;
    } else {
        PCCERT_CONTEXT cert = pvPara;
        extensions = cert->pCertInfo->rgExtension;
        count = cert->pCertInfo->cExtension;
    }
    PCERT_EXTENSION found_cdp = CertFindExtension(szOID_CRL_DIST_POINTS, count, (PCERT_EXTENSION)extensions);
    PCERT_EXTENSION found_aia = CertFindExtension(szOID_AUTHORITY_INFO_ACCESS, count, (PCERT_EXTENSION)extensions);
    PCERT_EXTENSION found_fresh = CertFindExtension(szOID_FRESHEST_CRL, count, (PCERT_EXTENSION)extensions);
    switch (oid) {
    case (ULONG_PTR)URL_OID_CERTIFICATE_ISSUER:
    case (ULONG_PTR)URL_OID_CRL_ISSUER:
        aia = found_aia;
        break;
    case (ULONG_PTR)URL_OID_CERTIFICATE_CRL_DIST_POINT:
        cdp = found_cdp;
        break;
    case (ULONG_PTR)URL_OID_CERTIFICATE_FRESHEST_CRL:
    case (ULONG_PTR)URL_OID_CRL_FRESHEST_CRL:
        cdp = found_fresh;
        break;
    case (ULONG_PTR)URL_OID_CERTIFICATE_OCSP:
    case (ULONG_PTR)URL_OID_CERTIFICATE_ONLY_OCSP:
        aia = found_aia;
        method = szOID_PKIX_OCSP;
        break;
    case (ULONG_PTR)URL_OID_CERTIFICATE_OCSP_AND_CRL_DIST_POINT:
        aia = found_aia;
        cdp = found_cdp;
        method = szOID_PKIX_OCSP;
        aia_first = TRUE;
        break;
    case (ULONG_PTR)URL_OID_CERTIFICATE_CRL_DIST_POINT_AND_OCSP:
        aia = found_aia;
        cdp = found_cdp;
        method = szOID_PKIX_OCSP;
        break;
    default:
        return CnFail(ERROR_FILE_NOT_FOUND);
    }
    if (!(dwFlags & CRYPT_GET_URL_FROM_EXTENSION))
        cdp = aia = nullptr;
    url_list list = {};
    void *a = nullptr, *b = nullptr;
    if (aia_first)
        add_access(&list, aia, method, &a);
    add_dist_points(&list, cdp, &b);
    if (!aia_first)
        add_access(&list, aia, method, &a);
    BOOL ok = FALSE;
    if (list.failed) {
        SetLastError(ERROR_OUTOFMEMORY);
    } else if (!list.count) {
        SetLastError(CRYPT_E_NOT_FOUND);
    } else {
        DWORD size;
        PCRYPT_URL_ARRAY packed = pack(&list, &size);
        if (!packed) {
            SetLastError(ERROR_OUTOFMEMORY);
        } else {
            ok = TRUE;
            if (pcbUrlArray) {
                /* the pointers move with the block. */
                if (pUrlArray && *pcbUrlArray >= size) {
                    memcpy(pUrlArray, packed, size);
                    pUrlArray->rgwszUrl = (LPWSTR *)(pUrlArray + 1);
                    for (DWORD i = 0; i < packed->cUrl; i++)
                        pUrlArray->rgwszUrl[i] = (LPWSTR)((BYTE *)pUrlArray + ((BYTE *)packed->rgwszUrl[i] - (BYTE *)packed));
                    *pcbUrlArray = size;
                } else {
                    ok = copy_out(nullptr, size, pUrlArray, pcbUrlArray);
                }
            }
            CnFree(packed);
            if (ok && pcbUrlInfo) {
                DWORD info_size = sizeof(CRYPT_URL_INFO) + list.group_count * sizeof(DWORD);
                if (pUrlInfo) {
                    if (*pcbUrlInfo < info_size) {
                        ok = CnFail(ERROR_MORE_DATA);
                    } else {
                        memset(pUrlInfo, 0, info_size);
                        pUrlInfo->cbSize = sizeof(CRYPT_URL_INFO);
                        pUrlInfo->cGroup = list.group_count;
                        pUrlInfo->rgcGroupEntry = list.group_count ? (DWORD *)(pUrlInfo + 1) : nullptr;
                        if (list.group_count)
                            memcpy(pUrlInfo + 1, list.groups, list.group_count * sizeof(DWORD));
                    }
                }
                *pcbUrlInfo = info_size;
            }
        }
    }
    LocalFree(a);
    LocalFree(b);
    CnFree(list.urls);
    CnFree(list.groups);
    return ok;
}
