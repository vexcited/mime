/*
 * __unDName: the undecorated form of a name the microsoft compilers
 * decorate, as UnDecorateSymbolName prints it. covers qualified names
 * and their back references, templates, operators, constructors and
 * destructors, the special names of vtables and type information,
 * functions with their access, calling convention and signature,
 * variables, and types, including pointers to functions and members.
 * a name it cannot read is returned as it was given.
 */
#include "msvcrt_private.h"

/* the flags of UnDecorateSymbolName, the ones honoured here. */
#define UNDNAME_NO_MS_KEYWORDS 0x0002
#define UNDNAME_NO_FUNCTION_RETURNS 0x0004
#define UNDNAME_NO_ACCESS_SPECIFIERS 0x0080
#define UNDNAME_NO_MEMBER_TYPE 0x0200
#define UNDNAME_NAME_ONLY 0x1000
#define UNDNAME_NO_ARGUMENTS 0x2000
#define UNDNAME_NO_PTR64 0x20000

typedef struct {
    char *p;
    size_t length, capacity;
} BUF;

typedef struct {
    const char *in;
    BOOL failed;
    unsigned flags;
    /* the back references: up to ten names and ten argument types. */
    char *names[10];
    int name_count;
    char *types[10];
    int type_count;
} UND;

static char *dup_n(const char *s, size_t n)
{
    char *r = malloc(n + 1);
    if (r) {
        memcpy(r, s, n);
        r[n] = 0;
    }
    return r;
}

static char *dup(const char *s) { return dup_n(s, strlen(s)); }

static void put(BUF *b, const char *s)
{
    size_t n = strlen(s);
    if (b->length + n + 1 > b->capacity) {
        size_t capacity = (b->length + n + 1) * 2;
        char *p = realloc(b->p, capacity);
        if (!p)
            return;
        b->p = p;
        b->capacity = capacity;
    }
    memcpy(b->p + b->length, s, n + 1);
    b->length += n;
}

static char *take(BUF *b) { return b->p ? b->p : dup(""); }

static char *cat3(const char *a, const char *b, const char *c)
{
    BUF r = {0};
    put(&r, a);
    put(&r, b);
    put(&r, c);
    return take(&r);
}

static void fail(UND *u) { u->failed = TRUE; }

static char *type(UND *u);
static char *qualified(UND *u);
static char *function_type(UND *u, const char *inner, BOOL member);

/* ------------------------------------------------------------------ names */

static const char *operator_name(char c, BOOL underscore)
{
    static const char *const PLAIN[] = {
        ['2'] = "operator new",    ['3'] = "operator delete", ['4'] = "operator=",   ['5'] = "operator>>",
        ['6'] = "operator<<",      ['7'] = "operator!",       ['8'] = "operator==",  ['9'] = "operator!=",
        ['A'] = "operator[]",      ['C'] = "operator->",      ['D'] = "operator*",   ['E'] = "operator++",
        ['F'] = "operator--",      ['G'] = "operator-",       ['H'] = "operator+",   ['I'] = "operator&",
        ['J'] = "operator->*",     ['K'] = "operator/",       ['L'] = "operator%",   ['M'] = "operator<",
        ['N'] = "operator<=",      ['O'] = "operator>",       ['P'] = "operator>=",  ['Q'] = "operator,",
        ['R'] = "operator()",      ['S'] = "operator~",       ['T'] = "operator^",   ['U'] = "operator|",
        ['V'] = "operator&&",      ['W'] = "operator||",      ['X'] = "operator*=",  ['Y'] = "operator+=",
        ['Z'] = "operator-=",
    };
    static const char *const UNDERSCORE[] = {
        ['0'] = "operator/=",
        ['1'] = "operator%=",
        ['2'] = "operator>>=",
        ['3'] = "operator<<=",
        ['4'] = "operator&=",
        ['5'] = "operator|=",
        ['6'] = "operator^=",
        ['7'] = "`vftable'",
        ['8'] = "`vbtable'",
        ['9'] = "`vcall'",
        ['A'] = "`typeof'",
        ['B'] = "`local static guard'",
        ['D'] = "`vbase destructor'",
        ['E'] = "`vector deleting destructor'",
        ['F'] = "`default constructor closure'",
        ['G'] = "`scalar deleting destructor'",
        ['H'] = "`vector constructor iterator'",
        ['I'] = "`vector destructor iterator'",
        ['J'] = "`vector vbase constructor iterator'",
        ['K'] = "`virtual displacement map'",
        ['L'] = "`eh vector constructor iterator'",
        ['M'] = "`eh vector destructor iterator'",
        ['N'] = "`eh vector vbase constructor iterator'",
        ['O'] = "`copy constructor closure'",
        ['S'] = "`local vftable'",
        ['T'] = "`local vftable constructor closure'",
        ['U'] = "operator new[]",
        ['V'] = "operator delete[]",
        ['X'] = "`placement delete closure'",
        ['Y'] = "`placement delete[] closure'",
    };
    const char *const *table = underscore ? UNDERSCORE : PLAIN;
    size_t size = underscore ? sizeof(UNDERSCORE) / sizeof(*UNDERSCORE) : sizeof(PLAIN) / sizeof(*PLAIN);
    return (unsigned char)c < size ? table[(unsigned char)c] : NULL;
}

static char *remember_name(UND *u, char *name)
{
    if (name && u->name_count < 10)
        u->names[u->name_count++] = dup(name);
    return name;
}

/* an identifier up to its @. */
static char *identifier(UND *u)
{
    const char *end = strchr(u->in, '@');
    if (!end) {
        fail(u);
        return NULL;
    }
    char *r = dup_n(u->in, end - u->in);
    u->in = end + 1;
    return r;
}

/* a template's arguments, types up to @, in angle brackets. */
static char *template_arguments(UND *u)
{
    BUF r = {0};
    put(&r, "<");
    BOOL first = TRUE;
    while (*u->in && *u->in != '@' && !u->failed) {
        char *arg;
        if (u->in[0] == '$' && u->in[1] == '0') {
            /* an integer constant. */
            u->in += 2;
            BOOL negative = *u->in == '?';
            if (negative)
                u->in++;
            unsigned long long v = 0;
            if (*u->in >= '0' && *u->in <= '9') {
                v = *u->in++ - '0' + 1;
            } else {
                while (*u->in >= 'A' && *u->in <= 'P')
                    v = v * 16 + (*u->in++ - 'A');
                if (*u->in++ != '@')
                    fail(u);
            }
            char text[24];
            int n = 0;
            char digits[24];
            do
                digits[n++] = (char)('0' + v % 10);
            while ((v /= 10));
            int k = 0;
            if (negative)
                text[k++] = '-';
            while (n)
                text[k++] = digits[--n];
            text[k] = 0;
            arg = dup(text);
        } else {
            arg = type(u);
        }
        if (!first)
            put(&r, ",");
        put(&r, arg ? arg : "");
        free(arg);
        first = FALSE;
    }
    if (*u->in == '@')
        u->in++;
    put(&r, r.p && r.p[r.length - 1] == '>' ? " >" : ">");
    return take(&r);
}

/* one fragment of a qualified name: an identifier, a back reference, a template or a special name. */
static char *fragment(UND *u, BOOL first, char **special)
{
    char c = *u->in;
    if (c >= '0' && c <= '9') {
        u->in++;
        return c - '0' < u->name_count ? dup(u->names[c - '0']) : (fail(u), (char *)NULL);
    }
    if (c == '?' && u->in[1] == '$') {
        /* a template has its own back references. */
        u->in += 2;
        UND saved = *u;
        u->name_count = 0;
        u->type_count = 0;
        char *name = first && *u->in == '?' ? NULL : identifier(u);
        if (!name && first) {
            u->in++;
            const char *op = operator_name(*u->in == '_' ? u->in[1] : *u->in, *u->in == '_');
            u->in += *u->in == '_' ? 2 : 1;
            name = dup(op ? op : "");
        }
        char *args = template_arguments(u);
        for (int i = 0; i < u->name_count; i++)
            free(u->names[i]);
        for (int i = 0; i < u->type_count; i++)
            free(u->types[i]);
        BOOL failed = u->failed;
        const char *in = u->in;
        *u = saved;
        u->in = in;
        u->failed = failed;
        char *r = cat3(name ? name : "", args, "");
        free(name);
        free(args);
        return remember_name(u, r);
    }
    if (c == '?' && first) {
        u->in++;
        char code = *u->in++;
        if (code == '0' || code == '1') {
            *special = code == '0' ? dup("") : dup("~");
            return NULL;
        }
        BOOL underscore = code == '_';
        if (underscore)
            code = *u->in++;
        if (underscore && code == 'R') {
            /* the records of run time type information. */
            char kind = *u->in++;
            switch (kind) {
            case '0': {
                char *t = type(u);
                char *r = cat3(t ? t : "", " `RTTI Type Descriptor'", "");
                free(t);
                *special = r;
                return NULL;
            }
            case '1':
                *special = dup("`RTTI Base Class Descriptor at (...)'");
                while (*u->in && *u->in != '@' && !(u->in[0] >= 'A' && u->in[0] <= 'Z' && u->in[1] == '@'))
                    u->in++;
                return NULL;
            case '2':
                *special = dup("`RTTI Base Class Array'");
                return NULL;
            case '3':
                *special = dup("`RTTI Class Hierarchy Descriptor'");
                return NULL;
            case '4':
                *special = dup("`RTTI Complete Object Locator'");
                return NULL;
            }
            fail(u);
            return NULL;
        }
        const char *op = operator_name(code, underscore);
        if (!op) {
            fail(u);
            return NULL;
        }
        *special = dup(op);
        return NULL;
    }
    return remember_name(u, identifier(u));
}

/* a qualified name: its fragments innermost first, joined outermost first with ::. */
static char *qualified_name(UND *u, char **special)
{
    char *parts[32];
    int count = 0;
    *special = NULL;
    BOOL first = TRUE;
    while (*u->in && *u->in != '@' && !u->failed && count < 32) {
        char *f = fragment(u, first, first ? special : &(char *){NULL});
        if (f)
            parts[count++] = f;
        first = FALSE;
    }
    if (*u->in == '@')
        u->in++;
    else
        fail(u);
    BUF r = {0};
    for (int i = count - 1; i >= 0; i--) {
        put(&r, parts[i]);
        if (i)
            put(&r, "::");
    }
    if (*special) {
        /* constructors and destructors take the name of their class. */
        if (count) {
            if (r.length)
                put(&r, "::");
            put(&r, *special);
            if (!strcmp(*special, "") || !strcmp(*special, "~")) {
                char *cls = parts[0];
                const char *angle = strchr(cls, '<');
                char *base = angle ? dup_n(cls, angle - cls) : dup(cls);
                put(&r, base);
                free(base);
            }
        } else {
            put(&r, *special);
        }
    }
    for (int i = 0; i < count; i++)
        free(parts[i]);
    return take(&r);
}

static char *qualified(UND *u)
{
    char *special;
    char *r = qualified_name(u, &special);
    free(special);
    return r;
}

/* ------------------------------------------------------------------ types */

static const char *basic_type(char c)
{
    switch (c) {
    case 'C':
        return "signed char";
    case 'D':
        return "char";
    case 'E':
        return "unsigned char";
    case 'F':
        return "short";
    case 'G':
        return "unsigned short";
    case 'H':
        return "int";
    case 'I':
        return "unsigned int";
    case 'J':
        return "long";
    case 'K':
        return "unsigned long";
    case 'M':
        return "float";
    case 'N':
        return "double";
    case 'O':
        return "long double";
    case 'X':
        return "void";
    case 'Z':
        return "...";
    }
    return NULL;
}

static const char *extended_type(char c)
{
    switch (c) {
    case 'D':
        return "__int8";
    case 'E':
        return "unsigned __int8";
    case 'F':
        return "__int16";
    case 'G':
        return "unsigned __int16";
    case 'H':
        return "__int32";
    case 'I':
        return "unsigned __int32";
    case 'J':
        return "__int64";
    case 'K':
        return "unsigned __int64";
    case 'L':
        return "__int128";
    case 'M':
        return "unsigned __int128";
    case 'N':
        return "bool";
    case 'Q':
        return "char8_t";
    case 'S':
        return "char16_t";
    case 'U':
        return "char32_t";
    case 'W':
        return "wchar_t";
    }
    return NULL;
}

static const char *cv_name(char c)
{
    switch (c) {
    case 'B':
    case 'J':
        return "const ";
    case 'C':
    case 'K':
        return "volatile ";
    case 'D':
    case 'L':
        return "const volatile ";
    }
    return "";
}

/* what a pointer or reference points at, its own qualifier after the star given. */
static char *pointer_type(UND *u, const char *star)
{
    BUF suffix = {0};
    put(&suffix, star);
    for (;;) {
        if (*u->in == 'E') {
            if (!(u->flags & UNDNAME_NO_PTR64))
                put(&suffix, " __ptr64");
            u->in++;
        } else if (*u->in == 'F') {
            put(&suffix, " __unaligned");
            u->in++;
        } else if (*u->in == 'I') {
            put(&suffix, " __restrict");
            u->in++;
        } else {
            break;
        }
    }
    char *s = take(&suffix);
    char *r;
    if (*u->in == '6' || *u->in == '8') {
        BOOL member = *u->in == '8';
        u->in++;
        char *cls = member ? qualified(u) : NULL;
        char *inner = member ? cat3(cls ? cls : "", "::", s) : dup(s);
        if (member && *u->in)
            u->in++; /* the this qualifier */
        r = function_type(u, inner, member);
        free(cls);
        free(inner);
    } else {
        /* the qualifier of the target follows its type; a pointer target carries its own. */
        const char *cv = cv_name(*u->in);
        if (*u->in)
            u->in++;
        BOOL pointer = *u->in >= 'P' && *u->in <= 'S';
        char *target = type(u);
        BUF b = {0};
        put(&b, target ? target : "");
        if (*cv && !pointer) {
            put(&b, " ");
            put(&b, cv);
            b.p[--b.length] = 0;
        }
        put(&b, " ");
        put(&b, s);
        free(target);
        r = take(&b);
    }
    free(s);
    return r;
}

static char *type(UND *u)
{
    char c = *u->in;
    if (!c) {
        fail(u);
        return NULL;
    }
    if (c >= '0' && c <= '9') {
        u->in++;
        return c - '0' < u->type_count ? dup(u->types[c - '0']) : (fail(u), (char *)NULL);
    }
    u->in++;
    const char *basic = basic_type(c);
    if (basic)
        return dup(basic);
    char *r = NULL;
    switch (c) {
    case '_': {
        const char *e = extended_type(*u->in);
        if (!e) {
            fail(u);
            return NULL;
        }
        u->in++;
        r = dup(e);
        break;
    }
    case 'T':
    case 'U':
    case 'V': {
        char *n = qualified(u);
        r = cat3(c == 'T' ? "union " : c == 'U' ? "struct " : "class ", n ? n : "", "");
        free(n);
        break;
    }
    case 'W': {
        if (*u->in)
            u->in++;
        char *n = qualified(u);
        r = cat3("enum ", n ? n : "", "");
        free(n);
        break;
    }
    case 'P':
    case 'Q':
    case 'R':
    case 'S': {
        static const char *const STAR[] = {"*", "* const", "* volatile", "* const volatile"};
        r = pointer_type(u, STAR[c - 'P']);
        break;
    }
    case 'A':
        r = pointer_type(u, "&");
        break;
    case 'B':
        r = pointer_type(u, "& volatile");
        break;
    case '$':
        if (u->in[0] == '$' && (u->in[1] == 'Q' || u->in[1] == 'R')) {
            u->in += 2;
            r = pointer_type(u, "&&");
        } else if (u->in[0] == '$' && u->in[1] == 'C') {
            u->in += 2;
            char cv[16];
            strcpy(cv, cv_name(*u->in));
            u->in++;
            char *t = type(u);
            if (*cv)
                cv[strlen(cv) - 1] = 0;
            r = cat3(t ? t : "", *cv ? " " : "", cv);
            free(t);
        } else if (u->in[0] == '$' && u->in[1] == 'T') {
            u->in += 2;
            r = dup("std::nullptr_t");
        } else {
            fail(u);
        }
        break;
    case 'Y': {
        /* an array: its dimensions, then the element type. */
        int dims = *u->in >= '0' && *u->in <= '9' ? *u->in++ - '0' + 1 : 0;
        BUF b = {0};
        for (int i = 0; i < dims; i++) {
            char n[4] = {'[', *u->in >= '0' && *u->in <= '9' ? (char)(*u->in + 1) : '?', ']', 0};
            u->in++;
            put(&b, n);
        }
        char *element = type(u);
        r = cat3(element ? element : "", " ", b.p ? b.p : "");
        free(element);
        free(b.p);
        break;
    }
    default:
        fail(u);
        return NULL;
    }
    return r;
}

/* a type as a function argument, remembered for back references when longer than one letter. */
static char *argument(UND *u)
{
    const char *start = u->in;
    char *t = type(u);
    if (t && u->in - start > 1 && u->type_count < 10)
        u->types[u->type_count++] = dup(t);
    return t;
}

static char *arguments(UND *u)
{
    BUF r = {0};
    put(&r, "(");
    if (*u->in == 'X') {
        u->in++;
        put(&r, "void");
    } else {
        BOOL first = TRUE;
        while (*u->in && *u->in != '@' && *u->in != 'Z' && !u->failed) {
            char *a = argument(u);
            if (!first)
                put(&r, ",");
            put(&r, a ? a : "");
            free(a);
            first = FALSE;
        }
        if (*u->in == 'Z') {
            u->in++;
            put(&r, first ? "..." : ",...");
        } else if (*u->in == '@') {
            u->in++;
        }
    }
    put(&r, ")");
    return take(&r);
}

static const char *calling_convention(char c)
{
    switch (c) {
    case 'A':
    case 'B':
        return "__cdecl";
    case 'C':
    case 'D':
        return "__pascal";
    case 'E':
    case 'F':
        return "__thiscall";
    case 'G':
    case 'H':
        return "__stdcall";
    case 'I':
    case 'J':
        return "__fastcall";
    case 'Q':
        return "__vectorcall";
    }
    return NULL;
}

/* a function type around inner, a pointer's star or nothing: return type, convention, arguments. */
static char *function_type(UND *u, const char *inner, BOOL member)
{
    const char *cc = calling_convention(*u->in);
    if (!cc) {
        fail(u);
        return NULL;
    }
    u->in++;
    char *ret = *u->in == '@' ? (u->in++, dup("")) : type(u);
    char *args = arguments(u);
    if (*u->in == 'Z')
        u->in++;
    BUF r = {0};
    put(&r, ret ? ret : "");
    put(&r, " (");
    if (!(u->flags & UNDNAME_NO_MS_KEYWORDS)) {
        put(&r, cc);
        if (*inner != '*')
            put(&r, " ");
    }
    put(&r, inner);
    put(&r, ")");
    put(&r, args ? args : "");
    free(ret);
    free(args);
    return take(&r);
}

/* ------------------------------------------------------------------ symbols */

static char *symbol(UND *u)
{
    if (*u->in == '.') {
        /* a type's name, as type information keeps it: ?A and its qualifier before classes. */
        u->in++;
        if (*u->in == '?' && u->in[1])
            u->in += 2;
        return type(u);
    }
    if (*u->in != '?')
        return NULL;
    u->in++;
    char *special;
    char *name = qualified_name(u, &special);
    if (u->failed)
        goto done;
    if (u->flags & UNDNAME_NAME_ONLY) {
        free(special);
        return name;
    }
    char c = *u->in;
    if (!c) {
        free(special);
        return name;
    }
    BUF r = {0};
    if (c >= '0' && c <= '4') {
        /* a variable: static members and globals, its type and storage class. */
        u->in++;
        static const char *const ACCESS[] = {"private: static ", "protected: static ", "public: static ", "", ""};
        char *t = type(u);
        const char *cv = cv_name(*u->in);
        if (*u->in == 'E')
            u->in++;
        if (*u->in)
            u->in++;
        if (!(u->flags & UNDNAME_NO_ACCESS_SPECIFIERS))
            put(&r, ACCESS[c - '0']);
        put(&r, t ? t : "");
        put(&r, " ");
        put(&r, cv);
        put(&r, name);
        free(t);
    } else if (c == '6' || c == '7') {
        /* a vtable or vbtable: const, for the class named after it. */
        u->in++;
        const char *cv = cv_name(*u->in);
        if (*u->in)
            u->in++;
        put(&r, cv);
        put(&r, name);
        if (*u->in && *u->in != '@') {
            char *of = qualified(u);
            put(&r, "{for `");
            put(&r, of ? of : "");
            put(&r, "'}");
            free(of);
        }
    } else if (c == '8') {
        put(&r, name);
    } else {
        /* a function: access and kind, then its type. */
        u->in++;
        const char *access = "";
        BOOL member = TRUE, is_static = FALSE, is_virtual = FALSE;
        if (c >= 'A' && c <= 'X') {
            int kind = (c - 'A') / 2;
            static const char *const ACCESS[] = {"private: ", "private: ", "private: ", "private: ",
                                                 "protected: ", "protected: ", "protected: ", "protected: ",
                                                 "public: ", "public: ", "public: ", "public: "};
            access = ACCESS[kind];
            is_static = kind % 4 == 1;
            is_virtual = kind % 4 == 2;
        } else if (c == 'Y' || c == 'Z') {
            member = FALSE;
        } else {
            fail(u);
            goto done;
        }
        const char *this_cv = "";
        BOOL this_ptr64 = FALSE;
        if (member && !is_static) {
            if (*u->in == 'E') {
                this_ptr64 = TRUE;
                u->in++;
            }
            this_cv = cv_name(*u->in);
            u->in++;
        }
        const char *cc = calling_convention(*u->in);
        if (!cc) {
            fail(u);
            goto done;
        }
        u->in++;
        char *ret = *u->in == '@' ? (u->in++, NULL) : type(u);
        char *args = arguments(u);
        if (!(u->flags & UNDNAME_NO_ACCESS_SPECIFIERS))
            put(&r, access);
        if (!(u->flags & UNDNAME_NO_MEMBER_TYPE)) {
            if (is_static)
                put(&r, "static ");
            if (is_virtual)
                put(&r, "virtual ");
        }
        if (ret && !(u->flags & UNDNAME_NO_FUNCTION_RETURNS)) {
            put(&r, ret);
            put(&r, " ");
        }
        if (!(u->flags & UNDNAME_NO_MS_KEYWORDS)) {
            put(&r, cc);
            put(&r, " ");
        }
        put(&r, name);
        if (!(u->flags & UNDNAME_NO_ARGUMENTS)) {
            put(&r, args ? args : "");
            if (*this_cv) {
                put(&r, this_cv);
                r.p[--r.length] = 0;
            }
            if (this_ptr64 && !(u->flags & UNDNAME_NO_PTR64))
                put(&r, " __ptr64");
        }
        free(ret);
        free(args);
    }
    free(special);
    free(name);
    return take(&r);
done:
    free(special);
    free(name);
    return NULL;
}

static char *undecorate(const char *decorated, unsigned flags)
{
    UND u;
    memset(&u, 0, sizeof(u));
    u.in = decorated;
    u.flags = flags;
    char *r = symbol(&u);
    for (int i = 0; i < u.name_count; i++)
        free(u.names[i]);
    for (int i = 0; i < u.type_count; i++)
        free(u.types[i]);
    if (u.failed) {
        free(r);
        return NULL;
    }
    return r;
}

char *MsvcrtUndecorateType(const char *decorated)
{
    char *r = undecorate(decorated, 0);
    return r ? r : dup(decorated);
}

typedef void *(__cdecl *und_alloc_t)(size_t);
typedef void(__cdecl *und_free_t)(void *);

/* into buffer when given, else into memory of the caller's allocator; a name not understood comes back as given. */
char *__cdecl __unDNameEx(char *buffer, const char *decorated, int length, und_alloc_t alloc, und_free_t release,
                          void *get_parameter, unsigned short flags)
{
    if (!decorated || !alloc)
        return NULL;
    char *r = undecorate(decorated, flags);
    const char *text = r ? r : decorated;
    size_t n = strlen(text);
    if (!buffer) {
        buffer = alloc(n + 1);
        length = (int)(n + 1);
    }
    if (buffer && length > 0) {
        size_t copy = n < (size_t)length - 1 ? n : (size_t)length - 1;
        memcpy(buffer, text, copy);
        buffer[copy] = 0;
    }
    free(r);
    return buffer;
}

char *__cdecl __unDName(char *buffer, const char *decorated, int length, und_alloc_t alloc, und_free_t release,
                        unsigned short flags)
{
    return __unDNameEx(buffer, decorated, length, alloc, release, NULL, flags);
}
