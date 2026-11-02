/*
 * wgl: pixel formats, contexts and presenting windows.
 *
 * a window's default framebuffer is a surface: a back buffer the host keeps and copies to a layer over the window's
 * client area at each swap. the surface follows the window's size and position, checked when a frame starts and when
 * it is presented.
 */
#include "opengl32.h"

#include <mime/pixelformat.h>

#define MAX_FRAMES_IN_FLIGHT 2

static GlSurface *g_surfaces;

static int dc_pixel_format(HDC dc)
{
    DWORD v = 0;
    if (!NtGdiGetDCDword(dc, MIME_DCDW_PIXELFORMAT, &v))
        return 0;
    return (int)v;
}

static void set_last_error(DWORD error)
{
    NtCurrentTeb()->LastErrorValue = error;
}

/* ---------------------------------------------------------------- pixel formats */

GLAPI int WINAPI wglDescribePixelFormat(HDC dc, int format, UINT size, PIXELFORMATDESCRIPTOR *pfd)
{
    if (!pfd)
        return MIME_PIXEL_FORMAT_COUNT;
    if (format < 1 || format > MIME_PIXEL_FORMAT_COUNT || size < sizeof(PIXELFORMATDESCRIPTOR)) {
        set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    MimeDescribePixelFormat(format, pfd);
    return MIME_PIXEL_FORMAT_COUNT;
}

GLAPI int WINAPI wglChoosePixelFormat(HDC dc, const PIXELFORMATDESCRIPTOR *want)
{
    if (!want) {
        set_last_error(ERROR_INVALID_PARAMETER);
        return 0;
    }
    return MimeChoosePixelFormat(want);
}

GLAPI int WINAPI wglGetPixelFormat(HDC dc)
{
    return dc_pixel_format(dc);
}

GLAPI BOOL WINAPI wglSetPixelFormat(HDC dc, int format, const PIXELFORMATDESCRIPTOR *pfd)
{
    if (format < 1 || format > MIME_PIXEL_FORMAT_COUNT) {
        set_last_error(ERROR_INVALID_PIXEL_FORMAT);
        return FALSE;
    }
    return NtGdiSetPixelFormat(dc, format);
}

static BOOL gpu_format(int format)
{
    return format >= 1 && format <= MIME_PIXEL_FORMAT_COUNT &&
           !(MimePixelFormats[format - 1].Flags & PFD_GENERIC_FORMAT);
}

static int attrib_of(int format, int attrib, BOOL *ok)
{
    PIXELFORMATDESCRIPTOR p;
    MimeDescribePixelFormat(format, &p);
    *ok = TRUE;
    switch (attrib) {
    case WGL_NUMBER_PIXEL_FORMATS_ARB:
        return MIME_PIXEL_FORMAT_COUNT;
    case WGL_DRAW_TO_WINDOW_ARB:
        return !!(p.dwFlags & PFD_DRAW_TO_WINDOW);
    case WGL_DRAW_TO_BITMAP_ARB:
        return !!(p.dwFlags & PFD_DRAW_TO_BITMAP);
    case WGL_ACCELERATION_ARB:
        return (p.dwFlags & PFD_GENERIC_FORMAT) ? WGL_NO_ACCELERATION_ARB : WGL_FULL_ACCELERATION_ARB;
    case WGL_SWAP_METHOD_ARB:
        return WGL_SWAP_COPY_ARB;
    case WGL_SUPPORT_GDI_ARB:
        return !!(p.dwFlags & PFD_SUPPORT_GDI);
    case WGL_SUPPORT_OPENGL_ARB:
        return !!(p.dwFlags & PFD_SUPPORT_OPENGL);
    case WGL_DOUBLE_BUFFER_ARB:
        return !!(p.dwFlags & PFD_DOUBLEBUFFER);
    case WGL_STEREO_ARB:
        return 0;
    case WGL_PIXEL_TYPE_ARB:
        return p.iPixelType == PFD_TYPE_RGBA ? WGL_TYPE_RGBA_ARB : WGL_TYPE_COLORINDEX_ARB;
    case WGL_COLOR_BITS_ARB:
        return p.cColorBits;
    case WGL_RED_BITS_ARB:
        return p.cRedBits;
    case WGL_RED_SHIFT_ARB:
        return p.cRedShift;
    case WGL_GREEN_BITS_ARB:
        return p.cGreenBits;
    case WGL_GREEN_SHIFT_ARB:
        return p.cGreenShift;
    case WGL_BLUE_BITS_ARB:
        return p.cBlueBits;
    case WGL_BLUE_SHIFT_ARB:
        return p.cBlueShift;
    case WGL_ALPHA_BITS_ARB:
        return p.cAlphaBits;
    case WGL_ALPHA_SHIFT_ARB:
        return p.cAlphaShift;
    case WGL_ACCUM_BITS_ARB:
        return p.cAccumBits;
    case WGL_DEPTH_BITS_ARB:
        return p.cDepthBits;
    case WGL_STENCIL_BITS_ARB:
        return p.cStencilBits;
    case WGL_AUX_BUFFERS_ARB:
    case WGL_SAMPLE_BUFFERS_ARB:
    case WGL_SAMPLES_ARB:
    case WGL_NUMBER_OVERLAYS_ARB:
    case WGL_NUMBER_UNDERLAYS_ARB:
        return 0;
    case WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB:
        return gpu_format(format);
    default:
        *ok = FALSE;
        return 0;
    }
}

GLAPI BOOL WINAPI wglGetPixelFormatAttribivARB(HDC dc, int format, int layer, UINT count, const int *attribs,
                                                int *values)
{
    if ((format < 1 || format > MIME_PIXEL_FORMAT_COUNT) && !(count == 1 && attribs[0] == WGL_NUMBER_PIXEL_FORMATS_ARB)) {
        set_last_error(ERROR_INVALID_PIXEL_FORMAT);
        return FALSE;
    }
    for (UINT i = 0; i < count; i++) {
        BOOL ok;
        values[i] = attrib_of(format < 1 ? 1 : format, attribs[i], &ok);
        if (!ok) {
            set_last_error(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
    }
    return TRUE;
}

GLAPI BOOL WINAPI wglGetPixelFormatAttribfvARB(HDC dc, int format, int layer, UINT count, const int *attribs,
                                                FLOAT *values)
{
    int tmp[64];
    if (count > 64 || !wglGetPixelFormatAttribivARB(dc, format, layer, count, attribs, tmp))
        return FALSE;
    for (UINT i = 0; i < count; i++)
        values[i] = (FLOAT)tmp[i];
    return TRUE;
}

/* the formats meeting every integer requirement, the closest first. */
GLAPI BOOL WINAPI wglChoosePixelFormatARB(HDC dc, const int *iattribs, const FLOAT *fattribs, UINT max, int *formats,
                                           UINT *count)
{
    int scores[MIME_PIXEL_FORMAT_COUNT + 1];
    int found[MIME_PIXEL_FORMAT_COUNT];
    UINT n = 0;
    for (int f = 1; f <= MIME_PIXEL_FORMAT_COUNT; f++) {
        BOOL match = TRUE;
        int score = 0;
        for (const int *a = iattribs; a && a[0] && match; a += 2) {
            BOOL ok;
            int v = attrib_of(f, a[0], &ok);
            if (!ok)
                continue;
            switch (a[0]) {
            case WGL_COLOR_BITS_ARB:
            case WGL_RED_BITS_ARB:
            case WGL_GREEN_BITS_ARB:
            case WGL_BLUE_BITS_ARB:
            case WGL_ALPHA_BITS_ARB:
            case WGL_DEPTH_BITS_ARB:
            case WGL_STENCIL_BITS_ARB:
            case WGL_ACCUM_BITS_ARB:
            case WGL_SAMPLES_ARB:
            case WGL_SAMPLE_BUFFERS_ARB:
            case WGL_AUX_BUFFERS_ARB:
                if (v < a[1])
                    match = FALSE;
                score += v - a[1];
                break;
            case WGL_SWAP_METHOD_ARB:
                break;
            default:
                if (v != a[1])
                    match = FALSE;
                break;
            }
        }
        if (match) {
            scores[f] = score;
            UINT i = n++;
            while (i > 0 && scores[found[i - 1]] > score) {
                found[i] = found[i - 1];
                i--;
            }
            found[i] = f;
        }
    }
    for (UINT i = 0; i < n && i < max; i++)
        formats[i] = found[i];
    *count = n < max ? n : max;
    return TRUE;
}

/* ---------------------------------------------------------------- surfaces */

static void format_buffers(int pixel_format, uint32_t *depth, uint32_t *stencil)
{
    PIXELFORMATDESCRIPTOR p;
    *depth = *stencil = 0;
    if (pixel_format < 1 || pixel_format > MIME_PIXEL_FORMAT_COUNT)
        return;
    MimeDescribePixelFormat(pixel_format, &p);
    if (p.cDepthBits && p.cStencilBits)
        *depth = *stencil = 260; /* MTLPixelFormatDepth32Float_Stencil8 */
    else if (p.cDepthBits)
        *depth = 252; /* MTLPixelFormatDepth32Float */
    else if (p.cStencilBits)
        *stencil = 253; /* MTLPixelFormatStencil8 */
}

GlSurface *gl_surface_for(HDC dc, int pixel_format)
{
    MIME_GPU_SURFACE info;
    if (!NtGdiMimeGpuGetSurface(dc, &info))
        return nullptr;
    gl_device_lock();
    GlSurface *s = g_surfaces;
    while (s && s->hwnd != info.Window)
        s = s->next;
    if (!s) {
        s = gl_alloc(sizeof(GlSurface));
        if (s) {
            s->hwnd = info.Window;
            s->id = gl_new_id(ID_SURFACE);
            s->sync = TRUE;
            format_buffers(pixel_format, &s->depth_format, &s->stencil_format);
            s->next = g_surfaces;
            g_surfaces = s;
        }
    }
    gl_device_unlock();
    if (s)
        gl_surface_refresh(s, dc);
    return s;
}

/* sends the window's current region to the host when it changed. */
void gl_surface_refresh(GlSurface *s, HDC dc)
{
    MIME_GPU_SURFACE info;
    if (!NtGdiMimeGpuGetSurface(dc, &info) || info.Window != s->hwnd)
        return;
    if (s->configured && !memcmp(&info, &s->info, sizeof(info)))
        return;
    s->info = info;
    s->configured = TRUE;
    gl_device_lock();
    MimeGpuSurfaceConfig *c = gl_emit(MIME_GPU_SURFACE_CONFIG, sizeof(MimeGpuSurfaceConfig));
    c->id = s->id;
    c->window_id = info.HostWindow;
    c->x = info.X;
    c->y = info.Y;
    c->width = info.Width ? info.Width : 1;
    c->height = info.Height ? info.Height : 1;
    c->color_format = 80; /* MTLPixelFormatBGRA8Unorm */
    c->depth_format = (uint16_t)s->depth_format;
    c->stencil_format = (uint16_t)s->stencil_format;
    c->sync = (uint16_t)s->sync;
    c->samples = 1;
    c->source = (uint32_t)(ULONG_PTR)info.Window;
    c->order = info.Order;
    c->visible[0] = info.Visible.left;
    c->visible[1] = info.Visible.top;
    c->visible[2] = info.Visible.right;
    c->visible[3] = info.Visible.bottom;
    gl_device_unlock();
}

/* ---------------------------------------------------------------- contexts */

static HGLRC create_context(HDC dc, GlContext *share, int major, int minor, int profile)
{
    int format = dc_pixel_format(dc);
    if (!format) {
        set_last_error(ERROR_INVALID_PIXEL_FORMAT);
        return nullptr;
    }
    if (!gpu_format(format) || !gl_device_open()) {
        set_last_error(ERROR_NOT_SUPPORTED);
        return nullptr;
    }
    GlContext *ctx = gl_alloc(sizeof(GlContext));
    if (!ctx)
        return nullptr;
    if (share) {
        ctx->shared = share->shared;
        __atomic_fetch_add(&ctx->shared->refs, 1, __ATOMIC_ACQ_REL);
    } else {
        ctx->shared = gl_alloc(sizeof(GlShared));
        ctx->shared->refs = 1;
    }
    ctx->pixel_format = format;
    MimeDescribePixelFormat(format, &ctx->pfd);
    ctx->major = major;
    ctx->minor = minor;
    ctx->profile = profile;
    gl_context_init(ctx);
    return (HGLRC)ctx;
}

GLAPI HGLRC WINAPI wglCreateContext(HDC dc)
{
    return create_context(dc, nullptr, 3, 0, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB);
}

GLAPI HGLRC WINAPI wglCreateLayerContext(HDC dc, int layer)
{
    if (layer) {
        set_last_error(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    return wglCreateContext(dc);
}

GLAPI HGLRC WINAPI wglCreateContextAttribsARB(HDC dc, HGLRC share, const int *attribs)
{
    int major = 1, minor = 0, profile = WGL_CONTEXT_CORE_PROFILE_BIT_ARB;
    for (const int *a = attribs; a && a[0]; a += 2) {
        switch (a[0]) {
        case WGL_CONTEXT_MAJOR_VERSION_ARB:
            major = a[1];
            break;
        case WGL_CONTEXT_MINOR_VERSION_ARB:
            minor = a[1];
            break;
        case WGL_CONTEXT_PROFILE_MASK_ARB:
            profile = a[1];
            break;
        default:
            break;
        }
    }
    if (major > 4 || (major == 4 && minor > 1)) {
        set_last_error(ERROR_INVALID_VERSION_ARB);
        return nullptr;
    }
    /* below 3.2 there are no profiles: the context is compatible. */
    if (major < 3 || (major == 3 && minor < 2)) {
        profile = WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB;
        major = 3;
        minor = 0;
    }
    return create_context(dc, (GlContext *)share, major, minor, profile);
}

static void release_current(void)
{
    GlContext *ctx = gl_current();
    if (!ctx)
        return;
    gl_device_lock();
    gl_flush_clears(ctx);
    gl_end_pass(ctx);
    MimeGpuCmd *f = gl_emit(MIME_GPU_FLUSH, sizeof(MimeGpuCmd));
    (void)f;
    gl_publish();
    gl_device_unlock();
    ctx->thread = 0;
    NtCurrentTeb()->glCurrentRC = nullptr;
}

GLAPI BOOL WINAPI wglMakeCurrent(HDC dc, HGLRC rc)
{
    GlContext *ctx = (GlContext *)rc;
    DWORD self = (DWORD)(ULONG_PTR)NtCurrentTeb()->ClientId.UniqueThread;
    if (ctx && ctx->thread && ctx->thread != self) {
        set_last_error(ERROR_BUSY);
        return FALSE;
    }
    if (gl_current() == ctx && (!ctx || ctx->dc == dc))
        return TRUE;
    release_current();
    if (!ctx)
        return TRUE;
    int format = dc_pixel_format(dc);
    if (format != ctx->pixel_format) {
        set_last_error(ERROR_INVALID_PIXEL_FORMAT);
        return FALSE;
    }
    GlSurface *s = gl_surface_for(dc, format);
    if (!s) {
        set_last_error(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    ctx->dc = dc;
    ctx->surface = s;
    ctx->thread = self;
    ctx->dirty = DIRTY_ALL;
    ctx->pass_open = FALSE;
    gl_invalidate_bindings(ctx);
    if (!ctx->made_current) {
        ctx->made_current = TRUE;
        ctx->viewport[2] = ctx->scissor[2] = (GLint)s->info.Width;
        ctx->viewport[3] = ctx->scissor[3] = (GLint)s->info.Height;
    }
    NtCurrentTeb()->glCurrentRC = ctx;
    return TRUE;
}

GLAPI BOOL WINAPI wglMakeContextCurrentARB(HDC draw, HDC read, HGLRC rc)
{
    return wglMakeCurrent(draw, rc);
}

GLAPI HGLRC WINAPI wglGetCurrentContext(void)
{
    return (HGLRC)gl_current();
}

GLAPI HDC WINAPI wglGetCurrentDC(void)
{
    GlContext *ctx = gl_current();
    return ctx ? ctx->dc : nullptr;
}

GLAPI HDC WINAPI wglGetCurrentReadDCARB(void)
{
    return wglGetCurrentDC();
}

GLAPI BOOL WINAPI wglDeleteContext(HGLRC rc)
{
    GlContext *ctx = (GlContext *)rc;
    if (!ctx) {
        set_last_error(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (gl_current() == ctx)
        release_current();
    else if (ctx->thread) {
        set_last_error(ERROR_BUSY);
        return FALSE;
    }
    /* the share group's objects stay with the other contexts; their memory is kept for now. */
    __atomic_fetch_sub(&ctx->shared->refs, 1, __ATOMIC_ACQ_REL);
    gl_queries_destroy(ctx);
    gl_ff_destroy(ctx);
    gl_free(ctx);
    return TRUE;
}

GLAPI BOOL WINAPI wglShareLists(HGLRC a, HGLRC b)
{
    GlContext *src = (GlContext *)a, *dst = (GlContext *)b;
    if (!src || !dst) {
        set_last_error(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (dst->shared == src->shared)
        return TRUE;
    __atomic_fetch_sub(&dst->shared->refs, 1, __ATOMIC_ACQ_REL);
    dst->shared = src->shared;
    __atomic_fetch_add(&dst->shared->refs, 1, __ATOMIC_ACQ_REL);
    return TRUE;
}

GLAPI BOOL WINAPI wglCopyContext(HGLRC src, HGLRC dst, UINT mask)
{
    set_last_error(ERROR_NOT_SUPPORTED);
    return FALSE;
}

/* ---------------------------------------------------------------- presenting */

GLAPI BOOL WINAPI wglSwapBuffers(HDC dc)
{
    GlContext *ctx = gl_current();
    GlSurface *s = nullptr;
    int format = dc_pixel_format(dc);
    if (ctx && ctx->dc == dc)
        s = ctx->surface;
    else if (format)
        s = gl_surface_for(dc, format);
    if (!s)
        return FALSE;
    gl_device_lock();
    if (ctx) {
        gl_flush_clears(ctx);
        gl_end_pass(ctx);
    }
    MimeGpuObject *p = gl_emit(MIME_GPU_PRESENT, sizeof(MimeGpuObject));
    p->id = s->id;
    uint64_t fence = gl_fence();
    gl_publish();
    s->frames[s->frame_index++ % 3] = fence;
    uint64_t wait = s->frame_index > MAX_FRAMES_IN_FLIGHT ? s->frames[(s->frame_index - 1 - MAX_FRAMES_IN_FLIGHT) % 3] : 0;
    gl_device_unlock();
    if (wait)
        gl_wait(wait);
    gl_surface_refresh(s, dc);
    return TRUE;
}

GLAPI BOOL WINAPI wglSwapLayerBuffers(HDC dc, UINT planes)
{
    return wglSwapBuffers(dc);
}

GLAPI BOOL WINAPI wglSwapIntervalEXT(int interval)
{
    GlContext *ctx = gl_current();
    if (!ctx || !ctx->surface)
        return FALSE;
    ctx->swap_interval = interval;
    BOOL sync = interval != 0;
    if (ctx->surface->sync != sync) {
        ctx->surface->sync = sync;
        ctx->surface->configured = FALSE;
        gl_surface_refresh(ctx->surface, ctx->dc);
    }
    return TRUE;
}

GLAPI int WINAPI wglGetSwapIntervalEXT(void)
{
    GlContext *ctx = gl_current();
    return ctx ? ctx->swap_interval : 0;
}

/* ---------------------------------------------------------------- extensions */

static constexpr char g_wgl_extensions[] = "WGL_ARB_extensions_string WGL_EXT_extensions_string WGL_ARB_pixel_format "
                                       "WGL_ARB_create_context WGL_ARB_create_context_profile WGL_EXT_swap_control "
                                       "WGL_ARB_make_current_read";

GLAPI const char *WINAPI wglGetExtensionsStringARB(HDC dc)
{
    return g_wgl_extensions;
}

GLAPI const char *WINAPI wglGetExtensionsStringEXT(void)
{
    return g_wgl_extensions;
}

GLAPI PROC WINAPI wglGetProcAddress(LPCSTR name)
{
    if (!name)
        return nullptr;
    ULONG lo = 0, hi = GlProcCount;
    while (lo < hi) {
        ULONG mid = (lo + hi) / 2;
        int c = strcmp(name, GlProcs[mid].Name);
        if (!c)
            return (PROC)(ULONG_PTR)GlProcs[mid].Function;
        if (c < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return nullptr;
}

GLAPI BOOL WINAPI wglUseFontBitmapsA(HDC dc, DWORD first, DWORD count, DWORD list_base)
{
    return FALSE;
}

GLAPI BOOL WINAPI wglUseFontBitmapsW(HDC dc, DWORD first, DWORD count, DWORD list_base)
{
    return FALSE;
}

GLAPI BOOL WINAPI wglUseFontOutlinesA(HDC dc, DWORD first, DWORD count, DWORD list_base, FLOAT deviation,
                                       FLOAT extrusion, int format, LPGLYPHMETRICSFLOAT metrics)
{
    return FALSE;
}

GLAPI BOOL WINAPI wglUseFontOutlinesW(HDC dc, DWORD first, DWORD count, DWORD list_base, FLOAT deviation,
                                       FLOAT extrusion, int format, LPGLYPHMETRICSFLOAT metrics)
{
    return FALSE;
}

GLAPI BOOL WINAPI wglDescribeLayerPlane(HDC dc, int format, int layer, UINT size, LPLAYERPLANEDESCRIPTOR desc)
{
    return FALSE;
}

GLAPI int WINAPI wglSetLayerPaletteEntries(HDC dc, int layer, int start, int count, const COLORREF *entries)
{
    return 0;
}

GLAPI int WINAPI wglGetLayerPaletteEntries(HDC dc, int layer, int start, int count, COLORREF *entries)
{
    return 0;
}

GLAPI BOOL WINAPI wglRealizeLayerPalette(HDC dc, int layer, BOOL realize)
{
    return FALSE;
}
