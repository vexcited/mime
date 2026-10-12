/*
 * cocoa backend, written against the objective-c runtime so the whole
 * code base stays plain c. every guest top level window becomes a
 * borderless, transparent NSWindow whose layer shows the guest pixels.
 *
 * objc_msgSend must always be cast to the exact prototype of the method:
 * on arm64 variadic arguments travel on the stack, unlike real arguments.
 */
#include "ui.h"

#include <CoreGraphics/CoreGraphics.h>
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
    NSEventTypeKeyUp = 11,
};

enum {
    NSTrackingMouseEnteredAndExited = 0x01,
    NSTrackingMouseMoved = 0x02,
    NSTrackingActiveAlways = 0x80,
    NSTrackingInVisibleRect = 0x200,
};

extern id kCAGravityTopLeft;
extern id kCAFilterNearest;
extern id NSWindowDidMoveNotification;
extern void NSBeep(void);

#define MAX_WINDOWS 256

typedef struct UiWindow {
    uint32_t id;
    bool used;
    id window;
    id view;
    MimeWinConfig config;
    bool configured;
    bool shown;

    pthread_mutex_t lock;
    uint8_t *pixels;
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
static id g_delegate;
static double g_scale = 2.0;
static double g_screen_height_pts;
static CGColorSpaceRef g_colorspace;
static bool g_suppress_move;

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

/* the host window covers the visible part of the guest window, in screen pixels. */
static MimeRect visible_screen_rect(const MimeWinConfig *c)
{
    MimeRect v = c->visible;
    if (v.right <= v.left || v.bottom <= v.top)
        v = (MimeRect){0, 0, c->rect.right - c->rect.left, c->rect.bottom - c->rect.top};
    return (MimeRect){c->rect.left + v.left, c->rect.top + v.top, c->rect.left + v.right, c->rect.top + v.bottom};
}

/* true when a window relative guest point lies in a caption drag area. */
static bool in_drag_area(UiWindow *w, id event)
{
    NSPoint loc = MSG_POINT(event, "locationInWindow");
    NSRect frame = MSG_RECT(w->window, "frame");
    MimeRect vis = visible_screen_rect(&w->config);
    int32_t x = (int32_t)(loc.x * g_scale) + vis.left - w->config.rect.left;
    int32_t y = (int32_t)((frame.size.height - loc.y) * g_scale) + vis.top - w->config.rect.top;
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
    if (w && in_drag_area(w, event) && ((NSInteger(*)(id, SEL))objc_msgSend)(event, SEL_("clickCount")) == 1) {
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

static void view_mouse_moved(id self, SEL sel, id event)
{
    emit_mouse(self, event, MIME_INPUT_MOUSE_MOVE, 0);
}

static void view_mouse_exited(id self, SEL sel, id event)
{
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
    ev.scancode = k->scan;
    ev.modifiers = modifiers_of(event);
    if (type == MIME_INPUT_KEY_DOWN && MSG_BOOL(event, "isARepeat"))
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
    if (!w)
        return;
    NSRect frame = MSG_RECT(window, "frame");
    MimeRect vis = visible_screen_rect(&w->config);
    MimeInputEvent ev = {0};
    ev.type = MIME_INPUT_WINDOW_MOVED;
    ev.window_id = w->id;
    ev.x = (int32_t)(frame.origin.x * g_scale + 0.5) - (vis.left - w->config.rect.left);
    ev.y = (int32_t)((g_screen_height_pts - frame.origin.y - frame.size.height) * g_scale + 0.5) -
           (vis.top - w->config.rect.top);
    w->config.rect.right += ev.x - w->config.rect.left;
    w->config.rect.bottom += ev.y - w->config.rect.top;
    w->config.rect.left = ev.x;
    w->config.rect.top = ev.y;
    emit(&ev);
}

static void delegate_focus(id self, SEL sel, id notification, bool gained)
{
    UiWindow *w = find_by_nswindow(MSG_ID(notification, "object"));
    if (!w)
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
}

static void delegate_resigned_key(id self, SEL sel, id notification)
{
    delegate_focus(self, sel, notification, false);
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

static void (*g_app_send_event)(id, SEL, id);

/* appkit drops key up events while command is held; the guest needs them to release its keys. */
static void app_send_event(id self, SEL sel, id event)
{
    NSUInteger type = MSG_UINT(event, "type");
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
    class_addMethod(g_window_class, SEL_("canBecomeKeyWindow"), (IMP)ret_yes, "c@:");
    class_addMethod(g_window_class, SEL_("canBecomeMainWindow"), (IMP)ret_yes, "c@:");
    objc_registerClassPair(g_window_class);

    g_view_class = objc_allocateClassPair(objc_getClass("NSView"), "MimeHostView", 0);
    class_addMethod(g_view_class, SEL_("isFlipped"), (IMP)ret_yes, "c@:");
    class_addMethod(g_view_class, SEL_("acceptsFirstResponder"), (IMP)ret_yes, "c@:");
    class_addMethod(g_view_class, SEL_("acceptsFirstMouse:"), (IMP)ret_yes_arg, "c@:@");
    class_addMethod(g_view_class, SEL_("mouseDown:"), (IMP)view_mouse_down, "v@:@");
    class_addMethod(g_view_class, SEL_("mouseUp:"), (IMP)view_mouse_up, "v@:@");
    class_addMethod(g_view_class, SEL_("mouseDragged:"), (IMP)view_mouse_moved, "v@:@");
    class_addMethod(g_view_class, SEL_("mouseMoved:"), (IMP)view_mouse_moved, "v@:@");
    class_addMethod(g_view_class, SEL_("mouseExited:"), (IMP)view_mouse_exited, "v@:@");
    class_addMethod(g_view_class, SEL_("rightMouseDown:"), (IMP)view_right_down, "v@:@");
    class_addMethod(g_view_class, SEL_("rightMouseUp:"), (IMP)view_right_up, "v@:@");
    class_addMethod(g_view_class, SEL_("rightMouseDragged:"), (IMP)view_mouse_moved, "v@:@");
    class_addMethod(g_view_class, SEL_("otherMouseDown:"), (IMP)view_other_down, "v@:@");
    class_addMethod(g_view_class, SEL_("otherMouseUp:"), (IMP)view_other_up, "v@:@");
    class_addMethod(g_view_class, SEL_("otherMouseDragged:"), (IMP)view_mouse_moved, "v@:@");
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
    objc_registerClassPair(g_delegate_class);
    g_delegate = MSG_ID(MSG_ID((id)g_delegate_class, "alloc"), "init");
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
    /* accessory: no dock icon or menu bar, the guest windows are the whole ui. */
    MSG_V_INT(g_app, "setActivationPolicy:", 1);
    MSG_V(g_app, "finishLaunching");
    register_classes();
    update_screen_metrics();
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
    id layer = MSG_ID(view, "layer");
    MSG_V_DBL(layer, "setContentsScale:", g_scale);
    MSG_V_ID(layer, "setContentsGravity:", kCAGravityTopLeft);
    MSG_V_ID(layer, "setMagnificationFilter:", kCAFilterNearest);
    MSG_V_BOOL(layer, "setMasksToBounds:", YES);
    MSG_V_ID(window, "setContentView:", view);
    MSG_V_ID(window, "makeFirstResponder:", view);

    id area = ((id(*)(id, SEL, NSRect, NSUInteger, id, id))objc_msgSend)(
        MSG_ID(CLS("NSTrackingArea"), "alloc"), SEL_("initWithRect:options:owner:userInfo:"), rect,
        NSTrackingMouseMoved | NSTrackingActiveAlways | NSTrackingInVisibleRect | NSTrackingMouseEnteredAndExited, view,
        nil);
    MSG_V_ID(view, "addTrackingArea:", area);
    MSG_V(area, "release");

    w->window = window;
    w->view = view;
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
    w->used = false;
    pthread_mutex_unlock(&g_windows_lock);
    if (w->window) {
        MSG_V_ID(w->window, "setDelegate:", nil);
        MSG_V_ID(w->window, "orderOut:", nil);
        MSG_V(w->window, "close");
        MSG_V(w->view, "release");
        MSG_V(w->window, "release");
    }
    pthread_mutex_lock(&w->lock);
    free(w->pixels);
    w->pixels = NULL;
    pthread_mutex_unlock(&w->lock);
    w->window = nil;
    w->view = nil;
}

void mime_ui_window_destroy(uint32_t id_)
{
    on_main(destroy_main, (void *)(uintptr_t)id_);
}

static void present_main(void *arg);

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
    bool cropping_changed = memcmp(&w->config.visible, &cfg->visible, sizeof(cfg->visible)) != 0 ||
                            w->config.rect.right - w->config.rect.left != cfg->rect.right - cfg->rect.left ||
                            w->config.rect.bottom - w->config.rect.top != cfg->rect.bottom - cfg->rect.top;
    w->config = *cfg;
    w->configured = true;

    /* rounded corners and the 1 pixel border are drawn by the compositor, over the guest pixels. */
    id layer = MSG_ID(w->view, "layer");
    MSG_V_DBL(layer, "setCornerRadius:", cfg->corner_radius / g_scale);
    if (cfg->border_color) {
        uint32_t c = cfg->border_color;
        CGColorRef color = CGColorCreateSRGB(((c >> 16) & 0xFF) / 255.0, ((c >> 8) & 0xFF) / 255.0, (c & 0xFF) / 255.0,
                                             ((c >> 24) & 0xFF) / 255.0);
        MSG_V_PTR(layer, "setBorderColor:", color);
        CGColorRelease(color);
        MSG_V_DBL(layer, "setBorderWidth:", 1.0 / g_scale);
    } else {
        MSG_V_DBL(layer, "setBorderWidth:", 0.0);
    }
    MSG_V_BOOL(w->window, "setHasShadow:", (cfg->flags & MIME_WIN_SHADOW) != 0);
    if (cropping_changed) {
        pthread_mutex_lock(&w->lock);
        w->dirty = w->pixels != NULL;
        pthread_mutex_unlock(&w->lock);
    }

    NSRect frame = guest_to_frame(visible_screen_rect(cfg));
    g_suppress_move = true;
    ((void (*)(id, SEL, NSRect, BOOL))objc_msgSend)(w->window, SEL_("setFrame:display:"), frame, NO);
    g_suppress_move = false;
    MSG_V_INT(w->window, "setLevel:", (cfg->flags & MIME_WIN_TOPMOST) ? 3 : 0);

    char title[512];
    mime_utf16_to_utf8(title, sizeof(title), cfg->title, MIME_MIN(cfg->title_chars, 256u));
    id str = ((id(*)(id, SEL, const char *))objc_msgSend)(CLS("NSString"), SEL_("stringWithUTF8String:"), title);
    MSG_V_ID(w->window, "setTitle:", str);

    if (cfg->flags & MIME_WIN_VISIBLE) {
        if (cfg->flags & MIME_WIN_ACTIVATE) {
            MSG_V_BOOL(g_app, "activateIgnoringOtherApps:", YES);
            MSG_V_ID(w->window, "makeKeyAndOrderFront:", nil);
        } else if (!w->shown) {
            MSG_V(w->window, "orderFrontRegardless");
        }
        w->shown = true;
    } else if (w->shown) {
        MSG_V_ID(w->window, "orderOut:", nil);
        w->shown = false;
    }
    free(cfg);
    if (cropping_changed)
        present_main((void *)(uintptr_t)w->id);
}

void mime_ui_window_config(const MimeWinConfig *cfg)
{
    MimeWinConfig *copy = mime_xmalloc(sizeof(*copy));
    *copy = *cfg;
    on_main(config_main, copy);
}

static void release_data(void *info, const void *data, size_t size)
{
    free((void *)data);
}

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
    if (!w->dirty || !w->pixels) {
        pthread_mutex_unlock(&w->lock);
        return;
    }
    /* only the visible part of the surface is shown. */
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
    uint32_t width = (uint32_t)(v.right - v.left), height = (uint32_t)(v.bottom - v.top);
    size_t size = (size_t)width * height * 4;
    uint8_t *copy = malloc(size);
    for (uint32_t y = 0; y < height; y++)
        memcpy(copy + (size_t)y * width * 4, w->pixels + (((size_t)(v.top + (int32_t)y)) * w->width + (size_t)v.left) * 4,
               (size_t)width * 4);
    w->dirty = false;
    pthread_mutex_unlock(&w->lock);

    CGDataProviderRef provider = CGDataProviderCreateWithData(NULL, copy, size, release_data);
    CGImageRef image = CGImageCreate(width, height, 8, 32, width * 4, g_colorspace,
                                     kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst, provider, NULL,
                                     false, kCGRenderingIntentDefault);
    CGDataProviderRelease(provider);
    id layer = MSG_ID(w->view, "layer");
    id ca = CLS("CATransaction");
    MSG_V(ca, "begin");
    MSG_V_BOOL(ca, "setDisableActions:", YES);
    MSG_V_PTR(layer, "setContents:", image);
    MSG_V(ca, "commit");
    CGImageRelease(image);
    MSG_V(w->window, "invalidateShadow");
}

void mime_ui_window_present(uint32_t id_, const void *pixels, uint32_t width, uint32_t height, uint32_t stride,
                            MimeRect dirty)
{
    pthread_mutex_lock(&g_windows_lock);
    UiWindow *w = find_window(id_);
    pthread_mutex_unlock(&g_windows_lock);
    if (!w)
        return;
    pthread_mutex_lock(&w->lock);
    if (w->width != width || w->height != height || !w->pixels) {
        free(w->pixels);
        w->pixels = mime_xcalloc((size_t)width * height, 4);
        w->width = width;
        w->height = height;
        dirty = (MimeRect){0, 0, (int32_t)width, (int32_t)height};
    }
    int32_t left = MIME_MAX(dirty.left, 0), top = MIME_MAX(dirty.top, 0);
    int32_t right = MIME_MIN(dirty.right, (int32_t)width), bottom = MIME_MIN(dirty.bottom, (int32_t)height);
    for (int32_t y = top; y < bottom && left < right; y++)
        memcpy(w->pixels + ((size_t)y * width + (size_t)left) * 4, (const uint8_t *)pixels + (size_t)y * stride + (size_t)left * 4,
               (size_t)(right - left) * 4);
    w->dirty = true;
    bool queue = !w->update_queued;
    w->update_queued = true;
    pthread_mutex_unlock(&w->lock);
    if (queue)
        on_main(present_main, (void *)(uintptr_t)id_);
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
    MSG_V(MSG_ID(CLS("NSCursor"), names[c]), "set");
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
