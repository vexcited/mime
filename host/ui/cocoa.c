/*
 * cocoa backend, written against the objective-c runtime so the whole
 * code base stays plain c. every guest top level window becomes a
 * borderless, transparent NSWindow whose layer shows the guest pixels.
 *
 * objc_msgSend must always be cast to the exact prototype of the method:
 * on arm64 variadic arguments travel on the stack, unlike real arguments.
 */
#include "ui.h"
#include "app_identity.h"

#include <CoreGraphics/CoreGraphics.h>
#include <IOSurface/IOSurface.h>
#include <dispatch/dispatch.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef CGRect NSRect;
typedef CGPoint NSPoint;
typedef CGSize NSSize;
typedef unsigned long NSUInteger;
typedef long NSInteger;

#define SEL_(name) sel_registerName(name)
#define CLS(name) ((id)objc_getClass(name))

#define MSG_ID(obj, sel) ((id (*)(id, SEL))objc_msgSend)((id)(obj), SEL_(sel))
#define MSG_ID_ID(obj, sel, a) ((id (*)(id, SEL, id))objc_msgSend)((id)(obj), SEL_(sel), (id)(a))
#define MSG_V(obj, sel) ((void (*)(id, SEL))objc_msgSend)((id)(obj), SEL_(sel))
#define MSG_V_ID(obj, sel, a) ((void (*)(id, SEL, id))objc_msgSend)((id)(obj), SEL_(sel), (id)(a))
#define MSG_V_BOOL(obj, sel, a) ((void (*)(id, SEL, BOOL))objc_msgSend)((id)(obj), SEL_(sel), (BOOL)(a))
#define MSG_V_INT(obj, sel, a) ((void (*)(id, SEL, NSInteger))objc_msgSend)((id)(obj), SEL_(sel), (NSInteger)(a))
#define MSG_V_DBL(obj, sel, a) ((void (*)(id, SEL, double))objc_msgSend)((id)(obj), SEL_(sel), (double)(a))
#define MSG_V_PTR(obj, sel, a) ((void (*)(id, SEL, const void *))objc_msgSend)((id)(obj), SEL_(sel), (a))
#define MSG_BOOL(obj, sel) ((BOOL (*)(id, SEL))objc_msgSend)((id)(obj), SEL_(sel))
#define MSG_DBL(obj, sel) ((double (*)(id, SEL))objc_msgSend)((id)(obj), SEL_(sel))
#define MSG_UINT(obj, sel) ((NSUInteger (*)(id, SEL))objc_msgSend)((id)(obj), SEL_(sel))
#define MSG_RECT(obj, sel) ((NSRect (*)(id, SEL))objc_msgSend)((id)(obj), SEL_(sel))
#define MSG_POINT(obj, sel) ((NSPoint (*)(id, SEL))objc_msgSend)((id)(obj), SEL_(sel))

enum {
    NSEventModifierFlagCapsLock = 1 << 16,
    NSEventModifierFlagShift = 1 << 17,
    NSEventModifierFlagControl = 1 << 18,
    NSEventModifierFlagOption = 1 << 19,
    NSEventModifierFlagCommand = 1 << 20,
};

enum {
    NSEventTypeKeyDown = 10,
    NSEventTypeKeyUp = 11,
};

enum {
    NSTrackingMouseEnteredAndExited = 0x01,
    NSTrackingMouseMoved = 0x02,
    NSTrackingCursorUpdate = 0x04,
    NSTrackingActiveInActiveApp = 0x40,
    NSTrackingActiveAlways = 0x80,
    NSTrackingInVisibleRect = 0x200,
};

extern id kCAGravityTopLeft;
extern id kCAFilterNearest;
extern id NSWindowDidMoveNotification;
extern id NSApplicationDidBecomeActiveNotification;
extern id NSApplicationDidResignActiveNotification;
extern void NSBeep(void);

#define MAX_WINDOWS 256

typedef struct UiWindow {
    uint32_t id;
    bool used;
    id window;
    id view;
    id content; /* the rounded layer showing the visible part, inside any resize border */
    MimeWinConfig config;
    bool configured;
    bool shown;
    bool suppress_state;
    bool state_pending;
    uint32_t state_request;
    int state_command;
    bool fullscreen_transition;
    bool fullscreen;
    bool fullscreen_synced;
    bool fullscreen_config_pending;
    MimeWinConfig fullscreen_config;
    MimeWinConfig pending_config;
    bool config_queued;

    pthread_mutex_t lock;
    IOSurfaceRef surface; /* the guest pixels, read by the compositor in place */
    uint32_t width, height;
    bool dirty;
    bool update_queued;
    bool shadow_dirty;
} UiWindow;

static UiWindow g_windows[MAX_WINDOWS];
static pthread_mutex_t g_windows_lock = PTHREAD_MUTEX_INITIALIZER;
static MimeUiInputSink g_sink;
static void *g_sink_ctx;
static id g_app;
static Class g_window_class, g_view_class, g_delegate_class;
static void (*g_toggle_fullscreen)(id, SEL, id);
static id g_delegate;
static double g_scale = 2.0;
static double g_screen_height_pts;
static CGColorSpaceRef g_colorspace;
static bool g_suppress_move;
static uint32_t g_cursor;
static bool g_cursor_hidden;
static bool g_pointer_in_guest;
static bool g_fullscreen_presentation;
static void cursor_main(void *arg);

static void restore_host_cursor(void)
{
    if (g_cursor_hidden) {
        MSG_V(CLS("NSCursor"), "unhide");
        g_cursor_hidden = false;
    }
}

static void view_cursor_update(id self, SEL sel, id event)
{
    g_pointer_in_guest = true;
    cursor_main((void *)(uintptr_t)g_cursor);
}

static UiWindow *find_window(uint32_t id)
{
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (g_windows[i].used && g_windows[i].id == id)
            return &g_windows[i];
    return NULL;
}

static UiWindow *find_by_nswindow(id window)
{
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (g_windows[i].used && g_windows[i].window == window)
            return &g_windows[i];
    return NULL;
}

static void update_presentation(void)
{
    UiWindow *w = find_by_nswindow(MSG_ID(g_app, "keyWindow"));
    if (w && (w->fullscreen || w->fullscreen_transition))
        return;
    bool fullscreen = w && (w->config.flags & MIME_WIN_FULLSCREEN) &&
                      (w->config.flags & MIME_WIN_VISIBLE) && !(w->config.flags & MIME_WIN_MINIMIZED);
    if (fullscreen != g_fullscreen_presentation) {
        MSG_V_INT(g_app, "setPresentationOptions:", fullscreen ? (1u << 0) | (1u << 2) : 0);
        g_fullscreen_presentation = fullscreen;
    }
}

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* converts a guest screen rectangle (pixels, y down) to a cocoa frame (points, y up). */
static NSRect guest_to_frame(MimeRect r)
{
    double x = r.left / g_scale;
    double w = (r.right - r.left) / g_scale;
    double h = (r.bottom - r.top) / g_scale;
    double y = g_screen_height_pts - r.bottom / g_scale;
    return CGRectMake(x, y, w, h);
}

static void screen_point_to_guest(NSPoint p, int32_t *x, int32_t *y)
{
    *x = (int32_t)(p.x * g_scale);
    *y = (int32_t)((g_screen_height_pts - p.y) * g_scale);
}

static uint32_t modifiers_of(id event)
{
    NSUInteger flags = MSG_UINT(event, "modifierFlags");
    uint32_t m = 0;
    if (flags & NSEventModifierFlagShift)
        m |= MIME_MOD_SHIFT;
    if (flags & (NSEventModifierFlagControl | NSEventModifierFlagCommand))
        m |= MIME_MOD_CONTROL;
    if (flags & NSEventModifierFlagOption)
        m |= MIME_MOD_ALT;
    if (flags & NSEventModifierFlagCapsLock)
        m |= MIME_MOD_CAPS;
    return m;
}

static void emit(MimeInputEvent *ev)
{
    ev->time_ms = now_ms();
    if (g_sink)
        g_sink(g_sink_ctx, ev);
}

static void emit_mouse(id self, id event, uint32_t type, uint32_t button)
{
    g_pointer_in_guest = true;
    id window = MSG_ID(self, "window");
    UiWindow *w = find_by_nswindow(window);
    if (!w)
        return;
    NSPoint loc = MSG_POINT(event, "locationInWindow");
    NSRect frame = MSG_RECT(window, "frame");
    NSPoint screen = CGPointMake(frame.origin.x + loc.x, frame.origin.y + loc.y);
    MimeInputEvent ev = {0};
    ev.type = type;
    ev.window_id = w->id;
    screen_point_to_guest(screen, &ev.x, &ev.y);
    ev.button = button;
    ev.modifiers = modifiers_of(event);
    emit(&ev);
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

/* the host window in screen pixels. */
static MimeRect host_screen_rect(const MimeWinConfig *c)
{
    MimeRect h = host_part(c);
    return (MimeRect){c->rect.left + h.left, c->rect.top + h.top, c->rect.left + h.right, c->rect.top + h.bottom};
}

/* true when a window relative guest point lies in a caption drag area. */
static bool in_drag_area(UiWindow *w, id event)
{
    NSPoint loc = MSG_POINT(event, "locationInWindow");
    NSRect frame = MSG_RECT(w->window, "frame");
    MimeRect host = host_part(&w->config);
    int32_t x = (int32_t)(loc.x * g_scale) + host.left;
    int32_t y = (int32_t)((frame.size.height - loc.y) * g_scale) + host.top;
    for (uint32_t i = 0; i < w->config.drag_count && i < MIME_WIN_MAX_DRAG_RECTS; i++) {
        MimeRect r = w->config.drag[i];
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
            return true;
    }
    return false;
}

static void view_mouse_down(id self, SEL sel, id event)
{
    UiWindow *w = find_by_nswindow(MSG_ID(self, "window"));
    if (w && !(w->config.flags & (MIME_WIN_MAXIMIZED | MIME_WIN_FULLSCREEN)) &&
        in_drag_area(w, event) && ((NSInteger(*)(id, SEL))objc_msgSend)(event, SEL_("clickCount")) == 1) {
        /* tell the guest about the click so it activates, then let macos move the window. */
        emit_mouse(self, event, MIME_INPUT_MOUSE_DOWN, MIME_MOUSE_LEFT);
        emit_mouse(self, event, MIME_INPUT_MOUSE_UP, MIME_MOUSE_LEFT);
        MSG_V_ID(w->window, "performWindowDragWithEvent:", event);
        return;
    }
    emit_mouse(self, event, MIME_INPUT_MOUSE_DOWN, MIME_MOUSE_LEFT);
}

static void view_mouse_up(id self, SEL sel, id event)
{
    emit_mouse(self, event, MIME_INPUT_MOUSE_UP, MIME_MOUSE_LEFT);
}

static void view_right_down(id self, SEL sel, id event)
{
    emit_mouse(self, event, MIME_INPUT_MOUSE_DOWN, MIME_MOUSE_RIGHT);
}

static void view_right_up(id self, SEL sel, id event)
{
    emit_mouse(self, event, MIME_INPUT_MOUSE_UP, MIME_MOUSE_RIGHT);
}

static void view_other_down(id self, SEL sel, id event)
{
    emit_mouse(self, event, MIME_INPUT_MOUSE_DOWN, MIME_MOUSE_MIDDLE);
}

static void view_other_up(id self, SEL sel, id event)
{
    emit_mouse(self, event, MIME_INPUT_MOUSE_UP, MIME_MOUSE_MIDDLE);
}

static void view_mouse_dragged(id self, SEL sel, id event)
{
    emit_mouse(self, event, MIME_INPUT_MOUSE_MOVE, 0);
}

/*
 * macos hands mouseMoved: to the key window wherever the pointer is, so a
 * move goes to the guest window under the pointer instead, like windows
 * does: an open popup menu is never key but must see the hover.
 */
static void view_mouse_moved(id self, SEL sel, id event)
{
    /* an event without a window carries its location in screen coordinates. */
    id window = MSG_ID(event, "window");
    NSPoint loc = MSG_POINT(event, "locationInWindow");
    NSPoint screen = loc;
    if (window) {
        NSRect frame = MSG_RECT(window, "frame");
        screen = CGPointMake(frame.origin.x + loc.x, frame.origin.y + loc.y);
    }
    NSInteger number = ((NSInteger(*)(id, SEL, NSPoint, NSInteger))objc_msgSend)(
        (id)CLS("NSWindow"), SEL_("windowNumberAtPoint:belowWindowWithWindowNumber:"), screen, 0);
    UiWindow *under = NULL;
    for (int i = 0; i < MAX_WINDOWS && !under; i++)
        if (g_windows[i].used && g_windows[i].window &&
            ((NSInteger(*)(id, SEL))objc_msgSend)(g_windows[i].window, SEL_("windowNumber")) == number)
            under = &g_windows[i];
    if (!under)
        return;
    g_pointer_in_guest = true;
    MimeInputEvent ev = {0};
    ev.type = MIME_INPUT_MOUSE_MOVE;
    ev.window_id = under->id;
    screen_point_to_guest(screen, &ev.x, &ev.y);
    ev.modifiers = modifiers_of(event);
    emit(&ev);
}

static void view_mouse_exited(id self, SEL sel, id event)
{
    g_pointer_in_guest = false;
    restore_host_cursor();
    UiWindow *w = find_by_nswindow(MSG_ID(self, "window"));
    if (!w)
        return;
    MimeInputEvent ev = {0};
    ev.type = MIME_INPUT_MOUSE_LEAVE;
    ev.window_id = w->id;
    emit(&ev);
}

static void view_scroll(id self, SEL sel, id event)
{
    id window = MSG_ID(self, "window");
    UiWindow *w = find_by_nswindow(window);
    if (!w)
        return;
    double dy = MSG_DBL(event, "scrollingDeltaY");
    double dx = MSG_DBL(event, "scrollingDeltaX");
    BOOL precise = MSG_BOOL(event, "hasPreciseScrollingDeltas");
    double factor = precise ? 120.0 / 40.0 : 120.0;
    NSPoint loc = MSG_POINT(event, "locationInWindow");
    NSRect frame = MSG_RECT(window, "frame");
    MimeInputEvent ev = {0};
    ev.type = MIME_INPUT_MOUSE_WHEEL;
    ev.window_id = w->id;
    screen_point_to_guest(CGPointMake(frame.origin.x + loc.x, frame.origin.y + loc.y), &ev.x, &ev.y);
    ev.modifiers = modifiers_of(event);
    if (dy != 0) {
        ev.wheel = (int32_t)(dy * factor);
        emit(&ev);
    }
    if (dx != 0) {
        ev.wheel = (int32_t)(-dx * factor);
        ev.flags = 1;
        emit(&ev);
    }
}

typedef struct KeyMap {
    uint16_t mac;
    uint8_t vk;
    uint16_t scan;
} KeyMap;

static const KeyMap g_keymap[] = {
    {0x00, 'A', 0x1E}, {0x01, 'S', 0x1F}, {0x02, 'D', 0x20}, {0x03, 'F', 0x21}, {0x04, 'H', 0x23},
    {0x05, 'G', 0x22}, {0x06, 'Z', 0x2C}, {0x07, 'X', 0x2D}, {0x08, 'C', 0x2E}, {0x09, 'V', 0x2F},
    {0x0B, 'B', 0x30}, {0x0C, 'Q', 0x10}, {0x0D, 'W', 0x11}, {0x0E, 'E', 0x12}, {0x0F, 'R', 0x13},
    {0x10, 'Y', 0x15}, {0x11, 'T', 0x14}, {0x12, '1', 0x02}, {0x13, '2', 0x03}, {0x14, '3', 0x04},
    {0x15, '4', 0x05}, {0x16, '6', 0x07}, {0x17, '5', 0x06}, {0x18, 0xBB, 0x0D}, {0x19, '9', 0x0A},
    {0x1A, '7', 0x08}, {0x1B, 0xBD, 0x0C}, {0x1C, '8', 0x09}, {0x1D, '0', 0x0B}, {0x1E, 0xDD, 0x1B},
    {0x1F, 'O', 0x18}, {0x20, 'U', 0x16}, {0x21, 0xDB, 0x1A}, {0x22, 'I', 0x17}, {0x23, 'P', 0x19},
    {0x24, 0x0D, 0x1C}, {0x25, 'L', 0x26}, {0x26, 'J', 0x24}, {0x27, 0xDE, 0x28}, {0x28, 'K', 0x25},
    {0x29, 0xBA, 0x27}, {0x2A, 0xDC, 0x2B}, {0x2B, 0xBC, 0x33}, {0x2C, 0xBF, 0x35}, {0x2D, 'N', 0x31},
    {0x2E, 'M', 0x32}, {0x2F, 0xBE, 0x34}, {0x30, 0x09, 0x0F}, {0x31, 0x20, 0x39}, {0x32, 0xC0, 0x29},
    {0x33, 0x08, 0x0E}, {0x35, 0x1B, 0x01}, {0x37, 0xA2, 0x1D}, {0x38, 0xA0, 0x2A}, {0x39, 0x14, 0x3A},
    {0x3A, 0xA4, 0x38}, {0x3B, 0xA2, 0x1D}, {0x36, 0xA3, 0x11D}, {0x3C, 0xA1, 0x36}, {0x3D, 0xA5, 0x138},
    {0x3E, 0xA3, 0x11D}, {0x41, 0x6E, 0x53}, {0x43, 0x6A, 0x37}, {0x45, 0x6B, 0x4E}, {0x4B, 0x6F, 0x135},
    {0x4C, 0x0D, 0x11C}, {0x4E, 0x6D, 0x4A}, {0x52, 0x60, 0x52}, {0x53, 0x61, 0x4F}, {0x54, 0x62, 0x50},
    {0x55, 0x63, 0x51}, {0x56, 0x64, 0x4B}, {0x57, 0x65, 0x4C}, {0x58, 0x66, 0x4D}, {0x59, 0x67, 0x47},
    {0x5B, 0x68, 0x48}, {0x5C, 0x69, 0x49}, {0x60, 0x74, 0x3F}, {0x61, 0x75, 0x40}, {0x62, 0x76, 0x41},
    {0x63, 0x72, 0x3D}, {0x64, 0x77, 0x42}, {0x65, 0x78, 0x43}, {0x67, 0x7A, 0x57}, {0x6D, 0x79, 0x44},
    {0x6F, 0x7B, 0x58}, {0x72, 0x2D, 0x152}, {0x73, 0x24, 0x147}, {0x74, 0x21, 0x149}, {0x75, 0x2E, 0x153},
    {0x76, 0x73, 0x3E}, {0x77, 0x23, 0x14F}, {0x78, 0x71, 0x3C}, {0x79, 0x22, 0x151}, {0x7A, 0x70, 0x3B},
    {0x7B, 0x25, 0x14B}, {0x7C, 0x27, 0x14D}, {0x7D, 0x28, 0x150}, {0x7E, 0x26, 0x148},
};

/* the text input sources api, declared here rather than through all of carbon. */
typedef struct __TISInputSource *TISInputSourceRef;
extern TISInputSourceRef TISCopyCurrentKeyboardLayoutInputSource(void);
extern void *TISGetInputSourceProperty(TISInputSourceRef source, CFStringRef key);
extern const CFStringRef kTISPropertyUnicodeKeyLayoutData;
extern OSStatus UCKeyTranslate(const void *layout, UInt16 code, UInt16 action, UInt32 modifiers, UInt32 keyboard,
                               OptionBits options, UInt32 *dead, UniCharCount max, UniCharCount *length, UniChar *chars);
extern UInt8 LMGetKbdType(void);

/*
 * the letter a key types in the current macos layout, 0 for other keys.
 * windows gives letter keys the virtual key of their letter in the active
 * layout, so ctrl+z is the key labelled z on an azerty or qwertz keyboard.
 */
static uint32_t layout_letter(uint16_t mac)
{
    TISInputSourceRef source = TISCopyCurrentKeyboardLayoutInputSource();
    if (!source)
        return 0;
    CFDataRef data = TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData);
    uint32_t letter = 0;
    if (data) {
        UInt32 dead = 0;
        UniChar ch[4];
        UniCharCount n = 0;
        if (UCKeyTranslate(CFDataGetBytePtr(data), mac, 0 /* kUCKeyActionDown */, 0, LMGetKbdType(),
                           1 /* kUCKeyTranslateNoDeadKeysMask */, &dead, 4, &n, ch) == 0 &&
            n == 1) {
            if (ch[0] >= 'a' && ch[0] <= 'z')
                letter = ch[0] - 32;
            else if (ch[0] >= 'A' && ch[0] <= 'Z')
                letter = ch[0];
        }
    }
    CFRelease(source);
    return letter;
}

static const KeyMap *map_key(uint16_t mac)
{
    for (size_t i = 0; i < MIME_ARRAY_COUNT(g_keymap); i++)
        if (g_keymap[i].mac == mac)
            return &g_keymap[i];
    return NULL;
}

static void emit_key(id self, id event, uint32_t type)
{
    UiWindow *w = find_by_nswindow(MSG_ID(self, "window"));
    if (!w)
        return;
    uint16_t code = ((uint16_t(*)(id, SEL))objc_msgSend)(event, SEL_("keyCode"));
    const KeyMap *k = map_key(code);
    if (!k)
        return;
    MimeInputEvent ev = {0};
    ev.type = type;
    ev.window_id = w->id;
    ev.vk = k->vk;
    /* the character keys of the main block, which layouts move letters onto. */
    bool movable = (k->vk >= 'A' && k->vk <= 'Z') || (k->vk >= 0xBA && k->vk <= 0xC0) || (k->vk >= 0xDB && k->vk <= 0xDE);
    uint32_t letter = movable ? layout_letter(code) : 0;
    if (letter)
        ev.vk = letter;
    ev.scancode = k->scan;
    ev.modifiers = modifiers_of(event);
    /* isARepeat raises on the flagsChanged events of modifier keys, which never repeat. */
    if (type == MIME_INPUT_KEY_DOWN && MSG_UINT(event, "type") == NSEventTypeKeyDown && MSG_BOOL(event, "isARepeat"))
        ev.flags = 2;
    emit(&ev);
}

static void view_key_down(id self, SEL sel, id event)
{
    emit_key(self, event, MIME_INPUT_KEY_DOWN);
    UiWindow *w = find_by_nswindow(MSG_ID(self, "window"));
    NSUInteger flags = MSG_UINT(event, "modifierFlags");
    if (!w || (flags & (NSEventModifierFlagControl | NSEventModifierFlagCommand)))
        return;
    id chars = MSG_ID(event, "characters");
    const char *utf8 = ((const char *(*)(id, SEL))objc_msgSend)(chars, SEL_("UTF8String"));
    if (!utf8)
        return;
    uint16_t buf[32];
    size_t n = mime_utf8_to_utf16(buf, MIME_ARRAY_COUNT(buf), utf8);
    for (size_t i = 0; i < n; i++) {
        uint32_t c = buf[i];
        /* function keys arrive as private use characters; they only produce key events. */
        if (c >= 0xF700 && c <= 0xF8FF)
            continue;
        if (c == 0x7F)
            c = 0x08;
        if (c == '\r' || c == 0x03)
            c = '\r';
        MimeInputEvent ev = {0};
        ev.type = MIME_INPUT_CHAR;
        ev.window_id = w->id;
        ev.ch = c;
        emit(&ev);
    }
}

static void view_key_up(id self, SEL sel, id event)
{
    emit_key(self, event, MIME_INPUT_KEY_UP);
}

static void view_flags_changed(id self, SEL sel, id event)
{
    uint16_t code = ((uint16_t(*)(id, SEL))objc_msgSend)(event, SEL_("keyCode"));
    NSUInteger flags = MSG_UINT(event, "modifierFlags");
    NSUInteger mask = 0;
    switch (code) {
    case 0x38:
    case 0x3C: mask = NSEventModifierFlagShift; break;
    case 0x3B:
    case 0x3E: mask = NSEventModifierFlagControl; break;
    case 0x3A:
    case 0x3D: mask = NSEventModifierFlagOption; break;
    case 0x37:
    case 0x36: mask = NSEventModifierFlagCommand; break;
    case 0x39: mask = NSEventModifierFlagCapsLock; break;
    default: return;
    }
    emit_key(self, event, (flags & mask) ? MIME_INPUT_KEY_DOWN : MIME_INPUT_KEY_UP);
}

static BOOL ret_yes(id self, SEL sel)
{
    return YES;
}

static BOOL ret_yes_arg(id self, SEL sel, id arg)
{
    return YES;
}

static void delegate_did_move(id self, SEL sel, id notification)
{
    if (g_suppress_move)
        return;
    id window = MSG_ID(notification, "object");
    UiWindow *w = find_by_nswindow(window);
    if (!w || w->fullscreen_transition || w->fullscreen)
        return;
    NSRect frame = MSG_RECT(window, "frame");
    MimeRect host = host_part(&w->config);
    MimeInputEvent ev = {0};
    ev.type = MIME_INPUT_WINDOW_MOVED;
    ev.window_id = w->id;
    ev.x = (int32_t)(frame.origin.x * g_scale + 0.5) - host.left;
    ev.y = (int32_t)((g_screen_height_pts - frame.origin.y - frame.size.height) * g_scale + 0.5) - host.top;
    w->config.rect.right += ev.x - w->config.rect.left;
    w->config.rect.bottom += ev.y - w->config.rect.top;
    w->config.rect.left = ev.x;
    w->config.rect.top = ev.y;
    emit(&ev);
}

static void delegate_focus(id self, SEL sel, id notification, bool gained)
{
    UiWindow *w = find_by_nswindow(MSG_ID(notification, "object"));
    if (!w || (gained && (w->config.flags & MIME_WIN_MINIMIZED)))
        return;
    MimeInputEvent ev = {0};
    ev.type = MIME_INPUT_WINDOW_FOCUS;
    ev.window_id = w->id;
    ev.x = gained ? 1 : 0;
    emit(&ev);
}

static void delegate_became_key(id self, SEL sel, id notification)
{
    delegate_focus(self, sel, notification, true);
    update_presentation();
}

static void delegate_resigned_key(id self, SEL sel, id notification)
{
    delegate_focus(self, sel, notification, false);
    update_presentation();
}

static BOOL delegate_should_close(id self, SEL sel, id window)
{
    UiWindow *w = find_by_nswindow(window);
    if (w) {
        MimeInputEvent ev = {0};
        ev.type = MIME_INPUT_WINDOW_CLOSE;
        ev.window_id = w->id;
        emit(&ev);
    }
    return NO;
}

static void request_window_state(UiWindow *w, int command)
{
    if (!w || w->suppress_state || (w->state_pending && w->state_command == command))
        return;
    if (++w->state_request == 0)
        ++w->state_request;
    w->state_pending = true;
    w->state_command = command;
    MimeInputEvent ev = {.type = MIME_INPUT_WINDOW_STATE, .window_id = w->id, .x = command,
                        .state_request = w->state_request};
    emit(&ev);
}

static void window_zoom(id self, SEL sel, id sender)
{
    UiWindow *w = find_by_nswindow(self);
    if (w && (w->config.flags & MIME_WIN_MAXIMIZABLE) && !w->fullscreen)
        request_window_state(w, (w->config.flags & MIME_WIN_MAXIMIZED) ? 9 : 3);
}

static void window_minimize(id self, SEL sel, id sender)
{
    UiWindow *w = find_by_nswindow(self);
    if (w && (w->config.flags & MIME_WIN_MINIMIZABLE))
        request_window_state(w, 6);
}

static void window_toggle_fullscreen(id self, SEL sel, id sender)
{
    UiWindow *w = find_by_nswindow(self);
    if (!w || w->fullscreen_transition || (!(w->config.flags & MIME_WIN_RESIZABLE) && !w->fullscreen))
        return;
    MSG_V_INT(self, "setStyleMask:", MSG_UINT(self, "styleMask") | (1u << 3));
    g_toggle_fullscreen(self, sel, sender);
}

static void delegate_miniaturized(id self, SEL sel, id notification)
{
    restore_host_cursor();
    UiWindow *w = find_by_nswindow(MSG_ID(notification, "object"));
    if (w && !(w->config.flags & MIME_WIN_MINIMIZED))
        request_window_state(w, 6);
}

static void delegate_deminiaturized(id self, SEL sel, id notification)
{
    UiWindow *w = find_by_nswindow(MSG_ID(notification, "object"));
    if (w && ((w->config.flags & MIME_WIN_MINIMIZED) || (w->state_pending && w->state_command == 6)))
        request_window_state(w, 9);
}

static void emit_fullscreen(UiWindow *w, bool entering)
{
    NSRect frame = MSG_RECT(w->window, "frame");
    MimeInputEvent ev = {.type = MIME_INPUT_WINDOW_FULLSCREEN, .window_id = w->id, .flags = entering};
    screen_point_to_guest(CGPointMake(frame.origin.x, frame.origin.y + frame.size.height), &ev.x, &ev.y);
    ev.width = (int32_t)(frame.size.width * g_scale + 0.5);
    ev.height = (int32_t)(frame.size.height * g_scale + 0.5);
    emit(&ev);
}

static void delegate_fullscreen_will_change(id self, SEL sel, id notification)
{
    UiWindow *w = find_by_nswindow(MSG_ID(notification, "object"));
    if (w)
        w->fullscreen_transition = true;
}

static void exit_fullscreen_main(void *arg)
{
    UiWindow *w = find_window((uint32_t)(uintptr_t)arg);
    if (w && w->fullscreen && !w->fullscreen_transition && w->fullscreen_config_pending)
        MSG_V_ID(w->window, "toggleFullScreen:", nil);
}

static void delegate_fullscreen_entered(id self, SEL sel, id notification)
{
    UiWindow *w = find_by_nswindow(MSG_ID(notification, "object"));
    if (!w)
        return;
    w->fullscreen_transition = false;
    w->fullscreen = true;
    if (w->fullscreen_config_pending) {
        dispatch_async_f(dispatch_get_main_queue(), (void *)(uintptr_t)w->id, exit_fullscreen_main);
        return;
    }
    emit_fullscreen(w, true);
}

static void delegate_fullscreen_exited(id self, SEL sel, id notification)
{
    UiWindow *w = find_by_nswindow(MSG_ID(notification, "object"));
    if (!w)
        return;
    w->fullscreen_transition = false;
    w->fullscreen = false;
    w->fullscreen_synced = false;
    emit_fullscreen(w, false);
    if (w->fullscreen_config_pending) {
        w->fullscreen_config_pending = false;
        MimeWinConfig config = w->fullscreen_config;
        mime_ui_window_config(&config);
    }
}

static void delegate_fullscreen_failed(id self, SEL sel, id window)
{
    UiWindow *w = find_by_nswindow(window);
    if (!w)
        return;
    w->fullscreen_transition = false;
    if (!w->fullscreen) {
        MSG_V_INT(window, "setStyleMask:", (w->config.flags & MIME_WIN_MINIMIZABLE) ? 1u << 2 : 0);
        if (w->fullscreen_config_pending) {
            w->fullscreen_config_pending = false;
            MimeWinConfig config = w->fullscreen_config;
            mime_ui_window_config(&config);
        }
    }
}

static void clip_start(void *arg);
static void app_became_active(id self, SEL sel, id notification);
static void app_resigned_active(id self, SEL sel, id notification);
static void (*g_app_send_event)(id, SEL, id);

/* appkit drops key up events while command is held; the guest needs them to release its keys. */
static void app_send_event(id self, SEL sel, id event)
{
    NSUInteger type = MSG_UINT(event, "type");
    static uint16_t shortcut_key = UINT16_MAX;
    if (type == NSEventTypeKeyDown || type == NSEventTypeKeyUp) {
        uint16_t key = ((uint16_t (*)(id, SEL))objc_msgSend)(event, SEL_("keyCode"));
        if (type == NSEventTypeKeyUp && key == shortcut_key) {
            shortcut_key = UINT16_MAX;
            return;
        }
        NSUInteger flags = MSG_UINT(event, "modifierFlags");
        UiWindow *w = find_by_nswindow(MSG_ID(self, "keyWindow"));
        if (w && type == NSEventTypeKeyDown && (flags & NSEventModifierFlagCommand)) {
            if (key == 3 && (flags & NSEventModifierFlagControl) &&
                ((w->config.flags & MIME_WIN_RESIZABLE) || w->fullscreen)) {
                if (!w->fullscreen_transition)
                    MSG_V_ID(w->window, "toggleFullScreen:", nil);
                shortcut_key = key;
                return;
            }
            if (key == 46 && (w->config.flags & MIME_WIN_MINIMIZABLE) &&
                !(flags & (NSEventModifierFlagControl | NSEventModifierFlagOption))) {
                request_window_state(w, 6);
                shortcut_key = key;
                return;
            }
        }
    }
    if (type == NSEventTypeKeyUp && (MSG_UINT(event, "modifierFlags") & NSEventModifierFlagCommand)) {
        id window = MSG_ID(self, "keyWindow");
        if (window && find_by_nswindow(window)) {
            MSG_V_ID(window, "sendEvent:", event);
            return;
        }
    }
    g_app_send_event(self, sel, event);
}

static void register_app_class(void)
{
    Class base = objc_getClass("NSApplication");
    Class cls = objc_allocateClassPair(base, "MimeHostApplication", 0);
    g_app_send_event = (void (*)(id, SEL, id))class_getMethodImplementation(base, SEL_("sendEvent:"));
    class_addMethod(cls, SEL_("sendEvent:"), (IMP)app_send_event, "v@:@");
    objc_registerClassPair(cls);
    g_app = MSG_ID((id)cls, "sharedApplication");
}

static void register_classes(void)
{
    g_window_class = objc_allocateClassPair(objc_getClass("NSWindow"), "MimeHostWindow", 0);
    g_toggle_fullscreen = (void (*)(id, SEL, id))class_getMethodImplementation(objc_getClass("NSWindow"),
                                                                           SEL_("toggleFullScreen:"));
    class_addMethod(g_window_class, SEL_("canBecomeKeyWindow"), (IMP)ret_yes, "c@:");
    class_addMethod(g_window_class, SEL_("canBecomeMainWindow"), (IMP)ret_yes, "c@:");
    class_addMethod(g_window_class, SEL_("zoom:"), (IMP)window_zoom, "v@:@");
    class_addMethod(g_window_class, SEL_("performMiniaturize:"), (IMP)window_minimize, "v@:@");
    class_addMethod(g_window_class, SEL_("toggleFullScreen:"), (IMP)window_toggle_fullscreen, "v@:@");
    objc_registerClassPair(g_window_class);

    g_view_class = objc_allocateClassPair(objc_getClass("NSView"), "MimeHostView", 0);
    class_addMethod(g_view_class, SEL_("isFlipped"), (IMP)ret_yes, "c@:");
    class_addMethod(g_view_class, SEL_("acceptsFirstResponder"), (IMP)ret_yes, "c@:");
    class_addMethod(g_view_class, SEL_("acceptsFirstMouse:"), (IMP)ret_yes_arg, "c@:@");
    class_addMethod(g_view_class, SEL_("mouseDown:"), (IMP)view_mouse_down, "v@:@");
    class_addMethod(g_view_class, SEL_("mouseUp:"), (IMP)view_mouse_up, "v@:@");
    class_addMethod(g_view_class, SEL_("mouseDragged:"), (IMP)view_mouse_dragged, "v@:@");
    class_addMethod(g_view_class, SEL_("mouseMoved:"), (IMP)view_mouse_moved, "v@:@");
    class_addMethod(g_view_class, SEL_("mouseExited:"), (IMP)view_mouse_exited, "v@:@");
    class_addMethod(g_view_class, SEL_("mouseEntered:"), (IMP)view_cursor_update, "v@:@");
    class_addMethod(g_view_class, SEL_("cursorUpdate:"), (IMP)view_cursor_update, "v@:@");
    class_addMethod(g_view_class, SEL_("rightMouseDown:"), (IMP)view_right_down, "v@:@");
    class_addMethod(g_view_class, SEL_("rightMouseUp:"), (IMP)view_right_up, "v@:@");
    class_addMethod(g_view_class, SEL_("rightMouseDragged:"), (IMP)view_mouse_dragged, "v@:@");
    class_addMethod(g_view_class, SEL_("otherMouseDown:"), (IMP)view_other_down, "v@:@");
    class_addMethod(g_view_class, SEL_("otherMouseUp:"), (IMP)view_other_up, "v@:@");
    class_addMethod(g_view_class, SEL_("otherMouseDragged:"), (IMP)view_mouse_dragged, "v@:@");
    class_addMethod(g_view_class, SEL_("scrollWheel:"), (IMP)view_scroll, "v@:@");
    class_addMethod(g_view_class, SEL_("keyDown:"), (IMP)view_key_down, "v@:@");
    class_addMethod(g_view_class, SEL_("keyUp:"), (IMP)view_key_up, "v@:@");
    class_addMethod(g_view_class, SEL_("flagsChanged:"), (IMP)view_flags_changed, "v@:@");
    objc_registerClassPair(g_view_class);

    g_delegate_class = objc_allocateClassPair(objc_getClass("NSObject"), "MimeHostWindowDelegate", 0);
    class_addMethod(g_delegate_class, SEL_("windowDidMove:"), (IMP)delegate_did_move, "v@:@");
    class_addMethod(g_delegate_class, SEL_("windowDidBecomeKey:"), (IMP)delegate_became_key, "v@:@");
    class_addMethod(g_delegate_class, SEL_("windowDidResignKey:"), (IMP)delegate_resigned_key, "v@:@");
    class_addMethod(g_delegate_class, SEL_("windowShouldClose:"), (IMP)delegate_should_close, "c@:@");
    class_addMethod(g_delegate_class, SEL_("windowDidMiniaturize:"), (IMP)delegate_miniaturized, "v@:@");
    class_addMethod(g_delegate_class, SEL_("windowDidDeminiaturize:"), (IMP)delegate_deminiaturized, "v@:@");
    class_addMethod(g_delegate_class, SEL_("windowWillEnterFullScreen:"), (IMP)delegate_fullscreen_will_change, "v@:@");
    class_addMethod(g_delegate_class, SEL_("windowWillExitFullScreen:"), (IMP)delegate_fullscreen_will_change, "v@:@");
    class_addMethod(g_delegate_class, SEL_("windowDidEnterFullScreen:"), (IMP)delegate_fullscreen_entered, "v@:@");
    class_addMethod(g_delegate_class, SEL_("windowDidExitFullScreen:"), (IMP)delegate_fullscreen_exited, "v@:@");
    class_addMethod(g_delegate_class, SEL_("windowDidFailToEnterFullScreen:"), (IMP)delegate_fullscreen_failed, "v@:@");
    class_addMethod(g_delegate_class, SEL_("windowDidFailToExitFullScreen:"), (IMP)delegate_fullscreen_failed, "v@:@");
    class_addMethod(g_delegate_class, SEL_("appDidBecomeActive:"), (IMP)app_became_active, "v@:@");
    class_addMethod(g_delegate_class, SEL_("appDidResignActive:"), (IMP)app_resigned_active, "v@:@");
    objc_registerClassPair(g_delegate_class);
    g_delegate = MSG_ID(MSG_ID((id)g_delegate_class, "alloc"), "init");
    id center = MSG_ID(CLS("NSNotificationCenter"), "defaultCenter");
    ((void (*)(id, SEL, id, SEL, id, id))objc_msgSend)(center, SEL_("addObserver:selector:name:object:"), g_delegate,
                                                       SEL_("appDidBecomeActive:"),
                                                       NSApplicationDidBecomeActiveNotification, nil);
    ((void (*)(id, SEL, id, SEL, id, id))objc_msgSend)(center, SEL_("addObserver:selector:name:object:"), g_delegate,
                                                       SEL_("appDidResignActive:"),
                                                       NSApplicationDidResignActiveNotification, nil);
}

static void app_action(void *ctx, uint32_t process_id, uint32_t action)
{
    for (unsigned i = 0; i < MAX_WINDOWS; i++) {
        UiWindow *w = &g_windows[i];
        if (!w->used || w->config.process_id != process_id ||
            (w->config.flags & (MIME_WIN_APPWINDOW | MIME_WIN_VISIBLE)) !=
                (MIME_WIN_APPWINDOW | MIME_WIN_VISIBLE))
            continue;
        if (action == MIME_APP_CLOSE) {
            MimeInputEvent ev = {.type = MIME_INPUT_WINDOW_CLOSE, .window_id = w->id};
            emit(&ev);
        } else if (action == MIME_APP_ACTIVATE) {
            if ((w->config.flags & MIME_WIN_MINIMIZED) || MSG_BOOL(w->window, "isMiniaturized") ||
                (w->state_pending && w->state_command == 6)) {
                request_window_state(w, 9);
            } else {
                MSG_V_BOOL(g_app, "activateIgnoringOtherApps:", YES);
                MSG_V_ID(w->window, "makeKeyAndOrderFront:", nil);
            }
        }
    }
}

static void update_screen_metrics(void)
{
    id screens = MSG_ID(CLS("NSScreen"), "screens");
    if (!screens || MSG_UINT(screens, "count") == 0)
        return;
    id primary = ((id(*)(id, SEL, NSUInteger))objc_msgSend)(screens, SEL_("objectAtIndex:"), 0);
    NSRect frame = MSG_RECT(primary, "frame");
    g_scale = MSG_DBL(primary, "backingScaleFactor");
    if (g_scale < 1)
        g_scale = 1;
    g_screen_height_pts = frame.size.height;
}

void mime_ui_init(MimeUiInputSink sink, void *ctx)
{
    g_sink = sink;
    g_sink_ctx = ctx;
    g_colorspace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    register_app_class();
    /* per-program companion processes provide the dock entries. */
    MSG_V_INT(g_app, "setActivationPolicy:", 1);
    MSG_V(g_app, "finishLaunching");
    register_classes();
    update_screen_metrics();
    mime_app_init(NULL, app_action, NULL);
    clip_start(NULL);
}

void mime_ui_run(void)
{
    MSG_V(g_app, "run");
}

void mime_ui_get_display(MimeDisplayInfo *info)
{
    memset(info, 0, sizeof(*info));
    id screens = MSG_ID(CLS("NSScreen"), "screens");
    if (!screens || MSG_UINT(screens, "count") == 0) {
        info->width = 1920;
        info->height = 1080;
        info->work_width = 1920;
        info->work_height = 1080;
        info->dpi = 96;
        info->refresh_hz = 60;
        return;
    }
    id primary = ((id(*)(id, SEL, NSUInteger))objc_msgSend)(screens, SEL_("objectAtIndex:"), 0);
    NSRect frame = MSG_RECT(primary, "frame");
    NSRect visible = MSG_RECT(primary, "visibleFrame");
    update_screen_metrics();
    info->width = (uint32_t)(frame.size.width * g_scale);
    info->height = (uint32_t)(frame.size.height * g_scale);
    info->work_x = (uint32_t)((visible.origin.x - frame.origin.x) * g_scale);
    info->work_y = (uint32_t)((frame.size.height - (visible.origin.y + visible.size.height)) * g_scale);
    info->work_width = (uint32_t)(visible.size.width * g_scale);
    info->work_height = (uint32_t)(visible.size.height * g_scale);
    info->dpi = (uint32_t)(96.0 * g_scale + 0.5);
    NSInteger fps = ((NSInteger(*)(id, SEL))objc_msgSend)(primary, SEL_("maximumFramesPerSecond"));
    info->refresh_hz = fps > 0 ? (uint32_t)fps : 60;
}

typedef struct MainCall {
    void (*fn)(void *);
    void *arg;
} MainCall;

static void on_main(void (*fn)(void *), void *arg)
{
    if (pthread_main_np()) {
        fn(arg);
        return;
    }
    dispatch_async_f(dispatch_get_main_queue(), arg, fn);
}

static void stop_main(void *arg)
{
    restore_host_cursor();
    mime_app_shutdown();
    id app = MSG_ID(CLS("NSApplication"), "sharedApplication");
    MSG_V_ID(app, "terminate:", nil);
}

void mime_ui_stop(void)
{
    on_main(stop_main, NULL);
}

static void create_main(void *arg)
{
    uint32_t id_ = (uint32_t)(uintptr_t)arg;
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window(id_);
    pthread_mutex_unlock(&g_windows_lock);
    if (!w || w->window)
        return;

    NSRect rect = CGRectMake(0, 0, 100, 100);
    id window = ((id(*)(id, SEL, NSRect, NSUInteger, NSUInteger, BOOL))objc_msgSend)(
        MSG_ID((id)g_window_class, "alloc"), SEL_("initWithContentRect:styleMask:backing:defer:"), rect, 0, 2, YES);
    MSG_V_BOOL(window, "setReleasedWhenClosed:", NO);
    MSG_V_BOOL(window, "setOpaque:", NO);
    MSG_V_ID(window, "setBackgroundColor:", MSG_ID(CLS("NSColor"), "clearColor"));
    MSG_V_BOOL(window, "setHasShadow:", YES);
    MSG_V_BOOL(window, "setAcceptsMouseMovedEvents:", YES);
    MSG_V_ID(window, "setDelegate:", g_delegate);

    id view = ((id(*)(id, SEL, NSRect))objc_msgSend)(MSG_ID((id)g_view_class, "alloc"), SEL_("initWithFrame:"), rect);
    MSG_V_BOOL(view, "setWantsLayer:", YES);
    id content = MSG_ID(MSG_ID(CLS("CALayer"), "alloc"), "init");
    MSG_V_DBL(content, "setContentsScale:", g_scale);
    MSG_V_ID(content, "setContentsGravity:", kCAGravityTopLeft);
    MSG_V_ID(content, "setMagnificationFilter:", kCAFilterNearest);
    MSG_V_BOOL(content, "setMasksToBounds:", YES);
    MSG_V_ID(MSG_ID(view, "layer"), "addSublayer:", content);
    MSG_V_ID(window, "setContentView:", view);
    MSG_V_ID(window, "makeFirstResponder:", view);

    const NSUInteger tracking[] = {
        NSTrackingMouseMoved | NSTrackingActiveAlways | NSTrackingInVisibleRect | NSTrackingMouseEnteredAndExited,
        NSTrackingCursorUpdate | NSTrackingActiveInActiveApp | NSTrackingInVisibleRect,
    };
    for (unsigned i = 0; i < MIME_ARRAY_COUNT(tracking); i++) {
        id area = ((id (*)(id, SEL, NSRect, NSUInteger, id, id))objc_msgSend)(
            MSG_ID(CLS("NSTrackingArea"), "alloc"), SEL_("initWithRect:options:owner:userInfo:"), rect, tracking[i], view, nil);
        MSG_V_ID(view, "addTrackingArea:", area);
        MSG_V(area, "release");
    }

    w->window = window;
    w->view = view;
    w->content = content;
}

void mime_ui_window_create(uint32_t id_)
{
    pthread_mutex_lock(&g_windows_lock);
    if (!find_window(id_)) {
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (!g_windows[i].used) {
                memset(&g_windows[i], 0, sizeof(g_windows[i]));
                g_windows[i].used = true;
                g_windows[i].id = id_;
                pthread_mutex_init(&g_windows[i].lock, NULL);
                break;
            }
        }
    }
    pthread_mutex_unlock(&g_windows_lock);
    on_main(create_main, (void *)(uintptr_t)id_);
}

static void destroy_main(void *arg)
{
    uint32_t id_ = (uint32_t)(uintptr_t)arg;
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window(id_);
    if (!w) {
        pthread_mutex_unlock(&g_windows_lock);
        return;
    }
    pthread_mutex_unlock(&g_windows_lock);
    restore_host_cursor();
    mime_app_window_remove(id_);
    g_pointer_in_guest = false;
    if (w->window) {
        MSG_V_ID(w->window, "setDelegate:", nil);
        MSG_V_ID(w->window, "orderOut:", nil);
        MSG_V(w->window, "close");
        MSG_V(w->content, "release");
        MSG_V(w->view, "release");
        MSG_V(w->window, "release");
    }
    pthread_mutex_lock(&g_windows_lock);
    pthread_mutex_lock(&w->lock);
    if (w->surface)
        CFRelease(w->surface);
    w->surface = NULL;
    pthread_mutex_unlock(&w->lock);
    pthread_mutex_destroy(&w->lock);
    w->window = nil;
    w->view = nil;
    w->content = nil;
    w->used = false;
    pthread_mutex_unlock(&g_windows_lock);
    update_presentation();
}

void mime_ui_window_destroy(uint32_t id_)
{
    on_main(destroy_main, (void *)(uintptr_t)id_);
}

static void process_exit_main(void *arg)
{
    mime_app_process_exit((uint32_t)(uintptr_t)arg);
}

void mime_ui_process_exit(uint32_t process_id)
{
    on_main(process_exit_main, (void *)(uintptr_t)process_id);
}

typedef struct WindowIcon {
    uint32_t window_id, width, height;
    uint8_t pixels[];
} WindowIcon;

static void icon_main(void *arg)
{
    WindowIcon *icon = arg;
    if (find_window(icon->window_id))
        mime_app_window_icon(icon->window_id, icon->width && icon->height ? icon->pixels : NULL,
                              icon->width, icon->height, icon->width * 4);
    free(icon);
}

void mime_ui_window_icon(uint32_t id_, const void *pixels, uint32_t width, uint32_t height, uint32_t stride)
{
    if (width > 1024 || height > 1024 || stride < width * 4 || (!pixels && width && height))
        return;
    WindowIcon *icon = mime_xmalloc(sizeof(*icon) + (size_t)width * height * 4);
    *icon = (WindowIcon){.window_id = id_, .width = width, .height = height};
    for (uint32_t y = 0; y < height; y++)
        memcpy(icon->pixels + (size_t)y * width * 4, (const uint8_t *)pixels + (size_t)y * stride, (size_t)width * 4);
    on_main(icon_main, icon);
}

static void present_main(void *arg);
static void metal_relayout(uint32_t window_id);

static void config_main(void *arg)
{
    MimeWinConfig *cfg = arg;
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window(cfg->window_id);
    pthread_mutex_unlock(&g_windows_lock);
    if (!w || !w->window) {
        free(cfg);
        return;
    }
    if (w->state_pending) {
        if (cfg->state_request != w->state_request) {
            free(cfg);
            return;
        }
        w->state_pending = false;
    }
    char path[2048];
    mime_utf16_to_utf8(path, sizeof(path), cfg->image_path, MIME_MIN(cfg->image_chars, 512u));
    mime_app_window_set(w->id, cfg->process_id, path,
                        (cfg->flags & (MIME_WIN_APPWINDOW | MIME_WIN_VISIBLE)) ==
                            (MIME_WIN_APPWINDOW | MIME_WIN_VISIBLE));
    char title[1024];
    mime_utf16_to_utf8(title, sizeof(title), cfg->title, MIME_MIN(cfg->title_chars, 256u));
    id str = ((id (*)(id, SEL, const char *))objc_msgSend)(CLS("NSString"), SEL_("stringWithUTF8String:"), title);
    MSG_V_ID(w->window, "setTitle:", str);
    MSG_V_ID(w->window, "setMiniwindowTitle:", str);
    uint32_t state = MIME_WIN_VISIBLE | MIME_WIN_MINIMIZED | MIME_WIN_MAXIMIZED;
    if (w->fullscreen_transition && !w->fullscreen && !(cfg->flags & MIME_WIN_FULLSCREEN) &&
        (w->fullscreen_config_pending || ((cfg->flags ^ w->config.flags) & state))) {
        w->fullscreen_config = *cfg;
        w->fullscreen_config_pending = true;
        free(cfg);
        return;
    }
    if (w->fullscreen && w->fullscreen_synced && !(cfg->flags & MIME_WIN_FULLSCREEN)) {
        w->fullscreen_config = *cfg;
        w->fullscreen_config_pending = true;
        if (!w->fullscreen_transition)
            MSG_V_ID(w->window, "toggleFullScreen:", nil);
        free(cfg);
        return;
    }
    if (w->fullscreen && (cfg->flags & MIME_WIN_FULLSCREEN))
        w->fullscreen_synced = true;
    if (cfg->flags & MIME_WIN_MINIMIZED) {
        w->config = *cfg;
        w->configured = true;
        w->suppress_state = true;
        if (!(MSG_UINT(w->window, "styleMask") & (1u << 2)))
            MSG_V_INT(w->window, "setStyleMask:", MSG_UINT(w->window, "styleMask") | (1u << 2));
        if ((cfg->flags & MIME_WIN_VISIBLE) && !MSG_BOOL(w->window, "isMiniaturized"))
            MSG_V_ID(w->window, "miniaturize:", nil);
        if (!(cfg->flags & MIME_WIN_VISIBLE))
            MSG_V_ID(w->window, "orderOut:", nil);
        w->suppress_state = false;
        w->shown = (cfg->flags & MIME_WIN_VISIBLE) != 0;
        update_presentation();
        free(cfg);
        return;
    }
    bool cropping_changed = memcmp(&w->config.visible, &cfg->visible, sizeof(cfg->visible)) != 0 ||
                            memcmp(&w->config.input, &cfg->input, sizeof(cfg->input)) != 0 ||
                            w->config.rect.right - w->config.rect.left != cfg->rect.right - cfg->rect.left ||
                            w->config.rect.bottom - w->config.rect.top != cfg->rect.bottom - cfg->rect.top;
    w->config = *cfg;
    w->configured = true;

    /*
     * rounded corners and the 1 pixel border are drawn by the compositor, over
     * the guest pixels. a resize border around them stays all but transparent:
     * macos sends clicks on fully transparent pixels to the window below.
     */
    MimeRect vis = visible_part(cfg), host = host_part(cfg);
    id ca = CLS("CATransaction");
    MSG_V(ca, "begin");
    MSG_V_BOOL(ca, "setDisableActions:", YES);
    id root = MSG_ID(w->view, "layer");
    bool border = memcmp(&vis, &host, sizeof(vis)) != 0;
    CGColorRef band = CGColorCreateSRGB(0, 0, 0, border ? 2.0 / 255.0 : 0);
    MSG_V_PTR(root, "setBackgroundColor:", band);
    CGColorRelease(band);
    ((void (*)(id, SEL, NSRect))objc_msgSend)(
        w->content, SEL_("setFrame:"),
        CGRectMake((vis.left - host.left) / g_scale, (vis.top - host.top) / g_scale, (vis.right - vis.left) / g_scale,
                   (vis.bottom - vis.top) / g_scale));
    MSG_V_DBL(w->content, "setCornerRadius:", cfg->corner_radius / g_scale);
    if (cfg->border_color) {
        uint32_t c = cfg->border_color;
        CGColorRef color = CGColorCreateSRGB(((c >> 16) & 0xFF) / 255.0, ((c >> 8) & 0xFF) / 255.0, (c & 0xFF) / 255.0,
                                             ((c >> 24) & 0xFF) / 255.0);
        MSG_V_PTR(w->content, "setBorderColor:", color);
        CGColorRelease(color);
        MSG_V_DBL(w->content, "setBorderWidth:", 1.0 / g_scale);
    } else {
        MSG_V_DBL(w->content, "setBorderWidth:", 0.0);
    }
    MSG_V_BOOL(w->window, "setHasShadow:", (cfg->flags & MIME_WIN_SHADOW) != 0);
    if (cropping_changed) {
        pthread_mutex_lock(&w->lock);
        w->dirty = w->surface != NULL;
        pthread_mutex_unlock(&w->lock);
    }

    NSRect frame = guest_to_frame(host_screen_rect(cfg));
    g_suppress_move = true;
    if (!w->fullscreen_transition && !w->fullscreen) {
        NSUInteger style = (cfg->flags & MIME_WIN_MINIMIZABLE) ? 1u << 2 : 0;
        if (MSG_UINT(w->window, "styleMask") != style)
            MSG_V_INT(w->window, "setStyleMask:", style);
        MSG_V_INT(w->window, "setCollectionBehavior:", (cfg->flags & MIME_WIN_RESIZABLE) ? 1u << 7 : 1u << 8);
        if (!CGRectEqualToRect(MSG_RECT(w->window, "frame"), frame))
            ((void (*)(id, SEL, NSRect, BOOL))objc_msgSend)(w->window, SEL_("setFrame:display:"), frame, NO);
    }
    g_suppress_move = false;
    if (!w->fullscreen_transition && !w->fullscreen)
        MSG_V_INT(w->window, "setLevel:", (cfg->flags & MIME_WIN_TOPMOST) ? 3 : 0);

    if (cfg->flags & MIME_WIN_VISIBLE) {
        if (MSG_BOOL(w->window, "isMiniaturized")) {
            w->suppress_state = true;
            MSG_V_ID(w->window, "deminiaturize:", nil);
            w->suppress_state = false;
        }
        if (cfg->flags & MIME_WIN_ACTIVATE) {
            MSG_V_BOOL(g_app, "activateIgnoringOtherApps:", YES);
            MSG_V_ID(w->window, "makeKeyAndOrderFront:", nil);
        } else if (!w->shown || !MSG_BOOL(w->window, "isVisible")) {
            MSG_V(w->window, "orderFrontRegardless");
        }
        w->shown = true;
    } else if (w->shown) {
        MSG_V_ID(w->window, "orderOut:", nil);
        w->shown = false;
    }
    free(cfg);
    if (cropping_changed) {
        present_main((void *)(uintptr_t)w->id);
        metal_relayout(w->id);
    }
    MSG_V(ca, "commit");
    update_presentation();
    /* the shadow follows the shape, which a new corner radius or border changes. */
    if (cropping_changed)
        MSG_V(w->window, "invalidateShadow");
}

static void config_latest_main(void *arg)
{
    uint32_t id_ = (uint32_t)(uintptr_t)arg;
    MimeWinConfig *copy = NULL;
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window(id_);
    if (w) {
        pthread_mutex_lock(&w->lock);
        if (w->config_queued) {
            copy = mime_xmalloc(sizeof(*copy));
            *copy = w->pending_config;
            w->config_queued = false;
        }
        pthread_mutex_unlock(&w->lock);
    }
    pthread_mutex_unlock(&g_windows_lock);
    if (copy)
        config_main(copy);
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
        on_main(config_latest_main, (void *)(uintptr_t)cfg->window_id);
}

/*
 * shows the visible part of the window surface. the compositor reads the
 * surface itself, so a present costs no copy here; contentsChanged tells it
 * the pixels moved on.
 */
static void present_main(void *arg)
{
    uint32_t id_ = (uint32_t)(uintptr_t)arg;
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window(id_);
    pthread_mutex_unlock(&g_windows_lock);
    if (!w || !w->view)
        return;
    pthread_mutex_lock(&w->lock);
    w->update_queued = false;
    if (!w->dirty || !w->surface) {
        pthread_mutex_unlock(&w->lock);
        return;
    }
    MimeRect v = w->config.visible;
    if (!w->configured || v.right <= v.left || v.bottom <= v.top)
        v = (MimeRect){0, 0, (int32_t)w->width, (int32_t)w->height};
    v.left = MIME_MAX(v.left, 0);
    v.top = MIME_MAX(v.top, 0);
    v.right = MIME_MIN(v.right, (int32_t)w->width);
    v.bottom = MIME_MIN(v.bottom, (int32_t)w->height);
    if (v.right <= v.left || v.bottom <= v.top) {
        pthread_mutex_unlock(&w->lock);
        return;
    }
    w->dirty = false;
    IOSurfaceRef surface = (IOSurfaceRef)CFRetain(w->surface);
    double width = w->width, height = w->height;
    bool shadow_dirty = w->shadow_dirty;
    w->shadow_dirty = false;
    pthread_mutex_unlock(&w->lock);

    id layer = w->content;
    id ca = CLS("CATransaction");
    MSG_V(ca, "begin");
    MSG_V_BOOL(ca, "setDisableActions:", YES);
    NSRect crop = {{v.left / width, v.top / height}, {(v.right - v.left) / width, (v.bottom - v.top) / height}};
    ((void (*)(id, SEL, NSRect))objc_msgSend)(layer, SEL_("setContentsRect:"), crop);
    if (MSG_ID(layer, "contents") != (id)surface)
        MSG_V_ID(layer, "setContents:", (id)surface);
    else
        MSG_V(layer, "setContentsChanged");
    MSG_V(ca, "commit");
    CFRelease(surface);
    if (shadow_dirty) {
        MSG_V(w->window, "invalidateShadow");
    }
}

static IOSurfaceRef create_surface(uint32_t width, uint32_t height)
{
    int32_t w = (int32_t)width, h = (int32_t)height, bpe = 4, format = 'BGRA';
    CFNumberRef nw = CFNumberCreate(NULL, kCFNumberSInt32Type, &w);
    CFNumberRef nh = CFNumberCreate(NULL, kCFNumberSInt32Type, &h);
    CFNumberRef nb = CFNumberCreate(NULL, kCFNumberSInt32Type, &bpe);
    CFNumberRef nf = CFNumberCreate(NULL, kCFNumberSInt32Type, &format);
    const void *keys[] = {kIOSurfaceWidth, kIOSurfaceHeight, kIOSurfaceBytesPerElement, kIOSurfacePixelFormat};
    const void *values[] = {nw, nh, nb, nf};
    CFDictionaryRef props = CFDictionaryCreate(NULL, keys, values, 4, &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);
    IOSurfaceRef surface = IOSurfaceCreate(props);
    CFRelease(props);
    if (surface) {
        CFPropertyListRef space = CGColorSpaceCopyPropertyList(g_colorspace);
        if (space) {
            IOSurfaceSetValue(surface, kIOSurfaceColorSpace, space);
            CFRelease(space);
        }
    }
    CFRelease(nw);
    CFRelease(nh);
    CFRelease(nb);
    CFRelease(nf);
    return surface;
}

void mime_ui_window_present(uint32_t id_, const void *pixels, uint32_t width, uint32_t height, uint32_t stride,
                            MimeRect dirty)
{
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window(id_);
    if (!w) {
        pthread_mutex_unlock(&g_windows_lock);
        return;
    }
    pthread_mutex_lock(&w->lock);
    pthread_mutex_unlock(&g_windows_lock);
    if (w->width != width || w->height != height || !w->surface) {
        if (w->surface)
            CFRelease(w->surface);
        w->surface = create_surface(width, height);
        w->width = width;
        w->height = height;
        w->shadow_dirty = true;
        dirty = (MimeRect){0, 0, (int32_t)width, (int32_t)height};
    }
    if (!w->surface) {
        pthread_mutex_unlock(&w->lock);
        return;
    }
    int32_t left = MIME_MAX(dirty.left, 0), top = MIME_MAX(dirty.top, 0);
    int32_t right = MIME_MIN(dirty.right, (int32_t)width), bottom = MIME_MIN(dirty.bottom, (int32_t)height);
    IOSurfaceLock(w->surface, 0, NULL);
    uint8_t *base = IOSurfaceGetBaseAddress(w->surface);
    size_t row = IOSurfaceGetBytesPerRow(w->surface);
    for (int32_t y = top; y < bottom && left < right; y++)
        memcpy(base + (size_t)y * row + (size_t)left * 4, (const uint8_t *)pixels + (size_t)y * stride + (size_t)left * 4,
               (size_t)(right - left) * 4);
    IOSurfaceUnlock(w->surface, 0, NULL);
    w->dirty = true;
    bool queue = !w->update_queued;
    w->update_queued = true;
    pthread_mutex_unlock(&w->lock);
    if (queue)
        on_main(present_main, (void *)(uintptr_t)id_);
}

static id wait_cursor(void)
{
    static id cursor;
    if (cursor)
        return cursor;
    CGContextRef ctx = CGBitmapContextCreate(NULL, 64, 64, 8, 0, g_colorspace, kCGImageAlphaPremultipliedLast);
    if (!ctx)
        return MSG_ID(CLS("NSCursor"), "arrowCursor");
    CGContextScaleCTM(ctx, 2, 2);
    CGMutablePathRef path = CGPathCreateMutable();
    CGPathMoveToPoint(path, NULL, 9, 4);
    CGPathAddLineToPoint(path, NULL, 23, 4);
    CGPathAddLineToPoint(path, NULL, 23, 8);
    CGPathAddLineToPoint(path, NULL, 17, 16);
    CGPathAddLineToPoint(path, NULL, 23, 24);
    CGPathAddLineToPoint(path, NULL, 23, 28);
    CGPathAddLineToPoint(path, NULL, 9, 28);
    CGPathAddLineToPoint(path, NULL, 9, 24);
    CGPathAddLineToPoint(path, NULL, 15, 16);
    CGPathAddLineToPoint(path, NULL, 9, 8);
    CGPathCloseSubpath(path);
    CGContextSetRGBFillColor(ctx, 1, 1, 1, 1);
    CGContextSetRGBStrokeColor(ctx, 1, 1, 1, 1);
    CGContextSetLineWidth(ctx, 4);
    CGContextAddPath(ctx, path);
    CGContextDrawPath(ctx, kCGPathFillStroke);
    CGContextSetRGBStrokeColor(ctx, 0, 0, 0, 1);
    CGContextSetLineWidth(ctx, 2);
    CGContextAddPath(ctx, path);
    CGContextStrokePath(ctx);
    CGPathRelease(path);
    CGContextSetRGBFillColor(ctx, 0, 0, 0, 1);
    CGContextFillRect(ctx, CGRectMake(12, 24, 8, 2));
    CGContextMoveToPoint(ctx, 12, 8);
    CGContextAddLineToPoint(ctx, 20, 8);
    CGContextAddLineToPoint(ctx, 16, 13);
    CGContextClosePath(ctx);
    CGContextFillPath(ctx);
    CGImageRef cgimage = CGBitmapContextCreateImage(ctx);
    id image = ((id (*)(id, SEL, CGImageRef, NSSize))objc_msgSend)(MSG_ID(CLS("NSImage"), "alloc"),
        SEL_("initWithCGImage:size:"), cgimage, CGSizeMake(32, 32));
    cursor = ((id (*)(id, SEL, id, NSPoint))objc_msgSend)(MSG_ID(CLS("NSCursor"), "alloc"),
        SEL_("initWithImage:hotSpot:"), image, CGPointMake(16, 16));
    MSG_V(image, "release");
    CGImageRelease(cgimage);
    CGContextRelease(ctx);
    return cursor;
}

static void cursor_main(void *arg)
{
    static const char *const names[] = {
        "arrowCursor", "IBeamCursor", "arrowCursor", "crosshairCursor", "pointingHandCursor",
        "resizeUpDownCursor", "resizeLeftRightCursor", "arrowCursor", "arrowCursor", "openHandCursor",
        "operationNotAllowedCursor", "arrowCursor",
    };
    uint32_t c = (uint32_t)(uintptr_t)arg;
    if (c >= MIME_ARRAY_COUNT(names))
        c = 0;
    g_cursor = c;
    if (!g_pointer_in_guest)
        return;
    if (c == MIME_CURSOR_HIDDEN) {
        if (!g_cursor_hidden) {
            MSG_V(CLS("NSCursor"), "hide");
            g_cursor_hidden = true;
        }
        return;
    }
    restore_host_cursor();
    id cursor;
    if (c == MIME_CURSOR_SIZE_NWSE || c == MIME_CURSOR_SIZE_NESW) {
        NSUInteger corner = c == MIME_CURSOR_SIZE_NWSE ? 3 : 9;
        cursor = ((id (*)(id, SEL, NSUInteger, NSUInteger))objc_msgSend)(CLS("NSCursor"),
            SEL_("frameResizeCursorFromPosition:inDirections:"), corner, 3);
    } else if (c == MIME_CURSOR_WAIT) {
        cursor = wait_cursor();
    } else {
        cursor = MSG_ID(CLS("NSCursor"), names[c]);
    }
    MSG_V(cursor, "set");
}

void mime_ui_set_cursor(uint32_t cursor)
{
    on_main(cursor_main, (void *)(uintptr_t)cursor);
}

static void beep_main(void *arg)
{
    NSBeep();
}

void mime_ui_beep(uint32_t type)
{
    on_main(beep_main, NULL);
}

/* ------------------------------------------------------------------ clipboard */

/*
 * macos has no pasteboard change notification. other applications copy
 * while mime is in the background, so the change count is checked when
 * mime becomes active, and polled slowly only while it stays active for
 * the copies that happen meanwhile, such as universal clipboard. a change
 * only tells the guest whether there is text: the text itself is read when
 * a guest program asks for it, so vcpu threads never touch the pasteboard.
 */
typedef struct NSRange {
    NSUInteger location, length;
} NSRange;

extern id NSPasteboardTypeString;
extern void *objc_autoreleasePoolPush(void);
extern void objc_autoreleasePoolPop(void *pool);

static pthread_mutex_t g_clip_lock = PTHREAD_MUTEX_INITIALIZER;
static uint16_t *g_clip_text;
static size_t g_clip_chars;
static bool g_clip_stale;           /* the pasteboard changed since g_clip_text was read */
static NSInteger g_clip_seen = -1;  /* main thread only */
static dispatch_source_t g_clip_timer;
static bool g_clip_polling;

static void clip_check(void *arg)
{
    void *pool = objc_autoreleasePoolPush();
    id pb = MSG_ID(CLS("NSPasteboard"), "generalPasteboard");
    NSInteger count = ((NSInteger(*)(id, SEL))objc_msgSend)(pb, SEL_("changeCount"));
    if (count != g_clip_seen) {
        g_clip_seen = count;
        id types = ((id(*)(id, SEL, id))objc_msgSend)(CLS("NSArray"), SEL_("arrayWithObject:"), NSPasteboardTypeString);
        bool text = MSG_ID_ID(pb, "availableTypeFromArray:", types) != nil;
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
    objc_autoreleasePoolPop(pool);
}

/* the pasteboard text with windows line ends, read on the main thread. */
static void clip_read_main(void *arg)
{
    void *pool = objc_autoreleasePoolPush();
    id pb = MSG_ID(CLS("NSPasteboard"), "generalPasteboard");
    id str = MSG_ID_ID(pb, "stringForType:", NSPasteboardTypeString);
    NSUInteger n = str ? MSG_UINT(str, "length") : 0;
    uint16_t *raw = n ? malloc(n * sizeof(uint16_t)) : NULL;
    uint16_t *text = n ? malloc(n * 2 * sizeof(uint16_t)) : NULL;
    size_t chars = 0;
    if (raw && text) {
        ((void (*)(id, SEL, uint16_t *, NSRange))objc_msgSend)(str, SEL_("getCharacters:range:"), raw, (NSRange){0, n});
        for (NSUInteger i = 0; i < n; i++) {
            if (raw[i] == '\n' && (i == 0 || raw[i - 1] != '\r'))
                text[chars++] = '\r';
            text[chars++] = raw[i];
        }
    } else {
        free(text);
        text = NULL;
    }
    free(raw);
    pthread_mutex_lock(&g_clip_lock);
    free(g_clip_text);
    g_clip_text = text;
    g_clip_chars = chars;
    g_clip_stale = false;
    pthread_mutex_unlock(&g_clip_lock);
    objc_autoreleasePoolPop(pool);
}

static void clip_start(void *arg)
{
    g_clip_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    dispatch_source_set_event_handler_f(g_clip_timer, clip_check);
    dispatch_source_set_timer(g_clip_timer, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), NSEC_PER_SEC,
                              250 * NSEC_PER_MSEC);
    /* once the run loop starts, when the machine exists. */
    dispatch_async_f(dispatch_get_main_queue(), NULL, clip_check);
}

static void app_became_active(id self, SEL sel, id notification)
{
    clip_check(NULL);
    if (g_clip_timer && !g_clip_polling) {
        dispatch_resume(g_clip_timer);
        g_clip_polling = true;
    }
}

static void app_resigned_active(id self, SEL sel, id notification)
{
    restore_host_cursor();
    g_pointer_in_guest = false;
    if (g_clip_timer && g_clip_polling) {
        dispatch_suspend(g_clip_timer);
        g_clip_polling = false;
    }
}

typedef struct ClipSet {
    uint16_t *text;
    size_t chars;
} ClipSet;

static void clip_set_main(void *arg)
{
    ClipSet *c = arg;
    void *pool = objc_autoreleasePoolPush();
    id pb = MSG_ID(CLS("NSPasteboard"), "generalPasteboard");
    ((NSInteger(*)(id, SEL))objc_msgSend)(pb, SEL_("clearContents"));
    if (c->chars) {
        id str = ((id(*)(id, SEL, const uint16_t *, NSUInteger))objc_msgSend)(
            MSG_ID(CLS("NSString"), "alloc"), SEL_("initWithCharacters:length:"), c->text, c->chars);
        ((BOOL(*)(id, SEL, id, id))objc_msgSend)(pb, SEL_("setString:forType:"), str, NSPasteboardTypeString);
        MSG_V(str, "release");
    }
    /* the guest already holds this text: do not offer it back. */
    g_clip_seen = ((NSInteger(*)(id, SEL))objc_msgSend)(pb, SEL_("changeCount"));
    pthread_mutex_lock(&g_clip_lock);
    g_clip_stale = false;
    pthread_mutex_unlock(&g_clip_lock);
    objc_autoreleasePoolPop(pool);
    free(c->text);
    free(c);
}

void mime_ui_clipboard_set(const uint16_t *text, size_t chars)
{
    ClipSet *c = calloc(1, sizeof(*c));
    uint16_t *copy = malloc((chars ? chars : 1) * sizeof(uint16_t));
    uint16_t *keep = malloc((chars ? chars : 1) * sizeof(uint16_t));
    if (!c || !copy || !keep) {
        free(c);
        free(copy);
        free(keep);
        return;
    }
    size_t n = 0;
    for (size_t i = 0; i < chars; i++)
        if (!(text[i] == '\r' && i + 1 < chars && text[i + 1] == '\n'))
            copy[n++] = text[i];
    memcpy(keep, text, chars * sizeof(uint16_t));
    pthread_mutex_lock(&g_clip_lock);
    free(g_clip_text);
    g_clip_text = keep;
    g_clip_chars = chars;
    g_clip_stale = false;
    pthread_mutex_unlock(&g_clip_lock);
    c->text = copy;
    c->chars = n;
    on_main(clip_set_main, c);
}

size_t mime_ui_clipboard_read(uint16_t *dst, size_t offset, size_t count)
{
    pthread_mutex_lock(&g_clip_lock);
    bool stale = g_clip_stale;
    pthread_mutex_unlock(&g_clip_lock);
    /* the main thread never waits for a vcpu, so this cannot deadlock. */
    if (stale)
        dispatch_sync_f(dispatch_get_main_queue(), NULL, clip_read_main);
    pthread_mutex_lock(&g_clip_lock);
    size_t total = g_clip_chars;
    if (offset < total && count) {
        size_t n = total - offset < count ? total - offset : count;
        memcpy(dst, g_clip_text + offset, n * sizeof(uint16_t));
    }
    pthread_mutex_unlock(&g_clip_lock);
    return total;
}

/* ---------------------------------------------------------------- metal layers */

/*
 * layers the gpu channels present to, placed over the windows' views. the
 * guest renders rows bottom first, as gl stores them, so each layer is
 * flipped by its transform instead of a copy.
 */
typedef struct CATransform3D {
    double m11, m12, m13, m14, m21, m22, m23, m24, m31, m32, m33, m34, m41, m42, m43, m44;
} CATransform3D;

typedef struct MetalLayer {
    id layer;
    uint32_t window_id;
    int32_t x, y;
    uint32_t width, height;
    bool sync;
} MetalLayer;

#define MAX_METAL_LAYERS 64

static MetalLayer g_metal[MAX_METAL_LAYERS]; /* main thread only */

static void metal_layout(MetalLayer *m)
{
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = m->window_id ? find_window(m->window_id) : NULL;
    pthread_mutex_unlock(&g_windows_lock);
    id ca = CLS("CATransaction");
    MSG_V(ca, "begin");
    MSG_V_BOOL(ca, "setDisableActions:", YES);
    if (!w || !w->view) {
        MSG_V_BOOL(m->layer, "setHidden:", YES);
        MSG_V(m->layer, "removeFromSuperlayer");
    } else {
        id parent = w->content;
        if (MSG_ID(m->layer, "superlayer") != parent)
            MSG_V_ID(parent, "addSublayer:", m->layer);
        MimeRect v = w->config.visible;
        if (!w->configured || v.right <= v.left || v.bottom <= v.top)
            v = (MimeRect){0, 0, 0, 0};
        double width = m->width / g_scale, height = m->height / g_scale;
        double x = (m->x - v.left) / g_scale, y = (m->y - v.top) / g_scale;
        ((void (*)(id, SEL, NSRect))objc_msgSend)(m->layer, SEL_("setBounds:"), CGRectMake(0, 0, width, height));
        ((void (*)(id, SEL, NSPoint))objc_msgSend)(m->layer, SEL_("setPosition:"),
                                                   CGPointMake(x + width / 2, y + height / 2));
        ((void (*)(id, SEL, NSSize))objc_msgSend)(m->layer, SEL_("setDrawableSize:"), CGSizeMake(m->width, m->height));
        MSG_V_BOOL(m->layer, "setDisplaySyncEnabled:", m->sync);
        MSG_V_BOOL(m->layer, "setHidden:", NO);
    }
    MSG_V(ca, "commit");
}

static void metal_relayout(uint32_t window_id)
{
    for (int i = 0; i < MAX_METAL_LAYERS; i++)
        if (g_metal[i].layer && g_metal[i].window_id == window_id)
            metal_layout(&g_metal[i]);
}

typedef struct MetalCreate {
    void *device;
    id layer;
} MetalCreate;

static void metal_create_main(void *arg)
{
    MetalCreate *m = arg;
    id ca = CLS("CATransaction");
    MSG_V(ca, "begin");
    m->layer = MSG_ID(MSG_ID(CLS("CAMetalLayer"), "alloc"), "init");
    MSG_V_ID(m->layer, "setDevice:", (id)m->device);
    MSG_V_INT(m->layer, "setPixelFormat:", 80); /* MTLPixelFormatBGRA8Unorm */
    MSG_V_BOOL(m->layer, "setFramebufferOnly:", NO);
    MSG_V(ca, "commit");
}

/* on the main thread: a thread without a run loop never commits the layer's implicit transaction. */
void *mime_ui_metal_layer_create(void *device)
{
    MetalCreate m = {device, nil};
    if (pthread_main_np())
        metal_create_main(&m);
    else
        dispatch_sync_f(dispatch_get_main_queue(), &m, metal_create_main);
    return m.layer;
}

static void metal_place_main(void *arg)
{
    MetalLayer *p = arg;
    MetalLayer *m = NULL, *free_slot = NULL;
    for (int i = 0; i < MAX_METAL_LAYERS && !m; i++) {
        if (g_metal[i].layer == p->layer)
            m = &g_metal[i];
        else if (!g_metal[i].layer && !free_slot)
            free_slot = &g_metal[i];
    }
    if (!m && free_slot) {
        m = free_slot;
        id ca = CLS("CATransaction");
        MSG_V(ca, "begin");
        MSG_V_BOOL(ca, "setDisableActions:", YES);
        MSG_V_DBL(p->layer, "setContentsScale:", g_scale);
        MSG_V_BOOL(p->layer, "setOpaque:", YES);
        CATransform3D flip = {1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        ((void (*)(id, SEL, CATransform3D))objc_msgSend)(p->layer, SEL_("setTransform:"), flip);
        MSG_V(ca, "commit");
    }
    if (m) {
        *m = *p;
        metal_layout(m);
    }
    free(p);
}

void mime_ui_metal_layer_place(void *layer, uint32_t window_id, int32_t x, int32_t y, uint32_t width,
                               uint32_t height, bool sync)
{
    MetalLayer *p = mime_xmalloc(sizeof(*p));
    *p = (MetalLayer){layer, window_id, x, y, width, height, sync};
    on_main(metal_place_main, p);
}

static void metal_destroy_main(void *arg)
{
    id layer = arg;
    for (int i = 0; i < MAX_METAL_LAYERS; i++)
        if (g_metal[i].layer == layer)
            memset(&g_metal[i], 0, sizeof(g_metal[i]));
    MSG_V(layer, "removeFromSuperlayer");
    MSG_V(layer, "release");
}

void mime_ui_metal_layer_destroy(void *layer)
{
    on_main(metal_destroy_main, layer);
}
