/*
 * the driver's own shaders: clears of rectangles inside a render pass and scaled blits. their MSL is written
 * for each attachment layout, and the pipeline states are cached for the process like the guest's.
 */
#include "vkm_cmd.h"

#include <stdio.h>

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static VkmCache *g_cache;
static id g_samplers[2];

static id cached(const void *key, size_t size)
{
    pthread_mutex_lock(&g_lock);
    id object = vkm_cache_find(g_cache, key, size, vkm_hash(key, size, 3));
    pthread_mutex_unlock(&g_lock);
    return object;
}

static id cache(const void *key, size_t size, id object)
{
    uint64_t hash = vkm_hash(key, size, 3);
    pthread_mutex_lock(&g_lock);
    id raced = vkm_cache_find(g_cache, key, size, hash);
    if (raced) {
        mtl_release(object);
        object = raced;
    } else {
        vkm_cache_add(&g_cache, key, size, hash, object);
    }
    pthread_mutex_unlock(&g_lock);
    return object;
}

/* 0 for float formats, 1 for signed and 2 for unsigned integers. */
static int pixel_kind(uint32_t pixel)
{
    switch (pixel) {
    case MTLPixelFormatR8Sint: case MTLPixelFormatRG8Sint: case MTLPixelFormatRGBA8Sint: case MTLPixelFormatR16Sint:
    case MTLPixelFormatRG16Sint: case MTLPixelFormatRGBA16Sint: case MTLPixelFormatR32Sint: case MTLPixelFormatRG32Sint:
    case MTLPixelFormatRGBA32Sint: return 1;
    case MTLPixelFormatR8Uint: case MTLPixelFormatRG8Uint: case MTLPixelFormatRGBA8Uint: case MTLPixelFormatR16Uint:
    case MTLPixelFormatRG16Uint: case MTLPixelFormatRGBA16Uint: case MTLPixelFormatR32Uint: case MTLPixelFormatRG32Uint:
    case MTLPixelFormatRGBA32Uint: case MTLPixelFormatRGB10A2Uint: return 2;
    default: return 0;
    }
}

static const char *const g_kind_type[] = {"float", "int", "uint"};

static id render_pipeline(VkDevice device, const char *source, bool fragment, const uint32_t colors[VKM_COLORS],
                          uint32_t mask, uint32_t samples)
{
    id library = vkm_library(device, source);
    if (!library)
        return nullptr;
    void *pool = objc_autoreleasePoolPush();
    id vs = vkm_function(library, "meta_vs");
    id fs = fragment ? vkm_function(library, "meta_fs") : nullptr;
    id desc = mtl_new("MTL4RenderPipelineDescriptor");
    M_V_ID(desc, "setVertexFunctionDescriptor:", vs);
    if (fs)
        M_V_ID(desc, "setFragmentFunctionDescriptor:", fs);
    M_V_U(desc, "setRasterSampleCount:", MIME_MAX(samples, 1u));
    M_V_U(desc, "setInputPrimitiveTopology:", MTLPrimitiveTopologyClassTriangle);
    id attachments = M_ID(desc, "colorAttachments");
    for (uint32_t i = 0; i < VKM_COLORS; i++) {
        if (!colors[i])
            continue;
        id c = M_AT(attachments, i);
        M_V_U(c, "setPixelFormat:", colors[i]);
        M_V_U(c, "setWriteMask:", fs && (mask & (1u << i)) ? MTLColorWriteMaskAll : MTLColorWriteMaskNone);
    }
    id error = nullptr;
    id state = MTL_MSG(id (*)(id, SEL, id, id, id *), device->compiler,
                       "newRenderPipelineStateWithDescriptor:compilerTaskOptions:error:", desc, nullptr, &error);
    if (!state)
        VKM_ERROR("no internal pipeline: %s", mtl_error_text(error));
    mtl_release(desc);
    mtl_release(fs);
    mtl_release(vs);
    mtl_release(library);
    objc_autoreleasePoolPop(pool);
    return state;
}

/* ------------------------------------------------------------------ clears. */

/* a full screen triangle the viewport puts over the rectangle; depth comes from the values. */
static const char g_clear_vs[] = "#include <metal_stdlib>\n"
                                 "using namespace metal;\n"
                                 "struct Values { uint4 colors[8]; float depth; };\n"
                                 "struct V { float4 position [[position]];%s };\n"
                                 "vertex V meta_vs(uint vid [[vertex_id]], uint iid [[instance_id]],\n"
                                 "                 constant Values &v [[buffer(0)]]) {\n"
                                 "    float2 p = float2((vid << 1) & 2, vid & 2);\n"
                                 "    V o;\n"
                                 "    o.position = float4(p * 2.0 - 1.0, v.depth, 1.0);\n"
                                 "%s"
                                 "    return o;\n"
                                 "}\n";

id vkm_meta_clear(VkDevice device, const uint32_t colors[VKM_COLORS], uint32_t mask, bool depth, bool stencil,
                  uint32_t samples, bool layered)
{
    struct {
        uint32_t tag;
        uint32_t colors[VKM_COLORS];
        uint32_t mask, samples, layered;
    } key = {1, {}, mask, samples, layered};
    memcpy(key.colors, colors, sizeof(key.colors));
    id state = cached(&key, sizeof(key));
    if (state)
        return state;
    char source[4096];
    int n = snprintf(source, sizeof(source), g_clear_vs, layered ? " uint layer [[render_target_array_index]];" : "",
                     layered ? "    o.layer = iid;\n" : "");
    if (mask) {
        n += snprintf(source + n, sizeof(source) - n, "struct F {");
        for (uint32_t i = 0; i < VKM_COLORS; i++)
            if (mask & (1u << i))
                n += snprintf(source + n, sizeof(source) - n, " %s4 c%u [[color(%u)]];",
                              g_kind_type[pixel_kind(colors[i])], i, i);
        n += snprintf(source + n, sizeof(source) - n,
                      " };\nfragment F meta_fs(V in [[stage_in]], constant Values &v [[buffer(0)]]) {\n    F o;\n");
        for (uint32_t i = 0; i < VKM_COLORS; i++)
            if (mask & (1u << i))
                n += snprintf(source + n, sizeof(source) - n, "    o.c%u = as_type<%s4>(v.colors[%u]);\n", i,
                              g_kind_type[pixel_kind(colors[i])], i);
        snprintf(source + n, sizeof(source) - n, "    return o;\n}\n");
    }
    state = render_pipeline(device, source, mask != 0, colors, mask, samples);
    return state ? cache(&key, sizeof(key), state) : nullptr;
}

id vkm_meta_depth_state(VkDevice device, bool depth, bool stencil)
{
    VkmDepthKey key;
    memset(&key, 0, sizeof(key));
    key.test = depth;
    key.write = depth;
    key.compare = VK_COMPARE_OP_ALWAYS;
    key.stencil = stencil;
    VkmStencilFace face = {VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_REPLACE, VK_COMPARE_OP_ALWAYS,
                           0xff, 0xff};
    if (stencil)
        key.front = key.back = face;
    return vkm_depth_state(device, &key);
}

/* ------------------------------------------------------------------ blits. */

static const char g_blit_vs[] = "#include <metal_stdlib>\n"
                                "using namespace metal;\n"
                                "struct P { float4 src; float layer, lod; };\n"
                                "struct V { float4 position [[position]]; float2 uv; };\n"
                                "struct D { float d [[depth(any)]]; };\n"
                                "struct C { %s4 c [[color(0)]]; };\n"
                                "vertex V meta_vs(uint vid [[vertex_id]], constant P &p [[buffer(0)]]) {\n"
                                "    float2 t = float2(vid & 1, vid >> 1);\n"
                                "    V o;\n"
                                "    o.position = float4(t.x * 2.0 - 1.0, 1.0 - t.y * 2.0, 0.0, 1.0);\n"
                                "    o.uv = mix(p.src.xy, p.src.zw, t);\n"
                                "    return o;\n"
                                "}\n"
                                "fragment %s meta_fs(V in [[stage_in]], constant P &p [[buffer(0)]], %s t [[texture(0)]],\n"
                                "                    sampler s [[sampler(0)]]) {\n"
                                "    uint lod = uint(p.lod);\n"
                                "    (void)lod;\n"
                                "    return {%s};\n"
                                "}\n";

id vkm_meta_blit(VkDevice device, uint32_t format, uint32_t source_type, bool depth, uint32_t samples)
{
    struct {
        uint32_t tag, format, source_type, depth, samples;
    } key = {2, format, source_type, depth, samples};
    id state = cached(&key, sizeof(key));
    if (state)
        return state;
    bool array = source_type == MTLTextureType2DArray, is3d = source_type == MTLTextureType3D;
    if (!array && !is3d && source_type != MTLTextureType2D) {
        VKM_ERROR("a blit from a texture of type %u", source_type);
        return nullptr;
    }
    int kind = depth ? 0 : pixel_kind(format);
    const char *type = g_kind_type[kind];
    const char *dim = is3d ? "3d" : array ? "2d_array" : "2d";
    char texture[64], fetch[256];
    if (depth) {
        snprintf(texture, sizeof(texture), "%s<float>", array ? "depth2d_array" : "depth2d");
        snprintf(fetch, sizeof(fetch), "t.sample(s, in.uv%s, level(p.lod))", array ? ", uint(p.layer)" : "");
    } else if (kind && is3d) {
        /* integers are read, not filtered. */
        snprintf(texture, sizeof(texture), "texture3d<%s>", type);
        snprintf(fetch, sizeof(fetch),
                 "t.read(uint3(float3(in.uv, p.layer) * float3(t.get_width(lod), t.get_height(lod), t.get_depth(lod))), "
                 "lod)");
    } else if (kind) {
        snprintf(texture, sizeof(texture), "texture%s<%s>", dim, type);
        snprintf(fetch, sizeof(fetch), "t.read(uint2(in.uv * float2(t.get_width(lod), t.get_height(lod)))%s, lod)",
                 array ? ", uint(p.layer)" : "");
    } else {
        snprintf(texture, sizeof(texture), "texture%s<float>", dim);
        snprintf(fetch, sizeof(fetch), "t.sample(s, %s, level(p.lod))",
                 is3d ? "float3(in.uv, p.layer)" : array ? "in.uv, uint(p.layer)" : "in.uv");
    }
    char source[4096];
    snprintf(source, sizeof(source), g_blit_vs, type, depth ? "D" : "C", texture, fetch);
    uint32_t colors[VKM_COLORS] = {};
    if (!depth)
        colors[0] = format;
    state = render_pipeline(device, source, true, colors, 1, samples);
    return state ? cache(&key, sizeof(key), state) : nullptr;
}

static id make_sampler(VkDevice device, bool linear)
{
    id desc = mtl_new("MTLSamplerDescriptor");
    uint32_t filter = linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    M_V_U(desc, "setMinFilter:", filter);
    M_V_U(desc, "setMagFilter:", filter);
    M_V_U(desc, "setMipFilter:", MTLSamplerMipFilterNearest);
    M_V_U(desc, "setSAddressMode:", MTLSamplerAddressModeClampToEdge);
    M_V_U(desc, "setTAddressMode:", MTLSamplerAddressModeClampToEdge);
    M_V_U(desc, "setRAddressMode:", MTLSamplerAddressModeClampToEdge);
    M_V_B(desc, "setSupportArgumentBuffers:", true);
    id state = M_ID_ID(device->mtl, "newSamplerStateWithDescriptor:", desc);
    mtl_release(desc);
    return state;
}

static id sampler(VkDevice device, bool linear)
{
    pthread_mutex_lock(&g_lock);
    if (!g_samplers[linear])
        g_samplers[linear] = make_sampler(device, linear);
    id state = g_samplers[linear];
    pthread_mutex_unlock(&g_lock);
    return state;
}

id vkm_sampler_linear(VkDevice device)
{
    return sampler(device, true);
}

id vkm_sampler_nearest(VkDevice device)
{
    return sampler(device, false);
}
