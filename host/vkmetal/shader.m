/*
 * shaders and pipelines. SPIR-V becomes MSL with argument buffers laid out like the pipeline layout, compiled
 * once per MSL text for the process: every device shares the GPU, so libraries, render pipeline states and
 * depth stencil states are cached for all of them.
 */
#include "vkm_cmd.h"

#include <stdio.h>

typedef struct Library {
    struct Library *next;
    uint64_t hash;
    id<MTLLibrary> library;
} Library;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static Library *g_libraries;
static VkmCache *g_pipelines, *g_depth_states, *g_compute;

/* ------------------------------------------------------------------ MSL libraries. */

static MTLCompileOptions *compile_options(void)
{
    MTLCompileOptions *options = [[MTLCompileOptions alloc] init];
    options.languageVersion = MTLLanguageVersion3_2;
    options.mathMode = MTLMathModeSafe;
    options.preserveInvariance = true;
    return options;
}

static id<MTLLibrary> library_get(VkDevice device, const char *source, uint64_t hash)
{
    pthread_mutex_lock(&g_lock);
    for (Library *l = g_libraries; l; l = l->next)
        if (l->hash == hash) {
            id<MTLLibrary> library = [l->library retain];
            pthread_mutex_unlock(&g_lock);
            return library;
        }
    pthread_mutex_unlock(&g_lock);
    /* MIME_VK_DUMP names a folder that gets every source made, by its hash, to read what a shader became. */
    const char *dump = getenv("MIME_VK_DUMP");
    if (dump && *dump) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%016llx.metal", dump, (unsigned long long)hash);
        FILE *f = fopen(path, "w");
        if (f) {
            fputs(source, f);
            fclose(f);
        }
    }
    id<MTLLibrary> library;
    @autoreleasepool {
        MTL4LibraryDescriptor *desc = [[MTL4LibraryDescriptor alloc] init];
        MTLCompileOptions *options = compile_options();
        desc.source = @(source);
        desc.options = options;
        NSError *error = nil;
        library = mime_pipeline_cache_library(device->pipelines, desc, (id *)&error);
        [options release];
        [desc release];
        if (!library) {
            const char *text = vkm_error_text(error);
            VKM_ERROR("an MSL library does not compile: %.2000s", text);
            mime_debug("vkmetal: the MSL was:\n%s", source);
        }
    }
    if (!library)
        return nullptr;
    Library *l = vkm_zalloc(sizeof(*l));
    if (l) {
        l->hash = hash;
        l->library = [library retain];
        pthread_mutex_lock(&g_lock);
        l->next = g_libraries;
        g_libraries = l;
        pthread_mutex_unlock(&g_lock);
    }
    return library;
}

id<MTLLibrary> vkm_library(VkDevice device, const char *source)
{
    return library_get(device, source, vkm_hash(source, strlen(source), 0x6d736c));
}

MTL4LibraryFunctionDescriptor *vkm_function(id<MTLLibrary> library, const char *name)
{
    MTL4LibraryFunctionDescriptor *f = [[MTL4LibraryFunctionDescriptor alloc] init];
    f.name = @(name);
    f.library = library;
    return f;
}

/* ------------------------------------------------------------------ SPIR-V to Metal. */

static int execution_model(VkShaderStageFlagBits stage)
{
    switch (stage) {
    case VK_SHADER_STAGE_VERTEX_BIT: return 0;
    case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: return 1;
    case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: return 2;
    case VK_SHADER_STAGE_GEOMETRY_BIT: return 3;
    case VK_SHADER_STAGE_FRAGMENT_BIT: return 4;
    case VK_SHADER_STAGE_COMPUTE_BIT: return 5;
    default: return -1;
    }
}

static uint8_t msl_kind(VkDescriptorType type)
{
    switch (type) {
    case VK_DESCRIPTOR_TYPE_SAMPLER: return VKM_MSL_SAMPLER;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: return VKM_MSL_COMBINED;
    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
    case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
    /* every type a mutable binding holds is a texture, whose views the shaders declare at the same binding. */
    case VK_DESCRIPTOR_TYPE_MUTABLE_EXT: return VKM_MSL_IMAGE;
    default: return VKM_MSL_BUFFER;
    }
}

struct VkmSource {
    uint32_t *spirv;
    size_t words;
    char entry[128];
    VkmMslBinding *bindings;
    uint32_t binding_count, set_mask;
    VkmMslSpec *spec;
    uint32_t spec_count;
};

static VkResult compile_msl(VkDevice device, const uint32_t *spirv, size_t words, const char *entry, int model,
                            VkShaderStageFlagBits stage, const VkmMslBinding *bindings, uint32_t count,
                            uint32_t set_mask, const VkmMslSpec *spec, uint32_t spec_count, const uint8_t *widen,
                            bool dual_source, VkmCode *out)
{
    memset(out, 0, sizeof(*out));
    out->stage = stage;
    VkmMslResult r;
    if (!vkm_msl(spirv, words, entry, model, bindings, count, set_mask, spec, spec_count, widen, dual_source, &r)) {
        VKM_ERROR("a shader does not translate to MSL: %s", r.error);
        return VK_ERROR_INVALID_SHADER_NV;
    }
    out->hash = vkm_hash(r.source, strlen(r.source), vkm_hash(r.entry, strlen(r.entry), 0));
    out->library = library_get(device, r.source, out->hash);
    free(r.source);
    if (!out->library)
        return VK_ERROR_INVALID_SHADER_NV;
    out->function = vkm_function(out->library, r.entry);
    out->local = MTLSizeMake(r.local.width, r.local.height, r.local.depth);
    out->inputs = r.inputs;
    memcpy(out->input, r.input, sizeof(out->input));
    out->outputs = r.outputs;
    out->writes_depth = r.writes_depth;
    out->writes_stencil = r.writes_stencil;
    return VK_SUCCESS;
}

/* a vertex function of the geometry stage's emulation, with the buffers it binds after the layout's. */
static VkResult variant(VkDevice device, VkShaderStageFlagBits stage, const uint32_t *spirv, size_t words,
                        const char *entry, const VkmMslBinding *bindings, uint32_t count, uint32_t set_mask,
                        const VkmMslSpec *spec, uint32_t spec_count, const VkmMslBinding *extra, uint32_t extra_count,
                        VkmCode **out)
{
    VkmMslBinding *all = vkm_zalloc(sizeof(*all) * (count + extra_count));
    VkmCode *code = vkm_zalloc(sizeof(*code));
    if (!all || !code) {
        free(all);
        free(code);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    if (count)
        memcpy(all, bindings, sizeof(*all) * count);
    memcpy(all + count, extra, sizeof(*all) * extra_count);
    VkResult r = compile_msl(device, spirv, words, entry, execution_model(stage), stage, all, count + extra_count,
                             set_mask, spec, spec_count, nullptr, false, code);
    free(all);
    if (r != VK_SUCCESS) {
        free(code);
        return r;
    }
    *out = code;
    return VK_SUCCESS;
}

static const VkmMslBinding g_gs_buffers[] = {
    {VKM_EMU_SET, 0, 1, VKM_BUFFER_VERTEX, VKM_BUFFER_VERTEX, VKM_MSL_BUFFER},
    {VKM_EMU_SET, 1, 1, VKM_BUFFER_VERTEX + 1, VKM_BUFFER_VERTEX + 1, VKM_MSL_BUFFER},
};

static VkResult compile_geometry(VkDevice device, const uint32_t *spirv, size_t words, const char *entry,
                                 const VkmMslBinding *bindings, uint32_t count, uint32_t set_mask,
                                 const VkmMslSpec *spec, uint32_t spec_count, VkmCode *out)
{
    memset(out, 0, sizeof(*out));
    out->stage = VK_SHADER_STAGE_GEOMETRY_BIT;
    VkmGsInfo expand = {}, replay = {};
    VkResult r = VK_ERROR_INVALID_SHADER_NV;
    if (vkm_spirv_geometry(spirv, words, false, &expand) && vkm_spirv_geometry(spirv, words, true, &replay)) {
        r = variant(device, VK_SHADER_STAGE_VERTEX_BIT, expand.code, expand.words, entry, bindings, count, set_mask, spec, spec_count, g_gs_buffers,
                    2, &out->expand);
        if (r == VK_SUCCESS)
            r = variant(device, VK_SHADER_STAGE_VERTEX_BIT, replay.code, replay.words, entry, bindings, count, set_mask, spec, spec_count,
                        g_gs_buffers, 1, &out->replay);
    } else {
        VKM_ERROR("a geometry shader its emulation cannot rewrite");
    }
    free(expand.code);
    free(replay.code);
    out->gs = expand;
    out->gs.code = nullptr;
    if (r != VK_SUCCESS)
        vkm_code_free(out);
    return r;
}

VkResult vkm_compile(VkDevice device, const uint32_t *spirv, size_t size, const char *entry,
                     VkShaderStageFlagBits stage, const VkSpecializationInfo *spec, const VkmLayoutView *layout,
                     VkmCode *out)
{
    memset(out, 0, sizeof(*out));
    out->stage = stage;
    int model = execution_model(stage);
    if (model < 0 || size < 20 || spirv[0] != 0x07230203)
        return VK_ERROR_INVALID_SHADER_NV;
    uint32_t count = 0, set_mask = 0;
    for (uint32_t s = 0; s < layout->set_count; s++)
        if (layout->sets[s])
            count += layout->sets[s]->count;
    VkmMslBinding *bindings = vkm_zalloc(sizeof(*bindings) * MIME_MAX(count, 1u));
    VkmMslSpec *values = vkm_zalloc(sizeof(*values) * MIME_MAX(spec ? spec->mapEntryCount : 0, 1u));
    if (!bindings || !values) {
        free(bindings);
        free(values);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    count = 0;
    for (uint32_t s = 0; s < layout->set_count; s++) {
        VkDescriptorSetLayout l = layout->sets[s];
        if (!l)
            continue;
        set_mask |= 1u << s;
        for (uint32_t i = 0; i < l->count; i++) {
            const VkmBinding *b = &l->bindings[i];
            bindings[count++] = (VkmMslBinding){s, b->binding, b->type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK ? 1 : b->count,
                                                b->slot, b->slot + b->count, msl_kind(b->type)};
        }
    }
    uint32_t spec_count = 0;
    for (uint32_t i = 0; spec && i < spec->mapEntryCount; i++) {
        const VkSpecializationMapEntry *e = &spec->pMapEntries[i];
        uint64_t v = 0;
        memcpy(&v, (const uint8_t *)spec->pData + e->offset, MIME_MIN(e->size, sizeof(v)));
        values[spec_count++] = (VkmMslSpec){e->constantID, (uint32_t)e->size, v};
    }
    if (!entry)
        entry = "main";
    VkResult r;
    if (stage == VK_SHADER_STAGE_GEOMETRY_BIT)
        r = compile_geometry(device, spirv, size / 4, entry, bindings, count, set_mask, values, spec_count, out);
    else
        r = compile_msl(device, spirv, size / 4, entry, model, stage, bindings, count, set_mask, values, spec_count,
                        nullptr, false, out);
    VkmXfb xfb;
    if (r == VK_SUCCESS && (stage == VK_SHADER_STAGE_VERTEX_BIT || stage == VK_SHADER_STAGE_GEOMETRY_BIT) &&
        vkm_spirv_xfb(spirv, size / 4, &xfb) && (out->xfb = malloc(sizeof(xfb))))
        *out->xfb = xfb;
    VkmSource *src = nullptr;
    if (r == VK_SUCCESS && (stage == VK_SHADER_STAGE_VERTEX_BIT || stage == VK_SHADER_STAGE_FRAGMENT_BIT) &&
        (src = vkm_zalloc(sizeof(*src))) &&
        (src->spirv = malloc(size))) {
        memcpy(src->spirv, spirv, size);
        src->words = size / 4;
        snprintf(src->entry, sizeof(src->entry), "%s", entry);
        src->bindings = bindings;
        src->binding_count = count;
        src->set_mask = set_mask;
        src->spec = values;
        src->spec_count = spec_count;
        out->source = src;
        return r;
    }
    free(src);
    free(values);
    free(bindings);
    return r;
}

static pthread_mutex_t g_capture_lock = PTHREAD_MUTEX_INITIALIZER;

const VkmCode *vkm_code_capture(VkDevice device, const VkmCode *vs)
{
    if (!vs || !vs->source)
        return nullptr;
    VkmCode *code = (VkmCode *)vs;
    pthread_mutex_lock(&g_capture_lock);
    if (!code->capture && code->record_words != ~0u) {
        VkmSource *src = code->source;
        uint32_t *spirv = nullptr, words = 0, builtins = 0;
        size_t length = 0;
        VkmCode *c = nullptr;
        static const VkmMslBinding emu = {VKM_EMU_SET, 0, 1, VKM_BUFFER_AUX, VKM_BUFFER_AUX, VKM_MSL_BUFFER};
        if (vkm_spirv_capture(src->spirv, src->words, &spirv, &length, &words, &builtins) &&
            variant(device, VK_SHADER_STAGE_VERTEX_BIT, spirv, length, src->entry, src->bindings, src->binding_count,
                    src->set_mask, src->spec, src->spec_count, &emu, 1, &c) == VK_SUCCESS) {
            code->capture = c;
            code->record_words = words;
            code->record_builtins = builtins;
        } else {
            VKM_ERROR("a vertex shader its outputs cannot be captured from");
            code->record_words = ~0u;
        }
        free(spirv);
    }
    pthread_mutex_unlock(&g_capture_lock);
    return code->capture;
}

const VkmCode *vkm_code_counted(VkDevice device, const VkmCode *fs)
{
    if (!fs || !fs->source)
        return nullptr;
    VkmCode *code = (VkmCode *)fs;
    pthread_mutex_lock(&g_capture_lock);
    if (!code->counted && !code->count_failed) {
        VkmSource *src = code->source;
        uint32_t *spirv = nullptr;
        size_t length = 0;
        VkmCode *c = nullptr;
        static const VkmMslBinding counter = {VKM_EMU_SET, 0, 1, VKM_BUFFER_AUX, VKM_BUFFER_AUX, VKM_MSL_BUFFER};
        if (vkm_spirv_count(src->spirv, src->words, &spirv, &length) &&
            variant(device, VK_SHADER_STAGE_FRAGMENT_BIT, spirv, length, src->entry, src->bindings,
                    src->binding_count, src->set_mask, src->spec, src->spec_count, &counter, 1, &c) == VK_SUCCESS) {
            code->counted = c;
        } else {
            code->count_failed = true;
        }
        free(spirv);
    }
    pthread_mutex_unlock(&g_capture_lock);
    return code->counted;
}

const VkmCode *vkm_code_widened(VkDevice device, const VkmCode *vs, const uint8_t *widen)
{
    if (!vs || !vs->source)
        return nullptr;
    VkmCode *code = (VkmCode *)vs;
    pthread_mutex_lock(&g_capture_lock);
    VkmWidened *w = code->widened;
    while (w && memcmp(w->widen, widen, sizeof(w->widen)))
        w = w->next;
    if (!w && (w = vkm_zalloc(sizeof(*w)))) {
        VkmSource *src = code->source;
        memcpy(w->widen, widen, sizeof(w->widen));
        w->code = vkm_zalloc(sizeof(*w->code));
        if (w->code &&
            compile_msl(device, src->spirv, src->words, src->entry, execution_model(VK_SHADER_STAGE_VERTEX_BIT),
                        VK_SHADER_STAGE_VERTEX_BIT, src->bindings, src->binding_count, src->set_mask, src->spec,
                        src->spec_count, widen, false, w->code) != VK_SUCCESS) {
            free(w->code);
            w->code = nullptr;
        }
        w->next = code->widened;
        code->widened = w;
    }
    pthread_mutex_unlock(&g_capture_lock);
    return w ? w->code : nullptr;
}

const VkmCode *vkm_code_dual(VkDevice device, const VkmCode *fs)
{
    if (!fs || !fs->source)
        return nullptr;
    VkmCode *code = (VkmCode *)fs;
    pthread_mutex_lock(&g_capture_lock);
    if (!code->dual && !code->dual_failed) {
        VkmSource *src = code->source;
        VkmCode *c = vkm_zalloc(sizeof(*c));
        if (c && compile_msl(device, src->spirv, src->words, src->entry,
                             execution_model(VK_SHADER_STAGE_FRAGMENT_BIT), VK_SHADER_STAGE_FRAGMENT_BIT, src->bindings,
                             src->binding_count, src->set_mask, src->spec, src->spec_count, nullptr, true,
                             c) == VK_SUCCESS) {
            code->dual = c;
        } else {
            free(c);
            code->dual_failed = true;
        }
    }
    pthread_mutex_unlock(&g_capture_lock);
    return code->dual;
}

void vkm_code_free(VkmCode *code)
{
    while (code->widened) {
        VkmWidened *w = code->widened;
        code->widened = w->next;
        if (w->code) {
            vkm_code_free(w->code);
            free(w->code);
        }
        free(w);
    }
    [code->function release];
    [code->library release];
    VkmCode *variants[] = {code->capture, code->expand, code->replay, code->counted, code->dual};
    for (size_t i = 0; i < MIME_ARRAY_COUNT(variants); i++)
        if (variants[i]) {
            vkm_code_free(variants[i]);
            free(variants[i]);
        }
    free(code->xfb);
    if (code->source) {
        free(code->source->spirv);
        free(code->source->bindings);
        free(code->source->spec);
        free(code->source);
    }
    memset(code, 0, sizeof(*code));
}

/* ------------------------------------------------------------------ render pipeline states. */

uint32_t vkm_blend_factor(VkBlendFactor f)
{
    switch (f) {
    case VK_BLEND_FACTOR_ZERO: return MTLBlendFactorZero;
    case VK_BLEND_FACTOR_ONE: return MTLBlendFactorOne;
    case VK_BLEND_FACTOR_SRC_COLOR: return MTLBlendFactorSourceColor;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR: return MTLBlendFactorOneMinusSourceColor;
    case VK_BLEND_FACTOR_DST_COLOR: return MTLBlendFactorDestinationColor;
    case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR: return MTLBlendFactorOneMinusDestinationColor;
    case VK_BLEND_FACTOR_SRC_ALPHA: return MTLBlendFactorSourceAlpha;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA: return MTLBlendFactorOneMinusSourceAlpha;
    case VK_BLEND_FACTOR_DST_ALPHA: return MTLBlendFactorDestinationAlpha;
    case VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA: return MTLBlendFactorOneMinusDestinationAlpha;
    case VK_BLEND_FACTOR_CONSTANT_COLOR: return MTLBlendFactorBlendColor;
    case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR: return MTLBlendFactorOneMinusBlendColor;
    case VK_BLEND_FACTOR_CONSTANT_ALPHA: return MTLBlendFactorBlendAlpha;
    case VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA: return MTLBlendFactorOneMinusBlendAlpha;
    case VK_BLEND_FACTOR_SRC_ALPHA_SATURATE: return MTLBlendFactorSourceAlphaSaturated;
    case VK_BLEND_FACTOR_SRC1_COLOR: return MTLBlendFactorSource1Color;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR: return MTLBlendFactorOneMinusSource1Color;
    case VK_BLEND_FACTOR_SRC1_ALPHA: return MTLBlendFactorSource1Alpha;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA: return MTLBlendFactorOneMinusSource1Alpha;
    default: return MTLBlendFactorOne;
    }
}

uint32_t vkm_blend_op(VkBlendOp op)
{
    switch (op) {
    case VK_BLEND_OP_SUBTRACT: return MTLBlendOperationSubtract;
    case VK_BLEND_OP_REVERSE_SUBTRACT: return MTLBlendOperationReverseSubtract;
    case VK_BLEND_OP_MIN: return MTLBlendOperationMin;
    case VK_BLEND_OP_MAX: return MTLBlendOperationMax;
    default: return MTLBlendOperationAdd;
    }
}

static uint32_t write_mask(uint8_t vk)
{
    return ((vk & VK_COLOR_COMPONENT_R_BIT) ? MTLColorWriteMaskRed : 0) |
           ((vk & VK_COLOR_COMPONENT_G_BIT) ? MTLColorWriteMaskGreen : 0) |
           ((vk & VK_COLOR_COMPONENT_B_BIT) ? MTLColorWriteMaskBlue : 0) |
           ((vk & VK_COLOR_COMPONENT_A_BIT) ? MTLColorWriteMaskAlpha : 0);
}

static id<MTLRenderPipelineState> make_render_pipeline(VkDevice device, const VkmPipelineKey *key, const VkmCode *vs,
                                                      const VkmCode *fs)
{
    id<MTLRenderPipelineState> state;
    @autoreleasepool {
        MTL4RenderPipelineDescriptor *desc = [[MTL4RenderPipelineDescriptor alloc] init];
        desc.vertexFunctionDescriptor = vs->function;
        if (fs)
            desc.fragmentFunctionDescriptor = fs->function;
        if (vs->inputs) {
            MTLVertexDescriptor *vd = [MTLVertexDescriptor vertexDescriptor];
            MTLVertexAttributeDescriptorArray *attributes = vd.attributes;
            MTLVertexBufferLayoutDescriptorArray *layouts = vd.layouts;
            uint32_t used = 0;
            for (uint32_t i = 0; i < key->attribute_count; i++) {
                uint32_t location = key->attributes[i].location;
                if (!(vs->inputs & (1u << location)))
                    continue;
                MTLVertexAttributeDescriptor *a = attributes[location];
                a.format = (MTLVertexFormat)key->attributes[i].format;
                a.offset = key->attributes[i].offset;
                a.bufferIndex = VKM_BUFFER_VERTEX + key->attributes[i].binding;
                used |= 1u << key->attributes[i].binding;
            }
            for (uint32_t b = 0; b < VKM_VERTEX_BINDINGS; b++) {
                if (!(used & (1u << b)))
                    continue;
                MTLVertexBufferLayoutDescriptor *l = layouts[VKM_BUFFER_VERTEX + b];
                l.stride = ~(NSUInteger)0;
                l.stepFunction = (MTLVertexStepFunction)key->bindings[b].rate;
                l.stepRate = key->bindings[b].rate == MTLVertexStepFunctionConstant ? 0
                                                                       : MIME_MAX(key->bindings[b].divisor, 1u);
            }
            desc.vertexDescriptor = vd;
        }
        desc.rasterSampleCount = MIME_MAX(key->samples, 1);
        desc.alphaToCoverageState =
            key->alpha_to_coverage ? MTL4AlphaToCoverageStateEnabled : MTL4AlphaToCoverageStateDisabled;
        desc.alphaToOneState = key->alpha_to_one ? MTL4AlphaToOneStateEnabled : MTL4AlphaToOneStateDisabled;
        desc.rasterizationEnabled = !key->discard;
        desc.inputPrimitiveTopology = (MTLPrimitiveTopologyClass)key->topology_class;
        if (key->icb)
            desc.supportIndirectCommandBuffers = MTL4IndirectCommandBufferSupportStateEnabled;
        MTL4RenderPipelineColorAttachmentDescriptorArray *colors = desc.colorAttachments;
        for (uint32_t i = 0; i < VKM_COLORS; i++) {
            if (!key->colors[i])
                continue;
            MTL4RenderPipelineColorAttachmentDescriptor *c = colors[i];
            const VkmBlend *b = &key->blend[i];
            c.pixelFormat = (MTLPixelFormat)key->colors[i];
            c.writeMask =
                fs && (fs->outputs & (1u << i)) ? (MTLColorWriteMask)write_mask(b->write) : MTLColorWriteMaskNone;
            c.blendingState = b->enable ? MTL4BlendStateEnabled : MTL4BlendStateDisabled;
            if (b->enable) {
                c.sourceRGBBlendFactor = (MTLBlendFactor)vkm_blend_factor(b->src_color);
                c.destinationRGBBlendFactor = (MTLBlendFactor)vkm_blend_factor(b->dst_color);
                c.rgbBlendOperation = (MTLBlendOperation)vkm_blend_op(b->color_op);
                c.sourceAlphaBlendFactor = (MTLBlendFactor)vkm_blend_factor(b->src_alpha);
                c.destinationAlphaBlendFactor = (MTLBlendFactor)vkm_blend_factor(b->dst_alpha);
                c.alphaBlendOperation = (MTLBlendOperation)vkm_blend_op(b->alpha_op);
            }
        }
        NSError *error = nil;
        state = mime_pipeline_cache_render(device->pipelines, desc, (id *)&error);
        if (!state)
            VKM_ERROR("no render pipeline: %s", vkm_error_text(error));
        [desc release];
    }
    return state;
}

id<MTLRenderPipelineState> vkm_render_pipeline(VkDevice device, const VkmPipelineKey *key, const VkmCode *vs,
                                               const VkmCode *fs)
{
    uint64_t hash = vkm_hash(key, sizeof(*key), 1);
    pthread_mutex_lock(&g_lock);
    id<MTLRenderPipelineState> state = vkm_cache_find(g_pipelines, key, sizeof(*key), hash);
    pthread_mutex_unlock(&g_lock);
    if (state)
        return state;
    state = make_render_pipeline(device, key, vs, fs);
    if (!state)
        return nullptr;
    pthread_mutex_lock(&g_lock);
    id raced = vkm_cache_find(g_pipelines, key, sizeof(*key), hash);
    if (raced) {
        [state release];
        state = raced;
    } else {
        vkm_cache_add(&g_pipelines, key, sizeof(*key), hash, state);
    }
    pthread_mutex_unlock(&g_lock);
    return state;
}

id<MTLComputePipelineState> vkm_compute_pipeline(VkDevice device, const VkmCode *code, bool icb)
{
    uint64_t hash = code->hash ^ ((uint64_t)code->local.width << 40 | (uint64_t)code->local.height << 20 |
                                  (uint64_t)code->local.depth) ^ (icb ? 1ull << 63 : 0);
    pthread_mutex_lock(&g_lock);
    id<MTLComputePipelineState> state = vkm_cache_find(g_compute, &hash, sizeof(hash), hash);
    pthread_mutex_unlock(&g_lock);
    if (state)
        return state;
    @autoreleasepool {
        MTL4ComputePipelineDescriptor *desc = [[MTL4ComputePipelineDescriptor alloc] init];
        desc.computeFunctionDescriptor = code->function;
        desc.requiredThreadsPerThreadgroup = code->local;
        if (icb)
            desc.supportIndirectCommandBuffers = MTL4IndirectCommandBufferSupportStateEnabled;
        NSError *error = nil;
        state = mime_pipeline_cache_compute(device->pipelines, desc, (id *)&error);
        if (!state)
            VKM_ERROR("no compute pipeline: %s", vkm_error_text(error));
        [desc release];
    }
    if (!state)
        return nullptr;
    pthread_mutex_lock(&g_lock);
    id raced = vkm_cache_find(g_compute, &hash, sizeof(hash), hash);
    if (raced) {
        [state release];
        state = raced;
    } else {
        vkm_cache_add(&g_compute, &hash, sizeof(hash), hash, state);
    }
    pthread_mutex_unlock(&g_lock);
    return state;
}

/* ------------------------------------------------------------------ depth stencil states. */

uint32_t vkm_stencil_op(VkStencilOp op)
{
    switch (op) {
    case VK_STENCIL_OP_ZERO: return MTLStencilOperationZero;
    case VK_STENCIL_OP_REPLACE: return MTLStencilOperationReplace;
    case VK_STENCIL_OP_INCREMENT_AND_CLAMP: return MTLStencilOperationIncrementClamp;
    case VK_STENCIL_OP_DECREMENT_AND_CLAMP: return MTLStencilOperationDecrementClamp;
    case VK_STENCIL_OP_INVERT: return MTLStencilOperationInvert;
    case VK_STENCIL_OP_INCREMENT_AND_WRAP: return MTLStencilOperationIncrementWrap;
    case VK_STENCIL_OP_DECREMENT_AND_WRAP: return MTLStencilOperationDecrementWrap;
    default: return MTLStencilOperationKeep;
    }
}

static MTLStencilDescriptor *stencil_descriptor(const VkmStencilFace *f)
{
    MTLStencilDescriptor *d = [[MTLStencilDescriptor alloc] init];
    d.stencilCompareFunction = (MTLCompareFunction)vkm_compare(f->compare);
    d.stencilFailureOperation = (MTLStencilOperation)vkm_stencil_op(f->fail);
    d.depthFailureOperation = (MTLStencilOperation)vkm_stencil_op(f->depth_fail);
    d.depthStencilPassOperation = (MTLStencilOperation)vkm_stencil_op(f->pass);
    d.readMask = f->read;
    d.writeMask = f->write;
    return d;
}

id<MTLDepthStencilState> vkm_depth_state(VkDevice device, const VkmDepthKey *key)
{
    uint64_t hash = vkm_hash(key, sizeof(*key), 2);
    pthread_mutex_lock(&g_lock);
    id<MTLDepthStencilState> state = vkm_cache_find(g_depth_states, key, sizeof(*key), hash);
    pthread_mutex_unlock(&g_lock);
    if (state)
        return state;
    @autoreleasepool {
        MTLDepthStencilDescriptor *desc = [[MTLDepthStencilDescriptor alloc] init];
        desc.depthCompareFunction =
            key->test ? (MTLCompareFunction)vkm_compare(key->compare) : MTLCompareFunctionAlways;
        desc.depthWriteEnabled = key->test && key->write;
        if (key->stencil) {
            MTLStencilDescriptor *front = stencil_descriptor(&key->front), *back = stencil_descriptor(&key->back);
            desc.frontFaceStencil = front;
            desc.backFaceStencil = back;
            [front release];
            [back release];
        }
        state = [device->mtl newDepthStencilStateWithDescriptor:desc];
        [desc release];
    }
    if (!state)
        return nullptr;
    pthread_mutex_lock(&g_lock);
    id raced = vkm_cache_find(g_depth_states, key, sizeof(*key), hash);
    if (raced) {
        [state release];
        state = raced;
    } else {
        vkm_cache_add(&g_depth_states, key, sizeof(*key), hash, state);
    }
    pthread_mutex_unlock(&g_lock);
    return state;
}

/* ------------------------------------------------------------------ shader modules and objects. */

static VKAPI_ATTR VkResult VKAPI_CALL CreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo *info,
                                                         const VkAllocationCallbacks *a, VkShaderModule *out)
{
    VkShaderModule m = vkm_zalloc(sizeof(*m));
    if (!m || !(m->code = malloc(info->codeSize))) {
        free(m);
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    memcpy(m->code, info->pCode, info->codeSize);
    m->size = info->codeSize;
    *out = m;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroyShaderModule(VkDevice device, VkShaderModule m,
                                                      const VkAllocationCallbacks *a)
{
    if (!m)
        return;
    free(m->code);
    free(m);
}

static VkmLayoutView layout_view(uint32_t set_count, const VkDescriptorSetLayout *sets, uint32_t push_count,
                                 const VkPushConstantRange *push)
{
    VkmLayoutView v = {MIME_MIN(set_count, (uint32_t)VKM_SETS)};
    for (uint32_t i = 0; i < v.set_count; i++)
        v.sets[i] = sets[i];
    for (uint32_t i = 0; i < push_count; i++)
        v.push_size = MIME_MAX(v.push_size, push[i].offset + push[i].size);
    return v;
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateShadersEXT(VkDevice device, uint32_t count,
                                                       const VkShaderCreateInfoEXT *infos,
                                                       const VkAllocationCallbacks *a, VkShaderEXT *out)
{
    VkResult result = VK_SUCCESS;
    for (uint32_t i = 0; i < count; i++)
        out[i] = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < count; i++) {
        const VkShaderCreateInfoEXT *info = &infos[i];
        if (info->codeType != VK_SHADER_CODE_TYPE_SPIRV_EXT) {
            result = VK_INCOMPATIBLE_SHADER_BINARY_EXT;
            continue;
        }
        VkShaderEXT s = vkm_zalloc(sizeof(*s));
        if (!s)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        VkmLayoutView layout = layout_view(info->setLayoutCount, info->pSetLayouts, info->pushConstantRangeCount,
                                           info->pPushConstantRanges);
        VkResult r = vkm_compile(device, info->pCode, info->codeSize, info->pName, info->stage,
                                 info->pSpecializationInfo, &layout, &s->code);
        if (r != VK_SUCCESS) {
            free(s);
            result = r;
            continue;
        }
        out[i] = s;
    }
    return result;
}

static VKAPI_ATTR void VKAPI_CALL DestroyShaderEXT(VkDevice device, VkShaderEXT s, const VkAllocationCallbacks *a)
{
    if (!s)
        return;
    vkm_code_free(&s->code);
    free(s);
}

static VKAPI_ATTR VkResult VKAPI_CALL GetShaderBinaryDataEXT(VkDevice device, VkShaderEXT s, size_t *size, void *data)
{
    *size = 0;
    return VK_SUCCESS;
}

/* ------------------------------------------------------------------ pipelines. */

int vkm_dynamic_bit(VkDynamicState state)
{
    switch (state) {
    case VK_DYNAMIC_STATE_VIEWPORT: return VKM_D_VIEWPORT;
    case VK_DYNAMIC_STATE_SCISSOR: return VKM_D_SCISSOR;
    case VK_DYNAMIC_STATE_LINE_WIDTH: return VKM_D_LINE_WIDTH;
    case VK_DYNAMIC_STATE_DEPTH_BIAS: return VKM_D_DEPTH_BIAS;
    case VK_DYNAMIC_STATE_BLEND_CONSTANTS: return VKM_D_BLEND_CONSTANTS;
    case VK_DYNAMIC_STATE_DEPTH_BOUNDS: return VKM_D_DEPTH_BOUNDS;
    case VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK: return VKM_D_STENCIL_COMPARE_MASK;
    case VK_DYNAMIC_STATE_STENCIL_WRITE_MASK: return VKM_D_STENCIL_WRITE_MASK;
    case VK_DYNAMIC_STATE_STENCIL_REFERENCE: return VKM_D_STENCIL_REFERENCE;
    case VK_DYNAMIC_STATE_CULL_MODE: return VKM_D_CULL_MODE;
    case VK_DYNAMIC_STATE_FRONT_FACE: return VKM_D_FRONT_FACE;
    case VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY: return VKM_D_TOPOLOGY;
    case VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT: return VKM_D_VIEWPORT_COUNT;
    case VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT: return VKM_D_SCISSOR_COUNT;
    case VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE: return VKM_D_STRIDE;
    case VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE: return VKM_D_DEPTH_TEST;
    case VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE: return VKM_D_DEPTH_WRITE;
    case VK_DYNAMIC_STATE_DEPTH_COMPARE_OP: return VKM_D_DEPTH_COMPARE;
    case VK_DYNAMIC_STATE_DEPTH_BOUNDS_TEST_ENABLE: return VKM_D_BOUNDS_TEST;
    case VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE: return VKM_D_STENCIL_TEST;
    case VK_DYNAMIC_STATE_STENCIL_OP: return VKM_D_STENCIL_OP;
    case VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE: return VKM_D_DISCARD;
    case VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE: return VKM_D_BIAS_ENABLE;
    case VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE: return VKM_D_RESTART;
    case VK_DYNAMIC_STATE_VERTEX_INPUT_EXT: return VKM_D_VERTEX_INPUT;
    case VK_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT: return VKM_D_DEPTH_CLAMP;
    case VK_DYNAMIC_STATE_POLYGON_MODE_EXT: return VKM_D_POLYGON_MODE;
    case VK_DYNAMIC_STATE_RASTERIZATION_SAMPLES_EXT: return VKM_D_SAMPLES;
    case VK_DYNAMIC_STATE_SAMPLE_MASK_EXT: return VKM_D_SAMPLE_MASK;
    case VK_DYNAMIC_STATE_ALPHA_TO_COVERAGE_ENABLE_EXT: return VKM_D_ALPHA_TO_COVERAGE;
    case VK_DYNAMIC_STATE_ALPHA_TO_ONE_ENABLE_EXT: return VKM_D_ALPHA_TO_ONE;
    case VK_DYNAMIC_STATE_COLOR_BLEND_ENABLE_EXT: return VKM_D_BLEND_ENABLE;
    case VK_DYNAMIC_STATE_COLOR_BLEND_EQUATION_EXT: return VKM_D_BLEND_EQUATION;
    case VK_DYNAMIC_STATE_COLOR_WRITE_MASK_EXT: return VKM_D_WRITE_MASK;
    case VK_DYNAMIC_STATE_DEPTH_CLIP_ENABLE_EXT: return VKM_D_DEPTH_CLIP;
    default: return -1;
    }
}

static void stencil_face(VkmStencilFace *out, const VkStencilOpState *s)
{
    *out = (VkmStencilFace){(uint8_t)s->failOp, (uint8_t)s->passOp, (uint8_t)s->depthFailOp, (uint8_t)s->compareOp,
                            s->compareMask, s->writeMask};
}

/* the state a graphics pipeline fixes, in the command buffer's terms. */
static void graphics_static(const VkGraphicsPipelineCreateInfo *info, VkmGfxStatic *g)
{
    VkmDynamic *d = &g->state;
    d->depth_clip = VKM_CLIP_UNSET;
    d->sample_mask = ~0u;
    d->samples = 1;
    if (info->pDynamicState)
        for (uint32_t i = 0; i < info->pDynamicState->dynamicStateCount; i++) {
            int bit = vkm_dynamic_bit(info->pDynamicState->pDynamicStates[i]);
            if (bit >= 0)
                g->dynamic |= 1ull << bit;
        }
    const VkPipelineVertexInputStateCreateInfo *vi = info->pVertexInputState;
    if (vi) {
        d->vertex.binding_count = MIME_MIN(vi->vertexBindingDescriptionCount, (uint32_t)VKM_VERTEX_BINDINGS);
        d->vertex.attribute_count = MIME_MIN(vi->vertexAttributeDescriptionCount, (uint32_t)VKM_VERTEX_ATTRIBUTES);
        const VkPipelineVertexInputDivisorStateCreateInfoKHR *divisors =
            vkm_find(vi->pNext, VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_KHR);
        for (uint32_t i = 0; i < d->vertex.binding_count; i++) {
            const VkVertexInputBindingDescription *b = &vi->pVertexBindingDescriptions[i];
            uint32_t divisor = 1;
            for (uint32_t k = 0; divisors && k < divisors->vertexBindingDivisorCount; k++)
                if (divisors->pVertexBindingDivisors[k].binding == b->binding)
                    divisor = divisors->pVertexBindingDivisors[k].divisor;
            d->vertex.bindings[i] = (VkVertexInputBindingDescription2EXT){
                VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT, nullptr, b->binding, b->stride, b->inputRate,
                divisor};
            if (b->binding < VKM_VERTEX_BINDINGS)
                d->strides[b->binding] = b->stride;
        }
        for (uint32_t i = 0; i < d->vertex.attribute_count; i++) {
            const VkVertexInputAttributeDescription *at = &vi->pVertexAttributeDescriptions[i];
            d->vertex.attributes[i] = (VkVertexInputAttributeDescription2EXT){
                VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT, nullptr, at->location, at->binding,
                at->format, at->offset};
        }
    }
    if (info->pInputAssemblyState) {
        d->topology = (uint8_t)info->pInputAssemblyState->topology;
        d->restart = (uint8_t)info->pInputAssemblyState->primitiveRestartEnable;
    }
    if (info->pViewportState) {
        d->viewport_count = MIME_MIN(info->pViewportState->viewportCount, (uint32_t)VKM_VIEWPORTS);
        d->scissor_count = MIME_MIN(info->pViewportState->scissorCount, (uint32_t)VKM_VIEWPORTS);
        if (info->pViewportState->pViewports)
            memcpy(d->viewports, info->pViewportState->pViewports, sizeof(VkViewport) * d->viewport_count);
        if (info->pViewportState->pScissors)
            memcpy(d->scissors, info->pViewportState->pScissors, sizeof(VkRect2D) * d->scissor_count);
    }
    const VkPipelineRasterizationStateCreateInfo *rs = info->pRasterizationState;
    if (rs) {
        d->discard = (uint8_t)rs->rasterizerDiscardEnable;
        d->cull = (uint8_t)rs->cullMode;
        d->front = (uint8_t)rs->frontFace;
        d->polygon = (uint8_t)rs->polygonMode;
        d->depth_clamp = (uint8_t)rs->depthClampEnable;
        d->bias_enable = (uint8_t)rs->depthBiasEnable;
        d->bias_constant = rs->depthBiasConstantFactor;
        d->bias_clamp = rs->depthBiasClamp;
        d->bias_slope = rs->depthBiasSlopeFactor;
        const VkPipelineRasterizationDepthClipStateCreateInfoEXT *clip =
            vkm_find(rs->pNext, VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT);
        d->depth_clip = clip ? (uint8_t)clip->depthClipEnable : VKM_CLIP_UNSET;
    }
    const VkPipelineMultisampleStateCreateInfo *ms = info->pMultisampleState;
    if (ms && !d->discard) {
        d->samples = (uint8_t)ms->rasterizationSamples;
        d->alpha_to_coverage = (uint8_t)ms->alphaToCoverageEnable;
        d->alpha_to_one = (uint8_t)ms->alphaToOneEnable;
        if (ms->pSampleMask)
            d->sample_mask = ms->pSampleMask[0];
    }
    const VkPipelineDepthStencilStateCreateInfo *ds = info->pDepthStencilState;
    if (ds) {
        d->depth_test = (uint8_t)ds->depthTestEnable;
        d->depth_write = (uint8_t)ds->depthWriteEnable;
        d->depth_compare = (uint8_t)ds->depthCompareOp;
        d->bounds_test = (uint8_t)ds->depthBoundsTestEnable;
        d->bounds_min = ds->minDepthBounds;
        d->bounds_max = ds->maxDepthBounds;
        d->stencil_test = (uint8_t)ds->stencilTestEnable;
        stencil_face(&d->front_stencil, &ds->front);
        stencil_face(&d->back_stencil, &ds->back);
        d->ref_front = ds->front.reference;
        d->ref_back = ds->back.reference;
    }
    const VkPipelineColorBlendStateCreateInfo *cb = info->pColorBlendState;
    if (cb) {
        for (uint32_t i = 0; i < MIME_MIN(cb->attachmentCount, (uint32_t)VKM_COLORS); i++) {
            const VkPipelineColorBlendAttachmentState *a = &cb->pAttachments[i];
            d->blend[i] = (VkmBlend){(uint8_t)a->blendEnable,       (uint8_t)a->colorWriteMask,
                                     (uint8_t)a->srcColorBlendFactor, (uint8_t)a->dstColorBlendFactor,
                                     (uint8_t)a->colorBlendOp,        (uint8_t)a->srcAlphaBlendFactor,
                                     (uint8_t)a->dstAlphaBlendFactor, (uint8_t)a->alphaBlendOp};
        }
        memcpy(d->blend_constants, cb->blendConstants, sizeof(d->blend_constants));
    }
}

static const VkPipelineShaderStageCreateInfo *stage_of(const VkGraphicsPipelineCreateInfo *info,
                                                       VkShaderStageFlagBits stage)
{
    for (uint32_t i = 0; i < info->stageCount; i++)
        if (info->pStages[i].stage == stage)
            return &info->pStages[i];
    return nullptr;
}

static VkResult stage_compile(VkDevice device, const VkPipelineShaderStageCreateInfo *stage, VkPipelineLayout layout,
                              VkmCode *out)
{
    VkmLayoutView view = {};
    if (layout) {
        view.set_count = layout->set_count;
        memcpy(view.sets, layout->sets, sizeof(view.sets));
        view.push_size = layout->push_size;
    }
    const uint32_t *code;
    size_t size;
    const VkShaderModuleCreateInfo *inline_code = vkm_find(stage->pNext, VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    if (stage->module) {
        code = stage->module->code;
        size = stage->module->size;
    } else if (inline_code) {
        code = inline_code->pCode;
        size = inline_code->codeSize;
    } else {
        return VK_ERROR_INVALID_SHADER_NV;
    }
    return vkm_compile(device, code, size, stage->pName, stage->stage, stage->pSpecializationInfo, &view, out);
}

static void pipeline_free(VkPipeline p)
{
    if (!p)
        return;
    for (int i = 0; i < 3; i++)
        if (p->has[i])
            vkm_code_free(&p->stages[i]);
    free(p->gfx);
    free(p);
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateGraphicsPipelines(VkDevice device, VkPipelineCache cache, uint32_t count,
                                                              const VkGraphicsPipelineCreateInfo *infos,
                                                              const VkAllocationCallbacks *a, VkPipeline *out)
{
    VkResult result = VK_SUCCESS;
    for (uint32_t i = 0; i < count; i++) {
        out[i] = VK_NULL_HANDLE;
        const VkGraphicsPipelineCreateInfo *info = &infos[i];
        VkPipeline p = vkm_zalloc(sizeof(*p));
        if (!p || !(p->gfx = vkm_zalloc(sizeof(*p->gfx)))) {
            free(p);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        p->bind = VK_PIPELINE_BIND_POINT_GRAPHICS;
        graphics_static(info, p->gfx);
        const VkPipelineShaderStageCreateInfo *vs = stage_of(info, VK_SHADER_STAGE_VERTEX_BIT);
        const VkPipelineShaderStageCreateInfo *fs = stage_of(info, VK_SHADER_STAGE_FRAGMENT_BIT);
        VkResult r = vs ? stage_compile(device, vs, info->layout, &p->stages[0]) : VK_ERROR_FEATURE_NOT_PRESENT;
        p->has[0] = r == VK_SUCCESS;
        if (r == VK_SUCCESS && fs) {
            r = stage_compile(device, fs, info->layout, &p->stages[1]);
            p->has[1] = r == VK_SUCCESS;
        }
        const VkPipelineShaderStageCreateInfo *gs = stage_of(info, VK_SHADER_STAGE_GEOMETRY_BIT);
        if (r == VK_SUCCESS && gs) {
            r = stage_compile(device, gs, info->layout, &p->stages[2]);
            p->has[2] = r == VK_SUCCESS;
        }
        if (r != VK_SUCCESS) {
            pipeline_free(p);
            result = r;
            continue;
        }
        out[i] = p;
    }
    return result;
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateComputePipelines(VkDevice device, VkPipelineCache cache, uint32_t count,
                                                             const VkComputePipelineCreateInfo *infos,
                                                             const VkAllocationCallbacks *a, VkPipeline *out)
{
    VkResult result = VK_SUCCESS;
    for (uint32_t i = 0; i < count; i++) {
        out[i] = VK_NULL_HANDLE;
        VkPipeline p = vkm_zalloc(sizeof(*p));
        if (!p)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        p->bind = VK_PIPELINE_BIND_POINT_COMPUTE;
        VkResult r = stage_compile(device, &infos[i].stage, infos[i].layout, &p->stages[0]);
        p->has[0] = r == VK_SUCCESS;
        if (r == VK_SUCCESS && !(p->compute = [vkm_compute_pipeline(device, &p->stages[0], false) retain]))
            r = VK_ERROR_INVALID_SHADER_NV;
        if (r != VK_SUCCESS) {
            pipeline_free(p);
            result = r;
            continue;
        }
        out[i] = p;
    }
    return result;
}

static VKAPI_ATTR void VKAPI_CALL DestroyPipeline(VkDevice device, VkPipeline p, const VkAllocationCallbacks *a)
{
    if (!p)
        return;
    [p->compute release];
    pipeline_free(p);
}

static VKAPI_ATTR VkResult VKAPI_CALL CreatePipelineCache(VkDevice device, const VkPipelineCacheCreateInfo *info,
                                                          const VkAllocationCallbacks *a, VkPipelineCache *out)
{
    *out = vkm_zalloc(sizeof(**out));
    return *out ? VK_SUCCESS : VK_ERROR_OUT_OF_HOST_MEMORY;
}

static VKAPI_ATTR void VKAPI_CALL DestroyPipelineCache(VkDevice device, VkPipelineCache cache,
                                                       const VkAllocationCallbacks *a)
{
    free(cache);
}

static VKAPI_ATTR VkResult VKAPI_CALL GetPipelineCacheData(VkDevice device, VkPipelineCache cache, size_t *size,
                                                           void *data)
{
    *size = 0;
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL MergePipelineCaches(VkDevice device, VkPipelineCache dst, uint32_t count,
                                                          const VkPipelineCache *src)
{
    return VK_SUCCESS;
}

const VkmEntry vkm_shader_entries[] = {
    VKM_ENTRY(CreateShaderModule),
    VKM_ENTRY(DestroyShaderModule),
    VKM_ENTRY(CreateShadersEXT),
    VKM_ENTRY(DestroyShaderEXT),
    VKM_ENTRY(GetShaderBinaryDataEXT),
    VKM_ENTRY(CreateGraphicsPipelines),
    VKM_ENTRY(CreateComputePipelines),
    VKM_ENTRY(DestroyPipeline),
    VKM_ENTRY(CreatePipelineCache),
    VKM_ENTRY(DestroyPipelineCache),
    VKM_ENTRY(GetPipelineCacheData),
    VKM_ENTRY(MergePipelineCaches),
    {nullptr, nullptr},
};
