/*
 * xpath 1.0: a parser for expressions and an evaluator over the dom. the selection language "XSLPattern" of msxml3
 * is the same engine with zero based indexes and the dollar operators.
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

/* numbers a tree that is not a document's own, which the document does not remember. */
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

/* document order, each node once. */
static void sort_unique(XSet *s)
{
    if (s->n < 2)
        return;
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

enum { X_NUM, X_STR, X_PATH, X_FILTER, X_FUNC, X_BIN, X_NEG, X_UNION };
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
    /* the token being looked at. */
    int kind;
    const WCHAR *tstart;
    UINT tlen;
    double tnum;
    UINT colon; /* where the colon of a name is, zero for none. */
    BOOL func_follows;
    int prev;
} Parser;

enum {
    T_EOF, T_NUM, T_STR, T_NAME, T_STAR_NAME, T_MUL, T_OP, T_SLASH, T_DSLASH, T_DOT, T_DDOT, T_AT, T_COMMA, T_LP,
    T_RP, T_LB, T_RB, T_PIPE, T_PLUS, T_MINUS, T_EQ, T_NE, T_LT, T_LE, T_GT, T_GE, T_AXIS, T_VAR, T_ERROR
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
    if (!ps->error)
        ps->error = hr;
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
            ps->error = ps->error ? ps->error : (HRESULT)XE_STRING_NOT_CLOSED;
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
    if (kind == T_ERROR && !ps->error)
        ps->error = (HRESULT)XE_XPATH_CHAR;
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
        Expr *x = parse_or(ps);
        if (!x)
            return FALSE;
        if (ps->kind != T_RB)
            return fail(ps, XE_XPATH_INCOMPLETE);
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

/* the namespace a prefix stands for in the selection settings. */
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
        st->element_default_ns = found; /* a wildcard in one namespace. */
        next_token(ps);
    } else if (ps->kind == T_NAME) {
        const WCHAR *name = ps->tstart;
        UINT len = ps->tlen, colon = ps->colon;
        BOOL func = ps->func_follows;
        if (func && !colon && (name_is(name, len, "text") || name_is(name, len, "comment") ||
                               name_is(name, len, "node") || name_is(name, len, "processing-instruction"))) {
            next_token(ps);
            if (ps->kind != T_LP)
                return fail(ps, XE_XPATH_INCOMPLETE), nullptr;
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
                }
            }
            if (ps->kind != T_RP)
                return fail(ps, XE_XPATH_INCOMPLETE), nullptr;
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
                    /* xslpattern takes a prefix nobody declared as the prefix the document uses. */
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
                    /* xslpattern: a name without a prefix is one of the nodes that have none, whatever their
                       namespace. */
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

/* a relative path: steps joined by slashes. first is the step already parsed, may be null. */
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
            fail(ps, XE_XPATH_INCOMPLETE);
            return nullptr;
        }
        next_token(ps);
        return x;
    }
    case T_VAR:
        fail(ps, XE_NO_VARIABLES);
        return nullptr;
    case T_NAME: {
        /* a function call. */
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
        if (id < 0 || (id == F_INDEX && ps->xpath)) {
            fail(ps, XE_BAD_FUNCTION);
            return nullptr;
        }
        next_token(ps);
        if (ps->kind != T_LP) {
            fail(ps, XE_XPATH_INCOMPLETE);
            return nullptr;
        }
        next_token(ps);
        x = new_expr(ps, X_FUNC);
        if (!x)
            return nullptr;
        x->op = id;
        Expr *args[16];
        UINT n = 0;
        if (ps->kind != T_RP) {
            for (;;) {
                if (n == 16) {
                    fail(ps, XE_BAD_ARGUMENTS);
                    return nullptr;
                }
                Expr *a = parse_or(ps);
                if (!a)
                    return nullptr;
                args[n++] = a;
                if (ps->kind == T_COMMA) {
                    next_token(ps);
                    continue;
                }
                break;
            }
        }
        if (ps->kind != T_RP) {
            fail(ps, XE_XPATH_INCOMPLETE);
            return nullptr;
        }
        next_token(ps);
        if (n < min || n > max) {
            fail(ps, XE_BAD_ARGUMENTS);
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
    fail(ps, XE_EXPR_EXPECTED);
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
        merge_steps(path);
        return path;
    }
    if (starts_step(ps)) {
        Expr *path = new_expr(ps, X_PATH);
        if (!path)
            return nullptr;
        if (!parse_steps(ps, path, nullptr))
            return nullptr;
        merge_steps(path);
        return path;
    }
    Expr *primary = parse_primary(ps);
    if (!primary)
        return nullptr;
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
        next_token(ps);
        Expr *right = parse_path_expr(ps);
        if (!right)
            return nullptr;
        Expr *u = new_expr(ps, X_UNION);
        if (!u)
            return nullptr;
        u->a = left;
        u->b = right;
        left = u;
    }
    return left;
}

static Expr *parse_unary(Parser *ps)
{
    if (ps->kind == T_MINUS) {
        next_token(ps);
        Expr *x = parse_unary(ps);
        if (!x)
            return nullptr;
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
        if (!right)
            return nullptr;
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
        if (!right)
            return nullptr;
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

enum { V_SET, V_NUM, V_STR, V_BOOL };

typedef struct Val {
    int t;
    double n;
    BOOL b;
    XBuf s;
    XSet set;
} Val;

typedef struct Env {
    XNode *node;
    UINT pos;
    UINT size;
    BOOL xpath;
    XNode *root;
    HRESULT error;
} Env;

static void val_free(Val *v)
{
    xb_free(&v->s);
    xs_free(&v->set);
}

static void node_string(XNode *n, XBuf *out)
{
    xd_text(n, out);
}

static void val_to_string(Val *v, XBuf *out)
{
    switch (v->t) {
    case V_STR:
        xb_put(out, v->s.s ? v->s.s : L"", v->s.length);
        break;
    case V_NUM:
        xd_format_number(v->n, out);
        break;
    case V_BOOL:
        xb_puta(out, v->b ? "true" : "false");
        break;
    case V_SET:
        if (v->set.n)
            node_string(v->set.v[0], out);
        break;
    }
}

static double val_to_number(Val *v)
{
    switch (v->t) {
    case V_NUM:
        return v->n;
    case V_BOOL:
        return v->b ? 1 : 0;
    default: {
        XBuf b = {};
        val_to_string(v, &b);
        double d = xd_parse_number(b.s ? b.s : L"", b.length);
        xb_free(&b);
        return d;
    }
    }
}

static BOOL val_to_bool(Val *v)
{
    switch (v->t) {
    case V_NUM:
        return v->n != 0 && !xd_isnan(v->n);
    case V_BOOL:
        return v->b;
    case V_STR:
        return v->s.length != 0;
    default:
        return v->set.n != 0;
    }
}

static void val_set_string(Val *v, const WCHAR *s, UINT n)
{
    v->t = V_STR;
    xb_put(&v->s, s, n);
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

/* the nodes of an axis from a node, in the axis' own order, collected when they pass the node test. */
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

static HRESULT eval(Expr *x, Env *env, Val *out);

/* narrows a candidate set by a predicate, the positions counted along the axis. */
static HRESULT apply_predicate(Expr *pred, XSet *set, Env *env, BOOL zero_based)
{
    XSet kept = {};
    for (UINT i = 0; i < set->n; i++) {
        Env sub = *env;
        sub.node = set->v[i];
        sub.pos = i + 1;
        sub.size = set->n;
        Val v = {};
        HRESULT hr = eval(pred, &sub, &v);
        if (FAILED(hr)) {
            val_free(&v);
            xs_free(&kept);
            return hr;
        }
        BOOL keep;
        if (v.t == V_NUM)
            keep = (v.n == (double)(i + 1) - (zero_based ? 1 : 0));
        else
            keep = val_to_bool(&v);
        val_free(&v);
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

static BOOL compare_values(int op, Val *a, Val *b)
{
    BOOL equality = op == OP_EQ || op == OP_NE;
    if (a->t == V_SET && b->t == V_SET) {
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
    if (a->t == V_SET || b->t == V_SET) {
        BOOL swapped = b->t == V_SET;
        Val *set = swapped ? b : a;
        Val *other = swapped ? a : b;
        if (other->t == V_BOOL) {
            BOOL sb = set->set.n != 0;
            return swapped ? (equality ? (op == OP_EQ ? other->b == sb : other->b != sb) : compare_numbers(op, other->b, sb))
                           : (equality ? (op == OP_EQ ? sb == other->b : sb != other->b) : compare_numbers(op, sb, other->b));
        }
        for (UINT i = 0; i < set->set.n; i++) {
            XBuf s = {};
            node_string(set->set.v[i], &s);
            BOOL r;
            if (other->t == V_NUM) {
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
        if (a->t == V_BOOL || b->t == V_BOOL) {
            BOOL x = val_to_bool(a), y = val_to_bool(b);
            return op == OP_EQ ? x == y : x != y;
        }
        if (a->t == V_NUM || b->t == V_NUM)
            return compare_numbers(op, val_to_number(a), val_to_number(b));
        return compare_strings(op, a->s.s ? a->s.s : L"", a->s.length, b->s.s ? b->s.s : L"", b->s.length);
    }
    return compare_numbers(op, val_to_number(a), val_to_number(b));
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
        Val v = {};
        HRESULT hr = eval(x->args[i], env, &v);
        if (SUCCEEDED(hr))
            val_to_string(&v, out);
        val_free(&v);
        return hr;
    }
    node_string(env->node, out);
    return S_OK;
}

static HRESULT arg_number(Expr *x, UINT i, Env *env, double *out)
{
    Val v = {};
    HRESULT hr = eval(x->args[i], env, &v);
    if (SUCCEEDED(hr))
        *out = val_to_number(&v);
    val_free(&v);
    return hr;
}

static HRESULT eval_function(Expr *x, Env *env, Val *out)
{
    HRESULT hr = S_OK;
    XBuf a = {}, b = {}, c = {};
    switch (x->op) {
    case F_LAST:
        out->t = V_NUM;
        out->n = env->size;
        break;
    case F_POSITION:
        out->t = V_NUM;
        out->n = env->pos;
        break;
    case F_INDEX:
        out->t = V_NUM;
        out->n = env->pos ? env->pos - 1 : 0;
        break;
    case F_COUNT: {
        Val v = {};
        hr = eval(x->args[0], env, &v);
        if (SUCCEEDED(hr) && v.t != V_SET)
            hr = (HRESULT)XE_NOT_NODESET;
        out->t = V_NUM;
        out->n = v.set.n;
        val_free(&v);
        break;
    }
    case F_ID:
        out->t = V_SET;
        break;
    case F_LOCAL_NAME:
    case F_NAMESPACE_URI:
    case F_NAME: {
        XNode *n = env->node;
        if (x->nargs) {
            Val v = {};
            hr = eval(x->args[0], env, &v);
            if (SUCCEEDED(hr) && v.t != V_SET)
                hr = (HRESULT)XE_NOT_NODESET;
            n = SUCCEEDED(hr) && v.set.n ? v.set.v[0] : nullptr;
            val_free(&v);
        }
        out->t = V_STR;
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
        out->t = V_STR;
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
            out->t = V_BOOL;
            out->b = at != (UINT)-1;
        } else {
            out->t = V_STR;
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
        out->t = V_STR;
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
        out->t = V_NUM;
        out->n = a.length;
        break;
    case F_NORMALIZE_SPACE: {
        hr = arg_string(x, 0, env, &a);
        out->t = V_STR;
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
        out->t = V_STR;
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
        Val v = {};
        hr = eval(x->args[0], env, &v);
        out->t = V_BOOL;
        out->b = SUCCEEDED(hr) ? val_to_bool(&v) : FALSE;
        if (x->op == F_NOT)
            out->b = !out->b;
        val_free(&v);
        break;
    }
    case F_TRUE:
    case F_FALSE:
        out->t = V_BOOL;
        out->b = x->op == F_TRUE;
        break;
    case F_LANG: {
        hr = arg_string(x, 0, env, &a);
        out->t = V_BOOL;
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
        out->t = V_NUM;
        if (x->nargs) {
            Val v = {};
            hr = eval(x->args[0], env, &v);
            out->n = SUCCEEDED(hr) ? val_to_number(&v) : 0;
            val_free(&v);
        } else {
            node_string(env->node, &a);
            out->n = xd_parse_number(a.s ? a.s : L"", a.length);
        }
        break;
    }
    case F_SUM: {
        Val v = {};
        hr = eval(x->args[0], env, &v);
        if (SUCCEEDED(hr) && v.t != V_SET)
            hr = (HRESULT)XE_NOT_NODESET;
        out->t = V_NUM;
        double total = 0;
        for (UINT i = 0; SUCCEEDED(hr) && i < v.set.n; i++) {
            XBuf s = {};
            node_string(v.set.v[i], &s);
            total += xd_parse_number(s.s ? s.s : L"", s.length);
            xb_free(&s);
        }
        out->n = total;
        val_free(&v);
        break;
    }
    case F_FLOOR:
    case F_CEILING:
    case F_ROUND: {
        double d = 0;
        hr = arg_number(x, 0, env, &d);
        out->t = V_NUM;
        out->n = x->op == F_FLOOR ? xp_floor(d) : x->op == F_CEILING ? xp_ceil(d) : xp_floor(d + 0.5);
        break;
    }
    }
    xb_free(&a);
    xb_free(&b);
    xb_free(&c);
    return hr;
}

static HRESULT eval(Expr *x, Env *env, Val *out)
{
    switch (x->kind) {
    case X_NUM:
        out->t = V_NUM;
        out->n = x->num;
        return S_OK;
    case X_STR:
        val_set_string(out, x->str, x->slen);
        return S_OK;
    case X_FUNC:
        return eval_function(x, env, out);
    case X_NEG: {
        Val v = {};
        HRESULT hr = eval(x->a, env, &v);
        out->t = V_NUM;
        out->n = SUCCEEDED(hr) ? -val_to_number(&v) : 0;
        val_free(&v);
        return hr;
    }
    case X_UNION: {
        Val l = {}, r = {};
        HRESULT hr = eval(x->a, env, &l);
        if (SUCCEEDED(hr))
            hr = eval(x->b, env, &r);
        if (SUCCEEDED(hr) && (l.t != V_SET || r.t != V_SET))
            hr = (HRESULT)XE_NOT_NODESET;
        if (SUCCEEDED(hr)) {
            out->t = V_SET;
            out->set = l.set;
            memset(&l.set, 0, sizeof(l.set));
            for (UINT i = 0; i < r.set.n && SUCCEEDED(hr); i++)
                if (!xs_add(&out->set, r.set.v[i]))
                    hr = E_OUTOFMEMORY;
            sort_unique(&out->set);
        }
        val_free(&l);
        val_free(&r);
        return hr;
    }
    case X_BIN: {
        if (x->op == OP_AND || x->op == OP_OR) {
            Val l = {};
            HRESULT hr = eval(x->a, env, &l);
            if (FAILED(hr)) {
                val_free(&l);
                return hr;
            }
            BOOL lb = val_to_bool(&l);
            val_free(&l);
            out->t = V_BOOL;
            if (x->op == OP_AND && !lb) {
                out->b = FALSE;
                return S_OK;
            }
            if (x->op == OP_OR && lb) {
                out->b = TRUE;
                return S_OK;
            }
            Val r = {};
            hr = eval(x->b, env, &r);
            out->b = SUCCEEDED(hr) && val_to_bool(&r);
            val_free(&r);
            return hr;
        }
        Val l = {}, r = {};
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
                out->t = V_BOOL;
                out->b = compare_values(x->op, &l, &r);
                break;
            default: {
                double a = val_to_number(&l), b = val_to_number(&r);
                out->t = V_NUM;
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
        val_free(&l);
        val_free(&r);
        return hr;
    }
    case X_FILTER: {
        Val v = {};
        HRESULT hr = eval(x->primary, env, &v);
        if (FAILED(hr)) {
            val_free(&v);
            return hr;
        }
        if (v.t != V_SET) {
            if (x->npreds || x->steps) {
                val_free(&v);
                return (HRESULT)XE_NOT_NODESET;
            }
            *out = v;
            return S_OK;
        }
        for (UINT p = 0; p < x->npreds && SUCCEEDED(hr); p++)
            hr = apply_predicate(x->preds[p], &v.set, env, FALSE);
        if (SUCCEEDED(hr) && x->steps)
            hr = run_steps(x->steps, &v.set, env);
        if (FAILED(hr)) {
            val_free(&v);
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
        out->t = V_SET;
        out->set = cur;
        return S_OK;
    }
    }
    return E_FAIL;
}

/* ------------------------------------------------------------------ entry points */

HRESULT xp_select_in(XNode *ctx, const WCHAR *expr, const XNs *ns, UINT nscount, BOOL xpath, XSet *out)
{
    Parser ps = {};
    ps.s = ps.p = expr;
    ps.e = expr + xd_wlen(expr);
    ps.ns = ns;
    ps.nscount = nscount;
    ps.xpath = xpath;
    ps.doc = ctx->doc;
    ps.kind = T_EOF;
    ps.prev = T_EOF;
    next_token(&ps);
    HRESULT hr;
    Expr *x = nullptr;
    if (ps.kind == T_EOF) {
        hr = (HRESULT)XE_EXPR_EXPECTED;
    } else {
        x = parse_or(&ps);
        if (x && ps.kind != T_EOF && !ps.error)
            ps.error = (HRESULT)XE_XPATH_UNEXPECTED;
        hr = ps.error ? ps.error : (x ? S_OK : (HRESULT)XE_EXPR_EXPECTED);
    }
    if (SUCCEEDED(hr)) {
        Env env = {};
        env.node = ctx;
        env.pos = env.size = 1;
        env.xpath = xpath;
        env.root = tree_root(ctx);
        Val v = {};
        hr = eval(x, &env, &v);
        if (SUCCEEDED(hr) && v.t != V_SET)
            hr = (HRESULT)XE_NOT_NODESET;
        if (SUCCEEDED(hr)) {
            *out = v.set;
            memset(&v.set, 0, sizeof(v.set));
        }
        val_free(&v);
    }
    arena_free(ps.arena);
    return hr;
}

HRESULT xp_select(XNode *ctx, const WCHAR *expr, XSet *out)
{
    XDoc *doc = ctx->doc;
    return xp_select_in(ctx, expr, doc->sel_ns, doc->sel_ns_count, (doc->flags & XDF_XPATH) != 0, out);
}
