#include "internal.h"

typedef struct HlHideset {
    struct HlHideset *next;
    const char *name;
} HlHideset;

typedef struct Macro {
    const char *name;
    bool function_like;
    bool variadic;
    bool deleted;
    /* __FILE__ and __LINE__. */
    uint8_t builtin;
    uint32_t param_count;
    const char **params;
    HlToken *body;
    HlLoc loc;
} Macro;

enum { BUILTIN_NONE, BUILTIN_FILE, BUILTIN_LINE };

enum { COND_IF, COND_ELIF, COND_ELSE };

typedef struct Cond {
    uint8_t ctx;
    bool included;
    HlLoc loc;
} Cond;

typedef struct Pp {
    Hlsl *h;
    Macro **macros;
    uint32_t macro_count, macro_capacity;
    Cond *conds;
    uint32_t cond_count, cond_capacity;
    /* the files being read, innermost last, with the condition depth each began at. */
    uint32_t *stack;
    uint32_t *stack_conds;
    uint32_t depth, stack_capacity;
    /* the work of all expansions, against sources that expand without end. */
    uint32_t steps, nesting;
    bool stopped;
    const char *k_defined, *k_va_args, *k_include, *k_define, *k_undef, *k_if, *k_ifdef, *k_ifndef, *k_elif,
        *k_else, *k_endif, *k_line, *k_pragma, *k_error, *k_once, *k_pack_matrix, *k_warning, *k_message;
} Pp;

#define MAX_INCLUDE_DEPTH 64

/* ------------------------------------------------------------------ files */

/* copies the text with \r\n as \n and line splices removed, keeping the line count. */
static uint32_t add_file(Hlsl *h, const char *name, const char *data, size_t size, const void *opened,
                         uint32_t parent)
{
    if (size > 0x7FFFFFF0)
        size = 0x7FFFFFF0;
    char *out = hl_alloc(h, size + 2);
    size_t n = 0;
    uint32_t pending = 0;
    for (size_t i = 0; i < size; i++) {
        char c = data[i];
        if (c == '\\') {
            size_t j = i + 1;
            if (j < size && data[j] == '\r')
                j++;
            if (j < size && data[j] == '\n') {
                pending++;
                i = j;
                continue;
            }
        }
        if (c == '\r') {
            if (i + 1 < size && data[i + 1] == '\n')
                continue;
            c = '\n';
        }
        /* fxc stops reading at a nul. */
        if (!c)
            break;
        out[n++] = c;
        if (c == '\n')
            for (; pending; pending--)
                out[n++] = '\n';
    }
    if (n && out[n - 1] != '\n')
        out[n++] = '\n';
    for (; pending; pending--)
        out[n++] = '\n';
    HlFile f = {.name = name, .data = out, .size = (uint32_t)n, .opened = opened, .parent = parent};
    HL_PUSH(h, h->files, h->file_count, h->file_capacity, f);
    return h->file_count - 1;
}

/* ------------------------------------------------------------------ lexing */

static bool is_ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static bool is_digit(char c) { return c >= '0' && c <= '9'; }
static bool is_ident_char(char c) { return is_ident_start(c) || is_digit(c); }

static const struct {
    const char *text;
    uint16_t punct;
} puncts[] = {
    {"...", P_ELLIPSIS}, {"<<=", P_SHL_ASSIGN}, {">>=", P_SHR_ASSIGN}, {"->", P_ARROW},     {"++", P_INC},
    {"--", P_DEC},       {"<<", P_SHL},         {">>", P_SHR},         {"<=", P_LE},        {">=", P_GE},
    {"==", P_EQ},        {"!=", P_NE},          {"&&", P_ANDAND},      {"||", P_OROR},      {"*=", P_MUL_ASSIGN},
    {"/=", P_DIV_ASSIGN}, {"%=", P_MOD_ASSIGN}, {"+=", P_ADD_ASSIGN},  {"-=", P_SUB_ASSIGN}, {"&=", P_AND_ASSIGN},
    {"^=", P_XOR_ASSIGN}, {"|=", P_OR_ASSIGN},  {"##", P_HASHHASH},    {"::", P_COLONCOLON},
};

static HlToken *new_token(Hlsl *h, int kind, const char *text, uint32_t length, HlLoc loc)
{
    HlToken *t = hl_alloc(h, sizeof(HlToken));
    t->kind = (uint8_t)kind;
    t->text = text;
    t->length = length;
    t->loc = loc;
    return t;
}

static HlToken *tokenize(Pp *pp, uint32_t file)
{
    Hlsl *h = pp->h;
    const char *p = h->files[file].data, *end = p + h->files[file].size, *line_start = p;
    uint32_t line = 1;
    HlToken head = {}, *tail = &head;
    uint8_t flags = TF_BOL;
    while (p < end) {
        char c = *p;
        if (c == '\n') {
            p++;
            line++;
            line_start = p;
            flags = TF_BOL;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\v' || c == '\f') {
            p++;
            flags |= TF_SPACE;
            continue;
        }
        if (c == '/' && p + 1 < end && p[1] == '/') {
            while (p < end && *p != '\n')
                p++;
            flags |= TF_SPACE;
            continue;
        }
        if (c == '/' && p + 1 < end && p[1] == '*') {
            HlLoc loc = {file, line, (uint32_t)(p - line_start) + 1};
            p += 2;
            while (p < end && !(p[0] == '*' && p + 1 < end && p[1] == '/')) {
                if (*p == '\n')
                    line++, line_start = p + 1;
                p++;
            }
            if (p >= end)
                hl_error(h, loc, 1004, "unexpected end of file in comment");
            else
                p += 2;
            flags |= TF_SPACE;
            continue;
        }
        HlLoc loc = {file, line, (uint32_t)(p - line_start) + 1};
        const char *start = p;
        HlToken *t;
        if (is_ident_start(c)) {
            while (p < end && is_ident_char(*p))
                p++;
            t = new_token(h, TK_IDENT, start, (uint32_t)(p - start), loc);
            t->ident = hl_intern(h, start, (size_t)(p - start));
        } else if (is_digit(c) || (c == '.' && p + 1 < end && is_digit(p[1]))) {
            p++;
            while (p < end) {
                if ((*p == 'e' || *p == 'E' || *p == 'p' || *p == 'P') && p + 1 < end && (p[1] == '+' || p[1] == '-'))
                    p += 2;
                else if (is_ident_char(*p) || *p == '.')
                    p++;
                /* 1.#INF and 1.#QNAN, as fxc prints them. */
                else if (*p == '#' && p > start && p[-1] == '.' && p + 1 < end && is_ident_start(p[1]))
                    p++;
                else
                    break;
            }
            t = new_token(h, TK_NUMBER, start, (uint32_t)(p - start), loc);
        } else if (c == '"' || c == '\'') {
            p++;
            while (p < end && *p != c && *p != '\n') {
                if (*p == '\\' && p + 1 < end && p[1] != '\n')
                    p++;
                p++;
            }
            if (p >= end || *p != c)
                hl_error(h, loc, 1005, c == '"' ? "newline in string constant" : "newline in character constant");
            else
                p++;
            t = new_token(h, c == '"' ? TK_STRING : TK_CHAR, start, (uint32_t)(p - start), loc);
        } else {
            uint16_t punct = 0;
            uint32_t length = 1;
            for (size_t i = 0; i < HL_ARRAY_SIZE(puncts); i++) {
                size_t n = __builtin_strlen(puncts[i].text);
                if ((size_t)(end - p) >= n && !__builtin_memcmp(p, puncts[i].text, n)) {
                    punct = puncts[i].punct;
                    length = (uint32_t)n;
                    break;
                }
            }
            if (!punct)
                punct = (uint8_t)c;
            p += length;
            t = new_token(h, TK_PUNCT, start, length, loc);
            t->punct = punct;
        }
        t->flags = flags;
        flags = 0;
        tail = tail->next = t;
    }
    HlToken *eof = new_token(h, TK_EOF, end, 0, (HlLoc){file, line, 1});
    eof->flags = TF_BOL;
    tail->next = eof;
    return head.next;
}

const char *hl_token_text(Hlsl *h, const HlToken *t)
{
    if (t->kind == TK_EOF || t->kind == TK_FILE_END)
        return "<EOF>";
    return hl_strndup(h, t->text, t->length);
}

/* ------------------------------------------------------------------ macros */

static Macro **macro_slot(Pp *pp, const char *name)
{
    uint32_t mask = pp->macro_capacity - 1;
    uint32_t at = (uint32_t)(((uintptr_t)name >> 4) * 2654435761u) & mask;
    while (pp->macros[at] && pp->macros[at]->name != name)
        at = (at + 1) & mask;
    return &pp->macros[at];
}

static Macro *find_macro(Pp *pp, const HlToken *t)
{
    if (t->kind != TK_IDENT || !pp->macro_capacity)
        return nullptr;
    Macro *m = *macro_slot(pp, t->ident);
    return m && !m->deleted ? m : nullptr;
}

static Macro *add_macro(Pp *pp, const char *name)
{
    if ((pp->macro_count + 1) * 2 > pp->macro_capacity) {
        Macro **old = pp->macros;
        uint32_t old_capacity = pp->macro_capacity;
        pp->macro_capacity = old_capacity ? old_capacity * 2 : 256;
        pp->macros = hl_alloc(pp->h, pp->macro_capacity * sizeof(Macro *));
        for (uint32_t i = 0; i < old_capacity; i++)
            if (old[i])
                *macro_slot(pp, old[i]->name) = old[i];
    }
    Macro **slot = macro_slot(pp, name);
    if (!*slot) {
        *slot = hl_alloc(pp->h, sizeof(Macro));
        pp->macro_count++;
    } else {
        __builtin_memset(*slot, 0, sizeof(Macro));
    }
    (*slot)->name = name;
    return *slot;
}

static bool hideset_contains(const HlHideset *hs, const char *name)
{
    for (; hs; hs = hs->next)
        if (hs->name == name)
            return true;
    return false;
}

static HlHideset *hideset_add(Hlsl *h, HlHideset *hs, const char *name)
{
    if (hideset_contains(hs, name))
        return hs;
    HlHideset *n = hl_alloc(h, sizeof(HlHideset));
    n->name = name;
    n->next = hs;
    return n;
}

static HlHideset *hideset_union(Hlsl *h, HlHideset *a, HlHideset *b)
{
    for (; b; b = b->next)
        a = hideset_add(h, a, b->name);
    return a;
}

static HlHideset *hideset_intersection(Hlsl *h, HlHideset *a, HlHideset *b)
{
    HlHideset *out = nullptr;
    for (; a; a = a->next)
        if (hideset_contains(b, a->name))
            out = hideset_add(h, out, a->name);
    return out;
}

static HlToken *copy_token(Hlsl *h, const HlToken *t)
{
    HlToken *c = hl_alloc(h, sizeof(HlToken));
    *c = *t;
    c->next = nullptr;
    return c;
}

/* ------------------------------------------------------------------ line helpers */

static HlToken *skip_line(Pp *pp, HlToken *t)
{
    while (!(t->flags & TF_BOL) && t->kind != TK_EOF)
        t = t->next;
    return t;
}

/* the rest of the directive line, ended by an eof token. */
static HlToken *copy_line(Pp *pp, HlToken **rest, HlToken *t)
{
    HlToken head = {}, *tail = &head;
    for (; !(t->flags & TF_BOL) && t->kind != TK_EOF; t = t->next)
        tail = tail->next = copy_token(pp->h, t);
    HlToken *eof = new_token(pp->h, TK_EOF, t->text, 0, t->loc);
    eof->flags = TF_BOL;
    tail->next = eof;
    *rest = t;
    return head.next;
}

static void extra_tokens(Pp *pp, HlToken **rest, HlToken *t, const char *directive)
{
    if (!(t->flags & TF_BOL) && t->kind != TK_EOF)
        hl_warning(pp->h, t->loc, 1020, "unexpected tokens following preprocessor directive - expected a newline");
    *rest = skip_line(pp, t);
}

/* ------------------------------------------------------------------ #define */

static HlToken *read_body(Pp *pp, HlToken **rest, HlToken *t) { return copy_line(pp, rest, t); }

static void read_define(Pp *pp, HlToken **rest, HlToken *t)
{
    Hlsl *h = pp->h;
    if (t->kind != TK_IDENT || (t->flags & TF_BOL)) {
        hl_error(h, t->loc, 1012, "#define: expected an identifier");
        *rest = skip_line(pp, t);
        return;
    }
    const char *name = t->ident;
    HlLoc loc = t->loc;
    if (name == pp->k_defined) {
        hl_error(h, t->loc, 1011, "cannot #define 'defined'");
        *rest = skip_line(pp, t);
        return;
    }
    t = t->next;
    bool function_like = false, variadic = false;
    const char **params = nullptr;
    uint32_t param_count = 0, param_capacity = 0;
    if (hl_is_punct(t, '(') && !(t->flags & (TF_SPACE | TF_BOL))) {
        function_like = true;
        t = t->next;
        if (!hl_is_punct(t, ')')) {
            for (;;) {
                if (t->flags & TF_BOL) {
                    hl_error(h, t->loc, 1009, "missing ')' in macro parameter list");
                    *rest = t;
                    return;
                }
                if (hl_is_punct(t, P_ELLIPSIS)) {
                    variadic = true;
                    t = t->next;
                    break;
                }
                if (t->kind != TK_IDENT) {
                    hl_error(h, t->loc, 1010, "unexpected token '%s' in macro parameter list", hl_token_text(h, t));
                    *rest = skip_line(pp, t);
                    return;
                }
                HL_PUSH(h, params, param_count, param_capacity, t->ident);
                t = t->next;
                if (!hl_is_punct(t, ','))
                    break;
                t = t->next;
            }
        }
        if (!hl_is_punct(t, ')') || (t->flags & TF_BOL)) {
            hl_error(h, t->loc, 1009, "missing ')' in macro parameter list");
            *rest = skip_line(pp, t);
            return;
        }
        t = t->next;
    }
    HlToken *body = read_body(pp, rest, t);
    if (body->kind != TK_EOF && body->kind == TK_PUNCT && body->punct == P_HASHHASH)
        hl_error(h, body->loc, 1007, "'##' cannot occur at the beginning of a macro definition");
    for (HlToken *b = body; b->kind != TK_EOF; b = b->next)
        if (b->next->kind == TK_EOF && hl_is_punct(b, P_HASHHASH))
            hl_error(h, b->loc, 1008, "'##' cannot occur at the end of a macro definition");
    Macro *old = pp->macro_capacity ? *macro_slot(pp, name) : nullptr;
    if (old && !old->deleted && !old->builtin) {
        /* a different redefinition warns, as fxc does. */
        bool same = old->function_like == function_like && old->param_count == param_count;
        HlToken *a = old->body, *b = body;
        for (; same && a->kind != TK_EOF && b->kind != TK_EOF; a = a->next, b = b->next)
            same = a->length == b->length && !__builtin_memcmp(a->text, b->text, a->length) &&
                   (a == old->body || (a->flags & TF_SPACE) == (b->flags & TF_SPACE));
        if (!same || a->kind != b->kind)
            hl_warning(h, loc, 1519, "'%s' : macro redefinition", name);
    }
    Macro *m = add_macro(pp, name);
    m->function_like = function_like;
    m->variadic = variadic;
    m->params = params;
    m->param_count = param_count;
    m->body = body;
    m->loc = loc;
}

/* ------------------------------------------------------------------ expansion */

typedef struct MacroArg {
    HlToken *tokens;
} MacroArg;

static HlToken *preprocess_tokens(Pp *pp, HlToken *t, bool directives);

static HlToken *stringize(Pp *pp, HlToken *arg, HlToken *at)
{
    Hlsl *h = pp->h;
    HlBuf b = {};
    hl_buf_putc(h, &b, '"');
    for (HlToken *t = arg; t->kind != TK_EOF; t = t->next) {
        if (t != arg && (t->flags & (TF_SPACE | TF_BOL)))
            hl_buf_putc(h, &b, ' ');
        if (t->kind == TK_STRING || t->kind == TK_CHAR) {
            for (uint32_t i = 0; i < t->length; i++) {
                if (t->text[i] == '"' || t->text[i] == '\\')
                    hl_buf_putc(h, &b, '\\');
                hl_buf_putc(h, &b, t->text[i]);
            }
        } else {
            hl_buf_append(h, &b, t->text, t->length);
        }
    }
    hl_buf_putc(h, &b, '"');
    HlToken *s = new_token(h, TK_STRING, b.data, (uint32_t)b.length, at->loc);
    return s;
}

/* lexes the text of two pasted tokens into one. */
static HlToken *paste(Pp *pp, HlToken *a, HlToken *b)
{
    Hlsl *h = pp->h;
    if (a->kind == TK_PLACEMARKER)
        return copy_token(h, b);
    if (b->kind == TK_PLACEMARKER)
        return copy_token(h, a);
    char *text = hl_alloc(h, a->length + b->length + 2);
    __builtin_memcpy(text, a->text, a->length);
    __builtin_memcpy(text + a->length, b->text, b->length);
    text[a->length + b->length] = '\n';
    HlFile *f;
    uint32_t file = add_file(h, nullptr, text, a->length + b->length, nullptr, a->loc.file);
    f = &h->files[file];
    f->name = hl_file_name(h, a->loc.file);
    HlToken *t = tokenize(pp, file);
    if (t->kind == TK_EOF || t->next->kind != TK_EOF) {
        hl_error(h, a->loc, 1013, "pasting \"%s\" and \"%s\" does not give a valid preprocessing token",
                 hl_token_text(h, a), hl_token_text(h, b));
        return copy_token(h, a);
    }
    t->loc = a->loc;
    t->flags = a->flags;
    t->next = nullptr;
    return t;
}

static int param_index(const Macro *m, const HlToken *t)
{
    if (t->kind != TK_IDENT)
        return -1;
    for (uint32_t i = 0; i < m->param_count; i++)
        if (m->params[i] == t->ident)
            return (int)i;
    if (m->variadic && hl_streq(t->ident, "__VA_ARGS__"))
        return (int)m->param_count;
    return -1;
}

static HlToken *copy_list(Hlsl *h, HlToken *t, HlToken **tail_out, HlLoc loc, uint8_t first_flags)
{
    HlToken head = {}, *tail = &head;
    bool first = true;
    for (; t->kind != TK_EOF; t = t->next) {
        HlToken *c = copy_token(h, t);
        c->loc = loc;
        if (first) {
            c->flags = (uint8_t)((c->flags & ~(TF_SPACE | TF_BOL)) | first_flags);
            first = false;
        }
        tail = tail->next = c;
    }
    *tail_out = tail;
    return head.next;
}

/* the body with the arguments in: stringized, pasted or fully expanded. */
static HlToken *substitute(Pp *pp, Macro *m, MacroArg *args, HlToken *at)
{
    Hlsl *h = pp->h;
    HlToken head = {}, *tail = &head;
    uint32_t arg_count = m->param_count + (m->variadic ? 1 : 0);
    HlToken **expanded = hl_alloc(h, (arg_count + 1) * sizeof(HlToken *));
    for (HlToken *t = m->body; t->kind != TK_EOF; t = t->next) {
        if (hl_is_punct(t, '#') && m->function_like) {
            int i = param_index(m, t->next);
            if (i < 0) {
                hl_error(h, t->loc, 1015, "'#' is not followed by a macro parameter");
                continue;
            }
            HlToken *s = stringize(pp, args[i].tokens, at);
            s->flags = t->flags;
            tail = tail->next = s;
            t = t->next;
            continue;
        }
        if (hl_is_punct(t, P_HASHHASH)) {
            HlToken *rhs = t->next;
            if (rhs->kind == TK_EOF || tail == &head)
                continue;
            int i = param_index(m, rhs);
            HlToken *right;
            HlToken *rest = nullptr;
            if (i >= 0) {
                right = args[i].tokens;
                if (right->kind == TK_EOF) {
                    t = rhs;
                    continue;
                }
                rest = right->next;
            } else {
                right = rhs;
            }
            HlToken *joined = paste(pp, tail, right);
            joined->loc = at->loc;
            /* replace the tail with the pasted token. */
            HlToken *prev = &head;
            while (prev->next != tail)
                prev = prev->next;
            prev->next = joined;
            tail = joined;
            if (i >= 0) {
                for (; rest && rest->kind != TK_EOF; rest = rest->next) {
                    HlToken *c = copy_token(h, rest);
                    c->loc = at->loc;
                    tail = tail->next = c;
                }
            }
            t = rhs;
            continue;
        }
        int i = m->function_like ? param_index(m, t) : -1;
        if (i >= 0) {
            HlToken *arg;
            if (hl_is_punct(t->next, P_HASHHASH)) {
                arg = args[i].tokens;
                if (arg->kind == TK_EOF) {
                    /* an empty left operand of ## keeps the right one alone. */
                    HlToken *pm = new_token(h, TK_PLACEMARKER, "", 0, at->loc);
                    tail = tail->next = pm;
                    continue;
                }
            } else {
                if (!expanded[i])
                    expanded[i] = preprocess_tokens(pp, args[i].tokens, false);
                arg = expanded[i];
            }
            HlToken *last;
            HlToken *list = copy_list(h, arg, &last, at->loc, t->flags & TF_SPACE);
            if (list) {
                tail->next = list;
                tail = last;
            }
            continue;
        }
        HlToken *c = copy_token(h, t);
        c->loc = at->loc;
        tail = tail->next = c;
    }
    /* drop placemarkers. */
    HlToken *prev = &head;
    for (HlToken *t = head.next; t; t = t->next) {
        if (t->kind == TK_PLACEMARKER)
            prev->next = t->next;
        else
            prev = t;
    }
    tail = prev;
    tail->next = nullptr;
    return head.next;
}

/* reads the arguments of a call; NULL when the call is not complete. */
static MacroArg *read_args(Pp *pp, Macro *m, HlToken **rest, HlToken *t, HlToken **rparen)
{
    Hlsl *h = pp->h;
    uint32_t count = m->param_count + (m->variadic ? 1 : 0);
    MacroArg *args = hl_alloc(h, (count + 1) * sizeof(MacroArg));
    uint32_t index = 0;
    bool first_empty = false;
    HlLoc loc = t->loc;
    t = t->next;
    for (;;) {
        HlToken head = {}, *tail = &head;
        int level = 0;
        for (;; t = t->next) {
            if (t->kind == TK_EOF || t->kind == TK_FILE_END) {
                hl_error(h, loc, 1004, "unexpected end of file in macro expansion");
                *rest = t;
                return nullptr;
            }
            if (level == 0 && (hl_is_punct(t, ')') ||
                               (hl_is_punct(t, ',') && !(m->variadic && index == m->param_count))))
                break;
            if (hl_is_punct(t, '('))
                level++;
            else if (hl_is_punct(t, ')'))
                level--;
            HlToken *c = copy_token(h, t);
            if (tail == &head)
                c->flags &= (uint8_t)~(TF_SPACE | TF_BOL);
            else if (c->flags & TF_BOL)
                c->flags = (uint8_t)((c->flags & ~TF_BOL) | TF_SPACE);
            tail = tail->next = c;
        }
        HlToken *eof = new_token(h, TK_EOF, t->text, 0, t->loc);
        if (!index)
            first_empty = tail == &head;
        tail->next = eof;
        if (index < count)
            args[index].tokens = head.next;
        index++;
        if (hl_is_punct(t, ')'))
            break;
        t = t->next;
    }
    /* m() is one empty argument, which a macro of no parameters takes. */
    if (index == 1 && count == 0 && first_empty)
        index = 0;
    if (index < count && !(m->variadic && index == m->param_count)) {
        hl_warning(h, loc, 1001, "not enough actual parameters for macro '%s'", m->name);
        for (; index < count; index++)
            args[index].tokens = new_token(h, TK_EOF, "", 0, loc);
    } else if (index > count) {
        hl_warning(h, loc, 1002, "too many actual parameters for macro '%s'", m->name);
    }
    for (uint32_t i = 0; i < count; i++)
        if (!args[i].tokens)
            args[i].tokens = new_token(h, TK_EOF, "", 0, loc);
    *rparen = t;
    *rest = t->next;
    return args;
}

/* expands the macro t names into the list; false when t is no expansion. */
static bool expand_macro(Pp *pp, HlToken **rest, HlToken *t)
{
    Hlsl *h = pp->h;
    if (hideset_contains(t->hide, t->ident))
        return false;
    Macro *m = find_macro(pp, t);
    if (!m)
        return false;
    if (m->builtin) {
        HlToken *c = copy_token(h, t);
        if (m->builtin == BUILTIN_LINE) {
            char *text = hl_sprintf(h, "%u", t->loc.line);
            c->kind = TK_NUMBER;
            c->text = text;
            c->length = (uint32_t)__builtin_strlen(text);
        } else {
            HlBuf b = {};
            hl_buf_putc(h, &b, '"');
            for (const char *s = hl_file_name(h, t->loc.file); *s; s++) {
                if (*s == '\\' || *s == '"')
                    hl_buf_putc(h, &b, '\\');
                hl_buf_putc(h, &b, *s);
            }
            hl_buf_putc(h, &b, '"');
            c->kind = TK_STRING;
            c->text = b.data;
            c->length = (uint32_t)b.length;
        }
        c->ident = nullptr;
        c->next = t->next;
        *rest = c;
        return true;
    }
    if (!m->function_like) {
        HlHideset *hs = hideset_add(h, t->hide, m->name);
        HlToken *body = substitute(pp, m, nullptr, t);
        HlToken *tail = nullptr;
        for (HlToken *b = body; b; b = b->next) {
            b->hide = hideset_union(h, b->hide, hs);
            tail = b;
        }
        if (!body) {
            /* an empty expansion passes its spacing to what follows. */
            HlToken *next = copy_token(h, t->next);
            next->next = t->next->next;
            next->flags |= (uint8_t)(t->flags & (TF_SPACE | TF_BOL));
            if (t->next->kind == TK_EOF)
                next->flags = t->next->flags;
            *rest = next;
            return true;
        }
        body->flags = (uint8_t)((body->flags & ~(TF_SPACE | TF_BOL)) | (t->flags & (TF_SPACE | TF_BOL)));
        tail->next = t->next;
        *rest = body;
        return true;
    }
    if (!hl_is_punct(t->next, '('))
        return false;
    HlToken *rparen = nullptr;
    MacroArg *args = read_args(pp, m, rest, t->next, &rparen);
    if (!args)
        return true;
    HlHideset *hs = hideset_add(h, hideset_intersection(h, t->hide, rparen->hide), m->name);
    HlToken *body = substitute(pp, m, args, t);
    HlToken *tail = nullptr;
    for (HlToken *b = body; b; b = b->next) {
        b->hide = hideset_union(h, b->hide, hs);
        tail = b;
    }
    if (!body)
        return true;
    body->flags = (uint8_t)((body->flags & ~(TF_SPACE | TF_BOL)) | (t->flags & (TF_SPACE | TF_BOL)));
    tail->next = *rest;
    *rest = body;
    return true;
}

/* ------------------------------------------------------------------ #if */

typedef struct Eval {
    Pp *pp;
    HlToken *t;
    bool failed;
} Eval;

static int64_t eval_conditional(Eval *e);

static bool parse_int(const HlToken *t, int64_t *out)
{
    const char *p = t->text, *end = t->text + t->length;
    uint64_t v = 0;
    unsigned base = 10;
    if (end - p > 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X'))
        base = 16, p += 2;
    else if (end - p > 1 && p[0] == '0')
        base = 8, p++;
    bool any = base != 10 || p < end;
    for (; p < end; p++) {
        unsigned d;
        char c = *p;
        if (c >= '0' && c <= '9')
            d = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f')
            d = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            d = (unsigned)(c - 'A' + 10);
        else
            break;
        if (d >= base)
            return false;
        v = v * base + d;
    }
    while (p < end && (*p == 'u' || *p == 'U' || *p == 'l' || *p == 'L'))
        p++;
    *out = (int64_t)v;
    return any && p == end;
}

static int64_t eval_primary(Eval *e)
{
    Hlsl *h = e->pp->h;
    HlToken *t = e->t;
    if (hl_is_punct(t, '(')) {
        e->t = t->next;
        int64_t v = eval_conditional(e);
        if (!hl_is_punct(e->t, ')')) {
            if (!e->failed)
                hl_error(h, e->t->loc, 1016, "expected ')' in preprocessor expression");
            e->failed = true;
        } else {
            e->t = e->t->next;
        }
        return v;
    }
    if (hl_is_punct(t, '-') || hl_is_punct(t, '+') || hl_is_punct(t, '!') || hl_is_punct(t, '~')) {
        e->t = t->next;
        int64_t v = eval_primary(e);
        switch (t->punct) {
        case '-': return (int64_t)(0 - (uint64_t)v);
        case '!': return !v;
        case '~': return ~v;
        default: return v;
        }
    }
    if (t->kind == TK_NUMBER) {
        int64_t v;
        if (!parse_int(t, &v)) {
            if (!e->failed)
                hl_error(h, t->loc, 1017, "invalid integer constant expression");
            e->failed = true;
            v = 0;
        }
        e->t = t->next;
        return v;
    }
    if (t->kind == TK_CHAR) {
        e->t = t->next;
        return t->length > 2 ? (uint8_t)t->text[1] : 0;
    }
    if (t->kind == TK_IDENT) {
        e->t = t->next;
        if (hl_streq(t->ident, "true"))
            return 1;
        return 0;
    }
    if (!e->failed)
        hl_error(h, t->loc, 1017, "invalid integer constant expression");
    e->failed = true;
    if (t->kind != TK_EOF)
        e->t = t->next;
    return 0;
}

static int binary_precedence(const HlToken *t)
{
    if (t->kind != TK_PUNCT)
        return 0;
    switch (t->punct) {
    case '*': case '/': case '%': return 10;
    case '+': case '-': return 9;
    case P_SHL: case P_SHR: return 8;
    case '<': case '>': case P_LE: case P_GE: return 7;
    case P_EQ: case P_NE: return 6;
    case '&': return 5;
    case '^': return 4;
    case '|': return 3;
    case P_ANDAND: return 2;
    case P_OROR: return 1;
    default: return 0;
    }
}

static int64_t eval_binary(Eval *e, int min_precedence)
{
    int64_t lhs = eval_primary(e);
    for (;;) {
        HlToken *op = e->t;
        int prec = binary_precedence(op);
        if (!prec || prec < min_precedence)
            return lhs;
        e->t = op->next;
        int64_t rhs = eval_binary(e, prec + 1);
        switch (op->punct) {
        case '*': lhs = (int64_t)((uint64_t)lhs * (uint64_t)rhs); break;
        case '/':
        case '%':
            if (!rhs) {
                if (!e->failed)
                    hl_error(e->pp->h, op->loc, 1018, "division by zero in preprocessor expression");
                e->failed = true;
                lhs = 0;
            } else if (rhs == -1) {
                lhs = op->punct == '/' ? (int64_t)(0 - (uint64_t)lhs) : 0;
            } else {
                lhs = op->punct == '/' ? lhs / rhs : lhs % rhs;
            }
            break;
        case '+': lhs = (int64_t)((uint64_t)lhs + (uint64_t)rhs); break;
        case '-': lhs = (int64_t)((uint64_t)lhs - (uint64_t)rhs); break;
        case P_SHL: lhs = (int64_t)((uint64_t)lhs << (rhs & 63)); break;
        case P_SHR: lhs = lhs >> (rhs & 63); break;
        case '<': lhs = lhs < rhs; break;
        case '>': lhs = lhs > rhs; break;
        case P_LE: lhs = lhs <= rhs; break;
        case P_GE: lhs = lhs >= rhs; break;
        case P_EQ: lhs = lhs == rhs; break;
        case P_NE: lhs = lhs != rhs; break;
        case '&': lhs = lhs & rhs; break;
        case '^': lhs = lhs ^ rhs; break;
        case '|': lhs = lhs | rhs; break;
        case P_ANDAND: lhs = lhs && rhs; break;
        case P_OROR: lhs = lhs || rhs; break;
        }
    }
}

static int64_t eval_conditional(Eval *e)
{
    int64_t c = eval_binary(e, 1);
    if (!hl_is_punct(e->t, '?'))
        return c;
    e->t = e->t->next;
    int64_t a = eval_conditional(e);
    if (!hl_is_punct(e->t, ':')) {
        if (!e->failed)
            hl_error(e->pp->h, e->t->loc, 1016, "expected ':' in preprocessor expression");
        e->failed = true;
        return 0;
    }
    e->t = e->t->next;
    int64_t b = eval_conditional(e);
    return c ? a : b;
}

static bool read_condition(Pp *pp, HlToken **rest, HlToken *t)
{
    Hlsl *h = pp->h;
    HlLoc loc = t->loc;
    HlToken *line = copy_line(pp, rest, t);
    /* defined X and defined(X) before expansion. */
    HlToken head = {}, *tail = &head;
    for (HlToken *u = line; u->kind != TK_EOF; u = u->next) {
        if (u->kind == TK_IDENT && u->ident == pp->k_defined) {
            HlToken *start = u;
            bool paren = hl_is_punct(u->next, '(');
            if (paren)
                u = u->next;
            u = u->next;
            if (u->kind != TK_IDENT) {
                hl_error(h, start->loc, 1012, "expected an identifier after 'defined'");
                return false;
            }
            bool defined = find_macro(pp, u) != nullptr;
            if (paren) {
                if (!hl_is_punct(u->next, ')')) {
                    hl_error(h, start->loc, 1016, "expected ')' after 'defined'");
                    return false;
                }
                u = u->next;
            }
            HlToken *n = copy_token(h, start);
            n->kind = TK_NUMBER;
            n->text = defined ? "1" : "0";
            n->length = 1;
            tail = tail->next = n;
            continue;
        }
        tail = tail->next = copy_token(h, u);
    }
    HlToken *eof = new_token(h, TK_EOF, "", 0, loc);
    tail->next = eof;
    HlToken *expanded = preprocess_tokens(pp, head.next ? head.next : eof, false);
    if (expanded->kind == TK_EOF) {
        hl_error(h, loc, 1017, "#if: expected an integer constant expression");
        return false;
    }
    Eval e = {pp, expanded, false};
    int64_t v = eval_conditional(&e);
    if (!e.failed && e.t->kind != TK_EOF)
        hl_error(h, e.t->loc, 1017, "unexpected token '%s' in preprocessor expression", hl_token_text(h, e.t));
    return v != 0;
}

/* skips a group to the #elif, #else or #endif of its level, which it returns. */
static HlToken *skip_group(Pp *pp, HlToken *t)
{
    int level = 0;
    while (t->kind != TK_EOF && t->kind != TK_FILE_END) {
        if ((t->flags & TF_BOL) && hl_is_punct(t, '#') && t->next->kind == TK_IDENT &&
            !(t->next->flags & TF_BOL)) {
            const char *d = t->next->ident;
            if (d == pp->k_if || d == pp->k_ifdef || d == pp->k_ifndef) {
                level++;
            } else if (d == pp->k_endif) {
                if (!level)
                    return t;
                level--;
            } else if ((d == pp->k_elif || d == pp->k_else) && !level) {
                return t;
            }
            t = skip_line(pp, t->next);
            continue;
        }
        t = t->next;
    }
    return t;
}

/* ------------------------------------------------------------------ #include */

static HlToken *include_file(Pp *pp, HlToken *at, HlToken *rest, const char *name, bool system)
{
    Hlsl *h = pp->h;
    if (pp->depth >= MAX_INCLUDE_DEPTH) {
        hl_error(h, at->loc, 1506, "#include nested too deeply");
        return rest;
    }
    if (!h->include || !h->include->open) {
        hl_error(h, at->loc, 1507, "failed to open source file: '%s'", name);
        return rest;
    }
    const HlFile *current = &h->files[pp->stack[pp->depth - 1]];
    for (uint32_t i = 0; i < h->file_count; i++)
        if (h->files[i].once && h->files[i].name && hl_streq(h->files[i].name, name))
            return rest;
    const void *data = nullptr;
    uint32_t size = 0;
    if (h->include->open(h->include->ctx, system, name, current->opened, &data, &size) || (!data && size)) {
        hl_error(h, at->loc, 1507, "failed to open source file: '%s'", name);
        return rest;
    }
    uint32_t file = add_file(h, hl_strndup(h, name, __builtin_strlen(name)), data ? data : "", data ? size : 0,
                             data, pp->stack[pp->depth - 1]);
    HlToken *tokens = tokenize(pp, file);
    /* the end of the file pops it; the eof token becomes that marker. */
    HlToken *t = tokens;
    while (t->kind != TK_EOF)
        t = t->next;
    t->kind = TK_FILE_END;
    t->next = rest;
    HL_GROW(h, pp->stack, pp->depth, pp->stack_capacity);
    uint32_t capacity = pp->stack_capacity;
    pp->stack_conds = hl_realloc(h, pp->stack_conds, pp->depth * sizeof(uint32_t), capacity * sizeof(uint32_t));
    pp->stack[pp->depth] = file;
    pp->stack_conds[pp->depth] = pp->cond_count;
    pp->depth++;
    return tokens;
}

static void read_include(Pp *pp, HlToken **rest, HlToken *t, HlToken *at)
{
    Hlsl *h = pp->h;
    char *name = nullptr;
    bool system = false;
    if (t->kind == TK_STRING && !(t->flags & TF_BOL)) {
        name = hl_strndup(h, t->text + 1, t->length >= 2 ? t->length - 2 : 0);
        extra_tokens(pp, rest, t->next, "include");
    } else if (hl_is_punct(t, '<') && !(t->flags & TF_BOL)) {
        HlToken *start = t;
        HlBuf b = {};
        for (t = t->next; !(t->flags & TF_BOL) && t->kind != TK_EOF && !hl_is_punct(t, '>'); t = t->next) {
            if (t->flags & TF_SPACE)
                hl_buf_putc(h, &b, ' ');
            hl_buf_append(h, &b, t->text, t->length);
        }
        if (!hl_is_punct(t, '>')) {
            hl_error(h, start->loc, 1503, "#include: expected a file name");
            *rest = skip_line(pp, t);
            return;
        }
        name = b.data ? b.data : hl_strndup(h, "", 0);
        system = true;
        extra_tokens(pp, rest, t->next, "include");
    } else {
        /* a macro may give the name. */
        HlToken *line = copy_line(pp, rest, t);
        HlToken *e = preprocess_tokens(pp, line, false);
        if (e->kind == TK_STRING) {
            name = hl_strndup(h, e->text + 1, e->length >= 2 ? e->length - 2 : 0);
        } else {
            hl_error(h, at->loc, 1503, "#include: expected a file name");
            return;
        }
    }
    *rest = include_file(pp, at, *rest, name, system);
}

/* ------------------------------------------------------------------ #line and #pragma */

static void read_line(Pp *pp, HlToken **rest, HlToken *t)
{
    Hlsl *h = pp->h;
    HlToken *line = copy_line(pp, rest, t);
    HlToken *e = preprocess_tokens(pp, line, false);
    int64_t number;
    if (e->kind != TK_NUMBER || !parse_int(e, &number) || number < 0 || number > 0x7FFFFFFF) {
        hl_error(h, t->loc, 1513, "#line: expected a line number");
        return;
    }
    const char *name = nullptr;
    if (e->next->kind == TK_STRING)
        name = hl_strndup(h, e->next->text + 1, e->next->length >= 2 ? e->next->length - 2 : 0);
    uint32_t file = t->loc.file, target = file;
    if (name) {
        HlFile alias = h->files[file];
        alias.name = name;
        alias.opened = h->files[file].opened;
        HL_PUSH(h, h->files, h->file_count, h->file_capacity, alias);
        target = h->file_count - 1;
        if (pp->depth && pp->stack[pp->depth - 1] == file)
            pp->stack[pp->depth - 1] = target;
    }
    /* the next line of the file is the given number. */
    int64_t delta = number - (int64_t)(t->loc.line + 1);
    for (HlToken *u = *rest; u && u->kind != TK_EOF; u = u->next) {
        if (u->loc.file != file)
            continue;
        u->loc.line = (uint32_t)((int64_t)u->loc.line + delta);
        u->loc.file = target;
        if (u->kind == TK_FILE_END)
            break;
    }
}

static HlToken *read_pragma(Pp *pp, HlToken **rest, HlToken *t, HlToken *hash)
{
    Hlsl *h = pp->h;
    HlToken *line = copy_line(pp, rest, t);
    if (line->kind == TK_IDENT && line->ident == pp->k_once) {
        h->files[pp->stack[pp->depth - 1]].once = true;
        return nullptr;
    }
    if (line->kind == TK_IDENT && line->ident == pp->k_warning) {
        /* warning(disable: 1 2; error: 3; once: 4; default: 5). */
        HlToken *u = line->next;
        if (!hl_is_punct(u, '('))
            goto unknown;
        u = u->next;
        while (u->kind == TK_IDENT) {
            const char *what = u->ident;
            uint8_t mode = hl_streq(what, "disable") ? 1 : hl_streq(what, "error") ? 2 : hl_streq(what, "once") ? 3 : 0;
            if (!mode && !hl_streq(what, "default"))
                break;
            u = u->next;
            if (!hl_is_punct(u, ':'))
                break;
            u = u->next;
            while (u->kind == TK_NUMBER) {
                int64_t code;
                if (parse_int(u, &code))
                    hl_set_warning(h, (uint32_t)code, mode == 3 ? 0 : mode);
                u = u->next;
            }
            if (hl_is_punct(u, ';'))
                u = u->next;
        }
        return nullptr;
    }
    if (line->kind == TK_IDENT && line->ident == pp->k_message)
        return nullptr;
    if (line->kind == TK_IDENT &&
        (line->ident == pp->k_pack_matrix || hl_streq(line->ident, "def") || hl_streq(line->ident, "ruledisable"))) {
        HlToken *p = copy_token(h, hash);
        p->kind = TK_PRAGMA;
        p->next = nullptr;
        p->pragma = line;
        HlBuf b = {};
        for (HlToken *u = line; u->kind != TK_EOF; u = u->next) {
            if (u != line && (u->flags & TF_SPACE))
                hl_buf_putc(h, &b, ' ');
            hl_buf_append(h, &b, u->text, u->length);
        }
        p->text = b.data ? b.data : "";
        p->length = (uint32_t)b.length;
        return p;
    }
unknown:
    if (line->kind != TK_EOF)
        hl_warning(h, t->loc, 3568, "'%s' : unknown pragma ignored", hl_token_text(h, line));
    return nullptr;
}

/* ------------------------------------------------------------------ the main loop */

static void pop_file(Pp *pp, HlToken *end)
{
    Hlsl *h = pp->h;
    if (pp->depth <= 1)
        return;
    pp->depth--;
    if (pp->cond_count > pp->stack_conds[pp->depth]) {
        hl_error(h, pp->conds[pp->cond_count - 1].loc, 1004, "unexpected end of file in conditional");
        pp->cond_count = pp->stack_conds[pp->depth];
    }
}

static HlToken *directive(Pp *pp, HlToken *t, HlToken **out)
{
    Hlsl *h = pp->h;
    HlToken *hash = t;
    t = t->next;
    *out = nullptr;
    if (t->flags & TF_BOL || t->kind == TK_EOF)
        return t;
    if (t->kind == TK_NUMBER) {
        /* # 12 "file", as preprocessors print line markers. */
        HlToken *rest;
        read_line(pp, &rest, t);
        return rest;
    }
    const char *d = t->kind == TK_IDENT ? t->ident : nullptr;
    HlToken *rest = t;
    if (d == pp->k_include) {
        read_include(pp, &rest, t->next, hash);
        return rest;
    }
    if (d == pp->k_define) {
        read_define(pp, &rest, t->next);
        return rest;
    }
    if (d == pp->k_undef) {
        t = t->next;
        if (t->kind != TK_IDENT || (t->flags & TF_BOL)) {
            hl_error(h, t->loc, 1012, "#undef: expected an identifier");
            return skip_line(pp, t);
        }
        Macro *m = find_macro(pp, t);
        if (m && !m->builtin)
            m->deleted = true;
        extra_tokens(pp, &rest, t->next, "undef");
        return rest;
    }
    if (d == pp->k_if || d == pp->k_ifdef || d == pp->k_ifndef) {
        bool value;
        HlLoc loc = t->loc;
        if (d == pp->k_if) {
            value = read_condition(pp, &rest, t->next);
        } else {
            t = t->next;
            if (t->kind != TK_IDENT || (t->flags & TF_BOL)) {
                hl_error(h, t->loc, 1012, "#%s: expected an identifier", d);
                value = false;
                rest = skip_line(pp, t);
            } else {
                value = (find_macro(pp, t) != nullptr) == (d == pp->k_ifdef);
                extra_tokens(pp, &rest, t->next, d);
            }
        }
        HL_PUSH(h, pp->conds, pp->cond_count, pp->cond_capacity, ((Cond){COND_IF, value, loc}));
        return value ? rest : skip_group(pp, rest);
    }
    if (d == pp->k_elif || d == pp->k_else || d == pp->k_endif) {
        uint32_t floor = pp->depth ? pp->stack_conds[pp->depth - 1] : 0;
        if (pp->cond_count <= floor) {
            hl_error(h, t->loc, 1501, "unexpected #%s", d);
            return skip_line(pp, t->next);
        }
        Cond *c = &pp->conds[pp->cond_count - 1];
        if (d == pp->k_endif) {
            pp->cond_count--;
            extra_tokens(pp, &rest, t->next, d);
            return rest;
        }
        if (c->ctx == COND_ELSE) {
            hl_error(h, t->loc, 1501, "unexpected #%s", d);
            return skip_line(pp, t->next);
        }
        if (d == pp->k_else) {
            c->ctx = COND_ELSE;
            extra_tokens(pp, &rest, t->next, d);
            if (c->included)
                return skip_group(pp, rest);
            c->included = true;
            return rest;
        }
        c->ctx = COND_ELIF;
        if (c->included)
            return skip_group(pp, skip_line(pp, t->next));
        bool value = read_condition(pp, &rest, t->next);
        if (!value)
            return skip_group(pp, rest);
        c->included = true;
        return rest;
    }
    if (d == pp->k_line) {
        read_line(pp, &rest, t->next);
        return rest;
    }
    if (d == pp->k_pragma) {
        *out = read_pragma(pp, &rest, t->next, hash);
        return rest;
    }
    if (d == pp->k_error) {
        HlToken *line = copy_line(pp, &rest, t->next);
        HlBuf b = {};
        for (HlToken *u = line; u->kind != TK_EOF; u = u->next) {
            if (u != line && (u->flags & TF_SPACE))
                hl_buf_putc(h, &b, ' ');
            hl_buf_append(h, &b, u->text, u->length);
        }
        hl_error(h, t->loc, 1504, "#error directive: %s", b.data ? b.data : "");
        return rest;
    }
    hl_error(h, t->loc, 1000, "unknown preprocessor directive '%s'", hl_token_text(h, t));
    return skip_line(pp, t);
}

/* expands macros, and with directives, runs them; the list ends with an eof token. */
static HlToken *preprocess_tokens(Pp *pp, HlToken *t, bool directives)
{
    Hlsl *h = pp->h;
    HlToken head = {}, *tail = &head;
    if (pp->nesting >= HL_MAX_EXPANSION_DEPTH) {
        hl_error(h, t->loc, 1009, "macro expansion is nested too deeply");
        return t;
    }
    pp->nesting++;
    while (t->kind != TK_EOF) {
        /* every token any expansion looks at counts, across nested expansions too. */
        if (++pp->steps > 4 * HL_MAX_TOKENS || pp->stopped) {
            if (!pp->stopped)
                hl_error(h, t->loc, 1009, "macro expansion is too large");
            pp->stopped = true;
            while (t->kind != TK_EOF)
                t = t->next;
            break;
        }
        if (t->kind == TK_FILE_END) {
            if (directives)
                pop_file(pp, t);
            t = t->next;
            continue;
        }
        if (directives && (t->flags & TF_BOL) && hl_is_punct(t, '#') && !t->hide) {
            HlToken *pragma;
            t = directive(pp, t, &pragma);
            if (pragma)
                tail = tail->next = pragma;
            continue;
        }
        if (t->kind == TK_IDENT && expand_macro(pp, &t, t))
            continue;
        tail = tail->next = t;
        t = t->next;
    }
    tail->next = t;
    pp->nesting--;
    if (directives && pp->cond_count > (pp->depth ? pp->stack_conds[0] : 0))
        hl_error(h, pp->conds[pp->cond_count - 1].loc, 1004, "unexpected end of file in conditional");
    return head.next ? head.next : t;
}

static void define_option(Pp *pp, const char *name, const char *definition)
{
    Hlsl *h = pp->h;
    /* each option is a #define line of a file of its own. */
    HlBuf b = {};
    hl_buf_printf(h, &b, "%s %s\n", name, definition ? definition : "1");
    uint32_t file = add_file(h, h->source_name, b.data, b.length, nullptr, 0);
    HlToken *rest;
    HlToken *t = tokenize(pp, file);
    if (t->kind != TK_IDENT) {
        hl_error(h, t->loc, 1012, "invalid macro name '%s'", name);
        return;
    }
    t->flags &= (uint8_t)~TF_BOL;
    read_define(pp, &rest, t);
}

static void init_keywords(Pp *pp)
{
    Hlsl *h = pp->h;
    pp->k_defined = hl_intern_z(h, "defined");
    pp->k_va_args = hl_intern_z(h, "__VA_ARGS__");
    pp->k_include = hl_intern_z(h, "include");
    pp->k_define = hl_intern_z(h, "define");
    pp->k_undef = hl_intern_z(h, "undef");
    pp->k_if = hl_intern_z(h, "if");
    pp->k_ifdef = hl_intern_z(h, "ifdef");
    pp->k_ifndef = hl_intern_z(h, "ifndef");
    pp->k_elif = hl_intern_z(h, "elif");
    pp->k_else = hl_intern_z(h, "else");
    pp->k_endif = hl_intern_z(h, "endif");
    pp->k_line = hl_intern_z(h, "line");
    pp->k_pragma = hl_intern_z(h, "pragma");
    pp->k_error = hl_intern_z(h, "error");
    pp->k_once = hl_intern_z(h, "once");
    pp->k_pack_matrix = hl_intern_z(h, "pack_matrix");
    pp->k_warning = hl_intern_z(h, "warning");
    pp->k_message = hl_intern_z(h, "message");
}

int hl_preprocess(Hlsl *h)
{
    const HlslOptions *o = h->options;
    Pp pp_ = {.h = h}, *pp = &pp_;
    init_keywords(pp);
    Macro *m = add_macro(pp, hl_intern_z(h, "__FILE__"));
    m->builtin = BUILTIN_FILE;
    m = add_macro(pp, hl_intern_z(h, "__LINE__"));
    m->builtin = BUILTIN_LINE;
    for (uint32_t i = 0; i < o->macro_count; i++)
        if (o->macros[i].name)
            define_option(pp, o->macros[i].name, o->macros[i].definition);
    uint32_t file = add_file(h, h->source_name, o->source ? o->source : "", o->source ? o->size : 0, nullptr, 0);
    HL_GROW(h, pp->stack, pp->depth, pp->stack_capacity);
    pp->stack_conds = hl_alloc(h, pp->stack_capacity * sizeof(uint32_t));
    pp->stack[0] = file;
    pp->stack_conds[0] = 0;
    pp->depth = 1;
    HlToken *t = preprocess_tokens(pp, tokenize(pp, file), true);
    uint32_t count = 0;
    for (HlToken *u = t; u; u = u->next)
        count++;
    h->tokens = hl_alloc(h, count * sizeof(HlToken *));
    h->token_count = 0;
    for (HlToken *u = t; u; u = u->next) {
        u->hide = nullptr;
        h->tokens[h->token_count++] = u;
    }
    if (h->include && h->include->close)
        for (uint32_t i = 0; i < h->file_count; i++)
            if (h->files[i].opened) {
                const void *data = h->files[i].opened;
                h->include->close(h->include->ctx, data);
                for (uint32_t j = i; j < h->file_count; j++)
                    if (h->files[j].opened == data)
                        h->files[j].opened = nullptr;
            }
    return h->errors ? -1 : 0;
}

/* ------------------------------------------------------------------ text output */

static void print_string(Hlsl *h, HlBuf *out, const char *s)
{
    hl_buf_putc(h, out, '"');
    for (; *s; s++) {
        if (*s == '\\' || *s == '"')
            hl_buf_putc(h, out, '\\');
        hl_buf_putc(h, out, *s);
    }
    hl_buf_putc(h, out, '"');
}

void hl_print_tokens(Hlsl *h, HlBuf *out)
{
    uint32_t file = (uint32_t)-1, line = 0;
    const char *name = nullptr;
    bool at_bol = true;
    for (uint32_t i = 0; i < h->token_count; i++) {
        const HlToken *t = h->tokens[i];
        if (t->kind == TK_EOF)
            break;
        const char *tname = hl_file_name(h, t->loc.file);
        if (t->loc.file != file && (!name || !hl_streq(tname, name))) {
            if (!at_bol)
                hl_buf_putc(h, out, '\n');
            hl_buf_printf(h, out, "#line %u ", t->loc.line);
            print_string(h, out, tname);
            hl_buf_putc(h, out, '\n');
            file = t->loc.file;
            name = tname;
            line = t->loc.line;
            at_bol = true;
        } else if (t->loc.line != line) {
            file = t->loc.file;
            if (t->loc.line > line && t->loc.line - line <= 8) {
                for (; line < t->loc.line; line++)
                    hl_buf_putc(h, out, '\n');
            } else {
                if (!at_bol)
                    hl_buf_putc(h, out, '\n');
                hl_buf_printf(h, out, "#line %u\n", t->loc.line);
            }
            line = t->loc.line;
            at_bol = true;
        }
        if (t->kind == TK_PRAGMA) {
            if (!at_bol)
                hl_buf_putc(h, out, '\n');
            hl_buf_puts(h, out, "#pragma ");
            hl_buf_append(h, out, t->text, t->length);
            hl_buf_putc(h, out, '\n');
            line++;
            at_bol = true;
            continue;
        }
        if (!at_bol && (t->flags & (TF_SPACE | TF_BOL)))
            hl_buf_putc(h, out, ' ');
        hl_buf_append(h, out, t->text, t->length);
        at_bol = false;
    }
    if (!at_bol)
        hl_buf_putc(h, out, '\n');
}
