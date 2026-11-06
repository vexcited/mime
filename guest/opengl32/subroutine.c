/*
 * shader subroutines. SPIR-V has no subroutines, so the source is rewritten before it is sent: a subroutine uniform becomes
 * an int uniform that holds the index of the function it selects, a call through it becomes a call of a function made
 * for it that tests the index, and the type and the subroutine qualifiers disappear. glUniformSubroutinesuiv writes the
 * ints. the indices of the functions and the locations of the uniforms are the ones this pass numbers, in the order the
 * source declares them.
 */
#include "opengl32.h"

typedef struct Tok {
    int start, end;
    char kind; /* i identifier, n number, p punctuation. */
} Tok;

typedef struct Edit {
    int start, end;
    char *text;
} Edit;

typedef struct Buf {
    char *data;
    SIZE_T len, cap;
} Buf;

static void buf_add(Buf *b, const char *s, SIZE_T n)
{
    if (b->len + n + 1 > b->cap) {
        SIZE_T cap = b->cap ? b->cap : 256;
        while (cap < b->len + n + 1)
            cap *= 2;
        char *d = gl_realloc(b->data, cap);
        if (!d)
            return;
        b->data = d;
        b->cap = cap;
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}

static void buf_str(Buf *b, const char *s)
{
    buf_add(b, s, strlen(s));
}

static uint32_t parse_uint(const char *s)
{
    uint32_t n = 0;
    while (*s >= '0' && *s <= '9')
        n = n * 10 + (uint32_t)(*s++ - '0');
    return n;
}

static BOOL is_id(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static Tok *tokenize(const char *s, int *count)
{
    int n = (int)strlen(s), cap = 256, k = 0;
    Tok *t = gl_alloc((SIZE_T)cap * sizeof(Tok));
    for (int i = 0; i < n && t;) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            i++;
        } else if (c == '/' && s[i + 1] == '/') {
            while (i < n && s[i] != '\n')
                i++;
        } else if (c == '/' && s[i + 1] == '*') {
            i += 2;
            while (i < n && !(s[i] == '*' && s[i + 1] == '/'))
                i++;
            i += 2;
        } else if (c == '#') {
            /* a directive runs to the end of its line, a backslash continuing it. */
            while (i < n && s[i] != '\n') {
                if (s[i] == '\\' && s[i + 1] == '\n')
                    i++;
                i++;
            }
        } else {
            int start = i;
            char kind = 'p';
            if (is_id(c)) {
                kind = c >= '0' && c <= '9' ? 'n' : 'i';
                while (i < n && (is_id(s[i]) || (kind == 'n' && s[i] == '.')))
                    i++;
            } else {
                i++;
            }
            if (k == cap) {
                cap *= 2;
                Tok *g = gl_realloc(t, (SIZE_T)cap * sizeof(Tok));
                if (!g)
                    return nullptr;
                t = g;
            }
            t[k++] = (Tok){start, i, kind};
        }
    }
    *count = k;
    return t;
}

static BOOL tok_is(const char *s, const Tok *t, const char *word)
{
    SIZE_T n = strlen(word);
    return (SIZE_T)(t->end - t->start) == n && !memcmp(s + t->start, word, n);
}

static char *copy_range(const char *s, int a, int b)
{
    char *r = gl_alloc((SIZE_T)(b - a) + 1);
    if (r)
        memcpy(r, s + a, (SIZE_T)(b - a));
    return r;
}

/* ---------------------------------------------------------------- what a source declares */

typedef struct SubType {
    char *name, *ret, *params;
} SubType;

typedef struct SubFunction {
    char *name;
    uint64_t types;
} SubFunction;

typedef struct SubUniform {
    char *name;
    int type;
    uint32_t size;     /* array elements, 0 for a single one. */
    uint32_t location; /* of the first element. */
} SubUniform;

struct GlSubroutines {
    SubType *types;
    SubFunction *functions;
    SubUniform *uniforms;
    uint32_t type_count, function_count, uniform_count, locations;
};

void gl_subroutines_free(GlSubroutines *s)
{
    if (!s)
        return;
    for (uint32_t i = 0; i < s->type_count; i++) {
        gl_free(s->types[i].name);
        gl_free(s->types[i].ret);
        gl_free(s->types[i].params);
    }
    for (uint32_t i = 0; i < s->function_count; i++)
        gl_free(s->functions[i].name);
    for (uint32_t i = 0; i < s->uniform_count; i++)
        gl_free(s->uniforms[i].name);
    gl_free(s->types);
    gl_free(s->functions);
    gl_free(s->uniforms);
    gl_free(s);
}

static void *grow(void *p, uint32_t count, SIZE_T size)
{
    return gl_realloc(p, (SIZE_T)(count + 1) * size);
}

static int find_type(const GlSubroutines *s, const char *name)
{
    for (uint32_t i = 0; i < s->type_count; i++)
        if (!strcmp(s->types[i].name, name))
            return (int)i;
    return -1;
}

static const char *const g_qualifiers[] = {"in", "out", "inout", "const", "highp", "mediump", "lowp", "precise", "flat",
                                           "smooth", "noperspective", "coherent", "volatile", "restrict", "readonly",
                                           "writeonly", "centroid", "sample", "patch"};

static BOOL is_qualifier(const char *w, SIZE_T n)
{
    for (SIZE_T i = 0; i < sizeof(g_qualifiers) / sizeof(g_qualifiers[0]); i++)
        if (strlen(g_qualifiers[i]) == n && !memcmp(g_qualifiers[i], w, n))
            return TRUE;
    return FALSE;
}

/*
 * the parameters of a subroutine type as a list that names every one, and the call that passes them on. a parameter without
 * a name gets one.
 */
static void split_parameters(const char *text, Buf *declaration, Buf *arguments)
{
    int depth = 0, count = 0;
    SIZE_T n = strlen(text), start = 0;
    for (SIZE_T i = 0; i <= n; i++) {
        char c = i < n ? text[i] : ',';
        if (c == '(' || c == '[')
            depth++;
        else if (c == ')' || c == ']')
            depth--;
        if (c != ',' || depth)
            continue;
        const char *piece = text + start;
        SIZE_T len = i - start;
        start = i + 1;
        while (len && (*piece == ' ' || *piece == '\n' || *piece == '\t' || *piece == '\r')) {
            piece++;
            len--;
        }
        while (len && (piece[len - 1] == ' ' || piece[len - 1] == '\n' || piece[len - 1] == '\t' || piece[len - 1] == '\r'))
            len--;
        if (!len || (len == 4 && !memcmp(piece, "void", 4)))
            continue;
        /* the words before an array suffix. */
        SIZE_T core = len;
        for (SIZE_T k = 0; k < len; k++)
            if (piece[k] == '[') {
                core = k;
                break;
            }
        while (core && (piece[core - 1] == ' '))
            core--;
        int words = 0;
        SIZE_T last_start = 0, last_end = 0;
        for (SIZE_T k = 0; k < core;) {
            while (k < core && !is_id(piece[k]))
                k++;
            SIZE_T a = k;
            while (k < core && is_id(piece[k]))
                k++;
            if (k > a && !is_qualifier(piece + a, k - a)) {
                words++;
                last_start = a;
                last_end = k;
            }
        }
        if (count++) {
            buf_str(declaration, ", ");
            buf_str(arguments, ", ");
        }
        buf_add(declaration, piece, len);
        if (words >= 2) {
            buf_add(arguments, piece + last_start, last_end - last_start);
        } else {
            char name[24];
            _snprintf(name, sizeof(name), " mime_a%d", count - 1);
            buf_str(declaration, name);
            buf_str(arguments, name + 1);
        }
    }
}

static void sort_edits(Edit *e, uint32_t n)
{
    for (uint32_t i = 1; i < n; i++) {
        Edit x = e[i];
        uint32_t k = i;
        while (k && e[k - 1].start > x.start) {
            e[k] = e[k - 1];
            k--;
        }
        e[k] = x;
    }
}

static const char g_stage_char[] = {'v', 'c', 'e', 'g', 'f'};

/* the source with the subroutines taken out, or null when it has none. stage is the position in g_stage_char. */
char *gl_subroutine_rewrite(const char *src, int stage, GlSubroutines **out)
{
    *out = nullptr;
    if (!strstr(src, "subroutine"))
        return nullptr;
    int count = 0;
    Tok *t = tokenize(src, &count);
    if (!t)
        return nullptr;
    GlSubroutines *s = gl_alloc(sizeof(GlSubroutines));
    Edit *edits = gl_alloc((SIZE_T)(count + 8) * sizeof(Edit));
    uint32_t edit_count = 0;
    Buf dispatch = {};
    BOOL found = FALSE;
    if (!s || !edits) {
        gl_free(t);
        gl_free(s);
        gl_free(edits);
        return nullptr;
    }
#define EDIT(a, b, txt) (edits[edit_count++] = (Edit){(a), (b), (txt)})

    for (int i = 0; i < count; i++) {
        if (t[i].kind != 'i' || !tok_is(src, &t[i], "subroutine"))
            continue;
        found = TRUE;
        if (i + 1 < count && tok_is(src, &t[i + 1], "(")) {
            /* subroutine(Type, Type) returnType name(...) { ... } */
            int close = i + 2;
            uint64_t mask = 0;
            while (close < count && !tok_is(src, &t[close], ")")) {
                if (t[close].kind == 'i') {
                    char *name = copy_range(src, t[close].start, t[close].end);
                    int type = name ? find_type(s, name) : -1;
                    gl_free(name);
                    if (type >= 0 && type < 64)
                        mask |= 1ull << type;
                }
                close++;
            }
            int j = close + 1;
            while (j < count && !tok_is(src, &t[j], "("))
                j++;
            if (j >= count || j == close + 1)
                continue;
            SubFunction *f = grow(s->functions, s->function_count, sizeof(SubFunction));
            if (!f)
                continue;
            s->functions = f;
            s->functions[s->function_count++] = (SubFunction){copy_range(src, t[j - 1].start, t[j - 1].end), mask};
            EDIT(t[i].start, t[close].end, nullptr);
            i = j;
        } else if (i + 1 < count && tok_is(src, &t[i + 1], "uniform")) {
            /* subroutine uniform Type name[size]; */
            if (i + 3 >= count)
                continue;
            char *type_name = copy_range(src, t[i + 2].start, t[i + 2].end);
            char *name = copy_range(src, t[i + 3].start, t[i + 3].end);
            int type = type_name ? find_type(s, type_name) : -1;
            gl_free(type_name);
            int j = i + 4;
            uint32_t size = 0;
            if (j + 2 < count && tok_is(src, &t[j], "[")) {
                size = (uint32_t)parse_uint(src + t[j + 1].start);
                j += 3;
            }
            while (j < count && !tok_is(src, &t[j], ";"))
                j++;
            if (type < 0 || !name || j >= count) {
                gl_free(name);
                continue;
            }
            SubUniform *u = grow(s->uniforms, s->uniform_count, sizeof(SubUniform));
            if (!u) {
                gl_free(name);
                continue;
            }
            s->uniforms = u;
            s->uniforms[s->uniform_count++] = (SubUniform){name, type, size, s->locations};
            s->locations += size ? size : 1;
            /* the replacement: the int, and the prototype of the function that calls through it. */
            Buf decl = {}, args = {};
            split_parameters(s->types[type].params, &decl, &args);
            char line[160];
            if (size)
                _snprintf(line, sizeof(line), "uniform int mime_su_%c_%s[%u];\n", g_stage_char[stage], name, size);
            else
                _snprintf(line, sizeof(line), "uniform int mime_su_%c_%s;\n", g_stage_char[stage], name);
            Buf text = {};
            buf_str(&text, line);
            buf_str(&text, s->types[type].ret);
            _snprintf(line, sizeof(line), " mime_sd_%c_%s(", g_stage_char[stage], name);
            buf_str(&text, line);
            if (size) {
                buf_str(&text, "int mime_i");
                if (decl.len)
                    buf_str(&text, ", ");
            }
            if (decl.data)
                buf_str(&text, decl.data);
            buf_str(&text, ");\n");
            /* the function itself goes at the end, where every subroutine is defined. */
            buf_str(&dispatch, s->types[type].ret);
            buf_str(&dispatch, line);
            if (size) {
                buf_str(&dispatch, "int mime_i");
                if (decl.len)
                    buf_str(&dispatch, ", ");
            }
            if (decl.data)
                buf_str(&dispatch, decl.data);
            buf_str(&dispatch, ")\n{\n");
            char sel[120];
            if (size)
                _snprintf(sel, sizeof(sel), "    int mime_k = mime_su_%c_%s[mime_i];\n", g_stage_char[stage], name);
            else
                _snprintf(sel, sizeof(sel), "    int mime_k = mime_su_%c_%s;\n", g_stage_char[stage], name);
            buf_str(&dispatch, sel);
            BOOL is_void = !strncmp(s->types[type].ret, "void", 4) && (!s->types[type].ret[4] || s->types[type].ret[4] == ' ');
            uint32_t emitted = 0;
            for (uint32_t k = 0; k < s->function_count; k++) {
                if (!(s->functions[k].types & (1ull << type)))
                    continue;
                char head[80];
                _snprintf(head, sizeof(head), "    %sif (mime_k == %u) ", emitted ? "else " : "", k);
                buf_str(&dispatch, head);
                buf_str(&dispatch, is_void ? "" : "return ");
                buf_str(&dispatch, s->functions[k].name);
                buf_str(&dispatch, "(");
                if (args.data)
                    buf_str(&dispatch, args.data);
                buf_str(&dispatch, ");\n");
                emitted++;
            }
            if (!is_void) {
                buf_str(&dispatch, "    return ");
                buf_str(&dispatch, s->types[type].ret);
                buf_str(&dispatch, "(0);\n");
            }
            buf_str(&dispatch, "}\n");
            gl_free(decl.data);
            gl_free(args.data);
            /* a layout qualifier in front goes too. */
            int start = t[i].start;
            if (i >= 1 && tok_is(src, &t[i - 1], ")")) {
                int k = i - 1;
                while (k >= 0 && !tok_is(src, &t[k], "("))
                    k--;
                if (k >= 1 && tok_is(src, &t[k - 1], "layout")) {
                    start = t[k - 1].start;
                }
            }
            EDIT(start, t[j].end, text.data);
            i = j;
        } else {
            /* subroutine returnType Name(params); */
            int j = i + 1;
            while (j < count && !tok_is(src, &t[j], "("))
                j++;
            int close = j;
            int depth = 0;
            for (; close < count; close++) {
                if (tok_is(src, &t[close], "("))
                    depth++;
                else if (tok_is(src, &t[close], ")") && --depth == 0)
                    break;
            }
            if (j >= count || close + 1 >= count || j == i + 1 || !tok_is(src, &t[close + 1], ";"))
                continue;
            SubType *ty = grow(s->types, s->type_count, sizeof(SubType));
            if (!ty)
                continue;
            s->types = ty;
            SubType *n = &s->types[s->type_count++];
            n->name = copy_range(src, t[j - 1].start, t[j - 1].end);
            n->ret = copy_range(src, t[i + 1].start, t[j - 2].end);
            n->params = copy_range(src, t[j].end, t[close].start);
            EDIT(t[i].start, t[close + 1].end, nullptr);
            i = close + 1;
        }
    }

    /* the calls through a subroutine uniform. */
    for (int i = 0; i < count && found; i++) {
        if (t[i].kind != 'i')
            continue;
        int u = -1;
        for (uint32_t k = 0; k < s->uniform_count && u < 0; k++)
            if (tok_is(src, &t[i], s->uniforms[k].name))
                u = (int)k;
        if (u < 0)
            continue;
        BOOL inside = FALSE;
        for (uint32_t e = 0; e < edit_count; e++)
            inside |= t[i].start >= edits[e].start && t[i].end <= edits[e].end;
        if (inside)
            continue;
        char head[96];
        _snprintf(head, sizeof(head), "mime_sd_%c_%s(", g_stage_char[stage], s->uniforms[u].name);
        int j = i + 1;
        Buf text = {};
        buf_str(&text, head);
        if (s->uniforms[u].size && j < count && tok_is(src, &t[j], "[")) {
            int depth = 0, k = j;
            for (; k < count; k++) {
                if (tok_is(src, &t[k], "["))
                    depth++;
                else if (tok_is(src, &t[k], "]") && --depth == 0)
                    break;
            }
            if (k + 1 >= count || !tok_is(src, &t[k + 1], "("))
                continue;
            buf_add(&text, src + t[j].end, (SIZE_T)(t[k].start - t[j].end));
            if (k + 2 < count && !tok_is(src, &t[k + 2], ")"))
                buf_str(&text, ", ");
            EDIT(t[i].start, t[k + 1].end, text.data);
            i = k + 1;
        } else if (!s->uniforms[u].size && j < count && tok_is(src, &t[j], "(")) {
            EDIT(t[i].start, t[j].end, text.data);
            i = j;
        } else {
            gl_free(text.data);
        }
    }

    char *result = nullptr;
    if (found) {
        sort_edits(edits, edit_count);
        Buf o = {};
        int at = 0;
        for (uint32_t e = 0; e < edit_count; e++) {
            if (edits[e].start < at)
                continue;
            buf_add(&o, src + at, (SIZE_T)(edits[e].start - at));
            /* what is taken out leaves its lines behind, so the compiler's messages keep their numbers. */
            if (edits[e].text)
                buf_str(&o, edits[e].text);
            for (int k = edits[e].start; k < edits[e].end; k++)
                if (src[k] == '\n')
                    buf_str(&o, "\n");
            at = edits[e].end;
        }
        buf_str(&o, src + at);
        if (dispatch.data) {
            buf_str(&o, "\n");
            buf_str(&o, dispatch.data);
        }
        result = o.data;
    }
    for (uint32_t e = 0; e < edit_count; e++)
        gl_free(edits[e].text);
    gl_free(edits);
    gl_free(dispatch.data);
    gl_free(t);
    if (result)
        *out = s;
    else
        gl_subroutines_free(s);
    return result;
}

/* ---------------------------------------------------------------- the program's side */

static int stage_index(GLenum type)
{
    switch (type) {
    case GL_VERTEX_SHADER:
        return 0;
    case GL_TESS_CONTROL_SHADER:
        return 1;
    case GL_TESS_EVALUATION_SHADER:
        return 2;
    case GL_GEOMETRY_SHADER:
        return 3;
    case GL_FRAGMENT_SHADER:
        return 4;
    default:
        return -1;
    }
}

static GlSubroutines *stage_of(GlContext *ctx, GlProgram *p, GLenum type)
{
    int s = stage_index(type);
    if (s < 0) {
        gl_error(ctx, GL_INVALID_ENUM);
        return nullptr;
    }
    gl_program_ready(p);
    return p->subroutines[s];
}

static GlProgram *program_of(GlContext *ctx, GLuint program)
{
    GlProgram *p = gl_program(ctx, program);
    if (!p)
        gl_error(ctx, GL_INVALID_VALUE);
    return p;
}

GLAPI GLint APIENTRY glGetSubroutineUniformLocation(GLuint program, GLenum shadertype, const GLchar *name)
{
    GL_CONTEXT_OR_RETURN(-1);
    GlProgram *p = program_of(ctx, program);
    GlSubroutines *s = p ? stage_of(ctx, p, shadertype) : nullptr;
    if (!s || !name)
        return -1;
    /* "name[2]" asks for an element. */
    SIZE_T n = strlen(name);
    uint32_t element = 0;
    const char *br = strchr(name, '[');
    if (br) {
        element = (uint32_t)parse_uint(br + 1);
        n = (SIZE_T)(br - name);
    }
    for (uint32_t i = 0; i < s->uniform_count; i++)
        if (strlen(s->uniforms[i].name) == n && !strncmp(s->uniforms[i].name, name, n) &&
            element < (s->uniforms[i].size ? s->uniforms[i].size : 1))
            return (GLint)(s->uniforms[i].location + element);
    return -1;
}

GLAPI GLuint APIENTRY glGetSubroutineIndex(GLuint program, GLenum shadertype, const GLchar *name)
{
    GL_CONTEXT_OR_RETURN(GL_INVALID_INDEX);
    GlProgram *p = program_of(ctx, program);
    GlSubroutines *s = p ? stage_of(ctx, p, shadertype) : nullptr;
    for (uint32_t i = 0; s && name && i < s->function_count; i++)
        if (!strcmp(s->functions[i].name, name))
            return i;
    return GL_INVALID_INDEX;
}

static void copy_text(const char *text, GLsizei size, GLsizei *length, GLchar *out)
{
    SIZE_T n = strlen(text);
    if (size > 0 && out) {
        if (n >= (SIZE_T)size)
            n = (SIZE_T)size - 1;
        memcpy(out, text, n);
        out[n] = 0;
    } else {
        n = 0;
    }
    if (length)
        *length = (GLsizei)n;
}

GLAPI void APIENTRY glGetActiveSubroutineUniformiv(GLuint program, GLenum shadertype, GLuint index, GLenum pname,
                                                    GLint *values)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = program_of(ctx, program);
    GlSubroutines *s = p ? stage_of(ctx, p, shadertype) : nullptr;
    if (!p || !values)
        return;
    if (!s || index >= s->uniform_count) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    const SubUniform *u = &s->uniforms[index];
    switch (pname) {
    case GL_NUM_COMPATIBLE_SUBROUTINES:
    case GL_COMPATIBLE_SUBROUTINES: {
        GLint n = 0;
        for (uint32_t k = 0; k < s->function_count; k++)
            if (s->functions[k].types & (1ull << u->type)) {
                if (pname == GL_COMPATIBLE_SUBROUTINES)
                    values[n] = (GLint)k;
                n++;
            }
        if (pname == GL_NUM_COMPATIBLE_SUBROUTINES)
            *values = n;
        break;
    }
    case GL_UNIFORM_SIZE:
        *values = u->size ? (GLint)u->size : 1;
        break;
    case GL_UNIFORM_NAME_LENGTH:
        *values = (GLint)strlen(u->name) + 1;
        break;
    default:
        gl_error(ctx, GL_INVALID_ENUM);
        break;
    }
}

GLAPI void APIENTRY glGetActiveSubroutineUniformName(GLuint program, GLenum shadertype, GLuint index, GLsizei bufsize,
                                                      GLsizei *length, GLchar *name)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = program_of(ctx, program);
    GlSubroutines *s = p ? stage_of(ctx, p, shadertype) : nullptr;
    if (!p)
        return;
    if (!s || index >= s->uniform_count || bufsize < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    copy_text(s->uniforms[index].name, bufsize, length, name);
}

GLAPI void APIENTRY glGetActiveSubroutineName(GLuint program, GLenum shadertype, GLuint index, GLsizei bufsize,
                                               GLsizei *length, GLchar *name)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = program_of(ctx, program);
    GlSubroutines *s = p ? stage_of(ctx, p, shadertype) : nullptr;
    if (!p)
        return;
    if (!s || index >= s->function_count || bufsize < 0) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    copy_text(s->functions[index].name, bufsize, length, name);
}

GLAPI void APIENTRY glGetProgramStageiv(GLuint program, GLenum shadertype, GLenum pname, GLint *values)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = program_of(ctx, program);
    if (!p || !values)
        return;
    int stage = stage_index(shadertype);
    if (stage < 0) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    GlSubroutines *s = stage_of(ctx, p, shadertype);
    GLint longest = 0;
    switch (pname) {
    case GL_ACTIVE_SUBROUTINE_UNIFORMS:
        *values = s ? (GLint)s->uniform_count : 0;
        break;
    case GL_ACTIVE_SUBROUTINE_UNIFORM_LOCATIONS:
        *values = s ? (GLint)s->locations : 0;
        break;
    case GL_ACTIVE_SUBROUTINES:
        *values = s ? (GLint)s->function_count : 0;
        break;
    case GL_ACTIVE_SUBROUTINE_UNIFORM_MAX_LENGTH:
        for (uint32_t i = 0; s && i < s->uniform_count; i++)
            longest = (GLint)strlen(s->uniforms[i].name) + 1 > longest ? (GLint)strlen(s->uniforms[i].name) + 1 : longest;
        *values = longest;
        break;
    case GL_ACTIVE_SUBROUTINE_MAX_LENGTH:
        for (uint32_t i = 0; s && i < s->function_count; i++)
            longest = (GLint)strlen(s->functions[i].name) + 1 > longest ? (GLint)strlen(s->functions[i].name) + 1 : longest;
        *values = longest;
        break;
    default:
        gl_error(ctx, GL_INVALID_ENUM);
        break;
    }
}

/* the uniform of the program that holds the function selected at a location of a stage. */
static GLint selector_location(GlProgram *p, int stage, const GlSubroutines *s, GLuint location)
{
    for (uint32_t i = 0; i < s->uniform_count; i++) {
        const SubUniform *u = &s->uniforms[i];
        uint32_t span = u->size ? u->size : 1;
        if (location < u->location || location >= u->location + span)
            continue;
        char name[160];
        _snprintf(name, sizeof(name), u->size ? "mime_su_%c_%s[0]" : "mime_su_%c_%s", g_stage_char[stage], u->name);
        for (uint32_t k = 0; k < p->uniform_count; k++)
            if (!strcmp(p->uniforms[k].name, name))
                return (GLint)(k * 4096 + (location - u->location));
        return -1;
    }
    return -1;
}

GLAPI void APIENTRY glUniformSubroutinesuiv(GLenum shadertype, GLsizei count, const GLuint *indices)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_draw_program(ctx);
    int stage = stage_index(shadertype);
    if (stage < 0) {
        gl_error(ctx, GL_INVALID_ENUM);
        return;
    }
    if (!p || !gl_program_ready(p)) {
        gl_error(ctx, GL_INVALID_OPERATION);
        return;
    }
    GlSubroutines *s = p->subroutines[stage];
    if (!s || count < 0 || (GLuint)count != s->locations || (count && !indices)) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    for (GLsizei i = 0; i < count; i++) {
        if (indices[i] >= s->function_count) {
            gl_error(ctx, GL_INVALID_VALUE);
            return;
        }
    }
    for (GLsizei i = 0; i < count; i++) {
        GLint location = selector_location(p, stage, s, (GLuint)i);
        if (location >= 0)
            gl_program_set_int(ctx, p, location, (GLint)indices[i]);
    }
}

GLAPI void APIENTRY glGetUniformSubroutineuiv(GLenum shadertype, GLint location, GLuint *params)
{
    GL_CONTEXT_OR_RETURN();
    GlProgram *p = gl_draw_program(ctx);
    int stage = stage_index(shadertype);
    if (stage < 0 || !p || !gl_program_ready(p)) {
        gl_error(ctx, stage < 0 ? GL_INVALID_ENUM : GL_INVALID_OPERATION);
        return;
    }
    GlSubroutines *s = p->subroutines[stage];
    GLint u = s && location >= 0 ? selector_location(p, stage, s, (GLuint)location) : -1;
    if (u < 0 || !params) {
        gl_error(ctx, GL_INVALID_VALUE);
        return;
    }
    GLint value = 0;
    gl_program_get_int(p, u, &value);
    *params = (GLuint)value;
}
