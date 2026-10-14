#ifndef MIME_TRUST_PRIVATE_H
#define MIME_TRUST_PRIVATE_H
#define MIME_BUILD_WINTRUST
#include <mime/win32/wintrust.h>
#include <rtl.h>

typedef struct { const BYTE *data; DWORD size; } WT_BYTES;
typedef struct { WT_BYTES encoded, value; BYTE tag; } WT_DER;
BOOL WtTake(WT_BYTES *input, BYTE tag, WT_DER *item);
BOOL WtOid(WT_DER item, const BYTE *value, DWORD size);
LONG WtVerifyPe(const BYTE *bytes, DWORD size, const WINTRUST_DATA *options);
LONG WtVerifySignedData(WT_BYTES encoded, const BYTE *image, DWORD size, DWORD checksum, DWORD directory,
                       DWORD certificate, const WINTRUST_DATA *options);
#endif
