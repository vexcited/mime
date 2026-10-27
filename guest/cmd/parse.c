/*
 * the parser: a logical line, with the lines of a parenthesized block and the lines joined by a caret, becomes a tree.
 *
 * from the loosest binding to the tightest: &  then ||  then &&  then |  then redirections around a command, a
 * parenthesized block, if, for, rem or a plain command. the body of if and for, and the right side of every
 * operator, is a whole statement of the same grammar.
 */
#include "cmd.h"

typedef struct PARSER {
    STR buf;
    SIZE_T pos;
    LINESRC *more;
    BOOL colon_is_token;
    int depth;          /* open parentheses. */
    BOOL block_direct;  /* the innermost construct is a parenthesized block, so a line break separates statements. */
    BOOL error;
} PARSER;

static NODE *parse_s0(PARSER *p);

static WCHAR peek(PARSER *p)
{
    return p->pos < p->buf.len ? p->buf.p[p->pos] : 0;
}

static WCHAR peek_at(PARSER *p, SIZE_T off)
{
    return p->pos + off < p->buf.len ? p->buf.p[p->pos + off] : 0;
}

/* appends the next line to the text, after a line break. */
static BOOL fetch_more(PARSER *p)
{
    if (!p->more || !p->more->next)
        return FALSE;
    STR line;
    str_init(&line);
    BOOL ok = p->more->next(p->more, &line);
    if (ok) {
        str_addc(&p->buf, L'\n');
        str_adds(&p->buf, str_get(&line));
    }
    str_free(&line);
    return ok;
}

static void skip_blank(PARSER *p)
{
    while (p->pos < p->buf.len && (p->buf.p[p->pos] == L' ' || p->buf.p[p->pos] == L'\t' || p->buf.p[p->pos] == L'\r'))
        p->pos++;
}

/* blanks and the separators ,;= that the lexer of the real interpreter skips before a command. */
static void skip_lead(PARSER *p)
{
    while (p->pos < p->buf.len) {
        WCHAR c = p->buf.p[p->pos];
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L',' || c == L';' || c == L'=')
            p->pos++;
        else
            break;
    }
}

/* line breaks inside a block, reading more lines when the text ends. */
static void skip_breaks(PARSER *p)
{
    for (;;) {
        skip_blank(p);
        if (peek(p) == L'\n') {
            p->pos++;
            continue;
        }
        if (p->pos >= p->buf.len && p->depth > 0 && fetch_more(p))
            continue;
        break;
    }
}

static void fail_unexpected(PARSER *p, const WCHAR *token)
{
    if (!p->error) {
        p->error = TRUE;
        if (!token || !token[0] || token[0] == L'\n')
            msg_syntax();
        else
            msg_unexpected(token);
    }
}

static void fail_here(PARSER *p)
{
    STR tok;
    str_init(&tok);
    SIZE_T i = p->pos;
    WCHAR c = i < p->buf.len ? p->buf.p[i] : 0;
    if (c == L'&' || c == L'|') {
        str_addc(&tok, c);
        if (i + 1 < p->buf.len && p->buf.p[i + 1] == c)
            str_addc(&tok, c);
    } else if (c == L')' || c == L'<' || c == L'>' || c == L'(') {
        str_addc(&tok, c);
    } else {
        while (i < p->buf.len && !is_space(p->buf.p[i]) && p->buf.p[i] != L'\n')
            str_addc(&tok, p->buf.p[i++]);
    }
    fail_unexpected(p, str_get(&tok));
    str_free(&tok);
}

static NODE *new_node(NODE_KIND kind)
{
    NODE *n = xalloc(sizeof(*n));
    n->kind = kind;
    return n;
}

static void set_src(PARSER *p, NODE *n, SIZE_T from, SIZE_T to)
{
    if (!n || n->src)
        return;
    while (to > from && (p->buf.p[to - 1] == L' ' || p->buf.p[to - 1] == L'\t' || p->buf.p[to - 1] == L'\r'))
        to--;
    n->src = xdupn(p->buf.p + from, to - from);
}

/* ------------------------------------------------------------------ words and redirections */

static BOOL word_stop(PARSER *p, WCHAR c, BOOL equals_stops)
{
    if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == L',' || c == L';')
        return TRUE;
    if (c == L'=' && equals_stops)
        return TRUE;
    if (c == L'&' || c == L'|' || c == L'<' || c == L'>')
        return TRUE;
    if (c == L')' && p->depth > 0)
        return TRUE;
    return FALSE;
}

/* a word of a condition or a redirection target: quotes kept, carets removed. */
static void read_word(PARSER *p, STR *word, BOOL equals_stops)
{
    BOOL quoted = FALSE;
    for (;;) {
        WCHAR c = peek(p);
        if (!c || c == L'\n')
            break;
        if (!quoted) {
            if (c == L'^') {
                p->pos++;
                if (p->pos >= p->buf.len && fetch_more(p)) {
                }
                if (peek(p) == L'\n') {
                    p->pos++;
                    if (p->pos >= p->buf.len)
                        fetch_more(p);
                }
                c = peek(p);
                if (c) {
                    str_addc(word, c);
                    p->pos++;
                }
                continue;
            }
            if (word_stop(p, c, equals_stops))
                break;
        }
        if (c == L'"')
            quoted = !quoted;
        str_addc(word, c);
        p->pos++;
    }
    str_get(word);
}

static BOOL word_is(const STR *w, const WCHAR *name)
{
    return w->p && !_wcsicmp(w->p, name);
}

static REDIR *new_redir(int fd, REDIR_KIND kind)
{
    REDIR *r = xalloc(sizeof(*r));
    r->fd = fd;
    r->kind = kind;
    return r;
}

/* at a < or >. fd is the handle number a digit before it gave, or -1. */
static REDIR *parse_one_redirect(PARSER *p, int fd)
{
    WCHAR op = peek(p);
    p->pos++;
    REDIR_KIND kind = op == L'<' ? R_IN : R_OUT;
    if (op == L'>' && peek(p) == L'>') {
        kind = R_APPEND;
        p->pos++;
    } else if (op == L'<' && peek(p) == L'<') {
        fail_unexpected(p, L"<<");
        return nullptr;
    }
    if (fd < 0)
        fd = op == L'<' ? 0 : 1;
    if (peek(p) == L'&') {
        p->pos++;
        while (peek(p) == L' ' || peek(p) == L'\t' || peek(p) == L'=' || peek(p) == L',' || peek(p) == L';')
            p->pos++;
        WCHAR d = peek(p);
        if (!is_digit(d)) {
            fail_here(p);
            return nullptr;
        }
        p->pos++;
        REDIR *r = new_redir(fd, R_DUP);
        r->dup_fd = d - L'0';
        return r;
    }
    skip_blank(p);
    STR target;
    str_init(&target);
    read_word(p, &target, FALSE);
    if (!target.len) {
        str_free(&target);
        fail_here(p);
        return nullptr;
    }
    REDIR *r = new_redir(fd, kind);
    r->target = str_take(&target);
    return r;
}

static void redir_append(REDIR **list, REDIR *r)
{
    while (*list)
        list = &(*list)->next;
    *list = r;
}

/* leading redirections such as > file command. */
static void parse_redirs(PARSER *p, REDIR **list)
{
    for (;;) {
        skip_blank(p);
        WCHAR c = peek(p);
        int fd = -1;
        SIZE_T save = p->pos;
        if (is_digit(c) && (peek_at(p, 1) == L'<' || peek_at(p, 1) == L'>')) {
            fd = c - L'0';
            p->pos++;
            c = peek(p);
        }
        if (c != L'<' && c != L'>') {
            p->pos = save;
            return;
        }
        REDIR *r = parse_one_redirect(p, fd);
        if (!r)
            return;
        redir_append(list, r);
    }
}

/* ------------------------------------------------------------------ statements */

static BOOL at_keyword(PARSER *p, const WCHAR *name)
{
    SIZE_T n = wcslen(name);
    if (p->pos + n > p->buf.len || _wcsnicmp(p->buf.p + p->pos, name, n))
        return FALSE;
    WCHAR after = p->pos + n < p->buf.len ? p->buf.p[p->pos + n] : 0;
    if (after == 0 || after == L'\n')
        return TRUE;
    if (after == L' ' || after == L'\t' || after == L'\r' || after == L',' || after == L';' || after == L'=' ||
        after == L'(' || after == L'&' || after == L'|' || after == L'<' || after == L'>' || after == L'"')
        return TRUE;
    return FALSE;
}

/* a command with its arguments up to the next operator. */
static NODE *parse_simple(PARSER *p)
{
    STR text;
    str_init(&text);
    REDIR *redirs = nullptr;
    BOOL quoted = FALSE;
    for (;;) {
        WCHAR c = peek(p);
        if (!c || c == L'\n')
            break;
        if (!quoted) {
            if (c == L'^') {
                p->pos++;
                if (p->pos >= p->buf.len)
                    fetch_more(p);
                if (peek(p) == L'\n') {
                    p->pos++;
                    if (p->pos >= p->buf.len)
                        fetch_more(p);
                }
                c = peek(p);
                if (c) {
                    str_addc(&text, c);
                    p->pos++;
                }
                continue;
            }
            if (c == L'&' || c == L'|')
                break;
            if (c == L')' && p->depth > 0)
                break;
            if (c == L'<' || c == L'>') {
                int fd = -1;
                if (text.len && is_digit(text.p[text.len - 1])) {
                    WCHAR before = text.len > 1 ? text.p[text.len - 2] : 0;
                    if (!before || is_space(before) || wcschr(L"()|&=,;\"", before)) {
                        fd = text.p[text.len - 1] - L'0';
                        text.p[--text.len] = 0;
                    }
                }
                REDIR *r = parse_one_redirect(p, fd);
                if (!r) {
                    str_free(&text);
                    return nullptr;
                }
                redir_append(&redirs, r);
                continue;
            }
        }
        if (c == L'"')
            quoted = !quoted;
        str_addc(&text, c);
        p->pos++;
    }
    NODE *n = new_node(N_CMD);
    n->text = str_take(&text);
    n->redirs = redirs;
    return n;
}

static NODE *parse_rem(PARSER *p, SIZE_T start)
{
    while (peek(p) && peek(p) != L'\n')
        p->pos++;
    NODE *n = new_node(N_EMPTY);
    set_src(p, n, start, p->pos);
    return n;
}

static BOOL read_keyword_word(PARSER *p, STR *w)
{
    skip_lead(p);
    read_word(p, w, TRUE);
    return w->len > 0;
}

static NODE *parse_if(PARSER *p)
{
    NODE *n = new_node(N_IF);
    STR w;
    str_init(&w);
    if (!read_keyword_word(p, &w)) {
        fail_here(p);
        goto fail;
    }
    if (g.ext && word_is(&w, L"/i")) {
        n->icase = TRUE;
        str_reset(&w);
        if (!read_keyword_word(p, &w)) {
            fail_here(p);
            goto fail;
        }
    }
    if (word_is(&w, L"not")) {
        n->negate = TRUE;
        str_reset(&w);
        if (!read_keyword_word(p, &w)) {
            fail_here(p);
            goto fail;
        }
    }
    {
        IF_KIND kind = IF_STRING;
        BOOL keyword = TRUE;
        if (word_is(&w, L"errorlevel"))
            kind = IF_ERRORLEVEL;
        else if (word_is(&w, L"exist"))
            kind = IF_EXIST;
        else if (g.ext && word_is(&w, L"defined"))
            kind = IF_DEFINED;
        else if (g.ext && word_is(&w, L"cmdextversion"))
            kind = IF_CMDEXT;
        else
            keyword = FALSE;
        if (keyword) {
            STR arg;
            str_init(&arg);
            skip_lead(p);
            read_word(p, &arg, FALSE);
            if (!arg.len) {
                str_free(&arg);
                fail_here(p);
                goto fail;
            }
            n->if_kind = kind;
            n->lhs = str_take(&arg);
        } else {
            if (!w.len || (w.len >= 2 && w.p[0] == L'=' && w.p[1] == L'=')) {
                fail_unexpected(p, L"==");
                goto fail;
            }
            n->lhs = str_take(&w);
            str_init(&w);
            skip_blank(p);
            if (peek(p) == L'=' && peek_at(p, 1) == L'=') {
                p->pos += 2;
                n->if_kind = IF_STRING;
            } else {
                STR op;
                str_init(&op);
                read_word(p, &op, TRUE);
                static const struct {
                    const WCHAR *name;
                    CMP_OP op;
                } ops[] = {{L"equ", CMP_EQU}, {L"neq", CMP_NEQ}, {L"lss", CMP_LSS},
                           {L"leq", CMP_LEQ}, {L"gtr", CMP_GTR}, {L"geq", CMP_GEQ}};
                BOOL found = FALSE;
                if (g.ext)
                    for (SIZE_T i = 0; i < NUM(ops); i++)
                        if (word_is(&op, ops[i].name)) {
                            n->op = ops[i].op;
                            n->if_kind = IF_COMPARE;
                            found = TRUE;
                        }
                if (!found) {
                    fail_unexpected(p, op.len ? str_get(&op) : L"");
                    str_free(&op);
                    goto fail;
                }
                str_free(&op);
            }
            STR rhs;
            str_init(&rhs);
            skip_lead(p);
            read_word(p, &rhs, FALSE);
            if (!rhs.len) {
                str_free(&rhs);
                fail_here(p);
                goto fail;
            }
            n->rhs = str_take(&rhs);
        }
    }
    str_free(&w);
    {
        BOOL saved = p->block_direct;
        p->block_direct = FALSE;
        skip_lead(p);
        if (!peek(p) || peek(p) == L'\n') {
            p->block_direct = saved;
            msg_syntax();
            p->error = TRUE;
            goto fail2;
        }
        n->a = parse_s0(p);
        if (!n->a || p->error) {
            p->block_direct = saved;
            goto fail2;
        }
        SIZE_T save = p->pos;
        skip_blank(p);
        if (at_keyword(p, L"else")) {
            p->pos += 4;
            skip_lead(p);
            n->b = parse_s0(p);
            if (!n->b || p->error) {
                if (!p->error)
                    fail_here(p);
                p->block_direct = saved;
                goto fail2;
            }
        } else {
            p->pos = save;
        }
        p->block_direct = saved;
    }
    return n;
fail:
    str_free(&w);
fail2:
    node_free(n);
    return nullptr;
}

static NODE *parse_for(PARSER *p)
{
    NODE *n = new_node(N_FOR);
    STR w;
    str_init(&w);
    for (;;) {
        str_reset(&w);
        if (!read_keyword_word(p, &w)) {
            fail_here(p);
            goto fail;
        }
        if (g.ext && word_is(&w, L"/l")) {
            n->for_flags |= FOR_L;
        } else if (g.ext && word_is(&w, L"/d")) {
            n->for_flags |= FOR_D;
        } else if (g.ext && (word_is(&w, L"/f") || word_is(&w, L"/r"))) {
            BOOL is_f = w.p[1] == L'f' || w.p[1] == L'F';
            n->for_flags |= is_f ? FOR_F : FOR_R;
            str_reset(&w);
            if (!read_keyword_word(p, &w)) {
                fail_here(p);
                goto fail;
            }
            if (w.p[0] != L'%' && w.p[0] != L'/') {
                n->for_option = xdup(w.p);
            } else {
                /* the word is already the variable or the next switch. */
                p->pos -= w.len;
            }
        } else {
            break;
        }
    }
    if (w.p[0] != L'%' || w.len != 2 || wcschr(L"=,;", w.p[1]) || is_space(w.p[1])) {
        fail_unexpected(p, w.p);
        goto fail;
    }
    n->for_var = w.p[1];
    str_reset(&w);
    if (!read_keyword_word(p, &w) || _wcsicmp(w.p, L"in")) {
        fail_unexpected(p, w.len ? w.p : L"");
        goto fail;
    }
    skip_lead(p);
    if (peek(p) != L'(') {
        fail_here(p);
        goto fail;
    }
    p->pos++;
    {
        STR set;
        str_init(&set);
        BOOL quoted = FALSE;
        BOOL closed = FALSE;
        while (p->pos <= p->buf.len) {
            WCHAR c = peek(p);
            if (!c) {
                if (p->depth > 0 && fetch_more(p))
                    continue;
                break;
            }
            if (!quoted && c == L')') {
                p->pos++;
                closed = TRUE;
                break;
            }
            if (!quoted && c == L'^') {
                p->pos++;
                /* the caret stays: the set is parsed again when a command is run from it. */
                str_addc(&set, L'^');
                c = peek(p);
                if (c && c != L'\n') {
                    str_addc(&set, c);
                    p->pos++;
                }
                continue;
            }
            if (c == L'"')
                quoted = !quoted;
            if (c == L'\n')
                c = L' ';
            str_addc(&set, c);
            p->pos++;
        }
        if (!closed) {
            str_free(&set);
            fail_here(p);
            goto fail;
        }
        n->for_set = str_take(&set);
    }
    str_reset(&w);
    if (!read_keyword_word(p, &w) || _wcsicmp(w.p, L"do")) {
        fail_unexpected(p, w.len ? w.p : L"");
        goto fail;
    }
    str_free(&w);
    {
        BOOL saved = p->block_direct;
        p->block_direct = FALSE;
        skip_lead(p);
        if (!peek(p) || peek(p) == L'\n') {
            p->block_direct = saved;
            msg_syntax();
            p->error = TRUE;
            goto fail2;
        }
        n->a = parse_s0(p);
        p->block_direct = saved;
        if (!n->a || p->error)
            goto fail2;
    }
    return n;
fail:
    str_free(&w);
fail2:
    node_free(n);
    return nullptr;
}

static NODE *parse_s5(PARSER *p)
{
    skip_lead(p);
    SIZE_T start = p->pos;
    WCHAR c = peek(p);
    NODE *n;
    if (c == L'@') {
        p->pos++;
        n = new_node(N_QUIET);
        n->a = parse_s0(p);
        if (!n->a) {
            node_free(n);
            return nullptr;
        }
        set_src(p, n, start, p->pos);
        return n;
    }
    if (c == L'(') {
        p->pos++;
        p->depth++;
        BOOL saved = p->block_direct;
        p->block_direct = TRUE;
        skip_breaks(p);
        NODE *inner;
        if (peek(p) == L')')
            inner = new_node(N_EMPTY);
        else
            inner = parse_s0(p);
        p->block_direct = saved;
        if (!inner || p->error) {
            p->depth--;
            node_free(inner);
            return nullptr;
        }
        skip_breaks(p);
        if (peek(p) == L')') {
            p->pos++;
        } else if (p->pos < p->buf.len) {
            fail_here(p);
            p->depth--;
            node_free(inner);
            return nullptr;
        }
        p->depth--;
        n = new_node(N_BLOCK);
        n->a = inner;
        set_src(p, n, start, p->pos);
        return n;
    }
    if (!c || c == L'\n') {
        fail_unexpected(p, L"");
        return nullptr;
    }
    if (c == L')' || c == L'&' || c == L'|') {
        fail_here(p);
        return nullptr;
    }
    if (at_keyword(p, L"for")) {
        p->pos += 3;
        n = parse_for(p);
    } else if (at_keyword(p, L"if")) {
        p->pos += 2;
        n = parse_if(p);
    } else if (at_keyword(p, L"rem") && (peek_at(p, 3) != L'.')) {
        p->pos += 3;
        return parse_rem(p, start);
    } else {
        n = parse_simple(p);
    }
    if (n)
        set_src(p, n, start, p->pos);
    return n;
}

static NODE *parse_s4(PARSER *p)
{
    REDIR *lead = nullptr;
    parse_redirs(p, &lead);
    if (p->error) {
        return nullptr;
    }
    NODE *n = parse_s5(p);
    if (!n) {
        while (lead) {
            REDIR *next = lead->next;
            xfree(lead->target);
            xfree(lead);
            lead = next;
        }
        return nullptr;
    }
    if (lead) {
        if (n->kind == N_IF || n->kind == N_FOR) {
            fail_unexpected(p, L"");
            node_free(n);
            return nullptr;
        }
        REDIR *tail = lead;
        while (tail->next)
            tail = tail->next;
        tail->next = n->redirs;
        n->redirs = lead;
    }
    /* redirections after a block. */
    if (n->kind == N_BLOCK || n->kind == N_QUIET) {
        REDIR *after = nullptr;
        parse_redirs(p, &after);
        if (after) {
            REDIR **list = &n->redirs;
            while (*list)
                list = &(*list)->next;
            *list = after;
        }
    }
    return n;
}

static NODE *binary(PARSER *p, NODE_KIND kind, NODE *left, NODE *right, SIZE_T start)
{
    NODE *n = new_node(kind);
    n->a = left;
    n->b = right;
    set_src(p, n, start, p->pos);
    return n;
}

static NODE *parse_s3(PARSER *p)
{
    skip_lead(p);
    SIZE_T start = p->pos;
    NODE *left = parse_s4(p);
    if (!left)
        return nullptr;
    set_src(p, left, start, p->pos);
    skip_blank(p);
    if (peek(p) == L'|' && peek_at(p, 1) != L'|') {
        p->pos++;
        if (p->depth > 0)
            skip_breaks(p);
        skip_lead(p);
        if (!peek(p) || peek(p) == L'\n') {
            fail_unexpected(p, L"");
            node_free(left);
            return nullptr;
        }
        NODE *right = parse_s3(p);
        if (!right) {
            node_free(left);
            return nullptr;
        }
        return binary(p, N_PIPE, left, right, start);
    }
    return left;
}

static NODE *parse_s2(PARSER *p)
{
    skip_lead(p);
    SIZE_T start = p->pos;
    NODE *left = parse_s3(p);
    if (!left)
        return nullptr;
    skip_blank(p);
    if (peek(p) == L'&' && peek_at(p, 1) == L'&') {
        p->pos += 2;
        if (p->depth > 0)
            skip_breaks(p);
        skip_lead(p);
        if (!peek(p) || peek(p) == L'\n') {
            fail_unexpected(p, L"");
            node_free(left);
            return nullptr;
        }
        NODE *right = parse_s2(p);
        if (!right) {
            node_free(left);
            return nullptr;
        }
        return binary(p, N_AND, left, right, start);
    }
    return left;
}

static NODE *parse_s1(PARSER *p)
{
    skip_lead(p);
    SIZE_T start = p->pos;
    NODE *left = parse_s2(p);
    if (!left)
        return nullptr;
    skip_blank(p);
    if (peek(p) == L'|' && peek_at(p, 1) == L'|') {
        p->pos += 2;
        if (p->depth > 0)
            skip_breaks(p);
        skip_lead(p);
        if (!peek(p) || peek(p) == L'\n') {
            fail_unexpected(p, L"");
            node_free(left);
            return nullptr;
        }
        NODE *right = parse_s1(p);
        if (!right) {
            node_free(left);
            return nullptr;
        }
        return binary(p, N_OR, left, right, start);
    }
    return left;
}

static void skip_to_line_end(PARSER *p)
{
    while (peek(p) && peek(p) != L'\n')
        p->pos++;
    if (peek(p) == L'\n')
        p->pos++;
}

static NODE *parse_s0(PARSER *p)
{
    skip_lead(p);
    SIZE_T start = p->pos;
    /* a label or a :: comment is a line of its own and does nothing. */
    if (peek(p) == L':' && !p->colon_is_token) {
        skip_to_line_end(p);
        if (p->depth > 0 && p->block_direct) {
            skip_breaks(p);
            if (peek(p) == L')' || !peek(p))
                return new_node(N_EMPTY);
            return parse_s0(p);
        }
        NODE *n = new_node(N_EMPTY);
        n->src = xdupn(p->buf.p + start, p->pos - start);
        return n;
    }
    NODE *left = parse_s1(p);
    if (!left)
        return nullptr;
    skip_blank(p);
    WCHAR c = peek(p);
    if (c == L'&' && peek_at(p, 1) != L'&') {
        p->pos++;
        if (p->depth > 0 && p->block_direct)
            skip_breaks(p);
        skip_lead(p);
        NODE *right;
        WCHAR d = peek(p);
        if (!d || d == L'\n' || (d == L')' && p->depth > 0))
            right = new_node(N_EMPTY);
        else
            right = parse_s0(p);
        if (!right) {
            node_free(left);
            return nullptr;
        }
        return binary(p, N_SEQ, left, right, start);
    }
    if ((c == L'\n' || !c) && p->depth > 0 && p->block_direct) {
        skip_breaks(p);
        WCHAR d = peek(p);
        if (!d || d == L')')
            return left;
        NODE *right = parse_s0(p);
        if (!right) {
            node_free(left);
            return nullptr;
        }
        return binary(p, N_SEQ, left, right, start);
    }
    return left;
}

/* a logical line. the error is set when the text is not a statement, with the message already printed. */
NODE *parse_line(const WCHAR *line, LINESRC *more, BOOL colon_is_token, int *error)
{
    PARSER p = {};
    str_init(&p.buf);
    str_adds(&p.buf, line);
    str_get(&p.buf);
    p.more = more;
    p.colon_is_token = colon_is_token;
    NODE *n = parse_s0(&p);
    if (n && !p.error) {
        skip_blank(&p);
        WCHAR c = peek(&p);
        if (c && c != L'\n') {
            fail_here(&p);
        }
    }
    if (p.error) {
        node_free(n);
        n = nullptr;
    }
    if (error)
        *error = p.error;
    if (n && !n->src)
        n->src = xdupn(p.buf.p, p.buf.len);
    str_free(&p.buf);
    return n;
}

void node_free(NODE *n)
{
    if (!n)
        return;
    for (REDIR *r = n->redirs; r;) {
        REDIR *next = r->next;
        xfree(r->target);
        xfree(r);
        r = next;
    }
    xfree(n->src);
    xfree(n->text);
    xfree(n->lhs);
    xfree(n->rhs);
    xfree(n->for_option);
    xfree(n->for_set);
    node_free(n->a);
    node_free(n->b);
    node_free(n->c);
    xfree(n);
}
