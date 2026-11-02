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

LONG WtVerifyPe(const BYTE *bytes, DWORD size, const WINTRUST_DATA *options, PCCERT_CHAIN_CONTEXT *chain)
{
    if (size < 64 || word(bytes) != 0x5a4d)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    DWORD nt = dword(bytes + 60);
    if (nt > size - 24 || dword(bytes + nt) != 0x4550)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    DWORD optional = nt + 24, optional_size = word(bytes + nt + 20);
    DWORD sections = word(bytes + nt + 6);
    if (optional_size > size - optional || optional_size < 96 || !sections || sections > 96)
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
    if (headers > size || table > headers || sections > (headers - table) / 40 ||
        certificate < headers || certificate > size || certificate_size != size - certificate || certificate_size < 8)
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
    DWORD length = dword(bytes + certificate);
    if (length < 8 || length > certificate_size || word(bytes + certificate + 4) != 0x200 ||
        word(bytes + certificate + 6) != 2)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    if (padding_checked())
        for (DWORD i = length; i < certificate_size; i++)
            if (bytes[certificate + i])
                return TRUST_E_SUBJECT_NOT_TRUSTED;
    WT_SUBJECT subject = {.image = bytes, .size = size, .checksum = optional + 64, .directory = directory,
                          .certificate = certificate};
    return WtVerifySignedData((WT_BYTES){bytes + certificate + 8, length - 8}, &subject, options, chain);
}
