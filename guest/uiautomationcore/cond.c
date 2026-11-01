/*
 * conditions, cache requests, the views of the tree, searches and tree walkers.
 */
#include "uia.h"

/* ------------------------------------------------------------------ the conditions */

struct COND {
    LONG refs;
    int type;
    int property;
    VARIANT value;
    int flags;
    COND **children;
    int count;
};

COND *CondNew(int type)
{
    COND *c = UiaAlloc(sizeof(*c));
    if (c) {
        c->refs = 1;
        c->type = type;
        VariantInit(&c->value);
    }
    return c;
}

void CondAddRef(COND *c)
{
    if (c)
        InterlockedIncrement(&c->refs);
}

void CondRelease(COND *c)
{
    if (c && !InterlockedDecrement(&c->refs)) {
        for (int i = 0; i < c->count; i++)
            CondRelease(c->children[i]);
        UiaFree(c->children);
        VariantClear(&c->value);
        UiaFree(c);
    }
}

int CondType(COND *c) { return c->type; }
int CondProperty(COND *c) { return c->property; }
int CondFlags(COND *c) { return c->flags; }
int CondCount(COND *c) { return c->count; }
COND *CondChild(COND *c, int i) { return c->children[i]; }
const VARIANT *CondValue(COND *c) { return &c->value; }

HRESULT CondProperty_(int property, const VARIANT *value, int flags, COND **out)
{
    COND *c = CondNew(ConditionType_Property);
    if (!c)
        return E_OUTOFMEMORY;
    c->property = property;
    c->flags = flags;
    HRESULT hr = VariantCopy(&c->value, (VARIANT *)value);
    if (FAILED(hr)) {
        CondRelease(c);
        return hr;
    }
    *out = c;
    return S_OK;
}

HRESULT CondGroup(int type, COND **items, int count, COND **out)
{
    COND *c = CondNew(type);
    if (!c)
        return E_OUTOFMEMORY;
    c->children = count ? UiaAlloc((SIZE_T)count * sizeof(*c->children)) : nullptr;
    if (count && !c->children) {
        CondRelease(c);
        return E_OUTOFMEMORY;
    }
    for (int i = 0; i < count; i++) {
        c->children[i] = items[i];
        CondAddRef(items[i]);
    }
    c->count = count;
    *out = c;
    return S_OK;
}

HRESULT CondNot(COND *child, COND **out)
{
    return CondGroup(ConditionType_Not, &child, 1, out);
}

/* a condition of the flat api, copied. */
HRESULT CondFromStruct(const UiaCondition *in, COND **out)
{
    *out = nullptr;
    if (!in)
        return E_INVALIDARG;
    switch (in->ConditionType) {
    case ConditionType_True:
    case ConditionType_False:
        *out = CondNew(in->ConditionType);
        return *out ? S_OK : E_OUTOFMEMORY;
    case ConditionType_Property: {
        const UiaPropertyCondition *p = (const UiaPropertyCondition *)in;
        const PROPERTY_INFO *info = UiaPropertyInfo(p->PropertyId);
        if (!info || p->Value.vt != info->vt)
            return E_INVALIDARG;
        return CondProperty_(p->PropertyId, &p->Value, p->Flags, out);
    }
    case ConditionType_And:
    case ConditionType_Or: {
        const UiaAndOrCondition *g = (const UiaAndOrCondition *)in;
        COND **items = g->cConditions > 0 ? UiaAlloc((SIZE_T)g->cConditions * sizeof(*items)) : nullptr;
        HRESULT hr = S_OK;
        int made = 0;
        for (int i = 0; i < g->cConditions && SUCCEEDED(hr); i++, made++)
            hr = CondFromStruct(g->ppConditions[i], &items[i]);
        if (SUCCEEDED(hr))
            hr = CondGroup(in->ConditionType, items, made, out);
        for (int i = 0; i < made; i++)
            CondRelease(items[i]);
        UiaFree(items);
        return hr;
    }
    case ConditionType_Not: {
        const UiaNotCondition *n = (const UiaNotCondition *)in;
        COND *child = nullptr;
        HRESULT hr = CondFromStruct(n->pConditions, &child);
        if (SUCCEEDED(hr)) {
            hr = CondNot(child, out);
            CondRelease(child);
        }
        return hr;
    }
    }
    return E_INVALIDARG;
}

static BOOL substring_match(const WCHAR *hay, const WCHAR *needle, BOOL ignore_case)
{
    SIZE_T hn = wcslen(hay), nn = wcslen(needle);
    if (nn > hn)
        return FALSE;
    for (SIZE_T i = 0; i + nn <= hn; i++)
        if ((ignore_case ? _wcsnicmp(hay + i, needle, nn) : wcsncmp(hay + i, needle, nn)) == 0)
            return TRUE;
    return FALSE;
}

static HRESULT match_property(COND *c, NODE *n, BOOL *match)
{
    *match = FALSE;
    VARIANT actual;
    HRESULT hr = NodeGetProperty(n, c->property, FALSE, &actual);
    if (FAILED(hr))
        return hr;
    VARIANT want;
    VariantInit(&want);
    VariantCopy(&want, &c->value);
    if (actual.vt == VT_BSTR) {
        const WCHAR *a = actual.bstrVal ? actual.bstrVal : L"";
        const WCHAR *w = want.vt == VT_BSTR && want.bstrVal ? want.bstrVal : L"";
        BOOL ic = (c->flags & PropertyConditionFlags_IgnoreCase) != 0;
        if (c->flags & PropertyConditionFlags_MatchSubstring)
            *match = substring_match(a, w, ic);
        else
            *match = (ic ? _wcsicmp(a, w) : wcscmp(a, w)) == 0;
    } else if (actual.vt == (VT_ARRAY | VT_I4) && want.vt == (VT_ARRAY | VT_I4)) {
        int *ai = nullptr, *wi = nullptr, an = 0, wn = 0;
        UiaSafeArrayToIntArray(actual.parray, &ai, &an);
        UiaSafeArrayToIntArray(want.parray, &wi, &wn);
        *match = !UiaCompareRuntimeIds(ai, an, wi, wn);
        UiaFree(ai);
        UiaFree(wi);
    } else if (actual.vt == VT_UNKNOWN) {
        NODE *other = nullptr;
        if (want.vt == VT_UNKNOWN && want.punkVal && SUCCEEDED(NodeFromUnknown(want.punkVal, &other)) && other) {
            NODE *mine = nullptr;
            if (actual.punkVal && SUCCEEDED(NodeFromUnknown(actual.punkVal, &mine)) && mine) {
                *match = NodeSame(mine, other);
                NodeRelease(mine);
            }
            NodeRelease(other);
        } else {
            *match = !actual.punkVal && (want.vt == VT_EMPTY || (want.vt == VT_UNKNOWN && !want.punkVal));
        }
    } else if (actual.vt == VT_R8) {
        VARIANT w;
        VariantInit(&w);
        if (SUCCEEDED(VariantChangeType(&w, &want, 0, VT_R8)))
            *match = w.dblVal == actual.dblVal;
    } else {
        VARIANT w;
        VariantInit(&w);
        if (SUCCEEDED(VariantChangeType(&w, &want, 0, actual.vt)))
            *match = actual.vt == VT_BOOL ? (w.boolVal != 0) == (actual.boolVal != 0) : actual.lVal == w.lVal;
    }
    VariantClear(&want);
    VariantClear(&actual);
    return S_OK;
}

HRESULT CondMatch(COND *c, NODE *n, BOOL *match)
{
    HRESULT hr = S_OK;
    switch (c->type) {
    case ConditionType_True:
        *match = TRUE;
        return S_OK;
    case ConditionType_False:
        *match = FALSE;
        return S_OK;
    case ConditionType_Property:
        return match_property(c, n, match);
    case ConditionType_And:
        *match = TRUE;
        for (int i = 0; i < c->count && *match; i++) {
            hr = CondMatch(c->children[i], n, match);
            if (FAILED(hr))
                return hr;
        }
        return S_OK;
    case ConditionType_Or:
        *match = FALSE;
        for (int i = 0; i < c->count && !*match; i++) {
            hr = CondMatch(c->children[i], n, match);
            if (FAILED(hr))
                return hr;
        }
        return S_OK;
    case ConditionType_Not:
        hr = CondMatch(c->children[0], n, match);
        *match = !*match;
        return hr;
    }
    return E_INVALIDARG;
}

/* ------------------------------------------------------------------ the views of the tree */

static HRESULT matches_view(COND *view, NODE *n, BOOL *match)
{
    if (!view) {
        *match = TRUE;
        return S_OK;
    }
    return CondMatch(view, n, match);
}

/* the parent in a view: the nearest ancestor the view shows. */
HRESULT ViewParent(NODE *n, COND *view, NODE **out)
{
    *out = nullptr;
    NODE *current = n;
    NodeAddRef(current);
    for (int guard = 0; guard < 4096; guard++) {
        NODE *parent = nullptr;
        HRESULT hr = NodeNavigate(current, NavigateDirection_Parent, &parent);
        NodeRelease(current);
        if (FAILED(hr))
            return hr;
        if (!parent)
            return S_OK;
        BOOL match;
        hr = matches_view(view, parent, &match);
        if (FAILED(hr) || match) {
            *out = parent;
            return hr;
        }
        current = parent;
    }
    return S_OK;
}

static HRESULT append(NODE ***list, int *count, int *capacity, NODE *node)
{
    if (*count == *capacity) {
        int grown = *capacity ? *capacity * 2 : 8;
        NODE **bigger = UiaAlloc((SIZE_T)grown * sizeof(*bigger));
        if (!bigger)
            return E_OUTOFMEMORY;
        if (*count)
            memcpy(bigger, *list, (SIZE_T)*count * sizeof(*bigger));
        UiaFree(*list);
        *list = bigger;
        *capacity = grown;
    }
    (*list)[(*count)++] = node;
    return S_OK;
}

void NodeListFree(NODE **list, int count)
{
    for (int i = 0; i < count; i++)
        NodeRelease(list[i]);
    UiaFree(list);
}

/* children in a view: the shown children, with those of hidden children in their place. */
static HRESULT view_children_into(NODE *n, COND *view, NODE ***list, int *count, int *capacity, int depth)
{
    NODE *child = nullptr;
    HRESULT hr = NodeNavigate(n, NavigateDirection_FirstChild, &child);
    for (int guard = 0; SUCCEEDED(hr) && child && guard < 100000; guard++) {
        BOOL match;
        hr = matches_view(view, child, &match);
        NODE *next = nullptr;
        if (SUCCEEDED(hr)) {
            if (match) {
                NodeAddRef(child);
                hr = append(list, count, capacity, child);
            } else if (depth < 64) {
                hr = view_children_into(child, view, list, count, capacity, depth + 1);
            }
        }
        if (SUCCEEDED(hr))
            hr = NodeNavigate(child, NavigateDirection_NextSibling, &next);
        NodeRelease(child);
        child = next;
    }
    NodeRelease(child);
    return hr;
}

HRESULT ViewChildren(NODE *n, COND *view, NODE ***list, int *count)
{
    *list = nullptr;
    *count = 0;
    int capacity = 0;
    HRESULT hr = view_children_into(n, view, list, count, &capacity, 0);
    if (FAILED(hr)) {
        NodeListFree(*list, *count);
        *list = nullptr;
        *count = 0;
    }
    return hr;
}

HRESULT ViewStep(NODE *n, COND *view, int direction, NODE **out)
{
    *out = nullptr;
    if (direction == NavigateDirection_Parent)
        return ViewParent(n, view, out);
    if (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild) {
        NODE **list;
        int count;
        HRESULT hr = ViewChildren(n, view, &list, &count);
        if (SUCCEEDED(hr) && count) {
            int pick = direction == NavigateDirection_FirstChild ? 0 : count - 1;
            *out = list[pick];
            list[pick] = nullptr;
        }
        if (list)
            NodeListFree(list, count);
        return hr;
    }
    NODE *parent = nullptr;
    HRESULT hr = ViewParent(n, view, &parent);
    if (FAILED(hr) || !parent)
        return hr;
    NODE **list;
    int count;
    hr = ViewChildren(parent, view, &list, &count);
    NodeRelease(parent);
    if (FAILED(hr))
        return hr;
    for (int i = 0; i < count; i++) {
        if (!NodeSame(list[i], n))
            continue;
        int at = direction == NavigateDirection_NextSibling ? i + 1 : i - 1;
        if (at >= 0 && at < count) {
            *out = list[at];
            list[at] = nullptr;
        }
        break;
    }
    NodeListFree(list, count);
    return S_OK;
}

/* ------------------------------------------------------------------ searching */

typedef struct SEARCH {
    COND *condition;
    BOOL first;
    BOOL exclude_root;
    int max_depth;
    int options;
    NODE **found;
    int count, capacity;
    BOOL done;
} SEARCH;

static HRESULT search_node(SEARCH *s, NODE *n, int depth, BOOL is_root);

static HRESULT search_children(SEARCH *s, NODE *n, int depth)
{
    if (s->max_depth >= 0 && depth > s->max_depth)
        return S_OK;
    NODE **kids = nullptr;
    int count = 0, capacity = 0;
    NODE *child = nullptr;
    NavigateDirection first = s->options & TreeTraversalOptions_LastToFirstOrder ? NavigateDirection_LastChild
                                                                                : NavigateDirection_FirstChild;
    NavigateDirection step = s->options & TreeTraversalOptions_LastToFirstOrder ? NavigateDirection_PreviousSibling
                                                                               : NavigateDirection_NextSibling;
    HRESULT hr = NodeNavigate(n, first, &child);
    while (SUCCEEDED(hr) && child && count < 100000) {
        NODE *next = nullptr;
        hr = append(&kids, &count, &capacity, child);
        if (SUCCEEDED(hr))
            hr = NodeNavigate(child, step, &next);
        child = next;
    }
    NodeRelease(child);
    for (int i = 0; SUCCEEDED(hr) && i < count && !s->done; i++)
        hr = search_node(s, kids[i], depth, FALSE);
    NodeListFree(kids, count);
    return hr;
}

static HRESULT search_node(SEARCH *s, NODE *n, int depth, BOOL is_root)
{
    BOOL post = (s->options & TreeTraversalOptions_PostOrder) != 0;
    HRESULT hr = S_OK;
    if (!post && !(is_root && s->exclude_root)) {
        BOOL match;
        hr = CondMatch(s->condition, n, &match);
        if (FAILED(hr))
            return hr;
        if (match) {
            NodeAddRef(n);
            hr = append(&s->found, &s->count, &s->capacity, n);
            if (s->first)
                s->done = TRUE;
        }
    }
    if (SUCCEEDED(hr) && !s->done)
        hr = search_children(s, n, depth + 1);
    if (SUCCEEDED(hr) && post && !s->done && !(is_root && s->exclude_root)) {
        BOOL match;
        hr = CondMatch(s->condition, n, &match);
        if (SUCCEEDED(hr) && match) {
            NodeAddRef(n);
            hr = append(&s->found, &s->count, &s->capacity, n);
            if (s->first)
                s->done = TRUE;
        }
    }
    return hr;
}

/* the nodes of a scope below a node that satisfy a condition. */
HRESULT NodeFind(NODE *root, int scope, COND *condition, BOOL first, int options, NODE *ancestors_root, NODE ***list,
                 int *count)
{
    *list = nullptr;
    *count = 0;
    SEARCH s = {};
    s.condition = condition;
    s.first = first;
    s.options = options;
    s.max_depth = -1;
    HRESULT hr = S_OK;
    if (scope & TreeScope_Element) {
        BOOL match;
        hr = CondMatch(condition, root, &match);
        if (SUCCEEDED(hr) && match) {
            NodeAddRef(root);
            hr = append(&s.found, &s.count, &s.capacity, root);
        }
    }
    if (SUCCEEDED(hr) && !(first && s.count) && (scope & (TreeScope_Children | TreeScope_Descendants))) {
        s.exclude_root = TRUE;
        s.max_depth = scope & TreeScope_Descendants ? -1 : 1;
        hr = search_node(&s, root, 0, TRUE);
    }
    if (SUCCEEDED(hr) && !(first && s.count) && (scope & (TreeScope_Parent | TreeScope_Ancestors))) {
        NODE *current = root;
        NodeAddRef(current);
        for (int guard = 0; SUCCEEDED(hr) && guard < 256 && !(first && s.count); guard++) {
            NODE *parent = nullptr;
            hr = NodeNavigate(current, NavigateDirection_Parent, &parent);
            NodeRelease(current);
            current = parent;
            if (FAILED(hr) || !parent)
                break;
            BOOL match;
            hr = CondMatch(condition, parent, &match);
            if (SUCCEEDED(hr) && match) {
                NodeAddRef(parent);
                hr = append(&s.found, &s.count, &s.capacity, parent);
            }
            if (ancestors_root && NodeSame(parent, ancestors_root))
                break;
            if (!(scope & TreeScope_Ancestors))
                break;
        }
        NodeRelease(current);
    }
    if (FAILED(hr)) {
        NodeListFree(s.found, s.count);
        return hr;
    }
    *list = s.found;
    *count = s.count;
    return S_OK;
}
