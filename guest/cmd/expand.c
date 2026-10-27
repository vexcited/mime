/* percent expansion of a line, delayed expansion, for variables and the %~ modifiers. */
#include "cmd.h"

#define MOD_F 0x001
#define MOD_D 0x002
#define MOD_P 0x004
#define MOD_N 0x008
#define MOD_X 0x010
#define MOD_S 0x020
#define MOD_A 0x100
#define MOD_T 0x200
#define MOD_Z 0x400
#define MOD_PATHS (MOD_F | MOD_D | MOD_P | MOD_N | MOD_X | MOD_S)
#define MOD_ATTRS (MOD_A | MOD_T | MOD_Z)
#define MOD_SEARCHED 0x8000

typedef const WCHAR *(*PARAM_LOOKUP)(void *context, WCHAR c, DWORD flags);

/* ------------------------------------------------------------------ %name:...% and !name:...! */

/* case insensitive substring search, a position or null. */
static const WCHAR *find_i(const WCHAR *hay, const WCHAR *needle, SIZE_T needle_len)
{
    for (; *hay; hay++)
        if (!_wcsnicmp(hay, needle, needle_len))
            return hay;
    return nullptr;
}

/*
 * start follows the opening % or !. returns TRUE with the value appended to out. consumed counts the characters to
 * skip: the name and its closing mark, or only the name and its colon for a variable that is not set.
 */
BOOL expand_var_spec(const WCHAR *start, WCHAR term, STR *out, SIZE_T *consumed, BOOL *defined)
{
    *consumed = 0;
    if (defined)
        *defined = FALSE;
    const WCHAR *p = start;
    for (;; p++) {
        if (!*p)
            return FALSE;
        if (*p == term || (g.ext && *p == L':' && p[1] != term))
            break;
    }
    if (p == start)
        return FALSE;
    WCHAR sep = *p;
    WCHAR *name = xdupn(start, (SIZE_T)(p - start));
    STR value;
    str_init(&value);
    BOOL found = env_get(name, &value);
    xfree(name);
    const WCHAR *q = p + 1;
    if (!g.ext || sep != L':' || !found) {
        *consumed = (SIZE_T)(q - start);
        if (defined)
            *defined = found;
        if (found)
            str_adds(out, str_get(&value));
        str_free(&value);
        return found;
    }
    const WCHAR *v = str_get(&value);
    LONG length = (LONG)value.len;
    BOOL ok = FALSE;
    if (*q == L'~') {
        const WCHAR *end;
        LONG offset = parse_long(q + 1, &end);
        q = end;
        if (offset < 0)
            offset += length;
        if (offset > length)
            offset = length;
        if (offset < 0)
            offset = 0;
        LONG count = length - offset;
        if (*q == L',') {
            LONG want = parse_long(q + 1, &end);
            q = end;
            if (want < 0)
                want += count;
            if (want < 0)
                want = 0;
            if (want < count)
                count = want;
        }
        if (*q == term) {
            str_addn(out, v + offset, (SIZE_T)count);
            *consumed = (SIZE_T)(q + 1 - start);
            ok = TRUE;
        }
    } else {
        BOOL star = FALSE;
        if (*q == L'*') {
            star = TRUE;
            q++;
        }
        const WCHAR *pattern = q;
        const WCHAR *eq = pattern;
        while (*eq && *eq != L'=')
            eq++;
        if (*eq == L'=') {
            if (eq == pattern) {
                STR shown;
                str_init(&shown);
                for (const WCHAR *s = eq; *s && *s != term; s++)
                    str_addc(&shown, *s);
                msg_unexpected(str_get(&shown));
                str_free(&shown);
                str_free(&value);
                return FALSE;
            }
            SIZE_T plen = (SIZE_T)(eq - pattern);
            const WCHAR *rep = eq + 1;
            const WCHAR *r = rep;
            while (*r && *r != term)
                r++;
            if (*r == term) {
                SIZE_T rlen = (SIZE_T)(r - rep);
                *consumed = (SIZE_T)(r + 1 - start);
                ok = TRUE;
                if (!*v) {
                    /* nothing to replace in. */
                } else if (star) {
                    const WCHAR *hit = find_i(v, pattern, plen);
                    if (hit) {
                        str_addn(out, rep, rlen);
                        str_adds(out, hit + plen);
                    } else {
                        str_adds(out, v);
                    }
                } else {
                    const WCHAR *c = v;
                    while (*c) {
                        if (!_wcsnicmp(c, pattern, plen)) {
                            str_addn(out, rep, rlen);
                            c += plen;
                        } else {
                            str_addc(out, *c++);
                        }
                    }
                }
            }
        }
    }
    if (ok && defined)
        *defined = TRUE;
    str_free(&value);
    return ok;
}

/* ------------------------------------------------------------------ the %~ modifiers */

static void attribute_text(DWORD attr, STR *out)
{
    static const struct {
        DWORD bit;
        WCHAR letter;
    } table[] = {{FILE_ATTRIBUTE_DIRECTORY, L'd'}, {FILE_ATTRIBUTE_READONLY, L'r'}, {FILE_ATTRIBUTE_ARCHIVE, L'a'},
                 {FILE_ATTRIBUTE_HIDDEN, L'h'},    {FILE_ATTRIBUTE_SYSTEM, L's'},   {FILE_ATTRIBUTE_COMPRESSED, L'c'},
                 {FILE_ATTRIBUTE_OFFLINE, L'o'},   {FILE_ATTRIBUTE_TEMPORARY, L't'}, {FILE_ATTRIBUTE_REPARSE_POINT, L'l'}};
    for (SIZE_T i = 0; i < NUM(table); i++)
        str_addc(out, (attr & table[i].bit) ? table[i].letter : L'-');
}

static void apply_modifiers(const WCHAR *argument, DWORD flags, const WCHAR *search_list, BOOL search_set, STR *out)
{
    if (!argument || !*argument)
        return;
    STR value;
    str_init(&value);
    str_adds(&value, argument);
    if (value.p[0] == L'"') {
        memmove(value.p, value.p + 1, value.len * sizeof(WCHAR));
        value.len--;
        if (value.len && value.p[value.len - 1] == L'"')
            value.p[--value.len] = 0;
    }
    if (!(flags & (MOD_PATHS | MOD_ATTRS | MOD_SEARCHED))) {
        str_adds(out, str_get(&value));
        str_free(&value);
        return;
    }
    WCHAR full[MAX_PATH * 4];
    full[0] = 0;
    if (flags & MOD_SEARCHED) {
        if (search_set && search_list) {
            WCHAR *file;
            if (!SearchPathW(search_list, str_get(&value), nullptr, NUM(full), full, &file))
                full[0] = 0;
        }
        if (!(flags & (MOD_PATHS | MOD_ATTRS)))
            flags |= MOD_F;
    } else if (!GetFullPathNameW(str_get(&value), NUM(full), full, nullptr)) {
        full[0] = 0;
    }
    str_free(&value);
    if (!full[0])
        return;
    if (flags & MOD_S) {
        WCHAR shortp[MAX_PATH * 4];
        DWORD n = GetShortPathNameW(full, shortp, NUM(shortp));
        if (n && n < NUM(shortp))
            wcscpy(full, shortp);
    }
    if (flags & MOD_ATTRS) {
        WIN32_FILE_ATTRIBUTE_DATA data;
        if (GetFileAttributesExW(full, GetFileExInfoStandard, &data)) {
            if (flags & MOD_A)
                attribute_text(data.dwFileAttributes, out);
            if (flags & MOD_T) {
                if (out->len)
                    str_addc(out, L' ');
                format_date_time(&data.ftLastWriteTime, out);
            }
            if (flags & MOD_Z) {
                if (out->len)
                    str_addc(out, L' ');
                ULONGLONG size = ((ULONGLONG)data.nFileSizeHigh << 32) | data.nFileSizeLow;
                WCHAR digits[32];
                _snwprintf(digits, NUM(digits), L"%I64u", size);
                str_adds(out, digits);
            }
        }
    }
    if (!(flags & MOD_PATHS) && (flags & MOD_ATTRS))
        return;
    if (out->len)
        str_addc(out, L' ');
    if ((flags & MOD_F) || (flags & 0x3E) == MOD_S) {
        str_adds(out, full);
        return;
    }
    const WCHAR *rest = full;
    const WCHAR *drive_end = full;
    if (full[0] && full[1] == L':') {
        drive_end = full + 2;
        rest = drive_end;
    }
    const WCHAR *file = wcsrchr(rest, L'\\');
    file = file ? file + 1 : rest;
    const WCHAR *dot = wcsrchr(file, L'.');
    SIZE_T name_len = dot ? (SIZE_T)(dot - file) : wcslen(file);
    if (flags & MOD_D)
        str_addn(out, full, (SIZE_T)(drive_end - full));
    if (flags & MOD_P)
        str_addn(out, rest, (SIZE_T)(file - rest));
    if (flags & MOD_N)
        str_addn(out, file, name_len);
    if ((flags & MOD_X) && dot)
        str_adds(out, dot);
}

/*
 * p follows "%~". on success the text is appended and used counts the characters after the percent sign.
 * -1 when the modifiers are not followed by a parameter, 0 on success.
 */
static int tilde_param(const WCHAR *p, const WCHAR *valid, PARAM_LOOKUP lookup, void *context, STR *out,
                       SIZE_T *used)
{
    const WCHAR *q = p;
    DWORD flags = 0;
    const WCHAR *last_q = p;
    DWORD last_flags = 0;
    while (*q && *q != L'$') {
        WCHAR lc = towlower(*q);
        if (!wcschr(L"fdpnxsatz", lc))
            break;
        if (wcschr(valid, *q)) {
            last_q = q;
            last_flags = flags;
        }
        switch (lc) {
        case L'a': flags |= MOD_A; break;
        case L'd': flags |= MOD_D; break;
        case L'f': flags |= MOD_F; break;
        case L'n': flags |= MOD_N; break;
        case L'p': flags |= MOD_P; break;
        case L's': flags |= MOD_S; break;
        case L't': flags |= MOD_T; break;
        case L'x': flags |= MOD_X; break;
        case L'z': flags |= MOD_Z; break;
        }
        q++;
    }
    WCHAR *list_name = nullptr;
    const WCHAR *search_list = nullptr;
    BOOL search_set = FALSE;
    STR search_value;
    str_init(&search_value);
    if (*q == L'$') {
        const WCHAR *colon = wcschr(q + 1, L':');
        if (!colon)
            return -1;
        list_name = xdupn(q + 1, (SIZE_T)(colon - q - 1));
        search_set = env_get(list_name, &search_value);
        xfree(list_name);
        search_list = search_value.p;
        q = colon + 1;
        flags |= MOD_SEARCHED;
    } else if (!*q || !wcschr(valid, *q)) {
        q = last_q;
        flags = last_flags;
    }
    if (!*q || !wcschr(valid, *q)) {
        str_free(&search_value);
        return -1;
    }
    const WCHAR *arg = lookup(context, *q, flags);
    apply_modifiers(arg, flags, search_list, search_set, out);
    str_free(&search_value);
    *used = (SIZE_T)(q - p) + 2;
    return 0;
}

/* ------------------------------------------------------------------ batch parameters */

static const WCHAR *batch_lookup(void *context, WCHAR c, DWORD flags)
{
    BATCH *b = context;
    if (c == L'0') {
        /* the modifiers of %0 work on the file itself, wherever the current directory is now. */
        return (flags & (MOD_PATHS | MOD_ATTRS | MOD_SEARCHED)) ? b->path : b->name;
    }
    return batch_arg(b, c - L'0');
}

static void expand_param_text(BATCH *b, int index, STR *out)
{
    const WCHAR *a = batch_arg(b, index);
    if (a)
        str_adds(out, a);
}

BOOL expand_percent(const WCHAR *in, SIZE_T n, STR *out)
{
    BATCH *b = g.batch;
    for (SIZE_T i = 0; i < n;) {
        WCHAR c = in[i];
        if (c != L'%') {
            str_addc(out, c);
            i++;
            continue;
        }
        const WCHAR *rest = in + i + 1;
        if (b) {
            if (rest[0] == L'%') {
                str_addc(out, L'%');
                i += 2;
                continue;
            }
            if (rest[0] == L'*' && g.ext) {
                if (b->args)
                    str_adds(out, b->args);
                i += 2;
                continue;
            }
            if (g.ext && rest[0] == L'~') {
                STR piece;
                str_init(&piece);
                SIZE_T used = 0;
                if (tilde_param(rest + 1, L"0123456789", batch_lookup, b, &piece, &used) == 0) {
                    /* %~0 style modifiers on the batch name use its full path when path flags are given. */
                    str_adds(out, str_get(&piece));
                    str_free(&piece);
                    i += 1 + used + 0;
                    continue;
                }
                str_free(&piece);
                err_fmt(L"The following usage of the path operator in batch-parameter\r\nsubstitution is invalid: %.*ls\r\n\r\nFor valid formats type CALL /? or FOR /?\r\n",
                        (int)(wcslen(in + i)), in + i);
                return FALSE;
            }
            if (is_digit(rest[0])) {
                expand_param_text(b, rest[0] - L'0', out);
                i += 2;
                continue;
            }
        }
        STR piece;
        str_init(&piece);
        SIZE_T consumed;
        BOOL defined;
        if (expand_var_spec(rest, L'%', &piece, &consumed, &defined)) {
            str_adds(out, str_get(&piece));
            str_free(&piece);
            i += 1 + consumed;
        } else {
            str_free(&piece);
            if (b) {
                i += 1 + consumed;
            } else {
                str_addc(out, L'%');
                i++;
            }
        }
        if (out->len > LINE_LIMIT + 1)
            break;
    }
    if (out->len > LINE_LIMIT) {
        err_s(L"The input line is too long.\r\n");
        return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ delayed expansion */

BOOL expand_delayed(const WCHAR *in, STR *out)
{
    BOOL batch = g.batch != nullptr;
    const WCHAR *p = in;
    while (*p) {
        WCHAR c = *p++;
        if (c == L'^') {
            if (!*p)
                break;
            str_addc(out, *p++);
            continue;
        }
        if (c != L'!') {
            str_addc(out, c);
            continue;
        }
        STR piece;
        str_init(&piece);
        SIZE_T consumed;
        BOOL defined;
        if (expand_var_spec(p, L'!', &piece, &consumed, &defined)) {
            str_adds(out, str_get(&piece));
            p += consumed;
        } else if (batch) {
            p += consumed;
        } else {
            str_addc(out, L'!');
        }
        str_free(&piece);
        if (out->len > LINE_LIMIT + 1)
            break;
    }
    if (out->len > LINE_LIMIT) {
        err_s(L"The input line is too long.\r\n");
        return FALSE;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ for variables */

FORCTX *forctx_push(void)
{
    FORCTX *ctx = xalloc(sizeof(*ctx));
    ctx->up = g.forctx;
    g.forctx = ctx;
    return ctx;
}

void forctx_pop(FORCTX *ctx)
{
    g.forctx = ctx->up;
    forctx_clear(ctx);
    xfree(ctx);
}

void forctx_clear(FORCTX *ctx)
{
    for (int i = 0; i < ctx->count; i++) {
        xfree(ctx->values[i]);
        ctx->values[i] = nullptr;
        ctx->defined[i] = FALSE;
    }
}

void forctx_set(FORCTX *ctx, WCHAR name, const WCHAR *value)
{
    int i;
    for (i = 0; i < ctx->count; i++)
        if (ctx->names[i] == name)
            break;
    if (i == ctx->count) {
        if (i == (int)NUM(ctx->names))
            return;
        ctx->count++;
        ctx->names[i] = name;
        ctx->values[i] = nullptr;
    }
    xfree(ctx->values[i]);
    ctx->values[i] = value ? xdup(value) : nullptr;
    ctx->defined[i] = value != nullptr;
}

static const WCHAR *forvar_value(WCHAR c, BOOL *defined)
{
    for (FORCTX *ctx = g.forctx; ctx; ctx = ctx->up)
        for (int i = 0; i < ctx->count; i++)
            if (ctx->names[i] == c) {
                *defined = ctx->defined[i];
                return ctx->values[i];
            }
    *defined = FALSE;
    return nullptr;
}

static const WCHAR *for_lookup(void *context, WCHAR c, DWORD flags)
{
    BOOL defined;
    return forvar_value(c, &defined);
}

static BOOL is_forvar_char(WCHAR c)
{
    for (FORCTX *ctx = g.forctx; ctx; ctx = ctx->up)
        for (int i = 0; i < ctx->count; i++)
            if (ctx->names[i] == c)
                return TRUE;
    return FALSE;
}

void expand_forvars(const WCHAR *in, STR *out)
{
    if (!g.forctx) {
        str_adds(out, in);
        return;
    }
    WCHAR valid[130];
    int nvalid = 0;
    for (FORCTX *ctx = g.forctx; ctx; ctx = ctx->up)
        for (int i = 0; i < ctx->count && nvalid < 128; i++)
            valid[nvalid++] = ctx->names[i];
    valid[nvalid] = 0;
    for (const WCHAR *p = in; *p;) {
        if (*p != L'%') {
            str_addc(out, *p++);
            continue;
        }
        if (p[1] == L'~' && g.ext) {
            STR piece;
            str_init(&piece);
            SIZE_T used;
            if (nvalid && tilde_param(p + 2, valid, for_lookup, nullptr, &piece, &used) == 0) {
                BOOL defined;
                WCHAR var = p[used];
                forvar_value(var, &defined);
                if (defined) {
                    str_adds(out, str_get(&piece));
                    str_free(&piece);
                    p += 1 + used;
                    continue;
                }
            }
            str_free(&piece);
            str_addc(out, *p++);
            continue;
        }
        BOOL defined;
        if (p[1] && is_forvar_char(p[1])) {
            const WCHAR *v = forvar_value(p[1], &defined);
            if (defined) {
                str_adds(out, v);
                p += 2;
                continue;
            }
        }
        str_addc(out, *p++);
    }
}
