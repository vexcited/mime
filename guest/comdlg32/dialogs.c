/*
 * the common dialogs that have no window yet: color, font and the print
 * dialogs validate their structure like windows and then report what a user
 * who cancelled would: FALSE, or PD_RESULT_CANCEL, with extended error 0.
 * without printers, asking for the default printer fails with
 * PDERR_NODEFAULTPRN. the file dialogs are in openfile.c.
 */
#include "comdlg32_private.h"

static DWORD g_error_tls = TLS_OUT_OF_INDEXES;

void DlgProcessAttach(void)
{
    g_error_tls = TlsAlloc();
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

/* a structure is valid when present and of the size this version knows. */
static BOOL check(const void *data, DWORD size, DWORD old_size)
{
    if (!data) {
        DlgSetError(CDERR_INITIALIZATION);
        return FALSE;
    }
    DWORD given = *(const DWORD *)data;
    if (given != size && (!old_size || given != old_size)) {
        DlgSetError(CDERR_STRUCTSIZE);
        return FALSE;
    }
    return TRUE;
}

static BOOL cancelled(void)
{
    DlgSetError(0);
    return FALSE;
}

/* the name part of a path; returns the size needed when the buffer is too small, -1 for a bad name. */
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

CDAPI BOOL WINAPI ChooseColorW(LPCHOOSECOLORW cc)
{
    if (!check(cc, sizeof(*cc), 0))
        return FALSE;
    return cancelled();
}

CDAPI BOOL WINAPI ChooseFontW(LPCHOOSEFONTW cf)
{
    if (!check(cf, sizeof(*cf), 0))
        return FALSE;
    return cancelled();
}

CDAPI BOOL WINAPI PrintDlgW(LPPRINTDLGW pd)
{
    if (!check(pd, sizeof(*pd), 0))
        return FALSE;
    if (pd->Flags & PD_RETURNDEFAULT) {
        DlgSetError(PDERR_NODEFAULTPRN);
        return FALSE;
    }
    return cancelled();
}

CDAPI HRESULT WINAPI PrintDlgExW(LPPRINTDLGEXW pd)
{
    if (!check(pd, sizeof(*pd), 0))
        return E_INVALIDARG;
    if (pd->Flags & PD_RETURNDEFAULT) {
        DlgSetError(PDERR_NODEFAULTPRN);
        return E_FAIL;
    }
    if (!pd->hwndOwner || !IsWindow(pd->hwndOwner)) {
        DlgSetError(CDERR_DIALOGFAILURE);
        return E_HANDLE;
    }
    DlgSetError(0);
    pd->dwResultAction = PD_RESULT_CANCEL;
    return S_OK;
}

CDAPI BOOL WINAPI PageSetupDlgW(LPPAGESETUPDLGW psd)
{
    if (!check(psd, sizeof(*psd), 0))
        return FALSE;
    if (psd->Flags & PSD_RETURNDEFAULT) {
        DlgSetError(PDERR_NODEFAULTPRN);
        return FALSE;
    }
    return cancelled();
}
