/*
 * the drag and drop helpers of the shell: the drag image of a drag source, the window that shows it over a
 * drop target, SHDoDragDrop and the format enumerator of SHCreateStdEnumFmtEtc.
 *
 * a source gives the helper its image, which goes into the data object as the two formats of Windows: "DragImageBits",
 * a header of the size, the hot spot, a bitmap handle and the color key, then the rows of 32 bit pixels bottom row
 * first with premultiplied alpha, and "DragContext", 16 bytes of which the second word says the bits are there. the
 * helper of a target takes them out of the data object, and shows them in a layered window that follows the pointer.
 * read from shell32.dll of build 26300 (CDragDropHelper). when the host takes a drag over, ole32 reads the same two
 * formats to give the compositor the image of the drag.
 */
#include "shell32_private.h"
#include <mime/win32/shobjidl_ui.h>
#include <mime/win32/oleidl.h>

#ifndef CLR_NONE
#define CLR_NONE 0xFFFFFFFFL
#endif

typedef struct _IMAGE_BITS {
    LONG Width, Height;
    LONG OffsetX, OffsetY;
    DWORD Bitmap;   /* the handle the image came from, a leftover */
    COLORREF Key;
    DWORD Pixels[1]; /* bottom row first, premultiplied */
} IMAGE_BITS;

#define IMAGE_HEADER 24

/* shell32 does not import ole32, which a program using the helpers has loaded already. */
static FARPROC ole_proc(const char *name)
{
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    return ole ? GetProcAddress(ole, name) : nullptr;
}

static void release_medium(STGMEDIUM *medium)
{
    typedef void(WINAPI * RELEASE)(STGMEDIUM *);
    RELEASE release = (RELEASE)(void *)ole_proc("ReleaseStgMedium");
    if (release)
        release(medium);
}

static HRESULT make_stream(IStream **stream)
{
    typedef HRESULT(WINAPI * CREATE)(HGLOBAL, BOOL, IStream **);
    CREATE create = (CREATE)(void *)ole_proc("CreateStreamOnHGlobal");
    return create ? create(nullptr, TRUE, stream) : E_NOTIMPL;
}

static UINT register_format(UINT *cache, LPCWSTR name)
{
    if (!*cache)
        *cache = RegisterClipboardFormatW(name);
    return *cache;
}

static UINT image_format(void)
{
    static UINT format;
    return register_format(&format, L"DragImageBits");
}

static UINT context_format(void)
{
    static UINT format;
    return register_format(&format, L"DragContext");
}

static UINT flags_format(void)
{
    static UINT format;
    return register_format(&format, L"DragSourceHelperFlags");
}

static UINT window_format(void)
{
    static UINT format;
    return register_format(&format, L"DragWindow");
}

static UINT layered_format(void)
{
    static UINT format;
    return register_format(&format, L"DragIsShowingLayered");
}

/* a DWORD in the data object as a format of its own, which the source reads to see what the helper shows. */
static void set_dword(IDataObject *data, UINT format, DWORD value)
{
    if (!data || !format)
        return;
    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    DWORD *p = global ? GlobalLock(global) : nullptr;
    if (!p) {
        if (global)
            GlobalFree(global);
        return;
    }
    *p = value;
    GlobalUnlock(global);
    FORMATETC fetc = {(CLIPFORMAT)format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium = {};
    medium.tymed = TYMED_HGLOBAL;
    medium.hGlobal = global;
    if (FAILED(data->lpVtbl->SetData(data, &fetc, &medium, TRUE)))
        GlobalFree(global);
}

/*
 * the pixels of a bitmap as the helper keeps them (read: _ApplyAlphaToDragBitmap). a pixel with no alpha and some
 * color gets full alpha; pixels near the color key (each channel within 5) go clear; an image over 300 pixels
 * either way fades from its hot spot to its far edges; then the alpha is cut to three quarters and the colors
 * premultiplied. the fade and the three quarters are the default of Windows as read; the guesses are the rounding.
 */
static IMAGE_BITS *bits_from_bitmap(HBITMAP bitmap, SIZE size, POINT offset, COLORREF key)
{
    BITMAP info;
    if (!bitmap || !GetObjectW(bitmap, sizeof(info), &info) || info.bmWidth <= 0 || info.bmHeight <= 0)
        return nullptr;
    LONG width = info.bmWidth, height = info.bmHeight;
    SIZE_T count = (SIZE_T)width * height;
    IMAGE_BITS *bits = ShAlloc(sizeof(IMAGE_BITS) + count * 4);
    if (!bits)
        return nullptr;
    bits->Width = width;
    bits->Height = height;
    bits->OffsetX = offset.x;
    bits->OffsetY = offset.y;
    bits->Bitmap = 0;
    bits->Key = key;
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    HDC dc = GetDC(nullptr);
    int lines = GetDIBits(dc, bitmap, 0, (UINT)height, bits->Pixels, &bmi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (lines != height) {
        ShFree(bits);
        return nullptr;
    }
    (void)size;
    LONG max_x = offset.x > width - offset.x ? offset.x : width - offset.x;
    LONG max_y = offset.y > height - offset.y ? offset.y : height - offset.y;
    BOOL fade = (width > 300 || height > 300) && max_x > 0 && max_y > 0;
    for (LONG row = 0; row < height; row++) {
        LONG top_row = height - 1 - row;
        for (LONG x = 0; x < width; x++) {
            DWORD *pixel = &bits->Pixels[(SIZE_T)row * width + x];
            DWORD p = *pixel;
            if (!(p >> 24) && p)
                p |= 0xFF000000u;
            if (key != CLR_NONE) {
                LONG b = (LONG)(p & 0xFF), g = (LONG)((p >> 8) & 0xFF), r = (LONG)((p >> 16) & 0xFF);
                if (b - (LONG)GetBValue(key) >= -5 && b - (LONG)GetBValue(key) <= 5 &&
                    g - (LONG)GetGValue(key) >= -5 && g - (LONG)GetGValue(key) <= 5 &&
                    r - (LONG)GetRValue(key) >= -5 && r - (LONG)GetRValue(key) <= 5)
                    p = 0;
            }
            if (fade && (p >> 24)) {
                LONG dx = x - offset.x, dy = top_row + 1 - offset.y;
                if (dx < 0)
                    dx = -dx;
                if (dy < 0)
                    dy = -dy;
                LONG64 kx = 100000 - 100000LL * dx / max_x, ky = 100000 - 100000LL * dy / max_y;
                LONG64 a = kx * ky * (p >> 24) / 10000000000LL;
                a = a < 0 ? 0 : a > 255 ? 255 : a;
                p = (p & 0x00FFFFFFu) | (DWORD)a << 24;
            }
            DWORD a = (p >> 24) * 3 >> 2;
            DWORD r = (((p >> 16) & 0xFF) * a + 255) >> 8, g = (((p >> 8) & 0xFF) * a + 255) >> 8,
                  b = ((p & 0xFF) * a + 255) >> 8;
            *pixel = a << 24 | r << 16 | g << 8 | b;
        }
    }
    return bits;
}

/* puts the image into the data object as the two formats; the object keeps what it takes, the helper frees the rest. */
static HRESULT give_image(IDataObject *data, IMAGE_BITS *bits, DWORD flags)
{
    SIZE_T size = IMAGE_HEADER + (SIZE_T)bits->Width * bits->Height * 4;
    HGLOBAL global = GlobalAlloc(GHND, size);
    BYTE *p = global ? GlobalLock(global) : nullptr;
    if (!p) {
        if (global)
            GlobalFree(global);
        ShFree(bits);
        return E_OUTOFMEMORY;
    }
    memcpy(p, bits, size);
    GlobalUnlock(global);
    ShFree(bits);
    FORMATETC format = {(CLIPFORMAT)image_format(), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium = {};
    medium.tymed = TYMED_HGLOBAL;
    medium.hGlobal = global;
    HRESULT hr = data->lpVtbl->SetData(data, &format, &medium, TRUE);
    if (FAILED(hr)) {
        release_medium(&medium);
        return hr;
    }
    /* the context: whether the layered image is in the object. */
    IStream *stream;
    hr = make_stream(&stream);
    if (FAILED(hr))
        return hr;
    DWORD context[4] = {0, 1, 0, 0};
    hr = stream->lpVtbl->Write(stream, context, sizeof(context), nullptr);
    if (SUCCEEDED(hr)) {
        LARGE_INTEGER zero = {};
        stream->lpVtbl->Seek(stream, zero, STREAM_SEEK_SET, nullptr);
        format.cfFormat = (CLIPFORMAT)context_format();
        format.tymed = TYMED_ISTREAM;
        STGMEDIUM stream_medium = {};
        stream_medium.tymed = TYMED_ISTREAM;
        stream_medium.pstm = stream;
        hr = data->lpVtbl->SetData(data, &format, &stream_medium, TRUE);
        if (FAILED(hr))
            release_medium(&stream_medium);
        stream = nullptr;
    }
    if (stream)
        stream->lpVtbl->Release(stream);
    if (SUCCEEDED(hr) && flags)
        set_dword(data, flags_format(), flags);
    return hr;
}

/* the image of a data object, a copy, or the error the object gave when it has none. */
static HRESULT take_image(IDataObject *data, IMAGE_BITS **out)
{
    *out = nullptr;
    FORMATETC format = {(CLIPFORMAT)context_format(), nullptr, DVASPECT_CONTENT, -1, TYMED_ISTREAM};
    STGMEDIUM medium = {};
    HRESULT hr = data->lpVtbl->GetData(data, &format, &medium);
    if (FAILED(hr))
        return hr;
    DWORD context[4] = {};
    BOOL has_bits = FALSE;
    hr = E_FAIL;
    if (medium.tymed == TYMED_ISTREAM && medium.pstm) {
        LARGE_INTEGER zero = {};
        medium.pstm->lpVtbl->Seek(medium.pstm, zero, STREAM_SEEK_SET, nullptr);
        ULONG read = 0;
        if (SUCCEEDED(medium.pstm->lpVtbl->Read(medium.pstm, context, sizeof(context), &read)) && read == sizeof(context)) {
            has_bits = context[1] != 0;
            hr = S_OK;
        }
        medium.pstm->lpVtbl->Seek(medium.pstm, zero, STREAM_SEEK_SET, nullptr);
    }
    release_medium(&medium);
    if (FAILED(hr))
        return hr;
    format.cfFormat = (CLIPFORMAT)image_format();
    format.tymed = TYMED_HGLOBAL;
    memset(&medium, 0, sizeof(medium));
    hr = data->lpVtbl->GetData(data, &format, &medium);
    if (FAILED(hr))
        return hr;
    hr = E_FAIL;
    if (medium.tymed == TYMED_HGLOBAL && medium.hGlobal) {
        SIZE_T size = GlobalSize(medium.hGlobal);
        const IMAGE_BITS *p = GlobalLock(medium.hGlobal);
        if (p && size >= IMAGE_HEADER && p->Width > 0 && p->Height > 0 && p->Width <= 16384 && p->Height <= 16384) {
            SIZE_T bytes = (SIZE_T)p->Width * p->Height * 4;
            IMAGE_BITS *copy = has_bits && size >= IMAGE_HEADER + bytes ? ShAlloc(IMAGE_HEADER + bytes) : nullptr;
            if (copy) {
                memcpy(copy, p, IMAGE_HEADER + bytes);
                *out = copy;
                hr = S_OK;
            }
        }
        if (p)
            GlobalUnlock(medium.hGlobal);
    }
    release_medium(&medium);
    return hr;
}

/* ------------------------------------------------------------------ the window of the image */

static const WCHAR g_image_class[] = L"SysDragImage";

static LRESULT CALLBACK image_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    /* the image never takes the mouse: whatever is under it is the target. */
    if (message == WM_NCHITTEST)
        return HTTRANSPARENT;
    return DefWindowProcW(window, message, wparam, lparam);
}

static BOOL register_image_class(void)
{
    static BOOL done;
    if (done)
        return TRUE;
    WNDCLASSW c = {0};
    c.lpfnWndProc = image_proc;
    c.hInstance = GetModuleHandleW(nullptr);
    c.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    c.lpszClassName = g_image_class;
    if (!RegisterClassW(&c) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return FALSE;
    done = TRUE;
    return TRUE;
}

/*
 * a layered, topmost window that shows the image with its alpha, not activated, owned by the window of the target so
 * the host shows it as a popup of that window. the class and the styles are the shell's, but for the activation.
 */
static HWND make_image_window(const IMAGE_BITS *bits, POINT at, HWND owner)
{
    if (!register_image_class())
        return nullptr;
    HWND window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                                  g_image_class, L"Drag", WS_POPUP, at.x, at.y, bits->Width, bits->Height, owner, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    if (!window)
        return nullptr;
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = bits->Width;
    bmi.bmiHeader.biHeight = bits->Height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    void *pixels = nullptr;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP dib = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &pixels, nullptr, 0);
    BOOL ok = FALSE;
    if (dib && pixels) {
        memcpy(pixels, bits->Pixels, (SIZE_T)bits->Width * bits->Height * 4);
        HGDIOBJ old = SelectObject(dc, dib);
        SIZE size = {bits->Width, bits->Height};
        POINT source = {0, 0};
        BLENDFUNCTION blend = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        ok = UpdateLayeredWindow(window, screen, &at, &size, dc, &source, 0, &blend, ULW_ALPHA);
        SelectObject(dc, old);
    }
    if (dib)
        DeleteObject(dib);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    if (!ok) {
        DestroyWindow(window);
        return nullptr;
    }
    return window;
}

/* ------------------------------------------------------------------ the helper object */

typedef struct _HELPER {
    IDragSourceHelper2 Source;
    IDropTargetHelper Target;
    LONG Refs;
    DWORD Flags;
    /* what the target side shows. */
    IMAGE_BITS *Image;
    HWND Window;
    BOOL Hidden;
    POINT Offset;
    IDataObject *Data; /* the object the window was announced in, not referenced */
} HELPER;

static HELPER *from_source(IDragSourceHelper2 *This)
{
    return CONTAINING_RECORD(This, HELPER, Source);
}

static HELPER *from_target(IDropTargetHelper *This)
{
    return CONTAINING_RECORD(This, HELPER, Target);
}

/* the window and the image go; the data object is told it shows nothing any more. */
static void hide_image(HELPER *h)
{
    if (h->Window)
        DestroyWindow(h->Window);
    h->Window = nullptr;
    ShFree(h->Image);
    h->Image = nullptr;
    h->Data = nullptr;
}

static HRESULT STDMETHODCALLTYPE helper_query_interface(HELPER *h, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDragSourceHelper) ||
        IsEqualIID(riid, &IID_IDragSourceHelper2))
        *object = &h->Source;
    else if (IsEqualIID(riid, &IID_IDropTargetHelper))
        *object = &h->Target;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&h->Refs);
    return S_OK;
}

static ULONG helper_release(HELPER *h)
{
    LONG refs = InterlockedDecrement(&h->Refs);
    if (!refs) {
        hide_image(h);
        ShFree(h);
    }
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE src_query_interface(IDragSourceHelper2 *This, REFIID riid, void **object)
{
    return helper_query_interface(from_source(This), riid, object);
}

static ULONG STDMETHODCALLTYPE src_add_ref(IDragSourceHelper2 *This)
{
    return (ULONG)InterlockedIncrement(&from_source(This)->Refs);
}

static ULONG STDMETHODCALLTYPE src_release(IDragSourceHelper2 *This)
{
    return helper_release(from_source(This));
}

static HRESULT STDMETHODCALLTYPE src_from_bitmap(IDragSourceHelper2 *This, LPSHDRAGIMAGE image, IDataObject *data)
{
    if (!image || !data)
        return E_INVALIDARG;
    HELPER *h = from_source(This);
    IMAGE_BITS *bits = bits_from_bitmap(image->hbmpDragImage, image->sizeDragImage, image->ptOffset, image->crColorKey);
    /* the helper owns the bitmap from now on, whether it could use it or not. */
    if (image->hbmpDragImage)
        DeleteObject(image->hbmpDragImage);
    if (!bits)
        return E_FAIL;
    return give_image(data, bits, h->Flags);
}

/* the registered message a window answers with the image of its drag, "ShellGetDragImage". */
static UINT drag_image_message(void)
{
    static UINT message;
    if (!message)
        message = RegisterWindowMessageW(L"ShellGetDragImage");
    return message;
}

/*
 * the image of a window is what it asks the window for with ShellGetDragImage, its SHDRAGIMAGE filled in. a window
 * that gives none gets a picture of itself (guess: Windows draws the icons and names of the items of the data object
 * instead, and has no picture of the window), kept to 400 pixels each way so a window as big as the screen is no
 * drag image.
 */
static HRESULT STDMETHODCALLTYPE src_from_window(IDragSourceHelper2 *This, HWND window, POINT *point, IDataObject *data)
{
    if (!data)
        return E_INVALIDARG;
    HELPER *h = from_source(This);
    if (window) {
        SHDRAGIMAGE asked;
        memset(&asked, 0, sizeof(asked));
        if (SendMessageW(window, drag_image_message(), 0, (LPARAM)&asked) && asked.hbmpDragImage) {
            IMAGE_BITS *bits = bits_from_bitmap(asked.hbmpDragImage, asked.sizeDragImage, asked.ptOffset, asked.crColorKey);
            DeleteObject(asked.hbmpDragImage);
            if (bits)
                give_image(data, bits, h->Flags);
            return S_OK;
        }
    }
    RECT r;
    if (!window || !point || !GetWindowRect(window, &r) || r.right <= r.left || r.bottom <= r.top)
        return S_OK;
    LONG width = r.right - r.left, height = r.bottom - r.top;
    if (width > 400 || height > 400)
        return S_OK;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    ReleaseDC(nullptr, screen);
    if (!bitmap) {
        DeleteDC(dc);
        return S_OK;
    }
    HGDIOBJ old = SelectObject(dc, bitmap);
    PrintWindow(window, dc, 0);
    SelectObject(dc, old);
    DeleteDC(dc);
    SIZE size = {width, height};
    IMAGE_BITS *bits = bits_from_bitmap(bitmap, size, *point, CLR_NONE);
    DeleteObject(bitmap);
    if (bits)
        give_image(data, bits, h->Flags);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE src_set_flags(IDragSourceHelper2 *This, DWORD flags)
{
    from_source(This)->Flags = flags;
    return S_OK;
}

static const IDragSourceHelper2Vtbl g_source_vtbl = {
    src_query_interface, src_add_ref, src_release, src_from_bitmap, src_from_window, src_set_flags,
};

static HRESULT STDMETHODCALLTYPE tgt_query_interface(IDropTargetHelper *This, REFIID riid, void **object)
{
    return helper_query_interface(from_target(This), riid, object);
}

static ULONG STDMETHODCALLTYPE tgt_add_ref(IDropTargetHelper *This)
{
    return (ULONG)InterlockedIncrement(&from_target(This)->Refs);
}

static ULONG STDMETHODCALLTYPE tgt_release(IDropTargetHelper *This)
{
    return helper_release(from_target(This));
}

static void place_image(HELPER *h, const POINT *point)
{
    if (!h->Window || !point)
        return;
    SetWindowPos(h->Window, HWND_TOPMOST, point->x - h->Offset.x, point->y - h->Offset.y, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | (h->Hidden ? 0 : SWP_SHOWWINDOW));
}

/*
 * the window the image belongs to: the target's, else the one under the pointer when it is the process's own, else the
 * active one.
 */
static HWND owner_of(HWND target, const POINT *point)
{
    HWND root = target ? GetAncestor(target, GA_ROOT) : nullptr;
    if (!root) {
        HWND under = WindowFromPoint(*point);
        root = under ? GetAncestor(under, GA_ROOT) : nullptr;
        if (root && GetWindowThreadProcessId(root, nullptr) != GetCurrentThreadId())
            root = nullptr;
    }
    return root ? root : GetActiveWindow();
}

static HRESULT STDMETHODCALLTYPE tgt_drag_enter(IDropTargetHelper *This, HWND target, IDataObject *data, POINT *point,
                                                DWORD effect)
{
    HELPER *h = from_target(This);
    if (!point)
        return E_INVALIDARG;
    hide_image(h);
    if (!data)
        return S_OK;
    /* an object with no image gives the error it answered with, and nothing is shown. */
    HRESULT hr = take_image(data, &h->Image);
    if (FAILED(hr))
        return hr;
    h->Offset = (POINT){h->Image->OffsetX, h->Image->OffsetY};
    h->Window = make_image_window(h->Image, (POINT){point->x - h->Offset.x, point->y - h->Offset.y},
                                  owner_of(target, point));
    h->Hidden = FALSE;
    if (!h->Window) {
        ShFree(h->Image);
        h->Image = nullptr;
        return E_FAIL;
    }
    ShowWindow(h->Window, SW_SHOWNOACTIVATE);
    h->Data = data;
    set_dword(data, layered_format(), 1);
    set_dword(data, window_format(), (DWORD)(ULONG_PTR)h->Window);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tgt_drag_leave(IDropTargetHelper *This)
{
    HELPER *h = from_target(This);
    IDataObject *data = h->Data;
    hide_image(h);
    set_dword(data, layered_format(), 0);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tgt_drag_over(IDropTargetHelper *This, POINT *point, DWORD effect)
{
    if (!point)
        return E_INVALIDARG;
    place_image(from_target(This), point);
    return S_OK;
}

/* a drop ends the image like a leave does (read: Drop is DragLeave). */
static HRESULT STDMETHODCALLTYPE tgt_drop(IDropTargetHelper *This, IDataObject *data, POINT *point, DWORD effect)
{
    return tgt_drag_leave(This);
}

static HRESULT STDMETHODCALLTYPE tgt_show(IDropTargetHelper *This, BOOL show)
{
    HELPER *h = from_target(This);
    h->Hidden = !show;
    if (h->Window)
        ShowWindow(h->Window, show ? SW_SHOWNOACTIVATE : SW_HIDE);
    return S_OK;
}

static const IDropTargetHelperVtbl g_target_vtbl = {
    tgt_query_interface, tgt_add_ref, tgt_release, tgt_drag_enter, tgt_drag_leave, tgt_drag_over, tgt_drop, tgt_show,
};

/* ------------------------------------------------------------------ the class */

static HRESULT STDMETHODCALLTYPE factory_query_interface(IClassFactory *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IClassFactory))
        return E_NOINTERFACE;
    *object = This;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE factory_add_ref(IClassFactory *This)
{
    return 2;
}

static ULONG STDMETHODCALLTYPE factory_release(IClassFactory *This)
{
    return 1;
}

static HRESULT STDMETHODCALLTYPE factory_create_instance(IClassFactory *This, IUnknown *outer, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (outer)
        return CLASS_E_NOAGGREGATION;
    HELPER *h = ShAllocZero(sizeof(*h));
    if (!h)
        return E_OUTOFMEMORY;
    h->Source.lpVtbl = &g_source_vtbl;
    h->Target.lpVtbl = &g_target_vtbl;
    h->Refs = 1;
    HRESULT hr = helper_query_interface(h, riid, object);
    helper_release(h);
    return hr;
}

static HRESULT STDMETHODCALLTYPE factory_lock_server(IClassFactory *This, BOOL lock)
{
    return S_OK;
}

static const IClassFactoryVtbl g_factory_vtbl = {
    factory_query_interface, factory_add_ref, factory_release, factory_create_instance, factory_lock_server,
};
static IClassFactory g_factory = {&g_factory_vtbl};

SHSTDAPI HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!clsid || !riid)
        return E_INVALIDARG;
    if (!IsEqualCLSID(clsid, &CLSID_DragDropHelper))
        return CLASS_E_CLASSNOTAVAILABLE;
    return g_factory.lpVtbl->QueryInterface(&g_factory, riid, object);
}

SHSTDAPI HRESULT WINAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

/* ------------------------------------------------------------------ SHDoDragDrop */

typedef struct _DEFAULT_SOURCE {
    IDropSource Interface;
    LONG Refs;
} DEFAULT_SOURCE;

static HRESULT STDMETHODCALLTYPE ds_query_interface(IDropSource *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IDropSource))
        return E_NOINTERFACE;
    *object = This;
    InterlockedIncrement(&((DEFAULT_SOURCE *)This)->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE ds_add_ref(IDropSource *This)
{
    return (ULONG)InterlockedIncrement(&((DEFAULT_SOURCE *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE ds_release(IDropSource *This)
{
    LONG refs = InterlockedDecrement(&((DEFAULT_SOURCE *)This)->Refs);
    if (!refs)
        ShFree(This);
    return (ULONG)refs;
}

/* escape cancels, the buttons going up drops. */
static HRESULT STDMETHODCALLTYPE ds_query_continue(IDropSource *This, BOOL escape, DWORD keys)
{
    if (escape)
        return DRAGDROP_S_CANCEL;
    if (!(keys & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)))
        return DRAGDROP_S_DROP;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ds_give_feedback(IDropSource *This, DWORD effect)
{
    return DRAGDROP_S_USEDEFAULTCURSORS;
}

static const IDropSourceVtbl g_default_source_vtbl = {
    ds_query_interface, ds_add_ref, ds_release, ds_query_continue, ds_give_feedback,
};

typedef HRESULT(WINAPI *DO_DRAG_DROP)(IDataObject *, IDropSource *, DWORD, DWORD *);

/* the drag of a data object with the default source when there is none, the pointer drag of the shell. */
SHSTDAPI HRESULT WINAPI SHDoDragDrop(HWND window, IDataObject *data, IDropSource *source, DWORD effects,
                                     DWORD *effect)
{
    DWORD dummy;
    if (!data)
        return E_INVALIDARG;
    if (!effect)
        effect = &dummy;
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    DO_DRAG_DROP drag = ole ? (DO_DRAG_DROP)(void *)GetProcAddress(ole, "DoDragDrop") : nullptr;
    if (!drag)
        return E_NOTIMPL;
    DEFAULT_SOURCE *own = nullptr;
    if (!source) {
        own = ShAllocZero(sizeof(*own));
        if (!own)
            return E_OUTOFMEMORY;
        own->Interface.lpVtbl = &g_default_source_vtbl;
        own->Refs = 1;
        source = &own->Interface;
    }
    HRESULT hr = drag(data, source, effects, effect);
    if (own)
        source->lpVtbl->Release(source);
    return hr;
}

/* ------------------------------------------------------------------ SHCreateStdEnumFmtEtc */

typedef struct _FORMAT_ENUM {
    IEnumFORMATETC Interface;
    LONG Refs;
    ULONG Count, Next;
    FORMATETC Formats[1];
} FORMAT_ENUM;

static const IEnumFORMATETCVtbl g_enum_vtbl;

static HRESULT enum_create(const FORMATETC *formats, ULONG count, ULONG next, IEnumFORMATETC **out)
{
    FORMAT_ENUM *e = ShAllocZero(sizeof(*e) + (count ? count - 1 : 0) * sizeof(FORMATETC));
    if (!e)
        return E_OUTOFMEMORY;
    e->Interface.lpVtbl = &g_enum_vtbl;
    e->Refs = 1;
    e->Count = count;
    e->Next = next;
    if (count)
        memcpy(e->Formats, formats, count * sizeof(FORMATETC));
    /* the target device of a format is not kept. */
    for (ULONG i = 0; i < count; i++)
        e->Formats[i].ptd = nullptr;
    *out = &e->Interface;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_query_interface(IEnumFORMATETC *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (!IsEqualIID(riid, &IID_IUnknown) && !IsEqualIID(riid, &IID_IEnumFORMATETC))
        return E_NOINTERFACE;
    *object = This;
    InterlockedIncrement(&((FORMAT_ENUM *)This)->Refs);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE enum_add_ref(IEnumFORMATETC *This)
{
    return (ULONG)InterlockedIncrement(&((FORMAT_ENUM *)This)->Refs);
}

static ULONG STDMETHODCALLTYPE enum_release(IEnumFORMATETC *This)
{
    LONG refs = InterlockedDecrement(&((FORMAT_ENUM *)This)->Refs);
    if (!refs)
        ShFree(This);
    return (ULONG)refs;
}

static HRESULT STDMETHODCALLTYPE enum_next(IEnumFORMATETC *This, ULONG count, FORMATETC *items, ULONG *fetched)
{
    FORMAT_ENUM *e = (FORMAT_ENUM *)This;
    ULONG n = 0;
    if (!items || (!fetched && count != 1))
        return E_INVALIDARG;
    while (n < count && e->Next < e->Count)
        items[n++] = e->Formats[e->Next++];
    if (fetched)
        *fetched = n;
    return n == count ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE enum_skip(IEnumFORMATETC *This, ULONG count)
{
    FORMAT_ENUM *e = (FORMAT_ENUM *)This;
    e->Next += count;
    if (e->Next > e->Count) {
        e->Next = e->Count;
        return S_FALSE;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_reset(IEnumFORMATETC *This)
{
    ((FORMAT_ENUM *)This)->Next = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE enum_clone(IEnumFORMATETC *This, IEnumFORMATETC **out)
{
    FORMAT_ENUM *e = (FORMAT_ENUM *)This;
    if (!out)
        return E_POINTER;
    return enum_create(e->Formats, e->Count, e->Next, out);
}

static const IEnumFORMATETCVtbl g_enum_vtbl = {
    enum_query_interface, enum_add_ref, enum_release, enum_next, enum_skip, enum_reset, enum_clone,
};

SHSTDAPI HRESULT WINAPI SHCreateStdEnumFmtEtc(UINT count, const FORMATETC formats[], IEnumFORMATETC **out)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (count && !formats)
        return E_INVALIDARG;
    return enum_create(formats, count, 0, out);
}
