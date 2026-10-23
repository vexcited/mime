/*
 * the icons shell32 draws when the system has no art: a page, a window, a folder, a drive and a few more. each
 * picture is filled polygons in a unit square, rasterized at a multiple of the size and averaged down so edges stay
 * smooth at any size.
 */
#include "shell32_private.h"

/* the compiler wants this symbol in any code using floating point. */
int _fltused = 0x9875;

typedef struct {
    float X, Y;
} PT;

typedef struct {
    int W, H;
    /* canvas size over picture size. */
    int Scale;
    /* premultiplied pixels, a b g r in the low to high bytes of each. */
    DWORD *Pixels;
} CANVAS;

static DWORD premultiply(DWORD argb)
{
    DWORD a = argb >> 24;
    DWORD r = ((argb >> 16) & 0xFF) * a / 255, g = ((argb >> 8) & 0xFF) * a / 255, b = (argb & 0xFF) * a / 255;
    return (a << 24) | (r << 16) | (g << 8) | b;
}

static DWORD mix_color(DWORD from, DWORD to, float t)
{
    if (t < 0)
        t = 0;
    if (t > 1)
        t = 1;
    DWORD result = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        float a = (float)((from >> shift) & 0xFF), b = (float)((to >> shift) & 0xFF);
        result |= (DWORD)(a + (b - a) * t + 0.5f) << shift;
    }
    return result;
}

/* blends a straight color over a premultiplied pixel. */
static void blend_pixel(DWORD *dst, DWORD argb)
{
    DWORD src = premultiply(argb), inverse = 255 - (src >> 24), d = *dst, out = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        DWORD v = ((src >> shift) & 0xFF) + (((d >> shift) & 0xFF) * inverse + 127) / 255;
        out |= (v > 255 ? 255 : v) << shift;
    }
    *dst = out;
}

static void sort_floats(float *v, int n)
{
    for (int i = 1; i < n; i++) {
        float x = v[i];
        int j = i - 1;
        for (; j >= 0 && v[j] > x; j--)
            v[j + 1] = v[j];
        v[j + 1] = x;
    }
}

/* points are in canvas pixels. the color runs from top to bottom between two rows. */
static void fill_polygon(CANVAS *c, const PT *p, int n, DWORD top, DWORD bottom, float row0, float row1)
{
    float low = p[0].Y, high = p[0].Y;
    for (int i = 1; i < n; i++) {
        if (p[i].Y < low)
            low = p[i].Y;
        if (p[i].Y > high)
            high = p[i].Y;
    }
    int y0 = (int)low, y1 = (int)high + 1;
    if (y0 < 0)
        y0 = 0;
    if (y1 > c->H)
        y1 = c->H;
    float crossings[64];
    for (int y = y0; y < y1; y++) {
        float sy = (float)y + 0.5f;
        int count = 0;
        for (int i = 0; i < n && count < 62; i++) {
            PT a = p[i], b = p[(i + 1) % n];
            if ((a.Y <= sy) == (b.Y <= sy))
                continue;
            crossings[count++] = a.X + (sy - a.Y) * (b.X - a.X) / (b.Y - a.Y);
        }
        sort_floats(crossings, count);
        DWORD color = top == bottom ? top : mix_color(top, bottom, row1 > row0 ? (sy - row0) / (row1 - row0) : 0);
        for (int k = 0; k + 1 < count; k += 2) {
            int x0 = (int)(crossings[k] + 0.5f), x1 = (int)(crossings[k + 1] + 0.5f);
            if (x0 < 0)
                x0 = 0;
            if (x1 > c->W)
                x1 = c->W;
            for (int x = x0; x < x1; x++)
                blend_pixel(&c->Pixels[y * c->W + x], color);
        }
    }
}

/* maps the unit square to canvas pixels. */
static PT unit(const CANVAS *c, float x, float y)
{
    return (PT){x * (float)c->W, y * (float)c->H};
}

static void poly(CANVAS *c, const float *xy, int n, DWORD top, DWORD bottom)
{
    PT p[32];
    float low = 1, high = 0;
    for (int i = 0; i < n && i < 32; i++) {
        p[i] = unit(c, xy[i * 2], xy[i * 2 + 1]);
        if (xy[i * 2 + 1] < low)
            low = xy[i * 2 + 1];
        if (xy[i * 2 + 1] > high)
            high = xy[i * 2 + 1];
    }
    fill_polygon(c, p, n, top, bottom, low * (float)c->H, high * (float)c->H);
}

static void box(CANVAS *c, float x0, float y0, float x1, float y1, DWORD top, DWORD bottom)
{
    float xy[8] = {x0, y0, x1, y0, x1, y1, x0, y1};
    poly(c, xy, 4, top, bottom);
}

/* a quarter of a circle in steps of 18 degrees, as cosine and sine. */
static const float g_arc[6][2] = {{1.0f, 0.0f},    {0.9511f, 0.3090f}, {0.8090f, 0.5878f},
                                  {0.5878f, 0.8090f}, {0.3090f, 0.9511f}, {0.0f, 1.0f}};

/* corners go clockwise from the top right. */
static void round_box(CANVAS *c, float x0, float y0, float x1, float y1, float radius, DWORD top, DWORD bottom)
{
    float xy[2 * 4 * 6];
    int n = 0;
    const float mx[4] = {x1 - radius, x1 - radius, x0 + radius, x0 + radius};
    const float my[4] = {y0 + radius, y1 - radius, y1 - radius, y0 + radius};
    for (int corner = 0; corner < 4; corner++)
        for (int i = 0; i < 6; i++) {
            float cs = g_arc[i][0], sn = g_arc[i][1], dx, dy;
            switch (corner) {
            case 0:
                dx = sn;
                dy = -cs;
                break;
            case 1:
                dx = cs;
                dy = sn;
                break;
            case 2:
                dx = -sn;
                dy = cs;
                break;
            default:
                dx = -cs;
                dy = -sn;
                break;
            }
            xy[n * 2] = mx[corner] + dx * radius;
            xy[n * 2 + 1] = my[corner] + dy * radius;
            n++;
        }
    poly(c, xy, n, top, bottom);
}

/* a 32 point polygon. */
static void disc(CANVAS *c, float mx, float my, float rx, float ry, DWORD top, DWORD bottom)
{
    float xy[64];
    /* a regular 32 gon, in radius units. */
    static const float unit_cos[32] = {1.0f,   0.9808f, 0.9239f, 0.8315f, 0.7071f, 0.5556f, 0.3827f, 0.1951f,
                                       0.0f,   -0.1951f, -0.3827f, -0.5556f, -0.7071f, -0.8315f, -0.9239f, -0.9808f,
                                       -1.0f,  -0.9808f, -0.9239f, -0.8315f, -0.7071f, -0.5556f, -0.3827f, -0.1951f,
                                       0.0f,   0.1951f, 0.3827f, 0.5556f, 0.7071f, 0.8315f, 0.9239f, 0.9808f};
    for (int i = 0; i < 32; i++) {
        xy[i * 2] = mx + unit_cos[i] * rx;
        xy[i * 2 + 1] = my + unit_cos[(i + 24) & 31] * ry;
    }
    poly(c, xy, 32, top, bottom);
}

/* the border color first, then the fill inset by the line width. */
static void outlined_box(CANVAS *c, float x0, float y0, float x1, float y1, float line, DWORD border, DWORD top,
                         DWORD bottom)
{
    box(c, x0, y0, x1, y1, border, border);
    box(c, x0 + line, y0 + line, x1 - line, y1 - line, top, bottom);
}

/* ------------------------------------------------------------------ the pictures */

#define LINE 0.025f

static void draw_page(CANVAS *c, BOOL lines)
{
    const float outer[10] = {0.2f, 0.06f, 0.58f, 0.06f, 0.8f, 0.28f, 0.8f, 0.94f, 0.2f, 0.94f};
    const float inner[10] = {0.2f + LINE, 0.06f + LINE, 0.58f, 0.06f + LINE, 0.8f - LINE, 0.28f + LINE * 0.6f,
                             0.8f - LINE, 0.94f - LINE, 0.2f + LINE, 0.94f - LINE};
    poly(c, outer, 5, 0xFF8D97A3, 0xFF8D97A3);
    poly(c, inner, 5, 0xFFFFFFFF, 0xFFE4E9EF);
    const float fold[6] = {0.58f, 0.06f, 0.58f, 0.28f, 0.8f, 0.28f};
    poly(c, fold, 3, 0xFFB9C3CE, 0xFFD9E0E8);
    if (lines)
        for (int i = 0; i < 4; i++)
            box(c, 0.3f, 0.42f + (float)i * 0.1f, i == 3 ? 0.55f : 0.7f, 0.46f + (float)i * 0.1f, 0xFFAEB8C4,
                0xFFAEB8C4);
}

static void draw_window(CANVAS *c, float x0, float y0, float x1, float y1)
{
    float h = y1 - y0, bar = h * 0.22f;
    round_box(c, x0, y0, x1, y1, 0.05f, 0xFF5E6E80, 0xFF5E6E80);
    box(c, x0 + LINE, y0 + bar, x1 - LINE, y1 - LINE, 0xFFFFFFFF, 0xFFEEF2F6);
    round_box(c, x0 + LINE, y0 + LINE, x1 - LINE, y0 + bar, 0.03f, 0xFF5B9BE4, 0xFF2D6DC0);
    box(c, x1 - LINE - 0.07f, y0 + LINE * 1.8f, x1 - LINE * 1.8f, y0 + bar - LINE * 0.8f, 0xFFFFFFFF, 0xFFFFFFFF);
    box(c, x0 + LINE * 3, y0 + bar + LINE * 3, x0 + (x1 - x0) * 0.55f, y0 + bar + LINE * 8, 0xFFB7C3D0, 0xFFB7C3D0);
}

static void draw_folder(CANVAS *c, BOOL open)
{
    const float back[12] = {0.06f, 0.2f, 0.36f, 0.2f, 0.44f, 0.3f, 0.94f, 0.3f, 0.94f, 0.84f, 0.06f, 0.84f};
    poly(c, back, 6, 0xFFD9A62A, 0xFFC9921A);
    if (!open) {
        round_box(c, 0.06f, 0.4f, 0.94f, 0.86f, 0.03f, 0xFFC08F1C, 0xFFC08F1C);
        round_box(c, 0.06f + LINE, 0.4f + LINE, 0.94f - LINE, 0.86f - LINE, 0.02f, 0xFFFFE38D, 0xFFF3BF3F);
    } else {
        const float lid[8] = {0.18f, 0.4f, 0.98f, 0.4f, 0.88f, 0.86f, 0.04f, 0.86f};
        const float lid_in[8] = {0.18f + 0.02f, 0.4f + LINE, 0.98f - 0.02f, 0.4f + LINE, 0.88f - LINE * 0.6f,
                                 0.86f - LINE, 0.04f + LINE, 0.86f - LINE};
        box(c, 0.1f, 0.34f, 0.86f, 0.5f, 0xFFFDF8EA, 0xFFEDE4C8);
        poly(c, lid, 4, 0xFFC08F1C, 0xFFC08F1C);
        poly(c, lid_in, 4, 0xFFFFE38D, 0xFFF3BF3F);
    }
}

/* a drive body with a light and a slot. */
static void draw_drive_body(CANVAS *c, float y, DWORD light)
{
    const float top[8] = {0.06f, y + 0.12f, 0.18f, y, 0.82f, y, 0.94f, y + 0.12f};
    poly(c, top, 4, 0xFFE9EDF1, 0xFFD3D9DF);
    round_box(c, 0.06f, y + 0.12f, 0.94f, y + 0.4f, 0.03f, 0xFF7C8794, 0xFF7C8794);
    round_box(c, 0.06f + LINE * 0.7f, y + 0.12f + LINE * 0.7f, 0.94f - LINE * 0.7f, y + 0.4f - LINE * 0.7f, 0.02f,
              0xFFDDE2E8, 0xFFA9B2BC);
    box(c, 0.14f, y + 0.22f, 0.6f, y + 0.25f, 0xFF6B7581, 0xFF6B7581);
    box(c, 0.74f, y + 0.2f, 0.86f, y + 0.28f, light, light);
}

static void draw_drive(CANVAS *c, int kind)
{
    switch (kind) {
    case 1: /* removable */
        draw_drive_body(c, 0.38f, 0xFF4C9BE8);
        box(c, 0.36f, 0.3f, 0.64f, 0.4f, 0xFFB5BDC6, 0xFF8A94A0);
        break;
    case 2: /* optical */
        disc(c, 0.5f, 0.34f, 0.32f, 0.3f, 0xFFF4F6F8, 0xFFC9D0D8);
        disc(c, 0.5f, 0.34f, 0.07f, 0.07f, 0xFF7C8794, 0xFF7C8794);
        disc(c, 0.5f, 0.34f, 0.03f, 0.03f, 0xFFFFFFFF, 0xFFFFFFFF);
        draw_drive_body(c, 0.5f, 0xFF42B450);
        break;
    case 3: /* network */
        draw_drive_body(c, 0.32f, 0xFF42B450);
        box(c, 0.46f, 0.72f, 0.54f, 0.88f, 0xFF3FA34D, 0xFF2D8A3B);
        box(c, 0.16f, 0.84f, 0.84f, 0.9f, 0xFF3FA34D, 0xFF2D8A3B);
        break;
    case 4: { /* memory */
        for (int i = 0; i < 6; i++) {
            float x = 0.22f + (float)i * 0.1f;
            box(c, x, 0.22f, x + 0.05f, 0.3f, 0xFFC6CDD5, 0xFFC6CDD5);
            box(c, x, 0.7f, x + 0.05f, 0.78f, 0xFFC6CDD5, 0xFFC6CDD5);
        }
        outlined_box(c, 0.16f, 0.3f, 0.84f, 0.7f, LINE * 0.6f, 0xFF27303A, 0xFF566371, 0xFF37424E);
        break;
    }
    default: /* fixed */
        draw_drive_body(c, 0.34f, 0xFF42B450);
        break;
    }
}

static void draw_computer(CANVAS *c)
{
    round_box(c, 0.1f, 0.12f, 0.9f, 0.68f, 0.04f, 0xFF4F5A66, 0xFF4F5A66);
    box(c, 0.1f + LINE * 1.6f, 0.12f + LINE * 1.6f, 0.9f - LINE * 1.6f, 0.68f - LINE * 1.6f, 0xFF7DBBF5, 0xFF2D72C6);
    box(c, 0.43f, 0.68f, 0.57f, 0.78f, 0xFF9BA5B0, 0xFF7C8794);
    round_box(c, 0.28f, 0.78f, 0.72f, 0.86f, 0.03f, 0xFFB5BDC6, 0xFF8A94A0);
}

static void draw_link(CANVAS *c)
{
    round_box(c, 0.04f, 0.54f, 0.5f, 0.96f, 0.06f, 0xFF3B6FB6, 0xFF3B6FB6);
    round_box(c, 0.04f + LINE, 0.54f + LINE, 0.5f - LINE, 0.96f - LINE, 0.04f, 0xFFFFFFFF, 0xFFE8EEF6);
    const float arrow[14] = {0.12f, 0.88f, 0.12f, 0.8f, 0.24f, 0.8f, 0.24f, 0.7f, 0.4f, 0.8f, 0.24f, 0.9f, 0.24f, 0.88f};
    const float shaft[8] = {0.12f, 0.88f, 0.12f, 0.8f, 0.24f, 0.8f, 0.24f, 0.88f};
    poly(c, arrow, 6, 0xFF2F66B5, 0xFF2F66B5);
    poly(c, shaft, 4, 0xFF2F66B5, 0xFF2F66B5);
}

/*
 * premultiplied, freed by the caller with HeapFree. kinds are the ART_ values of shell32_private.h.
 */
DWORD *ShIconArt(int kind, int cx, int cy)
{
    if (cx <= 0 || cy <= 0 || cx > 1024 || cy > 1024)
        return nullptr;
    int scale = cx <= 64 ? 4 : cx <= 128 ? 3 : 2;
    CANVAS canvas = {cx * scale, cy * scale, scale, nullptr};
    canvas.Pixels = ShAllocZero((SIZE_T)canvas.W * (SIZE_T)canvas.H * sizeof(DWORD));
    DWORD *out = ShAlloc((SIZE_T)cx * (SIZE_T)cy * sizeof(DWORD));
    if (!canvas.Pixels || !out) {
        ShFree(canvas.Pixels);
        ShFree(out);
        return nullptr;
    }
    switch (kind) {
    case ART_DOC:
        draw_page(&canvas, TRUE);
        break;
    case ART_DOCASSOC:
        draw_page(&canvas, FALSE);
        draw_window(&canvas, 0.3f, 0.36f, 0.84f, 0.84f);
        break;
    case ART_APP:
        draw_window(&canvas, 0.08f, 0.14f, 0.92f, 0.86f);
        break;
    case ART_FOLDER:
        draw_folder(&canvas, FALSE);
        break;
    case ART_FOLDEROPEN:
        draw_folder(&canvas, TRUE);
        break;
    case ART_DRIVE:
        draw_drive(&canvas, 0);
        break;
    case ART_DRIVE_REMOVABLE:
        draw_drive(&canvas, 1);
        break;
    case ART_DRIVE_OPTICAL:
        draw_drive(&canvas, 2);
        break;
    case ART_DRIVE_NETWORK:
        draw_drive(&canvas, 3);
        break;
    case ART_DRIVE_MEMORY:
        draw_drive(&canvas, 4);
        break;
    case ART_COMPUTER:
        draw_computer(&canvas);
        break;
    case ART_LINK:
        draw_link(&canvas);
        break;
    default:
        draw_page(&canvas, TRUE);
        break;
    }
    /* averages each block of samples. */
    for (int y = 0; y < cy; y++)
        for (int x = 0; x < cx; x++) {
            DWORD sum[4] = {0, 0, 0, 0};
            for (int sy = 0; sy < scale; sy++)
                for (int sx = 0; sx < scale; sx++) {
                    DWORD p = canvas.Pixels[(y * scale + sy) * canvas.W + x * scale + sx];
                    for (int k = 0; k < 4; k++)
                        sum[k] += (p >> (k * 8)) & 0xFF;
                }
            DWORD n = (DWORD)(scale * scale);
            out[y * cx + x] = (sum[0] / n) | ((sum[1] / n) << 8) | ((sum[2] / n) << 16) | ((sum[3] / n) << 24);
        }
    ShFree(canvas.Pixels);
    return out;
}
