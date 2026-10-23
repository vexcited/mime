/*
 * what the shell libraries tell the host's desktop through NtUserMimeShell: the app user model id of a process
 * or window, the progress, badge and attention state of a window's launcher button, and a window's request for
 * attention. each call is a mime/abi.h command. a host without a desktop answers unsupported, which callers ignore.
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

/* win32k checks that the window belongs to the caller. */
FORCEINLINE void MimeShellWindowCommand(MimeShellWindow *w)
{
    NtUserMimeShell(MIME_SHELLOP_COMMAND, 0, (PUCHAR)w, sizeof(*w));
}

/* the process id applies to its windows that have none of their own. */
FORCEINLINE void MimeShellProcessAppId(const WCHAR *aumid)
{
    MimeShellWindow w;
    MimeShellClear(&w, sizeof(w));
    w.op = MIME_SHELL_WINDOW;
    w.kind = MIME_SHELL_PROCESS_APPID;
    MimeShellCopyText(w.aumid, sizeof(w.aumid) / sizeof(w.aumid[0]), aumid);
    MimeShellWindowCommand(&w);
}

/* a NULL id makes the window use its process's again. */
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
