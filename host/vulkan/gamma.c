/*
 * guest display gamma ramp, applied by a Vulkan presentation pass or during surface uploads. the identity ramp
 * disables correction.
 */
#include "gamma.h"

#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/resource_limits_c.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static uint16_t g_ramp[3][256];
static uint8_t g_table[3][256];
static bool g_active;
static uint32_t g_generation;

void mime_gamma_set(const uint16_t *ramp)
{
    pthread_mutex_lock(&g_lock);
    bool active = false;
    for (int c = 0; c < 3; c++)
        for (int i = 0; i < 256; i++) {
            uint16_t v = ramp ? ramp[c * 256 + i] : (uint16_t)(i * 257);
            g_ramp[c][i] = v;
            g_table[c][i] = (uint8_t)(v >> 8);
            active |= g_table[c][i] != i;
        }
    g_active = active;
    g_generation++;
    pthread_mutex_unlock(&g_lock);
}

bool mime_gamma_ramp(uint16_t ramp[3][256], uint32_t *generation)
{
    pthread_mutex_lock(&g_lock);
    bool active = g_active;
    if (ramp)
        memcpy(ramp, g_ramp, sizeof(g_ramp));
    if (generation)
        *generation = g_generation;
    pthread_mutex_unlock(&g_lock);
    return active;
}

bool mime_gamma_table(uint8_t table[3][256])
{
    pthread_mutex_lock(&g_lock);
    bool active = g_active;
    if (active)
        memcpy(table, g_table, sizeof(g_table));
    pthread_mutex_unlock(&g_lock);
    return active;
}

/* draws a fullscreen triangle, applying the ramp to each composed pixel. */
static const char VERTEX[] = "#version 450\n"
                             "void main() {\n"
                             "    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);\n"
                             "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                             "}\n";

/* an sRGB target encodes what it is given, so the ramp's value is handed over decoded. */
static const char FRAGMENT[] =
    "#version 450\n"
    "layout(set = 0, binding = 0) uniform sampler2D frame;\n"
    "layout(set = 0, binding = 1) uniform sampler2D ramp;\n"
    "layout(push_constant) uniform Push { uint srgb; } push;\n"
    "layout(location = 0) out vec4 color;\n"
    "float decode(float c) { return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4); }\n"
    "void main() {\n"
    "    vec4 t = texelFetch(frame, ivec2(gl_FragCoord.xy), 0);\n"
    "    ivec3 i = ivec3(round(clamp(t.rgb, 0.0, 1.0) * 255.0));\n"
    "    vec3 c = vec3(texelFetch(ramp, ivec2(i.r, 0), 0).r, texelFetch(ramp, ivec2(i.g, 0), 0).g,\n"
    "                  texelFetch(ramp, ivec2(i.b, 0), 0).b);\n"
    "    if (push.srgb != 0u)\n"
    "        c = vec3(decode(c.r), decode(c.g), decode(c.b));\n"
    "    color = vec4(c, 1.0);\n"
    "}\n";

/*
 * a guest image that is scRGB or HDR10 drawn as what its target shows, in nits between the two. a target of standard
 * range shows white as 1, and rolls off what is brighter when asked instead of cutting it.
 */
static const char CONVERT[] =
    "#version 450\n"
    "layout(set = 0, binding = 0) uniform sampler2D frame;\n"
    "layout(push_constant) uniform Push { uint source; uint target; float white; uint roll; int flip; } push;\n"
    "layout(location = 0) out vec4 color;\n"
    "const mat3 TO_709 = mat3(1.660491, -0.124550, -0.018151, -0.587641, 1.132900, -0.100579,\n"
    "                         -0.072850, -0.008349, 1.118730);\n"
    "const mat3 TO_2020 = mat3(0.627404, 0.069097, 0.016391, 0.329283, 0.919540, 0.088013,\n"
    "                          0.043313, 0.011362, 0.895595);\n"
    "vec3 pq_decode(vec3 e) {\n"
    "    vec3 p = pow(clamp(e, 0.0, 1.0), vec3(1.0 / 78.84375));\n"
    "    return 10000.0 * pow(max(p - 0.8359375, 0.0) / (18.8515625 - 18.6875 * p), vec3(1.0 / 0.1593017578125));\n"
    "}\n"
    "vec3 pq_encode(vec3 nits) {\n"
    "    vec3 y = pow(clamp(nits / 10000.0, 0.0, 1.0), vec3(0.1593017578125));\n"
    "    return pow((0.8359375 + 18.8515625 * y) / (1.0 + 18.6875 * y), vec3(78.84375));\n"
    "}\n"
    "vec3 srgb_encode(vec3 c) {\n"
    "    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c));\n"
    "}\n"
    "void main() {\n"
    "    ivec2 at = ivec2(gl_FragCoord.xy);\n"
    "    if (push.flip > 0)\n"
    "        at.y = push.flip - 1 - at.y;\n"
    "    vec4 t = texelFetch(frame, at, 0);\n"
    "    vec3 nits = push.source == 2u ? TO_709 * pq_decode(t.rgb) : t.rgb * 80.0;\n"
    "    vec3 c;\n"
    "    if (push.target == 2u) {\n"
    "        c = pq_encode(TO_2020 * nits);\n"
    "    } else if (push.target == 1u) {\n"
    "        c = nits / 80.0;\n"
    "    } else {\n"
    "        c = max(nits / push.white, 0.0);\n"
    "        float m = max(c.r, max(c.g, c.b));\n"
    "        if (push.roll != 0u && m > 0.8)\n"
    "            c *= (0.8 + 0.2 * (1.0 - exp(-(m - 0.8) / 0.2))) / m;\n"
    "        c = srgb_encode(clamp(c, 0.0, 1.0));\n"
    "    }\n"
    "    color = vec4(c, t.a);\n"
    "}\n";

typedef struct Spirv {
    uint32_t *words;
    size_t size;
} Spirv;

static Spirv compile(glslang_stage_t stage, const char *source)
{
    glslang_input_t in = {
        .language = GLSLANG_SOURCE_GLSL,
        .stage = stage,
        .client = GLSLANG_CLIENT_VULKAN,
        .client_version = GLSLANG_TARGET_VULKAN_1_0,
        .target_language = GLSLANG_TARGET_SPV,
        .target_language_version = GLSLANG_TARGET_SPV_1_0,
        .code = source,
        .default_version = 450,
        .default_profile = GLSLANG_NO_PROFILE,
        .messages = GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT,
        .resource = glslang_default_resource(),
    };
    Spirv out = {};
    glslang_shader_t *sh = glslang_shader_create(&in);
    glslang_program_t *prog = glslang_program_create();
    if (glslang_shader_preprocess(sh, &in) && glslang_shader_parse(sh, &in)) {
        glslang_program_add_shader(prog, sh);
        if (glslang_program_link(prog, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT)) {
            glslang_program_SPIRV_generate(prog, stage);
            out.size = glslang_program_SPIRV_get_size(prog) * 4;
            out.words = malloc(out.size);
            if (out.words)
                memcpy(out.words, glslang_program_SPIRV_get_ptr(prog), out.size);
        }
    }
    glslang_program_delete(prog);
    glslang_shader_delete(sh);
    return out;
}

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static Spirv g_vertex, g_fragment, g_convert;

static void compile_all(void)
{
    glslang_initialize_process();
    g_vertex = compile(GLSLANG_STAGE_VERTEX, VERTEX);
    g_fragment = compile(GLSLANG_STAGE_FRAGMENT, FRAGMENT);
    g_convert = compile(GLSLANG_STAGE_FRAGMENT, CONVERT);
}

bool mime_gamma_shaders(const uint32_t **vertex, size_t *vertex_size, const uint32_t **fragment, size_t *fragment_size)
{
    pthread_once(&g_once, compile_all);
    *vertex = g_vertex.words;
    *vertex_size = g_vertex.size;
    *fragment = g_fragment.words;
    *fragment_size = g_fragment.size;
    return g_vertex.words && g_fragment.words;
}

bool mime_convert_shaders(const uint32_t **vertex, size_t *vertex_size, const uint32_t **fragment,
                          size_t *fragment_size)
{
    pthread_once(&g_once, compile_all);
    *vertex = g_vertex.words;
    *vertex_size = g_vertex.size;
    *fragment = g_convert.words;
    *fragment_size = g_convert.size;
    return g_vertex.words && g_convert.words;
}
