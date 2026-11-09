#include "trust_private.h"

/*
 * the signature file of an app package (AppxSignature.p7x), given to WinVerifyTrust as a blob of the p7x subject: the four
 * bytes of PKCX and then the signed data of an authenticode signature. what is checked is the signature, the chain of
 * its certificate at the time of its timestamp, and the shape of the signed content. the digests it holds are compared with
 * the package by the program that reads the package.
 */
LONG WtVerifyP7x(const BYTE *bytes, DWORD size, const WINTRUST_DATA *options, PCCERT_CHAIN_CONTEXT *chain)
{
    if (size <= 4 || memcmp(bytes, "PKCX", 4))
        return TRUST_E_SUBJECT_FORM_UNKNOWN;
    WT_SUBJECT subject = {.p7x = TRUE};
    return WtVerifySignedData((WT_BYTES){bytes + 4, size - 4}, &subject, options, chain);
}

/* a catalog file: its own signature is what is checked, against the member hashes it lists. */
LONG WtVerifyCatalog(const BYTE *bytes, DWORD size, const WINTRUST_DATA *options, PCCERT_CHAIN_CONTEXT *chain)
{
    WT_SUBJECT subject = {.catalog = TRUE};
    return WtVerifySignedData((WT_BYTES){bytes, size}, &subject, options, chain);
}
