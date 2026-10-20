/*
 * regions: the Region of the flat api. a region is a tree of rectangles and paths joined by set operations,
 * kept in world units. it turns into pixels, a mask, only when a graphics or a matrix says what a unit is, and
 * the pixels are the ones whose sample point the shapes cover, the way a non antialiased fill has them.
 *
 * the data of a region is a list of nodes in prefix order: an operation, then its left and right nodes, with the
 * rectangle, path, empty and infinite leaves. around it go the magic and the number of nodes below the root, and
 * for the api a size and a crc.
 */
#include "gp_private.h"

#define INFINITE_LIMIT 4194304.0f
#define REGION_MAGIC 0xdbc01002u
#define NODE_RECT 0x10000000u
#define NODE_PATH 0x10000001u
#define NODE_EMPTY 0x10000002u
#define NODE_INFINITE 0x10000003u
#define PATH_MAGIC 0xdbc01001u

GRegion *gp_region_cast(const void *p)
{
    return GP_CAST(GRegion, GP_TAG_REGION, p);
}

/* ------------------------------------------------------------------ the tree */

static GRegionNode *node_new(GRegionKind kind)
{
    GRegionNode *n = gp_alloc_zero(sizeof(*n));
    if (n)
        n->kind = kind;
    return n;
}

static void node_free(GRegionNode *n)
{
    if (!n)
        return;
    node_free(n->left);
    node_free(n->right);
    gp_path_free(n->path);
    gp_free(n);
}

static GRegionNode *node_clone(const GRegionNode *s)
{
    if (!s)
        return nullptr;
    GRegionNode *n = gp_alloc(sizeof(*n));
    if (!n)
        return nullptr;
    *n = *s;
    n->left = n->right = nullptr;
    n->path = nullptr;
    if (s->path) {
        n->path = gp_path_clone(s->path);
        if (!n->path) {
            gp_free(n);
            return nullptr;
        }
    }
    if (s->left && !(n->left = node_clone(s->left))) {
        node_free(n);
        return nullptr;
    }
    if (s->right && !(n->right = node_clone(s->right))) {
        node_free(n);
        return nullptr;
    }
    return n;
}

static GRegion *region_new(GRegionNode *root)
{
    GRegion *r = gp_alloc(sizeof(*r));
    if (!r)
        return nullptr;
    r->tag = GP_TAG_REGION;
    r->root = root;
    return r;
}

GRegion *gp_region_new_infinite(void)
{
    GRegionNode *n = node_new(GREGION_INFINITE);
    GRegion *r = n ? region_new(n) : nullptr;
    if (!r)
        node_free(n);
    return r;
}

GRegion *gp_region_clone(const GRegion *s)
{
    GRegionNode *n = node_clone(s->root);
    GRegion *r = n ? region_new(n) : nullptr;
    if (!r)
        node_free(n);
    return r;
}

void gp_region_free(GRegion *r)
{
    if (!r)
        return;
    node_free(r->root);
    r->tag = 0;
    gp_free(r);
}

static GpStatus region_set_root(GRegion *r, GRegionNode *n)
{
    if (!n)
        return OutOfMemory;
    node_free(r->root);
    r->root = n;
    return Ok;
}

static GRegionNode *rect_node(float x, float y, float w, float h)
{
    GRegionNode *n = node_new(GREGION_RECT);
    if (n) {
        n->rect.X = x;
        n->rect.Y = y;
        n->rect.Width = w;
        n->rect.Height = h;
    }
    return n;
}

static GRegionNode *path_node(const GPath *p)
{
    GRegionNode *n = node_new(GREGION_PATH);
    if (n) {
        n->path = gp_path_clone(p);
        if (!n->path) {
            gp_free(n);
            return nullptr;
        }
    }
    return n;
}

/* ------------------------------------------------------------------ pixels */

typedef struct MaskBuild {
    MimeVecMask *mask;
    INT left, right;
    UINT8 *row;
    INT y;
    INT lo, hi;
    BOOL has;
    int err;
} MaskBuild;

static void build_flush(MaskBuild *b)
{
    if (!b->has)
        return;
    if (!b->err)
        b->err = mime_vec_mask_add_row(b->mask, b->y, b->lo, b->hi - b->lo, b->row + (b->lo - b->left));
    memset(b->row + (b->lo - b->left), 0, (SIZE_T)(b->hi - b->lo));
    b->has = FALSE;
}

static void build_span(void *user, INT y, INT x0, INT count, const UINT8 *cov, UINT cmax)
{
    MaskBuild *b = user;
    if (b->has && y != b->y)
        build_flush(b);
    if (!b->has) {
        b->y = y;
        b->lo = x0;
        b->hi = x0 + count;
        b->has = TRUE;
    } else {
        if (x0 < b->lo)
            b->lo = x0;
        if (x0 + count > b->hi)
            b->hi = x0 + count;
    }
    for (INT i = 0; i < count; i++)
        b->row[x0 + i - b->left] = cmax <= 1 ? 255 : (UINT8)((cov[i] * 255u + cmax / 2) / cmax);
}

/* the mask of polygons filled without antialiasing, inside the clip. */
static int mask_from_polys(MimeVecMask *m, const MimeVecPolys *polys, INT rule, const MimeVecRect *clip)
{
    mime_vec_mask_clear(m);
    if (clip->right <= clip->left || clip->bottom <= clip->top || !polys->npts)
        return MIME_VEC_OK;
    MaskBuild b;
    memset(&b, 0, sizeof(b));
    b.mask = m;
    b.left = clip->left;
    b.right = clip->right;
    b.row = gp_alloc_zero((SIZE_T)(clip->right - clip->left) + 1);
    if (!b.row)
        return MIME_VEC_NOMEM;
    RECT rc = {clip->left, clip->top, clip->right, clip->bottom};
    GpStatus st = gp_raster(polys, rule, 0, &rc, build_span, &b);
    build_flush(&b);
    gp_free(b.row);
    if (st != Ok)
        return MIME_VEC_NOMEM;
    if (b.err)
        return b.err;
    mime_vec_mask_finish(m);
    return MIME_VEC_OK;
}

static int mask_from_rect_poly(MimeVecMask *m, float x, float y, float w, float h, const MimeVecMatrix *mat,
                               const MimeVecRect *clip)
{
    MimeVecPoint pts[4] = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
    MimeVecPolys polys;
    mime_vec_polys_init(&polys, &gp_allocator);
    for (INT i = 0; i < 4; i++)
        pts[i] = mime_vec_xform(mat, pts[i].x, pts[i].y);
    int e = mime_vec_polys_add_sub(&polys, pts, 4, true);
    if (!e)
        e = mask_from_polys(m, &polys, MIME_VEC_FILL_WINDING, clip);
    mime_vec_polys_free(&polys);
    return e;
}

static GpStatus status_of(int e) { return e == MIME_VEC_OK ? Ok : (e == MIME_VEC_INVALID ? InvalidParameter : OutOfMemory); }

static GpStatus node_mask(const GRegionNode *n, const MimeVecMatrix *m, const MimeVecRect *clip, MimeVecMask *out)
{
    switch (n->kind) {
    case GREGION_INFINITE:
        return status_of(mime_vec_mask_set_rect(out, clip));
    case GREGION_EMPTY:
        mime_vec_mask_clear(out);
        return Ok;
    case GREGION_RECT:
        return status_of(mask_from_rect_poly(out, n->rect.X, n->rect.Y, n->rect.Width, n->rect.Height, m, clip));
    case GREGION_PATH: {
        MimeVecPolys polys;
        mime_vec_polys_init(&polys, &gp_allocator);
        GpStatus st = gp_path_flatten(&n->path->path, m, 0.25f, &polys);
        if (st == Ok)
            st = status_of(mask_from_polys(out, &polys,
                                           n->path->fill_mode == FillModeWinding ? MIME_VEC_FILL_WINDING
                                                                                  : MIME_VEC_FILL_ALTERNATE,
                                           clip));
        mime_vec_polys_free(&polys);
        return st;
    }
    default: {
        MimeVecMask a, b;
        mime_vec_mask_init(&a, &gp_allocator);
        mime_vec_mask_init(&b, &gp_allocator);
        GpStatus st = node_mask(n->left, m, clip, &a);
        if (st == Ok)
            st = node_mask(n->right, m, clip, &b);
        if (st == Ok)
            st = status_of(mime_vec_mask_combine(out, &a, &b, (int)n->op));
        mime_vec_mask_free(&a);
        mime_vec_mask_free(&b);
        return st;
    }
    }
}

GpStatus gp_region_to_mask(const GRegion *r, const MimeVecMatrix *m, const MimeVecRect *clip, MimeVecMask *mask)
{
    return node_mask(r->root, m, clip, mask);
}

GpStatus gp_mask_from_polys(MimeVecMask *m, const MimeVecPolys *polys, INT rule, const MimeVecRect *clip)
{
    return status_of(mask_from_polys(m, polys, rule, clip));
}

/* the pixels the region may cover, for the regions that have bounds. */
static BOOL node_bounds(const GRegionNode *n, const MimeVecMatrix *m, MimeVecBounds *b, BOOL *infinite)
{
    switch (n->kind) {
    case GREGION_INFINITE:
        *infinite = TRUE;
        return FALSE;
    case GREGION_EMPTY:
        return FALSE;
    case GREGION_RECT: {
        MimeVecPoint p[4] = {{n->rect.X, n->rect.Y},
                             {n->rect.X + n->rect.Width, n->rect.Y},
                             {n->rect.X + n->rect.Width, n->rect.Y + n->rect.Height},
                             {n->rect.X, n->rect.Y + n->rect.Height}};
        b->left = b->right = mime_vec_xform(m, p[0].x, p[0].y).x;
        b->top = b->bottom = mime_vec_xform(m, p[0].x, p[0].y).y;
        for (INT i = 1; i < 4; i++) {
            MimeVecPoint q = mime_vec_xform(m, p[i].x, p[i].y);
            if (q.x < b->left)
                b->left = q.x;
            if (q.x > b->right)
                b->right = q.x;
            if (q.y < b->top)
                b->top = q.y;
            if (q.y > b->bottom)
                b->bottom = q.y;
        }
        return TRUE;
    }
    case GREGION_PATH: {
        MimeVecPolys polys;
        mime_vec_polys_init(&polys, &gp_allocator);
        BOOL have = gp_path_flatten(&n->path->path, m, 0.25f, &polys) == Ok && mime_vec_polys_bounds(&polys, b);
        mime_vec_polys_free(&polys);
        return have;
    }
    default: {
        MimeVecBounds lb, rb;
        BOOL linf = FALSE, rinf = FALSE;
        BOOL l = node_bounds(n->left, m, &lb, &linf), r = node_bounds(n->right, m, &rb, &rinf);
        BOOL inf = linf || rinf;
        switch ((CombineMode)n->op) {
        case CombineModeIntersect:
            /* bounded when either side is */
            if (!l && !linf)
                return FALSE;
            if (!r && !rinf)
                return FALSE;
            if (l && r) {
                b->left = lb.left > rb.left ? lb.left : rb.left;
                b->top = lb.top > rb.top ? lb.top : rb.top;
                b->right = lb.right < rb.right ? lb.right : rb.right;
                b->bottom = lb.bottom < rb.bottom ? lb.bottom : rb.bottom;
                return b->right > b->left && b->bottom > b->top;
            }
            if (l) {
                *b = lb;
                return TRUE;
            }
            if (r) {
                *b = rb;
                return TRUE;
            }
            *infinite = TRUE;
            return FALSE;
        case CombineModeExclude:
            *infinite = linf;
            if (l) {
                *b = lb;
                return TRUE;
            }
            return FALSE;
        case CombineModeComplement:
            *infinite = rinf;
            if (r) {
                *b = rb;
                return TRUE;
            }
            return FALSE;
        default:
            if (inf) {
                *infinite = TRUE;
                return FALSE;
            }
            if (l && r) {
                b->left = lb.left < rb.left ? lb.left : rb.left;
                b->top = lb.top < rb.top ? lb.top : rb.top;
                b->right = lb.right > rb.right ? lb.right : rb.right;
                b->bottom = lb.bottom > rb.bottom ? lb.bottom : rb.bottom;
                return TRUE;
            }
            if (l) {
                *b = lb;
                return TRUE;
            }
            if (r) {
                *b = rb;
                return TRUE;
            }
            return FALSE;
        }
    }
    }
}

/* a device mask of the region over its own pixels, or over the limit when it has no bounds. */
static GpStatus region_mask_own(const GRegion *r, const MimeVecMatrix *m, MimeVecMask *mask, BOOL *infinite)
{
    MimeVecBounds b;
    *infinite = FALSE;
    BOOL have = node_bounds(r->root, m, &b, infinite);
    if (!have) {
        mime_vec_mask_clear(mask);
        return Ok;
    }
    MimeVecRect clip;
    clip.left = (INT)mime_vec_floorf(b.left) - 1;
    clip.top = (INT)mime_vec_floorf(b.top) - 1;
    clip.right = (INT)mime_vec_ceilf(b.right) + 2;
    clip.bottom = (INT)mime_vec_ceilf(b.bottom) + 2;
    if (clip.right - clip.left > 1 << 22 || clip.bottom - clip.top > 1 << 22)
        return OutOfMemory;
    return node_mask(r->root, m, &clip, mask);
}

/* ------------------------------------------------------------------ creation */

WINGDIPAPI GpStatus WINAPI GdipCreateRegion(GpRegion **region)
{
    GP_REQUIRE_INIT();
    if (!region)
        return InvalidParameter;
    GRegion *r = gp_region_new_infinite();
    *region = r;
    return r ? Ok : OutOfMemory;
}

WINGDIPAPI GpStatus WINAPI GdipCreateRegionRect(const GpRectF *rect, GpRegion **region)
{
    GP_REQUIRE_INIT();
    if (!rect || !region)
        return InvalidParameter;
    GRegionNode *n = rect_node(rect->X, rect->Y, rect->Width, rect->Height);
    GRegion *r = n ? region_new(n) : nullptr;
    if (!r)
        node_free(n);
    *region = r;
    return r ? Ok : OutOfMemory;
}

WINGDIPAPI GpStatus WINAPI GdipCreateRegionRectI(const GpRect *rect, GpRegion **region)
{
    GP_REQUIRE_INIT();
    if (!rect)
        return InvalidParameter;
    GpRectF r = {(float)rect->X, (float)rect->Y, (float)rect->Width, (float)rect->Height};
    return GdipCreateRegionRect(&r, region);
}

WINGDIPAPI GpStatus WINAPI GdipCreateRegionPath(GpPath *path, GpRegion **region)
{
    GP_REQUIRE_INIT();
    GPath *p = gp_path_cast(path);
    if (!p || !region)
        return InvalidParameter;
    GRegionNode *n = path_node(p);
    GRegion *r = n ? region_new(n) : nullptr;
    if (!r)
        node_free(n);
    *region = r;
    return r ? Ok : OutOfMemory;
}

/* the rectangles of a gdi region, as a union of rectangle nodes. */
WINGDIPAPI GpStatus WINAPI GdipCreateRegionHrgn(HRGN hrgn, GpRegion **region)
{
    GP_REQUIRE_INIT();
    if (!hrgn || !region)
        return InvalidParameter;
    DWORD size = GetRegionData(hrgn, 0, nullptr);
    if (!size)
        return InvalidParameter;
    RGNDATA *data = gp_alloc(size);
    if (!data)
        return OutOfMemory;
    if (!GetRegionData(hrgn, size, data)) {
        gp_free(data);
        return InvalidParameter;
    }
    const RECT *rects = (const RECT *)data->Buffer;
    GRegionNode *root = nullptr;
    for (DWORD i = 0; i < data->rdh.nCount; i++) {
        GRegionNode *n = rect_node((float)rects[i].left, (float)rects[i].top, (float)(rects[i].right - rects[i].left),
                                   (float)(rects[i].bottom - rects[i].top));
        if (!n) {
            node_free(root);
            gp_free(data);
            return OutOfMemory;
        }
        if (!root) {
            root = n;
        } else {
            GRegionNode *op = node_new(GREGION_OP);
            if (!op) {
                node_free(n);
                node_free(root);
                gp_free(data);
                return OutOfMemory;
            }
            op->op = CombineModeUnion;
            op->left = root;
            op->right = n;
            root = op;
        }
    }
    gp_free(data);
    if (!root)
        root = node_new(GREGION_EMPTY);
    GRegion *r = root ? region_new(root) : nullptr;
    if (!r) {
        node_free(root);
        return OutOfMemory;
    }
    *region = r;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipCloneRegion(GpRegion *region, GpRegion **clone)
{
    GP_REQUIRE_INIT();
    GRegion *r = gp_region_cast(region);
    if (!r || !clone)
        return InvalidParameter;
    GRegion *c = gp_region_clone(r);
    *clone = c;
    return c ? Ok : OutOfMemory;
}

WINGDIPAPI GpStatus WINAPI GdipDeleteRegion(GpRegion *region)
{
    GRegion *r = gp_region_cast(region);
    if (!r)
        return InvalidParameter;
    gp_region_free(r);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipSetInfinite(GpRegion *region)
{
    GRegion *r = gp_region_cast(region);
    if (!r)
        return InvalidParameter;
    return region_set_root(r, node_new(GREGION_INFINITE));
}

WINGDIPAPI GpStatus WINAPI GdipSetEmpty(GpRegion *region)
{
    GRegion *r = gp_region_cast(region);
    if (!r)
        return InvalidParameter;
    return region_set_root(r, node_new(GREGION_EMPTY));
}

/* ------------------------------------------------------------------ combining */

/* joins the tree of the region with a new node. */
static GpStatus region_combine_node(GRegion *r, GRegionNode *other, CombineMode mode)
{
    if (!other)
        return OutOfMemory;
    if (mode == CombineModeReplace) {
        return region_set_root(r, other);
    }
    GRegionNode *op = node_new(GREGION_OP);
    if (!op) {
        node_free(other);
        return OutOfMemory;
    }
    op->op = mode;
    op->left = r->root;
    op->right = other;
    r->root = op;
    return Ok;
}

static BOOL combine_valid(CombineMode m) { return m >= CombineModeReplace && m <= CombineModeComplement; }

WINGDIPAPI GpStatus WINAPI GdipCombineRegionRect(GpRegion *region, const GpRectF *rect, CombineMode mode)
{
    GRegion *r = gp_region_cast(region);
    if (!r || !rect || !combine_valid(mode))
        return InvalidParameter;
    return region_combine_node(r, rect_node(rect->X, rect->Y, rect->Width, rect->Height), mode);
}

WINGDIPAPI GpStatus WINAPI GdipCombineRegionRectI(GpRegion *region, const GpRect *rect, CombineMode mode)
{
    if (!rect)
        return InvalidParameter;
    GpRectF r = {(float)rect->X, (float)rect->Y, (float)rect->Width, (float)rect->Height};
    return GdipCombineRegionRect(region, &r, mode);
}

WINGDIPAPI GpStatus WINAPI GdipCombineRegionPath(GpRegion *region, GpPath *path, CombineMode mode)
{
    GRegion *r = gp_region_cast(region);
    GPath *p = gp_path_cast(path);
    if (!r || !p || !combine_valid(mode))
        return InvalidParameter;
    return region_combine_node(r, path_node(p), mode);
}

WINGDIPAPI GpStatus WINAPI GdipCombineRegionRegion(GpRegion *region, GpRegion *other, CombineMode mode)
{
    GRegion *r = gp_region_cast(region), *o = gp_region_cast(other);
    if (!r || !o || !combine_valid(mode))
        return InvalidParameter;
    return region_combine_node(r, node_clone(o->root), mode);
}

/* ------------------------------------------------------------------ moving */

static GpStatus node_transform(GRegionNode *n, const MimeVecMatrix *m)
{
    switch (n->kind) {
    case GREGION_RECT:
        if (m->m12 == 0.0f && m->m21 == 0.0f) {
            float x0 = n->rect.X * m->m11 + m->dx, x1 = (n->rect.X + n->rect.Width) * m->m11 + m->dx;
            float y0 = n->rect.Y * m->m22 + m->dy, y1 = (n->rect.Y + n->rect.Height) * m->m22 + m->dy;
            n->rect.X = x0 < x1 ? x0 : x1;
            n->rect.Y = y0 < y1 ? y0 : y1;
            n->rect.Width = x0 < x1 ? x1 - x0 : x0 - x1;
            n->rect.Height = y0 < y1 ? y1 - y0 : y0 - y1;
            return Ok;
        } else {
            /* a rectangle that is turned or sheared is a polygon from then on */
            GPath *p = gp_path_new(FillModeWinding);
            if (!p)
                return OutOfMemory;
            if (mime_vec_path_add_rect(&p->path, n->rect.X, n->rect.Y, n->rect.Width, n->rect.Height) != MIME_VEC_OK) {
                gp_path_free(p);
                return OutOfMemory;
            }
            mime_vec_path_transform(&p->path, m);
            n->kind = GREGION_PATH;
            n->path = p;
            return Ok;
        }
    case GREGION_PATH:
        mime_vec_path_transform(&n->path->path, m);
        return Ok;
    case GREGION_OP: {
        GpStatus st = node_transform(n->left, m);
        return st == Ok ? node_transform(n->right, m) : st;
    }
    default:
        return Ok;
    }
}

WINGDIPAPI GpStatus WINAPI GdipTranslateRegion(GpRegion *region, REAL dx, REAL dy)
{
    GRegion *r = gp_region_cast(region);
    if (!r)
        return InvalidParameter;
    MimeVecMatrix m = {1, 0, 0, 1, dx, dy};
    return node_transform(r->root, &m);
}

WINGDIPAPI GpStatus WINAPI GdipTranslateRegionI(GpRegion *region, INT dx, INT dy)
{
    return GdipTranslateRegion(region, (float)dx, (float)dy);
}

WINGDIPAPI GpStatus WINAPI GdipTransformRegion(GpRegion *region, GpMatrix *matrix)
{
    GRegion *r = gp_region_cast(region);
    GMatrix *m = gp_matrix_cast(matrix);
    if (!r || !m)
        return InvalidParameter;
    return node_transform(r->root, &m->m);
}

/* ------------------------------------------------------------------ queries */

static const MimeVecMatrix *graphics_matrix(GpGraphics *graphics, MimeVecMatrix *identity)
{
    GGraphics *g = gp_graphics_cast(graphics);
    if (g)
        return gp_graphics_w2d(g);
    mime_vec_matrix_identity(identity);
    return identity;
}

static BOOL bounds_graphics_ok(GpGraphics *graphics) { return !graphics || gp_graphics_cast(graphics); }

static BOOL single_rect(const GRegionNode *n) { return n->kind == GREGION_RECT; }

WINGDIPAPI GpStatus WINAPI GdipGetRegionBounds(GpRegion *region, GpGraphics *graphics, GpRectF *rect)
{
    GRegion *r = gp_region_cast(region);
    if (!r || !rect || !bounds_graphics_ok(graphics))
        return InvalidParameter;
    MimeVecMatrix id;
    const MimeVecMatrix *m = graphics_matrix(graphics, &id);
    if (r->root->kind == GREGION_INFINITE) {
        rect->X = rect->Y = -INFINITE_LIMIT;
        rect->Width = rect->Height = 2.0f * INFINITE_LIMIT;
        return Ok;
    }
    if (r->root->kind == GREGION_EMPTY) {
        memset(rect, 0, sizeof(*rect));
        return Ok;
    }
    if (single_rect(r->root) && (!gp_graphics_cast(graphics) || (m->m12 == 0 && m->m21 == 0))) {
        *rect = r->root->rect;
        return Ok;
    }
    MimeVecMask mask;
    mime_vec_mask_init(&mask, &gp_allocator);
    BOOL infinite;
    GpStatus st = region_mask_own(r, m, &mask, &infinite);
    if (st == Ok) {
        if (infinite) {
            rect->X = rect->Y = -INFINITE_LIMIT;
            rect->Width = rect->Height = 2.0f * INFINITE_LIMIT;
        } else {
            MimeVecRect b = mime_vec_mask_bounds(&mask);
            if (mime_vec_mask_is_empty(&mask)) {
                memset(rect, 0, sizeof(*rect));
            } else {
                /* back from the device to the world of the graphics */
                MimeVecMatrix inv;
                float x0 = (float)b.left, y0 = (float)b.top, x1 = (float)b.right, y1 = (float)b.bottom;
                if (gp_graphics_cast(graphics) && mime_vec_matrix_invert(&inv, m)) {
                    MimeVecPoint a = mime_vec_xform(&inv, x0, y0), c = mime_vec_xform(&inv, x1, y1);
                    x0 = a.x;
                    y0 = a.y;
                    x1 = c.x;
                    y1 = c.y;
                }
                rect->X = x0 < x1 ? x0 : x1;
                rect->Y = y0 < y1 ? y0 : y1;
                rect->Width = x0 < x1 ? x1 - x0 : x0 - x1;
                rect->Height = y0 < y1 ? y1 - y0 : y0 - y1;
            }
        }
    }
    mime_vec_mask_free(&mask);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipGetRegionBoundsI(GpRegion *region, GpGraphics *graphics, GpRect *rect)
{
    GpRectF f;
    if (!rect)
        return InvalidParameter;
    GpStatus st = GdipGetRegionBounds(region, graphics, &f);
    if (st != Ok)
        return st;
    INT l = (INT)mime_vec_floorf(f.X), t = (INT)mime_vec_floorf(f.Y);
    INT rr = (INT)mime_vec_ceilf(f.X + f.Width), b = (INT)mime_vec_ceilf(f.Y + f.Height);
    rect->X = l;
    rect->Y = t;
    rect->Width = rr - l;
    rect->Height = b - t;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetRegionHRgn(GpRegion *region, GpGraphics *graphics, HRGN *hrgn)
{
    GRegion *r = gp_region_cast(region);
    if (!r || !hrgn || !bounds_graphics_ok(graphics))
        return InvalidParameter;
    MimeVecMatrix id;
    const MimeVecMatrix *m = graphics_matrix(graphics, &id);
    MimeVecMask mask;
    mime_vec_mask_init(&mask, &gp_allocator);
    BOOL infinite;
    GpStatus st = region_mask_own(r, m, &mask, &infinite);
    if (st == Ok && infinite) {
        *hrgn = CreateRectRgn(-4194304, -4194304, 4194304, 4194304);
        mime_vec_mask_free(&mask);
        return *hrgn ? Ok : OutOfMemory;
    }
    if (st == Ok) {
        UINT count = 0;
        for (UINT b = 0; b < mask.nbands; b++) {
            for (UINT i = 0; i < mask.bands[b].count; i++)
                count++;
        }
        SIZE_T bytes = sizeof(RGNDATAHEADER) + (SIZE_T)(count ? count : 1) * sizeof(RECT);
        RGNDATA *data = gp_alloc_zero(bytes);
        if (!data) {
            st = OutOfMemory;
        } else {
            RECT *rects = (RECT *)data->Buffer;
            UINT n = 0;
            for (UINT b = 0; b < mask.nbands; b++) {
                const MimeVecBand *band = &mask.bands[b];
                for (UINT i = 0; i < band->count; i++) {
                    const MimeVecRun *run = &mask.runs[band->first + i];
                    rects[n].left = run->x0;
                    rects[n].right = run->x1;
                    rects[n].top = band->y0;
                    rects[n].bottom = band->y1;
                    n++;
                }
            }
            data->rdh.dwSize = sizeof(RGNDATAHEADER);
            data->rdh.iType = RDH_RECTANGLES;
            data->rdh.nCount = n;
            data->rdh.nRgnSize = n * sizeof(RECT);
            if (n) {
                data->rdh.rcBound.left = mask.bounds.left;
                data->rdh.rcBound.top = mask.bounds.top;
                data->rdh.rcBound.right = mask.bounds.right;
                data->rdh.rcBound.bottom = mask.bounds.bottom;
            }
            HRGN h = ExtCreateRegion(nullptr, (DWORD)(sizeof(RGNDATAHEADER) + n * sizeof(RECT)), data);
            if (!h && !n)
                h = CreateRectRgn(0, 0, 0, 0);
            gp_free(data);
            if (!h)
                st = OutOfMemory;
            else
                *hrgn = h;
        }
    }
    mime_vec_mask_free(&mask);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipIsEmptyRegion(GpRegion *region, GpGraphics *graphics, BOOL *result)
{
    GRegion *r = gp_region_cast(region);
    if (!r || !result || !bounds_graphics_ok(graphics))
        return InvalidParameter;
    if (r->root->kind == GREGION_EMPTY) {
        *result = TRUE;
        return Ok;
    }
    if (r->root->kind == GREGION_INFINITE) {
        *result = FALSE;
        return Ok;
    }
    MimeVecMatrix id;
    const MimeVecMatrix *m = graphics_matrix(graphics, &id);
    MimeVecMask mask;
    mime_vec_mask_init(&mask, &gp_allocator);
    BOOL infinite;
    GpStatus st = region_mask_own(r, m, &mask, &infinite);
    if (st == Ok)
        *result = !infinite && mime_vec_mask_is_empty(&mask);
    mime_vec_mask_free(&mask);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipIsInfiniteRegion(GpRegion *region, GpGraphics *graphics, BOOL *result)
{
    GRegion *r = gp_region_cast(region);
    if (!r || !result || !bounds_graphics_ok(graphics))
        return InvalidParameter;
    *result = r->root->kind == GREGION_INFINITE;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipIsEqualRegion(GpRegion *region, GpRegion *other, GpGraphics *graphics, BOOL *result)
{
    GRegion *a = gp_region_cast(region), *b = gp_region_cast(other);
    if (!a || !b || !result || !bounds_graphics_ok(graphics))
        return InvalidParameter;
    const GRegionNode *x = a->root, *y = b->root;
    if (x->kind == GREGION_INFINITE || y->kind == GREGION_INFINITE) {
        *result = x->kind == y->kind;
        return Ok;
    }
    if (x->kind == GREGION_EMPTY && y->kind == GREGION_EMPTY) {
        *result = TRUE;
        return Ok;
    }
    MimeVecMatrix id;
    const MimeVecMatrix *m = graphics_matrix(graphics, &id);
    MimeVecMask ma, mb, diff;
    mime_vec_mask_init(&ma, &gp_allocator);
    mime_vec_mask_init(&mb, &gp_allocator);
    mime_vec_mask_init(&diff, &gp_allocator);
    BOOL ia, ib;
    GpStatus st = region_mask_own(a, m, &ma, &ia);
    if (st == Ok)
        st = region_mask_own(b, m, &mb, &ib);
    if (st == Ok) {
        if (ia != ib)
            *result = FALSE;
        else if (mime_vec_mask_combine(&diff, &ma, &mb, MIME_VEC_COMBINE_XOR) != MIME_VEC_OK)
            st = OutOfMemory;
        else
            *result = mime_vec_mask_is_empty(&diff);
    }
    mime_vec_mask_free(&ma);
    mime_vec_mask_free(&mb);
    mime_vec_mask_free(&diff);
    return st;
}

static BOOL point_inside(const GRegion *r, const MimeVecMatrix *m, float x, float y, GpStatus *status)
{
    *status = Ok;
    if (r->root->kind == GREGION_INFINITE)
        return TRUE;
    if (r->root->kind == GREGION_EMPTY)
        return FALSE;
    MimeVecPoint p = mime_vec_xform(m, x, y);
    INT px = (INT)mime_vec_floorf(p.x), py = (INT)mime_vec_floorf(p.y);
    MimeVecMask mask;
    mime_vec_mask_init(&mask, &gp_allocator);
    MimeVecRect clip = {px, py, px + 1, py + 1};
    BOOL inside = FALSE;
    *status = node_mask(r->root, m, &clip, &mask);
    if (*status == Ok)
        inside = mime_vec_mask_coverage_at(&mask, px, py) != 0;
    mime_vec_mask_free(&mask);
    return inside;
}

WINGDIPAPI GpStatus WINAPI GdipIsVisibleRegionPoint(GpRegion *region, REAL x, REAL y, GpGraphics *graphics, BOOL *result)
{
    GRegion *r = gp_region_cast(region);
    if (!r || !result || !bounds_graphics_ok(graphics))
        return InvalidParameter;
    MimeVecMatrix id;
    const MimeVecMatrix *m = graphics_matrix(graphics, &id);
    GpStatus st;
    *result = point_inside(r, m, x, y, &st);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipIsVisibleRegionPointI(GpRegion *region, INT x, INT y, GpGraphics *graphics, BOOL *result)
{
    return GdipIsVisibleRegionPoint(region, (float)x, (float)y, graphics, result);
}

WINGDIPAPI GpStatus WINAPI GdipIsVisibleRegionRect(GpRegion *region, REAL x, REAL y, REAL w, REAL h,
                                                   GpGraphics *graphics, BOOL *result)
{
    GRegion *r = gp_region_cast(region);
    if (!r || !result || !bounds_graphics_ok(graphics))
        return InvalidParameter;
    if (r->root->kind == GREGION_INFINITE) {
        *result = TRUE;
        return Ok;
    }
    if (r->root->kind == GREGION_EMPTY) {
        *result = FALSE;
        return Ok;
    }
    MimeVecMatrix id;
    const MimeVecMatrix *m = graphics_matrix(graphics, &id);
    MimeVecPoint p0 = mime_vec_xform(m, x, y), p1 = mime_vec_xform(m, x + w, y + h);
    MimeVecRect clip;
    float lx = p0.x < p1.x ? p0.x : p1.x, hx = p0.x < p1.x ? p1.x : p0.x;
    float ly = p0.y < p1.y ? p0.y : p1.y, hy = p0.y < p1.y ? p1.y : p0.y;
    clip.left = (INT)mime_vec_floorf(lx);
    clip.top = (INT)mime_vec_floorf(ly);
    clip.right = (INT)mime_vec_ceilf(hx);
    clip.bottom = (INT)mime_vec_ceilf(hy);
    if (clip.right <= clip.left)
        clip.right = clip.left + 1;
    if (clip.bottom <= clip.top)
        clip.bottom = clip.top + 1;
    MimeVecMask mask;
    mime_vec_mask_init(&mask, &gp_allocator);
    GpStatus st = node_mask(r->root, m, &clip, &mask);
    if (st == Ok)
        *result = mime_vec_mask_test_rect(&mask, &clip) != MIME_VEC_TEST_OUTSIDE;
    mime_vec_mask_free(&mask);
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipIsVisibleRegionRectI(GpRegion *region, INT x, INT y, INT w, INT h, GpGraphics *graphics,
                                                    BOOL *result)
{
    return GdipIsVisibleRegionRect(region, (float)x, (float)y, (float)w, (float)h, graphics, result);
}

/* the rectangles of the mask, rows of equal runs merged downwards. */
static GpStatus region_rects(const GRegion *r, const MimeVecMatrix *m, GpRectF **out, INT *count)
{
    *out = nullptr;
    *count = 0;
    if (r->root->kind == GREGION_INFINITE) {
        GpRectF *f = gp_alloc(sizeof(GpRectF));
        if (!f)
            return OutOfMemory;
        f->X = f->Y = -INFINITE_LIMIT;
        f->Width = f->Height = 2.0f * INFINITE_LIMIT;
        *out = f;
        *count = 1;
        return Ok;
    }
    MimeVecMask mask;
    mime_vec_mask_init(&mask, &gp_allocator);
    BOOL infinite;
    GpStatus st = region_mask_own(r, m, &mask, &infinite);
    if (st != Ok) {
        mime_vec_mask_free(&mask);
        return st;
    }
    UINT n = 0;
    for (UINT b = 0; b < mask.nbands; b++)
        n += mask.bands[b].count;
    GpRectF *f = gp_alloc(sizeof(GpRectF) * (n ? n : 1));
    if (!f) {
        mime_vec_mask_free(&mask);
        return OutOfMemory;
    }
    UINT k = 0;
    for (UINT b = 0; b < mask.nbands; b++) {
        const MimeVecBand *band = &mask.bands[b];
        for (UINT i = 0; i < band->count; i++) {
            const MimeVecRun *run = &mask.runs[band->first + i];
            f[k].X = (float)run->x0;
            f[k].Y = (float)band->y0;
            f[k].Width = (float)(run->x1 - run->x0);
            f[k].Height = (float)(band->y1 - band->y0);
            k++;
        }
    }
    mime_vec_mask_free(&mask);
    *out = f;
    *count = (INT)k;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetRegionScansCount(GpRegion *region, UINT *count, GpMatrix *matrix)
{
    GRegion *r = gp_region_cast(region);
    GMatrix *m = gp_matrix_cast(matrix);
    if (!r || !count || !m)
        return InvalidParameter;
    GpRectF *rects;
    INT n;
    GpStatus st = region_rects(r, &m->m, &rects, &n);
    if (st == Ok) {
        *count = (UINT)n;
        gp_free(rects);
    }
    return st;
}

WINGDIPAPI GpStatus WINAPI GdipGetRegionScans(GpRegion *region, GpRectF *rects, INT *count, GpMatrix *matrix)
{
    GRegion *r = gp_region_cast(region);
    GMatrix *m = gp_matrix_cast(matrix);
    if (!r || !count || !m)
        return InvalidParameter;
    GpRectF *list;
    INT n;
    GpStatus st = region_rects(r, &m->m, &list, &n);
    if (st != Ok)
        return st;
    if (rects) {
        for (INT i = 0; i < n; i++)
            rects[i] = list[i];
    }
    *count = n;
    gp_free(list);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetRegionScansI(GpRegion *region, GpRect *rects, INT *count, GpMatrix *matrix)
{
    GRegion *r = gp_region_cast(region);
    GMatrix *m = gp_matrix_cast(matrix);
    if (!r || !count || !m)
        return InvalidParameter;
    GpRectF *list;
    INT n;
    GpStatus st = region_rects(r, &m->m, &list, &n);
    if (st != Ok)
        return st;
    if (rects) {
        for (INT i = 0; i < n; i++) {
            rects[i].X = (INT)list[i].X;
            rects[i].Y = (INT)list[i].Y;
            rects[i].Width = (INT)list[i].Width;
            rects[i].Height = (INT)list[i].Height;
        }
    }
    *count = n;
    gp_free(list);
    return Ok;
}

/* ------------------------------------------------------------------ data */

static UINT crc_table[256];
static BOOL crc_ready;

static UINT32 crc32_of(const BYTE *p, SIZE_T n)
{
    if (!crc_ready) {
        for (UINT i = 0; i < 256; i++) {
            UINT c = i;
            for (INT k = 0; k < 8; k++)
                c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            crc_table[i] = c;
        }
        crc_ready = TRUE;
    }
    UINT c = 0xffffffffu;
    while (n--)
        c = crc_table[(c ^ *p++) & 255] ^ (c >> 8);
    return ~c;
}

static SIZE_T path_blob_size(const GPath *p)
{
    return 12 + (SIZE_T)p->path.count * 8 + ((p->path.count + 3) & ~3u);
}

static SIZE_T node_size(const GRegionNode *n)
{
    switch (n->kind) {
    case GREGION_RECT:
        return 4 + 16;
    case GREGION_PATH:
        return 4 + 4 + path_blob_size(n->path);
    case GREGION_OP:
        return 4 + node_size(n->left) + node_size(n->right);
    default:
        return 4;
    }
}

static INT node_count(const GRegionNode *n)
{
    return n->kind == GREGION_OP ? 1 + node_count(n->left) + node_count(n->right) : 1;
}

static BYTE *put32(BYTE *p, UINT32 v)
{
    memcpy(p, &v, 4);
    return p + 4;
}

static BYTE *put_node(BYTE *p, const GRegionNode *n)
{
    switch (n->kind) {
    case GREGION_RECT:
        p = put32(p, NODE_RECT);
        memcpy(p, &n->rect, 16);
        return p + 16;
    case GREGION_PATH: {
        p = put32(p, NODE_PATH);
        p = put32(p, (UINT32)path_blob_size(n->path));
        p = put32(p, PATH_MAGIC);
        p = put32(p, n->path->path.count);
        p = put32(p, 0);
        memcpy(p, n->path->path.pts, n->path->path.count * 8);
        p += n->path->path.count * 8;
        memcpy(p, n->path->path.types, n->path->path.count);
        SIZE_T pad = ((n->path->path.count + 3) & ~3u) - n->path->path.count;
        memset(p + n->path->path.count, 0, pad);
        return p + n->path->path.count + pad;
    }
    case GREGION_OP:
        p = put32(p, (UINT32)n->op);
        p = put_node(p, n->left);
        return put_node(p, n->right);
    case GREGION_EMPTY:
        return put32(p, NODE_EMPTY);
    default:
        return put32(p, NODE_INFINITE);
    }
}

WINGDIPAPI GpStatus WINAPI GdipGetRegionDataSize(GpRegion *region, UINT *size)
{
    GRegion *r = gp_region_cast(region);
    if (!r || !size)
        return InvalidParameter;
    /* the size and crc, the magic and the count, and the nodes */
    *size = (UINT)(8 + 8 + node_size(r->root));
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetRegionData(GpRegion *region, BYTE *buffer, UINT size, UINT *filled)
{
    GRegion *r = gp_region_cast(region);
    if (!r || !buffer || !size)
        return InvalidParameter;
    UINT need = (UINT)(8 + 8 + node_size(r->root));
    if (size < need)
        return InsufficientBuffer;
    BYTE *p = buffer + 8;
    p = put32(p, REGION_MAGIC);
    p = put32(p, (UINT32)(node_count(r->root) - 1));
    p = put_node(p, r->root);
    UINT body = (UINT)(p - (buffer + 8));
    put32(buffer, body);
    put32(buffer + 4, crc32_of(buffer + 8, body));
    if (filled)
        *filled = need;
    return Ok;
}

typedef struct Reader {
    const BYTE *p, *end;
    BOOL bad;
} Reader;

static UINT32 rd32(Reader *r)
{
    UINT32 v = 0;
    if (r->end - r->p < 4) {
        r->bad = TRUE;
        return 0;
    }
    memcpy(&v, r->p, 4);
    r->p += 4;
    return v;
}

static GRegionNode *read_node(Reader *r, INT depth)
{
    if (depth > 256) {
        r->bad = TRUE;
        return nullptr;
    }
    UINT32 type = rd32(r);
    if (r->bad)
        return nullptr;
    if (type & 0x10000000u) {
        switch (type) {
        case NODE_RECT: {
            if (r->end - r->p < 16) {
                r->bad = TRUE;
                return nullptr;
            }
            GpRectF rc;
            memcpy(&rc, r->p, 16);
            r->p += 16;
            return rect_node(rc.X, rc.Y, rc.Width, rc.Height);
        }
        case NODE_PATH: {
            UINT32 size = rd32(r);
            if (r->bad || size < 12 || (SIZE_T)(r->end - r->p) < size) {
                r->bad = TRUE;
                return nullptr;
            }
            const BYTE *q = r->p;
            UINT32 magic, count, flags;
            memcpy(&magic, q, 4);
            memcpy(&count, q + 4, 4);
            memcpy(&flags, q + 8, 4);
            q += 12;
            BOOL ints = (flags & 0x4000) != 0;
            SIZE_T point_bytes = (SIZE_T)count * (ints ? 4 : 8);
            if ((magic & 0xfffff000u) != 0xdbc01000u || count > (1u << 20) || 12 + point_bytes + count > size) {
                r->bad = TRUE;
                return nullptr;
            }
            GPath *p = gp_path_new(FillModeAlternate);
            if (!p)
                return nullptr;
            MimeVecPoint *pts = gp_alloc(sizeof(MimeVecPoint) * (count ? count : 1));
            if (!pts) {
                gp_path_free(p);
                return nullptr;
            }
            for (UINT i = 0; i < count; i++) {
                if (ints) {
                    short xy[2];
                    memcpy(xy, q + i * 4, 4);
                    pts[i].x = xy[0];
                    pts[i].y = xy[1];
                } else {
                    memcpy(&pts[i], q + i * 8, 8);
                }
            }
            const BYTE *types = q + point_bytes;
            BOOL ok = count == 0 || (gp_path_types_valid(types, (INT)count) &&
                                     mime_vec_path_add_raw(&p->path, pts, types, count) == MIME_VEC_OK);
            gp_free(pts);
            if (!ok) {
                gp_path_free(p);
                r->bad = TRUE;
                return nullptr;
            }
            r->p += size;
            GRegionNode *n = node_new(GREGION_PATH);
            if (!n) {
                gp_path_free(p);
                return nullptr;
            }
            n->path = p;
            return n;
        }
        case NODE_EMPTY:
            return node_new(GREGION_EMPTY);
        case NODE_INFINITE:
            return node_new(GREGION_INFINITE);
        default:
            r->bad = TRUE;
            return nullptr;
        }
    }
    if (type < 1 || type > 5) {
        r->bad = TRUE;
        return nullptr;
    }
    GRegionNode *left = read_node(r, depth + 1);
    GRegionNode *right = left ? read_node(r, depth + 1) : nullptr;
    GRegionNode *n = left && right ? node_new(GREGION_OP) : nullptr;
    if (!n) {
        node_free(left);
        node_free(right);
        return nullptr;
    }
    n->op = (CombineMode)type;
    n->left = left;
    n->right = right;
    return n;
}

WINGDIPAPI GpStatus WINAPI GdipCreateRegionRgnData(const BYTE *data, INT size, GpRegion **region)
{
    GP_REQUIRE_INIT();
    if (!data || !region || size < 0)
        return InvalidParameter;
    if (size < 12)
        return InsufficientBuffer;
    UINT32 body, crc;
    memcpy(&body, data, 4);
    memcpy(&crc, data + 4, 4);
    if ((SIZE_T)size - 8 < body)
        return InsufficientBuffer;
    if (crc != crc32_of(data + 8, body))
        return InvalidParameter;
    Reader rd = {data + 8, data + 8 + body, FALSE};
    UINT32 magic = rd32(&rd);
    rd32(&rd);
    if (rd.bad || (magic & 0xfffff000u) != 0xdbc01000u)
        return InvalidParameter;
    GRegionNode *root = read_node(&rd, 0);
    if (!root) {
        return rd.bad ? InvalidParameter : OutOfMemory;
    }
    GRegion *r = region_new(root);
    if (!r) {
        node_free(root);
        return OutOfMemory;
    }
    *region = r;
    return Ok;
}
