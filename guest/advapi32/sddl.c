/*
 * the security descriptor definition language: security descriptors, lists, aces, sids and conditions as text.
 *
 * the text is parsed and written the way advapi32 does on windows: the same tokens in the same tables, the same
 * order of the fields, the same errors. conditional aces keep their condition in the binary form of the
 * "artx" token stream, compiled by the parser here and written back by the printer.
 *
 * resource attribute aces hold one attribute in its relative form. the trailing data of audit and label aces
 * of kinds the tables do not know is not supported.
 */
#include "advapi32_private.h"

#define ERROR_INVALID_DATATYPE_ 1804L
#define ERROR_INVALID_FLAGS_ 1004L
#define ERROR_INVALID_UUID_ 1705L

/* ---------------------------------------------------------------- text helpers */

typedef struct SB {
    PWSTR Text;
    DWORD Length;
    DWORD Capacity;
    BOOL Failed;
} SB;

static void sb_reserve(SB *sb, DWORD more)
{
    if (sb->Failed || sb->Length + more + 1 <= sb->Capacity)
        return;
    DWORD capacity = sb->Capacity ? sb->Capacity : 64;
    while (capacity < sb->Length + more + 1)
        capacity *= 2;
    PWSTR text = AdvAlloc(capacity * sizeof(WCHAR));
    if (!text) {
        sb->Failed = TRUE;
        return;
    }
    if (sb->Text)
        memcpy(text, sb->Text, (sb->Length + 1) * sizeof(WCHAR));
    AdvFree(sb->Text);
    sb->Text = text;
    sb->Capacity = capacity;
}

static void sb_chars(SB *sb, PCWSTR text, DWORD count)
{
    sb_reserve(sb, count);
    if (sb->Failed)
        return;
    memcpy(sb->Text + sb->Length, text, count * sizeof(WCHAR));
    sb->Length += count;
    sb->Text[sb->Length] = 0;
}

static void sb_text(SB *sb, PCWSTR text)
{
    sb_chars(sb, text, (DWORD)wcslen(text));
}

static void sb_char(SB *sb, WCHAR c)
{
    sb_chars(sb, &c, 1);
}

static void sb_hex(SB *sb, ULONGLONG value, DWORD min_digits, BOOL upper)
{
    WCHAR digits[16];
    DWORD n = 0;
    do {
        DWORD d = (DWORD)(value & 15);
        digits[n++] = (WCHAR)(d < 10 ? L'0' + d : (upper ? L'A' : L'a') + d - 10);
        value >>= 4;
    } while (value);
    while (n < min_digits && n < 16)
        digits[n++] = L'0';
    while (n)
        sb_char(sb, digits[--n]);
}

static void sb_decimal(SB *sb, ULONGLONG value)
{
    WCHAR digits[24];
    DWORD n = 0;
    do {
        digits[n++] = (WCHAR)(L'0' + value % 10);
        value /= 10;
    } while (value);
    while (n)
        sb_char(sb, digits[--n]);
}

static BOOL is_space(WCHAR c)
{
    return c == L' ' || (c >= 9 && c <= 13);
}

static BOOL is_alnum(WCHAR c)
{
    return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
}

static int hex_value(WCHAR c)
{
    if (c >= L'0' && c <= L'9')
        return c - L'0';
    if (c >= L'a' && c <= L'f')
        return c - L'a' + 10;
    if (c >= L'A' && c <= L'F')
        return c - L'A' + 10;
    return -1;
}

/* whether the text starts with the token, ignoring case. */
static BOOL starts_with(PCWSTR text, PCWSTR token, DWORD length)
{
    return _wcsnicmp(text, token, length) == 0;
}

/*
 * a number as c writes it: 0x for hex, a leading 0 for octal, else decimal. returns the end or NULL when no digit
 * is there; a value above the limit is clamped.
 */
static PCWSTR scan_number(PCWSTR s, ULONGLONG limit, ULONGLONG *value, BOOL *overflow, BOOL octal)
{
    ULONGLONG v = 0;
    DWORD base = 10, digits = 0;
    PCWSTR start = s;
    *overflow = FALSE;
    if (s[0] == L'0' && (s[1] == L'x' || s[1] == L'X') && hex_value(s[2]) >= 0) {
        base = 16;
        s += 2;
    } else if (octal && s[0] == L'0') {
        base = 8;
    }
    for (;; s++, digits++) {
        int d = hex_value(*s);
        if (d < 0 || (DWORD)d >= base)
            break;
        if (v > (limit - (ULONGLONG)d) / base) {
            *overflow = TRUE;
            v = limit;
        } else if (!*overflow) {
            v = v * base + d;
        }
    }
    *value = v;
    return s != start || digits ? s : nullptr;
}

/* ---------------------------------------------------------------- sid aliases */

/*
 * the sids the language names with two letters. a kind says what the sid is made of: a relative id in the
 * domain of the caller, in the root domain or in this machine's domain, or after a fixed prefix.
 */
enum {
    KIND_DOMAIN = 0,
    KIND_WORLD = 1,
    KIND_LOCAL = 2,
    KIND_MACHINE = 3,
    KIND_CREATOR = 4,
    KIND_NT = 5,
    KIND_BUILTIN = 6,
    KIND_ROOT = 7,
    KIND_LABEL = 8,
    KIND_PACKAGE = 9,
    KIND_DRIVERS = 10,
    KIND_AUTHENTICATION = 11,
};

typedef struct SDDL_ALIAS {
    WCHAR Name[3];
    BYTE Kind;
    DWORD Rid;
} SDDL_ALIAS;

static const SDDL_ALIAS g_aliases[] = {
    {L"DA", KIND_DOMAIN, 512},      {L"DG", KIND_DOMAIN, 514},   {L"DU", KIND_DOMAIN, 513},
    {L"DD", KIND_DOMAIN, 516},      {L"DC", KIND_DOMAIN, 515},   {L"SA", KIND_ROOT, 518},
    {L"EA", KIND_ROOT, 519},        {L"CA", KIND_DOMAIN, 517},   {L"AO", KIND_BUILTIN, 548},
    {L"BO", KIND_BUILTIN, 551},     {L"PO", KIND_BUILTIN, 550},  {L"SO", KIND_BUILTIN, 549},
    {L"RE", KIND_BUILTIN, 552},     {L"RS", KIND_DOMAIN, 553},   {L"AU", KIND_NT, 11},
    {L"PS", KIND_NT, 10},           {L"CO", KIND_CREATOR, 0},    {L"OW", KIND_CREATOR, 4},
    {L"CG", KIND_CREATOR, 1},       {L"SY", KIND_NT, 18},        {L"IU", KIND_NT, 4},
    {L"NU", KIND_NT, 2},            {L"SU", KIND_NT, 6},         {L"ED", KIND_NT, 9},
    {L"RC", KIND_NT, 12},           {L"WR", KIND_NT, 33},        {L"AN", KIND_NT, 7},
    {L"LA", KIND_MACHINE, 500},     {L"LG", KIND_MACHINE, 501},  {L"BA", KIND_BUILTIN, 544},
    {L"CY", KIND_BUILTIN, 569},     {L"BG", KIND_BUILTIN, 546},  {L"BU", KIND_BUILTIN, 545},
    {L"PU", KIND_BUILTIN, 547},     {L"WD", KIND_WORLD, 0},      {L"PA", KIND_DOMAIN, 520},
    {L"RU", KIND_BUILTIN, 554},     {L"LS", KIND_NT, 19},        {L"NS", KIND_NT, 20},
    {L"RD", KIND_BUILTIN, 555},     {L"NO", KIND_BUILTIN, 556},  {L"MU", KIND_BUILTIN, 558},
    {L"LU", KIND_BUILTIN, 559},     {L"IS", KIND_BUILTIN, 568},  {L"LW", KIND_LABEL, 4096},
    {L"ME", KIND_LABEL, 8192},      {L"MP", KIND_LABEL, 8448},   {L"HI", KIND_LABEL, 12288},
    {L"SI", KIND_LABEL, 16384},     {L"CY", KIND_BUILTIN, 569},  {L"ER", KIND_BUILTIN, 573},
    {L"RO", KIND_ROOT, 498},        {L"CD", KIND_BUILTIN, 574},  {L"AC", KIND_PACKAGE, 1},
    {L"RA", KIND_BUILTIN, 575},     {L"ES", KIND_BUILTIN, 576},  {L"MS", KIND_BUILTIN, 577},
    {L"UD", KIND_DRIVERS, 84},      {L"HA", KIND_BUILTIN, 578},  {L"AA", KIND_BUILTIN, 579},
    {L"RM", KIND_BUILTIN, 580},     {L"AS", KIND_AUTHENTICATION, 1}, {L"SS", KIND_AUTHENTICATION, 2},
    {L"KA", KIND_DOMAIN, 526},      {L"EK", KIND_ROOT, 527},     {L"HO", KIND_BUILTIN, 584},
    {L"SH", KIND_BUILTIN, 585},
};

static void make_sid(SID *sid, BYTE authority, BOOL prefixed, DWORD prefix, DWORD rid)
{
    sid->Revision = SID_REVISION;
    memset(sid->IdentifierAuthority.Value, 0, 6);
    sid->IdentifierAuthority.Value[5] = authority;
    sid->SubAuthorityCount = prefixed ? 2 : 1;
    if (prefixed)
        sid->SubAuthority[0] = prefix;
    sid->SubAuthority[prefixed ? 1 : 0] = rid;
}

static BOOL domain_relative(const SID *domain, DWORD rid, SID *out)
{
    if (!domain || domain->SubAuthorityCount >= SID_MAX_SUB_AUTHORITIES)
        return FALSE;
    memcpy(out, domain, AdvSidLength(domain));
    out->SubAuthority[out->SubAuthorityCount++] = rid;
    return TRUE;
}

/* the sid an alias stands for in a context, or FALSE when the domain it needs is not known. */
static BOOL alias_sid(const SDDL_ALIAS *a, const SDDL_CONTEXT *context, SID *out)
{
    switch (a->Kind) {
    case KIND_DOMAIN:
        return context && domain_relative(context->Domain, a->Rid, out);
    case KIND_ROOT:
        return context && domain_relative(context->Root, a->Rid, out);
    case KIND_MACHINE: {
        BYTE machine[SECURITY_MAX_SID_SIZE];
        return AdvMachineSid(machine) && domain_relative((SID *)machine, a->Rid, out);
    }
    case KIND_WORLD:
        make_sid(out, 1, FALSE, 0, a->Rid);
        return TRUE;
    case KIND_LOCAL:
        make_sid(out, 2, FALSE, 0, a->Rid);
        return TRUE;
    case KIND_CREATOR:
        make_sid(out, 3, FALSE, 0, a->Rid);
        return TRUE;
    case KIND_NT:
        make_sid(out, 5, FALSE, 0, a->Rid);
        return TRUE;
    case KIND_BUILTIN:
        make_sid(out, 5, TRUE, 32, a->Rid);
        return TRUE;
    case KIND_LABEL:
        make_sid(out, 16, FALSE, 0, a->Rid);
        return TRUE;
    case KIND_PACKAGE:
        make_sid(out, 15, TRUE, 2, a->Rid);
        return TRUE;
    case KIND_AUTHENTICATION:
        make_sid(out, 18, FALSE, 0, a->Rid);
        return TRUE;
    case KIND_DRIVERS:
        AdvMakeSid(out, 5, 6, 84, 0, 0, 0, 0, 0);
        return TRUE;
    }
    return FALSE;
}

/* the alias that starts the text, if its sid is known. */
static const SDDL_ALIAS *find_alias(PCWSTR text, const SDDL_CONTEXT *context, SID *sid)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_aliases); i++)
        if (starts_with(text, g_aliases[i].Name, 2))
            return alias_sid(&g_aliases[i], context, sid) ? &g_aliases[i] : nullptr;
    return nullptr;
}

/* ---------------------------------------------------------------- sids as text */

/* where a sid in a longer text stops: the end, a space, a parenthesis, a comma, a semicolon or the next part. */
static BOOL sid_stop(PCWSTR s)
{
    WCHAR c = *s;
    if (c == 0 || c == L' ' || c == L')' || c == L',' || c == L';')
        return TRUE;
    return s[1] == L':' && (c == L'D' || c == L'G' || c == L'O' || c == L'S');
}

/* S-R-A-s-s...: the revision, then the 48 bit authority, then at least one sub authority. */
static PCWSTR parse_numeric_sid(PCWSTR s, SID *sid)
{
    if ((s[0] != L'S' && s[0] != L's') || s[1] != L'-')
        return nullptr;
    s += 2;
    ULONGLONG revision, authority, value;
    BOOL overflow;
    PCWSTR end = scan_number(s, 0xFF, &revision, &overflow, FALSE);
    if (!end || overflow || *end != L'-')
        return nullptr;
    s = end + 1;
    end = scan_number(s, 0xFFFFFFFFFFFFull, &authority, &overflow, FALSE);
    if (!end || overflow || *end != L'-' || end[1] == 0)
        return nullptr;
    sid->Revision = (BYTE)revision;
    sid->SubAuthorityCount = 0;
    for (int i = 0; i < 6; i++)
        sid->IdentifierAuthority.Value[i] = (BYTE)(authority >> (40 - 8 * i));
    s = end;
    while (*s == L'-') {
        s++;
        end = scan_number(s, 0xFFFFFFFFull, &value, &overflow, FALSE);
        if (!end || sid->SubAuthorityCount == SID_MAX_SUB_AUTHORITIES)
            return nullptr;
        sid->SubAuthority[sid->SubAuthorityCount++] = (DWORD)value;
        s = end;
    }
    return sid_stop(s) ? s : nullptr;
}

DWORD AdvParseSidString(PCWSTR string, const SDDL_CONTEXT *context, void *storage, PCWSTR *end)
{
    SID *sid = storage;
    PCWSTR stop = parse_numeric_sid(string, sid);
    if (!stop) {
        const SDDL_ALIAS *alias = find_alias(string, context, sid);
        if (!alias)
            return ERROR_INVALID_SID;
        stop = string + 2;
        if (!end && *stop)
            return ERROR_INVALID_SID;
    }
    if (end)
        *end = stop;
    else if (*stop)
        return ERROR_INVALID_SID;
    return ERROR_SUCCESS;
}

/* writes a sid as its alias when a context knows one, else as S-1-.... */
static void sb_sid(SB *sb, const SID *sid, const SDDL_CONTEXT *context)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_aliases); i++) {
        BYTE candidate[SECURITY_MAX_SID_SIZE];
        if (alias_sid(&g_aliases[i], context, (SID *)candidate) && RtlEqualSid((PSID)sid, candidate)) {
            sb_chars(sb, g_aliases[i].Name, 2);
            return;
        }
    }
    UNICODE_STRING text;
    if (NT_SUCCESS(RtlConvertSidToUnicodeString(&text, (PSID)sid, TRUE))) {
        sb_chars(sb, text.Buffer, text.Length / sizeof(WCHAR));
        RtlFreeUnicodeString(&text);
    } else {
        sb->Failed = TRUE;
    }
}

/* ---------------------------------------------------------------- conditions */

#define CT_INT8 0x01
#define CT_INT64 0x04
#define CT_STRING 0x10
#define CT_OCTETS 0x18
#define CT_COMPOSITE 0x50
#define CT_SID 0x51
#define CT_ATTR_LOCAL 0xF8
#define CT_ATTR_USER 0xF9
#define CT_ATTR_RESOURCE 0xFA
#define CT_ATTR_DEVICE 0xFB
#define CT_ATTR_TOKEN 0xFC
#define CT_AND 0xA0
#define CT_OR 0xA1
#define CT_NOT 0xA2
#define CT_BITAND 0xA3
#define CT_EXISTS 0x87
#define CT_NOT_EXISTS 0x8D

#define COND_MAGIC "artx"
#define COND_MAX_DEPTH 64

enum { OP_UNARY_ATTRIBUTE, OP_UNARY_VALUE, OP_BINARY, OP_LOGIC, OP_NOT };

typedef struct COND_OP {
    PCWSTR Name;
    BYTE Token;
    BYTE Kind;
    BOOL Word; /* spelled with letters, so a whole word of the text */
} COND_OP;

static const COND_OP g_operators[] = {
    {L"Exists", 0x87, OP_UNARY_ATTRIBUTE, TRUE},
    {L"Not_Exists", 0x8D, OP_UNARY_ATTRIBUTE, TRUE},
    {L"==", 0x80, OP_BINARY, FALSE},
    {L">=", 0x85, OP_BINARY, FALSE},
    {L"!=", 0x81, OP_BINARY, FALSE},
    {L"<=", 0x83, OP_BINARY, FALSE},
    {L"&&", CT_AND, OP_LOGIC, FALSE},
    {L"||", CT_OR, OP_LOGIC, FALSE},
    {L"&", CT_BITAND, OP_BINARY, FALSE},
    {L"<", 0x82, OP_BINARY, FALSE},
    {L">", 0x84, OP_BINARY, FALSE},
    {L"Contains", 0x86, OP_BINARY, TRUE},
    {L"Not_Contains", 0x8E, OP_BINARY, TRUE},
    {L"Any_of", 0x88, OP_BINARY, TRUE},
    {L"Not_Any_of", 0x8F, OP_BINARY, TRUE},
    {L"!", CT_NOT, OP_NOT, FALSE},
    {L"Member_of", 0x89, OP_UNARY_VALUE, TRUE},
    {L"Not_Member_of", 0x90, OP_UNARY_VALUE, TRUE},
    {L"Device_Member_of", 0x8A, OP_UNARY_VALUE, TRUE},
    {L"Not_Device_Member_of", 0x91, OP_UNARY_VALUE, TRUE},
    {L"Member_of_Any", 0x8B, OP_UNARY_VALUE, TRUE},
    {L"Not_Member_of_Any", 0x92, OP_UNARY_VALUE, TRUE},
    {L"Device_Member_of_Any", 0x8C, OP_UNARY_VALUE, TRUE},
    {L"Not_Device_Member_of_Any", 0x93, OP_UNARY_VALUE, TRUE},
};

static const COND_OP *operator_by_token(BYTE token)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_operators); i++)
        if (g_operators[i].Token == token)
            return &g_operators[i];
    return nullptr;
}

/* where the name of a word operator ends in the text. */
static BOOL word_end(WCHAR c)
{
    return c == 0 || is_space(c) || c == L'(' || c == L')' || c == L'{' || c == L'}';
}

/* the operator of the kind that starts the text, symbols by their characters and words as whole words. */
static const COND_OP *match_operator(PCWSTR s, BOOL (*wanted)(const COND_OP *), DWORD *length)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_operators); i++) {
        const COND_OP *op = &g_operators[i];
        DWORD n = (DWORD)wcslen(op->Name);
        if (!wanted(op) || !starts_with(s, op->Name, n) || (op->Word && !word_end(s[n])))
            continue;
        /* a lone & or ! must not take the start of && or !=. */
        if (op->Token == CT_BITAND && s[1] == L'&')
            continue;
        if (op->Token == CT_NOT && s[1] == L'=')
            continue;
        *length = n;
        return op;
    }
    return nullptr;
}

static BOOL want_binary(const COND_OP *op)
{
    return op->Kind == OP_BINARY;
}

static BOOL want_unary(const COND_OP *op)
{
    return op->Kind == OP_UNARY_ATTRIBUTE || op->Kind == OP_UNARY_VALUE;
}

typedef struct COND {
    PCWSTR Text;
    const SDDL_CONTEXT *Context;
    BYTE *Buffer;
    DWORD Length;
    DWORD Capacity;
    DWORD Depth;
} COND;

static DWORD cond_emit(COND *c, const void *data, DWORD size)
{
    if (c->Length + size < c->Length)
        return ERROR_ARITHMETIC_OVERFLOW;
    if (c->Length + size > c->Capacity) {
        DWORD capacity = c->Capacity ? c->Capacity * 2 : 64;
        while (capacity < c->Length + size)
            capacity *= 2;
        BYTE *grown = AdvAlloc(capacity);
        if (!grown)
            return ERROR_NOT_ENOUGH_MEMORY;
        if (c->Buffer)
            memcpy(grown, c->Buffer, c->Length);
        AdvFree(c->Buffer);
        c->Buffer = grown;
        c->Capacity = capacity;
    }
    memcpy(c->Buffer + c->Length, data, size);
    c->Length += size;
    return ERROR_SUCCESS;
}

static DWORD cond_emit_byte(COND *c, BYTE b)
{
    return cond_emit(c, &b, 1);
}

static DWORD cond_emit_sized(COND *c, BYTE token, const void *data, DWORD size)
{
    DWORD error = cond_emit_byte(c, token);
    if (!error)
        error = cond_emit(c, &size, sizeof(size));
    if (!error && size)
        error = cond_emit(c, data, size);
    return error;
}

static void cond_skip(COND *c)
{
    while (is_space(*c->Text))
        c->Text++;
}

/* the characters an attribute name may be made of: those of the bare name, or those after an at sign. */
static BOOL bare_name_char(WCHAR c)
{
    return c <= 0xFF && (is_alnum(c) || c == L':' || c == L'.' || c == L'/' || c == L'_' || c == L'@');
}

static BOOL at_name_char(WCHAR c)
{
    return c != 0 && !(c < 0x7F && wcschr(L" !\"&)(<>=|", c));
}

/* a character that a name has to write as % and four hex digits. */
static BOOL needs_encoding(WCHAR c)
{
    if (c >= 0x7F)
        return TRUE;
    return !is_alnum(c) && !wcschr(L"#$'*+-./:;?@[\\]^_`{}~", c);
}

static DWORD parse_attribute(COND *c)
{
    PCWSTR s = c->Text;
    BYTE token = CT_ATTR_LOCAL;
    PCWSTR name;
    DWORD run = 0;
    if (*s == L'@') {
        while (at_name_char(s[run]))
            run++;
        static const struct {
            PCWSTR Prefix;
            DWORD Length;
            BYTE Token;
        } prefixes[] = {{L"@USER.", 6, CT_ATTR_USER},
                        {L"@DEVICE.", 8, CT_ATTR_DEVICE},
                        {L"@RESOURCE.", 10, CT_ATTR_RESOURCE},
                        {L"@TOKEN.", 7, CT_ATTR_TOKEN}};
        DWORD skip = 0;
        for (ULONG i = 0; i < RTL_NUMBER_OF(prefixes); i++) {
            if (run > prefixes[i].Length && starts_with(s, prefixes[i].Prefix, prefixes[i].Length)) {
                token = prefixes[i].Token;
                skip = prefixes[i].Length;
                break;
            }
        }
        if (!skip)
            return ERROR_INVALID_ACL;
        name = s + skip;
        run -= skip;
    } else {
        while (bare_name_char(s[run]))
            run++;
        if (!run)
            return ERROR_INVALID_ACL;
        name = s;
    }
    WCHAR decoded[256];
    PWSTR text = decoded;
    DWORD count = 0, error = ERROR_SUCCESS;
    if (run > RTL_NUMBER_OF(decoded))
        text = AdvAlloc(run * sizeof(WCHAR));
    if (!text)
        return ERROR_NOT_ENOUGH_MEMORY;
    for (DWORD i = 0; i < run && !error;) {
        WCHAR ch = name[i];
        if (ch == L'%' && token != CT_ATTR_LOCAL) {
            int v0, v1, v2, v3;
            if (i + 4 >= run || (v0 = hex_value(name[i + 1])) < 0 || (v1 = hex_value(name[i + 2])) < 0 ||
                (v2 = hex_value(name[i + 3])) < 0 || (v3 = hex_value(name[i + 4])) < 0) {
                error = ERROR_INVALID_ACL;
                break;
            }
            ch = (WCHAR)(v0 << 12 | v1 << 8 | v2 << 4 | v3);
            if (!needs_encoding(ch))
                error = ERROR_INVALID_ACL;
            i += 5;
        } else {
            i++;
        }
        text[count++] = ch;
    }
    if (!error)
        error = cond_emit_sized(c, token, text, count * sizeof(WCHAR));
    if (text != decoded)
        AdvFree(text);
    if (!error)
        c->Text = name + run;
    return error;
}

/* the end of a bare literal: a space, a closing bracket, a bar, an and sign, a comma or the end. */
static PCWSTR literal_end(PCWSTR s)
{
    while (*s && *s != L')' && *s != L'}' && *s != L'|' && *s != L'&' && *s != L',' && *s != L' ')
        s++;
    return s;
}

static DWORD parse_operand(COND *c, BOOL attribute, BOOL nested);

static DWORD parse_composite(COND *c)
{
    DWORD error = cond_emit_byte(c, CT_COMPOSITE), mark = c->Length, zero = 0;
    if (!error)
        error = cond_emit(c, &zero, sizeof(zero));
    c->Text++;
    for (;;) {
        cond_skip(c);
        if (error || *c->Text == 0 || *c->Text == L'{')
            return error ? error : ERROR_INVALID_ACL;
        error = parse_operand(c, FALSE, TRUE);
        if (error)
            return error;
        cond_skip(c);
        if (*c->Text == L'}') {
            c->Text++;
            DWORD size = c->Length - mark - sizeof(DWORD);
            if (!size)
                return ERROR_INVALID_ACL;
            memcpy(c->Buffer + mark, &size, sizeof(size));
            return ERROR_SUCCESS;
        }
        if (*c->Text != L',')
            return ERROR_INVALID_ACL;
        c->Text++;
    }
}

static DWORD parse_operand(COND *c, BOOL attribute, BOOL nested)
{
    cond_skip(c);
    PCWSTR s = c->Text;
    if (attribute || *s == L'@')
        return parse_attribute(c);
    if (*s == L'"') {
        PCWSTR close = wcschr(s + 1, L'"');
        if (!close)
            return ERROR_INVALID_ACL;
        DWORD error = cond_emit_sized(c, CT_STRING, s + 1, (DWORD)(close - s - 1) * sizeof(WCHAR));
        c->Text = close + 1;
        return error;
    }
    if (*s == L'{')
        return nested ? ERROR_INVALID_ACL : parse_composite(c);
    if (starts_with(s, L"SID", 3)) {
        s += 3;
        while (is_space(*s))
            s++;
        if (*s != L'(')
            return ERROR_INVALID_ACL;
        BYTE sid[SECURITY_MAX_SID_SIZE];
        PCWSTR end;
        DWORD error = AdvParseSidString(s + 1, c->Context, sid, &end);
        if (error)
            return error;
        while (is_space(*end))
            end++;
        if (*end != L')')
            return ERROR_INVALID_ACL;
        c->Text = end + 1;
        return cond_emit_sized(c, CT_SID, sid, AdvSidLength((SID *)sid));
    }
    PCWSTR end = literal_end(s);
    if (end == s)
        return ERROR_INVALID_ACL;
    if (*s == L'#') {
        DWORD digits = (DWORD)(end - s - 1), bytes = (digits + 1) / 2;
        if (!digits || (digits & 1))
            return ERROR_INVALID_ACL;
        DWORD error = cond_emit_byte(c, CT_OCTETS);
        if (!error)
            error = cond_emit(c, &bytes, sizeof(bytes));
        for (DWORD i = 0; !error && i < bytes; i++) {
            int hi = hex_value(s[1 + 2 * i]), lo = hex_value(s[2 + 2 * i]);
            if (hi < 0 || lo < 0)
                return ERROR_INVALID_ACL;
            error = cond_emit_byte(c, (BYTE)(hi << 4 | lo));
        }
        c->Text = end;
        return error;
    }
    /* a number: optional sign, then hex, octal or decimal; the whole word must be the number. */
    PCWSTR digits = s;
    BYTE sign = 3, base = 2;
    if (*digits == L'-' || *digits == L'+') {
        sign = *digits == L'-' ? 2 : 1;
        digits++;
    }
    if (digits[0] == L'0')
        base = digits[1] == L'x' || digits[1] == L'X' ? 3 : 1;
    ULONGLONG magnitude;
    BOOL overflow;
    PCWSTR stop = scan_number(digits, 0xFFFFFFFFFFFFFFFFull, &magnitude, &overflow, TRUE);
    if (!stop || stop != end || overflow || (sign == 2 && magnitude > 0x7FFFFFFFFFFFFFFFull))
        return ERROR_INVALID_ACL;
    ULONGLONG value = sign == 2 ? 0 - magnitude : magnitude;
    BYTE data[10];
    memcpy(data, &value, 8);
    data[8] = sign;
    data[9] = base;
    DWORD error = cond_emit_byte(c, CT_INT64);
    if (!error)
        error = cond_emit(c, data, sizeof(data));
    c->Text = end;
    return error;
}

static DWORD parse_or(COND *c);

static DWORD parse_primary(COND *c)
{
    cond_skip(c);
    if (++c->Depth > COND_MAX_DEPTH)
        return ERROR_INVALID_ACL;
    DWORD error;
    if (*c->Text == L'(') {
        c->Text++;
        error = parse_or(c);
        cond_skip(c);
        if (!error && *c->Text != L')')
            error = ERROR_INVALID_ACL;
        if (!error)
            c->Text++;
        c->Depth--;
        return error;
    }
    DWORD length;
    const COND_OP *op = match_operator(c->Text, want_unary, &length);
    if (op) {
        c->Text += length;
        DWORD mark = c->Length;
        error = parse_operand(c, op->Kind == OP_UNARY_ATTRIBUTE, FALSE);
        /* the existence of the token, user and device claims is not asked of a condition. */
        if (!error && op->Kind == OP_UNARY_ATTRIBUTE &&
            (c->Buffer[mark] == CT_ATTR_USER || c->Buffer[mark] == CT_ATTR_DEVICE || c->Buffer[mark] == CT_ATTR_TOKEN))
            error = ERROR_INVALID_ACL;
        if (!error)
            error = cond_emit_byte(c, op->Token);
        c->Depth--;
        return error;
    }
    error = parse_operand(c, TRUE, FALSE);
    if (error) {
        c->Depth--;
        return error;
    }
    cond_skip(c);
    op = match_operator(c->Text, want_binary, &length);
    if (!op) {
        c->Depth--;
        return ERROR_INVALID_ACL;
    }
    c->Text += length;
    error = parse_operand(c, FALSE, FALSE);
    if (!error)
        error = cond_emit_byte(c, op->Token);
    c->Depth--;
    return error;
}

static DWORD parse_unary(COND *c)
{
    cond_skip(c);
    if (*c->Text == L'!' && c->Text[1] != L'=') {
        if (++c->Depth > COND_MAX_DEPTH)
            return ERROR_INVALID_ACL;
        c->Text++;
        DWORD error = parse_unary(c);
        c->Depth--;
        return error ? error : cond_emit_byte(c, CT_NOT);
    }
    return parse_primary(c);
}

static DWORD parse_and(COND *c)
{
    DWORD error = parse_unary(c);
    for (;;) {
        cond_skip(c);
        if (error || c->Text[0] != L'&' || c->Text[1] != L'&')
            return error;
        c->Text += 2;
        if (!(error = parse_unary(c)))
            error = cond_emit_byte(c, CT_AND);
    }
}

static DWORD parse_or(COND *c)
{
    DWORD error = parse_and(c);
    for (;;) {
        cond_skip(c);
        if (error || c->Text[0] != L'|' || c->Text[1] != L'|')
            return error;
        c->Text += 2;
        if (!(error = parse_and(c)))
            error = cond_emit_byte(c, CT_OR);
    }
}

/*
 * compiles "(expression)" to the token stream. with end the text may go on after the closing parenthesis and the
 * parse stops there, else nothing may follow. the stream is LocalAlloc'd and has no padding.
 */
static DWORD condition_compile(PCWSTR text, const SDDL_CONTEXT *context, PVOID *binary, DWORD *size, PCWSTR *end)
{
    if (wcslen(text) < 3 || text[0] != L'(')
        return ERROR_INVALID_ACL;
    COND c = {text, context, nullptr, 0, 0, 0};
    DWORD error = cond_emit(&c, COND_MAGIC, 4);
    if (!error)
        error = parse_primary(&c);
    if (!error && !end && *c.Text)
        error = ERROR_INVALID_ACL;
    if (!error && c.Length == 4)
        error = ERROR_INVALID_ACL;
    if (!error) {
        PVOID out = LocalAlloc(LMEM_FIXED, c.Length);
        if (!out) {
            error = ERROR_NOT_ENOUGH_MEMORY;
        } else {
            memcpy(out, c.Buffer, c.Length);
            *binary = out;
            *size = c.Length;
            if (end)
                *end = c.Text;
        }
    }
    AdvFree(c.Buffer);
    return error;
}

DWORD AdvConditionFromString(PCWSTR text, PVOID *binary, DWORD *size)
{
    return condition_compile(text, nullptr, binary, size, nullptr);
}

/* ---------------------------------------------------------------- conditions as text */

#define COND_MAX_STACK 255

static void free_stack(PWSTR *stack, DWORD count)
{
    for (DWORD i = 0; i < count; i++)
        AdvFree(stack[i]);
}

static PWSTR sb_take(SB *sb)
{
    if (sb->Failed) {
        AdvFree(sb->Text);
        return nullptr;
    }
    return sb->Text;
}

/* wraps an operand text in parentheses unless it already starts with one. */
static DWORD enclose(PWSTR *text)
{
    if ((*text)[0] == L'(')
        return ERROR_SUCCESS;
    SB sb = {};
    sb_char(&sb, L'(');
    sb_text(&sb, *text);
    sb_char(&sb, L')');
    PWSTR out = sb_take(&sb);
    if (!out)
        return ERROR_NOT_ENOUGH_MEMORY;
    AdvFree(*text);
    *text = out;
    return ERROR_SUCCESS;
}

/* an operand at the start of the bytes into text; used is how many bytes it took. */
static DWORD print_operand(const BYTE *p, DWORD left, const SDDL_CONTEXT *context, PWSTR *text, DWORD *used, BOOL nested)
{
    SB sb = {};
    BYTE token = p[0];
    if (token >= CT_INT8 && token <= CT_INT64) {
        if (left < 11)
            return ERROR_INVALID_ACL;
        ULONGLONG value;
        memcpy(&value, p + 1, 8);
        BYTE sign = p[9], base = p[10];
        if (sign == 1)
            sb_char(&sb, L'+');
        else if (sign == 2)
            sb_char(&sb, L'-');
        if (sign == 2)
            value = 0 - value;
        if (base == 1) {
            sb_char(&sb, L'0');
            if (value) {
                WCHAR digits[24];
                DWORD n = 0;
                for (ULONGLONG v = value; v; v >>= 3)
                    digits[n++] = (WCHAR)(L'0' + (v & 7));
                while (n)
                    sb_char(&sb, digits[--n]);
            }
        } else if (base == 3) {
            sb_text(&sb, L"0x");
            sb_hex(&sb, value, 1, FALSE);
        } else {
            sb_decimal(&sb, value);
        }
        *used = 11;
    } else if (token == CT_STRING || token == CT_OCTETS || token == CT_COMPOSITE || token == CT_SID) {
        DWORD size;
        if (left < 5)
            return ERROR_INVALID_ACL;
        memcpy(&size, p + 1, 4);
        if (left - 5 < size)
            return ERROR_INVALID_ACL;
        *used = 5 + size;
        if (token == CT_STRING) {
            if (size & 1)
                return ERROR_INVALID_ACL;
            sb_char(&sb, L'"');
            sb_chars(&sb, (const WCHAR *)(p + 5), size / 2);
            sb_char(&sb, L'"');
        } else if (token == CT_OCTETS) {
            if (!size)
                return ERROR_INVALID_ACL;
            sb_char(&sb, L'#');
            for (DWORD i = 0; i < size; i++)
                sb_hex(&sb, p[5 + i], 2, FALSE);
        } else if (token == CT_SID) {
            if (size > 0x44 || size < 8 || !RtlValidSid((PSID)(p + 5)) || AdvSidLength((SID *)(p + 5)) > size)
                return ERROR_INVALID_SID;
            BYTE sid[0x48];
            memcpy(sid, p + 5, size);
            sb_text(&sb, L"SID(");
            sb_sid(&sb, (SID *)sid, context);
            sb_char(&sb, L')');
        } else {
            if (nested || !size)
                return ERROR_INVALID_ACL;
            sb_char(&sb, L'{');
            for (DWORD at = 0; at < size;) {
                PWSTR element;
                DWORD n;
                if (p[5 + at] == CT_COMPOSITE)
                    return ERROR_INVALID_ACL;
                DWORD error = print_operand(p + 5 + at, size - at, context, &element, &n, TRUE);
                if (error) {
                    AdvFree(sb.Text);
                    return error;
                }
                if (at)
                    sb_text(&sb, L", ");
                sb_text(&sb, element);
                AdvFree(element);
                at += n;
            }
            sb_char(&sb, L'}');
        }
    } else {
        return ERROR_INVALID_ACL;
    }
    *text = sb_take(&sb);
    return *text ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
}

static DWORD print_attribute(const BYTE *p, DWORD left, PWSTR *text, DWORD *used)
{
    BYTE token = p[0];
    DWORD size;
    if (left < 5)
        return ERROR_INVALID_ACL;
    memcpy(&size, p + 1, 4);
    if (left - 5 < size)
        return ERROR_INVALID_ACL;
    *used = 5 + size;
    SB sb = {};
    PCWSTR prefix = token == CT_ATTR_DEVICE ? L"@DEVICE." : token == CT_ATTR_USER ? L"@USER."
                    : token == CT_ATTR_RESOURCE ? L"@RESOURCE." : token == CT_ATTR_TOKEN ? L"@TOKEN." : nullptr;
    if (prefix) {
        if (!size)
            return ERROR_INVALID_PARAMETER;
        if (size & 1)
            return ERROR_INVALID_ACL;
        sb_text(&sb, prefix);
        const WCHAR *name = (const WCHAR *)(p + 5);
        for (DWORD i = 0; i < size / 2; i++) {
            if (needs_encoding(name[i])) {
                sb_char(&sb, L'%');
                sb_hex(&sb, name[i], 4, FALSE);
            } else {
                sb_char(&sb, name[i]);
            }
        }
    } else {
        sb_chars(&sb, (const WCHAR *)(p + 5), size / 2);
    }
    *text = sb_take(&sb);
    return *text ? ERROR_SUCCESS : ERROR_NOT_ENOUGH_MEMORY;
}

static DWORD condition_text(const BYTE *binary, DWORD size, const SDDL_CONTEXT *context, PWSTR *result)
{
    PWSTR stack[COND_MAX_STACK] = {};
    DWORD count = 0, at = 4, error = ERROR_SUCCESS;
    if (!binary || !size)
        return ERROR_INVALID_PARAMETER;
    if (size < 6 || memcmp(binary, COND_MAGIC, 4))
        return 805;
    while (!error && at < size) {
        BYTE token = binary[at];
        if (count == COND_MAX_STACK) {
            error = 1001;
            break;
        }
        if (token == 0) {
            while (at < size && binary[at] == 0)
                at++;
            continue;
        }
        DWORD used = 1;
        if (token >= CT_ATTR_LOCAL && token <= CT_ATTR_TOKEN) {
            error = print_attribute(binary + at, size - at, &stack[count], &used);
            if (!error)
                count++;
        } else if (operator_by_token(token)) {
            const COND_OP *op = operator_by_token(token);
            if (op->Kind == OP_UNARY_ATTRIBUTE || op->Kind == OP_UNARY_VALUE || op->Kind == OP_NOT) {
                if (count < 1) {
                    error = ERROR_INVALID_ACL;
                    break;
                }
                PWSTR *operand = &stack[count - 1];
                if (op->Kind == OP_NOT)
                    error = enclose(operand);
                if (!error && op->Kind == OP_UNARY_ATTRIBUTE &&
                    (starts_with(*operand, L"@USER.", 6) || starts_with(*operand, L"@DEVICE.", 8) ||
                     starts_with(*operand, L"@TOKEN.", 7)))
                    error = ERROR_INVALID_ACL;
                if (!error) {
                    SB sb = {};
                    sb_char(&sb, L'(');
                    sb_text(&sb, op->Name);
                    if (op->Kind != OP_NOT)
                        sb_char(&sb, L' ');
                    sb_text(&sb, *operand);
                    sb_char(&sb, L')');
                    PWSTR out = sb_take(&sb);
                    if (!out) {
                        error = ERROR_NOT_ENOUGH_MEMORY;
                    } else {
                        AdvFree(*operand);
                        *operand = out;
                    }
                }
            } else {
                if (count < 2) {
                    error = ERROR_INVALID_ACL;
                    break;
                }
                PWSTR *right = &stack[count - 1], *left = &stack[count - 2];
                if (op->Kind == OP_LOGIC)
                {
                    error = enclose(right);
                    if (!error)
                        error = enclose(left);
                }
                if (!error) {
                    SB sb = {};
                    sb_char(&sb, L'(');
                    sb_text(&sb, *left);
                    sb_char(&sb, L' ');
                    sb_text(&sb, op->Name);
                    sb_char(&sb, L' ');
                    sb_text(&sb, *right);
                    sb_char(&sb, L')');
                    PWSTR out = sb_take(&sb);
                    if (!out) {
                        error = ERROR_NOT_ENOUGH_MEMORY;
                    } else {
                        AdvFree(*left);
                        AdvFree(*right);
                        *left = out;
                        stack[--count] = nullptr;
                    }
                }
            }
        } else if ((token >= CT_INT8 && token <= CT_INT64) || token == CT_STRING || token == CT_OCTETS ||
                   token == CT_COMPOSITE || token == CT_SID) {
            error = print_operand(binary + at, size - at, context, &stack[count], &used, FALSE);
            if (!error)
                count++;
        } else {
            error = ERROR_INVALID_ACL;
        }
        at += used;
    }
    if (!error && count != 1)
        error = ERROR_INVALID_ACL;
    if (!error)
        error = enclose(&stack[0]);
    if (!error) {
        *result = stack[0];
        stack[0] = nullptr;
    }
    free_stack(stack, count + 1 > COND_MAX_STACK ? COND_MAX_STACK : count + 1);
    return error;
}

/* the text of a compiled condition, undocumented in windows: (condition, size, unused, LocalAlloc'd text). */
ADVAPI BOOL WINAPI GetStringConditionFromBinary(PVOID binary, DWORD size, PVOID unused, LPWSTR *text)
{
    PWSTR out;
    if (text)
        *text = nullptr;
    DWORD error = text ? condition_text(binary, size, nullptr, &out) : ERROR_INVALID_PARAMETER;
    if (!error) {
        DWORD chars = (DWORD)wcslen(out) + 1;
        *text = LocalAlloc(LMEM_FIXED, chars * sizeof(WCHAR));
        if (*text)
            memcpy(*text, out, chars * sizeof(WCHAR));
        else
            error = ERROR_NOT_ENOUGH_MEMORY;
        AdvFree(out);
    }
    if (error)
        AdvSetError(error);
    return error == ERROR_SUCCESS;
}

/* ---------------------------------------------------------------- guids */

static BOOL parse_guid(PCWSTR s, GUID *guid)
{
    static const BYTE dashes[4] = {8, 13, 18, 23};
    for (int i = 0; i < 36; i++) {
        BOOL dash = i == dashes[0] || i == dashes[1] || i == dashes[2] || i == dashes[3];
        if (dash ? s[i] != L'-' : hex_value(s[i]) < 0)
            return FALSE;
    }
    ULONGLONG part;
    BYTE *raw = (BYTE *)guid;
    static const struct {
        BYTE At;
        BYTE Digits;
    } fields[] = {{0, 8}, {9, 4}, {14, 4}};
    for (int f = 0; f < 3; f++) {
        part = 0;
        for (int i = 0; i < fields[f].Digits; i++)
            part = part << 4 | hex_value(s[fields[f].At + i]);
        if (f == 0)
            guid->Data1 = (ULONG)part;
        else if (f == 1)
            guid->Data2 = (USHORT)part;
        else
            guid->Data3 = (USHORT)part;
    }
    for (int i = 0; i < 8; i++) {
        PCWSTR at = i < 2 ? s + 19 + 2 * i : s + 24 + 2 * (i - 2);
        raw[8 + i] = (BYTE)(hex_value(at[0]) << 4 | hex_value(at[1]));
    }
    return TRUE;
}

static void sb_guid(SB *sb, const GUID *guid)
{
    sb_hex(sb, guid->Data1, 8, FALSE);
    sb_char(sb, L'-');
    sb_hex(sb, guid->Data2, 4, FALSE);
    sb_char(sb, L'-');
    sb_hex(sb, guid->Data3, 4, FALSE);
    sb_char(sb, L'-');
    sb_hex(sb, guid->Data4[0], 2, FALSE);
    sb_hex(sb, guid->Data4[1], 2, FALSE);
    sb_char(sb, L'-');
    for (int i = 2; i < 8; i++)
        sb_hex(sb, guid->Data4[i], 2, FALSE);
}

/* ---------------------------------------------------------------- lists as text */

#define KIND_DACL 1
#define KIND_SACL 2
#define KIND_LABEL_RIGHTS 4

static const struct {
    WCHAR Name[3];
    BYTE Length;
    BYTE Type;
    BYTE Kinds;
} g_ace_types[] = {
    {L"A", 1, ACCESS_ALLOWED_ACE_TYPE, KIND_DACL},
    {L"D", 1, ACCESS_DENIED_ACE_TYPE, KIND_DACL},
    {L"OA", 2, ACCESS_ALLOWED_OBJECT_ACE_TYPE, KIND_DACL},
    {L"OD", 2, ACCESS_DENIED_OBJECT_ACE_TYPE, KIND_DACL},
    {L"AU", 2, SYSTEM_AUDIT_ACE_TYPE, KIND_SACL},
    {L"AL", 2, SYSTEM_ALARM_ACE_TYPE, KIND_SACL},
    {L"OU", 2, SYSTEM_AUDIT_OBJECT_ACE_TYPE, KIND_SACL},
    {L"OL", 2, SYSTEM_ALARM_OBJECT_ACE_TYPE, KIND_SACL},
    {L"ML", 2, SYSTEM_MANDATORY_LABEL_ACE_TYPE, KIND_SACL},
    {L"TL", 2, SYSTEM_PROCESS_TRUST_LABEL_ACE_TYPE, KIND_SACL},
    {L"FL", 2, SYSTEM_ACCESS_FILTER_ACE_TYPE, KIND_SACL},
    {L"XA", 2, ACCESS_ALLOWED_CALLBACK_ACE_TYPE, KIND_DACL},
    {L"XD", 2, ACCESS_DENIED_CALLBACK_ACE_TYPE, KIND_DACL},
    {L"RA", 2, SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE, KIND_SACL},
    {L"SP", 2, SYSTEM_SCOPED_POLICY_ID_ACE_TYPE, KIND_SACL},
    {L"XU", 2, SYSTEM_AUDIT_CALLBACK_ACE_TYPE, KIND_SACL},
    {L"ZA", 2, ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE, KIND_DACL},
};

static const struct {
    WCHAR Name[3];
    BYTE Value;
    BYTE Kinds;
    /* the ace types the flag is written for, none listed meaning all of them */
    BYTE Types[8];
    BYTE TypeCount;
} g_ace_flags[] = {
    {L"CI", CONTAINER_INHERIT_ACE, KIND_DACL | KIND_SACL, {0}, 0},
    {L"OI", OBJECT_INHERIT_ACE, KIND_DACL | KIND_SACL, {0}, 0},
    {L"NP", NO_PROPAGATE_INHERIT_ACE, KIND_DACL | KIND_SACL, {0}, 0},
    {L"IO", INHERIT_ONLY_ACE, KIND_DACL | KIND_SACL, {0}, 0},
    {L"ID", INHERITED_ACE, KIND_DACL | KIND_SACL, {0}, 0},
    /* the critical flag is listed for no ace type, so the text can neither read nor write it. */
    {L"CR", CRITICAL_ACE_FLAG, KIND_DACL, {255}, 1},
    {L"TP", 0x40, KIND_SACL, {SYSTEM_ACCESS_FILTER_ACE_TYPE}, 1},
    {L"SA", SUCCESSFUL_ACCESS_ACE_FLAG, KIND_SACL, {2, 3, 7, 8, 13, 14, 15, 16}, 8},
    {L"FA", FAILED_ACCESS_ACE_FLAG, KIND_SACL, {2, 3, 7, 8, 13, 14, 15, 16}, 8},
};

static const struct {
    WCHAR Name[3];
    ACCESS_MASK Value;
    BYTE Kinds;
} g_rights[] = {
    {L"NW", 0x00000001, KIND_LABEL_RIGHTS},
    {L"NR", 0x00000002, KIND_LABEL_RIGHTS},
    {L"NX", 0x00000004, KIND_LABEL_RIGHTS},
    {L"RP", 0x00000010, KIND_DACL | KIND_SACL},
    {L"WP", 0x00000020, KIND_DACL | KIND_SACL},
    {L"CC", 0x00000001, KIND_DACL | KIND_SACL},
    {L"DC", 0x00000002, KIND_DACL | KIND_SACL},
    {L"LC", 0x00000004, KIND_DACL | KIND_SACL},
    {L"SW", 0x00000008, KIND_DACL | KIND_SACL},
    {L"LO", 0x00000080, KIND_DACL | KIND_SACL},
    {L"DT", 0x00000040, KIND_DACL | KIND_SACL},
    {L"CR", 0x00000100, KIND_DACL | KIND_SACL},
    {L"RC", 0x00020000, KIND_DACL | KIND_SACL},
    {L"WD", 0x00040000, KIND_DACL | KIND_SACL},
    {L"WO", 0x00080000, KIND_DACL | KIND_SACL},
    {L"SD", 0x00010000, KIND_DACL | KIND_SACL},
    {L"GA", 0x10000000, KIND_DACL | KIND_SACL},
    {L"GR", 0x80000000, KIND_DACL | KIND_SACL},
    {L"GW", 0x40000000, KIND_DACL | KIND_SACL},
    {L"GX", 0x20000000, KIND_DACL | KIND_SACL},
    {L"FA", 0x001F01FF, KIND_DACL | KIND_SACL},
    {L"FR", 0x00120089, KIND_DACL | KIND_SACL},
    {L"FW", 0x00120116, KIND_DACL | KIND_SACL},
    {L"FX", 0x001200A0, KIND_DACL | KIND_SACL},
    {L"KA", 0x000F003F, KIND_DACL | KIND_SACL},
    {L"KR", 0x00020019, KIND_DACL | KIND_SACL},
    {L"KW", 0x00020006, KIND_DACL | KIND_SACL},
    {L"KX", 0x00020019, KIND_DACL | KIND_SACL},
};

static const struct {
    WCHAR Name[3];
    BYTE Length;
    WORD Value;
    BYTE Kinds;
} g_controls[] = {
    {L"P", 1, SE_DACL_PROTECTED, KIND_DACL},
    {L"AR", 2, SE_DACL_AUTO_INHERIT_REQ, KIND_DACL},
    {L"AI", 2, SE_DACL_AUTO_INHERITED, KIND_DACL},
    {L"P", 1, SE_SACL_PROTECTED, KIND_SACL},
    {L"AR", 2, SE_SACL_AUTO_INHERIT_REQ, KIND_SACL},
    {L"AI", 2, SE_SACL_AUTO_INHERITED, KIND_SACL},
};

#define NO_ACCESS_CONTROL L"NO_ACCESS_CONTROL"

static BOOL flag_applies(ULONG index, BYTE kind, BYTE type)
{
    if (!(g_ace_flags[index].Kinds & kind))
        return FALSE;
    if (!g_ace_flags[index].TypeCount)
        return TRUE;
    for (BYTE i = 0; i < g_ace_flags[index].TypeCount; i++)
        if (g_ace_flags[index].Types[i] == type)
            return TRUE;
    return FALSE;
}

static BOOL is_object_type(BYTE type)
{
    return type == 5 || type == 6 || type == 7 || type == 8 || type == 11;
}

/* the aces that end in a condition. */
static BOOL has_condition(BYTE type)
{
    return type == 9 || type == 10 || type == 11 || type == 13 || type == 18 || type == 21;
}

/*
 * a resource attribute ace carries one attribute in its relative form after the sid: the offset of the name, the
 * type, the flags, the number of values, the offset of each value, then the name and the values one after the other.
 * as text it is ("name",type,0xflags,value,...) with the types TI, TU, TS, TD, TX and TB.
 */
#define RA_INT64 1
#define RA_UINT64 2
#define RA_STRING 3
#define RA_SID 5
#define RA_BOOLEAN 6
#define RA_OCTETS 0x10

static const struct {
    PCWSTR Name;
    WORD Type;
} g_attribute_types[] = {{L"TI", RA_INT64}, {L"TU", RA_UINT64}, {L"TS", RA_STRING},
                         {L"TD", RA_SID},   {L"TX", RA_OCTETS}, {L"TB", RA_BOOLEAN}};

typedef struct RA_VALUES {
    BYTE *Data;
    DWORD Size, Capacity, Count;
    DWORD *Offsets;
    DWORD OffsetsCapacity;
} RA_VALUES;

/* appends bytes to the values and notes where they began. */
static BOOL ra_add(RA_VALUES *v, const void *data, DWORD size, DWORD prefix_length)
{
    DWORD total = size + (prefix_length ? 4 : 0);
    if (v->Count == v->OffsetsCapacity) {
        DWORD capacity = v->OffsetsCapacity ? v->OffsetsCapacity * 2 : 8;
        DWORD *grown = AdvAlloc(capacity * sizeof(DWORD));
        if (!grown)
            return FALSE;
        if (v->Offsets)
            memcpy(grown, v->Offsets, v->Count * sizeof(DWORD));
        AdvFree(v->Offsets);
        v->Offsets = grown;
        v->OffsetsCapacity = capacity;
    }
    if (v->Size + total < v->Size)
        return FALSE;
    if (v->Size + total > v->Capacity) {
        DWORD capacity = v->Capacity ? v->Capacity * 2 : 64;
        while (capacity < v->Size + total)
            capacity *= 2;
        BYTE *grown = AdvAlloc(capacity);
        if (!grown)
            return FALSE;
        if (v->Data)
            memcpy(grown, v->Data, v->Size);
        AdvFree(v->Data);
        v->Data = grown;
        v->Capacity = capacity;
    }
    v->Offsets[v->Count++] = v->Size;
    if (prefix_length) {
        DWORD length = size;
        memcpy(v->Data + v->Size, &length, 4);
        v->Size += 4;
    }
    if (size)
        memcpy(v->Data + v->Size, data, size);
    v->Size += size;
    return TRUE;
}

static BOOL ra_digit(WCHAR c, BYTE *value)
{
    int v = hex_value(c);
    if (c == L'#')
        v = 0;
    if (v < 0)
        return FALSE;
    *value = (BYTE)v;
    return TRUE;
}

/* the attribute of the text standing at its opening parenthesis, as the relative form in a new block. */
static DWORD parse_resource_attribute(PCWSTR p, const SDDL_CONTEXT *context, PVOID *result, DWORD *size, PCWSTR *end)
{
    RA_VALUES values = {};
    PWSTR name = nullptr;
    BYTE *out = nullptr;
    DWORD error = ERROR_INVALID_ACL, name_length = 0, flags = 0;
    WORD type = 0;
    if (*p != L'(')
        goto done;
    p++;
    while (is_space(*p))
        p++;
    if (*p != L'"')
        goto done;
    p++;
    DWORD run = 0;
    while (at_name_char(p[run]))
        run++;
    if (!run || p[run] != L'"')
        goto done;
    name = AdvAlloc((run + 1) * sizeof(WCHAR));
    if (!name) {
        error = ERROR_NOT_ENOUGH_MEMORY;
        goto done;
    }
    for (DWORD i = 0; i < run;) {
        WCHAR ch = p[i];
        if (ch == L'%') {
            int v0, v1, v2, v3;
            if (i + 4 >= run || (v0 = hex_value(p[i + 1])) < 0 || (v1 = hex_value(p[i + 2])) < 0 ||
                (v2 = hex_value(p[i + 3])) < 0 || (v3 = hex_value(p[i + 4])) < 0)
                goto done;
            ch = (WCHAR)(v0 << 12 | v1 << 8 | v2 << 4 | v3);
            if (!needs_encoding(ch))
                goto done;
            i += 5;
        } else {
            i++;
        }
        name[name_length++] = ch;
    }
    name[name_length] = 0;
    p += run + 1;
    while (is_space(*p))
        p++;
    if (*p++ != L',')
        goto done;
    while (is_space(*p))
        p++;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_attribute_types) && !type; i++)
        if (starts_with(p, g_attribute_types[i].Name, 2))
            type = g_attribute_types[i].Type;
    if (!type)
        goto done;
    p += 2;
    while (is_space(*p))
        p++;
    if (*p++ != L',')
        goto done;
    while (is_space(*p))
        p++;
    {
        ULONGLONG value;
        BOOL overflow;
        PCWSTR after = scan_number(p, 0xFFFFFFFFull, &value, &overflow, TRUE);
        if (overflow) {
            error = ERROR_ARITHMETIC_OVERFLOW;
            goto done;
        }
        if (!after || after == p || (value & 0xFFC0))
            goto done;
        flags = (DWORD)value;
        p = after;
    }
    while (is_space(*p))
        p++;
    if (*p++ != L',')
        goto done;
    for (;;) {
        while (is_space(*p))
            p++;
        if (!*p)
            goto done;
        BOOL added = FALSE;
        if (type == RA_INT64 || type == RA_UINT64 || type == RA_BOOLEAN) {
            BOOL negative = type == RA_INT64 && *p == L'-';
            ULONGLONG value;
            BOOL overflow;
            PCWSTR after = scan_number(negative || (type == RA_INT64 && *p == L'+') ? p + 1 : p,
                                       negative ? 0x8000000000000000ull : (type == RA_INT64 ? 0x7FFFFFFFFFFFFFFFull : ~0ull),
                                       &value, &overflow, TRUE);
            if (overflow) {
                error = ERROR_ARITHMETIC_OVERFLOW;
                goto done;
            }
            if (!after || after == p || (type == RA_BOOLEAN && value > 1))
                goto done;
            LONGLONG stored = negative ? (LONGLONG)(0 - value) : (LONGLONG)value;
            added = ra_add(&values, &stored, 8, 0);
            p = after;
        } else if (type == RA_STRING) {
            if (*p != L'"')
                goto done;
            PCWSTR close = p + 1;
            while (*close && *close != L'"')
                close++;
            if (*close != L'"')
                goto done;
            DWORD chars = (DWORD)(close - (p + 1));
            PWSTR copy = AdvAlloc((chars + 1) * sizeof(WCHAR));
            if (!copy) {
                error = ERROR_NOT_ENOUGH_MEMORY;
                goto done;
            }
            memcpy(copy, p + 1, chars * sizeof(WCHAR));
            copy[chars] = 0;
            added = ra_add(&values, copy, (chars + 1) * sizeof(WCHAR), 0);
            AdvFree(copy);
            p = close + 1;
        } else if (type == RA_SID) {
            BYTE sid[SECURITY_MAX_SID_SIZE];
            PCWSTR after;
            DWORD sid_error = AdvParseSidString(p, context, sid, &after);
            if (sid_error) {
                error = sid_error;
                goto done;
            }
            added = ra_add(&values, sid, AdvSidLength((const SID *)sid), 4);
            p = after;
        } else {
            DWORD digits = 0;
            BYTE d;
            while (ra_digit(p[digits], &d))
                digits++;
            DWORD bytes = (digits + 1) / 2;
            BYTE *raw = bytes ? AdvAlloc(bytes) : nullptr;
            if (bytes && !raw) {
                error = ERROR_NOT_ENOUGH_MEMORY;
                goto done;
            }
            /* the digits are taken from the right, so a lone first digit is the low half of the first byte. */
            for (DWORD i = 0; i < bytes; i++) {
                BYTE low = 0, high = 0;
                LONG at = (LONG)digits - 1 - 2 * (LONG)(bytes - 1 - i);
                ra_digit(p[at], &low);
                if (at > 0)
                    ra_digit(p[at - 1], &high);
                raw[i] = (BYTE)(low | high << 4);
            }
            added = ra_add(&values, raw, bytes, 4);
            AdvFree(raw);
            p += digits;
        }
        if (!added) {
            error = ERROR_NOT_ENOUGH_MEMORY;
            goto done;
        }
        while (is_space(*p))
            p++;
        if (*p == L',') {
            p++;
            continue;
        }
        break;
    }
    if (*p != L')' || !values.Count)
        goto done;
    p++;
    {
        DWORD header = 16 + 4 * values.Count, name_bytes = (name_length + 1) * sizeof(WCHAR);
        DWORD total = (header + name_bytes + values.Size + 3) & ~3u;
        out = AdvAllocZero(total);
        if (!out) {
            error = ERROR_NOT_ENOUGH_MEMORY;
            goto done;
        }
        *(DWORD *)out = header;
        *(WORD *)(out + 4) = type;
        *(DWORD *)(out + 8) = flags;
        *(DWORD *)(out + 12) = values.Count;
        for (DWORD i = 0; i < values.Count; i++)
            *(DWORD *)(out + 16 + 4 * i) = header + name_bytes + values.Offsets[i];
        memcpy(out + header, name, name_bytes);
        memcpy(out + header + name_bytes, values.Data, values.Size);
        *result = out;
        *size = total;
        *end = p;
        error = ERROR_SUCCESS;
    }
done:
    AdvFree(name);
    AdvFree(values.Data);
    AdvFree(values.Offsets);
    return error;
}

/* the text of a relative attribute, which is checked as it is read. */
static DWORD print_resource_attribute(const BYTE *d, DWORD n, const SDDL_CONTEXT *context, SB *sb)
{
    if (n < 20)
        return ERROR_INVALID_ACL;
    DWORD name_offset = *(const DWORD *)d, flags = *(const DWORD *)(d + 8), count = *(const DWORD *)(d + 12);
    WORD type = *(const WORD *)(d + 4);
    if (!count || n - 16 < 4 * (ULONGLONG)count || name_offset >= n || n - name_offset < 4)
        return ERROR_INVALID_ACL;
    DWORD room = (n - name_offset) / sizeof(WCHAR), name_length = 0;
    const WCHAR *name = (const WCHAR *)(d + name_offset);
    while (name_length < room && name[name_length])
        name_length++;
    if (name_length == room || !name_length)
        return ERROR_INVALID_ACL;
    PCWSTR type_name = nullptr;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_attribute_types); i++)
        if (g_attribute_types[i].Type == type)
            type_name = g_attribute_types[i].Name;
    if (!type_name)
        return ERROR_INVALID_ACL;
    sb_text(sb, L"(\"");
    for (DWORD i = 0; i < name_length; i++) {
        if (needs_encoding(name[i])) {
            sb_char(sb, L'%');
            sb_hex(sb, name[i], 4, FALSE);
        } else {
            sb_char(sb, name[i]);
        }
    }
    sb_text(sb, L"\",");
    sb_text(sb, type_name);
    sb_text(sb, L",0x");
    sb_hex(sb, flags, 1, FALSE);
    for (DWORD i = 0; i < count; i++) {
        DWORD at = *(const DWORD *)(d + 16 + 4 * i);
        sb_char(sb, L',');
        if (type == RA_INT64 || type == RA_UINT64 || type == RA_BOOLEAN) {
            if (at > n || n - at < 8)
                return ERROR_INVALID_ACL;
            ULONGLONG value;
            memcpy(&value, d + at, 8);
            if (type == RA_INT64 && (LONGLONG)value < 0) {
                sb_char(sb, L'-');
                value = 0 - value;
            }
            sb_decimal(sb, value);
        } else if (type == RA_STRING) {
            if (at > n || n - at < 2)
                return ERROR_INVALID_ACL;
            DWORD chars = 0, left = (n - at) / sizeof(WCHAR);
            const WCHAR *text = (const WCHAR *)(d + at);
            while (chars < left && text[chars])
                chars++;
            if (chars == left)
                return ERROR_INVALID_ACL;
            sb_char(sb, L'"');
            sb_chars(sb, text, chars);
            sb_char(sb, L'"');
        } else {
            if (at > n || n - at < 4)
                return ERROR_INVALID_ACL;
            DWORD length = *(const DWORD *)(d + at);
            if (n - at - 4 < length)
                return ERROR_INVALID_ACL;
            const BYTE *bytes = d + at + 4;
            if (type == RA_SID) {
                if (length < 8 || !RtlValidSid((PSID)bytes) || AdvSidLength((const SID *)bytes) > length)
                    return ERROR_INVALID_ACL;
                sb_sid(sb, (const SID *)bytes, context);
            } else {
                for (DWORD b = 0; b < length; b++)
                    sb_hex(sb, bytes[b], 2, FALSE);
            }
        }
    }
    sb_char(sb, L')');
    return ERROR_SUCCESS;
}

static DWORD add_ace_text(ACL *acl, BYTE type, BYTE flags, ACCESS_MASK mask, const GUID *object, const GUID *inherited,
                          const SID *sid, PVOID condition, DWORD condition_size)
{
    NTSTATUS status;
    PACL a = acl;
    switch (type) {
    case ACCESS_ALLOWED_ACE_TYPE:
        status = RtlAddAccessAllowedAceEx(a, 2, flags, mask, (PSID)sid);
        break;
    case ACCESS_DENIED_ACE_TYPE:
        status = RtlAddAccessDeniedAceEx(a, 2, flags, mask, (PSID)sid);
        break;
    case SYSTEM_AUDIT_ACE_TYPE:
        status = RtlAddAuditAccessAceEx(a, 2, flags & 0x3F, mask, (PSID)sid, (flags & 0x40) != 0, (flags & 0x80) != 0);
        break;
    case ACCESS_ALLOWED_OBJECT_ACE_TYPE:
        status = RtlAddAccessAllowedObjectAce(a, 4, flags, mask, object, inherited, (PSID)sid);
        break;
    case ACCESS_DENIED_OBJECT_ACE_TYPE:
        status = RtlAddAccessDeniedObjectAce(a, 4, flags, mask, object, inherited, (PSID)sid);
        break;
    case SYSTEM_AUDIT_OBJECT_ACE_TYPE:
        status = RtlAddAuditAccessObjectAce(a, 4, flags, mask, object, inherited, (PSID)sid, (flags & 0x40) != 0,
                                            (flags & 0x80) != 0);
        break;
    case ACCESS_ALLOWED_CALLBACK_ACE_TYPE:
    case ACCESS_DENIED_CALLBACK_ACE_TYPE:
    case SYSTEM_AUDIT_CALLBACK_ACE_TYPE:
    case ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE: {
        /* a callback object ace without guids is the plain callback ace. */
        BOOL objects = type == ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE && (object || inherited);
        BYTE ace_type = type == ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE && !objects ? ACCESS_ALLOWED_CALLBACK_ACE_TYPE : type;
        DWORD sid_size = RtlLengthSid((PSID)sid), padded = (condition_size + 3) & ~3u;
        DWORD guids = objects ? 4 + (object ? 16 : 0) + (inherited ? 16 : 0) : 0;
        DWORD size = 8 + guids + sid_size + padded;
        if (condition_size > 0xFFFFFFFC || size >= 0xFFFF)
            return ERROR_INVALID_PARAMETER;
        BYTE *ace = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, size);
        if (!ace)
            return ERROR_NOT_ENOUGH_MEMORY;
        BYTE *at = ace + 8;
        ace[0] = ace_type;
        ace[1] = flags;
        *(WORD *)(ace + 2) = (WORD)size;
        *(DWORD *)(ace + 4) = mask;
        if (objects) {
            *(DWORD *)at = (object ? ACE_OBJECT_TYPE_PRESENT : 0) | (inherited ? ACE_INHERITED_OBJECT_TYPE_PRESENT : 0);
            at += 4;
            if (object) {
                memcpy(at, object, 16);
                at += 16;
            }
            if (inherited) {
                memcpy(at, inherited, 16);
                at += 16;
            }
        }
        memcpy(at, sid, sid_size);
        memcpy(at + sid_size, condition, condition_size);
        status = RtlAddAce(a, objects ? 4 : 2, 0xFFFFFFFF, ace, size);
        LocalFree(ace);
        break;
    }
    case SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE: {
        DWORD sid_size = RtlLengthSid((PSID)sid), size = 8 + sid_size + condition_size;
        if (size >= 0xFFFF)
            return ERROR_INVALID_PARAMETER;
        BYTE *ace = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, size);
        if (!ace)
            return ERROR_NOT_ENOUGH_MEMORY;
        ace[0] = type;
        ace[1] = flags;
        *(WORD *)(ace + 2) = (WORD)size;
        *(DWORD *)(ace + 4) = mask;
        memcpy(ace + 8, sid, sid_size);
        memcpy(ace + 8 + sid_size, condition, condition_size);
        status = RtlAddAce(a, 2, 0xFFFFFFFF, ace, size);
        LocalFree(ace);
        break;
    }
    case SYSTEM_MANDATORY_LABEL_ACE_TYPE:
        status = RtlAddMandatoryAce(a, 2, flags, (PSID)sid, SYSTEM_MANDATORY_LABEL_ACE_TYPE, mask);
        break;
    case SYSTEM_SCOPED_POLICY_ID_ACE_TYPE:
        status = RtlAddScopedPolicyIDAce(a, 2, flags, mask, (PSID)sid);
        break;
    case SYSTEM_PROCESS_TRUST_LABEL_ACE_TYPE:
        status = RtlAddProcessTrustLabelAce(a, 2, flags, (PSID)sid, SYSTEM_PROCESS_TRUST_LABEL_ACE_TYPE, mask);
        break;
    case SYSTEM_ACCESS_FILTER_ACE_TYPE:
        status = condition_size < 0xFFFF
                     ? RtlAddAccessFilterAce(a, 2, flags, (PSID)sid, SYSTEM_ACCESS_FILTER_ACE_TYPE, mask, condition,
                                             (USHORT)condition_size)
                     : STATUS_INVALID_PARAMETER;
        break;
    default:
        status = STATUS_INVALID_PARAMETER;
        break;
    }
    return NT_SUCCESS(status) ? ERROR_SUCCESS : RtlNtStatusToDosError(status);
}

/* one ace between the parentheses, the text standing after the opening one. */
static DWORD parse_ace(PCWSTR *pp, BYTE kind, const SDDL_CONTEXT *context, ACL *acl)
{
    PCWSTR p = *pp;
    ULONG t = 0;
    while (t < RTL_NUMBER_OF(g_ace_types) &&
           (!(g_ace_types[t].Kinds & kind) || !starts_with(p, g_ace_types[t].Name, g_ace_types[t].Length)))
        t++;
    if (t == RTL_NUMBER_OF(g_ace_types))
        return ERROR_INVALID_DATATYPE_;
    BYTE type = g_ace_types[t].Type;
    p += g_ace_types[t].Length;
    if (*p != L';' && *p != L' ')
        return ERROR_INVALID_ACL;
    p++;
    if (is_object_type(type))
        acl->AclRevision = ACL_REVISION_DS;
    while (*p == L' ')
        p++;
    /* the flags, up to the semicolon that ends them. */
    BYTE flags = 0;
    while (*p != L';') {
        while (*p == L' ')
            p++;
        if (*p == 0)
            return ERROR_INVALID_ACL;
        ULONG f = 0;
        while (f < RTL_NUMBER_OF(g_ace_flags) &&
               (!flag_applies(f, kind, type) || !starts_with(p, g_ace_flags[f].Name, 2)))
            f++;
        if (f == RTL_NUMBER_OF(g_ace_flags))
            return ERROR_INVALID_FLAGS_;
        flags |= g_ace_flags[f].Value;
        p += 2;
    }
    p++;
    /* the rights: names and numbers, up to the next semicolon. */
    while (*p == L' ')
        p++;
    ACCESS_MASK mask = 0;
    BYTE rights_kind = type == SYSTEM_MANDATORY_LABEL_ACE_TYPE ? KIND_LABEL_RIGHTS : kind;
    while (*p != L';') {
        while (*p == L' ')
            p++;
        if (*p == L';')
            break;
        ULONG r = 0;
        while (r < RTL_NUMBER_OF(g_rights) &&
               (!(g_rights[r].Kinds & rights_kind) || !starts_with(p, g_rights[r].Name, 2)))
            r++;
        if (r < RTL_NUMBER_OF(g_rights)) {
            mask |= g_rights[r].Value;
            p += 2;
        } else {
            ULONGLONG value;
            BOOL overflow;
            PCWSTR end = scan_number(p, 0xFFFFFFFFull, &value, &overflow, TRUE);
            if (!end || end == p)
                return ERROR_INVALID_ACL;
            mask |= (ACCESS_MASK)value;
            p = end;
        }
    }
    p++;
    /* the object and inherited object guids, each with its semicolon. */
    GUID guids[2];
    const GUID *given[2] = {nullptr, nullptr};
    for (int j = 0; j < 2; j++) {
        while (*p == L' ')
            p++;
        if (*p != L';') {
            if (!is_object_type(type))
                return ERROR_INVALID_ACL;
            if (!parse_guid(p, &guids[j]))
                return ERROR_INVALID_UUID_;
            given[j] = &guids[j];
            p += 36;
            if (*p != L';' && *p != L' ')
                return ERROR_INVALID_UUID_;
        }
        p++;
    }
    while (*p == L' ')
        p++;
    if (!*p || !p[1])
        return ERROR_NONE_MAPPED;
    BYTE sid[SECURITY_MAX_SID_SIZE];
    PCWSTR end;
    DWORD error = AdvParseSidString(p, context, sid, &end);
    if (error)
        return error;
    p = end;
    while (*p == L' ')
        p++;
    PVOID condition = nullptr;
    DWORD condition_size = 0;
    if (has_condition(type)) {
        if (*p != L';')
            return ERROR_INVALID_ACL;
        p++;
        while (*p == L' ')
            p++;
        if (*p != L'(')
            return ERROR_INVALID_ACL;
        error = type == SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE
                    ? parse_resource_attribute(p, context, &condition, &condition_size, &end)
                    : condition_compile(p, context, &condition, &condition_size, &end);
        if (error)
            return error;
        p = end;
        while (*p == L' ')
            p++;
    }
    if (*p != L')') {
        error = ERROR_INVALID_ACL;
    } else {
        p++;
        error = add_ace_text(acl, type, flags, mask, given[0], given[1], (SID *)sid, condition, condition_size);
    }
    if (condition) {
        if (type == SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE)
            AdvFree(condition);
        else
            LocalFree(condition);
    }
    *pp = p;
    return error;
}

static BOOL section_start(PCWSTR p)
{
    return p[1] == L':' && (p[0] == L'O' || p[0] == L'G' || p[0] == L'D' || p[0] == L'S');
}

/*
 * the list of one D: or S: part. a null list is written NO_ACCESS_CONTROL; the text ends at the next part. the list
 * comes back trimmed to the bytes its aces use, LocalAlloc'd, or NULL for the null list.
 */
static DWORD parse_acl_text(PCWSTR *pp, BYTE kind, const SDDL_CONTEXT *context, ACL **result)
{
    PCWSTR p = *pp;
    *result = nullptr;
    if (starts_with(p, NO_ACCESS_CONTROL, 17)) {
        *pp = p + 17;
        return ERROR_SUCCESS;
    }
    ACL *acl = AdvAlloc(0xFFFC);
    if (!acl)
        return ERROR_NOT_ENOUGH_MEMORY;
    RtlCreateAcl(acl, 0xFFFC, ACL_REVISION);
    DWORD error = ERROR_SUCCESS;
    for (;;) {
        while (*p == L' ')
            p++;
        if (*p != L'(')
            break;
        p++;
        while (*p == L' ')
            p++;
        error = parse_ace(&p, kind, context, acl);
        if (error)
            break;
    }
    if (!error && *p && !section_start(p))
        error = ERROR_INVALID_PARAMETER;
    if (!error) {
        DWORD used = sizeof(ACL);
        const BYTE *at = (const BYTE *)(acl + 1);
        for (DWORD i = 0; i < acl->AceCount; i++) {
            DWORD n = *(const WORD *)(at + 2);
            used += n;
            at += n;
        }
        ACL *out = LocalAlloc(LMEM_FIXED, used);
        if (!out) {
            error = ERROR_NOT_ENOUGH_MEMORY;
        } else {
            memcpy(out, acl, used);
            out->AclSize = (WORD)used;
            *result = out;
            *pp = p;
        }
    }
    AdvFree(acl);
    return error;
}

/* ---------------------------------------------------------------- lists to text */

static void sb_flags(SB *sb, BYTE kind, BYTE type, BYTE flags)
{
    for (int bit = 0; bit < 8; bit++) {
        if (!(flags & (1 << bit)))
            continue;
        for (ULONG f = 0; f < RTL_NUMBER_OF(g_ace_flags); f++) {
            if (g_ace_flags[f].Value == 1 << bit && flag_applies(f, kind, type)) {
                sb_chars(sb, g_ace_flags[f].Name, 2);
                break;
            }
        }
    }
}

static void sb_rights(SB *sb, BYTE kind, ACCESS_MASK mask)
{
    for (ULONG r = 0; r < RTL_NUMBER_OF(g_rights); r++) {
        if ((g_rights[r].Kinds & kind) == kind && g_rights[r].Value == mask) {
            sb_chars(sb, g_rights[r].Name, 2);
            return;
        }
    }
    /* a mask of single bit names, or the number when one of its bits has none. */
    SB names = {};
    for (int bit = 0; bit < 32; bit++) {
        if (!(mask & (1u << bit)))
            continue;
        ULONG r = 0;
        while (r < RTL_NUMBER_OF(g_rights) && !((g_rights[r].Kinds & kind) == kind && g_rights[r].Value == 1u << bit))
            r++;
        if (r == RTL_NUMBER_OF(g_rights)) {
            sb_text(sb, L"0x");
            sb_hex(sb, mask, 1, FALSE);
            AdvFree(names.Text);
            return;
        }
        sb_chars(&names, g_rights[r].Name, 2);
    }
    if (names.Text)
        sb_text(sb, names.Text);
    AdvFree(names.Text);
}

/* an ace of the list as "(type;flags;rights;object;inherited;sid[;condition])". */
static DWORD sb_ace(SB *sb, const BYTE *ace, BYTE kind, const SDDL_CONTEXT *context)
{
    BYTE type = ace[0];
    ULONG t = 0;
    while (t < RTL_NUMBER_OF(g_ace_types) && !((g_ace_types[t].Kinds & kind) == kind && g_ace_types[t].Type == type))
        t++;
    if (t == RTL_NUMBER_OF(g_ace_types))
        return ERROR_INVALID_ACL;
    DWORD size = *(const WORD *)(ace + 2), mask = *(const DWORD *)(ace + 4);
    const GUID *object = nullptr, *inherited = nullptr;
    const BYTE *sid = ace + 8;
    if (is_object_type(type)) {
        DWORD flags = *(const DWORD *)(ace + 8);
        const BYTE *at = ace + 12;
        if (flags & ACE_OBJECT_TYPE_PRESENT) {
            object = (const GUID *)at;
            at += 16;
        }
        if (flags & ACE_INHERITED_OBJECT_TYPE_PRESENT) {
            inherited = (const GUID *)at;
            at += 16;
        }
        sid = at;
    }
    if (!RtlValidSid((PSID)sid))
        return ERROR_INVALID_ACL;
    sb_char(sb, L'(');
    sb_chars(sb, g_ace_types[t].Name, g_ace_types[t].Length);
    sb_char(sb, L';');
    sb_flags(sb, kind, type, ace[1]);
    sb_char(sb, L';');
    sb_rights(sb, type == SYSTEM_MANDATORY_LABEL_ACE_TYPE ? KIND_LABEL_RIGHTS : kind, mask);
    sb_char(sb, L';');
    if (object)
        sb_guid(sb, object);
    sb_char(sb, L';');
    if (inherited)
        sb_guid(sb, inherited);
    sb_char(sb, L';');
    sb_sid(sb, (const SID *)sid, context);
    if (has_condition(type)) {
        DWORD data = size - (DWORD)(sid - ace) - AdvSidLength((const SID *)sid);
        if (type == SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE) {
            if (data) {
                sb_char(sb, L';');
                DWORD error = print_resource_attribute(sid + AdvSidLength((const SID *)sid), data, context, sb);
                if (error)
                    return error;
            }
        } else if (data) {
            PWSTR text;
            DWORD error = condition_text(sid + AdvSidLength((const SID *)sid), data, context, &text);
            if (error)
                return error;
            sb_char(sb, L';');
            sb_text(sb, text);
            AdvFree(text);
        }
    }
    sb_char(sb, L')');
    return ERROR_SUCCESS;
}

/* the list as text; a missing list is NO_ACCESS_CONTROL and an empty one is nothing. */
static DWORD acl_text(const ACL *acl, BYTE kind, const SDDL_CONTEXT *context, SB *sb)
{
    if (!acl) {
        sb_text(sb, NO_ACCESS_CONTROL);
        return ERROR_SUCCESS;
    }
    const BYTE *ace = (const BYTE *)(acl + 1);
    for (DWORD i = 0; i < acl->AceCount; i++) {
        DWORD error = sb_ace(sb, ace, kind, context);
        if (error)
            return error;
        ace += *(const WORD *)(ace + 2);
    }
    return sb->Failed ? ERROR_NOT_ENOUGH_MEMORY : ERROR_SUCCESS;
}

/* ---------------------------------------------------------------- descriptors as text */

/* the control letters of a part, P for protected and AR and AI for the auto inherit bits. */
static void parse_control(PCWSTR *pp, BYTE kind, WORD *control)
{
    PCWSTR p = *pp;
    for (;;) {
        while (*p == L' ')
            p++;
        ULONG i = 0;
        while (i < RTL_NUMBER_OF(g_controls) &&
               (!(g_controls[i].Kinds & kind) || !starts_with(p, g_controls[i].Name, g_controls[i].Length)))
            i++;
        if (i == RTL_NUMBER_OF(g_controls))
            break;
        *control |= g_controls[i].Value;
        p += g_controls[i].Length;
    }
    *pp = p;
}

/* the sid of an O: or G: part, with the text left after it. */
static DWORD parse_owner(PCWSTR *pp, const SDDL_CONTEXT *context, BYTE *sid)
{
    PCWSTR p = *pp;
    if (!*p || !p[1])
        return ERROR_NONE_MAPPED;
    PCWSTR end;
    DWORD error = AdvParseSidString(p, context, sid, &end);
    if (!error)
        *pp = end;
    return error;
}

static DWORD sd_from_text(PCWSTR text, const SDDL_CONTEXT *context, PSECURITY_DESCRIPTOR *result, ULONG *size)
{
    BYTE owner[SECURITY_MAX_SID_SIZE], group[SECURITY_MAX_SID_SIZE];
    ACL *dacl = nullptr, *sacl = nullptr;
    BOOL has_owner = FALSE, has_group = FALSE, has_dacl = FALSE, has_sacl = FALSE;
    WORD control = 0;
    DWORD error = ERROR_SUCCESS;
    PCWSTR p = text;
    while (!error && *p) {
        switch (*p) {
        case L' ':
            p++;
            break;
        case L'D':
        case L'S': {
            BOOL is_dacl = *p == L'D';
            if (p[1] != L':' || (is_dacl ? has_dacl : has_sacl)) {
                error = ERROR_INVALID_PARAMETER;
                break;
            }
            p += 2;
            ACL *acl;
            if (*p != L'(')
                parse_control(&p, is_dacl ? KIND_DACL : KIND_SACL, &control);
            error = parse_acl_text(&p, is_dacl ? KIND_DACL : KIND_SACL, context, &acl);
            if (error)
                break;
            if (is_dacl)
                dacl = acl, has_dacl = TRUE;
            else
                sacl = acl, has_sacl = TRUE;
            break;
        }
        case L'O':
        case L'G': {
            BOOL is_owner = *p == L'O';
            if (p[1] != L':' || (is_owner ? has_owner : has_group)) {
                error = ERROR_INVALID_PARAMETER;
                break;
            }
            p += 2;
            error = parse_owner(&p, context, is_owner ? owner : group);
            if (!error) {
                if (is_owner)
                    has_owner = TRUE;
                else
                    has_group = TRUE;
            }
            break;
        }
        default:
            error = ERROR_INVALID_PARAMETER;
            break;
        }
    }
    if (!error) {
        SECURITY_DESCRIPTOR sd;
        RtlCreateSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
        sd.Control |= control;
        RtlSetOwnerSecurityDescriptor(&sd, has_owner ? owner : nullptr, FALSE);
        RtlSetGroupSecurityDescriptor(&sd, has_group ? group : nullptr, FALSE);
        RtlSetDaclSecurityDescriptor(&sd, has_dacl, dacl, FALSE);
        RtlSetSaclSecurityDescriptor(&sd, has_sacl, sacl, FALSE);
        ULONG length = 0;
        NTSTATUS status = RtlAbsoluteToSelfRelativeSD(&sd, nullptr, &length);
        if (status != STATUS_BUFFER_TOO_SMALL) {
            error = ERROR_INSUFFICIENT_BUFFER;
        } else {
            PVOID out = LocalAlloc(LMEM_FIXED | LMEM_ZEROINIT, length);
            if (!out) {
                error = ERROR_NOT_ENOUGH_MEMORY;
            } else {
                status = RtlAbsoluteToSelfRelativeSD(&sd, out, &length);
                if (NT_SUCCESS(status)) {
                    *result = out;
                    if (size)
                        *size = length;
                } else {
                    LocalFree(out);
                    error = RtlNtStatusToDosError(status);
                }
            }
        }
    }
    LocalFree(dacl);
    LocalFree(sacl);
    return error;
}

/* the aces of a system list the requested information keeps. */
static BOOL sacl_ace_wanted(BYTE type, SECURITY_INFORMATION info)
{
    switch (type) {
    case SYSTEM_MANDATORY_LABEL_ACE_TYPE:
        return (info & 0x10) != 0;
    case SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE:
        return (info & 0x20) != 0;
    case SYSTEM_SCOPED_POLICY_ID_ACE_TYPE:
        return (info & 0x40) != 0;
    case SYSTEM_PROCESS_TRUST_LABEL_ACE_TYPE:
        return (info & 0x80) != 0;
    case SYSTEM_ACCESS_FILTER_ACE_TYPE:
        return (info & 0x100) != 0;
    default:
        return (info & 8) != 0;
    }
}

static void sb_control(SB *sb, WORD control, BYTE kind)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_controls); i++)
        if ((g_controls[i].Kinds & kind) && (control & g_controls[i].Value))
            sb_chars(sb, g_controls[i].Name, g_controls[i].Length);
}

/*
 * the descriptor as text, O: G: D: S: in that order, for the parts the information asks for. the length counts
 * the characters the way windows reports it: one extra for each ace and for each null list.
 */
static DWORD sd_to_text(PSECURITY_DESCRIPTOR sd, SECURITY_INFORMATION info, const SDDL_CONTEXT *context, PWSTR *result,
                        ULONG *length)
{
    PSID owner = nullptr, group = nullptr;
    PACL dacl = nullptr, sacl = nullptr, filtered = nullptr;
    BOOLEAN dacl_present = FALSE, sacl_present = FALSE, defaulted;
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    ULONG revision;
    NTSTATUS status = STATUS_SUCCESS;
    DWORD error = ERROR_SUCCESS;
    if (info & OWNER_SECURITY_INFORMATION)
        status = RtlGetOwnerSecurityDescriptor(sd, &owner, &defaulted);
    if (NT_SUCCESS(status) && (info & GROUP_SECURITY_INFORMATION))
        status = RtlGetGroupSecurityDescriptor(sd, &group, &defaulted);
    if (NT_SUCCESS(status) && (info & DACL_SECURITY_INFORMATION))
        status = RtlGetDaclSecurityDescriptor(sd, &dacl_present, &dacl, &defaulted);
    if (NT_SUCCESS(status) && (info & 0x1F8)) {
        status = RtlGetSaclSecurityDescriptor(sd, &sacl_present, &sacl, &defaulted);
        if (NT_SUCCESS(status) && sacl_present && sacl) {
            filtered = AdvAlloc(sacl->AclSize);
            if (!filtered)
                return ERROR_NOT_ENOUGH_MEMORY;
            RtlCreateAcl(filtered, sacl->AclSize, sacl->AclRevision);
            UCHAR *to = (UCHAR *)(filtered + 1);
            const UCHAR *from = (const UCHAR *)(sacl + 1);
            for (DWORD i = 0; i < sacl->AceCount; i++) {
                DWORD n = *(const WORD *)(from + 2);
                if (sacl_ace_wanted(from[0], info)) {
                    memcpy(to, from, n);
                    to += n;
                    filtered->AceCount++;
                }
                from += n;
            }
            sacl = filtered;
        }
    }
    if (NT_SUCCESS(status))
        status = RtlGetControlSecurityDescriptor(sd, &control, &revision);
    if (!NT_SUCCESS(status)) {
        AdvFree(filtered);
        return RtlNtStatusToDosError(status);
    }
    SB sb = {};
    DWORD counted = 0;
    sb_reserve(&sb, 0);
    if (sb.Text)
        sb.Text[0] = 0;
    if (owner) {
        sb_text(&sb, L"O:");
        sb_sid(&sb, owner, context);
    }
    if (group) {
        sb_text(&sb, L"G:");
        sb_sid(&sb, group, context);
    }
    if (dacl_present) {
        sb_text(&sb, L"D:");
        if (control)
            sb_control(&sb, control, KIND_DACL);
        error = acl_text(dacl, KIND_DACL, context, &sb);
    }
    if (!error && sacl_present) {
        sb_text(&sb, L"S:");
        if (control)
            sb_control(&sb, control, KIND_SACL);
        error = acl_text(sacl, KIND_SACL, context, &sb);
    }
    counted = sb.Length + (dacl_present ? (dacl ? dacl->AceCount : 1) : 0) + (sacl_present ? (sacl ? sacl->AceCount : 1) : 0);
    AdvFree(filtered);
    PWSTR text = sb_take(&sb);
    if (!error && !text)
        error = ERROR_NOT_ENOUGH_MEMORY;
    if (error) {
        if (text)
            AdvFree(text);
        return error;
    }
    PWSTR out = LocalAlloc(LMEM_FIXED, (sb.Length + 1) * sizeof(WCHAR));
    if (!out) {
        AdvFree(text);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    memcpy(out, text, (sb.Length + 1) * sizeof(WCHAR));
    AdvFree(text);
    *result = out;
    if (length)
        *length = counted;
    return ERROR_SUCCESS;
}

/* ---------------------------------------------------------------- exports */

static BOOL finish(DWORD error)
{
    AdvSetError(error);
    return error == ERROR_SUCCESS;
}

ADVAPI BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorW(LPCWSTR text, DWORD revision,
                                                                       PSECURITY_DESCRIPTOR *result, PULONG size)
{
    if (!text || !result)
        return finish(ERROR_INVALID_PARAMETER);
    if (revision != SECURITY_DESCRIPTOR_REVISION)
        return finish(ERROR_UNKNOWN_REVISION);
    if (size)
        *size = 0;
    return finish(sd_from_text(text, nullptr, result, size));
}

ADVAPI BOOL WINAPI ConvertStringSecurityDescriptorToSecurityDescriptorA(LPCSTR text, DWORD revision,
                                                                       PSECURITY_DESCRIPTOR *result, PULONG size)
{
    if (!text || !result)
        return finish(ERROR_INVALID_PARAMETER);
    PWSTR wide = AdvAnsiToWide(text);
    if (!wide)
        return FALSE;
    BOOL ok = ConvertStringSecurityDescriptorToSecurityDescriptorW(wide, revision, result, size);
    AdvFree(wide);
    return ok;
}

/* the domain aware forms: the first sid is the domain, the second is optional and the root of the forest. */
ADVAPI BOOL WINAPI ConvertStringSDToSDDomainW(PSID domain, PSID root, LPCWSTR text, DWORD revision,
                                              PSECURITY_DESCRIPTOR *result, PULONG size)
{
    if (!text || !result || !RtlValidSid(domain) || (root && !RtlValidSid(root)))
        return finish(ERROR_INVALID_PARAMETER);
    if (revision != SECURITY_DESCRIPTOR_REVISION)
        return finish(ERROR_UNKNOWN_REVISION);
    SDDL_CONTEXT context = {domain, root};
    if (size)
        *size = 0;
    return finish(sd_from_text(text, &context, result, size));
}

ADVAPI BOOL WINAPI ConvertStringSDToSDDomainA(PSID domain, PSID root, LPCSTR text, DWORD revision,
                                              PSECURITY_DESCRIPTOR *result, PULONG size)
{
    if (!text || !result)
        return finish(ERROR_INVALID_PARAMETER);
    PWSTR wide = AdvAnsiToWide(text);
    if (!wide)
        return FALSE;
    BOOL ok = ConvertStringSDToSDDomainW(domain, root, wide, revision, result, size);
    AdvFree(wide);
    return ok;
}

ADVAPI BOOL WINAPI ConvertStringSDToSDRootDomainW(PSID root, LPCWSTR text, DWORD revision,
                                                  PSECURITY_DESCRIPTOR *result, PULONG size)
{
    if (!text || !result)
        return finish(ERROR_INVALID_PARAMETER);
    if (revision != SECURITY_DESCRIPTOR_REVISION)
        return finish(ERROR_UNKNOWN_REVISION);
    SDDL_CONTEXT context = {nullptr, root};
    if (size)
        *size = 0;
    return finish(sd_from_text(text, &context, result, size));
}

ADVAPI BOOL WINAPI ConvertStringSDToSDRootDomainA(PSID root, LPCSTR text, DWORD revision,
                                                  PSECURITY_DESCRIPTOR *result, PULONG size)
{
    if (!text || !result)
        return finish(ERROR_INVALID_PARAMETER);
    PWSTR wide = AdvAnsiToWide(text);
    if (!wide)
        return FALSE;
    BOOL ok = ConvertStringSDToSDRootDomainW(root, wide, revision, result, size);
    AdvFree(wide);
    return ok;
}

ADVAPI BOOL WINAPI ConvertSecurityDescriptorToStringSecurityDescriptorW(PSECURITY_DESCRIPTOR sd, DWORD revision,
                                                                       SECURITY_INFORMATION info, LPWSTR *text,
                                                                       PULONG length)
{
    if (!sd || !info) {
        /* no descriptor or nothing asked of it is the empty string, if a place for it was given. */
        if (!text)
            return finish(ERROR_INVALID_PARAMETER);
        PWSTR empty = LocalAlloc(LMEM_FIXED, sizeof(WCHAR));
        if (!empty)
            return finish(ERROR_NOT_ENOUGH_MEMORY);
        *empty = 0;
        *text = empty;
        if (length)
            *length = 0;
        return finish(ERROR_SUCCESS);
    }
    if (!text)
        return finish(ERROR_INVALID_PARAMETER);
    if (revision != SECURITY_DESCRIPTOR_REVISION)
        return finish(ERROR_UNKNOWN_REVISION);
    return finish(sd_to_text(sd, info, nullptr, text, length));
}

ADVAPI BOOL WINAPI ConvertSecurityDescriptorToStringSecurityDescriptorA(PSECURITY_DESCRIPTOR sd, DWORD revision,
                                                                       SECURITY_INFORMATION info, LPSTR *text,
                                                                       PULONG length)
{
    PWSTR wide;
    ULONG chars = 0;
    if (!text)
        return finish(ERROR_INVALID_PARAMETER);
    if (!ConvertSecurityDescriptorToStringSecurityDescriptorW(sd, revision, info, &wide, &chars))
        return FALSE;
    int bytes = WideCharToMultiByte(CP_ACP, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    PSTR out = bytes > 0 ? LocalAlloc(LMEM_FIXED, bytes) : nullptr;
    if (!out) {
        LocalFree(wide);
        return finish(ERROR_NOT_ENOUGH_MEMORY);
    }
    WideCharToMultiByte(CP_ACP, 0, wide, -1, out, bytes, nullptr, nullptr);
    LocalFree(wide);
    *text = out;
    if (length)
        *length = chars;
    return finish(ERROR_SUCCESS);
}

ADVAPI BOOL WINAPI ConvertSDToStringSDDomainW(PSID domain, PSID root, PSECURITY_DESCRIPTOR sd, DWORD revision,
                                              SECURITY_INFORMATION info, LPWSTR *text, PULONG length)
{
    if (!sd || !text || !info)
        return finish(ERROR_INVALID_PARAMETER);
    if (revision != SECURITY_DESCRIPTOR_REVISION)
        return finish(ERROR_UNKNOWN_REVISION);
    if ((domain && !RtlValidSid(domain)) || (root && !RtlValidSid(root)))
        return finish(ERROR_INVALID_SID);
    SDDL_CONTEXT context = {domain, root};
    return finish(sd_to_text(sd, info, &context, text, length));
}

ADVAPI BOOL WINAPI ConvertSDToStringSDRootDomainW(PSID root, PSECURITY_DESCRIPTOR sd, DWORD revision,
                                                  SECURITY_INFORMATION info, LPWSTR *text, PULONG length)
{
    if (!sd || !text || !info)
        return finish(ERROR_INVALID_PARAMETER);
    if (revision != SECURITY_DESCRIPTOR_REVISION)
        return finish(ERROR_UNKNOWN_REVISION);
    if (root && !RtlValidSid(root))
        return finish(ERROR_INVALID_SID);
    SDDL_CONTEXT context = {nullptr, root};
    return finish(sd_to_text(sd, info, &context, text, length));
}

ADVAPI BOOL WINAPI ConvertSDToStringSDRootDomainA(PSID root, PSECURITY_DESCRIPTOR sd, DWORD revision,
                                                  SECURITY_INFORMATION info, LPSTR *text, PULONG length)
{
    PWSTR wide;
    ULONG chars = 0;
    if (!text)
        return finish(ERROR_INVALID_PARAMETER);
    if (!ConvertSDToStringSDRootDomainW(root, sd, revision, info, &wide, &chars))
        return FALSE;
    int bytes = WideCharToMultiByte(CP_ACP, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    PSTR out = bytes > 0 ? LocalAlloc(LMEM_FIXED, bytes) : nullptr;
    if (!out) {
        LocalFree(wide);
        return finish(ERROR_NOT_ENOUGH_MEMORY);
    }
    WideCharToMultiByte(CP_ACP, 0, wide, -1, out, bytes, nullptr, nullptr);
    LocalFree(wide);
    *text = out;
    if (length)
        *length = chars;
    return finish(ERROR_SUCCESS);
}
