/*
 * visuals and targets. A visual holds the properties of a node of the tree
 * and the nodes under it; changes go to its pending state and become what the
 * compositor draws at Commit. A target binds a root visual to a window.
 */
#include "dcomp_private.h"

static BOOL finite(float v) { return v - v == 0.0f; }

/* ------------------------------------------------------------------ states */

static void assign(DcObject **slot, DcObject *object)
{
    if (object)
        DcObjectAddRef(object);
    if (*slot)
        DcObjectRelease(*slot);
    *slot = object;
}

static void assign_visual(DcVisual **slot, DcVisual *visual)
{
    if (visual)
        DcObjectAddRef(&visual->base);
    if (*slot)
        DcObjectRelease(&(*slot)->base);
    *slot = visual;
}

static void assign_content(DcContent **slot, DcContent *content)
{
    if (content)
        DcContentRef(content);
    if (*slot)
        DcContentUnref(*slot);
    *slot = content;
}

static void state_free(DcVisualState *s)
{
    DcFloatClear(&s->offset_x);
    DcFloatClear(&s->offset_y);
    DcFloatClear(&s->offset_z);
    DcFloatClear(&s->opacity);
    assign(&s->transform, nullptr);
    assign_visual(&s->transform_parent, nullptr);
    assign(&s->effect, nullptr);
    assign(&s->clip, nullptr);
    assign_content(&s->content, nullptr);
    for (UINT i = 0; i < s->child_count; i++)
        DcObjectRelease(&s->children[i]->base);
    if (s->children)
        HeapFree(GetProcessHeap(), 0, s->children);
    s->children = nullptr;
    s->child_count = s->child_capacity = 0;
}

static DcVisual *vis_impl(void *iface) { return CONTAINING_RECORD(iface, DcVisual, iface); }

static void vis_commit(DcObject *object)
{
    DcVisual *v = CONTAINING_RECORD(object, DcVisual, base);
    DcVisualState *src = &v->pending, *dst = &v->committed;
    DcVisual **children = nullptr;
    if (src->child_count) {
        children = HeapAlloc(GetProcessHeap(), 0, src->child_count * sizeof(*children));
        if (!children)
            return;
        memcpy(children, src->children, src->child_count * sizeof(*children));
    }
    DcVisualState old = *dst;
    *dst = *src;
    /* the new state takes references of its own to everything it refers to. */
    dst->offset_x.animation = dst->offset_y.animation = dst->offset_z.animation = dst->opacity.animation = nullptr;
    DcFloatCopy(&dst->offset_x, &src->offset_x);
    DcFloatCopy(&dst->offset_y, &src->offset_y);
    DcFloatCopy(&dst->offset_z, &src->offset_z);
    DcFloatCopy(&dst->opacity, &src->opacity);
    if (dst->transform)
        DcObjectAddRef(dst->transform);
    if (dst->transform_parent)
        DcObjectAddRef(&dst->transform_parent->base);
    if (dst->effect)
        DcObjectAddRef(dst->effect);
    if (dst->clip)
        DcObjectAddRef(dst->clip);
    if (dst->content)
        DcContentRef(dst->content);
    dst->children = children;
    dst->child_capacity = src->child_count;
    for (UINT i = 0; i < dst->child_count; i++)
        DcObjectAddRef(&children[i]->base);
    state_free(&old);
    /* the targets that show this visual's tree draw it again. */
    for (DcTarget *t = object->device->targets; t; t = t->next_in_device)
        t->needs_compose = TRUE;
}

static void vis_destroy(DcObject *object)
{
    DcVisual *v = CONTAINING_RECORD(object, DcVisual, base);
    /* a child that outlives its parent has none. */
    for (UINT i = 0; i < v->pending.child_count; i++)
        if (v->pending.children[i]->parent == v)
            v->pending.children[i]->parent = nullptr;
    state_free(&v->pending);
    state_free(&v->committed);
    HeapFree(GetProcessHeap(), 0, v);
}

static const DcClass vis_class = {DC_VISUAL, vis_commit, vis_destroy};

static DcVisual *visual_from(IUnknown *iface, DcDevice *device)
{
    if (!iface || iface->lpVtbl != (const IUnknownVtbl *)&g_visual_vtbl)
        return nullptr;
    DcVisual *v = vis_impl(iface);
    return v->base.device == device ? v : nullptr;
}

/* ------------------------------------------------------------------ IUnknown */

static HRESULT STDMETHODCALLTYPE vis_query(void *This, REFIID riid, void **out)
{
    DcVisual *v = vis_impl(This);
    DcInterface interfaces[] = {{&IID_IDCompositionVisual, This},      {&IID_IDCompositionVisual2, This},
                                {&IID_IDCompositionVisualDebug, This}, {&IID_IDCompositionVisual3, This}};
    return DcQuery(&v->base, interfaces, 4, riid, out);
}

static ULONG STDMETHODCALLTYPE vis_add_ref(void *This) { return DcObjectAddRef(&vis_impl(This)->base); }
static ULONG STDMETHODCALLTYPE vis_release(void *This) { return DcObjectRelease(&vis_impl(This)->base); }

/* ------------------------------------------------------------------ properties */

static HRESULT set_float(DcVisual *v, DcFloat *slot, float value)
{
    if (!finite(value))
        return E_INVALIDARG;
    DcDeviceLock(v->base.device);
    DcFloatClear(slot);
    slot->value = value;
    DcObjectDirty(&v->base);
    DcDeviceUnlock(v->base.device);
    return S_OK;
}

static HRESULT set_animation(DcVisual *v, DcFloat *slot, IDCompositionAnimation *animation)
{
    DcAnimation *a = DcAnimationFrom(animation);
    if (!a || a->base.device != v->base.device)
        return E_INVALIDARG;
    DcDeviceLock(v->base.device);
    DcObjectAddRef(&a->base);
    DcFloatClear(slot);
    slot->animation = a;
    DcObjectDirty(&v->base);
    DcDeviceUnlock(v->base.device);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vis_set_offset_x_a(void *This, IDCompositionAnimation *animation)
{
    DcVisual *v = vis_impl(This);
    return set_animation(v, &v->pending.offset_x, animation);
}

static HRESULT STDMETHODCALLTYPE vis_set_offset_x(void *This, float offset)
{
    DcVisual *v = vis_impl(This);
    return set_float(v, &v->pending.offset_x, offset);
}

static HRESULT STDMETHODCALLTYPE vis_set_offset_y_a(void *This, IDCompositionAnimation *animation)
{
    DcVisual *v = vis_impl(This);
    return set_animation(v, &v->pending.offset_y, animation);
}

static HRESULT STDMETHODCALLTYPE vis_set_offset_y(void *This, float offset)
{
    DcVisual *v = vis_impl(This);
    return set_float(v, &v->pending.offset_y, offset);
}

static HRESULT STDMETHODCALLTYPE vis_set_offset_z_a(void *This, IDCompositionAnimation *animation)
{
    DcVisual *v = vis_impl(This);
    return set_animation(v, &v->pending.offset_z, animation);
}

static HRESULT STDMETHODCALLTYPE vis_set_offset_z(void *This, float offset)
{
    DcVisual *v = vis_impl(This);
    return set_float(v, &v->pending.offset_z, offset);
}

static HRESULT STDMETHODCALLTYPE vis_set_opacity_a(void *This, IDCompositionAnimation *animation)
{
    DcVisual *v = vis_impl(This);
    return set_animation(v, &v->pending.opacity, animation);
}

static HRESULT STDMETHODCALLTYPE vis_set_opacity(void *This, float opacity)
{
    DcVisual *v = vis_impl(This);
    return set_float(v, &v->pending.opacity, opacity);
}

/* a transform given as an object of the visual's device, or none. */
static HRESULT set_transform_object(DcVisual *v, IUnknown *transform)
{
    DcObject *object = nullptr;
    if (transform) {
        object = DcObjectFrom(transform);
        if (!object || object->cls->kind != DC_TRANSFORM || object->device != v->base.device) {
            if (object)
                DcObjectRelease(object);
            return E_INVALIDARG;
        }
    }
    DcDeviceLock(v->base.device);
    assign(&v->pending.transform, object);
    v->pending.transform_kind = object ? 1 : 0;
    DcObjectDirty(&v->base);
    DcDeviceUnlock(v->base.device);
    if (object)
        DcObjectRelease(object);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vis_set_transform_object(void *This, IDCompositionTransform *transform)
{
    return set_transform_object(vis_impl(This), (IUnknown *)transform);
}

static HRESULT STDMETHODCALLTYPE vis_set_transform3d_object(void *This, IDCompositionTransform3D *transform)
{
    return set_transform_object(vis_impl(This), (IUnknown *)transform);
}

static HRESULT STDMETHODCALLTYPE vis_set_transform(void *This, const D2D_MATRIX_3X2_F *matrix)
{
    DcVisual *v = vis_impl(This);
    if (!matrix)
        return E_INVALIDARG;
    DcDeviceLock(v->base.device);
    assign(&v->pending.transform, nullptr);
    v->pending.transform_kind = 2;
    DcMatrixFrom3x2(v->pending.matrix, matrix);
    DcObjectDirty(&v->base);
    DcDeviceUnlock(v->base.device);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vis_set_transform3d(void *This, const D2D_MATRIX_4X4_F *matrix)
{
    DcVisual *v = vis_impl(This);
    if (!matrix)
        return E_INVALIDARG;
    DcDeviceLock(v->base.device);
    assign(&v->pending.transform, nullptr);
    v->pending.transform_kind = 3;
    memcpy(v->pending.matrix, matrix->m, sizeof(v->pending.matrix));
    DcObjectDirty(&v->base);
    DcDeviceUnlock(v->base.device);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vis_set_transform_parent(void *This, IDCompositionVisual *visual)
{
    DcVisual *v = vis_impl(This), *parent = nullptr;
    if (visual) {
        parent = visual_from((IUnknown *)visual, v->base.device);
        if (!parent)
            return E_INVALIDARG;
    }
    DcDeviceLock(v->base.device);
    assign_visual(&v->pending.transform_parent, parent);
    DcObjectDirty(&v->base);
    DcDeviceUnlock(v->base.device);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vis_set_effect(void *This, IDCompositionEffect *effect)
{
    DcVisual *v = vis_impl(This);
    DcObject *object = nullptr;
    if (effect) {
        object = DcObjectFrom((IUnknown *)effect);
        if (!object || object->device != v->base.device ||
            (object->cls->kind != DC_EFFECT_GROUP && object->cls->kind != DC_FILTER &&
             object->cls->kind != DC_TRANSFORM)) {
            if (object)
                DcObjectRelease(object);
            return E_INVALIDARG;
        }
    }
    DcDeviceLock(v->base.device);
    assign(&v->pending.effect, object);
    DcObjectDirty(&v->base);
    DcDeviceUnlock(v->base.device);
    if (object)
        DcObjectRelease(object);
    return S_OK;
}

static HRESULT set_enum(DcVisual *v, int *slot, int value, const int *valid, UINT count)
{
    BOOL ok = FALSE;
    for (UINT i = 0; i < count; i++)
        ok |= valid[i] == value;
    if (!ok)
        return E_INVALIDARG;
    DcDeviceLock(v->base.device);
    *slot = value;
    DcObjectDirty(&v->base);
    DcDeviceUnlock(v->base.device);
    return S_OK;
}

static const int MODES_01[] = {-1, 0, 1};
static const int MODES_012[] = {-1, 0, 1, 2};
static const int MODES_DEPTH[] = {-1, 0, 1, 3};

static HRESULT STDMETHODCALLTYPE vis_set_bitmap_interpolation_mode(void *This, DCOMPOSITION_BITMAP_INTERPOLATION_MODE m)
{
    DcVisual *v = vis_impl(This);
    return set_enum(v, &v->pending.interpolation, m, MODES_01, 3);
}

static HRESULT STDMETHODCALLTYPE vis_set_border_mode(void *This, DCOMPOSITION_BORDER_MODE m)
{
    DcVisual *v = vis_impl(This);
    return set_enum(v, &v->pending.border_mode, m, MODES_01, 3);
}

static HRESULT STDMETHODCALLTYPE vis_set_composite_mode(void *This, DCOMPOSITION_COMPOSITE_MODE m)
{
    DcVisual *v = vis_impl(This);
    return set_enum(v, &v->pending.composite_mode, m, MODES_012, 4);
}

static HRESULT STDMETHODCALLTYPE vis_set_opacity_mode(void *This, DCOMPOSITION_OPACITY_MODE m)
{
    DcVisual *v = vis_impl(This);
    return set_enum(v, &v->pending.opacity_mode, m, MODES_01, 3);
}

static HRESULT STDMETHODCALLTYPE vis_set_backface_visibility(void *This, DCOMPOSITION_BACKFACE_VISIBILITY m)
{
    DcVisual *v = vis_impl(This);
    return set_enum(v, &v->pending.backface, m, MODES_01, 3);
}

static HRESULT STDMETHODCALLTYPE vis_set_depth_mode(void *This, DCOMPOSITION_DEPTH_MODE m)
{
    DcVisual *v = vis_impl(This);
    return set_enum(v, &v->pending.depth_mode, m, MODES_DEPTH, 4);
}

static HRESULT STDMETHODCALLTYPE vis_set_visible(void *This, BOOL visible)
{
    DcVisual *v = vis_impl(This);
    DcDeviceLock(v->base.device);
    v->pending.visible = !!visible;
    DcObjectDirty(&v->base);
    DcDeviceUnlock(v->base.device);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vis_set_clip_object(void *This, IDCompositionClip *clip)
{
    DcVisual *v = vis_impl(This);
    DcObject *object = nullptr;
    if (clip) {
        object = DcObjectFrom((IUnknown *)clip);
        if (!object || object->cls->kind != DC_CLIP || object->device != v->base.device) {
            if (object)
                DcObjectRelease(object);
            return E_INVALIDARG;
        }
    }
    DcDeviceLock(v->base.device);
    assign(&v->pending.clip, object);
    v->pending.clip_kind = object ? 2 : 0;
    DcObjectDirty(&v->base);
    DcDeviceUnlock(v->base.device);
    if (object)
        DcObjectRelease(object);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vis_set_clip(void *This, const D2D_RECT_F *rect)
{
    DcVisual *v = vis_impl(This);
    if (!rect)
        return E_INVALIDARG;
    DcDeviceLock(v->base.device);
    assign(&v->pending.clip, nullptr);
    v->pending.clip_kind = 1;
    v->pending.clip_rect = *rect;
    DcObjectDirty(&v->base);
    DcDeviceUnlock(v->base.device);
    return S_OK;
}

/* what the visual shows: a surface, a swap chain made for composition or a texture; nothing clears it. */
static HRESULT STDMETHODCALLTYPE vis_set_content(void *This, IUnknown *content)
{
    DcVisual *v = vis_impl(This);
    DcContent *c = nullptr;
    HRESULT hr = S_OK;
    if (content) {
        DcDeviceLock(v->base.device);
        hr = DcContentForObject(v->base.device, content, &c);
        DcDeviceUnlock(v->base.device);
        /* a swap chain that is not for composition has nothing to show, and no error. */
        if (hr == S_FALSE)
            hr = S_OK;
        if (FAILED(hr))
            return hr;
    }
    DcDeviceLock(v->base.device);
    assign_content(&v->pending.content, c);
    DcObjectDirty(&v->base);
    DcDeviceUnlock(v->base.device);
    if (c)
        DcContentUnref(c);
    return S_OK;
}

/* ------------------------------------------------------------------ the tree */

static UINT child_index(const DcVisual *parent, const DcVisual *child)
{
    for (UINT i = 0; i < parent->pending.child_count; i++)
        if (parent->pending.children[i] == child)
            return i;
    return ~0u;
}

static void remove_child(DcVisual *parent, UINT index)
{
    DcVisualState *s = &parent->pending;
    DcVisual *child = s->children[index];
    memmove(&s->children[index], &s->children[index + 1], (s->child_count - index - 1) * sizeof(*s->children));
    s->child_count--;
    if (child->parent == parent)
        child->parent = nullptr;
    DcObjectRelease(&child->base);
}

/* whether a visual is the other or under it in the pending tree. */
static BOOL is_within(const DcVisual *ancestor, const DcVisual *visual)
{
    for (; visual; visual = visual->parent)
        if (visual == ancestor)
            return TRUE;
    return FALSE;
}

/*
 * inserts a child over or under another, or on top or at the bottom of the
 * others when no other is named. A child of another visual moves.
 */
static HRESULT STDMETHODCALLTYPE vis_add_visual(void *This, IDCompositionVisual *visual, BOOL above,
                                                IDCompositionVisual *reference)
{
    DcVisual *v = vis_impl(This);
    DcVisual *child = visual_from((IUnknown *)visual, v->base.device);
    DcVisual *ref = nullptr;
    if (!child)
        return E_INVALIDARG;
    if (reference) {
        ref = visual_from((IUnknown *)reference, v->base.device);
        if (!ref)
            return E_INVALIDARG;
    }
    DcDeviceLock(v->base.device);
    HRESULT hr = S_OK;
    if (is_within(child, v) || ref == child || (ref && child_index(v, ref) == ~0u)) {
        hr = E_INVALIDARG;
    } else {
        DcObjectAddRef(&child->base);
        if (child->parent) {
            UINT old = child_index(child->parent, child);
            if (old != ~0u)
                remove_child(child->parent, old);
        }
        DcVisualState *s = &v->pending;
        /* without a reference the visual goes past an end of the list: above nothing is the bottom, below nothing the top. */
        UINT at = above ? 0 : s->child_count;
        if (ref)
            at = child_index(v, ref) + (above ? 1 : 0);
        if (s->child_count == s->child_capacity) {
            UINT capacity = s->child_capacity ? s->child_capacity * 2 : 4;
            DcVisual **grown = HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*grown));
            if (!grown) {
                hr = E_OUTOFMEMORY;
            } else {
                if (s->child_count)
                    memcpy(grown, s->children, s->child_count * sizeof(*grown));
                if (s->children)
                    HeapFree(GetProcessHeap(), 0, s->children);
                s->children = grown;
                s->child_capacity = capacity;
            }
        }
        if (SUCCEEDED(hr)) {
            memmove(&s->children[at + 1], &s->children[at], (s->child_count - at) * sizeof(*s->children));
            s->children[at] = child;
            s->child_count++;
            child->parent = v;
            DcObjectDirty(&v->base);
        } else {
            DcObjectRelease(&child->base);
        }
    }
    DcDeviceUnlock(v->base.device);
    return hr;
}

static HRESULT STDMETHODCALLTYPE vis_remove_visual(void *This, IDCompositionVisual *visual)
{
    DcVisual *v = vis_impl(This);
    DcVisual *child = visual_from((IUnknown *)visual, v->base.device);
    if (!child)
        return E_INVALIDARG;
    DcDeviceLock(v->base.device);
    UINT index = child_index(v, child);
    if (index != ~0u) {
        remove_child(v, index);
        DcObjectDirty(&v->base);
    }
    DcDeviceUnlock(v->base.device);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE vis_remove_all_visuals(void *This)
{
    DcVisual *v = vis_impl(This);
    DcDeviceLock(v->base.device);
    while (v->pending.child_count)
        remove_child(v, v->pending.child_count - 1);
    DcObjectDirty(&v->base);
    DcDeviceUnlock(v->base.device);
    return S_OK;
}

/* ------------------------------------------------------------------ debugging aids */

static HRESULT STDMETHODCALLTYPE vis_enable_heat_map(void *This, const D2D1_COLOR_F *color)
{
    return color ? S_OK : E_INVALIDARG;
}

static HRESULT STDMETHODCALLTYPE vis_nothing(void *This) { return S_OK; }

const IDCompositionVisual3Vtbl g_visual_vtbl = {
    VTBL_FN(vis_query),
    VTBL_FN(vis_add_ref),
    VTBL_FN(vis_release),
    VTBL_FN(vis_set_offset_x_a),
    VTBL_FN(vis_set_offset_x),
    VTBL_FN(vis_set_offset_y_a),
    VTBL_FN(vis_set_offset_y),
    VTBL_FN(vis_set_transform_object),
    VTBL_FN(vis_set_transform),
    VTBL_FN(vis_set_transform_parent),
    VTBL_FN(vis_set_effect),
    VTBL_FN(vis_set_bitmap_interpolation_mode),
    VTBL_FN(vis_set_border_mode),
    VTBL_FN(vis_set_clip_object),
    VTBL_FN(vis_set_clip),
    VTBL_FN(vis_set_content),
    VTBL_FN(vis_add_visual),
    VTBL_FN(vis_remove_visual),
    VTBL_FN(vis_remove_all_visuals),
    VTBL_FN(vis_set_composite_mode),
    VTBL_FN(vis_set_opacity_mode),
    VTBL_FN(vis_set_backface_visibility),
    VTBL_FN(vis_enable_heat_map),
    VTBL_FN(vis_nothing),
    VTBL_FN(vis_nothing),
    VTBL_FN(vis_nothing),
    VTBL_FN(vis_set_depth_mode),
    VTBL_FN(vis_set_offset_z_a),
    VTBL_FN(vis_set_offset_z),
    VTBL_FN(vis_set_opacity_a),
    VTBL_FN(vis_set_opacity),
    VTBL_FN(vis_set_transform3d_object),
    VTBL_FN(vis_set_transform3d),
    VTBL_FN(vis_set_visible),
};

HRESULT DcCreateVisual(DcDevice *device, IDCompositionVisual3 **out)
{
    DcVisual *v = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*v));
    if (!v)
        return E_OUTOFMEMORY;
    v->iface.lpVtbl = &g_visual_vtbl;
    DcVisualState *s = &v->pending;
    s->opacity.value = 1.0f;
    s->visible = TRUE;
    s->interpolation = s->border_mode = s->composite_mode = s->opacity_mode = s->backface = s->depth_mode = -1;
    DcObjectInit(&v->base, device, &vis_class);
    DcDeviceLock(device);
    DcObjectDirty(&v->base);
    DcDeviceUnlock(device);
    *out = &v->iface;
    return S_OK;
}

/* ------------------------------------------------------------------ targets */

static CRITICAL_SECTION g_targets_lock;
static INIT_ONCE g_targets_once = INIT_ONCE_STATIC_INIT;
static DcTarget *g_targets;

static BOOL CALLBACK targets_init(INIT_ONCE *once, void *param, void **context)
{
    InitializeCriticalSection(&g_targets_lock);
    return TRUE;
}

static DcTarget *tg_impl(IDCompositionTarget *iface) { return CONTAINING_RECORD(iface, DcTarget, iface); }

static void tg_commit(DcObject *object)
{
    DcTarget *t = CONTAINING_RECORD(object, DcTarget, base);
    assign_visual(&t->root_committed, t->root_pending);
    t->needs_compose = TRUE;
}

static void tg_destroy(DcObject *object)
{
    DcTarget *t = CONTAINING_RECORD(object, DcTarget, base);
    DcDevice *device = object->device;
    for (DcTarget **p = &device->targets; *p; p = &(*p)->next_in_device)
        if (*p == t) {
            *p = t->next_in_device;
            break;
        }
    InitOnceExecuteOnce(&g_targets_once, targets_init, nullptr, nullptr);
    EnterCriticalSection(&g_targets_lock);
    for (DcTarget **p = &g_targets; *p; p = &(*p)->next_global)
        if (*p == t) {
            *p = t->next_global;
            break;
        }
    LeaveCriticalSection(&g_targets_lock);
    assign_visual(&t->root_pending, nullptr);
    assign_visual(&t->root_committed, nullptr);
    DcOutputRelease(&t->output);
    HeapFree(GetProcessHeap(), 0, t);
}

static const DcClass tg_class = {DC_TARGET, tg_commit, tg_destroy};

static HRESULT STDMETHODCALLTYPE tg_query(IDCompositionTarget *iface, REFIID riid, void **out)
{
    DcTarget *t = tg_impl(iface);
    DcInterface interfaces[] = {{&IID_IDCompositionTarget, iface}};
    return DcQuery(&t->base, interfaces, 1, riid, out);
}

static ULONG STDMETHODCALLTYPE tg_add_ref(IDCompositionTarget *iface) { return DcObjectAddRef(&tg_impl(iface)->base); }
static ULONG STDMETHODCALLTYPE tg_release(IDCompositionTarget *iface) { return DcObjectRelease(&tg_impl(iface)->base); }

static HRESULT STDMETHODCALLTYPE tg_set_root(IDCompositionTarget *iface, IDCompositionVisual *visual)
{
    DcTarget *t = tg_impl(iface);
    DcVisual *root = nullptr;
    if (visual) {
        root = visual_from((IUnknown *)visual, t->base.device);
        if (!root)
            return E_INVALIDARG;
    }
    DcDeviceLock(t->base.device);
    assign_visual(&t->root_pending, root);
    DcObjectDirty(&t->base);
    DcDeviceUnlock(t->base.device);
    return S_OK;
}

static const IDCompositionTargetVtbl tg_vtbl = {tg_query, tg_add_ref, tg_release, tg_set_root};

/*
 * a target binds a window of this process: one target for each window and
 * topmost flag, since the compositor draws a window's targets itself.
 */
HRESULT DcCreateTarget(DcDevice *device, HWND hwnd, BOOL topmost, IDCompositionTarget **out)
{
    if (!hwnd || !IsWindow(hwnd))
        return E_HANDLE_HR;
    DWORD process;
    GetWindowThreadProcessId(hwnd, &process);
    if (process != GetCurrentProcessId())
        return E_ACCESSDENIED_HR;
    InitOnceExecuteOnce(&g_targets_once, targets_init, nullptr, nullptr);
    DcTarget *t = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*t));
    if (!t)
        return E_OUTOFMEMORY;
    t->iface.lpVtbl = &tg_vtbl;
    t->hwnd = hwnd;
    t->topmost = !!topmost;
    EnterCriticalSection(&g_targets_lock);
    for (DcTarget *other = g_targets; other; other = other->next_global)
        if (other->hwnd == hwnd && other->topmost == t->topmost) {
            LeaveCriticalSection(&g_targets_lock);
            HeapFree(GetProcessHeap(), 0, t);
            return DCOMPOSITION_ERROR_WINDOW_ALREADY_COMPOSED;
        }
    t->next_global = g_targets;
    g_targets = t;
    LeaveCriticalSection(&g_targets_lock);
    DcObjectInit(&t->base, device, &tg_class);
    DcDeviceLock(device);
    t->next_in_device = device->targets;
    device->targets = t;
    DcObjectDirty(&t->base);
    DcDeviceUnlock(device);
    *out = &t->iface;
    return S_OK;
}
