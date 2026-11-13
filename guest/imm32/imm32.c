/*
 * imm32: input contexts keep their windows, positions, fonts and status like on Windows. the input method is the
 * host's: when the desktop has one, win32k sends the composition messages and strings to the focus window,
 * otherwise nothing composes. there is no ime window or candidate list, the host's input method draws its own near
 * where the program says text is entered.
 */
#define MIME_BUILD_IMM32
#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/ntuser.h>
#include <mime/win32/winbase.h>
#include <mime/win32/imm.h>

WINUSERAPI BOOL WINAPI SetPropW(HWND hwnd, LPCWSTR name, HANDLE data);
WINUSERAPI HANDLE WINAPI GetPropW(HWND hwnd, LPCWSTR name);
WINUSERAPI HANDLE WINAPI RemovePropW(HWND hwnd, LPCWSTR name);
WINUSERAPI DWORD WINAPI GetWindowThreadProcessId(HWND hwnd, LPDWORD pid);
WINUSERAPI HWND WINAPI GetFocus(void);

WINUSERAPI UINT WINAPI RegisterWindowMessageW(LPCWSTR name);

/*
 * window property holding the associated context, 1 means an explicit NULL association. its name is a registered
 * atom that win32k looks up to find the windows that opted out of input methods.
 */
#define IMM_PROP_NAME L"ImmContext"
static LPCWSTR imm_prop(void)
{
    static UINT atom;
    if (!atom)
        atom = RegisterWindowMessageW(IMM_PROP_NAME);
    return MAKEINTATOM(atom);
}
#define IMM_PROP imm_prop()
#define IMM_NULL_CONTEXT ((HANDLE)(ULONG_PTR)1)

typedef struct IMM_CONTEXT {
    DWORD Thread;
    BOOL Used, Default, Open, HasFont;
    DWORD Conversion, Sentence;
    COMPOSITIONFORM Composition;
    CANDIDATEFORM Candidates[4];
    LOGFONTW Font;
} IMM_CONTEXT;

static struct {
    RTL_RUN_ONCE Once;
    RTL_CRITICAL_SECTION Lock;
    IMM_CONTEXT *Contexts;
    ULONG Count, Capacity;
} g_imm;

static ULONG NTAPI init_imm(PRTL_RUN_ONCE once, PVOID param, PVOID *context)
{
    RtlInitializeCriticalSection(&g_imm.Lock);
    return TRUE;
}

static void lock(void)
{
    RtlRunOnceExecuteOnce(&g_imm.Once, init_imm, nullptr, nullptr);
    RtlEnterCriticalSection(&g_imm.Lock);
}

static void unlock(void)
{
    RtlLeaveCriticalSection(&g_imm.Lock);
}

/* handles are the slot index plus one, times 4, so they never look like the NULL marker. */
static HIMC to_handle(ULONG index)
{
    return (HIMC)(ULONG_PTR)((index + 1) * 4);
}

/* call with the lock held. */
static IMM_CONTEXT *from_handle(HIMC himc)
{
    ULONG_PTR v = (ULONG_PTR)himc;
    if (!v || (v & 3) || v / 4 - 1 >= g_imm.Count)
        return nullptr;
    IMM_CONTEXT *c = &g_imm.Contexts[v / 4 - 1];
    return c->Used ? c : nullptr;
}

static HIMC new_context(DWORD thread, BOOL is_default)
{
    ULONG i;
    for (i = 0; i < g_imm.Count; i++)
        if (!g_imm.Contexts[i].Used)
            break;
    if (i == g_imm.Count) {
        if (g_imm.Count == g_imm.Capacity) {
            ULONG cap = g_imm.Capacity ? g_imm.Capacity * 2 : 16;
            IMM_CONTEXT *n = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, HEAP_ZERO_MEMORY, cap * sizeof(*n));
            if (!n)
                return nullptr;
            if (g_imm.Contexts) {
                memcpy(n, g_imm.Contexts, g_imm.Count * sizeof(*n));
                RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, g_imm.Contexts);
            }
            g_imm.Contexts = n;
            g_imm.Capacity = cap;
        }
        g_imm.Count++;
    }
    IMM_CONTEXT *c = &g_imm.Contexts[i];
    memset(c, 0, sizeof(*c));
    c->Used = TRUE;
    c->Default = is_default;
    c->Thread = thread;
    for (int k = 0; k < 4; k++)
        c->Candidates[k].dwIndex = (DWORD)-1;
    return to_handle(i);
}

/* a thread's default context, created on first use. */
static HIMC thread_context(DWORD thread)
{
    for (ULONG i = 0; i < g_imm.Count; i++)
        if (g_imm.Contexts[i].Used && g_imm.Contexts[i].Default && g_imm.Contexts[i].Thread == thread)
            return to_handle(i);
    return new_context(thread, TRUE);
}

/* call with the lock held. FALSE for windows of other processes. */
static BOOL window_context(HWND hwnd, HIMC *himc)
{
    DWORD pid = 0, thread = GetWindowThreadProcessId(hwnd, &pid);
    if (!thread || pid != GetCurrentProcessId())
        return FALSE;
    HANDLE prop = GetPropW(hwnd, IMM_PROP);
    if (prop == IMM_NULL_CONTEXT)
        *himc = nullptr;
    else if (prop && from_handle((HIMC)prop))
        *himc = (HIMC)prop;
    else
        *himc = thread_context(thread);
    return TRUE;
}

static BOOL owned_by_thread(HIMC himc)
{
    IMM_CONTEXT *c = from_handle(himc);
    return c && c->Thread == GetCurrentThreadId();
}

IMMAPI HIMC WINAPI ImmCreateContext(void)
{
    lock();
    HIMC himc = new_context(GetCurrentThreadId(), FALSE);
    unlock();
    return himc;
}

IMMAPI BOOL WINAPI ImmDestroyContext(HIMC himc)
{
    lock();
    IMM_CONTEXT *c = from_handle(himc);
    BOOL ok = c && !c->Default && c->Thread == GetCurrentThreadId();
    if (ok)
        c->Used = FALSE;
    unlock();
    return ok;
}

IMMAPI HIMC WINAPI ImmGetContext(HWND hwnd)
{
    if (!hwnd)
        return nullptr;
    if (!IsWindow(hwnd)) {
        SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        return nullptr;
    }
    HIMC himc = nullptr;
    lock();
    if (!window_context(hwnd, &himc))
        himc = nullptr;
    unlock();
    return himc;
}

IMMAPI BOOL WINAPI ImmReleaseContext(HWND hwnd, HIMC himc)
{
    return TRUE;
}

static void set_window_context(HWND hwnd, HIMC himc)
{
    SetPropW(hwnd, IMM_PROP, himc ? (HANDLE)himc : IMM_NULL_CONTEXT);
}

IMMAPI HIMC WINAPI ImmAssociateContext(HWND hwnd, HIMC himc)
{
    if (!hwnd || !IsWindow(hwnd))
        return nullptr;
    lock();
    HIMC old = nullptr;
    if ((!himc || owned_by_thread(himc)) && window_context(hwnd, &old) && old != himc)
        set_window_context(hwnd, himc);
    unlock();
    return old;
}

typedef struct ASSOCIATE {
    HIMC Context;
    DWORD Flags;
} ASSOCIATE;

static void associate_one(HWND hwnd, const ASSOCIATE *a)
{
    HIMC current;
    if (!window_context(hwnd, &current))
        return;
    if (a->Flags & IACE_DEFAULT) {
        RemovePropW(hwnd, IMM_PROP);
        return;
    }
    if ((a->Flags & IACE_IGNORENOCONTEXT) && !current)
        return;
    set_window_context(hwnd, a->Context);
}

static BOOL CALLBACK associate_child(HWND hwnd, LPARAM param)
{
    associate_one(hwnd, (const ASSOCIATE *)param);
    return TRUE;
}

IMMAPI BOOL WINAPI ImmAssociateContextEx(HWND hwnd, HIMC himc, DWORD flags)
{
    if (!hwnd || !IsWindow(hwnd))
        return FALSE;
    lock();
    BOOL ok = !himc || (flags & IACE_DEFAULT) || owned_by_thread(himc);
    if (ok) {
        ASSOCIATE a = {himc, flags};
        associate_one(hwnd, &a);
        if (flags & IACE_CHILDREN)
            EnumChildWindows(hwnd, associate_child, (LPARAM)&a);
    }
    unlock();
    return ok;
}

static BOOL own_context(HIMC himc)
{
    lock();
    BOOL ok = owned_by_thread(himc);
    unlock();
    return ok;
}

IMMAPI LONG WINAPI ImmGetCompositionStringW(HIMC himc, DWORD index, LPVOID buffer, DWORD size)
{
    if (!own_context(himc))
        return 0;
    return (LONG)NtUserMimeIme(MIME_IME_GET, index, 0, buffer, size);
}

IMMAPI LONG WINAPI ImmGetCompositionStringA(HIMC himc, DWORD index, LPVOID buffer, DWORD size)
{
    if (!own_context(himc))
        return 0;
    if (index != GCS_COMPSTR && index != GCS_COMPREADSTR && index != GCS_RESULTSTR && index != GCS_RESULTREADSTR)
        return (LONG)NtUserMimeIme(MIME_IME_GET, index, 0, buffer, size);
    /* converts to the ansi code page, clause and attribute counts match for single byte characters. */
    WCHAR wide[512];
    LONG bytes = (LONG)NtUserMimeIme(MIME_IME_GET, index, 0, (PUCHAR)wide, sizeof(wide));
    int chars = bytes / (int)sizeof(WCHAR);
    int needed = chars ? WideCharToMultiByte(CP_ACP, 0, wide, chars, nullptr, 0, nullptr, nullptr) : 0;
    if (!buffer || !size)
        return needed;
    char *out = buffer;
    int n = needed ? WideCharToMultiByte(CP_ACP, 0, wide, chars, out, (int)size, nullptr, nullptr) : 0;
    return n;
}

IMMAPI BOOL WINAPI ImmSetCompositionStringW(HIMC himc, DWORD index, LPVOID comp, DWORD comp_size, LPVOID read,
                                            DWORD read_size)
{
    return FALSE;
}

IMMAPI BOOL WINAPI ImmGetCompositionWindow(HIMC himc, LPCOMPOSITIONFORM form)
{
    if (!form)
        return FALSE;
    lock();
    IMM_CONTEXT *c = from_handle(himc);
    if (c)
        *form = c->Composition;
    unlock();
    return c != nullptr;
}

/* tells the host where text is entered, in client coordinates of the focus window. */
static void report_area(const POINT *at, const RECT *exclude, LONG height)
{
    HWND focus = GetFocus();
    if (!focus)
        return;
    LONG area[5] = {at->x, at->y, at->x + 1, at->y + (height > 0 ? height : 16), at->x};
    if (exclude) {
        area[0] = exclude->left;
        area[1] = exclude->top;
        area[2] = exclude->right;
        area[3] = exclude->bottom;
    }
    NtUserMimeIme(MIME_IME_AREA, (ULONG_PTR)focus, 0, (PUCHAR)area, sizeof(area));
}

IMMAPI BOOL WINAPI ImmSetCompositionWindow(HIMC himc, LPCOMPOSITIONFORM form)
{
    if (!form)
        return FALSE;
    lock();
    IMM_CONTEXT *c = from_handle(himc);
    BOOL ok = c && c->Thread == GetCurrentThreadId();
    LONG height = 0;
    if (ok) {
        c->Composition = *form;
        height = c->HasFont ? (c->Font.lfHeight < 0 ? -c->Font.lfHeight : c->Font.lfHeight) : 0;
    }
    unlock();
    if (ok && form->dwStyle != CFS_DEFAULT)
        report_area(&form->ptCurrentPos, (form->dwStyle & CFS_RECT) ? &form->rcArea : nullptr, height);
    return ok;
}

IMMAPI BOOL WINAPI ImmGetCandidateWindow(HIMC himc, DWORD index, LPCANDIDATEFORM form)
{
    if (!form || index >= 4)
        return FALSE;
    lock();
    IMM_CONTEXT *c = from_handle(himc);
    BOOL ok = c && c->Candidates[index].dwIndex == index;
    if (ok)
        *form = c->Candidates[index];
    unlock();
    return ok;
}

IMMAPI BOOL WINAPI ImmSetCandidateWindow(HIMC himc, LPCANDIDATEFORM form)
{
    if (!form || form->dwIndex >= 4)
        return FALSE;
    lock();
    IMM_CONTEXT *c = from_handle(himc);
    BOOL ok = c && c->Thread == GetCurrentThreadId();
    LONG height = 0;
    if (ok) {
        c->Candidates[form->dwIndex] = *form;
        height = c->HasFont ? (c->Font.lfHeight < 0 ? -c->Font.lfHeight : c->Font.lfHeight) : 0;
    }
    unlock();
    /* a candidate window beside a spot is where the text is; one that excludes an area stays out of it. */
    if (ok && (form->dwStyle & (CFS_CANDIDATEPOS | CFS_EXCLUDE | CFS_POINT)))
        report_area(&form->ptCurrentPos, (form->dwStyle & CFS_EXCLUDE) ? &form->rcArea : nullptr, height);
    return ok;
}

IMMAPI BOOL WINAPI ImmGetCompositionFontW(HIMC himc, LPLOGFONTW font)
{
    if (!font)
        return FALSE;
    lock();
    IMM_CONTEXT *c = from_handle(himc);
    BOOL ok = c && c->HasFont;
    if (ok)
        *font = c->Font;
    unlock();
    return ok;
}

IMMAPI BOOL WINAPI ImmSetCompositionFontW(HIMC himc, LPLOGFONTW font)
{
    if (!font)
        return FALSE;
    lock();
    IMM_CONTEXT *c = from_handle(himc);
    BOOL ok = c && c->Thread == GetCurrentThreadId();
    if (ok) {
        c->Font = *font;
        c->HasFont = TRUE;
    }
    unlock();
    return ok;
}

IMMAPI BOOL WINAPI ImmGetOpenStatus(HIMC himc)
{
    lock();
    IMM_CONTEXT *c = from_handle(himc);
    BOOL open = c && c->Open;
    unlock();
    return open;
}

IMMAPI BOOL WINAPI ImmSetOpenStatus(HIMC himc, BOOL open)
{
    lock();
    IMM_CONTEXT *c = from_handle(himc);
    BOOL ok = c && c->Thread == GetCurrentThreadId();
    if (ok)
        c->Open = open != FALSE;
    unlock();
    return ok;
}

IMMAPI BOOL WINAPI ImmGetConversionStatus(HIMC himc, LPDWORD conversion, LPDWORD sentence)
{
    lock();
    IMM_CONTEXT *c = from_handle(himc);
    if (c) {
        if (conversion)
            *conversion = c->Conversion;
        if (sentence)
            *sentence = c->Sentence;
    }
    unlock();
    return c != nullptr;
}

IMMAPI BOOL WINAPI ImmSetConversionStatus(HIMC himc, DWORD conversion, DWORD sentence)
{
    lock();
    IMM_CONTEXT *c = from_handle(himc);
    BOOL ok = c && c->Thread == GetCurrentThreadId();
    if (ok) {
        c->Conversion = conversion;
        c->Sentence = sentence;
    }
    unlock();
    return ok;
}

/* the host's input method can only be told to complete or cancel the composition. */
IMMAPI BOOL WINAPI ImmNotifyIME(HIMC himc, DWORD action, DWORD index, DWORD value)
{
    if (action != NI_COMPOSITIONSTR || (index != CPS_COMPLETE && index != CPS_CANCEL) || !own_context(himc))
        return FALSE;
    return NtUserMimeIme(MIME_IME_NOTIFY, index, 0, nullptr, 0) != 0;
}

/* no ime processed the key, which Windows reports as VK_PROCESSKEY. */
IMMAPI UINT WINAPI ImmGetVirtualKey(HWND hwnd)
{
    return VK_PROCESSKEY;
}

/* Windows creates an "IME" class window per thread. mime has none, and callers handle NULL. */
IMMAPI HWND WINAPI ImmGetDefaultIMEWnd(HWND hwnd)
{
    return nullptr;
}

/* the layout has an input method when the desktop does. */
IMMAPI BOOL WINAPI ImmIsIME(HKL hkl)
{
    return NtUserMimeIme(MIME_IME_AVAILABLE, 0, 0, nullptr, 0) != 0;
}

IMMAPI DWORD WINAPI ImmGetProperty(HKL hkl, DWORD index)
{
    return 0;
}

IMMAPI BOOL WINAPI ImmDisableIME(DWORD thread)
{
    return TRUE;
}

IMMAPI BOOL WINAPI ImmIsUIMessageW(HWND ime, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return FALSE;
}

IMMAPI BOOL WINAPI ImmIsUIMessageA(HWND ime, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return ImmIsUIMessageW(ime, msg, wparam, lparam);
}

IMMAPI UINT WINAPI ImmGetDescriptionW(HKL hkl, LPWSTR buffer, UINT size)
{
    if (buffer && size)
        buffer[0] = 0;
    return 0;
}

IMMAPI UINT WINAPI ImmGetIMEFileNameW(HKL hkl, LPWSTR buffer, UINT size)
{
    if (buffer && size)
        buffer[0] = 0;
    return 0;
}

/* no layout has an editor here, so the name is empty. */
IMMAPI UINT WINAPI ImmGetIMEFileNameA(HKL hkl, LPSTR buffer, UINT size)
{
    WCHAR wide[MAX_PATH];
    UINT length = ImmGetIMEFileNameW(hkl, wide, MAX_PATH);
    if (!length || !size) {
        if (buffer && size)
            buffer[0] = 0;
        return 0;
    }
    int bytes = WideCharToMultiByte(CP_ACP, 0, wide, (int)length, buffer, size > 1 ? (int)size - 1 : 0, nullptr, nullptr);
    if (buffer)
        buffer[bytes] = 0;
    return (UINT)bytes;
}

IMMAPI LRESULT WINAPI ImmEscapeW(HKL hkl, HIMC himc, UINT escape, LPVOID data)
{
    return 0;
}

IMMAPI DWORD WINAPI ImmGetCandidateListW(HIMC himc, DWORD index, LPCANDIDATELIST list, DWORD size)
{
    return 0;
}

IMMAPI DWORD WINAPI ImmGetCandidateListCountW(HIMC himc, LPDWORD count)
{
    if (count)
        *count = 0;
    return 0;
}

IMMAPI DWORD WINAPI ImmGetGuideLineW(HIMC himc, DWORD index, LPWSTR buffer, DWORD size)
{
    return 0;
}

IMMAPI BOOL WINAPI ImmConfigureIMEW(HKL hkl, HWND hwnd, DWORD mode, LPVOID data)
{
    return FALSE;
}

IMMAPI BOOL WINAPI ImmSimulateHotKey(HWND hwnd, DWORD id)
{
    return FALSE;
}

IMMAPI LRESULT WINAPI ImmRequestMessageW(HIMC himc, WPARAM wparam, LPARAM lparam)
{
    return 0;
}

IMMAPI BOOL WINAPI ImmGenerateMessage(HIMC himc)
{
    lock();
    BOOL ok = from_handle(himc) != nullptr;
    unlock();
    return ok;
}
