/* ole32 internals shared by its source files. */
#ifndef MIME_OLE32_PRIVATE_H
#define MIME_OLE32_PRIVATE_H

#define WINOLEAPI DECLSPEC_EXPORT
#include <mime/win32/ole2.h>
#include <mime/win32/propidl.h>
#include <rtl.h>

/* user32 exports that winuser.h does not declare yet. */
WINUSERAPI BOOL WINAPI OpenClipboard(HWND owner);
WINUSERAPI BOOL WINAPI CloseClipboard(void);
WINUSERAPI BOOL WINAPI EmptyClipboard(void);
WINUSERAPI HANDLE WINAPI SetClipboardData(UINT format, HANDLE data);
WINUSERAPI HANDLE WINAPI GetClipboardData(UINT format);
WINUSERAPI BOOL WINAPI IsClipboardFormatAvailable(UINT format);
WINUSERAPI HWND WINAPI WindowFromPoint(POINT pt);
WINUSERAPI DWORD WINAPI GetWindowThreadProcessId(HWND hwnd, LPDWORD process);
WINUSERAPI BOOL WINAPI SetPropW(HWND hwnd, LPCWSTR name, HANDLE data);
WINUSERAPI HANDLE WINAPI RemovePropW(HWND hwnd, LPCWSTR name);
WINUSERAPI DWORD WINAPI MsgWaitForMultipleObjects(DWORD count, const HANDLE *handles, BOOL wait_all,
                                                  DWORD milliseconds, DWORD wake_mask);

#define OLE_TLS_LOCALTID 0x01
#define OLE_TLS_APARTMENTTHREADED 0x80
#define OLE_TLS_MULTITHREADED 0x100

/* per thread COM and ole state. */
typedef struct _OLE_THREAD {
    /* the first fields mirror the block windows keeps in the TEB's OLE slot. programs such as Chromium read the
       apartment flags there: 0x80 single threaded, 0x100 multithreaded. */
    PVOID ThreadBase, SmAllocator;
    DWORD ApartmentId, ApartmentFlags;
    LONG TlsMapIndex;
    PVOID TlsSlot;
    LONG ComInits;
    LONG OleInits;
    DWORD Calls;
    PVOID ServerCall, CallObjectCache, ContextStack, ObjectServer;
    DWORD NativeCallerThread;
    /* native clr reads this cached context directly from the ole tls block. */
    IUnknown *CachedContext;
    PVOID CurrentContext;
    BOOL Sta;
    /* what CoGetCurrentProcess returns, unique per thread. */
    DWORD ProcessId;
    /* set by CoRegisterMessageFilter, referenced. */
    IMessageFilter *MessageFilter;
    /* the hidden window owning the clipboard for the thread's ole clipboard, see clipboard.c. */
    HWND ClipWindow;
    /* the call context set by CoSwitchCallContext, see callctx.c. */
    IUnknown *CallContext;
    /* the caller's thread of the call being served, 0 for none. */
    DWORD CallerThread;
    /* CoEnableCallCancellation calls not yet matched by CoDisableCallCancellation. */
    LONG CancelEnabled;
    /* set by CoSetCancelObject, referenced. */
    IUnknown *CancelObject;
    /* the logical thread guid, made on first request. */
    GUID LogicalThread;
    BOOL HasLogicalThread;
    /* the single threaded apartment, made when the first object leaves it, see orpc_export.c. */
    struct APARTMENT *Apartment;
    /* an event for calls this thread makes into another thread's apartment. */
    HANDLE OrpcEvent;
    /* the call from another process this thread is serving. */
    void *OrpcCall;
    /* the application's result reported by the running stub, see DcomChannelSetHResult. */
    HRESULT CallResult;
} OLE_THREAD;

static_assert(offsetof(OLE_THREAD, CachedContext) == (sizeof(void *) == 4 ? 0x38 : 0x60));
static_assert(offsetof(OLE_THREAD, CurrentContext) == (sizeof(void *) == 4 ? 0x3c : 0x68));

OLE_THREAD *OleThreadState(BOOL create);
void OleThreadDetach(void);
void OleProcessAttach(void);

/* com.c */
void OleMtaUsage(LONG delta);
void OleContextMtaUsers(LONG delta);
void OleContextLeave(OLE_THREAD *thread);

/* CO_E_NOTINITIALIZED when the calling thread has no apartment. */
HRESULT OleCheckApartment(void);

FORCEINLINE void *OleAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), 0, size);
}

FORCEINLINE void *OleAllocZero(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
}

FORCEINLINE void OleFree(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

/* com.c: braced upper case guids, 39 characters with the terminator. */
void OleFormatGuid(REFGUID guid, WCHAR *out);
BOOL OleParseGuid(LPCOLESTR string, GUID *guid);
/* the user's registrations come before the machine's. */
LSTATUS OleOpenClassesKey(LPCWSTR subkey, REGSAM access, HKEY *key);
/* HKCR\CLSID\{clsid} or one of its subkeys. */
LSTATUS OleOpenClassKey(REFCLSID clsid, LPCWSTR subkey, REGSAM access, HKEY *key);

/* com.c */
void OleLocalServersResume(void);

/* clipboard.c */
void OleClipboardUninitialize(void);
/* enumerates a copy of the formats. */
IEnumFORMATETC *OleFormatEnumCreate(const FORMATETC *formats, ULONG count);

/* dragdrop.c */
IDropTarget *OleFindDropTarget(HWND hwnd);

/* hostdnd.c: drag and drop with host programs. user32 calls the handler for WM_MIME_HOSTDND. */
void WINAPI OleHostDndHandler(HWND hwnd, LPARAM lparam);
void OleHostDndRegister(HWND hwnd);
void OleHostDndRevoke(HWND hwnd);
/* moves a DoDragDrop drag to the host, false when it cannot. */
bool OleHostDragBegin(IDataObject *data, DWORD allowed);
bool OleHostDragActive(void);
bool OleHostDragEnded(bool *dropped, DWORD *effect);
void OleHostDragFinish(void);
/* whether the pointer left the host window the drag began in, which a held button allows. */
bool OleHostPointerOutside(void);

#endif
