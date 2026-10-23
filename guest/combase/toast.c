/*
 * Windows.UI.Notifications toasts. a program builds an XmlDocument, wraps it in a ToastNotification and shows it
 * through its app user model id's notifier. the content is read at show time: title, body, picture, buttons, a reply
 * box and urgency go to the host through win32k's NtUserMimeShell. what the user does comes back as Activated,
 * Dismissed or Failed events, which a process thread waits for.
 *
 * the manager's factory only answers when the host can show notifications, so a program that checks first, like a
 * browser, keeps its own notification center when there is no desktop.
 */
#include "xmldom.h"
#include <mime/abi.h>
#include <mime/ntuser.h>
#include <mime/win32/winuser.h>

const WCHAR CbToastManagerClass[] = L"Windows.UI.Notifications.ToastNotificationManager";
const WCHAR CbToastClass[] = L"Windows.UI.Notifications.ToastNotification";
static const WCHAR g_notifier_class[] = L"Windows.UI.Notifications.ToastNotifier";
static const WCHAR g_history_class[] = L"Windows.UI.Notifications.ToastNotificationHistory";
static const WCHAR g_manager_user_class[] = L"Windows.UI.Notifications.ToastNotificationManagerForUser";
static const WCHAR g_activated_class[] = L"Windows.UI.Notifications.ToastActivatedEventArgs";
static const WCHAR g_dismissed_class[] = L"Windows.UI.Notifications.ToastDismissedEventArgs";
static const WCHAR g_failed_class[] = L"Windows.UI.Notifications.ToastFailedEventArgs";
static const WCHAR g_set_class[] = L"Windows.Foundation.Collections.PropertySet";
static const WCHAR g_vector_class[] = L"Windows.Foundation.Collections.IVectorView`1<Windows.UI.Notifications.ToastNotification>";

#define HANDLERS_MAX 8
#define ACTIONS_MAX MIME_TOAST_ACTIONS
#define ERROR_NOT_FOUND_HR ((HRESULT)0x80070490)

/* ------------------------------------------------------------------ helpers */

static WCHAR *copy_text(const WCHAR *text)
{
    SIZE_T n = text ? wcslen(text) : 0;
    WCHAR *out = CbAlloc((n + 1) * sizeof(WCHAR));
    if (out) {
        if (n)
            memcpy(out, text, n * sizeof(WCHAR));
        out[n] = 0;
    }
    return out;
}

static WCHAR *copy_hstring(HSTRING string)
{
    UINT32 length;
    LPCWSTR raw = WindowsGetStringRawBuffer(string, &length);
    WCHAR *out = CbAlloc(((SIZE_T)length + 1) * sizeof(WCHAR));
    if (out) {
        if (length)
            memcpy(out, raw, length * sizeof(WCHAR));
        out[length] = 0;
    }
    return out;
}

static void put_text(uint16_t *dst, SIZE_T capacity, const WCHAR *text)
{
    SIZE_T n = 0;
    if (text)
        while (n + 1 < capacity && text[n]) {
            dst[n] = text[n];
            n++;
        }
    dst[n] = 0;
}

static BOOL same_text(const WCHAR *a, const WCHAR *b)
{
    return !wcscmp(a ? a : L"", b ? b : L"");
}

static void set_string(WCHAR **slot, WCHAR *text)
{
    CbFree(*slot);
    *slot = text;
}

/* ------------------------------------------------------------------ handlers */

typedef struct HANDLERS {
    struct {
        ITypedEventHandler *Handler;
        INT64 Token;
    } Entries[HANDLERS_MAX];
} HANDLERS;

static volatile INT64 g_next_token;

static HRESULT handlers_add(HANDLERS *set, CRITICAL_SECTION *lock, ITypedEventHandler *handler,
                            EventRegistrationToken *token)
{
    if (!handler || !token)
        return E_POINTER;
    HRESULT hr = E_OUTOFMEMORY;
    EnterCriticalSection(lock);
    for (UINT i = 0; i < HANDLERS_MAX; i++)
        if (!set->Entries[i].Handler) {
            handler->lpVtbl->AddRef(handler);
            set->Entries[i].Handler = handler;
            set->Entries[i].Token = InterlockedIncrement64(&g_next_token);
            token->value = set->Entries[i].Token;
            hr = S_OK;
            break;
        }
    LeaveCriticalSection(lock);
    return hr;
}

static HRESULT handlers_remove(HANDLERS *set, CRITICAL_SECTION *lock, EventRegistrationToken token)
{
    ITypedEventHandler *removed = nullptr;
    EnterCriticalSection(lock);
    for (UINT i = 0; i < HANDLERS_MAX; i++)
        if (set->Entries[i].Handler && set->Entries[i].Token == token.value) {
            removed = set->Entries[i].Handler;
            set->Entries[i].Handler = nullptr;
        }
    LeaveCriticalSection(lock);
    if (removed)
        removed->lpVtbl->Release(removed);
    return S_OK;
}

static void handlers_raise(HANDLERS *set, CRITICAL_SECTION *lock, IInspectable *sender, IInspectable *args)
{
    ITypedEventHandler *list[HANDLERS_MAX];
    UINT n = 0;
    EnterCriticalSection(lock);
    for (UINT i = 0; i < HANDLERS_MAX; i++)
        if (set->Entries[i].Handler) {
            list[n] = set->Entries[i].Handler;
            list[n]->lpVtbl->AddRef(list[n]);
            n++;
        }
    LeaveCriticalSection(lock);
    for (UINT i = 0; i < n; i++) {
        list[i]->lpVtbl->Invoke(list[i], sender, args);
        list[i]->lpVtbl->Release(list[i]);
    }
}

static void handlers_clear(HANDLERS *set)
{
    for (UINT i = 0; i < HANDLERS_MAX; i++)
        if (set->Entries[i].Handler) {
            set->Entries[i].Handler->lpVtbl->Release(set->Entries[i].Handler);
            set->Entries[i].Handler = nullptr;
        }
}

/* ------------------------------------------------------------------ the objects */

typedef struct TOAST_ACTION {
    WCHAR *Arguments;
    WCHAR *InputId; /* the input whose text the button sends, or null. */
} TOAST_ACTION;

typedef struct TOAST {
    WRT_OBJECT Base;
    CRITICAL_SECTION Lock;
    IXmlDocument *Content;
    BOOL HasExpiration;
    DateTime Expiration;
    WCHAR *Tag, *Group, *RemoteId;
    BOOLEAN SuppressPopup, ExpiresOnReboot;
    INT32 Mirroring, Priority;
    IInspectable *Data;
    HANDLERS Dismissed, Activated, Failed;
    /* set while shown. */
    ULONG HostId;
    BOOL Showing; /* the host has not returned its id yet. */
    WCHAR *Aumid;
    WCHAR *Launch;
    TOAST_ACTION Actions[ACTIONS_MAX];
    ULONG ActionCount;
    struct TOAST *Next; /* link in the shown table. */
} TOAST;

typedef struct ARGS {
    WRT_OBJECT Base;
    WCHAR *Arguments;
    IInspectable *Input;
    INT32 Reason;
    HRESULT Error;
} ARGS;

/* the typed text, as a property set of boxed strings. */
typedef struct SETENTRY {
    WCHAR *Key;
    WCHAR *Value;
} SETENTRY;

typedef struct PSET {
    WRT_OBJECT Base;
    CRITICAL_SECTION Lock;
    SETENTRY *Entries;
    UINT32 Count;
} PSET;

typedef struct NOTIFIER {
    WRT_OBJECT Base;
    WCHAR *Aumid;
} NOTIFIER;

typedef struct HISTORY {
    WRT_OBJECT Base;
    WCHAR *Aumid; /* null for the process's own. */
} HISTORY;

typedef struct VECTOR {
    WRT_OBJECT Base;
    TOAST **Items;
    UINT32 Count;
} VECTOR;

static const WRT_CLASS toast_class, args_activated_class, args_dismissed_class, args_failed_class, pset_class,
    notifier_class, history_class, vector_class;

static CRITICAL_SECTION g_table_lock;
static TOAST *g_shown;
static HANDLE g_event_thread;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK once(PINIT_ONCE o, PVOID parameter, PVOID *context)
{
    InitializeCriticalSection(&g_table_lock);
    return TRUE;
}

static void init(void)
{
    InitOnceExecuteOnce(&g_once, once, nullptr, nullptr);
}

/* ------------------------------------------------------------------ the host's channel */

static LONG shell_call(void *command, ULONG size)
{
    return NtUserMimeShell(MIME_SHELLOP_COMMAND, 0, (PUCHAR)command, size);
}

static HRESULT status_hr(LONG status)
{
    switch (status) {
    case (LONG)STATUS_NOT_SUPPORTED:
        return E_NOTIMPL;
    case (LONG)STATUS_INVALID_PARAMETER:
        return E_INVALIDARG;
    case (LONG)STATUS_NOT_FOUND:
        return ERROR_NOT_FOUND_HR;
    case (LONG)STATUS_INSUFFICIENT_RESOURCES:
        return E_OUTOFMEMORY;
    default:
        return E_FAIL;
    }
}

static BOOL host_can_show(void)
{
    MimeShellToast probe;
    memset(&probe, 0, sizeof(probe));
    probe.op = MIME_SHELL_TOAST_PROBE;
    return shell_call(&probe, sizeof(probe)) == 1;
}

/* ------------------------------------------------------------------ the process's identity */

static WCHAR *current_aumid(void)
{
    HMODULE shell32 = LoadLibraryW(L"shell32.dll");
    typedef HRESULT(WINAPI * GET_ID)(PWSTR *);
    GET_ID get = shell32 ? (GET_ID)(void *)GetProcAddress(shell32, "GetCurrentProcessExplicitAppUserModelID") : nullptr;
    PWSTR id = nullptr;
    if (!get || FAILED(get(&id)) || !id)
        return nullptr;
    WCHAR *copy = copy_text(id);
    typedef void(WINAPI * FREE)(LPVOID);
    HMODULE ole32 = LoadLibraryW(L"ole32.dll");
    FREE release = ole32 ? (FREE)(void *)GetProcAddress(ole32, "CoTaskMemFree") : nullptr;
    if (release)
        release(id);
    return copy;
}

/* the registered DisplayName, else the exe's ProductName, else the exe's file name. */
static void application_name(WCHAR *out, SIZE_T capacity, const WCHAR *aumid)
{
    out[0] = 0;
    WCHAR key[300];
    if (aumid && wcslen(aumid) < 200) {
        wcscpy(key, L"Software\\Classes\\AppUserModelId\\");
        wcscat(key, aumid);
        HKEY hkey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_READ, &hkey) == ERROR_SUCCESS) {
            DWORD type, size = (DWORD)(capacity * sizeof(WCHAR));
            if (RegQueryValueExW(hkey, L"DisplayName", nullptr, &type, (BYTE *)out, &size) != ERROR_SUCCESS ||
                type != REG_SZ)
                out[0] = 0;
            RegCloseKey(hkey);
        }
        if (out[0])
            return;
    }
    WCHAR path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!n || n >= MAX_PATH)
        return;
    /* find ProductName in the version resource by its UTF-16 key. */
    HMODULE module = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_AS_DATAFILE);
    if (module) {
        HRSRC found = FindResourceW(module, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(16));
        HGLOBAL loaded = found ? LoadResource(module, found) : nullptr;
        const BYTE *data = loaded ? LockResource(loaded) : nullptr;
        DWORD size = found ? SizeofResource(module, found) : 0;
        static const WCHAR name[] = L"ProductName";
        for (DWORD i = 0; data && i + sizeof(name) + 8 < size; i += 2) {
            if (memcmp(data + i, name, sizeof(name) - sizeof(WCHAR)))
                continue;
            const WCHAR *value = (const WCHAR *)(data + i + sizeof(name) - sizeof(WCHAR));
            /* the key is zero terminated, and the value starts on a 4 byte boundary after it. */
            value = (const WCHAR *)(((ULONG_PTR)value - (ULONG_PTR)data + 2 + 3) / 4 * 4 + (ULONG_PTR)data);
            SIZE_T len = 0;
            while ((const BYTE *)(value + len + 1) <= data + size && value[len] && len + 1 < capacity) {
                out[len] = value[len];
                len++;
            }
            out[len] = 0;
            break;
        }
        FreeLibrary(module);
    }
    if (out[0])
        return;
    const WCHAR *base = path;
    for (const WCHAR *p = path; *p; p++)
        if (*p == L'\\' || *p == L'/')
            base = p + 1;
    SIZE_T len = 0;
    while (base[len] && base[len] != L'.' && len + 1 < capacity) {
        out[len] = base[len];
        len++;
    }
    out[len] = 0;
}

/* ------------------------------------------------------------------ the content of a toast */

typedef struct CONTENT {
    WCHAR *Title, *Body, *Logo, *Hero, *Picture, *Placeholder;
    WCHAR *Launch;
    BOOL Silent, Urgent, Resident, Reply;
    ULONG Timeout;
    ULONG ActionCount;
    WCHAR *Labels[ACTIONS_MAX];
    TOAST_ACTION Actions[ACTIONS_MAX];
} CONTENT;

static void content_free(CONTENT *c)
{
    CbFree(c->Title);
    CbFree(c->Body);
    CbFree(c->Logo);
    CbFree(c->Hero);
    CbFree(c->Picture);
    CbFree(c->Placeholder);
    CbFree(c->Launch);
    for (ULONG i = 0; i < ACTIONS_MAX; i++) {
        CbFree(c->Labels[i]);
        CbFree(c->Actions[i].Arguments);
        CbFree(c->Actions[i].InputId);
    }
}

static XNODE *child_named(XNODE *parent, const WCHAR *name)
{
    for (XNODE *c = parent ? parent->First : nullptr; c; c = c->Next)
        if (c->Type == NodeType_ElementNode && !wcscmp(c->Name, name))
            return c;
    return nullptr;
}

static const WCHAR *attribute_of(XNODE *e, const WCHAR *name)
{
    XNODE *a = CbXmlAttribute(e, name);
    return a ? (a->Value ? a->Value : L"") : nullptr;
}

/* a file path or file:// address as a guest path, NULL for anything that is not a file. */
static WCHAR *picture_path(const WCHAR *source)
{
    if (!source || !source[0])
        return nullptr;
    const WCHAR *p = source;
    BOOL address = FALSE;
    if (!_wcsnicmp(p, L"file:///", 8)) {
        p += 8;
        address = TRUE;
    } else if (!_wcsnicmp(p, L"file://", 7)) {
        p += 7;
        address = TRUE;
    } else if (wcsstr(p, L"://")) {
        return nullptr;
    }
    SIZE_T n = wcslen(p);
    char *bytes = CbAlloc(n * 3 + 1);
    if (!bytes)
        return nullptr;
    /* addresses are percent encoded UTF-8; paths are made UTF-8 too so both share the rest. */
    SIZE_T k = 0;
    for (SIZE_T i = 0; i < n; i++) {
        if (address && p[i] == L'%' && i + 2 < n) {
            int hv = p[i + 1] >= L'0' && p[i + 1] <= L'9' ? p[i + 1] - L'0' : (p[i + 1] | 0x20) >= L'a' && (p[i + 1] | 0x20) <= L'f' ? (p[i + 1] | 0x20) - L'a' + 10 : -1;
            int lv = p[i + 2] >= L'0' && p[i + 2] <= L'9' ? p[i + 2] - L'0' : (p[i + 2] | 0x20) >= L'a' && (p[i + 2] | 0x20) <= L'f' ? (p[i + 2] | 0x20) - L'a' + 10 : -1;
            if (hv >= 0 && lv >= 0) {
                bytes[k++] = (char)(hv * 16 + lv);
                i += 2;
                continue;
            }
        }
        int len = WideCharToMultiByte(CP_UTF8, 0, p + i, 1, bytes + k, 3, nullptr, nullptr);
        if (len > 0)
            k += (SIZE_T)len;
        else
            bytes[k++] = '?';
    }
    WCHAR *out = CbAlloc((k + 1) * sizeof(WCHAR));
    if (out) {
        int len = k ? MultiByteToWideChar(CP_UTF8, 0, bytes, (int)k, out, (int)k) : 0;
        out[len > 0 ? len : 0] = 0;
        for (WCHAR *q = out; *q; q++)
            if (*q == L'/')
                *q = L'\\';
    }
    CbFree(bytes);
    return out;
}

static BOOL append(WCHAR **text, const WCHAR *more)
{
    SIZE_T a = *text ? wcslen(*text) : 0, b = wcslen(more);
    WCHAR *out = CbAlloc((a + b + 2) * sizeof(WCHAR));
    if (!out)
        return FALSE;
    if (a) {
        memcpy(out, *text, a * sizeof(WCHAR));
        out[a++] = L'\n';
    }
    memcpy(out + a, more, (b + 1) * sizeof(WCHAR));
    CbFree(*text);
    *text = out;
    return TRUE;
}

static HRESULT read_content(IXmlDocument *document, CONTENT *c)
{
    memset(c, 0, sizeof(*c));
    XNODE *doc = CbXmlFromInterface((IUnknown *)document);
    XNODE *toast = doc ? CbXmlRootElement(doc) : nullptr;
    if (!toast || wcscmp(toast->Name, L"toast"))
        return E_INVALIDARG;
    const WCHAR *launch = attribute_of(toast, L"launch");
    c->Launch = copy_text(launch);
    const WCHAR *scenario = attribute_of(toast, L"scenario");
    if (scenario && (!wcscmp(scenario, L"alarm") || !wcscmp(scenario, L"incomingCall") || !wcscmp(scenario, L"urgent")))
        c->Urgent = TRUE;
    if (scenario && !wcscmp(scenario, L"reminder"))
        c->Resident = TRUE;
    const WCHAR *duration = attribute_of(toast, L"duration");
    if (duration && !wcscmp(duration, L"long"))
        c->Timeout = 25000;
    XNODE *visual = child_named(toast, L"visual");
    XNODE *binding = nullptr;
    for (XNODE *b = visual ? visual->First : nullptr; b; b = b->Next)
        if (b->Type == NodeType_ElementNode && !wcscmp(b->Name, L"binding")) {
            const WCHAR *template = attribute_of(b, L"template");
            if (!binding || (template && !wcscmp(template, L"ToastGeneric")))
                binding = b;
        }
    BOOL first_text = TRUE;
    WCHAR *attribution = nullptr;
    for (XNODE *n = binding ? binding->First : nullptr; n; n = n->Next) {
        if (n->Type != NodeType_ElementNode)
            continue;
        if (!wcscmp(n->Name, L"text")) {
            WCHAR *text = CbXmlInnerText(n);
            const WCHAR *placement = attribute_of(n, L"placement");
            if (text && placement && !wcscmp(placement, L"attribution")) {
                append(&attribution, text);
            } else if (text && first_text) {
                c->Title = text;
                text = nullptr;
                first_text = FALSE;
            } else if (text) {
                append(&c->Body, text);
            }
            CbFree(text);
        } else if (!wcscmp(n->Name, L"image")) {
            const WCHAR *placement = attribute_of(n, L"placement");
            WCHAR *path = picture_path(attribute_of(n, L"src"));
            if (!path)
                continue;
            WCHAR **slot = !placement || !wcscmp(placement, L"inline") ? &c->Picture
                           : !wcscmp(placement, L"hero")                  ? &c->Hero
                                                                          : &c->Logo;
            if (*slot)
                CbFree(path);
            else
                *slot = path;
        }
    }
    if (attribution) {
        append(&c->Body, attribution);
        CbFree(attribution);
    }
    XNODE *actions = child_named(toast, L"actions");
    WCHAR *reply_input = nullptr;
    for (XNODE *n = actions ? actions->First : nullptr; n; n = n->Next) {
        if (n->Type != NodeType_ElementNode)
            continue;
        if (!wcscmp(n->Name, L"input")) {
            const WCHAR *type = attribute_of(n, L"type");
            if (!reply_input && type && !wcscmp(type, L"text")) {
                reply_input = copy_text(attribute_of(n, L"id"));
                c->Placeholder = copy_text(attribute_of(n, L"placeHolderContent"));
            }
        } else if (!wcscmp(n->Name, L"action") && c->ActionCount < ACTIONS_MAX) {
            const WCHAR *type = attribute_of(n, L"activationType");
            const WCHAR *args = attribute_of(n, L"arguments");
            const WCHAR *label = attribute_of(n, L"content");
            const WCHAR *input = attribute_of(n, L"hint-inputId");
            /* protocol buttons open the address and system ones belong to the desktop: neither calls the program. */
            if (type && (!wcscmp(type, L"protocol") || !wcscmp(type, L"system")))
                continue;
            ULONG i = c->ActionCount++;
            c->Labels[i] = copy_text(label);
            c->Actions[i].Arguments = copy_text(args);
            c->Actions[i].InputId = input && input[0] ? copy_text(input) : nullptr;
            if (input && reply_input && !wcscmp(input, reply_input))
                c->Reply = TRUE;
        }
    }
    CbFree(reply_input);
    XNODE *audio = child_named(toast, L"audio");
    const WCHAR *silent = audio ? attribute_of(audio, L"silent") : nullptr;
    c->Silent = silent && !wcscmp(silent, L"true");
    if (!c->Title)
        c->Title = copy_text(L"");
    return S_OK;
}

/* ------------------------------------------------------------------ the shown toasts */

static void table_add(TOAST *t)
{
    init();
    EnterCriticalSection(&g_table_lock);
    InterlockedIncrement(&t->Base.Refs);
    t->Next = g_shown;
    g_shown = t;
    LeaveCriticalSection(&g_table_lock);
}

/* the table's reference passes to the caller, who releases it. */
static BOOL table_remove(TOAST *t)
{
    BOOL found = FALSE;
    EnterCriticalSection(&g_table_lock);
    for (TOAST **p = &g_shown; *p; p = &(*p)->Next)
        if (*p == t) {
            *p = t->Next;
            t->Next = nullptr;
            found = TRUE;
            break;
        }
    LeaveCriticalSection(&g_table_lock);
    return found;
}

static TOAST *table_find(ULONG host_id)
{
    init();
    /* an answer can come before the call that showed the toast has returned its number. */
    for (int attempt = 0; attempt < 40; attempt++) {
        BOOL pending = FALSE;
        TOAST *found = nullptr;
        EnterCriticalSection(&g_table_lock);
        for (TOAST *t = g_shown; t; t = t->Next) {
            pending |= t->Showing;
            if (t->HostId == host_id) {
                InterlockedIncrement(&t->Base.Refs);
                found = t;
                break;
            }
        }
        LeaveCriticalSection(&g_table_lock);
        if (found || !pending)
            return found;
        Sleep(5);
    }
    return nullptr;
}

static void toast_unref(TOAST *t)
{
    WrtRelease(&t->Base.Ifaces[0]);
}

/* ------------------------------------------------------------------ event arguments */

WRT_INSPECTABLE_THUNKS(IToastActivatedEventArgs)
WRT_INSPECTABLE_THUNKS(IToastActivatedEventArgs2)
WRT_INSPECTABLE_THUNKS(IToastDismissedEventArgs)
WRT_INSPECTABLE_THUNKS(IToastFailedEventArgs)

static ARGS *args_of(void *iface)
{
    return (ARGS *)((WRT_IFACE *)iface)->Owner;
}

static HRESULT STDMETHODCALLTYPE act_get_arguments(IToastActivatedEventArgs *This, HSTRING *value)
{
    ARGS *a = args_of(This);
    return WindowsCreateString(a->Arguments ? a->Arguments : L"", (UINT32)(a->Arguments ? wcslen(a->Arguments) : 0), value);
}

static HRESULT STDMETHODCALLTYPE act_get_input(IToastActivatedEventArgs2 *This, IInspectable **value)
{
    ARGS *a = args_of(This);
    *value = a->Input;
    if (a->Input)
        a->Input->lpVtbl->AddRef(a->Input);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE dis_get_reason(IToastDismissedEventArgs *This, INT32 *value)
{
    *value = args_of(This)->Reason;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fail_get_error(IToastFailedEventArgs *This, HRESULT *value)
{
    *value = args_of(This)->Error;
    return S_OK;
}

static const IToastActivatedEventArgsVtbl act_vtbl = {WRT_INSPECTABLE_ENTRIES(IToastActivatedEventArgs),
                                                      act_get_arguments};
static const IToastActivatedEventArgs2Vtbl act2_vtbl = {WRT_INSPECTABLE_ENTRIES(IToastActivatedEventArgs2),
                                                        act_get_input};
static const IToastDismissedEventArgsVtbl dis_vtbl = {WRT_INSPECTABLE_ENTRIES(IToastDismissedEventArgs),
                                                      dis_get_reason};
static const IToastFailedEventArgsVtbl fail_vtbl = {WRT_INSPECTABLE_ENTRIES(IToastFailedEventArgs), fail_get_error};

static void args_destroy(WRT_OBJECT *object)
{
    ARGS *a = (ARGS *)object;
    CbFree(a->Arguments);
    if (a->Input)
        a->Input->lpVtbl->Release(a->Input);
    CbFree(a);
}

static const WRT_CLASS args_activated_class = {g_activated_class, 2,
                                               {&IID_IToastActivatedEventArgs, &IID_IToastActivatedEventArgs2}, 0,
                                               args_destroy};
static const WRT_CLASS args_dismissed_class = {g_dismissed_class, 1, {&IID_IToastDismissedEventArgs}, 0, args_destroy};
static const WRT_CLASS args_failed_class = {g_failed_class, 1, {&IID_IToastFailedEventArgs}, 0, args_destroy};

/* ------------------------------------------------------------------ the property set of the typed text */

WRT_INSPECTABLE_THUNKS(IInspectableMap)

static PSET *pset_of(void *iface)
{
    return (PSET *)((WRT_IFACE *)iface)->Owner;
}

static SETENTRY *pset_find(PSET *s, const WCHAR *key)
{
    for (UINT32 i = 0; i < s->Count; i++)
        if (!wcscmp(s->Entries[i].Key, key))
            return &s->Entries[i];
    return nullptr;
}

static HRESULT STDMETHODCALLTYPE map_lookup(IInspectableMap *This, HSTRING key, IInspectable **value)
{
    PSET *s = pset_of(This);
    WCHAR *k = copy_hstring(key);
    *value = nullptr;
    if (!k)
        return E_OUTOFMEMORY;
    EnterCriticalSection(&s->Lock);
    SETENTRY *e = pset_find(s, k);
    HRESULT hr = e ? CbBoxString(e->Value, value) : (HRESULT)0x8000000B; /* E_BOUNDS */
    LeaveCriticalSection(&s->Lock);
    CbFree(k);
    return hr;
}

static HRESULT STDMETHODCALLTYPE map_size(IInspectableMap *This, UINT32 *size)
{
    *size = pset_of(This)->Count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE map_has_key(IInspectableMap *This, HSTRING key, BOOLEAN *found)
{
    PSET *s = pset_of(This);
    WCHAR *k = copy_hstring(key);
    if (!k)
        return E_OUTOFMEMORY;
    EnterCriticalSection(&s->Lock);
    *found = pset_find(s, k) != nullptr;
    LeaveCriticalSection(&s->Lock);
    CbFree(k);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE map_get_view(IInspectableMap *This, IInspectable **view)
{
    *view = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE map_insert(IInspectableMap *This, HSTRING key, IInspectable *value, BOOLEAN *replaced)
{
    PSET *s = pset_of(This);
    WCHAR *k = copy_hstring(key), *v;
    HRESULT hr = CbUnboxString(value, &v);
    if (FAILED(hr) || !k) {
        CbFree(k);
        return FAILED(hr) ? hr : E_OUTOFMEMORY;
    }
    EnterCriticalSection(&s->Lock);
    SETENTRY *e = pset_find(s, k);
    if (e) {
        set_string(&e->Value, v);
        CbFree(k);
        *replaced = TRUE;
    } else {
        SETENTRY *grown = CbAlloc((s->Count + 1) * sizeof(SETENTRY));
        if (!grown) {
            LeaveCriticalSection(&s->Lock);
            CbFree(k);
            CbFree(v);
            return E_OUTOFMEMORY;
        }
        if (s->Count)
            memcpy(grown, s->Entries, s->Count * sizeof(SETENTRY));
        CbFree(s->Entries);
        s->Entries = grown;
        s->Entries[s->Count].Key = k;
        s->Entries[s->Count].Value = v;
        s->Count++;
        *replaced = FALSE;
    }
    LeaveCriticalSection(&s->Lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE map_remove(IInspectableMap *This, HSTRING key)
{
    PSET *s = pset_of(This);
    WCHAR *k = copy_hstring(key);
    if (!k)
        return E_OUTOFMEMORY;
    EnterCriticalSection(&s->Lock);
    SETENTRY *e = pset_find(s, k);
    if (e) {
        CbFree(e->Key);
        CbFree(e->Value);
        *e = s->Entries[--s->Count];
    }
    LeaveCriticalSection(&s->Lock);
    CbFree(k);
    return e ? S_OK : (HRESULT)0x8000000B;
}

static HRESULT STDMETHODCALLTYPE map_clear(IInspectableMap *This)
{
    PSET *s = pset_of(This);
    EnterCriticalSection(&s->Lock);
    for (UINT32 i = 0; i < s->Count; i++) {
        CbFree(s->Entries[i].Key);
        CbFree(s->Entries[i].Value);
    }
    s->Count = 0;
    LeaveCriticalSection(&s->Lock);
    return S_OK;
}

/* IPropertySet adds nothing to IInspectable, so its pointer is a plain inspectable. */
WRT_INSPECTABLE_THUNKS(IInspectable)
static const IInspectableVtbl inspectable_vtbl = {WRT_INSPECTABLE_ENTRIES(IInspectable)};

static const IInspectableMapVtbl map_vtbl = {WRT_INSPECTABLE_ENTRIES(IInspectableMap), map_lookup, map_size, map_has_key,
                                             map_get_view, map_insert, map_remove, map_clear};

static void pset_destroy(WRT_OBJECT *object)
{
    PSET *s = (PSET *)object;
    for (UINT32 i = 0; i < s->Count; i++) {
        CbFree(s->Entries[i].Key);
        CbFree(s->Entries[i].Value);
    }
    CbFree(s->Entries);
    DeleteCriticalSection(&s->Lock);
    CbFree(s);
}

/* the property set's interfaces are reached through IPropertySet, which adds no methods. */
static const WRT_CLASS pset_class = {g_set_class, 2, {&IID_IPropertySet, &IID_IInspectableMap}, 0, pset_destroy};

static PSET *pset_new(void)
{
    PSET *s = CbAlloc(sizeof(*s));
    if (!s)
        return nullptr;
    memset(s, 0, sizeof(*s));
    InitializeCriticalSection(&s->Lock);
    const void *vtables[2] = {&inspectable_vtbl, &map_vtbl};
    WrtInit(&s->Base, &pset_class, vtables);
    return s;
}

static BOOL pset_put(PSET *s, const WCHAR *key, const WCHAR *value)
{
    SETENTRY *grown = CbAlloc((s->Count + 1) * sizeof(SETENTRY));
    if (!grown)
        return FALSE;
    if (s->Count)
        memcpy(grown, s->Entries, s->Count * sizeof(SETENTRY));
    CbFree(s->Entries);
    s->Entries = grown;
    s->Entries[s->Count].Key = copy_text(key);
    s->Entries[s->Count].Value = copy_text(value);
    s->Count++;
    return TRUE;
}

/* ------------------------------------------------------------------ the toast */

WRT_INSPECTABLE_THUNKS(IToastNotification)
WRT_INSPECTABLE_THUNKS(IToastNotification2)
WRT_INSPECTABLE_THUNKS(IToastNotification3)
WRT_INSPECTABLE_THUNKS(IToastNotification4)
WRT_INSPECTABLE_THUNKS(IToastNotification6)

static TOAST *toast_of(void *iface)
{
    return (TOAST *)((WRT_IFACE *)iface)->Owner;
}

static HRESULT STDMETHODCALLTYPE toast_get_content(IToastNotification *This, IXmlDocument **value)
{
    TOAST *t = toast_of(This);
    *value = t->Content;
    t->Content->lpVtbl->AddRef(t->Content);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE toast_put_expiration(IToastNotification *This, IReferenceDateTime *value)
{
    TOAST *t = toast_of(This);
    if (!value) {
        t->HasExpiration = FALSE;
        return S_OK;
    }
    DateTime time;
    HRESULT hr = value->lpVtbl->get_Value(value, &time);
    if (SUCCEEDED(hr)) {
        t->Expiration = time;
        t->HasExpiration = TRUE;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE toast_get_expiration(IToastNotification *This, IReferenceDateTime **value)
{
    TOAST *t = toast_of(This);
    *value = nullptr;
    return t->HasExpiration ? CbBoxDateTime(t->Expiration, value) : S_OK;
}

static HRESULT STDMETHODCALLTYPE toast_add_dismissed(IToastNotification *This, ITypedEventHandler *handler,
                                                     EventRegistrationToken *token)
{
    TOAST *t = toast_of(This);
    return handlers_add(&t->Dismissed, &t->Lock, handler, token);
}

static HRESULT STDMETHODCALLTYPE toast_remove_dismissed(IToastNotification *This, EventRegistrationToken token)
{
    TOAST *t = toast_of(This);
    return handlers_remove(&t->Dismissed, &t->Lock, token);
}

static HRESULT STDMETHODCALLTYPE toast_add_activated(IToastNotification *This, ITypedEventHandler *handler,
                                                     EventRegistrationToken *token)
{
    TOAST *t = toast_of(This);
    return handlers_add(&t->Activated, &t->Lock, handler, token);
}

static HRESULT STDMETHODCALLTYPE toast_remove_activated(IToastNotification *This, EventRegistrationToken token)
{
    TOAST *t = toast_of(This);
    return handlers_remove(&t->Activated, &t->Lock, token);
}

static HRESULT STDMETHODCALLTYPE toast_add_failed(IToastNotification *This, ITypedEventHandler *handler,
                                                  EventRegistrationToken *token)
{
    TOAST *t = toast_of(This);
    return handlers_add(&t->Failed, &t->Lock, handler, token);
}

static HRESULT STDMETHODCALLTYPE toast_remove_failed(IToastNotification *This, EventRegistrationToken token)
{
    TOAST *t = toast_of(This);
    return handlers_remove(&t->Failed, &t->Lock, token);
}

static HRESULT get_text(const WCHAR *text, HSTRING *value)
{
    return WindowsCreateString(text ? text : L"", (UINT32)(text ? wcslen(text) : 0), value);
}

static HRESULT STDMETHODCALLTYPE toast_put_tag(IToastNotification2 *This, HSTRING value)
{
    WCHAR *copy = copy_hstring(value);
    if (!copy)
        return E_OUTOFMEMORY;
    set_string(&toast_of(This)->Tag, copy);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE toast_get_tag(IToastNotification2 *This, HSTRING *value)
{
    return get_text(toast_of(This)->Tag, value);
}

static HRESULT STDMETHODCALLTYPE toast_put_group(IToastNotification2 *This, HSTRING value)
{
    WCHAR *copy = copy_hstring(value);
    if (!copy)
        return E_OUTOFMEMORY;
    set_string(&toast_of(This)->Group, copy);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE toast_get_group(IToastNotification2 *This, HSTRING *value)
{
    return get_text(toast_of(This)->Group, value);
}

static HRESULT STDMETHODCALLTYPE toast_put_suppress(IToastNotification2 *This, BOOLEAN value)
{
    toast_of(This)->SuppressPopup = value != 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE toast_get_suppress(IToastNotification2 *This, BOOLEAN *value)
{
    *value = toast_of(This)->SuppressPopup;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE toast_get_mirroring(IToastNotification3 *This, INT32 *value)
{
    *value = toast_of(This)->Mirroring;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE toast_put_mirroring(IToastNotification3 *This, INT32 value)
{
    toast_of(This)->Mirroring = value;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE toast_get_remote(IToastNotification3 *This, HSTRING *value)
{
    return get_text(toast_of(This)->RemoteId, value);
}

static HRESULT STDMETHODCALLTYPE toast_put_remote(IToastNotification3 *This, HSTRING value)
{
    WCHAR *copy = copy_hstring(value);
    if (!copy)
        return E_OUTOFMEMORY;
    set_string(&toast_of(This)->RemoteId, copy);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE toast_get_data(IToastNotification4 *This, IInspectable **value)
{
    TOAST *t = toast_of(This);
    *value = t->Data;
    if (t->Data)
        t->Data->lpVtbl->AddRef(t->Data);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE toast_put_data(IToastNotification4 *This, IInspectable *value)
{
    TOAST *t = toast_of(This);
    if (value)
        value->lpVtbl->AddRef(value);
    if (t->Data)
        t->Data->lpVtbl->Release(t->Data);
    t->Data = value;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE toast_get_priority(IToastNotification4 *This, INT32 *value)
{
    *value = toast_of(This)->Priority;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE toast_put_priority(IToastNotification4 *This, INT32 value)
{
    toast_of(This)->Priority = value;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE toast_get_reboot(IToastNotification6 *This, BOOLEAN *value)
{
    *value = toast_of(This)->ExpiresOnReboot;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE toast_put_reboot(IToastNotification6 *This, BOOLEAN value)
{
    toast_of(This)->ExpiresOnReboot = value != 0;
    return S_OK;
}

static const IToastNotificationVtbl toast_vtbl = {
    WRT_INSPECTABLE_ENTRIES(IToastNotification), toast_get_content, toast_put_expiration, toast_get_expiration,
    toast_add_dismissed, toast_remove_dismissed, toast_add_activated, toast_remove_activated, toast_add_failed,
    toast_remove_failed};
static const IToastNotification2Vtbl toast2_vtbl = {WRT_INSPECTABLE_ENTRIES(IToastNotification2), toast_put_tag,
                                                    toast_get_tag, toast_put_group, toast_get_group,
                                                    toast_put_suppress, toast_get_suppress};
static const IToastNotification3Vtbl toast3_vtbl = {WRT_INSPECTABLE_ENTRIES(IToastNotification3),
                                                    toast_get_mirroring, toast_put_mirroring, toast_get_remote,
                                                    toast_put_remote};
static const IToastNotification4Vtbl toast4_vtbl = {WRT_INSPECTABLE_ENTRIES(IToastNotification4), toast_get_data,
                                                    toast_put_data, toast_get_priority, toast_put_priority};
static const IToastNotification6Vtbl toast6_vtbl = {WRT_INSPECTABLE_ENTRIES(IToastNotification6), toast_get_reboot,
                                                    toast_put_reboot};

static void toast_destroy(WRT_OBJECT *object)
{
    TOAST *t = (TOAST *)object;
    handlers_clear(&t->Dismissed);
    handlers_clear(&t->Activated);
    handlers_clear(&t->Failed);
    if (t->Content)
        t->Content->lpVtbl->Release(t->Content);
    if (t->Data)
        t->Data->lpVtbl->Release(t->Data);
    CbFree(t->Tag);
    CbFree(t->Group);
    CbFree(t->RemoteId);
    CbFree(t->Aumid);
    CbFree(t->Launch);
    for (ULONG i = 0; i < ACTIONS_MAX; i++) {
        CbFree(t->Actions[i].Arguments);
        CbFree(t->Actions[i].InputId);
    }
    DeleteCriticalSection(&t->Lock);
    CbFree(t);
}

static const WRT_CLASS toast_class = {CbToastClass, 5,
                                      {&IID_IToastNotification, &IID_IToastNotification2, &IID_IToastNotification3,
                                       &IID_IToastNotification4, &IID_IToastNotification6},
                                      0, toast_destroy};

static HRESULT toast_new(IXmlDocument *content, TOAST **out)
{
    TOAST *t = CbAlloc(sizeof(*t));
    *out = nullptr;
    if (!t)
        return E_OUTOFMEMORY;
    memset(t, 0, sizeof(*t));
    InitializeCriticalSection(&t->Lock);
    const void *vtables[5] = {&toast_vtbl, &toast2_vtbl, &toast3_vtbl, &toast4_vtbl, &toast6_vtbl};
    WrtInit(&t->Base, &toast_class, vtables);
    t->Content = content;
    content->lpVtbl->AddRef(content);
    t->Tag = copy_text(L"");
    t->Group = copy_text(L"");
    t->RemoteId = copy_text(L"");
    *out = t;
    return S_OK;
}

/* ------------------------------------------------------------------ events from the desktop */

static ARGS *args_new(const WRT_CLASS *class, const void *const *vtables)
{
    ARGS *a = CbAlloc(sizeof(*a));
    if (!a)
        return nullptr;
    memset(a, 0, sizeof(*a));
    WrtInit(&a->Base, class, vtables);
    return a;
}

static void raise_activated(TOAST *t, LONG action, const WCHAR *typed)
{
    static const void *const vtables[2] = {&act_vtbl, &act2_vtbl};
    ARGS *a = args_new(&args_activated_class, vtables);
    if (!a)
        return;
    const WCHAR *arguments = action >= 0 && (ULONG)action < t->ActionCount ? t->Actions[action].Arguments : t->Launch;
    a->Arguments = copy_text(arguments);
    PSET *set = pset_new();
    if (set) {
        if (typed && action >= 0 && (ULONG)action < t->ActionCount && t->Actions[action].InputId)
            pset_put(set, t->Actions[action].InputId, typed);
        a->Input = (IInspectable *)&set->Base.Ifaces[0];
    }
    handlers_raise(&t->Activated, &t->Lock, (IInspectable *)&t->Base.Ifaces[0], (IInspectable *)&a->Base.Ifaces[0]);
    WrtRelease(&a->Base.Ifaces[0]);
}

static void raise_dismissed(TOAST *t, INT32 reason)
{
    static const void *const vtables[1] = {&dis_vtbl};
    ARGS *a = args_new(&args_dismissed_class, vtables);
    if (!a)
        return;
    a->Reason = reason;
    handlers_raise(&t->Dismissed, &t->Lock, (IInspectable *)&t->Base.Ifaces[0], (IInspectable *)&a->Base.Ifaces[0]);
    WrtRelease(&a->Base.Ifaces[0]);
}

static void raise_failed(TOAST *t, HRESULT error)
{
    static const void *const vtables[1] = {&fail_vtbl};
    ARGS *a = args_new(&args_failed_class, vtables);
    if (!a)
        return;
    a->Error = error;
    handlers_raise(&t->Failed, &t->Lock, (IInspectable *)&t->Base.Ifaces[0], (IInspectable *)&a->Base.Ifaces[0]);
    WrtRelease(&a->Base.Ifaces[0]);
}

/* ------------------------------------------------------------------ activation through the application's COM server */

/* in activate.c. */
BOOL CbActivateApplication(const WCHAR *aumid, const WCHAR *arguments, const WCHAR *key, const WCHAR *value);

typedef struct ACTIVATION {
    WCHAR *Aumid, *Arguments, *Key, *Value;
} ACTIVATION;

static void activation_free(ACTIVATION *a)
{
    CbFree(a->Aumid);
    CbFree(a->Arguments);
    CbFree(a->Key);
    CbFree(a->Value);
    CbFree(a);
}

/* on its own thread, since the server may take a while to start. */
static DWORD WINAPI activation_thread(void *param)
{
    ACTIVATION *a = param;
    CbActivateApplication(a->Aumid, a->Arguments, a->Key, a->Value);
    activation_free(a);
    return 0;
}

/* the toast was acted on and its program registered no handler: call its COM server. */
static void activate_through_server(TOAST *t, LONG action, const WCHAR *typed)
{
    ACTIVATION *a = CbAlloc(sizeof(*a));
    if (!a)
        return;
    memset(a, 0, sizeof(*a));
    BOOL action_ok = action >= 0 && (ULONG)action < t->ActionCount;
    a->Aumid = copy_text(t->Aumid);
    a->Arguments = copy_text(action_ok ? t->Actions[action].Arguments : t->Launch);
    if (typed && action_ok && t->Actions[action].InputId) {
        a->Key = copy_text(t->Actions[action].InputId);
        a->Value = copy_text(typed);
    }
    HANDLE thread = a->Aumid && a->Arguments ? CreateThread(nullptr, 0, activation_thread, a, 0, nullptr) : nullptr;
    if (thread)
        CloseHandle(thread);
    else
        activation_free(a);
}

static BOOL has_handlers(HANDLERS *set, CRITICAL_SECTION *lock)
{
    BOOL any = FALSE;
    EnterCriticalSection(lock);
    for (UINT i = 0; i < HANDLERS_MAX; i++)
        any |= set->Entries[i].Handler != nullptr;
    LeaveCriticalSection(lock);
    return any;
}

static void dispatch(const MIME_SHELL_EVENT *e)
{
    TOAST *t = table_find(e->Id);
    if (!t)
        return;
    switch (e->Kind) {
    case MIME_SHELL_EVENT_TOAST_ACTIVATED:
        /* the desktop closes an answered notification, so the toast is done. */
        if (table_remove(t))
            toast_unref(t);
        if (has_handlers(&t->Activated, &t->Lock))
            raise_activated(t, e->Arg, nullptr);
        else
            activate_through_server(t, e->Arg, nullptr);
        break;
    case MIME_SHELL_EVENT_TOAST_REPLIED: {
        WCHAR typed[sizeof(e->Reply) / sizeof(e->Reply[0])];
        for (ULONG i = 0; i < e->ReplyLength && i + 1 < RTL_NUMBER_OF(typed); i++)
            typed[i] = e->Reply[i];
        typed[e->ReplyLength < RTL_NUMBER_OF(typed) ? e->ReplyLength : RTL_NUMBER_OF(typed) - 1] = 0;
        if (table_remove(t))
            toast_unref(t);
        if (has_handlers(&t->Activated, &t->Lock))
            raise_activated(t, e->Arg, typed);
        else
            activate_through_server(t, e->Arg, typed);
        break;
    }
    case MIME_SHELL_EVENT_TOAST_DISMISSED:
        if (e->Arg == 2) {
            /* a timed out popup stays in the history, as in the action center, with no popup left to hide. */
            EnterCriticalSection(&g_table_lock);
            t->HostId = 0;
            LeaveCriticalSection(&g_table_lock);
        } else if (table_remove(t)) {
            toast_unref(t);
        }
        raise_dismissed(t, e->Arg == 1 ? 1 : e->Arg == 2 ? 2 : 0);
        break;
    case MIME_SHELL_EVENT_TOAST_FAILED:
        if (table_remove(t))
            toast_unref(t);
        raise_failed(t, (HRESULT)e->Arg);
        break;
    }
    toast_unref(t);
}

static DWORD WINAPI event_thread(void *param)
{
    for (;;) {
        MIME_SHELL_EVENT e;
        LONG r = NtUserMimeShell(MIME_SHELLOP_WAIT, 0xFFFFFFFF, (PUCHAR)&e, sizeof(e));
        if (r == 1)
            dispatch(&e);
        else if (r < 0)
            Sleep(1000);
    }
    return 0;
}

static void ensure_event_thread(void)
{
    init();
    EnterCriticalSection(&g_table_lock);
    if (!g_event_thread)
        g_event_thread = CreateThread(nullptr, 0, event_thread, nullptr, 0, nullptr);
    LeaveCriticalSection(&g_table_lock);
}

/* ------------------------------------------------------------------ showing and hiding */

static void fill_message(MimeShellToast *m, NOTIFIER *n, TOAST *t, const CONTENT *c)
{
    m->op = MIME_SHELL_TOAST_SHOW;
    if (c->Silent)
        m->flags |= MIME_TOAST_SILENT;
    if (c->Urgent || t->Priority == 1)
        m->flags |= MIME_TOAST_URGENT;
    if (c->Resident)
        m->flags |= MIME_TOAST_RESIDENT;
    if (c->Reply)
        m->flags |= MIME_TOAST_REPLY;
    m->timeout_ms = c->Timeout;
    m->action_count = c->ActionCount;
    WCHAR app[64];
    application_name(app, RTL_NUMBER_OF(app), n->Aumid);
    put_text(m->aumid, RTL_NUMBER_OF(m->aumid), n->Aumid);
    put_text(m->app, RTL_NUMBER_OF(m->app), app);
    put_text(m->tag, RTL_NUMBER_OF(m->tag), t->Tag);
    put_text(m->group, RTL_NUMBER_OF(m->group), t->Group);
    put_text(m->title, RTL_NUMBER_OF(m->title), c->Title);
    put_text(m->body, RTL_NUMBER_OF(m->body), c->Body);
    put_text(m->logo, RTL_NUMBER_OF(m->logo), c->Logo);
    put_text(m->hero, RTL_NUMBER_OF(m->hero), c->Hero);
    put_text(m->picture, RTL_NUMBER_OF(m->picture), c->Picture);
    put_text(m->placeholder, RTL_NUMBER_OF(m->placeholder), c->Placeholder);
    for (ULONG i = 0; i < c->ActionCount; i++)
        put_text(m->actions[i].label, RTL_NUMBER_OF(m->actions[i].label), c->Labels[i]);
}

static HRESULT show_toast(NOTIFIER *n, TOAST *t)
{
    CONTENT c;
    HRESULT hr = read_content(t->Content, &c);
    if (FAILED(hr)) {
        content_free(&c);
        return hr;
    }
    /* a toast without a popup only goes to the history, which the desktop's notification server has no room for. */
    BOOL quiet = t->SuppressPopup;
    MimeShellToast *m = nullptr;
    if (!quiet) {
        m = CbAlloc(sizeof(*m));
        if (!m) {
            content_free(&c);
            return E_OUTOFMEMORY;
        }
        memset(m, 0, sizeof(*m));
        fill_message(m, n, t, &c);
    }
    /* the host replaces an earlier toast with the same tag and group; the table forgets it too. */
    TOAST *replaced = nullptr;
    if (t->Tag && t->Tag[0]) {
        init();
        EnterCriticalSection(&g_table_lock);
        for (TOAST **p = &g_shown; *p; p = &(*p)->Next)
            if (*p != t && same_text((*p)->Aumid, n->Aumid) && same_text((*p)->Tag, t->Tag) &&
                same_text((*p)->Group, t->Group)) {
                replaced = *p;
                *p = replaced->Next;
                replaced->Next = nullptr;
                break;
            }
        LeaveCriticalSection(&g_table_lock);
    }
    if (replaced)
        toast_unref(replaced);
    set_string(&t->Aumid, copy_text(n->Aumid));
    set_string(&t->Launch, c.Launch);
    c.Launch = nullptr;
    for (ULONG i = 0; i < ACTIONS_MAX; i++) {
        CbFree(t->Actions[i].Arguments);
        CbFree(t->Actions[i].InputId);
        t->Actions[i] = c.Actions[i];
        c.Actions[i].Arguments = c.Actions[i].InputId = nullptr;
    }
    t->ActionCount = c.ActionCount;
    content_free(&c);
    if (quiet) {
        if (table_remove(t))
            toast_unref(t);
        t->HostId = 0;
        table_add(t);
        return S_OK;
    }
    ensure_event_thread();
    /* add to the table before the host can answer; the id is filled in when the call returns. */
    if (table_remove(t))
        toast_unref(t);
    t->HostId = 0;
    t->Showing = TRUE;
    table_add(t);
    LONG result = shell_call(m, sizeof(*m));
    CbFree(m);
    EnterCriticalSection(&g_table_lock);
    if (result > 0)
        t->HostId = (ULONG)result;
    t->Showing = FALSE;
    LeaveCriticalSection(&g_table_lock);
    if (result <= 0) {
        if (table_remove(t))
            toast_unref(t);
        return status_hr(result);
    }
    return S_OK;
}

static HRESULT hide_toast(TOAST *t)
{
    if (!t->HostId) {
        /* in the history only: no popup was made, or it timed out. */
        if (table_remove(t)) {
            raise_dismissed(t, 1);
            toast_unref(t);
        }
        return S_OK;
    }
    MimeShellToast m;
    memset(&m, 0, sizeof(m));
    m.op = MIME_SHELL_TOAST_HIDE;
    m.id = t->HostId;
    LONG r = shell_call(&m, sizeof(m));
    return r < 0 ? status_hr(r) : S_OK;
}

/* drops the history-only toasts a removal names; the host has no popup for them. */
static void forget_quiet(const WCHAR *aumid, const WCHAR *tag, const WCHAR *group)
{
    TOAST *gone = nullptr;
    init();
    EnterCriticalSection(&g_table_lock);
    for (TOAST **p = &g_shown; *p;) {
        TOAST *t = *p;
        if (!t->HostId && !t->Showing && same_text(t->Aumid, aumid) && (!tag || same_text(t->Tag, tag)) &&
            (!group || same_text(t->Group, group))) {
            *p = t->Next;
            t->Next = gone;
            gone = t;
        } else {
            p = &t->Next;
        }
    }
    LeaveCriticalSection(&g_table_lock);
    while (gone) {
        TOAST *next = gone->Next;
        gone->Next = nullptr;
        toast_unref(gone);
        gone = next;
    }
}

static HRESULT remove_toasts(const WCHAR *aumid, const WCHAR *tag, const WCHAR *group)
{
    forget_quiet(aumid, tag, group);
    MimeShellToast m;
    memset(&m, 0, sizeof(m));
    m.op = MIME_SHELL_TOAST_REMOVE;
    put_text(m.aumid, RTL_NUMBER_OF(m.aumid), aumid);
    put_text(m.tag, RTL_NUMBER_OF(m.tag), tag);
    put_text(m.group, RTL_NUMBER_OF(m.group), group);
    LONG r = shell_call(&m, sizeof(m));
    return r < 0 ? status_hr(r) : S_OK;
}

/* ------------------------------------------------------------------ vectors of toasts */

WRT_INSPECTABLE_THUNKS(IVectorViewPtr)

static VECTOR *vector_of(void *iface)
{
    return (VECTOR *)((WRT_IFACE *)iface)->Owner;
}

static HRESULT STDMETHODCALLTYPE vec_get_at(IVectorViewPtr *This, UINT32 index, void **item)
{
    VECTOR *v = vector_of(This);
    *item = nullptr;
    if (index >= v->Count)
        return (HRESULT)0x8000000B;
    *item = &v->Items[index]->Base.Ifaces[0];
    WrtAddRef(&v->Items[index]->Base.Ifaces[0]);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vec_size(IVectorViewPtr *This, UINT32 *size)
{
    *size = vector_of(This)->Count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vec_index_of(IVectorViewPtr *This, void *item, UINT32 *index, BOOLEAN *found)
{
    VECTOR *v = vector_of(This);
    *found = FALSE;
    *index = 0;
    for (UINT32 i = 0; i < v->Count; i++)
        if (item == (void *)&v->Items[i]->Base.Ifaces[0]) {
            *index = i;
            *found = TRUE;
            break;
        }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vec_get_many(IVectorViewPtr *This, UINT32 start, UINT32 capacity, void **items,
                                              UINT32 *actual)
{
    VECTOR *v = vector_of(This);
    UINT32 n = 0;
    for (UINT32 i = start; i < v->Count && n < capacity; i++, n++) {
        items[n] = &v->Items[i]->Base.Ifaces[0];
        WrtAddRef(&v->Items[i]->Base.Ifaces[0]);
    }
    *actual = n;
    return S_OK;
}

static const IVectorViewPtrVtbl vec_vtbl = {WRT_INSPECTABLE_ENTRIES(IVectorViewPtr), vec_get_at, vec_size, vec_index_of,
                                            vec_get_many};

static void vector_destroy(WRT_OBJECT *object)
{
    VECTOR *v = (VECTOR *)object;
    for (UINT32 i = 0; i < v->Count; i++)
        toast_unref(v->Items[i]);
    CbFree(v->Items);
    CbFree(v);
}

static const WRT_CLASS vector_class = {g_vector_class, 1, {&IID_IVectorViewToast}, 0, vector_destroy};

/* this process's toasts for an application, oldest first. */
static HRESULT new_history_vector(const WCHAR *aumid, IVectorViewPtr **out)
{
    VECTOR *v = CbAlloc(sizeof(*v));
    *out = nullptr;
    if (!v)
        return E_OUTOFMEMORY;
    memset(v, 0, sizeof(*v));
    const void *vtables[1] = {&vec_vtbl};
    WrtInit(&v->Base, &vector_class, vtables);
    init();
    EnterCriticalSection(&g_table_lock);
    UINT32 n = 0;
    for (TOAST *t = g_shown; t; t = t->Next)
        if (!aumid || same_text(t->Aumid, aumid))
            n++;
    v->Items = n ? CbAlloc(n * sizeof(TOAST *)) : nullptr;
    if (n && !v->Items) {
        LeaveCriticalSection(&g_table_lock);
        CbFree(v);
        return E_OUTOFMEMORY;
    }
    /* the table is newest first. */
    UINT32 k = n;
    for (TOAST *t = g_shown; t; t = t->Next)
        if (!aumid || same_text(t->Aumid, aumid)) {
            InterlockedIncrement(&t->Base.Refs);
            v->Items[--k] = t;
        }
    v->Count = n;
    LeaveCriticalSection(&g_table_lock);
    *out = (IVectorViewPtr *)&v->Base.Ifaces[0];
    return S_OK;
}

/* ------------------------------------------------------------------ the notifier */

WRT_INSPECTABLE_THUNKS(IToastNotifier)
WRT_INSPECTABLE_THUNKS(IToastNotifier2)

static NOTIFIER *notifier_of(void *iface)
{
    return (NOTIFIER *)((WRT_IFACE *)iface)->Owner;
}

static HRESULT STDMETHODCALLTYPE notifier_show(IToastNotifier *This, IToastNotification *toast)
{
    if (!toast)
        return E_INVALIDARG;
    IToastNotification *own;
    if (FAILED(toast->lpVtbl->QueryInterface(toast, &IID_IToastNotification, (void **)&own)))
        return E_INVALIDARG;
    TOAST *t = toast_of(own);
    HRESULT hr = t->Base.Class == &toast_class ? show_toast(notifier_of(This), t) : E_INVALIDARG;
    own->lpVtbl->Release(own);
    return hr;
}

static HRESULT STDMETHODCALLTYPE notifier_hide(IToastNotifier *This, IToastNotification *toast)
{
    if (!toast)
        return E_INVALIDARG;
    IToastNotification *own;
    if (FAILED(toast->lpVtbl->QueryInterface(toast, &IID_IToastNotification, (void **)&own)))
        return E_INVALIDARG;
    TOAST *t = toast_of(own);
    HRESULT hr = t->Base.Class == &toast_class ? hide_toast(t) : E_INVALIDARG;
    own->lpVtbl->Release(own);
    return hr;
}

static HRESULT STDMETHODCALLTYPE notifier_get_setting(IToastNotifier *This, INT32 *value)
{
    *value = NotificationSetting_Enabled;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE notifier_schedule(IToastNotifier *This, IInspectable *toast)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE notifier_scheduled(IToastNotifier *This, IVectorViewPtr **value)
{
    *value = nullptr;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE notifier_update_tag_group(IToastNotifier2 *This, IInspectable *data, HSTRING tag,
                                                           HSTRING group, INT32 *result)
{
    NOTIFIER *n = notifier_of(This);
    WCHAR *t = copy_hstring(tag), *g = copy_hstring(group);
    *result = NotificationUpdateResult_NotFound;
    if (t && g) {
        init();
        EnterCriticalSection(&g_table_lock);
        for (TOAST *o = g_shown; o; o = o->Next)
            if (same_text(o->Aumid, n->Aumid) && same_text(o->Tag, t) && same_text(o->Group, g))
                *result = NotificationUpdateResult_Succeeded;
        LeaveCriticalSection(&g_table_lock);
    }
    CbFree(t);
    CbFree(g);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE notifier_update_tag(IToastNotifier2 *This, IInspectable *data, HSTRING tag,
                                                     INT32 *result)
{
    HSTRING empty = nullptr;
    return notifier_update_tag_group(This, data, tag, empty, result);
}

static const IToastNotifierVtbl notifier_vtbl = {WRT_INSPECTABLE_ENTRIES(IToastNotifier), notifier_show, notifier_hide,
                                                 notifier_get_setting, notifier_schedule, notifier_schedule,
                                                 notifier_scheduled};
static const IToastNotifier2Vtbl notifier2_vtbl = {WRT_INSPECTABLE_ENTRIES(IToastNotifier2), notifier_update_tag_group,
                                                   notifier_update_tag};

static void notifier_destroy(WRT_OBJECT *object)
{
    NOTIFIER *n = (NOTIFIER *)object;
    CbFree(n->Aumid);
    CbFree(n);
}

static const WRT_CLASS notifier_class = {g_notifier_class, 2, {&IID_IToastNotifier, &IID_IToastNotifier2}, 0,
                                         notifier_destroy};

/* the process's own notifier when no id is given. */
static HRESULT notifier_new(const WCHAR *aumid, IToastNotifier **out)
{
    *out = nullptr;
    WCHAR *id = aumid ? copy_text(aumid) : current_aumid();
    if (!id)
        return ERROR_NOT_FOUND_HR;
    if (!id[0]) {
        CbFree(id);
        return E_INVALIDARG;
    }
    NOTIFIER *n = CbAlloc(sizeof(*n));
    if (!n) {
        CbFree(id);
        return E_OUTOFMEMORY;
    }
    memset(n, 0, sizeof(*n));
    const void *vtables[2] = {&notifier_vtbl, &notifier2_vtbl};
    WrtInit(&n->Base, &notifier_class, vtables);
    n->Aumid = id;
    *out = (IToastNotifier *)&n->Base.Ifaces[0];
    return S_OK;
}

/* ------------------------------------------------------------------ the history */

WRT_INSPECTABLE_THUNKS(IToastNotificationHistory)
WRT_INSPECTABLE_THUNKS(IToastNotificationHistory2)

static HISTORY *history_of(void *iface)
{
    return (HISTORY *)((WRT_IFACE *)iface)->Owner;
}

/* the named application, else the history's own, else the process's. */
static WCHAR *history_aumid(HISTORY *h, HSTRING named)
{
    if (named)
        return copy_hstring(named);
    return h->Aumid ? copy_text(h->Aumid) : current_aumid();
}

static HRESULT history_remove(HISTORY *h, HSTRING aumid_named, HSTRING tag, HSTRING group)
{
    WCHAR *aumid = history_aumid(h, aumid_named);
    WCHAR *t = tag ? copy_hstring(tag) : nullptr, *g = group ? copy_hstring(group) : nullptr;
    HRESULT hr = aumid ? remove_toasts(aumid, t, g) : ERROR_NOT_FOUND_HR;
    CbFree(aumid);
    CbFree(t);
    CbFree(g);
    return hr;
}

static HRESULT STDMETHODCALLTYPE hist_remove_group(IToastNotificationHistory *This, HSTRING group)
{
    return history_remove(history_of(This), nullptr, nullptr, group);
}

static HRESULT STDMETHODCALLTYPE hist_remove_group_id(IToastNotificationHistory *This, HSTRING group, HSTRING id)
{
    return history_remove(history_of(This), id, nullptr, group);
}

static HRESULT STDMETHODCALLTYPE hist_remove_tagged_id(IToastNotificationHistory *This, HSTRING tag, HSTRING group,
                                                       HSTRING id)
{
    return history_remove(history_of(This), id, tag, group);
}

static HRESULT STDMETHODCALLTYPE hist_remove_tagged(IToastNotificationHistory *This, HSTRING tag, HSTRING group)
{
    return history_remove(history_of(This), nullptr, tag, group);
}

static HRESULT STDMETHODCALLTYPE hist_remove(IToastNotificationHistory *This, HSTRING tag)
{
    return history_remove(history_of(This), nullptr, tag, nullptr);
}

static HRESULT STDMETHODCALLTYPE hist_clear(IToastNotificationHistory *This)
{
    return history_remove(history_of(This), nullptr, nullptr, nullptr);
}

static HRESULT STDMETHODCALLTYPE hist_clear_id(IToastNotificationHistory *This, HSTRING id)
{
    return history_remove(history_of(This), id, nullptr, nullptr);
}

static HRESULT STDMETHODCALLTYPE hist_get(IToastNotificationHistory2 *This, IVectorViewPtr **value)
{
    HISTORY *h = history_of(This);
    WCHAR *aumid = history_aumid(h, nullptr);
    HRESULT hr = new_history_vector(aumid, value);
    CbFree(aumid);
    return hr;
}

static HRESULT STDMETHODCALLTYPE hist_get_id(IToastNotificationHistory2 *This, HSTRING id, IVectorViewPtr **value)
{
    WCHAR *aumid = copy_hstring(id);
    HRESULT hr = new_history_vector(aumid, value);
    CbFree(aumid);
    return hr;
}

static const IToastNotificationHistoryVtbl hist_vtbl = {
    WRT_INSPECTABLE_ENTRIES(IToastNotificationHistory), hist_remove_group, hist_remove_group_id, hist_remove_tagged_id,
    hist_remove_tagged, hist_remove, hist_clear, hist_clear_id};
static const IToastNotificationHistory2Vtbl hist2_vtbl = {WRT_INSPECTABLE_ENTRIES(IToastNotificationHistory2), hist_get,
                                                          hist_get_id};

static void history_destroy(WRT_OBJECT *object)
{
    HISTORY *h = (HISTORY *)object;
    CbFree(h->Aumid);
    CbFree(h);
}

static const WRT_CLASS history_class = {g_history_class, 2, {&IID_IToastNotificationHistory, &IID_IToastNotificationHistory2},
                                        0, history_destroy};

static HRESULT history_new(IToastNotificationHistory **out)
{
    HISTORY *h = CbAlloc(sizeof(*h));
    *out = nullptr;
    if (!h)
        return E_OUTOFMEMORY;
    memset(h, 0, sizeof(*h));
    const void *vtables[2] = {&hist_vtbl, &hist2_vtbl};
    WrtInit(&h->Base, &history_class, vtables);
    *out = (IToastNotificationHistory *)&h->Base.Ifaces[0];
    return S_OK;
}

/* ------------------------------------------------------------------ the manager */

WRT_INSPECTABLE_THUNKS(IToastNotificationManagerStatics)
WRT_INSPECTABLE_THUNKS(IToastNotificationManagerStatics2)
WRT_INSPECTABLE_THUNKS(IToastNotificationManagerStatics4)
WRT_INSPECTABLE_THUNKS(IToastNotificationManagerStatics5)
WRT_INSPECTABLE_THUNKS(IToastNotificationManagerForUser)
WRT_INSPECTABLE_THUNKS(IToastNotificationFactory)

static const WCHAR *g_templates[] = {
    L"<toast><visual><binding template=\"ToastImageAndText01\"><image id=\"1\" src=\"\"/><text id=\"1\"></text></binding></visual></toast>",
    L"<toast><visual><binding template=\"ToastImageAndText02\"><image id=\"1\" src=\"\"/><text id=\"1\"></text><text id=\"2\"></text></binding></visual></toast>",
    L"<toast><visual><binding template=\"ToastImageAndText03\"><image id=\"1\" src=\"\"/><text id=\"1\"></text><text id=\"2\"></text></binding></visual></toast>",
    L"<toast><visual><binding template=\"ToastImageAndText04\"><image id=\"1\" src=\"\"/><text id=\"1\"></text><text id=\"2\"></text><text id=\"3\"></text></binding></visual></toast>",
    L"<toast><visual><binding template=\"ToastText01\"><text id=\"1\"></text></binding></visual></toast>",
    L"<toast><visual><binding template=\"ToastText02\"><text id=\"1\"></text><text id=\"2\"></text></binding></visual></toast>",
    L"<toast><visual><binding template=\"ToastText03\"><text id=\"1\"></text><text id=\"2\"></text></binding></visual></toast>",
    L"<toast><visual><binding template=\"ToastText04\"><text id=\"1\"></text><text id=\"2\"></text><text id=\"3\"></text></binding></visual></toast>",
};

static HRESULT STDMETHODCALLTYPE mgr_create_notifier(IToastNotificationManagerStatics *This, IToastNotifier **notifier)
{
    return notifier_new(nullptr, notifier);
}

static HRESULT STDMETHODCALLTYPE mgr_create_notifier_id(IToastNotificationManagerStatics *This, HSTRING id,
                                                        IToastNotifier **notifier)
{
    WCHAR *aumid = copy_hstring(id);
    *notifier = nullptr;
    if (!aumid)
        return E_OUTOFMEMORY;
    HRESULT hr = notifier_new(aumid, notifier);
    CbFree(aumid);
    return hr;
}

static HRESULT STDMETHODCALLTYPE mgr_get_template(IToastNotificationManagerStatics *This, ToastTemplateType type,
                                                  IXmlDocument **content)
{
    *content = nullptr;
    if ((ULONG)type >= RTL_NUMBER_OF(g_templates))
        return E_INVALIDARG;
    HRESULT hr = CbXmlNewDocument(content);
    if (SUCCEEDED(hr))
        hr = CbXmlLoadText(CbXmlFromInterface((IUnknown *)*content), g_templates[type], wcslen(g_templates[type]));
    if (FAILED(hr) && *content) {
        (*content)->lpVtbl->Release(*content);
        *content = nullptr;
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE mgr_get_history(IToastNotificationManagerStatics2 *This,
                                                 IToastNotificationHistory **value)
{
    return history_new(value);
}

static HRESULT new_for_user(IToastNotificationManagerForUser **out);

static HRESULT STDMETHODCALLTYPE mgr_get_for_user(IToastNotificationManagerStatics4 *This, IInspectable *user,
                                                  IToastNotificationManagerForUser **value)
{
    return new_for_user(value);
}

static HRESULT STDMETHODCALLTYPE mgr_configure_mirroring(IToastNotificationManagerStatics4 *This, INT32 value)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mgr_get_default(IToastNotificationManagerStatics5 *This,
                                                 IToastNotificationManagerForUser **value)
{
    return new_for_user(value);
}

static HRESULT STDMETHODCALLTYPE user_create_notifier(IToastNotificationManagerForUser *This, IToastNotifier **notifier)
{
    return notifier_new(nullptr, notifier);
}

static HRESULT STDMETHODCALLTYPE user_create_notifier_id(IToastNotificationManagerForUser *This, HSTRING id,
                                                         IToastNotifier **notifier)
{
    return mgr_create_notifier_id(nullptr, id, notifier);
}

static HRESULT STDMETHODCALLTYPE user_get_history(IToastNotificationManagerForUser *This,
                                                  IToastNotificationHistory **value)
{
    return history_new(value);
}

static HRESULT STDMETHODCALLTYPE user_get_user(IToastNotificationManagerForUser *This, IInspectable **value)
{
    *value = nullptr;
    return E_NOTIMPL;
}

static const IToastNotificationManagerStaticsVtbl mgr_vtbl = {WRT_INSPECTABLE_ENTRIES(IToastNotificationManagerStatics),
                                                              mgr_create_notifier, mgr_create_notifier_id,
                                                              mgr_get_template};
static const IToastNotificationManagerStatics2Vtbl mgr2_vtbl = {
    WRT_INSPECTABLE_ENTRIES(IToastNotificationManagerStatics2), mgr_get_history};
static const IToastNotificationManagerStatics4Vtbl mgr4_vtbl = {
    WRT_INSPECTABLE_ENTRIES(IToastNotificationManagerStatics4), mgr_get_for_user, mgr_configure_mirroring};
static const IToastNotificationManagerStatics5Vtbl mgr5_vtbl = {
    WRT_INSPECTABLE_ENTRIES(IToastNotificationManagerStatics5), mgr_get_default};
static const IToastNotificationManagerForUserVtbl user_vtbl = {WRT_INSPECTABLE_ENTRIES(IToastNotificationManagerForUser),
                                                               user_create_notifier, user_create_notifier_id,
                                                               user_get_history, user_get_user};

static void keep_destroy(WRT_OBJECT *object)
{
    object->Refs = 1;
}

static void free_destroy(WRT_OBJECT *object)
{
    CbFree(object);
}

static const WRT_CLASS user_class = {g_manager_user_class, 1, {&IID_IToastNotificationManagerForUser}, 0, free_destroy};

static HRESULT new_for_user(IToastNotificationManagerForUser **out)
{
    WRT_OBJECT *o = CbAlloc(sizeof(*o));
    *out = nullptr;
    if (!o)
        return E_OUTOFMEMORY;
    memset(o, 0, sizeof(*o));
    const void *vtables[1] = {&user_vtbl};
    WrtInit(o, &user_class, vtables);
    *out = (IToastNotificationManagerForUser *)&o->Ifaces[0];
    return S_OK;
}

static const WRT_CLASS manager_class = {CbToastManagerClass, 4,
                                        {&IID_IToastNotificationManagerStatics, &IID_IToastNotificationManagerStatics2,
                                         &IID_IToastNotificationManagerStatics4, &IID_IToastNotificationManagerStatics5},
                                        0, keep_destroy};
static WRT_OBJECT g_manager;
static INIT_ONCE g_manager_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK make_manager(PINIT_ONCE o, PVOID parameter, PVOID *context)
{
    const void *vtables[4] = {&mgr_vtbl, &mgr2_vtbl, &mgr4_vtbl, &mgr5_vtbl};
    WrtInit(&g_manager, &manager_class, vtables);
    return TRUE;
}

HRESULT CbToastManagerFactory(REFIID riid, void **factory)
{
    /* without a desktop that shows notifications, the class does not exist for programs. */
    if (!host_can_show()) {
        *factory = nullptr;
        return REGDB_E_CLASSNOTREG;
    }
    InitOnceExecuteOnce(&g_manager_once, make_manager, nullptr, nullptr);
    return WrtQuery(&g_manager.Ifaces[0], riid, factory);
}

/* ------------------------------------------------------------------ the factory of toasts */

static HRESULT STDMETHODCALLTYPE factory_create(IToastNotificationFactory *This, IXmlDocument *content,
                                                IToastNotification **value)
{
    TOAST *t;
    *value = nullptr;
    if (!content)
        return E_INVALIDARG;
    HRESULT hr = toast_new(content, &t);
    if (SUCCEEDED(hr))
        *value = (IToastNotification *)&t->Base.Ifaces[0];
    return hr;
}

static const IToastNotificationFactoryVtbl factory_vtbl = {WRT_INSPECTABLE_ENTRIES(IToastNotificationFactory),
                                                           factory_create};
static const WRT_CLASS factory_class = {CbToastClass, 1, {&IID_IToastNotificationFactory}, 0, keep_destroy};
static WRT_OBJECT g_factory;
static INIT_ONCE g_factory_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK make_factory(PINIT_ONCE o, PVOID parameter, PVOID *context)
{
    const void *vtables[1] = {&factory_vtbl};
    WrtInit(&g_factory, &factory_class, vtables);
    return TRUE;
}

HRESULT CbToastFactory(REFIID riid, void **factory)
{
    InitOnceExecuteOnce(&g_factory_once, make_factory, nullptr, nullptr);
    return WrtQuery(&g_factory.Ifaces[0], riid, factory);
}
