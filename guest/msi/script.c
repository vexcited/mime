#include "automation.h"
#include <mime/win32/activscp.h>

/*
 * script custom actions, the rows of the CustomAction table whose type is 5 or 6. a script runs as windows runs
 * one: an active scripting engine is made from its class id and given one named item, Session, the automation
 * object of the installation. the script text is parsed, the engine started, and the entry point called through
 * the dispatch of the script, with no arguments. whatever the function returns is the result of the action.
 */

static const GUID CLSID_VBScriptEngine = {0xB54F3741, 0x5B07, 0x11CF, {0xA4, 0xB0, 0x00, 0xAA, 0x00, 0x4A, 0x55, 0xE8}};
static const GUID CLSID_JScriptEngine = {0xF414C260, 0x6AC0, 0x11CF, {0xB6, 0xD1, 0x00, 0xAA, 0x00, 0xBB, 0xBB, 0x58}};
static const GUID IID_ActiveScript = {0xBB1A2AE1, 0xA4F9, 0x11CF, {0x8F, 0x20, 0x00, 0x80, 0x5F, 0x2C, 0xD0, 0x64}};
static const GUID IID_ActiveScriptParse32 = {0xBB1A2AE2, 0xA4F9, 0x11CF, {0x8F, 0x20, 0x00, 0x80, 0x5F, 0x2C, 0xD0, 0x64}};
static const GUID IID_ActiveScriptParse64 = {0xC7EF7658, 0xE1EE, 0x480E, {0x97, 0xEA, 0xD5, 0x2C, 0xB4, 0xD7, 0x6D, 0x17}};
static const GUID IID_ActiveScriptSiteN = {0xDB01A1E3, 0xA42B, 0x11CF, {0x8F, 0x20, 0x00, 0x80, 0x5F, 0x2C, 0xD0, 0x64}};
static const GUID IID_ActiveScriptSiteWindowN = {0xD10F6761, 0x83E9, 0x11CF, {0x8F, 0x20, 0x00, 0x80, 0x5F, 0x2C, 0xD0, 0x64}};

typedef struct SCRIPT_SITE {
    IActiveScriptSite Site;
    IActiveScriptSiteWindow Window;
    LONG References;
    IDispatch *Session;
    LANGID Language;
    SCRIPTSTATE State;
    /* the last error the engine reported. */
    HRESULT Error;
    BSTR Source, Description, LineText;
    ULONG Line, Column;
} SCRIPT_SITE;

#define SITE_OF(window) ((SCRIPT_SITE *)((BYTE *)(window) - offsetof(SCRIPT_SITE, Window)))

static void clear_error(SCRIPT_SITE *site)
{
    SysFreeString(site->Source);
    SysFreeString(site->Description);
    SysFreeString(site->LineText);
    site->Source = site->Description = site->LineText = nullptr;
    site->Error = S_OK;
    site->Line = site->Column = 0;
}

static HRESULT STDMETHODCALLTYPE site_query_interface(IActiveScriptSite *self, REFIID iid, void **out)
{
    SCRIPT_SITE *site = (SCRIPT_SITE *)self;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_ActiveScriptSiteN))
        *out = &site->Site;
    else if (IsEqualGUID(iid, &IID_ActiveScriptSiteWindowN))
        *out = &site->Window;
    else {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&site->References);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE site_add_ref(IActiveScriptSite *self)
{
    return InterlockedIncrement(&((SCRIPT_SITE *)self)->References);
}

static ULONG STDMETHODCALLTYPE site_release(IActiveScriptSite *self)
{
    SCRIPT_SITE *site = (SCRIPT_SITE *)self;
    LONG references = InterlockedDecrement(&site->References);
    if (!references) {
        clear_error(site);
        if (site->Session)
            site->Session->lpVtbl->Release(site->Session);
        MimeMsiFree(site);
    }
    return (ULONG)references;
}

static HRESULT STDMETHODCALLTYPE site_get_lcid(IActiveScriptSite *self, LCID *lcid)
{
    *lcid = ((SCRIPT_SITE *)self)->Language;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE site_get_item_info(IActiveScriptSite *self, LPCOLESTR name, DWORD mask, IUnknown **item,
                                                    ITypeInfo **info)
{
    SCRIPT_SITE *site = (SCRIPT_SITE *)self;
    if (_wcsicmp(name, L"Session"))
        return TYPE_E_ELEMENTNOTFOUND;
    if (mask & SCRIPTINFO_ITYPEINFO)
        return E_NOTIMPL;
    if (mask & SCRIPTINFO_IUNKNOWN) {
        if (!item)
            return E_POINTER;
        *item = (IUnknown *)site->Session;
        site->Session->lpVtbl->AddRef(site->Session);
    }
    if (info)
        *info = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE site_get_doc_version(IActiveScriptSite *self, BSTR *version)
{
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE site_on_terminate(IActiveScriptSite *self, const VARIANT *result, const EXCEPINFO *info)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE site_on_state_change(IActiveScriptSite *self, SCRIPTSTATE state)
{
    ((SCRIPT_SITE *)self)->State = state;
    return S_OK;
}

/* keeps the exception, the position and the line of the error. */
static HRESULT STDMETHODCALLTYPE site_on_error(IActiveScriptSite *self, IActiveScriptError *error)
{
    SCRIPT_SITE *site = (SCRIPT_SITE *)self;
    clear_error(site);
    if (!error)
        return S_OK;
    EXCEPINFO info = {};
    if (error->lpVtbl->GetExceptionInfo(error, &info) != S_OK) {
        site->Error = E_ABORT;
    } else {
        site->Error = info.scode ? info.scode : (HRESULT)0x80020009;
        site->Source = info.bstrSource;
        site->Description = info.bstrDescription;
        SysFreeString(info.bstrHelpFile);
    }
    BSTR text = nullptr;
    error->lpVtbl->GetSourceLineText(error, &text);
    site->LineText = text;
    DWORD_PTR cookie = 0;
    ULONG line = 0;
    LONG column = 0;
    if (error->lpVtbl->GetSourcePosition(error, (DWORD *)&cookie, &line, &column) == S_OK) {
        site->Line = line + 1;
        site->Column = (ULONG)column + 1;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE site_on_enter(IActiveScriptSite *self)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE site_on_leave(IActiveScriptSite *self)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE window_query_interface(IActiveScriptSiteWindow *self, REFIID iid, void **out)
{
    return site_query_interface(&SITE_OF(self)->Site, iid, out);
}

static ULONG STDMETHODCALLTYPE window_add_ref(IActiveScriptSiteWindow *self)
{
    return site_add_ref(&SITE_OF(self)->Site);
}

static ULONG STDMETHODCALLTYPE window_release(IActiveScriptSiteWindow *self)
{
    return site_release(&SITE_OF(self)->Site);
}

static HRESULT STDMETHODCALLTYPE window_get_window(IActiveScriptSiteWindow *self, HWND *window)
{
    *window = nullptr;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE window_enable_modeless(IActiveScriptSiteWindow *self, BOOL enable)
{
    return S_OK;
}

static const IActiveScriptSiteVtbl site_vtbl = {
    site_query_interface, site_add_ref, site_release, site_get_lcid, site_get_item_info, site_get_doc_version,
    site_on_terminate, site_on_state_change, site_on_error, site_on_enter, site_on_leave,
};

static const IActiveScriptSiteWindowVtbl window_vtbl = {
    window_query_interface, window_add_ref, window_release, window_get_window, window_enable_modeless,
};

/* the error record of a script that did not run, with what the engine reported. */
static void post_error(MSIHANDLE session, UINT number, PCWSTR action, SCRIPT_SITE *site)
{
    MSIHANDLE record = MsiCreateRecord(8);
    if (!record)
        return;
    MsiRecordSetStringW(record, 0, number == 1720 ?
        L"Error [1]. There is a problem with this Windows Installer package. A script required for this install to "
        L"complete could not be run. Contact your support personnel or package vendor. Custom action [2] script error "
        L"[3], [4]: [5] Line [6], Column [7], [8]" :
        number == 2738 ? L"Error [1]. Could not access VBScript run time for custom action [2]." :
        L"Error [1]. Could not access JScript run time for custom action [2].");
    MsiRecordSetInteger(record, 1, (int)number);
    MsiRecordSetStringW(record, 2, action ? action : L"");
    if (site && site->Error) {
        MsiRecordSetInteger(record, 3, (int)site->Error);
        MsiRecordSetStringW(record, 4, site->Source ? site->Source : L"");
        MsiRecordSetStringW(record, 5, site->Description ? site->Description : L"");
        MsiRecordSetInteger(record, 6, (int)site->Line);
        MsiRecordSetInteger(record, 7, (int)site->Column);
        MsiRecordSetStringW(record, 8, site->LineText ? site->LineText : L"");
    }
    MsiProcessMessage(session, INSTALLMESSAGE_ERROR, record);
    MsiCloseHandle(record);
}

/* a registration of the engine below the current user would let a user's script engine run in the installer. */
static BOOL user_registration_exists(const GUID *clsid)
{
    WCHAR text[40], path[100];
    if (StringFromGUID2(clsid, text, 40) <= 0)
        return FALSE;
    _snwprintf(path, RTL_NUMBER_OF(path), L"Software\\Classes\\CLSID\\%s", text);
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ, &key))
        return FALSE;
    RegCloseKey(key);
    return TRUE;
}

/*
 * runs the script. the session handle is the automation object's from here on. the result is what the function
 * returned as a number, 0 when it returned none. the return is a Windows error for an action that could not run,
 * after the reason went out as a message.
 */
UINT MimeMsiRunActionScript(MSIHANDLE session, BOOL jscript, PCWSTR text, PCWSTR function, PCWSTR action, UINT *result)
{
    *result = 0;
    const GUID *clsid = jscript ? &CLSID_JScriptEngine : &CLSID_VBScriptEngine;
    UINT unavailable = jscript ? 2739 : 2738;
    AUTO_OBJECT *installer = AutoNewInstaller();
    AUTO_OBJECT *object = installer ? AutoNewSession(session, installer) : nullptr;
    if (installer)
        AutoRelease(installer);
    if (!object) {
        if (!installer)
            MsiCloseHandle(session);
        return ERROR_INSTALL_FAILURE;
    }
    UINT error = ERROR_SUCCESS;
    SCRIPT_SITE *site = nullptr;
    IActiveScript *engine = nullptr;
    IActiveScriptParse *parse = nullptr;
    if (user_registration_exists(clsid)) {
        post_error(session, unavailable, action, nullptr);
        error = ERROR_INSTALL_FAILURE;
        goto done;
    }
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, &IID_ActiveScript, (void **)&engine);
    BOOL initialized = FALSE;
    if (hr == CO_E_NOTINITIALIZED) {
        CoInitialize(nullptr);
        initialized = TRUE;
        hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, &IID_ActiveScript, (void **)&engine);
    }
    if (FAILED(hr)) {
        post_error(session, unavailable, action, nullptr);
        error = ERROR_INSTALL_FAILURE;
        goto uninitialize;
    }
    site = MimeMsiAllocate(sizeof(*site));
    if (!site) {
        error = ERROR_NOT_ENOUGH_MEMORY;
        goto uninitialize;
    }
    site->Site.lpVtbl = &site_vtbl;
    site->Window.lpVtbl = &window_vtbl;
    site->References = 1;
    site->Session = (IDispatch *)object;
    ((IDispatch *)object)->lpVtbl->AddRef((IDispatch *)object);
    site->Language = GetUserDefaultLangID();
    hr = engine->lpVtbl->QueryInterface(engine, sizeof(void *) == 8 ? &IID_ActiveScriptParse64 : &IID_ActiveScriptParse32,
                                        (void **)&parse);
    if (SUCCEEDED(hr))
        hr = engine->lpVtbl->SetScriptSite(engine, &site->Site);
    if (SUCCEEDED(hr))
        hr = parse->lpVtbl->InitNew(parse);
    if (SUCCEEDED(hr))
        hr = engine->lpVtbl->AddNamedItem(engine, L"Session", SCRIPTITEM_ISVISIBLE | SCRIPTITEM_ISSOURCE);
    if (FAILED(hr)) {
        post_error(session, unavailable, action, nullptr);
        error = ERROR_INSTALL_FAILURE;
        goto close;
    }
    hr = parse->lpVtbl->ParseScriptText(parse, text, L"Session", nullptr, nullptr, 0, 0, 0, nullptr, nullptr);
    if (SUCCEEDED(hr) && site->State != SCRIPTSTATE_STARTED) {
        site->Error = S_OK;
        hr = engine->lpVtbl->SetScriptState(engine, SCRIPTSTATE_CONNECTED);
        if (SUCCEEDED(hr))
            hr = site->Error;
    }
    if (SUCCEEDED(hr) && function && *function) {
        IDispatch *dispatch = nullptr;
        if (site->State != SCRIPTSTATE_CONNECTED) {
            hr = E_UNEXPECTED;
        } else if (SUCCEEDED(hr = engine->lpVtbl->GetScriptDispatch(engine, L"Session", &dispatch))) {
            DISPID id;
            LPOLESTR names[1] = {(LPOLESTR)function};
            if (FAILED(dispatch->lpVtbl->GetIDsOfNames(dispatch, &IID_NULL, names, 1, 0, &id)))
                hr = dispatch->lpVtbl->GetIDsOfNames(dispatch, &IID_NULL, names, 1, site->Language, &id);
            else
                hr = S_OK;
            if (SUCCEEDED(hr)) {
                DISPPARAMS params = {};
                VARIANT value;
                VariantInit(&value);
                site->Error = S_OK;
                HRESULT invoke = dispatch->lpVtbl->Invoke(dispatch, id, &IID_NULL, site->Language, DISPATCH_METHOD,
                                                          &params, &value, nullptr, nullptr);
                hr = site->Error ? site->Error : invoke;
                if (value.vt != VT_EMPTY) {
                    if (VariantChangeType(&value, &value, 0, VT_I4) == S_OK)
                        *result = (UINT)value.lVal;
                    else
                        *result = 0;
                }
                VariantClear(&value);
            }
            dispatch->lpVtbl->Release(dispatch);
        }
    }
    if (FAILED(hr)) {
        post_error(session, 1720, action, site);
        *result = 0;
        error = ERROR_INSTALL_FAILURE;
    } else if (*result >= 4 && *result - 4 >= 2) {
        /* the values a script may return are those of the dialog buttons. */
        *result = 3;
    }
close:
    engine->lpVtbl->Close(engine);
uninitialize:
    if (parse)
        parse->lpVtbl->Release(parse);
    if (engine)
        engine->lpVtbl->Release(engine);
    if (site)
        site_release(&site->Site);
    if (initialized)
        CoUninitialize();
done:
    AutoRelease(object);
    return error;
}
