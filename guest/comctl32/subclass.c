/*
 * the first SetWindowSubclass on a window replaces its window procedure with master_proc, which calls the
 * subclass procedures from newest to oldest. each reaches the next with DefSubclassProc, and the last one calls
 * the original window procedure. a frame per call on the thread remembers how far the chain went, so nested and
 * reentrant messages each walk their own chain.
 */
#include "comctl32_private.h"

typedef struct _SUBCLASS {
    struct _SUBCLASS *Next;
    SUBCLASSPROC Proc;
    UINT_PTR Id;
    DWORD_PTR RefData;
    BOOL Removed;
} SUBCLASS;

typedef struct _SUBCLASS_HEADER {
    struct _SUBCLASS_HEADER *NextWindow;
    HWND Window;
    WNDPROC Original;
    /* newest first. */
    SUBCLASS *First;
    /* calls into the chain in progress. removed entries are freed only when there are none. */
    ULONG Calls;
} SUBCLASS_HEADER;

typedef struct _SUBCLASS_FRAME {
    struct _SUBCLASS_FRAME *Previous;
    SUBCLASS_HEADER *Header;
    SUBCLASS *Next;
} SUBCLASS_FRAME;

static SRWLOCK g_lock = SRWLOCK_INIT;
static SUBCLASS_HEADER *g_headers;
static DWORD g_frame_tls = TLS_OUT_OF_INDEXES;

static LRESULT CALLBACK master_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

void SubclassProcessAttach(void)
{
    g_frame_tls = TlsAlloc();
}

static SUBCLASS_HEADER *find_header(HWND hwnd)
{
    SUBCLASS_HEADER *header;
    AcquireSRWLockExclusive(&g_lock);
    for (header = g_headers; header && header->Window != hwnd; header = header->NextWindow)
        ;
    ReleaseSRWLockExclusive(&g_lock);
    return header;
}

static void unlink_header(SUBCLASS_HEADER *header)
{
    AcquireSRWLockExclusive(&g_lock);
    for (SUBCLASS_HEADER **link = &g_headers; *link; link = &(*link)->NextWindow) {
        if (*link == header) {
            *link = header->NextWindow;
            break;
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
    HeapFree(GetProcessHeap(), 0, header);
}

static SUBCLASS *find_entry(SUBCLASS_HEADER *header, SUBCLASSPROC proc, UINT_PTR id)
{
    for (SUBCLASS *entry = header->First; entry; entry = entry->Next)
        if (!entry->Removed && entry->Proc == proc && entry->Id == id)
            return entry;
    return nullptr;
}

/* frees removed entries, and the header with the hook once no subclass is left. */
static void cleanup(SUBCLASS_HEADER *header)
{
    if (header->Calls)
        return;
    for (SUBCLASS **link = &header->First; *link;) {
        SUBCLASS *entry = *link;
        if (entry->Removed) {
            *link = entry->Next;
            HeapFree(GetProcessHeap(), 0, entry);
        } else {
            link = &entry->Next;
        }
    }
    if (header->First)
        return;
    /* another procedure installed on top keeps calling master_proc, which then just passes through. */
    if (IsWindow(header->Window)) {
        if ((WNDPROC)GetWindowLongPtrW(header->Window, GWLP_WNDPROC) != master_proc)
            return;
        SetWindowLongPtrW(header->Window, GWLP_WNDPROC, (LONG_PTR)header->Original);
    }
    unlink_header(header);
}

static LRESULT call_next(SUBCLASS_FRAME *frame, HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    while (frame->Next && frame->Next->Removed)
        frame->Next = frame->Next->Next;
    SUBCLASS *entry = frame->Next;
    if (!entry)
        return CallWindowProcW(frame->Header->Original, hwnd, msg, wparam, lparam);
    frame->Next = entry->Next;
    return entry->Proc(hwnd, msg, wparam, lparam, entry->Id, entry->RefData);
}

static LRESULT CALLBACK master_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    SUBCLASS_HEADER *header = find_header(hwnd);
    if (!header)
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    SUBCLASS_FRAME frame = {TlsGetValue(g_frame_tls), header, header->First};
    TlsSetValue(g_frame_tls, &frame);
    header->Calls++;
    LRESULT result = call_next(&frame, hwnd, msg, wparam, lparam);
    header->Calls--;
    TlsSetValue(g_frame_tls, frame.Previous);
    if (msg == WM_NCDESTROY) {
        /* the window is gone, so drop whatever the subclasses did not remove. */
        for (SUBCLASS *entry = header->First; entry; entry = entry->Next)
            entry->Removed = TRUE;
    }
    cleanup(header);
    return result;
}

WINCOMMCTRLAPI BOOL WINAPI SetWindowSubclass(HWND hwnd, SUBCLASSPROC proc, UINT_PTR id, DWORD_PTR ref_data)
{
    if (!proc || !hwnd || !IsWindow(hwnd) || GetWindowThreadProcessId(hwnd, nullptr) != GetCurrentThreadId())
        return FALSE;
    SUBCLASS_HEADER *header = find_header(hwnd);
    if (header) {
        SUBCLASS *entry = find_entry(header, proc, id);
        if (entry) {
            entry->RefData = ref_data;
            return TRUE;
        }
    } else {
        header = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*header));
        if (!header)
            return FALSE;
        header->Window = hwnd;
        header->Original = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
        if (!header->Original) {
            HeapFree(GetProcessHeap(), 0, header);
            return FALSE;
        }
        AcquireSRWLockExclusive(&g_lock);
        header->NextWindow = g_headers;
        g_headers = header;
        ReleaseSRWLockExclusive(&g_lock);
        SetLastError(0);
        WNDPROC previous = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)master_proc);
        if (!previous && GetLastError()) {
            unlink_header(header);
            return FALSE;
        }
        if (previous)
            header->Original = previous;
    }
    SUBCLASS *entry = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*entry));
    if (!entry) {
        cleanup(header);
        return FALSE;
    }
    entry->Proc = proc;
    entry->Id = id;
    entry->RefData = ref_data;
    entry->Next = header->First;
    header->First = entry;
    return TRUE;
}

WINCOMMCTRLAPI BOOL WINAPI GetWindowSubclass(HWND hwnd, SUBCLASSPROC proc, UINT_PTR id, DWORD_PTR *ref_data)
{
    SUBCLASS_HEADER *header = hwnd ? find_header(hwnd) : nullptr;
    SUBCLASS *entry = header ? find_entry(header, proc, id) : nullptr;
    if (ref_data)
        *ref_data = entry ? entry->RefData : 0;
    return entry != nullptr;
}

WINCOMMCTRLAPI BOOL WINAPI RemoveWindowSubclass(HWND hwnd, SUBCLASSPROC proc, UINT_PTR id)
{
    SUBCLASS_HEADER *header = hwnd ? find_header(hwnd) : nullptr;
    SUBCLASS *entry = header ? find_entry(header, proc, id) : nullptr;
    if (!entry)
        return FALSE;
    entry->Removed = TRUE;
    cleanup(header);
    return TRUE;
}

WINCOMMCTRLAPI LRESULT WINAPI DefSubclassProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    SUBCLASS_FRAME *frame = g_frame_tls != TLS_OUT_OF_INDEXES ? TlsGetValue(g_frame_tls) : nullptr;
    for (; frame; frame = frame->Previous)
        if (frame->Header->Window == hwnd)
            return call_next(frame, hwnd, msg, wparam, lparam);
    /* called outside a subclass procedure: go straight to the original one. */
    SUBCLASS_HEADER *header = find_header(hwnd);
    if (header)
        return CallWindowProcW(header->Original, hwnd, msg, wparam, lparam);
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
