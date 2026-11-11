/* the settings of the shell, the data of an item of a folder, the unique names and the about box. */
#include "shell32_private.h"
#include <mime/win32/shlwapi.h>

/* the SHELLSTATE of the sdk: a word of flags, three numbers, the sorting, the version and more flags. */
typedef struct SHELL_STATE {
    DWORD Flags;
    DWORD Unused1;
    UINT Unused2;
    LONG SortColumn;
    int SortDirection;
    UINT Version;
    UINT Unused3;
    DWORD Flags2;
} SHELL_STATE;

typedef struct SHELL_FLAG_STATE {
    WORD Flags;
} SHELL_FLAG_STATE;

/* the bits of the flags the state names, with the mask bit that selects each. */
enum {
    F_SHOWALLOBJECTS = 1 << 0, F_SHOWEXTENSIONS = 1 << 1, F_NOCONFIRMRECYCLE = 1 << 2, F_SHOWSYSFILES = 1 << 3,
    F_SHOWCOMPCOLOR = 1 << 4, F_DOUBLECLICKINWEBVIEW = 1 << 5, F_DESKTOPHTML = 1 << 6, F_WIN95CLASSIC = 1 << 7,
    F_DONTPRETTYPATH = 1 << 8, F_SHOWATTRIBCOL = 1 << 9, F_MAPNETDRVBUTTON = 1 << 10, F_SHOWINFOTIP = 1 << 11,
    F_HIDEICONS = 1 << 12, F_WEBVIEW = 1 << 13, F_FILTER = 1 << 14, F_SHOWSUPERHIDDEN = 1 << 15,
    F_NONETCRAWLING = 1 << 16,
};

enum {
    G_SEPPROCESS = 1 << 0, G_STARTPANELON = 1 << 1, G_SHOWSTARTPAGE = 1 << 2, G_AUTOCHECKSELECT = 1 << 3,
    G_ICONSONLY = 1 << 4, G_SHOWTYPEOVERLAY = 1 << 5, G_SHOWSTATUSBAR = 1 << 6,
};

static const struct {
    DWORD Mask;
    DWORD Bit;
    BOOL Second;
} g_fields[] = {
    {0x00000001, F_SHOWALLOBJECTS, 0},      {0x00000002, F_SHOWEXTENSIONS, 0},     {0x00008000, F_NOCONFIRMRECYCLE, 0},
    {0x00000020, F_SHOWSYSFILES, 0},        {0x00000008, F_SHOWCOMPCOLOR, 0},      {0x00000080, F_DOUBLECLICKINWEBVIEW, 0},
    {0x00000400, F_WIN95CLASSIC, 0},        {0x00000800, F_DONTPRETTYPATH, 0},     {0x00002000, F_SHOWINFOTIP, 0},
    {0x00001000, F_MAPNETDRVBUTTON, 0},     {0x00004000, F_HIDEICONS, 0},          {0x00020000, F_WEBVIEW, 0},
    {0x00010000, F_FILTER, 0},              {0x00040000, F_SHOWSUPERHIDDEN, 0},    {0x00100000, F_NONETCRAWLING, 0},
    {0x00080000, G_SEPPROCESS, 1},          {0x00200000, G_STARTPANELON, 1},       {0x00400000, G_SHOWSTARTPAGE, 1},
    {0x00800000, G_AUTOCHECKSELECT, 1},     {0x01000000, G_ICONSONLY, 1},          {0x02000000, G_SHOWTYPEOVERLAY, 1},
    {0x04000000, G_SHOWSTATUSBAR, 1},
};

static SRWLOCK g_lock = SRWLOCK_INIT;
static SHELL_STATE g_state;
static BOOL g_loaded;

static const WCHAR g_advanced[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";

/* the advanced values the user's settings keep, and the bit each is the state of. inverted ones are on when 0. */
static const struct {
    PCWSTR Name;
    DWORD Bit;
    BOOL Second;
    BOOL Inverted;
} g_advanced_values[] = {
    {L"HideFileExt", F_SHOWEXTENSIONS, 0, 1}, {L"ShowSuperHidden", F_SHOWSUPERHIDDEN, 0, 0},
    {L"ShowInfoTip", F_SHOWINFOTIP, 0, 0},    {L"ShowStatusBar", G_SHOWSTATUSBAR, 1, 0},
    {L"ShowTypeOverlay", G_SHOWTYPEOVERLAY, 1, 0}, {L"AutoCheckSelect", G_AUTOCHECKSELECT, 1, 0},
    {L"IconsOnly", G_ICONSONLY, 1, 0},        {L"NoNetCrawling", F_NONETCRAWLING, 0, 0},
    {L"ShowCompColor", F_SHOWCOMPCOLOR, 0, 0}, {L"ShowSuperHidden", F_SHOWSUPERHIDDEN, 0, 0},
};

static DWORD *flags_of(BOOL second)
{
    return second ? &g_state.Flags2 : &g_state.Flags;
}

/* what a fresh account has: hidden files and the extensions of known files are not shown. */
static void load_locked(void)
{
    if (g_loaded)
        return;
    g_loaded = TRUE;
    memset(&g_state, 0, sizeof(g_state));
    g_state.Flags = F_SHOWCOMPCOLOR | F_DOUBLECLICKINWEBVIEW | F_SHOWINFOTIP | F_WEBVIEW;
    g_state.Flags2 = G_STARTPANELON | G_SHOWSTARTPAGE | G_SHOWTYPEOVERLAY | G_SHOWSTATUSBAR;
    g_state.Version = 10;
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, g_advanced, 0, KEY_QUERY_VALUE, &key))
        return;
    DWORD value, size = sizeof(value), type;
    if (!RegQueryValueExW(key, L"Hidden", nullptr, &type, (BYTE *)&value, &size) && type == REG_DWORD) {
        if (value == 1)
            g_state.Flags |= F_SHOWALLOBJECTS;
        else
            g_state.Flags &= ~(DWORD)F_SHOWALLOBJECTS;
    }
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(g_advanced_values); i++) {
        size = sizeof(value);
        if (RegQueryValueExW(key, g_advanced_values[i].Name, nullptr, &type, (BYTE *)&value, &size) || type != REG_DWORD)
            continue;
        DWORD *flags = flags_of(g_advanced_values[i].Second);
        if ((value != 0) != (g_advanced_values[i].Inverted != 0))
            *flags |= g_advanced_values[i].Bit;
        else
            *flags &= ~g_advanced_values[i].Bit;
    }
    RegCloseKey(key);
}

static void save_locked(void)
{
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, g_advanced, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr))
        return;
    DWORD value = g_state.Flags & F_SHOWALLOBJECTS ? 1 : 2;
    RegSetValueExW(key, L"Hidden", 0, REG_DWORD, (BYTE *)&value, sizeof(value));
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(g_advanced_values); i++) {
        BOOL on = (*flags_of(g_advanced_values[i].Second) & g_advanced_values[i].Bit) != 0;
        value = on != (g_advanced_values[i].Inverted != 0);
        RegSetValueExW(key, g_advanced_values[i].Name, 0, REG_DWORD, (BYTE *)&value, sizeof(value));
    }
    RegCloseKey(key);
}

/* the settings named by the mask are read into the state, or written from it. the rest of the state is left alone. */
SHSTDAPI void WINAPI SHGetSetSettings(LPSHELLSTATEW state_out, DWORD mask, BOOL set)
{
    SHELL_STATE *state = (SHELL_STATE *)state_out;
    if (!state && !mask && set)
        return;
    AcquireSRWLockExclusive(&g_lock);
    load_locked();
    BOOL changed = FALSE;
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(g_fields); i++) {
        if (!(mask & g_fields[i].Mask))
            continue;
        DWORD *mine = flags_of(g_fields[i].Second), *theirs = g_fields[i].Second ? &state->Flags2 : &state->Flags;
        if (set) {
            if ((*mine ^ *theirs) & g_fields[i].Bit)
                changed = TRUE;
            *mine = (*mine & ~g_fields[i].Bit) | (*theirs & g_fields[i].Bit);
        } else {
            *theirs = (*theirs & ~g_fields[i].Bit) | (*mine & g_fields[i].Bit);
        }
    }
    if (mask & SSF_SORTCOLUMNS) {
        if (set) {
            g_state.SortColumn = state->SortColumn;
            g_state.SortDirection = state->SortDirection;
        } else {
            state->SortColumn = g_state.SortColumn;
            state->SortDirection = g_state.SortDirection;
        }
    }
    if (set && changed)
        save_locked();
    ReleaseSRWLockExclusive(&g_lock);
}

/* the older form: the flags in a word, with the check boxes of the folder options at the top of it. */
SHSTDAPI void WINAPI SHGetSettings(LPSHELLFLAGSTATE flags_out, DWORD mask)
{
    if (!flags_out)
        return;
    SHELL_STATE state;
    memset(&state, 0, sizeof(state));
    SHGetSetSettings((LPSHELLSTATEW)&state, mask & 0xFFFFFFEB, FALSE);
    DWORD flags = state.Flags & 0xFFFF;
    flags &= ~0x6000u;
    flags |= (state.Flags2 & 0x18) << 10;
    *(WORD *)flags_out = (WORD)flags;
}

/* ------------------------------------------------------------------ the data of an item */

#define SHGDFIL_FINDDATA 1
#define SHGDFIL_NETRESOURCE 2
#define SHGDFIL_DESCRIPTIONID 3

typedef struct SH_DESCRIPTION_ID {
    DWORD Id;
    CLSID Clsid;
} SH_DESCRIPTION_ID;

#define SHDID_FS_FILE 2
#define SHDID_FS_DIRECTORY 3
#define SHDID_ROOT_REGITEM 1

/* of an item of a file system folder: its find data, or the kind of the item. */
SHSTDAPI HRESULT WINAPI SHGetDataFromIDListW(IShellFolder *folder, PCUITEMID_CHILD pidl, int format, void *data, int size)
{
    if (!data || !folder || !pidl)
        return E_INVALIDARG;
    memset(data, 0, (SIZE_T)size);
    if (format < SHGDFIL_FINDDATA || format > SHGDFIL_DESCRIPTIONID)
        return E_INVALIDARG;
    if ((format == SHGDFIL_FINDDATA && size < (int)sizeof(WIN32_FIND_DATAW)) ||
        (format == SHGDFIL_DESCRIPTIONID && size < (int)sizeof(SH_DESCRIPTION_ID)))
        return E_INVALIDARG;
    if (format == SHGDFIL_NETRESOURCE)
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    STRRET name;
    HRESULT hr = folder->lpVtbl->GetDisplayNameOf(folder, pidl, SHGDN_FORPARSING, &name);
    if (FAILED(hr))
        return hr;
    WCHAR path[MAX_PATH];
    hr = StrRetToBufW(&name, pidl, path, RTL_NUMBER_OF(path));
    if (FAILED(hr))
        return hr;
    WIN32_FIND_DATAW found;
    HANDLE find = FindFirstFileW(path, &found);
    if (find == INVALID_HANDLE_VALUE) {
        DWORD attributes = GetFileAttributesW(path);
        if (attributes == INVALID_FILE_ATTRIBUTES)
            return HRESULT_FROM_WIN32(GetLastError());
        memset(&found, 0, sizeof(found));
        found.dwFileAttributes = attributes;
    } else {
        FindClose(find);
    }
    if (format == SHGDFIL_FINDDATA) {
        memcpy(data, &found, sizeof(found));
        return S_OK;
    }
    ((SH_DESCRIPTION_ID *)data)->Id = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? SHDID_FS_DIRECTORY : SHDID_FS_FILE;
    return S_OK;
}

/* ------------------------------------------------------------------ unique names */

/*
 * a name that is not taken: the template, or the long template when there is one, with a number in its parentheses or
 * before its extension, and the directory in front. the numbers go up from start, and the one used is told.
 */
static BOOL make_unique(WCHAR *unique, UINT size, const WCHAR *name, const WCHAR *directory, int start, int *used)
{
    if (used)
        *used = 0;
    if (!size || !unique)
        return FALSE;
    unique[0] = 0;
    WCHAR *end = unique;
    SIZE_T directory_length = 0;
    if (directory) {
        directory_length = wcslen(directory);
        if (directory_length + 1 >= size)
            return FALSE;
        wcscpy(unique, directory);
        end = unique + directory_length;
        if (directory_length && end[-1] != L'\\') {
            *end++ = L'\\';
            *end = 0;
            directory_length++;
        }
    }
    /* a number already in parentheses is the one to count up, else it goes before the extension. */
    const WCHAR *open = nullptr;
    for (const WCHAR *p = wcschr(name, L'('); p; p = wcschr(p + 1, L'(')) {
        const WCHAR *digits = p + 1;
        while (*digits >= L'0' && *digits <= L'9')
            digits++;
        if (*digits == L')') {
            open = p;
            break;
        }
    }
    SIZE_T prefix;
    const WCHAR *suffix;
    WCHAR format[32];
    if (open) {
        prefix = (SIZE_T)(open + 1 - name);
        suffix = open + 1;
        while (*suffix >= L'0' && *suffix <= L'9')
            suffix++;
        wcscpy(format, L"%d");
    } else {
        suffix = PathFindExtensionW(name);
        prefix = (SIZE_T)(suffix - name);
        wcscpy(format, L" (%d)");
    }
    SIZE_T room = size - directory_length;
    if (prefix + wcslen(suffix) + 12 > room)
        return FALSE;
    for (int n = start; n < start + 10000; n++) {
        memcpy(end, name, prefix * sizeof(WCHAR));
        WCHAR number[32];
        _snwprintf(number, RTL_NUMBER_OF(number), format, n);
        if (prefix + wcslen(number) + wcslen(suffix) + 1 > room)
            return FALSE;
        wcscpy(end + prefix, number);
        wcscat(end, suffix);
        if (GetFileAttributesW(unique) == INVALID_FILE_ATTRIBUTES) {
            if (used)
                *used = n;
            return TRUE;
        }
    }
    unique[0] = 0;
    return FALSE;
}

SHSTDAPI BOOL WINAPI PathMakeUniqueName(PWSTR unique, UINT size, PCWSTR name, PCWSTR long_name, PCWSTR directory)
{
    if (!name && !long_name)
        return FALSE;
    return make_unique(unique, size, long_name ? long_name : name, directory, 1, nullptr);
}

#define PCS_FATAL 0x80000000
#define PCS_REPLACEDCHAR 0x00000001
#define PCS_REMOVEDCHAR 0x00000002
#define PCS_PATHTOOLONG 0x00000008

/* windows keeps the characters a long name allows, makes a slash a dash and drops the rest. */
SHSTDAPI int WINAPI PathCleanupSpec(PCWSTR directory, PWSTR spec)
{
    int result = 0;
    PWSTR out = spec;
    for (PCWSTR in = spec; *in; in++) {
        if (PathGetCharTypeW(*in) & GCT_LFNCHAR) {
            *out++ = *in;
        } else if (*in == L' ' || *in == L'/') {
            *out++ = L'-';
            result |= PCS_REPLACEDCHAR;
        } else {
            result |= PCS_REMOVEDCHAR;
        }
    }
    *out = 0;
    if (directory && wcslen(directory) + wcslen(spec) >= MAX_PATH)
        result |= PCS_FATAL | PCS_PATHTOOLONG;
    return result;
}

/* ------------------------------------------------------------------ about box */

SHSTDAPI int WINAPI ShellAboutW(HWND hwnd, PCWSTR title, PCWSTR other, HICON icon)
{
    WCHAR caption[256], text[1024], version[128] = L"";
    RTL_OSVERSIONINFOEXW info = {sizeof(info)};
    if (RtlGetVersion(&info) == 0)
        _snwprintf(version, RTL_NUMBER_OF(version), L"Microsoft Windows\nVersion %u.%u (Build %u)", info.dwMajorVersion,
                   info.dwMinorVersion, info.dwBuildNumber);
    /* the title may carry "name#other": the caption names the program, the first line the other text. */
    WCHAR name[128];
    lstrcpynW(name, title ? title : L"", RTL_NUMBER_OF(name));
    WCHAR *separator = wcschr(name, L'#');
    if (separator)
        *separator = 0;
    _snwprintf(caption, RTL_NUMBER_OF(caption), L"About %s", name);
    _snwprintf(text, RTL_NUMBER_OF(text), L"%s\n\n%s%s%s", separator ? separator + 1 : name, version, other ? L"\n\n" : L"",
               other ? other : L"");
    MessageBoxW(hwnd, text, caption, MB_OK | MB_ICONINFORMATION);
    return TRUE;
}

SHSTDAPI int WINAPI ShellAboutA(HWND hwnd, PCSTR title, PCSTR other, HICON icon)
{
    PWSTR wide_title = ShWiden(title), wide_other = ShWiden(other);
    int result = ShellAboutW(hwnd, wide_title, wide_other, icon);
    ShFree(wide_title);
    ShFree(wide_other);
    return result;
}
