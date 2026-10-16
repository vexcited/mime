/*
 * pipeline states: the shaders, compiled when the state is made, the root
 * signature known, into vulkan shader objects, and the fixed function
 * state the command list sets dynamically when the pipeline state is set.
 */
#include "d3d12_private.h"

static D12PipelineState *impl(ID3D12PipelineState *iface) { return CONTAINING_RECORD(iface, D12PipelineState, iface); }

D12PipelineState *D12PipelineFrom(ID3D12PipelineState *iface) { return iface ? impl(iface) : NULL; }

static void pipeline_destroy(D12PipelineState *state)
{
    D12Device *device = state->child.device;
    if (state->pipeline)
        device->vk.vkDestroyPipeline(device->vk.device, state->pipeline, NULL);
    if (state->root)
        state->root->iface.lpVtbl->Release(&state->root->iface);
}

static HRESULT STDMETHODCALLTYPE pipeline_query(ID3D12PipelineState *iface, REFIID riid, void **out)
{
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D12Object) ||
        IsEqualGUID(riid, &IID_ID3D12DeviceChild) || IsEqualGUID(riid, &IID_ID3D12Pageable) ||
        IsEqualGUID(riid, &IID_ID3D12PipelineState)) {
        iface->lpVtbl->AddRef(iface);
        *out = iface;
        return S_OK;
    }
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE pipeline_add_ref(ID3D12PipelineState *iface)
{
    return InterlockedIncrement(&impl(iface)->child.refs);
}

static ULONG STDMETHODCALLTYPE pipeline_release(ID3D12PipelineState *iface)
{
    D12PipelineState *state = impl(iface);
    ULONG refs = InterlockedDecrement(&state->child.refs);
    if (!refs) {
        pipeline_destroy(state);
        D12ChildRelease(&state->child);
        HeapFree(GetProcessHeap(), 0, state);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE pipeline_get_private_data(ID3D12PipelineState *iface, REFGUID guid, UINT *size,
                                                           void *data)
{
    return MimePrivateGet(&impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE pipeline_set_private_data(ID3D12PipelineState *iface, REFGUID guid, UINT size,
                                                           const void *data)
{
    return MimePrivateSet(&impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE pipeline_set_private_data_interface(ID3D12PipelineState *iface, REFGUID guid,
                                                                     const IUnknown *data)
{
    return MimePrivateSetInterface(&impl(iface)->child.data, guid, data);
}

static HRESULT STDMETHODCALLTYPE pipeline_set_name(ID3D12PipelineState *iface, LPCWSTR name) { return S_OK; }

static HRESULT STDMETHODCALLTYPE pipeline_get_device(ID3D12PipelineState *iface, REFIID riid, void **device)
{
    return D12ChildGetDevice(&impl(iface)->child, riid, device);
}

/* the cached blob names nothing: states given it back compile again, which the shader objects make cheap. */
static HRESULT STDMETHODCALLTYPE pipeline_get_cached_blob(ID3D12PipelineState *iface, ID3DBlob **blob)
{
    if (!blob)
        return E_POINTER;
    static const char tag[] = "mime";
    return D12CreateBlob(tag, sizeof(tag), blob);
}

static const struct ID3D12PipelineStateVtbl g_pipeline_vtbl = {
    pipeline_query,
    pipeline_add_ref,
    pipeline_release,
    pipeline_get_private_data,
    pipeline_set_private_data,
    pipeline_set_private_data_interface,
    pipeline_set_name,
    pipeline_get_device,
    pipeline_get_cached_blob,
};

/* ------------------------------------------------------------------ shaders */

static const DxbcProgram PROGRAMS[D12_STAGES] = {DXBC_VERTEX, DXBC_HULL,  DXBC_DOMAIN,
                                                 DXBC_GEOMETRY, DXBC_PIXEL, DXBC_COMPUTE};

/* the kind of program a container holds, from its dxbc code or its dxil part's header. */
static int program_of(const void *bytecode, SIZE_T length)
{
    uint32_t size;
    const uint32_t *code = dxbc_find_chunk(bytecode, length, DXBC_FOURCC('S', 'H', 'E', 'X'), &size);
    if (!code)
        code = dxbc_find_chunk(bytecode, length, DXBC_FOURCC('S', 'H', 'D', 'R'), &size);
    if (!code)
        code = dxbc_find_chunk(bytecode, length, DXBC_FOURCC('D', 'X', 'I', 'L'), &size);
    return code && size >= 8 ? (int)(code[0] >> 16) : -1;
}

/* compiles a stage against the root signature into a shader module. */
static HRESULT compile_stage(D12Device *device, D12PipelineState *state, D12Stage stage,
                             const D3D12_SHADER_BYTECODE *code, DxbcCompileOptions *options, VkShaderModule *module)
{
    const void *bytecode = code->pShaderBytecode;
    SIZE_T length = code->BytecodeLength;
    if (dxbc_check(bytecode, length))
        return E_INVALIDARG;
    if (program_of(bytecode, length) != (int)PROGRAMS[stage])
        return E_INVALIDARG;
    UINT count;
    DxbcRootBinding *bindings = D12RootBindings(state->root, stage, &count);
    if (!bindings)
        return E_OUTOFMEMORY;
    options->root_signature = 1;
    options->root = bindings;
    options->root_count = count;
    DxbcShaderInfo info;
    int failed = dxil_is(bytecode, length) ? dxil_compile(bytecode, length, options, &info, &g_d12_allocator)
                                           : dxbc_compile(bytecode, length, options, &info, &g_d12_allocator);
    HeapFree(GetProcessHeap(), 0, bindings);
    if (failed) {
        D12Log("d3d12: shader compilation failed: %s", info.error);
        dxbc_free(&info, &g_d12_allocator);
        return E_INVALIDARG;
    }
    VkShaderModuleCreateInfo create = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    create.codeSize = info.spirv_words * 4;
    create.pCode = info.spirv;
    VkResult vr = device->vk.vkCreateShaderModule(device->vk.device, &create, NULL, module);
    dxbc_free(&info, &g_d12_allocator);
    if (vr != VK_SUCCESS)
        return E_OUTOFMEMORY;
    state->stage_mask |= 1u << stage;
    return S_OK;
}

/* the root signature given, or the one a shader carries. */
static HRESULT pick_root(D12Device *device, D12PipelineState *state, ID3D12RootSignature *given,
                         const D3D12_SHADER_BYTECODE *const *codes, UINT count)
{
    if (given) {
        state->root = D12RootSignatureFrom(given);
        given->lpVtbl->AddRef(given);
        return S_OK;
    }
    for (UINT i = 0; i < count; i++) {
        uint32_t size;
        if (codes[i]->pShaderBytecode && codes[i]->BytecodeLength &&
            dxbc_find_chunk(codes[i]->pShaderBytecode, codes[i]->BytecodeLength, DXBC_FOURCC('R', 'T', 'S', '0'),
                            &size))
            return D12CreateRootSignature(device, codes[i]->pShaderBytecode, codes[i]->BytecodeLength, &state->root);
    }
    return E_INVALIDARG;
}

static D12PipelineState *new_pipeline(D12Device *device)
{
    D12PipelineState *state = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*state));
    if (!state)
        return NULL;
    state->iface.lpVtbl = &g_pipeline_vtbl;
    state->child.refs = 1;
    state->child.device = device;
    return state;
}

static HRESULT finish(D12Device *device, D12PipelineState *state, HRESULT hr, D12PipelineState **out)
{
    if (FAILED(hr)) {
        pipeline_destroy(state);
        HeapFree(GetProcessHeap(), 0, state);
        return hr;
    }
    D12ChildInit(&state->child, device);
    *out = state;
    return S_OK;
}

HRESULT D12CreateComputePipeline(D12Device *device, const D3D12_COMPUTE_PIPELINE_STATE_DESC *desc,
                                 D12PipelineState **out)
{
    if (!desc->CS.pShaderBytecode)
        return E_INVALIDARG;
    D12PipelineState *state = new_pipeline(device);
    if (!state)
        return E_OUTOFMEMORY;
    state->compute = TRUE;
    const D3D12_SHADER_BYTECODE *codes[] = {&desc->CS};
    HRESULT hr = pick_root(device, state, desc->pRootSignature, codes, 1);
    if (SUCCEEDED(hr)) {
        DxbcCompileOptions options = {0};
        VkShaderModule module = VK_NULL_HANDLE;
        hr = compile_stage(device, state, D12_CS, &desc->CS, &options, &module);
        if (SUCCEEDED(hr)) {
            VkComputePipelineCreateInfo info = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            info.flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
            info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            info.stage.module = module;
            info.stage.pName = "main";
            info.layout = state->root->layout;
            if (device->vk.vkCreateComputePipelines(device->vk.device, device->pipeline_cache, 1, &info, NULL,
                                                    &state->pipeline) != VK_SUCCESS)
                hr = E_INVALIDARG;
        }
        if (module)
            device->vk.vkDestroyShaderModule(device->vk.device, module, NULL);
    }
    return finish(device, state, hr, out);
}

/* ------------------------------------------------------------------ graphics */

/* the vertex input the input layout makes of the vertex shader's inputs. */
static HRESULT input_layout(D12PipelineState *state, const D3D12_INPUT_LAYOUT_DESC *layout,
                            const D3D12_SHADER_BYTECODE *vs)
{
    if (!layout->NumElements)
        return S_OK;
    if (layout->NumElements > 32 || !layout->pInputElementDescs)
        return E_INVALIDARG;
    DxbcSignature inputs = {0};
    if (dxbc_parse_io(vs->pShaderBytecode, vs->BytecodeLength, 0, &inputs, &g_d12_allocator))
        return E_INVALIDARG;
    UINT offsets[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {0};
    HRESULT hr = S_OK;
    for (UINT i = 0; i < layout->NumElements; i++) {
        const D3D12_INPUT_ELEMENT_DESC *e = &layout->pInputElementDescs[i];
        const D3dFormat *f = D3dGetFormat(e->Format);
        if (e->InputSlot >= D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT || f->vk == VK_FORMAT_UNDEFINED ||
            f->block != 1 || !e->SemanticName) {
            hr = E_INVALIDARG;
            break;
        }
        UINT offset = e->AlignedByteOffset == D3D12_APPEND_ALIGNED_ELEMENT ? offsets[e->InputSlot]
                                                                           : e->AlignedByteOffset;
        offsets[e->InputSlot] = offset + f->block_bytes;
        state->slot_mask |= 1u << e->InputSlot;
        state->instanced[e->InputSlot] = e->InputSlotClass == D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
        state->step_rate[e->InputSlot] =
            e->InputSlotClass == D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA ? e->InstanceDataStepRate : 1;
        const DxbcSignatureElement *input = NULL;
        for (UINT k = 0; k < inputs.count && !input; k++)
            if (!_stricmp(inputs.elements[k].name, e->SemanticName) &&
                inputs.elements[k].semantic_index == e->SemanticIndex)
                input = &inputs.elements[k];
        if (!input)
            continue;
        VkVertexInputAttributeDescription2EXT *a = &state->attributes[state->attribute_count++];
        a->sType = VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT;
        a->location = input->reg;
        a->binding = e->InputSlot;
        a->format = f->vk;
        a->offset = offset;
    }
    g_d12_allocator.free(NULL, inputs.elements);
    return hr;
}

/* the declaration as the compiler takes it, its names copied into the same block. */
static DxbcStreamOutput *stream_output(const D3D12_STREAM_OUTPUT_DESC *so)
{
    DxbcStreamOutput *out = HeapAlloc(GetProcessHeap(), 0, so->NumEntries * sizeof(*out) + 1);
    if (!out)
        return NULL;
    for (UINT i = 0; i < so->NumEntries; i++) {
        const D3D12_SO_DECLARATION_ENTRY *e = &so->pSODeclaration[i];
        out[i] = (DxbcStreamOutput){e->Stream,         e->SemanticName,   e->SemanticIndex,
                                    e->StartComponent, e->ComponentCount, e->OutputSlot};
    }
    return out;
}

/* the gs input primitive a topology type draws, as dcl_inputprimitive numbers them. */
static UINT input_primitive(D3D12_PRIMITIVE_TOPOLOGY_TYPE type)
{
    switch (type) {
    case D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT:
        return 1;
    case D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE:
        return 2;
    default:
        return 3;
    }
}

/* a graphics description in its widest form, what the stream and the plain description both fill. */
typedef struct GraphicsDesc {
    ID3D12RootSignature *root;
    D3D12_SHADER_BYTECODE shaders[D12_STAGES];
    D3D12_STREAM_OUTPUT_DESC so;
    D3D12_BLEND_DESC blend;
    UINT sample_mask;
    D3D12_RASTERIZER_DESC2 rasterizer;
    D3D12_DEPTH_STENCIL_DESC2 depth_stencil;
    D3D12_INPUT_LAYOUT_DESC input;
    D3D12_INDEX_BUFFER_STRIP_CUT_VALUE strip_cut;
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topology;
    D3D12_RT_FORMAT_ARRAY rtv;
    DXGI_FORMAT dsv;
    DXGI_SAMPLE_DESC sample;
    D3D12_PIPELINE_STATE_FLAGS flags;
} GraphicsDesc;

static void default_graphics(GraphicsDesc *g)
{
    memset(g, 0, sizeof(*g));
    g->sample_mask = ~0u;
    g->sample.Count = 1;
    g->rasterizer.FillMode = D3D12_FILL_MODE_SOLID;
    g->rasterizer.CullMode = D3D12_CULL_MODE_BACK;
    g->rasterizer.DepthClipEnable = TRUE;
    g->depth_stencil.DepthEnable = TRUE;
    g->depth_stencil.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    g->depth_stencil.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    D3D12_DEPTH_STENCILOP_DESC1 op = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP,
                                      D3D12_COMPARISON_FUNC_ALWAYS, 0xFF, 0xFF};
    g->depth_stencil.FrontFace = g->depth_stencil.BackFace = op;
    for (UINT i = 0; i < 8; i++) {
        D3D12_RENDER_TARGET_BLEND_DESC *b = &g->blend.RenderTarget[i];
        b->SrcBlend = b->SrcBlendAlpha = D3D12_BLEND_ONE;
        b->DestBlend = b->DestBlendAlpha = D3D12_BLEND_ZERO;
        b->BlendOp = b->BlendOpAlpha = D3D12_BLEND_OP_ADD;
        b->LogicOp = D3D12_LOGIC_OP_NOOP;
        b->RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }
}

static void rasterizer_from(D3D12_RASTERIZER_DESC2 *out, const D3D12_RASTERIZER_DESC *r)
{
    out->FillMode = r->FillMode;
    out->CullMode = r->CullMode;
    out->FrontCounterClockwise = r->FrontCounterClockwise;
    out->DepthBias = (FLOAT)r->DepthBias;
    out->DepthBiasClamp = r->DepthBiasClamp;
    out->SlopeScaledDepthBias = r->SlopeScaledDepthBias;
    out->DepthClipEnable = r->DepthClipEnable;
    out->LineRasterizationMode = r->MultisampleEnable       ? D3D12_LINE_RASTERIZATION_MODE_QUADRILATERAL_WIDE
                                 : r->AntialiasedLineEnable ? D3D12_LINE_RASTERIZATION_MODE_ALPHA_ANTIALIASED
                                                            : D3D12_LINE_RASTERIZATION_MODE_ALIASED;
    out->ForcedSampleCount = r->ForcedSampleCount;
    out->ConservativeRaster = r->ConservativeRaster;
}

static void depth_stencil_from(D3D12_DEPTH_STENCIL_DESC2 *out, const D3D12_DEPTH_STENCIL_DESC *d, BOOL bounds)
{
    out->DepthEnable = d->DepthEnable;
    out->DepthWriteMask = d->DepthWriteMask;
    out->DepthFunc = d->DepthFunc;
    out->StencilEnable = d->StencilEnable;
    out->FrontFace = (D3D12_DEPTH_STENCILOP_DESC1){d->FrontFace.StencilFailOp, d->FrontFace.StencilDepthFailOp,
                                                   d->FrontFace.StencilPassOp, d->FrontFace.StencilFunc,
                                                   d->StencilReadMask, d->StencilWriteMask};
    out->BackFace = (D3D12_DEPTH_STENCILOP_DESC1){d->BackFace.StencilFailOp, d->BackFace.StencilDepthFailOp,
                                                  d->BackFace.StencilPassOp, d->BackFace.StencilFunc,
                                                  d->StencilReadMask, d->StencilWriteMask};
    out->DepthBoundsTestEnable = bounds;
}

static VkBlendFactor blend_factor(D3D12_BLEND b, BOOL alpha)
{
    switch (b) {
    case D3D12_BLEND_ZERO:
        return VK_BLEND_FACTOR_ZERO;
    case D3D12_BLEND_ONE:
        return VK_BLEND_FACTOR_ONE;
    case D3D12_BLEND_SRC_COLOR:
        return VK_BLEND_FACTOR_SRC_COLOR;
    case D3D12_BLEND_INV_SRC_COLOR:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case D3D12_BLEND_SRC_ALPHA:
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case D3D12_BLEND_INV_SRC_ALPHA:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case D3D12_BLEND_DEST_ALPHA:
        return VK_BLEND_FACTOR_DST_ALPHA;
    case D3D12_BLEND_INV_DEST_ALPHA:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case D3D12_BLEND_DEST_COLOR:
        return VK_BLEND_FACTOR_DST_COLOR;
    case D3D12_BLEND_INV_DEST_COLOR:
        return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case D3D12_BLEND_SRC_ALPHA_SAT:
        return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
    case D3D12_BLEND_BLEND_FACTOR:
        return alpha ? VK_BLEND_FACTOR_CONSTANT_ALPHA : VK_BLEND_FACTOR_CONSTANT_COLOR;
    case D3D12_BLEND_INV_BLEND_FACTOR:
        return alpha ? VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA : VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case D3D12_BLEND_SRC1_COLOR:
        return VK_BLEND_FACTOR_SRC1_COLOR;
    case D3D12_BLEND_INV_SRC1_COLOR:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
    case D3D12_BLEND_SRC1_ALPHA:
        return VK_BLEND_FACTOR_SRC1_ALPHA;
    case D3D12_BLEND_INV_SRC1_ALPHA:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
    case D3D12_BLEND_ALPHA_FACTOR:
        return VK_BLEND_FACTOR_CONSTANT_ALPHA;
    case D3D12_BLEND_INV_ALPHA_FACTOR:
        return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    default:
        return VK_BLEND_FACTOR_ONE;
    }
}

static VkStencilOp stencil_op(D3D12_STENCIL_OP op)
{
    static const VkStencilOp OPS[] = {VK_STENCIL_OP_KEEP,
                                      VK_STENCIL_OP_KEEP,
                                      VK_STENCIL_OP_ZERO,
                                      VK_STENCIL_OP_REPLACE,
                                      VK_STENCIL_OP_INCREMENT_AND_CLAMP,
                                      VK_STENCIL_OP_DECREMENT_AND_CLAMP,
                                      VK_STENCIL_OP_INVERT,
                                      VK_STENCIL_OP_INCREMENT_AND_WRAP,
                                      VK_STENCIL_OP_DECREMENT_AND_WRAP};
    return (UINT)op < RTL_NUMBER_OF(OPS) ? OPS[op] : VK_STENCIL_OP_KEEP;
}

static VkCompareOp compare_op(D3D12_COMPARISON_FUNC f)
{
    return f >= D3D12_COMPARISON_FUNC_NEVER && f <= D3D12_COMPARISON_FUNC_ALWAYS ? (VkCompareOp)(f - 1)
                                                                                 : VK_COMPARE_OP_ALWAYS;
}

static VkStencilOpState stencil_state(const D3D12_DEPTH_STENCILOP_DESC1 *d)
{
    return (VkStencilOpState){stencil_op(d->StencilFailOp), stencil_op(d->StencilPassOp),
                              stencil_op(d->StencilDepthFailOp), compare_op(d->StencilFunc), d->StencilReadMask,
                              d->StencilWriteMask, 0};
}

/* the vulkan pipeline of a graphics state: what direct3d 12 fixes in it, the rest dynamic. */
static HRESULT build_graphics(D12Device *device, D12PipelineState *state, const GraphicsDesc *g,
                              const VkShaderModule *modules)
{
    VkPipelineShaderStageCreateInfo stages[5];
    UINT stage_count = 0;
    static const VkShaderStageFlagBits VK_GRAPHICS[] = {
        VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
        VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, VK_SHADER_STAGE_GEOMETRY_BIT, VK_SHADER_STAGE_FRAGMENT_BIT};
    for (UINT i = 0; i < 5; i++)
        if (modules[i])
            stages[stage_count++] = (VkPipelineShaderStageCreateInfo){
                VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_GRAPHICS[i], modules[i], "main"};

    VkVertexInputBindingDescription bindings[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    VkVertexInputBindingDivisorDescriptionKHR divisors[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
    VkVertexInputAttributeDescription attributes[32];
    UINT binding_count = 0, divisor_count = 0;
    for (UINT i = 0; i < D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT; i++) {
        if (!(state->slot_mask & (1u << i)))
            continue;
        bindings[binding_count++] = (VkVertexInputBindingDescription){
            i, 0, state->instanced[i] ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX};
        UINT step = state->step_rate[i];
        if (state->instanced[i] && step != 1 &&
            ((step > 1 && device->has_divisor) || (!step && device->has_zero_divisor)))
            divisors[divisor_count++] = (VkVertexInputBindingDivisorDescriptionKHR){i, step};
    }
    for (UINT i = 0; i < state->attribute_count; i++)
        attributes[i] = (VkVertexInputAttributeDescription){state->attributes[i].location, state->attributes[i].binding,
                                                            state->attributes[i].format, state->attributes[i].offset};
    VkPipelineVertexInputDivisorStateCreateInfoKHR divisor_info = {
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_KHR};
    divisor_info.vertexBindingDivisorCount = divisor_count;
    divisor_info.pVertexBindingDivisors = divisors;
    VkPipelineVertexInputStateCreateInfo vertex = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
                                                   divisor_count ? &divisor_info : NULL};
    vertex.vertexBindingDescriptionCount = binding_count;
    vertex.pVertexBindingDescriptions = bindings;
    vertex.vertexAttributeDescriptionCount = state->attribute_count;
    vertex.pVertexAttributeDescriptions = attributes;

    static const VkPrimitiveTopology CLASSES[] = {
        [D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED] = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        [D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT] = VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
        [D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE] = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
        [D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE] = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        [D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH] = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST,
    };
    VkPipelineInputAssemblyStateCreateInfo assembly = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = CLASSES[g->topology <= D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH ? g->topology : 0];
    if (modules[D12_HS])
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    VkPipelineTessellationStateCreateInfo tessellation = {VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO};
    tessellation.patchControlPoints = 3;
    VkPipelineViewportStateCreateInfo viewport = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};

    const D3D12_RASTERIZER_DESC2 *rs = &g->rasterizer;
    static const VkCullModeFlags CULL[] = {VK_CULL_MODE_NONE, VK_CULL_MODE_NONE, VK_CULL_MODE_FRONT_BIT,
                                           VK_CULL_MODE_BACK_BIT};
    VkPipelineRasterizationDepthClipStateCreateInfoEXT clip = {
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT};
    clip.depthClipEnable = rs->DepthClipEnable;
    VkPipelineRasterizationStateStreamCreateInfoEXT stream = {
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_STREAM_CREATE_INFO_EXT};
    UINT rasterized = state->stream_output ? state->rasterized_stream : 0;
    stream.rasterizationStream = rasterized < 4 ? rasterized : 0;
    VkPipelineRasterizationStateCreateInfo raster = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    /* direct3d clamps depth to the viewport always; turning clipping off leaves only the clamp. */
    if (device->has_depth_clip) {
        raster.depthClampEnable = TRUE;
        clip.pNext = raster.pNext;
        raster.pNext = &clip;
    } else {
        raster.depthClampEnable = !rs->DepthClipEnable;
    }
    if (device->has_transform_feedback && state->stream_output) {
        stream.pNext = (void *)raster.pNext;
        raster.pNext = &stream;
    }
    raster.rasterizerDiscardEnable = state->stream_output && rasterized == D3D12_SO_NO_RASTERIZED_STREAM;
    raster.polygonMode = rs->FillMode == D3D12_FILL_MODE_WIREFRAME ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
    raster.cullMode = CULL[rs->CullMode & 3];
    raster.frontFace = rs->FrontCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    UINT samples = state->samples;
    if (!state->rt_count && g->dsv == DXGI_FORMAT_UNKNOWN && rs->ForcedSampleCount)
        samples = rs->ForcedSampleCount;
    multisample.rasterizationSamples = (VkSampleCountFlagBits)samples;
    VkSampleMask mask = g->sample_mask;
    multisample.pSampleMask = &mask;
    multisample.alphaToCoverageEnable = g->blend.AlphaToCoverageEnable;

    const D3D12_DEPTH_STENCIL_DESC2 *ds = &g->depth_stencil;
    VkPipelineDepthStencilStateCreateInfo depth = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthCompareOp = compare_op(ds->DepthFunc);
    depth.depthBoundsTestEnable = state->depth_bounds;
    depth.front = stencil_state(&ds->FrontFace);
    depth.back = stencil_state(&ds->BackFace);
    depth.maxDepthBounds = 1.0f;

    VkPipelineColorBlendAttachmentState blends[8];
    for (UINT i = 0; i < state->rt_count; i++) {
        const D3D12_RENDER_TARGET_BLEND_DESC *t = &g->blend.RenderTarget[g->blend.IndependentBlendEnable ? i : 0];
        blends[i] = (VkPipelineColorBlendAttachmentState){
            t->BlendEnable,
            blend_factor(t->SrcBlend, FALSE),
            blend_factor(t->DestBlend, FALSE),
            t->BlendOp ? (VkBlendOp)(t->BlendOp - 1) : VK_BLEND_OP_ADD,
            blend_factor(t->SrcBlendAlpha, TRUE),
            blend_factor(t->DestBlendAlpha, TRUE),
            t->BlendOpAlpha ? (VkBlendOp)(t->BlendOpAlpha - 1) : VK_BLEND_OP_ADD,
            t->RenderTargetWriteMask & 0xF};
    }
    VkPipelineColorBlendStateCreateInfo blend = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = state->rt_count;
    blend.pAttachments = blends;
    if (g->blend.RenderTarget[0].LogicOpEnable && device->has_logic_op) {
        static const VkLogicOp LOGIC[] = {
            VK_LOGIC_OP_CLEAR,       VK_LOGIC_OP_SET,          VK_LOGIC_OP_COPY,       VK_LOGIC_OP_COPY_INVERTED,
            VK_LOGIC_OP_NO_OP,       VK_LOGIC_OP_INVERT,       VK_LOGIC_OP_AND,        VK_LOGIC_OP_NAND,
            VK_LOGIC_OP_OR,          VK_LOGIC_OP_NOR,          VK_LOGIC_OP_XOR,        VK_LOGIC_OP_EQUIVALENT,
            VK_LOGIC_OP_AND_REVERSE, VK_LOGIC_OP_AND_INVERTED, VK_LOGIC_OP_OR_REVERSE, VK_LOGIC_OP_OR_INVERTED};
        blend.logicOpEnable = TRUE;
        blend.logicOp = LOGIC[g->blend.RenderTarget[0].LogicOp & 15];
    }

    VkDynamicState dynamic[] = {
        VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT,     VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT,
        VK_DYNAMIC_STATE_BLEND_CONSTANTS,         VK_DYNAMIC_STATE_STENCIL_REFERENCE,
        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,      VK_DYNAMIC_STATE_DEPTH_BIAS,
        VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,       VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
        VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,      VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE,
        VK_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY,      VK_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE,
        VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE, VK_DYNAMIC_STATE_DEPTH_BOUNDS,
        VK_DYNAMIC_STATE_PATCH_CONTROL_POINTS_EXT,
    };
    VkPipelineDynamicStateCreateInfo dynamic_info = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic_info.dynamicStateCount = RTL_NUMBER_OF(dynamic) - (device->has_patch_points ? 0 : 1);
    dynamic_info.pDynamicStates = dynamic;
    if (!state->depth_bounds) {
        dynamic[13] = dynamic[14];
        dynamic_info.dynamicStateCount--;
    }

    VkFormat colors[8];
    for (UINT i = 0; i < state->rt_count; i++)
        colors[i] = D3dGetFormat(g->rtv.RTFormats[i])->vk;
    VkPipelineRenderingCreateInfo rendering = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = state->rt_count;
    rendering.pColorAttachmentFormats = colors;
    if (g->dsv != DXGI_FORMAT_UNKNOWN) {
        const D3dFormat *f = D3dGetFormat(g->dsv);
        VkFormat format = f->depth ? f->depth : f->vk;
        if (format != VK_FORMAT_S8_UINT)
            rendering.depthAttachmentFormat = format;
        if (format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_S8_UINT)
            rendering.stencilAttachmentFormat = format;
    }
    state->depth_format = rendering.depthAttachmentFormat;
    state->stencil_format = rendering.stencilAttachmentFormat;

    VkGraphicsPipelineCreateInfo info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &rendering};
    info.flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    info.stageCount = stage_count;
    info.pStages = stages;
    info.pVertexInputState = &vertex;
    info.pInputAssemblyState = &assembly;
    info.pTessellationState = modules[D12_HS] ? &tessellation : NULL;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic_info;
    info.layout = state->root->layout;
    if (device->vk.vkCreateGraphicsPipelines(device->vk.device, device->pipeline_cache, 1, &info, NULL,
                                             &state->pipeline) != VK_SUCCESS)
        return E_INVALIDARG;
    return S_OK;
}

static HRESULT create_graphics(D12Device *device, const GraphicsDesc *g, D12PipelineState **out)
{
    if (!g->shaders[D12_VS].pShaderBytecode)
        return E_INVALIDARG;
    if ((g->shaders[D12_HS].pShaderBytecode || g->shaders[D12_DS].pShaderBytecode) && !device->has_tessellation)
        return E_INVALIDARG;
    D12PipelineState *state = new_pipeline(device);
    if (!state)
        return E_OUTOFMEMORY;
    const D3D12_SHADER_BYTECODE *codes[D12_STAGES];
    for (UINT i = 0; i < D12_STAGES; i++)
        codes[i] = &g->shaders[i];
    HRESULT hr = pick_root(device, state, g->root, codes, D12_CS);
    state->blend = g->blend;
    state->sample_mask = g->sample_mask;
    state->rasterizer = g->rasterizer;
    state->depth_stencil = g->depth_stencil;
    state->strip_cut = g->strip_cut;
    state->topology_type = g->topology;
    state->rt_count = D12_MIN(g->rtv.NumRenderTargets, 8u);
    memcpy(state->rtv_formats, g->rtv.RTFormats, sizeof(state->rtv_formats));
    state->dsv_format = g->dsv;
    state->samples = g->sample.Count ? g->sample.Count : 1;
    state->depth_bounds = g->depth_stencil.DepthBoundsTestEnable && device->has_depth_bounds;
    if (SUCCEEDED(hr))
        hr = input_layout(state, &g->input, &g->shaders[D12_VS]);

    /* stream output goes from the geometry shader, or one passing through the last stage before it. */
    DxbcStreamOutput *so = NULL;
    UINT strides[4] = {0};
    BOOL with_so = g->so.NumEntries && g->so.pSODeclaration;
    if (SUCCEEDED(hr) && with_so) {
        if (!device->has_transform_feedback || !(so = stream_output(&g->so)))
            hr = E_INVALIDARG;
        for (UINT i = 0; i < D12_MIN(g->so.NumStrides, 4u) && g->so.pBufferStrides; i++)
            strides[i] = g->so.pBufferStrides[i];
        state->stream_output = TRUE;
        memcpy(state->so_strides, strides, sizeof(strides));
        state->rasterized_stream = g->so.RasterizedStream;
    }
    VkShaderModule modules[D12_STAGES] = {0};
    for (UINT stage = D12_VS; stage <= D12_PS && SUCCEEDED(hr); stage++) {
        if (!g->shaders[stage].pShaderBytecode)
            continue;
        DxbcCompileOptions options = {0};
        options.sample_count = state->samples;
        if (with_so && stage == D12_GS) {
            options.so = so;
            options.so_count = g->so.NumEntries;
            options.so_strides = strides;
            options.so_stride_count = 4;
            options.rasterized_stream = g->so.RasterizedStream;
        }
        hr = compile_stage(device, state, (D12Stage)stage, &g->shaders[stage], &options, &modules[stage]);
    }
    if (SUCCEEDED(hr) && with_so && !g->shaders[D12_GS].pShaderBytecode) {
        const D3D12_SHADER_BYTECODE *last = g->shaders[D12_DS].pShaderBytecode ? &g->shaders[D12_DS] : &g->shaders[D12_VS];
        SIZE_T length;
        void *gs = dxbc_passthrough_gs(last->pShaderBytecode, last->BytecodeLength, input_primitive(g->topology),
                                       &g_d12_allocator, &length);
        if (!gs) {
            hr = E_INVALIDARG;
        } else {
            D3D12_SHADER_BYTECODE code = {gs, length};
            DxbcCompileOptions options = {0};
            options.so = so;
            options.so_count = g->so.NumEntries;
            options.so_strides = strides;
            options.so_stride_count = 4;
            options.rasterized_stream = g->so.RasterizedStream;
            hr = compile_stage(device, state, D12_GS, &code, &options, &modules[D12_GS]);
            g_d12_allocator.free(NULL, gs);
        }
    }
    HeapFree(GetProcessHeap(), 0, so);
    if (SUCCEEDED(hr))
        hr = build_graphics(device, state, g, modules);
    for (UINT i = 0; i < D12_STAGES; i++)
        if (modules[i])
            device->vk.vkDestroyShaderModule(device->vk.device, modules[i], NULL);
    return finish(device, state, hr, out);
}

HRESULT D12CreateGraphicsPipeline(D12Device *device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC *desc,
                                  D12PipelineState **out)
{
    GraphicsDesc g;
    default_graphics(&g);
    g.root = desc->pRootSignature;
    g.shaders[D12_VS] = desc->VS;
    g.shaders[D12_HS] = desc->HS;
    g.shaders[D12_DS] = desc->DS;
    g.shaders[D12_GS] = desc->GS;
    g.shaders[D12_PS] = desc->PS;
    g.so = desc->StreamOutput;
    g.blend = desc->BlendState;
    g.sample_mask = desc->SampleMask;
    rasterizer_from(&g.rasterizer, &desc->RasterizerState);
    depth_stencil_from(&g.depth_stencil, &desc->DepthStencilState, FALSE);
    g.input = desc->InputLayout;
    g.strip_cut = desc->IBStripCutValue;
    g.topology = desc->PrimitiveTopologyType;
    g.rtv.NumRenderTargets = desc->NumRenderTargets;
    memcpy(g.rtv.RTFormats, desc->RTVFormats, sizeof(g.rtv.RTFormats));
    g.dsv = desc->DSVFormat;
    g.sample = desc->SampleDesc;
    g.flags = desc->Flags;
    return create_graphics(device, &g, out);
}

/* ------------------------------------------------------------------ pipeline state streams */

/* each subobject is its type then its structure, at the structure's alignment, padded to a pointer's. */
#define SUBOBJECT(type) {sizeof(type), _Alignof(type)}
static const struct {
    UINT size, align;
} g_subobjects[] = {
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE] = SUBOBJECT(ID3D12RootSignature *),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS] = SUBOBJECT(D3D12_SHADER_BYTECODE),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS] = SUBOBJECT(D3D12_SHADER_BYTECODE),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS] = SUBOBJECT(D3D12_SHADER_BYTECODE),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS] = SUBOBJECT(D3D12_SHADER_BYTECODE),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS] = SUBOBJECT(D3D12_SHADER_BYTECODE),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS] = SUBOBJECT(D3D12_SHADER_BYTECODE),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT] = SUBOBJECT(D3D12_STREAM_OUTPUT_DESC),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND] = SUBOBJECT(D3D12_BLEND_DESC),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK] = SUBOBJECT(UINT),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER] = SUBOBJECT(D3D12_RASTERIZER_DESC),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL] = SUBOBJECT(D3D12_DEPTH_STENCIL_DESC),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT] = SUBOBJECT(D3D12_INPUT_LAYOUT_DESC),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE] = SUBOBJECT(D3D12_INDEX_BUFFER_STRIP_CUT_VALUE),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY] = SUBOBJECT(D3D12_PRIMITIVE_TOPOLOGY_TYPE),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS] = SUBOBJECT(D3D12_RT_FORMAT_ARRAY),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT] = SUBOBJECT(DXGI_FORMAT),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC] = SUBOBJECT(DXGI_SAMPLE_DESC),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_NODE_MASK] = SUBOBJECT(UINT),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CACHED_PSO] = SUBOBJECT(D3D12_CACHED_PIPELINE_STATE),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS] = SUBOBJECT(D3D12_PIPELINE_STATE_FLAGS),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1] = SUBOBJECT(D3D12_DEPTH_STENCIL_DESC1),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VIEW_INSTANCING] = SUBOBJECT(D3D12_VIEW_INSTANCING_DESC),
    [23] = {0, 0},
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS] = SUBOBJECT(D3D12_SHADER_BYTECODE),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS] = SUBOBJECT(D3D12_SHADER_BYTECODE),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2] = SUBOBJECT(D3D12_DEPTH_STENCIL_DESC2),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER1] = SUBOBJECT(D3D12_RASTERIZER_DESC1),
    [D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER2] = SUBOBJECT(D3D12_RASTERIZER_DESC2),
};

HRESULT D12CreatePipelineFromStream(D12Device *device, const D3D12_PIPELINE_STATE_STREAM_DESC *desc,
                                    D12PipelineState **out)
{
    GraphicsDesc g;
    default_graphics(&g);
    D3D12_SHADER_BYTECODE cs = {0};
    const BYTE *p = desc->pPipelineStateSubobjectStream, *end = p + desc->SizeInBytes;
    UINT seen = 0;
    while (p + sizeof(UINT) <= end) {
        UINT type;
        memcpy(&type, p, sizeof(type));
        if (type >= RTL_NUMBER_OF(g_subobjects) || !g_subobjects[type].size || (seen & (1u << type)))
            return E_INVALIDARG;
        seen |= 1u << type;
        const BYTE *data = p + D12_ALIGN(sizeof(UINT), g_subobjects[type].align);
        if (data + g_subobjects[type].size > end)
            return E_INVALIDARG;
        switch (type) {
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_ROOT_SIGNATURE:
            memcpy(&g.root, data, sizeof(g.root));
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_VS:
            memcpy(&g.shaders[D12_VS], data, sizeof(D3D12_SHADER_BYTECODE));
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PS:
            memcpy(&g.shaders[D12_PS], data, sizeof(D3D12_SHADER_BYTECODE));
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DS:
            memcpy(&g.shaders[D12_DS], data, sizeof(D3D12_SHADER_BYTECODE));
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_HS:
            memcpy(&g.shaders[D12_HS], data, sizeof(D3D12_SHADER_BYTECODE));
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_GS:
            memcpy(&g.shaders[D12_GS], data, sizeof(D3D12_SHADER_BYTECODE));
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_CS:
            memcpy(&cs, data, sizeof(cs));
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_STREAM_OUTPUT:
            memcpy(&g.so, data, sizeof(g.so));
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_BLEND:
            memcpy(&g.blend, data, sizeof(g.blend));
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_MASK:
            memcpy(&g.sample_mask, data, sizeof(UINT));
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER: {
            D3D12_RASTERIZER_DESC r;
            memcpy(&r, data, sizeof(r));
            rasterizer_from(&g.rasterizer, &r);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER1: {
            D3D12_RASTERIZER_DESC1 r;
            memcpy(&r, data, sizeof(r));
            D3D12_RASTERIZER_DESC r0;
            memcpy(&r0, &r, sizeof(r0));
            rasterizer_from(&g.rasterizer, &r0);
            g.rasterizer.DepthBias = r.DepthBias;
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RASTERIZER2:
            memcpy(&g.rasterizer, data, sizeof(g.rasterizer));
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL: {
            D3D12_DEPTH_STENCIL_DESC d;
            memcpy(&d, data, sizeof(d));
            depth_stencil_from(&g.depth_stencil, &d, FALSE);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL1: {
            D3D12_DEPTH_STENCIL_DESC1 d;
            memcpy(&d, data, sizeof(d));
            depth_stencil_from(&g.depth_stencil, (const D3D12_DEPTH_STENCIL_DESC *)&d, d.DepthBoundsTestEnable);
            break;
        }
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL2:
            memcpy(&g.depth_stencil, data, sizeof(g.depth_stencil));
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_INPUT_LAYOUT:
            memcpy(&g.input, data, sizeof(g.input));
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_IB_STRIP_CUT_VALUE:
            memcpy(&g.strip_cut, data, sizeof(UINT));
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_PRIMITIVE_TOPOLOGY:
            memcpy(&g.topology, data, sizeof(UINT));
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_RENDER_TARGET_FORMATS:
            memcpy(&g.rtv, data, sizeof(g.rtv));
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_DEPTH_STENCIL_FORMAT:
            memcpy(&g.dsv, data, sizeof(UINT));
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_SAMPLE_DESC:
            memcpy(&g.sample, data, sizeof(g.sample));
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_FLAGS:
            memcpy(&g.flags, data, sizeof(UINT));
            break;
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_AS:
        case D3D12_PIPELINE_STATE_SUBOBJECT_TYPE_MS: {
            D3D12_SHADER_BYTECODE code;
            memcpy(&code, data, sizeof(code));
            /* mesh shaders are not supported, as OPTIONS7 reports. */
            if (code.pShaderBytecode)
                return E_INVALIDARG;
            break;
        }
        default:
            break;
        }
        p = data + g_subobjects[type].size;
        p = (const BYTE *)desc->pPipelineStateSubobjectStream +
            D12_ALIGN(p - (const BYTE *)desc->pPipelineStateSubobjectStream, sizeof(void *));
    }
    if (cs.pShaderBytecode) {
        if (g.shaders[D12_VS].pShaderBytecode)
            return E_INVALIDARG;
        D3D12_COMPUTE_PIPELINE_STATE_DESC compute = {g.root, cs};
        return D12CreateComputePipeline(device, &compute, out);
    }
    return create_graphics(device, &g, out);
}
