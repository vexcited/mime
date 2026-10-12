/*
 * character classification and case mapping of the c locale.
 */
#include "msvcrt_private.h"

/* msvcrt's _ctype: the entry before character 0 classifies EOF. */
const unsigned short MsvcrtCtype[257] = {
    0x000, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x028, 0x028, 0x028, 0x028, 0x028, 0x020,
    0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020,
    0x020, 0x048, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010,
    0x010, 0x084, 0x084, 0x084, 0x084, 0x084, 0x084, 0x084, 0x084, 0x084, 0x084, 0x010, 0x010, 0x010, 0x010, 0x010,
    0x010, 0x010, 0x081, 0x081, 0x081, 0x081, 0x081, 0x081, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001,
    0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x001, 0x010, 0x010, 0x010, 0x010,
    0x010, 0x010, 0x082, 0x082, 0x082, 0x082, 0x082, 0x082, 0x002, 0x002, 0x002, 0x002, 0x002, 0x002, 0x002, 0x002,
    0x002, 0x002, 0x002, 0x002, 0x002, 0x002, 0x002, 0x002, 0x002, 0x002, 0x002, 0x002, 0x010, 0x010, 0x010, 0x010,
    0x020, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
    0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000, 0x000,
    0x000,
};

/* msvcrt's _wctype for the first 256 characters, latin-1 included. */
const unsigned short MsvcrtWctypeTable[256] = {
    0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x068, 0x028, 0x028, 0x028, 0x028, 0x020, 0x020,
    0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020,
    0x048, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010,
    0x084, 0x084, 0x084, 0x084, 0x084, 0x084, 0x084, 0x084, 0x084, 0x084, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010,
    0x010, 0x181, 0x181, 0x181, 0x181, 0x181, 0x181, 0x101, 0x101, 0x101, 0x101, 0x101, 0x101, 0x101, 0x101, 0x101,
    0x101, 0x101, 0x101, 0x101, 0x101, 0x101, 0x101, 0x101, 0x101, 0x101, 0x101, 0x010, 0x010, 0x010, 0x010, 0x010,
    0x010, 0x182, 0x182, 0x182, 0x182, 0x182, 0x182, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102,
    0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x010, 0x010, 0x010, 0x010, 0x020,
    0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020,
    0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020, 0x020,
    0x048, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010,
    0x010, 0x010, 0x014, 0x014, 0x010, 0x010, 0x010, 0x010, 0x010, 0x014, 0x010, 0x010, 0x010, 0x010, 0x010, 0x010,
    0x101, 0x101, 0x101, 0x101, 0x101, 0x101, 0x101, 0x101, 0x101, 0x101, 0x101, 0x101, 0x101, 0x101, 0x101, 0x101,
    0x101, 0x101, 0x101, 0x101, 0x101, 0x101, 0x101, 0x010, 0x101, 0x101, 0x101, 0x101, 0x101, 0x101, 0x101, 0x102,
    0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102,
    0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x010, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102, 0x102,
};

const unsigned short *_pctype = MsvcrtCtype + 1;
const unsigned short *_pwctype = MsvcrtWctypeTable;
int __mb_cur_max = 1;

const unsigned short *__cdecl __pctype_func(void)
{
    return MsvcrtCtype + 1;
}

const unsigned short **__cdecl __p__pctype(void)
{
    return &_pctype;
}

const unsigned short *__cdecl __pwctype_func(void)
{
    return MsvcrtWctypeTable;
}

const unsigned short **__cdecl __p__pwctype(void)
{
    return &_pwctype;
}

int __cdecl ___mb_cur_max_func(void)
{
    return __mb_cur_max;
}

int *__cdecl __p___mb_cur_max(void)
{
    return &__mb_cur_max;
}

int __cdecl ___mb_cur_max_l_func(void *locale)
{
    return __mb_cur_max;
}

int __cdecl _isctype(int c, int mask)
{
    if (c < -1 || c > 255)
        return 0;
    return MsvcrtCtype[c + 1] & mask;
}

int __cdecl _isctype_l(int c, int mask, void *locale)
{
    return _isctype(c, mask);
}

#define CTYPE_FN(name, mask)                                                                                      \
    int __cdecl name(int c)                                                                                       \
    {                                                                                                             \
        return _isctype(c, mask);                                                                                 \
    }                                                                                                             \
    int __cdecl _##name##_l(int c, void *locale)                                                                  \
    {                                                                                                             \
        return _isctype(c, mask);                                                                                 \
    }

CTYPE_FN(isalpha, _ALPHA)
CTYPE_FN(isupper, _UPPER)
CTYPE_FN(islower, _LOWER)
CTYPE_FN(isdigit, _DIGIT)
CTYPE_FN(isxdigit, _HEX)
CTYPE_FN(isspace, _SPACE)
CTYPE_FN(ispunct, _PUNCT)
CTYPE_FN(isalnum, _ALPHA | _DIGIT)
CTYPE_FN(isprint, _BLANK | _PUNCT | _ALPHA | _DIGIT)
CTYPE_FN(isgraph, _PUNCT | _ALPHA | _DIGIT)
CTYPE_FN(iscntrl, _CONTROL)

int __cdecl isblank(int c)
{
    return c == '\t' ? _BLANK : _isctype(c, _BLANK);
}

int __cdecl __isascii(int c)
{
    return (unsigned)c < 0x80;
}

int __cdecl __toascii(int c)
{
    return c & 0x7f;
}

int __cdecl __iscsymf(int c)
{
    return isalpha(c) || c == '_';
}

int __cdecl __iscsym(int c)
{
    return isalnum(c) || c == '_';
}

int __cdecl isleadbyte(int c)
{
    return 0;
}

int __cdecl _isleadbyte_l(int c, void *locale)
{
    return 0;
}

int __cdecl toupper(int c)
{
    return MsvcrtToUpper(c);
}

int __cdecl tolower(int c)
{
    return MsvcrtToLower(c);
}

int __cdecl _toupper(int c)
{
    return c - 'a' + 'A';
}

int __cdecl _tolower(int c)
{
    return c - 'A' + 'a';
}

int __cdecl _toupper_l(int c, void *locale)
{
    return MsvcrtToUpper(c);
}

int __cdecl _tolower_l(int c, void *locale)
{
    return MsvcrtToLower(c);
}

/* ------------------------------------------------------------------ wide characters */

unsigned short MsvcrtWctype(wint_t c)
{
    if (c == WEOF)
        return 0;
    if (c < 256)
        return MsvcrtWctypeTable[c];
    WORD type = 0;
    wchar_t ch = c;
    if (!GetStringTypeW(CT_CTYPE1, &ch, 1, &type))
        return 0;
    return type;
}

int __cdecl iswctype(wint_t c, wctype_t mask)
{
    return MsvcrtWctype(c) & mask;
}

int __cdecl is_wctype(wint_t c, wctype_t mask)
{
    return iswctype(c, mask);
}

int __cdecl _iswctype_l(wint_t c, wctype_t mask, void *locale)
{
    return iswctype(c, mask);
}

#define WCTYPE_FN(name, mask)                                                                                     \
    int __cdecl name(wint_t c)                                                                                    \
    {                                                                                                             \
        return iswctype(c, mask);                                                                                 \
    }                                                                                                             \
    int __cdecl _##name##_l(wint_t c, void *locale)                                                               \
    {                                                                                                             \
        return iswctype(c, mask);                                                                                 \
    }

WCTYPE_FN(iswalpha, _ALPHA)
WCTYPE_FN(iswupper, _UPPER)
WCTYPE_FN(iswlower, _LOWER)
WCTYPE_FN(iswdigit, _DIGIT)
WCTYPE_FN(iswxdigit, _HEX)
WCTYPE_FN(iswspace, _SPACE)
WCTYPE_FN(iswpunct, _PUNCT)
WCTYPE_FN(iswalnum, _ALPHA | _DIGIT)
WCTYPE_FN(iswprint, _BLANK | _PUNCT | _ALPHA | _DIGIT)
WCTYPE_FN(iswgraph, _PUNCT | _ALPHA | _DIGIT)
WCTYPE_FN(iswcntrl, _CONTROL)

int __cdecl iswblank(wint_t c)
{
    return c == '\t' ? _BLANK : iswctype(c, _BLANK);
}

int __cdecl iswascii(wint_t c)
{
    return c < 0x80;
}

int __cdecl __iswcsymf(wint_t c)
{
    return iswalpha(c) || c == '_';
}

int __cdecl __iswcsym(wint_t c)
{
    return iswalnum(c) || c == '_';
}

/* the c locale maps only the ascii letters, like msvcrt. */
wchar_t __cdecl towupper(wchar_t c)
{
    return c >= 'a' && c <= 'z' ? c - 32 : c;
}

wchar_t __cdecl towlower(wchar_t c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

wint_t __cdecl _towupper_l(wint_t c, void *locale)
{
    return towupper(c);
}

wint_t __cdecl _towlower_l(wint_t c, void *locale)
{
    return towlower(c);
}

typedef struct {
    const char *name;
    wctype_t mask;
} WCTYPE_NAME;

static const WCTYPE_NAME MsvcrtWctypeTable_names[] = {
    {"alnum", _ALPHA | _DIGIT}, {"alpha", _ALPHA},   {"cntrl", _CONTROL},
    {"digit", _DIGIT},          {"graph", _PUNCT | _ALPHA | _DIGIT}, {"lower", _LOWER},
    {"print", _BLANK | _PUNCT | _ALPHA | _DIGIT}, {"punct", _PUNCT}, {"space", _SPACE},
    {"upper", _UPPER},          {"xdigit", _HEX},    {"blank", _BLANK},
};

wctype_t __cdecl wctype(const char *name)
{
    for (size_t i = 0; i < sizeof(MsvcrtWctypeTable_names) / sizeof(MsvcrtWctypeTable_names[0]); i++)
        if (!strcmp(name, MsvcrtWctypeTable_names[i].name))
            return MsvcrtWctypeTable_names[i].mask;
    return 0;
}
