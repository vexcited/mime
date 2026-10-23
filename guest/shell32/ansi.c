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
           directory = ShWiden(info->lpDirectory), cls = ShWiden(info->lpClass);
    wide.lpVerb = verb;
    wide.lpFile = file;
    wide.lpParameters = parameters;
    wide.lpDirectory = directory;
    wide.lpClass = cls;
    BOOL ok = FALSE;
    /* a string that cannot be converted fails the call as if out of memory. */
    if ((info->lpVerb && !verb) || (info->lpFile && !file) || (info->lpParameters && !parameters) ||
        (info->lpDirectory && !directory) || (info->lpClass && !cls))
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

/* the sizes of the oldest structure, with its 64 character tip, and of the one adding the balloon. */
#define OLD_SIZE_A (offsetof(NOTIFYICONDATAA, szTip) + 64)
#define BALLOON_SIZE_A offsetof(NOTIFYICONDATAA, guidItem)

static void to_wide(WCHAR *out, int out_chars, const CHAR *in, int in_chars)
{
    int n = 0;
    while (n < in_chars && in[n])
        n++;
    /* an ansi text of n bytes has at most n characters. */
    int made = n ? MultiByteToWideChar(CP_ACP, 0, in, n, out, out_chars - 1) : 0;
    out[made] = 0;
}

SHSTDAPI BOOL WINAPI Shell_NotifyIconA(DWORD message, PNOTIFYICONDATAA data)
{
    if (!data || data->cbSize < OLD_SIZE_A) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    NOTIFYICONDATAW wide;
    memset(&wide, 0, sizeof(wide));
    DWORD size = data->cbSize;
    wide.hWnd = data->hWnd;
    wide.uID = data->uID;
    wide.uFlags = data->uFlags;
    wide.uCallbackMessage = data->uCallbackMessage;
    wide.hIcon = data->hIcon;
    /* the tip holds 64 characters in the oldest form, 128 in the others. */
    to_wide(wide.szTip, 128, data->szTip, size >= BALLOON_SIZE_A ? 128 : 64);
    if (size >= BALLOON_SIZE_A) {
        wide.dwState = data->dwState;
        wide.dwStateMask = data->dwStateMask;
        to_wide(wide.szInfo, 256, data->szInfo, 256);
        wide.uTimeout = data->uTimeout;
        to_wide(wide.szInfoTitle, 64, data->szInfoTitle, 64);
        wide.dwInfoFlags = data->dwInfoFlags;
    }
    if (size >= sizeof(NOTIFYICONDATAA) - sizeof(HICON)) {
        wide.guidItem = data->guidItem;
        wide.cbSize = size >= sizeof(NOTIFYICONDATAA) ? sizeof(NOTIFYICONDATAW) : sizeof(NOTIFYICONDATAW) - sizeof(HICON);
        if (size >= sizeof(NOTIFYICONDATAA))
            wide.hBalloonIcon = data->hBalloonIcon;
    } else if (size >= BALLOON_SIZE_A) {
        wide.cbSize = offsetof(NOTIFYICONDATAW, guidItem);
    } else {
        wide.cbSize = offsetof(NOTIFYICONDATAW, szTip) + 64 * sizeof(WCHAR);
    }
    return Shell_NotifyIconW(message, &wide);
}
