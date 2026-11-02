#ifndef MIME_TRUST_PRIVATE_H
#define MIME_TRUST_PRIVATE_H
#define MIME_BUILD_WINTRUST
#include <mime/win32/wintrust.h>
#include <rtl.h>

typedef struct { const BYTE *data; DWORD size; } WT_BYTES;
typedef struct { WT_BYTES encoded, value; BYTE tag; } WT_DER;
BOOL WtTake(WT_BYTES *input, BYTE tag, WT_DER *item);
BOOL WtOid(WT_DER item, const BYTE *value, DWORD size);
/*
 * digest input assembled by the subject reader: PE bytes excluding signature-related fields, or the signed cabinet
 * bytes.
 */
typedef struct {
    /* the file's mapping, its size, and the start of the file, which holds the headers. the rest is read in views. */
    HANDLE mapping;
    const BYTE *image;
    DWORD size, checksum, directory, certificate;
    const BYTE *digest_input;
    DWORD digest_size;
    /* Windows Installer package bytes, identified by the signature's SIP identifier. */
    BOOL installer;
    /* the signature file of an app package, checked on its own: its digests are the package's to compare. */
    BOOL p7x;
} WT_SUBJECT;
LONG WtVerifyPe(HANDLE mapping, DWORD size, const WINTRUST_DATA *options, PCCERT_CHAIN_CONTEXT *chain);
LONG WtVerifyP7x(const BYTE *bytes, DWORD size, const WINTRUST_DATA *options, PCCERT_CHAIN_CONTEXT *chain);
LONG WtVerifyCab(const BYTE *bytes, DWORD size, const WINTRUST_DATA *options, PCCERT_CHAIN_CONTEXT *chain);
LONG WtVerifyInstaller(const BYTE *bytes, DWORD size, const WINTRUST_DATA *options, PCCERT_CHAIN_CONTEXT *chain);
LONG WtVerifySignedData(WT_BYTES encoded, const WT_SUBJECT *subject, const WINTRUST_DATA *options,
                        PCCERT_CHAIN_CONTEXT *chain);
#endif
