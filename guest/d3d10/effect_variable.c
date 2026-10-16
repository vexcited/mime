/*
 * the variables and types of effects: one structure for every variable,
 * global, member, element or annotation, with the vtable of the interface
 * its type answers to. numbers live in their constant buffer's bytes,
 * converted between floats, integers and booleans as d3d10.dll does it:
 * true is -1, and -1.0f as a float.
 */
#include "effect_private.h"

#define VTBL_FN(f) ((void *)(f))

static FxVariable *impl(void *iface) { return CONTAINING_RECORD(iface, FxVariable, iface); }

/* ------------------------------------------------------------------ invalid objects */

static BOOL STDMETHODCALLTYPE fail_valid(void *iface) { return FALSE; }

static HRESULT STDMETHODCALLTYPE fail1(void *iface, UINT_PTR a) { return E_FAIL; }

static HRESULT STDMETHODCALLTYPE fail2(void *iface, UINT_PTR a, UINT_PTR b) { return E_FAIL; }

static HRESULT STDMETHODCALLTYPE fail3(void *iface, UINT_PTR a, UINT b, UINT c) { return E_FAIL; }

static LPCSTR STDMETHODCALLTYPE fail_name(void *iface, UINT index) { return nullptr; }

static ID3D10EffectType *STDMETHODCALLTYPE invalid_type_get(void *iface, UINT_PTR which) { return &FxInvalidType; }

static const ID3D10EffectTypeVtbl invalid_type_vtbl = {
    VTBL_FN(fail_valid),       VTBL_FN(fail1),     VTBL_FN(invalid_type_get), VTBL_FN(invalid_type_get),
    VTBL_FN(invalid_type_get), VTBL_FN(fail_name), VTBL_FN(fail_name),
};

ID3D10EffectType FxInvalidType = {&invalid_type_vtbl};

/* the invalid variable of each interface, which As* hands out for what does not fit. */
enum {
    I_SCALAR,
    I_VECTOR,
    I_MATRIX,
    I_STRING,
    I_SRV,
    I_RTV,
    I_DSV,
    I_BUFFER,
    I_SHADER,
    I_BLEND,
    I_DEPTHSTENCIL,
    I_RASTERIZER,
    I_SAMPLER,
    I_COUNT
};

static FxVariable invalid[I_COUNT];

static ID3D10EffectType *STDMETHODCALLTYPE invalid_get_type(void *iface) { return &FxInvalidType; }

static ID3D10EffectVariable *STDMETHODCALLTYPE invalid_get(void *iface, UINT_PTR which)
{
    return &invalid[I_SCALAR].iface;
}

static void *STDMETHODCALLTYPE invalid_parent(void *iface) { return &invalid[I_BUFFER].iface; }

#define INVALID_AS(i)                                                                                                  \
    static void *STDMETHODCALLTYPE invalid_as_##i(void *iface) { return &invalid[i].iface; }
INVALID_AS(I_SCALAR)
INVALID_AS(I_VECTOR)
INVALID_AS(I_MATRIX)
INVALID_AS(I_STRING)
INVALID_AS(I_SRV)
INVALID_AS(I_RTV)
INVALID_AS(I_DSV)
INVALID_AS(I_BUFFER)
INVALID_AS(I_SHADER)
INVALID_AS(I_BLEND)
INVALID_AS(I_DEPTHSTENCIL)
INVALID_AS(I_RASTERIZER)
INVALID_AS(I_SAMPLER)

#define INVALID_BASE                                                                                                   \
    VTBL_FN(fail_valid), VTBL_FN(invalid_get_type), VTBL_FN(fail1), VTBL_FN(invalid_get), VTBL_FN(invalid_get),        \
        VTBL_FN(invalid_get), VTBL_FN(invalid_get), VTBL_FN(invalid_get), VTBL_FN(invalid_get),                        \
        VTBL_FN(invalid_parent), VTBL_FN(invalid_as_I_SCALAR), VTBL_FN(invalid_as_I_VECTOR),                           \
        VTBL_FN(invalid_as_I_MATRIX), VTBL_FN(invalid_as_I_STRING), VTBL_FN(invalid_as_I_SRV),                         \
        VTBL_FN(invalid_as_I_RTV), VTBL_FN(invalid_as_I_DSV), VTBL_FN(invalid_as_I_BUFFER),                            \
        VTBL_FN(invalid_as_I_SHADER), VTBL_FN(invalid_as_I_BLEND), VTBL_FN(invalid_as_I_DEPTHSTENCIL),                 \
        VTBL_FN(invalid_as_I_RASTERIZER), VTBL_FN(invalid_as_I_SAMPLER), VTBL_FN(fail3), VTBL_FN(fail3)

/* the methods past the variable's: those of one value, then those of arrays. */
#define F1 VTBL_FN(fail1)
#define F2 VTBL_FN(fail2)
#define F3 VTBL_FN(fail3)

static const void *const invalid_scalar_vtbl[] = {INVALID_BASE, F1, F1, F3, F3, F1, F1, F3, F3, F1, F1, F3, F3};
static const void *const invalid_vector_vtbl[] = {INVALID_BASE, F1, F1, F1, F1, F1, F1, F3, F3, F3, F3, F3, F3};
static const void *const invalid_matrix_vtbl[] = {INVALID_BASE, F1, F1, F3, F3, F1, F1, F3, F3};
static const void *const invalid_string_vtbl[] = {INVALID_BASE, F1, F3};
static const void *const invalid_view_vtbl[] = {INVALID_BASE, F1, F1, F3, F3};
static const void *const invalid_buffer_vtbl[] = {INVALID_BASE, F1, F1, F1, F1};
static const void *const invalid_shader_vtbl[] = {INVALID_BASE, F2, F2, F2, F2, F3, F3};
static const void *const invalid_state_vtbl[] = {INVALID_BASE, F2, F2};

FxVariable FxInvalidVariable, FxInvalidBuffer;
FxVariable FxAnonymousNull[3];

/* ------------------------------------------------------------------ types */

static FxType *type_impl(ID3D10EffectType *iface) { return CONTAINING_RECORD(iface, FxType, iface); }

static BOOL STDMETHODCALLTYPE type_is_valid(ID3D10EffectType *iface) { return TRUE; }

/* the size of the type without the padding after its last element. */
static UINT unpacked_size(const FxType *t)
{
    if (t->buffer_type)
        return t->size;
    if (!t->elements)
        return t->size;
    return t->size - (t->elements - 1) * t->stride;
}

static HRESULT STDMETHODCALLTYPE type_get_desc(ID3D10EffectType *iface, D3D10_EFFECT_TYPE_DESC *desc)
{
    static const D3D10_SHADER_VARIABLE_TYPE OBJECTS[23] = {
        0,
        D3D_SVT_STRING,
        D3D_SVT_BLEND,
        D3D_SVT_DEPTHSTENCIL,
        D3D_SVT_RASTERIZER,
        D3D_SVT_PIXELSHADER,
        D3D_SVT_VERTEXSHADER,
        D3D_SVT_GEOMETRYSHADER,
        D3D_SVT_GEOMETRYSHADER,
        D3D_SVT_TEXTURE,
        D3D_SVT_TEXTURE1D,
        D3D_SVT_TEXTURE1DARRAY,
        D3D_SVT_TEXTURE2D,
        D3D_SVT_TEXTURE2DARRAY,
        D3D_SVT_TEXTURE2DMS,
        D3D_SVT_TEXTURE2DMSARRAY,
        D3D_SVT_TEXTURE3D,
        D3D_SVT_TEXTURECUBE,
        0,
        D3D_SVT_RENDERTARGETVIEW,
        D3D_SVT_DEPTHSTENCILVIEW,
        D3D_SVT_SAMPLER,
        D3D_SVT_BUFFER,
    };
    static const D3D10_SHADER_VARIABLE_TYPE BASES[5] = {0, D3D_SVT_FLOAT, D3D_SVT_INT, D3D_SVT_UINT, D3D_SVT_BOOL};
    FxType *t = type_impl(iface);
    if (!desc)
        return E_INVALIDARG;
    memset(desc, 0, sizeof(*desc));
    desc->TypeName = t->name;
    desc->Elements = t->elements;
    desc->PackedSize = t->packed;
    desc->UnpackedSize = unpacked_size(t);
    desc->Stride = t->stride;
    if (t->buffer_type) {
        desc->Class = D3D_SVC_OBJECT;
        desc->Type = t->buffer_type;
        desc->Members = t->member_count;
        return S_OK;
    }
    switch (t->kind) {
    case FX_NUMERIC:
        desc->Class = FX_LAYOUT(t->numeric) == FX_SCALAR   ? D3D_SVC_SCALAR
                      : FX_LAYOUT(t->numeric) == FX_VECTOR ? D3D_SVC_VECTOR
                      : t->numeric & FX_COLUMN_MAJOR       ? D3D_SVC_MATRIX_COLUMNS
                                                           : D3D_SVC_MATRIX_ROWS;
        desc->Type = BASES[FX_BASE(t->numeric)];
        desc->Rows = FX_ROWS(t->numeric);
        desc->Columns = FX_COLUMNS(t->numeric);
        break;
    case FX_OBJECT:
        desc->Class = D3D_SVC_OBJECT;
        desc->Type = OBJECTS[t->object];
        break;
    default:
        desc->Class = D3D_SVC_STRUCT;
        desc->Members = t->member_count;
        break;
    }
    return S_OK;
}

static ID3D10EffectType *STDMETHODCALLTYPE type_member_by_index(ID3D10EffectType *iface, UINT index)
{
    FxType *t = type_impl(iface);
    return t->kind == FX_STRUCT && index < t->member_count ? &t->members[index].type->iface : &FxInvalidType;
}

static ID3D10EffectType *STDMETHODCALLTYPE type_member_by_name(ID3D10EffectType *iface, LPCSTR name)
{
    FxType *t = type_impl(iface);
    for (UINT i = 0; name && t->kind == FX_STRUCT && i < t->member_count; i++)
        if (t->members[i].name && !strcmp(t->members[i].name, name))
            return &t->members[i].type->iface;
    return &FxInvalidType;
}

static ID3D10EffectType *STDMETHODCALLTYPE type_member_by_semantic(ID3D10EffectType *iface, LPCSTR semantic)
{
    FxType *t = type_impl(iface);
    for (UINT i = 0; semantic && t->kind == FX_STRUCT && i < t->member_count; i++)
        if (t->members[i].semantic && !lstrcmpiA(t->members[i].semantic, semantic))
            return &t->members[i].type->iface;
    return &FxInvalidType;
}

static LPCSTR STDMETHODCALLTYPE type_member_name(ID3D10EffectType *iface, UINT index)
{
    FxType *t = type_impl(iface);
    return t->kind == FX_STRUCT && index < t->member_count ? t->members[index].name : nullptr;
}

static LPCSTR STDMETHODCALLTYPE type_member_semantic(ID3D10EffectType *iface, UINT index)
{
    FxType *t = type_impl(iface);
    return t->kind == FX_STRUCT && index < t->member_count ? t->members[index].semantic : nullptr;
}

static const ID3D10EffectTypeVtbl type_vtbl = {
    type_is_valid,           type_get_desc,    type_member_by_index, type_member_by_name,
    type_member_by_semantic, type_member_name, type_member_semantic,
};

void FxInitType(FxType *t)
{
    t->iface.lpVtbl = &type_vtbl;
    t->single = t;
}

/* the type of one element, made when first asked: no elements, a share of the packed size. */
static FxType *single_type(FxEffect *effect, FxType *t)
{
    if (!t->elements)
        return t;
    if (t->single == t) {
        FxType *s = FxAlloc(effect, sizeof(*s));
        if (!s)
            return t;
        *s = *t;
        s->elements = 0;
        s->packed = t->packed / t->elements;
        s->size = unpacked_size(t);
        s->single = s;
        t->single = s;
    }
    return t->single;
}

/* ------------------------------------------------------------------ values */

enum { V_FLOAT, V_INT, V_BOOL };

static UINT base_of(const FxVariable *v)
{
    UINT base = FX_BASE(v->type->numeric);
    return base == FX_FLOAT ? V_FLOAT : base == FX_BOOL ? V_BOOL : V_INT;
}

/* a value given as one kind stored as the variable's. */
static void put(BYTE *dst, UINT storage, UINT from, const void *src)
{
    INT i;
    float f;
    memcpy(&i, src, 4);
    memcpy(&f, src, 4);
    if (storage == from) {
        memcpy(dst, src, 4);
        return;
    }
    if (storage == V_FLOAT) {
        f = from == V_INT ? (float)i : i ? -1.0f : 0.0f;
        memcpy(dst, &f, 4);
        return;
    }
    if (storage == V_INT)
        i = from == V_FLOAT ? (INT)f : i ? -1 : 0;
    else
        i = (from == V_FLOAT ? f != 0.0f : i != 0) ? -1 : 0;
    memcpy(dst, &i, 4);
}

/* a stored value read as another kind: booleans are -1 when true, whatever is stored. */
static void get(void *dst, UINT storage, UINT to, const BYTE *src)
{
    INT i;
    float f;
    memcpy(&i, src, 4);
    memcpy(&f, src, 4);
    if (to == V_BOOL) {
        i = (storage == V_FLOAT ? f != 0.0f : i != 0) ? -1 : 0;
        memcpy(dst, &i, 4);
    } else if (storage == to || (to == V_INT && storage == V_BOOL)) {
        memcpy(dst, src, 4);
    } else if (to == V_FLOAT) {
        f = storage == V_INT ? (float)i : i ? -1.0f : 0.0f;
        memcpy(dst, &f, 4);
    } else {
        i = (INT)f;
        memcpy(dst, &i, 4);
    }
}

static HRESULT dirty(FxVariable *v)
{
    if (v->annotation)
        return D3DERR_INVALIDCALL;
    if (v->buffer)
        v->buffer->dirty = TRUE;
    return S_OK;
}

/* scalars: one register an element, the offset of arrays not read, as d3d10.dll does. */
static HRESULT set_scalars(void *iface, UINT from, const void *data, UINT count)
{
    FxVariable *v = impl(iface);
    HRESULT hr = dirty(v);
    UINT n = FxElementCount(v->type);
    if (FAILED(hr))
        return hr;
    for (UINT i = 0; i < count && i < n; i++)
        put(v->data + 16 * i, base_of(v), from, (const BYTE *)data + 4 * i);
    return S_OK;
}

static HRESULT get_scalars(void *iface, UINT to, void *data, UINT count)
{
    FxVariable *v = impl(iface);
    UINT n = FxElementCount(v->type);
    for (UINT i = 0; i < count && i < n; i++)
        get((BYTE *)data + 4 * i, base_of(v), to, v->data + 16 * i);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE set_float(void *iface, float value) { return set_scalars(iface, V_FLOAT, &value, 1); }
static HRESULT STDMETHODCALLTYPE get_float(void *iface, float *value) { return get_scalars(iface, V_FLOAT, value, 1); }
static HRESULT STDMETHODCALLTYPE set_int(void *iface, int value) { return set_scalars(iface, V_INT, &value, 1); }
static HRESULT STDMETHODCALLTYPE get_int(void *iface, int *value) { return get_scalars(iface, V_INT, value, 1); }
static HRESULT STDMETHODCALLTYPE set_bool(void *iface, BOOL value) { return set_scalars(iface, V_BOOL, &value, 1); }
static HRESULT STDMETHODCALLTYPE get_bool(void *iface, BOOL *value) { return get_scalars(iface, V_BOOL, value, 1); }

#define SCALAR_ARRAY(name, kind, type)                                                                                 \
    static HRESULT STDMETHODCALLTYPE set_##name##_array(void *iface, type *data, UINT offset, UINT count)              \
    {                                                                                                                  \
        return set_scalars(iface, kind, data, count);                                                                  \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE get_##name##_array(void *iface, type *data, UINT offset, UINT count)              \
    {                                                                                                                  \
        return get_scalars(iface, kind, data, count);                                                                  \
    }
SCALAR_ARRAY(float, V_FLOAT, float)
SCALAR_ARRAY(int, V_INT, int)
SCALAR_ARRAY(bool, V_BOOL, BOOL)

/* vectors: the columns of each element, an element a register. */
static HRESULT set_vectors(void *iface, UINT from, const void *data, UINT offset, UINT count)
{
    FxVariable *v = impl(iface);
    HRESULT hr = dirty(v);
    UINT n = FxElementCount(v->type), columns = FX_COLUMNS(v->type->numeric);
    if (FAILED(hr))
        return hr;
    for (UINT e = offset, k = 0; e < n && e < offset + count; e++)
        for (UINT c = 0; c < columns; c++, k++)
            put(v->data + v->type->stride * e + 4 * c, base_of(v), from, (const BYTE *)data + 4 * k);
    return S_OK;
}

static HRESULT get_vectors(void *iface, UINT to, void *data, UINT offset, UINT count)
{
    FxVariable *v = impl(iface);
    UINT n = FxElementCount(v->type), columns = FX_COLUMNS(v->type->numeric);
    for (UINT e = offset, k = 0; e < n && e < offset + count; e++)
        for (UINT c = 0; c < columns; c++, k++)
            get((BYTE *)data + 4 * k, base_of(v), to, v->data + v->type->stride * e + 4 * c);
    return S_OK;
}

#define VECTOR(name, kind, type)                                                                                       \
    static HRESULT STDMETHODCALLTYPE set_##name##_vector(void *iface, type *data)                                      \
    {                                                                                                                  \
        return set_vectors(iface, kind, data, 0, 1);                                                                   \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE get_##name##_vector(void *iface, type *data)                                      \
    {                                                                                                                  \
        return get_vectors(iface, kind, data, 0, 1);                                                                   \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE set_##name##_vector_array(void *iface, type *data, UINT offset, UINT count)       \
    {                                                                                                                  \
        return set_vectors(iface, kind, data, offset, count);                                                          \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE get_##name##_vector_array(void *iface, type *data, UINT offset, UINT count)       \
    {                                                                                                                  \
        return get_vectors(iface, kind, data, offset, count);                                                          \
    }
VECTOR(float, V_FLOAT, float)
VECTOR(int, V_INT, int)
VECTOR(bool, V_BOOL, BOOL)

/*
 * matrices: given as 4x4 floats, row after row. a register holds a row, a
 * column in column major matrices; transposed calls give the transpose.
 */
static HRESULT matrices(void *iface, BOOL set, BOOL transpose, float *data, UINT offset, UINT count)
{
    FxVariable *v = impl(iface);
    if (set) {
        HRESULT hr = dirty(v);
        if (FAILED(hr))
            return hr;
    }
    UINT n = FxElementCount(v->type), rows = FX_ROWS(v->type->numeric), cols = FX_COLUMNS(v->type->numeric);
    BOOL column_major = (v->type->numeric & FX_COLUMN_MAJOR) != 0;
    for (UINT e = offset; e < n && e < offset + count; e++, data += 16) {
        float *store = (float *)(v->data + v->type->stride * e);
        for (UINT r = 0; r < rows; r++)
            for (UINT c = 0; c < cols; c++) {
                float *s = column_major ? &store[4 * c + r] : &store[4 * r + c];
                float *m = transpose ? &data[4 * c + r] : &data[4 * r + c];
                if (set)
                    *s = *m;
                else
                    *m = *s;
            }
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE set_matrix(void *iface, float *data)
{
    return matrices(iface, TRUE, FALSE, data, 0, 1);
}
static HRESULT STDMETHODCALLTYPE get_matrix(void *iface, float *data)
{
    return matrices(iface, FALSE, FALSE, data, 0, 1);
}
static HRESULT STDMETHODCALLTYPE set_matrix_t(void *iface, float *data)
{
    return matrices(iface, TRUE, TRUE, data, 0, 1);
}
static HRESULT STDMETHODCALLTYPE get_matrix_t(void *iface, float *data)
{
    return matrices(iface, FALSE, TRUE, data, 0, 1);
}

static HRESULT STDMETHODCALLTYPE set_matrix_array(void *iface, float *data, UINT offset, UINT count)
{
    return matrices(iface, TRUE, FALSE, data, offset, count);
}

static HRESULT STDMETHODCALLTYPE get_matrix_array(void *iface, float *data, UINT offset, UINT count)
{
    return matrices(iface, FALSE, FALSE, data, offset, count);
}

static HRESULT STDMETHODCALLTYPE set_matrix_t_array(void *iface, float *data, UINT offset, UINT count)
{
    return matrices(iface, TRUE, TRUE, data, offset, count);
}

static HRESULT STDMETHODCALLTYPE get_matrix_t_array(void *iface, float *data, UINT offset, UINT count)
{
    return matrices(iface, FALSE, TRUE, data, offset, count);
}

/* ------------------------------------------------------------------ objects */

static HRESULT STDMETHODCALLTYPE get_string(void *iface, LPCSTR *out)
{
    if (!out)
        return E_INVALIDARG;
    *out = ((const char **)impl(iface)->data)[0];
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE get_string_array(void *iface, LPCSTR *out, UINT offset, UINT count)
{
    FxVariable *v = impl(iface);
    if (!out)
        return E_INVALIDARG;
    for (UINT i = offset, k = 0; i < FxElementCount(v->type) && i < offset + count; i++)
        out[k++] = ((const char **)v->data)[i];
    return S_OK;
}

/* views: the variable holds a reference on each. */
static HRESULT set_views(void *iface, IUnknown **views, UINT offset, UINT count)
{
    FxVariable *v = impl(iface);
    IUnknown **slots = (IUnknown **)v->data;
    for (UINT i = offset, k = 0; i < FxElementCount(v->type) && i < offset + count; i++, k++) {
        if (views[k])
            views[k]->lpVtbl->AddRef(views[k]);
        if (slots[i])
            slots[i]->lpVtbl->Release(slots[i]);
        slots[i] = views[k];
    }
    return S_OK;
}

static HRESULT get_views(void *iface, IUnknown **views, UINT offset, UINT count)
{
    FxVariable *v = impl(iface);
    IUnknown **slots = (IUnknown **)v->data;
    if (!views)
        return E_INVALIDARG;
    for (UINT i = offset, k = 0; i < FxElementCount(v->type) && i < offset + count; i++, k++) {
        views[k] = slots[i];
        if (views[k])
            views[k]->lpVtbl->AddRef(views[k]);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE set_view(void *iface, IUnknown *view) { return set_views(iface, &view, 0, 1); }
static HRESULT STDMETHODCALLTYPE get_view(void *iface, IUnknown **view) { return get_views(iface, view, 0, 1); }

static HRESULT STDMETHODCALLTYPE set_view_array(void *iface, IUnknown **views, UINT offset, UINT count)
{
    return views ? set_views(iface, views, offset, count) : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE get_view_array(void *iface, IUnknown **views, UINT offset, UINT count)
{
    return get_views(iface, views, offset, count);
}

static FxShader *shader_at(void *iface, UINT index)
{
    FxVariable *v = impl(iface);
    return index < FxElementCount(v->type) ? (FxShader *)v->data + index : nullptr;
}

static HRESULT STDMETHODCALLTYPE get_shader_desc(void *iface, UINT index, D3D10_EFFECT_SHADER_DESC *desc)
{
    FxShader *s = shader_at(iface, index);
    if (!s || !desc)
        return E_INVALIDARG;
    memset(desc, 0, sizeof(*desc));
    desc->IsInline = impl(iface)->name && !strcmp(impl(iface)->name, "$Anonymous");
    if (!s->code)
        return S_OK;
    desc->pInputSignature = s->signature ? s->signature->lpVtbl->GetBufferPointer(s->signature) : nullptr;
    desc->pBytecode = s->code;
    desc->BytecodeLength = s->length;
    desc->SODecl = s->so_decl;
    ID3D10ShaderReflection *r;
    if (SUCCEEDED(D3D10ReflectShader(s->code, s->length, &r))) {
        D3D10_SHADER_DESC sd;
        r->lpVtbl->GetDesc(r, &sd);
        desc->NumInputSignatureEntries = sd.InputParameters;
        desc->NumOutputSignatureEntries = sd.OutputParameters;
        r->lpVtbl->Release(r);
    }
    return S_OK;
}

static HRESULT get_shader_object(void *iface, UINT index, UINT kind, IUnknown **out)
{
    FxShader *s = shader_at(iface, index);
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    if (!s || s->kind != kind)
        return E_FAIL;
    if ((*out = s->object))
        s->object->lpVtbl->AddRef(s->object);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE get_vs(void *iface, UINT index, IUnknown **out)
{
    return get_shader_object(iface, index, FX_VERTEXSHADER, out);
}

static HRESULT STDMETHODCALLTYPE get_gs(void *iface, UINT index, IUnknown **out)
{
    return get_shader_object(iface, index, FX_GEOMETRYSHADER, out);
}

static HRESULT STDMETHODCALLTYPE get_ps(void *iface, UINT index, IUnknown **out)
{
    return get_shader_object(iface, index, FX_PIXELSHADER, out);
}

static HRESULT signature_element(void *iface, UINT index, UINT element, BOOL output,
                                 D3D10_SIGNATURE_PARAMETER_DESC *desc)
{
    FxShader *s = shader_at(iface, index);
    ID3D10ShaderReflection *r;
    if (!s || !desc || !s->code)
        return E_INVALIDARG;
    if (FAILED(D3D10ReflectShader(s->code, s->length, &r)))
        return E_FAIL;
    /* the names point into the reflection, which lives as long as the effect. */
    HRESULT hr = output ? r->lpVtbl->GetOutputParameterDesc(r, element, desc)
                        : r->lpVtbl->GetInputParameterDesc(r, element, desc);
    if (SUCCEEDED(hr) && desc->SemanticName) {
        SIZE_T n = strlen(desc->SemanticName) + 1;
        char *name = FxAlloc(impl(iface)->effect, n);
        if (name)
            memcpy(name, desc->SemanticName, n);
        desc->SemanticName = name;
    }
    r->lpVtbl->Release(r);
    return hr;
}

static HRESULT STDMETHODCALLTYPE get_input_element(void *iface, UINT index, UINT element,
                                                   D3D10_SIGNATURE_PARAMETER_DESC *desc)
{
    return signature_element(iface, index, element, FALSE, desc);
}

static HRESULT STDMETHODCALLTYPE get_output_element(void *iface, UINT index, UINT element,
                                                    D3D10_SIGNATURE_PARAMETER_DESC *desc)
{
    return signature_element(iface, index, element, TRUE, desc);
}

/* state objects and their descriptions, after the assignments of variables. */
static FxBlock *block_at(void *iface, UINT index)
{
    FxVariable *v = impl(iface);
    if (index >= FxElementCount(v->type))
        return nullptr;
    FxBlock *b = (FxBlock *)v->data + index;
    FxEvaluateBlock(v->effect, b);
    return b;
}

static HRESULT STDMETHODCALLTYPE get_state(void *iface, UINT index, IUnknown **out)
{
    FxBlock *b = block_at(iface, index);
    if (!b || !out)
        return E_INVALIDARG;
    if ((*out = b->state))
        b->state->lpVtbl->AddRef(b->state);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE get_backing_store(void *iface, UINT index, void *desc)
{
    FxBlock *b = block_at(iface, index);
    if (!b || !desc)
        return E_INVALIDARG;
    SIZE_T size = b->kind == FX_BLOCK_BLEND          ? sizeof(D3D10_BLEND_DESC)
                  : b->kind == FX_BLOCK_DEPTHSTENCIL ? sizeof(D3D10_DEPTH_STENCIL_DESC)
                  : b->kind == FX_BLOCK_RASTERIZER   ? sizeof(D3D10_RASTERIZER_DESC)
                                                     : sizeof(D3D10_SAMPLER_DESC);
    memcpy(desc, &b->desc, size);
    return S_OK;
}

/* constant buffers: the program's own buffer replaces the effect's, which is no longer written. */
static FxBuffer *buffer_impl(void *iface) { return CONTAINING_RECORD(iface, FxBuffer, var.iface); }

static HRESULT STDMETHODCALLTYPE set_constant_buffer(void *iface, ID3D10Buffer *buffer)
{
    FxBuffer *b = buffer_impl(iface);
    if (!buffer)
        return E_INVALIDARG;
    if (b->tbuffer)
        return D3DERR_INVALIDCALL;
    buffer->lpVtbl->AddRef(buffer);
    if (b->buffer)
        b->buffer->lpVtbl->Release(b->buffer);
    b->buffer = buffer;
    b->user = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE get_constant_buffer(void *iface, ID3D10Buffer **buffer)
{
    FxBuffer *b = buffer_impl(iface);
    if (!buffer)
        return E_INVALIDARG;
    if (b->tbuffer)
        return D3DERR_INVALIDCALL;
    if ((*buffer = b->buffer))
        b->buffer->lpVtbl->AddRef(b->buffer);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE set_texture_buffer(void *iface, ID3D10ShaderResourceView *view)
{
    FxBuffer *b = buffer_impl(iface);
    if (!view)
        return E_INVALIDARG;
    if (!b->tbuffer)
        return D3DERR_INVALIDCALL;
    b->user = TRUE;
    if (b->buffer)
        b->buffer->lpVtbl->Release(b->buffer);
    b->buffer = nullptr;
    view->lpVtbl->AddRef(view);
    if (b->view)
        b->view->lpVtbl->Release(b->view);
    b->view = view;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE get_texture_buffer(void *iface, ID3D10ShaderResourceView **view)
{
    FxBuffer *b = buffer_impl(iface);
    if (!view)
        return E_INVALIDARG;
    if (!b->tbuffer)
        return D3DERR_INVALIDCALL;
    if ((*view = b->view))
        b->view->lpVtbl->AddRef(b->view);
    return S_OK;
}

/* ------------------------------------------------------------------ every variable */

static BOOL STDMETHODCALLTYPE var_is_valid(void *iface) { return TRUE; }

static ID3D10EffectType *STDMETHODCALLTYPE var_get_type(void *iface) { return &impl(iface)->type->iface; }

static HRESULT STDMETHODCALLTYPE var_get_desc(void *iface, D3D10_EFFECT_VARIABLE_DESC *desc)
{
    FxVariable *v = impl(iface);
    if (!desc)
        return E_INVALIDARG;
    memset(desc, 0, sizeof(*desc));
    desc->Name = v->name;
    desc->Semantic = v->semantic;
    if (v->effect && v->effect->is_pool)
        desc->Flags |= D3D10_EFFECT_VARIABLE_POOLED;
    if (v->annotation) {
        desc->Flags |= D3D10_EFFECT_VARIABLE_ANNOTATION;
        return S_OK;
    }
    desc->Annotations = v->top->annotation_count;
    if (v->buffer)
        desc->BufferOffset = (UINT)(v->data - v->buffer->store);
    if (v->bind_point != ~0u && !v->member) {
        desc->Flags |= D3D10_EFFECT_VARIABLE_EXPLICIT_BIND_POINT;
        desc->ExplicitBindPoint = v->bind_point;
    }
    return S_OK;
}

ID3D10EffectVariable *FxAnnotationByIndex(UINT count, FxVariable *annotations, UINT index)
{
    return index < count ? &annotations[index].iface : &FxInvalidVariable.iface;
}

ID3D10EffectVariable *FxAnnotationByName(UINT count, FxVariable *annotations, LPCSTR name)
{
    for (UINT i = 0; name && i < count; i++)
        if (annotations[i].name && !strcmp(annotations[i].name, name))
            return &annotations[i].iface;
    return &FxInvalidVariable.iface;
}

static ID3D10EffectVariable *STDMETHODCALLTYPE var_annotation_by_index(void *iface, UINT index)
{
    FxVariable *top = impl(iface)->top;
    return FxAnnotationByIndex(top->annotation_count, top->annotations, index);
}

static ID3D10EffectVariable *STDMETHODCALLTYPE var_annotation_by_name(void *iface, LPCSTR name)
{
    FxVariable *top = impl(iface)->top;
    return FxAnnotationByName(top->annotation_count, top->annotations, name);
}

/* an element or member, kept once made; its index counts the elements first. */
static FxVariable *child(FxVariable *v, UINT index)
{
    FxType *t = v->type;
    UINT elements = t->elements, total = elements + (t->kind == FX_STRUCT ? t->member_count : 0);
    if (!v->children && !(v->children = FxAlloc(v->effect, total * sizeof(*v->children))))
        return nullptr;
    if (v->children[index])
        return v->children[index];
    FxVariable *c = FxAlloc(v->effect, sizeof(*c));
    if (!c)
        return nullptr;
    *c = *v;
    c->children = nullptr;
    c->member = TRUE;
    if (index < elements) {
        c->type = single_type(v->effect, t);
        if (t->kind == FX_OBJECT) {
            UINT size = t->object == FX_BLEND || t->object == FX_DEPTHSTENCIL || t->object == FX_RASTERIZER ||
                                t->object == FX_SAMPLER
                            ? sizeof(FxBlock)
                        : t->object >= FX_PIXELSHADER && t->object <= FX_GEOMETRYSHADER_SO ? sizeof(FxShader)
                                                                                           : sizeof(void *);
            c->data = v->data + (SIZE_T)size * index;
        } else {
            c->data = v->data + t->stride * index;
        }
    } else {
        FxMember *m = &t->members[index - elements];
        c->type = m->type;
        c->name = m->name;
        c->semantic = m->semantic;
        c->data = v->data + m->offset;
    }
    FxInitVariable(c);
    v->children[index] = c;
    return c;
}

static FxBuffer *buffer_of(FxVariable *v) { return v->type->buffer_type ? CONTAINING_RECORD(v, FxBuffer, var) : nullptr; }

/* a constant buffer's members are its variables. */
static ID3D10EffectVariable *STDMETHODCALLTYPE var_member_by_index(void *iface, UINT index)
{
    FxVariable *v = impl(iface), *c;
    FxBuffer *b = buffer_of(v);
    if (b)
        return index < b->variable_count ? &b->variables[index].iface : &FxInvalidVariable.iface;
    if (v->type->kind != FX_STRUCT || index >= v->type->member_count || !(c = child(v, v->type->elements + index)))
        return &FxInvalidVariable.iface;
    return &c->iface;
}

static ID3D10EffectVariable *STDMETHODCALLTYPE var_member_by_name(void *iface, LPCSTR name)
{
    FxVariable *v = impl(iface);
    FxBuffer *b = buffer_of(v);
    for (UINT i = 0; b && name && i < b->variable_count; i++)
        if (b->variables[i].name && !strcmp(b->variables[i].name, name))
            return &b->variables[i].iface;
    for (UINT i = 0; name && v->type->kind == FX_STRUCT && i < v->type->member_count; i++)
        if (v->type->members[i].name && !strcmp(v->type->members[i].name, name))
            return var_member_by_index(iface, i);
    return &FxInvalidVariable.iface;
}

static ID3D10EffectVariable *STDMETHODCALLTYPE var_member_by_semantic(void *iface, LPCSTR semantic)
{
    FxVariable *v = impl(iface);
    FxBuffer *b = buffer_of(v);
    for (UINT i = 0; b && semantic && i < b->variable_count; i++)
        if (b->variables[i].semantic && !lstrcmpiA(b->variables[i].semantic, semantic))
            return &b->variables[i].iface;
    for (UINT i = 0; semantic && v->type->kind == FX_STRUCT && i < v->type->member_count; i++)
        if (v->type->members[i].semantic && !lstrcmpiA(v->type->members[i].semantic, semantic))
            return var_member_by_index(iface, i);
    return &FxInvalidVariable.iface;
}

static ID3D10EffectVariable *STDMETHODCALLTYPE var_element(void *iface, UINT index)
{
    FxVariable *v = impl(iface), *c;
    if (index >= v->type->elements || !(c = child(v, index)))
        return &FxInvalidVariable.iface;
    return &c->iface;
}

static ID3D10EffectConstantBuffer *STDMETHODCALLTYPE var_parent(void *iface)
{
    FxVariable *v = impl(iface);
    return (ID3D10EffectConstantBuffer *)(v->buffer ? &v->buffer->var.iface : &FxInvalidBuffer.iface);
}

static BOOL is_kind(FxVariable *v, UINT object) { return v->type->kind == FX_OBJECT && v->type->object == object; }

static BOOL is_layout(FxVariable *v, UINT layout)
{
    return v->type->kind == FX_NUMERIC && FX_LAYOUT(v->type->numeric) == layout;
}

static void *as(void *iface, BOOL fits, UINT which) { return fits ? iface : &invalid[which].iface; }

static void *STDMETHODCALLTYPE var_as_scalar(void *iface)
{
    return as(iface, is_layout(impl(iface), FX_SCALAR), I_SCALAR);
}
static void *STDMETHODCALLTYPE var_as_vector(void *iface)
{
    return as(iface, is_layout(impl(iface), FX_VECTOR), I_VECTOR);
}
static void *STDMETHODCALLTYPE var_as_matrix(void *iface)
{
    return as(iface, is_layout(impl(iface), FX_MATRIX), I_MATRIX);
}
static void *STDMETHODCALLTYPE var_as_string(void *iface)
{
    return as(iface, is_kind(impl(iface), FX_STRING), I_STRING);
}

static void *STDMETHODCALLTYPE var_as_srv(void *iface)
{
    FxType *t = impl(iface)->type;
    return as(iface,
              t->kind == FX_OBJECT &&
                  ((t->object >= FX_TEXTURE && t->object <= FX_TEXTURECUBE) || t->object == FX_BUFFER),
              I_SRV);
}

static void *STDMETHODCALLTYPE var_as_rtv(void *iface)
{
    return as(iface, is_kind(impl(iface), FX_RENDERTARGETVIEW), I_RTV);
}
static void *STDMETHODCALLTYPE var_as_dsv(void *iface)
{
    return as(iface, is_kind(impl(iface), FX_DEPTHSTENCILVIEW), I_DSV);
}
static void *STDMETHODCALLTYPE var_as_buffer(void *iface)
{
    return as(iface, impl(iface)->type->buffer_type != 0, I_BUFFER);
}

static void *STDMETHODCALLTYPE var_as_shader(void *iface)
{
    FxType *t = impl(iface)->type;
    return as(iface, t->kind == FX_OBJECT && t->object >= FX_PIXELSHADER && t->object <= FX_GEOMETRYSHADER_SO,
              I_SHADER);
}

static void *STDMETHODCALLTYPE var_as_blend(void *iface) { return as(iface, is_kind(impl(iface), FX_BLEND), I_BLEND); }
static void *STDMETHODCALLTYPE var_as_ds(void *iface)
{
    return as(iface, is_kind(impl(iface), FX_DEPTHSTENCIL), I_DEPTHSTENCIL);
}
static void *STDMETHODCALLTYPE var_as_rs(void *iface)
{
    return as(iface, is_kind(impl(iface), FX_RASTERIZER), I_RASTERIZER);
}
static void *STDMETHODCALLTYPE var_as_sampler(void *iface)
{
    return as(iface, is_kind(impl(iface), FX_SAMPLER), I_SAMPLER);
}

/* raw bytes of numbers, which may run past the variable to the end of its buffer. */
static HRESULT STDMETHODCALLTYPE var_set_raw(void *iface, void *data, UINT offset, UINT count)
{
    FxVariable *v = impl(iface);
    if (v->type->kind == FX_OBJECT || v->annotation)
        return D3DERR_INVALIDCALL;
    BYTE *end = v->buffer ? v->buffer->store + v->buffer->size : v->data + v->type->size;
    if (!data || offset > (UINT)(end - v->data) || count > (UINT)(end - v->data) - offset)
        return E_INVALIDARG;
    memcpy(v->data + offset, data, count);
    return dirty(v);
}

static HRESULT STDMETHODCALLTYPE var_get_raw(void *iface, void *data, UINT offset, UINT count)
{
    FxVariable *v = impl(iface);
    if (v->type->kind == FX_OBJECT)
        return D3DERR_INVALIDCALL;
    BYTE *end = v->buffer ? v->buffer->store + v->buffer->size : v->data + v->type->size;
    if (!data || offset > (UINT)(end - v->data) || count > (UINT)(end - v->data) - offset)
        return E_INVALIDARG;
    memcpy(data, v->data + offset, count);
    return S_OK;
}

#define VAR_BASE                                                                                                       \
    VTBL_FN(var_is_valid), VTBL_FN(var_get_type), VTBL_FN(var_get_desc), VTBL_FN(var_annotation_by_index),             \
        VTBL_FN(var_annotation_by_name), VTBL_FN(var_member_by_index), VTBL_FN(var_member_by_name),                    \
        VTBL_FN(var_member_by_semantic), VTBL_FN(var_element), VTBL_FN(var_parent), VTBL_FN(var_as_scalar),            \
        VTBL_FN(var_as_vector), VTBL_FN(var_as_matrix), VTBL_FN(var_as_string), VTBL_FN(var_as_srv),                   \
        VTBL_FN(var_as_rtv), VTBL_FN(var_as_dsv), VTBL_FN(var_as_buffer), VTBL_FN(var_as_shader),                      \
        VTBL_FN(var_as_blend), VTBL_FN(var_as_ds), VTBL_FN(var_as_rs), VTBL_FN(var_as_sampler), VTBL_FN(var_set_raw),  \
        VTBL_FN(var_get_raw)

static const void *const struct_vtbl[] = {VAR_BASE};
static const void *const scalar_vtbl[] = {
    VAR_BASE,
    VTBL_FN(set_float),
    VTBL_FN(get_float),
    VTBL_FN(set_float_array),
    VTBL_FN(get_float_array),
    VTBL_FN(set_int),
    VTBL_FN(get_int),
    VTBL_FN(set_int_array),
    VTBL_FN(get_int_array),
    VTBL_FN(set_bool),
    VTBL_FN(get_bool),
    VTBL_FN(set_bool_array),
    VTBL_FN(get_bool_array),
};
static const void *const vector_vtbl[] = {
    VAR_BASE,
    VTBL_FN(set_bool_vector),
    VTBL_FN(set_int_vector),
    VTBL_FN(set_float_vector),
    VTBL_FN(get_bool_vector),
    VTBL_FN(get_int_vector),
    VTBL_FN(get_float_vector),
    VTBL_FN(set_bool_vector_array),
    VTBL_FN(set_int_vector_array),
    VTBL_FN(set_float_vector_array),
    VTBL_FN(get_bool_vector_array),
    VTBL_FN(get_int_vector_array),
    VTBL_FN(get_float_vector_array),
};
static const void *const matrix_vtbl[] = {
    VAR_BASE,
    VTBL_FN(set_matrix),
    VTBL_FN(get_matrix),
    VTBL_FN(set_matrix_array),
    VTBL_FN(get_matrix_array),
    VTBL_FN(set_matrix_t),
    VTBL_FN(get_matrix_t),
    VTBL_FN(set_matrix_t_array),
    VTBL_FN(get_matrix_t_array),
};
static const void *const string_vtbl[] = {VAR_BASE, VTBL_FN(get_string), VTBL_FN(get_string_array)};
static const void *const view_vtbl[] = {VAR_BASE, VTBL_FN(set_view), VTBL_FN(get_view), VTBL_FN(set_view_array),
                                        VTBL_FN(get_view_array)};
static const void *const shader_vtbl[] = {VAR_BASE,
                                          VTBL_FN(get_shader_desc),
                                          VTBL_FN(get_vs),
                                          VTBL_FN(get_gs),
                                          VTBL_FN(get_ps),
                                          VTBL_FN(get_input_element),
                                          VTBL_FN(get_output_element)};
static const void *const state_vtbl[] = {VAR_BASE, VTBL_FN(get_state), VTBL_FN(get_backing_store)};
static const void *const buffer_vtbl[] = {VAR_BASE, VTBL_FN(set_constant_buffer), VTBL_FN(get_constant_buffer),
                                          VTBL_FN(set_texture_buffer), VTBL_FN(get_texture_buffer)};

void FxInitVariable(FxVariable *v)
{
    FxType *t = v->type;
    const void *const *vtbl = struct_vtbl;
    if (t->kind == FX_NUMERIC) {
        UINT layout = FX_LAYOUT(t->numeric);
        vtbl = layout == FX_SCALAR ? scalar_vtbl : layout == FX_VECTOR ? vector_vtbl : matrix_vtbl;
    } else if (t->kind == FX_OBJECT) {
        UINT o = t->object;
        vtbl = o == FX_STRING                                                                   ? string_vtbl
               : o >= FX_PIXELSHADER && o <= FX_GEOMETRYSHADER_SO                               ? shader_vtbl
               : o == FX_BLEND || o == FX_DEPTHSTENCIL || o == FX_RASTERIZER || o == FX_SAMPLER ? state_vtbl
                                                                                                : view_vtbl;
    }
    v->iface.lpVtbl = (const ID3D10EffectVariableVtbl *)vtbl;
}

/* a constant buffer's type: cbuffer or tbuffer, its variables the members, their packed sizes summed. */
void FxInitBuffer(FxBuffer *b)
{
    FxType *t = &b->type;
    t->name = b->tbuffer ? "tbuffer" : "cbuffer";
    t->buffer_type = b->tbuffer ? D3D_SVT_TBUFFER : D3D_SVT_CBUFFER;
    t->member_count = b->variable_count;
    t->size = b->size;
    t->stride = (b->size + 15) & ~15u;
    for (UINT i = 0; i < b->variable_count; i++)
        t->packed += b->variables[i].type->packed;
    FxInitType(t);
    b->var.type = t;
    b->var.top = &b->var;
    b->var.buffer = b;
    b->var.data = b->store;
    b->var.iface.lpVtbl = (const ID3D10EffectVariableVtbl *)buffer_vtbl;
}

/* the invalid objects and the null shaders' anonymous variables, set up when the dll loads. */
static FxType null_types[3] = {
    {.name = "pixelshader", .kind = FX_OBJECT, .object = FX_PIXELSHADER},
    {.name = "vertexshader", .kind = FX_OBJECT, .object = FX_VERTEXSHADER},
    {.name = "geometryshader", .kind = FX_OBJECT, .object = FX_GEOMETRYSHADER},
};
static FxShader null_shader_storage[3];

void FxInit(void)
{
    static const void *const *const VTBLS[I_COUNT] = {
        invalid_scalar_vtbl, invalid_vector_vtbl, invalid_matrix_vtbl, invalid_string_vtbl, invalid_view_vtbl,
        invalid_view_vtbl,   invalid_view_vtbl,   invalid_buffer_vtbl, invalid_shader_vtbl, invalid_state_vtbl,
        invalid_state_vtbl,  invalid_state_vtbl,  invalid_state_vtbl,
    };
    for (UINT i = 0; i < I_COUNT; i++)
        invalid[i].iface.lpVtbl = (const ID3D10EffectVariableVtbl *)VTBLS[i];
    FxInvalidVariable.iface.lpVtbl = (const ID3D10EffectVariableVtbl *)invalid_scalar_vtbl;
    FxInvalidBuffer.iface.lpVtbl = (const ID3D10EffectVariableVtbl *)invalid_buffer_vtbl;
    for (UINT i = 0; i < 3; i++) {
        FxInitType(&null_types[i]);
        null_shader_storage[i].kind = null_types[i].object;
        FxVariable *v = &FxAnonymousNull[i];
        v->type = &null_types[i];
        v->name = "$Anonymous";
        v->top = v;
        v->bind_point = ~0u;
        v->data = (BYTE *)&null_shader_storage[i];
        FxInitVariable(v);
    }
}
