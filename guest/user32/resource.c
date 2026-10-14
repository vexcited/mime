/*
 * resources: string tables, icons and cursors. the system resources of
 * user32 live in SystemResources\user32.dll.mun and en-US\user32.dll.mui,
 * read straight from the files.
 */
#include "user32_private.h"
#include <png/png.h>

/* ------------------------------------------------------------------ raw pe resource files */

/*
 * the files are read on demand: imageres.dll.mun is tens of megabytes and
 * only a few of its resources are ever used. found resources are cached for
 * the life of the process because callers keep pointers to them.
 */
typedef struct USER_RESDATA {
    struct USER_RESDATA *Next;
    ULONG Offset;
    DWORD Size;
    UCHAR Data[];
} USER_RESDATA;

struct USER_RESFILE {
    HANDLE File;
    ULONG64 Size;
    ULONG ResourceOffset; /* file offset of the resource directory */
    ULONG Sections;
    IMAGE_SECTION_HEADER *Section;
    RTL_CRITICAL_SECTION Lock;
    USER_RESDATA *Cache;
};

static BOOL read_at(USER_RESFILE *f, ULONG64 offset, void *buffer, DWORD length)
{
    if (offset > f->Size || length > f->Size - offset)
        return FALSE;
    OVERLAPPED ov = {0};
    ov.Offset = (DWORD)offset;
    ov.OffsetHigh = (DWORD)(offset >> 32);
    DWORD read = 0;
    return ReadFile(f->File, buffer, length, &read, &ov) && read == length;
}

static ULONG rva_to_offset(USER_RESFILE *f, ULONG rva)
{
    for (ULONG i = 0; i < f->Sections; i++) {
        const IMAGE_SECTION_HEADER *s = &f->Section[i];
        ULONG size = s->Misc.VirtualSize > s->SizeOfRawData ? s->Misc.VirtualSize : s->SizeOfRawData;
        if (rva >= s->VirtualAddress && rva < s->VirtualAddress + size)
            return rva - s->VirtualAddress + s->PointerToRawData;
    }
    return 0;
}

USER_RESFILE *UserResOpen(PCWSTR path)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;
    USER_RESFILE *f = UserAlloc(sizeof(*f));
    if (!f) {
        CloseHandle(h);
        return NULL;
    }
    memset(f, 0, sizeof(*f));
    f->File = h;
    DWORD high = 0;
    DWORD low = GetFileSize(h, &high);
    f->Size = low == INVALID_FILE_SIZE ? 0 : ((ULONG64)high << 32) | low;

    /* .mun files are resource only images and may use either optional header kind. */
    IMAGE_DOS_HEADER dos;
    struct {
        DWORD Signature;
        IMAGE_FILE_HEADER FileHeader;
        WORD Magic;
    } nt;
    if (read_at(f, 0, &dos, sizeof(dos)) && dos.e_magic == IMAGE_DOS_SIGNATURE &&
        read_at(f, (ULONG)dos.e_lfanew, &nt, sizeof(nt)) && nt.Signature == IMAGE_NT_SIGNATURE &&
        nt.FileHeader.NumberOfSections <= 96) {
        ULONG64 optional = (ULONG)dos.e_lfanew + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
        ULONG directories = nt.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC ? 112 : nt.Magic == 0x10b ? 96 : 0;
        IMAGE_DATA_DIRECTORY dir = {0};
        if (directories && nt.FileHeader.SizeOfOptionalHeader >= directories + 3 * sizeof(dir))
            read_at(f, optional + directories + IMAGE_DIRECTORY_ENTRY_RESOURCE * sizeof(dir), &dir, sizeof(dir));
        ULONG count = nt.FileHeader.NumberOfSections;
        f->Section = dir.VirtualAddress && count ? UserAlloc(count * sizeof(IMAGE_SECTION_HEADER)) : NULL;
        if (f->Section &&
            read_at(f, optional + nt.FileHeader.SizeOfOptionalHeader, f->Section, count * sizeof(IMAGE_SECTION_HEADER))) {
            f->Sections = count;
            f->ResourceOffset = rva_to_offset(f, dir.VirtualAddress);
        }
    }
    if (!f->ResourceOffset) {
        UserFree(f->Section);
        UserFree(f);
        CloseHandle(h);
        return NULL;
    }
    RtlInitializeCriticalSection(&f->Lock);
    return f;
}

/* returns the OffsetToData of the matching entry, or 0. */
static ULONG find_entry(USER_RESFILE *f, ULONG dir_offset, LPCWSTR name)
{
    IMAGE_RESOURCE_DIRECTORY dir;
    if (!read_at(f, dir_offset, &dir, sizeof(dir)))
        return 0;
    ULONG count = (ULONG)dir.NumberOfNamedEntries + dir.NumberOfIdEntries;
    if (!count)
        return 0;
    IMAGE_RESOURCE_DIRECTORY_ENTRY *e = UserAlloc(count * sizeof(*e));
    if (!e || !read_at(f, dir_offset + sizeof(dir), e, count * sizeof(*e))) {
        UserFree(e);
        return 0;
    }
    ULONG found = 0;
    if (!name) {
        found = e[0].OffsetToData;
    } else {
        SIZE_T name_len = IS_INTRESOURCE(name) ? 0 : wcslen(name);
        for (ULONG i = 0; i < count && !found; i++) {
            if (IS_INTRESOURCE(name)) {
                if (!(e[i].Name & 0x80000000u) && e[i].Name == (ULONG)(ULONG_PTR)name)
                    found = e[i].OffsetToData;
            } else if (e[i].Name & 0x80000000u) {
                ULONG off = f->ResourceOffset + (e[i].Name & 0x7FFFFFFFu);
                WORD len;
                WCHAR text[64];
                if (read_at(f, off, &len, sizeof(len)) && len == name_len && len <= ARRAYSIZE(text) &&
                    read_at(f, off + 2, text, len * sizeof(WCHAR)) && !_wcsnicmp(text, name, len))
                    found = e[i].OffsetToData;
            }
        }
    }
    UserFree(e);
    return found;
}

const void *UserResFind(USER_RESFILE *f, LPCWSTR type, LPCWSTR name, WORD lang, DWORD *size)
{
    if (!f)
        return NULL;
    ULONG t = find_entry(f, f->ResourceOffset, type);
    if (!(t & 0x80000000u))
        return NULL;
    ULONG n = find_entry(f, f->ResourceOffset + (t & 0x7FFFFFFFu), name);
    if (!(n & 0x80000000u))
        return NULL;
    ULONG lang_dir = f->ResourceOffset + (n & 0x7FFFFFFFu);
    ULONG l = lang ? find_entry(f, lang_dir, (LPCWSTR)(ULONG_PTR)lang) : 0;
    if (!l)
        l = find_entry(f, lang_dir, NULL);
    if (!l || (l & 0x80000000u))
        return NULL;
    IMAGE_RESOURCE_DATA_ENTRY d;
    if (!read_at(f, f->ResourceOffset + l, &d, sizeof(d)))
        return NULL;
    ULONG off = rva_to_offset(f, d.OffsetToData);
    if (!off)
        return NULL;

    RtlEnterCriticalSection(&f->Lock);
    USER_RESDATA *r = f->Cache;
    while (r && r->Offset != off)
        r = r->Next;
    if (!r) {
        r = UserAlloc(sizeof(*r) + d.Size);
        if (r && read_at(f, off, r->Data, d.Size)) {
            r->Offset = off;
            r->Size = d.Size;
            r->Next = f->Cache;
            f->Cache = r;
        } else {
            UserFree(r);
            r = NULL;
        }
    }
    RtlLeaveCriticalSection(&f->Lock);
    if (!r)
        return NULL;
    if (size)
        *size = r->Size;
    return r->Data;
}

static USER_RESFILE *open_cached(USER_RESFILE **slot, BOOL *tried, PCWSTR relative)
{
    if (!*tried) {
        *tried = TRUE;
        WCHAR path[MAX_PATH];
        UINT n = GetWindowsDirectoryW(path, MAX_PATH);
        if (n && n + wcslen(relative) < MAX_PATH) {
            wcscpy(path + n, relative);
            *slot = UserResOpen(path);
        }
    }
    return *slot;
}

USER_RESFILE *UserSystemResources(void)
{
    static USER_RESFILE *file;
    static BOOL tried;
    return open_cached(&file, &tried, L"\\SystemResources\\user32.dll.mun");
}

USER_RESFILE *UserImageResources(void)
{
    static USER_RESFILE *file;
    static BOOL tried;
    return open_cached(&file, &tried, L"\\SystemResources\\imageres.dll.mun");
}

USER_RESFILE *UserStringResources(void)
{
    static USER_RESFILE *file;
    static BOOL tried;
    return open_cached(&file, &tried, L"\\System32\\en-US\\user32.dll.mui");
}

/* ------------------------------------------------------------------ strings */

static int copy_string_from_table(const WORD *table, DWORD size, UINT id, LPWSTR buffer, int max)
{
    const WORD *p = table, *end = (const WORD *)((const UCHAR *)table + size);
    for (UINT i = 0; i < (id & 15) && p < end; i++)
        p += 1 + *p;
    if (p >= end)
        return 0;
    int len = *p;
    if (max == 0) {
        /* LoadString with a zero buffer size returns a read only pointer to the resource. */
        if (buffer)
            *(const WCHAR **)buffer = (const WCHAR *)(p + 1);
        return len;
    }
    if (!buffer)
        return 0;
    int n = len < max - 1 ? len : max - 1;
    memcpy(buffer, p + 1, (SIZE_T)n * sizeof(WCHAR));
    buffer[n] = 0;
    return n;
}

int UserLoadSystemString(UINT id, LPWSTR buffer, int max)
{
    DWORD size;
    const WORD *table = UserResFind(UserStringResources(), RTW_STRING, MAKEINTRESOURCEW((id >> 4) + 1), 0, &size);
    if (!table)
        table = UserResFind(UserSystemResources(), RTW_STRING, MAKEINTRESOURCEW((id >> 4) + 1), 0, &size);
    if (!table) {
        if (buffer && max > 0)
            buffer[0] = 0;
        return 0;
    }
    return copy_string_from_table(table, size, id, buffer, max);
}

WINUSERAPI int WINAPI LoadStringW(HINSTANCE instance, UINT id, LPWSTR buffer, int max)
{
    if (!instance || instance == User32Instance)
        return UserLoadSystemString(id, buffer, max);
    HRSRC res = FindResourceExW(instance, RTW_STRING, MAKEINTRESOURCEW((id >> 4) + 1), 0);
    if (!res) {
        if (buffer && max > 0)
            buffer[0] = 0;
        return 0;
    }
    const WORD *table = LockResource(LoadResource(instance, res));
    if (!table)
        return 0;
    return copy_string_from_table(table, SizeofResource(instance, res), id, buffer, max);
}

WINUSERAPI int WINAPI LoadStringA(HINSTANCE instance, UINT id, LPSTR buffer, int max)
{
    WCHAR w[1024];
    int n = LoadStringW(instance, id, w, 1024);
    if (!buffer || max <= 0)
        return 0;
    int a = WideCharToMultiByte(CP_ACP, 0, w, n, buffer, max - 1, NULL, NULL);
    buffer[a] = 0;
    return a;
}

/* ------------------------------------------------------------------ icons */

#pragma pack(push, 2)
typedef struct GRPICONDIRENTRY {
    BYTE Width, Height, ColorCount, Reserved;
    WORD Planes, BitCount;
    DWORD BytesInRes;
    WORD Id;
} GRPICONDIRENTRY;
typedef struct GRPICONDIR {
    WORD Reserved, Type, Count;
    GRPICONDIRENTRY Entries[1];
} GRPICONDIR;
#pragma pack(pop)

typedef struct ICON_IMAGE {
    int Width, Height;
    PULONG Pixels; /* premultiplied bgra, top down */
} ICON_IMAGE;

static void *png_alloc(void *ctx, size_t size)
{
    return UserAlloc(size);
}

static void png_free(void *ctx, void *p)
{
    UserFree(p);
}

static const MimeAllocator g_png_alloc = {png_alloc, png_free, NULL};

/* decodes an RTW_ICON or RTW_CURSOR image: a png or a dib with its AND mask. */
static BOOL decode_icon_image(const UCHAR *data, DWORD size, ICON_IMAGE *out)
{
    if (mime_png_is_png(data, size)) {
        MimePngInfo info;
        uint8_t *pixels;
        if (mime_png_decode_alloc(data, size, &g_png_alloc, MIME_PNG_PREMULTIPLIED, &pixels, &info) != MIME_PNG_OK)
            return FALSE;
        out->Width = (int)info.width;
        out->Height = (int)info.height;
        out->Pixels = (PULONG)pixels;
        return TRUE;
    }
    if (size < sizeof(BITMAPINFOHEADER))
        return FALSE;
    const BITMAPINFOHEADER *bih = (const BITMAPINFOHEADER *)data;
    int w = bih->biWidth, h = bih->biHeight / 2, bpp = bih->biBitCount;
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024 || (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32))
        return FALSE;
    ULONG colors = bpp <= 8 ? (bih->biClrUsed ? bih->biClrUsed : 1u << bpp) : 0;
    const RGBQUAD *palette = (const RGBQUAD *)(data + bih->biSize);
    const UCHAR *xor_bits = (const UCHAR *)(palette + colors);
    ULONG xor_stride = (ULONG)((w * bpp + 31) / 32 * 4);
    ULONG and_stride = (ULONG)((w + 31) / 32 * 4);
    const UCHAR *and_bits = xor_bits + xor_stride * (ULONG)h;
    if ((SIZE_T)(and_bits - data) + and_stride * (ULONG)h > size)
        and_bits = NULL;
    PULONG px = UserAlloc((SIZE_T)w * (SIZE_T)h * 4);
    if (!px)
        return FALSE;
    BOOL has_alpha = FALSE;
    if (bpp == 32)
        for (int y = 0; y < h && !has_alpha; y++)
            for (int x = 0; x < w; x++)
                if (xor_bits[(ULONG)y * xor_stride + (ULONG)x * 4 + 3]) {
                    has_alpha = TRUE;
                    break;
                }
    for (int y = 0; y < h; y++) {
        const UCHAR *row = xor_bits + (ULONG)(h - 1 - y) * xor_stride;
        const UCHAR *mask = and_bits ? and_bits + (ULONG)(h - 1 - y) * and_stride : NULL;
        for (int x = 0; x < w; x++) {
            ULONG b, g, r, a = 255;
            switch (bpp) {
            case 32:
                b = row[x * 4], g = row[x * 4 + 1], r = row[x * 4 + 2];
                if (has_alpha)
                    a = row[x * 4 + 3];
                break;
            case 24:
                b = row[x * 3], g = row[x * 3 + 1], r = row[x * 3 + 2];
                break;
            default: {
                ULONG index;
                if (bpp == 8)
                    index = row[x];
                else if (bpp == 4)
                    index = (row[x / 2] >> ((x & 1) ? 0 : 4)) & 15;
                else
                    index = (row[x / 8] >> (7 - (x & 7))) & 1;
                RGBQUAD q = index < colors ? palette[index] : (RGBQUAD){0, 0, 0, 0};
                b = q.rgbBlue, g = q.rgbGreen, r = q.rgbRed;
                break;
            }
            }
            if (!has_alpha && mask && (mask[x / 8] & (0x80 >> (x & 7))))
                a = 0;
            px[(ULONG)y * (ULONG)w + (ULONG)x] = (a << 24) | ((r * a / 255) << 16) | ((g * a / 255) << 8) | (b * a / 255);
        }
    }
    out->Width = w;
    out->Height = h;
    out->Pixels = px;
    return TRUE;
}

/* wraps premultiplied pixels into a new icon or cursor object. */
static HICON create_icon_object(const ICON_IMAGE *img, BOOL icon, POINT hotspot, ULONG flags)
{
    BITMAPINFO bi;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = img->Width;
    bi.bmiHeader.biHeight = -img->Height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void *bits;
    HBITMAP color = CreateDIBSection(NULL, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!color)
        return NULL;
    memcpy(bits, img->Pixels, (SIZE_T)img->Width * (SIZE_T)img->Height * 4);
    HICON h = NtUserCreateEmptyCurObject(icon);
    if (h) {
        MIME_CURSOR_DATA d = {(ULONG)img->Width, (ULONG)img->Height, (ULONG)hotspot.x, (ULONG)hotspot.y,
                              icon, flags, color, NULL};
        if (!NtUserSetCursorIconData(h, NULL, NULL, &d)) {
            NtUserDestroyCursor(h, TRUE);
            h = NULL;
        }
    }
    DeleteObject(color);
    return h;
}

/* the best entry of an icon group for a size: exact size first, then the closest larger, then the deepest color. */
static WORD pick_icon(const GRPICONDIR *dir, DWORD size, int cx, int cy, BOOL icon)
{
    int best = -1, best_score = 0x7FFFFFFF;
    if (size < 6)
        return 0;
    for (int i = 0; i < dir->Count && (DWORD)(6 + (i + 1) * 14) <= size; i++) {
        const GRPICONDIRENTRY *e = &dir->Entries[i];
        int w = e->Width ? e->Width : 256, h = e->Height ? e->Height : 256;
        if (!icon) {
            /* cursor groups store width and doubled height as words. */
            w = ((const WORD *)e)[0];
            h = ((const WORD *)e)[1] / 2;
        }
        int dist = w >= cx ? (w - cx) * 2 : (cx - w) * 3;
        dist += h >= cy ? (h - cy) * 2 : (cy - h) * 3;
        int score = dist * 64 - e->BitCount;
        if (score < best_score) {
            best_score = score;
            best = i;
        }
    }
    return best >= 0 ? dir->Entries[best].Id : 0;
}

typedef struct SHARED_ICON {
    struct SHARED_ICON *Next;
    HINSTANCE Module;
    ULONG_PTR Name; /* resource id, or a hash for string names */
    int Cx, Cy;
    BOOL Icon;
    HICON Handle;
} SHARED_ICON;

static SHARED_ICON *g_shared;
static RTL_CRITICAL_SECTION g_shared_lock = RTL_CRITICAL_SECTION_INIT;

static ULONG_PTR name_key(LPCWSTR name)
{
    if (IS_INTRESOURCE(name))
        return (ULONG_PTR)name;
    ULONG_PTR h = 0x10000;
    for (; *name; name++)
        h = h * 31 + (ULONG_PTR)(*name | 0x20);
    return h | 0x10000;
}

HICON UserLoadIconFromResources(USER_RESFILE *file, HINSTANCE module, LPCWSTR name, int cx, int cy, BOOL icon,
                                UINT flags)
{
    const void *group;
    DWORD size;
    HRSRC res = NULL;
    LPCWSTR group_type = icon ? RTW_GROUP_ICON : RTW_GROUP_CURSOR;
    LPCWSTR image_type = icon ? RTW_ICON : RTW_CURSOR;
    if (file) {
        group = UserResFind(file, group_type, name, 0, &size);
    } else {
        res = FindResourceW(module, name, group_type);
        group = res ? LockResource(LoadResource(module, res)) : NULL;
        size = res ? SizeofResource(module, res) : 0;
    }
    if (!group)
        return NULL;
    WORD id = pick_icon(group, size, cx, cy, icon);
    if (!id)
        return NULL;
    const UCHAR *data;
    DWORD data_size;
    if (file) {
        data = UserResFind(file, image_type, MAKEINTRESOURCEW(id), 0, &data_size);
    } else {
        res = FindResourceW(module, MAKEINTRESOURCEW(id), image_type);
        data = res ? LockResource(LoadResource(module, res)) : NULL;
        data_size = res ? SizeofResource(module, res) : 0;
    }
    if (!data)
        return NULL;
    POINT hotspot = {0, 0};
    if (!icon) {
        if (data_size < 4)
            return NULL;
        hotspot = (POINT){((const WORD *)data)[0], ((const WORD *)data)[1]};
        data += 4;
        data_size -= 4;
    }
    ICON_IMAGE img;
    if (!decode_icon_image(data, data_size, &img))
        return NULL;
    /* scale to the requested size with a box filter when no exact size exists. */
    if ((cx && img.Width != cx) || (cy && img.Height != cy)) {
        int tw = cx ? cx : img.Width, th = cy ? cy : img.Height;
        PULONG scaled = UserAlloc((SIZE_T)tw * (SIZE_T)th * 4);
        if (scaled) {
            for (int y = 0; y < th; y++)
                for (int x = 0; x < tw; x++) {
                    int x0 = x * img.Width / tw, x1 = (x + 1) * img.Width / tw;
                    int y0 = y * img.Height / th, y1 = (y + 1) * img.Height / th;
                    if (x1 <= x0)
                        x1 = x0 + 1;
                    if (y1 <= y0)
                        y1 = y0 + 1;
                    ULONG sum[4] = {0, 0, 0, 0}, n = 0;
                    for (int sy = y0; sy < y1; sy++)
                        for (int sx = x0; sx < x1; sx++, n++) {
                            ULONG p = img.Pixels[sy * img.Width + sx];
                            for (int c = 0; c < 4; c++)
                                sum[c] += (p >> (c * 8)) & 0xFF;
                        }
                    scaled[y * tw + x] = (sum[0] / n) | ((sum[1] / n) << 8) | ((sum[2] / n) << 16) | ((sum[3] / n) << 24);
                }
            hotspot.x = hotspot.x * tw / img.Width;
            hotspot.y = hotspot.y * th / img.Height;
            UserFree(img.Pixels);
            img.Pixels = scaled;
            img.Width = tw;
            img.Height = th;
        }
    }
    HICON h = create_icon_object(&img, icon, hotspot, 0);
    UserFree(img.Pixels);
    return h;
}

/* IDI_ values to the icon groups of user32. */
static LPCWSTR system_icon_resource(LPCWSTR name)
{
    switch ((ULONG_PTR)name) {
    case 32512: /* IDI_APPLICATION */
        return MAKEINTRESOURCEW(100);
    case 32513: /* IDI_HAND */
        return MAKEINTRESOURCEW(103);
    case 32514: /* IDI_QUESTION */
        return MAKEINTRESOURCEW(102);
    case 32515: /* IDI_EXCLAMATION */
        return MAKEINTRESOURCEW(101);
    case 32516: /* IDI_ASTERISK */
        return MAKEINTRESOURCEW(104);
    case 32517: /* IDI_WINLOGO */
        return MAKEINTRESOURCEW(105);
    case 32518: /* IDI_SHIELD */
        return MAKEINTRESOURCEW(106);
    }
    return name;
}

static HICON load_shared(HINSTANCE module, LPCWSTR name, int cx, int cy, BOOL icon, UINT flags)
{
    BOOL system = !module || module == User32Instance;
    ULONG_PTR key = name_key(name);
    RtlEnterCriticalSection(&g_shared_lock);
    for (SHARED_ICON *s = g_shared; s; s = s->Next)
        if (s->Module == module && s->Name == key && s->Cx == cx && s->Cy == cy && s->Icon == icon) {
            RtlLeaveCriticalSection(&g_shared_lock);
            return s->Handle;
        }
    RtlLeaveCriticalSection(&g_shared_lock);

    HICON h = NULL;
    if (system && !icon) {
        /* system cursors are shown by the host with its native cursor. */
        h = NtUserCreateEmptyCurObject(FALSE);
        if (h) {
            MIME_CURSOR_DATA d = {(ULONG)cx, (ULONG)cy, 0, 0, FALSE, MIME_CURSORF_SYSTEM | (ULONG)(ULONG_PTR)name, NULL, NULL};
            NtUserSetCursorIconData(h, NULL, NULL, &d);
        }
    } else if (system) {
        h = UserLoadIconFromResources(UserSystemResources(), NULL, system_icon_resource(name), cx, cy, icon, flags);
    } else {
        h = UserLoadIconFromResources(NULL, module, name, cx, cy, icon, flags);
    }
    if (!h)
        return NULL;
    SHARED_ICON *s = UserAlloc(sizeof(*s));
    if (s) {
        *s = (SHARED_ICON){NULL, module, key, cx, cy, icon, h};
        RtlEnterCriticalSection(&g_shared_lock);
        s->Next = g_shared;
        g_shared = s;
        RtlLeaveCriticalSection(&g_shared_lock);
    }
    return h;
}

/*
 * LoadIconWithScaleDown of comctl32 v6, which the themed Static uses for
 * SS_ICON: the IDI_ values come from imageres, where the windows 11 artwork
 * lives, and the image is never shared.
 */
HICON UserLoadIconWithScaleDown(HINSTANCE instance, LPCWSTR name, int cx, int cy)
{
    /* user32 itself has no resources with the IDI_ values. */
    if (instance == User32Instance && IS_INTRESOURCE(name) && (ULONG_PTR)name >= 32512 && (ULONG_PTR)name <= 32518)
        return NULL;
    if (!instance && IS_INTRESOURCE(name)) {
        WORD id = 0;
        switch ((ULONG_PTR)name) {
        case 32512: /* IDI_APPLICATION */
        case 32517: /* IDI_WINLOGO */
            id = 15;
            break;
        case 32513: /* IDI_HAND */
            id = 98;
            break;
        case 32514: /* IDI_QUESTION */
            id = 99;
            break;
        case 32515: /* IDI_EXCLAMATION */
            id = 84;
            break;
        case 32516: /* IDI_ASTERISK */
            id = 81;
            break;
        case 32518: /* IDI_SHIELD */
            id = 78;
            break;
        }
        if (id) {
            HICON h = UserLoadIconFromResources(UserImageResources(), NULL, MAKEINTRESOURCEW(id), cx, cy, TRUE, 0);
            if (h)
                return h;
        }
    }
    return LoadImageW(instance, name, IMAGE_ICON, cx, cy, 0);
}

WINUSERAPI HANDLE WINAPI LoadImageW(HINSTANCE instance, LPCWSTR name, UINT type, int cx, int cy, UINT flags)
{
    if (type == IMAGE_BITMAP) {
        HRSRC res = FindResourceW(instance, name, RTW_BITMAP);
        const BITMAPINFO *bi = res ? LockResource(LoadResource(instance, res)) : NULL;
        if (!bi)
            return NULL;
        ULONG colors = bi->bmiHeader.biBitCount <= 8 ? (bi->bmiHeader.biClrUsed ? bi->bmiHeader.biClrUsed : 1u << bi->bmiHeader.biBitCount) : 0;
        if (bi->bmiHeader.biCompression == BI_BITFIELDS)
            colors = 3;
        const void *bits = (const UCHAR *)bi + bi->bmiHeader.biSize + colors * sizeof(RGBQUAD);
        void *dib_bits;
        HBITMAP bm = CreateDIBSection(NULL, bi, DIB_RGB_COLORS, &dib_bits, NULL, 0);
        if (bm) {
            LONG h = bi->bmiHeader.biHeight < 0 ? -bi->bmiHeader.biHeight : bi->bmiHeader.biHeight;
            SIZE_T stride = (SIZE_T)((bi->bmiHeader.biWidth * bi->bmiHeader.biBitCount + 31) / 32 * 4);
            memcpy(dib_bits, bits, stride * (SIZE_T)h);
        }
        return bm;
    }
    BOOL icon = type == IMAGE_ICON;
    UINT dpi = GetDpiForSystem();
    if (!cx && (flags & LR_DEFAULTSIZE))
        cx = GetSystemMetricsForDpi(icon ? SM_CXICON : SM_CXCURSOR, dpi);
    if (!cy && (flags & LR_DEFAULTSIZE))
        cy = GetSystemMetricsForDpi(icon ? SM_CYICON : SM_CYCURSOR, dpi);
    if (!cx)
        cx = GetSystemMetricsForDpi(icon ? SM_CXICON : SM_CXCURSOR, dpi);
    if (!cy)
        cy = GetSystemMetricsForDpi(icon ? SM_CYICON : SM_CYCURSOR, dpi);
    if ((flags & LR_SHARED) || !instance || instance == User32Instance)
        return load_shared(instance, name, cx, cy, icon, flags);
    return UserLoadIconFromResources(NULL, instance, name, cx, cy, icon, flags);
}

WINUSERAPI HANDLE WINAPI LoadImageA(HINSTANCE instance, LPCSTR name, UINT type, int cx, int cy, UINT flags)
{
    LPWSTR w = UserAtoW(name);
    HANDLE h = LoadImageW(instance, w, type, cx, cy, flags);
    UserFreeConverted(w, name);
    return h;
}

/* the system OBM_ bitmaps of a NULL instance are not available. */
WINUSERAPI HBITMAP WINAPI LoadBitmapW(HINSTANCE instance, LPCWSTR name)
{
    if (!instance) {
        SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND);
        return NULL;
    }
    return LoadImageW(instance, name, IMAGE_BITMAP, 0, 0, 0);
}

WINUSERAPI HICON WINAPI LoadIconW(HINSTANCE instance, LPCWSTR name)
{
    return LoadImageW(instance, name, IMAGE_ICON, 0, 0, LR_SHARED | LR_DEFAULTSIZE);
}

WINUSERAPI HICON WINAPI LoadIconA(HINSTANCE instance, LPCSTR name)
{
    return LoadImageA(instance, name, IMAGE_ICON, 0, 0, LR_SHARED | LR_DEFAULTSIZE);
}

WINUSERAPI HCURSOR WINAPI LoadCursorW(HINSTANCE instance, LPCWSTR name)
{
    return LoadImageW(instance, name, IMAGE_CURSOR, 0, 0, LR_SHARED | LR_DEFAULTSIZE);
}

WINUSERAPI HCURSOR WINAPI LoadCursorA(HINSTANCE instance, LPCSTR name)
{
    return LoadImageA(instance, name, IMAGE_CURSOR, 0, 0, LR_SHARED | LR_DEFAULTSIZE);
}

WINUSERAPI HICON WINAPI CreateIconIndirect(PICONINFO info)
{
    BITMAP bm;
    if (!info || !GetObjectW(info->hbmColor ? info->hbmColor : info->hbmMask, sizeof(bm), &bm))
        return NULL;
    HICON h = NtUserCreateEmptyCurObject(info->fIcon);
    if (!h)
        return NULL;
    MIME_CURSOR_DATA d = {(ULONG)bm.bmWidth, (ULONG)(info->hbmColor ? bm.bmHeight : bm.bmHeight / 2),
                          info->xHotspot, info->yHotspot, info->fIcon, 0, info->hbmColor, info->hbmMask};
    if (!NtUserSetCursorIconData(h, NULL, NULL, &d)) {
        NtUserDestroyCursor(h, TRUE);
        return NULL;
    }
    return h;
}

WINUSERAPI BOOL WINAPI GetIconInfo(HICON icon, PICONINFO info)
{
    return NtUserGetIconInfo(icon, info, NULL, NULL, NULL, FALSE);
}

static BOOL is_shared(HICON icon)
{
    BOOL shared = FALSE;
    RtlEnterCriticalSection(&g_shared_lock);
    for (SHARED_ICON *s = g_shared; s; s = s->Next)
        if (s->Handle == icon)
            shared = TRUE;
    RtlLeaveCriticalSection(&g_shared_lock);
    return shared;
}

WINUSERAPI BOOL WINAPI DestroyIcon(HICON icon)
{
    if (is_shared(icon))
        return TRUE;
    return NtUserDestroyCursor(icon, FALSE);
}

WINUSERAPI BOOL WINAPI DestroyCursor(HCURSOR cursor)
{
    return DestroyIcon(cursor);
}

WINUSERAPI HICON WINAPI CopyIcon(HICON icon)
{
    ICONINFO info;
    if (!GetIconInfo(icon, &info))
        return NULL;
    HICON copy = CreateIconIndirect(&info);
    DeleteObject(info.hbmColor);
    DeleteObject(info.hbmMask);
    return copy;
}

WINUSERAPI HANDLE WINAPI CopyImage(HANDLE image, UINT type, int cx, int cy, UINT flags)
{
    if (type == IMAGE_ICON || type == IMAGE_CURSOR)
        return CopyIcon(image);
    return NULL;
}

WINUSERAPI BOOL WINAPI DrawIconEx(HDC dc, int x, int y, HICON icon, int cx, int cy, UINT step, HBRUSH brush,
                                  UINT flags)
{
    return NtUserDrawIconEx(dc, x, y, icon, cx, cy, step, brush, flags, FALSE, NULL);
}

WINUSERAPI BOOL WINAPI DrawIcon(HDC dc, int x, int y, HICON icon)
{
    return NtUserDrawIconEx(dc, x, y, icon, 0, 0, 0, NULL, DI_NORMAL | DI_DEFAULTSIZE, FALSE, NULL);
}

WINUSERAPI HCURSOR WINAPI SetCursor(HCURSOR cursor)
{
    return NtUserSetCursor(cursor);
}

WINUSERAPI HCURSOR WINAPI GetCursor(void)
{
    HCURSOR c = NtUserSetCursor(NULL);
    NtUserSetCursor(c);
    return c;
}

WINUSERAPI BOOL WINAPI GetCursorInfo(PCURSORINFO info)
{
    return NtUserGetCursorInfo(info);
}

/* a monochrome cursor from and and xor planes, word aligned rows like CreateBitmap. */
WINUSERAPI HCURSOR WINAPI CreateCursor(HINSTANCE instance, int hot_x, int hot_y, int width, int height,
                                       const void *and_plane, const void *xor_plane)
{
    if (width <= 0 || height <= 0 || !and_plane || !xor_plane) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    int stride = (width + 15) / 16 * 2;
    SIZE_T plane = (SIZE_T)stride * (SIZE_T)height;
    BYTE *bits = UserAlloc(plane * 2);
    if (!bits)
        return NULL;
    memcpy(bits, and_plane, plane);
    memcpy(bits + plane, xor_plane, plane);
    HBITMAP mask = CreateBitmap(width, height * 2, 1, 1, bits);
    UserFree(bits);
    if (!mask)
        return NULL;
    ICONINFO info = {FALSE, (DWORD)hot_x, (DWORD)hot_y, mask, NULL};
    HCURSOR cursor = CreateIconIndirect(&info);
    DeleteObject(mask);
    (void)instance;
    return cursor;
}

WINUSERAPI HICON WINAPI CreateIconFromResource(PBYTE data, DWORD size, BOOL icon, DWORD version)
{
    if (!data || version != 0x30000) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    POINT hotspot = {0, 0};
    if (!icon) {
        if (size < 4)
            return NULL;
        hotspot.x = ((WORD *)data)[0];
        hotspot.y = ((WORD *)data)[1];
        data += 4;
        size -= 4;
    }
    ICON_IMAGE image;
    if (!decode_icon_image(data, size, &image)) {
        SetLastError(ERROR_INVALID_DATA);
        return NULL;
    }
    HICON result = create_icon_object(&image, icon, hotspot, 0);
    UserFree(image.Pixels);
    return result;
}
