/*
 * red eye correction. for each area the effect decides where the eye is and how big it is, learns how red and how
 * dark each of 60 sectors around the center is, and then pulls the red of the pixels in that circle towards the
 * other colors by a limit the surroundings give. what the analysis adds up is not started again for the next area
 * or the next run, the numbers are kept in the effect.
 */
#include "fx_core.h"

#define SECTORS 60

typedef struct EyeState {
    RECT *areas;
    UINT count;
    /* sums that go on from area to area. */
    float red_x, red_y, dark_x, dark_y;
    INT brightest, brightest_x, brightest_y;
    INT low, high;
    /* the circle. */
    float center_x, center_y, radius;
    BOOL pale, small_area;
    float sector_red[SECTORS], sector_base[SECTORS];
} EyeState;

/* ------------------------------------------------------------------ parameters */

static void eye_finish(void *state)
{
    gp_free(((EyeState *)state)->areas);
}

static UINT eye_size(const void *state)
{
    return sizeof(RedEyeCorrectionParams) + ((const EyeState *)state)->count * sizeof(RECT);
}

static GpStatus eye_set(void *state, const void *params, UINT size)
{
    EyeState *s = state;
    if (size < sizeof(RedEyeCorrectionParams))
        return InvalidParameter;
    UINT count = ((const RedEyeCorrectionParams *)params)->numberOfAreas;
    if (count == 0 || count > (size - sizeof(RedEyeCorrectionParams)) / sizeof(RECT))
        return InvalidParameter;
    const RECT *given = (const RECT *)((const BYTE *)params + sizeof(RedEyeCorrectionParams));
    for (UINT i = 0; i < count; i++)
        if (given[i].right <= given[i].left || given[i].bottom <= given[i].top)
            return InvalidParameter;
    RECT *copy = gp_dup(given, count * sizeof(RECT));
    if (!copy)
        return OutOfMemory;
    gp_free(s->areas);
    s->areas = copy;
    s->count = count;
    return Ok;
}

static void eye_get(const void *state, void *out)
{
    const EyeState *s = state;
    RedEyeCorrectionParams *p = out;
    RECT *rects = (RECT *)((BYTE *)out + sizeof(RedEyeCorrectionParams));
    p->numberOfAreas = s->count;
    p->areas = rects;
    if (s->count)
        memcpy(rects, s->areas, s->count * sizeof(RECT));
}

/* ------------------------------------------------------------------ pixels and angles */

/* the pixels the analysis reads, which are the result so far when the bitmap is worked on in place. */
typedef struct EyePixels {
    const UINT32 *bits;
    INT x0, y0, width;
} EyePixels;

static UINT32 pixel_at(const EyePixels *p, INT x, INT y) { return p->bits[(SIZE_T)(y - p->y0) * p->width + (x - p->x0)]; }

static float distance_of(float dx, float dy) { return mime_vec_sqrtf(dy * dy + dx * dx); }

/* how far red leads the other colors, nothing when it does not. */
static float lead_of(float r, float g, float b)
{
    if (!(r > g && r > b))
        return 0.0f;
    return g <= b ? r - b : r - g;
}

/* the slice of 6 degrees a direction from the center is in. a pixel in the row of the center and left of it is in the first. */
static INT sector_of(float dx, float dy)
{
    float degrees;
    if (dx == 0.0f) {
        degrees = dy < 0.0f ? 270.0f : 90.0f;
    } else if (dy == 0.0f) {
        degrees = 0.0f;
    } else {
        degrees = mime_vec_atan2f(dy, dx) * (180.0f / 3.14159265f);
        if (degrees < 0.0f)
            degrees += 360.0f;
    }
    INT sector = (INT)(degrees / 6.0f);
    return sector < 0 ? 0 : sector >= SECTORS ? SECTORS - 1 : sector;
}

/* ------------------------------------------------------------------ the analysis */

static void channels_of(UINT32 p, float *r, float *g, float *b)
{
    *r = (float)((p >> 16) & 255);
    *g = (float)((p >> 8) & 255);
    *b = (float)(p & 255);
}

/* a pale eye has light green and blue near the middle of the area, a flash on a washed out face. */
static void find_pale(EyeState *s, const EyePixels *px, const RECT *area)
{
    float mx = (float)(area->left + area->right) * 0.5f, my = (float)(area->top + area->bottom) * 0.5f;
    float sum_r = 0, sum_g = 0, sum_b = 0, count = 0;
    for (INT y = area->top; y < area->bottom; y++) {
        for (INT x = area->left; x < area->right; x++) {
            UINT32 p = pixel_at(px, x, y);
            float dy = (float)y - my, dx = (float)x - mx;
            if ((p >> 24) == 0 || !(dy * dy + dx * dx < 121.0f))
                continue;
            float r, g, b;
            channels_of(p, &r, &g, &b);
            sum_r += r;
            sum_g += g;
            sum_b += b;
            count += 1.0f;
        }
    }
    float mean_r = 0, mean_g = 0, mean_b = 0;
    if (count > 0.0f) {
        mean_r = sum_r / count;
        mean_g = sum_g / count;
        mean_b = sum_b / count;
    }
    s->pale = mean_g > 150.0f && mean_b > 150.0f && mean_g + 10.0f > mean_r && mean_b + 10.0f > mean_r;
}

/* the middle of the eye from where the red is and where it is dark, and a first guess of the radius. */
static void find_center(EyeState *s, const EyePixels *px, const RECT *area)
{
    INT histogram[256] = {0};
    float red_total = 0, dark_total = 0, seen = 0;
    for (INT y = area->top; y < area->bottom; y++) {
        for (INT x = area->left; x < area->right; x++) {
            UINT32 p = pixel_at(px, x, y);
            if ((p >> 24) == 0)
                continue;
            float r, g, b;
            channels_of(p, &r, &g, &b);
            float weight = lead_of(r, g, b);
            INT level = (BYTE)fx_floor_int((2.0f * b + 9.0f * g) / 11.0f + 0.5f);
            if (weight > 0.0f)
                weight = level != 0 ? weight / (float)level : 1.0f;
            histogram[level]++;
            red_total += weight;
            dark_total += (float)(255 - level);
            seen += 1.0f;
            s->red_x = (float)x * weight + s->red_x;
            s->red_y = (float)y * weight + s->red_y;
            s->dark_x = (float)(x * (255 - level)) + s->dark_x;
            s->dark_y = (float)(y * (255 - level)) + s->dark_y;
            if (level > s->brightest) {
                s->brightest = level;
                s->brightest_x = x;
                s->brightest_y = y;
            }
        }
    }
    if (red_total != 0.0f) {
        s->red_x /= red_total;
        s->red_y /= red_total;
    }
    /* with nothing dark the earlier circle stays. */
    if (dark_total == 0.0f)
        return;
    s->dark_x /= dark_total;
    s->dark_y /= dark_total;
    /* where the darkest 30 and the darkest 70 percent end. */
    INT below = 0;
    for (INT i = 0; i < 256; i++) {
        below += histogram[i];
        double share = (double)((float)below / seen);
        if (share < 0.3)
            s->low = i;
        if (share < 0.7)
            s->high = i;
    }
    INT size = (area->right + area->bottom) - area->top - area->left;
    float gap_y = s->dark_y - s->red_y, gap_x = s->dark_x - s->red_x;
    float gap = gap_y * gap_y + gap_x * gap_x;
    float half = (float)size * 0.5f;
    float near_enough = (half * half) / 9.0f;
    if (gap < near_enough) {
        s->center_x = s->red_x;
        s->center_y = s->red_y;
    } else {
        s->center_x = s->dark_x;
        s->center_y = s->dark_y;
    }
    s->small_area = near_enough > gap;
    if (s->pale) {
        s->small_area = FALSE;
        s->center_x = (float)(area->left + area->right) * 0.5f;
        s->center_y = (float)(area->top + area->bottom) * 0.5f;
    }
    /* the radius reaches from the center to the nearest edge of the area, counted from the coordinates themselves. */
    float cx = s->center_x, cy = s->center_y;
    if (cx <= cy) {
        s->radius = cx;
        if (cy <= (float)((area->bottom + area->top) / 2)) {
            if ((float)area->top > cy - cx)
                s->radius = cy - (float)area->top;
        } else if (cy + cx > (float)area->bottom) {
            s->radius = (float)area->bottom - cy;
        }
    } else {
        s->radius = cy;
        if (cx <= (float)((area->right + area->left) / 2)) {
            if ((float)area->left > cx - cy)
                s->radius = cx - (float)area->left;
        } else if (cy + cx > (float)area->right) {
            s->radius = (float)area->right - cx;
        }
    }
}

/* the rim of a pale eye, and the mean red and the mean of the other colors in each sector of the circle. */
static void find_references(EyeState *s, const EyePixels *px, const RECT *area)
{
    if (s->pale) {
        UINT darkest[SECTORS];
        float rim[SECTORS];
        for (INT i = 0; i < SECTORS; i++) {
            darkest[i] = 0xffffffffu;
            rim[i] = 0.0f;
        }
        for (INT y = area->top; y < area->bottom; y++) {
            for (INT x = area->left; x < area->right; x++) {
                UINT32 p = pixel_at(px, x, y);
                if ((p >> 24) == 0)
                    continue;
                float r, g, b;
                channels_of(p, &r, &g, &b);
                UINT level = (BYTE)fx_floor_int(((g * 9.0f + r * 5.0f) + (b + b)) * 0.0625f + 0.5f);
                float dx = (float)x - s->center_x, dy = (float)y - s->center_y;
                float d2 = dy * dy + dx * dx;
                INT sector = sector_of(dx, dy);
                if (darkest[sector] > level && d2 >= 121.0f) {
                    darkest[sector] = level;
                    rim[sector] = mime_vec_sqrtf(d2);
                }
            }
        }
        float total = 0.0f;
        for (INT i = 0; i < SECTORS; i++)
            total += rim[i];
        float mean = total / (float)SECTORS;
        if (mean < s->radius)
            s->radius = mean;
    }
    float red_sum[SECTORS], base_sum[SECTORS], count[SECTORS];
    for (INT i = 0; i < SECTORS; i++)
        red_sum[i] = base_sum[i] = count[i] = 0.0f;
    float limit = s->radius * s->radius;
    for (INT y = area->top; y < area->bottom; y++) {
        for (INT x = area->left; x < area->right; x++) {
            UINT32 p = pixel_at(px, x, y);
            if ((p >> 24) == 0)
                continue;
            float dx = (float)x - s->center_x, dy = (float)y - s->center_y;
            if (!(limit > dy * dy + dx * dx))
                continue;
            float r, g, b;
            channels_of(p, &r, &g, &b);
            INT sector = sector_of(dx, dy);
            red_sum[sector] += lead_of(r, g, b);
            base_sum[sector] += (2.0f * b + 9.0f * g) / 11.0f;
            count[sector] += 1.0f;
        }
    }
    for (INT i = 0; i < SECTORS; i++) {
        if (count[i] > 0.0f) {
            s->sector_red[i] = red_sum[i] / count[i];
            s->sector_base[i] = base_sum[i] / count[i];
        } else {
            s->sector_red[i] = 0.0f;
            s->sector_base[i] = 1.0f;
        }
    }
}

/* ------------------------------------------------------------------ the correction */

static void correct_area(EyeState *s, const EyePixels *px, const FxJob *job, const RECT *area)
{
    /* how fast the limit falls to the middle of a small eye, from how widely its levels spread. */
    float falloff = 1.0f;
    if (s->small_area) {
        UINT spread = (UINT)(s->high - s->low);
        falloff = spread <= 25 ? -0.25f : spread <= 40 ? -0.5f : spread <= 80 ? -0.66f : -0.75f;
    }
    float mx = (float)(area->left + area->right) * 0.5f, my = (float)(area->top + area->bottom) * 0.5f;
    float bx = (float)s->brightest_x, by = (float)s->brightest_y;
    float spot_distance = distance_of(s->center_x - bx, s->center_y - by);
    INT aw = job->area.right - job->area.left;
    for (INT y = area->top; y < area->bottom; y++) {
        for (INT x = area->left; x < area->right; x++) {
            UINT32 p = pixel_at(px, x, y);
            float dx = (float)x - s->center_x, dy = (float)y - s->center_y;
            float d = distance_of(dx, dy);
            if ((p >> 24) == 0 || d >= s->radius)
                continue;
            float r, g, b;
            channels_of(p, &r, &g, &b);
            if (s->pale) {
                float soft = s->radius / 5.0f;
                if (soft < 1.0f)
                    soft = 2.0f;
                float to_mid = distance_of((float)x - mx, (float)y - my);
                float from_spot = s->radius / 3.0f <= spot_distance ? to_mid : distance_of((float)x - bx, (float)y - by);
                float near_spot = 1.0f - ((from_spot * from_spot / soft) / soft);
                if (near_spot < 0.0f)
                    near_spot = 0.0f;
                float outside = to_mid - s->radius;
                float rim = 1.0f - ((((outside + outside) * outside) / s->radius) / s->radius);
                float keep = rim <= near_spot ? near_spot : rim;
                r *= keep;
                g *= keep;
                b *= keep;
            }
            if (!(r > g && r > b))
                continue;
            INT sector = sector_of(dx, dy);
            /* the limit the sector gives, lower when the pixel is not as light as the sector is. */
            float by_sector = 255.0f, base = s->sector_base[sector];
            if (base != 0.0f) {
                float here = (2.0f * b + 9.0f * g) / 11.0f;
                if (base + base <= here) {
                    by_sector = s->sector_red[sector];
                } else {
                    float off = here - base;
                    by_sector = (((off * off) / ((base * -2.0f) * base)) + 1.0f) * s->sector_red[sector];
                }
            }
            float by_radius;
            if (s->small_area) {
                float in = d - s->radius;
                by_radius = (((in * in) * falloff) / (s->radius * s->radius)) + 1.0f;
            } else {
                by_radius = ((d / s->radius) / 3.0f) + 0.667f;
            }
            by_radius *= s->sector_red[sector];
            /* near a bright spot of the eye its own red is kept. */
            float by_spot = 255.0f;
            if ((UINT)(s->brightest - s->high) > 40 && s->radius * 0.667f > spot_distance) {
                float sy = (float)y - by, sx = (float)x - bx;
                by_spot = (sy * sy + sx * sx) * 5.0f;
            }
            float cap = by_spot < by_sector ? by_spot : by_sector;
            if (by_radius < cap)
                cap = by_radius;
            float base_color = g - b < 0.0f ? b : g;
            float lead = r - base_color;
            if (lead <= cap)
                continue;
            float share = ((lead - cap) * 5.0f) * 0.0625f;
            UINT32 ng = fx_round_byte(share + g), nb = fx_round_byte(share + b);
            UINT32 nr = fx_round_byte((share + base_color) + cap);
            job->dst[(SIZE_T)(y - job->area.top) * aw + (x - job->area.left)] = (p & 0xff000000u) | nr << 16 | ng << 8 | nb;
        }
    }
}

static GpStatus eye_run(void *state, const FxJob *job)
{
    EyeState *s = state;
    INT aw = job->area.right - job->area.left;
    for (INT y = job->area.top; y < job->area.bottom; y++)
        memcpy(fx_dst_row(job, y), fx_src_row(job, y), (SIZE_T)aw * 4);
    if (s->count == 0)
        return InvalidParameter;
    EyePixels px;
    if (job->in_place) {
        px.bits = job->dst;
        px.x0 = job->area.left;
        px.y0 = job->area.top;
        px.width = aw;
    } else {
        px.bits = job->src;
        px.x0 = job->src_x;
        px.y0 = job->src_y;
        px.width = job->src_w;
    }
    for (UINT i = 0; i < s->count; i++) {
        RECT *a = &s->areas[i];
        a->left = fx_max(a->left, job->area.left);
        a->top = fx_max(a->top, job->area.top);
        a->right = fx_min(a->right, job->area.right);
        a->bottom = fx_min(a->bottom, job->area.bottom);
        if (a->right <= a->left || a->bottom <= a->top)
            return InvalidParameter;
        find_pale(s, &px, a);
        find_center(s, &px, a);
        find_references(s, &px, a);
        correct_area(s, &px, job, a);
    }
    return Ok;
}

const FxClass fx_class_redeye = {
    &RedEyeCorrectionEffectGuid, sizeof(EyeState), 0, nullptr, eye_finish, eye_size, eye_set, eye_get, nullptr, eye_run,
    nullptr,
};
