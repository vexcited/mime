/*
 * component object model basics: guids, IUnknown, the task allocator and
 * the com functions of ole32.dll. interfaces are declared the C way, a
 * struct whose first member points to a table of functions.
 */
#ifndef MIME_WIN32_OBJBASE_H
#define MIME_WIN32_OBJBASE_H

#include <mime/win32/winbase.h>

#ifndef WINOLEAPI
#define WINOLEAPI DECLSPEC_IMPORT
#endif

#define STDMETHODCALLTYPE WINAPI
#define STDAPICALLTYPE WINAPI
#define DECLSPEC_SELECTANY __declspec(selectany)

/* defines a guid once per module; the linker merges the copies. */
#define MIME_DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8)                                         \
    const GUID DECLSPEC_SELECTANY name = {l, w1, w2, {b1, b2, b3, b4, b5, b6, b7, b8}}

typedef WCHAR OLECHAR, *LPOLESTR;
typedef const WCHAR *LPCOLESTR;
typedef GUID IID, CLSID, FMTID, *LPGUID, *LPIID, *LPCLSID;
typedef const GUID *REFGUID, *REFIID, *REFCLSID, *REFFMTID, *LPCGUID;
typedef WORD CLIPFORMAT;
typedef LONG SCODE;

FORCEINLINE BOOL IsEqualGUID(REFGUID a, REFGUID b)
{
    if (a->Data1 != b->Data1 || a->Data2 != b->Data2 || a->Data3 != b->Data3)
        return FALSE;
    for (int i = 0; i < 8; i++)
        if (a->Data4[i] != b->Data4[i])
            return FALSE;
    return TRUE;
}
#define IsEqualIID(a, b) IsEqualGUID(a, b)
#define IsEqualCLSID(a, b) IsEqualGUID(a, b)

/* ------------------------------------------------------------------ errors beyond winerror.h */

#ifndef CO_E_CLASSSTRING
#define CO_E_CLASSSTRING ((HRESULT)0x800401F3L)
#endif
#ifndef CO_E_DLLNOTFOUND
#define CO_E_DLLNOTFOUND ((HRESULT)0x800401F8L)
#endif
#ifndef CO_E_ERRORINDLL
#define CO_E_ERRORINDLL ((HRESULT)0x800401F9L)
#endif
#ifndef CO_E_IIDSTRING
#define CO_E_IIDSTRING ((HRESULT)0x800401F4L)
#endif
#ifndef REGDB_E_READREGDB
#define REGDB_E_READREGDB ((HRESULT)0x80040150L)
#endif
#ifndef REGDB_E_IIDNOTREG
#define REGDB_E_IIDNOTREG ((HRESULT)0x80040155L)
#endif
#ifndef CO_S_NOTALLINTERFACES
#define CO_S_NOTALLINTERFACES ((HRESULT)0x00080012L)
#endif
#ifndef CO_E_NOT_SUPPORTED
#define CO_E_NOT_SUPPORTED ((HRESULT)0x80004021L)
#endif
#ifndef CO_E_OBJNOTREG
#define CO_E_OBJNOTREG ((HRESULT)0x800401FBL)
#endif
#ifndef CO_E_OBJISREG
#define CO_E_OBJISREG ((HRESULT)0x800401FCL)
#endif
#ifndef REGDB_E_KEYMISSING
#define REGDB_E_KEYMISSING ((HRESULT)0x80040152L)
#endif
#ifndef REGDB_E_INVALIDVALUE
#define REGDB_E_INVALIDVALUE ((HRESULT)0x80040153L)
#endif
#ifndef MK_E_INVALIDEXTENSION
#define MK_E_INVALIDEXTENSION ((HRESULT)0x800401E6L)
#endif
#ifndef REGDB_E_WRITEREGDB
#define REGDB_E_WRITEREGDB ((HRESULT)0x80040151L)
#endif
#ifndef RPC_E_TOO_LATE
#define RPC_E_TOO_LATE ((HRESULT)0x80010119L)
#endif

/* ------------------------------------------------------------------ class contexts and apartments */

#define CLSCTX_INPROC_SERVER 0x1
#define CLSCTX_INPROC_HANDLER 0x2
#define CLSCTX_LOCAL_SERVER 0x4
#define CLSCTX_INPROC_SERVER16 0x8
#define CLSCTX_REMOTE_SERVER 0x10
#define CLSCTX_INPROC (CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER)
#define CLSCTX_SERVER (CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER)
#define CLSCTX_ALL (CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER)

#define COINIT_MULTITHREADED 0x0
#define COINIT_APARTMENTTHREADED 0x2
#define COINIT_DISABLE_OLE1DDE 0x4
#define COINIT_SPEED_OVER_MEMORY 0x8

typedef enum _APTTYPE {
    APTTYPE_CURRENT = -1,
    APTTYPE_STA = 0,
    APTTYPE_MTA = 1,
    APTTYPE_NA = 2,
    APTTYPE_MAINSTA = 3,
} APTTYPE;

typedef enum _APTTYPEQUALIFIER {
    APTTYPEQUALIFIER_NONE = 0,
    APTTYPEQUALIFIER_IMPLICIT_MTA = 1,
    APTTYPEQUALIFIER_NA_ON_MTA = 2,
    APTTYPEQUALIFIER_NA_ON_STA = 3,
    APTTYPEQUALIFIER_NA_ON_IMPLICIT_MTA = 4,
    APTTYPEQUALIFIER_NA_ON_MAINSTA = 5,
    APTTYPEQUALIFIER_APPLICATION_STA = 6,
} APTTYPEQUALIFIER;

#define MEMCTX_TASK 1

#define REGCLS_SINGLEUSE 0
#define REGCLS_MULTIPLEUSE 1
#define REGCLS_MULTI_SEPARATE 2
#define REGCLS_SUSPENDED 4
#define REGCLS_SURROGATE 8

/* ------------------------------------------------------------------ IUnknown */

typedef struct IUnknown IUnknown, *LPUNKNOWN;
/*
 * each interface's methods after its parent's, as a macro of the
 * interface type, so a vtable lists them in windows' order.
 */
#define MIME_IUNKNOWN_METHODS(T)                                                                                       \
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(T * This, REFIID riid, void **object);                                  \
    ULONG(STDMETHODCALLTYPE *AddRef)(T * This);                                                                        \
    ULONG(STDMETHODCALLTYPE *Release)(T * This);

/* the vtable and object of an interface whose methods macro is I_METHODS. */
#define MIME_COM_INTERFACE(I)                                                                                          \
    typedef struct I##Vtbl {                                                                                           \
        I##_METHODS(I)                                                                                                 \
    } I##Vtbl;                                                                                                         \
    struct I {                                                                                                         \
        const I##Vtbl *lpVtbl;                                                                                         \
    };

typedef struct IUnknownVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IUnknown *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IUnknown *This);
    ULONG(STDMETHODCALLTYPE *Release)(IUnknown *This);
} IUnknownVtbl;
struct IUnknown {
    const IUnknownVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ IClassFactory */

typedef struct IClassFactory IClassFactory, *LPCLASSFACTORY;
typedef struct IClassFactoryVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IClassFactory *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IClassFactory *This);
    ULONG(STDMETHODCALLTYPE *Release)(IClassFactory *This);
    HRESULT(STDMETHODCALLTYPE *CreateInstance)(IClassFactory *This, IUnknown *outer, REFIID riid, void **object);
    HRESULT(STDMETHODCALLTYPE *LockServer)(IClassFactory *This, BOOL lock);
} IClassFactoryVtbl;
struct IClassFactory {
    const IClassFactoryVtbl *lpVtbl;
};

/* ------------------------------------------------------------------ IMalloc */

typedef struct IMalloc IMalloc, *LPMALLOC;
typedef struct IMallocVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IMalloc *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IMalloc *This);
    ULONG(STDMETHODCALLTYPE *Release)(IMalloc *This);
    void *(STDMETHODCALLTYPE *Alloc)(IMalloc *This, SIZE_T size);
    void *(STDMETHODCALLTYPE *Realloc)(IMalloc *This, void *p, SIZE_T size);
    void(STDMETHODCALLTYPE *Free)(IMalloc *This, void *p);
    SIZE_T(STDMETHODCALLTYPE *GetSize)(IMalloc *This, void *p);
    int(STDMETHODCALLTYPE *DidAlloc)(IMalloc *This, void *p);
    void(STDMETHODCALLTYPE *HeapMinimize)(IMalloc *This);
} IMallocVtbl;
struct IMalloc {
    const IMallocVtbl *lpVtbl;
};

typedef struct _COSERVERINFO {
    DWORD dwReserved1;
    LPWSTR pwszName;
    void *pAuthInfo;
    DWORD dwReserved2;
} COSERVERINFO;

typedef struct tagMULTI_QI {
    const IID *pIID;
    IUnknown *pItf;
    HRESULT hr;
} MULTI_QI;

MIME_DEFINE_GUID(GUID_NULL, 0x00000000, 0x0000, 0x0000, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
MIME_DEFINE_GUID(IID_IUnknown, 0x00000000, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IClassFactory, 0x00000001, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
MIME_DEFINE_GUID(IID_IMalloc, 0x00000002, 0x0000, 0x0000, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46);
#define CLSID_NULL GUID_NULL
#define IID_NULL GUID_NULL

/* ------------------------------------------------------------------ ole32 com functions */

typedef struct IMessageFilter IMessageFilter, *LPMESSAGEFILTER;

typedef HRESULT(STDAPICALLTYPE *LPFNGETCLASSOBJECT)(REFCLSID clsid, REFIID riid, void **object);
typedef HRESULT(STDAPICALLTYPE *LPFNCANUNLOADNOW)(void);

WINOLEAPI HRESULT WINAPI CoInitialize(void *reserved);
WINOLEAPI HRESULT WINAPI CoInitializeEx(void *reserved, DWORD flags);
WINOLEAPI void WINAPI CoUninitialize(void);
WINOLEAPI HRESULT WINAPI CoGetApartmentType(APTTYPE *type, APTTYPEQUALIFIER *qualifier);
WINOLEAPI DWORD WINAPI CoGetCurrentProcess(void);
WINOLEAPI HRESULT WINAPI CoCreateInstance(REFCLSID clsid, IUnknown *outer, DWORD context, REFIID riid, void **object);
WINOLEAPI HRESULT WINAPI CoCreateInstanceEx(REFCLSID clsid, IUnknown *outer, DWORD context, COSERVERINFO *server,
                                            DWORD count, MULTI_QI *results);
WINOLEAPI HRESULT WINAPI CoGetClassObject(REFCLSID clsid, DWORD context, void *server, REFIID riid, void **object);
WINOLEAPI void WINAPI CoFreeUnusedLibraries(void);
WINOLEAPI void WINAPI CoFreeUnusedLibrariesEx(DWORD delay, DWORD reserved);
WINOLEAPI HRESULT WINAPI CoLockObjectExternal(IUnknown *object, BOOL lock, BOOL last_unlock_releases);
WINOLEAPI HRESULT WINAPI CoGetMalloc(DWORD context, IMalloc **malloc);
WINOLEAPI void *WINAPI CoTaskMemAlloc(SIZE_T size);
WINOLEAPI void *WINAPI CoTaskMemRealloc(void *p, SIZE_T size);
WINOLEAPI void WINAPI CoTaskMemFree(void *p);
WINOLEAPI HRESULT WINAPI CoCreateGuid(GUID *guid);
WINOLEAPI int WINAPI StringFromGUID2(REFGUID guid, LPOLESTR buffer, int size);
WINOLEAPI HRESULT WINAPI StringFromCLSID(REFCLSID clsid, LPOLESTR *string);
WINOLEAPI HRESULT WINAPI StringFromIID(REFIID iid, LPOLESTR *string);
WINOLEAPI HRESULT WINAPI CLSIDFromString(LPCOLESTR string, LPCLSID clsid);
WINOLEAPI HRESULT WINAPI IIDFromString(LPCOLESTR string, LPIID iid);
WINOLEAPI HRESULT WINAPI CLSIDFromProgID(LPCOLESTR progid, LPCLSID clsid);
WINOLEAPI HRESULT WINAPI ProgIDFromCLSID(REFCLSID clsid, LPOLESTR *progid);
WINOLEAPI HRESULT WINAPI CoRegisterClassObject(REFCLSID clsid, IUnknown *object, DWORD context, DWORD flags,
                                               DWORD *cookie);
WINOLEAPI HRESULT WINAPI CoRevokeClassObject(DWORD cookie);
WINOLEAPI HRESULT WINAPI CoResumeClassObjects(void);
WINOLEAPI HRESULT WINAPI CoTreatAsClass(REFCLSID old_clsid, REFCLSID new_clsid);
WINOLEAPI HRESULT WINAPI CoGetTreatAsClass(REFCLSID clsid, CLSID *treat);
WINOLEAPI HRESULT WINAPI CoIncrementMTAUsage(void **cookie);
WINOLEAPI HRESULT WINAPI CoDecrementMTAUsage(void *cookie);
WINOLEAPI HRESULT WINAPI CoGetCallContext(REFIID iid, void **context);
WINOLEAPI HRESULT WINAPI CoSwitchCallContext(IUnknown *new_context, IUnknown **old_context);
WINOLEAPI HRESULT WINAPI CoImpersonateClient(void);
WINOLEAPI HRESULT WINAPI CoRevertToSelf(void);
WINOLEAPI HRESULT WINAPI CoGetCallerTID(DWORD *thread);
WINOLEAPI HRESULT WINAPI CoEnableCallCancellation(void *reserved);
WINOLEAPI HRESULT WINAPI CoDisableCallCancellation(void *reserved);
WINOLEAPI HRESULT WINAPI CoSetCancelObject(IUnknown *object);
WINOLEAPI HRESULT WINAPI CoGetCancelObject(DWORD thread, REFIID iid, void **object);
WINOLEAPI HRESULT WINAPI CoCancelCall(DWORD thread, ULONG timeout);
WINOLEAPI HRESULT WINAPI CoTestCancel(void);
WINOLEAPI BOOL WINAPI CoIsHandlerConnected(IUnknown *object);
WINOLEAPI HRESULT WINAPI CoGetCurrentLogicalThreadId(GUID *guid);
WINOLEAPI HRESULT WINAPI OleGetAutoConvert(REFCLSID old_clsid, LPCLSID new_clsid);
WINOLEAPI HRESULT WINAPI OleSetAutoConvert(REFCLSID old_clsid, REFCLSID new_clsid);
WINOLEAPI HRESULT WINAPI CoRegisterPSClsid(REFIID iid, REFCLSID clsid);
WINOLEAPI HRESULT WINAPI CoGetPSClsid(REFIID iid, CLSID *clsid);
WINOLEAPI HRESULT WINAPI CoRegisterMessageFilter(IMessageFilter *filter, IMessageFilter **previous);
WINOLEAPI HRESULT WINAPI CoDisconnectObject(IUnknown *object, DWORD reserved);


/* the wire forms of the pointers to interfaces and of the handles, with the 64 bit forms of the second transfer syntax. */
WINOLEAPI ULONG WINAPI WdtpInterfacePointer_UserSize(ULONG *flags, ULONG context, ULONG offset, IUnknown *punk,
                                                     REFIID iid);
WINOLEAPI BYTE *WINAPI WdtpInterfacePointer_UserMarshal(ULONG *flags, ULONG context, BYTE *buffer, IUnknown *punk,
                                                        REFIID iid);
WINOLEAPI BYTE *WINAPI WdtpInterfacePointer_UserUnmarshal(ULONG *flags, BYTE *buffer, IUnknown **punk, REFIID iid);
WINOLEAPI void WINAPI WdtpInterfacePointer_UserFree(IUnknown *punk);
WINOLEAPI ULONG WINAPI WdtpInterfacePointer_UserSize64(ULONG *flags, ULONG context, ULONG offset, IUnknown *punk,
                                                       REFIID iid);
WINOLEAPI BYTE *WINAPI WdtpInterfacePointer_UserMarshal64(ULONG *flags, ULONG context, BYTE *buffer, IUnknown *punk,
                                                          REFIID iid);
WINOLEAPI BYTE *WINAPI WdtpInterfacePointer_UserUnmarshal64(ULONG *flags, BYTE *buffer, IUnknown **punk, REFIID iid);
WINOLEAPI void WINAPI WdtpInterfacePointer_UserFree64(IUnknown *punk);


/* the wire forms of the handles and of global memory that a stub names, each with its form for the second syntax. */
#define MIME_WIRE_ROUTINES(prefix, type)                                                                               \
    WINOLEAPI ULONG WINAPI prefix##_UserSize(ULONG *flags, ULONG offset, type *value);                                 \
    WINOLEAPI BYTE *WINAPI prefix##_UserMarshal(ULONG *flags, BYTE *buffer, type *value);                              \
    WINOLEAPI BYTE *WINAPI prefix##_UserUnmarshal(ULONG *flags, BYTE *buffer, type *value);                            \
    WINOLEAPI void WINAPI prefix##_UserFree(ULONG *flags, type *value);                                                \
    WINOLEAPI ULONG WINAPI prefix##_UserSize64(ULONG *flags, ULONG offset, type *value);                               \
    WINOLEAPI BYTE *WINAPI prefix##_UserMarshal64(ULONG *flags, BYTE *buffer, type *value);                            \
    WINOLEAPI BYTE *WINAPI prefix##_UserUnmarshal64(ULONG *flags, BYTE *buffer, type *value);                          \
    WINOLEAPI void WINAPI prefix##_UserFree64(ULONG *flags, type *value);
MIME_WIRE_ROUTINES(HWND, HWND)
MIME_WIRE_ROUTINES(HICON, HICON)
MIME_WIRE_ROUTINES(HMENU, HMENU)
MIME_WIRE_ROUTINES(HACCEL, HANDLE)
MIME_WIRE_ROUTINES(HBRUSH, HBRUSH)
MIME_WIRE_ROUTINES(HDC, HDC)
MIME_WIRE_ROUTINES(HMONITOR, HMONITOR)
MIME_WIRE_ROUTINES(HGLOBAL, HGLOBAL)
MIME_WIRE_ROUTINES(HBITMAP, HBITMAP)

#endif
