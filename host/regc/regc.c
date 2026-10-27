/*
 * compiles the registry sources of data/registry into the hive files the ROM carries: SYSTEM, SOFTWARE, DEFAULT and the
 * NTUSER.DAT of the default user. a source is a text file in the syntax of .reg files with these additions.
 *
 * the roots are HKLM\SYSTEM, HKLM\SOFTWARE, HKU\.DEFAULT and HKCU. HKCR stands for HKLM\SOFTWARE\Classes and
 * CurrentControlSet for ControlSet001. a header may hold a list in braces, [HKLM\SOFTWARE{,\WOW6432Node}\Foo] is two
 * keys that get the same lines, and braces without a comma are a guid.
 *
 * a value is "name"="text", dword:, qword:, expand:"text", multi:"a","b", or hex: and a list of bytes, with a type number
 * after hex as .reg files write it. @ is the default value and "name"=- deletes a value. a backslash in a string is
 * written twice.
 *
 * #security and a descriptor string gives the keys of the header their descriptor, a key without one inherits from its
 * key. later lines win over earlier ones and the files are read in the order given. once they are read the 32-bit view of
 * the machine is made from the 64-bit one, see mirror_wow64.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../guest/ntos/cm/shared_keys.h"

#define REG_SZ 1
#define REG_EXPAND_SZ 2
#define REG_BINARY 3
#define REG_DWORD 4
#define REG_MULTI_SZ 7
#define REG_QWORD 11

#define MAX_NAME 255
#define MAX_VALUE_DATA 16000
#define BIN_SIZE 0x10000

static const char *g_file;
static int g_line;

static void fail(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    if (g_file)
        fprintf(stderr, "%s:%d: ", g_file, g_line);
    else
        fprintf(stderr, "regc: ");
    vfprintf(stderr, format, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}

static void *xrealloc(void *p, size_t size)
{
    p = realloc(p, size ? size : 1);
    if (!p)
        fail("out of memory");
    return p;
}

static char *xstrndup(const char *s, size_t n)
{
    char *p = xrealloc(nullptr, n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

/* ------------------------------------------------------------------ growing buffers */

typedef struct Buf {
    uint8_t *data;
    size_t size, capacity;
} Buf;

static void buf_put(Buf *b, const void *data, size_t n)
{
    if (b->size + n > b->capacity) {
        b->capacity = (b->size + n) * 2 + 64;
        b->data = xrealloc(b->data, b->capacity);
    }
    if (n)
        memcpy(b->data + b->size, data, n);
    b->size += n;
}

static void buf_u16(Buf *b, uint16_t v)
{
    uint8_t t[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    buf_put(b, t, 2);
}

static void put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, v);
    put16(p + 2, v >> 16);
}

static void put64(uint8_t *p, uint64_t v)
{
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

/* ------------------------------------------------------------------ text */

/* appends the utf-16 form of a utf-8 string. */
static void utf16_append(Buf *out, const char *s, size_t n)
{
    for (size_t i = 0; i < n;) {
        uint32_t c = (uint8_t)s[i];
        size_t extra = c < 0x80 ? 0 : c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3;
        if (c >= 0x80)
            c &= extra == 1 ? 0x1F : extra == 2 ? 0x0F : 0x07;
        if (c >= 0x80 && extra == 0)
            fail("invalid utf-8");
        i++;
        for (size_t k = 0; k < extra; k++, i++) {
            if (i >= n || (s[i] & 0xC0) != 0x80)
                fail("invalid utf-8");
            c = (c << 6) | ((uint8_t)s[i] & 0x3F);
        }
        if (c >= 0x10000) {
            c -= 0x10000;
            buf_u16(out, (uint16_t)(0xD800 + (c >> 10)));
            buf_u16(out, (uint16_t)(0xDC00 + (c & 0x3FF)));
        } else {
            buf_u16(out, (uint16_t)c);
        }
    }
}

static int upcase(int c)
{
    return c >= 'a' && c <= 'z' ? c - 32 : c;
}

static int name_compare(const char *a, const char *b)
{
    for (;; a++, b++) {
        int x = upcase((uint8_t)*a), y = upcase((uint8_t)*b);
        if (x != y)
            return x < y ? -1 : 1;
        if (!x)
            return 0;
    }
}

/* ------------------------------------------------------------------ security descriptors */

typedef struct Sid {
    uint8_t bytes[68];
    size_t size;
} Sid;

typedef struct Ace {
    uint8_t type, flags;
    uint32_t mask;
    Sid sid;
} Ace;

typedef struct Sd {
    Sid owner, group;
    Ace *aces;
    size_t count;
    bool protect;
} Sd;

#define ACE_CI 0x02
#define ACE_IO 0x08
#define ACE_NP 0x04
#define ACE_ID 0x10

static void sid_make(Sid *s, uint64_t authority, const uint32_t *subs, size_t count)
{
    s->bytes[0] = 1;
    s->bytes[1] = (uint8_t)count;
    for (int i = 0; i < 6; i++)
        s->bytes[2 + i] = (uint8_t)(authority >> (8 * (5 - i)));
    for (size_t i = 0; i < count; i++)
        put32(s->bytes + 8 + 4 * i, subs[i]);
    s->size = 8 + 4 * count;
}

static void sid_parse(Sid *s, const char *text, size_t n)
{
    static const struct {
        const char *alias;
        uint64_t authority;
        uint32_t subs[2];
        size_t count;
    } known[] = {
        {"SY", 5, {18}, 1},     {"BA", 5, {32, 544}, 2}, {"BU", 5, {32, 545}, 2}, {"BG", 5, {32, 546}, 2},
        {"PU", 5, {32, 547}, 2}, {"AU", 5, {11}, 1},      {"IU", 5, {4}, 1},       {"LS", 5, {19}, 1},
        {"NS", 5, {20}, 1},     {"RC", 5, {12}, 1},      {"SU", 5, {6}, 1},       {"WD", 1, {0}, 1},
        {"CO", 3, {0}, 1},      {"CG", 3, {1}, 1},       {"OW", 3, {4}, 1},       {"AN", 5, {7}, 1},
        {"NU", 5, {2}, 1},      {"SO", 5, {32, 548}, 2}, {"BO", 5, {32, 551}, 2}, {"RD", 5, {32, 555}, 2},
        {"AC", 15, {2, 1}, 2},
    };
    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        if (strlen(known[i].alias) == n && !memcmp(known[i].alias, text, n)) {
            sid_make(s, known[i].authority, known[i].subs, known[i].count);
            return;
        }
    }
    if (n < 5 || memcmp(text, "S-1-", 4))
        fail("unknown sid %.*s", (int)n, text);
    char *copy = xstrndup(text + 4, n - 4), *p = copy, *end;
    uint64_t authority = strtoull(p, &end, 10);
    uint32_t subs[15];
    size_t count = 0;
    while (*end == '-') {
        p = end + 1;
        if (count == 15)
            fail("too many sub authorities in %.*s", (int)n, text);
        subs[count++] = (uint32_t)strtoull(p, &end, 10);
    }
    if (*end)
        fail("bad sid %.*s", (int)n, text);
    sid_make(s, authority, subs, count);
    free(copy);
}

static uint32_t rights_parse(const char *s, size_t n)
{
    uint32_t mask = 0;
    if (n > 2 && s[0] == '0' && s[1] == 'x') {
        char *copy = xstrndup(s, n);
        mask = (uint32_t)strtoul(copy, nullptr, 16);
        free(copy);
        return mask;
    }
    static const struct {
        const char *code;
        uint32_t mask;
    } codes[] = {
        {"GA", 0x10000000}, {"GR", 0x80000000}, {"GW", 0x40000000}, {"GX", 0x20000000},
        {"KA", 0xF003F},    {"KR", 0x20019},    {"KW", 0x20006},    {"KX", 0x20019},
        {"RC", 0x20000},    {"SD", 0x10000},    {"WD", 0x40000},    {"WO", 0x80000},
    };
    for (size_t i = 0; i + 1 < n; i += 2) {
        size_t k = 0;
        for (; k < sizeof(codes) / sizeof(codes[0]); k++)
            if (!memcmp(codes[k].code, s + i, 2))
                break;
        if (k == sizeof(codes) / sizeof(codes[0]))
            fail("unknown right %.2s", s + i);
        mask |= codes[k].mask;
    }
    return mask;
}

static uint8_t ace_flags_parse(const char *s, size_t n)
{
    uint8_t flags = 0;
    for (size_t i = 0; i + 1 < n; i += 2) {
        if (!memcmp(s + i, "CI", 2))
            flags |= ACE_CI;
        else if (!memcmp(s + i, "OI", 2))
            flags |= 0x01;
        else if (!memcmp(s + i, "IO", 2))
            flags |= ACE_IO;
        else if (!memcmp(s + i, "NP", 2))
            flags |= ACE_NP;
        else if (!memcmp(s + i, "ID", 2))
            flags |= ACE_ID;
        else
            fail("unknown ace flag %.2s", s + i);
    }
    return flags;
}

/* the part of the descriptor string language the sources use: owner, group and one dacl of allow and deny entries. */
static Sd *sd_parse(const char *text)
{
    Sd *sd = xrealloc(nullptr, sizeof(*sd));
    memset(sd, 0, sizeof(*sd));
    const char *p = text;
    while (*p) {
        if ((p[0] == 'O' || p[0] == 'G') && p[1] == ':') {
            const char *start = p + 2, *q = start;
            while (*q && *q != ':' && !((q[0] == 'G' || q[0] == 'D') && q[1] == ':'))
                q++;
            sid_parse(p[0] == 'O' ? &sd->owner : &sd->group, start, (size_t)(q - start));
            p = q;
        } else if (p[0] == 'D' && p[1] == ':') {
            p += 2;
            while (*p && *p != '(') {
                if (*p == 'P')
                    sd->protect = true;
                p++;
            }
            while (*p == '(') {
                const char *field[6];
                size_t length[6];
                const char *q = p + 1;
                for (int i = 0; i < 6; i++) {
                    field[i] = q;
                    while (*q && *q != ';' && *q != ')')
                        q++;
                    length[i] = (size_t)(q - field[i]);
                    if (i < 5) {
                        if (*q != ';')
                            fail("bad ace in %s", text);
                        q++;
                    }
                }
                if (*q != ')')
                    fail("bad ace in %s", text);
                sd->aces = xrealloc(sd->aces, (sd->count + 1) * sizeof(Ace));
                Ace *ace = &sd->aces[sd->count++];
                memset(ace, 0, sizeof(*ace));
                ace->type = length[0] == 1 && field[0][0] == 'D' ? 1 : 0;
                if (length[0] != 1 || (field[0][0] != 'A' && field[0][0] != 'D'))
                    fail("only allow and deny entries are supported: %s", text);
                ace->flags = ace_flags_parse(field[1], length[1]);
                ace->mask = rights_parse(field[2], length[2]);
                sid_parse(&ace->sid, field[5], length[5]);
                p = q + 1;
            }
        } else {
            fail("bad security descriptor %s", text);
        }
    }
    if (!sd->owner.size || !sd->group.size)
        fail("a security descriptor needs an owner and a group: %s", text);
    return sd;
}

static uint32_t map_generic(uint32_t mask)
{
    uint32_t out = mask & 0x0FFFFFFF;
    if (mask & 0x80000000)
        out |= 0x20019;
    if (mask & 0x40000000)
        out |= 0x20006;
    if (mask & 0x20000000)
        out |= 0x20019;
    if (mask & 0x10000000)
        out |= 0xF003F;
    return out;
}

/* what a subkey gets from its key: the entries that propagate, as inherited entries that apply. */
static Sd *sd_inherit(const Sd *parent)
{
    Sd *sd = xrealloc(nullptr, sizeof(*sd));
    *sd = *parent;
    sd->aces = nullptr;
    sd->count = 0;
    sd->protect = false;
    for (size_t i = 0; i < parent->count; i++) {
        const Ace *ace = &parent->aces[i];
        if (!(ace->flags & ACE_CI))
            continue;
        sd->aces = xrealloc(sd->aces, (sd->count + 1) * sizeof(Ace));
        Ace *copy = &sd->aces[sd->count++];
        *copy = *ace;
        copy->mask = map_generic(ace->mask);
        copy->flags = (uint8_t)((ace->flags & ACE_NP) ? ACE_ID : ((ace->flags & ~ACE_IO) | ACE_ID));
    }
    return sd;
}

/* the self relative form, with the dacl first as the system writes it. */
static void sd_serialize(const Sd *sd, Buf *out)
{
    size_t acl_size = 8;
    for (size_t i = 0; i < sd->count; i++)
        acl_size += 8 + sd->aces[i].sid.size;
    uint8_t header[20] = {1, 0};
    put16(header + 2, 0x8004 | 0x0400 | (sd->protect ? 0x1000 : 0));
    put32(header + 4, 20 + (uint32_t)acl_size);
    put32(header + 8, 20 + (uint32_t)acl_size + (uint32_t)sd->owner.size);
    put32(header + 12, 0);
    put32(header + 16, 20);
    buf_put(out, header, sizeof(header));
    uint8_t acl[8] = {2, 0};
    put16(acl + 2, (uint16_t)acl_size);
    put16(acl + 4, (uint16_t)sd->count);
    buf_put(out, acl, sizeof(acl));
    for (size_t i = 0; i < sd->count; i++) {
        const Ace *ace = &sd->aces[i];
        uint8_t head[8] = {ace->type, ace->flags};
        put16(head + 2, (uint16_t)(8 + ace->sid.size));
        put32(head + 4, ace->mask);
        buf_put(out, head, 8);
        buf_put(out, ace->sid.bytes, ace->sid.size);
    }
    buf_put(out, sd->owner.bytes, sd->owner.size);
    buf_put(out, sd->group.bytes, sd->group.size);
}

/* ------------------------------------------------------------------ the tree */

typedef struct Value {
    Buf name; /* utf-16 */
    uint32_t type;
    Buf data;
} Value;

typedef struct Key {
    char *name;
    struct Key *parent;
    struct Key **kids;
    size_t kid_count, kid_capacity;
    Value *values;
    size_t value_count, value_capacity;
    Sd *sd;        /* set by the sources, else inherited */
    Sd *effective; /* worked out when the hive is written */
    int sk;        /* index of the shared descriptor */
    uint32_t cell;
} Key;

typedef struct Hive {
    const char *file;
    Key root;
} Hive;

enum { HIVE_SYSTEM, HIVE_SOFTWARE, HIVE_DEFAULT, HIVE_USER, HIVE_COUNT };

static Hive g_hives[HIVE_COUNT] = {{.file = "SYSTEM"}, {.file = "SOFTWARE"}, {.file = "DEFAULT"}, {.file = "NTUSER.DAT"}};


static Key *key_child(Key *parent, const char *name, size_t n, bool create)
{
    if (n == 0 || n > MAX_NAME)
        fail("a key name has 1 to %d characters: %.*s", MAX_NAME, (int)n, name);
    for (size_t i = 0; i < n; i++)
        if ((uint8_t)name[i] >= 0x80)
            fail("key names are ascii: %.*s", (int)n, name);
    char *copy = xstrndup(name, n);
    for (size_t i = 0; i < parent->kid_count; i++) {
        if (name_compare(parent->kids[i]->name, copy) == 0) {
            free(copy);
            return parent->kids[i];
        }
    }
    if (!create) {
        free(copy);
        return nullptr;
    }
    Key *key = xrealloc(nullptr, sizeof(*key));
    memset(key, 0, sizeof(*key));
    key->name = copy;
    key->parent = parent;
    key->sk = -1;
    if (parent->kid_count == parent->kid_capacity) {
        parent->kid_capacity = parent->kid_capacity ? parent->kid_capacity * 2 : 8;
        parent->kids = xrealloc(parent->kids, parent->kid_capacity * sizeof(Key *));
    }
    parent->kids[parent->kid_count++] = key;
    return key;
}

static int utf16_compare(const Buf *a, const Buf *b)
{
    size_t n = (a->size < b->size ? a->size : b->size) / 2;
    for (size_t i = 0; i < n; i++) {
        int x = upcase(a->data[2 * i] | (a->data[2 * i + 1] << 8));
        int y = upcase(b->data[2 * i] | (b->data[2 * i + 1] << 8));
        if (x != y)
            return x < y ? -1 : 1;
    }
    return a->size == b->size ? 0 : a->size < b->size ? -1 : 1;
}

static void key_set_value(Key *key, Buf *name, uint32_t type, Buf *data)
{
    for (size_t i = 0; i < key->value_count; i++) {
        if (utf16_compare(&key->values[i].name, name) == 0) {
            free(key->values[i].data.data);
            key->values[i].type = type;
            key->values[i].data = *data;
            free(name->data);
            return;
        }
    }
    if (key->value_count == key->value_capacity) {
        key->value_capacity = key->value_capacity ? key->value_capacity * 2 : 4;
        key->values = xrealloc(key->values, key->value_capacity * sizeof(Value));
    }
    Value *v = &key->values[key->value_count++];
    v->name = *name;
    v->type = type;
    v->data = *data;
}

static void key_delete_value(Key *key, const Buf *name)
{
    for (size_t i = 0; i < key->value_count; i++) {
        if (utf16_compare(&key->values[i].name, name) == 0) {
            memmove(&key->values[i], &key->values[i + 1], (key->value_count - i - 1) * sizeof(Value));
            key->value_count--;
            return;
        }
    }
}

/* ------------------------------------------------------------------ paths */

static bool word_is(const char *s, size_t n, const char *word)
{
    return strlen(word) == n && name_compare(word, xstrndup(s, n)) == 0;
}

/* the key a path names, below the hive that holds it. */
static Key *resolve(const char *path)
{
    const char *parts[64];
    size_t lengths[64], count = 0;
    for (const char *p = path;;) {
        const char *end = strchr(p, '\\');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (count == 64)
            fail("a key path is too deep: %s", path);
        if (n == 0)
            fail("an empty name in the key path %s", path);
        parts[count] = p;
        lengths[count++] = n;
        if (!end)
            break;
        p = end + 1;
    }
    size_t at = 1;
    Key *key = nullptr;
    if (word_is(parts[0], lengths[0], "HKLM") || word_is(parts[0], lengths[0], "HKEY_LOCAL_MACHINE")) {
        if (count < 2)
            fail("HKLM needs SYSTEM or SOFTWARE: %s", path);
        if (word_is(parts[1], lengths[1], "SYSTEM"))
            key = &g_hives[HIVE_SYSTEM].root;
        else if (word_is(parts[1], lengths[1], "SOFTWARE"))
            key = &g_hives[HIVE_SOFTWARE].root;
        else
            fail("only SYSTEM and SOFTWARE are kept below HKLM: %s", path);
        at = 2;
    } else if (word_is(parts[0], lengths[0], "HKCR") || word_is(parts[0], lengths[0], "HKEY_CLASSES_ROOT")) {
        key = key_child(&g_hives[HIVE_SOFTWARE].root, "Classes", 7, true);
    } else if (word_is(parts[0], lengths[0], "HKU") || word_is(parts[0], lengths[0], "HKEY_USERS")) {
        if (count < 2 || !word_is(parts[1], lengths[1], ".DEFAULT"))
            fail("only .DEFAULT is kept below HKU: %s", path);
        key = &g_hives[HIVE_DEFAULT].root;
        at = 2;
    } else if (word_is(parts[0], lengths[0], "HKCU") || word_is(parts[0], lengths[0], "HKEY_CURRENT_USER")) {
        key = &g_hives[HIVE_USER].root;
    } else {
        fail("unknown root in the key path %s", path);
    }
    bool system = key == &g_hives[HIVE_SYSTEM].root;
    for (size_t i = at; i < count; i++) {
        if (system && i == at && word_is(parts[i], lengths[i], "CurrentControlSet"))
            key = key_child(key, "ControlSet001", 13, true);
        else
            key = key_child(key, parts[i], lengths[i], true);
    }
    return key;
}

/* {a,b} in a key path stands for each of the choices, as in a shell. */
static void expand(const char *text, Key ***keys, size_t *count)
{
    const char *open = strchr(text, '{');
    const char *close = open ? strchr(open, '}') : nullptr;
    /* a guid is also in braces, only a comma makes a list. */
    for (const char *scan = text; open && close;) {
        const char *comma = memchr(open, ',', (size_t)(close - open));
        if (comma)
            break;
        scan = close + 1;
        open = strchr(scan, '{');
        close = open ? strchr(open, '}') : nullptr;
    }
    if (!open || !close) {
        *keys = xrealloc(*keys, (*count + 1) * sizeof(Key *));
        (*keys)[(*count)++] = resolve(text);
        return;
    }
    size_t head = (size_t)(open - text);
    for (const char *p = open + 1; p <= close;) {
        const char *end = p;
        while (end < close && *end != ',')
            end++;
        char *variant = xrealloc(nullptr, head + (size_t)(end - p) + strlen(close + 1) + 1);
        memcpy(variant, text, head);
        memcpy(variant + head, p, (size_t)(end - p));
        strcpy(variant + head + (end - p), close + 1);
        expand(variant, keys, count);
        free(variant);
        p = end + 1;
    }
}

/* ------------------------------------------------------------------ the 32-bit view */

#define CM_STRING_ENTRY(text) text,
static const char *const g_shared[] = {CM_SHARED_SOFTWARE_KEYS(CM_STRING_ENTRY)};
static const char *const g_redirected_classes[] = {CM_REDIRECTED_CLASSES(CM_STRING_ENTRY)};

static bool path_is_shared(const char *path)
{
    for (size_t i = 0; i < sizeof(g_shared) / sizeof(g_shared[0]); i++) {
        size_t n = strlen(g_shared[i]);
        if (!strncmp(path, g_shared[i], n) && (!path[n] || path[n] == '\\'))
            return true;
    }
    return false;
}

static bool value_exists(const Key *key, const Buf *name)
{
    for (size_t i = 0; i < key->value_count; i++)
        if (utf16_compare(&key->values[i].name, name) == 0)
            return true;
    return false;
}

/* what the 64-bit side has and the 32-bit side was not given, the sources' own lines for Wow6432Node win. */
static void mirror_tree(Key *src, Key *dst, const char *path, bool check_shared)
{
    for (size_t i = 0; i < src->value_count; i++) {
        const Value *v = &src->values[i];
        if (value_exists(dst, &v->name))
            continue;
        Buf name = {}, data = {};
        buf_put(&name, v->name.data, v->name.size);
        buf_put(&data, v->data.data, v->data.size);
        key_set_value(dst, &name, v->type, &data);
    }
    if (!dst->sd && src->sd)
        dst->sd = src->sd;
    for (size_t i = 0; i < src->kid_count; i++) {
        const Key *kid = src->kids[i];
        char child[1024];
        snprintf(child, sizeof(child), "%s\\%s", path, kid->name);
        if (check_shared && path_is_shared(child))
            continue;
        mirror_tree(src->kids[i], key_child(dst, kid->name, strlen(kid->name), true), child, check_shared);
    }
}

/*
 * a 32-bit program sees HKLM\SOFTWARE below its Wow6432Node, except for the subtrees both views share, and Classes\CLSID and
 * its likes below Classes\Wow6432Node. the sources describe one machine, the 32-bit side is made from it.
 */
static void mirror_wow64(void)
{
    Key *software = &g_hives[HIVE_SOFTWARE].root;
    Key *wow = key_child(software, "WOW6432Node", 11, true);
    size_t count = software->kid_count;
    Key **top = xrealloc(nullptr, count * sizeof(Key *));
    memcpy(top, software->kids, count * sizeof(Key *));
    for (size_t i = 0; i < count; i++) {
        Key *kid = top[i];
        if (kid == wow)
            continue;
        if (!name_compare(kid->name, "Classes")) {
            Key *classes_wow = key_child(kid, "WOW6432Node", 11, true);
            for (size_t c = 0; c < sizeof(g_redirected_classes) / sizeof(g_redirected_classes[0]); c++) {
                Key *from = key_child(kid, g_redirected_classes[c], strlen(g_redirected_classes[c]), false);
                if (from)
                    mirror_tree(from, key_child(classes_wow, g_redirected_classes[c], strlen(g_redirected_classes[c]), true),
                                "", false);
            }
            continue;
        }
        char path[1024];
        snprintf(path, sizeof(path), "\\%s", kid->name);
        if (path_is_shared(path))
            continue;
        mirror_tree(kid, key_child(wow, kid->name, strlen(kid->name), true), path, true);
    }
    free(top);
}

/* ------------------------------------------------------------------ reading the sources */

static const char *skip_blanks(const char *p)
{
    while (*p == ' ' || *p == '\t')
        p++;
    return p;
}

/* a quoted string, p at the opening quote. */
static const char *parse_quoted(const char *p, Buf *out, bool terminate)
{
    if (*p != '"')
        fail("a quoted string is expected");
    p++;
    const char *start = p;
    Buf bytes = {};
    for (;;) {
        if (!*p)
            fail("a string is not closed");
        if (*p == '"')
            break;
        if (*p == '\\') {
            if (p[1] != '\\' && p[1] != '"')
                fail("a backslash in a string is written \\\\ here");
            buf_put(&bytes, start, (size_t)(p - start));
            buf_put(&bytes, p + 1, 1);
            p += 2;
            start = p;
            continue;
        }
        p++;
    }
    buf_put(&bytes, start, (size_t)(p - start));
    utf16_append(out, (const char *)bytes.data, bytes.size);
    free(bytes.data);
    if (terminate)
        buf_u16(out, 0);
    return p + 1;
}

static uint64_t parse_hex(const char *p, size_t digits)
{
    uint64_t v = 0;
    for (size_t i = 0; i < digits; i++) {
        int c = p[i];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0)
            fail("hexadecimal digits are expected");
        v = v << 4 | (uint64_t)d;
    }
    return v;
}

static void parse_value(Key **keys, size_t key_count, const char *line)
{
    if (!key_count)
        fail("a value needs a key header before it");
    Buf name = {};
    const char *p = line;
    if (*p == '@')
        p++;
    else
        p = parse_quoted(p, &name, false);
    p = skip_blanks(p);
    if (*p != '=')
        fail("= is expected after the value name");
    p = skip_blanks(p + 1);
    for (size_t k = 0; k < key_count; k++) {
        Buf copy = {};
        buf_put(&copy, name.data, name.size);
        Buf data = {};
        uint32_t type = 0;
        const char *q = p;
        if (*q == '-' && !q[1]) {
            key_delete_value(keys[k], &copy);
            free(copy.data);
            continue;
        } else if (*q == '"') {
            type = REG_SZ;
            q = parse_quoted(q, &data, true);
        } else if (!strncmp(q, "dword:", 6)) {
            type = REG_DWORD;
            uint8_t t[4];
            put32(t, (uint32_t)parse_hex(q + 6, 8));
            buf_put(&data, t, 4);
            q += 14;
        } else if (!strncmp(q, "qword:", 6)) {
            type = REG_QWORD;
            uint8_t t[8];
            put64(t, parse_hex(q + 6, 16));
            buf_put(&data, t, 8);
            q += 22;
        } else if (!strncmp(q, "expand:", 7)) {
            type = REG_EXPAND_SZ;
            q = parse_quoted(q + 7, &data, true);
        } else if (!strncmp(q, "multi:", 6)) {
            type = REG_MULTI_SZ;
            q = skip_blanks(q + 6);
            while (*q == '"') {
                q = skip_blanks(parse_quoted(q, &data, true));
                if (*q == ',')
                    q = skip_blanks(q + 1);
            }
            if (!data.size)
                buf_u16(&data, 0);
            buf_u16(&data, 0);
        } else if (!strncmp(q, "hex", 3)) {
            type = REG_BINARY;
            q += 3;
            if (*q == '(') {
                char *end;
                type = (uint32_t)strtoul(q + 1, &end, 16);
                if (*end != ')')
                    fail("a ) is expected after the value type");
                q = end + 1;
            }
            if (*q != ':')
                fail("a : is expected after hex");
            q = skip_blanks(q + 1);
            while (*q) {
                uint8_t byte = (uint8_t)parse_hex(q, 2);
                buf_put(&data, &byte, 1);
                q = skip_blanks(q + 2);
                if (*q == ',')
                    q = skip_blanks(q + 1);
                else if (*q)
                    fail("a comma is expected between bytes");
            }
        } else {
            fail("the value is not a string, dword, qword, expand, multi or hex");
        }
        if (*q)
            fail("unexpected text after the value: %s", q);
        if (data.size > MAX_VALUE_DATA)
            fail("a value of %zu bytes is too large for regc", data.size);
        key_set_value(keys[k], &copy, type, &data);
    }
    free(name.data);
}

static void process_line(char *line, Key ***keys, size_t *key_count)
{
    line = (char *)skip_blanks(line);
    if (!*line || *line == ';')
        return;
    if (line[0] == '[') {
        size_t len = strlen(line);
        if (line[len - 1] != ']')
            fail("a key header ends with ]");
        line[len - 1] = 0;
        free(*keys);
        *keys = nullptr;
        *key_count = 0;
        expand(line + 1, keys, key_count);
    } else if (line[0] == '#') {
        if (strncmp(line, "#security ", 10))
            fail("unknown directive %s", line);
        if (!*key_count)
            fail("#security needs a key header before it");
        Sd *sd = sd_parse(line + 10);
        for (size_t k = 0; k < *key_count; k++)
            (*keys)[k]->sd = sd;
    } else {
        parse_value(*keys, *key_count, line);
    }
}

static void parse_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "regc: cannot read %s: %s\n", path, strerror(errno));
        exit(1);
    }
    Buf text = {};
    char chunk[65536];
    size_t got;
    while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0)
        buf_put(&text, chunk, got);
    fclose(f);
    buf_put(&text, "", 1);
    g_file = path;
    g_line = 0;
    Key **keys = nullptr;
    size_t key_count = 0;
    char *p = (char *)text.data;
    if (!memcmp(p, "\xEF\xBB\xBF", 3))
        p += 3;
    Buf logical = {};
    while (*p) {
        char *end = strchr(p, '\n');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        const char *line = p;
        p += end ? n + 1 : n;
        g_line++;
        while (n && (line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t'))
            n--;
        if (logical.size)
            while (n && (*line == ' ' || *line == '\t')) {
                line++;
                n--;
            }
        /* a list of bytes goes on in the next line when this one ends in a backslash. */
        bool continued = n && line[n - 1] == '\\' && (logical.size || (n > 4 && strstr(line, "hex")));
        buf_put(&logical, line, continued ? n - 1 : n);
        if (continued)
            continue;
        buf_put(&logical, "", 1);
        process_line((char *)logical.data, &keys, &key_count);
        logical.size = 0;
    }
    free(keys);
    free(logical.data);
    free(text.data);
    g_file = nullptr;
}

/* ------------------------------------------------------------------ writing the hive */

typedef struct Writer {
    Buf data;
    size_t bin_start, bin_size, used;
    uint64_t time;
    Buf *blobs;
    size_t blob_count;
    uint32_t *sk_cells;
    uint32_t *sk_uses;
} Writer;

static void bin_close(Writer *w, bool last)
{
    if (!w->bin_size)
        return;
    size_t size = w->bin_size;
    if (last) {
        size = (w->used + 0xFFF) & ~(size_t)0xFFF;
        w->data.size = w->bin_start + size;
        put32(w->data.data + w->bin_start + 8, (uint32_t)size);
    }
    if (size > w->used)
        put32(w->data.data + w->bin_start + w->used, (uint32_t)(size - w->used));
    w->bin_size = 0;
}

static void bin_open(Writer *w, size_t size)
{
    w->bin_start = w->data.size;
    w->bin_size = size;
    w->used = 32;
    uint8_t *zero = xrealloc(nullptr, size);
    memset(zero, 0, size);
    buf_put(&w->data, zero, size);
    free(zero);
    memcpy(w->data.data + w->bin_start, "hbin", 4);
    put32(w->data.data + w->bin_start + 4, (uint32_t)w->bin_start);
    put32(w->data.data + w->bin_start + 8, (uint32_t)size);
}

static uint32_t cell_alloc(Writer *w, size_t payload)
{
    size_t total = (payload + 4 + 7) & ~(size_t)7;
    if (!w->bin_size || w->used + total > w->bin_size) {
        bin_close(w, false);
        size_t need = 32 + total;
        bin_open(w, need > BIN_SIZE ? (need + 0xFFF) & ~(size_t)0xFFF : BIN_SIZE);
    }
    uint32_t at = (uint32_t)(w->bin_start + w->used);
    w->used += total;
    put32(w->data.data + at, (uint32_t)-(int32_t)total);
    return at;
}

static uint8_t *cell_data(Writer *w, uint32_t at)
{
    return w->data.data + at + 4;
}

static int key_compare(const void *a, const void *b)
{
    return name_compare((*(Key *const *)a)->name, (*(Key *const *)b)->name);
}

static uint32_t name_hash(const char *name)
{
    uint32_t h = 0;
    for (; *name; name++)
        h = h * 37 + (uint32_t)upcase((uint8_t)*name);
    return h;
}

static void assign_security(Writer *w, Key *key, const Sd *parent)
{
    const Sd *effective = key->sd ? key->sd : sd_inherit(parent);
    Buf blob = {};
    sd_serialize(effective, &blob);
    size_t index = 0;
    for (; index < w->blob_count; index++)
        if (w->blobs[index].size == blob.size && !memcmp(w->blobs[index].data, blob.data, blob.size))
            break;
    if (index == w->blob_count) {
        w->blobs = xrealloc(w->blobs, (w->blob_count + 1) * sizeof(Buf));
        w->blobs[w->blob_count++] = blob;
    } else {
        free(blob.data);
    }
    key->sk = (int)index;
    for (size_t i = 0; i < key->kid_count; i++)
        assign_security(w, key->kids[i], effective);
}

static void count_security(Writer *w, const Key *key)
{
    w->sk_uses[key->sk]++;
    for (size_t i = 0; i < key->kid_count; i++)
        count_security(w, key->kids[i]);
}

static uint32_t write_subkey_list(Writer *w, Key **kids, size_t count)
{
    size_t per_list = count > 1000 ? 500 : count;
    size_t lists = (count + per_list - 1) / per_list;
    uint32_t *offsets = xrealloc(nullptr, lists * sizeof(uint32_t));
    for (size_t l = 0; l < lists; l++) {
        size_t first = l * per_list, n = count - first < per_list ? count - first : per_list;
        uint32_t at = cell_alloc(w, 4 + 8 * n);
        uint8_t *p = cell_data(w, at);
        memcpy(p, "lh", 2);
        put16(p + 2, (uint16_t)n);
        for (size_t i = 0; i < n; i++) {
            put32(p + 4 + 8 * i, kids[first + i]->cell);
            put32(p + 8 + 8 * i, name_hash(kids[first + i]->name));
        }
        offsets[l] = at;
    }
    uint32_t result = offsets[0];
    if (lists > 1) {
        result = cell_alloc(w, 4 + 4 * lists);
        uint8_t *p = cell_data(w, result);
        memcpy(p, "ri", 2);
        put16(p + 2, (uint16_t)lists);
        for (size_t l = 0; l < lists; l++)
            put32(p + 4 + 4 * l, offsets[l]);
    }
    free(offsets);
    return result;
}

static uint32_t write_key(Writer *w, Key *key, uint32_t parent, bool root)
{
    const char *name = root ? "ROOT" : key->name;
    size_t name_length = strlen(name);
    uint32_t at = cell_alloc(w, 76 + name_length);
    key->cell = at;
    qsort(key->kids, key->kid_count, sizeof(Key *), key_compare);
    uint32_t max_kid_name = 0;
    for (size_t i = 0; i < key->kid_count; i++) {
        write_key(w, key->kids[i], at, false);
        uint32_t bytes = (uint32_t)strlen(key->kids[i]->name) * 2;
        if (bytes > max_kid_name)
            max_kid_name = bytes;
    }
    uint32_t list = 0xFFFFFFFF;
    if (key->kid_count)
        list = write_subkey_list(w, key->kids, key->kid_count);

    uint32_t values = 0xFFFFFFFF, max_value_name = 0, max_value_data = 0;
    if (key->value_count) {
        values = cell_alloc(w, 4 * key->value_count);
        for (size_t i = 0; i < key->value_count; i++) {
            Value *v = &key->values[i];
            bool ascii = true;
            for (size_t b = 1; b < v->name.size; b += 2)
                if (v->name.data[b] || v->name.data[b - 1] >= 0x80)
                    ascii = false;
            size_t name_bytes = ascii ? v->name.size / 2 : v->name.size;
            uint32_t data_cell = 0;
            if (v->data.size > 4) {
                data_cell = cell_alloc(w, v->data.size);
                memcpy(cell_data(w, data_cell), v->data.data, v->data.size);
            }
            uint32_t vk = cell_alloc(w, 20 + name_bytes);
            uint8_t *p = cell_data(w, vk);
            memcpy(p, "vk", 2);
            put16(p + 2, (uint16_t)name_bytes);
            if (v->data.size > 4) {
                put32(p + 4, (uint32_t)v->data.size);
                put32(p + 8, data_cell);
            } else {
                uint8_t inline_data[4] = {};
                memcpy(inline_data, v->data.data, v->data.size);
                put32(p + 4, 0x80000000u | (uint32_t)v->data.size);
                memcpy(p + 8, inline_data, 4);
            }
            put32(p + 12, v->type);
            put16(p + 16, ascii ? 1 : 0);
            if (ascii)
                for (size_t b = 0; b < name_bytes; b++)
                    p[20 + b] = v->name.data[2 * b];
            else
                memcpy(p + 20, v->name.data, name_bytes);
            put32(cell_data(w, values) + 4 * i, vk);
            if (v->name.size > max_value_name)
                max_value_name = (uint32_t)v->name.size;
            if (v->data.size > max_value_data)
                max_value_data = (uint32_t)v->data.size;
        }
    }

    uint8_t *p = cell_data(w, at);
    memcpy(p, "nk", 2);
    put16(p + 2, root ? 0x2C : 0x20);
    put64(p + 4, w->time);
    put32(p + 12, 0);
    put32(p + 16, root ? 0xFFFFFFFF : parent);
    put32(p + 20, (uint32_t)key->kid_count);
    put32(p + 24, 0);
    put32(p + 28, list);
    put32(p + 32, 0xFFFFFFFF);
    put32(p + 36, (uint32_t)key->value_count);
    put32(p + 40, values);
    put32(p + 44, w->sk_cells[key->sk]);
    put32(p + 48, 0xFFFFFFFF);
    put32(p + 52, max_kid_name);
    put32(p + 56, 0);
    put32(p + 60, max_value_name);
    put32(p + 64, max_value_data);
    put32(p + 68, 0);
    put16(p + 72, (uint16_t)name_length);
    put16(p + 74, 0);
    memcpy(p + 76, name, name_length);
    return at;
}

static const char g_default_security[] =
    "O:BAG:SYD:P(A;CI;KR;;;BU)(A;CI;KA;;;BA)(A;CI;KA;;;SY)(A;CIIO;GA;;;CO)(A;CI;KR;;;AC)";

static void write_hive(Hive *hive, const char *directory, uint64_t time)
{
    Writer w = {};
    w.time = time;
    if (!hive->root.sd)
        hive->root.sd = sd_parse(g_default_security);
    assign_security(&w, &hive->root, hive->root.sd);
    w.sk_cells = xrealloc(nullptr, w.blob_count * sizeof(uint32_t));
    w.sk_uses = xrealloc(nullptr, w.blob_count * sizeof(uint32_t));
    memset(w.sk_uses, 0, w.blob_count * sizeof(uint32_t));
    count_security(&w, &hive->root);
    for (size_t i = 0; i < w.blob_count; i++)
        w.sk_cells[i] = cell_alloc(&w, 20 + w.blobs[i].size);
    for (size_t i = 0; i < w.blob_count; i++) {
        uint8_t *p = cell_data(&w, w.sk_cells[i]);
        memcpy(p, "sk", 2);
        put16(p + 2, 0);
        put32(p + 4, w.sk_cells[(i + 1) % w.blob_count]);
        put32(p + 8, w.sk_cells[(i + w.blob_count - 1) % w.blob_count]);
        put32(p + 12, w.sk_uses[i]);
        put32(p + 16, (uint32_t)w.blobs[i].size);
        memcpy(p + 20, w.blobs[i].data, w.blobs[i].size);
    }
    uint32_t root = write_key(&w, &hive->root, 0xFFFFFFFF, true);
    bin_close(&w, true);

    uint8_t base[0x1000] = {};
    memcpy(base, "regf", 4);
    put32(base + 4, 1);
    put32(base + 8, 1);
    put64(base + 12, time);
    put32(base + 20, 1);
    put32(base + 24, 5);
    put32(base + 28, 0);
    put32(base + 32, 1);
    put32(base + 36, root);
    put32(base + 40, (uint32_t)w.data.size);
    put32(base + 44, 1);
    for (size_t i = 0; hive->file[i] && i < 31; i++)
        put16(base + 48 + 2 * i, (uint8_t)hive->file[i]);
    uint32_t sum = 0;
    for (size_t i = 0; i < 508; i += 4)
        sum ^= (uint32_t)base[i] | (uint32_t)base[i + 1] << 8 | (uint32_t)base[i + 2] << 16 | (uint32_t)base[i + 3] << 24;
    put32(base + 508, sum == 0xFFFFFFFF ? 0xFFFFFFFE : sum == 0 ? 1 : sum);

    char path[4096];
    snprintf(path, sizeof(path), "%s/%s", directory, hive->file);
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(base, 1, sizeof(base), f) != sizeof(base) || fwrite(w.data.data, 1, w.data.size, f) != w.data.size ||
        fclose(f) != 0) {
        fprintf(stderr, "regc: cannot write %s: %s\n", path, strerror(errno));
        exit(1);
    }
    free(w.data.data);
}

int main(int argc, char **argv)
{
    const char *directory = nullptr;
    /* the hives carry one fixed time, so equal sources give equal files. */
    uint64_t unix_time = 1767225600;
    int first = 1;
    for (; first < argc && argv[first][0] == '-'; first++) {
        if (!strcmp(argv[first], "-o") && first + 1 < argc)
            directory = argv[++first];
        else if (!strcmp(argv[first], "--time") && first + 1 < argc)
            unix_time = strtoull(argv[++first], nullptr, 10);
        else
            fail("usage: regc [--time seconds] -o directory source.reg...");
    }
    if (!directory)
        fail("usage: regc [--time seconds] -o directory source.reg...");
    for (int i = first; i < argc; i++)
        parse_file(argv[i]);
    mirror_wow64();
    uint64_t time = (unix_time + 11644473600ull) * 10000000ull;
    for (int i = 0; i < HIVE_COUNT; i++)
        write_hive(&g_hives[i], directory, time);
    return 0;
}
