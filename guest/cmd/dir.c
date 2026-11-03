/* dir. */
#include "cmd.h"

typedef struct DIRENT {
    WCHAR *name;
    DWORD attr;
    ULONGLONG size;
    FILETIME time;
    BOOL is_dir;
    DWORD tag;     /* of a symbolic link or a junction, else 0. */
    WCHAR *target; /* what it points to, as the link says. */
} DIRENT;

typedef struct DIROPT {
    BOOL bare, recurse, lower, wide, thousands;
    BOOL attr_given;
    DWORD need, forbid;
    WCHAR order[16];
    int time_field; /* 0 write, 1 creation, 2 access. */
} DIROPT;

typedef struct TOTALS {
    ULONGLONG files, bytes, dirs;
} TOTALS;

static const DIROPT *g_opt;

static ULONGLONG time_value(const DIRENT *e)
{
    return ((ULONGLONG)e->time.dwHighDateTime << 32) | e->time.dwLowDateTime;
}

/* ordinal compare of the upper case names, the order a volume lists them in. */
static int name_order(const WCHAR *a, const WCHAR *b)
{
    for (;; a++, b++) {
        WCHAR x = towupper(*a), y = towupper(*b);
        if (x != y)
            return x < y ? -1 : 1;
        if (!x)
            return 0;
    }
}

static int compare_entries(const void *pa, const void *pb)
{
    const DIRENT *a = pa, *b = pb;
    /* the dot entries come first whatever the order. */
    BOOL da = !wcscmp(a->name, L".") || !wcscmp(a->name, L".."), db = !wcscmp(b->name, L".") || !wcscmp(b->name, L"..");
    if (da || db) {
        if (da && db)
            return wcscmp(a->name, b->name);
        return da ? -1 : 1;
    }
    BOOL reverse = FALSE;
    for (const WCHAR *o = g_opt->order; *o; o++) {
        if (*o == L'-') {
            reverse = TRUE;
            continue;
        }
        int r = 0;
        switch (towlower(*o)) {
        case L'n': r = name_order(a->name, b->name); break;
        case L'e': {
            const WCHAR *ea = wcsrchr(a->name, L'.'), *eb = wcsrchr(b->name, L'.');
            r = name_order(ea ? ea : L"", eb ? eb : L"");
            break;
        }
        case L's': r = a->size < b->size ? -1 : a->size > b->size ? 1 : 0; break;
        case L'd': {
            ULONGLONG ta = time_value(a), tb = time_value(b);
            r = ta < tb ? -1 : ta > tb ? 1 : 0;
            break;
        }
        case L'g': r = (a->is_dir == b->is_dir) ? 0 : (a->is_dir ? -1 : 1); break;
        }
        if (reverse)
            r = -r;
        reverse = FALSE;
        if (r)
            return r;
    }
    return name_order(a->name, b->name);
}

static int compare_name_pointers(const void *a, const void *b)
{
    return name_order((const WCHAR *)a, (const WCHAR *)b);
}

static void parse_attribute_filter(const WCHAR *p, DIROPT *o)
{
    o->attr_given = TRUE;
    if (*p == L':')
        p++;
    BOOL neg = FALSE;
    for (; *p; p++) {
        if (*p == L'-') {
            neg = TRUE;
            continue;
        }
        DWORD bit = 0;
        switch (towlower(*p)) {
        case L'd': bit = FILE_ATTRIBUTE_DIRECTORY; break;
        case L'h': bit = FILE_ATTRIBUTE_HIDDEN; break;
        case L's': bit = FILE_ATTRIBUTE_SYSTEM; break;
        case L'r': bit = FILE_ATTRIBUTE_READONLY; break;
        case L'a': bit = FILE_ATTRIBUTE_ARCHIVE; break;
        case L'l': bit = FILE_ATTRIBUTE_REPARSE_POINT; break;
        case L'i': bit = FILE_ATTRIBUTE_NOT_CONTENT_INDEXED; break;
        case L'o': bit = FILE_ATTRIBUTE_OFFLINE; break;
        }
        if (neg)
            o->forbid |= bit;
        else
            o->need |= bit;
        neg = FALSE;
    }
}

static BOOL entry_wanted(const WIN32_FIND_DATAW *fd, const DIROPT *o)
{
    if (o->attr_given) {
        if ((fd->dwFileAttributes & o->need) != o->need)
            return FALSE;
        if (fd->dwFileAttributes & o->forbid)
            return FALSE;
        return TRUE;
    }
    return !(fd->dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
}

static void date_time_text(const FILETIME *ft, STR *out)
{
    FILETIME local;
    SYSTEMTIME st;
    if (!FileTimeToLocalFileTime(ft, &local) || !FileTimeToSystemTime(&local, &st)) {
        str_adds(out, L"                    ");
        return;
    }
    format_date(&st, FALSE, out);
    str_adds(out, L"  ");
    format_time_short(&st, out);
}

static void number_text(ULONGLONG v, BOOL commas, STR *out)
{
    if (commas) {
        format_number_commas(v, out);
    } else {
        WCHAR t[32];
        _snwprintf(t, NUM(t), L"%I64u", v);
        str_adds(out, t);
    }
}

static void pad_left(STR *line, const WCHAR *text, SIZE_T width)
{
    SIZE_T n = wcslen(text);
    for (SIZE_T i = n; i < width; i++)
        str_addc(line, L' ');
    str_adds(line, text);
}

/* the name a link points to, the print name when it has one, as dir shows it between brackets. */
static WCHAR *link_target(const WCHAR *dir, const WCHAR *name)
{
    STR path;
    str_init(&path);
    str_adds(&path, dir);
    str_adds(&path, name);
    HANDLE h = CreateFileW(path.p, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    str_free(&path);
    if (h == INVALID_HANDLE_VALUE)
        return xdup(L"...");
    BYTE *buffer = xalloc(MAXIMUM_REPARSE_DATA_BUFFER_SIZE + 2);
    DWORD got = 0;
    WCHAR *result = nullptr;
    if (DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, nullptr, 0, buffer, MAXIMUM_REPARSE_DATA_BUFFER_SIZE, &got, nullptr)) {
        REPARSE_DATA_BUFFER *b = (REPARSE_DATA_BUFFER *)buffer;
        const BYTE *base;
        USHORT so, sl, po, pl;
        if (b->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
            base = (const BYTE *)b->MountPointReparseBuffer.PathBuffer;
            so = b->MountPointReparseBuffer.SubstituteNameOffset;
            sl = b->MountPointReparseBuffer.SubstituteNameLength;
            po = b->MountPointReparseBuffer.PrintNameOffset;
            pl = b->MountPointReparseBuffer.PrintNameLength;
        } else {
            base = (const BYTE *)b->SymbolicLinkReparseBuffer.PathBuffer;
            so = b->SymbolicLinkReparseBuffer.SubstituteNameOffset;
            sl = b->SymbolicLinkReparseBuffer.SubstituteNameLength;
            po = b->SymbolicLinkReparseBuffer.PrintNameOffset;
            pl = b->SymbolicLinkReparseBuffer.PrintNameLength;
        }
        USHORT offset = pl ? po : so, length = pl ? pl : sl;
        if (base + offset + length <= buffer + got) {
            result = xalloc(length + sizeof(WCHAR));
            memcpy(result, base + offset, length);
            result[length / sizeof(WCHAR)] = 0;
        }
    }
    xfree(buffer);
    CloseHandle(h);
    return result ? result : xdup(L"...");
}

static void show_volume_header(WCHAR letter)
{
    WCHAR root[4] = {upcase(letter), L':', L'\\', 0};
    WCHAR label[MAX_PATH];
    DWORD serial = 0;
    if (!GetVolumeInformationW(root, label, NUM(label), &serial, nullptr, nullptr, nullptr, 0))
        return;
    if (label[0])
        out_fmt(L" Volume in drive %lc is %ls\r\n", root[0], label);
    else
        out_fmt(L" Volume in drive %lc has no label.\r\n", root[0]);
    out_fmt(L" Volume Serial Number is %04X-%04X\r\n", serial >> 16, serial & 0xFFFF);
}

static void show_free_space(const WCHAR *dir, const DIROPT *o, ULONGLONG dirs)
{
    ULARGE_INTEGER free_bytes, total, total_free;
    STR line;
    str_init(&line);
    STR dirs_text;
    str_init(&dirs_text);
    number_text(dirs, FALSE, &dirs_text);
    pad_left(&line, dirs_text.p, 16);
    str_adds(&line, L" Dir(s)  ");
    STR bytes;
    str_init(&bytes);
    if (GetDiskFreeSpaceExW(dir, &free_bytes, &total, &total_free))
        number_text(free_bytes.QuadPart, o->thousands, &bytes);
    else
        str_adds(&bytes, L"0");
    pad_left(&line, bytes.p, 14);
    str_adds(&line, L" bytes free\r\n");
    out_n(line.p, line.len);
    str_free(&bytes);
    str_free(&dirs_text);
    str_free(&line);
}

static void show_totals(const TOTALS *t, const DIROPT *o)
{
    STR line, n, b;
    str_init(&line);
    str_init(&n);
    str_init(&b);
    number_text(t->files, FALSE, &n);
    pad_left(&line, n.p, 16);
    str_adds(&line, L" File(s) ");
    number_text(t->bytes, o->thousands, &b);
    pad_left(&line, b.p, 14);
    str_adds(&line, L" bytes\r\n");
    out_n(line.p, line.len);
    str_free(&line);
    str_free(&n);
    str_free(&b);
}

static int wide_width(void)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (g_io.con[1] && GetConsoleScreenBufferInfo(g_io.h[1], &info) && info.dwSize.X > 20)
        return info.dwSize.X;
    return 80;
}

/* lists one folder for a pattern. returns the entries found. */
static int list_directory(const WCHAR *dir, const WCHAR *pattern, const DIROPT *o, TOTALS *totals, BOOL print_header_always,
                          const WCHAR *display_dir)
{
    STR spec;
    str_init(&spec);
    str_adds(&spec, dir);
    str_adds(&spec, pattern);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(spec.p, &fd);
    DIRENT *entries = nullptr;
    SIZE_T count = 0, cap = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!entry_wanted(&fd, o))
                continue;
            /* the dot entries are listed in a real folder only, and not by /b or /s. */
            if ((!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) && (o->bare || o->recurse))
                continue;
            if (count == cap) {
                cap = cap ? cap * 2 : 32;
                entries = xrealloc(entries, cap * sizeof(DIRENT));
            }
            DIRENT *e = &entries[count++];
            e->name = xdup(fd.cFileName);
            e->attr = fd.dwFileAttributes;
            e->is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            e->tag = 0;
            e->target = nullptr;
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                (fd.dwReserved0 == IO_REPARSE_TAG_SYMLINK || fd.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT)) {
                e->tag = fd.dwReserved0;
                if (!o->bare && !o->wide)
                    e->target = link_target(dir, fd.cFileName);
            }
            e->size = ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            e->time = o->time_field == 1 ? fd.ftCreationTime : o->time_field == 2 ? fd.ftLastAccessTime : fd.ftLastWriteTime;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    str_free(&spec);
    g_opt = o;
    DIRENT **sorted = xalloc((count + 1) * sizeof(DIRENT *));
    for (SIZE_T i = 0; i < count; i++)
        sorted[i] = &entries[i];
    sort_pointers((void **)sorted, count, compare_entries);
    if (count || print_header_always) {
        if (!o->bare)
            out_fmt(L"\r\n Directory of %ls\r\n\r\n", display_dir);
    }
    ULONGLONG dir_files = 0, dir_bytes = 0, dir_dirs = 0;
    if (o->wide) {
        SIZE_T maxlen = 0;
        STR *labels = xalloc((count + 1) * sizeof(STR));
        for (SIZE_T i = 0; i < count; i++) {
            DIRENT *e = sorted[i];
            str_init(&labels[i]);
            if (e->is_dir)
                str_fmt(&labels[i], L"[%ls]", e->name);
            else
                str_adds(&labels[i], e->name);
            if (labels[i].len > maxlen)
                maxlen = labels[i].len;
        }
        SIZE_T colw = maxlen + 2;
        SIZE_T cols = (SIZE_T)wide_width() / colw;
        if (!cols)
            cols = 1;
        for (SIZE_T i = 0; i < count; i++) {
            DIRENT *e = sorted[i];
            STR line;
            str_init(&line);
            str_adds(&line, labels[i].p);
            if ((i + 1) % cols == 0 || i + 1 == count) {
                str_adds(&line, L"\r\n");
            } else {
                for (SIZE_T k = labels[i].len; k < colw; k++)
                    str_addc(&line, L' ');
            }
            out_n(line.p, line.len);
            str_free(&line);
            if (e->is_dir)
                dir_dirs++;
            else {
                dir_files++;
                dir_bytes += e->size;
            }
            str_free(&labels[i]);
        }
        xfree(labels);
    } else {
        for (SIZE_T i = 0; i < count; i++) {
            DIRENT *e = sorted[i];
            STR line;
            str_init(&line);
            if (o->bare) {
                if (o->recurse) {
                    str_adds(&line, display_dir);
                    if (line.len && line.p[line.len - 1] != L'\\')
                        str_addc(&line, L'\\');
                }
                if (o->lower) {
                    WCHAR *low = xdup(e->name);
                    lower_inplace(low);
                    str_adds(&line, low);
                    xfree(low);
                } else {
                    str_adds(&line, e->name);
                }
            } else {
                date_time_text(&e->time, &line);
                if (e->tag == IO_REPARSE_TAG_MOUNT_POINT) {
                    str_adds(&line, L"    <JUNCTION>     ");
                } else if (e->tag == IO_REPARSE_TAG_SYMLINK) {
                    str_adds(&line, e->is_dir ? L"    <SYMLINKD>     " : L"    <SYMLINK>      ");
                } else if (e->is_dir) {
                    str_adds(&line, L"    <DIR>          ");
                } else {
                    STR n;
                    str_init(&n);
                    number_text(e->size, o->thousands, &n);
                    str_adds(&line, L"    ");
                    pad_left(&line, n.p, 14);
                    str_addc(&line, L' ');
                    str_free(&n);
                }
                if (o->lower) {
                    WCHAR *low = xdup(e->name);
                    lower_inplace(low);
                    str_adds(&line, low);
                    xfree(low);
                } else {
                    str_adds(&line, e->name);
                }
                if (e->target) {
                    str_adds(&line, L" [");
                    str_adds(&line, e->target);
                    str_addc(&line, L']');
                }
            }
            str_adds(&line, L"\r\n");
            out_n(line.p, line.len);
            str_free(&line);
            if (e->is_dir)
                dir_dirs++;
            else {
                dir_files++;
                dir_bytes += e->size;
            }
        }
    }
    for (SIZE_T i = 0; i < count; i++) {
        xfree(entries[i].name);
        xfree(entries[i].target);
    }
    xfree(entries);
    xfree(sorted);
    totals->files += dir_files;
    totals->bytes += dir_bytes;
    totals->dirs += dir_dirs;
    if (!o->bare && count) {
        TOTALS here = {dir_files, dir_bytes, dir_dirs};
        show_totals(&here, o);
        if (!o->recurse)
            show_free_space(dir, o, dir_dirs);
    }
    return (int)count;
}

/* the folders below, depth first, each listed when it has matches. */
static void list_tree(const WCHAR *dir, const WCHAR *pattern, const DIROPT *o, TOTALS *totals, int *found)
{
    STR spec;
    str_init(&spec);
    str_adds(&spec, dir);
    str_adds(&spec, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(spec.p, &fd);
    WCHAR **subs = nullptr;
    SIZE_T count = 0, cap = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !wcscmp(fd.cFileName, L".") ||
                !wcscmp(fd.cFileName, L"..") || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                continue;
            if (count == cap) {
                cap = cap ? cap * 2 : 16;
                subs = xrealloc(subs, cap * sizeof(WCHAR *));
            }
            subs[count++] = xdup(fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    str_free(&spec);
    sort_pointers((void **)subs, count, compare_name_pointers);
    for (SIZE_T i = 0; i < count; i++) {
        STR sub;
        str_init(&sub);
        str_adds(&sub, dir);
        str_adds(&sub, subs[i]);
        str_addc(&sub, L'\\');
        STR shown;
        str_init(&shown);
        str_adds(&shown, sub.p);
        if (shown.len > 3)
            shown.p[--shown.len] = 0;
        *found += list_directory(sub.p, pattern, o, totals, FALSE, shown.p);
        list_tree(sub.p, pattern, o, totals, found);
        str_free(&shown);
        str_free(&sub);
        xfree(subs[i]);
    }
    xfree(subs);
}

int internal_dir(const WCHAR *args, NODE *node)
{
    DIROPT o = {};
    o.thousands = TRUE;
    wcscpy(o.order, L"");
    STR copydir;
    str_init(&copydir);
    ARGS a;
    args_split(args, &a, FALSE);
    int specs = 0;
    WCHAR *spec_list[32];
    for (int i = 0; i < a.n; i++) {
        const WCHAR *w = a.v[i];
        /* switches may be glued: dir/w/s. */
        if (w[0] == L'/') {
            const WCHAR *p = w;
            while (*p == L'/') {
                p++;
                WCHAR c = towlower(*p);
                const WCHAR *start = p;
                if (c == L'b') {
                    o.bare = TRUE;
                    p++;
                } else if (c == L's') {
                    o.recurse = TRUE;
                    p++;
                } else if (c == L'l') {
                    o.lower = TRUE;
                    p++;
                } else if (c == L'w') {
                    o.wide = TRUE;
                    p++;
                } else if (c == L'c') {
                    o.thousands = TRUE;
                    p++;
                } else if (c == L'-' && towlower(p[1]) == L'c') {
                    o.thousands = FALSE;
                    p += 2;
                } else if (c == L'p' || c == L'q' || c == L'x' || c == L'n' || c == L'r' || c == L'd' || c == L'4') {
                    p++;
                } else if (c == L'a') {
                    p++;
                    const WCHAR *e = p;
                    while (*e && *e != L'/')
                        e++;
                    WCHAR *attrs = xdupn(p, (SIZE_T)(e - p));
                    parse_attribute_filter(attrs, &o);
                    if (!*attrs)
                        o.attr_given = TRUE;
                    xfree(attrs);
                    p = e;
                } else if (c == L'o') {
                    p++;
                    const WCHAR *e = p;
                    while (*e && *e != L'/')
                        e++;
                    SIZE_T n = (SIZE_T)(e - p);
                    const WCHAR *src = p;
                    if (n && *src == L':') {
                        src++;
                        n--;
                    }
                    if (n >= NUM(o.order))
                        n = NUM(o.order) - 1;
                    memcpy(o.order, src, n * sizeof(WCHAR));
                    o.order[n] = 0;
                    if (!n)
                        wcscpy(o.order, L"gn");
                    p = e;
                } else if (c == L't') {
                    p++;
                    if (*p == L':')
                        p++;
                    WCHAR f = towlower(*p);
                    if (f == L'c')
                        o.time_field = 1;
                    else if (f == L'a')
                        o.time_field = 2;
                    else if (f == L'w')
                        o.time_field = 0;
                    if (*p && *p != L'/')
                        p++;
                } else {
                    err_fmt(L"Invalid switch - \"%.*ls\".\r\n", 1, start);
                    args_free(&a);
                    set_errorlevel(1);
                    return 1;
                }
            }
            continue;
        }
        if (specs < 32)
            spec_list[specs++] = xdup(w);
    }
    args_free(&a);
    if (specs == 0)
        spec_list[specs++] = xdup(L"*");
    int result = 0;
    BOOL header_done = FALSE;
    TOTALS totals = {};
    int found_total = 0;
    for (int i = 0; i < specs; i++) {
        WCHAR *spec = xdup(spec_list[i]);
        strip_quotes_inplace(spec);
        WCHAR full[MAX_PATH * 2];
        DWORD n = GetFullPathNameW(spec, NUM(full), full, nullptr);
        if (!n || n >= NUM(full)) {
            print_win_error(ERROR_PATH_NOT_FOUND);
            set_errorlevel(1);
            result = 1;
            xfree(spec);
            continue;
        }
        STR dir, pattern;
        str_init(&dir);
        str_init(&pattern);
        DWORD attr = GetFileAttributesW(full);
        BOOL spec_has_wild = has_wildcards(spec);
        if (!spec_has_wild && attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            str_adds(&dir, full);
            if (dir.len && dir.p[dir.len - 1] != L'\\')
                str_addc(&dir, L'\\');
            str_adds(&pattern, L"*");
        } else {
            path_split_pattern(full, &dir, &pattern);
        }
        if (!o.bare && !header_done) {
            show_volume_header(full[0]);
            header_done = TRUE;
        }
        STR shown;
        str_init(&shown);
        str_adds(&shown, dir.p);
        if (shown.len > 3)
            shown.p[--shown.len] = 0;
        int before = found_total;
        TOTALS spec_totals = {};
        int c = list_directory(dir.p, pattern.p, &o, o.recurse ? &totals : &spec_totals, !o.recurse, shown.p);
        found_total += c;
        if (!o.recurse)
            totals.files += spec_totals.files, totals.bytes += spec_totals.bytes, totals.dirs += spec_totals.dirs;
        if (o.recurse)
            list_tree(dir.p, pattern.p, &o, &totals, &found_total);
        if (found_total == before) {
            if (!o.bare || TRUE) {
                err_s(o.bare ? L"File Not Found\r\n" : L"File Not Found\r\n");
            }
            set_errorlevel(1);
            result = 1;
        }
        str_free(&shown);
        str_free(&dir);
        str_free(&pattern);
        xfree(spec);
    }
    if (o.recurse && !o.bare && found_total) {
        out_s(L"\r\n     Total Files Listed:\r\n");
        show_totals(&totals, &o);
        STR cwd;
        str_init(&cwd);
        get_cwd(&cwd);
        show_free_space(str_get(&cwd), &o, totals.dirs);
        str_free(&cwd);
    }
    for (int i = 0; i < specs; i++)
        xfree(spec_list[i]);
    str_free(&copydir);
    return result;
}
