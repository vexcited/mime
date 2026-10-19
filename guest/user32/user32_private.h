/*
 * user32 internal interface.
 */
#ifndef MIME_USER32_PRIVATE_H
#define MIME_USER32_PRIVATE_H

#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/ntuser.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>

/* resource types as wide resource ids. */
#define RTW_CURSOR MAKEINTRESOURCEW(1)
#define RTW_BITMAP MAKEINTRESOURCEW(2)
#define RTW_ICON MAKEINTRESOURCEW(3)
#define RTW_DIALOG MAKEINTRESOURCEW(5)
#define RTW_STRING MAKEINTRESOURCEW(6)
#define RTW_GROUP_CURSOR MAKEINTRESOURCEW(12)
#define RTW_GROUP_ICON MAKEINTRESOURCEW(14)

#ifndef ERROR_CANNOT_FIND_WND_CLASS
#define ERROR_CANNOT_FIND_WND_CLASS 1407L
#endif
#ifndef ERROR_INVALID_DWP_HANDLE
#define ERROR_INVALID_DWP_HANDLE 1405L
#endif
#ifndef ERROR_NO_SCROLLBARS
#define ERROR_NO_SCROLLBARS 1447L
#endif
#ifndef ERROR_INVALID_SCROLLBAR_RANGE
#define ERROR_INVALID_SCROLLBAR_RANGE 1448L
#endif
#ifndef ERROR_INVALID_COMBOBOX_MESSAGE
#define ERROR_INVALID_COMBOBOX_MESSAGE 1422L
#endif

static inline int UserMulDiv(int a, int b, int c)
{
    if (!c)
        return -1;
    LONG64 v = (LONG64)a * b;
    v += (v < 0) == (c < 0) ? c / 2 : -c / 2;
    return (int)(v / c);
}

extern HINSTANCE User32Instance;

/* dnd.c: runs WM_MIME_HOSTDND, drag and drop with the host's programs. */
void UserHostDnd(const MSG *msg);

/* the process dpi awareness: 0 unaware, 1 system, 2 per monitor, 3 per monitor v2. */
extern ULONG UserDpiAwareness;
extern BOOL UserDpiAwarenessLocked;
extern BOOL UserGdiScaling;
void UserInitializeDpiAwareness(void);
DPI_AWARENESS_CONTEXT UserDpiAwarenessContext(void);
extern MIME_CLIENT_INFO User32Client;

/* memory from the process heap. */
static inline void *UserAlloc(SIZE_T size)
{
    return RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, HEAP_ZERO_MEMORY, size ? size : 1);
}

static inline void UserFree(void *p)
{
    if (p)
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, p);
}

/* ansi helpers: heap copies, NULL for NULL, atoms and resource ids pass through. */
LPWSTR UserAtoW(LPCSTR s);
LPWSTR UserAtoWn(LPCSTR s, int count, int *out_count);
LPSTR UserWtoA(LPCWSTR s);
static inline void UserFreeConverted(const void *converted, const void *original)
{
    if (converted != original && !IS_INTRESOURCE(converted))
        UserFree((void *)converted);
}

/* the move and size loop of DefWindowProc, movesize.c. */
BOOL UserDefMoveSize(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, LRESULT *result);

/* message translation between ansi and unicode, ansi.c. */
typedef struct CONV {
    int kind;
    UINT msg;
    HWND hwnd;
    WPARAM wparam, user_wparam;  /* what the call is given and what the caller gave */
    LPARAM lparam, user_lparam;
    void *block;                 /* memory of the conversion, released at its end */
    int room;
    BOOL refused;                /* nothing to send: refusal is the answer */
    LRESULT refusal;
} CONV;
BOOL UserConvBegin(CONV *c, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT UserConvEnd(CONV *c, LRESULT result);
LRESULT UserMessageCall(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, ULONG_PTR info, DWORD type, BOOL ansi);
LRESULT UserCallProc(WNDPROC proc, BOOL proc_ansi, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT UserSendMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, BOOL ansi);
WPARAM UserCharWparam(UINT msg, WPARAM wparam, BOOL to_wide);

/* window procedures of the system classes. */
LRESULT CALLBACK ButtonProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK StaticProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK ListBoxProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK ComboBoxProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK ScrollBarProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK MessageWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK DesktopProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK MenuProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

/* resources. */
typedef struct USER_RESFILE USER_RESFILE;
USER_RESFILE *UserResOpen(PCWSTR path);
const void *UserResFind(USER_RESFILE *f, LPCWSTR type, LPCWSTR name, WORD lang, DWORD *size);
USER_RESFILE *UserSystemResources(void); /* SystemResources\user32.dll.mun */
USER_RESFILE *UserStringResources(void); /* en-US\user32.dll.mui */
USER_RESFILE *UserImageResources(void);  /* SystemResources\imageres.dll.mun */
HICON UserLoadIconWithScaleDown(HINSTANCE instance, LPCWSTR name, int cx, int cy);
int UserLoadSystemString(UINT id, LPWSTR buffer, int max);
HICON UserLoadIconFromResources(USER_RESFILE *file, HINSTANCE module, LPCWSTR name, int cx, int cy, BOOL icon,
                                UINT flags);

/* theme drawing for the common controls look. */
typedef enum THEME_PART {
    THEME_PUSHBUTTON = 1,
    THEME_RADIOBUTTON = 2,
    THEME_CHECKBOX = 3,
    THEME_GROUPBOX = 4,
} THEME_PART;

/* push button states, as in vssym32.h. */
enum {
    PBS_NORMAL = 1,
    PBS_HOT = 2,
    PBS_PRESSED = 3,
    PBS_DISABLED = 4,
    PBS_DEFAULTED = 5,
};

BOOL UserThemeActive(void);
BOOL UserThemeDrawBackground(HDC dc, int part, int state, const RECT *rect, UINT dpi);
BOOL UserThemeContentRect(int part, int state, const RECT *rect, RECT *content, UINT dpi);
BOOL UserThemePartSize(int part, int state, UINT dpi, SIZE *size);
COLORREF UserThemeTextColor(int part, int state, COLORREF fallback);

/* drawing helpers. */
void UserDrawFocusRect(HDC dc, const RECT *r);
HFONT UserGetMessageFont(UINT dpi);
HFONT UserGetMessageBoxFont(UINT dpi);
UINT UserWindowDpi(HWND hwnd);

/* scroll bar state kept in window properties, scroll.c. */
BOOL UserScrollBarMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, LRESULT *result);
void UserFreeScrollData(HWND hwnd);
BOOL UserDefScrollBars(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, LRESULT *result);

/* caret.c: the blink of the caret, its hiding while its window paints, and its end with its window. */
BOOL UserCaretTimer(HWND hwnd, WPARAM id);
void UserCaretPaint(HWND hwnd, BOOL begin);
void UserCaretDestroyed(HWND hwnd);
HWND UserCaretInfo(RECT *rect);
void UserDrawScrollBar(HWND hwnd, HDC dc);
void UserTrackScrollBar(HWND hwnd, POINT screen);

/* dialogs. */
HWND UserCreateDialog(HINSTANCE instance, LPCDLGTEMPLATEW tmpl, HWND owner, DLGPROC proc, LPARAM param, BOOL ansi,
                      BOOL modal);
INT_PTR UserModalLoop(HWND dialog, HWND owner);

#endif
