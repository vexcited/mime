/*
 * ole32 internal interface shared by its source files.
 */
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

/* the per thread com and ole state. */
typedef struct _OLE_THREAD {
    LONG ComInits;
    BOOL Sta;
    LONG OleInits;
    /* the value of CoGetCurrentProcess, unique per thread. */
    DWORD ProcessId;
    /* the filter of CoRegisterMessageFilter, referenced. */
    IMessageFilter *MessageFilter;
    /* the hidden window that owns the clipboard for the thread's ole clipboard, see clipboard.c. */
    HWND ClipWindow;
    /* the object CoSwitchCallContext put as the context of the call being served, see callctx.c. */
    IUnknown *CallContext;
    /* the thread that made the call being served, 0 when there is none. */
    DWORD CallerThread;
    /* the number of CoEnableCallCancellation without its CoDisableCallCancellation. */
    LONG CancelEnabled;
    /* the object of CoSetCancelObject, referenced. */
    IUnknown *CancelObject;
    /* the logical thread, a guid made when first asked for. */
    GUID LogicalThread;
    BOOL HasLogicalThread;
    /* the apartment of a single threaded thread, made when the first object leaves it, see orpc_export.c. */
    struct APARTMENT *Apartment;
    /* an event for the calls the thread makes into the apartment of another thread. */
    HANDLE OrpcEvent;
    /* the call of another process this thread is serving. */
    void *OrpcCall;
    /* the result of the application's call that the stub being run reported, see DcomChannelSetHResult. */
    HRESULT CallResult;
} OLE_THREAD;

OLE_THREAD *OleThreadState(BOOL create);
void OleThreadDetach(void);
void OleProcessAttach(void);

/* com.c: a user of the multithreaded apartment comes and goes. */
void OleMtaUsage(LONG delta);

/* the apartment of the calling thread, or CO_E_NOTINITIALIZED. */
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
/* opens a key below the classes, the user's registrations before the machine's. */
LSTATUS OleOpenClassesKey(LPCWSTR subkey, REGSAM access, HKEY *key);
/* opens HKCR\CLSID\{clsid} or one of its subkeys. */
LSTATUS OleOpenClassKey(REFCLSID clsid, LPCWSTR subkey, REGSAM access, HKEY *key);

/* com.c: the classes registered suspended are offered. */
void OleLocalServersResume(void);

/* clipboard.c */
void OleClipboardUninitialize(void);
/* an enumerator over a copy of the formats. */
IEnumFORMATETC *OleFormatEnumCreate(const FORMATETC *formats, ULONG count);

/* dragdrop.c */
IDropTarget *OleFindDropTarget(HWND hwnd);

/* hostdnd.c: drag and drop with the host's programs. user32 calls the handler for WM_MIME_HOSTDND. */
void WINAPI OleHostDndHandler(HWND hwnd, LPARAM lparam);
void OleHostDndRegister(HWND hwnd);
void OleHostDndRevoke(HWND hwnd);
/* a drag of DoDragDrop moves to the host; false when it cannot. */
bool OleHostDragBegin(IDataObject *data, DWORD allowed);
bool OleHostDragActive(void);
bool OleHostDragEnded(bool *dropped, DWORD *effect);
void OleHostDragFinish(void);
/* whether the pointer is outside the host window it began in, which a held button allows. */
bool OleHostPointerOutside(void);

#endif
