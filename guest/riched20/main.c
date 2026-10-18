/*
 * riched20.dll, rich edit 3.0: the classes RichEdit20W and RichEdit20A
 * over the shared engine, and the 1.0 emulation riched32.dll's class
 * runs on.
 */
#include <richedit/richedit_private.h>

static HINSTANCE g_module;

static LRESULT CALLBACK wide_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return ReWndProc(hwnd, msg, wparam, lparam, FALSE, FALSE, FALSE);
}

LRESULT WINAPI RichEditANSIWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return ReWndProc(hwnd, msg, wparam, lparam, TRUE, FALSE, FALSE);
}

LRESULT WINAPI RichEdit10ANSIWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return ReWndProc(hwnd, msg, wparam, lparam, TRUE, TRUE, FALSE);
}

/* windows registers REListBox20W and REComboBox20W here; their rich list boxes are not offered. */
int WINAPI REExtendedRegisterClass(void)
{
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        return ReRegisterClasses(module, L"RichEdit20W", wide_proc, "RichEdit20A", RichEditANSIWndProc);
    }
    if (reason == DLL_PROCESS_DETACH) {
        UnregisterClassA("RichEdit20A", g_module);
        UnregisterClassW(L"RichEdit20W", g_module);
    }
    return TRUE;
}
