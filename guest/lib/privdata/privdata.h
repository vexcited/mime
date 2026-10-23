/*
 * directx object private data: bytes or an interface stored under a guid by
 * SetPrivateData and SetPrivateDataInterface.
 */
#ifndef MIME_LIB_PRIVDATA_H
#define MIME_LIB_PRIVDATA_H

#include <mime/win32/objbase.h>

typedef struct MimePrivateEntry MimePrivateEntry;

typedef struct MimePrivateData {
    SRWLOCK lock;
    MimePrivateEntry *first;
} MimePrivateData;

HRESULT MimePrivateSet(MimePrivateData *data, REFGUID guid, UINT size, const void *bytes);
HRESULT MimePrivateSetInterface(MimePrivateData *data, REFGUID guid, const IUnknown *unknown);
HRESULT MimePrivateGet(MimePrivateData *data, REFGUID guid, UINT *size, void *bytes);
void MimePrivateClear(MimePrivateData *data);

#endif
