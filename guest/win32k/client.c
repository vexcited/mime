/*
 * the calls of win32k into user32, as win32kfull of windows 11 makes them and the genuine user32 of 26300.9457 takes
 * them: KeUserModeCallback with an index of the apfnDispatch table user32 puts in the peb, and a message laid out for
 * the function at that index. read in IDA from user32 of 26300.9457 and win32kfull of 26100.9974.
 *
 * a window procedure is called through the dispatcher of the client procedures ntdll keeps, DispatchClientMessage
 * of user32, with the procedure as the extra parameter. it finds the handle of the window in the callback window
 * of the client information of the teb, which win32k sets around every call. each message starts with the address
 * of the window in the heap of the desktop as the program sees it, then the message and its parameters.
 *
 * the answer of every call is 24 bytes: the result, then the size and the address of what the procedure wrote, for
 * the messages that give something back.
 *
 * a message that carries strings starts with a capture buffer of 40 bytes: its size, the room left, the number of
 * pointers, the next free byte, the offset of the table of pointers and the address of the data when it is not in
 * the message. a pointer of such a message holds an offset from the start of the message, and the table lists the
 * offsets of those pointers, which user32 turns into addresses. data over 0x800 bytes goes in memory of the
 * process instead, and its pointers are addresses.
 */
#include "win32k.h"

#include "client.inc"

#define CI_FLAGS 0x00
#define CI_CALLBACK_WND 0x40
#define CI_INITTHREAD 0x8u

/* the indexes of apfnDispatch. */
enum {
    FI_CLIENTCOPYIMAGE = 64,
    FI_CLIENTLOADLIBRARY = 75,
    FI_COPYDATA = 0,
    FI_DWORD = 2,
    FI_NCDESTROY = 3,
    FI_DWORDOPTINLPMSG = 4,
    FI_GETTEXTLENGTHS = 6,
    FI_INLPCOMPAREITEMSTRUCT = 9,
    FI_INLPCREATESTRUCT = 10,
    FI_INLPDELETEITEMSTRUCT = 11,
    FI_INLPDRAWITEMSTRUCT = 12,
    FI_INOUTLPMEASUREITEMSTRUCT = 16,
    FI_INLPWINDOWPOS = 17,
    FI_INOUTLPPOINT5 = 18,
    FI_INOUTLPSCROLLINFO = 19,
    FI_INOUTLPRECT = 20,
    FI_INOUTNCCALCSIZE = 21,
    FI_INOUTLPWINDOWPOS = 22,
    FI_INSTRING = 26,
    FI_INSTRINGNULL = 27,
    FI_INOUTNEXTMENU = 30,
    FI_OPTOUTLPDWORDOPTOUTLPDWORD = 31,
    FI_OUTDWORDINDWORD = 33,
    FI_OUTLPRECT = 34,
    FI_OUTSTRING = 35,
    FI_INOUTSTYLECHANGE = 39,
    FI_CLIENTTHREADSETUP = 84,
    FI_CLIENTMONITORENUMPROC = 87,
    FI_INWPARAMDBCSCHAR = 95,
    FI_INOUTMENUGETOBJECT = 102,
    FI_OUTLPCOMBOBOXINFO = 104,
    FI_OUTLPSCROLLBARINFO = 105,
    FI_EMPTY = 117,
    FI_INOUTLPSIZE = 126,
};

/* the functions of gapfnScSendMessage, which g_message_fn names. */
enum {
    SFN_DWORD = 0,
    SFN_NCDESTROY = 1,
    SFN_INLPCREATESTRUCT = 2,
    SFN_INSTRINGNULL = 3,
    SFN_OUTSTRING = 4,
    SFN_INSTRING = 5,
    SFN_INOUTLPPOINT5 = 6,
    SFN_INLPDRAWITEMSTRUCT = 7,
    SFN_INOUTLPMEASUREITEMSTRUCT = 8,
    SFN_INLPDELETEITEMSTRUCT = 9,
    SFN_INWPARAMCHAR = 10,
    SFN_INLPCOMPAREITEMSTRUCT = 12,
    SFN_INOUTLPWINDOWPOS = 13,
    SFN_INLPWINDOWPOS = 14,
    SFN_COPYDATA = 16,
    SFN_INOUTSTYLECHANGE = 19,
    SFN_INOUTNCCALCSIZE = 20,
    SFN_DWORDOPTINLPMSG = 21,
    SFN_INOUTLPSIZE = 22,
    SFN_INOUTLPRECT = 23,
    SFN_OPTOUTLPDWORDOPTOUTLPDWORD = 24,
    SFN_OUTLPRECT = 25,
    SFN_INOUTLPSCROLLINFO = 28,
    SFN_OUTDWORDINDWORD = 34,
    SFN_INOUTNEXTMENU = 35,
    SFN_DWORD2 = 43,
    SFN_DWORD3 = 44,
    SFN_GETDBCSTEXTLENGTHS = 46,
    SFN_OPTOUTLPDWORDOPTOUTLPDWORD2 = 47,
    SFN_DWORD4 = 48,
    SFN_INWPARAMDBCSCHAR = 49,
    SFN_OPTOUTLPDWORDOPTOUTLPDWORD3 = 50,
    SFN_INOUTMENUGETOBJECT = 52,
    SFN_OUTLPCOMBOBOXINFO = 54,
    SFN_OUTLPSCROLLBARINFO = 55,
    SFN_DWORD5 = 65,
    SFN_DWORD6 = 66,
    SFN_EMPTY = 72,
};

typedef struct CALLBACK_WND {
    ULONG64 Hwnd, Pwnd, ActCtx;
} CALLBACK_WND;

typedef struct CALLBACK_RESULT {
    LONG64 Result;
    ULONG OutputSize, Pad;
    ULONG64 Output;
} CALLBACK_RESULT;

typedef struct CAPTUREBUF {
    ULONG Size;     /* of the message, or of its head when the data is elsewhere. */
    ULONG Room;     /* left for data. */
    ULONG Pointers;
    ULONG Pad;
    ULONG64 Free;   /* of win32k only, 0 in the call. */
    ULONG PointerTable;
    ULONG Pad2;
    ULONG64 Remote; /* the data in memory of the process, or 0. */
} CAPTUREBUF;
MIME_STATIC_ASSERT(sizeof(CAPTUREBUF) == 40, "capture buffer");

#define CAPTURE_INLINE_MAX 0x800

/* ------------------------------------------------------------------ the dispatcher of the client procedures */

static ULONG64 g_pfn[MIME_NTUSER_PFN_COUNT];
static BOOLEAN g_pfn_known;

/* the procedure of ntdll that stands for one of user32: ansi ones come first, then unicode ones, 24 each. */
ULONG64 ClientPfn(ULONG index)
{
    if (!g_pfn_known && NT_SUCCESS(PsQueryNtUserPfn(g_pfn)))
        g_pfn_known = TRUE;
    return g_pfn_known && index < MIME_NTUSER_PFN_COUNT ? g_pfn[index] : 0;
}

#define PFN_DISPATCH_MESSAGE 21

static ULONG64 dispatcher(BOOLEAN ansi)
{
    return ClientPfn((ansi ? 0 : 24) + PFN_DISPATCH_MESSAGE);
}

/* ------------------------------------------------------------------ the callback window of the teb */

static PUCHAR client_info(void)
{
    PUCHAR teb = PsGetCurrentThreadTeb();
    return teb ? teb + FIELD_OFFSET(TEB, Win32ClientInfo) : nullptr;
}

static void set_callback_window(const CALLBACK_WND *now, CALLBACK_WND *saved)
{
    PUCHAR ci = client_info();
    if (!ci)
        return;
    if (saved && !NT_SUCCESS(MimeCopyFromUser(saved, ci + CI_CALLBACK_WND, sizeof(*saved))))
        memset(saved, 0, sizeof(*saved));
    MimeCopyToUser(ci + CI_CALLBACK_WND, now, sizeof(*now));
}

/*
 * calls user32 with the lock of the window manager let go, as windows does, and gives the result back. the window
 * is held over the call and is the callback window of the thread while it runs.
 */
static NTSTATUS call_client(WND *w, ULONG index, PVOID message, ULONG length, CALLBACK_RESULT *result)
{
    W32THREAD *t = W32GetThread();
    memset(result, 0, sizeof(*result));
    CALLBACK_WND now = {}, saved;
    if (w) {
        now.Hwnd = (ULONG64)(ULONG_PTR)WndHandle(w);
        now.Pwnd = DeskUserWindow(w);
        UserReference(&w->Header);
    }
    if (t)
        t->CallbackDepth++;
    ULONG depth = UserLeaveAll();
    set_callback_window(&now, &saved);
    PVOID out = nullptr;
    ULONG out_len = 0;
    NTSTATUS status = KeUserModeCallback(index, message, length, &out, &out_len);
    set_callback_window(&saved, nullptr);
    UserReenter(depth);
    if (t)
        t->CallbackDepth--;
    if (w)
        UserDereference(&w->Header);
    if (NT_SUCCESS(status) && (out_len != sizeof(*result) || !NT_SUCCESS(MimeCopyFromUser(result, out, sizeof(*result)))))
        status = STATUS_UNSUCCESSFUL;
    return status;
}

/* ------------------------------------------------------------------ the thread */

/*
 * xxxCreateThreadInfo calls ClientThreadSetup of user32 when a thread of a program becomes one of the window
 * manager: user32 sets up gdi for its first thread, marks the client information as ready and loads the
 * libraries of AppInit. a program without user32 has no table of callbacks and is not called.
 */
void ClientThreadSetup(void)
{
    PPEB peb = PsGetProcessPeb(PsGetCurrentProcess());
    PVOID table = nullptr;
    if (!peb || !NT_SUCCESS(MimeCopyFromUser(&table, &peb->KernelCallbackTable, sizeof(table))) || !table)
        return;
    PUCHAR ci = client_info();
    ULONG64 flags = 0;
    if (!ci || !NT_SUCCESS(MimeCopyFromUser(&flags, ci + CI_FLAGS, sizeof(flags))) || (flags & CI_INITTHREAD))
        return;
    PVOID out = nullptr;
    ULONG out_len = 0;
    NTSTATUS status = KeUserModeCallback(FI_CLIENTTHREADSETUP, nullptr, 0, &out, &out_len);
    if (!NT_SUCCESS(status))
        DbgPrint("win32k: ClientThreadSetup of thread %u failed: %08x\n", (ULONG)(ULONG_PTR)PsGetCurrentThreadId(),
                 status);
}

/*
 * xxxClientMonitorEnumProc of win32kfull: the monitor, the dc, the rectangle and the lparam, then the procedure,
 * 48 bytes. __ClientMonitorEnumProc of user32 answers 24 bytes with the result first. anything else stops the
 * enumeration.
 */
BOOL ClientMonitorEnumProc(HMONITOR monitor, HDC dc, const RECT *rect, LPARAM lparam, ULONG64 proc)
{
    struct {
        ULONG64 Monitor, Dc;
        RECT Rect;
        ULONG64 Lparam, Proc;
    } m = {(ULONG64)(ULONG_PTR)monitor, (ULONG64)(ULONG_PTR)dc, *rect, (ULONG64)lparam, proc};
    MIME_STATIC_ASSERT(sizeof(m) == 48, "monitor enumeration message");
    CALLBACK_RESULT r;
    if (!NT_SUCCESS(call_client(nullptr, FI_CLIENTMONITORENUMPROC, &m, sizeof(m), &r)))
        return FALSE;
    return (BOOL)r.Result;
}

/* __ClientCopyImage of user32: InternalCopyImage of an image at a size, with the LR_ flags given. */
HANDLE ClientCopyImage(HANDLE image, UINT type, int cx, int cy, UINT flags)
{
    struct {
        ULONG64 Image;
        ULONG Type;
        LONG Cx, Cy;
        ULONG Flags;
    } m = {(ULONG64)(ULONG_PTR)image, type, cx, cy, flags};
    MIME_STATIC_ASSERT(sizeof(m) == 24, "copy image message");
    CALLBACK_RESULT r;
    if (!NT_SUCCESS(call_client(nullptr, FI_CLIENTCOPYIMAGE, &m, sizeof(m), &r)))
        return nullptr;
    return (HANDLE)(ULONG_PTR)r.Result;
}

/* ------------------------------------------------------------------ strings */

/* the bytes of a wide string in the charset of the receiver, its terminator included. */
static ULONG string_bytes(PCWSTR text, ULONG chars, BOOLEAN ansi)
{
    return ansi ? chars + 1 : (chars + 1) * (ULONG)sizeof(WCHAR);
}

static void string_put(PUCHAR to, PCWSTR text, ULONG chars, BOOLEAN ansi)
{
    if (ansi) {
        RtlUnicodeToMultiByteN((PCHAR)to, chars, nullptr, text, chars * sizeof(WCHAR));
        to[chars] = 0;
    } else {
        memcpy(to, text, chars * sizeof(WCHAR));
        ((PWCHAR)to)[chars] = 0;
    }
}

/* ------------------------------------------------------------------ captured messages */

typedef struct CAPTURE {
    PUCHAR Message;
    ULONG Length;   /* of what is passed: the fields, the table of pointers and the data when it is in the message. */
    ULONG Free;     /* the next byte of data, from the start of the message or of the remote data. */
    ULONG End;
    PUCHAR Remote;  /* the data in memory of the process, over 0x800 bytes. */
    alignas(8) UCHAR Inline[256]; /* most messages fit, and need no pool. */
} CAPTURE;

/* AllocCallbackMessage: the fields, room for a pointer of 8 bytes per pointer, then the data. */
static BOOLEAN capture_begin(CAPTURE *c, ULONG head, ULONG pointers, ULONG data)
{
    memset(c, 0, sizeof(*c));
    ULONG fields = (head + 7) & ~7u, table = fields + 8 * pointers;
    data = (data + 7 * pointers) & ~7u;
    if (data > CAPTURE_INLINE_MAX) {
        PVOID base = nullptr;
        SIZE_T size = data;
        if (!NT_SUCCESS(ZwAllocateVirtualMemory((HANDLE)-1, &base, 0, &size, MEM_COMMIT, PAGE_READWRITE)))
            return FALSE;
        c->Remote = base;
        c->Length = table;
        c->End = data;
    } else {
        c->Length = table + data;
        c->Free = table;
        c->End = table + data;
    }
    c->Message = c->Length <= sizeof(c->Inline) ? c->Inline : W32Alloc(c->Length);
    if (!c->Message) {
        if (c->Remote) {
            SIZE_T size = 0;
            PVOID base = c->Remote;
            ZwFreeVirtualMemory((HANDLE)-1, &base, &size, MEM_RELEASE);
        }
        return FALSE;
    }
    CAPTUREBUF *b = (CAPTUREBUF *)c->Message;
    b->Size = c->Length;
    b->Room = data;
    b->PointerTable = fields;
    b->Remote = (ULONG64)(ULONG_PTR)c->Remote;
    return TRUE;
}

/* CaptureCallbackData: copies bytes into the data and points a field of the message at them. */
static BOOLEAN capture_put(CAPTURE *c, ULONG field, const void *bytes, ULONG size)
{
    CAPTUREBUF *b = (CAPTUREBUF *)c->Message;
    if (c->Free + size > c->End)
        return FALSE;
    ULONG at = c->Free;
    c->Free += (size + 7) & ~7u;
    if (c->Remote) {
        *(PULONG64)(c->Message + field) = (ULONG64)(ULONG_PTR)(c->Remote + at);
        return NT_SUCCESS(MimeCopyToUser(c->Remote + at, bytes, size));
    }
    memcpy(c->Message + at, bytes, size);
    *(PULONG64)(c->Message + field) = at;
    *(PULONG)(c->Message + b->PointerTable + 4 * b->Pointers++) = field;
    return TRUE;
}

static void capture_end(CAPTURE *c)
{
    if (c->Remote) {
        SIZE_T size = 0;
        PVOID base = c->Remote;
        ZwFreeVirtualMemory((HANDLE)-1, &base, &size, MEM_RELEASE);
    }
    if (c->Message != c->Inline)
        W32Free(c->Message);
}

/* ------------------------------------------------------------------ libraries */

/*
 * ClientLoadLibrary of win32kfull 26300: user32 loads a library and, given the name of a function, calls it as the
 * initialization of an api hook. two counted strings follow the capture buffer, the path of the library and the name
 * of the function; the answer is the module, or 0 when the library or the hook failed.
 */
HMODULE ClientLoadLibrary(PCWSTR library, PCWSTR init)
{
    enum { LIBRARY = 40, INIT = 56, HEAD = 72 };
    ULONG lib_bytes = (ULONG)(wcslen(library) + 1) * sizeof(WCHAR), init_bytes = (ULONG)(wcslen(init) + 1) * sizeof(WCHAR);
    CAPTURE c;
    if (!capture_begin(&c, HEAD, 2, lib_bytes + init_bytes))
        return nullptr;
    PUCHAR m = c.Message;
    *(PUSHORT)(m + LIBRARY) = (USHORT)(lib_bytes - sizeof(WCHAR));
    *(PUSHORT)(m + LIBRARY + 2) = (USHORT)lib_bytes;
    *(PUSHORT)(m + INIT) = (USHORT)(init_bytes - sizeof(WCHAR));
    *(PUSHORT)(m + INIT + 2) = (USHORT)init_bytes;
    HMODULE module = nullptr;
    CALLBACK_RESULT r;
    if (capture_put(&c, LIBRARY + 8, library, lib_bytes) && capture_put(&c, INIT + 8, init, init_bytes) &&
        NT_SUCCESS(call_client(nullptr, FI_CLIENTLOADLIBRARY, m, c.Length, &r)))
        module = (HMODULE)(ULONG_PTR)r.Result;
    capture_end(&c);
    return module;
}

/* ------------------------------------------------------------------ window procedures */

typedef struct MSG_HEAD {
    ULONG64 Pwnd;
    ULONG Msg, Pad;
    ULONG64 Wparam;
} MSG_HEAD;

/* SfnDWORD and SfnNCDESTROY: the parameters as they are. */
typedef struct FNDWORDMSG {
    MSG_HEAD Head;
    ULONG64 Lparam, Xparam, Xpfn;
} FNDWORDMSG;
MIME_STATIC_ASSERT(sizeof(FNDWORDMSG) == 48, "dword message");

/* SfnGETDBCSTEXTLENGTHS and SfnOUTLPRECT: no lparam. */
typedef struct FNNOLPARAMMSG {
    MSG_HEAD Head;
    ULONG64 Xparam, Xpfn;
} FNNOLPARAMMSG;

static void head_of(MSG_HEAD *h, ULONG64 pwnd, UINT msg, WPARAM wparam)
{
    h->Pwnd = pwnd;
    h->Msg = msg;
    h->Pad = 0;
    h->Wparam = wparam;
}

static BOOLEAN g_reported[0x400 / 8];

static LRESULT unsupported(UINT msg, ULONG kind)
{
    if (msg < 0x400 && !(g_reported[msg / 8] & (1u << (msg % 8)))) {
        g_reported[msg / 8] |= (UCHAR)(1u << (msg % 8));
        DbgPrint("win32k: no call of user32 for message %x with data of kind %u, function %u\n", msg, kind,
                 g_message_fn[msg]);
    }
    return 0;
}

/*
 * the messages of a structure placed after the parameters, with the extra parameter and the dispatcher after it:
 * the part of the message that comes back is the structure.
 */
static LRESULT call_struct(WND *w, ULONG index, ULONG64 pwnd, UINT msg, WPARAM wparam, PVOID data, ULONG size,
                           ULONG slot, BOOLEAN back, ULONG64 xparam, ULONG64 xpfn)
{
    UCHAR message[24 + 64 + 16] = {};
    if (slot > 64)
        return 0;
    head_of((MSG_HEAD *)message, pwnd, msg, wparam);
    if (data)
        memcpy(message + 24, data, size < slot ? size : slot);
    *(PULONG64)(message + 24 + slot) = xparam;
    *(PULONG64)(message + 32 + slot) = xpfn;
    CALLBACK_RESULT r;
    if (!NT_SUCCESS(call_client(w, index, message, 40 + slot, &r)))
        return 0;
    if (back && data && r.Output && r.OutputSize) {
        ULONG n = r.OutputSize < size ? r.OutputSize : size;
        MimeCopyFromUser(data, (PVOID)(ULONG_PTR)r.Output, n);
    }
    return (LRESULT)r.Result;
}

static LRESULT call_createstruct(WND *w, ULONG64 pwnd, UINT msg, WPARAM wparam, PUCHAR data, ULONG size,
                                 BOOLEAN ansi, ULONG64 xparam, ULONG64 xpfn)
{
    enum { CS = 72, NAME = CS + 56, CLASS = CS + 64, XPARAM = 152, XPFN = 160, HEAD = 168 };
    CREATESTRUCTW cs;
    memcpy(&cs, data, sizeof(cs));
    /* the strings follow the structure and are named by their offsets; a class made by its atom is that atom. */
    ULONG_PTR name_at = (ULONG_PTR)cs.lpszName, class_at = (ULONG_PTR)cs.lpszClass;
    PCWSTR name = name_at >= sizeof(cs) && name_at < size ? (PCWSTR)(data + name_at) : nullptr;
    PCWSTR klass = class_at >= sizeof(cs) && class_at < size ? (PCWSTR)(data + class_at) : nullptr;
    ULONG name_chars = name ? (ULONG)wcslen(name) : 0, class_chars = klass ? (ULONG)wcslen(klass) : 0;
    /* a name made of 0xffff and the number of a resource is copied as those 4 bytes, or 3 for an ansi window. */
    BOOLEAN ordinal = name && name[0] == 0xFFFF;
    ULONG name_size = !name ? 0 : ordinal ? (ansi ? 3 : 4) : string_bytes(name, name_chars, ansi);
    ULONG class_size = klass ? string_bytes(klass, class_chars, ansi) : 0;
    CAPTURE c;
    if (!capture_begin(&c, HEAD, (name ? 1 : 0) + (klass ? 1 : 0), name_size + class_size))
        return 0;
    PUCHAR m = c.Message;
    head_of((MSG_HEAD *)(m + 40), pwnd, msg, wparam);
    *(PULONG64)(m + 64) = 1;
    *(PULONG64)(m + CS + 0) = (ULONG64)(ULONG_PTR)cs.lpCreateParams;
    *(PULONG64)(m + CS + 8) = (ULONG64)(ULONG_PTR)cs.hInstance;
    *(PULONG64)(m + CS + 16) = (ULONG64)(ULONG_PTR)cs.hMenu;
    *(PULONG64)(m + CS + 24) = (ULONG64)(ULONG_PTR)cs.hwndParent;
    *(PLONG)(m + CS + 32) = cs.cy;
    *(PLONG)(m + CS + 36) = cs.cx;
    *(PLONG)(m + CS + 40) = cs.y;
    *(PLONG)(m + CS + 44) = cs.x;
    *(PLONG)(m + CS + 48) = cs.style;
    *(PULONG)(m + CS + 72) = cs.dwExStyle;
    *(PULONG64)(m + CLASS) = klass ? 0 : class_at;
    BOOLEAN ok = TRUE;
    UCHAR small[512];
    PUCHAR text = nullptr;
    if (name) {
        text = name_size <= sizeof(small) ? small : W32Alloc(name_size);
        if (!text) {
            ok = FALSE;
        } else if (ordinal) {
            if (ansi) {
                text[0] = 0xFF;
                memcpy(text + 1, &name[1], 2);
            } else {
                memcpy(text, name, 4);
            }
        } else {
            string_put(text, name, name_chars, ansi);
        }
        ok = ok && capture_put(&c, NAME, text, name_size);
        if (text && text != small)
            W32Free(text);
    }
    if (ok && klass) {
        text = class_size <= sizeof(small) ? small : W32Alloc(class_size);
        ok = text != nullptr;
        if (ok) {
            string_put(text, klass, class_chars, ansi);
            ok = capture_put(&c, CLASS, text, class_size);
        }
        if (text && text != small)
            W32Free(text);
    }
    LRESULT result = 0;
    *(PULONG64)(m + XPARAM) = xparam;
    *(PULONG64)(m + XPFN) = xpfn;
    CALLBACK_RESULT r;
    if (ok && NT_SUCCESS(call_client(w, FI_INLPCREATESTRUCT, m, c.Length, &r)))
        result = (LRESULT)r.Result;
    capture_end(&c);
    return result;
}

/* SfnINSTRING and SfnINSTRINGNULL: a string in the charset of the window. */
static LRESULT call_instring(WND *w, ULONG index, ULONG64 pwnd, UINT msg, WPARAM wparam, PCWSTR text, ULONG size,
                             BOOLEAN ansi, ULONG64 xparam, ULONG64 xpfn)
{
    enum { XPARAM = 64, XPFN = 72, LPARAM = 80, HEAD = 88 };
    ULONG chars = 0;
    if (text)
        while (chars < size / sizeof(WCHAR) && text[chars])
            chars++;
    ULONG bytes = text ? string_bytes(text, chars, ansi) : 0;
    CAPTURE c;
    if (!capture_begin(&c, HEAD, text ? 1 : 0, bytes))
        return 0;
    PUCHAR m = c.Message;
    head_of((MSG_HEAD *)(m + 40), pwnd, msg, wparam);
    *(PULONG64)(m + XPARAM) = xparam;
    *(PULONG64)(m + XPFN) = xpfn;
    BOOLEAN ok = TRUE;
    if (text) {
        PUCHAR copy = W32Alloc(bytes);
        ok = copy != nullptr;
        if (ok) {
            string_put(copy, text, chars, ansi);
            ok = capture_put(&c, LPARAM, copy, bytes);
            W32Free(copy);
        }
    }
    LRESULT result = 0;
    CALLBACK_RESULT r;
    if (ok && NT_SUCCESS(call_client(w, index, m, c.Length, &r)))
        result = (LRESULT)r.Result;
    capture_end(&c);
    return result;
}

/*
 * SfnOUTSTRING: room for wparam characters, which user32 fills in its own buffer or in the data of the message, and
 * gives back with the length it answers. what an ansi window writes is made wide here.
 */
static LRESULT call_outstring(WND *w, ULONG64 pwnd, UINT msg, WPARAM wparam, PWSTR out, ULONG size, BOOLEAN ansi,
                              ULONG64 xparam, ULONG64 xpfn)
{
    enum { XPARAM = 64, XPFN = 72, OUTPUT = 80, OUTPUT_SIZE = 88, HEAD = 96 };
    ULONG room = size;
    CAPTURE c;
    if (!capture_begin(&c, HEAD, 1, room + 2))
        return 0;
    PUCHAR m = c.Message;
    head_of((MSG_HEAD *)(m + 40), pwnd, msg, ansi ? wparam * 2 : wparam);
    *(PULONG64)(m + XPARAM) = xparam;
    *(PULONG64)(m + XPFN) = xpfn;
    *(PULONG)(m + OUTPUT_SIZE) = room + 2;
    PUCHAR zero = W32Alloc(room + 2);
    BOOLEAN ok = zero && capture_put(&c, OUTPUT, zero, room + 2);
    W32Free(zero);
    LRESULT result = 0;
    CALLBACK_RESULT r;
    if (ok && NT_SUCCESS(call_client(w, FI_OUTSTRING, m, c.Length, &r))) {
        result = (LRESULT)r.Result;
        ULONG chars = size / sizeof(WCHAR);
        if (out && chars && r.Output) {
            ULONG got = (ULONG)(result < 0 ? 0 : result);
            if (ansi) {
                ULONG n = got < chars - 1 ? got : chars - 1;
                PUCHAR bytes = W32Alloc(n + 1);
                if (bytes && NT_SUCCESS(MimeCopyFromUser(bytes, (PVOID)(ULONG_PTR)r.Output, n))) {
                    ULONG written = 0;
                    RtlMultiByteToUnicodeN(out, n * sizeof(WCHAR), &written, (const CHAR *)bytes, n);
                    out[written / sizeof(WCHAR)] = 0;
                    result = written / sizeof(WCHAR);
                } else {
                    out[0] = 0;
                    result = 0;
                }
                W32Free(bytes);
            } else {
                ULONG n = got < chars - 1 ? got : chars - 1;
                if (!NT_SUCCESS(MimeCopyFromUser(out, (PVOID)(ULONG_PTR)r.Output, n * sizeof(WCHAR))))
                    n = 0;
                out[n] = 0;
            }
        }
    }
    capture_end(&c);
    return result;
}

/*
 * SfnCOPYDATA: whether there is a COPYDATASTRUCT, then the structure with its data captured, as __fnCOPYDATA of
 * user32 reads them. data is what MsgCapture holds: the structure, whose lpData is not 0 when the bytes follow it.
 */
static LRESULT call_copydata(WND *w, ULONG64 pwnd, UINT msg, WPARAM wparam, PUCHAR data, ULONG size, ULONG64 xparam,
                             ULONG64 xpfn)
{
    enum { PRESENT = 64, COPY = 72, DATA = 88, XPARAM = 96, XPFN = 104, HEAD = 112 };
    COPYDATASTRUCT cd = {};
    BOOLEAN present = data && size >= sizeof(cd);
    if (present)
        memcpy(&cd, data, sizeof(cd));
    BOOLEAN pointer = present && cd.lpData;
    ULONG bytes = pointer && cd.cbData <= size - sizeof(cd) ? cd.cbData : 0;
    CAPTURE c;
    if (!capture_begin(&c, HEAD, pointer, bytes))
        return 0;
    PUCHAR m = c.Message;
    head_of((MSG_HEAD *)(m + 40), pwnd, msg, wparam);
    *(PULONG)(m + PRESENT) = present;
    *(PULONG64)(m + COPY) = cd.dwData;
    *(PULONG)(m + COPY + 8) = cd.cbData;
    *(PULONG64)(m + XPARAM) = xparam;
    *(PULONG64)(m + XPFN) = xpfn;
    LRESULT result = 0;
    CALLBACK_RESULT r;
    if ((!pointer || capture_put(&c, DATA, data + sizeof(cd), bytes)) &&
        NT_SUCCESS(call_client(w, FI_COPYDATA, m, c.Length, &r)))
        result = (LRESULT)r.Result;
    capture_end(&c);
    return result;
}

/* SfnINOUTNCCALCSIZE: a rectangle, or three with the position of the window after them. */
static LRESULT call_nccalcsize(WND *w, ULONG64 pwnd, UINT msg, WPARAM wparam, PUCHAR data, ULONG size,
                               ULONG64 xparam, ULONG64 xpfn)
{
    UCHAR m[136] = {};
    head_of((MSG_HEAD *)m, pwnd, msg, wparam);
    *(PULONG64)(m + 24) = xparam;
    *(PULONG64)(m + 32) = xpfn;
    ULONG length = 56;
    if (wparam && size >= sizeof(NCCALCSIZE_PARAMS) + sizeof(WINDOWPOS)) {
        memcpy(m + 40, data, 3 * sizeof(RECT));
        memcpy(m + 96, data + sizeof(NCCALCSIZE_PARAMS), sizeof(WINDOWPOS));
        *(PULONG)(m + 96 + 36) = 0;
        length = 136;
    } else if (size >= sizeof(RECT)) {
        memcpy(m + 40, data, sizeof(RECT));
    }
    CALLBACK_RESULT r;
    if (!NT_SUCCESS(call_client(w, FI_INOUTNCCALCSIZE, m, length, &r)))
        return 0;
    UCHAR back[96];
    if (r.Output && NT_SUCCESS(MimeCopyFromUser(back, (PVOID)(ULONG_PTR)r.Output, length == 136 ? 96 : 16))) {
        if (length == 136) {
            memcpy(data, back, 3 * sizeof(RECT));
            memcpy(data + sizeof(NCCALCSIZE_PARAMS), back + 56, sizeof(WINDOWPOS));
        } else {
            memcpy(data, back, sizeof(RECT));
        }
    }
    return (LRESULT)r.Result;
}

/* a character of wparam in the charset of the window. */
static WPARAM char_for(WPARAM wparam, BOOLEAN ansi)
{
    if (!ansi || (wparam & 0xFFFF) < 0x80)
        return wparam;
    WCHAR wide = (WCHAR)wparam;
    CHAR narrow = 0;
    RtlUnicodeToMultiByteN(&narrow, 1, nullptr, &wide, sizeof(wide));
    return (wparam & ~(WPARAM)0xFFFF) | (UCHAR)narrow;
}

/*
 * a window procedure of user mode, for a message win32k holds the data of: lparam points at size bytes of the kind
 * MsgSend gives, which come back changed for the messages whose procedure writes them.
 */
LRESULT ClientCallWindowProc(WND *w, WNDPROC proc, BOOLEAN ansi, UINT msg, WPARAM wparam, LPARAM lparam, ULONG kind,
                             ULONG size)
{
    ULONG64 pwnd = DeskUserWindow(w), xparam = (ULONG64)(ULONG_PTR)proc, xpfn = dispatcher(ansi);
    if (!pwnd || !xpfn)
        return 0;
    ULONG fn = msg < 0x400 ? g_message_fn[msg] : SFN_DWORD;
    BOOLEAN value = kind == MIME_LPARAM_VALUE;
    PUCHAR data = value ? nullptr : (PUCHAR)lparam;
    if (kind == MIME_LPARAM_CREATESTRUCT && fn != SFN_INLPCREATESTRUCT)
        return unsupported(msg, kind);
    switch (fn) {
    case SFN_DWORD:
    case SFN_DWORD2:
    case SFN_DWORD3:
    case SFN_DWORD4:
    case SFN_DWORD5:
    case SFN_DWORD6:
    case SFN_EMPTY:
    case SFN_NCDESTROY:
    case SFN_INWPARAMCHAR: {
        if (!value && lparam)
            return unsupported(msg, kind);
        FNDWORDMSG m = {};
        head_of(&m.Head, pwnd, msg, fn == SFN_INWPARAMCHAR ? char_for(wparam, ansi) : wparam);
        m.Lparam = value ? lparam : 0;
        m.Xparam = xparam;
        m.Xpfn = xpfn;
        ULONG index = fn == SFN_NCDESTROY ? FI_NCDESTROY : fn == SFN_EMPTY ? FI_EMPTY : FI_DWORD;
        CALLBACK_RESULT r;
        return NT_SUCCESS(call_client(w, index, &m, sizeof(m), &r)) ? (LRESULT)r.Result : 0;
    }
    case SFN_INLPCREATESTRUCT:
        if (kind != MIME_LPARAM_CREATESTRUCT || !data)
            return unsupported(msg, kind);
        return call_createstruct(w, pwnd, msg, wparam, data, size, ansi, xparam, xpfn);
    case SFN_INSTRINGNULL:
    case SFN_INSTRING:
        if (!value && !data)
            return unsupported(msg, kind);
        if (value && lparam)
            return unsupported(msg, kind);
        return call_instring(w, fn == SFN_INSTRING ? FI_INSTRING : FI_INSTRINGNULL, pwnd, msg, wparam, (PCWSTR)data,
                             size, ansi, xparam, xpfn);
    case SFN_OUTSTRING:
        if (!data)
            return unsupported(msg, kind);
        return call_outstring(w, pwnd, msg, wparam, (PWSTR)data, size, ansi, xparam, xpfn);
    case SFN_GETDBCSTEXTLENGTHS: {
        FNNOLPARAMMSG m = {};
        head_of(&m.Head, pwnd, msg, wparam);
        m.Xparam = xparam;
        m.Xpfn = xpfn;
        CALLBACK_RESULT r;
        return NT_SUCCESS(call_client(w, FI_GETTEXTLENGTHS, &m, sizeof(m), &r)) ? (LRESULT)r.Result : 0;
    }
    case SFN_COPYDATA:
        if (value && lparam)
            return unsupported(msg, kind);
        return call_copydata(w, pwnd, msg, wparam, data, size, xparam, xpfn);
    case SFN_INOUTNCCALCSIZE:
        if (!data)
            return unsupported(msg, kind);
        return call_nccalcsize(w, pwnd, msg, wparam, data, size, xparam, xpfn);
    case SFN_INOUTLPPOINT5:
        return data ? call_struct(w, FI_INOUTLPPOINT5, pwnd, msg, wparam, data, size, 40, TRUE, xparam, xpfn)
                    : unsupported(msg, kind);
    case SFN_INOUTLPWINDOWPOS:
        return data ? call_struct(w, FI_INOUTLPWINDOWPOS, pwnd, msg, wparam, data, size, 40, TRUE, xparam, xpfn)
                    : unsupported(msg, kind);
    case SFN_INLPWINDOWPOS:
        return data ? call_struct(w, FI_INLPWINDOWPOS, pwnd, msg, wparam, data, size, 40, FALSE, xparam, xpfn)
                    : unsupported(msg, kind);
    case SFN_INOUTLPRECT:
        return data ? call_struct(w, FI_INOUTLPRECT, pwnd, msg, wparam, data, size, 16, TRUE, xparam, xpfn)
                    : unsupported(msg, kind);
    case SFN_INOUTSTYLECHANGE:
        return data ? call_struct(w, FI_INOUTSTYLECHANGE, pwnd, msg, wparam, data, size, 8, TRUE, xparam, xpfn)
                    : unsupported(msg, kind);
    case SFN_INOUTLPSIZE:
        return data ? call_struct(w, FI_INOUTLPSIZE, pwnd, msg, wparam, data, size, 8, TRUE, xparam, xpfn)
                    : unsupported(msg, kind);
    case SFN_INLPDRAWITEMSTRUCT:
        return data ? call_struct(w, FI_INLPDRAWITEMSTRUCT, pwnd, msg, wparam, data, size, 64, FALSE, xparam, xpfn)
                    : unsupported(msg, kind);
    case SFN_INOUTLPMEASUREITEMSTRUCT:
        return data ? call_struct(w, FI_INOUTLPMEASUREITEMSTRUCT, pwnd, msg, wparam, data, size, 32, TRUE, xparam,
                                  xpfn)
                    : unsupported(msg, kind);
    case SFN_INLPDELETEITEMSTRUCT:
        return data ? call_struct(w, FI_INLPDELETEITEMSTRUCT, pwnd, msg, wparam, data, size, 32, FALSE, xparam, xpfn)
                    : unsupported(msg, kind);
    case SFN_INLPCOMPAREITEMSTRUCT:
        return data ? call_struct(w, FI_INLPCOMPAREITEMSTRUCT, pwnd, msg, wparam, data, size, 56, FALSE, xparam, xpfn)
                    : unsupported(msg, kind);
    case SFN_INOUTLPSCROLLINFO:
        return data ? call_struct(w, FI_INOUTLPSCROLLINFO, pwnd, msg, wparam, data, size, 32, TRUE, xparam, xpfn)
                    : unsupported(msg, kind);
    case SFN_OUTLPCOMBOBOXINFO:
        return data ? call_struct(w, FI_OUTLPCOMBOBOXINFO, pwnd, msg, wparam, data, size, 64, TRUE, xparam, xpfn)
                    : unsupported(msg, kind);
    case SFN_OUTLPSCROLLBARINFO:
        return data ? call_struct(w, FI_OUTLPSCROLLBARINFO, pwnd, msg, wparam, data, size, 64, TRUE, xparam, xpfn)
                    : unsupported(msg, kind);
    case SFN_INOUTMENUGETOBJECT:
        return data ? call_struct(w, FI_INOUTMENUGETOBJECT, pwnd, msg, wparam, data, size, 32, TRUE, xparam, xpfn)
                    : unsupported(msg, kind);
    case SFN_INOUTNEXTMENU:
        return data ? call_struct(w, FI_INOUTNEXTMENU, pwnd, msg, wparam, data, size, 24, TRUE, xparam, xpfn)
                    : unsupported(msg, kind);
    case SFN_OUTLPRECT: {
        if (!data)
            return unsupported(msg, kind);
        FNNOLPARAMMSG m = {};
        head_of(&m.Head, pwnd, msg, wparam);
        m.Xparam = xparam;
        m.Xpfn = xpfn;
        CALLBACK_RESULT r;
        if (!NT_SUCCESS(call_client(w, FI_OUTLPRECT, &m, sizeof(m), &r)))
            return 0;
        if (r.Output)
            MimeCopyFromUser(data, (PVOID)(ULONG_PTR)r.Output, size < sizeof(RECT) ? size : sizeof(RECT));
        return (LRESULT)r.Result;
    }
    case SFN_OPTOUTLPDWORDOPTOUTLPDWORD:
    case SFN_OPTOUTLPDWORDOPTOUTLPDWORD2:
    case SFN_OPTOUTLPDWORDOPTOUTLPDWORD3: {
        struct {
            ULONG64 Pwnd;
            ULONG Msg, Pad;
            ULONG64 Xparam, Xpfn;
        } m = {pwnd, msg, 0, xparam, xpfn};
        CALLBACK_RESULT r;
        if (!NT_SUCCESS(call_client(w, FI_OPTOUTLPDWORDOPTOUTLPDWORD, &m, sizeof(m), &r)))
            return 0;
        if (data && kind == MIME_LPARAM_PTR2 && r.Output)
            MimeCopyFromUser(data, (PVOID)(ULONG_PTR)r.Output, size < 8 ? size : 8);
        return (LRESULT)r.Result;
    }
    case SFN_OUTDWORDINDWORD: {
        FNDWORDMSG m = {};
        head_of(&m.Head, pwnd, msg, 0);
        m.Lparam = kind == MIME_LPARAM_WPTR && data && size >= 16 ? *(PULONG64)(data + 8) : value ? lparam : 0;
        m.Xparam = xparam;
        m.Xpfn = xpfn;
        CALLBACK_RESULT r;
        if (!NT_SUCCESS(call_client(w, FI_OUTDWORDINDWORD, &m, sizeof(m), &r)))
            return 0;
        if (data && r.Output)
            MimeCopyFromUser(data, (PVOID)(ULONG_PTR)r.Output, 4);
        return (LRESULT)r.Result;
    }
    case SFN_DWORDOPTINLPMSG: {
        UCHAR m[96] = {};
        head_of((MSG_HEAD *)m, pwnd, msg, wparam);
        if (data && size >= 48) {
            *(PULONG)(m + 24) = 1;
            memcpy(m + 48, data, 48);
        }
        *(PULONG64)(m + 32) = xparam;
        *(PULONG64)(m + 40) = xpfn;
        CALLBACK_RESULT r;
        return NT_SUCCESS(call_client(w, FI_DWORDOPTINLPMSG, m, sizeof(m), &r)) ? (LRESULT)r.Result : 0;
    }
    }
    return unsupported(msg, kind);
}

/* ------------------------------------------------------------------ extra bytes of windows */

/*
 * windows 11 keeps the extra bytes of a window in memory of its process, which user32 reads for GetWindowLongPtr
 * of its own windows: xxxClientAllocWindowClassExtraBytes asks user32 for them from its heap when the window is made.
 */
PVOID ClientAllocExtraBytes(ULONG size)
{
    struct {
        ULONG Size, Pad;
    } m = {size, 0};
    CALLBACK_RESULT r;
    if (!NT_SUCCESS(call_client(nullptr, 123, &m, sizeof(m), &r)))
        return nullptr;
    return (PVOID)(ULONG_PTR)r.Result;
}

void ClientFreeExtraBytes(WND *w, PVOID bytes)
{
    struct {
        ULONG64 Hwnd, Bytes;
    } m = {(ULONG64)(ULONG_PTR)WndHandle(w), (ULONG64)(ULONG_PTR)bytes};
    W32THREAD *t = W32GetThread();
    if (t)
        t->CallbackDepth++;
    ULONG depth = UserLeaveAll();
    PVOID out = nullptr;
    ULONG out_len = 0;
    KeUserModeCallback(124, &m, sizeof(m), &out, &out_len);
    UserReenter(depth);
    if (t)
        t->CallbackDepth--;
}

/* the extra bytes win32k changed reach the copy user32 reads. */
void WndExtraChanged(WND *w, ULONG offset, ULONG size)
{
    if (!w->UserExtra || !w->Extra || offset + size > w->ExtraSize || !w->Thread || !w->Thread->Process)
        return;
    PEPROCESS owner = w->Thread->Process->Process;
    if (owner == PsGetCurrentProcess()) {
        MimeCopyToUser(w->UserExtra + offset, w->Extra + offset, size);
        return;
    }
    PKPROCESS state;
    KeStackAttachProcess((PRKPROCESS)owner, &state);
    MimeCopyToUser(w->UserExtra + offset, w->Extra + offset, size);
    KeUnstackDetachProcess(&state);
}
