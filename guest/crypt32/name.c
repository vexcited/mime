/*
 * x.500 names as text: CertNameToStr, CertStrToName, CertGetNameString and
 * CertRDNValueToStr. the separators, quoting rules and keys follow
 * crypt32.dll of windows 11.
 */
#include "crypt32_private.h"

/*
 * counts and stores characters like crypt32 does: one place stays for the
 * terminator, and once only it is left the count stops growing, so a
 * truncated result reports the length actually written.
 */
typedef struct {
    WCHAR *buffer;
    DWORD remaining;
    DWORD count;
} wsink;

static void put(wsink *s, WCHAR c)
{
    if (s->remaining == 1)
        return;
    if (s->remaining) {
        *s->buffer++ = c;
        s->remaining--;
    }
    s->count++;
}

static void put_str(wsink *s, LPCWSTR text, BOOL double_quotes)
{
    for (; *text; text++) {
        if (double_quotes && *text == '"')
            put(s, '"');
        put(s, *text);
    }
}

static void put_ascii(wsink *s, LPCSTR text)
{
    while (*text)
        put(s, (BYTE)*text++);
}

static DWORD finish(wsink *s)
{
    if (s->remaining)
        *s->buffer = 0;
    return s->count + 1;
}

static void put_xml_escaped(wsink *s, LPCWSTR text)
{
    for (; *text; text++) {
        WCHAR c = *text;
        switch (c) {
        case '&':
            put_str(s, L"&amp;", FALSE);
            break;
        case '\'':
            put_str(s, L"&apos;", FALSE);
            break;
        case '"':
            put_str(s, L"&quot;", FALSE);
            break;
        case '<':
            put_str(s, L"&lt;", FALSE);
            break;
        case '>':
            put_str(s, L"&gt;", FALSE);
            break;
        default:
            if ((c >= 0x20 && c <= 0xD7FF) || (c >= 0xE000 && c <= 0xFFFD)) {
                put(s, c);
            } else {
                WCHAR escape[16];
                _snwprintf(escape, RTL_NUMBER_OF(escape), L"&#x%X;", c);
                put_str(s, escape, FALSE);
            }
            break;
        }
    }
}

#define KEY_OPEN 1
#define KEY_OCTET 2
#define KEY_ENCODED 4
#define KEY_NOT_FIRST 8

static BOOL all_alpha(LPCWSTR s)
{
    for (; *s; s++)
        if (!((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z')))
            return FALSE;
    return TRUE;
}

/* writes the key before a value, or the xml tags around it. */
static void put_key(wsink *s, DWORD flags, DWORD str_type, LPCSTR oid)
{
    if (str_type == CERT_SIMPLE_NAME_STR || (str_type != CERT_OID_NAME_STR && str_type != CERT_X500_NAME_STR &&
                                             str_type != CERT_XML_NAME_STR))
        return;
    if (str_type == CERT_XML_NAME_STR)
        put_str(s, (flags & KEY_OPEN) ? L"<" : L"</", FALSE);
    PCCRYPT_OID_INFO info = str_type != CERT_OID_NAME_STR ? CrFindOid(oid, CRYPT_RDN_ATTR_OID_GROUP_ID) : NULL;
    if (info && info->pwszName[0] && (str_type != CERT_XML_NAME_STR || all_alpha(info->pwszName))) {
        if (str_type == CERT_XML_NAME_STR) {
            put_xml_escaped(s, info->pwszName);
            if ((flags & (KEY_OPEN | KEY_NOT_FIRST)) == (KEY_OPEN | KEY_NOT_FIRST))
                put_str(s, L" rDNAttribute=\"true\"", FALSE);
            put(s, '>');
        } else {
            put_str(s, info->pwszName, FALSE);
            put(s, '=');
        }
        return;
    }
    if (str_type == CERT_XML_NAME_STR) {
        if (!(flags & KEY_OPEN)) {
            put_str(s, L"RDN>", FALSE);
            return;
        }
        put_str(s, (flags & KEY_NOT_FIRST) ? L"RDN rDNAttribute=\"true\" oid=\"" : L"RDN oid=\"", FALSE);
    } else if (str_type == CERT_X500_NAME_STR) {
        put_str(s, L"OID.", FALSE);
    }
    put_ascii(s, oid);
    if (str_type == CERT_XML_NAME_STR)
        put_str(s, (flags & KEY_OCTET)     ? L"\" type=\"octet\">"
                   : (flags & KEY_ENCODED) ? L"\" type=\"encoded\">"
                                           : L"\" type=\"string\">",
                FALSE);
    else
        put(s, '=');
}

static void put_hex(wsink *s, const CRYPT_DATA_BLOB *blob, BOOL xml)
{
    static const char digits[] = "0123456789ABCDEF";
    if (!xml)
        put(s, '#');
    for (DWORD i = 0; i < blob->cbData; i++) {
        put(s, digits[blob->pbData[i] >> 4]);
        put(s, digits[blob->pbData[i] & 15]);
    }
}

static BOOL is_space(WCHAR c)
{
    return c == ' ' || (c >= 9 && c <= 13);
}

static BOOL needs_quotes(const WCHAR *value, DWORD n)
{
    if (!n || is_space(value[0]) || is_space(value[n - 1]))
        return TRUE;
    for (DWORD i = 0; i < n; i++)
        if (wcschr(L",+=\"\n<>#;", value[i]))
            return TRUE;
    return FALSE;
}

DWORD CrNameToStr(const CERT_NAME_BLOB *name, DWORD str_type, WCHAR *out, DWORD capacity)
{
    wsink s = {out, out ? capacity : 0, 0};
    LPCWSTR separator = (str_type & CERT_NAME_STR_SEMICOLON_FLAG) ? L"; "
                        : (str_type & CERT_NAME_STR_CRLF_FLAG)    ? L"\r\n"
                                                                  : L", ";
    LPCWSTR plus = (str_type & CERT_NAME_STR_NO_PLUS_FLAG) ? L" " : L" + ";
    DWORD type = str_type & 0xFFFF;
    BOOL xml = type == CERT_XML_NAME_STR;
    DWORD decode_flags = (str_type & CERT_NAME_STR_DISABLE_IE4_UTF8_FLAG) ? CERT_RDN_DISABLE_IE4_UTF8_FLAG : 0;
    DWORD size;
    CERT_NAME_INFO *info = name && name->pbData
                               ? CrDecodeAlloc(X509_UNICODE_NAME, name->pbData, name->cbData, decode_flags, &size)
                               : NULL;
    if (!info)
        return finish(&s);
    if (str_type & CERT_NAME_STR_REVERSE_FLAG)
        for (DWORD i = 0, j = info->cRDN ? info->cRDN - 1 : 0; i < j; i++, j--) {
            CERT_RDN t = info->rgRDN[i];
            info->rgRDN[i] = info->rgRDN[j];
            info->rgRDN[j] = t;
        }
    if (!info->cRDN)
        SetLastError(CRYPT_E_NOT_FOUND);
    for (DWORD i = 0; i < info->cRDN; i++) {
        const CERT_RDN *rdn = &info->rgRDN[i];
        for (DWORD j = 0; j < rdn->cRDNAttr; j++) {
            const CERT_RDN_ATTR *attr = &rdn->rgRDNAttr[j];
            DWORD value_type = attr->dwValueType & CERT_RDN_TYPE_MASK;
            DWORD flags = value_type == CERT_RDN_OCTET_STRING    ? KEY_OCTET
                          : value_type == CERT_RDN_ENCODED_BLOB ? KEY_ENCODED
                                                                 : 0;
            if (j)
                flags |= KEY_NOT_FIRST;
            put_key(&s, flags | KEY_OPEN, type, attr->pszObjId);
            if (flags & (KEY_OCTET | KEY_ENCODED)) {
                put_hex(&s, &attr->Value, xml);
            } else {
                const WCHAR *value = (const WCHAR *)attr->Value.pbData;
                DWORD n = attr->Value.cbData / sizeof(WCHAR);
                if (xml)
                    put_xml_escaped(&s, value);
                else if (!(str_type & CERT_NAME_STR_NO_QUOTING_FLAG) && needs_quotes(value, n)) {
                    put(&s, '"');
                    put_str(&s, value, TRUE);
                    put(&s, '"');
                } else
                    put_str(&s, value, FALSE);
            }
            if (xml) {
                put_key(&s, flags, type, attr->pszObjId);
                if (str_type & CERT_NAME_STR_CRLF_FLAG)
                    put_str(&s, L"\r\n", FALSE);
            } else if (j + 1 < rdn->cRDNAttr) {
                put_str(&s, plus, FALSE);
            }
        }
        if (!xml && i + 1 < info->cRDN)
            put_str(&s, separator, FALSE);
    }
    CrFree(info);
    return finish(&s);
}

DWORD WINAPI CertNameToStrW(DWORD dwCertEncodingType, PCERT_NAME_BLOB pName, DWORD dwStrType, LPWSTR psz, DWORD csz)
{
    return CrNameToStr(pName, dwStrType, psz, psz ? csz : 0);
}

/* converts a wide result for the ansi functions with the same truncation rules. */
static DWORD wide_to_ansi_result(LPCWSTR wide, LPSTR out, DWORD capacity)
{
    int needed = WideCharToMultiByte(CP_ACP, 0, wide, -1, NULL, 0, NULL, NULL);
    if (needed <= 0)
        needed = 1;
    if (!out || !capacity)
        return (DWORD)needed;
    int written = WideCharToMultiByte(CP_ACP, 0, wide, -1, out, (int)capacity, NULL, NULL);
    if (written <= 0) {
        out[capacity - 1] = 0;
        return capacity;
    }
    return (DWORD)written;
}

DWORD WINAPI CertNameToStrA(DWORD dwCertEncodingType, PCERT_NAME_BLOB pName, DWORD dwStrType, LPSTR psz, DWORD csz)
{
    DWORD n = CrNameToStr(pName, dwStrType, NULL, 0);
    WCHAR *wide = CrAlloc(n * sizeof(WCHAR));
    if (!wide) {
        if (psz && csz)
            *psz = 0;
        return 1;
    }
    CrNameToStr(pName, dwStrType, wide, n);
    DWORD result = wide_to_ansi_result(wide, psz, csz);
    CrFree(wide);
    return result;
}

/* ------------------------------------------------------------------ CertStrToName */

typedef struct {
    LPSTR oid;
    WCHAR *value;
    DWORD length;
    BOOL encoded;
    BYTE *raw;
    DWORD raw_size;
    LPCWSTR position;
    BOOL same_rdn;
} parsed_attr;

static int hex_value(WCHAR c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static BOOL str_space(WCHAR c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static DWORD parse_x500(LPCWSTR text, DWORD str_type, parsed_attr **out, DWORD *out_count, LPCWSTR *error_at)
{
    LPCWSTR separators = (str_type & CERT_NAME_STR_SEMICOLON_FLAG) ? L";"
                         : (str_type & CERT_NAME_STR_COMMA_FLAG)   ? L","
                         : (str_type & CERT_NAME_STR_CRLF_FLAG)    ? L"\r\n"
                                                                   : L",;";
    BOOL plus_splits = !(str_type & CERT_NAME_STR_NO_PLUS_FLAG);
    DWORD capacity = 8, count = 0;
    parsed_attr *attrs = CrAllocZero(capacity * sizeof(parsed_attr));
    if (!attrs)
        return ERROR_OUTOFMEMORY;
    LPCWSTR p = text;
    BOOL same_rdn = FALSE;
    for (;;) {
        while (*p && str_space(*p))
            p++;
        if (!*p)
            break;
        LPCWSTR key = p;
        while (*p && *p != '=' && !wcschr(separators, *p) && !(plus_splits && *p == '+'))
            p++;
        if (*p != '=') {
            *error_at = key;
            goto bad;
        }
        LPCWSTR key_end = p;
        while (key_end > key && str_space(key_end[-1]))
            key_end--;
        DWORD key_length = (DWORD)(key_end - key);
        char oid[128];
        LPCWSTR dotted = NULL;
        if (key_length > 4 && !_wcsnicmp(key, L"OID.", 4))
            dotted = key + 4;
        else if (key_length && key[0] >= '0' && key[0] <= '9')
            dotted = key;
        if (dotted) {
            DWORD n = (DWORD)(key_end - dotted);
            if (!n || n >= sizeof(oid)) {
                *error_at = key;
                goto bad;
            }
            for (DWORD i = 0; i < n; i++) {
                if (!((dotted[i] >= '0' && dotted[i] <= '9') || dotted[i] == '.')) {
                    *error_at = key;
                    goto bad;
                }
                oid[i] = (char)dotted[i];
            }
            oid[n] = 0;
        } else {
            WCHAR name[64];
            if (!key_length || key_length >= RTL_NUMBER_OF(name)) {
                *error_at = key;
                goto bad;
            }
            memcpy(name, key, key_length * sizeof(WCHAR));
            name[key_length] = 0;
            PCCRYPT_OID_INFO info = CrFindOidByName(name, CRYPT_RDN_ATTR_OID_GROUP_ID);
            if (!info) {
                *error_at = key;
                goto bad;
            }
            strcpy(oid, info->pszOID);
        }
        p++;
        while (*p && str_space(*p) && !wcschr(separators, *p))
            p++;
        parsed_attr attr = {0};
        attr.position = p;
        attr.same_rdn = same_rdn;
        if (*p == '"') {
            p++;
            LPCWSTR start = p;
            DWORD n = 0;
            for (;;) {
                if (!*p) {
                    *error_at = start - 1;
                    goto bad;
                }
                if (*p == '"') {
                    if (p[1] != '"')
                        break;
                    p++;
                }
                p++;
                n++;
            }
            attr.value = CrAlloc((n + 1) * sizeof(WCHAR));
            if (!attr.value)
                goto oom;
            for (DWORD i = 0; start < p; i++) {
                attr.value[i] = *start;
                start += *start == '"' ? 2 : 1;
            }
            attr.value[n] = 0;
            attr.length = n;
            p++;
            while (*p && str_space(*p) && !wcschr(separators, *p))
                p++;
            if (*p && !wcschr(separators, *p) && !(plus_splits && *p == '+')) {
                *error_at = p;
                CrFree(attr.value);
                goto bad;
            }
        } else {
            LPCWSTR start = p;
            while (*p && !wcschr(separators, *p) && !(plus_splits && *p == '+'))
                p++;
            LPCWSTR end = p;
            while (end > start && str_space(end[-1]))
                end--;
            if (*start == '#') {
                DWORD n = (DWORD)(end - start - 1);
                attr.raw = CrAlloc(n / 2 + 1);
                if (!attr.raw)
                    goto oom;
                DWORD bytes = 0;
                for (DWORD i = 0; i < n; i += 2) {
                    int h = i + 1 < n ? hex_value(start[1 + i]) : -1, l = i + 1 < n ? hex_value(start[2 + i]) : -1;
                    if (h < 0 || l < 0) {
                        *error_at = start;
                        CrFree(attr.raw);
                        goto bad;
                    }
                    attr.raw[bytes++] = (BYTE)(h << 4 | l);
                }
                attr.encoded = TRUE;
                attr.raw_size = bytes;
            } else {
                DWORD n = (DWORD)(end - start);
                attr.value = CrAlloc((n + 1) * sizeof(WCHAR));
                if (!attr.value)
                    goto oom;
                memcpy(attr.value, start, n * sizeof(WCHAR));
                attr.value[n] = 0;
                attr.length = n;
            }
        }
        attr.oid = CrAlloc(strlen(oid) + 1);
        if (!attr.oid) {
            CrFree(attr.value);
            CrFree(attr.raw);
            goto oom;
        }
        strcpy(attr.oid, oid);
        if (count == capacity) {
            parsed_attr *bigger = CrAllocZero(capacity * 2 * sizeof(parsed_attr));
            if (!bigger) {
                CrFree(attr.oid);
                CrFree(attr.value);
                CrFree(attr.raw);
                goto oom;
            }
            memcpy(bigger, attrs, count * sizeof(parsed_attr));
            CrFree(attrs);
            attrs = bigger;
            capacity *= 2;
        }
        attrs[count++] = attr;
        same_rdn = plus_splits && *p == '+';
        if (*p)
            p++;
    }
    *out = attrs;
    *out_count = count;
    return 0;
bad:
    for (DWORD i = 0; i < count; i++) {
        CrFree(attrs[i].oid);
        CrFree(attrs[i].value);
        CrFree(attrs[i].raw);
    }
    CrFree(attrs);
    return CRYPT_E_INVALID_X500_STRING;
oom:
    *error_at = NULL;
    for (DWORD i = 0; i < count; i++) {
        CrFree(attrs[i].oid);
        CrFree(attrs[i].value);
        CrFree(attrs[i].raw);
    }
    CrFree(attrs);
    return ERROR_OUTOFMEMORY;
}

BOOL WINAPI CertStrToNameW(DWORD dwCertEncodingType, LPCWSTR pszX500, DWORD dwStrType, void *pvReserved,
                           BYTE *pbEncoded, DWORD *pcbEncoded, LPCWSTR *ppszError)
{
    if (ppszError)
        *ppszError = NULL;
    DWORD type = dwStrType & 0xFFFF;
    if (type && type != CERT_OID_NAME_STR && type != CERT_X500_NAME_STR)
        return CrFail(E_INVALIDARG);
    DWORD rdn_flags = 0;
    if (dwStrType & CERT_NAME_STR_ENABLE_T61_UNICODE_FLAG)
        rdn_flags |= CERT_RDN_ENABLE_T61_UNICODE_FLAG;
    if (dwStrType & CERT_NAME_STR_ENABLE_UTF8_UNICODE_FLAG)
        rdn_flags |= CERT_RDN_ENABLE_UTF8_UNICODE_FLAG;
    if (dwStrType & CERT_NAME_STR_FORCE_UTF8_DIR_STR_FLAG)
        rdn_flags |= CERT_RDN_ENABLE_UTF8_UNICODE_FLAG | CERT_RDN_FORCE_UTF8_UNICODE_FLAG;
    parsed_attr *attrs = NULL;
    DWORD count = 0;
    LPCWSTR error_at = NULL;
    if (pszX500 && *pszX500) {
        DWORD error = parse_x500(pszX500, dwStrType, &attrs, &count, &error_at);
        if (error) {
            if (ppszError)
                *ppszError = error_at;
            return CrFail(error);
        }
    }
    /* groups the attributes into relative distinguished names. */
    DWORD rdn_count = 0;
    for (DWORD i = 0; i < count; i++)
        if (!attrs[i].same_rdn)
            rdn_count++;
    CERT_RDN *rdns = CrAllocZero((rdn_count ? rdn_count : 1) * sizeof(CERT_RDN));
    CERT_RDN_ATTR *rdn_attrs = CrAllocZero((count ? count : 1) * sizeof(CERT_RDN_ATTR));
    BOOL ok = FALSE;
    if (rdns && rdn_attrs) {
        DWORD r = 0;
        for (DWORD i = 0; i < count; i++) {
            if (!attrs[i].same_rdn || !r) {
                rdns[r].rgRDNAttr = &rdn_attrs[i];
                r++;
            }
            rdns[r - 1].cRDNAttr++;
            rdn_attrs[i].pszObjId = attrs[i].oid;
            if (attrs[i].encoded) {
                rdn_attrs[i].dwValueType = CERT_RDN_ENCODED_BLOB;
                rdn_attrs[i].Value.cbData = attrs[i].raw_size;
                rdn_attrs[i].Value.pbData = attrs[i].raw;
            } else {
                rdn_attrs[i].dwValueType = CERT_RDN_ANY_TYPE | rdn_flags;
                rdn_attrs[i].Value.cbData = attrs[i].length * sizeof(WCHAR);
                rdn_attrs[i].Value.pbData = (BYTE *)attrs[i].value;
            }
        }
        if (dwStrType & CERT_NAME_STR_REVERSE_FLAG)
            for (DWORD i = 0, j = rdn_count ? rdn_count - 1 : 0; i < j; i++, j--) {
                CERT_RDN t = rdns[i];
                rdns[i] = rdns[j];
                rdns[j] = t;
            }
        CERT_NAME_INFO info = {rdn_count, rdns};
        DWORD size;
        BYTE *encoded = CrEncodeAlloc(X509_UNICODE_NAME, &info, 0, &size);
        if (encoded) {
            if (!pcbEncoded) {
                SetLastError(E_INVALIDARG);
            } else if (pbEncoded && *pcbEncoded < size) {
                *pcbEncoded = size;
                SetLastError(ERROR_MORE_DATA);
            } else {
                if (pbEncoded)
                    memcpy(pbEncoded, encoded, size);
                *pcbEncoded = size;
                ok = TRUE;
            }
            CrFree(encoded);
        } else if (ppszError && count) {
            DWORD error = GetLastError();
            /* points at the value the encoder refused. */
            if (error == (DWORD)CRYPT_E_INVALID_PRINTABLE_STRING || error == (DWORD)CRYPT_E_INVALID_IA5_STRING ||
                error == (DWORD)CRYPT_E_INVALID_NUMERIC_STRING || error == (DWORD)CRYPT_E_NOT_CHAR_STRING)
                *ppszError = attrs[0].position;
        }
    } else {
        SetLastError(ERROR_OUTOFMEMORY);
    }
    DWORD error = GetLastError();
    for (DWORD i = 0; i < count; i++) {
        CrFree(attrs[i].oid);
        CrFree(attrs[i].value);
        CrFree(attrs[i].raw);
    }
    CrFree(attrs);
    CrFree(rdns);
    CrFree(rdn_attrs);
    if (!ok)
        SetLastError(error);
    return ok;
}

BOOL WINAPI CertStrToNameA(DWORD dwCertEncodingType, LPCSTR pszX500, DWORD dwStrType, void *pvReserved,
                           BYTE *pbEncoded, DWORD *pcbEncoded, LPCSTR *ppszError)
{
    if (ppszError)
        *ppszError = NULL;
    WCHAR *wide = NULL;
    if (pszX500) {
        int n = MultiByteToWideChar(CP_ACP, 0, pszX500, -1, NULL, 0);
        wide = CrAlloc((n > 0 ? n : 1) * sizeof(WCHAR));
        if (!wide)
            return CrFail(ERROR_OUTOFMEMORY);
        MultiByteToWideChar(CP_ACP, 0, pszX500, -1, wide, n);
    }
    LPCWSTR error_at = NULL;
    BOOL ok = CertStrToNameW(dwCertEncodingType, wide, dwStrType, pvReserved, pbEncoded, pcbEncoded, &error_at);
    if (!ok && error_at && ppszError) {
        /* maps the position back through the ansi text. */
        int bytes = WideCharToMultiByte(CP_ACP, 0, wide, (int)(error_at - wide), NULL, 0, NULL, NULL);
        *ppszError = pszX500 + (bytes > 0 ? bytes : 0);
    }
    DWORD error = GetLastError();
    CrFree(wide);
    SetLastError(error);
    return ok;
}

/* ------------------------------------------------------------------ CertGetNameString */

static CERT_ALT_NAME_INFO *alt_names(PCCERT_CONTEXT cert, BOOL issuer)
{
    const CERT_INFO *info = cert->pCertInfo;
    PCERT_EXTENSION ext = CertFindExtension(issuer ? szOID_ISSUER_ALT_NAME2 : szOID_SUBJECT_ALT_NAME2,
                                            info->cExtension, info->rgExtension);
    if (!ext)
        ext = CertFindExtension(issuer ? szOID_ISSUER_ALT_NAME : szOID_SUBJECT_ALT_NAME, info->cExtension,
                                info->rgExtension);
    if (!ext)
        return NULL;
    DWORD size;
    return CrDecodeAlloc(X509_ALTERNATE_NAME, ext->Value.pbData, ext->Value.cbData, 0, &size);
}

/* writes the first attribute of the name with the oid. */
static BOOL put_attr(wsink *s, const CERT_NAME_BLOB *name, LPCSTR oid, DWORD flags)
{
    DWORD size;
    CERT_NAME_INFO *info = CrDecodeAlloc(X509_UNICODE_NAME, name->pbData, name->cbData,
                                         (flags & CERT_NAME_DISABLE_IE4_UTF8_FLAG) ? CERT_RDN_DISABLE_IE4_UTF8_FLAG
                                                                                   : 0,
                                         &size);
    if (!info)
        return FALSE;
    BOOL found = FALSE;
    for (DWORD i = info->cRDN; i-- && !found;)
        for (DWORD j = 0; j < info->rgRDN[i].cRDNAttr && !found; j++) {
            const CERT_RDN_ATTR *attr = &info->rgRDN[i].rgRDNAttr[j];
            DWORD type = attr->dwValueType & CERT_RDN_TYPE_MASK;
            if (strcmp(attr->pszObjId, oid) || type == CERT_RDN_ENCODED_BLOB || type == CERT_RDN_OCTET_STRING ||
                !attr->Value.cbData)
                continue;
            put_str(s, (const WCHAR *)attr->Value.pbData, FALSE);
            found = TRUE;
        }
    CrFree(info);
    return found;
}

static BOOL put_alt(wsink *s, CERT_ALT_NAME_INFO *names, DWORD choice, BOOL all)
{
    BOOL found = FALSE;
    for (DWORD i = 0; names && i < names->cAltEntry; i++) {
        if (names->rgAltEntry[i].dwAltNameChoice != choice)
            continue;
        put_str(s, names->rgAltEntry[i].pwszDNSName, FALSE);
        found = TRUE;
        if (!all)
            break;
        put(s, 0);
    }
    return found;
}

static BOOL put_upn(wsink *s, CERT_ALT_NAME_INFO *names)
{
    for (DWORD i = 0; names && i < names->cAltEntry; i++) {
        const CERT_ALT_NAME_ENTRY *e = &names->rgAltEntry[i];
        if (e->dwAltNameChoice != CERT_ALT_NAME_OTHER_NAME || strcmp(e->pOtherName->pszObjId, "1.3.6.1.4.1.311.20.2.3"))
            continue;
        der_reader r = {e->pOtherName->Value.pbData, e->pOtherName->Value.cbData};
        der_item value;
        if (DerRead(&r, &value))
            continue;
        DWORD type = CrStringTypeFromTag(value.tag);
        DWORD n = CrDecodeNameValueString(type, value.content, value.length, NULL, 0);
        WCHAR *text = CrAlloc((n + 1) * sizeof(WCHAR));
        if (!text)
            return FALSE;
        CrDecodeNameValueString(type, value.content, value.length, text, n);
        text[n] = 0;
        put_str(s, text, FALSE);
        CrFree(text);
        return TRUE;
    }
    return FALSE;
}

DWORD CrGetNameString(PCCERT_CONTEXT cert, DWORD type, DWORD flags, void *para, WCHAR *out, DWORD capacity)
{
    wsink s = {out, out ? capacity : 0, 0};
    if (!cert)
        return finish(&s);
    BOOL issuer = (flags & CERT_NAME_ISSUER_FLAG) != 0;
    const CERT_NAME_BLOB *name = issuer ? &cert->pCertInfo->Issuer : &cert->pCertInfo->Subject;
    CERT_ALT_NAME_INFO *names = NULL;
    BOOL all = (flags & CERT_NAME_SEARCH_ALL_NAMES_FLAG) != 0;
    BOOL found = FALSE;
    switch (type) {
    case CERT_NAME_EMAIL_TYPE:
        names = alt_names(cert, issuer);
        found = put_alt(&s, names, CERT_ALT_NAME_RFC822_NAME, FALSE) || put_attr(&s, name, szOID_RSA_emailAddr, flags);
        break;
    case CERT_NAME_RDN_TYPE: {
        DWORD str_type = para ? *(const DWORD *)para : CERT_SIMPLE_NAME_STR;
        DWORD n = CrNameToStr(name, str_type, NULL, 0);
        WCHAR *text = CrAlloc(n * sizeof(WCHAR));
        if (text) {
            CrNameToStr(name, str_type, text, n);
            put_str(&s, text, FALSE);
            CrFree(text);
        }
        found = TRUE;
        break;
    }
    case CERT_NAME_ATTR_TYPE:
        found = para && put_attr(&s, name, para, flags);
        if (!found && para && !strcmp(para, szOID_RSA_emailAddr)) {
            names = alt_names(cert, issuer);
            found = put_alt(&s, names, CERT_ALT_NAME_RFC822_NAME, FALSE);
        }
        break;
    case CERT_NAME_FRIENDLY_DISPLAY_TYPE:
        if (!issuer) {
            DWORD size = 0;
            if (CertGetCertificateContextProperty(cert, CERT_FRIENDLY_NAME_PROP_ID, NULL, &size) && size > 2) {
                WCHAR *text = CrAllocZero(size + sizeof(WCHAR));
                if (text && CertGetCertificateContextProperty(cert, CERT_FRIENDLY_NAME_PROP_ID, text, &size)) {
                    put_str(&s, text, FALSE);
                    found = TRUE;
                }
                CrFree(text);
                if (found)
                    break;
            }
        }
        /* fall through */
    case CERT_NAME_SIMPLE_DISPLAY_TYPE: {
        static const LPCSTR order[] = {szOID_COMMON_NAME, szOID_ORGANIZATIONAL_UNIT_NAME, szOID_ORGANIZATION_NAME,
                                       szOID_RSA_emailAddr};
        for (DWORD i = 0; i < RTL_NUMBER_OF(order) && !found; i++)
            found = put_attr(&s, name, order[i], flags);
        if (!found) {
            names = alt_names(cert, issuer);
            found = put_alt(&s, names, CERT_ALT_NAME_RFC822_NAME, FALSE) ||
                    put_alt(&s, names, CERT_ALT_NAME_DNS_NAME, FALSE) || put_alt(&s, names, CERT_ALT_NAME_URL, FALSE);
        }
        break;
    }
    case CERT_NAME_DNS_TYPE:
        names = alt_names(cert, issuer);
        /* with every name asked for, each one ends with its terminator and the list with one more. */
        found = put_alt(&s, names, CERT_ALT_NAME_DNS_NAME, all);
        if (!found)
            found = put_attr(&s, name, szOID_COMMON_NAME, flags);
        break;
    case CERT_NAME_URL_TYPE:
        names = alt_names(cert, issuer);
        found = put_alt(&s, names, CERT_ALT_NAME_URL, FALSE);
        break;
    case CERT_NAME_UPN_TYPE:
        names = alt_names(cert, issuer);
        found = put_upn(&s, names);
        break;
    }
    CrFree(names);
    return finish(&s);
}

DWORD WINAPI CertGetNameStringW(PCCERT_CONTEXT pCertContext, DWORD dwType, DWORD dwFlags, void *pvTypePara,
                                LPWSTR pszNameString, DWORD cchNameString)
{
    return CrGetNameString(pCertContext, dwType, dwFlags, pvTypePara, pszNameString,
                           pszNameString ? cchNameString : 0);
}

DWORD WINAPI CertGetNameStringA(PCCERT_CONTEXT pCertContext, DWORD dwType, DWORD dwFlags, void *pvTypePara,
                                LPSTR pszNameString, DWORD cchNameString)
{
    DWORD n = CrGetNameString(pCertContext, dwType, dwFlags, pvTypePara, NULL, 0);
    WCHAR *wide = CrAlloc(n * sizeof(WCHAR));
    if (!wide) {
        if (pszNameString && cchNameString)
            *pszNameString = 0;
        return 1;
    }
    CrGetNameString(pCertContext, dwType, dwFlags, pvTypePara, wide, n);
    DWORD result;
    if ((dwFlags & CERT_NAME_SEARCH_ALL_NAMES_FLAG) && dwType == CERT_NAME_DNS_TYPE) {
        int needed = WideCharToMultiByte(CP_ACP, 0, wide, (int)n, NULL, 0, NULL, NULL);
        if (!pszNameString || !cchNameString)
            result = (DWORD)needed;
        else
            result = (DWORD)WideCharToMultiByte(CP_ACP, 0, wide, (int)n, pszNameString, (int)cchNameString, NULL,
                                                NULL);
    } else {
        result = wide_to_ansi_result(wide, pszNameString, cchNameString);
    }
    CrFree(wide);
    return result;
}

/* ------------------------------------------------------------------ CertRDNValueToStr */

DWORD WINAPI CertRDNValueToStrW(DWORD dwValueType, PCERT_RDN_VALUE_BLOB pValue, LPWSTR psz, DWORD csz)
{
    wsink s = {psz, psz ? csz : 0, 0};
    DWORD type = dwValueType & CERT_RDN_TYPE_MASK;
    if (!pValue || !pValue->pbData || type == CERT_RDN_ANY_TYPE || type == CERT_RDN_ENCODED_BLOB ||
        type == CERT_RDN_OCTET_STRING)
        return finish(&s);
    if (type == CERT_RDN_BMP_STRING || type == CERT_RDN_UTF8_STRING) {
        const WCHAR *w = (const WCHAR *)pValue->pbData;
        for (DWORD i = 0; i < pValue->cbData / sizeof(WCHAR) && w[i]; i++)
            put(&s, w[i]);
    } else if (type == CERT_RDN_UNIVERSAL_STRING) {
        const DWORD *c = (const DWORD *)pValue->pbData;
        for (DWORD i = 0; i < pValue->cbData / 4 && c[i]; i++)
            put(&s, c[i] > 0xFFFF ? 0xFFFD : (WCHAR)c[i]);
    } else {
        DWORD n = CrDecodeNameValueString(type, pValue->pbData, pValue->cbData, NULL, 0);
        WCHAR *text = CrAlloc((n + 1) * sizeof(WCHAR));
        if (text) {
            CrDecodeNameValueString(type, pValue->pbData, pValue->cbData, text, n);
            for (DWORD i = 0; i < n && text[i]; i++)
                put(&s, text[i]);
            CrFree(text);
        }
    }
    return finish(&s);
}

DWORD WINAPI CertRDNValueToStrA(DWORD dwValueType, PCERT_RDN_VALUE_BLOB pValue, LPSTR psz, DWORD csz)
{
    DWORD n = CertRDNValueToStrW(dwValueType, pValue, NULL, 0);
    WCHAR *wide = CrAlloc(n * sizeof(WCHAR));
    if (!wide) {
        if (psz && csz)
            *psz = 0;
        return 1;
    }
    CertRDNValueToStrW(dwValueType, pValue, wide, n);
    DWORD result = wide_to_ansi_result(wide, psz, csz);
    CrFree(wide);
    return result;
}

BOOL WINAPI CertIsRDNAttrsInCertificateName(DWORD dwCertEncodingType, DWORD dwFlags, PCERT_NAME_BLOB pCertName,
                                            PCERT_RDN pRDN)
{
    if (!pCertName || !pRDN)
        return CrFail(E_INVALIDARG);
    BOOL unicode = dwFlags & CERT_UNICODE_IS_RDN_ATTRS_FLAG;
    DWORD size;
    CERT_NAME_INFO *info = CrDecodeAlloc(unicode ? X509_UNICODE_NAME : X509_NAME, pCertName->pbData,
                                         pCertName->cbData, 0, &size);
    if (!info)
        return FALSE;
    BOOL all = TRUE;
    for (DWORD a = 0; a < pRDN->cRDNAttr && all; a++) {
        const CERT_RDN_ATTR *want = &pRDN->rgRDNAttr[a];
        BOOL found = FALSE;
        for (DWORD i = 0; i < info->cRDN && !found; i++)
            for (DWORD j = 0; j < info->rgRDN[i].cRDNAttr && !found; j++) {
                const CERT_RDN_ATTR *have = &info->rgRDN[i].rgRDNAttr[j];
                if (want->pszObjId && strcmp(want->pszObjId, have->pszObjId))
                    continue;
                DWORD n = want->Value.cbData;
                if (unicode && !n && want->Value.pbData)
                    n = (DWORD)wcslen((const WCHAR *)want->Value.pbData) * sizeof(WCHAR);
                if (n != have->Value.cbData)
                    continue;
                if (unicode && (dwFlags & CERT_CASE_INSENSITIVE_IS_RDN_ATTRS_FLAG))
                    found = !_wcsnicmp((const WCHAR *)want->Value.pbData, (const WCHAR *)have->Value.pbData,
                                       n / sizeof(WCHAR));
                else
                    found = !n || !memcmp(want->Value.pbData, have->Value.pbData, n);
            }
        all = found;
    }
    CrFree(info);
    return all ? TRUE : CrFail(CRYPT_E_NO_MATCH);
}
