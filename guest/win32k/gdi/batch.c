#include "../win32k.h"

#define BATCH_BYTES 0x4d8
#define BATCH_MAX_COUNT 0x135

typedef struct BATCH {
    ULONG Offset;
    ULONG Pad;
    ULONG64 Hdc;
    UCHAR Buffer[BATCH_BYTES];
} BATCH;

#define R32(r, at) (*(const ULONG *)((r) + (at)))
#define R64(r, at) (*(const ULONG64 *)((r) + (at)))

/*
 * the records gdi32 writes in GdiTebBatch, read in gdi32full and in win32kfull's NtGdiFlushUserBatchInternal. a
 * 32-bit program writes the batch of its 64-bit teb in the layout of 64 bits, so there is one batch. a record starts
 * with its size and its kind, 16 bits each, and the next starts 8 bytes aligned:
 *
 *   0 PatBlt, 0x48 bytes      x, y, width, height at 4, the brush at 0x18, the rop at 0x20, the colours of the
 *                             text, the background and the dc brush at 0x24, the origin of the viewport at 0x34
 *   1 PolyPatBlt              the rop at 4, the mode, the count, the three colours at 0x10, the origin of the
 *                             viewport at 0x28, rectangles of 24 bytes with their brush from 0x30
 *   2 ExtTextOutW             the colours of the text and the background at 4, the mode of the background, x and
 *                             y at 0x18, the options at 0x20 with the top bit when there is no rectangle, the
 *                             rectangle, the code page at 0x34, the count, where the advances are after the text,
 *                             the font at 0x40, the alignment at 0x48, the origin of the viewport, the text at 0x54
 *   3 ExtTextOutW of a rectangle and no text, 0x28 bytes: the background colour at 4, the options, the rectangle at
 *                             0xc, the origin of the viewport at 0x1c
 *   4 SetBrushOrgEx, x and y at 4
 *   5 SelectClipRgn of a rectangle or of none: the mode at 4, 0x8000005 for none, the rectangle at 8
 *   6 SelectObject of a font at 8
 *   7 DeleteObject of a brush, 8 of a region, the handle at 8
 *
 * the colours each record carries are those of the time of the call, and the kernel puts them in place for that
 * record only. the deletions run even when the dc is gone.
 */
enum {
    BATCH_PATBLT = 0,
    BATCH_POLYPATBLT = 1,
    BATCH_TEXTOUT = 2,
    BATCH_TEXTOUTRECT = 3,
    BATCH_BRUSHORG = 4,
    BATCH_SELECTCLIP = 5,
    BATCH_SELECTFONT = 6,
    BATCH_DELETEBRUSH = 7,
    BATCH_DELETEREGION = 8,
};

/* the colours a record carries replace those of the dc while it runs. */
typedef struct BATCH_SAVED {
    COLORREF TextColor, BkColor, BrushColor;
    POINT ViewportOrg;
} BATCH_SAVED;

static void put_colors(DC *dc, BATCH_SAVED *saved, ULONG text, ULONG bk, ULONG brush, const UCHAR *viewport)
{
    DC_STATE *s = &dc->State;
    *saved = (BATCH_SAVED){s->TextColor, s->BkColor, s->BrushColor, s->ViewportOrg};
    s->TextColor = text & 0xFFFFFF;
    s->BkColor = bk & 0xFFFFFF;
    s->BrushColor = brush & 0xFFFFFF;
    s->ViewportOrg = (POINT){(LONG)R32(viewport, 0), (LONG)R32(viewport, 4)};
}

static void take_colors(DC *dc, const BATCH_SAVED *saved)
{
    DC_STATE *s = &dc->State;
    s->TextColor = saved->TextColor;
    s->BkColor = saved->BkColor;
    s->BrushColor = saved->BrushColor;
    s->ViewportOrg = saved->ViewportOrg;
}

static BOOLEAN uses_source(ULONG rop)
{
    UCHAR code = (UCHAR)(rop >> 16);
    return ((code ^ (code << 2)) & 0xCC) != 0;
}

static void run_patblt(DC *dc, const UCHAR *r, ULONG size)
{
    if (size < 0x48 || uses_source(R32(r, 0x20)))
        return;
    BRUSH *brush = (BRUSH *)GdiLookup((HANDLE)(ULONG_PTR)R64(r, 0x18), GDI_TYPE_BRUSH);
    BATCH_SAVED saved;
    put_colors(dc, &saved, R32(r, 0x24), R32(r, 0x28), R32(r, 0x2c), r + 0x34);
    GdiPatBltDc(dc, (int)R32(r, 4), (int)R32(r, 8), (int)R32(r, 12), (int)R32(r, 16), R32(r, 0x20), brush);
    take_colors(dc, &saved);
}

static void run_polypatblt(DC *dc, const UCHAR *r, ULONG size)
{
    if (size < 0x38)
        return;
    ULONG count = R32(r, 12);
    if (count >= 0xAAAAAAA || (ULONG64)count * 24 > size - 48 || R32(r, 8) || uses_source(R32(r, 4)))
        return;
    BATCH_SAVED saved;
    put_colors(dc, &saved, R32(r, 0x10), R32(r, 0x14), R32(r, 0x18), r + 0x28);
    GdiPolyPatBltDc(dc, R32(r, 4), r + 0x30, count);
    take_colors(dc, &saved);
}

static void run_textout(DC *dc, const UCHAR *r, ULONG size)
{
    if (size < 0x54)
        return;
    ULONG options = R32(r, 0x20), count = R32(r, 0x38), advances = R32(r, 0x3c);
    ULONG room = size - 0x54;
    if (count > room / 2)
        return;
    INT *dx = nullptr;
    if (advances) {
        ULONG each = (options & ETO_PDY) ? 8 : 4;
        if (advances > room || advances & 3 || count > (room - advances) / each)
            return;
        dx = W32Alloc((SIZE_T)count * sizeof(INT) + 4);
        if (!dx)
            return;
        for (ULONG i = 0; i < count; i++)
            dx[i] = (INT)R32(r, 0x54 + advances + i * each);
    }
    const RECT *rect = (LONG)options >= 0 ? (const RECT *)(r + 0x24) : nullptr;
    options &= 0x7FFFFFFF;
    DC_STATE *s = &dc->State;
    BATCH_SAVED saved;
    put_colors(dc, &saved, R32(r, 4), R32(r, 8), s->BrushColor, r + 0x4c);
    int mode = s->BkMode;
    UINT align = s->TextAlign;
    s->BkMode = R32(r, 12) == 2 ? OPAQUE : TRANSPARENT;
    s->TextAlign = R32(r, 0x48);
    FONT *font = (FONT *)GdiLookup((HANDLE)(ULONG_PTR)R64(r, 0x40), GDI_TYPE_FONT);
    FONT *was = s->Font;
    if (font && font != was)
        DcSelectFont(dc, font);
    TextDraw(dc, (LONG)R32(r, 0x18), (LONG)R32(r, 0x1c), options, rect, (PCWSTR)(r + 0x54), count, dx);
    if (s->Font != was)
        DcSelectFont(dc, was);
    s->BkMode = mode;
    s->TextAlign = align;
    take_colors(dc, &saved);
    W32Free(dx);
}

static void run_textout_rect(DC *dc, const UCHAR *r, ULONG size)
{
    if (size < 0x28)
        return;
    BATCH_SAVED saved;
    put_colors(dc, &saved, dc->State.TextColor, R32(r, 4), dc->State.BrushColor, r + 0x1c);
    TextDraw(dc, 0, 0, ETO_OPAQUE, (const RECT *)(r + 12), L"", 0, nullptr);
    take_colors(dc, &saved);
}

static void run_select_clip(DC *dc, const UCHAR *r, ULONG size)
{
    if (size < 0x18)
        return;
    ULONG mode = R32(r, 4);
    if (mode & 0x8000000) {
        DcSelectClip(dc, nullptr, RGN_COPY);
        return;
    }
    REGION rect;
    RgnInit(&rect);
    RgnSetRect(&rect, (const RECT *)(r + 8));
    DcSelectClip(dc, &rect, (int)mode);
    RgnFree(&rect);
}

static void run_delete(const UCHAR *r, ULONG size)
{
    if (size >= 0x10)
        NtGdiDeleteObjectApp((HANDLE)(ULONG_PTR)R64(r, 8));
}

static void run(DC *dc, const BATCH *b, ULONG count)
{
    const UCHAR *p = b->Buffer, *end = b->Buffer + BATCH_BYTES;
    while (count-- && p + 4 <= end) {
        ULONG size = *(const USHORT *)p, kind = *(const USHORT *)(p + 2);
        if (p + size > end)
            break;
        if (kind == BATCH_DELETEBRUSH || kind == BATCH_DELETEREGION) {
            run_delete(p, size);
        } else if (dc) {
            switch (kind) {
            case BATCH_PATBLT:
                run_patblt(dc, p, size);
                break;
            case BATCH_POLYPATBLT:
                run_polypatblt(dc, p, size);
                break;
            case BATCH_TEXTOUT:
                run_textout(dc, p, size);
                break;
            case BATCH_TEXTOUTRECT:
                run_textout_rect(dc, p, size);
                break;
            case BATCH_BRUSHORG:
                if (size >= 12)
                    dc->State.BrushOrg = (POINT){(LONG)R32(p, 4), (LONG)R32(p, 8)};
                break;
            case BATCH_SELECTCLIP:
                run_select_clip(dc, p, size);
                break;
            case BATCH_SELECTFONT: {
                FONT *f = size >= 0x10 ? (FONT *)GdiLookup((HANDLE)(ULONG_PTR)R64(p, 8), GDI_TYPE_FONT) : nullptr;
                if (f)
                    DcSelectFont(dc, f);
                break;
            }
            }
        }
        p += (size + 7) & ~7u;
    }
}

/*
 * takes the batch out of the teb, empty, and runs it, before every service of win32k as windows does. gdi32 makes
 * such a call itself for GdiFlush, when the batch is full, when another dc comes and at GdiBatchLimit records.
 * measured with bq.exe of scratch/dlls/gdibatch in the research repository.
 */
void NTAPI GdiFlushUserBatch(void)
{
    PUCHAR teb = PsGetCurrentThreadTeb();
    ULONG count = 0;
    if (!teb || !NT_SUCCESS(MimeCopyFromUser(&count, teb + FIELD_OFFSET(TEB, GdiBatchCount), sizeof(count))) || !count)
        return;
    BATCH *b = W32Alloc(sizeof(BATCH));
    if (!b)
        return;
    BOOLEAN read = NT_SUCCESS(MimeCopyFromUser(b, teb + FIELD_OFFSET(TEB, GdiTebBatch), sizeof(BATCH)));
    ULONG zero = 0;
    BATCH head = {0};
    MimeCopyToUser(teb + FIELD_OFFSET(TEB, GdiBatchCount), &zero, sizeof(zero));
    MimeCopyToUser(teb + FIELD_OFFSET(TEB, GdiTebBatch), &head, FIELD_OFFSET(BATCH, Buffer));
    if (read && count - 1 < BATCH_MAX_COUNT) {
        UserEnter();
        DC *dc = b->Hdc ? DcLookup((HDC)(ULONG_PTR)b->Hdc) : nullptr;
        if (dc && dc->Header.OwnerPid != PsGetCurrentProcessId())
            dc = nullptr;
        run(dc, b, count);
        UserLeave();
    }
    W32Free(b);
}

/* a thread that ends with a batch: windows deletes the regions in it and drops the rest. */
void GdiThreadExitBatch(void)
{
    PUCHAR teb = PsGetCurrentThreadTeb();
    ULONG count = 0;
    if (!teb || !NT_SUCCESS(MimeCopyFromUser(&count, teb + FIELD_OFFSET(TEB, GdiBatchCount), sizeof(count))) || !count)
        return;
    BATCH *b = W32Alloc(sizeof(BATCH));
    if (!b)
        return;
    if (NT_SUCCESS(MimeCopyFromUser(b, teb + FIELD_OFFSET(TEB, GdiTebBatch), sizeof(BATCH))) && count - 1 < BATCH_MAX_COUNT) {
        UserEnter();
        const UCHAR *p = b->Buffer, *end = b->Buffer + BATCH_BYTES;
        while (count-- && p + 4 <= end) {
            ULONG size = *(const USHORT *)p;
            if (p + size > end)
                break;
            if (*(const USHORT *)(p + 2) == BATCH_DELETEREGION)
                run_delete(p, size);
            p += (size + 7) & ~7u;
        }
        UserLeave();
    }
    ULONG zero = 0;
    MimeCopyToUser(teb + FIELD_OFFSET(TEB, GdiBatchCount), &zero, sizeof(zero));
    W32Free(b);
}
