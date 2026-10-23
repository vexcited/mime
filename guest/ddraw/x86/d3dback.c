/*
 * the direct3d 9 device that direct3d 1 to 7 draw through. d3d9.dll loads
 * on first use and the device presents nothing: its targets are surface
 * copies, read back when the cpu wants their pixels. every call runs with
 * ddraw's lock held.
 */
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/d3d9.h>
#include "d3dback.h"

struct DbDevice {
    IDirect3DDevice9 *device;
    HWND window;
    DbImage *target, *depth;
};

struct DbImage {
    DbDevice *dev;
    DWORD usage;
    D3DFORMAT format;
    UINT width, height, levels;
    IDirect3DBaseTexture9 *texture; /* a texture or a cube texture. */
    IDirect3DSurface9 *surface;     /* a target or depth surface that is not sampled. */
    IDirect3DSurface9 *staging;     /* system memory copy of a target, for reads and writes. */
};

static DbDevice g_dev;
static BOOL g_tried;

static LRESULT CALLBACK window_proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcW(window, msg, wparam, lparam);
}

DbDevice *DbDeviceGet(void)
{
    if (g_dev.device || g_tried)
        return g_dev.device ? &g_dev : nullptr;
    g_tried = TRUE;
    HMODULE d3d9 = LoadLibraryW(L"d3d9.dll");
    typedef IDirect3D9 *(WINAPI * CREATE)(UINT);
    CREATE create = d3d9 ? (CREATE)(void *)GetProcAddress(d3d9, "Direct3DCreate9") : nullptr;
    IDirect3D9 *d3d = create ? create(D3D_SDK_VERSION) : nullptr;
    if (!d3d)
        return nullptr;
    WNDCLASSW wc = {0, window_proc, 0, 0, GetModuleHandleW(nullptr)};
    wc.lpszClassName = L"MimeDirect3DImmediate";
    RegisterClassW(&wc);
    g_dev.window = CreateWindowExW(0, wc.lpszClassName, wc.lpszClassName, WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                                   wc.hInstance, nullptr);
    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = pp.BackBufferHeight = 1;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = g_dev.window;
    pp.Windowed = TRUE;
    /* direct3d 7 sets up the fpu itself or leaves it as the program set it. */
    HRESULT hr = d3d->lpVtbl->CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_dev.window,
                                           D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp,
                                           &g_dev.device);
    d3d->lpVtbl->Release(d3d);
    if (FAILED(hr)) {
        g_dev.device = nullptr;
        return nullptr;
    }
    g_dev.device->lpVtbl->BeginScene(g_dev.device);
    return &g_dev;
}

/* ------------------------------------------------------------------ formats */

static BOOL masks(const DDPIXELFORMAT *pf, DWORD bits, DWORD r, DWORD g, DWORD b, DWORD a)
{
    return pf->dwRGBBitCount == bits && pf->dwRBitMask == r && pf->dwGBitMask == g && pf->dwBBitMask == b &&
           ((pf->dwFlags & DDPF_ALPHAPIXELS) ? pf->dwRGBAlphaBitMask : 0) == a;
}

DWORD DbFormatOf(const DDPIXELFORMAT *pf, DWORD usage)
{
    DWORD flags = pf->dwFlags;
    if (usage & DB_DEPTH) {
        if (!(flags & DDPF_ZBUFFER))
            return 0;
        DWORD z = pf->dwZBitMask, s = (flags & DDPF_STENCILBUFFER) ? pf->dwStencilBitMask : 0;
        if (pf->dwZBufferBitDepth <= 16)
            return s ? D3DFMT_D15S1 : D3DFMT_D16;
        if (z == 0xFFFFFFFF || (!z && !s))
            return D3DFMT_D32;
        return s ? D3DFMT_D24S8 : D3DFMT_D24X8;
    }
    if (flags & DDPF_FOURCC) {
        switch (pf->dwFourCC) {
        case D3DFMT_DXT1:
        case D3DFMT_DXT2:
        case D3DFMT_DXT3:
        case D3DFMT_DXT4:
        case D3DFMT_DXT5:
            return (usage & DB_TARGET) ? 0 : pf->dwFourCC;
        }
        return 0;
    }
    if (flags & DDPF_LUMINANCE) {
        if (usage & DB_TARGET)
            return 0;
        if (!(flags & DDPF_ALPHAPIXELS) && pf->dwRGBBitCount == 8 && pf->dwRBitMask == 0xFF)
            return D3DFMT_L8;
        if ((flags & DDPF_ALPHAPIXELS) && pf->dwRGBBitCount == 16 && pf->dwRBitMask == 0xFF &&
            pf->dwRGBAlphaBitMask == 0xFF00)
            return D3DFMT_A8L8;
        if ((flags & DDPF_ALPHAPIXELS) && pf->dwRGBBitCount == 8 && pf->dwRBitMask == 0xF &&
            pf->dwRGBAlphaBitMask == 0xF0)
            return D3DFMT_A4L4;
        return 0;
    }
    if (flags & DDPF_BUMPDUDV) {
        if (!(flags & DDPF_BUMPLUMINANCE) && pf->dwRGBBitCount == 16 && pf->dwRBitMask == 0xFF &&
            pf->dwGBitMask == 0xFF00 && !(usage & DB_TARGET))
            return D3DFMT_V8U8;
        return 0;
    }
    if (flags == DDPF_ALPHA && pf->dwAlphaBitDepth == 8)
        return D3DFMT_A8;
    if (!(flags & DDPF_RGB) || (flags & (DDPF_PALETTEINDEXED1 | DDPF_PALETTEINDEXED2 | DDPF_PALETTEINDEXED4 |
                                         DDPF_PALETTEINDEXED8 | DDPF_PALETTEINDEXEDTO8)))
        return 0;
    if (masks(pf, 32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000))
        return D3DFMT_A8R8G8B8;
    if (masks(pf, 32, 0xFF0000, 0xFF00, 0xFF, 0))
        return D3DFMT_X8R8G8B8;
    if (masks(pf, 16, 0xF800, 0x7E0, 0x1F, 0))
        return D3DFMT_R5G6B5;
    if (masks(pf, 16, 0x7C00, 0x3E0, 0x1F, 0))
        return D3DFMT_X1R5G5B5;
    if (masks(pf, 16, 0x7C00, 0x3E0, 0x1F, 0x8000))
        return D3DFMT_A1R5G5B5;
    if (masks(pf, 16, 0xF00, 0xF0, 0xF, 0xF000))
        return D3DFMT_A4R4G4B4;
    if (masks(pf, 16, 0xF00, 0xF0, 0xF, 0))
        return D3DFMT_X4R4G4B4;
    return 0;
}

/* ------------------------------------------------------------------ images */

static UINT bytes_of(D3DFORMAT format, UINT width)
{
    switch ((DWORD)format) {
    case D3DFMT_DXT1:
        return (width + 3) / 4 * 8;
    case D3DFMT_DXT2:
    case D3DFMT_DXT3:
    case D3DFMT_DXT4:
    case D3DFMT_DXT5:
        return (width + 3) / 4 * 16;
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
    case D3DFMT_D24S8:
    case D3DFMT_D24X8:
    case D3DFMT_D32:
        return width * 4;
    case D3DFMT_L8:
    case D3DFMT_A8:
    case D3DFMT_A4L4:
        return width;
    default:
        return width * 2;
    }
}

static UINT rows_of(D3DFORMAT format, UINT height)
{
    switch ((DWORD)format) {
    case D3DFMT_DXT1:
    case D3DFMT_DXT2:
    case D3DFMT_DXT3:
    case D3DFMT_DXT4:
    case D3DFMT_DXT5:
        return (height + 3) / 4;
    default:
        return height;
    }
}

DbImage *DbImageCreate(DbDevice *dev, DWORD usage, UINT width, UINT height, UINT levels, DWORD format)
{
    DbImage *image = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*image));
    if (!image)
        return nullptr;
    image->dev = dev;
    image->usage = usage;
    image->format = (D3DFORMAT)format;
    image->width = width;
    image->height = height;
    image->levels = levels ? levels : 1;
    IDirect3DDevice9 *d = dev->device;
    HRESULT hr;
    if (usage & DB_DEPTH) {
        hr = d->lpVtbl->CreateDepthStencilSurface(d, width, height, image->format, D3DMULTISAMPLE_NONE, 0, FALSE,
                                                  &image->surface, nullptr);
    } else if (usage & DB_TARGET) {
        /* a target also sampled is a texture whose first level is drawn to. */
        if (usage & DB_TEXTURE)
            hr = d->lpVtbl->CreateTexture(d, width, height, 1, D3DUSAGE_RENDERTARGET, image->format, D3DPOOL_DEFAULT,
                                          (IDirect3DTexture9 **)&image->texture, nullptr);
        else
            hr = d->lpVtbl->CreateRenderTarget(d, width, height, image->format, D3DMULTISAMPLE_NONE, 0, FALSE,
                                               &image->surface, nullptr);
        if (SUCCEEDED(hr) && image->texture)
            hr = ((IDirect3DTexture9 *)image->texture)->lpVtbl->GetSurfaceLevel((IDirect3DTexture9 *)image->texture,
                                                                               0, &image->surface);
        if (SUCCEEDED(hr))
            hr = d->lpVtbl->CreateOffscreenPlainSurface(d, width, height, image->format, D3DPOOL_SYSTEMMEM,
                                                        &image->staging, nullptr);
    } else if (usage & DB_CUBE) {
        hr = d->lpVtbl->CreateCubeTexture(d, width, image->levels, 0, image->format, D3DPOOL_MANAGED,
                                          (IDirect3DCubeTexture9 **)&image->texture, nullptr);
    } else {
        hr = d->lpVtbl->CreateTexture(d, width, height, image->levels, 0, image->format, D3DPOOL_MANAGED,
                                      (IDirect3DTexture9 **)&image->texture, nullptr);
    }
    if (FAILED(hr)) {
        DbImageDestroy(image);
        return nullptr;
    }
    return image;
}

void DbImageDestroy(DbImage *image)
{
    if (!image)
        return;
    DbDevice *dev = image->dev;
    if (dev->target == image || dev->depth == image) {
        if (dev->depth == image) {
            dev->device->lpVtbl->SetDepthStencilSurface(dev->device, nullptr);
            dev->depth = nullptr;
        }
        if (dev->target == image)
            dev->target = nullptr;
    }
    if (image->staging)
        image->staging->lpVtbl->Release(image->staging);
    if (image->surface)
        image->surface->lpVtbl->Release(image->surface);
    if (image->texture)
        image->texture->lpVtbl->Release(image->texture);
    HeapFree(GetProcessHeap(), 0, image);
}

DWORD DbImageFormat(const DbImage *image)
{
    return image->format;
}

static void copy_rows(BYTE *dst, UINT dst_pitch, const BYTE *src, UINT src_pitch, UINT bytes, UINT rows)
{
    for (UINT y = 0; y < rows; y++)
        memcpy(dst + (SIZE_T)y * dst_pitch, src + (SIZE_T)y * src_pitch, bytes);
}

BOOL DbImageWrite(DbImage *image, UINT face, UINT level, const RECT *rect, const void *data, UINT pitch)
{
    RECT full = {0, 0, (LONG)(image->width >> level ? image->width >> level : 1),
                 (LONG)(image->height >> level ? image->height >> level : 1)};
    RECT r = rect ? *rect : full;
    UINT bytes = bytes_of(image->format, (UINT)(r.right - r.left)), rows = rows_of(image->format, (UINT)(r.bottom - r.top));
    D3DLOCKED_RECT locked;
    if (image->usage & DB_TARGET) {
        /* goes through the system memory copy, which UpdateSurface copies to the target. */
        if (FAILED(image->staging->lpVtbl->LockRect(image->staging, &locked, &r, 0)))
            return FALSE;
        copy_rows(locked.pBits, (UINT)locked.Pitch, data, pitch, bytes, rows);
        image->staging->lpVtbl->UnlockRect(image->staging);
        POINT at = {r.left, r.top};
        IDirect3DDevice9 *d = image->dev->device;
        return SUCCEEDED(d->lpVtbl->UpdateSurface(d, image->staging, &r, image->surface, &at));
    }
    if (image->usage & DB_DEPTH)
        return FALSE;
    HRESULT hr;
    if (image->usage & DB_CUBE) {
        IDirect3DCubeTexture9 *cube = (IDirect3DCubeTexture9 *)image->texture;
        hr = cube->lpVtbl->LockRect(cube, (D3DCUBEMAP_FACES)face, level, &locked, &r, 0);
        if (FAILED(hr))
            return FALSE;
        copy_rows(locked.pBits, (UINT)locked.Pitch, data, pitch, bytes, rows);
        cube->lpVtbl->UnlockRect(cube, (D3DCUBEMAP_FACES)face, level);
        return TRUE;
    }
    IDirect3DTexture9 *texture = (IDirect3DTexture9 *)image->texture;
    if (FAILED(texture->lpVtbl->LockRect(texture, level, &locked, &r, 0)))
        return FALSE;
    copy_rows(locked.pBits, (UINT)locked.Pitch, data, pitch, bytes, rows);
    texture->lpVtbl->UnlockRect(texture, level);
    return TRUE;
}

BOOL DbImageRead(DbImage *image, const RECT *rect, void *data, UINT pitch)
{
    if (!(image->usage & DB_TARGET))
        return FALSE;
    IDirect3DDevice9 *d = image->dev->device;
    if (FAILED(d->lpVtbl->GetRenderTargetData(d, image->surface, image->staging)))
        return FALSE;
    RECT full = {0, 0, (LONG)image->width, (LONG)image->height};
    RECT r = rect ? *rect : full;
    D3DLOCKED_RECT locked;
    if (FAILED(image->staging->lpVtbl->LockRect(image->staging, &locked, &r, D3DLOCK_READONLY)))
        return FALSE;
    copy_rows(data, pitch, locked.pBits, (UINT)locked.Pitch, bytes_of(image->format, (UINT)(r.right - r.left)),
              (UINT)(r.bottom - r.top));
    image->staging->lpVtbl->UnlockRect(image->staging);
    return TRUE;
}

/* ------------------------------------------------------------------ state */

void DbSetTargets(DbDevice *dev, DbImage *target, DbImage *depth)
{
    IDirect3DDevice9 *d = dev->device;
    if (target && target != dev->target) {
        d->lpVtbl->SetRenderTarget(d, 0, target->surface);
        dev->target = target;
    }
    if (depth != dev->depth) {
        d->lpVtbl->SetDepthStencilSurface(d, depth ? depth->surface : nullptr);
        dev->depth = depth;
    }
}

void DbSetRenderState(DbDevice *dev, DWORD state, DWORD value)
{
    dev->device->lpVtbl->SetRenderState(dev->device, (D3DRENDERSTATETYPE)state, value);
}

void DbSetTextureStageState(DbDevice *dev, DWORD stage, DWORD state, DWORD value)
{
    dev->device->lpVtbl->SetTextureStageState(dev->device, stage, (D3DTEXTURESTAGESTATETYPE)state, value);
}

void DbSetSamplerState(DbDevice *dev, DWORD stage, DWORD state, DWORD value)
{
    dev->device->lpVtbl->SetSamplerState(dev->device, stage, (D3DSAMPLERSTATETYPE)state, value);
}

void DbSetTexture(DbDevice *dev, DWORD stage, DbImage *image)
{
    dev->device->lpVtbl->SetTexture(dev->device, stage, image ? image->texture : nullptr);
}

void DbSetTransform(DbDevice *dev, DWORD state, const float *matrix)
{
    dev->device->lpVtbl->SetTransform(dev->device, (D3DTRANSFORMSTATETYPE)state, (const D3DMATRIX *)matrix);
}

void DbSetViewport(DbDevice *dev, DWORD x, DWORD y, DWORD width, DWORD height, float min_z, float max_z)
{
    D3DVIEWPORT9 vp = {x, y, width, height, min_z, max_z};
    dev->device->lpVtbl->SetViewport(dev->device, &vp);
}

void DbSetMaterial(DbDevice *dev, const void *material)
{
    dev->device->lpVtbl->SetMaterial(dev->device, material);
}

void DbSetLight(DbDevice *dev, DWORD index, const void *light)
{
    dev->device->lpVtbl->SetLight(dev->device, index, light);
}

void DbLightEnable(DbDevice *dev, DWORD index, BOOL enable)
{
    dev->device->lpVtbl->LightEnable(dev->device, index, enable);
}

void DbSetClipPlane(DbDevice *dev, DWORD index, const float *plane)
{
    dev->device->lpVtbl->SetClipPlane(dev->device, index, plane);
}

/* ------------------------------------------------------------------ drawing */

HRESULT DbClear(DbDevice *dev, DWORD count, const void *rects, DWORD flags, DWORD color, float z, DWORD stencil)
{
    return dev->device->lpVtbl->Clear(dev->device, count, rects, flags, color, z, stencil);
}

HRESULT DbDraw(DbDevice *dev, DWORD type, DWORD primitives, const void *vertices, DWORD stride, DWORD fvf)
{
    IDirect3DDevice9 *d = dev->device;
    d->lpVtbl->SetFVF(d, fvf);
    return d->lpVtbl->DrawPrimitiveUP(d, (D3DPRIMITIVETYPE)type, primitives, vertices, stride);
}

HRESULT DbDrawIndexed(DbDevice *dev, DWORD type, DWORD primitives, DWORD vertex_count, const WORD *indices,
                      const void *vertices, DWORD stride, DWORD fvf)
{
    IDirect3DDevice9 *d = dev->device;
    d->lpVtbl->SetFVF(d, fvf);
    return d->lpVtbl->DrawIndexedPrimitiveUP(d, (D3DPRIMITIVETYPE)type, 0, vertex_count, primitives, indices,
                                             D3DFMT_INDEX16, vertices, stride);
}
