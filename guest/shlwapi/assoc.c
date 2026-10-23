/*
 * the QueryAssociations object and the Assoc functions over the classes root, where the user's classes in
 * HKEY_CURRENT_USER\Software\Classes come before the machine's.
 */
#include "shlwapi_private.h"

#define ASSOC_NONE HRESULT_FROM_WIN32(ERROR_NO_ASSOCIATION)
#define TEXT_SIZE 2048

/* one class key opened on both the user and the machine side. */
typedef struct {
    HKEY user, machine;
} CLASS_KEY;

typedef struct {
    IQueryAssociations iface;
    LONG refs;
    ASSOCF flags;
    CLASS_KEY chain[6];         /* the class, the extension, system associations, then defaults. */
    int count;
    WCHAR extension[MAX_PATH];  /* ".txt" when the association began with one. */
    WCHAR progid[MAX_PATH];     /* the class an extension or a user's choice led to. */
    WCHAR application[MAX_PATH];
} ASSOC;

static ASSOC *impl(IQueryAssociations *iface)
{
    return CONTAINING_RECORD(iface, ASSOC, iface);
}

static void close_class(CLASS_KEY *c)
{
    if (c->user)
        RegCloseKey(c->user);
    if (c->machine)
        RegCloseKey(c->machine);
    c->user = c->machine = nullptr;
}

static BOOL open_class(LPCWSTR name, BOOL user_settings, CLASS_KEY *c)
{
    c->user = c->machine = nullptr;
    if (!name || !*name)
        return FALSE;
    if (user_settings) {
        WCHAR path[MAX_PATH + 32];
        if (wcslen(name) < MAX_PATH) {
            wsprintfW(path, L"Software\\Classes\\%s", name);
            if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &c->user))
                c->user = nullptr;
        }
    }
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, name, 0, KEY_READ, &c->machine))
        c->machine = nullptr;
    return c->user || c->machine;
}

static void add_class(ASSOC *a, LPCWSTR name)
{
    if (a->count >= (int)RTL_NUMBER_OF(a->chain))
        return;
    CLASS_KEY c;
    if (open_class(name, !(a->flags & ASSOCF_NOUSERSETTINGS), &c))
        a->chain[a->count++] = c;
}

/* REG_EXPAND_SZ values come back expanded. */
static BOOL read_string(HKEY key, LPCWSTR subkey, LPCWSTR value, LPWSTR out, DWORD size)
{
    if (!key)
        return FALSE;
    HKEY sub = key;
    if (subkey && *subkey && RegOpenKeyExW(key, subkey, 0, KEY_READ, &sub))
        return FALSE;
    WCHAR raw[TEXT_SIZE];
    DWORD bytes = sizeof(raw) - sizeof(WCHAR), type;
    LONG error = RegQueryValueExW(sub, value, nullptr, &type, (BYTE *)raw, &bytes);
    if (sub != key)
        RegCloseKey(sub);
    if (error || (type != REG_SZ && type != REG_EXPAND_SZ))
        return FALSE;
    raw[bytes / sizeof(WCHAR)] = 0;
    if (type == REG_EXPAND_SZ) {
        DWORD n = ExpandEnvironmentStringsW(raw, out, size);
        return n && n <= size;
    }
    lstrcpynW(out, raw, (int)size);
    return TRUE;
}

/* the user's side before the machine's. */
static BOOL class_string(const CLASS_KEY *c, LPCWSTR subkey, LPCWSTR value, LPWSTR out, DWORD size)
{
    return read_string(c->user, subkey, value, out, size) || read_string(c->machine, subkey, value, out, size);
}

static BOOL class_has(const CLASS_KEY *c, LPCWSTR subkey)
{
    HKEY sides[2] = {c->user, c->machine};
    for (int i = 0; i < 2; i++) {
        HKEY sub;
        if (sides[i] && !RegOpenKeyExW(sides[i], subkey, 0, KEY_READ, &sub)) {
            RegCloseKey(sub);
            return TRUE;
        }
    }
    return FALSE;
}

/* the progid the user picked for an extension or a protocol in explorer. */
static BOOL user_choice(LPCWSTR name, BOOL protocol, LPWSTR out, DWORD size)
{
    WCHAR path[MAX_PATH + 96];
    if (wcslen(name) >= MAX_PATH)
        return FALSE;
    if (protocol)
        wsprintfW(path, L"Software\\Microsoft\\Windows\\Shell\\Associations\\UrlAssociations\\%s\\UserChoice", name);
    else
        wsprintfW(path, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\FileExts\\%s\\UserChoice", name);
    return read_string(HKEY_CURRENT_USER, path, L"ProgId", out, size) && *out;
}

static void reset(ASSOC *a)
{
    for (int i = 0; i < a->count; i++)
        close_class(&a->chain[i]);
    a->count = 0;
    a->extension[0] = a->progid[0] = a->application[0] = 0;
}

/* ------------------------------------------------------------------ IQueryAssociations */

static HRESULT STDMETHODCALLTYPE assoc_query(IQueryAssociations *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IQueryAssociations)) {
        *out = iface;
        iface->lpVtbl->AddRef(iface);
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE assoc_add_ref(IQueryAssociations *iface)
{
    return (ULONG)InterlockedIncrement(&impl(iface)->refs);
}

static ULONG STDMETHODCALLTYPE assoc_release(IQueryAssociations *iface)
{
    ASSOC *a = impl(iface);
    LONG refs = InterlockedDecrement(&a->refs);
    if (!refs) {
        reset(a);
        ShlFree(a);
    }
    return (ULONG)refs;
}

/* an extension leads to its progid, the user's choice first. an executable leads to its Applications key,
   anything else is its own progid, protocol or clsid. */
static HRESULT STDMETHODCALLTYPE assoc_init(IQueryAssociations *iface, ASSOCF flags, LPCWSTR name, HKEY progid,
                                            HWND window)
{
    ASSOC *a = impl(iface);
    reset(a);
    a->flags = flags;
    BOOL user = !(flags & ASSOCF_NOUSERSETTINGS);
    if (!name && !progid)
        return E_INVALIDARG;
    if (!name) {
        CLASS_KEY c = {nullptr, nullptr};
        if (RegOpenKeyExW(progid, nullptr, 0, KEY_READ, &c.machine))
            return E_INVALIDARG;
        a->chain[a->count++] = c;
    } else if (flags & ASSOCF_INIT_BYEXENAME) {
        lstrcpynW(a->application, PathFindFileNameW(name), MAX_PATH);
        WCHAR key[MAX_PATH + 16];
        wsprintfW(key, L"Applications\\%s", a->application);
        add_class(a, key);
    } else {
        LPCWSTR ext = name;
        if (*name != L'.' && (wcschr(name, L'\\') || wcschr(name, L'/') || (name[0] && name[1] == L':'))) {
            ext = PathFindExtensionW(name);
            if (!*ext)
                ext = name;
        }
        if (*ext == L'.') {
            lstrcpynW(a->extension, ext, MAX_PATH);
            CLASS_KEY ext_key;
            BOOL has_ext = open_class(ext, user, &ext_key);
            WCHAR id[MAX_PATH] = L"";
            if (!(user && user_choice(ext, FALSE, id, MAX_PATH)) && has_ext)
                class_string(&ext_key, nullptr, nullptr, id, MAX_PATH);
            if (*id) {
                CLASS_KEY c;
                if (open_class(id, user, &c)) {
                    lstrcpynW(a->progid, id, MAX_PATH);
                    a->chain[a->count++] = c;
                }
            }
            if (has_ext)
                a->chain[a->count++] = ext_key;
            WCHAR system[MAX_PATH + 32];
            wsprintfW(system, L"SystemFileAssociations\\%s", ext);
            add_class(a, system);
            WCHAR perceived[64];
            if (has_ext && class_string(&ext_key, nullptr, L"PerceivedType", perceived, RTL_NUMBER_OF(perceived))) {
                wsprintfW(system, L"SystemFileAssociations\\%s", perceived);
                add_class(a, system);
            }
            if (!a->count && !(flags & ASSOCF_INIT_IGNOREUNKNOWN) && !(flags & ASSOCF_INIT_DEFAULTTOSTAR))
                add_class(a, L"Unknown");
        } else {
            WCHAR id[MAX_PATH] = L"";
            if (user && !wcschr(name, L'\\') && user_choice(name, TRUE, id, MAX_PATH)) {
                CLASS_KEY c;
                if (open_class(id, user, &c)) {
                    lstrcpynW(a->progid, id, MAX_PATH);
                    a->chain[a->count++] = c;
                }
            }
            if (*name == L'{') {
                WCHAR key[MAX_PATH + 8];
                if (wcslen(name) < MAX_PATH) {
                    wsprintfW(key, L"CLSID\\%s", name);
                    add_class(a, key);
                }
            } else {
                int before = a->count;
                add_class(a, name);
                if (a->count > before && !*a->progid)
                    lstrcpynW(a->progid, name, MAX_PATH);
            }
        }
    }
    if (flags & ASSOCF_INIT_DEFAULTTOSTAR)
        add_class(a, L"*");
    if (flags & ASSOCF_INIT_DEFAULTTOFOLDER)
        add_class(a, L"Folder");
    return S_OK;
}

/* the verb asked for, else the shell key's default, else open, else the first verb. */
static BOOL find_verb(ASSOC *a, LPCWSTR wanted, LPWSTR verb, int *which)
{
    for (int i = 0; i < a->count; i++) {
        const CLASS_KEY *c = &a->chain[i];
        WCHAR key[MAX_PATH + 16];
        if (wanted && *wanted) {
            if (wcslen(wanted) >= MAX_PATH)
                return FALSE;
            wsprintfW(key, L"shell\\%s", wanted);
            if (class_has(c, key)) {
                lstrcpynW(verb, wanted, MAX_PATH);
                *which = i;
                return TRUE;
            }
            continue;
        }
        WCHAR list[MAX_PATH];
        if (class_string(c, L"shell", nullptr, list, MAX_PATH) && *list) {
            for (LPWSTR next = list, item; next && *next; next = item) {
                item = wcschr(next, L',');
                if (item)
                    *item++ = 0;
                while (*next == L' ')
                    next++;
                wsprintfW(key, L"shell\\%s", next);
                if (*next && class_has(c, key)) {
                    lstrcpynW(verb, next, MAX_PATH);
                    *which = i;
                    return TRUE;
                }
            }
        }
        if (class_has(c, L"shell\\open")) {
            wcscpy(verb, L"open");
            *which = i;
            return TRUE;
        }
        HKEY sides[2] = {c->user, c->machine};
        for (int s = 0; s < 2; s++) {
            HKEY shell;
            if (!sides[s] || RegOpenKeyExW(sides[s], L"shell", 0, KEY_READ, &shell))
                continue;
            DWORD size = MAX_PATH;
            LONG error = RegEnumKeyExW(shell, 0, verb, &size, nullptr, nullptr, nullptr, nullptr);
            RegCloseKey(shell);
            if (!error) {
                *which = i;
                return TRUE;
            }
        }
    }
    return FALSE;
}

/* a value under the verb's key, the default value of subkey when value is NULL. */
static BOOL verb_string(ASSOC *a, LPCWSTR wanted, LPCWSTR subkey, LPCWSTR value, LPWSTR out, DWORD size)
{
    WCHAR verb[MAX_PATH];
    int which;
    if (!find_verb(a, wanted, verb, &which))
        return FALSE;
    WCHAR key[MAX_PATH * 2];
    wsprintfW(key, subkey ? L"shell\\%s\\%s" : L"shell\\%s", verb, subkey);
    return class_string(&a->chain[which], key, value, out, size);
}

static BOOL chain_string(ASSOC *a, LPCWSTR subkey, LPCWSTR value, LPWSTR out, DWORD size)
{
    for (int i = 0; i < a->count; i++)
        if (class_string(&a->chain[i], subkey, value, out, size))
            return TRUE;
    return FALSE;
}

/* the program of a command line: the quoted part, else the longest leading words naming a file, else
   the first word. a bare name is searched for. */
static BOOL command_executable(LPCWSTR command, ASSOCF flags, LPWSTR out, DWORD size)
{
    while (*command == L' ' || *command == L'\t')
        command++;
    WCHAR exe[TEXT_SIZE];
    LPCWSTR rest;
    if (*command == L'"') {
        LPCWSTR close = wcschr(command + 1, L'"');
        SIZE_T n = close ? (SIZE_T)(close - command - 1) : wcslen(command + 1);
        if (n >= TEXT_SIZE)
            return FALSE;
        memcpy(exe, command + 1, n * sizeof(WCHAR));
        exe[n] = 0;
        rest = close ? close + 1 : command + 1 + n;
    } else {
        SIZE_T total = wcslen(command), n = 0;
        for (SIZE_T i = 1; i <= total && i < TEXT_SIZE; i++) {
            if (i < total && command[i] != L' ')
                continue;
            if (!n)
                n = i;
            memcpy(exe, command, i * sizeof(WCHAR));
            exe[i] = 0;
            if (PathFileExistsW(exe) && !PathIsDirectoryW(exe))
                n = i;
        }
        if (!n || n >= TEXT_SIZE)
            return FALSE;
        memcpy(exe, command, n * sizeof(WCHAR));
        exe[n] = 0;
        rest = command + n;
    }
    if (!*exe)
        return FALSE;
    if ((flags & ASSOCF_REMAPRUNDLL) && !lstrcmpiW(PathFindFileNameW(exe), L"rundll32.exe")) {
        while (*rest == L' ')
            rest++;
        SIZE_T n = 0;
        while (rest[n] && rest[n] != L',' && rest[n] != L' ' && n < TEXT_SIZE - 1)
            n++;
        if (n) {
            memcpy(exe, rest, n * sizeof(WCHAR));
            exe[n] = 0;
        }
    }
    if (*exe != L'%' && PathIsFileSpecW(exe)) {
        WCHAR found[MAX_PATH];
        if (SearchPathW(nullptr, exe, L".exe", MAX_PATH, found, nullptr))
            wcscpy(exe, found);
    }
    if ((flags & ASSOCF_VERIFY) && *exe != L'%' && !PathFileExistsW(exe))
        return FALSE;
    lstrcpynW(out, exe, (int)size);
    return TRUE;
}

static BOOL executable(ASSOC *a, ASSOCF flags, LPCWSTR verb, LPWSTR out, DWORD size)
{
    WCHAR command[TEXT_SIZE];
    if (!verb_string(a, verb, L"command", nullptr, command, TEXT_SIZE))
        return FALSE;
    return command_executable(command, flags | a->flags, out, size);
}

/* the description of an executable's version resource. */
static BOOL file_description(LPCWSTR exe, LPWSTR out, DWORD size)
{
    DWORD(WINAPI * info_size)(LPCWSTR, DWORD *) =
        (DWORD(WINAPI *)(LPCWSTR, DWORD *))ShlProc(L"version.dll", "GetFileVersionInfoSizeW");
    BOOL(WINAPI * info)(LPCWSTR, DWORD, DWORD, void *) =
        (BOOL(WINAPI *)(LPCWSTR, DWORD, DWORD, void *))ShlProc(L"version.dll", "GetFileVersionInfoW");
    BOOL(WINAPI * query)(const void *, LPCWSTR, void **, UINT *) =
        (BOOL(WINAPI *)(const void *, LPCWSTR, void **, UINT *))ShlProc(L"version.dll", "VerQueryValueW");
    if (!info_size || !info || !query)
        return FALSE;
    DWORD handle, bytes = info_size(exe, &handle);
    if (!bytes)
        return FALSE;
    void *data = ShlAlloc(bytes);
    if (!data)
        return FALSE;
    BOOL found = FALSE;
    struct {
        WORD language, code_page;
    } *translations;
    UINT length;
    if (info(exe, 0, bytes, data) && query(data, L"\\VarFileInfo\\Translation", (void **)&translations, &length) &&
        length >= sizeof(*translations)) {
        WCHAR key[64];
        LPWSTR text;
        wsprintfW(key, L"\\StringFileInfo\\%04x%04x\\FileDescription", translations->language,
                  translations->code_page);
        if (query(data, key, (void **)&text, &length) && length && *text) {
            lstrcpynW(out, text, (int)size);
            found = TRUE;
        }
    }
    ShlFree(data);
    return found;
}

static BOOL get_string(ASSOC *a, ASSOCF flags, ASSOCSTR str, LPCWSTR extra, LPWSTR text, DWORD size)
{
    switch (str) {
    case ASSOCSTR_COMMAND:
        return verb_string(a, extra, L"command", nullptr, text, size);
    case ASSOCSTR_EXECUTABLE:
        return executable(a, flags, extra, text, size);
    case ASSOCSTR_FRIENDLYDOCNAME: {
        if (a->count) {
            const CLASS_KEY *c = &a->chain[0];
            WCHAR name[TEXT_SIZE];
            if (class_string(c, nullptr, L"FriendlyTypeName", name, TEXT_SIZE) &&
                SUCCEEDED(SHLoadIndirectString(name, text, size, nullptr)) && *text)
                return TRUE;
            if (class_string(c, nullptr, nullptr, text, size) && *text)
                return TRUE;
        }
        if (!*a->extension)
            return FALSE;
        WCHAR upper[MAX_PATH];
        lstrcpynW(upper, a->extension + 1, MAX_PATH);
        CharUpperW(upper);
        wnsprintfW(text, (int)size, L"%s File", upper);
        return TRUE;
    }
    case ASSOCSTR_FRIENDLYAPPNAME: {
        WCHAR exe[MAX_PATH];
        if (*a->application) {
            if (chain_string(a, nullptr, L"FriendlyAppName", text, size))
                return SUCCEEDED(SHLoadIndirectString(text, text, size, nullptr));
            if (!SearchPathW(nullptr, a->application, L".exe", MAX_PATH, exe, nullptr))
                lstrcpynW(exe, a->application, MAX_PATH);
        } else {
            if (verb_string(a, extra, nullptr, L"FriendlyAppName", text, size))
                return SUCCEEDED(SHLoadIndirectString(text, text, size, nullptr));
            if (!executable(a, flags, extra, exe, MAX_PATH))
                return FALSE;
            WCHAR key[MAX_PATH + 16];
            CLASS_KEY app;
            wsprintfW(key, L"Applications\\%s", PathFindFileNameW(exe));
            if (open_class(key, !(a->flags & ASSOCF_NOUSERSETTINGS), &app)) {
                BOOL found = class_string(&app, nullptr, L"FriendlyAppName", text, size);
                close_class(&app);
                if (found)
                    return SUCCEEDED(SHLoadIndirectString(text, text, size, nullptr));
            }
        }
        if (file_description(exe, text, size))
            return TRUE;
        lstrcpynW(text, PathFindFileNameW(exe), (int)size);
        PathRemoveExtensionW(text);
        return TRUE;
    }
    case ASSOCSTR_NOOPEN:
        return chain_string(a, nullptr, L"NoOpen", text, size);
    case ASSOCSTR_SHELLNEWVALUE:
        return FALSE;
    case ASSOCSTR_DDECOMMAND:
        return verb_string(a, extra, L"ddeexec", nullptr, text, size);
    case ASSOCSTR_DDEIFEXEC:
        return verb_string(a, extra, L"ddeexec\\ifexec", nullptr, text, size);
    case ASSOCSTR_DDEAPPLICATION:
        return verb_string(a, extra, L"ddeexec\\application", nullptr, text, size);
    case ASSOCSTR_DDETOPIC:
        return verb_string(a, extra, L"ddeexec\\topic", nullptr, text, size);
    case ASSOCSTR_INFOTIP:
        return chain_string(a, nullptr, L"InfoTip", text, size);
    case ASSOCSTR_QUICKTIP:
        return chain_string(a, nullptr, L"QuickTip", text, size);
    case ASSOCSTR_TILEINFO:
        return chain_string(a, nullptr, L"TileInfo", text, size);
    case ASSOCSTR_CONTENTTYPE: {
        if (!*a->extension)
            return chain_string(a, nullptr, L"Content Type", text, size);
        CLASS_KEY ext;
        if (!open_class(a->extension, !(a->flags & ASSOCF_NOUSERSETTINGS), &ext))
            return FALSE;
        BOOL found = class_string(&ext, nullptr, L"Content Type", text, size);
        close_class(&ext);
        return found;
    }
    case ASSOCSTR_DEFAULTICON:
        return chain_string(a, L"DefaultIcon", nullptr, text, size);
    case ASSOCSTR_SHELLEXTENSION: {
        if (!extra || wcslen(extra) >= MAX_PATH)
            return FALSE;
        WCHAR key[MAX_PATH + 16];
        wsprintfW(key, L"ShellEx\\%s", extra);
        return chain_string(a, key, nullptr, text, size);
    }
    case ASSOCSTR_DROPTARGET:
        return verb_string(a, extra, L"DropTarget", L"CLSID", text, size);
    case ASSOCSTR_DELEGATEEXECUTE:
        return verb_string(a, extra, L"command", L"DelegateExecute", text, size);
    case ASSOCSTR_PROGID:
        if (!*a->progid)
            return FALSE;
        lstrcpynW(text, a->progid, (int)size);
        return TRUE;
    case ASSOCSTR_APPID:
        return chain_string(a, L"Application", L"AppUserModelID", text, size);
    case ASSOCSTR_APPPUBLISHER:
        return chain_string(a, L"Application", L"ApplicationCompany", text, size);
    case ASSOCSTR_APPICONREFERENCE:
        return chain_string(a, L"Application", L"ApplicationIcon", text, size);
    default:
        return FALSE;
    }
}

/* no buffer gives S_FALSE and the size needed. too small a buffer gives E_POINTER and truncated text,
   unless ASSOCF_NOTRUNCATE. */
static HRESULT STDMETHODCALLTYPE assoc_get_string(IQueryAssociations *iface, ASSOCF flags, ASSOCSTR str,
                                                  LPCWSTR extra, LPWSTR out, DWORD *size)
{
    ASSOC *a = impl(iface);
    if (!size || str <= 0 || str >= ASSOCSTR_MAX)
        return E_INVALIDARG;
    WCHAR *text = ShlAlloc(TEXT_SIZE * sizeof(WCHAR));
    if (!text)
        return E_OUTOFMEMORY;
    *text = 0;
    HRESULT hr;
    if (!get_string(a, flags, str, extra, text, TEXT_SIZE)) {
        hr = ASSOC_NONE;
    } else {
        DWORD needed = (DWORD)wcslen(text) + 1;
        if (!out) {
            *size = needed;
            hr = S_FALSE;
        } else if (needed > *size) {
            if (!(flags & ASSOCF_NOTRUNCATE) && *size)
                lstrcpynW(out, text, (int)*size);
            *size = needed;
            hr = E_POINTER;
        } else {
            memcpy(out, text, needed * sizeof(WCHAR));
            *size = needed;
            hr = S_OK;
        }
    }
    ShlFree(text);
    return hr;
}

static HKEY duplicate(const CLASS_KEY *c)
{
    HKEY key = c->user ? c->user : c->machine, copy;
    if (!key || RegOpenKeyExW(key, nullptr, 0, KEY_READ, &copy))
        return nullptr;
    return copy;
}

static HRESULT STDMETHODCALLTYPE assoc_get_key(IQueryAssociations *iface, ASSOCF flags, ASSOCKEY which,
                                               LPCWSTR extra, HKEY *out)
{
    ASSOC *a = impl(iface);
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    switch (which) {
    case ASSOCKEY_SHELLEXECCLASS: {
        WCHAR verb[MAX_PATH];
        int index;
        if (find_verb(a, extra, verb, &index))
            *out = duplicate(&a->chain[index]);
        else if (a->count)
            *out = duplicate(&a->chain[0]);
        break;
    }
    case ASSOCKEY_CLASS:
        if (a->count)
            *out = duplicate(&a->chain[0]);
        break;
    case ASSOCKEY_APP: {
        WCHAR exe[MAX_PATH], key[MAX_PATH + 16];
        LPCWSTR name = a->application;
        if (!*name) {
            if (!executable(a, flags, extra, exe, MAX_PATH))
                break;
            name = PathFindFileNameW(exe);
        }
        wsprintfW(key, L"Applications\\%s", name);
        CLASS_KEY app;
        if (open_class(key, !(a->flags & ASSOCF_NOUSERSETTINGS), &app)) {
            *out = duplicate(&app);
            close_class(&app);
        }
        break;
    }
    case ASSOCKEY_BASECLASS:
        if (a->count > 1)
            *out = duplicate(&a->chain[a->count - 1]);
        break;
    default:
        return E_INVALIDARG;
    }
    return *out ? S_OK : ASSOC_NONE;
}

static HRESULT STDMETHODCALLTYPE assoc_get_data(IQueryAssociations *iface, ASSOCF flags, ASSOCDATA which,
                                                LPCWSTR extra, void *out, DWORD *size)
{
    ASSOC *a = impl(iface);
    LPCWSTR value;
    switch (which) {
    case ASSOCDATA_EDITFLAGS:
        value = L"EditFlags";
        break;
    case ASSOCDATA_VALUE:
        value = extra;
        break;
    case ASSOCDATA_NOACTIVATEHANDLER:
        value = L"NoActivateHandler";
        break;
    case ASSOCDATA_HASPERUSERASSOC:
        for (int i = 0; i < a->count; i++)
            if (a->chain[i].user)
                return S_OK;
        return S_FALSE;
    default:
        return ASSOC_NONE;
    }
    for (int i = 0; i < a->count; i++) {
        HKEY sides[2] = {a->chain[i].user, a->chain[i].machine};
        for (int s = 0; s < 2; s++) {
            if (!sides[s])
                continue;
            DWORD bytes = size ? *size : 0;
            LONG error = RegQueryValueExW(sides[s], value, nullptr, nullptr, out, size ? &bytes : nullptr);
            if (error == ERROR_FILE_NOT_FOUND)
                continue;
            if (which == ASSOCDATA_NOACTIVATEHANDLER)
                return S_OK;
            if (size)
                *size = bytes;
            if (error == ERROR_MORE_DATA)
                return E_POINTER;
            return error ? HRESULT_FROM_WIN32(error) : S_OK;
        }
    }
    return ASSOC_NONE;
}

static HRESULT STDMETHODCALLTYPE assoc_get_enum(IQueryAssociations *iface, ASSOCF flags, ASSOCENUM which,
                                                LPCWSTR extra, REFIID riid, void **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}

static const IQueryAssociationsVtbl assoc_vtbl = {assoc_query,   assoc_add_ref,    assoc_release,
                                                  assoc_init,    assoc_get_string, assoc_get_key,
                                                  assoc_get_data, assoc_get_enum};

HRESULT ShlAssocCreateInstance(REFIID riid, void **out);
HRESULT ShlAssocCreateInstance(REFIID riid, void **out)
{
    ASSOC *a = ShlAlloc(sizeof(*a));
    if (!a)
        return E_OUTOFMEMORY;
    memset(a, 0, sizeof(*a));
    a->iface.lpVtbl = &assoc_vtbl;
    a->refs = 1;
    HRESULT hr = assoc_query(&a->iface, riid, out);
    assoc_release(&a->iface);
    return hr;
}

/* ------------------------------------------------------------------ Assoc functions */

/* the class id must be CLSID_QueryAssociations, though the interface id passes too. */
HRESULT WINAPI AssocCreate(CLSID clsid, REFIID riid, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!IsEqualCLSID(&clsid, &CLSID_QueryAssociations) && !IsEqualIID(&clsid, &IID_IQueryAssociations))
        return E_INVALIDARG;
    return ShlAssocCreateInstance(riid, out);
}

#define INIT_FLAGS                                                                                                     \
    (ASSOCF_INIT_NOREMAPCLSID | ASSOCF_INIT_BYEXENAME | ASSOCF_INIT_DEFAULTTOSTAR | ASSOCF_INIT_DEFAULTTOFOLDER |      \
     ASSOCF_INIT_IGNOREUNKNOWN | ASSOCF_NOUSERSETTINGS)

static HRESULT open_assoc(ASSOCF flags, LPCWSTR name, HKEY key, IQueryAssociations **out)
{
    HRESULT hr = ShlAssocCreateInstance(&IID_IQueryAssociations, (void **)out);
    if (FAILED(hr))
        return hr;
    hr = (*out)->lpVtbl->Init(*out, flags & INIT_FLAGS, name, key, nullptr);
    if (FAILED(hr)) {
        (*out)->lpVtbl->Release(*out);
        *out = nullptr;
    }
    return hr;
}

HRESULT WINAPI AssocQueryStringW(ASSOCF flags, ASSOCSTR str, LPCWSTR name, LPCWSTR extra, LPWSTR out, DWORD *size)
{
    if (!size || !name)
        return E_INVALIDARG;
    IQueryAssociations *assoc;
    HRESULT hr = open_assoc(flags, name, nullptr, &assoc);
    if (SUCCEEDED(hr)) {
        hr = assoc->lpVtbl->GetString(assoc, flags, str, extra, out, size);
        assoc->lpVtbl->Release(assoc);
    }
    return hr;
}

HRESULT WINAPI AssocQueryStringByKeyW(ASSOCF flags, ASSOCSTR str, HKEY key, LPCWSTR extra, LPWSTR out, DWORD *size)
{
    if (!size || !key)
        return E_INVALIDARG;
    IQueryAssociations *assoc;
    HRESULT hr = open_assoc(flags, nullptr, key, &assoc);
    if (SUCCEEDED(hr)) {
        hr = assoc->lpVtbl->GetString(assoc, flags, str, extra, out, size);
        assoc->lpVtbl->Release(assoc);
    }
    return hr;
}

/* the ANSI forms query the wide string, and report twice its length when given no buffer. */
static HRESULT string_ansi(HRESULT hr, LPCWSTR wide, DWORD wide_size, LPSTR out, DWORD *size)
{
    if (hr == S_FALSE) {
        *size = wide_size * 2;
        return S_FALSE;
    }
    if (FAILED(hr) && hr != E_POINTER)
        return hr;
    int needed = WideCharToMultiByte(CP_ACP, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (hr == E_POINTER || (DWORD)needed > *size) {
        if (*size)
            SHUnicodeToAnsiCP(CP_ACP, wide, out, (int)*size);
        *size = (DWORD)needed;
        return E_POINTER;
    }
    WideCharToMultiByte(CP_ACP, 0, wide, -1, out, (int)*size, nullptr, nullptr);
    *size = (DWORD)needed;
    return S_OK;
}

HRESULT WINAPI AssocQueryStringA(ASSOCF flags, ASSOCSTR str, LPCSTR name, LPCSTR extra, LPSTR out, DWORD *size)
{
    if (!size || !name)
        return E_INVALIDARG;
    LPWSTR n = ShlWide(name), e = ShlWide(extra);
    WCHAR *wide = ShlAlloc(TEXT_SIZE * sizeof(WCHAR));
    HRESULT hr = E_OUTOFMEMORY;
    if (n && (e || !extra) && wide) {
        *wide = 0;
        DWORD wide_size = TEXT_SIZE;
        hr = AssocQueryStringW(flags, str, n, e, out ? wide : nullptr, &wide_size);
        hr = string_ansi(hr, wide, wide_size, out, size);
    }
    ShlFree(n);
    ShlFree(e);
    ShlFree(wide);
    return hr;
}

HRESULT WINAPI AssocQueryStringByKeyA(ASSOCF flags, ASSOCSTR str, HKEY key, LPCSTR extra, LPSTR out, DWORD *size)
{
    if (!size || !key)
        return E_INVALIDARG;
    LPWSTR e = ShlWide(extra);
    WCHAR *wide = ShlAlloc(TEXT_SIZE * sizeof(WCHAR));
    HRESULT hr = E_OUTOFMEMORY;
    if ((e || !extra) && wide) {
        *wide = 0;
        DWORD wide_size = TEXT_SIZE;
        hr = AssocQueryStringByKeyW(flags, str, key, e, out ? wide : nullptr, &wide_size);
        hr = string_ansi(hr, wide, wide_size, out, size);
    }
    ShlFree(e);
    ShlFree(wide);
    return hr;
}

HRESULT WINAPI AssocQueryKeyW(ASSOCF flags, ASSOCKEY which, LPCWSTR name, LPCWSTR extra, HKEY *out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!name)
        return E_INVALIDARG;
    IQueryAssociations *assoc;
    HRESULT hr = open_assoc(flags, name, nullptr, &assoc);
    if (SUCCEEDED(hr)) {
        hr = assoc->lpVtbl->GetKey(assoc, flags, which, extra, out);
        assoc->lpVtbl->Release(assoc);
    }
    return hr;
}

HRESULT WINAPI AssocQueryKeyA(ASSOCF flags, ASSOCKEY which, LPCSTR name, LPCSTR extra, HKEY *out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    LPWSTR n = ShlWide(name), e = ShlWide(extra);
    HRESULT hr = (n || !name) && (e || !extra) ? AssocQueryKeyW(flags, which, n, e, out) : E_OUTOFMEMORY;
    ShlFree(n);
    ShlFree(e);
    return hr;
}

/* ------------------------------------------------------------------ perceived types */

static const struct {
    PCWSTR extension;
    PERCEIVED type;
    PERCEIVEDFLAG flags;
} known_types[] = {
    {L".txt", PERCEIVED_TYPE_TEXT, PERCEIVEDFLAG_HARDCODED},
    {L".bmp", PERCEIVED_TYPE_IMAGE, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_GDIPLUS},
    {L".dib", PERCEIVED_TYPE_IMAGE, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_GDIPLUS},
    {L".emf", PERCEIVED_TYPE_IMAGE, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_GDIPLUS},
    {L".gif", PERCEIVED_TYPE_IMAGE, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_GDIPLUS},
    {L".ico", PERCEIVED_TYPE_IMAGE, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_GDIPLUS},
    {L".jfif", PERCEIVED_TYPE_IMAGE, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_GDIPLUS},
    {L".jpe", PERCEIVED_TYPE_IMAGE, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_GDIPLUS},
    {L".jpeg", PERCEIVED_TYPE_IMAGE, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_GDIPLUS},
    {L".jpg", PERCEIVED_TYPE_IMAGE, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_GDIPLUS},
    {L".png", PERCEIVED_TYPE_IMAGE, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_GDIPLUS},
    {L".rle", PERCEIVED_TYPE_IMAGE, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_GDIPLUS},
    {L".tif", PERCEIVED_TYPE_IMAGE, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_GDIPLUS},
    {L".tiff", PERCEIVED_TYPE_IMAGE, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_GDIPLUS},
    {L".wmf", PERCEIVED_TYPE_IMAGE, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_GDIPLUS},
    {L".aif", PERCEIVED_TYPE_AUDIO, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_WMSDK},
    {L".aiff", PERCEIVED_TYPE_AUDIO, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_WMSDK},
    {L".au", PERCEIVED_TYPE_AUDIO, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_WMSDK},
    {L".mid", PERCEIVED_TYPE_AUDIO, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_WMSDK},
    {L".midi", PERCEIVED_TYPE_AUDIO, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_WMSDK},
    {L".mp3", PERCEIVED_TYPE_AUDIO, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_WMSDK},
    {L".rmi", PERCEIVED_TYPE_AUDIO, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_WMSDK},
    {L".snd", PERCEIVED_TYPE_AUDIO, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_WMSDK},
    {L".wav", PERCEIVED_TYPE_AUDIO, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_WMSDK},
    {L".wma", PERCEIVED_TYPE_AUDIO, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_WMSDK},
    {L".asf", PERCEIVED_TYPE_VIDEO, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_WMSDK},
    {L".avi", PERCEIVED_TYPE_VIDEO, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_WMSDK},
    {L".m1v", PERCEIVED_TYPE_VIDEO, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_WMSDK},
    {L".mp2", PERCEIVED_TYPE_VIDEO, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_WMSDK},
    {L".mpa", PERCEIVED_TYPE_VIDEO, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_WMSDK},
    {L".mpe", PERCEIVED_TYPE_VIDEO, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_WMSDK},
    {L".mpeg", PERCEIVED_TYPE_VIDEO, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_WMSDK},
    {L".mpg", PERCEIVED_TYPE_VIDEO, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_WMSDK},
    {L".mp4", PERCEIVED_TYPE_VIDEO, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_WMSDK},
    {L".wmv", PERCEIVED_TYPE_VIDEO, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_WMSDK},
    {L".cab", PERCEIVED_TYPE_COMPRESSED, PERCEIVEDFLAG_HARDCODED},
    {L".zip", PERCEIVED_TYPE_COMPRESSED, PERCEIVEDFLAG_HARDCODED | PERCEIVEDFLAG_ZIPFOLDER},
    {L".bat", PERCEIVED_TYPE_APPLICATION, PERCEIVEDFLAG_HARDCODED},
    {L".cmd", PERCEIVED_TYPE_APPLICATION, PERCEIVEDFLAG_HARDCODED},
    {L".com", PERCEIVED_TYPE_APPLICATION, PERCEIVEDFLAG_HARDCODED},
    {L".exe", PERCEIVED_TYPE_APPLICATION, PERCEIVEDFLAG_HARDCODED},
    {L".pif", PERCEIVED_TYPE_APPLICATION, PERCEIVEDFLAG_HARDCODED},
    {L".scr", PERCEIVED_TYPE_APPLICATION, PERCEIVEDFLAG_HARDCODED},
};

static const PCWSTR perceived_names[] = {L"unknown",    L"text",     L"image",  L"audio",
                                         L"video",      L"compressed", L"document", L"system",
                                         L"application", L"gamemedia", L"contacts"};

/* the registry's PerceivedType first, then the types windows has built in. */
HRESULT WINAPI AssocGetPerceivedType(LPCWSTR extension, PERCEIVED *type, PERCEIVEDFLAG *flags, LPWSTR *name)
{
    if (!extension || !type || !flags)
        return E_INVALIDARG;
    *type = PERCEIVED_TYPE_UNSPECIFIED;
    *flags = PERCEIVEDFLAG_UNDEFINED;
    if (name)
        *name = nullptr;
    PERCEIVED found = PERCEIVED_TYPE_UNSPECIFIED;
    PERCEIVEDFLAG found_flags = 0;
    WCHAR value[64];
    CLASS_KEY ext;
    BOOL registered = FALSE;
    if (*extension == L'.' && open_class(extension, TRUE, &ext)) {
        registered = class_string(&ext, nullptr, L"PerceivedType", value, RTL_NUMBER_OF(value));
        close_class(&ext);
    }
    if (registered) {
        found = PERCEIVED_TYPE_CUSTOM;
        found_flags = PERCEIVEDFLAG_SOFTCODED;
        for (ULONG i = 1; i < RTL_NUMBER_OF(perceived_names); i++)
            if (!lstrcmpiW(value, perceived_names[i]))
                found = (PERCEIVED)i;
    } else {
        for (ULONG i = 0; i < RTL_NUMBER_OF(known_types); i++) {
            if (!lstrcmpiW(extension, known_types[i].extension)) {
                found = known_types[i].type;
                found_flags = known_types[i].flags;
                wcscpy(value, perceived_names[found]);
                break;
            }
        }
    }
    if (found == PERCEIVED_TYPE_UNSPECIFIED)
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    *type = found;
    *flags = found_flags;
    if (name) {
        SIZE_T bytes = (wcslen(value) + 1) * sizeof(WCHAR);
        *name = ShlTaskAlloc(bytes);
        if (!*name)
            return E_OUTOFMEMORY;
        memcpy(*name, value, bytes);
    }
    return S_OK;
}

/* mime has no zones to ask, so there is no policy. */
DWORD WINAPI AssocGetUrlAction(LPCWSTR extension);
DWORD WINAPI AssocGetUrlAction(LPCWSTR extension)
{
    return 0;
}

/* extensions that run code, and any whose EditFlags say FTA_AlwaysUnsafe. */
BOOL WINAPI AssocIsDangerous(LPCWSTR name)
{
    static const PCWSTR dangerous[] = {
        L".ade", L".adp", L".app", L".asp", L".bas", L".bat", L".cer", L".chm", L".cmd", L".cnt", L".com",
        L".cpl", L".crt", L".csh", L".der", L".exe", L".fxp", L".gadget", L".grp", L".hlp", L".hpj", L".hta",
        L".inf", L".ins", L".isp", L".its", L".js", L".jse", L".ksh", L".lnk", L".mad", L".maf", L".mag",
        L".mam", L".maq", L".mar", L".mas", L".mat", L".mau", L".mav", L".maw", L".mcf", L".mda", L".mdb",
        L".mde", L".mdt", L".mdw", L".mdz", L".msc", L".msh", L".msh1", L".msh2", L".mshxml", L".msh1xml",
        L".msh2xml", L".msi", L".msp", L".mst", L".ops", L".pcd", L".pif", L".plg", L".prf", L".prg",
        L".ps1", L".ps1xml", L".ps2", L".ps2xml", L".psc1", L".psc2", L".pst", L".reg", L".scf", L".scr",
        L".sct", L".shb", L".shs", L".tmp", L".url", L".vb", L".vbe", L".vbp", L".vbs", L".vsmacros",
        L".vsw", L".ws", L".wsc", L".wsf", L".wsh", L".xnk"};
    if (!name)
        return FALSE;
    LPCWSTR extension = *name == L'.' ? name : PathFindExtensionW(name);
    for (ULONG i = 0; i < RTL_NUMBER_OF(dangerous); i++)
        if (!lstrcmpiW(extension, dangerous[i]))
            return TRUE;
    DWORD flags = 0, size = sizeof(flags);
    IQueryAssociations *assoc;
    if (SUCCEEDED(open_assoc(0, name, nullptr, &assoc))) {
        HRESULT hr = assoc->lpVtbl->GetData(assoc, 0, ASSOCDATA_EDITFLAGS, nullptr, &flags, &size);
        assoc->lpVtbl->Release(assoc);
        if (SUCCEEDED(hr) && (flags & 0x00020000))
            return TRUE;
    }
    return FALSE;
}

/* looks the content type up in the MIME database. */
BOOL WINAPI MIME_GetExtensionW(LPCWSTR type, LPWSTR extension, UINT size);
BOOL WINAPI MIME_GetExtensionW(LPCWSTR type, LPWSTR extension, UINT size)
{
    if (!type || !extension || size < 2 || wcslen(type) >= MAX_PATH)
        return FALSE;
    WCHAR key[MAX_PATH + 48];
    wsprintfW(key, L"MIME\\Database\\Content Type\\%s", type);
    CLASS_KEY c;
    if (!open_class(key, TRUE, &c))
        return FALSE;
    BOOL found = class_string(&c, nullptr, L"Extension", extension, size);
    close_class(&c);
    return found;
}

/* falls back to the file name when there is no version description. */
BOOL WINAPI SHGetFileDescriptionW(LPCWSTR file, LPCWSTR version, LPCWSTR language, LPWSTR out, UINT *size);
BOOL WINAPI SHGetFileDescriptionW(LPCWSTR file, LPCWSTR version, LPCWSTR language, LPWSTR out, UINT *size)
{
    if (!file || !out || !size || !*size)
        return FALSE;
    if (!file_description(file, out, *size)) {
        lstrcpynW(out, PathFindFileNameW(file), (int)*size);
        PathRemoveExtensionW(out);
    }
    *size = (UINT)wcslen(out);
    return TRUE;
}

BOOL WINAPI SHGetFileDescriptionA(LPCSTR file, LPCSTR version, LPCSTR language, LPSTR out, UINT *size);
BOOL WINAPI SHGetFileDescriptionA(LPCSTR file, LPCSTR version, LPCSTR language, LPSTR out, UINT *size)
{
    if (!file || !out || !size || !*size)
        return FALSE;
    LPWSTR w = ShlWide(file);
    WCHAR text[MAX_PATH];
    UINT n = MAX_PATH;
    BOOL ok = w && SHGetFileDescriptionW(w, nullptr, nullptr, text, &n);
    if (ok) {
        SHUnicodeToAnsiCP(CP_ACP, text, out, (int)*size);
        *size = (UINT)strlen(out);
    }
    ShlFree(w);
    return ok;
}
