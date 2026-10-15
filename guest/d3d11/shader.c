/*
 * shaders: dxbc compiled to spir-v and made a vulkan shader object, which
 * any other bound shaders combine with at draw time, as direct3d binds
 * them. all stages share one pipeline layout: the heap and the tables.
 */
#include "d3d11_private.h"

#define VTBL_FN(f) ((void *)(f))

extern const MimeAllocator g_d3d_allocator;

static D3dShader *impl(void *iface) { return CONTAINING_RECORD(iface, D3dShader, iface); }

D3dShader *D3dShaderFrom(void *iface, D3dStage stage)
{
    if (!iface)
        return NULL;
    D3dShader *shader = impl(iface);
    return shader->stage == stage ? shader : NULL;
}

static const GUID *const STAGE_IIDS[D3D_STAGES] = {&IID_ID3D11VertexShader,   &IID_ID3D11HullShader,
                                                   &IID_ID3D11DomainShader,   &IID_ID3D11GeometryShader,
                                                   &IID_ID3D11PixelShader,    &IID_ID3D11ComputeShader};

static HRESULT STDMETHODCALLTYPE shader_query(void *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    D3dShader *shader = impl(iface);
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_ID3D11DeviceChild) &&
        !IsEqualGUID(riid, STAGE_IIDS[shader->stage])) {
        *out = NULL;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&shader->child.refs);
    *out = iface;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE shader_add_ref(void *iface) { return InterlockedIncrement(&impl(iface)->child.refs); }

static void shader_destroy(D3dShader *shader)
{
    D3dDevice *device = shader->child.device;
    D3dDefer(device, device->next_serial, D3D_GARBAGE_SHADER, (UINT64)shader->shader);
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

/* every stage's vtable is the device child's alone. */
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

/* the stages that may follow, as enabled on the device. */
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

HRESULT D3dCreateShader(D3dDevice *device, D3dStage stage, const void *bytecode, SIZE_T length,
                        const D3D11_SO_DECLARATION_ENTRY *so, UINT so_count, const UINT *strides,
                        UINT stride_count, UINT rasterized_stream, D3dShader **out)
{
    if (dxbc_check(bytecode, length))
        return E_INVALIDARG;
    if ((stage == D3D_HS || stage == D3D_DS) && !device->has_tessellation)
        return E_INVALIDARG;
    D3dShader *shader = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*shader));
    if (!shader)
        return E_OUTOFMEMORY;
    DxbcStreamOutput outputs[64];
    DxbcCompileOptions options = {0};
    if (so_count) {
        for (UINT i = 0; i < so_count; i++)
            outputs[i] = (DxbcStreamOutput){so[i].Stream,         so[i].SemanticName,   so[i].SemanticIndex,
                                            so[i].StartComponent, so[i].ComponentCount, so[i].OutputSlot};
        options.so = outputs;
        options.so_count = so_count;
        options.so_strides = strides;
        options.so_stride_count = stride_count;
        options.rasterized_stream = rasterized_stream;
    }
    if (dxbc_compile(bytecode, length, &options, &shader->info, &g_d3d_allocator)) {
        D3dLog("d3d11: shader compilation failed: %s", shader->info.error);
        dxbc_free(&shader->info, &g_d3d_allocator);
        HeapFree(GetProcessHeap(), 0, shader);
        return E_INVALIDARG;
    }
    if (shader->info.program != PROGRAMS[stage]) {
        dxbc_free(&shader->info, &g_d3d_allocator);
        HeapFree(GetProcessHeap(), 0, shader);
        return E_INVALIDARG;
    }
    if (!out) {
        dxbc_free(&shader->info, &g_d3d_allocator);
        HeapFree(GetProcessHeap(), 0, shader);
        return S_FALSE;
    }
    VkDescriptorSetLayout sets[2] = {device->heap_layout, device->table_layout};
    VkShaderCreateInfoEXT info = {VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT};
    info.stage = VK_STAGES[stage];
    info.nextStage = next_stages(device, stage);
    info.codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;
    info.codeSize = shader->info.spirv_words * 4;
    info.pCode = shader->info.spirv;
    info.pName = "main";
    info.setLayoutCount = 2;
    info.pSetLayouts = sets;
    if (device->vk.vkCreateShadersEXT(device->vk.device, 1, &info, NULL, &shader->shader) != VK_SUCCESS) {
        D3dLog("d3d11: vkCreateShadersEXT failed for a stage %u shader", stage);
        dxbc_free(&shader->info, &g_d3d_allocator);
        HeapFree(GetProcessHeap(), 0, shader);
        return E_INVALIDARG;
    }
    /* the spir-v went to the driver; the rest of the description stays. */
    g_d3d_allocator.free(NULL, shader->info.spirv);
    shader->info.spirv = NULL;
    shader->iface.vs.lpVtbl = &shader_vtbl;
    shader->stage = stage;
    D3dChildInit(&shader->child, device);
    *out = shader;
    return S_OK;
}
