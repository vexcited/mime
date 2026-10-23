/* IWICPalette: up to 256 colors from predefined Windows palettes or bitmap quantization. */
#include "wic_private.h"

#define PALETTE_MAX 256

typedef struct WicPalette {
    WicBase base;
    WicSlot palette;
    CRITICAL_SECTION lock;
    WICBitmapPaletteType type;
    UINT count;
    WICColor colors[PALETTE_MAX];
} WicPalette;

/* the cube sizes of the halftone palettes, red then green then blue. */
static const BYTE g_cube[10][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {2, 2, 2}, {3, 3, 3},
                                   {4, 4, 4}, {5, 5, 5}, {6, 6, 6}, {6, 7, 6}, {8, 8, 4}};

/* add missing standard display colors to smaller halftone cubes. */
static const WICColor g_system[16] = {0xff000000, 0xffffffff, 0xffc0c0c0, 0xff808080, 0xff800000, 0xffff0000,
                                      0xff008000, 0xff00ff00, 0xff000080, 0xff0000ff, 0xff808000, 0xffffff00,
                                      0xff800080, 0xffff00ff, 0xff008080, 0xff00ffff};

static BYTE level(UINT index, UINT levels)
{
    float step = 255.0f / (float)(levels - 1);
    return (BYTE)(int)(step * (float)index + 0.5f);
}

UINT WicPredefinedPalette(WICBitmapPaletteType type, BOOL transparent, WICColor *colors)
{
    UINT count = 0;
    switch (type) {
    case WICBitmapPaletteTypeFixedBW:
        colors[0] = 0xff000000;
        colors[1] = 0xffffffff;
        return 2;
    case WICBitmapPaletteTypeFixedGray4:
        colors[0] = 0xff000000;
        colors[1] = 0xff555555;
        colors[2] = 0xffaaaaaa;
        colors[3] = 0xffffffff;
        return 4;
    case WICBitmapPaletteTypeFixedGray16:
        for (UINT i = 0; i < 16; i++)
            colors[i] = 0xff000000 | (i * 0x111111);
        return 16;
    case WICBitmapPaletteTypeFixedGray256:
        for (UINT i = 0; i < 256; i++)
            colors[i] = 0xff000000 | (i * 0x010101);
        return 256;
    case WICBitmapPaletteTypeFixedHalftone8:
    case WICBitmapPaletteTypeFixedHalftone27:
    case WICBitmapPaletteTypeFixedHalftone64:
    case WICBitmapPaletteTypeFixedHalftone125:
    case WICBitmapPaletteTypeFixedHalftone216:
    case WICBitmapPaletteTypeFixedHalftone252:
    case WICBitmapPaletteTypeFixedHalftone256:
        break;
    default:
        return 0;
    }
    const BYTE *cube = g_cube[type];
    for (UINT r = 0; r < cube[0]; r++) {
        for (UINT g = 0; g < cube[1]; g++) {
            for (UINT b = 0; b < cube[2]; b++) {
                colors[count++] = 0xff000000 | ((UINT)level(r, cube[0]) << 16) | ((UINT)level(g, cube[1]) << 8) |
                                  level(b, cube[2]);
            }
        }
    }
    if (type <= WICBitmapPaletteTypeFixedHalftone216) {
        for (UINT i = 0; i < 16 && count < PALETTE_MAX; i++) {
            UINT j = 0;
            while (j < count && colors[j] != g_system[i])
                j++;
            if (j == count)
                colors[count++] = g_system[i];
        }
    }
    if (transparent) {
        if (count >= PALETTE_MAX)
            count = PALETTE_MAX - 1;
        colors[count++] = 0;
    }
    return count;
}

/* median cut. */

typedef struct Box {
    UINT first, count; /* in the sorted bin list. */
    UINT population;
    BYTE lo[3], hi[3];
} Box;

typedef struct Bin {
    UINT key; /* 5 bits of each channel. */
    UINT count;
} Bin;

static void box_bounds(Box *box, const Bin *bins)
{
    box->lo[0] = box->lo[1] = box->lo[2] = 31;
    box->hi[0] = box->hi[1] = box->hi[2] = 0;
    box->population = 0;
    for (UINT i = 0; i < box->count; i++) {
        UINT key = bins[box->first + i].key;
        BYTE c[3] = {(BYTE)(key >> 10), (BYTE)((key >> 5) & 31), (BYTE)(key & 31)};
        for (int k = 0; k < 3; k++) {
            if (c[k] < box->lo[k])
                box->lo[k] = c[k];
            if (c[k] > box->hi[k])
                box->hi[k] = c[k];
        }
        box->population += bins[box->first + i].count;
    }
}

static void sort_bins(Bin *bins, UINT first, UINT count, int channel)
{
    /* use insertion sort after the first cuts, when boxes have too few bins to justify merging. */
    UINT shift = channel == 0 ? 10 : channel == 1 ? 5 : 0;
    if (count > 64) {
        /* a shell sort for the large first boxes. */
        for (UINT gap = count / 2; gap > 0; gap /= 2) {
            for (UINT i = gap; i < count; i++) {
                Bin tmp = bins[first + i];
                UINT j = i;
                while (j >= gap && ((bins[first + j - gap].key >> shift) & 31) > ((tmp.key >> shift) & 31)) {
                    bins[first + j] = bins[first + j - gap];
                    j -= gap;
                }
                bins[first + j] = tmp;
            }
        }
        return;
    }
    for (UINT i = 1; i < count; i++) {
        Bin tmp = bins[first + i];
        UINT j = i;
        while (j > 0 && ((bins[first + j - 1].key >> shift) & 31) > ((tmp.key >> shift) & 31)) {
            bins[first + j] = bins[first + j - 1];
            j--;
        }
        bins[first + j] = tmp;
    }
}

static BYTE expand5(UINT v) { return (BYTE)((v << 3) | (v >> 2)); }

static HRESULT median_cut(Bin *bins, UINT bin_count, UINT target, WICColor *colors, UINT *used)
{
    Box boxes[PALETTE_MAX];
    UINT box_count = 1;
    boxes[0].first = 0;
    boxes[0].count = bin_count;
    box_bounds(&boxes[0], bins);
    while (box_count < target) {
        /* choose the most populated box that can still be split. */
        int best = -1;
        UINT best_population = 0;
        for (UINT i = 0; i < box_count; i++) {
            if (boxes[i].count > 1 && boxes[i].population > best_population) {
                best = (int)i;
                best_population = boxes[i].population;
            }
        }
        if (best < 0)
            break;
        Box *box = &boxes[best];
        int channel = 0;
        UINT widest = 0;
        for (int k = 0; k < 3; k++) {
            UINT extent = (UINT)(box->hi[k] - box->lo[k]);
            /* weight green more heavily for perceptual distance. */
            UINT weight = k == 1 ? extent * 3 : k == 0 ? extent * 2 : extent;
            if (weight >= widest) {
                widest = weight;
                channel = k;
            }
        }
        sort_bins(bins, box->first, box->count, channel);
        UINT half = box->population / 2, sum = 0, cut = 0;
        while (cut < box->count - 1 && sum + bins[box->first + cut].count <= half) {
            sum += bins[box->first + cut].count;
            cut++;
        }
        if (cut == 0)
            cut = 1;
        Box second;
        second.first = box->first + cut;
        second.count = box->count - cut;
        box->count = cut;
        box_bounds(box, bins);
        box_bounds(&second, bins);
        boxes[box_count++] = second;
    }
    for (UINT i = 0; i < box_count; i++) {
        unsigned long long r = 0, g = 0, b = 0, n = 0;
        for (UINT k = 0; k < boxes[i].count; k++) {
            const Bin *bin = &bins[boxes[i].first + k];
            r += (unsigned long long)expand5(bin->key >> 10) * bin->count;
            g += (unsigned long long)expand5((bin->key >> 5) & 31) * bin->count;
            b += (unsigned long long)expand5(bin->key & 31) * bin->count;
            n += bin->count;
        }
        if (!n)
            n = 1;
        colors[i] = 0xff000000 | ((UINT)((r + n / 2) / n) << 16) | ((UINT)((g + n / 2) / n) << 8) |
                    (UINT)((b + n / 2) / n);
    }
    *used = box_count;
    return S_OK;
}

HRESULT WicPaletteFromBitmap(IWICBitmapSource *source, UINT count, BOOL transparent, WICColor *colors, UINT *used)
{
    UINT width, height;
    IWICBitmapSource *bgra = nullptr;
    HRESULT hr = source->lpVtbl->GetSize(source, &width, &height);
    if (FAILED(hr))
        return hr;
    hr = WicEnsureFormat(source, &GUID_WICPixelFormat32bppBGRA, &bgra);
    if (FAILED(hr))
        return hr;
    UINT *histogram = WicAllocZero(32768 * sizeof(UINT));
    BYTE *row = WicAlloc((SIZE_T)width * 4);
    if (!histogram || !row) {
        WicFree(histogram);
        WicFree(row);
        bgra->lpVtbl->Release(bgra);
        return E_OUTOFMEMORY;
    }
    BOOL has_transparent = FALSE;
    for (UINT y = 0; y < height && SUCCEEDED(hr); y++) {
        WICRect rect = {0, (INT)y, (INT)width, 1};
        hr = bgra->lpVtbl->CopyPixels(bgra, &rect, width * 4, width * 4, row);
        if (FAILED(hr))
            break;
        for (UINT x = 0; x < width; x++) {
            const BYTE *p = row + x * 4;
            if (p[3] < 128) {
                has_transparent = TRUE;
                continue;
            }
            histogram[((UINT)(p[2] >> 3) << 10) | ((UINT)(p[1] >> 3) << 5) | (p[0] >> 3)]++;
        }
    }
    bgra->lpVtbl->Release(bgra);
    WicFree(row);
    if (FAILED(hr)) {
        WicFree(histogram);
        return hr;
    }
    UINT bin_count = 0;
    for (UINT i = 0; i < 32768; i++) {
        if (histogram[i])
            bin_count++;
    }
    Bin *bins = WicAlloc(((SIZE_T)bin_count + 1) * sizeof(Bin));
    if (!bins) {
        WicFree(histogram);
        return E_OUTOFMEMORY;
    }
    UINT n = 0;
    for (UINT i = 0; i < 32768; i++) {
        if (histogram[i]) {
            bins[n].key = i;
            bins[n].count = histogram[i];
            n++;
        }
    }
    WicFree(histogram);
    UINT target = count;
    if (transparent && target > 0)
        target--;
    UINT got = 0;
    if (n)
        hr = median_cut(bins, n, target, colors, &got);
    WicFree(bins);
    if (FAILED(hr))
        return hr;
    if (transparent || (has_transparent && got < count && FALSE))
        colors[got++] = 0;
    *used = got;
    return S_OK;
}

/* the object. */

static WicPalette *palette_from(IWICPalette *This) { return WIC_FROM_SLOT(WicPalette, palette, This); }

static void palette_destroy(WicBase *base)
{
    WicPalette *palette = (WicPalette *)base;
    DeleteCriticalSection(&palette->lock);
}

static HRESULT STDMETHODCALLTYPE palette_init_predefined(IWICPalette *This, WICBitmapPaletteType type,
                                                         BOOL add_transparent)
{
    WicPalette *palette = palette_from(This);
    WICColor colors[PALETTE_MAX];
    UINT count = WicPredefinedPalette(type, add_transparent, colors);
    if (!count)
        return E_INVALIDARG;
    EnterCriticalSection(&palette->lock);
    memcpy(palette->colors, colors, count * sizeof(WICColor));
    palette->count = count;
    palette->type = type;
    LeaveCriticalSection(&palette->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE palette_init_custom(IWICPalette *This, WICColor *colors, UINT count)
{
    WicPalette *palette = palette_from(This);
    if (count > PALETTE_MAX || (count && !colors))
        return E_INVALIDARG;
    EnterCriticalSection(&palette->lock);
    if (count)
        memcpy(palette->colors, colors, count * sizeof(WICColor));
    palette->count = count;
    palette->type = WICBitmapPaletteTypeCustom;
    LeaveCriticalSection(&palette->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE palette_init_bitmap(IWICPalette *This, IWICBitmapSource *source, UINT count,
                                                     BOOL add_transparent)
{
    WicPalette *palette = palette_from(This);
    if (!source || !count || count > PALETTE_MAX)
        return E_INVALIDARG;
    WICColor colors[PALETTE_MAX];
    UINT used = 0;
    HRESULT hr = WicPaletteFromBitmap(source, count, add_transparent, colors, &used);
    if (FAILED(hr))
        return hr;
    EnterCriticalSection(&palette->lock);
    memcpy(palette->colors, colors, used * sizeof(WICColor));
    palette->count = used;
    palette->type = WICBitmapPaletteTypeMedianCut;
    LeaveCriticalSection(&palette->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE palette_init_palette(IWICPalette *This, IWICPalette *source)
{
    WicPalette *palette = palette_from(This);
    if (!source)
        return E_INVALIDARG;
    if (source == This)
        return S_OK;
    WICBitmapPaletteType type;
    UINT count = 0, got = 0;
    WICColor colors[PALETTE_MAX];
    HRESULT hr = source->lpVtbl->GetType(source, &type);
    if (SUCCEEDED(hr))
        hr = source->lpVtbl->GetColorCount(source, &count);
    if (SUCCEEDED(hr) && count > PALETTE_MAX)
        hr = E_FAIL;
    if (SUCCEEDED(hr) && count)
        hr = source->lpVtbl->GetColors(source, count, colors, &got);
    if (FAILED(hr))
        return hr;
    EnterCriticalSection(&palette->lock);
    memcpy(palette->colors, colors, got * sizeof(WICColor));
    palette->count = got;
    palette->type = type;
    LeaveCriticalSection(&palette->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE palette_get_type(IWICPalette *This, WICBitmapPaletteType *type)
{
    WicPalette *palette = palette_from(This);
    if (!type)
        return E_INVALIDARG;
    *type = palette->type;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE palette_get_count(IWICPalette *This, UINT *count)
{
    WicPalette *palette = palette_from(This);
    if (!count)
        return E_INVALIDARG;
    *count = palette->count;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE palette_get_colors(IWICPalette *This, UINT count, WICColor *colors, UINT *actual)
{
    WicPalette *palette = palette_from(This);
    if (!colors || !actual)
        return E_INVALIDARG;
    EnterCriticalSection(&palette->lock);
    UINT n = count < palette->count ? count : palette->count;
    memcpy(colors, palette->colors, n * sizeof(WICColor));
    *actual = n;
    LeaveCriticalSection(&palette->lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE palette_is_bw(IWICPalette *This, BOOL *result)
{
    WicPalette *palette = palette_from(This);
    if (!result)
        return E_INVALIDARG;
    *result = palette->count == 2 && palette->colors[0] == 0xff000000 && palette->colors[1] == 0xffffffff;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE palette_is_gray(IWICPalette *This, BOOL *result)
{
    WicPalette *palette = palette_from(This);
    if (!result)
        return E_INVALIDARG;
    BOOL gray = palette->count != 0;
    for (UINT i = 0; i < palette->count && gray; i++) {
        WICColor c = palette->colors[i];
        UINT r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
        gray = r == g && g == b && (c >> 24) == 0xff;
    }
    *result = gray;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE palette_has_alpha(IWICPalette *This, BOOL *result)
{
    WicPalette *palette = palette_from(This);
    if (!result)
        return E_INVALIDARG;
    BOOL alpha = FALSE;
    for (UINT i = 0; i < palette->count && !alpha; i++)
        alpha = (palette->colors[i] >> 24) != 0xff;
    *result = alpha;
    return S_OK;
}

static const IWICPaletteVtbl g_palette_vtbl = {
    WIC_IUNKNOWN_FUNCS,
    palette_init_predefined,
    palette_init_custom,
    palette_init_bitmap,
    palette_init_palette,
    palette_get_type,
    palette_get_count,
    palette_get_colors,
    palette_is_bw,
    palette_is_gray,
    palette_has_alpha,
};

static const WicIfaceEntry g_palette_ifaces[] = {WIC_ENTRY(WicPalette, IID_IWICPalette, palette)};
static const WicClass g_palette_class = {palette_destroy, g_palette_ifaces, 1};

HRESULT WicPaletteCreate(IWICPalette **out)
{
    WicPalette *palette = WicNew(sizeof(*palette), &g_palette_class);
    if (!palette)
        return E_OUTOFMEMORY;
    InitializeCriticalSection(&palette->lock);
    WIC_SLOT(palette, palette, &g_palette_vtbl);
    *out = (IWICPalette *)&palette->palette;
    return S_OK;
}

HRESULT WicPaletteSet(IWICPalette *palette, const WICColor *colors, UINT count)
{
    return palette->lpVtbl->InitializeCustom(palette, (WICColor *)colors, count);
}

HRESULT WicPaletteGet(IWICPalette *palette, WICColor **colors, UINT *count)
{
    *colors = nullptr;
    *count = 0;
    UINT n = 0;
    HRESULT hr = palette->lpVtbl->GetColorCount(palette, &n);
    if (FAILED(hr) || !n)
        return hr;
    WICColor *list = WicAlloc((SIZE_T)n * sizeof(WICColor));
    if (!list)
        return E_OUTOFMEMORY;
    UINT got = 0;
    hr = palette->lpVtbl->GetColors(palette, n, list, &got);
    if (FAILED(hr)) {
        WicFree(list);
        return hr;
    }
    *colors = list;
    *count = got;
    return S_OK;
}
