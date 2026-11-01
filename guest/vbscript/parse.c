/*
 * the lexer and parser of vbscript: a unit of source text becomes statements, procedures and classes in an arena.
 */
#include "vbs.h"

/* ------------------------------------------------------------------ memory */

void *vbs_alloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void *vbs_realloc(void *p, SIZE_T size)
{
    if (!p)
        return vbs_alloc(size);
    return HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, p, size ? size : 1);
}

void vbs_free(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

PWSTR vbs_wcsndup(PCWSTR text, SIZE_T length)
{
    PWSTR copy = vbs_alloc((length + 1) * sizeof(WCHAR));
    if (copy) {
        memcpy(copy, text, length * sizeof(WCHAR));
        copy[length] = 0;
    }
    return copy;
}

PWSTR vbs_wcsdup(PCWSTR text)
{
    return vbs_wcsndup(text, wcslen(text));
}

typedef struct ArenaBlock {
    struct ArenaBlock *Next;
    SIZE_T Used, Size;
} ArenaBlock;

struct Arena {
    ArenaBlock *Blocks;
};

Arena *arena_create(void)
{
    return vbs_alloc(sizeof(Arena));
}

void *arena_alloc(Arena *arena, SIZE_T size)
{
    size = (size + 15) & ~(SIZE_T)15;
    ArenaBlock *block = arena->Blocks;
    if (!block || block->Used + size > block->Size) {
        SIZE_T capacity = size > 32768 ? size : 32768;
        block = vbs_alloc(sizeof(ArenaBlock) + capacity);
        if (!block)
            return nullptr;
        block->Size = capacity;
        block->Next = arena->Blocks;
        arena->Blocks = block;
    }
    void *p = (BYTE *)(block + 1) + block->Used;
    block->Used += size;
    return p;
}

PWSTR arena_wcsndup(Arena *arena, PCWSTR text, SIZE_T length)
{
    PWSTR copy = arena_alloc(arena, (length + 1) * sizeof(WCHAR));
    if (copy) {
        memcpy(copy, text, length * sizeof(WCHAR));
        copy[length] = 0;
    }
    return copy;
}

void arena_destroy(Arena *arena)
{
    ArenaBlock *block = arena->Blocks;
    while (block) {
        ArenaBlock *next = block->Next;
        vbs_free(block);
        block = next;
    }
    vbs_free(arena);
}

static WCHAR fold(WCHAR c)
{
    if (c >= L'A' && c <= L'Z')
        return c + 32;
    if (c < 128)
        return c;
    return towlower(c);
}

ULONG name_hash(PCWSTR name)
{
    ULONG h = 5381;
    for (; *name; name++)
        h = h * 33 + fold(*name);
    return h;
}

BOOL name_equal(PCWSTR a, PCWSTR b)
{
    for (; *a && fold(*a) == fold(*b); a++, b++)
        ;
    return *a == *b;
}

/* ------------------------------------------------------------------ lexer */

int vbs_setjmp(void *buffer) __attribute__((returns_twice));
void vbs_longjmp(void *buffer, int value) __attribute__((noreturn));

typedef struct Token {
    BYTE Kind;
    ULONG Line, Col;
    PCWSTR Text;
    ULONG Length;
    /* the value of a literal, or for a bad token the error number in Value.lVal. */
    VARIANT Value;
    BOOL Bracketed, SpaceBefore;
    ULONG LineStart;
} Token;

typedef struct Parser {
    Engine *E;
    Unit *U;
    Arena *A;
    PCWSTR Source;
    PCWSTR Cursor;
    ULONG Line, Col;
    ULONG FirstLine;
    Token *Tokens;
    UINT Count, Capacity, Index;
    void *Jump[10];
    LONG ErrorNumber;
    ULONG ErrorLine, ErrorCol;
    BOOL InClass, InProc, InSub, InFunction, InProperty;
    UINT LoopFor, LoopDo;
    BOOL ExecuteMode;
    Class *CurrentClass;
    Proc *CurrentProc;
    UINT StatementCount;
    BOOL AnyStatement, SpaceFlag, StatementTarget;
    UINT SingleLine;
} Parser;

static const struct {
    PCWSTR Text;
    BYTE Kind;
} g_keywords[] = {
    {L"and", K_AND}, {L"as", K_AS}, {L"byref", K_BYREF}, {L"byval", K_BYVAL}, {L"call", K_CALL}, {L"case", K_CASE},
    {L"class", K_CLASS}, {L"const", K_CONST}, {L"dim", K_DIM}, {L"do", K_DO}, {L"each", K_EACH}, {L"else", K_ELSE},
    {L"elseif", K_ELSEIF}, {L"empty", K_EMPTY}, {L"end", K_END}, {L"eqv", K_EQV}, {L"erase", K_ERASE},
    {L"exit", K_EXIT}, {L"false", K_FALSE}, {L"for", K_FOR}, {L"function", K_FUNCTION}, {L"goto", K_GOTO},
    {L"if", K_IF}, {L"imp", K_IMP}, {L"in", K_IN}, {L"is", K_IS}, {L"let", K_LET}, {L"loop", K_LOOP}, {L"me", K_ME},
    {L"mod", K_MOD}, {L"new", K_NEW}, {L"next", K_NEXT}, {L"not", K_NOT}, {L"nothing", K_NOTHING}, {L"null", K_NULL},
    {L"on", K_ON}, {L"option", K_OPTION}, {L"or", K_OR}, {L"private", K_PRIVATE}, {L"property", K_PROPERTY},
    {L"public", K_PUBLIC}, {L"redim", K_REDIM}, {L"select", K_SELECT}, {L"set", K_SET}, {L"sub", K_SUB},
    {L"then", K_THEN}, {L"to", K_TO}, {L"true", K_TRUE}, {L"until", K_UNTIL}, {L"wend", K_WEND}, {L"while", K_WHILE},
    {L"with", K_WITH}, {L"xor", K_XOR}, {L"step", K_STEP}, {L"default", K_DEFAULT}, {L"error", K_ERROR},
    {L"explicit", K_EXPLICIT}, {L"preserve", K_PRESERVE}, {L"get", K_GET}, {L"resume", K_RESUME},
    {L"randomize", K_RANDOMIZE}, {L"execute", K_EXECUTE}, {L"executeglobal", K_EXECUTEGLOBAL}, {L"stop", K_STOP},
    {L"endif", K_RESERVED}, {L"enum", K_RESERVED}, {L"event", K_RESERVED}, {L"implements", K_RESERVED}, {L"optional", K_RESERVED},
    {L"paramarray", K_RESERVED}, {L"raiseevent", K_RESERVED}, {L"shared", K_RESERVED}, {L"static", K_RESERVED},
    {L"type", K_RESERVED}, {L"typeof", K_RESERVED}, {L"lset", K_RESERVED}, {L"rset", K_RESERVED}, {L"like", K_LIKE},
};

/* words that stay identifiers wherever a name is expected. */
static BOOL contextual(BYTE kind)
{
    return kind == K_STEP || kind == K_DEFAULT || kind == K_ERROR || kind == K_EXPLICIT || kind == K_PRESERVE ||
           kind == K_GET || kind == K_RESUME || kind == K_RANDOMIZE || kind == K_EXECUTE || kind == K_EXECUTEGLOBAL ||
           kind == K_STOP || kind == K_GOTO || kind == K_PROPERTY || kind == K_BYREF || kind == K_BYVAL;
}

static void add_token(Parser *p, Token *t)
{
    t->SpaceBefore = p->SpaceFlag;
    p->SpaceFlag = FALSE;
    t->LineStart = !p->Count || p->Tokens[p->Count - 1].Kind == T_NL ? t->Col : p->Tokens[p->Count - 1].LineStart;
    if (p->Count == p->Capacity) {
        UINT capacity = p->Capacity ? p->Capacity * 2 : 256;
        Token *tokens = arena_alloc(p->A, capacity * sizeof(Token));
        if (p->Tokens)
            memcpy(tokens, p->Tokens, p->Count * sizeof(Token));
        p->Tokens = tokens;
        p->Capacity = capacity;
    }
    p->Tokens[p->Count++] = *t;
}

static BSTR keep_bstr(Parser *p, PCWSTR text, SIZE_T length)
{
    BSTR b = SysAllocStringLen(text, (UINT)length);
    Unit *u = p->U;
    if (u->BstrCount == u->BstrCapacity) {
        UINT capacity = u->BstrCapacity ? u->BstrCapacity * 2 : 16;
        u->Bstrs = vbs_realloc(u->Bstrs, capacity * sizeof(BSTR));
        u->BstrCapacity = capacity;
    }
    u->Bstrs[u->BstrCount++] = b;
    return b;
}

static BOOL is_alpha(WCHAR c)
{
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

static BOOL is_digit(WCHAR c)
{
    return c >= L'0' && c <= L'9';
}

static BOOL is_hex(WCHAR c)
{
    return is_digit(c) || (c >= L'a' && c <= L'f') || (c >= L'A' && c <= L'F');
}

static void bad_token(Parser *p, Token *t, LONG number)
{
    t->Kind = T_EOF;
    t->Value.vt = VT_ERROR;
    t->Value.lVal = number;
    t->Bracketed = TRUE;
    add_token(p, t);
}

static void lex_number(Parser *p, Token *t)
{
    PCWSTR s = p->Cursor;
    if (*s == L'&') {
        WCHAR kind = (s[1] | 0x20);
        int base = kind == L'h' ? 16 : 8;
        PCWSTR d = s + 2;
        ULONGLONG v = 0;
        UINT digits = 0;
        while (base == 16 ? is_hex(*d) : (*d >= L'0' && *d <= L'7')) {
            int n = is_digit(*d) ? *d - L'0' : (*d | 0x20) - L'a' + 10;
            v = v * base + n;
            digits++;
            d++;
        }
        if (!digits || v > 0xFFFFFFFFull) {
            bad_token(p, t, digits ? E_VBS_OVERFLOW : E_VBS_SYNTAX);
            p->Col += (ULONG)(d - s);
            p->Cursor = d;
            return;
        }
        BOOL suffix = *d == L'&';
        if (suffix)
            d++;
        LONG value;
        if (!suffix && v <= 0xFFFF && v > 0x7FFF)
            value = (SHORT)v;
        else
            value = (LONG)(ULONG)v;
        t->Kind = T_INT;
        if (value >= -32767 && value <= 32767) {
            t->Value.vt = VT_I2;
            t->Value.iVal = (SHORT)value;
        } else {
            t->Value.vt = VT_I4;
            t->Value.lVal = value;
        }
        t->Length = (ULONG)(d - s);
        p->Col += t->Length;
        p->Cursor = d;
        return;
    }
    PCWSTR d = s;
    BOOL real = FALSE;
    while (is_digit(*d))
        d++;
    if (*d == L'.') {
        real = TRUE;
        d++;
        while (is_digit(*d))
            d++;
    }
    if ((*d == L'e' || *d == L'E') && (is_digit(d[1]) || ((d[1] == L'+' || d[1] == L'-') && is_digit(d[2])))) {
        real = TRUE;
        d += 2;
        while (is_digit(*d))
            d++;
    }
    WCHAR buffer[64];
    SIZE_T length = (SIZE_T)(d - s);
    if (length >= 63)
        length = 63;
    memcpy(buffer, s, length * sizeof(WCHAR));
    buffer[length] = 0;
    double value = 0;
    {
        VARIANT in, out;
        VariantInit(&out);
        in.vt = VT_BSTR;
        in.bstrVal = SysAllocString(buffer);
        HRESULT hr = VariantChangeTypeEx(&out, &in, 0x409, 0, VT_R8);
        SysFreeString(in.bstrVal);
        value = SUCCEEDED(hr) ? out.dblVal : 0;
    }
    t->Kind = real ? T_FLOAT : T_INT;
    if (!real && value <= 32767) {
        t->Value.vt = VT_I2;
        t->Value.iVal = (SHORT)value;
    } else if (!real && value <= 2147483647.0) {
        t->Value.vt = VT_I4;
        t->Value.lVal = (LONG)value;
    } else {
        t->Kind = T_FLOAT;
        t->Value.vt = VT_R8;
        t->Value.dblVal = value;
    }
    t->Length = (ULONG)(d - s);
    p->Col += t->Length;
    p->Cursor = d;
    if (is_alpha(*d) || *d == L'_') {
        /* a letter right after a number is not part of any token. */
        Token bad = *t;
        add_token(p, t);
        bad.Line = p->Line;
        bad.Col = p->Col + (*d == L'_' ? 1 : 0);
        bad_token(p, &bad, *d == L'_' ? 1032 : E_VBS_SYNTAX);
        while (is_alpha(*p->Cursor) || is_digit(*p->Cursor) || *p->Cursor == L'_') {
            p->Cursor++;
            p->Col++;
        }
        t->Kind = 0xFF;
    }
}

static void lex(Parser *p)
{
    BOOL line_start = TRUE;
    BYTE previous = T_NL;
    for (;;) {
        PCWSTR s = p->Cursor;
        WCHAR c = *s;
        Token t = {};
        t.Line = p->Line;
        t.Col = p->Col;
        t.Text = s;
        t.Length = 1;
        if (c == L' ' || c == L'\t' || c == 0xA0 || c == 0x3000) {
            p->Cursor++;
            p->Col++;
            p->SpaceFlag = TRUE;
            continue;
        }
        if (c == L'_') {
            /* a continuation: only blanks follow it before the line ends. */
            PCWSTR q = s + 1;
            while (*q == L' ' || *q == L'\t')
                q++;
            if (*q == L'\r' || *q == L'\n' || !*q) {
                if (*q == L'\r' && q[1] == L'\n')
                    q++;
                if (*q)
                    q++;
                p->Cursor = q;
                p->Line++;
                p->Col = 0;
                p->SpaceFlag = TRUE;
                continue;
            }
        }
        if (!c) {
            t.Kind = T_EOF;
            t.Length = 0;
            add_token(p, &t);
            return;
        }
        if (c == L'\r' || c == L'\n') {
            if (c == L'\r' && s[1] == L'\n')
                p->Cursor++;
            p->Cursor++;
            t.Kind = T_NL;
            add_token(p, &t);
            p->Line++;
            p->Col = 0;
            previous = T_NL;
            line_start = TRUE;
            continue;
        }
        if (c == L'\'') {
            while (*p->Cursor && *p->Cursor != L'\r' && *p->Cursor != L'\n') {
                p->Cursor++;
                p->Col++;
            }
            continue;
        }
        if (is_alpha(c)) {
            PCWSTR d = s;
            while (is_alpha(*d) || is_digit(*d) || *d == L'_')
                d++;
            ULONG length = (ULONG)(d - s);
            p->Cursor = d;
            p->Col += length;
            t.Length = length;
            if (length > 255) {
                t.Col = p->Col;
                bad_token(p, &t, 1030);
                continue;
            }
            t.Kind = T_IDENT;
            if (previous != T_DOT) {
                for (UINT i = 0; i < sizeof(g_keywords) / sizeof(g_keywords[0]); i++) {
                    PCWSTR k = g_keywords[i].Text;
                    UINT n = 0;
                    while (k[n] && fold(s[n]) == k[n])
                        n++;
                    if (!k[n] && n == length) {
                        t.Kind = g_keywords[i].Kind;
                        break;
                    }
                }
            }
            if (t.Kind == T_IDENT && length == 3 && fold(s[0]) == 'r' && fold(s[1]) == 'e' && fold(s[2]) == 'm' &&
                previous != T_DOT) {
                /* Rem starts a comment. */
                while (*p->Cursor && *p->Cursor != L'\r' && *p->Cursor != L'\n') {
                    p->Cursor++;
                    p->Col++;
                }
                continue;
            }
            add_token(p, &t);
            previous = t.Kind;
            line_start = FALSE;
            continue;
        }
        if (is_digit(c) || (c == L'.' && is_digit(s[1])) ||
            (c == L'&' && ((s[1] | 0x20) == L'h' || (s[1] | 0x20) == L'o'))) {
            lex_number(p, &t);
            if (t.Kind != 0xFF) {
                if (p->Count && p->Tokens[p->Count - 1].Line == t.Line && p->Tokens[p->Count - 1].Col == t.Col &&
                    p->Tokens[p->Count - 1].Kind == T_EOF && p->Tokens[p->Count - 1].Bracketed)
                    ;
                else
                    add_token(p, &t);
            }
            previous = T_INT;
            line_start = FALSE;
            continue;
        }
        line_start = FALSE;
        p->Cursor++;
        p->Col++;
        switch (c) {
        case L'"': {
            WCHAR *buffer = nullptr;
            SIZE_T length = 0, capacity = 0;
            PCWSTR q = p->Cursor;
            BOOL closed = FALSE;
            for (;;) {
                if (!*q || *q == L'\r' || *q == L'\n')
                    break;
                if (*q == L'"') {
                    if (q[1] == L'"') {
                        q++;
                    } else {
                        closed = TRUE;
                        q++;
                        break;
                    }
                }
                if (length + 1 > capacity) {
                    capacity = capacity ? capacity * 2 : 64;
                    buffer = vbs_realloc(buffer, capacity * sizeof(WCHAR));
                }
                buffer[length++] = *q++;
            }
            p->Col += (ULONG)(q - p->Cursor);
            p->Cursor = q;
            if (!closed) {
                vbs_free(buffer);
                t.Col = p->Col;
                bad_token(p, &t, 1033);
                break;
            }
            t.Kind = T_STRING;
            t.Value.vt = VT_BSTR;
            t.Value.bstrVal = keep_bstr(p, buffer ? buffer : L"", length);
            vbs_free(buffer);
            add_token(p, &t);
            break;
        }
        case L'#': {
            PCWSTR q = p->Cursor;
            while (*q && *q != L'#' && *q != L'\r' && *q != L'\n')
                q++;
            if (*q != L'#') {
                bad_token(p, &t, E_VBS_SYNTAX);
                break;
            }
            SIZE_T length = (SIZE_T)(q - p->Cursor);
            BSTR text = SysAllocStringLen(p->Cursor, (UINT)length);
            DATE date;
            HRESULT hr = VarDateFromStr(text, 0x409, 0, &date);
            SysFreeString(text);
            p->Col += (ULONG)(length + 1);
            p->Cursor = q + 1;
            if (FAILED(hr)) {
                bad_token(p, &t, E_VBS_SYNTAX);
                break;
            }
            t.Kind = T_DATE;
            t.Value.vt = VT_DATE;
            t.Value.date = date;
            add_token(p, &t);
            break;
        }
        case L'[': {
            PCWSTR q = p->Cursor;
            while (*q && *q != L']' && *q != L'\r' && *q != L'\n')
                q++;
            if (*q != L']') {
                p->Col += (ULONG)(q - p->Cursor);
                p->Cursor = q;
                t.Col = p->Col;
                bad_token(p, &t, 1007);
                break;
            }
            t.Kind = T_IDENT;
            t.Text = p->Cursor;
            t.Length = (ULONG)(q - p->Cursor);
            t.Bracketed = TRUE;
            p->Col += t.Length + 1;
            p->Cursor = q + 1;
            add_token(p, &t);
            break;
        }
        case L'(': t.Kind = T_LPAREN; add_token(p, &t); break;
        case L')': t.Kind = T_RPAREN; add_token(p, &t); break;
        case L',': t.Kind = T_COMMA; add_token(p, &t); break;
        case L'.': t.Kind = T_DOT; add_token(p, &t); break;
        case L':': t.Kind = T_COLON; add_token(p, &t); break;
        case L'+': t.Kind = T_PLUS; add_token(p, &t); break;
        case L'-': t.Kind = T_MINUS; add_token(p, &t); break;
        case L'*': t.Kind = T_STAR; add_token(p, &t); break;
        case L'/': t.Kind = T_SLASH; add_token(p, &t); break;
        case L'\\': t.Kind = T_BSLASH; add_token(p, &t); break;
        case L'^': t.Kind = T_CARET; add_token(p, &t); break;
        case L'&': t.Kind = T_AMP; add_token(p, &t); break;
        case L'=': t.Kind = T_EQ; add_token(p, &t); break;
        case L'<':
            if (*p->Cursor == L'>') {
                p->Cursor++;
                p->Col++;
                t.Kind = T_NE;
                t.Length = 2;
            } else if (*p->Cursor == L'=') {
                p->Cursor++;
                p->Col++;
                t.Kind = T_LE;
                t.Length = 2;
            } else
                t.Kind = T_LT;
            add_token(p, &t);
            break;
        case L'>':
            if (*p->Cursor == L'=') {
                p->Cursor++;
                p->Col++;
                t.Kind = T_GE;
                t.Length = 2;
            } else
                t.Kind = T_GT;
            add_token(p, &t);
            break;
        default:
            if (c == L'_')
                t.Col++;
            bad_token(p, &t, 1032);
            break;
        }
        previous = p->Count ? p->Tokens[p->Count - 1].Kind : T_NL;
        if (previous == T_EOF && p->Tokens[p->Count - 1].Bracketed)
            previous = T_INT;
    }
    (void)line_start;
}

/* ------------------------------------------------------------------ parser support */

static void fail(Parser *p, LONG number, const Token *at)
{
    p->ErrorNumber = number;
    p->ErrorLine = at->Line + p->FirstLine;
    p->ErrorCol = at->Col;
    vbs_longjmp(p->Jump, 1);
}

static Token *cur(Parser *p)
{
    Token *t = &p->Tokens[p->Index];
    if (t->Kind == T_EOF && t->Bracketed)
        fail(p, t->Value.lVal, t);
    return t;
}

static Token *peek_token(Parser *p, UINT ahead)
{
    UINT i = p->Index + ahead;
    if (i >= p->Count)
        i = p->Count - 1;
    return &p->Tokens[i];
}

static void advance(Parser *p)
{
    if (p->Index + 1 < p->Count)
        p->Index++;
}

static BOOL accept(Parser *p, BYTE kind)
{
    if (cur(p)->Kind == kind) {
        advance(p);
        return TRUE;
    }
    return FALSE;
}

static void expect(Parser *p, BYTE kind, LONG error)
{
    Token *t = cur(p);
    if (t->Kind != kind)
        fail(p, error, t);
    advance(p);
}

static void *node(Parser *p, SIZE_T size)
{
    void *n = arena_alloc(p->A, size);
    if (!n)
        fail(p, E_VBS_OUT_OF_MEMORY_C, cur(p));
    memset(n, 0, size);
    return n;
}

static Expr *new_expr(Parser *p, BYTE kind, const Token *at)
{
    Expr *e = node(p, sizeof(Expr));
    e->Kind = kind;
    e->Line = at->Line + p->FirstLine;
    e->Col = at->Col;
    return e;
}

static Stmt *new_stmt(Parser *p, BYTE kind, const Token *at)
{
    Stmt *s = node(p, sizeof(Stmt));
    s->Kind = kind;
    s->Line = at->Line + p->FirstLine;
    s->Col = at->LineStart;
    return s;
}

static PWSTR token_name(Parser *p, const Token *t)
{
    return arena_wcsndup(p->A, t->Text, t->Length);
}

static BOOL is_name_token(const Token *t)
{
    return t->Kind == T_IDENT || contextual(t->Kind);
}

static PWSTR expect_name(Parser *p, Token *out)
{
    Token *t = cur(p);
    if (!is_name_token(t))
        fail(p, 1010, t);
    if (out)
        *out = *t;
    PWSTR name = token_name(p, t);
    advance(p);
    return name;
}

static Expr *parse_expr(Parser *p);
static Stmt *parse_block(Parser *p, BOOL (*stop)(Parser *));
static Stmt *parse_statement(Parser *p);

/* ------------------------------------------------------------------ expressions */

static void parse_args(Parser *p, Args *args, BOOL parenthesized)
{
    Expr *items[64];
    UINT count = 0;
    BOOL expect_more = FALSE;
    for (;;) {
        Token *t = cur(p);
        Expr *item = nullptr;
        if (t->Kind == T_COMMA) {
            /* an omitted argument. */
            item = nullptr;
        } else if (parenthesized && t->Kind == T_RPAREN && !expect_more && count == 0) {
            break;
        } else {
            item = parse_expr(p);
        }
        if (count >= 64)
            fail(p, E_VBS_SYNTAX, t);
        items[count++] = item;
        if (accept(p, T_COMMA)) {
            expect_more = TRUE;
            Token *n = cur(p);
            if (n->Kind == T_RPAREN || n->Kind == T_NL || n->Kind == T_EOF || n->Kind == T_COLON)
                fail(p, E_VBS_SYNTAX, n);
            continue;
        }
        break;
    }
    args->Count = count;
    if (count) {
        args->Items = arena_alloc(p->A, count * sizeof(Expr *));
        memcpy(args->Items, items, count * sizeof(Expr *));
    }
}

static Expr *parse_primary(Parser *p)
{
    Token *t = cur(p);
    Expr *e;
    switch (t->Kind) {
    case T_INT:
    case T_FLOAT:
    case T_STRING:
    case T_DATE:
        e = new_expr(p, X_CONST, t);
        e->u.Value = t->Value;
        advance(p);
        return e;
    case K_TRUE:
    case K_FALSE:
        e = new_expr(p, X_CONST, t);
        e->u.Value.vt = VT_BOOL;
        e->u.Value.boolVal = t->Kind == K_TRUE ? VARIANT_TRUE : VARIANT_FALSE;
        advance(p);
        return e;
    case K_EMPTY:
        e = new_expr(p, X_CONST, t);
        advance(p);
        return e;
    case K_NULL:
        e = new_expr(p, X_CONST, t);
        e->u.Value.vt = VT_NULL;
        advance(p);
        return e;
    case K_NOTHING:
        e = new_expr(p, X_CONST, t);
        e->u.Value.vt = VT_DISPATCH;
        advance(p);
        return e;
    case K_ME:
        e = new_expr(p, X_ME, t);
        advance(p);
        return e;
    case K_NEW: {
        e = new_expr(p, X_NEW, t);
        advance(p);
        Token n;
        e->u.New.Name = expect_name(p, &n);
        return e;
    }
    case T_LPAREN: {
        advance(p);
        e = parse_expr(p);
        expect(p, T_RPAREN, 1006);
        e->Parens = 1;
        return e;
    }
    case T_DOT: {
        /* a member of the object of the innermost With. */
        advance(p);
        e = new_expr(p, X_MEMBER, t);
        Token n = *cur(p);
        if (!is_name_token(&n) && n.Kind < K_AND)
            fail(p, 1010, &n);
        if (n.Kind == T_EOF || n.Kind == T_NL || n.Kind == T_COLON)
            fail(p, 1010, &n);
        e->u.Member.Name = token_name(p, &n);
        advance(p);
        return e;
    }
    default:
        if (is_name_token(t)) {
            e = new_expr(p, X_NAME, t);
            e->u.Name.Name = token_name(p, t);
            e->u.Name.Hash = name_hash(e->u.Name.Name);
            advance(p);
            return e;
        }
        fail(p, t->Kind == T_NL || t->Kind == T_EOF || t->Kind == T_COLON || t->Kind == T_RPAREN ||
                        t->Kind == T_COMMA ? E_VBS_SYNTAX : E_VBS_SYNTAX, t);
    }
    return nullptr;
}

/* whether the parenthesis at the cursor holds the whole argument list: the matching one ends the statement. */
static BOOL paren_ends_statement(Parser *p)
{
    UINT depth = 0, i = p->Index;
    for (; i < p->Count; i++) {
        BYTE k = p->Tokens[i].Kind;
        if (k == T_LPAREN)
            depth++;
        else if (k == T_RPAREN) {
            if (--depth == 0)
                break;
        } else if (k == T_NL || k == T_EOF)
            return FALSE;
    }
    if (i + 1 >= p->Count)
        return TRUE;
    BYTE next = p->Tokens[i + 1].Kind;
    return next == T_NL || next == T_COLON || next == T_EOF || next == K_ELSE || next == T_EQ || next == T_LPAREN ||
           next == T_DOT || (next == K_END && i + 2 < p->Count && p->Tokens[i + 2].Kind == K_IF);
}

static Expr *parse_postfix(Parser *p)
{
    BOOL statement = p->StatementTarget;
    p->StatementTarget = FALSE;
    Expr *e = parse_primary(p);
    if (e->Kind == X_NEW)
        return e;
    for (;;) {
        Token *t = cur(p);
        /* in a statement, a dot after a blank starts the first argument, a member of the With object. */
        if (statement && t->Kind == T_DOT && t->SpaceBefore && e->Kind != X_CALL)
            break;
        if (statement && t->Kind == T_LPAREN && !paren_ends_statement(p))
            break;
        if (t->Kind == T_DOT) {
            advance(p);
            Token n = *cur(p);
            if (n.Kind == T_NL || n.Kind == T_EOF || n.Kind == T_COLON || n.Kind == T_LPAREN || n.Kind == T_INT ||
                n.Kind == T_FLOAT || n.Kind == T_STRING || n.Kind == T_DOT)
                fail(p, 1010, &n);
            Expr *m = new_expr(p, X_MEMBER, t);
            m->Line = e->Line;
            m->Col = e->Col;
            m->u.Member.Object = e;
            m->u.Member.Name = token_name(p, &n);
            advance(p);
            e = m;
        } else if (t->Kind == T_LPAREN && e->Parens != 1) {
            advance(p);
            if (e->Kind == X_MEMBER && !e->u.Member.HasArguments) {
                e->u.Member.HasArguments = TRUE;
                parse_args(p, &e->u.Member.Arguments, TRUE);
                expect(p, T_RPAREN, 1006);
                e->Parens = 2;
            } else {
                Expr *c = new_expr(p, X_CALL, t);
                c->Line = e->Line;
                c->Col = e->Col;
                c->u.Call.Target = e;
                parse_args(p, &c->u.Call.Arguments, TRUE);
                expect(p, T_RPAREN, 1006);
                c->Parens = 2;
                e = c;
            }
        } else
            break;
    }
    return e;
}

static Expr *parse_unary(Parser *p)
{
    Token *t = cur(p);
    if (t->Kind == T_MINUS || t->Kind == T_PLUS) {
        advance(p);
        Expr *e = new_expr(p, X_UNARY, t);
        e->Op = t->Kind == T_MINUS ? OP_NEG : OP_PLUS;
        e->u.Operand = parse_unary(p);
        return e;
    }
    return parse_postfix(p);
}

static Expr *binary(Parser *p, BYTE op, Expr *l, Expr *r, const Token *at)
{
    Expr *e = new_expr(p, X_BINARY, at);
    e->Line = l->Line;
    e->Col = l->Col;
    e->Op = op;
    e->u.Operands.Left = l;
    e->u.Operands.Right = r;
    return e;
}

static Expr *parse_pow(Parser *p)
{
    Expr *l = parse_unary(p);
    while (cur(p)->Kind == T_CARET) {
        Token t = *cur(p);
        advance(p);
        l = binary(p, OP_POW, l, parse_unary(p), &t);
    }
    return l;
}

static Expr *parse_mul(Parser *p)
{
    Expr *l = parse_pow(p);
    for (;;) {
        Token t = *cur(p);
        if (t.Kind == T_STAR || t.Kind == T_SLASH) {
            advance(p);
            l = binary(p, t.Kind == T_STAR ? OP_MUL : OP_DIV, l, parse_pow(p), &t);
        } else
            return l;
    }
}

static Expr *parse_idiv(Parser *p)
{
    Expr *l = parse_mul(p);
    while (cur(p)->Kind == T_BSLASH) {
        Token t = *cur(p);
        advance(p);
        l = binary(p, OP_IDIV, l, parse_mul(p), &t);
    }
    return l;
}

static Expr *parse_mod(Parser *p)
{
    Expr *l = parse_idiv(p);
    while (cur(p)->Kind == K_MOD) {
        Token t = *cur(p);
        advance(p);
        l = binary(p, OP_MOD, l, parse_idiv(p), &t);
    }
    return l;
}

static Expr *parse_add(Parser *p)
{
    Expr *l = parse_mod(p);
    for (;;) {
        Token t = *cur(p);
        if (t.Kind == T_PLUS || t.Kind == T_MINUS) {
            advance(p);
            l = binary(p, t.Kind == T_PLUS ? OP_ADD : OP_SUB, l, parse_mod(p), &t);
        } else
            return l;
    }
}

static Expr *parse_concat(Parser *p)
{
    Expr *l = parse_add(p);
    while (cur(p)->Kind == T_AMP) {
        Token t = *cur(p);
        advance(p);
        l = binary(p, OP_CAT, l, parse_add(p), &t);
    }
    return l;
}

static Expr *parse_compare(Parser *p)
{
    Expr *l = parse_concat(p);
    for (;;) {
        Token t = *cur(p);
        int op;
        switch (t.Kind) {
        case T_EQ: op = OP_EQ; break;
        case T_NE: op = OP_NE; break;
        case T_LT: op = OP_LT; break;
        case T_GT: op = OP_GT; break;
        case T_LE: op = OP_LE; break;
        case T_GE: op = OP_GE; break;
        case K_IS: op = OP_IS; break;
        case K_LIKE: op = OP_LIKE; break;
        default: return l;
        }
        advance(p);
        l = binary(p, (BYTE)op, l, parse_concat(p), &t);
    }
}

static Expr *parse_not(Parser *p)
{
    Token *t = cur(p);
    if (t->Kind == K_NOT) {
        advance(p);
        Expr *e = new_expr(p, X_UNARY, t);
        e->Op = OP_NOT;
        e->u.Operand = parse_not(p);
        return e;
    }
    return parse_compare(p);
}

static Expr *parse_and(Parser *p)
{
    Expr *l = parse_not(p);
    while (cur(p)->Kind == K_AND) {
        Token t = *cur(p);
        advance(p);
        l = binary(p, OP_AND, l, parse_not(p), &t);
    }
    return l;
}

static Expr *parse_or(Parser *p)
{
    Expr *l = parse_and(p);
    while (cur(p)->Kind == K_OR) {
        Token t = *cur(p);
        advance(p);
        l = binary(p, OP_OR, l, parse_and(p), &t);
    }
    return l;
}

static Expr *parse_xor(Parser *p)
{
    Expr *l = parse_or(p);
    while (cur(p)->Kind == K_XOR) {
        Token t = *cur(p);
        advance(p);
        l = binary(p, OP_XOR, l, parse_or(p), &t);
    }
    return l;
}

static Expr *parse_eqv(Parser *p)
{
    Expr *l = parse_xor(p);
    while (cur(p)->Kind == K_EQV) {
        Token t = *cur(p);
        advance(p);
        l = binary(p, OP_EQV, l, parse_xor(p), &t);
    }
    return l;
}

static Expr *parse_expr(Parser *p)
{
    Expr *l = parse_eqv(p);
    while (cur(p)->Kind == K_IMP) {
        Token t = *cur(p);
        advance(p);
        l = binary(p, OP_IMP, l, parse_eqv(p), &t);
    }
    return l;
}

/* ------------------------------------------------------------------ statements */

static BOOL at_end_of_statement(Parser *p)
{
    BYTE k = cur(p)->Kind;
    return k == T_NL || k == T_COLON || k == T_EOF;
}

static void end_statement(Parser *p)
{
    Token *t = cur(p);
    if (p->SingleLine) {
        /* a single line If ends at its line or at Else, and the line break is for the If to take. */
        if (t->Kind == T_COLON)
            advance(p);
        else if (t->Kind != T_NL && t->Kind != T_EOF && t->Kind != K_ELSE &&
                 !(t->Kind == K_END && peek_token(p, 1)->Kind == K_IF))
            fail(p, 1025, t);
        return;
    }
    if (t->Kind == T_NL || t->Kind == T_COLON) {
        advance(p);
        return;
    }
    if (t->Kind == T_EOF)
        return;
    fail(p, 1025, t);
}

static void skip_newlines(Parser *p)
{
    while (cur(p)->Kind == T_NL || cur(p)->Kind == T_COLON)
        advance(p);
}

static BOOL is_lvalue(Expr *e)
{
    if (e->Kind == X_NAME || e->Kind == X_MEMBER)
        return TRUE;
    if (e->Kind == X_CALL)
        return e->u.Call.Target->Kind == X_NAME || e->u.Call.Target->Kind == X_MEMBER || e->u.Call.Target->Kind == X_CALL;
    return FALSE;
}

/* the assignment or call a statement starting with a name is. */
static Stmt *parse_simple(Parser *p, BOOL set_keyword)
{
    Token first = set_keyword ? p->Tokens[p->Index - 1] : *cur(p);
    Stmt *s;
    p->StatementTarget = TRUE;
    Expr *target = parse_postfix(p);
    if (cur(p)->Kind == T_EQ && !(target->Parens == 1)) {
        if (!is_lvalue(target))
            fail(p, 1024, &first);
        advance(p);
        s = new_stmt(p, set_keyword ? S_SETASSIGN : S_ASSIGN, &first);
        s->u.Assign.Target = target;
        s->u.Assign.Value = parse_expr(p);
        return s;
    }
    if (set_keyword)
        fail(p, 1011, cur(p));
    s = new_stmt(p, S_CALL, &first);
    s->u.Call = target;
    if (target->Kind == X_MEMBER && target->u.Member.HasArguments && target->Parens == 2 && !at_end_of_statement(p)) {
        /* Name(args) followed by more text. */
        fail(p, 1025, cur(p));
    }
    if (!at_end_of_statement(p)) {
        /* arguments without parentheses. */
        Args args = {};
        if (target->Kind == X_NAME || (target->Kind == X_MEMBER && !target->u.Member.HasArguments)) {
            parse_args(p, &args, FALSE);
            if (target->Kind == X_NAME) {
                Expr *c = new_expr(p, X_CALL, &first);
                c->u.Call.Target = target;
                c->u.Call.Arguments = args;
                c->Op = 1;
                s->u.Call = c;
            } else {
                target->u.Member.HasArguments = TRUE;
                target->u.Member.Arguments = args;
                target->Op = 1;
            }
        } else
            fail(p, 1025, cur(p));
    } else if (target->Parens == 2) {
        /* a parenthesized argument list of a statement call must hold a single argument. */
        Args *args = target->Kind == X_MEMBER ? &target->u.Member.Arguments : &target->u.Call.Arguments;
        if (args->Count > 1) {
            Token end = p->Tokens[p->Index];
            fail(p, 1044, &end);
        }
        /* the one argument of a call without Call is an expression in parentheses, which goes by value. */
        if (args->Count == 1 && args->Items[0])
            args->Items[0]->Parens = 1;
        target->Op = 2;
    }
    return s;
}

static Stmt *parse_call_keyword(Parser *p)
{
    Token first = *cur(p);
    advance(p);
    Expr *target = parse_postfix(p);
    Stmt *s = new_stmt(p, S_CALL, &first);
    s->u.Call = target;
    if (target->Kind == X_NAME) {
        Expr *c = new_expr(p, X_CALL, &first);
        c->u.Call.Target = target;
        c->Op = 3;
        s->u.Call = c;
    } else if (target->Kind == X_CONST)
        ;
    else if (target->Kind != X_MEMBER && target->Kind != X_CALL)
        fail(p, 1024, &first);
    else
        target->Op = 3;
    return s;
}

static BOOL stop_never(Parser *p)
{
    return FALSE;
}

static BOOL stop_end_if(Parser *p)
{
    Token *t = cur(p);
    return t->Kind == K_ELSE || t->Kind == K_ELSEIF || (t->Kind == K_END && peek_token(p, 1)->Kind == K_IF);
}

static BOOL stop_case(Parser *p)
{
    Token *t = cur(p);
    return t->Kind == K_CASE || (t->Kind == K_END && peek_token(p, 1)->Kind == K_SELECT);
}

static BOOL stop_next(Parser *p)
{
    return cur(p)->Kind == K_NEXT;
}

static BOOL stop_loop(Parser *p)
{
    return cur(p)->Kind == K_LOOP;
}

static BOOL stop_wend(Parser *p)
{
    return cur(p)->Kind == K_WEND;
}

static BOOL stop_end_with(Parser *p)
{
    return cur(p)->Kind == K_END && peek_token(p, 1)->Kind == K_WITH;
}

static BOOL stop_end_sub(Parser *p)
{
    return cur(p)->Kind == K_END && (peek_token(p, 1)->Kind == K_SUB || peek_token(p, 1)->Kind == K_FUNCTION ||
                                     peek_token(p, 1)->Kind == K_PROPERTY);
}

static Stmt *parse_block(Parser *p, BOOL (*stop)(Parser *))
{
    Stmt *head = nullptr, **tail = &head;
    for (;;) {
        skip_newlines(p);
        Token *t = cur(p);
        if (t->Kind == T_EOF || stop(p))
            break;
        Stmt *s = parse_statement(p);
        if (s) {
            *tail = s;
            while (s->Next)
                s = s->Next;
            tail = &s->Next;
        }
    }
    return head;
}

static Stmt *parse_single_line_block(Parser *p)
{
    Stmt *head = nullptr, **tail = &head;
    for (;;) {
        Token *t = cur(p);
        if (t->Kind == T_NL || t->Kind == T_EOF || t->Kind == K_ELSE || (t->Kind == K_END && peek_token(p, 1)->Kind == K_IF))
            break;
        if (t->Kind == T_COLON) {
            advance(p);
            continue;
        }
        Stmt *s = parse_statement(p);
        if (s) {
            *tail = s;
            while (s->Next)
                s = s->Next;
            tail = &s->Next;
        }
    }
    return head;
}

static Stmt *parse_if_clause(Parser *p, BOOL elseif);

static Stmt *parse_if(Parser *p)
{
    return parse_if_clause(p, FALSE);
}

static Stmt *parse_if_clause(Parser *p, BOOL elseif)
{
    Token first = *cur(p);
    advance(p);
    Stmt *s = new_stmt(p, S_IF, &first);
    s->u.If.Condition = parse_expr(p);
    expect(p, K_THEN, 1017);
    Token *t = cur(p);
    if (t->Kind == T_NL || t->Kind == T_EOF) {
        s->u.If.Then = parse_block(p, stop_end_if);
        Token *n = cur(p);
        if (n->Kind == K_ELSEIF) {
            s->u.If.Else = parse_if_clause(p, TRUE);
            s->u.If.Else->Flag2 = 1;
            return s;
        }
        if (n->Kind == K_ELSE) {
            advance(p);
            s->u.If.Else = parse_block(p, stop_end_if);
        }
        if (!(cur(p)->Kind == K_END && peek_token(p, 1)->Kind == K_IF))
            fail(p, 1014, cur(p));
        advance(p);
        advance(p);
        end_statement(p);
        return s;
    }
    if (elseif) {
        /* what follows ElseIf on its line belongs to the block, which goes on to the End If. */
        s->u.If.Then = parse_block(p, stop_end_if);
        Token *n = cur(p);
        if (n->Kind == K_ELSEIF) {
            s->u.If.Else = parse_if_clause(p, TRUE);
            s->u.If.Else->Flag2 = 1;
            return s;
        }
        if (n->Kind == K_ELSE) {
            advance(p);
            s->u.If.Else = parse_block(p, stop_end_if);
        }
        if (!(cur(p)->Kind == K_END && peek_token(p, 1)->Kind == K_IF))
            fail(p, 1014, cur(p));
        advance(p);
        advance(p);
        end_statement(p);
        return s;
    }
    if (t->Kind == K_ELSE)
        fail(p, 1024, t);
    p->SingleLine++;
    s->u.If.Then = parse_single_line_block(p);
    if (cur(p)->Kind == K_ELSE) {
        advance(p);
        s->u.If.Else = parse_single_line_block(p);
    }
    if (cur(p)->Kind == K_END && peek_token(p, 1)->Kind == K_IF) {
        advance(p);
        advance(p);
    }
    p->SingleLine--;
    end_statement(p);
    return s;
}

static Stmt *parse_select(Parser *p)
{
    Token first = *cur(p);
    advance(p);
    expect(p, K_CASE, 1021);
    Stmt *s = new_stmt(p, S_SELECT, &first);
    s->u.Select.Subject = parse_expr(p);
    end_statement(p);
    CaseClause **tail = &s->u.Select.Cases;
    BOOL had_else = FALSE;
    skip_newlines(p);
    for (;;) {
        Token *t = cur(p);
        if (t->Kind == K_CASE) {
            if (had_else)
                fail(p, 1014, t);
            advance(p);
            CaseClause *c = node(p, sizeof(CaseClause));
            if (cur(p)->Kind == K_ELSE) {
                advance(p);
                c->IsElse = TRUE;
                had_else = TRUE;
            } else {
                Expr *values[64];
                UINT count = 0;
                do {
                    if (count >= 64)
                        fail(p, E_VBS_SYNTAX, cur(p));
                    values[count++] = parse_expr(p);
                } while (accept(p, T_COMMA));
                c->Values = arena_alloc(p->A, count * sizeof(Expr *));
                memcpy(c->Values, values, count * sizeof(Expr *));
                c->Count = count;
            }
            c->Body = parse_block(p, stop_case);
            *tail = c;
            tail = &c->Next;
            continue;
        }
        if (t->Kind == K_END && peek_token(p, 1)->Kind == K_SELECT) {
            advance(p);
            advance(p);
            end_statement(p);
            return s;
        }
        if (t->Kind == T_EOF)
            fail(p, 1014, t);
        fail(p, E_VBS_SYNTAX, t);
    }
}

static Stmt *parse_for(Parser *p)
{
    Token first = *cur(p);
    advance(p);
    if (cur(p)->Kind == K_EACH) {
        advance(p);
        Stmt *s = new_stmt(p, S_FOREACH, &first);
        if (!is_name_token(cur(p)))
            fail(p, 1010, cur(p));
        Expr *var = parse_postfix(p);
        if (var->Kind != X_NAME)
            fail(p, 1040, cur(p));
        s->u.Each.Var = var;
        expect(p, K_IN, 1046);
        s->u.Each.Group = parse_expr(p);
        end_statement(p);
        p->LoopFor++;
        s->u.Each.Body = parse_block(p, stop_next);
        p->LoopFor--;
        if (cur(p)->Kind != K_NEXT)
            fail(p, 1020, cur(p));
        advance(p);
        end_statement(p);
        return s;
    }
    Stmt *s = new_stmt(p, S_FOR, &first);
    if (!is_name_token(cur(p)))
        fail(p, 1010, cur(p));
    Expr *var = parse_postfix(p);
    if (var->Kind != X_NAME)
        fail(p, 1040, cur(p));
    s->u.For.Var = var;
    expect(p, T_EQ, 1011);
    s->u.For.From = parse_expr(p);
    expect(p, K_TO, 1013);
    s->u.For.To = parse_expr(p);
    if (cur(p)->Kind == K_STEP) {
        advance(p);
        s->u.For.Step = parse_expr(p);
    }
    end_statement(p);
    p->LoopFor++;
    s->u.For.Body = parse_block(p, stop_next);
    p->LoopFor--;
    if (cur(p)->Kind != K_NEXT)
        fail(p, 1020, cur(p));
    advance(p);
    end_statement(p);
    return s;
}

static Stmt *parse_do(Parser *p)
{
    Token first = *cur(p);
    advance(p);
    Stmt *s = new_stmt(p, S_DO, &first);
    Token *t = cur(p);
    if (t->Kind == K_WHILE || t->Kind == K_UNTIL) {
        s->Flag = t->Kind == K_UNTIL;
        advance(p);
        s->u.Loop.Condition = parse_expr(p);
    } else if (!at_end_of_statement(p))
        fail(p, 1028, t);
    end_statement(p);
    p->LoopDo++;
    s->u.Loop.Body = parse_block(p, stop_loop);
    p->LoopDo--;
    if (cur(p)->Kind != K_LOOP)
        fail(p, 1019, cur(p));
    advance(p);
    t = cur(p);
    if (t->Kind == K_WHILE || t->Kind == K_UNTIL) {
        if (s->u.Loop.Condition)
            fail(p, 1025, t);
        s->Flag = t->Kind == K_UNTIL;
        s->Flag2 = 1;
        advance(p);
        s->u.Loop.Condition = parse_expr(p);
    }
    end_statement(p);
    return s;
}

static Stmt *parse_while(Parser *p)
{
    Token first = *cur(p);
    advance(p);
    Stmt *s = new_stmt(p, S_WHILE, &first);
    s->u.Loop.Condition = parse_expr(p);
    end_statement(p);
    s->u.Loop.Body = parse_block(p, stop_wend);
    if (cur(p)->Kind != K_WEND)
        fail(p, 1018, cur(p));
    advance(p);
    end_statement(p);
    return s;
}

static Stmt *parse_with(Parser *p)
{
    Token first = *cur(p);
    advance(p);
    Stmt *s = new_stmt(p, S_WITH, &first);
    s->u.With.Object = parse_expr(p);
    end_statement(p);
    s->u.With.Body = parse_block(p, stop_end_with);
    if (!(cur(p)->Kind == K_END && peek_token(p, 1)->Kind == K_WITH))
        fail(p, 1014, cur(p));
    advance(p);
    advance(p);
    end_statement(p);
    return s;
}

static Stmt *parse_exit(Parser *p)
{
    Token first = *cur(p);
    advance(p);
    Token *t = cur(p);
    Stmt *s = new_stmt(p, S_EXIT, &first);
    switch (t->Kind) {
    case K_FOR:
        if (!p->LoopFor)
            fail(p, 1039, t);
        s->Flag = EXIT_FOR;
        break;
    case K_DO:
        if (!p->LoopDo)
            fail(p, 1039, t);
        s->Flag = EXIT_DO;
        break;
    case K_SUB:
    case K_FUNCTION:
    case K_PROPERTY: {
        if (!p->InProc)
            fail(p, 1039, t);
        BOOL right = t->Kind == K_SUB ? p->InSub : t->Kind == K_FUNCTION ? p->InFunction : p->InProperty;
        if (!right)
            fail(p, p->InSub ? 1016 : p->InFunction ? 1015 : 1050, t);
        s->Flag = t->Kind == K_SUB ? EXIT_SUB : t->Kind == K_FUNCTION ? EXIT_FUNCTION : EXIT_PROPERTY;
        break;
    }
    default:
        fail(p, 1039, t);
    }
    advance(p);
    end_statement(p);
    return s;
}

static Stmt *parse_on(Parser *p)
{
    Token first = *cur(p);
    advance(p);
    if (cur(p)->Kind != K_ERROR)
        fail(p, E_VBS_SYNTAX, cur(p));
    advance(p);
    Stmt *s = new_stmt(p, S_ONERROR, &first);
    Token *t = cur(p);
    if (t->Kind == K_RESUME) {
        advance(p);
        if (cur(p)->Kind != K_NEXT)
            fail(p, E_VBS_SYNTAX, cur(p));
        advance(p);
        s->Flag = 1;
    } else if (t->Kind == K_GOTO) {
        advance(p);
        Token *z = cur(p);
        if (z->Kind != T_INT || z->Value.vt != VT_I2 || z->Value.iVal != 0)
            fail(p, E_VBS_SYNTAX, z);
        advance(p);
    } else
        fail(p, E_VBS_SYNTAX, t);
    end_statement(p);
    return s;
}

static void declare_name(Parser *p, PCWSTR name, const Token *at);

static Stmt *parse_dim(Parser *p, BYTE is_public, BOOL in_class_field)
{
    Token first = *cur(p);
    advance(p);
    Stmt *s = new_stmt(p, S_DIM, &first);
    DimItem **tail = &s->u.Dim;
    do {
        Token nt;
        DimItem *d = node(p, sizeof(DimItem));
        d->Public = is_public;
        d->Name = expect_name(p, &nt);
        if (accept(p, T_LPAREN)) {
            d->IsArray = TRUE;
            if (cur(p)->Kind != T_RPAREN)
                parse_args(p, &d->Bounds, TRUE);
            expect(p, T_RPAREN, 1006);
        }
        d->Target = nullptr;
        declare_name(p, d->Name, &nt);
        *tail = d;
        tail = &d->Next;
    } while (accept(p, T_COMMA));
    (void)in_class_field;
    end_statement(p);
    return s;
}

static Stmt *parse_redim(Parser *p)
{
    Token first = *cur(p);
    advance(p);
    Stmt *s = new_stmt(p, S_REDIM, &first);
    if (cur(p)->Kind == K_PRESERVE) {
        s->Flag = 1;
        advance(p);
    }
    DimItem **tail = &s->u.Dim;
    do {
        DimItem *d = node(p, sizeof(DimItem));
        Token nt = *cur(p);
        d->Name = expect_name(p, &nt);
        d->IsArray = TRUE;
        Expr *target = new_expr(p, X_NAME, &nt);
        target->u.Name.Name = d->Name;
        target->u.Name.Hash = name_hash(d->Name);
        d->Target = target;
        if (cur(p)->Kind != T_LPAREN)
            fail(p, 1005, cur(p));
        advance(p);
        if (cur(p)->Kind == T_RPAREN)
            fail(p, E_VBS_SYNTAX, cur(p));
        parse_args(p, &d->Bounds, TRUE);
        expect(p, T_RPAREN, 1006);
        *tail = d;
        tail = &d->Next;
    } while (accept(p, T_COMMA));
    end_statement(p);
    return s;
}

static Expr *literal_value(Parser *p)
{
    Token *t = cur(p);
    UINT start = p->Index;
    BOOL negative = FALSE, signed_ = FALSE;
    BOOL paren = FALSE;
    if (t->Kind == T_LPAREN) {
        paren = TRUE;
        advance(p);
        t = cur(p);
    }
    if (t->Kind == T_MINUS || t->Kind == T_PLUS) {
        negative = t->Kind == T_MINUS;
        signed_ = TRUE;
        advance(p);
        t = cur(p);
    }
    Expr *e;
    switch (t->Kind) {
    case T_INT:
    case T_FLOAT:
    case T_STRING:
    case T_DATE:
        if (signed_ && (t->Kind == T_STRING || t->Kind == T_DATE))
            fail(p, 1045, t);
        e = new_expr(p, X_CONST, t);
        e->u.Value = t->Value;
        if (negative) {
            VARIANT v = e->u.Value, r;
            VariantInit(&r);
            VarNeg(&v, &r);
            e->u.Value = r;
        }
        break;
    case K_TRUE:
    case K_FALSE:
        e = new_expr(p, X_CONST, t);
        e->u.Value.vt = VT_BOOL;
        e->u.Value.boolVal = t->Kind == K_TRUE ? VARIANT_TRUE : VARIANT_FALSE;
        break;
    case K_EMPTY:
        e = new_expr(p, X_CONST, t);
        break;
    case K_NULL:
        e = new_expr(p, X_CONST, t);
        e->u.Value.vt = VT_NULL;
        break;
    case K_NOTHING:
        e = new_expr(p, X_CONST, t);
        e->u.Value.vt = VT_DISPATCH;
        break;
    default:
        parse_expr(p);
        fail(p, 1045, cur(p));
        return nullptr;
    }
    advance(p);
    if (paren)
        expect(p, T_RPAREN, 1006);
    if (cur(p)->Kind != T_COMMA && !at_end_of_statement(p)) {
        p->Index = start;
        parse_expr(p);
        fail(p, 1045, cur(p));
    }
    return e;
}

static Stmt *parse_const(Parser *p, BYTE is_public)
{
    Token first = *cur(p);
    advance(p);
    Stmt *head = nullptr, **tail = &head;
    do {
        Token nt;
        Stmt *s = new_stmt(p, S_CONST, &first);
        s->u.Const.Name = expect_name(p, &nt);
        expect(p, T_EQ, 1011);
        s->u.Const.Value = literal_value(p);
        s->Flag = is_public;
        declare_name(p, s->u.Const.Name, &nt);
        *tail = s;
        tail = &s->Next;
    } while (accept(p, T_COMMA));
    end_statement(p);
    return head;
}

static BOOL scope_has(Parser *p, PCWSTR name)
{
    if (p->CurrentProc) {
        Proc *pr = p->CurrentProc;
        for (UINT i = 0; i < pr->LocalCount; i++)
            if (name_equal(pr->Locals[i], name))
                return TRUE;
        return FALSE;
    }
    if (p->CurrentClass) {
        Member *m = member_find(p->CurrentClass, name, name_hash(name));
        return m != nullptr;
    }
    return FALSE;
}

static void proc_add_local(Parser *p, Proc *pr, PCWSTR name)
{
    if (pr->LocalCount == pr->LocalCapacity) {
        UINT capacity = pr->LocalCapacity ? pr->LocalCapacity * 2 : 8;
        PWSTR *names = arena_alloc(p->A, capacity * sizeof(PWSTR));
        ULONG *hashes = arena_alloc(p->A, capacity * sizeof(ULONG));
        if (pr->LocalCount) {
            memcpy(names, pr->Locals, pr->LocalCount * sizeof(PWSTR));
            memcpy(hashes, pr->LocalHashes, pr->LocalCount * sizeof(ULONG));
        }
        pr->Locals = names;
        pr->LocalHashes = hashes;
        pr->LocalCapacity = capacity;
    }
    pr->Locals[pr->LocalCount] = (PWSTR)name;
    pr->LocalHashes[pr->LocalCount] = name_hash(name);
    pr->LocalCount++;
}

/* script level names go in a list of the unit so the second one can be told. */
static void claim_name(Parser *p, PCWSTR name, const Token *at)
{
    Unit *u = p->U;
    for (UINT i = 0; i < u->NameCount; i++)
        if (name_equal(u->Names[i], name))
            fail(p, 1041, at);
    if (u->NameCount == u->NameCapacity) {
        UINT capacity = u->NameCapacity ? u->NameCapacity * 2 : 16;
        PWSTR *names = arena_alloc(p->A, capacity * sizeof(PWSTR));
        if (u->NameCount)
            memcpy(names, u->Names, u->NameCount * sizeof(PWSTR));
        u->Names = names;
        u->NameCapacity = capacity;
    }
    u->Names[u->NameCount++] = (PWSTR)name;
}

static void declare_name(Parser *p, PCWSTR name, const Token *at)
{
    if (p->CurrentProc) {
        if (scope_has(p, name))
            fail(p, 1041, at);
        proc_add_local(p, p->CurrentProc, name);
        return;
    }
    if (p->CurrentClass) {
        return;
    }
    claim_name(p, name, at);
}

static Param *parse_params(Parser *p, UINT *count_out, Proc *proc)
{
    Param params[64];
    UINT count = 0;
    if (accept(p, T_LPAREN)) {
        if (cur(p)->Kind != T_RPAREN) {
            do {
                Param prm = {};
                if (cur(p)->Kind == K_BYVAL) {
                    advance(p);
                    prm.ByVal = TRUE;
                } else if (cur(p)->Kind == K_BYREF) {
                    advance(p);
                }
                Token nt;
                prm.Name = expect_name(p, &nt);
                for (UINT i = 0; i < count; i++)
                    if (name_equal(params[i].Name, prm.Name))
                        fail(p, 1041, &nt);
                if (accept(p, T_LPAREN))
                    expect(p, T_RPAREN, 1006);
                if (count >= 64)
                    fail(p, E_VBS_SYNTAX, &nt);
                params[count++] = prm;
            } while (accept(p, T_COMMA));
        }
        expect(p, T_RPAREN, 1006);
    }
    *count_out = count;
    Param *copy = nullptr;
    if (count) {
        copy = arena_alloc(p->A, count * sizeof(Param));
        memcpy(copy, params, count * sizeof(Param));
    }
    for (UINT i = 0; i < count; i++)
        proc_add_local(p, proc, copy[i].Name);
    return copy;
}

static Proc *parse_proc(Parser *p, BYTE public_, BOOL is_default, BYTE default_error_flag)
{
    Token first = *cur(p);
    BYTE kind = PK_SUB;
    Token *t = cur(p);
    BYTE end_kind;
    if (t->Kind == K_SUB) {
        kind = PK_SUB;
        end_kind = K_SUB;
    } else if (t->Kind == K_FUNCTION) {
        kind = PK_FUNCTION;
        end_kind = K_FUNCTION;
    } else {
        advance(p);
        Token *k = cur(p);
        if (k->Kind == K_GET)
            kind = PK_GET;
        else if (k->Kind == K_LET)
            kind = PK_LET;
        else if (k->Kind == K_SET)
            kind = PK_SET;
        else
            fail(p, 1049, k);
        end_kind = K_PROPERTY;
    }
    advance(p);
    Proc *proc = node(p, sizeof(Proc));
    Token nt;
    proc->Name = expect_name(p, &nt);
    proc->Kind = kind;
    proc->Public = public_;
    proc->IsDefault = is_default;
    proc->Line = first.Line + p->FirstLine;
    proc->Col = first.Col;
    proc->Unit = p->U;
    proc->Cls = p->CurrentClass;
    proc->ResultSlot = -1;
    Proc *saved = p->CurrentProc;
    p->CurrentProc = proc;
    p->InProc = TRUE;
    BOOL ss = p->InSub, sf = p->InFunction, sp = p->InProperty;
    UINT lf = p->LoopFor, ld = p->LoopDo;
    p->LoopFor = p->LoopDo = 0;
    p->InSub = kind == PK_SUB;
    p->InFunction = kind == PK_FUNCTION;
    p->InProperty = kind >= PK_GET;
    proc->Params = parse_params(p, &proc->ParamCount, proc);
    /* a function or property get returns through a slot named like it. */
    if (kind == PK_FUNCTION || kind == PK_GET) {
        proc->ResultSlot = (LONG)proc->LocalCount;
        proc_add_local(p, proc, proc->Name);
    }
    if (cur(p)->Kind == T_NL || cur(p)->Kind == T_COLON)
        advance(p);
    proc->Body = parse_block(p, stop_end_sub);
    if (!(cur(p)->Kind == K_END && peek_token(p, 1)->Kind == end_kind)) {
        if (cur(p)->Kind == K_END && (peek_token(p, 1)->Kind == K_SUB || peek_token(p, 1)->Kind == K_FUNCTION ||
                                      peek_token(p, 1)->Kind == K_PROPERTY))
            fail(p, end_kind == K_SUB ? 1016 : end_kind == K_FUNCTION ? 1015 : 1050, peek_token(p, 1));
        fail(p, end_kind == K_SUB ? 1016 : end_kind == K_FUNCTION ? 1015 : 1050, cur(p));
    }
    advance(p);
    advance(p);
    end_statement(p);
    proc->DeclaredCount = proc->LocalCount;
    p->CurrentProc = saved;
    p->InProc = saved != nullptr;
    p->InSub = ss;
    p->InFunction = sf;
    p->InProperty = sp;
    p->LoopFor = lf;
    p->LoopDo = ld;
    (void)default_error_flag;
    return proc;
}

static Member *class_member_add(Parser *p, Class *cls, PCWSTR name, BYTE kind, const Token *at)
{
    ULONG hash = name_hash(name);
    Member *m = member_find(cls, name, hash);
    if (m) {
        if (!(kind == M_PROPERTY && m->Kind == M_PROPERTY))
            fail(p, 1041, at);
        return m;
    }
    m = node(p, sizeof(Member));
    m->Name = (PWSTR)name;
    m->Hash = hash;
    m->Kind = kind;
    m->Next = cls->Members;
    cls->Members = m;
    m->Id = 1000 + (DISPID)cls->MemberCount++;
    return m;
}

static void parse_class(Parser *p)
{
    Token first = *cur(p);
    advance(p);
    Token nt;
    Class *cls = node(p, sizeof(Class));
    cls->Name = expect_name(p, &nt);
    cls->Unit = p->U;
    cls->Line = first.Line + p->FirstLine;
    cls->Col = first.Col;
    end_statement(p);
    claim_name(p, cls->Name, &nt);
    cls->Next = p->U->Classes;
    p->U->Classes = cls;
    Class *saved = p->CurrentClass;
    p->CurrentClass = cls;
    p->InClass = TRUE;
    for (;;) {
        skip_newlines(p);
        Token *t = cur(p);
        if (t->Kind == T_EOF)
            fail(p, 1014, t);
        if (t->Kind == K_END) {
            if (peek_token(p, 1)->Kind != K_CLASS)
                fail(p, E_VBS_SYNTAX, t);
            advance(p);
            advance(p);
            end_statement(p);
            break;
        }
        BYTE vis = 1;
        BOOL is_default = FALSE, explicit_vis = FALSE;
        Token start = *t;
        if (t->Kind == K_PUBLIC || t->Kind == K_PRIVATE) {
            vis = t->Kind == K_PUBLIC;
            explicit_vis = TRUE;
            advance(p);
            t = cur(p);
        }
        if (t->Kind == K_DEFAULT) {
            is_default = TRUE;
            advance(p);
            t = cur(p);
            if (!explicit_vis || !vis) {
                if (t->Kind == K_SUB || t->Kind == K_FUNCTION || t->Kind == K_PROPERTY) {
                    if (explicit_vis && !vis)
                        fail(p, 1057, &start);
                    fail(p, 1057, &start);
                }
                fail(p, 1056, t);
            }
            if (t->Kind != K_SUB && t->Kind != K_FUNCTION && t->Kind != K_PROPERTY)
                fail(p, 1056, t);
        }
        if (t->Kind == K_SUB || t->Kind == K_FUNCTION || t->Kind == K_PROPERTY) {
            Token kt = *t;
            Proc *proc = parse_proc(p, vis, is_default, 0);
            if (is_default && proc->Kind != PK_GET && proc->Kind != PK_SUB && proc->Kind != PK_FUNCTION)
                fail(p, 1058, &kt);
            if (is_default && proc->Kind == PK_SET)
                fail(p, 1058, &kt);
            Member *m;
            if (proc->Kind == PK_SUB || proc->Kind == PK_FUNCTION) {
                if (name_equal(proc->Name, L"Class_Initialize") || name_equal(proc->Name, L"Class_Terminate")) {
                    if (proc->ParamCount)
                        fail(p, 1053, &kt);
                    if (name_equal(proc->Name, L"Class_Initialize"))
                        cls->Initialize = proc;
                    else
                        cls->Terminate = proc;
                }
                m = class_member_add(p, cls, proc->Name, M_METHOD, &kt);
                m->Method = proc;
            } else {
                m = class_member_add(p, cls, proc->Name, M_PROPERTY, &kt);
                Proc **slot = proc->Kind == PK_GET ? &m->Get : proc->Kind == PK_LET ? &m->Let : &m->Set;
                if (*slot)
                    fail(p, 1041, &kt);
                *slot = proc;
                if (proc->Kind != PK_GET && proc->ParamCount < 1)
                    fail(p, 1054, &kt);
                UINT args = proc->Kind == PK_GET ? proc->ParamCount : proc->ParamCount - 1;
                Proc *others[3] = {m->Get, m->Let, m->Set};
                for (int i = 0; i < 3; i++) {
                    if (others[i] && others[i] != proc) {
                        UINT oa = others[i]->Kind == PK_GET ? others[i]->ParamCount : others[i]->ParamCount - 1;
                        if (oa != args)
                            fail(p, 1051, &kt);
                    }
                }
            }
            m->Public = vis;
            if (is_default) {
                if (cls->DefaultMember)
                    fail(p, 1052, &kt);
                cls->DefaultMember = m;
            }
            continue;
        }
        if (t->Kind == K_DIM || explicit_vis) {
            if (t->Kind == K_DIM) {
                advance(p);
            }
            do {
                Token ft;
                PWSTR name = expect_name(p, &ft);
                Member *m = class_member_add(p, cls, name, M_FIELD, &ft);
                m->Public = vis;
                m->Slot = (LONG)cls->FieldCount++;
                if (accept(p, T_LPAREN)) {
                    if (cur(p)->Kind != T_RPAREN) {
                        Args dummy;
                        parse_args(p, &dummy, TRUE);
                    }
                    expect(p, T_RPAREN, 1006);
                    m->Public |= 2;
                }
            } while (accept(p, T_COMMA));
            end_statement(p);
            continue;
        }
        fail(p, E_VBS_SYNTAX, t);
    }
    p->CurrentClass = saved;
    p->InClass = saved != nullptr;
}

static Stmt *parse_declaration(Parser *p, BYTE is_public, BOOL is_default)
{
    Token *t = cur(p);
    Stmt *s;
    if (t->Kind == K_SUB || t->Kind == K_FUNCTION || t->Kind == K_PROPERTY) {
        if (p->InProc || p->InClass)
            fail(p, E_VBS_SYNTAX, t);
        if (t->Kind == K_PROPERTY)
            fail(p, 1048, t);
        Token first = *t;
        Proc *proc = parse_proc(p, is_public, is_default, 0);
        claim_name(p, proc->Name, &first);
        proc->Next = p->U->Procedures;
        p->U->Procedures = proc;
        s = new_stmt(p, S_PROCDECL, &first);
        s->u.Procedure = proc;
        return s;
    }
    if (t->Kind == K_CLASS) {
        if (p->InProc || p->InClass)
            fail(p, E_VBS_SYNTAX, t);
        parse_class(p);
        return nullptr;
    }
    if (t->Kind == K_CONST) {
        if (p->InProc)
            return parse_const(p, is_public);
        return parse_const(p, is_public);
    }
    if (p->InProc)
        fail(p, E_VBS_SYNTAX, &p->Tokens[p->Index - 1]);
    if (t->Kind == K_DIM)
        fail(p, 1010, t);
    /* Public x, y */
    Token first = *t;
    s = new_stmt(p, S_DIM, &first);
    DimItem **tail = &s->u.Dim;
    do {
        Token nt;
        DimItem *d = node(p, sizeof(DimItem));
        d->Public = is_public;
        d->Name = expect_name(p, &nt);
        if (accept(p, T_LPAREN)) {
            d->IsArray = TRUE;
            if (cur(p)->Kind != T_RPAREN)
                parse_args(p, &d->Bounds, TRUE);
            expect(p, T_RPAREN, 1006);
        }
        declare_name(p, d->Name, &nt);
        *tail = d;
        tail = &d->Next;
    } while (accept(p, T_COMMA));
    end_statement(p);
    return s;
}

static Stmt *parse_statement(Parser *p)
{
    Token *t = cur(p);
    Stmt *s = nullptr;
    p->StatementCount++;
    switch (t->Kind) {
    case K_IF: return parse_if(p);
    case K_SELECT: return parse_select(p);
    case K_FOR: return parse_for(p);
    case K_DO: return parse_do(p);
    case K_WHILE: return parse_while(p);
    case K_WITH: return parse_with(p);
    case K_EXIT: return parse_exit(p);
    case K_ON: return parse_on(p);
    case K_DIM:
        if (p->InClass && !p->InProc)
            fail(p, E_VBS_SYNTAX, t);
        return parse_dim(p, 1, FALSE);
    case K_REDIM: return parse_redim(p);
    case K_CONST: return parse_const(p, 1);
    case K_PUBLIC:
    case K_PRIVATE: {
        BYTE vis = t->Kind == K_PUBLIC;
        advance(p);
        BOOL def = FALSE;
        if (cur(p)->Kind == K_DEFAULT && (peek_token(p, 1)->Kind == K_SUB || peek_token(p, 1)->Kind == K_FUNCTION ||
                                          peek_token(p, 1)->Kind == K_PROPERTY)) {
            def = TRUE;
            advance(p);
        }
        return parse_declaration(p, vis, def);
    }
    case K_DEFAULT: {
        advance(p);
        return parse_declaration(p, 1, TRUE);
    }
    case K_SUB:
    case K_FUNCTION:
    case K_CLASS:
    case K_PROPERTY:
        return parse_declaration(p, 1, FALSE);
    case K_CALL: {
        s = parse_call_keyword(p);
        end_statement(p);
        return s;
    }
    case K_SET: {
        advance(p);
        s = parse_simple(p, TRUE);
        end_statement(p);
        return s;
    }
    case K_ERASE: {
        Token first = *t;
        advance(p);
        s = new_stmt(p, S_ERASE, &first);
        if (!at_end_of_statement(p))
            s->u.Randomize = parse_expr(p);
        end_statement(p);
        return s;
    }
    case K_OPTION: {
        Token first = *t;
        if (p->StatementCount > 1 || p->InProc || p->InClass)
            fail(p, 1024, t);
        advance(p);
        if (cur(p)->Kind != K_EXPLICIT)
            fail(p, E_VBS_SYNTAX, cur(p));
        advance(p);
        s = new_stmt(p, S_OPTION, &first);
        p->U->Explicit = TRUE;
        if (!at_end_of_statement(p))
            fail(p, E_VBS_SYNTAX, cur(p));
        end_statement(p);
        return s;
    }
    case K_RANDOMIZE: {
        Token first = *t;
        advance(p);
        s = new_stmt(p, S_RANDOMIZE, &first);
        if (!at_end_of_statement(p))
            s->u.Randomize = parse_expr(p);
        end_statement(p);
        return s;
    }
    case K_STOP:
        advance(p);
        end_statement(p);
        return nullptr;
    case K_EXECUTE:
    case K_EXECUTEGLOBAL: {
        Token first = *t;
        advance(p);
        if (cur(p)->Kind == T_EQ || cur(p)->Kind == T_DOT) {
            p->Index--;
            goto simple;
        }
        s = new_stmt(p, S_EXECUTE, &first);
        s->u.Execute.Global = first.Kind == K_EXECUTEGLOBAL;
        if (!at_end_of_statement(p))
            s->u.Execute.Source = parse_expr(p);
        end_statement(p);
        return s;
    }
    case T_IDENT:
    case K_ME:
    case T_DOT:
    case K_ERROR:
    case K_EXPLICIT:
    case K_PRESERVE:
    case K_GET:
    case K_STEP:
    case K_BYREF:
    case K_BYVAL:
    simple:
        s = parse_simple(p, FALSE);
        end_statement(p);
        return s;
    case T_NL:
    case T_COLON:
        advance(p);
        return nullptr;
    case K_NEXT:
        fail(p, 1055, t);
        return nullptr;
    case K_LOOP:
        fail(p, 1038, t);
        return nullptr;
    default:
        fail(p, 1024, t);
    }
    return nullptr;
}

/* ------------------------------------------------------------------ entry points */

static LONG fail_unit(Parser *p)
{
    return p->ErrorNumber;
}

static PWSTR line_text(Arena *a, PCWSTR source, ULONG line)
{
    PCWSTR s = source;
    while (line && *s) {
        if (*s == L'\r' || *s == L'\n') {
            if (*s == L'\r' && s[1] == L'\n')
                s++;
            line--;
        }
        s++;
    }
    PCWSTR e = s;
    while (*e && *e != L'\r' && *e != L'\n')
        e++;
    return arena_wcsndup(a, s, (SIZE_T)(e - s));
}

static void unit_reset(Unit *unit)
{
    if (unit->Bstrs) {
        for (UINT i = 0; i < unit->BstrCount; i++)
            SysFreeString(unit->Bstrs[i]);
        vbs_free(unit->Bstrs);
    }
    vbs_free(unit->Source);
    arena_destroy(unit->Memory);
    vbs_free(unit);
}

void unit_free(Unit *unit)
{
    if (unit)
        unit_reset(unit);
}

/* parses a unit, or reports the compile error through the engine and returns null. */
Unit *parse_unit(Engine *e, PCWSTR text, ULONG first_line, DWORD_PTR cookie, BOOL expression, BOOL execute)
{
    Parser p = {};
    Unit *unit = vbs_alloc(sizeof(Unit));
    if (!unit)
        return nullptr;
    unit->Memory = arena_create();
    unit->Source = vbs_wcsdup(text);
    unit->FirstLine = first_line;
    unit->Cookie = cookie;
    p.E = e;
    p.U = unit;
    p.A = unit->Memory;
    p.Source = unit->Source;
    p.Cursor = unit->Source;
    p.FirstLine = first_line;
    p.ExecuteMode = execute;
    p.StatementCount = 0;
    lex(&p);
    if (vbs_setjmp(p.Jump)) {
        /* the registers are not restored, so only what lives in memory is used here. */
        Parser *q = &p;
        Unit *failed = q->U;
        PWSTR line = line_text(failed->Memory, failed->Source, q->ErrorLine - q->FirstLine);
        raise_compile(q->E, fail_unit(q), q->ErrorLine, q->ErrorCol, line, failed->Cookie);
        unit_reset(failed);
        return nullptr;
    }
    if (expression) {
        Expr *x = parse_expr(&p);
        skip_newlines(&p);
        if (cur(&p)->Kind != T_EOF)
            fail(&p, E_VBS_SYNTAX, cur(&p));
        Stmt *s = new_stmt(&p, S_CALL, &p.Tokens[0]);
        Expr *wrapper = new_expr(&p, X_EVAL, &p.Tokens[0]);
        wrapper->u.Operand = x;
        s->u.Call = wrapper;
        unit->Statements = s;
        return unit;
    }
    unit->Statements = parse_block(&p, stop_never);
    if (cur(&p)->Kind != T_EOF)
        fail(&p, E_VBS_SYNTAX, cur(&p));
    return unit;
}
