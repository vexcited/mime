/*
 * atmlib.dll: the Adobe Type Manager client. the font driver it talks to is not part of the system any more, so it is
 * not running: the library says so, and the calls that need the driver give the error for that.
 */
#include <mime/win32/winbase.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, PVOID reserved)
{
    return TRUE;
}

#define ATM_NOT_RUNNING (-6)
#define ATM_BAD_ARGUMENT (-102)

int WINAPI ATMProperlyLoaded(void)
{
    return 0;
}

static int names(const void *name, void *result)
{
    return name && result ? ATM_NOT_RUNNING : ATM_BAD_ARGUMENT;
}

int WINAPI ATMGetFontPathsA(const char *name, char *paths)
{
    return names(name, paths);
}

int WINAPI ATMGetFontPathsW(const WCHAR *name, WCHAR *paths)
{
    return names(name, paths);
}

int WINAPI ATMGetMenuNameA(const char *name, char *menu)
{
    return names(name, menu);
}

int WINAPI ATMGetMenuNameW(const WCHAR *name, WCHAR *menu)
{
    return names(name, menu);
}

int WINAPI ATMGetPostScriptNameA(const char *name, char *postscript)
{
    return names(name, postscript);
}

int WINAPI ATMGetPostScriptNameW(const WCHAR *name, WCHAR *postscript)
{
    return names(name, postscript);
}
