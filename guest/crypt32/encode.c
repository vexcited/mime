/*
 * CryptEncodeObject and CryptEncodeObjectEx: the X.509 structures of wincrypt.h to DER.
 */
#include "crypt32_private.h"

typedef struct {
    der_writer w;
    DWORD flags;
    DWORD location;
} enc_ctx;

static void fail(enc_ctx *c, DWORD error)
{
    if (!c->w.error)
        c->w.error = error;
}

static void enc_bits(enc_ctx *c, BYTE tag, const CRYPT_BIT_BLOB *bits, BOOL trim)
{
    DWORD size = bits->cbData, unused = bits->cUnusedBits & 7;
    BYTE last = size ? bits->pbData[size - 1] : 0;
    if (trim) {
        while (size && !bits->pbData[size - 1])
            size--;
        unused = 0;
        if (size) {
            last = bits->pbData[size - 1];
            while (!(last & (1 << unused)))
                unused++;
        }
    }
    if (size && unused)
        last &= (BYTE)(0xFF << unused);
    DWORD mark = DerBegin(&c->w, tag);
    BYTE first = (BYTE)(size ? unused : 0);
    DerWriteBytes(&c->w, &first, 1);
    if (size > 1)
        DerWriteBytes(&c->w, bits->pbData, size - 1);
    if (size)
        DerWriteBytes(&c->w, &last, 1);
    DerEnd(&c->w, mark);
}

static void enc_algid(enc_ctx *c, const CRYPT_ALGORITHM_IDENTIFIER *alg)
{
    DWORD mark = DerBegin(&c->w, ASN_SEQUENCE);
    DerWriteOid(&c->w, alg->pszObjId);
    if (alg->Parameters.cbData)
        DerWriteBytes(&c->w, alg->Parameters.pbData, alg->Parameters.cbData);
    DerEnd(&c->w, mark);
}

static void enc_pubkey(enc_ctx *c, const CERT_PUBLIC_KEY_INFO *key)
{
    DWORD mark = DerBegin(&c->w, ASN_SEQUENCE);
    enc_algid(c, &key->Algorithm);
    enc_bits(c, ASN_BITSTRING, &key->PublicKey, FALSE);
    DerEnd(&c->w, mark);
}

static void enc_extensions(enc_ctx *c, DWORD count, const CERT_EXTENSION *ext)
{
    DWORD mark = DerBegin(&c->w, ASN_SEQUENCE);
    for (DWORD i = 0; i < count; i++) {
        DWORD inner = DerBegin(&c->w, ASN_SEQUENCE);
        DerWriteOid(&c->w, ext[i].pszObjId);
        if (ext[i].fCritical) {
            BYTE t = 0xFF;
            DerWriteTlv(&c->w, ASN_BOOLEAN, &t, 1);
        }
        DerWriteTlv(&c->w, ASN_OCTETSTRING, ext[i].Value.pbData, ext[i].Value.cbData);
        DerEnd(&c->w, inner);
    }
    DerEnd(&c->w, mark);
}

/* ------------------------------------------------------------------ strings */

static BOOL printable_char(WCHAR c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '\'' ||
           c == '(' || c == ')' || c == '+' || c == ',' || c == '-' || c == '.' || c == '/' || c == ':' ||
           c == '=' || c == '?';
}

BOOL CrIsPrintable(const WCHAR *s, DWORD n)
{
    for (DWORD i = 0; i < n; i++)
        if (!printable_char(s[i]))
            return FALSE;
    return TRUE;
}

/* returns the index of the first character the type cannot hold, or n. */
static DWORD check_chars(DWORD type, const WCHAR *s, DWORD n)
{
    for (DWORD i = 0; i < n; i++) {
        WCHAR ch = s[i];
        switch (type) {
        case CERT_RDN_NUMERIC_STRING:
            if (!(ch >= '0' && ch <= '9') && ch != ' ')
                return i;
            break;
        case CERT_RDN_PRINTABLE_STRING:
            if (!printable_char(ch))
                return i;
            break;
        case CERT_RDN_IA5_STRING:
        case CERT_RDN_VISIBLE_STRING:
            if (ch > 0x7F)
                return i;
            break;
        case CERT_RDN_TELETEX_STRING:
        case CERT_RDN_VIDEOTEX_STRING:
        case CERT_RDN_GRAPHIC_STRING:
        case CERT_RDN_GENERAL_STRING:
            if (ch > 0xFF)
                return i;
            break;
        }
    }
    return n;
}

static DWORD char_error(DWORD type)
{
    switch (type) {
    case CERT_RDN_NUMERIC_STRING:
        return CRYPT_E_INVALID_NUMERIC_STRING;
    case CERT_RDN_PRINTABLE_STRING:
        return CRYPT_E_INVALID_PRINTABLE_STRING;
    case CERT_RDN_IA5_STRING:
        return CRYPT_E_INVALID_IA5_STRING;
    }
    return CRYPT_E_NOT_CHAR_STRING;
}

static void enc_wide_value(enc_ctx *c, DWORD type, const WCHAR *s, DWORD n)
{
    BYTE tag = CrTagFromStringType(type);
    if (!tag || type == CERT_RDN_OCTET_STRING) {
        fail(c, CRYPT_E_ASN1_CHOICE);
        return;
    }
    if (!(c->flags & CERT_RDN_DISABLE_CHECK_TYPE_FLAG)) {
        DWORD bad = check_chars(type, s, n);
        if (bad < n) {
            c->location |= bad;
            fail(c, char_error(type));
            return;
        }
    }
    DWORD mark = DerBegin(&c->w, tag);
    switch (type) {
    case CERT_RDN_BMP_STRING:
        for (DWORD i = 0; i < n; i++) {
            BYTE b[2] = {(BYTE)(s[i] >> 8), (BYTE)s[i]};
            DerWriteBytes(&c->w, b, 2);
        }
        break;
    case CERT_RDN_UNIVERSAL_STRING:
        for (DWORD i = 0; i < n; i++) {
            DWORD ch = s[i];
            if (ch >= 0xD800 && ch < 0xDC00 && i + 1 < n && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000) {
                ch = 0x10000 + ((ch - 0xD800) << 10) + (s[i + 1] - 0xDC00);
                i++;
            }
            BYTE b[4] = {(BYTE)(ch >> 24), (BYTE)(ch >> 16), (BYTE)(ch >> 8), (BYTE)ch};
            DerWriteBytes(&c->w, b, 4);
        }
        break;
    case CERT_RDN_UTF8_STRING: {
        int size = n ? WideCharToMultiByte(CP_UTF8, 0, s, (int)n, nullptr, 0, nullptr, nullptr) : 0;
        if (size > 0) {
            BYTE *utf8 = CrAlloc(size);
            if (!utf8) {
                fail(c, ERROR_OUTOFMEMORY);
                break;
            }
            WideCharToMultiByte(CP_UTF8, 0, s, (int)n, (LPSTR)utf8, size, nullptr, nullptr);
            DerWriteBytes(&c->w, utf8, size);
            CrFree(utf8);
        }
        break;
    }
    default:
        for (DWORD i = 0; i < n; i++) {
            BYTE b = (BYTE)s[i];
            DerWriteBytes(&c->w, &b, 1);
        }
        break;
    }
    DerEnd(&c->w, mark);
}

/* picks the string type windows uses for an attribute whose type is left to the encoder. */
static DWORD choose_type(LPCSTR oid, DWORD flags, const WCHAR *s, DWORD n)
{
    PCCRYPT_OID_INFO info = oid ? CrFindOid(oid, CRYPT_RDN_ATTR_OID_GROUP_ID) : nullptr;
    if (info && info->ExtraInfo.cbData >= sizeof(DWORD)) {
        const DWORD *types = (const DWORD *)info->ExtraInfo.pbData;
        DWORD count = info->ExtraInfo.cbData / sizeof(DWORD);
        for (DWORD i = 0; i < count && types[i]; i++)
            if (types[i] == CERT_RDN_UTF8_STRING || check_chars(types[i], s, n) == n)
                return types[i];
        return types[0];
    }
    if (flags & CERT_RDN_FORCE_UTF8_UNICODE_FLAG)
        return CERT_RDN_UTF8_STRING;
    if (CrIsPrintable(s, n))
        return CERT_RDN_PRINTABLE_STRING;
    if ((flags & CERT_RDN_ENABLE_T61_UNICODE_FLAG) && check_chars(CERT_RDN_TELETEX_STRING, s, n) == n)
        return CERT_RDN_TELETEX_STRING;
    if (flags & CERT_RDN_ENABLE_UTF8_UNICODE_FLAG)
        return CERT_RDN_UTF8_STRING;
    return CERT_RDN_BMP_STRING;
}

static void enc_value(enc_ctx *c, LPCSTR oid, DWORD value_type, const CERT_RDN_VALUE_BLOB *value, BOOL unicode)
{
    DWORD type = value_type & CERT_RDN_TYPE_MASK;
    DWORD saved = c->flags;
    c->flags |= value_type & CERT_RDN_FLAGS_MASK;
    if (type == CERT_RDN_ENCODED_BLOB) {
        DerWriteBytes(&c->w, value->pbData, value->cbData);
    } else if (type == CERT_RDN_OCTET_STRING) {
        DerWriteTlv(&c->w, ASN_OCTETSTRING, value->pbData, value->cbData);
    } else if (unicode) {
        const WCHAR *s = (const WCHAR *)value->pbData;
        DWORD n = value->cbData ? value->cbData / sizeof(WCHAR) : (s ? (DWORD)wcslen(s) : 0);
        if (type == CERT_RDN_ANY_TYPE)
            type = choose_type(oid, c->flags, s, n);
        enc_wide_value(c, type, s, n);
    } else if (type == CERT_RDN_BMP_STRING || type == CERT_RDN_UTF8_STRING) {
        enc_wide_value(c, type, (const WCHAR *)value->pbData, value->cbData / sizeof(WCHAR));
    } else if (type == CERT_RDN_UNIVERSAL_STRING) {
        DWORD mark = DerBegin(&c->w, ASN_UNIVERSALSTRING);
        const DWORD *chars = (const DWORD *)value->pbData;
        for (DWORD i = 0; i < value->cbData / 4; i++) {
            BYTE b[4] = {(BYTE)(chars[i] >> 24), (BYTE)(chars[i] >> 16), (BYTE)(chars[i] >> 8), (BYTE)chars[i]};
            DerWriteBytes(&c->w, b, 4);
        }
        DerEnd(&c->w, mark);
    } else {
        BYTE tag = CrTagFromStringType(type);
        if (!tag)
            fail(c, CRYPT_E_ASN1_CHOICE);
        else
            DerWriteTlv(&c->w, tag, value->pbData, value->cbData);
    }
    c->flags = saved;
}

static int compare_der(const void *a, DWORD an, const void *b, DWORD bn)
{
    int r = memcmp(a, b, an < bn ? an : bn);
    return r ? r : (int)an - (int)bn;
}

static void enc_name(enc_ctx *c, const CERT_NAME_INFO *name, BOOL unicode)
{
    DWORD mark = DerBegin(&c->w, ASN_SEQUENCE);
    for (DWORD i = 0; i < name->cRDN && !c->w.error; i++) {
        const CERT_RDN *rdn = &name->rgRDN[i];
        DWORD set = DerBegin(&c->w, ASN_SET);
        DWORD start = c->w.size;
        DWORD *offsets = rdn->cRDNAttr > 1 ? CrAlloc(rdn->cRDNAttr * 2 * sizeof(DWORD)) : nullptr;
        for (DWORD j = 0; j < rdn->cRDNAttr && !c->w.error; j++) {
            const CERT_RDN_ATTR *attr = &rdn->rgRDNAttr[j];
            c->location = (i << 22) | (j << 16);
            if (offsets)
                offsets[j * 2] = c->w.size;
            DWORD seq = DerBegin(&c->w, ASN_SEQUENCE);
            DerWriteOid(&c->w, attr->pszObjId);
            enc_value(c, attr->pszObjId, attr->dwValueType, &attr->Value, unicode);
            DerEnd(&c->w, seq);
            if (offsets)
                offsets[j * 2 + 1] = c->w.size;
        }
        /* der orders the members of a set by their encoding. */
        if (offsets && !c->w.error) {
            DWORD total = c->w.size - start;
            BYTE *copy = CrAlloc(total);
            if (copy) {
                memcpy(copy, c->w.data + start, total);
                BYTE *out = c->w.data + start;
                for (DWORD k = 0; k < rdn->cRDNAttr; k++)
                    for (DWORD m = k + 1; m < rdn->cRDNAttr; m++) {
                        const BYTE *pk = copy + offsets[k * 2] - start, *pm = copy + offsets[m * 2] - start;
                        DWORD nk = offsets[k * 2 + 1] - offsets[k * 2], nm = offsets[m * 2 + 1] - offsets[m * 2];
                        if (compare_der(pm, nm, pk, nk) < 0) {
                            DWORD t0 = offsets[k * 2], t1 = offsets[k * 2 + 1];
                            offsets[k * 2] = offsets[m * 2];
                            offsets[k * 2 + 1] = offsets[m * 2 + 1];
                            offsets[m * 2] = t0;
                            offsets[m * 2 + 1] = t1;
                        }
                    }
                for (DWORD k = 0; k < rdn->cRDNAttr; k++) {
                    DWORD n = offsets[k * 2 + 1] - offsets[k * 2];
                    memcpy(out, copy + offsets[k * 2] - start, n);
                    out += n;
                }
                CrFree(copy);
            }
        }
        CrFree(offsets);
        DerEnd(&c->w, set);
    }
    DerEnd(&c->w, mark);
}

static void enc_ia5_wide(enc_ctx *c, BYTE tag, LPCWSTR s)
{
    DWORD mark = DerBegin(&c->w, tag);
    for (; s && *s; s++) {
        if (*s > 0x7F) {
            fail(c, CRYPT_E_INVALID_IA5_STRING);
            return;
        }
        BYTE b = (BYTE)*s;
        DerWriteBytes(&c->w, &b, 1);
    }
    DerEnd(&c->w, mark);
}

static void enc_alt_name(enc_ctx *c, BYTE tag, const CERT_ALT_NAME_INFO *info)
{
    DWORD mark = DerBegin(&c->w, tag);
    for (DWORD i = 0; i < info->cAltEntry && !c->w.error; i++) {
        const CERT_ALT_NAME_ENTRY *e = &info->rgAltEntry[i];
        switch (e->dwAltNameChoice) {
        case CERT_ALT_NAME_OTHER_NAME: {
            DWORD other = DerBegin(&c->w, ASN_CONSTRUCTED_CONTEXT(0));
            DerWriteOid(&c->w, e->pOtherName->pszObjId);
            DerWriteTlv(&c->w, ASN_CONSTRUCTED_CONTEXT(0), e->pOtherName->Value.pbData,
                        e->pOtherName->Value.cbData);
            DerEnd(&c->w, other);
            break;
        }
        case CERT_ALT_NAME_RFC822_NAME:
            enc_ia5_wide(c, ASN_CONTEXT(1), e->pwszRfc822Name);
            break;
        case CERT_ALT_NAME_DNS_NAME:
            enc_ia5_wide(c, ASN_CONTEXT(2), e->pwszDNSName);
            break;
        case CERT_ALT_NAME_URL:
            enc_ia5_wide(c, ASN_CONTEXT(6), e->pwszURL);
            break;
        case CERT_ALT_NAME_DIRECTORY_NAME:
            DerWriteTlv(&c->w, ASN_CONSTRUCTED_CONTEXT(4), e->DirectoryName.pbData, e->DirectoryName.cbData);
            break;
        case CERT_ALT_NAME_IP_ADDRESS:
            DerWriteTlv(&c->w, ASN_CONTEXT(7), e->IPAddress.pbData, e->IPAddress.cbData);
            break;
        case CERT_ALT_NAME_REGISTERED_ID: {
            DWORD start = c->w.size;
            DerWriteOid(&c->w, e->pszRegisteredID);
            if (!c->w.error)
                c->w.data[start] = ASN_CONTEXT(8);
            break;
        }
        default:
            fail(c, E_INVALIDARG);
            break;
        }
    }
    DerEnd(&c->w, mark);
}

static void enc_cert_info(enc_ctx *c, const CERT_INFO *info)
{
    DWORD mark = DerBegin(&c->w, ASN_SEQUENCE);
    if (info->dwVersion != CERT_V1) {
        DWORD version = DerBegin(&c->w, ASN_CONSTRUCTED_CONTEXT(0));
        DerWriteInt(&c->w, (int)info->dwVersion);
        DerEnd(&c->w, version);
    }
    DerWriteIntLe(&c->w, info->SerialNumber.pbData, info->SerialNumber.cbData, FALSE);
    enc_algid(c, &info->SignatureAlgorithm);
    DerWriteBytes(&c->w, info->Issuer.pbData, info->Issuer.cbData);
    DWORD validity = DerBegin(&c->w, ASN_SEQUENCE);
    DerWriteTime(&c->w, &info->NotBefore);
    DerWriteTime(&c->w, &info->NotAfter);
    DerEnd(&c->w, validity);
    DerWriteBytes(&c->w, info->Subject.pbData, info->Subject.cbData);
    enc_pubkey(c, &info->SubjectPublicKeyInfo);
    if (info->IssuerUniqueId.cbData)
        enc_bits(c, ASN_CONTEXT(1), &info->IssuerUniqueId, FALSE);
    if (info->SubjectUniqueId.cbData)
        enc_bits(c, ASN_CONTEXT(2), &info->SubjectUniqueId, FALSE);
    if (info->cExtension) {
        DWORD ext = DerBegin(&c->w, ASN_CONSTRUCTED_CONTEXT(3));
        enc_extensions(c, info->cExtension, info->rgExtension);
        DerEnd(&c->w, ext);
    }
    DerEnd(&c->w, mark);
}

static void enc_crl_info(enc_ctx *c, const CRL_INFO *info)
{
    DWORD mark = DerBegin(&c->w, ASN_SEQUENCE);
    if (info->dwVersion != CRL_V1)
        DerWriteInt(&c->w, (int)info->dwVersion);
    enc_algid(c, &info->SignatureAlgorithm);
    DerWriteBytes(&c->w, info->Issuer.pbData, info->Issuer.cbData);
    DerWriteTime(&c->w, &info->ThisUpdate);
    if (info->NextUpdate.dwLowDateTime || info->NextUpdate.dwHighDateTime)
        DerWriteTime(&c->w, &info->NextUpdate);
    if (info->cCRLEntry) {
        DWORD entries = DerBegin(&c->w, ASN_SEQUENCE);
        for (DWORD i = 0; i < info->cCRLEntry && !c->w.error; i++) {
            const CRL_ENTRY *e = &info->rgCRLEntry[i];
            DWORD entry = DerBegin(&c->w, ASN_SEQUENCE);
            DerWriteIntLe(&c->w, e->SerialNumber.pbData, e->SerialNumber.cbData, FALSE);
            DerWriteTime(&c->w, &e->RevocationDate);
            if (e->cExtension)
                enc_extensions(c, e->cExtension, e->rgExtension);
            DerEnd(&c->w, entry);
        }
        DerEnd(&c->w, entries);
    }
    if (info->cExtension) {
        DWORD ext = DerBegin(&c->w, ASN_CONSTRUCTED_CONTEXT(0));
        enc_extensions(c, info->cExtension, info->rgExtension);
        DerEnd(&c->w, ext);
    }
    DerEnd(&c->w, mark);
}

/* a distribution point name as the [0] field of a distribution point or an issuing distribution point. */
static void enc_dist_point_name(enc_ctx *c, const CRL_DIST_POINT_NAME *name)
{
    if (name->dwDistPointNameChoice == CRL_DIST_POINT_NO_NAME)
        return;
    if (name->dwDistPointNameChoice != CRL_DIST_POINT_FULL_NAME) {
        fail(c, E_INVALIDARG);
        return;
    }
    DWORD mark = DerBegin(&c->w, ASN_CONSTRUCTED_CONTEXT(0));
    enc_alt_name(c, ASN_CONSTRUCTED_CONTEXT(0), &name->FullName);
    DerEnd(&c->w, mark);
}

static void enc_bool_field(enc_ctx *c, BYTE tag, BOOL value)
{
    if (value) {
        BYTE b = 0xFF;
        DerWriteTlv(&c->w, tag, &b, 1);
    }
}

static void enc_alt_entry(enc_ctx *c, const CERT_ALT_NAME_ENTRY *entry)
{
    CERT_ALT_NAME_INFO one = {1, (PCERT_ALT_NAME_ENTRY)entry};
    der_writer saved = c->w;
    DerWriteInit(&c->w);
    enc_alt_name(c, ASN_SEQUENCE, &one);
    der_writer inner = c->w;
    c->w = saved;
    if (inner.error) {
        fail(c, inner.error);
    } else {
        der_reader r = {inner.data, inner.size};
        der_item seq;
        if (!DerRead(&r, &seq))
            DerWriteBytes(&c->w, seq.content, seq.length);
    }
    DerWriteFree(&inner);
}

/* ------------------------------------------------------------------ top level encoders */

typedef void (*encode_fn)(enc_ctx *c, const void *info);

static void e_cert(enc_ctx *c, const void *p)
{
    const CERT_SIGNED_CONTENT_INFO *info = p;
    DWORD mark = DerBegin(&c->w, ASN_SEQUENCE);
    DerWriteBytes(&c->w, info->ToBeSigned.pbData, info->ToBeSigned.cbData);
    enc_algid(c, &info->SignatureAlgorithm);
    DWORD bits = DerBegin(&c->w, ASN_BITSTRING);
    BYTE unused = (BYTE)(info->Signature.cUnusedBits & 7);
    DerWriteBytes(&c->w, &unused, 1);
    if (c->flags & CRYPT_ENCODE_NO_SIGNATURE_BYTE_REVERSAL_FLAG) {
        DerWriteBytes(&c->w, info->Signature.pbData, info->Signature.cbData);
    } else {
        for (DWORD i = info->Signature.cbData; i; i--)
            DerWriteBytes(&c->w, &info->Signature.pbData[i - 1], 1);
    }
    DerEnd(&c->w, bits);
    DerEnd(&c->w, mark);
}

static void e_cert_info(enc_ctx *c, const void *p) { enc_cert_info(c, p); }

static void e_crl_info(enc_ctx *c, const void *p) { enc_crl_info(c, p); }

static void e_crl_dist_points(enc_ctx *c, const void *p)
{
    const CRL_DIST_POINTS_INFO *info = p;
    DWORD mark = DerBegin(&c->w, ASN_SEQUENCE);
    for (DWORD i = 0; i < info->cDistPoint && !c->w.error; i++) {
        const CRL_DIST_POINT *point = &info->rgDistPoint[i];
        DWORD seq = DerBegin(&c->w, ASN_SEQUENCE);
        enc_dist_point_name(c, &point->DistPointName);
        if (point->ReasonFlags.cbData)
            enc_bits(c, ASN_CONTEXT(1), &point->ReasonFlags, TRUE);
        if (point->CRLIssuer.cAltEntry)
            enc_alt_name(c, ASN_CONSTRUCTED_CONTEXT(2), &point->CRLIssuer);
        DerEnd(&c->w, seq);
    }
    DerEnd(&c->w, mark);
}

static void e_issuing_dist_point(enc_ctx *c, const void *p)
{
    const CRL_ISSUING_DIST_POINT *info = p;
    DWORD mark = DerBegin(&c->w, ASN_SEQUENCE);
    enc_dist_point_name(c, &info->DistPointName);
    enc_bool_field(c, ASN_CONTEXT(1), info->fOnlyContainsUserCerts);
    enc_bool_field(c, ASN_CONTEXT(2), info->fOnlyContainsCACerts);
    if (info->OnlySomeReasonFlags.cbData)
        enc_bits(c, ASN_CONTEXT(3), &info->OnlySomeReasonFlags, TRUE);
    enc_bool_field(c, ASN_CONTEXT(4), info->fIndirectCRL);
    DerEnd(&c->w, mark);
}

static void e_info_access(enc_ctx *c, const void *p)
{
    const CERT_AUTHORITY_INFO_ACCESS *info = p;
    DWORD mark = DerBegin(&c->w, ASN_SEQUENCE);
    for (DWORD i = 0; i < info->cAccDescr && !c->w.error; i++) {
        DWORD seq = DerBegin(&c->w, ASN_SEQUENCE);
        DerWriteOid(&c->w, info->rgAccDescr[i].pszAccessMethod);
        enc_alt_entry(c, &info->rgAccDescr[i].AccessLocation);
        DerEnd(&c->w, seq);
    }
    DerEnd(&c->w, mark);
}

static void e_extensions(enc_ctx *c, const void *p)
{
    const CERT_EXTENSIONS *ext = p;
    enc_extensions(c, ext->cExtension, ext->rgExtension);
}

static void e_any_string(enc_ctx *c, const void *p)
{
    const CERT_NAME_VALUE *v = p;
    enc_value(c, nullptr, v->dwValueType, &v->Value, FALSE);
}

static void e_unicode_string(enc_ctx *c, const void *p)
{
    const CERT_NAME_VALUE *v = p;
    if ((v->dwValueType & CERT_RDN_TYPE_MASK) == CERT_RDN_ANY_TYPE) {
        fail(c, CRYPT_E_ASN1_CHOICE);
        return;
    }
    enc_value(c, nullptr, v->dwValueType, &v->Value, TRUE);
}

static void e_name(enc_ctx *c, const void *p) { enc_name(c, p, FALSE); }
static void e_unicode_name(enc_ctx *c, const void *p) { enc_name(c, p, TRUE); }
static void e_pubkey(enc_ctx *c, const void *p) { enc_pubkey(c, p); }
static void e_alt_name(enc_ctx *c, const void *p) { enc_alt_name(c, ASN_SEQUENCE, p); }
static void e_bits(enc_ctx *c, const void *p) { enc_bits(c, ASN_BITSTRING, p, FALSE); }
static void e_bits_trimmed(enc_ctx *c, const void *p) { enc_bits(c, ASN_BITSTRING, p, TRUE); }

static void e_octets(enc_ctx *c, const void *p)
{
    const CRYPT_DATA_BLOB *blob = p;
    DerWriteTlv(&c->w, ASN_OCTETSTRING, blob->pbData, blob->cbData);
}

static void e_int(enc_ctx *c, const void *p) { DerWriteInt(&c->w, *(const int *)p); }

static void e_enumerated(enc_ctx *c, const void *p)
{
    DWORD start = c->w.size;
    DerWriteInt(&c->w, *(const int *)p);
    if (!c->w.error)
        c->w.data[start] = ASN_ENUMERATED;
}

static void e_multi_int(enc_ctx *c, const void *p)
{
    const CRYPT_INTEGER_BLOB *blob = p;
    DerWriteIntLe(&c->w, blob->pbData, blob->cbData, FALSE);
}

static void e_multi_uint(enc_ctx *c, const void *p)
{
    const CRYPT_UINT_BLOB *blob = p;
    DerWriteIntLe(&c->w, blob->pbData, blob->cbData, TRUE);
}

static void e_time(enc_ctx *c, const void *p) { DerWriteTime(&c->w, p); }

static void e_utc_time(enc_ctx *c, const void *p)
{
    SYSTEMTIME st;
    if (!FileTimeToSystemTime(p, &st) || st.wYear < 1950 || st.wYear >= 2050) {
        fail(c, CRYPT_E_BAD_ENCODE);
        return;
    }
    DerWriteTime(&c->w, p);
}

static void e_oid(enc_ctx *c, const void *p) { DerWriteOid(&c->w, *(const LPCSTR *)p); }

static void e_basic_constraints2(enc_ctx *c, const void *p)
{
    const CERT_BASIC_CONSTRAINTS2_INFO *info = p;
    DWORD mark = DerBegin(&c->w, ASN_SEQUENCE);
    if (info->fCA) {
        BYTE t = 0xFF;
        DerWriteTlv(&c->w, ASN_BOOLEAN, &t, 1);
        if (info->fPathLenConstraint)
            DerWriteInt(&c->w, (int)info->dwPathLenConstraint);
    }
    DerEnd(&c->w, mark);
}

static void e_eku(enc_ctx *c, const void *p)
{
    const CERT_ENHKEY_USAGE *usage = p;
    DWORD mark = DerBegin(&c->w, ASN_SEQUENCE);
    for (DWORD i = 0; i < usage->cUsageIdentifier; i++)
        DerWriteOid(&c->w, usage->rgpszUsageIdentifier[i]);
    DerEnd(&c->w, mark);
}

static void e_authority_key_id2(enc_ctx *c, const void *p)
{
    const CERT_AUTHORITY_KEY_ID2_INFO *info = p;
    DWORD mark = DerBegin(&c->w, ASN_SEQUENCE);
    if (info->KeyId.cbData)
        DerWriteTlv(&c->w, ASN_CONTEXT(0), info->KeyId.pbData, info->KeyId.cbData);
    if (info->AuthorityCertIssuer.cAltEntry)
        enc_alt_name(c, ASN_CONSTRUCTED_CONTEXT(1), &info->AuthorityCertIssuer);
    if (info->AuthorityCertSerialNumber.cbData) {
        DWORD start = c->w.size;
        DerWriteIntLe(&c->w, info->AuthorityCertSerialNumber.pbData, info->AuthorityCertSerialNumber.cbData,
                      FALSE);
        if (!c->w.error)
            c->w.data[start] = ASN_CONTEXT(2);
    }
    DerEnd(&c->w, mark);
}

static void e_rsa_public_key(enc_ctx *c, const void *p)
{
    const BLOBHEADER *header = p;
    if (header->bType != PUBLICKEYBLOB) {
        fail(c, CRYPT_E_BAD_ENCODE);
        return;
    }
    const RSAPUBKEY *key = (const RSAPUBKEY *)(header + 1);
    if (key->magic != 0x31415352) {
        fail(c, CRYPT_E_BAD_ENCODE);
        return;
    }
    DWORD mark = DerBegin(&c->w, ASN_SEQUENCE);
    DerWriteIntLe(&c->w, (const BYTE *)(key + 1), key->bitlen / 8, TRUE);
    BYTE e[4] = {(BYTE)key->pubexp, (BYTE)(key->pubexp >> 8), (BYTE)(key->pubexp >> 16), (BYTE)(key->pubexp >> 24)};
    DerWriteIntLe(&c->w, e, 4, TRUE);
    DerEnd(&c->w, mark);
}

static encode_fn encoder_for(LPCSTR type)
{
    if (IS_INTRESOURCE(type)) {
        switch ((ULONG_PTR)type) {
        case 1:
            return e_cert;
        case 2:
            return e_cert_info;
        case 3:
            return e_crl_info;
        case 5:
            return e_extensions;
        case 6:
            return e_any_string;
        case 7:
            return e_name;
        case 8:
            return e_pubkey;
        case 12:
            return e_alt_name;
        case 14:
        case 51:
            return e_bits_trimmed;
        case 15:
            return e_basic_constraints2;
        case 17:
            return e_utc_time;
        case 19:
            return e_rsa_public_key;
        case 20:
            return e_unicode_name;
        case 24:
            return e_unicode_string;
        case 25:
            return e_octets;
        case 26:
            return e_bits;
        case 27:
            return e_int;
        case 28:
            return e_multi_int;
        case 29:
            return e_enumerated;
        case 30:
            return e_time;
        case 31:
            return e_authority_key_id2;
        case 32:
            return e_info_access;
        case 35:
            return e_crl_dist_points;
        case 36:
            return e_eku;
        case 38:
            return e_multi_uint;
        case 54:
            return e_issuing_dist_point;
        case 73:
            return e_oid;
        }
        return nullptr;
    }
    static const struct {
        LPCSTR oid;
        encode_fn fn;
    } map[] = {
        {szOID_SUBJECT_ALT_NAME, e_alt_name},      {szOID_ISSUER_ALT_NAME, e_alt_name},
        {szOID_SUBJECT_ALT_NAME2, e_alt_name},     {szOID_ISSUER_ALT_NAME2, e_alt_name},
        {szOID_BASIC_CONSTRAINTS2, e_basic_constraints2},
        {szOID_KEY_USAGE, e_bits_trimmed},         {szOID_ENHANCED_KEY_USAGE, e_eku},
        {szOID_SUBJECT_KEY_IDENTIFIER, e_octets},  {szOID_AUTHORITY_KEY_IDENTIFIER2, e_authority_key_id2},
        {szOID_CRL_NUMBER, e_multi_int},           {szOID_CRL_REASON_CODE, e_enumerated},
        {szOID_DELTA_CRL_INDICATOR, e_multi_int},  {szOID_CRL_DIST_POINTS, e_crl_dist_points},
        {szOID_FRESHEST_CRL, e_crl_dist_points},   {szOID_ISSUING_DIST_POINT, e_issuing_dist_point},
        {szOID_AUTHORITY_INFO_ACCESS, e_info_access}, {"1.3.6.1.5.5.7.1.11", e_info_access},
    };
    for (DWORD i = 0; i < RTL_NUMBER_OF(map); i++)
        if (!strcmp(map[i].oid, type))
            return map[i].fn;
    return nullptr;
}

static DWORD run_encoder(encode_fn fn, const void *info, DWORD flags, enc_ctx *c)
{
    DerWriteInit(&c->w);
    c->flags = flags;
    c->location = 0;
    fn(c, info);
    if (c->w.error) {
        DWORD error = c->w.error;
        DerWriteFree(&c->w);
        return error;
    }
    return 0;
}

BYTE *CrEncodeAlloc(LPCSTR type, const void *info, DWORD flags, DWORD *out_size)
{
    encode_fn fn = encoder_for(type);
    enc_ctx c;
    if (!fn) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return nullptr;
    }
    DWORD error = run_encoder(fn, info, flags, &c);
    if (error) {
        SetLastError(error);
        return nullptr;
    }
    if (!c.w.data && !(c.w.data = CrAlloc(1))) {
        SetLastError(ERROR_OUTOFMEMORY);
        return nullptr;
    }
    *out_size = c.w.size;
    return c.w.data;
}

BOOL WINAPI CryptEncodeObjectEx(DWORD dwCertEncodingType, LPCSTR lpszStructType, const void *pvStructInfo,
                                DWORD dwFlags, PCRYPT_ENCODE_PARA pEncodePara, void *pvEncoded, DWORD *pcbEncoded)
{
    if (!pcbEncoded || !pvStructInfo || (!pvEncoded && (dwFlags & CRYPT_ENCODE_ALLOC_FLAG)))
        return CrFail(ERROR_INVALID_PARAMETER);
    if (!(dwCertEncodingType & (X509_ASN_ENCODING | PKCS_7_ASN_ENCODING)))
        return CrFail(ERROR_FILE_NOT_FOUND);
    encode_fn fn = encoder_for(lpszStructType);
    if (!fn)
        return CrFail(ERROR_FILE_NOT_FOUND);
    enc_ctx c;
    DWORD error = run_encoder(fn, pvStructInfo, dwFlags, &c);
    if (error) {
        if (error >= (DWORD)CRYPT_E_INVALID_NUMERIC_STRING && error <= (DWORD)CRYPT_E_INVALID_IA5_STRING)
            *pcbEncoded = c.location;
        else
            *pcbEncoded = 0;
        return CrFail(error);
    }
    BOOL ok = TRUE;
    if (dwFlags & CRYPT_ENCODE_ALLOC_FLAG) {
        BYTE *out;
        if (pEncodePara && pEncodePara->cbSize >= FIELD_OFFSET(CRYPT_ENCODE_PARA, pfnFree) && pEncodePara->pfnAlloc)
            out = pEncodePara->pfnAlloc(c.w.size);
        else
            out = LocalAlloc(LMEM_FIXED, c.w.size);
        if (!out) {
            ok = CrFail(ERROR_OUTOFMEMORY);
        } else {
            memcpy(out, c.w.data, c.w.size);
            *(BYTE **)pvEncoded = out;
            *pcbEncoded = c.w.size;
        }
    } else if (!pvEncoded) {
        *pcbEncoded = c.w.size;
    } else if (*pcbEncoded < c.w.size) {
        *pcbEncoded = c.w.size;
        ok = CrFail(ERROR_MORE_DATA);
    } else {
        memcpy(pvEncoded, c.w.data, c.w.size);
        *pcbEncoded = c.w.size;
    }
    DerWriteFree(&c.w);
    return ok;
}

BOOL WINAPI CryptEncodeObject(DWORD dwCertEncodingType, LPCSTR lpszStructType, const void *pvStructInfo,
                              BYTE *pbEncoded, DWORD *pcbEncoded)
{
    return CryptEncodeObjectEx(dwCertEncodingType, lpszStructType, pvStructInfo, 0, nullptr, pbEncoded, pcbEncoded);
}
