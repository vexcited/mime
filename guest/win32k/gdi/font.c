/*
 * fonts: the face registry built from \SystemRoot\Fonts, logical font
 * matching, realization at a pixel size and the glyph cache.
 *
 * font files are large, so registration only reads the table directory
 * and the name, OS/2 and head tables; a file is loaded whole the first
 * time one of its faces is realized.
 */
#include "../win32k.h"
#include "ttf/ttf.h"

#define MAX_FACES 512
#define FONT_TAG W32_TAG('W', 'f', 'n', 't')

typedef struct FONT_FILE {
    WCHAR Path[MAX_PATH];
    PUCHAR Data;
    SIZE_T Size;
    BOOLEAN Failed;
} FONT_FILE;

typedef struct FONT_FACE {
    FONT_FILE *File;
    ULONG FaceIndex;
    WCHAR Family[LF_FACESIZE];
    WCHAR TypoFamily[LF_FACESIZE]; /* name id 16 when present, for weight variants */
    WCHAR FullName[LF_FULLFACESIZE];
    LONG Weight;
    BOOLEAN Italic;
    BOOLEAN Fixed;
    BOOLEAN Symbol;
    BOOLEAN Loaded;
    MimeTtfFont Ttf;
    MimeTtfMetrics Metrics;
} FONT_FACE;

static FONT_FACE *g_faces;
static ULONG g_face_count;
static LIST_ENTRY g_instances = {&g_instances, &g_instances};
LOGFONTW W32SystemFont;

static void *alloc_cb(void *ctx, size_t size)
{
    return ExAllocatePool2(POOL_FLAG_NON_PAGED, size, FONT_TAG);
}

static void free_cb(void *ctx, void *p)
{
    ExFreePool(p);
}

static const MimeAllocator g_alloc = {alloc_cb, free_cb, NULL};

/* ------------------------------------------------------------------ file access */

static HANDLE open_font_file(PCWSTR path)
{
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, path);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    IO_STATUS_BLOCK iosb;
    HANDLE h;
    if (!NT_SUCCESS(ZwCreateFile(&h, FILE_READ_DATA | SYNCHRONIZE, &oa, &iosb, NULL, 0, FILE_SHARE_READ, FILE_OPEN,
                                 FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, NULL, 0)))
        return NULL;
    return h;
}

static BOOLEAN read_at(HANDLE h, ULONG64 offset, PVOID buffer, ULONG length)
{
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER off;
    off.QuadPart = (LONGLONG)offset;
    NTSTATUS status = ZwReadFile(h, NULL, NULL, NULL, &iosb, buffer, length, &off, NULL);
    return NT_SUCCESS(status) && iosb.Information == length;
}

static ULONG64 file_size(HANDLE h)
{
    FILE_STANDARD_INFORMATION info;
    IO_STATUS_BLOCK iosb;
    if (!NT_SUCCESS(ZwQueryInformationFile(h, &iosb, &info, sizeof(info), FileStandardInformation)))
        return 0;
    return (ULONG64)info.EndOfFile.QuadPart;
}

static BOOLEAN load_file(FONT_FILE *f)
{
    if (f->Data)
        return TRUE;
    if (f->Failed)
        return FALSE;
    HANDLE h = open_font_file(f->Path);
    if (h) {
        ULONG64 size = file_size(h);
        if (size && size < 64 * 1024 * 1024) {
            f->Data = ExAllocatePool2(POOL_FLAG_NON_PAGED, size, FONT_TAG);
            if (f->Data && read_at(h, 0, f->Data, (ULONG)size)) {
                f->Size = size;
            } else if (f->Data) {
                ExFreePool(f->Data);
                f->Data = NULL;
            }
        }
        ZwClose(h);
    }
    if (!f->Data) {
        f->Failed = TRUE;
        W32Trace("win32k: cannot load font %ws\n", f->Path);
    }
    return f->Data != NULL;
}

/* ------------------------------------------------------------------ registration */

static USHORT be16(const UCHAR *p)
{
    return (USHORT)(p[0] << 8 | p[1]);
}

static ULONG be32(const UCHAR *p)
{
    return (ULONG)p[0] << 24 | (ULONG)p[1] << 16 | (ULONG)p[2] << 8 | p[3];
}

/* copies a name record, preferring windows unicode english. */
static BOOLEAN name_record(const UCHAR *name, ULONG size, USHORT id, PWSTR out, ULONG chars)
{
    if (size < 6)
        return FALSE;
    USHORT count = be16(name + 2), strings = be16(name + 4);
    LONG best = -1, best_score = -1;
    for (USHORT i = 0; i < count && 6u + (i + 1u) * 12u <= size; i++) {
        const UCHAR *r = name + 6 + i * 12;
        USHORT platform = be16(r), encoding = be16(r + 2), lang = be16(r + 4), nid = be16(r + 6);
        if (nid != id || platform != 3 || (encoding != 1 && encoding != 0 && encoding != 10))
            continue;
        LONG score = lang == 0x0409 ? 2 : 1;
        if (score > best_score) {
            best_score = score;
            best = i;
        }
    }
    if (best < 0)
        return FALSE;
    const UCHAR *r = name + 6 + best * 12;
    ULONG length = be16(r + 8), offset = be16(r + 10);
    if ((ULONG)strings + offset + length > size)
        return FALSE;
    ULONG n = length / 2;
    if (n >= chars)
        n = chars - 1;
    for (ULONG k = 0; k < n; k++)
        out[k] = be16(name + strings + offset + 2 * k);
    out[n] = 0;
    return TRUE;
}

static void register_face(FONT_FILE *file, HANDLE h, ULONG64 dir_offset, ULONG face_index)
{
    UCHAR header[12];
    if (!read_at(h, dir_offset, header, sizeof(header)))
        return;
    ULONG version = be32(header);
    if (version != 0x00010000 && version != 0x4F54544F && version != 0x74727565)
        return;
    USHORT tables = be16(header + 4);
    if (tables > 64)
        return;
    UCHAR dir[64 * 16];
    if (!read_at(h, dir_offset + 12, dir, tables * 16u))
        return;
    ULONG name_off = 0, name_len = 0, os2_off = 0, os2_len = 0, cmap_off = 0;
    BOOLEAN glyf = FALSE;
    for (USHORT i = 0; i < tables; i++) {
        const UCHAR *t = dir + i * 16;
        ULONG tag = be32(t), off = be32(t + 8), len = be32(t + 12);
        if (tag == 0x6E616D65) { /* name */
            name_off = off;
            name_len = len;
        } else if (tag == 0x4F532F32) { /* OS/2 */
            os2_off = off;
            os2_len = len;
        } else if (tag == 0x676C7966) { /* glyf */
            glyf = TRUE;
        } else if (tag == 0x636D6170) { /* cmap */
            cmap_off = off;
        }
    }
    if (!name_off || !name_len || name_len > 512 * 1024 || !glyf || g_face_count >= MAX_FACES)
        return;
    UCHAR *name = ExAllocatePool2(POOL_FLAG_NON_PAGED, name_len, FONT_TAG);
    if (!name)
        return;
    FONT_FACE *face = &g_faces[g_face_count];
    memset(face, 0, sizeof(*face));
    if (read_at(h, name_off, name, name_len) && name_record(name, name_len, 1, face->Family, LF_FACESIZE)) {
        if (!name_record(name, name_len, 16, face->TypoFamily, LF_FACESIZE))
            wcscpy(face->TypoFamily, face->Family);
        if (!name_record(name, name_len, 4, face->FullName, LF_FULLFACESIZE))
            wcscpy(face->FullName, face->Family);
        face->File = file;
        face->FaceIndex = face_index;
        face->Weight = FW_NORMAL;
        UCHAR os2[64];
        if (os2_off && os2_len >= 64 && read_at(h, os2_off, os2, sizeof(os2))) {
            face->Weight = be16(os2 + 4);
            face->Italic = (be16(os2 + 62) & 1) != 0;
            face->Fixed = os2[32 + 3] == 9; /* panose proportion monospaced */
        }
        (void)cmap_off;
        g_face_count++;
    }
    ExFreePool(name);
}

static void register_file(PCWSTR path)
{
    HANDLE h = open_font_file(path);
    if (!h)
        return;
    FONT_FILE *file = W32Alloc(sizeof(FONT_FILE));
    if (!file) {
        ZwClose(h);
        return;
    }
    wcscpy(file->Path, path);
    ULONG before = g_face_count;
    UCHAR header[12];
    if (read_at(h, 0, header, sizeof(header))) {
        if (be32(header) == 0x74746366) { /* ttcf */
            ULONG count = be32(header + 8);
            if (count > 32)
                count = 32;
            UCHAR offsets[32 * 4];
            if (read_at(h, 12, offsets, count * 4))
                for (ULONG i = 0; i < count; i++)
                    register_face(file, h, be32(offsets + i * 4), i);
        } else {
            register_face(file, h, 0, 0);
        }
    }
    ZwClose(h);
    if (g_face_count == before)
        W32Free(file);
}

static BOOLEAN has_suffix(PCWSTR name, ULONG chars, PCWSTR suffix)
{
    ULONG n = (ULONG)wcslen(suffix);
    return chars >= n && !_wcsnicmp(name + chars - n, suffix, n);
}

void FontInitialize(void)
{
    g_faces = W32Alloc(MAX_FACES * sizeof(FONT_FACE));
    if (!g_faces)
        return;
    UNICODE_STRING dir_name = RTL_CONSTANT_STRING(L"\\SystemRoot\\Fonts");
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &dir_name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, NULL, NULL);
    IO_STATUS_BLOCK iosb;
    HANDLE dir;
    NTSTATUS status = ZwCreateFile(&dir, FILE_LIST_DIRECTORY | SYNCHRONIZE, &oa, &iosb, NULL, 0,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN,
                                   FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, NULL, 0);
    if (!NT_SUCCESS(status)) {
        W32Trace("win32k: cannot open the fonts directory: %08x\n", status);
        return;
    }
    UCHAR *buffer = W32Alloc(16384);
    BOOLEAN restart = TRUE;
    while (buffer) {
        status = ZwQueryDirectoryFile(dir, NULL, NULL, NULL, &iosb, buffer, 16384, FileDirectoryInformation, FALSE,
                                      NULL, restart);
        restart = FALSE;
        if (!NT_SUCCESS(status))
            break;
        FILE_DIRECTORY_INFORMATION *info = (FILE_DIRECTORY_INFORMATION *)buffer;
        for (;;) {
            ULONG chars = info->FileNameLength / sizeof(WCHAR);
            if (has_suffix(info->FileName, chars, L".ttf") || has_suffix(info->FileName, chars, L".ttc") ||
                has_suffix(info->FileName, chars, L".otf")) {
                WCHAR path[MAX_PATH];
                wcscpy(path, L"\\SystemRoot\\Fonts\\");
                ULONG base = (ULONG)wcslen(path);
                if (base + chars < MAX_PATH) {
                    memcpy(path + base, info->FileName, chars * sizeof(WCHAR));
                    path[base + chars] = 0;
                    register_file(path);
                }
            }
            if (!info->NextEntryOffset)
                break;
            info = (FILE_DIRECTORY_INFORMATION *)((UCHAR *)info + info->NextEntryOffset);
        }
    }
    W32Free(buffer);
    ZwClose(dir);
    W32Trace("win32k: %u font faces registered\n", g_face_count);
}

/* ------------------------------------------------------------------ matching */

typedef struct SUBSTITUTE {
    PCWSTR From;
    PCWSTR To;
} SUBSTITUTE;

/* the FontSubstitutes registry key of windows, plus bitmap faces mapped to outline ones. */
static const SUBSTITUTE g_substitutes[] = {
    {L"MS Shell Dlg", L"Microsoft Sans Serif"},
    {L"MS Shell Dlg 2", L"Tahoma"},
    {L"MS Sans Serif", L"Microsoft Sans Serif"},
    {L"Helv", L"Microsoft Sans Serif"},
    {L"Helvetica", L"Arial"},
    {L"Tms Rmn", L"Times New Roman"},
    {L"Times", L"Times New Roman"},
    {L"MS Serif", L"Times New Roman"},
    {L"Courier", L"Courier New"},
    {L"System", L"Segoe UI"},
    {L"Fixedsys", L"Consolas"},
    {L"Terminal", L"Consolas"},
    {L"Small Fonts", L"Segoe UI"},
    {L"Segoe UI Variable", L"Segoe UI"},
    {L"Segoe UI Variable Text", L"Segoe UI"},
    {L"Segoe UI Variable Display", L"Segoe UI"},
};

static PCWSTR substitute(PCWSTR name)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_substitutes); i++)
        if (!_wcsicmp(name, g_substitutes[i].From))
            return g_substitutes[i].To;
    return name;
}

static FONT_FACE *match_face(const LOGFONTW *lf)
{
    PCWSTR wanted = lf->lfFaceName[0] ? substitute(lf->lfFaceName) : NULL;
    LONG weight = lf->lfWeight ? lf->lfWeight : FW_NORMAL;
    FONT_FACE *best = NULL;
    LONG best_score = 0x7FFFFFFF;
    for (int pass = 0; pass < 2 && !best; pass++) {
        PCWSTR name = wanted;
        if (pass == 1)
            name = (lf->lfPitchAndFamily & 3) == FIXED_PITCH || (lf->lfPitchAndFamily & 0xF0) == FF_MODERN
                       ? L"Consolas"
                       : (lf->lfPitchAndFamily & 0xF0) == FF_ROMAN ? L"Times New Roman" : L"Segoe UI";
        if (!name)
            continue;
        for (ULONG i = 0; i < g_face_count; i++) {
            FONT_FACE *f = &g_faces[i];
            if (_wcsicmp(f->Family, name) && _wcsicmp(f->TypoFamily, name) && _wcsicmp(f->FullName, name))
                continue;
            LONG score = (f->Weight > weight ? f->Weight - weight : weight - f->Weight);
            /* prefer lighter faces for weights below normal, like gdi. */
            if (weight <= FW_NORMAL && f->Weight > weight)
                score += 50;
            if (!!f->Italic != !!lf->lfItalic)
                score += 1000;
            if (!_wcsicmp(f->Family, name))
                score -= 1; /* exact family beats typographic family */
            if (score < best_score) {
                best_score = score;
                best = f;
            }
        }
    }
    if (!best && g_face_count)
        best = &g_faces[0];
    return best;
}

static BOOLEAN load_face(FONT_FACE *f)
{
    if (f->Loaded)
        return TRUE;
    if (!load_file(f->File))
        return FALSE;
    if (!mime_ttf_open(&f->Ttf, f->File->Data, f->File->Size, f->FaceIndex))
        return FALSE;
    mime_ttf_metrics(&f->Ttf, &f->Metrics);
    f->Loaded = TRUE;
    return TRUE;
}

static LONG round_f(float v)
{
    return (LONG)(v >= 0 ? v + 0.5f : v - 0.5f);
}

static void compute_metrics(FONT_INSTANCE *fi, const LOGFONTW *lf)
{
    FONT_FACE *face = fi->Face;
    const MimeTtfMetrics *m = &face->Metrics;
    TEXTMETRICW *tm = &fi->Metrics;
    memset(tm, 0, sizeof(*tm));
    LONG ascent = round_f((float)m->win_ascent * fi->ScaleY);
    LONG descent = round_f((float)m->win_descent * fi->ScaleY);
    tm->tmAscent = ascent;
    tm->tmDescent = descent;
    tm->tmHeight = ascent + descent;
    tm->tmInternalLeading = tm->tmHeight - fi->EmPixels;
    if (tm->tmInternalLeading < 0)
        tm->tmInternalLeading = 0;
    LONG gap = m->line_gap + m->ascender - m->descender - (m->win_ascent + m->win_descent);
    tm->tmExternalLeading = gap > 0 ? round_f((float)gap * fi->ScaleY) : 0;
    tm->tmAveCharWidth = round_f((float)m->avg_char_width * fi->ScaleX);
    tm->tmMaxCharWidth = round_f((float)m->max_advance * fi->ScaleX);
    tm->tmWeight = fi->Weight;
    tm->tmOverhang = 0;
    tm->tmDigitizedAspectX = 96;
    tm->tmDigitizedAspectY = 96;
    tm->tmFirstChar = 0x1E;
    tm->tmLastChar = 0xFFFC;
    tm->tmDefaultChar = 0x1F;
    tm->tmBreakChar = 0x20;
    tm->tmItalic = fi->Italic ? 255 : 0;
    tm->tmUnderlined = fi->Underline ? 255 : 0;
    tm->tmStruckOut = fi->StrikeOut ? 255 : 0;
    /* note the inverted meaning of TMPF_FIXED_PITCH: set for variable pitch fonts. */
    tm->tmPitchAndFamily = (face->Fixed ? 0 : TMPF_FIXED_PITCH) | TMPF_VECTOR | TMPF_TRUETYPE |
                           (lf->lfPitchAndFamily & 0xF0 ? lf->lfPitchAndFamily & 0xF0 : FF_SWISS);
    tm->tmCharSet = face->Symbol ? SYMBOL_CHARSET : ANSI_CHARSET;
    fi->UnderlinePos = -round_f((float)m->underline_position * fi->ScaleY);
    fi->UnderlineSize = W32Max(1, round_f((float)m->underline_thickness * fi->ScaleY));
    fi->StrikeoutPos = round_f((float)m->strikeout_position * fi->ScaleY);
}

FONT_INSTANCE *FontRealize(const LOGFONTW *lf, UINT dpi)
{
    FONT_FACE *face = match_face(lf);
    if (!face || !load_face(face))
        return NULL;
    const MimeTtfMetrics *m = &face->Metrics;
    LONG em;
    if (lf->lfHeight < 0)
        em = -lf->lfHeight;
    else if (lf->lfHeight > 0)
        em = W32MulDiv(lf->lfHeight, m->units_per_em, m->win_ascent + m->win_descent);
    else
        em = W32MulDiv(16 * (LONG)dpi / 96, m->units_per_em, m->win_ascent + m->win_descent);
    if (em < 1)
        em = 1;
    if (em > 2048)
        em = 2048;
    LONG weight = lf->lfWeight ? lf->lfWeight : FW_NORMAL;
    BOOLEAN sim_bold = weight >= FW_SEMIBOLD && face->Weight < FW_SEMIBOLD;
    BOOLEAN sim_italic = lf->lfItalic && !face->Italic;
    for (PLIST_ENTRY e = g_instances.Flink; e != &g_instances; e = e->Flink) {
        FONT_INSTANCE *fi = CONTAINING_RECORD(e, FONT_INSTANCE, Links);
        if (fi->Face == face && fi->EmPixels == em && fi->Width == lf->lfWidth && fi->Weight == weight &&
            fi->Italic == !!lf->lfItalic && fi->Underline == !!lf->lfUnderline && fi->StrikeOut == !!lf->lfStrikeOut)
            return fi;
    }
    FONT_INSTANCE *fi = W32Alloc(sizeof(FONT_INSTANCE));
    if (!fi)
        return NULL;
    fi->Face = face;
    fi->EmPixels = em;
    fi->Width = lf->lfWidth;
    fi->Weight = weight;
    fi->Italic = lf->lfItalic != 0;
    fi->Underline = lf->lfUnderline != 0;
    fi->StrikeOut = lf->lfStrikeOut != 0;
    fi->SimBold = sim_bold;
    fi->SimItalic = sim_italic;
    fi->ScaleY = (float)em / (float)m->units_per_em;
    fi->ScaleX = fi->ScaleY;
    if (lf->lfWidth > 0 && m->avg_char_width > 0)
        fi->ScaleX = (float)lf->lfWidth / (float)m->avg_char_width;
    compute_metrics(fi, lf);
    InsertTailList(&g_instances, &fi->Links);
    return fi;
}

static BOOLEAN render_glyph(FONT_INSTANCE *fi, USHORT index, GLYPH *g)
{
    FONT_FACE *face = fi->Face;
    MimeTtfRenderParams params = {fi->ScaleX, fi->ScaleY, 0, 0, fi->SimItalic ? 0.2f : 0.0f,
                                  fi->SimBold ? (float)W32Max(1, fi->EmPixels / 24) : 0.0f};
    int advance, lsb;
    mime_ttf_glyph_hmetrics(&face->Ttf, index, &advance, &lsb);
    g->Index = index;
    g->Advance = (SHORT)(round_f((float)advance * fi->ScaleX) + (fi->SimBold ? W32Max(1, fi->EmPixels / 24) : 0));
    int x0, y0, x1, y1;
    if (!mime_ttf_glyph_box_ex(&face->Ttf, index, &params, &x0, &y0, &x1, &y1) || x1 <= x0 || y1 <= y0) {
        g->Width = g->Height = 0;
        return TRUE;
    }
    LONG w = x1 - x0, h = y1 - y0;
    if (w > 4096 || h > 4096)
        return FALSE;
    UCHAR *cov = W32Alloc((SIZE_T)w * (SIZE_T)h);
    if (!cov)
        return FALSE;
    if (!mime_ttf_render_glyph_ex(&face->Ttf, &g_alloc, index, &params, cov, (int)w, (int)h, (int)w)) {
        W32Free(cov);
        g->Width = g->Height = 0;
        return TRUE;
    }
    g->Left = (SHORT)x0;
    g->Top = (SHORT)y0;
    g->Width = (USHORT)w;
    g->Height = (USHORT)h;
    g->Coverage = cov;
    return TRUE;
}

/* glyph for a code point in the basic multilingual plane; others use the missing glyph. */
const GLYPH *FontGetGlyph(FONT_INSTANCE *fi, ULONG codepoint)
{
    if (codepoint > 0xFFFF)
        codepoint = 0xFFFD;
    GLYPH **page = &fi->Pages[codepoint >> 8];
    if (!*page) {
        *page = W32Alloc(256 * sizeof(GLYPH));
        if (!*page)
            return NULL;
        for (ULONG i = 0; i < 256; i++)
            (*page)[i].Index = 0xFFFF; /* not rendered yet */
    }
    GLYPH *g = &(*page)[codepoint & 0xFF];
    if (g->Index == 0xFFFF) {
        USHORT index = mime_ttf_glyph_index(&fi->Face->Ttf, codepoint);
        if (!render_glyph(fi, index, g)) {
            g->Index = 0;
            g->Width = g->Height = 0;
            g->Advance = 0;
        }
    }
    return g;
}

void FontGetFaceName(FONT_INSTANCE *fi, PWSTR name, ULONG chars)
{
    if (!chars)
        return;
    ULONG n = (ULONG)wcslen(fi->Face->Family);
    if (n >= chars)
        n = chars - 1;
    memcpy(name, fi->Face->Family, n * sizeof(WCHAR));
    name[n] = 0;
}

LONG FontTextWidth(FONT_INSTANCE *fi, PCWSTR text, ULONG count, LONG extra)
{
    LONG w = 0;
    for (ULONG i = 0; i < count; i++) {
        const GLYPH *g = FontGetGlyph(fi, text[i]);
        if (g)
            w += g->Advance + extra;
    }
    return w;
}

/* fonts of the non client metrics; windows 11 uses segoe ui 9 point throughout. */
LOGFONTW W32MessageFontForDpi(UINT dpi)
{
    LOGFONTW lf;
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -W32MulDiv(9, (LONG)dpi, 72);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = DEFAULT_QUALITY;
    wcscpy(lf.lfFaceName, L"Segoe UI");
    return lf;
}

LOGFONTW W32CaptionFontForDpi(UINT dpi)
{
    return W32MessageFontForDpi(dpi);
}
