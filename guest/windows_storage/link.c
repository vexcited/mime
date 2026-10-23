/*
 * IShellLinkW and IShellLinkA with target, string fields, extra data and a property store. linkfile.c handles file
 * and stream persistence.
 */
#include "windows_storage_private.h"

#ifndef STG_E_ACCESSDENIED
#define STG_E_ACCESSDENIED ((HRESULT)0x80030005L)
#endif

#define LINK_FROM(This, member) ((LINK *)((BYTE *)(This) - offsetof(LINK, member)))

/* property-key interface ids handled directly by the link. */
static const IShellLinkWVtbl g_link_w_vtbl;
static const IShellLinkAVtbl g_link_a_vtbl;
static const IPersistFileVtbl g_file_vtbl;
static const IPersistStreamVtbl g_stream_vtbl;
static const IShellLinkDataListVtbl g_data_list_vtbl;
static const IPropertyStoreVtbl g_store_vtbl;
static const IObjectWithSiteVtbl g_site_vtbl;

/* the object. */

static HRESULT link_query(LINK *link, REFIID riid, void **object)
{
    void *found = nullptr;
    if (!object)
        return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IShellLinkW))
        found = &link->LinkW;
    else if (IsEqualIID(riid, &IID_IShellLinkA))
        found = &link->LinkA;
    else if (IsEqualIID(riid, &IID_IPersistFile) || IsEqualIID(riid, &IID_IPersist))
        found = &link->File;
    else if (IsEqualIID(riid, &IID_IPersistStream))
        found = &link->Stream;
    else if (IsEqualIID(riid, &IID_IShellLinkDataList))
        found = &link->DataList;
    else if (IsEqualIID(riid, &IID_IPropertyStore))
        found = &link->Store;
    else if (IsEqualIID(riid, &IID_IObjectWithSite))
        found = &link->Site;
    *object = found;
    if (!found)
        return E_NOINTERFACE;
    InterlockedIncrement(&link->Refs);
    return S_OK;
}

static ULONG link_add_ref(LINK *link)
{
    return (ULONG)InterlockedIncrement(&link->Refs);
}

static ULONG link_release(LINK *link)
{
    LONG refs = InterlockedDecrement(&link->Refs);
    if (!refs) {
        LkReset(link);
        LkFree(link->RelativeSource);
        LkFree(link->CurFile);
        if (link->SiteObject)
            link->SiteObject->lpVtbl->Release(link->SiteObject);
        LkFree(link);
    }
    return (ULONG)refs;
}

#define UNKNOWN_METHODS(prefix, Type, member)                                                                      \
    static HRESULT STDMETHODCALLTYPE prefix##_query_interface(Type *This, REFIID riid, void **object)               \
    {                                                                                                              \
        return link_query(LINK_FROM(This, member), riid, object);                                                  \
    }                                                                                                              \
    static ULONG STDMETHODCALLTYPE prefix##_add_ref(Type *This)                                                    \
    {                                                                                                              \
        return link_add_ref(LINK_FROM(This, member));                                                              \
    }                                                                                                              \
    static ULONG STDMETHODCALLTYPE prefix##_release(Type *This)                                                    \
    {                                                                                                              \
        return link_release(LINK_FROM(This, member));                                                              \
    }

UNKNOWN_METHODS(w, IShellLinkW, LinkW)
UNKNOWN_METHODS(a, IShellLinkA, LinkA)
UNKNOWN_METHODS(file, IPersistFile, File)
UNKNOWN_METHODS(stream, IPersistStream, Stream)
UNKNOWN_METHODS(list, IShellLinkDataList, DataList)
UNKNOWN_METHODS(store, IPropertyStore, Store)
UNKNOWN_METHODS(site, IObjectWithSite, Site)

HRESULT LinkCreate(IUnknown *outer, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    LINK *link = LkAlloc(sizeof(*link));
    if (!link)
        return E_OUTOFMEMORY;
    link->LinkW.lpVtbl = &g_link_w_vtbl;
    link->LinkA.lpVtbl = &g_link_a_vtbl;
    link->File.lpVtbl = &g_file_vtbl;
    link->Stream.lpVtbl = &g_stream_vtbl;
    link->DataList.lpVtbl = &g_data_list_vtbl;
    link->Store.lpVtbl = &g_store_vtbl;
    link->Site.lpVtbl = &g_site_vtbl;
    link->Refs = 1;
    link->ShowCmd = SW_SHOWNORMAL;
    link->Mode = STGM_READWRITE;
    HRESULT hr = link_query(link, riid, object);
    link_release(link);
    return hr;
}

static void set_dirty(LINK *link)
{
    link->Dirty = TRUE;
}

/* text and fields. */

/* load UI strings and truncate to the caller's buffer. */
static HRESULT get_field(LPCWSTR field, LPWSTR out, int chars)
{
    if (chars < 1) {
        if (chars && out)
            *out = 0;
        return E_INVALIDARG;
    }
    if (!out)
        return E_POINTER;
    if (field)
        return SHLoadIndirectString(field, out, (UINT)chars, nullptr);
    out[0] = 0;
    return S_OK;
}

static HRESULT set_field(LINK *link, LPWSTR *field, LPCWSTR text)
{
    if (*field ? (text && !wcscmp(*field, text)) : !text)
        return S_OK;
    set_dirty(link);
    LkSetString(field, text);
    if (text && !*field)
        return E_OUTOFMEMORY;
    return S_OK;
}

/* copy a path, truncating to the character capacity. */
static HRESULT copy_text(LPCWSTR text, LPWSTR out, int chars)
{
    if (chars < 1) {
        if (chars && out)
            *out = 0;
        return E_INVALIDARG;
    }
    int i = 0;
    while (i < chars - 1 && text[i]) {
        out[i] = text[i];
        i++;
    }
    out[i] = 0;
    return text[i] ? HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) : S_OK;
}

/* convert the wide result into the ANSI caller buffer. */
static void narrow(LPCWSTR wide, LPSTR out, int chars)
{
    if (!out || chars < 1)
        return;
    int n = WideCharToMultiByte(CP_ACP, 0, wide, -1, out, chars, nullptr, nullptr);
    if (!n)
        out[chars - 1] = 0;
}

static LPWSTR widen(LPCSTR text)
{
    if (!text)
        return nullptr;
    int n = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    LPWSTR wide = n > 0 ? LkAlloc((SIZE_T)n * sizeof(WCHAR)) : nullptr;
    if (wide)
        MultiByteToWideChar(CP_ACP, 0, text, -1, wide, n);
    return wide;
}

/* the target. */

/* return FALSE when the link has no filesystem target. */
static BOOL target_path(const LINK *link, LPWSTR path)
{
    path[0] = 0;
    return link->Pidl && SHGetPathFromIDListEx(link->Pidl, path, MAX_PATH, 0);
}

/* retain existing target find data in the shortcut header. */
static void read_find_data(LINK *link, LPCWSTR path)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &data))
        return;
    DWORD size = data.nFileSizeHigh ? 0xFFFFFFFF : data.nFileSizeLow;
    if (link->Attributes != data.dwFileAttributes || link->FileSize != size ||
        memcmp(&link->Created, &data.ftCreationTime, sizeof(FILETIME)) ||
        memcmp(&link->Written, &data.ftLastWriteTime, sizeof(FILETIME))) {
        link->Attributes = data.dwFileAttributes;
        link->FileSize = size;
        link->Created = data.ftCreationTime;
        link->Accessed = data.ftLastAccessTime;
        link->Written = data.ftLastWriteTime;
        set_dirty(link);
    }
}

/* set the target list. paths also supply find data and link info; lists supply a path. */
static HRESULT set_target(LINK *link, PCIDLIST_ABSOLUTE pidl, LPCWSTR path)
{
    PIDLIST_ABSOLUTE list;
    WCHAR full[MAX_PATH];
    if (path) {
        if (PathIsRelativeW(path)) {
            DWORD n = GetFullPathNameW(path, MAX_PATH, full, nullptr);
            if (!n || n >= MAX_PATH)
                return E_FAIL;
            path = full;
        }
        list = SHSimpleIDListFromPath(path);
    } else {
        list = pidl ? ILClone(pidl) : nullptr;
        if (pidl && !list)
            return E_OUTOFMEMORY;
    }
    if (!list && !pidl && !path)
        return S_OK;
    if (!list)
        return E_FAIL;
    if (link->Pidl && ILIsEqual(link->Pidl, list)) {
        ILFree(list);
        return S_OK;
    }
    /* discard locations relative to the previous target. */
    LkRemoveBlock(link, EXP_SPECIAL_FOLDER_SIG);
    LkRemoveBlock(link, EXP_KNOWN_FOLDER_SIG);
    LkRemoveBlock(link, LNK_SIG_TRACKER);
    if (link->Pidl)
        ILFree(link->Pidl);
    link->Pidl = list;
    LkFree(link->Info);
    link->Info = nullptr;
    link->InfoSize = 0;
    WCHAR local[MAX_PATH];
    if (path || target_path(link, local)) {
        LPCWSTR where = path ? path : local;
        read_find_data(link, where);
        if (!(link->Flags & (SLDF_FORCE_NO_LINKINFO | SLDF_PREFER_ENVIRONMENT_PATH)))
            link->Info = LkInfoCreate(where, &link->InfoSize);
    }
    set_dirty(link);
    return S_OK;
}

/* expand the environment path stored in the block. */
static BOOL environment_path(const LINK *link, LPWSTR path)
{
    LINK_BLOCK *block = LkFindBlock(link, EXP_SZ_LINK_SIG);
    path[0] = 0;
    if (!block || block->Size < sizeof(EXP_SZ_LINK))
        return FALSE;
    const EXP_SZ_LINK *exp = (const EXP_SZ_LINK *)block->Data;
    WCHAR raw[MAX_PATH];
    LkCopy(raw, exp->swzTarget, MAX_PATH);
    raw[MAX_PATH - 1] = 0;
    if (!raw[0] && exp->szTarget[0])
        MultiByteToWideChar(CP_ACP, 0, exp->szTarget, -1, raw, MAX_PATH);
    return raw[0] && ExpandEnvironmentStringsW(raw, path, MAX_PATH) != 0;
}

/* store the variable path in both wide and ANSI forms. */
static HRESULT set_exp_block(LINK *link, DWORD signature, LPCWSTR text)
{
    EXP_SZ_LINK exp;
    memset(&exp, 0, sizeof(exp));
    exp.cbSize = sizeof(exp);
    exp.dwSignature = signature;
    LkCopy(exp.swzTarget, text, MAX_PATH);
    WideCharToMultiByte(CP_ACP, 0, exp.swzTarget, -1, exp.szTarget, MAX_PATH, nullptr, nullptr);
    exp.szTarget[MAX_PATH - 1] = 0;
    return LkSetBlock(link, &exp);
}

/* IShellLinkW. */

static HRESULT STDMETHODCALLTYPE w_get_path(IShellLinkW *This, LPWSTR file, int chars, WIN32_FIND_DATAW *data,
                                            DWORD flags)
{
    LINK *link = LINK_FROM(This, LinkW);
    WCHAR path[MAX_PATH];
    HRESULT hr = S_OK;
    path[0] = 0;
    if (link->Flags & SLDF_HAS_DARWINID) {
        /* installer shortcuts use the icon location in place of a target. */
        if (!ExpandEnvironmentStringsW(link->IconLocation ? link->IconLocation : L"", path, MAX_PATH))
            hr = E_FAIL;
    } else {
        if (target_path(link, path) && (flags & SLGP_SHORTPATH))
            GetShortPathNameW(path, path, MAX_PATH);
        if (data) {
            memset(data, 0, sizeof(*data));
            if (path[0]) {
                data->dwFileAttributes = link->Attributes;
                data->ftCreationTime = link->Created;
                data->ftLastAccessTime = link->Accessed;
                data->ftLastWriteTime = link->Written;
                data->nFileSizeLow = link->FileSize;
                LPCWSTR name = PathFindFileNameW(path);
                copy_text(name, data->cFileName, MAX_PATH);
            }
        }
        if ((link->Flags & SLDF_HAS_EXP_SZ) && (flags & SLGP_RAWPATH)) {
            LINK_BLOCK *block = LkFindBlock(link, EXP_SZ_LINK_SIG);
            if (block && block->Size >= sizeof(EXP_SZ_LINK))
                copy_text(((const EXP_SZ_LINK *)block->Data)->swzTarget, path, MAX_PATH);
        }
        if (link->RelativePath && (flags & SLGP_RELATIVEPRIORITY))
            copy_text(link->RelativePath, path, MAX_PATH);
    }
    if (hr < 0)
        return hr;
    if (file) {
        hr = copy_text(path, file, chars);
        if (hr < 0)
            return hr;
    }
    return path[0] ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE w_get_id_list(IShellLinkW *This, PIDLIST_ABSOLUTE *pidl)
{
    LINK *link = LINK_FROM(This, LinkW);
    if (!pidl)
        return E_POINTER;
    *pidl = nullptr;
    if (!link->Pidl)
        return S_FALSE;
    *pidl = ILClone(link->Pidl);
    return *pidl ? S_OK : HRESULT_FROM_WIN32(ERROR_OUTOFMEMORY);
}

/* an empty target resets the shortcut. */
static HRESULT STDMETHODCALLTYPE w_set_id_list(IShellLinkW *This, PCIDLIST_ABSOLUTE pidl)
{
    LINK *link = LINK_FROM(This, LinkW);
    link->Flags &= ~SLDF_PREFER_ENVIRONMENT_PATH;
    if (!pidl) {
        LkReset(link);
        set_dirty(link);
        return S_OK;
    }
    set_target(link, pidl, nullptr);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE w_get_description(IShellLinkW *This, LPWSTR name, int chars)
{
    return get_field(LINK_FROM(This, LinkW)->Name, name, chars);
}

static HRESULT STDMETHODCALLTYPE w_set_description(IShellLinkW *This, LPCWSTR name)
{
    LINK *link = LINK_FROM(This, LinkW);
    return set_field(link, &link->Name, name);
}

static HRESULT STDMETHODCALLTYPE w_get_working_directory(IShellLinkW *This, LPWSTR directory, int chars)
{
    return get_field(LINK_FROM(This, LinkW)->WorkingDir, directory, chars);
}

static HRESULT STDMETHODCALLTYPE w_set_working_directory(IShellLinkW *This, LPCWSTR directory)
{
    LINK *link = LINK_FROM(This, LinkW);
    return set_field(link, &link->WorkingDir, directory);
}

static HRESULT STDMETHODCALLTYPE w_get_arguments(IShellLinkW *This, LPWSTR arguments, int chars)
{
    return get_field(LINK_FROM(This, LinkW)->Arguments, arguments, chars);
}

static HRESULT STDMETHODCALLTYPE w_set_arguments(IShellLinkW *This, LPCWSTR arguments)
{
    LINK *link = LINK_FROM(This, LinkW);
    return set_field(link, &link->Arguments, arguments);
}

static HRESULT STDMETHODCALLTYPE w_get_hotkey(IShellLinkW *This, WORD *hotkey)
{
    if (!hotkey)
        return E_POINTER;
    *hotkey = LINK_FROM(This, LinkW)->HotKey;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE w_set_hotkey(IShellLinkW *This, WORD hotkey)
{
    LINK *link = LINK_FROM(This, LinkW);
    if (link->HotKey != hotkey) {
        link->HotKey = hotkey;
        set_dirty(link);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE w_get_show_cmd(IShellLinkW *This, int *show)
{
    if (!show)
        return E_POINTER;
    *show = (int)LINK_FROM(This, LinkW)->ShowCmd;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE w_set_show_cmd(IShellLinkW *This, int show)
{
    LINK *link = LINK_FROM(This, LinkW);
    if ((int)link->ShowCmd != show) {
        link->ShowCmd = (DWORD)show;
        set_dirty(link);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE w_get_icon_location(IShellLinkW *This, LPWSTR path, int chars, int *index)
{
    LINK *link = LINK_FROM(This, LinkW);
    if (!path || !index)
        return E_POINTER;
    *index = link->IconIndex;
    return get_field(link->IconLocation, path, chars);
}

/* retain the supplied icon path and, where possible, an environment-variable form in a block. */
static HRESULT STDMETHODCALLTYPE w_set_icon_location(IShellLinkW *This, LPCWSTR path, int index)
{
    LINK *link = LINK_FROM(This, LinkW);
    if (path) {
        WCHAR unexpanded[MAX_PATH];
        if (PathUnExpandEnvStringsW(path, unexpanded, MAX_PATH)) {
            link->Flags |= SLDF_HAS_EXP_ICON_SZ;
            set_exp_block(link, EXP_SZ_ICON_SIG, unexpanded);
        } else {
            link->Flags &= ~SLDF_HAS_EXP_ICON_SZ;
            LkRemoveBlock(link, EXP_SZ_ICON_SIG);
        }
    }
    set_field(link, &link->IconLocation, path);
    if (link->IconIndex != index) {
        link->IconIndex = index;
        set_dirty(link);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE w_set_relative_path(IShellLinkW *This, LPCWSTR path, DWORD reserved)
{
    LINK *link = LINK_FROM(This, LinkW);
    LkSetString(&link->RelativeSource, path);
    return S_OK;
}

/* only check target existence; shell search would require user interaction and tracking. */
static HRESULT STDMETHODCALLTYPE w_resolve(IShellLinkW *This, HWND hwnd, DWORD flags)
{
    LINK *link = LINK_FROM(This, LinkW);
    WCHAR path[MAX_PATH];
    if (link->Flags & SLDF_PREFER_ENVIRONMENT_PATH) {
        if (!environment_path(link, path) || !PathFileExistsW(path))
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
        return S_OK;
    }
    if (!link->Pidl)
        return S_OK;
    if (!target_path(link, path))
        return S_OK;
    if (PathFileExistsW(path)) {
        if (!link->Dirty && (flags & SLR_UPDATE)) {
            read_find_data(link, path);
            if (link->Dirty && link->CurFile)
                link->File.lpVtbl->Save(&link->File, nullptr, TRUE);
        }
        return S_OK;
    }
    /* link info may locate the target on an existing volume. */
    if (link->Info && LkInfoPath(link->Info, link->InfoSize, path, MAX_PATH) && PathFileExistsW(path) &&
        SUCCEEDED(set_target(link, nullptr, path)))
        return S_OK;
    /* try the relative path when the link moved with its target. */
    if (link->RelativePath && (link->RelativeSource || link->CurFile)) {
        WCHAR base[MAX_PATH], combined[MAX_PATH];
        LkCopy(base, link->RelativeSource ? link->RelativeSource : link->CurFile, MAX_PATH);
        base[MAX_PATH - 1] = 0;
        PathRemoveFileSpecW(base);
        if (PathCombineW(combined, base, link->RelativePath) && PathFileExistsW(combined)) {
            if (SUCCEEDED(set_target(link, nullptr, combined)))
                return S_OK;
        }
    }
    /* a missing target without user interaction returns false, like canceled resolution. */
    return S_FALSE;
}

/* a path contains environment variables if expansion changes it. */
static HRESULT STDMETHODCALLTYPE w_set_path(IShellLinkW *This, LPCWSTR file)
{
    LINK *link = LINK_FROM(This, LinkW);
    WCHAR expanded[MAX_PATH];
    if (!file)
        return E_INVALIDARG;
    if (link->Flags & (SLDF_HAS_DARWINID | SLDF_DISABLE_LINK_PATH_TRACKING))
        return S_FALSE;
    if (!SHExpandEnvironmentStringsW(file, expanded, MAX_PATH))
        return E_FAIL;
    if (wcscmp(expanded, file)) {
        link->Flags |= SLDF_HAS_EXP_SZ;
        set_exp_block(link, EXP_SZ_LINK_SIG, file);
    } else {
        link->Flags &= ~(SLDF_HAS_EXP_SZ | SLDF_PREFER_ENVIRONMENT_PATH);
        LkRemoveBlock(link, EXP_SZ_LINK_SIG);
    }
    return set_target(link, nullptr, expanded);
}

static const IShellLinkWVtbl g_link_w_vtbl = {
    w_query_interface,        w_add_ref,         w_release,         w_get_path,
    w_get_id_list,            w_set_id_list,     w_get_description, w_set_description,
    w_get_working_directory,  w_set_working_directory, w_get_arguments, w_set_arguments,
    w_get_hotkey,             w_set_hotkey,      w_get_show_cmd,    w_set_show_cmd,
    w_get_icon_location,      w_set_icon_location, w_set_relative_path, w_resolve,
    w_set_path,
};

/* IShellLinkA. */

static IShellLinkW *wide_of(IShellLinkA *This)
{
    return &LINK_FROM(This, LinkA)->LinkW;
}

static HRESULT STDMETHODCALLTYPE a_get_path(IShellLinkA *This, LPSTR file, int chars, WIN32_FIND_DATAA *data,
                                            DWORD flags)
{
    IShellLinkW *w = wide_of(This);
    WCHAR path[MAX_PATH];
    WIN32_FIND_DATAW found;
    HRESULT hr = w->lpVtbl->GetPath(w, path, MAX_PATH, &found, flags);
    if (file)
        narrow(path, file, chars);
    if (data) {
        if (path[0]) {
            data->dwFileAttributes = found.dwFileAttributes;
            data->ftCreationTime = found.ftCreationTime;
            data->ftLastAccessTime = found.ftLastAccessTime;
            data->ftLastWriteTime = found.ftLastWriteTime;
            data->nFileSizeHigh = found.nFileSizeHigh;
            data->nFileSizeLow = found.nFileSizeLow;
            narrow(found.cFileName, data->cFileName, MAX_PATH);
        } else {
            memset(data, 0, sizeof(*data));
        }
    }
    return hr;
}

static HRESULT STDMETHODCALLTYPE a_get_id_list(IShellLinkA *This, PIDLIST_ABSOLUTE *pidl)
{
    IShellLinkW *w = wide_of(This);
    return w->lpVtbl->GetIDList(w, pidl);
}

static HRESULT STDMETHODCALLTYPE a_set_id_list(IShellLinkA *This, PCIDLIST_ABSOLUTE pidl)
{
    IShellLinkW *w = wide_of(This);
    return w->lpVtbl->SetIDList(w, pidl);
}

/* convert the wide getter result using the caller's buffer size. */
#define ANSI_GETTER(name, method)                                                                                  \
    static HRESULT STDMETHODCALLTYPE a_##name(IShellLinkA *This, LPSTR out, int chars)                             \
    {                                                                                                              \
        IShellLinkW *w = wide_of(This);                                                                            \
        if (chars < 1) {                                                                                           \
            if (chars && out)                                                                                      \
                *out = 0;                                                                                          \
            return E_INVALIDARG;                                                                                   \
        }                                                                                                          \
        LPWSTR wide = LkAlloc((SIZE_T)chars * sizeof(WCHAR));                                                      \
        if (!wide)                                                                                                 \
            return E_OUTOFMEMORY;                                                                                  \
        HRESULT hr = w->lpVtbl->method(w, wide, chars);                                                            \
        if (SUCCEEDED(hr))                                                                                         \
            narrow(wide, out, chars);                                                                              \
        LkFree(wide);                                                                                              \
        return hr;                                                                                                 \
    }

ANSI_GETTER(get_description, GetDescription)
ANSI_GETTER(get_working_directory, GetWorkingDirectory)
ANSI_GETTER(get_arguments, GetArguments)

#define ANSI_SETTER(name, method)                                                                                  \
    static HRESULT STDMETHODCALLTYPE a_##name(IShellLinkA *This, LPCSTR text)                                      \
    {                                                                                                              \
        IShellLinkW *w = wide_of(This);                                                                            \
        LPWSTR wide = widen(text);                                                                                 \
        if (text && !wide)                                                                                         \
            return E_OUTOFMEMORY;                                                                                  \
        HRESULT hr = w->lpVtbl->method(w, wide);                                                                   \
        LkFree(wide);                                                                                              \
        return hr;                                                                                                 \
    }

ANSI_SETTER(set_description, SetDescription)
ANSI_SETTER(set_working_directory, SetWorkingDirectory)
ANSI_SETTER(set_arguments, SetArguments)
ANSI_SETTER(set_path, SetPath)

static HRESULT STDMETHODCALLTYPE a_get_hotkey(IShellLinkA *This, WORD *hotkey)
{
    IShellLinkW *w = wide_of(This);
    return w->lpVtbl->GetHotkey(w, hotkey);
}

static HRESULT STDMETHODCALLTYPE a_set_hotkey(IShellLinkA *This, WORD hotkey)
{
    IShellLinkW *w = wide_of(This);
    return w->lpVtbl->SetHotkey(w, hotkey);
}

static HRESULT STDMETHODCALLTYPE a_get_show_cmd(IShellLinkA *This, int *show)
{
    IShellLinkW *w = wide_of(This);
    return w->lpVtbl->GetShowCmd(w, show);
}

static HRESULT STDMETHODCALLTYPE a_set_show_cmd(IShellLinkA *This, int show)
{
    IShellLinkW *w = wide_of(This);
    return w->lpVtbl->SetShowCmd(w, show);
}

static HRESULT STDMETHODCALLTYPE a_get_icon_location(IShellLinkA *This, LPSTR path, int chars, int *index)
{
    IShellLinkW *w = wide_of(This);
    if (!path || !index)
        return E_POINTER;
    if (chars < 1) {
        *index = 0;
        return E_INVALIDARG;
    }
    LPWSTR wide = LkAlloc((SIZE_T)chars * sizeof(WCHAR));
    if (!wide)
        return E_OUTOFMEMORY;
    HRESULT hr = w->lpVtbl->GetIconLocation(w, wide, chars, index);
    if (SUCCEEDED(hr))
        narrow(wide, path, chars);
    LkFree(wide);
    return hr;
}

static HRESULT STDMETHODCALLTYPE a_set_icon_location(IShellLinkA *This, LPCSTR path, int index)
{
    IShellLinkW *w = wide_of(This);
    LPWSTR wide = widen(path);
    if (path && !wide)
        return E_OUTOFMEMORY;
    HRESULT hr = w->lpVtbl->SetIconLocation(w, wide, index);
    LkFree(wide);
    return hr;
}

static HRESULT STDMETHODCALLTYPE a_set_relative_path(IShellLinkA *This, LPCSTR path, DWORD reserved)
{
    IShellLinkW *w = wide_of(This);
    LPWSTR wide = widen(path);
    if (path && !wide)
        return E_OUTOFMEMORY;
    HRESULT hr = w->lpVtbl->SetRelativePath(w, wide, reserved);
    LkFree(wide);
    return hr;
}

static HRESULT STDMETHODCALLTYPE a_resolve(IShellLinkA *This, HWND hwnd, DWORD flags)
{
    IShellLinkW *w = wide_of(This);
    return w->lpVtbl->Resolve(w, hwnd, flags);
}

static const IShellLinkAVtbl g_link_a_vtbl = {
    a_query_interface,       a_add_ref,          a_release,         a_get_path,
    a_get_id_list,           a_set_id_list,      a_get_description, a_set_description,
    a_get_working_directory, a_set_working_directory, a_get_arguments, a_set_arguments,
    a_get_hotkey,            a_set_hotkey,       a_get_show_cmd,    a_set_show_cmd,
    a_get_icon_location,     a_set_icon_location, a_set_relative_path, a_resolve,
    a_set_path,
};

/* the property store. */

/* lazily load the property store from its data block. */
static HRESULT ensure_props(LINK *link)
{
    if (link->Props)
        return S_OK;
    IPropertyStore *store;
    HRESULT hr = PSCreateMemoryPropertyStore(&IID_IPropertyStore, (void **)&store);
    if (FAILED(hr))
        return hr;
    IPersistSerializedPropStorage *serialized;
    hr = store->lpVtbl->QueryInterface(store, &IID_IPersistSerializedPropStorage, (void **)&serialized);
    if (FAILED(hr)) {
        store->lpVtbl->Release(store);
        return hr;
    }
    LINK_BLOCK *block = LkFindBlock(link, EXP_PROPERTYSTORAGE_SIG);
    if (block && block->Size > 8)
        serialized->lpVtbl->SetPropertyStorage(serialized, (PCUSERIALIZEDPROPSTORAGE)(block->Data + 8),
                                               block->Size - 8);
    link->Props = store;
    link->Serialized = serialized;
    return S_OK;
}

/* write the property store into its block before saving the shortcut. */
static HRESULT update_props_block(LINK *link)
{
    if (!link->Serialized)
        return S_OK;
    SERIALIZEDPROPSTORAGE *storage = nullptr;
    DWORD size = 0;
    HRESULT hr = link->Serialized->lpVtbl->GetPropertyStorage(link->Serialized, &storage, &size);
    if (FAILED(hr))
        return hr;
    BYTE *block = LkAlloc((SIZE_T)size + 8);
    if (!block) {
        CoTaskMemFree(storage);
        return E_OUTOFMEMORY;
    }
    memcpy(block, &(DWORD){size + 8}, 4);
    memcpy(block + 4, &(DWORD){EXP_PROPERTYSTORAGE_SIG}, 4);
    memcpy(block + 8, storage, size);
    CoTaskMemFree(storage);
    BOOL was_dirty = link->Dirty;
    hr = LkSetBlock(link, block);
    link->Dirty = was_dirty;
    LkFree(block);
    return hr;
}

static HRESULT STDMETHODCALLTYPE store_get_count(IPropertyStore *This, DWORD *count)
{
    LINK *link = LINK_FROM(This, Store);
    if (!count)
        return E_POINTER;
    *count = 0;
    HRESULT hr = ensure_props(link);
    if (SUCCEEDED(hr))
        link->Props->lpVtbl->GetCount(link->Props, count);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE store_get_at(IPropertyStore *This, DWORD index, PROPERTYKEY *key)
{
    LINK *link = LINK_FROM(This, Store);
    HRESULT hr = ensure_props(link);
    if (FAILED(hr))
        return hr;
    return link->Props->lpVtbl->GetAt(link->Props, index, key);
}

static HRESULT STDMETHODCALLTYPE store_get_value(IPropertyStore *This, REFPROPERTYKEY key, PROPVARIANT *value)
{
    LINK *link = LINK_FROM(This, Store);
    memset(value, 0, sizeof(*value));
    HRESULT hr = ensure_props(link);
    if (FAILED(hr))
        return hr;
    return link->Props->lpVtbl->GetValue(link->Props, key, value);
}

/* properties are written only to a shortcut opened for writing. */
static HRESULT STDMETHODCALLTYPE store_set_value(IPropertyStore *This, REFPROPERTYKEY key, const PROPVARIANT *value)
{
    LINK *link = LINK_FROM(This, Store);
    if (!(link->Mode & 3))
        return STG_E_ACCESSDENIED;
    HRESULT hr = ensure_props(link);
    if (FAILED(hr))
        return hr;
    hr = link->Props->lpVtbl->SetValue(link->Props, key, value);
    if (SUCCEEDED(hr))
        set_dirty(link);
    return hr;
}

/* save writable shortcuts back to their file. */
static HRESULT STDMETHODCALLTYPE store_commit(IPropertyStore *This)
{
    LINK *link = LINK_FROM(This, Store);
    if (!(link->Mode & 3))
        return STG_E_ACCESSDENIED;
    if (!link->Dirty || !link->CurFile)
        return S_OK;
    return link->File.lpVtbl->Save(&link->File, nullptr, TRUE);
}

static const IPropertyStoreVtbl g_store_vtbl = {
    store_query_interface, store_add_ref, store_release, store_get_count,
    store_get_at,          store_get_value, store_set_value, store_commit,
};

/* the data list. */

static HRESULT STDMETHODCALLTYPE list_add_data_block(IShellLinkDataList *This, void *block)
{
    LkSetBlock(LINK_FROM(This, DataList), block);
    return S_OK;
}

/* a copy of a block, which the caller frees with LocalFree. */
static HRESULT STDMETHODCALLTYPE list_copy_data_block(IShellLinkDataList *This, DWORD signature, void **block)
{
    LINK *link = LINK_FROM(This, DataList);
    *block = nullptr;
    LINK_BLOCK *found = LkFindBlock(link, signature);
    if (!found)
        return E_FAIL;
    void *copy = LocalAlloc(LPTR, found->Size);
    if (!copy)
        return E_OUTOFMEMORY;
    memcpy(copy, found->Data, found->Size);
    *block = copy;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE list_remove_data_block(IShellLinkDataList *This, DWORD signature)
{
    LkRemoveBlock(LINK_FROM(This, DataList), signature);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE list_get_flags(IShellLinkDataList *This, DWORD *flags)
{
    *flags = LINK_FROM(This, DataList)->Flags;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE list_set_flags(IShellLinkDataList *This, DWORD flags)
{
    LINK *link = LINK_FROM(This, DataList);
    if (link->Flags == flags)
        return S_FALSE;
    link->Flags = flags;
    set_dirty(link);
    return S_OK;
}

static const IShellLinkDataListVtbl g_data_list_vtbl = {
    list_query_interface,    list_add_ref,   list_release,       list_add_data_block, list_copy_data_block,
    list_remove_data_block,  list_get_flags, list_set_flags,
};

/* the site. */

static HRESULT STDMETHODCALLTYPE site_set_site(IObjectWithSite *This, IUnknown *site)
{
    LINK *link = LINK_FROM(This, Site);
    if (site)
        site->lpVtbl->AddRef(site);
    if (link->SiteObject)
        link->SiteObject->lpVtbl->Release(link->SiteObject);
    link->SiteObject = site;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE site_get_site(IObjectWithSite *This, REFIID riid, void **site)
{
    LINK *link = LINK_FROM(This, Site);
    if (!site)
        return E_POINTER;
    *site = nullptr;
    if (!link->SiteObject)
        return E_FAIL;
    return link->SiteObject->lpVtbl->QueryInterface(link->SiteObject, riid, site);
}

static const IObjectWithSiteVtbl g_site_vtbl = {
    site_query_interface, site_add_ref, site_release, site_set_site, site_get_site,
};

/* persistence. */

/* serialize after updating the relative path and property block. */
static BYTE *serialize(LINK *link, LPCWSTR file, SIZE_T *size, HRESULT *hr)
{
    LPCWSTR from = link->RelativeSource ? link->RelativeSource : file;
    WCHAR path[MAX_PATH], relative[MAX_PATH];
    if (from && target_path(link, path))
        LkSetString(&link->RelativePath,
                    PathRelativePathToW(relative, from, 0, path, link->Attributes) ? relative : nullptr);
    *hr = update_props_block(link);
    if (FAILED(*hr))
        return nullptr;
    BYTE *bytes = LkBuild(link, size);
    if (!bytes)
        *hr = E_FAIL;
    return bytes;
}

static HRESULT write_file(LPCWSTR path, const BYTE *bytes, SIZE_T size)
{
    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());
    DWORD written = 0;
    BOOL ok = WriteFile(file, bytes, (DWORD)size, &written, nullptr) && written == size;
    DWORD error = ok ? 0 : GetLastError();
    CloseHandle(file);
    if (!ok) {
        DeleteFileW(path);
        return HRESULT_FROM_WIN32(error ? error : ERROR_WRITE_FAULT);
    }
    return S_OK;
}

static HRESULT file_save(LINK *link, LPCWSTR file, BOOL remember)
{
    WCHAR path[MAX_PATH];
    if (file) {
        if (PathIsRelativeW(file))
            return E_INVALIDARG;
        LkCopy(path, file, MAX_PATH);
        path[MAX_PATH - 1] = 0;
    } else if (link->CurFile) {
        wcscpy(path, link->CurFile);
    } else {
        return link->Store.lpVtbl->Commit(&link->Store);
    }
    BOOL existed = GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
    SIZE_T size;
    HRESULT hr;
    BYTE *bytes = serialize(link, path, &size, &hr);
    if (!bytes)
        return hr;
    hr = write_file(path, bytes, size);
    LkFree(bytes);
    if (FAILED(hr))
        return hr;
    SHChangeNotify(existed ? SHCNE_UPDATEITEM : SHCNE_CREATE, SHCNF_PATH, path, nullptr);
    if (remember) {
        LkSetString(&link->CurFile, path);
        link->Dirty = FALSE;
    }
    return S_OK;
}

static HRESULT file_load(LINK *link, LPCWSTR file, DWORD mode)
{
    if (!file)
        return E_INVALIDARG;
    if ((mode & 3) == STGM_WRITE)
        mode = (mode & ~3u) | STGM_READWRITE;
    HANDLE handle = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());
    LARGE_INTEGER length;
    HRESULT hr = E_FAIL;
    if (GetFileSizeEx(handle, &length) && length.QuadPart < 0x200000) {
        BYTE *bytes = LkAlloc((SIZE_T)length.QuadPart);
        DWORD read = 0;
        if (!bytes) {
            hr = E_OUTOFMEMORY;
        } else if (length.QuadPart && !ReadFile(handle, bytes, (DWORD)length.QuadPart, &read, nullptr)) {
            hr = HRESULT_FROM_WIN32(GetLastError());
        } else {
            hr = LkParse(link, bytes, read);
        }
        LkFree(bytes);
    }
    CloseHandle(handle);
    if (FAILED(hr)) {
        LkReset(link);
        return hr;
    }
    link->Mode = mode;
    LkSetString(&link->CurFile, file);
    /* expand environment variables in icon paths. */
    LINK_BLOCK *icon = (link->Flags & SLDF_HAS_EXP_ICON_SZ) ? LkFindBlock(link, EXP_SZ_ICON_SIG) : nullptr;
    if (icon && icon->Size >= sizeof(EXP_SZ_LINK)) {
        WCHAR expanded[MAX_PATH];
        if (ExpandEnvironmentStringsW(((const EXP_SZ_LINK *)icon->Data)->swzTarget, expanded, MAX_PATH))
            LkSetString(&link->IconLocation, expanded);
    }
    /* environment and relative target paths may not have an item list yet. */
    if (!link->Pidl) {
        WCHAR path[MAX_PATH];
        if ((link->Flags & SLDF_PREFER_ENVIRONMENT_PATH) && environment_path(link, path)) {
            link->Pidl = SHSimpleIDListFromPath(path);
        } else {
            LINK_BLOCK *vista = LkFindBlock(link, LNK_SIG_VISTA_IDLIST);
            if (vista && vista->Size > 10)
                link->Pidl = ILClone((PCUIDLIST_RELATIVE)(vista->Data + 8));
        }
        if (!link->Pidl && link->RelativePath) {
            WCHAR base[MAX_PATH];
            LkCopy(base, file, MAX_PATH);
            base[MAX_PATH - 1] = 0;
            PathRemoveFileSpecW(base);
            if (PathCombineW(path, base, link->RelativePath))
                link->Pidl = SHSimpleIDListFromPath(path);
        }
    }
    link->Dirty = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE file_get_class_id(IPersistFile *This, CLSID *clsid)
{
    if (!clsid)
        return E_POINTER;
    *clsid = CLSID_ShellLink;
    return S_OK;
}

/* S_OK for a shortcut with changes, S_FALSE for one without. */
static HRESULT STDMETHODCALLTYPE file_is_dirty(IPersistFile *This)
{
    return LINK_FROM(This, File)->Dirty ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE file_load_method(IPersistFile *This, LPCOLESTR file, DWORD mode)
{
    return file_load(LINK_FROM(This, File), file, mode);
}

static HRESULT STDMETHODCALLTYPE file_save_method(IPersistFile *This, LPCOLESTR file, BOOL remember)
{
    return file_save(LINK_FROM(This, File), file, remember);
}

static HRESULT STDMETHODCALLTYPE file_save_completed(IPersistFile *This, LPCOLESTR file)
{
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE file_get_cur_file(IPersistFile *This, LPOLESTR *file)
{
    LINK *link = LINK_FROM(This, File);
    if (!file)
        return E_POINTER;
    *file = nullptr;
    if (!link->CurFile)
        return S_FALSE;
    SIZE_T bytes = (wcslen(link->CurFile) + 1) * sizeof(WCHAR);
    *file = CoTaskMemAlloc(bytes);
    if (!*file)
        return E_OUTOFMEMORY;
    memcpy(*file, link->CurFile, bytes);
    return S_OK;
}

static const IPersistFileVtbl g_file_vtbl = {
    file_query_interface, file_add_ref,        file_release,       file_get_class_id,
    file_is_dirty,        file_load_method,    file_save_method,   file_save_completed,
    file_get_cur_file,
};

static HRESULT STDMETHODCALLTYPE stream_get_class_id(IPersistStream *This, CLSID *clsid)
{
    if (!clsid)
        return E_POINTER;
    *clsid = CLSID_ShellLink;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_is_dirty(IPersistStream *This)
{
    return LINK_FROM(This, Stream)->Dirty ? S_OK : S_FALSE;
}

/* read the rest of the stream as a shortcut file. */
static HRESULT STDMETHODCALLTYPE stream_load(IPersistStream *This, IStream *stream)
{
    LINK *link = LINK_FROM(This, Stream);
    if (!stream)
        return E_POINTER;
    SIZE_T capacity = 4096, used = 0;
    BYTE *bytes = LkAlloc(capacity);
    if (!bytes)
        return E_OUTOFMEMORY;
    for (;;) {
        ULONG read = 0;
        HRESULT hr = stream->lpVtbl->Read(stream, bytes + used, (ULONG)(capacity - used), &read);
        if (FAILED(hr)) {
            LkFree(bytes);
            return hr;
        }
        used += read;
        if (!read)
            break;
        if (used == capacity) {
            if (capacity >= 0x200000) {
                LkFree(bytes);
                return E_FAIL;
            }
            BYTE *grown = HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes, capacity * 2);
            if (!grown) {
                LkFree(bytes);
                return E_OUTOFMEMORY;
            }
            bytes = grown;
            capacity *= 2;
        }
    }
    HRESULT hr = LkParse(link, bytes, used);
    LkFree(bytes);
    if (FAILED(hr))
        LkReset(link);
    return hr;
}

static HRESULT STDMETHODCALLTYPE stream_save(IPersistStream *This, IStream *stream, BOOL clear_dirty)
{
    LINK *link = LINK_FROM(This, Stream);
    if (!stream)
        return E_POINTER;
    SIZE_T size;
    HRESULT hr;
    BYTE *bytes = serialize(link, nullptr, &size, &hr);
    if (!bytes)
        return hr;
    ULONG written = 0;
    hr = stream->lpVtbl->Write(stream, bytes, (ULONG)size, &written);
    LkFree(bytes);
    if (FAILED(hr))
        return hr;
    if (written != size)
        return E_FAIL;
    if (clear_dirty)
        link->Dirty = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE stream_get_size_max(IPersistStream *This, ULARGE_INTEGER *size)
{
    LINK *link = LINK_FROM(This, Stream);
    if (!size)
        return E_POINTER;
    SIZE_T bytes;
    HRESULT hr;
    BYTE *built = serialize(link, nullptr, &bytes, &hr);
    if (!built)
        return hr;
    LkFree(built);
    size->QuadPart = bytes;
    return S_OK;
}

static const IPersistStreamVtbl g_stream_vtbl = {
    stream_query_interface, stream_add_ref, stream_release,      stream_get_class_id, stream_is_dirty,
    stream_load,            stream_save,    stream_get_size_max,
};
