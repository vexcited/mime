/*
 * installing from an inf: directory ids, file sources, and the install section directives. file copies, deletes and
 * renames are queued and committed together, the others edit the registry and ini files, register libraries and
 * include other infs.
 */
#include "setup_private.h"

#define PROFILE_INI_LIMIT 4096

/* ------------------------------------------------------------------ directory ids */

static BOOL set_known(PCWSTR base, PCWSTR rest, PWSTR out, SIZE_T chars)
{
    if (wcslen(base) + wcslen(rest) + 1 > chars)
        return FALSE;
    wcscpy(out, base);
    wcscat(out, rest);
    return TRUE;
}

typedef HRESULT(WINAPI *GET_FOLDER_PATH)(HWND, int, HANDLE, DWORD, PWSTR);

static BOOL csidl_path(int csidl, PWSTR out, SIZE_T chars)
{
    HMODULE shell = LoadLibraryW(L"shell32.dll");
    GET_FOLDER_PATH get = shell ? (GET_FOLDER_PATH)(void *)GetProcAddress(shell, "SHGetFolderPathW") : nullptr;
    WCHAR path[MAX_PATH];
    BOOL ok = get && chars >= MAX_PATH && get(nullptr, csidl, nullptr, 0, path) >= 0;
    if (ok)
        wcscpy(out, path);
    return ok;
}

BOOL SetupDirectoryPath(INF_FILE *file, UINT id, PCWSTR absolute, PWSTR out, SIZE_T chars)
{
    if (id >= DIRID_USER && id <= 0xFFFF && file) {
        for (ULONG i = 0; i < file->DirectoryCount; i++) {
            if (file->Directories[i].Id == id)
                return set_known(file->Directories[i].Path, L"", out, chars);
        }
        return FALSE;
    }
    WCHAR windows[MAX_PATH], system[MAX_PATH];
    GetWindowsDirectoryW(windows, MAX_PATH);
    GetSystemDirectoryW(system, MAX_PATH);
    switch (id) {
    case DIRID_ABSOLUTE:
        return absolute && set_known(absolute, L"", out, chars);
    case DIRID_NULL:
        out[0] = 0;
        return TRUE;
    case DIRID_SRCPATH: {
        if (!file)
            return FALSE;
        if (!set_known(file->Path, L"", out, chars))
            return FALSE;
        PWSTR slash = wcsrchr(out, '\\');
        if (slash)
            *slash = 0;
        return TRUE;
    }
    case DIRID_WINDOWS:
    case 25: /* shared files */
        return set_known(windows, L"", out, chars);
    case DIRID_SYSTEM:
    case 21: /* viewers */
    case 54: /* the loader */
        return set_known(system, L"", out, chars);
    case DIRID_DRIVERS:
        return set_known(system, L"\\drivers", out, chars);
    case DIRID_INF:
        return set_known(windows, L"\\inf", out, chars);
    case 18:
        return set_known(windows, L"\\help", out, chars);
    case 20:
        return set_known(windows, L"\\fonts", out, chars);
    case 23:
        return set_known(system, L"\\spool\\drivers\\color", out, chars);
    case 24:
    case 30: {
        WCHAR root[4] = {windows[0], ':', '\\', 0};
        return set_known(root, L"", out, chars);
    }
    case 50:
        return set_known(windows, L"\\system", out, chars);
    case 51:
        return set_known(system, L"\\spool", out, chars);
    case 52:
        return set_known(system, L"\\spool\\drivers", out, chars);
    case 55:
        return set_known(system, L"\\spool\\prtprocs", out, chars);
    case 53:
        return csidl_path(0x28, out, chars); /* CSIDL_PROFILE */
    default:
        /* ids from 16384 on are shell folders by CSIDL. */
        if (id >= 0x4000 && id < 0x4100)
            return csidl_path((int)(id - 0x4000), out, chars);
        return FALSE;
    }
}

void SetupExpandDirectories(INF_FILE *file, PCWSTR text, PWSTR out, SIZE_T chars)
{
    SIZE_T used = 0;
    for (PCWSTR p = text; *p && used + 1 < chars;) {
        if (*p == '%' && p[1] >= '0' && p[1] <= '9') {
            PCWSTR q = p + 1;
            UINT id = 0;
            while (*q >= '0' && *q <= '9')
                id = id * 10 + (UINT)(*q++ - '0');
            WCHAR path[MAX_PATH];
            if (*q == '%' && SetupDirectoryPath(file, id, nullptr, path, MAX_PATH)) {
                SIZE_T n = wcslen(path);
                if (used + n + 1 > chars)
                    break;
                wcscpy(out + used, path);
                used += n;
                p = q + 1;
                continue;
            }
        }
        out[used++] = *p++;
    }
    out[used] = 0;
}

SETUPAPI BOOL WINAPI SetupSetDirectoryIdW(HINF handle, DWORD id, PCWSTR directory)
{
    INF_FILE *f = SetupInfOf(handle);
    if (!f)
        return FALSE;
    if (id < DIRID_USER || id > 0xFFFF) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    for (ULONG i = 0; i < f->DirectoryCount; i++) {
        if (f->Directories[i].Id == id) {
            SetupFree(f->Directories[i].Path);
            f->Directories[i].Path = SetupDuplicate(directory);
            if (!directory) {
                f->Directories[i] = f->Directories[--f->DirectoryCount];
            }
            return TRUE;
        }
    }
    if (!directory)
        return TRUE;
    INF_USER_DIRECTORY *bigger = SetupAlloc((f->DirectoryCount + 1) * sizeof(*bigger));
    if (!bigger) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    if (f->Directories) {
        memcpy(bigger, f->Directories, f->DirectoryCount * sizeof(*bigger));
        SetupFree(f->Directories);
    }
    f->Directories = bigger;
    f->Directories[f->DirectoryCount].Id = id;
    f->Directories[f->DirectoryCount].Path = SetupDuplicate(directory);
    f->DirectoryCount++;
    return TRUE;
}

SETUPAPI BOOL WINAPI SetupSetDirectoryIdA(HINF handle, DWORD id, PCSTR directory)
{
    PWSTR wide = SetupToWide(directory);
    BOOL ok = (!directory || wide) ? SetupSetDirectoryIdW(handle, id, wide) : FALSE;
    SetupFree(wide);
    return ok;
}

/* ------------------------------------------------------------------ sections */

/* tries the name with this machine's decorations first, across the head file's chain. */
static BOOL find_decorated(INF_FILE *head, PCWSTR name, PWSTR out, SIZE_T chars)
{
    return SetupDecoratedSection(head, name, out, chars);
}

/* searches every file of the chain. */
static BOOL line_of(INF_FILE *head, PCWSTR section, PCWSTR key, INFCONTEXT *context)
{
    WCHAR decorated[256];
    if (find_decorated(head, section, decorated, RTL_NUMBER_OF(decorated)) &&
        SetupFirstLine(head, decorated, key, context))
        return TRUE;
    return SetupFirstLine(head, section, key, context);
}

static PWSTR field_of(const INFCONTEXT *context, DWORD field)
{
    return SetupContextField(context, field);
}

/* the field's number, or the fallback when it is empty. */
static LONG64 number_of(const INFCONTEXT *context, DWORD field, LONG64 fallback)
{
    PWSTR text = field_of(context, field);
    LONG64 value = fallback;
    if (text && *text) {
        int n;
        if (SetupGetIntField((PINFCONTEXT)context, field, &n))
            value = (DWORD)n;
    }
    SetupFree(text);
    return value;
}

/* the section's DestinationDirs line, else DefaultDestDir. */
static BOOL destination_of(INF_FILE *head, PCWSTR section, PWSTR out, SIZE_T chars)
{
    INFCONTEXT line;
    BOOL found = line_of(head, L"DestinationDirs", section, &line);
    if (!found)
        found = line_of(head, L"DestinationDirs", L"DefaultDestDir", &line);
    if (!found)
        return FALSE;
    UINT id = (UINT)number_of(&line, 1, 0);
    PWSTR sub = field_of(&line, 2);
    PWSTR absolute = nullptr;
    if (id == DIRID_ABSOLUTE) {
        absolute = sub;
        sub = nullptr;
    }
    BOOL ok = SetupDirectoryPath(head, id, absolute, out, chars);
    if (ok && sub && *sub) {
        SIZE_T n = wcslen(out);
        if (n + wcslen(sub) + 2 > chars)
            ok = FALSE;
        else {
            if (n && out[n - 1] != '\\')
                out[n++] = '\\';
            wcscpy(out + n, sub);
        }
    }
    SetupFree(sub);
    SetupFree(absolute);
    return ok;
}

static void join(PWSTR out, SIZE_T chars, PCWSTR a, PCWSTR b)
{
    SIZE_T n = wcslen(a);
    if (n >= chars)
        n = chars - 1;
    memcpy(out, a, n * sizeof(WCHAR));
    if (n && out[n - 1] != '\\' && b && *b && n + 1 < chars)
        out[n++] = '\\';
    out[n] = 0;
    if (b) {
        SIZE_T more = wcslen(b);
        if (n + more >= chars)
            more = chars - n - 1;
        memcpy(out + n, b, more * sizeof(WCHAR));
        out[n + more] = 0;
    }
}

/*
 * SourceDisksFiles names a file's disk and optional subfolder, SourceDisksNames the disk's path under the source
 * root. a file the inf does not list is in the root.
 */
static BOOL source_of(INF_FILE *head, INF_FILE *layout, PCWSTR file, PCWSTR root, PWSTR directory, SIZE_T chars,
                      PWSTR description, PWSTR tag, UINT *disk)
{
    WCHAR base[MAX_PATH];
    if (root && *root) {
        wcscpy(base, root);
    } else {
        SetupDirectoryPath(head, DIRID_SRCPATH, nullptr, base, MAX_PATH);
    }
    INF_FILE *in = layout ? layout : head;
    INFCONTEXT line;
    PWSTR sub = nullptr;
    UINT id = 0;
    BOOL listed = line_of(in, L"SourceDisksFiles", file, &line);
    if (listed) {
        id = (UINT)number_of(&line, 1, 1);
        sub = field_of(&line, 2);
    }
    WCHAR disk_path[MAX_PATH] = L"";
    if (description)
        description[0] = 0;
    if (tag)
        tag[0] = 0;
    if (listed) {
        WCHAR key[16];
        _snwprintf(key, RTL_NUMBER_OF(key), L"%u", id);
        INFCONTEXT names;
        if (line_of(in, L"SourceDisksNames", key, &names)) {
            PWSTR d = field_of(&names, 1), t = field_of(&names, 2), path = field_of(&names, 4);
            if (description && d)
                SetupCopyText(description, d, MAX_PATH);
            if (tag && t)
                SetupCopyText(tag, t, MAX_PATH);
            if (path)
                SetupCopyText(disk_path, path, MAX_PATH);
            SetupFree(d);
            SetupFree(t);
            SetupFree(path);
        }
    }
    join(directory, chars, base, disk_path);
    if (sub && *sub)
        join(directory, chars, directory, sub);
    SetupFree(sub);
    if (disk)
        *disk = id;
    return TRUE;
}

/* ------------------------------------------------------------------ file directives */

static DWORD copy_style(DWORD flags)
{
    DWORD style = 0;
    if (flags & 0x1)
        style |= SP_COPY_WARNIFSKIP;
    if (flags & 0x2)
        style |= SP_COPY_NOSKIP;
    if (flags & 0x4)
        style |= SP_COPY_FORCE_NEWER;
    if (flags & 0x8)
        style |= SP_COPY_FORCE_IN_USE;
    if (flags & 0x10)
        style |= SP_COPY_NOOVERWRITE;
    if (flags & 0x40)
        style |= SP_COPY_NEWER_ONLY;
    if (flags & 0x400)
        style |= SP_COPY_REPLACEONLY;
    if (flags & 0x800)
        style |= SP_COPY_NODECOMP;
    if (flags & 0x2000)
        style |= SP_COPY_NOPRUNE;
    if (flags & 0x4000)
        style |= SP_COPY_IN_USE_NEEDS_REBOOT;
    return style;
}

/* a directive lists sections, or @file for a single file. */
static BOOL each_name(INF_FILE *head, PCWSTR section, PCWSTR directive,
                      BOOL (*visit)(INF_FILE *head, PCWSTR name, void *state), void *state)
{
    INFCONTEXT line;
    BOOL ok = TRUE;
    if (!line_of(head, section, directive, &line))
        return TRUE;
    for (;;) {
        DWORD count = SetupGetFieldCount(&line);
        for (DWORD i = 1; i <= count && ok; i++) {
            PWSTR name = field_of(&line, i);
            if (name && *name)
                ok = visit(head, name, state);
            SetupFree(name);
        }
        INFCONTEXT next;
        if (!ok || !SetupFindNextMatchLineW(&line, directive, &next))
            break;
        line = next;
    }
    return ok;
}

typedef struct FILE_STATE {
    FILE_QUEUE *Queue;
    INF_FILE *Layout;
    PCWSTR Root;
    DWORD Style;
    BOOL Copies, Deletes, Renames;
    DWORD Error;
} FILE_STATE;

static BOOL queue_copy_name(INF_FILE *head, PCWSTR name, void *state_);

static BOOL queue_one_copy(FILE_STATE *state, INF_FILE *head, PCWSTR dest_dir, PCWSTR target, PCWSTR source_name, DWORD flags)
{
    WCHAR directory[MAX_PATH], description[MAX_PATH], tag[MAX_PATH];
    UINT disk;
    if (!source_of(head, state->Layout, source_name, state->Root, directory, MAX_PATH, description, tag, &disk))
        return FALSE;
    return SetupQueueAdd(state->Queue, QUEUE_COPY, directory, source_name, dest_dir, target, description, tag,
                         state->Style | copy_style(flags));
}

static BOOL queue_copy_name(INF_FILE *head, PCWSTR name, void *state_)
{
    FILE_STATE *state = state_;
    WCHAR destination[MAX_PATH];
    if (name[0] == '@') {
        if (!destination_of(head, L"DefaultDestDir", destination, MAX_PATH) &&
            !destination_of(head, name + 1, destination, MAX_PATH)) {
            state->Error = ERROR_LINE_NOT_FOUND;
            return FALSE;
        }
        return queue_one_copy(state, head, destination, name + 1, name + 1, 0);
    }
    if (!destination_of(head, name, destination, MAX_PATH)) {
        state->Error = ERROR_LINE_NOT_FOUND;
        return FALSE;
    }
    INFCONTEXT line;
    if (!line_of(head, name, nullptr, &line)) {
        state->Error = ERROR_SECTION_NOT_FOUND;
        return FALSE;
    }
    for (;;) {
        PWSTR target = field_of(&line, 1), source = field_of(&line, 2);
        DWORD flags = (DWORD)number_of(&line, 4, 0);
        BOOL ok = TRUE;
        if (target && *target)
            ok = queue_one_copy(state, head, destination, target, source && *source ? source : target, flags);
        SetupFree(target);
        SetupFree(source);
        if (!ok)
            return FALSE;
        INFCONTEXT next;
        if (!SetupNextLine(&line, &next))
            break;
        line = next;
    }
    return TRUE;
}

static BOOL queue_delete_name(INF_FILE *head, PCWSTR name, void *state_)
{
    FILE_STATE *state = state_;
    WCHAR destination[MAX_PATH];
    if (!destination_of(head, name, destination, MAX_PATH)) {
        state->Error = ERROR_LINE_NOT_FOUND;
        return FALSE;
    }
    INFCONTEXT line;
    if (name[0] == '@')
        return SetupQueueAdd(state->Queue, QUEUE_DELETE, nullptr, nullptr, destination, name + 1, nullptr, nullptr, 0);
    if (!line_of(head, name, nullptr, &line)) {
        state->Error = ERROR_SECTION_NOT_FOUND;
        return FALSE;
    }
    for (;;) {
        PWSTR target = field_of(&line, 1);
        BOOL ok = !target || !*target || SetupQueueAdd(state->Queue, QUEUE_DELETE, nullptr, nullptr, destination, target,
                                                      nullptr, nullptr, 0);
        SetupFree(target);
        if (!ok)
            return FALSE;
        INFCONTEXT next;
        if (!SetupNextLine(&line, &next))
            break;
        line = next;
    }
    return TRUE;
}

static BOOL queue_rename_name(INF_FILE *head, PCWSTR name, void *state_)
{
    FILE_STATE *state = state_;
    WCHAR destination[MAX_PATH];
    if (!destination_of(head, name, destination, MAX_PATH)) {
        state->Error = ERROR_LINE_NOT_FOUND;
        return FALSE;
    }
    INFCONTEXT line;
    if (!line_of(head, name, nullptr, &line)) {
        state->Error = ERROR_SECTION_NOT_FOUND;
        return FALSE;
    }
    for (;;) {
        PWSTR to = field_of(&line, 1), from = field_of(&line, 2);
        BOOL ok = !to || !from || SetupQueueAdd(state->Queue, QUEUE_RENAME, destination, from, destination, to, nullptr,
                                              nullptr, 0);
        SetupFree(to);
        SetupFree(from);
        if (!ok)
            return FALSE;
        INFCONTEXT next;
        if (!SetupNextLine(&line, &next))
            break;
        line = next;
    }
    return TRUE;
}

BOOL SetupQueueSections(HSPFILEQ queue, INF_FILE *file, INF_FILE *layout, PCWSTR section, PCWSTR source_root, DWORD style,
                        BOOL copies, BOOL deletes, BOOL renames)
{
    FILE_QUEUE *q = SetupQueueOf(queue);
    if (!q)
        return FALSE;
    FILE_STATE state = {q, layout, source_root, style, copies, deletes, renames, 0};
    BOOL ok = TRUE;
    /* deletes and renames come before copies, which may depend on them. */
    if (deletes)
        ok = each_name(file, section, L"DelFiles", queue_delete_name, &state);
    if (ok && renames)
        ok = each_name(file, section, L"RenFiles", queue_rename_name, &state);
    if (ok && copies)
        ok = each_name(file, section, L"CopyFiles", queue_copy_name, &state);
    if (!ok)
        SetLastError(state.Error ? state.Error : ERROR_NOT_ENOUGH_MEMORY);
    return ok;
}

SETUPAPI BOOL WINAPI SetupInstallFilesFromInfSectionW(HINF handle, HINF layout, HSPFILEQ queue, PCWSTR section,
                                                      PCWSTR source_root, UINT copy_flags)
{
    INF_FILE *file = SetupInfOf(handle);
    if (!file)
        return FALSE;
    INF_FILE *lay = nullptr;
    if (layout && layout != INVALID_HANDLE_VALUE && !(lay = SetupInfOf(layout)))
        return FALSE;
    if (!section) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR name[256];
    if (!find_decorated(file, section, name, RTL_NUMBER_OF(name))) {
        SetLastError(ERROR_SECTION_NOT_FOUND);
        return FALSE;
    }
    return SetupQueueSections(queue, file, lay, name, source_root, copy_flags, TRUE, TRUE, TRUE);
}

SETUPAPI BOOL WINAPI SetupInstallFilesFromInfSectionA(HINF handle, HINF layout, HSPFILEQ queue, PCSTR section,
                                                      PCSTR source_root, UINT copy_flags)
{
    PWSTR a = SetupToWide(section), b = SetupToWide(source_root);
    BOOL ok = a && (!source_root || b) ? SetupInstallFilesFromInfSectionW(handle, layout, queue, a, b, copy_flags) : FALSE;
    SetupFree(a);
    SetupFree(b);
    return ok;
}

SETUPAPI BOOL WINAPI SetupQueueCopySectionW(HSPFILEQ queue, PCWSTR source_root, HINF handle, HINF layout, PCWSTR section,
                                            DWORD style)
{
    INF_FILE *file = SetupInfOf(handle);
    if (!file || !section)
        return FALSE;
    INF_FILE *lay = layout && layout != INVALID_HANDLE_VALUE ? SetupInfOf(layout) : nullptr;
    FILE_QUEUE *q = SetupQueueOf(queue);
    if (!q)
        return FALSE;
    FILE_STATE state = {q, lay, source_root, style, TRUE, FALSE, FALSE, 0};
    BOOL ok = queue_copy_name(file, section, &state);
    if (!ok && state.Error)
        SetLastError(state.Error);
    return ok;
}

SETUPAPI BOOL WINAPI SetupQueueCopySectionA(HSPFILEQ queue, PCSTR source_root, HINF handle, HINF layout, PCSTR section,
                                            DWORD style)
{
    PWSTR a = SetupToWide(source_root), b = SetupToWide(section);
    BOOL ok = b && (!source_root || a) ? SetupQueueCopySectionW(queue, a, handle, layout, b, style) : FALSE;
    SetupFree(a);
    SetupFree(b);
    return ok;
}

SETUPAPI BOOL WINAPI SetupQueueDeleteSectionW(HSPFILEQ queue, HINF handle, HINF layout, PCWSTR section)
{
    INF_FILE *file = SetupInfOf(handle);
    FILE_QUEUE *q = SetupQueueOf(queue);
    if (!file || !q || !section)
        return FALSE;
    FILE_STATE state = {q, nullptr, nullptr, 0, FALSE, TRUE, FALSE, 0};
    BOOL ok = queue_delete_name(file, section, &state);
    if (!ok && state.Error)
        SetLastError(state.Error);
    return ok;
}

SETUPAPI BOOL WINAPI SetupQueueDeleteSectionA(HSPFILEQ queue, HINF handle, HINF layout, PCSTR section)
{
    PWSTR wide = SetupToWide(section);
    BOOL ok = wide ? SetupQueueDeleteSectionW(queue, handle, layout, wide) : FALSE;
    SetupFree(wide);
    return ok;
}

SETUPAPI BOOL WINAPI SetupQueueRenameSectionW(HSPFILEQ queue, HINF handle, HINF layout, PCWSTR section)
{
    INF_FILE *file = SetupInfOf(handle);
    FILE_QUEUE *q = SetupQueueOf(queue);
    if (!file || !q || !section)
        return FALSE;
    FILE_STATE state = {q, nullptr, nullptr, 0, FALSE, FALSE, TRUE, 0};
    BOOL ok = queue_rename_name(file, section, &state);
    if (!ok && state.Error)
        SetLastError(state.Error);
    return ok;
}

SETUPAPI BOOL WINAPI SetupQueueRenameSectionA(HSPFILEQ queue, HINF handle, HINF layout, PCSTR section)
{
    PWSTR wide = SetupToWide(section);
    BOOL ok = wide ? SetupQueueRenameSectionW(queue, handle, layout, wide) : FALSE;
    SetupFree(wide);
    return ok;
}

/* ------------------------------------------------------------------ registry */

#define FLG_ADDREG_BINVALUETYPE 0x00000001
#define FLG_ADDREG_NOCLOBBER 0x00000002
#define FLG_ADDREG_DELVAL 0x00000004
#define FLG_ADDREG_APPEND 0x00000008
#define FLG_ADDREG_KEYONLY 0x00000010
#define FLG_ADDREG_OVERWRITEONLY 0x00000020
#define FLG_ADDREG_64BITKEY 0x00001000
#define FLG_ADDREG_32BITKEY 0x00004000
#define FLG_ADDREG_TYPE_MULTI_SZ 0x00010000
#define FLG_ADDREG_TYPE_EXPAND_SZ 0x00020000

static BOOL root_of(PCWSTR name, HKEY relative, HKEY *root)
{
    struct { PCWSTR Name; HKEY Key; } names[] = {
        {L"HKCR", HKEY_CLASSES_ROOT}, {L"HKEY_CLASSES_ROOT", HKEY_CLASSES_ROOT},
        {L"HKCU", HKEY_CURRENT_USER}, {L"HKEY_CURRENT_USER", HKEY_CURRENT_USER},
        {L"HKLM", HKEY_LOCAL_MACHINE}, {L"HKEY_LOCAL_MACHINE", HKEY_LOCAL_MACHINE},
        {L"HKU", HKEY_USERS}, {L"HKEY_USERS", HKEY_USERS},
    };
    if (!_wcsicmp(name, L"HKR")) {
        *root = relative;
        return relative != nullptr;
    }
    for (SIZE_T i = 0; i < RTL_NUMBER_OF(names); i++) {
        if (!_wcsicmp(name, names[i].Name)) {
            *root = names[i].Key;
            return TRUE;
        }
    }
    return FALSE;
}

static DWORD parse_dword(PCWSTR text)
{
    int n = 0;
    INFCONTEXT unused;
    (void)unused;
    while (*text == ' ')
        text++;
    int base = 10;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
    }
    DWORD value = 0;
    for (; *text; text++) {
        int digit = *text >= '0' && *text <= '9' ? *text - '0' : *text >= 'a' && *text <= 'f' ? *text - 'a' + 10 :
                    *text >= 'A' && *text <= 'F' ? *text - 'A' + 10 : -1;
        if (digit < 0 || digit >= base)
            break;
        value = value * (DWORD)base + (DWORD)digit;
    }
    (void)n;
    return value;
}

static BOOL add_reg_line(INF_FILE *head, const INFCONTEXT *line, HKEY relative)
{
    PWSTR root_name = field_of(line, 1), subkey = field_of(line, 2), value_name = field_of(line, 3);
    DWORD flags = (DWORD)number_of(line, 4, 0);
    HKEY root;
    BOOL ok = root_name && root_of(root_name, relative, &root);
    if (!ok) {
        SetLastError(ERROR_INVALID_DATA);
    } else {
        REGSAM view = (flags & FLG_ADDREG_64BITKEY) ? KEY_WOW64_64KEY : (flags & FLG_ADDREG_32BITKEY) ? KEY_WOW64_32KEY : 0;
        HKEY key = root;
        BOOL opened = FALSE;
        WCHAR expanded[MAX_PATH];
        if (subkey && *subkey) {
            SetupExpandDirectories(head, subkey, expanded, MAX_PATH);
            DWORD disposition;
            LSTATUS r = RegCreateKeyExW(root, expanded, 0, nullptr, 0, KEY_READ | KEY_WRITE | view, nullptr, &key, &disposition);
            if (r != ERROR_SUCCESS) {
                SetLastError((DWORD)r);
                ok = FALSE;
            } else {
                opened = TRUE;
            }
        }
        if (ok && (flags & FLG_ADDREG_KEYONLY)) {
            /* only the key is created. */
        } else if (ok && (flags & FLG_ADDREG_DELVAL)) {
            RegDeleteValueW(key, value_name && *value_name ? value_name : nullptr);
        } else if (ok) {
            PCWSTR name = value_name && *value_name ? value_name : nullptr;
            DWORD type_bits = flags & 0xFFFF0001;
            DWORD exists_type = 0, exists_size = 0;
            BOOL exists = RegQueryValueExW(key, name, nullptr, &exists_type, nullptr, &exists_size) == ERROR_SUCCESS;
            if (exists && (flags & FLG_ADDREG_NOCLOBBER)) {
                /* kept as is. */
            } else if (!exists && (flags & FLG_ADDREG_OVERWRITEONLY)) {
                /* nothing to overwrite. */
            } else {
                DWORD count = SetupGetFieldCount((PINFCONTEXT)line);
                LSTATUS r = ERROR_SUCCESS;
                if (type_bits == 0x00010001) { /* REG_DWORD */
                    PWSTR text = field_of(line, 5);
                    DWORD value = text ? parse_dword(text) : 0;
                    SetupFree(text);
                    r = RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
                } else if (type_bits == 0x00000001 || type_bits == 0x00020001) { /* binary, or none */
                    DWORD need = count >= 5 ? count - 4 : 0;
                    BYTE *bytes = SetupAlloc(need + 1);
                    DWORD got = 0;
                    if (bytes && need && !SetupGetBinaryField((PINFCONTEXT)line, 5, bytes, need, &got))
                        got = 0;
                    r = RegSetValueExW(key, name, 0, type_bits == 0x00000001 ? REG_BINARY : REG_NONE, bytes, got);
                    SetupFree(bytes);
                } else if (type_bits == FLG_ADDREG_TYPE_MULTI_SZ) {
                    /* the line's strings, after the existing ones when appending. */
                    SIZE_T total = 1;
                    PWSTR *parts = SetupAlloc((count + 1) * sizeof(PWSTR));
                    DWORD n = 0;
                    for (DWORD i = 5; i <= count + 0 && parts; i++) {
                        parts[n] = field_of(line, i);
                        if (parts[n])
                            total += wcslen(parts[n]) + 1;
                        n++;
                    }
                    BYTE *old = nullptr;
                    DWORD old_size = 0;
                    if ((flags & FLG_ADDREG_APPEND) && exists && exists_type == REG_MULTI_SZ && exists_size > sizeof(WCHAR)) {
                        old = SetupAlloc(exists_size + 4);
                        old_size = exists_size;
                        if (old && RegQueryValueExW(key, name, nullptr, nullptr, old, &old_size) != ERROR_SUCCESS)
                            old_size = 0;
                        total += old_size / sizeof(WCHAR);
                    }
                    PWSTR all = SetupAlloc((total + 2) * sizeof(WCHAR));
                    PWSTR at = all;
                    if (all && old && old_size > sizeof(WCHAR)) {
                        SIZE_T chars = old_size / sizeof(WCHAR);
                        while (chars && ((PWSTR)old)[chars - 1] == 0)
                            chars--;
                        memcpy(at, old, chars * sizeof(WCHAR));
                        at += chars;
                        *at++ = 0;
                    }
                    for (DWORD i = 0; all && i < n; i++) {
                        if (parts[i]) {
                            wcscpy(at, parts[i]);
                            at += wcslen(at) + 1;
                        }
                    }
                    if (all) {
                        *at++ = 0;
                        r = RegSetValueExW(key, name, 0, REG_MULTI_SZ, (const BYTE *)all, (DWORD)((at - all) * sizeof(WCHAR)));
                    }
                    for (DWORD i = 0; parts && i < n; i++)
                        SetupFree(parts[i]);
                    SetupFree(parts);
                    SetupFree(old);
                    SetupFree(all);
                } else {
                    /* a plain or expandable string, empty when the line has no value. */
                    PWSTR text = field_of(line, 5);
                    WCHAR value[2048];
                    SetupExpandDirectories(head, text ? text : L"", value, RTL_NUMBER_OF(value));
                    SetupFree(text);
                    DWORD type = type_bits == FLG_ADDREG_TYPE_EXPAND_SZ ? REG_EXPAND_SZ : REG_SZ;
                    r = RegSetValueExW(key, name, 0, type, (const BYTE *)value, (DWORD)((wcslen(value) + 1) * sizeof(WCHAR)));
                }
                if (r != ERROR_SUCCESS) {
                    SetLastError((DWORD)r);
                    ok = FALSE;
                }
            }
        }
        if (opened)
            RegCloseKey(key);
    }
    SetupFree(root_name);
    SetupFree(subkey);
    SetupFree(value_name);
    return ok;
}

typedef struct REG_STATE {
    HKEY Relative;
    BOOL Delete;
} REG_STATE;

static BOOL registry_section(INF_FILE *head, PCWSTR name, void *state_)
{
    REG_STATE *state = state_;
    INFCONTEXT line;
    if (!SetupFirstLine(head, name, nullptr, &line))
        return GetLastError() == ERROR_LINE_NOT_FOUND ? TRUE : FALSE;
    for (;;) {
        if (state->Delete) {
            PWSTR root_name = field_of(&line, 1), subkey = field_of(&line, 2), value = field_of(&line, 3);
            HKEY root;
            if (root_name && root_of(root_name, state->Relative, &root)) {
                WCHAR path[MAX_PATH];
                SetupExpandDirectories(head, subkey ? subkey : L"", path, MAX_PATH);
                if (value && *value) {
                    HKEY key;
                    if (RegOpenKeyExW(root, path, 0, KEY_WRITE, &key) == ERROR_SUCCESS) {
                        RegDeleteValueW(key, value);
                        RegCloseKey(key);
                    }
                } else if (*path) {
                    RegDeleteKeyW(root, path);
                }
            }
            SetupFree(root_name);
            SetupFree(subkey);
            SetupFree(value);
        } else if (!add_reg_line(head, &line, state->Relative)) {
            return FALSE;
        }
        INFCONTEXT next;
        if (!SetupNextLine(&line, &next))
            break;
        line = next;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ ini files, libraries, the rest */

/* UpdateInis lines: ini file, section, old entry, new entry. the new entry replaces the old one or is added. */
static BOOL ini_section(INF_FILE *head, PCWSTR name, void *state)
{
    INFCONTEXT line;
    if (!SetupFirstLine(head, name, nullptr, &line))
        return TRUE;
    for (;;) {
        PWSTR file = field_of(&line, 1), section = field_of(&line, 2), old = field_of(&line, 3), entry = field_of(&line, 4);
        WCHAR path[MAX_PATH];
        if (file && section) {
            SetupExpandDirectories(head, file, path, MAX_PATH);
            if (old && *old) {
                /* the old entry's key is the text before its equals sign. */
                WCHAR key[256];
                SetupCopyText(key, old, RTL_NUMBER_OF(key));
                key[RTL_NUMBER_OF(key) - 1] = 0;
                PWSTR eq = wcschr(key, '=');
                if (eq)
                    *eq = 0;
                WritePrivateProfileStringW(section, key, nullptr, path);
            }
            if (entry && *entry) {
                WCHAR key[256];
                SetupCopyText(key, entry, RTL_NUMBER_OF(key));
                key[RTL_NUMBER_OF(key) - 1] = 0;
                PWSTR eq = wcschr(key, '=');
                PCWSTR value = L"";
                if (eq) {
                    *eq = 0;
                    value = wcschr(entry, '=') + 1;
                }
                WritePrivateProfileStringW(section, key, value, path);
            }
        }
        SetupFree(file);
        SetupFree(section);
        SetupFree(old);
        SetupFree(entry);
        INFCONTEXT next;
        if (!SetupNextLine(&line, &next))
            break;
        line = next;
    }
    return TRUE;
}

#define FLG_REGSVR_DLLREGISTER 0x00000001
#define FLG_REGSVR_DLLINSTALL 0x00000002

typedef struct REGSVR_STATE {
    BOOL Unregister;
} REGSVR_STATE;

static BOOL regsvr_section(INF_FILE *head, PCWSTR name, void *state_)
{
    REGSVR_STATE *state = state_;
    INFCONTEXT line;
    if (!SetupFirstLine(head, name, nullptr, &line))
        return TRUE;
    for (;;) {
        UINT id = (UINT)number_of(&line, 1, 0);
        PWSTR sub = field_of(&line, 2), file = field_of(&line, 3);
        DWORD flags = (DWORD)number_of(&line, 4, 0);
        WCHAR dir[MAX_PATH], path[MAX_PATH];
        if (file && SetupDirectoryPath(head, id, nullptr, dir, MAX_PATH)) {
            join(path, MAX_PATH, dir, sub);
            join(path, MAX_PATH, path, file);
            HMODULE module = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
            if (module) {
                typedef HRESULT(WINAPI * ENTRY)(void);
                typedef HRESULT(WINAPI * INSTALL)(BOOL, PCWSTR);
                if (flags & FLG_REGSVR_DLLREGISTER) {
                    ENTRY entry = (ENTRY)(void *)GetProcAddress(module, state->Unregister ? "DllUnregisterServer" : "DllRegisterServer");
                    if (entry)
                        entry();
                }
                if (flags & FLG_REGSVR_DLLINSTALL) {
                    INSTALL install = (INSTALL)(void *)GetProcAddress(module, "DllInstall");
                    if (install)
                        install(!state->Unregister, nullptr);
                }
                FreeLibrary(module);
            }
        }
        SetupFree(sub);
        SetupFree(file);
        INFCONTEXT next;
        if (!SetupNextLine(&line, &next))
            break;
        line = next;
    }
    return TRUE;
}

static BOOL include_names(INF_FILE *head, PCWSTR section, PCWSTR directive, HINF handle)
{
    /* Include names inf files to open besides this one; Needs names the sections of them to run too. */
    INFCONTEXT line;
    if (!line_of(head, section, directive, &line))
        return TRUE;
    for (;;) {
        DWORD count = SetupGetFieldCount(&line);
        for (DWORD i = 1; i <= count; i++) {
            PWSTR name = field_of(&line, i);
            if (name && *name && !_wcsicmp(directive, L"Include"))
                SetupOpenAppendInfFileW(name, handle, nullptr);
            SetupFree(name);
        }
        INFCONTEXT next;
        if (!SetupFindNextMatchLineW(&line, directive, &next))
            break;
        line = next;
    }
    return TRUE;
}

static BOOL names_of(INF_FILE *head, PCWSTR section, PCWSTR directive, BOOL (*visit)(INF_FILE *, PCWSTR, void *), void *state)
{
    return each_name(head, section, directive, visit, state);
}

SETUPAPI BOOL WINAPI SetupInstallFromInfSectionW(HWND owner, HINF handle, PCWSTR section, UINT flags, HKEY key_root,
                                                 PCWSTR source_root, UINT copy_flags, PSP_FILE_CALLBACK_W handler,
                                                 PVOID context, HDEVINFO devices, PSP_DEVINFO_DATA device)
{
    return SetupInstallInfSection(owner, handle, section, flags, key_root, source_root, copy_flags, handler, context, FALSE);
}

SETUPAPI BOOL WINAPI SetupInstallFromInfSectionA(HWND owner, HINF handle, PCSTR section, UINT flags, HKEY key_root,
                                                 PCSTR source_root, UINT copy_flags, PSP_FILE_CALLBACK_A handler,
                                                 PVOID context, HDEVINFO devices, PSP_DEVINFO_DATA device)
{
    PWSTR a = SetupToWide(section), b = SetupToWide(source_root);
    BOOL ok = a && (!source_root || b)
                  ? SetupInstallInfSection(owner, handle, a, flags, key_root, b, copy_flags, handler, context, TRUE)
                  : FALSE;
    SetupFree(a);
    SetupFree(b);
    return ok;
}

static BOOL run_section(INF_FILE *head, HINF handle, PCWSTR name, UINT flags, HKEY relative, PCWSTR root, UINT copy_flags,
                        PVOID handler, PVOID context, BOOL ansi, ULONG depth)
{
    if (depth > 8) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    BOOL ok = TRUE;
    include_names(head, name, L"Include", handle);
    /* Needs: sections of the included files, run first. */
    {
        INFCONTEXT line;
        if (line_of(head, name, L"Needs", &line)) {
            for (;;) {
                DWORD count = SetupGetFieldCount(&line);
                for (DWORD i = 1; i <= count && ok; i++) {
                    PWSTR needed = field_of(&line, i);
                    if (needed && *needed) {
                        WCHAR decorated[256];
                        if (SetupDecoratedSection(head, needed, decorated, RTL_NUMBER_OF(decorated)))
                            ok = run_section(head, handle, decorated, flags, relative, root, copy_flags, handler, context,
                                             ansi, depth + 1);
                    }
                    SetupFree(needed);
                }
                INFCONTEXT next;
                if (!ok || !SetupFindNextMatchLineW(&line, L"Needs", &next))
                    break;
                line = next;
            }
        }
    }
    if (ok && (flags & SPINST_FILES)) {
        HSPFILEQ queue = SetupOpenFileQueue();
        if (!queue)
            return FALSE;
        ok = SetupQueueSections(queue, head, nullptr, name, root, copy_flags, TRUE, TRUE, TRUE);
        if (ok)
            ok = SetupCommit(SetupQueueOf(queue), (PSP_FILE_CALLBACK_W)handler, context, ansi, handler);
        DWORD error = GetLastError();
        SetupCloseFileQueue(queue);
        SetLastError(error);
    }
    if (ok && (flags & SPINST_INIFILES))
        names_of(head, name, L"UpdateInis", ini_section, nullptr);
    if (ok && (flags & SPINST_REGISTRY)) {
        REG_STATE del = {relative, TRUE}, add = {relative, FALSE};
        ok = names_of(head, name, L"DelReg", registry_section, &del);
        if (ok)
            ok = names_of(head, name, L"AddReg", registry_section, &add);
    }
    if (ok && (flags & SPINST_UNREGSVR)) {
        REGSVR_STATE state = {TRUE};
        names_of(head, name, L"UnregisterDlls", regsvr_section, &state);
    }
    if (ok && (flags & SPINST_REGSVR)) {
        REGSVR_STATE state = {FALSE};
        names_of(head, name, L"RegisterDlls", regsvr_section, &state);
    }
    return ok;
}

BOOL SetupInstallInfSection(HWND owner, HINF handle, PCWSTR section, UINT flags, HKEY key_root, PCWSTR source_root,
                            UINT copy_flags, PVOID handler, PVOID context, BOOL ansi)
{
    INF_FILE *head = SetupInfOf(handle);
    if (!head)
        return FALSE;
    if (!section || (flags & ~(UINT)SPINST_ALL)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR name[256];
    if (!find_decorated(head, section, name, RTL_NUMBER_OF(name))) {
        SetLastError(ERROR_SECTION_NOT_FOUND);
        return FALSE;
    }
    return run_section(head, handle, name, flags, key_root, source_root, copy_flags, handler, context, ansi, 0);
}

/* ------------------------------------------------------------------ file paths */

SETUPAPI BOOL WINAPI SetupGetTargetPathW(HINF handle, PINFCONTEXT context, PCWSTR section, PWSTR buffer, DWORD size,
                                         PDWORD required)
{
    INF_FILE *head = SetupInfOf(handle);
    if (!head)
        return FALSE;
    WCHAR path[MAX_PATH];
    PCWSTR name = section;
    PWSTR from_context = nullptr;
    if (!name && context) {
        INF_FILE *f;
        if (!SetupContextLine(context, &f))
            return FALSE;
        name = f->Sections[context->Section].Name;
    }
    if (!name || !destination_of(head, name, path, MAX_PATH)) {
        SetupFree(from_context);
        SetLastError(ERROR_LINE_NOT_FOUND);
        return FALSE;
    }
    DWORD need = (DWORD)wcslen(path) + 1;
    if (required)
        *required = need;
    if (!buffer)
        return TRUE;
    if (size < need) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    wcscpy(buffer, path);
    return TRUE;
}

SETUPAPI BOOL WINAPI SetupGetTargetPathA(HINF handle, PINFCONTEXT context, PCSTR section, PSTR buffer, DWORD size, PDWORD required)
{
    PWSTR name = SetupToWide(section);
    WCHAR path[MAX_PATH];
    BOOL ok = (!section || name) && SetupGetTargetPathW(handle, context, name, path, MAX_PATH, nullptr);
    SetupFree(name);
    if (!ok)
        return FALSE;
    PSTR ansi = SetupToAnsi(path);
    DWORD need = ansi ? (DWORD)strlen(ansi) + 1 : 0;
    if (required)
        *required = need;
    if (buffer && ansi) {
        if (size < need) {
            SetupFree(ansi);
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        memcpy(buffer, ansi, need);
    }
    SetupFree(ansi);
    return TRUE;
}

SETUPAPI BOOL WINAPI SetupGetSourceFileLocationW(HINF handle, PINFCONTEXT context, PCWSTR file, PUINT source_id, PWSTR buffer,
                                                 DWORD size, PDWORD required)
{
    INF_FILE *head = SetupInfOf(handle);
    if (!head)
        return FALSE;
    PWSTR from_line = nullptr;
    if (!file && context)
        from_line = SetupContextField(context, 1);
    PCWSTR name = file ? file : from_line;
    if (!name) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    WCHAR directory[MAX_PATH];
    UINT disk = 0;
    BOOL ok = source_of(head, nullptr, name, L"", directory, MAX_PATH, nullptr, nullptr, &disk);
    SetupFree(from_line);
    if (!ok)
        return FALSE;
    /* the path under the root, without the root of the inf's own directory. */
    WCHAR base[MAX_PATH];
    SetupDirectoryPath(head, DIRID_SRCPATH, nullptr, base, MAX_PATH);
    PCWSTR relative = directory;
    SIZE_T n = wcslen(base);
    if (!_wcsnicmp(directory, base, n)) {
        relative = directory + n;
        while (*relative == '\\')
            relative++;
    }
    if (source_id)
        *source_id = disk;
    DWORD need = (DWORD)wcslen(relative) + 1;
    if (required)
        *required = need;
    if (!buffer)
        return TRUE;
    if (size < need) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    wcscpy(buffer, relative);
    return TRUE;
}

SETUPAPI BOOL WINAPI SetupGetSourceFileLocationA(HINF handle, PINFCONTEXT context, PCSTR file, PUINT source_id, PSTR buffer,
                                                 DWORD size, PDWORD required)
{
    PWSTR name = SetupToWide(file);
    WCHAR path[MAX_PATH];
    BOOL ok = (!file || name) && SetupGetSourceFileLocationW(handle, context, name, source_id, path, MAX_PATH, nullptr);
    SetupFree(name);
    if (!ok)
        return FALSE;
    PSTR ansi = SetupToAnsi(path);
    DWORD need = ansi ? (DWORD)strlen(ansi) + 1 : 0;
    if (required)
        *required = need;
    if (buffer && ansi) {
        if (size < need) {
            SetupFree(ansi);
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        memcpy(buffer, ansi, need);
    }
    SetupFree(ansi);
    return TRUE;
}

SETUPAPI BOOL WINAPI SetupGetSourceInfoW(HINF handle, UINT source_id, UINT info, PWSTR buffer, DWORD size, PDWORD required)
{
    INF_FILE *head = SetupInfOf(handle);
    if (!head)
        return FALSE;
    WCHAR key[16];
    _snwprintf(key, RTL_NUMBER_OF(key), L"%u", source_id);
    INFCONTEXT line;
    if (!line_of(head, L"SourceDisksNames", key, &line)) {
        SetLastError(ERROR_LINE_NOT_FOUND);
        return FALSE;
    }
    DWORD field = info == SRCINFO_DESCRIPTION ? 1 : info == SRCINFO_TAGFILE ? 2 : info == SRCINFO_PATH ? 4 :
                  info == SRCINFO_FLAGS ? 5 : 0;
    if (!field) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    PWSTR text = field_of(&line, field);
    DWORD need = text ? (DWORD)wcslen(text) + 1 : 1;
    if (required)
        *required = need;
    BOOL ok = TRUE;
    if (buffer) {
        if (size < need) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            ok = FALSE;
        } else {
            wcscpy(buffer, text ? text : L"");
        }
    }
    SetupFree(text);
    return ok;
}

SETUPAPI BOOL WINAPI SetupGetSourceInfoA(HINF handle, UINT source_id, UINT info, PSTR buffer, DWORD size, PDWORD required)
{
    WCHAR text[MAX_PATH];
    if (!SetupGetSourceInfoW(handle, source_id, info, text, MAX_PATH, nullptr))
        return FALSE;
    PSTR ansi = SetupToAnsi(text);
    DWORD need = ansi ? (DWORD)strlen(ansi) + 1 : 0;
    if (required)
        *required = need;
    if (buffer && ansi) {
        if (size < need) {
            SetupFree(ansi);
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        memcpy(buffer, ansi, need);
    }
    SetupFree(ansi);
    return TRUE;
}
