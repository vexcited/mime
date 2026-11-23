#include "font_private.h"

#define RT_FONT 0x8008
#define FNT_HEADER 118
#define FNT_HEADER_V3 148

static USHORT le16(const UCHAR *p)
{
    return (USHORT)(p[0] | p[1] << 8);
}

static ULONG le32(const UCHAR *p)
{
    return (ULONG)p[0] | (ULONG)p[1] << 8 | (ULONG)p[2] << 16 | (ULONG)p[3] << 24;
}

/*
 * the raster fonts of windows: a .fon file is a 16 bit NE executable whose RT_FONT resources are FNT fonts, one
 * for each size. a FNT is a header, a table of the width and the bitmap of each character from dfFirstChar to
 * dfLastChar and one more for the default, then the bitmaps, stored in strips of 8 columns, each strip a byte for
 * every row, the leftmost pixel in the high bit. version 2 has 4 byte entries, version 3 adds 30 bytes of header
 * and 6 byte entries.
 */

ULONG FntList(const UCHAR *data, SIZE_T size, ULONG *offsets, ULONG *sizes, ULONG max)
{
    if (size < 0x40 || data[0] != 'M' || data[1] != 'Z')
        return 0;
    ULONG ne = le32(data + 0x3C);
    if ((SIZE_T)ne + 0x40 > size || data[ne] != 'N' || data[ne + 1] != 'E')
        return 0;
    SIZE_T p = (SIZE_T)ne + le16(data + ne + 0x24);
    if (p + 2 > size)
        return 0;
    USHORT shift = le16(data + p);
    p += 2;
    ULONG count = 0;
    while (p + 8 <= size && shift < 16) {
        USHORT type = le16(data + p), entries = le16(data + p + 2);
        if (!type)
            break;
        p += 8;
        for (USHORT i = 0; i < entries && p + 12 <= size; i++, p += 12) {
            ULONG offset = (ULONG)le16(data + p) << shift, length = (ULONG)le16(data + p + 2) << shift;
            if (type == RT_FONT && count < max && offset < size && length <= size - offset) {
                offsets[count] = offset;
                sizes[count] = length;
                count++;
            }
        }
    }
    return count;
}

BOOLEAN FntRead(const UCHAR *fnt, ULONG size, FNT_INFO *info)
{
    memset(info, 0, sizeof(*info));
    if (size < FNT_HEADER)
        return FALSE;
    info->Version = le16(fnt);
    if (info->Version != 0x200 && info->Version != 0x300)
        return FALSE;
    /* bit 0 of dfType is a vector font, which is drawn with strokes and not read here. */
    if (le16(fnt + 66) & 1)
        return FALSE;
    info->Points = le16(fnt + 68);
    info->VertRes = le16(fnt + 70);
    info->HorizRes = le16(fnt + 72);
    info->Ascent = le16(fnt + 74);
    info->InternalLeading = le16(fnt + 76);
    info->ExternalLeading = le16(fnt + 78);
    info->Italic = fnt[80];
    info->Underline = fnt[81];
    info->StrikeOut = fnt[82];
    info->Weight = le16(fnt + 83);
    info->CharSet = fnt[85];
    info->PixWidth = le16(fnt + 86);
    info->PixHeight = le16(fnt + 88);
    info->PitchAndFamily = fnt[90];
    info->AvgWidth = le16(fnt + 91);
    info->MaxWidth = le16(fnt + 93);
    info->FirstChar = fnt[95];
    info->LastChar = fnt[96];
    info->DefaultChar = fnt[97];
    info->BreakChar = fnt[98];
    info->Table = info->Version == 0x300 ? FNT_HEADER_V3 : FNT_HEADER;
    info->Size = size;
    ULONG face = le32(fnt + 105);
    ULONG entries = (ULONG)(info->LastChar - info->FirstChar + 2);
    if (!info->PixHeight || info->LastChar < info->FirstChar ||
        info->Table + entries * (info->Version == 0x300 ? 6u : 4u) > size || face >= size)
        return FALSE;
    ULONG n = 0;
    while (face + n < size && fnt[face + n] && n + 1 < RTL_NUMBER_OF(info->Face)) {
        info->Face[n] = fnt[face + n];
        n++;
    }
    info->Face[n] = 0;
    return n != 0;
}

/* the width and the bitmap of a character of the font, FALSE outside its range. */
BOOLEAN FntGlyph(const UCHAR *fnt, const FNT_INFO *info, ULONG code, ULONG *width, ULONG *bits)
{
    if (code < info->FirstChar || code > (ULONG)info->LastChar + 1)
        return FALSE;
    ULONG i = code - info->FirstChar;
    const UCHAR *e = fnt + info->Table + i * (info->Version == 0x300 ? 6u : 4u);
    *width = le16(e);
    *bits = info->Version == 0x300 ? le32(e + 2) : le16(e + 2);
    ULONG strips = (*width + 7) / 8;
    return *bits < info->Size && (ULONG64)strips * info->PixHeight <= info->Size - *bits;
}

BOOLEAN FntPixel(const UCHAR *fnt, const FNT_INFO *info, ULONG bits, ULONG x, ULONG y)
{
    return (fnt[bits + (x / 8) * info->PixHeight + y] >> (7 - x % 8)) & 1;
}

/* the code pages of the raster fonts: their upper halves, the lower one is ascii. */
static const USHORT g_cp1252[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D,
    0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A,
    0x0153, 0x009D, 0x017E, 0x0178,
};

static const USHORT g_cp437[128] = {
    0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7, 0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC,
    0x00C4, 0x00C5, 0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9, 0x00FF, 0x00D6, 0x00DC, 0x00A2,
    0x00A3, 0x00A5, 0x20A7, 0x0192, 0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA, 0x00BF, 0x2310,
    0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB, 0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510, 0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C,
    0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559,
    0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580, 0x03B1, 0x00DF,
    0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4, 0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
    0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248, 0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2,
    0x25A0, 0x00A0,
};

WCHAR FntToUnicode(UINT codepage, UCHAR c)
{
    if (c < 0x80)
        return c;
    if (codepage == 437)
        return g_cp437[c - 0x80];
    if (codepage == 1252 && c < 0xA0)
        return g_cp1252[c - 0x80];
    return c;
}

/* the byte of a character in the code page, -1 when it has none. */
int FntFromUnicode(UINT codepage, WCHAR c)
{
    if (c < 0x80)
        return c;
    for (ULONG b = 0x80; b < 0x100; b++)
        if (FntToUnicode(codepage, (UCHAR)b) == c)
            return (int)b;
    return -1;
}
