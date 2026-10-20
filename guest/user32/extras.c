/*
 * user32 functions that the api sweep found missing: waiting for a process to be idle, the setters of window
 * classes, the ANSI class queries and the small queries of the process and thread.
 */
#include "user32_private.h"
#include <mime/win32/winuser.h>

/* ------------------------------------------------------------------ waiting for input */

#define STATUS_OBJECT_TYPE_MISMATCH_ ((NTSTATUS)0xC0000024)

/*
 * until the process waits for input with nothing to read: 0 when it does, WAIT_TIMEOUT after the time, and
 * WAIT_FAILED (-1) for a handle that is none of a process (ERROR_INVALID_HANDLE) or a process that ended.
 * A program of the console has no queue and gives 0 at once.
 */
WINUSERAPI DWORD WINAPI WaitForInputIdle(HANDLE process, DWORD milliseconds)
{
    PROCESS_BASIC_INFORMATION info;
    NTSTATUS status = NtQueryInformationProcess(process, ProcessBasicInformation, &info, sizeof(info), nullptr);
    if (NT_SUCCESS(status))
        return (DWORD)NtUserWaitForInputIdle((ULONG_PTR)info.UniqueProcessId, milliseconds, FALSE);
    if (status == STATUS_OBJECT_TYPE_MISMATCH_) {
        /* a handle that is not one of a process but a remote one or a pseudo handle. */
        if ((ULONG_PTR)process & 2)
            return (DWORD)NtUserWaitForInputIdle((ULONG_PTR)process & ~(ULONG_PTR)3, milliseconds, TRUE);
        if ((ULONG_PTR)process & 1)
            return 0;
    }
    SetLastError(ERROR_INVALID_HANDLE);
    return WAIT_FAILED;
}

/* ------------------------------------------------------------------ classes */

/*
 * an atom or a name: the A forms of the class functions turn a name to wide characters and leave an atom as it
 * is. The value is the old one, 0 with the last error set when the change failed.
 */
WINUSERAPI ULONG_PTR WINAPI SetClassLongPtrW(HWND hwnd, int index, LONG_PTR value)
{
    if (index == GCLP_HBRBACKGROUND && (ULONG_PTR)value > 30) {
        if (GetObjectType((HGDIOBJ)value) != OBJ_BRUSH) {
            SetLastError(ERROR_INVALID_HANDLE);
            return 0;
        }
    }
    return NtUserSetClassLongPtr(hwnd, index, (ULONG_PTR)value, FALSE);
}

WINUSERAPI ULONG_PTR WINAPI SetClassLongPtrA(HWND hwnd, int index, LONG_PTR value)
{
    if (index == GCLP_HBRBACKGROUND && (ULONG_PTR)value > 30) {
        if (GetObjectType((HGDIOBJ)value) != OBJ_BRUSH) {
            SetLastError(ERROR_INVALID_HANDLE);
            return 0;
        }
    }
    if (index == GCL_MENUNAME && value && !IS_INTRESOURCE(value)) {
        LPWSTR name = UserAtoW((LPCSTR)value);
        ULONG_PTR old = name ? NtUserSetClassLongPtr(hwnd, index, (ULONG_PTR)name, TRUE) : 0;
        UserFreeConverted(name, (LPCSTR)value);
        return old;
    }
    return NtUserSetClassLongPtr(hwnd, index, (ULONG_PTR)value, index == GCLP_WNDPROC);
}

WINUSERAPI DWORD WINAPI SetClassLongA(HWND hwnd, int index, LONG value)
{
#ifdef __i386__
    return (DWORD)SetClassLongPtrA(hwnd, index, (LONG_PTR)(ULONG)value);
#else
    return NtUserSetClassLong(hwnd, index, (ULONG)value, index == GCLP_WNDPROC);
#endif
}

WINUSERAPI WORD WINAPI SetClassWord(HWND hwnd, int index, WORD value)
{
    return NtUserSetClassWord(hwnd, index, value);
}

WINUSERAPI BOOL WINAPI GetClassInfoExA(HINSTANCE instance, LPCSTR name, LPWNDCLASSEXA wc)
{
    LPWSTR wide = UserAtoW(name);
    WNDCLASSEXW ex;
    memset(&ex, 0, sizeof(ex));
    ex.cbSize = sizeof(ex);
    BOOL ok = GetClassInfoExW(instance, wide, &ex);
    UserFreeConverted(wide, name);
    if (!ok)
        return FALSE;
    wc->cbSize = sizeof(*wc);
    wc->style = ex.style;
    wc->lpfnWndProc = ex.lpfnWndProc;
    wc->cbClsExtra = ex.cbClsExtra;
    wc->cbWndExtra = ex.cbWndExtra;
    wc->hInstance = ex.hInstance;
    wc->hIcon = ex.hIcon;
    wc->hCursor = ex.hCursor;
    wc->hbrBackground = ex.hbrBackground;
    wc->lpszMenuName = nullptr;
    wc->lpszClassName = name;
    wc->hIconSm = ex.hIconSm;
    return TRUE;
}

WINUSERAPI BOOL WINAPI GetClassInfoA(HINSTANCE instance, LPCSTR name, LPWNDCLASSA wc)
{
    WNDCLASSEXA ex;
    memset(&ex, 0, sizeof(ex));
    if (!GetClassInfoExA(instance, name, &ex))
        return FALSE;
    wc->style = ex.style;
    wc->lpfnWndProc = ex.lpfnWndProc;
    wc->cbClsExtra = ex.cbClsExtra;
    wc->cbWndExtra = ex.cbWndExtra;
    wc->hInstance = ex.hInstance;
    wc->hIcon = ex.hIcon;
    wc->hCursor = ex.hCursor;
    wc->hbrBackground = ex.hbrBackground;
    wc->lpszMenuName = ex.lpszMenuName;
    wc->lpszClassName = ex.lpszClassName;
    return TRUE;
}

/* ------------------------------------------------------------------ the thread and the process */

/*
 * whether the thread has a queue; with convert a thread that has none is given one, and its failing is
 * ERROR_NOT_ENOUGH_MEMORY.
 */
WINUSERAPI BOOL WINAPI IsGUIThread(BOOL convert)
{
    if (NtUserGetThreadState(15))
        return TRUE;
    if (!convert)
        return FALSE;
    if (NtUserGetThreadState(14))
        return TRUE;
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
}

WINUSERAPI LPARAM WINAPI SetMessageExtraInfo(LPARAM value)
{
    return (LPARAM)NtUserCallOneParam((ULONG_PTR)value, MIME_ONEPARAM_SETMESSAGEEXTRAINFO);
}

/* the layout windows get when they are made: LAYOUT_RTL makes the top level windows right to left. */
static volatile LONG g_default_layout;

WINUSERAPI BOOL WINAPI GetProcessDefaultLayout(DWORD *layout)
{
    if (!layout) {
        SetLastError(ERROR_NOACCESS);
        return FALSE;
    }
    *layout = (DWORD)g_default_layout;
    return TRUE;
}

WINUSERAPI BOOL WINAPI SetProcessDefaultLayout(DWORD layout)
{
    if (layout & ~1u) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    InterlockedExchange(&g_default_layout, (LONG)layout);
    return TRUE;
}

DWORD UserDefaultLayout(void)
{
    return (DWORD)g_default_layout;
}

/* there are no programs of the store here, so none is an immersive process. */
WINUSERAPI BOOL WINAPI IsImmersiveProcess(HANDLE process)
{
    return FALSE;
}

/* the desktop is always composed, since windows 8 has no way to turn that off. */
WINUSERAPI BOOL WINAPI IsThreadDesktopComposited(void)
{
    return TRUE;
}

/* no shims apply to the programs here. */
WINUSERAPI DWORD WINAPI GetAppCompatFlags(HTASK task)
{
    return 0;
}

WINUSERAPI DWORD WINAPI GetAppCompatFlags2(HTASK task)
{
    return 0;
}

/* realizing a palette changes nothing on a true color display. */
WINUSERAPI UINT WINAPI UserRealizePalette(HDC dc)
{
    return 0;
}

/* ------------------------------------------------------------------ small functions */

/* the type that older programs passed is not used. */
WINUSERAPI void WINAPI SetLastErrorEx(DWORD error, DWORD type)
{
    SetLastError(error);
}

/* turns the character to the wide one of the code page of the layout's locale, then asks as the wide form does. */
WINUSERAPI SHORT WINAPI VkKeyScanExA(CHAR ch, HKL layout)
{
    UINT page = 0;
    WCHAR wide = 0;
    if (!GetLocaleInfoW(LOWORD((ULONG_PTR)layout), LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER, (LPWSTR)&page, 2))
        return -1;
    if (IsDBCSLeadByteEx(page, (BYTE)ch) || MultiByteToWideChar(page, 0, &ch, 1, &wide, 1) == 0)
        return -1;
    return VkKeyScanExW(wide, layout);
}

/* ------------------------------------------------------------------ touch injection */

#define TOUCH_FEEDBACK_DEFAULT_ 1
#define TOUCH_FEEDBACK_NONE_ 3
#define TOUCH_MAX_CONTACTS 256

static volatile LONG g_touch_contacts;

/*
 * the contacts the process may inject at once, 1 to 256, and how the feedback shows, default, indirect or none; read
 * from the documentation, the exact checks are a guess.
 */
WINUSERAPI BOOL WINAPI InitializeTouchInjection(UINT32 max_contacts, DWORD mode)
{
    if (max_contacts < 1 || max_contacts > TOUCH_MAX_CONTACTS || mode < TOUCH_FEEDBACK_DEFAULT_ ||
        mode > TOUCH_FEEDBACK_NONE_) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    InterlockedExchange(&g_touch_contacts, (LONG)max_contacts);
    return TRUE;
}

/*
 * there is no pointer input stack to deliver a contact to, so an injection that is well formed is refused with
 * ERROR_NOT_SUPPORTED. One before the initialization, with no contacts, too many or a null array is a bad parameter.
 */
WINUSERAPI BOOL WINAPI InjectTouchInput(UINT32 count, const VOID *contacts)
{
    UINT32 allowed = (UINT32)g_touch_contacts;
    if (!allowed || !count || count > allowed || !contacts) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    SetLastError(ERROR_NOT_SUPPORTED);
    return FALSE;
}
