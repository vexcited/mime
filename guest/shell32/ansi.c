/*
 * ansi forms of the shell functions taking structures: the strings are widened, the wide function runs, and its
 * results are copied back.
 */
#include "shell32_private.h"
#include <mime/win32/shellapi.h>

SHSTDAPI BOOL WINAPI ShellExecuteExA(SHELLEXECUTEINFOA *info)
{
    if (!info || info->cbSize != sizeof(*info)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    SHELLEXECUTEINFOW wide;
    memset(&wide, 0, sizeof(wide));
    wide.cbSize = sizeof(wide);
    wide.fMask = info->fMask;
    wide.hwnd = info->hwnd;
    wide.nShow = info->nShow;
    wide.lpIDList = info->lpIDList;
    wide.hkeyClass = info->hkeyClass;
    wide.dwHotKey = info->dwHotKey;
    wide.hIcon = info->hIcon;
    LPWSTR verb = ShWiden(info->lpVerb), file = ShWiden(info->lpFile), parameters = ShWiden(info->lpParameters),
           directory = ShWiden(info->lpDirectory);
    /* lpClass is read only with the class name mask, callers leave it uninitialized otherwise. */
    LPCSTR class_name = (info->fMask & SEE_MASK_CLASSNAME) ? info->lpClass : nullptr;
    LPWSTR cls = ShWiden(class_name);
    wide.lpVerb = verb;
    wide.lpFile = file;
    wide.lpParameters = parameters;
    wide.lpDirectory = directory;
    wide.lpClass = cls;
    BOOL ok = FALSE;
    /* a string that cannot be converted fails the call as if out of memory. */
    if ((info->lpVerb && !verb) || (info->lpFile && !file) || (info->lpParameters && !parameters) ||
        (info->lpDirectory && !directory) || (class_name && !cls))
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    else
        ok = ShellExecuteExW(&wide);
    info->hInstApp = wide.hInstApp;
    info->hProcess = wide.hProcess;
    ShFree(verb);
    ShFree(file);
    ShFree(parameters);
    ShFree(directory);
    ShFree(cls);
    return ok;
}

static void to_wide(WCHAR *out, int out_chars, const CHAR *in, int in_chars)
{
    int n = 0;
    while (n < in_chars && in[n])
        n++;
    /* an ansi text of n bytes has at most n characters. */
    int made = n ? MultiByteToWideChar(CP_ACP, 0, in, n, out, out_chars - 1) : 0;
    out[made] = 0;
}

/*
 * Shell_NotifyIconA of shell32 26300: the wide structure of the size that matches, the first one for a size that is
 * none of the known ones, with the texts the flags of that size name.
 */
SHSTDAPI BOOL WINAPI Shell_NotifyIconA(DWORD message, PNOTIFYICONDATAA data)
{
    if (!data) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    NOTIFYICONDATAW wide;
    memset(&wide, 0, sizeof(wide));
    DWORD size = data->cbSize;
    UINT flags = data->uFlags;
    wide.cbSize = sizeof(wide);
    wide.hWnd = data->hWnd;
    wide.uID = data->uID;
    wide.uCallbackMessage = data->uCallbackMessage;
    wide.hIcon = data->hIcon;
    if (size >= offsetof(NOTIFYICONDATAA, guidItem)) {
        wide.dwState = data->dwState;
        wide.dwStateMask = data->dwStateMask;
        wide.uTimeout = data->uTimeout;
        wide.dwInfoFlags = data->dwInfoFlags;
    }
    if (size == sizeof(NOTIFYICONDATAA)) {
        wide.hBalloonIcon = data->hBalloonIcon;
    } else if (size == offsetof(NOTIFYICONDATAA, hBalloonIcon)) {
        wide.cbSize = offsetof(NOTIFYICONDATAW, hBalloonIcon);
        flags &= 0x3F;
    } else if (size == offsetof(NOTIFYICONDATAA, guidItem)) {
        wide.cbSize = offsetof(NOTIFYICONDATAW, guidItem);
        flags &= 0x1F;
    } else {
        wide.cbSize = offsetof(NOTIFYICONDATAW, szTip) + 64 * sizeof(WCHAR);
        flags &= 7;
    }
    wide.uFlags = flags;
    if (flags & NIF_TIP)
        to_wide(wide.szTip, 128, data->szTip, wide.cbSize < offsetof(NOTIFYICONDATAW, guidItem) ? 64 : 128);
    if (flags & NIF_INFO) {
        to_wide(wide.szInfo, 256, data->szInfo, 256);
        to_wide(wide.szInfoTitle, 64, data->szInfoTitle, 64);
    }
    if (flags & NIF_GUID)
        wide.guidItem = data->guidItem;
    return Shell_NotifyIconW(message, &wide);
}
