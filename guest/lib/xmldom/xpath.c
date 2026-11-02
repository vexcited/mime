/*
 * xpath 1.0 over the dom. msxml3's XSLPattern selection language is the same engine with zero based indexes and the
 * dollar operators.
 */
#include <xmldom/xmldom.h>

/* ------------------------------------------------------------------ node sets */

void xs_free(XSet *s)
{
    xd_free(s->v);
    s->v = nullptr;
    s->n = s->cap = 0;
}

BOOL xs_add(XSet *s, XNode *n)
{
    if (s->n == s->cap) {
        UINT cap = s->cap ? s->cap * 2 : 16;
        XNode **v = xd_realloc(s->v, (SIZE_T)cap * sizeof(XNode *));
        if (!v)
            return FALSE;
        s->v = v;
        s->cap = cap;
    }
    s->v[s->n++] = n;
    return TRUE;
}

static XNode *tree_root(XNode *n)
{
    while (n->parent)
        n = n->parent;
    return n;
}

/* numbers a detached tree, whose order the document does not track. */
static void number_tree(XNode *root)
{
    UINT order = 0;
    XNode *n = root;
    for (;;) {
        n->order = ++order;
        for (XNode *a = n->attrs; a; a = a->next)
            a->order = ++order;
        if (n->first) {
            n = n->first;
            continue;
        }
        while (n != root && !n->next)
            n = n->parent;
        if (n == root)
            break;
        n = n->next;
    }
}

static void ensure_order(XNode *root)
{
    if (root->type == XN_DOCUMENT)
        xd_number_order(root->doc);
    else
        number_tree(root);
}

static void merge_sort(XNode **v, XNode **tmp, UINT n)
{
    if (n < 2)
        return;
    if (n <= 8) {
        for (UINT i = 1; i < n; i++) {
            XNode *x = v[i];
            UINT j = i;
            while (j > 0 && v[j - 1]->order > x->order) {
                v[j] = v[j - 1];
                j--;
            }
            v[j] = x;
        }
        return;
    }
    UINT half = n / 2;
    merge_sort(v, tmp, half);
    merge_sort(v + half, tmp, n - half);
    UINT i = 0, j = half, k = 0;
    while (i < half && j < n)
        tmp[k++] = v[i]->order <= v[j]->order ? v[i++] : v[j++];
    while (i < half)
        tmp[k++] = v[i++];
    while (j < n)
        tmp[k++] = v[j++];
    memcpy(v, tmp, (SIZE_T)n * sizeof(XNode *));
}

typedef struct Keyed {
    UINT64 key;
    XNode *n;
} Keyed;

static void keyed_sort(Keyed *v, Keyed *tmp, UINT n)
{
    if (n < 2)
        return;
    UINT mid = n / 2;
    keyed_sort(v, tmp, mid);
    keyed_sort(v + mid, tmp, n - mid);
    UINT i = 0, j = mid, o = 0;
    while (i < mid && j < n)
        tmp[o++] = v[j].key < v[i].key ? v[j++] : v[i++];
    while (i < mid)
        tmp[o++] = v[i++];
    while (j < n)
        tmp[o++] = v[j++];
    memcpy(v, tmp, (SIZE_T)n * sizeof(Keyed));
}

/* a set whose nodes come from several trees: trees in the order their documents were made, then the order in a tree. */
static void sort_mixed(XSet *s)
{
    Keyed *v = xd_alloc_raw((SIZE_T)s->n * 2 * sizeof(Keyed));
    if (!v)
        return;
    for (UINT i = 0; i < s->n; i++) {
        XNode *root = tree_root(s->v[i]);
        ensure_order(root);
        UINT64 tree = ((UINT64)root->doc->seq << 20) | (root->type == XN_DOCUMENT ? 0 : ((UINT64)(ULONG_PTR)root >> 4) & 0xFFFFF);
        v[i].key = (tree << 32) | s->v[i]->order;
        v[i].n = s->v[i];
    }
    keyed_sort(v, v + s->n, s->n);
    UINT k = 0;
    for (UINT i = 0; i < s->n; i++)
        if (!k || s->v[k - 1] != v[i].n)
            s->v[k++] = v[i].n;
    s->n = k;
    xd_free(v);
}

static void sort_unique(XSet *s)
{
    if (s->n < 2)
        return;
    XNode *first_root = tree_root(s->v[0]);
    for (UINT i = 1; i < s->n; i++)
        if (tree_root(s->v[i]) != first_root) {
            sort_mixed(s);
            return;
        }
    ensure_order(tree_root(s->v[0]));
    BOOL sorted = TRUE;
    for (UINT i = 1; i < s->n; i++) {
        if (s->v[i - 1]->order >= s->v[i]->order) {
            sorted = FALSE;
            break;
        }
    }
    if (!sorted) {
        XNode **tmp = xd_alloc_raw((SIZE_T)s->n * sizeof(XNode *));
        if (!tmp)
            return;
        merge_sort(s->v, tmp, s->n);
        xd_free(tmp);
        UINT k = 0;
        for (UINT i = 0; i < s->n; i++)
            if (!k || s->v[k - 1] != s->v[i])
                s->v[k++] = s->v[i];
        s->n = k;
    }
}

/* ------------------------------------------------------------------ expressions */

typedef struct Arena {
    struct Arena *next;
    UINT8 *cur;
    UINT8 *end;
} Arena;

typedef struct Expr Expr;
typedef struct Step Step;

enum { X_NUM, X_STR, X_PATH, X_FILTER, X_FUNC, X_BIN, X_NEG, X_UNION, X_VAR, X_EXT };
enum { OP_OR, OP_AND, OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD };
enum {
    AX_CHILD,
    AX_DESCENDANT,
    AX_PARENT,
    AX_ANCESTOR,
    AX_FOLLOWING_SIBLING,
    AX_PRECEDING_SIBLING,
    AX_FOLLOWING,
    AX_PRECEDING,
    AX_ATTRIBUTE,
    AX_NAMESPACE,
    AX_SELF,
    AX_DESCENDANT_OR_SELF,
    AX_ANCESTOR_OR_SELF,
};
enum { NT_NAME, NT_ANY, NT_TEXT, NT_COMMENT, NT_PI, NT_NODE };
enum {
    F_LAST, F_POSITION, F_COUNT, F_ID, F_LOCAL_NAME, F_NAMESPACE_URI, F_NAME, F_STRING, F_CONCAT, F_STARTS_WITH,
    F_CONTAINS, F_SUBSTRING_BEFORE, F_SUBSTRING_AFTER, F_SUBSTRING, F_STRING_LENGTH, F_NORMALIZE_SPACE,
    F_TRANSLATE, F_BOOLEAN, F_NOT, F_TRUE, F_FALSE, F_LANG, F_NUMBER, F_SUM, F_FLOOR, F_CEILING, F_ROUND,
    F_INDEX, F_COUNT_FUNCS
};

struct Step {
    Step *next;
    int axis;
    int test;
    const WCHAR *local;
    UINT llen;
    const WCHAR *uri;
    BOOL element_default_ns; /* a wildcard that is bound to one namespace. */
    BOOL by_prefix;          /* xslpattern: the name is compared with the prefix of a node, not its namespace. */
    const WCHAR *prefix;
    UINT plen;
    Expr **preds;
    UINT npreds;
    BOOL positional;
    UINT tok_start, tok_end; /* the axis name or the abbreviation, for messages. */
};

struct Expr {
    int kind;
    double num;
    const WCHAR *str;
    UINT slen;
    int op; /* the operator or the function. */
    Expr *a;
    Expr *b;
    Expr **args;
    UINT nargs;
    Expr *primary;
    Expr **preds;
    UINT npreds;
    Step *steps;
    BOOL absolute;
    const WCHAR *uri; /* of a variable or an extension function. */
    const WCHAR *local;
    UINT llen;
    UINT t0, t1; /* where a variable or a function call is in the text. */
};

typedef struct Parser {
    const WCHAR *s;
    const WCHAR *p;
    const WCHAR *e;
    Arena *arena;
    HRESULT error;
    UINT depth;
    const XNs *ns;
    UINT nscount;
    BOOL xpath;
    XDoc *doc;
    const XpHost *host;
    BOOL pattern;
    UINT errpos;
    UINT errstart, errend;
    WCHAR errp1[40], errp2[40];
    BOOL v6;
    UINT prevend; /* where the token before the current one ended. */
    int kind;
    const WCHAR *tstart;
    UINT tlen;
    double tnum;
    UINT colon; /* offset of the colon in a name, 0 for none. */
    BOOL func_follows;
    int prev;
    UINT in_predicate; /* inside the brackets of a predicate. */
} Parser;

enum {
    T_EOF, T_NUM, T_STR, T_NAME, T_STAR_NAME, T_MUL, T_OP, T_SLASH, T_DSLASH, T_DOT, T_DDOT, T_AT, T_COMMA, T_LP,
    T_RP, T_LB, T_RB, T_PIPE, T_PLUS, T_MINUS, T_EQ, T_NE, T_LT, T_LE, T_GT, T_GE, T_AXIS, T_VAR, T_ERROR, T_COLON
};

static void *arena_alloc(Parser *ps, SIZE_T size)
{
    size = (size + 7) & ~(SIZE_T)7;
    Arena *a = ps->arena;
    if (!a || (SIZE_T)(a->end - a->cur) < size) {
        SIZE_T block = size > 8192 ? size : 8192;
        Arena *n = xd_alloc_raw(sizeof(Arena) + block);
        if (!n) {
            ps->error = E_OUTOFMEMORY;
            return nullptr;
        }
        n->next = ps->arena;
        n->cur = (UINT8 *)(n + 1);
        n->end = n->cur + block;
        ps->arena = n;
        a = n;
    }
    void *p = a->cur;
    a->cur += size;
    memset(p, 0, size);
    return p;
}

static void arena_free(Arena *a)
{
    while (a) {
        Arena *next = a->next;
        xd_free(a);
        a = next;
    }
}

static BOOL fail(Parser *ps, HRESULT hr)
{
    if (!ps->error) {
        ps->error = hr;
        ps->errpos = (UINT)(ps->p - ps->s);
        ps->errstart = (UINT)(ps->tstart - ps->s);
        ps->errend = ps->errstart + ps->tlen;
        if (ps->kind == T_VAR && ps->errstart)
            ps->errstart--;
        if (ps->kind == T_STR && ps->errstart) {
            ps->errstart--;
            ps->errend++;
        }
        if (ps->kind == T_EOF)
            ps->errstart = ps->errend = (UINT)(ps->e - ps->s);
    }
    return FALSE;
}

static BOOL fail_range(Parser *ps, HRESULT hr, UINT start, UINT end)
{
    if (!ps->error) {
        ps->error = hr;
        ps->errpos = end;
        ps->errstart = start;
        ps->errend = end;
    }
    return FALSE;
}

/* an expression that cannot be a node set. */
static BOOL expr_not_set(const Expr *x)
{
    switch (x->kind) {
    case X_NUM:
    case X_STR:
    case X_NEG:
    case X_BIN:
        return TRUE;
    case X_FUNC:
        return x->op != F_ID;
    }
    return FALSE;
}

/* the name msxml gives a token in "Expected token ')' found ','". */
static void token_text(Parser *ps, WCHAR *out)
{
    const char *name = nullptr;
    switch (ps->kind) {
    case T_EOF:
        name = ps->v6 ? "EOF" : "eof";
        break;
    case T_NAME:
    case T_STAR_NAME:
        name = ps->kind == T_STAR_NAME && ps->tlen == 1 ? "*" : "NAME";
        break;
    case T_NUM:
        name = "NUMBER";
        break;
    case T_STR:
        name = "STRING";
        break;
    case T_ERROR:
        name = "unknown";
        break;
    case T_OP:
        name = nullptr;
        break;
    }
    UINT n = 0;
    out[n++] = '\'';
    if (name) {
        for (; *name && n < 36; name++)
            out[n++] = (WCHAR)*name;
    } else {
        for (UINT i = 0; i < ps->tlen && n < 36; i++)
            out[n++] = ps->tstart[i];
    }
    out[n++] = '\'';
    out[n] = 0;
}

/* a failure because a token was needed: the message names the one wanted and the one found. */
static BOOL fail_expected(Parser *ps, const char *wanted)
{
    if (!ps->error) {
        fail(ps, XE_XPATH_INCOMPLETE);
        UINT i = 0;
        ps->errp1[i++] = '\'';
        for (; *wanted && i < 36; wanted++)
            ps->errp1[i++] = (WCHAR)*wanted;
        ps->errp1[i++] = '\'';
        ps->errp1[i] = 0;
        token_text(ps, ps->errp2);
    }
    return FALSE;
}

/* ------------------------------------------------------------------ lexer */

static BOOL operand_ended(int kind)
{
    return kind == T_NAME || kind == T_STAR_NAME || kind == T_NUM || kind == T_STR || kind == T_RP || kind == T_RB ||
           kind == T_DOT || kind == T_DDOT || kind == T_VAR;
}

static BOOL name_is(const WCHAR *s, UINT n, const char *lit)
{
    UINT i = 0;
    for (; i < n; i++)
        if (!lit[i] || s[i] != (UCHAR)lit[i])
            return FALSE;
    return lit[i] == 0;
}

static void next_token(Parser *ps)
{
    ps->prevend = (UINT)(ps->p - ps->s);
    ps->prev = ps->kind;
    ps->func_follows = FALSE;
    const WCHAR *p = ps->p, *e = ps->e;
    while (p < e && xd_is_space(*p))
        p++;
    ps->tstart = p;
    ps->tlen = 0;
    ps->colon = 0;
    if (p >= e) {
        ps->kind = T_EOF;
        ps->p = p;
        return;
    }
    WCHAR c = *p;
    int kind = T_ERROR;
    if ((c >= '0' && c <= '9') || (c == '.' && p + 1 < e && p[1] >= '0' && p[1] <= '9')) {
        const WCHAR *q = p;
        while (q < e && *q >= '0' && *q <= '9')
            q++;
        if (q < e && *q == '.') {
            q++;
            while (q < e && *q >= '0' && *q <= '9')
                q++;
        }
        ps->tnum = xd_parse_number(p, (UINT)(q - p));
        ps->tlen = (UINT)(q - p);
        p = q;
        kind = T_NUM;
    } else if (c == '"' || c == '\'') {
        const WCHAR *q = p + 1;
        while (q < e && *q != c)
            q++;
        if (q >= e) {
            ps->kind = T_ERROR;
            if (!ps->error) {
                ps->error = (HRESULT)XE_STRING_NOT_CLOSED;
                ps->errstart = (UINT)(p - ps->s);
                ps->errend = (UINT)(e - ps->s);
                ps->errpos = ps->errend;
            }
            ps->p = e;
            return;
        }
        ps->tstart = p + 1;
        ps->tlen = (UINT)(q - p - 1);
        p = q + 1;
        kind = T_STR;
    } else if (c == '$' && ps->doc && !ps->xpath && p + 1 < e && xd_is_name_start(p[1])) {
        /* xslpattern writes its operators between dollar signs. */
        const WCHAR *q = p + 1;
        while (q < e && xd_is_name_char(*q) && *q != '$')
            q++;
        if (q < e && *q == '$') {
            ps->tstart = p;
            ps->tlen = (UINT)(q - p + 1);
            p = q + 1;
            kind = T_OP;
        } else {
            kind = T_VAR;
            ps->tstart = p + 1;
            ps->tlen = (UINT)(q - p - 1);
            p = q;
        }
    } else if (c == '$') {
        const WCHAR *q = p + 1;
        while (q < e && (xd_is_name_char(*q)))
            q++;
        ps->tstart = p + 1;
        ps->tlen = (UINT)(q - p - 1);
        p = q;
        kind = T_VAR;
    } else if (xd_is_name_start(c) && c != ':') {
        const WCHAR *q = p;
        while (q < e && xd_is_name_char(*q) && *q != ':')
            q++;
        UINT first = (UINT)(q - p);
        if (operand_ended(ps->prev) && (name_is(p, first, "and") || name_is(p, first, "or") ||
                                        name_is(p, first, "div") || name_is(p, first, "mod"))) {
            ps->tlen = first;
            ps->p = q;
            ps->kind = T_OP;
            return;
        }
        if (q + 1 < e && q[0] == ':' && q[1] == ':') {
            ps->tlen = first;
            ps->p = q + 2;
            ps->kind = T_AXIS;
            return;
        }
        if (q + 1 < e && q[0] == ':' && q[1] == '*') {
            ps->colon = first;
            ps->tlen = first + 2;
            ps->p = q + 2;
            ps->kind = T_STAR_NAME;
            return;
        }
        if (q + 1 < e && q[0] == ':' && xd_is_name_start(q[1]) && q[1] != ':') {
            const WCHAR *r = q + 1;
            while (r < e && xd_is_name_char(*r) && *r != ':')
                r++;
            ps->colon = first;
            q = r;
        }
        ps->tlen = (UINT)(q - p);
        p = q;
        kind = T_NAME;
        /* a name followed by an opening parenthesis is a function or a node type. */
        const WCHAR *r = p;
        while (r < e && xd_is_space(*r))
            r++;
        ps->func_follows = r < e && *r == '(';
    } else {
        p++;
        switch (c) {
        case '/':
            if (p < e && *p == '/') {
                p++;
                kind = T_DSLASH;
            } else {
                kind = T_SLASH;
            }
            break;
        case '.':
            if (p < e && *p == '.') {
                p++;
                kind = T_DDOT;
            } else {
                kind = T_DOT;
            }
            break;
        case '@':
            kind = T_AT;
            break;
        case ',':
            kind = T_COMMA;
            break;
        case '(':
            kind = T_LP;
            break;
        case ')':
            kind = T_RP;
            break;
        case '[':
            kind = T_LB;
            break;
        case ']':
            kind = T_RB;
            break;
        case '|':
            kind = T_PIPE;
            break;
        case ':':
            kind = ps->host ? T_COLON : T_ERROR;
            break;
        case '+':
            kind = T_PLUS;
            break;
        case '-':
            kind = T_MINUS;
            break;
        case '=':
            kind = T_EQ;
            break;
        case '!':
            if (p < e && *p == '=') {
                p++;
                kind = T_NE;
            }
            break;
        case '<':
            if (p < e && *p == '=') {
                p++;
                kind = T_LE;
            } else {
                kind = T_LT;
            }
            break;
        case '>':
            if (p < e && *p == '=') {
                p++;
                kind = T_GE;
            } else {
                kind = T_GT;
            }
            break;
        case '*':
            kind = operand_ended(ps->prev) ? T_MUL : T_STAR_NAME;
            ps->tlen = 1;
            break;
        }
    }
    ps->p = p;
    ps->kind = kind;
    if (!ps->tlen && p > ps->tstart && kind != T_STR)
        ps->tlen = (UINT)(p - ps->tstart);
    if (kind == T_ERROR && !ps->error && (ps->v6 || !ps->host))
        fail(ps, (HRESULT)XE_XPATH_CHAR);
}

/* ------------------------------------------------------------------ parser */

static Expr *new_expr(Parser *ps, int kind)
{
    Expr *x = arena_alloc(ps, sizeof(Expr));
    if (x)
        x->kind = kind;
    return x;
}

static Expr *parse_or(Parser *ps);

static BOOL is_op(Parser *ps, const char *name)
{
    return ps->kind == T_OP && name_is(ps->tstart, ps->tlen, name);
}

static const struct {
    const char *name;
    int id;
    int min;
    int max;
} functions[] = {
    {"last", F_LAST, 0, 0},
    {"position", F_POSITION, 0, 0},
    {"count", F_COUNT, 1, 1},
    {"id", F_ID, 1, 1},
    {"local-name", F_LOCAL_NAME, 0, 1},
    {"namespace-uri", F_NAMESPACE_URI, 0, 1},
    {"name", F_NAME, 0, 1},
    {"string", F_STRING, 0, 1},
    {"concat", F_CONCAT, 2, 100},
    {"starts-with", F_STARTS_WITH, 2, 2},
    {"contains", F_CONTAINS, 2, 2},
    {"substring-before", F_SUBSTRING_BEFORE, 2, 2},
    {"substring-after", F_SUBSTRING_AFTER, 2, 2},
    {"substring", F_SUBSTRING, 2, 3},
    {"string-length", F_STRING_LENGTH, 0, 1},
    {"normalize-space", F_NORMALIZE_SPACE, 0, 1},
    {"translate", F_TRANSLATE, 3, 3},
    {"boolean", F_BOOLEAN, 1, 1},
    {"not", F_NOT, 1, 1},
    {"true", F_TRUE, 0, 0},
    {"false", F_FALSE, 0, 0},
    {"lang", F_LANG, 1, 1},
    {"number", F_NUMBER, 0, 1},
    {"sum", F_SUM, 1, 1},
    {"floor", F_FLOOR, 1, 1},
    {"ceiling", F_CEILING, 1, 1},
    {"round", F_ROUND, 1, 1},
    {"index", F_INDEX, 0, 0},
};

/* whether an expression is a number, which as a predicate selects by position. */
static BOOL may_be_number(const Expr *x)
{
    switch (x->kind) {
    case X_NUM:
    case X_NEG:
        return TRUE;
    case X_FUNC:
        switch (x->op) {
        case F_LAST:
        case F_POSITION:
        case F_COUNT:
        case F_SUM:
        case F_NUMBER:
        case F_STRING_LENGTH:
        case F_FLOOR:
        case F_CEILING:
        case F_ROUND:
        case F_INDEX:
            return TRUE;
        }
        return FALSE;
    case X_BIN:
        return x->op >= OP_ADD;
    case X_VAR:
    case X_EXT:
        return TRUE;
    }
    return FALSE;
}

static BOOL uses_position(const Expr *x);

static BOOL steps_use_position(const Step *st)
{
    for (; st; st = st->next)
        for (UINT i = 0; i < st->npreds; i++)
            if (uses_position(st->preds[i]))
                return TRUE;
    return FALSE;
}

static BOOL uses_position(const Expr *x)
{
    if (!x)
        return FALSE;
    if (x->kind == X_FUNC && (x->op == F_POSITION || x->op == F_LAST || x->op == F_INDEX))
        return TRUE;
    for (UINT i = 0; i < x->nargs; i++)
        if (uses_position(x->args[i]))
            return TRUE;
    for (UINT i = 0; i < x->npreds; i++)
        if (uses_position(x->preds[i]))
            return TRUE;
    return uses_position(x->a) || uses_position(x->b) || uses_position(x->primary) || steps_use_position(x->steps);
}

static BOOL positional_expr(const Expr *x)
{
    return may_be_number(x) || uses_position(x);
}

static BOOL parse_predicates(Parser *ps, Expr ***list, UINT *count, BOOL *positional)
{
    Expr *items[64];
    UINT n = 0;
    while (ps->kind == T_LB) {
        next_token(ps);
        if (n == 64)
            return fail(ps, XE_XPATH_INCOMPLETE);
        ps->in_predicate++;
        Expr *x = parse_or(ps);
        ps->in_predicate--;
        if (!x)
            return FALSE;
        if (ps->kind != T_RB)
            return fail_expected(ps, "]");
        next_token(ps);
        items[n++] = x;
        if (positional && positional_expr(x))
            *positional = TRUE;
    }
    if (n) {
        Expr **v = arena_alloc(ps, n * sizeof(Expr *));
        if (!v)
            return FALSE;
        memcpy(v, items, n * sizeof(Expr *));
        *list = v;
    }
    *count = n;
    return TRUE;
}

static const struct {
    const char *name;
    int axis;
} axes[] = {
    {"child", AX_CHILD},
    {"descendant", AX_DESCENDANT},
    {"parent", AX_PARENT},
    {"ancestor", AX_ANCESTOR},
    {"following-sibling", AX_FOLLOWING_SIBLING},
    {"preceding-sibling", AX_PRECEDING_SIBLING},
    {"following", AX_FOLLOWING},
    {"preceding", AX_PRECEDING},
    {"attribute", AX_ATTRIBUTE},
    {"namespace", AX_NAMESPACE},
    {"self", AX_SELF},
    {"descendant-or-self", AX_DESCENDANT_OR_SELF},
    {"ancestor-or-self", AX_ANCESTOR_OR_SELF},
};

static const WCHAR *resolve_prefix(Parser *ps, const WCHAR *prefix, UINT length, BOOL *found)
{
    for (UINT i = 0; i < ps->nscount; i++) {
        UINT l = xd_wlen(ps->ns[i].prefix);
        if (l == length && !memcmp(ps->ns[i].prefix, prefix, (SIZE_T)length * sizeof(WCHAR))) {
            *found = TRUE;
            return ps->ns[i].uri;
        }
    }
    if (length == 3 && !memcmp(prefix, L"xml", 6)) {
        *found = TRUE;
        return L"http://www.w3.org/XML/1998/namespace";
    }
    *found = FALSE;
    return nullptr;
}

static Step *parse_step(Parser *ps)
{
    Step *st = arena_alloc(ps, sizeof(Step));
    if (!st)
        return nullptr;
    st->tok_start = (UINT)(ps->tstart - ps->s);
    st->tok_end = st->tok_start + ps->tlen;
    if (ps->kind == T_DOT) {
        st->axis = AX_SELF;
        st->test = NT_NODE;
        next_token(ps);
        return st;
    }
    if (ps->kind == T_DDOT) {
        st->axis = AX_PARENT;
        st->test = NT_NODE;
        next_token(ps);
        return st;
    }
    st->axis = AX_CHILD;
    if (ps->kind == T_AT) {
        st->axis = AX_ATTRIBUTE;
        next_token(ps);
    } else if (ps->kind == T_AXIS) {
        BOOL found = FALSE;
        for (UINT i = 0; i < sizeof(axes) / sizeof(axes[0]); i++) {
            if (name_is(ps->tstart, ps->tlen, axes[i].name)) {
                st->axis = axes[i].axis;
                found = TRUE;
            }
        }
        if (!found) {
            fail(ps, XE_BAD_AXIS);
            return nullptr;
        }
        next_token(ps);
    }
    if (ps->kind == T_STAR_NAME && !ps->colon) {
        st->test = NT_ANY;
        next_token(ps);
    } else if (ps->kind == T_STAR_NAME) {
        /* a wildcard in a namespace. */
        BOOL found;
        const WCHAR *uri = resolve_prefix(ps, ps->tstart, ps->colon, &found);
        if (!found && ps->xpath) {
            fail(ps, XE_UNDECLARED_PREFIX);
            return nullptr;
        }
        if (!found) {
            st->by_prefix = TRUE;
            st->prefix = ps->tstart;
            st->plen = ps->colon;
        }
        st->test = NT_ANY;
        st->uri = uri;
        st->element_default_ns = found;
        next_token(ps);
    } else if (ps->kind == T_NAME) {
        const WCHAR *name = ps->tstart;
        UINT len = ps->tlen, colon = ps->colon;
        BOOL func = ps->func_follows;
        if (func && !colon && (name_is(name, len, "text") || name_is(name, len, "comment") ||
                               name_is(name, len, "node") || name_is(name, len, "processing-instruction"))) {
            next_token(ps);
            if (ps->kind != T_LP)
                return fail_expected(ps, "("), nullptr;
            next_token(ps);
            if (name_is(name, len, "text"))
                st->test = NT_TEXT;
            else if (name_is(name, len, "comment"))
                st->test = NT_COMMENT;
            else if (name_is(name, len, "node"))
                st->test = NT_NODE;
            else {
                st->test = NT_PI;
                if (ps->kind == T_STR) {
                    st->local = ps->tstart;
                    st->llen = ps->tlen;
                    next_token(ps);
                } else if (ps->kind != T_RP && ps->host) {
                    return fail_expected(ps, "STRING"), nullptr;
                }
            }
            if (ps->kind != T_RP)
                return fail_expected(ps, ")"), nullptr;
            next_token(ps);
        } else if (func) {
            fail(ps, XE_NODETEST);
            return nullptr;
        } else {
            st->test = NT_NAME;
            if (colon) {
                BOOL found;
                st->uri = resolve_prefix(ps, name, colon, &found);
                if (!found && ps->xpath) {
                    fail(ps, XE_UNDECLARED_PREFIX);
                    return nullptr;
                }
                if (!found) {
                    /* xslpattern treats an undeclared prefix as the prefix the document uses. */
                    st->by_prefix = TRUE;
                    st->prefix = name;
                    st->plen = colon;
                }
                st->local = name + colon + 1;
                st->llen = len - colon - 1;
            } else {
                st->local = name;
                st->llen = len;
                if (!ps->xpath) {
                    /* xslpattern: an unprefixed name matches nodes without a prefix, whatever their namespace. */
                    st->by_prefix = TRUE;
                } else {
                    /* the default namespace of the selection, for element names. */
                    BOOL found;
                    const WCHAR *uri = resolve_prefix(ps, L"", 0, &found);
                    if (found && st->axis != AX_ATTRIBUTE) {
                        st->uri = uri;
                    }
                }
            }
            next_token(ps);
        }
    } else {
        fail(ps, XE_NODETEST);
        return nullptr;
    }
    if (!parse_predicates(ps, &st->preds, &st->npreds, &st->positional))
        return nullptr;
    return st;
}

static Step *descendant_or_self_step(Parser *ps)
{
    Step *st = arena_alloc(ps, sizeof(Step));
    if (st) {
        st->axis = AX_DESCENDANT_OR_SELF;
        st->test = NT_NODE;
    }
    return st;
}

/* first is a step already parsed, or null. */
static BOOL parse_steps(Parser *ps, Expr *path, Step *first)
{
    Step **tail = &path->steps;
    while (*tail)
        tail = &(*tail)->next;
    Step *st = first;
    for (;;) {
        if (!st) {
            st = parse_step(ps);
            if (!st)
                return FALSE;
        }
        *tail = st;
        tail = &st->next;
        st = nullptr;
        if (ps->kind == T_SLASH) {
            next_token(ps);
        } else if (ps->kind == T_DSLASH) {
            next_token(ps);
            Step *d = descendant_or_self_step(ps);
            if (!d)
                return FALSE;
            *tail = d;
            tail = &d->next;
        } else {
            break;
        }
    }
    return TRUE;
}

/* "//name" without a position is the descendants named so. */
static void merge_steps(Expr *path)
{
    Step *s = path->steps;
    while (s && s->next) {
        Step *n = s->next;
        if (s->axis == AX_DESCENDANT_OR_SELF && s->test == NT_NODE && !s->npreds && n->axis == AX_CHILD &&
            !n->positional) {
            s->axis = AX_DESCENDANT;
            s->test = n->test;
            s->local = n->local;
            s->llen = n->llen;
            s->uri = n->uri;
            s->element_default_ns = n->element_default_ns;
            s->by_prefix = n->by_prefix;
            s->prefix = n->prefix;
            s->plen = n->plen;
            s->preds = n->preds;
            s->npreds = n->npreds;
            s->positional = n->positional;
            s->next = n->next;
            continue;
        }
        s = s->next;
    }
}

static BOOL starts_step(Parser *ps)
{
    if (ps->kind == T_NAME)
        return !ps->func_follows || name_is(ps->tstart, ps->tlen, "text") || name_is(ps->tstart, ps->tlen, "comment") ||
               name_is(ps->tstart, ps->tlen, "node") || name_is(ps->tstart, ps->tlen, "processing-instruction");
    return ps->kind == T_STAR_NAME || ps->kind == T_AT || ps->kind == T_AXIS || ps->kind == T_DOT ||
           ps->kind == T_DDOT;
}

static Expr *parse_primary(Parser *ps)
{
    Expr *x = nullptr;
    switch (ps->kind) {
    case T_NUM:
        x = new_expr(ps, X_NUM);
        if (x)
            x->num = ps->tnum;
        next_token(ps);
        return x;
    case T_STR:
        x = new_expr(ps, X_STR);
        if (x) {
            x->str = ps->tstart;
            x->slen = ps->tlen;
        }
        next_token(ps);
        return x;
    case T_LP: {
        next_token(ps);
        x = parse_or(ps);
        if (!x)
            return nullptr;
        if (ps->kind != T_RP) {
            fail_expected(ps, ")");
            return nullptr;
        }
        next_token(ps);
        return x;
    }
    case T_VAR: {
        if (!ps->host)
            return fail(ps, XE_NO_VARIABLES), nullptr;
        const WCHAR *name = ps->tstart;
        UINT len = ps->tlen, colon = 0;
        if (!len) {
            next_token(ps);
            fail_expected(ps, "NAME");
            return nullptr;
        }
        for (UINT i = 0; i < len; i++)
            if (name[i] == ':') {
                colon = i;
                break;
            }
        if (!len || (colon && (colon + 1 >= len || colon == 0)))
            return fail(ps, XE_EXPR_EXPECTED), nullptr;
        x = new_expr(ps, X_VAR);
        if (!x)
            return nullptr;
        if (ps->pattern) {
            if (ps->v6 && !ps->in_predicate) {
                UINT pos = (UINT)(name - ps->s) - 1;
                return fail_range(ps, 0xC00CE618u, pos, pos + 1), nullptr;
            }
            return fail(ps, XE_PATTERN_VAR), nullptr;
        }
        if (colon) {
            BOOL found;
            x->uri = resolve_prefix(ps, name, colon, &found);
            if (!found)
                return fail(ps, XE_UNDECLARED_PREFIX), nullptr;
            x->local = name + colon + 1;
            x->llen = len - colon - 1;
        } else {
            x->local = name;
            x->llen = len;
        }
        if (ps->host->declared && !ps->host->declared(ps->host->user, x->uri, x->local, x->llen))
            return fail(ps, XE_VAR_UNRESOLVED), nullptr;
        x->t0 = (UINT)(ps->tstart - ps->s) - 1;
        x->t1 = (UINT)(ps->tstart - ps->s) + ps->tlen;
        next_token(ps);
        return x;
    }
    case T_NAME: {
        const WCHAR *name = ps->tstart;
        UINT len = ps->tlen;
        int id = -1;
        UINT min = 0, max = 0;
        for (UINT i = 0; i < sizeof(functions) / sizeof(functions[0]); i++) {
            if (name_is(name, len, functions[i].name)) {
                id = functions[i].id;
                min = (UINT)functions[i].min;
                max = (UINT)functions[i].max;
                break;
            }
        }
        /* msxml6 reads a pattern as steps only, a function other than id and key is not a node test. */
        BOOL is_key_call = name_is(name, len, "key");
        if (ps->pattern && ps->host && ps->v6 && !ps->in_predicate && id != F_ID && !is_key_call) {
            fail_range(ps, XE_NODETEST, (UINT)(name - ps->s), (UINT)(name - ps->s) + len);
            return nullptr;
        }
        if (ps->pattern && ps->host && len == 7 && name_is(name, len, "current")) {
            const WCHAR *close = name + len;
            while (close < ps->e && *close != ')')
                close++;
            fail_range(ps, XE_PATTERN_FUNCTION, (UINT)(name - ps->s), (UINT)(close < ps->e ? close + 1 - ps->s : ps->e - ps->s));
            UINT k = 0;
            for (const char *q = "current"; *q; q++)
                ps->errp1[k++] = (WCHAR)*q;
            ps->errp1[k] = 0;
            return nullptr;
        }
        const WCHAR *ext_uri = nullptr, *ext_local = name;
        UINT ext_len = len;
        BOOL ext = FALSE;
        UINT fstart = (UINT)(name - ps->s);
        UINT fname_len = len;
        if ((id < 0 || (id == F_INDEX && ps->xpath)) && ps->host) {
            UINT colon = ps->colon;
            if (colon) {
                BOOL found;
                ext_uri = resolve_prefix(ps, name, colon, &found);
                if (!found)
                    return fail(ps, XE_UNDECLARED_PREFIX), nullptr;
                ext_local = name + colon + 1;
                ext_len = len - colon - 1;
            }
            ext = TRUE;
            id = F_COUNT_FUNCS;
            min = 0;
            max = 1000;
        }
        if (id < 0 || (id == F_INDEX && ps->xpath)) {
            fail(ps, XE_BAD_FUNCTION);
            return nullptr;
        }
        next_token(ps);
        if (ps->kind != T_LP) {
            fail_expected(ps, "(");
            return nullptr;
        }
        next_token(ps);
        UINT astart = (UINT)(ps->tstart - ps->s);
        x = new_expr(ps, ext ? X_EXT : X_FUNC);
        if (!x)
            return nullptr;
        x->op = id;
        x->uri = ext_uri;
        x->local = ext_local;
        x->llen = ext_len;
        Expr *args[128];
        UINT n = 0;
        if (ps->kind == T_RP && min > 0 && ps->host && !ps->v6) {
            fail(ps, XE_EXPR_EXPECTED);
            return nullptr;
        }
        if (ps->kind != T_RP) {
            for (;;) {
                if (n == 128) {
                    fail(ps, XE_BAD_ARGUMENTS);
                    return nullptr;
                }
                /* in msxml6 the arguments of id() and key() in a pattern are strings. */
                if (ps->pattern && ps->host && ps->v6 && !ps->in_predicate && (id == F_ID || is_key_call) &&
                    ps->kind != T_STR) {
                    fail_expected(ps, "STRING");
                    return nullptr;
                }
                Expr *a = parse_or(ps);
                if (!a)
                    return nullptr;
                args[n++] = a;
                if (n == 1 && !ext && ps->host && !ps->v6 &&
                    (id == F_COUNT || id == F_SUM || id == F_LOCAL_NAME || id == F_NAMESPACE_URI || id == F_NAME) &&
                    expr_not_set(a)) {
                    fail_range(ps, XE_NOT_DOM_NODE, fstart, (UINT)(ps->p - ps->s));
                    return nullptr;
                }
                if (ps->kind == T_COMMA) {
                    if (n >= max && !ps->v6) {
                        fail_expected(ps, ")");
                        return nullptr;
                    }
                    next_token(ps);
                    continue;
                }
                break;
            }
        }
        if (ps->kind != T_RP) {
            fail_expected(ps, ")");
            return nullptr;
        }
        next_token(ps);
        UINT fend = ps->prevend;
        if (n < min || n > max) {
            fail_range(ps, XE_BAD_ARGUMENTS, ps->v6 ? fstart : astart, fend);
            return nullptr;
        }
        if (ext && !ps->host->known(ps->host->user, ext_uri, ext_local, ext_len, n)) {
            fail_range(ps, XE_BAD_FUNCTION, fstart, fend);
            UINT k = 0;
            for (UINT i = 0; i < fname_len && k < 38; i++)
                ps->errp1[k++] = name[i];
            ps->errp1[k] = 0;
            return nullptr;
        }
        x->t0 = fstart;
        x->t1 = fend;
        if (!ext && n && (id == F_COUNT || id == F_SUM || id == F_LOCAL_NAME || id == F_NAMESPACE_URI || id == F_NAME) &&
            expr_not_set(args[0])) {
            fail_range(ps, ps->v6 ? 0xC00CE336u : XE_NOT_DOM_NODE, fstart, fend);
            if (ps->v6)
                ps->errp1[0] = '1', ps->errp1[1] = 0;
            return nullptr;
        }
        if (n) {
            x->args = arena_alloc(ps, n * sizeof(Expr *));
            if (!x->args)
                return nullptr;
            memcpy(x->args, args, n * sizeof(Expr *));
        }
        x->nargs = n;
        return x;
    }
    }
    fail(ps, ps->kind == T_ERROR ? XE_XPATH_CHAR
                                 : ps->host && !ps->v6 && ps->kind == T_PIPE ? XE_NOT_DOM_NODE : XE_EXPR_EXPECTED);
    return nullptr;
}

static Expr *parse_path_expr(Parser *ps)
{
    if (ps->depth > 200) {
        fail(ps, XE_XPATH_INCOMPLETE);
        return nullptr;
    }
    if (ps->kind == T_SLASH || ps->kind == T_DSLASH) {
        Expr *path = new_expr(ps, X_PATH);
        if (!path)
            return nullptr;
        path->absolute = TRUE;
        BOOL deep = ps->kind == T_DSLASH;
        next_token(ps);
        if (!deep && !starts_step(ps))
            return path; /* "/" alone is the root. */
        Step *first = nullptr;
        if (deep) {
            first = descendant_or_self_step(ps);
            if (!first)
                return nullptr;
            path->steps = first;
            first = nullptr;
        }
        if (!starts_step(ps)) {
            fail(ps, XE_NODETEST);
            return nullptr;
        }
        if (!parse_steps(ps, path, first))
            return nullptr;
        if (!ps->pattern)
            merge_steps(path);
        return path;
    }
    if (starts_step(ps)) {
        Expr *path = new_expr(ps, X_PATH);
        if (!path)
            return nullptr;
        if (!parse_steps(ps, path, nullptr))
            return nullptr;
        if (!ps->pattern)
            merge_steps(path);
        return path;
    }
    UINT pstart = (UINT)(ps->tstart - ps->s);
    Expr *primary = parse_primary(ps);
    if (!primary)
        return nullptr;
    if (ps->host && (ps->kind == T_SLASH || ps->kind == T_DSLASH) && expr_not_set(primary)) {
        if (ps->v6)
            fail_range(ps, XE_NOT_NODESET, pstart, (UINT)(ps->tstart - ps->s));
        else
            fail_range(ps, XE_NOT_DOM_NODE, pstart, (UINT)(ps->p - ps->s));
        return nullptr;
    }
    if (ps->host && ps->kind == T_LB && expr_not_set(primary)) {
        if (ps->v6)
            fail_range(ps, XE_NOT_NODESET, pstart, (UINT)(ps->tstart - ps->s));
        else
            fail(ps, XE_NOT_DOM_NODE);
        return nullptr;
    }
    if (ps->kind == T_LB || ps->kind == T_SLASH || ps->kind == T_DSLASH) {
        Expr *f = new_expr(ps, X_FILTER);
        if (!f)
            return nullptr;
        f->primary = primary;
        BOOL pos = FALSE;
        if (!parse_predicates(ps, &f->preds, &f->npreds, &pos))
            return nullptr;
        if (ps->kind == T_SLASH || ps->kind == T_DSLASH) {
            BOOL deep = ps->kind == T_DSLASH;
            next_token(ps);
            if (deep) {
                Step *d = descendant_or_self_step(ps);
                if (!d)
                    return nullptr;
                f->steps = d;
            }
            if (!starts_step(ps)) {
                fail(ps, XE_NODETEST);
                return nullptr;
            }
            if (!parse_steps(ps, f, nullptr))
                return nullptr;
            if (!ps->pattern)
                merge_steps(f);
        }
        return f;
    }
    return primary;
}

static Expr *parse_union(Parser *ps)
{
    Expr *left = parse_path_expr(ps);
    while (left && ps->kind == T_PIPE) {
        if (ps->host && expr_not_set(left)) {
            fail_range(ps, XE_NOT_DOM_NODE, 0, (UINT)(ps->p - ps->s));
            return nullptr;
        }
        next_token(ps);
        Expr *right = parse_path_expr(ps);
        if (!right) {
            if (ps->host && ps->error == (HRESULT)XE_EXPR_EXPECTED && ps->kind == T_EOF) {
                if (ps->v6) {
                    ps->error = (HRESULT)XE_XPATH_UNEXPECTED;
                    UINT k = 0;
                    for (const char *q = "'EOF'"; *q; q++)
                        ps->errp1[k++] = (WCHAR)*q;
                    ps->errp1[k] = 0;
                } else {
                    ps->error = (HRESULT)XE_NOT_DOM_NODE;
                }
            }
            return nullptr;
        }
        Expr *u = new_expr(ps, X_UNION);
        if (!u)
            return nullptr;
        u->a = left;
        u->b = right;
        left = u;
    }
    return left;
}

/* an operand that was missing after an arithmetic operator is named a number. */
static void number_expected(Parser *ps)
{
    if (ps->host && ps->error == (HRESULT)XE_EXPR_EXPECTED)
        ps->error = (HRESULT)XE_NUMBER_EXPECTED;
}

static Expr *parse_unary(Parser *ps)
{
    if (ps->kind == T_MINUS) {
        next_token(ps);
        if (ps->host && !ps->v6 && ps->kind == T_MINUS) {
            fail_range(ps, XE_NUMBER_EXPECTED, (UINT)(ps->tstart - ps->s), (UINT)(ps->tstart - ps->s) + 1);
            return nullptr;
        }
        Expr *x = parse_unary(ps);
        if (!x) {
            number_expected(ps);
            return nullptr;
        }
        Expr *n = new_expr(ps, X_NEG);
        if (!n)
            return nullptr;
        n->a = x;
        return n;
    }
    return parse_union(ps);
}

static Expr *binary(Parser *ps, int op, Expr *a, Expr *b)
{
    Expr *x = new_expr(ps, X_BIN);
    if (x) {
        x->op = op;
        x->a = a;
        x->b = b;
    }
    return x;
}

static Expr *parse_mul(Parser *ps)
{
    Expr *left = parse_unary(ps);
    while (left) {
        int op;
        if (ps->kind == T_MUL)
            op = OP_MUL;
        else if (is_op(ps, "div"))
            op = OP_DIV;
        else if (is_op(ps, "mod"))
            op = OP_MOD;
        else
            break;
        next_token(ps);
        Expr *right = parse_unary(ps);
        if (!right) {
            number_expected(ps);
            return nullptr;
        }
        left = binary(ps, op, left, right);
    }
    return left;
}

static Expr *parse_add(Parser *ps)
{
    Expr *left = parse_mul(ps);
    while (left && (ps->kind == T_PLUS || ps->kind == T_MINUS)) {
        int op = ps->kind == T_PLUS ? OP_ADD : OP_SUB;
        next_token(ps);
        Expr *right = parse_mul(ps);
        if (!right) {
            number_expected(ps);
            return nullptr;
        }
        left = binary(ps, op, left, right);
    }
    return left;
}

/* the dollar operators of xslpattern. */
static int xsl_op(Parser *ps)
{
    if (ps->kind != T_OP || ps->xpath || ps->tlen < 3 || ps->tstart[0] != '$')
        return -1;
    const WCHAR *n = ps->tstart + 1;
    UINT len = ps->tlen - 2;
    if (name_is(n, len, "eq"))
        return OP_EQ;
    if (name_is(n, len, "ne"))
        return OP_NE;
    if (name_is(n, len, "lt"))
        return OP_LT;
    if (name_is(n, len, "le"))
        return OP_LE;
    if (name_is(n, len, "gt"))
        return OP_GT;
    if (name_is(n, len, "ge"))
        return OP_GE;
    if (name_is(n, len, "and"))
        return OP_AND;
    if (name_is(n, len, "or"))
        return OP_OR;
    return -1;
}

static Expr *parse_rel(Parser *ps)
{
    Expr *left = parse_add(ps);
    while (left) {
        int op = -1;
        switch (ps->kind) {
        case T_LT:
            op = OP_LT;
            break;
        case T_LE:
            op = OP_LE;
            break;
        case T_GT:
            op = OP_GT;
            break;
        case T_GE:
            op = OP_GE;
            break;
        }
        int x = xsl_op(ps);
        if (x == OP_LT || x == OP_LE || x == OP_GT || x == OP_GE)
            op = x;
        if (op < 0)
            break;
        next_token(ps);
        Expr *right = parse_add(ps);
        if (!right)
            return nullptr;
        left = binary(ps, op, left, right);
    }
    return left;
}

static Expr *parse_eq(Parser *ps)
{
    Expr *left = parse_rel(ps);
    while (left) {
        int op = -1;
        if (ps->kind == T_EQ)
            op = OP_EQ;
        else if (ps->kind == T_NE)
            op = OP_NE;
        int x = xsl_op(ps);
        if (x == OP_EQ || x == OP_NE)
            op = x;
        if (op < 0)
            break;
        next_token(ps);
        Expr *right = parse_rel(ps);
        if (!right)
            return nullptr;
        left = binary(ps, op, left, right);
    }
    return left;
}

static Expr *parse_and(Parser *ps)
{
    Expr *left = parse_eq(ps);
    while (left && (is_op(ps, "and") || xsl_op(ps) == OP_AND)) {
        next_token(ps);
        Expr *right = parse_eq(ps);
        if (!right)
            return nullptr;
        left = binary(ps, OP_AND, left, right);
    }
    return left;
}

static Expr *parse_or(Parser *ps)
{
    ps->depth++;
    Expr *left = ps->depth > 200 ? (fail(ps, XE_XPATH_INCOMPLETE), nullptr) : parse_and(ps);
    while (left && (is_op(ps, "or") || xsl_op(ps) == OP_OR)) {
        next_token(ps);
        Expr *right = parse_and(ps);
        if (!right) {
            left = nullptr;
            break;
        }
        left = binary(ps, OP_OR, left, right);
    }
    ps->depth--;
    return left;
}

/* ------------------------------------------------------------------ values */

typedef struct Env {
    XNode *node;
    UINT pos;
    UINT size;
    BOOL xpath;
    XNode *root;
    HRESULT error;
    const XpHost *host;
    XNode *current;
    const XNs *ns;
    UINT nns;
    const WCHAR *text;
} Env;

void xpv_free(XpVal *v)
{
    xb_free(&v->s);
    xs_free(&v->set);
}

/* whether strings of nodes include the white space the parser dropped between tags, as xslt sees them. */
static LONG g_mode_tls = -1;

static DWORD mode_slot(void)
{
    if (g_mode_tls < 0) {
        DWORD s = TlsAlloc();
        if (InterlockedCompareExchange(&g_mode_tls, (LONG)s, -1) != -1)
            TlsFree(s);
    }
    return (DWORD)g_mode_tls;
}

BOOL xd_text_mode(BOOL on)
{
    DWORD slot = mode_slot();
    BOOL old = TlsGetValue(slot) != nullptr;
    TlsSetValue(slot, on ? (PVOID)1 : nullptr);
    return old;
}

static void put_hidden(XNode *n, XBuf *out)
{
    if (n->flags & XF_WS_AFTER)
        xb_putc(out, ' ');
}

/* the string of a node of a source tree: the text of its descendants with a space for each run of white space the
 * parser dropped. */
void xd_text_xsl(XNode *n, XBuf *out)
{
    switch (n->type) {
    case XN_ELEMENT:
    case XN_DOCUMENT:
    case XN_FRAGMENT:
        break;
    default:
        xd_text(n, out);
        return;
    }
    if (n->type == XN_ELEMENT && (n->flags & XF_WS_INSIDE))
        xb_putc(out, ' ');
    XNode *c = n->first;
    while (c) {
        if ((c->type == XN_TEXT || c->type == XN_CDATA) && c->value)
            xb_put(out, c->value, c->length);
        if (c->type == XN_ELEMENT && c->first) {
            if (c->flags & XF_WS_INSIDE)
                xb_putc(out, ' ');
            c = c->first;
            continue;
        }
        if (c->type == XN_ELEMENT && (c->flags & XF_WS_INSIDE))
            xb_putc(out, ' ');
        put_hidden(c, out);
        while (c != n && !c->next) {
            c = c->parent;
            if (c == n)
                break;
            put_hidden(c, out);
        }
        if (c == n)
            break;
        c = c->next;
    }
}

static void node_string(XNode *n, XBuf *out)
{
    if (TlsGetValue(mode_slot()))
        xd_text_xsl(n, out);
    else
        xd_text(n, out);
}

void xpv_string(XpVal *v, XBuf *out)
{
    switch (v->t) {
    case XPV_STR:
        xb_put(out, v->s.s ? v->s.s : L"", v->s.length);
        break;
    case XPV_NUM:
        xd_format_number(v->n, out);
        break;
    case XPV_BOOL:
        xb_puta(out, v->b ? "true" : "false");
        break;
    case XPV_SET:
        if (v->set.n)
            node_string(v->set.v[0], out);
        break;
    }
}

double xpv_number(XpVal *v)
{
    switch (v->t) {
    case XPV_NUM:
        return v->n;
    case XPV_BOOL:
        return v->b ? 1 : 0;
    default: {
        XBuf b = {};
        xpv_string(v, &b);
        double d = xd_parse_number(b.s ? b.s : L"", b.length);
        xb_free(&b);
        return d;
    }
    }
}

BOOL xpv_bool(XpVal *v)
{
    switch (v->t) {
    case XPV_NUM:
        return v->n != 0 && !xd_isnan(v->n);
    case XPV_BOOL:
        return v->b;
    case XPV_STR:
        return v->s.length != 0;
    default:
        return v->set.n != 0;
    }
}

static void val_set_string(XpVal *v, const WCHAR *s, UINT n)
{
    v->t = XPV_STR;
    xb_put(&v->s, s, n);
}

void xpv_set_string(XpVal *v, const WCHAR *s, UINT n)
{
    val_set_string(v, s, n);
}

BOOL xpv_copy(XpVal *to, const XpVal *from)
{
    memset(to, 0, sizeof(*to));
    to->t = from->t;
    to->n = from->n;
    to->b = from->b;
    to->rtf = from->rtf;
    if (from->s.length)
        xb_put(&to->s, from->s.s, from->s.length);
    for (UINT i = 0; i < from->set.n; i++)
        if (!xs_add(&to->set, from->set.v[i]))
            return FALSE;
    return !to->s.failed;
}

/* ------------------------------------------------------------------ axes and tests */

static BOOL uri_equal(const WCHAR *a, const WCHAR *b)
{
    if (a == b)
        return TRUE;
    if (!a || !b)
        return FALSE;
    return !wcscmp(a, b);
}

/* xslpattern compares the prefix written in the query with the prefix of the node. */
static BOOL prefix_equal(Step *st, XNode *n)
{
    if (st->test == NT_ANY && !st->prefix)
        return TRUE;
    UINT np = n->local ? (UINT)(n->local - 1) : 0;
    return np == st->plen && !memcmp(n->qname, st->prefix, (SIZE_T)np * sizeof(WCHAR));
}

static BOOL test_node(Step *st, XNode *n)
{
    switch (st->test) {
    case NT_NODE:
        if ((n->type == XN_DOCTYPE || (n->type == XN_PI && (n->flags & XF_XMLDECL))) && TlsGetValue(mode_slot()))
            return FALSE;
        return TRUE;
    case NT_TEXT:
        return n->type == XN_TEXT || n->type == XN_CDATA;
    case NT_COMMENT:
        return n->type == XN_COMMENT;
    case NT_PI:
        if (n->type != XN_PI || (n->flags & XF_XMLDECL))
            return FALSE;
        return !st->local || (xd_atom_len(n->qname) == st->llen &&
                              !memcmp(n->qname, st->local, (SIZE_T)st->llen * sizeof(WCHAR)));
    case NT_ANY:
        if (st->axis == AX_ATTRIBUTE) {
            if (n->type != XN_ATTRIBUTE || xd_is_xmlns_attr(n))
                return FALSE;
        } else if (n->type != XN_ELEMENT) {
            return FALSE;
        }
        if (st->by_prefix)
            return prefix_equal(st, n);
        return !st->element_default_ns || uri_equal(n->uri, st->uri);
    case NT_NAME: {
        if (st->axis == AX_ATTRIBUTE) {
            if (n->type != XN_ATTRIBUTE || xd_is_xmlns_attr(n))
                return FALSE;
        } else if (n->type != XN_ELEMENT) {
            return FALSE;
        }
        if (xd_local_length(n) != st->llen || memcmp(xd_local(n), st->local, (SIZE_T)st->llen * sizeof(WCHAR)))
            return FALSE;
        if (st->by_prefix)
            return prefix_equal(st, n);
        return uri_equal(n->uri, st->uri);
    }
    }
    return FALSE;
}

/* the nodes of an axis in the axis' own order, filtered by the node test. */
static BOOL gather(Step *st, XNode *c, XSet *out)
{
    switch (st->axis) {
    case AX_CHILD:
        for (XNode *n = c->first; n; n = n->next)
            if (test_node(st, n) && !xs_add(out, n))
                return FALSE;
        return TRUE;
    case AX_ATTRIBUTE:
        if (c->type != XN_ELEMENT && c->type != XN_PI)
            return TRUE;
        for (XNode *n = c->attrs; n; n = n->next)
            if (test_node(st, n) && !xs_add(out, n))
                return FALSE;
        return TRUE;
    case AX_SELF:
        return !test_node(st, c) || xs_add(out, c);
    case AX_PARENT:
        return !c->parent || !test_node(st, c->parent) || xs_add(out, c->parent);
    case AX_ANCESTOR_OR_SELF:
        if (test_node(st, c) && !xs_add(out, c))
            return FALSE;
        [[fallthrough]];
    case AX_ANCESTOR:
        for (XNode *n = c->parent; n; n = n->parent)
            if (test_node(st, n) && !xs_add(out, n))
                return FALSE;
        return TRUE;
    case AX_DESCENDANT_OR_SELF:
        if (test_node(st, c) && !xs_add(out, c))
            return FALSE;
        [[fallthrough]];
    case AX_DESCENDANT: {
        if (c->type == XN_ATTRIBUTE)
            return TRUE;
        XNode *n = c->first;
        while (n) {
            if (test_node(st, n) && !xs_add(out, n))
                return FALSE;
            if (n->first) {
                n = n->first;
                continue;
            }
            while (n != c && !n->next)
                n = n->parent;
            if (n == c)
                break;
            n = n->next;
        }
        return TRUE;
    }
    case AX_FOLLOWING_SIBLING:
        if (c->type == XN_ATTRIBUTE)
            return TRUE;
        for (XNode *n = c->next; n; n = n->next)
            if (test_node(st, n) && !xs_add(out, n))
                return FALSE;
        return TRUE;
    case AX_PRECEDING_SIBLING:
        if (c->type == XN_ATTRIBUTE)
            return TRUE;
        for (XNode *n = c->prev; n; n = n->prev)
            if (test_node(st, n) && !xs_add(out, n))
                return FALSE;
        return TRUE;
    case AX_FOLLOWING: {
        XNode *n = c;
        if (n->type == XN_ATTRIBUTE) {
            /* the element's own subtree comes after its attributes. */
            n = n->parent;
            for (XNode *top = n->first; top; top = top->next) {
                XNode *d = top;
                for (;;) {
                    if (test_node(st, d) && !xs_add(out, d))
                        return FALSE;
                    if (d->first) {
                        d = d->first;
                        continue;
                    }
                    while (d != top && !d->next)
                        d = d->parent;
                    if (d == top)
                        break;
                    d = d->next;
                }
            }
        }
        /* everything after the node's own subtree. */
        for (;;) {
            while (n && !n->next)
                n = n->parent;
            if (!n)
                return TRUE;
            n = n->next;
            XNode *top = n;
            XNode *d = n;
            for (;;) {
                if (test_node(st, d) && !xs_add(out, d))
                    return FALSE;
                if (d->first) {
                    d = d->first;
                    continue;
                }
                while (d != top && !d->next)
                    d = d->parent;
                if (d == top)
                    break;
                d = d->next;
            }
        }
    }
    case AX_PRECEDING: {
        /* reverse document order: the siblings before each ancestor, deepest last-first. */
        XNode *n = c;
        if (n->type == XN_ATTRIBUTE)
            n = n->parent;
        for (; n; n = n->parent) {
            for (XNode *s = n->prev; s; s = s->prev) {
                /* the subtree of s, last node first. */
                XNode *d = s;
                while (d->last)
                    d = d->last;
                for (;;) {
                    if (test_node(st, d) && !xs_add(out, d))
                        return FALSE;
                    if (d == s)
                        break;
                    if (d->prev) {
                        d = d->prev;
                        while (d->last)
                            d = d->last;
                    } else {
                        d = d->parent;
                    }
                }
            }
        }
        return TRUE;
    }
    case AX_NAMESPACE:
        return TRUE;
    }
    return TRUE;
}

static BOOL reverse_axis(int axis)
{
    return axis == AX_PARENT || axis == AX_ANCESTOR || axis == AX_ANCESTOR_OR_SELF || axis == AX_PRECEDING ||
           axis == AX_PRECEDING_SIBLING;
}

static HRESULT eval(Expr *x, Env *env, XpVal *out);

/* narrows a set by a predicate, positions counted along the axis. */
static HRESULT apply_predicate(Expr *pred, XSet *set, Env *env, BOOL zero_based)
{
    XSet kept = {};
    for (UINT i = 0; i < set->n; i++) {
        Env sub = *env;
        sub.node = set->v[i];
        sub.pos = i + 1;
        sub.size = set->n;
        XpVal v = {};
        HRESULT hr = eval(pred, &sub, &v);
        if (FAILED(hr)) {
            xpv_free(&v);
            xs_free(&kept);
            return hr;
        }
        BOOL keep;
        if (v.t == XPV_NUM)
            keep = (v.n == (double)(i + 1) - (zero_based ? 1 : 0));
        else
            keep = xpv_bool(&v);
        xpv_free(&v);
        if (keep && !xs_add(&kept, set->v[i])) {
            xs_free(&kept);
            return E_OUTOFMEMORY;
        }
    }
    xs_free(set);
    *set = kept;
    return S_OK;
}

static HRESULT run_steps(Step *st, XSet *current, Env *env)
{
    for (; st; st = st->next) {
        XSet next = {};
        BOOL single = current->n == 1;
        for (UINT i = 0; i < current->n; i++) {
            XSet cand = {};
            if (!gather(st, current->v[i], &cand)) {
                xs_free(&cand);
                xs_free(&next);
                return E_OUTOFMEMORY;
            }
            for (UINT p = 0; p < st->npreds && cand.n; p++) {
                HRESULT hr = apply_predicate(st->preds[p], &cand, env, !env->xpath);
                if (FAILED(hr)) {
                    xs_free(&cand);
                    xs_free(&next);
                    return hr;
                }
            }
            if (single && reverse_axis(st->axis)) {
                for (UINT a = 0, b = cand.n; a + 1 < b; a++, b--) {
                    XNode *t = cand.v[a];
                    cand.v[a] = cand.v[b - 1];
                    cand.v[b - 1] = t;
                }
            }
            if (!next.n && !next.v) {
                next = cand;
                continue;
            }
            for (UINT k = 0; k < cand.n; k++) {
                if (!xs_add(&next, cand.v[k])) {
                    xs_free(&cand);
                    xs_free(&next);
                    return E_OUTOFMEMORY;
                }
            }
            xs_free(&cand);
        }
        xs_free(current);
        *current = next;
        if (!single)
            sort_unique(current);
    }
    return S_OK;
}

/* ------------------------------------------------------------------ evaluation */

static BOOL compare_strings(int op, const WCHAR *a, UINT al, const WCHAR *b, UINT bl)
{
    BOOL equal = al == bl && !memcmp(a, b, (SIZE_T)al * sizeof(WCHAR));
    return op == OP_EQ ? equal : !equal;
}

static BOOL compare_numbers(int op, double a, double b)
{
    switch (op) {
    case OP_EQ:
        return a == b;
    case OP_NE:
        return a != b;
    case OP_LT:
        return a < b;
    case OP_LE:
        return a <= b;
    case OP_GT:
        return a > b;
    default:
        return a >= b;
    }
}

static BOOL compare_values(int op, XpVal *a, XpVal *b)
{
    BOOL equality = op == OP_EQ || op == OP_NE;
    if (a->t == XPV_SET && b->t == XPV_SET) {
        for (UINT i = 0; i < a->set.n; i++) {
            XBuf sa = {};
            node_string(a->set.v[i], &sa);
            for (UINT j = 0; j < b->set.n; j++) {
                XBuf sb = {};
                node_string(b->set.v[j], &sb);
                BOOL r;
                if (equality) {
                    r = compare_strings(op, sa.s ? sa.s : L"", sa.length, sb.s ? sb.s : L"", sb.length);
                } else {
                    r = compare_numbers(op, xd_parse_number(sa.s ? sa.s : L"", sa.length),
                                        xd_parse_number(sb.s ? sb.s : L"", sb.length));
                }
                xb_free(&sb);
                if (r) {
                    xb_free(&sa);
                    return TRUE;
                }
            }
            xb_free(&sa);
        }
        return FALSE;
    }
    if (a->t == XPV_SET || b->t == XPV_SET) {
        BOOL swapped = b->t == XPV_SET;
        XpVal *set = swapped ? b : a;
        XpVal *other = swapped ? a : b;
        if (other->t == XPV_BOOL) {
            BOOL sb = set->set.n != 0;
            return swapped ? (equality ? (op == OP_EQ ? other->b == sb : other->b != sb) : compare_numbers(op, other->b, sb))
                           : (equality ? (op == OP_EQ ? sb == other->b : sb != other->b) : compare_numbers(op, sb, other->b));
        }
        for (UINT i = 0; i < set->set.n; i++) {
            XBuf s = {};
            node_string(set->set.v[i], &s);
            BOOL r;
            if (other->t == XPV_NUM) {
                double n = xd_parse_number(s.s ? s.s : L"", s.length);
                r = swapped ? compare_numbers(op, other->n, n) : compare_numbers(op, n, other->n);
            } else if (equality) {
                r = compare_strings(op, s.s ? s.s : L"", s.length, other->s.s ? other->s.s : L"", other->s.length);
            } else {
                double n = xd_parse_number(s.s ? s.s : L"", s.length);
                double m = xd_parse_number(other->s.s ? other->s.s : L"", other->s.length);
                r = swapped ? compare_numbers(op, m, n) : compare_numbers(op, n, m);
            }
            xb_free(&s);
            if (r)
                return TRUE;
        }
        return FALSE;
    }
    if (equality) {
        if (a->t == XPV_BOOL || b->t == XPV_BOOL) {
            BOOL x = xpv_bool(a), y = xpv_bool(b);
            return op == OP_EQ ? x == y : x != y;
        }
        if (a->t == XPV_NUM || b->t == XPV_NUM)
            return compare_numbers(op, xpv_number(a), xpv_number(b));
        return compare_strings(op, a->s.s ? a->s.s : L"", a->s.length, b->s.s ? b->s.s : L"", b->s.length);
    }
    return compare_numbers(op, xpv_number(a), xpv_number(b));
}

static double xp_floor(double x)
{
    if (xd_isnan(x) || x > 4.5e15 || x < -4.5e15)
        return x;
    double t = (double)(INT64)x;
    return t > x ? t - 1 : t;
}

static double xp_ceil(double x)
{
    if (xd_isnan(x) || x > 4.5e15 || x < -4.5e15)
        return x;
    double t = (double)(INT64)x;
    return t < x ? t + 1 : t;
}

static BOOL is_infinite(double x)
{
    return !xd_isnan(x) && xd_isnan(x - x);
}

static double xp_mod(double a, double b)
{
    if (xd_isnan(a) || xd_isnan(b) || b == 0 || is_infinite(a))
        return xd_nan();
    if (is_infinite(b))
        return a;
    double q = a / b;
    double t = q < 0 ? xp_ceil(q) : xp_floor(q);
    return a - t * b;
}

/* the first argument or the context node's string, for the functions that take either. */
static HRESULT arg_string(Expr *x, UINT i, Env *env, XBuf *out)
{
    if (i < x->nargs) {
        XpVal v = {};
        HRESULT hr = eval(x->args[i], env, &v);
        if (SUCCEEDED(hr))
            xpv_string(&v, out);
        xpv_free(&v);
        return hr;
    }
    node_string(env->node, out);
    return S_OK;
}

static HRESULT arg_number(Expr *x, UINT i, Env *env, double *out)
{
    XpVal v = {};
    HRESULT hr = eval(x->args[i], env, &v);
    if (SUCCEEDED(hr))
        *out = xpv_number(&v);
    xpv_free(&v);
    return hr;
}

static HRESULT eval_function(Expr *x, Env *env, XpVal *out)
{
    HRESULT hr = S_OK;
    XBuf a = {}, b = {}, c = {};
    switch (x->op) {
    case F_LAST:
        out->t = XPV_NUM;
        out->n = env->size;
        break;
    case F_POSITION:
        out->t = XPV_NUM;
        out->n = env->pos;
        break;
    case F_INDEX:
        out->t = XPV_NUM;
        out->n = env->pos ? env->pos - 1 : 0;
        break;
    case F_COUNT: {
        XpVal v = {};
        hr = eval(x->args[0], env, &v);
        if (SUCCEEDED(hr) && v.rtf && x->args[0]->kind == X_VAR && env->host && env->host->rtf_error)
            hr = env->host->rtf_error(env->host->user, 1, x->args[0]->local, x->args[0]->llen, 1, env->text, x->t0,
                                      x->t1);
        if (SUCCEEDED(hr) && v.t != XPV_SET)
            hr = (HRESULT)XE_NOT_NODESET;
        out->t = XPV_NUM;
        out->n = v.set.n;
        xpv_free(&v);
        break;
    }
    case F_ID:
        out->t = XPV_SET;
        break;
    case F_LOCAL_NAME:
    case F_NAMESPACE_URI:
    case F_NAME: {
        XNode *n = env->node;
        if (x->nargs) {
            XpVal v = {};
            hr = eval(x->args[0], env, &v);
            if (SUCCEEDED(hr) && v.t != XPV_SET)
                hr = (HRESULT)XE_NOT_NODESET;
            n = SUCCEEDED(hr) && v.set.n ? v.set.v[0] : nullptr;
            xpv_free(&v);
        }
        out->t = XPV_STR;
        if (n && SUCCEEDED(hr)) {
            if (x->op == F_NAMESPACE_URI) {
                if (n->uri)
                    xb_put(&out->s, n->uri, xd_wlen(n->uri));
            } else if (n->qname && (n->type == XN_ELEMENT || n->type == XN_ATTRIBUTE || n->type == XN_PI)) {
                if (x->op == F_NAME)
                    xb_put(&out->s, n->qname, xd_atom_len(n->qname));
                else
                    xb_put(&out->s, xd_local(n), xd_local_length(n));
            }
        }
        break;
    }
    case F_STRING:
        hr = arg_string(x, 0, env, &a);
        if (SUCCEEDED(hr))
            val_set_string(out, a.s ? a.s : L"", a.length);
        break;
    case F_CONCAT:
        out->t = XPV_STR;
        for (UINT i = 0; i < x->nargs && SUCCEEDED(hr); i++) {
            a.length = 0;
            hr = arg_string(x, i, env, &a);
            if (SUCCEEDED(hr) && a.length)
                xb_put(&out->s, a.s, a.length);
        }
        break;
    case F_STARTS_WITH:
    case F_CONTAINS:
    case F_SUBSTRING_BEFORE:
    case F_SUBSTRING_AFTER: {
        hr = arg_string(x, 0, env, &a);
        if (SUCCEEDED(hr))
            hr = arg_string(x, 1, env, &b);
        if (FAILED(hr))
            break;
        const WCHAR *s = a.s ? a.s : L"", *t = b.s ? b.s : L"";
        UINT sl = a.length, tl = b.length, at = (UINT)-1;
        for (UINT i = 0; i + tl <= sl; i++) {
            if (!memcmp(s + i, t, (SIZE_T)tl * sizeof(WCHAR))) {
                at = i;
                break;
            }
            if (x->op == F_STARTS_WITH)
                break;
        }
        if (x->op == F_STARTS_WITH || x->op == F_CONTAINS) {
            out->t = XPV_BOOL;
            out->b = at != (UINT)-1;
        } else {
            out->t = XPV_STR;
            if (at != (UINT)-1) {
                if (x->op == F_SUBSTRING_BEFORE)
                    xb_put(&out->s, s, at);
                else
                    xb_put(&out->s, s + at + tl, sl - at - tl);
            }
        }
        break;
    }
    case F_SUBSTRING: {
        hr = arg_string(x, 0, env, &a);
        double start = 0, len = 0;
        if (SUCCEEDED(hr))
            hr = arg_number(x, 1, env, &start);
        if (SUCCEEDED(hr) && x->nargs == 3)
            hr = arg_number(x, 2, env, &len);
        if (FAILED(hr))
            break;
        /* positions are rounded and counted from one. */
        double first = xp_floor(start + 0.5);
        double last = x->nargs == 3 ? first + xp_floor(len + 0.5) : 1.0e300;
        if (x->nargs == 3 && (xd_isnan(len) || xd_isnan(start)))
            last = first - 1;
        out->t = XPV_STR;
        if (xd_isnan(start))
            break;
        for (UINT i = 0; i < a.length; i++) {
            double p = (double)(i + 1);
            if (p >= first && p < last)
                xb_putc(&out->s, a.s[i]);
        }
        break;
    }
    case F_STRING_LENGTH:
        hr = arg_string(x, 0, env, &a);
        out->t = XPV_NUM;
        out->n = a.length;
        break;
    case F_NORMALIZE_SPACE: {
        hr = arg_string(x, 0, env, &a);
        out->t = XPV_STR;
        BOOL pending = FALSE;
        for (UINT i = 0; i < a.length; i++) {
            if (xd_is_space(a.s[i])) {
                pending = out->s.length != 0;
            } else {
                if (pending)
                    xb_putc(&out->s, ' ');
                pending = FALSE;
                xb_putc(&out->s, a.s[i]);
            }
        }
        break;
    }
    case F_TRANSLATE: {
        hr = arg_string(x, 0, env, &a);
        if (SUCCEEDED(hr))
            hr = arg_string(x, 1, env, &b);
        if (SUCCEEDED(hr))
            hr = arg_string(x, 2, env, &c);
        if (FAILED(hr))
            break;
        out->t = XPV_STR;
        for (UINT i = 0; i < a.length; i++) {
            UINT k = 0;
            while (k < b.length && b.s[k] != a.s[i])
                k++;
            if (k >= b.length)
                xb_putc(&out->s, a.s[i]);
            else if (k < c.length)
                xb_putc(&out->s, c.s[k]);
        }
        break;
    }
    case F_BOOLEAN:
    case F_NOT: {
        XpVal v = {};
        hr = eval(x->args[0], env, &v);
        out->t = XPV_BOOL;
        out->b = SUCCEEDED(hr) ? xpv_bool(&v) : FALSE;
        if (x->op == F_NOT)
            out->b = !out->b;
        xpv_free(&v);
        break;
    }
    case F_TRUE:
    case F_FALSE:
        out->t = XPV_BOOL;
        out->b = x->op == F_TRUE;
        break;
    case F_LANG: {
        hr = arg_string(x, 0, env, &a);
        out->t = XPV_BOOL;
        if (FAILED(hr))
            break;
        for (XNode *n = env->node; n; n = n->parent) {
            XNode *lang = n->type == XN_ELEMENT ? xd_attr_find(n, L"xml:lang", 8) : nullptr;
            if (!lang || !lang->value)
                continue;
            BOOL same = lang->length >= a.length;
            for (UINT i = 0; i < a.length && same; i++) {
                WCHAR p = lang->value[i], q = a.s[i];
                if (p >= 'A' && p <= 'Z')
                    p += 32;
                if (q >= 'A' && q <= 'Z')
                    q += 32;
                same = p == q;
            }
            out->b = same && (lang->length == a.length || lang->value[a.length] == '-');
            break;
        }
        break;
    }
    case F_NUMBER: {
        out->t = XPV_NUM;
        if (x->nargs) {
            XpVal v = {};
            hr = eval(x->args[0], env, &v);
            out->n = SUCCEEDED(hr) ? xpv_number(&v) : 0;
            xpv_free(&v);
        } else {
            node_string(env->node, &a);
            out->n = xd_parse_number(a.s ? a.s : L"", a.length);
        }
        break;
    }
    case F_SUM: {
        XpVal v = {};
        hr = eval(x->args[0], env, &v);
        if (SUCCEEDED(hr) && v.t != XPV_SET)
            hr = (HRESULT)XE_NOT_NODESET;
        out->t = XPV_NUM;
        double total = 0;
        for (UINT i = 0; SUCCEEDED(hr) && i < v.set.n; i++) {
            XBuf s = {};
            node_string(v.set.v[i], &s);
            total += xd_parse_number(s.s ? s.s : L"", s.length);
            xb_free(&s);
        }
        out->n = total;
        xpv_free(&v);
        break;
    }
    case F_FLOOR:
    case F_CEILING:
    case F_ROUND: {
        double d = 0;
        hr = arg_number(x, 0, env, &d);
        out->t = XPV_NUM;
        out->n = x->op == F_FLOOR ? xp_floor(d) : x->op == F_CEILING ? xp_ceil(d) : xp_floor(d + 0.5);
        break;
    }
    }
    xb_free(&a);
    xb_free(&b);
    xb_free(&c);
    return hr;
}

static HRESULT eval(Expr *x, Env *env, XpVal *out)
{
    switch (x->kind) {
    case X_NUM:
        out->t = XPV_NUM;
        out->n = x->num;
        return S_OK;
    case X_STR:
        val_set_string(out, x->str, x->slen);
        return S_OK;
    case X_FUNC:
        return eval_function(x, env, out);
    case X_VAR:
        if (!env->host || !env->host->variable)
            return (HRESULT)XE_NO_VARIABLES;
        return env->host->variable(env->host->user, x->uri, x->local, x->llen, out);
    case X_EXT: {
        XpVal args[16];
        XpVal *list = args;
        if (x->nargs > 16) {
            list = xd_alloc(sizeof(XpVal) * x->nargs);
            if (!list)
                return E_OUTOFMEMORY;
        }
        memset(list, 0, sizeof(XpVal) * x->nargs);
        HRESULT hr = S_OK;
        for (UINT i = 0; i < x->nargs && SUCCEEDED(hr); i++)
            hr = eval(x->args[i], env, &list[i]);
        if (SUCCEEDED(hr)) {
            XpCall call = {env->node, env->pos, env->size, env->current, env->host, env->ns, env->nns};
            hr = env->host->function(env->host->user, x->uri, x->local, x->llen, list, x->nargs, &call, out);
        }
        for (UINT i = 0; i < x->nargs; i++)
            xpv_free(&list[i]);
        if (list != args)
            xd_free(list);
        return hr;
    }
    case X_NEG: {
        XpVal v = {};
        HRESULT hr = eval(x->a, env, &v);
        out->t = XPV_NUM;
        out->n = SUCCEEDED(hr) ? -xpv_number(&v) : 0;
        xpv_free(&v);
        return hr;
    }
    case X_UNION: {
        XpVal l = {}, r = {};
        HRESULT hr = eval(x->a, env, &l);
        if (SUCCEEDED(hr))
            hr = eval(x->b, env, &r);
        if (SUCCEEDED(hr) && (l.t != XPV_SET || r.t != XPV_SET))
            hr = (HRESULT)XE_NOT_NODESET;
        if (SUCCEEDED(hr)) {
            out->t = XPV_SET;
            out->set = l.set;
            memset(&l.set, 0, sizeof(l.set));
            for (UINT i = 0; i < r.set.n && SUCCEEDED(hr); i++)
                if (!xs_add(&out->set, r.set.v[i]))
                    hr = E_OUTOFMEMORY;
            sort_unique(&out->set);
        }
        xpv_free(&l);
        xpv_free(&r);
        return hr;
    }
    case X_BIN: {
        if (x->op == OP_AND || x->op == OP_OR) {
            XpVal l = {};
            HRESULT hr = eval(x->a, env, &l);
            if (FAILED(hr)) {
                xpv_free(&l);
                return hr;
            }
            BOOL lb = xpv_bool(&l);
            xpv_free(&l);
            out->t = XPV_BOOL;
            if (x->op == OP_AND && !lb) {
                out->b = FALSE;
                return S_OK;
            }
            if (x->op == OP_OR && lb) {
                out->b = TRUE;
                return S_OK;
            }
            XpVal r = {};
            hr = eval(x->b, env, &r);
            out->b = SUCCEEDED(hr) && xpv_bool(&r);
            xpv_free(&r);
            return hr;
        }
        XpVal l = {}, r = {};
        HRESULT hr = eval(x->a, env, &l);
        if (SUCCEEDED(hr))
            hr = eval(x->b, env, &r);
        if (SUCCEEDED(hr)) {
            switch (x->op) {
            case OP_EQ:
            case OP_NE:
            case OP_LT:
            case OP_LE:
            case OP_GT:
            case OP_GE:
                out->t = XPV_BOOL;
                out->b = compare_values(x->op, &l, &r);
                break;
            default: {
                double a = xpv_number(&l), b = xpv_number(&r);
                out->t = XPV_NUM;
                switch (x->op) {
                case OP_ADD:
                    out->n = a + b;
                    break;
                case OP_SUB:
                    out->n = a - b;
                    break;
                case OP_MUL:
                    out->n = a * b;
                    break;
                case OP_DIV:
                    out->n = a / b;
                    break;
                default:
                    out->n = xp_mod(a, b);
                }
            }
            }
        }
        xpv_free(&l);
        xpv_free(&r);
        return hr;
    }
    case X_FILTER: {
        XpVal v = {};
        HRESULT hr = eval(x->primary, env, &v);
        if (FAILED(hr)) {
            xpv_free(&v);
            return hr;
        }
        if (v.t != XPV_SET) {
            if (x->npreds || x->steps) {
                xpv_free(&v);
                return (HRESULT)XE_NOT_NODESET;
            }
            *out = v;
            return S_OK;
        }
        if (v.rtf && (x->npreds || x->steps) && x->primary->kind == X_VAR && env->host && env->host->rtf_error) {
            xpv_free(&v);
            return env->host->rtf_error(env->host->user, 0, x->primary->local, x->primary->llen, 0, env->text,
                                        x->primary->t0, x->primary->t1);
        }
        for (UINT p = 0; p < x->npreds && SUCCEEDED(hr); p++)
            hr = apply_predicate(x->preds[p], &v.set, env, FALSE);
        if (SUCCEEDED(hr) && x->steps)
            hr = run_steps(x->steps, &v.set, env);
        if (FAILED(hr)) {
            xpv_free(&v);
            return hr;
        }
        *out = v;
        return S_OK;
    }
    case X_PATH: {
        XSet cur = {};
        XNode *start = env->node;
        if (x->absolute) {
            start = env->root;
        }
        if (!xs_add(&cur, start))
            return E_OUTOFMEMORY;
        HRESULT hr = run_steps(x->steps, &cur, env);
        if (FAILED(hr)) {
            xs_free(&cur);
            return hr;
        }
        out->t = XPV_SET;
        out->set = cur;
        return S_OK;
    }
    }
    return E_FAIL;
}

/* ------------------------------------------------------------------ patterns */

static void collect_steps(Step *st, Step **v, int *n)
{
    for (; st && *n < 64; st = st->next)
        v[(*n)++] = st;
}

/* whether the node passes the test of a child or attribute step and its predicates among its siblings. */
static HRESULT step_accepts(Step *st, XNode *n, Env *env, BOOL *ok)
{
    *ok = FALSE;
    if (st->axis == AX_ATTRIBUTE) {
        if (n->type != XN_ATTRIBUTE || !n->parent)
            return S_OK;
    } else if (n->type == XN_ATTRIBUTE || !n->parent) {
        return S_OK;
    }
    if (!test_node(st, n))
        return S_OK;
    if (!st->npreds) {
        *ok = TRUE;
        return S_OK;
    }
    XSet cand = {};
    if (!gather(st, n->parent, &cand))
        return E_OUTOFMEMORY;
    HRESULT hr = S_OK;
    for (UINT p = 0; p < st->npreds && cand.n && SUCCEEDED(hr); p++)
        hr = apply_predicate(st->preds[p], &cand, env, FALSE);
    for (UINT i = 0; SUCCEEDED(hr) && i < cand.n; i++)
        if (cand.v[i] == n)
            *ok = TRUE;
    xs_free(&cand);
    return hr;
}

static BOOL is_descend_step(const Step *st)
{
    return st->axis == AX_DESCENDANT_OR_SELF && st->test == NT_NODE && !st->npreds;
}

static HRESULT match_chain(Step **st, int k, BOOL absolute, XNode *n, Env *env, BOOL *matches)
{
    BOOL ok;
    HRESULT hr = step_accepts(st[k], n, env, &ok);
    if (FAILED(hr) || !ok) {
        *matches = FALSE;
        return hr;
    }
    if (k == 0) {
        *matches = !absolute || (n->parent && n->parent->type == XN_DOCUMENT) ||
                   (n->parent && !n->parent->parent && n->parent->type == XN_FRAGMENT);
        return S_OK;
    }
    if (is_descend_step(st[k - 1])) {
        if (k - 1 == 0) {
            *matches = TRUE;
            return S_OK;
        }
        for (XNode *a = n->parent; a; a = a->parent) {
            hr = match_chain(st, k - 2, absolute, a, env, matches);
            if (FAILED(hr) || *matches)
                return hr;
        }
        *matches = FALSE;
        return S_OK;
    }
    if (!n->parent) {
        *matches = FALSE;
        return S_OK;
    }
    return match_chain(st, k - 1, absolute, n->parent, env, matches);
}

/* a pattern by the definition: some ancestor or self context selects the node. used where the fast match cannot be. */
static HRESULT match_slow(Expr *x, XNode *n, Env *env, BOOL *matches)
{
    *matches = FALSE;
    for (XNode *c = n;; c = c->parent) {
        Env sub = *env;
        sub.node = c;
        sub.pos = sub.size = 1;
        sub.root = tree_root(c);
        XpVal v = {};
        HRESULT hr = eval(x, &sub, &v);
        if (SUCCEEDED(hr) && v.t == XPV_SET)
            for (UINT i = 0; i < v.set.n; i++)
                if (v.set.v[i] == n)
                    *matches = TRUE;
        xpv_free(&v);
        if (FAILED(hr) || *matches || !c->parent)
            return hr;
    }
}

static HRESULT match_expr(Expr *x, XNode *n, Env *env, BOOL *matches)
{
    *matches = FALSE;
    if (x->kind == X_UNION) {
        HRESULT hr = match_expr(x->a, n, env, matches);
        if (FAILED(hr) || *matches)
            return hr;
        return match_expr(x->b, n, env, matches);
    }
    if (x->kind != X_PATH)
        return match_slow(x, n, env, matches);
    if (!x->steps) {
        *matches = x->absolute && n->type == XN_DOCUMENT;
        return S_OK;
    }
    Step *st[64];
    int count = 0;
    collect_steps(x->steps, st, &count);
    return match_chain(st, count - 1, x->absolute, n, env, matches);
}

/* ------------------------------------------------------------------ entry points */

struct XpExpr {
    Expr *root;
    Arena *arena;
    WCHAR *text;
    const XNs *ns;
    UINT nns;
    const XpHost *host;
    BOOL xpath;
    BOOL pattern;
};

XpExpr *xp_compile(const WCHAR *text, UINT length, const XNs *ns, UINT nscount, UINT flags, const XpHost *host,
                   XDoc *doc, XpError *error)
{
    Parser ps = {};
    WCHAR *copy = xd_alloc_raw(((SIZE_T)length + 1) * sizeof(WCHAR));
    if (!copy) {
        if (error)
            error->hr = E_OUTOFMEMORY;
        return nullptr;
    }
    memcpy(copy, text, (SIZE_T)length * sizeof(WCHAR));
    copy[length] = 0;
    ps.s = ps.p = copy;
    ps.e = copy + length;
    ps.ns = ns;
    ps.nscount = nscount;
    ps.xpath = (flags & XPF_XPATH) != 0;
    ps.v6 = (flags & XPF_V6) != 0;
    ps.doc = doc;
    ps.host = host;
    ps.pattern = (flags & XPF_PATTERN) != 0;
    ps.kind = T_EOF;
    ps.prev = T_EOF;
    next_token(&ps);
    HRESULT hr;
    Expr *x = nullptr;
    if (ps.kind == T_EOF) {
        fail(&ps, (HRESULT)XE_EXPR_EXPECTED);
        hr = ps.error;
    } else {
        x = ps.pattern ? parse_union(&ps) : parse_or(&ps);
        if (x && ps.kind != T_EOF && !ps.error)
            fail_expected(&ps, ps.v6 ? "EOF" : "eof");
        hr = ps.error ? ps.error : (x ? S_OK : (HRESULT)XE_EXPR_EXPECTED);
    }
    if (error) {
        error->hr = hr;
        error->start = ps.errstart;
        error->end = ps.errend;
        memcpy(error->p1, ps.errp1, sizeof(error->p1));
        memcpy(error->p2, ps.errp2, sizeof(error->p2));
    }
    if (FAILED(hr)) {
        arena_free(ps.arena);
        xd_free(copy);
        return nullptr;
    }
    XpExpr *e = xd_alloc(sizeof(XpExpr));
    if (!e) {
        arena_free(ps.arena);
        xd_free(copy);
        if (error)
            error->hr = E_OUTOFMEMORY;
        return nullptr;
    }
    e->root = x;
    e->arena = ps.arena;
    e->text = copy;
    e->ns = ns;
    e->nns = nscount;
    e->host = host;
    e->xpath = ps.xpath;
    e->pattern = ps.pattern;
    return e;
}

void xp_free(XpExpr *x)
{
    if (x) {
        arena_free(x->arena);
        xd_free(x->text);
        xd_free(x);
    }
}

static void env_for(XpExpr *x, XpCall *call, Env *env)
{
    memset(env, 0, sizeof(*env));
    env->node = call->node;
    env->pos = call->pos;
    env->size = call->size;
    env->xpath = x->xpath;
    env->root = tree_root(call->node);
    env->ns = x->ns;
    env->nns = x->nns;
    env->text = x->text;
    env->host = call->host ? call->host : x->host;
    env->current = call->current;
}

HRESULT xp_eval(XpExpr *x, XpCall *call, XpVal *out)
{
    Env env;
    env_for(x, call, &env);
    memset(out, 0, sizeof(*out));
    BOOL saved = xd_text_mode(x->host != nullptr);
    HRESULT hr = eval(x->root, &env, out);
    xd_text_mode(saved);
    return hr;
}

HRESULT xp_match(XpExpr *x, XNode *node, XpCall *call, BOOL *matches)
{
    Env env;
    env_for(x, call, &env);
    BOOL saved = xd_text_mode(x->host != nullptr);
    HRESULT hr = match_expr(x->root, node, &env, matches);
    xd_text_mode(saved);
    return hr;
}

static void flatten(Expr *x, Expr **v, UINT *n, UINT max)
{
    if (x->kind == X_UNION) {
        flatten(x->a, v, n, max);
        flatten(x->b, v, n, max);
    } else if (*n < max) {
        v[(*n)++] = x;
    }
}

/* an alternative of a pattern is its own compiled expression that shares the arena of the whole. */
UINT xp_alternatives(XpExpr *x, XpExpr **out, UINT max)
{
    Expr *v[64];
    UINT n = 0;
    flatten(x->root, v, &n, max < 64 ? max : 64);
    for (UINT i = 0; i < n; i++) {
        XpExpr *e = xd_alloc(sizeof(XpExpr));
        if (!e)
            return i;
        *e = *x;
        e->root = v[i];
        e->arena = nullptr;
        e->text = nullptr;
        out[i] = e;
    }
    return n;
}

double xp_priority(XpExpr *x)
{
    Expr *e = x->root;
    if (e->kind != X_PATH || !e->steps)
        return 0.5;
    Step *st = e->steps;
    if (st->next || st->npreds || e->absolute)
        return 0.5;
    switch (st->test) {
    case NT_NAME:
        return 0;
    case NT_ANY:
        return st->uri || st->by_prefix ? -0.25 : -0.5;
    case NT_PI:
        return st->local ? 0 : -0.5;
    }
    return -0.5;
}

/* whether an alternative is a location path pattern: child and attribute steps joined by / and //, or id() or key() first. */
/* whether the expression cannot be a node set. */
BOOL xp_not_nodeset(XpExpr *x)
{
    return expr_not_set(x->root);
}

static BOOL literal_args(const Expr *e)
{
    for (UINT i = 0; i < e->nargs; i++)
        if (e->args[i]->kind != X_STR)
            return FALSE;
    return TRUE;
}

BOOL xp_pattern_ok(XpExpr *x, UINT *start, UINT *end)
{
    Expr *e = x->root;
    *start = *end = 0;
    BOOL has_path = TRUE;
    Expr *call = nullptr;
    if (e->kind == X_FILTER)
        call = e->primary;
    else if (e->kind == X_FUNC || e->kind == X_EXT)
        call = e, has_path = FALSE;
    else if (e->kind != X_PATH)
        return FALSE;
    if (call) {
        BOOL is_id = call->kind == X_FUNC && call->op == F_ID;
        BOOL is_key = call->kind == X_EXT && !call->uri && call->llen == 3 && !memcmp(call->local, L"key", 6);
        if (!is_id && !is_key)
            return FALSE;
        if (!literal_args(call))
            return FALSE;
        if (!has_path)
            return TRUE;
    }
    for (Step *st = e->steps; st; st = st->next) {
        if (st->axis == AX_CHILD || st->axis == AX_ATTRIBUTE)
            continue;
        if (st->axis == AX_DESCENDANT_OR_SELF && st->test == NT_NODE && !st->npreds)
            continue;
        *start = st->tok_start;
        *end = st->tok_end;
        return FALSE;
    }
    return TRUE;
}

/* the name of the function a pattern starts with, id() or key(), else null. */
/* what a pattern can match, from its last step, so a node of another kind or name is skipped without testing it. */
int xp_quick(XpExpr *x, const WCHAR **local, UINT *length)
{
    Expr *e = x->root;
    *local = nullptr;
    *length = 0;
    if (e->kind != X_PATH)
        return XPQ_NONE;
    if (!e->steps)
        return e->absolute ? XPQ_ROOT : XPQ_NONE;
    Step *st = e->steps;
    while (st->next)
        st = st->next;
    BOOL attr = st->axis == AX_ATTRIBUTE;
    switch (st->test) {
    case NT_NAME:
        *local = st->local;
        *length = st->llen;
        return attr ? XPQ_ATTRIBUTE_NAME : XPQ_ELEMENT_NAME;
    case NT_ANY:
        return attr ? XPQ_ATTRIBUTE : XPQ_ELEMENT;
    case NT_TEXT:
        return XPQ_TEXT;
    case NT_COMMENT:
        return XPQ_COMMENT;
    case NT_PI:
        return XPQ_PI;
    }
    return XPQ_NONE;
}

void xs_sort(XSet *s)
{
    sort_unique(s);
}

HRESULT xp_select_in(XNode *ctx, const WCHAR *expr, const XNs *ns, UINT nscount, BOOL xpath, XSet *out)
{
    XpError err;
    XpExpr *x = xp_compile(expr, xd_wlen(expr), ns, nscount, xpath ? XPF_XPATH : 0, nullptr, ctx->doc, &err);
    HRESULT hr = err.hr;
    if (!x)
        return hr;
    XpCall call = {ctx, 1, 1, nullptr, nullptr, nullptr, 0};
    XpVal v = {};
    hr = xp_eval(x, &call, &v);
    if (SUCCEEDED(hr) && v.t != XPV_SET)
        hr = (HRESULT)XE_NOT_NODESET;
    if (SUCCEEDED(hr)) {
        *out = v.set;
        memset(&v.set, 0, sizeof(v.set));
    }
    xpv_free(&v);
    xp_free(x);
    return hr;
}

HRESULT xp_select(XNode *ctx, const WCHAR *expr, XSet *out)
{
    XDoc *doc = ctx->doc;
    return xp_select_in(ctx, expr, doc->sel_ns, doc->sel_ns_count, (doc->flags & XDF_XPATH) != 0, out);
}
