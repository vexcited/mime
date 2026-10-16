/*
 * imm32: input contexts for a keyboard layout without an input method
 * editor. contexts exist and keep their windows, positions, fonts and
 * status like windows, but nothing ever composes: composition strings are
 * empty, notifications to the ime fail and there is no ime window.
 */
#define MIME_BUILD_IMM32
#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>
#include <mime/win32/imm.h>

WINUSERAPI BOOL WINAPI SetPropW(HWND hwnd, LPCWSTR name, HANDLE data);
WINUSERAPI HANDLE WINAPI GetPropW(HWND hwnd, LPCWSTR name);
WINUSERAPI HANDLE WINAPI RemovePropW(HWND hwnd, LPCWSTR name);
WINUSERAPI DWORD WINAPI GetWindowThreadProcessId(HWND hwnd, LPDWORD pid);

/* the window property holding the associated context; 1 stands for an explicit NULL association. */
#define IMM_PROP L"MimeImmContext"
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

/* the context of a handle, with the lock held. */
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

/* the default context of a thread, created on first use. */
static HIMC thread_context(DWORD thread)
{
    for (ULONG i = 0; i < g_imm.Count; i++)
        if (g_imm.Contexts[i].Used && g_imm.Contexts[i].Default && g_imm.Contexts[i].Thread == thread)
            return to_handle(i);
    return new_context(thread, TRUE);
}

/* the context a window uses, with the lock held. FALSE for windows of other processes. */
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

/* nothing is ever being composed: every composition string is empty. */
IMMAPI LONG WINAPI ImmGetCompositionStringW(HIMC himc, DWORD index, LPVOID buffer, DWORD size)
{
    return 0;
}

IMMAPI LONG WINAPI ImmGetCompositionStringA(HIMC himc, DWORD index, LPVOID buffer, DWORD size)
{
    return 0;
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

IMMAPI BOOL WINAPI ImmSetCompositionWindow(HIMC himc, LPCOMPOSITIONFORM form)
{
    if (!form)
        return FALSE;
    lock();
    IMM_CONTEXT *c = from_handle(himc);
    BOOL ok = c && c->Thread == GetCurrentThreadId();
    if (ok)
        c->Composition = *form;
    unlock();
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
    if (ok)
        c->Candidates[form->dwIndex] = *form;
    unlock();
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

/* the keyboard layout has no ime to notify. */
IMMAPI BOOL WINAPI ImmNotifyIME(HIMC himc, DWORD action, DWORD index, DWORD value)
{
    return FALSE;
}

/* no ime processed the key, which windows reports as VK_PROCESSKEY. */
IMMAPI UINT WINAPI ImmGetVirtualKey(HWND hwnd)
{
    return VK_PROCESSKEY;
}

/* windows creates an "IME" class window per thread; mime has none, which callers handle as NULL. */
IMMAPI HWND WINAPI ImmGetDefaultIMEWnd(HWND hwnd)
{
    return nullptr;
}

IMMAPI BOOL WINAPI ImmIsIME(HKL hkl)
{
    return FALSE;
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
