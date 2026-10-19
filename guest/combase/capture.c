/*
 * Windows.Graphics.Capture: capture items for a window or a monitor, a frame pool of Direct3D 11 textures and the
 * session that fills it. the pixels come from win32k's capture (guest/win32k/capture.c, the same as dxgi.dll's output
 * duplication): a session keeps a duplication of its item's window or monitor, and a thread polls the sessions once a
 * blank of the display, writes what changed into a texture of the session's device and tells the pool's handlers
 * that a frame arrived.
 *
 * a frame is a copy of the session's own texture into a free texture of the pool, so a program holding frames
 * never sees them change; when all the pool's textures are held, the frame is dropped.
 */
#include "combase_private.h"
#include <mime/ntuser.h>
#include <mime/vblank.h>
#include <mime/win32/d3d11.h>
#include <mime/win32/winuser.h>
#include <mime/winrt_object.h>

#define POOL_MAX_BUFFERS 16
#define POOL_MAX_HANDLERS 8
#define DWMWA_EXTENDED_FRAME_BOUNDS 9
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

const WCHAR CbCaptureItemClass[] = L"Windows.Graphics.Capture.GraphicsCaptureItem";
const WCHAR CbCaptureSessionClass[] = L"Windows.Graphics.Capture.GraphicsCaptureSession";
const WCHAR CbCapturePoolClass[] = L"Windows.Graphics.Capture.Direct3D11CaptureFramePool";
static const WCHAR g_frame_class[] = L"Windows.Graphics.Capture.Direct3D11CaptureFrame";

typedef struct POOL POOL;
typedef struct SESSION SESSION;

typedef struct HANDLER_SET {
    struct {
        ITypedEventHandler *Handler;
        INT64 Token;
    } Entries[POOL_MAX_HANDLERS];
} HANDLER_SET;

typedef struct ITEM {
    WRT_OBJECT Base;
    CRITICAL_SECTION Lock;
    HWND Window;
    HMONITOR Monitor;
    UINT MonitorNumber;
    HANDLER_SET Closed;
    BOOL IsClosed;
} ITEM;

typedef struct FRAME {
    WRT_OBJECT Base;
    POOL *Pool;
    UINT Slot;
    ID3D11Texture2D *Texture;
    SizeInt32 ContentSize;
    TimeSpan Time;
    BOOL Closed;
} FRAME;

struct POOL {
    WRT_OBJECT Base;
    CRITICAL_SECTION Lock;
    IDirect3DDevice *Wrapper;
    ID3D11Device *Device;
    ID3D11DeviceContext *Context;
    ID3D11Multithread *Multithread;
    DirectXPixelFormat Format;
    INT32 Buffers;
    SizeInt32 Size;
    ID3D11Texture2D *Textures[POOL_MAX_BUFFERS];
    BOOL Held[POOL_MAX_BUFFERS];
    FRAME *Queue[POOL_MAX_BUFFERS];
    UINT QueueCount;
    HANDLER_SET Arrived;
    SESSION *Session;
    BOOL IsClosed;
};

struct SESSION {
    WRT_OBJECT Base;
    POOL *Pool;
    ITEM *Item;
    BOOL Started, IsClosed, Cursor, Border;
    ULONG Capture;
    BYTE *Pixels;
    SIZE_T PixelsSize;
    UINT Width, Height;
    ID3D11Texture2D *Shadow;
    UINT ShadowWidth, ShadowHeight;
    /* a pool made again wants a frame whether anything changed or not, from the last pixels. */
    BOOL Resend;
    SizeInt32 LastContent;
    UINT LastCropX, LastCropY;
    struct SESSION *Next;
};

static volatile INT64 g_next_token;

/* ------------------------------------------------------------------ handler lists */

static HRESULT handlers_add(HANDLER_SET *set, CRITICAL_SECTION *lock, ITypedEventHandler *handler,
                            EventRegistrationToken *token)
{
    if (!handler || !token)
        return E_POINTER;
    HRESULT hr = E_OUTOFMEMORY;
    EnterCriticalSection(lock);
    for (UINT i = 0; i < POOL_MAX_HANDLERS; i++)
        if (!set->Entries[i].Handler) {
            handler->lpVtbl->AddRef(handler);
            set->Entries[i].Handler = handler;
            set->Entries[i].Token = InterlockedIncrement64(&g_next_token);
            token->value = set->Entries[i].Token;
            hr = S_OK;
            break;
        }
    LeaveCriticalSection(lock);
    return hr;
}

static HRESULT handlers_remove(HANDLER_SET *set, CRITICAL_SECTION *lock, EventRegistrationToken token)
{
    ITypedEventHandler *removed = nullptr;
    EnterCriticalSection(lock);
    for (UINT i = 0; i < POOL_MAX_HANDLERS; i++)
        if (set->Entries[i].Handler && set->Entries[i].Token == token.value) {
            removed = set->Entries[i].Handler;
            set->Entries[i].Handler = nullptr;
        }
    LeaveCriticalSection(lock);
    if (removed)
        removed->lpVtbl->Release(removed);
    return S_OK;
}

/* the handlers called with the sender, outside the lock since they call back. */
static void handlers_raise(HANDLER_SET *set, CRITICAL_SECTION *lock, IInspectable *sender)
{
    ITypedEventHandler *list[POOL_MAX_HANDLERS];
    UINT n = 0;
    EnterCriticalSection(lock);
    for (UINT i = 0; i < POOL_MAX_HANDLERS; i++)
        if (set->Entries[i].Handler) {
            list[n] = set->Entries[i].Handler;
            list[n]->lpVtbl->AddRef(list[n]);
            n++;
        }
    LeaveCriticalSection(lock);
    for (UINT i = 0; i < n; i++) {
        list[i]->lpVtbl->Invoke(list[i], sender, nullptr);
        list[i]->lpVtbl->Release(list[i]);
    }
}

static void handlers_clear(HANDLER_SET *set)
{
    for (UINT i = 0; i < POOL_MAX_HANDLERS; i++)
        if (set->Entries[i].Handler) {
            set->Entries[i].Handler->lpVtbl->Release(set->Entries[i].Handler);
            set->Entries[i].Handler = nullptr;
        }
}

/* ------------------------------------------------------------------ the thread of sessions */

static CRITICAL_SECTION g_lock;
static BOOL g_lock_made;
static SESSION *g_sessions;
static HANDLE g_thread, g_wake;

static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK once(PINIT_ONCE o, PVOID parameter, PVOID *context)
{
    InitializeCriticalSection(&g_lock);
    g_wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_lock_made = g_wake != nullptr;
    return TRUE;
}

static BOOL initialize(void)
{
    InitOnceExecuteOnce(&g_once, once, nullptr, nullptr);
    return g_lock_made;
}

static void session_poll(SESSION *s);
static DWORD WINAPI capture_thread(void *param);

static void sessions_add(SESSION *s)
{
    EnterCriticalSection(&g_lock);
    InterlockedIncrement(&s->Base.Refs);
    s->Next = g_sessions;
    g_sessions = s;
    if (!g_thread)
        g_thread = CreateThread(nullptr, 0, capture_thread, nullptr, 0, nullptr);
    LeaveCriticalSection(&g_lock);
    SetEvent(g_wake);
}

static BOOL sessions_remove(SESSION *s)
{
    BOOL found = FALSE;
    EnterCriticalSection(&g_lock);
    for (SESSION **p = &g_sessions; *p; p = &(*p)->Next)
        if (*p == s) {
            *p = s->Next;
            found = TRUE;
            break;
        }
    LeaveCriticalSection(&g_lock);
    return found;
}

typedef HRESULT(WINAPI *CO_INITIALIZE_EX)(LPVOID, DWORD);

static DWORD WINAPI capture_thread(void *param)
{
    HMODULE ole32 = LoadLibraryW(L"ole32.dll");
    CO_INITIALIZE_EX init = ole32 ? (CO_INITIALIZE_EX)(void *)GetProcAddress(ole32, "CoInitializeEx") : nullptr;
    if (init)
        init(nullptr, COINIT_MULTITHREADED);
    for (;;) {
        SESSION *list[16];
        UINT n = 0;
        EnterCriticalSection(&g_lock);
        for (SESSION *s = g_sessions; s && n < RTL_NUMBER_OF(list); s = s->Next) {
            InterlockedIncrement(&s->Base.Refs);
            list[n++] = s;
        }
        LeaveCriticalSection(&g_lock);
        if (!n) {
            WaitForSingleObject(g_wake, INFINITE);
            continue;
        }
        for (UINT i = 0; i < n; i++) {
            session_poll(list[i]);
            WrtRelease(&list[i]->Base.Ifaces[0]);
        }
        MIME_VBLANK vblank;
        MimeVblankInfo(&vblank);
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        UINT64 next = MimeVblankTime(&vblank, MimeVblankIndex(&vblank, now.QuadPart) + 1);
        DWORD ms = (DWORD)((next - now.QuadPart) * 1000 / vblank.frequency);
        Sleep(ms ? ms : 1);
    }
}

/* ------------------------------------------------------------------ items */

/* the bounds of a window as it shows: without the invisible border, from dwm as capturers read them. */
static BOOL window_bounds(HWND window, RECT *window_rect, RECT *frame)
{
    typedef HRESULT(WINAPI * GET)(HWND, DWORD, PVOID, DWORD);
    if (!GetWindowRect(window, window_rect))
        return FALSE;
    *frame = *window_rect;
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    GET get = dwm ? (GET)(void *)GetProcAddress(dwm, "DwmGetWindowAttribute") : nullptr;
    RECT r;
    if (get && SUCCEEDED(get(window, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))) && r.right > r.left && r.bottom > r.top)
        *frame = r;
    return TRUE;
}

static SizeInt32 item_size(ITEM *item)
{
    SizeInt32 size = {0, 0};
    if (item->Window) {
        RECT wr, frame;
        if (window_bounds(item->Window, &wr, &frame)) {
            size.Width = frame.right - frame.left;
            size.Height = frame.bottom - frame.top;
        }
    } else {
        MONITORINFO info = {sizeof(info)};
        if (GetMonitorInfoW(item->Monitor, &info)) {
            size.Width = info.rcMonitor.right - info.rcMonitor.left;
            size.Height = info.rcMonitor.bottom - info.rcMonitor.top;
        }
    }
    return size;
}

WRT_INSPECTABLE_THUNKS(IGraphicsCaptureItem)

static HRESULT STDMETHODCALLTYPE item_display_name(IGraphicsCaptureItem *This, HSTRING *value)
{
    ITEM *item = (ITEM *)((WRT_IFACE *)This)->Owner;
    WCHAR text[260] = L"";
    if (!value)
        return E_POINTER;
    if (item->Window) {
        GetWindowTextW(item->Window, text, RTL_NUMBER_OF(text));
    } else {
        MONITORINFOEXW info = {sizeof(info)};
        if (GetMonitorInfoW(item->Monitor, (MONITORINFO *)&info))
            lstrcpynW(text, info.szDevice, RTL_NUMBER_OF(text));
    }
    return WindowsCreateString(text, (UINT32)wcslen(text), value);
}

static HRESULT STDMETHODCALLTYPE item_get_size(IGraphicsCaptureItem *This, SizeInt32 *value)
{
    if (!value)
        return E_POINTER;
    *value = item_size((ITEM *)((WRT_IFACE *)This)->Owner);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE item_add_closed(IGraphicsCaptureItem *This, ITypedEventHandler *handler,
                                                 EventRegistrationToken *token)
{
    ITEM *item = (ITEM *)((WRT_IFACE *)This)->Owner;
    return handlers_add(&item->Closed, &item->Lock, handler, token);
}

static HRESULT STDMETHODCALLTYPE item_remove_closed(IGraphicsCaptureItem *This, EventRegistrationToken token)
{
    ITEM *item = (ITEM *)((WRT_IFACE *)This)->Owner;
    return handlers_remove(&item->Closed, &item->Lock, token);
}

static const IGraphicsCaptureItemVtbl item_vtbl = {WRT_INSPECTABLE_ENTRIES(IGraphicsCaptureItem), item_display_name,
                                                   item_get_size, item_add_closed, item_remove_closed};

static void item_destroy(WRT_OBJECT *object)
{
    ITEM *item = (ITEM *)object;
    handlers_clear(&item->Closed);
    DeleteCriticalSection(&item->Lock);
    CbFree(item);
}

static const WRT_CLASS item_class = {CbCaptureItemClass, 1, {&IID_IGraphicsCaptureItem}, 0, item_destroy};

/* the item closes once, when its window or monitor is gone. */
static void item_close(ITEM *item)
{
    EnterCriticalSection(&item->Lock);
    BOOL first = !item->IsClosed;
    item->IsClosed = TRUE;
    LeaveCriticalSection(&item->Lock);
    if (first)
        handlers_raise(&item->Closed, &item->Lock, (IInspectable *)&item->Base.Ifaces[0]);
}

static HRESULT item_make(HWND window, HMONITOR monitor, UINT number, REFIID riid, void **result)
{
    ITEM *item = CbAlloc(sizeof(*item));
    if (!item)
        return E_OUTOFMEMORY;
    memset(item, 0, sizeof(*item));
    InitializeCriticalSection(&item->Lock);
    const void *vtables[1] = {&item_vtbl};
    WrtInit(&item->Base, &item_class, vtables);
    item->Window = window;
    item->Monitor = monitor;
    item->MonitorNumber = number;
    HRESULT hr = WrtQuery(&item->Base.Ifaces[0], riid, result);
    WrtRelease(&item->Base.Ifaces[0]);
    return hr;
}

/* a monitor's number, the one its device name ends with. */
static UINT monitor_number(HMONITOR monitor)
{
    MONITORINFOEXW info = {sizeof(info)};
    if (!GetMonitorInfoW(monitor, (MONITORINFO *)&info))
        return 0;
    size_t n = wcslen(info.szDevice);
    UINT value = 0, scale = 1;
    while (n && info.szDevice[n - 1] >= L'0' && info.szDevice[n - 1] <= L'9') {
        value += (info.szDevice[n - 1] - L'0') * scale;
        scale *= 10;
        n--;
    }
    return value;
}

/* guesses: a window that is gone is an invalid window handle, and the windows that cannot be captured are invalid. */
static HRESULT STDMETHODCALLTYPE interop_create_for_window(IGraphicsCaptureItemInterop *This, HWND window, REFIID riid,
                                                           void **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!IsWindow(window))
        return HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    if (window == GetDesktopWindow() || (GetWindowLongW(window, GWL_STYLE) & WS_CHILD))
        return E_INVALIDARG;
    return item_make(window, nullptr, 0, riid, result);
}

static HRESULT STDMETHODCALLTYPE interop_create_for_monitor(IGraphicsCaptureItemInterop *This, HMONITOR monitor,
                                                            REFIID riid, void **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    UINT number = monitor_number(monitor);
    if (!number)
        return HRESULT_FROM_WIN32(1461);
    return item_make(nullptr, monitor, number, riid, result);
}

static HRESULT STDMETHODCALLTYPE item_statics_create_from_visual(IGraphicsCaptureItemStatics *This,
                                                                 IInspectable *visual, IGraphicsCaptureItem **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    return visual ? E_NOTIMPL : E_INVALIDARG;
}

/* ------------------------------------------------------------------ frames and the pool */

WRT_INSPECTABLE_THUNKS(IDirect3D11CaptureFrame)
WRT_INSPECTABLE_THUNKS(IDirect3D11CaptureFramePool)
WRT_INSPECTABLE_THUNKS(IGraphicsCaptureSession)
WRT_INSPECTABLE_THUNKS(IGraphicsCaptureSession2)
WRT_INSPECTABLE_THUNKS(IGraphicsCaptureSession3)
WRT_INSPECTABLE_THUNKS(IClosable)

static void frame_release_slot(FRAME *frame)
{
    POOL *pool = frame->Pool;
    EnterCriticalSection(&pool->Lock);
    if (!frame->Closed) {
        frame->Closed = TRUE;
        if (pool->Textures[frame->Slot] == frame->Texture)
            pool->Held[frame->Slot] = FALSE;
    }
    LeaveCriticalSection(&pool->Lock);
}

static void frame_destroy(WRT_OBJECT *object)
{
    FRAME *frame = (FRAME *)object;
    frame_release_slot(frame);
    if (frame->Texture)
        frame->Texture->lpVtbl->Release(frame->Texture);
    WrtRelease(&frame->Pool->Base.Ifaces[0]);
    CbFree(frame);
}

/* the surface wrapper d3d11.dll makes of the texture. */
static HRESULT STDMETHODCALLTYPE frame_get_surface(IDirect3D11CaptureFrame *This, IDirect3DSurface **value)
{
    typedef HRESULT(WINAPI * WRAP)(IDXGISurface *, IInspectable **);
    FRAME *frame = (FRAME *)((WRT_IFACE *)This)->Owner;
    if (!value)
        return E_POINTER;
    *value = nullptr;
    if (frame->Closed)
        return RO_E_CLOSED;
    HMODULE d3d11 = LoadLibraryW(L"d3d11.dll");
    WRAP wrap = d3d11 ? (WRAP)(void *)GetProcAddress(d3d11, "CreateDirect3D11SurfaceFromDXGISurface") : nullptr;
    if (!wrap)
        return E_FAIL;
    IDXGISurface *surface;
    HRESULT hr = frame->Texture->lpVtbl->QueryInterface(frame->Texture, &IID_IDXGISurface, (void **)&surface);
    if (FAILED(hr))
        return hr;
    IInspectable *wrapped;
    hr = wrap(surface, &wrapped);
    surface->lpVtbl->Release(surface);
    if (FAILED(hr))
        return hr;
    hr = wrapped->lpVtbl->QueryInterface(wrapped, &IID_IDirect3DSurface, (void **)value);
    wrapped->lpVtbl->Release(wrapped);
    return hr;
}

static HRESULT STDMETHODCALLTYPE frame_get_time(IDirect3D11CaptureFrame *This, TimeSpan *value)
{
    FRAME *frame = (FRAME *)((WRT_IFACE *)This)->Owner;
    if (!value)
        return E_POINTER;
    if (frame->Closed)
        return RO_E_CLOSED;
    *value = frame->Time;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE frame_get_content_size(IDirect3D11CaptureFrame *This, SizeInt32 *value)
{
    FRAME *frame = (FRAME *)((WRT_IFACE *)This)->Owner;
    if (!value)
        return E_POINTER;
    if (frame->Closed)
        return RO_E_CLOSED;
    *value = frame->ContentSize;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE frame_close(IClosable *This)
{
    frame_release_slot((FRAME *)((WRT_IFACE *)This)->Owner);
    return S_OK;
}

static const IDirect3D11CaptureFrameVtbl frame_vtbl = {WRT_INSPECTABLE_ENTRIES(IDirect3D11CaptureFrame), frame_get_surface,
                                                       frame_get_time, frame_get_content_size};
static const IClosableVtbl closable_vtbl = {WRT_INSPECTABLE_ENTRIES(IClosable), frame_close};

static const WRT_CLASS frame_class = {g_frame_class, 2, {&IID_IDirect3D11CaptureFrame, &IID_IClosable}, 0, frame_destroy};

static void pool_release_textures(POOL *pool)
{
    for (UINT i = 0; i < POOL_MAX_BUFFERS; i++) {
        if (pool->Textures[i])
            pool->Textures[i]->lpVtbl->Release(pool->Textures[i]);
        pool->Textures[i] = nullptr;
        pool->Held[i] = FALSE;
    }
    for (UINT i = 0; i < pool->QueueCount; i++) {
        pool->Queue[i]->Closed = TRUE;
        WrtRelease(&pool->Queue[i]->Base.Ifaces[0]);
    }
    pool->QueueCount = 0;
}

/* textures of the pool's size and format, one for each buffer. */
static HRESULT pool_make_textures(POOL *pool)
{
    D3D11_TEXTURE2D_DESC desc = {.Width = (UINT)pool->Size.Width,
                                 .Height = (UINT)pool->Size.Height,
                                 .MipLevels = 1,
                                 .ArraySize = 1,
                                 .Format = (DXGI_FORMAT)pool->Format,
                                 .SampleDesc = {1, 0},
                                 .Usage = D3D11_USAGE_DEFAULT,
                                 .BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET};
    for (INT32 i = 0; i < pool->Buffers; i++) {
        HRESULT hr = pool->Device->lpVtbl->CreateTexture2D(pool->Device, &desc, nullptr, &pool->Textures[i]);
        if (FAILED(hr))
            return hr;
    }
    return S_OK;
}

static void pool_destroy(WRT_OBJECT *object)
{
    POOL *pool = (POOL *)object;
    pool_release_textures(pool);
    handlers_clear(&pool->Arrived);
    if (pool->Multithread)
        pool->Multithread->lpVtbl->Release(pool->Multithread);
    if (pool->Context)
        pool->Context->lpVtbl->Release(pool->Context);
    if (pool->Device)
        pool->Device->lpVtbl->Release(pool->Device);
    if (pool->Wrapper)
        pool->Wrapper->lpVtbl->Release(pool->Wrapper);
    DeleteCriticalSection(&pool->Lock);
    CbFree(pool);
}

static BOOL format_accepted(DirectXPixelFormat format)
{
    return format == 87 || format == 28 || format == 10;
}

/* the device of a wrapper, and its immediate context. */
static HRESULT pool_bind_device(POOL *pool, IDirect3DDevice *wrapper)
{
    IDirect3DDxgiInterfaceAccess *access;
    HRESULT hr = wrapper->lpVtbl->QueryInterface(wrapper, &IID_IDirect3DDxgiInterfaceAccess, (void **)&access);
    if (FAILED(hr))
        return hr;
    ID3D11Device *device;
    hr = access->lpVtbl->GetInterface(access, &IID_ID3D11Device, (void **)&device);
    access->lpVtbl->Release(access);
    if (FAILED(hr))
        return hr;
    if (pool->Context)
        pool->Context->lpVtbl->Release(pool->Context);
    if (pool->Multithread)
        pool->Multithread->lpVtbl->Release(pool->Multithread);
    if (pool->Device)
        pool->Device->lpVtbl->Release(pool->Device);
    if (pool->Wrapper)
        pool->Wrapper->lpVtbl->Release(pool->Wrapper);
    pool->Device = device;
    pool->Wrapper = wrapper;
    wrapper->lpVtbl->AddRef(wrapper);
    device->lpVtbl->GetImmediateContext(device, &pool->Context);
    pool->Multithread = nullptr;
    if (pool->Context)
        pool->Context->lpVtbl->QueryInterface(pool->Context, &IID_ID3D11Multithread, (void **)&pool->Multithread);
    return pool->Context ? S_OK : E_FAIL;
}

static HRESULT STDMETHODCALLTYPE pool_recreate(IDirect3D11CaptureFramePool *This, IDirect3DDevice *device,
                                               DirectXPixelFormat format, INT32 buffers, SizeInt32 size)
{
    POOL *pool = (POOL *)((WRT_IFACE *)This)->Owner;
    if (!device || buffers < 1 || buffers > POOL_MAX_BUFFERS || size.Width <= 0 || size.Height <= 0 ||
        !format_accepted(format))
        return E_INVALIDARG;
    EnterCriticalSection(&pool->Lock);
    HRESULT hr = pool->IsClosed ? RO_E_CLOSED : S_OK;
    if (SUCCEEDED(hr)) {
        pool_release_textures(pool);
        hr = pool_bind_device(pool, device);
        pool->Format = format;
        pool->Buffers = buffers;
        pool->Size = size;
        if (SUCCEEDED(hr))
            hr = pool_make_textures(pool);
        if (SUCCEEDED(hr) && pool->Session)
            pool->Session->Resend = TRUE;
    }
    LeaveCriticalSection(&pool->Lock);
    return hr;
}

/* the oldest frame there is, or none. */
static HRESULT STDMETHODCALLTYPE pool_try_get_next_frame(IDirect3D11CaptureFramePool *This,
                                                         IDirect3D11CaptureFrame **result)
{
    POOL *pool = (POOL *)((WRT_IFACE *)This)->Owner;
    if (!result)
        return E_POINTER;
    *result = nullptr;
    EnterCriticalSection(&pool->Lock);
    if (pool->IsClosed) {
        LeaveCriticalSection(&pool->Lock);
        return RO_E_CLOSED;
    }
    if (pool->QueueCount) {
        FRAME *frame = pool->Queue[0];
        memmove(pool->Queue, pool->Queue + 1, (pool->QueueCount - 1) * sizeof(pool->Queue[0]));
        pool->QueueCount--;
        *result = (IDirect3D11CaptureFrame *)&frame->Base.Ifaces[0];
    }
    LeaveCriticalSection(&pool->Lock);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pool_add_frame_arrived(IDirect3D11CaptureFramePool *This, ITypedEventHandler *handler,
                                                        EventRegistrationToken *token)
{
    POOL *pool = (POOL *)((WRT_IFACE *)This)->Owner;
    return handlers_add(&pool->Arrived, &pool->Lock, handler, token);
}

static HRESULT STDMETHODCALLTYPE pool_remove_frame_arrived(IDirect3D11CaptureFramePool *This,
                                                           EventRegistrationToken token)
{
    POOL *pool = (POOL *)((WRT_IFACE *)This)->Owner;
    return handlers_remove(&pool->Arrived, &pool->Lock, token);
}

static const IGraphicsCaptureSessionVtbl session_vtbl;
static const IGraphicsCaptureSession2Vtbl session2_vtbl;
static const IGraphicsCaptureSession3Vtbl session3_vtbl;
static const IClosableVtbl session_closable_vtbl;
static const WRT_CLASS session_class;

static void session_stop(SESSION *s);

static void session_destroy(WRT_OBJECT *object)
{
    SESSION *s = (SESSION *)object;
    session_stop(s);
    EnterCriticalSection(&s->Pool->Lock);
    if (s->Pool->Session == s)
        s->Pool->Session = nullptr;
    LeaveCriticalSection(&s->Pool->Lock);
    if (s->Shadow)
        s->Shadow->lpVtbl->Release(s->Shadow);
    CbFree(s->Pixels);
    if (s->Item)
        WrtRelease(&s->Item->Base.Ifaces[0]);
    if (s->Pool)
        WrtRelease(&s->Pool->Base.Ifaces[0]);
    CbFree(s);
}

static HRESULT STDMETHODCALLTYPE pool_create_capture_session(IDirect3D11CaptureFramePool *This,
                                                             IGraphicsCaptureItem *item, IGraphicsCaptureSession **result)
{
    POOL *pool = (POOL *)((WRT_IFACE *)This)->Owner;
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!item)
        return E_INVALIDARG;
    IGraphicsCaptureItem *iface;
    if (FAILED(item->lpVtbl->QueryInterface(item, &IID_IGraphicsCaptureItem, (void **)&iface)))
        return E_INVALIDARG;
    /* only the items made here can be captured. */
    ITEM *real = (ITEM *)((WRT_IFACE *)iface)->Owner;
    if (iface->lpVtbl != &item_vtbl) {
        iface->lpVtbl->Release(iface);
        return E_INVALIDARG;
    }
    SESSION *s = CbAlloc(sizeof(*s));
    if (!s) {
        WrtRelease(&real->Base.Ifaces[0]);
        return E_OUTOFMEMORY;
    }
    memset(s, 0, sizeof(*s));
    const void *all[4] = {&session_vtbl, &session2_vtbl, &session3_vtbl, &session_closable_vtbl};
    WrtInit(&s->Base, &session_class, all);
    s->Item = real;
    s->Pool = pool;
    WrtAddRef(&pool->Base.Ifaces[0]);
    pool->Session = s;
    s->Cursor = TRUE;
    s->Border = TRUE;
    *result = (IGraphicsCaptureSession *)&s->Base.Ifaces[0];
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE pool_get_dispatcher_queue(IDirect3D11CaptureFramePool *This, IInspectable **value)
{
    if (!value)
        return E_POINTER;
    *value = nullptr;
    return S_OK;
}

static const IDirect3D11CaptureFramePoolVtbl pool_vtbl = {
    WRT_INSPECTABLE_ENTRIES(IDirect3D11CaptureFramePool), pool_recreate, pool_try_get_next_frame, pool_add_frame_arrived,
    pool_remove_frame_arrived, pool_create_capture_session, pool_get_dispatcher_queue};

static HRESULT STDMETHODCALLTYPE pool_close(IClosable *This)
{
    POOL *pool = (POOL *)((WRT_IFACE *)This)->Owner;
    EnterCriticalSection(&pool->Lock);
    pool->IsClosed = TRUE;
    pool_release_textures(pool);
    LeaveCriticalSection(&pool->Lock);
    return S_OK;
}

static const IClosableVtbl pool_closable_vtbl = {WRT_INSPECTABLE_ENTRIES(IClosable), pool_close};

static const WRT_CLASS pool_class = {
    CbCapturePoolClass, 2, {&IID_IDirect3D11CaptureFramePool, &IID_IClosable}, 0, pool_destroy};

static HRESULT pool_make(IDirect3DDevice *device, DirectXPixelFormat format, INT32 buffers, SizeInt32 size,
                         IDirect3D11CaptureFramePool **result)
{
    if (!result)
        return E_POINTER;
    *result = nullptr;
    if (!device || buffers < 1 || buffers > POOL_MAX_BUFFERS || size.Width <= 0 || size.Height <= 0 ||
        !format_accepted(format))
        return E_INVALIDARG;
    POOL *pool = CbAlloc(sizeof(*pool));
    if (!pool)
        return E_OUTOFMEMORY;
    memset(pool, 0, sizeof(*pool));
    InitializeCriticalSection(&pool->Lock);
    const void *vtables[2] = {&pool_vtbl, &pool_closable_vtbl};
    WrtInit(&pool->Base, &pool_class, vtables);
    pool->Format = format;
    pool->Buffers = buffers;
    pool->Size = size;
    HRESULT hr = pool_bind_device(pool, device);
    if (SUCCEEDED(hr))
        hr = pool_make_textures(pool);
    if (FAILED(hr)) {
        WrtRelease(&pool->Base.Ifaces[0]);
        return hr;
    }
    *result = (IDirect3D11CaptureFramePool *)&pool->Base.Ifaces[0];
    return S_OK;
}

/* ------------------------------------------------------------------ sessions */

static void session_stop(SESSION *s)
{
    if (sessions_remove(s))
        WrtRelease(&s->Base.Ifaces[0]);
    if (s->Capture) {
        NtUserMimeCapture(MIME_CAPOP_CLOSE, s->Capture, nullptr, nullptr, 0);
        s->Capture = 0;
    }
}

static HRESULT STDMETHODCALLTYPE session_start_capture(IGraphicsCaptureSession *This)
{
    SESSION *s = (SESSION *)((WRT_IFACE *)This)->Owner;
    if (s->IsClosed)
        return RO_E_CLOSED;
    /* guess: a session starts once. */
    if (s->Started)
        return (HRESULT)0x8000000E;
    if (!initialize())
        return E_FAIL;
    MIME_CAPTURE_OPEN open = {};
    if (s->Item->Window) {
        open.Flags = MIME_CAPTURE_WINDOW_TARGET;
        open.Monitor = (ULONG)(ULONG_PTR)s->Item->Window;
    } else {
        open.Monitor = s->Item->MonitorNumber - 1;
    }
    LONG status = NtUserMimeCapture(MIME_CAPOP_OPEN, 0, (PUCHAR)&open, nullptr, 0);
    if (status < 0)
        return status == (LONG)0xC00000AB ? HRESULT_FROM_WIN32(ERROR_BUSY) : E_ACCESSDENIED;
    s->Capture = open.Handle;
    s->Started = TRUE;
    sessions_add(s);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE session_get_cursor(IGraphicsCaptureSession2 *This, BOOLEAN *value)
{
    if (!value)
        return E_POINTER;
    *value = ((SESSION *)((WRT_IFACE *)This)->Owner)->Cursor;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE session_put_cursor(IGraphicsCaptureSession2 *This, BOOLEAN value)
{
    ((SESSION *)((WRT_IFACE *)This)->Owner)->Cursor = value != 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE session_get_border(IGraphicsCaptureSession3 *This, BOOLEAN *value)
{
    if (!value)
        return E_POINTER;
    *value = ((SESSION *)((WRT_IFACE *)This)->Owner)->Border;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE session_put_border(IGraphicsCaptureSession3 *This, BOOLEAN value)
{
    ((SESSION *)((WRT_IFACE *)This)->Owner)->Border = value != 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE session_close(IClosable *This)
{
    SESSION *s = (SESSION *)((WRT_IFACE *)This)->Owner;
    s->IsClosed = TRUE;
    session_stop(s);
    return S_OK;
}

static const IGraphicsCaptureSessionVtbl session_vtbl = {WRT_INSPECTABLE_ENTRIES(IGraphicsCaptureSession),
                                                         session_start_capture};
static const IGraphicsCaptureSession2Vtbl session2_vtbl = {WRT_INSPECTABLE_ENTRIES(IGraphicsCaptureSession2),
                                                           session_get_cursor, session_put_cursor};
static const IGraphicsCaptureSession3Vtbl session3_vtbl = {WRT_INSPECTABLE_ENTRIES(IGraphicsCaptureSession3),
                                                           session_get_border, session_put_border};
static const IClosableVtbl session_closable_vtbl = {WRT_INSPECTABLE_ENTRIES(IClosable), session_close};
static const WRT_CLASS session_class = {CbCaptureSessionClass,
                                        4,
                                        {&IID_IGraphicsCaptureSession, &IID_IGraphicsCaptureSession2,
                                         &IID_IGraphicsCaptureSession3, &IID_IClosable},
                                        0,
                                        session_destroy};

static UINT64 system_time(void)
{
    LARGE_INTEGER counter, frequency;
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return (UINT64)counter.QuadPart / (UINT64)frequency.QuadPart * 10000000ull +
           (UINT64)counter.QuadPart % (UINT64)frequency.QuadPart * 10000000ull / (UINT64)frequency.QuadPart;
}

/* a copy of the session's texture into a free texture of the pool, queued for the program. false when all are held. */
static BOOL session_deliver(SESSION *s, SizeInt32 content, UINT crop_x, UINT crop_y)
{
    POOL *pool = s->Pool;
    EnterCriticalSection(&pool->Lock);
    INT32 slot = -1;
    if (!pool->IsClosed)
        for (INT32 i = 0; i < pool->Buffers; i++)
            if (!pool->Held[i] && pool->Textures[i]) {
                slot = i;
                break;
            }
    if (slot < 0 || pool->QueueCount >= POOL_MAX_BUFFERS) {
        LeaveCriticalSection(&pool->Lock);
        return FALSE;
    }
    FRAME *frame = CbAlloc(sizeof(*frame));
    if (!frame) {
        LeaveCriticalSection(&pool->Lock);
        return FALSE;
    }
    memset(frame, 0, sizeof(*frame));
    const void *vtables[2] = {&frame_vtbl, &closable_vtbl};
    WrtInit(&frame->Base, &frame_class, vtables);
    WrtAddRef(&pool->Base.Ifaces[0]);
    frame->Pool = pool;
    frame->Slot = (UINT)slot;
    frame->Texture = pool->Textures[slot];
    frame->Texture->lpVtbl->AddRef(frame->Texture);
    frame->ContentSize = content;
    frame->Time.Duration = (INT64)system_time();
    pool->Held[slot] = TRUE;
    pool->Queue[pool->QueueCount++] = frame;
    UINT w = (UINT)MIN(content.Width, pool->Size.Width), h = (UINT)MIN(content.Height, pool->Size.Height);
    D3D11_BOX box = {crop_x, crop_y, 0, crop_x + w, crop_y + h, 1};
    if (pool->Multithread)
        pool->Multithread->lpVtbl->Enter(pool->Multithread);
    pool->Context->lpVtbl->CopySubresourceRegion(pool->Context, (ID3D11Resource *)frame->Texture, 0, 0, 0, 0,
                                                 (ID3D11Resource *)s->Shadow, 0, &box);
    if (pool->Multithread)
        pool->Multithread->lpVtbl->Leave(pool->Multithread);
    LeaveCriticalSection(&pool->Lock);
    return TRUE;
}

/* the session's texture, the size of the frames the kernel gives. */
static HRESULT session_shadow(SESSION *s, UINT width, UINT height)
{
    if (s->Shadow && s->ShadowWidth == width && s->ShadowHeight == height)
        return S_OK;
    if (s->Shadow)
        s->Shadow->lpVtbl->Release(s->Shadow);
    s->Shadow = nullptr;
    D3D11_TEXTURE2D_DESC desc = {.Width = width,
                                 .Height = height,
                                 .MipLevels = 1,
                                 .ArraySize = 1,
                                 .Format = DXGI_FORMAT_B8G8R8A8_UNORM,
                                 .SampleDesc = {1, 0},
                                 .Usage = D3D11_USAGE_DEFAULT,
                                 .BindFlags = D3D11_BIND_SHADER_RESOURCE};
    HRESULT hr = s->Pool->Device->lpVtbl->CreateTexture2D(s->Pool->Device, &desc, nullptr, &s->Shadow);
    s->ShadowWidth = width;
    s->ShadowHeight = height;
    return hr;
}

static void session_poll(SESSION *s)
{
    POOL *pool = s->Pool;
    if (s->IsClosed || !s->Capture || pool->IsClosed)
        return;
    if (s->Resend && s->Shadow) {
        s->Resend = FALSE;
        if (session_deliver(s, s->LastContent, s->LastCropX, s->LastCropY))
            handlers_raise(&pool->Arrived, &pool->Lock, (IInspectable *)&pool->Base.Ifaces[0]);
    }
    MIME_CAPTURE_FRAME frame;
    LONG status = STATUS_BUFFER_TOO_SMALL;
    for (int attempt = 0; attempt < 2 && status == STATUS_BUFFER_TOO_SMALL; attempt++) {
        status = NtUserMimeCapture(MIME_CAPOP_FRAME, s->Capture, (PUCHAR)&frame, s->Pixels, (ULONG)s->PixelsSize);
        if (status == STATUS_BUFFER_TOO_SMALL) {
            SIZE_T need = (SIZE_T)frame.Width * frame.Height * 4;
            BYTE *pixels = CbAlloc(need);
            if (!pixels)
                return;
            CbFree(s->Pixels);
            s->Pixels = pixels;
            s->PixelsSize = need;
            s->Width = frame.Width;
            s->Height = frame.Height;
        }
    }
    if (status == STATUS_TIMEOUT)
        return;
    if (status < 0) {
        /* the window is gone, or the monitor changed: the item closes and the capture is over. */
        s->IsClosed = TRUE;
        item_close(s->Item);
        session_stop(s);
        return;
    }
    s->Width = frame.Width;
    s->Height = frame.Height;
    if (FAILED(session_shadow(s, frame.Width, frame.Height)))
        return;

    /* the dirty rectangles into the session's texture, and from there a frame. */
    RECT rects[128];
    UINT count = frame.DirtyBytes / sizeof(RECT);
    if (count > RTL_NUMBER_OF(rects))
        count = RTL_NUMBER_OF(rects);
    if (!count || NtUserMimeCapture(MIME_CAPOP_DIRTY, s->Capture, nullptr, (PUCHAR)rects, count * sizeof(RECT)) < 0) {
        rects[0] = (RECT){0, 0, (LONG)frame.Width, (LONG)frame.Height};
        count = 1;
    }
    UINT pitch = frame.Width * 4;
    if (pool->Multithread)
        pool->Multithread->lpVtbl->Enter(pool->Multithread);
    for (UINT i = 0; i < count; i++) {
        D3D11_BOX box = {(UINT)rects[i].left, (UINT)rects[i].top, 0, (UINT)rects[i].right, (UINT)rects[i].bottom, 1};
        pool->Context->lpVtbl->UpdateSubresource(pool->Context, (ID3D11Resource *)s->Shadow, 0, &box,
                                                 s->Pixels + (SIZE_T)rects[i].top * pitch + (SIZE_T)rects[i].left * 4,
                                                 pitch, 0);
    }
    if (pool->Multithread)
        pool->Multithread->lpVtbl->Leave(pool->Multithread);

    /* a window shows without its invisible border: the frame is the part of its pixels the bounds hold. */
    SizeInt32 content = {(INT32)frame.Width, (INT32)frame.Height};
    UINT crop_x = 0, crop_y = 0;
    if (s->Item->Window) {
        RECT wr, bounds;
        if (window_bounds(s->Item->Window, &wr, &bounds)) {
            crop_x = (UINT)MAX(bounds.left - wr.left, 0);
            crop_y = (UINT)MAX(bounds.top - wr.top, 0);
            content.Width = (INT32)MIN((LONG)(frame.Width - crop_x), bounds.right - bounds.left);
            content.Height = (INT32)MIN((LONG)(frame.Height - crop_y), bounds.bottom - bounds.top);
        }
    }
    s->LastContent = content;
    s->LastCropX = crop_x;
    s->LastCropY = crop_y;
    if (session_deliver(s, content, crop_x, crop_y))
        handlers_raise(&pool->Arrived, &pool->Lock, (IInspectable *)&pool->Base.Ifaces[0]);
}

/* ------------------------------------------------------------------ the factories */

WRT_INSPECTABLE_THUNKS(IGraphicsCaptureSessionStatics)
WRT_INSPECTABLE_THUNKS(IGraphicsCaptureItemStatics)
WRT_INSPECTABLE_THUNKS(IDirect3D11CaptureFramePoolStatics)
WRT_INSPECTABLE_THUNKS(IDirect3D11CaptureFramePoolStatics2)
WRT_UNKNOWN_THUNKS(IGraphicsCaptureItemInterop)

static HRESULT STDMETHODCALLTYPE statics_is_supported(IGraphicsCaptureSessionStatics *This, BOOLEAN *result)
{
    if (!result)
        return E_POINTER;
    *result = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE statics_create(IDirect3D11CaptureFramePoolStatics *This, IDirect3DDevice *device,
                                                DirectXPixelFormat format, INT32 buffers, SizeInt32 size,
                                                IDirect3D11CaptureFramePool **result)
{
    return pool_make(device, format, buffers, size, result);
}

static HRESULT STDMETHODCALLTYPE statics2_create_free_threaded(IDirect3D11CaptureFramePoolStatics2 *This,
                                                               IDirect3DDevice *device, DirectXPixelFormat format,
                                                               INT32 buffers, SizeInt32 size,
                                                               IDirect3D11CaptureFramePool **result)
{
    return pool_make(device, format, buffers, size, result);
}

static const IGraphicsCaptureSessionStaticsVtbl session_statics_vtbl = {
    WRT_INSPECTABLE_ENTRIES(IGraphicsCaptureSessionStatics), statics_is_supported};
static const IGraphicsCaptureItemStaticsVtbl item_statics_vtbl = {WRT_INSPECTABLE_ENTRIES(IGraphicsCaptureItemStatics),
                                                                  item_statics_create_from_visual};
static const IGraphicsCaptureItemInteropVtbl interop_vtbl = {WRT_UNKNOWN_ENTRIES(IGraphicsCaptureItemInterop),
                                                             interop_create_for_window, interop_create_for_monitor};
static const IDirect3D11CaptureFramePoolStaticsVtbl pool_statics_vtbl = {
    WRT_INSPECTABLE_ENTRIES(IDirect3D11CaptureFramePoolStatics), statics_create};
static const IDirect3D11CaptureFramePoolStatics2Vtbl pool_statics2_vtbl = {
    WRT_INSPECTABLE_ENTRIES(IDirect3D11CaptureFramePoolStatics2), statics2_create_free_threaded};

/* a factory lives as long as the dll: it counts references and never goes. */
static void factory_destroy(WRT_OBJECT *object)
{
    object->Refs = 1;
}

static const WRT_CLASS session_factory_class = {CbCaptureSessionClass, 1, {&IID_IGraphicsCaptureSessionStatics}, 0,
                                                factory_destroy};
static const WRT_CLASS item_factory_class = {
    CbCaptureItemClass, 2, {&IID_IGraphicsCaptureItemStatics, &IID_IGraphicsCaptureItemInterop}, 1u << 1,
    factory_destroy};
static const WRT_CLASS pool_factory_class = {
    CbCapturePoolClass, 2, {&IID_IDirect3D11CaptureFramePoolStatics, &IID_IDirect3D11CaptureFramePoolStatics2}, 0,
    factory_destroy};

static WRT_OBJECT g_session_factory, g_item_factory, g_pool_factory;
static INIT_ONCE g_factories = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK make_factories(PINIT_ONCE o, PVOID parameter, PVOID *context)
{
    const void *session[1] = {&session_statics_vtbl};
    const void *item[2] = {&item_statics_vtbl, &interop_vtbl};
    const void *pool[2] = {&pool_statics_vtbl, &pool_statics2_vtbl};
    WrtInit(&g_session_factory, &session_factory_class, session);
    WrtInit(&g_item_factory, &item_factory_class, item);
    WrtInit(&g_pool_factory, &pool_factory_class, pool);
    return TRUE;
}

HRESULT CbCaptureSessionFactory(REFIID riid, void **factory)
{
    InitOnceExecuteOnce(&g_factories, make_factories, nullptr, nullptr);
    return WrtQuery(&g_session_factory.Ifaces[0], riid, factory);
}

HRESULT CbCaptureItemFactory(REFIID riid, void **factory)
{
    InitOnceExecuteOnce(&g_factories, make_factories, nullptr, nullptr);
    return WrtQuery(&g_item_factory.Ifaces[0], riid, factory);
}

HRESULT CbCapturePoolFactory(REFIID riid, void **factory)
{
    InitOnceExecuteOnce(&g_factories, make_factories, nullptr, nullptr);
    return WrtQuery(&g_pool_factory.Ifaces[0], riid, factory);
}
