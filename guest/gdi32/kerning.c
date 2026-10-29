/*
 * GetKerningPairsA and GetKerningPairsW: the pairs of the kern table of the font in the device context, with the
 * glyphs of the table named by the characters the font maps to them. the amounts are scaled to logical units.
 */
#include "gdi32_private.h"
#include <mime/win32/wingdi.h>

#define TAG(a, b, c, d) ((DWORD)(a) | (DWORD)(b) << 8 | (DWORD)(c) << 16 | (DWORD)(d) << 24)

typedef struct KERNINGPAIR_ {
    WORD wFirst;
    WORD wSecond;
    int iKernAmount;
} KERNINGPAIR_;

static WORD be16(const BYTE *p)
{
    return (WORD)(p[0] << 8 | p[1]);
}

static DWORD be32(const BYTE *p)
{
    return (DWORD)p[0] << 24 | (DWORD)p[1] << 16 | (DWORD)p[2] << 8 | p[3];
}

static void *table(HDC dc, DWORD tag, DWORD *size)
{
    *size = GetFontData(dc, tag, 0, nullptr, 0);
    if (*size == GDI_ERROR || *size == 0)
        return nullptr;
    BYTE *data = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, *size);
    if (data && GetFontData(dc, tag, 0, data, *size) != *size) {
        RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, data);
        return nullptr;
    }
    return data;
}

/* the first character each glyph is mapped from, by the unicode cmap of formats 4 and 12. */
static BOOL map_glyphs(const BYTE *cmap, DWORD size, WORD *characters, DWORD glyph_count)
{
    if (size < 4)
        return FALSE;
    WORD count = be16(cmap + 2);
    DWORD offset = 0, best = 0;
    for (WORD i = 0; i < count && 4 + 8u * (i + 1) <= size; i++) {
        WORD platform = be16(cmap + 4 + 8 * i), encoding = be16(cmap + 6 + 8 * i);
        DWORD at = be32(cmap + 8 + 8 * i);
        DWORD rank = platform == 3 && encoding == 1 ? 2 : platform == 0 ? 1 : 0;
        if (rank > best && at + 4 <= size) {
            best = rank;
            offset = at;
        }
    }
    if (!best)
        return FALSE;
    const BYTE *sub = cmap + offset;
    DWORD room = size - offset;
    if (be16(sub) == 4 && room >= 14) {
        WORD segments = be16(sub + 6) / 2;
        if (16u + 8u * segments > room)
            return FALSE;
        const BYTE *ends = sub + 14, *starts = ends + 2 * segments + 2, *deltas = starts + 2 * segments,
                   *ranges = deltas + 2 * segments;
        for (WORD s = 0; s < segments; s++) {
            WORD end = be16(ends + 2 * s), start = be16(starts + 2 * s), delta = be16(deltas + 2 * s),
                 range = be16(ranges + 2 * s);
            for (DWORD c = start; c <= end && c != 0xFFFF; c++) {
                DWORD glyph;
                if (!range) {
                    glyph = (c + delta) & 0xFFFF;
                } else {
                    const BYTE *at = ranges + 2 * s + range + 2 * (c - start);
                    if (at + 2 > cmap + size)
                        break;
                    glyph = be16(at);
                    if (glyph)
                        glyph = (glyph + delta) & 0xFFFF;
                }
                if (glyph && glyph < glyph_count && !characters[glyph])
                    characters[glyph] = (WORD)c;
            }
        }
        return TRUE;
    }
    if (be16(sub) == 12 && room >= 16) {
        DWORD groups = be32(sub + 12);
        if (groups > (room - 16) / 12)
            return FALSE;
        for (DWORD g = 0; g < groups; g++) {
            DWORD start = be32(sub + 16 + 12 * g), end = be32(sub + 20 + 12 * g), first = be32(sub + 24 + 12 * g);
            for (DWORD c = start; c <= end && c < 0x10000; c++) {
                DWORD glyph = first + (c - start);
                if (glyph && glyph < glyph_count && !characters[glyph])
                    characters[glyph] = (WORD)c;
            }
        }
        return TRUE;
    }
    return FALSE;
}

/* the pairs go to the buffer when there is one, and the number of them is returned either way. */
static DWORD collect(HDC dc, DWORD capacity, KERNINGPAIR_ *out, BOOL *enough)
{
    *enough = TRUE;
    DWORD kern_size, cmap_size, head_size, maxp_size, count = 0;
    BYTE *kern = table(dc, TAG('k', 'e', 'r', 'n'), &kern_size);
    BYTE *cmap = table(dc, TAG('c', 'm', 'a', 'p'), &cmap_size);
    BYTE *head = table(dc, TAG('h', 'e', 'a', 'd'), &head_size);
    BYTE *maxp = table(dc, TAG('m', 'a', 'x', 'p'), &maxp_size);
    WORD *characters = nullptr;
    TEXTMETRICW metrics;
    if (kern && cmap && head && head_size >= 20 && maxp && maxp_size >= 6 && kern_size >= 4 &&
        GetTextMetricsW(dc, &metrics)) {
        DWORD glyphs = be16(maxp + 4), units = be16(head + 18);
        LONG em = metrics.tmHeight - metrics.tmInternalLeading;
        characters = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, HEAP_ZERO_MEMORY, glyphs * sizeof(WORD));
        if (characters && units && map_glyphs(cmap, cmap_size, characters, glyphs)) {
            DWORD tables = be16(kern + 2), at = 4;
            for (DWORD t = 0; t < tables && at + 14 <= kern_size; t++) {
                const BYTE *sub = kern + at;
                WORD length = be16(sub + 2), coverage = be16(sub + 4), pairs = be16(sub + 6);
                /* horizontal, format 0 subtables only. */
                if ((coverage & 1) && !(coverage & 4) && (coverage >> 8) == 0) {
                    for (WORD p = 0; p < pairs && at + 14 + 6u * (p + 1) <= kern_size; p++) {
                        const BYTE *pair = sub + 14 + 6 * p;
                        WORD left = be16(pair), right = be16(pair + 2);
                        int value = (short)be16(pair + 4);
                        if (left >= glyphs || right >= glyphs || !characters[left] || !characters[right] || !value)
                            continue;
                        if (out && count < capacity) {
                            out[count].wFirst = characters[left];
                            out[count].wSecond = characters[right];
                            out[count].iKernAmount = (int)(((LONGLONG)value * em + (value < 0 ? -(LONGLONG)units / 2 : units / 2)) / units);
                        } else if (out) {
                            *enough = FALSE;
                        }
                        count++;
                    }
                }
                at += length ? length : kern_size;
            }
        }
    }
    HANDLE heap = NtCurrentPeb()->ProcessHeap;
    if (characters)
        RtlFreeHeap(heap, 0, characters);
    BYTE *blocks[4] = {kern, cmap, head, maxp};
    for (int i = 0; i < 4; i++)
        if (blocks[i])
            RtlFreeHeap(heap, 0, blocks[i]);
    return count;
}

WINGDIAPI DWORD WINAPI GetKerningPairsW(HDC dc, DWORD count, KERNINGPAIR_ *pairs)
{
    if (!pairs && count) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    BOOL enough;
    DWORD total = collect(dc, count, pairs, &enough);
    if (pairs && !enough) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    return total;
}

/* the characters of the pairs are the ones of the font's code page, the pairs that have none left out. */
WINGDIAPI DWORD WINAPI GetKerningPairsA(HDC dc, DWORD count, KERNINGPAIR_ *pairs)
{
    if (!pairs && count) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    BOOL enough;
    DWORD wide = collect(dc, 0xFFFFFFFF, nullptr, &enough);
    if (!wide)
        return 0;
    KERNINGPAIR_ *all = RtlAllocateHeap(NtCurrentPeb()->ProcessHeap, 0, wide * sizeof(*all));
    if (!all) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return 0;
    }
    collect(dc, wide, all, &enough);
    UINT charset = CP_ACP;
    DWORD total = 0;
    for (DWORD i = 0; i < wide; i++) {
        char first[4], second[4];
        WCHAR a = all[i].wFirst, b = all[i].wSecond;
        BOOL lost = FALSE;
        int n1 = WideCharToMultiByte(charset, 0, &a, 1, first, sizeof(first), nullptr, &lost);
        BOOL lost2 = FALSE;
        int n2 = WideCharToMultiByte(charset, 0, &b, 1, second, sizeof(second), nullptr, &lost2);
        if (n1 != 1 || n2 != 1 || lost || lost2)
            continue;
        if (pairs && total < count) {
            pairs[total].wFirst = (BYTE)first[0];
            pairs[total].wSecond = (BYTE)second[0];
            pairs[total].iKernAmount = all[i].iKernAmount;
        } else if (pairs) {
            RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, all);
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return 0;
        }
        total++;
    }
    RtlFreeHeap(NtCurrentPeb()->ProcessHeap, 0, all);
    return total;
}
