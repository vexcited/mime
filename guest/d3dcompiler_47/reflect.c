/*
 * D3DReflect: ID3D11ShaderReflection and its constant buffers, variables
 * and types, read from the RDEF, STAT, SFI0, Aon9 and signature chunks
 * and the declarations of the shader code, the way CShaderReflection of
 * D3DCompiler_47.dll reads them. what is out of range or not found is one
 * of the null objects, whose methods fail with E_FAIL.
 */
#include "d3dcompiler_private.h"

#define VTBL_FN(f) ((void *)(f))

static UINT read32(const BYTE *p) { return p[0] | p[1] << 8 | p[2] << 16 | (UINT)p[3] << 24; }
static UINT read16(const BYTE *p) { return p[0] | p[1] << 8; }

/* ------------------------------------------------------------------ objects */

typedef struct DcType DcType;
typedef struct DcBuffer DcBuffer;

struct DcType {
    ID3D11ShaderReflectionType iface;
    D3D11_SHADER_TYPE_DESC desc;
    const char **member_names;
    DcType *members;
    DcType *sub;
    DcType *base;
    DcType *interfaces;
    UINT interface_count;
    /* where the type is in the RDEF chunk: types there are equal when they are the same. */
    UINT identity;
};

typedef struct DcVariable {
    ID3D11ShaderReflectionVariable iface;
    D3D11_SHADER_VARIABLE_DESC desc;
    DcType *type;
    DcBuffer *buffer;
} DcVariable;

struct DcBuffer {
    ID3D11ShaderReflectionConstantBuffer iface;
    D3D11_SHADER_BUFFER_DESC desc;
    DcVariable *variables;
};

typedef struct DcBlock {
    struct DcBlock *next;
} DcBlock;

struct DcReflection {
    ID3D11ShaderReflection iface;
    LONG refs;
    BOOL d3d12;
    D3D11_SHADER_DESC desc;
    UINT program;
    UINT internal_version;
    UINT legacy_version;
    BOOL sample_frequency;
    UINT mov, movc, conversion, bitwise;
    D3D_PRIMITIVE gs_input_primitive;
    UINT interface_slots;
    UINT thread_group[3];
    UINT64 requires;
    DcBuffer *buffers;
    DcBindDesc *binds;
    D3D11_SIGNATURE_PARAMETER_DESC *signatures[3];
    DcBlock *blocks;
};

/* every allocation of a reflection, freed with it. */
static void *zalloc(DcReflection *r, SIZE_T size)
{
    DcBlock *block = DcAlloc(sizeof(DcBlock) + 8 + size);
    if (!block)
        return NULL;
    block->next = r->blocks;
    r->blocks = block;
    void *p = (BYTE *)block + ((sizeof(DcBlock) + 7) & ~(SIZE_T)7);
    memset(p, 0, size);
    return p;
}

/* ------------------------------------------------------------------ the null objects */

/* each takes the arguments of the method it stands for, as 32-bit callees pop them. */
static HRESULT STDMETHODCALLTYPE null_fail(void *iface, void *arg) { return E_FAIL; }
static UINT STDMETHODCALLTYPE null_zero(void *iface) { return 0; }
static UINT STDMETHODCALLTYPE null_slot(void *iface, UINT index) { return 0xFFFFFFFF; }
static LPCSTR STDMETHODCALLTYPE null_name(void *iface, UINT index) { return "$Invalid"; }
static ID3D11ShaderReflectionType *STDMETHODCALLTYPE null_type(void *iface);
static ID3D11ShaderReflectionType *STDMETHODCALLTYPE null_type_of(void *iface, UINT_PTR arg);
static ID3D11ShaderReflectionVariable *STDMETHODCALLTYPE null_variable_of(void *iface, UINT_PTR arg);
static ID3D11ShaderReflectionConstantBuffer *STDMETHODCALLTYPE null_buffer(void *iface);

static const ID3D11ShaderReflectionTypeVtbl null_type_vtbl = {
    VTBL_FN(null_fail),    VTBL_FN(null_type_of), VTBL_FN(null_type_of), VTBL_FN(null_name),
    VTBL_FN(null_fail),    VTBL_FN(null_type),    VTBL_FN(null_type),    VTBL_FN(null_zero),
    VTBL_FN(null_type_of), VTBL_FN(null_fail),    VTBL_FN(null_fail)};
static const ID3D11ShaderReflectionVariableVtbl null_variable_vtbl = {VTBL_FN(null_fail), VTBL_FN(null_type),
                                                                      VTBL_FN(null_buffer), VTBL_FN(null_slot)};
static const ID3D11ShaderReflectionConstantBufferVtbl null_buffer_vtbl = {
    VTBL_FN(null_fail), VTBL_FN(null_variable_of), VTBL_FN(null_variable_of)};

static ID3D11ShaderReflectionType g_null_type = {&null_type_vtbl};
static ID3D11ShaderReflectionVariable g_null_variable = {&null_variable_vtbl};
static ID3D11ShaderReflectionConstantBuffer g_null_buffer = {&null_buffer_vtbl};

static ID3D11ShaderReflectionType *STDMETHODCALLTYPE null_type(void *iface) { return &g_null_type; }
static ID3D11ShaderReflectionType *STDMETHODCALLTYPE null_type_of(void *iface, UINT_PTR arg) { return &g_null_type; }
static ID3D11ShaderReflectionVariable *STDMETHODCALLTYPE null_variable_of(void *iface, UINT_PTR arg)
{
    return &g_null_variable;
}
static ID3D11ShaderReflectionConstantBuffer *STDMETHODCALLTYPE null_buffer(void *iface) { return &g_null_buffer; }

/* ------------------------------------------------------------------ types */

static const ID3D11ShaderReflectionTypeVtbl type_vtbl;

static DcType *type_impl(ID3D11ShaderReflectionType *iface) { return CONTAINING_RECORD(iface, DcType, iface); }

/* the type behind an interface, or NULL for the null type and foreign objects. */
static DcType *type_from(ID3D11ShaderReflectionType *iface)
{
    return iface && iface->lpVtbl == &type_vtbl ? type_impl(iface) : NULL;
}

static HRESULT STDMETHODCALLTYPE type_get_desc(ID3D11ShaderReflectionType *iface, D3D11_SHADER_TYPE_DESC *desc)
{
    if (!desc)
        return E_FAIL;
    *desc = type_impl(iface)->desc;
    return S_OK;
}

static ID3D11ShaderReflectionType *STDMETHODCALLTYPE type_member_by_index(ID3D11ShaderReflectionType *iface,
                                                                          UINT index)
{
    DcType *type = type_impl(iface);
    return index < type->desc.Members ? &type->members[index].iface : &g_null_type;
}

static ID3D11ShaderReflectionType *STDMETHODCALLTYPE type_member_by_name(ID3D11ShaderReflectionType *iface,
                                                                         LPCSTR name)
{
    DcType *type = type_impl(iface);
    if (!name)
        return &g_null_type;
    for (UINT i = 0; i < type->desc.Members; i++)
        if (!strcmp(type->member_names[i], name))
            return &type->members[i].iface;
    return &g_null_type;
}

static LPCSTR STDMETHODCALLTYPE type_member_name(ID3D11ShaderReflectionType *iface, UINT index)
{
    DcType *type = type_impl(iface);
    return index < type->desc.Members ? type->member_names[index] : NULL;
}

static HRESULT STDMETHODCALLTYPE type_is_equal(ID3D11ShaderReflectionType *iface, ID3D11ShaderReflectionType *other)
{
    DcType *o = type_from(other);
    return o && o->identity == type_impl(iface)->identity ? S_OK : S_FALSE;
}

static ID3D11ShaderReflectionType *STDMETHODCALLTYPE type_sub(ID3D11ShaderReflectionType *iface)
{
    DcType *type = type_impl(iface);
    return type->sub ? &type->sub->iface : NULL;
}

static ID3D11ShaderReflectionType *STDMETHODCALLTYPE type_base(ID3D11ShaderReflectionType *iface)
{
    DcType *type = type_impl(iface);
    return type->base ? &type->base->iface : NULL;
}

static UINT STDMETHODCALLTYPE type_interface_count(ID3D11ShaderReflectionType *iface)
{
    return type_impl(iface)->interface_count;
}

static ID3D11ShaderReflectionType *STDMETHODCALLTYPE type_interface(ID3D11ShaderReflectionType *iface, UINT index)
{
    DcType *type = type_impl(iface);
    return index < type->interface_count ? &type->interfaces[index].iface : &g_null_type;
}

static HRESULT STDMETHODCALLTYPE type_is_of_type(ID3D11ShaderReflectionType *iface, ID3D11ShaderReflectionType *other)
{
    DcType *o = type_from(other);
    if (!o)
        return S_FALSE;
    for (DcType *type = type_impl(iface); type; type = type->base)
        if (type->identity == o->identity)
            return S_OK;
    return S_FALSE;
}

static HRESULT STDMETHODCALLTYPE type_implements(ID3D11ShaderReflectionType *iface, ID3D11ShaderReflectionType *other)
{
    DcType *o = type_from(other);
    if (!o)
        return S_FALSE;
    for (DcType *type = type_impl(iface); type; type = type->base)
        for (UINT i = 0; i < type->interface_count; i++)
            if (type->interfaces[i].identity == o->identity)
                return S_OK;
    return S_FALSE;
}

static const ID3D11ShaderReflectionTypeVtbl type_vtbl = {
    VTBL_FN(type_get_desc),  VTBL_FN(type_member_by_index), VTBL_FN(type_member_by_name),
    VTBL_FN(type_member_name), VTBL_FN(type_is_equal),      VTBL_FN(type_sub),
    VTBL_FN(type_base),      VTBL_FN(type_interface_count), VTBL_FN(type_interface),
    VTBL_FN(type_is_of_type), VTBL_FN(type_implements)};

/* ------------------------------------------------------------------ variables */

static DcVariable *variable_impl(ID3D11ShaderReflectionVariable *iface)
{
    return CONTAINING_RECORD(iface, DcVariable, iface);
}

static HRESULT STDMETHODCALLTYPE variable_get_desc(ID3D11ShaderReflectionVariable *iface,
                                                   D3D11_SHADER_VARIABLE_DESC *desc)
{
    if (!desc)
        return E_FAIL;
    *desc = variable_impl(iface)->desc;
    return S_OK;
}

static ID3D11ShaderReflectionType *STDMETHODCALLTYPE variable_type(ID3D11ShaderReflectionVariable *iface)
{
    return &variable_impl(iface)->type->iface;
}

static ID3D11ShaderReflectionConstantBuffer *STDMETHODCALLTYPE variable_buffer(ID3D11ShaderReflectionVariable *iface)
{
    return &variable_impl(iface)->buffer->iface;
}

/* an interface pointer's slots are its offset and size. */
static UINT STDMETHODCALLTYPE variable_slot(ID3D11ShaderReflectionVariable *iface, UINT index)
{
    DcVariable *v = variable_impl(iface);
    if ((v->desc.uFlags & D3D_SVF_INTERFACE_POINTER) && index < v->desc.Size)
        return v->desc.StartOffset + index;
    return 0xFFFFFFFF;
}

static const ID3D11ShaderReflectionVariableVtbl variable_vtbl = {VTBL_FN(variable_get_desc), VTBL_FN(variable_type),
                                                                 VTBL_FN(variable_buffer), VTBL_FN(variable_slot)};

/* ------------------------------------------------------------------ constant buffers */

static DcBuffer *buffer_impl(ID3D11ShaderReflectionConstantBuffer *iface)
{
    return CONTAINING_RECORD(iface, DcBuffer, iface);
}

static HRESULT STDMETHODCALLTYPE buffer_get_desc(ID3D11ShaderReflectionConstantBuffer *iface,
                                                 D3D11_SHADER_BUFFER_DESC *desc)
{
    if (!desc)
        return E_FAIL;
    *desc = buffer_impl(iface)->desc;
    return S_OK;
}

static ID3D11ShaderReflectionVariable *STDMETHODCALLTYPE buffer_variable(ID3D11ShaderReflectionConstantBuffer *iface,
                                                                         UINT index)
{
    DcBuffer *b = buffer_impl(iface);
    return index < b->desc.Variables ? &b->variables[index].iface : &g_null_variable;
}

static ID3D11ShaderReflectionVariable *STDMETHODCALLTYPE buffer_variable_by_name(
    ID3D11ShaderReflectionConstantBuffer *iface, LPCSTR name)
{
    DcBuffer *b = buffer_impl(iface);
    if (!name)
        return &g_null_variable;
    for (UINT i = 0; i < b->desc.Variables; i++)
        if (!strcmp(b->variables[i].desc.Name, name))
            return &b->variables[i].iface;
    return &g_null_variable;
}

static const ID3D11ShaderReflectionConstantBufferVtbl buffer_vtbl = {
    VTBL_FN(buffer_get_desc), VTBL_FN(buffer_variable), VTBL_FN(buffer_variable_by_name)};

/* ------------------------------------------------------------------ the reflection */

static DcReflection *impl(ID3D11ShaderReflection *iface) { return CONTAINING_RECORD(iface, DcReflection, iface); }

static HRESULT STDMETHODCALLTYPE reflection_query(ID3D11ShaderReflection *iface, REFIID riid, void **out)
{
    if (!out)
        return E_FAIL;
    if (!IsEqualGUID(riid, &IID_ID3D12ShaderReflection) && !IsEqualGUID(riid, &IID_ID3D11ShaderReflection) &&
        !IsEqualGUID(riid, &IID_IUnknown)) {
        *out = NULL;
        return E_NOINTERFACE;
    }
    *out = iface;
    iface->lpVtbl->AddRef(iface);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE reflection_add_ref(ID3D11ShaderReflection *iface)
{
    return InterlockedIncrement(&impl(iface)->refs);
}

static void reflection_free(DcReflection *r)
{
    while (r->blocks) {
        DcBlock *next = r->blocks->next;
        DcFree(r->blocks);
        r->blocks = next;
    }
    DcFree(r);
}

static ULONG STDMETHODCALLTYPE reflection_release(ID3D11ShaderReflection *iface)
{
    DcReflection *r = impl(iface);
    ULONG refs = InterlockedDecrement(&r->refs);
    if (!refs)
        reflection_free(r);
    return refs;
}

static HRESULT STDMETHODCALLTYPE reflection_get_desc(ID3D11ShaderReflection *iface, D3D11_SHADER_DESC *desc)
{
    if (!desc)
        return E_FAIL;
    *desc = impl(iface)->desc;
    return S_OK;
}

static ID3D11ShaderReflectionConstantBuffer *STDMETHODCALLTYPE reflection_buffer(ID3D11ShaderReflection *iface,
                                                                                 UINT index)
{
    DcReflection *r = impl(iface);
    return index < r->desc.ConstantBuffers ? &r->buffers[index].iface : &g_null_buffer;
}

static ID3D11ShaderReflectionConstantBuffer *STDMETHODCALLTYPE reflection_buffer_by_name(ID3D11ShaderReflection *iface,
                                                                                         LPCSTR name)
{
    DcReflection *r = impl(iface);
    if (!name)
        return &g_null_buffer;
    for (UINT i = 0; i < r->desc.ConstantBuffers; i++)
        if (!strcmp(r->buffers[i].desc.Name, name))
            return &r->buffers[i].iface;
    return &g_null_buffer;
}

/* direct3d 11 callers get the binding without the space and id of direct3d 12. */
static void copy_bind(DcReflection *r, const DcBindDesc *bind, D3D11_SHADER_INPUT_BIND_DESC *desc)
{
    memcpy(desc, bind, r->d3d12 ? sizeof(DcBindDesc) : sizeof(D3D11_SHADER_INPUT_BIND_DESC));
}

static HRESULT STDMETHODCALLTYPE reflection_bind(ID3D11ShaderReflection *iface, UINT index,
                                                 D3D11_SHADER_INPUT_BIND_DESC *desc)
{
    DcReflection *r = impl(iface);
    if (!desc || index >= r->desc.BoundResources)
        return E_INVALIDARG;
    copy_bind(r, &r->binds[index], desc);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE reflection_bind_by_name(ID3D11ShaderReflection *iface, LPCSTR name,
                                                         D3D11_SHADER_INPUT_BIND_DESC *desc)
{
    DcReflection *r = impl(iface);
    if (!name || !desc)
        return E_INVALIDARG;
    for (UINT i = 0; i < r->desc.BoundResources; i++) {
        if (!strcmp(r->binds[i].Name, name)) {
            copy_bind(r, &r->binds[i], desc);
            return S_OK;
        }
    }
    return E_INVALIDARG;
}

static HRESULT signature_desc(DcReflection *r, UINT which, UINT index, D3D11_SIGNATURE_PARAMETER_DESC *desc)
{
    UINT count = which == 0 ? r->desc.InputParameters
                            : which == 1 ? r->desc.OutputParameters : r->desc.PatchConstantParameters;
    if (!desc || index >= count)
        return E_INVALIDARG;
    *desc = r->signatures[which][index];
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE reflection_input(ID3D11ShaderReflection *iface, UINT index,
                                                  D3D11_SIGNATURE_PARAMETER_DESC *desc)
{
    return signature_desc(impl(iface), 0, index, desc);
}

static HRESULT STDMETHODCALLTYPE reflection_output(ID3D11ShaderReflection *iface, UINT index,
                                                   D3D11_SIGNATURE_PARAMETER_DESC *desc)
{
    return signature_desc(impl(iface), 1, index, desc);
}

static HRESULT STDMETHODCALLTYPE reflection_patch(ID3D11ShaderReflection *iface, UINT index,
                                                  D3D11_SIGNATURE_PARAMETER_DESC *desc)
{
    return signature_desc(impl(iface), 2, index, desc);
}

static ID3D11ShaderReflectionVariable *STDMETHODCALLTYPE reflection_variable(ID3D11ShaderReflection *iface,
                                                                             LPCSTR name)
{
    DcReflection *r = impl(iface);
    ID3D11ShaderReflectionVariable *found = &g_null_variable;
    if (!name)
        return found;
    for (UINT i = 0; i < r->desc.ConstantBuffers; i++) {
        ID3D11ShaderReflectionConstantBuffer *b = &r->buffers[i].iface;
        found = b->lpVtbl->GetVariableByName(b, name);
        if (found != &g_null_variable)
            break;
    }
    return found;
}

static UINT STDMETHODCALLTYPE reflection_mov(ID3D11ShaderReflection *iface) { return impl(iface)->mov; }
static UINT STDMETHODCALLTYPE reflection_movc(ID3D11ShaderReflection *iface) { return impl(iface)->movc; }
static UINT STDMETHODCALLTYPE reflection_conversion(ID3D11ShaderReflection *iface) { return impl(iface)->conversion; }
static UINT STDMETHODCALLTYPE reflection_bitwise(ID3D11ShaderReflection *iface) { return impl(iface)->bitwise; }

static D3D_PRIMITIVE STDMETHODCALLTYPE reflection_gs_input(ID3D11ShaderReflection *iface)
{
    return impl(iface)->gs_input_primitive;
}

static BOOL STDMETHODCALLTYPE reflection_sample_frequency(ID3D11ShaderReflection *iface)
{
    return impl(iface)->sample_frequency;
}

static UINT STDMETHODCALLTYPE reflection_interface_slots(ID3D11ShaderReflection *iface)
{
    return impl(iface)->interface_slots;
}

/*
 * the feature level of each shader model the compiler knows, in its order:
 * a version found several times takes the last entry, as GetShaderModelByVersion
 * searches from the end. 0xFFFFFFFF stands for direct3d 9 only.
 */
static const struct {
    UINT version;
    UINT level;
} SHADER_MODELS[] = {
    {0x43530000, 0xA000}, {0x43530400, 0xA000}, {0x43530401, 0xA100}, {0x43530500, 0xB000}, {0x43530501, 0xB000},
    {0x44530000, 0xB000}, {0x44530500, 0xB000}, {0x44530501, 0xB000}, {0x46580200, 0xFFFFFFFF},
    {0x46580400, 0xA000}, {0x46580401, 0xA100}, {0x46580500, 0xB000}, {0x47530000, 0xA000}, {0x47530400, 0xA000},
    {0x47530401, 0xA100}, {0x47530500, 0xB000}, {0x47530501, 0xB000}, {0x48530000, 0xB000}, {0x48530500, 0xB000},
    {0x48530501, 0xB000}, {0x4C460000, 0xA000}, {0x4C460400, 0xA000}, {0x4C460401, 0xA100}, {0x4C560200, 0x9100},
    {0x4C560200, 0x9100}, {0x4C500200, 0x9100}, {0x4C560201, 0x9300}, {0x4C560201, 0x9300}, {0x4C500201, 0x9300},
    {0x4C460500, 0xB000}, {0xFFFF0000, 0xFFFFFFFF}, {0xFFFF0100, 0xFFFFFFFF}, {0xFFFF0101, 0xFFFFFFFF},
    {0xFFFF0102, 0xFFFFFFFF}, {0xFFFF0103, 0xFFFFFFFF}, {0xFFFF0104, 0xFFFFFFFF}, {0xFFFF0200, 0xFFFFFFFF},
    {0xFFFF0202, 0xFFFFFFFF}, {0xFFFF0203, 0xFFFFFFFF}, {0xFFFF02FF, 0xFFFFFFFF}, {0xFFFF0201, 0xFFFFFFFF},
    {0xFFFF0300, 0xFFFFFFFF}, {0xFFFF03FF, 0xFFFFFFFF}, {0xFFFF0100, 0xFFFFFFFF}, {0xFFFF0101, 0xFFFFFFFF},
    {0xFFFF0102, 0xFFFFFFFF}, {0xFFFF0103, 0xFFFFFFFF}, {0xFFFF0104, 0xFFFFFFFF}, {0xFFFF0200, 0xFFFFFFFF},
    {0xFFFF0202, 0xFFFFFFFF}, {0xFFFF0203, 0xFFFFFFFF}, {0xFFFF02FF, 0xFFFFFFFF}, {0xFFFF0201, 0xFFFFFFFF},
    {0xFFFF0300, 0xFFFFFFFF}, {0xFFFF03FF, 0xFFFFFFFF}, {0xFFFF0400, 0xA000}, {0xFFFF0201, 0x9100},
    {0xFFFF0204, 0x9300}, {0xFFFF0265, 0x9000}, {0xFFFF0401, 0xA100}, {0xFFFF0500, 0xB000}, {0xFFFF0501, 0xB000},
    {0x52530100, 0xFFFFFFFF}, {0x52530101, 0xFFFFFFFF}, {0x54580000, 0xFFFFFFFF}, {0x54580100, 0xFFFFFFFF},
    {0xFFFE0000, 0xFFFFFFFF}, {0xFFFE0100, 0xFFFFFFFF}, {0xFFFE0101, 0xFFFFFFFF}, {0xFFFE0200, 0xFFFFFFFF},
    {0xFFFE0202, 0xFFFFFFFF}, {0xFFFE02FF, 0xFFFFFFFF}, {0xFFFE0201, 0xFFFFFFFF}, {0xFFFE0300, 0xFFFFFFFF},
    {0xFFFE03FF, 0xFFFFFFFF}, {0xFFFE0100, 0xFFFFFFFF}, {0xFFFE0101, 0xFFFFFFFF}, {0xFFFE0200, 0xFFFFFFFF},
    {0xFFFE0202, 0xFFFFFFFF}, {0xFFFE02FF, 0xFFFFFFFF}, {0xFFFE0201, 0xFFFFFFFF}, {0xFFFE0300, 0xFFFFFFFF},
    {0xFFFE03FF, 0xFFFFFFFF}, {0xFFFE0400, 0xA000}, {0xFFFE0201, 0x9100}, {0xFFFE0203, 0x9300},
    {0xFFFE0265, 0x9000}, {0xFFFE0401, 0xA100}, {0xFFFE0500, 0xB000}, {0xFFFE0501, 0xB000},
};

static HRESULT STDMETHODCALLTYPE reflection_min_level(ID3D11ShaderReflection *iface, D3D_FEATURE_LEVEL *level)
{
    DcReflection *r = impl(iface);
    if (!level)
        return E_INVALIDARG;
    UINT version = r->legacy_version ? r->legacy_version : r->internal_version;
    int i = (int)(sizeof(SHADER_MODELS) / sizeof(SHADER_MODELS[0]));
    while (--i >= 0 && SHADER_MODELS[i].version != version)
        ;
    if (i < 0)
        return E_INVALIDARG;
    int found = (int)SHADER_MODELS[i].level;
    *level = (D3D_FEATURE_LEVEL)found;
    if (found == -1)
        return S_FALSE;
    /* uavs at every stage need 11_0, 64 uav slots 11_1. */
    if ((r->requires & 4) && found < D3D_FEATURE_LEVEL_11_0)
        found = D3D_FEATURE_LEVEL_11_0;
    if ((r->requires & 8) && found < D3D_FEATURE_LEVEL_11_1)
        found = D3D_FEATURE_LEVEL_11_1;
    *level = (D3D_FEATURE_LEVEL)found;
    return S_OK;
}

static UINT STDMETHODCALLTYPE reflection_thread_group(ID3D11ShaderReflection *iface, UINT *x, UINT *y, UINT *z)
{
    DcReflection *r = impl(iface);
    if (x)
        *x = r->thread_group[0];
    if (y)
        *y = r->thread_group[1];
    if (z)
        *z = r->thread_group[2];
    return r->thread_group[0] * r->thread_group[1] * r->thread_group[2];
}

static UINT64 STDMETHODCALLTYPE reflection_requires(ID3D11ShaderReflection *iface) { return impl(iface)->requires; }

static const ID3D11ShaderReflectionVtbl reflection_vtbl = {
    VTBL_FN(reflection_query),          VTBL_FN(reflection_add_ref),         VTBL_FN(reflection_release),
    VTBL_FN(reflection_get_desc),       VTBL_FN(reflection_buffer),          VTBL_FN(reflection_buffer_by_name),
    VTBL_FN(reflection_bind),           VTBL_FN(reflection_input),           VTBL_FN(reflection_output),
    VTBL_FN(reflection_patch),          VTBL_FN(reflection_variable),        VTBL_FN(reflection_bind_by_name),
    VTBL_FN(reflection_mov),            VTBL_FN(reflection_movc),            VTBL_FN(reflection_conversion),
    VTBL_FN(reflection_bitwise),        VTBL_FN(reflection_gs_input),        VTBL_FN(reflection_sample_frequency),
    VTBL_FN(reflection_interface_slots), VTBL_FN(reflection_min_level),      VTBL_FN(reflection_thread_group),
    VTBL_FN(reflection_requires)};

ID3D11ShaderReflection *DcReflectionInterface(DcReflection *r) { return &r->iface; }

/* ------------------------------------------------------------------ reading the chunks */

/* a string of the bytecode, which must end before the end of the container. */
static HRESULT copy_string(DcReflection *r, const BYTE *base, UINT offset, const BYTE *end, LPCSTR *out)
{
    const BYTE *s = base + offset;
    if (s < base || s >= end)
        return E_FAIL;
    const BYTE *p = s;
    while (*p)
        if (++p >= end)
            return E_FAIL;
    char *copy = zalloc(r, p - s + 1);
    if (!copy)
        return E_OUTOFMEMORY;
    memcpy(copy, s, p - s + 1);
    *out = copy;
    return S_OK;
}

/* the header of an RDEF chunk with the strides of its entries, which shader model 4 leaves out. */
typedef struct RdefHeader {
    UINT buffer_count, buffer_offset, bind_count, bind_offset, target, flags, creator;
    UINT buffer_stride, bind_stride, variable_stride, type_stride, member_stride, interface_slots;
} RdefHeader;

typedef struct RdefContext {
    DcReflection *r;
    const BYTE *chunk;
    const BYTE *end;
    RdefHeader h;
} RdefContext;

/* a range of the bytecode after the chunk's start and up to the end of the container. */
static BOOL in_range(const RdefContext *c, const BYTE *p, UINT64 size)
{
    return p > c->chunk && p < c->end && size && (UINT64)(c->end - p) >= size;
}

static HRESULT read_type(RdefContext *c, DcType *type, UINT offset, UINT depth)
{
    const BYTE *p = c->chunk + offset;
    if (depth > 64 || !in_range(c, p, c->h.type_stride))
        return E_FAIL;
    type->iface.lpVtbl = &type_vtbl;
    type->identity = offset;
    type->desc.Class = read16(p);
    type->desc.Type = read16(p + 2);
    type->desc.Rows = read16(p + 4);
    type->desc.Columns = read16(p + 6);
    type->desc.Elements = read16(p + 8);
    type->desc.Members = read16(p + 10);
    UINT count = type->desc.Members;
    HRESULT hr;
    if (count) {
        const BYTE *members = c->chunk + read32(p + 12);
        if (members > c->end || (UINT64)c->h.member_stride * count > (UINT64)(c->end - members))
            return E_FAIL;
        type->members = zalloc(c->r, count * sizeof(DcType));
        type->member_names = zalloc(c->r, count * sizeof(char *));
        if (!type->members || !type->member_names)
            return E_OUTOFMEMORY;
        for (UINT i = 0; i < count; i++) {
            const BYTE *m = members + c->h.member_stride * i;
            if (FAILED(hr = copy_string(c->r, c->chunk, read32(m), c->end, &type->member_names[i])) ||
                FAILED(hr = read_type(c, &type->members[i], read32(m + 4), depth + 1)))
                return hr;
            type->members[i].desc.Offset = read32(m + 8);
        }
    }
    if (c->h.type_stride < 36)
        return S_OK;
    UINT sub = read32(p + 16), base = read32(p + 20), interfaces = read32(p + 24), names = read32(p + 32);
    if (sub) {
        if (!(type->sub = zalloc(c->r, sizeof(DcType))))
            return E_OUTOFMEMORY;
        if (FAILED(hr = read_type(c, type->sub, sub, depth + 1)))
            return hr;
    }
    if (base) {
        if (!(type->base = zalloc(c->r, sizeof(DcType))))
            return E_OUTOFMEMORY;
        if (FAILED(hr = read_type(c, type->base, base, depth + 1)))
            return hr;
    }
    if (interfaces) {
        const BYTE *list = c->chunk + read32(p + 28);
        if (list < c->chunk || list > c->end || (UINT64)interfaces * 4 > (UINT64)(c->end - list))
            return E_FAIL;
        if (!(type->interfaces = zalloc(c->r, interfaces * sizeof(DcType))))
            return E_OUTOFMEMORY;
        type->interface_count = interfaces;
        for (UINT i = 0; i < interfaces; i++)
            if (FAILED(hr = read_type(c, &type->interfaces[i], read32(list + i * 4), depth + 1)))
                return hr;
    }
    if (names)
        return copy_string(c->r, c->chunk, names, c->end, &type->desc.Name);
    return S_OK;
}

static HRESULT read_variable(RdefContext *c, DcBuffer *buffer, DcVariable *v, const BYTE *p)
{
    if (!in_range(c, p, c->h.variable_stride))
        return E_FAIL;
    v->iface.lpVtbl = &variable_vtbl;
    v->buffer = buffer;
    HRESULT hr = copy_string(c->r, c->chunk, read32(p), c->end, &v->desc.Name);
    if (FAILED(hr))
        return hr;
    v->desc.StartOffset = read32(p + 4);
    v->desc.Size = read32(p + 8);
    v->desc.uFlags = read32(p + 12) & 0xF;
    UINT defaults = read32(p + 20);
    if (v->desc.Size && defaults) {
        const BYTE *src = c->chunk + defaults;
        if (src > c->end || v->desc.Size > (UINT64)(c->end - src))
            return E_FAIL;
        if (!(v->desc.DefaultValue = zalloc(c->r, v->desc.Size)))
            return E_OUTOFMEMORY;
        memcpy(v->desc.DefaultValue, src, v->desc.Size);
    }
    if (c->h.variable_stride < 40) {
        v->desc.StartTexture = 0xFFFFFFFF;
        v->desc.TextureSize = 0;
        v->desc.StartSampler = 0xFFFFFFFF;
        v->desc.SamplerSize = 0;
    } else {
        v->desc.StartTexture = read32(p + 24);
        v->desc.TextureSize = read32(p + 28);
        v->desc.StartSampler = read32(p + 32);
        v->desc.SamplerSize = read32(p + 36);
    }
    if (!(v->type = zalloc(c->r, sizeof(DcType))))
        return E_OUTOFMEMORY;
    return read_type(c, v->type, read32(p + 16), 0);
}

static HRESULT read_buffer(RdefContext *c, DcBuffer *b, const BYTE *p)
{
    if (!in_range(c, p, c->h.buffer_stride))
        return E_FAIL;
    b->iface.lpVtbl = &buffer_vtbl;
    HRESULT hr = copy_string(c->r, c->chunk, read32(p), c->end, &b->desc.Name);
    if (FAILED(hr))
        return hr;
    b->desc.Type = read32(p + 20);
    b->desc.Size = read32(p + 12);
    b->desc.Variables = read32(p + 4);
    b->desc.uFlags = read32(p + 16) & 1;
    if (!b->desc.Variables)
        return S_OK;
    if (!(b->variables = zalloc(c->r, b->desc.Variables * sizeof(DcVariable))))
        return E_OUTOFMEMORY;
    const BYTE *variables = c->chunk + read32(p + 8);
    for (UINT i = 0; i < b->desc.Variables; i++)
        if (FAILED(hr = read_variable(c, b, &b->variables[i], variables + (UINT64)c->h.variable_stride * i)))
            return hr;
    return S_OK;
}

static HRESULT read_rdef(DcReflection *r, const BYTE *chunk, UINT size, const BYTE *end)
{
    if (size < 28 || size > (UINT64)(end - chunk))
        return E_FAIL;
    RdefContext c = {r, chunk, end};
    RdefHeader *h = &c.h;
    h->buffer_count = read32(chunk);
    h->buffer_offset = read32(chunk + 4);
    h->bind_count = read32(chunk + 8);
    h->bind_offset = read32(chunk + 12);
    h->target = read32(chunk + 16);
    h->flags = read32(chunk + 20);
    h->creator = read32(chunk + 24);
    UINT tag = size >= 60 ? read32(chunk + 28) : 0;
    if ((tag == DC_TAG('R', 'D', '1', '1') || tag == 0x25441313) && read32(chunk + 32) == 60) {
        h->buffer_stride = read32(chunk + 36);
        h->bind_stride = read32(chunk + 40);
        h->variable_stride = read32(chunk + 44);
        h->type_stride = read32(chunk + 48);
        h->member_stride = read32(chunk + 52);
        h->interface_slots = read32(chunk + 56);
    } else {
        h->buffer_stride = 24;
        h->bind_stride = 32;
        h->variable_stride = 24;
        h->type_stride = 16;
        h->member_stride = 12;
        h->interface_slots = 0;
    }
    r->desc.Flags = h->flags;
    HRESULT hr = copy_string(r, chunk, h->creator, end, &r->desc.Creator);
    if (FAILED(hr))
        return hr;
    r->interface_slots = h->interface_slots;
    if (h->buffer_count) {
        r->desc.ConstantBuffers = h->buffer_count;
        if (!(r->buffers = zalloc(r, (SIZE_T)h->buffer_count * sizeof(DcBuffer))))
            return E_OUTOFMEMORY;
        const BYTE *buffers = chunk + h->buffer_offset;
        for (UINT i = 0; i < h->buffer_count; i++)
            if (FAILED(hr = read_buffer(&c, &r->buffers[i], buffers + (UINT64)h->buffer_stride * i)))
                return hr;
    }
    if (h->bind_count) {
        r->desc.BoundResources = h->bind_count;
        const BYTE *binds = chunk + h->bind_offset;
        if (binds > end || (UINT64)h->bind_stride * h->bind_count > (UINT64)(end - binds))
            return E_FAIL;
        if (!(r->binds = zalloc(r, (SIZE_T)h->bind_count * sizeof(DcBindDesc))))
            return E_OUTOFMEMORY;
        for (UINT i = 0; i < h->bind_count; i++) {
            const BYTE *p = binds + h->bind_stride * i;
            DcBindDesc *b = &r->binds[i];
            if (FAILED(hr = copy_string(r, chunk, read32(p), end, &b->Name)))
                return hr;
            b->Type = read32(p + 4);
            b->BindPoint = read32(p + 20);
            b->BindCount = read32(p + 24);
            b->uFlags = read32(p + 28) & 0x1F;
            b->ReturnType = read32(p + 8);
            b->Dimension = read32(p + 12);
            b->NumSamples = read32(p + 16);
            /* shader model 5.1 adds the register space and the id. */
            b->Space = h->bind_stride >= 40 ? read32(p + 32) : 0;
            b->uID = h->bind_stride >= 40 ? read32(p + 36) : b->BindPoint;
        }
    }
    return S_OK;
}

static const struct {
    const char *name;
    D3D_NAME value;
} PIXEL_OUTPUTS[] = {
    {"SV_TARGET", D3D_NAME_TARGET},
    {"SV_DEPTH", D3D_NAME_DEPTH},
    {"SV_DEPTHGREATEREQUAL", D3D_NAME_DEPTH_GREATER_EQUAL},
    {"SV_DEPTHLESSEQUAL", D3D_NAME_DEPTH_LESS_EQUAL},
    {"SV_COVERAGE", D3D_NAME_COVERAGE},
    {"SV_INNERCOVERAGE", D3D_NAME_INNER_COVERAGE},
    {"SV_STENCILREF", D3D_NAME_STENCIL_REF},
};

/* a signature of 24, 28 or 32 byte entries, the last two leading with the stream. */
static HRESULT read_signature(DcReflection *r, const BYTE *chunk, const BYTE *end, UINT stride, UINT which,
                              UINT *count_out)
{
    if (!chunk) {
        *count_out = 0;
        return S_FALSE;
    }
    if (end - chunk < 8)
        return E_FAIL;
    UINT count = read32(chunk);
    if (!count)
        return S_OK;
    *count_out = count;
    const BYTE *entries = chunk + read32(chunk + 4);
    if (entries > end || (UINT64)stride * count > (UINT64)(end - entries))
        return E_FAIL;
    D3D11_SIGNATURE_PARAMETER_DESC *descs = zalloc(r, (SIZE_T)count * sizeof(*descs));
    if (!descs)
        return E_OUTOFMEMORY;
    r->signatures[which] = descs;
    for (UINT i = 0; i < count; i++) {
        const BYTE *p = entries + stride * i;
        D3D11_SIGNATURE_PARAMETER_DESC *d = &descs[i];
        UINT stream = 0;
        if (stride != 24) {
            stream = read32(p);
            p += 4;
        }
        HRESULT hr = copy_string(r, chunk, read32(p), end, &d->SemanticName);
        if (FAILED(hr))
            return hr;
        d->SemanticIndex = read32(p + 4);
        d->SystemValueType = read32(p + 8);
        d->ComponentType = read32(p + 12);
        d->Register = read32(p + 16);
        d->Mask = p[20];
        d->ReadWriteMask = p[21];
        d->Stream = stream;
        d->MinPrecision = stride == 32 ? read32(p + 24) : 0;
        if (d->SystemValueType == D3D_NAME_UNDEFINED && r->program == D3D11_SHVER_PIXEL_SHADER) {
            for (UINT j = 0; j < sizeof(PIXEL_OUTPUTS) / sizeof(PIXEL_OUTPUTS[0]); j++)
                if (!_stricmp(d->SemanticName, PIXEL_OUTPUTS[j].name))
                    d->SystemValueType = PIXEL_OUTPUTS[j].value;
        }
    }
    return S_OK;
}

/* the version d3dcompiler's shader model table knows the shader by. */
static UINT internal_version(UINT token)
{
    static const UINT PREFIX[] = {0xFFFF, 0xFFFE, 0x4753, 0x4853, 0x4453, 0x4353};
    UINT type = token >> 16, prefix;
    if (type < 6)
        prefix = PREFIX[type];
    else if (type == 0xFFF0)
        prefix = 0x4C46;
    else
        return 0;
    return prefix << 16 | ((token >> 4) & 0xF) << 8 | (token & 0xF);
}

/* the version token and the declarations before the first instruction. */
static HRESULT read_code(DcReflection *r, const BYTE *chunk, UINT size)
{
    if (size < 4)
        return E_FAIL;
    UINT token = read32(chunk);
    r->program = token >> 16;
    r->desc.Version = (token & 0xFF) | r->program << 16;
    r->internal_version = internal_version(token);
    UINT words = size >= 8 ? read32(chunk + 4) : 0;
    if (words > size / 4)
        words = size / 4;
    for (UINT at = 2; at < words;) {
        UINT t = read32(chunk + at * 4), opcode = t & 0x7FF, length = (t >> 24) & 0x7F;
        if (opcode == OP_CUSTOMDATA)
            length = at + 1 < words ? read32(chunk + at * 4 + 4) : 0;
        else if (!DcIsDeclaration(opcode))
            break;
        if (opcode == OP_DCL_GLOBAL_FLAGS) {
            if (t & (1 << 12))
                r->requires |= D3D_SHADER_REQUIRES_DOUBLES;
            if (t & (1 << 13))
                r->requires |= D3D_SHADER_REQUIRES_EARLY_DEPTH_STENCIL;
            if (t & (1 << 16))
                r->requires |= D3D_SHADER_REQUIRES_MINIMUM_PRECISION;
            if (t & (1 << 17))
                r->requires |= D3D_SHADER_REQUIRES_11_1_DOUBLE_EXTENSIONS;
            if (t & (1 << 18))
                r->requires |= D3D_SHADER_REQUIRES_11_1_SHADER_EXTENSIONS;
        } else if (opcode == OP_DCL_THREAD_GROUP && at + 3 < words) {
            for (UINT i = 0; i < 3; i++)
                r->thread_group[i] = read32(chunk + (at + 1 + i) * 4);
        }
        if (!length)
            break;
        at += length;
    }
    return S_OK;
}

/* the legacy shader of feature level 9 gives the shader model its feature level. */
static HRESULT read_legacy(DcReflection *r, const BYTE *chunk, UINT size)
{
    if (size < 36 || read32(chunk + 8) < 4)
        return E_FAIL;
    UINT offset = read32(chunk + 12);
    if ((UINT64)offset + 4 > size)
        return E_FAIL;
    UINT version = read32(chunk + offset);
    switch (version) {
    case 0xFFFE0200:
        r->legacy_version = 0xFFFE0201;
        break;
    case 0xFFFE0201:
        r->legacy_version = 0xFFFE0203;
        break;
    case 0xFFFF0200:
        r->legacy_version = 0xFFFF0201;
        break;
    case 0xFFFF0201:
        r->legacy_version = 0xFFFF0204;
        break;
    default:
        r->legacy_version = version;
        break;
    }
    return S_OK;
}

/* the instruction counts of the STAT chunk, which comes in 28, 29 and 37 words. */
static void read_stat(DcReflection *r, const BYTE *chunk, UINT size)
{
    UINT s[37] = {0};
    for (UINT i = 0; i < size / 4 && i < 37; i++)
        s[i] = read32(chunk + i * 4);
    D3D11_SHADER_DESC *d = &r->desc;
    d->InstructionCount = s[0];
    d->TempRegisterCount = s[1];
    d->DefCount = s[2];
    d->DclCount = s[3];
    d->FloatInstructionCount = s[4];
    d->IntInstructionCount = s[5];
    d->UintInstructionCount = s[6];
    d->StaticFlowControlCount = s[7];
    d->DynamicFlowControlCount = s[8];
    d->MacroInstructionCount = s[9];
    d->TempArrayCount = s[10];
    d->ArrayInstructionCount = s[11];
    d->CutInstructionCount = s[12];
    d->EmitInstructionCount = s[13];
    d->TextureNormalInstructions = s[14];
    d->TextureLoadInstructions = s[15];
    d->TextureCompInstructions = s[16];
    d->TextureBiasInstructions = s[17];
    d->TextureGradientInstructions = s[18];
    r->mov = s[19];
    r->movc = s[20];
    r->conversion = s[21];
    r->bitwise = s[22];
    d->InputPrimitive = s[23];
    r->gs_input_primitive = s[23];
    d->GSOutputTopology = s[24];
    d->GSMaxOutputVertexCount = s[25];
    r->sample_frequency = s[28];
    d->cGSInstanceCount = s[29];
    d->cControlPoints = s[30];
    d->HSOutputPrimitive = s[31];
    d->HSPartitioning = s[32];
    d->TessellatorDomain = s[33];
    d->cBarrierInstructions = s[34];
    d->cInterlockedInstructions = s[35];
    d->cTextureStoreInstructions = s[36];
}

static HRESULT read_all(DcReflection *r, const DcContainer *c)
{
    const BYTE *end = c->base + c->size;
    HRESULT hr;
    int index = DcFindChunk(c, DC_TAG('A', 'o', 'n', '9'));
    if (index >= 0) {
        DcChunk chunk = DcGetChunk(c, index);
        if (FAILED(hr = read_legacy(r, chunk.data, chunk.size)))
            return hr;
    }
    index = DcFindChunk(c, DC_TAG('S', 'H', 'E', 'X'));
    if (index < 0)
        index = DcFindChunk(c, DC_TAG('S', 'H', 'D', 'R'));
    if (index < 0)
        return E_FAIL;
    DcChunk code = DcGetChunk(c, index);
    if (FAILED(hr = read_code(r, code.data, (UINT)(end - code.data) < code.size ? (UINT)(end - code.data) : code.size)))
        return hr;
    if ((index = DcFindChunk(c, DC_TAG('S', 'F', 'I', '0'))) >= 0) {
        DcChunk chunk = DcGetChunk(c, index);
        if (chunk.size > 8)
            return E_FAIL;
        BYTE bits[8] = {0};
        memcpy(bits, chunk.data, chunk.size);
        r->requires |= read16(bits) & 0x3FFD;
    }
    if ((index = DcFindChunk(c, DC_TAG('R', 'D', 'E', 'F'))) >= 0) {
        DcChunk chunk = DcGetChunk(c, index);
        if (FAILED(hr = read_rdef(r, chunk.data, chunk.size, end)))
            return hr;
    }
    if ((index = DcFindChunk(c, DC_TAG('S', 'T', 'A', 'T'))) >= 0) {
        DcChunk chunk = DcGetChunk(c, index);
        if (chunk.size != 112 && chunk.size != 116 && chunk.size != 148)
            return E_FAIL;
        read_stat(r, chunk.data, chunk.size);
    }
    static const struct {
        UINT tag;
        UINT stride;
    } INPUTS[] = {{DC_TAG('I', 'S', 'G', 'N'), 24}, {DC_TAG('I', 'S', 'G', '1'), 32}},
      OUTPUTS[] = {{DC_TAG('O', 'S', 'G', 'N'), 24}, {DC_TAG('O', 'S', 'G', '5'), 28}, {DC_TAG('O', 'S', 'G', '1'), 32}},
      PATCHES[] = {{DC_TAG('P', 'C', 'S', 'G'), 24}, {DC_TAG('P', 'S', 'G', '1'), 32}};
    UINT stride = 0;
    index = -1;
    for (UINT i = 0; i < 2 && index < 0; i++)
        if ((index = DcFindChunk(c, INPUTS[i].tag)) >= 0)
            stride = INPUTS[i].stride;
    if (index < 0)
        return E_FAIL;
    if (FAILED(hr = read_signature(r, DcGetChunk(c, index).data, end, stride, 0, &r->desc.InputParameters)))
        return hr;
    index = -1;
    for (UINT i = 0; i < 3 && index < 0; i++)
        if ((index = DcFindChunk(c, OUTPUTS[i].tag)) >= 0)
            stride = OUTPUTS[i].stride;
    if (index < 0)
        return E_FAIL;
    if (FAILED(hr = read_signature(r, DcGetChunk(c, index).data, end, stride, 1, &r->desc.OutputParameters)))
        return hr;
    index = -1;
    for (UINT i = 0; i < 2 && index < 0; i++)
        if ((index = DcFindChunk(c, PATCHES[i].tag)) >= 0)
            stride = PATCHES[i].stride;
    return read_signature(r, index >= 0 ? DcGetChunk(c, index).data : NULL, end, stride, 2,
                          &r->desc.PatchConstantParameters);
}

HRESULT DcReflectionCreate(const DcContainer *c, BOOL d3d12, DcReflection **out)
{
    DcReflection *r = DcAlloc(sizeof(*r));
    if (!r)
        return E_OUTOFMEMORY;
    memset(r, 0, sizeof(*r));
    r->iface.lpVtbl = &reflection_vtbl;
    r->refs = 1;
    r->d3d12 = d3d12;
    /* whatever the chunks hold that cannot be read is an invalid argument. */
    if (FAILED(read_all(r, c))) {
        reflection_free(r);
        return E_INVALIDARG;
    }
    *out = r;
    return S_OK;
}

D3DCOMPILERAPI HRESULT WINAPI D3DReflect(LPCVOID data, SIZE_T size, REFIID riid, void **reflector)
{
    DcContainer c;
    HRESULT hr = DcOpenContainer(data, size, &c);
    if (FAILED(hr))
        return hr;
    BOOL d3d12 = riid && IsEqualGUID(riid, &IID_ID3D12ShaderReflection);
    if (!riid || (!d3d12 && !IsEqualGUID(riid, &IID_ID3D11ShaderReflection)))
        return E_INVALIDARG;
    DcReflection *r;
    if (FAILED(hr = DcReflectionCreate(&c, d3d12, &r)))
        return hr;
    hr = r->iface.lpVtbl->QueryInterface(&r->iface, riid, reflector);
    r->iface.lpVtbl->Release(&r->iface);
    return SUCCEEDED(hr) ? S_OK : hr;
}

D3DCOMPILERAPI HRESULT WINAPI D3DReflectLibrary(LPCVOID data, SIZE_T size, REFIID riid, void **reflector)
{
    DcContainer c;
    HRESULT hr = DcOpenContainer(data, size, &c);
    if (FAILED(hr))
        return hr;
    if (reflector)
        *reflector = NULL;
    return E_NOTIMPL;
}
