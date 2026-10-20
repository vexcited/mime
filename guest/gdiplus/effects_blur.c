/*
 * blur and sharpen: a separable gaussian over straight argb pixels, and the unsharp mask built on it.
 *
 * the kernel is a gaussian truncated at the radius. a radius from 20 up is not convolved at full size: lines are
 * averaged down by a power of two first, blurred with the smaller radius and interpolated back up. every tap
 * works on bytes through a table of rounded products with eight fractional bits, which fixes the rounding.
 */
#include "fx_private.h"

#define MAX_TAPS 32

typedef struct BlurPlan {
    INT shift;                   /* lines are averaged down by 1 << shift */
    INT taps;                    /* weights of the one sided kernel, the centre first */
    INT pad;                     /* pixels added on both sides of a line before it is convolved */
    INT expand;                  /* the value the program gave, only 1 pads with transparent pixels */
    UINT16 product[MAX_TAPS][256];
} BlurPlan;

static INT blur_shift(float radius)
{
    if (!(radius >= 1.0f))
        return 0;
    double ratio = gp_log((double)(float)(radius * 0.1f)) / gp_log(2.0);
    if (!(ratio > 0.0))
        ratio = 0.0;
    INT shift = fx_float_to_int(mime_vec_floorf((float)ratio));
    return shift > 8 ? 8 : shift;
}

static void blur_plan_init(BlurPlan *plan, float radius, INT expand)
{
    plan->shift = blur_shift(radius);
    plan->expand = expand;
    float reduced = radius / (float)(1 << plan->shift);
    INT taps = fx_float_to_int(mime_vec_ceilf(reduced)) + 1;
    if (taps > MAX_TAPS)
        taps = MAX_TAPS;
    plan->taps = taps;
    plan->pad = 2 * ((taps + 1) / 2) - 1;

    float weight[MAX_TAPS];
    float step = 1.4f / reduced;
    float x = 0.0f, sum = 0.0f;
    for (INT i = 0; i < taps; i++) {
        float w = (float)gp_exp((double)-(x * x));
        x += step;
        weight[i] = w;
        sum += (i == 0 ? 1.0f : 2.0f) * w;
    }
    float norm = (float)(1.0 / (double)sum);
    for (INT i = 0; i < taps; i++)
        weight[i] = norm * weight[i];
    /* the centre tap is met from both sides. */
    weight[0] *= 0.5f;
    for (INT j = 0; j < taps; j++)
        for (INT b = 0; b < 256; b++)
            plan->product[j][b] = (UINT16)(fx_float_to_int((float)b * weight[j] * 262144.0f + 512.0f) >> 10);
}

/* the average of blocks of 1 << shift pixels, the last block may be short. */
static void blur_down(const BlurPlan *plan, const UINT32 *src, INT step, INT n, UINT32 *out)
{
    INT k = plan->shift;
    if (k == 0) {
        for (INT i = 0; i < n; i++)
            out[i] = src[(SSIZE_T)i * step];
        return;
    }
    INT block = 1 << k, full = n / block, rest = n % block;
    for (INT i = 0; i < full; i++) {
        UINT sum[4] = {0, 0, 0, 0};
        for (INT j = 0; j < block; j++) {
            UINT32 c = src[(SSIZE_T)(i * block + j) * step];
            for (INT ch = 0; ch < 4; ch++)
                sum[ch] += (c >> (8 * ch)) & 255;
        }
        out[i] = (UINT32)(BYTE)(sum[0] >> k) | (UINT32)(BYTE)(sum[1] >> k) << 8 | (UINT32)(BYTE)(sum[2] >> k) << 16 |
                 (UINT32)(BYTE)(sum[3] >> k) << 24;
    }
    if (rest) {
        UINT sum[4] = {0, 0, 0, 0};
        for (INT j = 0; j < rest; j++) {
            UINT32 c = src[(SSIZE_T)(full * block + j) * step];
            for (INT ch = 0; ch < 4; ch++)
                sum[ch] += (c >> (8 * ch)) & 255;
        }
        out[full] = (UINT32)(BYTE)(sum[0] / rest) | (UINT32)(BYTE)(sum[1] / rest) << 8 |
                    (UINT32)(BYTE)(sum[2] / rest) << 16 | (UINT32)(BYTE)(sum[3] / rest) << 24;
    }
}

/* fills the pad pixels before and after the line, line points at its first real pixel. */
static void blur_pad(const BlurPlan *plan, UINT32 *line, INT n)
{
    INT pad = plan->pad;
    if (plan->expand == 1) {
        memset(line - pad, 0, sizeof(UINT32) * (SIZE_T)pad);
        memset(line + n, 0, sizeof(UINT32) * (SIZE_T)pad);
    } else if (n <= pad) {
        for (INT i = 1; i <= pad; i++) {
            line[-i] = line[0];
            line[n - 1 + i] = line[n - 1];
        }
    } else {
        /* the line is mirrored about its end pixels, which are not repeated. */
        for (INT i = 1; i <= pad; i++) {
            line[-i] = line[i];
            line[n - 1 + i] = line[n - 1 - i];
        }
    }
}

static void blur_convolve(const BlurPlan *plan, const UINT32 *line, INT n, UINT32 *out)
{
    INT taps = plan->taps;
    for (INT x = 0; x < n; x++) {
        UINT sum0 = 0, sum1 = 0, sum2 = 0, sum3 = 0;
        for (INT j = 0; j < taps; j++) {
            const UINT16 *table = plan->product[j];
            UINT32 a = line[x - j], b = line[x + j];
            sum0 += table[a & 255u] + table[b & 255u];
            sum1 += table[(a >> 8) & 255u] + table[(b >> 8) & 255u];
            sum2 += table[(a >> 16) & 255u] + table[(b >> 16) & 255u];
            sum3 += table[a >> 24] + table[b >> 24];
        }
        out[x] = (UINT32)(BYTE)((sum0 + 128) >> 8) | (UINT32)(BYTE)((sum1 + 128) >> 8) << 8 |
                 (UINT32)(BYTE)((sum2 + 128) >> 8) << 16 | (UINT32)(BYTE)((sum3 + 128) >> 8) << 24;
    }
}

/*
 * one channel of the interpolation back to full size. samples are at the middle of their blocks, values are
 * kept as 16 bit numbers with seven fractional bits and saturate at 255 << 7.
 */
static void up_channel(const UINT32 *samples, INT m, INT n, INT k, INT bits, UINT32 *dst, INT step)
{
    const INT block = 1 << k, half = block >> 1;
    const UINT32 keep = ~(255u << bits);
#define SAMPLE(i) ((INT)((samples[(i)] >> bits) & 255u))
#define PUT(pos, v) (dst[(SSIZE_T)(pos) * step] = (dst[(SSIZE_T)(pos) * step] & keep) | (UINT32)((v) >> 7) << bits)
    INT s0 = SAMPLE(0);
    INT s1 = m > 1 ? SAMPLE(1) : s0;
    SHORT forward = (SHORT)((s1 - s0) << (7 - k));
    SHORT back = (SHORT)-forward;
    /* before the first sample the line continues the slope of the first two. */
    SHORT v = (SHORT)(back / 2 + (s0 << 7));
    if (v < 0)
        v = back / 2 < 0 ? 0 : 32640;
    INT first = half <= n ? half : n;
    for (INT pos = first - 1; pos >= 0; pos--) {
        PUT(pos, v);
        SHORT next = (SHORT)(v + back);
        v = next;
        if (next < 0)
            v = back < 0 ? 0 : 32640;
    }
    if (first >= n)
        return;
    INT pos = half;
    SHORT last_step = back;
    for (INT i = 0; i + 1 < m; i++) {
        INT cur = SAMPLE(i), nxt = SAMPLE(i + 1);
        SHORT slope = (SHORT)((nxt - cur) << (7 - k));
        SHORT value = (SHORT)((cur << 7) + slope / 2);
        INT end = pos + block < n ? pos + block : n;
        for (; pos < end; pos++) {
            PUT(pos, value);
            value = (SHORT)(value + slope);
        }
        last_step = slope;
    }
    if (half < n - 1) {
        INT last = SAMPLE(m - 1);
        SHORT value = (SHORT)(last_step / 2 + (last << 7));
        if (value < 0)
            value = last_step / 2 < 0 ? 0 : 32640;
        for (; pos < n; pos++) {
            PUT(pos, value);
            SHORT next = (SHORT)(value + last_step);
            value = next;
            if (next < 0)
                value = last_step < 0 ? 0 : 32640;
        }
    }
#undef SAMPLE
#undef PUT
}

static void blur_up(const BlurPlan *plan, const UINT32 *samples, INT m, INT n, UINT32 *dst, INT step)
{
    if (plan->shift == 0) {
        for (INT i = 0; i < n; i++)
            dst[(SSIZE_T)i * step] = samples[i];
        return;
    }
    for (INT ch = 0; ch < 4; ch++)
        up_channel(samples, m, n, plan->shift, 8 * ch, dst, step);
}

/* the scratch memory of a pass: the padded line and the convolved line. */
typedef struct BlurScratch {
    UINT32 *buffer;
    UINT32 *convolved;
} BlurScratch;

static void blur_line(const BlurPlan *plan, const BlurScratch *scratch, const UINT32 *src, INT src_step, INT n,
                      UINT32 *dst, INT dst_step)
{
    INT m = (n + (1 << plan->shift) - 1) >> plan->shift;
    UINT32 *line = scratch->buffer + plan->pad;
    blur_down(plan, src, src_step, n, line);
    blur_pad(plan, line, m);
    blur_convolve(plan, line, m, scratch->convolved);
    blur_up(plan, scratch->convolved, m, n, dst, dst_step);
}

static FxResult blur_scratch_make(const BlurPlan *plan, INT longest, BlurScratch *scratch)
{
    SIZE_T count = (SIZE_T)(longest + 2 * plan->pad + 2);
    scratch->buffer = gp_alloc(sizeof(UINT32) * count * 2);
    if (!scratch->buffer)
        return FX_E_OUTOFMEMORY;
    scratch->convolved = scratch->buffer + count;
    return FX_OK;
}

/* both passes over the area in place: rows first, then columns. */
static FxResult blur_area(const BlurPlan *plan, const UINT32 *src, INT src_stride, UINT32 *dst, INT dst_stride, INT w,
                          INT h)
{
    BlurScratch scratch;
    FxResult hr = blur_scratch_make(plan, w > h ? w : h, &scratch);
    if (hr != FX_OK)
        return hr;
    for (INT y = 0; y < h; y++)
        blur_line(plan, &scratch, src + (SSIZE_T)y * src_stride, 1, w, dst + (SSIZE_T)y * dst_stride, 1);
    for (INT x = 0; x < w; x++)
        blur_line(plan, &scratch, dst + x, dst_stride, h, dst + x, dst_stride);
    gp_free(scratch.buffer);
    return FX_OK;
}

FxResult fx_blur(const FxImage *src, const RECT *exec, UINT32 *dst, INT dst_stride, float radius, INT expand,
                 const RECT *bounds)
{
    if (radius == 0.0f) {
        RECT whole = {0, 0, src->width, src->height}, area = whole;
        if (exec)
            fx_intersect(&area, &whole, exec);
        INT w = area.right - area.left;
        const UINT32 *from = src->pixels + (SSIZE_T)area.top * src->stride + area.left;
        for (INT y = 0; y < area.bottom - area.top && from != dst; y++)
            memcpy(dst + (SSIZE_T)y * dst_stride, from + (SSIZE_T)y * src->stride, sizeof(UINT32) * (SIZE_T)w);
        return FX_OK;
    }
    BlurPlan *plan = gp_alloc(sizeof(*plan));
    if (!plan)
        return FX_E_OUTOFMEMORY;
    blur_plan_init(plan, radius, expand);

    RECT rect;
    if (exec)
        fx_intersect(&rect, bounds, exec);
    else
        rect = *bounds;
    INT w = rect.right - rect.left, h = rect.bottom - rect.top;
    FxResult hr = FX_OK;
    BOOL inside = rect.left >= 0 && rect.top >= 0 && rect.right <= src->width && rect.bottom <= src->height;
    if (w <= 0 || h <= 0) {
        hr = FX_OK;
    } else if (expand == 0 || inside) {
        hr = blur_area(plan, src->pixels + (SSIZE_T)rect.top * src->stride + rect.left, src->stride, dst, dst_stride, w,
                       h);
    } else {
        /* the area reaches beyond the image: it is blurred together with a border of transparent pixels. */
        INT grow = fx_float_to_int(mime_vec_ceilf(radius));
        RECT region = {rect.left - grow, rect.top - grow, rect.right + grow, rect.bottom + grow}, limited;
        fx_intersect(&limited, &region, bounds);
        INT rw = limited.right - limited.left, rh = limited.bottom - limited.top;
        UINT32 *buffer = gp_alloc_zero(sizeof(UINT32) * (SIZE_T)rw * (SIZE_T)rh);
        if (!buffer) {
            hr = FX_E_OUTOFMEMORY;
        } else {
            RECT image = {0, 0, src->width, src->height}, copy;
            fx_intersect(&copy, &limited, &image);
            for (INT y = copy.top; y < copy.bottom; y++)
                memcpy(buffer + (SSIZE_T)(y - limited.top) * rw + (copy.left - limited.left),
                       src->pixels + (SSIZE_T)y * src->stride + copy.left,
                       sizeof(UINT32) * (SIZE_T)(copy.right - copy.left));
            hr = blur_area(plan, buffer, rw, buffer, rw, rw, rh);
            if (hr == FX_OK)
                for (INT y = rect.top; y < rect.bottom; y++)
                    memcpy(dst + (SSIZE_T)(y - rect.top) * dst_stride,
                           buffer + (SSIZE_T)(y - limited.top) * rw + (rect.left - limited.left),
                           sizeof(UINT32) * (SIZE_T)w);
            gp_free(buffer);
        }
    }
    gp_free(plan);
    return hr;
}

/* ------------------------------------------------------------------ sharpen */

/*
 * the sharpened pixel is the original scaled up by a/(2a-1) minus the blurred one scaled by (1-a)/(2a-1), with six
 * fractional bits. the alpha is then raised to the largest color so a color never exceeds it.
 */
static void unsharp_row(const UINT32 *orig, const UINT32 *blurred, UINT32 *out, INT count, float strength)
{
    double denom = (double)strength + (double)strength - 1.0;
    INT up = fx_float_to_int((float)((double)strength / denom) * 262144.0f + 2048.0f) >> 12;
    INT down = fx_float_to_int((float)((1.0 - (double)strength) / denom) * 262144.0f + 2048.0f) >> 12;
    for (INT i = 0; i < count; i++) {
        UINT32 o = orig[i], b = blurred[i];
        INT r = (up * (INT)((o >> 16) & 255) - down * (INT)((b >> 16) & 255) + 32) >> 6;
        INT g = (up * (INT)((o >> 8) & 255) - down * (INT)((b >> 8) & 255) + 32) >> 6;
        INT bl = (up * (INT)(o & 255) - down * (INT)(b & 255) + 32) >> 6;
        INT a = (up * (INT)(o >> 24) - down * (INT)(b >> 24) + 32) >> 6;
        BYTE cr = fx_saturate(r), cg = fx_saturate(g), cb = fx_saturate(bl), ca = fx_saturate(a);
        if (ca < 255) {
            BYTE most = cr > cg ? cr : cg;
            if (cb > most)
                most = cb;
            if (ca < most)
                ca = most;
        }
        out[i] = (UINT32)ca << 24 | (UINT32)cr << 16 | (UINT32)cg << 8 | cb;
    }
}

FxResult fx_sharpen(const FxImage *src, const RECT *exec, UINT32 *dst, INT dst_stride, float radius, float amount,
                    const RECT *bounds)
{
    FxResult hr = fx_blur(src, exec, dst, dst_stride, radius, 0, bounds);
    if (hr != FX_OK)
        return hr;
    RECT rect = *bounds;
    if (exec)
        fx_intersect(&rect, bounds, exec);
    float strength = (0.4f - (amount * 0.4f) / 100.0f) + 0.6f;
    INT w = rect.right - rect.left;
    for (INT y = 0; y < rect.bottom - rect.top; y++) {
        UINT32 *row = dst + (SSIZE_T)y * dst_stride;
        const UINT32 *orig = src->pixels + (SSIZE_T)(rect.top + y) * src->stride + rect.left;
        unsharp_row(orig, row, row, w, strength);
    }
    return FX_OK;
}
