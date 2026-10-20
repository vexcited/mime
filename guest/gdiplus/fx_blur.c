/*
 * blur and sharpen. the blur is a gaussian run along the rows and then along the columns, each line on its own
 * for the four bytes of a pixel. a large radius works on lines that are averaged down by a power of two and
 * brought back up by interpolation, which keeps the kernel short.
 */
#include "fx_core.h"

#define BLUR_MAX_TAPS 40

typedef struct BlurState {
    float radius;
    INT expand;
} BlurState;

typedef struct SharpenState {
    float radius;
    float amount;
} SharpenState;

/* the edge of a line is mirrored about its end pixel, or transparent. */
enum { EDGE_MIRROR, EDGE_TRANSPARENT };

/* ------------------------------------------------------------------ the kernel */

typedef struct BlurPlan {
    INT shift;           /* the lines are averaged in blocks of 1 << shift pixels */
    INT taps;            /* the center and the taps - 1 on each side */
    INT reach;           /* the pixels the edge of a line is made up for */
    INT edge;
    /* the product of a byte and the weight of each tap, for the four bytes of a sum. */
    UINT16 product[BLUR_MAX_TAPS][256];
} BlurPlan;

/* the blocks double with every ten radius steps above two. a radius under one is not reduced. */
static INT plan_shift(float radius)
{
    if (radius < 1.0f)
        return 0;
    float tenth = (float)((double)radius * 0.1);
    if (!(tenth >= 2.0f))
        return 0;
    UINT32 bits;
    memcpy(&bits, &tenth, sizeof(bits));
    INT exponent = (INT)(bits >> 23) - 127;
    return exponent > 8 ? 8 : exponent;
}

static BOOL plan_make(BlurPlan *plan, float radius, INT edge)
{
    plan->shift = plan_shift(radius);
    plan->edge = edge;
    float reduced = radius / (float)(1 << plan->shift);
    INT whole = (INT)reduced;
    INT taps = ((float)whole < reduced ? whole + 1 : whole) + 1;
    if (taps > BLUR_MAX_TAPS)
        return FALSE;
    plan->taps = taps;
    plan->reach = 2 * ((taps + 1) / 2) - 1;
    /* the weights of a gaussian, 1.4 steps wide at the radius, the center counted once and the others twice. */
    float weight[BLUR_MAX_TAPS];
    float step = 1.4f / reduced, at = 0.0f, sum = 0.0f;
    for (INT j = 0; j < taps; j++) {
        weight[j] = (float)gp_exp(-(double)(at * at));
        sum += (j == 0 ? 1.0f : 2.0f) * weight[j];
        at += step;
    }
    float norm = (float)(1.0 / (double)sum);
    for (INT j = 0; j < taps; j++)
        weight[j] *= norm;
    /* the center is met on both sides of the sum below. */
    weight[0] *= 0.5f;
    for (INT j = 0; j < taps; j++)
        for (INT b = 0; b < 256; b++)
            plan->product[j][b] = (UINT16)((INT)((float)b * weight[j] * 262144.0f + 512.0f) >> 10);
    return TRUE;
}

/* ------------------------------------------------------------------ one line */

typedef struct LineBuffers {
    BYTE *padded;   /* a line with the edge made up for on both sides */
    BYTE *samples;  /* the averaged line */
    BYTE *smooth;   /* the averaged line after the filter */
    BYTE *whole;    /* the result before it is put in its column */
} LineBuffers;

/* the line in padded + reach, with the pixels beyond its ends. */
static void pad_line(const BlurPlan *plan, BYTE *padded, const BYTE *line, INT n)
{
    INT reach = plan->reach;
    BYTE *at = padded + reach;
    memcpy(at, line, (SIZE_T)n);
    for (INT i = 1; i <= reach; i++) {
        BYTE before, after;
        if (plan->edge == EDGE_TRANSPARENT) {
            before = after = 0;
        } else if (n > reach) {
            before = line[i];
            after = line[n - 1 - i];
        } else {
            /* a line this short has nothing to mirror, its end pixels go on. */
            before = line[0];
            after = line[n - 1];
        }
        at[-i] = before;
        at[n - 1 + i] = after;
    }
}

static void filter_line(const BlurPlan *plan, const BYTE *padded, INT n, BYTE *out)
{
    const BYTE *at = padded + plan->reach;
    for (INT x = 0; x < n; x++) {
        INT sum = 2 * plan->product[0][at[x]];
        for (INT j = 1; j < plan->taps; j++)
            sum += plan->product[j][at[x - j]] + plan->product[j][at[x + j]];
        sum = (sum + 128) >> 8;
        out[x] = (BYTE)(sum > 255 ? 255 : sum);
    }
}

/* how a value changes from one pixel to the next on the way to the next sample, in 1/128 of a byte. */
static INT slope_of(const BYTE *s, INT i, INT block) { return ((INT)s[i + 1] - (INT)s[i]) * 128 / block; }

/* a value that left the range of a 16 bit number is taken to the end the slope points to. */
static INT keep_in_range(INT v, INT slope)
{
    if (v < 0 || v > 32767)
        return slope < 0 ? 0 : 32640;
    return v;
}

/*
 * brings the samples back to n pixels. a sample stands for the middle of its block, between two samples the
 * value is on the line between them, and before the first and after the last the slope goes on.
 */
static void scale_up(const BYTE *s, INT m, INT shift, BYTE *out, INT n)
{
    INT block = 1 << shift, half = block >> 1;
    INT first = m > 1 ? slope_of(s, 0, block) : 0;
    INT back = -first;
    /* the pixels before the middle of the first block, walked from the middle outwards. */
    INT lead = fx_min(half, n);
    INT v = keep_in_range(back / 2 + ((INT)s[0] << 7), back);
    for (INT p = lead - 1; p >= 0; p--) {
        out[p] = (BYTE)(v >> 7);
        v = keep_in_range(v + back, back);
    }
    INT pos = half, last = back;
    for (INT i = 0; i + 1 < m && pos < n; i++) {
        INT slope = slope_of(s, i, block);
        INT end = fx_min(pos + block, n);
        v = ((INT)s[i] << 7) + slope / 2;
        for (; pos < end; pos++) {
            out[pos] = (BYTE)(v >> 7);
            v += slope;
        }
        last = slope;
    }
    if (pos < n) {
        v = keep_in_range(last / 2 + ((INT)s[m - 1] << 7), last);
        for (; pos < n; pos++) {
            out[pos] = (BYTE)(v >> 7);
            v = keep_in_range(v + last, last);
        }
    }
}

static void blur_line(const BlurPlan *plan, const BYTE *line, INT n, BYTE *out, LineBuffers *buf)
{
    if (plan->shift == 0) {
        pad_line(plan, buf->padded, line, n);
        filter_line(plan, buf->padded, n, out);
        return;
    }
    INT block = 1 << plan->shift;
    INT m = (n + block - 1) / block;
    for (INT i = 0; i < m; i++) {
        INT count = fx_min(block, n - i * block), sum = 0;
        for (INT k = 0; k < count; k++)
            sum += line[i * block + k];
        buf->samples[i] = (BYTE)((sum + count / 2) / count);
    }
    pad_line(plan, buf->padded, buf->samples, m);
    filter_line(plan, buf->padded, m, buf->smooth);
    scale_up(buf->smooth, m, plan->shift, out, n);
}

/*
 * blurs the lines of a plane of w by h bytes and stores the result turned, a pixel at x, y of the input goes to
 * y, x of the output. two passes bring the plane back the right way.
 */
static void blur_pass(const BlurPlan *plan, const BYTE *in, INT w, INT h, BYTE *out, LineBuffers *buf)
{
    for (INT y = 0; y < h; y++) {
        blur_line(plan, in + (SIZE_T)y * w, w, buf->whole, buf);
        for (INT x = 0; x < w; x++)
            out[(SIZE_T)x * h + y] = buf->whole[x];
    }
}

/* the blurred planes of the pixels in bw by bh, with the source at ox and oy. blue, green, red and alpha. */
static GpStatus blur_pixels(const UINT32 *src, INT sw, INT sh, INT ox, INT oy, INT bw, INT bh, float radius, INT edge,
                            BYTE **planes)
{
    BlurPlan *plan = gp_alloc(sizeof(*plan));
    if (!plan)
        return OutOfMemory;
    if (!plan_make(plan, radius, edge)) {
        gp_free(plan);
        return InvalidParameter;
    }
    SIZE_T count = (SIZE_T)bw * bh;
    INT longest = fx_max(bw, bh);
    LineBuffers buf;
    BYTE *work = gp_alloc((SIZE_T)longest * 4 + 4 * (SIZE_T)plan->reach + 64);
    BYTE *a = gp_alloc_zero(count * 4), *b = gp_alloc(count * 4);
    if (!work || !a || !b) {
        gp_free(plan);
        gp_free(work);
        gp_free(a);
        gp_free(b);
        return OutOfMemory;
    }
    buf.padded = work;
    buf.samples = work + longest + 2 * plan->reach + 16;
    buf.smooth = buf.samples + longest;
    buf.whole = buf.smooth + longest;
    for (INT y = 0; y < sh; y++) {
        const UINT32 *row = src + (SIZE_T)y * sw;
        for (INT x = 0; x < sw; x++) {
            SIZE_T at = (SIZE_T)(y + oy) * bw + (x + ox);
            UINT32 p = row[x];
            a[at] = (BYTE)p;
            a[count + at] = (BYTE)(p >> 8);
            a[2 * count + at] = (BYTE)(p >> 16);
            a[3 * count + at] = (BYTE)(p >> 24);
        }
    }
    for (INT c = 0; c < 4; c++) {
        blur_pass(plan, a + c * count, bw, bh, b + c * count, &buf);
        blur_pass(plan, b + c * count, bh, bw, a + c * count, &buf);
    }
    gp_free(plan);
    gp_free(work);
    gp_free(b);
    *planes = a;
    return Ok;
}

/* ------------------------------------------------------------------ the blur */

static void blur_init(void *state)
{
    BlurState *s = state;
    s->radius = 10.0f;
    s->expand = 0;
}

static UINT blur_size(const void *state) { return sizeof(BlurParams); }

static GpStatus blur_set(void *state, const void *params, UINT size)
{
    BlurState *s = state;
    const BlurParams *p = params;
    if (size != sizeof(BlurParams))
        return InvalidParameter;
    /* the comparison is false for a number that is not one. */
    if (!(p->radius >= 0.0f && p->radius <= 256.0f))
        return InvalidParameter;
    s->radius = p->radius;
    s->expand = p->expandEdge;
    return Ok;
}

static void blur_get(const void *state, void *out)
{
    const BlurState *s = state;
    BlurParams *p = out;
    p->radius = s->radius;
    p->expandEdge = s->expand;
}

static INT blur_margin(const void *state)
{
    const BlurState *s = state;
    if (!s->expand)
        return 0;
    INT whole = (INT)s->radius;
    return (float)whole < s->radius ? whole + 1 : whole;
}

static GpStatus blur_run(void *state, const FxJob *job)
{
    const BlurState *s = state;
    INT aw = job->area.right - job->area.left, ah = job->area.bottom - job->area.top;
    if (s->radius <= 0.0f) {
        for (INT y = 0; y < ah; y++)
            memcpy(job->dst + (SIZE_T)y * aw, fx_src_row(job, job->area.top + y), (SIZE_T)aw * 4);
        return Ok;
    }
    INT margin = blur_margin(s);
    INT bw = job->src_w + 2 * margin, bh = job->src_h + 2 * margin;
    BYTE *planes;
    GpStatus st = blur_pixels(job->src, job->src_w, job->src_h, margin, margin, bw, bh, s->radius,
                              s->expand == 1 ? EDGE_TRANSPARENT : EDGE_MIRROR, &planes);
    if (st != Ok)
        return st;
    SIZE_T count = (SIZE_T)bw * bh;
    for (INT y = 0; y < ah; y++) {
        /* the area is in input coordinates, the planes start margin pixels before the input. */
        SIZE_T at = (SIZE_T)(job->area.top + y + margin) * bw + (job->area.left + margin);
        for (INT x = 0; x < aw; x++, at++)
            job->dst[(SIZE_T)y * aw + x] = (UINT32)planes[3 * count + at] << 24 | (UINT32)planes[2 * count + at] << 16 |
                                           (UINT32)planes[count + at] << 8 | planes[at];
    }
    gp_free(planes);
    return Ok;
}

const FxClass fx_class_blur = {
    &BlurEffectGuid, sizeof(BlurState), FX_NEIGHBORS, blur_init, nullptr, blur_size, blur_set, blur_get, blur_margin,
    blur_run, nullptr,
};

/* ------------------------------------------------------------------ the sharpen */

static void sharpen_init(void *state)
{
    SharpenState *s = state;
    s->radius = 10.0f;
    s->amount = 50.0f;
}

static UINT sharpen_size(const void *state) { return sizeof(SharpenParams); }

static GpStatus sharpen_set(void *state, const void *params, UINT size)
{
    SharpenState *s = state;
    const SharpenParams *p = params;
    if (size != sizeof(SharpenParams))
        return InvalidParameter;
    /* both are checked before either is kept. */
    if (!(p->radius >= 0.0f && p->radius <= 256.0f) || !(p->amount >= 0.0f && p->amount <= 100.0f))
        return InvalidParameter;
    s->radius = p->radius;
    s->amount = p->amount;
    return Ok;
}

static void sharpen_get(const void *state, void *out)
{
    const SharpenState *s = state;
    SharpenParams *p = out;
    p->radius = s->radius;
    p->amount = s->amount;
}

static GpStatus sharpen_run(void *state, const FxJob *job)
{
    const SharpenState *s = state;
    INT aw = job->area.right - job->area.left, ah = job->area.bottom - job->area.top;
    INT bw = job->src_w, bh = job->src_h;
    SIZE_T count = (SIZE_T)bw * bh;
    BYTE *planes = nullptr;
    if (s->radius > 0.0f) {
        GpStatus st = blur_pixels(job->src, bw, bh, 0, 0, bw, bh, s->radius, EDGE_MIRROR, &planes);
        if (st != Ok)
            return st;
    }
    /* the picture is the original pushed away from the blurred one, in weights with 6 fractional bits. */
    float strength = (0.4f - s->amount * 0.4f / 100.0f) + 0.6f;
    double denom = 2.0 * strength - 1.0;
    INT push = (INT)((float)(strength / denom) * 262144.0f + 2048.0f) >> 12;
    INT pull = (INT)((float)((1.0 - strength) / denom) * 262144.0f + 2048.0f) >> 12;
    for (INT y = 0; y < ah; y++) {
        const UINT32 *orig = fx_src_row(job, job->area.top + y);
        SIZE_T at = (SIZE_T)(job->area.top + y) * bw + job->area.left;
        for (INT x = 0; x < aw; x++, at++) {
            UINT32 o = orig[x];
            INT o4[4] = {(INT)(o & 255), (INT)((o >> 8) & 255), (INT)((o >> 16) & 255), (INT)(o >> 24)};
            INT n4[4];
            for (INT c = 0; c < 4; c++) {
                INT blurred = planes ? planes[c * count + at] : o4[c];
                n4[c] = fx_byte((push * o4[c] - pull * blurred + 32) >> 6);
            }
            /* a pixel that is not opaque is at least as opaque as its brightest color. */
            if (n4[3] < 255)
                n4[3] = fx_max(n4[3], fx_max(n4[2], fx_max(n4[1], n4[0])));
            job->dst[(SIZE_T)y * aw + x] = (UINT32)n4[3] << 24 | (UINT32)n4[2] << 16 | (UINT32)n4[1] << 8 | (UINT32)n4[0];
        }
    }
    gp_free(planes);
    return Ok;
}

const FxClass fx_class_sharpen = {
    &SharpenEffectGuid, sizeof(SharpenState), FX_NEIGHBORS, sharpen_init, nullptr, sharpen_size, sharpen_set,
    sharpen_get, nullptr, sharpen_run, nullptr,
};
