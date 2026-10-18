/*
 * GetOpenFileName and GetSaveFileName, over the file dialog window of
 * filedlg.c: the structure's filters, names and flags become the dialog's,
 * and what the user chose is written back as the structure promises, in the
 * buffer of one path, or a folder and the names in it for several.
 */
#include "filedlg.h"
#include <mime/win32/shlwapi.h>

#ifndef FNERR_BUFFERTOOSMALL
#define FNERR_BUFFERTOOSMALL 0x3003
#endif

/* the flags that mean the same as the options of the item dialogs. */
#define SHARED_FLAGS                                                                                               \
    (OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_ALLOWMULTISELECT | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST |        \
     OFN_CREATEPROMPT | OFN_SHAREAWARE | OFN_NOREADONLYRETURN | OFN_NOTESTFILECREATE | OFN_NODEREFERENCELINKS |    \
     OFN_FORCESHOWHIDDEN)

typedef struct _OFN_RUN {
    LPOPENFILENAMEW Ofn;
    BOOL Save;
    BOOL HasCustom;
    HWND Hook;
    DWORD Error;
    COMDLG_FILTERSPEC *Types;
    UINT TypeCount;
} OFN_RUN;

/* ------------------------------------------------------------------ the hook */

/* a notification to the hook; the answer is what it left in the message result. */
static LRESULT notify_hook(OFN_RUN *run, UINT code, LPWSTR file)
{
    OFNOTIFYW notify;
    if (!run->Hook)
        return 0;
    memset(&notify, 0, sizeof(notify));
    notify.hdr.hwndFrom = GetParent(run->Hook);
    notify.hdr.code = code;
    notify.lpOFN = run->Ofn;
    notify.pszFile = file;
    SetWindowLongPtrW(run->Hook, DWLP_MSGRESULT, 0);
    SendMessageW(run->Hook, WM_NOTIFY, 0, (LPARAM)&notify);
    return GetWindowLongPtrW(run->Hook, DWLP_MSGRESULT);
}

/* the hook of an explorer style dialog lives in a child of the dialog, which is the window it works with. */
static void make_hook(OFN_RUN *run, HWND parent)
{
    static const WORD child[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 'S', 'e', 'g', 'o', 'e', ' ', 'U', 'I', 0};
    WORD *copy = HeapAlloc(GetProcessHeap(), 0, sizeof(child));
    if (!copy)
        return;
    memcpy(copy, child, sizeof(child));
    DWORD style = WS_CHILD | WS_VISIBLE | DS_CONTROL | DS_SETFONT;
    copy[0] = LOWORD(style);
    copy[1] = HIWORD(style);
    run->Hook = CreateDialogIndirectParamW(g_comdlg32, (LPCDLGTEMPLATEW)copy, parent,
                                           (DLGPROC)run->Ofn->lpfnHook, (LPARAM)run->Ofn);
    HeapFree(GetProcessHeap(), 0, copy);
}

/* ------------------------------------------------------------------ the buffers */

static BOOL ends_with_slash(LPCWSTR text)
{
    SIZE_T n = wcslen(text);
    return n && (text[n - 1] == L'\\' || text[n - 1] == L'/');
}

/* the offsets of the structure for a path in one string. */
static void offsets_of(LPCWSTR path, WORD *file, WORD *extension)
{
    LPCWSTR name = path;
    for (LPCWSTR p = path; *p; p++)
        if (*p == L'\\' || *p == L'/' || *p == L':')
            name = p + 1;
    *file = (WORD)(name - path);
    LPCWSTR dot = nullptr;
    for (LPCWSTR p = name; *p; p++)
        if (*p == L'.')
            dot = p;
    *extension = dot ? (WORD)(dot + 1 - path) : 0;
}

/*
 * writes the results into the file buffer: one path alone, or the folder and then each name, in the explorer
 * style the folder and the names ended by a zero, in the old style separated by blanks. FALSE when it does not fit,
 * with the size it needs in the first character.
 */
static BOOL store_results(OFN_RUN *run, FILEDLG *dialog)
{
    LPOPENFILENAMEW ofn = run->Ofn;
    UINT count = FdResultCount(dialog);
    if (!count)
        return TRUE;
    BOOL several = count > 1;
    WCHAR folder[MAX_PATH];
    LPCWSTR first = FdResultPath(dialog, 0);
    SIZE_T needed;
    if (!several) {
        needed = wcslen(first) + 1;
    } else {
        lstrcpynW(folder, first, MAX_PATH);
        PathRemoveFileSpecW(folder);
        needed = wcslen(folder) + 1;
        for (UINT i = 0; i < count; i++) {
            LPCWSTR name = PathFindFileNameW(FdResultPath(dialog, i));
            needed += wcslen(name) + 1 + (!(ofn->Flags & OFN_EXPLORER) && wcschr(name, L' ') ? 2 : 0);
        }
    }
    if (several)
        needed++;
    if (needed > ofn->nMaxFile) {
        if (ofn->lpstrFile && ofn->nMaxFile)
            ofn->lpstrFile[0] = (WCHAR)(needed > 0xFFFF ? 0xFFFF : needed);
        run->Error = FNERR_BUFFERTOOSMALL;
        return FALSE;
    }
    WCHAR *out = ofn->lpstrFile;
    if (!several) {
        wcscpy(out, first);
        out[needed - 1] = 0;
        offsets_of(out, &ofn->nFileOffset, &ofn->nFileExtension);
    } else {
        SIZE_T at = 0;
        wcscpy(out, folder);
        at = wcslen(folder);
        BOOL explorer = (ofn->Flags & OFN_EXPLORER) != 0;
        out[at++] = explorer ? 0 : L' ';
        ofn->nFileOffset = (WORD)at;
        ofn->nFileExtension = 0;
        for (UINT i = 0; i < count; i++) {
            LPCWSTR name = PathFindFileNameW(FdResultPath(dialog, i));
            BOOL quote = !explorer && wcschr(name, L' ');
            if (quote)
                out[at++] = L'"';
            wcscpy(out + at, name);
            at += wcslen(name);
            if (quote)
                out[at++] = L'"';
            out[at++] = explorer || i + 1 == count ? 0 : L' ';
        }
        out[at] = 0;
    }
    /* the name alone, for the program that wants it. */
    if (ofn->lpstrFileTitle && ofn->nMaxFileTitle) {
        LPCWSTR name = PathFindFileNameW(first);
        lstrcpynW(ofn->lpstrFileTitle, name, (int)ofn->nMaxFileTitle);
    }
    return TRUE;
}

/* ------------------------------------------------------------------ events */

static void ofn_initialized(FILEDLG *dialog, void *context)
{
    OFN_RUN *run = context;
    if ((run->Ofn->Flags & OFN_ENABLEHOOK) && run->Ofn->lpfnHook && (run->Ofn->Flags & OFN_EXPLORER)) {
        make_hook(run, FdWindow(dialog));
        notify_hook(run, CDN_INITDONE, nullptr);
        /* the folder the dialog opened in was reached before the hook was there. */
        notify_hook(run, CDN_FOLDERCHANGE, nullptr);
    }
}

static BOOL ofn_file_ok(FILEDLG *dialog, void *context)
{
    OFN_RUN *run = context;
    if (!store_results(run, dialog)) {
        FdClose(dialog, E_FAIL);
        return TRUE;
    }
    return notify_hook(run, CDN_FILEOK, run->Ofn->lpstrFile) == 0;
}

static void ofn_folder_change(FILEDLG *dialog, void *context)
{
    notify_hook(context, CDN_FOLDERCHANGE, nullptr);
}

static void ofn_selection_change(FILEDLG *dialog, void *context)
{
    notify_hook(context, CDN_SELCHANGE, nullptr);
}

static void ofn_type_change(FILEDLG *dialog, void *context)
{
    OFN_RUN *run = context;
    UINT index = FdTypeIndex(dialog);
    run->Ofn->nFilterIndex = run->HasCustom ? index - 1 : index;
    notify_hook(run, CDN_TYPECHANGE, nullptr);
}

static const FILEDLG_EVENTS g_ofn_events = {
    .Initialized = ofn_initialized,
    .FileOk = ofn_file_ok,
    .FolderChange = ofn_folder_change,
    .SelectionChange = ofn_selection_change,
    .TypeChange = ofn_type_change,
};

/* ------------------------------------------------------------------ the structure's filters */

/* the filter strings in pairs of a name and a spec, ended by an empty string. */
static UINT count_pairs(LPCWSTR filter)
{
    UINT n = 0;
    while (filter && filter[0]) {
        filter += wcslen(filter) + 1;
        if (!filter[0])
            break;
        filter += wcslen(filter) + 1;
        n++;
    }
    return n;
}

static BOOL build_types(OFN_RUN *run)
{
    LPOPENFILENAMEW ofn = run->Ofn;
    BOOL custom = ofn->lpstrCustomFilter && ofn->lpstrCustomFilter[0] &&
                  ofn->lpstrCustomFilter[wcslen(ofn->lpstrCustomFilter) + 1];
    UINT pairs = count_pairs(ofn->lpstrFilter);
    run->HasCustom = custom;
    run->TypeCount = pairs + (custom ? 1 : 0);
    if (!run->TypeCount)
        return TRUE;
    run->Types = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, run->TypeCount * sizeof(COMDLG_FILTERSPEC));
    if (!run->Types)
        return FALSE;
    UINT at = 0;
    if (custom) {
        LPCWSTR name = ofn->lpstrCustomFilter;
        run->Types[at].pszName = name;
        run->Types[at].pszSpec = name + wcslen(name) + 1;
        at++;
    }
    LPCWSTR p = ofn->lpstrFilter;
    for (UINT i = 0; i < pairs; i++) {
        run->Types[at].pszName = p;
        p += wcslen(p) + 1;
        run->Types[at].pszSpec = p;
        p += wcslen(p) + 1;
        at++;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ running it */

static BOOL file_name_w(LPOPENFILENAMEW ofn, BOOL save)
{
    if (!ofn) {
        DlgSetError(CDERR_INITIALIZATION);
        return FALSE;
    }
    if (ofn->lStructSize != OPENFILENAME_SIZE_VERSION_400W && ofn->lStructSize != sizeof(OPENFILENAMEW)) {
        DlgSetError(CDERR_STRUCTSIZE);
        return FALSE;
    }
    if (!ofn->lpstrFile || !ofn->nMaxFile || ofn->nMaxFile > 0x7FFFFFFF ||
        (ofn->lpstrCustomFilter && ofn->nMaxCustFilter > 0x7FFFFFFF) ||
        (ofn->lpstrFileTitle && ofn->nMaxFileTitle > 0x7FFFFFFF)) {
        DlgSetError(CDERR_INITIALIZATION);
        return FALSE;
    }
    if ((ofn->Flags & OFN_ENABLEHOOK) && !ofn->lpfnHook) {
        DlgSetError(CDERR_NOHOOK);
        return FALSE;
    }
    DlgSetError(0);

    OFN_RUN run;
    memset(&run, 0, sizeof(run));
    run.Ofn = ofn;
    run.Save = save;
    FILEDLG *dialog = FdCreate(save);
    if (!dialog || !build_types(&run)) {
        FdFree(dialog);
        DlgSetError(CDERR_MEMALLOCFAILURE);
        return FALSE;
    }
    FdSetEvents(dialog, &g_ofn_events, &run);
    FdSetOptions(dialog, ofn->Flags & SHARED_FLAGS);
    if (run.TypeCount) {
        FdSetTypes(dialog, run.TypeCount, run.Types);
        UINT index = ofn->nFilterIndex;
        if (run.HasCustom)
            index = index ? index + 1 : 1;
        FdSetTypeIndex(dialog, index ? index : 1);
    }
    if (ofn->lpstrTitle)
        FdSetTitle(dialog, ofn->lpstrTitle);
    if (ofn->lpstrDefExt && ofn->lpstrDefExt[0])
        FdSetDefaultExtension(dialog, ofn->lpstrDefExt);
    if (!save && !(ofn->Flags & OFN_HIDEREADONLY))
        FdSetReadOnlyBox(dialog, TRUE, (ofn->Flags & OFN_READONLY) != 0);

    /* the file name box starts with the name part of the buffer, and its folder is a start for the dialog. */
    WCHAR folder[MAX_PATH];
    folder[0] = 0;
    if (ofn->lpstrFile[0]) {
        WCHAR start[MAX_PATH];
        lstrcpynW(start, ofn->lpstrFile, MAX_PATH);
        if (wcschr(start, L'\\') || wcschr(start, L'/') || wcschr(start, L':')) {
            if (!ends_with_slash(start)) {
                FdSetFileName(dialog, PathFindFileNameW(start));
                PathRemoveFileSpecW(start);
            }
            lstrcpynW(folder, start, MAX_PATH);
        } else {
            FdSetFileName(dialog, start);
        }
    }
    if (ofn->lpstrInitialDir && ofn->lpstrInitialDir[0])
        FdSetFolder(dialog, ofn->lpstrInitialDir);
    else if (folder[0] && PathIsDirectoryW(folder))
        FdSetFolder(dialog, folder);
    else {
        WCHAR current[MAX_PATH];
        if (GetCurrentDirectoryW(MAX_PATH, current))
            FdSetFolder(dialog, current);
    }

    HRESULT hr = FdShow(dialog, ofn->hwndOwner);
    BOOL ok = SUCCEEDED(hr) && FdResultCount(dialog) && !run.Error;
    if (ok) {
        /* the results were written at the file ok; the rest of the structure follows. */
        ofn->Flags &= ~OFN_READONLY;
        if (FdReadOnlyChecked(dialog))
            ofn->Flags |= OFN_READONLY;
        UINT index = FdTypeIndex(dialog);
        if (run.TypeCount)
            ofn->nFilterIndex = run.HasCustom ? index - 1 : index;
        ofn->Flags &= ~OFN_EXTENSIONDIFFERENT;
        if (ofn->lpstrDefExt && ofn->lpstrDefExt[0] && ofn->nFileExtension && FdResultCount(dialog) == 1) {
            LPCWSTR extension = ofn->lpstrFile + ofn->nFileExtension;
            if (_wcsicmp(extension, ofn->lpstrDefExt))
                ofn->Flags |= OFN_EXTENSIONDIFFERENT;
        }
        if (!(ofn->Flags & OFN_NOCHANGEDIR)) {
            WCHAR where[MAX_PATH];
            lstrcpynW(where, FdResultPath(dialog, 0), MAX_PATH);
            PathRemoveFileSpecW(where);
            SetCurrentDirectoryW(where);
        }
    } else if (run.Error) {
        DlgSetError(run.Error);
    } else if (hr != HRESULT_FROM_WIN32(ERROR_CANCELLED) && FAILED(hr)) {
        DlgSetError(CDERR_DIALOGFAILURE);
    }
    HeapFree(GetProcessHeap(), 0, run.Types);
    FdFree(dialog);
    return ok;
}

CDAPI BOOL WINAPI GetOpenFileNameW(LPOPENFILENAMEW ofn)
{
    return file_name_w(ofn, FALSE);
}

CDAPI BOOL WINAPI GetSaveFileNameW(LPOPENFILENAMEW ofn)
{
    return file_name_w(ofn, TRUE);
}

/* ------------------------------------------------------------------ the ansi structure */

static LPWSTR widen(LPCSTR text)
{
    if (!text)
        return nullptr;
    int n = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    LPWSTR wide = n > 0 ? HeapAlloc(GetProcessHeap(), 0, (SIZE_T)n * sizeof(WCHAR)) : nullptr;
    if (wide)
        MultiByteToWideChar(CP_ACP, 0, text, -1, wide, n);
    return wide;
}

/* a list of strings ended by an empty one, as the filters are. */
static LPWSTR widen_list(LPCSTR list, DWORD *chars)
{
    if (!list)
        return nullptr;
    SIZE_T length = 0;
    while (list[length] || list[length + 1])
        length++;
    length += 2;
    int n = MultiByteToWideChar(CP_ACP, 0, list, (int)length, nullptr, 0);
    LPWSTR wide = n > 0 ? HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ((SIZE_T)n + 2) * sizeof(WCHAR)) : nullptr;
    if (wide) {
        MultiByteToWideChar(CP_ACP, 0, list, (int)length, wide, n);
        if (chars)
            *chars = (DWORD)n;
    }
    return wide;
}

/* the text of a buffer of strings, converted back until the double zero or the end. */
static void narrow_buffer(LPCWSTR wide, DWORD wide_chars, LPSTR out, DWORD out_chars)
{
    DWORD length = 0;
    while (length < wide_chars && (wide[length] || (length + 1 < wide_chars && wide[length + 1])))
        length++;
    length += 2;
    if (length > wide_chars)
        length = wide_chars;
    int n = WideCharToMultiByte(CP_ACP, 0, wide, (int)length, out, (int)out_chars, nullptr, nullptr);
    if (n <= 0 && out_chars)
        out[0] = 0;
}

static BOOL file_name_a(LPOPENFILENAMEA ofn, BOOL save)
{
    if (!ofn) {
        DlgSetError(CDERR_INITIALIZATION);
        return FALSE;
    }
    if (ofn->lStructSize != OPENFILENAME_SIZE_VERSION_400A && ofn->lStructSize != sizeof(OPENFILENAMEA)) {
        DlgSetError(CDERR_STRUCTSIZE);
        return FALSE;
    }
    if (!ofn->lpstrFile || !ofn->nMaxFile || ofn->nMaxFile > 0x7FFFFFFF) {
        DlgSetError(CDERR_INITIALIZATION);
        return FALSE;
    }
    OPENFILENAMEW w;
    memset(&w, 0, sizeof(w));
    w.lStructSize = sizeof(w);
    w.hwndOwner = ofn->hwndOwner;
    w.hInstance = ofn->hInstance;
    w.nMaxCustFilter = ofn->nMaxCustFilter;
    w.nFilterIndex = ofn->nFilterIndex;
    w.nMaxFile = ofn->nMaxFile;
    w.nMaxFileTitle = ofn->nMaxFileTitle;
    w.Flags = ofn->Flags;
    w.lCustData = ofn->lCustData;
    w.FlagsEx = ofn->FlagsEx;
    /* hooks and templates are of the ansi structure, which the wide dialog cannot give them. */
    w.Flags &= ~(OFN_ENABLEHOOK | OFN_ENABLETEMPLATE | OFN_ENABLETEMPLATEHANDLE);
    w.lpstrFilter = widen_list(ofn->lpstrFilter, nullptr);
    DWORD custom_chars = 0;
    if (ofn->lpstrCustomFilter) {
        w.lpstrCustomFilter = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ((SIZE_T)ofn->nMaxCustFilter + 2) * sizeof(WCHAR));
        if (w.lpstrCustomFilter && ofn->lpstrCustomFilter[0])
            MultiByteToWideChar(CP_ACP, 0, ofn->lpstrCustomFilter, (int)ofn->nMaxCustFilter, w.lpstrCustomFilter,
                                (int)ofn->nMaxCustFilter);
        custom_chars = ofn->nMaxCustFilter;
    }
    w.lpstrFile = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ((SIZE_T)ofn->nMaxFile + 2) * sizeof(WCHAR));
    if (w.lpstrFile)
        MultiByteToWideChar(CP_ACP, 0, ofn->lpstrFile, (int)ofn->nMaxFile, w.lpstrFile, (int)ofn->nMaxFile);
    if (ofn->lpstrFileTitle && ofn->nMaxFileTitle)
        w.lpstrFileTitle = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ((SIZE_T)ofn->nMaxFileTitle + 1) * sizeof(WCHAR));
    w.lpstrInitialDir = widen(ofn->lpstrInitialDir);
    w.lpstrTitle = widen(ofn->lpstrTitle);
    w.lpstrDefExt = widen(ofn->lpstrDefExt);
    BOOL ok = w.lpstrFile ? file_name_w(&w, save) : FALSE;
    if (ok || CommDlgExtendedError() == FNERR_BUFFERTOOSMALL) {
        /* the offsets are of the ansi text. */
        narrow_buffer(w.lpstrFile, ofn->nMaxFile, ofn->lpstrFile, ofn->nMaxFile);
        if (ok) {
            ofn->Flags = (ofn->Flags & (OFN_ENABLEHOOK | OFN_ENABLETEMPLATE | OFN_ENABLETEMPLATEHANDLE)) |
                         (w.Flags & ~(OFN_ENABLEHOOK | OFN_ENABLETEMPLATE | OFN_ENABLETEMPLATEHANDLE));
            ofn->nFilterIndex = w.nFilterIndex;
            if (ofn->lpstrFileTitle && ofn->nMaxFileTitle)
                WideCharToMultiByte(CP_ACP, 0, w.lpstrFileTitle, -1, ofn->lpstrFileTitle, (int)ofn->nMaxFileTitle,
                                    nullptr, nullptr);
            /* the offsets are in the wide string, so the ansi ones count the bytes of what comes before. */
            int offset = WideCharToMultiByte(CP_ACP, 0, w.lpstrFile, w.nFileOffset, nullptr, 0, nullptr, nullptr);
            ofn->nFileOffset = (WORD)(offset > 0 ? offset : 0);
            int extension = w.nFileExtension ? WideCharToMultiByte(CP_ACP, 0, w.lpstrFile, w.nFileExtension, nullptr, 0,
                                                                   nullptr, nullptr) : 0;
            ofn->nFileExtension = (WORD)(extension > 0 ? extension : 0);
        }
        if (ofn->lpstrCustomFilter && w.lpstrCustomFilter)
            narrow_buffer(w.lpstrCustomFilter, custom_chars, ofn->lpstrCustomFilter, custom_chars);
    }
    HeapFree(GetProcessHeap(), 0, (void *)w.lpstrFilter);
    HeapFree(GetProcessHeap(), 0, w.lpstrCustomFilter);
    HeapFree(GetProcessHeap(), 0, w.lpstrFile);
    HeapFree(GetProcessHeap(), 0, w.lpstrFileTitle);
    HeapFree(GetProcessHeap(), 0, (void *)w.lpstrInitialDir);
    HeapFree(GetProcessHeap(), 0, (void *)w.lpstrTitle);
    HeapFree(GetProcessHeap(), 0, (void *)w.lpstrDefExt);
    return ok;
}

CDAPI BOOL WINAPI GetOpenFileNameA(LPOPENFILENAMEA ofn)
{
    return file_name_a(ofn, FALSE);
}

CDAPI BOOL WINAPI GetSaveFileNameA(LPOPENFILENAMEA ofn)
{
    return file_name_a(ofn, TRUE);
}
