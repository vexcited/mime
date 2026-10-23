/*
 * shaders: dxbc compiled to spir-v as vulkan shader objects, so any combination can be bound at draw time like in
 * direct3d. all stages share one pipeline layout: the heap and the binding tables.
 */
#include "d3d11_private.h"

#define VTBL_FN(f) ((void *)(f))

extern const MimeAllocator g_d3d_allocator;

static D3dShader *impl(void *iface) { return CONTAINING_RECORD(iface, D3dShader, iface); }

D3dShader *D3dShaderFrom(void *iface, D3dStage stage)
{
    if (!iface)
        return nullptr;
    D3dShader *shader = impl(iface);
    return shader->stage == stage ? shader : nullptr;
}

static const GUID *const STAGE_IIDS[D3D_STAGES] = {&IID_ID3D11VertexShader,   &IID_ID3D11HullShader,
                                                   &IID_ID3D11DomainShader,   &IID_ID3D11GeometryShader,
                                                   &IID_ID3D11PixelShader,    &IID_ID3D11ComputeShader};

static HRESULT STDMETHODCALLTYPE shader_query(void *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    D3dShader *shader = impl(iface);
    BOOL match = IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D11DeviceChild) ||
                 IsEqualGUID(riid, STAGE_IIDS[shader->stage]);
    void *d3d10 = match ? nullptr : D3d10ShaderInterface(shader, riid);
    if (!match && !d3d10) {
        *out = nullptr;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&shader->child.refs);
    *out = d3d10 ? d3d10 : iface;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE shader_add_ref(void *iface) { return InterlockedIncrement(&impl(iface)->child.refs); }

static void shader_destroy(D3dShader *shader)
{
    D3dDevice *device = shader->child.device;
    D3dDefer(device, device->next_serial, D3D_GARBAGE_SHADER, (UINT64)shader->shader);
    for (UINT i = 0; i < RTL_NUMBER_OF(shader->so_variants); i++)
        if (shader->so_variants[i] && shader->so_variants[i] != shader->shader)
            D3dDefer(device, device->next_serial, D3D_GARBAGE_SHADER, (UINT64)shader->so_variants[i]);
    HeapFree(GetProcessHeap(), 0, shader->so_source);
    HeapFree(GetProcessHeap(), 0, shader->so_entries);
    dxbc_free(&shader->info, &g_d3d_allocator);
    D3dChildRelease(&shader->child);
    HeapFree(GetProcessHeap(), 0, shader);
}

static ULONG STDMETHODCALLTYPE shader_release(void *iface)
{
    D3dShader *shader = impl(iface);
    ULONG refs = InterlockedDecrement(&shader->child.refs);
    if (!refs)
        shader_destroy(shader);
    return refs;
}

static void STDMETHODCALLTYPE shader_get_device(void *iface, ID3D11Device **device)
{
    D3dDevice *d = impl(iface)->child.device;
    d->iface.lpVtbl->AddRef(&d->iface);
    *device = (ID3D11Device *)&d->iface;
}

static HRESULT STDMETHODCALLTYPE shader_get_private_data(void *iface, REFGUID guid, UINT *size, void *data)
{
    return MimePrivateGet(&impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE shader_set_private_data(void *iface, REFGUID guid, UINT size, const void *data)
{
    return MimePrivateSet(&impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE shader_set_private_data_interface(void *iface, REFGUID guid, const IUnknown *data)
{
    return MimePrivateSetInterface(&impl(iface)->child.data, guid, data);
}

/* all stages share the device child vtable. */
static const ID3D11VertexShaderVtbl shader_vtbl = {
    VTBL_FN(shader_query),           VTBL_FN(shader_add_ref),         VTBL_FN(shader_release),
    VTBL_FN(shader_get_device),      VTBL_FN(shader_get_private_data), VTBL_FN(shader_set_private_data),
    VTBL_FN(shader_set_private_data_interface),
};

static const DxbcProgram PROGRAMS[D3D_STAGES] = {DXBC_VERTEX, DXBC_HULL,  DXBC_DOMAIN,
                                                 DXBC_GEOMETRY, DXBC_PIXEL, DXBC_COMPUTE};
static const VkShaderStageFlagBits VK_STAGES[D3D_STAGES] = {
    VK_SHADER_STAGE_VERTEX_BIT,   VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
    VK_SHADER_STAGE_GEOMETRY_BIT, VK_SHADER_STAGE_FRAGMENT_BIT,             VK_SHADER_STAGE_COMPUTE_BIT,
};

static VkShaderStageFlags next_stages(D3dDevice *device, D3dStage stage)
{
    VkShaderStageFlags gs = device->has_geometry ? VK_SHADER_STAGE_GEOMETRY_BIT : 0;
    switch (stage) {
    case D3D_VS:
        return (device->has_tessellation ? VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT : 0) | gs |
               VK_SHADER_STAGE_FRAGMENT_BIT;
    case D3D_HS:
        return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    case D3D_DS:
        return gs | VK_SHADER_STAGE_FRAGMENT_BIT;
    case D3D_GS:
        return VK_SHADER_STAGE_FRAGMENT_BIT;
    default:
        return 0;
    }
}

/* the program type from the code chunk's version token. */
static int program_of(const void *bytecode, SIZE_T length)
{
    uint32_t size;
    const uint32_t *code = dxbc_find_chunk(bytecode, length, DXBC_FOURCC('S', 'H', 'E', 'X'), &size);
    if (!code)
        code = dxbc_find_chunk(bytecode, length, DXBC_FOURCC('S', 'H', 'D', 'R'), &size);
    return code && size >= 8 ? (int)(code[0] >> 16) : -1;
}

static BOOL has_trailing_digit(const char *name)
{
    SIZE_T n = strlen(name);
    return n && name[n - 1] >= '0' && name[n - 1] <= '9';
}

/*
 * the declaration checks windows does at feature level 11_0 and later: entries in stream order, at most 128 per
 * stream, each buffer used by one stream, gaps without semantic index or start component, components that fit in
 * four, names without a trailing digit, no entry twice, a stride for every buffer written, and each entry an output
 * of its stream's signature with the components it takes. without strides, each buffer's stride is the size of
 * its entries.
 */
static BOOL check_stream_output(const void *bytecode, SIZE_T length, const D3D11_SO_DECLARATION_ENTRY *so,
                                UINT count, const UINT *strides, UINT stride_count, UINT out_strides[4])
{
    if ((count && !so) || (stride_count && !strides) || (!stride_count && strides) || stride_count > 4)
        return FALSE;
    DxbcSignature sig;
    if (dxbc_parse_io(bytecode, length, 1, &sig, &g_d3d_allocator))
        return FALSE;
    BOOL ok = TRUE;
    UINT slot_stream[4] = {~0u, ~0u, ~0u, ~0u}, per_stream[4] = {}, sizes[4] = {}, used = 0;
    for (UINT i = 0; i < count && ok; i++) {
        const D3D11_SO_DECLARATION_ENTRY *e = &so[i];
        if (e->Stream >= 4 || (i && e->Stream < so[i - 1].Stream) || e->OutputSlot >= 4 || ++per_stream[e->Stream] > 128 ||
            (slot_stream[e->OutputSlot] != ~0u && slot_stream[e->OutputSlot] != e->Stream) || !e->ComponentCount) {
            ok = FALSE;
            break;
        }
        slot_stream[e->OutputSlot] = e->Stream;
        used |= 1u << e->OutputSlot;
        sizes[e->OutputSlot] += e->ComponentCount * 4;
        if (!e->SemanticName) {
            ok = !e->SemanticIndex && !e->StartComponent;
            continue;
        }
        BYTE mask = (BYTE)(((1u << e->ComponentCount) - 1) << e->StartComponent);
        if (e->StartComponent >= 4 || e->StartComponent + e->ComponentCount > 4 || has_trailing_digit(e->SemanticName)) {
            ok = FALSE;
            break;
        }
        for (UINT k = 0; k < i && ok; k++)
            if (so[k].SemanticName && so[k].Stream == e->Stream && so[k].SemanticIndex == e->SemanticIndex &&
                !_stricmp(so[k].SemanticName, e->SemanticName) &&
                (((1u << so[k].ComponentCount) - 1) << so[k].StartComponent & mask))
                ok = FALSE;
        /* the mask is relative to the element's first component. */
        const DxbcSignatureElement *found = nullptr;
        for (UINT k = 0; k < sig.count && !found; k++)
            if (sig.elements[k].stream == e->Stream && sig.elements[k].semantic_index == e->SemanticIndex &&
                !_stricmp(sig.elements[k].name, e->SemanticName))
                found = &sig.elements[k];
        if (!found || !found->mask || (((UINT)mask << __builtin_ctz(found->mask)) & ~found->mask))
            ok = FALSE;
    }
    if (ok && stride_count && (used >> stride_count))
        ok = FALSE;
    for (UINT i = 0; i < 4; i++)
        out_strides[i] = i < stride_count ? strides[i] : stride_count ? 0 : sizes[i];
    g_d3d_allocator.free(nullptr, sig.elements);
    return ok;
}

static VkShaderEXT make_shader(D3dDevice *device, D3dStage stage, const DxbcShaderInfo *info)
{
    VkDescriptorSetLayout sets[2] = {device->heap_layout, device->table_layout};
    VkShaderCreateInfoEXT create = {VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT};
    create.stage = VK_STAGES[stage];
    create.nextStage = next_stages(device, stage);
    create.codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;
    create.codeSize = info->spirv_words * 4;
    create.pCode = info->spirv;
    create.pName = "main";
    create.setLayoutCount = 2;
    create.pSetLayouts = sets;
    VkShaderEXT shader = VK_NULL_HANDLE;
    if (device->vk.vkCreateShadersEXT(device->vk.device, 1, &create, nullptr, &shader) != VK_SUCCESS) {
        D3dLog("d3d11: vkCreateShadersEXT failed for a stage %u shader", stage);
        return VK_NULL_HANDLE;
    }
    return shader;
}

static DxbcCompileOptions so_options(const D3dShader *shader)
{
    DxbcCompileOptions options = {};
    options.so = shader->so_entries;
    options.so_count = shader->so_count;
    options.so_strides = shader->so_strides;
    options.so_stride_count = 4;
    options.rasterized_stream = shader->rasterized_stream;
    return options;
}

/*
 * picks the stream output geometry shader for a dcl_inputprimitive kind. one made from a vertex or domain shader
 * gets a pass-through variant per primitive, compiled on first draw.
 */
VkShaderEXT D3dShaderVariant(D3dShader *shader, UINT primitive)
{
    if (!shader->so_source || primitive >= RTL_NUMBER_OF(shader->so_variants))
        return shader->shader;
    if (shader->so_variants[primitive])
        return shader->so_variants[primitive];
    SIZE_T length;
    void *gs = dxbc_passthrough_gs(shader->so_source, shader->so_source_length, primitive, &g_d3d_allocator, &length);
    if (!gs)
        return shader->shader;
    DxbcCompileOptions options = so_options(shader);
    DxbcShaderInfo info;
    VkShaderEXT variant = VK_NULL_HANDLE;
    if (!dxbc_compile(gs, length, &options, &info, &g_d3d_allocator))
        variant = make_shader(shader->child.device, D3D_GS, &info);
    dxbc_free(&info, &g_d3d_allocator);
    g_d3d_allocator.free(nullptr, gs);
    if (!variant)
        return shader->shader;
    /* if two threads raced, keep the first variant. handles are 64 bits wide everywhere. */
    LONG64 value, old;
    memcpy(&value, &variant, sizeof(value));
    old = InterlockedCompareExchange64((LONG64 *)&shader->so_variants[primitive], value, 0);
    if (old) {
        D3dDefer(shader->child.device, 0, D3D_GARBAGE_SHADER, (UINT64)value);
        memcpy(&variant, &old, sizeof(variant));
    }
    return variant;
}

/* copies the declaration so its names outlive the program's. */
static BOOL keep_declaration(D3dShader *shader, const D3D11_SO_DECLARATION_ENTRY *so, UINT count)
{
    SIZE_T names = 0;
    for (UINT i = 0; i < count; i++)
        names += so[i].SemanticName ? strlen(so[i].SemanticName) + 1 : 0;
    shader->so_entries = HeapAlloc(GetProcessHeap(), 0, count * sizeof(DxbcStreamOutput) + names + 1);
    if (!shader->so_entries)
        return FALSE;
    char *text = (char *)(shader->so_entries + count);
    for (UINT i = 0; i < count; i++) {
        const char *name = nullptr;
        if (so[i].SemanticName) {
            SIZE_T n = strlen(so[i].SemanticName) + 1;
            memcpy(text, so[i].SemanticName, n);
            name = text;
            text += n;
        }
        shader->so_entries[i] = (DxbcStreamOutput){so[i].Stream,         name,   so[i].SemanticIndex,
                                                   so[i].StartComponent, so[i].ComponentCount, so[i].OutputSlot};
    }
    shader->so_count = count;
    return TRUE;
}

static void free_shader(D3dShader *shader)
{
    dxbc_free(&shader->info, &g_d3d_allocator);
    HeapFree(GetProcessHeap(), 0, shader->so_source);
    HeapFree(GetProcessHeap(), 0, shader->so_entries);
    HeapFree(GetProcessHeap(), 0, shader);
}

HRESULT D3dCreateShader(D3dDevice *device, D3dStage stage, const void *bytecode, SIZE_T length,
                        const D3D11_SO_DECLARATION_ENTRY *so, UINT so_count, const UINT *strides,
                        UINT stride_count, UINT rasterized_stream, D3dShader **out)
{
    if (dxbc_check(bytecode, length))
        return E_INVALIDARG;
    if ((stage == D3D_HS || stage == D3D_DS) && !device->has_tessellation)
        return E_INVALIDARG;
    int program = program_of(bytecode, length);
    BOOL with_so = stage == D3D_GS && (so_count || so || strides);
    if (with_so) {
        if (program != DXBC_GEOMETRY && (!so_count || (program != DXBC_VERTEX && program != DXBC_DOMAIN)))
            return E_INVALIDARG;
        if (rasterized_stream != D3D11_SO_NO_RASTERIZED_STREAM && rasterized_stream >= 4)
            return E_INVALIDARG;
    } else if (program != (int)PROGRAMS[stage]) {
        return E_INVALIDARG;
    }
    D3dShader *shader = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*shader));
    if (!shader)
        return E_OUTOFMEMORY;
    DxbcCompileOptions options = {};
    if (with_so) {
        if (!check_stream_output(bytecode, length, so, so_count, strides, stride_count, shader->so_strides) ||
            !keep_declaration(shader, so, so_count)) {
            free_shader(shader);
            return E_INVALIDARG;
        }
        shader->so = so_count != 0;
        shader->rasterized_stream = rasterized_stream;
        options = so_options(shader);
    }
    const void *code = bytecode;
    SIZE_T code_length = length;
    void *passthrough = nullptr;
    if (with_so && program != DXBC_GEOMETRY) {
        /* compile the triangle variant now since most draws use it, the others on first use. */
        shader->so_source = HeapAlloc(GetProcessHeap(), 0, length);
        if (!shader->so_source) {
            free_shader(shader);
            return E_OUTOFMEMORY;
        }
        memcpy(shader->so_source, bytecode, length);
        shader->so_source_length = length;
        passthrough = dxbc_passthrough_gs(bytecode, length, 3, &g_d3d_allocator, &code_length);
        code = passthrough;
    }
    if (!code || dxbc_compile(code, code_length, &options, &shader->info, &g_d3d_allocator)) {
        D3dLog("d3d11: shader compilation failed: %s", shader->info.error);
        g_d3d_allocator.free(nullptr, passthrough);
        free_shader(shader);
        return E_INVALIDARG;
    }
    g_d3d_allocator.free(nullptr, passthrough);
    if (!out) {
        free_shader(shader);
        return S_FALSE;
    }
    shader->shader = make_shader(device, stage, &shader->info);
    if (!shader->shader) {
        free_shader(shader);
        return E_INVALIDARG;
    }
    if (shader->so_source)
        shader->so_variants[3] = shader->shader;
    /* the driver has the spir-v now, the rest of the info is still needed. */
    g_d3d_allocator.free(nullptr, shader->info.spirv);
    shader->info.spirv = nullptr;
    shader->iface.vs.lpVtbl = &shader_vtbl;
    shader->stage = stage;
    D3dChildInit(&shader->child, device);
    *out = shader;
    return S_OK;
}
