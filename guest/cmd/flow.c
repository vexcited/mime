/* if and for. */
#include "cmd.h"

/* ------------------------------------------------------------------ if */

static int compare_strings(const WCHAR *a, const WCHAR *b, BOOL icase)
{
    int r = CompareStringW(LOCALE_USER_DEFAULT, icase ? NORM_IGNORECASE : 0, a, -1, b, -1);
    return r ? r - 2 : wcscmp(a, b);
}

static BOOL path_exists(const WCHAR *spec)
{
    STR clean;
    str_init(&clean);
    str_adds(&clean, spec);
    strip_quotes_inplace(clean.p ? clean.p : (WCHAR *)L"");
    clean.len = wcslen(str_get(&clean));
    BOOL result = FALSE;
    if (has_wildcards(clean.p)) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(clean.p, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..")) {
                    result = TRUE;
                    break;
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    } else {
        /* a trailing backslash names a folder. */
        result = GetFileAttributesW(clean.p) != INVALID_FILE_ATTRIBUTES;
    }
    str_free(&clean);
    return result;
}

static BOOL digits_only(const WCHAR *s, BOOL allow_minus)
{
    for (SIZE_T i = 0; s[i]; i++) {
        if (i == 0 && allow_minus && s[i] == L'-' && s[1])
            continue;
        if (!is_digit(s[i]))
            return FALSE;
    }
    return s[0] != 0;
}

int exec_if(NODE *n)
{
    STR lhs, rhs;
    str_init(&lhs);
    str_init(&rhs);
    NODE *branch = nullptr;
    BOOL cond = FALSE;
    {
        STR text;
        str_init(&text);
        expand_forvars(n->lhs, &text);
        if (g.delayed && wcschr(str_get(&text), L'!')) {
            if (!expand_delayed(text.p, &lhs)) {
                str_free(&text);
                str_free(&lhs);
                return 1;
            }
        } else {
            str_adds(&lhs, str_get(&text));
        }
        str_free(&text);
        if (n->rhs) {
            expand_forvars(n->rhs, &text);
            if (g.delayed && wcschr(str_get(&text), L'!')) {
                if (!expand_delayed(text.p, &rhs)) {
                    str_free(&text);
                    str_free(&lhs);
                    str_free(&rhs);
                    return 1;
                }
            } else {
                str_adds(&rhs, str_get(&text));
            }
            str_free(&text);
        }
    }
    const WCHAR *l = str_get(&lhs);
    switch (n->if_kind) {
    case IF_ERRORLEVEL:
    case IF_CMDEXT: {
        if (!digits_only(l, n->if_kind == IF_ERRORLEVEL)) {
            msg_unexpected(l);
            str_free(&lhs);
            str_free(&rhs);
            return 1;
        }
        LONG want = parse_long(l, nullptr);
        /* decimal only: a leading zero is not octal here. */
        want = 0;
        BOOL neg = l[0] == L'-';
        for (const WCHAR *p = l + (neg ? 1 : 0); *p; p++)
            want = want * 10 + (*p - L'0');
        if (neg)
            want = -want;
        cond = n->if_kind == IF_ERRORLEVEL ? (LONG)g.errorlevel >= want : 2 >= want;
        break;
    }
    case IF_EXIST:
        cond = path_exists(l);
        break;
    case IF_DEFINED: {
        STR name;
        str_init(&name);
        str_adds(&name, l);
        strip_quotes_inplace(name.p ? name.p : (WCHAR *)L"");
        cond = env_defined(str_get(&name));
        str_free(&name);
        break;
    }
    case IF_STRING:
        cond = compare_strings(l, str_get(&rhs), n->icase) == 0;
        break;
    case IF_COMPARE: {
        LONG a, b;
        int c;
        if (all_numeric_long(l, &a) && all_numeric_long(rhs.p, &b))
            c = a < b ? -1 : a > b ? 1 : 0;
        else
            c = compare_strings(l, str_get(&rhs), n->icase);
        switch (n->op) {
        case CMP_EQU: cond = c == 0; break;
        case CMP_NEQ: cond = c != 0; break;
        case CMP_LSS: cond = c < 0; break;
        case CMP_LEQ: cond = c <= 0; break;
        case CMP_GTR: cond = c > 0; break;
        case CMP_GEQ: cond = c >= 0; break;
        }
        break;
    }
    }
    str_free(&lhs);
    str_free(&rhs);
    if (n->negate)
        cond = !cond;
    branch = cond ? n->a : n->b;
    return branch ? exec_node(branch, FALSE) : 0;
}

/* ------------------------------------------------------------------ for */

typedef struct FORRUN {
    NODE *node;
    FORCTX *ctx;
    int result;
    BOOL stop;
} FORRUN;

static void for_body(FORRUN *run, const WCHAR *value)
{
    if (run->stop)
        return;
    forctx_set(run->ctx, run->node->for_var, value);
    NODE *body = run->node->a;
    if (g.echo && !g.quiet && g.batch && body->kind != N_QUIET && body->kind != N_EMPTY && body->src) {
        STR shown;
        str_init(&shown);
        expand_forvars(body->src, &shown);
        print_prompt();
        out_s(str_get(&shown));
        out_s(L"\r\n");
        str_free(&shown);
    }
    run->result = exec_node(body, FALSE);
    if (g.goto_pending || g.abort_all || (g.ctrlc && ask_terminate_batch()))
        run->stop = TRUE;
}

/* the words of a plain set, split on blanks and ,;= with quotes kept. */
static void set_words(const WCHAR *set, ARGS *a)
{
    args_split(set, a, TRUE);
}

static int compare_entry_names(const void *x, const void *y)
{
    return compare_names((const WCHAR *)x, (const WCHAR *)y);
}

/* the names in a folder that match, files or folders, sorted. */
static void list_matches(const WCHAR *dir, const WCHAR *pattern, BOOL want_dirs, void ***names, SIZE_T *count)
{
    STR spec;
    str_init(&spec);
    str_adds(&spec, dir);
    str_adds(&spec, pattern);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(str_get(&spec), &fd);
    SIZE_T cap = 0;
    *names = nullptr;
    *count = 0;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            BOOL is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (is_dir != want_dirs)
                continue;
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
                continue;
            if (*count == cap) {
                cap = cap ? cap * 2 : 16;
                *names = xrealloc(*names, cap * sizeof(void *));
            }
            (*names)[(*count)++] = xdup(fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    str_free(&spec);
    sort_pointers(*names, *count, compare_entry_names);
}

static void free_names(void **names, SIZE_T count)
{
    for (SIZE_T i = 0; i < count; i++)
        xfree(names[i]);
    xfree(names);
}

static void run_plain_item(FORRUN *run, const WCHAR *item, BOOL dirs_only)
{
    if (has_wildcards(item)) {
        STR dir, pattern;
        str_init(&dir);
        str_init(&pattern);
        STR clean;
        str_init(&clean);
        str_adds(&clean, item);
        strip_quotes_inplace(clean.p);
        path_split_pattern(clean.p, &dir, &pattern);
        void **names;
        SIZE_T count;
        list_matches(dir.p, pattern.p, dirs_only, &names, &count);
        for (SIZE_T i = 0; i < count && !run->stop; i++) {
            STR full;
            str_init(&full);
            str_adds(&full, dir.p);
            str_adds(&full, names[i]);
            for_body(run, str_get(&full));
            str_free(&full);
        }
        free_names(names, count);
        str_free(&clean);
        str_free(&dir);
        str_free(&pattern);
    } else {
        for_body(run, item);
    }
}

static void for_plain(FORRUN *run, const WCHAR *set, BOOL dirs_only)
{
    ARGS a;
    set_words(set, &a);
    for (int i = 0; i < a.n && !run->stop; i++)
        run_plain_item(run, a.v[i], dirs_only);
    args_free(&a);
}

static void for_numbers(FORRUN *run, const WCHAR *set)
{
    ARGS a;
    set_words(set, &a);
    if (a.n < 3) {
        args_free(&a);
        msg_syntax();
        run->result = 1;
        return;
    }
    LONG start = parse_long(a.v[0], nullptr), step = parse_long(a.v[1], nullptr), end = parse_long(a.v[2], nullptr);
    args_free(&a);
    if (!step)
        return;
    for (LONGLONG i = start; step > 0 ? i <= end : i >= end; i += step) {
        WCHAR text[16];
        _snwprintf(text, NUM(text), L"%ld", (long)i);
        for_body(run, text);
        if (run->stop)
            break;
    }
}

static void recurse_dir(FORRUN *run, const WCHAR *dir, ARGS *items, BOOL dirs_only)
{
    STR base;
    str_init(&base);
    str_adds(&base, dir);
    if (base.len && base.p[base.len - 1] != L'\\')
        str_addc(&base, L'\\');
    for (int i = 0; i < items->n && !run->stop; i++) {
        const WCHAR *item = items->v[i];
        if (has_wildcards(item)) {
            STR clean;
            str_init(&clean);
            str_adds(&clean, item);
            strip_quotes_inplace(clean.p);
            void **names;
            SIZE_T count;
            list_matches(base.p, clean.p, dirs_only, &names, &count);
            for (SIZE_T k = 0; k < count && !run->stop; k++) {
                STR full;
                str_init(&full);
                str_adds(&full, base.p);
                str_adds(&full, names[k]);
                for_body(run, str_get(&full));
                str_free(&full);
            }
            free_names(names, count);
            str_free(&clean);
        } else {
            STR full;
            str_init(&full);
            str_adds(&full, base.p);
            str_adds(&full, item);
            for_body(run, str_get(&full));
            str_free(&full);
        }
    }
    void **subdirs;
    SIZE_T count;
    list_matches(base.p, L"*", TRUE, &subdirs, &count);
    for (SIZE_T i = 0; i < count && !run->stop; i++) {
        STR sub;
        str_init(&sub);
        str_adds(&sub, base.p);
        str_adds(&sub, subdirs[i]);
        recurse_dir(run, str_get(&sub), items, dirs_only);
        str_free(&sub);
    }
    free_names(subdirs, count);
    str_free(&base);
}

static void for_recursive(FORRUN *run, const WCHAR *root, const WCHAR *set, BOOL dirs_only)
{
    STR start;
    str_init(&start);
    if (root && *root) {
        str_adds(&start, root);
        strip_quotes_inplace(start.p);
        WCHAR full[MAX_PATH * 2];
        if (GetFullPathNameW(start.p, NUM(full), full, nullptr)) {
            str_reset(&start);
            str_adds(&start, full);
        }
    } else {
        get_cwd(&start);
    }
    ARGS items;
    set_words(set, &items);
    DWORD attr = GetFileAttributesW(str_get(&start));
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        err_s(L"The system cannot find the path specified.\r\n");
        run->result = 1;
        args_free(&items);
        str_free(&start);
        return;
    }
    /* a root of a drive keeps its backslash, the others get none before the first name. */
    recurse_dir(run, start.p, &items, dirs_only);
    args_free(&items);
    str_free(&start);
}

/* ------------------------------------------------------------------ for /f */

typedef struct FOROPT {
    WCHAR eol;
    int skip;
    WCHAR *delims;
    WCHAR *tokens;
    BOOL usebackq;
} FOROPT;

static BOOL keyword_at(const WCHAR *p, const WCHAR *name)
{
    SIZE_T n = wcslen(name);
    return !_wcsnicmp(p, name, n);
}

static BOOL option_keyword_ahead(const WCHAR *p)
{
    return keyword_at(p, L"eol=") || keyword_at(p, L"skip=") || keyword_at(p, L"delims=") ||
           keyword_at(p, L"tokens=") || keyword_at(p, L"usebackq");
}

static BOOL parse_for_options(const WCHAR *text, FOROPT *o)
{
    o->eol = L';';
    o->skip = 0;
    o->delims = xdup(L" \t");
    o->tokens = xdup(L"1");
    o->usebackq = FALSE;
    STR clean;
    str_init(&clean);
    str_adds(&clean, text ? text : L"");
    if (clean.len >= 2 && clean.p[0] == L'"' && clean.p[clean.len - 1] == L'"') {
        memmove(clean.p, clean.p + 1, (clean.len - 1) * sizeof(WCHAR));
        clean.len -= 2;
        clean.p[clean.len] = 0;
    } else if (clean.len >= 2 && clean.p[0] == L'\'' && clean.p[clean.len - 1] == L'\'') {
        memmove(clean.p, clean.p + 1, (clean.len - 1) * sizeof(WCHAR));
        clean.len -= 2;
        clean.p[clean.len] = 0;
    }
    const WCHAR *p = str_get(&clean);
    while (*p) {
        if (is_space(*p)) {
            p++;
            continue;
        }
        if (keyword_at(p, L"eol=")) {
            p += 4;
            o->eol = *p;
            if (*p)
                p++;
        } else if (keyword_at(p, L"skip=")) {
            p += 5;
            o->skip = (int)parse_long(p, &p);
        } else if (keyword_at(p, L"usebackq")) {
            o->usebackq = TRUE;
            p += 8;
        } else if (keyword_at(p, L"delims=")) {
            p += 7;
            const WCHAR *e = p;
            while (*e) {
                if (*e == L' ' && option_keyword_ahead(e + 1))
                    break;
                e++;
            }
            xfree(o->delims);
            o->delims = xdupn(p, (SIZE_T)(e - p));
            p = e;
        } else if (keyword_at(p, L"tokens=")) {
            p += 7;
            const WCHAR *e = p;
            while (*e && !is_space(*e))
                e++;
            xfree(o->tokens);
            o->tokens = xdupn(p, (SIZE_T)(e - p));
            p = e;
        } else {
            err_fmt(L"Parameter format not correct - \"%.*ls\".\r\n", 1, p);
            str_free(&clean);
            return FALSE;
        }
    }
    str_free(&clean);
    return TRUE;
}

typedef struct TOKSPEC {
    int from[32], to[32];
    int count;
    BOOL star;
    int max_index;
} TOKSPEC;

static BOOL parse_tokens(const WCHAR *text, TOKSPEC *spec)
{
    memset(spec, 0, sizeof(*spec));
    const WCHAR *p = text;
    while (*p) {
        if (spec->count >= 31)
            return FALSE;
        if (*p == L'*') {
            spec->star = TRUE;
            p++;
            if (*p)
                return FALSE;
            break;
        }
        if (!is_digit(*p))
            return FALSE;
        int a = 0;
        while (is_digit(*p))
            a = a * 10 + (*p++ - L'0');
        int b = a;
        if (*p == L'-') {
            p++;
            if (!is_digit(*p))
                return FALSE;
            b = 0;
            while (is_digit(*p))
                b = b * 10 + (*p++ - L'0');
        }
        if (a < 1 || b < a || b > 128)
            return FALSE;
        spec->from[spec->count] = a;
        spec->to[spec->count] = b;
        spec->count++;
        if (b > spec->max_index)
            spec->max_index = b;
        if (*p == L',')
            p++;
        else if (*p && *p != L'*')
            return FALSE;
    }
    return spec->count > 0 || spec->star;
}

static BOOL is_delim(const WCHAR *delims, WCHAR c)
{
    return wcschr(delims, c) != nullptr && c != 0;
}

static void for_f_line(FORRUN *run, const FOROPT *o, const TOKSPEC *spec, const WCHAR *line)
{
    const WCHAR *p = line;
    while (*p && is_delim(o->delims, *p))
        p++;
    if (!*p)
        return;
    if (o->eol && *p == o->eol)
        return;
    /* split into tokens. */
    const WCHAR *starts[130];
    SIZE_T lens[130];
    int count = 0;
    const WCHAR *rest_after_last = nullptr;
    int wanted_max = spec->max_index;
    while (*p) {
        const WCHAR *s = p;
        while (*p && !is_delim(o->delims, *p))
            p++;
        if (count < 128) {
            starts[count] = s;
            lens[count] = (SIZE_T)(p - s);
            count++;
        }
        const WCHAR *after = p;
        while (*p && is_delim(o->delims, *p))
            p++;
        if (count == wanted_max)
            rest_after_last = p;
        (void)after;
        if (count >= 128)
            break;
    }
    if (!rest_after_last && count < wanted_max + 1)
        rest_after_last = line + wcslen(line);
    WCHAR var = run->node->for_var;
    int vi = 0;
    for (int i = 0; i < spec->count; i++) {
        for (int t = spec->from[i]; t <= spec->to[i]; t++, vi++) {
            WCHAR name = (WCHAR)(var + vi);
            if (t <= count) {
                WCHAR *value = xdupn(starts[t - 1], lens[t - 1]);
                forctx_set(run->ctx, name, value);
                xfree(value);
            } else {
                forctx_set(run->ctx, name, nullptr);
            }
        }
    }
    if (spec->star) {
        WCHAR name = (WCHAR)(var + vi);
        const WCHAR *rest = wanted_max < count ? starts[wanted_max] : nullptr;
        if (spec->count == 0)
            rest = starts[0];
        if (rest)
            forctx_set(run->ctx, name, rest);
        else
            forctx_set(run->ctx, name, nullptr);
    }
    /* the body runs with the variables set, the first one is set again by for_body. */
    NODE *body = run->node->a;
    (void)body;
    WCHAR first = var;
    BOOL defined = FALSE;
    const WCHAR *first_value = nullptr;
    for (int i = 0; i < run->ctx->count; i++)
        if (run->ctx->names[i] == first) {
            defined = run->ctx->defined[i];
            first_value = run->ctx->values[i];
        }
    (void)defined;
    WCHAR *copy = first_value ? xdup(first_value) : nullptr;
    for_body(run, copy);
    xfree(copy);
}

static void for_f_text(FORRUN *run, const FOROPT *o, const TOKSPEC *spec, const WCHAR *text, BOOL single_line)
{
    int skipped = 0;
    const WCHAR *p = text;
    while (*p && !run->stop) {
        const WCHAR *e = p;
        while (*e && *e != L'\n')
            e++;
        SIZE_T len = (SIZE_T)(e - p);
        if (len && p[len - 1] == L'\r')
            len--;
        if (skipped < o->skip) {
            skipped++;
        } else {
            WCHAR *line = xdupn(p, len);
            for_f_line(run, o, spec, line);
            xfree(line);
        }
        if (single_line)
            break;
        p = *e ? e + 1 : e;
    }
}

static BOOL read_whole_file(const WCHAR *path, STR *text)
{
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return FALSE;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(f, &size) || size.QuadPart > 0x7FFFFFF) {
        CloseHandle(f);
        return FALSE;
    }
    SIZE_T n = (SIZE_T)size.QuadPart;
    unsigned char *bytes = xalloc(n + 4);
    SIZE_T total = 0;
    DWORD got;
    while (total < n && ReadFile(f, bytes + total, (DWORD)(n - total), &got, nullptr) && got)
        total += got;
    CloseHandle(f);
    if (total >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        str_addn(text, (WCHAR *)(bytes + 2), (total - 2) / 2);
    } else {
        SIZE_T skip = (total >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF) ? 3 : 0;
        DWORD cp = GetConsoleCP();
        if (!cp)
            cp = GetOEMCP();
        if (skip)
            cp = CP_UTF8;
        int chars = MultiByteToWideChar(cp, 0, (char *)bytes + skip, (int)(total - skip), nullptr, 0);
        if (chars > 0) {
            WCHAR *w = xalloc(((SIZE_T)chars + 1) * sizeof(WCHAR));
            MultiByteToWideChar(cp, 0, (char *)bytes + skip, (int)(total - skip), w, chars);
            str_addn(text, w, (SIZE_T)chars);
            xfree(w);
        }
    }
    xfree(bytes);
    str_get(text);
    return TRUE;
}

/* carets of the set are escapes of the command line that runs. */
static void remove_carets(STR *s)
{
    SIZE_T d = 0;
    for (SIZE_T i = 0; i < s->len; i++) {
        if (s->p[i] == L'^' && i + 1 < s->len)
            i++;
        s->p[d++] = s->p[i];
    }
    s->len = d;
    s->p[d] = 0;
}

static void for_files(FORRUN *run, const WCHAR *set)
{
    FOROPT o;
    if (!parse_for_options(run->node->for_option, &o)) {
        run->result = 1;
        return;
    }
    TOKSPEC spec;
    if (!parse_tokens(o.tokens, &spec)) {
        msg_syntax();
        xfree(o.delims);
        xfree(o.tokens);
        run->result = 1;
        return;
    }
    STR s;
    str_init(&s);
    str_adds(&s, set);
    /* trim blanks around the set. */
    while (s.len && is_space(s.p[s.len - 1]))
        s.p[--s.len] = 0;
    const WCHAR *trim = skip_space(str_get(&s));
    WCHAR first = *trim;
    SIZE_T len = wcslen(trim);
    WCHAR last = len ? trim[len - 1] : 0;
    WCHAR cmd_quote = o.usebackq ? L'`' : L'\'';
    WCHAR lit_quote = o.usebackq ? L'\'' : L'"';
    if (len >= 2 && first == lit_quote && last == lit_quote) {
        WCHAR *inner = xdupn(trim + 1, len - 2);
        for_f_text(run, &o, &spec, inner, TRUE);
        xfree(inner);
    } else if (len >= 2 && first == cmd_quote && last == cmd_quote) {
        STR command;
        str_init(&command);
        str_addn(&command, trim + 1, len - 2);
        remove_carets(&command);
        STR output;
        str_init(&output);
        run_command_capture(str_get(&command), &output);
        for_f_text(run, &o, &spec, str_get(&output), FALSE);
        str_free(&output);
        str_free(&command);
    } else {
        ARGS files;
        if (o.usebackq && first == L'"') {
            files.v = xalloc(sizeof(WCHAR *));
            files.v[0] = xdup(trim);
            files.n = 1;
        } else {
            args_split(trim, &files, FALSE);
            /* a set with several files and spaces inside names is split on the blanks only. */
        }
        for (int i = 0; i < files.n && !run->stop; i++) {
            STR clean;
            str_init(&clean);
            str_adds(&clean, files.v[i]);
            strip_quotes_inplace(clean.p ? clean.p : (WCHAR *)L"");
            clean.len = wcslen(str_get(&clean));
            void **names = nullptr;
            SIZE_T count = 0;
            STR dir, pattern;
            str_init(&dir);
            str_init(&pattern);
            BOOL wild = has_wildcards(clean.p);
            if (wild) {
                path_split_pattern(clean.p, &dir, &pattern);
                list_matches(dir.p, pattern.p, FALSE, &names, &count);
            }
            SIZE_T total = wild ? count : 1;
            for (SIZE_T k = 0; k < total && !run->stop; k++) {
                STR path;
                str_init(&path);
                if (wild) {
                    str_adds(&path, dir.p);
                    str_adds(&path, names[k]);
                } else {
                    str_adds(&path, clean.p);
                }
                STR content;
                str_init(&content);
                if (read_whole_file(str_get(&path), &content))
                    for_f_text(run, &o, &spec, content.p, FALSE);
                else {
                    err_fmt(L"The system cannot find the file %ls.\r\n", path.p);
                    run->result = 1;
                }
                str_free(&content);
                str_free(&path);
            }
            free_names(names, count);
            str_free(&dir);
            str_free(&pattern);
            str_free(&clean);
        }
        args_free(&files);
    }
    str_free(&s);
    xfree(o.delims);
    xfree(o.tokens);
}

int exec_for(NODE *n)
{
    STR set;
    str_init(&set);
    {
        STR stage;
        str_init(&stage);
        expand_forvars(n->for_set, &stage);
        if (g.delayed && wcschr(str_get(&stage), L'!')) {
            if (!expand_delayed(stage.p, &set)) {
                str_free(&stage);
                str_free(&set);
                return 1;
            }
        } else {
            str_adds(&set, str_get(&stage));
        }
        str_free(&stage);
    }
    STR option;
    str_init(&option);
    if (n->for_option) {
        STR stage;
        str_init(&stage);
        expand_forvars(n->for_option, &stage);
        if (g.delayed && wcschr(str_get(&stage), L'!'))
            expand_delayed(stage.p, &option);
        else
            str_adds(&option, str_get(&stage));
        str_free(&stage);
    }
    FORRUN run = {};
    run.node = n;
    run.ctx = forctx_push();
    NODE saved = *n;
    WCHAR *replaced = n->for_option;
    if (n->for_option) {
        n->for_option = option.p;
    }
    if (n->for_flags & FOR_F)
        for_files(&run, str_get(&set));
    else if (n->for_flags & FOR_R)
        for_recursive(&run, n->for_option, str_get(&set), (n->for_flags & FOR_D) != 0);
    else if (n->for_flags & FOR_L)
        for_numbers(&run, str_get(&set));
    else
        for_plain(&run, str_get(&set), (n->for_flags & FOR_D) != 0);
    n->for_option = replaced;
    (void)saved;
    forctx_pop(run.ctx);
    str_free(&option);
    str_free(&set);
    return run.result;
}
