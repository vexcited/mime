/*
 * the driver's own shaders: clears of rectangles inside a render pass and scaled blits. their MSL is written
 * for each attachment layout, and the pipeline states are cached for the process like the guest's.
 */
#include "vkm_cmd.h"

#include <stdio.h>

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static VkmCache *g_cache;
static id<MTLSamplerState> g_samplers[2];

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
        [object release];
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

/* a pipeline of meta_vs and meta_fs, or of meta_vs alone writing buffers with nothing rasterized. */
static id<MTLRenderPipelineState> pipeline_of(VkDevice device, const char *source, bool fragment,
                                              const uint32_t colors[VKM_COLORS], uint32_t mask, uint32_t samples,
                                              bool raster)
{
    id<MTLLibrary> library = vkm_library(device, source);
    if (!library)
        return nullptr;
    id<MTLRenderPipelineState> state;
    @autoreleasepool {
        MTL4LibraryFunctionDescriptor *vs = vkm_function(library, "meta_vs");
        MTL4LibraryFunctionDescriptor *fs = fragment ? vkm_function(library, "meta_fs") : nil;
        MTL4RenderPipelineDescriptor *desc = [[MTL4RenderPipelineDescriptor alloc] init];
        desc.vertexFunctionDescriptor = vs;
        if (fs)
            desc.fragmentFunctionDescriptor = fs;
        desc.rasterSampleCount = MIME_MAX(samples, 1u);
        desc.inputPrimitiveTopology = raster ? MTLPrimitiveTopologyClassTriangle : MTLPrimitiveTopologyClassPoint;
        if (!raster)
            desc.rasterizationEnabled = false;
        MTL4RenderPipelineColorAttachmentDescriptorArray *attachments = desc.colorAttachments;
        for (uint32_t i = 0; i < VKM_COLORS; i++) {
            if (!colors[i])
                continue;
            MTL4RenderPipelineColorAttachmentDescriptor *c = attachments[i];
            c.pixelFormat = (MTLPixelFormat)colors[i];
            c.writeMask = fs && (mask & (1u << i)) ? MTLColorWriteMaskAll : MTLColorWriteMaskNone;
        }
        NSError *error = nil;
        state = mime_pipeline_cache_render(device->pipelines, desc, (id *)&error);
        if (!state)
            VKM_ERROR("no internal pipeline: %s", vkm_error_text(error));
        [desc release];
        [fs release];
        [vs release];
        [library release];
    }
    return state;
}

static id<MTLRenderPipelineState> render_pipeline(VkDevice device, const char *source, bool fragment,
                                                  const uint32_t colors[VKM_COLORS], uint32_t mask, uint32_t samples)
{
    return pipeline_of(device, source, fragment, colors, mask, samples, true);
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

id<MTLRenderPipelineState> vkm_meta_clear(VkDevice device, const uint32_t colors[VKM_COLORS], uint32_t mask, bool depth,
                                          bool stencil, uint32_t samples, bool layered)
{
    struct {
        uint32_t tag;
        uint32_t colors[VKM_COLORS];
        uint32_t mask, samples, layered;
    } key = {1, {}, mask, samples, layered};
    memcpy(key.colors, colors, sizeof(key.colors));
    id<MTLRenderPipelineState> state = cached(&key, sizeof(key));
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

id<MTLDepthStencilState> vkm_meta_depth_state(VkDevice device, bool depth, bool stencil)
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

id<MTLRenderPipelineState> vkm_meta_blit(VkDevice device, uint32_t format, uint32_t source_type, bool depth,
                                         uint32_t samples)
{
    struct {
        uint32_t tag, format, source_type, depth, samples;
    } key = {2, format, source_type, depth, samples};
    id<MTLRenderPipelineState> state = cached(&key, sizeof(key));
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

static id<MTLSamplerState> make_sampler(VkDevice device, bool linear)
{
    MTLSamplerDescriptor *desc = [[MTLSamplerDescriptor alloc] init];
    MTLSamplerMinMagFilter filter = linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
    desc.minFilter = filter;
    desc.magFilter = filter;
    desc.mipFilter = MTLSamplerMipFilterNearest;
    desc.sAddressMode = MTLSamplerAddressModeClampToEdge;
    desc.tAddressMode = MTLSamplerAddressModeClampToEdge;
    desc.rAddressMode = MTLSamplerAddressModeClampToEdge;
    desc.supportArgumentBuffers = true;
    id<MTLSamplerState> state = [device->mtl newSamplerStateWithDescriptor:desc];
    [desc release];
    return state;
}

static id<MTLSamplerState> sampler(VkDevice device, bool linear)
{
    pthread_mutex_lock(&g_lock);
    if (!g_samplers[linear])
        g_samplers[linear] = make_sampler(device, linear);
    id<MTLSamplerState> state = g_samplers[linear];
    pthread_mutex_unlock(&g_lock);
    return state;
}

id<MTLSamplerState> vkm_sampler_linear(VkDevice device)
{
    return sampler(device, true);
}

id<MTLSamplerState> vkm_sampler_nearest(VkDevice device)
{
    return sampler(device, false);
}

/* ------------------------------------------------------------------ generated commands. */

/*
 * a thread a sequence: its push constants are the bound ones with what its tokens change, its vertex and index
 * buffers too, and it encodes its draw or dispatch with every buffer the stages read, since the commands inherit
 * only the pipeline. the first thread of each chunk writes the range of the sequences the count lets run.
 */
static const char g_generate[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct Token { uint type, offset, dst, size; };\n"
    "struct Params {\n"
    "    ulong args, count, pushes, ranges, index;\n"
    "    ulong buffers[31];\n"
    "    uint strides[31];\n"
    "    uint stride, max_count, token_count, primitive, index_wide, local[3];\n"
    "    Token tokens[16];\n"
    "    uint push[64];\n"
    "};\n"
    "struct Icb { command_buffer cmds; };\n"
    "template <typename T> static device T *at(ulong address) { return reinterpret_cast<device T *>(address); }\n"
    "static uint sequences(constant Params &p, uint i) {\n"
    "    uint count = p.max_count;\n"
    "    if (p.count)\n"
    "        count = min(count, *at<const uint>(p.count));\n"
    "    if (i % CHUNK == 0) {\n"
    "        device uint *r = at<uint>(p.ranges) + i / CHUNK * 2;\n"
    "        r[0] = i;\n"
    "        r[1] = count > i ? min(CHUNK, count - i) : 0u;\n"
    "    }\n"
    "    return count;\n"
    "}\n"
    "static ulong pushes(constant Params &p, uint i, device const uint *a) {\n"
    "    if (!p.pushes)\n"
    "        return p.buffers[8];\n"
    "    ulong address = p.pushes + (ulong)i * 256;\n"
    "    device uint *b = at<uint>(address);\n"
    "    for (uint n = 0; n < 64; n++)\n"
    "        b[n] = p.push[n];\n"
    "    for (uint t = 0; t < p.token_count; t++) {\n"
    "        Token k = p.tokens[t];\n"
    "        if (k.type == 1)\n"
    "            for (uint n = 0; n < k.size / 4; n++)\n"
    "                b[k.dst / 4 + n] = a[k.offset / 4 + n];\n"
    "        else if (k.type == 4)\n"
    "            b[k.dst / 4] = i;\n"
    "    }\n"
    "    return address;\n"
    "}\n"
    "kernel void meta_draw(constant Params &p [[buffer(0)]], device const Icb &icb [[buffer(1)]],\n"
    "                      uint i [[thread_position_in_grid]]) {\n"
    "    if (i >= p.max_count)\n"
    "        return;\n"
    "    uint count = sequences(p, i);\n"
    "    if (i >= count)\n"
    "        return;\n"
    "    render_command cmd(icb.cmds, i);\n"
    "    cmd.reset();\n"
    "    device const uint *a = at<const uint>(p.args + (ulong)i * p.stride), *w = a;\n"
    "    ulong vbs[21];\n"
    "    uint strides[21];\n"
    "    for (uint b = 0; b < 21; b++) {\n"
    "        vbs[b] = p.buffers[10 + b];\n"
    "        strides[b] = p.strides[10 + b];\n"
    "    }\n"
    "    ulong index = p.index;\n"
    "    bool wide = p.index_wide;\n"
    "    uint draw = 0;\n"
    "    for (uint t = 0; t < p.token_count; t++) {\n"
    "        Token k = p.tokens[t];\n"
    "        device const uint *v = a + k.offset / 4;\n"
    "        if (k.type == 2 && k.dst < 20) {\n"
    "            vbs[k.dst] = ulong(v[0]) | ulong(v[1]) << 32;\n"
    "            strides[k.dst] = v[3];\n"
    "        } else if (k.type == 3) {\n"
    "            index = ulong(v[0]) | ulong(v[1]) << 32;\n"
    "            wide = k.dst ? v[3] == 42 : v[3] == 1;\n"
    "        } else if (k.type == 5 || k.type == 6) {\n"
    "            draw = k.type;\n"
    "            w = v;\n"
    "        }\n"
    "    }\n"
    "    if (!draw || !w[0] || !w[1] || (draw == 6 && !index))\n"
    "        return;\n"
    "    ulong push = pushes(p, i, a);\n"
    "    for (uint b = 0; b < 10; b++) {\n"
    "        ulong v = b == 8 ? push : p.buffers[b];\n"
    "        if (v) {\n"
    "            cmd.set_vertex_buffer(at<char>(v), b);\n"
    "            cmd.set_fragment_buffer(at<char>(v), b);\n"
    "        }\n"
    "    }\n"
    "    for (uint b = 0; b < 21; b++)\n"
    "        if (vbs[b])\n"
    "            cmd.set_vertex_buffer(at<char>(vbs[b]), (size_t)strides[b], 10 + b);\n"
    "    primitive_type type = primitive_type(p.primitive);\n"
    "    if (draw == 5)\n"
    "        cmd.draw_primitives(type, w[2], w[0], w[1], w[3]);\n"
    "    else if (wide)\n"
    "        cmd.draw_indexed_primitives(type, w[0], at<uint>(index) + w[2], w[1], w[3], w[4]);\n"
    "    else\n"
    "        cmd.draw_indexed_primitives(type, w[0], at<ushort>(index) + w[2], w[1], w[3], w[4]);\n"
    "}\n"
    "kernel void meta_dispatch(constant Params &p [[buffer(0)]], device const Icb &icb [[buffer(1)]],\n"
    "                          uint i [[thread_position_in_grid]]) {\n"
    "    if (i >= p.max_count)\n"
    "        return;\n"
    "    uint count = sequences(p, i);\n"
    "    if (i >= count)\n"
    "        return;\n"
    "    compute_command cmd(icb.cmds, i);\n"
    "    cmd.reset();\n"
    "    device const uint *a = at<const uint>(p.args + (ulong)i * p.stride), *w = nullptr;\n"
    "    for (uint t = 0; t < p.token_count; t++)\n"
    "        if (p.tokens[t].type == 7)\n"
    "            w = a + p.tokens[t].offset / 4;\n"
    "    if (!w || !w[0] || !w[1] || !w[2])\n"
    "        return;\n"
    "    ulong push = pushes(p, i, a);\n"
    "    for (uint b = 0; b < 10; b++) {\n"
    "        ulong v = b == 8 ? push : p.buffers[b];\n"
    "        if (v)\n"
    "            cmd.set_kernel_buffer(at<char>(v), b);\n"
    "    }\n"
    "    cmd.concurrent_dispatch_threadgroups(uint3(w[0], w[1], w[2]), uint3(p.local[0], p.local[1], p.local[2]));\n"
    "}\n";

id<MTLComputePipelineState> vkm_meta_generate(VkDevice device, bool dispatch)
{
    struct {
        uint32_t tag, dispatch;
    } key = {3, dispatch};
    id<MTLComputePipelineState> state = cached(&key, sizeof(key));
    if (state)
        return state;
    char source[sizeof(g_generate) + 64];
    snprintf(source, sizeof(source), "#define CHUNK %uu\n%s", VKM_DGC_CHUNK, g_generate);
    id<MTLLibrary> library = vkm_library(device, source);
    if (!library)
        return nullptr;
    @autoreleasepool {
        MTL4LibraryFunctionDescriptor *function = vkm_function(library, dispatch ? "meta_dispatch" : "meta_draw");
        MTL4ComputePipelineDescriptor *desc = [[MTL4ComputePipelineDescriptor alloc] init];
        desc.computeFunctionDescriptor = function;
        NSError *error = nil;
        state = mime_pipeline_cache_compute(device->pipelines, desc, (id *)&error);
        if (!state)
            VKM_ERROR("no generation pipeline: %s", vkm_error_text(error));
        [desc release];
        [function release];
        [library release];
    }
    return state ? cache(&key, sizeof(key), state) : nullptr;
}

/* ------------------------------------------------------------------ stream output. */

/*
 * one invocation writes a draw's stream output in primitive order from the records of its last stage before
 * rasterization: a primitive goes whole into every buffer or, once one has no room, it and all after it are only
 * counted. the offsets go on from draw to draw in a state block, starting at the counters or the bound offsets.
 */
static const char g_xfb[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct Entry { uint buffer, offset, word, count; };\n"
    "struct Xfb {\n"
    "    ulong emu, index, buffers[4], counters[4], query, state;\n"
    "    uint sizes[4], starts[4], strides[4];\n"
    "    uint mode, verts, prims, instances, mul, swap, index_type, index_first, cap, in_base, in_stride;\n"
    "    uint invocations, max_vertices, out_base, out_stride, idx_base, counts_base, strip, entry_count;\n"
    "    Entry entries[32];\n"
    "};\n"
    "struct State { uint offsets[4]; uint ready; };\n"
    "template <typename T> static device T *at(ulong a) { return reinterpret_cast<device T *>(a); }\n"
    "struct Out {\n"
    "    device const Xfb &x;\n"
    "    device State &s;\n"
    "    uint written, needed;\n"
    "    bool full;\n"
    "    void primitive(device const uint *const rec[3]) {\n"
    "        needed++;\n"
    "        for (uint b = 0; b < 4 && !full; b++)\n"
    "            if (x.strides[b] && s.offsets[b] + x.verts * x.strides[b] > x.sizes[b])\n"
    "                full = true;\n"
    "        if (full)\n"
    "            return;\n"
    "        for (uint k = 0; k < x.verts; k++)\n"
    "            for (uint e = 0; e < x.entry_count; e++) {\n"
    "                Entry n = x.entries[e];\n"
    "                device uint *dst = at<uint>(x.buffers[n.buffer] + s.offsets[n.buffer] +\n"
    "                                            k * x.strides[n.buffer] + n.offset);\n"
    "                for (uint c = 0; c < n.count; c++)\n"
    "                    dst[c] = rec[k][n.word + c];\n"
    "            }\n"
    "        for (uint b = 0; b < 4; b++)\n"
    "            s.offsets[b] += x.verts * x.strides[b];\n"
    "        written++;\n"
    "    }\n"
    "};\n"
    "vertex void meta_vs(device const Xfb &x [[buffer(0)]]) {\n"
    "    device State &s = *at<State>(x.state);\n"
    "    if (x.mode == 2) {\n"
    "        for (uint b = 0; b < 4 && s.ready; b++)\n"
    "            if (x.counters[b])\n"
    "                *at<uint>(x.counters[b]) = s.offsets[b];\n"
    "        return;\n"
    "    }\n"
    "    if (!s.ready) {\n"
    "        for (uint b = 0; b < 4; b++)\n"
    "            s.offsets[b] = x.counters[b] ? *at<uint>(x.counters[b]) : x.starts[b];\n"
    "        s.ready = 1;\n"
    "    }\n"
    "    device const uint *emu = at<const uint>(x.emu);\n"
    "    device const uint *rec[3] = {emu, emu, emu};\n"
    "    Out o = {x, s, 0, 0, false};\n"
    "    if (x.mode == 0) {\n"
    "        for (uint i = 0; i < x.instances; i++)\n"
    "            for (uint p = 0; p < x.prims; p++) {\n"
    "                for (uint k = 0; k < x.verts; k++) {\n"
    "                    uint pos = x.mul * p + ((k < 2 && x.swap && (p & 1)) ? 1 - k : k);\n"
    "                    uint slot = pos;\n"
    "                    if (x.index_type == 2)\n"
    "                        slot = at<const uint>(x.index)[x.index_first + pos];\n"
    "                    else if (x.index_type == 1)\n"
    "                        slot = at<const ushort>(x.index)[x.index_first + pos];\n"
    "                    rec[k] = emu + x.in_base + (i * x.cap + (slot < x.cap ? slot : 0)) * x.in_stride;\n"
    "                }\n"
    "                o.primitive(rec);\n"
    "            }\n"
    "    } else {\n"
    "        uint total = x.instances * x.invocations, span = 2 * x.max_vertices;\n"
    "        for (uint g = 0; g < total; g++) {\n"
    "            if (!x.strip) {\n"
    "                for (uint t = 0; t < emu[x.counts_base + g]; t++) {\n"
    "                    rec[0] = emu + x.out_base + (g * x.max_vertices + t) * x.out_stride;\n"
    "                    o.primitive(rec);\n"
    "                }\n"
    "                continue;\n"
    "            }\n"
    "            uint run = 0, last[3] = {0, 0, 0};\n"
    "            for (uint t = 0; t < span; t++) {\n"
    "                uint r = emu[x.idx_base + g * span + t];\n"
    "                if (r == 0xffffffffu) {\n"
    "                    run = 0;\n"
    "                    continue;\n"
    "                }\n"
    "                last[0] = last[1];\n"
    "                last[1] = last[2];\n"
    "                last[2] = r;\n"
    "                if (++run < x.verts)\n"
    "                    continue;\n"
    "                uint first = 3 - x.verts;\n"
    "                bool odd = x.verts == 3 && ((run - 3) & 1);\n"
    "                for (uint k = 0; k < x.verts; k++) {\n"
    "                    uint v = odd && k < 2 ? last[first + 1 - k] : last[first + k];\n"
    "                    rec[k] = emu + x.out_base + v * x.out_stride;\n"
    "                }\n"
    "                o.primitive(rec);\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    if (x.query) {\n"
    "        device uint *q = at<uint>(x.query);\n"
    "        q[0] += o.written;\n"
    "        q[2] += o.needed;\n"
    "    }\n"
    "}\n";

id<MTLRenderPipelineState> vkm_meta_xfb(VkDevice device, const uint32_t colors[VKM_COLORS], uint32_t samples)
{
    struct {
        uint32_t tag;
        uint32_t colors[VKM_COLORS];
        uint32_t samples;
    } key = {4, {}, samples};
    memcpy(key.colors, colors, sizeof(key.colors));
    id<MTLRenderPipelineState> state = cached(&key, sizeof(key));
    if (state)
        return state;
    state = pipeline_of(device, g_xfb, false, colors, 0, samples, false);
    return state ? cache(&key, sizeof(key), state) : nullptr;
}

/* the vertex count of a draw of what stream output wrote, from its counter. */
static const char g_byte_count[] =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct P { ulong counter, out; uint vertex_offset, stride, instances, first_instance; };\n"
    "kernel void meta_count(constant P &p [[buffer(0)]]) {\n"
    "    uint bytes = *reinterpret_cast<device const uint *>(p.counter);\n"
    "    device uint *o = reinterpret_cast<device uint *>(p.out);\n"
    "    o[0] = bytes > p.vertex_offset && p.stride ? (bytes - p.vertex_offset) / p.stride : 0;\n"
    "    o[1] = p.instances;\n"
    "    o[2] = 0;\n"
    "    o[3] = p.first_instance;\n"
    "}\n";

id<MTLComputePipelineState> vkm_meta_byte_count(VkDevice device)
{
    uint32_t key = 5;
    id<MTLComputePipelineState> state = cached(&key, sizeof(key));
    if (state)
        return state;
    id<MTLLibrary> library = vkm_library(device, g_byte_count);
    if (!library)
        return nullptr;
    @autoreleasepool {
        MTL4LibraryFunctionDescriptor *function = vkm_function(library, "meta_count");
        MTL4ComputePipelineDescriptor *desc = [[MTL4ComputePipelineDescriptor alloc] init];
        desc.computeFunctionDescriptor = function;
        NSError *error = nil;
        state = mime_pipeline_cache_compute(device->pipelines, desc, (id *)&error);
        if (!state)
            VKM_ERROR("no byte count pipeline: %s", vkm_error_text(error));
        [desc release];
        [function release];
        [library release];
    }
    return state ? cache(&key, sizeof(key), state) : nullptr;
}
