#ifndef MIME_LIB_WINRT_H
#define MIME_LIB_WINRT_H

#include <mime/win32/objbase.h>
#include <mime/winrt_object.h>
#include <mime/win32/winrt_async.h>

MIME_DEFINE_GUID(IID_WrtMarshal, 0x00000003, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IWeakReference, 0x00000037, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IWeakReferenceSource, 0x00000038, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x46);

#define RO_E_CLOSED_ ((HRESULT)0x80000013L)

typedef struct IWeakReference IWeakReference;
#define IWeakReference_METHODS(T)                                                                                      \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Resolve)(T * This, REFIID riid, IInspectable * *object);
MIME_COM_INTERFACE(IWeakReference)

typedef struct IWeakReferenceSource IWeakReferenceSource;
#define IWeakReferenceSource_METHODS(T)                                                                                \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetWeakReference)(T * This, IWeakReference * *reference);
MIME_COM_INTERFACE(IWeakReferenceSource)

/* TypedEventHandler and EventHandler of any sender and arguments. */
typedef struct IWrtEventHandler IWrtEventHandler;
#define IWrtEventHandler_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Invoke)(T * This, IInspectable * sender, IInspectable * args);
MIME_COM_INTERFACE(IWrtEventHandler)

/*
 * an object agile through the free threaded marshaler, as WRL makes them. when the class lists IWeakReferenceSource
 * its interface takes WrtWeakSourceVtbl and hands out weak references.
 */
typedef struct WRT_WEAK WRT_WEAK;
typedef struct WRT_AGILE {
    WRT_OBJECT Base;
    IUnknown *volatile Ftm;
    WRT_WEAK *volatile Weak;
} WRT_AGILE;

extern const IWeakReferenceSourceVtbl WrtWeakSourceVtbl;

void WrtAgileInit(WRT_AGILE *object, const WRT_CLASS *class, const void *const *vtables);
HRESULT WrtAgileQuery(WRT_IFACE *self, REFIID riid, void **object);
ULONG WrtAgileAddRef(WRT_IFACE *self);
ULONG WrtAgileRelease(WRT_IFACE *self);
/* a reference unless the object is already being destroyed. */
BOOL WrtAgileTryAddRef(WRT_AGILE *object);

FORCEINLINE IInspectable *WrtAgileIface(WRT_AGILE *object)
{
    return (IInspectable *)&object->Base.Ifaces[0];
}

FORCEINLINE WRT_AGILE *WrtAgileOf(void *iface)
{
    return (WRT_AGILE *)((WRT_IFACE *)iface)->Owner;
}

#define WRT_AGILE_THUNKS(T)                                                                                            \
    static HRESULT STDMETHODCALLTYPE T##_QueryInterface(T *This, REFIID riid, void **object)                           \
    {                                                                                                                  \
        return WrtAgileQuery((WRT_IFACE *)This, riid, object);                                                         \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE T##_AddRef(T *This)                                                                 \
    {                                                                                                                  \
        return WrtAgileAddRef((WRT_IFACE *)This);                                                                      \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE T##_Release(T *This)                                                                \
    {                                                                                                                  \
        return WrtAgileRelease((WRT_IFACE *)This);                                                                     \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE T##_GetIids(T *This, ULONG *count, IID **iids)                                    \
    {                                                                                                                  \
        return WrtGetIids((WRT_IFACE *)This, count, iids);                                                             \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE T##_GetRuntimeClassName(T *This, HSTRING *name)                                   \
    {                                                                                                                  \
        return WrtGetRuntimeClassName((WRT_IFACE *)This, name);                                                        \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE T##_GetTrustLevel(T *This, TrustLevel *level)                                     \
    {                                                                                                                  \
        return WrtGetTrustLevel((WRT_IFACE *)This, level);                                                             \
    }

/* the handlers of an event, called in the order they were added. */
typedef struct WRT_EVENT {
    SRWLOCK Lock;
    UINT32 Count, Capacity;
    struct WRT_EVENT_ENTRY {
        INT64 Token;
        IWrtEventHandler *Handler;
    } *Entries;
} WRT_EVENT;

HRESULT WrtEventAdd(WRT_EVENT *event, IWrtEventHandler *handler, EventRegistrationToken *token);
HRESULT WrtEventRemove(WRT_EVENT *event, EventRegistrationToken token);
UINT32 WrtEventCount(WRT_EVENT *event);
void WrtEventInvoke(WRT_EVENT *event, IInspectable *sender, IInspectable *args);
void WrtEventClear(WRT_EVENT *event);

/*
 * asynchronous operations that are over when they are handed out, agile like the system's. the type tells the
 * parameterized iid of the operation, its class name, whether it reports progress and how its result is kept.
 */
typedef enum WRT_RESULT_KIND {
    WRT_RESULT_NONE,
    WRT_RESULT_BOOL,
    WRT_RESULT_UINT32,
    WRT_RESULT_OBJECT,
} WRT_RESULT_KIND;

typedef struct WRT_ASYNC_TYPE {
    LPCWSTR Name;
    const IID *Iid;
    WRT_RESULT_KIND Result;
    BOOL Progress;
} WRT_ASYNC_TYPE;

/* the object, when there is one, is taken over even on failure. */
HRESULT WrtAsyncCompleted(const WRT_ASYNC_TYPE *type, HRESULT error, UINT32 value, IUnknown *object,
                          void **operation);

#endif
