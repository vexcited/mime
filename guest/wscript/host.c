/*
 * the run of a script: the site the engine reports to, the engine made from the registry, the timer that ends a script that
 * runs too long, and the entry point with the order of the steps and the exit code.
 */
#include "wsh.h"

HOST g_host;

static SCRIPT_ENGINE *g_engines;
static DWORD g_contexts;
static volatile DWORD g_timer_ms;
static volatile BOOL g_timer_exit;

/* ------------------------------------------------------------------ the interrupt and the timer */

HRESULT WshInterrupt(HRESULT code)
{
    InterlockedExchange(&g_host.Pending, code);
    EXCEPINFO info = {};
    info.scode = code;
    HRESULT hr = E_FAIL;
    EnterCriticalSection(&g_host.Lock);
    hr = S_OK;
    for (SCRIPT_ENGINE *engine = g_engines; engine; engine = engine->Next) {
        if (engine->Closed)
            continue;
        HRESULT part = engine->Script->lpVtbl->InterruptScriptThread(engine->Script, SCRIPTTHREADID_ALL, &info, SCRIPTINTERRUPT_RAISEEXCEPTION);
        if (FAILED(part))
            hr = part;
    }
    LeaveCriticalSection(&g_host.Lock);
    return hr;
}

static BOOL is_ime_window(HWND window)
{
    for (; window; window = GetParent(window)) {
        WCHAR name[8];
        if (GetClassNameW(window, name, 5) == 3 && (name[0] | 0x20) == 'i' && (name[1] | 0x20) == 'm' && (name[2] | 0x20) == 'e')
            return TRUE;
    }
    return FALSE;
}

static BOOL CALLBACK close_window(HWND window, LPARAM param)
{
    if (IsWindowVisible(window) && !is_ime_window(window)) {
        PostMessageA(window, WM_CLOSE, 0, 0);
        PostMessageA(window, WM_KEYDOWN, VK_RETURN, 0);
        ++*(int *)param;
    }
    return TRUE;
}

/* the time is over: the script is interrupted and the windows of the script are closed, such as the box of an Echo. */
static void time_is_over(void)
{
    WshInterrupt(WSH_E_TIMEOUT);
    int count;
    do {
        count = 0;
        EnumThreadWindows(g_host.MainThread, close_window, (LPARAM)&count);
        if (count)
            Sleep(100);
    } while (count);
}

static DWORD WINAPI timer_thread(LPVOID param)
{
    for (;;) {
        DWORD ms = g_timer_ms;
        DWORD result = WaitForSingleObject(g_host.TimerEvent, ms ? ms : INFINITE);
        if (g_timer_exit)
            return 0;
        if (result == WAIT_TIMEOUT) {
            g_timer_ms = 0;
            time_is_over();
        }
    }
}

static HRESULT create_timer(void)
{
    g_host.TimerEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_host.TimerEvent)
        return HRESULT_FROM_WIN32(GetLastError());
    g_host.TimerThread = CreateThread(nullptr, 0, timer_thread, nullptr, 0, nullptr);
    if (!g_host.TimerThread)
        return HRESULT_FROM_WIN32(GetLastError());
    return S_OK;
}

static void destroy_timer(void)
{
    if (!g_host.TimerThread)
        return;
    g_timer_exit = TRUE;
    SetEvent(g_host.TimerEvent);
    WaitForSingleObject(g_host.TimerThread, 2000);
    CloseHandle(g_host.TimerThread);
    CloseHandle(g_host.TimerEvent);
    g_host.TimerThread = g_host.TimerEvent = nullptr;
}

HRESULT WshStartTimer(DWORD seconds)
{
    if (!g_host.TimerThread)
        return S_FALSE;
    g_timer_ms = seconds * 1000;
    SetEvent(g_host.TimerEvent);
    return S_OK;
}

void WshStopTimer(void)
{
    if (!g_host.TimerThread)
        return;
    g_timer_ms = 0;
    SetEvent(g_host.TimerEvent);
}

/* ------------------------------------------------------------------ the site */

static SCRIPT_ENGINE *engine_of_site(IActiveScriptSite *site)
{
    return (SCRIPT_ENGINE *)site;
}

static HRESULT STDMETHODCALLTYPE site_query_interface(IActiveScriptSite *This, REFIID riid, void **out)
{
    SCRIPT_ENGINE *engine = engine_of_site(This);
    if (!out)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IActiveScriptSite))
        *out = &engine->Site;
    else if (IsEqualIID(riid, &IID_IActiveScriptSiteWindow) && !g_host.Batch)
        *out = &engine->Window;
    else {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&engine->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE site_add_ref(IActiveScriptSite *This)
{
    return (ULONG)InterlockedIncrement(&engine_of_site(This)->Refs);
}

static void engine_free(SCRIPT_ENGINE *engine);

static ULONG STDMETHODCALLTYPE site_release(IActiveScriptSite *This)
{
    SCRIPT_ENGINE *engine = engine_of_site(This);
    LONG refs = InterlockedDecrement(&engine->Refs);
    if (!refs)
        engine_free(engine);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE site_get_lcid(IActiveScriptSite *This, LCID *lcid)
{
    if (!lcid)
        return E_POINTER;
    LCID user = GetUserDefaultLCID();
    *lcid = user == (LCID)-1 ? 1033 : user;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE site_get_item_info(IActiveScriptSite *This, LPCOLESTR name, DWORD mask, IUnknown **item, ITypeInfo **info)
{
    SCRIPT_ENGINE *engine = engine_of_site(This);
    if (!name)
        return E_POINTER;
    IUnknown *object = nullptr;
    if (!_wcsicmp(name, L"WScript") || !_wcsicmp(name, L"WSH"))
        object = (IUnknown *)WshHostDispatch(g_host.Object);
    else
        for (ITEM *entry = engine->Items; entry; entry = entry->Next)
            if (!_wcsicmp(entry->Name, name)) {
                object = entry->Object;
                break;
            }
    if (!object)
        return E_FAIL;
    if (item)
        *item = nullptr;
    if (info)
        *info = nullptr;
    HRESULT hr = S_OK;
    if (mask & SCRIPTINFO_ITYPEINFO) {
        IProvideClassInfo *provider;
        hr = object->lpVtbl->QueryInterface(object, &IID_IProvideClassInfo, (void **)&provider);
        if (SUCCEEDED(hr)) {
            hr = provider->lpVtbl->GetClassInfo(provider, info);
            provider->lpVtbl->Release(provider);
        }
        if (FAILED(hr) && object == (IUnknown *)WshHostDispatch(g_host.Object))
            return hr;
    }
    if (mask & SCRIPTINFO_IUNKNOWN) {
        object->lpVtbl->AddRef(object);
        if (item)
            *item = object;
        else
            object->lpVtbl->Release(object);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE site_get_doc_version(IActiveScriptSite *This, BSTR *version)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE site_on_terminate(IActiveScriptSite *This, const VARIANT *result, const EXCEPINFO *info)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE site_on_state_change(IActiveScriptSite *This, SCRIPTSTATE state)
{
    return S_OK;
}

static HRESULT script_position(IActiveScriptError *error, ULONG *line, LONG *column)
{
    DWORD64 context64 = 0;
    DWORD context = 0;
    IActiveScriptError *wide = nullptr;
    HRESULT hr = E_FAIL;
    /* the 64-bit interface has its own position call, the seventh slot of the table. */
    if (SUCCEEDED(error->lpVtbl->QueryInterface(error, &IID_IActiveScriptError64, (void **)&wide))) {
        typedef HRESULT(STDMETHODCALLTYPE * POSITION64)(IActiveScriptError *, DWORD64 *, ULONG *, LONG *);
        hr = ((POSITION64 *)wide->lpVtbl)[6](wide, &context64, line, column);
        wide->lpVtbl->Release(wide);
    }
    if (FAILED(hr))
        hr = error->lpVtbl->GetSourcePosition(error, &context, line, column);
    return hr;
}

/* the script ended with a call to the host: the function WScript_OnScriptTerminate of the script, when there is one. */
static void call_terminate(SCRIPT_ENGINE *engine)
{
    IDispatch *dispatch = nullptr;
    SCRIPTSTATE state;
    if (FAILED(engine->Script->lpVtbl->GetScriptState(engine->Script, &state)) ||
        FAILED(engine->Script->lpVtbl->GetScriptDispatch(engine->Script, nullptr, &dispatch)))
        return;
    DISPID id;
    PWSTR names[1] = {L"WScript_OnScriptTerminate"};
    if (SUCCEEDED(dispatch->lpVtbl->GetIDsOfNames(dispatch, &IID_NULL, names, 1, LOCALE_USER_DEFAULT, &id))) {
        DISPPARAMS none = {};
        VARIANT result;
        VariantInit(&result);
        dispatch->lpVtbl->Invoke(dispatch, id, &IID_NULL, LOCALE_USER_DEFAULT, DISPATCH_METHOD, &none, &result, nullptr, nullptr);
        VariantClear(&result);
    }
    dispatch->lpVtbl->Release(dispatch);
}

/* a script error: the interrupts of the host are not errors, the others go to the user with their position. */
static HRESULT STDMETHODCALLTYPE site_on_error(IActiveScriptSite *This, IActiveScriptError *error)
{
    SCRIPT_ENGINE *engine = engine_of_site(This);
    EXCEPINFO info = {};
    HRESULT hr = error->lpVtbl->GetExceptionInfo(error, &info);
    if (FAILED(hr))
        return hr;
    InterlockedExchange(&g_host.Pending, 0);
    hr = S_OK;
    BSTR text = nullptr;
    if (info.scode == WSH_E_TERMINATE) {
        call_terminate(engine);
    } else if (info.scode == WSH_E_QUIT) {
        hr = S_OK;
    } else if (info.scode == WSH_E_TIMEOUT) {
        WshOnTimeout();
    } else {
        BSTR description = info.bstrDescription, formatted = nullptr;
        g_host.ScriptFailed = TRUE;
        if (!description || !*description) {
            if (FAILED(WshFormatHResult(info.scode, &formatted)))
                goto done;
            description = formatted;
        }
        ULONG line = 0;
        LONG column = -1;
        if (FAILED(script_position(error, &line, &column))) {
            line = 0;
            column = -1;
        }
        /* the engines give the line of an error in the code, and none for an error that happened while it ran. */
        g_host.CompileError = SUCCEEDED(error->lpVtbl->GetSourceLineText(error, &text)) && text && *text;
        /* the file parser of Windows counts the columns of a script from one, which the host then counts again. */
        WshReportScriptError(line, column + 1 + (g_host.InWsf ? 1 : 0), info.bstrSource ? info.bstrSource : L"(null)", description, text, (DWORD)info.scode);
        SysFreeString(formatted);
    }
done:
    SysFreeString(info.bstrSource);
    SysFreeString(info.bstrDescription);
    SysFreeString(info.bstrHelpFile);
    SysFreeString(text);
    return hr;
}

static HRESULT STDMETHODCALLTYPE site_on_enter(IActiveScriptSite *This)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE site_on_leave(IActiveScriptSite *This)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE window_query_interface(IActiveScriptSiteWindow *This, REFIID riid, void **out)
{
    return site_query_interface((IActiveScriptSite *)((BYTE *)This - offsetof(SCRIPT_ENGINE, Window)), riid, out);
}

static ULONG STDMETHODCALLTYPE window_add_ref(IActiveScriptSiteWindow *This)
{
    return site_add_ref((IActiveScriptSite *)((BYTE *)This - offsetof(SCRIPT_ENGINE, Window)));
}

static ULONG STDMETHODCALLTYPE window_release(IActiveScriptSiteWindow *This)
{
    return site_release((IActiveScriptSite *)((BYTE *)This - offsetof(SCRIPT_ENGINE, Window)));
}

static HRESULT STDMETHODCALLTYPE window_get_window(IActiveScriptSiteWindow *This, HWND *window)
{
    if (!window)
        return E_POINTER;
    *window = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE window_enable_modeless(IActiveScriptSiteWindow *This, BOOL enable)
{
    return S_OK;
}

static const IActiveScriptSiteVtbl g_site_vtbl = {
    site_query_interface, site_add_ref, site_release, site_get_lcid, site_get_item_info, site_get_doc_version,
    site_on_terminate, site_on_state_change, site_on_error, site_on_enter, site_on_leave,
};

static const IActiveScriptSiteWindowVtbl g_window_vtbl = {
    window_query_interface, window_add_ref, window_release, window_get_window, window_enable_modeless,
};

/* ------------------------------------------------------------------ the engine */

static void engine_free(SCRIPT_ENGINE *engine)
{
    while (engine->Items) {
        ITEM *next = engine->Items->Next;
        HeapFree(GetProcessHeap(), 0, engine->Items->Name);
        if (engine->Items->Object)
            engine->Items->Object->lpVtbl->Release(engine->Items->Object);
        HeapFree(GetProcessHeap(), 0, engine->Items);
        engine->Items = next;
    }
    HeapFree(GetProcessHeap(), 0, engine);
}

/* the engine of a class id, or of a program id, which asks the registry for the class. It knows the host as its site. */
HRESULT WshCreateEngine(PCWSTR clsid_text, SCRIPT_ENGINE **out)
{
    SCRIPT_ENGINE *engine = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*engine));
    if (!engine)
        return E_OUTOFMEMORY;
    engine->Site.lpVtbl = &g_site_vtbl;
    engine->Window.lpVtbl = &g_window_vtbl;
    engine->Refs = 1;
    CLSID clsid;
    IUnknown *unknown = nullptr;
    HRESULT hr = CLSIDFromString(clsid_text, &clsid);
    if (SUCCEEDED(hr))
        hr = CoCreateInstance(&clsid, nullptr, CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER,
                              &IID_IUnknown, (void **)&unknown);
    if (SUCCEEDED(hr))
        hr = unknown->lpVtbl->QueryInterface(unknown, &IID_IActiveScript, (void **)&engine->Script);
    if (SUCCEEDED(hr))
        hr = unknown->lpVtbl->QueryInterface(unknown, sizeof(void *) == 8 ? &IID_IActiveScriptParse64 : &IID_IActiveScriptParse,
                                             (void **)&engine->Parse);
    BOOL sited = FALSE;
    if (SUCCEEDED(hr)) {
        hr = engine->Script->lpVtbl->SetScriptSite(engine->Script, &engine->Site);
        sited = SUCCEEDED(hr);
    }
    if (SUCCEEDED(hr))
        hr = engine->Parse->lpVtbl->InitNew(engine->Parse);
    if (SUCCEEDED(hr))
        hr = engine->Script->lpVtbl->AddNamedItem(engine->Script, L"WScript", SCRIPTITEM_ISVISIBLE);
    if (SUCCEEDED(hr))
        hr = engine->Script->lpVtbl->AddNamedItem(engine->Script, L"WSH", SCRIPTITEM_ISVISIBLE);
    if (SUCCEEDED(hr))
        hr = engine->Script->lpVtbl->SetScriptState(engine->Script, SCRIPTSTATE_INITIALIZED);
    /* with //D or //X the engine has to be one that a debugger can attach to. */
    if (SUCCEEDED(hr) && g_host.Debug) {
        IUnknown *debug;
        hr = unknown->lpVtbl->QueryInterface(unknown, sizeof(void *) == 8 ? &IID_IActiveScriptDebug64 : &IID_IActiveScriptDebug32,
                                             (void **)&debug);
        if (SUCCEEDED(hr))
            debug->lpVtbl->Release(debug);
    }
    if (unknown)
        unknown->lpVtbl->Release(unknown);
    if (FAILED(hr)) {
        if (sited)
            engine->Script->lpVtbl->Close(engine->Script);
        if (engine->Parse)
            engine->Parse->lpVtbl->Release(engine->Parse);
        if (engine->Script)
            engine->Script->lpVtbl->Release(engine->Script);
        engine_free(engine);
        return hr;
    }
    engine->Cookie = ++g_contexts;
    EnterCriticalSection(&g_host.Lock);
    engine->Next = g_engines;
    g_engines = engine;
    LeaveCriticalSection(&g_host.Lock);
    *out = engine;
    return S_OK;
}

/* an object of the script file that the engine looks up by name. */
HRESULT WshEngineAddItem(SCRIPT_ENGINE *engine, PCWSTR name, IUnknown *object, DWORD flags)
{
    ITEM *item = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*item));
    if (!item)
        return E_OUTOFMEMORY;
    item->Name = HeapAlloc(GetProcessHeap(), 0, (wcslen(name) + 1) * sizeof(WCHAR));
    if (!item->Name) {
        HeapFree(GetProcessHeap(), 0, item);
        return E_OUTOFMEMORY;
    }
    wcscpy(item->Name, name);
    item->Object = object;
    object->lpVtbl->AddRef(object);
    item->Flags = flags;
    item->Next = engine->Items;
    engine->Items = item;
    HRESULT hr = engine->Script->lpVtbl->AddNamedItem(engine->Script, name, flags);
    if (FAILED(hr)) {
        engine->Items = item->Next;
        object->lpVtbl->Release(object);
        HeapFree(GetProcessHeap(), 0, item->Name);
        HeapFree(GetProcessHeap(), 0, item);
    }
    return hr;
}

/* the code goes to the engine with the line it starts at, so errors name the line of the file. */
HRESULT WshEngineParse(SCRIPT_ENGINE *engine, BSTR code, ULONG first_line, DWORD flags)
{
    HRESULT hr = engine->Parse->lpVtbl->ParseScriptText(engine->Parse, code, nullptr, nullptr, nullptr, engine->Cookie, first_line, flags,
                                                         nullptr, nullptr);
    if (hr == (HRESULT)SCRIPT_E_REPORTED)
        return WSH_E_REPORTED;
    return hr;
}

HRESULT WshEngineRun(SCRIPT_ENGINE *engine)
{
    HRESULT hr = engine->Script->lpVtbl->SetScriptState(engine->Script, SCRIPTSTATE_STARTED);
    if (hr == (HRESULT)SCRIPT_E_REPORTED)
        return WSH_E_REPORTED;
    return FAILED(hr) ? hr : S_OK;
}

void WshEngineClose(SCRIPT_ENGINE *engine)
{
    if (engine->Closed)
        return;
    EnterCriticalSection(&g_host.Lock);
    engine->Closed = TRUE;
    SCRIPT_ENGINE **link = &g_engines;
    while (*link && *link != engine)
        link = &(*link)->Next;
    if (*link)
        *link = engine->Next;
    LeaveCriticalSection(&g_host.Lock);
    engine->Script->lpVtbl->Close(engine->Script);
    engine->Parse->lpVtbl->Release(engine->Parse);
    engine->Script->lpVtbl->Release(engine->Script);
    engine->Parse = nullptr;
    engine->Script = nullptr;
    engine->Site.lpVtbl->Release(&engine->Site);
}

/* ------------------------------------------------------------------ the engine of a file */

/* a registry value: the default value of a key of the classes, as text. */
static HRESULT class_default(PCWSTR key_name, WCHAR *out, int capacity)
{
    HKEY key;
    LONG error = RegOpenKeyExW(HKEY_CLASSES_ROOT, key_name, 0, KEY_READ, &key);
    if (error)
        return error > 0 ? HRESULT_FROM_WIN32(error) : error;
    DWORD type = 0, size = (DWORD)capacity * sizeof(WCHAR);
    error = RegQueryValueExW(key, nullptr, nullptr, &type, (PBYTE)out, &size);
    RegCloseKey(key);
    if (error)
        return HRESULT_FROM_WIN32(error);
    if ((type != REG_SZ && type != REG_EXPAND_SZ) || size < sizeof(WCHAR) || out[size / sizeof(WCHAR) - 1])
        return (HRESULT)0x80040153;
    return S_OK;
}

/* the engine named by the extension of a script file: the file type of the extension names the engine in its ScriptEngine. */
HRESULT WshEngineNameOf(PCWSTR extension, WCHAR *out, int capacity)
{
    WCHAR progid[512];
    HRESULT hr = class_default(extension, progid, 256);
    if (FAILED(hr))
        return hr;
    wcscat(progid, L"\\ScriptEngine");
    return class_default(progid, out, capacity);
}

/* ------------------------------------------------------------------ the source */

/* the text of a file as a string. An even size that looks like UTF-16 is UTF-16 with the byte order of its mark or of its
   control characters, anything else is in the code page of the system or of //CP. A final Ctrl-Z ends the last line. */
HRESULT WshConvertText(UINT page, const BYTE *data, UINT size, BSTR *text)
{
    *text = nullptr;
    if (!size) {
        *text = SysAllocString(L"");
        return *text ? S_OK : E_OUTOFMEMORY;
    }
    int order = 0;
    if (!(size & 1)) {
        int result = 494;
        IsTextUnicode(data, (int)size, &result);
        if (result & 0x100)
            order = 0;
        else if (result & 8) {
            data += 2;
            size -= 2;
            order = 1;
        } else if (result & 0x80) {
            data += 2;
            size -= 2;
            order = 2;
        } else if (result & 4)
            order = 1;
        else if (result & 0x40)
            order = 2;
        else if (result & 2)
            order = 1;
        else
            order = (result >> 4) & 2;
    }
    UINT length;
    if (order)
        length = size >> 1;
    else {
        length = MultiByteToWideChar(page, 0, (PCSTR)data, (int)size, nullptr, 0);
        if (!length)
            return HRESULT_FROM_WIN32(GetLastError());
    }
    *text = SysAllocStringLen(nullptr, length);
    if (!*text)
        return E_OUTOFMEMORY;
    if (order == 1)
        memcpy(*text, data, length * sizeof(WCHAR));
    else if (order == 2) {
        for (UINT i = 0; i < length; i++)
            (*text)[i] = (WCHAR)(data[i * 2] << 8 | data[i * 2 + 1]);
    } else if (!MultiByteToWideChar(page, 0, (PCSTR)data, (int)size, *text, (int)length)) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        SysFreeString(*text);
        *text = nullptr;
        return hr;
    }
    if (length && (*text)[length - 1] == 26)
        (*text)[length - 1] = 10;
    return S_OK;
}

HRESULT WshLoadSource(PCWSTR path, BSTR *text)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());
    LARGE_INTEGER size;
    BYTE *data = nullptr;
    HRESULT hr = S_OK;
    if (!GetFileSizeEx(file, &size) || size.QuadPart > 0x7FFFFFF0)
        hr = HRESULT_FROM_WIN32(GetLastError() ? GetLastError() : ERROR_FILE_TOO_LARGE);
    else {
        data = HeapAlloc(GetProcessHeap(), 0, (SIZE_T)size.QuadPart + 4);
        DWORD read = 0;
        if (!data)
            hr = E_OUTOFMEMORY;
        else if (size.QuadPart && !ReadFile(file, data, (DWORD)size.QuadPart, &read, nullptr))
            hr = HRESULT_FROM_WIN32(GetLastError());
        else
            size.QuadPart = read;
    }
    CloseHandle(file);
    if (FAILED(hr)) {
        HeapFree(GetProcessHeap(), 0, data);
        return hr;
    }
    hr = WshConvertText(g_host.CodePage ? g_host.CodePage : CP_ACP, data, (UINT)size.QuadPart, text);
    HeapFree(GetProcessHeap(), 0, data);
    return hr;
}

/* ------------------------------------------------------------------ one script file */

HRESULT WshReportLoadError(HRESULT code)
{
    if (code == WSH_E_REPORTED || code == (HRESULT)SCRIPT_E_REPORTED)
        return WSH_E_REPORTED;
    if (code == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || code == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND)) {
        WshPrintF(RES_BASE + 21, 3310, g_host.ScriptPath);
        return WSH_E_REPORTED;
    }
    BSTR reason = nullptr;
    WshFormatHResult(code, &reason);
    WshPrintF(RES_BASE + 20, 3204, g_host.ScriptPath, reason);
    SysFreeString(reason);
    return WSH_E_REPORTED;
}

static HRESULT report_failure(HRESULT hr)
{
    if (hr == WSH_E_REPORTED || hr == (HRESULT)SCRIPT_E_REPORTED)
        return WSH_E_REPORTED;
    return WshReportError(RES_BASE + 20, 3206, hr);
}

static HRESULT run_standard_script(SCRIPT_ENGINE *engine, BSTR text)
{
    if (g_host.TimeoutSet)
        WshStartTimer(g_host.Timeout);
    HRESULT hr = WshEngineParse(engine, text, 1, SCRIPTTEXT_HOSTMANAGESSOURCE | SCRIPTTEXT_ISVISIBLE);
    if (SUCCEEDED(hr))
        hr = WshEngineRun(engine);
    WshDisconnectAll();
    WshEngineClose(engine);
    g_host.Engine = nullptr;
    WshStopTimer();
    if (InterlockedCompareExchange(&g_host.Pending, 0, 0) == WSH_E_TIMEOUT)
        WshOnTimeout();
    return hr;
}

/* the extension of a file name, with its dot. */
static PCWSTR extension_of(PCWSTR name)
{
    PCWSTR dot = nullptr;
    for (; *name; name++) {
        if (*name == L'\\' || *name == L'/')
            dot = nullptr;
        else if (*name == L'.')
            dot = name;
    }
    return dot;
}

HRESULT WshExecute(PCWSTR file, int count, PWSTR *arguments, PCWSTR engine_name, PCWSTR job)
{
    HRESULT hr;
    WCHAR full[MAX_PATH], engine_text[256];
    PWSTR part = nullptr;
    BSTR text = nullptr;
    SCRIPT_ENGINE *engine = nullptr;
    g_host.Pending = 0;
    g_host.ScriptFailed = FALSE;
    g_host.InWsf = FALSE;
    hr = WshCreateHostObject(count, arguments, &g_host.Object);
    if (FAILED(hr))
        return WshReportError(RES_BASE + 20, 3200, hr);
    hr = create_timer();
    if (FAILED(hr))
        return WshReportError(RES_BASE + 20, 3200, hr);
    if (!GetFullPathNameW(file, MAX_PATH, full, &part))
        return WshReportError(RES_BASE + 20, 3200, HRESULT_FROM_WIN32(GetLastError()));
    SysFreeString(g_host.ScriptPath);
    g_host.ScriptPath = SysAllocString(full);
    if (!g_host.ScriptPath)
        return report_failure(E_OUTOFMEMORY);
    PCWSTR ext = part ? extension_of(part) : nullptr;
    if (engine_name) {
        SIZE_T n = wcslen(engine_name);
        if (n >= RTL_NUMBER_OF(engine_text))
            n = RTL_NUMBER_OF(engine_text) - 1;
        memcpy(engine_text, engine_name, n * sizeof(WCHAR));
        engine_text[n] = 0;
        hr = S_OK;
    } else if (!ext) {
        WshPrintF(RES_BASE + 21, 3306, g_host.ScriptPath);
        return WSH_E_REPORTED;
    } else
        hr = WshEngineNameOf(ext, engine_text, RTL_NUMBER_OF(engine_text));
    if (SUCCEEDED(hr)) {
        static const GUID chakra = {0x1B7CD997, 0xE5FF, 0x4932, {0xA7, 0xA6, 0x2A, 0x9E, 0x63, 0x6D, 0xA3, 0x85}};
        CLSID clsid;
        if (SUCCEEDED(CLSIDFromString(engine_text, &clsid)) && IsEqualGUID(&clsid, &chakra)) {
            WshPrintF(0, 3216, L"", L"");
            return WSH_E_REPORTED;
        }
        hr = WshCreateEngine(engine_text, &engine);
        if (FAILED(hr)) {
            WshPrintF(RES_BASE + 20, 3201, g_host.ScriptPath, engine_text);
            hr = WSH_E_REPORTED;
        }
    }
    if (FAILED(hr)) {
        if (hr == WSH_E_REPORTED)
            return hr;
        if (ext && !_wcsicmp(ext, L".wsf"))
            return WshRunWsf(g_host.ScriptPath, job, count, arguments);
        if (ext)
            WshPrintF(RES_BASE + 21, 3309, ext);
        else
            WshPrintF(RES_BASE + 21, 3306, g_host.ScriptPath);
        return WSH_E_REPORTED;
    }
    hr = WshLoadSource(g_host.ScriptPath, &text);
    if (FAILED(hr)) {
        if (hr == (HRESULT)0x800C0005)
            hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        WshEngineClose(engine);
        hr = WshReportLoadError(hr);
        return hr;
    }
    g_host.Engine = engine;
    hr = run_standard_script(engine, text);
    SysFreeString(text);
    if (FAILED(hr) && hr != WSH_E_REPORTED)
        hr = report_failure(hr);
    return hr;
}

/* ------------------------------------------------------------------ the run */

static HRESULT module_version(void)
{
    DWORD handle = 0, size = GetFileVersionInfoSizeW(g_host.Module, &handle);
    if (!size)
        return HRESULT_FROM_WIN32(GetLastError());
    PBYTE block = HeapAlloc(GetProcessHeap(), 0, size);
    if (!block)
        return E_OUTOFMEMORY;
    VS_FIXEDFILEINFO *info = nullptr;
    UINT length = 0;
    HRESULT hr = S_OK;
    if (!GetFileVersionInfoW(g_host.Module, 0, size, block) || !VerQueryValueW(block, L"\\", (LPVOID *)&info, &length))
        hr = HRESULT_FROM_WIN32(GetLastError());
    else {
        g_host.VersionMajor = HIWORD(info->dwProductVersionMS);
        g_host.VersionMinor = LOWORD(info->dwProductVersionMS);
        g_host.VersionBuild = HIWORD(info->dwProductVersionLS);
        g_host.VersionRevision = LOWORD(info->dwProductVersionLS);
    }
    HeapFree(GetProcessHeap(), 0, block);
    return hr;
}

/* the directory of the program: everything before its last backslash, or a dot when it is the first character. */
static void module_directory(void)
{
    PCWSTR last = nullptr;
    for (PCWSTR p = g_host.Module; *p; p++)
        if (*p == L'\\' || *p == L'/')
            last = p;
    if (last == g_host.Module) {
        wcscpy(g_host.ModuleDirectory, L".");
        return;
    }
    SIZE_T length = last ? (SIZE_T)(last - g_host.Module) : 0;
    memcpy(g_host.ModuleDirectory, g_host.Module, length * sizeof(WCHAR));
    g_host.ModuleDirectory[length] = 0;
}

static HRESULT apply_settings(const SETTINGS *s)
{
    g_host.Batch = s->Batch;
    g_host.Logo = s->Logo;
    g_host.Debug = s->Debug;
    g_host.Unicode = s->Unicode;
    /* the code page of //CP is checked and never used, scripts are read in the ANSI code page whatever it says. */
    g_host.CodePage = 0;
    g_host.TimeoutSet = s->TimeoutSet;
    g_host.Timeout = s->Timeout;
    return S_OK;
}

static int run_main(void)
{
    SETTINGS settings = {.Logo = TRUE};
    HRESULT hr;
    g_host.Logo = TRUE;
    if (!GetModuleFileNameW(g_host.Instance, g_host.Module, MAX_PATH) || GetLastError() == ERROR_INSUFFICIENT_BUFFER)
        return 1;
    hr = module_version();
    if (FAILED(hr)) {
        WshPrintF(RES_BASE + 20, 3200, hr);
        return 1;
    }
    module_directory();
    PCWSTR command_line = GetCommandLineW();
    SIZE_T length = wcslen(command_line) + 1;
    PWSTR line = HeapAlloc(GetProcessHeap(), 0, length * sizeof(WCHAR));
    if (!line)
        return 1;
    wcscpy(line, command_line);
    int total = WshSplitCommandLine(line, nullptr);
    PWSTR *words = HeapAlloc(GetProcessHeap(), 0, (total + 1) * sizeof(PWSTR));
    if (!words)
        return 1;
    WshSplitCommandLine(line, words);
    words[total] = nullptr;
    int count = total > 0 ? total - 1 : 0;
    PWSTR *arguments = words + 1;
    if (count == 0)
        return WshRunWithoutArgument();
    hr = WshLoadRegistrySettings(HKEY_LOCAL_MACHINE, &settings);
    if (SUCCEEDED(hr))
        hr = WshLoadRegistrySettings(HKEY_CURRENT_USER, &settings);
    if (FAILED(hr)) {
        WshPrintF(RES_BASE + 20, 3205, hr);
        return 1;
    }
    hr = WshParseOptions(&settings, &count, arguments);
    if (FAILED(hr)) {
        if (hr != WSH_E_REPORTED)
            WshPrintF(RES_BASE + 20, 3207, hr);
        return 1;
    }
    WCHAR script[MAX_PATH] = L"";
    if (count) {
        if (wcslen(arguments[0]) >= MAX_PATH) {
            if (settings.Logo)
                WshShowLogo();
            WshPrintF(RES_BASE + 21, 3311, arguments[0]);
            return 1;
        }
        wcscpy(script, arguments[0]);
        PCWSTR dot = nullptr;
        for (PCWSTR p = script; *p; p++)
            if (*p == L'.')
                dot = p;
        if (dot && !_wcsicmp(dot + 1, L"WSH")) {
            WCHAR from_file[MAX_PATH];
            hr = WshLoadFileSettings(script, &settings, from_file, MAX_PATH);
            if (FAILED(hr) || hr == 1) {
                if (settings.Logo)
                    WshShowLogo();
                if (hr == 1)
                    WshPrintF(RES_BASE + 21, 3400, script);
                else if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
                    WshPrintF(RES_BASE + 21, 3310, script);
                else {
                    BSTR reason = nullptr;
                    WshFormatHResult(hr, &reason);
                    WshPrintF(RES_BASE + 20, 3204, script, reason);
                    SysFreeString(reason);
                }
                return 1;
            }
            wcscpy(script, from_file);
        }
    }
    apply_settings(&settings);
    if (settings.Help) {
        g_host.Batch = FALSE;
        if (settings.Logo)
            WshShowLogo();
        WshShowUsage();
        return 0;
    }
    if (settings.Logo)
        WshShowLogo();
    BOOL done = FALSE;
    if (settings.SaveAlone) {
        /* the settings go back to the defaults and then the default host is changed with a value that is none of the two,
           which fails, so the message that says they were reset is never the one that is told. */
        SETTINGS defaults = {.Logo = TRUE};
        WshSaveRegistrySettings(&defaults);
        WshPrintF(RES_BASE + 20, 3203);
        return 1;
    }
    if (settings.Save) {
        hr = WshSaveRegistrySettings(&settings);
        if (FAILED(hr)) {
            WshPrintF(RES_BASE + 20, 3203);
            return 1;
        }
        WshPrintF(0, 3051);
        done = TRUE;
    }
    if (settings.ChangeHost) {
        hr = WshChangeDefaultHost(settings.Host);
        if (FAILED(hr)) {
            WshPrintF(RES_BASE + 20, 3202);
            return 1;
        }
        if (!count)
            WshPrintF(0, settings.Host == 'C' ? 3053 : 3052);
        done = TRUE;
    }
    if (!count) {
        if (done)
            return 0;
        WshPrintF(RES_BASE + 21, 3304);
        return 1;
    }
    if (!script[0]) {
        WshPrintF(RES_BASE + 21, 3304);
        return 1;
    }
    int jobs = settings.JobCount ? settings.JobCount : 1;
    for (int i = 0; i < jobs; i++) {
        hr = WshExecute(script, count - 1, arguments + 1, settings.Engine, settings.JobCount ? settings.Jobs[i] : nullptr);
        if (FAILED(hr)) {
            if (!g_host.Quit)
                g_host.ExitCode = 1;
            if (hr != WSH_E_REPORTED)
                WshPrintF(RES_BASE + 20, 3207);
        }
    }
    if (g_host.Object) {
        WshFreeHostObject(g_host.Object);
        g_host.Object = nullptr;
    }
    destroy_timer();
    return g_host.ExitCode;
}

void entry(void)
{
    int code = 1;
    g_host.Instance = GetModuleHandleW(nullptr);
    g_host.MainThread = GetCurrentThreadId();
    InitializeCriticalSection(&g_host.Lock);
    if (SUCCEEDED(CoInitialize(nullptr))) {
        if (!WshSecurityEnabled()) {
            WshPrintF(RES_BASE + 20, 3208);
        } else {
            CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, 0, nullptr);
            code = run_main();
        }
        CoUninitialize();
    }
    ExitProcess((UINT)code);
}
