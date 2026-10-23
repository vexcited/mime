/*
 * windows metafiles: the 16-bit metafile dc, which lives only in gdi32 like
 * on windows, playback, and conversion to and from enhanced metafiles.
 *
 * a metafile is an 18 byte header then records of 16-bit words: size in
 * words, function, then the parameters in reverse call order. objects take
 * the first free slot of a table sized by the header, on first selection.
 */
#include "emf_private.h"
#include <mime/win32/winnls.h>

#define WMF_HEADER_WORDS 9
#define WMF_MAX 1024

static RTL_SRWLOCK g_lock;

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

/* ------------------------------------------------------------------ metafile objects */

typedef struct WMF {
    BYTE *Data;
    DWORD Size;
} WMF;

static WMF *g_wmfs[WMF_MAX];
static BYTE g_wmf_unique[WMF_MAX];

static BOOL header_valid(const BYTE *data, DWORD size)
{
    if (!data || size < sizeof(METAHEADER))
        return FALSE;
    const METAHEADER *h = (const METAHEADER *)data;
    return (h->mtType == 1 || h->mtType == 2) && h->mtHeaderSize == WMF_HEADER_WORDS &&
           (h->mtVersion == 0x100 || h->mtVersion == 0x300);
}

static HMETAFILE wmf_create_handle(BYTE *data, DWORD size)
{
    WMF *w = heap_zalloc(sizeof(WMF));
    if (!w)
        return nullptr;
    w->Data = data;
    w->Size = size;
    HMETAFILE h = nullptr;
    RtlAcquireSRWLockExclusive(&g_lock);
    for (UINT i = 1; i < WMF_MAX; i++) {
        if (!g_wmfs[i]) {
            g_wmfs[i] = w;
            g_wmf_unique[i]++;
            h = (HMETAFILE)(ULONG_PTR)(((ULONG)(g_wmf_unique[i] & 0x7F) << 24) | (MF_TYPE_WMF << 16) | i);
            break;
        }
    }
    RtlReleaseSRWLockExclusive(&g_lock);
    if (!h)
        heap_free(w);
    return h;
}

static UINT wmf_index(HMETAFILE handle)
{
    ULONG_PTR v = (ULONG_PTR)handle;
    UINT i = (UINT)(v & 0xFFFF);
    if (((v >> 16) & 0xFF) != MF_TYPE_WMF || !i || i >= WMF_MAX || ((v >> 24) & 0x7F) != (g_wmf_unique[i] & 0x7Fu))
        return 0;
    return i;
}

static WMF *wmf_lookup(HMETAFILE handle)
{
    RtlAcquireSRWLockShared(&g_lock);
    UINT i = wmf_index(handle);
    WMF *w = i ? g_wmfs[i] : nullptr;
    RtlReleaseSRWLockShared(&g_lock);
    return w;
}

/* ------------------------------------------------------------------ metafile dcs */

#define WMF_DC_MAX 256

typedef struct WMF_DC {
    BYTE *Data;
    DWORD Size;
    DWORD Capacity;
    DWORD MaxRecord;
    BOOL Failed;
    HGDIOBJ *Objects;
    WORD Slots;    /* object table size written in the header. */
    WORD Capacity16;
    HGDIOBJ Pen, Brush, Font, Palette;
    HANDLE File;
    WCHAR Path[MAX_PATH];
} WMF_DC;

static WMF_DC *g_dcs[WMF_DC_MAX];
static BYTE g_dc_unique[WMF_DC_MAX];

static WMF_DC *dc_of(HDC dc)
{
    ULONG_PTR v = (ULONG_PTR)dc;
    UINT i = (UINT)(v & 0xFFFF);
    if (((v >> 16) & 0xFF) != MF_TYPE_WMF_DC || i >= WMF_DC_MAX || ((v >> 24) & 0x7F) != (g_dc_unique[i] & 0x7Fu))
        return nullptr;
    RtlAcquireSRWLockShared(&g_lock);
    WMF_DC *w = g_dcs[i];
    RtlReleaseSRWLockShared(&g_lock);
    return w;
}

static BOOL append(WMF_DC *w, const void *data, DWORD size)
{
    if (w->Failed)
        return FALSE;
    if (w->Size + size > w->Capacity) {
        DWORD cap = w->Capacity ? w->Capacity : 0x4000;
        while (cap < w->Size + size)
            cap *= 2;
        BYTE *d = heap_realloc(w->Data, cap);
        if (!d) {
            w->Failed = TRUE;
            return FALSE;
        }
        w->Data = d;
        w->Capacity = cap;
    }
    memcpy(w->Data + w->Size, data, size);
    w->Size += size;
    return TRUE;
}

static BOOL record_raw(WMF_DC *w, WORD function, const WORD *params, UINT count, const void *tail, UINT tail_bytes)
{
    DWORD words = 3 + count + (tail_bytes + 1) / 2;
    WORD head[3] = {(WORD)words, (WORD)(words >> 16), function};
    if (!append(w, head, sizeof(head)) || (count && !append(w, params, count * 2)))
        return FALSE;
    if (tail_bytes) {
        if (!append(w, tail, tail_bytes))
            return FALSE;
        if (tail_bytes & 1) {
            BYTE zero = 0;
            if (!append(w, &zero, 1))
                return FALSE;
        }
    }
    if (words > w->MaxRecord)
        w->MaxRecord = words;
    return TRUE;
}

BOOL WmfRecord(HDC dc, WORD function, UINT count, const WORD *params)
{
    WMF_DC *w = dc_of(dc);
    if (!w) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return record_raw(w, function, params, count, nullptr, 0);
}

BOOL WmfDibRecord(HDC dc, WORD function, const WORD *params, UINT count, const BITMAPINFO *info, UINT info_size,
                  const void *bits, UINT bits_size)
{
    WMF_DC *w = dc_of(dc);
    if (!w)
        return FALSE;
    BYTE *dib = heap_alloc((SIZE_T)info_size + bits_size + 1);
    if (!dib)
        return FALSE;
    if (info)
        memcpy(dib, info, info_size);
    if (bits && bits_size)
        memcpy(dib + info_size, bits, bits_size);
    BOOL ok = record_raw(w, function, params, count, dib, info ? info_size + bits_size : 0);
    heap_free(dib);
    return ok;
}

HDC WmfCreateDC(void)
{
    WMF_DC *w = heap_zalloc(sizeof(WMF_DC));
    if (!w)
        return nullptr;
    w->File = INVALID_HANDLE_VALUE;
    w->Pen = GetStockObject(BLACK_PEN);
    w->Brush = GetStockObject(WHITE_BRUSH);
    w->Font = GetStockObject(SYSTEM_FONT);
    w->Palette = GetStockObject(DEFAULT_PALETTE);
    METAHEADER header;
    memset(&header, 0, sizeof(header));
    if (!append(w, &header, sizeof(header))) {
        heap_free(w);
        return nullptr;
    }
    HDC h = nullptr;
    RtlAcquireSRWLockExclusive(&g_lock);
    for (UINT i = 1; i < WMF_DC_MAX; i++) {
        if (!g_dcs[i]) {
            g_dcs[i] = w;
            g_dc_unique[i]++;
            h = (HDC)(ULONG_PTR)(((ULONG)(g_dc_unique[i] & 0x7F) << 24) | (MF_TYPE_WMF_DC << 16) | i);
            _InterlockedIncrement(&MfDcCount);
            break;
        }
    }
    RtlReleaseSRWLockExclusive(&g_lock);
    if (!h) {
        heap_free(w->Data);
        heap_free(w);
    }
    return h;
}

static void dc_destroy(HDC dc, WMF_DC *w, BOOL delete_file)
{
    RtlAcquireSRWLockExclusive(&g_lock);
    UINT i = (UINT)((ULONG_PTR)dc & 0xFFFF);
    if (i < WMF_DC_MAX && g_dcs[i] == w) {
        g_dcs[i] = nullptr;
        _InterlockedDecrement(&MfDcCount);
    }
    RtlReleaseSRWLockExclusive(&g_lock);
    if (w->File != INVALID_HANDLE_VALUE) {
        CloseHandle(w->File);
        if (delete_file)
            DeleteFileW(w->Path);
    }
    heap_free(w->Objects);
    heap_free(w->Data);
    heap_free(w);
}

BOOL WmfDeleteDC(HDC dc)
{
    WMF_DC *w = dc_of(dc);
    if (!w)
        return FALSE;
    dc_destroy(dc, w, TRUE);
    return TRUE;
}

/* ------------------------------------------------------------------ objects */

static BOOL put_object(WMF_DC *w, HGDIOBJ object, WORD *slot)
{
    for (WORD i = 0; i < w->Slots; i++) {
        if (!w->Objects[i]) {
            w->Objects[i] = object;
            *slot = i;
            return TRUE;
        }
    }
    if (w->Slots == w->Capacity16) {
        WORD cap = w->Capacity16 ? (WORD)(w->Capacity16 * 2) : 16;
        HGDIOBJ *objects = heap_zalloc(cap * sizeof(HGDIOBJ));
        if (!objects)
            return FALSE;
        if (w->Objects)
            memcpy(objects, w->Objects, w->Slots * sizeof(HGDIOBJ));
        heap_free(w->Objects);
        w->Objects = objects;
        w->Capacity16 = cap;
    }
    w->Objects[w->Slots] = object;
    *slot = w->Slots++;
    return TRUE;
}

/* the 16-bit LOGFONT: five shorts, eight bytes and an ansi face name. */
static BOOL create_font(WMF_DC *w, HGDIOBJ font)
{
    LOGFONTW lf;
    if (GetObjectW(font, sizeof(lf), &lf) != sizeof(lf))
        return FALSE;
    BYTE p[50];
    memset(p, 0, sizeof(p));
    SHORT shorts[5] = {(SHORT)lf.lfHeight, (SHORT)lf.lfWidth, (SHORT)lf.lfEscapement, (SHORT)lf.lfOrientation,
                       (SHORT)lf.lfWeight};
    memcpy(p, shorts, sizeof(shorts));
    memcpy(p + 10, &lf.lfItalic, 8);
    WideCharToMultiByte(CP_ACP, 0, lf.lfFaceName, -1, (char *)p + 18, LF_FACESIZE, nullptr, nullptr);
    return record_raw(w, META_CREATEFONTINDIRECT, (const WORD *)p, 25, nullptr, 0);
}

static BOOL create_object(WMF_DC *w, HGDIOBJ object)
{
    switch (((ULONG_PTR)object >> 16) & 0xFF) {
    case 0x30:
    case 0x50: {
        DWORD style = PS_SOLID, width = 0;
        COLORREF color = 0;
        if ((((ULONG_PTR)object >> 16) & 0xFF) == 0x50) {
            EXTLOGPEN elp;
            if (!GetObjectW(object, sizeof(elp), &elp))
                return FALSE;
            style = elp.elpPenStyle & PS_STYLE_MASK;
            width = elp.elpWidth;
            color = elp.elpColor;
        } else {
            LOGPEN lp;
            if (!GetObjectW(object, sizeof(lp), &lp))
                return FALSE;
            style = lp.lopnStyle;
            width = (DWORD)lp.lopnWidth.x;
            color = lp.lopnColor;
        }
        WORD p[5] = {(WORD)style, (WORD)width, 0, (WORD)(color & 0xFFFF), (WORD)(color >> 16)};
        return record_raw(w, META_CREATEPENINDIRECT, p, 5, nullptr, 0);
    }
    case 0x10: {
        LOGBRUSH lb;
        if (!GetObjectW(object, sizeof(lb), &lb))
            return FALSE;
        WORD p[4] = {(WORD)(lb.lbStyle == BS_NULL ? BS_NULL : lb.lbStyle == BS_HATCHED ? BS_HATCHED : BS_SOLID),
                     (WORD)(lb.lbColor & 0xFFFF), (WORD)(lb.lbColor >> 16), (WORD)lb.lbHatch};
        return record_raw(w, META_CREATEBRUSHINDIRECT, p, 4, nullptr, 0);
    }
    case 0x0A:
        return create_font(w, object);
    case 0x08: {
        PALETTEENTRY entries[256];
        WORD count = 0;
        if (GetObjectW(object, sizeof(count), &count) != sizeof(count))
            return FALSE;
        UINT n = count > 256 ? 256 : count;
        GetPaletteEntries(object, 0, n, entries);
        WORD head[2] = {0x300, (WORD)n};
        BYTE *p = heap_alloc(4 + n * 4);
        if (!p)
            return FALSE;
        memcpy(p, head, 4);
        memcpy(p + 4, entries, n * 4);
        BOOL ok = record_raw(w, META_CREATEPALETTE, (const WORD *)p, 2 + 2 * n, nullptr, 0);
        heap_free(p);
        return ok;
    }
    }
    return FALSE;
}

BOOL WmfSelectObject(HDC dc, HGDIOBJ object, HGDIOBJ *previous)
{
    WMF_DC *w = dc_of(dc);
    if (!w || !object)
        return FALSE;
    UINT type = (UINT)(((ULONG_PTR)object >> 16) & 0xFF);
    if (type == 0x04)
        return MfSelectClipRgn(dc, object, RGN_COPY);
    WORD slot = 0xFFFF;
    for (WORD i = 0; i < w->Slots; i++)
        if (w->Objects[i] == object)
            slot = i;
    if (slot == 0xFFFF) {
        if (!create_object(w, object) || !put_object(w, object, &slot))
            return FALSE;
    }
    if (!record_raw(w, type == 0x08 ? META_SELECTPALETTE : META_SELECTOBJECT, &slot, 1, nullptr, 0))
        return FALSE;
    HGDIOBJ *current = type == 0x10 ? &w->Brush : type == 0x0A ? &w->Font : type == 0x08 ? &w->Palette : &w->Pen;
    if (previous)
        *previous = *current;
    *current = object;
    return TRUE;
}

/* a deleted object is dropped from every metafile dc holding it. */
void WmfDeleteObject(HGDIOBJ object)
{
    if (!MfDcCount)
        return;
    WMF_DC *dcs[WMF_DC_MAX];
    UINT n = 0;
    RtlAcquireSRWLockShared(&g_lock);
    for (UINT i = 0; i < WMF_DC_MAX; i++)
        if (g_dcs[i])
            dcs[n++] = g_dcs[i];
    RtlReleaseSRWLockShared(&g_lock);
    for (UINT k = 0; k < n; k++) {
        WMF_DC *w = dcs[k];
        for (WORD i = 0; i < w->Slots; i++) {
            if (w->Objects[i] != object)
                continue;
            record_raw(w, META_DELETEOBJECT, &i, 1, nullptr, 0);
            w->Objects[i] = nullptr;
        }
    }
}

/* ------------------------------------------------------------------ creating and closing */

WINGDIAPI HDC WINAPI CreateMetaFileW(LPCWSTR file)
{
    HDC dc = WmfCreateDC();
    if (!dc || !file)
        return dc;
    WMF_DC *w = dc_of(dc);
    DWORD n = GetFullPathNameW(file, MAX_PATH, w->Path, nullptr);
    if (n && n < MAX_PATH)
        w->File = CreateFileW(w->Path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (w->File == INVALID_HANDLE_VALUE) {
        if (n >= MAX_PATH)
            SetLastError(ERROR_FILENAME_EXCED_RANGE);
        dc_destroy(dc, w, FALSE);
        return nullptr;
    }
    return dc;
}

WINGDIAPI HDC WINAPI CreateMetaFileA(LPCSTR file)
{
    WCHAR wfile[MAX_PATH];
    if (file && !MultiByteToWideChar(CP_ACP, 0, file, -1, wfile, MAX_PATH))
        return nullptr;
    return CreateMetaFileW(file ? wfile : nullptr);
}

static BOOL write_all(HANDLE file, const BYTE *data, DWORD size)
{
    DWORD written = 0;
    return WriteFile(file, data, size, &written, nullptr) && written == size;
}

static HMETAFILE wmf_from_bits(const BYTE *data, DWORD size)
{
    BYTE *copy = heap_alloc(size);
    if (!copy)
        return nullptr;
    memcpy(copy, data, size);
    HMETAFILE h = wmf_create_handle(copy, size);
    if (!h)
        heap_free(copy);
    return h;
}

WINGDIAPI HMETAFILE WINAPI CloseMetaFile(HDC dc)
{
    WMF_DC *w = dc_of(dc);
    if (!w) {
        SetLastError(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    HMETAFILE result = nullptr;
    if (record_raw(w, 0, nullptr, 0, nullptr, 0)) {
        METAHEADER *h = (METAHEADER *)w->Data;
        h->mtType = w->File != INVALID_HANDLE_VALUE ? 2 : 1;
        h->mtHeaderSize = WMF_HEADER_WORDS;
        h->mtVersion = 0x300;
        h->mtSize = w->Size / 2;
        h->mtNoObjects = w->Slots;
        h->mtMaxRecord = w->MaxRecord;
        h->mtNoParameters = 0;
        BOOL ok = TRUE;
        if (w->File != INVALID_HANDLE_VALUE) {
            /* like windows, a disk metafile's header still says memory metafile. */
            h->mtType = 1;
            ok = write_all(w->File, w->Data, w->Size);
            h->mtType = 2;
            CloseHandle(w->File);
            w->File = INVALID_HANDLE_VALUE;
        }
        if (ok)
            result = wmf_from_bits(w->Data, w->Size);
    }
    dc_destroy(dc, w, result == nullptr);
    return result;
}

WINGDIAPI BOOL WINAPI DeleteMetaFile(HMETAFILE handle)
{
    RtlAcquireSRWLockExclusive(&g_lock);
    UINT i = wmf_index(handle);
    WMF *w = i ? g_wmfs[i] : nullptr;
    if (w)
        g_wmfs[i] = nullptr;
    RtlReleaseSRWLockExclusive(&g_lock);
    if (!w) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    heap_free(w->Data);
    heap_free(w);
    return TRUE;
}

WINGDIAPI UINT WINAPI GetMetaFileBitsEx(HMETAFILE handle, UINT size, LPVOID data)
{
    WMF *w = wmf_lookup(handle);
    if (!w) {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (!data)
        return w->Size;
    UINT n = size < w->Size ? size : w->Size;
    memcpy(data, w->Data, n);
    return n;
}

WINGDIAPI HMETAFILE WINAPI SetMetaFileBitsEx(UINT size, const BYTE *data)
{
    if (!header_valid(data, size)) {
        SetLastError(ERROR_INVALID_DATA);
        return nullptr;
    }
    const METAHEADER *h = (const METAHEADER *)data;
    DWORD bytes = h->mtSize * 2;
    if (bytes > size || bytes < sizeof(METAHEADER))
        bytes = size;
    HMETAFILE result = wmf_from_bits(data, bytes);
    WMF *w = result ? wmf_lookup(result) : nullptr;
    if (w)
        ((METAHEADER *)w->Data)->mtType = 1;
    return result;
}

WINGDIAPI HMETAFILE WINAPI GetMetaFileW(LPCWSTR file)
{
    HANDLE f = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return nullptr;
    LARGE_INTEGER size;
    HMETAFILE h = nullptr;
    if (GetFileSizeEx(f, &size) && size.QuadPart >= (LONGLONG)sizeof(METAHEADER) && size.QuadPart <= 0x7FFFFFFF) {
        BYTE *data = heap_alloc((SIZE_T)size.QuadPart);
        DWORD read = 0;
        if (data && ReadFile(f, data, (DWORD)size.QuadPart, &read, nullptr) && header_valid(data, read))
            h = SetMetaFileBitsEx(read, data);
        heap_free(data);
    }
    CloseHandle(f);
    return h;
}

WINGDIAPI HMETAFILE WINAPI GetMetaFileA(LPCSTR file)
{
    WCHAR wfile[MAX_PATH];
    if (!file || !MultiByteToWideChar(CP_ACP, 0, file, -1, wfile, MAX_PATH))
        return nullptr;
    return GetMetaFileW(wfile);
}

WINGDIAPI HMETAFILE WINAPI CopyMetaFileW(HMETAFILE handle, LPCWSTR file)
{
    WMF *w = wmf_lookup(handle);
    if (!w) {
        SetLastError(ERROR_INVALID_HANDLE);
        return nullptr;
    }
    if (!file)
        return wmf_from_bits(w->Data, w->Size);
    HANDLE f = CreateFileW(file, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return nullptr;
    BOOL ok = write_all(f, w->Data, w->Size);
    CloseHandle(f);
    return ok ? GetMetaFileW(file) : nullptr;
}

WINGDIAPI HMETAFILE WINAPI CopyMetaFileA(HMETAFILE handle, LPCSTR file)
{
    WCHAR wfile[MAX_PATH];
    if (file && !MultiByteToWideChar(CP_ACP, 0, file, -1, wfile, MAX_PATH))
        return nullptr;
    return CopyMetaFileW(handle, file ? wfile : nullptr);
}

DWORD WmfObjectType(HGDIOBJ handle)
{
    ULONG_PTR type = ((ULONG_PTR)handle >> 16) & 0xFF;
    if (type == MF_TYPE_WMF)
        return wmf_lookup(handle) ? OBJ_METAFILE : 0;
    if (type == MF_TYPE_WMF_DC)
        return dc_of(handle) ? OBJ_METADC : 0;
    return 0;
}

/* ------------------------------------------------------------------ playback */

static SHORT s16(const METARECORD *r, UINT i)
{
    return (SHORT)r->rdParm[i];
}

static BOOL play_dib(HDC dc, const METARECORD *r, UINT params, const BITMAPINFO **info, const BYTE **bits)
{
    DWORD words = r->rdSize;
    if (words < 3 + params + sizeof(BITMAPINFOHEADER) / 2)
        return FALSE;
    const BITMAPINFO *bi = (const BITMAPINFO *)&r->rdParm[params];
    if (bi->bmiHeader.biSize < sizeof(BITMAPINFOHEADER))
        return FALSE;
    *info = bi;
    *bits = (const BYTE *)bi + bi->bmiHeader.biSize + GdiDibTableSize(&bi->bmiHeader, DIB_RGB_COLORS);
    return TRUE;
}

static void table_put(HANDLETABLE *table, UINT handles, HGDIOBJ object)
{
    for (UINT i = 0; i < handles; i++) {
        if (!table->objectHandle[i]) {
            table->objectHandle[i] = object;
            return;
        }
    }
    DeleteObject(object);
}

WINGDIAPI BOOL WINAPI PlayMetaFileRecord(HDC dc, LPHANDLETABLE table, LPMETARECORD record, UINT handles)
{
    if (!record)
        return FALSE;
    const METARECORD *r = record;
    switch (r->rdFunction) {
    case 0:
        return TRUE;
    case META_SETBKCOLOR:
        return SetBkColor(dc, MAKELONG(r->rdParm[0], r->rdParm[1])) != CLR_INVALID;
    case META_SETTEXTCOLOR:
        return SetTextColor(dc, MAKELONG(r->rdParm[0], r->rdParm[1])) != CLR_INVALID;
    case META_SETBKMODE:
        return SetBkMode(dc, r->rdParm[0]) != 0;
    case META_SETMAPMODE:
        return SetMapMode(dc, r->rdParm[0]) != 0;
    case META_SETROP2:
        return SetROP2(dc, r->rdParm[0]) != 0;
    case META_SETPOLYFILLMODE:
        return SetPolyFillMode(dc, r->rdParm[0]) != 0;
    case META_SETSTRETCHBLTMODE:
        return SetStretchBltMode(dc, r->rdParm[0]) != 0;
    case META_SETTEXTCHAREXTRA:
        SetTextCharacterExtra(dc, s16(r, 0));
        return TRUE;
    case META_SETTEXTALIGN:
        return SetTextAlign(dc, r->rdParm[0]) != GDI_ERROR;
    case META_SETMAPPERFLAGS:
        SetMapperFlags(dc, MAKELONG(r->rdParm[0], r->rdParm[1]));
        return TRUE;
    case META_SETLAYOUT:
        SetLayout(dc, MAKELONG(r->rdParm[0], r->rdParm[1]));
        return TRUE;
    case META_SETTEXTJUSTIFICATION:
        return SetTextJustification(dc, s16(r, 1), s16(r, 0));
    case META_SETWINDOWORG:
        return SetWindowOrgEx(dc, s16(r, 1), s16(r, 0), nullptr);
    case META_SETWINDOWEXT:
        return SetWindowExtEx(dc, s16(r, 1), s16(r, 0), nullptr);
    case META_SETVIEWPORTORG:
        return SetViewportOrgEx(dc, s16(r, 1), s16(r, 0), nullptr);
    case META_SETVIEWPORTEXT:
        return SetViewportExtEx(dc, s16(r, 1), s16(r, 0), nullptr);
    case META_OFFSETWINDOWORG:
        return OffsetWindowOrgEx(dc, s16(r, 1), s16(r, 0), nullptr);
    case META_OFFSETVIEWPORTORG:
        return OffsetViewportOrgEx(dc, s16(r, 1), s16(r, 0), nullptr);
    case META_SCALEWINDOWEXT:
        return ScaleWindowExtEx(dc, s16(r, 3), s16(r, 2), s16(r, 1), s16(r, 0), nullptr);
    case META_SCALEVIEWPORTEXT:
        return ScaleViewportExtEx(dc, s16(r, 3), s16(r, 2), s16(r, 1), s16(r, 0), nullptr);
    case META_SAVEDC:
        return SaveDC(dc) != 0;
    case META_RESTOREDC:
        return RestoreDC(dc, s16(r, 0));
    case META_LINETO:
        return LineTo(dc, s16(r, 1), s16(r, 0));
    case META_MOVETO:
        return MoveToEx(dc, s16(r, 1), s16(r, 0), nullptr);
    case META_RECTANGLE:
        return Rectangle(dc, s16(r, 3), s16(r, 2), s16(r, 1), s16(r, 0));
    case META_ELLIPSE:
        return Ellipse(dc, s16(r, 3), s16(r, 2), s16(r, 1), s16(r, 0));
    case META_ROUNDRECT:
        return RoundRect(dc, s16(r, 5), s16(r, 4), s16(r, 3), s16(r, 2), s16(r, 1), s16(r, 0));
    case META_ARC:
        return Arc(dc, s16(r, 7), s16(r, 6), s16(r, 5), s16(r, 4), s16(r, 3), s16(r, 2), s16(r, 1), s16(r, 0));
    case META_PIE:
        return Pie(dc, s16(r, 7), s16(r, 6), s16(r, 5), s16(r, 4), s16(r, 3), s16(r, 2), s16(r, 1), s16(r, 0));
    case META_CHORD:
        return Chord(dc, s16(r, 7), s16(r, 6), s16(r, 5), s16(r, 4), s16(r, 3), s16(r, 2), s16(r, 1), s16(r, 0));
    case META_SETPIXEL:
        return SetPixelV(dc, s16(r, 3), s16(r, 2), MAKELONG(r->rdParm[0], r->rdParm[1]));
    case META_EXTFLOODFILL:
        return ExtFloodFill(dc, s16(r, 4), s16(r, 3), MAKELONG(r->rdParm[1], r->rdParm[2]), r->rdParm[0]);
    case META_FLOODFILL:
        return ExtFloodFill(dc, s16(r, 3), s16(r, 2), MAKELONG(r->rdParm[0], r->rdParm[1]), FLOODFILLBORDER);
    case META_EXCLUDECLIPRECT:
        return ExcludeClipRect(dc, s16(r, 3), s16(r, 2), s16(r, 1), s16(r, 0)) != ERROR;
    case META_INTERSECTCLIPRECT:
        return IntersectClipRect(dc, s16(r, 3), s16(r, 2), s16(r, 1), s16(r, 0)) != ERROR;
    case META_OFFSETCLIPRGN:
        return OffsetClipRgn(dc, s16(r, 1), s16(r, 0)) != ERROR;
    case META_SELECTCLIPREGION: {
        WORD i = r->rdParm[0];
        HRGN region = i && table && i < handles ? table->objectHandle[i] : nullptr;
        return SelectClipRgn(dc, region) != ERROR;
    }
    case META_POLYGON:
    case META_POLYLINE: {
        UINT n = r->rdParm[0];
        if (r->rdSize < 4 + 2 * n)
            return FALSE;
        POINT *pts = heap_alloc((n + 1) * sizeof(POINT));
        if (!pts)
            return FALSE;
        for (UINT i = 0; i < n; i++)
            pts[i] = (POINT){s16(r, 1 + 2 * i), s16(r, 2 + 2 * i)};
        BOOL ok = r->rdFunction == META_POLYGON ? Polygon(dc, pts, (int)n) : Polyline(dc, pts, (int)n);
        heap_free(pts);
        return ok;
    }
    case META_POLYPOLYGON: {
        UINT polys = r->rdParm[0], total = 0;
        if (r->rdSize < 4 + polys)
            return FALSE;
        for (UINT i = 0; i < polys; i++)
            total += r->rdParm[1 + i];
        if (r->rdSize < 4 + polys + 2 * total)
            return FALSE;
        POINT *pts = heap_alloc((total + 1) * sizeof(POINT));
        INT *counts = heap_alloc((polys + 1) * sizeof(INT));
        BOOL ok = FALSE;
        if (pts && counts) {
            for (UINT i = 0; i < polys; i++)
                counts[i] = r->rdParm[1 + i];
            for (UINT i = 0; i < total; i++)
                pts[i] = (POINT){s16(r, 1 + polys + 2 * i), s16(r, 2 + polys + 2 * i)};
            ok = PolyPolygon(dc, pts, counts, (int)polys);
        }
        heap_free(pts);
        heap_free(counts);
        return ok;
    }
    case META_TEXTOUT: {
        UINT n = r->rdParm[0];
        if (r->rdSize < 6 + (n + 1) / 2)
            return FALSE;
        UINT at = 1 + (n + 1) / 2;
        return TextOutA(dc, s16(r, at + 1), s16(r, at), (LPCSTR)&r->rdParm[1], (int)n);
    }
    case META_EXTTEXTOUT: {
        int y = s16(r, 0), x = s16(r, 1);
        UINT n = r->rdParm[2], options = r->rdParm[3], at = 4;
        RECT rect, *prect = nullptr;
        if (options & (ETO_OPAQUE | ETO_CLIPPED)) {
            rect = (RECT){s16(r, 4), s16(r, 5), s16(r, 6), s16(r, 7)};
            prect = &rect;
            at = 8;
        }
        if (r->rdSize < 3 + at + (n + 1) / 2)
            return FALSE;
        LPCSTR text = (LPCSTR)&r->rdParm[at];
        INT *dx = nullptr;
        UINT dx_at = at + (n + 1) / 2;
        if (r->rdSize >= 3 + dx_at + n && n) {
            dx = heap_alloc(n * sizeof(INT));
            for (UINT i = 0; dx && i < n; i++)
                dx[i] = s16(r, dx_at + i);
        }
        BOOL ok = ExtTextOutA(dc, x, y, options, prect, text, n, dx);
        heap_free(dx);
        return ok;
    }
    case META_PATBLT:
        return PatBlt(dc, s16(r, 5), s16(r, 4), s16(r, 3), s16(r, 2), MAKELONG(r->rdParm[0], r->rdParm[1]));
    case META_DIBSTRETCHBLT:
    case META_DIBBITBLT: {
        BOOL stretch = r->rdFunction == META_DIBSTRETCHBLT;
        UINT params = stretch ? 10 : 8;
        DWORD rop = MAKELONG(r->rdParm[0], r->rdParm[1]);
        UINT i = 2;
        int sh = stretch ? s16(r, i++) : 0, sw = stretch ? s16(r, i++) : 0;
        int sy = s16(r, i++), sx = s16(r, i++), h = s16(r, i++), w = s16(r, i++), y = s16(r, i++), x = s16(r, i++);
        if (!stretch) {
            sw = w;
            sh = h;
        }
        const BITMAPINFO *info;
        const BYTE *bits;
        if (!play_dib(dc, r, params, &info, &bits))
            return PatBlt(dc, x, y, w, h, rop);
        LONG bh = info->bmiHeader.biHeight < 0 ? -info->bmiHeader.biHeight : info->bmiHeader.biHeight;
        /* the source rect counts from the top, StretchDIBits from the bottom of a bottom-up dib. */
        int ys = info->bmiHeader.biHeight > 0 ? (int)bh - sy - sh : sy;
        return StretchDIBits(dc, x, y, w, h, sx, ys, sw, sh, bits, info, DIB_RGB_COLORS, rop) != 0;
    }
    case META_STRETCHDIB: {
        DWORD rop = MAKELONG(r->rdParm[0], r->rdParm[1]);
        UINT usage = r->rdParm[2];
        int sh = s16(r, 3), sw = s16(r, 4), sy = s16(r, 5), sx = s16(r, 6), h = s16(r, 7), w = s16(r, 8),
            y = s16(r, 9), x = s16(r, 10);
        const BITMAPINFO *info;
        const BYTE *bits;
        if (!play_dib(dc, r, 11, &info, &bits))
            return PatBlt(dc, x, y, w, h, rop);
        bits = (const BYTE *)info + info->bmiHeader.biSize + GdiDibTableSize(&info->bmiHeader, usage);
        return StretchDIBits(dc, x, y, w, h, sx, sy, sw, sh, bits, info, usage, rop) != 0;
    }
    case META_SETDIBTODEV: {
        UINT usage = r->rdParm[0], lines = r->rdParm[1], start = r->rdParm[2];
        int sy = s16(r, 3), sx = s16(r, 4), h = s16(r, 5), w = s16(r, 6), y = s16(r, 7), x = s16(r, 8);
        const BITMAPINFO *info;
        const BYTE *bits;
        if (!play_dib(dc, r, 9, &info, &bits))
            return FALSE;
        bits = (const BYTE *)info + info->bmiHeader.biSize + GdiDibTableSize(&info->bmiHeader, usage);
        return SetDIBitsToDevice(dc, x, y, (DWORD)w, (DWORD)h, sx, sy, start, lines, bits, info, usage) != 0;
    }
    case META_CREATEPENINDIRECT: {
        LOGPEN lp = {r->rdParm[0], {s16(r, 1), 0}, MAKELONG(r->rdParm[3], r->rdParm[4])};
        HPEN pen = CreatePenIndirect(&lp);
        if (pen && table)
            table_put(table, handles, pen);
        return pen != nullptr;
    }
    case META_CREATEBRUSHINDIRECT: {
        LOGBRUSH lb = {r->rdParm[0], MAKELONG(r->rdParm[1], r->rdParm[2]), r->rdParm[3]};
        HBRUSH brush = lb.lbStyle == BS_HATCHED ? CreateHatchBrush((int)lb.lbHatch, lb.lbColor)
                                                : CreateBrushIndirect(&lb);
        if (brush && table)
            table_put(table, handles, brush);
        return brush != nullptr;
    }
    case META_DIBCREATEPATTERNBRUSH: {
        if (r->rdSize < 5 + sizeof(BITMAPINFOHEADER) / 2)
            return FALSE;
        HBRUSH brush = CreateDIBPatternBrushPt(&r->rdParm[2], r->rdParm[1]);
        if (brush && table)
            table_put(table, handles, brush);
        return brush != nullptr;
    }
    case META_CREATEFONTINDIRECT: {
        const BYTE *p = (const BYTE *)r->rdParm;
        LOGFONTA lf;
        memset(&lf, 0, sizeof(lf));
        lf.lfHeight = (SHORT)(p[0] | p[1] << 8);
        lf.lfWidth = (SHORT)(p[2] | p[3] << 8);
        lf.lfEscapement = (SHORT)(p[4] | p[5] << 8);
        lf.lfOrientation = (SHORT)(p[6] | p[7] << 8);
        lf.lfWeight = (SHORT)(p[8] | p[9] << 8);
        memcpy(&lf.lfItalic, p + 10, 8);
        UINT room = r->rdSize * 2 - 6 > 18 ? r->rdSize * 2 - 6 - 18 : 0;
        memcpy(lf.lfFaceName, p + 18, room < LF_FACESIZE ? room : LF_FACESIZE);
        lf.lfFaceName[LF_FACESIZE - 1] = 0;
        HFONT font = CreateFontIndirectA(&lf);
        if (font && table)
            table_put(table, handles, font);
        return font != nullptr;
    }
    case META_CREATEPALETTE: {
        UINT n = r->rdParm[1];
        LOGPALETTE *lp = heap_alloc(sizeof(LOGPALETTE) + n * sizeof(PALETTEENTRY));
        if (!lp)
            return FALSE;
        lp->palVersion = 0x300;
        lp->palNumEntries = (WORD)n;
        memcpy(lp->palPalEntry, &r->rdParm[2], n * sizeof(PALETTEENTRY));
        HPALETTE palette = CreatePalette(lp);
        heap_free(lp);
        if (palette && table)
            table_put(table, handles, palette);
        return palette != nullptr;
    }
    case META_CREATEREGION:
    case META_CREATEPATTERNBRUSH:
        /* a failed object still takes its slot so later indices stay right. */
        if (table)
            table_put(table, handles, CreateRectRgn(0, 0, 0, 0));
        return TRUE;
    case META_SELECTOBJECT:
    case META_SELECTPALETTE: {
        WORD i = r->rdParm[0];
        if (!table || i >= handles || !table->objectHandle[i])
            return FALSE;
        if (r->rdFunction == META_SELECTPALETTE)
            return SelectPalette(dc, table->objectHandle[i], FALSE) != nullptr;
        return SelectObject(dc, table->objectHandle[i]) != nullptr;
    }
    case META_DELETEOBJECT: {
        WORD i = r->rdParm[0];
        if (!table || i >= handles || !table->objectHandle[i])
            return FALSE;
        DeleteObject(table->objectHandle[i]);
        table->objectHandle[i] = nullptr;
        return TRUE;
    }
    case META_REALIZEPALETTE:
        RealizePalette(dc);
        return TRUE;
    case META_ESCAPE:
        return TRUE;
    }
    return TRUE;
}

/* the objects are deleted after playback. */
static BOOL play(HDC dc, HMETAFILE handle, MFENUMPROC proc, LPARAM param)
{
    WMF *w = wmf_lookup(handle);
    if (!w) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    const METAHEADER *h = (const METAHEADER *)w->Data;
    UINT handles = h->mtNoObjects;
    HANDLETABLE *table = heap_zalloc((handles + 1) * sizeof(HGDIOBJ));
    if (!table)
        return FALSE;
    HGDIOBJ pen = GetCurrentObject(dc, OBJ_PEN), brush = GetCurrentObject(dc, OBJ_BRUSH);
    HGDIOBJ font = GetCurrentObject(dc, OBJ_FONT);
    BOOL ok = TRUE;
    DWORD offset = h->mtHeaderSize * 2;
    while (offset + 6 <= w->Size) {
        METARECORD *r = (METARECORD *)(w->Data + offset);
        if (r->rdSize < 3 || r->rdSize > (w->Size - offset) / 2)
            break;
        if (!r->rdFunction)
            break;
        if (proc) {
            if (!proc(dc, table, r, (int)handles, param)) {
                ok = FALSE;
                break;
            }
        } else if (!PlayMetaFileRecord(dc, table, r, handles)) {
            ok = FALSE;
        }
        offset += r->rdSize * 2;
    }
    /* delete the metafile's objects and give the dc its own back. */
    if (pen)
        SelectObject(dc, pen);
    if (brush)
        SelectObject(dc, brush);
    if (font)
        SelectObject(dc, font);
    for (UINT i = 0; i < handles; i++)
        if (table->objectHandle[i])
            DeleteObject(table->objectHandle[i]);
    heap_free(table);
    return ok;
}

WINGDIAPI BOOL WINAPI PlayMetaFile(HDC dc, HMETAFILE handle)
{
    return play(dc, handle, nullptr, 0);
}

WINGDIAPI BOOL WINAPI EnumMetaFile(HDC dc, HMETAFILE handle, MFENUMPROC proc, LPARAM param)
{
    if (!proc) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return play(dc, handle, proc, param);
}

/* ------------------------------------------------------------------ conversions */

/*
 * an enhanced metafile embedded in a windows metafile: MFCOMMENT escapes
 * starting with 'WMFC', each holding a piece. the checksum makes the 16-bit
 * words of the whole file sum to zero.
 */
#define WMFC_IDENTIFIER 0x43464D57u
#define WMFC_CHUNK 0x2000u
#define WMFC_HEAD 34u

static WORD word_sum(const BYTE *data, DWORD size)
{
    WORD sum = 0;
    for (DWORD i = 0; i + 1 < size; i += 2)
        sum = (WORD)(sum + (data[i] | data[i + 1] << 8));
    return sum;
}

static HENHMETAFILE embedded_emf(const BYTE *data, DWORD size)
{
    if (size < 18 + 44 || word_sum(data, size & ~1u))
        return nullptr;
    DWORD offset = 18, total = 0, got = 0;
    BYTE *emf = nullptr;
    while (offset + 44 <= size) {
        const BYTE *r = data + offset;
        DWORD words;
        memcpy(&words, r, 4);
        DWORD ident, type, flags, chunk, whole;
        memcpy(&ident, r + 10, 4);
        memcpy(&type, r + 14, 4);
        memcpy(&flags, r + 24, 4);
        memcpy(&chunk, r + 32, 4);
        memcpy(&whole, r + 40, 4);
        WORD function = (WORD)(r[4] | r[5] << 8), escape = (WORD)(r[6] | r[7] << 8);
        if (function != META_ESCAPE || escape != MFCOMMENT || ident != WMFC_IDENTIFIER || type != 1 || flags ||
            words <= 22 || words * 2 > size - offset || chunk > words * 2 - 44)
            break;
        if (!emf) {
            if (whole < 88 || whole > 0x10000000)
                break;
            total = whole;
            emf = heap_alloc(total);
            if (!emf)
                break;
        }
        if (chunk > total - got)
            break;
        memcpy(emf + got, r + 44, chunk);
        got += chunk;
        offset += words * 2;
    }
    HENHMETAFILE h = nullptr;
    if (emf && got == total)
        h = SetEnhMetaFileBits(total, emf);
    heap_free(emf);
    return h;
}

/* the comment's checksum makes the dwords of the whole metafile sum to zero. */
static DWORD dword_sum(const BYTE *data, DWORD size)
{
    DWORD sum = 0;
    for (DWORD i = 0; i + 3 < size; i += 4) {
        DWORD v;
        memcpy(&v, data + i, 4);
        sum += v;
    }
    return sum;
}

WINGDIAPI HENHMETAFILE WINAPI SetWinMetaFileBits(UINT size, const BYTE *data, HDC ref, const METAFILEPICT *picture)
{
    if (!header_valid(data, size)) {
        SetLastError(ERROR_INVALID_DATA);
        return nullptr;
    }
    HENHMETAFILE inner = embedded_emf(data, size);
    if (inner)
        return inner;
    HMETAFILE wmf = SetMetaFileBitsEx(size, data);
    if (!wmf)
        return nullptr;
    int mode = picture ? (picture->mm ? (int)picture->mm : MM_ANISOTROPIC) : MM_ANISOTROPIC;
    if (mode < MM_TEXT || mode > MM_ANISOTROPIC) {
        DeleteMetaFile(wmf);
        return nullptr;
    }
    HDC screen = nullptr;
    if (!ref)
        ref = screen = CreateDCW(L"DISPLAY", nullptr, nullptr, nullptr);
    RECT frame, *pframe = nullptr;
    int px = 0, py = 0;
    if (mode >= MM_ISOTROPIC && picture && picture->xExt > 0 && picture->yExt > 0) {
        /* xExt and yExt are in hundredths of a millimeter. */
        px = MulDiv(picture->xExt, GetDeviceCaps(ref, DESKTOPHORZRES), 100 * GetDeviceCaps(ref, HORZSIZE));
        py = MulDiv(picture->yExt, GetDeviceCaps(ref, DESKTOPVERTRES), 100 * GetDeviceCaps(ref, VERTSIZE));
        frame = (RECT){0, 0, picture->xExt, picture->yExt};
        pframe = &frame;
    }
    HENHMETAFILE result = nullptr;
    HDC dc = CreateEnhMetaFileW(ref, nullptr, pframe, nullptr);
    if (dc) {
        BOOL ok = TRUE;
        if (mode == MM_ANISOTROPIC) {
            /* embed the original so GetWinMetaFileBits can return it. */
            DWORD head[6] = {GDICOMMENT_IDENTIFIER, GDICOMMENT_WINDOWS_METAFILE, 0x300, 0, 0, size};
            /* windows reserves this public comment, so it bypasses GdiComment's filter. */
            EMF_DC *rec = EmfDc(dc);
            EMRGDICOMMENT *r = rec ? EmfRecordAlloc(rec, EMR_GDICOMMENT, offsetof(EMRGDICOMMENT, Data) + sizeof(head) + size)
                                   : nullptr;
            ok = r != nullptr;
            if (ok) {
                r->cbData = sizeof(head) + size;
                memcpy(r->Data, head, sizeof(head));
                memcpy(r->Data + sizeof(head), data, size);
            }
        }
        if (ok && SetMapMode(dc, mode) && (!px || (SetViewportExtEx(dc, px, py, nullptr) &&
                                                     SetWindowExtEx(dc, px, py, nullptr))))
            PlayMetaFile(dc, wmf);
        result = CloseEnhMetaFile(dc);
        EMF *emf = result ? EmfLookup(result) : nullptr;
        if (emf && mode == MM_ANISOTROPIC) {
            /* the comment follows the header, its checksum zeroes the dword sum. */
            DWORD at = ((ENHMETAHEADER *)emf->Data)->nSize;
            DWORD *checksum = (DWORD *)(emf->Data + at + 24);
            *checksum = 0;
            *checksum = 0u - dword_sum(emf->Data, emf->Size);
        }
    }
    if (screen)
        DeleteDC(screen);
    DeleteMetaFile(wmf);
    return result;
}

/* returns the windows metafile embedded by SetWinMetaFileBits, only for MM_ANISOTROPIC. */
static UINT original_wmf(const EMF *emf, UINT size, LPBYTE data, INT mode)
{
    const ENHMETAHEADER *h = (const ENHMETAHEADER *)emf->Data;
    if (mode != MM_ANISOTROPIC || h->nSize + 36 > emf->Size)
        return 0;
    const DWORD *c = (const DWORD *)(emf->Data + h->nSize);
    if (c[0] != EMR_GDICOMMENT || c[3] != GDICOMMENT_IDENTIFIER || c[4] != GDICOMMENT_WINDOWS_METAFILE ||
        (c[5] != 0x100 && c[5] != 0x300) || c[7] || dword_sum(emf->Data, emf->Size))
        return 0;
    DWORD bytes = c[8];
    if (bytes > emf->Size - h->nSize - 36)
        return 0;
    if (data) {
        if (size < bytes) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return 0;
        }
        memcpy(data, c + 9, bytes);
    }
    return bytes;
}

static BOOL push(BYTE **buffer, DWORD *used, DWORD *cap, const void *data, DWORD size)
{
    if (*used + size > *cap) {
        DWORD n = *cap ? *cap : 0x4000;
        while (n < *used + size)
            n *= 2;
        BYTE *b = heap_realloc(*buffer, n);
        if (!b)
            return FALSE;
        *buffer = b;
        *cap = n;
    }
    memcpy(*buffer + *used, data, size);
    *used += size;
    return TRUE;
}

static BOOL push_record(BYTE **buffer, DWORD *used, DWORD *cap, WORD function, const WORD *params, UINT count,
                        DWORD *max)
{
    DWORD words = 3 + count;
    WORD head[3] = {(WORD)words, (WORD)(words >> 16), function};
    if (words > *max)
        *max = words;
    return push(buffer, used, cap, head, 6) && (!count || push(buffer, used, cap, params, count * 2));
}

/*
 * windows converts record by record. mime writes one stretched dib of the
 * rendered picture in the mode's coordinates, preceded by the metafile in
 * comments so SetWinMetaFileBits gets it back whole.
 */
static UINT convert(const EMF *emf, HENHMETAFILE handle, UINT size, LPBYTE data, INT mode, HDC ref)
{
    const ENHMETAHEADER *h = (const ENHMETAHEADER *)emf->Data;
    BYTE *out = nullptr;
    DWORD used = 0, cap = 0, max = 0;
    METAHEADER mh;
    memset(&mh, 0, sizeof(mh));
    BOOL ok = push(&out, &used, &cap, &mh, sizeof(mh));
    DWORD sent = 0, chunks = (emf->Size + WMFC_CHUNK - 1) / WMFC_CHUNK;
    DWORD checksum_at = 0;
    for (DWORD i = 0; ok && i < chunks; i++) {
        DWORD chunk = emf->Size - sent < WMFC_CHUNK ? emf->Size - sent : WMFC_CHUNK;
        DWORD bytes = WMFC_HEAD + chunk;
        DWORD words = 3 + 2 + (bytes + 1) / 2;
        BYTE head[44];
        memset(head, 0, sizeof(head));
        memcpy(head, &words, 4);
        WORD function = META_ESCAPE, escape = MFCOMMENT, count = (WORD)bytes;
        DWORD ident = WMFC_IDENTIFIER, type = 1, version = 0x10000, remaining = emf->Size - sent - chunk;
        memcpy(head + 4, &function, 2);
        memcpy(head + 6, &escape, 2);
        memcpy(head + 8, &count, 2);
        memcpy(head + 10, &ident, 4);
        memcpy(head + 14, &type, 4);
        memcpy(head + 18, &version, 4);
        memcpy(head + 28, &chunks, 4);
        memcpy(head + 32, &chunk, 4);
        memcpy(head + 36, &remaining, 4);
        memcpy(head + 40, &emf->Size, 4);
        if (!i)
            checksum_at = used + 22;
        ok = push(&out, &used, &cap, head, sizeof(head)) && push(&out, &used, &cap, emf->Data + sent, chunk);
        if (ok && (chunk & 1)) {
            BYTE zero = 0;
            ok = push(&out, &used, &cap, &zero, 1);
        }
        if (words > max)
            max = words;
        sent += chunk;
    }
    /* picture size in mode units: hundredths of millimeters for the scaling modes. */
    LONG fw = h->rclFrame.right - h->rclFrame.left, fh = h->rclFrame.bottom - h->rclFrame.top;
    LONG pw = MulDiv(fw, h->szlDevice.cx, 100 * h->szlMillimeters.cx);
    LONG ph = MulDiv(fh, h->szlDevice.cy, 100 * h->szlMillimeters.cy);
    if (pw <= 0)
        pw = 1;
    if (ph <= 0)
        ph = 1;
    if (pw > 4096)
        pw = 4096;
    if (ph > 4096)
        ph = 4096;
    LONG lw, lh;
    switch (mode) {
    case MM_TEXT:
        lw = pw;
        lh = ph;
        break;
    case MM_LOMETRIC:
        lw = fw / 10;
        lh = -fh / 10;
        break;
    case MM_HIMETRIC:
        lw = fw;
        lh = -fh;
        break;
    case MM_LOENGLISH:
        lw = MulDiv(fw, 100, 2540);
        lh = -MulDiv(fh, 100, 2540);
        break;
    case MM_HIENGLISH:
        lw = MulDiv(fw, 1000, 2540);
        lh = -MulDiv(fh, 1000, 2540);
        break;
    case MM_TWIPS:
        lw = MulDiv(fw, 1440, 2540);
        lh = -MulDiv(fh, 1440, 2540);
        break;
    default:
        lw = fw;
        lh = fh;
        break;
    }
    WORD mm = (WORD)mode;
    ok = ok && push_record(&out, &used, &cap, META_SETMAPMODE, &mm, 1, &max);
    if (mode >= MM_ISOTROPIC) {
        WORD org[2] = {0, 0}, ext[2] = {(WORD)lh, (WORD)lw};
        ok = ok && push_record(&out, &used, &cap, META_SETWINDOWORG, org, 2, &max) &&
             push_record(&out, &used, &cap, META_SETWINDOWEXT, ext, 2, &max);
    }
    /* render to a 24 bpp dib on white. */
    DWORD stride = ((DWORD)pw * 3 + 3) & ~3u, bits_size = stride * (DWORD)ph;
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = pw;
    bi.bmiHeader.biHeight = ph;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biSizeImage = bits_size;
    HDC mem = CreateCompatibleDC(ref);
    void *pixels = nullptr;
    HBITMAP dib = mem ? CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &pixels, nullptr, 0) : nullptr;
    if (ok && dib && pixels) {
        HGDIOBJ old = SelectObject(mem, dib);
        PatBlt(mem, 0, 0, pw, ph, WHITENESS);
        RECT rect = {0, 0, pw, ph};
        PlayEnhMetaFile(mem, handle, &rect);
        GdiFlush();
        SelectObject(mem, old);
        WORD p[11] = {(WORD)(SRCCOPY & 0xFFFF), (WORD)(SRCCOPY >> 16), DIB_RGB_COLORS, (WORD)ph, (WORD)pw, 0, 0,
                      (WORD)lh, (WORD)lw, 0, 0};
        DWORD words = 3 + 11 + (sizeof(BITMAPINFOHEADER) + bits_size + 1) / 2;
        WORD head[3] = {(WORD)words, (WORD)(words >> 16), META_STRETCHDIB};
        if (words > max)
            max = words;
        ok = push(&out, &used, &cap, head, 6) && push(&out, &used, &cap, p, sizeof(p)) &&
             push(&out, &used, &cap, &bi.bmiHeader, sizeof(BITMAPINFOHEADER)) &&
             push(&out, &used, &cap, pixels, bits_size);
    }
    if (dib)
        DeleteObject(dib);
    if (mem)
        DeleteDC(mem);
    ok = ok && push_record(&out, &used, &cap, 0, nullptr, 0, &max);
    UINT result = 0;
    if (ok) {
        METAHEADER *mhp = (METAHEADER *)out;
        mhp->mtType = 1;
        mhp->mtHeaderSize = WMF_HEADER_WORDS;
        mhp->mtVersion = 0x300;
        mhp->mtSize = used / 2;
        mhp->mtMaxRecord = max;
        if (checksum_at) {
            WORD zero = 0;
            memcpy(out + checksum_at, &zero, 2);
            WORD sum = (WORD)(0u - word_sum(out, used));
            memcpy(out + checksum_at, &sum, 2);
        }
        result = used;
        if (data) {
            if (size < used) {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                result = 0;
            } else {
                memcpy(data, out, used);
            }
        }
    }
    heap_free(out);
    return result;
}

WINGDIAPI UINT WINAPI GetWinMetaFileBits(HENHMETAFILE handle, UINT size, LPBYTE data, INT mode, HDC ref)
{
    if (mode < MM_TEXT || mode > MM_ANISOTROPIC || (((ULONG_PTR)handle >> 16) & 0xFF) != MF_TYPE_EMF) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    EMF *emf = EmfLookup(handle);
    if (!emf)
        return 0;
    UINT n = original_wmf(emf, size, data, mode);
    if (n)
        return n;
    return convert(emf, handle, size, data, mode, ref);
}
