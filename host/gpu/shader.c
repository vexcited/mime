/*
 * compiles GLSL to SPIR-V for Vulkan or MSL for Metal. sources older than GLSL 3.30 are adapted with macros; relaxed
 * Vulkan rules combine loose uniforms into one block. SPIRV-Cross emits MSL and reflection supplies attribute
 * locations, uniform layout and sampler slots. Vulkan uses the SPIR-V with assigned locations and bindings. Metal
 * flips vertex Y and remaps depth to preserve OpenGL's bottom-first texture rows; Vulkan uses those rows and
 * OpenGL's depth range directly.
 */
#include "shader.h"
#include "glsl_atomics.h"
#include "glsl_compat.h"

#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#include <SPIRV-Cross/spirv_cross_c.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GL_FLOAT 0x1406
#define GL_INT 0x1404
#define GL_UNSIGNED_INT 0x1405
#define GL_BOOL 0x8B56

typedef struct Buf {
    char *data;
    size_t len, cap;
} Buf;

static void buf_add(Buf *b, const void *data, size_t len)
{
    if (b->len + len + 1 > b->cap) {
        size_t cap = b->cap ? b->cap : 256;
        while (cap < b->len + len + 1)
            cap *= 2;
        b->data = realloc(b->data, cap);
        b->cap = cap;
    }
    memcpy(b->data + b->len, data, len);
    b->len += len;
    b->data[b->len] = 0;
}

static void buf_str(Buf *b, const char *s)
{
    buf_add(b, s, strlen(s));
}

static void buf_fmt(Buf *b, const char *fmt, ...)
{
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0)
        buf_add(b, tmp, (size_t)n < sizeof(tmp) ? (size_t)n : sizeof(tmp) - 1);
}

static bool is_ident(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static bool uses(const char *src, const char *word)
{
    size_t n = strlen(word);
    for (const char *p = strstr(src, word); p; p = strstr(p + 1, word))
        if ((p == src || !is_ident(p[-1])) && !is_ident(p[n]))
            return true;
    return false;
}

/* the highest constant index of gl_FragData plus one, 8 when an index is computed. */
static int frag_data_count(const char *src)
{
    int count = 1;
    for (const char *p = strstr(src, "gl_FragData"); p; p = strstr(p + 1, "gl_FragData")) {
        const char *q = p + 11;
        while (*q == ' ' || *q == '\t')
            q++;
        if (*q != '[')
            continue;
        q++;
        while (*q == ' ')
            q++;
        if (*q < '0' || *q > '9')
            return 8;
        int i = atoi(q);
        if (i + 1 > count)
            count = i + 1 > 8 ? 8 : i + 1;
    }
    return count;
}

/* replaces the version directive with a blank line to preserve diagnostic line numbers. */
/*
 * where the declarations can go: after the last #extension directive, outside comments, or after the conditional
 * block that holds it, so that a block left out does not take them along.
 */
static char *extensions_end(char *text)
{
    char *insert = text;
    int depth = 0;
    bool pending = false, in_comment = false;
    for (char *line = text; *line;) {
        char *eol = strchr(line, '\n');
        char *next = eol ? eol + 1 : line + strlen(line);
        char *q = line;
        if (in_comment) {
            char *end = strstr(q, "*/");
            if (!end || (eol && end > eol)) {
                line = next;
                continue;
            }
            in_comment = false;
            q = end + 2;
        }
        while (*q == ' ' || *q == '\t' || *q == '\r')
            q++;
        if (*q == '#') {
            char *d = q + 1;
            while (*d == ' ' || *d == '\t')
                d++;
            if (!strncmp(d, "if", 2))
                depth++;
            else if (!strncmp(d, "endif", 5) && depth > 0 && --depth == 0 && pending) {
                insert = next;
                pending = false;
            } else if (!strncmp(d, "extension", 9)) {
                if (depth)
                    pending = true;
                else
                    insert = next;
            }
        } else {
            for (char *c = q; c < (eol ? eol : q + strlen(q)); c++) {
                if (c[0] == '/' && c[1] == '/')
                    break;
                if (c[0] == '/' && c[1] == '*') {
                    char *end = strstr(c + 2, "*/");
                    if (!end || (eol && end > eol)) {
                        in_comment = true;
                        break;
                    }
                    c = end + 1;
                }
            }
        }
        line = next;
    }
    return insert;
}

/*
 * the built-in output a fragment shader writes: 0 for none, 1 gl_FragColor, 2 gl_FragData. the core profile has
 * neither, and a shader that asks for it only names them in the branches it wrote for older versions.
 */
static int fragment_builtin(const char *text)
{
    const char *version = strstr(text, "#version");
    const char *eol = version ? strchr(version, '\n') : nullptr;
    const char *core = version ? strstr(version, " core") : nullptr;
    if (core && (!eol || core < eol))
        return 0;
    return uses(text, "gl_FragColor") ? 1 : uses(text, "gl_FragData") ? 2 : 0;
}

static char *lift_source(const char *src, size_t len, bool fragment, const MimeGlslCompat *compat)
{
    char *text = malloc(len + 1);
    memcpy(text, src, len);
    text[len] = 0;

    int version = 110;
    bool es = false;
    char *version_line = nullptr;
    for (char *p = text; *p;) {
        char *q = p;
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')
            q++;
        if (q[0] == '/' && q[1] == '/') {
            p = strchr(q, '\n');
            if (!p)
                break;
            continue;
        }
        if (q[0] == '/' && q[1] == '*') {
            char *end = strstr(q + 2, "*/");
            if (!end)
                break;
            p = end + 2;
            continue;
        }
        if (q[0] == '#') {
            char *d = q + 1;
            while (*d == ' ' || *d == '\t')
                d++;
            if (!strncmp(d, "version", 7)) {
                version_line = q;
                version = atoi(d + 7);
                char *eol = strchr(d, '\n');
                char save = eol ? *eol : 0;
                if (eol)
                    *eol = 0;
                es = strstr(d, " es") != nullptr;
                if (eol)
                    *eol = save;
            }
        }
        break;
    }

    Buf out = {};
    bool lift = es || version < 330;
    if (lift) {
        buf_str(&out, "#version 330\n");
        if (version < 130 || es) {
            buf_str(&out, fragment ? "#define varying in\n" : "#define attribute in\n#define varying out\n");
        }
        buf_str(&out, "#define texture2D texture\n#define texture2DProj textureProj\n#define texture2DLod textureLod\n"
                      "#define texture2DProjLod textureProjLod\n#define textureCube texture\n#define textureCubeLod textureLod\n"
                      "#define texture1D texture\n#define texture3D texture\n#define texture2DRect texture\n"
                      "#define shadow2D(s, c) vec4(texture(s, c))\n#define shadow2DProj(s, c) vec4(textureProj(s, c))\n");
    } else {
        /* keeps the version and drops a compatibility profile, which SPIR-V does not have. */
        buf_fmt(&out, "#version %d\n", version);
    }

    /* the declarations of the replaced fragment outputs go after the last #extension line. */
    Buf decl = {};
    int builtin = fragment ? fragment_builtin(text) : 0;
    if (builtin) {
        if (builtin == 1) {
            buf_str(&out, "#define gl_FragColor mime_FragColor\n");
            buf_str(&decl, "layout(location = 0) out vec4 mime_FragColor;\n");
        } else {
            buf_str(&out, "#define gl_FragData mime_FragData\n");
            buf_fmt(&decl, "layout(location = 0) out vec4 mime_FragData[%d];\n", frag_data_count(text));
        }
    }

    bool builtins = compat && compat->active;
    if (builtins) {
        const typeof(compat->stage[0]) *stage = &compat->stage[fragment ? 1 : 0];
        mime_glsl_compat_blank(compat, fragment, text);
        buf_str(&out, stage->header);
        buf_str(&decl, stage->decl);
    }

    if (version_line) {
        char *eol = strchr(version_line, '\n');
        for (char *p = version_line; *p && p != eol; p++)
            *p = ' ';
    }
    char *insert = extensions_end(text);
    buf_str(&out, "#line 1\n");
    if (decl.len && insert != text) {
        buf_add(&out, text, (size_t)(insert - text));
        buf_add(&out, decl.data, decl.len);
        buf_str(&out, insert);
    } else {
        if (decl.len)
            buf_add(&out, decl.data, decl.len);
        buf_str(&out, text);
    }
    if (builtins) {
        buf_str(&out, "\n");
        buf_str(&out, compat->stage[fragment ? 1 : 0].tail);
    }
    free(decl.data);
    free(text);
    return out.data;
}

/* ------------------------------------------------------------- reflection */

static uint32_t gl_type_of(spvc_compiler c, spvc_type t)
{
    spvc_basetype base = spvc_type_get_basetype(t);
    unsigned vec = spvc_type_get_vector_size(t), cols = spvc_type_get_columns(t);
    if (base == SPVC_BASETYPE_SAMPLED_IMAGE || base == SPVC_BASETYPE_IMAGE) {
        SpvDim dim = spvc_type_get_image_dimension(t);
        bool depth = spvc_type_get_image_is_depth(t), arrayed = spvc_type_get_image_arrayed(t);
        spvc_basetype sampled = spvc_type_get_basetype(spvc_compiler_get_type_handle(c, spvc_type_get_image_sampled_type(t)));
        uint32_t prefix = sampled == SPVC_BASETYPE_INT32 ? 1 : sampled == SPVC_BASETYPE_UINT32 ? 2 : 0;
        switch (dim) {
        case SpvDim1D:
            return depth ? 0x8B61 : arrayed ? 0x8DC0 : prefix == 1 ? 0x8DC9 : prefix == 2 ? 0x8DD1 : 0x8B5D;
        case SpvDim3D:
            return prefix == 1 ? 0x8DCB : prefix == 2 ? 0x8DD3 : 0x8B5F;
        case SpvDimCube:
            if (arrayed)
                return depth ? 0x900D : prefix == 1 ? 0x900E : prefix == 2 ? 0x900F : 0x900C;
            return depth ? 0x8DC5 : prefix == 1 ? 0x8DCC : prefix == 2 ? 0x8DD4 : 0x8B60;
        case SpvDimRect:
            return depth ? 0x8B64 : 0x8B63;
        case SpvDimBuffer:
            return prefix == 1 ? 0x8DD0 : prefix == 2 ? 0x8DD8 : 0x8DC2;
        default:
            if (spvc_type_get_image_multisampled(t))
                return 0x9108;
            if (arrayed)
                return depth ? 0x8DC4 : prefix == 1 ? 0x8DCF : prefix == 2 ? 0x8DD7 : 0x8DC1;
            return depth ? 0x8B62 : prefix == 1 ? 0x8DCA : prefix == 2 ? 0x8DD2 : 0x8B5E;
        }
    }
    if (cols > 1) {
        static const uint32_t mats[3][3] = {{0x8B5A, 0x8B65, 0x8B66}, {0x8B67, 0x8B5B, 0x8B68}, {0x8B69, 0x8B6A, 0x8B5C}};
        static const uint32_t dmats[3][3] = {{0x8F46, 0x8F49, 0x8F4A}, {0x8F4B, 0x8F47, 0x8F4C}, {0x8F4D, 0x8F4E, 0x8F48}};
        if (cols <= 4 && vec >= 2 && vec <= 4)
            return (base == SPVC_BASETYPE_FP64 ? dmats : mats)[cols - 2][vec - 2];
        return GL_FLOAT;
    }
    switch (base) {
    case SPVC_BASETYPE_INT32:
        return vec == 1 ? GL_INT : 0x8B53 + (vec - 2);
    case SPVC_BASETYPE_UINT32:
        return vec == 1 ? GL_UNSIGNED_INT : 0x8DC6 + (vec - 2);
    case SPVC_BASETYPE_BOOLEAN:
        return vec == 1 ? GL_BOOL : 0x8B57 + (vec - 2);
    case SPVC_BASETYPE_FP64:
        return vec == 1 ? 0x140A : 0x8FFC + (vec - 2);
    default:
        return vec == 1 ? GL_FLOAT : 0x8B50 + (vec - 2);
    }
}

/* GL_IMAGE_1D and the integer and unsigned forms follow each other in the same order of dimensions. */
static uint32_t gl_image_type_of(spvc_compiler c, spvc_type t)
{
    spvc_basetype sampled = spvc_type_get_basetype(spvc_compiler_get_type_handle(c, spvc_type_get_image_sampled_type(t)));
    uint32_t base = sampled == SPVC_BASETYPE_INT32 ? 0x9057 : sampled == SPVC_BASETYPE_UINT32 ? 0x9062 : 0x904C;
    bool arrayed = spvc_type_get_image_arrayed(t), ms = spvc_type_get_image_multisampled(t);
    switch (spvc_type_get_image_dimension(t)) {
    case SpvDim1D:
        return base + (arrayed ? 6 : 0);
    case SpvDim3D:
        return base + 2;
    case SpvDimCube:
        return base + (arrayed ? 8 : 4);
    case SpvDimRect:
        return base + 3;
    case SpvDimBuffer:
        return base + 5;
    default:
        return base + (ms ? (arrayed ? 10 : 9) : arrayed ? 7 : 1);
    }
}

static unsigned array_size_of(spvc_type t)
{
    if (spvc_type_get_num_array_dimensions(t) == 0)
        return 0;
    return spvc_type_get_array_dimension(t, 0);
}

typedef struct Uniform {
    char *name;
    MimeGpuUniformInfo info;
} Uniform;

typedef struct Attrib {
    char *name;
    MimeGpuAttribInfo info;
    unsigned slots;
} Attrib;

typedef struct Program {
    Uniform *uniforms;
    unsigned uniform_count, uniform_cap;
    Attrib *attribs;
    unsigned attrib_count;
    uint32_t uniform_bytes;
    struct {
        char *name;
        MimeGpuUniformBlockInfo info;
    } blocks[MIME_GPU_MAX_UNIFORM_BLOCKS];
    unsigned block_count;
    struct {
        char *name;
        MimeGpuStorageBlockInfo info;
    } storage[MIME_GPU_MAX_STORAGE_BLOCKS];
    unsigned storage_count, image_count;
    struct {
        char *name;
        MimeGpuCounterInfo info;
    } counters[64];
    unsigned counter_count;
    uint32_t local_size[3];
    MimeShaderBinding bindings[1 + MIME_GPU_MAX_UNIFORM_BLOCKS + MIME_GPU_STAGES * MIME_GPU_MAX_TEXTURES +
                               MIME_GPU_MAX_STORAGE_BLOCKS + MIME_GPU_MAX_IMAGES + MIME_GPU_MAX_COUNTER_BUFFERS];
    unsigned binding_count;
    unsigned next_slot[MIME_GPU_STAGES]; /* the next texture slot of each family of stages. */
    Buf log;
} Program;

/* a uniform buffer both stages may share, or a sampler of one stage. */
static void add_binding(Program *p, uint32_t binding, uint32_t count, uint32_t mask, uint32_t kind, uint32_t slot,
                        uint32_t gl_type, uint32_t bytes)
{
    for (unsigned i = 0; i < p->binding_count; i++) {
        if (p->bindings[i].binding == binding) {
            p->bindings[i].stages |= mask;
            if (bytes > p->bindings[i].bytes)
                p->bindings[i].bytes = bytes;
            return;
        }
    }
    p->bindings[p->binding_count++] = (MimeShaderBinding){binding, count, mask, kind, slot, gl_type, bytes};
}

/*
 * Vulkan has no sampler for a texel buffer: samplerBuffer becomes textureBuffer, which texelFetch takes alone, and
 * textureSize on it needs the samplerless functions.
 */
static char *texel_buffers(char *text)
{
    static const char *const names[3][2] = {
        {"samplerBuffer", "textureBuffer"}, {"isamplerBuffer", "itextureBuffer"}, {"usamplerBuffer", "utextureBuffer"}};
    if (!strstr(text, "samplerBuffer"))
        return text;
    Buf out = {};
    const char *eol = strchr(text, '\n');
    if (eol) {
        buf_add(&out, text, (size_t)(eol - text + 1));
        buf_str(&out, "#extension GL_EXT_samplerless_texture_functions : enable\n");
        text = (char *)eol + 1;
    }
    for (const char *p = text; *p;) {
        bool done = false;
        for (int k = 0; k < 3 && !done; k++) {
            size_t n = strlen(names[k][0]);
            if (!strncmp(p, names[k][0], n) && !is_ident(p[n]) && (p == text || !is_ident(p[-1]))) {
                buf_str(&out, names[k][1]);
                p += n;
                done = true;
            }
        }
        if (!done)
            buf_add(&out, p++, 1);
    }
    return out.data;
}

/* lifts the source of a stage and takes its atomic counters into the program. */
static char *lift_stage(Program *p, const char *src, size_t len, bool fragment, uint32_t bit, const MimeGlslCompat *compat)
{
    MimeAtomicDecl decls[MIME_ATOMICS_MAX];
    unsigned count = 0;
    char *rewritten = mime_glsl_atomics(src, len, decls, &count);
    char *text = rewritten ? lift_source(rewritten, strlen(rewritten), fragment, compat) : lift_source(src, len, fragment, compat);
    free(rewritten);
    if (text) {
        char *replaced = texel_buffers(text);
        if (replaced != text)
            free(text);
        text = replaced;
    }
    for (unsigned i = 0; i < count; i++) {
        unsigned k;
        for (k = 0; k < p->counter_count; k++)
            if (!strcmp(p->counters[k].name, decls[i].name) && p->counters[k].info.binding == decls[i].binding)
                break;
        if (k == p->counter_count) {
            if (k == sizeof(p->counters) / sizeof(p->counters[0]))
                continue;
            p->counters[k].name = strdup(decls[i].name);
            p->counters[k].info.binding = decls[i].binding;
            p->counters[k].info.offset = decls[i].offset;
            p->counters[k].info.array_size = decls[i].count;
            p->counter_count++;
        }
        p->counters[k].info.stages |= bit;
    }
    return text;
}

static Uniform *find_uniform(Program *p, const char *name)
{
    for (unsigned i = 0; i < p->uniform_count; i++)
        if (!strcmp(p->uniforms[i].name, name))
            return &p->uniforms[i];
    return nullptr;
}

static Uniform *add_uniform(Program *p, const char *name)
{
    Uniform *u = find_uniform(p, name);
    if (u)
        return u;
    if (p->uniform_count == p->uniform_cap) {
        p->uniform_cap = p->uniform_cap ? p->uniform_cap * 2 : 16;
        p->uniforms = realloc(p->uniforms, p->uniform_cap * sizeof(Uniform));
    }
    u = &p->uniforms[p->uniform_count++];
    memset(u, 0, sizeof(*u));
    u->name = strdup(name);
    u->info.texture[0] = u->info.texture[1] = -1;
    u->info.binding = -1;
    return u;
}

/* flattens block members using OpenGL naming rules. */
static void reflect_struct(Program *p, spvc_compiler c, spvc_type_id type_id, const char *prefix, uint32_t base,
                           uint32_t block)
{
    spvc_type t = spvc_compiler_get_type_handle(c, type_id);
    unsigned n = spvc_type_get_num_member_types(t);
    for (unsigned i = 0; i < n; i++) {
        spvc_type_id mid = spvc_type_get_member_type(t, i);
        spvc_type mt = spvc_compiler_get_type_handle(c, mid);
        const char *mname = spvc_compiler_get_member_name(c, type_id, i);
        unsigned offset = 0, astride = 0, mstride = 0;
        spvc_compiler_type_struct_member_offset(c, t, i, &offset);
        unsigned count = array_size_of(mt);
        if (count)
            spvc_compiler_type_struct_member_array_stride(c, t, i, &astride);
        if (spvc_type_get_columns(mt) > 1)
            spvc_compiler_type_struct_member_matrix_stride(c, t, i, &mstride);
        char name[256];
        if (spvc_type_get_basetype(mt) == SPVC_BASETYPE_STRUCT) {
            unsigned elems = count ? count : 1;
            for (unsigned e = 0; e < elems; e++) {
                if (count)
                    snprintf(name, sizeof(name), "%s%s[%u].", prefix, mname, e);
                else
                    snprintf(name, sizeof(name), "%s%s.", prefix, mname);
                reflect_struct(p, c, spvc_type_get_base_type_id(mt), name, base + offset + e * astride, block);
            }
            continue;
        }
        snprintf(name, sizeof(name), count ? "%s%s[0]" : "%s%s", prefix, mname);
        Uniform *u = add_uniform(p, name);
        u->info.gl_type = gl_type_of(c, mt);
        u->info.array_size = count ? count : 1;
        u->info.offset = base + offset;
        u->info.array_stride = astride;
        u->info.matrix_stride = mstride;
        u->info.block = block;
    }
}

static const spvc_reflected_resource *resources(spvc_compiler c, spvc_resource_type type, size_t *count)
{
    spvc_resources res;
    const spvc_reflected_resource *list = nullptr;
    *count = 0;
    if (type == SPVC_RESOURCE_TYPE_UNIFORM_BUFFER || type == SPVC_RESOURCE_TYPE_SAMPLED_IMAGE ||
        type == SPVC_RESOURCE_TYPE_SEPARATE_IMAGE ||
        type == SPVC_RESOURCE_TYPE_STORAGE_BUFFER || type == SPVC_RESOURCE_TYPE_STORAGE_IMAGE) {
        spvc_set active;
        if (spvc_compiler_get_active_interface_variables(c, &active) != SPVC_SUCCESS ||
            spvc_compiler_create_shader_resources_for_active_variables(c, &res, active) != SPVC_SUCCESS)
            return nullptr;
    } else if (spvc_compiler_create_shader_resources(c, &res) != SPVC_SUCCESS) {
        return nullptr;
    }
    spvc_resources_get_resource_list_for_type(res, type, &list, count);
    return list;
}

static bool is_builtin(spvc_compiler c, spvc_variable_id id)
{
    return spvc_compiler_has_decoration(c, id, SpvDecorationBuiltIn);
}

/* binding is "name\0location\0" pairs. */
static int bound_location(const char *bindings, size_t len, const char *name)
{
    const char *p = bindings, *end = bindings + len;
    while (p < end) {
        const char *loc = p + strlen(p) + 1;
        if (loc >= end)
            break;
        if (!strcmp(p, name))
            return atoi(loc);
        p = loc + strlen(loc) + 1;
    }
    return -1;
}

/* preprocessing expands location macros and removes inactive declarations. */
static bool explicit_input_location(const char *source, const char *name)
{
    unsigned braces = 0;
    bool input = false, layout = false, location = false, named = false;
    for (const char *s = source; *s;) {
        if (*s == '#') {
            while (*s && *s != '\n') s++;
            continue;
        } else if (*s == '{') {
            braces++;
        } else if (*s == '}') {
            if (braces) braces--;
            input = layout = location = named = false;
        } else if (!braces && *s == ';') {
            if (input && location && named) return true;
            input = layout = location = named = false;
        } else if (!braces && *s == ')') {
            layout = false;
        } else if (!braces && is_ident(*s)) {
            const char *start = s;
            while (is_ident(*s)) s++;
            size_t length = (size_t)(s - start);
            input |= length == 2 && !memcmp(start, "in", 2);
            layout |= length == 6 && !memcmp(start, "layout", 6);
            location |= layout && length == 8 && !memcmp(start, "location", 8);
            named |= length == strlen(name) && !memcmp(start, name, length);
            continue;
        }
        s++;
    }
    return false;
}

static bool assign_attribs(Program *p, spvc_compiler c, const char *bindings, size_t bindings_len,
                           const char *source)
{
    size_t n;
    const spvc_reflected_resource *in = resources(c, SPVC_RESOURCE_TYPE_STAGE_INPUT, &n);
    p->attribs = calloc(n ? n : 1, sizeof(Attrib));
    uint32_t used = 0;
    for (size_t i = 0; i < n; i++) {
        if (is_builtin(c, in[i].id))
            continue;
        spvc_type t = spvc_compiler_get_type_handle(c, in[i].type_id);
        Attrib *a = &p->attribs[p->attrib_count++];
        a->name = strdup(in[i].name);
        a->info.gl_type = gl_type_of(c, t);
        a->info.array_size = array_size_of(t) ? array_size_of(t) : 1;
        a->slots = (spvc_type_get_columns(t) > 1 ? spvc_type_get_columns(t) : 1) * a->info.array_size;
        int loc = explicit_input_location(source, a->name) ?
            (int)spvc_compiler_get_decoration(c, in[i].id, SpvDecorationLocation) :
            bound_location(bindings, bindings_len, a->name);
        a->info.location = loc >= 0 && loc < MIME_GPU_MAX_ATTRIBS ? (uint32_t)loc : UINT32_MAX;
        if (loc >= MIME_GPU_MAX_ATTRIBS || !a->slots || a->slots > MIME_GPU_MAX_ATTRIBS ||
            (a->info.location != UINT32_MAX && a->info.location + a->slots > MIME_GPU_MAX_ATTRIBS)) {
            buf_fmt(&p->log, "vertex input %s exceeds the attribute limit\n", a->name);
            return false;
        }
        if (a->info.location != UINT32_MAX)
            for (unsigned s = 0; s < a->slots && a->info.location + s < 32; s++)
                used |= 1u << (a->info.location + s);
    }
    /* the others take the lowest free locations, in declaration order. */
    for (unsigned i = 0; i < p->attrib_count; i++) {
        Attrib *a = &p->attribs[i];
        if (a->info.location != UINT32_MAX)
            continue;
        for (uint32_t loc = 0; loc + a->slots <= MIME_GPU_MAX_ATTRIBS; loc++) {
            uint32_t mask = ((1u << a->slots) - 1) << loc;
            if (!(used & mask)) {
                a->info.location = loc;
                used |= mask;
                break;
            }
        }
        if (a->info.location == UINT32_MAX) {
            buf_str(&p->log, "vertex inputs exceed the available attribute locations\n");
            return false;
        }
    }
    unsigned k = 0;
    for (size_t i = 0; i < n; i++)
        if (!is_builtin(c, in[i].id))
            spvc_compiler_set_decoration(c, in[i].id, SpvDecorationLocation, p->attribs[k++].info.location);
    return true;
}

/* matches fragment inputs to vertex outputs by name. */
static void match_varyings(spvc_compiler vs, spvc_compiler fs)
{
    size_t vn, fn;
    const spvc_reflected_resource *out = resources(vs, SPVC_RESOURCE_TYPE_STAGE_OUTPUT, &vn);
    spvc_reflected_resource *outs = malloc((vn ? vn : 1) * sizeof(*outs));
    memcpy(outs, out, vn * sizeof(*outs));
    const spvc_reflected_resource *in = resources(fs, SPVC_RESOURCE_TYPE_STAGE_INPUT, &fn);
    for (size_t i = 0; i < fn; i++) {
        if (is_builtin(fs, in[i].id))
            continue;
        for (size_t j = 0; j < vn; j++) {
            if (!is_builtin(vs, outs[j].id) && !strcmp(outs[j].name, in[i].name) &&
                spvc_compiler_has_decoration(vs, outs[j].id, SpvDecorationLocation)) {
                spvc_compiler_set_decoration(fs, in[i].id, SpvDecorationLocation,
                                             spvc_compiler_get_decoration(vs, outs[j].id, SpvDecorationLocation));
                break;
            }
        }
    }
    free(outs);
}


/*
 * the binding of layout(binding = n) on the declaration of name, read from the preprocessed source, or -1. glslang gives
 * every resource a binding, so the decoration cannot tell an explicit one from an assigned one.
 */
static int explicit_binding(const char *source, const char *name)
{
    size_t length = strlen(name);
    for (const char *s = strstr(source, "layout"); s; s = strstr(s + 6, "layout")) {
        if ((s != source && is_ident(s[-1])) || is_ident(s[6]))
            continue;
        const char *q = s + 6;
        while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')
            q++;
        if (*q != '(')
            continue;
        const char *qualifiers = q + 1, *close = strchr(q, ')');
        if (!close)
            break;
        const char *decl = close + 1, *end = decl;
        while (*end && *end != ';' && *end != '{')
            end++;
        bool named = false;
        for (const char *d = decl; d < end;) {
            if (!is_ident(*d)) {
                d++;
                continue;
            }
            const char *start = d;
            while (d < end && is_ident(*d))
                d++;
            named |= (size_t)(d - start) == length && !memcmp(start, name, length);
        }
        if (!named)
            continue;
        for (const char *b = qualifiers; b < close; b++) {
            if (!(b == qualifiers || !is_ident(b[-1])) || strncmp(b, "binding", 7) || is_ident(b[7]))
                continue;
            const char *v = b + 7;
            while (v < close && (*v == ' ' || *v == '\t'))
                v++;
            if (v < close && *v == '=')
                return atoi(v + 1);
        }
    }
    return -1;
}

/* shader storage blocks take a slot each by name, images a slot each by name, both shared by the stages. */
static bool bind_storage(Program *p, spvc_compiler c, int stage, int bit, const char *source)
{
    size_t n;
    const spvc_reflected_resource *ssbo = resources(c, SPVC_RESOURCE_TYPE_STORAGE_BUFFER, &n);
    for (size_t i = 0; i < n; i++) {
        const char *name = spvc_compiler_get_name(c, ssbo[i].base_type_id);
        if (!name || !*name)
            name = ssbo[i].name;
        spvc_type t = spvc_compiler_get_type_handle(c, ssbo[i].base_type_id);
        size_t size = 0;
        spvc_compiler_get_declared_struct_size(c, t, &size);
        unsigned index;
        for (index = 0; index < p->storage_count; index++)
            if (!strcmp(p->storage[index].name, name))
                break;
        if (index == MIME_GPU_MAX_STORAGE_BLOCKS) {
            buf_str(&p->log, "shader storage block count exceeds implementation limits\n");
            return false;
        }
        if (index == p->storage_count) {
            p->storage[index].name = strdup(name);
            p->storage[index].info.binding = explicit_binding(source, name);
            p->storage[index].info.atomic = !strncmp(name, "mime_acb_", 9);
            p->storage_count++;
        }
        p->storage[index].info.bytes = (uint32_t)size;
        p->storage[index].info.stages |= (uint32_t)bit;
        p->storage[index].info.variables = spvc_type_get_num_member_types(t);
        spvc_compiler_set_decoration(c, ssbo[i].id, SpvDecorationDescriptorSet, 0);
        spvc_compiler_set_decoration(c, ssbo[i].id, SpvDecorationBinding, MIME_SHADER_STORAGE_BINDING(index));
        add_binding(p, MIME_SHADER_STORAGE_BINDING(index), 1, bit, MIME_SHADER_STORAGE, index, 0, (uint32_t)size);
    }
    const spvc_reflected_resource *img = resources(c, SPVC_RESOURCE_TYPE_STORAGE_IMAGE, &n);
    for (size_t i = 0; i < n; i++) {
        spvc_type t = spvc_compiler_get_type_handle(c, img[i].type_id);
        unsigned count = array_size_of(t) ? array_size_of(t) : 1;
        char name[256];
        snprintf(name, sizeof(name), array_size_of(t) ? "%s[0]" : "%s", img[i].name);
        Uniform *u = find_uniform(p, name);
        if (!u) {
            if (p->image_count + count > MIME_GPU_MAX_IMAGES) {
                buf_str(&p->log, "images exceed the image units\n");
                return false;
            }
            u = add_uniform(p, name);
            u->info.gl_type = gl_image_type_of(c, t);
            u->info.array_size = count;
            u->info.texture[0] = (int32_t)p->image_count;
            u->info.binding = explicit_binding(source, img[i].name);
            p->image_count += count;
        }
        unsigned slot = (unsigned)u->info.texture[0];
        spvc_compiler_set_decoration(c, img[i].id, SpvDecorationDescriptorSet, 0);
        spvc_compiler_set_decoration(c, img[i].id, SpvDecorationBinding, MIME_SHADER_IMAGE_BINDING(slot));
        add_binding(p, MIME_SHADER_IMAGE_BINDING(slot), count, bit, MIME_SHADER_IMAGE, slot, u->info.gl_type, 0);
    }
    return true;
}

/* loose uniforms use buffer zero; named blocks occupy distinct slots below vertex buffers. */
static bool bind_resources(Program *p, spvc_compiler c, int stage, int bit, SpvExecutionModel model, const char *source)
{
    size_t n;
    const spvc_reflected_resource *ubo = resources(c, SPVC_RESOURCE_TYPE_UNIFORM_BUFFER, &n);
    for (size_t i = 0; i < n; i++) {
        const char *name = spvc_compiler_get_name(c, ubo[i].base_type_id);
        if (!name || !*name)
            name = ubo[i].name;
        bool loose = !strcmp(name, "gl_DefaultUniformBlock");
        size_t size = 0;
        spvc_compiler_get_declared_struct_size(c, spvc_compiler_get_type_handle(c, ubo[i].base_type_id), &size);
        uint32_t slot = 0;
        if (loose) {
            reflect_struct(p, c, ubo[i].base_type_id, "", 0, 0);
            if (size > p->uniform_bytes)
                p->uniform_bytes = (uint32_t)size;
        } else {
            unsigned index;
            for (index = 0; index < p->block_count; index++)
                if (!strcmp(p->blocks[index].name, name))
                    break;
            if (index == MIME_GPU_MAX_UNIFORM_BLOCKS || size > 16384) {
                buf_str(&p->log, "uniform block count or size exceeds implementation limits\n");
                return false;
            }
            if (index == p->block_count) {
                p->blocks[index].name = strdup(name);
                p->block_count++;
            }
            p->blocks[index].info.bytes = (uint32_t)size;
            p->blocks[index].info.stages |= (uint32_t)bit;
            slot = index + 1;
            /* the members are active uniforms named by the block when it has an instance name. */
            char prefix[256] = "";
            const char *instance = spvc_compiler_get_name(c, ubo[i].id);
            if (instance && *instance)
                snprintf(prefix, sizeof(prefix), "%s.", name);
            reflect_struct(p, c, ubo[i].base_type_id, prefix, 0, index + 1);
        }
        spvc_compiler_set_decoration(c, ubo[i].id, SpvDecorationDescriptorSet, 0);
        spvc_compiler_set_decoration(c, ubo[i].id, SpvDecorationBinding, slot);
        add_binding(p, slot, 1, bit, loose ? MIME_SHADER_UNIFORMS : MIME_SHADER_BLOCK, 0, 0, (uint32_t)size);
#ifdef __APPLE__
        spvc_msl_resource_binding b;
        spvc_msl_resource_binding_init(&b);
        b.stage = model;
        b.desc_set = 0;
        b.binding = slot;
        b.msl_buffer = slot;
        spvc_compiler_msl_add_resource_binding(c, &b);
#endif
    }

    for (int pass = 0; pass < 2; pass++) {
    const spvc_reflected_resource *img =
        resources(c, pass ? SPVC_RESOURCE_TYPE_SEPARATE_IMAGE : SPVC_RESOURCE_TYPE_SAMPLED_IMAGE, &n);
    for (size_t i = 0; i < n; i++) {
        spvc_type t = spvc_compiler_get_type_handle(c, img[i].type_id);
        /* the images that are not combined with a sampler are the texel buffers, which are sampled without one. */
        if (pass && spvc_type_get_image_dimension(t) != SpvDimBuffer)
            continue;
        bool texel = pass != 0;
        unsigned count = array_size_of(t) ? array_size_of(t) : 1;
        unsigned slot = p->next_slot[stage];
        char uname[256];
        snprintf(uname, sizeof(uname), array_size_of(t) ? "%s[0]" : "%s", img[i].name);
        /* a sampler a stage after the first reads keeps the slot it has, the unit being the program's. */
        Uniform *known = find_uniform(p, uname);
        if (known && known->info.texture[stage] >= 0)
            slot = (unsigned)known->info.texture[stage];
        else if (slot + count > MIME_GPU_MAX_TEXTURES) {
            buf_str(&p->log, "samplers exceed the texture units of a stage\n");
            return false;
        }
#ifdef __APPLE__
        spvc_compiler_set_decoration(c, img[i].id, SpvDecorationDescriptorSet, 1);
        spvc_compiler_set_decoration(c, img[i].id, SpvDecorationBinding, 1 + i);
        spvc_msl_resource_binding b;
        spvc_msl_resource_binding_init(&b);
        b.stage = model;
        b.desc_set = 1;
        b.binding = 1 + (unsigned)i;
        b.msl_texture = slot;
        b.msl_sampler = slot;
        spvc_compiler_msl_add_resource_binding(c, &b);
#else
        spvc_compiler_set_decoration(c, img[i].id, SpvDecorationDescriptorSet, 0);
        spvc_compiler_set_decoration(c, img[i].id, SpvDecorationBinding, MIME_SHADER_TEXTURE_BINDING(stage, slot));
        add_binding(p, MIME_SHADER_TEXTURE_BINDING(stage, slot), count, bit,
                    texel ? MIME_SHADER_TEXEL : MIME_SHADER_TEXTURE, slot, gl_type_of(c, t), 0);
#endif
        Uniform *u = add_uniform(p, uname);
        u->info.gl_type = gl_type_of(c, t);
        u->info.array_size = count;
        if (u->info.texture[stage] < 0) {
            u->info.texture[stage] = (int32_t)slot;
            p->next_slot[stage] = slot + count;
        }
        if (u->info.binding < 0)
            u->info.binding = explicit_binding(source, img[i].name);
    }
    }
#ifndef __APPLE__
    return bind_storage(p, c, stage, bit, source);
#else
    return true;
#endif
}

#ifdef __APPLE__
static spvc_compiler msl_compiler(spvc_context ctx, const uint32_t *spirv, size_t words, bool vertex)
{
    spvc_parsed_ir ir;
    spvc_compiler c;
    if (spvc_context_parse_spirv(ctx, spirv, words, &ir) != SPVC_SUCCESS)
        return nullptr;
    if (spvc_context_create_compiler(ctx, SPVC_BACKEND_MSL, ir, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &c) != SPVC_SUCCESS)
        return nullptr;
    spvc_compiler_options o;
    spvc_compiler_create_compiler_options(c, &o);
    spvc_compiler_options_set_uint(o, SPVC_COMPILER_OPTION_MSL_VERSION, 20400);
    spvc_compiler_options_set_uint(o, SPVC_COMPILER_OPTION_MSL_PLATFORM, SPVC_MSL_PLATFORM_MACOS);
    if (vertex) {
        spvc_compiler_options_set_bool(o, SPVC_COMPILER_OPTION_FLIP_VERTEX_Y, SPVC_TRUE);
        spvc_compiler_options_set_bool(o, SPVC_COMPILER_OPTION_FIXUP_DEPTH_CONVENTION, SPVC_TRUE);
    }
    spvc_compiler_install_compiler_options(c, o);
    return c;
}

static char *compile_msl(Program *p, spvc_compiler c, const char *stage)
{
    const char *src = nullptr;
    if (spvc_compiler_compile(c, &src) != SPVC_SUCCESS || !src) {
        buf_fmt(&p->log, "%s: metal translation failed\n", stage);
        return nullptr;
    }
    return strdup(src);
}
#else
/* a compiler for reflection alone: the SPIR-V stays glslang's. */
static spvc_compiler msl_compiler(spvc_context ctx, const uint32_t *spirv, size_t words, bool vertex)
{
    spvc_parsed_ir ir;
    spvc_compiler c;
    if (spvc_context_parse_spirv(ctx, spirv, words, &ir) != SPVC_SUCCESS)
        return nullptr;
    if (spvc_context_create_compiler(ctx, SPVC_BACKEND_NONE, ir, SPVC_CAPTURE_MODE_TAKE_OWNERSHIP, &c) != SPVC_SUCCESS)
        return nullptr;
    return c;
}

/* writes assigned locations and bindings into SPIR-V decorations. */
static void patch_spirv(spvc_compiler c, uint32_t *words, size_t count)
{
    for (size_t i = 5; i < count;) {
        uint32_t n = words[i] >> 16, op = words[i] & 0xFFFF;
        if (!n || i + n > count)
            break;
        if (op == SpvOpDecorate && n >= 4) {
            SpvDecoration d = (SpvDecoration)words[i + 2];
            if ((d == SpvDecorationLocation || d == SpvDecorationBinding || d == SpvDecorationDescriptorSet) &&
                spvc_compiler_has_decoration(c, words[i + 1], d))
                words[i + 3] = spvc_compiler_get_decoration(c, words[i + 1], d);
        }
        i += n;
    }
}
#endif

/* ---------------------------------------------------------------- driver */

static pthread_once_t g_once = PTHREAD_ONCE_INIT;

static void init_glslang(void)
{
    glslang_initialize_process();
}

static glslang_shader_t *parse_stage(Program *p, glslang_stage_t stage, const char *text, const char *label)
{
    glslang_input_t in = {
        .language = GLSLANG_SOURCE_GLSL,
        .stage = stage,
        .client = GLSLANG_CLIENT_VULKAN,
        .client_version = GLSLANG_TARGET_VULKAN_1_0,
        .target_language = GLSLANG_TARGET_SPV,
        .target_language_version = GLSLANG_TARGET_SPV_1_0,
        .code = text,
        .default_version = 330,
        .default_profile = GLSLANG_NO_PROFILE,
        .messages = GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT,
        .resource = glslang_default_resource(),
    };
    glslang_shader_t *sh = glslang_shader_create(&in);
    glslang_shader_set_options(sh, GLSLANG_SHADER_AUTO_MAP_BINDINGS | GLSLANG_SHADER_AUTO_MAP_LOCATIONS |
                                       GLSLANG_SHADER_VULKAN_RULES_RELAXED);
    glslang_shader_set_default_uniform_block_name(sh, "gl_DefaultUniformBlock");
    glslang_shader_set_default_uniform_block_set_and_binding(sh, 0, 0);
    if (!glslang_shader_preprocess(sh, &in) || !glslang_shader_parse(sh, &in)) {
        buf_fmt(&p->log, "%s shader:\n", label);
        buf_str(&p->log, glslang_shader_get_info_log(sh));
        glslang_shader_delete(sh);
        return nullptr;
    }
    return sh;
}

static uint32_t *stage_spirv(glslang_program_t *prog, glslang_stage_t stage, size_t *words)
{
    glslang_spv_options_t opt = {};
    opt.disable_optimizer = true;
    glslang_program_SPIRV_generate_with_options(prog, stage, &opt);
    *words = glslang_program_SPIRV_get_size(prog);
    uint32_t *spirv = malloc(*words * 4);
    glslang_program_SPIRV_get(prog, spirv);
    return spirv;
}

static void write_info(Program *p, bool ok, MimeShaderResult *out)
{
    Buf b = {};
    MimeGpuProgramInfo info = {};
    buf_add(&b, &info, sizeof(info));
    info.status = ok ? 0 : 1;
    info.attrib_count = ok ? p->attrib_count : 0;
    info.attrib_offset = (uint32_t)b.len;
    for (unsigned i = 0; i < info.attrib_count; i++)
        buf_add(&b, &p->attribs[i].info, sizeof(MimeGpuAttribInfo));
    info.uniform_count = ok ? p->uniform_count : 0;
    info.uniform_offset = (uint32_t)b.len;
    for (unsigned i = 0; i < info.uniform_count; i++)
        buf_add(&b, &p->uniforms[i].info, sizeof(MimeGpuUniformInfo));
    info.block_count = ok ? p->block_count : 0;
    info.block_offset = (uint32_t)b.len;
    for (unsigned i = 0; i < info.block_count; i++)
        buf_add(&b, &p->blocks[i].info, sizeof(MimeGpuUniformBlockInfo));
    info.storage_count = ok ? p->storage_count : 0;
    info.storage_offset = (uint32_t)b.len;
    for (unsigned i = 0; i < info.storage_count; i++)
        buf_add(&b, &p->storage[i].info, sizeof(MimeGpuStorageBlockInfo));
    info.counter_count = ok ? p->counter_count : 0;
    info.counter_offset = (uint32_t)b.len;
    for (unsigned i = 0; i < info.counter_count; i++)
        buf_add(&b, &p->counters[i].info, sizeof(MimeGpuCounterInfo));
    memcpy(info.local_size, p->local_size, sizeof(info.local_size));
    info.uniform_bytes = (p->uniform_bytes + 15) & ~15u; /* Metal rounds the struct up to its alignment. */
    for (unsigned i = 0; i < info.attrib_count; i++) {
        ((MimeGpuAttribInfo *)(b.data + info.attrib_offset))[i].name_offset = (uint32_t)b.len;
        buf_add(&b, p->attribs[i].name, strlen(p->attribs[i].name) + 1);
    }
    for (unsigned i = 0; i < info.uniform_count; i++) {
        ((MimeGpuUniformInfo *)(b.data + info.uniform_offset))[i].name_offset = (uint32_t)b.len;
        buf_add(&b, p->uniforms[i].name, strlen(p->uniforms[i].name) + 1);
    }
    for (unsigned i = 0; i < info.block_count; i++) {
        ((MimeGpuUniformBlockInfo *)(b.data + info.block_offset))[i].name_offset = (uint32_t)b.len;
        buf_add(&b, p->blocks[i].name, strlen(p->blocks[i].name) + 1);
    }
    for (unsigned i = 0; i < info.storage_count; i++) {
        ((MimeGpuStorageBlockInfo *)(b.data + info.storage_offset))[i].name_offset = (uint32_t)b.len;
        buf_add(&b, p->storage[i].name, strlen(p->storage[i].name) + 1);
    }
    for (unsigned i = 0; i < info.counter_count; i++) {
        ((MimeGpuCounterInfo *)(b.data + info.counter_offset))[i].name_offset = (uint32_t)b.len;
        buf_add(&b, p->counters[i].name, strlen(p->counters[i].name) + 1);
    }
    info.log_offset = (uint32_t)b.len;
    info.log_bytes = (uint32_t)p->log.len;
    if (p->log.len)
        buf_add(&b, p->log.data, p->log.len);
    buf_add(&b, "", 1);
    while (b.len & 7)
        buf_add(&b, "", 1);
    info.size = (uint32_t)b.len;
    memcpy(b.data, &info, sizeof(info));
    out->info = (uint8_t *)b.data;
    out->info_size = (uint32_t)b.len;
}

bool mime_shader_compile(const char *vs, size_t vs_len, const char *fs, size_t fs_len, const char *bindings,
                         size_t bindings_len, MimeShaderResult *out)
{
    return mime_shader_compile_stages(vs, vs_len, fs, fs_len, nullptr, bindings, bindings_len, out);
}

bool mime_shader_compile_stages(const char *vs, size_t vs_len, const char *fs, size_t fs_len,
                                const MimeShaderStages *extra, const char *bindings, size_t bindings_len,
                                MimeShaderResult *out)
{
    memset(out, 0, sizeof(*out));
    pthread_once(&g_once, init_glslang);
    Program p = {};
    bool ok = false;
    MimeGlslCompat compat;
    {
        char *fcopy = strndup(fs, fs_len);
        int kind = fragment_builtin(fcopy);
        mime_glsl_compat_analyze(vs, vs_len, fs, fs_len, &compat, kind, kind == 2 ? frag_data_count(fcopy) : 1);
        free(fcopy);
    }
    char *vtext = lift_stage(&p, vs, vs_len, false, MIME_SHADER_BIT_VERTEX, &compat),
         *ftext = lift_stage(&p, fs, fs_len, true, MIME_SHADER_BIT_FRAGMENT, &compat);
    glslang_shader_t *vsh = parse_stage(&p, GLSLANG_STAGE_VERTEX, vtext, "vertex");
    glslang_shader_t *fsh = vsh ? parse_stage(&p, GLSLANG_STAGE_FRAGMENT, ftext, "fragment") : nullptr;
    glslang_program_t *prog = nullptr;
    spvc_context ctx = nullptr;
    glslang_shader_t *xsh[MIME_SHADER_EXTRAS] = {};
    char *xtext[MIME_SHADER_EXTRAS] = {};
    spvc_compiler xc[MIME_SHADER_EXTRAS] = {};
    static const glslang_stage_t xstage[MIME_SHADER_EXTRAS] = {GLSLANG_STAGE_TESSCONTROL, GLSLANG_STAGE_TESSEVALUATION,
                                                              GLSLANG_STAGE_GEOMETRY};
    static const char *const xlabel[MIME_SHADER_EXTRAS] = {"tessellation control", "tessellation evaluation",
                                                           "geometry"};
    static const int xbit[MIME_SHADER_EXTRAS] = {MIME_SHADER_BIT_CONTROL, MIME_SHADER_BIT_EVALUATION,
                                                 MIME_SHADER_BIT_GEOMETRY};
    static const SpvExecutionModel xmodel[MIME_SHADER_EXTRAS] = {SpvExecutionModelTessellationControl,
                                                                 SpvExecutionModelTessellationEvaluation,
                                                                 SpvExecutionModelGeometry};
    if (!fsh)
        goto done;
    if (extra) {
        const char *src[MIME_SHADER_EXTRAS] = {extra->control, extra->evaluation, extra->geometry};
        const size_t len[MIME_SHADER_EXTRAS] = {extra->control_len, extra->evaluation_len, extra->geometry_len};
        for (int x = 0; x < MIME_SHADER_EXTRAS; x++) {
            if (!src[x] || !len[x])
                continue;
#ifdef __APPLE__
            buf_fmt(&p.log, "%s shaders need Vulkan\n", xlabel[x]);
            goto done;
#endif
            xtext[x] = lift_stage(&p, src[x], len[x], false, (uint32_t)xbit[x], nullptr);
            xsh[x] = parse_stage(&p, xstage[x], xtext[x], xlabel[x]);
            if (!xsh[x])
                goto done;
        }
        if ((xsh[MIME_SHADER_CONTROL] != nullptr) != (xsh[MIME_SHADER_EVALUATION] != nullptr)) {
            buf_str(&p.log, "tessellation needs a control and an evaluation shader\n");
            goto done;
        }
    }

    prog = glslang_program_create();
    glslang_program_add_shader(prog, vsh);
    for (int x = 0; x < MIME_SHADER_EXTRAS; x++)
        if (xsh[x])
            glslang_program_add_shader(prog, xsh[x]);
    glslang_program_add_shader(prog, fsh);
    if (!glslang_program_link(prog, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT) ||
        !glslang_program_map_io(prog)) {
        buf_str(&p.log, glslang_program_get_info_log(prog));
        goto done;
    }
    size_t vwords, fwords;
    uint32_t *vspv = stage_spirv(prog, GLSLANG_STAGE_VERTEX, &vwords);
    uint32_t *fspv = stage_spirv(prog, GLSLANG_STAGE_FRAGMENT, &fwords);

    spvc_context_create(&ctx);
    spvc_compiler vc = msl_compiler(ctx, vspv, vwords, true);
    spvc_compiler fc = msl_compiler(ctx, fspv, fwords, false);
#ifdef __APPLE__
    free(vspv);
    free(fspv);
#else
    out->spirv[0] = vspv;
    out->spirv[1] = fspv;
    out->spirv_words[0] = vwords;
    out->spirv_words[1] = fwords;
    for (int x = 0; x < MIME_SHADER_EXTRAS; x++) {
        if (!xsh[x])
            continue;
        out->spirv_extra[x] = stage_spirv(prog, xstage[x], &out->extra_words[x]);
        xc[x] = msl_compiler(ctx, out->spirv_extra[x], out->extra_words[x], false);
        if (!xc[x]) {
            buf_fmt(&p.log, "spir-v: %s\n", spvc_context_get_last_error_string(ctx));
            goto done;
        }
    }
#endif
    if (!vc || !fc) {
        buf_fmt(&p.log, "spir-v: %s\n", spvc_context_get_last_error_string(ctx));
        goto done;
    }
    if (!assign_attribs(&p, vc, bindings, bindings_len, glslang_shader_get_preprocessed_code(vsh)))
        goto done;
    /* the stages between the vertex and fragment ones get their locations from the linker. */
    match_varyings(xc[MIME_SHADER_GEOMETRY]     ? xc[MIME_SHADER_GEOMETRY]
                   : xc[MIME_SHADER_EVALUATION] ? xc[MIME_SHADER_EVALUATION]
                                                : vc,
                   fc);
    if (!bind_resources(&p, vc, MIME_GPU_STAGE_VERTEX, MIME_SHADER_BIT_VERTEX, SpvExecutionModelVertex,
                        glslang_shader_get_preprocessed_code(vsh)) ||
        !bind_resources(&p, fc, MIME_GPU_STAGE_FRAGMENT, MIME_SHADER_BIT_FRAGMENT, SpvExecutionModelFragment,
                        glslang_shader_get_preprocessed_code(fsh)))
        goto done;
    for (int x = 0; x < MIME_SHADER_EXTRAS; x++)
        if (xc[x] && !bind_resources(&p, xc[x], MIME_GPU_STAGE_VERTEX, xbit[x], xmodel[x],
                                     glslang_shader_get_preprocessed_code(xsh[x])))
            goto done;
#ifdef __APPLE__
    out->msl[0] = compile_msl(&p, vc, "vertex");
    out->msl[1] = compile_msl(&p, fc, "fragment");
    if (!out->msl[0] || !out->msl[1])
        goto done;
    snprintf(out->entry[0], sizeof(out->entry[0]), "%s",
             spvc_compiler_get_cleansed_entry_point_name(vc, "main", SpvExecutionModelVertex));
    snprintf(out->entry[1], sizeof(out->entry[1]), "%s",
             spvc_compiler_get_cleansed_entry_point_name(fc, "main", SpvExecutionModelFragment));
#else
    patch_spirv(vc, out->spirv[0], out->spirv_words[0]);
    patch_spirv(fc, out->spirv[1], out->spirv_words[1]);
    for (int x = 0; x < MIME_SHADER_EXTRAS; x++)
        if (xc[x])
            patch_spirv(xc[x], out->spirv_extra[x], out->extra_words[x]);
    if (p.binding_count) {
        out->bindings = malloc(p.binding_count * sizeof(MimeShaderBinding));
        memcpy(out->bindings, p.bindings, p.binding_count * sizeof(MimeShaderBinding));
        out->binding_count = p.binding_count;
    }
    snprintf(out->entry[0], sizeof(out->entry[0]), "main");
    snprintf(out->entry[1], sizeof(out->entry[1]), "main");
#endif
    ok = true;

done:
    if (ctx)
        spvc_context_destroy(ctx);
    if (prog)
        glslang_program_delete(prog);
    if (vsh)
        glslang_shader_delete(vsh);
    if (fsh)
        glslang_shader_delete(fsh);
    for (int x = 0; x < MIME_SHADER_EXTRAS; x++) {
        if (xsh[x])
            glslang_shader_delete(xsh[x]);
        free(xtext[x]);
    }
    free(vtext);
    free(ftext);
    mime_glsl_compat_free(&compat);
    if (!ok) {
        for (int s = 0; s < MIME_GPU_STAGES; s++) {
            free(out->msl[s]);
            free(out->spirv[s]);
            out->msl[s] = nullptr;
            out->spirv[s] = nullptr;
        }
        for (int x = 0; x < MIME_SHADER_EXTRAS; x++) {
            free(out->spirv_extra[x]);
            out->spirv_extra[x] = nullptr;
        }
        free(out->bindings);
        out->bindings = nullptr;
        out->binding_count = 0;
    }
    write_info(&p, ok, out);
    out->ok = ok;
    for (unsigned i = 0; i < p.uniform_count; i++)
        free(p.uniforms[i].name);
    for (unsigned i = 0; i < p.attrib_count; i++)
        free(p.attribs[i].name);
    free(p.uniforms);
    free(p.attribs);
    for (unsigned i = 0; i < p.block_count; i++)
        free(p.blocks[i].name);
    for (unsigned i = 0; i < p.storage_count; i++)
        free(p.storage[i].name);
    for (unsigned i = 0; i < p.counter_count; i++)
        free(p.counters[i].name);
    free(p.log.data);
    return ok;
}

#ifndef __APPLE__
bool mime_shader_compile_compute(const char *cs, size_t cs_len, MimeShaderResult *out)
{
    memset(out, 0, sizeof(*out));
    pthread_once(&g_once, init_glslang);
    Program p = {};
    bool ok = false;
    char *text = lift_stage(&p, cs, cs_len, false, MIME_SHADER_BIT_VERTEX, nullptr);
    glslang_shader_t *sh = parse_stage(&p, GLSLANG_STAGE_COMPUTE, text, "compute");
    glslang_program_t *prog = nullptr;
    spvc_context ctx = nullptr;
    if (!sh)
        goto done;
    prog = glslang_program_create();
    glslang_program_add_shader(prog, sh);
    if (!glslang_program_link(prog, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT)) {
        buf_str(&p.log, glslang_program_get_info_log(prog));
        goto done;
    }
    size_t words;
    uint32_t *spv = stage_spirv(prog, GLSLANG_STAGE_COMPUTE, &words);
    out->spirv[0] = spv;
    out->spirv_words[0] = words;
    spvc_context_create(&ctx);
    spvc_compiler cc = msl_compiler(ctx, spv, words, false);
    if (!cc) {
        buf_fmt(&p.log, "spir-v: %s\n", spvc_context_get_last_error_string(ctx));
        goto done;
    }
    if (!bind_resources(&p, cc, MIME_GPU_STAGE_VERTEX, MIME_SHADER_BIT_VERTEX, SpvExecutionModelGLCompute,
                        glslang_shader_get_preprocessed_code(sh)))
        goto done;
    for (unsigned i = 0; i < 3; i++)
        p.local_size[i] = spvc_compiler_get_execution_mode_argument_by_index(cc, SpvExecutionModeLocalSize, i);
    patch_spirv(cc, out->spirv[0], out->spirv_words[0]);
    if (p.binding_count) {
        out->bindings = malloc(p.binding_count * sizeof(MimeShaderBinding));
        memcpy(out->bindings, p.bindings, p.binding_count * sizeof(MimeShaderBinding));
        out->binding_count = p.binding_count;
    }
    snprintf(out->entry[0], sizeof(out->entry[0]), "main");
    ok = true;

done:
    if (ctx)
        spvc_context_destroy(ctx);
    if (prog)
        glslang_program_delete(prog);
    if (sh)
        glslang_shader_delete(sh);
    free(text);
    if (!ok) {
        free(out->spirv[0]);
        out->spirv[0] = nullptr;
        free(out->bindings);
        out->bindings = nullptr;
        out->binding_count = 0;
    }
    write_info(&p, ok, out);
    out->ok = ok;
    for (unsigned i = 0; i < p.uniform_count; i++)
        free(p.uniforms[i].name);
    for (unsigned i = 0; i < p.block_count; i++)
        free(p.blocks[i].name);
    for (unsigned i = 0; i < p.storage_count; i++)
        free(p.storage[i].name);
    for (unsigned i = 0; i < p.counter_count; i++)
        free(p.counters[i].name);
    free(p.uniforms);
    free(p.log.data);
    return ok;
}
#else
bool mime_shader_compile_compute(const char *cs, size_t cs_len, MimeShaderResult *out)
{
    memset(out, 0, sizeof(*out));
    return false;
}
#endif

void mime_shader_free(MimeShaderResult *r)
{
    for (int s = 0; s < MIME_GPU_STAGES; s++) {
        free(r->msl[s]);
        free(r->spirv[s]);
    }
    for (int x = 0; x < MIME_SHADER_EXTRAS; x++)
        free(r->spirv_extra[x]);
    free(r->bindings);
    free(r->info);
    memset(r, 0, sizeof(*r));
}
