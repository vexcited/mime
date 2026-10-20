/*
 * metafiles as images: the object, loading from handles, files and streams, the header every kind of metafile
 * reports, the conversion of windows metafiles, and the questions an image can be asked.
 *
 * playing is in emfplay.c and recording in emfrecord.c, both described in docs/gdiplus.md under Metafiles.
 */
#include "meta_private.h"

static DWORD rd32(const BYTE *p)
{
    DWORD v;
    memcpy(&v, p, 4);
    return v;
}

static WORD rd16(const BYTE *p)
{
    WORD v;
    memcpy(&v, p, 2);
    return v;
}

static INT iabs(INT v) { return v < 0 ? -v : v; }
static INT min_int(INT a, INT b) { return a < b ? a : b; }
static INT max_int(INT a, INT b) { return a > b ? a : b; }

/* ------------------------------------------------------------------ the object */

GMetafile *gp_meta_cast(const void *p)
{
    const GImage *i = p;
    if (!i || i->tag != GP_TAG_METAFILE || i->type != ImageTypeMetafile)
        return nullptr;
    return (GMetafile *)p;
}

GMetafile *gp_meta_new(void)
{
    GMetafile *m = gp_alloc_zero(sizeof(*m));
    if (!m)
        return nullptr;
    m->image.tag = GP_TAG_METAFILE;
    m->image.type = ImageTypeMetafile;
    m->header.Version = EMFPLUS_VERSION;
    return m;
}

static void release_handle(GMetafile *m)
{
    if (m->handle && m->owns_handle) {
        if (m->header.Type < MetafileTypeEmf)
            DeleteMetaFile((HMETAFILE)m->handle);
        else
            DeleteEnhMetaFile((HENHMETAFILE)m->handle);
    }
    m->handle = nullptr;
}

void gp_meta_free(GMetafile *m)
{
    if (!m)
        return;
    gp_meta_recorder_free(m);
    gp_meta_player_free(m);
    release_handle(m);
    gp_free(m->bytes);
    gp_free(m->file_name);
    if (m->stream)
        m->stream->lpVtbl->Release(m->stream);
    m->image.tag = 0;
    gp_free(m);
}

/* the bytes of the enhanced metafile, read from the handle once. */
const BYTE *gp_meta_bytes(GMetafile *m, DWORD *size)
{
    if (!m->bytes) {
        if (!m->handle || m->header.Type < MetafileTypeEmf)
            return nullptr;
        UINT n = GetEnhMetaFileBits((HENHMETAFILE)m->handle, 0, nullptr);
        if (!n)
            return nullptr;
        BYTE *buf = gp_alloc(n);
        if (!buf)
            return nullptr;
        if (GetEnhMetaFileBits((HENHMETAFILE)m->handle, n, buf) != n) {
            gp_free(buf);
            return nullptr;
        }
        m->bytes = buf;
        m->size = n;
    }
    *size = m->size;
    return m->bytes;
}

/* ------------------------------------------------------------------ headers */

static BOOL wmf_header_valid(const BYTE *h)
{
    WORD type = rd16(h), size = rd16(h + 2), version = rd16(h + 4);
    return (type == 1 || type == 2) && size == 9 && (version == 0x100 || version == 0x300);
}

INT16 gp_wmf_placeable_checksum(const WmfPlaceableFileHeader *h)
{
    const BYTE *p = (const BYTE *)h;
    WORD sum = 0;
    for (INT i = 0; i < 10; i++)
        sum ^= rd16(p + i * 2);
    return (INT16)sum;
}

/* the placeable header is read from its fields, its size in memory has padding the file does not have. */
static void placeable_bytes(const WmfPlaceableFileHeader *h, BYTE out[PLACEABLE_SIZE])
{
    memcpy(out, &h->Key, 4);
    memcpy(out + 4, &h->Hmf, 2);
    memcpy(out + 6, &h->BoundingBox, 8);
    memcpy(out + 14, &h->Inch, 2);
    memcpy(out + 16, &h->Reserved, 4);
    memcpy(out + 20, &h->Checksum, 2);
}

/* an enhanced metafile header is good when it says what it is and describes something that can be played. */
static BOOL emf_header_valid(const BYTE *h)
{
    return rd32(h) == 1 && rd32(h + 40) == 0x464d4520 && rd32(h + 4) >= EMF_HEADER3_SIZE && rd16(h + 56) != 0 &&
           rd32(h + 52) >= 2 && (rd32(h + 48) & 3) == 0 && (INT)rd32(h + 72) > 0 && (INT)rd32(h + 76) > 0 &&
           (INT)rd32(h + 80) > 0 && (INT)rd32(h + 84) > 0;
}

/* the 22 bytes of a placeable header: the key, a checksum that xors the words before it to the stored one, a box. */
static BOOL placeable_bytes_valid(const BYTE *p)
{
    WORD sum = 0;
    for (INT i = 0; i < 10; i++)
        sum ^= rd16(p + 2 * i);
    return rd32(p) == PLACEABLE_KEY && sum == rd16(p + 20) && (INT16)rd16(p + 6) != (INT16)rd16(p + 10) &&
           (INT16)rd16(p + 8) != (INT16)rd16(p + 12);
}

BOOL gp_wmf_placeable_valid(const WmfPlaceableFileHeader *h)
{
    BYTE bytes[PLACEABLE_SIZE];
    placeable_bytes(h, bytes);
    return placeable_bytes_valid(bytes);
}

/* the record of the emf+ header in the second record of an enhanced metafile, when there is one. */
static BOOL find_plus_header(const BYTE *bytes, DWORD size, const BYTE **plus)
{
    if (size < EMF_HEADER3_SIZE || rd32(bytes + 52) <= 2)
        return FALSE;
    DWORD off = rd32(bytes + 4);
    if (off > size || size - off < 44)
        return FALSE;
    const BYTE *r = bytes + off;
    if (rd32(r) != EMR_GDICOMMENT || rd32(r + 4) < 0x2c || rd32(r + 12) != EMFPLUS_SIGNATURE)
        return FALSE;
    *plus = r + 16;
    return TRUE;
}

/* a windows metafile with a placeable header: the box in the header is the size. */
static void fill_wmf_header(MetafileHeader *h, const BYTE *wmf, const BYTE *placeable)
{
    memset(h, 0, sizeof(*h));
    h->Type = MetafileTypeWmfPlaceable;
    h->Size = 2 * rd32(wmf + 6);
    h->Version = rd16(wmf + 4);
    memcpy(&h->WmfHeader, wmf, WMF_HEADER_SIZE);
    INT16 inch = (INT16)rd16(placeable + 14);
    h->DpiX = h->DpiY = inch > 0 ? (float)inch : 1440.0f;
    INT16 l = (INT16)rd16(placeable + 6), t = (INT16)rd16(placeable + 8);
    INT16 r = (INT16)rd16(placeable + 10), b = (INT16)rd16(placeable + 12);
    h->X = l < r ? l : r;
    h->Width = iabs((INT)r - l);
    h->Y = t < b ? t : b;
    h->Height = iabs((INT)b - t);
}

/*
 * the header the sdk reports for an enhanced metafile, from its 88 byte header and the first emf+ record when the
 * metafile starts with one. the header is filled even when it is refused for a frame that is empty.
 */
static GpStatus fill_emf_header(MetafileHeader *h, const BYTE *eh, const BYTE *plus)
{
    memset(h, 0, sizeof(*h));
    BOOL has_plus = plus && rd32(plus + 4) >= EMFPLUS_HEADER_RECORD && rd16(plus) == 0x4001 &&
                    rd32(plus + 8) == rd32(plus + 4) - 12 && (rd32(plus + 12) & EMFPLUS_VERSION_MASK) == EMFPLUS_VERSION_BASE &&
                    (INT)rd32(plus + 20) > 0 && (INT)rd32(plus + 24) > 0;
    if (has_plus) {
        h->Type = (MetafileType)(MetafileTypeEmfPlusOnly + (rd16(plus + 2) & 1));
        h->EmfPlusHeaderSize = (INT)rd32(plus + 4);
        h->Version = rd32(plus + 12);
        h->EmfPlusFlags = rd32(plus + 16);
        h->LogicalDpiX = (INT)rd32(plus + 20);
        h->LogicalDpiY = (INT)rd32(plus + 24);
    } else {
        h->Type = MetafileTypeEmf;
        h->Version = rd32(eh + 44);
    }
    h->Size = rd32(eh + 48);
    /* pixels per hundredth of a millimeter on the device of the metafile */
    float px = (float)(INT)rd32(eh + 72) / (float)(INT)rd32(eh + 80);
    float py = (float)(INT)rd32(eh + 76) / (float)(INT)rd32(eh + 84);
    h->DpiX = px * 25.4f;
    h->DpiY = py * 25.4f;
    float kx = px * 0.01f, ky = py * 0.01f;
    INT x0 = (INT)rd32(eh + 24), y0 = (INT)rd32(eh + 28), x1 = (INT)rd32(eh + 32), y1 = (INT)rd32(eh + 36);
    INT left = min_int(x0, x1), right = max_int(x0, x1), top = min_int(y0, y1), bottom = max_int(y0, y1);
    h->X = (INT)mime_vec_floorf((float)left * kx + 0.5f);
    h->Y = (INT)mime_vec_floorf((float)top * ky + 0.5f);
    h->Width = (INT)mime_vec_floorf(((float)(right - left) * kx + 1.0f) + 0.5f);
    h->Height = (INT)mime_vec_floorf(((float)(bottom - top) * ky + 1.0f) + 0.5f);
    memcpy(&h->EmfHeader, eh, EMF_HEADER3_SIZE);
    return h->Width && h->Height ? Ok : InvalidParameter;
}

/* the bytes of an enhanced metafile from its handle, for the header and the emf+ record in it. */
static BYTE *emf_bits(HENHMETAFILE hemf, UINT *size)
{
    UINT n = GetEnhMetaFileBits(hemf, 0, nullptr);
    if (!n)
        return nullptr;
    BYTE *buf = gp_alloc(n);
    if (!buf)
        return nullptr;
    if (GetEnhMetaFileBits(hemf, n, buf) != n) {
        gp_free(buf);
        return nullptr;
    }
    *size = n;
    return buf;
}

GpStatus gp_meta_header_of_emf(HENHMETAFILE hemf, MetafileHeader *out, BOOL *damaged)
{
    memset(out, 0, sizeof(*out));
    if (damaged)
        *damaged = FALSE;
    UINT n;
    BYTE *bits = emf_bits(hemf, &n);
    if (!bits || n < EMF_HEADER3_SIZE || !emf_header_valid(bits)) {
        gp_free(bits);
        return InvalidParameter;
    }
    const BYTE *plus = nullptr;
    find_plus_header(bits, n, &plus);
    GpStatus st = fill_emf_header(out, bits, plus);
    if (st != Ok && damaged)
        *damaged = TRUE;
    gp_free(bits);
    return st;
}

/* the header of the windows metafile and its type, as the first 18 bytes of its bits. */
static BOOL wmf_first_bytes(HMETAFILE wmf, BYTE out[WMF_HEADER_SIZE])
{
    UINT n = GetMetaFileBitsEx(wmf, 0, nullptr);
    if (n < WMF_HEADER_SIZE)
        return FALSE;
    BYTE *bits = gp_alloc(n);
    if (!bits)
        return FALSE;
    BOOL ok = GetMetaFileBitsEx(wmf, n, bits) == n;
    if (ok)
        memcpy(out, bits, WMF_HEADER_SIZE);
    gp_free(bits);
    return ok;
}

/*
 * the 18 byte header of a windows metafile as the sdk takes it: the one in the bits when it is a good one, else one
 * that is made up from the size of the bits, a version 3 header of 9 words with nothing else set.
 */
static void wmf_header_or_made_up(HMETAFILE wmf, BYTE out[WMF_HEADER_SIZE])
{
    if (wmf_first_bytes(wmf, out) && wmf_header_valid(out))
        return;
    UINT bytes = GetMetaFileBitsEx(wmf, 0, nullptr);
    memset(out, 0, WMF_HEADER_SIZE);
    WORD kind = 1, header_words = 9, version = 0x300;
    DWORD words = bytes / 2;
    memcpy(out, &kind, 2);
    memcpy(out + 2, &header_words, 2);
    memcpy(out + 4, &version, 2);
    memcpy(out + 6, &words, 4);
}

/* ------------------------------------------------------------------ windows metafiles to enhanced ones */

/*
 * a windows metafile can carry an enhanced one in its comment records, cut into pieces that follow each other. the
 * pieces are put together when the sum of all the 16 bit words of the file is zero, as the checksum makes it.
 *
 * a piece is a record of words: the size in words, the function 0x0626 of an escape, the escape 15 that is a comment,
 * the identifier 'WMFC' at byte 10 and the comment type 1 at byte 14. a flags field at byte 24 is zero, the length of
 * the piece is at byte 32, the length of the whole enhanced metafile at byte 40, and the data starts at byte 44.
 */
static BOOL is_piece(const BYTE *r, DWORD room)
{
    if (room < 44)
        return FALSE;
    return rd32(r) > 0x16 && rd16(r + 4) == 0x626 && rd16(r + 6) == 15 && rd32(r + 10) == 0x43464d57 &&
           rd32(r + 14) == 1;
}

/*
 * what the windows metafile has as its own size: the window it sets with its origin and extent records, or
 * failing that the first stretched blit. a guess at the full rules, which also look at the mapping mode.
 */
typedef struct WmfScan {
    BOOL have_org, have_ext, have_blit;
    INT org_x, org_y, ext_x, ext_y;
    INT blit_x, blit_y, blit_cx, blit_cy;
} WmfScan;

static BOOL wmf_scan(const BYTE *bits, UINT size, WmfScan *s)
{
    memset(s, 0, sizeof(*s));
    UINT pos = WMF_HEADER_SIZE;
    while (pos + 6 <= size) {
        DWORD words = rd32(bits + pos);
        WORD fn = rd16(bits + pos + 4);
        if (words < 3 || (UINT64)pos + words * 2ull > size)
            break;
        const BYTE *p = bits + pos + 6;
        UINT bytes = words * 2 - 6;
        switch (fn) {
        case 0x020b: /* the origin is y then x */
            if (bytes >= 4 && !s->have_org) {
                s->org_y = (INT16)rd16(p);
                s->org_x = (INT16)rd16(p + 2);
                s->have_org = TRUE;
            }
            break;
        case 0x020c:
            if (bytes >= 4 && !s->have_ext) {
                s->ext_y = (INT16)rd16(p);
                s->ext_x = (INT16)rd16(p + 2);
                s->have_ext = TRUE;
            }
            break;
        case 0x0b23: /* stretch blit: rop, source size, source origin, destination size and origin */
            if (bytes >= 22 && !s->have_blit) {
                s->blit_cy = (INT16)rd16(p + 12);
                s->blit_cx = (INT16)rd16(p + 14);
                s->blit_y = (INT16)rd16(p + 16);
                s->blit_x = (INT16)rd16(p + 18);
                s->have_blit = TRUE;
            }
            break;
        default:
            break;
        }
        if (fn == 0)
            break;
        pos += words * 2;
    }
    if (s->have_ext && s->ext_x && s->ext_y)
        return TRUE;
    if (s->have_blit && s->blit_cx && s->blit_cy) {
        s->org_x = s->blit_x;
        s->org_y = s->blit_y;
        s->ext_x = s->blit_cx;
        s->ext_y = s->blit_cy;
        return TRUE;
    }
    return FALSE;
}

static INT round_to_int(float v) { return (INT)mime_vec_floorf(v + 0.5f); }

static HENHMETAFILE emf_from_comments(const BYTE *bits, UINT size)
{
    if (size < WMF_HEADER_SIZE + 44)
        return nullptr;
    const BYTE *first = bits + WMF_HEADER_SIZE;
    DWORD room = size - WMF_HEADER_SIZE;
    if (!is_piece(first, room) || rd32(first + 24) != 0)
        return nullptr;
    WORD sum = 0;
    for (UINT i = 0; i + 1 < size; i += 2)
        sum = (WORD)(sum + rd16(bits + i));
    DWORD total = rd32(first + 40);
    if (sum != 0 || total == 0)
        return nullptr;
    BYTE *whole = gp_alloc(total);
    if (!whole)
        return nullptr;
    DWORD got = 0;
    const BYTE *r = first;
    for (;;) {
        DWORD piece = rd32(r + 32), record_bytes = 2 * rd32(r);
        BOOL bad = got + piece < got || got + piece > total || record_bytes > room || piece >= 0xffffffd4u ||
                   piece + 44 > record_bytes;
        if (bad) {
            gp_free(whole);
            return nullptr;
        }
        memcpy(whole + got, r + 44, piece);
        got += piece;
        room -= record_bytes;
        r += record_bytes;
        if (!is_piece(r, room))
            break;
    }
    HENHMETAFILE emf = got == total ? SetEnhMetaFileBits(total, whole) : nullptr;
    gp_free(whole);
    return emf;
}

/*
 * the enhanced metafile that stands for the records of a windows metafile: the one it carries when it does, else
 * the records played into a new enhanced metafile on the screen. the size of the picture comes from the placeable
 * box, which is in inches of the box's own unit, or from the window the records set, or is the whole screen.
 */
HENHMETAFILE gp_emf_from_wmf_data(HMETAFILE wmf, const WmfPlaceableFileHeader *placeable, const BYTE *bits, UINT size,
                                  INT compat)
{
    if (!wmf || !bits || size < WMF_HEADER_SIZE)
        return nullptr;
    HENHMETAFILE embedded = emf_from_comments(bits, size);
    if (embedded)
        return embedded;
    HDC screen = GetDC(nullptr);
    if (!screen)
        return nullptr;
    INT screen_w = GetDeviceCaps(screen, HORZRES), screen_h = GetDeviceCaps(screen, VERTRES);
    INT mm_w = GetDeviceCaps(screen, HORZSIZE), mm_h = GetDeviceCaps(screen, VERTSIZE);
    /* pixels per hundredth of a millimeter, with a size of 320 by 240 millimeters for a device that has none */
    float kx = ((float)screen_w / (float)(mm_w ? mm_w : 320)) / 100.0f;
    float ky = ((float)screen_h / (float)(mm_h ? mm_h : 240)) / 100.0f;
    float left = 0.0f, top = 0.0f, width = 0.0f, height = 0.0f;
    BOOL framed = FALSE;
    if (placeable && placeable->Inch != 0) {
        float inch = (float)placeable->Inch;
        left = ((float)placeable->BoundingBox.Left * kx * 2540.0f) / inch;
        top = ((float)placeable->BoundingBox.Top * ky * 2540.0f) / inch;
        width = ((float)placeable->BoundingBox.Right * kx * 2540.0f) / inch - left;
        height = ((float)placeable->BoundingBox.Bottom * ky * 2540.0f) / inch - top;
        framed = width > 20.0f && height > 20.0f;
    }
    WmfScan window;
    if (!framed) {
        if ((compat == 0 || compat == 2) && wmf_scan(bits, size, &window)) {
            left = (float)window.org_x;
            top = (float)window.org_y;
            width = (float)window.ext_x;
            height = (float)window.ext_y;
            if (width < 0.0f) {
                left += width;
                width = -width;
            }
            if (height < 0.0f) {
                top += height;
                height = -height;
            }
            framed = TRUE;
        } else {
            left = top = 0.0f;
            width = (float)screen_w;
            height = (float)screen_h;
        }
    }
    /* the frame of the new metafile keeps the last pixel of the picture out, in hundredths of a millimeter */
    RECT frame = {round_to_int(left / kx), round_to_int(top / ky), round_to_int((left + width - 1.0f) / kx),
                  round_to_int((top + height - 1.0f) / ky)};
    HDC dc = CreateEnhMetaFileW(screen, nullptr, framed ? &frame : nullptr, nullptr);
    BOOL ok = dc != nullptr;
    if (ok) {
        INT x = round_to_int(left), y = round_to_int(top), w = round_to_int(width), h = round_to_int(height);
        ok = SetMapMode(dc, MM_ANISOTROPIC) != 0 && SetViewportOrgEx(dc, x, y, nullptr) &&
             SetViewportExtEx(dc, w, h, nullptr) && SetWindowOrgEx(dc, x, y, nullptr) &&
             SetWindowExtEx(dc, w, h, nullptr) && PlayMetaFile(dc, wmf);
        HENHMETAFILE made = CloseEnhMetaFile(dc);
        if (!ok && made) {
            DeleteEnhMetaFile(made);
            made = nullptr;
        }
        ReleaseDC(nullptr, screen);
        return made;
    }
    ReleaseDC(nullptr, screen);
    return nullptr;
}

/* ------------------------------------------------------------------ setting an object up */

GpStatus gp_meta_init_emf(GMetafile *m, HENHMETAFILE hemf, BOOL owned)
{
    BOOL damaged;
    MetafileHeader h;
    if (GetObjectType(hemf) != OBJ_ENHMETAFILE)
        return GenericError;
    if (gp_meta_header_of_emf(hemf, &h, &damaged) != Ok) {
        if (owned)
            DeleteEnhMetaFile(hemf);
        m->state = damaged ? GM_CORRUPT : GM_INVALID;
        return GenericError;
    }
    m->header = h;
    m->handle = hemf;
    m->owns_handle = owned;
    m->state = GM_READY;
    return Ok;
}

/*
 * sets the object up for a windows metafile. one with a good placeable header stays a windows metafile, with the box
 * of the header as its frame. any other becomes an enhanced metafile, which the records are played into.
 */
GpStatus gp_meta_init_wmf(GMetafile *m, HMETAFILE wmf, const WmfPlaceableFileHeader *placeable, BOOL owned)
{
    BOOL valid = placeable && gp_wmf_placeable_valid(placeable);
    if (valid && m->compat == 0) {
        BYTE header[WMF_HEADER_SIZE], box[PLACEABLE_SIZE];
        wmf_header_or_made_up(wmf, header);
        placeable_bytes(placeable, box);
        fill_wmf_header(&m->header, header, box);
        m->handle = wmf;
        m->owns_handle = owned;
        m->state = GM_READY;
        return Ok;
    }
    GpStatus st = GenericError;
    UINT n = GetMetaFileBitsEx(wmf, 0, nullptr);
    BYTE *bits = n ? gp_alloc(n) : nullptr;
    if (bits && GetMetaFileBitsEx(wmf, n, bits) == n) {
        HENHMETAFILE emf = gp_emf_from_wmf_data(wmf, valid ? placeable : nullptr, bits, n, m->compat);
        if (emf) {
            MetafileHeader h;
            BOOL damaged;
            if (gp_meta_header_of_emf(emf, &h, &damaged) == Ok) {
                m->header = h;
                m->handle = emf;
                m->owns_handle = TRUE;
                m->state = GM_READY;
                st = Ok;
            } else {
                DeleteEnhMetaFile(emf);
                m->state = damaged ? GM_CORRUPT : GM_INVALID;
            }
        }
    }
    gp_free(bits);
    if (owned)
        DeleteMetaFile(wmf);
    return st;
}

/* ------------------------------------------------------------------ files and streams */

/* the metafile in the bytes. the used count is how many bytes the metafile takes from them. */
static GpStatus from_memory(const BYTE *data, SIZE_T size, INT compat, GMetafile **out, SIZE_T *used)
{
    GMetafile *m = gp_meta_new();
    if (!m)
        return OutOfMemory;
    m->compat = compat;
    GpStatus st = GenericError;
    if (size >= EMF_HEADER3_SIZE && emf_header_valid(data)) {
        SIZE_T n = rd32(data + 48);
        if (n > size)
            n = size;
        HENHMETAFILE hemf = SetEnhMetaFileBits((UINT)n, data);
        if (hemf) {
            st = gp_meta_init_emf(m, hemf, TRUE);
            if (used)
                *used = n;
        }
    } else if (size >= PLACEABLE_SIZE + WMF_HEADER_SIZE && placeable_bytes_valid(data) &&
               wmf_header_valid(data + PLACEABLE_SIZE)) {
        const BYTE *wmf = data + PLACEABLE_SIZE;
        SIZE_T n = 2ull * rd32(wmf + 6);
        if (n > size - PLACEABLE_SIZE)
            n = size - PLACEABLE_SIZE;
        HMETAFILE hmf = SetMetaFileBitsEx((UINT)n, wmf);
        if (hmf) {
            fill_wmf_header(&m->header, wmf, data);
            m->handle = hmf;
            m->owns_handle = TRUE;
            m->state = GM_READY;
            st = Ok;
            if (used)
                *used = PLACEABLE_SIZE + n;
        }
    } else if (size >= WMF_HEADER_SIZE && wmf_header_valid(data)) {
        SIZE_T n = 2ull * rd32(data + 6);
        if (n > size)
            n = size;
        HMETAFILE hmf = SetMetaFileBitsEx((UINT)n, data);
        if (hmf) {
            st = gp_meta_init_wmf(m, hmf, nullptr, TRUE);
            if (used)
                *used = n;
        }
    }
    if (st != Ok) {
        gp_meta_free(m);
        return st == OutOfMemory ? OutOfMemory : GenericError;
    }
    *out = m;
    return Ok;
}

GpStatus gp_meta_from_memory(const BYTE *data, SIZE_T size, INT compat, GMetafile **out)
{
    return from_memory(data, size, compat, out, nullptr);
}

GpStatus gp_meta_header_of_bytes(const BYTE *data, SIZE_T size, MetafileHeader *out)
{
    GMetafile *m;
    GpStatus st = from_memory(data, size, 0, &m, nullptr);
    if (st != Ok)
        return st;
    *out = m->header;
    gp_meta_free(m);
    return Ok;
}

/* reads a metafile from the stream where it is, and leaves the stream behind what it took. */
static GpStatus from_stream(IStream *stream, INT compat, GMetafile **out, MetafileHeader *header_only)
{
    LARGE_INTEGER zero = {};
    ULARGE_INTEGER start = {};
    if (FAILED(stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_CUR, &start)))
        return GenericError;
    BYTE *data;
    SIZE_T size;
    GpStatus st = gp_read_stream(stream, &data, &size);
    if (st != Ok)
        return st;
    SIZE_T used = 0;
    GMetafile *m;
    st = from_memory(data, size, compat, &m, &used);
    gp_free(data);
    if (st != Ok) {
        LARGE_INTEGER back;
        back.QuadPart = (LONGLONG)start.QuadPart;
        stream->lpVtbl->Seek(stream, back, STREAM_SEEK_SET, nullptr);
        return st;
    }
    LARGE_INTEGER end;
    end.QuadPart = (LONGLONG)(start.QuadPart + used);
    stream->lpVtbl->Seek(stream, end, STREAM_SEEK_SET, nullptr);
    if (header_only) {
        *header_only = m->header;
        gp_meta_free(m);
        return Ok;
    }
    *out = m;
    return Ok;
}

/* ------------------------------------------------------------------ the image calls */

GpStatus gp_metafile_from_bytes(BYTE *data, SIZE_T size, GImage **image)
{
    GMetafile *m;
    GpStatus st = from_memory(data, size, 0, &m, nullptr);
    if (st != Ok)
        return st == OutOfMemory ? OutOfMemory : InvalidParameter;
    *image = &m->image;
    return Ok;
}

GpStatus gp_metafile_dispose(GpImage *image)
{
    GMetafile *m = gp_meta_cast(image);
    if (!m)
        return InvalidParameter;
    if (m->state == GM_PLAYING)
        return ObjectBusy;
    gp_meta_free(m);
    return Ok;
}

GpStatus gp_metafile_clone(GpImage *image, GpImage **clone)
{
    GMetafile *m = gp_meta_cast(image);
    if (!m)
        return InvalidParameter;
    if (m->state != GM_READY)
        return OutOfMemory;
    HANDLE copy;
    if (m->header.Type < MetafileTypeEmf)
        copy = CopyMetaFileW((HMETAFILE)m->handle, nullptr);
    else
        copy = CopyEnhMetaFileW((HENHMETAFILE)m->handle, nullptr);
    GMetafile *c = copy ? gp_meta_new() : nullptr;
    if (!c) {
        if (copy) {
            if (m->header.Type < MetafileTypeEmf)
                DeleteMetaFile((HMETAFILE)copy);
            else
                DeleteEnhMetaFile((HENHMETAFILE)copy);
        }
        return OutOfMemory;
    }
    c->header = m->header;
    c->handle = copy;
    c->owns_handle = TRUE;
    c->state = GM_READY;
    c->compat = m->compat;
    *clone = &c->image;
    return Ok;
}

/* the questions need a metafile that is not being recorded. */
static GMetafile *ready_cast(GpImage *image)
{
    GMetafile *m = gp_meta_cast(image);
    return m && (m->state == GM_READY || m->state == GM_PLAYING) ? m : nullptr;
}

/* the frame in the pixels of the resolution of the metafile. an enhanced one reaches a pixel further than its frame. */
void gp_meta_real_bounds(const GMetafile *m, float *x, float *y, float *w, float *h)
{
    const MetafileHeader *hd = &m->header;
    if (hd->Type < MetafileTypeEmf) {
        *x = (float)hd->X;
        *y = (float)hd->Y;
        *w = (float)hd->Width;
        *h = (float)hd->Height;
        return;
    }
    float kx = hd->DpiX / HIMETRIC_PER_INCH, ky = hd->DpiY / HIMETRIC_PER_INCH;
    const RECTL *f = &hd->EmfHeader.rclFrame;
    *x = kx * (float)f->left;
    *y = ky * (float)f->top;
    *w = (float)(f->right - f->left) * kx + 1.0f;
    *h = (float)(f->bottom - f->top) * ky + 1.0f;
}

/* the physical size in hundredths of a millimeter. */
GpStatus gp_metafile_dimension(GpImage *image, float *width, float *height)
{
    GMetafile *m = ready_cast(image);
    if (!m)
        return InvalidParameter;
    const MetafileHeader *hd = &m->header;
    if (hd->Type < MetafileTypeEmf) {
        *width = (float)hd->Width / hd->DpiX * HIMETRIC_PER_INCH;
        *height = (float)hd->Height / hd->DpiY * HIMETRIC_PER_INCH;
    } else {
        const RECTL *f = &hd->EmfHeader.rclFrame;
        *width = (float)(f->right - f->left) + HIMETRIC_PER_INCH / hd->DpiX;
        *height = (float)(f->bottom - f->top) + HIMETRIC_PER_INCH / hd->DpiY;
    }
    return Ok;
}

GpStatus gp_metafile_bounds(GpImage *image, GpRectF *bounds, GpUnit *unit)
{
    GMetafile *m = gp_meta_cast(image);
    if (!m || m->state < GM_RECORDING)
        return InvalidParameter;
    gp_meta_real_bounds(m, &bounds->X, &bounds->Y, &bounds->Width, &bounds->Height);
    *unit = UnitPixel;
    return Ok;
}

GpStatus gp_metafile_raw_format(GpImage *image, GUID *format)
{
    GMetafile *m = ready_cast(image);
    if (!m)
        return InvalidParameter;
    *format = m->header.Type < MetafileTypeEmf ? ImageFormatWMF : ImageFormatEMF;
    return Ok;
}

GpStatus gp_metafile_resolution(GpImage *image, float *dpi_x, float *dpi_y)
{
    GMetafile *m = ready_cast(image);
    if (!m)
        return InvalidParameter;
    if (dpi_x)
        *dpi_x = m->header.DpiX;
    if (dpi_y)
        *dpi_y = m->header.DpiY;
    return Ok;
}

GpStatus gp_metafile_pixel_size(GpImage *image, UINT *width, UINT *height)
{
    GMetafile *m = ready_cast(image);
    if (!m)
        return InvalidParameter;
    if (width)
        *width = (UINT)m->header.Width;
    if (height)
        *height = (UINT)m->header.Height;
    return Ok;
}

GpStatus gp_metafile_flags(GpImage *image, UINT *flags)
{
    GMetafile *m = ready_cast(image);
    if (!m)
        return InvalidParameter;
    *flags = ImageFlagsScalable | ImageFlagsHasAlpha | ImageFlagsReadOnly | 0x40000;
    return Ok;
}

/* ------------------------------------------------------------------ as a bitmap */

/* a bitmap of the picture at a size, drawn from its bounds. */
static GpStatus render_bitmap(GMetafile *m, INT w, INT h, float bx, float by, float bw, float bh, GBitmap **bitmap)
{
    if (w <= 0 || h <= 0 || w > GP_MAX_DIMENSION || h > GP_MAX_DIMENSION)
        return GenericError;
    GBitmap *b;
    GpStatus st = gp_bitmap_create(w, h, PixelFormat32bppARGB, &b);
    if (st != Ok)
        return st;
    GpGraphics *graphics;
    st = GdipGetImageGraphicsContext(&b->image, &graphics);
    if (st == Ok) {
        GGraphics *g = gp_graphics_cast(graphics);
        g->interpolation = InterpolationModeHighQualityBilinear;
        GpRectF dest = {0, 0, (float)w, (float)h}, src = {bx, by, bw, bh};
        st = gp_meta_play(g, m, &dest, &src, UnitPixel, nullptr, nullptr, nullptr);
        GdipDeleteGraphics(graphics);
    }
    if (st != Ok) {
        gp_bitmap_free(b);
        return GenericError;
    }
    *bitmap = b;
    return Ok;
}

/* the size a metafile gets when it is saved as a picture: its own, with a windows metafile at the screen resolution. */
GpStatus gp_metafile_to_bitmap(GImage *image, GBitmap **bitmap)
{
    GMetafile *m = ready_cast(image);
    if (!m)
        return InvalidParameter;
    const MetafileHeader *hd = &m->header;
    INT w = hd->Width, h = hd->Height;
    if (hd->Type < MetafileTypeEmf) {
        if (hd->DpiX <= 0.0f || hd->DpiY <= 0.0f)
            return GenericError;
        HDC dc = GetDC(nullptr);
        float sx = dc ? (float)GetDeviceCaps(dc, LOGPIXELSX) : 96.0f, sy = dc ? (float)GetDeviceCaps(dc, LOGPIXELSY) : 96.0f;
        if (dc)
            ReleaseDC(nullptr, dc);
        w = (INT)mime_vec_floorf(((float)hd->Width / hd->DpiX) * sx + 0.5f);
        h = (INT)mime_vec_floorf(((float)hd->Height / hd->DpiY) * sy + 0.5f);
    }
    return render_bitmap(m, w, h, (float)hd->X, (float)hd->Y, (float)hd->Width, (float)hd->Height, bitmap);
}

/* a thumbnail: both sizes, or neither for 120 by 120. a picture cannot keep its proportions from one size. */
GpStatus gp_metafile_thumbnail(GpImage *image, UINT width, UINT height, GpImage **thumbnail)
{
    GMetafile *m = ready_cast(image);
    if (!m || !thumbnail)
        return InvalidParameter;
    if (!width && !height)
        width = height = 120;
    if (!width || !height)
        return OutOfMemory;
    GBitmap *b;
    GpStatus st = render_bitmap(m, (INT)width, (INT)height, (float)m->header.X, (float)m->header.Y,
                                (float)m->header.Width, (float)m->header.Height, &b);
    if (st != Ok)
        return st;
    *thumbnail = &b->image;
    return Ok;
}

/* the clip of a graphics as a region of device pixels, NULL when there is no clip. */
HRGN gp_meta_clip_region(const GGraphics *g)
{
    if (!g->clip_set)
        return nullptr;
    UINT n = 0;
    for (UINT b = 0; b < g->clip.nbands; b++)
        n += g->clip.bands[b].count;
    SIZE_T bytes = sizeof(RGNDATAHEADER) + (SIZE_T)n * sizeof(RECT);
    RGNDATA *data = gp_alloc_zero(bytes);
    if (!data)
        return nullptr;
    RECT *rects = (RECT *)data->Buffer;
    UINT k = 0;
    for (UINT b = 0; b < g->clip.nbands; b++) {
        const MimeVecBand *band = &g->clip.bands[b];
        for (UINT i = 0; i < band->count; i++) {
            const MimeVecRun *run = &g->clip.runs[band->first + i];
            rects[k].left = run->x0;
            rects[k].right = run->x1;
            rects[k].top = band->y0;
            rects[k].bottom = band->y1;
            k++;
        }
    }
    data->rdh.dwSize = sizeof(RGNDATAHEADER);
    data->rdh.iType = RDH_RECTANGLES;
    data->rdh.nCount = n;
    data->rdh.nRgnSize = n * sizeof(RECT);
    HRGN region = n ? ExtCreateRegion(nullptr, (DWORD)bytes, data) : CreateRectRgn(0, 0, 0, 0);
    gp_free(data);
    return region;
}

/* ------------------------------------------------------------------ units */

BOOL gp_meta_unit_factor(const GMetafile *m, GpUnit unit, float *fx, float *fy)
{
    float dx = m->header.DpiX, dy = m->header.DpiY;
    switch (unit) {
    case UnitPixel:
        *fx = *fy = 1.0f;
        return TRUE;
    case UnitPoint:
        *fx = dx / 72.0f;
        *fy = dy / 72.0f;
        return TRUE;
    case UnitInch:
        *fx = dx;
        *fy = dy;
        return TRUE;
    case UnitDocument:
        *fx = dx / 300.0f;
        *fy = dy / 300.0f;
        return TRUE;
    case UnitMillimeter:
        *fx = dx / 25.4f;
        *fy = dy / 25.4f;
        return TRUE;
    default:
        return FALSE;
    }
}

/* the placeable header that stands for the bounds of a windows metafile, for the ones that are one. */
BOOL gp_meta_placeable_for(const GMetafile *m, WmfPlaceableFileHeader *out)
{
    const MetafileHeader *hd = &m->header;
    if (hd->Type >= MetafileTypeEmf)
        return FALSE;
    memset(out, 0, sizeof(*out));
    out->Key = PLACEABLE_KEY;
    out->BoundingBox.Left = (INT16)hd->X;
    out->BoundingBox.Top = (INT16)hd->Y;
    out->BoundingBox.Right = (INT16)(hd->X + hd->Width);
    out->BoundingBox.Bottom = (INT16)(hd->Y + hd->Height);
    out->Inch = (INT16)mime_vec_floorf((hd->DpiX + hd->DpiY) * 0.5f + 0.5f);
    out->Checksum = gp_wmf_placeable_checksum(out);
    return TRUE;
}

/* ------------------------------------------------------------------ the creation calls */

static GpStatus created(GMetafile *m, GpStatus st, GpMetafile **out)
{
    if (st != Ok) {
        gp_meta_free(m);
        return st == OutOfMemory ? OutOfMemory : GenericError;
    }
    *out = m;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipCreateMetafileFromEmf(HENHMETAFILE hemf, BOOL delete_emf, GpMetafile **metafile)
{
    GP_REQUIRE_INIT();
    if (!hemf || !metafile)
        return InvalidParameter;
    GMetafile *m = gp_meta_new();
    if (!m)
        return OutOfMemory;
    return created(m, gp_meta_init_emf(m, hemf, delete_emf), metafile);
}

WINGDIPAPI GpStatus WINAPI GdipCreateMetafileFromWmf(HMETAFILE hwmf, BOOL delete_wmf,
                                                     const WmfPlaceableFileHeader *placeable, GpMetafile **metafile)
{
    GP_REQUIRE_INIT();
    if (!hwmf || !metafile)
        return InvalidParameter;
    GMetafile *m = gp_meta_new();
    if (!m)
        return OutOfMemory;
    return created(m, gp_meta_init_wmf(m, hwmf, placeable, delete_wmf), metafile);
}

/* a file is a placeable or plain windows metafile, or an enhanced one: the bytes tell. */
static GpStatus from_file(const WCHAR *file, const WmfPlaceableFileHeader *placeable, GpMetafile **metafile)
{
    BYTE *data;
    SIZE_T size;
    GpStatus st = gp_read_file(file, &data, &size);
    if (st != Ok)
        return GenericError;
    GMetafile *m;
    BYTE with_header[PLACEABLE_SIZE];
    if (placeable && gp_wmf_placeable_valid(placeable) && size >= WMF_HEADER_SIZE && wmf_header_valid(data)) {
        /* the caller supplies what the file lacks */
        placeable_bytes(placeable, with_header);
        BYTE *joined = gp_alloc(size + PLACEABLE_SIZE);
        if (!joined) {
            gp_free(data);
            return OutOfMemory;
        }
        memcpy(joined, with_header, PLACEABLE_SIZE);
        memcpy(joined + PLACEABLE_SIZE, data, size);
        gp_free(data);
        data = joined;
        size += PLACEABLE_SIZE;
    }
    st = from_memory(data, size, 0, &m, nullptr);
    gp_free(data);
    if (st != Ok)
        return st == OutOfMemory ? OutOfMemory : GenericError;
    *metafile = m;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipCreateMetafileFromFile(const WCHAR *file, GpMetafile **metafile)
{
    GP_REQUIRE_INIT();
    if (!file || !metafile)
        return InvalidParameter;
    return from_file(file, nullptr, metafile);
}

WINGDIPAPI GpStatus WINAPI GdipCreateMetafileFromWmfFile(const WCHAR *file, const WmfPlaceableFileHeader *placeable,
                                                         GpMetafile **metafile)
{
    GP_REQUIRE_INIT();
    if (!file || !metafile)
        return InvalidParameter;
    return from_file(file, placeable, metafile);
}

WINGDIPAPI GpStatus WINAPI GdipCreateMetafileFromStream(IStream *stream, GpMetafile **metafile)
{
    GP_REQUIRE_INIT();
    if (!stream || !metafile)
        return InvalidParameter;
    GMetafile *m;
    GpStatus st = from_stream(stream, 0, &m, nullptr);
    if (st != Ok)
        return st == OutOfMemory ? OutOfMemory : GenericError;
    *metafile = m;
    return Ok;
}

/* ------------------------------------------------------------------ headers */

WINGDIPAPI GpStatus WINAPI GdipGetMetafileHeaderFromEmf(HENHMETAFILE hemf, MetafileHeader *header)
{
    if (!hemf || !header)
        return InvalidParameter;
    return gp_meta_header_of_emf(hemf, header, nullptr);
}

WINGDIPAPI GpStatus WINAPI GdipGetMetafileHeaderFromWmf(HMETAFILE hwmf, const WmfPlaceableFileHeader *placeable,
                                                        MetafileHeader *header)
{
    if (!hwmf || !placeable || !header)
        return InvalidParameter;
    memset(header, 0, sizeof(*header));
    if (!gp_wmf_placeable_valid(placeable))
        return InvalidParameter;
    BYTE wmf[WMF_HEADER_SIZE], box[PLACEABLE_SIZE];
    wmf_header_or_made_up(hwmf, wmf);
    placeable_bytes(placeable, box);
    fill_wmf_header(header, wmf, box);
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetMetafileHeaderFromFile(const WCHAR *file, MetafileHeader *header)
{
    if (!file || !header)
        return InvalidParameter;
    memset(header, 0, sizeof(*header));
    BYTE *data;
    SIZE_T size;
    if (gp_read_file(file, &data, &size) != Ok)
        return InvalidParameter;
    GpStatus st = gp_meta_header_of_bytes(data, size, header);
    gp_free(data);
    return st == Ok ? Ok : InvalidParameter;
}

WINGDIPAPI GpStatus WINAPI GdipGetMetafileHeaderFromStream(IStream *stream, MetafileHeader *header)
{
    if (!stream || !header)
        return InvalidParameter;
    memset(header, 0, sizeof(*header));
    GpStatus st = from_stream(stream, 0, nullptr, header);
    return st == Ok ? Ok : (st == OutOfMemory ? OutOfMemory : InvalidParameter);
}

WINGDIPAPI GpStatus WINAPI GdipGetMetafileHeaderFromMetafile(GpMetafile *metafile, MetafileHeader *header)
{
    GMetafile *m = gp_meta_cast(metafile);
    if (!m || !header || m->state < GM_RECORDING)
        return InvalidParameter;
    *header = m->header;
    return Ok;
}

WINGDIPAPI GpStatus WINAPI GdipGetHemfFromMetafile(GpMetafile *metafile, HENHMETAFILE *hemf)
{
    if (!hemf)
        return InvalidParameter;
    *hemf = nullptr;
    GMetafile *m = gp_meta_cast(metafile);
    if (!m || m->state < GM_RECORDING)
        return InvalidParameter;
    if (m->state != GM_READY || m->header.Type < MetafileTypeEmf)
        return InvalidParameter;
    *hemf = (HENHMETAFILE)m->handle;
    m->handle = nullptr;
    m->owns_handle = FALSE;
    m->state = GM_INVALID;
    return Ok;
}
