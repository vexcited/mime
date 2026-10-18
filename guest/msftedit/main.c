/*
 * msftedit.dll, rich edit 4.1 and later: the class RICHEDIT50W over the
 * shared engine, with friendly hyperlinks from rtf fields. windows'
 * msftedit registers RICHEDIT50W alone. the math, custom text out and
 * windowless entries report E_NOTIMPL.
 */
#include <richedit/richedit_private.h>
#include <mime/win32/commctrl.h>

static HINSTANCE g_module;

LRESULT WINAPI RichEditWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return ReWndProc(hwnd, msg, wparam, lparam, FALSE, FALSE, TRUE);
}

LRESULT WINAPI RichEditANSIWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return ReWndProc(hwnd, msg, wparam, lparam, TRUE, FALSE, TRUE);
}

LRESULT WINAPI RichEdit10ANSIWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return ReWndProc(hwnd, msg, wparam, lparam, TRUE, TRUE, TRUE);
}

void WINAPI DisableOleinitCheck(void)
{
}

HRESULT WINAPI SetCustomTextOutHandlerEx(void **handler, DWORD flags)
{
    (void)flags;
    if (handler)
        *handler = nullptr;
    return E_NOTIMPL;
}

HRESULT WINAPI DllGetVersion(DLLVERSIONINFO *info)
{
    if (!info || info->cbSize < sizeof(DLLVERSIONINFO))
        return E_INVALIDARG;
    info->dwMajorVersion = 8;
    info->dwMinorVersion = 0;
    info->dwBuildNumber = 26300;
    info->dwPlatformID = 2;
    return S_OK;
}

HRESULT WINAPI MathBuildUp(void *range, void *strings, LONG flags)
{
    (void)range;
    (void)strings;
    (void)flags;
    return E_NOTIMPL;
}

HRESULT WINAPI MathBuildDown(void *range, void *strings, LONG flags)
{
    (void)range;
    (void)strings;
    (void)flags;
    return E_NOTIMPL;
}

HRESULT WINAPI MathTranslate(void *range, LONG flags)
{
    (void)range;
    (void)flags;
    return E_NOTIMPL;
}

LONG WINAPI GetMathAlphanumericCode(LONG ch, LONG style)
{
    (void)style;
    return ch;
}

LONG WINAPI GetMathAlphanumeric(LONG ch, LONG style)
{
    (void)style;
    return ch;
}

HRESULT WINAPI DllGetActivationFactory(void *id, void **factory)
{
    (void)id;
    if (factory)
        *factory = nullptr;
    return CLASS_E_CLASSNOTAVAILABLE;
}

HRESULT WINAPI ShutdownTextServices(void *services)
{
    (void)services;
    return E_NOTIMPL;
}

HRESULT WINAPI SetTextServicesDpiCalculationOverride(void *services, BOOL enable)
{
    (void)services;
    (void)enable;
    return E_NOTIMPL;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        return ReRegisterClasses(module, L"RICHEDIT50W", RichEditWndProc, nullptr, nullptr);
    }
    if (reason == DLL_PROCESS_DETACH)
        UnregisterClassW(L"RICHEDIT50W", g_module);
    return TRUE;
}
