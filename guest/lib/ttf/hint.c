/*
 * truetype bytecode interpreter.
 *
 * runs the font program, the control value program and the glyph programs of a face at one pixel size, as defined in
 * the published truetype instruction set. coordinates are 26.6 fixed point pixels, unit vectors are 2.14.
 */
#include "ttf.h"
#include "ttf_internal.h"

#define memset __builtin_memset
#define memcpy __builtin_memcpy

#define MAX_CALL_DEPTH 64
#define MAX_PROGRAM_STEPS 3000000u
#define MAX_GLYPH_STEPS 400000u
#define MAX_COMPONENT_DEPTH 6
#define MAX_POINTS 20000u

enum { ROUND_GRID, ROUND_HALF, ROUND_DOUBLE, ROUND_DOWN, ROUND_UP, ROUND_OFF, ROUND_SUPER, ROUND_SUPER45 };

enum { PROGRAM_FONT, PROGRAM_PREP, PROGRAM_GLYPH };

enum { TAG_ON = 1, TAG_TOUCH_X = 2, TAG_TOUCH_Y = 4 };

typedef struct V2 {
    int32_t x, y;
} V2;

typedef struct Zone {
    V2 *org, *cur, *orus;
    uint8_t *tag;
    const uint16_t *ends; /* contour end points, may be null. */
    uint32_t n, contours, cap;
} Zone;

typedef struct GState {
    uint32_t rp0, rp1, rp2;
    V2 dv, pv, fv;
    int32_t loop;
    int32_t min_dist;
    int round_mode;
    int32_t period, phase, threshold;
    bool auto_flip;
    int32_t cvt_cutin, sw_cutin, sw_value;
    int32_t delta_base, delta_shift;
    uint32_t instruct_control;
    uint32_t scan_control; /* raw SCANCTRL value. */
    uint32_t scan_type;
    int zp0, zp1, zp2;
} GState;

typedef struct Code {
    const uint8_t *data;
    uint32_t size;
} Code;

typedef struct FuncDef {
    Code code; /* the body after the FDEF, up to the ENDF. */
    bool active;
} FuncDef;

typedef struct InstrDef {
    Code code;
    uint8_t opcode;
    bool active;
} InstrDef;

typedef struct Call {
    Code code;
    uint32_t ip;
    uint32_t count;
    const FuncDef *func;
    int patch;        /* PATCH_* of a value held back for the length of the call. */
    int32_t saved;
    bool special;     /* the call runs one of the delta shift functions. */
} Call;

/*
 * Windows spots a few functions of the font programs of its own fonts by their first bytes and treats them on their
 * own while ClearType is on: a call holds back a storage value or the cut in, and the delta shift functions only move
 * along y. these are the flags found.
 */
enum {
    PATCH_STORAGE22 = 0x100, /* functions 64 to 66 that begin with a test of storage 22. */
    PATCH_STORAGE24 = 0x200, /* functions 0 to 2, 4, 7 and 8 that begin with a test of storage 24. */
    PATCH_READ8 = 0x400,     /* function 0 that reads storage 8, which then reads as zero. */
    PATCH_CUTIN = 0x800      /* function 58, called with an endless cut in. */
};

enum { HELD_NONE, HELD_STORAGE22, HELD_STORAGE24, HELD_CUTIN };

/*
 * a font unit to 26.6 conversion as the font driver of Windows does it: a whole number ratio rounded half up when the
 * divisor is a power of two, rounded away from zero when it is not, and a 16.16 factor when the numbers are big.
 */
typedef struct Scale {
    int32_t num, den, fix;
    int shift; /* above zero for a power of two divisor, otherwise minus one for a plain divisor or minus two for 16.16. */
} Scale;

/* what ClearType with compatible widths remembers about each point of a glyph while the program runs. */
typedef struct PhaseLink {
    int16_t first, second; /* the points this one follows, second is minus one when there is one. */
    int16_t partner;       /* the other edge of the stem. */
    uint16_t flags;        /* PHASE_*. */
    int32_t shift;
} PhaseLink;

enum { PHASE_CONFLICT = 1, PHASE_DONE = 2, PHASE_VISITING = 4 };

struct MimeTtfHinter {
    const MimeTtfFont *font;
    MimeAllocator alloc;
    int ppem_x, ppem_y;
    uint32_t flags;
    int32_t scale_x, scale_y; /* 16.16 pixels in 26.6 per font unit. */
    Scale sx, sy, su;         /* x, y and the larger of both. */
    bool stretched;
    bool usable;
    bool glyph_hinting; /* glyph programs run. */
    bool native; /* the prep asked for native cleartype behaviour. */
    MimeTtfHinter *plain; /* the same size without cleartype, whose advance the compatible widths take. */
    uint16_t patches;     /* PATCH_* flags the font program set when it defined its functions. */
    uint16_t special[4];  /* the numbers of the delta shift functions. */
    uint8_t special_count;

    int32_t *cvt;
    uint32_t cvt_count;
    int32_t *storage;
    uint32_t storage_count;
    FuncDef *funcs;
    uint32_t func_count;
    InstrDef *idefs;
    uint32_t idef_count;
    int32_t *stack;
    uint32_t stack_cap;

    Zone twilight;
    GState defaults; /* the graphics state after the prep. */
    uint32_t instruct_control;
    uint32_t scan_control, scan_type;
};

/* ------------------------------------------------------------------ fixed point helpers */


static int32_t sat32(int64_t v)
{
    return v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : (int32_t)v;
}

/* a * b / c rounded to nearest, ties away from zero. */
static int32_t mul_div(int32_t a, int32_t b, int32_t c)
{
    if (c == 0)
        return a < 0 ? INT32_MIN : INT32_MAX;
    int64_t n = (int64_t)a * b;
    int64_t d = c;
    bool neg = false;
    if (n < 0) {
        n = -n;
        neg = !neg;
    }
    if (d < 0) {
        d = -d;
        neg = !neg;
    }
    int64_t q = (n + d / 2) / d;
    return sat32(neg ? -q : q);
}

/* a * b / c rounded down, for the instructions that truncate. */
static int32_t mul_fix(int32_t a, int32_t b)
{
    return mul_div(a, b, 0x10000);
}

/* a product with a 2.14 number, rounded half up. */
static int32_t mul14(int32_t a, int32_t b)
{
    return (int32_t)((((int64_t)a * b >> 13) + 1) >> 1);
}

/* a divided by b rounded away from zero at a half, the way the font driver does it. */
static int32_t comp_div(int32_t b, int64_t a)
{
    if (b == 0)
        return (a < 0) + INT32_MAX;
    int64_t half = b / 2;
    int64_t v = ((b < 0) == (a < 0)) ? a + half : a - half;
    return (int32_t)(v / b);
}

/* two 26.6 numbers multiplied, rounded half up when they are small. */
static int32_t mul_26_6(int32_t a, int32_t b)
{
    if (a <= 46340 && a >= -46340 && b <= 46340 && b >= -46340)
        return (a * b + 32) >> 6;
    bool negative = (a < 0) != (b < 0);
    uint64_t m = (uint64_t)(a < 0 ? -(int64_t)a : a) * (uint64_t)(b < 0 ? -(int64_t)b : b);
    int32_t r = (int32_t)((m + 32) >> 6);
    return negative ? -r : r;
}

/* round to the pixel as ODD and EVEN do, whatever the rounding state is. */
static int32_t round_pixel(int32_t d)
{
    int32_t v = d < 0 ? -((32 - d) & ~63) : ((d + 32) & ~63);
    return ((d ^ v) < 0 && d != 0) ? 0 : v;
}

static int32_t scale_apply(const Scale *s, int32_t v)
{
    if (s->shift >= 0)
        return sat32(((int64_t)(s->den >> 1) + (int64_t)s->num * v) >> s->shift);
    if (s->shift == -1) {
        int64_t n = (int64_t)s->num * v;
        if (v < 0)
            return sat32(-((s->den / 2 - n) / s->den));
        return sat32((n + s->den / 2) / s->den);
    }
    int64_t n = (int64_t)s->fix * v;
    return sat32((n + (n >> 63) + 0x8000) >> 16);
}

static void scale_init(Scale *s, int ppem, int upem)
{
    uint32_t n = (uint32_t)ppem << 16, d = (uint32_t)upem << 16;
    uint32_t both = n | d;
    int zeros = 0;
    while (!(both & 1)) {
        both >>= 1;
        zeros++;
    }
    if (zeros - 1 > 0) {
        n >>= zeros - 1;
        d >>= zeros - 1;
    }
    uint32_t num = n << 6;
    if (n >= 0x2000000) {
        num = n;
        d >>= 6;
    }
    s->fix = (int32_t)((((int64_t)num << 16) + d / 2) / d);
    s->num = (int32_t)num;
    s->den = (int32_t)d;
    if (num > 0x7FFF) {
        s->shift = -2;
    } else if (d && !(d & (d - 1))) {
        s->shift = __builtin_ctz(d);
    } else {
        s->shift = -1;
    }
}

static uint64_t isqrt64(uint64_t v)
{
    uint64_t r = 0, bit = 1ull << 62;
    while (bit > v)
        bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

/* the unit vector in 2.14 along dx and dy, a zero vector gives the x axis. */
static bool normalize(int32_t dx, int32_t dy, V2 *out)
{
    if (dx == 0 && dy == 0) {
        out->x = 0x4000;
        out->y = 0;
        return true;
    }
    if (dy == 0) {
        out->x = dx < 0 ? -0x4000 : 0x4000;
        out->y = 0;
        return true;
    }
    if (dx == 0) {
        out->x = 0;
        out->y = dy < 0 ? -0x4000 : 0x4000;
        return true;
    }
    /* the square sum is lifted by an even power of two so the root keeps thirty bits. */
    uint64_t sq = (uint64_t)((int64_t)dx * dx + (int64_t)dy * dy);
    int bits = 64 - __builtin_clzll(sq), up = (62 - bits) & ~1;
    int64_t len = (int64_t)isqrt64(sq << up);
    if (len == 0)
        return false;
    int64_t one = (int64_t)1 << (up / 2);
    int64_t nx = (int64_t)dx * one * 0x4000, ny = (int64_t)dy * one * 0x4000;
    out->x = sat32((nx + (nx < 0 ? -len / 2 : len / 2)) / len);
    out->y = sat32((ny + (ny < 0 ? -len / 2 : len / 2)) / len);
    return true;
}

static uint32_t be16(const uint8_t *p)
{
    return (uint32_t)p[0] << 8 | p[1];
}

/* ------------------------------------------------------------------ interpreter state */

typedef struct Exec {
    MimeTtfHinter *h;
    GState gs;
    Zone zones[2]; /* 0 twilight, 1 glyph. */
    int32_t *stack;
    uint32_t sp, stack_cap;
    int32_t *cvt, *storage;
    uint32_t cvt_count, storage_count;
    Code code;
    uint32_t ip;
    Call calls[MAX_CALL_DEPTH];
    uint32_t depth;
    uint32_t steps, step_limit;
    int program;
    int32_t fdotp;
    bool iupy; /* the points were interpolated along y. */
    bool iupx;
    PhaseLink *links;      /* null unless the phase control runs. */
    uint8_t *misoriented;  /* per contour, wound against its nesting. */
    int32_t phase_scale;   /* 16.16, the compatible advance over the scaled one. */
    bool phase_done;
    int32_t line_p1, line_p2; /* the points of the last line a vector was set along, minus one after an axis. */
    bool dual_is_project; /* the dual projection vector is the projection vector, set from a line or two numbers. */
    bool read8; /* storage 8 was read as zero by the patch. */
    bool composite; /* the program is the one of a composite glyph. */
    uint32_t special_depth; /* calls into delta shift functions that are running. */
    bool failed;
    int ppem_for_delta;
} Exec;

#define FAIL() do { ex->failed = true; return; } while (0)

static bool pop(Exec *ex, int32_t *v)
{
    if (ex->sp == 0) {
        ex->failed = true;
        return false;
    }
    *v = ex->stack[--ex->sp];
    return true;
}

static void push(Exec *ex, int32_t v)
{
    if (ex->sp >= ex->stack_cap) {
        ex->failed = true;
        return;
    }
    ex->stack[ex->sp++] = v;
}

static Zone *zone_of(Exec *ex, int z)
{
    return &ex->zones[z];
}

static bool point_ok(const Zone *z, int32_t p)
{
    return p >= 0 && (uint32_t)p < z->n;
}

static void compute_fdotp(Exec *ex)
{
    int32_t d = ((ex->gs.pv.y * ex->gs.fv.y + 0x2000) >> 14) + ((ex->gs.pv.x * ex->gs.fv.x + 0x2000) >> 14);
    d = (int16_t)d;
    ex->fdotp = (d >= -1023 && d <= 1023) ? (d < 0 ? -0x4000 : 0x4000) : d;
}

static int32_t project(const Exec *ex, int32_t dx, int32_t dy)
{
    const V2 *p = &ex->gs.pv;
    if (p->x == 0x4000)
        return dx;
    if (p->y == 0x4000)
        return dy;
    return mul14(dy, p->y) + mul14(dx, p->x);
}

static int32_t dual_project(const Exec *ex, int32_t dx, int32_t dy)
{
    const V2 *p = &ex->gs.dv;
    if (p->x == 0x4000)
        return dx;
    if (p->y == 0x4000)
        return dy;
    return mul14(dy, p->y) + mul14(dx, p->x);
}

/* the ratio of the pixel scale along the projection vector to the y scale, in 16.16. */
static int32_t current_ratio(const Exec *ex)
{
    const MimeTtfHinter *h = ex->h;
    if (!h->stretched)
        return 0x10000;
    int32_t rx = mul_div(h->scale_x, 0x10000, h->scale_y);
    if (ex->gs.pv.y == 0x4000)
        return 0x10000;
    if (ex->gs.pv.x == 0x4000)
        return rx;
    int64_t px = (int64_t)ex->gs.pv.x * rx / 0x10000;
    int64_t py = ex->gs.pv.y;
    uint32_t len = isqrt64((uint64_t)(px * px + py * py));
    return (int32_t)((int64_t)len * 0x10000 / 0x4000);
}

static void shift_point(Exec *ex, Zone *z, uint32_t p, int32_t dx, int32_t dy);

/* moves a point along the freedom vector so its projection changes by dist. */
static void move_point(Exec *ex, Zone *z, uint32_t p, int32_t dist)
{
    const V2 *f = &ex->gs.fv;
    int32_t fd = ex->fdotp, dx = 0, dy = 0;
    if (fd == 0x4000) {
        if (f->x)
            dx = mul14(dist, f->x);
        if (f->y)
            dy = mul14(dist, f->y);
    } else {
        if (f->x) {
            if (fd == f->x) {
                dx = dist;
            } else {
                /* the half is added whatever the sign, so negative moves lean towards zero. */
                int32_t v = (int32_t)((uint32_t)dist * (uint32_t)f->x);
                v = fd < 0 ? v - fd / 2 : v + fd / 2;
                dx = fd ? v / fd : (v < 0) + INT32_MAX;
            }
        }
        if (f->y)
            dy = fd == f->y ? dist : comp_div(fd, (int64_t)dist * f->y);
    }
    shift_point(ex, z, p, dx, dy);
}

/* moves a point by a displacement along the freedom vector. */
static void shift_point(Exec *ex, Zone *z, uint32_t p, int32_t dx, int32_t dy)
{
    if (ex->gs.fv.x)
        z->tag[p] |= TAG_TOUCH_X;
    if (ex->gs.fv.y)
        z->tag[p] |= TAG_TOUCH_Y;
    z->cur[p].x = sat32((int64_t)z->cur[p].x + dx);
    z->cur[p].y = sat32((int64_t)z->cur[p].y + dy);
}

/* ------------------------------------------------------------------ rounding */

/* the largest multiple of period not above v. */
static int32_t floor_period(int32_t v, int32_t period)
{
    if (v >= 0)
        return v / period * period;
    return -((-v + period - 1) / period * period);
}

static int32_t round_super(const GState *gs, int32_t d)
{
    if (gs->period <= 0)
        return d;
    if (d >= 0) {
        int32_t v = floor_period(d - gs->phase + gs->threshold, gs->period) + gs->phase;
        return v < 0 ? gs->phase : v;
    }
    int32_t v = -floor_period(-d - gs->phase + gs->threshold, gs->period) - gs->phase;
    return v > 0 ? -gs->phase : v;
}

/*
 * with ClearType the rendering is finer than a pixel along the x axis, so rounding along it keeps a sixteenth of a
 * pixel and distances may shrink to half a pixel.
 */
static bool subpixel_axis(const Exec *ex)
{
    return (ex->h->flags & MIME_TTF_HINT_CLEARTYPE) && !(ex->gs.pv.x == 0 && ex->gs.pv.y == 0x4000);
}

static bool subpixel_rounding(const Exec *ex)
{
    return subpixel_axis(ex) && (ex->h->native || ex->program != PROGRAM_PREP);
}

static int32_t round_value(const Exec *ex, int32_t d)
{
    const GState *gs = &ex->gs;
    if (subpixel_rounding(ex) && gs->round_mode <= ROUND_OFF) {
        int32_t v;
        switch (gs->round_mode) {
        case ROUND_GRID:
            v = d < 0 ? -((-d + 2) & ~3) : ((d + 2) & ~3);
            break;
        case ROUND_HALF:
            v = d < 0 ? -2 - ((-d) & ~3) : ((d & ~3) + 2);
            if ((d ^ v) < 0 && d != 0)
                return d > 0 ? 2 : -2;
            return v;
        case ROUND_DOUBLE:
            v = d < 0 ? -((-d + 1) & ~1) : ((d + 1) & ~1);
            break;
        case ROUND_DOWN:
            /* along a line set by points the font driver rounds down to the pixel. */
            if (!ex->h->native && ex->dual_is_project)
                return d >= 0 ? (d & ~63) : -((-d) & ~63);
            v = d < 0 ? -((-d) & ~3) : (d & ~3);
            break;
        case ROUND_UP:
            v = d < 0 ? -((-d + 3) & ~3) : ((d + 3) & ~3);
            break;
        default:
            return d;
        }
        return ((d ^ v) < 0 && d != 0) ? 0 : v;
    }
    switch (gs->round_mode) {
    case ROUND_GRID:
        if (d >= 0) {
            int32_t v = (d + 32) & ~63;
            return v < 0 ? 0 : v;
        } else {
            int32_t v = -((-d + 32) & ~63);
            return v > 0 ? 0 : v;
        }
    case ROUND_HALF:
        if (d >= 0) {
            int32_t v = (d & ~63) + 32;
            return v < 0 ? 32 : v;
        } else {
            int32_t v = -((-d & ~63) + 32);
            return v > 0 ? -32 : v;
        }
    case ROUND_DOUBLE:
        if (d >= 0) {
            int32_t v = (d + 16) & ~31;
            return v < 0 ? 0 : v;
        } else {
            int32_t v = -((-d + 16) & ~31);
            return v > 0 ? 0 : v;
        }
    case ROUND_DOWN:
        return d >= 0 ? (d & ~63) : -((-d) & ~63);
    case ROUND_UP:
        return d >= 0 ? ((d + 63) & ~63) : -((-d + 63) & ~63);
    case ROUND_OFF:
        return d;
    default:
        return round_super(gs, d);
    }
}

static void set_super_round(Exec *ex, int32_t n, int32_t grid_period)
{
    GState *gs = &ex->gs;
    switch (n & 0xC0) {
    case 0x00: gs->period = grid_period / 2; break;
    case 0x40: gs->period = grid_period; break;
    case 0x80: gs->period = grid_period * 2; break;
    default: gs->period = grid_period; break;
    }
    switch (n & 0x30) {
    case 0x00: gs->phase = 0; break;
    case 0x10: gs->phase = gs->period / 4; break;
    case 0x20: gs->phase = gs->period / 2; break;
    default: gs->phase = gs->period * 3 / 4; break;
    }
    if ((n & 0x0F) == 0)
        gs->threshold = gs->period - 1;
    else
        gs->threshold = ((n & 0x0F) - 4) * gs->period / 8;
    /* the 26.6 values the stack holds are scaled by the 64 of the grid. */
}

/* ------------------------------------------------------------------ instruction lengths */

/* the length in bytes of the instruction at ip, 0 when it runs past the end. */
static uint32_t instr_length(Code c, uint32_t ip)
{
    uint8_t op = c.data[ip];
    uint32_t len = 1;
    if (op == 0x40) {
        if (ip + 1 >= c.size)
            return 0;
        len = 2 + c.data[ip + 1];
    } else if (op == 0x41) {
        if (ip + 1 >= c.size)
            return 0;
        len = 2 + 2u * c.data[ip + 1];
    } else if (op >= 0xB0 && op <= 0xB7) {
        len = 2 + (op - 0xB0);
    } else if (op >= 0xB8 && op <= 0xBF) {
        len = 1 + 2u * (op - 0xB8 + 1);
    }
    return ip + len > c.size ? 0 : len;
}

/* skips forward over nested conditionals, and stops after the ELSE if asked to or else after the EIF. */
static bool skip_if(Exec *ex, bool stop_at_else)
{
    int nest = 1;
    while (ex->ip < ex->code.size) {
        uint32_t len = instr_length(ex->code, ex->ip);
        if (!len)
            return false;
        uint8_t op = ex->code.data[ex->ip];
        ex->ip += len;
        if (op == 0x58) {
            nest++;
        } else if (op == 0x1B) {
            if (nest == 1 && stop_at_else)
                return true;
        } else if (op == 0x59) {
            if (--nest == 0)
                return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------------ individual instructions */

static void ins_getinfo(Exec *ex)
{
    int32_t sel;
    if (!pop(ex, &sel))
        return;
    const MimeTtfHinter *h = ex->h;
    uint32_t fl = h->flags;
    int32_t r = (sel & 1) ? 42 : 0;
    if ((sel & 4) && h->stretched)
        r |= 1 << 9;
    if (sel & 8)
        r |= 1 << 10;
    if (sel & 16)
        r |= 1 << 11;
    if ((sel & 32) && (fl & MIME_TTF_HINT_GRAY))
        r |= 1 << 12;
    if ((sel & 64) && (fl & MIME_TTF_HINT_CLEARTYPE))
        r |= 1 << 13;
    if ((sel & 128) && (fl & MIME_TTF_HINT_CLEARTYPE) && (fl & MIME_TTF_HINT_COMPAT_WIDTHS))
        r |= 1 << 14;
    if ((sel & 1024) && (fl & MIME_TTF_HINT_SUBPIXEL))
        r |= 1 << 17;
    if ((sel & 2048) && (fl & MIME_TTF_HINT_SYMMETRIC))
        r |= 1 << 18;
    if ((sel & 4096) && (fl & MIME_TTF_HINT_CLEARTYPE) && (fl & MIME_TTF_HINT_GRAY_CLEARTYPE))
        r |= 1 << 19;
    push(ex, r);
}

static bool set_vector_from_points(V2 *v, int32_t p1, int32_t p2, bool perp, bool original, Zone *za, Zone *zb)
{
    /* p1 in the zone za, p2 in the zone zb, the vector runs from p1 to p2... */
    if (!point_ok(za, p1) || !point_ok(zb, p2))
        return false;
    const V2 *a = original ? &za->org[p1] : &za->cur[p1];
    const V2 *b = original ? &zb->org[p2] : &zb->cur[p2];
    int32_t dx = b->x - a->x, dy = b->y - a->y;
    V2 n;
    if (!normalize(dx, dy, &n))
        return false;
    if (perp) {
        int32_t t = n.x;
        n.x = -n.y;
        n.y = t;
    }
    *v = n;
    return true;
}

static void ins_mdap(Exec *ex, uint8_t op)
{
    int32_t p;
    if (!pop(ex, &p))
        return;
    Zone *z = zone_of(ex, ex->gs.zp0);
    if (!point_ok(z, p))
        FAIL();
    int32_t dist = 0;
    if (op & 1) {
        int32_t cur = project(ex, z->cur[p].x, z->cur[p].y);
        dist = round_value(ex, cur) - cur;
    }
    move_point(ex, z, (uint32_t)p, dist);
    ex->gs.rp0 = ex->gs.rp1 = (uint32_t)p;
}

static int32_t read_cvt(const Exec *ex, int32_t i)
{
    const MimeTtfHinter *h = ex->h;
    if (i < 0 || (uint32_t)i >= ex->cvt_count)
        return 0;
    if (!h->stretched)
        return ex->cvt[i];
    return mul_fix(ex->cvt[i], current_ratio(ex));
}

static void ins_miap(Exec *ex, uint8_t op)
{
    int32_t n, p;
    if (!pop(ex, &n) || !pop(ex, &p))
        return;
    Zone *z = zone_of(ex, ex->gs.zp0);
    if (!point_ok(z, p) || n < 0 || (uint32_t)n >= ex->cvt_count)
        FAIL();
    int32_t dist = read_cvt(ex, n);
    if (ex->gs.zp0 == 0) {
        z->org[p].x = mul14(dist, ex->gs.pv.x);
        z->org[p].y = mul14(dist, ex->gs.pv.y);
        z->cur[p] = z->org[p];
    }
    int32_t org_dist = project(ex, z->cur[p].x, z->cur[p].y);
    if (op & 1) {
        int32_t diff = dist - org_dist;
        if (diff < 0)
            diff = -diff;
        if (diff > ex->gs.cvt_cutin)
            dist = org_dist;
        dist = round_value(ex, dist);
    }
    move_point(ex, z, (uint32_t)p, dist - org_dist);
    ex->gs.rp0 = ex->gs.rp1 = (uint32_t)p;
}

static void *halloc(const MimeTtfHinter *h, size_t n);

/* ------------------------------------------------------------------ phase control */

/*
 * with cleartype and compatible widths the glyph is first fitted without cleartype to find the advance the program
 * leaves, and fitted again with the stems free along x. every point then remembers the points it was placed from
 * and, once the program is done, is shifted by the stretch that takes the scaled advance to the compatible one.
 */

static bool phase_axis(const Exec *ex)
{
    return ex->links && subpixel_axis(ex);
}

static int contour_of(const Zone *z, int32_t p)
{
    for (uint32_t c = 0; c < z->contours; c++)
        if (p <= z->ends[c])
            return (int)c;
    return -1;
}

static bool depends_on(const Exec *ex, int32_t a, int32_t b, int depth)
{
    int d = depth - 1;
    if (d < 0)
        return true;
    const PhaseLink *l = &ex->links[a];
    if (l->first == -1)
        return false;
    if (l->second == -1)
        return l->first == b || depends_on(ex, l->first, b, d - 1);
    if (l->first == b || l->second == b)
        return true;
    return depends_on(ex, l->first, b, d - 1) || depends_on(ex, l->second, b, d - 1);
}

static bool phase_points_ok(const Zone *z, int32_t a, int32_t b)
{
    return a >= 0 && b >= 0 && (uint32_t)a < z->n && (uint32_t)b < z->n && a != b;
}

/* q follows p. */
static void phase_distance(Exec *ex, const Zone *z, int32_t p, int32_t q, int color)
{
    if (!phase_points_ok(z, p, q))
        return;
    PhaseLink *l = ex->links;
    int32_t root = p;
    if (depends_on(ex, p, q, 100)) {
        l[q].flags |= PHASE_CONFLICT;
    } else if (l[q].first == -1) {
        for (int32_t i = l[p].first; i != -1; i = l[i].first) {
            if (z->orus[root].x != z->orus[i].x)
                break;
            root = i;
        }
        l[q].first = (int16_t)root;
        l[q].second = -1;
    }
    if (color == 1 && l[root].partner == -1 && l[q].partner != root) {
        int32_t t = l[root].first;
        if (t != -1 && l[t].partner == root)
            l[q].flags |= PHASE_CONFLICT;
        else
            l[root].partner = (int16_t)q;
    }
}

/* q lies between p1 and p2. */
static void phase_proportion(Exec *ex, const Zone *z, int32_t p1, int32_t q, int32_t p2)
{
    if (!phase_points_ok(z, p1, q) || !phase_points_ok(z, p2, q) || p1 == p2)
        return;
    PhaseLink *l = ex->links;
    if (depends_on(ex, p1, q, 100) || depends_on(ex, p2, q, 100)) {
        l[q].flags |= PHASE_CONFLICT;
    } else if (l[q].first == -1 && l[q].second == -1) {
        l[q].first = (int16_t)p1;
        l[q].second = (int16_t)p2;
    }
}

static bool inter_align(const Zone *z, int32_t a, int32_t q, int32_t b)
{
    int32_t lo = z->orus[a].x, hi = z->orus[b].x;
    if (lo > hi) {
        int32_t t = lo;
        lo = hi;
        hi = t;
    }
    return lo <= z->orus[q].x && z->orus[q].x <= hi;
}

/* the color of the edge between two neighbours of a contour: 1 or 2 by the side of the stem, 0 when they disagree. */
static int link_color(const Exec *ex, const Zone *z, int32_t a, int32_t b, int fallback)
{
    int ca = contour_of(z, a), cb = contour_of(z, b);
    if (ca < 0 || cb < 0)
        return 0;
    if (ca != cb)
        return fallback;
    int32_t first = ca == 0 ? 0 : z->ends[ca - 1] + 1, last = z->ends[ca];
    int32_t next_a = a != last ? a + 1 : first, prev_a = a != first ? a - 1 : last;
    if (b != next_a && b != prev_a)
        return fallback;
    const V2 *o = z->orus;
    bool turn_a = (int32_t)((int64_t)(o[next_a].y - o[a].y) * (o[a].x - o[prev_a].x)) <
                  (int32_t)((int64_t)(o[next_a].x - o[a].x) * (o[a].y - o[prev_a].y));
    int32_t prev_b = b != first ? b - 1 : last, next_b = b != last ? b + 1 : first;
    bool turn_b = (int32_t)((int64_t)(o[next_b].y - o[b].y) * (o[b].x - o[prev_b].x)) <
                  (int32_t)((int64_t)(o[next_b].x - o[b].x) * (o[b].y - o[prev_b].y));
    if (turn_a != turn_b)
        return 0;
    int32_t dx = o[b].x - o[a].x, dy = o[b].y - o[a].y;
    if (dx < 0)
        dx = -dx;
    if (dy < 0)
        dy = -dy;
    if (2 * (int64_t)dx < dy)
        return 0;
    return ((turn_a ^ ((ex->misoriented[ca] & 1) == 0)) ? 1 : 0) + 1;
}

typedef struct FlatPoint {
    double x, y;
} FlatPoint;

static void flat_curve(FlatPoint *out, uint32_t *n, FlatPoint p0, FlatPoint c, FlatPoint p1)
{
    for (int t = 1; t <= 6; t++) {
        double u = t / 6.0, v = 1 - u;
        out[(*n)++] = (FlatPoint){v * v * p0.x + 2 * u * v * c.x + u * u * p1.x, v * v * p0.y + 2 * u * v * c.y + u * u * p1.y};
    }
}

/* the outline of a contour as a polygon, curves cut in six. */
static uint32_t flatten_contour(const Zone *z, uint32_t first, uint32_t last, FlatPoint *out)
{
    uint32_t count = last - first + 1, n = 0, start = first;
    bool have_on = false;
    for (uint32_t i = first; i <= last; i++) {
        if (z->tag[i] & TAG_ON) {
            start = i;
            have_on = true;
            break;
        }
    }
    FlatPoint begin;
    uint32_t skip;
    if (have_on) {
        begin = (FlatPoint){z->orus[start].x, z->orus[start].y};
        skip = 1;
    } else {
        begin = (FlatPoint){((double)z->orus[first].x + z->orus[last].x) / 2, ((double)z->orus[first].y + z->orus[last].y) / 2};
        start = last;
        skip = 0;
    }
    out[n++] = begin;
    FlatPoint cur = begin, ctrl = {0, 0};
    bool pending = false;
    for (uint32_t k = skip; k <= count; k++) {
        FlatPoint pt = begin;
        bool on = true;
        if (k != count) {
            uint32_t idx = first + (start - first + k) % count;
            pt = (FlatPoint){z->orus[idx].x, z->orus[idx].y};
            on = (z->tag[idx] & TAG_ON) != 0;
        }
        if (on) {
            if (pending)
                flat_curve(out, &n, cur, ctrl, pt);
            else
                out[n++] = pt;
            pending = false;
            cur = pt;
        } else {
            if (pending) {
                FlatPoint mid = {(ctrl.x + pt.x) / 2, (ctrl.y + pt.y) / 2};
                flat_curve(out, &n, cur, ctrl, mid);
                cur = mid;
            }
            ctrl = pt;
            pending = true;
        }
    }
    return n;
}

/* a contour is misoriented when it is wound against its nesting, outer contours clockwise and holes the other way. */
static void find_misoriented(const MimeTtfHinter *h, const Zone *z, uint8_t *out)
{
    uint32_t points = z->n >= 4 ? z->n - 4 : 0;
    FlatPoint *flat = halloc(h, sizeof(FlatPoint) * (7 * (size_t)points + 8 * (size_t)z->contours + 16));
    FlatPoint **starts = halloc(h, sizeof(FlatPoint *) * (z->contours + 1));
    uint32_t *counts = halloc(h, sizeof(uint32_t) * (z->contours + 1));
    memset(out, 0, z->contours);
    if (!flat || !starts || !counts)
        goto done;
    uint32_t used = 0, first = 0;
    for (uint32_t c = 0; c < z->contours; c++) {
        uint32_t last = z->ends[c];
        starts[c] = flat + used;
        counts[c] = 0;
        if (last >= first && last + 1 - first > 2)
            counts[c] = flatten_contour(z, first, last, flat + used);
        used += counts[c];
        first = last + 1u;
    }
    for (uint32_t c = 0; c < z->contours; c++) {
        if (counts[c] < 3)
            continue;
        const FlatPoint *f = starts[c];
        double area = 0;
        uint32_t left = 0;
        for (uint32_t i = 0; i + 1 < counts[c]; i++) {
            area += f[i].x * f[i + 1].y - f[i + 1].x * f[i].y;
            if (f[i].x < f[left].x || (f[i].x == f[left].x && f[i].y < f[left].y))
                left = i;
        }
        double px = f[left].x, py = f[left].y;
        uint32_t crossings = 0;
        for (uint32_t k = 0; k < z->contours; k++) {
            if (k == c)
                continue;
            const FlatPoint *g = starts[k];
            for (uint32_t i = 0; i + 1 < counts[k]; i++) {
                double y0 = g[i].y, y1 = g[i + 1].y;
                if ((y0 <= py) == (y1 <= py))
                    continue;
                double x = g[i].x + (py - y0) * (g[i + 1].x - g[i].x) / (y1 - y0);
                if (x < px)
                    crossings++;
            }
        }
        out[c] = ((crossings & 1) == 0) == (area > 0);
    }
done:
    mime_free(&h->alloc, flat);
    mime_free(&h->alloc, starts);
    mime_free(&h->alloc, counts);
}

static int32_t round_scale(int64_t v)
{
    v += v >= 0 ? 0x10000 : -0x10000;
    return (int32_t)(v / 0x20000);
}

/* the amount a point moves, found from the points it follows. */
static int32_t phase_shift(Exec *ex, Zone *z, bool conflict, int32_t idx)
{
    if (idx == -1)
        return 0;
    PhaseLink *l = ex->links;
    PhaseLink *r = &l[idx];
    if (r->flags & PHASE_VISITING)
        return 0;
    r->flags |= PHASE_VISITING;
    if (!(r->flags & PHASE_DONE)) {
        int64_t k = (int64_t)ex->phase_scale - 0x10000;
        int32_t s = 0;
        int32_t last = z->contours ? (int32_t)z->ends[z->contours - 1] : -1;
        bool scale_own = true;
        if (last >= idx || idx > last + 2) {
            if (r->first != -1) {
                scale_own = false;
                if (r->second == -1) {
                    s = phase_shift(ex, z, conflict, r->first);
                } else {
                    int32_t s1 = phase_shift(ex, z, conflict, r->second);
                    int32_t s0 = phase_shift(ex, z, conflict, r->first);
                    if (s0 == s1) {
                        s = s0;
                    } else {
                        int32_t p0 = z->org[r->first].x, p1 = z->org[r->second].x, pc = z->org[idx].x;
                        if (p0 >= p1) {
                            int32_t t = p0;
                            p0 = p1;
                            p1 = t;
                            t = s0;
                            s0 = s1;
                            s1 = t;
                        }
                        if (p0 == p1)
                            s = (s0 + s1) / 2;
                        else
                            s = (int32_t)(((int64_t)s1 * (pc - p0) + (int64_t)s0 * (p1 - pc)) / (p1 - p0));
                    }
                }
            } else if (!conflict) {
                scale_own = false;
            }
        }
        if (scale_own)
            s = round_scale(k * (int64_t)(2 * z->cur[idx].x));
        if (!(r->flags & PHASE_DONE)) {
            int32_t partner = r->partner;
            if (partner != -1 && !(l[partner].flags & PHASE_DONE)) {
                if (r->first == -1 || r->second == -1)
                    s = round_scale(k * (int64_t)(int32_t)(z->cur[idx].x + z->cur[partner].x));
                z->cur[idx].x += s;
                z->cur[partner].x += s;
                l[partner].flags |= PHASE_DONE;
                l[partner].shift = s;
            } else {
                if (conflict && r->second == -1 && !(r->first != -1 && z->org[idx].x == z->org[r->first].x))
                    s = round_scale(k * (int64_t)(2 * z->cur[idx].x));
                if (r->first != -1)
                    z->cur[idx].x += s;
            }
            r->flags |= PHASE_DONE;
            r->shift = s;
        }
    }
    r->flags &= (uint16_t)~PHASE_VISITING;
    return r->shift;
}

static void phase_run(Exec *ex)
{
    if (!ex->links || ex->phase_done)
        return;
    Zone *z = &ex->zones[1];
    bool conflict = false;
    for (uint32_t i = 0; i < z->n; i++) {
        if (ex->links[i].flags & PHASE_CONFLICT) {
            conflict = true;
            break;
        }
    }
    for (uint32_t i = 0; i < z->n; i++)
        phase_shift(ex, z, conflict, (int32_t)i);
    ex->phase_done = true;
}

/*
 * the distance between two points of the original outline. outside the twilight zone the font unit distance is
 * projected and scaled once, which is not the same as the difference of two points scaled one by one.
 */
static int32_t original_distance(Exec *ex, const Zone *z0, uint32_t p0, const Zone *z1, uint32_t p1)
{
    if (z0 == &ex->zones[0] || z1 == &ex->zones[0])
        return dual_project(ex, z1->org[p1].x - z0->org[p0].x, z1->org[p1].y - z0->org[p0].y);
    int32_t dx = z1->orus[p1].x - z0->orus[p0].x, dy = z1->orus[p1].y - z0->orus[p0].y;
    const MimeTtfHinter *h = ex->h;
    if (!h->stretched)
        return scale_apply(&h->su, dual_project(ex, dx, dy));
    return dual_project(ex, scale_apply(&h->sx, dx), scale_apply(&h->sy, dy));
}

/* the leader of a point moved against a reference point in the other zone than the twilight one. */
static void phase_follow(Exec *ex, const Zone *z, int32_t rp, int32_t p)
{
    if (ex->line_p1 < 0 || ex->line_p2 < 0 || !inter_align(z, ex->line_p1, p, ex->line_p2))
        phase_distance(ex, z, rp, p, 3);
    else
        phase_proportion(ex, z, ex->line_p1, p, ex->line_p2);
}

static void ins_mdrp(Exec *ex, uint8_t op)
{
    int32_t p;
    if (!pop(ex, &p))
        return;
    Zone *z0 = zone_of(ex, ex->gs.zp0), *z1 = zone_of(ex, ex->gs.zp1);
    if (!point_ok(z1, p) || !point_ok(z0, (int32_t)ex->gs.rp0))
        FAIL();
    uint32_t rp0 = ex->gs.rp0;
    if (phase_axis(ex) && ex->gs.zp1 != 0)
        phase_follow(ex, z1, (int32_t)rp0, p);
    int32_t org_dist = original_distance(ex, z0, rp0, z1, (uint32_t)p);
    /* single width cut in. */
    int32_t diff = org_dist - ex->gs.sw_value;
    if (diff < 0)
        diff = -diff;
    if (diff < ex->gs.sw_cutin)
        org_dist = org_dist >= 0 ? ex->gs.sw_value : -ex->gs.sw_value;
    int32_t dist = org_dist;
    if (op & 4)
        dist = round_value(ex, org_dist);
    if (op & 8) {
        int32_t least = subpixel_axis(ex) ? ex->gs.min_dist / 2 : ex->gs.min_dist;
        if (org_dist >= 0) {
            if (dist < least)
                dist = least;
        } else if (dist > -least) {
            dist = -least;
        }
    }
    int32_t cur_dist = project(ex, z1->cur[p].x - z0->cur[rp0].x, z1->cur[p].y - z0->cur[rp0].y);
    move_point(ex, z1, (uint32_t)p, dist - cur_dist);
    ex->gs.rp1 = ex->gs.rp0;
    ex->gs.rp2 = (uint32_t)p;
    if (op & 16)
        ex->gs.rp0 = (uint32_t)p;
}

static void ins_mirp(Exec *ex, uint8_t op)
{
    int32_t p, n;
    if (!pop(ex, &n) || !pop(ex, &p))
        return;
    Zone *z0 = zone_of(ex, ex->gs.zp0), *z1 = zone_of(ex, ex->gs.zp1);
    uint32_t rp0 = ex->gs.rp0;
    if (!point_ok(z1, p) || !point_ok(z0, (int32_t)rp0) || n < -1 || (n >= 0 && (uint32_t)n >= ex->cvt_count))
        FAIL();
    int32_t cvt_dist = n < 0 ? 0 : read_cvt(ex, n);
    int32_t diff = cvt_dist - ex->gs.sw_value;
    if (diff < 0)
        diff = -diff;
    if (diff < ex->gs.sw_cutin)
        cvt_dist = cvt_dist >= 0 ? ex->gs.sw_value : -ex->gs.sw_value;
    if (ex->gs.zp1 == 0) {
        z1->org[p].x = z0->org[rp0].x + mul14(cvt_dist, ex->gs.pv.x);
        z1->org[p].y = z0->org[rp0].y + mul14(cvt_dist, ex->gs.pv.y);
        z1->cur[p] = z1->org[p];
    }
    if (phase_axis(ex) && ex->gs.zp1 != 0)
        phase_distance(ex, z1, (int32_t)rp0, p, link_color(ex, z1, (int32_t)rp0, p, op & 3));
    int32_t org_dist = dual_project(ex, z1->org[p].x - z0->org[rp0].x, z1->org[p].y - z0->org[rp0].y);
    int32_t cur_dist = project(ex, z1->cur[p].x - z0->cur[rp0].x, z1->cur[p].y - z0->cur[rp0].y);
    if (ex->gs.auto_flip && ((org_dist ^ cvt_dist) < 0))
        cvt_dist = -cvt_dist;
    int32_t dist;
    bool sub = subpixel_axis(ex);
    /* along the subpixel axis the cut in is a sixteenth of the usual unless the font asked for native behaviour. */
    if (sub && !ex->h->native) {
        int32_t d = 16 * (cvt_dist - org_dist);
        if (d > ex->gs.cvt_cutin || d < -ex->gs.cvt_cutin)
            cvt_dist = org_dist;
    }
    if (op & 4) {
        if (!sub || ex->h->native) {
            int32_t d = cvt_dist - org_dist;
            if (d < 0)
                d = -d;
            if (d > ex->gs.cvt_cutin)
                cvt_dist = org_dist;
        }
        dist = round_value(ex, cvt_dist);
    } else {
        dist = cvt_dist;
    }
    if (op & 8) {
        int32_t least = sub ? ex->gs.min_dist / 2 : ex->gs.min_dist;
        if (org_dist >= 0) {
            if (dist < least)
                dist = least;
        } else if (dist > -least) {
            dist = -least;
        }
    }
    move_point(ex, z1, (uint32_t)p, dist - cur_dist);
    ex->gs.rp1 = ex->gs.rp0;
    ex->gs.rp2 = (uint32_t)p;
    if (op & 16)
        ex->gs.rp0 = (uint32_t)p;
}

static void ins_msirp(Exec *ex, uint8_t op)
{
    int32_t dist, p;
    if (!pop(ex, &dist) || !pop(ex, &p))
        return;
    Zone *z0 = zone_of(ex, ex->gs.zp0), *z1 = zone_of(ex, ex->gs.zp1);
    uint32_t rp0 = ex->gs.rp0;
    if (!point_ok(z1, p) || !point_ok(z0, (int32_t)rp0))
        FAIL();
    if (ex->gs.zp1 == 0) {
        z1->org[p] = z0->org[rp0];
        z1->cur[p] = z1->org[p];
    }
    if (phase_axis(ex) && ex->gs.zp1 != 0)
        phase_distance(ex, z1, (int32_t)rp0, p, link_color(ex, z1, (int32_t)rp0, p, 1));
    int32_t cur = project(ex, z1->cur[p].x - z0->cur[rp0].x, z1->cur[p].y - z0->cur[rp0].y);
    move_point(ex, z1, (uint32_t)p, dist - cur);
    ex->gs.rp1 = ex->gs.rp0;
    ex->gs.rp2 = (uint32_t)p;
    if (op & 1)
        ex->gs.rp0 = (uint32_t)p;
}

/* the displacement of a reference point from its original position, as a vector along the freedom vector. */
static bool displacement(Exec *ex, bool use_rp1, int32_t *dx, int32_t *dy)
{
    Zone *z;
    uint32_t rp;
    if (use_rp1) {
        z = zone_of(ex, ex->gs.zp0);
        rp = ex->gs.rp1;
    } else {
        z = zone_of(ex, ex->gs.zp1);
        rp = ex->gs.rp2;
    }
    if (!point_ok(z, (int32_t)rp))
        return false;
    int32_t d = project(ex, z->cur[rp].x - z->org[rp].x, z->cur[rp].y - z->org[rp].y);
    *dx = *dy = 0;
    if (ex->fdotp == 0x4000) {
        if (ex->gs.fv.x)
            *dx = mul14(d, ex->gs.fv.x);
        if (ex->gs.fv.y)
            *dy = mul14(d, ex->gs.fv.y);
    } else {
        if (ex->gs.fv.x)
            *dx = comp_div(ex->fdotp, (int64_t)d * ex->gs.fv.x);
        if (ex->gs.fv.y)
            *dy = comp_div(ex->fdotp, (int64_t)d * ex->gs.fv.y);
    }
    return true;
}

static void ins_shp(Exec *ex, uint8_t op)
{
    int32_t dx, dy;
    if (!displacement(ex, (op & 1) != 0, &dx, &dy)) {
        ex->failed = true;
        return;
    }
    Zone *z = zone_of(ex, ex->gs.zp2);
    while (ex->gs.loop > 0) {
        int32_t p;
        if (!pop(ex, &p))
            return;
        if (!point_ok(z, p))
            FAIL();
        if (phase_axis(ex) && ex->gs.zp2 != 0)
            phase_distance(ex, z, (int32_t)((op & 1) ? ex->gs.rp1 : ex->gs.rp2), p, 3);
        shift_point(ex, z, (uint32_t)p, dx, dy);
        ex->gs.loop--;
    }
    ex->gs.loop = 1;
}

static void ins_shc(Exec *ex, uint8_t op)
{
    int32_t c;
    if (!pop(ex, &c))
        return;
    int32_t dx, dy;
    if (!displacement(ex, (op & 1) != 0, &dx, &dy)) {
        ex->failed = true;
        return;
    }
    Zone *z = zone_of(ex, ex->gs.zp2);
    if (ex->gs.zp2 == 0 || z->ends == nullptr) {
        if (c != 0 && ex->gs.zp2 != 0)
            FAIL();
    }
    uint32_t first, last;
    if (ex->gs.zp2 == 0) {
        first = 0;
        last = z->n;
    } else {
        if (c < 0 || (uint32_t)c >= z->contours)
            FAIL();
        first = c == 0 ? 0 : (uint32_t)z->ends[c - 1] + 1u;
        last = (uint32_t)z->ends[c] + 1u;
    }
    Zone *refz = (op & 1) ? zone_of(ex, ex->gs.zp0) : zone_of(ex, ex->gs.zp1);
    uint32_t rp = (op & 1) ? ex->gs.rp1 : ex->gs.rp2;
    /* the phase pass runs first and the contour follows the reference point's own shift. */
    if (ex->links && !ex->phase_done && refz != &ex->zones[0]) {
        phase_run(ex);
        if (rp < z->n)
            dx += ex->links[rp].shift;
    }
    for (uint32_t p = first; p < last && p < z->n; p++)
        if (refz != z || p != rp)
            shift_point(ex, z, p, dx, dy);
}

static void ins_shz(Exec *ex, uint8_t op)
{
    int32_t e;
    if (!pop(ex, &e))
        return;
    if (e < 0 || e > 1)
        FAIL();
    int32_t dx, dy;
    if (!displacement(ex, (op & 1) != 0, &dx, &dy)) {
        ex->failed = true;
        return;
    }
    Zone *z = zone_of(ex, e);
    uint32_t limit = z->n;
    if (e == 1 && z->contours)
        limit = (uint32_t)z->ends[z->contours - 1] + 1u;
    Zone *refz = (op & 1) ? zone_of(ex, ex->gs.zp0) : zone_of(ex, ex->gs.zp1);
    uint32_t rp = (op & 1) ? ex->gs.rp1 : ex->gs.rp2;
    for (uint32_t p = 0; p < limit && p < z->n; p++) {
        if (refz == z && p == rp)
            continue;
        z->cur[p].x = sat32((int64_t)z->cur[p].x + dx);
        z->cur[p].y = sat32((int64_t)z->cur[p].y + dy);
    }
}

static void ins_shpix(Exec *ex)
{
    int32_t amount;
    if (!pop(ex, &amount))
        return;
    int32_t dx = ex->gs.fv.x ? mul14(amount, ex->gs.fv.x) : 0, dy = ex->gs.fv.y ? mul14(amount, ex->gs.fv.y) : 0;
    Zone *z = zone_of(ex, ex->gs.zp2);
    while (ex->gs.loop > 0) {
        int32_t p;
        if (!pop(ex, &p))
            return;
        if (!point_ok(z, p))
            FAIL();
        /* the delta shift functions only move along y, and only points touched along it that were not interpolated. */
        if (ex->special_depth) {
            bool allowed = ex->gs.pv.y == 0x4000 && ex->gs.pv.x == 0 &&
                           (ex->composite || (!ex->iupy && (z->tag[p] & TAG_TOUCH_Y)));
            if (!allowed) {
                ex->gs.loop--;
                continue;
            }
        }
        shift_point(ex, z, (uint32_t)p, dx, dy);
        ex->gs.loop--;
    }
    ex->gs.loop = 1;
}

static void ins_ip(Exec *ex)
{
    Zone *z0 = zone_of(ex, ex->gs.zp0), *z1 = zone_of(ex, ex->gs.zp1), *z2 = zone_of(ex, ex->gs.zp2);
    uint32_t rp1 = ex->gs.rp1, rp2 = ex->gs.rp2;
    if (!point_ok(z0, (int32_t)rp1) || !point_ok(z1, (int32_t)rp2))
        FAIL();
    bool twilight = ex->gs.zp0 == 0 || ex->gs.zp1 == 0 || ex->gs.zp2 == 0;
    const V2 *base;
    int32_t old_range;
    if (twilight) {
        base = &z0->org[rp1];
        old_range = dual_project(ex, z1->org[rp2].x - base->x, z1->org[rp2].y - base->y);
    } else {
        base = &z0->orus[rp1];
        old_range = dual_project(ex, z1->orus[rp2].x - base->x, z1->orus[rp2].y - base->y);
    }
    int32_t cur_range = project(ex, z1->cur[rp2].x - z0->cur[rp1].x, z1->cur[rp2].y - z0->cur[rp1].y);
    while (ex->gs.loop > 0) {
        int32_t p;
        if (!pop(ex, &p))
            return;
        if (!point_ok(z2, p))
            FAIL();
        if (phase_axis(ex) && ex->gs.zp2 != 0)
            phase_proportion(ex, z2, (int32_t)rp1, p, (int32_t)rp2);
        int32_t org_dist = twilight ? dual_project(ex, z2->org[p].x - base->x, z2->org[p].y - base->y)
                                    : dual_project(ex, z2->orus[p].x - base->x, z2->orus[p].y - base->y);
        int32_t new_dist;
        if (old_range != 0)
            new_dist = mul_div(org_dist, cur_range, old_range);
        else
            new_dist = org_dist;
        int32_t cur_dist = project(ex, z2->cur[p].x - z0->cur[rp1].x, z2->cur[p].y - z0->cur[rp1].y);
        move_point(ex, z2, (uint32_t)p, new_dist - cur_dist);
        ex->gs.loop--;
    }
    ex->gs.loop = 1;
}

static void ins_alignrp(Exec *ex)
{
    Zone *z0 = zone_of(ex, ex->gs.zp0), *z1 = zone_of(ex, ex->gs.zp1);
    if (!point_ok(z0, (int32_t)ex->gs.rp0))
        FAIL();
    while (ex->gs.loop > 0) {
        int32_t p;
        if (!pop(ex, &p))
            return;
        if (!point_ok(z1, p))
            FAIL();
        if (phase_axis(ex) && ex->gs.zp1 != 0)
            phase_follow(ex, z1, (int32_t)ex->gs.rp0, p);
        int32_t d = project(ex, z1->cur[p].x - z0->cur[ex->gs.rp0].x, z1->cur[p].y - z0->cur[ex->gs.rp0].y);
        move_point(ex, z1, (uint32_t)p, -d);
        ex->gs.loop--;
    }
    ex->gs.loop = 1;
}

static void ins_alignpts(Exec *ex)
{
    int32_t p2, p1;
    if (!pop(ex, &p2) || !pop(ex, &p1))
        return;
    Zone *z0 = zone_of(ex, ex->gs.zp0), *z1 = zone_of(ex, ex->gs.zp1);
    if (!point_ok(z1, p1) || !point_ok(z0, p2))
        FAIL();
    int32_t d = project(ex, z0->cur[p2].x - z1->cur[p1].x, z0->cur[p2].y - z1->cur[p1].y) / 2;
    move_point(ex, z1, (uint32_t)p1, d);
    move_point(ex, z0, (uint32_t)p2, -d);
}

static void ins_isect(Exec *ex)
{
    int32_t b1, b0, a1, a0, p;
    if (!pop(ex, &b1) || !pop(ex, &b0) || !pop(ex, &a1) || !pop(ex, &a0) || !pop(ex, &p))
        return;
    /* the line a lies in zp1 and the line b in zp0. */
    Zone *z0 = zone_of(ex, ex->gs.zp1), *z1 = zone_of(ex, ex->gs.zp0), *z2 = zone_of(ex, ex->gs.zp2);
    if (!point_ok(z0, a0) || !point_ok(z0, a1) || !point_ok(z1, b0) || !point_ok(z1, b1) || !point_ok(z2, p))
        FAIL();
    int64_t dbx = z1->cur[b1].x - z1->cur[b0].x, dby = z1->cur[b1].y - z1->cur[b0].y;
    int64_t dax = z0->cur[a1].x - z0->cur[a0].x, day = z0->cur[a1].y - z0->cur[a0].y;
    int64_t dx = z1->cur[b0].x - z0->cur[a0].x, dy = z1->cur[b0].y - z0->cur[a0].y;
    z2->tag[p] |= TAG_TOUCH_X | TAG_TOUCH_Y;
    if (phase_axis(ex) && ex->gs.zp2 != 0) {
        int32_t lead = b0, follow = b1;
        int64_t across_a = mul_26_6((int32_t)dax, (int32_t)dby), across_b = mul_26_6((int32_t)day, (int32_t)dbx);
        if ((across_b < 0 ? -across_b : across_b) > (across_a < 0 ? -across_a : across_a)) {
            lead = a0;
            follow = a1;
        }
        phase_proportion(ex, z2, lead, p, follow);
    }
    int64_t discriminant = dax * -dby + day * dbx;
    int64_t dotproduct = dax * dbx + day * dby;
    int64_t x, y;
    /* the lines are parallel when the discriminant is small against the dot product. */
    int64_t ad = discriminant < 0 ? -discriminant : discriminant;
    int64_t adot = dotproduct < 0 ? -dotproduct : dotproduct;
    if (ad * 19 > adot) {
        int64_t val = dx * -dby + dy * dbx;
        x = z0->cur[a0].x + val * dax / discriminant;
        y = z0->cur[a0].y + val * day / discriminant;
    } else {
        x = ((int64_t)z0->cur[a0].x + z0->cur[a1].x + z1->cur[b0].x + z1->cur[b1].x) / 4;
        y = ((int64_t)z0->cur[a0].y + z0->cur[a1].y + z1->cur[b0].y + z1->cur[b1].y) / 4;
    }
    z2->cur[p].x = sat32(x);
    z2->cur[p].y = sat32(y);
}

/* ------------------------------------------------------------------ interpolate untouched points */

static void iup_run(Zone *z, bool x_axis, uint32_t first, uint32_t last)
{
    uint8_t mask = x_axis ? TAG_TOUCH_X : TAG_TOUCH_Y;
    uint32_t n = last - first + 1;
    /* a coordinate accessor by axis keeps the loop short. */
#define COORD(arr, i) (x_axis ? (arr)[i].x : (arr)[i].y)
#define SETC(arr, i, v) do { if (x_axis) (arr)[i].x = (v); else (arr)[i].y = (v); } while (0)
    uint32_t start = first;
    while (start <= last && !(z->tag[start] & mask))
        start++;
    if (start > last)
        return;
    uint32_t touched = 0;
    for (uint32_t i = first; i <= last; i++)
        if (z->tag[i] & mask)
            touched++;
    if (touched == 1) {
        int32_t delta = COORD(z->cur, start) - COORD(z->org, start);
        if (delta == 0)
            return;
        for (uint32_t i = first; i <= last; i++)
            if (!(z->tag[i] & mask))
                SETC(z->cur, i, COORD(z->cur, i) + delta);
        return;
    }
    uint32_t t1 = start;
    for (;;) {
        /* find the next touched point after t1, cyclically. */
        uint32_t k = t1;
        uint32_t t2;
        for (;;) {
            k = k == last ? first : k + 1;
            if (z->tag[k] & mask) {
                t2 = k;
                break;
            }
        }
        /* the untouched points strictly between t1 and t2. */
        uint32_t count = (t2 + n - t1) % n;
        if (count > 1) {
            /* the lower one in font units leads, the second of two equal ones when they are. */
            uint32_t lo = t1, hi = t2;
            if (COORD(z->orus, t1) >= COORD(z->orus, t2)) {
                lo = t2;
                hi = t1;
            }
            int32_t o1 = COORD(z->orus, lo), o2 = COORD(z->orus, hi);
            int32_t c1 = COORD(z->cur, lo), c2 = COORD(z->cur, hi);
            int32_t g1 = COORD(z->org, lo), g2 = COORD(z->org, hi);
            int32_t d1 = c1 - g1, d2 = c2 - g2;
            int32_t span = o2 - o1, rise = c2 - c1;
            int32_t ratio = 0;
            bool small = span < 0x8000 && rise < 0x8000;
            if (span && !small) {
                int64_t num = (int64_t)rise << 16;
                int64_t half = span / 2;
                ratio = (int32_t)(((span < 0) == (num < 0) ? num + half : num - half) / span);
            }
            uint32_t p = t1;
            for (uint32_t j = 1; j < count; j++) {
                p = p == last ? first : p + 1;
                int32_t orus = COORD(z->orus, p);
                int32_t org = COORD(z->org, p);
                int32_t v;
                if (span == 0)
                    v = COORD(z->cur, p) + d1;
                else if (org <= g1)
                    v = org + d1;
                else if (org >= g2)
                    v = org + d2;
                else if (small)
                    v = c1 + (int32_t)((span / 2 + (int64_t)rise * (orus - o1)) / span);
                else
                    v = c1 + mul_fix(orus - o1, ratio);
                SETC(z->cur, p, v);
            }
        }
        t1 = t2;
        if (t1 == start)
            break;
    }
#undef COORD
#undef SETC
}

static void ins_iup(Exec *ex, uint8_t op)
{
    bool x_axis = (op & 1) != 0;
    Zone *z = zone_of(ex, 1);
    if (x_axis && ex->links && !ex->phase_done)
        phase_run(ex);
    uint32_t first = 0;
    for (uint32_t c = 0; c < z->contours; c++) {
        uint32_t last = z->ends[c];
        if (last >= z->n)
            FAIL();
        if (last >= first)
            iup_run(z, x_axis, first, last);
        first = last + 1;
    }
    if (x_axis)
        ex->iupx = true;
    else
        ex->iupy = true;
}

/* ------------------------------------------------------------------ deltas */

static void ins_deltap(Exec *ex, uint8_t op)
{
    int32_t n;
    if (!pop(ex, &n))
        return;
    int32_t base = ex->gs.delta_base + (op == 0x5D ? 0 : op == 0x71 ? 16 : 32);
    Zone *z = zone_of(ex, ex->gs.zp0);
    for (; n > 0; n--) {
        int32_t p, arg;
        if (!pop(ex, &p) || !pop(ex, &arg))
            return;
        if (!point_ok(z, p))
            FAIL();
        int32_t ppem = base + ((arg >> 4) & 0xF);
        if (ppem != ex->ppem_for_delta)
            continue;
        /* with ClearType only moves along y of points touched along y and not yet interpolated are kept. */
        if ((ex->h->flags & MIME_TTF_HINT_CLEARTYPE) && !ex->h->native &&
            !(ex->gs.fv.x == 0 && ex->gs.fv.y == 0x4000 && (z->tag[p] & TAG_TOUCH_Y) && !ex->iupy))
            continue;
        int32_t step = (arg & 0xF) < 8 ? (arg & 0xF) - 8 : (arg & 0xF) - 7;
        int32_t mag = step * (64 >> ex->gs.delta_shift);
        move_point(ex, z, (uint32_t)p, mag);
    }
}

static void ins_deltac(Exec *ex, uint8_t op)
{
    int32_t n;
    if (!pop(ex, &n))
        return;
    int32_t base = ex->gs.delta_base + (op == 0x73 ? 0 : op == 0x74 ? 16 : 32);
    for (; n > 0; n--) {
        int32_t c, arg;
        if (!pop(ex, &c) || !pop(ex, &arg))
            return;
        if (c < 0 || (uint32_t)c >= ex->cvt_count)
            FAIL();
        int32_t ppem = base + ((arg >> 4) & 0xF);
        if (ppem != ex->ppem_for_delta)
            continue;
        int32_t step = (arg & 0xF) < 8 ? (arg & 0xF) - 8 : (arg & 0xF) - 7;
        ex->cvt[c] += step * (64 >> ex->gs.delta_shift);
    }
}

/* ------------------------------------------------------------------ the main loop */

static bool call_function(Exec *ex, int32_t number, uint32_t count, bool patched)
{
    const MimeTtfHinter *h = ex->h;
    if (number < 0 || (uint32_t)number >= h->func_count || !h->funcs[number].active || ex->depth >= MAX_CALL_DEPTH) {
        ex->failed = true;
        return false;
    }
    if (count == 0)
        return true;
    Call *c = &ex->calls[ex->depth++];
    c->code = ex->code;
    c->ip = ex->ip;
    c->count = count;
    c->func = &h->funcs[number];
    c->patch = HELD_NONE;
    c->special = false;
    if (patched && (h->flags & MIME_TTF_HINT_CLEARTYPE) && !h->native) {
        if (number >= 64) {
            if (number <= 66 && (h->patches & PATCH_STORAGE22) && ex->storage_count > 22) {
                c->patch = HELD_STORAGE22;
                c->saved = ex->storage[22];
                ex->storage[22] = 0;
            }
        } else {
            if ((number <= 2 || number == 4 || (number >= 7 && number <= 8)) && (h->patches & PATCH_STORAGE24) &&
                ex->storage_count > 24) {
                c->patch = HELD_STORAGE24;
                c->saved = ex->storage[24];
                ex->storage[24] = 0;
            }
            if (number == 58 && (h->patches & PATCH_CUTIN)) {
                c->patch = HELD_CUTIN;
                c->saved = ex->gs.cvt_cutin;
                ex->gs.cvt_cutin = 0x7FFFFFFF;
            }
        }
        for (uint32_t i = 0; i < h->special_count; i++) {
            if (h->special[i] == number) {
                c->special = true;
                ex->special_depth++;
                break;
            }
        }
    }
    ex->code = c->func->code;
    ex->ip = 0;
    return true;
}

/* gives back what a patched call held. */
static void end_call(Exec *ex, const Call *c)
{
    switch (c->patch) {
    case HELD_STORAGE22: ex->storage[22] = c->saved; break;
    case HELD_STORAGE24: ex->storage[24] = c->saved; break;
    case HELD_CUTIN: ex->gs.cvt_cutin = c->saved; break;
    default: break;
    }
    if (c->special && ex->special_depth)
        ex->special_depth--;
}

static bool starts_with(const uint8_t *body, uint32_t size, const uint8_t *want, uint32_t n)
{
    return size >= n && __builtin_memcmp(body, want, n) == 0;
}

/* looks at the first bytes of a function a font program defined. */
static void note_signature(MimeTtfHinter *h, int32_t n, const uint8_t *body, uint32_t size)
{
    static const uint8_t storage22[] = {0xB0, 0x16, 0x43, 0x58};
    static const uint8_t storage24a[] = {0x01, 0xB0, 0x18, 0x43, 0x58};
    static const uint8_t storage24b[] = {0x01, 0x18, 0xB0, 0x18, 0x43, 0x58};
    static const uint8_t read8[] = {0x45, 0x23, 0x46, 0x60, 0x20, 0xB0, 0x26};
    static const uint8_t cutin[] = {0x20, 0x20, 0xB0, 0x01, 0x60, 0x46, 0xB0, 0x40, 0x23, 0x42};
    static const uint8_t delta_a[] = {0x4B, 0x54, 0x58, 0x38, 0x1B, 0x21, 0x21, 0x59, 0x2D};
    static const uint8_t delta_b[] = {0x4B, 0x53, 0x23, 0x4B, 0x51, 0x5A, 0x58, 0x38, 0x1B, 0x21, 0x21, 0x59, 0x2D};
    if (n >= 64) {
        if (n <= 66 && starts_with(body, size, storage22, sizeof(storage22)))
            h->patches |= PATCH_STORAGE22;
    } else {
        if ((n <= 2 || n == 4 || (n >= 7 && n <= 8)) &&
            (starts_with(body, size, storage24a, sizeof(storage24a)) ||
             starts_with(body, size, storage24b, sizeof(storage24b))))
            h->patches |= PATCH_STORAGE24;
        if (n == 0 && starts_with(body, size, read8, sizeof(read8)))
            h->patches |= PATCH_READ8;
        if (n == 58 && starts_with(body, size, cutin, sizeof(cutin)))
            h->patches |= PATCH_CUTIN;
    }
    if ((starts_with(body, size, delta_a, sizeof(delta_a)) || starts_with(body, size, delta_b, sizeof(delta_b))) &&
        h->special_count < 4)
        h->special[h->special_count++] = (uint16_t)n;
}

static void ins_fdef(Exec *ex)
{
    int32_t n;
    if (!pop(ex, &n))
        return;
    MimeTtfHinter *h = ex->h;
    if (ex->program == PROGRAM_GLYPH || n < 0 || (uint32_t)n >= h->func_count)
        FAIL();
    uint32_t begin = ex->ip;
    while (ex->ip < ex->code.size) {
        uint32_t len = instr_length(ex->code, ex->ip);
        if (!len)
            FAIL();
        uint8_t op = ex->code.data[ex->ip];
        if (op == 0x2D) {
            h->funcs[n].code.data = ex->code.data + begin;
            h->funcs[n].code.size = ex->ip - begin + 1;
            h->funcs[n].active = true;
            ex->ip += len;
            note_signature(h, n, ex->code.data + begin, ex->code.size - begin);
            return;
        }
        if (op == 0x2C)
            FAIL();
        ex->ip += len;
    }
    ex->failed = true;
}

static void ins_idef(Exec *ex)
{
    int32_t n;
    if (!pop(ex, &n))
        return;
    MimeTtfHinter *h = ex->h;
    if (ex->program == PROGRAM_GLYPH || n < 0 || n > 255)
        FAIL();
    InstrDef *slot = nullptr;
    for (uint32_t i = 0; i < h->idef_count; i++)
        if (h->idefs[i].active && h->idefs[i].opcode == n) {
            slot = &h->idefs[i];
            break;
        }
    for (uint32_t i = 0; !slot && i < h->idef_count; i++)
        if (!h->idefs[i].active)
            slot = &h->idefs[i];
    if (!slot)
        FAIL();
    uint32_t begin = ex->ip;
    while (ex->ip < ex->code.size) {
        uint32_t len = instr_length(ex->code, ex->ip);
        if (!len)
            FAIL();
        uint8_t op = ex->code.data[ex->ip];
        if (op == 0x2D) {
            slot->code.data = ex->code.data + begin;
            slot->code.size = ex->ip - begin + 1;
            slot->opcode = (uint8_t)n;
            slot->active = true;
            ex->ip += len;
            return;
        }
        ex->ip += len;
    }
    ex->failed = true;
}

#define POP1(a) int32_t a; if (!pop(ex, &a)) return
#define POP2(a, b) int32_t a, b; if (!pop(ex, &b) || !pop(ex, &a)) return

static void execute_one(Exec *ex, uint8_t op)
{
    GState *gs = &ex->gs;
    switch (op) {
    case 0x00: case 0x01: {
        V2 v = (op & 1) ? (V2){0x4000, 0} : (V2){0, 0x4000};
        gs->pv = gs->dv = gs->fv = v;
        ex->dual_is_project = false;
        ex->line_p1 = ex->line_p2 = -1;
        compute_fdotp(ex);
        return;
    }
    case 0x02: case 0x03: {
        gs->pv = gs->dv = (op & 1) ? (V2){0x4000, 0} : (V2){0, 0x4000};
        ex->dual_is_project = false;
        ex->line_p1 = ex->line_p2 = -1;
        compute_fdotp(ex);
        return;
    }
    case 0x04: case 0x05:
        gs->fv = (op & 1) ? (V2){0x4000, 0} : (V2){0, 0x4000};
        compute_fdotp(ex);
        return;
    case 0x06: case 0x07: {
        POP2(p2, p1);
        /* p1 is the top of the stack, in zone zp2, and p2 in zp1. */
        if (!set_vector_from_points(&gs->pv, p1, p2, op & 1, false, zone_of(ex, gs->zp2), zone_of(ex, gs->zp1)))
            FAIL();
        gs->dv = gs->pv;
        ex->dual_is_project = true;
        ex->line_p1 = p1;
        ex->line_p2 = p2;
        compute_fdotp(ex);
        return;
    }
    case 0x08: case 0x09: {
        POP2(p2, p1);
        if (!set_vector_from_points(&gs->fv, p1, p2, op & 1, false, zone_of(ex, gs->zp2), zone_of(ex, gs->zp1)))
            FAIL();
        compute_fdotp(ex);
        return;
    }
    case 0x0A: {
        POP2(x, y);
        V2 v;
        v.x = (int16_t)x;
        v.y = (int16_t)y;
        if (!normalize(v.x, v.y, &v))
            FAIL();
        gs->pv = gs->dv = v;
        ex->dual_is_project = true;
        ex->line_p1 = ex->line_p2 = -1;
        compute_fdotp(ex);
        return;
    }
    case 0x0B: {
        POP2(x, y);
        V2 v;
        v.x = (int16_t)x;
        v.y = (int16_t)y;
        if (!normalize(v.x, v.y, &v))
            FAIL();
        gs->fv = v;
        compute_fdotp(ex);
        return;
    }
    case 0x0C: push(ex, gs->pv.x); push(ex, gs->pv.y); return;
    case 0x0D: push(ex, gs->fv.x); push(ex, gs->fv.y); return;
    case 0x0E: gs->fv = gs->pv; ex->fdotp = 0x4000; return;
    case 0x0F: ins_isect(ex); return;
    case 0x10: { POP1(a); if (a < 0) FAIL(); gs->rp0 = (uint32_t)a; return; }
    case 0x11: { POP1(a); if (a < 0) FAIL(); gs->rp1 = (uint32_t)a; return; }
    case 0x12: { POP1(a); if (a < 0) FAIL(); gs->rp2 = (uint32_t)a; return; }
    case 0x13: { POP1(a); if (a < 0 || a > 1) FAIL(); gs->zp0 = a; return; }
    case 0x14: { POP1(a); if (a < 0 || a > 1) FAIL(); gs->zp1 = a; return; }
    case 0x15: { POP1(a); if (a < 0 || a > 1) FAIL(); gs->zp2 = a; return; }
    case 0x16: { POP1(a); if (a < 0 || a > 1) FAIL(); gs->zp0 = gs->zp1 = gs->zp2 = a; return; }
    case 0x17: { POP1(a); if (a < 0) FAIL(); gs->loop = a > 0xFFFF ? 0xFFFF : a; return; }
    case 0x18: gs->round_mode = ROUND_GRID; return;
    case 0x19: gs->round_mode = ROUND_HALF; return;
    case 0x1A: { POP1(a); gs->min_dist = a; return; }
    case 0x1B: /* ELSE reached by falling through the true branch. */
        if (!skip_if(ex, false))
            FAIL();
        return;
    case 0x1C: {
        POP1(off);
        if (off == 0 && false)
            FAIL();
        int64_t target = (int64_t)ex->ip - 1 + off;
        if (target < 0 || target > ex->code.size)
            FAIL();
        ex->ip = (uint32_t)target;
        return;
    }
    case 0x1D: { POP1(a); gs->cvt_cutin = a; return; }
    case 0x1E: { POP1(a); gs->sw_cutin = a; return; }
    case 0x1F: { POP1(a); gs->sw_value = scale_apply(&ex->h->su, a); return; }
    case 0x20: { POP1(a); push(ex, a); push(ex, a); return; }
    case 0x21: { POP1(a); (void)a; return; }
    case 0x22: ex->sp = 0; return;
    case 0x23: { POP2(a, b); push(ex, b); push(ex, a); return; }
    case 0x24: push(ex, (int32_t)ex->sp); return;
    case 0x25: {
        POP1(k);
        if (k <= 0 || (uint32_t)k > ex->sp)
            FAIL();
        push(ex, ex->stack[ex->sp - (uint32_t)k]);
        return;
    }
    case 0x26: {
        POP1(k);
        if (k <= 0 || (uint32_t)k > ex->sp)
            FAIL();
        int32_t v = ex->stack[ex->sp - (uint32_t)k];
        for (uint32_t i = ex->sp - (uint32_t)k; i + 1 < ex->sp; i++)
            ex->stack[i] = ex->stack[i + 1];
        ex->stack[ex->sp - 1] = v;
        return;
    }
    case 0x27: ins_alignpts(ex); return;
    case 0x29: {
        POP1(p);
        Zone *z = zone_of(ex, gs->zp0);
        if (!point_ok(z, p))
            FAIL();
        if (gs->fv.x)
            z->tag[p] &= (uint8_t)~TAG_TOUCH_X;
        if (gs->fv.y)
            z->tag[p] &= (uint8_t)~TAG_TOUCH_Y;
        return;
    }
    case 0x2A: {
        POP2(count, f);
        if (count < 0)
            FAIL();
        call_function(ex, f, (uint32_t)count, false);
        return;
    }
    case 0x2B: {
        POP1(f);
        call_function(ex, f, 1, true);
        return;
    }
    case 0x2C: ins_fdef(ex); return;
    case 0x2D: {
        if (ex->depth == 0)
            FAIL();
        Call *c = &ex->calls[ex->depth - 1];
        if (c->count > 1) {
            c->count--;
            ex->ip = 0;
            return;
        }
        end_call(ex, c);
        ex->code = c->code;
        ex->ip = c->ip;
        ex->depth--;
        return;
    }
    case 0x2E: case 0x2F: ins_mdap(ex, op); return;
    case 0x30: case 0x31: ins_iup(ex, op); return;
    case 0x32: case 0x33: ins_shp(ex, op); return;
    case 0x34: case 0x35: ins_shc(ex, op); return;
    case 0x36: case 0x37: ins_shz(ex, op); return;
    case 0x38: ins_shpix(ex); return;
    case 0x39: ins_ip(ex); return;
    case 0x3A: case 0x3B: ins_msirp(ex, op); return;
    case 0x3C: ins_alignrp(ex); return;
    case 0x3D: gs->round_mode = ROUND_DOUBLE; return;
    case 0x3E: case 0x3F: ins_miap(ex, op); return;
    case 0x40: {
        uint32_t n = ex->code.data[ex->ip];
        ex->ip++;
        for (uint32_t i = 0; i < n; i++)
            push(ex, ex->code.data[ex->ip++]);
        return;
    }
    case 0x41: {
        uint32_t n = ex->code.data[ex->ip];
        ex->ip++;
        for (uint32_t i = 0; i < n; i++) {
            push(ex, (int16_t)be16(ex->code.data + ex->ip));
            ex->ip += 2;
        }
        return;
    }
    case 0x42: {
        POP2(i, v);
        if (i < 0 || (uint32_t)i >= ex->storage_count)
            FAIL();
        ex->storage[i] = v;
        return;
    }
    case 0x43: {
        POP1(i);
        if (i < 0 || (uint32_t)i >= ex->storage_count)
            FAIL();
        if (i == 8 && (ex->h->flags & MIME_TTF_HINT_CLEARTYPE) && !ex->h->native && (ex->h->patches & PATCH_READ8)) {
            ex->read8 = true;
            push(ex, 0);
            return;
        }
        push(ex, ex->storage[i]);
        return;
    }
    case 0x44: {
        POP2(i, v);
        if (i < 0 || (uint32_t)i >= ex->cvt_count)
            FAIL();
        ex->cvt[i] = ex->h->stretched ? mul_div(v, 0x10000, current_ratio(ex)) : v;
        return;
    }
    case 0x45: {
        POP1(i);
        if (i < 0 || (uint32_t)i >= ex->cvt_count)
            FAIL();
        push(ex, read_cvt(ex, i));
        return;
    }
    case 0x46: case 0x47: {
        POP1(p);
        Zone *z = zone_of(ex, gs->zp2);
        if (!point_ok(z, p))
            FAIL();
        if (op & 1)
            push(ex, dual_project(ex, z->org[p].x, z->org[p].y));
        else
            push(ex, project(ex, z->cur[p].x, z->cur[p].y));
        return;
    }
    case 0x48: {
        POP2(p, v);
        Zone *z = zone_of(ex, gs->zp2);
        if (!point_ok(z, p))
            FAIL();
        int32_t cur = project(ex, z->cur[p].x, z->cur[p].y);
        move_point(ex, z, (uint32_t)p, v - cur);
        if (gs->zp2 == 0)
            z->org[p] = z->cur[p];
        return;
    }
    case 0x49: case 0x4A: {
        POP2(p2, p1);
        /* the top of the stack indexes zp1 and the other zp0. */
        Zone *z0 = zone_of(ex, gs->zp0), *z1 = zone_of(ex, gs->zp1);
        if (!point_ok(z0, p2) || !point_ok(z1, p1))
            FAIL();
        if (op == 0x49) {
            int32_t d = project(ex, z0->cur[p2].x - z1->cur[p1].x, z0->cur[p2].y - z1->cur[p1].y);
            /* the patched fonts measure 65 where the stem is exactly a pixel, once both axes were interpolated. */
            if (d == 64 && ex->iupx && ex->iupy && ex->read8)
                d = 65;
            push(ex, d);
        }
        else
            push(ex, original_distance(ex, z1, (uint32_t)p1, z0, (uint32_t)p2));
        return;
    }
    case 0x4B: {
        int32_t ppem = ex->h->ppem_y;
        if (ex->h->stretched)
            ppem = mul_fix(ex->h->ppem_y, current_ratio(ex));
        push(ex, ppem);
        return;
    }
    case 0x4C: push(ex, ex->h->ppem_y); return;
    case 0x4D: gs->auto_flip = true; return;
    case 0x4E: gs->auto_flip = false; return;
    case 0x4F: { POP1(a); (void)a; return; }
    case 0x50: { POP2(a, b); push(ex, a < b); return; }
    case 0x51: { POP2(a, b); push(ex, a <= b); return; }
    case 0x52: { POP2(a, b); push(ex, a > b); return; }
    case 0x53: { POP2(a, b); push(ex, a >= b); return; }
    case 0x54: { POP2(a, b); push(ex, a == b); return; }
    case 0x55: { POP2(a, b); push(ex, a != b); return; }
    case 0x56: { POP1(a); push(ex, (round_pixel(a) >> 6) & 1); return; }
    case 0x57: { POP1(a); push(ex, ((round_pixel(a) >> 6) - 1) & 1); return; }
    case 0x58: {
        POP1(c);
        if (c == 0 && !skip_if(ex, true))
            FAIL();
        return;
    }
    case 0x59: return;
    case 0x5A: { POP2(a, b); push(ex, a != 0 && b != 0); return; }
    case 0x5B: { POP2(a, b); push(ex, a != 0 || b != 0); return; }
    case 0x5C: { POP1(a); push(ex, a == 0); return; }
    case 0x5D: case 0x71: case 0x72: ins_deltap(ex, op); return;
    case 0x5E: { POP1(a); gs->delta_base = a; return; }
    case 0x5F: { POP1(a); if (a < 0 || a > 6) FAIL(); gs->delta_shift = a; return; }
    case 0x60: { POP2(a, b); push(ex, sat32((int64_t)a + b)); return; }
    case 0x61: { POP2(a, b); push(ex, sat32((int64_t)a - b)); return; }
    case 0x62: {
        POP2(a, b);
        if (b == 0)
            FAIL();
        push(ex, sat32((int64_t)a * 64 / b));
        return;
    }
    case 0x63: { POP2(a, b); push(ex, mul_26_6(a, b)); return; }
    case 0x64: { POP1(a); push(ex, a < 0 ? (a == INT32_MIN ? INT32_MAX : -a) : a); return; }
    case 0x65: { POP1(a); push(ex, a == INT32_MIN ? INT32_MAX : -a); return; }
    case 0x66: { POP1(a); push(ex, a & ~63); return; }
    case 0x67: { POP1(a); push(ex, sat32((int64_t)a + 63) & ~63); return; }
    case 0x68: case 0x69: case 0x6A: case 0x6B: { POP1(a); push(ex, round_value(ex, a)); return; }
    case 0x6C: case 0x6D: case 0x6E: case 0x6F: { POP1(a); push(ex, a); return; }
    case 0x70: {
        POP2(i, v);
        if (i < 0 || (uint32_t)i >= ex->cvt_count)
            FAIL();
        ex->cvt[i] = scale_apply(&ex->h->su, v);
        return;
    }
    case 0x73: case 0x74: case 0x75: ins_deltac(ex, op); return;
    case 0x76: { POP1(a); gs->round_mode = ROUND_SUPER; set_super_round(ex, a, 64); return; }
    case 0x77: { POP1(a); gs->round_mode = ROUND_SUPER45; set_super_round(ex, a, 45); return; }
    case 0x78: case 0x79: {
        POP2(off, c);
        if ((op == 0x78) == (c != 0)) {
            int64_t target = (int64_t)ex->ip - 1 + off;
            if (off == 0 || target < 0 || target > ex->code.size)
                FAIL();
            ex->ip = (uint32_t)target;
        }
        return;
    }
    case 0x7A: gs->round_mode = ROUND_OFF; return;
    case 0x7C: gs->round_mode = ROUND_UP; return;
    case 0x7D: gs->round_mode = ROUND_DOWN; return;
    case 0x7E: case 0x7F: { POP1(a); (void)a; return; }
    case 0x80: {
        Zone *z = zone_of(ex, 1);
        while (gs->loop > 0) {
            POP1(p);
            if (!point_ok(z, p))
                FAIL();
            z->tag[p] ^= TAG_ON;
            gs->loop--;
        }
        gs->loop = 1;
        return;
    }
    case 0x81: case 0x82: {
        POP2(lo, hi);
        Zone *z = zone_of(ex, 1);
        if (!point_ok(z, lo) || !point_ok(z, hi))
            FAIL();
        for (int32_t p = lo; p <= hi; p++) {
            if (op == 0x81)
                z->tag[p] |= TAG_ON;
            else
                z->tag[p] &= (uint8_t)~TAG_ON;
        }
        return;
    }
    case 0x85: { POP1(a); ex->gs.scan_control = (uint32_t)a; return; }
    case 0x86: case 0x87: {
        POP2(p2, p1);
        Zone *za = zone_of(ex, gs->zp2), *zb = zone_of(ex, gs->zp1);
        if (!point_ok(za, p1) || !point_ok(zb, p2))
            FAIL();
        V2 v;
        if (!set_vector_from_points(&v, p1, p2, op & 1, true, za, zb))
            FAIL();
        gs->dv = v;
        V2 w;
        if (!set_vector_from_points(&w, p1, p2, op & 1, false, za, zb))
            FAIL();
        gs->pv = w;
        ex->dual_is_project = false;
        ex->line_p1 = p1;
        ex->line_p2 = p2;
        compute_fdotp(ex);
        return;
    }
    case 0x88: ins_getinfo(ex); return;
    case 0x89: ins_idef(ex); return;
    case 0x8A: {
        if (ex->sp < 3)
            FAIL();
        int32_t a = ex->stack[ex->sp - 3], b = ex->stack[ex->sp - 2], c = ex->stack[ex->sp - 1];
        ex->stack[ex->sp - 3] = b;
        ex->stack[ex->sp - 2] = c;
        ex->stack[ex->sp - 1] = a;
        return;
    }
    case 0x8B: { POP2(a, b); push(ex, a > b ? a : b); return; }
    case 0x8C: { POP2(a, b); push(ex, a < b ? a : b); return; }
    case 0x8D: { POP1(a); gs->scan_type = (uint32_t)a; return; }
    case 0x8E: {
        POP2(value, sel);
        if (sel < 1 || sel > 3)
            return;
        uint32_t bit = 1u << (sel - 1);
        if (sel == 3) {
            if (ex->program == PROGRAM_PREP && (value == 0 || value == 4))
                ex->h->native = value == 4;
            return;
        }
        if (ex->program != PROGRAM_PREP)
            return;
        if (value != 0 && value != (int32_t)bit)
            return;
        gs->instruct_control = (gs->instruct_control & ~bit) | (uint32_t)value;
        return;
    }
    default:
        break;
    }
    if (op >= 0xB0 && op <= 0xB7) {
        uint32_t n = op - 0xB0 + 1;
        for (uint32_t i = 0; i < n; i++)
            push(ex, ex->code.data[ex->ip++]);
        return;
    }
    if (op >= 0xB8 && op <= 0xBF) {
        uint32_t n = op - 0xB8 + 1;
        for (uint32_t i = 0; i < n; i++) {
            push(ex, (int16_t)be16(ex->code.data + ex->ip));
            ex->ip += 2;
        }
        return;
    }
    if (op >= 0xC0 && op <= 0xDF) {
        ins_mdrp(ex, op);
        return;
    }
    if (op >= 0xE0) {
        ins_mirp(ex, op);
        return;
    }
    /* user defined instructions. */
    for (uint32_t i = 0; i < ex->h->idef_count; i++) {
        const InstrDef *d = &ex->h->idefs[i];
        if (d->active && d->opcode == op) {
            if (ex->depth >= MAX_CALL_DEPTH)
                FAIL();
            Call *c = &ex->calls[ex->depth++];
            c->code = ex->code;
            c->ip = ex->ip;
            c->count = 1;
            c->func = nullptr;
            c->patch = HELD_NONE;
            c->special = false;
            ex->code = d->code;
            ex->ip = 0;
            return;
        }
    }
    ex->failed = true;
}





static bool run_program(Exec *ex, Code code, int program)
{
    ex->code = code;
    ex->ip = 0;
    ex->depth = 0;
    ex->program = program;
    ex->failed = false;
    ex->steps = 0;
    while (!ex->failed) {
        if (ex->ip >= ex->code.size) {
            if (ex->depth != 0)
                return false;
            return true;
        }
        if (++ex->steps > ex->step_limit)
            return false;
        uint32_t len = instr_length(ex->code, ex->ip);
        if (!len)
            return false;
        uint8_t op = ex->code.data[ex->ip++];
        execute_one(ex, op);
    }
    return false;
}

/* ------------------------------------------------------------------ setup */

static void default_state(GState *gs)
{
    memset(gs, 0, sizeof(*gs));
    gs->dv = gs->pv = gs->fv = (V2){0x4000, 0};
    gs->loop = 1;
    gs->min_dist = 64;
    gs->round_mode = ROUND_GRID;
    gs->period = 64;
    gs->phase = 0;
    gs->threshold = 32;
    gs->auto_flip = true;
    gs->cvt_cutin = 68;
    gs->delta_base = 9;
    gs->delta_shift = 3;
    gs->zp0 = gs->zp1 = gs->zp2 = 1;
}

static void exec_init(Exec *ex, MimeTtfHinter *h, const GState *gs)
{
    memset(ex, 0, sizeof(*ex));
    ex->h = h;
    ex->gs = *gs;
    ex->stack = h->stack;
    ex->stack_cap = h->stack_cap;
    ex->zones[0] = h->twilight;
    ex->cvt = h->cvt;
    ex->storage = h->storage;
    ex->cvt_count = h->cvt_count;
    ex->storage_count = h->storage_count;
    ex->ppem_for_delta = h->ppem_y;
    ex->line_p1 = ex->line_p2 = -1;
    compute_fdotp(ex);
}

static void *halloc(const MimeTtfHinter *h, size_t n)
{
    return mime_alloc(&h->alloc, n ? n : 1);
}

void mime_ttf_hinter_free(MimeTtfHinter *h)
{
    if (!h)
        return;
    MimeAllocator a = h->alloc;
    mime_free(&a, h->cvt);
    mime_free(&a, h->storage);
    mime_free(&a, h->funcs);
    mime_free(&a, h->idefs);
    mime_free(&a, h->stack);
    mime_free(&a, h->twilight.org);
    mime_ttf_hinter_free(h->plain);
    mime_free(&a, h);
}

static uint32_t maxp_value(const MimeTtfFont *f, uint32_t off)
{
    if (f->maxp.size < off + 2)
        return 0;
    return be16(f->maxp.data + off);
}

MimeTtfHinter *mime_ttf_hinter_new(const MimeTtfFont *font, const MimeAllocator *alloc, int ppem_x, int ppem_y,
                                   uint32_t flags)
{
    if (!font || !alloc || !font->has_outlines || ppem_x < 1 || ppem_y < 1 || ppem_x > 2048 || ppem_y > 2048)
        return nullptr;
    MimeTtfHinter *h = mime_alloc(alloc, sizeof(*h));
    if (!h)
        return nullptr;
    memset(h, 0, sizeof(*h));
    h->font = font;
    h->alloc = *alloc;
    h->ppem_x = ppem_x;
    h->ppem_y = ppem_y;
    h->flags = flags;
    h->stretched = ppem_x != ppem_y;
    h->scale_x = (int32_t)(((int64_t)ppem_x * 64 * 65536 + font->units_per_em / 2) / font->units_per_em);
    h->scale_y = (int32_t)(((int64_t)ppem_y * 64 * 65536 + font->units_per_em / 2) / font->units_per_em);
    scale_init(&h->sx, ppem_x, font->units_per_em);
    scale_init(&h->sy, ppem_y, font->units_per_em);
    h->su = ppem_x >= ppem_y ? h->sx : h->sy;
    default_state(&h->defaults);

    uint32_t storage = maxp_value(font, 18);
    uint32_t funcs = maxp_value(font, 20);
    uint32_t idefs = maxp_value(font, 22);
    uint32_t stack = maxp_value(font, 24);
    uint32_t twilight = maxp_value(font, 16);
    if (font->maxp.size < 32) {
        storage = funcs = idefs = stack = twilight = 0;
    }
    h->storage_count = storage;
    h->func_count = funcs;
    h->idef_count = idefs;
    h->stack_cap = stack + 32;
    if (h->stack_cap < 256)
        h->stack_cap = 256;
    if (h->stack_cap > 65536)
        h->stack_cap = 65536;
    h->cvt_count = font->cvt.size / 2;
    if (twilight > 4096)
        twilight = 4096;

    h->cvt = halloc(h, h->cvt_count * sizeof(int32_t));
    h->storage = halloc(h, h->storage_count * sizeof(int32_t));
    h->funcs = halloc(h, h->func_count * sizeof(FuncDef));
    h->idefs = halloc(h, h->idef_count * sizeof(InstrDef));
    h->stack = halloc(h, h->stack_cap * sizeof(int32_t));
    uint32_t tn = twilight + 4;
    size_t zbytes = (size_t)tn * (3 * sizeof(V2) + 1);
    h->twilight.org = halloc(h, zbytes);
    if (!h->cvt || !h->storage || !h->funcs || !h->idefs || !h->stack || !h->twilight.org) {
        mime_ttf_hinter_free(h);
        return nullptr;
    }
    memset(h->funcs, 0, h->func_count * sizeof(FuncDef));
    memset(h->idefs, 0, h->idef_count * sizeof(InstrDef));
    memset(h->storage, 0, h->storage_count * sizeof(int32_t));
    memset(h->twilight.org, 0, zbytes);
    h->twilight.cur = h->twilight.org + tn;
    h->twilight.orus = h->twilight.cur + tn;
    h->twilight.tag = (uint8_t *)(h->twilight.orus + tn);
    h->twilight.n = tn;
    h->twilight.cap = tn;

    for (uint32_t i = 0; i < h->cvt_count; i++)
        h->cvt[i] = scale_apply(&h->su, (int16_t)be16(font->cvt.data + 2 * i));

    Exec ex;
    Zone empty = {nullptr, nullptr, nullptr, nullptr, nullptr, 0, 0, 0};
    exec_init(&ex, h, &h->defaults);
    ex.zones[1] = empty;
    ex.step_limit = MAX_PROGRAM_STEPS;
    bool ok = true;
    if (font->fpgm.size) {
        Code c = {font->fpgm.data, font->fpgm.size};
        ok = run_program(&ex, c, PROGRAM_FONT);
    }
    if (ok && font->prep.size) {
        /* the prep starts from the default state each time. */
        exec_init(&ex, h, &h->defaults);
        ex.zones[1] = empty;
        ex.step_limit = MAX_PROGRAM_STEPS;
        Code c = {font->prep.data, font->prep.size};
        ex.sp = 0;
        ok = run_program(&ex, c, PROGRAM_PREP);
        if (ok) {
            h->defaults = ex.gs;
            h->instruct_control = ex.gs.instruct_control;
        }
    }
    h->usable = ok && font->fpgm.size + font->prep.size > 0;
    h->glyph_hinting = h->usable && !(h->instruct_control & 1);
    h->scan_control = h->defaults.scan_control;
    h->scan_type = h->defaults.scan_type;
    /* the compatible widths take the advance of the glyph fitted without cleartype. */
    if ((flags & MIME_TTF_HINT_CLEARTYPE) && (flags & MIME_TTF_HINT_COMPAT_WIDTHS) && h->glyph_hinting)
        h->plain = mime_ttf_hinter_new(font, alloc, ppem_x, ppem_y, 0);
    return h;
}

bool mime_ttf_hinter_active(const MimeTtfHinter *h)
{
    return h && h->glyph_hinting;
}

void mime_ttf_hinter_scan(const MimeTtfHinter *h, uint32_t *control, uint32_t *type)
{
    *control = h ? h->scan_control : 0;
    *type = h ? h->scan_type : 0;
}

/* ------------------------------------------------------------------ hinted glyph loading */

typedef struct HGlyph {
    uint32_t n;       /* real points. */
    uint32_t contours;
    V2 *pts;          /* n + 4 with the phantom points, in 26.6. */
    uint8_t *on;
    uint16_t *ends;
    V2 *orus;
    uint8_t *tag;
    V2 *org;
    void *block;
    int32_t pp1x, pp2x;
    int32_t adv_fu, lsb_fu;
    bool hinted_ok;
} HGlyph;

static void hglyph_free(const MimeTtfHinter *h, HGlyph *g)
{
    if (g->block)
        mime_free(&h->alloc, g->block);
    memset(g, 0, sizeof(*g));
}

static bool hglyph_alloc(const MimeTtfHinter *h, HGlyph *g, uint32_t n, uint32_t contours)
{
    uint32_t total = n + 4;
    size_t bytes = (size_t)total * (3 * sizeof(V2) + 1 + 1) + sizeof(V2) * total + ((size_t)contours + 1) * 2;
    uint8_t *b = halloc(h, bytes);
    if (!b)
        return false;
    memset(b, 0, bytes);
    g->block = b;
    g->n = n;
    g->contours = contours;
    g->pts = (V2 *)b;
    g->org = g->pts + total;
    g->orus = g->org + total;
    V2 *after = g->orus + total;
    g->tag = (uint8_t *)after;
    g->on = g->tag + total;
    g->ends = (uint16_t *)(((uintptr_t)(g->on + total) + 1) & ~(uintptr_t)1);
    (void)bytes;
    return true;
}

static int32_t scale_fu_x(const MimeTtfHinter *h, int32_t v)
{
    return scale_apply(&h->sx, v);
}

static int32_t scale_fu_y(const MimeTtfHinter *h, int32_t v)
{
    return scale_apply(&h->sy, v);
}

/* runs the glyph program over a glyph whose points are in place. */
static void run_glyph_program(const MimeTtfHinter *hc, HGlyph *g, Code program, bool composite, int32_t ratio)
{
    MimeTtfHinter *h = (MimeTtfHinter *)hc;
    if (!h->glyph_hinting || program.size == 0)
        return;
    Exec ex;
    GState gs = h->defaults;
    if (h->instruct_control & 2)
        default_state(&gs);
    gs.rp0 = gs.rp1 = gs.rp2 = 0;
    gs.zp0 = gs.zp1 = gs.zp2 = 1;
    gs.dv = gs.pv = gs.fv = (V2){0x4000, 0};
    gs.loop = 1;
    exec_init(&ex, h, &gs);
    ex.composite = composite;
    Zone *z = &ex.zones[1];
    z->org = g->org;
    z->cur = g->pts;
    z->orus = g->orus;
    z->tag = g->tag;
    z->ends = g->ends;
    z->n = g->n + 4;
    z->contours = g->contours;
    ex.step_limit = MAX_GLYPH_STEPS;
    /* the program works on copies, so the tables of the hinter are never written and threads may share it. */
    size_t cvt_bytes = h->cvt_count * sizeof(int32_t), storage_bytes = h->storage_count * sizeof(int32_t);
    size_t twilight_bytes = (size_t)h->twilight.n * (3 * sizeof(V2) + 1), stack_bytes = h->stack_cap * sizeof(int32_t);
    bool phase = (h->flags & MIME_TTF_HINT_CLEARTYPE) && (h->flags & MIME_TTF_HINT_COMPAT_WIDTHS);
    size_t link_bytes = phase ? (size_t)(g->n + 4) * sizeof(PhaseLink) + g->contours + 8 : 0;
    uint8_t *scratch = halloc(h, cvt_bytes + storage_bytes + twilight_bytes + stack_bytes + link_bytes + 32);
    if (!scratch)
        return;
    int32_t *cvt = (int32_t *)scratch;
    int32_t *storage = cvt + h->cvt_count;
    V2 *twilight = (V2 *)(storage + h->storage_count);
    memcpy(cvt, h->cvt, cvt_bytes);
    memcpy(storage, h->storage, storage_bytes);
    memcpy(twilight, h->twilight.org, twilight_bytes);
    uint32_t tn = h->twilight.n;
    ex.cvt = cvt;
    ex.storage = storage;
    ex.zones[0].org = twilight;
    ex.zones[0].cur = twilight + tn;
    ex.zones[0].orus = twilight + 2 * tn;
    ex.zones[0].tag = (uint8_t *)(twilight + 3 * tn);
    ex.stack = (int32_t *)((uint8_t *)twilight + twilight_bytes + 8 - ((uintptr_t)((uint8_t *)twilight + twilight_bytes) & 7));
    ex.stack_cap = h->stack_cap;
    if (phase) {
        uint8_t *after = (uint8_t *)ex.stack + stack_bytes;
        after += (-(uintptr_t)after) & 7;
        ex.links = (PhaseLink *)after;
        for (uint32_t i = 0; i < g->n + 4; i++)
            ex.links[i] = (PhaseLink){-1, -1, -1, 0, 0};
        ex.misoriented = (uint8_t *)(ex.links + g->n + 4);
        find_misoriented(h, z, ex.misoriented);
        ex.phase_scale = ratio;
    }
    bool ok = run_program(&ex, program, PROGRAM_GLYPH);
    if (ok && ex.links && !ex.phase_done)
        phase_run(&ex);
    mime_free(&h->alloc, scratch);
    g->hinted_ok = ok;
    /* a program that fails leaves the glyph as scaled, not half fitted. */
    if (!ok)
        for (uint32_t i = 0; i < g->n + 4; i++)
            g->pts[i] = g->org[i];
}

static bool load_hglyph(const MimeTtfHinter *h, uint16_t glyph, unsigned depth, int32_t ratio, bool hint, HGlyph *out);

static void phantom_points(const MimeTtfHinter *h, uint16_t glyph, MimeTtfTable gd, HGlyph *g, bool has_outline)
{
    const MimeTtfFont *f = h->font;
    int adv, lsb;
    mime_ttf_glyph_hmetrics(f, glyph, &adv, &lsb);
    int xmin = 0, ymax = 0;
    if (gd.size >= 10) {
        xmin = (int16_t)be16(gd.data + 2);
        ymax = (int16_t)be16(gd.data + 8);
    }
    (void)has_outline;
    g->adv_fu = adv;
    g->lsb_fu = lsb;
    uint32_t n = g->n;
    g->orus[n + 0] = (V2){xmin - lsb, 0};
    g->orus[n + 1] = (V2){xmin - lsb + adv, 0};
    int vadv = f->units_per_em, tsb = 0;
    if (f->vhea.size >= 36 && f->vmtx.size >= 4) {
        uint32_t nm = be16(f->vhea.data + 34);
        if (nm == 0)
            nm = 1;
        uint32_t idx = glyph < nm ? glyph : nm - 1;
        if (4ull * idx + 2 <= f->vmtx.size)
            vadv = be16(f->vmtx.data + 4ull * idx);
        if (glyph < nm && 4ull * idx + 4 <= f->vmtx.size)
            tsb = (int16_t)be16(f->vmtx.data + 4ull * idx + 2);
    } else if (f->os2.size >= 72 || f->hhea.size >= 8) {
        /* without vertical metrics the line from the typographic ascender to the descender is used. */
        int asc, desc;
        if (f->os2.size >= 72) {
            asc = (int16_t)be16(f->os2.data + 68);
            desc = (int16_t)be16(f->os2.data + 70);
        } else {
            asc = (int16_t)be16(f->hhea.data + 4);
            desc = (int16_t)be16(f->hhea.data + 6);
        }
        vadv = asc - desc;
        tsb = asc - ymax;
    }
    g->orus[n + 2] = (V2){adv >> 1, ymax + tsb};
    g->orus[n + 3] = (V2){adv >> 1, ymax + tsb - vadv};
}

/* the side bearing points go to a sixteenth of a pixel along x with cleartype and to the pixel without. */
static int32_t round_bearing(const MimeTtfHinter *h, int32_t v)
{
    return (h->flags & MIME_TTF_HINT_CLEARTYPE) ? (v + 2) & ~3 : (v + 32) & ~63;
}

static void scale_and_round_phantoms(const MimeTtfHinter *h, HGlyph *g, bool composite)
{
    uint32_t n = g->n;
    for (uint32_t i = 0; i < 4; i++) {
        g->org[n + i].x = scale_fu_x(h, g->orus[n + i].x);
        g->org[n + i].y = scale_fu_y(h, g->orus[n + i].y);
    }
    /* the glyph moves so that its left bearing point sits on the grid, the original positions follow. */
    int32_t shift = round_bearing(h, g->org[n].x) - g->org[n].x;
    if (shift) {
        for (uint32_t i = composite ? n : 0; i < n + 4; i++)
            g->org[i].x += shift;
    }
    /* the points of a composite are its components assembled, which only the phantom points are not. */
    for (uint32_t i = composite ? n : 0; i < n + 4; i++)
        g->pts[i] = g->org[i];
    int64_t upem = h->font->units_per_em;
    int64_t adv = (int16_t)(g->orus[n + 1].x - g->orus[n].x);
    int64_t wide = (((int64_t)h->ppem_x << 16) * adv + upem / 2) / upem;
    g->pts[n + 1].x = g->pts[n].x + round_bearing(h, (int32_t)((wide + 512) >> 10));
    int64_t tall = (int16_t)(g->orus[n + 3].y - g->orus[n + 2].y);
    int64_t drop = (((int64_t)h->ppem_y << 16) * tall + upem / 2) / upem;
    g->pts[n + 2].y = (g->pts[n + 2].y + 32) & ~63;
    g->pts[n + 3].y = (g->pts[n + 2].y + 32 + (int32_t)((drop + 512) >> 10)) & ~63;
}

static bool load_simple(const MimeTtfHinter *h, uint16_t glyph, MimeTtfTable gd, int contours, int32_t ratio, bool hint,
                        HGlyph *out)
{
    if (contours == 0) {
        if (!hglyph_alloc(h, out, 0, 0))
            return false;
        phantom_points(h, glyph, gd, out, false);
        scale_and_round_phantoms(h, out, false);
        return true;
    }
    uint64_t ends_off = 10;
    if (gd.size < ends_off + 2ull * contours + 2)
        return false;
    uint32_t points = be16(gd.data + ends_off + 2ull * (contours - 1)) + 1u;
    if (points > MAX_POINTS)
        return false;
    uint64_t ins_off = ends_off + 2ull * contours;
    uint32_t ins_len = be16(gd.data + ins_off);
    uint64_t flags_off = ins_off + 2 + ins_len;
    if (flags_off > gd.size)
        return false;
    if (!hglyph_alloc(h, out, points, (uint32_t)contours))
        return false;
    int32_t prev = -1;
    for (int i = 0; i < contours; i++) {
        int32_t e = be16(gd.data + ends_off + 2ull * i);
        if (e <= prev)
            return false;
        out->ends[i] = (uint16_t)e;
        prev = e;
    }
    /* flags. */
    uint64_t p = flags_off;
    uint8_t *fl = out->on;
    for (uint32_t i = 0; i < points;) {
        if (p >= gd.size)
            return false;
        uint8_t f = gd.data[p++];
        uint32_t run = 1;
        if (f & 8) {
            if (p >= gd.size)
                return false;
            run += gd.data[p++];
        }
        for (; run && i < points; run--)
            fl[i++] = f;
    }
    int32_t x = 0;
    for (uint32_t i = 0; i < points; i++) {
        uint8_t f = fl[i];
        if (f & 2) {
            if (p >= gd.size)
                return false;
            int v = gd.data[p++];
            x += (f & 16) ? v : -v;
        } else if (!(f & 16)) {
            if (p + 2 > gd.size)
                return false;
            x += (int16_t)be16(gd.data + p);
            p += 2;
        }
        out->orus[i].x = x;
    }
    int32_t y = 0;
    for (uint32_t i = 0; i < points; i++) {
        uint8_t f = fl[i];
        if (f & 4) {
            if (p >= gd.size)
                return false;
            int v = gd.data[p++];
            y += (f & 32) ? v : -v;
        } else if (!(f & 32)) {
            if (p + 2 > gd.size)
                return false;
            y += (int16_t)be16(gd.data + p);
            p += 2;
        }
        out->orus[i].y = y;
    }
    for (uint32_t i = 0; i < points; i++) {
        out->tag[i] = fl[i] & TAG_ON;
        out->on[i] = fl[i] & 1;
        out->org[i].x = scale_fu_x(h, out->orus[i].x);
        out->org[i].y = scale_fu_y(h, out->orus[i].y);
        out->pts[i] = out->org[i];
    }
    phantom_points(h, glyph, gd, out, true);
    scale_and_round_phantoms(h, out, false);
    Code prog = {gd.data + ins_off + 2, ins_len};
    if (hint && ins_off + 2 + ins_len <= gd.size)
        run_glyph_program(h, out, prog, false, ratio);
    return true;
}

static bool load_composite(const MimeTtfHinter *h, uint16_t glyph, MimeTtfTable gd, unsigned depth, int32_t ratio, bool hint,
                           HGlyph *out)
{
    if (depth >= MAX_COMPONENT_DEPTH)
        return false;
    typedef struct Part {
        HGlyph g;
        int32_t dx, dy;
    } Part;
    enum { MAX_PARTS = 64 };
    Part *parts = halloc(h, sizeof(Part) * MAX_PARTS);
    if (!parts)
        return false;
    memset(parts, 0, sizeof(Part) * MAX_PARTS);
    uint32_t nparts = 0, total_points = 0, total_contours = 0;
    bool ok = false;
    uint64_t off = 10;
    uint32_t flags;
    int32_t metrics_from = -1;
    uint32_t last_flags = 0;
    do {
        if (nparts >= MAX_PARTS || off + 4 > gd.size)
            goto done;
        flags = be16(gd.data + off);
        last_flags = flags;
        uint16_t child = (uint16_t)be16(gd.data + off + 2);
        off += 4;
        int32_t a1, a2;
        if (flags & 1) {
            if (off + 4 > gd.size)
                goto done;
            a1 = (flags & 2) ? (int16_t)be16(gd.data + off) : (int32_t)be16(gd.data + off);
            a2 = (flags & 2) ? (int16_t)be16(gd.data + off + 2) : (int32_t)be16(gd.data + off + 2);
            off += 4;
        } else {
            if (off + 2 > gd.size)
                goto done;
            a1 = (flags & 2) ? (int8_t)gd.data[off] : (int32_t)gd.data[off];
            a2 = (flags & 2) ? (int8_t)gd.data[off + 1] : (int32_t)gd.data[off + 1];
            off += 2;
        }
        int32_t m[4] = {0x4000, 0, 0, 0x4000};
        bool transformed = false;
        if (flags & 8) {
            if (off + 2 > gd.size)
                goto done;
            m[0] = m[3] = (int16_t)be16(gd.data + off);
            off += 2;
            transformed = true;
        } else if (flags & 0x40) {
            if (off + 4 > gd.size)
                goto done;
            m[0] = (int16_t)be16(gd.data + off);
            m[3] = (int16_t)be16(gd.data + off + 2);
            off += 4;
            transformed = true;
        } else if (flags & 0x80) {
            if (off + 8 > gd.size)
                goto done;
            m[0] = (int16_t)be16(gd.data + off);
            m[1] = (int16_t)be16(gd.data + off + 2);
            m[2] = (int16_t)be16(gd.data + off + 4);
            m[3] = (int16_t)be16(gd.data + off + 6);
            off += 8;
            transformed = true;
        }
        Part *part = &parts[nparts];
        /* a component that is scaled or turned is not fitted to the grid, only the ones a matrix does not change are. */
        bool fitted = hint && (!transformed || (m[1] == 0 && m[2] == 0 && __builtin_abs(m[0]) == 0x4000 && __builtin_abs(m[3]) == 0x4000));
        if (!load_hglyph(h, child, depth + 1, ratio, fitted, &part->g))
            goto done;
        HGlyph *pg = &part->g;
        if (transformed) {
            for (uint32_t i = 0; i < pg->n; i++) {
                int32_t x = pg->pts[i].x, y = pg->pts[i].y;
                pg->pts[i].x = mul_div(x, m[0], 0x4000) + mul_div(y, m[2], 0x4000);
                pg->pts[i].y = mul_div(x, m[1], 0x4000) + mul_div(y, m[3], 0x4000);
                x = pg->orus[i].x;
                y = pg->orus[i].y;
                pg->orus[i].x = mul_div(x, m[0], 0x4000) + mul_div(y, m[2], 0x4000);
                pg->orus[i].y = mul_div(x, m[1], 0x4000) + mul_div(y, m[3], 0x4000);
            }
        }
        if (flags & 0x200)
            metrics_from = (int32_t)nparts;
        if (flags & 2) {
            int32_t dx = scale_fu_x(h, a1), dy = scale_fu_y(h, a2);
            /* an offset that scales with the component is made larger by the larger entry of each row of the matrix. */
            if ((flags & 0x800) && !(flags & 0x1000) && transformed) {
                int32_t sx = __builtin_abs(m[0]) > __builtin_abs(m[1]) ? __builtin_abs(m[0]) : __builtin_abs(m[1]);
                int32_t sy = __builtin_abs(m[2]) > __builtin_abs(m[3]) ? __builtin_abs(m[2]) : __builtin_abs(m[3]);
                if (sx != 0x4000)
                    dx = mul_div(dx, sx, 0x4000);
                if (sy != 0x4000)
                    dy = mul_div(dy, sy, 0x4000);
            }
            /* along x the grid is a sixteenth of a pixel with cleartype. */
            if (flags & 4) {
                dx = round_bearing(h, dx);
                dy = (dy + 32) & ~63;
            }
            part->dx = dx;
            part->dy = dy;
        } else {
            /* point matching, the first point is in the composite so far. */
            uint32_t acc = 0;
            bool found1 = false;
            int32_t px = 0, py = 0;
            for (uint32_t k = 0; k < nparts && !found1; k++) {
                if ((uint32_t)a1 < acc + parts[k].g.n) {
                    px = parts[k].g.pts[a1 - acc].x + parts[k].dx;
                    py = parts[k].g.pts[a1 - acc].y + parts[k].dy;
                    found1 = true;
                }
                acc += parts[k].g.n;
            }
            if (!found1 || (uint32_t)a2 >= pg->n)
                goto done;
            part->dx = px - pg->pts[a2].x;
            part->dy = py - pg->pts[a2].y;
        }
        total_points += pg->n;
        total_contours += pg->contours;
        nparts++;
    } while (flags & 0x20);
    {
        if (total_points > MAX_POINTS)
            goto done;
        if (!hglyph_alloc(h, out, total_points, total_contours))
            goto done;
        uint32_t pi = 0, ci = 0;
        for (uint32_t k = 0; k < nparts; k++) {
            HGlyph *pg = &parts[k].g;
            for (uint32_t i = 0; i < pg->n; i++) {
                out->pts[pi + i].x = pg->pts[i].x + parts[k].dx;
                out->pts[pi + i].y = pg->pts[i].y + parts[k].dy;
                out->orus[pi + i] = pg->orus[i];
                out->tag[pi + i] = pg->tag[i] & TAG_ON;
                out->on[pi + i] = pg->on[i];
            }
            for (uint32_t c = 0; c < pg->contours; c++)
                out->ends[ci + c] = (uint16_t)(pg->ends[c] + pi);
            pi += pg->n;
            ci += pg->contours;
        }
        /* the composite's own phantom points, or those of the component that asked to be used. */
        phantom_points(h, glyph, gd, out, true);
        scale_and_round_phantoms(h, out, true);
        if (metrics_from >= 0) {
            HGlyph *mg = &parts[metrics_from].g;
            for (uint32_t i = 0; i < 4; i++) {
                out->pts[out->n + i] = mg->pts[mg->n + i];
                out->org[out->n + i] = mg->org[mg->n + i];
                out->orus[out->n + i] = mg->orus[mg->n + i];
            }
            out->adv_fu = mg->adv_fu;
            out->lsb_fu = mg->lsb_fu;
        }
        /* the original positions of the composite are the assembled hinted components. */
        for (uint32_t i = 0; i < out->n; i++)
            out->org[i] = out->pts[i];
        if (hint && (last_flags & 0x100)) {
            if (off + 2 <= gd.size) {
                uint32_t ilen = be16(gd.data + off);
                if (off + 2 + ilen <= gd.size) {
                    Code prog = {gd.data + off + 2, ilen};
                    run_glyph_program(h, out, prog, true, ratio);
                }
            }
        }
        ok = true;
    }
done:
    for (uint32_t k = 0; k <= nparts && k < MAX_PARTS; k++)
        hglyph_free(h, &parts[k].g);
    mime_free(&h->alloc, parts);
    return ok;
}

static bool load_hglyph(const MimeTtfHinter *h, uint16_t glyph, unsigned depth, int32_t ratio, bool hint, HGlyph *out)
{
    memset(out, 0, sizeof(*out));
    MimeTtfTable gd;
    if (!mime_ttf_glyph_data(h->font, glyph, &gd))
        return false;
    bool ok;
    int contours = gd.size >= 10 ? (int16_t)be16(gd.data) : 0;
    if (gd.size == 0)
        ok = load_simple(h, glyph, gd, 0, ratio, hint, out);
    else if (gd.size < 10)
        ok = false;
    else if (contours >= 0)
        ok = load_simple(h, glyph, gd, contours, ratio, hint, out);
    else
        ok = load_composite(h, glyph, gd, depth, ratio, hint, out);
    if (!ok)
        hglyph_free(h, out);
    return ok;
}

/* ------------------------------------------------------------------ public glyph access */

/* the advance the compatible widths give a glyph over the scaled one, in 16.16, the program's phase control uses it. */
static int32_t phase_ratio(const MimeTtfHinter *h, uint16_t glyph)
{
    if (!h->plain)
        return 0x10000;
    int adv_fu;
    mime_ttf_glyph_hmetrics(h->font, glyph, &adv_fu, nullptr);
    int32_t fitted;
    if (adv_fu == 0 || !mime_ttf_hint_advance(h->plain, glyph, &fitted) || fitted == 0)
        return 0x10000;
    int32_t scaled = scale_apply(&h->sx, adv_fu);
    if (scaled == 0)
        return 0x10000;
    int64_t num = (int64_t)fitted << 16, half = scaled / 2;
    int64_t q = ((scaled < 0) == (num < 0) ? num + half : num - half) / scaled;
    q = q < 0 ? -q : q;
    return q > INT32_MAX ? INT32_MAX : (int32_t)q;
}

bool mime_ttf_hint_advance(const MimeTtfHinter *h, uint16_t glyph, int32_t *advance)
{
    HGlyph g;
    if (h && h->plain)
        return mime_ttf_hint_advance(h->plain, glyph, advance);
    if (!h || !load_hglyph(h, glyph, 0, 0x10000, true, &g))
        return false;
    int32_t adv = g.pts[g.n + 1].x - g.pts[g.n].x;
    hglyph_free(h, &g);
    *advance = adv;
    return true;
}

bool mime_ttf_hint_walk(const MimeTtfHinter *h, uint16_t glyph, const MimeTtfSink *sink)
{
    HGlyph g;
    if (!h || !sink || !load_hglyph(h, glyph, 0, phase_ratio(h, glyph), true, &g))
        return false;
    int32_t shift = g.pts[g.n].x;
    uint32_t first = 0;
    for (uint32_t c = 0; c < g.contours; c++) {
        for (uint32_t i = first; i <= g.ends[c]; i++)
            sink->point(sink->ctx, (float)(g.pts[i].x - shift) / 64.0f, (float)g.pts[i].y / 64.0f, g.on[i] != 0);
        sink->end(sink->ctx);
        first = (uint32_t)g.ends[c] + 1u;
    }
    hglyph_free(h, &g);
    return true;
}
