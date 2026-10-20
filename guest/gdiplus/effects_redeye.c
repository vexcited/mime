/*
 * red eye correction: the pixels of each area given by the program whose red clearly leads over green and blue
 * are pulled toward the other colors. the area is analysed first, a circle that holds the pupil is found, then the
 * redness of each of sixty sectors of the circle is measured and used to limit how far the pixels are corrected.
 *
 * the analysis keeps some of its sums from one area to the next and from one run to the next, as the library does,
 * and the results depend on it.
 */
#include "fx_private.h"

#define SECTORS 60

struct FxRedEye {
    UINT count;
    RECT *areas;
    /* found for the area that is being worked on */
    BOOL small_area;      /* the two centers agree, the correction falls off toward the rim faster */
    BOOL pale;            /* the area is bright without a red tint, a catchlight on white */
    float center_x, center_y, radius;
    INT low_percentile, high_percentile;
    float sector_red[SECTORS];   /* the mean redness of the sector */
    float sector_base[SECTORS];  /* the mean of the other colors there */
    /* sums that are never started again */
    float red_x, red_y;          /* the sum of the position times the redness, then the mean */
    float dark_x, dark_y;        /* the same weighted by the darkness */
    INT brightest;               /* the largest, with the position it was at */
    float brightest_x, brightest_y;
};

FxRedEye *fx_redeye_new(void)
{
    FxRedEye *r = gp_alloc_zero(sizeof(*r));
    if (r) {
        r->pale = TRUE;
    }
    return r;
}

void fx_redeye_free(FxRedEye *r)
{
    if (r)
        gp_free(r->areas);
    gp_free(r);
}

UINT fx_redeye_count(const FxRedEye *r) { return r->count; }

const RECT *fx_redeye_areas(const FxRedEye *r) { return r->areas; }

FxResult fx_redeye_set(FxRedEye *r, UINT count, const RECT *areas)
{
    if (!count || !areas)
        return FX_E_INVALIDARG;
    for (UINT i = 0; i < count; i++)
        if (areas[i].left >= areas[i].right || areas[i].top >= areas[i].bottom)
            return FX_E_INVALIDARG;
    r->count = count;
    gp_free(r->areas);
    r->areas = gp_dup(areas, sizeof(RECT) * count);
    return r->areas ? FX_OK : FX_E_OUTOFMEMORY;
}

/* ------------------------------------------------------------------ geometry */

static double sqrt_double(double x)
{
    if (!(x > 0.0))
        return 0.0;
    double r = (double)mime_vec_sqrtf((float)x);
    for (INT i = 0; i < 4; i++)
        r = 0.5 * (r + x / r);
    return r;
}

static double atan_double(double x)
{
    const double pi = 3.141592653589793;
    BOOL negative = x < 0.0;
    if (negative)
        x = -x;
    BOOL inverted = x > 1.0;
    if (inverted)
        x = 1.0 / x;
    /* halving the angle twice brings the series within a few terms of its limit */
    for (INT i = 0; i < 2; i++)
        x = x / (1.0 + sqrt_double(1.0 + x * x));
    double term = x, sum = x, sq = x * x;
    for (INT k = 1; k < 20; k++) {
        term *= -sq;
        sum += term / (2 * k + 1);
    }
    sum *= 4.0;
    if (inverted)
        sum = pi / 2.0 - sum;
    return negative ? -sum : sum;
}

/* the direction of a vector in degrees, 0 to 360, y pointing down the picture as an angle counted from the x axis. */
static double direction(double dx, double dy)
{
    const double pi = 3.141592653589793;
    if (dy < 0.0) {
        if (dx == 0.0)
            return 270.0;
        double a = atan_double(dy / dx) * 180.0 / pi;
        return a < 0.0 ? a + 360.0 : a + 180.0;
    }
    if (dx == 0.0)
        return 90.0;
    double a = atan_double(dy / dx) * 180.0 / pi;
    return a < 0.0 ? a + 180.0 : a;
}

/* the sector of the circle a vector points to, six degrees each. */
static INT sector_of(float dx, float dy)
{
    float turn = (float)direction((double)dx, (double)dy) / 6.0f;
    INT sector = fx_float_to_int(turn < 0.0f ? -mime_vec_ceilf(-turn) : mime_vec_floorf(turn));
    return sector < 0 ? 0 : sector >= SECTORS ? SECTORS - 1 : sector;
}

/* ------------------------------------------------------------------ the pixels */

typedef struct Rgba {
    float r, g, b;
} Rgba;

static const UINT32 *pixel_at(const FxImage *img, INT x, INT y) { return img->pixels + (SSIZE_T)y * img->stride + x; }

static Rgba split(UINT32 c)
{
    Rgba p = {(float)((c >> 16) & 255), (float)((c >> 8) & 255), (float)(c & 255)};
    return p;
}

/* red over the larger of the other two, or nothing. */
static float redness(const Rgba *p)
{
    if (p->r > p->g && p->r > p->b)
        return p->g <= p->b ? p->r - p->b : p->r - p->g;
    return 0.0f;
}

/* the value the other colors have, green far more than blue. */
static float other_colors(const Rgba *p) { return ((p->b + p->b) + p->g * 9.0f) / 11.0f; }

/* ------------------------------------------------------------------ analysis */

/* is the middle of the area bright and neutral, a white that holds a reflection */
static void find_base_colors(FxRedEye *r, const FxImage *img, const RECT *area)
{
    UINT sum_r = 0, sum_g = 0, sum_b = 0;
    INT n = 0;
    float cy = (float)(area->top + area->bottom) * 0.5f;
    for (INT y = area->top; y < area->bottom; y++) {
        const UINT32 *row = pixel_at(img, area->left, y);
        for (INT x = area->left; x < area->right; x++) {
            UINT32 c = row[x - area->left];
            float dy = (float)y - cy;
            float dx = (float)x - (float)(area->left + area->right) * 0.5f;
            if ((c >> 24) != 0 && dy * dy + dx * dx < 121.0f) {
                sum_r += (c >> 16) & 255;
                sum_g += (c >> 8) & 255;
                sum_b += c & 255;
                n++;
            }
        }
    }
    float mean_r = 0.0f, mean_g = 0.0f, mean_b = 0.0f;
    if (n) {
        mean_r = (float)(INT)sum_r / (float)n;
        mean_g = (float)(INT)sum_g / (float)n;
        mean_b = (float)(INT)sum_b / (float)n;
    }
    r->pale = mean_g > 150.0f && mean_b > 150.0f && mean_g + 10.0f > mean_r && mean_b + 10.0f > mean_r;
}

/* the middle of the redness, which is the center of the circle, and how far it can reach inside the area. */
static void find_center(FxRedEye *r, const FxImage *img, const RECT *area)
{
    INT histogram[256] = {0};
    float red_total = 0.0f;
    INT dark_total = 0, seen = 0;
    for (INT y = area->top; y < area->bottom; y++) {
        const UINT32 *row = pixel_at(img, area->left, y);
        for (INT x = area->left; x < area->right; x++) {
            UINT32 c = row[x - area->left];
            if ((c >> 24) == 0)
                continue;
            Rgba p = split(c);
            float w = redness(&p);
            BYTE base = (BYTE)fx_float_to_int(mime_vec_floorf(other_colors(&p) + 0.5f));
            if (w > 0.0f)
                w = base ? w / (float)base : 1.0f;
            histogram[base]++;
            red_total += w;
            dark_total += 255 - base;
            seen++;
            r->red_x = (float)x * w + r->red_x;
            r->red_y = (float)y * w + r->red_y;
            r->dark_x = (float)(x * (255 - base)) + r->dark_x;
            r->dark_y = (float)(y * (255 - base)) + r->dark_y;
            if (base > r->brightest) {
                r->brightest_x = (float)x;
                r->brightest_y = (float)y;
                r->brightest = base;
            }
        }
    }
    if (red_total != 0.0f) {
        r->red_x = r->red_x / red_total;
        r->red_y = r->red_y / red_total;
    }
    if (dark_total == 0)
        return;
    r->dark_x = r->dark_x / (float)dark_total;
    r->dark_y = r->dark_y / (float)dark_total;
    INT cumulative = 0;
    for (INT i = 0; i < 256; i++) {
        cumulative += histogram[i];
        double share = (double)((float)cumulative / (float)seen);
        if (share < 0.3)
            r->low_percentile = i;
        if (share < 0.7)
            r->high_percentile = i;
    }
    float size = (float)(area->right + area->bottom - area->top - area->left);
    float gap = (r->dark_y - r->red_y) * (r->dark_y - r->red_y) + (r->dark_x - r->red_x) * (r->dark_x - r->red_x);
    float near_enough = ((size * 0.5f) * (size * 0.5f)) / 9.0f;
    float cx = r->red_x, cy = r->red_y;
    if (near_enough <= gap) {
        cx = r->dark_x;
        cy = r->dark_y;
    }
    r->small_area = near_enough > gap;
    if (r->pale) {
        r->small_area = FALSE;
        cx = (float)(area->left + area->right) * 0.5f;
        cy = (float)(area->top + area->bottom) * 0.5f;
    }
    r->center_x = cx;
    r->center_y = cy;
    /* the radius starts from the position of the center, which is how the library has it. */
    if (cx <= cy) {
        r->radius = cx;
        if (cy <= (float)((area->bottom + area->top) / 2)) {
            if ((float)area->top > cy - cx)
                r->radius = cy - (float)area->top;
        } else if (cy + cx > (float)area->bottom) {
            r->radius = (float)area->bottom - cy;
        }
    } else {
        r->radius = cy;
        if (cx <= (float)((area->right + area->left) / 2)) {
            if ((float)area->left > cx - cy)
                r->radius = cx - (float)area->left;
        } else if (cy + cx > (float)area->right) {
            r->radius = (float)area->right - cx;
        }
    }
}

/* the sectors of the circle: the mean redness and other colors inside the radius, and a radius found from the rim. */
static void find_references(FxRedEye *r, const FxImage *img, const RECT *area)
{
    float old_radius = r->radius;
    float rim[SECTORS];
    memset(rim, 0, sizeof(rim));
    if (r->pale) {
        /* in each direction the darkest pixel at least eleven away from the center marks the rim */
        UINT darkest[SECTORS];
        memset(darkest, 0xff, sizeof(darkest));
        for (INT y = area->top; y < area->bottom; y++) {
            const UINT32 *row = pixel_at(img, area->left, y);
            for (INT x = area->left; x < area->right; x++) {
                UINT32 c = row[x - area->left];
                if ((c >> 24) == 0)
                    continue;
                Rgba p = split(c);
                INT value = (BYTE)fx_float_to_int(
                    mime_vec_floorf((((p.g * 9.0f) + (p.r * 5.0f)) + (p.b + p.b)) * 0.0625f + 0.5f));
                float dy = (float)y - r->center_y, dx = (float)x - r->center_x;
                float d2 = dy * dy + dx * dx;
                INT s = sector_of(dx, dy);
                if (darkest[s] > (UINT)value && d2 >= 121.0f) {
                    darkest[s] = (UINT)value;
                    rim[s] = mime_vec_sqrtf(d2);
                }
            }
        }
        float sum = 0.0f;
        for (INT i = 0; i < SECTORS; i++)
            sum += rim[i];
        float mean = sum / 60.0f;
        r->radius = mean > old_radius ? old_radius : mean;
    }
    INT counts[SECTORS] = {0};
    memset(r->sector_red, 0, sizeof(r->sector_red));
    memset(r->sector_base, 0, sizeof(r->sector_base));
    float limit = r->radius * r->radius;
    for (INT y = area->top; y < area->bottom; y++) {
        const UINT32 *row = pixel_at(img, area->left, y);
        for (INT x = area->left; x < area->right; x++) {
            UINT32 c = row[x - area->left];
            if ((c >> 24) == 0)
                continue;
            Rgba p = split(c);
            float dx = (float)x - r->center_x, dy = (float)y - r->center_y;
            if (limit > dy * dy + dx * dx) {
                INT s = sector_of(dx, dy);
                r->sector_red[s] = redness(&p) + r->sector_red[s];
                r->sector_base[s] = other_colors(&p) + r->sector_base[s];
                counts[s]++;
            }
        }
    }
    for (INT i = 0; i < SECTORS; i++) {
        if (counts[i]) {
            r->sector_red[i] = r->sector_red[i] / (float)counts[i];
            r->sector_base[i] = r->sector_base[i] / (float)counts[i];
        } else {
            r->sector_red[i] = 0.0f;
            r->sector_base[i] = 1.0f;
        }
    }
}

/* ------------------------------------------------------------------ correction */

static float distance(float dx, float dy) { return (float)sqrt_double((double)(dy * dy + dx * dx)); }

static void correct_area(FxRedEye *r, const FxImage *src, const RECT *exec, const RECT *area, UINT32 *dst,
                         INT dst_stride)
{
    float area_cx = (float)(area->left + area->right) * 0.5f, area_cy = (float)(area->bottom + area->top) * 0.5f;
    float falloff = 1.0f;
    if (r->small_area) {
        UINT spread = (UINT)(r->high_percentile - r->low_percentile);
        falloff = spread <= 0x50 ? (spread <= 0x28 ? (spread <= 0x19 ? -0.25f : -0.5f) : -0.66f) : -0.75f;
    }
    float radius = r->radius;
    for (INT y = area->top; y < area->bottom; y++) {
        const UINT32 *from = pixel_at(src, area->left, y);
        UINT32 *to = dst + (SSIZE_T)(y - exec->top) * dst_stride + (area->left - exec->left);
        for (INT x = area->left; x < area->right; x++, from++, to++) {
            UINT32 out = *from;
            if ((out >> 24) == 0) {
                *to = out;
                continue;
            }
            Rgba p = split(out);
            float fx = (float)x, fy = (float)y;
            float dx = fx - r->center_x, dy = fy - r->center_y;
            float away = distance(dx, dy);
            if (radius <= away) {
                *to = out;
                continue;
            }
            if (r->pale) {
                /* a fade by the distance from the bright spot, and the one from the rim, the larger stays */
                float soft = radius / 5.0f;
                if (soft < 1.0f)
                    soft = 2.0f;
                float spot = distance(r->center_x - r->brightest_x, r->center_y - r->brightest_y);
                float from_spot;
                if (radius / 3.0f <= spot)
                    from_spot = distance(fx - area_cx, fy - area_cy);
                else
                    from_spot = distance(fx - r->brightest_x, fy - r->brightest_y);
                float near_spot = 1.0f - ((from_spot * from_spot) / soft) / soft;
                if (near_spot < 0.0f)
                    near_spot = 0.0f;
                float out_of_rim = distance(fx - area_cx, fy - area_cy) - radius;
                float rim = 1.0f - ((((out_of_rim + out_of_rim) * out_of_rim) / radius) / radius);
                float keep = rim <= near_spot ? near_spot : rim;
                p.r *= keep;
                p.g *= keep;
                p.b *= keep;
            }
            if (!(p.r > p.g && p.r > p.b)) {
                *to = out;
                continue;
            }
            float green_over_blue = p.g - p.b;
            INT sector = sector_of(dx, dy);
            float by_sector = 255.0f;
            float base = r->sector_base[sector];
            if (base != 0.0f) {
                float here = other_colors(&p);
                if (base + base <= here)
                    by_sector = r->sector_red[sector];
                else
                    by_sector = ((((here - base) * (here - base)) / ((base * -2.0f) * base)) + 1.0f) * r->sector_red[sector];
            }
            float by_radius;
            if (r->small_area)
                by_radius = ((((away - radius) * (away - radius)) * falloff) / (radius * radius)) + 1.0f;
            else
                by_radius = ((away / radius) / 3.0f) + 0.667f;
            by_radius *= r->sector_red[sector];
            float by_spot = 255.0f;
            if ((UINT)(r->brightest - r->high_percentile) > 0x28) {
                float to_spot = distance(r->center_x - r->brightest_x, r->center_y - r->brightest_y);
                if (radius * 0.667f > to_spot)
                    by_spot = ((fy - r->brightest_y) * (fy - r->brightest_y) +
                               ((float)x - r->brightest_x) * ((float)x - r->brightest_x)) * 5.0f;
            }
            float cap = by_spot;
            float inner = by_sector < by_radius ? by_sector : by_radius;
            if (inner < cap)
                cap = inner;
            /* the red is brought down to the larger of the other two plus the cap, and they share the rest */
            float lead, base_color = green_over_blue < 0.0f ? p.b : p.g;
            lead = p.r - base_color;
            if (lead <= cap) {
                *to = out;
                continue;
            }
            float share = ((lead - cap) * 5.0f) * 0.0625f;
            BYTE new_g = (BYTE)fx_float_to_int(mime_vec_floorf((share + p.g) + 0.5f));
            BYTE new_b = (BYTE)fx_float_to_int(mime_vec_floorf((share + p.b) + 0.5f));
            BYTE new_r = (BYTE)fx_float_to_int(mime_vec_floorf(((share + base_color) + cap) + 0.5f));
            out = (out & 0xff000000u) | (UINT32)new_r << 16 | (UINT32)new_g << 8 | new_b;
            *to = out;
        }
    }
}

FxResult fx_redeye_run(FxRedEye *r, const FxImage *src, const RECT *rect, UINT32 *dst, INT dst_stride)
{
    if (!r->areas)
        return FX_E_INVALIDARG;
    INT w = rect->right - rect->left;
    const UINT32 *from = pixel_at(src, rect->left, rect->top);
    for (INT y = 0; y < rect->bottom - rect->top && from != dst; y++)
        memcpy(dst + (SSIZE_T)y * dst_stride, from + (SSIZE_T)y * src->stride, sizeof(UINT32) * (SIZE_T)w);
    for (UINT i = 0; i < r->count; i++) {
        RECT *area = &r->areas[i];
        fx_intersect(area, area, rect);
        if (area->left >= area->right || area->top >= area->bottom)
            return FX_E_INVALIDARG;
        find_base_colors(r, src, area);
        find_center(r, src, area);
        find_references(r, src, area);
        correct_area(r, src, rect, area, dst, dst_stride);
    }
    return FX_OK;
}
