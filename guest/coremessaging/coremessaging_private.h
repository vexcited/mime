#ifndef MIME_COREMESSAGING_PRIVATE_H
#define MIME_COREMESSAGING_PRIVATE_H

#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/objbase.h>
#include <mime/winrt_object.h>
#include <mime/win32/winrt_async.h>

extern HINSTANCE CmModule;
extern DWORD CmTls;

MIME_DEFINE_GUID(IID_CmMarshal, 0x00000003, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_CmWeakReference, 0x00000037, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_CmWeakReferenceSource, 0x00000038, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                 0x46);
MIME_DEFINE_GUID(IID_IDispatcherQueue, 0x603E88E4, 0xA338, 0x4FFE, 0xA4, 0x57, 0xA5, 0xCF, 0xB9, 0xCE, 0xB8, 0x99);
MIME_DEFINE_GUID(IID_IDispatcherQueue2, 0xC822C647, 0x30EF, 0x506E, 0xBD, 0x1E, 0xA6, 0x47, 0xAE, 0x66, 0x75, 0xFF);
MIME_DEFINE_GUID(IID_IDispatcherQueueController, 0x22F34E66, 0x50DB, 0x4E36, 0xA9, 0x8D, 0x61, 0xC0, 0x1B, 0x38, 0x4D,
                 0x20);
MIME_DEFINE_GUID(IID_IDispatcherQueueStatics, 0xA96D83D7, 0x9371, 0x4517, 0x92, 0x45, 0xD0, 0x82, 0x4A, 0xC1, 0x2C,
                 0x74);
MIME_DEFINE_GUID(IID_IDispatcherQueueControllerStatics, 0x0A6C98E0, 0x5198, 0x49A2, 0xA3, 0x13, 0x3F, 0x70, 0xD1, 0xF1,
                 0x3C, 0x27);
MIME_DEFINE_GUID(IID_IDispatcherQueueTimer, 0x5FEABB1D, 0xA31C, 0x4727, 0xB1, 0xAC, 0x37, 0x45, 0x46, 0x49, 0xD5, 0x6A);
MIME_DEFINE_GUID(IID_IDispatcherQueueShutdownStartingEventArgs, 0xC4724C4C, 0xFF97, 0x40C0, 0xA2, 0x26, 0xCC, 0x0A,
                 0xAA, 0x54, 0x5E, 0x89);
MIME_DEFINE_GUID(IID_IDeferral, 0xD6269732, 0x3B7F, 0x46A7, 0xB4, 0x0B, 0x4F, 0xDC, 0xA2, 0xA2, 0xC6, 0x93);
MIME_DEFINE_GUID(IID_CmClosable, 0x30D5A829, 0x7FA4, 0x4026, 0x83, 0xBB, 0xD7, 0x5B, 0xAE, 0x4E, 0xA9, 0x9E);

#define RPC_E_WRONG_THREAD_ ((HRESULT)0x8001010EL)
#define E_ILLEGAL_STATE_CHANGE_ ((HRESULT)0x8000000DL)
/* what TryEnqueue answers for a handler that is not agile. */
#define CM_E_NOT_AGILE ((HRESULT)0x8000001CL)

/* the threadpool timers of kernel32. */
typedef struct CM_TP_TIMER *PCM_TP_TIMER;
typedef void(CALLBACK *CM_TP_TIMER_CALLBACK)(void *instance, void *context, PCM_TP_TIMER timer);
DECLSPEC_IMPORT PCM_TP_TIMER WINAPI CreateThreadpoolTimer(CM_TP_TIMER_CALLBACK callback, void *context, void *environment);
DECLSPEC_IMPORT void WINAPI SetThreadpoolTimer(PCM_TP_TIMER timer, const FILETIME *due, DWORD period, DWORD window);
DECLSPEC_IMPORT void WINAPI WaitForThreadpoolTimerCallbacks(PCM_TP_TIMER timer, BOOL cancel);
DECLSPEC_IMPORT void WINAPI CloseThreadpoolTimer(PCM_TP_TIMER timer);
DECLSPEC_IMPORT HRESULT WINAPI CoCreateFreeThreadedMarshaler(IUnknown *outer, IUnknown **result);

/* ------------------------------------------------------------------ interfaces */

typedef struct IDispatcherQueueHandler IDispatcherQueueHandler;
#define IDispatcherQueueHandler_METHODS(T)                                                                             \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Invoke)(T * This);
MIME_COM_INTERFACE(IDispatcherQueueHandler)

/* TypedEventHandler of any sender and arguments. */
typedef struct ICmEventHandler ICmEventHandler;
#define ICmEventHandler_METHODS(T)                                                                                     \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Invoke)(T * This, IInspectable * sender, IInspectable * args);
MIME_COM_INTERFACE(ICmEventHandler)

typedef struct IDispatcherQueueTimer IDispatcherQueueTimer;
#define IDispatcherQueueTimer_METHODS(T)                                                                               \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_Interval)(T * This, INT64 * value);                                                 \
    HRESULT(STDMETHODCALLTYPE *put_Interval)(T * This, INT64 value);                                                   \
    HRESULT(STDMETHODCALLTYPE *get_IsRunning)(T * This, BOOLEAN * value);                                              \
    HRESULT(STDMETHODCALLTYPE *get_IsRepeating)(T * This, BOOLEAN * value);                                            \
    HRESULT(STDMETHODCALLTYPE *put_IsRepeating)(T * This, BOOLEAN value);                                              \
    HRESULT(STDMETHODCALLTYPE *Start)(T * This);                                                                       \
    HRESULT(STDMETHODCALLTYPE *Stop)(T * This);                                                                        \
    HRESULT(STDMETHODCALLTYPE *add_Tick)(T * This, ICmEventHandler * handler, EventRegistrationToken * token);         \
    HRESULT(STDMETHODCALLTYPE *remove_Tick)(T * This, EventRegistrationToken token);
MIME_COM_INTERFACE(IDispatcherQueueTimer)

typedef struct IDispatcherQueue IDispatcherQueue;
#define IDispatcherQueue_METHODS(T)                                                                                    \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *CreateTimer)(T * This, IDispatcherQueueTimer * *result);                                \
    HRESULT(STDMETHODCALLTYPE *TryEnqueue)(T * This, IDispatcherQueueHandler * callback, BOOLEAN * result);            \
    HRESULT(STDMETHODCALLTYPE *TryEnqueueWithPriority)(T * This, INT32 priority, IDispatcherQueueHandler * callback,   \
                                                       BOOLEAN *result);                                               \
    HRESULT(STDMETHODCALLTYPE *add_ShutdownStarting)(T * This, ICmEventHandler * handler,                              \
                                                     EventRegistrationToken * token);                                  \
    HRESULT(STDMETHODCALLTYPE *remove_ShutdownStarting)(T * This, EventRegistrationToken token);                       \
    HRESULT(STDMETHODCALLTYPE *add_ShutdownCompleted)(T * This, ICmEventHandler * handler,                             \
                                                      EventRegistrationToken * token);                                 \
    HRESULT(STDMETHODCALLTYPE *remove_ShutdownCompleted)(T * This, EventRegistrationToken token);
MIME_COM_INTERFACE(IDispatcherQueue)

typedef struct IDispatcherQueue2 IDispatcherQueue2;
#define IDispatcherQueue2_METHODS(T)                                                                                   \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_HasThreadAccess)(T * This, BOOLEAN * value);
MIME_COM_INTERFACE(IDispatcherQueue2)

typedef struct IDispatcherQueueController IDispatcherQueueController;
#define IDispatcherQueueController_METHODS(T)                                                                          \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *get_DispatcherQueue)(T * This, IDispatcherQueue * *value);                              \
    HRESULT(STDMETHODCALLTYPE *ShutdownQueueAsync)(T * This, IAsyncAction * *operation);
MIME_COM_INTERFACE(IDispatcherQueueController)

typedef struct IDispatcherQueueStatics IDispatcherQueueStatics;
#define IDispatcherQueueStatics_METHODS(T)                                                                             \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *GetForCurrentThread)(T * This, IDispatcherQueue * *result);
MIME_COM_INTERFACE(IDispatcherQueueStatics)

typedef struct IDispatcherQueueControllerStatics IDispatcherQueueControllerStatics;
#define IDispatcherQueueControllerStatics_METHODS(T)                                                                   \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *CreateOnDedicatedThread)(T * This, IDispatcherQueueController * *result);
MIME_COM_INTERFACE(IDispatcherQueueControllerStatics)

typedef struct IDeferral IDeferral;
#define IDeferral_METHODS(T)                                                                                           \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *Complete)(T * This);
MIME_COM_INTERFACE(IDeferral)

typedef struct ICmClosable ICmClosable;
#define ICmClosable_METHODS(T)                                                                                         \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *Close)(T * This);
MIME_COM_INTERFACE(ICmClosable)

typedef struct IDispatcherQueueShutdownStartingEventArgs IDispatcherQueueShutdownStartingEventArgs;
#define IDispatcherQueueShutdownStartingEventArgs_METHODS(T)                                                           \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *GetDeferral)(T * This, IDeferral * *result);
MIME_COM_INTERFACE(IDispatcherQueueShutdownStartingEventArgs)

typedef struct ICmWeakReference ICmWeakReference;
#define ICmWeakReference_METHODS(T)                                                                                    \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Resolve)(T * This, REFIID riid, IInspectable * *object);
MIME_COM_INTERFACE(ICmWeakReference)

typedef struct ICmWeakReferenceSource ICmWeakReferenceSource;
#define ICmWeakReferenceSource_METHODS(T)                                                                              \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetWeakReference)(T * This, ICmWeakReference * *reference);
MIME_COM_INTERFACE(ICmWeakReferenceSource)

/* ------------------------------------------------------------------ objects */

/*
 * every object is agile with the free threaded marshaler, as WRL makes them, and gives weak references. the weak
 * reference source is the last interface of each class and GetIids hides it.
 */
typedef struct CM_WEAK CM_WEAK;
typedef struct CM_OBJECT {
    WRT_OBJECT Base;
    IUnknown *volatile Ftm;
    CM_WEAK *volatile Weak;
} CM_OBJECT;

extern const ICmWeakReferenceSourceVtbl CmWeakSourceVtbl;

void CmInit(CM_OBJECT *object, const WRT_CLASS *class, const void *const *vtables);
HRESULT CmQuery(WRT_IFACE *self, REFIID riid, void **object);
ULONG CmAddRef(WRT_IFACE *self);
ULONG CmRelease(WRT_IFACE *self);
/* a reference unless the object is already being destroyed. */
BOOL CmTryAddRef(CM_OBJECT *object);

FORCEINLINE IInspectable *CmIface(CM_OBJECT *object)
{
    return (IInspectable *)&object->Base.Ifaces[0];
}

FORCEINLINE CM_OBJECT *CmOwner(void *iface)
{
    return (CM_OBJECT *)((WRT_IFACE *)iface)->Owner;
}

#define CM_INSPECTABLE_THUNKS(T)                                                                                       \
    static HRESULT STDMETHODCALLTYPE T##_QueryInterface(T *This, REFIID riid, void **object)                           \
    {                                                                                                                  \
        return CmQuery((WRT_IFACE *)This, riid, object);                                                               \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE T##_AddRef(T *This)                                                                 \
    {                                                                                                                  \
        return CmAddRef((WRT_IFACE *)This);                                                                            \
    }                                                                                                                  \
    static ULONG STDMETHODCALLTYPE T##_Release(T *This)                                                                \
    {                                                                                                                  \
        return CmRelease((WRT_IFACE *)This);                                                                           \
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
typedef struct CM_EVENT {
    SRWLOCK Lock;
    UINT32 Count, Capacity;
    struct CM_EVENT_ENTRY {
        INT64 Token;
        ICmEventHandler *Handler;
    } *Entries;
} CM_EVENT;

HRESULT CmEventAdd(CM_EVENT *event, ICmEventHandler *handler, EventRegistrationToken *token);
HRESULT CmEventRemove(CM_EVENT *event, EventRegistrationToken token);
void CmEventInvoke(CM_EVENT *event, IInspectable *sender, IInspectable *args);
void CmEventClear(CM_EVENT *event);

/* ------------------------------------------------------------------ the queue */

enum { CM_LOW, CM_NORMAL, CM_HIGH, CM_PRIORITIES };

/* running, ShutdownStarting asked for, waiting for deferrals, and shut: new work is refused. */
enum { CM_RUNNING, CM_STARTING, CM_DEFERRING, CM_SHUT };

typedef struct CM_QUEUE CM_QUEUE;
typedef struct CM_CONTROLLER CM_CONTROLLER;
typedef struct CM_TIMER CM_TIMER;
typedef struct CM_ASYNC CM_ASYNC;

typedef struct CM_WORK {
    struct CM_WORK *Next;
    IDispatcherQueueHandler *Handler;
    void (*Run)(CM_QUEUE *queue, void *context, UINT64 value);
    void *Context;
    UINT64 Value;
} CM_WORK;

struct CM_QUEUE {
    CM_OBJECT Object;
    SRWLOCK Lock;
    HWND Window;
    DWORD Thread;
    BOOL Dedicated;
    BOOL Exit;
    struct {
        CM_WORK *Head, *Tail;
    } Work[CM_PRIORITIES];
    BOOL WakePosted, IdlePosted;
    LONG State;
    volatile LONG Deferrals;
    CM_EVENT Starting, Completed;
    CM_TIMER *Timers;
    CM_ASYNC *ShutdownAsync;
    CM_CONTROLLER *ShutdownController;
};

struct CM_CONTROLLER {
    CM_OBJECT Object;
    CM_QUEUE *Queue;
};

/* queue.c */
HRESULT CmQueueCreate(BOOL dedicated, CM_QUEUE **result);
CM_QUEUE *CmQueueForThread(void);
BOOL CmQueuePush(CM_QUEUE *queue, int priority, CM_WORK *work, BOOL internal);
BOOL CmQueuePost(CM_QUEUE *queue, int priority, void (*run)(CM_QUEUE *, void *, UINT64), void *context, UINT64 value,
                 BOOL internal);
void CmQueueDeferralDone(CM_QUEUE *queue);
HRESULT CmControllerCreate(CM_QUEUE *queue, CM_CONTROLLER **result);
HRESULT CmControllerOnDedicatedThread(INT32 apartment, IDispatcherQueueController **result);

/* timer.c */
HRESULT CmTimerCreate(CM_QUEUE *queue, IDispatcherQueueTimer **result);
/* with the queue lock held. */
void CmTimersStopAll(CM_QUEUE *queue);

/* shutdown.c */
HRESULT CmAsyncCreate(CM_ASYNC **result);
void CmAsyncComplete(CM_ASYNC *async);
IAsyncAction *CmAsyncIface(CM_ASYNC *async);
void CmShutdownStarting(CM_QUEUE *queue, void *context, UINT64 value);

#endif
