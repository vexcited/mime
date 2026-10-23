/*
 * stateless msctf.dll entry points: input scopes, which the host's input method ignores, and the language bar and
 * ctfmon helpers, which mime does not have.
 */
#include "msctf_private.h"

HRESULT WINAPI DllRegisterServer(void)
{
    return S_OK;
}

HRESULT WINAPI DllUnregisterServer(void)
{
    return S_OK;
}

HRESULT WINAPI SetInputScope(HWND hwnd, INT scope)
{
    return IsWindow(hwnd) ? S_OK : E_INVALIDARG;
}

HRESULT WINAPI SetInputScopes(HWND hwnd, const INT *scopes, UINT count, WCHAR **phrases, UINT phrase_count,
                              WCHAR *regexp, WCHAR *srgs)
{
    if (!IsWindow(hwnd) || (count && !scopes))
        return E_INVALIDARG;
    return S_OK;
}

HRESULT WINAPI SetInputScopeXML(HWND hwnd, WCHAR *xml)
{
    return IsWindow(hwnd) ? S_OK : E_INVALIDARG;
}

HRESULT WINAPI TF_CreateLangBarMgr(void **lang_bar_mgr)
{
    if (!lang_bar_mgr)
        return E_INVALIDARG;
    *lang_bar_mgr = nullptr;
    return E_NOTIMPL;
}

HRESULT WINAPI TF_CreateLangBarItemMgr(void **item_mgr)
{
    if (!item_mgr)
        return E_INVALIDARG;
    *item_mgr = nullptr;
    return E_NOTIMPL;
}

HRESULT WINAPI TF_InitSystem(void)
{
    return S_OK;
}

HRESULT WINAPI TF_UninitSystem(void)
{
    return S_OK;
}

BOOL WINAPI TF_IsCtfmonRunning(void)
{
    return FALSE;
}

BOOL WINAPI TF_IsLanguageBarEnabled(void)
{
    return FALSE;
}
