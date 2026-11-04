#include "uianimation_private.h"

extern HINSTANCE UaInstance;

#define TIMER_TICK (WM_USER + 1)
#define TIMER_RESTART (WM_USER + 2)

typedef struct UaTimer {
    const IUIAnimationTimerVtbl *lpVtbl;
    const IUIAnimationTimerClientEventHandlerVtbl *client_vtbl;
    LONG refs;
    DWORD thread;
    HWND hwnd;
    BOOL enabled;
    LARGE_INTEGER frequency;
    IUIAnimationTimerUpdateHandler *update_handler;
    IUIAnimationTimerEventHandler *event_handler;
    UINT threshold;
    /* milliseconds between ticks, from the composition rate. */
    UINT interval;
    double last_tick;
    UINT frames;
    double rate_since;
} UaTimer;

static UaTimer *from_client(IUIAnimationTimerClientEventHandler *iface) {
    return (UaTimer *)((char *)iface - offsetof(UaTimer, client_vtbl));
}

static double timer_now(UaTimer *timer) {
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)timer->frequency.QuadPart;
}

static HRESULT start(UaTimer *timer) {
    if (timer->enabled)
        return S_FALSE;
    timer->enabled = TRUE;
    if (!SetTimer(timer->hwnd, 1, timer->interval > 1 ? timer->interval : 1, nullptr)) {
        timer->enabled = FALSE;
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return S_OK;
}

static HRESULT stop(UaTimer *timer) {
    if (!timer->enabled)
        return S_FALSE;
    timer->enabled = FALSE;
    KillTimer(timer->hwnd, 1);
    return S_OK;
}

static HRESULT tick(UaTimer *timer) {
    if (!timer->enabled)
        return S_FALSE;
    double now = timer_now(timer);
    ((IUnknown *)timer)->lpVtbl->AddRef((IUnknown *)timer);
    HRESULT hr = S_OK;
    if (timer->event_handler)
        hr = timer->event_handler->lpVtbl->OnPreUpdate(timer->event_handler);
    UI_ANIMATION_UPDATE_RESULT result = UI_ANIMATION_UPDATE_VARIABLES_CHANGED;
    if (SUCCEEDED(hr) && timer->update_handler) {
        hr = timer->update_handler->lpVtbl->OnUpdate(timer->update_handler, now, &result);
        if (SUCCEEDED(hr) && (UINT)result > UI_ANIMATION_UPDATE_VARIABLES_CHANGED)
            hr = UI_E_INVALID_OUTPUT;
    }
    if (SUCCEEDED(hr) && result == UI_ANIMATION_UPDATE_VARIABLES_CHANGED && timer->event_handler)
        hr = timer->event_handler->lpVtbl->OnPostUpdate(timer->event_handler);
    /* the frame rate over the last second, against the threshold the caller set. */
    timer->frames++;
    if (timer->rate_since == 0.0)
        timer->rate_since = now;
    if (now - timer->rate_since >= 1.0) {
        UINT rate = (UINT)(timer->frames / (now - timer->rate_since));
        timer->frames = 0;
        timer->rate_since = now;
        if (SUCCEEDED(hr) && rate < timer->threshold && timer->event_handler && timer->enabled)
            hr = timer->event_handler->lpVtbl->OnRenderingTooSlow(timer->event_handler, rate);
    }
    timer->last_tick = now;
    ((IUnknown *)timer)->lpVtbl->Release((IUnknown *)timer);
    return hr;
}

static LRESULT CALLBACK timer_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lparam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)create->lpCreateParams);
    }
    UaTimer *timer = (UaTimer *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (timer && (message == WM_TIMER || message == TIMER_TICK))
        return tick(timer);
    if (timer && message == TIMER_RESTART) {
        BOOL was = timer->enabled;
        if (stop(timer) == S_OK && was)
            start(timer);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

static void register_class(void) {
    static LONG registered;
    if (InterlockedCompareExchange(&registered, 1, 0))
        return;
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = timer_proc;
    wc.hInstance = UaInstance;
    wc.lpszClassName = L"ANIMATION_TIMER_HWND";
    RegisterClassW(&wc);
}

/* ------------------------------------------------------------------ IUIAnimationTimer */

static HRESULT STDMETHODCALLTYPE timer_query(IUIAnimationTimer *iface, REFIID iid, void **out) {
    UaTimer *timer = (UaTimer *)iface;
    if (!out)
        return E_POINTER;
    if (IsEqualGUID(iid, &IID_IUIAnimationTimerClientEventHandler)) {
        *out = &timer->client_vtbl;
        InterlockedIncrement(&timer->refs);
        return S_OK;
    }
    return UaQuery((IUnknown *)iface, &IID_IUIAnimationTimer, iid, out);
}

static ULONG STDMETHODCALLTYPE timer_addref(IUIAnimationTimer *iface) {
    return InterlockedIncrement(&((UaTimer *)iface)->refs);
}

static ULONG STDMETHODCALLTYPE timer_release(IUIAnimationTimer *iface) {
    UaTimer *timer = (UaTimer *)iface;
    ULONG refs = InterlockedDecrement(&timer->refs);
    if (refs)
        return refs;
    stop(timer);
    if (timer->hwnd) {
        SetWindowLongPtrW(timer->hwnd, GWLP_USERDATA, 0);
        DestroyWindow(timer->hwnd);
    }
    if (timer->update_handler)
        timer->update_handler->lpVtbl->Release(timer->update_handler);
    if (timer->event_handler)
        timer->event_handler->lpVtbl->Release(timer->event_handler);
    UaFree(timer);
    UaObjectCountChange(-1);
    return 0;
}

static HRESULT STDMETHODCALLTYPE timer_set_update_handler(IUIAnimationTimer *iface,
                                                          IUIAnimationTimerUpdateHandler *handler,
                                                          UI_ANIMATION_IDLE_BEHAVIOR idle) {
    UaTimer *timer = (UaTimer *)iface;
    if ((UINT)idle > UI_ANIMATION_IDLE_BEHAVIOR_DISABLE)
        return E_INVALIDARG;
    if (GetCurrentThreadId() != timer->thread)
        return UI_E_WRONG_THREAD;
    IUIAnimationTimerUpdateHandler *old = timer->update_handler;
    if (old) {
        timer->update_handler = nullptr;
        HRESULT hr = old->lpVtbl->ClearTimerClientEventHandler(old);
        old->lpVtbl->Release(old);
        if (FAILED(hr))
            return hr;
    }
    if (idle == UI_ANIMATION_IDLE_BEHAVIOR_DISABLE && handler) {
        HRESULT hr = handler->lpVtbl->SetTimerClientEventHandler(
            handler, (IUIAnimationTimerClientEventHandler *)&timer->client_vtbl);
        if (FAILED(hr))
            return hr;
    }
    if (handler)
        handler->lpVtbl->AddRef(handler);
    timer->update_handler = handler;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE timer_set_event_handler(IUIAnimationTimer *iface,
                                                         IUIAnimationTimerEventHandler *handler) {
    UaTimer *timer = (UaTimer *)iface;
    if (GetCurrentThreadId() != timer->thread)
        return UI_E_WRONG_THREAD;
    if (handler)
        handler->lpVtbl->AddRef(handler);
    if (timer->event_handler)
        timer->event_handler->lpVtbl->Release(timer->event_handler);
    timer->event_handler = handler;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE timer_enable(IUIAnimationTimer *iface) {
    UaTimer *timer = (UaTimer *)iface;
    if (GetCurrentThreadId() != timer->thread)
        return UI_E_WRONG_THREAD;
    return start(timer);
}

static HRESULT STDMETHODCALLTYPE timer_disable(IUIAnimationTimer *iface) {
    UaTimer *timer = (UaTimer *)iface;
    if (GetCurrentThreadId() != timer->thread)
        return UI_E_WRONG_THREAD;
    return stop(timer);
}

static HRESULT STDMETHODCALLTYPE timer_is_enabled(IUIAnimationTimer *iface) {
    UaTimer *timer = (UaTimer *)iface;
    if (GetCurrentThreadId() != timer->thread)
        return UI_E_WRONG_THREAD;
    return timer->enabled ? S_OK : S_FALSE;
}

static HRESULT STDMETHODCALLTYPE timer_get_time(IUIAnimationTimer *iface, double *seconds) {
    UaTimer *timer = (UaTimer *)iface;
    if (!seconds)
        return E_POINTER;
    *seconds = 0.0;
    if (GetCurrentThreadId() != timer->thread)
        return UI_E_WRONG_THREAD;
    *seconds = timer_now(timer);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE timer_set_threshold(IUIAnimationTimer *iface, UINT fps) {
    UaTimer *timer = (UaTimer *)iface;
    if (GetCurrentThreadId() != timer->thread)
        return UI_E_WRONG_THREAD;
    timer->threshold = fps;
    return S_OK;
}

static const IUIAnimationTimerVtbl g_timer_vtbl = {
    timer_query,  timer_addref,  timer_release,    timer_set_update_handler, timer_set_event_handler,
    timer_enable, timer_disable, timer_is_enabled, timer_get_time,           timer_set_threshold,
};

/* ------------------------------------------------------------------ IUIAnimationTimerClientEventHandler */

static HRESULT STDMETHODCALLTYPE client_query(IUIAnimationTimerClientEventHandler *iface, REFIID iid, void **out) {
    return timer_query((IUIAnimationTimer *)from_client(iface), iid, out);
}

static ULONG STDMETHODCALLTYPE client_addref(IUIAnimationTimerClientEventHandler *iface) {
    return timer_addref((IUIAnimationTimer *)from_client(iface));
}

static ULONG STDMETHODCALLTYPE client_release(IUIAnimationTimerClientEventHandler *iface) {
    return timer_release((IUIAnimationTimer *)from_client(iface));
}

static HRESULT STDMETHODCALLTYPE client_status_changed(IUIAnimationTimerClientEventHandler *iface,
                                                       UI_ANIMATION_TIMER_CLIENT_STATUS status,
                                                       UI_ANIMATION_TIMER_CLIENT_STATUS previous) {
    UaTimer *timer = from_client(iface);
    if (status == previous)
        return S_OK;
    HRESULT hr = S_OK;
    if (status == UI_ANIMATION_TIMER_CLIENT_IDLE)
        hr = stop(timer);
    else if (status == UI_ANIMATION_TIMER_CLIENT_BUSY)
        hr = start(timer);
    return FAILED(hr) ? hr : S_OK;
}

static const IUIAnimationTimerClientEventHandlerVtbl g_client_vtbl = {
    client_query,
    client_addref,
    client_release,
    client_status_changed,
};

HRESULT UaTimerCreate(REFIID iid, void **out) {
    UaTimer *timer = UaAlloc(sizeof(*timer));
    if (!timer)
        return E_OUTOFMEMORY;
    timer->lpVtbl = &g_timer_vtbl;
    timer->client_vtbl = &g_client_vtbl;
    timer->refs = 1;
    timer->thread = GetCurrentThreadId();
    timer->interval = 16;
    QueryPerformanceFrequency(&timer->frequency);
    UaObjectCountChange(1);
    register_class();
    timer->hwnd = CreateWindowExW(0, L"ANIMATION_TIMER_HWND", nullptr, 0, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                  CW_USEDEFAULT, HWND_MESSAGE, nullptr, UaInstance, timer);
    if (!timer->hwnd) {
        timer_release((IUIAnimationTimer *)timer);
        return UI_E_CREATE_FAILED;
    }
    HRESULT hr = timer_query((IUIAnimationTimer *)timer, iid, out);
    timer_release((IUIAnimationTimer *)timer);
    return hr;
}
