
enum { RN_CHAR, RN_ANY, RN_CLASS, RN_BOL, RN_EOL, RN_WORDB, RN_NWORDB, RN_GROUP, RN_BACKREF, RN_ALT, RN_REPEAT, RN_LOOK };

typedef struct RClassRange {
    WCHAR Lo, Hi;
} RClassRange;

typedef struct RNode {
    BYTE Type;
    BOOL Negate, Greedy, Capture;
    WCHAR Char;
    LONG Min, Max, Index;
    RClassRange *Ranges;
    UINT RangeCount;
    struct RNode *Next, *Child;
    struct RNode **Branches;
    UINT BranchCount;
} RNode;

typedef struct Regex {
    RNode *Program;
    UINT Groups;
    BOOL IgnoreCase, Multiline;
    int Error;
    RNode **All;
    UINT AllCount, AllCapacity;
} Regex;

typedef struct RCompiler {
    Regex *R;
    PCWSTR S;
    UINT Pos, Len;
    int Error;
} RCompiler;

static RNode *new_node(RCompiler *c, BYTE type)
{
    RNode *n = js_alloc(sizeof(RNode));
    n->Type = type;
    Regex *r = c->R;
    if (r->AllCount == r->AllCapacity) {
        r->AllCapacity = r->AllCapacity ? r->AllCapacity * 2 : 32;
        r->All = js_realloc(r->All, r->AllCapacity * sizeof(RNode *));
    }
    r->All[r->AllCount++] = n;
    return n;
}

static void add_range(RNode *n, WCHAR lo, WCHAR hi)
{
    n->Ranges = js_realloc(n->Ranges, (n->RangeCount + 1) * sizeof(RClassRange));
    n->Ranges[n->RangeCount].Lo = lo;
    n->Ranges[n->RangeCount].Hi = hi;
    n->RangeCount++;
}

static RNode *parse_alt(RCompiler *c);

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
static BOOL class_escape(RNode *n, WCHAR e)
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
static void invert_ranges(RNode *n)
{
    UINT count = n->RangeCount;
    RClassRange *old = n->Ranges;
    for (UINT i = 0; i < count; i++)
        for (UINT j = i + 1; j < count; j++)
            if (old[j].Lo < old[i].Lo) {
                RClassRange t = old[i];
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
    js_free(old);
}

/* reads the escape after a backslash and gives the character it stands for. */
static BOOL char_escape(RCompiler *c, WCHAR *out)
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

static RNode *parse_class(RCompiler *c)
{
    RNode *n = new_node(c, RN_CLASS);
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
                RNode tmp = {};
                class_escape(&tmp, e | 0x20);
                invert_ranges(&tmp);
                for (UINT i = 0; i < tmp.RangeCount; i++)
                    add_range(n, tmp.Ranges[i].Lo, tmp.Ranges[i].Hi);
                js_free(tmp.Ranges);
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

static BOOL quantifier(RCompiler *c, LONG *min, LONG *max)
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
                if (d2 && bb < a)
                    c->Error = 5017;
                return TRUE;
            }
        }
    }
    return FALSE;
}

static RNode *parse_atom(RCompiler *c)
{
    WCHAR ch = c->S[c->Pos++];
    RNode *n;
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
        RNode *inner = parse_alt(c);
        if (c->Error)
            return nullptr;
        if (c->S[c->Pos] != L')') {
            c->Error = 5020;
            return nullptr;
        }
        c->Pos++;
        n = new_node(c, look ? RN_LOOK : RN_GROUP);
        n->Child = inner;
        n->Capture = capture && !look;
        n->Negate = neg;
        n->Index = index;
        return n;
    }
    case L'[':
        return parse_class(c);
    case L'.':
        return new_node(c, RN_ANY);
    case L'^':
        return new_node(c, RN_BOL);
    case L'$':
        return new_node(c, RN_EOL);
    case L'\\': {
        if (c->Pos >= c->Len) {
            c->Error = 5017;
            return nullptr;
        }
        WCHAR e = c->S[c->Pos];
        if (e == L'b' || e == L'B') {
            c->Pos++;
            return new_node(c, e == L'b' ? RN_WORDB : RN_NWORDB);
        }
        if (e == L'd' || e == L'w' || e == L's') {
            c->Pos++;
            n = new_node(c, RN_CLASS);
            class_escape(n, e);
            return n;
        }
        if (e == L'D' || e == L'W' || e == L'S') {
            c->Pos++;
            n = new_node(c, RN_CLASS);
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
            n = new_node(c, RN_BACKREF);
            n->Index = v;
            return n;
        }
        n = new_node(c, RN_CHAR);
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
        n = new_node(c, RN_CHAR);
        n->Char = ch;
        return n;
    }
}

static RNode *parse_seq(RCompiler *c)
{
    RNode *head = nullptr, *tail = nullptr;
    while (c->Pos < c->Len && c->S[c->Pos] != L'|' && c->S[c->Pos] != L')') {
        RNode *atom = parse_atom(c);
        if (c->Error)
            return nullptr;
        LONG min, max;
        if (c->Pos < c->Len && quantifier(c, &min, &max)) {
            RNode *r = new_node(c, RN_REPEAT);
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

static RNode *parse_alt(RCompiler *c)
{
    RNode *first = parse_seq(c);
    if (c->Error)
        return nullptr;
    if (c->Pos >= c->Len || c->S[c->Pos] != L'|')
        return first;
    RNode *alt = new_node(c, RN_ALT);
    alt->Branches = js_alloc(sizeof(RNode *));
    alt->Branches[0] = first;
    alt->BranchCount = 1;
    while (c->Pos < c->Len && c->S[c->Pos] == L'|') {
        c->Pos++;
        RNode *b = parse_seq(c);
        if (c->Error)
            return nullptr;
        alt->Branches = js_realloc(alt->Branches, (alt->BranchCount + 1) * sizeof(RNode *));
        alt->Branches[alt->BranchCount++] = b;
    }
    return alt;
}

void regexp_free(Regex *r)
{
    if (!r)
        return;
    for (UINT i = 0; i < r->AllCount; i++) {
        js_free(r->All[i]->Ranges);
        js_free(r->All[i]->Branches);
        js_free(r->All[i]);
    }
    js_free(r->All);
    js_free(r);
}

Regex *regex_compile(PCWSTR pattern, BOOL icase, BOOL multi, int *error)
{
    Regex *r = js_alloc(sizeof(Regex));
    RCompiler c = {r, pattern, 0, (UINT)wcslen(pattern), 0};
    r->IgnoreCase = icase;
    r->Multiline = multi;
    r->Program = parse_alt(&c);
    if (!c.Error && c.Pos < c.Len)
        c.Error = c.S[c.Pos] == L')' ? 5017 : 5017;
    if (c.Error) {
        *error = c.Error;
        regexp_free(r);
        return nullptr;
    }
    return r;
}

/* ------------------------------------------------------------------ matching */

typedef struct RCont {
    BYTE Kind;
    RNode *N;
    LONG Count;
    LONG Pos;
    struct RCont *Up;
    LONG OldStart, OldEnd;
} RCont;

enum { RK_REPEAT, RK_GROUP, RK_LOOK };

typedef struct RMatcher {
    Regex *R;
    PCWSTR S;
    LONG Len;
    LONG *Caps;
    UINT Steps;
} RMatcher;

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

static BOOL in_class(RNode *n, WCHAR c, BOOL icase)
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
static BOOL single(RMatcher *m, RNode *n, LONG i)
{
    if (i >= m->Len)
        return FALSE;
    WCHAR c = m->S[i];
    switch (n->Type) {
    case RN_CHAR: return m->R->IgnoreCase ? lower(c) == lower(n->Char) : c == n->Char;
    case RN_ANY: return !is_newline(c);
    case RN_CLASS: return in_class(n, c, m->R->IgnoreCase);
    }
    return FALSE;
}

static LONG run(RMatcher *m, RNode *n, LONG i, RCont *k);

static LONG repeat_step(RMatcher *m, RNode *r, LONG count, LONG i, RCont *up)
{
    if (r->Greedy) {
        if (r->Max < 0 || count < r->Max) {
            RCont c = {RK_REPEAT, r, count + 1, i, up};
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
        RCont c = {RK_REPEAT, r, count + 1, i, up};
        return run(m, r->Child, i, &c);
    }
    return -1;
}

static LONG run(RMatcher *m, RNode *n, LONG i, RCont *k)
{
    for (;;) {
        if (!n) {
            if (!k)
                return i;
            switch (k->Kind) {
            case RK_REPEAT:
                /* an iteration that matched nothing ends the repeat. */
                if (i == k->Pos && k->Count > k->N->Min)
                    return -1;
                return repeat_step(m, k->N, k->Count, i, k->Up);
            case RK_GROUP: {
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
            case RK_LOOK:
                return i;
            }
            return -1;
        }
        if (++m->Steps > 20000000)
            return -1;
        switch (n->Type) {
        case RN_CHAR:
        case RN_ANY:
        case RN_CLASS:
            if (!single(m, n, i))
                return -1;
            i++;
            n = n->Next;
            continue;
        case RN_BOL:
            if (i == 0 || (m->R->Multiline && is_newline(m->S[i - 1]))) {
                n = n->Next;
                continue;
            }
            return -1;
        case RN_EOL:
            if (i == m->Len || (m->R->Multiline && is_newline(m->S[i]))) {
                n = n->Next;
                continue;
            }
            return -1;
        case RN_WORDB:
        case RN_NWORDB: {
            BOOL a = i > 0 && is_word(m->S[i - 1]), b = i < m->Len && is_word(m->S[i]);
            if ((a != b) == (n->Type == RN_WORDB)) {
                n = n->Next;
                continue;
            }
            return -1;
        }
        case RN_BACKREF: {
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
        case RN_GROUP: {
            RCont c = {RK_GROUP, n, 0, i, k};
            return run(m, n->Child, i, &c);
        }
        case RN_LOOK: {
            LONG saved[64];
            UINT cap = m->R->Groups * 2 + 2;
            if (cap > 64)
                cap = 64;
            memcpy(saved, m->Caps, cap * sizeof(LONG));
            RCont c = {RK_LOOK, n, 0, i, nullptr};
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
        case RN_ALT: {
            /* each branch continues with what follows the alternation. */
            for (UINT b = 0; b < n->BranchCount; b++) {
                /* a branch ends where its list ends; chain to the node after the alternation through a group. */
                RNode chain = {};
                chain.Type = RN_GROUP;
                chain.Child = n->Branches[b];
                chain.Next = n->Next;
                chain.Capture = FALSE;
                RCont c = {RK_GROUP, &chain, 0, i, k};
                LONG res = run(m, chain.Child, i, &c);
                if (res >= 0)
                    return res;
            }
            return -1;
        }
        case RN_REPEAT: {
            RNode *child = n->Child;
            if (!child->Next && (child->Type == RN_CHAR || child->Type == RN_ANY || child->Type == RN_CLASS)) {
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
BOOL regex_exec(Regex *r, PCWSTR s, LONG len, LONG start, LONG *caps)
{
    RMatcher m = {r, s, len, caps, 0};
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


