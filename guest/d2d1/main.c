/*
 * d2d1.dll entry points that need no object, plus the memory and math every part uses.
 */
#include "d2d_private.h"
#include <stddef.h>

int _fltused = 0x9875;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

#define IDX(type, member) (offsetof(type, member) / sizeof(void *))
_Static_assert(IDX(ID2D1RenderTargetVtbl, BeginDraw) == 48, "vtable order");
_Static_assert(IDX(ID2D1DCRenderTargetVtbl, BindDC) == 57, "vtable order");
_Static_assert(IDX(ID2D1DeviceContext5Vtbl, CreateSvgDocument) == 115, "vtable order");
_Static_assert(IDX(ID2D1FactoryVtbl, CreateDCRenderTarget) == 16, "vtable order");

/* ------------------------------------------------------------------ memory */

void *D2dAlloc(SIZE_T size) { return HeapAlloc(GetProcessHeap(), 0, size ? size : 1); }

void *D2dAllocZero(SIZE_T size) { return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1); }

void *D2dRealloc(void *p, SIZE_T size)
{
    if (!p)
        return D2dAlloc(size);
    return HeapReAlloc(GetProcessHeap(), 0, p, size ? size : 1);
}

void D2dFree(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

/*
 * pixel buffers a program frees and asks for again at once, as one that rebuilds its bitmaps for every frame
 * does, are kept for a while. the heap gives large blocks back to the system, and fresh pages cost a fault each.
 */
#define PIXELS_MIN ((SIZE_T)256 << 10)
#define PIXELS_KEEP ((SIZE_T)64 << 20)
#define PIXELS_AGE_MS 4000

static struct {
    void *p;
    SIZE_T size;
    ULONGLONG freed;
} pixels[64];
static SIZE_T pixels_kept;
static SRWLOCK pixels_lock = SRWLOCK_INIT;

static void pixels_drop(UINT i)
{
    HeapFree(GetProcessHeap(), 0, pixels[i].p);
    pixels_kept -= pixels[i].size;
    pixels[i].p = nullptr;
}

/* the lock is held. the slot of the buffer kept the longest, or the count of slots when none is kept. */
static UINT pixels_oldest(void)
{
    UINT oldest = ARRAYSIZE(pixels);
    for (UINT i = 0; i < ARRAYSIZE(pixels); i++)
        if (pixels[i].p && (oldest == ARRAYSIZE(pixels) || pixels[i].freed < pixels[oldest].freed))
            oldest = i;
    return oldest;
}

void *D2dPixelsAlloc(SIZE_T size, BOOL zero)
{
    void *p = nullptr;
    if (size >= PIXELS_MIN && size <= PIXELS_KEEP / 2) {
        /* sizes go up in steps of an eighth, so a window that grows finds the blocks of its last size. */
        SIZE_T step = PIXELS_MIN / 8;
        while (step * 16 <= size)
            step *= 2;
        size = (size + step - 1) & ~(step - 1);
        AcquireSRWLockExclusive(&pixels_lock);
        /* a window being resized asks for a new size at every step, so a block somewhat larger serves too. */
        UINT best = ARRAYSIZE(pixels);
        for (UINT i = 0; i < ARRAYSIZE(pixels); i++)
            if (pixels[i].p && pixels[i].size >= size && pixels[i].size - size <= size / 2 &&
                (best == ARRAYSIZE(pixels) || pixels[i].size < pixels[best].size))
                best = i;
        if (best < ARRAYSIZE(pixels)) {
            p = pixels[best].p;
            pixels[best].p = nullptr;
            pixels_kept -= pixels[best].size;
        }
        ReleaseSRWLockExclusive(&pixels_lock);
    }
    if (!p)
        return zero ? D2dAllocZero(size) : D2dAlloc(size);
    if (zero)
        memset(p, 0, size);
    return p;
}

void D2dPixelsFree(void *p)
{
    if (!p)
        return;
    SIZE_T size = HeapSize(GetProcessHeap(), 0, p);
    if (size < PIXELS_MIN || size > PIXELS_KEEP / 2) {
        D2dFree(p);
        return;
    }
    AcquireSRWLockExclusive(&pixels_lock);
    ULONGLONG now = GetTickCount64();
    for (UINT i = 0; i < ARRAYSIZE(pixels); i++)
        if (pixels[i].p && now - pixels[i].freed > PIXELS_AGE_MS)
            pixels_drop(i);
    UINT slot;
    for (;;) {
        for (slot = 0; slot < ARRAYSIZE(pixels) && pixels[slot].p; slot++)
            ;
        if (slot < ARRAYSIZE(pixels) && pixels_kept + size <= PIXELS_KEEP)
            break;
        /* the oldest go until the new one fits. */
        pixels_drop(pixels_oldest());
    }
    pixels[slot].p = p;
    pixels[slot].size = size;
    pixels[slot].freed = now;
    pixels_kept += size;
    ReleaseSRWLockExclusive(&pixels_lock);
}

static void *vec_alloc(void *ctx, size_t size) { return D2dAlloc(size); }
static void vec_free(void *ctx, void *p) { D2dFree(p); }
const MimeAllocator d2d_allocator = {vec_alloc, vec_free, nullptr};

BOOL D2dArrayGrow(D2dArray *a, UINT32 element_size, UINT32 extra)
{
    if (a->count + extra <= a->capacity)
        return TRUE;
    UINT32 cap = a->capacity ? a->capacity : 8;
    while (cap < a->count + extra)
        cap *= 2;
    void *data = D2dRealloc(a->data, (SIZE_T)cap * element_size);
    if (!data)
        return FALSE;
    a->data = data;
    a->capacity = cap;
    return TRUE;
}

/* ------------------------------------------------------------------ math */

float D2dAbsF(float x) { return x < 0 ? -x : x; }
float D2dFloorF(float x) { return mime_vec_floorf(x); }
float D2dSqrtF(float x) { return mime_vec_sqrtf(x); }
float D2dAtan2F(float y, float x) { return mime_vec_atan2f(y, x); }
void D2dSinCosF(float a, float *s, float *c) { mime_vec_sincosf(a, s, c); }
int D2dRoundInt(float x) { return (int)mime_vec_floorf(x + 0.5f); }

float D2dTanF(float x)
{
    float s, c;
    mime_vec_sincosf(x, &s, &c);
    return s / c;
}

float D2dLogF(float x)
{
    if (!(x > 0.0f))
        return x == 0.0f ? -3.4e38f : 0.0f / 0.0f;
    union {
        float f;
        uint32_t u;
    } v = {x};
    int e = (int)((v.u >> 23) & 0xff) - 127;
    v.u = (v.u & 0x7fffff) | 0x3f800000;
    double m = v.f;
    if (m > 1.41421356) {
        m *= 0.5;
        e++;
    }
    double s = (m - 1.0) / (m + 1.0), s2 = s * s;
    double r = 2.0 * s * (1.0 + s2 * (1.0 / 3 + s2 * (1.0 / 5 + s2 * (1.0 / 7 + s2 * (1.0 / 9 + s2 / 11)))));
    return (float)(r + e * 0.6931471805599453);
}

float D2dExpF(float x)
{
    if (x > 88.0f)
        return 3.4e38f;
    if (x < -87.0f)
        return 0.0f;
    double k = mime_vec_floorf((float)(x * 1.4426950408889634 + 0.5));
    double r = x - k * 0.6931471805599453;
    double p = 1.0 + r * (1.0 + r * (0.5 + r * (1.0 / 6 + r * (1.0 / 24 + r * (1.0 / 120 + r * (1.0 / 720))))));
    union {
        float f;
        uint32_t u;
    } v;
    v.u = (uint32_t)((int)k + 127) << 23;
    return (float)(p * v.f);
}

float D2dPowF(float x, float y)
{
    if (y == 0.0f)
        return 1.0f;
    if (x <= 0.0f)
        return x == 0.0f ? 0.0f : 0.0f / 0.0f;
    return D2dExpF(y * D2dLogF(x));
}

const D2dMatrix d2d_identity = {1, 0, 0, 1, 0, 0};

void D2dMatMul(D2dMatrix *out, const D2dMatrix *a, const D2dMatrix *b)
{
    D2dMatrix r;
    r._11 = a->_11 * b->_11 + a->_12 * b->_21;
    r._12 = a->_11 * b->_12 + a->_12 * b->_22;
    r._21 = a->_21 * b->_11 + a->_22 * b->_21;
    r._22 = a->_21 * b->_12 + a->_22 * b->_22;
    r._31 = a->_31 * b->_11 + a->_32 * b->_21 + b->_31;
    r._32 = a->_31 * b->_12 + a->_32 * b->_22 + b->_32;
    *out = r;
}

BOOL D2dMatInvert(D2dMatrix *out, const D2dMatrix *m)
{
    float det = m->_22 * m->_11 - m->_21 * m->_12;
    if (det == 0.0f)
        return FALSE;
    float inv = 1.0f / det;
    if (!D2dFinite(inv))
        return FALSE;
    D2dMatrix r;
    r._11 = m->_22 * inv;
    r._12 = -m->_12 * inv;
    r._21 = -m->_21 * inv;
    r._22 = m->_11 * inv;
    r._32 = (m->_31 * m->_12 - m->_32 * m->_11) * inv;
    r._31 = (m->_32 * m->_21 - m->_31 * m->_22) * inv;
    *out = r;
    return TRUE;
}

float D2dMatScale(const D2dMatrix *m)
{
    float a = m->_11, b = m->_12, c = m->_21, d = m->_22;
    float sum = a * a + b * b + c * c + d * d;
    float det = a * d - b * c;
    float disc = sum * sum - 4.0f * det * det;
    if (disc < 0)
        disc = 0;
    return D2dSqrtF((sum + D2dSqrtF(disc)) * 0.5f);
}

BOOL D2dMatIsIdentity(const D2dMatrix *m)
{
    return m->_11 == 1 && m->_12 == 0 && m->_21 == 0 && m->_22 == 1 && m->_31 == 0 && m->_32 == 0;
}

/* ------------------------------------------------------------------ exports */

D2D1API HRESULT WINAPI D2D1CreateFactory(D2D1_FACTORY_TYPE type, REFIID riid, const D2D1_FACTORY_OPTIONS *options,
                                         void **factory)
{
    if (!factory || !riid)
        return E_INVALIDARG;
    *factory = nullptr;
    if (type != D2D1_FACTORY_TYPE_SINGLE_THREADED && type != D2D1_FACTORY_TYPE_MULTI_THREADED)
        return E_INVALIDARG;
    return D2dFactoryCreate(type, options, riid, factory);
}

D2D1API void WINAPI D2D1MakeRotateMatrix(FLOAT angle, D2D1_POINT_2F center, D2D1_MATRIX_3X2_F *matrix)
{
    float a = angle;
    /* fmodf by hand, the quotient is small enough for a truncation. */
    a = a - 360.0f * (float)(int)(a / 360.0f);
    if (a < 0)
        a += 360.0f;
    float s, c;
    if (a == 0.0f) {
        s = 0;
        c = 1;
    } else if (a == 90.0f) {
        s = 1;
        c = 0;
    } else if (a == 180.0f) {
        s = 0;
        c = -1;
    } else if (a == 270.0f) {
        s = -1;
        c = 0;
    } else {
        D2dSinCosF(a * 0.017453292f, &s, &c);
    }
    matrix->_11 = c;
    matrix->_12 = s;
    matrix->_21 = -s;
    matrix->_22 = c;
    matrix->_31 = (1.0f - c) * center.x + center.y * s;
    matrix->_32 = (1.0f - c) * center.y - s * center.x;
}

D2D1API void WINAPI D2D1MakeSkewMatrix(FLOAT angle_x, FLOAT angle_y, D2D1_POINT_2F center, D2D1_MATRIX_3X2_F *matrix)
{
    float tx = D2dTanF(angle_x * 0.017453292f), ty = D2dTanF(angle_y * 0.017453292f);
    matrix->_11 = 1;
    matrix->_12 = ty;
    matrix->_21 = tx;
    matrix->_22 = 1;
    matrix->_31 = -center.y * tx;
    matrix->_32 = -center.x * ty;
}

D2D1API BOOL WINAPI D2D1IsMatrixInvertible(const D2D1_MATRIX_3X2_F *matrix)
{
    float det = matrix->_22 * matrix->_11 - matrix->_21 * matrix->_12;
    return det != 0.0f && D2dFinite(1.0f / det);
}

D2D1API BOOL WINAPI D2D1InvertMatrix(D2D1_MATRIX_3X2_F *matrix) { return D2dMatInvert(matrix, matrix); }

static float srgb_to_scrgb(float v)
{
    if (v <= 0.0f)
        return 0.0f;
    if (v <= 0.04045f)
        return v / 12.92f;
    if (v < 1.0f)
        return D2dPowF((v + 0.055f) / 1.055f, 2.4f);
    return 1.0f;
}

static float scrgb_to_srgb(float v)
{
    if (v <= 0.0f)
        return 0.0f;
    if (v <= 0.0031308f)
        return v * 12.92f;
    if (v < 1.0f)
        return D2dPowF(v, 1.0f / 2.4f) * 1.055f - 0.055f;
    return 1.0f;
}

float D2dSrgbToLinear(float v) { return srgb_to_scrgb(v); }
float D2dLinearToSrgb(float v) { return scrgb_to_srgb(v); }

D2D1API D2D1_COLOR_F *WINAPI D2D1ConvertColorSpace(D2D1_COLOR_F *result, D2D1_COLOR_SPACE source,
                                                   D2D1_COLOR_SPACE destination, const D2D1_COLOR_F *color)
{
    /* a space that is not srgb or scrgb gives transparent black. */
    if ((source != D2D1_COLOR_SPACE_SRGB && source != D2D1_COLOR_SPACE_SCRGB) ||
        (destination != D2D1_COLOR_SPACE_SRGB && destination != D2D1_COLOR_SPACE_SCRGB)) {
        result->r = result->g = result->b = result->a = 0;
        return result;
    }
    D2D1_COLOR_F c = *color;
    if (source != destination) {
        if (source == D2D1_COLOR_SPACE_SRGB) {
            c.r = srgb_to_scrgb(c.r);
            c.g = srgb_to_scrgb(c.g);
            c.b = srgb_to_scrgb(c.b);
        } else {
            c.r = scrgb_to_srgb(c.r);
            c.g = scrgb_to_srgb(c.g);
            c.b = scrgb_to_srgb(c.b);
        }
    }
    *result = c;
    return result;
}

D2D1API void WINAPI D2D1SinCos(FLOAT angle, FLOAT *s, FLOAT *c) { D2dSinCosF(angle, s, c); }

D2D1API FLOAT WINAPI D2D1Tan(FLOAT angle) { return D2dTanF(angle); }

D2D1API FLOAT WINAPI D2D1Vec3Length(FLOAT x, FLOAT y, FLOAT z) { return D2dSqrtF(x * x + y * y + z * z); }

D2D1API FLOAT WINAPI D2D1ComputeMaximumScaleFactor(const D2D1_MATRIX_3X2_F *matrix)
{
    return D2dMatScale(matrix);
}

D2D1API HRESULT WINAPI D2D1GetGradientMeshInteriorPointsFromCoonsPatch(
    const D2D1_POINT_2F *p0, const D2D1_POINT_2F *p1, const D2D1_POINT_2F *p2, const D2D1_POINT_2F *p3,
    const D2D1_POINT_2F *p4, const D2D1_POINT_2F *p5, const D2D1_POINT_2F *p6, const D2D1_POINT_2F *p7,
    const D2D1_POINT_2F *p8, const D2D1_POINT_2F *p9, const D2D1_POINT_2F *p10, const D2D1_POINT_2F *p11,
    D2D1_POINT_2F *t11, D2D1_POINT_2F *t12, D2D1_POINT_2F *t21, D2D1_POINT_2F *t22)
{
    if (!p0 || !p1 || !p2 || !p3 || !p4 || !p5 || !p6 || !p7 || !p8 || !p9 || !p10 || !p11 || !t11 || !t12 || !t21 ||
        !t22)
        return E_INVALIDARG;
    /* the perimeter runs 00 01 02 03 13 23 33 32 31 30 20 10. */
    const D2D1_POINT_2F *P00 = p0, *P01 = p1, *P02 = p2, *P03 = p3, *P13 = p4, *P23 = p5, *P33 = p6, *P32 = p7,
                        *P31 = p8, *P30 = p9, *P20 = p10, *P10 = p11;
    t11->x = (-4 * P00->x + 6 * (P01->x + P10->x) - 2 * (P03->x + P30->x) + 3 * (P31->x + P13->x) - P33->x) / 9;
    t11->y = (-4 * P00->y + 6 * (P01->y + P10->y) - 2 * (P03->y + P30->y) + 3 * (P31->y + P13->y) - P33->y) / 9;
    t12->x = (-4 * P03->x + 6 * (P02->x + P13->x) - 2 * (P00->x + P33->x) + 3 * (P32->x + P10->x) - P30->x) / 9;
    t12->y = (-4 * P03->y + 6 * (P02->y + P13->y) - 2 * (P00->y + P33->y) + 3 * (P32->y + P10->y) - P30->y) / 9;
    t21->x = (-4 * P30->x + 6 * (P31->x + P20->x) - 2 * (P33->x + P00->x) + 3 * (P01->x + P23->x) - P03->x) / 9;
    t21->y = (-4 * P30->y + 6 * (P31->y + P20->y) - 2 * (P33->y + P00->y) + 3 * (P01->y + P23->y) - P03->y) / 9;
    t22->x = (-4 * P33->x + 6 * (P32->x + P23->x) - 2 * (P30->x + P03->x) + 3 * (P02->x + P20->x) - P00->x) / 9;
    t22->y = (-4 * P33->y + 6 * (P32->y + P23->y) - 2 * (P30->y + P03->y) + 3 * (P02->y + P20->y) - P00->y) / 9;
    return S_OK;
}
