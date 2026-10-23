#include "trust_private.h"

/*
 * a cabinet signature follows its data. the reserved header gives the prefix and description lengths; the 16-byte
 * description starts with signature offset and size. the offset equals the cabinet header's size and the signature
 * extends to EOF. the digest covers the cabinet signature and size, header fields after the reserved fields, the
 * reserved prefix and description lengths, neighboring cabinet names, and all data from the header's end to the
 * signature.
 */
static WORD word(const BYTE *p)
{
    return p[0] | (WORD)p[1] << 8;
}

static DWORD dword(const BYTE *p)
{
    return word(p) | (DWORD)word(p + 2) << 16;
}

LONG WtVerifyCab(const BYTE *bytes, DWORD size, const WINTRUST_DATA *options, PCCERT_CHAIN_CONTEXT *chain)
{
    if (size < 36 || dword(bytes) != 0x4643534d)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    DWORD cabinet = dword(bytes + 8);
    WORD flags = word(bytes + 30);
    if (!(flags & 4))
        return TRUST_E_NOSIGNATURE;
    if (size < 40)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    DWORD reserve = word(bytes + 36);
    /* no room in the data blocks, which the digest does not take into account. */
    if (bytes[39])
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    if (!reserve)
        return TRUST_E_NOSIGNATURE;
    if (reserve < 4 || reserve > size - 40)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    const BYTE *area = bytes + 40;
    DWORD before = word(area), description = word(area + 2);
    if (before + description + 4 > reserve)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    if (!description)
        return TRUST_E_NOSIGNATURE;
    /* signing tools place the signature after the data, not inside the reserved space. */
    if (description != 16)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    DWORD offset = dword(area + 4 + before), length = dword(area + 8 + before);
    if (!length)
        return TRUST_E_NOSIGNATURE;
    /* the strings of the neighbours in a set, a name and a disk label each. */
    DWORD at = 40 + reserve, names = 0;
    DWORD strings = (flags & 1 ? 2 : 0) + (flags & 2 ? 2 : 0);
    while (strings) {
        if (at + names >= size)
            return TRUST_E_SUBJECT_FORM_UNKNOWN;
        if (bytes[at + names++] == 0)
            strings--;
    }
    DWORD header = at + names;
    if (header > cabinet || offset != cabinet || offset >= size || length != size - offset)
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    DWORD input_size = 4 + 28 + 2 + before + names + (offset - header);
    BYTE *input = LocalAlloc(LMEM_FIXED, input_size);
    if (!input)
        return E_OUTOFMEMORY;
    BYTE *out = input;
    memcpy(out, bytes, 4);
    out += 4;
    memcpy(out, bytes + 8, 28);
    out += 28;
    memcpy(out, area, 2);
    out += 2;
    memcpy(out, area + 4, before);
    out += before;
    memcpy(out, bytes + at, names);
    out += names;
    memcpy(out, bytes + header, offset - header);
    WT_SUBJECT subject = {.digest_input = input, .digest_size = input_size};
    LONG result = WtVerifySignedData((WT_BYTES){bytes + offset, length}, &subject, options, chain);
    LocalFree(input);
    return result;
}
