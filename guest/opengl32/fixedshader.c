/*
 * the fixed function program: one vertex and one fragment shader reading their whole state from a vec4 array laid out
 * in fixed.h, so a state change is a few uniform writes and never a new program.
 */
#include "fixed.h"

#define COMMON_DEFINES(X)                                                                                              \
    X(U_MVP, FF_U_MVP)                                                                                                 \
    X(U_MV, FF_U_MV)                                                                                                   \
    X(U_NORMAL, FF_U_NORMAL)                                                                                           \
    X(U_TEXMAT, FF_U_TEXMAT)                                                                                           \
    X(U_FLAGS0, FF_U_FLAGS0)                                                                                           \
    X(U_FLAGS1, FF_U_FLAGS1)                                                                                           \
    X(U_ENV_COLOR, FF_U_ENV_COLOR)                                                                                     \
    X(U_FOG, FF_U_FOG)                                                                                                 \
    X(U_FOG_COLOR, FF_U_FOG_COLOR)                                                                                     \
    X(U_MISC, FF_U_MISC)                                                                                               \
    X(U_MODEL_AMBIENT, FF_U_MODEL_AMBIENT)                                                                             \
    X(U_EMISSION, FF_U_EMISSION)                                                                                       \
    X(U_AMBIENT, FF_U_AMBIENT)                                                                                         \
    X(U_DIFFUSE, FF_U_DIFFUSE)                                                                                         \
    X(U_SPECULAR, FF_U_SPECULAR)                                                                                       \
    X(U_SHININESS, FF_U_SHININESS)                                                                                     \
    X(U_LIGHT, FF_U_LIGHT)                                                                                             \
    X(U_LIGHT_SIZE, FF_U_LIGHT_SIZE)                                                                                   \
    X(U_COUNT, FF_U_COUNT)                                                                                             \
    X(LIGHTS, FF_LIGHTS)                                                                                               \
    X(TEX_COLOR, FF_TEX_COLOR)                                                                                         \
    X(TEX_ALPHA, FF_TEX_ALPHA)                                                                                         \
    X(TEX_INTENSITY, FF_TEX_INTENSITY)                                                                                 \
    X(ENV_MODULATE, FF_ENV_MODULATE)                                                                                   \
    X(ENV_REPLACE, FF_ENV_REPLACE)                                                                                     \
    X(ENV_DECAL, FF_ENV_DECAL)                                                                                         \
    X(ENV_BLEND, FF_ENV_BLEND)                                                                                         \
    X(ENV_ADD, FF_ENV_ADD)

static const char g_vertex_body[] =
    "layout(location = 0) in vec4 aPosition;\n"
    "layout(location = 1) in vec4 aColor;\n"
    "layout(location = 2) in vec3 aNormal;\n"
    "layout(location = 3) in vec4 aTexCoord;\n"
    "uniform vec4 u[U_COUNT];\n"
    "out vec4 vColor;\n"
    "flat out vec4 vFlatColor;\n"
    "out vec3 vSecondary;\n"
    "flat out vec3 vFlatSecondary;\n"
    "out vec4 vTexCoord;\n"
    "out float vFog;\n"
    "void main()\n"
    "{\n"
    "    mat4 mvp = mat4(u[U_MVP], u[U_MVP + 1], u[U_MVP + 2], u[U_MVP + 3]);\n"
    "    mat4 mv = mat4(u[U_MV], u[U_MV + 1], u[U_MV + 2], u[U_MV + 3]);\n"
    "    mat4 texmat = mat4(u[U_TEXMAT], u[U_TEXMAT + 1], u[U_TEXMAT + 2], u[U_TEXMAT + 3]);\n"
    "    gl_Position = mvp * aPosition;\n"
    "    gl_PointSize = u[U_MISC].x;\n"
    "    vTexCoord = texmat * aTexCoord;\n"
    "    vec4 eye = mv * aPosition;\n"
    "    vFog = abs(eye.z);\n"
    "    vec4 color = aColor;\n"
    "    vec3 secondary = vec3(0.0);\n"
    "    if (u[U_FLAGS0].x != 0.0) {\n"
    "        vec4 emission = u[U_EMISSION];\n"
    "        vec4 ambient = u[U_AMBIENT];\n"
    "        vec4 diffuse = u[U_DIFFUSE];\n"
    "        vec4 specular = u[U_SPECULAR];\n"
    "        float shininess = u[U_SHININESS].x;\n"
    "        int tracked = int(u[U_MISC].y + 0.5);\n"
    "        if (tracked == 1) emission = aColor;\n"
    "        else if (tracked == 2) ambient = aColor;\n"
    "        else if (tracked == 3) diffuse = aColor;\n"
    "        else if (tracked == 4) specular = aColor;\n"
    "        else if (tracked == 5) { ambient = aColor; diffuse = aColor; }\n"
    "        mat3 nm = mat3(u[U_NORMAL].xyz, u[U_NORMAL + 1].xyz, u[U_NORMAL + 2].xyz);\n"
    "        vec3 n = nm * aNormal;\n"
    "        if (u[U_FLAGS1].w != 0.0) n = normalize(n);\n"
    "        vec3 pos = eye.xyz / eye.w;\n"
    "        vec3 viewer = u[U_MISC].z != 0.0 ? normalize(-pos) : vec3(0.0, 0.0, 1.0);\n"
    "        vec3 lit = u[U_MODEL_AMBIENT].rgb * ambient.rgb;\n"
    "        vec3 shine = vec3(0.0);\n"
    "        for (int i = 0; i < LIGHTS; i++) {\n"
    "            int b = U_LIGHT + U_LIGHT_SIZE * i;\n"
    "            if (u[b + 6].w == 0.0) continue;\n"
    "            vec4 lp = u[b + 3];\n"
    "            vec3 vp;\n"
    "            float att = 1.0;\n"
    "            if (lp.w == 0.0) {\n"
    "                vp = normalize(lp.xyz);\n"
    "            } else {\n"
    "                vec3 d = lp.xyz / lp.w - pos;\n"
    "                float dist = length(d);\n"
    "                vp = dist > 0.0 ? d / dist : vec3(0.0, 0.0, 1.0);\n"
    "                att = 1.0 / (u[b + 6].x + u[b + 6].y * dist + u[b + 6].z * dist * dist);\n"
    "            }\n"
    "            float spot = 1.0;\n"
    "            if (u[b + 5].y != 0.0) {\n"
    "                float c = dot(-vp, u[b + 4].xyz);\n"
    "                if (c < u[b + 5].x) spot = 0.0;\n"
    "                else if (u[b + 4].w != 0.0) spot = pow(max(c, 0.0), u[b + 4].w);\n"
    "            }\n"
    "            float ndl = dot(n, vp);\n"
    "            vec3 term = u[b].rgb * ambient.rgb;\n"
    "            if (ndl > 0.0) {\n"
    "                term += ndl * u[b + 1].rgb * diffuse.rgb;\n"
    "                float ndh = max(dot(n, normalize(vp + viewer)), 0.0);\n"
    "                float f = shininess == 0.0 ? 1.0 : pow(ndh, shininess);\n"
    "                shine += att * spot * f * u[b + 2].rgb * specular.rgb;\n"
    "            }\n"
    "            lit += att * spot * term;\n"
    "        }\n"
    "        if (u[U_MISC].w != 0.0) {\n"
    "            secondary = shine;\n"
    "            color = vec4(emission.rgb + lit, diffuse.a);\n"
    "        } else {\n"
    "            color = vec4(emission.rgb + lit + shine, diffuse.a);\n"
    "        }\n"
    "    }\n"
    "    color = clamp(color, 0.0, 1.0);\n"
    "    secondary = clamp(secondary, 0.0, 1.0);\n"
    "    vColor = color;\n"
    "    vFlatColor = color;\n"
    "    vSecondary = secondary;\n"
    "    vFlatSecondary = secondary;\n"
    "}\n";

static const char g_fragment_body[] =
    "in vec4 vColor;\n"
    "flat in vec4 vFlatColor;\n"
    "in vec3 vSecondary;\n"
    "flat in vec3 vFlatSecondary;\n"
    "in vec4 vTexCoord;\n"
    "in float vFog;\n"
    "uniform vec4 u[U_COUNT];\n"
    "uniform sampler2D tex0;\n"
    "out vec4 fragColor;\n"
    "void main()\n"
    "{\n"
    "    bool shadeFlat = u[U_FLAGS1].z != 0.0;\n"
    "    vec4 c = shadeFlat ? vFlatColor : vColor;\n"
    "    vec3 secondary = shadeFlat ? vFlatSecondary : vSecondary;\n"
    "    int mode = int(u[U_FLAGS1].x + 0.5);\n"
    "    if (mode != 0) {\n"
    "        int texFormat = int(u[U_FLAGS1].y + 0.5);\n"
    "        bool hasColor = (texFormat & TEX_COLOR) != 0;\n"
    "        bool hasAlpha = (texFormat & TEX_ALPHA) != 0;\n"
    "        bool intensity = (texFormat & TEX_INTENSITY) != 0;\n"
    "        vec4 t = texture(tex0, vTexCoord.xy / vTexCoord.w);\n"
    "        vec4 env = u[U_ENV_COLOR];\n"
    "        if (mode == ENV_MODULATE) {\n"
    "            if (hasColor) c.rgb *= t.rgb;\n"
    "            if (hasAlpha) c.a *= t.a;\n"
    "        } else if (mode == ENV_REPLACE) {\n"
    "            if (hasColor) c.rgb = t.rgb;\n"
    "            if (hasAlpha) c.a = t.a;\n"
    "        } else if (mode == ENV_DECAL) {\n"
    "            c.rgb = hasAlpha ? mix(c.rgb, t.rgb, t.a) : t.rgb;\n"
    "        } else if (mode == ENV_BLEND) {\n"
    "            if (hasColor) c.rgb = mix(c.rgb, env.rgb, t.rgb);\n"
    "            if (intensity) c.a = mix(c.a, env.a, t.a);\n"
    "            else if (hasAlpha) c.a *= t.a;\n"
    "        } else if (mode == ENV_ADD) {\n"
    "            if (hasColor) c.rgb += t.rgb;\n"
    "            if (intensity) c.a += t.a;\n"
    "            else if (hasAlpha) c.a *= t.a;\n"
    "        }\n"
    "        c = clamp(c, 0.0, 1.0);\n"
    "    }\n"
    "    c.rgb = clamp(c.rgb + secondary, 0.0, 1.0);\n"
    "    int fogMode = int(u[U_FLAGS0].w + 0.5);\n"
    "    if (fogMode != 0) {\n"
    "        float f;\n"
    "        if (fogMode == 1) f = (u[U_FOG].z - vFog) * u[U_FOG].w;\n"
    "        else if (fogMode == 2) f = exp(-u[U_FOG].x * vFog);\n"
    "        else f = exp(-(u[U_FOG].x * vFog) * (u[U_FOG].x * vFog));\n"
    "        c.rgb = mix(u[U_FOG_COLOR].rgb, c.rgb, clamp(f, 0.0, 1.0));\n"
    "    }\n"
    "    int func = int(u[U_FLAGS0].y + 0.5);\n"
    "    if (func != 7) {\n"
    "        float ref = u[U_FLAGS0].z;\n"
    "        bool pass = func == 0 ? false\n"
    "                  : func == 1 ? c.a < ref\n"
    "                  : func == 2 ? c.a == ref\n"
    "                  : func == 3 ? c.a <= ref\n"
    "                  : func == 4 ? c.a > ref\n"
    "                  : func == 5 ? c.a != ref\n"
    "                  : c.a >= ref;\n"
    "        if (!pass) discard;\n"
    "    }\n"
    "    fragColor = c;\n"
    "}\n";

static char g_vertex[8192], g_fragment[8192];

static void build(void)
{
    if (g_vertex[0])
        return;
    char header[2048];
    int n = _snprintf(header, sizeof(header), "#version 330 core\n");
#define DEFINE(name, value) n += _snprintf(header + n, sizeof(header) - (SIZE_T)n, "#define " #name " %d\n", (int)(value));
    COMMON_DEFINES(DEFINE)
#undef DEFINE
    _snprintf(g_fragment, sizeof(g_fragment), "%s%s", header, g_fragment_body);
    _snprintf(g_vertex, sizeof(g_vertex), "%s%s", header, g_vertex_body);
}

const char *gl_ff_vertex_source(void)
{
    build();
    return g_vertex;
}

const char *gl_ff_fragment_source(void)
{
    build();
    return g_fragment;
}

/* linked on the first draw; NULL when the host refused it. */
static GlProgram *ff_program(GlContext *ctx)
{
    GlFixed *f = &ctx->ff;
    if (f->program_failed)
        return nullptr;
    GlProgram *p = f->program;
    if (!p) {
        p = gl_alloc(sizeof(GlProgram));
        if (!p) {
            f->program_failed = TRUE;
            return nullptr;
        }
        f->program = p;
        gl_program_link_sources(p, gl_ff_vertex_source(), gl_ff_fragment_source());
        f->dirty = TRUE;
    }
    if (f->uniform_found)
        return p;
    if (!gl_program_ready(p)) {
        gl_log("the fixed function program failed to link:\n%s", p->log ? p->log : "");
        f->program_failed = TRUE;
        return nullptr;
    }
    for (uint32_t i = 0; i < p->uniform_count; i++) {
        const GlUniform *u = &p->uniforms[i];
        if (u->base_len == 1 && u->name[0] == 'u') {
            f->uniform_offset = u->offset;
            f->uniform_stride = u->array_stride ? u->array_stride : 16;
            f->uniform_found = TRUE;
        }
    }
    if (!f->uniform_found || f->uniform_offset + (FF_U_COUNT - 1) * f->uniform_stride + 16 > p->ublock_size) {
        gl_log("the fixed function program has no uniform array");
        f->uniform_found = FALSE;
        f->program_failed = TRUE;
        return nullptr;
    }
    return p;
}

/* the program for a draw without one, and where it reads its vertices; NULL when nothing is drawn. */
GlProgram *gl_ff_begin_draw(GlContext *ctx, GlVertexInput *in, BOOL immediate)
{
    GlFixed *f = &ctx->ff;
    /* the other render modes draw nothing, and arrays need a vertex array. */
    if (f->render_mode != GL_RENDER || (!immediate && !ctx->vao->legacy[0].enabled))
        return nullptr;
    GlProgram *p = ff_program(ctx);
    if (!p)
        return nullptr;
    memcpy(f->constants[1], f->current[0], 16);
    memcpy(f->constants[2], f->current[1], 16);
    memcpy(f->constants[3], f->current[2], 16);
    in->attribs = ctx->vao->legacy;
    in->mask = 0xF;
    in->constants = f->constants;
    in->fixed = TRUE;
    in->flat = f->shade_model == GL_FLAT;
    return p;
}

/* updates the program's uniforms when the state changed. */
void gl_ff_prepare(GlContext *ctx, GlProgram *p)
{
    GlFixed *f = &ctx->ff;
    if (f->dirty) {
        gl_ff_pack(ctx, f->uniforms);
        f->dirty = FALSE;
    }
    /* state that depends on the bound texture and point size is checked every draw. */
    uint32_t env, format;
    gl_ff_texture_state(ctx, &env, &format);
    f->uniforms[FF_U_FLAGS1][0] = (GLfloat)env;
    f->uniforms[FF_U_FLAGS1][1] = (GLfloat)format;
    f->uniforms[FF_U_MISC][0] = gl_point_size(ctx);
    for (uint32_t i = 0; i < FF_U_COUNT; i++) {
        uint8_t *dst = p->ublock + f->uniform_offset + i * f->uniform_stride;
        if (memcmp(dst, f->uniforms[i], 16)) {
            memcpy(dst, f->uniforms[i], 16);
            p->ublock_dirty = TRUE;
        }
    }
}
