/*
 * atomic counters for Vulkan GLSL, which has none: the declarations of a binding become the members of a storage block and
 * the counter functions become the atomic functions of buffer members, so the shader reaches glslang as ordinary GLSL.
 * the counters found are returned for the program's reflection.
 */
#ifndef MIME_HOST_GPU_GLSL_ATOMICS_H
#define MIME_HOST_GPU_GLSL_ATOMICS_H

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MIME_ATOMICS_MAX 64
#define MIME_ATOMICS_BINDINGS 8

typedef struct MimeAtomicDecl {
    char name[64];
    unsigned binding, offset, count;
} MimeAtomicDecl;

typedef struct AtomicText {
    char *data;
    size_t len, cap;
} AtomicText;

static void atomic_add(AtomicText *t, const char *s, size_t n)
{
    if (t->len + n + 1 > t->cap) {
        size_t cap = t->cap ? t->cap : 1024;
        while (cap < t->len + n + 1)
            cap *= 2;
        t->data = realloc(t->data, cap);
        t->cap = cap;
    }
    memcpy(t->data + t->len, s, n);
    t->len += n;
    t->data[t->len] = 0;
}

static void atomic_str(AtomicText *t, const char *s)
{
    atomic_add(t, s, strlen(s));
}

static bool atomic_ident(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

/* the next character outside comments, blanks and strings of a comment-only skip; NULL at the end. */
static const char *atomic_skip(const char *p)
{
    for (;;) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            p++;
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n')
                p++;
        } else if (p[0] == '/' && p[1] == '*') {
            const char *end = strstr(p + 2, "*/");
            if (!end)
                return p + strlen(p);
            p = end + 2;
        } else {
            return p;
        }
    }
}

static bool atomic_word(const char *p, const char *word)
{
    size_t n = strlen(word);
    return !strncmp(p, word, n) && !atomic_ident(p[n]);
}

/* a layout qualifier value: binding = 2, offset = 8. */
static bool atomic_layout_value(const char *layout, const char *end, const char *key, unsigned *value)
{
    size_t n = strlen(key);
    for (const char *p = layout; p + n <= end; p++) {
        if ((p == layout || !atomic_ident(p[-1])) && !strncmp(p, key, n) && !atomic_ident(p[n])) {
            const char *v = p + n;
            while (v < end && (*v == ' ' || *v == '\t'))
                v++;
            if (v < end && *v == '=') {
                *value = (unsigned)strtoul(v + 1, nullptr, 0);
                return true;
            }
        }
    }
    return false;
}

typedef struct AtomicSpan {
    size_t begin, end;      /* the whole statement, layout included. */
    unsigned first_decl, decl_count;
} AtomicSpan;

/*
 * returns the rewritten source, or null when it declares no counter. decls and count get the counters in order of
 * appearance, with their offsets filled in.
 */
static char *mime_glsl_atomics(const char *src, size_t len, MimeAtomicDecl *decls, unsigned *count)
{
    *count = 0;
    if (!strstr(src, "atomic_uint"))
        return nullptr;
    AtomicSpan spans[MIME_ATOMICS_MAX];
    unsigned span_count = 0;
    unsigned next_offset[MIME_ATOMICS_BINDINGS] = {};
    bool seen[MIME_ATOMICS_BINDINGS] = {};
    size_t first_span_of[MIME_ATOMICS_BINDINGS];
    for (const char *p = src; *p;) {
        if (p[0] == '/' && p[1] == '/') {
            while (*p && *p != '\n')
                p++;
            continue;
        }
        if (p[0] == '/' && p[1] == '*') {
            const char *end = strstr(p + 2, "*/");
            p = end ? end + 2 : p + strlen(p);
            continue;
        }
        if (p[0] == '#') {
            while (*p && *p != '\n')
                p++;
            continue;
        }
        if (!(atomic_word(p, "uniform") && (p == src || !atomic_ident(p[-1])))) {
            p++;
            continue;
        }
        const char *q = atomic_skip(p + 7);
        if (!atomic_word(q, "atomic_uint")) {
            p++;
            continue;
        }
        /* the layout qualifier before it. */
        const char *start = p;
        unsigned binding = 0, offset = 0;
        bool has_offset = false;
        const char *b = p;
        while (b > src && isspace((unsigned char)b[-1]))
            b--;
        if (b > src && b[-1] == ')') {
            const char *close = b - 1, *open = close;
            while (open > src && *open != '(')
                open--;
            const char *w = open;
            while (w > src && isspace((unsigned char)w[-1]))
                w--;
            if (w - src >= 6 && !strncmp(w - 6, "layout", 6)) {
                atomic_layout_value(open + 1, close, "binding", &binding);
                has_offset = atomic_layout_value(open + 1, close, "offset", &offset);
                start = w - 6;
            }
        }
        const char *semi = strchr(q, ';');
        if (!semi || span_count == MIME_ATOMICS_MAX || binding >= MIME_ATOMICS_BINDINGS)
            return nullptr;
        /* the declarators: name or name[count], separated by commas. */
        AtomicSpan *span = &spans[span_count++];
        span->begin = (size_t)(start - src);
        span->end = (size_t)(semi + 1 - src);
        span->first_decl = *count;
        const char *d = q + 11;
        while (d < semi) {
            d = atomic_skip(d);
            if (d >= semi)
                break;
            if (!atomic_ident(*d)) {
                d++;
                continue;
            }
            MimeAtomicDecl *decl = &decls[*count];
            size_t n = 0;
            while (atomic_ident(d[n]) && n < sizeof(decl->name) - 1)
                n++;
            memset(decl, 0, sizeof(*decl));
            memcpy(decl->name, d, n);
            d += n;
            decl->count = 1;
            d = atomic_skip(d);
            if (*d == '[') {
                decl->count = (unsigned)strtoul(d + 1, nullptr, 0);
                const char *close = strchr(d, ']');
                d = close ? close + 1 : semi;
            }
            decl->binding = binding;
            decl->offset = has_offset ? offset : next_offset[binding];
            has_offset = false;
            next_offset[binding] = decl->offset + 4 * decl->count;
            if (!seen[binding]) {
                seen[binding] = true;
                first_span_of[binding] = span_count - 1;
            }
            (*count)++;
            if (*count == MIME_ATOMICS_MAX)
                return nullptr;
        }
        span->decl_count = *count - span->first_decl;
        p = semi + 1;
    }
    if (!span_count)
        return nullptr;

    /* a version below 4.3 has no storage blocks. */
    AtomicText out = {};
    size_t at = 0;
    for (unsigned s = 0; s < span_count; s++) {
        atomic_add(&out, src + at, spans[s].begin - at);
        for (unsigned b = 0; b < MIME_ATOMICS_BINDINGS; b++) {
            if (!seen[b] || first_span_of[b] != s)
                continue;
            char line[160];
            snprintf(line, sizeof(line), "layout(std430, binding = %u) buffer mime_acb_%u {\n", b, b);
            atomic_str(&out, line);
            unsigned position = 0, pad = 0;
            for (;;) {
                const MimeAtomicDecl *next = nullptr;
                for (unsigned i = 0; i < *count; i++)
                    if (decls[i].binding == b && decls[i].offset >= position && (!next || decls[i].offset < next->offset))
                        next = &decls[i];
                if (!next)
                    break;
                for (; position < next->offset; position += 4) {
                    snprintf(line, sizeof(line), "uint mime_pad_%u_%u;\n", b, pad++);
                    atomic_str(&out, line);
                }
                if (next->count > 1)
                    snprintf(line, sizeof(line), "uint %s[%u];\n", next->name, next->count);
                else
                    snprintf(line, sizeof(line), "uint %s;\n", next->name);
                atomic_str(&out, line);
                position = next->offset + 4 * next->count;
            }
            atomic_str(&out, "};\n");
        }
        /* the line count stays: the declaration's lines become blank ones. */
        for (size_t i = spans[s].begin; i < spans[s].end; i++)
            if (src[i] == '\n')
                atomic_add(&out, "\n", 1);
        at = spans[s].end;
    }
    atomic_add(&out, src + at, len - at);

    static const char macros[] =
        "#define atomicCounterIncrement(c) atomicAdd(c, 1u)\n"
        "#define atomicCounterDecrement(c) (atomicAdd(c, 0xFFFFFFFFu) - 1u)\n"
        "#define atomicCounter(c) atomicAdd(c, 0u)\n"
        "#define atomicCounterAdd(c, v) atomicAdd(c, v)\n"
        "#define atomicCounterSubtract(c, v) atomicAdd(c, 0u - (v))\n"
        "#define atomicCounterMin(c, v) atomicMin(c, v)\n"
        "#define atomicCounterMax(c, v) atomicMax(c, v)\n"
        "#define atomicCounterAnd(c, v) atomicAnd(c, v)\n"
        "#define atomicCounterOr(c, v) atomicOr(c, v)\n"
        "#define atomicCounterXor(c, v) atomicXor(c, v)\n"
        "#define atomicCounterExchange(c, v) atomicExchange(c, v)\n"
        "#define atomicCounterCompSwap(c, a, v) atomicCompSwap(c, a, v)\n";

    /* the version is raised to 4.3 for the storage blocks, the macros follow it, and the extension lines for counters,
       which glslang does not take in Vulkan semantics, become blank. */
    AtomicText result = {};
    bool macros_done = false;
    for (char *p = out.data; *p;) {
        char *eol = strchr(p, '\n');
        size_t n = eol ? (size_t)(eol - p + 1) : strlen(p);
        const char *q = p;
        while (*q == ' ' || *q == '\t')
            q++;
        bool written = false;
        if (*q == '#') {
            q++;
            while (*q == ' ' || *q == '\t')
                q++;
            if (!macros_done && atomic_word(q, "version")) {
                int version = atoi(q + 7);
                bool es = strstr(q, " es") != nullptr && (!eol || strstr(q, " es") < eol);
                if (!es && version >= 330 && version < 430)
                    atomic_str(&result, "#version 430\n");
                else
                    atomic_add(&result, p, n);
                atomic_str(&result, macros);
                macros_done = true;
                written = true;
            } else if (atomic_word(q, "extension")) {
                const char *e = q + 9;
                while (*e == ' ' || *e == '\t')
                    e++;
                if (atomic_word(e, "GL_ARB_shader_atomic_counters") || atomic_word(e, "GL_ARB_shader_atomic_counter_ops")) {
                    atomic_str(&result, "\n");
                    written = true;
                }
            }
        }
        if (!written)
            atomic_add(&result, p, n);
        if (!eol)
            break;
        p = eol + 1;
    }
    if (!macros_done) {
        AtomicText first = {};
        atomic_str(&first, macros);
        atomic_str(&first, result.data ? result.data : "");
        free(result.data);
        result = first;
    }
    free(out.data);
    return result.data;
}

#endif
