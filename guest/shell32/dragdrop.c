/*
 * the drag and drop helpers of the shell: the drag image of a drag source, the window that shows it over a
 * drop target, SHDoDragDrop and the format enumerator of SHCreateStdEnumFmtEtc.
 *
 * a source gives the helper its image, which goes into the data object as the format "DragImageBits"; the helper
 * of a target takes it out of the data object, and shows it in a layered window that follows the pointer. the
 * bits are our own layout: a header, then the pixels as premultiplied 32 bit rows, top row first. when the data
 * object refuses the format, the helper keeps the image itself for as long as the object lives in the process.
 */
#include "shell32_private.h"
#include <mime/win32/shobjidl_ui.h>
#include <mime/win32/oleidl.h>

#define IMAGE_MAGIC 0x4244494D /* MIDB */
#ifndef CLR_NONE
#define CLR_NONE 0xFFFFFFFFL
#endif
#define IMAGE_KEEP 8

typedef struct _IMAGE_BITS {
    DWORD Magic;
    LONG Width, Height;
    LONG OffsetX, OffsetY;
    DWORD Pixels[1];
} IMAGE_BITS;

/* the images kept for data objects that do not take the format, keyed by the identity of the object. */
static SRWLOCK g_keep_lock = SRWLOCK_INIT;
static struct {
    IUnknown *Object;
    IMAGE_BITS *Bits;
} g_keep[IMAGE_KEEP];
static UINT g_keep_next;

static UINT image_format(void)
{
    static UINT format;
    if (!format)
        format = RegisterClipboardFormatW(L"DragImageBits");
    return format;
}

static HRESULT identity_of(IDataObject *data, IUnknown **identity)
{
    return data->lpVtbl->QueryInterface(data, &IID_IUnknown, (void **)identity);
}

static void keep_image(IDataObject *data, IMAGE_BITS *bits)
{
    IUnknown *identity;
    if (FAILED(identity_of(data, &identity)))
        return;
    AcquireSRWLockExclusive(&g_keep_lock);
    int slot = -1;
    for (int i = 0; i < IMAGE_KEEP && slot < 0; i++)
        if (g_keep[i].Object == identity)
            slot = i;
    for (int i = 0; i < IMAGE_KEEP && slot < 0; i++)
        if (!g_keep[i].Object)
            slot = i;
    if (slot < 0)
        slot = (int)(g_keep_next++ % IMAGE_KEEP);
    ShFree(g_keep[slot].Bits);
    g_keep[slot].Object = identity;
    g_keep[slot].Bits = bits;
    ReleaseSRWLockExclusive(&g_keep_lock);
    /* the identity is only compared, so the object does not stay alive for it. */
    identity->lpVtbl->Release(identity);
}

static IMAGE_BITS *kept_image(IDataObject *data)
{
    IUnknown *identity;
    IMAGE_BITS *copy = nullptr;
    if (FAILED(identity_of(data, &identity)))
        return nullptr;
    AcquireSRWLockShared(&g_keep_lock);
    for (int i = 0; i < IMAGE_KEEP; i++)
        if (g_keep[i].Object == identity && g_keep[i].Bits) {
            SIZE_T size = sizeof(IMAGE_BITS) + (SIZE_T)g_keep[i].Bits->Width * g_keep[i].Bits->Height * 4;
            copy = ShAlloc(size);
            if (copy)
                memcpy(copy, g_keep[i].Bits, size);
        }
    ReleaseSRWLockShared(&g_keep_lock);
    identity->lpVtbl->Release(identity);
    return copy;
}

/* the pixels of a bitmap, premultiplied: a bitmap without alpha is opaque but for the pixels of its color key. */
static IMAGE_BITS *bits_from_bitmap(HBITMAP bitmap, SIZE size, POINT offset, COLORREF key)
{
    BITMAP info;
    if (!GetObjectW(bitmap, sizeof(info), &info) || info.bmWidth <= 0 || info.bmHeight <= 0)
        return nullptr;
    LONG width = info.bmWidth, height = info.bmHeight;
    SIZE_T bytes = (SIZE_T)width * height * 4;
    IMAGE_BITS *bits = ShAlloc(sizeof(IMAGE_BITS) + bytes);
    if (!bits)
        return nullptr;
    bits->Magic = IMAGE_MAGIC;
    bits->Width = width;
    bits->Height = height;
    bits->OffsetX = offset.x;
    bits->OffsetY = offset.y;
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    HDC dc = GetDC(nullptr);
    int lines = GetDIBits(dc, bitmap, 0, (UINT)height, bits->Pixels, &bmi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (lines != height) {
        ShFree(bits);
        return nullptr;
    }
    BOOL alpha = FALSE;
    SIZE_T count = (SIZE_T)width * height;
    for (SIZE_T i = 0; i < count && !alpha; i++)
        alpha = (bits->Pixels[i] >> 24) != 0;
    DWORD key_pixel = (GetBValue(key)) | GetGValue(key) << 8 | (DWORD)GetRValue(key) << 16;
    for (SIZE_T i = 0; i < count; i++) {
        DWORD p = bits->Pixels[i];
        if (alpha)
            continue;
        bits->Pixels[i] = key != CLR_NONE && (p & 0xFFFFFF) == key_pixel ? 0 : p | 0xFF000000u;
    }
    (void)size;
    return bits;
}

/* puts the image into the data object, or keeps it when the object takes nothing. */
static HRESULT give_image(IDataObject *data, IMAGE_BITS *bits)
{
    SIZE_T size = sizeof(IMAGE_BITS) + (SIZE_T)bits->Width * bits->Height * 4;
    HGLOBAL global = GlobalAlloc(GMEM_MOVEABLE, size);
    void *p = global ? GlobalLock(global) : nullptr;
    if (!p) {
        if (global)
            GlobalFree(global);
        ShFree(bits);
        return E_OUTOFMEMORY;
    }
    memcpy(p, bits, size);
    GlobalUnlock(global);
    FORMATETC format = {(CLIPFORMAT)image_format(), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium = {};
    medium.tymed = TYMED_HGLOBAL;
    medium.hGlobal = global;
    if (SUCCEEDED(data->lpVtbl->SetData(data, &format, &medium, TRUE))) {
        ShFree(bits);
        return S_OK;
    }
    GlobalFree(global);
    keep_image(data, bits);
    return S_OK;
}

/* the image of a data object, a copy, or nothing when the source gave none. */
static IMAGE_BITS *take_image(IDataObject *data)
{
    FORMATETC format = {(CLIPFORMAT)image_format(), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium = {};
    if (SUCCEEDED(data->lpVtbl->GetData(data, &format, &medium)) && medium.tymed == TYMED_HGLOBAL && medium.hGlobal) {
        IMAGE_BITS *copy = nullptr;
        SIZE_T size = GlobalSize(medium.hGlobal);
        const IMAGE_BITS *p = GlobalLock(medium.hGlobal);
        if (p && size >= sizeof(IMAGE_BITS) && p->Magic == IMAGE_MAGIC && p->Width > 0 && p->Height > 0 &&
            size >= sizeof(IMAGE_BITS) + (SIZE_T)p->Width * p->Height * 4) {
            copy = ShAlloc(sizeof(IMAGE_BITS) + (SIZE_T)p->Width * p->Height * 4);
            if (copy)
                memcpy(copy, p, sizeof(IMAGE_BITS) + (SIZE_T)p->Width * p->Height * 4);
        }
        if (p)
            GlobalUnlock(medium.hGlobal);
        if (medium.pUnkForRelease)
            medium.pUnkForRelease->lpVtbl->Release(medium.pUnkForRelease);
        else
            GlobalFree(medium.hGlobal);
        if (copy)
            return copy;
    }
    return kept_image(data);
}

/* ------------------------------------------------------------------ the window of the image */

static const WCHAR g_image_class[] = L"MimeDragImage";

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
    c.lpszClassName = g_image_class;
    if (!RegisterClassW(&c) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return FALSE;
    done = TRUE;
    return TRUE;
}

/*
 * a layered, topmost window that shows the image with its alpha, not activated, owned by the window of the target so
 * the host shows it as a popup of that window.
 */
static HWND make_image_window(const IMAGE_BITS *bits, POINT at, HWND owner)
{
    if (!register_image_class())
        return nullptr;
    HWND window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
                                  g_image_class, L"", WS_POPUP, at.x, at.y, bits->Width, bits->Height, owner, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    if (!window)
        return nullptr;
    BITMAPINFO bmi;
    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = bits->Width;
    bmi.bmiHeader.biHeight = -bits->Height;
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
} HELPER;

static HELPER *from_source(IDragSourceHelper2 *This)
{
    return CONTAINING_RECORD(This, HELPER, Source);
}

static HELPER *from_target(IDropTargetHelper *This)
{
    return CONTAINING_RECORD(This, HELPER, Target);
}

static void hide_image(HELPER *h)
{
    if (h->Window)
        DestroyWindow(h->Window);
    h->Window = nullptr;
    ShFree(h->Image);
    h->Image = nullptr;
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
    IMAGE_BITS *bits = bits_from_bitmap(image->hbmpDragImage, image->sizeDragImage, image->ptOffset, image->crColorKey);
    /* the helper owns the bitmap from now on. */
    if (image->hbmpDragImage)
        DeleteObject(image->hbmpDragImage);
    if (!bits)
        return E_FAIL;
    return give_image(data, bits);
}

static HRESULT STDMETHODCALLTYPE src_from_window(IDragSourceHelper2 *This, HWND window, POINT *point, IDataObject *data)
{
    if (!data)
        return E_INVALIDARG;
    /* without a window the shell draws the data itself; nothing is drawn here. */
    if (!window || !point)
        return S_OK;
    RECT r;
    if (!GetWindowRect(window, &r) || r.right <= r.left || r.bottom <= r.top)
        return E_FAIL;
    LONG width = r.right - r.left, height = r.bottom - r.top;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, width, height);
    ReleaseDC(nullptr, screen);
    if (!bitmap) {
        DeleteDC(dc);
        return E_OUTOFMEMORY;
    }
    HGDIOBJ old = SelectObject(dc, bitmap);
    PrintWindow(window, dc, 0);
    SelectObject(dc, old);
    DeleteDC(dc);
    SIZE size = {width, height};
    IMAGE_BITS *bits = bits_from_bitmap(bitmap, size, *point, CLR_NONE);
    DeleteObject(bitmap);
    if (!bits)
        return E_FAIL;
    return give_image(data, bits);
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
    if (!data || !point)
        return E_INVALIDARG;
    hide_image(h);
    h->Image = take_image(data);
    if (h->Image) {
        h->Offset = (POINT){h->Image->OffsetX, h->Image->OffsetY};
        h->Window = make_image_window(h->Image, (POINT){point->x - h->Offset.x, point->y - h->Offset.y},
                                      owner_of(target, point));
        h->Hidden = FALSE;
        if (h->Window)
            ShowWindow(h->Window, SW_SHOWNOACTIVATE);
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tgt_drag_leave(IDropTargetHelper *This)
{
    hide_image(from_target(This));
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tgt_drag_over(IDropTargetHelper *This, POINT *point, DWORD effect)
{
    if (!point)
        return E_INVALIDARG;
    place_image(from_target(This), point);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE tgt_drop(IDropTargetHelper *This, IDataObject *data, POINT *point, DWORD effect)
{
    hide_image(from_target(This));
    return S_OK;
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
