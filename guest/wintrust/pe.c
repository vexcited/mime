#include "trust_private.h"

static WORD word(const BYTE *p)
{
    return p[0] | (WORD)p[1] << 8;
}

static DWORD dword(const BYTE *p)
{
    return word(p) | (DWORD)word(p + 2) << 16;
}

/* imagehlp looks at the zeros after the signature only when the machine asks for it, CVE-2013-3900. */
static BOOL padding_checked(void)
{
    DWORD value = 0, size = sizeof(value);
    return RegGetValueW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Cryptography\\Wintrust\\Config", L"EnableCertPaddingCheck",
                        RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS && value;
}

/* the headers of a file are read from its first megabyte, the certificate table from a view of its own. */
#define HEAD_LIMIT 0x100000
#define TABLE_LIMIT 0x1000000

static LONG verify_views(HANDLE mapping, const BYTE *bytes, DWORD head, DWORD size, const WINTRUST_DATA *options,
                         PCCERT_CHAIN_CONTEXT *chain);

LONG WtVerifyPe(HANDLE mapping, DWORD size, const WINTRUST_DATA *options, PCCERT_CHAIN_CONTEXT *chain)
{
    DWORD head = size < HEAD_LIMIT ? size : HEAD_LIMIT;
    const BYTE *bytes = head ? MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, head) : nullptr;
    if (!bytes)
        return head ? HRESULT_FROM_WIN32(GetLastError()) : TRUST_E_SUBJECT_FORM_UNKNOWN;
    LONG result = verify_views(mapping, bytes, head, size, options, chain);
    UnmapViewOfFile(bytes);
    return result;
}

static LONG verify_views(HANDLE mapping, const BYTE *bytes, DWORD head, DWORD size, const WINTRUST_DATA *options,
                         PCCERT_CHAIN_CONTEXT *chain)
{
    if (head < 64 || word(bytes) != 0x5a4d)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    DWORD nt = dword(bytes + 60);
    if (nt > head - 24 || dword(bytes + nt) != 0x4550)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    DWORD optional = nt + 24, optional_size = word(bytes + nt + 20);
    DWORD sections = word(bytes + nt + 6);
    if (optional_size > head - optional || optional_size < 96 || !sections || sections > 96)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    WORD magic = word(bytes + optional);
    DWORD directories = magic == 0x10b ? 96 : magic == 0x20b ? 112 : 0;
    if (!directories || optional_size < directories)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    DWORD count = dword(bytes + optional + directories - 4);
    if (count <= 4)
        return TRUST_E_NOSIGNATURE;
    if (optional_size < directories + 40)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    DWORD directory = optional + directories + 32;
    DWORD certificate = dword(bytes + directory), certificate_size = dword(bytes + directory + 4);
    if (!certificate && !certificate_size)
        return TRUST_E_NOSIGNATURE;
    DWORD headers = dword(bytes + optional + 60), table = optional + optional_size;
    if (headers > head || table > headers || sections > (headers - table) / 40 ||
        certificate < headers || certificate > size || certificate_size != size - certificate ||
        certificate_size < 8 || certificate_size > TABLE_LIMIT)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    struct { DWORD offset, size; } sorted[96];
    DWORD n = 0;
    for (DWORD i = 0; i < sections; i++) {
        const BYTE *section = bytes + table + i * 40;
        DWORD length = dword(section + 16), offset = dword(section + 20);
        if (!length)
            continue;
        if (offset < headers || offset > certificate || length > certificate - offset)
            return TRUST_E_SUBJECT_FORM_UNKNOWN;
        DWORD at = n++;
        while (at && sorted[at - 1].offset > offset) {
            sorted[at] = sorted[at - 1];
            at--;
        }
        sorted[at].offset = offset;
        sorted[at].size = length;
    }
    DWORD end = headers;
    for (DWORD i = 0; i < n; i++) {
        /* noncontiguous layouts need the general section hashing provider. */
        if (sorted[i].offset != end)
            return TRUST_E_SUBJECT_FORM_UNKNOWN;
        end += sorted[i].size;
    }
    /* the table is mapped from an allocation boundary, which comes before it. */
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    DWORD start = certificate & ~(info.dwAllocationGranularity - 1);
    const BYTE *view = MapViewOfFile(mapping, FILE_MAP_READ, 0, start, size - start);
    if (!view)
        return HRESULT_FROM_WIN32(GetLastError());
    const BYTE *table_bytes = view + (certificate - start);
    DWORD length = dword(table_bytes);
    LONG result = TRUST_E_SUBJECT_FORM_UNKNOWN;
    if (length >= 8 && length <= certificate_size && word(table_bytes + 4) == 0x200 && word(table_bytes + 6) == 2) {
        result = 0;
        if (padding_checked())
            for (DWORD i = length; i < certificate_size; i++)
                if (table_bytes[i])
                    result = TRUST_E_SUBJECT_NOT_TRUSTED;
        if (!result) {
            WT_SUBJECT subject = {.mapping = mapping, .image = bytes, .size = size, .checksum = optional + 64,
                                  .directory = directory, .certificate = certificate};
            result = WtVerifySignedData((WT_BYTES){table_bytes + 8, length - 8}, &subject, options, chain);
        }
    }
    UnmapViewOfFile(view);
    return result;
}
