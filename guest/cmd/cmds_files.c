/* file and directory commands: cd pushd popd md rd del copy move ren type vol mklink assoc ftype. */
#include "cmd.h"

/* ------------------------------------------------------------------ helpers */

/* y or n from the user, TRUE for yes. an input that has ended answers no. */
static BOOL ask_yes_no(const WCHAR *prompt)
{
    for (;;) {
        out_s(prompt);
        WCHAR c;
        if (!read_key(&c)) {
            out_s(L"\r\n");
            return FALSE;
        }
        c = towupper(c);
        if (c == L'Y' || c == L'N') {
            out_fmt(L"%lc\r\n", c);
            return c == L'Y';
        }
        out_s(L"\r\n");
    }
}

/* Yes, No or All. */
static int ask_overwrite(const WCHAR *name)
{
    for (;;) {
        out_fmt(L"Overwrite %ls? (Yes/No/All): ", name);
        WCHAR c;
        if (!read_key(&c)) {
            out_s(L"\r\n");
            return 0;
        }
        c = towupper(c);
        if (c == L'Y' || c == L'N' || c == L'A') {
            out_fmt(L"%lc\r\n", c);
            return c == L'Y' ? 1 : c == L'A' ? 2 : 0;
        }
        out_s(L"\r\n");
    }
}

static void trim_trailing_space(STR *s)
{
    while (s->len && is_space(s->p[s->len - 1]))
        s->p[--s->len] = 0;
}

/* an argument with quotes removed. */
static WCHAR *unquote(const WCHAR *s)
{
    WCHAR *d = xdup(s);
    strip_quotes_inplace(d);
    return d;
}

static void fail_last_error(void)
{
    DWORD e = GetLastError();
    print_win_error(e);
    set_errorlevel(1);
}

/* ------------------------------------------------------------------ cd, pushd, popd */

BOOL change_directory(const WCHAR *path, BOOL switch_drive)
{
    WCHAR full[MAX_PATH * 2];
    DWORD n = GetFullPathNameW(path, NUM(full), full, nullptr);
    if (!n || n >= NUM(full)) {
        SetLastError(ERROR_PATH_NOT_FOUND);
        return FALSE;
    }
    STR cwd;
    str_init(&cwd);
    get_cwd(&cwd);
    BOOL other_drive = full[1] == L':' && cwd.len >= 2 && towupper(full[0]) != towupper(cwd.p[0]);
    str_free(&cwd);
    DWORD attr = GetFileAttributesW(full);
    if (attr == INVALID_FILE_ATTRIBUTES)
        return FALSE;
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        SetLastError(ERROR_DIRECTORY);
        return FALSE;
    }
    if (other_drive && !switch_drive) {
        drive_dir_set(full);
        return TRUE;
    }
    if (!SetCurrentDirectoryW(full))
        return FALSE;
    drive_dir_set(full);
    return TRUE;
}

int internal_cd(const WCHAR *args, NODE *node)
{
    const WCHAR *p = skip_space(args);
    BOOL switch_drive = FALSE;
    if (!_wcsnicmp(p, L"/d", 2) && (!p[2] || is_space(p[2]))) {
        switch_drive = TRUE;
        p = skip_space(p + 2);
    }
    STR path;
    str_init(&path);
    str_adds(&path, p);
    trim_trailing_space(&path);
    strip_quotes_inplace(path.p ? path.p : (WCHAR *)L"");
    path.len = wcslen(str_get(&path));
    if (!path.len) {
        STR cwd;
        str_init(&cwd);
        get_cwd(&cwd);
        out_fmt(L"%ls\r\n", str_get(&cwd));
        str_free(&cwd);
        str_free(&path);
        return 0;
    }
    /* a drive alone shows the directory of that drive. */
    if (path.len == 2 && path.p[1] == L':') {
        STR dir;
        str_init(&dir);
        if (!drive_dir_get(path.p[0], &dir)) {
            WCHAR root[4] = {upcase(path.p[0]), L':', L'\\', 0};
            str_adds(&dir, root);
        }
        out_fmt(L"%ls\r\n", str_get(&dir));
        str_free(&dir);
        str_free(&path);
        return 0;
    }
    if (!change_directory(path.p, switch_drive)) {
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND)
            e = ERROR_PATH_NOT_FOUND;
        print_win_error(e);
        set_errorlevel(1);
        str_free(&path);
        return 1;
    }
    str_free(&path);
    return 0;
}

int internal_pushd(const WCHAR *args, NODE *node)
{
    const WCHAR *p = skip_space(args);
    if (!*p) {
        for (int i = g.pushd_count - 1; i >= 0; i--)
            out_fmt(L"%ls\r\n", g.pushd[i]);
        return 0;
    }
    if (g.pushd_count >= (int)NUM(g.pushd)) {
        err_s(L"There are too many directories already in the PUSHD stack.\r\n");
        return 1;
    }
    STR cwd;
    str_init(&cwd);
    get_cwd(&cwd);
    STR path;
    str_init(&path);
    str_adds(&path, p);
    trim_trailing_space(&path);
    strip_quotes_inplace(path.p ? path.p : (WCHAR *)L"");
    int result = 0;
    if (!change_directory(path.p, TRUE)) {
        print_win_error(GetLastError() == ERROR_FILE_NOT_FOUND ? ERROR_PATH_NOT_FOUND : GetLastError());
        set_errorlevel(1);
        result = 1;
    } else {
        g.pushd[g.pushd_count++] = str_take(&cwd);
    }
    str_free(&cwd);
    str_free(&path);
    return result;
}

int internal_popd(const WCHAR *args, NODE *node)
{
    if (g.pushd_count == 0)
        return 0;
    WCHAR *dir = g.pushd[--g.pushd_count];
    g.pushd[g.pushd_count] = nullptr;
    int result = 0;
    if (!change_directory(dir, TRUE)) {
        fail_last_error();
        result = 1;
    }
    xfree(dir);
    return result;
}

/* ------------------------------------------------------------------ md, rd */

static BOOL make_path(const WCHAR *path)
{
    if (CreateDirectoryW(path, nullptr))
        return TRUE;
    DWORD e = GetLastError();
    if (e != ERROR_PATH_NOT_FOUND || !g.ext)
        return FALSE;
    /* create the folders above first. */
    STR parent;
    str_init(&parent);
    str_adds(&parent, path);
    while (parent.len && (parent.p[parent.len - 1] == L'\\' || parent.p[parent.len - 1] == L'/'))
        parent.p[--parent.len] = 0;
    WCHAR *slash = nullptr;
    for (WCHAR *c = parent.p; *c; c++)
        if (*c == L'\\' || *c == L'/')
            slash = c;
    BOOL ok = FALSE;
    if (slash && slash > parent.p && slash[-1] != L':') {
        *slash = 0;
        ok = make_path(parent.p) || GetLastError() == ERROR_ALREADY_EXISTS;
    }
    str_free(&parent);
    if (!ok) {
        SetLastError(e);
        return FALSE;
    }
    return CreateDirectoryW(path, nullptr);
}

int internal_md(const WCHAR *args, NODE *node)
{
    ARGS a;
    args_split(args, &a, FALSE);
    int result = 0;
    if (!a.n) {
        msg_syntax();
        args_free(&a);
        return 1;
    }
    for (int i = 0; i < a.n; i++) {
        WCHAR *path = unquote(a.v[i]);
        if (!make_path(path)) {
            DWORD e = GetLastError();
            if (e == ERROR_ALREADY_EXISTS || e == ERROR_FILE_EXISTS)
                err_fmt(L"A subdirectory or file %ls already exists.\r\n", path);
            else
                print_win_error(e);
            set_errorlevel(1);
            result = 1;
        }
        xfree(path);
    }
    args_free(&a);
    return result;
}

static BOOL remove_tree(const WCHAR *dir)
{
    STR spec;
    str_init(&spec);
    str_adds(&spec, dir);
    str_adds(&spec, L"\\*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(str_get(&spec), &fd);
    BOOL ok = TRUE;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
                continue;
            STR child;
            str_init(&child);
            str_adds(&child, dir);
            str_addc(&child, L'\\');
            str_adds(&child, fd.cFileName);
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                if (!remove_tree(child.p))
                    ok = FALSE;
            } else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!RemoveDirectoryW(child.p))
                    ok = FALSE;
            } else {
                if (fd.dwFileAttributes & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
                    SetFileAttributesW(child.p, FILE_ATTRIBUTE_NORMAL);
                if (!DeleteFileW(child.p))
                    ok = FALSE;
            }
            str_free(&child);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    str_free(&spec);
    DWORD attr = GetFileAttributesW(dir);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_READONLY))
        SetFileAttributesW(dir, attr & ~FILE_ATTRIBUTE_READONLY);
    if (!RemoveDirectoryW(dir))
        ok = FALSE;
    return ok;
}

int internal_rd(const WCHAR *args, NODE *node)
{
    ARGS a;
    args_split(args, &a, FALSE);
    BOOL tree = FALSE, quiet = FALSE;
    int result = 0, dirs = 0;
    for (int i = 0; i < a.n; i++) {
        if (g.ext && a.v[i][0] == L'/' && a.v[i][1] && !a.v[i][2]) {
            WCHAR c = towlower(a.v[i][1]);
            if (c == L's') {
                tree = TRUE;
                continue;
            }
            if (c == L'q') {
                quiet = TRUE;
                continue;
            }
        }
        if (a.v[i][0] == L'/') {
            err_fmt(L"Invalid switch - \"%ls\".\r\n", a.v[i] + 1);
            args_free(&a);
            return 1;
        }
    }
    for (int i = 0; i < a.n; i++) {
        if (a.v[i][0] == L'/' && g.ext)
            continue;
        dirs++;
        WCHAR *path = unquote(a.v[i]);
        if (tree) {
            if (!quiet) {
                STR prompt;
                str_init(&prompt);
                str_fmt(&prompt, L"%ls, Are you sure (Y/N)? ", path);
                BOOL yes = ask_yes_no(str_get(&prompt));
                str_free(&prompt);
                if (!yes) {
                    xfree(path);
                    continue;
                }
            }
            DWORD attr = GetFileAttributesW(path);
            if (attr == INVALID_FILE_ATTRIBUTES) {
                fail_last_error();
                result = 1;
            } else if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) {
                err_s(L"The directory name is invalid.\r\n");
                set_errorlevel(1);
                result = 1;
            } else if (!remove_tree(path)) {
                fail_last_error();
                result = 1;
            }
        } else if (!RemoveDirectoryW(path)) {
            fail_last_error();
            result = 1;
        }
        xfree(path);
    }
    if (!dirs) {
        msg_syntax();
        result = 1;
    }
    args_free(&a);
    return result;
}

/* ------------------------------------------------------------------ del */

typedef struct DELOPT {
    BOOL force, subdirs, quiet, prompt;
    DWORD need, forbid;
    BOOL attr_filter;
} DELOPT;

static void parse_attr_letters(const WCHAR *p, DWORD *need, DWORD *forbid)
{
    BOOL neg = FALSE;
    for (; *p; p++) {
        if (*p == L'-') {
            neg = TRUE;
            continue;
        }
        DWORD bit = 0;
        switch (towlower(*p)) {
        case L'd': bit = FILE_ATTRIBUTE_DIRECTORY; break;
        case L'r': bit = FILE_ATTRIBUTE_READONLY; break;
        case L'a': bit = FILE_ATTRIBUTE_ARCHIVE; break;
        case L's': bit = FILE_ATTRIBUTE_SYSTEM; break;
        case L'h': bit = FILE_ATTRIBUTE_HIDDEN; break;
        case L'l': bit = FILE_ATTRIBUTE_REPARSE_POINT; break;
        case L'i': bit = FILE_ATTRIBUTE_NOT_CONTENT_INDEXED; break;
        }
        if (neg)
            *forbid |= bit;
        else
            *need |= bit;
        neg = FALSE;
    }
}

static BOOL delete_in(const WCHAR *dir, const WCHAR *pattern, const DELOPT *o, int *found, int *deleted)
{
    STR spec;
    str_init(&spec);
    str_adds(&spec, dir);
    str_adds(&spec, pattern);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(str_get(&spec), &fd);
    BOOL all_ok = TRUE;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            if (o->attr_filter) {
                if ((fd.dwFileAttributes & o->need) != o->need || (fd.dwFileAttributes & o->forbid))
                    continue;
            }
            (*found)++;
            STR path;
            str_init(&path);
            str_adds(&path, dir);
            str_adds(&path, fd.cFileName);
            BOOL go = TRUE;
            if (o->prompt) {
                STR q;
                str_init(&q);
                str_fmt(&q, L"%ls, Delete (Y/N)? ", path.p);
                go = ask_yes_no(q.p);
                str_free(&q);
            }
            if (go) {
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) && o->force)
                    SetFileAttributesW(path.p, fd.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
                if (DeleteFileW(path.p)) {
                    (*deleted)++;
                    if (o->subdirs && g.ext)
                        out_fmt(L"Deleted file - %ls\r\n", path.p);
                } else {
                    DWORD e = GetLastError();
                    print_win_error(e);
                    err_fmt(L"%ls\r\n", path.p);
                    all_ok = FALSE;
                }
            }
            str_free(&path);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    str_free(&spec);
    if (o->subdirs) {
        STR sub_spec;
        str_init(&sub_spec);
        str_adds(&sub_spec, dir);
        str_adds(&sub_spec, L"*");
        h = FindFirstFileW(sub_spec.p, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !wcscmp(fd.cFileName, L".") ||
                    !wcscmp(fd.cFileName, L"..") || (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    continue;
                STR child;
                str_init(&child);
                str_adds(&child, dir);
                str_adds(&child, fd.cFileName);
                str_addc(&child, L'\\');
                if (!delete_in(child.p, pattern, o, found, deleted))
                    all_ok = FALSE;
                str_free(&child);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        str_free(&sub_spec);
    }
    return all_ok;
}

int internal_del(const WCHAR *args, NODE *node)
{
    ARGS a;
    args_split(args, &a, FALSE);
    DELOPT o = {};
    int names = 0, result = 0;
    for (int i = 0; i < a.n; i++) {
        const WCHAR *w = a.v[i];
        if (w[0] != L'/')
            continue;
        switch (towlower(w[1])) {
        case L'f': o.force = TRUE; break;
        case L's': o.subdirs = TRUE; break;
        case L'q': o.quiet = TRUE; break;
        case L'p': o.prompt = TRUE; break;
        case L'a':
            o.attr_filter = TRUE;
            parse_attr_letters(w[2] == L':' ? w + 3 : w + 2, &o.need, &o.forbid);
            break;
        default:
            err_fmt(L"Invalid switch - \"%ls\".\r\n", w + 1);
            args_free(&a);
            return 1;
        }
    }
    for (int i = 0; i < a.n; i++) {
        if (a.v[i][0] == L'/')
            continue;
        names++;
        WCHAR *spec = unquote(a.v[i]);
        STR full;
        str_init(&full);
        WCHAR buffer[MAX_PATH * 2];
        DWORD n = GetFullPathNameW(spec, NUM(buffer), buffer, nullptr);
        str_adds(&full, n && n < NUM(buffer) ? buffer : spec);
        /* a folder name deletes the files in it. */
        DWORD attr = GetFileAttributesW(full.p);
        STR dir, pattern;
        str_init(&dir);
        str_init(&pattern);
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
            str_adds(&dir, full.p);
            if (dir.len && dir.p[dir.len - 1] != L'\\')
                str_addc(&dir, L'\\');
            str_adds(&pattern, L"*");
            if (!o.quiet) {
                STR q;
                str_init(&q);
                str_fmt(&q, L"%ls*, Are you sure (Y/N)? ", dir.p);
                BOOL yes = ask_yes_no(q.p);
                str_free(&q);
                if (!yes) {
                    str_free(&dir);
                    str_free(&pattern);
                    str_free(&full);
                    xfree(spec);
                    continue;
                }
            }
        } else {
            path_split_pattern(full.p, &dir, &pattern);
            if (!o.quiet && (!wcscmp(pattern.p, L"*") || !wcscmp(pattern.p, L"*.*"))) {
                STR q;
                str_init(&q);
                str_fmt(&q, L"%ls%ls, Are you sure (Y/N)? ", dir.p, pattern.p);
                BOOL yes = ask_yes_no(q.p);
                str_free(&q);
                if (!yes) {
                    str_free(&dir);
                    str_free(&pattern);
                    str_free(&full);
                    xfree(spec);
                    continue;
                }
            }
        }
        int found = 0, deleted = 0;
        BOOL ok = delete_in(dir.p, pattern.p, &o, &found, &deleted);
        if (!found) {
            if (!o.subdirs || !g.ext)
                err_fmt(L"Could Not Find %ls\r\n", full.p);
            else
                err_fmt(L"Could Not Find %ls\r\n", full.p);
            set_errorlevel(1);
            result = 1;
        } else if (!ok) {
            set_errorlevel(1);
            result = 1;
        }
        str_free(&dir);
        str_free(&pattern);
        str_free(&full);
        xfree(spec);
    }
    if (!names) {
        msg_syntax();
        result = 1;
    }
    args_free(&a);
    return result;
}

/* ------------------------------------------------------------------ wildcard names */

/* the name a source file gets from a destination pattern with * and ?, name and extension apart. */
static void apply_pattern_part(const WCHAR *src, SIZE_T src_len, const WCHAR *pat, SIZE_T pat_len, STR *out)
{
    SIZE_T s = 0;
    for (SIZE_T i = 0; i < pat_len; i++) {
        if (pat[i] == L'*') {
            while (s < src_len)
                str_addc(out, src[s++]);
        } else if (pat[i] == L'?') {
            if (s < src_len)
                str_addc(out, src[s++]);
        } else {
            str_addc(out, pat[i]);
            if (s < src_len)
                s++;
        }
    }
}

static void apply_pattern(const WCHAR *src, const WCHAR *pattern, STR *out)
{
    const WCHAR *sd = wcsrchr(src, L'.');
    const WCHAR *pd = wcsrchr(pattern, L'.');
    SIZE_T s_name = sd ? (SIZE_T)(sd - src) : wcslen(src);
    SIZE_T p_name = pd ? (SIZE_T)(pd - pattern) : wcslen(pattern);
    apply_pattern_part(src, s_name, pattern, p_name, out);
    if (pd) {
        STR ext;
        str_init(&ext);
        apply_pattern_part(sd ? sd + 1 : L"", sd ? wcslen(sd + 1) : 0, pd + 1, wcslen(pd + 1), &ext);
        if (ext.len) {
            str_addc(out, L'.');
            str_adds(out, ext.p);
        }
        str_free(&ext);
    } else if (sd && !has_wildcards(pattern)) {
        /* no extension in the pattern: none in the result. */
    }
}

/* ------------------------------------------------------------------ copy and move */

typedef struct COPYITEM {
    WCHAR *spec;
} COPYITEM;

static BOOL same_file(const WCHAR *a, const WCHAR *b)
{
    WCHAR fa[MAX_PATH * 2], fb[MAX_PATH * 2];
    if (!GetFullPathNameW(a, NUM(fa), fa, nullptr) || !GetFullPathNameW(b, NUM(fb), fb, nullptr))
        return FALSE;
    return !_wcsicmp(fa, fb);
}

static BOOL append_file(HANDLE out, const WCHAR *source)
{
    HANDLE in = CreateFileW(source, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    if (in == INVALID_HANDLE_VALUE)
        return FALSE;
    unsigned char *buf = xalloc(65536);
    DWORD got, wrote;
    BOOL ok = TRUE;
    while (ReadFile(in, buf, 65536, &got, nullptr) && got) {
        if (!WriteFile(out, buf, got, &wrote, nullptr) || wrote != got) {
            ok = FALSE;
            break;
        }
    }
    xfree(buf);
    CloseHandle(in);
    return ok;
}

int internal_copy(const WCHAR *args, NODE *node)
{
    /* split into operands and switches. a + joins sources. */
    ARGS a;
    args_split(args, &a, FALSE);
    BOOL yes = g.batch != nullptr, no_prompt_set = FALSE;
    STR copycmd;
    str_init(&copycmd);
    if (env_get(L"COPYCMD", &copycmd)) {
        if (!_wcsnicmp(copycmd.p, L"/y", 2))
            yes = TRUE;
        else if (!_wcsnicmp(copycmd.p, L"/-y", 3))
            yes = FALSE;
    }
    str_free(&copycmd);
    (void)no_prompt_set;
    STR sources_list;
    str_init(&sources_list);
    WCHAR *operands[64];
    int count = 0;
    BOOL plus_between = FALSE;
    BOOL ascii_mode = FALSE;
    for (int i = 0; i < a.n; i++) {
        const WCHAR *w = a.v[i];
        if (w[0] == L'/') {
            if (!_wcsicmp(w, L"/y"))
                yes = TRUE;
            else if (!_wcsicmp(w, L"/-y"))
                yes = FALSE;
            else if (!_wcsicmp(w, L"/a"))
                ascii_mode = TRUE;
            else if (!_wcsicmp(w, L"/b") || !_wcsicmp(w, L"/v") || !_wcsicmp(w, L"/n") || !_wcsicmp(w, L"/z") ||
                     !_wcsicmp(w, L"/d") || !_wcsicmp(w, L"/l"))
                ;
            else {
                err_fmt(L"Invalid switch - \"%ls\".\r\n", w + 1);
                args_free(&a);
                return 1;
            }
            continue;
        }
        /* pieces around + */
        const WCHAR *p = w;
        BOOL quoted = FALSE;
        STR piece;
        str_init(&piece);
        for (;; p++) {
            if (*p == L'"')
                quoted = !quoted;
            if ((*p == L'+' && !quoted) || !*p) {
                if (piece.len && count < 63)
                    operands[count++] = str_take(&piece);
                if (*p == L'+')
                    plus_between = TRUE;
                str_init(&piece);
                if (!*p)
                    break;
                continue;
            }
            str_addc(&piece, *p);
        }
    }
    (void)ascii_mode;
    args_free(&a);
    if (count == 0) {
        msg_syntax();
        return 1;
    }
    /* with a + every operand but a trailing one that follows a space is a source. the destination is the last
       operand when the line has no plus between all of them. */
    int source_count = count;
    WCHAR *dest = nullptr;
    if (!plus_between) {
        if (count >= 2) {
            dest = operands[count - 1];
            source_count = count - 1;
        }
    } else if (count >= 2) {
        /* "a + b dest": the operand after the last + is a source, a further one is the destination. We cannot
           tell the grouping from the pieces alone, so look at the original text. */
        const WCHAR *t = args;
        const WCHAR *last_plus = nullptr;
        BOOL q = FALSE;
        for (const WCHAR *c = t; *c; c++) {
            if (*c == L'"')
                q = !q;
            else if (*c == L'+' && !q)
                last_plus = c;
        }
        if (last_plus) {
            const WCHAR *after = skip_space(last_plus + 1);
            BOOL in_q = FALSE;
            const WCHAR *c = after;
            while (*c && (in_q || !is_space(*c))) {
                if (*c == L'"')
                    in_q = !in_q;
                c++;
            }
            const WCHAR *rest = skip_space(c);
            while (*rest == L'/') {
                while (*rest && !is_space(*rest))
                    rest++;
                rest = skip_space(rest);
            }
            if (*rest) {
                dest = operands[count - 1];
                source_count = count - 1;
            }
        }
    }
    WCHAR *dest_text = dest ? unquote(dest) : nullptr;
    BOOL dest_is_dir = FALSE;
    STR dest_full;
    str_init(&dest_full);
    if (dest_text) {
        WCHAR buffer[MAX_PATH * 2];
        DWORD n = GetFullPathNameW(dest_text, NUM(buffer), buffer, nullptr);
        str_adds(&dest_full, n && n < NUM(buffer) ? buffer : dest_text);
        DWORD attr = GetFileAttributesW(dest_full.p);
        size_t dl = wcslen(dest_text);
        dest_is_dir = (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) ||
                      (dl && (dest_text[dl - 1] == L'\\' || dest_text[dl - 1] == L'/'));
    }
    /* collect source files. */
    STR names;
    str_init(&names);
    WCHAR **files = nullptr;
    int nfiles = 0, files_cap = 0;
    BOOL wildcard_used = FALSE;
    for (int i = 0; i < source_count; i++) {
        WCHAR *spec = unquote(operands[i]);
        if (has_wildcards(spec)) {
            wildcard_used = TRUE;
            SIZE_T found;
            WCHAR **matches = list_matching(spec, &found, FALSE);
            for (SIZE_T k = 0; k < found; k++) {
                if (nfiles == files_cap) {
                    files_cap = files_cap ? files_cap * 2 : 16;
                    files = xrealloc(files, (SIZE_T)files_cap * sizeof(WCHAR *));
                }
                files[nfiles++] = matches[k];
            }
            xfree(matches);
        } else {
            DWORD attr = GetFileAttributesW(spec);
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
                /* a folder as a source copies the files in it. */
                STR all;
                str_init(&all);
                str_adds(&all, spec);
                str_adds(&all, L"\\*");
                SIZE_T found;
                WCHAR **matches = list_matching(str_get(&all), &found, FALSE);
                if (found)
                    wildcard_used = TRUE;
                for (SIZE_T k = 0; k < found; k++) {
                    if (nfiles == files_cap) {
                        files_cap = files_cap ? files_cap * 2 : 16;
                        files = xrealloc(files, (SIZE_T)files_cap * sizeof(WCHAR *));
                    }
                    files[nfiles++] = matches[k];
                }
                xfree(matches);
                str_free(&all);
            } else {
                if (nfiles == files_cap) {
                    files_cap = files_cap ? files_cap * 2 : 16;
                    files = xrealloc(files, (SIZE_T)files_cap * sizeof(WCHAR *));
                }
                files[nfiles++] = xdup(spec);
            }
        }
        xfree(spec);
    }
    int copied = 0, result = 0;
    BOOL show_names = wildcard_used || plus_between;
    BOOL concatenate = plus_between || (wildcard_used && dest_text && !dest_is_dir && !has_wildcards(dest_text) &&
                                        nfiles > 1);
    if (concatenate && nfiles > 0) {
        STR target;
        str_init(&target);
        if (dest_text && !dest_is_dir) {
            str_adds(&target, dest_text);
        } else {
            /* the first source names the result. */
            const WCHAR *slash = wcsrchr(files[0], L'\\');
            str_adds(&target, slash ? slash + 1 : files[0]);
            if (dest_is_dir) {
                str_reset(&target);
                str_adds(&target, dest_text);
                if (target.len && target.p[target.len - 1] != L'\\')
                    str_addc(&target, L'\\');
                str_adds(&target, slash ? slash + 1 : files[0]);
            }
        }
        BOOL proceed = TRUE;
        if (GetFileAttributesW(target.p) != INVALID_FILE_ATTRIBUTES && !yes) {
            int a2 = ask_overwrite(target.p);
            proceed = a2 != 0;
        }
        HANDLE out = INVALID_HANDLE_VALUE;
        if (proceed) {
            out = CreateFileW(target.p, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
            if (out == INVALID_HANDLE_VALUE) {
                fail_last_error();
                result = 1;
            }
        }
        if (out != INVALID_HANDLE_VALUE) {
            for (int i = 0; i < nfiles; i++) {
                if (same_file(files[i], target.p)) {
                    continue;
                }
                if (show_names)
                    out_fmt(L"%ls\r\n", files[i]);
                if (!append_file(out, files[i])) {
                    print_win_error(GetLastError());
                    set_errorlevel(1);
                    result = 1;
                } else if (i == nfiles - 1 || TRUE) {
                    /* one file is produced. */
                }
            }
            CloseHandle(out);
            copied = 1;
        }
        str_free(&target);
    } else {
        BOOL all = yes;
        for (int i = 0; i < nfiles; i++) {
            const WCHAR *src = files[i];
            const WCHAR *slash = wcsrchr(src, L'\\');
            const WCHAR *src_name = slash ? slash + 1 : src;
            STR target;
            str_init(&target);
            if (!dest_text) {
                str_adds(&target, src_name);
            } else if (dest_is_dir) {
                str_adds(&target, dest_text);
                if (target.len && target.p[target.len - 1] != L'\\' && target.p[target.len - 1] != L'/')
                    str_addc(&target, L'\\');
                str_adds(&target, src_name);
            } else if (has_wildcards(dest_text)) {
                STR dd, dp;
                str_init(&dd);
                str_init(&dp);
                path_split_pattern(dest_text, &dd, &dp);
                str_adds(&target, dd.p);
                apply_pattern(src_name, dp.p, &target);
                str_free(&dd);
                str_free(&dp);
            } else {
                str_adds(&target, dest_text);
            }
            if (GetFileAttributesW(src) == INVALID_FILE_ATTRIBUTES) {
                err_s(L"The system cannot find the file specified.\r\n");
                set_errorlevel(1);
                result = 1;
                str_free(&target);
                continue;
            }
            if (same_file(src, str_get(&target))) {
                err_s(L"The file cannot be copied onto itself.\r\n");
                set_errorlevel(1);
                result = 1;
                str_free(&target);
                continue;
            }
            if (show_names)
                out_fmt(L"%ls\r\n", src);
            BOOL go = TRUE;
            if (!all && GetFileAttributesW(target.p) != INVALID_FILE_ATTRIBUTES) {
                int answer = ask_overwrite(target.p);
                if (answer == 2)
                    all = TRUE;
                go = answer != 0;
            }
            if (go) {
                if (CopyFileW(src, target.p, FALSE)) {
                    copied++;
                } else {
                    DWORD e = GetLastError();
                    if (e == ERROR_ACCESS_DENIED) {
                        DWORD ta = GetFileAttributesW(target.p);
                        if (ta != INVALID_FILE_ATTRIBUTES && (ta & FILE_ATTRIBUTE_READONLY)) {
                            /* a read-only target is replaced, it is a plain error otherwise. */
                        }
                    }
                    SetLastError(e);
                    print_win_error(e);
                    set_errorlevel(1);
                    result = 1;
                }
            }
            str_free(&target);
        }
        if (nfiles == 0 && !result) {
            err_s(L"The system cannot find the file specified.\r\n");
            set_errorlevel(1);
            result = 1;
        }
    }
    out_fmt(L"%9d file(s) copied.\r\n", copied);
    for (int i = 0; i < nfiles; i++)
        xfree(files[i]);
    xfree(files);
    for (int i = 0; i < count; i++)
        xfree(operands[i]);
    xfree(dest_text);
    str_free(&dest_full);
    str_free(&names);
    str_free(&sources_list);
    return result;
}

int internal_move(const WCHAR *args, NODE *node)
{
    ARGS a;
    args_split(args, &a, FALSE);
    BOOL yes = g.batch != nullptr;
    WCHAR *operands[8];
    int count = 0;
    for (int i = 0; i < a.n; i++) {
        if (a.v[i][0] == L'/') {
            if (!_wcsicmp(a.v[i], L"/y"))
                yes = TRUE;
            else if (!_wcsicmp(a.v[i], L"/-y"))
                yes = FALSE;
            else {
                err_fmt(L"Invalid switch - \"%ls\".\r\n", a.v[i] + 1);
                args_free(&a);
                return 1;
            }
            continue;
        }
        if (count < 8)
            operands[count++] = unquote(a.v[i]);
    }
    args_free(&a);
    if (count < 1) {
        msg_syntax();
        return 1;
    }
    WCHAR *src_spec = operands[0];
    WCHAR *dest_text = count >= 2 ? operands[1] : nullptr;
    int result = 0, moved_files = 0, moved_dirs = 0;
    STR dest_full;
    str_init(&dest_full);
    BOOL dest_is_dir = FALSE;
    if (dest_text) {
        WCHAR buffer[MAX_PATH * 2];
        DWORD n = GetFullPathNameW(dest_text, NUM(buffer), buffer, nullptr);
        str_adds(&dest_full, n && n < NUM(buffer) ? buffer : dest_text);
        DWORD attr = GetFileAttributesW(dest_full.p);
        size_t dl = wcslen(dest_text);
        dest_is_dir = (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) ||
                      (dl && (dest_text[dl - 1] == L'\\' || dest_text[dl - 1] == L'/'));
    } else {
        get_cwd(&dest_full);
        dest_is_dir = TRUE;
    }
    STR dir, pattern;
    str_init(&dir);
    str_init(&pattern);
    path_split_pattern(src_spec, &dir, &pattern);
    BOOL wild = has_wildcards(src_spec);
    SIZE_T entry_count;
    WIN32_FIND_DATAW *entries = find_all_sorted(src_spec, &entry_count);
    BOOL all = yes;
    int matched = 0;
    for (SIZE_T entry_index = 0; entry_index < entry_count; entry_index++) {
        WIN32_FIND_DATAW fd = entries[entry_index];
        {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
                continue;
            matched++;
            STR src;
            str_init(&src);
            str_adds(&src, dir.p);
            str_adds(&src, fd.cFileName);
            STR target;
            str_init(&target);
            if (dest_is_dir) {
                str_adds(&target, dest_full.p);
                if (target.len && target.p[target.len - 1] != L'\\')
                    str_addc(&target, L'\\');
                str_adds(&target, fd.cFileName);
            } else if (has_wildcards(dest_text)) {
                STR dd, dp;
                str_init(&dd);
                str_init(&dp);
                path_split_pattern(dest_text, &dd, &dp);
                str_adds(&target, dd.p);
                apply_pattern(fd.cFileName, dp.p, &target);
                str_free(&dd);
                str_free(&dp);
            } else {
                str_adds(&target, dest_full.p);
            }
            BOOL is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (wild)
                out_fmt(L"%ls\r\n", src.p);
            BOOL go = TRUE;
            DWORD existing = GetFileAttributesW(target.p);
            if (!all && existing != INVALID_FILE_ATTRIBUTES && !is_dir) {
                int answer = ask_overwrite(target.p);
                if (answer == 2)
                    all = TRUE;
                go = answer != 0;
            }
            if (go) {
                DWORD flags = MOVEFILE_COPY_ALLOWED;
                if (!is_dir)
                    flags |= MOVEFILE_REPLACE_EXISTING;
                if (MoveFileExW(src.p, target.p, flags)) {
                    if (is_dir)
                        moved_dirs++;
                    else
                        moved_files++;
                } else {
                    print_win_error(GetLastError());
                    set_errorlevel(1);
                    result = 1;
                }
            }
            str_free(&target);
            str_free(&src);
        }
    }
    xfree(entries);
    if (!matched) {
        err_s(L"The system cannot find the file specified.\r\n");
        set_errorlevel(1);
        result = 1;
    } else {
        if (moved_dirs)
            out_fmt(L"%9d dir(s) moved.\r\n", moved_dirs);
        if (moved_files || !moved_dirs)
            out_fmt(L"%9d file(s) moved.\r\n", moved_files);
    }
    str_free(&dir);
    str_free(&pattern);
    str_free(&dest_full);
    for (int i = 0; i < count; i++)
        xfree(operands[i]);
    return result;
}

int internal_ren(const WCHAR *args, NODE *node)
{
    ARGS a;
    args_split(args, &a, FALSE);
    if (a.n != 2) {
        msg_syntax();
        args_free(&a);
        return 1;
    }
    WCHAR *src_spec = unquote(a.v[0]);
    WCHAR *new_name = unquote(a.v[1]);
    args_free(&a);
    if (wcspbrk(new_name, L"\\/:")) {
        msg_syntax();
        xfree(src_spec);
        xfree(new_name);
        return 1;
    }
    STR dir, pattern;
    str_init(&dir);
    str_init(&pattern);
    path_split_pattern(src_spec, &dir, &pattern);
    SIZE_T entry_count;
    WIN32_FIND_DATAW *entries = find_all_sorted(src_spec, &entry_count);
    int result = 0, matched = 0;
    for (SIZE_T entry_index = 0; entry_index < entry_count; entry_index++) {
        WIN32_FIND_DATAW fd = entries[entry_index];
        {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
                continue;
            matched++;
            STR src, target;
            str_init(&src);
            str_init(&target);
            str_adds(&src, dir.p);
            str_adds(&src, fd.cFileName);
            str_adds(&target, dir.p);
            if (has_wildcards(new_name))
                apply_pattern(fd.cFileName, new_name, &target);
            else
                str_adds(&target, new_name);
            if (GetFileAttributesW(target.p) != INVALID_FILE_ATTRIBUTES && _wcsicmp(src.p, target.p)) {
                err_s(L"A duplicate file name exists, or the file\r\ncannot be found.\r\n");
                set_errorlevel(1);
                result = 1;
            } else if (!MoveFileExW(src.p, target.p, 0)) {
                DWORD e = GetLastError();
                if (e == ERROR_ALREADY_EXISTS || e == ERROR_FILE_EXISTS)
                    err_s(L"A duplicate file name exists, or the file\r\ncannot be found.\r\n");
                else
                    print_win_error(e);
                set_errorlevel(1);
                result = 1;
            }
            str_free(&src);
            str_free(&target);
        }
    }
    xfree(entries);
    if (!matched) {
        err_s(L"The system cannot find the file specified.\r\n");
        set_errorlevel(1);
        result = 1;
    }
    str_free(&dir);
    str_free(&pattern);
    xfree(src_spec);
    xfree(new_name);
    return result;
}

/* ------------------------------------------------------------------ type */

static int type_file(const WCHAR *path)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)
            err_fmt(L"The system cannot find the file specified.\r\n");
        else
            print_win_error(e);
        set_errorlevel(1);
        return 1;
    }
    unsigned char *buf = xalloc(32768);
    DWORD got;
    BOOL console = g_io.con[1];
    char carry[4];
    DWORD carry_len = 0;
    while (ReadFile(f, buf, 32768, &got, nullptr) && got) {
        if (console) {
            /* keep a split multibyte character for the next block. */
            unsigned char *data = buf;
            DWORD len = got;
            char *joined = nullptr;
            if (carry_len) {
                joined = xalloc(len + carry_len);
                memcpy(joined, carry, carry_len);
                memcpy(joined + carry_len, buf, len);
                data = (unsigned char *)joined;
                len += carry_len;
                carry_len = 0;
            }
            if (g.out_cp == CP_UTF8) {
                DWORD back = 0;
                while (back < 3 && back < len && (data[len - 1 - back] & 0xC0) == 0x80)
                    back++;
                if (back < len && (data[len - 1 - back] & 0xC0) == 0xC0) {
                    DWORD need = data[len - 1 - back] >= 0xF0 ? 4 : data[len - 1 - back] >= 0xE0 ? 3 : 2;
                    if (back + 1 < need) {
                        carry_len = back + 1;
                        memcpy(carry, data + len - carry_len, carry_len);
                        len -= carry_len;
                    }
                }
            }
            int chars = MultiByteToWideChar(g.out_cp, 0, (char *)data, (int)len, nullptr, 0);
            if (chars > 0) {
                WCHAR *w = xalloc(((SIZE_T)chars + 1) * sizeof(WCHAR));
                MultiByteToWideChar(g.out_cp, 0, (char *)data, (int)len, w, chars);
                write_to(g_io.h[1], TRUE, w, (SIZE_T)chars);
                xfree(w);
            }
            xfree(joined);
        } else {
            DWORD wrote;
            WriteFile(g_io.h[1], buf, got, &wrote, nullptr);
        }
        if (g.ctrlc)
            break;
    }
    xfree(buf);
    CloseHandle(f);
    return 0;
}

int internal_type(const WCHAR *args, NODE *node)
{
    ARGS a;
    args_split(args, &a, FALSE);
    int result = 0, names = 0;
    for (int i = 0; i < a.n; i++)
        if (a.v[i][0] != L'/')
            names++;
    if (!names) {
        err_s(L"The syntax of the command is incorrect.\r\n");
        args_free(&a);
        return 1;
    }
    for (int i = 0; i < a.n; i++) {
        if (a.v[i][0] == L'/') {
            err_fmt(L"Invalid switch - \"%ls\".\r\n", a.v[i] + 1);
            result = 1;
            continue;
        }
        WCHAR *spec = unquote(a.v[i]);
        if (has_wildcards(spec)) {
            SIZE_T found;
            WCHAR **matches = list_matching(spec, &found, FALSE);
            for (SIZE_T k = 0; k < found; k++) {
                out_fmt(L"\r\n%ls\r\n\r\n\r\n", matches[k]);
                if (type_file(matches[k]))
                    result = 1;
                xfree(matches[k]);
            }
            xfree(matches);
            if (!found) {
                err_s(L"The system cannot find the file specified.\r\n");
                set_errorlevel(1);
                result = 1;
            }
        } else {
            if (names > 1)
                out_fmt(L"\r\n%ls\r\n\r\n\r\n", spec);
            if (type_file(spec))
                result = 1;
        }
        xfree(spec);
    }
    args_free(&a);
    return result;
}

/* ------------------------------------------------------------------ vol, mklink, assoc, ftype */

static void show_volume(WCHAR letter)
{
    WCHAR root[4] = {upcase(letter), L':', L'\\', 0};
    WCHAR label[MAX_PATH];
    DWORD serial = 0;
    if (!GetVolumeInformationW(root, label, NUM(label), &serial, nullptr, nullptr, nullptr, 0)) {
        print_win_error(GetLastError());
        set_errorlevel(1);
        return;
    }
    if (label[0])
        out_fmt(L" Volume in drive %lc is %ls\r\n", root[0], label);
    else
        out_fmt(L" Volume in drive %lc has no label.\r\n", root[0]);
    out_fmt(L" Volume Serial Number is %04X-%04X\r\n", serial >> 16, serial & 0xFFFF);
}

int internal_vol(const WCHAR *args, NODE *node)
{
    const WCHAR *p = skip_space(args);
    WCHAR letter = 0;
    if (*p && p[1] == L':')
        letter = p[0];
    else if (*p) {
        msg_syntax();
        return 1;
    }
    if (!letter) {
        STR cwd;
        str_init(&cwd);
        get_cwd(&cwd);
        letter = cwd.len >= 2 ? cwd.p[0] : L'C';
        str_free(&cwd);
    }
    show_volume(letter);
    return 0;
}

int internal_mklink(const WCHAR *args, NODE *node)
{
    ARGS a;
    args_split(args, &a, FALSE);
    BOOL dir = FALSE, hard = FALSE, junction = FALSE;
    WCHAR *names[2] = {};
    int n = 0;
    for (int i = 0; i < a.n; i++) {
        if (a.v[i][0] == L'/' && a.v[i][1] && !a.v[i][2]) {
            WCHAR c = towlower(a.v[i][1]);
            if (c == L'd')
                dir = TRUE;
            else if (c == L'h')
                hard = TRUE;
            else if (c == L'j')
                junction = TRUE;
            else {
                msg_syntax();
                args_free(&a);
                return 1;
            }
            continue;
        }
        if (n < 2)
            names[n++] = unquote(a.v[i]);
        else
            n = 3;
    }
    args_free(&a);
    if (n != 2) {
        out_s(L"Creates a symbolic link.\r\n\r\nMKLINK [[/D] | [/H] | [/J]] Link Target\r\n");
        xfree(names[0]);
        xfree(names[1]);
        return 1;
    }
    BOOL ok;
    if (hard)
        ok = CreateHardLinkW(names[0], names[1], nullptr);
    else if (junction) {
        SetLastError(ERROR_NOT_SUPPORTED);
        ok = FALSE;
    } else
        ok = CreateSymbolicLinkW(names[0], names[1], dir ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0);
    int result = 0;
    if (ok) {
        out_fmt(hard ? L"Hardlink created for %ls <<===>> %ls\r\n" : L"symbolic link created for %ls <<===>> %ls\r\n",
                names[0], names[1]);
    } else {
        DWORD e = GetLastError();
        if (e == ERROR_PRIVILEGE_NOT_HELD)
            err_s(L"You do not have sufficient privilege to perform this operation.\r\n");
        else
            print_win_error(e);
        set_errorlevel(1);
        result = 1;
    }
    xfree(names[0]);
    xfree(names[1]);
    return result;
}

static BOOL reg_query_default(HKEY root, const WCHAR *subkey, STR *value)
{
    HKEY key;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key))
        return FALSE;
    DWORD type, size = 0;
    LSTATUS s = RegQueryValueExW(key, nullptr, nullptr, &type, nullptr, &size);
    BOOL ok = FALSE;
    if (!s && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        WCHAR *buf = xalloc(size + 4);
        if (!RegQueryValueExW(key, nullptr, nullptr, &type, (BYTE *)buf, &size)) {
            str_adds(value, buf);
            ok = TRUE;
        }
        xfree(buf);
    }
    RegCloseKey(key);
    return ok;
}

int internal_assoc(const WCHAR *args, NODE *node)
{
    const WCHAR *p = skip_space(args);
    if (!*p) {
        HKEY key;
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, nullptr, 0, KEY_READ, &key))
            return 1;
        for (DWORD i = 0;; i++) {
            WCHAR name[256];
            DWORD n = NUM(name);
            if (RegEnumKeyExW(key, i, name, &n, nullptr, nullptr, nullptr, nullptr))
                break;
            if (name[0] != L'.')
                continue;
            STR v;
            str_init(&v);
            if (reg_query_default(key, name, &v) && v.len)
                out_fmt(L"%ls=%ls\r\n", name, str_get(&v));
            str_free(&v);
        }
        RegCloseKey(key);
        return 0;
    }
    STR spec;
    str_init(&spec);
    str_adds(&spec, p);
    trim_trailing_space(&spec);
    WCHAR *eq = wcschr(spec.p, L'=');
    int result = 0;
    if (!eq) {
        STR v;
        str_init(&v);
        if (reg_query_default(HKEY_CLASSES_ROOT, spec.p, &v) && v.len)
            out_fmt(L"%ls=%ls\r\n", spec.p, str_get(&v));
        else {
            err_fmt(L"File association not found for extension %ls\r\n", spec.p);
            set_errorlevel(1);
            result = 1;
        }
        str_free(&v);
    } else {
        *eq = 0;
        const WCHAR *type = eq + 1;
        HKEY key;
        if (*type) {
            DWORD disp;
            LSTATUS s = RegCreateKeyExW(HKEY_CLASSES_ROOT, spec.p, 0, nullptr, 0, KEY_WRITE, nullptr, &key, &disp);
            if (!s) {
                s = RegSetValueExW(key, nullptr, 0, REG_SZ, (const BYTE *)type, (DWORD)((wcslen(type) + 1) * sizeof(WCHAR)));
                RegCloseKey(key);
            }
            if (s) {
                print_win_error((DWORD)s);
                set_errorlevel(1);
                result = 1;
            } else {
                out_fmt(L"%ls=%ls\r\n", spec.p, type);
            }
        } else {
            LSTATUS s = RegDeleteKeyW(HKEY_CLASSES_ROOT, spec.p);
            if (s) {
                err_fmt(L"File association not found for extension %ls\r\n", spec.p);
                set_errorlevel(1);
                result = 1;
            }
        }
    }
    str_free(&spec);
    return result;
}

int internal_ftype(const WCHAR *args, NODE *node)
{
    const WCHAR *p = skip_space(args);
    if (!*p) {
        HKEY key;
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, nullptr, 0, KEY_READ, &key))
            return 1;
        for (DWORD i = 0;; i++) {
            WCHAR name[256];
            DWORD n = NUM(name);
            if (RegEnumKeyExW(key, i, name, &n, nullptr, nullptr, nullptr, nullptr))
                break;
            if (name[0] == L'.')
                continue;
            STR sub, v;
            str_init(&sub);
            str_init(&v);
            str_fmt(&sub, L"%ls\\shell\\open\\command", name);
            if (reg_query_default(key, sub.p, &v) && v.len)
                out_fmt(L"%ls=%ls\r\n", name, str_get(&v));
            str_free(&sub);
            str_free(&v);
        }
        RegCloseKey(key);
        return 0;
    }
    STR spec;
    str_init(&spec);
    str_adds(&spec, p);
    trim_trailing_space(&spec);
    WCHAR *eq = wcschr(spec.p, L'=');
    int result = 0;
    if (!eq) {
        STR sub, v;
        str_init(&sub);
        str_init(&v);
        str_fmt(&sub, L"%ls\\shell\\open\\command", spec.p);
        if (reg_query_default(HKEY_CLASSES_ROOT, sub.p, &v) && v.len)
            out_fmt(L"%ls=%ls\r\n", spec.p, str_get(&v));
        else {
            err_fmt(L"File type '%ls' not found or no open command associated with it.\r\n", spec.p);
            set_errorlevel(1);
            result = 1;
        }
        str_free(&sub);
        str_free(&v);
    } else {
        *eq = 0;
        const WCHAR *command = eq + 1;
        STR sub;
        str_init(&sub);
        str_fmt(&sub, L"%ls\\shell\\open\\command", spec.p);
        if (*command) {
            HKEY key;
            DWORD disp;
            LSTATUS s = RegCreateKeyExW(HKEY_CLASSES_ROOT, sub.p, 0, nullptr, 0, KEY_WRITE, nullptr, &key, &disp);
            if (!s) {
                s = RegSetValueExW(key, nullptr, 0, REG_SZ, (const BYTE *)command,
                                   (DWORD)((wcslen(command) + 1) * sizeof(WCHAR)));
                RegCloseKey(key);
            }
            if (s) {
                print_win_error((DWORD)s);
                set_errorlevel(1);
                result = 1;
            } else {
                out_fmt(L"%ls=%ls\r\n", spec.p, command);
            }
        } else {
            RegDeleteKeyW(HKEY_CLASSES_ROOT, sub.p);
        }
        str_free(&sub);
    }
    str_free(&spec);
    return result;
}
