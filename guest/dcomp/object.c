/*
 * the parts every object shares: references, the dirty list that Commit
 * walks, the numbers properties hold with the animations that may give them,
 * and the animations themselves.
 */
#include "dcomp_private.h"

/* ------------------------------------------------------------------ math */

static const double TWO_PI = 6.283185307179586;
static const double HALF_PI = 1.5707963267948966;

double DcSin(double x)
{
    /* reduced to a quarter turn either side of zero, where a series of fifteen terms is exact to a billionth. */
    x -= (double)(long long)(x / TWO_PI + (x >= 0 ? 0.5 : -0.5)) * TWO_PI;
    if (x > HALF_PI)
        x = 2 * HALF_PI - x;
    else if (x < -HALF_PI)
        x = -2 * HALF_PI - x;
    double x2 = x * x;
    return x * (1 + x2 * (-1.0 / 6 + x2 * (1.0 / 120 + x2 * (-1.0 / 5040 + x2 * (1.0 / 362880 +
                x2 * (-1.0 / 39916800 + x2 * (1.0 / 6227020800.0)))))));
}

double DcCos(double x) { return DcSin(x + HALF_PI); }

double DcTan(double x) { return DcSin(x) / DcCos(x); }

double DcSqrt(double x) { return x > 0 ? __builtin_elementwise_sqrt(x) : 0; }

void DcMatrixIdentity(float m[16])
{
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

/* out = a * b, as row vectors multiply: a applies first. */
void DcMatrixMultiply(float out[16], const float a[16], const float b[16])
{
    float r[16];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            r[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] + a[i * 4 + 3] * b[12 + j];
    memcpy(out, r, sizeof(r));
}

void DcMatrixFrom3x2(float m[16], const D2D_MATRIX_3X2_F *matrix)
{
    DcMatrixIdentity(m);
    m[0] = matrix->_11;
    m[1] = matrix->_12;
    m[4] = matrix->_21;
    m[5] = matrix->_22;
    m[12] = matrix->_31;
    m[13] = matrix->_32;
}

/* ------------------------------------------------------------------ objects */

void DcObjectInit(DcObject *object, DcDevice *device, const DcClass *cls)
{
    object->cls = cls;
    object->refs = 1;
    object->device = device;
    DcDeviceAddRef(device);
}

ULONG DcObjectAddRef(DcObject *object) { return InterlockedIncrement(&object->refs); }

ULONG DcObjectRelease(DcObject *object)
{
    ULONG refs = InterlockedDecrement(&object->refs);
    if (!refs) {
        DcDevice *device = object->device;
        DcDeviceLock(device);
        DcObjectUndirty(object);
        object->cls->destroy(object);
        DcDeviceUnlock(device);
        DcDeviceRelease(device);
    }
    return refs;
}

/* the dirty list holds no references, so an object that dies leaves it. */
void DcObjectDirty(DcObject *object)
{
    if (object->dirty)
        return;
    object->dirty = TRUE;
    object->dirty_prev = nullptr;
    object->dirty_next = object->device->dirty;
    if (object->dirty_next)
        object->dirty_next->dirty_prev = object;
    object->device->dirty = object;
}

void DcObjectUndirty(DcObject *object)
{
    if (!object->dirty)
        return;
    object->dirty = FALSE;
    if (object->dirty_prev)
        object->dirty_prev->dirty_next = object->dirty_next;
    else
        object->device->dirty = object->dirty_next;
    if (object->dirty_next)
        object->dirty_next->dirty_prev = object->dirty_prev;
    object->dirty_next = object->dirty_prev = nullptr;
}

DcObject *DcObjectFrom(IUnknown *unknown)
{
    DcObject *object = nullptr;
    if (!unknown || FAILED(unknown->lpVtbl->QueryInterface(unknown, &IID_MimeDcObject, (void **)&object)))
        return nullptr;
    return object;
}

HRESULT DcQuery(DcObject *object, const DcInterface *interfaces, UINT count, REFIID riid, void **out)
{
    if (!out)
        return E_INVALIDARG;
    *out = nullptr;
    void *found = nullptr;
    if (IsEqualGUID(riid, &IID_IUnknown))
        found = interfaces[0].pointer;
    else if (IsEqualGUID(riid, &IID_MimeDcObject))
        found = object;
    else
        for (UINT i = 0; i < count && !found; i++)
            if (IsEqualGUID(riid, interfaces[i].iid))
                found = interfaces[i].pointer;
    if (!found)
        return E_NOINTERFACE;
    DcObjectAddRef(object);
    *out = found;
    return S_OK;
}

/* ------------------------------------------------------------------ floats and animations */

float DcFloatValue(const DcFloat *f, UINT64 now)
{
    return f->animation ? DcAnimationValue(f->animation, now) : f->value;
}

void DcFloatClear(DcFloat *f)
{
    if (f->animation)
        DcObjectRelease(&f->animation->base);
    f->animation = nullptr;
}

void DcFloatCopy(DcFloat *dst, const DcFloat *src)
{
    if (src->animation)
        DcObjectAddRef(&src->animation->base);
    DcFloatClear(dst);
    *dst = *src;
}

static void anim_free(DcAnimData *data)
{
    if (data->items)
        HeapFree(GetProcessHeap(), 0, data->items);
    memset(data, 0, sizeof(*data));
}

static BOOL anim_copy(DcAnimData *dst, const DcAnimData *src)
{
    DcAnimPrimitive *items = nullptr;
    if (src->count) {
        items = HeapAlloc(GetProcessHeap(), 0, src->count * sizeof(*items));
        if (!items)
            return FALSE;
        memcpy(items, src->items, src->count * sizeof(*items));
    }
    anim_free(dst);
    *dst = *src;
    dst->items = items;
    dst->capacity = src->count;
    return TRUE;
}

static void anim_commit(DcObject *object)
{
    DcAnimation *a = CONTAINING_RECORD(object, DcAnimation, base);
    if (anim_copy(&a->committed, &a->pending))
        a->begin = DcNow();
}

static void anim_destroy(DcObject *object)
{
    DcAnimation *a = CONTAINING_RECORD(object, DcAnimation, base);
    anim_free(&a->pending);
    anim_free(&a->committed);
    HeapFree(GetProcessHeap(), 0, a);
}

static const DcClass anim_class = {DC_ANIMATION, anim_commit, anim_destroy};

static DcAnimation *anim_impl(IDCompositionAnimation *iface) { return CONTAINING_RECORD(iface, DcAnimation, iface); }

DcAnimation *DcAnimationFrom(IDCompositionAnimation *animation)
{
    if (!animation || animation->lpVtbl != &g_animation_vtbl)
        return nullptr;
    return anim_impl(animation);
}

/*
 * the value at a time: the primitive in force at the time the animation
 * has run, after repeats folded it back. A cubic and a sine run from their
 * own offset, an end holds its value.
 */
float DcAnimationValue(const DcAnimation *a, UINT64 now)
{
    const DcAnimData *d = &a->committed;
    if (!d->count)
        return 0.0f;
    UINT64 begin = d->absolute ? (UINT64)d->absolute_begin.QuadPart : a->begin;
    UINT64 t = now > begin ? now - begin : 0;
    for (UINT i = 0; i < d->count; i++) {
        const DcAnimPrimitive *p = &d->items[i];
        if (p->kind == DC_PRIM_REPEAT && t >= p->offset && p->duration)
            t = p->offset - p->duration + (t - p->offset) % p->duration;
    }
    const DcAnimPrimitive *active = &d->items[0];
    for (UINT i = 0; i < d->count; i++) {
        const DcAnimPrimitive *p = &d->items[i];
        if (p->kind != DC_PRIM_REPEAT && p->kind != DC_PRIM_FREEZE && p->offset <= t)
            active = p;
    }
    double seconds = t > active->offset ? (double)(t - active->offset) / (double)DcFrequency() : 0.0;
    switch (active->kind) {
    case DC_PRIM_CUBIC:
        return (float)(active->a + seconds * (active->b + seconds * (active->c + seconds * active->d)));
    case DC_PRIM_SINE:
        return (float)(active->a + active->b * DcSin(active->c * seconds + active->d));
    case DC_PRIM_END:
        return active->a;
    default:
        return 0.0f;
    }
}

/* ------------------------------------------------------------------ IDCompositionAnimation */

static HRESULT STDMETHODCALLTYPE anim_query(IDCompositionAnimation *iface, REFIID riid, void **out)
{
    DcAnimation *a = anim_impl(iface);
    DcInterface interfaces[] = {{&IID_IDCompositionAnimation, iface}};
    return DcQuery(&a->base, interfaces, 1, riid, out);
}

static ULONG STDMETHODCALLTYPE anim_add_ref(IDCompositionAnimation *iface)
{
    return DcObjectAddRef(&anim_impl(iface)->base);
}

static ULONG STDMETHODCALLTYPE anim_release(IDCompositionAnimation *iface)
{
    return DcObjectRelease(&anim_impl(iface)->base);
}

static BOOL finite(double v) { return v - v == 0.0; }

/* seconds as ticks of the performance counter, kept to a 1024th as the dwm does. */
static UINT64 ticks(double seconds)
{
    long long units = (long long)(seconds * 1024.0 + (seconds < 0 ? -0.5 : 0.5));
    return (UINT64)((double)units * (1.0 / 1024.0) * (double)DcFrequency());
}

/* adds a primitive: offsets never go back, and an end closes the animation. */
static HRESULT add_primitive(DcAnimation *a, const DcAnimPrimitive *p)
{
    DcAnimData *d = &a->pending;
    if (d->ended)
        return E_INVALIDARG;
    BOOL old_device = a->base.device->version < 2;
    if (d->count && p->offset < d->items[d->count - 1].offset)
        return E_INVALIDARG;
    if (old_device && (p->kind == DC_PRIM_REPEAT || p->kind == DC_PRIM_FREEZE) &&
        (!d->count || p->offset == d->items[0].offset))
        return E_INVALIDARG;
    if (p->kind == DC_PRIM_REPEAT && old_device && d->count && p->duration > p->offset - d->items[0].offset)
        return E_INVALIDARG;
    BOOL replace = old_device && d->count && d->items[d->count - 1].offset == p->offset;
    if (!replace) {
        if (d->count == d->capacity) {
            UINT capacity = d->capacity ? d->capacity * 2 : 4;
            DcAnimPrimitive *items = HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(*items));
            if (!items)
                return E_OUTOFMEMORY;
            if (d->count)
                memcpy(items, d->items, d->count * sizeof(*items));
            if (d->items)
                HeapFree(GetProcessHeap(), 0, d->items);
            d->items = items;
            d->capacity = capacity;
        }
        d->count++;
    }
    d->items[d->count - 1] = *p;
    if (p->kind == DC_PRIM_END)
        d->ended = TRUE;
    DcObjectDirty(&a->base);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE anim_reset(IDCompositionAnimation *iface)
{
    DcAnimation *a = anim_impl(iface);
    DcDeviceLock(a->base.device);
    DcAnimData *d = &a->pending;
    d->count = 0;
    d->ended = d->absolute = FALSE;
    DcObjectDirty(&a->base);
    DcDeviceUnlock(a->base.device);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE anim_set_absolute_begin_time(IDCompositionAnimation *iface, LARGE_INTEGER begin)
{
    DcAnimation *a = anim_impl(iface);
    DcDeviceLock(a->base.device);
    HRESULT hr = S_OK;
    if (a->pending.count) {
        hr = E_ACCESSDENIED_HR;
    } else {
        a->pending.absolute = TRUE;
        a->pending.absolute_begin = begin;
        DcObjectDirty(&a->base);
    }
    DcDeviceUnlock(a->base.device);
    return hr;
}

static BOOL valid_offset(DcAnimation *a, double offset) { return finite(offset) && (a->base.device->version >= 2 || offset >= 0.0); }

static HRESULT add(DcAnimation *a, UINT kind, double offset, double duration, float p0, float p1, float p2, float p3)
{
    DcAnimPrimitive p = {kind, ticks(offset), ticks(duration), p0, p1, p2, p3};
    DcDeviceLock(a->base.device);
    HRESULT hr = add_primitive(a, &p);
    DcDeviceUnlock(a->base.device);
    return hr;
}

static HRESULT STDMETHODCALLTYPE anim_add_cubic(IDCompositionAnimation *iface, double offset, float constant,
                                                float linear, float quadratic, float cubic)
{
    DcAnimation *a = anim_impl(iface);
    if (!valid_offset(a, offset) || !finite(constant) || !finite(linear) || !finite(quadratic) || !finite(cubic))
        return E_INVALIDARG;
    return add(a, DC_PRIM_CUBIC, offset, 0, constant, linear, quadratic, cubic);
}

/* the frequency is in cycles per second, the phase in degrees. */
static HRESULT STDMETHODCALLTYPE anim_add_sinusoidal(IDCompositionAnimation *iface, double offset, float bias,
                                                     float amplitude, float frequency, float phase)
{
    DcAnimation *a = anim_impl(iface);
    if (!valid_offset(a, offset) || !finite(bias) || !finite(amplitude) || !finite(frequency) || !finite(phase))
        return E_INVALIDARG;
    return add(a, DC_PRIM_SINE, offset, 0, bias, amplitude, (float)((double)frequency * TWO_PI),
               (float)((double)phase * DC_PI / 180.0));
}

static HRESULT STDMETHODCALLTYPE anim_add_repeat(IDCompositionAnimation *iface, double offset, double duration)
{
    DcAnimation *a = anim_impl(iface);
    if (!valid_offset(a, offset) || !finite(duration) || duration <= 0.0)
        return E_INVALIDARG;
    return add(a, DC_PRIM_REPEAT, offset, duration, 0, 0, 0, 0);
}

static HRESULT STDMETHODCALLTYPE anim_end(IDCompositionAnimation *iface, double offset, float value)
{
    DcAnimation *a = anim_impl(iface);
    if (!finite(offset) || offset < 0.0 || !finite(value))
        return E_INVALIDARG;
    return add(a, DC_PRIM_END, offset, 0, value, 0, 0, 0);
}

const IDCompositionAnimationVtbl g_animation_vtbl = {
    anim_query,          anim_add_ref,      anim_release,   anim_reset, anim_set_absolute_begin_time,
    anim_add_cubic,      anim_add_sinusoidal, anim_add_repeat, anim_end,
};

HRESULT DcCreateAnimation(DcDevice *device, IDCompositionAnimation **out)
{
    DcAnimation *a = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*a));
    if (!a)
        return E_OUTOFMEMORY;
    a->iface.lpVtbl = &g_animation_vtbl;
    DcObjectInit(&a->base, device, &anim_class);
    *out = &a->iface;
    return S_OK;
}
