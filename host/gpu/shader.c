/*
 * glsl programs to metal shading language.
 *
 * the guest sends a program's glsl sources as the application wrote them.
 * sources older than glsl 3.30 are lifted to 3.30 with a few macros, then
 * glslang compiles both stages to spir-v under the relaxed vulkan rules,
 * which gather the loose uniforms into one block, and SPIRV-Cross writes
 * metal source for each stage. the reflection the guest needs to answer gl
 * queries is read from the spir-v: attribute locations, follow the
 * application's bindings, the uniform block layout and the texture slots
 * of every sampler.
 *
 * vertex positions are flipped vertically and depth is moved to metal's
 * range, so rows of every texture keep gl's order, bottom first.
 */
#include "shader.h"

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

/* whether the identifier occurs in the source, as a whole token. */
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

/*
 * returns the source as glslang should compile it. the version line is
 * replaced by a blank one so the line numbers of errors stay right.
 */
static char *lift_source(const char *src, size_t len, bool fragment)
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
        /* keeps the version and drops a compatibility profile, which spir-v does not have. */
        buf_fmt(&out, "#version %d\n", version);
    }

    /* the declarations of the replaced fragment outputs go after the last #extension line. */
    Buf decl = {};
    if (fragment && lift) {
        if (uses(text, "gl_FragColor")) {
            buf_str(&out, "#define gl_FragColor mime_FragColor\n");
            buf_str(&decl, "layout(location = 0) out vec4 mime_FragColor;\n");
        } else if (uses(text, "gl_FragData")) {
            buf_str(&out, "#define gl_FragData mime_FragData\n");
            buf_fmt(&decl, "layout(location = 0) out vec4 mime_FragData[%d];\n", frag_data_count(text));
        }
    }

    if (version_line) {
        char *eol = strchr(version_line, '\n');
        for (char *p = version_line; *p && p != eol; p++)
            *p = ' ';
    }
    char *insert = text;
    for (char *p = strstr(text, "#extension"); p; p = strstr(p + 1, "#extension")) {
        char *eol = strchr(p, '\n');
        insert = eol ? eol + 1 : p + strlen(p);
    }
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
            return depth ? 0x8DC5 : prefix == 1 ? 0x8DCC : prefix == 2 ? 0x8DD4 : 0x8B60;
        case SpvDimRect:
            return depth ? 0x8B64 : 0x8B63;
        case SpvDimBuffer:
            return 0x8DC2;
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
        if (cols <= 4 && vec >= 2 && vec <= 4)
            return mats[cols - 2][vec - 2];
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
    Buf log;
} Program;

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
    return u;
}

/* adds the members of a block struct, flattened the way gl names them. */
static void reflect_struct(Program *p, spvc_compiler c, spvc_type_id type_id, const char *prefix, uint32_t base)
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
                reflect_struct(p, c, spvc_type_get_base_type_id(mt), name, base + offset + e * astride);
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
    }
}

static const spvc_reflected_resource *resources(spvc_compiler c, spvc_resource_type type, size_t *count)
{
    spvc_resources res;
    const spvc_reflected_resource *list = nullptr;
    *count = 0;
    if (type == SPVC_RESOURCE_TYPE_UNIFORM_BUFFER || type == SPVC_RESOURCE_TYPE_SAMPLED_IMAGE) {
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

/* the fragment stage reads each varying where the vertex stage wrote the one of the same name. */
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

/* loose uniforms use buffer zero; named blocks occupy distinct slots below vertex buffers. */
static bool bind_resources(Program *p, spvc_compiler c, int stage, SpvExecutionModel model)
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
            reflect_struct(p, c, ubo[i].base_type_id, "", 0);
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
            p->blocks[index].info.stages |= 1u << stage;
            slot = index + 1;
        }
        spvc_compiler_set_decoration(c, ubo[i].id, SpvDecorationDescriptorSet, 0);
        spvc_compiler_set_decoration(c, ubo[i].id, SpvDecorationBinding, slot);
        spvc_msl_resource_binding b;
        spvc_msl_resource_binding_init(&b);
        b.stage = model;
        b.desc_set = 0;
        b.binding = slot;
        b.msl_buffer = slot;
        spvc_compiler_msl_add_resource_binding(c, &b);
    }

    const spvc_reflected_resource *img = resources(c, SPVC_RESOURCE_TYPE_SAMPLED_IMAGE, &n);
    unsigned slot = 0;
    for (size_t i = 0; i < n; i++) {
        spvc_type t = spvc_compiler_get_type_handle(c, img[i].type_id);
        unsigned count = array_size_of(t) ? array_size_of(t) : 1;
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
        char name[256];
        snprintf(name, sizeof(name), array_size_of(t) ? "%s[0]" : "%s", img[i].name);
        Uniform *u = add_uniform(p, name);
        u->info.gl_type = gl_type_of(c, t);
        u->info.array_size = count;
        u->info.texture[stage] = (int32_t)slot;
        slot += count;
    }
    return true;
}

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
    info.uniform_bytes = (p->uniform_bytes + 15) & ~15u; /* metal rounds the struct up to its alignment. */
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
    memset(out, 0, sizeof(*out));
    pthread_once(&g_once, init_glslang);
    Program p = {};
    bool ok = false;
    char *vtext = lift_source(vs, vs_len, false), *ftext = lift_source(fs, fs_len, true);
    glslang_shader_t *vsh = parse_stage(&p, GLSLANG_STAGE_VERTEX, vtext, "vertex");
    glslang_shader_t *fsh = vsh ? parse_stage(&p, GLSLANG_STAGE_FRAGMENT, ftext, "fragment") : nullptr;
    glslang_program_t *prog = nullptr;
    spvc_context ctx = nullptr;
    if (!fsh)
        goto done;

    prog = glslang_program_create();
    glslang_program_add_shader(prog, vsh);
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
    free(vspv);
    free(fspv);
    if (!vc || !fc) {
        buf_fmt(&p.log, "spir-v: %s\n", spvc_context_get_last_error_string(ctx));
        goto done;
    }
    if (!assign_attribs(&p, vc, bindings, bindings_len, glslang_shader_get_preprocessed_code(vsh)))
        goto done;
    match_varyings(vc, fc);
    if (!bind_resources(&p, vc, MIME_GPU_STAGE_VERTEX, SpvExecutionModelVertex) ||
        !bind_resources(&p, fc, MIME_GPU_STAGE_FRAGMENT, SpvExecutionModelFragment))
        goto done;
    out->msl[0] = compile_msl(&p, vc, "vertex");
    out->msl[1] = compile_msl(&p, fc, "fragment");
    if (!out->msl[0] || !out->msl[1])
        goto done;
    snprintf(out->entry[0], sizeof(out->entry[0]), "%s",
             spvc_compiler_get_cleansed_entry_point_name(vc, "main", SpvExecutionModelVertex));
    snprintf(out->entry[1], sizeof(out->entry[1]), "%s",
             spvc_compiler_get_cleansed_entry_point_name(fc, "main", SpvExecutionModelFragment));
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
    free(vtext);
    free(ftext);
    if (!ok) {
        free(out->msl[0]);
        free(out->msl[1]);
        out->msl[0] = out->msl[1] = nullptr;
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
    free(p.log.data);
    return ok;
}

void mime_shader_free(MimeShaderResult *r)
{
    free(r->msl[0]);
    free(r->msl[1]);
    free(r->info);
    memset(r, 0, sizeof(*r));
}
