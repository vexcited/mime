/*
 * the lexer, with conditional compilation, and the parser of JScript into a syntax tree.
 */
#include "js.h"

enum { PARSE_PROGRAM, PARSE_EVAL, PARSE_FUNCTION };

enum {
    K_BREAK = 256, K_CASE, K_CATCH, K_CONTINUE, K_DEFAULT, K_DELETE, K_DO, K_ELSE, K_FINALLY, K_FOR, K_FUNCTION, K_IF,
    K_IN, K_INSTANCEOF, K_NEW, K_RETURN, K_SWITCH, K_THIS, K_THROW, K_TRY, K_TYPEOF, K_VAR, K_VOID, K_WHILE, K_WITH,
    K_TRUE, K_FALSE, K_NULL, K_RESERVED, K_COUNT
};

static const struct {
    PCWSTR Text;
    int Id;
} g_keywords[] = {
    {L"break", K_BREAK}, {L"case", K_CASE}, {L"catch", K_CATCH}, {L"continue", K_CONTINUE}, {L"default", K_DEFAULT},
    {L"delete", K_DELETE}, {L"do", K_DO}, {L"else", K_ELSE}, {L"finally", K_FINALLY}, {L"for", K_FOR},
    {L"function", K_FUNCTION}, {L"if", K_IF}, {L"in", K_IN}, {L"instanceof", K_INSTANCEOF}, {L"new", K_NEW},
    {L"return", K_RETURN}, {L"switch", K_SWITCH}, {L"this", K_THIS}, {L"throw", K_THROW}, {L"try", K_TRY},
    {L"typeof", K_TYPEOF}, {L"var", K_VAR}, {L"void", K_VOID}, {L"while", K_WHILE}, {L"with", K_WITH},
    {L"true", K_TRUE}, {L"false", K_FALSE}, {L"null", K_NULL}, {L"class", K_RESERVED}, {L"const", K_RESERVED},
    {L"debugger", K_RESERVED}, {L"enum", K_RESERVED}, {L"export", K_RESERVED}, {L"extends", K_RESERVED},
    {L"import", K_RESERVED}, {L"super", K_RESERVED},
};

/* ------------------------------------------------------------------ memory of a unit */

typedef struct ArenaBlock {
    struct ArenaBlock *Next;
    SIZE_T Used, Size;
} ArenaBlock;

struct Arena {
    ArenaBlock *Blocks;
};

void *arena_alloc(struct Arena *a, SIZE_T size)
{
    size = (size + 15) & ~(SIZE_T)15;
    ArenaBlock *b = a->Blocks;
    if (!b || b->Used + size > b->Size) {
        SIZE_T cap = size > 32768 ? size : 32768;
        b = js_alloc(sizeof(ArenaBlock) + cap);
        b->Size = cap;
        b->Next = a->Blocks;
        a->Blocks = b;
    }
    void *p = (BYTE *)(b + 1) + b->Used;
    b->Used += size;
    return p;
}

void unit_free(Unit *u)
{
    if (!u)
        return;
    ArenaBlock *b = u->arena ? u->arena->Blocks : nullptr;
    while (b) {
        ArenaBlock *n = b->Next;
        js_free(b);
        b = n;
    }
    js_free(u->arena);
    js_free(u->text);
    js_free(u);
}

/* ------------------------------------------------------------------ the parser state */

typedef struct Label {
    Str *Name;
    BOOL IsLoop;
    struct Label *Next;
} Label;

typedef struct CcFrame {
    BYTE Active, Taken, ParentActive, SeenElse;
} CcFrame;

typedef struct CcVar {
    Str *Name;
    double Value;
    BOOL Defined;
} CcVar;

typedef struct Parser {
    Engine *e;
    Unit *u;
    const WCHAR *s;
    ULONG n, pos, line, line_start, first_line;
    /* the token. */
    int tok, op;
    Str *str;
    double num;
    ULONG tstart, tend, tline, tcol;
    BOOL nl_before;
    ULONG prev_end;
    /* errors. */
    BOOL failed;
    int code;
    ULONG eline, ecol;
    /* where the parse is. */
    FuncNode *fn;
    int loops, switches, funcdepth;
    Label *labels;
    int mode;
    /* conditional compilation. */
    BOOL cc_on;
    CcFrame cc[32];
    int cc_depth;
    CcVar ccvars[32];
    int ccvar_count;
} Parser;

static void perr(Parser *p, int code)
{
    if (p->failed)
        return;
    p->failed = TRUE;
    p->code = code;
    p->eline = p->tline;
    p->ecol = p->tcol;
    p->tok = TK_EOF;
    p->op = 0;
}

static void perr_at(Parser *p, int code, ULONG line, ULONG col)
{
    if (p->failed)
        return;
    perr(p, code);
    p->eline = line;
    p->ecol = col;
}

static Node *new_node(Parser *p, int kind)
{
    Node *n = arena_alloc(p->u->arena, sizeof(Node));
    n->kind = (BYTE)kind;
    n->line = p->tline;
    n->col = p->tcol;
    n->start = p->tstart;
    return n;
}

/* ------------------------------------------------------------------ conditional compilation values */

static BOOL ident_start(WCHAR c)
{
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'$' || c == L'_' || (c > 127 && iswalpha(c));
}

static BOOL ident_part(WCHAR c)
{
    return ident_start(c) || (c >= L'0' && c <= L'9') || (c > 127 && iswalnum(c));
}

static BOOL cc_var_get(Parser *p, const WCHAR *name, SIZE_T len, double *value)
{
    static const struct {
        PCWSTR Name;
        double Value;
    } builtin[] = {
        {L"_jscript", 1}, {L"_jscript_build", 16384}, {L"_jscript_version", 10.8},
    };
    for (UINT i = 0; i < sizeof(builtin) / sizeof(builtin[0]); i++)
        if (wcslen(builtin[i].Name) == len && !wcsncmp(builtin[i].Name, name, len)) {
            *value = builtin[i].Value;
            return TRUE;
        }
    for (int i = 0; i < p->ccvar_count; i++)
        if (p->ccvars[i].Name->len == len && !wcsncmp(p->ccvars[i].Name->s, name, len)) {
            *value = p->ccvars[i].Value;
            return p->ccvars[i].Defined;
        }
    return FALSE;
}

static void cc_skip_space(Parser *p)
{
    while (p->pos < p->n && (p->s[p->pos] == ' ' || p->s[p->pos] == '\t' || p->s[p->pos] == '\r' || p->s[p->pos] == '\n')) {
        if (p->s[p->pos] == '\n') {
            p->line++;
            p->line_start = p->pos + 1;
        }
        p->pos++;
    }
}

static double cc_expr(Parser *p, int level);

static double cc_primary(Parser *p)
{
    cc_skip_space(p);
    if (p->pos >= p->n) {
        perr(p, 1031);
        return 0;
    }
    WCHAR c = p->s[p->pos];
    if (c == '(') {
        p->pos++;
        double v = cc_expr(p, 0);
        cc_skip_space(p);
        if (p->pos < p->n && p->s[p->pos] == ')')
            p->pos++;
        else
            perr(p, 1006);
        return v;
    }
    if (c == '!') {
        p->pos++;
        return cc_primary(p) == 0 ? 1 : 0;
    }
    if (c == '-') {
        p->pos++;
        return -cc_primary(p);
    }
    if (c == '~') {
        p->pos++;
        return (double)~d_to_int32(cc_primary(p));
    }
    if (c == '+') {
        p->pos++;
        return cc_primary(p);
    }
    if (c >= '0' && c <= '9') {
        char buf[64];
        int k = 0;
        while (p->pos < p->n && k < 60 && ((p->s[p->pos] >= '0' && p->s[p->pos] <= '9') || p->s[p->pos] == '.'))
            buf[k++] = (char)p->s[p->pos++];
        buf[k] = 0;
        return strtod(buf, nullptr);
    }
    if (c == '@') {
        p->pos++;
        ULONG st = p->pos;
        while (p->pos < p->n && ident_part(p->s[p->pos]))
            p->pos++;
        double v;
        if (cc_var_get(p, p->s + st, p->pos - st, &v))
            return v;
        return js_nan;
    }
    if (ident_start(c)) {
        ULONG st = p->pos;
        while (p->pos < p->n && ident_part(p->s[p->pos]))
            p->pos++;
        if (p->pos - st == 4 && !wcsncmp(p->s + st, L"true", 4))
            return 1;
        if (p->pos - st == 5 && !wcsncmp(p->s + st, L"false", 5))
            return 0;
        return js_nan;
    }
    perr(p, 1031);
    return 0;
}

static int cc_binop(Parser *p, int *prec)
{
    cc_skip_space(p);
    if (p->pos >= p->n)
        return 0;
    WCHAR c = p->s[p->pos], d = p->pos + 1 < p->n ? p->s[p->pos + 1] : 0;
    *prec = 0;
    switch (c) {
    case '|': return d == '|' ? (*prec = 1, 'o') : (*prec = 3, '|');
    case '&': return d == '&' ? (*prec = 2, 'a') : (*prec = 5, '&');
    case '^': *prec = 4; return '^';
    case '=': if (d == '=') { *prec = 6; return 'e'; } return 0;
    case '!': if (d == '=') { *prec = 6; return 'n'; } return 0;
    case '<': return d == '=' ? (*prec = 7, 'l') : d == '<' ? (*prec = 8, 'L') : (*prec = 7, '<');
    case '>': return d == '=' ? (*prec = 7, 'g') : d == '>' ? (*prec = 8, 'R') : (*prec = 7, '>');
    case '+': *prec = 9; return '+';
    case '-': *prec = 9; return '-';
    case '*': *prec = 10; return '*';
    case '/': *prec = 10; return '/';
    case '%': *prec = 10; return '%';
    }
    return 0;
}

static double cc_expr(Parser *p, int level)
{
    double l = cc_primary(p);
    for (;;) {
        int prec, op = cc_binop(p, &prec);
        if (!op || prec <= level)
            return l;
        int width = (op == 'o' || op == 'a' || op == 'e' || op == 'n' || op == 'l' || op == 'g' || op == 'L' || op == 'R') ? 2 : 1;
        p->pos += width;
        double r = cc_expr(p, prec);
        switch (op) {
        case 'o': l = (l != 0 || r != 0); break;
        case 'a': l = (l != 0 && r != 0); break;
        case '|': l = (double)(d_to_int32(l) | d_to_int32(r)); break;
        case '^': l = (double)(d_to_int32(l) ^ d_to_int32(r)); break;
        case '&': l = (double)(d_to_int32(l) & d_to_int32(r)); break;
        case 'e': l = l == r; break;
        case 'n': l = l != r; break;
        case '<': l = l < r; break;
        case '>': l = l > r; break;
        case 'l': l = l <= r; break;
        case 'g': l = l >= r; break;
        case 'L': l = (double)(d_to_int32(l) << (d_to_int32(r) & 31)); break;
        case 'R': l = (double)(d_to_int32(l) >> (d_to_int32(r) & 31)); break;
        case '+': l = l + r; break;
        case '-': l = l - r; break;
        case '*': l = l * r; break;
        case '/': l = l / r; break;
        case '%': l = fmod(l, r); break;
        }
    }
}

/* ------------------------------------------------------------------ the lexer */

static BOOL cc_active(Parser *p)
{
    return p->cc_depth == 0 || p->cc[p->cc_depth - 1].Active;
}

static void skip_to_line_end(Parser *p)
{
    while (p->pos < p->n && p->s[p->pos] != '\n' && p->s[p->pos] != '\r' && p->s[p->pos] != 0x2028 && p->s[p->pos] != 0x2029)
        p->pos++;
}

static void newline_at(Parser *p)
{
    if (p->s[p->pos] == '\r' && p->pos + 1 < p->n && p->s[p->pos + 1] == '\n')
        p->pos++;
    p->pos++;
    p->line++;
    p->line_start = p->pos;
}

static BOOL is_line_term(WCHAR c)
{
    return c == '\n' || c == '\r' || c == 0x2028 || c == 0x2029;
}

static int hex_digit(WCHAR c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static BOOL name_is(Parser *p, const WCHAR *name, SIZE_T len, const WCHAR *what)
{
    return wcslen(what) == len && !wcsncmp(name, what, len);
}

/* handles @if and the rest at the position of an @, returns TRUE when the text was a directive and is consumed. */
static BOOL cc_directive(Parser *p)
{
    ULONG at = p->pos;
    ULONG st = at + 1;
    ULONG e2 = st;
    while (e2 < p->n && ident_part(p->s[e2]))
        e2++;
    const WCHAR *nm = p->s + st;
    SIZE_T len = e2 - st;
    if (name_is(p, nm, len, L"cc_on")) {
        p->cc_on = TRUE;
        p->pos = e2;
        return TRUE;
    }
    if (name_is(p, nm, len, L"if")) {
        p->pos = e2;
        cc_skip_space(p);
        BOOL parent = cc_active(p);
        double v = 0;
        if (p->pos < p->n && p->s[p->pos] == '(') {
            p->pos++;
            v = cc_expr(p, 0);
            cc_skip_space(p);
            if (p->pos < p->n && p->s[p->pos] == ')')
                p->pos++;
            else
                perr(p, 1006);
        } else
            perr(p, 1005);
        if (p->cc_depth >= 31) {
            perr(p, 1002);
            return TRUE;
        }
        CcFrame *f = &p->cc[p->cc_depth++];
        f->ParentActive = parent;
        f->Taken = v != 0 && !is_nan(v);
        f->Active = parent && f->Taken;
        f->SeenElse = 0;
        return TRUE;
    }
    if (name_is(p, nm, len, L"elif")) {
        p->pos = e2;
        if (!p->cc_depth || p->cc[p->cc_depth - 1].SeenElse) {
            perr(p, 1002);
            return TRUE;
        }
        CcFrame *f = &p->cc[p->cc_depth - 1];
        cc_skip_space(p);
        double v = 0;
        if (p->pos < p->n && p->s[p->pos] == '(') {
            p->pos++;
            v = cc_expr(p, 0);
            cc_skip_space(p);
            if (p->pos < p->n && p->s[p->pos] == ')')
                p->pos++;
            else
                perr(p, 1006);
        } else
            perr(p, 1005);
        if (f->Taken)
            f->Active = 0;
        else {
            f->Taken = v != 0 && !is_nan(v);
            f->Active = f->ParentActive && f->Taken;
        }
        return TRUE;
    }
    if (name_is(p, nm, len, L"else")) {
        p->pos = e2;
        if (!p->cc_depth || p->cc[p->cc_depth - 1].SeenElse) {
            perr(p, 1002);
            return TRUE;
        }
        CcFrame *f = &p->cc[p->cc_depth - 1];
        f->SeenElse = 1;
        f->Active = f->ParentActive && !f->Taken;
        f->Taken = 1;
        return TRUE;
    }
    if (name_is(p, nm, len, L"end")) {
        p->pos = e2;
        if (!p->cc_depth) {
            perr(p, 1002);
            return TRUE;
        }
        p->cc_depth--;
        return TRUE;
    }
    if (name_is(p, nm, len, L"set")) {
        p->pos = e2;
        cc_skip_space(p);
        if (p->pos >= p->n || p->s[p->pos] != '@') {
            perr(p, 1032);
            return TRUE;
        }
        p->pos++;
        ULONG vs = p->pos;
        while (p->pos < p->n && ident_part(p->s[p->pos]))
            p->pos++;
        SIZE_T vl = p->pos - vs;
        cc_skip_space(p);
        if (p->pos >= p->n || p->s[p->pos] != '=') {
            perr(p, 1011);
            return TRUE;
        }
        p->pos++;
        double v = cc_expr(p, 0);
        if (cc_active(p)) {
            int idx = -1;
            for (int i = 0; i < p->ccvar_count; i++)
                if (p->ccvars[i].Name->len == vl && !wcsncmp(p->ccvars[i].Name->s, p->s + vs, vl))
                    idx = i;
            if (idx < 0 && p->ccvar_count < 32) {
                idx = p->ccvar_count++;
                p->ccvars[idx].Name = str_intern(p->e, p->s + vs, vl);
            }
            if (idx >= 0) {
                p->ccvars[idx].Value = v;
                p->ccvars[idx].Defined = TRUE;
            }
        }
        return TRUE;
    }
    return FALSE;
}

static void lex(Parser *p);

static void lex_number(Parser *p)
{
    ULONG st = p->pos;
    const WCHAR *s = p->s;
    double v = 0;
    if (s[p->pos] == '0' && p->pos + 1 < p->n && (s[p->pos + 1] == 'x' || s[p->pos + 1] == 'X')) {
        p->pos += 2;
        if (p->pos >= p->n || hex_digit(s[p->pos]) < 0) {
            perr_at(p, 1023, p->line, p->pos - p->line_start);
            return;
        }
        while (p->pos < p->n && hex_digit(s[p->pos]) >= 0)
            v = v * 16 + hex_digit(s[p->pos++]);
    } else if (s[p->pos] == '0' && p->pos + 1 < p->n && s[p->pos + 1] >= '0' && s[p->pos + 1] <= '9') {
        ULONG q = p->pos;
        BOOL octal = TRUE;
        while (q < p->n && s[q] >= '0' && s[q] <= '9') {
            if (s[q] > '7')
                octal = FALSE;
            q++;
        }
        if (octal) {
            for (ULONG k = p->pos; k < q; k++)
                v = v * 8 + (s[k] - '0');
            p->pos = q;
        } else
            goto decimal;
    } else {
    decimal:;
        char buf[400];
        int k = 0;
        ULONG q = p->pos;
        while (q < p->n && s[q] >= '0' && s[q] <= '9' && k < 390)
            buf[k++] = (char)s[q++];
        if (q < p->n && s[q] == '.') {
            buf[k++] = '.';
            q++;
            while (q < p->n && s[q] >= '0' && s[q] <= '9' && k < 390)
                buf[k++] = (char)s[q++];
        }
        if (q < p->n && (s[q] == 'e' || s[q] == 'E')) {
            ULONG r = q + 1;
            WCHAR sign = 0;
            if (r < p->n && (s[r] == '+' || s[r] == '-'))
                sign = s[r++];
            if (r < p->n && s[r] >= '0' && s[r] <= '9') {
                buf[k++] = 'e';
                if (sign)
                    buf[k++] = (char)sign;
                while (r < p->n && s[r] >= '0' && s[r] <= '9' && k < 395)
                    buf[k++] = (char)s[r++];
                q = r;
            }
        }
        buf[k] = 0;
        v = strtod(buf, nullptr);
        p->pos = q;
    }
    (void)st;
    p->tok = TK_NUM;
    p->num = v;
}

static void lex_string(Parser *p, WCHAR quote)
{
    const WCHAR *s = p->s;
    WCHAR *buf = nullptr;
    SIZE_T cap = 0, len = 0;
#define PUT(ch)                                                                                                        \
    do {                                                                                                               \
        if (len + 1 >= cap) {                                                                                          \
            cap = cap ? cap * 2 : 64;                                                                                  \
            buf = js_realloc(buf, cap * sizeof(WCHAR));                                                                \
        }                                                                                                              \
        buf[len++] = (WCHAR)(ch);                                                                                      \
    } while (0)
    p->pos++;
    for (;;) {
        if (p->pos >= p->n || s[p->pos] == '\n' || s[p->pos] == '\r') {
            js_free(buf);
            perr_at(p, 1015, p->line, p->pos - p->line_start);
            return;
        }
        WCHAR c = s[p->pos++];
        if (c == quote)
            break;
        if (c != '\\') {
            PUT(c);
            continue;
        }
        if (p->pos >= p->n) {
            js_free(buf);
            perr_at(p, 1015, p->line, p->pos - p->line_start);
            return;
        }
        c = s[p->pos++];
        switch (c) {
        case 'n': PUT('\n'); break;
        case 't': PUT('\t'); break;
        case 'r': PUT('\r'); break;
        case 'b': PUT('\b'); break;
        case 'f': PUT('\f'); break;
        case 'v': PUT('\v'); break;
        case '\r':
            if (p->pos < p->n && s[p->pos] == '\n')
                p->pos++;
            p->line++;
            p->line_start = p->pos;
            break;
        case '\n':
        case 0x2028:
        case 0x2029:
            p->line++;
            p->line_start = p->pos;
            break;
        case 'x': {
            int a = p->pos < p->n ? hex_digit(s[p->pos]) : -1;
            int b = p->pos + 1 < p->n ? hex_digit(s[p->pos + 1]) : -1;
            if (a < 0 || b < 0) {
                js_free(buf);
                perr_at(p, 1023, p->line, p->pos + (a < 0 ? 0 : 1) - p->line_start);
                return;
            }
            PUT(a * 16 + b);
            p->pos += 2;
            break;
        }
        case 'u': {
            int v = 0;
            for (int i = 0; i < 4; i++) {
                int d = p->pos + i < p->n ? hex_digit(s[p->pos + i]) : -1;
                if (d < 0) {
                    js_free(buf);
                    perr_at(p, 1023, p->line, p->pos + i - p->line_start);
                    return;
                }
                v = v * 16 + d;
            }
            p->pos += 4;
            PUT(v);
            break;
        }
        default:
            if (c >= '0' && c <= '7') {
                int v = c - '0';
                int maxd = c <= '3' ? 2 : 1;
                while (maxd-- && p->pos < p->n && s[p->pos] >= '0' && s[p->pos] <= '7')
                    v = v * 8 + (s[p->pos++] - '0');
                PUT(v);
            } else
                PUT(c);
        }
    }
    p->tok = TK_STR;
    p->str = str_new(p->e, buf ? buf : L"", len);
    js_free(buf);
#undef PUT
}

/* a token is read; whitespace, comments and the directives of conditional compilation are consumed before it. */
static void lex(Parser *p)
{
    const WCHAR *s = p->s;
    p->prev_end = p->tend;
    p->nl_before = FALSE;
    for (;;) {
        /* white space and comments. */
        for (;;) {
            if (p->pos >= p->n)
                break;
            WCHAR c = s[p->pos];
            if (is_line_term(c)) {
                p->nl_before = TRUE;
                newline_at(p);
            } else if (c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == 0xA0 || c == 0xFEFF || (c > 127 && is_js_space(c)))
                p->pos++;
            else if (c == '/' && p->pos + 1 < p->n && s[p->pos + 1] == '/') {
                if (p->pos + 2 < p->n && s[p->pos + 2] == '@' && p->cc_on == FALSE && p->pos + 7 < p->n &&
                    !wcsncmp(s + p->pos + 3, L"cc_on", 5)) {
                    p->pos += 3;
                    p->cc_on = TRUE;
                    p->pos += 5;
                    continue;
                }
                if (p->pos + 2 < p->n && s[p->pos + 2] == '@' && p->cc_on) {
                    p->pos += 3;
                    /* the rest of the line is code. */
                    if (p->pos < p->n && ident_start(s[p->pos]))
                        p->pos--;
                    continue;
                }
                skip_to_line_end(p);
            } else if (c == '/' && p->pos + 1 < p->n && s[p->pos + 1] == '*') {
                ULONG sl = p->line, sc = p->pos - p->line_start;
                if (p->pos + 2 < p->n && s[p->pos + 2] == '@') {
                    if (!p->cc_on && p->pos + 7 < p->n && !wcsncmp(s + p->pos + 3, L"cc_on", 5)) {
                        p->cc_on = TRUE;
                        p->pos += 8;
                        continue;
                    }
                    if (p->cc_on) {
                        p->pos += 3;
                        if (p->pos < p->n && ident_start(s[p->pos]))
                            p->pos--;
                        continue;
                    }
                }
                p->pos += 2;
                BOOL closed = FALSE;
                while (p->pos < p->n) {
                    if (s[p->pos] == '*' && p->pos + 1 < p->n && s[p->pos + 1] == '/') {
                        p->pos += 2;
                        closed = TRUE;
                        break;
                    }
                    if (is_line_term(s[p->pos])) {
                        p->nl_before = TRUE;
                        newline_at(p);
                    } else
                        p->pos++;
                }
                if (!closed) {
                    (void)sl;
                    (void)sc;
                    perr_at(p, 1016, p->line, p->pos - p->line_start);
                    p->tok = TK_EOF;
                    return;
                }
            } else if (c == '@' && p->cc_on && p->pos + 1 < p->n && s[p->pos + 1] == '*' && p->pos + 2 < p->n &&
                       s[p->pos + 2] == '/') {
                p->pos += 3;
            } else
                break;
        }
        p->tstart = p->pos;
        p->tline = p->line;
        p->tcol = p->pos - p->line_start;
        if (p->pos >= p->n) {
            p->tok = TK_EOF;
            p->op = 0;
            p->tend = p->pos;
            if (p->cc_depth && !p->failed)
                perr_at(p, 1029, p->tline, p->tcol);
            return;
        }
        WCHAR c = s[p->pos];
        if (c == '@') {
            if (cc_directive(p)) {
                if (p->failed) {
                    p->tok = TK_EOF;
                    return;
                }
                continue;
            }
            if (!p->cc_on) {
                /* an @ with conditional compilation off. */
                ULONG st = p->pos + 1;
                ULONG q = st;
                while (q < p->n && ident_part(s[q]))
                    q++;
                if (q > st)
                    perr_at(p, 1030, p->line, q - p->line_start);
                else
                    perr(p, 1014);
                return;
            }
            /* @name is a number. */
            ULONG st = p->pos + 1;
            ULONG q = st;
            while (q < p->n && ident_part(s[q]))
                q++;
            if (q == st) {
                perr(p, 1014);
                return;
            }
            if (!cc_active(p)) {
                p->pos = q;
                continue;
            }
            double v;
            if (!cc_var_get(p, s + st, q - st, &v))
                v = js_nan;
            p->pos = q;
            if (name_is(p, s + st, q - st, L"_jscript")) {
                p->tok = TK_KEYWORD;
                p->op = K_TRUE;
            } else {
                p->tok = TK_NUM;
                p->num = v;
            }
            p->tend = p->pos;
            return;
        }
        if (!cc_active(p)) {
            /* the text of an inactive branch is skipped token by token, strings and comments included. */
            if (c == '"' || c == '\'') {
                lex_string(p, c);
                if (p->failed) {
                    p->tok = TK_EOF;
                    return;
                }
            } else if (ident_part(c) || c > 127)
                while (p->pos < p->n && ident_part(s[p->pos]))
                    p->pos++;
            else
                p->pos++;
            continue;
        }
        break;
    }
    const WCHAR *sp = p->s;
    WCHAR c = sp[p->pos];
    if (ident_start(c) || c == '\\') {
        ULONG st = p->pos;
        WCHAR *buf = nullptr;
        SIZE_T len = 0, cap = 0;
        while (p->pos < p->n && (ident_part(sp[p->pos]) || sp[p->pos] == '\\')) {
            WCHAR ch = sp[p->pos];
            if (ch == '\\') {
                if (p->pos + 5 < p->n + 0 && sp[p->pos + 1] == 'u') {
                    int v = 0, ok = 1;
                    for (int i = 0; i < 4; i++) {
                        int d = hex_digit(sp[p->pos + 2 + i]);
                        if (d < 0)
                            ok = 0;
                        v = v * 16 + d;
                    }
                    if (ok) {
                        p->pos += 6;
                        ch = (WCHAR)v;
                    } else {
                        perr(p, 1014);
                        return;
                    }
                } else {
                    perr(p, 1014);
                    return;
                }
            } else
                p->pos++;
            if (len + 1 >= cap) {
                cap = cap ? cap * 2 : 32;
                buf = js_realloc(buf, cap * sizeof(WCHAR));
            }
            buf[len++] = ch;
        }
        (void)st;
        p->tok = TK_IDENT;
        p->op = 0;
        for (UINT i = 0; i < sizeof(g_keywords) / sizeof(g_keywords[0]); i++)
            if (wcslen(g_keywords[i].Text) == len && !wcsncmp(g_keywords[i].Text, buf, len)) {
                p->tok = TK_KEYWORD;
                p->op = g_keywords[i].Id;
                break;
            }
        p->str = str_intern(p->e, buf, len);
        js_free(buf);
        p->tend = p->pos;
        return;
    }
    if ((c >= '0' && c <= '9') || (c == '.' && p->pos + 1 < p->n && sp[p->pos + 1] >= '0' && sp[p->pos + 1] <= '9')) {
        lex_number(p);
        p->tend = p->pos;
        return;
    }
    if (c == '"' || c == '\'') {
        lex_string(p, c);
        p->tend = p->pos;
        return;
    }
    /* punctuators. */
    p->tok = TK_PUNCT;
    WCHAR c1 = p->pos + 1 < p->n ? sp[p->pos + 1] : 0;
    WCHAR c2 = p->pos + 2 < p->n ? sp[p->pos + 2] : 0;
    WCHAR c3 = p->pos + 3 < p->n ? sp[p->pos + 3] : 0;
    int op = 0, width = 1;
    switch (c) {
    case '{': case '}': case '(': case ')': case '[': case ']': case ';': case ',': case '~': case '?': case ':':
    case '.':
        op = c;
        break;
    case '<':
        if (c1 == '<') {
            if (c2 == '=') { op = OP_SHLA; width = 3; }
            else { op = OP_SHL; width = 2; }
        } else if (c1 == '=') { op = OP_LE; width = 2; }
        else op = '<';
        break;
    case '>':
        if (c1 == '>') {
            if (c2 == '>') {
                if (c3 == '=') { op = OP_USHRA; width = 4; }
                else { op = OP_USHR; width = 3; }
            } else if (c2 == '=') { op = OP_SHRA; width = 3; }
            else { op = OP_SHR; width = 2; }
        } else if (c1 == '=') { op = OP_GE; width = 2; }
        else op = '>';
        break;
    case '=':
        if (c1 == '=') {
            if (c2 == '=') { op = OP_SEQ; width = 3; }
            else { op = OP_EQ; width = 2; }
        } else op = '=';
        break;
    case '!':
        if (c1 == '=') {
            if (c2 == '=') { op = OP_SNE; width = 3; }
            else { op = OP_NE; width = 2; }
        } else op = '!';
        break;
    case '+':
        if (c1 == '+') { op = OP_INC; width = 2; }
        else if (c1 == '=') { op = OP_ADDA; width = 2; }
        else op = '+';
        break;
    case '-':
        if (c1 == '-') { op = OP_DEC; width = 2; }
        else if (c1 == '=') { op = OP_SUBA; width = 2; }
        else op = '-';
        break;
    case '*':
        if (c1 == '=') { op = OP_MULA; width = 2; } else op = '*';
        break;
    case '%':
        if (c1 == '=') { op = OP_MODA; width = 2; } else op = '%';
        break;
    case '/':
        if (c1 == '=') { op = OP_DIVA; width = 2; } else op = '/';
        break;
    case '&':
        if (c1 == '&') { op = OP_AND; width = 2; }
        else if (c1 == '=') { op = OP_ANDA; width = 2; }
        else op = '&';
        break;
    case '|':
        if (c1 == '|') { op = OP_OR; width = 2; }
        else if (c1 == '=') { op = OP_ORA; width = 2; }
        else op = '|';
        break;
    case '^':
        if (c1 == '=') { op = OP_XORA; width = 2; } else op = '^';
        break;
    default:
        perr(p, 1014);
        return;
    }
    p->op = op;
    p->pos += width;
    p->tend = p->pos;
}

/* a / or /= at the start of an operand begins a regular expression. */
static void lex_regex(Parser *p)
{
    const WCHAR *s = p->s;
    ULONG st = p->tstart;
    p->pos = st + 1;
    BOOL in_class = FALSE;
    for (;;) {
        if (p->pos >= p->n || is_line_term(s[p->pos])) {
            perr_at(p, 1012, p->line, p->pos - p->line_start);
            return;
        }
        WCHAR c = s[p->pos++];
        if (c == '\\') {
            if (p->pos < p->n && !is_line_term(s[p->pos]))
                p->pos++;
            continue;
        }
        if (c == '[')
            in_class = TRUE;
        else if (c == ']')
            in_class = FALSE;
        else if (c == '/' && !in_class)
            break;
    }
    while (p->pos < p->n && ident_part(s[p->pos]))
        p->pos++;
    p->tok = TK_REGEX;
    p->str = str_new(p->e, s + st, p->pos - st);
    p->tend = p->pos;
}

/* ------------------------------------------------------------------ parsing */

static Node *parse_assignment(Parser *p, BOOL no_in);
static Node *parse_expression(Parser *p, BOOL no_in);
static Node *parse_statement(Parser *p);

static BOOL is_punct(Parser *p, int op)
{
    return p->tok == TK_PUNCT && p->op == op;
}

static BOOL is_kw(Parser *p, int k)
{
    return p->tok == TK_KEYWORD && p->op == k;
}

static BOOL accept_punct(Parser *p, int op)
{
    if (is_punct(p, op)) {
        lex(p);
        return TRUE;
    }
    return FALSE;
}

static BOOL expect_punct(Parser *p, int op, int code)
{
    if (is_punct(p, op)) {
        lex(p);
        return TRUE;
    }
    perr(p, code);
    return FALSE;
}

static Node *finish(Parser *p, Node *n)
{
    n->end = p->prev_end;
    return n;
}

static Node *parse_function(Parser *p, BOOL is_decl);

static Node *parse_primary(Parser *p)
{
    Node *n;
    switch (p->tok) {
    case TK_NUM:
        n = new_node(p, N_NUM);
        n->u.num = p->num;
        lex(p);
        return finish(p, n);
    case TK_STR:
        n = new_node(p, N_STR);
        n->u.str = str_intern_str(p->e, p->str);
        lex(p);
        return finish(p, n);
    case TK_IDENT:
        n = new_node(p, N_IDENT);
        n->u.str = p->str;
        if (p->str->len == 9 && !wcscmp(p->str->s, L"arguments") && p->fn)
            p->fn->uses_args = 1;
        lex(p);
        return finish(p, n);
    case TK_KEYWORD:
        switch (p->op) {
        case K_THIS:
            n = new_node(p, N_THIS);
            lex(p);
            return finish(p, n);
        case K_TRUE:
            n = new_node(p, N_TRUE);
            lex(p);
            return finish(p, n);
        case K_FALSE:
            n = new_node(p, N_FALSE);
            lex(p);
            return finish(p, n);
        case K_NULL:
            n = new_node(p, N_NULL);
            lex(p);
            return finish(p, n);
        case K_FUNCTION:
            return parse_function(p, FALSE);
        }
        perr(p, 1002);
        return nullptr;
    case TK_PUNCT:
        if (p->op == '(') {
            ULONG start = p->tstart, line = p->tline, col = p->tcol;
            lex(p);
            n = parse_expression(p, FALSE);
            if (p->failed)
                return nullptr;
            if (!expect_punct(p, ')', 1006))
                return nullptr;
            /* the group is part of the text of a function written inside it. */
            n->flag |= 4;
            if (n->kind == N_FUNC) {
                n->start = start;
                n->line = line;
                n->col = col;
                n->u.fn->n.start = start;
                n->u.fn->n.end = p->prev_end;
            }
            return n;
        }
        if (p->op == '[') {
            n = new_node(p, N_ARRAY);
            lex(p);
            Node **tail = &n->a;
            ULONG count = 0;
            for (;;) {
                if (is_punct(p, ']')) {
                    lex(p);
                    break;
                }
                if (is_punct(p, ',')) {
                    Node *hole = new_node(p, N_ELISION);
                    *tail = hole;
                    tail = &hole->next;
                    count++;
                    lex(p);
                    if (is_punct(p, ']')) {
                        Node *last = new_node(p, N_ELISION);
                        *tail = last;
                        tail = &last->next;
                        count++;
                    }
                    continue;
                }
                Node *el = parse_assignment(p, FALSE);
                if (p->failed)
                    return nullptr;
                *tail = el;
                tail = &el->next;
                count++;
                if (is_punct(p, ',')) {
                    lex(p);
                    /* a trailing comma counts as one more hole, as JScript does. */
                    if (is_punct(p, ']')) {
                        Node *hole = new_node(p, N_ELISION);
                        *tail = hole;
                        tail = &hole->next;
                        count++;
                    }
                    continue;
                }
                if (!expect_punct(p, ']', 1007))
                    return nullptr;
                break;
            }
            n->u.count = count;
            return finish(p, n);
        }
        if (p->op == '{') {
            n = new_node(p, N_OBJECT);
            lex(p);
            Node **tail = &n->a;
            for (;;) {
                if (is_punct(p, '}')) {
                    lex(p);
                    break;
                }
                Node *prop = new_node(p, N_PROP);
                if (p->tok == TK_IDENT)
                    prop->u.str = p->str;
                else if (p->tok == TK_STR)
                    prop->u.str = str_intern_str(p->e, p->str);
                else if (p->tok == TK_NUM)
                    prop->u.str = str_intern_str(p->e, num_to_str(p->e, p->num));
                else {
                    perr(p, 1028);
                    return nullptr;
                }
                lex(p);
                if (!expect_punct(p, ':', 1003))
                    return nullptr;
                prop->a = parse_assignment(p, FALSE);
                if (p->failed)
                    return nullptr;
                *tail = prop;
                tail = &prop->next;
                if (is_punct(p, ',')) {
                    lex(p);
                    continue;
                }
                if (!expect_punct(p, '}', 1009))
                    return nullptr;
                break;
            }
            return finish(p, n);
        }
        if (p->op == '/' || p->op == OP_DIVA) {
            n = new_node(p, N_REGEX);
            lex_regex(p);
            if (p->failed)
                return nullptr;
            n->u.str = p->str;
            lex(p);
            return finish(p, n);
        }
        perr(p, 1002);
        return nullptr;
    }
    if (!p->failed)
        perr(p, p->tok == TK_EOF ? 1002 : 1002);
    return nullptr;
}

static Node *parse_arguments(Parser *p, Node *call)
{
    /* the open parenthesis is the current token. */
    lex(p);
    Node **tail = &call->b;
    ULONG count = 0;
    if (!is_punct(p, ')')) {
        for (;;) {
            Node *arg = parse_assignment(p, FALSE);
            if (p->failed)
                return nullptr;
            *tail = arg;
            tail = &arg->next;
            count++;
            if (!accept_punct(p, ','))
                break;
        }
    }
    if (!expect_punct(p, ')', 1006))
        return nullptr;
    call->u.count = count;
    return call;
}

static Node *parse_member_tail(Parser *p, Node *base, BOOL allow_call)
{
    for (;;) {
        if (p->failed)
            return nullptr;
        if (is_punct(p, '.')) {
            lex(p);
            if (p->tok != TK_IDENT) {
                perr(p, 1010);
                return nullptr;
            }
            Node *m = new_node(p, N_MEMBER);
            m->start = base->start;
            m->line = base->line;
            m->col = base->col;
            m->a = base;
            m->u.str = p->str;
            lex(p);
            base = finish(p, m);
        } else if (is_punct(p, '[')) {
            Node *m = new_node(p, N_INDEX);
            m->start = base->start;
            m->line = base->line;
            m->col = base->col;
            m->a = base;
            lex(p);
            m->b = parse_expression(p, FALSE);
            if (p->failed)
                return nullptr;
            if (!expect_punct(p, ']', 1007))
                return nullptr;
            base = finish(p, m);
        } else if (allow_call && is_punct(p, '(')) {
            Node *c = new_node(p, N_CALL);
            c->start = base->start;
            c->line = base->line;
            c->col = base->col;
            c->a = base;
            if (!parse_arguments(p, c))
                return nullptr;
            base = finish(p, c);
        } else
            return base;
    }
}

static Node *parse_new(Parser *p)
{
    Node *n = new_node(p, N_NEW);
    lex(p);
    Node *target;
    if (is_kw(p, K_NEW))
        target = parse_new(p);
    else
        target = parse_primary(p);
    if (p->failed || !target)
        return nullptr;
    target = parse_member_tail(p, target, FALSE);
    if (!target)
        return nullptr;
    n->a = target;
    if (is_punct(p, '(')) {
        if (!parse_arguments(p, n))
            return nullptr;
    }
    return finish(p, n);
}

static Node *parse_postfix(Parser *p)
{
    Node *e;
    if (is_kw(p, K_NEW))
        e = parse_new(p);
    else
        e = parse_primary(p);
    if (!e || p->failed)
        return nullptr;
    e = parse_member_tail(p, e, TRUE);
    if (!e)
        return nullptr;
    if (!p->nl_before && p->tok == TK_PUNCT && (p->op == OP_INC || p->op == OP_DEC)) {
        Node *n = new_node(p, N_POSTFIX);
        n->start = e->start;
        n->line = e->line;
        n->col = e->col;
        n->op = (BYTE)p->op;
        n->a = e;
        lex(p);
        return finish(p, n);
    }
    return e;
}

static Node *parse_unary(Parser *p)
{
    if (p->tok == TK_PUNCT) {
        int op = p->op;
        if (op == '!' || op == '~' || op == '+' || op == '-') {
            Node *n = new_node(p, N_UNARY);
            n->op = (BYTE)op;
            lex(p);
            n->a = parse_unary(p);
            if (!n->a)
                return nullptr;
            return finish(p, n);
        }
        if (op == OP_INC || op == OP_DEC) {
            Node *n = new_node(p, N_PREFIX);
            n->op = (BYTE)op;
            lex(p);
            n->a = parse_unary(p);
            if (!n->a)
                return nullptr;
            return finish(p, n);
        }
    } else if (p->tok == TK_KEYWORD && (p->op == K_TYPEOF || p->op == K_VOID || p->op == K_DELETE)) {
        Node *n = new_node(p, N_UNARY);
        n->op = p->op == K_TYPEOF ? OP_TYPEOF : p->op == K_VOID ? OP_VOID : OP_DELETE;
        lex(p);
        n->a = parse_unary(p);
        if (!n->a)
            return nullptr;
        return finish(p, n);
    }
    return parse_postfix(p);
}

static int binary_prec(Parser *p, BOOL no_in)
{
    if (p->tok == TK_KEYWORD) {
        if (p->op == K_INSTANCEOF)
            return 7;
        if (p->op == K_IN && !no_in)
            return 7;
        return 0;
    }
    if (p->tok != TK_PUNCT)
        return 0;
    switch (p->op) {
    case OP_OR: return 1;
    case OP_AND: return 2;
    case '|': return 3;
    case '^': return 4;
    case '&': return 5;
    case OP_EQ: case OP_NE: case OP_SEQ: case OP_SNE: return 6;
    case '<': case '>': case OP_LE: case OP_GE: return 7;
    case OP_SHL: case OP_SHR: case OP_USHR: return 8;
    case '+': case '-': return 9;
    case '*': case '/': case '%': return 10;
    }
    return 0;
}

static Node *parse_binary(Parser *p, int level, BOOL no_in)
{
    Node *left = parse_unary(p);
    if (!left)
        return nullptr;
    for (;;) {
        int prec = binary_prec(p, no_in);
        if (!prec || prec <= level)
            return left;
        int op = p->tok == TK_KEYWORD ? (p->op == K_INSTANCEOF ? OP_INSTANCEOF : OP_IN) : p->op;
        Node *n = new_node(p, (op == OP_AND || op == OP_OR) ? N_LOGICAL : N_BINARY);
        n->start = left->start;
        n->line = left->line;
        n->col = left->col;
        n->op = (BYTE)op;
        lex(p);
        n->a = left;
        n->b = parse_binary(p, prec, no_in);
        if (!n->b)
            return nullptr;
        left = finish(p, n);
    }
}

static BOOL is_assign_op(int op)
{
    return op == '=' || (op >= OP_ADDA && op <= OP_XORA) || op == OP_DIVA || op == OP_MODA;
}

static Node *parse_assignment(Parser *p, BOOL no_in)
{
    Node *left = parse_binary(p, 0, no_in);
    if (!left)
        return nullptr;
    if (is_punct(p, '?')) {
        Node *n = new_node(p, N_COND);
        n->start = left->start;
        n->line = left->line;
        n->col = left->col;
        lex(p);
        n->a = left;
        n->b = parse_assignment(p, FALSE);
        if (!n->b || !expect_punct(p, ':', 1003))
            return nullptr;
        n->c = parse_assignment(p, no_in);
        if (!n->c)
            return nullptr;
        return finish(p, n);
    }
    if (p->tok == TK_PUNCT && is_assign_op(p->op)) {
        if (!(left->flag & 4) && (left->kind == N_BINARY || left->kind == N_LOGICAL || left->kind == N_COND ||
                                  left->kind == N_UNARY || left->kind == N_POSTFIX || left->kind == N_PREFIX ||
                                  left->kind == N_COMMA || left->kind == N_FUNC || left->kind == N_ASSIGN)) {
            perr(p, 1002);
            return nullptr;
        }
        Node *n = new_node(p, N_ASSIGN);
        n->start = left->start;
        n->line = left->line;
        n->col = left->col;
        n->op = (BYTE)p->op;
        lex(p);
        n->a = left;
        n->b = parse_assignment(p, no_in);
        if (!n->b)
            return nullptr;
        return finish(p, n);
    }
    return left;
}

static Node *parse_expression(Parser *p, BOOL no_in)
{
    Node *left = parse_assignment(p, no_in);
    if (!left)
        return nullptr;
    while (is_punct(p, ',')) {
        Node *n = new_node(p, N_COMMA);
        n->start = left->start;
        n->line = left->line;
        n->col = left->col;
        lex(p);
        n->a = left;
        n->b = parse_assignment(p, no_in);
        if (!n->b)
            return nullptr;
        left = finish(p, n);
    }
    return left;
}

/* ------------------------------------------------------------------ functions */

static void fn_add_var(Parser *p, FuncNode *f, Str *name)
{
    for (ULONG i = 0; i < f->nvars; i++)
        if (f->vars[i] == name)
            return;
    Str **nv = arena_alloc(p->u->arena, (f->nvars + 1) * sizeof(Str *));
    if (f->nvars)
        memcpy(nv, f->vars, f->nvars * sizeof(Str *));
    nv[f->nvars++] = name;
    f->vars = nv;
}

static void fn_add_func(Parser *p, FuncNode *f, Node *decl)
{
    Node **nf = arena_alloc(p->u->arena, (f->nfuncs + 1) * sizeof(Node *));
    if (f->nfuncs)
        memcpy(nf, f->funcs, f->nfuncs * sizeof(Node *));
    nf[f->nfuncs++] = decl;
    f->funcs = nf;
}

static Node *parse_function(Parser *p, BOOL is_decl)
{
    ULONG start = p->tstart;
    FuncNode *f = arena_alloc(p->u->arena, sizeof(FuncNode));
    f->n.kind = is_decl ? N_FUNCDECL : N_FUNC;
    f->n.line = p->tline;
    f->n.col = p->tcol;
    f->n.start = start;
    f->n.u.fn = f;
    f->unit = p->u;
    f->is_expr = !is_decl;
    lex(p);
    if (p->tok == TK_IDENT) {
        f->name = p->str;
        lex(p);
    } else if (is_decl) {
        perr(p, 1010);
        return nullptr;
    }
    if (!expect_punct(p, '(', 1005))
        return nullptr;
    Str **params = nullptr;
    ULONG np = 0;
    if (!is_punct(p, ')')) {
        for (;;) {
            if (p->tok != TK_IDENT) {
                perr(p, 1010);
                return nullptr;
            }
            Str **np2 = arena_alloc(p->u->arena, (np + 1) * sizeof(Str *));
            if (np)
                memcpy(np2, params, np * sizeof(Str *));
            np2[np++] = p->str;
            params = np2;
            lex(p);
            if (!accept_punct(p, ','))
                break;
        }
    }
    f->params = params;
    f->nparams = np;
    if (!expect_punct(p, ')', 1006))
        return nullptr;
    if (!is_punct(p, '{')) {
        perr(p, 1008);
        return nullptr;
    }
    lex(p);
    FuncNode *saved = p->fn;
    int sl = p->loops, ss = p->switches;
    Label *slabels = p->labels;
    p->fn = f;
    p->loops = p->switches = 0;
    p->labels = nullptr;
    p->funcdepth++;
    Node **tail = &f->body;
    while (!is_punct(p, '}') && p->tok != TK_EOF && !p->failed) {
        Node *st = parse_statement(p);
        if (!st)
            break;
        *tail = st;
        while (st->next)
            st = st->next;
        tail = &st->next;
    }
    p->funcdepth--;
    p->fn = saved;
    p->loops = sl;
    p->switches = ss;
    p->labels = slabels;
    if (p->failed)
        return nullptr;
    if (!is_punct(p, '}')) {
        perr(p, 1009);
        return nullptr;
    }
    lex(p);
    f->n.end = p->prev_end;
    if (is_decl) {
        fn_add_func(p, saved, &f->n);
        /* a function declared in a function is a variable of it. */
        if (f->name)
            fn_add_var(p, saved, f->name);
    }
    return &f->n;
}

/* ------------------------------------------------------------------ statements */

static void consume_semicolon(Parser *p)
{
    if (p->failed)
        return;
    if (is_punct(p, ';')) {
        lex(p);
        return;
    }
    if (is_punct(p, '}') || p->tok == TK_EOF || p->nl_before)
        return;
    perr(p, 1004);
}

static Node *parse_var(Parser *p, BOOL no_in)
{
    Node *n = new_node(p, N_VAR);
    lex(p);
    Node **tail = &n->a;
    for (;;) {
        if (p->tok != TK_IDENT) {
            perr(p, 1010);
            return nullptr;
        }
        Node *d = new_node(p, N_PROP);
        d->u.str = p->str;
        if (p->fn)
            fn_add_var(p, p->fn, p->str);
        lex(p);
        if (is_punct(p, '=')) {
            lex(p);
            d->a = parse_assignment(p, no_in);
            if (!d->a)
                return nullptr;
        }
        *tail = d;
        tail = &d->next;
        if (!accept_punct(p, ','))
            break;
    }
    return finish(p, n);
}

static Node *parse_block(Parser *p)
{
    Node *n = new_node(p, N_BLOCK);
    lex(p);
    Node **tail = &n->a;
    while (!is_punct(p, '}') && p->tok != TK_EOF && !p->failed) {
        Node *st = parse_statement(p);
        if (!st)
            return nullptr;
        *tail = st;
        while (st->next)
            st = st->next;
        tail = &st->next;
    }
    if (p->failed)
        return nullptr;
    if (!is_punct(p, '}')) {
        perr(p, 1009);
        return nullptr;
    }
    lex(p);
    return finish(p, n);
}

static Node *parse_loop_body(Parser *p)
{
    p->loops++;
    Node *b = parse_statement(p);
    p->loops--;
    return b;
}

static Label *find_label(Parser *p, Str *name)
{
    for (Label *l = p->labels; l; l = l->Next)
        if (l->Name == name)
            return l;
    return nullptr;
}

static Node *parse_statement_inner(Parser *p)
{
    Node *n;
    if (p->tok == TK_PUNCT) {
        if (p->op == '{')
            return parse_block(p);
        if (p->op == ';') {
            n = new_node(p, N_EMPTY);
            lex(p);
            return finish(p, n);
        }
    }
    if (p->tok == TK_KEYWORD) {
        switch (p->op) {
        case K_VAR:
            n = parse_var(p, FALSE);
            if (!n)
                return nullptr;
            consume_semicolon(p);
            n->end = p->prev_end;
            return n;
        case K_FUNCTION: {
            /* a function without a name at the start of a statement is an expression. */
            {
                ULONG save_pos = p->pos, save_line = p->line, save_ls = p->line_start;
                int stok = p->tok, sop = p->op;
                Str *sstr = p->str;
                ULONG st = p->tstart, se = p->tend, sl = p->tline, sc = p->tcol;
                BOOL snl = p->nl_before;
                ULONG spe = p->prev_end;
                lex(p);
                BOOL named = p->tok == TK_IDENT;
                p->pos = save_pos;
                p->line = save_line;
                p->line_start = save_ls;
                p->tok = stok;
                p->op = sop;
                p->str = sstr;
                p->tstart = st;
                p->tend = se;
                p->tline = sl;
                p->tcol = sc;
                p->nl_before = snl;
                p->prev_end = spe;
                if (!named)
                    break;
            }
            Node *f = parse_function(p, TRUE);
            if (!f)
                return nullptr;
            n = new_node(p, N_EMPTY);
            n->line = f->line;
            n->col = f->col;
            n->start = f->start;
            n->end = f->end;
            return n;
        }
        case K_IF: {
            n = new_node(p, N_IF);
            lex(p);
            if (!expect_punct(p, '(', 1005))
                return nullptr;
            n->a = parse_expression(p, FALSE);
            if (!n->a || !expect_punct(p, ')', 1006))
                return nullptr;
            n->b = parse_statement(p);
            if (!n->b)
                return nullptr;
            if (is_kw(p, K_ELSE)) {
                lex(p);
                n->c = parse_statement(p);
                if (!n->c)
                    return nullptr;
            }
            return finish(p, n);
        }
        case K_FOR: {
            n = new_node(p, N_FOR);
            lex(p);
            if (!expect_punct(p, '(', 1005))
                return nullptr;
            Node *init = nullptr;
            if (is_kw(p, K_VAR)) {
                init = parse_var(p, TRUE);
                if (!init)
                    return nullptr;
            } else if (!is_punct(p, ';')) {
                init = parse_expression(p, TRUE);
                if (!init)
                    return nullptr;
            }
            if (init && is_kw(p, K_IN)) {
                n->kind = N_FORIN;
                if (init->kind == N_VAR && init->a->next) {
                    perr(p, 1002);
                    return nullptr;
                }
                lex(p);
                n->a = init;
                n->b = parse_expression(p, FALSE);
                if (!n->b || !expect_punct(p, ')', 1006))
                    return nullptr;
                n->d = parse_loop_body(p);
                if (!n->d)
                    return nullptr;
                return finish(p, n);
            }
            n->a = init;
            if (!expect_punct(p, ';', 1004))
                return nullptr;
            if (!is_punct(p, ';')) {
                n->b = parse_expression(p, FALSE);
                if (!n->b)
                    return nullptr;
            }
            if (!expect_punct(p, ';', 1004))
                return nullptr;
            if (!is_punct(p, ')')) {
                n->c = parse_expression(p, FALSE);
                if (!n->c)
                    return nullptr;
            }
            if (!expect_punct(p, ')', 1006))
                return nullptr;
            n->d = parse_loop_body(p);
            if (!n->d)
                return nullptr;
            return finish(p, n);
        }
        case K_WHILE:
            n = new_node(p, N_WHILE);
            lex(p);
            if (!expect_punct(p, '(', 1005))
                return nullptr;
            n->a = parse_expression(p, FALSE);
            if (!n->a || !expect_punct(p, ')', 1006))
                return nullptr;
            n->d = parse_loop_body(p);
            if (!n->d)
                return nullptr;
            return finish(p, n);
        case K_DO:
            n = new_node(p, N_DO);
            lex(p);
            n->d = parse_loop_body(p);
            if (!n->d)
                return nullptr;
            if (!is_kw(p, K_WHILE)) {
                perr(p, 1024);
                return nullptr;
            }
            lex(p);
            if (!expect_punct(p, '(', 1005))
                return nullptr;
            n->a = parse_expression(p, FALSE);
            if (!n->a || !expect_punct(p, ')', 1006))
                return nullptr;
            /* a semicolon after the do loop is optional even on one line. */
            accept_punct(p, ';');
            return finish(p, n);
        case K_CONTINUE:
        case K_BREAK: {
            BOOL is_break = p->op == K_BREAK;
            n = new_node(p, is_break ? N_BREAK : N_CONTINUE);
            lex(p);
            if (p->tok == TK_IDENT && !p->nl_before) {
                Str *lname = p->str;
                lex(p);
                Label *l = find_label(p, lname);
                if (!l || (!is_break && !l->IsLoop)) {
                    perr(p, 1026);
                    return nullptr;
                }
                n->u.str = lname;
            } else if (is_break ? (!p->loops && !p->switches) : !p->loops) {
                perr(p, is_break ? 1019 : 1020);
                return nullptr;
            }
            consume_semicolon(p);
            return finish(p, n);
        }
        case K_RETURN:
            n = new_node(p, N_RETURN);
            if (!p->funcdepth && p->mode != PARSE_FUNCTION) {
                perr(p, 1018);
                return nullptr;
            }
            lex(p);
            if (!p->nl_before && !is_punct(p, ';') && !is_punct(p, '}') && p->tok != TK_EOF) {
                n->a = parse_expression(p, FALSE);
                if (!n->a)
                    return nullptr;
            }
            consume_semicolon(p);
            return finish(p, n);
        case K_WITH:
            n = new_node(p, N_WITH);
            lex(p);
            if (!expect_punct(p, '(', 1005))
                return nullptr;
            n->a = parse_expression(p, FALSE);
            if (!n->a || !expect_punct(p, ')', 1006))
                return nullptr;
            if (p->fn)
                p->fn->uses_eval = 1;
            n->d = parse_statement(p);
            if (!n->d)
                return nullptr;
            return finish(p, n);
        case K_SWITCH: {
            n = new_node(p, N_SWITCH);
            lex(p);
            if (!expect_punct(p, '(', 1005))
                return nullptr;
            n->a = parse_expression(p, FALSE);
            if (!n->a || !expect_punct(p, ')', 1006))
                return nullptr;
            if (!expect_punct(p, '{', 1008))
                return nullptr;
            Node **tail = &n->b;
            BOOL has_default = FALSE;
            p->switches++;
            while (!is_punct(p, '}')) {
                if (p->failed || p->tok == TK_EOF) {
                    perr(p, 1009);
                    p->switches--;
                    return nullptr;
                }
                Node *c = new_node(p, N_CASE);
                if (is_kw(p, K_CASE)) {
                    lex(p);
                    c->a = parse_expression(p, FALSE);
                    if (!c->a) {
                        p->switches--;
                        return nullptr;
                    }
                } else if (is_kw(p, K_DEFAULT)) {
                    if (has_default) {
                        perr(p, 1027);
                        p->switches--;
                        return nullptr;
                    }
                    has_default = TRUE;
                    c->flag = 1;
                    lex(p);
                } else {
                    perr(p, 1002);
                    p->switches--;
                    return nullptr;
                }
                if (!expect_punct(p, ':', 1003)) {
                    p->switches--;
                    return nullptr;
                }
                Node **st = &c->b;
                while (!is_punct(p, '}') && !is_kw(p, K_CASE) && !is_kw(p, K_DEFAULT) && p->tok != TK_EOF && !p->failed) {
                    Node *s = parse_statement(p);
                    if (!s) {
                        p->switches--;
                        return nullptr;
                    }
                    *st = s;
                    while (s->next)
                        s = s->next;
                    st = &s->next;
                }
                *tail = c;
                tail = &c->next;
            }
            p->switches--;
            lex(p);
            return finish(p, n);
        }
        case K_THROW:
            n = new_node(p, N_THROW);
            lex(p);
            if (p->nl_before) {
                perr(p, 1035);
                return nullptr;
            }
            n->a = parse_expression(p, FALSE);
            if (!n->a)
                return nullptr;
            consume_semicolon(p);
            return finish(p, n);
        case K_TRY: {
            n = new_node(p, N_TRY);
            lex(p);
            if (!is_punct(p, '{')) {
                perr(p, 1008);
                return nullptr;
            }
            n->a = parse_block(p);
            if (!n->a)
                return nullptr;
            if (is_kw(p, K_CATCH)) {
                lex(p);
                if (!expect_punct(p, '(', 1005))
                    return nullptr;
                if (p->tok != TK_IDENT) {
                    perr(p, 1010);
                    return nullptr;
                }
                n->u.str = p->str;
                lex(p);
                if (!expect_punct(p, ')', 1006))
                    return nullptr;
                if (!is_punct(p, '{')) {
                    perr(p, 1008);
                    return nullptr;
                }
                n->b = parse_block(p);
                if (!n->b)
                    return nullptr;
            }
            if (is_kw(p, K_FINALLY)) {
                lex(p);
                if (!is_punct(p, '{')) {
                    perr(p, 1008);
                    return nullptr;
                }
                n->d = parse_block(p);
                if (!n->d)
                    return nullptr;
            } else if (!n->b) {
                perr(p, 1033);
                return nullptr;
            }
            return finish(p, n);
        }
        default:
            break;
        }
    }
    /* a label or an expression statement. */
    if (p->tok == TK_IDENT) {
        /* look ahead for a colon without consuming the expression parse. */
        ULONG save_pos = p->pos, save_line = p->line, save_ls = p->line_start;
        int stok = p->tok, sop = p->op;
        Str *sstr = p->str;
        ULONG st = p->tstart, se = p->tend, sl = p->tline, sc = p->tcol;
        BOOL snl = p->nl_before;
        ULONG spe = p->prev_end;
        lex(p);
        if (is_punct(p, ':') && !p->failed) {
            if (find_label(p, sstr)) {
                perr(p, 1025);
                return nullptr;
            }
            lex(p);
            Label lab = {sstr, FALSE, p->labels};
            if (is_kw(p, K_FOR) || is_kw(p, K_WHILE) || is_kw(p, K_DO))
                lab.IsLoop = TRUE;
            p->labels = &lab;
            n = new_node(p, N_LABEL);
            n->line = sl;
            n->col = sc;
            n->start = st;
            n->u.str = sstr;
            n->a = parse_statement(p);
            p->labels = lab.Next;
            if (!n->a)
                return nullptr;
            return finish(p, n);
        }
        if (p->failed)
            return nullptr;
        /* not a label: rewind. */
        p->pos = save_pos;
        p->line = save_line;
        p->line_start = save_ls;
        p->tok = stok;
        p->op = sop;
        p->str = sstr;
        p->tstart = st;
        p->tend = se;
        p->tline = sl;
        p->tcol = sc;
        p->nl_before = snl;
        p->prev_end = spe;
    }
    n = new_node(p, N_EXPR);
    n->a = parse_expression(p, FALSE);
    if (!n->a)
        return nullptr;
    consume_semicolon(p);
    return finish(p, n);
}

static Node *parse_statement(Parser *p)
{
    if (p->failed)
        return nullptr;
    Node *n = parse_statement_inner(p);
    if (p->failed)
        return nullptr;
    return n;
}

/* ------------------------------------------------------------------ entry */

static WCHAR *line_text(const WCHAR *s, ULONG n, ULONG target_line)
{
    ULONG line = 0, pos = 0;
    while (pos < n && line < target_line) {
        if (s[pos] == '\n')
            line++;
        else if (s[pos] == '\r') {
            if (pos + 1 < n && s[pos + 1] == '\n')
                pos++;
            line++;
        }
        pos++;
    }
    ULONG st = pos;
    while (pos < n && s[pos] != '\n' && s[pos] != '\r')
        pos++;
    WCHAR *t = js_alloc((pos - st + 1) * sizeof(WCHAR));
    memcpy(t, s + st, (pos - st) * sizeof(WCHAR));
    return t;
}

Unit *parse_unit(Engine *e, const WCHAR *text, SIZE_T length, ULONG first_line, DWORD_PTR cookie, int mode, ParseError *err)
{
    Unit *u = js_alloc(sizeof(Unit));
    u->arena = js_alloc(sizeof(struct Arena));
    u->text = js_alloc((length + 1) * sizeof(WCHAR));
    memcpy(u->text, text, length * sizeof(WCHAR));
    u->length = (ULONG)length;
    u->first_line = first_line;
    u->cookie = cookie;
    Parser p = {};
    p.e = e;
    p.u = u;
    p.s = u->text;
    p.n = (ULONG)length;
    p.mode = mode;
    p.first_line = first_line;
    FuncNode *prog = arena_alloc(u->arena, sizeof(FuncNode));
    prog->n.kind = N_FUNC;
    prog->n.u.fn = prog;
    prog->is_global = 1;
    prog->unit = u;
    u->program = prog;
    p.fn = prog;
    BOOL saved = e->NoGc;
    e->NoGc = TRUE;
    lex(&p);
    Node **tail = &prog->body;
    while (p.tok != TK_EOF && !p.failed) {
        Node *st = parse_statement(&p);
        if (!st)
            break;
        *tail = st;
        while (st->next)
            st = st->next;
        tail = &st->next;
    }
    if (!p.failed && p.tok != TK_EOF)
        perr(&p, 1002);
    e->NoGc = saved;
    if (p.failed) {
        if (err) {
            err->Failed = TRUE;
            err->Code = p.code;
            err->Line = p.eline;
            err->Col = p.ecol;
            err->LineText = line_text(u->text, u->length, p.eline);
        }
        unit_free(u);
        return nullptr;
    }
    prog->n.end = u->length;
    return u;
}
