/*
 * string tables, icons and cursors. the system's own resources are in this library: its icons, cursors, bitmaps and
 * strings, which LoadIcon, LoadCursor, LoadBitmap and LoadString of a null instance or of user32 read.
 */
#include "user32_private.h"
#include <png/png.h>

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
    HRSRC res = FindResourceExW(User32Instance, RTW_STRING, MAKEINTRESOURCEW((id >> 4) + 1), 0);
    const WORD *table = res ? LockResource(LoadResource(User32Instance, res)) : nullptr;
    if (!table) {
        if (buffer && max > 0)
            buffer[0] = 0;
        return 0;
    }
    return copy_string_from_table(table, SizeofResource(User32Instance, res), id, buffer, max);
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
    int a = WideCharToMultiByte(CP_ACP, 0, w, n, buffer, max - 1, nullptr, nullptr);
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
    PULONG Pixels; /* premultiplied bgra, top down. */
} ICON_IMAGE;

static void *png_alloc(void *ctx, size_t size)
{
    return UserAlloc(size);
}

static void png_free(void *ctx, void *p)
{
    UserFree(p);
}

static const MimeAllocator g_png_alloc = {png_alloc, png_free, nullptr};

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
        and_bits = nullptr;
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
        const UCHAR *mask = and_bits ? and_bits + (ULONG)(h - 1 - y) * and_stride : nullptr;
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
    HBITMAP color = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!color)
        return nullptr;
    memcpy(bits, img->Pixels, (SIZE_T)img->Width * (SIZE_T)img->Height * 4);
    HICON h = NtUserCreateEmptyCurObject(icon);
    if (h) {
        MIME_CURSOR_DATA d = {(ULONG)img->Width, (ULONG)img->Height, (ULONG)hotspot.x, (ULONG)hotspot.y,
                              icon, flags, color, nullptr};
        if (!NtUserSetCursorIconData(h, nullptr, nullptr, &d)) {
            NtUserDestroyCursor(h, TRUE);
            h = nullptr;
        }
    }
    DeleteObject(color);
    return h;
}

/* box filter scaling for when no exact size exists. a size of 0 keeps the current one. */
static void scale_image(ICON_IMAGE *img, int cx, int cy, POINT *hotspot)
{
    if ((!cx || img->Width == cx) && (!cy || img->Height == cy))
        return;
    int tw = cx ? cx : img->Width, th = cy ? cy : img->Height;
    PULONG scaled = UserAlloc((SIZE_T)tw * (SIZE_T)th * 4);
    if (!scaled)
        return;
    for (int y = 0; y < th; y++)
        for (int x = 0; x < tw; x++) {
            int x0 = x * img->Width / tw, x1 = (x + 1) * img->Width / tw;
            int y0 = y * img->Height / th, y1 = (y + 1) * img->Height / th;
            if (x1 <= x0)
                x1 = x0 + 1;
            if (y1 <= y0)
                y1 = y0 + 1;
            ULONG sum[4] = {0, 0, 0, 0}, n = 0;
            for (int sy = y0; sy < y1; sy++)
                for (int sx = x0; sx < x1; sx++, n++) {
                    ULONG p = img->Pixels[sy * img->Width + sx];
                    for (int c = 0; c < 4; c++)
                        sum[c] += (p >> (c * 8)) & 0xFF;
                }
            scaled[y * tw + x] = (sum[0] / n) | ((sum[1] / n) << 8) | ((sum[2] / n) << 16) | ((sum[3] / n) << 24);
        }
    hotspot->x = hotspot->x * tw / img->Width;
    hotspot->y = hotspot->y * th / img->Height;
    UserFree(img->Pixels);
    img->Pixels = scaled;
    img->Width = tw;
    img->Height = th;
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
    ULONG_PTR Name; /* resource id, or a hash for string names. */
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

/* the frame of an animated cursor that comes first, a cursor file inside the RIFF chunks. */
static BOOL find_frame(const UCHAR *p, const UCHAR *end, const UCHAR **frame, DWORD *frame_size)
{
    while (end - p >= 8) {
        DWORD length = *(const DWORD *)(p + 4);
        const UCHAR *body = p + 8;
        if ((SIZE_T)(end - body) < length)
            length = (DWORD)(end - body);
        if (!memcmp(p, "icon", 4)) {
            *frame = body;
            *frame_size = length;
            return TRUE;
        }
        if (!memcmp(p, "LIST", 4) && length >= 4 && !memcmp(body, "fram", 4) &&
            find_frame(body + 4, body + length, frame, frame_size))
            return TRUE;
        p = body + ((length + 1) & ~1u);
    }
    return FALSE;
}

typedef struct FILE_ICON_ENTRY {
    BYTE Width, Height, Colors, Reserved;
    WORD Planes, BitCount;
    DWORD Bytes, Offset;
} FILE_ICON_ENTRY;

/*
 * an icon or cursor file is a directory of images, and the one nearest the requested size is taken, as from a
 * resource group. a cursor's hot spot is in the directory entry, where an icon has planes and bits. an animated
 * cursor shows its first frame.
 */
static HICON load_icon_data(const UCHAR *data, DWORD size, int cx, int cy, BOOL icon, ULONG object_flags)
{
    if (size >= 12 && !memcmp(data, "RIFF", 4) && !memcmp(data + 8, "ACON", 4)) {
        const UCHAR *frame;
        DWORD frame_size;
        if (!find_frame(data + 12, data + size, &frame, &frame_size)) {
            SetLastError(ERROR_INVALID_DATA);
            return nullptr;
        }
        data = frame;
        size = frame_size;
    }
    /* a size of 0 picks the image that suits the system size and keeps its own size. */
    int want_cx = cx ? cx : GetSystemMetrics(icon ? SM_CXICON : SM_CXCURSOR);
    int want_cy = cy ? cy : GetSystemMetrics(icon ? SM_CYICON : SM_CYCURSOR);
    const WORD *directory = (const WORD *)data;
    if (size < 6 || directory[0] != 0 || (directory[1] != 1 && directory[1] != 2) || !directory[2] ||
        6 + (SIZE_T)directory[2] * 16 > size) {
        SetLastError(ERROR_INVALID_DATA);
        return nullptr;
    }
    WORD kind = directory[1], count = directory[2];
    const FILE_ICON_ENTRY *entries = (const FILE_ICON_ENTRY *)(data + 6);
    int best = -1, best_score = 0x7FFFFFFF;
    for (int i = 0; i < count; i++) {
        const FILE_ICON_ENTRY *e = &entries[i];
        if ((SIZE_T)e->Offset + e->Bytes > size || e->Bytes == 0)
            continue;
        int w = e->Width ? e->Width : 256, h = e->Height ? e->Height : 256;
        int dist = w >= want_cx ? (w - want_cx) * 2 : (want_cx - w) * 3;
        dist += h >= want_cy ? (h - want_cy) * 2 : (want_cy - h) * 3;
        int score = dist * 64 - (kind == 1 ? e->BitCount : 0);
        if (score < best_score) {
            best_score = score;
            best = i;
        }
    }
    if (best < 0) {
        SetLastError(ERROR_INVALID_DATA);
        return nullptr;
    }
    ICON_IMAGE image;
    POINT hotspot = {0, 0};
    if (kind == 2) {
        hotspot.x = entries[best].Planes;
        hotspot.y = entries[best].BitCount;
    }
    if (!decode_icon_image(data + entries[best].Offset, entries[best].Bytes, &image)) {
        SetLastError(ERROR_INVALID_DATA);
        return nullptr;
    }
    scale_image(&image, cx, cy, &hotspot);
    HICON result = create_icon_object(&image, icon, hotspot, object_flags);
    UserFree(image.Pixels);
    return result;
}

/* the group of an icon or cursor in a module's resources, an animated cursor included. */
HICON UserLoadIconFromResources(HINSTANCE module, LPCWSTR name, int cx, int cy, BOOL icon, ULONG object_flags)
{
    LPCWSTR group_type = icon ? RTW_GROUP_ICON : RTW_GROUP_CURSOR;
    LPCWSTR image_type = icon ? RTW_ICON : RTW_CURSOR;
    HRSRC res = FindResourceW(module, name, group_type);
    const void *group = res ? LockResource(LoadResource(module, res)) : nullptr;
    DWORD size = res ? SizeofResource(module, res) : 0;
    if (!group) {
        if (icon)
            return nullptr;
        res = FindResourceW(module, name, MAKEINTRESOURCEW(21) /* RT_ANICURSOR. */);
        const UCHAR *ani = res ? LockResource(LoadResource(module, res)) : nullptr;
        return ani ? load_icon_data(ani, SizeofResource(module, res), cx, cy, FALSE, object_flags) : nullptr;
    }
    WORD id = pick_icon(group, size, cx, cy, icon);
    if (!id)
        return nullptr;
    res = FindResourceW(module, MAKEINTRESOURCEW(id), image_type);
    const UCHAR *data = res ? LockResource(LoadResource(module, res)) : nullptr;
    DWORD data_size = res ? SizeofResource(module, res) : 0;
    if (!data)
        return nullptr;
    POINT hotspot = {0, 0};
    if (!icon) {
        if (data_size < 4)
            return nullptr;
        hotspot = (POINT){((const WORD *)data)[0], ((const WORD *)data)[1]};
        data += 4;
        data_size -= 4;
    }
    ICON_IMAGE img;
    if (!decode_icon_image(data, data_size, &img))
        return nullptr;
    scale_image(&img, cx, cy, &hotspot);
    HICON h = create_icon_object(&img, icon, hotspot, object_flags);
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

    HICON h = nullptr;
    if (system && !icon) {
        /* the host shows a system cursor with its native cursor, the picture here is what programs read from it. */
        ULONG system_flags = MIME_CURSORF_SYSTEM | (ULONG)(ULONG_PTR)name;
        h = UserLoadIconFromResources(User32Instance, name, cx, cy, FALSE, system_flags);
    } else if (system) {
        h = UserLoadIconFromResources(User32Instance, system_icon_resource(name), cx, cy, icon, 0);
    } else {
        h = UserLoadIconFromResources(module, name, cx, cy, icon, 0);
    }
    if (!h)
        return nullptr;
    SHARED_ICON *s = UserAlloc(sizeof(*s));
    if (s) {
        *s = (SHARED_ICON){nullptr, module, key, cx, cy, icon, h};
        RtlEnterCriticalSection(&g_shared_lock);
        s->Next = g_shared;
        g_shared = s;
        RtlLeaveCriticalSection(&g_shared_lock);
    }
    return h;
}

/*
 * LoadIconWithScaleDown of comctl32 v6, used by the themed Static for SS_ICON. a null instance reads the IDI_ values
 * from user32, and the image is never shared.
 */
HICON UserLoadIconWithScaleDown(HINSTANCE instance, LPCWSTR name, int cx, int cy)
{
    /* user32 itself has no resources with the IDI_ values. */
    if (instance == User32Instance && IS_INTRESOURCE(name) && (ULONG_PTR)name >= 32512 && (ULONG_PTR)name <= 32518)
        return nullptr;
    if (!instance && IS_INTRESOURCE(name) && (ULONG_PTR)name >= 32512 && (ULONG_PTR)name <= 32518) {
        HICON h = UserLoadIconFromResources(User32Instance, system_icon_resource(name), cx, cy, TRUE, 0);
        if (h)
            return h;
    }
    return LoadImageW(instance, name, IMAGE_ICON, cx, cy, 0);
}

/* reads a whole file of up to 32 MiB, the caller frees it. */
static UCHAR *read_file(LPCWSTR path, DWORD *size)
{
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return nullptr;
    DWORD length = GetFileSize(file, nullptr), got = 0;
    UCHAR *data = length != INVALID_FILE_SIZE && length >= 8 && length <= 0x2000000 ? UserAlloc(length) : nullptr;
    if (data && (!ReadFile(file, data, length, &got, nullptr) || got != length)) {
        UserFree(data);
        data = nullptr;
    }
    if (!data && length != INVALID_FILE_SIZE && (length < 8 || length > 0x2000000))
        SetLastError(ERROR_INVALID_DATA);
    CloseHandle(file);
    *size = length;
    return data;
}

static HICON load_icon_file(LPCWSTR path, int cx, int cy, BOOL icon)
{
    DWORD size;
    UCHAR *data = read_file(path, &size);
    if (!data)
        return nullptr;
    HICON result = load_icon_data(data, size, cx, cy, icon, 0);
    UserFree(data);
    return result;
}

/* the bits sit at the offset the file header gives. the dib section flag makes a dib section. */
static HBITMAP load_bitmap_file(LPCWSTR path, UINT flags)
{
    DWORD size;
    UCHAR *data = read_file(path, &size);
    if (!data)
        return nullptr;
    HBITMAP bitmap = nullptr;
    const BITMAPFILEHEADER *file = (const BITMAPFILEHEADER *)data;
    if (size < sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) || file->bfType != 0x4D42 ||
        file->bfOffBits >= size) {
        SetLastError(ERROR_INVALID_DATA);
        goto done;
    }
    const BITMAPINFO *info = (const BITMAPINFO *)(data + sizeof(BITMAPFILEHEADER));
    if (info->bmiHeader.biSize < sizeof(BITMAPINFOHEADER) || info->bmiHeader.biSize > 124 ||
        info->bmiHeader.biCompression > BI_BITFIELDS) {
        SetLastError(ERROR_INVALID_DATA);
        goto done;
    }
    const void *bits = data + file->bfOffBits;
    if (flags & LR_CREATEDIBSECTION) {
        void *dib_bits;
        bitmap = CreateDIBSection(nullptr, info, DIB_RGB_COLORS, &dib_bits, nullptr, 0);
        if (bitmap) {
            LONG h = info->bmiHeader.biHeight < 0 ? -info->bmiHeader.biHeight : info->bmiHeader.biHeight;
            SIZE_T stride = (SIZE_T)((info->bmiHeader.biWidth * info->bmiHeader.biBitCount + 31) / 32 * 4);
            SIZE_T have = size - file->bfOffBits;
            memcpy(dib_bits, bits, stride * (SIZE_T)h < have ? stride * (SIZE_T)h : have);
        }
    } else {
        HDC screen = GetDC(nullptr);
        bitmap = CreateDIBitmap(screen, &info->bmiHeader, CBM_INIT, bits, info, DIB_RGB_COLORS);
        ReleaseDC(nullptr, screen);
    }
done:
    UserFree(data);
    return bitmap;
}

WINUSERAPI HANDLE WINAPI LoadImageW(HINSTANCE instance, LPCWSTR name, UINT type, int cx, int cy, UINT flags)
{
    if (flags & LR_LOADFROMFILE) {
        if (IS_INTRESOURCE(name)) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return nullptr;
        }
        if (type == IMAGE_BITMAP)
            return load_bitmap_file(name, flags);
        UINT dpi = GetDpiForSystem();
        BOOL icon = type == IMAGE_ICON;
        if (!cx && (flags & LR_DEFAULTSIZE))
            cx = GetSystemMetricsForDpi(icon ? SM_CXICON : SM_CXCURSOR, dpi);
        if (!cy && (flags & LR_DEFAULTSIZE))
            cy = GetSystemMetricsForDpi(icon ? SM_CYICON : SM_CYCURSOR, dpi);
        return load_icon_file(name, cx, cy, icon);
    }
    if (type == IMAGE_BITMAP) {
        /* the OBM_ bitmaps of the system are user32's own. */
        if (!instance)
            instance = User32Instance;
        HRSRC res = FindResourceW(instance, name, RTW_BITMAP);
        const BITMAPINFO *bi = res ? LockResource(LoadResource(instance, res)) : nullptr;
        if (!bi)
            return nullptr;
        ULONG colors = bi->bmiHeader.biBitCount <= 8 ? (bi->bmiHeader.biClrUsed ? bi->bmiHeader.biClrUsed : 1u << bi->bmiHeader.biBitCount) : 0;
        if (bi->bmiHeader.biCompression == BI_BITFIELDS)
            colors = 3;
        const void *bits = (const UCHAR *)bi + bi->bmiHeader.biSize + colors * sizeof(RGBQUAD);
        void *dib_bits;
        HBITMAP bm = CreateDIBSection(nullptr, bi, DIB_RGB_COLORS, &dib_bits, nullptr, 0);
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
    return UserLoadIconFromResources(instance, name, cx, cy, icon, 0);
}

WINUSERAPI HANDLE WINAPI LoadImageA(HINSTANCE instance, LPCSTR name, UINT type, int cx, int cy, UINT flags)
{
    LPWSTR w = UserAtoW(name);
    HANDLE h = LoadImageW(instance, w, type, cx, cy, flags);
    UserFreeConverted(w, name);
    return h;
}

/* a null instance names the system bitmaps, OBM_CLOSE and the others. */
WINUSERAPI HBITMAP WINAPI LoadBitmapW(HINSTANCE instance, LPCWSTR name)
{
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
        return nullptr;
    HICON h = NtUserCreateEmptyCurObject(info->fIcon);
    if (!h)
        return nullptr;
    MIME_CURSOR_DATA d = {(ULONG)bm.bmWidth, (ULONG)(info->hbmColor ? bm.bmHeight : bm.bmHeight / 2),
                          info->xHotspot, info->yHotspot, info->fIcon, 0, info->hbmColor, info->hbmMask};
    if (!NtUserSetCursorIconData(h, nullptr, nullptr, &d)) {
        NtUserDestroyCursor(h, TRUE);
        return nullptr;
    }
    return h;
}

WINUSERAPI BOOL WINAPI GetIconInfo(HICON icon, PICONINFO info)
{
    return NtUserGetIconInfo(icon, info, nullptr, nullptr, nullptr, FALSE);
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
        return nullptr;
    HICON copy = CreateIconIndirect(&info);
    DeleteObject(info.hbmColor);
    DeleteObject(info.hbmMask);
    return copy;
}

WINUSERAPI HANDLE WINAPI CopyImage(HANDLE image, UINT type, int cx, int cy, UINT flags)
{
    if (type == IMAGE_ICON || type == IMAGE_CURSOR)
        return CopyIcon(image);
    return nullptr;
}

WINUSERAPI BOOL WINAPI DrawIconEx(HDC dc, int x, int y, HICON icon, int cx, int cy, UINT step, HBRUSH brush,
                                  UINT flags)
{
    return NtUserDrawIconEx(dc, x, y, icon, cx, cy, step, brush, flags, FALSE, nullptr);
}

WINUSERAPI BOOL WINAPI DrawIcon(HDC dc, int x, int y, HICON icon)
{
    return NtUserDrawIconEx(dc, x, y, icon, 0, 0, 0, nullptr, DI_NORMAL | DI_DEFAULTSIZE, FALSE, nullptr);
}

WINUSERAPI HCURSOR WINAPI SetCursor(HCURSOR cursor)
{
    return NtUserSetCursor(cursor);
}

WINUSERAPI HCURSOR WINAPI GetCursor(void)
{
    HCURSOR c = NtUserSetCursor(nullptr);
    NtUserSetCursor(c);
    return c;
}

WINUSERAPI BOOL WINAPI GetCursorInfo(PCURSORINFO info)
{
    return NtUserGetCursorInfo(info);
}

/* a monochrome cursor or icon from AND and XOR planes, with word aligned rows like CreateBitmap. */
static HICON create_monochrome(BOOL icon, int hot_x, int hot_y, int width, int height, const void *and_plane,
                               const void *xor_plane)
{
    if (width <= 0 || height <= 0 || !and_plane || !xor_plane) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    int stride = (width + 15) / 16 * 2;
    SIZE_T plane = (SIZE_T)stride * (SIZE_T)height;
    BYTE *bits = UserAlloc(plane * 2);
    if (!bits)
        return nullptr;
    memcpy(bits, and_plane, plane);
    memcpy(bits + plane, xor_plane, plane);
    HBITMAP mask = CreateBitmap(width, height * 2, 1, 1, bits);
    UserFree(bits);
    if (!mask)
        return nullptr;
    ICONINFO info = {icon, (DWORD)hot_x, (DWORD)hot_y, mask, nullptr};
    HICON result = CreateIconIndirect(&info);
    DeleteObject(mask);
    return result;
}

WINUSERAPI HCURSOR WINAPI CreateCursor(HINSTANCE instance, int hot_x, int hot_y, int width, int height,
                                       const void *and_plane, const void *xor_plane)
{
    (void)instance;
    return create_monochrome(FALSE, hot_x, hot_y, width, height, and_plane, xor_plane);
}

/* a color icon takes a plane of colors and a monochrome mask. */
WINUSERAPI HICON WINAPI CreateIcon(HINSTANCE instance, int width, int height, BYTE planes, BYTE bits, const BYTE *and_plane,
                                   const BYTE *xor_plane)
{
    (void)instance;
    if (planes * bits <= 1)
        return create_monochrome(TRUE, width / 2, height / 2, width, height, and_plane, xor_plane);
    if (width <= 0 || height <= 0 || !and_plane || !xor_plane) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    HBITMAP color = CreateBitmap(width, height, planes, bits, xor_plane);
    HBITMAP mask = CreateBitmap(width, height, 1, 1, and_plane);
    HICON result = nullptr;
    if (color && mask) {
        ICONINFO info = {TRUE, (DWORD)(width / 2), (DWORD)(height / 2), mask, color};
        result = CreateIconIndirect(&info);
    }
    DeleteObject(color);
    DeleteObject(mask);
    return result;
}

/* the best image of an icon or cursor directory for the size. a size of 0 is the system's. */
WINUSERAPI int WINAPI LookupIconIdFromDirectoryEx(PBYTE bits, BOOL icon, int cx, int cy, UINT flags)
{
    const GRPICONDIR *dir = (const GRPICONDIR *)bits;
    if (!dir || dir->Reserved != 0 || dir->Type != (icon ? 1 : 2))
        return 0;
    if (!cx)
        cx = GetSystemMetrics(icon ? SM_CXICON : SM_CXCURSOR);
    if (!cy)
        cy = GetSystemMetrics(icon ? SM_CYICON : SM_CYCURSOR);
    (void)flags;
    return pick_icon(dir, 6 + dir->Count * sizeof(GRPICONDIRENTRY), cx, cy, icon);
}

WINUSERAPI int WINAPI LookupIconIdFromDirectory(PBYTE bits, BOOL icon)
{
    return LookupIconIdFromDirectoryEx(bits, icon, 0, 0, 0);
}

WINUSERAPI HCURSOR WINAPI LoadCursorFromFileW(LPCWSTR file)
{
    return LoadImageW(nullptr, file, IMAGE_CURSOR, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
}

WINUSERAPI HCURSOR WINAPI LoadCursorFromFileA(LPCSTR file)
{
    return LoadImageA(nullptr, file, IMAGE_CURSOR, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE);
}

/*
 * a size of 0 keeps the image's size, or takes the system's with LR_DEFAULTSIZE. returns NULL without setting an
 * error for no data, too few bytes for a header and an image, or a version other than 2.0 or 3.0. animated cursors,
 * which start with RIFF, are not read.
 */
WINUSERAPI HICON WINAPI CreateIconFromResourceEx(PBYTE data, DWORD size, BOOL icon, DWORD version, int cx, int cy,
                                                 UINT flags)
{
    if (!data || size < 0x28 || version - 0x20000 > 0x10000)
        return nullptr;
    if ((flags & LR_DEFAULTSIZE) && !cx)
        cx = GetSystemMetrics(icon ? SM_CXICON : SM_CXCURSOR);
    if ((flags & LR_DEFAULTSIZE) && !cy)
        cy = GetSystemMetrics(icon ? SM_CYICON : SM_CYCURSOR);
    if (*(const DWORD *)data == 0x46464952)
        return nullptr;
    POINT hotspot = {0, 0};
    if (!icon) {
        hotspot.x = ((WORD *)data)[0];
        hotspot.y = ((WORD *)data)[1];
        data += 4;
        size -= 4;
    }
    ICON_IMAGE image;
    if (!decode_icon_image(data, size, &image))
        return nullptr;
    scale_image(&image, cx, cy, &hotspot);
    HICON result = create_icon_object(&image, icon, hotspot, 0);
    UserFree(image.Pixels);
    return result;
}

WINUSERAPI HICON WINAPI CreateIconFromResource(PBYTE data, DWORD size, BOOL icon, DWORD version)
{
    if (!data || version != 0x30000) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    POINT hotspot = {0, 0};
    if (!icon) {
        if (size < 4)
            return nullptr;
        hotspot.x = ((WORD *)data)[0];
        hotspot.y = ((WORD *)data)[1];
        data += 4;
        size -= 4;
    }
    ICON_IMAGE image;
    if (!decode_icon_image(data, size, &image)) {
        SetLastError(ERROR_INVALID_DATA);
        return nullptr;
    }
    HICON result = create_icon_object(&image, icon, hotspot, 0);
    UserFree(image.Pixels);
    return result;
}
