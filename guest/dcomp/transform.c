/*
 * the objects a visual refers to for its place and look: transforms, effect
 * groups, rectangle clips and filter effects. Their properties are numbers
 * or animations and are read when a frame is composed.
 */
#include "dcomp_private.h"

static BOOL finite(float v) { return v - v == 0.0f; }

/* ------------------------------------------------------------------ properties */

static HRESULT set_value(DcObject *owner, DcFloat *slot, float value)
{
    if (!finite(value))
        return E_INVALIDARG;
    DcDeviceLock(owner->device);
    DcFloatClear(slot);
    slot->value = value;
    DcObjectDirty(owner);
    DcDeviceUnlock(owner->device);
    return S_OK;
}

static HRESULT set_animation(DcObject *owner, DcFloat *slot, IDCompositionAnimation *animation)
{
    DcAnimation *a = DcAnimationFrom(animation);
    if (!a || a->base.device != owner->device)
        return E_INVALIDARG;
    DcDeviceLock(owner->device);
    DcObjectAddRef(&a->base);
    DcFloatClear(slot);
    slot->animation = a;
    DcObjectDirty(owner);
    DcDeviceUnlock(owner->device);
    return S_OK;
}

/* the references an object counts, assigned under the device's lock. */
static void assign(DcObject **slot, DcObject *object)
{
    if (object)
        DcObjectAddRef(object);
    if (*slot)
        DcObjectRelease(*slot);
    *slot = object;
}

/* ------------------------------------------------------------------ transforms */

static DcTransform *tr_impl(void *iface) { return CONTAINING_RECORD(iface, DcTransform, iface); }

static void tr_state_free(DcTransformState *s)
{
    for (UINT i = 0; i < DC_TRANSFORM_FLOATS; i++)
        DcFloatClear(&s->p[i]);
    for (UINT i = 0; i < s->count; i++)
        DcObjectRelease(s->list[i]);
    if (s->list)
        HeapFree(GetProcessHeap(), 0, s->list);
    memset(s, 0, sizeof(*s));
}

static void tr_commit(DcObject *object)
{
    DcTransform *t = CONTAINING_RECORD(object, DcTransform, base);
    DcObject **list = nullptr;
    if (t->pending.count) {
        list = HeapAlloc(GetProcessHeap(), 0, t->pending.count * sizeof(*list));
        if (!list)
            return;
        for (UINT i = 0; i < t->pending.count; i++) {
            list[i] = t->pending.list[i];
            DcObjectAddRef(list[i]);
        }
    }
    DcTransformState old = t->committed;
    t->committed = t->pending;
    for (UINT i = 0; i < DC_TRANSFORM_FLOATS; i++) {
        t->committed.p[i].animation = nullptr;
        DcFloatCopy(&t->committed.p[i], &t->pending.p[i]);
    }
    t->committed.list = list;
    tr_state_free(&old);
}

static void tr_destroy(DcObject *object)
{
    DcTransform *t = CONTAINING_RECORD(object, DcTransform, base);
    tr_state_free(&t->pending);
    tr_state_free(&t->committed);
    HeapFree(GetProcessHeap(), 0, t);
}

static const DcClass tr_class = {DC_TRANSFORM, tr_commit, tr_destroy};

static HRESULT STDMETHODCALLTYPE tr_set(void *This, UINT index, float value)
{
    DcTransform *t = tr_impl(This);
    return set_value(&t->base, &t->pending.p[index], value);
}

static HRESULT STDMETHODCALLTYPE tr_set_animation(void *This, UINT index, IDCompositionAnimation *animation)
{
    DcTransform *t = tr_impl(This);
    return set_animation(&t->base, &t->pending.p[index], animation);
}

static BOOL three_d(DcTransformKind kind) { return kind >= DCT_TRANSLATE3D; }

static const GUID *const TRANSFORM_IIDS_2D[] = {&IID_IDCompositionEffect, &IID_IDCompositionTransform3D,
                                                &IID_IDCompositionTransform};
static const GUID *const TRANSFORM_IIDS_3D[] = {&IID_IDCompositionEffect, &IID_IDCompositionTransform3D};

static const GUID *specific_iid(DcTransformKind kind)
{
    switch (kind) {
    case DCT_TRANSLATE:
        return &IID_IDCompositionTranslateTransform;
    case DCT_SCALE:
        return &IID_IDCompositionScaleTransform;
    case DCT_ROTATE:
        return &IID_IDCompositionRotateTransform;
    case DCT_SKEW:
        return &IID_IDCompositionSkewTransform;
    case DCT_MATRIX:
        return &IID_IDCompositionMatrixTransform;
    case DCT_TRANSLATE3D:
        return &IID_IDCompositionTranslateTransform3D;
    case DCT_SCALE3D:
        return &IID_IDCompositionScaleTransform3D;
    case DCT_ROTATE3D:
        return &IID_IDCompositionRotateTransform3D;
    case DCT_MATRIX3D:
        return &IID_IDCompositionMatrixTransform3D;
    default:
        return nullptr;
    }
}

static HRESULT STDMETHODCALLTYPE tr_query(void *This, REFIID riid, void **out)
{
    DcTransform *t = tr_impl(This);
    DcInterface interfaces[6];
    UINT n = 0;
    BOOL d3 = three_d(t->kind);
    for (UINT i = 0; i < (d3 ? RTL_NUMBER_OF(TRANSFORM_IIDS_3D) : RTL_NUMBER_OF(TRANSFORM_IIDS_2D)); i++) {
        interfaces[n].iid = d3 ? TRANSFORM_IIDS_3D[i] : TRANSFORM_IIDS_2D[i];
        interfaces[n++].pointer = This;
    }
    if (specific_iid(t->kind)) {
        interfaces[n].iid = specific_iid(t->kind);
        interfaces[n++].pointer = This;
    }
    return DcQuery(&t->base, interfaces, n, riid, out);
}

static ULONG STDMETHODCALLTYPE tr_add_ref(void *This) { return DcObjectAddRef(&tr_impl(This)->base); }
static ULONG STDMETHODCALLTYPE tr_release(void *This) { return DcObjectRelease(&tr_impl(This)->base); }

/* one setter for a value and one for an animation, of the property at an index. */
#define PROPERTY(prefix, name, index)                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_##name##_a(void *This, IDCompositionAnimation *animation)                \
    {                                                                                                                  \
        return tr_set_animation(This, index, animation);                                                               \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE prefix##_##name(void *This, float value) { return tr_set(This, index, value); }

PROPERTY(tl, offset_x, 0)
PROPERTY(tl, offset_y, 1)
PROPERTY(sc, scale_x, 0)
PROPERTY(sc, scale_y, 1)
PROPERTY(sc, center_x, 2)
PROPERTY(sc, center_y, 3)
PROPERTY(ro, angle, 0)
PROPERTY(ro, center_x, 1)
PROPERTY(ro, center_y, 2)
PROPERTY(sk, angle_x, 0)
PROPERTY(sk, angle_y, 1)
PROPERTY(sk, center_x, 2)
PROPERTY(sk, center_y, 3)
PROPERTY(tl3, offset_x, 0)
PROPERTY(tl3, offset_y, 1)
PROPERTY(tl3, offset_z, 2)
PROPERTY(sc3, scale_x, 0)
PROPERTY(sc3, scale_y, 1)
PROPERTY(sc3, scale_z, 2)
PROPERTY(sc3, center_x, 3)
PROPERTY(sc3, center_y, 4)
PROPERTY(sc3, center_z, 5)
PROPERTY(ro3, angle, 0)
PROPERTY(ro3, axis_x, 1)
PROPERTY(ro3, axis_y, 2)
PROPERTY(ro3, axis_z, 3)
PROPERTY(ro3, center_x, 4)
PROPERTY(ro3, center_y, 5)
PROPERTY(ro3, center_z, 6)

static HRESULT STDMETHODCALLTYPE mx_set_matrix(void *This, const D2D_MATRIX_3X2_F *matrix)
{
    DcTransform *t = tr_impl(This);
    if (!matrix)
        return E_INVALIDARG;
    DcDeviceLock(t->base.device);
    const float *f = &matrix->_11;
    for (UINT i = 0; i < 6; i++) {
        DcFloatClear(&t->pending.p[i]);
        t->pending.p[i].value = f[i];
    }
    DcObjectDirty(&t->base);
    DcDeviceUnlock(t->base.device);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mx_set_element_animation(void *This, int row, int column,
                                                          IDCompositionAnimation *animation)
{
    if ((UINT)row > 2 || (UINT)column > 1)
        return E_INVALIDARG;
    return tr_set_animation(This, column + 2 * row, animation);
}

static HRESULT STDMETHODCALLTYPE mx_set_element(void *This, int row, int column, float value)
{
    if ((UINT)row > 2 || (UINT)column > 1)
        return E_INVALIDARG;
    return tr_set(This, column + 2 * row, value);
}

static HRESULT STDMETHODCALLTYPE mx3_set_matrix(void *This, const D3DMATRIX *matrix)
{
    DcTransform *t = tr_impl(This);
    if (!matrix)
        return E_INVALIDARG;
    DcDeviceLock(t->base.device);
    for (UINT i = 0; i < 16; i++) {
        DcFloatClear(&t->pending.p[i]);
        t->pending.p[i].value = matrix->m[i / 4][i % 4];
    }
    DcObjectDirty(&t->base);
    DcDeviceUnlock(t->base.device);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE mx3_set_element_animation(void *This, int row, int column,
                                                           IDCompositionAnimation *animation)
{
    if ((UINT)row > 3 || (UINT)column > 3)
        return E_INVALIDARG;
    return tr_set_animation(This, column + 4 * row, animation);
}

static HRESULT STDMETHODCALLTYPE mx3_set_element(void *This, int row, int column, float value)
{
    if ((UINT)row > 3 || (UINT)column > 3)
        return E_INVALIDARG;
    return tr_set(This, column + 4 * row, value);
}

static const IDCompositionTranslateTransformVtbl translate_vtbl = {
    VTBL_FN(tr_query), VTBL_FN(tr_add_ref), VTBL_FN(tr_release), VTBL_FN(tl_offset_x_a), VTBL_FN(tl_offset_x),
    VTBL_FN(tl_offset_y_a), VTBL_FN(tl_offset_y),
};

static const IDCompositionScaleTransformVtbl scale_vtbl = {
    VTBL_FN(tr_query), VTBL_FN(tr_add_ref), VTBL_FN(tr_release), VTBL_FN(sc_scale_x_a), VTBL_FN(sc_scale_x),
    VTBL_FN(sc_scale_y_a), VTBL_FN(sc_scale_y), VTBL_FN(sc_center_x_a), VTBL_FN(sc_center_x),
    VTBL_FN(sc_center_y_a), VTBL_FN(sc_center_y),
};

static const IDCompositionRotateTransformVtbl rotate_vtbl = {
    VTBL_FN(tr_query), VTBL_FN(tr_add_ref), VTBL_FN(tr_release), VTBL_FN(ro_angle_a), VTBL_FN(ro_angle),
    VTBL_FN(ro_center_x_a), VTBL_FN(ro_center_x), VTBL_FN(ro_center_y_a), VTBL_FN(ro_center_y),
};

static const IDCompositionSkewTransformVtbl skew_vtbl = {
    VTBL_FN(tr_query), VTBL_FN(tr_add_ref), VTBL_FN(tr_release), VTBL_FN(sk_angle_x_a), VTBL_FN(sk_angle_x),
    VTBL_FN(sk_angle_y_a), VTBL_FN(sk_angle_y), VTBL_FN(sk_center_x_a), VTBL_FN(sk_center_x),
    VTBL_FN(sk_center_y_a), VTBL_FN(sk_center_y),
};

static const IDCompositionMatrixTransformVtbl matrix_vtbl = {
    VTBL_FN(tr_query), VTBL_FN(tr_add_ref), VTBL_FN(tr_release), VTBL_FN(mx_set_matrix),
    VTBL_FN(mx_set_element_animation), VTBL_FN(mx_set_element),
};

static const IDCompositionTransformVtbl group_vtbl = {
    VTBL_FN(tr_query),
    VTBL_FN(tr_add_ref),
    VTBL_FN(tr_release),
};

static const IDCompositionTranslateTransform3DVtbl translate3d_vtbl = {
    VTBL_FN(tr_query), VTBL_FN(tr_add_ref), VTBL_FN(tr_release), VTBL_FN(tl3_offset_x_a), VTBL_FN(tl3_offset_x),
    VTBL_FN(tl3_offset_y_a), VTBL_FN(tl3_offset_y), VTBL_FN(tl3_offset_z_a), VTBL_FN(tl3_offset_z),
};

static const IDCompositionScaleTransform3DVtbl scale3d_vtbl = {
    VTBL_FN(tr_query), VTBL_FN(tr_add_ref), VTBL_FN(tr_release), VTBL_FN(sc3_scale_x_a), VTBL_FN(sc3_scale_x),
    VTBL_FN(sc3_scale_y_a), VTBL_FN(sc3_scale_y), VTBL_FN(sc3_scale_z_a), VTBL_FN(sc3_scale_z),
    VTBL_FN(sc3_center_x_a), VTBL_FN(sc3_center_x), VTBL_FN(sc3_center_y_a), VTBL_FN(sc3_center_y),
    VTBL_FN(sc3_center_z_a), VTBL_FN(sc3_center_z),
};

static const IDCompositionRotateTransform3DVtbl rotate3d_vtbl = {
    VTBL_FN(tr_query), VTBL_FN(tr_add_ref), VTBL_FN(tr_release), VTBL_FN(ro3_angle_a), VTBL_FN(ro3_angle),
    VTBL_FN(ro3_axis_x_a), VTBL_FN(ro3_axis_x), VTBL_FN(ro3_axis_y_a), VTBL_FN(ro3_axis_y), VTBL_FN(ro3_axis_z_a),
    VTBL_FN(ro3_axis_z), VTBL_FN(ro3_center_x_a), VTBL_FN(ro3_center_x), VTBL_FN(ro3_center_y_a),
    VTBL_FN(ro3_center_y), VTBL_FN(ro3_center_z_a), VTBL_FN(ro3_center_z),
};

static const IDCompositionMatrixTransform3DVtbl matrix3d_vtbl = {
    VTBL_FN(tr_query), VTBL_FN(tr_add_ref), VTBL_FN(tr_release), VTBL_FN(mx3_set_matrix),
    VTBL_FN(mx3_set_element_animation), VTBL_FN(mx3_set_element),
};

static const IDCompositionTransform3DVtbl group3d_vtbl = {
    VTBL_FN(tr_query),
    VTBL_FN(tr_add_ref),
    VTBL_FN(tr_release),
};

HRESULT DcCreateTransform(DcDevice *device, DcTransformKind kind, void **out)
{
    static const void *const vtables[] = {&translate_vtbl, &scale_vtbl,       &rotate_vtbl,   &skew_vtbl,
                                          &matrix_vtbl,    &group_vtbl,       &translate3d_vtbl, &scale3d_vtbl,
                                          &rotate3d_vtbl,  &matrix3d_vtbl,    &group3d_vtbl};
    DcTransform *t = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*t));
    if (!t)
        return E_OUTOFMEMORY;
    t->iface.vtbl = vtables[kind];
    t->kind = kind;
    DcObjectInit(&t->base, device, &tr_class);
    /* a scale starts at one, a matrix at the identity, a rotation around the z axis. */
    DcTransformState *s = &t->pending;
    switch (kind) {
    case DCT_SCALE:
        s->p[0].value = s->p[1].value = 1.0f;
        break;
    case DCT_MATRIX:
        s->p[0].value = s->p[3].value = 1.0f;
        break;
    case DCT_SCALE3D:
        s->p[0].value = s->p[1].value = s->p[2].value = 1.0f;
        break;
    case DCT_ROTATE3D:
        s->p[3].value = 1.0f;
        break;
    case DCT_MATRIX3D:
        s->p[0].value = s->p[5].value = s->p[10].value = s->p[15].value = 1.0f;
        break;
    default:
        break;
    }
    DcDeviceLock(device);
    DcObjectDirty(&t->base);
    DcDeviceUnlock(device);
    *out = &t->iface;
    return S_OK;
}

/* a group lists transforms of its device; one of 2d accepts no 3d transform. */
HRESULT DcCreateTransformGroup(DcDevice *device, BOOL three, void *const *transforms, UINT count, void **out)
{
    if (!transforms || !out)
        return E_INVALIDARG;
    *out = nullptr;
    DcObject **list = nullptr;
    if (count) {
        list = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, count * sizeof(*list));
        if (!list)
            return E_OUTOFMEMORY;
    }
    HRESULT hr = S_OK;
    for (UINT i = 0; i < count && SUCCEEDED(hr); i++) {
        DcObject *member = transforms[i] ? DcObjectFrom(transforms[i]) : nullptr;
        if (!member || member->cls->kind != DC_TRANSFORM || member->device != device ||
            (!three && three_d(CONTAINING_RECORD(member, DcTransform, base)->kind))) {
            hr = E_INVALIDARG;
            if (member)
                DcObjectRelease(member);
        } else {
            list[i] = member;
        }
    }
    if (FAILED(hr)) {
        for (UINT i = 0; i < count; i++)
            if (list[i])
                DcObjectRelease(list[i]);
        if (list)
            HeapFree(GetProcessHeap(), 0, list);
        return hr;
    }
    void *iface;
    DcCreateTransform(device, three ? DCT_GROUP3D : DCT_GROUP, &iface);
    DcTransform *t = tr_impl(iface);
    t->pending.list = list;
    t->pending.count = count;
    *out = iface;
    return S_OK;
}

/* the matrix of a committed transform at a time. */
BOOL DcTransformMatrix(const DcObject *object, UINT64 now, float m[16])
{
    if (!object || object->cls->kind != DC_TRANSFORM)
        return FALSE;
    const DcTransform *t = CONTAINING_RECORD(object, DcTransform, base);
    const DcTransformState *s = &t->committed;
    float v[DC_TRANSFORM_FLOATS];
    for (UINT i = 0; i < DC_TRANSFORM_FLOATS; i++)
        v[i] = DcFloatValue(&s->p[i], now);
    DcMatrixIdentity(m);
    switch (t->kind) {
    case DCT_TRANSLATE:
        m[12] = v[0];
        m[13] = v[1];
        break;
    case DCT_SCALE:
        m[0] = v[0];
        m[5] = v[1];
        m[12] = v[2] - v[0] * v[2];
        m[13] = v[3] - v[1] * v[3];
        break;
    case DCT_ROTATE: {
        float c = (float)DcCos(v[0] * DC_PI / 180.0), sn = (float)DcSin(v[0] * DC_PI / 180.0);
        m[0] = c;
        m[1] = sn;
        m[4] = -sn;
        m[5] = c;
        m[12] = v[1] - (v[1] * c - v[2] * sn);
        m[13] = v[2] - (v[1] * sn + v[2] * c);
        break;
    }
    case DCT_SKEW: {
        float tx = (float)DcTan(v[0] * DC_PI / 180.0), ty = (float)DcTan(v[1] * DC_PI / 180.0);
        m[1] = ty;
        m[4] = tx;
        m[12] = v[2] - (v[2] + v[3] * tx);
        m[13] = v[3] - (v[2] * ty + v[3]);
        break;
    }
    case DCT_MATRIX:
        m[0] = v[0];
        m[1] = v[1];
        m[4] = v[2];
        m[5] = v[3];
        m[12] = v[4];
        m[13] = v[5];
        break;
    case DCT_TRANSLATE3D:
        m[12] = v[0];
        m[13] = v[1];
        m[14] = v[2];
        break;
    case DCT_SCALE3D:
        m[0] = v[0];
        m[5] = v[1];
        m[10] = v[2];
        m[12] = v[3] - v[0] * v[3];
        m[13] = v[4] - v[1] * v[4];
        m[14] = v[5] - v[2] * v[5];
        break;
    case DCT_ROTATE3D: {
        double len = DcSqrt((double)v[1] * v[1] + (double)v[2] * v[2] + (double)v[3] * v[3]);
        if (len > 0.0) {
            float x = (float)(v[1] / len), y = (float)(v[2] / len), z = (float)(v[3] / len);
            float c = (float)DcCos(v[0] * DC_PI / 180.0), sn = (float)DcSin(v[0] * DC_PI / 180.0), k = 1.0f - c;
            m[0] = k * x * x + c;
            m[1] = k * x * y + sn * z;
            m[2] = k * x * z - sn * y;
            m[4] = k * x * y - sn * z;
            m[5] = k * y * y + c;
            m[6] = k * y * z + sn * x;
            m[8] = k * x * z + sn * y;
            m[9] = k * y * z - sn * x;
            m[10] = k * z * z + c;
            float cx = v[4], cy = v[5], cz = v[6];
            m[12] = cx - (cx * m[0] + cy * m[4] + cz * m[8]);
            m[13] = cy - (cx * m[1] + cy * m[5] + cz * m[9]);
            m[14] = cz - (cx * m[2] + cy * m[6] + cz * m[10]);
        }
        break;
    }
    case DCT_MATRIX3D:
        memcpy(m, v, sizeof(v));
        break;
    case DCT_GROUP:
    case DCT_GROUP3D:
        for (UINT i = 0; i < s->count; i++) {
            float member[16];
            if (DcTransformMatrix(s->list[i], now, member))
                DcMatrixMultiply(m, m, member);
        }
        break;
    }
    return TRUE;
}

/* ------------------------------------------------------------------ effect groups */

static DcEffectGroup *eg_impl(IDCompositionEffectGroup *iface) { return CONTAINING_RECORD(iface, DcEffectGroup, iface); }

static void eg_commit(DcObject *object)
{
    DcEffectGroup *g = CONTAINING_RECORD(object, DcEffectGroup, base);
    DcFloatCopy(&g->committed.opacity, &g->pending.opacity);
    assign(&g->committed.transform3d, g->pending.transform3d);
}

static void eg_destroy(DcObject *object)
{
    DcEffectGroup *g = CONTAINING_RECORD(object, DcEffectGroup, base);
    DcFloatClear(&g->pending.opacity);
    DcFloatClear(&g->committed.opacity);
    assign(&g->pending.transform3d, nullptr);
    assign(&g->committed.transform3d, nullptr);
    HeapFree(GetProcessHeap(), 0, g);
}

static const DcClass eg_class = {DC_EFFECT_GROUP, eg_commit, eg_destroy};

static HRESULT STDMETHODCALLTYPE eg_query(IDCompositionEffectGroup *iface, REFIID riid, void **out)
{
    DcEffectGroup *g = eg_impl(iface);
    DcInterface interfaces[] = {{&IID_IDCompositionEffect, iface}, {&IID_IDCompositionEffectGroup, iface}};
    return DcQuery(&g->base, interfaces, 2, riid, out);
}

static ULONG STDMETHODCALLTYPE eg_add_ref(IDCompositionEffectGroup *iface)
{
    return DcObjectAddRef(&eg_impl(iface)->base);
}

static ULONG STDMETHODCALLTYPE eg_release(IDCompositionEffectGroup *iface)
{
    return DcObjectRelease(&eg_impl(iface)->base);
}

static HRESULT STDMETHODCALLTYPE eg_set_opacity_animation(IDCompositionEffectGroup *iface,
                                                          IDCompositionAnimation *animation)
{
    DcEffectGroup *g = eg_impl(iface);
    return set_animation(&g->base, &g->pending.opacity, animation);
}

static HRESULT STDMETHODCALLTYPE eg_set_opacity(IDCompositionEffectGroup *iface, float opacity)
{
    DcEffectGroup *g = eg_impl(iface);
    return set_value(&g->base, &g->pending.opacity, opacity);
}

static HRESULT STDMETHODCALLTYPE eg_set_transform3d(IDCompositionEffectGroup *iface, IDCompositionTransform3D *transform)
{
    DcEffectGroup *g = eg_impl(iface);
    DcObject *object = nullptr;
    if (transform) {
        object = DcObjectFrom((IUnknown *)transform);
        if (!object || object->cls->kind != DC_TRANSFORM || object->device != g->base.device) {
            if (object)
                DcObjectRelease(object);
            return E_INVALIDARG;
        }
    }
    DcDeviceLock(g->base.device);
    assign(&g->pending.transform3d, object);
    DcObjectDirty(&g->base);
    DcDeviceUnlock(g->base.device);
    if (object)
        DcObjectRelease(object);
    return S_OK;
}

static const IDCompositionEffectGroupVtbl eg_vtbl = {
    eg_query, eg_add_ref, eg_release, eg_set_opacity_animation, eg_set_opacity, eg_set_transform3d,
};

HRESULT DcCreateEffectGroup(DcDevice *device, IDCompositionEffectGroup **out)
{
    DcEffectGroup *g = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*g));
    if (!g)
        return E_OUTOFMEMORY;
    g->iface.lpVtbl = &eg_vtbl;
    g->pending.opacity.value = 1.0f;
    DcObjectInit(&g->base, device, &eg_class);
    DcDeviceLock(device);
    DcObjectDirty(&g->base);
    DcDeviceUnlock(device);
    *out = &g->iface;
    return S_OK;
}

BOOL DcEffectOpacity(const DcObject *effect, UINT64 now, float *opacity, float transform3d[16], BOOL *has_transform)
{
    *opacity = 1.0f;
    *has_transform = FALSE;
    if (!effect)
        return FALSE;
    if (effect->cls->kind == DC_EFFECT_GROUP) {
        const DcEffectGroup *g = CONTAINING_RECORD(effect, DcEffectGroup, base);
        *opacity = DcFloatValue(&g->committed.opacity, now);
        if (g->committed.transform3d && DcTransformMatrix(g->committed.transform3d, now, transform3d))
            *has_transform = TRUE;
        return TRUE;
    }
    if (effect->cls->kind == DC_TRANSFORM && DcTransformMatrix(effect, now, transform3d)) {
        *has_transform = TRUE;
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------ rectangle clips */

static DcClip *clip_impl(IDCompositionRectangleClip *iface) { return CONTAINING_RECORD(iface, DcClip, iface); }

static void clip_commit(DcObject *object)
{
    DcClip *c = CONTAINING_RECORD(object, DcClip, base);
    for (UINT i = 0; i < DC_CLIP_FLOATS; i++)
        DcFloatCopy(&c->committed[i], &c->pending[i]);
}

static void clip_destroy(DcObject *object)
{
    DcClip *c = CONTAINING_RECORD(object, DcClip, base);
    for (UINT i = 0; i < DC_CLIP_FLOATS; i++) {
        DcFloatClear(&c->pending[i]);
        DcFloatClear(&c->committed[i]);
    }
    HeapFree(GetProcessHeap(), 0, c);
}

static const DcClass clip_class = {DC_CLIP, clip_commit, clip_destroy};

static HRESULT STDMETHODCALLTYPE clip_query(IDCompositionRectangleClip *iface, REFIID riid, void **out)
{
    DcClip *c = clip_impl(iface);
    DcInterface interfaces[] = {{&IID_IDCompositionClip, iface}, {&IID_IDCompositionRectangleClip, iface}};
    return DcQuery(&c->base, interfaces, 2, riid, out);
}

static ULONG STDMETHODCALLTYPE clip_add_ref(IDCompositionRectangleClip *iface)
{
    return DcObjectAddRef(&clip_impl(iface)->base);
}

static ULONG STDMETHODCALLTYPE clip_release(IDCompositionRectangleClip *iface)
{
    return DcObjectRelease(&clip_impl(iface)->base);
}

#define CLIP_PROPERTY(name, index)                                                                                     \
    static HRESULT STDMETHODCALLTYPE clip_##name##_a(IDCompositionRectangleClip *iface,                                \
                                                     IDCompositionAnimation *animation)                                \
    {                                                                                                                  \
        DcClip *c = clip_impl(iface);                                                                                  \
        return set_animation(&c->base, &c->pending[index], animation);                                                 \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE clip_##name(IDCompositionRectangleClip *iface, float value)                       \
    {                                                                                                                  \
        DcClip *c = clip_impl(iface);                                                                                  \
        return set_value(&c->base, &c->pending[index], value);                                                         \
    }

CLIP_PROPERTY(left, 0)
CLIP_PROPERTY(top, 1)
CLIP_PROPERTY(right, 2)
CLIP_PROPERTY(bottom, 3)
CLIP_PROPERTY(tl_x, 4)
CLIP_PROPERTY(tl_y, 5)
CLIP_PROPERTY(tr_x, 6)
CLIP_PROPERTY(tr_y, 7)
CLIP_PROPERTY(bl_x, 8)
CLIP_PROPERTY(bl_y, 9)
CLIP_PROPERTY(br_x, 10)
CLIP_PROPERTY(br_y, 11)

static const IDCompositionRectangleClipVtbl clip_vtbl = {
    clip_query,      clip_add_ref,    clip_release,    clip_left_a,     clip_left,       clip_top_a,
    clip_top,        clip_right_a,    clip_right,      clip_bottom_a,   clip_bottom,     clip_tl_x_a,
    clip_tl_x,       clip_tl_y_a,     clip_tl_y,       clip_tr_x_a,     clip_tr_x,       clip_tr_y_a,
    clip_tr_y,       clip_bl_x_a,     clip_bl_x,       clip_bl_y_a,     clip_bl_y,       clip_br_x_a,
    clip_br_x,       clip_br_y_a,     clip_br_y,
};

HRESULT DcCreateRectangleClip(DcDevice *device, IDCompositionRectangleClip **out)
{
    DcClip *c = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*c));
    if (!c)
        return E_OUTOFMEMORY;
    c->iface.lpVtbl = &clip_vtbl;
    DcObjectInit(&c->base, device, &clip_class);
    DcDeviceLock(device);
    DcObjectDirty(&c->base);
    DcDeviceUnlock(device);
    *out = &c->iface;
    return S_OK;
}

/* the committed rectangle, then the radii of the corners in the order top left, top right, bottom left, bottom right. */
BOOL DcClipValues(const DcObject *object, UINT64 now, float rect[4], float radii[8])
{
    if (!object || object->cls->kind != DC_CLIP)
        return FALSE;
    const DcClip *c = CONTAINING_RECORD(object, DcClip, base);
    for (UINT i = 0; i < 4; i++)
        rect[i] = DcFloatValue(&c->committed[i], now);
    for (UINT i = 0; i < 8; i++)
        radii[i] = DcFloatValue(&c->committed[4 + i], now);
    return TRUE;
}

/* ------------------------------------------------------------------ filter effects */

static DcFilter *fx_impl(void *iface) { return CONTAINING_RECORD(iface, DcFilter, iface); }

static void fx_state_clear(DcFilterState *s)
{
    for (UINT i = 0; i < RTL_NUMBER_OF(s->value); i++)
        DcFloatClear(&s->value[i]);
}

static void fx_commit(DcObject *object)
{
    DcFilter *f = CONTAINING_RECORD(object, DcFilter, base);
    DcFilterState copy = f->pending;
    for (UINT i = 0; i < RTL_NUMBER_OF(copy.value); i++) {
        copy.value[i].animation = nullptr;
        DcFloatCopy(&copy.value[i], &f->pending.value[i]);
    }
    fx_state_clear(&f->committed);
    f->committed = copy;
}

static void fx_destroy(DcObject *object)
{
    DcFilter *f = CONTAINING_RECORD(object, DcFilter, base);
    fx_state_clear(&f->pending);
    fx_state_clear(&f->committed);
    HeapFree(GetProcessHeap(), 0, f);
}

static const DcClass fx_class = {DC_FILTER, fx_commit, fx_destroy};

static HRESULT STDMETHODCALLTYPE fx_query(void *This, REFIID riid, void **out)
{
    DcFilter *f = fx_impl(This);
    static const GUID *const specific[] = {&IID_IDCompositionSaturationEffect, &IID_IDCompositionColorMatrixEffect,
                                           &IID_IDCompositionTableTransferEffect, &IID_IDCompositionGaussianBlurEffect};
    DcInterface interfaces[] = {
        {&IID_IDCompositionEffect, This}, {&IID_IDCompositionFilterEffect, This}, {specific[f->kind], This}};
    return DcQuery(&f->base, interfaces, 3, riid, out);
}

static ULONG STDMETHODCALLTYPE fx_add_ref(void *This) { return DcObjectAddRef(&fx_impl(This)->base); }
static ULONG STDMETHODCALLTYPE fx_release(void *This) { return DcObjectRelease(&fx_impl(This)->base); }

/* every one of these filters takes one input, which a visual's content is. */
static HRESULT STDMETHODCALLTYPE fx_set_input(void *This, UINT index, IUnknown *input, UINT flags)
{
    return index ? E_INVALIDARG : S_OK;
}

static HRESULT STDMETHODCALLTYPE fx_set_value(void *This, UINT index, float value)
{
    DcFilter *f = fx_impl(This);
    return set_value(&f->base, &f->pending.value[index], value);
}

static HRESULT STDMETHODCALLTYPE fx_set_animation(void *This, UINT index, IDCompositionAnimation *animation)
{
    DcFilter *f = fx_impl(This);
    return set_animation(&f->base, &f->pending.value[index], animation);
}

static HRESULT STDMETHODCALLTYPE fx_saturation_a(void *This, IDCompositionAnimation *animation)
{
    return fx_set_animation(This, 0, animation);
}

static HRESULT STDMETHODCALLTYPE fx_saturation(void *This, float value) { return fx_set_value(This, 0, value); }

static HRESULT STDMETHODCALLTYPE fx_cm_set_matrix(void *This, const D2D_MATRIX_5X4_F *matrix)
{
    DcFilter *f = fx_impl(This);
    if (!matrix)
        return E_INVALIDARG;
    DcDeviceLock(f->base.device);
    for (UINT i = 0; i < 20; i++) {
        DcFloatClear(&f->pending.value[i]);
        f->pending.value[i].value = matrix->m[i / 4][i % 4];
    }
    DcObjectDirty(&f->base);
    DcDeviceUnlock(f->base.device);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fx_cm_element_a(void *This, int row, int column, IDCompositionAnimation *animation)
{
    if ((UINT)row > 4 || (UINT)column > 3)
        return E_INVALIDARG;
    return fx_set_animation(This, column + 4 * row, animation);
}

static HRESULT STDMETHODCALLTYPE fx_cm_element(void *This, int row, int column, float value)
{
    if ((UINT)row > 4 || (UINT)column > 3)
        return E_INVALIDARG;
    return fx_set_value(This, column + 4 * row, value);
}

static HRESULT STDMETHODCALLTYPE fx_cm_alpha_mode(void *This, D2D1_COLORMATRIX_ALPHA_MODE mode)
{
    DcFilter *f = fx_impl(This);
    if (mode != D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED && mode != D2D1_COLORMATRIX_ALPHA_MODE_STRAIGHT)
        return E_INVALIDARG;
    DcDeviceLock(f->base.device);
    f->pending.alpha_mode = mode;
    DcObjectDirty(&f->base);
    DcDeviceUnlock(f->base.device);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fx_clamp(void *This, BOOL clamp)
{
    DcFilter *f = fx_impl(This);
    DcDeviceLock(f->base.device);
    f->pending.clamp = !!clamp;
    DcObjectDirty(&f->base);
    DcDeviceUnlock(f->base.device);
    return S_OK;
}

static HRESULT table_set(void *This, UINT channel, const float *values, UINT count)
{
    DcFilter *f = fx_impl(This);
    if (!values || !count || count > RTL_NUMBER_OF(f->pending.table[0]))
        return E_INVALIDARG;
    DcDeviceLock(f->base.device);
    memcpy(f->pending.table[channel], values, count * sizeof(float));
    f->pending.table_count[channel] = count;
    DcObjectDirty(&f->base);
    DcDeviceUnlock(f->base.device);
    return S_OK;
}

static HRESULT table_disable(void *This, UINT channel, BOOL disable)
{
    DcFilter *f = fx_impl(This);
    DcDeviceLock(f->base.device);
    f->pending.table_disable[channel] = !!disable;
    DcObjectDirty(&f->base);
    DcDeviceUnlock(f->base.device);
    return S_OK;
}

#define TABLE_CHANNEL(name, channel)                                                                                   \
    static HRESULT STDMETHODCALLTYPE fx_##name##_table(void *This, const float *values, UINT count)                    \
    {                                                                                                                  \
        return table_set(This, channel, values, count);                                                                \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE fx_##name##_disable(void *This, BOOL disable)                                     \
    {                                                                                                                  \
        return table_disable(This, channel, disable);                                                                  \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE fx_##name##_value_a(void *This, UINT index, IDCompositionAnimation *animation)    \
    {                                                                                                                  \
        return index < 64 ? S_OK : E_INVALIDARG;                                                                       \
    }                                                                                                                  \
    static HRESULT STDMETHODCALLTYPE fx_##name##_value(void *This, UINT index, float value)                            \
    {                                                                                                                  \
        DcFilter *f = fx_impl(This);                                                                                   \
        if (index >= f->pending.table_count[channel] || !finite(value))                                                \
            return E_INVALIDARG;                                                                                       \
        DcDeviceLock(f->base.device);                                                                                  \
        f->pending.table[channel][index] = value;                                                                      \
        DcObjectDirty(&f->base);                                                                                       \
        DcDeviceUnlock(f->base.device);                                                                                \
        return S_OK;                                                                                                   \
    }

TABLE_CHANNEL(red, 0)
TABLE_CHANNEL(green, 1)
TABLE_CHANNEL(blue, 2)
TABLE_CHANNEL(alpha, 3)

static HRESULT STDMETHODCALLTYPE fx_blur_deviation_a(void *This, IDCompositionAnimation *animation)
{
    return fx_set_animation(This, 0, animation);
}

static HRESULT STDMETHODCALLTYPE fx_blur_deviation(void *This, float value)
{
    if (value < 0.0f)
        return E_INVALIDARG;
    return fx_set_value(This, 0, value);
}

static HRESULT STDMETHODCALLTYPE fx_blur_border_mode(void *This, D2D1_BORDER_MODE mode)
{
    DcFilter *f = fx_impl(This);
    if (mode != D2D1_BORDER_MODE_SOFT && mode != D2D1_BORDER_MODE_HARD)
        return E_INVALIDARG;
    DcDeviceLock(f->base.device);
    f->pending.border_mode = mode;
    DcObjectDirty(&f->base);
    DcDeviceUnlock(f->base.device);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE fx_table_clamp(void *This, BOOL clamp) { return fx_clamp(This, clamp); }

static const IDCompositionSaturationEffectVtbl saturation_vtbl = {
    VTBL_FN(fx_query), VTBL_FN(fx_add_ref), VTBL_FN(fx_release), VTBL_FN(fx_set_input), VTBL_FN(fx_saturation_a),
    VTBL_FN(fx_saturation),
};

static const IDCompositionColorMatrixEffectVtbl color_matrix_vtbl = {
    VTBL_FN(fx_query), VTBL_FN(fx_add_ref), VTBL_FN(fx_release), VTBL_FN(fx_set_input), VTBL_FN(fx_cm_set_matrix),
    VTBL_FN(fx_cm_element_a), VTBL_FN(fx_cm_element), VTBL_FN(fx_cm_alpha_mode), VTBL_FN(fx_clamp),
};

static const IDCompositionTableTransferEffectVtbl table_vtbl = {
    VTBL_FN(fx_query), VTBL_FN(fx_add_ref), VTBL_FN(fx_release), VTBL_FN(fx_set_input), VTBL_FN(fx_red_table),
    VTBL_FN(fx_green_table), VTBL_FN(fx_blue_table), VTBL_FN(fx_alpha_table), VTBL_FN(fx_red_disable),
    VTBL_FN(fx_green_disable), VTBL_FN(fx_blue_disable), VTBL_FN(fx_alpha_disable), VTBL_FN(fx_table_clamp),
    VTBL_FN(fx_red_value_a), VTBL_FN(fx_red_value), VTBL_FN(fx_green_value_a), VTBL_FN(fx_green_value),
    VTBL_FN(fx_blue_value_a), VTBL_FN(fx_blue_value), VTBL_FN(fx_alpha_value_a), VTBL_FN(fx_alpha_value),
};

static const IDCompositionGaussianBlurEffectVtbl blur_vtbl = {
    VTBL_FN(fx_query), VTBL_FN(fx_add_ref), VTBL_FN(fx_release), VTBL_FN(fx_set_input), VTBL_FN(fx_blur_deviation_a),
    VTBL_FN(fx_blur_deviation), VTBL_FN(fx_blur_border_mode),
};

HRESULT DcCreateFilter(DcDevice *device, DcFilterKind kind, void **out)
{
    static const void *const vtables[] = {&saturation_vtbl, &color_matrix_vtbl, &table_vtbl, &blur_vtbl};
    DcFilter *f = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*f));
    if (!f)
        return E_OUTOFMEMORY;
    f->iface.vtbl = vtables[kind];
    f->kind = kind;
    DcObjectInit(&f->base, device, &fx_class);
    /* saturation starts at one, the color matrix at the identity with straight alpha, and the table at none. */
    if (kind == DCF_SATURATION)
        f->pending.value[0].value = 1.0f;
    if (kind == DCF_COLOR_MATRIX) {
        f->pending.value[0].value = f->pending.value[5].value = f->pending.value[10].value =
            f->pending.value[15].value = 1.0f;
        f->pending.alpha_mode = D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED;
    }
    DcDeviceLock(device);
    DcObjectDirty(&f->base);
    DcDeviceUnlock(device);
    *out = &f->iface;
    return S_OK;
}

/* the color transform of a committed filter: a color matrix, or the saturation as one. */
BOOL DcFilterColor(const DcObject *object, UINT64 now, float color[20])
{
    if (!object || object->cls->kind != DC_FILTER)
        return FALSE;
    const DcFilter *f = CONTAINING_RECORD(object, DcFilter, base);
    memset(color, 0, 20 * sizeof(float));
    if (f->kind == DCF_SATURATION) {
        float s = DcFloatValue(&f->committed.value[0], now);
        const float wr = 0.2125f, wg = 0.7154f, wb = 0.0721f;
        /* each channel keeps its part of the gray and the rest of itself, the alpha stays. */
        color[0] = wr * (1 - s) + s;
        color[1] = wr * (1 - s);
        color[2] = wr * (1 - s);
        color[4] = wg * (1 - s);
        color[5] = wg * (1 - s) + s;
        color[6] = wg * (1 - s);
        color[8] = wb * (1 - s);
        color[9] = wb * (1 - s);
        color[10] = wb * (1 - s) + s;
        color[15] = 1.0f;
        return TRUE;
    }
    if (f->kind == DCF_COLOR_MATRIX) {
        for (UINT i = 0; i < 20; i++)
            color[i] = DcFloatValue(&f->committed.value[i], now);
        return TRUE;
    }
    return FALSE;
}
