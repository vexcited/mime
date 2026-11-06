/* the per-thread extended error of the common dialogs, and the file title functions. */
#include "comdlg32_private.h"

static DWORD g_error_tls = TLS_OUT_OF_INDEXES;

void DlgProcessAttach(void)
{
    g_error_tls = TlsAlloc();
}

/* a FreeLibrary gives the slot back, or a program that loads and frees the dll again and again runs out of them. */
void DlgProcessFree(void)
{
    if (g_error_tls != TLS_OUT_OF_INDEXES)
        TlsFree(g_error_tls);
    g_error_tls = TLS_OUT_OF_INDEXES;
}

void DlgSetError(DWORD error)
{
    if (g_error_tls != TLS_OUT_OF_INDEXES)
        TlsSetValue(g_error_tls, (void *)(ULONG_PTR)error);
}

CDAPI DWORD WINAPI CommDlgExtendedError(void)
{
    return g_error_tls != TLS_OUT_OF_INDEXES ? (DWORD)(ULONG_PTR)TlsGetValue(g_error_tls) : 0;
}

/* the name part of a path. returns the size needed when the buffer is too small, -1 for a bad name. */
CDAPI short WINAPI GetFileTitleW(LPCWSTR file, LPWSTR title, WORD chars)
{
    if (!file || !file[0])
        return -1;
    SIZE_T length = wcslen(file);
    if (file[length - 1] == L'\\' || file[length - 1] == L'/' || file[length - 1] == L':')
        return -1;
    LPCWSTR name = file;
    for (LPCWSTR p = file; *p; p++) {
        if (*p == L'\\' || *p == L'/' || *p == L':')
            name = p + 1;
        else if (*p == L'*' || *p == L'?' || *p == L'<' || *p == L'>' || *p == L'|' || *p == L'"')
            return -1;
    }
    SIZE_T needed = wcslen(name) + 1;
    if (!title || chars < needed)
        return (short)needed;
    memcpy(title, name, needed * sizeof(WCHAR));
    return 0;
}

CDAPI short WINAPI GetFileTitleA(LPCSTR file, LPSTR title, WORD chars)
{
    if (!file)
        return -1;
    WCHAR wide[MAX_PATH], name[MAX_PATH];
    if (!MultiByteToWideChar(CP_ACP, 0, file, -1, wide, MAX_PATH))
        return -1;
    short needed = GetFileTitleW(wide, name, MAX_PATH);
    if (needed)
        return needed;
    int n = WideCharToMultiByte(CP_ACP, 0, name, -1, nullptr, 0, nullptr, nullptr);
    if (!title || chars < n)
        return (short)n;
    WideCharToMultiByte(CP_ACP, 0, name, -1, title, chars, nullptr, nullptr);
    return 0;
}
