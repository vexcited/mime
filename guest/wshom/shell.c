/*
 * WScript.Shell: starting programs, the registry, environment variables, special folders, shortcuts, message boxes, the event
 * log, windows and keys.
 */
#include "wshom_private.h"

static const WSH_CLASS g_shell_class = {
    &IID_IWshShell3, &CLSID_WshShell, {&IID_IWshShell, &IID_IWshShell2, &IID_IWshShell3, nullptr}};

typedef struct WSH_SHELL {
    WSH_OBJECT Object;
} WSH_SHELL;

WSH_COMMON_METHODS(shell, IWshShell3)

static const WCHAR *const k_run = L"WshShell.Run";

/* ------------------------------------------------------------------ strings of environment variables */

static HRESULT expand_environment(const WCHAR *source, BSTR *out)
{
    *out = nullptr;
    if (!source || !*source)
        return S_OK;
    WCHAR buffer[0x400];
    DWORD needed = ExpandEnvironmentStringsW(source, buffer, 0x400);
    if (!needed)
        return HrFromLastError();
    if (needed <= 0x400) {
        *out = SysAllocString(buffer);
        return *out ? S_OK : E_OUTOFMEMORY;
    }
    *out = SysAllocStringLen(nullptr, needed - 1);
    if (!*out)
        return E_OUTOFMEMORY;
    if (!ExpandEnvironmentStringsW(source, *out, needed)) {
        HRESULT hr = HrFromLastError();
        SysFreeString(*out);
        *out = nullptr;
        return hr;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE shell_ExpandEnvironmentStrings(IWshShell3 *This, BSTR source, BSTR *out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return expand_environment(source, out);
}

/* ------------------------------------------------------------------ Run */

static HRESULT STDMETHODCALLTYPE shell_Run(IWshShell3 *This, BSTR command, VARIANT *style, VARIANT *wait, int *result)
{
    if (!style || !wait || !result)
        return E_POINTER;
    if (!command || !*command)
        return E_INVALIDARG;
    BSTR text;
    HRESULT hr = expand_environment(command, &text);
    if (FAILED(hr))
        return hr;
    /* the program is up to the closing quote of a quoted name, otherwise up to the first space or tab. */
    WCHAR *file = text, *end;
    if (*text == '"') {
        file = text + 1;
        end = wcschr(text + 2, '"');
    } else {
        end = wcschr(text, ' ');
        if (!end)
            end = wcschr(text, '\t');
    }
    WCHAR *parameters = nullptr;
    if (end) {
        *end++ = 0;
        parameters = end;
    }
    LONG show = 1;
    BOOL wait_for = FALSE;
    DWORD mask = SEE_MASK_FLAG_NO_UI;
    if (WshArgumentGiven(style)) {
        hr = WshVariantLong(style, &show);
        if (FAILED(hr))
            goto done;
    }
    if (WshArgumentGiven(wait)) {
        int value = 0;
        mask |= SEE_MASK_NOCLOSEPROCESS;
        hr = WshVariantBool(wait, &value);
        if (FAILED(hr))
            goto done;
        wait_for = value != 0;
    }
    SHELLEXECUTEINFOW info;
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    info.fMask = mask;
    info.lpVerb = L"Open";
    info.lpFile = file;
    info.lpParameters = parameters;
    info.nShow = show;
    if (!ShellExecuteExW(&info)) {
        hr = HrFromLastError();
        goto done;
    }
    HANDLE process = info.hProcess;
    if (wait_for) {
        if (!process) {
            WshSignal(&IID_IWshShell, k_run, MSG_RUN_WAIT, nullptr);
            hr = DISP_E_EXCEPTION_HR;
            goto done;
        }
        MSG message;
        for (;;) {
            DWORD got = MsgWaitForMultipleObjects(1, &process, FALSE, INFINITE, QS_ALLINPUT);
            if (got == WAIT_FAILED) {
                hr = HrFromLastError();
                CloseHandle(process);
                goto done;
            }
            if (got != WAIT_OBJECT_0 + 1)
                break;
            BOOL quit = FALSE;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
                if (message.message == WM_QUIT)
                    quit = TRUE;
            }
            if (quit)
                break;
        }
        DWORD code = 0;
        if (!GetExitCodeProcess(process, &code)) {
            hr = HrFromLastError();
            CloseHandle(process);
            goto done;
        }
        *result = (int)code;
    } else {
        *result = 0;
    }
    if (process)
        CloseHandle(process);
    hr = S_OK;
done:
    SysFreeString(text);
    return hr;
}

/* ------------------------------------------------------------------ Popup */

typedef struct POPUP_ARGS {
    const WCHAR *Text, *Title;
    UINT Type;
} POPUP_ARGS;

static DWORD WINAPI popup_thread(LPVOID parameter)
{
    POPUP_ARGS *args = parameter;
    return (DWORD)MessageBoxW(nullptr, args->Text, args->Title, args->Type | MB_SETFOREGROUND);
}

static BOOL CALLBACK count_window(HWND window, LPARAM count)
{
    ++*(int *)count;
    return FALSE;
}

HRESULT WshPopup(const WCHAR *text, const WCHAR *title, UINT type, int seconds, int *button)
{
    POPUP_ARGS args = {text ? text : L"", title ? title : L"", type};
    DWORD timeout = seconds ? 1000u * (DWORD)seconds : INFINITE;
    DWORD thread_id = 0;
    HANDLE thread = CreateThread(nullptr, 0, popup_thread, &args, 0, &thread_id);
    if (!thread)
        return HrFromLastError();
    MSG message;
    DWORD got;
    for (;;) {
        got = MsgWaitForMultipleObjects(1, &thread, FALSE, timeout, QS_ALLINPUT);
        if (got != WAIT_OBJECT_0 + 1)
            break;
        BOOL quit = FALSE;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (message.message == WM_QUIT)
                quit = TRUE;
        }
        if (quit)
            break;
    }
    if (got != WAIT_OBJECT_0) {
        /* the time is out: the box is told to end until the thread has no window. */
        int windows;
        do {
            windows = 0;
            EnumThreadWindows(thread_id, count_window, (LPARAM)&windows);
            if (windows > 0) {
                PostThreadMessageA(thread_id, WM_QUIT, 0xABCDEFBB, 0);
                PostThreadMessageA(thread_id, WM_NULL, 0, 0);
                Sleep(100);
            }
        } while (windows);
        WaitForSingleObject(thread, INFINITE);
        *button = -1;
        CloseHandle(thread);
        return S_OK;
    }
    DWORD code = 0;
    GetExitCodeThread(thread, &code);
    *button = (int)code;
    CloseHandle(thread);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE shell_Popup(IWshShell3 *This, BSTR text, VARIANT *seconds, VARIANT *title, VARIANT *type,
                                             int *button)
{
    if (!seconds || !title || !type || !button)
        return E_POINTER;
    *button = 0;
    LONG wait = 0, flags = 0;
    BSTR caption = nullptr;
    HRESULT hr = S_OK;
    if (WshArgumentGiven(seconds) && FAILED(hr = WshVariantLong(seconds, &wait)))
        return hr;
    if (WshArgumentGiven(title)) {
        hr = WshVariantBstr(title, &caption);
        if (FAILED(hr))
            return hr;
    } else {
        caption = WshLoadBstr(MSG_TITLE);
        if (!caption)
            return E_OUTOFMEMORY;
    }
    if (WshArgumentGiven(type) && FAILED(hr = WshVariantLong(type, &flags))) {
        SysFreeString(caption);
        return hr;
    }
    int pressed = 0;
    hr = WshPopup(text, caption, (UINT)flags, wait, &pressed);
    if (SUCCEEDED(hr))
        *button = pressed;
    SysFreeString(caption);
    return hr;
}

/* ------------------------------------------------------------------ the registry */

static const struct {
    const WCHAR *Prefix;
    HKEY Root;
} g_roots[] = {
    {L"HKCU\\", HKEY_CURRENT_USER},
    {L"HKLM\\", HKEY_LOCAL_MACHINE},
    {L"HKCR\\", HKEY_CLASSES_ROOT},
    {L"HKEY_CURRENT_USER\\", HKEY_CURRENT_USER},
    {L"HKEY_LOCAL_MACHINE\\", HKEY_LOCAL_MACHINE},
    {L"HKEY_CLASSES_ROOT\\", HKEY_CLASSES_ROOT},
    {L"HKEY_USERS\\", HKEY_USERS},
    {L"HKEY_CURRENT_CONFIG\\", HKEY_CURRENT_CONFIG},
    {L"HKEY_DYN_DATA\\", (HKEY)(ULONG_PTR)0xFFFFFFFF80000006ull},
};

/* the key of a registry path and the value or key name after the last backslash, opened or created for the access. */
static LONG parse_key(const WCHAR *path, REGSAM access, BOOL create, HKEY *key, BOOL *opened, const WCHAR **name)
{
    *opened = FALSE;
    for (SIZE_T i = 0; i < sizeof(g_roots) / sizeof(g_roots[0]); i++) {
        SIZE_T length = wcslen(g_roots[i].Prefix);
        if (wcsncmp(path, g_roots[i].Prefix, length))
            continue;
        const WCHAR *rest = path + length;
        const WCHAR *slash = wcsrchr(rest, '\\');
        if (!slash) {
            *key = g_roots[i].Root;
            *name = rest;
            return ERROR_SUCCESS;
        }
        SIZE_T count = (SIZE_T)(slash - rest);
        WCHAR *sub = HeapAlloc(GetProcessHeap(), 0, (count + 1) * sizeof(WCHAR));
        if (!sub)
            return ERROR_OUTOFMEMORY;
        memcpy(sub, rest, count * sizeof(WCHAR));
        sub[count] = 0;
        LONG status;
        if (create && (access & 0x20006))
            status = RegCreateKeyExW(g_roots[i].Root, sub, 0, nullptr, 0, access, nullptr, key, nullptr);
        else
            status = RegOpenKeyExW(g_roots[i].Root, sub, 0, access, key);
        HeapFree(GetProcessHeap(), 0, sub);
        if (status == ERROR_SUCCESS) {
            *opened = TRUE;
            *name = slash + 1;
        }
        return status;
    }
    return ERROR_PATH_NOT_FOUND;
}

static HRESULT win32_hr(LONG status)
{
    return status > 0 ? (HRESULT)(0x80070000u | (status & 0xFFFF)) : (HRESULT)status;
}

static HRESULT STDMETHODCALLTYPE shell_RegRead(IWshShell3 *This, BSTR path, VARIANT *value)
{
    if (!path || !value)
        return E_POINTER;
    const WCHAR *source = L"WshShell.RegRead";
    HKEY key;
    BOOL opened;
    const WCHAR *name;
    LONG status = parse_key(path, KEY_QUERY_VALUE, FALSE, &key, &opened, &name);
    if (status) {
        WshSignal(&IID_IWshEnvironment, source, MSG_REG_ROOT, path);
        return win32_hr(status == ERROR_PATH_NOT_FOUND ? ERROR_PATH_NOT_FOUND : status);
    }
    DWORD type = 0, size = 0;
    status = RegQueryValueExW(key, name, nullptr, &type, nullptr, &size);
    if (status) {
        HRESULT hr;
        if (*name) {
            WshSignal(&IID_IWshEnvironment, source, MSG_REG_OPEN, path);
            hr = win32_hr(status);
        } else {
            value->vt = VT_BSTR;
            value->bstrVal = SysAllocString(L"");
            hr = value->bstrVal ? S_OK : E_OUTOFMEMORY;
        }
        if (opened)
            RegCloseKey(key);
        return hr;
    }
    BYTE *data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size + 4);
    if (!data) {
        if (opened)
            RegCloseKey(key);
        return E_OUTOFMEMORY;
    }
    status = RegQueryValueExW(key, name, nullptr, &type, data, &size);
    if (opened)
        RegCloseKey(key);
    HRESULT hr = S_OK;
    if (status) {
        WshSignal(&IID_IWshEnvironment, source, MSG_REG_OPEN, path);
        hr = win32_hr(status);
    } else {
        VariantInit(value);
        switch (type) {
        case REG_SZ:
        case REG_EXPAND_SZ:
            value->vt = VT_BSTR;
            value->bstrVal = SysAllocString((const WCHAR *)data);
            if (!value->bstrVal)
                hr = E_OUTOFMEMORY;
            break;
        case REG_BINARY: {
            SAFEARRAYBOUND bound = {size, 0};
            SAFEARRAY *array = SafeArrayCreate(VT_VARIANT, 1, &bound);
            if (!array) {
                hr = E_OUTOFMEMORY;
                break;
            }
            for (LONG i = 0; i < (LONG)size && SUCCEEDED(hr); i++) {
                VARIANT item;
                VariantInit(&item);
                item.vt = VT_UI1;
                item.bVal = data[i];
                hr = SafeArrayPutElement(array, &i, &item);
            }
            if (FAILED(hr)) {
                SafeArrayDestroy(array);
                break;
            }
            value->vt = VT_ARRAY | VT_VARIANT;
            value->parray = array;
            break;
        }
        case REG_DWORD:
            value->vt = VT_I4;
            value->lVal = *(LONG *)data;
            break;
        case REG_MULTI_SZ: {
            LONG count = 0;
            for (const WCHAR *p = (const WCHAR *)data; *p; p += wcslen(p) + 1)
                count++;
            SAFEARRAYBOUND bound = {(ULONG)count, 0};
            SAFEARRAY *array = SafeArrayCreate(VT_VARIANT, 1, &bound);
            if (!array) {
                hr = E_OUTOFMEMORY;
                break;
            }
            const WCHAR *p = (const WCHAR *)data;
            for (LONG i = 0; i < count && SUCCEEDED(hr); i++) {
                VARIANT item;
                VariantInit(&item);
                item.vt = VT_BSTR;
                item.bstrVal = SysAllocString(p);
                if (!item.bstrVal) {
                    hr = E_OUTOFMEMORY;
                    break;
                }
                hr = SafeArrayPutElement(array, &i, &item);
                VariantClear(&item);
                p += wcslen(p) + 1;
            }
            if (FAILED(hr)) {
                SafeArrayDestroy(array);
                break;
            }
            value->vt = VT_ARRAY | VT_VARIANT;
            value->parray = array;
            break;
        }
        default:
            hr = DISP_E_TYPEMISMATCH;
            break;
        }
    }
    HeapFree(GetProcessHeap(), 0, data);
    return hr;
}

/* the type a script names for a value, REG_SZ when it names none. */
static HRESULT registry_type(VARIANT *type, DWORD *out)
{
    if (!WshArgumentGiven(type)) {
        *out = REG_SZ;
        return S_OK;
    }
    BSTR name;
    HRESULT hr = WshVariantBstr(type, &name);
    if (FAILED(hr))
        return hr;
    if (!name)
        return E_OUTOFMEMORY;
    if (!wcscmp(name, L"REG_SZ"))
        *out = REG_SZ;
    else if (!wcscmp(name, L"REG_EXPAND_SZ"))
        *out = REG_EXPAND_SZ;
    else if (!wcscmp(name, L"REG_DWORD"))
        *out = REG_DWORD;
    else if (!wcscmp(name, L"REG_BINARY"))
        *out = REG_BINARY;
    else
        hr = E_INVALIDARG;
    SysFreeString(name);
    return hr;
}

static HRESULT STDMETHODCALLTYPE shell_RegWrite(IWshShell3 *This, BSTR path, VARIANT *value, VARIANT *type)
{
    if (!path || !value || !type)
        return E_POINTER;
    DWORD kind = 0;
    HRESULT hr = registry_type(type, &kind);
    if (FAILED(hr))
        return hr;
    HKEY key;
    BOOL opened;
    const WCHAR *name;
    LONG status = parse_key(path, KEY_SET_VALUE, TRUE, &key, &opened, &name);
    if (status) {
        WshSignal(&IID_IWshEnvironment, L"WshShell.RegWrite", MSG_REG_ROOT, path);
        return win32_hr(status);
    }
    BSTR text = nullptr;
    LONG number = 0;
    const BYTE *data = nullptr;
    DWORD size = 1;
    switch (kind) {
    case REG_SZ:
    case REG_EXPAND_SZ:
        hr = WshVariantBstr(value, &text);
        if (FAILED(hr))
            goto done;
        data = (const BYTE *)(text ? text : L"");
        size = (DWORD)((text ? wcslen(text) : 0) + 1) * sizeof(WCHAR);
        break;
    case REG_BINARY:
        switch (value->vt) {
        case VT_I2:
            size = 2;
            break;
        case VT_I4:
            size = 4;
            break;
        case VT_UI1:
            break;
        default:
            hr = DISP_E_TYPEMISMATCH;
            goto done;
        }
        data = (const BYTE *)&value->bVal;
        break;
    case REG_DWORD:
        hr = WshVariantLong(value, &number);
        if (FAILED(hr))
            goto done;
        size = 4;
        data = (const BYTE *)&number;
        break;
    default:
        hr = E_UNEXPECTED;
        goto done;
    }
    hr = win32_hr(RegSetValueExW(key, name, 0, kind, data, size));
done:
    SysFreeString(text);
    if (opened)
        RegCloseKey(key);
    return hr;
}

static HRESULT STDMETHODCALLTYPE shell_RegDelete(IWshShell3 *This, BSTR path)
{
    if (!SysStringLen(path))
        return E_INVALIDARG;
    SIZE_T length = wcslen(path);
    WCHAR *copy = HeapAlloc(GetProcessHeap(), 0, (length + 1) * sizeof(WCHAR));
    if (!copy)
        return E_OUTOFMEMORY;
    memcpy(copy, path, (length + 1) * sizeof(WCHAR));
    BOOL is_key = copy[length - 1] == '\\';
    if (is_key)
        copy[length - 1] = 0;
    HKEY key;
    BOOL opened;
    const WCHAR *name;
    LONG status = parse_key(copy, 0x20006, FALSE, &key, &opened, &name);
    HRESULT hr;
    if (status) {
        WshSignal(&IID_IWshEnvironment, L"WshShell.RegDelete", MSG_REG_ROOT, path);
        hr = win32_hr(status);
    } else {
        status = is_key ? RegDeleteKeyW(key, name) : RegDeleteValueW(key, name);
        if (opened)
            RegCloseKey(key);
        hr = win32_hr(status);
        if (FAILED(hr))
            WshSignal(&IID_IWshEnvironment, L"WshShell.RegDelete", MSG_REG_REMOVE, path);
    }
    HeapFree(GetProcessHeap(), 0, copy);
    return hr;
}

/* ------------------------------------------------------------------ environment and folders */

static HRESULT environment_type(VARIANT *type, int *out)
{
    if (!WshArgumentGiven(type)) {
        *out = 0;
        return S_OK;
    }
    BSTR name;
    HRESULT hr = WshVariantBstr(type, &name);
    if (FAILED(hr))
        return hr;
    const WCHAR *text = name ? name : L"";
    if (!_wcsicmp(text, L"SYSTEM"))
        *out = 0;
    else if (!_wcsicmp(text, L"USER"))
        *out = 1;
    else if (!_wcsicmp(text, L"VOLATILE"))
        *out = 2;
    else if (!_wcsicmp(text, L"PROCESS"))
        *out = 3;
    else
        hr = E_INVALIDARG;
    SysFreeString(name);
    return hr;
}

static HRESULT STDMETHODCALLTYPE shell_get_Environment(IWshShell3 *This, VARIANT *type, IWshEnvironment **out)
{
    int kind = 0;
    HRESULT hr = environment_type(type, &kind);
    if (FAILED(hr))
        return hr;
    return WshEnvironmentCreate(kind, out);
}

static const struct {
    const WCHAR *Name;
    int Csidl;
} g_special_folders[] = {
    {L"AllUsersDesktop", 0x19}, {L"AllUsersStartMenu", 0x16}, {L"AllUsersPrograms", 0x17}, {L"AllUsersStartup", 0x18},
    {L"Desktop", 0x0}, {L"AppData", 0x1a}, {L"PrintHood", 0x1b}, {L"Templates", 0x15}, {L"Fonts", 0x14},
    {L"NetHood", 0x13}, {L"Desktop", 0x10}, {L"StartMenu", 0xb}, {L"SendTo", 0x9}, {L"Recent", 0x8},
    {L"Startup", 0x7}, {L"Favorites", 0x6}, {L"MyDocuments", 0x5}, {L"Programs", 0x2},
};

static HRESULT STDMETHODCALLTYPE shell_get_SpecialFolders(IWshShell3 *This, IWshCollection **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    SAFEARRAY *paths = SafeArrayCreateVector(VT_VARIANT, 0, 19), *names = SafeArrayCreateVector(VT_VARIANT, 0, 19);
    if (!paths || !names) {
        if (paths)
            SafeArrayDestroy(paths);
        if (names)
            SafeArrayDestroy(names);
        return E_OUTOFMEMORY;
    }
    HRESULT hr = S_OK;
    LONG count = 0;
    for (SIZE_T i = 0; i < sizeof(g_special_folders) / sizeof(g_special_folders[0]) && SUCCEEDED(hr); i++) {
        LPITEMIDLIST list = nullptr;
        if (FAILED(SHGetSpecialFolderLocation(nullptr, g_special_folders[i].Csidl, &list)))
            continue;
        WCHAR path[MAX_PATH];
        BOOL ok = SHGetPathFromIDListW(list, path);
        CoTaskMemFree(list);
        if (!ok)
            continue;
        VARIANT item;
        VariantInit(&item);
        item.vt = VT_BSTR;
        item.bstrVal = SysAllocString(path);
        if (!item.bstrVal) {
            hr = E_OUTOFMEMORY;
            break;
        }
        hr = SafeArrayPutElement(paths, &count, &item);
        VariantClear(&item);
        if (FAILED(hr))
            break;
        item.vt = VT_BSTR;
        item.bstrVal = SysAllocString(g_special_folders[i].Name);
        if (!item.bstrVal) {
            hr = E_OUTOFMEMORY;
            break;
        }
        hr = SafeArrayPutElement(names, &count, &item);
        VariantClear(&item);
        count++;
    }
    if (SUCCEEDED(hr)) {
        SAFEARRAYBOUND bound = {(ULONG)count, 0};
        hr = SafeArrayRedim(paths, &bound);
        if (SUCCEEDED(hr))
            hr = SafeArrayRedim(names, &bound);
    }
    if (SUCCEEDED(hr))
        hr = WshCollectionCreate(paths, names, out);
    SafeArrayDestroy(paths);
    SafeArrayDestroy(names);
    return hr;
}

static HRESULT STDMETHODCALLTYPE shell_CreateShortcut(IWshShell3 *This, BSTR path, IDispatch **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    const WCHAR *dot = path ? wcsrchr(path, '.') : nullptr;
    if (dot && !_wcsicmp(dot + 1, L"LNK"))
        return WshShortcutCreate(path, out);
    if (dot && !_wcsicmp(dot + 1, L"URL"))
        return WshUrlShortcutCreate(path, out);
    WshSignal(&IID_IWshShell, L"WshShell.CreateShortcut", MSG_SHORTCUT_NAME, nullptr);
    return DISP_E_EXCEPTION_HR;
}

/* ------------------------------------------------------------------ the event log */

static HRESULT STDMETHODCALLTYPE shell_LogEvent(IWshShell3 *This, VARIANT *type, BSTR message, BSTR target,
                                                VARIANT_BOOL *result)
{
    if (!type || !message || !target || !result)
        return E_POINTER;
    LONG kind = 0;
    VARIANT converted;
    VariantInit(&converted);
    HRESULT hr = VariantChangeType(&converted, type, 0, VT_I2);
    if (FAILED(hr))
        return hr;
    kind = converted.iVal;
    if ((USHORT)kind > 16 || !((0x10117u >> (USHORT)kind) & 1))
        return E_INVALIDARG;
    DWORD id = 0;
    switch (kind) {
    case 1:
        id = 0xC0000001;
        break;
    case 2:
        id = 0x80000002;
        break;
    case 4:
        id = 0x40000004;
        break;
    case 8:
        id = 8;
        break;
    case 16:
        id = 0xC0000010;
        break;
    default:
        break;
    }
    HANDLE source = RegisterEventSourceW(target, L"WSH");
    if (!source)
        hr = HrFromLastError();
    else {
        const WCHAR *strings[1] = {message};
        hr = ReportEventW(source, (WORD)kind, 0, id, nullptr, 1, 0, strings, nullptr) ? S_OK : HrFromLastError();
        DeregisterEventSource(source);
    }
    *result = SUCCEEDED(hr) ? VARIANT_TRUE : VARIANT_FALSE;
    return hr;
}

/* ------------------------------------------------------------------ windows */

static BOOL window_usable(HWND window)
{
    return IsWindowEnabled(window) && IsWindowVisible(window);
}

static BOOL title_matches(HWND window, const WCHAR *title, int length, BOOL at_end)
{
    int text_length = GetWindowTextLengthW(window);
    if (text_length <= length)
        return FALSE;
    WCHAR *text = HeapAlloc(GetProcessHeap(), 0, (text_length + 1) * sizeof(WCHAR));
    if (!text)
        return FALSE;
    int got = GetWindowTextW(window, text, text_length + 1);
    BOOL match = got > 0 && !_wcsnicmp(title, text + (at_end ? got - length : 0), length);
    HeapFree(GetProcessHeap(), 0, text);
    return match;
}

static HRESULT STDMETHODCALLTYPE shell_AppActivate(IWshShell3 *This, VARIANT *app, VARIANT *wait, VARIANT_BOOL *result)
{
    if (!app || !wait)
        return E_POINTER;
    *result = VARIANT_FALSE;
    HWND window = nullptr;
    LONG pid = 0;
    BSTR title = nullptr;
    if (SUCCEEDED(WshVariantLong(app, &pid))) {
        HWND desktop = GetDesktopWindow();
        DWORD found = 0;
        for (window = GetWindow(desktop, GW_CHILD); window; window = GetWindow(window, GW_HWNDNEXT)) {
            GetWindowThreadProcessId(window, &found);
            if ((LONG)found == pid && window_usable(window))
                break;
        }
        if (!window) {
            for (window = GetWindow(desktop, GW_CHILD); window; window = GetWindow(window, GW_HWNDNEXT)) {
                GetWindowThreadProcessId(window, &found);
                if ((LONG)found == pid)
                    break;
            }
        }
    } else {
        HRESULT hr = WshVariantBstr(app, &title);
        if (FAILED(hr))
            return hr;
        int length = (int)SysStringLen(title);
        window = FindWindowW(nullptr, title);
        HWND desktop = GetDesktopWindow();
        for (HWND w = GetWindow(desktop, GW_CHILD); !window && w; w = GetWindow(w, GW_HWNDNEXT)) {
            if (title_matches(w, title, length, FALSE))
                window = w;
        }
        for (HWND w = GetWindow(desktop, GW_CHILD); !window && w; w = GetWindow(w, GW_HWNDNEXT)) {
            if (title_matches(w, title, length, TRUE))
                window = w;
        }
    }
    SysFreeString(title);
    if (!window)
        return S_OK;
    if (!window_usable(window)) {
        /* a disabled or hidden window passes to its popup that is usable. */
        HWND owned = GetWindow(window, GW_HWNDFIRST);
        for (; owned; owned = GetWindow(owned, GW_HWNDNEXT)) {
            if (GetWindow(owned, GW_OWNER) == window && window_usable(owned))
                break;
        }
        if (!owned)
            return S_OK;
        window = owned;
    }
    DWORD process;
    DWORD thread = GetWindowThreadProcessId(window, &process);
    BOOL attached = AttachThreadInput(GetCurrentThreadId(), thread, TRUE);
    SetForegroundWindow(window);
    *result = SetFocus(window) == nullptr ? (GetLastError() != 0 ? VARIANT_FALSE : VARIANT_TRUE) : VARIANT_TRUE;
    if (attached)
        AttachThreadInput(GetCurrentThreadId(), thread, FALSE);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE shell_SendKeys(IWshShell3 *This, BSTR keys, VARIANT *wait)
{
    if (!keys || !wait)
        return E_POINTER;
    BOOL wait_for = FALSE;
    if (WshArgumentGiven(wait)) {
        int value = 0;
        HRESULT hr = WshVariantBool(wait, &value);
        if (FAILED(hr))
            return hr;
        wait_for = value != 0;
    }
    return WshSendKeys(keys, wait_for);
}

/* ------------------------------------------------------------------ Exec */

static HRESULT STDMETHODCALLTYPE shell_Exec(IWshShell3 *This, BSTR command, IWshExec **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    BSTR text;
    HRESULT hr = expand_environment(command, &text);
    if (FAILED(hr))
        return hr;
    if (!text || !SysStringLen(text)) {
        SysFreeString(text);
        WshSignal(&IID_IWshShell, L"WshShell.Exec", MSG_EXEC_EMPTY, nullptr);
        return DISP_E_EXCEPTION_HR;
    }
    SECURITY_ATTRIBUTES attributes = {sizeof(attributes), nullptr, TRUE};
    HANDLE out_read = nullptr, out_write = nullptr, in_read = nullptr, in_write = nullptr, err_read = nullptr,
           err_write = nullptr;
    HANDLE out_read_own = nullptr, in_write_own = nullptr, err_read_own = nullptr;
    HANDLE self = GetCurrentProcess();
    IWshExec *exec = nullptr;
    if (CreatePipe(&out_read, &out_write, &attributes, 0) && CreatePipe(&in_read, &in_write, &attributes, 0) &&
        CreatePipe(&err_read, &err_write, &attributes, 0) &&
        DuplicateHandle(self, out_read, self, &out_read_own, 0, FALSE, DUPLICATE_SAME_ACCESS) &&
        DuplicateHandle(self, in_write, self, &in_write_own, 0, FALSE, DUPLICATE_SAME_ACCESS) &&
        DuplicateHandle(self, err_read, self, &err_read_own, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        CloseHandle(out_read);
        out_read = nullptr;
        CloseHandle(in_write);
        in_write = nullptr;
        CloseHandle(err_read);
        err_read = nullptr;
        hr = WshExecCreate(out_read_own, in_write_own, err_read_own, &exec);
        out_read_own = in_write_own = err_read_own = nullptr;
        if (SUCCEEDED(hr)) {
            hr = WshCreateProcess(exec, text, out_write, in_read, err_write);
            if (SUCCEEDED(hr)) {
                *out = exec;
                exec = nullptr;
            } else {
                WshSignalHr(&IID_IWshShell, L"WshShell.Exec", hr);
            }
        }
    } else {
        hr = HrFromLastError();
    }
    SysFreeString(text);
    HANDLE all[] = {out_read, out_write, in_read, in_write, err_read, err_write, out_read_own, in_write_own, err_read_own};
    for (int i = 0; i < 9; i++)
        if (all[i])
            CloseHandle(all[i]);
    if (exec)
        exec->lpVtbl->Release(exec);
    return hr;
}

static HRESULT STDMETHODCALLTYPE shell_get_CurrentDirectory(IWshShell3 *This, BSTR *out)
{
    if (!out)
        return E_POINTER;
    DWORD size = GetCurrentDirectoryW(0, nullptr);
    WCHAR *buffer = HeapAlloc(GetProcessHeap(), 0, (size + 1) * sizeof(WCHAR));
    if (!buffer)
        return E_OUTOFMEMORY;
    HRESULT hr = S_OK;
    if (GetCurrentDirectoryW(size, buffer))
        *out = SysAllocString(buffer);
    else
        hr = HrFromLastError();
    HeapFree(GetProcessHeap(), 0, buffer);
    return hr;
}

static HRESULT STDMETHODCALLTYPE shell_put_CurrentDirectory(IWshShell3 *This, BSTR directory)
{
    return SetCurrentDirectoryW(directory) ? S_OK : HrFromLastError();
}

static const IWshShell3Vtbl g_shell_vtbl = {
    shell_QueryInterface, shell_AddRef, shell_Release, shell_GetTypeInfoCount, shell_GetTypeInfo, shell_GetIDsOfNames,
    shell_Invoke, shell_get_SpecialFolders, shell_get_Environment, shell_Run, shell_Popup, shell_CreateShortcut,
    shell_ExpandEnvironmentStrings, shell_RegRead, shell_RegWrite, shell_RegDelete, shell_LogEvent, shell_AppActivate,
    shell_SendKeys, shell_Exec, shell_get_CurrentDirectory, shell_put_CurrentDirectory,
};

HRESULT WshShellCreate(REFIID riid, void **object)
{
    WSH_SHELL *shell = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*shell));
    if (!shell)
        return E_OUTOFMEMORY;
    WshObjectInit(&shell->Object, &g_shell_vtbl, &g_shell_class, nullptr);
    HRESULT hr = WshQueryInterface(shell, riid, object);
    WshRelease(shell);
    return hr;
}
