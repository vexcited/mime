/*
 * the RegExp object of vbscript: a backtracking matcher for the syntax of javascript regular expressions, and the
 * collections of matches it returns.
 */
#include "vbs.h"

/* ------------------------------------------------------------------ the pattern */

enum { N_CHAR, N_ANY, N_CLASS, N_BOL, N_EOL, N_WORDB, N_NWORDB, N_GROUP, N_BACKREF, N_ALT, N_REPEAT, N_LOOK };

typedef struct ClassRange {
    WCHAR Lo, Hi;
} ClassRange;

typedef struct Node {
    BYTE Type;
    BOOL Negate, Greedy, Capture;
    WCHAR Char;
    LONG Min, Max, Index;
    ClassRange *Ranges;
    UINT RangeCount;
    struct Node *Next, *Child;
    struct Node **Branches;
    UINT BranchCount;
} Node;

typedef struct Regex {
    Node *Program;
    UINT Groups;
    BOOL IgnoreCase, Multiline;
    int Error;
    Node **All;
    UINT AllCount, AllCapacity;
} Regex;

typedef struct Compiler {
    Regex *R;
    PCWSTR S;
    UINT Pos, Len;
    int Error;
} Compiler;

static Node *new_node(Compiler *c, BYTE type)
{
    Node *n = vbs_alloc(sizeof(Node));
    n->Type = type;
    Regex *r = c->R;
    if (r->AllCount == r->AllCapacity) {
        r->AllCapacity = r->AllCapacity ? r->AllCapacity * 2 : 32;
        r->All = vbs_realloc(r->All, r->AllCapacity * sizeof(Node *));
    }
    r->All[r->AllCount++] = n;
    return n;
}

static void add_range(Node *n, WCHAR lo, WCHAR hi)
{
    n->Ranges = vbs_realloc(n->Ranges, (n->RangeCount + 1) * sizeof(ClassRange));
    n->Ranges[n->RangeCount].Lo = lo;
    n->Ranges[n->RangeCount].Hi = hi;
    n->RangeCount++;
}

static Node *parse_alt(Compiler *c);

static int hexv(WCHAR ch)
{
    if (ch >= L'0' && ch <= L'9')
        return ch - L'0';
    if (ch >= L'a' && ch <= L'f')
        return ch - L'a' + 10;
    if (ch >= L'A' && ch <= L'F')
        return ch - L'A' + 10;
    return -1;
}

/* adds the ranges a class escape such as \d stands for. */
static BOOL class_escape(Node *n, WCHAR e)
{
    switch (e) {
    case L'd': add_range(n, L'0', L'9'); return TRUE;
    case L'w':
        add_range(n, L'0', L'9');
        add_range(n, L'A', L'Z');
        add_range(n, L'a', L'z');
        add_range(n, L'_', L'_');
        return TRUE;
    case L's':
        add_range(n, 9, 13);
        add_range(n, 32, 32);
        add_range(n, 0xA0, 0xA0);
        add_range(n, 0x1680, 0x1680);
        add_range(n, 0x2000, 0x200A);
        add_range(n, 0x2028, 0x2029);
        add_range(n, 0x202F, 0x202F);
        add_range(n, 0x205F, 0x205F);
        add_range(n, 0x3000, 0x3000);
        add_range(n, 0xFEFF, 0xFEFF);
        return TRUE;
    }
    return FALSE;
}

/* inverts the ranges of a node over the whole character space. */
static void invert_ranges(Node *n)
{
    UINT count = n->RangeCount;
    ClassRange *old = n->Ranges;
    for (UINT i = 0; i < count; i++)
        for (UINT j = i + 1; j < count; j++)
            if (old[j].Lo < old[i].Lo) {
                ClassRange t = old[i];
                old[i] = old[j];
                old[j] = t;
            }
    n->Ranges = nullptr;
    n->RangeCount = 0;
    UINT next = 0;
    for (UINT i = 0; i < count; i++) {
        if (old[i].Lo > next)
            add_range(n, (WCHAR)next, old[i].Lo - 1);
        if ((UINT)old[i].Hi + 1 > next)
            next = (UINT)old[i].Hi + 1;
    }
    if (next <= 0xFFFF)
        add_range(n, (WCHAR)next, 0xFFFF);
    vbs_free(old);
}

/* reads the escape after a backslash and gives the character it stands for. */
static BOOL char_escape(Compiler *c, WCHAR *out)
{
    WCHAR e = c->S[c->Pos++];
    switch (e) {
    case L'n': *out = L'\n'; return TRUE;
    case L'r': *out = L'\r'; return TRUE;
    case L't': *out = L'\t'; return TRUE;
    case L'f': *out = L'\f'; return TRUE;
    case L'v': *out = L'\v'; return TRUE;
    case L'0': *out = 0; return TRUE;
    case L'x':
        if (c->Pos + 1 < c->Len + 0 && hexv(c->S[c->Pos]) >= 0 && hexv(c->S[c->Pos + 1]) >= 0) {
            *out = (WCHAR)(hexv(c->S[c->Pos]) * 16 + hexv(c->S[c->Pos + 1]));
            c->Pos += 2;
        } else
            *out = L'x';
        return TRUE;
    case L'u':
        if (c->Pos + 3 < c->Len + 0 && hexv(c->S[c->Pos]) >= 0 && hexv(c->S[c->Pos + 1]) >= 0 &&
            hexv(c->S[c->Pos + 2]) >= 0 && hexv(c->S[c->Pos + 3]) >= 0) {
            *out = (WCHAR)((hexv(c->S[c->Pos]) << 12) | (hexv(c->S[c->Pos + 1]) << 8) | (hexv(c->S[c->Pos + 2]) << 4) |
                           hexv(c->S[c->Pos + 3]));
            c->Pos += 4;
        } else
            *out = L'u';
        return TRUE;
    case L'c':
        if ((c->S[c->Pos] | 0x20) >= L'a' && (c->S[c->Pos] | 0x20) <= L'z') {
            *out = c->S[c->Pos] & 31;
            c->Pos++;
        } else
            *out = L'c';
        return TRUE;
    }
    *out = e;
    return TRUE;
}

static Node *parse_class(Compiler *c)
{
    Node *n = new_node(c, N_CLASS);
    if (c->S[c->Pos] == L'^') {
        n->Negate = TRUE;
        c->Pos++;
    }
    for (;;) {
        if (c->Pos >= c->Len) {
            c->Error = 5019;
            return n;
        }
        WCHAR ch = c->S[c->Pos++];
        if (ch == L']')
            break;
        WCHAR lo;
        BOOL is_class = FALSE;
        if (ch == L'\\') {
            if (c->Pos >= c->Len) {
                c->Error = 5019;
                return n;
            }
            WCHAR e = c->S[c->Pos];
            if (e == L'd' || e == L'w' || e == L's') {
                class_escape(n, e);
                c->Pos++;
                is_class = TRUE;
            } else if (e == L'D' || e == L'W' || e == L'S') {
                Node tmp = {};
                class_escape(&tmp, e | 0x20);
                invert_ranges(&tmp);
                for (UINT i = 0; i < tmp.RangeCount; i++)
                    add_range(n, tmp.Ranges[i].Lo, tmp.Ranges[i].Hi);
                vbs_free(tmp.Ranges);
                c->Pos++;
                is_class = TRUE;
            } else if (e == L'b') {
                c->Pos++;
                lo = 8;
            } else
                char_escape(c, &lo);
        } else
            lo = ch;
        if (is_class)
            continue;
        WCHAR hi = lo;
        if (c->S[c->Pos] == L'-' && c->Pos + 1 < c->Len && c->S[c->Pos + 1] != L']') {
            c->Pos++;
            WCHAR h = c->S[c->Pos++];
            if (h == L'\\') {
                WCHAR e = c->S[c->Pos];
                if (e == L'd' || e == L'w' || e == L's' || e == L'D' || e == L'W' || e == L'S') {
                    add_range(n, lo, lo);
                    add_range(n, L'-', L'-');
                    c->Pos++;
                    class_escape(n, e | 0x20);
                    continue;
                }
                char_escape(c, &hi);
            } else
                hi = h;
            if (hi < lo) {
                c->Error = 5021;
                return n;
            }
        }
        add_range(n, lo, hi);
    }
    return n;
}

static BOOL quantifier(Compiler *c, LONG *min, LONG *max)
{
    WCHAR ch = c->S[c->Pos];
    if (ch == L'*') {
        *min = 0;
        *max = -1;
        c->Pos++;
        return TRUE;
    }
    if (ch == L'+') {
        *min = 1;
        *max = -1;
        c->Pos++;
        return TRUE;
    }
    if (ch == L'?') {
        *min = 0;
        *max = 1;
        c->Pos++;
        return TRUE;
    }
    if (ch == L'{') {
        UINT p = c->Pos + 1;
        LONG a = 0, b = -1;
        UINT digits = 0;
        while (p < c->Len && c->S[p] >= L'0' && c->S[p] <= L'9') {
            a = a * 10 + (c->S[p] - L'0');
            p++;
            digits++;
        }
        if (!digits)
            return FALSE;
        if (p < c->Len && c->S[p] == L'}') {
            *min = *max = a;
            c->Pos = p + 1;
            return TRUE;
        }
        if (p < c->Len && c->S[p] == L',') {
            p++;
            UINT d2 = 0;
            LONG bb = 0;
            while (p < c->Len && c->S[p] >= L'0' && c->S[p] <= L'9') {
                bb = bb * 10 + (c->S[p] - L'0');
                p++;
                d2++;
            }
            if (p < c->Len && c->S[p] == L'}') {
                *min = a;
                *max = d2 ? bb : -1;
                (void)b;
                c->Pos = p + 1;
                return TRUE;
            }
        }
    }
    return FALSE;
}

static Node *parse_atom(Compiler *c)
{
    WCHAR ch = c->S[c->Pos++];
    Node *n;
    switch (ch) {
    case L'(': {
        BOOL capture = TRUE, look = FALSE, neg = FALSE;
        if (c->S[c->Pos] == L'?') {
            if (c->S[c->Pos + 1] == L':') {
                capture = FALSE;
                c->Pos += 2;
            } else if (c->S[c->Pos + 1] == L'=' || c->S[c->Pos + 1] == L'!') {
                look = TRUE;
                neg = c->S[c->Pos + 1] == L'!';
                c->Pos += 2;
            } else {
                c->Error = 5017;
                return nullptr;
            }
        }
        LONG index = 0;
        if (capture && !look)
            index = (LONG)++c->R->Groups;
        Node *inner = parse_alt(c);
        if (c->Error)
            return nullptr;
        if (c->S[c->Pos] != L')') {
            c->Error = 5020;
            return nullptr;
        }
        c->Pos++;
        n = new_node(c, look ? N_LOOK : N_GROUP);
        n->Child = inner;
        n->Capture = capture && !look;
        n->Negate = neg;
        n->Index = index;
        return n;
    }
    case L'[':
        return parse_class(c);
    case L'.':
        return new_node(c, N_ANY);
    case L'^':
        return new_node(c, N_BOL);
    case L'$':
        return new_node(c, N_EOL);
    case L'\\': {
        if (c->Pos >= c->Len) {
            c->Error = 5017;
            return nullptr;
        }
        WCHAR e = c->S[c->Pos];
        if (e == L'b' || e == L'B') {
            c->Pos++;
            return new_node(c, e == L'b' ? N_WORDB : N_NWORDB);
        }
        if (e == L'd' || e == L'w' || e == L's') {
            c->Pos++;
            n = new_node(c, N_CLASS);
            class_escape(n, e);
            return n;
        }
        if (e == L'D' || e == L'W' || e == L'S') {
            c->Pos++;
            n = new_node(c, N_CLASS);
            class_escape(n, e | 0x20);
            invert_ranges(n);
            return n;
        }
        if (e >= L'1' && e <= L'9') {
            LONG v = 0;
            while (c->S[c->Pos] >= L'0' && c->S[c->Pos] <= L'9') {
                v = v * 10 + (c->S[c->Pos] - L'0');
                c->Pos++;
            }
            n = new_node(c, N_BACKREF);
            n->Index = v;
            return n;
        }
        n = new_node(c, N_CHAR);
        char_escape(c, &n->Char);
        return n;
    }
    case L'*':
    case L'+':
    case L'?':
        c->Error = 5018;
        return nullptr;
    case L')':
        c->Error = 5017;
        return nullptr;
    default:
        n = new_node(c, N_CHAR);
        n->Char = ch;
        return n;
    }
}

static Node *parse_seq(Compiler *c)
{
    Node *head = nullptr, *tail = nullptr;
    while (c->Pos < c->Len && c->S[c->Pos] != L'|' && c->S[c->Pos] != L')') {
        Node *atom = parse_atom(c);
        if (c->Error)
            return nullptr;
        LONG min, max;
        if (c->Pos < c->Len && quantifier(c, &min, &max)) {
            Node *r = new_node(c, N_REPEAT);
            r->Min = min;
            r->Max = max;
            r->Child = atom;
            r->Greedy = TRUE;
            if (c->S[c->Pos] == L'?') {
                r->Greedy = FALSE;
                c->Pos++;
            }
            atom = r;
        }
        if (!head)
            head = tail = atom;
        else {
            tail->Next = atom;
            tail = atom;
        }
    }
    return head;
}

static Node *parse_alt(Compiler *c)
{
    Node *first = parse_seq(c);
    if (c->Error)
        return nullptr;
    if (c->Pos >= c->Len || c->S[c->Pos] != L'|')
        return first;
    Node *alt = new_node(c, N_ALT);
    alt->Branches = vbs_alloc(sizeof(Node *));
    alt->Branches[0] = first;
    alt->BranchCount = 1;
    while (c->Pos < c->Len && c->S[c->Pos] == L'|') {
        c->Pos++;
        Node *b = parse_seq(c);
        if (c->Error)
            return nullptr;
        alt->Branches = vbs_realloc(alt->Branches, (alt->BranchCount + 1) * sizeof(Node *));
        alt->Branches[alt->BranchCount++] = b;
    }
    return alt;
}

static void regex_free(Regex *r)
{
    if (!r)
        return;
    for (UINT i = 0; i < r->AllCount; i++) {
        vbs_free(r->All[i]->Ranges);
        vbs_free(r->All[i]->Branches);
        vbs_free(r->All[i]);
    }
    vbs_free(r->All);
    vbs_free(r);
}

static Regex *regex_compile(PCWSTR pattern, BOOL icase, BOOL multi, int *error)
{
    Regex *r = vbs_alloc(sizeof(Regex));
    Compiler c = {r, pattern, 0, (UINT)wcslen(pattern), 0};
    r->IgnoreCase = icase;
    r->Multiline = multi;
    r->Program = parse_alt(&c);
    if (!c.Error && c.Pos < c.Len)
        c.Error = c.S[c.Pos] == L')' ? 5017 : 5017;
    if (c.Error) {
        *error = c.Error;
        regex_free(r);
        return nullptr;
    }
    return r;
}

/* ------------------------------------------------------------------ matching */

typedef struct Cont {
    BYTE Kind;
    Node *N;
    LONG Count;
    LONG Pos;
    struct Cont *Up;
    LONG OldStart, OldEnd;
} Cont;

enum { K_REPEAT, K_GROUP, K_LOOK };

typedef struct Matcher {
    Regex *R;
    PCWSTR S;
    LONG Len;
    LONG *Caps;
    UINT Steps;
} Matcher;

static WCHAR lower(WCHAR c)
{
    if (c < 128)
        return (c >= L'A' && c <= L'Z') ? c + 32 : c;
    return towlower(c);
}

static BOOL is_word(WCHAR c)
{
    return (c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || c == L'_';
}

static BOOL in_class(Node *n, WCHAR c, BOOL icase)
{
    BOOL hit = FALSE;
    for (UINT i = 0; i < n->RangeCount && !hit; i++) {
        if (c >= n->Ranges[i].Lo && c <= n->Ranges[i].Hi)
            hit = TRUE;
        else if (icase) {
            WCHAR l = lower(c), u = towupper(c);
            if ((l >= n->Ranges[i].Lo && l <= n->Ranges[i].Hi) || (u >= n->Ranges[i].Lo && u <= n->Ranges[i].Hi))
                hit = TRUE;
        }
    }
    return hit != n->Negate;
}

static BOOL is_newline(WCHAR c)
{
    return c == L'\n' || c == L'\r' || c == 0x2028 || c == 0x2029;
}

/* matches one character against a single character node. */
static BOOL single(Matcher *m, Node *n, LONG i)
{
    if (i >= m->Len)
        return FALSE;
    WCHAR c = m->S[i];
    switch (n->Type) {
    case N_CHAR: return m->R->IgnoreCase ? lower(c) == lower(n->Char) : c == n->Char;
    case N_ANY: return !is_newline(c);
    case N_CLASS: return in_class(n, c, m->R->IgnoreCase);
    }
    return FALSE;
}

static LONG run(Matcher *m, Node *n, LONG i, Cont *k);

static LONG repeat_step(Matcher *m, Node *r, LONG count, LONG i, Cont *up)
{
    if (r->Greedy) {
        if (r->Max < 0 || count < r->Max) {
            Cont c = {K_REPEAT, r, count + 1, i, up};
            LONG res = run(m, r->Child, i, &c);
            if (res >= 0)
                return res;
        }
        if (count >= r->Min)
            return run(m, r->Next, i, up);
        return -1;
    }
    if (count >= r->Min) {
        LONG res = run(m, r->Next, i, up);
        if (res >= 0)
            return res;
    }
    if (r->Max < 0 || count < r->Max) {
        Cont c = {K_REPEAT, r, count + 1, i, up};
        return run(m, r->Child, i, &c);
    }
    return -1;
}

static LONG run(Matcher *m, Node *n, LONG i, Cont *k)
{
    for (;;) {
        if (!n) {
            if (!k)
                return i;
            switch (k->Kind) {
            case K_REPEAT:
                /* an iteration that matched nothing ends the repeat. */
                if (i == k->Pos && k->Count > k->N->Min)
                    return -1;
                return repeat_step(m, k->N, k->Count, i, k->Up);
            case K_GROUP: {
                LONG idx = k->N->Index;
                LONG os = m->Caps[idx * 2], oe = m->Caps[idx * 2 + 1];
                if (k->N->Capture) {
                    m->Caps[idx * 2] = k->Pos;
                    m->Caps[idx * 2 + 1] = i;
                }
                LONG res = run(m, k->N->Next, i, k->Up);
                if (res < 0 && k->N->Capture) {
                    m->Caps[idx * 2] = os;
                    m->Caps[idx * 2 + 1] = oe;
                }
                return res;
            }
            case K_LOOK:
                return i;
            }
            return -1;
        }
        if (++m->Steps > 20000000)
            return -1;
        switch (n->Type) {
        case N_CHAR:
        case N_ANY:
        case N_CLASS:
            if (!single(m, n, i))
                return -1;
            i++;
            n = n->Next;
            continue;
        case N_BOL:
            if (i == 0 || (m->R->Multiline && is_newline(m->S[i - 1]))) {
                n = n->Next;
                continue;
            }
            return -1;
        case N_EOL:
            if (i == m->Len || (m->R->Multiline && is_newline(m->S[i]))) {
                n = n->Next;
                continue;
            }
            return -1;
        case N_WORDB:
        case N_NWORDB: {
            BOOL a = i > 0 && is_word(m->S[i - 1]), b = i < m->Len && is_word(m->S[i]);
            if ((a != b) == (n->Type == N_WORDB)) {
                n = n->Next;
                continue;
            }
            return -1;
        }
        case N_BACKREF: {
            LONG idx = n->Index;
            if ((UINT)idx > m->R->Groups)
                return -1;
            LONG s = m->Caps[idx * 2], e = m->Caps[idx * 2 + 1];
            if (s >= 0) {
                LONG len = e - s;
                if (i + len > m->Len)
                    return -1;
                for (LONG j = 0; j < len; j++) {
                    WCHAR a = m->S[s + j], b = m->S[i + j];
                    if (m->R->IgnoreCase ? lower(a) != lower(b) : a != b)
                        return -1;
                }
                i += len;
            }
            n = n->Next;
            continue;
        }
        case N_GROUP: {
            Cont c = {K_GROUP, n, 0, i, k};
            return run(m, n->Child, i, &c);
        }
        case N_LOOK: {
            LONG saved[64];
            UINT cap = m->R->Groups * 2 + 2;
            if (cap > 64)
                cap = 64;
            memcpy(saved, m->Caps, cap * sizeof(LONG));
            Cont c = {K_LOOK, n, 0, i, nullptr};
            LONG res = run(m, n->Child, i, &c);
            BOOL ok = res >= 0;
            if (n->Negate) {
                memcpy(m->Caps, saved, cap * sizeof(LONG));
                ok = !ok;
            }
            if (!ok)
                return -1;
            n = n->Next;
            continue;
        }
        case N_ALT: {
            /* each branch continues with what follows the alternation. */
            for (UINT b = 0; b < n->BranchCount; b++) {
                /* a branch ends where its list ends; chain to the node after the alternation through a group. */
                Node chain = {};
                chain.Type = N_GROUP;
                chain.Child = n->Branches[b];
                chain.Next = n->Next;
                chain.Capture = FALSE;
                Cont c = {K_GROUP, &chain, 0, i, k};
                LONG res = run(m, chain.Child, i, &c);
                if (res >= 0)
                    return res;
            }
            return -1;
        }
        case N_REPEAT: {
            Node *child = n->Child;
            if (!child->Next && (child->Type == N_CHAR || child->Type == N_ANY || child->Type == N_CLASS)) {
                LONG max = n->Max < 0 ? m->Len - i : n->Max;
                LONG count = 0;
                if (n->Greedy) {
                    while (count < max && single(m, child, i + count))
                        count++;
                    while (count >= n->Min) {
                        LONG res = run(m, n->Next, i + count, k);
                        if (res >= 0)
                            return res;
                        count--;
                    }
                    return -1;
                }
                while (count < n->Min) {
                    if (!single(m, child, i + count))
                        return -1;
                    count++;
                }
                for (;;) {
                    LONG res = run(m, n->Next, i + count, k);
                    if (res >= 0)
                        return res;
                    if (count >= max || !single(m, child, i + count))
                        return -1;
                    count++;
                }
            }
            return repeat_step(m, n, 0, i, k);
        }
        }
        return -1;
    }
}

/* finds the first match at or after start. caps holds the span and the groups: 2 * (groups + 1) values. */
static BOOL regex_exec(Regex *r, PCWSTR s, LONG len, LONG start, LONG *caps)
{
    Matcher m = {r, s, len, caps, 0};
    for (LONG i = start; i <= len; i++) {
        for (UINT k = 0; k < (r->Groups + 1) * 2; k++)
            caps[k] = -1;
        LONG end = run(&m, r->Program, i, nullptr);
        if (end >= 0) {
            caps[0] = i;
            caps[1] = end;
            return TRUE;
        }
        if (m.Steps > 20000000)
            break;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ the objects */

enum { O_REGEXP, O_MATCHES, O_MATCH, O_SUBMATCHES, O_ENUM };

typedef struct RObj {
    IDispatch Dispatch;
    IEnumVARIANT Enum;
    LONG Refs;
    int Kind;
    Engine *E;
    /* regexp */
    BSTR Pattern;
    BOOL IgnoreCase, Global, Multiline;
    /* match, submatches, matches */
    BSTR Value;
    LONG Index;
    struct RObj **Items;
    BSTR *Texts;
    BOOL *Present;
    UINT Count;
    struct RObj *Sub;
    UINT Position;
    struct RObj *Owner;
} RObj;

extern const IDispatchVtbl g_robj_vtbl;
extern const IEnumVARIANTVtbl g_renum_vtbl;

static RObj *robj_new(int kind)
{
    RObj *o = vbs_alloc(sizeof(RObj));
    o->Dispatch.lpVtbl = &g_robj_vtbl;
    o->Enum.lpVtbl = &g_renum_vtbl;
    o->Refs = 1;
    o->Kind = kind;
    InterlockedIncrement(&g_vbs_objects);
    return o;
}

static ULONG STDMETHODCALLTYPE r_addref(IDispatch *This)
{
    return InterlockedIncrement(&((RObj *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE r_release(IDispatch *This)
{
    RObj *o = (RObj *)This;
    LONG refs = InterlockedDecrement(&o->Refs);
    if (!refs) {
        SysFreeString(o->Pattern);
        SysFreeString(o->Value);
        for (UINT i = 0; i < o->Count; i++) {
            if (o->Items)
                r_release(&o->Items[i]->Dispatch);
            if (o->Texts)
                SysFreeString(o->Texts[i]);
        }
        vbs_free(o->Items);
        vbs_free(o->Texts);
        vbs_free(o->Present);
        if (o->Sub)
            r_release(&o->Sub->Dispatch);
        if (o->Owner)
            r_release(&o->Owner->Dispatch);
        InterlockedDecrement(&g_vbs_objects);
        vbs_free(o);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE r_qi(IDispatch *This, REFIID riid, void **object)
{
    RObj *o = (RObj *)This;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch)) {
        *object = &o->Dispatch;
    } else if (o->Kind == O_ENUM && IsEqualIID(riid, &IID_IEnumVARIANT)) {
        *object = &o->Enum;
    } else {
        *object = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&o->Refs);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_count(IDispatch *This, UINT *count)
{
    *count = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE r_typeinfo(IDispatch *This, UINT i, LCID lcid, ITypeInfo **info)
{
    *info = nullptr;
    return DISP_E_BADINDEX;
}

enum {
    D_VALUE = 0, D_PATTERN = 10001, D_IGNORECASE = 10002, D_GLOBAL = 10003, D_MULTILINE = 10004, D_EXECUTE = 10005,
    D_TEST = 10006, D_REPLACE = 10007, D_FIRSTINDEX = 10001, D_LENGTH = 10002, D_SUBMATCHES = 10003, D_COUNT = 1,
    D_ITEM = 0, D_NEWENUM = -4
};

static const struct {
    PCWSTR Name;
    DISPID Id;
    int Kind;
} g_names[] = {
    {L"Pattern", D_PATTERN, O_REGEXP}, {L"IgnoreCase", D_IGNORECASE, O_REGEXP}, {L"Global", D_GLOBAL, O_REGEXP},
    {L"Multiline", D_MULTILINE, O_REGEXP}, {L"Execute", D_EXECUTE, O_REGEXP}, {L"Test", D_TEST, O_REGEXP},
    {L"Replace", D_REPLACE, O_REGEXP}, {L"Value", D_VALUE, O_MATCH}, {L"FirstIndex", D_FIRSTINDEX, O_MATCH},
    {L"Length", D_LENGTH, O_MATCH}, {L"SubMatches", D_SUBMATCHES, O_MATCH}, {L"Count", D_COUNT, O_MATCHES},
    {L"Item", D_ITEM, O_MATCHES}, {L"_NewEnum", D_NEWENUM, O_MATCHES}, {L"Count", D_COUNT, O_SUBMATCHES},
    {L"Item", D_ITEM, O_SUBMATCHES}, {L"_NewEnum", D_NEWENUM, O_SUBMATCHES},
};

static HRESULT STDMETHODCALLTYPE r_names(IDispatch *This, REFIID riid, LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    RObj *o = (RObj *)This;
    HRESULT hr = S_OK;
    for (UINT i = 0; i < count; i++) {
        ids[i] = DISPID_UNKNOWN;
        if (i)
            continue;
        for (UINT k = 0; k < sizeof(g_names) / sizeof(g_names[0]); k++)
            if (g_names[k].Kind == o->Kind && name_equal(names[0], g_names[k].Name))
                ids[0] = g_names[k].Id;
        if (ids[0] == DISPID_UNKNOWN)
            hr = DISP_E_UNKNOWNNAME;
    }
    return hr;
}

static HRESULT regex_error(EXCEPINFO *ex, int number)
{
    if (ex) {
        memset(ex, 0, sizeof(*ex));
        ex->scode = (HRESULT)(0x800A0000u | (ULONG)number);
        ex->bstrSource = SysAllocString(L"Microsoft VBScript regular expression");
        PCWSTR text = error_text(number);
        ex->bstrDescription = SysAllocString(text ? text : L"");
    }
    return DISP_E_EXCEPTION;
}

static BSTR to_bstr(VARIANT *v, LCID lcid, HRESULT *hr)
{
    VARIANT t;
    VariantInit(&t);
    while ((v->vt & VT_BYREF) && (v->vt & VT_TYPEMASK) == VT_VARIANT && v->pvarVal)
        v = v->pvarVal;
    *hr = VariantChangeTypeEx(&t, v, lcid, 0, VT_BSTR);
    return SUCCEEDED(*hr) ? t.bstrVal : nullptr;
}

static RObj *make_match(RObj *re, BSTR source, LONG *caps, UINT groups)
{
    RObj *m = robj_new(O_MATCH);
    m->Value = SysAllocStringLen(source + caps[0], caps[1] - caps[0]);
    m->Index = caps[0];
    RObj *sub = robj_new(O_SUBMATCHES);
    sub->Count = groups;
    sub->Texts = vbs_alloc((groups + 1) * sizeof(BSTR));
    sub->Present = vbs_alloc((groups + 1) * sizeof(BOOL));
    for (UINT g = 0; g < groups; g++) {
        LONG s = caps[(g + 1) * 2], e = caps[(g + 1) * 2 + 1];
        sub->Present[g] = s >= 0;
        sub->Texts[g] = s >= 0 ? SysAllocStringLen(source + s, e - s) : SysAllocString(L"");
    }
    m->Sub = sub;
    return m;
}

static HRESULT run_regexp(RObj *o, BSTR source, RObj **matches, BOOL *any, EXCEPINFO *ex, BOOL collect)
{
    int error = 0;
    Regex *r = regex_compile(o->Pattern ? o->Pattern : L"", o->IgnoreCase, o->Multiline, &error);
    if (!r)
        return regex_error(ex, error);
    LONG len = (LONG)SysStringLen(source);
    LONG caps[200];
    if (r->Groups > 98)
        r->Groups = 98;
    RObj *list = collect ? robj_new(O_MATCHES) : nullptr;
    LONG pos = 0;
    *any = FALSE;
    while (pos <= len && regex_exec(r, source, len, pos, caps)) {
        *any = TRUE;
        if (!collect)
            break;
        RObj *m = make_match(o, source, caps, r->Groups);
        list->Items = vbs_realloc(list->Items, (list->Count + 1) * sizeof(RObj *));
        list->Items[list->Count++] = m;
        if (!o->Global)
            break;
        pos = caps[1] == caps[0] ? caps[1] + 1 : caps[1];
    }
    regex_free(r);
    *matches = list;
    return S_OK;
}

static HRESULT do_replace(RObj *o, BSTR source, BSTR rep, BSTR *out, EXCEPINFO *ex)
{
    int error = 0;
    Regex *r = regex_compile(o->Pattern ? o->Pattern : L"", o->IgnoreCase, o->Multiline, &error);
    if (!r)
        return regex_error(ex, error);
    if (r->Groups > 98)
        r->Groups = 98;
    LONG len = (LONG)SysStringLen(source), rl = (LONG)SysStringLen(rep);
    LONG caps[200];
    SIZE_T cap = len + 64, n = 0;
    WCHAR *buf = vbs_alloc(cap * sizeof(WCHAR));
#define PUSH(p, l) \
    do { \
        if (n + (l) + 1 > cap) { \
            cap = (n + (l) + 1) * 2; \
            buf = vbs_realloc(buf, cap * sizeof(WCHAR)); \
        } \
        memcpy(buf + n, (p), (l) * sizeof(WCHAR)); \
        n += (l); \
    } while (0)
    LONG pos = 0, copied = 0;
    while (pos <= len && regex_exec(r, source, len, pos, caps)) {
        PUSH(source + copied, caps[0] - copied);
        for (LONG i = 0; i < rl; i++) {
            WCHAR c = rep[i];
            if (c == L'$' && i + 1 < rl) {
                WCHAR d = rep[i + 1];
                if (d == L'$') {
                    PUSH(L"$", 1);
                    i++;
                } else if (d == L'&') {
                    PUSH(source + caps[0], caps[1] - caps[0]);
                    i++;
                } else if (d == L'`') {
                    PUSH(source, caps[0]);
                    i++;
                } else if (d == L'\'') {
                    PUSH(source + caps[1], len - caps[1]);
                    i++;
                } else if (d >= L'0' && d <= L'9') {
                    LONG g = d - L'0';
                    LONG used = 1;
                    if (i + 2 < rl && rep[i + 2] >= L'0' && rep[i + 2] <= L'9' && g * 10 + (rep[i + 2] - L'0') <= (LONG)r->Groups) {
                        g = g * 10 + (rep[i + 2] - L'0');
                        used = 2;
                    }
                    if (g >= 1 && g <= (LONG)r->Groups) {
                        if (caps[g * 2] >= 0)
                            PUSH(source + caps[g * 2], caps[g * 2 + 1] - caps[g * 2]);
                        i += used;
                    } else
                        PUSH(&c, 1);
                } else
                    PUSH(&c, 1);
            } else
                PUSH(&c, 1);
        }
        copied = caps[1];
        if (!o->Global)
            break;
        if (caps[1] == caps[0]) {
            if (caps[1] < len)
                PUSH(source + caps[1], 1);
            copied = caps[1] + 1;
            pos = caps[1] + 1;
        } else
            pos = caps[1];
    }
    if (copied < len)
        PUSH(source + copied, len - copied);
    *out = SysAllocStringLen(buf, (UINT)n);
    vbs_free(buf);
    regex_free(r);
    return S_OK;
#undef PUSH
}

static RObj *enum_of(RObj *o)
{
    RObj *e = robj_new(O_ENUM);
    InterlockedIncrement(&o->Refs);
    e->Owner = o;
    return e;
}

static HRESULT STDMETHODCALLTYPE r_invoke(IDispatch *This, DISPID id, REFIID riid, LCID lcid, WORD flags,
                                          DISPPARAMS *p, VARIANT *result, EXCEPINFO *ex, UINT *arg_err)
{
    RObj *o = (RObj *)This;
    UINT argc = p ? p->cArgs : 0;
    #define ARG(i) (&p->rgvarg[argc - 1 - (i)])
    BOOL put = (flags & (DISPATCH_PROPERTYPUT | DISPATCH_PROPERTYPUTREF)) != 0;
    if (result)
        VariantInit(result);
    HRESULT hr;
    switch (o->Kind) {
    case O_REGEXP:
        switch (id) {
        case D_PATTERN:
            if (put) {
                BSTR s = to_bstr(&p->rgvarg[0], lcid, &hr);
                if (!s)
                    return DISP_E_TYPEMISMATCH;
                SysFreeString(o->Pattern);
                o->Pattern = s;
                return S_OK;
            }
            if (result) {
                result->vt = VT_BSTR;
                result->bstrVal = SysAllocStringLen(o->Pattern ? o->Pattern : L"", o->Pattern ? SysStringLen(o->Pattern) : 0);
            }
            return S_OK;
        case D_IGNORECASE:
        case D_GLOBAL:
        case D_MULTILINE: {
            BOOL *flag = id == D_IGNORECASE ? &o->IgnoreCase : id == D_GLOBAL ? &o->Global : &o->Multiline;
            if (put) {
                VARIANT t;
                VariantInit(&t);
                VARIANT *a = &p->rgvarg[0];
                while ((a->vt & VT_BYREF) && (a->vt & VT_TYPEMASK) == VT_VARIANT && a->pvarVal)
                    a = a->pvarVal;
                if (FAILED(VariantChangeTypeEx(&t, a, lcid, 0, VT_BOOL)))
                    return DISP_E_TYPEMISMATCH;
                *flag = t.boolVal != 0;
                return S_OK;
            }
            if (result) {
                result->vt = VT_BOOL;
                result->boolVal = *flag ? VARIANT_TRUE : VARIANT_FALSE;
            }
            return S_OK;
        }
        case D_EXECUTE:
        case D_TEST:
        case D_REPLACE: {
            if (argc < (id == D_REPLACE ? 2u : 1u) || argc > (id == D_REPLACE ? 2u : 1u))
                return DISP_E_BADPARAMCOUNT;
            BSTR s = to_bstr(ARG(0), lcid, &hr);
            if (!s)
                return hr == DISP_E_TYPEMISMATCH || FAILED(hr) ? DISP_E_TYPEMISMATCH : hr;
            RObj *matches = nullptr;
            BOOL any = FALSE;
            if (id == D_REPLACE) {
                BSTR rep = to_bstr(ARG(1), lcid, &hr);
                if (!rep) {
                    SysFreeString(s);
                    return DISP_E_TYPEMISMATCH;
                }
                BSTR out = nullptr;
                hr = do_replace(o, s, rep, &out, ex);
                SysFreeString(rep);
                SysFreeString(s);
                if (hr != S_OK)
                    return hr;
                if (result) {
                    result->vt = VT_BSTR;
                    result->bstrVal = out;
                } else
                    SysFreeString(out);
                return S_OK;
            }
            hr = run_regexp(o, s, &matches, &any, ex, id == D_EXECUTE);
            SysFreeString(s);
            if (hr != S_OK)
                return hr;
            if (id == D_TEST) {
                if (result) {
                    result->vt = VT_BOOL;
                    result->boolVal = any ? VARIANT_TRUE : VARIANT_FALSE;
                }
                return S_OK;
            }
            if (result) {
                result->vt = VT_DISPATCH;
                result->pdispVal = &matches->Dispatch;
            } else
                r_release(&matches->Dispatch);
            return S_OK;
        }
        }
        return DISP_E_MEMBERNOTFOUND;
    case O_MATCH:
        switch (id) {
        case D_VALUE:
            if (put)
                return DISP_E_MEMBERNOTFOUND;
            if (result) {
                result->vt = VT_BSTR;
                result->bstrVal = SysAllocStringLen(o->Value, SysStringLen(o->Value));
            }
            return S_OK;
        case D_FIRSTINDEX:
        case D_LENGTH:
            if (result) {
                result->vt = VT_I4;
                result->lVal = id == D_FIRSTINDEX ? o->Index : (LONG)SysStringLen(o->Value);
            }
            return S_OK;
        case D_SUBMATCHES:
            if (result) {
                result->vt = VT_DISPATCH;
                result->pdispVal = &o->Sub->Dispatch;
                r_addref(&o->Sub->Dispatch);
            }
            return S_OK;
        }
        return DISP_E_MEMBERNOTFOUND;
    case O_MATCHES:
    case O_SUBMATCHES:
        switch (id) {
        case D_COUNT:
            if (result) {
                result->vt = VT_I4;
                result->lVal = (LONG)o->Count;
            }
            return S_OK;
        case D_ITEM: {
            if (argc != 1)
                return DISP_E_BADPARAMCOUNT;
            VARIANT t;
            VariantInit(&t);
            VARIANT *a = ARG(0);
            while ((a->vt & VT_BYREF) && (a->vt & VT_TYPEMASK) == VT_VARIANT && a->pvarVal)
                a = a->pvarVal;
            if (FAILED(VariantChangeTypeEx(&t, a, lcid, 0, VT_I4)))
                return DISP_E_TYPEMISMATCH;
            if (t.lVal < 0 || (UINT)t.lVal >= o->Count) {
                if (ex) {
                    memset(ex, 0, sizeof(*ex));
                    ex->scode = (HRESULT)0x800A0009u;
                    ex->bstrSource = SysAllocString(L"Microsoft VBScript runtime error");
                    ex->bstrDescription = SysAllocString(L"Subscript out of range");
                }
                return DISP_E_EXCEPTION;
            }
            if (result) {
                if (o->Kind == O_MATCHES) {
                    result->vt = VT_DISPATCH;
                    result->pdispVal = &o->Items[t.lVal]->Dispatch;
                    r_addref(result->pdispVal);
                } else {
                    result->vt = VT_BSTR;
                    result->bstrVal = SysAllocStringLen(o->Texts[t.lVal], SysStringLen(o->Texts[t.lVal]));
                }
            }
            return S_OK;
        }
        case D_NEWENUM:
            if (result) {
                RObj *e = enum_of(o);
                result->vt = VT_UNKNOWN;
                result->punkVal = (IUnknown *)&e->Enum;
            }
            return S_OK;
        }
        return DISP_E_MEMBERNOTFOUND;
    }
    return DISP_E_MEMBERNOTFOUND;
    #undef ARG
}

const IDispatchVtbl g_robj_vtbl = {r_qi, r_addref, r_release, r_count, r_typeinfo, r_names, r_invoke};

/* the enumerator over a collection. */
#define ENUM_OBJ(p) ((RObj *)((BYTE *)(p) - offsetof(RObj, Enum)))

static HRESULT STDMETHODCALLTYPE en_qi(IEnumVARIANT *This, REFIID riid, void **object)
{
    return r_qi(&ENUM_OBJ(This)->Dispatch, riid, object);
}

static ULONG STDMETHODCALLTYPE en_addref(IEnumVARIANT *This)
{
    return r_addref(&ENUM_OBJ(This)->Dispatch);
}

static ULONG STDMETHODCALLTYPE en_release(IEnumVARIANT *This)
{
    return r_release(&ENUM_OBJ(This)->Dispatch);
}

static HRESULT STDMETHODCALLTYPE en_next(IEnumVARIANT *This, ULONG count, VARIANT *values, ULONG *fetched)
{
    RObj *e = ENUM_OBJ(This);
    RObj *c = e->Owner;
    ULONG got = 0;
    while (got < count && e->Position < c->Count) {
        VariantInit(&values[got]);
        if (c->Kind == O_MATCHES) {
            values[got].vt = VT_DISPATCH;
            values[got].pdispVal = &c->Items[e->Position]->Dispatch;
            r_addref(values[got].pdispVal);
        } else {
            values[got].vt = VT_BSTR;
            values[got].bstrVal = SysAllocStringLen(c->Texts[e->Position], SysStringLen(c->Texts[e->Position]));
        }
        e->Position++;
        got++;
    }
    if (fetched)
        *fetched = got;
    return got == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE en_skip(IEnumVARIANT *This, ULONG count)
{
    RObj *e = ENUM_OBJ(This);
    e->Position += count;
    return e->Position <= e->Owner->Count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE en_reset(IEnumVARIANT *This)
{
    ENUM_OBJ(This)->Position = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE en_clone(IEnumVARIANT *This, IEnumVARIANT **out)
{
    RObj *e = ENUM_OBJ(This);
    RObj *n = enum_of(e->Owner);
    n->Position = e->Position;
    *out = &n->Enum;
    return S_OK;
}

const IEnumVARIANTVtbl g_renum_vtbl = {en_qi, en_addref, en_release, en_next, en_skip, en_reset, en_clone};

IDispatch *regexp_create(Engine *e)
{
    RObj *o = robj_new(O_REGEXP);
    return &o->Dispatch;
}
