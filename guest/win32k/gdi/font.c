/*
 * fonts: the face registry built from \SystemRoot\Fonts, logical font matching, realization at a pixel size and
 * the glyph cache.
 *
 * font files are large, so registration only reads the table directory and the name, OS/2 and head tables. a file
 * is loaded whole the first time one of its faces is realized.
 */
#include "font_private.h"

#define MAX_FACES 1024
#define FONT_TAG W32_TAG('W', 'f', 'n', 't')

static FONT_FACE *g_faces;
static ULONG g_face_count;
/* set once the kernel thread scanning the fonts directory is done. */
static KEVENT g_scanned;
static BOOLEAN g_scanning;
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

const MimeAllocator FontAllocator = {alloc_cb, free_cb, nullptr};

/* ------------------------------------------------------------------ file access */

static HANDLE open_font_file(PCWSTR path)
{
    UNICODE_STRING name;
    RtlInitUnicodeString(&name, path);
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);
    IO_STATUS_BLOCK iosb;
    HANDLE h;
    if (!NT_SUCCESS(ZwCreateFile(&h, FILE_READ_DATA | SYNCHRONIZE, &oa, &iosb, nullptr, 0, FILE_SHARE_READ, FILE_OPEN,
                                 FILE_SYNCHRONOUS_IO_NONALERT | FILE_NON_DIRECTORY_FILE, nullptr, 0)))
        return nullptr;
    return h;
}

static BOOLEAN read_at(HANDLE h, ULONG64 offset, PVOID buffer, ULONG length)
{
    IO_STATUS_BLOCK iosb;
    LARGE_INTEGER off;
    off.QuadPart = (LONGLONG)offset;
    NTSTATUS status = ZwReadFile(h, nullptr, nullptr, nullptr, &iosb, buffer, length, &off, nullptr);
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

/* font bytes come from an open file during registration, otherwise from memory. */
typedef struct FONT_SOURCE {
    HANDLE File;
    const UCHAR *Data;
    SIZE_T Size;
} FONT_SOURCE;

static BOOLEAN source_read(const FONT_SOURCE *src, ULONG64 offset, PVOID buffer, ULONG length)
{
    if (src->Data) {
        if (offset > src->Size || length > src->Size - offset)
            return FALSE;
        memcpy(buffer, src->Data + offset, length);
        return TRUE;
    }
    return read_at(src->File, offset, buffer, length);
}

static BOOLEAN load_file(FONT_FILE *f)
{
    if (f->Data)
        return TRUE;
    if (f->Failed || f->Memory)
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
                f->Data = nullptr;
            }
        }
        ZwClose(h);
    }
    if (!f->Data) {
        f->Failed = TRUE;
        W32Trace("win32k: cannot load font %ws\n", f->Path);
    }
    return f->Data != nullptr;
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

/* copies a name record, preferring the windows unicode english one. */
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

typedef struct TABLE_REF {
    ULONG Offset;
    ULONG Length;
} TABLE_REF;

static BOOLEAN read_table(const FONT_SOURCE *src, const TABLE_REF *t, PUCHAR buffer, ULONG size)
{
    if (!t->Offset || t->Length < size)
        return FALSE;
    return source_read(src, t->Offset, buffer, size);
}

/* the first and the last character of a format 4 subtable, the unicode cmap of windows. */
static void cmap_range(const FONT_SOURCE *src, const TABLE_REF *cmap, ULONG offset, FONT_FACE *face)
{
    UCHAR h[14];
    if (offset + sizeof(h) > cmap->Length || !source_read(src, cmap->Offset + offset, h, sizeof(h)) || be16(h) != 4)
        return;
    ULONG segments = be16(h + 6) / 2;
    if (!segments || segments > 8192 || offset + 16 + 4ull * segments > cmap->Length)
        return;
    UCHAR *table = ExAllocatePool2(POOL_FLAG_NON_PAGED, 4ull * segments + 2, FONT_TAG);
    if (!table)
        return;
    /* the end codes, a pad and the start codes. */
    if (source_read(src, cmap->Offset + offset + 14, table, 4 * segments + 2)) {
        const UCHAR *ends = table, *starts = table + 2 * segments + 2;
        face->CmapFirst = be16(starts);
        for (ULONG i = 0; i < segments; i++) {
            USHORT start = be16(starts + 2 * i), end = be16(ends + 2 * i);
            if (start == 0xFFFF)
                break;
            face->CmapLast = end;
        }
    }
    ExFreePool(table);
}

static void register_face(FONT_FILE *file, const FONT_SOURCE *src, ULONG64 dir_offset, ULONG face_index)
{
    UCHAR header[12];
    if (!source_read(src, dir_offset, header, sizeof(header)))
        return;
    ULONG version = be32(header);
    if (version != 0x00010000 && version != 0x4F54544F && version != 0x74727565)
        return;
    USHORT tables = be16(header + 4);
    if (tables > 64)
        return;
    UCHAR dir[64 * 16];
    if (!source_read(src, dir_offset + 12, dir, tables * 16u))
        return;
    TABLE_REF name = {0, 0}, os2 = {0, 0}, head = {0, 0}, hhea = {0, 0}, cmap = {0, 0}, post = {0, 0}, vdmx = {0, 0};
    BOOLEAN glyf = FALSE, dsig = FALSE;
    for (USHORT i = 0; i < tables; i++) {
        const UCHAR *t = dir + i * 16;
        ULONG tag = be32(t);
        TABLE_REF ref = {be32(t + 8), be32(t + 12)};
        switch (tag) {
        case 0x6E616D65: /* name */
            name = ref;
            break;
        case 0x4F532F32: /* OS/2 */
            os2 = ref;
            break;
        case 0x68656164: /* head */
            head = ref;
            break;
        case 0x68686561: /* hhea */
            hhea = ref;
            break;
        case 0x636D6170: /* cmap */
            cmap = ref;
            break;
        case 0x706F7374: /* post */
            post = ref;
            break;
        case 0x676C7966: /* glyf */
            glyf = TRUE;
            break;
        case 0x56444D58: /* VDMX */
            vdmx = ref;
            break;
        case 0x44534947: /* DSIG */
            dsig = TRUE;
            break;
        }
    }
    if (!name.Offset || !name.Length || name.Length > 512 * 1024 || !glyf || g_face_count >= MAX_FACES)
        return;
    UCHAR *names = ExAllocatePool2(POOL_FLAG_NON_PAGED, name.Length, FONT_TAG);
    if (!names)
        return;
    FONT_FACE *face = &g_faces[g_face_count];
    memset(face, 0, sizeof(*face));
    if (source_read(src, name.Offset, names, name.Length) &&
        name_record(names, name.Length, 1, face->Family, LF_FACESIZE)) {
        if (!name_record(names, name.Length, 16, face->TypoFamily, LF_FACESIZE))
            wcscpy(face->TypoFamily, face->Family);
        if (!name_record(names, name.Length, 4, face->FullName, LF_FULLFACESIZE))
            wcscpy(face->FullName, face->Family);
        if (!name_record(names, name.Length, 2, face->Style, LF_FACESIZE))
            wcscpy(face->Style, L"Regular");
        face->File = file;
        face->FaceIndex = face_index;
        face->DirOffset = (ULONG)dir_offset;
        face->Weight = FW_NORMAL;
        face->UnitsPerEm = 2048;
        UCHAR h[54];
        if (read_table(src, &head, h, sizeof(h))) {
            face->UnitsPerEm = be16(h + 18) ? be16(h + 18) : 2048;
            USHORT mac_style = be16(h + 44);
            face->Weight = (mac_style & 1) ? FW_BOLD : FW_NORMAL;
            face->Italic = (mac_style & 2) != 0;
            face->XMin = (SHORT)be16(h + 36);
            face->XMax = (SHORT)be16(h + 40);
        }
        face->Signed = dsig;
        face->EnumAscent = face->EnumDescent = -1;
        if (vdmx.Length && vdmx.Length <= 64 * 1024) {
            UCHAR *v = ExAllocatePool2(POOL_FLAG_NON_PAGED, vdmx.Length, FONT_TAG);
            MimeTtfFont t;
            int y_max, y_min;
            memset(&t, 0, sizeof(t));
            if (v && source_read(src, vdmx.Offset, v, vdmx.Length)) {
                t.vdmx.data = v;
                t.vdmx.size = vdmx.Length;
                if (mime_ttf_vdmx(&t, 1, 1, 32, &y_max, &y_min) && y_max >= 0 && y_min <= 0) {
                    face->EnumAscent = (SHORT)y_max;
                    face->EnumDescent = (SHORT)-y_min;
                }
            }
            if (v)
                ExFreePool(v);
        }
        LONG ascender = 0, descender = 0, gap = 0;
        UCHAR hh[36];
        if (read_table(src, &hhea, hh, sizeof(hh))) {
            ascender = (SHORT)be16(hh + 4);
            descender = (SHORT)be16(hh + 6);
            gap = (SHORT)be16(hh + 8);
            face->MaxAdvance = be16(hh + 10);
        }
        face->WinAscent = ascender;
        face->WinDescent = -descender;
        UCHAR o[96];
        memset(o, 0, sizeof(o));
        ULONG os2_size = os2.Length < sizeof(o) ? os2.Length : sizeof(o);
        if (os2_size >= 78 && read_table(src, &os2, o, os2_size)) {
            face->AvgWidth = (SHORT)be16(o + 2);
            face->Weight = be16(o + 4);
            face->Italic = (be16(o + 62) & 1) != 0;
            face->Fixed = o[32 + 3] == 9; /* panose proportion monospaced */
            if (o[32] == 3)
                face->PitchFamily = FF_SCRIPT;
            else if (o[32] == 4)
                face->PitchFamily = FF_DECORATIVE;
            else if (o[32] == 2 && o[33] >= 2 && o[33] <= 10)
                face->PitchFamily = FF_ROMAN;
            else if (o[32] == 2 && o[33] >= 11)
                face->PitchFamily = FF_SWISS;
            face->WinAscent = be16(o + 74);
            face->WinDescent = be16(o + 76);
            for (int k = 0; k < 4; k++)
                face->Signature.fsUsb[k] = be32(o + 42 + 4 * k);
            if (be16(o) >= 1 && os2_size >= 86) {
                face->Signature.fsCsb[0] = be32(o + 78);
                face->Signature.fsCsb[1] = be32(o + 82);
            }
        }
        if (!face->Signature.fsCsb[0])
            face->Signature.fsCsb[0] = 1; /* latin 1 */
        UCHAR pp[16];
        if (read_table(src, &post, pp, sizeof(pp)) && be32(pp + 12))
            face->Fixed = TRUE;
        if (!face->PitchFamily)
            face->PitchFamily = face->Fixed ? FF_MODERN : FF_SWISS;
        LONG leading = gap + ascender - descender - (face->WinAscent + face->WinDescent);
        face->LineGap = leading > 0 ? leading : 0;
        /* a symbol face has a windows symbol cmap subtable. */
        UCHAR c[4 + 8 * 16];
        if (cmap.Offset && cmap.Length >= 4 && source_read(src, cmap.Offset, c, 4)) {
            USHORT n = be16(c + 2);
            if (n > 16)
                n = 16;
            if (cmap.Length >= 4u + 8u * n && source_read(src, cmap.Offset + 4, c + 4, 8u * n))
                for (USHORT k = 0; k < n; k++) {
                    if (be16(c + 4 + 8 * k) == 3 && be16(c + 6 + 8 * k) == 0)
                        face->Symbol = TRUE;
                    if (be16(c + 4 + 8 * k) == 3 && be16(c + 6 + 8 * k) == 1)
                        cmap_range(src, &cmap, be32(c + 8 + 8 * k), face);
                }
        }
        if (face->Symbol)
            face->Signature.fsCsb[0] |= 0x80000000u;
        file->FaceCount++;
        g_face_count++;
    }
    ExFreePool(names);
}

/* counts the fonts added, after which the characters of the linked fonts are looked up again: a font may come first. */
static ULONG g_font_generation;

static void raster_chars(FONT_FACE *f);

/* a face for each size of a .fon file, which is small enough to read whole. */
static void register_raster(FONT_FILE *file, const FONT_SOURCE *src)
{
    SIZE_T size = src->Data ? src->Size : (SIZE_T)file_size(src->File);
    if (!size || size > 1024 * 1024)
        return;
    UCHAR *data = ExAllocatePool2(POOL_FLAG_NON_PAGED, size, FONT_TAG);
    if (!data)
        return;
    ULONG offsets[32], sizes[32];
    ULONG count = source_read(src, 0, data, (ULONG)size) ? FntList(data, size, offsets, sizes, 32) : 0;
    for (ULONG i = 0; i < count && g_face_count < MAX_FACES; i++) {
        FONT_FACE *face = &g_faces[g_face_count];
        memset(face, 0, sizeof(*face));
        if (!FntRead(data + offsets[i], sizes[i], &face->Fnt))
            continue;
        const FNT_INFO *fnt = &face->Fnt;
        for (ULONG c = 0; fnt->Face[c]; c++)
            face->Family[c] = (UCHAR)fnt->Face[c];
        wcscpy(face->TypoFamily, face->Family);
        wcscpy(face->FullName, face->Family);
        wcscpy(face->Style, fnt->Weight >= FW_SEMIBOLD ? (fnt->Italic ? L"Bold Italic" : L"Bold")
                            : fnt->Italic              ? L"Italic"
                                                       : L"Regular");
        face->File = file;
        face->FaceIndex = i;
        face->DirOffset = offsets[i];
        face->Raster = TRUE;
        face->Weight = fnt->Weight ? fnt->Weight : FW_NORMAL;
        face->Italic = fnt->Italic != 0;
        /* the low bit of the pitch is set for a font of variable pitch, as in TEXTMETRIC. */
        face->Fixed = !(fnt->PitchAndFamily & 1);
        face->PitchFamily = fnt->PitchAndFamily & 0xF0;
        face->Symbol = fnt->CharSet == SYMBOL_CHARSET;
        face->UnitsPerEm = fnt->PixHeight;
        face->WinAscent = fnt->Ascent;
        face->WinDescent = fnt->PixHeight - fnt->Ascent;
        face->AvgWidth = fnt->AvgWidth;
        face->MaxAdvance = fnt->MaxWidth;
        face->LineGap = fnt->ExternalLeading;
        raster_chars(face);
        face->CharsKnown = TRUE;
        /* a raster face has no font signature, GetTextCharsetInfo gives it zeros. */
        file->FaceCount++;
        g_face_count++;
    }
    ExFreePool(data);
}

static void register_source(FONT_FILE *file, const FONT_SOURCE *src)
{
    UCHAR header[12];
    g_font_generation++;
    file->FirstFace = g_face_count;
    if (!source_read(src, 0, header, sizeof(header)))
        return;
    if (header[0] == 'M' && header[1] == 'Z') {
        register_raster(file, src);
    } else if (be32(header) == 0x74746366) { /* ttcf */
        ULONG count = be32(header + 8);
        if (count > 32)
            count = 32;
        UCHAR offsets[32 * 4];
        if (source_read(src, 12, offsets, count * 4))
            for (ULONG i = 0; i < count; i++)
                register_face(file, src, be32(offsets + i * 4), i);
    } else {
        register_face(file, src, 0, 0);
    }
}

/* boot does not wait for the fonts directory scan, so this waits for it. */
static void wait_scanned(void)
{
    if (g_scanning)
        KeWaitForSingleObject(&g_scanned, Executive, KernelMode, FALSE, nullptr);
}

/* returns the number of faces added. */
static ULONG register_file(PCWSTR path, HANDLE owner, BOOLEAN hidden)
{
    if (!g_faces || wcslen(path) >= MAX_PATH)
        return 0;
    HANDLE h = open_font_file(path);
    if (!h)
        return 0;
    FONT_FILE *file = W32Alloc(sizeof(FONT_FILE));
    if (!file) {
        ZwClose(h);
        return 0;
    }
    wcscpy(file->Path, path);
    file->OwnerPid = owner;
    file->Hidden = hidden;
    FONT_SOURCE src = {h, nullptr, 0};
    register_source(file, &src);
    ZwClose(h);
    ULONG added = file->FaceCount;
    if (!added)
        W32Free(file);
    return added;
}

ULONG FontRegisterFile(PCWSTR path, HANDLE owner, BOOLEAN hidden)
{
    wait_scanned();
    return register_file(path, owner, hidden);
}

/* the data is copied into the kernel. */
ULONG FontRegisterMemory(PVOID data, SIZE_T size, HANDLE owner, FONT_FILE **out)
{
    *out = nullptr;
    wait_scanned();
    if (!g_faces || !size)
        return 0;
    FONT_FILE *file = W32Alloc(sizeof(FONT_FILE));
    if (!file)
        return 0;
    file->Data = ExAllocatePool2(POOL_FLAG_NON_PAGED, size, FONT_TAG);
    if (!file->Data) {
        W32Free(file);
        return 0;
    }
    memcpy(file->Data, data, size);
    file->Size = size;
    file->Memory = TRUE;
    file->Hidden = TRUE;
    file->OwnerPid = owner;
    FONT_SOURCE src = {nullptr, file->Data, size};
    register_source(file, &src);
    if (!file->FaceCount) {
        ExFreePool(file->Data);
        W32Free(file);
        return 0;
    }
    *out = file;
    return file->FaceCount;
}

ULONG FontFaceCount(void)
{
    wait_scanned();
    return g_face_count;
}

FONT_FACE *FontFaceAt(ULONG index)
{
    wait_scanned();
    return index < g_face_count ? &g_faces[index] : nullptr;
}

BOOLEAN FontFaceVisible(FONT_FACE *f)
{
    if (f->File->Removed)
        return FALSE;
    return !f->File->OwnerPid || f->File->OwnerPid == PsGetCurrentProcessId();
}

/* private fonts die with their process; realized instances keep the data alive. */
void FontReleaseProcess(HANDLE pid)
{
    /* directory faces have no owner, and no process can have added its own before they are read. */
    if (g_scanning && !KeReadStateEvent(&g_scanned))
        return;
    for (ULONG i = 0; i < g_face_count; i++) {
        FONT_FILE *f = g_faces[i].File;
        if (f->OwnerPid == pid)
            f->Removed = TRUE;
    }
}

static BOOLEAN has_suffix(PCWSTR name, ULONG chars, PCWSTR suffix)
{
    ULONG n = (ULONG)wcslen(suffix);
    return chars >= n && !_wcsnicmp(name + chars - n, suffix, n);
}

/*
 * the stock files load first, as win32kfull loads them when the session starts: those of LargeFont above 108 dpi,
 * then those of SmallFont, each in the order FONTS.FON, OEMFONT.FON and FIXEDFON.FON. the first loaded wins a tie of
 * the mapper and comes first in an enumeration.
 */
static const PCWSTR g_stock_files[] = {L"8514sys.fon", L"8514oem.fon", L"8514fix.fon",
                                       L"vgasys.fon",  L"vgaoem.fon",  L"vgafix.fon"};

static BOOLEAN stock_file(PCWSTR name, ULONG chars)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_stock_files); i++)
        if (chars == wcslen(g_stock_files[i]) && !_wcsicmp(name, g_stock_files[i]))
            return TRUE;
    return FALSE;
}

static void scan_stock_fonts(void)
{
    for (ULONG i = GdiLargeStockFonts() ? 0 : 3; i < RTL_NUMBER_OF(g_stock_files); i++) {
        WCHAR path[MAX_PATH];
        wcscpy(path, L"\\SystemRoot\\Fonts\\");
        wcscat(path, g_stock_files[i]);
        register_file(path, nullptr, FALSE);
    }
}

static void scan_fonts(void)
{
    scan_stock_fonts();
    UNICODE_STRING dir_name = RTL_CONSTANT_STRING(L"\\SystemRoot\\Fonts");
    OBJECT_ATTRIBUTES oa;
    InitializeObjectAttributes(&oa, &dir_name, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);
    IO_STATUS_BLOCK iosb;
    HANDLE dir;
    NTSTATUS status = ZwCreateFile(&dir, FILE_LIST_DIRECTORY | SYNCHRONIZE, &oa, &iosb, nullptr, 0,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, FILE_OPEN,
                                   FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT, nullptr, 0);
    if (!NT_SUCCESS(status)) {
        W32Trace("win32k: cannot open the fonts directory: %08x\n", status);
        return;
    }
    UCHAR *buffer = W32Alloc(16384);
    BOOLEAN restart = TRUE;
    while (buffer) {
        status = ZwQueryDirectoryFile(dir, nullptr, nullptr, nullptr, &iosb, buffer, 16384, FileDirectoryInformation, FALSE,
                                      nullptr, restart);
        restart = FALSE;
        if (!NT_SUCCESS(status))
            break;
        FILE_DIRECTORY_INFORMATION *info = (FILE_DIRECTORY_INFORMATION *)buffer;
        for (;;) {
            ULONG chars = info->FileNameLength / sizeof(WCHAR);
            if (has_suffix(info->FileName, chars, L".ttf") || has_suffix(info->FileName, chars, L".ttc") ||
                has_suffix(info->FileName, chars, L".otf") ||
                (has_suffix(info->FileName, chars, L".fon") && !stock_file(info->FileName, chars))) {
                WCHAR path[MAX_PATH];
                wcscpy(path, L"\\SystemRoot\\Fonts\\");
                ULONG base = (ULONG)wcslen(path);
                if (base + chars < MAX_PATH) {
                    memcpy(path + base, info->FileName, chars * sizeof(WCHAR));
                    path[base + chars] = 0;
                    register_file(path, nullptr, FALSE);
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

static void NTAPI scan_thread(PVOID context)
{
    scan_fonts();
    KeSetEvent(&g_scanned, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

/*
 * scanning the fonts directory takes 20 milliseconds, a fifth of boot, and nothing needs the faces before a window
 * is drawn, so a thread does it while boot goes on.
 */
void FontInitialize(void)
{
    g_faces = W32Alloc(MAX_FACES * sizeof(FONT_FACE));
    if (!g_faces)
        return;
    KeInitializeEvent(&g_scanned, NotificationEvent, FALSE);
    HANDLE thread;
    g_scanning = TRUE;
    if (NT_SUCCESS(PsCreateSystemThread(&thread, 0, nullptr, nullptr, nullptr, scan_thread, nullptr))) {
        ZwClose(thread);
        return;
    }
    g_scanning = FALSE;
    scan_fonts();
}

/* ------------------------------------------------------------------ table access */

BOOLEAN FontReadTable(FONT_FACE *f, ULONG tag, ULONG offset, PVOID buffer, ULONG size, ULONG *table_size)
{
    /* GetFontData fails for a raster font. */
    if (f->Raster)
        return FALSE;
    FONT_FILE *file = f->File;
    HANDLE h = nullptr;
    FONT_SOURCE src = {nullptr, file->Data, file->Size};
    if (!file->Data) {
        h = open_font_file(file->Path);
        if (!h)
            return FALSE;
        src.File = h;
        src.Size = (SIZE_T)file_size(h);
    }
    BOOLEAN ok = FALSE;
    ULONG start = 0, length = 0;
    if (tag == 0) {
        /* the whole face: for a ttc collection, starting at the face's own table directory. */
        start = f->DirOffset;
        length = (ULONG)(src.Size - f->DirOffset);
        ok = src.Size > f->DirOffset;
    } else if (tag == 0x74746366) {
        UCHAR head[4];
        if (source_read(&src, 0, head, 4) && be32(head) == 0x74746366) {
            start = 0;
            length = (ULONG)src.Size;
            ok = TRUE;
        }
    } else {
        UCHAR header[12];
        UCHAR dir[64 * 16];
        if (source_read(&src, f->DirOffset, header, sizeof(header))) {
            USHORT tables = be16(header + 4);
            if (tables <= 64 && source_read(&src, f->DirOffset + 12, dir, tables * 16u))
                for (USHORT i = 0; i < tables; i++)
                    if (be32(dir + i * 16) == tag) {
                        start = be32(dir + i * 16 + 8);
                        length = be32(dir + i * 16 + 12);
                        ok = (ULONG64)start + length <= src.Size;
                        break;
                    }
        }
    }
    if (ok && table_size)
        *table_size = length;
    if (ok && buffer && size) {
        if (offset >= length || size > length - offset)
            ok = FALSE;
        else
            ok = source_read(&src, (ULONG64)start + offset, buffer, size);
    }
    if (h)
        ZwClose(h);
    return ok;
}

/* ------------------------------------------------------------------ matching */

/* the FontSubstitutes registry key of windows. */
static const MIME_FONT_SUBSTITUTE g_substitutes[] = {MIME_FONT_SUBSTITUTES};

/*
 * the raster faces of windows come with a feature, and the stock fonts name them: until it is installed they are
 * drawn with an outline face. a program that names one asks for the feature itself, see FontAskFor.
 */
static const MIME_FONT_SUBSTITUTE g_raster_standins[] = {
    {L"MS Sans Serif", L"Microsoft Sans Serif"}, {L"MS Serif", L"Times New Roman"}, {L"Courier", L"Courier New"},
    {L"System", L"Segoe UI"},  {L"Fixedsys", L"Consolas"}, {L"Terminal", L"Consolas"}, {L"Small Fonts", L"Segoe UI"},
};

static const MIME_FONT_CHARSET_SUBSTITUTE g_charset_substitutes[] = {MIME_FONT_CHARSET_SUBSTITUTES};

/* the face a name stands for in a charset, the name itself when it stands for none. */
static PCWSTR substitute_in(PCWSTR name, UCHAR charset)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_substitutes); i++)
        if (!_wcsicmp(name, g_substitutes[i].From))
            return g_substitutes[i].To;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_charset_substitutes); i++)
        if (!_wcsicmp(name, g_charset_substitutes[i].From) &&
            (charset == DEFAULT_CHARSET || charset == g_charset_substitutes[i].FromCharset))
            return g_charset_substitutes[i].To;
    return name;
}

static PCWSTR substitute(PCWSTR name)
{
    return substitute_in(name, DEFAULT_CHARSET);
}

/* the names of a face in one charset, for EnumFontFamilies: the index-th, null past the last. */
BOOLEAN FontCharsetAlias(PCWSTR family, ULONG index, PCWSTR *name, UCHAR *charset)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_charset_substitutes); i++) {
        if (_wcsicmp(family, g_charset_substitutes[i].To))
            continue;
        if (index--)
            continue;
        *name = g_charset_substitutes[i].From;
        *charset = g_charset_substitutes[i].FromCharset;
        return TRUE;
    }
    return FALSE;
}

static PCWSTR raster_standin(PCWSTR name)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_raster_standins); i++)
        if (!_wcsicmp(name, g_raster_standins[i].From))
            return g_raster_standins[i].To;
    return nullptr;
}

/*
 * how near a size of a raster family is to a logical font, as MAPPER::bNearMatch of win32kfull weighs it, lower is
 * nearer. a face is stretched by a whole factor, up to 8 times in height and 5 in width.
 */
static LONG abs_of(LONG v)
{
    return v < 0 ? -v : v;
}

typedef struct RASTER_FIT {
    LONG Penalty;
    LONG Kx, Ky;
    BOOLEAN SimBold;
} RASTER_FIT;

/* the cell for a positive height, the character for a negative one, and 16 for none. */
static LONG raster_wish(const LOGFONTW *lf)
{
    return lf->lfHeight < 0 ? -lf->lfHeight : lf->lfHeight ? lf->lfHeight : 16;
}

static LONG raster_height(const FONT_FACE *f, const LOGFONTW *lf)
{
    return lf->lfHeight < 0 ? f->Fnt.PixHeight - f->Fnt.InternalLeading : f->Fnt.PixHeight;
}

/* the System of vgasys.fon, the face the mapper falls back to, which its height never rules out. */
static BOOLEAN raster_emergency(const FONT_FACE *f)
{
    ULONG n = (ULONG)wcslen(f->File->Path);
    return n >= 10 && !_wcsicmp(f->File->Path + n - 10, L"vgasys.fon");
}

static BOOLEAN raster_fit(const FONT_FACE *f, const LOGFONTW *lf, RASTER_FIT *fit)
{
    const FNT_INFO *fnt = &f->Fnt;
    LONG penalty = 0;
    BOOLEAN variable = (fnt->PitchAndFamily & 1) != 0;
    UCHAR pitch = lf->lfPitchAndFamily & 3;
    if (pitch == FIXED_PITCH && variable)
        penalty += 15000;
    else if (pitch == VARIABLE_PITCH && !variable)
        penalty += 350;
    else if (pitch == DEFAULT_PITCH && !variable)
        penalty += 1;
    /* a family not asked for is taken for swiss, against the family of the face. */
    UCHAR face_family = fnt->PitchAndFamily & 0x70, family = lf->lfPitchAndFamily & 0x70;
    if (!family)
        family = lf->lfCharSet == SYMBOL_CHARSET ? face_family : face_family ? FF_SWISS : 0;
    if (family != face_family)
        penalty += !face_family ? 8000 : 9000 + ((family > FF_MODERN) != (face_family > FF_MODERN) ? 50 : 0);
    /* a charset asked for rules out a face of another, the default one costs a little against ansi. */
    if (lf->lfCharSet != DEFAULT_CHARSET && lf->lfCharSet != fnt->CharSet)
        return FALSE;
    if (lf->lfCharSet == DEFAULT_CHARSET && fnt->CharSet != ANSI_CHARSET)
        penalty += 2;
    LONG wish = raster_wish(lf), height = raster_height(f, lf), ky = 1, cost = 0;
    if (height > 0 && height < wish && wish > 7 * height / 4) {
        ky = W32Min((wish + height / 4) / height, 8);
        if (ky > 1) {
            if (ky + 2 >= height)
                return FALSE;
            height *= ky;
            cost = (20 * ky) | (5 * (ky - 1));
        }
    }
    cost += height > wish ? 150 * (height - wish + 4) : 150 * (wish - height);
    penalty += cost;
    if (cost >= 10000 && !raster_emergency(f))
        return FALSE;
    if (lf->lfItalic)
        penalty += f->Italic ? 0 : 1;
    else if (f->Italic)
        penalty += 4;
    LONG weight = lf->lfWeight ? lf->lfWeight : FW_NORMAL, d = f->Weight - weight;
    fit->SimBold = FALSE;
    if (d < 0) {
        d = -d;
        if (d > 150) {
            fit->SimBold = TRUE;
            d -= 120;
        }
    }
    penalty += (73 * d) >> 8;
    /* the width asked for, or else the aspect of the face against the square pixels of the display. */
    LONG kx = 1, avg = fnt->AvgWidth;
    if (lf->lfWidth) {
        LONG want = lf->lfWidth < 0 ? -lf->lfWidth : lf->lfWidth, extra = 0;
        if (avg && want > avg)
            kx = W32Min((want - (avg + 1) / 2) / avg + 1, 5);
        if (kx > 1)
            extra = (kx - 1) | (20 * kx);
        penalty += 50 * abs_of(want - avg * kx) + extra;
    } else if (fnt->VertRes) {
        LONG aspect = 100 * fnt->HorizRes / fnt->VertRes, extra = 0;
        BOOLEAN square = fnt->HorizRes == fnt->VertRes;
        if (square && ky > 1) {
            kx = W32Min(ky, 5);
            extra = 20 * kx;
        } else if (!square && 100 > (3 * (aspect / ky)) >> 1) {
            if (!(aspect / ky))
                return FALSE;
            kx = W32Min(100 / (aspect / ky), 5);
            extra = 20 * kx;
        }
        if (!square || kx != ky)
            extra += 30 * abs_of(100 - kx * aspect / ky);
        penalty += extra;
    }
    if (kx > 1 || ky > 1) {
        penalty += 50;
        if (kx > ky)
            penalty += 4 * (ky == 1 ? 100 * kx : (100 * kx + ky / 2) / ky);
        else if (kx < ky)
            penalty += 4 * (kx == 1 ? 100 * ky : (100 * ky + kx / 2) / kx);
    }
    fit->Penalty = penalty;
    fit->Kx = kx;
    fit->Ky = ky;
    return TRUE;
}

/* the stock files load as the session starts, before the others, and the first loaded wins a tie. */
static ULONG raster_serial(const FONT_FACE *f)
{
    static const PCWSTR stock[] = {L"vgasys.fon", L"vgafix.fon", L"vgaoem.fon",
                                   L"8514sys.fon", L"8514fix.fon", L"8514oem.fon"};
    ULONG n = (ULONG)wcslen(f->File->Path), index = (ULONG)(f - g_faces);
    for (ULONG i = 0; i < RTL_NUMBER_OF(stock); i++) {
        ULONG m = (ULONG)wcslen(stock[i]);
        if (n >= m && !_wcsicmp(f->File->Path + n - m, stock[i]))
            return index;
    }
    return index + MAX_FACES;
}

/* the nearest size of a raster family, null when the mapper rules out every one. */
static FONT_FACE *raster_best(PCWSTR family, const LOGFONTW *lf)
{
    FONT_FACE *best = nullptr;
    LONG best_penalty = 0;
    for (ULONG i = 0; i < g_face_count; i++) {
        FONT_FACE *f = &g_faces[i];
        RASTER_FIT fit;
        if (!f->Raster || !FontFaceVisible(f) || _wcsicmp(f->Family, family) || !raster_fit(f, lf, &fit))
            continue;
        if (!best || fit.Penalty < best_penalty ||
            (fit.Penalty == best_penalty && raster_serial(f) < raster_serial(best))) {
            best_penalty = fit.Penalty;
            best = f;
        }
    }
    return best;
}

/*
 * the FontMapper key of windows: when the face asked for is not there, bGetFaceName takes the bitmap face of the
 * kind of font asked for when it has the very size, else the TrueType one. the value is the ansi charset, 0x8000 for
 * a fixed pitch, 0x4000 for roman, 0x1000 for the first bitmap face and 0x800 for the second.
 */
static const struct {
    USHORT Key;
    PCWSTR Face;
} g_font_mapper[] = {
    {0x0000, L"Arial"},         {0x8800, L"Courier"},     {0x8000, L"Courier New"}, {0x9000, L"Fixedsys"},
    {0x8080, L"MS Gothic"},     {0x0080, L"MS PGothic"},  {0x1000, L"MS Sans Serif"}, {0x5000, L"MS Serif"},
    {0x8086, L"NSimSun"},       {0x0086, L"SimSun"},      {0x0800, L"Small Fonts"}, {0x4002, L"Symbol"},
    {0x4000, L"Times New Roman"}, {0x0002, L"Wingdings"},
};

static PCWSTR mapper_face(USHORT key)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_font_mapper); i++)
        if (g_font_mapper[i].Key == key)
            return g_font_mapper[i].Face;
    return nullptr;
}

/* MAPPER::bFindBitmapFont: a size of the family of just the height asked for, and of the width when one is asked. */
static BOOLEAN bitmap_exact(PCWSTR family, const LOGFONTW *lf)
{
    LONG wish = raster_wish(lf);
    for (ULONG i = 0; i < g_face_count; i++) {
        FONT_FACE *f = &g_faces[i];
        if (!f->Raster || !FontFaceVisible(f) || _wcsicmp(f->Family, family))
            continue;
        LONG height = raster_height(f, lf);
        if ((height == wish || (wish == 2 && height == 3)) && (!lf->lfWidth || lf->lfWidth == f->Fnt.AvgWidth))
            return TRUE;
    }
    return FALSE;
}

static FONT_FACE *match_family(PCWSTR name, LONG weight, BOOLEAN italic);

static FONT_FACE *mapper_default(const LOGFONTW *lf)
{
    UCHAR charset = lf->lfCharSet == DEFAULT_CHARSET ? ANSI_CHARSET : lf->lfCharSet;
    UCHAR pitch = lf->lfPitchAndFamily & 3, family = lf->lfPitchAndFamily & 0x70;
    USHORT key = charset;
    if (pitch == FIXED_PITCH || (pitch == DEFAULT_PITCH && family == FF_MODERN))
        key |= 0x8000;
    if (family == FF_ROMAN)
        key |= 0x4000;
    if (charset == ANSI_CHARSET && lf->lfEscapement % 900 == 0) {
        static const USHORT bitmaps[] = {0x1000, 0x800};
        for (ULONG i = 0; i < RTL_NUMBER_OF(bitmaps); i++) {
            PCWSTR face = mapper_face(key | bitmaps[i]);
            FONT_FACE *f = face && bitmap_exact(face, lf) ? raster_best(face, lf) : nullptr;
            if (f)
                return f;
        }
    }
    PCWSTR face = mapper_face(key);
    FONT_FACE *f = face ? match_family(face, lf->lfWeight ? lf->lfWeight : FW_NORMAL, lf->lfItalic != 0) : nullptr;
    return f && f->Raster ? raster_best(face, lf) : f;
}

/* the best face of a family for the weight and slant asked for, null when the family has none. without a name, the
 * best of the faces the process added for itself. */
static FONT_FACE *match_family(PCWSTR name, LONG weight, BOOLEAN italic)
{
    FONT_FACE *best = nullptr;
    LONG best_score = 0x7FFFFFFF;
    for (ULONG i = 0; i < g_face_count; i++) {
        FONT_FACE *f = &g_faces[i];
        if (!FontFaceVisible(f))
            continue;
        if (!name) {
            if (!f->File->OwnerPid || f->File->Hidden || f->Symbol)
                continue;
        } else if (_wcsicmp(f->Family, name) && _wcsicmp(f->TypoFamily, name) && _wcsicmp(f->FullName, name)) {
            continue;
        }
        LONG score = (f->Weight > weight ? f->Weight - weight : weight - f->Weight);
        /* prefer lighter faces for weights below normal, like gdi. */
        if (weight <= FW_NORMAL && f->Weight > weight)
            score += 50;
        if (!!f->Italic != italic)
            score += 1000;
        if (name && !_wcsicmp(f->Family, name))
            score -= 1; /* an exact family beats a typographic family. */
        if (score < best_score) {
            best_score = score;
            best = f;
        }
    }
    return best;
}

/* a family by the name a program gives it, after the substitutes. the caller holds the lock. */
BOOLEAN FontFamilyInstalled(PCWSTR name)
{
    wait_scanned();
    return match_family(substitute(name), FW_NORMAL, FALSE) != nullptr;
}

/*
 * a raster family takes its nearest size. a raster face that is not installed is stood in for by an outline one, and
 * a face that is not there at all by the FontMapper face of the kind asked for, then by the generic family the
 * request names, a face the process added for itself and whatever is installed.
 */
FONT_FACE *FontMatchFace(const LOGFONTW *lf)
{
    wait_scanned();
    LONG weight = lf->lfWeight ? lf->lfWeight : FW_NORMAL;
    BOOLEAN italic = lf->lfItalic != 0;
    PCWSTR name = lf->lfFaceName[0] ? substitute_in(lf->lfFaceName, lf->lfCharSet) : nullptr;
    FONT_FACE *best = name ? match_family(name, weight, italic) : nullptr;
    if (best && best->Raster)
        best = raster_best(best->Family, lf);
    else if (!best && name && raster_standin(name))
        best = match_family(raster_standin(name), weight, italic);
    if (!best)
        best = mapper_default(lf);
    if (!best) {
        PCWSTR generic = (lf->lfPitchAndFamily & 3) == FIXED_PITCH || (lf->lfPitchAndFamily & 0xF0) == FF_MODERN
                             ? L"Consolas"
                             : (lf->lfPitchAndFamily & 0xF0) == FF_ROMAN ? L"Times New Roman" : L"Segoe UI";
        best = match_family(generic, weight, italic);
    }
    if (!best)
        best = match_family(nullptr, weight, italic);
    for (ULONG i = 0; !best && i < g_face_count; i++)
        if (FontFaceVisible(&g_faces[i]))
            best = &g_faces[i];
    return best;
}

BOOLEAN FontLoadFace(FONT_FACE *f)
{
    if (f->Loaded)
        return TRUE;
    if (!load_file(f->File))
        return FALSE;
    if (f->Raster) {
        f->Loaded = f->File->Size >= (SIZE_T)f->DirOffset + f->Fnt.Size;
        return f->Loaded;
    }
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

/*
 * the characters of the text metrics, as windows 11 gives them for a truetype face: the first and the last character of
 * its cmap, the break character the first one but at least 2 and the default character the one before. a symbol face
 * covers 0 to 0xf0ff and its ansi characters start at 0x20. measured on windows.
 */
/*
 * the characters of a raster face, as windows 11 gives them: its bytes read in code page 1252 whatever its charset,
 * the last character the highest of them, the default and the break characters counted from the first.
 */
static void raster_chars(FONT_FACE *f)
{
    const FNT_INFO *fnt = &f->Fnt;
    WCHAR last = 0;
    for (ULONG c = fnt->FirstChar; c <= fnt->LastChar; c++)
        last = (WCHAR)W32Max(last, FntToUnicode(1252, (UCHAR)c));
    f->FirstChar = FntToUnicode(1252, fnt->FirstChar);
    f->LastChar = last;
    f->DefaultChar = FntToUnicode(1252, (UCHAR)(fnt->FirstChar + fnt->DefaultChar));
    f->BreakChar = FntToUnicode(1252, (UCHAR)(fnt->FirstChar + fnt->BreakChar));
    f->AnsiChars[0] = fnt->FirstChar;
    f->AnsiChars[1] = fnt->LastChar;
    f->AnsiChars[2] = (UCHAR)(fnt->FirstChar + fnt->DefaultChar);
    f->AnsiChars[3] = (UCHAR)(fnt->FirstChar + fnt->BreakChar);
}

static void face_chars(FONT_FACE *f)
{
    if (f->CharsKnown)
        return;
    f->CharsKnown = TRUE;
    if (f->Raster) {
        raster_chars(f);
        return;
    }
    if (f->Symbol) {
        ULONG first = 0xF000, last = 0xF0FF;
        while (first < last && !mime_ttf_glyph_index(&f->Ttf, first))
            first++;
        while (last > first && !mime_ttf_glyph_index(&f->Ttf, last))
            last--;
        f->FirstChar = 0;
        f->LastChar = 0xF0FF;
        f->DefaultChar = 0x1F;
        f->BreakChar = 0x20;
        f->AnsiChars[0] = 0x1E;
        f->AnsiChars[1] = (UCHAR)W32Min(0xFF, (LONG)(last - first) + 0x20);
        f->AnsiChars[2] = 0x1F;
        f->AnsiChars[3] = 0x20;
        return;
    }
    ULONG first = 0, last = 0xFFFF;
    while (first < last && !mime_ttf_glyph_index(&f->Ttf, first))
        first++;
    while (last > first && !mime_ttf_glyph_index(&f->Ttf, last))
        last--;
    ULONG brk = first < 2 ? 2 : first;
    f->FirstChar = (WCHAR)first;
    f->LastChar = (WCHAR)last;
    f->BreakChar = (WCHAR)brk;
    f->DefaultChar = (WCHAR)(brk - 1);
    f->AnsiChars[0] = (UCHAR)(brk - 2);
    f->AnsiChars[1] = (UCHAR)W32Min(0xFF, (LONG)last);
    f->AnsiChars[2] = (UCHAR)(brk - 1);
    f->AnsiChars[3] = (UCHAR)brk;
}

const UCHAR *FontAnsiChars(FONT_INSTANCE *fi)
{
    face_chars(fi->Face);
    return fi->Face->AnsiChars;
}

ULONG FontFaceId(FONT_FACE *f)
{
    return (ULONG)(f - g_faces) + 1;
}

/* the charsets and their bit in the code page ranges of the OS/2 table. */
static const struct {
    UCHAR CharSet;
    UCHAR Bit;
    USHORT CodePage;
} g_charsets[] = {
    {ANSI_CHARSET, 0, 1252},         {EASTEUROPE_CHARSET, 1, 1250}, {RUSSIAN_CHARSET, 2, 1251},
    {GREEK_CHARSET, 3, 1253},        {TURKISH_CHARSET, 4, 1254},    {HEBREW_CHARSET, 5, 1255},
    {ARABIC_CHARSET, 6, 1256},       {BALTIC_CHARSET, 7, 1257},     {VIETNAMESE_CHARSET, 8, 1258},
    {THAI_CHARSET, 16, 874},         {SHIFTJIS_CHARSET, 17, 932},   {GB2312_CHARSET, 18, 936},
    {HANGEUL_CHARSET, 19, 949},      {CHINESEBIG5_CHARSET, 20, 950}, {JOHAB_CHARSET, 21, 1361},
    {SYMBOL_CHARSET, 31, 42},
};

UINT FontCharSetCodePage(UCHAR charset)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_charsets); i++)
        if (g_charsets[i].CharSet == charset)
            return g_charsets[i].CodePage;
    if (charset == OEM_CHARSET)
        return 437;
    if (charset == MAC_CHARSET)
        return 10000;
    return 1252;
}

/* the charset asked for when the face has it, else ansi, else the first the face has. */
static UCHAR resolve_charset(FONT_FACE *face, UCHAR asked)
{
    if (face->Raster)
        return face->Fnt.CharSet;
    if (face->Symbol)
        return SYMBOL_CHARSET;
    ULONG ranges = face->Signature.fsCsb[0];
    for (ULONG i = 0; asked != DEFAULT_CHARSET && i < RTL_NUMBER_OF(g_charsets); i++)
        if (g_charsets[i].CharSet == asked && (ranges & (1u << g_charsets[i].Bit)))
            return asked;
    if (ranges & 1)
        return ANSI_CHARSET;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_charsets); i++)
        if (ranges & (1u << g_charsets[i].Bit))
            return g_charsets[i].CharSet;
    return ANSI_CHARSET;
}

/* the faces with a value under FontLink\SystemLink on windows 11, read with NtGdiFontIsLinked in textj.c. */
static const WCHAR *const g_system_linked[] = {
    L"Segoe UI", L"Segoe UI Semibold", L"Segoe UI Light", L"Segoe UI Semilight", L"Tahoma", L"Microsoft Sans Serif",
    L"Lucida Sans Unicode", L"MS Gothic", L"MS PGothic", L"MS UI Gothic", L"Yu Gothic UI", L"Malgun Gothic",
    L"Microsoft YaHei UI", L"Microsoft JhengHei UI", L"SimSun", L"Ebrima", L"Nirmala UI", L"Leelawadee UI", L"Gadugi",
};

BOOLEAN FontHasSystemLink(FONT_FACE *f)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_system_linked); i++)
        if (!_wcsicmp(f->Family, g_system_linked[i]))
            return TRUE;
    return FALSE;
}

static void compute_metrics(FONT_INSTANCE *fi, const LOGFONTW *lf)
{
    FONT_FACE *face = fi->Face;
    const MimeTtfMetrics *m = &face->Metrics;
    TEXTMETRICW *tm = &fi->Metrics;
    memset(tm, 0, sizeof(*tm));
    LONG ascent = round_f((float)m->win_ascent * fi->ScaleY);
    LONG descent = round_f((float)m->win_descent * fi->ScaleY);
    /* fonts with a VDMX table say the ascent and descent they need at the size. */
    int y_max, y_min;
    if (fi->ScaleX == fi->ScaleY && mime_ttf_vdmx(&face->Ttf, 1, 1, fi->EmPixels, &y_max, &y_min) && y_max >= 0 &&
        y_min <= 0) {
        ascent = y_max;
        descent = -y_min;
    }
    tm->tmAscent = ascent;
    tm->tmDescent = descent;
    tm->tmHeight = ascent + descent;
    tm->tmInternalLeading = tm->tmHeight - fi->EmPixels;
    if (tm->tmInternalLeading < 0)
        tm->tmInternalLeading = 0;
    LONG gap = m->line_gap + m->ascender - m->descender - (m->win_ascent + m->win_descent);
    tm->tmExternalLeading = gap > 0 ? round_f((float)gap * fi->ScaleY) : 0;
    tm->tmAveCharWidth = round_f((float)m->avg_char_width * fi->ScaleX);
    /* the width of the bounding box of the face, not the widest advance. */
    tm->tmMaxCharWidth = round_f((float)(m->x_max - m->x_min) * fi->ScaleX);
    tm->tmWeight = fi->Weight;
    tm->tmOverhang = 0;
    tm->tmDigitizedAspectX = 96;
    tm->tmDigitizedAspectY = 96;
    face_chars(face);
    tm->tmFirstChar = face->FirstChar;
    tm->tmLastChar = face->LastChar;
    tm->tmDefaultChar = face->DefaultChar;
    tm->tmBreakChar = face->BreakChar;
    tm->tmItalic = fi->Italic ? 255 : 0;
    tm->tmUnderlined = fi->Underline ? 255 : 0;
    tm->tmStruckOut = fi->StrikeOut ? 255 : 0;
    /* TMPF_FIXED_PITCH is inverted: it is set for variable pitch fonts. */
    tm->tmPitchAndFamily = (face->Fixed ? 0 : TMPF_FIXED_PITCH) | TMPF_VECTOR | TMPF_TRUETYPE |
                           face->PitchFamily;
    tm->tmCharSet = fi->CharSet;
    fi->UnderlinePos = -round_f((float)m->underline_position * fi->ScaleY);
    fi->UnderlineSize = W32Max(1, round_f((float)m->underline_thickness * fi->ScaleY));
    fi->StrikeoutPos = round_f((float)m->strikeout_position * fi->ScaleY);
}

/*
 * the metrics of a raster face stretched by whole factors. a simulated bold is a pixel wider, a simulated italic
 * leans by half the height, which is its overhang.
 */
static void raster_metrics(FONT_INSTANCE *fi)
{
    const FNT_INFO *fnt = &fi->Face->Fnt;
    LONG kx = (LONG)fi->ScaleX, ky = (LONG)fi->ScaleY, bold = fi->SimBold ? 1 : 0;
    TEXTMETRICW *tm = &fi->Metrics;
    memset(tm, 0, sizeof(*tm));
    tm->tmHeight = fnt->PixHeight * ky;
    tm->tmAscent = fnt->Ascent * ky;
    tm->tmDescent = tm->tmHeight - tm->tmAscent;
    tm->tmInternalLeading = fnt->InternalLeading * ky;
    tm->tmExternalLeading = fnt->ExternalLeading * ky;
    /* a bold face leaning is a pixel wider on average as well, as System is on windows 11. */
    LONG lean_bold = fi->SimItalic && fi->Face->Weight >= FW_SEMIBOLD ? 1 : 0;
    tm->tmAveCharWidth = fnt->AvgWidth * kx + bold + lean_bold;
    tm->tmMaxCharWidth = fnt->MaxWidth * kx + bold + lean_bold;
    tm->tmWeight = fi->SimBold ? FW_BOLD : fi->Face->Weight;
    tm->tmOverhang = bold + (fi->SimItalic ? (tm->tmHeight - 1) / 2 : 0);
    tm->tmDigitizedAspectX = fnt->HorizRes;
    tm->tmDigitizedAspectY = fnt->VertRes;
    face_chars(fi->Face);
    tm->tmFirstChar = fi->Face->FirstChar;
    tm->tmLastChar = fi->Face->LastChar;
    tm->tmDefaultChar = fi->Face->DefaultChar;
    tm->tmBreakChar = fi->Face->BreakChar;
    tm->tmItalic = fi->Italic ? 255 : 0;
    tm->tmUnderlined = fi->Underline ? 255 : 0;
    tm->tmStruckOut = fi->StrikeOut ? 255 : 0;
    tm->tmPitchAndFamily = fnt->PitchAndFamily;
    tm->tmCharSet = fnt->CharSet;
    fi->UnderlinePos = ky;
    fi->UnderlineSize = ky;
    fi->StrikeoutPos = tm->tmAscent / 3;
}

static FONT_INSTANCE *realize_raster(FONT_FACE *face, const LOGFONTW *lf)
{
    RASTER_FIT fit = {.Kx = 1, .Ky = 1};
    if (!raster_fit(face, lf, &fit))
        fit.Kx = fit.Ky = 1;
    LONG kx = fit.Kx, ky = fit.Ky;
    LONG weight = lf->lfWeight ? lf->lfWeight : FW_NORMAL;
    LONG em = face->Fnt.PixHeight * ky;
    for (PLIST_ENTRY e = g_instances.Flink; e != &g_instances; e = e->Flink) {
        FONT_INSTANCE *fi = CONTAINING_RECORD(e, FONT_INSTANCE, Links);
        if (fi->Face == face && fi->EmPixels == em && fi->ScaleX == (float)kx && fi->Weight == weight &&
            fi->Italic == !!lf->lfItalic && fi->Underline == !!lf->lfUnderline && fi->StrikeOut == !!lf->lfStrikeOut)
            return fi;
    }
    FONT_INSTANCE *fi = W32Alloc(sizeof(FONT_INSTANCE));
    if (!fi)
        return nullptr;
    fi->Face = face;
    fi->EmPixels = em;
    fi->Smooth = FONT_SMOOTH_ALIASED;
    fi->Width = lf->lfWidth;
    fi->Weight = weight;
    fi->Italic = lf->lfItalic != 0;
    fi->Underline = lf->lfUnderline != 0;
    fi->StrikeOut = lf->lfStrikeOut != 0;
    fi->SimBold = fit.SimBold;
    fi->SimItalic = lf->lfItalic && !face->Italic;
    fi->CharSet = face->Fnt.CharSet;
    static ULONG next_raster_id = 0x80000000u;
    fi->Id = ++next_raster_id;
    fi->ScaleX = (float)kx;
    fi->ScaleY = (float)ky;
    raster_metrics(fi);
    InsertTailList(&g_instances, &fi->Links);
    return fi;
}

/* how text of a logical font is smoothed: the quality asks for it, or the settings of the system decide. */
static UCHAR smooth_of(const LOGFONTW *lf)
{
    switch (lf->lfQuality) {
    case NONANTIALIASED_QUALITY:
        return FONT_SMOOTH_ALIASED;
    case ANTIALIASED_QUALITY:
        return FONT_SMOOTH_GRAY;
    case CLEARTYPE_QUALITY:
        return FONT_SMOOTH_CLEARTYPE;
    case CLEARTYPE_NATURAL_QUALITY:
        return FONT_SMOOTH_NATURAL;
    }
    UINT type, contrast, orientation;
    if (!MetricsFontSmoothing(&type, &contrast, &orientation))
        return FONT_SMOOTH_ALIASED;
    return type == 2 ? FONT_SMOOTH_CLEARTYPE : FONT_SMOOTH_GRAY;
}

static FONT_INSTANCE *realize_face(FONT_FACE *face, const LOGFONTW *lf, UINT dpi)
{
    if (!face || !FontLoadFace(face))
        return nullptr;
    if (face->Raster)
        return realize_raster(face, lf);
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
    UCHAR smooth = smooth_of(lf);
    /*
     * the gasp table of the font can turn the smoothing off, measured on windows: gray text is black and white at the
     * sizes without the gray flag, and ClearType only for a font that asks for no smoothing at any size, which is
     * marlett.
     */
    if (smooth == FONT_SMOOTH_GRAY) {
        unsigned gasp = mime_ttf_gasp(&face->Ttf, (int)em);
        if (gasp != 0xffff && !(gasp & 2))
            smooth = FONT_SMOOTH_ALIASED;
    } else if (smooth != FONT_SMOOTH_ALIASED && !mime_ttf_gasp_any(&face->Ttf, 2 | 4 | 8)) {
        smooth = FONT_SMOOTH_ALIASED;
    }
    UCHAR charset = resolve_charset(face, lf->lfCharSet);
    for (PLIST_ENTRY e = g_instances.Flink; e != &g_instances; e = e->Flink) {
        FONT_INSTANCE *fi = CONTAINING_RECORD(e, FONT_INSTANCE, Links);
        if (fi->Face == face && fi->EmPixels == em && fi->Smooth == smooth && fi->Width == lf->lfWidth &&
            fi->Weight == weight &&
            fi->Italic == !!lf->lfItalic && fi->Underline == !!lf->lfUnderline && fi->StrikeOut == !!lf->lfStrikeOut && fi->CharSet == charset)
            return fi;
    }
    FONT_INSTANCE *fi = W32Alloc(sizeof(FONT_INSTANCE));
    if (!fi)
        return nullptr;
    fi->Face = face;
    fi->EmPixels = em;
    fi->Smooth = smooth;
    fi->Width = lf->lfWidth;
    fi->Weight = weight;
    fi->Italic = lf->lfItalic != 0;
    fi->Underline = lf->lfUnderline != 0;
    fi->StrikeOut = lf->lfStrikeOut != 0;
    fi->SimBold = sim_bold;
    fi->SimItalic = sim_italic;
    fi->CharSet = charset;
    static ULONG next_id;
    fi->Id = ++next_id;
    fi->ScaleY = (float)em / (float)m->units_per_em;
    fi->ScaleX = fi->ScaleY;
    if (lf->lfWidth > 0 && m->avg_char_width > 0)
        fi->ScaleX = (float)lf->lfWidth / (float)m->avg_char_width;
    compute_metrics(fi, lf);
    InsertTailList(&g_instances, &fi->Links);
    return fi;
}

FONT_INSTANCE *FontRealize(const LOGFONTW *lf, UINT dpi)
{
    return realize_face(FontMatchFace(lf), lf, dpi);
}

/*
 * font linking, the SystemLink key of windows: a character the face lacks is drawn by the first font of its script
 * that has it, at the same size. the han characters go to the japanese fonts first, as on an english windows.
 */
static const WCHAR *const g_link_families[FONT_LINKS] = {
    L"Tahoma", L"Leelawadee UI", L"Microsoft Sans Serif", L"Malgun Gothic", L"Gulim", L"Batang", L"Yu Gothic UI",
    L"Meiryo UI", L"MS UI Gothic", L"Microsoft JhengHei UI", L"Microsoft YaHei UI", L"SimSun", L"PMingLiU", L"Segoe UI",
    L"Segoe UI Symbol", L"Arial", L"Nirmala UI", L"Ebrima", L"Gadugi", L"Myanmar Text", L"Sylfaen", L"Segoe UI Historic",
    L"Segoe UI Emoji", L"Cambria Math",
};
static const UCHAR g_link_thai[] = {0, 1, 2};
static const UCHAR g_link_hangul[] = {3, 4, 5};
static const UCHAR g_link_han[] = {6, 7, 8, 9, 10, 3, 11, 12, 4, 5};
static const UCHAR g_link_general[] = {13, 0, 14, 15, 2, 16, 17, 18, 1, 19, 20, 21, 22, 23,
                                       6, 7, 8, 9, 10, 3, 11, 12, 4, 5};

static BOOLEAN is_han(ULONG c)
{
    return (c >= 0x2E80 && c <= 0x9FFF) || (c >= 0xF900 && c <= 0xFAFF) || (c >= 0xFF00 && c <= 0xFFEF) ||
           (c >= 0x20000 && c <= 0x3FFFF);
}

static BOOLEAN is_hangul(ULONG c)
{
    return (c >= 0x1100 && c <= 0x11FF) || (c >= 0x3130 && c <= 0x318F) || (c >= 0xA960 && c <= 0xA97F) ||
           (c >= 0xAC00 && c <= 0xD7FF);
}

static const UCHAR g_link_quality[] = {
    [FONT_SMOOTH_ALIASED] = NONANTIALIASED_QUALITY,
    [FONT_SMOOTH_GRAY] = ANTIALIASED_QUALITY,
    [FONT_SMOOTH_CLEARTYPE] = CLEARTYPE_QUALITY,
    [FONT_SMOOTH_NATURAL] = CLEARTYPE_NATURAL_QUALITY,
};

static FONT_INSTANCE *linked(FONT_INSTANCE *fi, ULONG slot)
{
    if (!fi->LinkedTried[slot]) {
        fi->LinkedTried[slot] = TRUE;
        FONT_FACE *face = match_family(g_link_families[slot], fi->Weight, fi->Italic);
        /* the instances are everyone's, so a font private to a process is no link. */
        if (face && face != fi->Face && !face->File->OwnerPid) {
            LOGFONTW lf = {};
            lf.lfHeight = -fi->EmPixels;
            lf.lfWeight = fi->Weight;
            lf.lfItalic = fi->Italic;
            lf.lfQuality = fi->Smooth < RTL_NUMBER_OF(g_link_quality) ? g_link_quality[fi->Smooth] : DEFAULT_QUALITY;
            fi->Linked[slot] = realize_face(face, &lf, 96);
        }
    }
    return fi->Linked[slot];
}

/* the glyph + 1 of a character with the linked font that has it from bit 17, 0 for none. */
static ULONG link_entry(FONT_INSTANCE *fi, ULONG codepoint)
{
    if (fi->Face->Raster) {
        /* the byte in code page 1252, or the default character, with no font linked. */
        const FNT_INFO *fnt = &fi->Face->Fnt;
        int b = codepoint <= 0xFFFF ? FntFromUnicode(1252, (WCHAR)codepoint) : -1;
        if (b < fnt->FirstChar || b > fnt->LastChar)
            b = fnt->FirstChar + fnt->DefaultChar;
        return (ULONG)b + 1;
    }
    USHORT index = mime_ttf_glyph_index(&fi->Face->Ttf, codepoint);
    if (index || fi->Face->Symbol || codepoint < 0x20 || (codepoint >= 0xD800 && codepoint < 0xE000))
        return (ULONG)index + 1;
    const UCHAR *order = g_link_general;
    ULONG count = RTL_NUMBER_OF(g_link_general);
    if (codepoint >= 0x0E00 && codepoint <= 0x0E7F) {
        order = g_link_thai;
        count = RTL_NUMBER_OF(g_link_thai);
    } else if (is_hangul(codepoint)) {
        order = g_link_hangul;
        count = RTL_NUMBER_OF(g_link_hangul);
    } else if (is_han(codepoint)) {
        order = g_link_han;
        count = RTL_NUMBER_OF(g_link_han);
    }
    for (int pass = 0; pass < 2; pass++) {
        for (ULONG i = 0; i < count; i++) {
            FONT_INSTANCE *l = linked(fi, order[i]);
            USHORT found = l ? mime_ttf_glyph_index(&l->Face->Ttf, codepoint) : 0;
            if (found)
                return ((ULONG)(order[i] + 1) << 17) | ((ULONG)found + 1);
        }
        if (order == g_link_general)
            break;
        order = g_link_general;
        count = RTL_NUMBER_OF(g_link_general);
    }
    return 1;
}

static ULONG char_entry(FONT_INSTANCE *fi, ULONG codepoint)
{
    if (fi->Generation != g_font_generation) {
        fi->Generation = g_font_generation;
        for (ULONG i = 0; i < FONT_LINKS; i++)
            if (!fi->Linked[i])
                fi->LinkedTried[i] = FALSE;
        for (ULONG p = 0; p < 256; p++)
            for (ULONG c = 0; fi->CharPages[p] && c < 256; c++)
                if (fi->CharPages[p][c] == 1 || fi->CharPages[p][c] >> 17)
                    fi->CharPages[p][c] = 0;
    }
    if (codepoint > 0xFFFF)
        return link_entry(fi, codepoint);
    ULONG **page = &fi->CharPages[codepoint >> 8];
    if (!*page) {
        *page = W32Alloc(256 * sizeof(ULONG));
        if (!*page)
            return link_entry(fi, codepoint);
    }
    ULONG *entry = &(*page)[codepoint & 0xFF];
    if (!*entry)
        *entry = link_entry(fi, codepoint);
    return *entry;
}

USHORT FontLinkedGlyph(FONT_INSTANCE *fi, ULONG codepoint, FONT_INSTANCE **owner)
{
    ULONG entry = char_entry(fi, codepoint);
    ULONG slot = entry >> 17;
    *owner = slot && fi->Linked[slot - 1] ? fi->Linked[slot - 1] : fi;
    return (USHORT)((entry & 0x1FFFF) - 1);
}

/* the interpreter for the size of an instance, null when the size is drawn unhinted. */
MimeTtfHinter *FontHinter(FONT_INSTANCE *fi)
{
    if (fi->Face->Raster)
        return nullptr;
    if (!fi->HinterTried) {
        fi->HinterTried = TRUE;
        if (fi->ScaleX == fi->ScaleY && fi->EmPixels >= 1 && fi->EmPixels <= 1024 && mime_ttf_has_outlines(&fi->Face->Ttf))
            fi->Hinter = mime_ttf_hinter_new(&fi->Face->Ttf, &FontAllocator, fi->EmPixels, fi->EmPixels,
                                             MIME_TTF_HINT_GRAY);
    }
    return fi->Hinter;
}

MimeTtfHinter *FontHinterAliased(FONT_INSTANCE *fi)
{
    if (fi->Face->Raster)
        return nullptr;
    if (!fi->HinterAliasedTried) {
        fi->HinterAliasedTried = TRUE;
        if (FontHinter(fi))
            fi->HinterAliased = mime_ttf_hinter_new(&fi->Face->Ttf, &FontAllocator, fi->EmPixels, fi->EmPixels, 0);
    }
    return fi->HinterAliased;
}

/*
 * ClearType smooths down as well, five rows to a pixel, where the gasp table of the font asks for symmetric smoothing.
 * a font without the table gets it from 21 pixels on, and one without instructions at any size.
 */
static BOOLEAN smooths_down(FONT_INSTANCE *fi)
{
    const MimeTtfFont *ttf = &fi->Face->Ttf;
    unsigned gasp = mime_ttf_gasp(ttf, fi->EmPixels);
    if (gasp != 0xffff)
        return (gasp & 8) != 0;
    return fi->EmPixels > 20 || ttf->fpgm.size + ttf->prep.size + ttf->cvt.size == 0;
}

/* the interpreter for ClearType text, null when the size is drawn unhinted. */
MimeTtfHinter *FontHinterClearType(FONT_INSTANCE *fi)
{
    if (fi->Face->Raster)
        return nullptr;
    if (!fi->HinterCtTried) {
        fi->HinterCtTried = TRUE;
        fi->CtVertical = smooths_down(fi);
        if (fi->ScaleX == fi->ScaleY && fi->EmPixels >= 1 && fi->EmPixels <= 1024 && mime_ttf_has_outlines(&fi->Face->Ttf)) {
            uint32_t flags = MIME_TTF_HINT_CLEARTYPE;
            if (fi->Smooth != FONT_SMOOTH_NATURAL)
                flags |= MIME_TTF_HINT_COMPAT_WIDTHS;
            fi->HinterCt = mime_ttf_hinter_new(&fi->Face->Ttf, &FontAllocator, fi->EmPixels, fi->EmPixels, flags);
        }
    }
    return fi->HinterCt;
}

/*
 * the device advance of a glyph. the hdmx table wins when it has the size, then glyphs the LTSH table calls linear
 * are scaled and rounded once, and the rest take the width the instructions leave.
 */
static const UCHAR *raster_data(FONT_INSTANCE *fi)
{
    return fi->Face->File->Data + fi->Face->DirOffset;
}

/* a glyph index out of the range of a raster face draws its default character. */
static USHORT raster_index(const FONT_INSTANCE *fi, USHORT index)
{
    const FNT_INFO *fnt = &fi->Face->Fnt;
    return index < fnt->FirstChar || index > fnt->LastChar ? (USHORT)(fnt->FirstChar + fnt->DefaultChar) : index;
}

LONG FontGlyphAdvance(FONT_INSTANCE *fi, USHORT index)
{
    if (fi->Face->Raster) {
        index = raster_index(fi, index);
        ULONG width, bits;
        if (!FntGlyph(raster_data(fi), &fi->Face->Fnt, index, &width, &bits))
            return 0;
        return (LONG)width * (LONG)fi->ScaleX + (fi->SimBold ? 1 : 0);
    }
    const MimeTtfFont *ttf = &fi->Face->Ttf;
    int advance, lsb;
    mime_ttf_glyph_hmetrics(ttf, index, &advance, &lsb);
    MimeTtfHinter *h = FontHinter(fi);
    if (!h)
        return round_f((float)advance * fi->ScaleX);
    int device;
    if (mime_ttf_hdmx_advance(ttf, fi->EmPixels, index, &device))
        return device;
    unsigned linear_from = mime_ttf_ltsh_threshold(ttf, index);
    if (linear_from && (unsigned)fi->EmPixels >= linear_from)
        return (LONG)(((int64_t)advance * fi->EmPixels * 2 + ttf->units_per_em) / (2 * (int64_t)ttf->units_per_em));
    int32_t hinted;
    if (mime_ttf_hint_advance(h, index, &hinted))
        return (hinted + 32) >> 6;
    return round_f((float)advance * fi->ScaleX);
}

/* the levels of a glyph for ClearType, fine to leave out. */
static void render_clear_type(FONT_INSTANCE *fi, USHORT index, GLYPH *g)
{
    if (fi->SimBold)
        return;
    FONT_FACE *face = fi->Face;
    MimeTtfRenderParams params = {0};
    params.hinter = FontHinterClearType(fi);
    if (!params.hinter) {
        params.scale_x = fi->ScaleX;
        params.scale_y = fi->ScaleY;
    }
    params.oblique = fi->SimItalic ? 0.2f : 0.0f;
    MimeTtfScan scan = {0, 0, (int)fi->EmPixels, false, false};
    if (params.hinter)
        mime_ttf_hinter_scan(params.hinter, &scan.control, &scan.type);
    MimeTtfBitmap bm;
    if (!mime_ttf_cleartype_bitmap(&face->Ttf, &FontAllocator, index, &params, fi->CtVertical ? 5 : 1, &scan, &bm))
        return;
    LONG w = bm.x1 - bm.x0, h = bm.y1 - bm.y0;
    if (bm.data && w > 0 && h > 0 && w <= 4096 && h <= 4096) {
        UCHAR *levels = W32Alloc((SIZE_T)w * (SIZE_T)h * 3);
        if (levels) {
            memcpy(levels, bm.data, (SIZE_T)w * (SIZE_T)h * 3);
            g->Subpixel = levels;
            g->SubLeft = (SHORT)bm.x0;
            g->SubTop = (SHORT)bm.y0;
            g->SubWidth = (USHORT)w;
            g->SubHeight = (USHORT)h;
        }
    }
    mime_ttf_bitmap_free(&FontAllocator, &bm);
}

/* text without smoothing: a sample is on or off. */
static BOOLEAN render_aliased(FONT_INSTANCE *fi, USHORT index, GLYPH *g)
{
    FONT_FACE *face = fi->Face;
    MimeTtfRenderParams params = {0};
    params.hinter = FontHinterAliased(fi);
    if (!params.hinter) {
        params.scale_x = fi->ScaleX;
        params.scale_y = fi->ScaleY;
    }
    params.oblique = fi->SimItalic ? 0.2f : 0.0f;
    params.embolden = fi->SimBold ? (float)W32Max(1, fi->EmPixels / 24) : 0.0f;
    MimeTtfScan scan = {0, 0, (int)fi->EmPixels, false, false};
    if (params.hinter)
        mime_ttf_hinter_scan(params.hinter, &scan.control, &scan.type);
    MimeTtfBitmap bm;
    if (!mime_ttf_scan_bitmap(&face->Ttf, &FontAllocator, index, &params, &scan, &bm))
        return FALSE;
    LONG w = bm.x1 - bm.x0, h = bm.y1 - bm.y0;
    if (!bm.data || w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        mime_ttf_bitmap_free(&FontAllocator, &bm);
        g->Width = g->Height = 0;
        return TRUE;
    }
    UCHAR *cov = W32Alloc((SIZE_T)w * (SIZE_T)h);
    if (!cov) {
        mime_ttf_bitmap_free(&FontAllocator, &bm);
        return FALSE;
    }
    for (LONG y = 0; y < h; y++)
        for (LONG x = 0; x < w; x++)
            cov[y * w + x] = bm.data[y * bm.stride + x] ? 255 : 0;
    g->Left = (SHORT)bm.x0;
    g->Top = (SHORT)bm.y0;
    g->Width = (USHORT)w;
    g->Height = (USHORT)h;
    g->Coverage = cov;
    mime_ttf_bitmap_free(&FontAllocator, &bm);
    return TRUE;
}

/*
 * a character of a raster face, its whole cell: each pixel stretched by the factors, a simulated bold is the bitmap
 * over itself a pixel to the right and a simulated italic moves row r of a cell of height h by (h - r) / 2 to the
 * right, as windows 11 draws them.
 */
static BOOLEAN render_raster(FONT_INSTANCE *fi, USHORT index, GLYPH *g)
{
    const FNT_INFO *fnt = &fi->Face->Fnt;
    const UCHAR *data = raster_data(fi);
    ULONG width, bits;
    g->Index = index;
    g->Advance = (SHORT)FontGlyphAdvance(fi, index);
    if (!FntGlyph(data, fnt, raster_index(fi, index), &width, &bits)) {
        g->Width = g->Height = 0;
        return TRUE;
    }
    LONG kx = (LONG)fi->ScaleX, ky = (LONG)fi->ScaleY, bold = fi->SimBold ? 1 : 0;
    LONG w = (LONG)width * kx, h = fnt->PixHeight * ky, lean = fi->SimItalic ? (h - 1) / 2 : 0;
    LONG out_w = w + bold + lean;
    if (out_w <= 0 || h <= 0 || out_w > 4096 || h > 4096) {
        g->Width = g->Height = 0;
        return TRUE;
    }
    UCHAR *cov = W32Alloc((SIZE_T)out_w * (SIZE_T)h);
    if (!cov)
        return FALSE;
    for (LONG y = 0; y < h; y++) {
        LONG shift = fi->SimItalic ? (h - y) / 2 : 0;
        for (LONG x = 0; x < out_w; x++) {
            BOOLEAN on = FALSE;
            for (LONG b = 0; b <= bold && !on; b++) {
                LONG sx = x - shift - b;
                on = sx >= 0 && sx < w && FntPixel(data, fnt, bits, (ULONG)(sx / kx), (ULONG)(y / ky));
            }
            cov[y * out_w + x] = on ? 255 : 0;
        }
    }
    g->Left = 0;
    g->Top = (SHORT)-(fnt->Ascent * ky);
    g->Width = (USHORT)out_w;
    g->Height = (USHORT)h;
    g->Coverage = cov;
    return TRUE;
}

static BOOLEAN render_glyph(FONT_INSTANCE *fi, USHORT index, GLYPH *g)
{
    if (fi->Face->Raster)
        return render_raster(fi, index, g);
    FONT_FACE *face = fi->Face;
    MimeTtfRenderParams params = {fi->ScaleX, fi->ScaleY, 0, 0, fi->SimItalic ? 0.2f : 0.0f,
                                  fi->SimBold ? (float)W32Max(1, fi->EmPixels / 24) : 0.0f};
    params.hinter = FontHinter(fi);
    g->Index = index;
    g->Advance = (SHORT)(FontGlyphAdvance(fi, index) + (fi->SimBold ? W32Max(1, fi->EmPixels / 24) : 0));
    if (fi->Smooth >= FONT_SMOOTH_CLEARTYPE)
        render_clear_type(fi, index, g);
    if (fi->Smooth == FONT_SMOOTH_ALIASED)
        return render_aliased(fi, index, g);
    int x0, y0, x1, y1;
    if (params.hinter && !mime_ttf_glyph_box_ex(&face->Ttf, index, &params, &x0, &y0, &x1, &y1))
        params.hinter = nullptr;
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
    if (!mime_ttf_render_glyph_ex(&face->Ttf, &FontAllocator, index, &params, cov, (int)w, (int)h, (int)w)) {
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

/* the face's own glyph, without the linked fonts, as GetGlyphIndices gives it. */
USHORT FontGlyphIndex(FONT_INSTANCE *fi, ULONG codepoint)
{
    if (codepoint > 0xFFFF && fi->Face->Raster)
        return (USHORT)(char_entry(fi, codepoint) - 1);
    if (codepoint > 0xFFFF)
        return mime_ttf_glyph_index(&fi->Face->Ttf, codepoint);
    ULONG entry = char_entry(fi, codepoint);
    return entry >> 17 ? 0 : (USHORT)(entry - 1);
}

const GLYPH *FontGetGlyphIndex(FONT_INSTANCE *fi, USHORT index)
{
    GLYPH **page = &fi->Pages[index >> 8];
    if (!*page) {
        *page = W32Alloc(256 * sizeof(GLYPH));
        if (!*page)
            return nullptr;
    }
    GLYPH *g = &(*page)[index & 0xFF];
    if (!g->Ready) {
        if (!render_glyph(fi, index, g)) {
            g->Width = g->Height = 0;
            g->Advance = 0;
        }
        g->Index = index;
        g->Ready = 1;
    }
    return g;
}

const GLYPH *FontGetGlyph(FONT_INSTANCE *fi, ULONG codepoint)
{
    FONT_INSTANCE *owner;
    USHORT index = FontLinkedGlyph(fi, codepoint, &owner);
    return FontGetGlyphIndex(owner, index);
}

/*
 * GrepGetTextFaceW gives the face name of the logical font when the mapper took the face through a FontSubstitutes
 * name, so MS Shell Dlg stays MS Shell Dlg, and the family of the face otherwise.
 */
void FontGetFaceName(FONT_INSTANCE *fi, PCWSTR requested, PWSTR name, ULONG chars)
{
    if (!chars)
        return;
    PCWSTR face = fi->Face->Family;
    if (requested && requested[0] && substitute(requested) != requested && !_wcsicmp(substitute(requested), face))
        face = requested;
    ULONG n = (ULONG)wcslen(face);
    if (n >= chars)
        n = chars - 1;
    memcpy(name, face, n * sizeof(WCHAR));
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

/* the non client metrics font: windows 11 uses Segoe UI 9 point throughout. */
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
