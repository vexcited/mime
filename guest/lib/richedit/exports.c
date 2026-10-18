/*
 * what riched20.dll and msftedit.dll both export: the interface
 * identifiers, and text services, which this engine does not offer
 * without a window.
 */
#include "richedit_private.h"

const GUID IID_IRichEditOle = {0x00020D00, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID IID_IRichEditOleCallback = {0x00020D03, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const GUID IID_ITextServices = {0x8D33F740, 0xCF58, 0x11CE, {0xA8, 0x9D, 0x00, 0xAA, 0x00, 0x6C, 0xAD, 0xC5}};
const GUID IID_ITextServices2 = {0x8D33F741, 0xCF58, 0x11CE, {0xA8, 0x9D, 0x00, 0xAA, 0x00, 0x6C, 0xAD, 0xC5}};
const GUID IID_ITextHost = {0x13E670F4, 0x1A5A, 0x11CF, {0xAB, 0xEB, 0x00, 0xAA, 0x00, 0xB6, 0x5E, 0xA1}};
const GUID IID_ITextHost2 = {0x13E670F5, 0x1A5A, 0x11CF, {0xAB, 0xEB, 0x00, 0xAA, 0x00, 0xB6, 0x5E, 0xA1}};
const GUID IID_ITextDocument2 = {0xC241F5E0, 0x7206, 0x11D8, {0xA2, 0xC7, 0x00, 0xA0, 0xD1, 0xD6, 0xC6, 0xB3}};
const GUID IID_IRicheditWindowlessAccessibility = {
    0x983E572D, 0x20CD, 0x460B, {0x91, 0x04, 0x83, 0x11, 0x15, 0x92, 0xDD, 0x10}};
const GUID IID_IRicheditUiaOverrides = {0xF2FB5CC0, 0xB5A9, 0x437F, {0x9B, 0xA2, 0x47, 0x63, 0x20, 0x82, 0x26, 0x9F}};
const GUID IID_IRicheditUiaNotificationOverrides = {
    0xA6BF39F8, 0x11DE, 0x48EA, {0x87, 0x80, 0x99, 0x50, 0xBC, 0x13, 0x94, 0x9D}};

HRESULT WINAPI CreateTextServices(void *outer, void *host, void **out)
{
    (void)outer;
    (void)host;
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}

/* the classes of a dll; the ansi one too unless its name is null. */
BOOL ReRegisterClasses(HINSTANCE module, const WCHAR *wide, WNDPROC wide_proc, const char *ansi, WNDPROC ansi_proc)
{
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_GLOBALCLASS | CS_PARENTDC | CS_DBLCLKS;
    wc.lpfnWndProc = wide_proc;
    wc.cbWndExtra = 8;
    wc.hInstance = module;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = wide;
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return FALSE;
    if (!ansi)
        return TRUE;
    WNDCLASSA ac;
    memset(&ac, 0, sizeof(ac));
    ac.style = wc.style;
    ac.lpfnWndProc = ansi_proc;
    ac.cbWndExtra = wc.cbWndExtra;
    ac.hInstance = module;
    ac.hbrBackground = wc.hbrBackground;
    ac.lpszClassName = ansi;
    return RegisterClassA(&ac) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}
