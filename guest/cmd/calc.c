/* set /a: the expression evaluator, 32 bit signed integers. */
#include "cmd.h"

#define ERR_UNBALANCED 0x400023CCu
#define ERR_OPERAND 0x400023CDu
#define ERR_OPERATOR 0x400023CEu
#define ERR_NUMBER 0x400023CFu
#define ERR_PRECISION 0x400023D0u
#define ERR_DIVIDE 0x400023D1u

typedef struct CALC {
    const WCHAR *p;
    DWORD error;
    LONG value;
} CALC;

static void skip_ws(CALC *c)
{
    while (*c->p && *c->p <= L' ')
        c->p++;
}

static BOOL name_char_stop(WCHAR c)
{
    return c <= L' ' || wcschr(L"<>+-*/%()|^&=,~!", c) != nullptr;
}

/* a variable name at the current position, or none. */
static BOOL read_variable(CALC *c, STR *name)
{
    skip_ws(c);
    WCHAR first = *c->p;
    if (!first || is_digit(first) || name_char_stop(first))
        return FALSE;
    while (*c->p && !name_char_stop(*c->p))
        str_addc(name, *c->p++);
    return TRUE;
}

static LONG variable_value(const WCHAR *name)
{
    STR v;
    str_init(&v);
    LONG result = 0;
    if (env_get(name, &v))
        result = parse_long(v.p, nullptr);
    str_free(&v);
    return result;
}

static void set_variable(const WCHAR *name, LONG value)
{
    WCHAR text[16];
    _snwprintf(text, NUM(text), L"%ld", (long)value);
    env_set(name, text);
}

static LONG parse_or(CALC *c);

static LONG parse_unary(CALC *c)
{
    skip_ws(c);
    WCHAR ch = *c->p;
    if (!ch) {
        c->error = ERR_OPERAND;
        return 0;
    }
    if (ch == L'(') {
        c->p++;
        LONG v = parse_or(c);
        /* the comma operator works inside parentheses too. */
        skip_ws(c);
        while (!c->error && *c->p == L',') {
            c->p++;
            v = parse_or(c);
            skip_ws(c);
        }
        if (c->error)
            return 0;
        if (*c->p == L')')
            c->p++;
        else
            c->error = ERR_UNBALANCED;
        return v;
    }
    if (ch == L'+' || ch == L'-' || ch == L'~' || ch == L'!') {
        c->p++;
        LONG v = parse_unary(c);
        if (c->error)
            return 0;
        switch (ch) {
        case L'-': return (LONG)(0u - (ULONG)v);
        case L'~': return ~v;
        case L'!': return v == 0;
        default: return v;
        }
    }
    if (is_digit(ch)) {
        const WCHAR *start = c->p;
        const WCHAR *end;
        LONG v;
        BOOL overflow = FALSE;
        if (start[0] == L'0' && (start[1] == L'x' || start[1] == L'X')) {
            ULONGLONG acc = 0;
            end = start + 2;
            while ((*end >= L'0' && *end <= L'9') || ((*end | 0x20) >= L'a' && (*end | 0x20) <= L'f')) {
                int d = *end <= L'9' ? *end - L'0' : (*end | 0x20) - L'a' + 10;
                acc = acc * 16 + (ULONGLONG)d;
                if (acc > 0xFFFFFFFFull)
                    overflow = TRUE, acc &= 0xFFFFFFFFull;
                end++;
            }
            v = (LONG)(ULONG)acc;
        } else {
            int base = start[0] == L'0' ? 8 : 10;
            ULONGLONG acc = 0;
            end = start;
            while (*end >= L'0' && *end <= L'0' + (base == 8 ? 7 : 9)) {
                acc = acc * (ULONGLONG)base + (ULONGLONG)(*end - L'0');
                if (acc > 0x7FFFFFFFull)
                    overflow = TRUE, acc = 0x7FFFFFFFull;
                end++;
            }
            v = (LONG)acc;
        }
        c->p = end;
        if (overflow) {
            c->error = ERR_PRECISION;
            return 0;
        }
        if (is_digit(*c->p) || ((*c->p | 0x20) >= L'a' && (*c->p | 0x20) <= L'z')) {
            c->error = ERR_NUMBER;
            return 0;
        }
        return v;
    }
    STR name;
    str_init(&name);
    if (read_variable(c, &name)) {
        LONG v = variable_value(str_get(&name));
        str_free(&name);
        return v;
    }
    str_free(&name);
    c->error = ERR_OPERAND;
    return 0;
}

static LONG apply(CALC *c, WCHAR op, LONG a, LONG b, BOOL shift_right)
{
    switch (op) {
    case L'*': return (LONG)((ULONG)a * (ULONG)b);
    case L'/':
        if (!b) {
            c->error = ERR_DIVIDE;
            return 0;
        }
        if (a == (LONG)0x80000000 && b == -1) {
            c->error = ERR_PRECISION;
            return 0;
        }
        return a / b;
    case L'%':
        if (!b) {
            c->error = ERR_DIVIDE;
            return 0;
        }
        if (a == (LONG)0x80000000 && b == -1) {
            c->error = ERR_PRECISION;
            return 0;
        }
        return a % b;
    case L'+': return (LONG)((ULONG)a + (ULONG)b);
    case L'-': return (LONG)((ULONG)a - (ULONG)b);
    case L'<': return (ULONG)b < 32 ? (LONG)((ULONG)a << b) : 0;
    case L'>': return (ULONG)b < 32 ? a >> b : (a < 0 ? -1 : 0);
    case L'&': return a & b;
    case L'^': return a ^ b;
    case L'|': return a | b;
    }
    (void)shift_right;
    return b;
}

static LONG parse_mult(CALC *c)
{
    LONG v = parse_unary(c);
    for (;;) {
        if (c->error)
            return 0;
        skip_ws(c);
        WCHAR op = *c->p;
        if ((op != L'*' && op != L'/' && op != L'%') || c->p[1] == L'=')
            return v;
        c->p++;
        LONG r = parse_unary(c);
        if (c->error)
            return 0;
        v = apply(c, op, v, r, FALSE);
    }
}

static LONG parse_add(CALC *c)
{
    LONG v = parse_mult(c);
    for (;;) {
        if (c->error)
            return 0;
        skip_ws(c);
        WCHAR op = *c->p;
        if ((op != L'+' && op != L'-') || c->p[1] == L'=')
            return v;
        c->p++;
        LONG r = parse_mult(c);
        if (c->error)
            return 0;
        v = apply(c, op, v, r, FALSE);
    }
}

static LONG parse_shift(CALC *c)
{
    LONG v = parse_add(c);
    for (;;) {
        if (c->error)
            return 0;
        skip_ws(c);
        WCHAR op = *c->p;
        if (!((op == L'<' && c->p[1] == L'<') || (op == L'>' && c->p[1] == L'>')) || c->p[2] == L'=')
            return v;
        c->p += 2;
        LONG r = parse_add(c);
        if (c->error)
            return 0;
        v = apply(c, op, v, r, op == L'>');
    }
}

static LONG parse_and(CALC *c)
{
    LONG v = parse_shift(c);
    for (;;) {
        if (c->error)
            return 0;
        skip_ws(c);
        if (c->p[0] != L'&' || c->p[1] == L'=')
            return v;
        c->p++;
        LONG r = parse_shift(c);
        if (c->error)
            return 0;
        v &= r;
    }
}

static LONG parse_xor(CALC *c)
{
    LONG v = parse_and(c);
    for (;;) {
        if (c->error)
            return 0;
        skip_ws(c);
        if (c->p[0] != L'^' || c->p[1] == L'=')
            return v;
        c->p++;
        LONG r = parse_and(c);
        if (c->error)
            return 0;
        v ^= r;
    }
}

static LONG parse_or(CALC *c)
{
    LONG v = parse_xor(c);
    for (;;) {
        if (c->error)
            return 0;
        skip_ws(c);
        if (c->p[0] != L'|' || c->p[1] == L'=')
            return v;
        c->p++;
        LONG r = parse_xor(c);
        if (c->error)
            return 0;
        v |= r;
    }
}

/* an assignment, a name followed by = or an operator and =, else an ordinary expression. */
static LONG parse_assign(CALC *c)
{
    const WCHAR *save = c->p;
    STR name;
    str_init(&name);
    if (read_variable(c, &name)) {
        skip_ws(c);
        WCHAR op = 0;
        const WCHAR *q = c->p;
        int len = 0;
        if (q[0] == L'=' && q[1] != L'=') {
            op = L'=';
            len = 1;
        } else if (q[1] == L'=' && wcschr(L"*/%+-&^|", q[0])) {
            op = q[0];
            len = 2;
        } else if ((q[0] == L'<' || q[0] == L'>') && q[1] == q[0] && q[2] == L'=') {
            op = q[0];
            len = 3;
        }
        if (op) {
            c->p += len;
            LONG r = parse_assign(c);
            if (c->error) {
                str_free(&name);
                return 0;
            }
            LONG result = r;
            if (op != L'=') {
                LONG old = variable_value(str_get(&name));
                result = apply(c, op, old, r, op == L'>');
                if (c->error) {
                    str_free(&name);
                    return 0;
                }
            }
            set_variable(str_get(&name), result);
            str_free(&name);
            return result;
        }
    }
    str_free(&name);
    c->p = save;
    return parse_or(c);
}

int set_arithmetic(const WCHAR *expr, BOOL show)
{
    STR clean;
    str_init(&clean);
    str_adds(&clean, expr);
    strip_quotes_inplace(clean.p ? clean.p : (WCHAR *)L"");
    clean.len = wcslen(str_get(&clean));
    if (!*skip_space(clean.p)) {
        str_free(&clean);
        msg_syntax();
        return 1;
    }
    CALC c = {clean.p, 0, 0};
    LONG v = parse_assign(&c);
    skip_ws(&c);
    while (!c.error && *c.p == L',') {
        c.p++;
        v = parse_assign(&c);
        skip_ws(&c);
    }
    if (!c.error && *c.p)
        c.error = ERR_OPERATOR;
    str_free(&clean);
    if (c.error) {
        switch (c.error) {
        case ERR_UNBALANCED: err_s(L"Unbalanced parenthesis.\r\n"); break;
        case ERR_OPERAND: err_s(L"Missing operand.\r\n"); break;
        case ERR_OPERATOR: err_s(L"Missing operator.\r\n"); break;
        case ERR_NUMBER:
            err_s(L"Invalid number.  Numeric constants are either decimal (17),\r\nhexadecimal (0x11), or octal (021).\r\n");
            break;
        case ERR_PRECISION: err_s(L"Invalid number.  Numbers are limited to 32-bits of precision.\r\n"); break;
        case ERR_DIVIDE: err_s(L"Divide by zero error.\r\n"); break;
        }
        set_errorlevel(c.error);
        return (int)c.error;
    }
    if (show)
        out_fmt(L"%ld", (long)v);
    return 0;
}
