/* queries: the select form of wql, run against the instances of a class and its subclasses. */
#include "wbemcore.h"

typedef enum QKind { Q_EOF, Q_WORD, Q_STRING, Q_NUMBER, Q_OP, Q_ERROR } QKind;

typedef struct QTok {
    QKind kind;
    WCHAR *text;
} QTok;

typedef struct Query {
    const WCHAR *src;
    SIZE_T pos;
    QTok tok;
    HRESULT error;
} Query;

static void q_free_tok(QTok *t)
{
    wb_free(t->text);
    t->text = nullptr;
}

static void q_next(Query *q)
{
    q_free_tok(&q->tok);
    while (q->src[q->pos] == ' ' || q->src[q->pos] == '\t' || q->src[q->pos] == '\r' || q->src[q->pos] == '\n')
        q->pos++;
    WCHAR c = q->src[q->pos];
    if (!c) {
        q->tok.kind = Q_EOF;
        return;
    }
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c > 0x7F) {
        SIZE_T start = q->pos;
        while ((q->src[q->pos] >= 'a' && q->src[q->pos] <= 'z') || (q->src[q->pos] >= 'A' && q->src[q->pos] <= 'Z') ||
               (q->src[q->pos] >= '0' && q->src[q->pos] <= '9') || q->src[q->pos] == '_' || q->src[q->pos] > 0x7F ||
               (q->src[q->pos] == '.' && q->src[q->pos + 1] != ' '))
            q->pos++;
        q->tok.kind = Q_WORD;
        q->tok.text = wb_wcsndup(q->src + start, q->pos - start);
        return;
    }
    if (c == '\'' || c == '"') {
        WBuf buf = {0};
        q->pos++;
        for (;;) {
            WCHAR d = q->src[q->pos];
            if (!d) {
                wb_free(buf.text);
                q->tok.kind = Q_ERROR;
                return;
            }
            q->pos++;
            if (d == c)
                break;
            if (d == '\\' && q->src[q->pos]) {
                d = q->src[q->pos++];
                if (d == 'n')
                    d = '\n';
                else if (d == 't')
                    d = '\t';
            }
            wb_buf_addc(&buf, d);
        }
        q->tok.kind = Q_STRING;
        q->tok.text = wb_buf_take(&buf);
        return;
    }
    if ((c >= '0' && c <= '9') || ((c == '-' || c == '+') && q->src[q->pos + 1] >= '0' && q->src[q->pos + 1] <= '9')) {
        SIZE_T start = q->pos;
        q->pos++;
        while ((q->src[q->pos] >= '0' && q->src[q->pos] <= '9') || q->src[q->pos] == '.' || q->src[q->pos] == 'x' ||
               q->src[q->pos] == 'X' || (q->src[q->pos] >= 'a' && q->src[q->pos] <= 'f') ||
               (q->src[q->pos] >= 'A' && q->src[q->pos] <= 'F'))
            q->pos++;
        q->tok.kind = Q_NUMBER;
        q->tok.text = wb_wcsndup(q->src + start, q->pos - start);
        return;
    }
    SIZE_T start = q->pos++;
    if ((c == '<' && (q->src[q->pos] == '>' || q->src[q->pos] == '=')) || (c == '>' && q->src[q->pos] == '=') ||
        (c == '!' && q->src[q->pos] == '='))
        q->pos++;
    q->tok.kind = Q_OP;
    q->tok.text = wb_wcsndup(q->src + start, q->pos - start);
}

static BOOL q_word(Query *q, const WCHAR *word)
{
    return q->tok.kind == Q_WORD && wb_eq(q->tok.text, word);
}

static BOOL q_op(Query *q, const WCHAR *op)
{
    return q->tok.kind == Q_OP && !wcscmp(q->tok.text, op);
}

/* ------------------------------------------------------------------ the condition tree */

typedef enum NodeKind { N_AND, N_OR, N_NOT, N_COMPARE, N_ISNULL, N_ISA } NodeKind;

typedef struct Node {
    NodeKind kind;
    struct Node *left, *right;
    WCHAR *prop;
    WCHAR *op;
    WCHAR *literal;
    BOOL literal_is_string, literal_is_null, negate;
    BOOL literal_bool;
} Node;

static void node_free(Node *n)
{
    if (!n)
        return;
    node_free(n->left);
    node_free(n->right);
    wb_free(n->prop);
    wb_free(n->op);
    wb_free(n->literal);
    wb_free(n);
}

static Node *parse_or(Query *q);

static Node *new_node(NodeKind kind)
{
    Node *n = wb_alloc(sizeof(Node));
    n->kind = kind;
    return n;
}

static Node *parse_primary(Query *q)
{
    if (q_op(q, L"(")) {
        q_next(q);
        Node *inner = parse_or(q);
        if (!inner)
            return nullptr;
        if (!q_op(q, L")")) {
            q->error = WBEM_E_INVALID_QUERY;
            node_free(inner);
            return nullptr;
        }
        q_next(q);
        return inner;
    }
    if (q_word(q, L"NOT")) {
        q_next(q);
        Node *n = new_node(N_NOT);
        n->left = parse_primary(q);
        if (!n->left) {
            node_free(n);
            return nullptr;
        }
        return n;
    }
    if (q->tok.kind != Q_WORD) {
        q->error = WBEM_E_INVALID_QUERY;
        return nullptr;
    }
    WCHAR *prop = wb_wcsdup(q->tok.text);
    q_next(q);
    if (q_word(q, L"IS")) {
        q_next(q);
        Node *n = new_node(N_ISNULL);
        n->prop = prop;
        if (q_word(q, L"NOT")) {
            n->negate = TRUE;
            q_next(q);
        }
        if (!q_word(q, L"NULL")) {
            q->error = WBEM_E_INVALID_QUERY;
            node_free(n);
            return nullptr;
        }
        q_next(q);
        return n;
    }
    if (q_word(q, L"ISA")) {
        q_next(q);
        Node *n = new_node(N_ISA);
        n->prop = prop;
        if (q->tok.kind != Q_STRING) {
            q->error = WBEM_E_INVALID_QUERY;
            node_free(n);
            return nullptr;
        }
        n->literal = wb_wcsdup(q->tok.text);
        q_next(q);
        return n;
    }
    Node *n = new_node(N_COMPARE);
    n->prop = prop;
    if (q_word(q, L"NOT")) {
        n->negate = TRUE;
        q_next(q);
    }
    if (q_word(q, L"LIKE")) {
        n->op = wb_wcsdup(L"LIKE");
        q_next(q);
    } else if (q->tok.kind == Q_OP && (q_op(q, L"=") || q_op(q, L"<>") || q_op(q, L"!=") || q_op(q, L"<") || q_op(q, L">") ||
                                       q_op(q, L"<=") || q_op(q, L">="))) {
        n->op = wb_wcsdup(q->tok.text);
        q_next(q);
    } else {
        q->error = WBEM_E_INVALID_QUERY;
        node_free(n);
        return nullptr;
    }
    if (q->tok.kind == Q_STRING) {
        n->literal_is_string = TRUE;
        n->literal = wb_wcsdup(q->tok.text);
    } else if (q->tok.kind == Q_NUMBER) {
        n->literal = wb_wcsdup(q->tok.text);
    } else if (q_word(q, L"TRUE") || q_word(q, L"FALSE")) {
        n->literal_bool = q_word(q, L"TRUE");
        n->literal = wb_wcsdup(q->tok.text);
    } else if (q_word(q, L"NULL")) {
        n->literal_is_null = TRUE;
    } else {
        q->error = WBEM_E_INVALID_QUERY;
        node_free(n);
        return nullptr;
    }
    q_next(q);
    return n;
}

static Node *parse_and(Query *q)
{
    Node *left = parse_primary(q);
    while (left && q_word(q, L"AND")) {
        q_next(q);
        Node *n = new_node(N_AND);
        n->left = left;
        n->right = parse_primary(q);
        left = n;
        if (!n->right) {
            node_free(left);
            return nullptr;
        }
    }
    return left;
}

static Node *parse_or(Query *q)
{
    Node *left = parse_and(q);
    while (left && q_word(q, L"OR")) {
        q_next(q);
        Node *n = new_node(N_OR);
        n->left = left;
        n->right = parse_and(q);
        left = n;
        if (!n->right) {
            node_free(left);
            return nullptr;
        }
    }
    return left;
}

/* ------------------------------------------------------------------ evaluation */

/* like with %, _ and [set] against a name, ignoring case. */
static BOOL like(const WCHAR *text, const WCHAR *pattern)
{
    for (; *pattern; pattern++) {
        if (*pattern == '%') {
            while (pattern[1] == '%')
                pattern++;
            if (!pattern[1])
                return TRUE;
            for (; *text; text++)
                if (like(text, pattern + 1))
                    return TRUE;
            return like(text, pattern + 1);
        }
        if (!*text)
            return FALSE;
        if (*pattern == '_') {
            text++;
            continue;
        }
        if (*pattern == '[') {
            const WCHAR *end = wcschr(pattern, ']');
            if (end) {
                BOOL negate = pattern[1] == '^';
                BOOL hit = FALSE;
                for (const WCHAR *s = pattern + 1 + (negate ? 1 : 0); s < end; s++) {
                    if (s[1] == '-' && s + 2 < end) {
                        if (towlower(*text) >= towlower(*s) && towlower(*text) <= towlower(s[2]))
                            hit = TRUE;
                        s += 2;
                    } else if (towlower(*s) == towlower(*text)) {
                        hit = TRUE;
                    }
                }
                if (hit == negate)
                    return FALSE;
                text++;
                pattern = end;
                continue;
            }
        }
        if (towlower(*pattern) != towlower(*text))
            return FALSE;
        text++;
    }
    return !*text;
}

static HRESULT eval_compare(const Node *n, const WObj *o, BOOL *result)
{
    VARIANT value;
    CIMTYPE type;
    wv_init(&value);
    const WProp *p = wo_prop(o, n->prop);
    if (p) {
        type = p->type;
        wv_copy(&value, &p->value);
    } else if (!wo_system_prop(o, n->prop, &value, &type)) {
        return WBEM_E_INVALID_QUERY;
    }
    BOOL answer = FALSE;
    if (n->literal_is_null) {
        BOOL null = wv_is_null(&value);
        answer = !wcscmp(n->op, L"=") ? null : !null;
    } else if (!wv_is_null(&value)) {
        if (!wcscmp(n->op, L"LIKE")) {
            VARIANT text;
            wv_init(&text);
            if (SUCCEEDED(wv_coerce(&text, &value, CIM_STRING)) && text.vt == VT_BSTR)
                answer = like(text.bstrVal, n->literal);
            VariantClear(&text);
        } else {
            VARIANT raw, lit;
            VariantInit(&raw);
            wv_init(&lit);
            if (n->literal_is_string || n->literal_bool || (type & ~CIM_FLAG_ARRAY) == CIM_BOOLEAN) {
                raw.vt = VT_BSTR;
                raw.bstrVal = SysAllocString(n->literal);
            } else {
                raw.vt = VT_BSTR;
                raw.bstrVal = SysAllocString(n->literal);
            }
            CIMTYPE base = type & ~CIM_FLAG_ARRAY;
            HRESULT hr = wv_coerce(&lit, &raw, base);
            VariantClear(&raw);
            if (FAILED(hr)) {
                VariantClear(&value);
                return WBEM_E_INVALID_QUERY;
            }
            int order;
            if (type & CIM_FLAG_ARRAY) {
                answer = FALSE;
            } else if (wv_compare(&value, &lit, base, &order)) {
                const WCHAR *op = n->op;
                if (!wcscmp(op, L"="))
                    answer = order == 0;
                else if (!wcscmp(op, L"<>") || !wcscmp(op, L"!="))
                    answer = order != 0;
                else if (!wcscmp(op, L"<"))
                    answer = order < 0;
                else if (!wcscmp(op, L">"))
                    answer = order > 0;
                else if (!wcscmp(op, L"<="))
                    answer = order <= 0;
                else if (!wcscmp(op, L">="))
                    answer = order >= 0;
            }
            VariantClear(&lit);
        }
    }
    VariantClear(&value);
    *result = n->negate ? !answer : answer;
    return S_OK;
}

static HRESULT eval(const Node *n, const WObj *o, BOOL *result)
{
    BOOL a, b;
    HRESULT hr;
    switch (n->kind) {
    case N_AND:
    case N_OR:
        hr = eval(n->left, o, &a);
        if (FAILED(hr))
            return hr;
        if (n->kind == N_AND && !a) {
            *result = FALSE;
            return S_OK;
        }
        if (n->kind == N_OR && a) {
            *result = TRUE;
            return S_OK;
        }
        hr = eval(n->right, o, &b);
        *result = b;
        return hr;
    case N_NOT:
        hr = eval(n->left, o, &a);
        *result = !a;
        return hr;
    case N_ISNULL: {
        VARIANT value;
        wv_init(&value);
        const WProp *p = wo_prop(o, n->prop);
        if (p)
            wv_copy(&value, &p->value);
        else if (!wo_system_prop(o, n->prop, &value, nullptr))
            return WBEM_E_INVALID_QUERY;
        BOOL null = wv_is_null(&value);
        VariantClear(&value);
        *result = n->negate ? !null : null;
        return S_OK;
    }
    case N_ISA:
        *result = wo_inherits(o, n->literal);
        return S_OK;
    case N_COMPARE:
        return eval_compare(n, o, result);
    }
    return WBEM_E_INVALID_QUERY;
}

/* ------------------------------------------------------------------ statements */

typedef struct SelectState {
    Node *where;
    WCHAR **select;
    UINT select_count;
    BOOL all;
    WEmit emit;
    void *context;
    HRESULT error;
} SelectState;

static BOOL selected(const SelectState *s, const WCHAR *name)
{
    for (UINT i = 0; i < s->select_count; i++)
        if (wb_eq(s->select[i], name))
            return TRUE;
    return FALSE;
}

static BOOL select_emit(void *context, WObj *object)
{
    SelectState *s = context;
    BOOL keep = TRUE;
    if (s->where) {
        HRESULT hr = eval(s->where, object, &keep);
        if (FAILED(hr)) {
            s->error = hr;
            return FALSE;
        }
    }
    if (!keep)
        return TRUE;
    if (s->all)
        return s->emit(s->context, object);
    WObj *trimmed = wo_clone(object);
    for (UINT i = trimmed->prop_count; i-- > 0;) {
        WProp *p = &trimmed->props[i];
        if (!selected(s, p->name) && !wq_true(&p->quals, L"key"))
            wo_remove_prop(trimmed, p->name);
    }
    BOOL more = s->emit(s->context, trimmed);
    wo_free(trimmed);
    return more;
}

HRESULT wq_execute(WNs *ns, const WCHAR *text, LONG flags, WEmit emit, void *context)
{
    Query q;
    memset(&q, 0, sizeof(q));
    q.src = text;
    q_next(&q);
    HRESULT hr = S_OK;
    SelectState state;
    memset(&state, 0, sizeof(state));
    state.emit = emit;
    state.context = context;
    WCHAR *class_name = nullptr;
    if (q_word(&q, L"SELECT")) {
        q_next(&q);
        if (q_op(&q, L"*")) {
            state.all = TRUE;
            q_next(&q);
        } else {
            for (;;) {
                if (q.tok.kind != Q_WORD) {
                    hr = WBEM_E_INVALID_QUERY;
                    break;
                }
                state.select = wb_realloc(state.select, (state.select_count + 1) * sizeof(WCHAR *));
                state.select[state.select_count++] = wb_wcsdup(q.tok.text);
                q_next(&q);
                if (q_op(&q, L",")) {
                    q_next(&q);
                    continue;
                }
                break;
            }
            for (UINT i = 0; i < state.select_count; i++)
                if (!wcscmp(state.select[i], L"*"))
                    state.all = TRUE;
        }
        if (SUCCEEDED(hr) && !q_word(&q, L"FROM"))
            hr = WBEM_E_INVALID_QUERY;
        if (SUCCEEDED(hr)) {
            q_next(&q);
            if (q.tok.kind != Q_WORD)
                hr = WBEM_E_INVALID_QUERY;
            else
                class_name = wb_wcsdup(q.tok.text);
            q_next(&q);
        }
        if (SUCCEEDED(hr) && q_word(&q, L"WHERE")) {
            q_next(&q);
            state.where = parse_or(&q);
            if (!state.where)
                hr = FAILED(q.error) ? q.error : WBEM_E_INVALID_QUERY;
        }
        if (SUCCEEDED(hr) && q.tok.kind != Q_EOF)
            hr = WBEM_E_INVALID_QUERY;
    } else if (q_word(&q, L"ASSOCIATORS") || q_word(&q, L"REFERENCES")) {
        hr = WBEM_E_NOT_SUPPORTED;
    } else {
        hr = WBEM_E_INVALID_QUERY;
    }
    if (SUCCEEDED(hr) && q.tok.kind == Q_ERROR)
        hr = WBEM_E_INVALID_QUERY;
    if (SUCCEEDED(hr)) {
        if (!wr_find_class(ns, class_name)) {
            hr = WBEM_E_INVALID_CLASS;
        } else {
            hr = wprov_enum(ns, class_name, TRUE, select_emit, &state);
            if (SUCCEEDED(hr) && FAILED(state.error))
                hr = state.error;
        }
    }
    node_free(state.where);
    for (UINT i = 0; i < state.select_count; i++)
        wb_free(state.select[i]);
    wb_free(state.select);
    wb_free(class_name);
    q_free_tok(&q.tok);
    return hr;
}
