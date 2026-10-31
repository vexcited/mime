/*
 * making associations: copying verbs between class keys, writing a class with its shell verbs, registering an
 * application from the class of a file type, and running the command a registry key keeps.
 */
#include "shlwapi_dynamic.h"
#include <mime/win32/shellapi.h>

#define SHELL_VERB_EXPAND 0x200
#define SHELL_VERB_SUBSTITUTE 0x400
#define ASSOCMAKE_VERIFY 0x40

/* what the caller gives to describe a class. */
typedef struct {
    DWORD size;
    LPCWSTR progid;
    LPCWSTR friendly_name;
    LPCWSTR default_icon;
    const void *shell;
    LPCWSTR extensions;
} ASSOCPROGID;

typedef struct {
    LPCWSTR verb;
    LPCWSTR title;
    LPCWSTR reserved;
    LPCWSTR application;
    LPCWSTR parameters;
    LPCWSTR more;
} ASSOCSHELLVERB;

typedef struct {
    const ASSOCSHELLVERB *verbs;
    DWORD count;
    DWORD default_verb;
} ASSOCSHELL;

/* the application and the verb filter shared by the copies that make an application key. */
typedef struct {
    IQueryAssociations *query;
    DWORD flags;
    LPCWSTR executable;
    BOOL per_user;
} EXECUTABLE_FILTER;

/* the value a key has by default, read from the subkey of a root. */
static LSTATUS query_default(HKEY root, LPCWSTR subkey, WCHAR *out, LONG *size)
{
    HKEY key = root;
    DWORD bytes = (DWORD)*size, type = 0;
    LSTATUS status = ERROR_SUCCESS;
    if (subkey && *subkey)
        status = RegOpenKeyExW(root, subkey, 0, KEY_QUERY_VALUE, &key);
    if (status == ERROR_SUCCESS) {
        status = RegQueryValueExW(key, nullptr, nullptr, &type, (BYTE *)out, &bytes);
        if (status == ERROR_SUCCESS && type != REG_SZ && type != REG_EXPAND_SZ)
            status = ERROR_FILE_NOT_FOUND;
        *size = (LONG)bytes;
        if (key != root)
            RegCloseKey(key);
    }
    return status;
}

static HRESULT status_result(LSTATUS status)
{
    return status > 0 ? (HRESULT)((status & 0xffff) | 0x80070000) : (HRESULT)status;
}

static HRESULT open_key(HKEY parent, LPCWSTR name, REGSAM access, HKEY *key, BOOL create)
{
    *key = nullptr;
    if (!parent)
        return HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
    LSTATUS status = create ? RegCreateKeyExW(parent, name, 0, nullptr, 0, access, nullptr, key, nullptr)
                            : RegOpenKeyExW(parent, name, 0, access, key);
    return status_result(status);
}

static HRESULT copy_verb(HKEY source, HKEY target, LPCWSTR verb)
{
    HRESULT hr = S_OK;
    WCHAR path[MAX_PATH];
    HKEY copy = nullptr;
    DWORD disposition = 0;
    wnsprintfW(path, MAX_PATH, L"shell\\%s", verb);
    RegCreateKeyExW(target, path, 0, nullptr, 0, 0x20006, nullptr, &copy, &disposition);
    if (copy) {
        if (disposition == REG_CREATED_NEW_KEY && SHCopyKeyW(source, verb, copy, 0) != ERROR_SUCCESS)
            hr = E_UNEXPECTED;
        RegCloseKey(copy);
    }
    return hr;
}

static HRESULT copy_verbs(HKEY source, HKEY target, BOOL (*filter)(LPCWSTR verb, void *context), void *context)
{
    HKEY shell;
    HRESULT result = E_INVALIDARG;
    if (FAILED(open_key(source, L"shell", 0x20019, &shell, FALSE)))
        return result;
    WCHAR name[MAX_PATH];
    DWORD size = MAX_PATH;
    for (DWORD i = 0; RegEnumKeyExW(shell, i, name, &size, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
         i++, size = MAX_PATH)
        if (!filter || filter(name, context))
            result = copy_verb(shell, target, name);
    size = sizeof(name);
    if (SHGetValueW(shell, nullptr, nullptr, nullptr, name, &size) == ERROR_SUCCESS)
        SHSetValueW(target, L"shell", nullptr, REG_SZ, name, size);
    RegCloseKey(shell);
    return result;
}

HRESULT WINAPI AssocCopyVerbs(HKEY source, HKEY target);
HRESULT WINAPI AssocCopyVerbs(HKEY source, HKEY target)
{
    return copy_verbs(source, target, nullptr, nullptr);
}

/* ------------------------------------------------------------------ the command of a verb */

static BOOL substitute(WCHAR *command, size_t size, LPCWSTR variable)
{
    WCHAR value[MAX_PATH];
    DWORD length = GetEnvironmentVariableW(variable, value, MAX_PATH);
    if (length < 1 || length > MAX_PATH || StrCmpNIW(command, value, length))
        return FALSE;
    WCHAR rest[2084];
    lstrcpynW(rest, command + length, ARRAYSIZE(rest));
    wnsprintfW(command, (int)size, L"%%%s%%%s", variable, rest);
    return TRUE;
}

static HRESULT make_command(DWORD *flags, LPCWSTR executable, LPCWSTR parameters, WCHAR *command, size_t size)
{
    static const LPCWSTR variables[] = {L"USERPROFILE", L"ProgramFiles", L"SystemRoot", L"SystemDrive", L"windir"};
    if (!parameters || !*parameters)
        parameters = L"\"%1\"";
    lstrcpynW(command, executable, (int)size);
    int length = lstrlenW(command);
    BOOL quote = !(command[0] == L'"' && length && command[length - 1] == L'"') && PathIsLFNFileSpecW(command);
    if (*flags & SHELL_VERB_SUBSTITUTE)
        for (size_t i = 0; i < ARRAYSIZE(variables); i++)
            if (substitute(command, size, variables[i])) {
                *flags |= SHELL_VERB_EXPAND;
                break;
            }
    StrTrimW(command, L" \t");
    if (quote)
        PathQuoteSpacesW(command);
    length = lstrlenW(command);
    if ((size_t)length + 2 + (size_t)lstrlenW(parameters) > size)
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    command[length] = L' ';
    lstrcpyW(command + length + 1, parameters);
    return S_OK;
}

static HRESULT make_verb_command(DWORD flags, HKEY verb, LPCWSTR executable, LPCWSTR parameters)
{
    WCHAR command[2084];
    HRESULT hr = make_command(&flags, executable, parameters, command, ARRAYSIZE(command));
    if (SUCCEEDED(hr))
        hr = status_result(SHSetValueW(verb, L"command", nullptr, (flags & SHELL_VERB_EXPAND) ? REG_EXPAND_SZ : REG_SZ,
                                       command, (lstrlenW(command) + 1) * sizeof(WCHAR)));
    return hr;
}

HRESULT WINAPI AssocMakeShell(DWORD flags, HKEY key, LPCWSTR executable, const ASSOCSHELL *shell);
HRESULT WINAPI AssocMakeShell(DWORD flags, HKEY key, LPCWSTR executable, const ASSOCSHELL *shell)
{
    HRESULT hr = E_INVALIDARG;
    if (!key || !executable || !shell)
        return hr;
    for (DWORD i = 0; i < shell->count; i++) {
        const ASSOCSHELLVERB *verb = &shell->verbs[i];
        WCHAR path[MAX_PATH];
        HKEY verb_key;
        if (!verb->verb)
            continue;
        wnsprintfW(path, MAX_PATH, L"%s\\%s", L"shell", verb->verb);
        if (i == shell->default_verb)
            SHSetValueW(key, L"shell", nullptr, REG_SZ, verb->verb, (lstrlenW(verb->verb) + 1) * sizeof(WCHAR));
        if (SUCCEEDED(open_key(key, path, 0x3001f, &verb_key, FALSE))) {
            SHDeleteKeyW(verb_key, nullptr);
            RegCloseKey(verb_key);
        }
        if (SUCCEEDED(open_key(key, path, 0x20006, &verb_key, TRUE))) {
            if (verb->title)
                SHSetValueW(verb_key, nullptr, nullptr, REG_SZ, verb->title, (lstrlenW(verb->title) + 1) * sizeof(WCHAR));
            hr = make_verb_command(flags, verb_key, verb->application ? verb->application : executable,
                                   verb->parameters);
            RegCloseKey(verb_key);
        }
    }
    return hr;
}

HRESULT WINAPI AssocMakeProgid(DWORD flags, LPCWSTR executable, const ASSOCPROGID *info, HKEY *progid_key);
HRESULT WINAPI AssocMakeProgid(DWORD flags, LPCWSTR executable, const ASSOCPROGID *info, HKEY *progid_key)
{
    HRESULT hr = E_INVALIDARG;
    if (!executable || !info || info->size < sizeof(ASSOCPROGID) || !info->progid || !*info->progid)
        return hr;
    if ((flags & ASSOCMAKE_VERIFY) && !PathFileExistsW(executable))
        return hr;
    HKEY classes = nullptr;
    RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\classes", 0, nullptr, 0, 0x2001f, nullptr, &classes, nullptr);
    if (!classes)
        return hr;
    HKEY progid = nullptr;
    hr = open_key(classes, info->progid, 0x20006, &progid, TRUE);
    if (SUCCEEDED(hr)) {
        if (info->friendly_name)
            SHSetValueW(progid, nullptr, nullptr, REG_SZ, info->friendly_name,
                        (lstrlenW(info->friendly_name) + 1) * sizeof(WCHAR));
        if (info->default_icon)
            SHSetValueW(progid, L"DefaultIcon", nullptr, REG_SZ, info->default_icon,
                        (lstrlenW(info->default_icon) + 1) * sizeof(WCHAR));
        BOOL failed = FALSE;
        if (info->shell) {
            hr = AssocMakeShell(flags, progid, executable, info->shell);
            failed = FAILED(hr);
        }
        if (!failed) {
            LSTATUS status = ERROR_SUCCESS;
            for (LPCWSTR extension = info->extensions; extension && *extension && !status;
                 extension += lstrlenW(extension) + 1)
                status = SHSetValueW(classes, extension, nullptr, REG_SZ, info->progid,
                                     (lstrlenW(info->progid) + 1) * sizeof(WCHAR));
            if (progid_key) {
                *progid_key = progid;
                progid = nullptr;
            }
        }
        if (progid)
            RegCloseKey(progid);
    }
    RegCloseKey(classes);
    return hr;
}

/* ------------------------------------------------------------------ applications from a file type */

static BOOL is_per_user_install(IQueryAssociations *query, DWORD flags, LPCWSTR verb)
{
    WCHAR product[MAX_PATH], assignment[3];
    DWORD size = 520, count = 3;
    UINT(WINAPI * info)(LPCWSTR, LPCWSTR, LPWSTR, DWORD *) =
        (UINT(WINAPI *)(LPCWSTR, LPCWSTR, LPWSTR, DWORD *))ShlProc(L"msi.dll", "MsiGetProductInfoW");
    if (FAILED(query->lpVtbl->GetData(query, flags, ASSOCDATA_MSIDESCRIPTOR, verb, product, &size)) || !info)
        return FALSE;
    if (info(product, L"AssignmentType", assignment, &count) != ERROR_SUCCESS)
        return FALSE;
    return !StrCmpW(assignment, L"0");
}

static BOOL allow_executable_verb(LPCWSTR verb, void *context)
{
    EXECUTABLE_FILTER *filter = context;
    WCHAR path[MAX_PATH];
    DWORD size = MAX_PATH;
    if (SUCCEEDED(filter->query->lpVtbl->GetString(filter->query, filter->flags, ASSOCSTR_EXECUTABLE, verb, path,
                                                    &size)) &&
        !StrCmpIW(PathFindFileNameW(path), filter->executable))
        return is_per_user_install(filter->query, filter->flags, verb) ? filter->per_user : TRUE;
    return FALSE;
}

static HRESULT create_application_key(LPCWSTR name, BOOL per_user, HKEY *key)
{
    WCHAR path[MAX_PATH];
    wnsprintfW(path, MAX_PATH, L"Software\\Classes\\Applications\\%s", name);
    if (!*PathFindExtensionW(name))
        lstrcatW(path, L".exe");
    HRESULT hr = open_key(per_user ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE, path, 0x20006, key, TRUE);
    if (hr == HRESULT_FROM_WIN32(1021) && per_user) {
        SHDeleteKeyW(HKEY_CURRENT_USER, L"Software\\Classes\\Applications");
        return open_key(HKEY_CURRENT_USER, path, 0x20006, key, TRUE);
    }
    return hr;
}

HRESULT WINAPI AssocMakeApplicationByKeyW(DWORD flags, HKEY class_key, LPCWSTR verb);
HRESULT WINAPI AssocMakeApplicationByKeyW(DWORD flags, HKEY class_key, LPCWSTR verb)
{
    DWORD query_flags = (flags & ASSOCMAKE_VERIFY) ? ASSOCF_VERIFY : 0;
    HRESULT hr = HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND);
    IQueryAssociations *query = nullptr;
    AssocCreate(CLSID_QueryAssociations, &IID_IQueryAssociations, (void **)&query);
    if (!query)
        return E_OUTOFMEMORY;
    if (SUCCEEDED(query->lpVtbl->Init(query, 0, nullptr, class_key, nullptr))) {
        WCHAR executable[MAX_PATH];
        DWORD size = MAX_PATH;
        if (SUCCEEDED(query->lpVtbl->GetString(query, query_flags, ASSOCSTR_EXECUTABLE, verb, executable, &size)) &&
            StrCmpW(executable, L"%1")) {
            LPCWSTR name = PathFindFileNameW(executable);
            BOOL per_user = is_per_user_install(query, query_flags, verb);
            HKEY application = nullptr;
            DWORD length = 0;
            hr = AssocQueryStringW(query_flags | ASSOCF_INIT_BYEXENAME, ASSOCSTR_COMMAND, name, verb, nullptr, &length);
            if (FAILED(hr) && SUCCEEDED(create_application_key(name, per_user, &application))) {
                EXECUTABLE_FILTER filter = {query, query_flags, name, per_user};
                if (verb) {
                    if (allow_executable_verb(verb, &filter)) {
                        HKEY shell;
                        if (FAILED(open_key(class_key, L"shell", 0x20019, &shell, FALSE))) {
                            hr = E_UNEXPECTED;
                        } else {
                            hr = copy_verb(shell, application, verb);
                            RegCloseKey(shell);
                        }
                    }
                } else {
                    hr = copy_verbs(class_key, application, allow_executable_verb, &filter);
                }
                RegCloseKey(application);
            }
            if ((flags & ASSOCMAKE_VERIFY) && SUCCEEDED(hr))
                AssocQueryStringW(query_flags | ASSOCF_INIT_BYEXENAME, ASSOCSTR_FRIENDLYAPPNAME, name, nullptr,
                                  nullptr, &length);
        }
        query->lpVtbl->Release(query);
    }
    return hr;
}

HRESULT WINAPI AssocMakeApplicationByKeyA(DWORD flags, HKEY class_key, LPCSTR verb);
HRESULT WINAPI AssocMakeApplicationByKeyA(DWORD flags, HKEY class_key, LPCSTR verb)
{
    if (!verb)
        return AssocMakeApplicationByKeyW(flags, class_key, nullptr);
    LPWSTR wide = ShlWide(verb);
    if (!wide)
        return E_OUTOFMEMORY;
    HRESULT hr = AssocMakeApplicationByKeyW(flags, class_key, wide);
    ShlFree(wide);
    return hr;
}

/* ------------------------------------------------------------------ running a command of the registry */

/* the command at a path, following a value that points to a key of another hive. */
static BOOL registry_command(HKEY root, LPCWSTR path, LPCWSTR value, WCHAR *out, DWORD size)
{
    WCHAR key[1024];
    DWORD bytes = size * sizeof(WCHAR);
    lstrcpynW(key, path, ARRAYSIZE(key));
    int length = lstrlenW(key);
    *out = 0;
    if (length && key[length - 1] == L'\\') {
        if (!value)
            key[length - 1] = 0;
    } else if (!value || *value) {
        if (!value && !PathFindFileNameW(key))
            return FALSE;
        PathRemoveFileSpecW(key);
        SHRegGetValueW(root, key, nullptr, SRRF_RT_REG_SZ | SRRF_RT_REG_EXPAND_SZ, nullptr, out, &bytes);
        goto resolved;
    }
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, out, &bytes) != ERROR_SUCCESS)
        *out = 0;
resolved:
    if (!*out)
        return FALSE;
    static const struct {
        LPCWSTR prefix;
        HKEY hive;
    } hives[] = {{L"HKCU:", HKEY_CURRENT_USER}, {L"HKLM:", HKEY_LOCAL_MACHINE}, {L"HKCR:", HKEY_CLASSES_ROOT}};
    for (size_t i = 0; i < ARRAYSIZE(hives); i++)
        if (!StrCmpNICW(out, hives[i].prefix, 5)) {
            lstrcpynW(key, out + 5, ARRAYSIZE(key));
            return registry_command(hives[i].hive, key, nullptr, out, size);
        }
    return TRUE;
}

HRESULT WINAPI RunRegCommand(HWND window, HKEY root, LPCWSTR path);
HRESULT WINAPI RunRegCommand(HWND window, HKEY root, LPCWSTR path)
{
    WCHAR template_text[1024];
    HRESULT hr = E_FAIL;
    if (!registry_command(root, path, L"", template_text, ARRAYSIZE(template_text)))
        return hr;
    HRESULT(WINAPI * evaluate)(LPCWSTR, LPWSTR *, LPWSTR *, LPWSTR *) =
        (HRESULT(WINAPI *)(LPCWSTR, LPWSTR *, LPWSTR *, LPWSTR *))ShlProc(L"shell32.dll",
                                                                          "SHEvaluateSystemCommandTemplate");
    if (!evaluate)
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    LPWSTR application = nullptr, parameters = nullptr;
    hr = evaluate(template_text, &application, nullptr, &parameters);
    if (SUCCEEDED(hr)) {
        SHELLEXECUTEINFOW execute = {sizeof(execute)};
        execute.fMask = SEE_MASK_FLAG_NO_UI;
        execute.lpFile = application;
        execute.lpParameters = parameters;
        execute.hwnd = window;
        execute.nShow = SW_SHOWNORMAL;
        BOOL (WINAPI * run)(SHELLEXECUTEINFOW *) =
            (BOOL(WINAPI *)(SHELLEXECUTEINFOW *))ShlProc(L"shell32.dll", "ShellExecuteExW");
        if (run && run(&execute)) {
            hr = S_OK;
        } else {
            hr = status_result((LSTATUS)GetLastError());
            if (hr >= 0)
                hr = E_FAIL;
        }
        void (WINAPI * free_task)(void *) = (void(WINAPI *)(void *))ShlProc(L"ole32.dll", "CoTaskMemFree");
        if (free_task) {
            free_task(application);
            free_task(parameters);
        }
    }
    return hr;
}

HRESULT WINAPI RunIndirectRegCommand(HWND window, HKEY root, LPCWSTR path, LPCWSTR verb);
HRESULT WINAPI RunIndirectRegCommand(HWND window, HKEY root, LPCWSTR path, LPCWSTR verb)
{
    WCHAR command[256];
    HRESULT hr;
    if (path)
        hr = wnsprintfW(command, ARRAYSIZE(command), L"%s\\shell\\%s\\command", path, verb) >= 0 ? S_OK : E_FAIL;
    else
        hr = wnsprintfW(command, ARRAYSIZE(command), L"shell\\%s\\command", verb) >= 0 ? S_OK : E_FAIL;
    return SUCCEEDED(hr) ? RunRegCommand(window, root, command) : hr;
}

HRESULT WINAPI SHRunIndirectRegClientCommand(HWND window, LPCWSTR client);
HRESULT WINAPI SHRunIndirectRegClientCommand(HWND window, LPCWSTR client)
{
    WCHAR path[256], name[80];
    LONG size = sizeof(name);
    wnsprintfW(path, ARRAYSIZE(path), L"Software\\Clients\\%s", client);
    LSTATUS status = query_default(HKEY_CURRENT_USER, path, name, &size);
    if (status != ERROR_SUCCESS && status != ERROR_MORE_DATA) {
        HKEY key = nullptr;
        status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, 0x20219, &key);
        if (status == ERROR_SUCCESS) {
            size = sizeof(name);
            status = query_default(key, nullptr, name, &size);
            RegCloseKey(key);
        }
        if (status != ERROR_SUCCESS && status != ERROR_MORE_DATA) {
            size = sizeof(name);
            status = query_default(HKEY_LOCAL_MACHINE, path, name, &size);
        }
    }
    if (status != ERROR_SUCCESS)
        return status_result(status);
    wnsprintfW(path, ARRAYSIZE(path), L"Software\\Clients\\%s\\%s", client, name);
    HRESULT hr = RunIndirectRegCommand(window, HKEY_LOCAL_MACHINE, path, L"Open");
    if (FAILED(hr)) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, path, 0, 0x20219, &key) == ERROR_SUCCESS) {
            hr = RunIndirectRegCommand(window, key, nullptr, L"Open");
            RegCloseKey(key);
        }
    }
    return hr;
}
