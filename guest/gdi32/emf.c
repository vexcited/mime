/*
 * enhanced metafiles: metafile objects, metafile dcs and their records, and
 * the functions that make, copy, read and delete metafiles. per call
 * recording is in mfrecord.c, playback in emfplay.c.
 *
 * like windows, a metafile dc is a win32k dc of the reference device that
 * keeps the dc state while gdi32 writes the records: the header, then one
 * record per call. objects are created on first selection at the lowest free
 * handle table index, and bounds are in reference device units. closing
 * writes the end of file record and, if the caller gave no frame, derives
 * the frame from the bounds.
 */
#include "emf_private.h"
#include <mime/win32/winnls.h>

volatile LONG MfDcCount;

static RTL_SRWLOCK g_lock;

/* ------------------------------------------------------------------ metafile objects */

#define EMF_MAX 4096

static EMF *g_emfs[EMF_MAX];
static BYTE g_emf_unique[EMF_MAX];

static void union_rect(RECT *a, const RECT *b)
{
    if (b->left < a->left)
        a->left = b->left;
    if (b->top < a->top)
        a->top = b->top;
    if (b->right > a->right)
        a->right = b->right;
    if (b->bottom > a->bottom)
        a->bottom = b->bottom;
}

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

static void *heap_realloc(void *p, SIZE_T size)
{
    if (!p)
        return RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, size);
    return RtlReAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, p, size);
}

/* the checks windows makes before accepting bits as a metafile. */
BOOL EmfValid(const BYTE *data, DWORD size)
{
    if (!data || size < 88)
        return FALSE;
    const ENHMETAHEADER *h = (const ENHMETAHEADER *)data;
    return h->iType == EMR_HEADER && h->dSignature == ENHMETA_SIGNATURE && h->nBytes >= 88 && h->nBytes <= size &&
           h->nSize >= 88 && h->nSize <= h->nBytes && !(h->nBytes & 3) && h->nHandles;
}

/* takes ownership of bits allocated from the process heap. */
HENHMETAFILE EmfCreateHandle(BYTE *data, DWORD size)
{
    EMF *emf = heap_zalloc(sizeof(EMF));
    if (!emf)
        return nullptr;
    emf->Data = data;
    emf->Size = size;
    HENHMETAFILE h = nullptr;
    RtlAcquireSRWLockExclusive(&g_lock);
    for (UINT i = 1; i < EMF_MAX; i++) {
        if (!g_emfs[i]) {
            g_emfs[i] = emf;
            g_emf_unique[i]++;
            h = (HENHMETAFILE)(ULONG_PTR)(((ULONG)(g_emf_unique[i] & 0x7F) << 24) | (MF_TYPE_EMF << 16) | i);
            break;
        }
    }
    RtlReleaseSRWLockExclusive(&g_lock);
    if (!h)
        heap_free(emf);
    return h;
}

static UINT emf_index(HENHMETAFILE handle)
{
    ULONG_PTR v = (ULONG_PTR)handle;
    UINT i = (UINT)(v & 0xFFFF);
    if (((v >> 16) & 0xFF) != MF_TYPE_EMF || !i || i >= EMF_MAX || ((v >> 24) & 0x7F) != (g_emf_unique[i] & 0x7Fu))
        return 0;
    return i;
}

EMF *EmfLookup(HENHMETAFILE handle)
{
    RtlAcquireSRWLockShared(&g_lock);
    UINT i = emf_index(handle);
    EMF *emf = i ? g_emfs[i] : nullptr;
    RtlReleaseSRWLockShared(&g_lock);
    return emf;
}

/* ------------------------------------------------------------------ metafile dcs */

#define EMF_DC_MAX 256

static EMF_DC *g_dcs[EMF_DC_MAX];

EMF_DC *EmfDc(HDC dc)
{
    if (!dc)
        return nullptr;
    EMF_DC *found = nullptr;
    RtlAcquireSRWLockShared(&g_lock);
    for (UINT i = 0; i < EMF_DC_MAX && !found; i++)
        if (g_dcs[i] && g_dcs[i]->Dc == dc)
            found = g_dcs[i];
    RtlReleaseSRWLockShared(&g_lock);
    return found;
}

ENHMETAHEADER *EmfHeader(EMF_DC *emf)
{
    return (ENHMETAHEADER *)emf->Data;
}

static BOOL reserve(EMF_DC *emf, DWORD more)
{
    if (emf->Failed)
        return FALSE;
    if (more > 0x7FFFFFFF - emf->Size) {
        emf->Failed = TRUE;
        return FALSE;
    }
    DWORD need = emf->Size + more;
    if (need <= emf->Capacity)
        return TRUE;
    DWORD cap = emf->Capacity ? emf->Capacity : 0x4000;
    while (cap < need)
        cap = cap > 0x40000000 ? need : cap * 2;
    BYTE *data = heap_realloc(emf->Data, cap);
    if (!data) {
        emf->Failed = TRUE;
        return FALSE;
    }
    emf->Data = data;
    emf->Capacity = cap;
    return TRUE;
}

/* a zeroed record, its size rounded up to four bytes; valid until the next call. */
void *EmfRecordAlloc(EMF_DC *emf, DWORD type, DWORD size)
{
    size = (size + 3) & ~3u;
    if (size < 8 || !reserve(emf, size))
        return nullptr;
    BYTE *r = emf->Data + emf->Size;
    memset(r, 0, size);
    ((EMR *)r)->iType = type;
    ((EMR *)r)->nSize = size;
    emf->Size += size;
    emf->Records++;
    return r;
}

/* kept since the reference device may go away first. a printer answers in user mode, not through the kernel. */
static void snapshot_caps(EMF_DC *emf, HDC ref)
{
    for (int i = 0; i < EMF_CAPS; i++)
        emf->Caps[i] = GetDeviceCaps(ref, i);
}

EMF_DC *EmfCreateDC(HDC ref, LPCWSTR file, const RECT *frame, LPCWSTR description, UINT desc_chars)
{
    if (frame && (frame->left > frame->right || frame->top > frame->bottom)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    HDC screen = nullptr;
    if (!ref) {
        screen = NtGdiOpenDCW(nullptr, nullptr, nullptr, 0, TRUE, nullptr, nullptr, nullptr);
        ref = screen;
    }
    HDC dc = ref ? NtGdiCreateMetafileDC(ref) : nullptr;
    EMF_DC *emf = dc ? heap_zalloc(sizeof(EMF_DC)) : nullptr;
    if (!emf) {
        if (dc)
            NtGdiDeleteObjectApp(dc);
        if (screen)
            NtGdiDeleteObjectApp(screen);
        return nullptr;
    }
    emf->Dc = dc;
    emf->File = INVALID_HANDLE_VALUE;
    snapshot_caps(emf, ref);
    {
        int unused;
        emf->Printer = PrnDcCount && PrnGetDeviceCaps(ref, HORZRES, &unused);
    }
    if (screen)
        NtGdiDeleteObjectApp(screen);
    if (file) {
        DWORD n = GetFullPathNameW(file, MAX_PATH, emf->Path, nullptr);
        if (!n || n >= MAX_PATH) {
            if (n >= MAX_PATH)
                SetLastError(ERROR_FILENAME_EXCED_RANGE);
            goto fail;
        }
        emf->File = CreateFileW(emf->Path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (emf->File == INVALID_HANDLE_VALUE)
            goto fail;
    }
    DWORD desc_bytes = desc_chars * sizeof(WCHAR);
    ENHMETAHEADER *h = EmfRecordAlloc(emf, EMR_HEADER, EMF_HEADER_SIZE + desc_bytes);
    if (!h)
        goto fail;
    h->rclBounds = (RECTL){0, 0, -1, -1};
    h->rclFrame = frame ? (RECTL){frame->left, frame->top, frame->right, frame->bottom} : (RECTL){0, 0, -1, -1};
    h->dSignature = ENHMETA_SIGNATURE;
    h->nVersion = 0x10000;
    h->nHandles = 1;
    if (desc_chars) {
        h->nDescription = desc_chars;
        h->offDescription = EMF_HEADER_SIZE;
        memcpy((BYTE *)h + EMF_HEADER_SIZE, description, desc_bytes);
    }
    h->szlDevice = (SIZEL){emf->Caps[DESKTOPHORZRES], emf->Caps[DESKTOPVERTRES]};
    h->szlMillimeters = (SIZEL){emf->Caps[HORZSIZE], emf->Caps[VERTSIZE]};
    h->szlMicrometers = (SIZEL){emf->Caps[HORZSIZE] * 1000, emf->Caps[VERTSIZE] * 1000};
    emf->Handles = 1;
    emf->Bounds = (RECTL){0, 0, -1, -1};
    RtlAcquireSRWLockExclusive(&g_lock);
    BOOL stored = FALSE;
    for (UINT i = 0; i < EMF_DC_MAX && !stored; i++) {
        if (!g_dcs[i]) {
            g_dcs[i] = emf;
            stored = TRUE;
        }
    }
    if (stored)
        _InterlockedIncrement(&MfDcCount);
    RtlReleaseSRWLockExclusive(&g_lock);
    if (stored)
        return emf;
fail:
    EmfDestroy(emf, TRUE);
    return nullptr;
}

/* also deletes the underlying win32k dc. */
void EmfDestroy(EMF_DC *emf, BOOL delete_file)
{
    RtlAcquireSRWLockExclusive(&g_lock);
    for (UINT i = 0; i < EMF_DC_MAX; i++) {
        if (g_dcs[i] == emf) {
            g_dcs[i] = nullptr;
            _InterlockedDecrement(&MfDcCount);
        }
    }
    RtlReleaseSRWLockExclusive(&g_lock);
    if (emf->File != INVALID_HANDLE_VALUE) {
        CloseHandle(emf->File);
        if (delete_file)
            DeleteFileW(emf->Path);
    }
    if (emf->Dc)
        NtGdiDeleteObjectApp(emf->Dc);
    heap_free(emf->Objects);
    heap_free(emf->Data);
    heap_free(emf);
}

int MfGetDeviceCaps(HDC dc, int index, BOOL *handled)
{
    *handled = FALSE;
    if (MfIsWmfDc(dc)) {
        /* a windows metafile dc has no device. */
        *handled = TRUE;
        return index == TECHNOLOGY ? DT_METAFILE : 0;
    }
    EMF_DC *emf = EmfDc(dc);
    if (!emf || index < 0 || index >= EMF_CAPS)
        return 0;
    *handled = TRUE;
    return emf->Caps[index];
}

/*
 * an escape sent to a metafile dc is recorded, and the ones that ask something are answered by the reference device,
 * here from what the metafile kept of it. a program that sizes its page with GETPHYSPAGESIZE reads the answer.
 */
int MfExtEscape(HDC dc, int escape, int in_size, LPCSTR in, int out_size, LPSTR out, BOOL *handled)
{
    *handled = FALSE;
    EMF_DC *emf = MfDcCount ? EmfDc(dc) : nullptr;
    if (!emf)
        return 0;
    *handled = TRUE;
    if (escape == QUERYESCSUPPORT) {
        if (!emf->Printer || !in || in_size < 2)
            return 0;
        int query = in_size >= 4 ? *(const int *)in : *(const WORD *)in;
        return PrnEscapeSupported(query);
    }
    if (in_size < 0 || (in_size && !in))
        return 0;
    EMREXTESCAPE *r = EmfRecordAlloc(emf, EMR_EXTESCAPE, offsetof(EMREXTESCAPE, EscData) + in_size);
    if (!r) {
        emf->Failed = TRUE;
        return 0;
    }
    r->iEscape = escape;
    r->cbEscData = in_size;
    if (in_size)
        memcpy(r->EscData, in, in_size);
    if (out && out_size >= (int)sizeof(POINT) && emf->Printer) {
        POINT *p = (POINT *)out;
        switch (escape) {
        case GETPHYSPAGESIZE:
            p->x = emf->Caps[PHYSICALWIDTH];
            p->y = emf->Caps[PHYSICALHEIGHT];
            return 1;
        case GETPRINTINGOFFSET:
            p->x = emf->Caps[PHYSICALOFFSETX];
            p->y = emf->Caps[PHYSICALOFFSETY];
            return 1;
        case GETSCALINGFACTOR:
            p->x = emf->Caps[SCALINGFACTORX];
            p->y = emf->Caps[SCALINGFACTORY];
            return 1;
        }
    }
    return in_size > 1 ? in_size : 1;
}

DWORD MfObjectType(HGDIOBJ handle)
{
    ULONG_PTR type = ((ULONG_PTR)handle >> 16) & 0xFF;
    if (type == MF_TYPE_EMF)
        return EmfLookup(handle) ? OBJ_ENHMETAFILE : 0;
    if (type == 0x01 && MfDcCount && EmfDc(handle))
        return OBJ_ENHMETADC;
    return 0;
}

BOOL MfDeleteDC(HDC dc)
{
    if (MfIsWmfDc(dc))
        return WmfDeleteDC(dc);
    EMF_DC *emf = EmfDc(dc);
    if (!emf)
        return FALSE;
    EmfDestroy(emf, TRUE);
    return TRUE;
}

/* ------------------------------------------------------------------ bounds */

void EmfBoundsInit(EMF_BOUNDS *b)
{
    b->Empty = TRUE;
}

void EmfBoundsPoint(EMF_BOUNDS *b, double x, double y)
{
    if (b->Empty) {
        b->Left = b->Right = x;
        b->Top = b->Bottom = y;
        b->Empty = FALSE;
        return;
    }
    if (x < b->Left)
        b->Left = x;
    if (x > b->Right)
        b->Right = x;
    if (y < b->Top)
        b->Top = y;
    if (y > b->Bottom)
        b->Bottom = y;
}

void EmfBoundsInflate(EMF_BOUNDS *b, double by)
{
    if (b->Empty)
        return;
    b->Left -= by;
    b->Top -= by;
    b->Right += by;
    b->Bottom += by;
}

static LONG round_d(double v)
{
    if (v > 2147483647.0)
        return 0x7FFFFFFF;
    if (v < -2147483648.0)
        return (LONG)0x80000000;
    return (LONG)(v >= 0 ? v + 0.5 : v - 0.5);
}

void EmfClipChanged(EMF_DC *emf)
{
    emf->ClipKnown = FALSE;
}

/* like windows, record bounds are clipped to the clip and meta regions. */
static BOOL clip_bounds(EMF_DC *emf, RECT *out)
{
    if (!emf->ClipKnown) {
        emf->HasClip = FALSE;
        HRGN rgn = NtGdiCreateRectRgn(0, 0, 0, 0);
        if (rgn) {
            if (NtGdiGetRandomRgn(emf->Dc, rgn, 3) == 1) {
                emf->HasClip = TRUE;
                NtGdiGetRgnBox(rgn, &emf->Clip);
            }
            NtGdiDeleteObjectApp(rgn);
        }
        emf->ClipKnown = TRUE;
    }
    *out = emf->Clip;
    return emf->HasClip;
}

void EmfBoundsCommit(EMF_DC *emf, RECTL *field, const EMF_BOUNDS *b)
{
    RECTL r = {0, 0, -1, -1};
    if (!b->Empty) {
        r = (RECTL){round_d(b->Left), round_d(b->Top), round_d(b->Right), round_d(b->Bottom)};
        RECT clip;
        if (clip_bounds(emf, &clip)) {
            /* the clip box is exclusive, the bounds inclusive. */
            if (r.left < clip.left)
                r.left = clip.left;
            if (r.top < clip.top)
                r.top = clip.top;
            if (r.right > clip.right - 1)
                r.right = clip.right - 1;
            if (r.bottom > clip.bottom - 1)
                r.bottom = clip.bottom - 1;
        }
        if (r.left > r.right || r.top > r.bottom)
            r = (RECTL){0, 0, -1, -1};
    }
    if (field)
        *field = r;
    if (r.left > r.right)
        return;
    if (!emf->HasBounds) {
        emf->Bounds = r;
        emf->HasBounds = TRUE;
    } else {
        if (r.left < emf->Bounds.left)
            emf->Bounds.left = r.left;
        if (r.top < emf->Bounds.top)
            emf->Bounds.top = r.top;
        if (r.right > emf->Bounds.right)
            emf->Bounds.right = r.right;
        if (r.bottom > emf->Bounds.bottom)
            emf->Bounds.bottom = r.bottom;
    }
    if (emf->AppBounds & DCB_ENABLE) {
        RECT a = {r.left, r.top, r.right + 1, r.bottom + 1};
        if (emf->AppRect.left >= emf->AppRect.right)
            emf->AppRect = a;
        else
            union_rect(&emf->AppRect, &a);
    }
}

BOOL EmfWorldToDevice(EMF_DC *emf, XFORM *m)
{
    if (NtGdiGetTransform(emf->Dc, MIME_XFORM_WORLD_TO_DEVICE, m))
        return TRUE;
    *m = (XFORM){1, 0, 0, 1, 0, 0};
    return FALSE;
}

void EmfTransformPoint(const XFORM *m, double x, double y, double *dx, double *dy)
{
    *dx = x * m->eM11 + y * m->eM21 + m->eDx;
    *dy = x * m->eM12 + y * m->eM22 + m->eDy;
}

/* SetBoundsRect and GetBoundsRect on a metafile dc: the bounds the records accumulate. */
BOOL MfGetBounds(HDC dc, LPRECT rect, UINT flags, UINT *result)
{
    EMF_DC *emf = EmfDc(dc);
    if (!emf)
        return FALSE;
    BOOL empty = emf->AppRect.left >= emf->AppRect.right;
    if (rect)
        *rect = empty ? (RECT){0, 0, 0, 0} : emf->AppRect;
    *result = (empty ? DCB_RESET : DCB_SET) | (emf->AppBounds & DCB_ENABLE ? DCB_ENABLE : DCB_DISABLE);
    if (flags & DCB_RESET)
        emf->AppRect = (RECT){0, 0, 0, 0};
    return TRUE;
}

void MfSetBounds(EMF_DC *emf, const RECT *rect, UINT flags)
{
    if (flags & DCB_RESET)
        emf->AppRect = (RECT){0, 0, 0, 0};
    if ((flags & DCB_ACCUMULATE) && rect && rect->left < rect->right && rect->top < rect->bottom) {
        if (emf->AppRect.left >= emf->AppRect.right)
            emf->AppRect = *rect;
        else
            union_rect(&emf->AppRect, rect);
    }
    if (flags & DCB_ENABLE)
        emf->AppBounds |= DCB_ENABLE;
    if (flags & DCB_DISABLE)
        emf->AppBounds &= ~(DWORD)DCB_ENABLE;
}

/* ------------------------------------------------------------------ objects */

static HGDIOBJ g_stock[STOCK_LAST + 1];
static BOOL g_stock_known;

DWORD EmfStockIndex(HGDIOBJ object)
{
    if (!g_stock_known) {
        for (int i = 0; i <= STOCK_LAST; i++)
            g_stock[i] = GetStockObject(i);
        g_stock_known = TRUE;
    }
    for (int i = 0; i <= STOCK_LAST; i++)
        if (g_stock[i] && g_stock[i] == object)
            return ENHMETA_STOCK_OBJECT | (DWORD)i;
    return 0;
}

static DWORD alloc_index(EMF_DC *emf, HGDIOBJ object)
{
    for (DWORD i = 1; i < 0xFFFF; i++) {
        if (i >= emf->ObjectCapacity) {
            DWORD cap = emf->ObjectCapacity ? emf->ObjectCapacity * 2 : 64;
            HGDIOBJ *objects = heap_zalloc(cap * sizeof(HGDIOBJ));
            if (!objects)
                return 0;
            if (emf->Objects)
                memcpy(objects, emf->Objects, emf->ObjectCapacity * sizeof(HGDIOBJ));
            heap_free(emf->Objects);
            emf->Objects = objects;
            emf->ObjectCapacity = cap;
        }
        if (!emf->Objects[i]) {
            emf->Objects[i] = object;
            if (i + 1 > emf->Handles)
                emf->Handles = (WORD)(i + 1);
            return i;
        }
    }
    return 0;
}

static void free_index(EMF_DC *emf, DWORD index)
{
    if (index < emf->ObjectCapacity)
        emf->Objects[index] = nullptr;
}

static BOOL record_pen(EMF_DC *emf, DWORD index, HGDIOBJ pen)
{
    LOGPEN lp;
    if (GetObjectW(pen, sizeof(lp), &lp) != sizeof(lp))
        return FALSE;
    EMRCREATEPEN *r = EmfRecordAlloc(emf, EMR_CREATEPEN, sizeof(EMRCREATEPEN));
    if (!r)
        return FALSE;
    r->ihPen = index;
    r->lopn = lp;
    return TRUE;
}

static BOOL record_extpen(EMF_DC *emf, DWORD index, HGDIOBJ pen)
{
    EXTLOGPEN lp;
    if (GetObjectW(pen, sizeof(lp), &lp) < (int)offsetof(EXTLOGPEN, elpStyleEntry))
        return FALSE;
    EMREXTCREATEPEN *r = EmfRecordAlloc(emf, EMR_EXTCREATEPEN, offsetof(EMREXTCREATEPEN, elp.elpStyleEntry));
    if (!r)
        return FALSE;
    r->ihPen = index;
    r->elp.elpPenStyle = lp.elpPenStyle;
    r->elp.elpWidth = lp.elpWidth;
    r->elp.elpBrushStyle = lp.elpBrushStyle;
    r->elp.elpColor = lp.elpColor;
    r->elp.elpHatch = (ULONG)lp.elpHatch;
    return TRUE;
}

static BOOL record_brush(EMF_DC *emf, DWORD index, HGDIOBJ brush)
{
    LOGBRUSH lb;
    if (!GetObjectW(brush, sizeof(lb), &lb))
        return FALSE;
    EMRCREATEBRUSHINDIRECT *r = EmfRecordAlloc(emf, EMR_CREATEBRUSHINDIRECT, sizeof(EMRCREATEBRUSHINDIRECT));
    if (!r)
        return FALSE;
    r->ihBrush = index;
    r->lb = (LOGBRUSH32){lb.lbStyle == BS_DIBPATTERNPT || lb.lbStyle == BS_PATTERN ? BS_SOLID : lb.lbStyle, lb.lbColor,
                         (ULONG)lb.lbHatch};
    return TRUE;
}

/*
 * like windows: the logical font, the full name, style and script, then the
 * design vector, 368 bytes with no axes.
 */
#define EMF_FONT_RECORD 368u
#define STAMP_DESIGNVECTOR 0x08007664u

static BOOL record_font(EMF_DC *emf, DWORD index, HGDIOBJ font)
{
    LOGFONTW lf;
    if (GetObjectW(font, sizeof(lf), &lf) != sizeof(lf))
        return FALSE;
    BYTE *r = EmfRecordAlloc(emf, EMR_EXTCREATEFONTINDIRECTW, EMF_FONT_RECORD);
    if (!r)
        return FALSE;
    ((EMREXTCREATEFONTINDIRECTW *)r)->ihFont = index;
    ((EMREXTCREATEFONTINDIRECTW *)r)->elfw = lf;
    DWORD *dv = (DWORD *)(r + 12 + sizeof(LOGFONTW) + (LF_FULLFACESIZE + 2 * LF_FACESIZE) * sizeof(WCHAR));
    dv[0] = STAMP_DESIGNVECTOR;
    dv[1] = 0;
    return TRUE;
}

static BOOL record_palette(EMF_DC *emf, DWORD index, HPALETTE palette)
{
    WORD count = 0;
    if (GetObjectW(palette, sizeof(count), &count) != sizeof(count))
        return FALSE;
    PALETTEENTRY entries[256];
    UINT n = count > 256 ? 256 : count;
    GetPaletteEntries(palette, 0, n, entries);
    EMRCREATEPALETTE *r = EmfRecordAlloc(emf, EMR_CREATEPALETTE, offsetof(EMRCREATEPALETTE, lgpl.palPalEntry) + n * 4);
    if (!r)
        return FALSE;
    r->ihPal = index;
    r->lgpl.palVersion = 0x300;
    r->lgpl.palNumEntries = (WORD)n;
    memcpy(r->lgpl.palPalEntry, entries, n * sizeof(PALETTEENTRY));
    return TRUE;
}

/* the object's handle table index, created in the metafile on first use; 0 on failure. */
DWORD EmfObjectIndex(EMF_DC *emf, HGDIOBJ object, BOOL *created)
{
    if (created)
        *created = FALSE;
    DWORD stock = EmfStockIndex(object);
    if (stock)
        return stock;
    for (DWORD i = 1; i < emf->ObjectCapacity; i++)
        if (emf->Objects[i] == object)
            return i;
    DWORD index = alloc_index(emf, object);
    if (!index)
        return 0;
    BOOL ok = FALSE;
    switch (((ULONG_PTR)object >> 16) & 0xFF) {
    case 0x30:
        ok = record_pen(emf, index, object);
        break;
    case 0x50:
        ok = record_extpen(emf, index, object);
        break;
    case 0x10:
        ok = record_brush(emf, index, object);
        break;
    case 0x0A:
        ok = record_font(emf, index, object);
        break;
    case 0x08:
        ok = record_palette(emf, index, object);
        break;
    }
    if (!ok) {
        free_index(emf, index);
        return 0;
    }
    if (created)
        *created = TRUE;
    return index;
}

/* deleting an object deletes it from every metafile that holds it. */
void MfDeleteObject(HGDIOBJ object)
{
    WmfDeleteObject(object);
    if (!MfDcCount)
        return;
    EMF_DC *dcs[EMF_DC_MAX];
    UINT n = 0;
    RtlAcquireSRWLockShared(&g_lock);
    for (UINT i = 0; i < EMF_DC_MAX; i++)
        if (g_dcs[i])
            dcs[n++] = g_dcs[i];
    RtlReleaseSRWLockShared(&g_lock);
    for (UINT k = 0; k < n; k++) {
        EMF_DC *emf = dcs[k];
        for (DWORD i = 1; i < emf->ObjectCapacity; i++) {
            if (emf->Objects[i] != object)
                continue;
            EMRDELETEOBJECT *r = EmfRecordAlloc(emf, EMR_DELETEOBJECT, sizeof(EMRDELETEOBJECT));
            if (r)
                r->ihObject = i;
            free_index(emf, i);
        }
    }
}

/* ------------------------------------------------------------------ creating and closing */

/* both strings and their two terminating nulls. */
static UINT description_chars(LPCWSTR description)
{
    if (!description)
        return 0;
    UINT n = 0;
    while (description[n] || description[n + 1])
        n++;
    return n + 2;
}

WINGDIAPI HDC WINAPI CreateEnhMetaFileW(HDC ref, LPCWSTR file, const RECT *frame, LPCWSTR description)
{
    EMF_DC *emf = EmfCreateDC(ref, file, frame, description, description_chars(description));
    return emf ? emf->Dc : nullptr;
}

WINGDIAPI HDC WINAPI CreateEnhMetaFileA(HDC ref, LPCSTR file, const RECT *frame, LPCSTR description)
{
    WCHAR wfile[MAX_PATH];
    if (file && !MultiByteToWideChar(CP_ACP, 0, file, -1, wfile, MAX_PATH))
        return nullptr;
    LPWSTR wdesc = nullptr;
    UINT chars = 0;
    if (description) {
        int n = 0;
        while (description[n] || description[n + 1])
            n++;
        n += 2;
        chars = (UINT)MultiByteToWideChar(CP_ACP, 0, description, n, nullptr, 0);
        wdesc = heap_alloc((chars + 2) * sizeof(WCHAR));
        if (!wdesc)
            return nullptr;
        MultiByteToWideChar(CP_ACP, 0, description, n, wdesc, (int)chars);
        wdesc[chars] = wdesc[chars + 1] = 0;
    }
    EMF_DC *emf = EmfCreateDC(ref, file ? wfile : nullptr, frame, wdesc, chars);
    heap_free(wdesc);
    return emf ? emf->Dc : nullptr;
}

static BOOL write_all(HANDLE file, const BYTE *data, DWORD size)
{
    DWORD written = 0;
    return WriteFile(file, data, size, &written, nullptr) && written == size;
}

WINGDIAPI HENHMETAFILE WINAPI CloseEnhMetaFile(HDC dc)
{
    EMF_DC *emf = EmfDc(dc);
    if (!emf) {
        SetLastError(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    /* unwinds the saved states to the first, like windows. */
    RestoreDC(dc, 1);
    EMREOF *eof = EmfRecordAlloc(emf, EMR_EOF, sizeof(EMREOF));
    if (eof) {
        eof->offPalEntries = 16;
        eof->nSizeLast = sizeof(EMREOF);
    }
    ENHMETAHEADER *h = EmfHeader(emf);
    HENHMETAFILE result = nullptr;
    if (!emf->Failed && h) {
        h->rclBounds = emf->Bounds;
        if (h->rclFrame.left > h->rclFrame.right || h->rclFrame.top > h->rclFrame.bottom) {
            /* the frame from the bounds, in hundredths of millimeters. */
            h->rclFrame.left = MulDiv(100 * emf->Bounds.left, h->szlMillimeters.cx, h->szlDevice.cx);
            h->rclFrame.top = MulDiv(100 * emf->Bounds.top, h->szlMillimeters.cy, h->szlDevice.cy);
            h->rclFrame.right = MulDiv(100 * emf->Bounds.right, h->szlMillimeters.cx, h->szlDevice.cx);
            h->rclFrame.bottom = MulDiv(100 * emf->Bounds.bottom, h->szlMillimeters.cy, h->szlDevice.cy);
        }
        h->nBytes = emf->Size;
        h->nRecords = emf->Records;
        h->nHandles = emf->Handles;
        BOOL ok = TRUE;
        if (emf->File != INVALID_HANDLE_VALUE) {
            ok = write_all(emf->File, emf->Data, emf->Size);
            CloseHandle(emf->File);
            emf->File = INVALID_HANDLE_VALUE;
            if (!ok)
                DeleteFileW(emf->Path);
        }
        BYTE *data = ok ? heap_realloc(emf->Data, emf->Size) : nullptr;
        if (data) {
            result = EmfCreateHandle(data, emf->Size);
            if (result)
                emf->Data = nullptr;
            else
                emf->Data = data;
        }
    }
    EmfDestroy(emf, result == nullptr);
    return result;
}

WINGDIAPI BOOL WINAPI DeleteEnhMetaFile(HENHMETAFILE handle)
{
    RtlAcquireSRWLockExclusive(&g_lock);
    UINT i = emf_index(handle);
    EMF *emf = i ? g_emfs[i] : nullptr;
    if (emf)
        g_emfs[i] = nullptr;
    RtlReleaseSRWLockExclusive(&g_lock);
    if (!emf) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    heap_free(emf->Data);
    heap_free(emf);
    return TRUE;
}

/* ------------------------------------------------------------------ bits and files */

WINGDIAPI UINT WINAPI GetEnhMetaFileBits(HENHMETAFILE handle, UINT size, LPBYTE data)
{
    EMF *emf = EmfLookup(handle);
    if (!emf)
        return 0;
    if (!data)
        return emf->Size;
    if (size < emf->Size) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    memcpy(data, emf->Data, emf->Size);
    return emf->Size;
}

WINGDIAPI HENHMETAFILE WINAPI SetEnhMetaFileBits(UINT size, const BYTE *data)
{
    if (!data || size < 88 || size < ((const ENHMETAHEADER *)data)->nBytes ||
        ((const ENHMETAHEADER *)data)->nBytes < 88) {
        SetLastError(ERROR_INVALID_DATA);
        return nullptr;
    }
    DWORD bytes = ((const ENHMETAHEADER *)data)->nBytes;
    if (!EmfValid(data, bytes))
        return nullptr;
    BYTE *copy = heap_alloc(bytes);
    if (!copy)
        return nullptr;
    memcpy(copy, data, bytes);
    HENHMETAFILE h = EmfCreateHandle(copy, bytes);
    if (!h)
        heap_free(copy);
    return h;
}

WINGDIAPI HENHMETAFILE WINAPI GetEnhMetaFileW(LPCWSTR file)
{
    HANDLE f = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return nullptr;
    LARGE_INTEGER size;
    HENHMETAFILE h = nullptr;
    if (GetFileSizeEx(f, &size) && size.QuadPart >= 88 && size.QuadPart <= 0x7FFFFFFF) {
        BYTE *data = heap_alloc((SIZE_T)size.QuadPart);
        DWORD read = 0;
        if (data && ReadFile(f, data, (DWORD)size.QuadPart, &read, nullptr) && read == (DWORD)size.QuadPart &&
            EmfValid(data, read)) {
            h = EmfCreateHandle(data, ((ENHMETAHEADER *)data)->nBytes);
            if (h)
                data = nullptr;
        }
        heap_free(data);
    }
    CloseHandle(f);
    return h;
}

WINGDIAPI HENHMETAFILE WINAPI GetEnhMetaFileA(LPCSTR file)
{
    WCHAR wfile[MAX_PATH];
    if (!file || !MultiByteToWideChar(CP_ACP, 0, file, -1, wfile, MAX_PATH)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    return GetEnhMetaFileW(wfile);
}

WINGDIAPI HENHMETAFILE WINAPI CopyEnhMetaFileW(HENHMETAFILE handle, LPCWSTR file)
{
    EMF *emf = EmfLookup(handle);
    if (!emf) {
        SetLastError(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    if (file) {
        HANDLE f = CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE)
            return nullptr;
        BOOL ok = write_all(f, emf->Data, emf->Size);
        CloseHandle(f);
        if (!ok) {
            DeleteFileW(file);
            return nullptr;
        }
        return GetEnhMetaFileW(file);
    }
    BYTE *copy = heap_alloc(emf->Size);
    if (!copy)
        return nullptr;
    memcpy(copy, emf->Data, emf->Size);
    HENHMETAFILE h = EmfCreateHandle(copy, emf->Size);
    if (!h)
        heap_free(copy);
    return h;
}

WINGDIAPI HENHMETAFILE WINAPI CopyEnhMetaFileA(HENHMETAFILE handle, LPCSTR file)
{
    WCHAR wfile[MAX_PATH];
    if (file && !MultiByteToWideChar(CP_ACP, 0, file, -1, wfile, MAX_PATH))
        return nullptr;
    return CopyEnhMetaFileW(handle, file ? wfile : nullptr);
}

/* ------------------------------------------------------------------ header, description and palette */

/* copies 88, 100 or 108 bytes, as much as the buffer and header allow, and sets nSize to match. */
WINGDIAPI UINT WINAPI GetEnhMetaFileHeader(HENHMETAFILE handle, UINT size, LPENHMETAHEADER header)
{
    EMF *emf = EmfLookup(handle);
    if (!emf)
        return 0;
    const ENHMETAHEADER *h = (const ENHMETAHEADER *)emf->Data;
    if (!header)
        return h->nSize;
    if (size < 88) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    UINT have = h->nSize >= 108 ? 108 : h->nSize >= 100 ? 100 : 88;
    UINT copy = size >= 108 ? 108 : size >= 100 ? 100 : 88;
    if (copy > have)
        copy = have;
    memcpy(header, h, copy);
    /* windows zeroes the extensions the header lacks when the buffer has room for them. */
    if (copy == 88 && (size == 100 || size == 108)) {
        header->cbPixelFormat = 0;
        header->offPixelFormat = 0;
        header->bOpenGL = 0;
        copy = 100;
    }
    if (copy == 100 && size == 108) {
        header->szlMicrometers = (SIZEL){0, 0};
        copy = 108;
    }
    header->nSize = copy;
    return copy;
}

static const ENHMETAHEADER *description_of(HENHMETAFILE handle, const WCHAR **text)
{
    EMF *emf = EmfLookup(handle);
    if (!emf)
        return nullptr;
    const ENHMETAHEADER *h = (const ENHMETAHEADER *)emf->Data;
    if (h->nDescription && (h->offDescription > emf->Size || h->nDescription > (emf->Size - h->offDescription) / 2))
        return nullptr;
    *text = (const WCHAR *)(emf->Data + h->offDescription);
    return h;
}

WINGDIAPI UINT WINAPI GetEnhMetaFileDescriptionW(HENHMETAFILE handle, UINT count, LPWSTR description)
{
    const WCHAR *text;
    const ENHMETAHEADER *h = description_of(handle, &text);
    if (!h)
        return GDI_ERROR;
    if (!description)
        return h->nDescription;
    UINT n = count < h->nDescription ? count : h->nDescription;
    memcpy(description, text, n * sizeof(WCHAR));
    return n;
}

WINGDIAPI UINT WINAPI GetEnhMetaFileDescriptionA(HENHMETAFILE handle, UINT count, LPSTR description)
{
    const WCHAR *text;
    const ENHMETAHEADER *h = description_of(handle, &text);
    if (!h)
        return GDI_ERROR;
    if (!h->nDescription)
        return 0;
    if (!description)
        return (UINT)WideCharToMultiByte(CP_ACP, 0, text, (int)h->nDescription, nullptr, 0, nullptr, nullptr);
    int n = WideCharToMultiByte(CP_ACP, 0, text, (int)h->nDescription, description, (int)count, nullptr, nullptr);
    return n ? (UINT)n : GDI_ERROR;
}

/* the palette lives in the end of file record, which the last dword locates. */
WINGDIAPI UINT WINAPI GetEnhMetaFilePaletteEntries(HENHMETAFILE handle, UINT count, LPPALETTEENTRY entries)
{
    EMF *emf = EmfLookup(handle);
    if (!emf)
        return GDI_ERROR;
    const ENHMETAHEADER *h = (const ENHMETAHEADER *)emf->Data;
    DWORD last = *(const DWORD *)(emf->Data + emf->Size - 4);
    if (last < sizeof(EMREOF) || last > emf->Size)
        return 0;
    const EMREOF *eof = (const EMREOF *)(emf->Data + emf->Size - last);
    if (eof->emr.iType != EMR_EOF || eof->offPalEntries > last ||
        (UINT64)eof->offPalEntries + 4ull * h->nPalEntries > last)
        return 0;
    UINT n = h->nPalEntries;
    if (!entries)
        return n;
    if (count < n)
        n = count;
    memcpy(entries, (const BYTE *)eof + eof->offPalEntries, n * sizeof(PALETTEENTRY));
    return n;
}

/* ------------------------------------------------------------------ comments */

WINGDIAPI BOOL WINAPI GdiComment(HDC dc, UINT size, const BYTE *data)
{
    if (!MfActive(dc))
        return TRUE;
    return MfComment(dc, size, data);
}
