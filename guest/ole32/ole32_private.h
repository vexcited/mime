/*
 * ole32 internal interface shared by its source files.
 */
#ifndef MIME_OLE32_PRIVATE_H
#define MIME_OLE32_PRIVATE_H

#define WINOLEAPI DECLSPEC_EXPORT
#include <mime/win32/oleidl.h>
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
} OLE_THREAD;

OLE_THREAD *OleThreadState(BOOL create);
void OleThreadDetach(void);
void OleProcessAttach(void);

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

/* clipboard.c */
void OleClipboardUninitialize(void);

/* dragdrop.c */
IDropTarget *OleFindDropTarget(HWND hwnd);

#endif
