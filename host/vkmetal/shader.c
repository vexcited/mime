/*
 * shaders and pipelines. SPIR-V becomes MSL with argument buffers laid out like the pipeline layout, compiled
 * once per MSL text for the process: every device shares the GPU, so libraries, render pipeline states and
 * depth stencil states are cached for all of them.
 */
#include "vkm_cmd.h"

typedef struct Library {
    struct Library *next;
    uint64_t hash;
    id library;
} Library;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static Library *g_libraries;
static VkmCache *g_pipelines, *g_depth_states, *g_compute;

/* ------------------------------------------------------------------ MSL libraries. */

static id compile_options(void)
{
    id options = mtl_new("MTLCompileOptions");
    M_V_U(options, "setLanguageVersion:", MTLLanguageVersion3_2);
    M_V_I(options, "setMathMode:", MTLMathModeSafe);
    M_V_B(options, "setPreserveInvariance:", true);
    return options;
}

static id library_get(VkDevice device, const char *source, uint64_t hash)
{
    pthread_mutex_lock(&g_lock);
    for (Library *l = g_libraries; l; l = l->next)
        if (l->hash == hash) {
            id library = mtl_retain(l->library);
            pthread_mutex_unlock(&g_lock);
            return library;
        }
    pthread_mutex_unlock(&g_lock);
    void *pool = objc_autoreleasePoolPush();
    id desc = mtl_new("MTL4LibraryDescriptor");
    id options = compile_options();
    M_V_ID(desc, "setSource:", mtl_string(source));
    M_V_ID(desc, "setOptions:", options);
    id error = nullptr;
    id library = MTL_MSG(id (*)(id, SEL, id, id *), device->compiler, "newLibraryWithDescriptor:error:", desc, &error);
    mtl_release(options);
    mtl_release(desc);
    if (!library) {
        const char *text = mtl_error_text(error);
        VKM_ERROR("an MSL library does not compile: %.2000s", text);
        mime_debug("vkmetal: the MSL was:\n%s", source);
    }
    objc_autoreleasePoolPop(pool);
    if (!library)
        return nullptr;
    Library *l = vkm_zalloc(sizeof(*l));
    if (l) {
        l->hash = hash;
        l->library = mtl_retain(library);
        pthread_mutex_lock(&g_lock);
        l->next = g_libraries;
        g_libraries = l;
        pthread_mutex_unlock(&g_lock);
    }
    return library;
}

id vkm_library(VkDevice device, const char *source)
{
    return library_get(device, source, vkm_hash(source, strlen(source), 0x6d736c));
}

id vkm_function(id library, const char *name)
{
    id f = mtl_new("MTL4LibraryFunctionDescriptor");
    M_V_ID(f, "setName:", mtl_string(name));
    M_V_ID(f, "setLibrary:", library);
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
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER: return VKM_MSL_IMAGE;
    default: return VKM_MSL_BUFFER;
    }
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
    VkmMslSpec *values = spec ? vkm_zalloc(sizeof(*values) * MIME_MAX(spec->mapEntryCount, 1u)) : nullptr;
    if (!bindings || (spec && !values)) {
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
    VkmMslResult r;
    bool ok = vkm_msl(spirv, size / 4, entry ? entry : "main", model, bindings, count, set_mask, values, spec_count, &r);
    free(values);
    free(bindings);
    if (!ok) {
        VKM_ERROR("a shader does not translate to MSL: %s", r.error);
        return VK_ERROR_INVALID_SHADER_NV;
    }
    out->hash = vkm_hash(r.source, strlen(r.source), vkm_hash(r.entry, strlen(r.entry), 0));
    out->library = library_get(device, r.source, out->hash);
    free(r.source);
    if (!out->library)
        return VK_ERROR_INVALID_SHADER_NV;
    out->function = vkm_function(out->library, r.entry);
    out->local = r.local;
    out->inputs = r.inputs;
    memcpy(out->input, r.input, sizeof(out->input));
    out->outputs = r.outputs;
    out->writes_depth = r.writes_depth;
    out->writes_stencil = r.writes_stencil;
    return VK_SUCCESS;
}

void vkm_code_free(VkmCode *code)
{
    mtl_release(code->function);
    mtl_release(code->library);
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

static id make_render_pipeline(VkDevice device, const VkmPipelineKey *key, const VkmCode *vs, const VkmCode *fs)
{
    void *pool = objc_autoreleasePoolPush();
    id desc = mtl_new("MTL4RenderPipelineDescriptor");
    M_V_ID(desc, "setVertexFunctionDescriptor:", vs->function);
    if (fs)
        M_V_ID(desc, "setFragmentFunctionDescriptor:", fs->function);
    if (vs->inputs) {
        id vd = M_ID(MTL_CLASS("MTLVertexDescriptor"), "vertexDescriptor");
        id attributes = M_ID(vd, "attributes"), layouts = M_ID(vd, "layouts");
        uint32_t used = 0;
        for (uint32_t i = 0; i < key->attribute_count; i++) {
            uint32_t location = key->attributes[i].location;
            if (!(vs->inputs & (1u << location)))
                continue;
            id a = M_AT(attributes, location);
            M_V_U(a, "setFormat:", key->attributes[i].format);
            M_V_U(a, "setOffset:", key->attributes[i].offset);
            M_V_U(a, "setBufferIndex:", VKM_BUFFER_VERTEX + key->attributes[i].binding);
            used |= 1u << key->attributes[i].binding;
        }
        for (uint32_t b = 0; b < VKM_VERTEX_BINDINGS; b++) {
            if (!(used & (1u << b)))
                continue;
            id l = M_AT(layouts, VKM_BUFFER_VERTEX + b);
            M_V_U(l, "setStride:", ~(NSUInteger)0);
            M_V_U(l, "setStepFunction:", key->bindings[b].rate);
            M_V_U(l, "setStepRate:", key->bindings[b].rate == MTLVertexStepFunctionConstant ? 0
                                     : MIME_MAX(key->bindings[b].divisor, 1u));
        }
        M_V_ID(desc, "setVertexDescriptor:", vd);
    }
    M_V_U(desc, "setRasterSampleCount:", MIME_MAX(key->samples, 1));
    M_V_I(desc, "setAlphaToCoverageState:",
          key->alpha_to_coverage ? MTL4AlphaToCoverageStateEnabled : MTL4AlphaToCoverageStateDisabled);
    M_V_I(desc, "setAlphaToOneState:", key->alpha_to_one ? MTL4AlphaToOneStateEnabled : MTL4AlphaToOneStateDisabled);
    M_V_B(desc, "setRasterizationEnabled:", !key->discard);
    M_V_U(desc, "setInputPrimitiveTopology:", key->topology_class);
    id colors = M_ID(desc, "colorAttachments");
    for (uint32_t i = 0; i < VKM_COLORS; i++) {
        if (!key->colors[i])
            continue;
        id c = M_AT(colors, i);
        const VkmBlend *b = &key->blend[i];
        M_V_U(c, "setPixelFormat:", key->colors[i]);
        M_V_U(c, "setWriteMask:", fs && (fs->outputs & (1u << i)) ? write_mask(b->write) : MTLColorWriteMaskNone);
        M_V_I(c, "setBlendingState:", b->enable ? MTL4BlendStateEnabled : MTL4BlendStateDisabled);
        if (b->enable) {
            M_V_U(c, "setSourceRGBBlendFactor:", vkm_blend_factor(b->src_color));
            M_V_U(c, "setDestinationRGBBlendFactor:", vkm_blend_factor(b->dst_color));
            M_V_U(c, "setRgbBlendOperation:", vkm_blend_op(b->color_op));
            M_V_U(c, "setSourceAlphaBlendFactor:", vkm_blend_factor(b->src_alpha));
            M_V_U(c, "setDestinationAlphaBlendFactor:", vkm_blend_factor(b->dst_alpha));
            M_V_U(c, "setAlphaBlendOperation:", vkm_blend_op(b->alpha_op));
        }
    }
    id error = nullptr;
    id state = MTL_MSG(id (*)(id, SEL, id, id, id *), device->compiler,
                       "newRenderPipelineStateWithDescriptor:compilerTaskOptions:error:", desc, nullptr, &error);
    if (!state)
        VKM_ERROR("no render pipeline: %s", mtl_error_text(error));
    mtl_release(desc);
    objc_autoreleasePoolPop(pool);
    return state;
}

id vkm_render_pipeline(VkDevice device, const VkmPipelineKey *key, const VkmCode *vs, const VkmCode *fs)
{
    uint64_t hash = vkm_hash(key, sizeof(*key), 1);
    pthread_mutex_lock(&g_lock);
    id state = vkm_cache_find(g_pipelines, key, sizeof(*key), hash);
    pthread_mutex_unlock(&g_lock);
    if (state)
        return state;
    state = make_render_pipeline(device, key, vs, fs);
    if (!state)
        return nullptr;
    pthread_mutex_lock(&g_lock);
    id raced = vkm_cache_find(g_pipelines, key, sizeof(*key), hash);
    if (raced) {
        mtl_release(state);
        state = raced;
    } else {
        vkm_cache_add(&g_pipelines, key, sizeof(*key), hash, state);
    }
    pthread_mutex_unlock(&g_lock);
    return state;
}

id vkm_compute_pipeline(VkDevice device, const VkmCode *code)
{
    uint64_t hash = code->hash ^ ((uint64_t)code->local.width << 40 | (uint64_t)code->local.height << 20 |
                                  (uint64_t)code->local.depth);
    pthread_mutex_lock(&g_lock);
    id state = vkm_cache_find(g_compute, &hash, sizeof(hash), hash);
    pthread_mutex_unlock(&g_lock);
    if (state)
        return state;
    void *pool = objc_autoreleasePoolPush();
    id desc = mtl_new("MTL4ComputePipelineDescriptor");
    M_V_ID(desc, "setComputeFunctionDescriptor:", code->function);
    MTL_MSG(void (*)(id, SEL, MTLSize), desc, "setRequiredThreadsPerThreadgroup:", code->local);
    id error = nullptr;
    state = MTL_MSG(id (*)(id, SEL, id, id, id *), device->compiler,
                    "newComputePipelineStateWithDescriptor:compilerTaskOptions:error:", desc, nullptr, &error);
    if (!state)
        VKM_ERROR("no compute pipeline: %s", mtl_error_text(error));
    mtl_release(desc);
    objc_autoreleasePoolPop(pool);
    if (!state)
        return nullptr;
    pthread_mutex_lock(&g_lock);
    id raced = vkm_cache_find(g_compute, &hash, sizeof(hash), hash);
    if (raced) {
        mtl_release(state);
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

static id stencil_descriptor(const VkmStencilFace *f)
{
    id d = mtl_new("MTLStencilDescriptor");
    M_V_U(d, "setStencilCompareFunction:", vkm_compare(f->compare));
    M_V_U(d, "setStencilFailureOperation:", vkm_stencil_op(f->fail));
    M_V_U(d, "setDepthFailureOperation:", vkm_stencil_op(f->depth_fail));
    M_V_U(d, "setDepthStencilPassOperation:", vkm_stencil_op(f->pass));
    M_V_U(d, "setReadMask:", f->read);
    M_V_U(d, "setWriteMask:", f->write);
    return d;
}

id vkm_depth_state(VkDevice device, const VkmDepthKey *key)
{
    uint64_t hash = vkm_hash(key, sizeof(*key), 2);
    pthread_mutex_lock(&g_lock);
    id state = vkm_cache_find(g_depth_states, key, sizeof(*key), hash);
    pthread_mutex_unlock(&g_lock);
    if (state)
        return state;
    void *pool = objc_autoreleasePoolPush();
    id desc = mtl_new("MTLDepthStencilDescriptor");
    M_V_U(desc, "setDepthCompareFunction:", key->test ? vkm_compare(key->compare) : MTLCompareFunctionAlways);
    M_V_B(desc, "setDepthWriteEnabled:", key->test && key->write);
    if (key->stencil) {
        id front = stencil_descriptor(&key->front), back = stencil_descriptor(&key->back);
        M_V_ID(desc, "setFrontFaceStencil:", front);
        M_V_ID(desc, "setBackFaceStencil:", back);
        mtl_release(front);
        mtl_release(back);
    }
    state = M_ID_ID(device->mtl, "newDepthStencilStateWithDescriptor:", desc);
    mtl_release(desc);
    objc_autoreleasePoolPop(pool);
    if (!state)
        return nullptr;
    pthread_mutex_lock(&g_lock);
    id raced = vkm_cache_find(g_depth_states, key, sizeof(*key), hash);
    if (raced) {
        mtl_release(state);
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
    d->depth_clip = 1;
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
        d->depth_clip = clip ? (uint8_t)clip->depthClipEnable : (uint8_t)!rs->depthClampEnable;
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
    for (int i = 0; i < 2; i++)
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
        if (r == VK_SUCCESS && !(p->compute = mtl_retain(vkm_compute_pipeline(device, &p->stages[0]))))
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
    mtl_release(p->compute);
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
