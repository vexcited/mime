/* batch files: a context per file or called label, the lines read as the file is run, goto, shift and setlocal. */
#include "cmd.h"

#define READ_CHUNK 8192

typedef struct LINEREADER {
    HANDLE file;
    LONGLONG next_offset; /* of the first byte not consumed yet */
    unsigned char *buf;
    DWORD have, at;
    LONGLONG file_size;
} LINEREADER;

static BOOL reader_open(LINEREADER *r, const WCHAR *path, LONGLONG offset)
{
    r->file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (r->file == INVALID_HANDLE_VALUE)
        return FALSE;
    LARGE_INTEGER size;
    r->file_size = GetFileSizeEx(r->file, &size) ? size.QuadPart : 0;
    LARGE_INTEGER to;
    to.QuadPart = offset;
    SetFilePointerEx(r->file, to, nullptr, FILE_BEGIN);
    r->next_offset = offset;
    r->buf = xalloc(READ_CHUNK);
    r->have = r->at = 0;
    return TRUE;
}

static void reader_close(LINEREADER *r)
{
    if (r->file != INVALID_HANDLE_VALUE)
        CloseHandle(r->file);
    xfree(r->buf);
    r->file = INVALID_HANDLE_VALUE;
    r->buf = nullptr;
}

static int reader_byte(LINEREADER *r)
{
    if (r->at == r->have) {
        DWORD got = 0;
        if (!ReadFile(r->file, r->buf, READ_CHUNK, &got, nullptr) || !got)
            return -1;
        r->have = got;
        r->at = 0;
    }
    return r->buf[r->at++];
}

static DWORD input_cp(void)
{
    DWORD cp = GetConsoleCP();
    return cp ? cp : GetOEMCP();
}

/*
 * the next line, decoded, without its line break. FALSE at the end of the file. a Ctrl+Z ends the file. the
 * offset after the line is in next_offset.
 */
static BOOL reader_line(LINEREADER *r, STR *line, BOOL *too_long)
{
    SIZE_T cap = 256, n = 0;
    unsigned char *bytes = xalloc(cap);
    BOOL any = FALSE;
    LONGLONG line_start = r->next_offset;
    *too_long = FALSE;
    for (;;) {
        int b = reader_byte(r);
        if (b < 0)
            break;
        r->next_offset++;
        any = TRUE;
        if (b == 0x1A) {
            r->next_offset = r->file_size;
            if (!n)
                any = FALSE;
            break;
        }
        if (b == '\n')
            break;
        if (b == '\r')
            continue;
        if (n + 1 >= cap) {
            cap *= 2;
            bytes = xrealloc(bytes, cap);
        }
        bytes[n++] = (unsigned char)b;
    }
    if (!any) {
        xfree(bytes);
        return FALSE;
    }
    SIZE_T skip = 0;
    if (line_start == 0 && n >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
        skip = 3;
    if (n > skip) {
        DWORD cp = input_cp();
        int chars = MultiByteToWideChar(cp, 0, (char *)bytes + skip, (int)(n - skip), nullptr, 0);
        if (chars > 0) {
            WCHAR *w = xalloc(((SIZE_T)chars + 1) * sizeof(WCHAR));
            MultiByteToWideChar(cp, 0, (char *)bytes + skip, (int)(n - skip), w, chars);
            str_addn(line, w, (SIZE_T)chars);
            xfree(w);
        }
    }
    str_get(line);
    xfree(bytes);
    if (line->len > LINE_LIMIT)
        *too_long = TRUE;
    return TRUE;
}

/* ------------------------------------------------------------------ contexts */

static void tokenize_args(BATCH *b, const WCHAR *args)
{
    ARGS a;
    args_split(args, &a, TRUE);
    b->tok = a.v;
    b->ntok = a.n;
    b->shifted = 0;
}

BATCH *batch_begin(const WCHAR *path, const WCHAR *name, const WCHAR *args)
{
    BATCH *b = xalloc(sizeof(*b));
    b->path = xdup(path);
    b->name = xdup(name);
    const WCHAR *start = skip_space(args);
    STR trimmed;
    str_init(&trimmed);
    str_adds(&trimmed, start);
    while (trimmed.len && (trimmed.p[trimmed.len - 1] == L' ' || trimmed.p[trimmed.len - 1] == L'\t'))
        trimmed.p[--trimmed.len] = 0;
    b->args = str_take(&trimmed);
    tokenize_args(b, b->args);
    b->parent = g.batch;
    g.batch = b;
    return b;
}

void batch_unwind_locals(BATCH *b)
{
    while (b->nlocals > 0) {
        LOCALENV *snap = b->locals[--b->nlocals];
        env_restore(snap);
        env_free_snapshot(snap);
        b->locals[b->nlocals] = nullptr;
    }
}

void batch_end(BATCH *b)
{
    batch_unwind_locals(b);
    if (g.batch == b)
        g.batch = b->parent;
    for (int i = 0; i < b->ntok; i++)
        xfree(b->tok[i]);
    xfree(b->tok);
    xfree(b->path);
    xfree(b->name);
    xfree(b->args);
    xfree(b);
}

const WCHAR *batch_arg(BATCH *b, int n)
{
    if (n == 0)
        return b->name;
    int i = n - 1 + b->shifted;
    return i < b->ntok ? b->tok[i] : nullptr;
}

/* shift or shift /n: argument n and the ones after it move down by one. */
void batch_shift(BATCH *b, int from)
{
    int total = b->ntok - b->shifted;
    if (from == 0) {
        /* %0 takes %1. */
        xfree(b->name);
        if (total > 0) {
            b->name = xdup(b->tok[b->shifted]);
            xfree(b->tok[b->shifted]);
            b->tok[b->shifted] = nullptr;
            b->shifted++;
        } else {
            b->name = xdup(L"");
        }
        return;
    }
    int index = from - 1;
    if (index >= total)
        return;
    xfree(b->tok[b->shifted + index]);
    for (int i = index; i + 1 < total; i++)
        b->tok[b->shifted + i] = b->tok[b->shifted + i + 1];
    b->tok[b->shifted + total - 1] = nullptr;
    b->ntok--;
}

BOOL batch_read_line(BATCH *b, STR *line)
{
    LINEREADER r;
    if (!reader_open(&r, b->path, b->offset)) {
        err_s(L"The batch file cannot be found.\r\n");
        return FALSE;
    }
    BOOL too_long;
    BOOL ok = reader_line(&r, line, &too_long);
    b->offset = r.next_offset;
    reader_close(&r);
    if (ok && too_long) {
        err_s(L"The input line is too long.\r\n");
        g.abort_all = TRUE;
        return FALSE;
    }
    return ok;
}

/* ------------------------------------------------------------------ goto */

static BOOL label_char_ends(WCHAR c)
{
    return c == 0 || wcschr(L"+:\t\r\n ", c) != nullptr || wcschr(L"&<|>", c) != nullptr;
}

/* the label a line defines, if it is a label line. */
static BOOL line_label(const WCHAR *line, STR *label)
{
    const WCHAR *p = skip_space(line);
    if (*p != L':')
        return FALSE;
    p++;
    p = skip_space(p);
    int count = 0;
    while (!label_char_ends(*p) && count < 127) {
        WCHAR c = *p;
        if (c == L'^' && p[1]) {
            c = p[1];
            p++;
        }
        str_addc(label, c);
        p++;
        count++;
    }
    str_get(label);
    return TRUE;
}

/* goto: finds the label after the current position, then from the top, and moves the next line to it. */
BOOL batch_goto(BATCH *b, const WCHAR *text)
{
    const WCHAR *p = skip_space(text);
    if (!*p) {
        err_s(L"No batch label specified to GOTO command.\r\n");
        return FALSE;
    }
    if (g.ext && starts_with_i(p, L":eof") && (!p[4] || is_space(p[4]))) {
        LINEREADER probe;
        LONGLONG end = 0;
        if (reader_open(&probe, b->path, 0)) {
            end = probe.file_size;
            reader_close(&probe);
        }
        b->offset = end;
        g.goto_pending = TRUE;
        return TRUE;
    }
    if (*p == L':' || *p == L'+')
        p++;
    STR want;
    str_init(&want);
    for (int i = 0; i < 127; i++) {
        WCHAR c = *p;
        if (!c || wcschr(L"=,;", c) || wcschr(L"+:\t\r\n ", c))
            break;
        str_addc(&want, c);
        p++;
    }
    str_get(&want);
    LONGLONG start = b->offset;
    BOOL found = FALSE;
    for (int pass = 0; pass < 2 && !found; pass++) {
        LINEREADER r;
        LONGLONG from = pass == 0 ? start : 0;
        if (!reader_open(&r, b->path, from))
            break;
        for (;;) {
            if (pass == 1 && r.next_offset >= start)
                break;
            if (g.ctrlc && ask_terminate_batch()) {
                reader_close(&r);
                str_free(&want);
                return FALSE;
            }
            STR line;
            str_init(&line);
            BOOL too_long;
            if (!reader_line(&r, &line, &too_long)) {
                str_free(&line);
                break;
            }
            STR label;
            str_init(&label);
            if (line_label(str_get(&line), &label) && !_wcsicmp(str_get(&label), str_get(&want))) {
                b->offset = r.next_offset;
                found = TRUE;
            }
            str_free(&label);
            str_free(&line);
            if (found)
                break;
        }
        reader_close(&r);
    }
    if (!found) {
        err_fmt(L"The system cannot find the batch label specified - %ls\r\n", str_get(&want));
        str_free(&want);
        return FALSE;
    }
    str_free(&want);
    g.goto_pending = TRUE;
    return TRUE;
}

/* ------------------------------------------------------------------ running */

typedef struct BATCH_SOURCE {
    LINESRC base;
    BATCH *batch;
} BATCH_SOURCE;

static BOOL batch_source_next(LINESRC *src, STR *line)
{
    BATCH_SOURCE *s = (BATCH_SOURCE *)src;
    STR raw;
    str_init(&raw);
    if (!batch_read_line(s->batch, &raw)) {
        str_free(&raw);
        return FALSE;
    }
    BOOL ok = expand_percent(raw.p ? raw.p : L"", raw.len, line);
    str_free(&raw);
    if (!ok)
        g.abort_all = TRUE;
    return ok;
}

/* runs the statements of a batch file from its current position to the end. returns the last result. */
int run_batch_loop(BATCH *b)
{
    int result = 0;
    BATCH_SOURCE source = {};
    source.base.next = batch_source_next;
    source.batch = b;
    for (;;) {
        if (g.ctrlc && ask_terminate_batch())
            break;
        if (g.abort_all || g.batch != b)
            break;
        g.goto_pending = FALSE;
        STR line;
        str_init(&line);
        if (!batch_source_next(&source.base, &line)) {
            str_free(&line);
            break;
        }
        int error = 0;
        NODE *node = parse_line(str_get(&line), &source.base, FALSE, &error);
        if (error) {
            if (g.echo && !g.quiet) {
                print_prompt();
                out_n(line.p, line.len);
                out_s(L"\r\n");
            }
            str_free(&line);
            g.abort_all = TRUE;
            if (!g.single)
                g.abort_all = TRUE;
            result = 255;
            break;
        }
        str_free(&line);
        if (!node)
            continue;
        if (g.echo && !g.quiet && node->kind != N_QUIET && node->kind != N_EMPTY && node->src) {
            print_prompt();
            out_s(node->src);
            out_s(L"\r\n");
        }
        result = exec_node(node, TRUE);
        node_free(node);
    }
    g.goto_pending = FALSE;
    return result;
}

/*
 * runs a batch file: a call makes a new context, a plain invocation from inside a batch file replaces the running one
 * and never returns to it.
 */
int run_batch_file(const WCHAR *path, const WCHAR *name, const WCHAR *args, BOOL is_call)
{
    if (!is_call && g.batch) {
        BATCH *b = g.batch;
        for (int i = 0; i < b->ntok; i++)
            xfree(b->tok[i]);
        xfree(b->tok);
        xfree(b->path);
        xfree(b->name);
        xfree(b->args);
        BATCH *fresh = batch_begin(path, name, args);
        /* batch_begin pushed a context, move its fields into the running one and drop it. */
        g.batch = fresh->parent;
        b->path = fresh->path;
        b->name = fresh->name;
        b->args = fresh->args;
        b->tok = fresh->tok;
        b->ntok = fresh->ntok;
        b->shifted = 0;
        b->offset = 0;
        xfree(fresh);
        g.goto_pending = TRUE;
        return 0;
    }
    BATCH *b = batch_begin(path, name, args);
    BOOL saved_echo = g.echo;
    if (!b->parent)
        b->echo_saved = saved_echo;
    g.nest++;
    int result = run_batch_loop(b);
    g.nest--;
    BOOL outermost = b->parent == nullptr;
    batch_end(b);
    if (outermost)
        g.echo = saved_echo;
    return result;
}
