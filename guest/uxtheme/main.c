/* uxtheme entry point. */
#include <mime/win32/winbase.h>

/*
 * GlobalsStartup of uxtheme 26300 keeps the file name of the program when the library loads. the first window call
 * of a process loads this library as the api hook, and GetModuleFileNameW leaves no error behind: that call returns
 * with a last error of 0 on windows, whatever it was before. measured there with a write breakpoint on the field.
 */
WCHAR UxProcessName[MAX_PATH];

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        WCHAR path[MAX_PATH];
        if (!GetModuleFileNameW(nullptr, path, MAX_PATH))
            return FALSE;
        PCWSTR name = path, end = nullptr;
        for (PCWSTR p = path; *p; p++) {
            if (*p == L'\\' || *p == L'/') {
                name = p + 1;
                end = nullptr;
            } else if (*p == L'.') {
                end = p;
            }
        }
        SIZE_T length = (SIZE_T)((end ? end : name + lstrlenW(name)) - name);
        for (SIZE_T i = 0; i < length; i++)
            UxProcessName[i] = name[i];
        UxProcessName[length] = 0;
    }
    return TRUE;
}
