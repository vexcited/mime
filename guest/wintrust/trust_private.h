#ifndef MIME_TRUST_PRIVATE_H
#define MIME_TRUST_PRIVATE_H
#define MIME_BUILD_WINTRUST
#include <mime/win32/wintrust.h>
#include <rtl.h>

typedef struct { const BYTE *data; DWORD size; } WT_BYTES;
typedef struct { WT_BYTES encoded, value; BYTE tag; } WT_DER;
BOOL WtTake(WT_BYTES *input, BYTE tag, WT_DER *item);
BOOL WtOid(WT_DER item, const BYTE *value, DWORD size);
/* what a signature covers: the image of a portable executable without the fields the signature is kept in, or the
   bytes a cabinet's digest is made of, which the subject's reader has put together. */
typedef struct {
    const BYTE *image;
    DWORD size, checksum, directory, certificate;
    const BYTE *digest_input;
    DWORD digest_size;
} WT_SUBJECT;
LONG WtVerifyPe(const BYTE *bytes, DWORD size, const WINTRUST_DATA *options, PCCERT_CHAIN_CONTEXT *chain);
LONG WtVerifyCab(const BYTE *bytes, DWORD size, const WINTRUST_DATA *options, PCCERT_CHAIN_CONTEXT *chain);
LONG WtVerifySignedData(WT_BYTES encoded, const WT_SUBJECT *subject, const WINTRUST_DATA *options,
                        PCCERT_CHAIN_CONTEXT *chain);
#endif
