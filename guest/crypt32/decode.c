/*
 * CryptDecodeObject and CryptDecodeObjectEx: DER to the X.509 structures of wincrypt.h.
 *
 * every decoder runs twice: once with an empty arena to measure the structure and the data it points to, then
 * into the caller's buffer. decoders fill local copies and store them only when the arena has memory, so both
 * passes make exactly the same allocations.
 */
#include "crypt32_private.h"

/* ------------------------------------------------------------------ arena */

void *ArenaAlloc(cr_arena *a, SIZE_T size)
{
    SIZE_T offset = (a->used + 7) & ~(SIZE_T)7;
    a->used = offset + size;
    return a->base ? a->base + offset : nullptr;
}

void *ArenaCopy(cr_arena *a, const void *data, SIZE_T size)
{
    void *p = ArenaAlloc(a, size);
    if (p && size)
        memcpy(p, data, size);
    return p;
}

LPSTR ArenaString(cr_arena *a, LPCSTR s)
{
    return ArenaCopy(a, s, strlen(s) + 1);
}

/* ------------------------------------------------------------------ string types */

DWORD CrStringTypeFromTag(BYTE tag)
{
    switch (tag) {
    case ASN_NUMERICSTRING:
        return CERT_RDN_NUMERIC_STRING;
    case ASN_PRINTABLESTRING:
        return CERT_RDN_PRINTABLE_STRING;
    case ASN_T61STRING:
        return CERT_RDN_TELETEX_STRING;
    case ASN_VIDEOTEXSTRING:
        return CERT_RDN_VIDEOTEX_STRING;
    case ASN_IA5STRING:
        return CERT_RDN_IA5_STRING;
    case ASN_GRAPHICSTRING:
        return CERT_RDN_GRAPHIC_STRING;
    case ASN_VISIBLESTRING:
        return CERT_RDN_VISIBLE_STRING;
    case ASN_GENERALSTRING:
        return CERT_RDN_GENERAL_STRING;
    case ASN_UNIVERSALSTRING:
        return CERT_RDN_UNIVERSAL_STRING;
    case ASN_BMPSTRING:
        return CERT_RDN_BMP_STRING;
    case ASN_UTF8STRING:
        return CERT_RDN_UTF8_STRING;
    case ASN_OCTETSTRING:
        return CERT_RDN_OCTET_STRING;
    }
    return CERT_RDN_ENCODED_BLOB;
}

BYTE CrTagFromStringType(DWORD type)
{
    static const BYTE tags[] = {0, 0, ASN_OCTETSTRING, ASN_NUMERICSTRING, ASN_PRINTABLESTRING, ASN_T61STRING,
                                ASN_VIDEOTEXSTRING, ASN_IA5STRING, ASN_GRAPHICSTRING, ASN_VISIBLESTRING,
                                ASN_GENERALSTRING, ASN_UNIVERSALSTRING, ASN_BMPSTRING, ASN_UTF8STRING};
    return type < RTL_NUMBER_OF(tags) ? tags[type] : 0;
}

static BOOL is_utf8(const BYTE *p, DWORD n)
{
    BOOL high = FALSE;
    for (DWORD i = 0; i < n;) {
        BYTE c = p[i];
        DWORD extra = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 9;
        if (extra == 9 || i + extra >= n)
            return FALSE;
        for (DWORD j = 1; j <= extra; j++)
            if ((p[i + j] & 0xC0) != 0x80)
                return FALSE;
        high |= extra != 0;
        i += extra + 1;
    }
    return high;
}

/*
 * converts the content of a string value to utf-16; returns the length in
 * characters, writing at most capacity characters when out is not NULL.
 */
DWORD CrDecodeNameValueString(DWORD value_type, const BYTE *data, DWORD size, WCHAR *out, DWORD capacity)
{
    DWORD n = 0;
#define PUT(c)                                                                                                    \
    do {                                                                                                          \
        if (out && n < capacity)                                                                                  \
            out[n] = (WCHAR)(c);                                                                                  \
        n++;                                                                                                      \
    } while (0)
    switch (value_type & CERT_RDN_TYPE_MASK) {
    case CERT_RDN_BMP_STRING:
        for (DWORD i = 0; i + 1 < size; i += 2)
            PUT(data[i] << 8 | data[i + 1]);
        break;
    case CERT_RDN_UNIVERSAL_STRING:
        for (DWORD i = 0; i + 3 < size; i += 4) {
            DWORD c = (DWORD)data[i] << 24 | data[i + 1] << 16 | data[i + 2] << 8 | data[i + 3];
            if (c >= 0x10000 && c <= 0x10FFFF) {
                c -= 0x10000;
                PUT(0xD800 | (c >> 10));
                PUT(0xDC00 | (c & 0x3FF));
            } else {
                PUT(c > 0xFFFF ? 0xFFFD : c);
            }
        }
        break;
    case CERT_RDN_UTF8_STRING:
    case CERT_RDN_TELETEX_STRING:
        if ((value_type & CERT_RDN_TYPE_MASK) == CERT_RDN_UTF8_STRING ||
            (!(value_type & CERT_RDN_DISABLE_IE4_UTF8_FLAG) && is_utf8(data, size))) {
            int count = size ? MultiByteToWideChar(CP_UTF8, 0, (LPCCH)data, (int)size, nullptr, 0) : 0;
            if (count > 0 && out && (DWORD)count <= capacity)
                MultiByteToWideChar(CP_UTF8, 0, (LPCCH)data, (int)size, out, count);
            n = count > 0 ? (DWORD)count : 0;
            break;
        }
        [[fallthrough]];
    default:
        for (DWORD i = 0; i < size; i++)
            PUT(data[i]);
        break;
    }
#undef PUT
    return n;
}

/* ------------------------------------------------------------------ primitives */

static DWORD dec_oid(cr_arena *a, const der_item *item, LPSTR *out)
{
    char buffer[512];
    DWORD error = DerParseOid(item, buffer, sizeof(buffer));
    if (error)
        return error;
    LPSTR s = ArenaString(a, buffer);
    if (s)
        *out = s;
    return 0;
}

static void dec_blob(cr_arena *a, const BYTE *data, DWORD size, CRYPT_DATA_BLOB *out)
{
    out->cbData = size;
    out->pbData = size ? ArenaCopy(a, data, size) : nullptr;
}

static void dec_reversed(cr_arena *a, const BYTE *data, DWORD size, CRYPT_DATA_BLOB *out)
{
    BYTE *p = size ? ArenaAlloc(a, size) : nullptr;
    if (p)
        for (DWORD i = 0; i < size; i++)
            p[i] = data[size - 1 - i];
    out->cbData = size;
    out->pbData = p;
}

static DWORD dec_bits(cr_arena *a, const der_item *item, CRYPT_BIT_BLOB *out)
{
    if (!item->length || item->content[0] > 7 || (item->length == 1 && item->content[0]))
        return CRYPT_E_ASN1_CORRUPT;
    CRYPT_DATA_BLOB blob;
    dec_blob(a, item->content + 1, item->length - 1, &blob);
    out->cbData = blob.cbData;
    out->pbData = blob.pbData;
    out->cUnusedBits = item->content[0];
    return 0;
}

static DWORD dec_int_blob(cr_arena *a, const der_item *item, CRYPT_INTEGER_BLOB *out, BOOL unsigned_value)
{
    if (item->tag != ASN_INTEGER)
        return CRYPT_E_ASN1_BADTAG;
    if (!item->length)
        return CRYPT_E_ASN1_CORRUPT;
    const BYTE *p = item->content;
    DWORD n = item->length;
    if (unsigned_value)
        while (n > 1 && !p[0]) {
            p++;
            n--;
        }
    dec_reversed(a, p, n, out);
    return 0;
}

static DWORD dec_algid(cr_arena *a, const der_item *item, CRYPT_ALGORITHM_IDENTIFIER *out)
{
    if (item->tag != ASN_SEQUENCE)
        return CRYPT_E_ASN1_BADTAG;
    der_reader r;
    der_item oid;
    DerOpen(&r, item);
    DWORD error = DerExpect(&r, ASN_OID, &oid);
    if (error)
        return error;
    memset(out, 0, sizeof(*out));
    if ((error = dec_oid(a, &oid, &out->pszObjId)))
        return error;
    if (r.n) {
        der_item params;
        if ((error = DerRead(&r, &params)))
            return error;
        dec_blob(a, params.start, params.total, &out->Parameters);
    }
    return 0;
}

static DWORD dec_pubkey(cr_arena *a, const der_item *item, CERT_PUBLIC_KEY_INFO *out)
{
    if (item->tag != ASN_SEQUENCE)
        return CRYPT_E_ASN1_BADTAG;
    der_reader r;
    der_item alg, key;
    DerOpen(&r, item);
    DWORD error = DerRead(&r, &alg);
    if (!error)
        error = dec_algid(a, &alg, &out->Algorithm);
    if (!error)
        error = DerExpect(&r, ASN_BITSTRING, &key);
    if (!error)
        error = dec_bits(a, &key, &out->PublicKey);
    return error;
}

static DWORD dec_extensions(cr_arena *a, const der_item *item, DWORD *count, PCERT_EXTENSION *array)
{
    if (item->tag != ASN_SEQUENCE)
        return CRYPT_E_ASN1_BADTAG;
    DWORD n = DerCount(item);
    PCERT_EXTENSION ext = n ? ArenaAlloc(a, n * sizeof(CERT_EXTENSION)) : nullptr;
    der_reader r;
    DerOpen(&r, item);
    for (DWORD i = 0; i < n; i++) {
        der_item seq, oid, value;
        der_reader inner;
        CERT_EXTENSION e = {};
        DWORD error = DerExpect(&r, ASN_SEQUENCE, &seq);
        if (error)
            return error;
        DerOpen(&inner, &seq);
        if ((error = DerExpect(&inner, ASN_OID, &oid)) || (error = dec_oid(a, &oid, &e.pszObjId)))
            return error;
        if (DerPeek(&inner, ASN_BOOLEAN)) {
            der_item critical;
            DerRead(&inner, &critical);
            if (critical.length != 1)
                return CRYPT_E_ASN1_CORRUPT;
            e.fCritical = critical.content[0] != 0;
        }
        if ((error = DerExpect(&inner, ASN_OCTETSTRING, &value)))
            return error;
        dec_blob(a, value.content, value.length, &e.Value);
        if (ext)
            ext[i] = e;
    }
    *count = n;
    *array = ext;
    return 0;
}

/* stores a string value: wide and terminated for the unicode structures, raw otherwise. */
static DWORD dec_value(cr_arena *a, const der_item *item, BOOL unicode, DWORD flags, DWORD *type,
                       CERT_RDN_VALUE_BLOB *value)
{
    DWORD t = CrStringTypeFromTag(item->tag);
    *type = t;
    if (t == CERT_RDN_ENCODED_BLOB) {
        dec_blob(a, item->start, item->total, value);
        return 0;
    }
    if (t == CERT_RDN_OCTET_STRING) {
        dec_blob(a, item->content, item->length, value);
        return 0;
    }
    BOOL wide = unicode || t == CERT_RDN_BMP_STRING || t == CERT_RDN_UTF8_STRING;
    if (!wide && t == CERT_RDN_UNIVERSAL_STRING) {
        /* an array of 32 bit characters in host order. */
        DWORD count = item->length / 4;
        DWORD *chars = ArenaAlloc(a, (count + 1) * 4);
        if (chars) {
            for (DWORD i = 0; i < count; i++) {
                const BYTE *c = item->content + i * 4;
                chars[i] = (DWORD)c[0] << 24 | c[1] << 16 | c[2] << 8 | c[3];
            }
            chars[count] = 0;
        }
        value->cbData = count * 4;
        value->pbData = (BYTE *)chars;
        return 0;
    }
    if (!wide) {
        BYTE *s = ArenaAlloc(a, item->length + 1);
        if (s) {
            memcpy(s, item->content, item->length);
            s[item->length] = 0;
        }
        value->cbData = item->length;
        value->pbData = s;
        return 0;
    }
    DWORD conv = t | (flags & CERT_RDN_DISABLE_IE4_UTF8_FLAG);
    DWORD n = CrDecodeNameValueString(conv, item->content, item->length, nullptr, 0);
    WCHAR *w = ArenaAlloc(a, (n + 1) * sizeof(WCHAR));
    if (w) {
        CrDecodeNameValueString(conv, item->content, item->length, w, n);
        w[n] = 0;
    }
    value->cbData = n * sizeof(WCHAR);
    value->pbData = (BYTE *)w;
    return 0;
}

static DWORD dec_name(cr_arena *a, const der_item *item, BOOL unicode, DWORD flags, CERT_NAME_INFO *out)
{
    if (item->tag != ASN_SEQUENCE)
        return CRYPT_E_ASN1_BADTAG;
    DWORD n = DerCount(item);
    PCERT_RDN rdns = n ? ArenaAlloc(a, n * sizeof(CERT_RDN)) : nullptr;
    der_reader r;
    DerOpen(&r, item);
    for (DWORD i = 0; i < n; i++) {
        der_item set;
        DWORD error = DerExpect(&r, ASN_SET, &set);
        if (error)
            return error;
        DWORD count = DerCount(&set);
        PCERT_RDN_ATTR attrs = count ? ArenaAlloc(a, count * sizeof(CERT_RDN_ATTR)) : nullptr;
        der_reader inner;
        DerOpen(&inner, &set);
        for (DWORD j = 0; j < count; j++) {
            der_item seq, oid, value;
            der_reader fields;
            CERT_RDN_ATTR attr = {};
            if ((error = DerExpect(&inner, ASN_SEQUENCE, &seq)))
                return error;
            DerOpen(&fields, &seq);
            if ((error = DerExpect(&fields, ASN_OID, &oid)) || (error = dec_oid(a, &oid, &attr.pszObjId)) ||
                (error = DerRead(&fields, &value)) ||
                (error = dec_value(a, &value, unicode, flags, &attr.dwValueType, &attr.Value)))
                return error;
            if (attrs)
                attrs[j] = attr;
        }
        if (rdns) {
            rdns[i].cRDNAttr = count;
            rdns[i].rgRDNAttr = attrs;
        }
    }
    out->cRDN = n;
    out->rgRDN = rdns;
    return 0;
}

static DWORD dec_alt_entry(cr_arena *a, const der_item *name_item, CERT_ALT_NAME_ENTRY *out)
{
    der_item name = *name_item;
    CERT_ALT_NAME_ENTRY e = {};
    DWORD error;
    switch (name.tag) {
    case ASN_CONSTRUCTED_CONTEXT(0): {
        der_reader inner;
        der_item oid, wrapper;
        CERT_OTHER_NAME other = {};
        DerOpen(&inner, &name);
        if ((error = DerExpect(&inner, ASN_OID, &oid)) || (error = dec_oid(a, &oid, &other.pszObjId)) ||
            (error = DerExpect(&inner, ASN_CONSTRUCTED_CONTEXT(0), &wrapper)))
            return error;
        dec_blob(a, wrapper.content, wrapper.length, &other.Value);
        PCERT_OTHER_NAME p = ArenaCopy(a, &other, sizeof(other));
        e.dwAltNameChoice = CERT_ALT_NAME_OTHER_NAME;
        e.pOtherName = p;
        break;
    }
    case ASN_CONTEXT(1):
    case ASN_CONTEXT(2):
    case ASN_CONTEXT(6): {
        WCHAR *w = ArenaAlloc(a, (name.length + 1) * sizeof(WCHAR));
        if (w) {
            for (DWORD k = 0; k < name.length; k++)
                w[k] = name.content[k];
            w[name.length] = 0;
        }
        e.dwAltNameChoice = name.tag == ASN_CONTEXT(1)   ? CERT_ALT_NAME_RFC822_NAME
                            : name.tag == ASN_CONTEXT(2) ? CERT_ALT_NAME_DNS_NAME
                                                         : CERT_ALT_NAME_URL;
        e.pwszDNSName = w;
        break;
    }
    case ASN_CONSTRUCTED_CONTEXT(4): {
        der_reader inner;
        der_item dn;
        DerOpen(&inner, &name);
        if ((error = DerExpect(&inner, ASN_SEQUENCE, &dn)))
            return error;
        e.dwAltNameChoice = CERT_ALT_NAME_DIRECTORY_NAME;
        dec_blob(a, dn.start, dn.total, &e.DirectoryName);
        break;
    }
    case ASN_CONTEXT(7):
        e.dwAltNameChoice = CERT_ALT_NAME_IP_ADDRESS;
        dec_blob(a, name.content, name.length, &e.IPAddress);
        break;
    case ASN_CONTEXT(8): {
        der_item oid = name;
        oid.tag = ASN_OID;
        e.dwAltNameChoice = CERT_ALT_NAME_REGISTERED_ID;
        if ((error = dec_oid(a, &oid, &e.pszRegisteredID)))
            return error;
        break;
    }
    default:
        return CRYPT_E_ASN1_BADTAG;
    }
    *out = e;
    return 0;
}

static DWORD dec_alt_name(cr_arena *a, const der_item *item, CERT_ALT_NAME_INFO *out)
{
    if (item->tag != ASN_SEQUENCE && (item->tag & 0xE0) != 0xA0)
        return CRYPT_E_ASN1_BADTAG;
    DWORD n = DerCount(item);
    PCERT_ALT_NAME_ENTRY entries = n ? ArenaAlloc(a, n * sizeof(CERT_ALT_NAME_ENTRY)) : nullptr;
    der_reader r;
    DerOpen(&r, item);
    for (DWORD i = 0; i < n; i++) {
        der_item name;
        CERT_ALT_NAME_ENTRY e;
        DWORD error = DerRead(&r, &name);
        if (!error)
            error = dec_alt_entry(a, &name, &e);
        if (error)
            return error;
        if (entries)
            entries[i] = e;
    }
    out->cAltEntry = n;
    out->rgAltEntry = entries;
    return 0;
}

static DWORD dec_cert_info(cr_arena *a, const der_item *item, CERT_INFO *info)
{
    if (item->tag != ASN_SEQUENCE)
        return CRYPT_E_ASN1_BADTAG;
    der_reader r;
    der_item it;
    DWORD error;
    memset(info, 0, sizeof(*info));
    DerOpen(&r, item);
    if (DerPeek(&r, ASN_CONSTRUCTED_CONTEXT(0))) {
        der_reader v;
        der_item version;
        int value;
        DerRead(&r, &it);
        DerOpen(&v, &it);
        if ((error = DerExpect(&v, ASN_INTEGER, &version)) || (error = DerParseInt(&version, &value)))
            return error;
        info->dwVersion = (DWORD)value;
    }
    if ((error = DerRead(&r, &it)) || (error = dec_int_blob(a, &it, &info->SerialNumber, FALSE)))
        return error;
    if ((error = DerRead(&r, &it)) || (error = dec_algid(a, &it, &info->SignatureAlgorithm)))
        return error;
    if ((error = DerExpect(&r, ASN_SEQUENCE, &it)))
        return error;
    dec_blob(a, it.start, it.total, &info->Issuer);
    der_item validity, time;
    der_reader times;
    if ((error = DerExpect(&r, ASN_SEQUENCE, &validity)))
        return error;
    DerOpen(&times, &validity);
    if ((error = DerRead(&times, &time)) || (error = DerParseTime(&time, &info->NotBefore)) ||
        (error = DerRead(&times, &time)) || (error = DerParseTime(&time, &info->NotAfter)))
        return error;
    if ((error = DerExpect(&r, ASN_SEQUENCE, &it)))
        return error;
    dec_blob(a, it.start, it.total, &info->Subject);
    if ((error = DerRead(&r, &it)) || (error = dec_pubkey(a, &it, &info->SubjectPublicKeyInfo)))
        return error;
    if (DerPeek(&r, ASN_CONTEXT(1))) {
        DerRead(&r, &it);
        if ((error = dec_bits(a, &it, &info->IssuerUniqueId)))
            return error;
    }
    if (DerPeek(&r, ASN_CONTEXT(2))) {
        DerRead(&r, &it);
        if ((error = dec_bits(a, &it, &info->SubjectUniqueId)))
            return error;
    }
    if (DerPeek(&r, ASN_CONSTRUCTED_CONTEXT(3))) {
        der_reader ext;
        der_item seq;
        DerRead(&r, &it);
        DerOpen(&ext, &it);
        if ((error = DerExpect(&ext, ASN_SEQUENCE, &seq)) ||
            (error = dec_extensions(a, &seq, &info->cExtension, &info->rgExtension)))
            return error;
    }
    return 0;
}

static DWORD dec_crl_info(cr_arena *a, const der_item *item, CRL_INFO *info)
{
    if (item->tag != ASN_SEQUENCE)
        return CRYPT_E_ASN1_BADTAG;
    der_reader r;
    der_item it;
    DWORD error;
    memset(info, 0, sizeof(*info));
    DerOpen(&r, item);
    if (DerPeek(&r, ASN_INTEGER)) {
        int value;
        DerRead(&r, &it);
        if ((error = DerParseInt(&it, &value)))
            return error;
        info->dwVersion = (DWORD)value;
    }
    if ((error = DerRead(&r, &it)) || (error = dec_algid(a, &it, &info->SignatureAlgorithm)))
        return error;
    if ((error = DerExpect(&r, ASN_SEQUENCE, &it)))
        return error;
    dec_blob(a, it.start, it.total, &info->Issuer);
    if ((error = DerRead(&r, &it)) || (error = DerParseTime(&it, &info->ThisUpdate)))
        return error;
    if (DerPeek(&r, ASN_UTCTIME) || DerPeek(&r, ASN_GENERALTIME)) {
        DerRead(&r, &it);
        if ((error = DerParseTime(&it, &info->NextUpdate)))
            return error;
    }
    if (DerPeek(&r, ASN_SEQUENCE)) {
        der_item list;
        DerRead(&r, &list);
        DWORD n = DerCount(&list);
        PCRL_ENTRY entries = n ? ArenaAlloc(a, n * sizeof(CRL_ENTRY)) : nullptr;
        der_reader items;
        DerOpen(&items, &list);
        for (DWORD i = 0; i < n; i++) {
            der_item seq, serial, date;
            der_reader fields;
            CRL_ENTRY e = {};
            if ((error = DerExpect(&items, ASN_SEQUENCE, &seq)))
                return error;
            DerOpen(&fields, &seq);
            if ((error = DerRead(&fields, &serial)) || (error = dec_int_blob(a, &serial, &e.SerialNumber, FALSE)) ||
                (error = DerRead(&fields, &date)) || (error = DerParseTime(&date, &e.RevocationDate)))
                return error;
            if (fields.n) {
                der_item ext;
                if ((error = DerRead(&fields, &ext)) ||
                    (error = dec_extensions(a, &ext, &e.cExtension, &e.rgExtension)))
                    return error;
            }
            if (entries)
                entries[i] = e;
        }
        info->cCRLEntry = n;
        info->rgCRLEntry = entries;
    }
    if (DerPeek(&r, ASN_CONSTRUCTED_CONTEXT(0))) {
        der_reader ext;
        der_item seq;
        DerRead(&r, &it);
        DerOpen(&ext, &it);
        if ((error = DerExpect(&ext, ASN_SEQUENCE, &seq)) ||
            (error = dec_extensions(a, &seq, &info->cExtension, &info->rgExtension)))
            return error;
    }
    return 0;
}

/* the [0] distribution point name of a distribution point or an issuing distribution point. */
static DWORD dec_dist_point_name(cr_arena *a, const der_item *item, CRL_DIST_POINT_NAME *out)
{
    der_reader r;
    der_item choice;
    DerOpen(&r, item);
    DWORD error = DerRead(&r, &choice);
    if (error)
        return error;
    if (choice.tag == ASN_CONSTRUCTED_CONTEXT(0)) {
        out->dwDistPointNameChoice = CRL_DIST_POINT_FULL_NAME;
        return dec_alt_name(a, &choice, &out->FullName);
    }
    if (choice.tag == ASN_CONSTRUCTED_CONTEXT(1)) {
        out->dwDistPointNameChoice = CRL_DIST_POINT_ISSUER_RDN_NAME;
        return 0;
    }
    return CRYPT_E_ASN1_BADTAG;
}

static DWORD dec_context_bool(const der_item *item, BOOL *out)
{
    if (item->length != 1)
        return CRYPT_E_ASN1_CORRUPT;
    *out = item->content[0] != 0;
    return 0;
}

static DWORD dec_dist_point(cr_arena *a, const der_item *item, CRL_DIST_POINT *out)
{
    if (item->tag != ASN_SEQUENCE)
        return CRYPT_E_ASN1_BADTAG;
    der_reader r;
    DerOpen(&r, item);
    memset(out, 0, sizeof(*out));
    while (r.n) {
        der_item field;
        DWORD error = DerRead(&r, &field);
        if (!error) {
            if (field.tag == ASN_CONSTRUCTED_CONTEXT(0))
                error = dec_dist_point_name(a, &field, &out->DistPointName);
            else if (field.tag == ASN_CONTEXT(1))
                error = dec_bits(a, &field, &out->ReasonFlags);
            else if (field.tag == ASN_CONSTRUCTED_CONTEXT(2))
                error = dec_alt_name(a, &field, &out->CRLIssuer);
            else
                error = CRYPT_E_ASN1_BADTAG;
        }
        if (error)
            return error;
    }
    return 0;
}

/* ------------------------------------------------------------------ top level decoders */

typedef DWORD (*decode_fn)(cr_arena *a, const der_item *item, DWORD flags);

#define TOP(type) type *top = ArenaAlloc(a, sizeof(type)), tmp = {}
#define STORE() \
    do {        \
        if (top) \
            *top = tmp; \
    } while (0)

static DWORD d_cert(cr_arena *a, const der_item *item, DWORD flags)
{
    TOP(CERT_SIGNED_CONTENT_INFO);
    if (item->tag != ASN_SEQUENCE)
        return CRYPT_E_ASN1_BADTAG;
    der_reader r;
    der_item tbs, alg, sig;
    DWORD error;
    DerOpen(&r, item);
    if ((error = DerRead(&r, &tbs)) || (error = DerRead(&r, &alg)) || (error = dec_algid(a, &alg, &tmp.SignatureAlgorithm)) ||
        (error = DerExpect(&r, ASN_BITSTRING, &sig)))
        return error;
    dec_blob(a, tbs.start, tbs.total, &tmp.ToBeSigned);
    if (!sig.length || sig.content[0] > 7)
        return CRYPT_E_ASN1_CORRUPT;
    if (flags & CRYPT_DECODE_NO_SIGNATURE_BYTE_REVERSAL_FLAG) {
        CRYPT_DATA_BLOB b;
        dec_blob(a, sig.content + 1, sig.length - 1, &b);
        tmp.Signature.cbData = b.cbData;
        tmp.Signature.pbData = b.pbData;
    } else {
        CRYPT_DATA_BLOB b;
        dec_reversed(a, sig.content + 1, sig.length - 1, &b);
        tmp.Signature.cbData = b.cbData;
        tmp.Signature.pbData = b.pbData;
    }
    tmp.Signature.cUnusedBits = sig.content[0];
    STORE();
    return 0;
}

static DWORD d_cert_info(cr_arena *a, const der_item *item, DWORD flags)
{
    TOP(CERT_INFO);
    DWORD error = dec_cert_info(a, item, &tmp);
    STORE();
    return error;
}

/* a signed list, its to be signed part followed by an algorithm and a bit string, decodes as its first part. */
static DWORD d_crl_info(cr_arena *a, const der_item *item, DWORD flags)
{
    TOP(CRL_INFO);
    der_item tbs = *item;
    if (!(flags & CRYPT_DECODE_TO_BE_SIGNED_FLAG) && item->tag == ASN_SEQUENCE && DerCount(item) == 3) {
        der_reader r;
        der_item first, alg, sig;
        DerOpen(&r, item);
        if (!DerRead(&r, &first) && !DerRead(&r, &alg) && !DerRead(&r, &sig) && sig.tag == ASN_BITSTRING &&
            first.tag == ASN_SEQUENCE)
            tbs = first;
    }
    DWORD error = dec_crl_info(a, &tbs, &tmp);
    STORE();
    return error;
}

static DWORD d_crl_dist_points(cr_arena *a, const der_item *item, DWORD flags)
{
    TOP(CRL_DIST_POINTS_INFO);
    if (item->tag != ASN_SEQUENCE)
        return CRYPT_E_ASN1_BADTAG;
    DWORD n = DerCount(item);
    PCRL_DIST_POINT points = n ? ArenaAlloc(a, n * sizeof(CRL_DIST_POINT)) : nullptr;
    der_reader r;
    DerOpen(&r, item);
    for (DWORD i = 0; i < n; i++) {
        der_item seq;
        CRL_DIST_POINT point;
        DWORD error = DerRead(&r, &seq);
        if (!error)
            error = dec_dist_point(a, &seq, &point);
        if (error)
            return error;
        if (points)
            points[i] = point;
    }
    tmp.cDistPoint = n;
    tmp.rgDistPoint = points;
    STORE();
    return 0;
}

static DWORD d_issuing_dist_point(cr_arena *a, const der_item *item, DWORD flags)
{
    TOP(CRL_ISSUING_DIST_POINT);
    if (item->tag != ASN_SEQUENCE)
        return CRYPT_E_ASN1_BADTAG;
    der_reader r;
    DerOpen(&r, item);
    while (r.n) {
        der_item field;
        DWORD error = DerRead(&r, &field);
        if (!error) {
            switch (field.tag) {
            case ASN_CONSTRUCTED_CONTEXT(0):
                error = dec_dist_point_name(a, &field, &tmp.DistPointName);
                break;
            case ASN_CONTEXT(1):
                error = dec_context_bool(&field, &tmp.fOnlyContainsUserCerts);
                break;
            case ASN_CONTEXT(2):
                error = dec_context_bool(&field, &tmp.fOnlyContainsCACerts);
                break;
            case ASN_CONTEXT(3):
                error = dec_bits(a, &field, &tmp.OnlySomeReasonFlags);
                break;
            case ASN_CONTEXT(4):
                error = dec_context_bool(&field, &tmp.fIndirectCRL);
                break;
            case ASN_CONTEXT(5):
                break;
            default:
                error = CRYPT_E_ASN1_BADTAG;
                break;
            }
        }
        if (error)
            return error;
    }
    STORE();
    return 0;
}

static DWORD d_info_access(cr_arena *a, const der_item *item, DWORD flags)
{
    TOP(CERT_AUTHORITY_INFO_ACCESS);
    if (item->tag != ASN_SEQUENCE)
        return CRYPT_E_ASN1_BADTAG;
    DWORD n = DerCount(item);
    PCERT_ACCESS_DESCRIPTION list = n ? ArenaAlloc(a, n * sizeof(CERT_ACCESS_DESCRIPTION)) : nullptr;
    der_reader r;
    DerOpen(&r, item);
    for (DWORD i = 0; i < n; i++) {
        der_item seq, oid, location;
        der_reader fields;
        CERT_ACCESS_DESCRIPTION d = {};
        DWORD error = DerExpect(&r, ASN_SEQUENCE, &seq);
        if (error)
            return error;
        DerOpen(&fields, &seq);
        if ((error = DerExpect(&fields, ASN_OID, &oid)) || (error = dec_oid(a, &oid, &d.pszAccessMethod)) ||
            (error = DerRead(&fields, &location)) || (error = dec_alt_entry(a, &location, &d.AccessLocation)))
            return error;
        if (list)
            list[i] = d;
    }
    tmp.cAccDescr = n;
    tmp.rgAccDescr = list;
    STORE();
    return 0;
}

static DWORD d_extensions(cr_arena *a, const der_item *item, DWORD flags)
{
    TOP(CERT_EXTENSIONS);
    DWORD error = dec_extensions(a, item, &tmp.cExtension, &tmp.rgExtension);
    STORE();
    return error;
}

static DWORD d_name_value(cr_arena *a, const der_item *item, DWORD flags, BOOL unicode)
{
    TOP(CERT_NAME_VALUE);
    DWORD error = dec_value(a, item, unicode, flags, &tmp.dwValueType, &tmp.Value);
    STORE();
    return error;
}

static DWORD d_any_string(cr_arena *a, const der_item *item, DWORD flags)
{
    return d_name_value(a, item, flags, FALSE);
}

static DWORD d_unicode_string(cr_arena *a, const der_item *item, DWORD flags)
{
    return d_name_value(a, item, flags, TRUE);
}

static DWORD d_name(cr_arena *a, const der_item *item, DWORD flags)
{
    TOP(CERT_NAME_INFO);
    DWORD error = dec_name(a, item, FALSE, flags, &tmp);
    STORE();
    return error;
}

static DWORD d_unicode_name(cr_arena *a, const der_item *item, DWORD flags)
{
    TOP(CERT_NAME_INFO);
    DWORD error = dec_name(a, item, TRUE, flags, &tmp);
    STORE();
    return error;
}

static DWORD d_pubkey(cr_arena *a, const der_item *item, DWORD flags)
{
    TOP(CERT_PUBLIC_KEY_INFO);
    DWORD error = dec_pubkey(a, item, &tmp);
    STORE();
    return error;
}

static DWORD d_alt_name(cr_arena *a, const der_item *item, DWORD flags)
{
    TOP(CERT_ALT_NAME_INFO);
    DWORD error = dec_alt_name(a, item, &tmp);
    STORE();
    return error;
}

static DWORD d_basic_constraints2(cr_arena *a, const der_item *item, DWORD flags)
{
    TOP(CERT_BASIC_CONSTRAINTS2_INFO);
    if (item->tag != ASN_SEQUENCE)
        return CRYPT_E_ASN1_BADTAG;
    der_reader r;
    der_item it;
    DWORD error;
    DerOpen(&r, item);
    if (DerPeek(&r, ASN_BOOLEAN)) {
        DerRead(&r, &it);
        if (it.length != 1)
            return CRYPT_E_ASN1_CORRUPT;
        tmp.fCA = it.content[0] != 0;
    }
    if (DerPeek(&r, ASN_INTEGER)) {
        int value;
        DerRead(&r, &it);
        if ((error = DerParseInt(&it, &value)))
            return error;
        tmp.fPathLenConstraint = TRUE;
        tmp.dwPathLenConstraint = (DWORD)value;
    }
    STORE();
    return 0;
}

static DWORD d_basic_constraints(cr_arena *a, const der_item *item, DWORD flags)
{
    TOP(CERT_BASIC_CONSTRAINTS_INFO);
    if (item->tag != ASN_SEQUENCE)
        return CRYPT_E_ASN1_BADTAG;
    der_reader r;
    der_item it;
    DWORD error;
    DerOpen(&r, item);
    if ((error = DerExpect(&r, ASN_BITSTRING, &it)) || (error = dec_bits(a, &it, &tmp.SubjectType)))
        return error;
    if (DerPeek(&r, ASN_INTEGER)) {
        int value;
        DerRead(&r, &it);
        if ((error = DerParseInt(&it, &value)))
            return error;
        tmp.fPathLenConstraint = TRUE;
        tmp.dwPathLenConstraint = (DWORD)value;
    }
    if (DerPeek(&r, ASN_SEQUENCE)) {
        DerRead(&r, &it);
        DWORD n = DerCount(&it);
        CERT_NAME_BLOB *names = n ? ArenaAlloc(a, n * sizeof(CERT_NAME_BLOB)) : nullptr;
        der_reader inner;
        DerOpen(&inner, &it);
        for (DWORD i = 0; i < n; i++) {
            der_item name;
            CERT_NAME_BLOB blob;
            if ((error = DerRead(&inner, &name)))
                return error;
            dec_blob(a, name.start, name.total, &blob);
            if (names)
                names[i] = blob;
        }
        tmp.cSubtreesConstraint = n;
        tmp.rgSubtreesConstraint = names;
    }
    STORE();
    return 0;
}

static DWORD d_bits(cr_arena *a, const der_item *item, DWORD flags)
{
    TOP(CRYPT_BIT_BLOB);
    if (item->tag != ASN_BITSTRING)
        return CRYPT_E_ASN1_BADTAG;
    DWORD error = dec_bits(a, item, &tmp);
    STORE();
    return error;
}

static DWORD d_octets(cr_arena *a, const der_item *item, DWORD flags)
{
    TOP(CRYPT_DATA_BLOB);
    if (item->tag != ASN_OCTETSTRING)
        return CRYPT_E_ASN1_BADTAG;
    dec_blob(a, item->content, item->length, &tmp);
    STORE();
    return 0;
}

static DWORD d_int(cr_arena *a, const der_item *item, DWORD flags)
{
    int *top = ArenaAlloc(a, sizeof(int)), value;
    if (item->tag != ASN_INTEGER)
        return CRYPT_E_ASN1_BADTAG;
    DWORD error = DerParseInt(item, &value);
    if (!error && top)
        *top = value;
    return error;
}

static DWORD d_enumerated(cr_arena *a, const der_item *item, DWORD flags)
{
    int *top = ArenaAlloc(a, sizeof(int)), value;
    if (item->tag != ASN_ENUMERATED)
        return CRYPT_E_ASN1_BADTAG;
    DWORD error = DerParseInt(item, &value);
    if (!error && top)
        *top = value;
    return error;
}

static DWORD d_multi_int(cr_arena *a, const der_item *item, DWORD flags)
{
    TOP(CRYPT_INTEGER_BLOB);
    DWORD error = dec_int_blob(a, item, &tmp, FALSE);
    STORE();
    return error;
}

static DWORD d_multi_uint(cr_arena *a, const der_item *item, DWORD flags)
{
    TOP(CRYPT_UINT_BLOB);
    DWORD error = dec_int_blob(a, item, &tmp, TRUE);
    STORE();
    return error;
}

static DWORD d_time(cr_arena *a, const der_item *item, DWORD flags)
{
    TOP(FILETIME);
    DWORD error = DerParseTime(item, &tmp);
    STORE();
    return error;
}

static DWORD d_utc_time(cr_arena *a, const der_item *item, DWORD flags)
{
    if (item->tag != ASN_UTCTIME)
        return CRYPT_E_ASN1_BADTAG;
    return d_time(a, item, flags);
}

/* a set of sequences, each an object identifier and a set of values. */
static DWORD dec_attributes(cr_arena *a, const der_item *item, CRYPT_ATTRIBUTES *out)
{
    DWORD n = DerCount(item);
    CRYPT_ATTRIBUTE *array = n ? ArenaAlloc(a, n * sizeof(*array)) : nullptr;
    der_reader r;
    DerOpen(&r, item);
    for (DWORD i = 0; i < n; i++) {
        der_item attribute, oid, values;
        der_reader inner;
        DWORD error = DerExpect(&r, ASN_SEQUENCE, &attribute);
        if (error)
            return error;
        DerOpen(&inner, &attribute);
        LPSTR name = nullptr;
        if ((error = DerExpect(&inner, ASN_OID, &oid)) || (error = dec_oid(a, &oid, &name)) ||
            (error = DerExpect(&inner, ASN_SET, &values)) || inner.n)
            return error ? error : CRYPT_E_ASN1_CORRUPT;
        DWORD count = DerCount(&values);
        CRYPT_ATTR_BLOB *blobs = count ? ArenaAlloc(a, count * sizeof(*blobs)) : nullptr;
        der_reader list;
        DerOpen(&list, &values);
        for (DWORD j = 0; j < count; j++) {
            der_item value;
            CRYPT_ATTR_BLOB blob;
            if ((error = DerRead(&list, &value)))
                return error;
            dec_blob(a, value.start, value.total, &blob);
            if (blobs)
                blobs[j] = blob;
        }
        if (array) {
            array[i].pszObjId = name;
            array[i].cValue = count;
            array[i].rgValue = blobs;
        }
    }
    out->cAttr = n;
    out->rgAttr = array;
    return 0;
}

/* decodes into CMSG_SIGNER_INFO. */
static DWORD d_signer_info(cr_arena *a, const der_item *item, DWORD flags)
{
    TOP(CMSG_SIGNER_INFO);
    if (item->tag != ASN_SEQUENCE)
        return CRYPT_E_ASN1_BADTAG;
    der_reader r;
    der_item version, sid, issuer, serial, hash, encryption, encrypted, auth = {}, unauth = {};
    DerOpen(&r, item);
    int number;
    DWORD error = DerExpect(&r, ASN_INTEGER, &version);
    if (error || DerParseInt(&version, &number) || (error = DerExpect(&r, ASN_SEQUENCE, &sid)) ||
        (error = DerExpect(&r, ASN_SEQUENCE, &hash)))
        return error ? error : CRYPT_E_ASN1_CORRUPT;
    BOOL have_auth = DerPeek(&r, 0xa0);
    if (have_auth && (error = DerExpect(&r, 0xa0, &auth)))
        return error;
    if ((error = DerExpect(&r, ASN_SEQUENCE, &encryption)) || (error = DerExpect(&r, ASN_OCTETSTRING, &encrypted)))
        return error;
    BOOL have_unauth = r.n && DerPeek(&r, 0xa1);
    if (have_unauth && (error = DerExpect(&r, 0xa1, &unauth)))
        return error;
    if (r.n)
        return CRYPT_E_ASN1_CORRUPT;
    der_reader identity;
    DerOpen(&identity, &sid);
    if ((error = DerExpect(&identity, ASN_SEQUENCE, &issuer)) || (error = DerExpect(&identity, ASN_INTEGER, &serial)) ||
        identity.n)
        return error ? error : CRYPT_E_ASN1_CORRUPT;
    tmp.dwVersion = (DWORD)number;
    dec_blob(a, issuer.start, issuer.total, &tmp.Issuer);
    dec_reversed(a, serial.content, serial.length, &tmp.SerialNumber);
    if ((error = dec_algid(a, &hash, &tmp.HashAlgorithm)) || (error = dec_algid(a, &encryption, &tmp.HashEncryptionAlgorithm)))
        return error;
    dec_blob(a, encrypted.content, encrypted.length, &tmp.EncryptedHash);
    if (have_auth && (error = dec_attributes(a, &auth, &tmp.AuthAttrs)))
        return error;
    if (have_unauth && (error = dec_attributes(a, &unauth, &tmp.UnauthAttrs)))
        return error;
    STORE();
    return 0;
}

/* ContentInfo: the type and the content inside its [0] wrapper, as it is encoded. */
static DWORD d_content_info(cr_arena *a, const der_item *item, DWORD flags)
{
    TOP(CRYPT_CONTENT_INFO);
    if (item->tag != ASN_SEQUENCE)
        return CRYPT_E_ASN1_BADTAG;
    der_reader r;
    der_item oid, wrapper;
    DerOpen(&r, item);
    DWORD error = DerExpect(&r, ASN_OID, &oid);
    if (error || (error = dec_oid(a, &oid, &tmp.pszObjId)))
        return error;
    if (r.n) {
        if ((error = DerExpect(&r, 0xa0, &wrapper)))
            return error;
        der_reader inner;
        der_item content;
        DerOpen(&inner, &wrapper);
        if (inner.n) {
            if ((error = DerRead(&inner, &content)))
                return error;
            dec_blob(a, content.start, content.total, &tmp.Content);
        }
    }
    if (r.n)
        return CRYPT_E_ASN1_CORRUPT;
    STORE();
    return 0;
}

/* the indirect data of an authenticode signature: what is signed, and the digest algorithm and value. */
static DWORD d_spc_indirect(cr_arena *a, const der_item *item, DWORD flags)
{
    TOP(SPC_INDIRECT_DATA_CONTENT);
    if (item->tag != ASN_SEQUENCE)
        return CRYPT_E_ASN1_BADTAG;
    der_reader r;
    der_item data, digest_info, oid, value, algorithm, digest;
    DerOpen(&r, item);
    DWORD error = DerExpect(&r, ASN_SEQUENCE, &data);
    if (error || (error = DerExpect(&r, ASN_SEQUENCE, &digest_info)))
        return error;
    der_reader d;
    DerOpen(&d, &data);
    if ((error = DerExpect(&d, ASN_OID, &oid)) || (error = dec_oid(a, &oid, &tmp.Data.pszObjId)))
        return error;
    if (d.n) {
        if ((error = DerRead(&d, &value)))
            return error;
        dec_blob(a, value.start, value.total, &tmp.Data.Value);
    }
    DerOpen(&d, &digest_info);
    if ((error = DerExpect(&d, ASN_SEQUENCE, &algorithm)) || (error = dec_algid(a, &algorithm, &tmp.DigestAlgorithm)) ||
        (error = DerExpect(&d, ASN_OCTETSTRING, &digest)))
        return error;
    dec_blob(a, digest.content, digest.length, &tmp.Digest);
    STORE();
    return 0;
}

static DWORD d_oid(cr_arena *a, const der_item *item, DWORD flags)
{
    LPSTR *top = ArenaAlloc(a, sizeof(LPSTR)), oid = nullptr;
    DWORD error = dec_oid(a, item, &oid);
    if (!error && top)
        *top = oid;
    return error;
}

static DWORD d_eku(cr_arena *a, const der_item *item, DWORD flags)
{
    TOP(CERT_ENHKEY_USAGE);
    if (item->tag != ASN_SEQUENCE)
        return CRYPT_E_ASN1_BADTAG;
    DWORD n = DerCount(item);
    LPSTR *oids = n ? ArenaAlloc(a, n * sizeof(LPSTR)) : nullptr;
    der_reader r;
    DerOpen(&r, item);
    for (DWORD i = 0; i < n; i++) {
        der_item oid;
        LPSTR s = nullptr;
        DWORD error = DerExpect(&r, ASN_OID, &oid);
        if (error || (error = dec_oid(a, &oid, &s)))
            return error;
        if (oids)
            oids[i] = s;
    }
    tmp.cUsageIdentifier = n;
    tmp.rgpszUsageIdentifier = oids;
    STORE();
    return 0;
}

static DWORD d_authority_key_id2(cr_arena *a, const der_item *item, DWORD flags)
{
    TOP(CERT_AUTHORITY_KEY_ID2_INFO);
    if (item->tag != ASN_SEQUENCE)
        return CRYPT_E_ASN1_BADTAG;
    der_reader r;
    der_item it;
    DWORD error;
    DerOpen(&r, item);
    if (DerPeek(&r, ASN_CONTEXT(0))) {
        DerRead(&r, &it);
        dec_blob(a, it.content, it.length, &tmp.KeyId);
    }
    if (DerPeek(&r, ASN_CONSTRUCTED_CONTEXT(1))) {
        DerRead(&r, &it);
        if ((error = dec_alt_name(a, &it, &tmp.AuthorityCertIssuer)))
            return error;
    }
    if (DerPeek(&r, ASN_CONTEXT(2))) {
        DerRead(&r, &it);
        it.tag = ASN_INTEGER;
        if ((error = dec_int_blob(a, &it, &tmp.AuthorityCertSerialNumber, FALSE)))
            return error;
    }
    STORE();
    return 0;
}

static DWORD d_authority_key_id(cr_arena *a, const der_item *item, DWORD flags)
{
    TOP(CERT_AUTHORITY_KEY_ID_INFO);
    if (item->tag != ASN_SEQUENCE)
        return CRYPT_E_ASN1_BADTAG;
    der_reader r;
    der_item it;
    DWORD error;
    DerOpen(&r, item);
    if (DerPeek(&r, ASN_CONTEXT(0))) {
        DerRead(&r, &it);
        dec_blob(a, it.content, it.length, &tmp.KeyId);
    }
    if (DerPeek(&r, ASN_CONSTRUCTED_CONTEXT(1))) {
        /* an implicitly tagged name: store it as a regular sequence. */
        DerRead(&r, &it);
        BYTE *name = ArenaCopy(a, it.start, it.total);
        if (name)
            name[0] = ASN_SEQUENCE;
        tmp.CertIssuer.cbData = it.total;
        tmp.CertIssuer.pbData = name;
    }
    if (DerPeek(&r, ASN_CONTEXT(2))) {
        DerRead(&r, &it);
        it.tag = ASN_INTEGER;
        if ((error = dec_int_blob(a, &it, &tmp.CertSerialNumber, FALSE)))
            return error;
    }
    STORE();
    return 0;
}

static DWORD d_rsa_public_key(cr_arena *a, const der_item *item, DWORD flags)
{
    if (item->tag != ASN_SEQUENCE)
        return CRYPT_E_ASN1_BADTAG;
    der_reader r;
    der_item modulus, exponent;
    DWORD error;
    DerOpen(&r, item);
    if ((error = DerExpect(&r, ASN_INTEGER, &modulus)) || (error = DerExpect(&r, ASN_INTEGER, &exponent)))
        return error;
    const BYTE *m = modulus.content;
    DWORD mn = modulus.length;
    while (mn > 1 && !m[0]) {
        m++;
        mn--;
    }
    const BYTE *e = exponent.content;
    DWORD en = exponent.length;
    while (en > 1 && !e[0]) {
        e++;
        en--;
    }
    if (!mn || en > 4)
        return CRYPT_E_ASN1_LARGE;
    DWORD pubexp = 0;
    for (DWORD i = 0; i < en; i++)
        pubexp = (pubexp << 8) | e[i];
    BYTE *blob = ArenaAlloc(a, sizeof(BLOBHEADER) + sizeof(RSAPUBKEY) + mn);
    if (blob) {
        BLOBHEADER *header = (BLOBHEADER *)blob;
        RSAPUBKEY *key = (RSAPUBKEY *)(header + 1);
        BYTE *le = (BYTE *)(key + 1);
        header->bType = PUBLICKEYBLOB;
        header->bVersion = CUR_BLOB_VERSION;
        header->reserved = 0;
        header->aiKeyAlg = CALG_RSA_KEYX;
        key->magic = 0x31415352;
        key->bitlen = mn * 8;
        key->pubexp = pubexp;
        for (DWORD i = 0; i < mn; i++)
            le[i] = m[mn - 1 - i];
    }
    return 0;
}

/* PKCS#1 key integers are positive, minimally encoded DER, not signed CAPI integers. */
static BOOL rsa_der_length(const der_item *item)
{
    return !(item->start[1] & 0x80) || (item->length >= 128 && item->start[2] != 0);
}

static DWORD rsa_integer(der_reader *r, der_item *value, BOOL allow_zero)
{
    DWORD error = DerExpect(r, ASN_INTEGER, value);
    if (error)
        return error;
    if (!rsa_der_length(value) || !value->length || (value->content[0] & 0x80))
        return CRYPT_E_ASN1_CORRUPT;
    if (value->length > 1 && !value->content[0]) {
        if (!(value->content[1] & 0x80))
            return CRYPT_E_ASN1_CORRUPT;
        value->content++;
        value->length--;
    }
    if (!allow_zero && value->length == 1 && !value->content[0])
        return CRYPT_E_ASN1_CORRUPT;
    return 0;
}

static BOOL rsa_less(const der_item *a, const der_item *b)
{
    return a->length < b->length ||
           (a->length == b->length && memcmp(a->content, b->content, a->length) < 0);
}

static BOOL rsa_odd(const der_item *value)
{
    return (value->content[value->length - 1] & 1) &&
           (value->length > 1 || value->content[0] > 1);
}

static DWORD rsa_public_values(der_reader *r, der_item *modulus, der_item *exponent)
{
    DWORD error = rsa_integer(r, modulus, FALSE);
    if (!error)
        error = rsa_integer(r, exponent, FALSE);
    if (error)
        return error;
    if (!rsa_odd(modulus) || !rsa_odd(exponent) || !rsa_less(exponent, modulus))
        return CRYPT_E_ASN1_CORRUPT;
    if (modulus->length > MAXDWORD / 8)
        return CRYPT_E_ASN1_LARGE;
    return 0;
}

static DWORD d_cng_rsa_public_key(cr_arena *a, const der_item *item, DWORD flags)
{
    if (item->tag != ASN_SEQUENCE)
        return CRYPT_E_ASN1_BADTAG;
    der_reader r;
    der_item modulus, exponent;
    DerOpen(&r, item);
    DWORD error = rsa_public_values(&r, &modulus, &exponent);
    if (error)
        return error;
    if (r.n)
        return CRYPT_E_ASN1_CORRUPT;
    DWORD mn = modulus.length, en = exponent.length;
    /* A BCRYPT_RSAKEY_BLOB: six DWORDs, then the exponent and the modulus, both big endian. */
    DWORD *blob = ArenaAlloc(a, 6 * sizeof(DWORD) + en + mn);
    if (blob) {
        blob[0] = 0x31415352;
        blob[1] = mn * 8;
        blob[2] = en;
        blob[3] = mn;
        blob[4] = 0;
        blob[5] = 0;
        memcpy(blob + 6, exponent.content, en);
        memcpy((BYTE *)(blob + 6) + en, modulus.content, mn);
    }
    return 0;
}

/* CAPI numbers occupy fixed width little endian slots, including zero padding. */
static void rsa_copy_le(BYTE *out, DWORD width, const der_item *value)
{
    memset(out, 0, width);
    for (DWORD i = 0; i < value->length; i++)
        out[i] = value->content[value->length - 1 - i];
}

static DWORD d_rsa_private_key(cr_arena *a, const der_item *item, DWORD flags)
{
    if (item->tag != ASN_SEQUENCE)
        return CRYPT_E_ASN1_BADTAG;
    if (!rsa_der_length(item))
        return CRYPT_E_ASN1_CORRUPT;
    der_reader r;
    der_item version, modulus, exponent, private_exponent, crt[5];
    DerOpen(&r, item);
    DWORD error = rsa_integer(&r, &version, TRUE);
    if (error)
        return error;
    /* PRIVATEKEYBLOB represents the two-prime form, whose version is zero. */
    if (version.length != 1 || version.content[0])
        return CRYPT_E_ASN1_CORRUPT;
    if ((error = rsa_public_values(&r, &modulus, &exponent)) ||
        (error = rsa_integer(&r, &private_exponent, FALSE)))
        return error;
    for (DWORD i = 0; i < RTL_NUMBER_OF(crt); i++)
        if ((error = rsa_integer(&r, &crt[i], FALSE)))
            return error;
    if (r.n || !(private_exponent.content[private_exponent.length - 1] & 1) ||
        !rsa_less(&private_exponent, &modulus) ||
        !(crt[2].content[crt[2].length - 1] & 1) || !(crt[3].content[crt[3].length - 1] & 1) ||
        !rsa_odd(&crt[0]) || !rsa_odd(&crt[1]) || !rsa_less(&crt[0], &modulus) ||
        !rsa_less(&crt[1], &modulus) ||
        (!rsa_less(&crt[0], &crt[1]) && !rsa_less(&crt[1], &crt[0])) ||
        !rsa_less(&crt[2], &crt[0]) || !rsa_less(&crt[3], &crt[1]) || !rsa_less(&crt[4], &crt[0]))
        return CRYPT_E_ASN1_CORRUPT;
    /* An even modulus width gives the CAPI bitlen/16 fields integral byte widths. */
    DWORD n = (modulus.length + 1) & ~1u, half = n / 2;
    if (n > MAXDWORD / 8 || exponent.length > sizeof(DWORD) || private_exponent.length > n)
        return CRYPT_E_ASN1_LARGE;
    for (DWORD i = 0; i < RTL_NUMBER_OF(crt); i++)
        if (crt[i].length > half)
            return CRYPT_E_ASN1_LARGE;
    ULONGLONG size = sizeof(BLOBHEADER) + sizeof(RSAPUBKEY) + 2ull * n + 5ull * half;
    if (size > MAXDWORD)
        return CRYPT_E_ASN1_LARGE;
    DWORD pubexp = 0;
    for (DWORD i = 0; i < exponent.length; i++)
        pubexp = (pubexp << 8) | exponent.content[i];
    BYTE *blob = ArenaAlloc(a, (SIZE_T)size);
    if (blob) {
        BLOBHEADER header = {PRIVATEKEYBLOB, CUR_BLOB_VERSION, 0, CALG_RSA_KEYX};
        RSAPUBKEY key = {0x32415352, n * 8, pubexp};
        memcpy(blob, &header, sizeof(header));
        memcpy(blob + sizeof(header), &key, sizeof(key));
        BYTE *out = blob + sizeof(header) + sizeof(key);
        rsa_copy_le(out, n, &modulus);
        out += n;
        for (DWORD i = 0; i < RTL_NUMBER_OF(crt); i++, out += half)
            rsa_copy_le(out, half, &crt[i]);
        rsa_copy_le(out, n, &private_exponent);
    }
    return 0;
}

#undef TOP
#undef STORE

static decode_fn decoder_for(LPCSTR type)
{
    if (IS_INTRESOURCE(type)) {
        switch ((ULONG_PTR)type) {
        case 1:
            return d_cert;
        case 2:
            return d_cert_info;
        case 3:
            return d_crl_info;
        case 5:
            return d_extensions;
        case 6:
            return d_any_string;
        case 7:
            return d_name;
        case 8:
            return d_pubkey;
        case 9:
            return d_authority_key_id;
        case 12:
            return d_alt_name;
        case 13:
            return d_basic_constraints;
        case 14:
        case 26:
            return d_bits;
        case 15:
            return d_basic_constraints2;
        case 17:
            return d_utc_time;
        case 19:
            return d_rsa_public_key;
        case 20:
            return d_unicode_name;
        case 24:
            return d_unicode_string;
        case 25:
            return d_octets;
        case 27:
            return d_int;
        case 28:
            return d_multi_int;
        case 29:
            return d_enumerated;
        case 30:
            return d_time;
        case 31:
            return d_authority_key_id2;
        case 32:
            return d_info_access;
        case 35:
            return d_crl_dist_points;
        case 54:
            return d_issuing_dist_point;
        case 36:
            return d_eku;
        case 33:
            return d_content_info;
        case 38:
            return d_multi_uint;
        case 43:
            return d_rsa_private_key;
        case 72:
            return d_cng_rsa_public_key;
        case 73:
            return d_oid;
        case 500:
            return d_signer_info;
        case 2003:
            return d_spc_indirect;
        }
        return nullptr;
    }
    static const struct {
        LPCSTR oid;
        decode_fn fn;
    } map[] = {
        {szOID_AUTHORITY_KEY_IDENTIFIER, d_authority_key_id},
        {szOID_SUBJECT_ALT_NAME, d_alt_name},
        {szOID_ISSUER_ALT_NAME, d_alt_name},
        {szOID_SUBJECT_ALT_NAME2, d_alt_name},
        {szOID_ISSUER_ALT_NAME2, d_alt_name},
        {szOID_BASIC_CONSTRAINTS, d_basic_constraints},
        {szOID_BASIC_CONSTRAINTS2, d_basic_constraints2},
        {szOID_KEY_USAGE, d_bits},
        {szOID_ENHANCED_KEY_USAGE, d_eku},
        {szOID_SUBJECT_KEY_IDENTIFIER, d_octets},
        {szOID_AUTHORITY_KEY_IDENTIFIER2, d_authority_key_id2},
        {szOID_CRL_NUMBER, d_multi_int},
        {szOID_CRL_REASON_CODE, d_enumerated},
        {szOID_DELTA_CRL_INDICATOR, d_multi_int},
        {szOID_CRL_DIST_POINTS, d_crl_dist_points},
        {szOID_FRESHEST_CRL, d_crl_dist_points},
        {szOID_ISSUING_DIST_POINT, d_issuing_dist_point},
        {szOID_AUTHORITY_INFO_ACCESS, d_info_access},
        {"1.3.6.1.5.5.7.1.11", d_info_access},
        {"1.2.840.113549.1.9.5", d_time},
    };
    for (DWORD i = 0; i < RTL_NUMBER_OF(map); i++)
        if (!strcmp(map[i].oid, type))
            return map[i].fn;
    return nullptr;
}

/* measures then decodes; returns 0 and the size, writing only when base is not NULL. */
static DWORD run_decoder(decode_fn fn, const BYTE *data, DWORD size, DWORD flags, BYTE *base, SIZE_T *needed)
{
    der_reader r = {data, size};
    der_item item;
    DWORD error = DerRead(&r, &item);
    if (error)
        return error;
    if (r.n && (fn == d_rsa_private_key || fn == d_cng_rsa_public_key))
        return CRYPT_E_ASN1_CORRUPT;
    cr_arena a = {base, 0};
    if ((error = fn(&a, &item, flags)))
        return error;
    *needed = a.used;
    return 0;
}

void *CrDecodeAlloc(LPCSTR type, const BYTE *data, DWORD size, DWORD flags, DWORD *out_size)
{
    decode_fn fn = decoder_for(type);
    SIZE_T needed;
    if (!fn) {
        SetLastError(ERROR_FILE_NOT_FOUND);
        return nullptr;
    }
    if (!data || !size) {
        SetLastError(CRYPT_E_ASN1_EOD);
        return nullptr;
    }
    DWORD error = run_decoder(fn, data, size, flags, nullptr, &needed);
    if (error) {
        SetLastError(error);
        return nullptr;
    }
    BYTE *p = CrAllocZero(needed ? needed : 1);
    if (!p) {
        SetLastError(ERROR_OUTOFMEMORY);
        return nullptr;
    }
    run_decoder(fn, data, size, flags, p, &needed);
    if (out_size)
        *out_size = (DWORD)needed;
    return p;
}

BOOL WINAPI CryptDecodeObjectEx(DWORD dwCertEncodingType, LPCSTR lpszStructType, const BYTE *pbEncoded,
                                DWORD cbEncoded, DWORD dwFlags, PCRYPT_DECODE_PARA pDecodePara, void *pvStructInfo,
                                DWORD *pcbStructInfo)
{
    if (!pcbStructInfo || (!pvStructInfo && (dwFlags & CRYPT_DECODE_ALLOC_FLAG)))
        return CrFail(ERROR_INVALID_PARAMETER);
    if (!(dwCertEncodingType & (X509_ASN_ENCODING | PKCS_7_ASN_ENCODING)))
        return CrFail(ERROR_FILE_NOT_FOUND);
    decode_fn fn = decoder_for(lpszStructType);
    if (!fn)
        return CrFail(ERROR_FILE_NOT_FOUND);
    if (!pbEncoded || !cbEncoded)
        return CrFail(CRYPT_E_ASN1_EOD);
    SIZE_T needed;
    DWORD error = run_decoder(fn, pbEncoded, cbEncoded, dwFlags, nullptr, &needed);
    if (error)
        return CrFail(error);
    if (needed > MAXDWORD)
        return CrFail(CRYPT_E_ASN1_LARGE);
    BYTE *out;
    if (dwFlags & CRYPT_DECODE_ALLOC_FLAG) {
        if (pDecodePara && pDecodePara->cbSize >= FIELD_OFFSET(CRYPT_DECODE_PARA, pfnFree) && pDecodePara->pfnAlloc)
            out = pDecodePara->pfnAlloc(needed);
        else
            out = LocalAlloc(LMEM_FIXED, needed);
        if (!out)
            return CrFail(ERROR_OUTOFMEMORY);
        *(void **)pvStructInfo = out;
    } else {
        if (!pvStructInfo) {
            *pcbStructInfo = (DWORD)needed;
            return TRUE;
        }
        if (*pcbStructInfo < needed) {
            *pcbStructInfo = (DWORD)needed;
            return CrFail(ERROR_MORE_DATA);
        }
        out = pvStructInfo;
    }
    memset(out, 0, needed);
    run_decoder(fn, pbEncoded, cbEncoded, dwFlags, out, &needed);
    *pcbStructInfo = (DWORD)needed;
    return TRUE;
}

BOOL WINAPI CryptDecodeObject(DWORD dwCertEncodingType, LPCSTR lpszStructType, const BYTE *pbEncoded,
                              DWORD cbEncoded, DWORD dwFlags, void *pvStructInfo, DWORD *pcbStructInfo)
{
    return CryptDecodeObjectEx(dwCertEncodingType, lpszStructType, pbEncoded, cbEncoded,
                               dwFlags & ~CRYPT_DECODE_ALLOC_FLAG, nullptr, pvStructInfo, pcbStructInfo);
}
