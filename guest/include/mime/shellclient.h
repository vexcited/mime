/*
 * what the shell libraries tell the host's desktop through win32k (NtUserMimeShell): the application user model id
 * of a process and of a window, the progress, badge and attention of a window's button in the launcher, and a window's
 * request for attention. every call is a command of mime/abi.h; a host without a desktop to tell answers unsupported,
 * which no caller needs to know.
 */
#ifndef MIME_SHELLCLIENT_H
#define MIME_SHELLCLIENT_H

#include <mime/abi.h>
#include <mime/ntuser.h>

FORCEINLINE void MimeShellCopyText(uint16_t *dst, ULONG chars, const WCHAR *src)
{
    ULONG n = 0;
    if (src)
        while (n + 1 < chars && src[n]) {
            dst[n] = src[n];
            n++;
        }
    dst[n] = 0;
}

FORCEINLINE void MimeShellClear(void *p, SIZE_T size)
{
    volatile char *b = p;
    for (SIZE_T i = 0; i < size; i++)
        b[i] = 0;
}

/* the command goes to win32k, which checks that the window is the caller's. */
FORCEINLINE void MimeShellWindowCommand(MimeShellWindow *w)
{
    NtUserMimeShell(MIME_SHELLOP_COMMAND, 0, (PUCHAR)w, sizeof(*w));
}

/* the id of the process, that its windows without one of their own have. */
FORCEINLINE void MimeShellProcessAppId(const WCHAR *aumid)
{
    MimeShellWindow w;
    MimeShellClear(&w, sizeof(w));
    w.op = MIME_SHELL_WINDOW;
    w.kind = MIME_SHELL_PROCESS_APPID;
    MimeShellCopyText(w.aumid, sizeof(w.aumid) / sizeof(w.aumid[0]), aumid);
    MimeShellWindowCommand(&w);
}

/* the id of a window; none gives it back to its process. */
FORCEINLINE void MimeShellWindowAppId(HWND window, const WCHAR *aumid)
{
    MimeShellWindow w;
    MimeShellClear(&w, sizeof(w));
    w.op = MIME_SHELL_WINDOW;
    w.kind = MIME_SHELL_WINDOW_APPID;
    w.window_id = (uint32_t)(ULONG_PTR)window;
    MimeShellCopyText(w.aumid, sizeof(w.aumid) / sizeof(w.aumid[0]), aumid);
    MimeShellWindowCommand(&w);
}

#endif
