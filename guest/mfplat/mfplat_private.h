/* what the parts of mfplat share: the object base and the attribute store every attribute object keeps. */
#ifndef MFPLAT_PRIVATE_H
#define MFPLAT_PRIVATE_H

#define MIME_BUILD_MFPLAT
#include <mime/nt.h>
#include <mime/win32/winbase.h>
#include <mime/win32/mfapi.h>

void *MfAlloc(SIZE_T size);
void *MfAllocZero(SIZE_T size);
void MfFree(void *p);

/* ------------------------------------------------------------------ objects */

typedef struct MfObject MfObject;

/* the interfaces an object answers besides IUnknown, and what its last release frees beyond the object. */
typedef struct MfClass {
    const GUID *const *iids;
    void (*destroy)(MfObject *object);
} MfClass;

typedef struct MfItem {
    GUID key;
    PROPVARIANT value;
} MfItem;

/*
 * the items sorted by the first field of their key, newer ones before older
 * ones sharing it, as windows keeps them. the lock is recursive, so a
 * thread holding LockStore still reads and writes.
 */
typedef struct MfStore {
    CRITICAL_SECTION lock;
    MfItem *items;
    UINT32 count;
    UINT32 capacity;
} MfStore;

struct MfObject {
    const void *vtbl;
    LONG refs;
    const MfClass *cls;
    MfStore store;
};

HRESULT MfObjectInit(MfObject *object, const void *vtbl, const MfClass *cls, UINT32 size);
HRESULT STDMETHODCALLTYPE MfObjectQueryInterface(IMFAttributes *iface, REFIID riid, void **out);
ULONG STDMETHODCALLTYPE MfObjectAddRef(IMFAttributes *iface);
ULONG STDMETHODCALLTYPE MfObjectRelease(IMFAttributes *iface);

static inline MfObject *mf_object(void *iface)
{
    return (MfObject *)iface;
}

/* the attribute methods of every attribute object, for the head of its vtable. */
HRESULT STDMETHODCALLTYPE MfAttrGetItem(IMFAttributes *iface, REFGUID key, PROPVARIANT *value);
HRESULT STDMETHODCALLTYPE MfAttrGetItemType(IMFAttributes *iface, REFGUID key, MF_ATTRIBUTE_TYPE *type);
HRESULT STDMETHODCALLTYPE MfAttrCompareItem(IMFAttributes *iface, REFGUID key, const PROPVARIANT *value,
                                            BOOL *result);
HRESULT STDMETHODCALLTYPE MfAttrCompare(IMFAttributes *iface, IMFAttributes *theirs, MF_ATTRIBUTES_MATCH_TYPE type,
                                        BOOL *result);
HRESULT STDMETHODCALLTYPE MfAttrGetUint32(IMFAttributes *iface, REFGUID key, UINT32 *value);
HRESULT STDMETHODCALLTYPE MfAttrGetUint64(IMFAttributes *iface, REFGUID key, UINT64 *value);
HRESULT STDMETHODCALLTYPE MfAttrGetDouble(IMFAttributes *iface, REFGUID key, double *value);
HRESULT STDMETHODCALLTYPE MfAttrGetGuid(IMFAttributes *iface, REFGUID key, GUID *value);
HRESULT STDMETHODCALLTYPE MfAttrGetStringLength(IMFAttributes *iface, REFGUID key, UINT32 *length);
HRESULT STDMETHODCALLTYPE MfAttrGetString(IMFAttributes *iface, REFGUID key, LPWSTR value, UINT32 size,
                                          UINT32 *length);
HRESULT STDMETHODCALLTYPE MfAttrGetAllocatedString(IMFAttributes *iface, REFGUID key, LPWSTR *value, UINT32 *length);
HRESULT STDMETHODCALLTYPE MfAttrGetBlobSize(IMFAttributes *iface, REFGUID key, UINT32 *size);
HRESULT STDMETHODCALLTYPE MfAttrGetBlob(IMFAttributes *iface, REFGUID key, UINT8 *buffer, UINT32 size, UINT32 *written);
HRESULT STDMETHODCALLTYPE MfAttrGetAllocatedBlob(IMFAttributes *iface, REFGUID key, UINT8 **buffer, UINT32 *size);
HRESULT STDMETHODCALLTYPE MfAttrGetUnknown(IMFAttributes *iface, REFGUID key, REFIID riid, void **object);
HRESULT STDMETHODCALLTYPE MfAttrSetItem(IMFAttributes *iface, REFGUID key, const PROPVARIANT *value);
HRESULT STDMETHODCALLTYPE MfAttrDeleteItem(IMFAttributes *iface, REFGUID key);
HRESULT STDMETHODCALLTYPE MfAttrDeleteAllItems(IMFAttributes *iface);
HRESULT STDMETHODCALLTYPE MfAttrSetUint32(IMFAttributes *iface, REFGUID key, UINT32 value);
HRESULT STDMETHODCALLTYPE MfAttrSetUint64(IMFAttributes *iface, REFGUID key, UINT64 value);
HRESULT STDMETHODCALLTYPE MfAttrSetDouble(IMFAttributes *iface, REFGUID key, double value);
HRESULT STDMETHODCALLTYPE MfAttrSetGuid(IMFAttributes *iface, REFGUID key, REFGUID value);
HRESULT STDMETHODCALLTYPE MfAttrSetString(IMFAttributes *iface, REFGUID key, LPCWSTR value);
HRESULT STDMETHODCALLTYPE MfAttrSetBlob(IMFAttributes *iface, REFGUID key, const UINT8 *buffer, UINT32 size);
HRESULT STDMETHODCALLTYPE MfAttrSetUnknown(IMFAttributes *iface, REFGUID key, IUnknown *object);
HRESULT STDMETHODCALLTYPE MfAttrLockStore(IMFAttributes *iface);
HRESULT STDMETHODCALLTYPE MfAttrUnlockStore(IMFAttributes *iface);
HRESULT STDMETHODCALLTYPE MfAttrGetCount(IMFAttributes *iface, UINT32 *count);
HRESULT STDMETHODCALLTYPE MfAttrGetItemByIndex(IMFAttributes *iface, UINT32 index, GUID *key, PROPVARIANT *value);
HRESULT STDMETHODCALLTYPE MfAttrCopyAllItems(IMFAttributes *iface, IMFAttributes *dest);
#define MF_ATTRIBUTES_VTBL                                                                                             \
    {                                                                                                                  \
        MfObjectQueryInterface, MfObjectAddRef, MfObjectRelease, MfAttrGetItem, MfAttrGetItemType,                     \
        MfAttrCompareItem, MfAttrCompare, MfAttrGetUint32, MfAttrGetUint64, MfAttrGetDouble, MfAttrGetGuid,            \
        MfAttrGetStringLength, MfAttrGetString, MfAttrGetAllocatedString, MfAttrGetBlobSize, MfAttrGetBlob,            \
        MfAttrGetAllocatedBlob, MfAttrGetUnknown, MfAttrSetItem, MfAttrDeleteItem, MfAttrDeleteAllItems,               \
        MfAttrSetUint32, MfAttrSetUint64, MfAttrSetDouble, MfAttrSetGuid, MfAttrSetString, MfAttrSetBlob,              \
        MfAttrSetUnknown, MfAttrLockStore, MfAttrUnlockStore, MfAttrGetCount, MfAttrGetItemByIndex,                    \
        MfAttrCopyAllItems                                                                                             \
    }
extern const IMFAttributesVtbl MfAttributesVtbl;

void MfStoreInit(MfStore *store);
void MfStoreClear(MfStore *store);
void MfStoreFree(MfStore *store);
const PROPVARIANT *MfStoreFind(MfStore *store, REFGUID key);
HRESULT MfStoreCompare(MfStore *ours, IMFAttributes *theirs, MF_ATTRIBUTES_MATCH_TYPE type, const GUID *skip,
                       UINT32 skip_count, BOOL *result);

/* ------------------------------------------------------------------ the parts */

extern int _fltused;
BOOL MfStarted(void);
HRESULT MfCreateMemoryBuffer(DWORD max_length, DWORD alignment_mask, IMFMediaBuffer **out);
void MfWorkQueuesShutdown(void);
HRESULT MfQueueResult(DWORD queue, IMFAsyncResult *result);
HRESULT MfAsyncResultCreate(IUnknown *object, IMFAsyncCallback *callback, IUnknown *state, IMFAsyncResult **out);
/* the callback of a result mfplat made, null for any other result. */
IMFAsyncCallback *MfAsyncResultCallback(IMFAsyncResult *result);
HRESULT MfMediaEventCreate(MediaEventType type, REFGUID extended, HRESULT status, const PROPVARIANT *value,
                           IMFMediaEvent **out);

#endif
