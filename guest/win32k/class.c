/*
 * user object handles and window classes.
 */
#include "win32k.h"

/* ------------------------------------------------------------------ handles */

#define USER_MAX_HANDLES 32768
#define USER_FIRST_INDEX 0x20

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
    if (g_user_table && i < USER_MAX_HANDLES && g_user_table[i].Object == object)
        g_user_table[i].Object = nullptr;
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
    /* a zero generation word matches any generation, like windows accepts 16 bit handles. */
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
    int Brush; /* system color + 1, 0 for none */
    PCWSTR Cursor;
} SYSTEM_CLASS;

/* the built in classes; their procedures come from user32 at registration time. */
static const SYSTEM_CLASS g_builtin[] = {
    {L"Button", 0x0080, MIME_FNID_BUTTON, CS_DBLCLKS | CS_VREDRAW | CS_HREDRAW | CS_PARENTDC, 8, 0, IDC_ARROW},
    {L"Edit", 0x0081, MIME_FNID_EDIT, CS_DBLCLKS | CS_PARENTDC, 8, 0, IDC_IBEAM},
    {L"Static", 0x0082, MIME_FNID_STATIC, CS_DBLCLKS | CS_PARENTDC, 8, 0, IDC_ARROW},
    {L"ListBox", 0x0083, MIME_FNID_LISTBOX, CS_DBLCLKS | CS_PARENTDC, 8, 0, IDC_ARROW},
    {L"ScrollBar", 0x0084, MIME_FNID_SCROLLBAR, CS_DBLCLKS | CS_VREDRAW | CS_HREDRAW | CS_PARENTDC, 8, 0, IDC_ARROW},
    {L"ComboBox", 0x0085, MIME_FNID_COMBOBOX, CS_DBLCLKS | CS_PARENTDC | CS_VREDRAW | CS_HREDRAW, 8, 0, IDC_ARROW},
    {L"#32770", 32770, MIME_FNID_DIALOG, CS_SAVEBITS | CS_DBLCLKS, 30 + 8 * 3, COLOR_3DFACE + 1, IDC_ARROW},
    {L"#32769", 32769, MIME_FNID_DESKTOP, CS_DBLCLKS, 8, COLOR_BACKGROUND + 1, IDC_ARROW},
    {L"#32768", 32768, MIME_FNID_MENU, CS_DROPSHADOW | CS_SAVEBITS | CS_DBLCLKS, 8, COLOR_MENU + 1, IDC_ARROW},
    {L"Message", 0, MIME_FNID_MESSAGE, 0, 0, 0, nullptr},
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
        c->Style = sc->Style | CS_GLOBALCLASS;
        c->WindowExtra = sc->WindowExtra;
        c->System = TRUE;
        c->Fnid = sc->Fnid;
        c->Background = sc->Brush ? MetricsSysColorBrush(sc->Brush - 1) : nullptr;
        InsertTailList(&g_system_classes, &c->Links);
        g_builtin_classes[i] = c;
    }
}

static BOOLEAN class_matches(WND_CLASS *c, PCWSTR name, ATOM atom)
{
    if (atom)
        return c->Atom == atom;
    return c->Name && !_wcsicmp(c->Name, name);
}

/* search order of windows: local classes, global classes, then system classes. */
WND_CLASS *ClassFind(W32PROCESS *p, PCWSTR name, ATOM atom, HINSTANCE instance)
{
    if (p) {
        for (PLIST_ENTRY e = p->Classes.Flink; e != &p->Classes; e = e->Flink) {
            WND_CLASS *c = CONTAINING_RECORD(e, WND_CLASS, Links);
            if (class_matches(c, name, atom) && (!instance || c->Instance == instance))
                return c;
        }
        /* a local class of another module of the same process still matches without an exact instance. */
        for (PLIST_ENTRY e = p->Classes.Flink; e != &p->Classes; e = e->Flink) {
            WND_CLASS *c = CONTAINING_RECORD(e, WND_CLASS, Links);
            if (class_matches(c, name, atom) && (c->Style & CS_GLOBALCLASS))
                return c;
        }
    }
    for (PLIST_ENTRY e = g_global_classes.Flink; e != &g_global_classes; e = e->Flink) {
        WND_CLASS *c = CONTAINING_RECORD(e, WND_CLASS, Links);
        if (class_matches(c, name, atom))
            return c;
    }
    for (PLIST_ENTRY e = g_system_classes.Flink; e != &g_system_classes; e = e->Flink) {
        WND_CLASS *c = CONTAINING_RECORD(e, WND_CLASS, Links);
        if (class_matches(c, name, atom))
            return c;
    }
    return nullptr;
}

static void free_class(WND_CLASS *c)
{
    RemoveEntryList(&c->Links);
    W32Free(c->Name);
    W32Free(c->MenuName);
    W32Free(c->Extra);
    W32Free(c);
}

void ClassFreeProcess(W32PROCESS *p)
{
    while (!IsListEmpty(&p->Classes))
        free_class(CONTAINING_RECORD(p->Classes.Flink, WND_CLASS, Links));
}

ATOM NTAPI NtUserRegisterClassExWOW(const WNDCLASSEXW *user_wc, PUNICODE_STRING user_name, PUNICODE_STRING user_menu,
                                    DWORD fnid, DWORD flags)
{
    WNDCLASSEXW wc;
    if (!NT_SUCCESS(MimeCopyFromUser(&wc, user_wc, sizeof(wc))) || wc.cbSize != sizeof(wc))
        return 0;
    PWSTR name = nullptr, menu = nullptr;
    SIZE_T name_chars = 0;
    ATOM atom = 0;
    if (IS_INTRESOURCE(wc.lpszClassName)) {
        atom = (ATOM)(ULONG_PTR)wc.lpszClassName;
    } else if (!NT_SUCCESS(W32CaptureString(user_name, &name, &name_chars)) || !name || !name_chars) {
        W32Free(name);
        return 0;
    }
    if (user_menu && !NT_SUCCESS(W32CaptureString(user_menu, &menu, nullptr)))
        menu = nullptr;
    if (wc.cbClsExtra < 0 || wc.cbWndExtra < 0 || wc.cbClsExtra > 40000 || wc.cbWndExtra > 40000) {
        W32Free(name);
        W32Free(menu);
        return 0;
    }
    UserEnter();
    W32PROCESS *p = W32GetProcess();
    ATOM result = 0;
    if (flags & MIME_CLASS_SYSTEM) {
        /* user32 registers the procedures of the built in classes. */
        for (ULONG i = 0; i < RTL_NUMBER_OF(g_builtin); i++) {
            WND_CLASS *c = g_builtin_classes[i];
            if (c && name && !_wcsicmp(c->Name, name)) {
                c->Proc = wc.lpfnWndProc;
                c->Ansi = FALSE;
                result = c->Atom;
            }
        }
    } else if (p) {
        WND_CLASS *existing = ClassFind(p, name, atom, wc.hInstance);
        if (existing && !existing->System && (existing->Process == p || (existing->Style & CS_GLOBALCLASS))) {
            UserLeave();
            W32Free(name);
            W32Free(menu);
            return 0; /* ERROR_CLASS_ALREADY_EXISTS */
        }
        WND_CLASS *c = W32Alloc(sizeof(WND_CLASS));
        if (c) {
            c->Name = name;
            name = nullptr;
            c->Atom = atom ? atom : AtomAdd(c->Name);
            c->Style = wc.style;
            c->Proc = wc.lpfnWndProc;
            c->Ansi = (flags & MIME_CLASS_ANSI) != 0;
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
            c->Process = p;
            c->Extra = wc.cbClsExtra ? W32Alloc((SIZE_T)wc.cbClsExtra) : nullptr;
            if (wc.style & CS_GLOBALCLASS)
                InsertTailList(&g_global_classes, &c->Links);
            else
                InsertTailList(&p->Classes, &c->Links);
            result = c->Atom;
        }
    }
    UserLeave();
    W32Free(name);
    W32Free(menu);
    return result;
}

BOOL NTAPI NtUserUnregisterClass(PUNICODE_STRING user_name, HINSTANCE instance)
{
    PWSTR name = nullptr;
    UNICODE_STRING s;
    ATOM atom = 0;
    if (!NT_SUCCESS(MimeCopyFromUser(&s, user_name, sizeof(s))))
        return FALSE;
    if (IS_INTRESOURCE(s.Buffer))
        atom = (ATOM)(ULONG_PTR)s.Buffer;
    else if (!NT_SUCCESS(W32CaptureString(user_name, &name, nullptr)))
        return FALSE;
    UserEnter();
    W32PROCESS *p = W32GetProcess();
    WND_CLASS *c = ClassFind(p, name, atom, instance);
    BOOL ok = FALSE;
    if (c && !c->System && c->Windows == 0) {
        free_class(c);
        ok = TRUE;
    }
    UserLeave();
    W32Free(name);
    return ok;
}

BOOL NTAPI NtUserGetClassInfoEx(HINSTANCE instance, PUNICODE_STRING user_name, LPWNDCLASSEXW user_wc, LPWSTR *menu,
                                BOOL ansi)
{
    PWSTR name = nullptr;
    UNICODE_STRING s;
    ATOM atom = 0;
    if (!NT_SUCCESS(MimeCopyFromUser(&s, user_name, sizeof(s))))
        return FALSE;
    if (IS_INTRESOURCE(s.Buffer))
        atom = (ATOM)(ULONG_PTR)s.Buffer;
    else if (!NT_SUCCESS(W32CaptureString(user_name, &name, nullptr)))
        return FALSE;
    UserEnter();
    WND_CLASS *c = ClassFind(W32GetProcess(), name, atom, instance);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    if (c) {
        wc.cbSize = sizeof(wc);
        wc.style = c->Style;
        wc.lpfnWndProc = c->Proc;
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
    if (!c)
        return FALSE;
    return NT_SUCCESS(MimeCopyToUser(user_wc, &wc, sizeof(wc))) ? c->Atom : FALSE;
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
    if (w && w->Class && w->Class->Name) {
        n = (ULONG)wcslen(w->Class->Name);
        if (n > 255)
            n = 255;
        memcpy(name, w->Class->Name, n * sizeof(WCHAR));
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
