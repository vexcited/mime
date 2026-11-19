/* user object handles and window classes. */
#include "win32k.h"

/* ------------------------------------------------------------------ handles */

#define USER_MAX_HANDLES 32768
#define USER_FIRST_INDEX 0x60 /* under it stand the handles of the monitors user32 was told of, usershared.c. */

typedef struct USER_ENTRY {
    USER_OBJECT *Object;
    USHORT Generation;
} USER_ENTRY;

static USER_ENTRY *g_user_table;
static ULONG g_user_next = USER_FIRST_INDEX;

/* windows hwnds look like 0x00010042: generation in the high word, index in the low word. */
HANDLE UserCreateHandle(USER_OBJECT *object, UCHAR type)
{
    if (!g_user_table) {
        g_user_table = W32Alloc(USER_MAX_HANDLES * sizeof(USER_ENTRY));
        if (!g_user_table)
            return nullptr;
    }
    for (ULONG n = 0; n < USER_MAX_HANDLES; n++) {
        ULONG i = g_user_next;
        g_user_next = g_user_next + 1 >= USER_MAX_HANDLES ? USER_FIRST_INDEX : g_user_next + 1;
        if (!g_user_table[i].Object) {
            USHORT gen = (USHORT)(g_user_table[i].Generation + 1);
            if (!gen)
                gen = 1;
            g_user_table[i].Generation = gen;
            g_user_table[i].Object = object;
            object->Type = type;
            object->Handle = (HANDLE)(ULONG_PTR)(((ULONG)gen << 16) | i);
            object->RefCount = 1;
            return object->Handle;
        }
    }
    return nullptr;
}

void UserFreeHandle(USER_OBJECT *object)
{
    ULONG i = (ULONG)(ULONG_PTR)object->Handle & 0xFFFF;
    if (g_user_table && i < USER_MAX_HANDLES && g_user_table[i].Object == object) {
        DeskObjectFreed(object);
        g_user_table[i].Object = nullptr;
    }
}

USER_OBJECT *UserNextObject(ULONG *index, UCHAR type)
{
    if (!g_user_table)
        return nullptr;
    for (ULONG i = *index < USER_FIRST_INDEX ? USER_FIRST_INDEX : *index; i < USER_MAX_HANDLES; i++) {
        USER_OBJECT *o = g_user_table[i].Object;
        if (o && o->Type == type) {
            *index = i + 1;
            return o;
        }
    }
    *index = USER_MAX_HANDLES;
    return nullptr;
}

USER_OBJECT *UserLookup(HANDLE handle, UCHAR type)
{
    ULONG_PTR v = (ULONG_PTR)handle;
    ULONG i = (ULONG)(v & 0xFFFF);
    if (!g_user_table || i < USER_FIRST_INDEX || i >= USER_MAX_HANDLES)
        return nullptr;
    USER_OBJECT *o = g_user_table[i].Object;
    if (!o || (type && o->Type != type) || o->Destroyed)
        return nullptr;
    /* a zero generation matches any, like windows accepts 16-bit handles. */
    USHORT gen = (USHORT)(v >> 16);
    if (gen && gen != g_user_table[i].Generation)
        return nullptr;
    return o;
}

void UserReference(USER_OBJECT *object)
{
    object->RefCount++;
}

void UserDereference(USER_OBJECT *object)
{
    if (--object->RefCount == 0) {
        UserFreeHandle(object);
        W32Free(object);
    }
}

/* ------------------------------------------------------------------ classes */

static LIST_ENTRY g_global_classes = {&g_global_classes, &g_global_classes};
static LIST_ENTRY g_system_classes = {&g_system_classes, &g_system_classes};

typedef struct SYSTEM_CLASS {
    PCWSTR Name;
    ATOM Atom;
    ULONG Fnid;
    UINT Style;
    int WindowExtra;
    int Brush; /* system color + 1, 0 for none. */
    PCWSTR Cursor;
} SYSTEM_CLASS;

/*
 * the classes win32k registers itself, whose windows run their procedures in the kernel. the others of windows
 * come from user32, which registers them in each process the first time a window or a query names them, see
 * RegisterDefaultClass. the class of message windows takes DefWindowProc of user32 as its procedure.
 */
static const SYSTEM_CLASS g_builtin[] = {
    {L"#32769", 0x8001, FNID_DESKTOP, CS_DBLCLKS, 8, COLOR_BACKGROUND + 1, IDC_ARROW},
    {L"#32768", 0x8000, FNID_MENU, CS_DROPSHADOW | CS_SAVEBITS | CS_DBLCLKS, 16, COLOR_MENU + 1, IDC_ARROW},
    {L"Message", 0, FNID_MESSAGEWND, 0, 0, 0, nullptr},
};

static WND_CLASS *g_builtin_classes[RTL_NUMBER_OF(g_builtin)];

void ClassInitialize(void)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_builtin); i++) {
        const SYSTEM_CLASS *sc = &g_builtin[i];
        WND_CLASS *c = W32Alloc(sizeof(WND_CLASS));
        if (!c)
            continue;
        c->Name = W32StrDup(sc->Name, wcslen(sc->Name));
        c->Atom = sc->Atom ? sc->Atom : AtomAdd(sc->Name);
        c->PlainAtom = c->Atom;
        c->Style = sc->Style | CS_GLOBALCLASS;
        c->WindowExtra = sc->WindowExtra;
        c->System = TRUE;
        c->Fnid = sc->Fnid;
        c->Background = sc->Brush ? MetricsSysColorBrush(sc->Brush - 1) : nullptr;
        c->Cursor = sc->Cursor ? CursorSystem((ULONG)(ULONG_PTR)sc->Cursor) : nullptr;
        InsertTailList(&g_system_classes, &c->Links);
        g_builtin_classes[i] = c;
    }
}

/* "#32770" names the integer atom 32770, as UserFindAtom reads a name of digits after a #. */
ATOM ClassIntegerAtom(PCWSTR name)
{
    if (!name || name[0] != L'#' || !name[1])
        return 0;
    ULONG v = 0;
    for (PCWSTR p = name + 1; *p; p++) {
        if (*p < L'0' || *p > L'9' || v > 0xFFFF)
            return 0;
        v = v * 10 + (ULONG)(*p - L'0');
    }
    return v && v < 0xC000 ? (ATOM)v : 0;
}

static BOOLEAN class_matches(WND_CLASS *c, PCWSTR name, ATOM atom)
{
    if (!atom)
        atom = ClassIntegerAtom(name);
    if (atom)
        return c->Atom == atom;
    /* an atom of 0 and no name is not a class. */
    return c->Name && name && !_wcsicmp(c->Name, name);
}

/* windows search order: local classes, global classes of the process, then system classes. */
WND_CLASS *ClassFind(W32PROCESS *p, PCWSTR name, ATOM atom, HINSTANCE instance)
{
    if (p) {
        for (PLIST_ENTRY e = p->Classes.Flink; e != &p->Classes; e = e->Flink) {
            WND_CLASS *c = CONTAINING_RECORD(e, WND_CLASS, Links);
            if (class_matches(c, name, atom) && (!instance || c->Instance == instance))
                return c;
        }
        /* a local class of another module in the process still matches without an exact instance. */
        for (PLIST_ENTRY e = p->Classes.Flink; e != &p->Classes; e = e->Flink) {
            WND_CLASS *c = CONTAINING_RECORD(e, WND_CLASS, Links);
            if (class_matches(c, name, atom) && (c->Style & CS_GLOBALCLASS))
                return c;
        }
    }
    /* a global class is only global within its process. */
    for (PLIST_ENTRY e = g_global_classes.Flink; e != &g_global_classes; e = e->Flink) {
        WND_CLASS *c = CONTAINING_RECORD(e, WND_CLASS, Links);
        if (class_matches(c, name, atom) && (!p || c->Process == p))
            return c;
    }
    for (PLIST_ENTRY e = g_system_classes.Flink; e != &g_system_classes; e = e->Flink) {
        WND_CLASS *c = CONTAINING_RECORD(e, WND_CLASS, Links);
        if (class_matches(c, name, atom))
            return c;
    }
    return nullptr;
}

/* the window of a message runs DefWindowProc of user32, the others of win32k run in the kernel. */
WNDPROC ClassProc(const WND_CLASS *c, W32PROCESS *p)
{
    (void)p;
    if (c->System && c->Fnid == FNID_MESSAGEWND)
        return (WNDPROC)(ULONG_PTR)ClientPfn(24 + FNID_MESSAGEWND - FNID_FIRST);
    return c->Proc;
}

static void free_class(WND_CLASS *c)
{
    DeskClassFreed(c);
    RemoveEntryList(&c->Links);
    W32Free(c->Name);
    W32Free(c->PlainName);
    if (!IS_INTRESOURCE(c->MenuName))
        W32Free(c->MenuName);
    W32Free(c->Extra);
    W32Free(c);
}

void ClassFreeProcess(W32PROCESS *p)
{
    while (!IsListEmpty(&p->Classes))
        free_class(CONTAINING_RECORD(p->Classes.Flink, WND_CLASS, Links));
    for (PLIST_ENTRY e = g_global_classes.Flink; e != &g_global_classes;) {
        WND_CLASS *c = CONTAINING_RECORD(e, WND_CLASS, Links);
        e = e->Flink;
        if (c->Process == p)
            free_class(c);
    }
}

/* the menu names user32 keeps for a class: ansi and wide copies of its own, and the one win32k holds. */
typedef struct CLSMENUNAME {
    ULONG64 AnsiMenuName;
    ULONG64 UnicodeMenuName;
    ULONG64 MenuName; /* a UNICODE_STRING, its buffer an int resource when its length is 0. */
} CLSMENUNAME;

#define CSF_ANSIPROC 0x0002
#define FNID_DDEML 0x2000

/* a UNICODE_STRING of a class: a name, or an atom in place of the buffer. */
static NTSTATUS capture_class(PUNICODE_STRING user, PWSTR *name, SIZE_T *chars, ATOM *atom)
{
    UNICODE_STRING s;
    *name = nullptr;
    *atom = 0;
    if (chars)
        *chars = 0;
    if (!user || !NT_SUCCESS(MimeCopyFromUser(&s, user, sizeof(s))))
        return STATUS_INVALID_PARAMETER;
    if (!s.Length && (ULONG_PTR)s.Buffer < 0x10000) {
        *atom = (ATOM)(ULONG_PTR)s.Buffer;
        return *atom ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    }
    NTSTATUS status = W32CaptureString(user, name, chars);
    if (NT_SUCCESS(status) && (!*name || !**name)) {
        W32Free(*name);
        *name = nullptr;
        return STATUS_INVALID_PARAMETER;
    }
    return status;
}

/*
 * RegisterClassExWOWW and RegisterClassExWOWA of user32 26300.9457: the WNDCLASSEXW, the name of the class and the
 * name of its version in the activation context, each a UNICODE_STRING or an atom, the menu names, the procedure
 * number of a class of user32, CSF flags of which 2 says the procedure is ansi, and a pointer for wow data that
 * user32 gives as 0. a class of user32 has no procedure in the structure: it gets the one ntdll keeps for its
 * number. the class is found by the name of its version, which is also the one its atom stands for.
 */
ATOM NTAPI NtUserRegisterClassExWOW(const WNDCLASSEXW *user_wc, PUNICODE_STRING user_name, PUNICODE_STRING user_version,
                                    const CLSMENUNAME *user_menu, DWORD fnid, DWORD flags, PDWORD wow)
{
    WNDCLASSEXW wc;
    (void)wow;
    if (!NT_SUCCESS(MimeCopyFromUser(&wc, user_wc, sizeof(wc))) || wc.cbSize != sizeof(wc)) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    fnid &= 0xFFFF;
    if (fnid == FNID_DDEML)
        fnid = 0;
    PWSTR plain = nullptr, name = nullptr, menu = nullptr;
    ATOM plain_atom = 0, atom = 0;
    if (!NT_SUCCESS(capture_class(user_name, &plain, nullptr, &plain_atom)) ||
        !NT_SUCCESS(capture_class(user_version ? user_version : user_name, &name, nullptr, &atom))) {
        W32Free(plain);
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    CLSMENUNAME cmn;
    UNICODE_STRING menu_string;
    if (user_menu && NT_SUCCESS(MimeCopyFromUser(&cmn, user_menu, sizeof(cmn))) && cmn.MenuName &&
        NT_SUCCESS(MimeCopyFromUser(&menu_string, (PVOID)(ULONG_PTR)cmn.MenuName, sizeof(menu_string)))) {
        if (!menu_string.Length && (ULONG_PTR)menu_string.Buffer < 0x10000)
            menu = menu_string.Buffer;
        else if (!NT_SUCCESS(W32CaptureString((PUNICODE_STRING)(ULONG_PTR)cmn.MenuName, &menu, nullptr)))
            menu = nullptr;
    }
    if (wc.cbClsExtra < 0 || wc.cbWndExtra < 0 || wc.cbClsExtra > 40000 || wc.cbWndExtra > 40000 ||
        (fnid && (fnid < FNID_FIRST || fnid > FNID_LAST_CLIENT))) {
        W32Free(plain);
        W32Free(name);
        if (!IS_INTRESOURCE(menu))
            W32Free(menu);
        W32SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    UserEnter();
    W32PROCESS *p = W32GetProcess();
    ATOM result = 0;
    if (p) {
        WND_CLASS *existing = ClassFind(p, name, atom, wc.hInstance);
        if (existing && !existing->System && (existing->Process == p || (existing->Style & CS_GLOBALCLASS)) &&
            (existing->Instance == wc.hInstance || (existing->Style & CS_GLOBALCLASS))) {
            W32SetLastError(ERROR_CLASS_ALREADY_EXISTS);
            goto out;
        }
        WND_CLASS *c = W32Alloc(sizeof(WND_CLASS));
        if (!c) {
            W32SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            goto out;
        }
        BOOLEAN ansi = (flags & CSF_ANSIPROC) != 0;
        c->Name = name;
        name = nullptr;
        c->Atom = atom ? atom : AtomAdd(c->Name);
        c->PlainName = plain;
        plain = nullptr;
        c->PlainAtom = plain_atom ? plain_atom : c->PlainName ? AtomAdd(c->PlainName) : c->Atom;
        c->Style = wc.style;
        c->Proc = wc.lpfnWndProc;
        if (fnid && !c->Proc)
            c->Proc = (WNDPROC)(ULONG_PTR)ClientPfn((ansi ? 0 : 24) + fnid - FNID_FIRST);
        c->Ansi = ansi;
        c->ClassExtra = wc.cbClsExtra;
        c->WindowExtra = wc.cbWndExtra;
        c->Instance = wc.hInstance;
        c->Icon = wc.hIcon;
        c->IconSmall = wc.hIconSm;
        c->Cursor = wc.hCursor;
        c->Background = wc.hbrBackground;
        c->MenuName = menu;
        menu = nullptr;
        c->Fnid = fnid;
        c->CsfFlags = (USHORT)flags;
        c->Process = p;
        c->Extra = wc.cbClsExtra ? W32Alloc((SIZE_T)wc.cbClsExtra) : nullptr;
        if (wc.style & CS_GLOBALCLASS)
            InsertTailList(&g_global_classes, &c->Links);
        else
            InsertTailList(&p->Classes, &c->Links);
        result = c->Atom;
    }
out:
    UserLeave();
    W32Free(plain);
    W32Free(name);
    if (!IS_INTRESOURCE(menu))
        W32Free(menu);
    return result;
}

/* the menu names of the class go back to user32, which frees its copies. */
BOOL NTAPI NtUserUnregisterClass(PUNICODE_STRING user_name, HINSTANCE instance, CLSMENUNAME *user_menu)
{
    PWSTR name = nullptr;
    ATOM atom = 0;
    if (!NT_SUCCESS(capture_class(user_name, &name, nullptr, &atom))) {
        W32SetLastError(ERROR_CLASS_DOES_NOT_EXIST);
        return FALSE;
    }
    UserEnter();
    W32PROCESS *p = W32GetProcess();
    WND_CLASS *c = ClassFind(p, name, atom, instance);
    BOOL ok = FALSE;
    if (!c || c->System || (c->Process && c->Process != p)) {
        W32SetLastError(ERROR_CLASS_DOES_NOT_EXIST);
    } else if (c->Windows) {
        W32SetLastError(ERROR_CLASS_HAS_WINDOWS);
    } else {
        free_class(c);
        ok = TRUE;
    }
    UserLeave();
    if (ok && user_menu) {
        CLSMENUNAME none = {};
        MimeCopyToUser(user_menu, &none, sizeof(none));
    }
    W32Free(name);
    return ok;
}

BOOL NTAPI NtUserGetClassInfoEx(HINSTANCE instance, PUNICODE_STRING user_name, LPWNDCLASSEXW user_wc, LPWSTR *menu,
                                BOOL ansi)
{
    PWSTR name = nullptr;
    ATOM atom = 0;
    if (!NT_SUCCESS(capture_class(user_name, &name, nullptr, &atom))) {
        W32SetLastError(ERROR_CLASS_DOES_NOT_EXIST);
        return FALSE;
    }
    UserEnter();
    WND_CLASS *c = ClassFind(W32GetProcess(), name, atom, instance);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    ATOM found = 0;
    if (c) {
        /* _GetClassInfoEx: the style without the bits of win32k, and a class of user32 is not global to the caller. */
        found = c->PlainAtom ? c->PlainAtom : c->Atom;
        wc.cbSize = sizeof(wc);
        wc.style = c->Style & 0x37BFF;
        if (c->Fnid)
            wc.style &= ~CS_GLOBALCLASS;
        wc.lpfnWndProc = ClassProc(c, W32GetProcess());
        wc.cbClsExtra = c->ClassExtra;
        wc.cbWndExtra = c->WindowExtra;
        wc.hInstance = c->Instance;
        wc.hIcon = c->Icon;
        wc.hCursor = c->Cursor;
        wc.hbrBackground = c->Background;
        wc.hIconSm = c->IconSmall;
        wc.lpszClassName = (LPCWSTR)(ULONG_PTR)c->Atom;
    }
    UserLeave();
    W32Free(name);
    (void)ansi;
    if (!c) {
        W32SetLastError(ERROR_CLASS_DOES_NOT_EXIST);
        return FALSE;
    }
    if (menu)
        MimeCopyToUser(menu, &(LPWSTR){nullptr}, sizeof(LPWSTR));
    return NT_SUCCESS(MimeCopyToUser(user_wc, &wc, sizeof(wc))) ? found : FALSE;
}

int NTAPI NtUserGetClassName(HWND hwnd, BOOL real, PUNICODE_STRING user_name)
{
    UNICODE_STRING s;
    if (!NT_SUCCESS(MimeCopyFromUser(&s, user_name, sizeof(s))))
        return 0;
    UserEnter();
    WND *w = WndLookup(hwnd);
    WCHAR name[256];
    ULONG n = 0;
    PCWSTR shown = w && w->Class ? (w->Class->PlainName ? w->Class->PlainName : w->Class->Name) : nullptr;
    WCHAR number[8];
    if (w && w->Class && !shown && w->Class->Atom && w->Class->Atom < 0xC000) {
        ULONG k = 0, v = w->Class->Atom, at = 0;
        WCHAR digits[6];
        while (v) {
            digits[k++] = (WCHAR)(L'0' + v % 10);
            v /= 10;
        }
        number[at++] = L'#';
        while (k)
            number[at++] = digits[--k];
        number[at] = 0;
        shown = number;
    }
    if (shown) {
        n = (ULONG)wcslen(shown);
        if (n > 255)
            n = 255;
        memcpy(name, shown, n * sizeof(WCHAR));
    }
    UserLeave();
    if (!n || !s.MaximumLength)
        return 0;
    ULONG max = s.MaximumLength / sizeof(WCHAR) - 1;
    if (n > max)
        n = max;
    name[n] = 0;
    if (!NT_SUCCESS(MimeCopyToUser(s.Buffer, name, (n + 1) * sizeof(WCHAR))))
        return 0;
    return (int)n;
}

/* ------------------------------------------------------------------ class changes */

static void move_windows(WND *w, WND_CLASS *from, WND_CLASS *to, W32PROCESS *p)
{
    for (; w; w = w->Next) {
        if (w->Class == from && w->Thread && w->Thread->Process == p) {
            w->Class = to;
            from->Windows--;
            to->Windows++;
        }
        move_windows(w->FirstChild, from, to, p);
    }
}

/*
 * like windows, each process gets its own version of a class it changes: a system class or another process's
 * global class is copied first, and the process's windows move to the copy.
 */
static WND_CLASS *private_class(WND *w, W32PROCESS *p)
{
    WND_CLASS *c = w->Class;
    if (c->Process == p && !c->System)
        return c;
    for (PLIST_ENTRY e = p->Classes.Flink; e != &p->Classes; e = e->Flink)
        if (CONTAINING_RECORD(e, WND_CLASS, Links) == c)
            return c;
    WND_CLASS *copy = W32Alloc(sizeof(WND_CLASS));
    if (!copy)
        return nullptr;
    *copy = *c;
    copy->Name = c->Name ? W32StrDup(c->Name, wcslen(c->Name)) : nullptr;
    copy->PlainName = c->PlainName ? W32StrDup(c->PlainName, wcslen(c->PlainName)) : nullptr;
    copy->MenuName = c->MenuName && !IS_INTRESOURCE(c->MenuName) ? W32StrDup(c->MenuName, wcslen(c->MenuName)) : c->MenuName;
    copy->Extra = c->ClassExtra ? W32Alloc((SIZE_T)c->ClassExtra) : nullptr;
    if (copy->Extra && c->Extra)
        memcpy(copy->Extra, c->Extra, (SIZE_T)c->ClassExtra);
    copy->Proc = ClassProc(c, p);
    copy->Process = p;
    copy->Windows = 0;
    copy->DeskOffset = copy->DeskSize = copy->DeskName = copy->DeskNameSize = 0;
    InsertTailList(&p->Classes, &copy->Links);
    move_windows(WndDesktop(), c, copy, p);
    return copy;
}

/* pointer sized values, which the 32-bit and 16-bit forms cannot reach. */
static BOOLEAN pointer_index(int index)
{
    return index == GCL_MENUNAME || index == GCLP_HBRBACKGROUND || index == GCLP_HCURSOR || index == GCLP_HICON ||
           index == GCLP_HMODULE || index == GCLP_WNDPROC || index == GCLP_HICONSM;
}

static BOOLEAN class_index_known(int index)
{
    return pointer_index(index) || index == GCL_CBWNDEXTRA || index == GCL_CBCLSEXTRA || index == GCL_STYLE ||
           index == GCW_ATOM;
}

/*
 * SetClassLongPtr, SetClassLong and SetClassWord; the old value comes back. the window must belong to the
 * process, else ERROR_ACCESS_DENIED. an index past the extra bytes, or a pointer sized value through the 32-bit
 * form, is ERROR_INVALID_INDEX; the extra byte size and the atom cannot be set. icons and cursors must be handles
 * of that kind, else ERROR_INVALID_ICON_HANDLE or ERROR_INVALID_CURSOR_HANDLE. changes go to the process's copy.
 */
static ULONG_PTR set_class_value(HWND hwnd, int index, ULONG_PTR value, BOOL ansi, SIZE_T width)
{
    UserEnter();
    W32PROCESS *p = W32GetProcess();
    WND *w = WndLookup(hwnd);
    ULONG_PTR old = 0;
    if (!w || !w->Class || w == WndDesktop() || !p) {
        W32SetLastError(ERROR_INVALID_WINDOW_HANDLE);
        UserLeave();
        return 0;
    }
    if (!w->Thread || w->Thread->Process != p) {
        W32SetLastError(ERROR_ACCESS_DENIED);
        UserLeave();
        return 0;
    }
    if (index >= 0 ? (SIZE_T)index + width > (SIZE_T)w->Class->ClassExtra
                   : !class_index_known(index) || (width < sizeof(ULONG_PTR) && pointer_index(index))) {
        W32SetLastError(ERROR_INVALID_INDEX);
        UserLeave();
        return 0;
    }
    if (index == GCW_ATOM || index == GCL_CBCLSEXTRA) {
        W32SetLastError(ERROR_INVALID_PARAMETER);
        UserLeave();
        return 0;
    }
    if (index == GCL_STYLE && (value & 0x10000)) {
        W32SetLastError(ERROR_INVALID_DATA);
        UserLeave();
        return 0;
    }
    if ((index == GCLP_HCURSOR || index == GCLP_HICON || index == GCLP_HICONSM) && value && !CursorValid((HICON)value)) {
        W32SetLastError(index == GCLP_HCURSOR ? ERROR_INVALID_CURSOR_HANDLE : ERROR_INVALID_ICON_HANDLE);
        UserLeave();
        return 0;
    }
    WND_CLASS *c = private_class(w, p);
    if (!c) {
        W32SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        UserLeave();
        return 0;
    }
    PWSTR menu = nullptr;
    switch (index) {
    case GCLP_WNDPROC:
        old = (ULONG_PTR)c->Proc;
        c->Proc = (WNDPROC)value;
        c->Ansi = ansi != 0;
        break;
    case GCLP_HBRBACKGROUND:
        old = (ULONG_PTR)c->Background;
        c->Background = (HBRUSH)value;
        break;
    case GCLP_HCURSOR:
        old = (ULONG_PTR)c->Cursor;
        c->Cursor = (HCURSOR)value;
        break;
    case GCLP_HICON:
    case GCLP_HICONSM:
        old = (ULONG_PTR)(index == GCLP_HICON ? c->Icon : c->IconSmall);
        if (index == GCLP_HICON)
            c->Icon = (HICON)value;
        else
            c->IconSmall = (HICON)value;
        break;
    case GCLP_HMODULE:
        old = (ULONG_PTR)c->Instance;
        c->Instance = (HINSTANCE)value;
        break;
    case GCL_STYLE:
        old = c->Style;
        c->Style = (UINT)value;
        break;
    case GCL_CBWNDEXTRA:
        old = (ULONG_PTR)c->WindowExtra;
        c->WindowExtra = (int)value;
        break;
    case GCL_MENUNAME: {
        /* a name or a resource number; a name is copied from the caller's memory. */
        old = (ULONG_PTR)c->MenuName;
        if (!value || IS_INTRESOURCE(value)) {
            menu = (PWSTR)value;
        } else {
            WCHAR text[256];
            SIZE_T n = 0;
            while (n < RTL_NUMBER_OF(text) - 1 && NT_SUCCESS(MimeCopyFromUser(&text[n], (PWSTR)value + n, sizeof(WCHAR))) && text[n])
                n++;
            text[n] = 0;
            menu = W32StrDup(text, n);
        }
        c->MenuName = menu;
        break;
    }
    default:
        memcpy(&old, c->Extra + index, width);
        memcpy(c->Extra + index, &value, width);
        if (width == sizeof(WORD))
            old &= 0xFFFF;
        break;
    }
    if (index == GCLP_HICON || index == GCLP_HICONSM) {
        WND *desktop = WndDesktop();
        for (WND *top = desktop ? desktop->FirstChild : nullptr; top; top = top->Next)
            if (top->Class == c)
                top->Flags |= WNDF_FRAME_DIRTY;
    }
    UserLeave();
    return old;
}

ULONG_PTR NTAPI NtUserSetClassLongPtr(HWND hwnd, int index, ULONG_PTR value, BOOL ansi)
{
    return set_class_value(hwnd, index, value, ansi, sizeof(ULONG_PTR));
}

ULONG NTAPI NtUserSetClassLong(HWND hwnd, int index, ULONG value, BOOL ansi)
{
    return (ULONG)set_class_value(hwnd, index, (ULONG_PTR)value, ansi, sizeof(ULONG));
}

USHORT NTAPI NtUserSetClassWord(HWND hwnd, int index, USHORT value)
{
    return (USHORT)set_class_value(hwnd, index, (ULONG_PTR)value, FALSE, sizeof(USHORT));
}
