/*
 * riched32.dll, rich edit 1.0: windows' riched32 exports nothing and
 * registers the ansi class RICHEDIT over riched20's 1.0 emulation.
 */
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>

DECLSPEC_IMPORT LRESULT WINAPI RichEdit10ANSIWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

static HINSTANCE g_module;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        WNDCLASSA wc;
        memset(&wc, 0, sizeof(wc));
        wc.style = CS_GLOBALCLASS | CS_PARENTDC | CS_DBLCLKS;
        wc.lpfnWndProc = RichEdit10ANSIWndProc;
        wc.cbWndExtra = 8;
        wc.hInstance = module;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = "RICHEDIT";
        return RegisterClassA(&wc) != 0;
    }
    if (reason == DLL_PROCESS_DETACH)
        UnregisterClassA("RICHEDIT", g_module);
    return TRUE;
}
