#ifndef EVR_PRIVATE_H
#define EVR_PRIVATE_H

#include <mime/nt.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/objbase.h>
#include <mime/win32/mfapi.h>
#include <mime/win32/evr.h>
#include <rtl.h>

/*
 * the IMFAttributes methods of an object that keeps its items in an attribute store of mfplat. store(This) names
 * that store.
 */
#define EVR_ATTRIBUTE_FORWARDERS(prefix, T, store) \
    static HRESULT STDMETHODCALLTYPE prefix##_GetItem(T *This, REFGUID key, PROPVARIANT *value) \
    { \
        return store(This)->lpVtbl->GetItem(store(This), key, value); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_GetItemType(T *This, REFGUID key, MF_ATTRIBUTE_TYPE *type) \
    { \
        return store(This)->lpVtbl->GetItemType(store(This), key, type); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_CompareItem(T *This, REFGUID key, const PROPVARIANT *value, BOOL *result) \
    { \
        return store(This)->lpVtbl->CompareItem(store(This), key, value, result); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_Compare(T *This, IMFAttributes *theirs, MF_ATTRIBUTES_MATCH_TYPE type, BOOL *result) \
    { \
        return store(This)->lpVtbl->Compare(store(This), theirs, type, result); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_GetUINT32(T *This, REFGUID key, UINT32 *value) \
    { \
        return store(This)->lpVtbl->GetUINT32(store(This), key, value); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_GetUINT64(T *This, REFGUID key, UINT64 *value) \
    { \
        return store(This)->lpVtbl->GetUINT64(store(This), key, value); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_GetDouble(T *This, REFGUID key, double *value) \
    { \
        return store(This)->lpVtbl->GetDouble(store(This), key, value); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_GetGUID(T *This, REFGUID key, GUID *value) \
    { \
        return store(This)->lpVtbl->GetGUID(store(This), key, value); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_GetStringLength(T *This, REFGUID key, UINT32 *length) \
    { \
        return store(This)->lpVtbl->GetStringLength(store(This), key, length); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_GetString(T *This, REFGUID key, LPWSTR value, UINT32 size, UINT32 *length) \
    { \
        return store(This)->lpVtbl->GetString(store(This), key, value, size, length); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_GetAllocatedString(T *This, REFGUID key, LPWSTR *value, UINT32 *length) \
    { \
        return store(This)->lpVtbl->GetAllocatedString(store(This), key, value, length); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_GetBlobSize(T *This, REFGUID key, UINT32 *size) \
    { \
        return store(This)->lpVtbl->GetBlobSize(store(This), key, size); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_GetBlob(T *This, REFGUID key, UINT8 *buffer, UINT32 size, UINT32 *written) \
    { \
        return store(This)->lpVtbl->GetBlob(store(This), key, buffer, size, written); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_GetAllocatedBlob(T *This, REFGUID key, UINT8 **buffer, UINT32 *size) \
    { \
        return store(This)->lpVtbl->GetAllocatedBlob(store(This), key, buffer, size); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_GetUnknown(T *This, REFGUID key, REFIID riid, void **object) \
    { \
        return store(This)->lpVtbl->GetUnknown(store(This), key, riid, object); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_SetItem(T *This, REFGUID key, const PROPVARIANT *value) \
    { \
        return store(This)->lpVtbl->SetItem(store(This), key, value); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_DeleteItem(T *This, REFGUID key) \
    { \
        return store(This)->lpVtbl->DeleteItem(store(This), key); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_DeleteAllItems(T *This) \
    { \
        return store(This)->lpVtbl->DeleteAllItems(store(This)); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_SetUINT32(T *This, REFGUID key, UINT32 value) \
    { \
        return store(This)->lpVtbl->SetUINT32(store(This), key, value); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_SetUINT64(T *This, REFGUID key, UINT64 value) \
    { \
        return store(This)->lpVtbl->SetUINT64(store(This), key, value); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_SetDouble(T *This, REFGUID key, double value) \
    { \
        return store(This)->lpVtbl->SetDouble(store(This), key, value); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_SetGUID(T *This, REFGUID key, REFGUID value) \
    { \
        return store(This)->lpVtbl->SetGUID(store(This), key, value); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_SetString(T *This, REFGUID key, LPCWSTR value) \
    { \
        return store(This)->lpVtbl->SetString(store(This), key, value); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_SetBlob(T *This, REFGUID key, const UINT8 *buffer, UINT32 size) \
    { \
        return store(This)->lpVtbl->SetBlob(store(This), key, buffer, size); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_SetUnknown(T *This, REFGUID key, IUnknown *object) \
    { \
        return store(This)->lpVtbl->SetUnknown(store(This), key, object); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_LockStore(T *This) \
    { \
        return store(This)->lpVtbl->LockStore(store(This)); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_UnlockStore(T *This) \
    { \
        return store(This)->lpVtbl->UnlockStore(store(This)); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_GetCount(T *This, UINT32 *count) \
    { \
        return store(This)->lpVtbl->GetCount(store(This), count); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_GetItemByIndex(T *This, UINT32 index, GUID *key, PROPVARIANT *value) \
    { \
        return store(This)->lpVtbl->GetItemByIndex(store(This), index, key, value); \
    } \
    static HRESULT STDMETHODCALLTYPE prefix##_CopyAllItems(T *This, IMFAttributes *dest) \
    { \
        return store(This)->lpVtbl->CopyAllItems(store(This), dest); \
    }

#define EVR_ATTRIBUTE_METHODS(prefix) \
    prefix##_GetItem, prefix##_GetItemType, prefix##_CompareItem, prefix##_Compare, prefix##_GetUINT32, \
    prefix##_GetUINT64, prefix##_GetDouble, prefix##_GetGUID, prefix##_GetStringLength, prefix##_GetString, \
    prefix##_GetAllocatedString, prefix##_GetBlobSize, prefix##_GetBlob, prefix##_GetAllocatedBlob, \
    prefix##_GetUnknown, prefix##_SetItem, prefix##_DeleteItem, prefix##_DeleteAllItems, prefix##_SetUINT32, \
    prefix##_SetUINT64, prefix##_SetDouble, prefix##_SetGUID, prefix##_SetString, prefix##_SetBlob, \
    prefix##_SetUnknown, prefix##_LockStore, prefix##_UnlockStore, prefix##_GetCount, prefix##_GetItemByIndex, \
    prefix##_CopyAllItems

/* the work queue thread of evr that hands released samples back to their allocator. */
HRESULT EvrWorkQueueStart(void);
void EvrWorkQueueStop(void);
HRESULT EvrWorkQueuePut(IMFAsyncCallback *callback, IMFAsyncResult *result);
HRESULT EvrCreateVideoSample(IUnknown *surface, IMFSample **out);
BOOL WINAPI EvrIsFormatYUV(DWORD format);
extern HINSTANCE EvrInstance;
void EvrObjectCreated(void);
void EvrObjectDestroyed(void);

UINT EvrExtendedFormat(const MFVIDEOFORMAT *format, UINT interlace, BOOL output);
void EvrVideoDesc(const MFVIDEOFORMAT *format, DXVA2_VideoDesc *desc);
const MFVIDEOFORMAT *EvrVideoFormat(IUnknown *video);
HRESULT EvrValidateVideoType(IUnknown *video);
void EvrReferenceSourceRect(const MFVIDEOFORMAT *stream, const MFVIDEOFORMAT *reference, RECT *out);
void EvrSourceAndDestRects(const MFVIDEOFORMAT *in, const MFVIDEOFORMAT *out, const MFVIDEOFORMAT *presentation,
                           const MFVideoNormalizedRect *zoom, const MFVideoNormalizedRect *rect, RECT *src, RECT *dst);
/*
 * evr's own interfaces, read from evr.dll: the tearless window presenter the presenters hand their pictures to, and
 * the statistics of both.
 */
typedef struct IEVRTearlessWindowPresenter9 IEVRTearlessWindowPresenter9;
typedef struct IPresenterStatistics IPresenterStatistics;
#define IEVRTearlessWindowPresenter9_METHODS(T) \
    MIME_IUNKNOWN_METHODS(T) \
    HRESULT(STDMETHODCALLTYPE *Present)(T * This, IDirect3DSwapChain9 *chain, IMediaEventSink *sink, const RECT *src, \
                                        const RECT *dst, HWND window, LONGLONG time, DWORD count, BOOL wait, \
                                        IUnknown *sample); \
    void(STDMETHODCALLTYPE *UpdateDestRect)(T * This, const RECT *dst); \
    void(STDMETHODCALLTYPE *SetDeviceChanged)(T * This); \
    HRESULT(STDMETHODCALLTYPE *StartClock)(T * This); \
    HRESULT(STDMETHODCALLTYPE *StopClock)(T * This); \
    HRESULT(STDMETHODCALLTYPE *Pause)(T * This, LONGLONG time); \
    HRESULT(STDMETHODCALLTYPE *Resume)(T * This, LONGLONG time); \
    void(STDMETHODCALLTYPE *Flush)(T * This); \
    HRESULT(STDMETHODCALLTYPE *SetMode)(T * This, DWORD mode); \
    HRESULT(STDMETHODCALLTYPE *SetRenderingPrefs)(T * This, DWORD prefs);
#define IPresenterStatistics_METHODS(T) \
    MIME_IUNKNOWN_METHODS(T) \
    HRESULT(STDMETHODCALLTYPE *GetPresenterStatistics)(T * This, DWORD *stats); \
    HRESULT(STDMETHODCALLTYPE *ResetStatistics)(T * This);
MIME_COM_INTERFACE(IEVRTearlessWindowPresenter9)
MIME_COM_INTERFACE(IPresenterStatistics)
MIME_DEFINE_GUID(IID_IEVRTearlessWindowPresenter9, 0x74AA57F9, 0x6C2D, 0x44AD, 0xB7, 0x43, 0x21, 0xC3, 0x51, 0x2A, 0x6A,
                 0x1F);
MIME_DEFINE_GUID(IID_IPresenterStatistics, 0x60AA1030, 0x904B, 0x4412, 0x88, 0xEE, 0x27, 0x6E, 0x8D, 0x03, 0xD5, 0xA1);
MIME_DEFINE_GUID(CLSID_MFVideoMixer9, 0xE474E05A, 0xAB65, 0x4F6A, 0x82, 0x7C, 0x21, 0x8B, 0x1B, 0xAA, 0xF3, 0x1F);
MIME_DEFINE_GUID(CLSID_MFVideoPresenter9, 0x98455561, 0x5136, 0x4D28, 0xAB, 0x08, 0x4C, 0xEE, 0x40, 0xEA, 0x27, 0x81);
MIME_DEFINE_GUID(CLSID_MFVideoPresenter9FlipMode, 0xF7AFD75B, 0xBF8C, 0x4A11, 0xBD, 0xB9, 0x04, 0xAD, 0x66, 0x18, 0x2F,
                 0x84);
MIME_DEFINE_GUID(CLSID_EVRTearlessWindowPresenter9, 0xA0A7A57B, 0x59B2, 0x4919, 0xA6, 0x94, 0xAD, 0xD0, 0xA5, 0x26,
                 0xC3, 0x73);
/* the statistics block is 61 DWORDs. */
#define EVR_STATISTICS_SIZE 244

HRESULT EvrCreateTearless(IUnknown *outer, REFIID riid, void **out);
HRESULT EvrCreateMixer(IUnknown *outer, REFIID riid, void **out);
HRESULT EvrCreatePresenter(IUnknown *outer, REFIID riid, void **out, BOOL flip);

#endif
