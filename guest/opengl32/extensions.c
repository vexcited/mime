/*
 * the extensions the context lists. an entry is listed only when the host announced the features it needs, so what the
 * string says is what the entry points of wglGetProcAddress can do.
 */
#include "opengl32.h"

typedef struct GlExtension {
    const char *name;
    uint64_t features; /* MIME_GPU_FEATURE_* the host must have. */
} GlExtension;

static const GlExtension g_extensions[] = {
    {"GL_ARB_framebuffer_object", 0},
    {"GL_EXT_framebuffer_object", 0},
    {"GL_EXT_framebuffer_blit", 0},
    {"GL_EXT_framebuffer_multisample", 0},
    {"GL_EXT_packed_depth_stencil", 0},
    {"GL_ARB_vertex_array_object", 0},
    {"GL_ARB_vertex_buffer_object", 0},
    {"GL_ARB_pixel_buffer_object", 0},
    {"GL_ARB_map_buffer_range", 0},
    {"GL_ARB_copy_buffer", 0},
    {"GL_ARB_texture_non_power_of_two", 0},
    {"GL_ARB_texture_rectangle", 0},
    {"GL_EXT_bgra", 0},
    {"GL_EXT_texture_format_BGRA8888", 0},
    {"GL_ARB_texture_float", 0},
    {"GL_ARB_half_float_pixel", 0},
    {"GL_ARB_texture_rg", 0},
    {"GL_ARB_depth_texture", 0},
    {"GL_EXT_texture_edge_clamp", 0},
    {"GL_ARB_texture_border_clamp", 0},
    {"GL_ARB_texture_mirrored_repeat", 0},
    {"GL_EXT_texture_filter_anisotropic", 0},
    {"GL_ARB_shader_objects", 0},
    {"GL_ARB_vertex_shader", 0},
    {"GL_ARB_fragment_shader", 0},
    {"GL_ARB_shading_language_100", 0},
    {"GL_ARB_explicit_attrib_location", 0},
    {"GL_EXT_blend_func_separate", 0},
    {"GL_EXT_blend_minmax", 0},
    {"GL_EXT_blend_subtract", 0},
    {"GL_EXT_blend_equation_separate", 0},
    {"GL_EXT_blend_color", 0},
    {"GL_ARB_draw_instanced", 0},
    {"GL_ARB_instanced_arrays", 0},
    {"GL_ARB_draw_elements_base_vertex", 0},
    {"GL_EXT_texture_sRGB", 0},
    {"GL_ARB_framebuffer_sRGB", 0},
    {"GL_ARB_texture_storage", 0},
    {"GL_EXT_texture_compression_s3tc", 0},
    {"GL_ARB_texture_compression", 0},
    {"GL_ARB_multitexture", 0},
    {"GL_ARB_depth_buffer_float", 0},
    {"GL_EXT_stencil_wrap", 0},
    {"GL_EXT_draw_buffers2", 0},
    {"GL_ARB_draw_buffers", 0},
    {"GL_ARB_occlusion_query", 0},
    {"GL_ARB_get_program_binary", 0},
    {"GL_KHR_debug", 0},
    {"GL_ARB_compute_shader", MIME_GPU_FEATURE_COMPUTE},
    {"GL_ARB_shader_storage_buffer_object", MIME_GPU_FEATURE_COMPUTE},
    {"GL_ARB_shader_image_load_store", MIME_GPU_FEATURE_COMPUTE},
    {"GL_ARB_shader_image_size", MIME_GPU_FEATURE_COMPUTE},
    {"GL_ARB_program_interface_query", MIME_GPU_FEATURE_COMPUTE},
    {"GL_ARB_buffer_storage", 0},
    {"GL_ARB_draw_indirect", 0},
    {"GL_ARB_multi_draw_indirect", 0},
    {"GL_ARB_indirect_parameters", 0},
    {"GL_ARB_base_instance", 0},
};

#define EXTENSION_MAX 512

static char g_string[EXTENSION_MAX * 40];
static const char *g_list[EXTENSION_MAX];
static GLuint g_count;
static BOOL g_built;

static void build(void)
{
    if (g_built)
        return;
    gl_device_open();
    size_t used = 0;
    for (size_t i = 0; i < sizeof(g_extensions) / sizeof(g_extensions[0]); i++) {
        const GlExtension *e = &g_extensions[i];
        if (e->features && !gl_host_has(e->features))
            continue;
        size_t n = strlen(e->name);
        if (g_count == EXTENSION_MAX || used + n + 2 > sizeof(g_string))
            break;
        g_list[g_count++] = e->name;
        memcpy(g_string + used, e->name, n);
        g_string[used + n] = ' ';
        used += n + 1;
    }
    if (used)
        used--;
    g_string[used] = 0;
    g_built = TRUE;
}

const char *gl_extension_string(void)
{
    build();
    return g_string;
}

GLuint gl_extension_count(void)
{
    build();
    return g_count;
}

const char *gl_extension_at(GLuint index)
{
    build();
    return index < g_count ? g_list[index] : nullptr;
}
