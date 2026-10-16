/*
 * state objects: ray tracing pipelines and the collections they take
 * shaders from. a state object gathers the libraries, shaders and hit
 * groups of its own subobjects and of the collections it names, with the
 * root signatures and configurations associated with each. an executable
 * one compiles every shader a group uses against its root signatures, the
 * local one's arguments read from the shader record, and makes a vulkan
 * ray tracing pipeline of a group for each ray generation, miss and
 * callable shader and each hit group. shader identifiers are the groups'
 * handles, vulkan's being 32 bytes as direct3d's are.
 *
 * a pipeline that allows additions is a vulkan pipeline library linked
 * into the executable pipeline, and AddToStateObject links the parent's
 * libraries with one of the addition's, so the identifiers the parent gave
 * stay valid, as VK_EXT_pipeline_library_group_handles keeps a library's
 * handles in every pipeline linking it.
 */
#include "d3d12_private.h"

#include "../lib/dxil/dxil.h"

/* a library's bytecode, shared by the state objects that take shaders from it. */
typedef struct SoBlob {
    LONG refs;
    SIZE_T size;
    BYTE data[];
} SoBlob;

typedef struct SoShader {
    WCHAR *name;
    char function[128];
    SoBlob *library;
    UINT kind;
    /* the local root signature associated with it, NULL for none yet. */
    D12RootSignature *local;
} SoShader;

typedef struct SoHitGroup {
    WCHAR *name;
    D3D12_HIT_GROUP_TYPE type;
    WCHAR *any, *closest, *intersection;
    D12RootSignature *local;
} SoHitGroup;

/* a vulkan group: what direct3d identifies, a general shader or a hit group. */
typedef struct SoGroup {
    const WCHAR *name;
    BYTE handle[D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES];
    /* the group's index in the linked pipeline, and its shaders' stack sizes. */
    UINT index;
    UINT64 stack[4];
    /* the shaders it has: general, closest hit, any hit, intersection. */
    UINT used;
    UINT kind;
} SoGroup;

struct D12StateObject {
    ID3D12StateObject iface;
    ID3D12StateObjectProperties1 props;
    D12Child child;
    D3D12_STATE_OBJECT_TYPE type;
    D3D12_STATE_OBJECT_FLAGS flags;

    SoShader *shaders;
    UINT shader_count, shader_capacity;
    SoHitGroup *groups;
    UINT group_count, group_capacity;
    D12RootSignature *global_root;
    BOOL has_shader_config, has_pipeline_config;
    D3D12_RAYTRACING_SHADER_CONFIG shader_config;
    D3D12_RAYTRACING_PIPELINE_CONFIG1 pipeline_config;

    /* executables: the pipeline, its libraries when it allows additions, and what it identifies. */
    VkPipeline pipeline;
    VkPipeline *libraries;
    UINT library_count;
    SoGroup *identified;
    UINT identified_count;
    UINT64 stack_size;
    struct D12StateObject *parent;
};

/* ------------------------------------------------------------------ names and blobs */

static BOOL name_equal(const WCHAR *a, const WCHAR *b)
{
    if (!a || !b)
        return FALSE;
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

static WCHAR *name_copy(const WCHAR *s)
{
    if (!s)
        return NULL;
    SIZE_T n = 0;
    while (s[n])
        n++;
    WCHAR *out = HeapAlloc(GetProcessHeap(), 0, (n + 1) * sizeof(WCHAR));
    if (out)
        memcpy(out, s, (n + 1) * sizeof(WCHAR));
    return out;
}

static WCHAR *name_from_ascii(const char *s)
{
    SIZE_T n = strlen(s);
    WCHAR *out = HeapAlloc(GetProcessHeap(), 0, (n + 1) * sizeof(WCHAR));
    if (out)
        for (SIZE_T i = 0; i <= n; i++)
            out[i] = (BYTE)s[i];
    return out;
}

/* a function's name, which libraries' names are, in ascii; FALSE when it is not. */
static BOOL ascii_of(const WCHAR *s, char *out, SIZE_T size)
{
    SIZE_T i = 0;
    for (; s[i]; i++) {
        if (s[i] > 0x7F || i + 1 >= size)
            return FALSE;
        out[i] = (char)s[i];
    }
    out[i] = 0;
    return TRUE;
}

static SoBlob *blob_new(const void *data, SIZE_T size)
{
    SoBlob *b = HeapAlloc(GetProcessHeap(), 0, sizeof(*b) + size);
    if (b) {
        b->refs = 1;
        b->size = size;
        memcpy(b->data, data, size);
    }
    return b;
}

static void blob_release(SoBlob *b)
{
    if (b && !InterlockedDecrement(&b->refs))
        HeapFree(GetProcessHeap(), 0, b);
}

static void root_add_ref(D12RootSignature *root)
{
    if (root)
        root->iface.lpVtbl->AddRef(&root->iface);
}

static void root_release(D12RootSignature *root)
{
    if (root)
        root->iface.lpVtbl->Release(&root->iface);
}

/* ------------------------------------------------------------------ gathering */

static BOOL grow(void **list, UINT count, UINT *capacity, SIZE_T item)
{
    if (count < *capacity)
        return TRUE;
    UINT n = *capacity ? *capacity * 2 : 16;
    void *p = *list ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, *list, n * item)
                    : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, n * item);
    if (!p)
        return FALSE;
    *list = p;
    *capacity = n;
    return TRUE;
}

static SoShader *find_shader(D12StateObject *so, const WCHAR *name)
{
    for (UINT i = 0; i < so->shader_count; i++)
        if (name_equal(so->shaders[i].name, name))
            return &so->shaders[i];
    return NULL;
}

static SoHitGroup *find_group(D12StateObject *so, const WCHAR *name)
{
    for (UINT i = 0; i < so->group_count; i++)
        if (name_equal(so->groups[i].name, name))
            return &so->groups[i];
    return NULL;
}

static HRESULT add_shader(D12StateObject *so, const WCHAR *name, const char *function, SoBlob *library, UINT kind,
                          D12RootSignature *local)
{
    if (find_shader(so, name) || find_group(so, name))
        return E_INVALIDARG;
    if (!grow((void **)&so->shaders, so->shader_count, &so->shader_capacity, sizeof(SoShader)))
        return E_OUTOFMEMORY;
    SoShader *s = &so->shaders[so->shader_count];
    s->name = name_copy(name);
    if (!s->name)
        return E_OUTOFMEMORY;
    SIZE_T n = D12_MIN(strlen(function), sizeof(s->function) - 1);
    memcpy(s->function, function, n);
    s->function[n] = 0;
    s->library = library;
    InterlockedIncrement(&library->refs);
    s->kind = kind;
    s->local = local;
    root_add_ref(local);
    so->shader_count++;
    return S_OK;
}

static HRESULT add_group(D12StateObject *so, const D3D12_HIT_GROUP_DESC *d, D12RootSignature *local)
{
    if (!d->HitGroupExport || find_shader(so, d->HitGroupExport) || find_group(so, d->HitGroupExport))
        return E_INVALIDARG;
    if (!grow((void **)&so->groups, so->group_count, &so->group_capacity, sizeof(SoHitGroup)))
        return E_OUTOFMEMORY;
    SoHitGroup *g = &so->groups[so->group_count++];
    g->name = name_copy(d->HitGroupExport);
    g->type = d->Type;
    g->any = name_copy(d->AnyHitShaderImport);
    g->closest = name_copy(d->ClosestHitShaderImport);
    g->intersection = name_copy(d->IntersectionShaderImport);
    g->local = local;
    root_add_ref(local);
    return g->name ? S_OK : E_OUTOFMEMORY;
}

/* the shaders of a dxil library: every one, or those the exports name, renamed as they say. */
static HRESULT add_library(D12StateObject *so, const D3D12_DXIL_LIBRARY_DESC *d)
{
    if (!d->DXILLibrary.pShaderBytecode || !d->DXILLibrary.BytecodeLength)
        return E_INVALIDARG;
    DxilExport exports[256];
    int n = dxil_library_exports(d->DXILLibrary.pShaderBytecode, d->DXILLibrary.BytecodeLength, exports, 256,
                                 &g_d12_allocator);
    if (n < 0)
        return E_INVALIDARG;
    n = D12_MIN(n, 256);
    SoBlob *blob = blob_new(d->DXILLibrary.pShaderBytecode, d->DXILLibrary.BytecodeLength);
    if (!blob)
        return E_OUTOFMEMORY;
    HRESULT hr = S_OK;
    if (!d->NumExports) {
        for (int i = 0; i < n && SUCCEEDED(hr); i++) {
            if (exports[i].kind < DXIL_KIND_RAYGEN || exports[i].kind > DXIL_KIND_CALLABLE)
                continue;
            WCHAR *name = name_from_ascii(exports[i].name);
            hr = name ? add_shader(so, name, exports[i].name, blob, exports[i].kind, NULL) : E_OUTOFMEMORY;
            HeapFree(GetProcessHeap(), 0, name);
        }
    }
    for (UINT k = 0; k < d->NumExports && SUCCEEDED(hr); k++) {
        const D3D12_EXPORT_DESC *e = &d->pExports[k];
        char function[128];
        if (!e->Name || !ascii_of(e->ExportToRename ? e->ExportToRename : e->Name, function, sizeof(function))) {
            hr = E_INVALIDARG;
            break;
        }
        int found = -1;
        for (int i = 0; i < n && found < 0; i++)
            if (!strcmp(exports[i].name, function) || dxil_export_matches(function, exports[i].name))
                found = i;
        if (found < 0) {
            D12Log("d3d12: a library exports no %s", function);
            hr = E_INVALIDARG;
            break;
        }
        /* functions that are no ray tracing shader are only called by others. */
        if (exports[found].kind >= DXIL_KIND_RAYGEN && exports[found].kind <= DXIL_KIND_CALLABLE)
            hr = add_shader(so, e->Name, exports[found].name, blob, exports[found].kind, NULL);
    }
    blob_release(blob);
    return hr;
}

static HRESULT add_collection(D12StateObject *so, const D3D12_EXISTING_COLLECTION_DESC *d)
{
    D12StateObject *from = D12StateObjectFrom(d->pExistingCollection);
    if (!from || from->type != D3D12_STATE_OBJECT_TYPE_COLLECTION)
        return E_INVALIDARG;
    HRESULT hr = S_OK;
    for (UINT i = 0; i < from->shader_count && SUCCEEDED(hr); i++) {
        const SoShader *s = &from->shaders[i];
        const WCHAR *name = s->name;
        BOOL wanted = !d->NumExports;
        for (UINT k = 0; k < d->NumExports && !wanted; k++) {
            const D3D12_EXPORT_DESC *e = &d->pExports[k];
            if (name_equal(e->ExportToRename ? e->ExportToRename : e->Name, s->name)) {
                wanted = TRUE;
                name = e->Name;
            }
        }
        if (wanted)
            hr = add_shader(so, name, s->function, s->library, s->kind, s->local);
    }
    for (UINT i = 0; i < from->group_count && SUCCEEDED(hr); i++) {
        const SoHitGroup *g = &from->groups[i];
        D3D12_HIT_GROUP_DESC d2 = {g->name, g->type, g->any, g->closest, g->intersection};
        hr = add_group(so, &d2, g->local);
    }
    if (SUCCEEDED(hr) && !so->global_root && from->global_root) {
        so->global_root = from->global_root;
        root_add_ref(so->global_root);
    }
    if (!so->has_shader_config && from->has_shader_config) {
        so->shader_config = from->shader_config;
        so->has_shader_config = TRUE;
    }
    if (!so->has_pipeline_config && from->has_pipeline_config) {
        so->pipeline_config = from->pipeline_config;
        so->has_pipeline_config = TRUE;
    }
    return hr;
}

/* the subobject an association names, as an index in the description's array. */
static int subobject_index(const D3D12_STATE_OBJECT_DESC *desc, const D3D12_STATE_SUBOBJECT *s)
{
    for (UINT i = 0; i < desc->NumSubobjects; i++)
        if (&desc->pSubobjects[i] == s)
            return (int)i;
    return -1;
}

/*
 * the local root signature for an export: one associated with its name,
 * else one of an association to no export, else one no association names.
 */
static D12RootSignature *local_root_for(const D3D12_STATE_OBJECT_DESC *desc, const BYTE *associated,
                                        const WCHAR *name)
{
    D12RootSignature *fallback = NULL, *unassociated = NULL;
    for (UINT i = 0; i < desc->NumSubobjects; i++) {
        const D3D12_STATE_SUBOBJECT *s = &desc->pSubobjects[i];
        if (s->Type == D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE && !associated[i] && !unassociated)
            unassociated = D12RootSignatureFrom(((const D3D12_LOCAL_ROOT_SIGNATURE *)s->pDesc)->pLocalRootSignature);
        if (s->Type != D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION)
            continue;
        const D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION *a = s->pDesc;
        if (!a->pSubobjectToAssociate || a->pSubobjectToAssociate->Type != D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE)
            continue;
        D12RootSignature *root =
            D12RootSignatureFrom(((const D3D12_LOCAL_ROOT_SIGNATURE *)a->pSubobjectToAssociate->pDesc)->pLocalRootSignature);
        if (!a->NumExports && !fallback)
            fallback = root;
        for (UINT k = 0; k < a->NumExports; k++)
            if (name_equal(a->pExports[k], name))
                return root;
    }
    return fallback ? fallback : unassociated;
}

static HRESULT gather(D12StateObject *so, const D3D12_STATE_OBJECT_DESC *desc)
{
    BYTE *associated = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, desc->NumSubobjects + 1);
    if (!associated)
        return E_OUTOFMEMORY;
    for (UINT i = 0; i < desc->NumSubobjects; i++) {
        const D3D12_STATE_SUBOBJECT *s = &desc->pSubobjects[i];
        if (s->Type == D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION) {
            int k = subobject_index(desc, ((const D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION *)s->pDesc)->pSubobjectToAssociate);
            if (k >= 0)
                associated[k] = 1;
        }
    }
    HRESULT hr = S_OK;
    UINT first_shader = so->shader_count, first_group = so->group_count;
    for (UINT i = 0; i < desc->NumSubobjects && SUCCEEDED(hr); i++) {
        const D3D12_STATE_SUBOBJECT *s = &desc->pSubobjects[i];
        switch (s->Type) {
        case D3D12_STATE_SUBOBJECT_TYPE_STATE_OBJECT_CONFIG:
            so->flags |= ((const D3D12_STATE_OBJECT_CONFIG *)s->pDesc)->Flags;
            break;
        case D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE: {
            D12RootSignature *root =
                D12RootSignatureFrom(((const D3D12_GLOBAL_ROOT_SIGNATURE *)s->pDesc)->pGlobalRootSignature);
            if (root && !so->global_root) {
                so->global_root = root;
                root_add_ref(root);
            }
            break;
        }
        case D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY: hr = add_library(so, s->pDesc); break;
        case D3D12_STATE_SUBOBJECT_TYPE_EXISTING_COLLECTION: hr = add_collection(so, s->pDesc); break;
        case D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP: hr = add_group(so, s->pDesc, NULL); break;
        case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG:
            so->shader_config = *(const D3D12_RAYTRACING_SHADER_CONFIG *)s->pDesc;
            so->has_shader_config = TRUE;
            break;
        case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG:
            so->pipeline_config.MaxTraceRecursionDepth =
                ((const D3D12_RAYTRACING_PIPELINE_CONFIG *)s->pDesc)->MaxTraceRecursionDepth;
            so->has_pipeline_config = TRUE;
            break;
        case D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG1:
            so->pipeline_config = *(const D3D12_RAYTRACING_PIPELINE_CONFIG1 *)s->pDesc;
            so->has_pipeline_config = TRUE;
            break;
        case D3D12_STATE_SUBOBJECT_TYPE_DXIL_SUBOBJECT_TO_EXPORTS_ASSOCIATION:
            D12Log("d3d12: subobjects of dxil libraries are not supported");
            break;
        default: break;
        }
    }
    /* this description's shaders and hit groups take its local root signatures. */
    for (UINT i = first_shader; i < so->shader_count && SUCCEEDED(hr); i++)
        if (!so->shaders[i].local) {
            so->shaders[i].local = local_root_for(desc, associated, so->shaders[i].name);
            root_add_ref(so->shaders[i].local);
        }
    for (UINT i = first_group; i < so->group_count && SUCCEEDED(hr); i++)
        if (!so->groups[i].local) {
            so->groups[i].local = local_root_for(desc, associated, so->groups[i].name);
            root_add_ref(so->groups[i].local);
        }
    HeapFree(GetProcessHeap(), 0, associated);
    return hr;
}

/* ------------------------------------------------------------------ the pipeline */

static VkShaderStageFlagBits stage_of(UINT kind)
{
    switch (kind) {
    case DXIL_KIND_RAYGEN: return VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    case DXIL_KIND_INTERSECTION: return VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
    case DXIL_KIND_ANYHIT: return VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
    case DXIL_KIND_CLOSESTHIT: return VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    case DXIL_KIND_MISS: return VK_SHADER_STAGE_MISS_BIT_KHR;
    default: return VK_SHADER_STAGE_CALLABLE_BIT_KHR;
    }
}

/* a shader against the global root signature and its local one, as a module. */
static HRESULT compile_shader(D12Device *device, D12StateObject *so, const SoShader *s, D12RootSignature *local,
                              VkShaderModule *module)
{
    UINT global_count = 0, local_count = 0;
    DxbcRootBinding *global = D12RootBindings(so->global_root, D12_CS, &global_count);
    DxbcRootBinding *locals = local ? D12RootBindings(local, D12_CS, &local_count) : NULL;
    DxbcRootBinding *all = HeapAlloc(GetProcessHeap(), 0, (global_count + local_count + 1) * sizeof(*all));
    HRESULT hr = E_OUTOFMEMORY;
    if (global && all && (!local || locals)) {
        memcpy(all, global, global_count * sizeof(*all));
        if (local_count)
            memcpy(all + global_count, locals, local_count * sizeof(*all));
        DxbcCompileOptions options = {0};
        options.root_signature = 1;
        options.root = all;
        options.root_count = global_count + local_count;
        options.export_name = s->function;
        options.handle_bits = D12_HANDLE_SHIFT;
        options.increment_shift = D12_INCREMENT_SHIFT;
        DxbcShaderInfo info;
        if (dxil_compile(s->library->data, s->library->size, &options, &info, &g_d12_allocator)) {
            D12Log("d3d12: shader %s failed to compile: %s", s->function, info.error);
            hr = E_INVALIDARG;
        } else {
            VkShaderModuleCreateInfo create = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            create.codeSize = info.spirv_words * 4;
            create.pCode = info.spirv;
            hr = device->vk.vkCreateShaderModule(device->vk.device, &create, NULL, module) == VK_SUCCESS ? S_OK
                                                                                                        : E_OUTOFMEMORY;
        }
        dxbc_free(&info, &g_d12_allocator);
    }
    HeapFree(GetProcessHeap(), 0, global);
    HeapFree(GetProcessHeap(), 0, locals);
    HeapFree(GetProcessHeap(), 0, all);
    return hr;
}

typedef struct Builder {
    VkPipelineShaderStageCreateInfo *stages;
    VkRayTracingShaderGroupCreateInfoKHR *groups;
    UINT stage_count, group_count;
    /* by shader, its stage, and the root signature it was compiled with. */
    UINT *stage_of_shader;
} Builder;

/* the stage of a shader, compiled at its first use with the local root signature of what uses it. */
static UINT use_shader(D12Device *device, D12StateObject *so, Builder *b, const WCHAR *name, UINT kind,
                       D12RootSignature *local, HRESULT *hr)
{
    if (!name)
        return VK_SHADER_UNUSED_KHR;
    SoShader *s = find_shader(so, name);
    if (!s || s->kind != kind) {
        *hr = E_INVALIDARG;
        return VK_SHADER_UNUSED_KHR;
    }
    UINT i = (UINT)(s - so->shaders);
    if (b->stage_of_shader[i] != ~0u)
        return b->stage_of_shader[i];
    VkPipelineShaderStageCreateInfo *st = &b->stages[b->stage_count];
    st->sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st->stage = stage_of(kind);
    st->pName = "main";
    HRESULT r = compile_shader(device, so, s, s->local ? s->local : local, &st->module);
    if (FAILED(r)) {
        *hr = r;
        return VK_SHADER_UNUSED_KHR;
    }
    return b->stage_of_shader[i] = b->stage_count++;
}

static void set_interface(D12StateObject *so, VkRayTracingPipelineInterfaceCreateInfoKHR *iface)
{
    iface->sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_INTERFACE_CREATE_INFO_KHR;
    iface->maxPipelineRayPayloadSize = so->shader_config.MaxPayloadSizeInBytes;
    iface->maxPipelineRayHitAttributeSize = so->shader_config.MaxAttributeSizeInBytes;
}

static VkPipelineCreateFlags pipeline_flags(D12StateObject *so)
{
    VkPipelineCreateFlags flags = VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    if (so->pipeline_config.Flags & D3D12_RAYTRACING_PIPELINE_FLAG_SKIP_TRIANGLES)
        flags |= VK_PIPELINE_CREATE_RAY_TRACING_SKIP_TRIANGLES_BIT_KHR;
    if (so->pipeline_config.Flags & D3D12_RAYTRACING_PIPELINE_FLAG_SKIP_PROCEDURAL_PRIMITIVES)
        flags |= VK_PIPELINE_CREATE_RAY_TRACING_SKIP_AABBS_BIT_KHR;
    return flags;
}

/* the groups of this state object's own shaders and hit groups, compiled into a pipeline or a library. */
static HRESULT build_own(D12Device *device, D12StateObject *so, BOOL library, VkPipeline *out, UINT group_base)
{
    D12Vk *vk = &device->vk;
    Builder b = {0};
    UINT max_stages = so->shader_count + 1, max_groups = so->shader_count + so->group_count + 1;
    b.stages = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, max_stages * sizeof(*b.stages));
    b.groups = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, max_groups * sizeof(*b.groups));
    b.stage_of_shader = HeapAlloc(GetProcessHeap(), 0, max_stages * sizeof(UINT));
    so->identified = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, max_groups * sizeof(SoGroup));
    HRESULT hr = b.stages && b.groups && b.stage_of_shader && so->identified ? S_OK : E_OUTOFMEMORY;
    for (UINT i = 0; i < max_stages && b.stage_of_shader; i++)
        b.stage_of_shader[i] = ~0u;

    for (UINT i = 0; i < so->shader_count && SUCCEEDED(hr); i++) {
        SoShader *s = &so->shaders[i];
        if (s->kind != DXIL_KIND_RAYGEN && s->kind != DXIL_KIND_MISS && s->kind != DXIL_KIND_CALLABLE)
            continue;
        VkRayTracingShaderGroupCreateInfoKHR *g = &b.groups[b.group_count];
        g->sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        g->type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        g->generalShader = use_shader(device, so, &b, s->name, s->kind, NULL, &hr);
        g->closestHitShader = g->anyHitShader = g->intersectionShader = VK_SHADER_UNUSED_KHR;
        so->identified[b.group_count].name = s->name;
        so->identified[b.group_count].kind = s->kind;
        so->identified[b.group_count].used = 1;
        b.group_count++;
    }
    for (UINT i = 0; i < so->group_count && SUCCEEDED(hr); i++) {
        SoHitGroup *h = &so->groups[i];
        VkRayTracingShaderGroupCreateInfoKHR *g = &b.groups[b.group_count];
        g->sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        g->type = h->type == D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE
                      ? VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR
                      : VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
        g->generalShader = VK_SHADER_UNUSED_KHR;
        g->closestHitShader = use_shader(device, so, &b, h->closest, DXIL_KIND_CLOSESTHIT, h->local, &hr);
        g->anyHitShader = use_shader(device, so, &b, h->any, DXIL_KIND_ANYHIT, h->local, &hr);
        g->intersectionShader = h->type == D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE
                                    ? use_shader(device, so, &b, h->intersection, DXIL_KIND_INTERSECTION, h->local, &hr)
                                    : VK_SHADER_UNUSED_KHR;
        so->identified[b.group_count].name = h->name;
        so->identified[b.group_count].kind = 0;
        so->identified[b.group_count].used = (g->closestHitShader != VK_SHADER_UNUSED_KHR) << 1 |
                                              (g->anyHitShader != VK_SHADER_UNUSED_KHR) << 2 |
                                              (g->intersectionShader != VK_SHADER_UNUSED_KHR) << 3;
        b.group_count++;
    }
    so->identified_count = b.group_count;

    if (SUCCEEDED(hr)) {
        VkRayTracingPipelineInterfaceCreateInfoKHR iface = {0};
        set_interface(so, &iface);
        VkDynamicState dynamic = VK_DYNAMIC_STATE_RAY_TRACING_PIPELINE_STACK_SIZE_KHR;
        VkPipelineDynamicStateCreateInfo dyn = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dyn.dynamicStateCount = 1;
        dyn.pDynamicStates = &dynamic;
        VkRayTracingPipelineCreateInfoKHR info = {VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
        info.flags = pipeline_flags(so) | (library ? VK_PIPELINE_CREATE_LIBRARY_BIT_KHR : 0);
        info.stageCount = b.stage_count;
        info.pStages = b.stages;
        info.groupCount = b.group_count;
        info.pGroups = b.groups;
        info.maxPipelineRayRecursionDepth = so->pipeline_config.MaxTraceRecursionDepth;
        info.pLibraryInterface = library ? &iface : NULL;
        info.pDynamicState = library ? NULL : &dyn;
        info.layout = so->global_root->layout;
        if (vk->vkCreateRayTracingPipelinesKHR(vk->device, VK_NULL_HANDLE, device->pipeline_cache, 1, &info, NULL,
                                               out) != VK_SUCCESS)
            hr = E_INVALIDARG;
    }
    for (UINT i = 0; i < b.stage_count; i++)
        vk->vkDestroyShaderModule(vk->device, b.stages[i].module, NULL);
    for (UINT i = 0; SUCCEEDED(hr) && i < b.group_count; i++) {
        so->identified[i].index = group_base + i;
        if (vk->vkGetRayTracingShaderGroupHandlesKHR(vk->device, *out, i, 1, D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES,
                                                     so->identified[i].handle) != VK_SUCCESS)
            hr = E_FAIL;
    }
    HeapFree(GetProcessHeap(), 0, b.stages);
    HeapFree(GetProcessHeap(), 0, b.groups);
    HeapFree(GetProcessHeap(), 0, b.stage_of_shader);
    return hr;
}

/* the stack sizes of each group's shaders, and direct3d's default pipeline stack size of them. */
static void stack_sizes(D12Device *device, D12StateObject *so)
{
    D12Vk *vk = &device->vk;
    static const VkShaderGroupShaderKHR WHICH[4] = {VK_SHADER_GROUP_SHADER_GENERAL_KHR,
                                                    VK_SHADER_GROUP_SHADER_CLOSEST_HIT_KHR,
                                                    VK_SHADER_GROUP_SHADER_ANY_HIT_KHR,
                                                    VK_SHADER_GROUP_SHADER_INTERSECTION_KHR};
    UINT64 raygen = 0, closest = 0, miss = 0, isect = 0, any = 0, callable = 0;
    for (D12StateObject *o = so; o; o = o->parent)
        for (UINT i = 0; i < o->identified_count; i++) {
            SoGroup *g = &o->identified[i];
            for (UINT w = 0; w < 4; w++)
                g->stack[w] = g->used & (1u << w) ? vk->vkGetRayTracingShaderGroupStackSizeKHR(vk->device, so->pipeline,
                                                                                               g->index, WHICH[w])
                                                  : 0;
            switch (g->kind) {
            case DXIL_KIND_RAYGEN: raygen = D12_MAX(raygen, g->stack[0]); break;
            case DXIL_KIND_MISS: miss = D12_MAX(miss, g->stack[0]); break;
            case DXIL_KIND_CALLABLE: callable = D12_MAX(callable, g->stack[0]); break;
            default:
                closest = D12_MAX(closest, g->stack[1]);
                any = D12_MAX(any, g->stack[2]);
                isect = D12_MAX(isect, g->stack[3]);
                break;
            }
        }
    UINT64 depth = so->pipeline_config.MaxTraceRecursionDepth;
    so->stack_size = raygen + D12_MIN(1, depth) * D12_MAX(D12_MAX(closest, miss), isect + any) +
                     (depth > 1 ? depth - 1 : 0) * D12_MAX(closest, miss) + 2 * callable;
}

static HRESULT build_executable(D12Device *device, D12StateObject *so)
{
    D12Vk *vk = &device->vk;
    if (!so->has_shader_config || !so->has_pipeline_config || !so->global_root) {
        D12Log("d3d12: a ray tracing pipeline lacks its configurations or its global root signature");
        return E_INVALIDARG;
    }
    BOOL libraries = so->parent || (so->flags & D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS);
    if (!libraries)
        return build_own(device, so, FALSE, &so->pipeline, 0);

    /* the parent's libraries first, so their groups keep their indices, then one of this object's own. */
    UINT inherited = so->parent ? so->parent->library_count : 0, base = 0;
    for (D12StateObject *o = so->parent; o; o = o->parent)
        base += o->identified_count;
    so->libraries = HeapAlloc(GetProcessHeap(), 0, (inherited + 1) * sizeof(VkPipeline));
    if (!so->libraries)
        return E_OUTOFMEMORY;
    VkPipeline own = VK_NULL_HANDLE;
    HRESULT hr = build_own(device, so, TRUE, &own, base);
    if (FAILED(hr))
        return hr;
    if (inherited)
        memcpy(so->libraries, so->parent->libraries, inherited * sizeof(VkPipeline));
    so->libraries[inherited] = own;
    so->library_count = inherited + 1;

    VkRayTracingPipelineInterfaceCreateInfoKHR iface = {0};
    set_interface(so, &iface);
    VkPipelineLibraryCreateInfoKHR link = {VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR};
    link.libraryCount = so->library_count;
    link.pLibraries = so->libraries;
    VkDynamicState dynamic = VK_DYNAMIC_STATE_RAY_TRACING_PIPELINE_STACK_SIZE_KHR;
    VkPipelineDynamicStateCreateInfo dyn = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 1;
    dyn.pDynamicStates = &dynamic;
    VkRayTracingPipelineCreateInfoKHR info = {VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR};
    info.flags = pipeline_flags(so);
    info.maxPipelineRayRecursionDepth = so->pipeline_config.MaxTraceRecursionDepth;
    info.pLibraryInfo = &link;
    info.pLibraryInterface = &iface;
    info.pDynamicState = &dyn;
    info.layout = so->global_root->layout;
    if (vk->vkCreateRayTracingPipelinesKHR(vk->device, VK_NULL_HANDLE, device->pipeline_cache, 1, &info, NULL,
                                           &so->pipeline) != VK_SUCCESS)
        return E_INVALIDARG;
    return S_OK;
}

/* ------------------------------------------------------------------ interfaces */

D12StateObject *D12StateObjectFrom(ID3D12StateObject *iface)
{
    return iface ? CONTAINING_RECORD(iface, D12StateObject, iface) : NULL;
}

static D12StateObject *impl(ID3D12StateObject *iface) { return CONTAINING_RECORD(iface, D12StateObject, iface); }

static D12StateObject *props_impl(ID3D12StateObjectProperties1 *iface)
{
    return CONTAINING_RECORD(iface, D12StateObject, props);
}

static void state_destroy(D12StateObject *so)
{
    D12Device *device = so->child.device;
    D12Vk *vk = &device->vk;
    if (so->pipeline)
        vk->vkDestroyPipeline(vk->device, so->pipeline, NULL);
    /* a library of its own is the last; the parent's go with the parent. */
    if (so->library_count)
        vk->vkDestroyPipeline(vk->device, so->libraries[so->library_count - 1], NULL);
    HeapFree(GetProcessHeap(), 0, so->libraries);
    for (UINT i = 0; i < so->shader_count; i++) {
        HeapFree(GetProcessHeap(), 0, so->shaders[i].name);
        blob_release(so->shaders[i].library);
        root_release(so->shaders[i].local);
    }
    for (UINT i = 0; i < so->group_count; i++) {
        SoHitGroup *g = &so->groups[i];
        HeapFree(GetProcessHeap(), 0, g->name);
        HeapFree(GetProcessHeap(), 0, g->any);
        HeapFree(GetProcessHeap(), 0, g->closest);
        HeapFree(GetProcessHeap(), 0, g->intersection);
        root_release(g->local);
    }
    HeapFree(GetProcessHeap(), 0, so->shaders);
    HeapFree(GetProcessHeap(), 0, so->groups);
    HeapFree(GetProcessHeap(), 0, so->identified);
    root_release(so->global_root);
    if (so->parent)
        so->parent->iface.lpVtbl->Release(&so->parent->iface);
}

static HRESULT STDMETHODCALLTYPE state_query(ID3D12StateObject *iface, REFIID riid, void **out)
{
    D12StateObject *so = impl(iface);
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_ID3D12Object) ||
        IsEqualGUID(riid, &IID_ID3D12DeviceChild) || IsEqualGUID(riid, &IID_ID3D12Pageable) ||
        IsEqualGUID(riid, &IID_ID3D12StateObject)) {
        *out = iface;
    } else if (IsEqualGUID(riid, &IID_ID3D12StateObjectProperties) ||
               IsEqualGUID(riid, &IID_ID3D12StateObjectProperties1)) {
        *out = &so->props;
    } else {
        *out = NULL;
        return E_NOINTERFACE;
    }
    InterlockedIncrement(&so->child.refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE state_add_ref(ID3D12StateObject *iface)
{
    return InterlockedIncrement(&impl(iface)->child.refs);
}

static ULONG STDMETHODCALLTYPE state_release(ID3D12StateObject *iface)
{
    D12StateObject *so = impl(iface);
    ULONG refs = InterlockedDecrement(&so->child.refs);
    if (!refs) {
        state_destroy(so);
        D12ChildRelease(&so->child);
        HeapFree(GetProcessHeap(), 0, so);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE state_get_private_data(ID3D12StateObject *iface, REFGUID guid, UINT *size, void *data)
{
    return MimePrivateGet(&impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE state_set_private_data(ID3D12StateObject *iface, REFGUID guid, UINT size,
                                                        const void *data)
{
    return MimePrivateSet(&impl(iface)->child.data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE state_set_private_data_interface(ID3D12StateObject *iface, REFGUID guid,
                                                                  const IUnknown *data)
{
    return MimePrivateSetInterface(&impl(iface)->child.data, guid, data);
}

static HRESULT STDMETHODCALLTYPE state_set_name(ID3D12StateObject *iface, LPCWSTR name) { return S_OK; }

static HRESULT STDMETHODCALLTYPE state_get_device(ID3D12StateObject *iface, REFIID riid, void **device)
{
    return D12ChildGetDevice(&impl(iface)->child, riid, device);
}

static const struct ID3D12StateObjectVtbl g_state_vtbl = {
    state_query,
    state_add_ref,
    state_release,
    state_get_private_data,
    state_set_private_data,
    state_set_private_data_interface,
    state_set_name,
    state_get_device,
};

static HRESULT STDMETHODCALLTYPE props_query(ID3D12StateObjectProperties1 *iface, REFIID riid, void **out)
{
    return state_query(&props_impl(iface)->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE props_add_ref(ID3D12StateObjectProperties1 *iface)
{
    return state_add_ref(&props_impl(iface)->iface);
}

static ULONG STDMETHODCALLTYPE props_release(ID3D12StateObjectProperties1 *iface)
{
    return state_release(&props_impl(iface)->iface);
}

/* a group this object or its parents identify. */
static SoGroup *identified(D12StateObject *so, const WCHAR *name)
{
    for (D12StateObject *o = so; o; o = o->parent)
        for (UINT i = 0; i < o->identified_count; i++)
            if (name_equal(o->identified[i].name, name))
                return &o->identified[i];
    return NULL;
}

static void *STDMETHODCALLTYPE props_get_shader_identifier(ID3D12StateObjectProperties1 *iface, LPCWSTR name)
{
    D12StateObject *so = props_impl(iface);
    SoGroup *g = so->pipeline ? identified(so, name) : NULL;
    return g ? g->handle : NULL;
}

/* a general shader's name, or a hit group's with "::closesthit", "::anyhit" or "::intersection". */
static UINT64 STDMETHODCALLTYPE props_get_shader_stack_size(ID3D12StateObjectProperties1 *iface, LPCWSTR name)
{
    D12StateObject *so = props_impl(iface);
    if (!name || !so->pipeline)
        return ~0ull;
    static const WCHAR *const PARTS[4] = {NULL, L"closesthit", L"anyhit", L"intersection"};
    WCHAR group[256];
    UINT n = 0;
    while (name[n] && !(name[n] == ':' && name[n + 1] == ':') && n + 1 < 256) {
        group[n] = name[n];
        n++;
    }
    group[n] = 0;
    SoGroup *g = identified(so, group);
    if (!g)
        return ~0ull;
    if (!name[n])
        return g->kind ? g->stack[0] : ~0ull;
    for (UINT w = 1; w < 4; w++)
        if (name_equal(name + n + 2, PARTS[w]))
            return g->kind ? ~0ull : g->stack[w];
    return ~0ull;
}

static UINT64 STDMETHODCALLTYPE props_get_pipeline_stack_size(ID3D12StateObjectProperties1 *iface)
{
    return props_impl(iface)->stack_size;
}

static void STDMETHODCALLTYPE props_set_pipeline_stack_size(ID3D12StateObjectProperties1 *iface, UINT64 size)
{
    props_impl(iface)->stack_size = size;
}

/* programs are work graphs', which mime does not run. */
static D3D12_PROGRAM_IDENTIFIER *STDMETHODCALLTYPE props_get_program_identifier(ID3D12StateObjectProperties1 *iface,
                                                                               D3D12_PROGRAM_IDENTIFIER *ret,
                                                                               LPCWSTR name)
{
    memset(ret, 0, sizeof(*ret));
    return ret;
}

static const struct ID3D12StateObjectProperties1Vtbl g_props_vtbl = {
    props_query,
    props_add_ref,
    props_release,
    props_get_shader_identifier,
    props_get_shader_stack_size,
    props_get_pipeline_stack_size,
    props_set_pipeline_stack_size,
    props_get_program_identifier,
};

HRESULT D12CreateStateObject(D12Device *device, const D3D12_STATE_OBJECT_DESC *desc, D12StateObject *parent,
                             D12StateObject **out)
{
    if (!desc || (desc->NumSubobjects && !desc->pSubobjects))
        return E_INVALIDARG;
    if (desc->Type != D3D12_STATE_OBJECT_TYPE_COLLECTION && desc->Type != D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE)
        return E_NOTIMPL;
    if (!device->has_rt_pipeline)
        return E_NOTIMPL;
    if (parent && (parent->type != D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE ||
                   !(parent->flags & D3D12_STATE_OBJECT_FLAG_ALLOW_STATE_OBJECT_ADDITIONS)))
        return E_INVALIDARG;
    D12StateObject *so = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*so));
    if (!so)
        return E_OUTOFMEMORY;
    so->iface.lpVtbl = &g_state_vtbl;
    so->props.lpVtbl = &g_props_vtbl;
    so->type = desc->Type;
    so->child.refs = 1;
    so->child.device = device;
    if (parent) {
        /* an addition keeps the parent's configurations and root signature. */
        so->parent = parent;
        parent->iface.lpVtbl->AddRef(&parent->iface);
        so->flags = parent->flags;
        so->global_root = parent->global_root;
        root_add_ref(so->global_root);
        so->shader_config = parent->shader_config;
        so->pipeline_config = parent->pipeline_config;
        so->has_shader_config = so->has_pipeline_config = TRUE;
    }
    HRESULT hr = gather(so, desc);
    if (SUCCEEDED(hr) && so->type == D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE) {
        hr = build_executable(device, so);
        if (SUCCEEDED(hr))
            stack_sizes(device, so);
    }
    if (FAILED(hr)) {
        state_destroy(so);
        HeapFree(GetProcessHeap(), 0, so);
        return hr;
    }
    D12ChildInit(&so->child, device);
    *out = so;
    return S_OK;
}

VkPipeline D12StateObjectPipeline(D12StateObject *so) { return so->pipeline; }

UINT64 D12StateObjectStackSize(D12StateObject *so) { return so->stack_size; }
