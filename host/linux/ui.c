/*
 * the linux display, through sdl3 on wayland or x11: one borderless host
 * window per guest top level window, fed with the pixels the guest window
 * manager renders, its frame included. without a display server the
 * machine still runs, with windows drawn nowhere.
 *
 * sdl wants its video calls on the thread that initialized it, the main
 * thread here: vcpu threads copy what they present into a window's staging
 * buffer and post the rest as calls the main loop runs.
 */
#include "../ui/ui.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_WINDOWS 256

typedef struct UiWindow {
    uint32_t id;
    bool used;
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    uint32_t texture_width, texture_height;
    MimeWinConfig config;
    bool configured;
    bool shown;
    bool popup;          /* created as a popup of its owner */
    uint32_t owner;      /* owner the popup belongs to */
    bool fullscreen;
    bool state_pending;
    uint32_t state_request;
    int state_command;
    double wheel_x, wheel_y;

    pthread_mutex_t lock;
    uint8_t *pixels; /* the latest guest surface, copied by the presenting vcpu */
    uint32_t width, height;
    MimeRect dirty;
    bool has_dirty;
    bool present_queued;
    MimeWinConfig pending_config;
    bool config_queued;
} UiWindow;

static UiWindow g_windows[MAX_WINDOWS];
static pthread_mutex_t g_windows_lock = PTHREAD_MUTEX_INITIALIZER;
static MimeUiInputSink g_sink;
static void *g_sink_ctx;
static bool g_video;          /* sdl runs; without it the display is headless */
static Uint32 g_call_event;   /* the event type of calls posted to the main thread */
static pthread_t g_main_thread;
static double g_scale = 1.0;  /* guest pixels per sdl window coordinate */
static MimeDisplayInfo g_display;
static SDL_Cursor *g_cursors[MIME_CURSOR_HIDDEN];
static uint32_t g_cursor;
static bool g_stop;
static pthread_mutex_t g_stop_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_stop_cond = PTHREAD_COND_INITIALIZER;

static pthread_mutex_t g_clip_lock = PTHREAD_MUTEX_INITIALIZER;
static uint16_t *g_clip_text;
static size_t g_clip_chars;
static bool g_clip_stale;

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void emit(MimeInputEvent *ev)
{
    ev->time_ms = now_ms();
    if (g_sink)
        g_sink(g_sink_ctx, ev);
}

/* runs fn on the main thread, at once when already there. */
static void on_main(void (*fn)(void *), void *arg)
{
    if (!g_video)
        return;
    if (pthread_equal(pthread_self(), g_main_thread)) {
        fn(arg);
        return;
    }
    SDL_Event ev;
    SDL_zero(ev);
    ev.type = g_call_event;
    ev.user.data1 = (void *)fn;
    ev.user.data2 = arg;
    SDL_PushEvent(&ev);
}

static UiWindow *find_window(uint32_t id)
{
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (g_windows[i].used && g_windows[i].id == id)
            return &g_windows[i];
    return NULL;
}

static UiWindow *find_by_sdl(SDL_WindowID id)
{
    SDL_Window *window = SDL_GetWindowFromID(id);
    if (!window)
        return NULL;
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (g_windows[i].used && g_windows[i].window == window)
            return &g_windows[i];
    return NULL;
}

/* the window relative part of the guest window that is drawn. */
static MimeRect visible_part(const MimeWinConfig *c)
{
    MimeRect v = c->visible;
    if (v.right <= v.left || v.bottom <= v.top)
        v = (MimeRect){0, 0, c->rect.right - c->rect.left, c->rect.bottom - c->rect.top};
    return v;
}

/* the window relative part the host window covers: the drawn part and the resize border around it. */
static MimeRect host_part(const MimeWinConfig *c)
{
    MimeRect v = visible_part(c), in = c->input;
    if (in.right <= in.left || in.bottom <= in.top)
        return v;
    return (MimeRect){MIME_MIN(v.left, in.left), MIME_MIN(v.top, in.top), MIME_MAX(v.right, in.right),
                      MIME_MAX(v.bottom, in.bottom)};
}

/* a point of the host window, in sdl window coordinates, as guest screen pixels. */
static void window_point_to_guest(UiWindow *w, float x, float y, int32_t *gx, int32_t *gy)
{
    MimeRect host = host_part(&w->config);
    *gx = w->config.rect.left + host.left + (int32_t)lround(x * g_scale);
    *gy = w->config.rect.top + host.top + (int32_t)lround(y * g_scale);
}

static uint32_t modifiers_of(SDL_Keymod mod)
{
    uint32_t m = 0;
    if (mod & SDL_KMOD_SHIFT)
        m |= MIME_MOD_SHIFT;
    if (mod & SDL_KMOD_CTRL)
        m |= MIME_MOD_CONTROL;
    if (mod & SDL_KMOD_ALT)
        m |= MIME_MOD_ALT;
    if (mod & SDL_KMOD_GUI)
        m |= MIME_MOD_WIN;
    if (mod & SDL_KMOD_CAPS)
        m |= MIME_MOD_CAPS;
    return m;
}

/* ------------------------------------------------------------------ display */

static void update_display(void)
{
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    SDL_Rect bounds, usable;
    if (!display || !SDL_GetDisplayBounds(display, &bounds))
        return;
    if (!SDL_GetDisplayUsableBounds(display, &usable))
        usable = bounds;
    float scale = SDL_GetDisplayContentScale(display);
    g_scale = scale >= 1.0f ? scale : 1.0;
    const SDL_DisplayMode *mode = SDL_GetCurrentDisplayMode(display);
    g_display.width = (uint32_t)lround(bounds.w * g_scale);
    g_display.height = (uint32_t)lround(bounds.h * g_scale);
    g_display.work_x = (uint32_t)lround((usable.x - bounds.x) * g_scale);
    g_display.work_y = (uint32_t)lround((usable.y - bounds.y) * g_scale);
    g_display.work_width = (uint32_t)lround(usable.w * g_scale);
    g_display.work_height = (uint32_t)lround(usable.h * g_scale);
    g_display.dpi = (uint32_t)lround(96.0 * g_scale);
    g_display.refresh_hz = mode && mode->refresh_rate > 0 ? (uint32_t)lround(mode->refresh_rate) : 60;
}

void mime_ui_get_display(MimeDisplayInfo *info)
{
    if (!g_display.width) {
        g_display = (MimeDisplayInfo){.width = 1920, .height = 1080, .work_width = 1920, .work_height = 1032,
                                      .dpi = 96, .refresh_hz = 60};
        if (g_video)
            update_display();
    }
    *info = g_display;
}

/* ------------------------------------------------------------------ keyboard */

typedef struct KeyMap {
    uint8_t vk;
    uint16_t scan; /* set 1, 0x100 marks extended keys */
} KeyMap;

/* by usb usage, the sdl scancode. */
static const KeyMap g_keymap[SDL_SCANCODE_COUNT] = {
    [SDL_SCANCODE_A] = {'A', 0x1E}, [SDL_SCANCODE_B] = {'B', 0x30}, [SDL_SCANCODE_C] = {'C', 0x2E},
    [SDL_SCANCODE_D] = {'D', 0x20}, [SDL_SCANCODE_E] = {'E', 0x12}, [SDL_SCANCODE_F] = {'F', 0x21},
    [SDL_SCANCODE_G] = {'G', 0x22}, [SDL_SCANCODE_H] = {'H', 0x23}, [SDL_SCANCODE_I] = {'I', 0x17},
    [SDL_SCANCODE_J] = {'J', 0x24}, [SDL_SCANCODE_K] = {'K', 0x25}, [SDL_SCANCODE_L] = {'L', 0x26},
    [SDL_SCANCODE_M] = {'M', 0x32}, [SDL_SCANCODE_N] = {'N', 0x31}, [SDL_SCANCODE_O] = {'O', 0x18},
    [SDL_SCANCODE_P] = {'P', 0x19}, [SDL_SCANCODE_Q] = {'Q', 0x10}, [SDL_SCANCODE_R] = {'R', 0x13},
    [SDL_SCANCODE_S] = {'S', 0x1F}, [SDL_SCANCODE_T] = {'T', 0x14}, [SDL_SCANCODE_U] = {'U', 0x16},
    [SDL_SCANCODE_V] = {'V', 0x2F}, [SDL_SCANCODE_W] = {'W', 0x11}, [SDL_SCANCODE_X] = {'X', 0x2D},
    [SDL_SCANCODE_Y] = {'Y', 0x15}, [SDL_SCANCODE_Z] = {'Z', 0x2C},
    [SDL_SCANCODE_1] = {'1', 0x02}, [SDL_SCANCODE_2] = {'2', 0x03}, [SDL_SCANCODE_3] = {'3', 0x04},
    [SDL_SCANCODE_4] = {'4', 0x05}, [SDL_SCANCODE_5] = {'5', 0x06}, [SDL_SCANCODE_6] = {'6', 0x07},
    [SDL_SCANCODE_7] = {'7', 0x08}, [SDL_SCANCODE_8] = {'8', 0x09}, [SDL_SCANCODE_9] = {'9', 0x0A},
    [SDL_SCANCODE_0] = {'0', 0x0B},
    [SDL_SCANCODE_RETURN] = {0x0D, 0x1C}, [SDL_SCANCODE_ESCAPE] = {0x1B, 0x01},
    [SDL_SCANCODE_BACKSPACE] = {0x08, 0x0E}, [SDL_SCANCODE_TAB] = {0x09, 0x0F},
    [SDL_SCANCODE_SPACE] = {0x20, 0x39}, [SDL_SCANCODE_MINUS] = {0xBD, 0x0C},
    [SDL_SCANCODE_EQUALS] = {0xBB, 0x0D}, [SDL_SCANCODE_LEFTBRACKET] = {0xDB, 0x1A},
    [SDL_SCANCODE_RIGHTBRACKET] = {0xDD, 0x1B}, [SDL_SCANCODE_BACKSLASH] = {0xDC, 0x2B},
    [SDL_SCANCODE_NONUSHASH] = {0xDC, 0x2B}, [SDL_SCANCODE_SEMICOLON] = {0xBA, 0x27},
    [SDL_SCANCODE_APOSTROPHE] = {0xDE, 0x28}, [SDL_SCANCODE_GRAVE] = {0xC0, 0x29},
    [SDL_SCANCODE_COMMA] = {0xBC, 0x33}, [SDL_SCANCODE_PERIOD] = {0xBE, 0x34},
    [SDL_SCANCODE_SLASH] = {0xBF, 0x35}, [SDL_SCANCODE_CAPSLOCK] = {0x14, 0x3A},
    [SDL_SCANCODE_F1] = {0x70, 0x3B}, [SDL_SCANCODE_F2] = {0x71, 0x3C}, [SDL_SCANCODE_F3] = {0x72, 0x3D},
    [SDL_SCANCODE_F4] = {0x73, 0x3E}, [SDL_SCANCODE_F5] = {0x74, 0x3F}, [SDL_SCANCODE_F6] = {0x75, 0x40},
    [SDL_SCANCODE_F7] = {0x76, 0x41}, [SDL_SCANCODE_F8] = {0x77, 0x42}, [SDL_SCANCODE_F9] = {0x78, 0x43},
    [SDL_SCANCODE_F10] = {0x79, 0x44}, [SDL_SCANCODE_F11] = {0x7A, 0x57}, [SDL_SCANCODE_F12] = {0x7B, 0x58},
    [SDL_SCANCODE_F13] = {0x7C, 0x64}, [SDL_SCANCODE_F14] = {0x7D, 0x65}, [SDL_SCANCODE_F15] = {0x7E, 0x66},
    [SDL_SCANCODE_F16] = {0x7F, 0x67}, [SDL_SCANCODE_F17] = {0x80, 0x68}, [SDL_SCANCODE_F18] = {0x81, 0x69},
    [SDL_SCANCODE_F19] = {0x82, 0x6A}, [SDL_SCANCODE_F20] = {0x83, 0x6B}, [SDL_SCANCODE_F21] = {0x84, 0x6C},
    [SDL_SCANCODE_F22] = {0x85, 0x6D}, [SDL_SCANCODE_F23] = {0x86, 0x6E}, [SDL_SCANCODE_F24] = {0x87, 0x76},
    [SDL_SCANCODE_PRINTSCREEN] = {0x2C, 0x137}, [SDL_SCANCODE_SCROLLLOCK] = {0x91, 0x46},
    [SDL_SCANCODE_PAUSE] = {0x13, 0x45}, [SDL_SCANCODE_INSERT] = {0x2D, 0x152},
    [SDL_SCANCODE_HOME] = {0x24, 0x147}, [SDL_SCANCODE_PAGEUP] = {0x21, 0x149},
    [SDL_SCANCODE_DELETE] = {0x2E, 0x153}, [SDL_SCANCODE_END] = {0x23, 0x14F},
    [SDL_SCANCODE_PAGEDOWN] = {0x22, 0x151}, [SDL_SCANCODE_RIGHT] = {0x27, 0x14D},
    [SDL_SCANCODE_LEFT] = {0x25, 0x14B}, [SDL_SCANCODE_DOWN] = {0x28, 0x150}, [SDL_SCANCODE_UP] = {0x26, 0x148},
    [SDL_SCANCODE_NUMLOCKCLEAR] = {0x90, 0x145}, [SDL_SCANCODE_KP_DIVIDE] = {0x6F, 0x135},
    [SDL_SCANCODE_KP_MULTIPLY] = {0x6A, 0x37}, [SDL_SCANCODE_KP_MINUS] = {0x6D, 0x4A},
    [SDL_SCANCODE_KP_PLUS] = {0x6B, 0x4E}, [SDL_SCANCODE_KP_ENTER] = {0x0D, 0x11C},
    [SDL_SCANCODE_KP_1] = {0x61, 0x4F}, [SDL_SCANCODE_KP_2] = {0x62, 0x50}, [SDL_SCANCODE_KP_3] = {0x63, 0x51},
    [SDL_SCANCODE_KP_4] = {0x64, 0x4B}, [SDL_SCANCODE_KP_5] = {0x65, 0x4C}, [SDL_SCANCODE_KP_6] = {0x66, 0x4D},
    [SDL_SCANCODE_KP_7] = {0x67, 0x47}, [SDL_SCANCODE_KP_8] = {0x68, 0x48}, [SDL_SCANCODE_KP_9] = {0x69, 0x49},
    [SDL_SCANCODE_KP_0] = {0x60, 0x52}, [SDL_SCANCODE_KP_PERIOD] = {0x6E, 0x53},
    [SDL_SCANCODE_NONUSBACKSLASH] = {0xE2, 0x56}, [SDL_SCANCODE_APPLICATION] = {0x5D, 0x15D},
    [SDL_SCANCODE_LCTRL] = {0xA2, 0x1D}, [SDL_SCANCODE_LSHIFT] = {0xA0, 0x2A}, [SDL_SCANCODE_LALT] = {0xA4, 0x38},
    [SDL_SCANCODE_LGUI] = {0x5B, 0x15B}, [SDL_SCANCODE_RCTRL] = {0xA3, 0x11D}, [SDL_SCANCODE_RSHIFT] = {0xA1, 0x36},
    [SDL_SCANCODE_RALT] = {0xA5, 0x138}, [SDL_SCANCODE_RGUI] = {0x5C, 0x15C},
};

static void emit_key(const SDL_KeyboardEvent *key)
{
    UiWindow *w = find_by_sdl(key->windowID);
    if (!w || key->scancode >= SDL_SCANCODE_COUNT || !g_keymap[key->scancode].vk)
        return;
    const KeyMap *k = &g_keymap[key->scancode];
    MimeInputEvent ev = {0};
    ev.type = key->down ? MIME_INPUT_KEY_DOWN : MIME_INPUT_KEY_UP;
    ev.window_id = w->id;
    ev.vk = k->vk;
    /* letter keys take the virtual key of the letter they type in the host layout, like windows layouts do. */
    if (k->vk >= 'A' && k->vk <= 'Z' && key->key >= 'a' && key->key <= 'z')
        ev.vk = (uint32_t)(key->key - 32);
    ev.scancode = k->scan;
    ev.modifiers = modifiers_of(key->mod);
    if (key->repeat)
        ev.flags = 2;
    emit(&ev);
}

static void emit_text(const SDL_TextInputEvent *text)
{
    UiWindow *w = find_by_sdl(text->windowID);
    if (!w || !text->text)
        return;
    uint16_t buf[64];
    size_t n = mime_utf8_to_utf16(buf, MIME_ARRAY_COUNT(buf), text->text);
    for (size_t i = 0; i < n; i++) {
        MimeInputEvent ev = {0};
        ev.type = MIME_INPUT_CHAR;
        ev.window_id = w->id;
        ev.ch = buf[i];
        emit(&ev);
    }
}

/* ------------------------------------------------------------------ mouse */

static void emit_mouse(SDL_WindowID id, float x, float y, uint32_t type, uint32_t button)
{
    UiWindow *w = find_by_sdl(id);
    if (!w)
        return;
    MimeInputEvent ev = {0};
    ev.type = type;
    ev.window_id = w->id;
    window_point_to_guest(w, x, y, &ev.x, &ev.y);
    ev.button = button;
    ev.modifiers = modifiers_of(SDL_GetModState());
    emit(&ev);
}

static uint32_t mouse_button(Uint8 button)
{
    switch (button) {
    case SDL_BUTTON_LEFT: return MIME_MOUSE_LEFT;
    case SDL_BUTTON_RIGHT: return MIME_MOUSE_RIGHT;
    case SDL_BUTTON_MIDDLE: return MIME_MOUSE_MIDDLE;
    case SDL_BUTTON_X1: return MIME_MOUSE_X1;
    case SDL_BUTTON_X2: return MIME_MOUSE_X2;
    default: return 0;
    }
}

static void emit_wheel(const SDL_MouseWheelEvent *wheel)
{
    UiWindow *w = find_by_sdl(wheel->windowID);
    if (!w)
        return;
    float sign = wheel->direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
    MimeInputEvent ev = {0};
    ev.type = MIME_INPUT_MOUSE_WHEEL;
    ev.window_id = w->id;
    window_point_to_guest(w, wheel->mouse_x, wheel->mouse_y, &ev.x, &ev.y);
    ev.modifiers = modifiers_of(SDL_GetModState());
    /* fractions of a notch from smooth scrolling add up to whole wheel deltas of 120 per notch. */
    w->wheel_y += wheel->y * sign * 120.0;
    w->wheel_x += wheel->x * sign * 120.0;
    int32_t dy = (int32_t)w->wheel_y, dx = (int32_t)w->wheel_x;
    w->wheel_y -= dy;
    w->wheel_x -= dx;
    if (dy) {
        ev.wheel = dy;
        emit(&ev);
    }
    if (dx) {
        ev.wheel = dx;
        ev.flags = 1;
        emit(&ev);
    }
}

/* ------------------------------------------------------------------ windows */

/*
 * the compositor moves a window when its caption is dragged: the drag
 * rects of the guest are the caption, the rest goes to the guest, which
 * runs its own sizing in the resize border.
 */
static SDL_HitTestResult hit_test(SDL_Window *window, const SDL_Point *point, void *data)
{
    uint32_t id = (uint32_t)(uintptr_t)data;
    UiWindow *w = find_window(id);
    if (!w || (w->config.flags & (MIME_WIN_MAXIMIZED | MIME_WIN_FULLSCREEN)))
        return SDL_HITTEST_NORMAL;
    MimeRect host = host_part(&w->config);
    int32_t x = (int32_t)lround(point->x * g_scale) + host.left;
    int32_t y = (int32_t)lround(point->y * g_scale) + host.top;
    for (uint32_t i = 0; i < w->config.drag_count && i < MIME_WIN_MAX_DRAG_RECTS; i++) {
        MimeRect r = w->config.drag[i];
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
            return SDL_HITTEST_DRAGGABLE;
    }
    return SDL_HITTEST_NORMAL;
}

static void destroy_host_window(UiWindow *w)
{
    if (w->texture)
        SDL_DestroyTexture(w->texture);
    if (w->renderer)
        SDL_DestroyRenderer(w->renderer);
    if (w->window)
        SDL_DestroyWindow(w->window);
    w->texture = NULL;
    w->renderer = NULL;
    w->window = NULL;
    w->texture_width = w->texture_height = 0;
}

/*
 * the host window of a configuration: a popup of its owner for menus and
 * tooltips, which a wayland compositor places relative to the owner, else
 * a top level window.
 */
static bool create_host_window(UiWindow *w, const MimeWinConfig *cfg)
{
    MimeRect host = host_part(cfg);
    int width = MIME_MAX(1, (int)lround((host.right - host.left) / g_scale));
    int height = MIME_MAX(1, (int)lround((host.bottom - host.top) / g_scale));
    UiWindow *owner = cfg->owner_id ? find_window(cfg->owner_id) : NULL;
    bool popup = (cfg->flags & MIME_WIN_TOOL) && owner && owner->window;
    SDL_WindowFlags flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_TRANSPARENT;
    if (popup) {
        MimeRect oh = host_part(&owner->config);
        int x = (int)lround((cfg->rect.left + host.left - owner->config.rect.left - oh.left) / g_scale);
        int y = (int)lround((cfg->rect.top + host.top - owner->config.rect.top - oh.top) / g_scale);
        w->window = SDL_CreatePopupWindow(owner->window, x, y, width, height, flags | SDL_WINDOW_POPUP_MENU);
    } else {
        SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, width);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, height);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, (int)lround((cfg->rect.left + host.left) / g_scale));
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, (int)lround((cfg->rect.top + host.top) / g_scale));
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, flags | SDL_WINDOW_BORDERLESS);
        if (cfg->flags & MIME_WIN_TOOL)
            SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_UTILITY_BOOLEAN, true);
        w->window = SDL_CreateWindowWithProperties(props);
        SDL_DestroyProperties(props);
    }
    if (!w->window) {
        mime_warn("cannot create a host window: %s", SDL_GetError());
        return false;
    }
    w->popup = popup;
    w->owner = cfg->owner_id;
    w->renderer = SDL_CreateRenderer(w->window, NULL);
    if (!w->renderer) {
        mime_warn("cannot create a renderer: %s", SDL_GetError());
        destroy_host_window(w);
        return false;
    }
    SDL_SetRenderVSync(w->renderer, 0);
    SDL_SetWindowHitTest(w->window, hit_test, (void *)(uintptr_t)w->id);
    SDL_StartTextInput(w->window);
    if (owner && owner->window && !popup)
        SDL_SetWindowParent(w->window, owner->window);
    return true;
}

/* draws the latest surface: the visible part at its place in the host window, the resize border clear. */
static void draw(UiWindow *w)
{
    if (!w->renderer)
        return;
    pthread_mutex_lock(&w->lock);
    w->present_queued = false;
    if (!w->pixels) {
        pthread_mutex_unlock(&w->lock);
        return;
    }
    if (!w->texture || w->texture_width != w->width || w->texture_height != w->height) {
        if (w->texture)
            SDL_DestroyTexture(w->texture);
        w->texture = SDL_CreateTexture(w->renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                       (int)w->width, (int)w->height);
        w->texture_width = w->width;
        w->texture_height = w->height;
        w->dirty = (MimeRect){0, 0, (int32_t)w->width, (int32_t)w->height};
        w->has_dirty = true;
        if (w->texture) {
            SDL_SetTextureBlendMode(w->texture, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
            SDL_SetTextureScaleMode(w->texture, SDL_SCALEMODE_NEAREST);
        }
    }
    if (w->texture && w->has_dirty) {
        MimeRect d = w->dirty;
        SDL_Rect r = {d.left, d.top, d.right - d.left, d.bottom - d.top};
        if (r.w > 0 && r.h > 0)
            SDL_UpdateTexture(w->texture, &r, w->pixels + ((size_t)d.top * w->width + (size_t)d.left) * 4,
                              (int)w->width * 4);
        w->has_dirty = false;
    }
    pthread_mutex_unlock(&w->lock);
    if (!w->texture)
        return;
    MimeRect vis = visible_part(&w->config), host = host_part(&w->config);
    SDL_SetRenderDrawColor(w->renderer, 0, 0, 0, 0);
    SDL_RenderClear(w->renderer);
    SDL_FRect src = {(float)vis.left, (float)vis.top, (float)(vis.right - vis.left), (float)(vis.bottom - vis.top)};
    SDL_FRect dst = {(float)((vis.left - host.left) / g_scale), (float)((vis.top - host.top) / g_scale),
                     (float)((vis.right - vis.left) / g_scale), (float)((vis.bottom - vis.top) / g_scale)};
    SDL_RenderTexture(w->renderer, w->texture, &src, &dst);
    if (w->config.border_color) {
        uint32_t c = w->config.border_color;
        SDL_SetRenderDrawColor(w->renderer, (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, (c >> 24) & 0xFF);
        SDL_RenderRect(w->renderer, &dst);
    }
    SDL_RenderPresent(w->renderer);
}

static void present_main(void *arg)
{
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window((uint32_t)(uintptr_t)arg);
    pthread_mutex_unlock(&g_windows_lock);
    if (w && w->shown)
        draw(w);
}

static void request_window_state(UiWindow *w, int command)
{
    if (w->state_pending && w->state_command == command)
        return;
    if (++w->state_request == 0)
        ++w->state_request;
    w->state_pending = true;
    w->state_command = command;
    MimeInputEvent ev = {.type = MIME_INPUT_WINDOW_STATE, .window_id = w->id, .x = command,
                         .state_request = w->state_request};
    emit(&ev);
}

static void apply_config(UiWindow *w, const MimeWinConfig *cfg)
{
    if (w->state_pending) {
        if (cfg->state_request != w->state_request)
            return;
        w->state_pending = false;
    }
    /* a popup sits on its owner from its creation: another owner or kind makes a new window. */
    UiWindow *owner = cfg->owner_id ? find_window(cfg->owner_id) : NULL;
    bool want_popup = (cfg->flags & MIME_WIN_TOOL) && owner && owner->window;
    if (w->window && (want_popup != w->popup || (w->popup && w->owner != cfg->owner_id)))
        destroy_host_window(w);
    bool size_changed = !w->configured || host_part(&w->config).right - host_part(&w->config).left !=
                                                  host_part(cfg).right - host_part(cfg).left ||
                        host_part(&w->config).bottom - host_part(&w->config).top !=
                            host_part(cfg).bottom - host_part(cfg).top;
    w->config = *cfg;
    w->configured = true;
    if (!w->window && !(cfg->flags & MIME_WIN_VISIBLE))
        return;
    if (!w->window && !create_host_window(w, cfg))
        return;

    char title[1024];
    mime_utf16_to_utf8(title, sizeof(title), cfg->title, MIME_MIN(cfg->title_chars, 256u));
    SDL_SetWindowTitle(w->window, title);
    MimeRect host = host_part(cfg);
    if (size_changed)
        SDL_SetWindowSize(w->window, MIME_MAX(1, (int)lround((host.right - host.left) / g_scale)),
                          MIME_MAX(1, (int)lround((host.bottom - host.top) / g_scale)));
    if (w->popup) {
        MimeRect oh = host_part(&owner->config);
        SDL_SetWindowPosition(w->window,
                              (int)lround((cfg->rect.left + host.left - owner->config.rect.left - oh.left) / g_scale),
                              (int)lround((cfg->rect.top + host.top - owner->config.rect.top - oh.top) / g_scale));
    } else if (!w->fullscreen) {
        /* wayland leaves top level placement to the compositor; x11 honors it. */
        SDL_SetWindowPosition(w->window, (int)lround((cfg->rect.left + host.left) / g_scale),
                              (int)lround((cfg->rect.top + host.top) / g_scale));
    }
    SDL_SetWindowAlwaysOnTop(w->window, (cfg->flags & MIME_WIN_TOPMOST) != 0);

    if (cfg->flags & MIME_WIN_MINIMIZED) {
        if (cfg->flags & MIME_WIN_VISIBLE)
            SDL_MinimizeWindow(w->window);
        else
            SDL_HideWindow(w->window);
        w->shown = (cfg->flags & MIME_WIN_VISIBLE) != 0;
        return;
    }
    if (cfg->flags & MIME_WIN_VISIBLE) {
        bool opening = !w->shown;
        if (opening)
            SDL_ShowWindow(w->window);
        if (SDL_GetWindowFlags(w->window) & SDL_WINDOW_MINIMIZED)
            SDL_RestoreWindow(w->window);
        if (cfg->flags & MIME_WIN_ACTIVATE)
            SDL_RaiseWindow(w->window);
        w->shown = true;
    } else if (w->shown) {
        SDL_HideWindow(w->window);
        w->shown = false;
    }
    bool fullscreen = (cfg->flags & (MIME_WIN_FULLSCREEN | MIME_WIN_VISIBLE)) == (MIME_WIN_FULLSCREEN | MIME_WIN_VISIBLE);
    if (fullscreen != w->fullscreen && !w->popup) {
        w->fullscreen = fullscreen;
        SDL_SetWindowFullscreen(w->window, fullscreen);
    }
    if (w->shown)
        draw(w);
}

static void config_main(void *arg)
{
    uint32_t id = (uint32_t)(uintptr_t)arg;
    MimeWinConfig cfg;
    bool have = false;
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window(id);
    if (w) {
        pthread_mutex_lock(&w->lock);
        if (w->config_queued) {
            cfg = w->pending_config;
            w->config_queued = false;
            have = true;
        }
        pthread_mutex_unlock(&w->lock);
    }
    pthread_mutex_unlock(&g_windows_lock);
    if (have)
        apply_config(w, &cfg);
}

void mime_ui_window_config(const MimeWinConfig *cfg)
{
    bool queue = false;
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window(cfg->window_id);
    if (w) {
        pthread_mutex_lock(&w->lock);
        uint32_t activate = w->config_queued ? w->pending_config.flags & MIME_WIN_ACTIVATE : 0;
        w->pending_config = *cfg;
        w->pending_config.flags |= activate;
        queue = !w->config_queued;
        w->config_queued = true;
        pthread_mutex_unlock(&w->lock);
    }
    pthread_mutex_unlock(&g_windows_lock);
    if (queue)
        on_main(config_main, (void *)(uintptr_t)cfg->window_id);
}

void mime_ui_window_create(uint32_t id)
{
    pthread_mutex_lock(&g_windows_lock);
    if (!find_window(id)) {
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (!g_windows[i].used) {
                memset(&g_windows[i], 0, sizeof(g_windows[i]));
                g_windows[i].used = true;
                g_windows[i].id = id;
                pthread_mutex_init(&g_windows[i].lock, NULL);
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_windows_lock);
}

static void destroy_main(void *arg)
{
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window((uint32_t)(uintptr_t)arg);
    if (!w) {
        pthread_mutex_unlock(&g_windows_lock);
        return;
    }
    /* popups go first: they cannot outlive the window they hang from. */
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (g_windows[i].used && g_windows[i].popup && g_windows[i].owner == w->id) {
            destroy_host_window(&g_windows[i]);
            g_windows[i].shown = false;
        }
    destroy_host_window(w);
    pthread_mutex_lock(&w->lock);
    free(w->pixels);
    w->pixels = NULL;
    pthread_mutex_unlock(&w->lock);
    pthread_mutex_destroy(&w->lock);
    w->used = false;
    pthread_mutex_unlock(&g_windows_lock);
}

void mime_ui_window_destroy(uint32_t id)
{
    if (!g_video) {
        pthread_mutex_lock(&g_windows_lock);
        UiWindow *w = find_window(id);
        if (w)
            w->used = false;
        pthread_mutex_unlock(&g_windows_lock);
        return;
    }
    on_main(destroy_main, (void *)(uintptr_t)id);
}

void mime_ui_process_exit(uint32_t process_id)
{
}

typedef struct WindowIcon {
    uint32_t window_id, width, height;
    uint8_t pixels[];
} WindowIcon;

static void icon_main(void *arg)
{
    WindowIcon *icon = arg;
    UiWindow *w = find_window(icon->window_id);
    if (w && w->window && icon->width && icon->height) {
        SDL_Surface *s = SDL_CreateSurfaceFrom((int)icon->width, (int)icon->height, SDL_PIXELFORMAT_ARGB8888,
                                               icon->pixels, (int)icon->width * 4);
        if (s) {
            SDL_SetWindowIcon(w->window, s);
            SDL_DestroySurface(s);
        }
    }
    free(icon);
}

void mime_ui_window_icon(uint32_t id, const void *pixels, uint32_t width, uint32_t height, uint32_t stride)
{
    if (!g_video || width > 1024 || height > 1024 || stride < width * 4 || (!pixels && width && height))
        return;
    WindowIcon *icon = mime_xmalloc(sizeof(*icon) + (size_t)width * height * 4);
    *icon = (WindowIcon){.window_id = id, .width = width, .height = height};
    for (uint32_t y = 0; y < height; y++)
        memcpy(icon->pixels + (size_t)y * width * 4, (const uint8_t *)pixels + (size_t)y * stride, (size_t)width * 4);
    on_main(icon_main, icon);
}

/* copies the dirty part into the window's staging buffer; the main thread uploads it. */
void mime_ui_window_present(uint32_t id, const void *pixels, uint32_t width, uint32_t height, uint32_t stride,
                            MimeRect dirty)
{
    if (!g_video)
        return;
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window(id);
    if (!w) {
        pthread_mutex_unlock(&g_windows_lock);
        return;
    }
    pthread_mutex_lock(&w->lock);
    pthread_mutex_unlock(&g_windows_lock);
    if (!w->pixels || w->width != width || w->height != height) {
        free(w->pixels);
        w->pixels = malloc((size_t)width * height * 4);
        w->width = width;
        w->height = height;
        dirty = (MimeRect){0, 0, (int32_t)width, (int32_t)height};
        w->has_dirty = false;
    }
    if (!w->pixels) {
        pthread_mutex_unlock(&w->lock);
        return;
    }
    int32_t left = MIME_MAX(dirty.left, 0), top = MIME_MAX(dirty.top, 0);
    int32_t right = MIME_MIN(dirty.right, (int32_t)width), bottom = MIME_MIN(dirty.bottom, (int32_t)height);
    for (int32_t y = top; y < bottom && left < right; y++)
        memcpy(w->pixels + ((size_t)y * width + (size_t)left) * 4,
               (const uint8_t *)pixels + (size_t)y * stride + (size_t)left * 4, (size_t)(right - left) * 4);
    if (left < right && top < bottom) {
        if (w->has_dirty)
            w->dirty = (MimeRect){MIME_MIN(w->dirty.left, left), MIME_MIN(w->dirty.top, top),
                                  MIME_MAX(w->dirty.right, right), MIME_MAX(w->dirty.bottom, bottom)};
        else
            w->dirty = (MimeRect){left, top, right, bottom};
        w->has_dirty = true;
    }
    bool queue = !w->present_queued;
    w->present_queued = true;
    pthread_mutex_unlock(&w->lock);
    if (queue)
        on_main(present_main, (void *)(uintptr_t)id);
}

/* ------------------------------------------------------------------ cursor */

static void cursor_main(void *arg)
{
    static const SDL_SystemCursor kinds[] = {
        SDL_SYSTEM_CURSOR_DEFAULT,     SDL_SYSTEM_CURSOR_TEXT,      SDL_SYSTEM_CURSOR_WAIT,
        SDL_SYSTEM_CURSOR_CROSSHAIR,   SDL_SYSTEM_CURSOR_POINTER,   SDL_SYSTEM_CURSOR_NS_RESIZE,
        SDL_SYSTEM_CURSOR_EW_RESIZE,   SDL_SYSTEM_CURSOR_NWSE_RESIZE, SDL_SYSTEM_CURSOR_NESW_RESIZE,
        SDL_SYSTEM_CURSOR_MOVE,        SDL_SYSTEM_CURSOR_NOT_ALLOWED,
    };
    uint32_t c = (uint32_t)(uintptr_t)arg;
    if (c == g_cursor)
        return;
    g_cursor = c;
    if (c >= MIME_CURSOR_HIDDEN) {
        SDL_HideCursor();
        return;
    }
    if (!g_cursors[c])
        g_cursors[c] = SDL_CreateSystemCursor(kinds[c]);
    if (g_cursors[c])
        SDL_SetCursor(g_cursors[c]);
    SDL_ShowCursor();
}

void mime_ui_set_cursor(uint32_t cursor)
{
    on_main(cursor_main, (void *)(uintptr_t)cursor);
}

void mime_ui_beep(uint32_t type)
{
}

/* ------------------------------------------------------------------ clipboard */

/*
 * a change of the host clipboard only tells the guest whether there is
 * text: the text itself is read when a guest program asks for it.
 */
static void clip_changed(void)
{
    bool text = SDL_HasClipboardText();
    pthread_mutex_lock(&g_clip_lock);
    free(g_clip_text);
    g_clip_text = NULL;
    g_clip_chars = 0;
    g_clip_stale = text;
    pthread_mutex_unlock(&g_clip_lock);
    MimeInputEvent ev = {0};
    ev.type = MIME_INPUT_CLIPBOARD;
    ev.x = text;
    emit(&ev);
}

typedef struct ClipRead {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool done;
} ClipRead;

/* the host text with windows line ends, read on the main thread. */
static void clip_read_main(void *arg)
{
    ClipRead *r = arg;
    char *utf8 = SDL_GetClipboardText();
    size_t len = utf8 ? strlen(utf8) : 0;
    uint16_t *raw = len ? malloc((len + 1) * sizeof(uint16_t)) : NULL;
    uint16_t *text = len ? malloc((len + 1) * 2 * sizeof(uint16_t)) : NULL;
    size_t chars = 0;
    if (raw && text) {
        size_t n = mime_utf8_to_utf16(raw, len + 1, utf8);
        for (size_t i = 0; i < n; i++) {
            if (raw[i] == '\n' && (i == 0 || raw[i - 1] != '\r'))
                text[chars++] = '\r';
            text[chars++] = raw[i];
        }
    } else {
        free(text);
        text = NULL;
    }
    free(raw);
    SDL_free(utf8);
    pthread_mutex_lock(&g_clip_lock);
    free(g_clip_text);
    g_clip_text = text;
    g_clip_chars = chars;
    g_clip_stale = false;
    pthread_mutex_unlock(&g_clip_lock);
    pthread_mutex_lock(&r->lock);
    r->done = true;
    pthread_cond_signal(&r->cond);
    pthread_mutex_unlock(&r->lock);
}

static void clip_set_main(void *arg)
{
    char *utf8 = arg;
    SDL_SetClipboardText(utf8);
    free(utf8);
    /* the guest already holds this text: the update event it raises is not offered back. */
    pthread_mutex_lock(&g_clip_lock);
    g_clip_stale = false;
    pthread_mutex_unlock(&g_clip_lock);
}

void mime_ui_clipboard_set(const uint16_t *text, size_t chars)
{
    uint16_t *copy = malloc((chars + 1) * sizeof(uint16_t));
    uint16_t *keep = malloc((chars ? chars : 1) * sizeof(uint16_t));
    char *utf8 = malloc(chars * 3 + 1);
    if (!copy || !keep || !utf8) {
        free(copy);
        free(keep);
        free(utf8);
        return;
    }
    size_t n = 0;
    for (size_t i = 0; i < chars; i++)
        if (!(text[i] == '\r' && i + 1 < chars && text[i + 1] == '\n'))
            copy[n++] = text[i];
    mime_utf16_to_utf8(utf8, chars * 3 + 1, copy, n);
    free(copy);
    memcpy(keep, text, chars * sizeof(uint16_t));
    pthread_mutex_lock(&g_clip_lock);
    free(g_clip_text);
    g_clip_text = keep;
    g_clip_chars = chars;
    g_clip_stale = false;
    pthread_mutex_unlock(&g_clip_lock);
    if (g_video)
        on_main(clip_set_main, utf8);
    else
        free(utf8);
}

size_t mime_ui_clipboard_read(uint16_t *dst, size_t offset, size_t count)
{
    pthread_mutex_lock(&g_clip_lock);
    bool stale = g_clip_stale && g_video;
    pthread_mutex_unlock(&g_clip_lock);
    /* the main thread never waits for a vcpu, so this cannot deadlock. */
    if (stale) {
        ClipRead r = {PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, false};
        on_main(clip_read_main, &r);
        pthread_mutex_lock(&r.lock);
        while (!r.done)
            pthread_cond_wait(&r.cond, &r.lock);
        pthread_mutex_unlock(&r.lock);
    }
    pthread_mutex_lock(&g_clip_lock);
    size_t total = g_clip_chars;
    if (dst && offset < total && count)
        memcpy(dst, g_clip_text + offset, MIME_MIN(count, total - offset) * sizeof(uint16_t));
    pthread_mutex_unlock(&g_clip_lock);
    return total;
}

/* ------------------------------------------------------------------ main loop */

void mime_ui_init(MimeUiInputSink sink, void *ctx)
{
    g_sink = sink;
    g_sink_ctx = ctx;
    g_main_thread = pthread_self();
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1");
    SDL_SetHint(SDL_HINT_APP_ID, "mime");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        mime_info("no display (%s): windows are not shown", SDL_GetError());
        return;
    }
    g_call_event = SDL_RegisterEvents(1);
    g_video = g_call_event != 0;
    if (g_video)
        update_display();
}

static void handle_event(const SDL_Event *e)
{
    if (e->type == g_call_event) {
        ((void (*)(void *))e->user.data1)(e->user.data2);
        return;
    }
    switch (e->type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        emit_key(&e->key);
        break;
    case SDL_EVENT_TEXT_INPUT:
        emit_text(&e->text);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        emit_mouse(e->motion.windowID, e->motion.x, e->motion.y, MIME_INPUT_MOUSE_MOVE, 0);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        uint32_t button = mouse_button(e->button.button);
        if (button)
            emit_mouse(e->button.windowID, e->button.x, e->button.y,
                       e->button.down ? MIME_INPUT_MOUSE_DOWN : MIME_INPUT_MOUSE_UP, button);
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        emit_wheel(&e->wheel);
        break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE: {
        UiWindow *w = find_by_sdl(e->window.windowID);
        if (w) {
            MimeInputEvent ev = {.type = MIME_INPUT_MOUSE_LEAVE, .window_id = w->id};
            emit(&ev);
        }
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST: {
        UiWindow *w = find_by_sdl(e->window.windowID);
        if (w) {
            MimeInputEvent ev = {.type = MIME_INPUT_WINDOW_FOCUS, .window_id = w->id,
                                 .x = e->type == SDL_EVENT_WINDOW_FOCUS_GAINED};
            emit(&ev);
        }
        break;
    }
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
        UiWindow *w = find_by_sdl(e->window.windowID);
        if (w) {
            MimeInputEvent ev = {.type = MIME_INPUT_WINDOW_CLOSE, .window_id = w->id};
            emit(&ev);
        }
        break;
    }
    case SDL_EVENT_WINDOW_MOVED: {
        /* x11 reports where a window went; wayland keeps top level positions to itself. */
        UiWindow *w = find_by_sdl(e->window.windowID);
        if (w && !w->popup && !w->fullscreen && w->configured) {
            MimeRect host = host_part(&w->config);
            int32_t x = (int32_t)lround(e->window.data1 * g_scale) - host.left;
            int32_t y = (int32_t)lround(e->window.data2 * g_scale) - host.top;
            if (x != w->config.rect.left || y != w->config.rect.top) {
                w->config.rect.right += x - w->config.rect.left;
                w->config.rect.bottom += y - w->config.rect.top;
                w->config.rect.left = x;
                w->config.rect.top = y;
                MimeInputEvent ev = {.type = MIME_INPUT_WINDOW_MOVED, .window_id = w->id, .x = x, .y = y};
                emit(&ev);
            }
        }
        break;
    }
    case SDL_EVENT_WINDOW_MINIMIZED: {
        UiWindow *w = find_by_sdl(e->window.windowID);
        if (w && !(w->config.flags & MIME_WIN_MINIMIZED))
            request_window_state(w, 6);
        break;
    }
    case SDL_EVENT_WINDOW_RESTORED: {
        UiWindow *w = find_by_sdl(e->window.windowID);
        if (w && (w->config.flags & MIME_WIN_MINIMIZED))
            request_window_state(w, 9);
        break;
    }
    case SDL_EVENT_WINDOW_EXPOSED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
        UiWindow *w = find_by_sdl(e->window.windowID);
        if (w && w->shown)
            draw(w);
        break;
    }
    case SDL_EVENT_CLIPBOARD_UPDATE:
        if (!e->clipboard.owner)
            clip_changed();
        break;
    case SDL_EVENT_DISPLAY_ORIENTATION:
    case SDL_EVENT_DISPLAY_ADDED:
    case SDL_EVENT_DISPLAY_REMOVED:
    case SDL_EVENT_DISPLAY_MOVED:
    case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED:
    case SDL_EVENT_DISPLAY_CONTENT_SCALE_CHANGED: {
        MimeDisplayInfo before = g_display;
        update_display();
        if (memcmp(&before, &g_display, sizeof(before))) {
            MimeInputEvent ev = {.type = MIME_INPUT_DISPLAY, .x = (int32_t)g_display.width,
                                 .y = (int32_t)g_display.height};
            emit(&ev);
        }
        break;
    }
    default:
        break;
    }
}

void mime_ui_run(void)
{
    if (!g_video) {
        pthread_mutex_lock(&g_stop_lock);
        while (!g_stop)
            pthread_cond_wait(&g_stop_cond, &g_stop_lock);
        pthread_mutex_unlock(&g_stop_lock);
        return;
    }
    SDL_Event e;
    while (!g_stop && SDL_WaitEvent(&e))
        handle_event(&e);
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (g_windows[i].used)
            destroy_host_window(&g_windows[i]);
    SDL_Quit();
}

static void stop_main(void *arg)
{
    g_stop = true;
}

void mime_ui_stop(void)
{
    pthread_mutex_lock(&g_stop_lock);
    g_stop = true;
    pthread_cond_broadcast(&g_stop_cond);
    pthread_mutex_unlock(&g_stop_lock);
    if (g_video) {
        /* wakes the main loop, which sees g_stop. */
        SDL_Event ev;
        SDL_zero(ev);
        ev.type = g_call_event;
        ev.user.data1 = (void *)stop_main;
        SDL_PushEvent(&ev);
    }
}

/* macos runs per program app bundles through this entry; linux desktops need none. */
int mime_app_proxy_main(int argc, char **argv)
{
    return -1;
}
