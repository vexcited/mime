/*
 * enhanced metafile playback: PlayEnhMetaFile, EnumEnhMetaFile and
 * PlayEnhMetaFileRecord.
 *
 * like windows, playback maps the metafile's reference device units through
 * its frame onto the destination rectangle. mapping records go to a private
 * dc at the reference resolution, and the destination's world transform is
 * that dc's world to device transform followed by the frame mapping. clip
 * regions are in reference device units, so only the frame mapping applies.
 */
#include "emf_private.h"

struct EMF_PLAY {
    HDC Dc;
    HDC Virtual; /* the metafile's own mapping. */
    XFORM Base;  /* reference device to destination device. */
    int Saves;
    BOOL Group;  /* the header opened a comment group on a metafile dc. */
    volatile BOOL Cancel;
};

#define PLAY_MAX 64

static RTL_SRWLOCK g_lock;
static EMF_PLAY *g_plays[PLAY_MAX];

static void *heap_alloc(SIZE_T size)
{
    return RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, size);
}

static void *heap_zalloc(SIZE_T size)
{
    return RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, HEAP_ZERO_MEMORY, size);
}

static void heap_free(void *p)
{
    if (p)
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, p);
}

/* for CancelDC: stops every playback on the dc. */
BOOL EmfCancelPlay(HDC dc)
{
    BOOL found = FALSE;
    RtlAcquireSRWLockShared(&g_lock);
    for (UINT i = 0; i < PLAY_MAX; i++) {
        if (g_plays[i] && g_plays[i]->Dc == dc) {
            g_plays[i]->Cancel = TRUE;
            found = TRUE;
        }
    }
    RtlReleaseSRWLockShared(&g_lock);
    return found;
}

static void plays_add(EMF_PLAY *p)
{
    RtlAcquireSRWLockExclusive(&g_lock);
    for (UINT i = 0; i < PLAY_MAX; i++) {
        if (!g_plays[i]) {
            g_plays[i] = p;
            break;
        }
    }
    RtlReleaseSRWLockExclusive(&g_lock);
}

static void plays_remove(EMF_PLAY *p)
{
    RtlAcquireSRWLockExclusive(&g_lock);
    for (UINT i = 0; i < PLAY_MAX; i++)
        if (g_plays[i] == p)
            g_plays[i] = nullptr;
    RtlReleaseSRWLockExclusive(&g_lock);
}

static void multiply(XFORM *out, const XFORM *a, const XFORM *b)
{
    XFORM r;
    r.eM11 = a->eM11 * b->eM11 + a->eM12 * b->eM21;
    r.eM12 = a->eM11 * b->eM12 + a->eM12 * b->eM22;
    r.eM21 = a->eM21 * b->eM11 + a->eM22 * b->eM21;
    r.eM22 = a->eM21 * b->eM12 + a->eM22 * b->eM22;
    r.eDx = a->eDx * b->eM11 + a->eDy * b->eM21 + b->eDx;
    r.eDy = a->eDx * b->eM12 + a->eDy * b->eM22 + b->eDy;
    *out = r;
}

/* a handle table's first entry is the metafile, which knows its playback. */
static EMF_PLAY *play_of(const HANDLETABLE *table)
{
    if (!table)
        return nullptr;
    EMF *emf = EmfLookup((HENHMETAFILE)table->objectHandle[0]);
    return emf ? emf->Play : nullptr;
}

/* the metafile's mapping, then the frame. */
static BOOL apply_transform(EMF_PLAY *p)
{
    XFORM m;
    if (!NtGdiGetTransform(p->Virtual, MIME_XFORM_WORLD_TO_DEVICE, &m))
        return FALSE;
    multiply(&m, &m, &p->Base);
    return SetWorldTransform(p->Dc, &m);
}

/* ------------------------------------------------------------------ record checks */

static BOOL fits(const ENHMETARECORD *r, DWORD offset, DWORD size)
{
    return offset <= r->nSize && size <= r->nSize - offset;
}

static const BYTE *at(const ENHMETARECORD *r, DWORD offset)
{
    return (const BYTE *)r + offset;
}

static HGDIOBJ table_object(const HANDLETABLE *table, UINT handles, DWORD index)
{
    if (index & ENHMETA_STOCK_OBJECT) {
        index &= 0x7FFFFFFF;
        return index <= STOCK_LAST ? GetStockObject((int)index) : nullptr;
    }
    return table && index && index < handles ? table->objectHandle[index] : nullptr;
}

static void table_set(HANDLETABLE *table, UINT handles, DWORD index, HGDIOBJ object)
{
    if (!table || !index || index >= handles) {
        if (object)
            DeleteObject(object);
        return;
    }
    if (table->objectHandle[index])
        DeleteObject(table->objectHandle[index]);
    table->objectHandle[index] = object;
}

/* ------------------------------------------------------------------ records with points */

static BOOL play_poly(HDC dc, const ENHMETARECORD *r, BOOL small)
{
    DWORD count = r->dParm[4];
    DWORD each = small ? sizeof(POINTS) : sizeof(POINTL);
    if (!fits(r, 28, 0) || count > (r->nSize - 28) / each)
        return FALSE;
    POINT *pts = heap_alloc(((SIZE_T)count + 1) * sizeof(POINT));
    if (!pts)
        return FALSE;
    for (DWORD i = 0; i < count; i++) {
        if (small) {
            const POINTS *s = (const POINTS *)at(r, 28) + i;
            pts[i] = (POINT){s->x, s->y};
        } else {
            pts[i] = ((const POINT *)at(r, 28))[i];
        }
    }
    BOOL ok;
    switch (small ? r->iType - 83 : r->iType) {
    case EMR_POLYBEZIER:
        ok = PolyBezier(dc, pts, count);
        break;
    case EMR_POLYGON:
        ok = Polygon(dc, pts, (int)count);
        break;
    case EMR_POLYLINE:
        ok = Polyline(dc, pts, (int)count);
        break;
    case EMR_POLYBEZIERTO:
        ok = PolyBezierTo(dc, pts, count);
        break;
    default:
        ok = PolylineTo(dc, pts, count);
        break;
    }
    heap_free(pts);
    return ok;
}

static BOOL play_polypoly(HDC dc, const ENHMETARECORD *r, BOOL small)
{
    if (!fits(r, 32, 0))
        return FALSE;
    DWORD polys = r->dParm[4], total = r->dParm[5];
    DWORD each = small ? sizeof(POINTS) : sizeof(POINTL);
    if (polys > (r->nSize - 32) / 4 || total > (r->nSize - 32 - polys * 4) / each)
        return FALSE;
    const DWORD *counts = (const DWORD *)at(r, 32);
    DWORD sum = 0;
    for (DWORD i = 0; i < polys; i++) {
        if (counts[i] > total - sum)
            return FALSE;
        sum += counts[i];
    }
    POINT *pts = heap_alloc(((SIZE_T)total + 1) * sizeof(POINT));
    if (!pts)
        return FALSE;
    const BYTE *base = at(r, 32 + polys * 4);
    for (DWORD i = 0; i < total; i++) {
        if (small) {
            const POINTS *s = (const POINTS *)base + i;
            pts[i] = (POINT){s->x, s->y};
        } else {
            pts[i] = ((const POINT *)base)[i];
        }
    }
    BOOL ok = (small ? r->iType - 83 : r->iType) == EMR_POLYPOLYGON
                  ? PolyPolygon(dc, pts, (const INT *)counts, (int)polys)
                  : PolyPolyline(dc, pts, counts, polys);
    heap_free(pts);
    return ok;
}

static BOOL play_polydraw(HDC dc, const ENHMETARECORD *r, BOOL small)
{
    DWORD count = r->dParm[4];
    DWORD each = small ? sizeof(POINTS) : sizeof(POINTL);
    if (!fits(r, 28, 0) || count > (r->nSize - 28) / (each + 1))
        return FALSE;
    POINT *pts = heap_alloc(((SIZE_T)count + 1) * sizeof(POINT));
    if (!pts)
        return FALSE;
    for (DWORD i = 0; i < count; i++) {
        if (small) {
            const POINTS *s = (const POINTS *)at(r, 28) + i;
            pts[i] = (POINT){s->x, s->y};
        } else {
            pts[i] = ((const POINT *)at(r, 28))[i];
        }
    }
    BOOL ok = PolyDraw(dc, pts, at(r, 28 + count * each), (int)count);
    heap_free(pts);
    return ok;
}

/* ------------------------------------------------------------------ bitmaps */

/* a dib section with the bitmap of a record selected in a memory dc of the destination. */
typedef struct SOURCE {
    HDC Dc;
    HBITMAP Bitmap;
    HGDIOBJ Old;
} SOURCE;

static BOOL source_open(SOURCE *s, HDC dest, const ENHMETARECORD *r, DWORD off_bmi, DWORD cb_bmi, DWORD off_bits,
                        DWORD cb_bits, UINT usage)
{
    memset(s, 0, sizeof(*s));
    if (!cb_bmi || !fits(r, off_bmi, cb_bmi) || !fits(r, off_bits, cb_bits) || cb_bmi < sizeof(BITMAPINFOHEADER))
        return FALSE;
    const BITMAPINFO *info = (const BITMAPINFO *)at(r, off_bmi);
    if (info->bmiHeader.biSize > cb_bmi)
        return FALSE;
    DWORD need = GdiDibBitsSize(&info->bmiHeader);
    if (info->bmiHeader.biCompression == BI_RGB || info->bmiHeader.biCompression == BI_BITFIELDS) {
        if (cb_bits < need)
            return FALSE;
    }
    s->Dc = CreateCompatibleDC(dest && !MfActive(dest) ? dest : nullptr);
    if (!s->Dc)
        return FALSE;
    void *bits = nullptr;
    s->Bitmap = CreateDIBSection(s->Dc, info, usage, &bits, nullptr, 0);
    if (!s->Bitmap || !bits) {
        if (s->Bitmap)
            DeleteObject(s->Bitmap);
        DeleteDC(s->Dc);
        return FALSE;
    }
    memcpy(bits, at(r, off_bits), need < cb_bits ? need : cb_bits);
    s->Old = SelectObject(s->Dc, s->Bitmap);
    SetGraphicsMode(s->Dc, GM_ADVANCED);
    return TRUE;
}

static void source_close(SOURCE *s)
{
    SelectObject(s->Dc, s->Old);
    DeleteObject(s->Bitmap);
    DeleteDC(s->Dc);
}

static BOOL play_blt(HDC dc, const ENHMETARECORD *r)
{
    if (!fits(r, 0, sizeof(EMRBITBLT)))
        return FALSE;
    const EMRSTRETCHBLT *b = (const EMRSTRETCHBLT *)r;
    BOOL stretch = r->iType != EMR_BITBLT && r->iType != EMR_MASKBLT;
    if (stretch && !fits(r, 0, sizeof(EMRSTRETCHBLT)))
        return FALSE;
    DWORD rop = r->iType == EMR_MASKBLT ? b->dwRop & 0xFFFFFF : b->dwRop;
    BOOL source = r->iType == EMR_ALPHABLEND || r->iType == EMR_TRANSPARENTBLT || ((rop ^ (rop << 2)) & 0xCCCC0000);
    if (!source)
        return PatBlt(dc, b->xDest, b->yDest, b->cxDest, b->cyDest, rop);
    SOURCE s;
    if (!source_open(&s, dc, r, b->offBmiSrc, b->cbBmiSrc, b->offBitsSrc, b->cbBitsSrc, b->iUsageSrc))
        return FALSE;
    SetWorldTransform(s.Dc, &b->xformSrc);
    SetBkColor(s.Dc, b->crBkColorSrc);
    BOOL ok;
    switch (r->iType) {
    case EMR_ALPHABLEND: {
        BLENDFUNCTION blend;
        memcpy(&blend, &b->dwRop, sizeof(blend));
        ok = GdiAlphaBlend(dc, b->xDest, b->yDest, b->cxDest, b->cyDest, s.Dc, b->xSrc, b->ySrc, b->cxSrc, b->cySrc,
                           blend);
        break;
    }
    case EMR_TRANSPARENTBLT:
        ok = GdiTransparentBlt(dc, b->xDest, b->yDest, b->cxDest, b->cyDest, s.Dc, b->xSrc, b->ySrc, b->cxSrc,
                               b->cySrc, b->dwRop);
        break;
    case EMR_STRETCHBLT:
        ok = StretchBlt(dc, b->xDest, b->yDest, b->cxDest, b->cyDest, s.Dc, b->xSrc, b->ySrc, b->cxSrc, b->cySrc, rop);
        break;
    default:
        ok = BitBlt(dc, b->xDest, b->yDest, b->cxDest, b->cyDest, s.Dc, b->xSrc, b->ySrc, rop);
        break;
    }
    source_close(&s);
    return ok;
}

static BOOL play_stretchdibits(HDC dc, const ENHMETARECORD *r)
{
    if (!fits(r, 0, sizeof(EMRSTRETCHDIBITS)))
        return FALSE;
    const EMRSTRETCHDIBITS *s = (const EMRSTRETCHDIBITS *)r;
    if (!s->cbBmiSrc)
        return PatBlt(dc, s->xDest, s->yDest, s->cxDest, s->cyDest, s->dwRop);
    if (!fits(r, s->offBmiSrc, s->cbBmiSrc) || !fits(r, s->offBitsSrc, s->cbBitsSrc) ||
        s->cbBmiSrc < sizeof(BITMAPINFOHEADER))
        return FALSE;
    return StretchDIBits(dc, s->xDest, s->yDest, s->cxDest, s->cyDest, s->xSrc, s->ySrc, s->cxSrc, s->cySrc,
                         at(r, s->offBitsSrc), (const BITMAPINFO *)at(r, s->offBmiSrc), s->iUsageSrc, s->dwRop) != 0;
}

/*
 * like windows, the destination point goes through the metafile's mapping,
 * but the bits keep their size in device units and only go through the frame
 * mapping.
 */
static BOOL play_setdibits(HDC dc, EMF_PLAY *p, const ENHMETARECORD *r)
{
    if (!fits(r, 0, sizeof(EMRSETDIBITSTODEVICE)))
        return FALSE;
    const EMRSETDIBITSTODEVICE *s = (const EMRSETDIBITSTODEVICE *)r;
    if (!fits(r, s->offBmiSrc, s->cbBmiSrc) || !fits(r, s->offBitsSrc, s->cbBitsSrc) ||
        s->cbBmiSrc < sizeof(BITMAPINFOHEADER))
        return FALSE;
    const BITMAPINFO *info = (const BITMAPINFO *)at(r, s->offBmiSrc);
    if (!p)
        return SetDIBitsToDevice(dc, s->xDest, s->yDest, (DWORD)s->cxSrc, (DWORD)s->cySrc, s->xSrc, s->ySrc,
                                 s->iStartScan, s->cScans, at(r, s->offBitsSrc), info, s->iUsageSrc) != 0;
    POINT pt = {s->xDest, s->yDest};
    if (!LPtoDP(p->Virtual, &pt, 1) || !SetWorldTransform(dc, &p->Base))
        return FALSE;
    BITMAPINFO *copy = heap_alloc(s->cbBmiSrc < 1272 ? 1272 : s->cbBmiSrc);
    BOOL ok = FALSE;
    if (copy) {
        memcpy(copy, info, s->cbBmiSrc);
        copy->bmiHeader.biHeight = info->bmiHeader.biHeight > 0 ? (LONG)s->cScans : -(LONG)s->cScans;
        copy->bmiHeader.biSizeImage = s->cbBitsSrc;
        ok = StretchDIBits(dc, pt.x, pt.y, s->cxSrc, s->cySrc, s->xSrc, s->ySrc - (LONG)s->iStartScan, s->cxSrc,
                           s->cySrc, at(r, s->offBitsSrc), copy, s->iUsageSrc, SRCCOPY) != 0;
        heap_free(copy);
    }
    apply_transform(p);
    return ok;
}

/* ------------------------------------------------------------------ text */

static BOOL play_text(HDC dc, const ENHMETARECORD *r, const EMRTEXT *t, BOOL wide)
{
    DWORD chars = t->nChars;
    if (!fits(r, t->offString, chars * (wide ? 2 : 1)))
        return FALSE;
    UINT step = (t->fOptions & ETO_PDY) ? 2 : 1;
    const INT *dx = nullptr;
    if (t->offDx && chars) {
        if (!fits(r, t->offDx, chars * 4 * step))
            return FALSE;
        dx = (const INT *)at(r, t->offDx);
    }
    RECT rect = {t->rcl.left, t->rcl.top, t->rcl.right, t->rcl.bottom};
    const RECT *prect = (t->fOptions & (ETO_OPAQUE | ETO_CLIPPED)) ? &rect : nullptr;
    if (wide)
        return ExtTextOutW(dc, t->ptlReference.x, t->ptlReference.y, t->fOptions, prect,
                           (LPCWSTR)at(r, t->offString), chars, dx);
    return ExtTextOutA(dc, t->ptlReference.x, t->ptlReference.y, t->fOptions, prect, (LPCSTR)at(r, t->offString),
                       chars, dx);
}

#define ETO_NO_RECT 0x0100
#define ETO_SMALL_CHARS 0x0200

/* the compact text record of windows' spooler: a point, counts, scales, an optional rectangle and the text. */
static BOOL play_smalltext(HDC dc, const ENHMETARECORD *r)
{
    if (!fits(r, 0, 36))
        return FALSE;
    LONG x = (LONG)r->dParm[0], y = (LONG)r->dParm[1];
    DWORD chars = r->dParm[2], options = r->dParm[3];
    DWORD off = 36;
    RECT rect = {0, 0, 0, 0};
    if (!(options & ETO_NO_RECT)) {
        if (!fits(r, off, sizeof(RECTL)))
            return FALSE;
        memcpy(&rect, at(r, off), sizeof(rect));
        off += sizeof(RECTL);
    }
    UINT flags = options & ~(UINT)(ETO_NO_RECT | ETO_SMALL_CHARS);
    const RECT *prect = (flags & (ETO_OPAQUE | ETO_CLIPPED)) ? &rect : nullptr;
    if (options & ETO_SMALL_CHARS) {
        if (!fits(r, off, chars))
            return FALSE;
        WCHAR *w = heap_alloc(((SIZE_T)chars + 1) * sizeof(WCHAR));
        if (!w)
            return FALSE;
        for (DWORD i = 0; i < chars; i++)
            w[i] = at(r, off)[i];
        BOOL ok = ExtTextOutW(dc, x, y, flags, prect, w, chars, nullptr);
        heap_free(w);
        return ok;
    }
    if (!fits(r, off, chars * 2))
        return FALSE;
    return ExtTextOutW(dc, x, y, flags, prect, (LPCWSTR)at(r, off), chars, nullptr);
}

/* ------------------------------------------------------------------ objects */

static BOOL play_dib_brush(HANDLETABLE *table, UINT handles, const ENHMETARECORD *r)
{
    if (!fits(r, 0, sizeof(EMRCREATEDIBPATTERNBRUSHPT)))
        return FALSE;
    const EMRCREATEDIBPATTERNBRUSHPT *b = (const EMRCREATEDIBPATTERNBRUSHPT *)r;
    if (!fits(r, b->offBmi, b->cbBmi) || !fits(r, b->offBits, b->cbBits) || b->cbBmi < sizeof(BITMAPINFOHEADER))
        return FALSE;
    BYTE *packed = heap_alloc((SIZE_T)b->cbBmi + b->cbBits);
    if (!packed)
        return FALSE;
    memcpy(packed, at(r, b->offBmi), b->cbBmi);
    memcpy(packed + b->cbBmi, at(r, b->offBits), b->cbBits);
    HBRUSH brush = CreateDIBPatternBrushPt(packed, r->iType == EMR_CREATEMONOBRUSH ? DIB_RGB_COLORS : b->iUsage);
    heap_free(packed);
    table_set(table, handles, b->ihBrush, brush);
    return brush != nullptr;
}

static BOOL play_extpen(HANDLETABLE *table, UINT handles, const ENHMETARECORD *r)
{
    if (!fits(r, 0, offsetof(EMREXTCREATEPEN, elp.elpStyleEntry)))
        return FALSE;
    const EMREXTCREATEPEN *e = (const EMREXTCREATEPEN *)r;
    DWORD entries = e->elp.elpNumEntries;
    if (!fits(r, offsetof(EMREXTCREATEPEN, elp.elpStyleEntry), entries * 4))
        entries = 0;
    LOGBRUSH lb = {e->elp.elpBrushStyle, e->elp.elpColor, e->elp.elpHatch};
    if (lb.lbStyle != BS_SOLID && lb.lbStyle != BS_NULL && lb.lbStyle != BS_HATCHED)
        lb.lbStyle = BS_SOLID;
    HPEN pen = ExtCreatePen(e->elp.elpPenStyle, e->elp.elpWidth, &lb, entries,
                            entries ? e->elp.elpStyleEntry : nullptr);
    table_set(table, handles, e->ihPen, pen);
    return pen != nullptr;
}

static BOOL play_region(HDC dc, HANDLETABLE *table, UINT handles, const ENHMETARECORD *r)
{
    DWORD head = r->iType == EMR_FILLRGN ? offsetof(EMRFILLRGN, RgnData)
                 : r->iType == EMR_FRAMERGN ? offsetof(EMRFRAMERGN, RgnData)
                                            : offsetof(EMRINVERTRGN, RgnData);
    if (!fits(r, 0, head))
        return FALSE;
    DWORD size = r->dParm[4];
    if (!fits(r, head, size))
        return FALSE;
    HRGN region = ExtCreateRegion(nullptr, size, (const RGNDATA *)at(r, head));
    if (!region)
        return FALSE;
    BOOL ok;
    switch (r->iType) {
    case EMR_FILLRGN:
        ok = FillRgn(dc, region, table_object(table, handles, r->dParm[5]));
        break;
    case EMR_FRAMERGN: {
        const EMRFRAMERGN *f = (const EMRFRAMERGN *)r;
        ok = FrameRgn(dc, region, table_object(table, handles, f->ihBrush), f->szlStroke.cx, f->szlStroke.cy);
        break;
    }
    case EMR_INVERTRGN:
        ok = InvertRgn(dc, region);
        break;
    default:
        ok = PaintRgn(dc, region);
        break;
    }
    DeleteObject(region);
    return ok;
}

static BOOL play_clip_region(HDC dc, EMF_PLAY *p, const ENHMETARECORD *r)
{
    if (!fits(r, 0, 16))
        return FALSE;
    DWORD size = r->dParm[0], mode = r->dParm[1];
    if (!size)
        return ExtSelectClipRgn(dc, nullptr, (int)mode) != ERROR;
    if (!fits(r, 16, size))
        return FALSE;
    /* the region is in reference device units, which the frame mapping takes to the destination's. */
    HRGN region = ExtCreateRegion(p ? &p->Base : nullptr, size, (const RGNDATA *)at(r, 16));
    if (!region)
        return FALSE;
    BOOL ok = ExtSelectClipRgn(dc, region, (int)mode) != ERROR;
    DeleteObject(region);
    return ok;
}

/* ------------------------------------------------------------------ comments */

static BOOL play_comment(HDC dc, const ENHMETARECORD *r)
{
    if (!fits(r, 0, 12))
        return FALSE;
    DWORD size = r->dParm[0];
    if (!fits(r, 12, size))
        return FALSE;
    const BYTE *data = at(r, 12);
    DWORD ident = 0, type = 0;
    if (size >= 8) {
        memcpy(&ident, data, 4);
        memcpy(&type, data + 4, 4);
    }
    if (ident != GDICOMMENT_IDENTIFIER || size < 8)
        return GdiComment(dc, size, data);
    switch (type) {
    case GDICOMMENT_UNICODE_STRING:
    case GDICOMMENT_UNICODE_END:
        return TRUE;
    case GDICOMMENT_MULTIFORMATS:
        break;
    default:
        return GdiComment(dc, size, data);
    }
    /* several formats of one picture: a normal dc plays the enhanced metafile one, in its output rectangle. */
    if (MfActive(dc))
        return GdiComment(dc, size, data);
    if (size < 28)
        return FALSE;
    RECTL out;
    DWORD formats;
    memcpy(&out, data + 8, sizeof(out));
    memcpy(&formats, data + 24, 4);
    if (formats > (size - 28) / 16)
        return FALSE;
    for (DWORD i = 0; i < formats; i++) {
        DWORD desc[4];
        memcpy(desc, data + 28 + i * 16, sizeof(desc));
        if (desc[0] != ENHMETA_SIGNATURE || desc[1] > 0x10000)
            continue;
        DWORD bytes = desc[2], offset = desc[3];
        /* the offsets count from the start of the comment's data. */
        if (offset > size || bytes > size - offset)
            return FALSE;
        HENHMETAFILE inner = SetEnhMetaFileBits(bytes, data + offset);
        if (!inner)
            return FALSE;
        RECT rect = {out.left, out.top, out.right, out.bottom};
        BOOL ok = PlayEnhMetaFile(dc, inner, &rect);
        DeleteEnhMetaFile(inner);
        return ok;
    }
    return FALSE;
}

/* into a metafile dc, a metafile plays as a group, its frame in reference device units. */
static BOOL begin_group(HDC dc, const ENHMETAHEADER *h)
{
    DWORD chars = h->nDescription;
    if (chars && (h->offDescription > h->nSize || chars > (h->nSize - h->offDescription) / 2))
        chars = 0;
    DWORD size = 28 + chars * 2;
    BYTE *data = heap_zalloc(size);
    if (!data)
        return FALSE;
    DWORD head[2] = {GDICOMMENT_IDENTIFIER, GDICOMMENT_BEGINGROUP};
    memcpy(data, head, 8);
    RECTL out = {MulDiv(h->rclFrame.left, h->szlDevice.cx, 100 * h->szlMillimeters.cx),
                 MulDiv(h->rclFrame.top, h->szlDevice.cy, 100 * h->szlMillimeters.cy),
                 MulDiv(h->rclFrame.right, h->szlDevice.cx, 100 * h->szlMillimeters.cx),
                 MulDiv(h->rclFrame.bottom, h->szlDevice.cy, 100 * h->szlMillimeters.cy)};
    memcpy(data + 8, &out, sizeof(out));
    memcpy(data + 24, &chars, 4);
    if (chars)
        memcpy(data + 28, (const BYTE *)h + h->offDescription, chars * 2);
    BOOL ok = MfComment(dc, size, data);
    heap_free(data);
    return ok;
}

/* ------------------------------------------------------------------ records */

/* the metafile's mapping records go to the private dc, then to the destination. */
static BOOL play_mapping(HDC dc, EMF_PLAY *p, const ENHMETARECORD *r)
{
    HDC target = p ? p->Virtual : dc;
    BOOL ok;
    switch (r->iType) {
    case EMR_SETMAPMODE:
        ok = SetMapMode(target, (int)r->dParm[0]) != 0;
        break;
    case EMR_SETWINDOWEXTEX:
        ok = SetWindowExtEx(target, (int)r->dParm[0], (int)r->dParm[1], nullptr);
        break;
    case EMR_SETWINDOWORGEX:
        ok = SetWindowOrgEx(target, (int)r->dParm[0], (int)r->dParm[1], nullptr);
        break;
    case EMR_SETVIEWPORTEXTEX:
        ok = SetViewportExtEx(target, (int)r->dParm[0], (int)r->dParm[1], nullptr);
        break;
    case EMR_SETVIEWPORTORGEX:
        ok = SetViewportOrgEx(target, (int)r->dParm[0], (int)r->dParm[1], nullptr);
        break;
    case EMR_SCALEVIEWPORTEXTEX:
        ok = ScaleViewportExtEx(target, (int)r->dParm[0], (int)r->dParm[1], (int)r->dParm[2], (int)r->dParm[3],
                                nullptr);
        break;
    case EMR_SCALEWINDOWEXTEX:
        ok = ScaleWindowExtEx(target, (int)r->dParm[0], (int)r->dParm[1], (int)r->dParm[2], (int)r->dParm[3],
                              nullptr);
        break;
    case EMR_SETWORLDTRANSFORM:
        ok = SetWorldTransform(target, (const XFORM *)r->dParm);
        break;
    default: {
        const EMRMODIFYWORLDTRANSFORM *m = (const EMRMODIFYWORLDTRANSFORM *)r;
        ok = ModifyWorldTransform(target, &m->xform, m->iMode);
        break;
    }
    }
    return ok && (!p || apply_transform(p));
}

/* the smallest size of each fixed size record, 0 when it varies or windows does not check it. */
static DWORD min_size(DWORD type)
{
    switch (type) {
    case EMR_SAVEDC:
    case EMR_REALIZEPALETTE:
    case EMR_SETMETARGN:
    case EMR_BEGINPATH:
    case EMR_ENDPATH:
    case EMR_CLOSEFIGURE:
    case EMR_FLATTENPATH:
    case EMR_WIDENPATH:
    case EMR_ABORTPATH:
        return 8;
    case EMR_SETMAPMODE:
    case EMR_SETBKMODE:
    case EMR_SETPOLYFILLMODE:
    case EMR_SETROP2:
    case EMR_SETSTRETCHBLTMODE:
    case EMR_SETTEXTALIGN:
    case EMR_SETTEXTCOLOR:
    case EMR_SETBKCOLOR:
    case EMR_SETMAPPERFLAGS:
    case EMR_RESTOREDC:
    case EMR_SELECTOBJECT:
    case EMR_DELETEOBJECT:
    case EMR_SELECTPALETTE:
    case EMR_SETARCDIRECTION:
    case EMR_SETMITERLIMIT:
    case EMR_SELECTCLIPPATH:
    case EMR_SETLAYOUT:
    case EMR_SETICMMODE:
        return 12;
    case EMR_SETWINDOWEXTEX:
    case EMR_SETWINDOWORGEX:
    case EMR_SETVIEWPORTEXTEX:
    case EMR_SETVIEWPORTORGEX:
    case EMR_SETBRUSHORGEX:
    case EMR_MOVETOEX:
    case EMR_LINETO:
    case EMR_OFFSETCLIPRGN:
        return 16;
    case EMR_SETPIXELV:
        return 20;
    case EMR_EXCLUDECLIPRECT:
    case EMR_INTERSECTCLIPRECT:
    case EMR_SCALEVIEWPORTEXTEX:
    case EMR_SCALEWINDOWEXTEX:
    case EMR_ELLIPSE:
    case EMR_RECTANGLE:
    case EMR_CREATEBRUSHINDIRECT:
    case EMR_FILLPATH:
    case EMR_STROKEPATH:
    case EMR_STROKEANDFILLPATH:
    case EMR_EXTFLOODFILL:
        return 24;
    case EMR_CREATEPEN:
        return 28;
    case EMR_ROUNDRECT:
        return sizeof(EMRROUNDRECT);
    case EMR_ANGLEARC:
        return sizeof(EMRANGLEARC);
    case EMR_ARC:
    case EMR_ARCTO:
    case EMR_CHORD:
    case EMR_PIE:
        return sizeof(EMRARC);
    case EMR_SETWORLDTRANSFORM:
        return sizeof(EMRSETWORLDTRANSFORM);
    case EMR_MODIFYWORLDTRANSFORM:
        return sizeof(EMRMODIFYWORLDTRANSFORM);
    case EMR_EXTCREATEFONTINDIRECTW:
        return offsetof(EMREXTCREATEFONTINDIRECTW, elfw) + sizeof(LOGFONTW);
    case EMR_EXTTEXTOUTA:
    case EMR_EXTTEXTOUTW:
        return sizeof(EMREXTTEXTOUTW);
    case EMR_EOF:
        return sizeof(EMREOF);
    case EMR_GRADIENTFILL:
        return offsetof(EMRGRADIENTFILL, Ver);
    }
    return 8;
}

WINGDIAPI BOOL WINAPI PlayEnhMetaFileRecord(HDC dc, LPHANDLETABLE table, const ENHMETARECORD *record, UINT handles)
{
    if (!record)
        return FALSE;
    const ENHMETARECORD *r = record;
    if (r->nSize < min_size(r->iType) || (r->nSize & 3))
        return FALSE;
    EMF_PLAY *p = play_of(table);
    const DWORD *d = r->dParm;
    switch (r->iType) {
    case EMR_HEADER:
        if (p && MfActive(dc) && !MfIsWmfDc(dc) && fits(r, 0, 88)) {
            p->Group = begin_group(dc, (const ENHMETAHEADER *)r);
            return p->Group;
        }
        return TRUE;
    case EMR_EOF:
        if (p && p->Group) {
            p->Group = FALSE;
            DWORD end[2] = {GDICOMMENT_IDENTIFIER, GDICOMMENT_ENDGROUP};
            return MfComment(dc, sizeof(end), (const BYTE *)end);
        }
        return TRUE;
    case EMR_POLYBEZIER:
    case EMR_POLYGON:
    case EMR_POLYLINE:
    case EMR_POLYBEZIERTO:
    case EMR_POLYLINETO:
        return play_poly(dc, r, FALSE);
    case EMR_POLYBEZIER16:
    case EMR_POLYGON16:
    case EMR_POLYLINE16:
    case EMR_POLYBEZIERTO16:
    case EMR_POLYLINETO16:
        return play_poly(dc, r, TRUE);
    case EMR_POLYPOLYLINE:
    case EMR_POLYPOLYGON:
        return play_polypoly(dc, r, FALSE);
    case EMR_POLYPOLYLINE16:
    case EMR_POLYPOLYGON16:
        return play_polypoly(dc, r, TRUE);
    case EMR_POLYDRAW:
        return play_polydraw(dc, r, FALSE);
    case EMR_POLYDRAW16:
        return play_polydraw(dc, r, TRUE);
    case EMR_SETMAPMODE:
    case EMR_SETWINDOWEXTEX:
    case EMR_SETWINDOWORGEX:
    case EMR_SETVIEWPORTEXTEX:
    case EMR_SETVIEWPORTORGEX:
    case EMR_SCALEVIEWPORTEXTEX:
    case EMR_SCALEWINDOWEXTEX:
    case EMR_SETWORLDTRANSFORM:
    case EMR_MODIFYWORLDTRANSFORM:
        return play_mapping(dc, p, r);
    case EMR_SETBRUSHORGEX:
        return SetBrushOrgEx(dc, (int)d[0], (int)d[1], nullptr);
    case EMR_SETPIXELV:
        return SetPixelV(dc, (int)d[0], (int)d[1], d[2]);
    case EMR_SETMAPPERFLAGS:
        return SetMapperFlags(dc, d[0]) != GDI_ERROR;
    case EMR_SETBKMODE:
        return SetBkMode(dc, (int)d[0]) != 0;
    case EMR_SETPOLYFILLMODE:
        return SetPolyFillMode(dc, (int)d[0]) != 0;
    case EMR_SETROP2:
        return SetROP2(dc, (int)d[0]) != 0;
    case EMR_SETSTRETCHBLTMODE:
        return SetStretchBltMode(dc, (int)d[0]) != 0;
    case EMR_SETTEXTALIGN:
        return SetTextAlign(dc, d[0]) != GDI_ERROR;
    case EMR_SETCOLORADJUSTMENT:
        return fits(r, 8, sizeof(COLORADJUSTMENT)) && SetColorAdjustment(dc, (const COLORADJUSTMENT *)d);
    case EMR_SETTEXTCOLOR:
        return SetTextColor(dc, d[0]) != CLR_INVALID;
    case EMR_SETBKCOLOR:
        return SetBkColor(dc, d[0]) != CLR_INVALID;
    case EMR_OFFSETCLIPRGN:
        return OffsetClipRgn(dc, (int)d[0], (int)d[1]) != ERROR;
    case EMR_MOVETOEX:
        return MoveToEx(dc, (int)d[0], (int)d[1], nullptr);
    case EMR_SETMETARGN:
        return SetMetaRgn(dc) != ERROR;
    case EMR_EXCLUDECLIPRECT:
        return ExcludeClipRect(dc, (int)d[0], (int)d[1], (int)d[2], (int)d[3]) != ERROR;
    case EMR_INTERSECTCLIPRECT:
        return IntersectClipRect(dc, (int)d[0], (int)d[1], (int)d[2], (int)d[3]) != ERROR;
    case EMR_SAVEDC:
        if (p) {
            if (!SaveDC(p->Virtual))
                return FALSE;
            if (!SaveDC(dc)) {
                RestoreDC(p->Virtual, -1);
                return FALSE;
            }
            p->Saves++;
            return TRUE;
        }
        return SaveDC(dc) != 0;
    case EMR_RESTOREDC: {
        int rel = (int)d[0];
        if (!p)
            return RestoreDC(dc, rel);
        if (rel > 0 || !RestoreDC(p->Virtual, rel))
            return FALSE;
        p->Saves += rel;
        return RestoreDC(dc, rel);
    }
    case EMR_SELECTOBJECT: {
        HGDIOBJ object = table_object(table, handles, d[0]);
        return object && SelectObject(dc, object) != nullptr;
    }
    case EMR_CREATEPEN: {
        const EMRCREATEPEN *c = (const EMRCREATEPEN *)r;
        HPEN pen = CreatePenIndirect(&c->lopn);
        table_set(table, handles, c->ihPen, pen);
        return pen != nullptr;
    }
    case EMR_CREATEBRUSHINDIRECT: {
        const EMRCREATEBRUSHINDIRECT *c = (const EMRCREATEBRUSHINDIRECT *)r;
        LOGBRUSH lb = {c->lb.lbStyle, c->lb.lbColor, c->lb.lbHatch};
        HBRUSH brush = lb.lbStyle == BS_HATCHED ? CreateHatchBrush((int)lb.lbHatch, lb.lbColor)
                                                : CreateBrushIndirect(&lb);
        table_set(table, handles, c->ihBrush, brush);
        return brush != nullptr;
    }
    case EMR_DELETEOBJECT: {
        DWORD i = d[0];
        if (i & ENHMETA_STOCK_OBJECT)
            return TRUE;
        if (!table || !i || i >= handles)
            return FALSE;
        BOOL ok = table->objectHandle[i] && DeleteObject(table->objectHandle[i]);
        table->objectHandle[i] = nullptr;
        return ok;
    }
    case EMR_ANGLEARC: {
        const EMRANGLEARC *a = (const EMRANGLEARC *)r;
        return AngleArc(dc, a->ptlCenter.x, a->ptlCenter.y, a->nRadius, a->eStartAngle, a->eSweepAngle);
    }
    case EMR_ELLIPSE:
        return Ellipse(dc, (int)d[0], (int)d[1], (int)d[2], (int)d[3]);
    case EMR_RECTANGLE:
        return Rectangle(dc, (int)d[0], (int)d[1], (int)d[2], (int)d[3]);
    case EMR_ROUNDRECT:
        return fits(r, 0, sizeof(EMRROUNDRECT)) &&
               RoundRect(dc, (int)d[0], (int)d[1], (int)d[2], (int)d[3], (int)d[4], (int)d[5]);
    case EMR_ARC:
        return Arc(dc, (int)d[0], (int)d[1], (int)d[2], (int)d[3], (int)d[4], (int)d[5], (int)d[6], (int)d[7]);
    case EMR_ARCTO:
        return ArcTo(dc, (int)d[0], (int)d[1], (int)d[2], (int)d[3], (int)d[4], (int)d[5], (int)d[6], (int)d[7]);
    case EMR_CHORD:
        return Chord(dc, (int)d[0], (int)d[1], (int)d[2], (int)d[3], (int)d[4], (int)d[5], (int)d[6], (int)d[7]);
    case EMR_PIE:
        return Pie(dc, (int)d[0], (int)d[1], (int)d[2], (int)d[3], (int)d[4], (int)d[5], (int)d[6], (int)d[7]);
    case EMR_SELECTPALETTE: {
        HGDIOBJ palette = table_object(table, handles, d[0]);
        return palette && SelectPalette(dc, palette, TRUE) != nullptr;
    }
    case EMR_CREATEPALETTE: {
        const EMRCREATEPALETTE *c = (const EMRCREATEPALETTE *)r;
        if (!fits(r, offsetof(EMRCREATEPALETTE, lgpl.palPalEntry), c->lgpl.palNumEntries * 4u))
            return FALSE;
        HPALETTE palette = CreatePalette(&c->lgpl);
        table_set(table, handles, c->ihPal, palette);
        return palette != nullptr;
    }
    case EMR_SETPALETTEENTRIES: {
        if (!fits(r, 20, 0) || !fits(r, 20, d[2] * 4))
            return FALSE;
        HGDIOBJ palette = table_object(table, handles, d[0]);
        return palette && SetPaletteEntries(palette, d[1], d[2], (const PALETTEENTRY *)(d + 3));
    }
    case EMR_RESIZEPALETTE:
    case EMR_SETICMMODE:
    case EMR_CREATECOLORSPACE:
    case EMR_SETCOLORSPACE:
    case EMR_DELETECOLORSPACE:
    case EMR_GLSRECORD:
    case EMR_GLSBOUNDEDRECORD:
    case EMR_PIXELFORMAT:
    case EMR_DRAWESCAPE:
    case EMR_EXTESCAPE:
    case EMR_FORCEUFIMAPPING:
    case EMR_NAMEDESCAPE:
    case EMR_COLORCORRECTPALETTE:
    case EMR_SETICMPROFILEA:
    case EMR_SETICMPROFILEW:
    case EMR_SETLINKEDUFIS:
    case EMR_COLORMATCHTOTARGETW:
    case EMR_CREATECOLORSPACEW:
        return TRUE;
    case EMR_REALIZEPALETTE:
        RealizePalette(dc);
        return TRUE;
    case EMR_EXTFLOODFILL:
        return ExtFloodFill(dc, (int)d[0], (int)d[1], d[2], d[3]);
    case EMR_LINETO:
        return LineTo(dc, (int)d[0], (int)d[1]);
    case EMR_SETARCDIRECTION:
        return SetArcDirection(dc, (int)d[0]) != 0;
    case EMR_SETMITERLIMIT:
        return SetMiterLimit(dc, (FLOAT)(LONG)d[0], nullptr);
    case EMR_BEGINPATH:
        return BeginPath(dc);
    case EMR_ENDPATH:
        return EndPath(dc);
    case EMR_CLOSEFIGURE:
        return CloseFigure(dc);
    case EMR_FILLPATH:
        return FillPath(dc);
    case EMR_STROKEANDFILLPATH:
        return StrokeAndFillPath(dc);
    case EMR_STROKEPATH:
        return StrokePath(dc);
    case EMR_FLATTENPATH:
        return FlattenPath(dc);
    case EMR_WIDENPATH:
        return WidenPath(dc);
    case EMR_SELECTCLIPPATH:
        return SelectClipPath(dc, (int)d[0]);
    case EMR_ABORTPATH:
        return AbortPath(dc);
    case EMR_GDICOMMENT:
        return play_comment(dc, r);
    case EMR_FILLRGN:
    case EMR_FRAMERGN:
    case EMR_INVERTRGN:
    case EMR_PAINTRGN:
        return play_region(dc, table, handles, r);
    case EMR_EXTSELECTCLIPRGN:
        return play_clip_region(dc, p, r);
    case EMR_BITBLT:
    case EMR_STRETCHBLT:
    case EMR_MASKBLT:
    case EMR_ALPHABLEND:
    case EMR_TRANSPARENTBLT:
        return play_blt(dc, r);
    case EMR_PLGBLT:
        return TRUE;
    case EMR_SETDIBITSTODEVICE:
        return play_setdibits(dc, p, r);
    case EMR_STRETCHDIBITS:
        return play_stretchdibits(dc, r);
    case EMR_EXTCREATEFONTINDIRECTW: {
        const EMREXTCREATEFONTINDIRECTW *c = (const EMREXTCREATEFONTINDIRECTW *)r;
        HFONT font = CreateFontIndirectW(&c->elfw);
        table_set(table, handles, c->ihFont, font);
        return font != nullptr;
    }
    case EMR_EXTTEXTOUTA:
    case EMR_EXTTEXTOUTW:
        return play_text(dc, r, &((const EMREXTTEXTOUTW *)r)->emrtext, r->iType == EMR_EXTTEXTOUTW);
    case EMR_POLYTEXTOUTA:
    case EMR_POLYTEXTOUTW: {
        if (!fits(r, 0, offsetof(EMRPOLYTEXTOUTW, aemrtext)))
            return FALSE;
        const EMRPOLYTEXTOUTW *t = (const EMRPOLYTEXTOUTW *)r;
        LONG strings = t->cStrings;
        if (strings < 0 || !fits(r, offsetof(EMRPOLYTEXTOUTW, aemrtext), (DWORD)strings * sizeof(EMRTEXT)))
            return FALSE;
        BOOL ok = TRUE;
        for (LONG i = 0; i < strings; i++)
            ok &= play_text(dc, r, &t->aemrtext[i], r->iType == EMR_POLYTEXTOUTW);
        return ok;
    }
    case EMR_SMALLTEXTOUT:
        return play_smalltext(dc, r);
    case EMR_CREATEMONOBRUSH:
    case EMR_CREATEDIBPATTERNBRUSHPT:
        return play_dib_brush(table, handles, r);
    case EMR_EXTCREATEPEN:
        return play_extpen(table, handles, r);
    case EMR_SETLAYOUT:
        SetLayout(dc, d[0]);
        return TRUE;
    case EMR_GRADIENTFILL: {
        const EMRGRADIENTFILL *g = (const EMRGRADIENTFILL *)r;
        DWORD mesh = g->ulMode == GRADIENT_FILL_TRIANGLE ? sizeof(GRADIENT_TRIANGLE) : sizeof(GRADIENT_RECT);
        if (g->nVer > 0x100000 || g->nTri > 0x100000 ||
            !fits(r, offsetof(EMRGRADIENTFILL, Ver), g->nVer * sizeof(TRIVERTEX) + g->nTri * mesh))
            return FALSE;
        return GdiGradientFill(dc, (PTRIVERTEX)g->Ver, g->nVer, (PVOID)(g->Ver + g->nVer), g->nTri, g->ulMode);
    }
    case EMR_SETTEXTJUSTIFICATION:
        return fits(r, 0, 16) && SetTextJustification(dc, (int)d[0], (int)d[1]);
    }
    /* records gdi32 does not know: a metafile dc keeps them, other dcs skip them. */
    if (r->iType & 0x80000000)
        return TRUE;
    EMF_DC *emf = MfActive(dc) && !MfIsWmfDc(dc) ? EmfDc(dc) : nullptr;
    if (emf && r->nSize > 8) {
        BYTE *copy = EmfRecordAlloc(emf, r->iType, r->nSize);
        if (!copy)
            return FALSE;
        memcpy(copy + 8, r->dParm, r->nSize - 8);
    }
    return TRUE;
}

/* ------------------------------------------------------------------ playback */

/* resets the destination before playback, like windows. */
static void reset_dc(HDC dc)
{
    SelectObject(dc, GetStockObject(WHITE_BRUSH));
    SelectObject(dc, GetStockObject(BLACK_PEN));
    SelectObject(dc, GetStockObject(DEVICE_DEFAULT_FONT));
    SelectPalette(dc, GetStockObject(DEFAULT_PALETTE), TRUE);
    SetBkColor(dc, RGB(255, 255, 255));
    SetTextColor(dc, RGB(0, 0, 0));
    SetTextCharacterExtra(dc, 0);
    SetBkMode(dc, OPAQUE);
    SetPolyFillMode(dc, ALTERNATE);
    SetROP2(dc, R2_COPYPEN);
    SetStretchBltMode(dc, BLACKONWHITE);
    SetTextAlign(dc, TA_LEFT | TA_TOP | TA_NOUPDATECP);
    SetMapperFlags(dc, 0);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    SetMiterLimit(dc, 10.0f, nullptr);
    SetTextJustification(dc, 0, 0);
    SetArcDirection(dc, AD_COUNTERCLOCKWISE);
    MoveToEx(dc, 0, 0, nullptr);
}

/* the frame mapping: reference device units to hundredths of millimeters to the rectangle, then to the device. */
static BOOL base_transform(HDC dc, const ENHMETAHEADER *h, const RECT *rect, XFORM *out)
{
    const RECTL *f = &h->rclFrame;
    double sx = f->right != f->left ? (double)(rect->right - rect->left) / (f->right - f->left)
                                    : (double)(rect->right - rect->left + 1);
    double sy = f->bottom != f->top ? (double)(rect->bottom - rect->top) / (f->bottom - f->top)
                                    : (double)(rect->bottom - rect->top + 1);
    double mx = h->szlDevice.cx ? 100.0 * h->szlMillimeters.cx / h->szlDevice.cx : 1;
    double my = h->szlDevice.cy ? 100.0 * h->szlMillimeters.cy / h->szlDevice.cy : 1;
    XFORM frame = {(FLOAT)(mx * sx), 0, 0, (FLOAT)(my * sy), (FLOAT)(rect->left - f->left * sx),
                   (FLOAT)(rect->top - f->top * sy)};
    XFORM device;
    if (!NtGdiGetTransform(dc, MIME_XFORM_WORLD_TO_DEVICE, &device))
        return FALSE;
    multiply(out, &frame, &device);
    /* close enough to one is one, so unscaled playback stays pixel exact. */
    if (!out->eM12 && !out->eM21 && out->eM11 >= 0.999f && out->eM11 <= 1.001f && out->eM22 >= 0.999f &&
        out->eM22 <= 1.001f) {
        out->eM11 = 1;
        out->eM22 = 1;
    }
    return TRUE;
}

static BOOL play(HDC dc, HENHMETAFILE handle, ENHMFENUMPROC proc, LPVOID param, const RECT *rect)
{
    EMF *emf = EmfLookup(handle);
    if (!emf)
        return FALSE;
    const ENHMETAHEADER *h = (const ENHMETAHEADER *)emf->Data;
    if (dc && !rect) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (emf->Play) {
        /* one playback of a metafile at a time. */
        SetLastError(ERROR_BUSY);
        return FALSE;
    }
    UINT handles = h->nHandles;
    HANDLETABLE *table = heap_zalloc(((SIZE_T)handles + 1) * sizeof(HGDIOBJ));
    EMF_PLAY *p = heap_zalloc(sizeof(EMF_PLAY));
    if (!table || !p) {
        heap_free(table);
        heap_free(p);
        return FALSE;
    }
    table->objectHandle[0] = handle;
    p->Dc = dc;
    BOOL ok = TRUE, setup = FALSE;
    if (dc) {
        if (h->rclFrame.right < h->rclFrame.left || h->rclFrame.bottom < h->rclFrame.top) {
            SetLastError(ERROR_INVALID_DATA);
            heap_free(table);
            heap_free(p);
            return FALSE;
        }
        if (!SaveDC(dc)) {
            heap_free(table);
            heap_free(p);
            return FALSE;
        }
        setup = TRUE;
        SetGraphicsMode(dc, GM_ADVANCED);
        SetMetaRgn(dc);
        reset_dc(dc);
        ok = base_transform(dc, h, rect, &p->Base);
        if (ok) {
            SetMapMode(dc, MM_TEXT);
            SetWindowOrgEx(dc, 0, 0, nullptr);
            SetViewportOrgEx(dc, 0, 0, nullptr);
            ok = SetWorldTransform(dc, &p->Base);
        }
        p->Virtual = ok ? CreateCompatibleDC(nullptr) : nullptr;
        ok = ok && p->Virtual;
        if (ok) {
            NtGdiSetVirtualResolution(p->Virtual, h->szlDevice.cx, h->szlDevice.cy, h->szlMillimeters.cx,
                                      h->szlMillimeters.cy);
            SetGraphicsMode(p->Virtual, GM_ADVANCED);
        }
        RECT clip;
        if (ok && GetClipBox(dc, &clip) == NULLREGION && !MfActive(dc)) {
            /* nothing of the destination is visible. */
            DeleteDC(p->Virtual);
            RestoreDC(dc, -1);
            heap_free(table);
            heap_free(p);
            return TRUE;
        }
        if (!ok) {
            if (p->Virtual)
                DeleteDC(p->Virtual);
            RestoreDC(dc, -1);
            heap_free(table);
            heap_free(p);
            return FALSE;
        }
    }
    emf->Play = p;
    plays_add(p);
    DWORD offset = 0;
    BOOL ended = FALSE, stopped = FALSE;
    const ENHMETARECORD *eof = nullptr;
    while (offset + 8 <= h->nBytes && offset + 8 <= emf->Size) {
        const ENHMETARECORD *r = (const ENHMETARECORD *)(emf->Data + offset);
        if (r->nSize < 8 || (r->nSize & 3) || r->nSize > h->nBytes - offset) {
            ok = FALSE;
            stopped = TRUE;
            break;
        }
        if (r->iType == EMR_EOF) {
            eof = r;
            ended = TRUE;
            break;
        }
        if (p->Cancel) {
            ok = FALSE;
            stopped = TRUE;
            break;
        }
        if (proc) {
            if (!proc(dc, table, r, (int)handles, (LPARAM)param)) {
                ok = FALSE;
                stopped = TRUE;
                break;
            }
        } else if (!PlayEnhMetaFileRecord(dc, table, r, handles)) {
            ok = FALSE;
        }
        offset += r->nSize;
    }
    if (!stopped) {
        /* states the metafile left saved are restored, as a record of their own. */
        if (p->Saves > 0) {
            EMRRESTOREDC restore = {{EMR_RESTOREDC, sizeof(EMRRESTOREDC)}, -p->Saves};
            if (proc) {
                if (!proc(dc, table, (const ENHMETARECORD *)&restore, (int)handles, (LPARAM)param))
                    ok = FALSE;
            } else if (!PlayEnhMetaFileRecord(dc, table, (const ENHMETARECORD *)&restore, handles)) {
                ok = FALSE;
            }
        }
        if (ended) {
            if (proc) {
                if (!proc(dc, table, eof, (int)handles, (LPARAM)param))
                    ok = FALSE;
                if (p->Group)
                    PlayEnhMetaFileRecord(dc, table, eof, handles);
            } else if (!PlayEnhMetaFileRecord(dc, table, eof, handles)) {
                ok = FALSE;
            }
        }
    }
    plays_remove(p);
    emf->Play = nullptr;
    if (setup) {
        RestoreDC(dc, -1);
        DeleteDC(p->Virtual);
    }
    for (UINT i = 1; i < handles; i++)
        if (table->objectHandle[i])
            DeleteObject(table->objectHandle[i]);
    heap_free(table);
    heap_free(p);
    return ok;
}

WINGDIAPI BOOL WINAPI PlayEnhMetaFile(HDC dc, HENHMETAFILE handle, const RECT *rect)
{
    if (!dc) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return play(dc, handle, nullptr, nullptr, rect);
}

WINGDIAPI BOOL WINAPI EnumEnhMetaFile(HDC dc, HENHMETAFILE handle, ENHMFENUMPROC proc, LPVOID param, const RECT *rect)
{
    if (!proc) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return play(dc, handle, proc, param, rect);
}
