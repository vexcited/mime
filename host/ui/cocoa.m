/*
 * Cocoa backend. each guest top level window becomes a borderless transparent NSWindow displaying guest pixels.
 */
#include "ui.h"
#include "app_identity.h"
#include "../vulkan/display.h"

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <IOSurface/IOSurface.h>
#include <dispatch/dispatch.h>
#include <math.h>
#include <os/lock.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* private: announces pixels written in place to an iosurface the layer already shows. */
@interface CALayer (MimePrivate)
- (void)setContentsChanged;
@end

__attribute__((objc_subclassing_restricted))
@interface MimeHostApplication : NSApplication
@end

__attribute__((objc_subclassing_restricted))
@interface MimeHostWindow : NSWindow
- (void)toggleFullScreenUnchecked __attribute__((objc_direct));
@end

@interface MimeHostView : NSView <NSTextInputClient>
@end

__attribute__((objc_subclassing_restricted))
@interface MimeHostWindowDelegate : NSObject <NSWindowDelegate>
@end

__attribute__((objc_subclassing_restricted))
@interface MimeMetalLayer : CAMetalLayer
@property (nonatomic) BOOL native;
@end

@implementation MimeMetalLayer
@end

static constexpr int MAX_WINDOWS = 256;

typedef struct UiWindow {
    uint32_t id;
    bool used;
    MimeHostWindow *window;
    MimeHostView *view;
    CALayer *content; /* the rounded layer showing the visible part, inside any resize border. */
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
    bool space_declined; /* the user left the space while the guest window still covers the screen. */
    MimeWinConfig fullscreen_config;
    MimeWinConfig pending_config;
    bool config_queued;
    double scroll_x, scroll_y;
    double pinch_remainder;
    uint32_t scroll_modifiers;
    bool scroll_precise;

    os_unfair_lock lock;
    IOSurfaceRef surface; /* the guest pixels, read by the compositor in place. */
    uint32_t width, height;
    bool dirty;
    bool update_queued;
    bool shadow_dirty;
    NSWindow *chrome;         /* a child window below, drawing the shadow and the outer outline. */
    CALayer *chrome_shadow;   /* its layer casting the shadow. */
    CALayer *chrome_outline;
    uint32_t anim_gen; /* bumped by every show or hide animation. */
} UiWindow;

static UiWindow g_windows[MAX_WINDOWS];
static os_unfair_lock g_windows_lock = OS_UNFAIR_LOCK_INIT;
static MimeUiInputSink g_sink;
static void *g_sink_ctx;
static MimeHostWindowDelegate *g_delegate;
[[maybe_unused]] static id<NSObject> g_activity; /* keeps App Nap away while the machine runs. */
static double g_scale = 2.0;
static double g_screen_height_pts;
static MimeDisplayInfo g_display; /* as last reported. */
static double g_screen_band; /* the menu bar or camera housing at the top of the main screen. */
static NSRect g_work_points;  /* the work area in points from the top left of the guest screen. */
static CGColorSpaceRef g_colorspace;
static bool g_suppress_move;
static uint32_t g_cursor;
static bool g_cursor_hidden;
static bool g_pointer_in_guest;
/* what the guest asked of the pointer, and whether the mouse is cut from the cursor to report motion alone now. */
static uint32_t g_pointer_mode, g_pointer_window;
static bool g_pointer_locked;
static double g_motion_x, g_motion_y;
static double g_warp_x, g_warp_y; /* how far the last parking carried the cursor, in points. */
static bool g_fullscreen_presentation;
/* MIME_BACKGROUND: windows open behind the user's apps and never take the focus, for test runs on a desktop in use. */
static bool g_background;
static void cursor_main(uint32_t c);
static void app_became_active(void);
static void app_resigned_active(void);
static void screens_changed(void);

static void restore_host_cursor(void)
{
    if (g_cursor_hidden) {
        [NSCursor unhide];
        g_cursor_hidden = false;
    }
}

static UiWindow *find_window(uint32_t id)
{
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (g_windows[i].used && g_windows[i].id == id)
            return &g_windows[i];
    return nullptr;
}

static UiWindow *find_by_nswindow(NSWindow *window)
{
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (g_windows[i].used && g_windows[i].window == window)
            return &g_windows[i];
    return nullptr;
}

/* buttons land where the cursor stays, which is the center of the window that locked it. */
static void pointer_park(UiWindow *w)
{
    if (!g_pointer_locked || !w || w->id != g_pointer_window)
        return;
    /* Quartz counts from the top of the whole primary screen, band included. */
    NSRect frame = w->window.frame;
    CGPoint center = CGPointMake(NSMidX(frame), g_screen_height_pts + g_screen_band - NSMidY(frame));
    CGEventRef probe = CGEventCreate(nullptr);
    if (probe) {
        CGPoint from = CGEventGetLocation(probe);
        CFRelease(probe);
        g_warp_x += center.x - from.x;
        g_warp_y += center.y - from.y;
    }
    CGWarpMouseCursorPosition(center);
    /* a warp holds the mouse events back for a moment, which this call ends. */
    CGAssociateMouseAndMouseCursorPosition(false);
}

/* the mouse is locked while the window that asked is the key one, so another app gets its cursor back. */
static void pointer_apply(void)
{
    UiWindow *w = g_pointer_mode == MIME_POINTER_RELATIVE ? find_window(g_pointer_window) : nullptr;
    bool lock = w && w->window.keyWindow && NSApp.active;
    if (lock == g_pointer_locked)
        return;
    g_pointer_locked = lock;
    g_motion_x = g_motion_y = 0;
    g_warp_x = g_warp_y = 0;
    if (lock) {
        pointer_park(w);
        /* the parked cursor is in the window without having entered it, so the guest's cursor is put again. */
        g_pointer_in_guest = true;
        cursor_main(g_cursor);
    } else {
        CGAssociateMouseAndMouseCursorPosition(true);
    }
}

/* the motion of an event: the device's own counts while locked, as raw input reports them, else guest pixels. */
static void motion_of(NSEvent *event, MimeInputEvent *ev)
{
    /* the first event after a parking counts the jump of the cursor in its deltas, which is not motion. */
    double dx = (event.deltaX - g_warp_x) * g_scale, dy = (event.deltaY - g_warp_y) * g_scale;
    g_warp_x = g_warp_y = 0;
    CGEventRef cg = event.CGEvent;
    if (g_pointer_locked && cg) {
        double x = CGEventGetDoubleValueField(cg, kCGEventUnacceleratedPointerMovementX);
        double y = CGEventGetDoubleValueField(cg, kCGEventUnacceleratedPointerMovementY);
        if (x || y) {
            dx = x;
            dy = y;
        }
    }
    g_motion_x += dx;
    g_motion_y += dy;
    ev->dx = (int32_t)g_motion_x;
    ev->dy = (int32_t)g_motion_y;
    g_motion_x -= ev->dx;
    g_motion_y -= ev->dy;
}

static void update_presentation(void)
{
    UiWindow *w = find_by_nswindow(NSApp.keyWindow);
    if (w && (w->fullscreen || w->fullscreen_transition))
        return;
    bool fullscreen = w && (w->config.flags & MIME_WIN_FULLSCREEN) &&
                      (w->config.flags & MIME_WIN_VISIBLE) && !(w->config.flags & MIME_WIN_MINIMIZED);
    if (fullscreen != g_fullscreen_presentation) {
        NSApp.presentationOptions = fullscreen ? NSApplicationPresentationAutoHideDock |
                                                     NSApplicationPresentationAutoHideMenuBar
                                               : NSApplicationPresentationDefault;
        g_fullscreen_presentation = fullscreen;
    }
}

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* converts guest screen pixels with Y down to Cocoa points with Y up. */
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

static uint32_t modifiers_of(NSEvent *event)
{
    NSEventModifierFlags flags = event.modifierFlags;
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

static void emit_mouse(NSView *view, NSEvent *event, uint32_t type, uint32_t button)
{
    g_pointer_in_guest = true;
    NSWindow *window = view.window;
    UiWindow *w = find_by_nswindow(window);
    if (!w)
        return;
    NSPoint loc = event.locationInWindow;
    NSRect frame = window.frame;
    NSPoint screen = CGPointMake(frame.origin.x + loc.x, frame.origin.y + loc.y);
    MimeInputEvent ev = {};
    ev.type = type;
    ev.window_id = w->id;
    screen_point_to_guest(screen, &ev.x, &ev.y);
    ev.button = button;
    ev.modifiers = modifiers_of(event);
    if (g_pointer_locked) {
        ev.window_id = g_pointer_window;
        ev.flags |= 4;
    }
    if (type == MIME_INPUT_MOUSE_MOVE) {
        motion_of(event, &ev);
        if (g_pointer_locked && !ev.dx && !ev.dy)
            return;
    }
    emit(&ev);
}

/* visible rectangle relative to the guest window. */
static MimeRect visible_part(const MimeWinConfig *c)
{
    MimeRect v = c->visible;
    if (v.right <= v.left || v.bottom <= v.top)
        v = (MimeRect){0, 0, c->rect.right - c->rect.left, c->rect.bottom - c->rect.top};
    return v;
}

/* host pixels per surface pixel: a window that does not know the display's dpi draws at a lower one and is stretched. */
static double surface_stretch(const MimeWinConfig *c)
{
    double dpi = c->dpi ? c->dpi : 96.0 * g_scale;
    return c->surface_dpi && dpi > c->surface_dpi ? dpi / c->surface_dpi : 1.0;
}

/* host window bounds: visible rectangle plus resize border. */
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

/* ---------------------------------------------------------------- windows 11 chrome. */

static constexpr double CHROME_MARGIN = 64.0;

static void animate(CALayer *layer, NSString *key, double from, double to, double duration, bool hold)
{
    CABasicAnimation *a = [CABasicAnimation animationWithKeyPath:key];
    a.fromValue = @(from);
    a.toValue = @(to);
    a.duration = duration;
    a.timingFunction = [CAMediaTimingFunction functionWithName:hold ? kCAMediaTimingFunctionEaseIn
                                                                    : kCAMediaTimingFunctionEaseOut];
    if (hold) {
        a.fillMode = kCAFillModeForwards;
        a.removedOnCompletion = NO;
    }
    [layer addAnimation:a forKey:key];
}

/* framed windows open and close with the dwm's zoom and fade. */
static bool animated(const UiWindow *w)
{
    uint32_t f = w->config.flags;
    return (f & MIME_WIN_SHADOW) && !(f & (MIME_WIN_TOOL | MIME_WIN_FULLSCREEN)) && w->config.border_color &&
           !w->fullscreen && !w->fullscreen_transition;
}

static void animate_window(UiWindow *w, bool show)
{
    CALayer *layers[] = {w->content, w->chrome_shadow, w->chrome_outline};
    w->anim_gen++;
    for (unsigned i = 0; i < MIME_ARRAY_COUNT(layers); i++) {
        if (!layers[i])
            continue;
        [layers[i] removeAllAnimations];
        if (show) {
            animate(layers[i], @"opacity", 0, 1, 0.2, false);
            animate(layers[i], @"transform.scale", 0.94, 1, 0.2, false);
        } else {
            animate(layers[i], @"opacity", 1, 0, 0.13, true);
            animate(layers[i], @"transform.scale", 1, 0.94, 0.13, true);
        }
    }
}

static void stop_animations(UiWindow *w)
{
    CALayer *layers[] = {w->content, w->chrome_shadow, w->chrome_outline};
    w->anim_gen++;
    for (unsigned i = 0; i < MIME_ARRAY_COUNT(layers); i++)
        [layers[i] removeAllAnimations];
}

static void chrome_detach(UiWindow *w)
{
    if (!w->chrome)
        return;
    if (w->chrome.parentWindow)
        [w->window removeChildWindow:w->chrome];
    [w->chrome orderOut:nil];
}

static void chrome_create(UiWindow *w)
{
    NSRect rect = NSMakeRect(0, 0, 100, 100);
    NSWindow *window = [[NSWindow alloc] initWithContentRect:rect styleMask:NSWindowStyleMaskBorderless
                                                     backing:NSBackingStoreBuffered defer:YES];
    window.releasedWhenClosed = NO;
    window.opaque = NO;
    window.backgroundColor = NSColor.clearColor;
    window.hasShadow = NO;
    window.ignoresMouseEvents = YES;
    window.animationBehavior = NSWindowAnimationBehaviorNone;
    CALayer *root = [[CALayer alloc] init];
    NSView *view = [[NSView alloc] initWithFrame:rect];
    view.layer = root;
    view.wantsLayer = YES;
    w->chrome_shadow = [[CALayer alloc] init];
    w->chrome_outline = [[CALayer alloc] init];
    w->chrome_outline.contentsScale = g_scale;
    [root addSublayer:w->chrome_shadow];
    [root addSublayer:w->chrome_outline];
    window.contentView = view;
    w->chrome = window;
}

/* draws the Windows DWM shadow and outline; the native macOS shadow and rim look different. */
static void chrome_update(UiWindow *w)
{
    const MimeWinConfig *c = &w->config;
    bool on = w->shown && (c->flags & MIME_WIN_SHADOW) && !(c->flags & (MIME_WIN_FULLSCREEN | MIME_WIN_MINIMIZED)) &&
              !w->fullscreen && !w->fullscreen_transition && w->window.visible;
    if (!on) {
        chrome_detach(w);
        return;
    }
    if (!w->chrome)
        chrome_create(w);
    MimeRect vis = visible_part(c), host = host_part(c);
    NSRect frame = w->window.frame;
    double x = (vis.left - host.left) / g_scale, top = (vis.top - host.top) / g_scale;
    double width = (vis.right - vis.left) / g_scale, height = (vis.bottom - vis.top) / g_scale;
    NSRect inner = CGRectMake(CHROME_MARGIN + x, CHROME_MARGIN + frame.size.height - top - height, width, height);
    double radius = c->corner_radius / g_scale, px = 1.0 / g_scale;
    bool active = (c->flags & MIME_WIN_ACTIVE) != 0, tool = (c->flags & MIME_WIN_TOOL) != 0;

    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    w->chrome_shadow.frame = inner;
    CGPathRef path = CGPathCreateWithRoundedRect(CGRectMake(0, 0, width, height), radius, radius, nullptr);
    w->chrome_shadow.shadowPath = path;
    CGPathRelease(path);
    CGColorRef black = CGColorCreateSRGB(0, 0, 0, 1);
    w->chrome_shadow.shadowColor = black;
    CGColorRelease(black);
    double drop = tool ? 3 : active ? 7 : 4;
    w->chrome_shadow.shadowOpacity = tool ? 0.28f : active ? 0.42f : 0.24f;
    w->chrome_shadow.shadowRadius = tool ? 8 : active ? 18 : 11;
    w->chrome_shadow.shadowOffset = CGSizeMake(0, -drop);
    w->chrome_outline.frame = CGRectInset(inner, -px, -px);
    w->chrome_outline.cornerRadius = radius ? radius + px : 0;
    w->chrome_outline.borderWidth = c->border_color ? px : 0;
    CGColorRef line = CGColorCreateSRGB(0, 0, 0, active ? 0.88 : 0.6);
    w->chrome_outline.borderColor = line;
    CGColorRelease(line);
    [CATransaction commit];

    NSRect outer = CGRectInset(frame, -CHROME_MARGIN, -CHROME_MARGIN);
    if (!CGRectEqualToRect(w->chrome.frame, outer))
        [w->chrome setFrame:outer display:NO];
    w->chrome.level = w->window.level;
    if (w->chrome.parentWindow != w->window)
        [w->window addChildWindow:w->chrome ordered:NSWindowBelow];
}

static void retire(NSWindow *window, NSWindow *chrome)
{
    [chrome orderOut:nil];
    [window orderOut:nil];
    [window close];
}

static void hide_later(uint32_t window_id, uint32_t gen)
{
    os_unfair_lock_lock(&g_windows_lock);
    UiWindow *w = find_window(window_id);
    os_unfair_lock_unlock(&g_windows_lock);
    if (w && w->window && !w->shown && w->anim_gen == gen) {
        [w->window orderOut:nil];
        stop_animations(w);
        chrome_detach(w);
    }
}

/* true when a window relative guest point lies in a caption drag area. */
static bool in_drag_area(UiWindow *w, NSEvent *event)
{
    NSPoint loc = event.locationInWindow;
    NSRect frame = w->window.frame;
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

static int32_t wheel_units(double *remainder, double delta, int32_t quantum)
{
    double total = *remainder + delta;
    if (!isfinite(total) || total > INT32_MAX || total < INT32_MIN)
        return 0;
    int32_t units = (int32_t)(total / quantum) * quantum;
    *remainder = total - units;
    return units;
}

static void emit_wheel(MimeInputEvent *event, int32_t delta)
{
    while (delta) {
        /* windows carries the wheel delta in a signed 16 bit word. */
        event->wheel = delta > 32767 ? 32767 : delta < -32767 ? -32767 : delta;
        emit(event);
        delta -= event->wheel;
    }
}

static MimeInputEvent wheel_event(UiWindow *w, NSEvent *event)
{
    NSPoint loc = event.locationInWindow;
    NSRect frame = w->window.frame;
    MimeInputEvent ev = {.type = MIME_INPUT_MOUSE_WHEEL, .window_id = w->id,
                         .modifiers = modifiers_of(event)};
    screen_point_to_guest(CGPointMake(frame.origin.x + loc.x, frame.origin.y + loc.y), &ev.x, &ev.y);
    /* the parked cursor says nothing of where the guest's one is. */
    if (g_pointer_locked) {
        ev.window_id = g_pointer_window;
        ev.flags |= 4;
    }
    return ev;
}

typedef struct KeyEntry {
    uint16_t mac;
    uint8_t vk;
    uint16_t scan;
} KeyEntry;

static const KeyEntry g_keymap[] = {
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

/*
 * the character a key types in the current macOS layout with shift alone, or 0 for keys without text. the
 * option, command and control modifiers are left out, so the result is the plain character windows sees.
 */
static uint32_t layout_char(uint16_t mac, bool shift)
{
    TISInputSourceRef source = TISCopyCurrentKeyboardLayoutInputSource();
    if (!source)
        return 0;
    CFDataRef data = TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData);
    uint32_t ch = 0;
    if (data) {
        UInt32 dead = 0;
        UniChar chars[4];
        UniCharCount n = 0;
        if (UCKeyTranslate((const UCKeyboardLayout *)CFDataGetBytePtr(data), mac, kUCKeyActionDown,
                           shift ? shiftKey >> 8 : 0, LMGetKbdType(), kUCKeyTranslateNoDeadKeysMask, &dead, 4, &n,
                           chars) == noErr &&
            n == 1)
            ch = chars[0];
    }
    CFRelease(source);
    return ch;
}

/*
 * returns the letter typed by a key in the current macOS layout, or 0 for other keys. Windows maps letter
 * keys to their active-layout virtual keys, so Ctrl+Z follows Z on AZERTY and QWERTZ too.
 */
static uint32_t layout_letter(uint16_t mac)
{
    uint32_t ch = layout_char(mac, false);
    if (ch >= 'a' && ch <= 'z')
        return ch - 32;
    return (ch >= 'A' && ch <= 'Z') ? ch : 0;
}

/*
 * the character the windows keyboard driver makes of Ctrl and a key: the C0 code of a letter, and the
 * control codes of the punctuation keys that have one. other keys keep their character.
 */
static uint32_t control_char(uint32_t ch)
{
    if (ch >= 'a' && ch <= 'z')
        return ch - 'a' + 1;
    if (ch >= 'A' && ch <= 'Z')
        return ch - 'A' + 1;
    switch (ch) {
    case '2':
    case '@': return 0x00;
    case '3':
    case '[': return 0x1B;
    case '4':
    case '\\': return 0x1C;
    case '5':
    case ']': return 0x1D;
    case '6':
    case '^': return 0x1E;
    case '7':
    case '-':
    case '_': return 0x1F;
    case '8': return 0x7F;
    case 0x0D: return 0x0A;
    case 0x09: return 0x09;
    case ' ': return 0x20;
    }
    return ch;
}

static const KeyEntry *map_key(uint16_t mac)
{
    for (size_t i = 0; i < MIME_ARRAY_COUNT(g_keymap); i++)
        if (g_keymap[i].mac == mac)
            return &g_keymap[i];
    return nullptr;
}

static void emit_key(NSView *view, NSEvent *event, uint32_t type)
{
    UiWindow *w = find_by_nswindow(view.window);
    if (!w)
        return;
    uint16_t code = event.keyCode;
    const KeyEntry *k = map_key(code);
    if (!k)
        return;
    MimeInputEvent ev = {};
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
    if (type == MIME_INPUT_KEY_DOWN && event.type == NSEventTypeKeyDown && event.ARepeat)
        ev.flags = 2;
    emit(&ev);
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

static void emit_fullscreen(UiWindow *w, bool entering)
{
    NSRect frame = w->window.frame;
    MimeInputEvent ev = {.type = MIME_INPUT_WINDOW_FULLSCREEN, .window_id = w->id, .flags = entering};
    screen_point_to_guest(CGPointMake(frame.origin.x, frame.origin.y + frame.size.height), &ev.x, &ev.y);
    ev.width = (int32_t)(frame.size.width * g_scale + 0.5);
    ev.height = (int32_t)(frame.size.height * g_scale + 0.5);
    emit(&ev);
}

static void exit_fullscreen(uint32_t window_id)
{
    UiWindow *w = find_window(window_id);
    if (w && w->fullscreen && !w->fullscreen_transition && w->fullscreen_config_pending)
        [w->window toggleFullScreen:nil];
}

static void window_focus(NSWindow *window, bool gained)
{
    UiWindow *w = find_by_nswindow(window);
    if (!w || (gained && (w->config.flags & MIME_WIN_MINIMIZED)))
        return;
    MimeInputEvent ev = {};
    ev.type = MIME_INPUT_WINDOW_FOCUS;
    ev.window_id = w->id;
    ev.x = gained ? 1 : 0;
    emit(&ev);
}

/* the guest's TranslateMessage makes the characters of these keys, the control ones, itself. */
static bool guest_translates(uint16_t code)
{
    const KeyEntry *k = map_key(code);
    return k && (k->vk == 0x08 || k->vk == 0x09 || k->vk == 0x0D || k->vk == 0x1B);
}

static void emit_char(UiWindow *w, uint32_t c)
{
    MimeInputEvent ev = {.type = MIME_INPUT_CHAR, .window_id = w->id, .ch = c};
    emit(&ev);
}

@implementation MimeHostView {
    NSString *_marked; /* a dead key or an input method's composition, waiting for the next key. */
}

- (BOOL)isFlipped
{
    return YES;
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent *)event
{
    return YES;
}

- (void)cursorUpdate:(NSEvent *)event
{
    g_pointer_in_guest = true;
    cursor_main(g_cursor);
}

- (void)mouseEntered:(NSEvent *)event
{
    [self cursorUpdate:event];
}

- (void)mouseDown:(NSEvent *)event
{
    UiWindow *w = find_by_nswindow(self.window);
    if (w && !g_pointer_locked && !(w->config.flags & (MIME_WIN_MAXIMIZED | MIME_WIN_FULLSCREEN)) &&
        in_drag_area(w, event) && event.clickCount == 1) {
        /* tell the guest about the click so it activates, then let macOS move the window. */
        emit_mouse(self, event, MIME_INPUT_MOUSE_DOWN, MIME_MOUSE_LEFT);
        emit_mouse(self, event, MIME_INPUT_MOUSE_UP, MIME_MOUSE_LEFT);
        [w->window performWindowDragWithEvent:event];
        return;
    }
    emit_mouse(self, event, MIME_INPUT_MOUSE_DOWN, MIME_MOUSE_LEFT);
}

- (void)mouseUp:(NSEvent *)event
{
    emit_mouse(self, event, MIME_INPUT_MOUSE_UP, MIME_MOUSE_LEFT);
}

- (void)rightMouseDown:(NSEvent *)event
{
    emit_mouse(self, event, MIME_INPUT_MOUSE_DOWN, MIME_MOUSE_RIGHT);
}

- (void)rightMouseUp:(NSEvent *)event
{
    emit_mouse(self, event, MIME_INPUT_MOUSE_UP, MIME_MOUSE_RIGHT);
}

- (void)otherMouseDown:(NSEvent *)event
{
    emit_mouse(self, event, MIME_INPUT_MOUSE_DOWN, MIME_MOUSE_MIDDLE);
}

- (void)otherMouseUp:(NSEvent *)event
{
    emit_mouse(self, event, MIME_INPUT_MOUSE_UP, MIME_MOUSE_MIDDLE);
}

- (void)mouseDragged:(NSEvent *)event
{
    emit_mouse(self, event, MIME_INPUT_MOUSE_MOVE, 0);
}

- (void)rightMouseDragged:(NSEvent *)event
{
    emit_mouse(self, event, MIME_INPUT_MOUSE_MOVE, 0);
}

- (void)otherMouseDragged:(NSEvent *)event
{
    emit_mouse(self, event, MIME_INPUT_MOUSE_MOVE, 0);
}

/*
 * macOS sends mouseMoved: to the key window regardless of pointer location. route it to the guest window
 * under the pointer so unfocused popup menus receive hover events, as on Windows.
 */
- (void)mouseMoved:(NSEvent *)event
{
    if (g_pointer_locked) {
        emit_mouse(self, event, MIME_INPUT_MOUSE_MOVE, 0);
        return;
    }
    /* an event without a window carries its location in screen coordinates. */
    NSWindow *window = event.window;
    NSPoint loc = event.locationInWindow;
    NSPoint screen = loc;
    if (window) {
        NSRect frame = window.frame;
        screen = CGPointMake(frame.origin.x + loc.x, frame.origin.y + loc.y);
    }
    NSInteger number = [NSWindow windowNumberAtPoint:screen belowWindowWithWindowNumber:0];
    UiWindow *under = nullptr;
    for (int i = 0; i < MAX_WINDOWS && !under; i++)
        if (g_windows[i].used && g_windows[i].window && g_windows[i].window.windowNumber == number)
            under = &g_windows[i];
    if (!under)
        return;
    g_pointer_in_guest = true;
    MimeInputEvent ev = {};
    ev.type = MIME_INPUT_MOUSE_MOVE;
    ev.window_id = under->id;
    screen_point_to_guest(screen, &ev.x, &ev.y);
    ev.modifiers = modifiers_of(event);
    motion_of(event, &ev);
    emit(&ev);
}

- (void)mouseExited:(NSEvent *)event
{
    g_pointer_in_guest = false;
    restore_host_cursor();
    UiWindow *w = find_by_nswindow(self.window);
    if (!w)
        return;
    MimeInputEvent ev = {};
    ev.type = MIME_INPUT_MOUSE_LEAVE;
    ev.window_id = w->id;
    emit(&ev);
}

- (void)scrollWheel:(NSEvent *)event
{
    UiWindow *w = find_by_nswindow(self.window);
    if (!w)
        return;
    BOOL precise = event.hasPreciseScrollingDeltas;
    NSEventPhase phase = event.phase;
    NSEventPhase momentum = event.momentumPhase;
    MimeInputEvent ev = wheel_event(w, event);
    if ((phase & (NSEventPhaseBegan | NSEventPhaseCancelled)) ||
        (momentum & NSEventPhaseCancelled) || w->scroll_modifiers != ev.modifiers ||
        w->scroll_precise != (bool)precise)
        w->scroll_x = w->scroll_y = 0;
    w->scroll_modifiers = ev.modifiers;
    w->scroll_precise = precise;
    if ((phase | momentum) & NSEventPhaseCancelled)
        return;
    double factor = precise ? 120.0 / 40.0 : 120.0;
    emit_wheel(&ev, wheel_units(&w->scroll_y, event.scrollingDeltaY * factor, 1));
    ev.flags |= 1;
    emit_wheel(&ev, wheel_units(&w->scroll_x, -event.scrollingDeltaX * factor, 1));
}

- (void)magnifyWithEvent:(NSEvent *)event
{
    UiWindow *w = find_by_nswindow(self.window);
    if (!w)
        return;
    NSEventPhase phase = event.phase;
    if (phase & (NSEventPhaseBegan | NSEventPhaseCancelled))
        w->pinch_remainder = 0;
    if (phase & NSEventPhaseCancelled)
        return;
    double change = event.magnification;
    if (!isfinite(change) || change <= -1)
        return;
    /* one wheel notch per 20 percent scale change keeps legacy zoom handlers usable. */
    int32_t delta = wheel_units(&w->pinch_remainder, log1p(change) * (120.0 / log(1.2)), 120);
    if (phase & NSEventPhaseEnded)
        w->pinch_remainder = 0;
    if (!delta)
        return;
    MimeInputEvent ev = wheel_event(w, event);
    MimeInputEvent key = {.type = MIME_INPUT_KEY_DOWN, .window_id = w->id,
                          .vk = 0xa2, .scancode = 0x1d, .modifiers = ev.modifiers | MIME_MOD_CONTROL};
    bool synthesize = !(ev.modifiers & MIME_MOD_CONTROL);
    if (synthesize)
        emit(&key);
    uint32_t modifiers = ev.modifiers;
    ev.modifiers |= MIME_MOD_CONTROL;
    /* separate notches also work with applications that inspect only the sign. */
    while (delta) {
        ev.wheel = delta > 0 ? 120 : -120;
        emit(&ev);
        delta -= ev.wheel;
    }
    if (synthesize) {
        key.type = MIME_INPUT_KEY_UP;
        key.modifiers = modifiers;
        emit(&key);
    }
}

- (void)keyDown:(NSEvent *)event
{
    emit_key(self, event, MIME_INPUT_KEY_DOWN);
    UiWindow *w = find_by_nswindow(self.window);
    if (!w)
        return;
    NSEventModifierFlags flags = event.modifierFlags;
    bool control = (flags & (NSEventModifierFlagControl | NSEventModifierFlagCommand)) != 0;
    bool alt = (flags & NSEventModifierFlagOption) != 0;
    uint16_t code = event.keyCode;
    if (control != alt) {
        if (guest_translates(code))
            return;
        uint32_t base = layout_char(code, (flags & NSEventModifierFlagShift) != 0);
        /*
         * windows makes a control character of Ctrl and a key, which its controls read as commands, and Alt and a key
         * is a mnemonic, so it carries the plain character rather than the option one.
         */
        uint32_t c = control ? control_char(base) : base;
        if (c)
            emit_char(w, c);
        return;
    }
    /* typed text goes through the input system, which composes dead keys and the text of input methods. */
    [self interpretKeyEvents:@[event]];
}

- (void)insertText:(id)string replacementRange:(NSRange)replacement
{
    _marked = nil;
    UiWindow *w = find_by_nswindow(self.window);
    NSString *text = [string isKindOfClass:NSAttributedString.class] ? [string string] : string;
    for (NSUInteger i = 0; w && i < text.length; i++) {
        unichar c = [text characterAtIndex:i];
        /* function keys come as private use characters; they only make key events. */
        if (c < 0xF700 || c > 0xF8FF)
            emit_char(w, c);
    }
}

/* the keys of commands, like a backspace or a return, made their key events and the guest's own characters. */
- (void)doCommandBySelector:(SEL)selector
{
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selected replacementRange:(NSRange)replacement
{
    _marked = [[string isKindOfClass:NSAttributedString.class] ? [string string] : string copy];
}

- (void)unmarkText
{
    _marked = nil;
}

- (BOOL)hasMarkedText
{
    return _marked.length != 0;
}

- (NSRange)markedRange
{
    return _marked.length ? NSMakeRange(0, _marked.length) : NSMakeRange(NSNotFound, 0);
}

- (NSRange)selectedRange
{
    return NSMakeRange(_marked.length, 0);
}

- (NSArray<NSAttributedStringKey> *)validAttributesForMarkedText
{
    return @[];
}

- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actual
{
    return nil;
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point
{
    return NSNotFound;
}

/* an input method's candidates open at the bottom left of the window, the guest does not say where its caret is. */
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actual
{
    NSRect frame = self.window.frame;
    return NSMakeRect(frame.origin.x, frame.origin.y, 0, 0);
}

- (void)keyUp:(NSEvent *)event
{
    emit_key(self, event, MIME_INPUT_KEY_UP);
}

- (void)flagsChanged:(NSEvent *)event
{
    NSEventModifierFlags mask = 0;
    switch (event.keyCode) {
    case kVK_Shift:
    case kVK_RightShift: mask = NSEventModifierFlagShift; break;
    case kVK_Control:
    case kVK_RightControl: mask = NSEventModifierFlagControl; break;
    case kVK_Option:
    case kVK_RightOption: mask = NSEventModifierFlagOption; break;
    case kVK_Command:
    case kVK_RightCommand: mask = NSEventModifierFlagCommand; break;
    case kVK_CapsLock: mask = NSEventModifierFlagCapsLock; break;
    default: return;
    }
    emit_key(self, event, (event.modifierFlags & mask) ? MIME_INPUT_KEY_DOWN : MIME_INPUT_KEY_UP);
}

@end

@implementation MimeHostWindow

- (BOOL)canBecomeKeyWindow
{
    return YES;
}

- (BOOL)canBecomeMainWindow
{
    return YES;
}

- (void)zoom:(id)sender
{
    UiWindow *w = find_by_nswindow(self);
    if (w && (w->config.flags & MIME_WIN_MAXIMIZABLE) && !w->fullscreen)
        request_window_state(w, (w->config.flags & MIME_WIN_MAXIMIZED) ? 9 : 3);
}

- (void)performMiniaturize:(id)sender
{
    UiWindow *w = find_by_nswindow(self);
    if (w && (w->config.flags & MIME_WIN_MINIMIZABLE))
        request_window_state(w, 6);
}

- (void)toggleFullScreen:(id)sender
{
    UiWindow *w = find_by_nswindow(self);
    if (!w || w->fullscreen_transition || (!(w->config.flags & MIME_WIN_RESIZABLE) && !w->fullscreen))
        return;
    self.styleMask |= NSWindowStyleMaskResizable;
    [super toggleFullScreen:sender];
}

- (void)toggleFullScreenUnchecked
{
    [super toggleFullScreen:nil];
}

@end

@implementation MimeHostWindowDelegate

- (void)windowDidMove:(NSNotification *)notification
{
    if (g_suppress_move)
        return;
    NSWindow *window = notification.object;
    UiWindow *w = find_by_nswindow(window);
    pointer_park(w);
    if (!w || w->fullscreen_transition || w->fullscreen)
        return;
    NSRect frame = window.frame;
    MimeRect host = host_part(&w->config);
    MimeInputEvent ev = {};
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

- (void)windowDidBecomeKey:(NSNotification *)notification
{
    window_focus(notification.object, true);
    update_presentation();
    pointer_apply();
}

- (void)windowDidResignKey:(NSNotification *)notification
{
    [[notification.object contentView].inputContext discardMarkedText];
    window_focus(notification.object, false);
    update_presentation();
    pointer_apply();
}

- (BOOL)windowShouldClose:(NSWindow *)window
{
    UiWindow *w = find_by_nswindow(window);
    if (w) {
        MimeInputEvent ev = {};
        ev.type = MIME_INPUT_WINDOW_CLOSE;
        ev.window_id = w->id;
        emit(&ev);
    }
    return NO;
}

- (void)windowDidMiniaturize:(NSNotification *)notification
{
    restore_host_cursor();
    UiWindow *w = find_by_nswindow(notification.object);
    if (w && !(w->config.flags & MIME_WIN_MINIMIZED))
        request_window_state(w, 6);
}

- (void)windowDidDeminiaturize:(NSNotification *)notification
{
    UiWindow *w = find_by_nswindow(notification.object);
    if (w && ((w->config.flags & MIME_WIN_MINIMIZED) || (w->state_pending && w->state_command == 6)))
        request_window_state(w, 9);
}

- (void)windowWillEnterFullScreen:(NSNotification *)notification
{
    UiWindow *w = find_by_nswindow(notification.object);
    if (w) {
        w->fullscreen_transition = true;
        chrome_detach(w);
    }
}

- (void)windowWillExitFullScreen:(NSNotification *)notification
{
    [self windowWillEnterFullScreen:notification];
}

- (void)windowDidEnterFullScreen:(NSNotification *)notification
{
    UiWindow *w = find_by_nswindow(notification.object);
    if (!w)
        return;
    w->fullscreen_transition = false;
    w->fullscreen = true;
    /* a program already covering the screen is not configured again. */
    if (w->config.flags & MIME_WIN_FULLSCREEN)
        w->fullscreen_synced = true;
    if (w->fullscreen_config_pending) {
        uint32_t window_id = w->id;
        dispatch_async(dispatch_get_main_queue(), ^{
            exit_fullscreen(window_id);
        });
        return;
    }
    emit_fullscreen(w, true);
}

- (void)windowDidExitFullScreen:(NSNotification *)notification
{
    UiWindow *w = find_by_nswindow(notification.object);
    if (!w)
        return;
    w->fullscreen_transition = false;
    w->fullscreen = false;
    w->fullscreen_synced = false;
    chrome_update(w);
    if (!w->fullscreen_config_pending && (w->config.flags & MIME_WIN_FULLSCREEN))
        w->space_declined = true;
    emit_fullscreen(w, false);
    if (w->fullscreen_config_pending) {
        w->fullscreen_config_pending = false;
        MimeWinConfig config = w->fullscreen_config;
        mime_ui_window_config(&config);
    }
}

- (void)windowDidFailToEnterFullScreen:(NSWindow *)window
{
    UiWindow *w = find_by_nswindow(window);
    if (!w)
        return;
    w->fullscreen_transition = false;
    if (!w->fullscreen) {
        w->space_declined = true;
        window.styleMask = (w->config.flags & MIME_WIN_MINIMIZABLE) ? NSWindowStyleMaskMiniaturizable
                                                                    : NSWindowStyleMaskBorderless;
        if (w->fullscreen_config_pending) {
            w->fullscreen_config_pending = false;
            MimeWinConfig config = w->fullscreen_config;
            mime_ui_window_config(&config);
        }
    }
}

- (void)windowDidFailToExitFullScreen:(NSWindow *)window
{
    [self windowDidFailToEnterFullScreen:window];
}

- (void)appDidBecomeActive:(NSNotification *)notification
{
    app_became_active();
    pointer_apply();
}

- (void)appDidResignActive:(NSNotification *)notification
{
    app_resigned_active();
    pointer_apply();
}

- (void)screensDidChange:(NSNotification *)notification
{
    screens_changed();
}

@end

static void clip_start(void *arg);

@implementation MimeHostApplication

/* AppKit drops key up events while command is held; the guest needs them to release its keys. */
- (void)sendEvent:(NSEvent *)event
{
    NSEventType type = event.type;
    static uint16_t shortcut_key = UINT16_MAX;
    if (type == NSEventTypeKeyDown || type == NSEventTypeKeyUp) {
        uint16_t key = event.keyCode;
        if (type == NSEventTypeKeyUp && key == shortcut_key) {
            shortcut_key = UINT16_MAX;
            return;
        }
        NSEventModifierFlags flags = event.modifierFlags;
        UiWindow *w = find_by_nswindow(self.keyWindow);
        if (w && type == NSEventTypeKeyDown && (flags & NSEventModifierFlagCommand)) {
            if (key == kVK_ANSI_F && (flags & NSEventModifierFlagControl) &&
                ((w->config.flags & MIME_WIN_RESIZABLE) || w->fullscreen)) {
                if (!w->fullscreen_transition)
                    [w->window toggleFullScreen:nil];
                shortcut_key = key;
                return;
            }
            if (key == kVK_ANSI_M && (w->config.flags & MIME_WIN_MINIMIZABLE) &&
                !(flags & (NSEventModifierFlagControl | NSEventModifierFlagOption))) {
                request_window_state(w, 6);
                shortcut_key = key;
                return;
            }
        }
    }
    if (type == NSEventTypeKeyUp && (event.modifierFlags & NSEventModifierFlagCommand)) {
        NSWindow *window = self.keyWindow;
        if (window && find_by_nswindow(window)) {
            [window sendEvent:event];
            return;
        }
    }
    [super sendEvent:event];
}

@end

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
            if ((w->config.flags & MIME_WIN_MINIMIZED) || w->window.miniaturized ||
                (w->state_pending && w->state_command == 6)) {
                request_window_state(w, 9);
            } else if (!g_background) {
                [NSApp activate];
                [w->window makeKeyAndOrderFront:nil];
            }
        }
    }
}

static void update_screen_metrics(void)
{
    NSArray<NSScreen *> *screens = NSScreen.screens;
    if (!screens.count)
        return;
    NSScreen *primary = screens[0];
    NSRect frame = primary.frame;
    g_scale = primary.backingScaleFactor;
    if (g_scale < 1)
        g_scale = 1;
    /*
     * the guest screen starts below the menu bar or camera housing. measure screen and work area once per
     * screen size so Spaces, fullscreen presentation and Dock changes do not move them.
     */
    static double band_w, band_h;
    if (frame.size.width != band_w || frame.size.height != band_h) {
        band_w = frame.size.width;
        band_h = frame.size.height;
        NSRect visible = primary.visibleFrame;
        double band = frame.origin.y + frame.size.height - (visible.origin.y + visible.size.height);
        g_screen_band = band > 0 ? band : 0;
        g_work_points = CGRectMake(visible.origin.x - frame.origin.x, 0, visible.size.width, visible.size.height);
    }
    g_screen_height_pts = frame.size.height - g_screen_band;
}

void mime_ui_init(MimeUiInputSink sink, void *ctx)
{
    g_sink = sink;
    g_sink_ctx = ctx;
    g_colorspace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    g_background = getenv("MIME_BACKGROUND") && *getenv("MIME_BACKGROUND") && strcmp(getenv("MIME_BACKGROUND"), "0");
    [MimeHostApplication sharedApplication];
    /* per-program companion processes provide the dock entries. */
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    [NSApp finishLaunching];
    /*
     * the guest's timers and wakeups are the host's: App Nap and timer coalescing would delay every one of them while
     * the app is not in front, input included. idle system sleep stays allowed.
     */
    NSActivityOptions options = NSActivityUserInitiatedAllowingIdleSystemSleep | NSActivityLatencyCritical;
    g_activity = [NSProcessInfo.processInfo beginActivityWithOptions:options reason:@"a running machine"];
    g_delegate = [[MimeHostWindowDelegate alloc] init];
    NSNotificationCenter *center = NSNotificationCenter.defaultCenter;
    [center addObserver:g_delegate selector:@selector(appDidBecomeActive:)
                   name:NSApplicationDidBecomeActiveNotification object:nil];
    [center addObserver:g_delegate selector:@selector(appDidResignActive:)
                   name:NSApplicationDidResignActiveNotification object:nil];
    [center addObserver:g_delegate selector:@selector(screensDidChange:)
                   name:NSApplicationDidChangeScreenParametersNotification object:nil];
    update_screen_metrics();
    mime_app_init(nullptr, app_action, nullptr);
    clip_start(nullptr);
}

void mime_ui_run(void)
{
    [NSApp run];
}

int mime_ui_dialog_helper(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return -1;
}

int mime_ui_presence_helper(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return -1;
}

/* macOS reports in the terminal only for now. */
void mime_ui_alert(const char *text)
{
    (void)text;
}

/* the guest keeps the US layout on macOS for now. */
bool mime_ui_get_keyboard(MimeKeyboardLayout *out)
{
    (void)out;
    return false;
}

void mime_ui_get_display(MimeDisplayInfo *info)
{
    memset(info, 0, sizeof(*info));
    NSArray<NSScreen *> *screens = NSScreen.screens;
    if (!screens.count) {
        info->width = 1920;
        info->height = 1080;
        info->work_width = 1920;
        info->work_height = 1080;
        info->dpi = 96;
        info->refresh_hz = 60;
        return;
    }
    NSScreen *primary = screens[0];
    NSRect frame = primary.frame;
    update_screen_metrics();
    info->width = (uint32_t)(frame.size.width * g_scale);
    info->height = (uint32_t)(g_screen_height_pts * g_scale);
    info->work_x = (uint32_t)(g_work_points.origin.x * g_scale);
    info->work_y = 0;
    info->work_width = (uint32_t)(g_work_points.size.width * g_scale);
    info->work_height = (uint32_t)(g_work_points.size.height * g_scale);
    info->dpi = (uint32_t)(96.0 * g_scale + 0.5);
    info->flags |= MIME_DISPLAY_STRETCH;
    NSInteger fps = primary.maximumFramesPerSecond;
    info->refresh_hz = fps > 0 ? (uint32_t)fps : 60;
    g_display = *info;
}

/* windows are moved by macOS itself from the caption areas, and sized by the guest. */
uint64_t mime_ui_window_movesize(uint32_t window, uint32_t edge)
{
    return MIME_HC_STATUS_UNSUPPORTED;
}

/* the menu bar has no place for the icons of programs here, and notifications are not sent. */
uint64_t mime_ui_tray(const MimeTrayCommand *command)
{
    return MIME_HC_STATUS_UNSUPPORTED;
}

void mime_ui_desktop_start(const MimePaths *paths)
{
}

/* the screen the display describes is the only monitor here. */
uint32_t mime_ui_get_monitors(MimeMonitor *monitors, uint32_t capacity)
{
    MimeDisplayInfo info;
    mime_ui_get_display(&info);
    if (!capacity)
        return 0;
    memset(monitors, 0, sizeof(*monitors));
    monitors->flags = MIME_MONITOR_PRIMARY;
    monitors->dpi = info.dpi;
    monitors->refresh_hz = info.refresh_hz;
    monitors->rect = (MimeRect){0, 0, (int32_t)info.width, (int32_t)info.height};
    monitors->work = (MimeRect){(int32_t)info.work_x, (int32_t)info.work_y, (int32_t)(info.work_x + info.work_width),
                                (int32_t)(info.work_y + info.work_height)};
    return 1;
}

static void on_main(dispatch_block_t block)
{
    if (pthread_main_np())
        block();
    else
        dispatch_async(dispatch_get_main_queue(), block);
}

static void sync_main(dispatch_block_t block)
{
    if (pthread_main_np())
        block();
    else
        dispatch_sync(dispatch_get_main_queue(), block);
}

void mime_ui_stop(void)
{
    on_main(^{
        restore_host_cursor();
        mime_app_shutdown();
        [NSApp terminate:nil];
    });
}

static void create_main(uint32_t id_)
{
    os_unfair_lock_lock(&g_windows_lock);
    UiWindow *w = find_window(id_);
    os_unfair_lock_unlock(&g_windows_lock);
    if (!w || w->window)
        return;

    NSRect rect = NSMakeRect(0, 0, 100, 100);
    MimeHostWindow *window = [[MimeHostWindow alloc] initWithContentRect:rect styleMask:NSWindowStyleMaskBorderless
                                                                 backing:NSBackingStoreBuffered defer:YES];
    window.releasedWhenClosed = NO;
    window.opaque = NO;
    window.backgroundColor = NSColor.clearColor;
    window.hasShadow = NO;
    window.animationBehavior = NSWindowAnimationBehaviorNone;
    window.acceptsMouseMovedEvents = YES;
    window.delegate = g_delegate;

    MimeHostView *view = [[MimeHostView alloc] initWithFrame:rect];
    view.wantsLayer = YES;
    CALayer *content = [[CALayer alloc] init];
    content.contentsScale = g_scale;
    content.contentsGravity = kCAGravityTopLeft;
    content.magnificationFilter = kCAFilterNearest;
    content.masksToBounds = YES;
    [view.layer addSublayer:content];
    window.contentView = view;
    [window makeFirstResponder:view];

    const NSTrackingAreaOptions tracking[] = {
        NSTrackingMouseMoved | NSTrackingActiveAlways | NSTrackingInVisibleRect | NSTrackingMouseEnteredAndExited,
        NSTrackingCursorUpdate | NSTrackingActiveInActiveApp | NSTrackingInVisibleRect,
    };
    for (unsigned i = 0; i < MIME_ARRAY_COUNT(tracking); i++) {
        NSTrackingArea *area = [[NSTrackingArea alloc] initWithRect:rect options:tracking[i] owner:view userInfo:nil];
        [view addTrackingArea:area];
    }

    w->window = window;
    w->view = view;
    w->content = content;
}

void mime_ui_window_create(uint32_t id_)
{
    os_unfair_lock_lock(&g_windows_lock);
    if (!find_window(id_)) {
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (!g_windows[i].used) {
                g_windows[i] = (UiWindow){.used = true, .id = id_, .lock = OS_UNFAIR_LOCK_INIT};
                break;
            }
        }
    }
    os_unfair_lock_unlock(&g_windows_lock);
    on_main(^{
        create_main(id_);
    });
}

static void destroy_main(uint32_t id_)
{
    os_unfair_lock_lock(&g_windows_lock);
    UiWindow *w = find_window(id_);
    if (!w) {
        os_unfair_lock_unlock(&g_windows_lock);
        return;
    }
    os_unfair_lock_unlock(&g_windows_lock);
    /* a vulkan presenter lets go of the window's layer before it goes. */
    mime_vk_window_lost(id_);
    if (id_ == g_pointer_window) {
        g_pointer_mode = MIME_POINTER_FREE;
        pointer_apply();
    }
    restore_host_cursor();
    mime_app_window_remove(id_);
    g_pointer_in_guest = false;
    if (w->window) {
        NSWindow *window = w->window, *chrome = w->chrome;
        window.delegate = nil;
        if (w->shown && animated(w) && window.visible) {
            window.ignoresMouseEvents = YES;
            animate_window(w, false);
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(130 * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
                retire(window, chrome);
            });
        } else {
            retire(window, chrome);
        }
    }
    os_unfair_lock_lock(&g_windows_lock);
    os_unfair_lock_lock(&w->lock);
    if (w->surface)
        CFRelease(w->surface);
    w->surface = nullptr;
    os_unfair_lock_unlock(&w->lock);
    w->window = nil;
    w->view = nil;
    w->content = nil;
    w->chrome = nil;
    w->chrome_shadow = w->chrome_outline = nil;
    w->used = false;
    os_unfair_lock_unlock(&g_windows_lock);
    update_presentation();
}

void mime_ui_window_destroy(uint32_t id_)
{
    on_main(^{
        destroy_main(id_);
    });
}

static void dialogs_process_exit(uint32_t process_id);

void mime_ui_process_exit(uint32_t process_id)
{
    on_main(^{
        mime_app_process_exit(process_id);
        dialogs_process_exit(process_id);
    });
}

void mime_ui_window_icon(uint32_t id_, const void *pixels, uint32_t width, uint32_t height, uint32_t stride)
{
    if (width > 1024 || height > 1024 || stride < width * 4 || (!pixels && width && height))
        return;
    NSMutableData *icon = [[NSMutableData alloc] initWithLength:(size_t)width * height * 4];
    for (uint32_t y = 0; y < height; y++)
        memcpy((uint8_t *)icon.mutableBytes + (size_t)y * width * 4, (const uint8_t *)pixels + (size_t)y * stride,
               (size_t)width * 4);
    on_main(^{
        if (find_window(id_))
            mime_app_window_icon(id_, width && height ? icon.bytes : nullptr, width, height, width * 4);
    });
}

static void present_main(uint32_t id_);
static void metal_relayout(uint32_t window_id);

static void config_main(const MimeWinConfig *cfg)
{
    os_unfair_lock_lock(&g_windows_lock);
    UiWindow *w = find_window(cfg->window_id);
    os_unfair_lock_unlock(&g_windows_lock);
    if (!w || !w->window)
        return;
    if (w->state_pending) {
        if (cfg->state_request != w->state_request)
            return;
        w->state_pending = false;
    }
    char path[2048];
    mime_utf16_to_utf8(path, sizeof(path), cfg->image_path, MIME_MIN(cfg->image_chars, 512u));
    mime_app_window_set(w->id, cfg->process_id, path,
                        (cfg->flags & (MIME_WIN_APPWINDOW | MIME_WIN_VISIBLE)) ==
                            (MIME_WIN_APPWINDOW | MIME_WIN_VISIBLE));
    char title[1024];
    mime_utf16_to_utf8(title, sizeof(title), cfg->title, MIME_MIN(cfg->title_chars, 256u));
    NSString *str = [NSString stringWithUTF8String:title];
    w->window.title = str;
    w->window.miniwindowTitle = str;
    uint32_t state = MIME_WIN_VISIBLE | MIME_WIN_MINIMIZED | MIME_WIN_MAXIMIZED;
    if (w->fullscreen_transition && !w->fullscreen && !(cfg->flags & MIME_WIN_FULLSCREEN) &&
        (w->fullscreen_config_pending || ((cfg->flags ^ w->config.flags) & state))) {
        w->fullscreen_config = *cfg;
        w->fullscreen_config_pending = true;
        return;
    }
    if (w->fullscreen && w->fullscreen_synced && !(cfg->flags & MIME_WIN_FULLSCREEN)) {
        w->fullscreen_config = *cfg;
        w->fullscreen_config_pending = true;
        if (!w->fullscreen_transition)
            [w->window toggleFullScreen:nil];
        return;
    }
    if (w->fullscreen && (cfg->flags & MIME_WIN_FULLSCREEN))
        w->fullscreen_synced = true;
    if (cfg->flags & MIME_WIN_MINIMIZED) {
        w->config = *cfg;
        w->configured = true;
        w->suppress_state = true;
        if (!(w->window.styleMask & NSWindowStyleMaskMiniaturizable))
            w->window.styleMask |= NSWindowStyleMaskMiniaturizable;
        if ((cfg->flags & MIME_WIN_VISIBLE) && !w->window.miniaturized)
            [w->window miniaturize:nil];
        if (!(cfg->flags & MIME_WIN_VISIBLE))
            [w->window orderOut:nil];
        w->suppress_state = false;
        w->shown = (cfg->flags & MIME_WIN_VISIBLE) != 0;
        chrome_update(w);
        update_presentation();
        return;
    }
    bool cropping_changed = memcmp(&w->config.visible, &cfg->visible, sizeof(cfg->visible)) != 0 ||
                            memcmp(&w->config.input, &cfg->input, sizeof(cfg->input)) != 0 ||
                            w->config.surface_dpi != cfg->surface_dpi || w->config.dpi != cfg->dpi ||
                            w->config.rect.right - w->config.rect.left != cfg->rect.right - cfg->rect.left ||
                            w->config.rect.bottom - w->config.rect.top != cfg->rect.bottom - cfg->rect.top;
    w->config = *cfg;
    w->configured = true;

    /*
     * the compositor draws rounded corners and the 1-pixel border over guest pixels. leave the resize border
     * slightly opaque because macOS passes clicks through fully transparent pixels.
     */
    MimeRect vis = visible_part(cfg), host = host_part(cfg);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    CALayer *root = w->view.layer;
    bool border = memcmp(&vis, &host, sizeof(vis)) != 0;
    CGColorRef band = CGColorCreateSRGB(0, 0, 0, border ? 2.0 / 255.0 : 0);
    root.backgroundColor = band;
    CGColorRelease(band);
    w->content.frame = CGRectMake((vis.left - host.left) / g_scale, (vis.top - host.top) / g_scale,
                                  (vis.right - vis.left) / g_scale, (vis.bottom - vis.top) / g_scale);
    w->content.cornerRadius = cfg->corner_radius / g_scale;
    if (cfg->border_color) {
        uint32_t c = cfg->border_color;
        CGColorRef color = CGColorCreateSRGB(((c >> 16) & 0xFF) / 255.0, ((c >> 8) & 0xFF) / 255.0, (c & 0xFF) / 255.0,
                                             ((c >> 24) & 0xFF) / 255.0);
        w->content.borderColor = color;
        CGColorRelease(color);
        w->content.borderWidth = 1.0 / g_scale;
    } else {
        w->content.borderWidth = 0.0;
    }

    if (cropping_changed) {
        os_unfair_lock_lock(&w->lock);
        w->dirty = w->surface != nullptr;
        os_unfair_lock_unlock(&w->lock);
    }

    NSRect frame = guest_to_frame(host_screen_rect(cfg));
    g_suppress_move = true;
    if (!w->fullscreen_transition && !w->fullscreen) {
        NSWindowStyleMask style = (cfg->flags & MIME_WIN_MINIMIZABLE) ? NSWindowStyleMaskMiniaturizable
                                                                      : NSWindowStyleMaskBorderless;
        if (w->window.styleMask != style)
            w->window.styleMask = style;
        w->window.collectionBehavior = (cfg->flags & MIME_WIN_RESIZABLE)
                                           ? NSWindowCollectionBehaviorFullScreenPrimary
                                           : NSWindowCollectionBehaviorFullScreenAuxiliary;
        if (!CGRectEqualToRect(w->window.frame, frame))
            [w->window setFrame:frame display:NO];
    }
    g_suppress_move = false;
    pointer_park(w);
    if (!w->fullscreen_transition && !w->fullscreen)
        w->window.level = (cfg->flags & MIME_WIN_TOPMOST) ? NSFloatingWindowLevel : NSNormalWindowLevel;

    bool opening = false;
    if (cfg->flags & MIME_WIN_VISIBLE) {
        if (w->window.miniaturized) {
            w->suppress_state = true;
            [w->window deminiaturize:nil];
            w->suppress_state = false;
        }
        opening = !w->shown || !w->window.visible;
        if (g_background) {
            if (opening)
                [w->window orderBack:nil];
        } else if (cfg->flags & MIME_WIN_ACTIVATE) {
            [NSApp activate];
            [w->window makeKeyAndOrderFront:nil];
        } else if (opening) {
            [w->window orderFrontRegardless];
        }
        w->shown = true;
    } else if (w->shown) {
        w->shown = false;
        if (animated(w) && w->window.visible) {
            animate_window(w, false);
            uint32_t window_id = w->id, gen = w->anim_gen;
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(130 * NSEC_PER_MSEC)), dispatch_get_main_queue(), ^{
                hide_later(window_id, gen);
            });
        } else {
            [w->window orderOut:nil];
            chrome_detach(w);
        }
    }
    if (w->shown)
        chrome_update(w);
    if (opening && animated(w))
        animate_window(w, true);
    else if (w->shown && opening)
        stop_animations(w);
    if (cropping_changed) {
        present_main(w->id);
        metal_relayout(w->id);
    }
    [CATransaction commit];
    update_presentation();
    /* the shadow follows the shape, which a new corner radius or border changes. */
    if (cropping_changed)
        [w->window invalidateShadow];
    /* a program covering the screen without a frame gets its own space, like a macOS game. */
    uint32_t shown = w->config.flags & (MIME_WIN_FULLSCREEN | MIME_WIN_VISIBLE | MIME_WIN_MINIMIZED);
    if (!(shown & MIME_WIN_FULLSCREEN))
        w->space_declined = false;
    else if (shown == (MIME_WIN_FULLSCREEN | MIME_WIN_VISIBLE) && !w->fullscreen && !w->fullscreen_transition &&
             !w->space_declined) {
        w->window.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
        w->window.styleMask |= NSWindowStyleMaskResizable;
        w->fullscreen_transition = true;
        [w->window toggleFullScreenUnchecked];
    }
}

static void config_latest(uint32_t id_)
{
    MimeWinConfig copy;
    bool queued = false;
    os_unfair_lock_lock(&g_windows_lock);
    UiWindow *w = find_window(id_);
    if (w) {
        os_unfair_lock_lock(&w->lock);
        if (w->config_queued) {
            copy = w->pending_config;
            w->config_queued = false;
            queued = true;
        }
        os_unfair_lock_unlock(&w->lock);
    }
    os_unfair_lock_unlock(&g_windows_lock);
    if (queued)
        config_main(&copy);
}

void mime_ui_window_config(const MimeWinConfig *cfg)
{
    bool queue = false;
    os_unfair_lock_lock(&g_windows_lock);
    UiWindow *w = find_window(cfg->window_id);
    if (w) {
        os_unfair_lock_lock(&w->lock);
        uint32_t activate = w->config_queued ? w->pending_config.flags & MIME_WIN_ACTIVATE : 0;
        w->pending_config = *cfg;
        w->pending_config.flags |= activate;
        queue = !w->config_queued;
        w->config_queued = true;
        os_unfair_lock_unlock(&w->lock);
    }
    os_unfair_lock_unlock(&g_windows_lock);
    uint32_t id_ = cfg->window_id;
    if (queue)
        on_main(^{
            config_latest(id_);
        });
}

/*
 * the compositor reads the surface directly, avoiding a present copy. contentsChanged announces changed
 * pixels.
 */
static void present_main(uint32_t id_)
{
    os_unfair_lock_lock(&g_windows_lock);
    UiWindow *w = find_window(id_);
    os_unfair_lock_unlock(&g_windows_lock);
    if (!w || !w->view)
        return;
    os_unfair_lock_lock(&w->lock);
    w->update_queued = false;
    if (!w->dirty || !w->surface) {
        os_unfair_lock_unlock(&w->lock);
        return;
    }
    MimeRect v = w->config.visible;
    double stretch = w->configured ? surface_stretch(&w->config) : 1.0;
    if (!w->configured || v.right <= v.left || v.bottom <= v.top)
        v = (MimeRect){0, 0, (int32_t)w->width, (int32_t)w->height};
    else if (stretch != 1.0)
        v = (MimeRect){(int32_t)lround(v.left / stretch), (int32_t)lround(v.top / stretch),
                       (int32_t)lround(v.right / stretch), (int32_t)lround(v.bottom / stretch)};
    v.left = MIME_MAX(v.left, 0);
    v.top = MIME_MAX(v.top, 0);
    v.right = MIME_MIN(v.right, (int32_t)w->width);
    v.bottom = MIME_MIN(v.bottom, (int32_t)w->height);
    if (v.right <= v.left || v.bottom <= v.top) {
        os_unfair_lock_unlock(&w->lock);
        return;
    }
    w->dirty = false;
    IOSurfaceRef surface = (IOSurfaceRef)CFRetain(w->surface);
    double width = w->width, height = w->height;
    bool shadow_dirty = w->shadow_dirty;
    w->shadow_dirty = false;
    os_unfair_lock_unlock(&w->lock);

    CALayer *layer = w->content;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    layer.contentsRect = CGRectMake(v.left / width, v.top / height, (v.right - v.left) / width,
                                    (v.bottom - v.top) / height);
    /* a stretched surface has fewer pixels per point, whole multiples repeat them and others blend them. */
    layer.contentsScale = g_scale / stretch;
    layer.magnificationFilter = stretch == floor(stretch) ? kCAFilterNearest : kCAFilterLinear;
    if (layer.contents != (__bridge id)surface)
        layer.contents = (__bridge id)surface;
    else
        [layer setContentsChanged];
    [CATransaction commit];
    CFRelease(surface);
    if (shadow_dirty)
        [w->window invalidateShadow];
}

static IOSurfaceRef create_surface(uint32_t width, uint32_t height)
{
    @autoreleasepool {
        NSDictionary *props = @{
            (__bridge NSString *)kIOSurfaceWidth: @(width),
            (__bridge NSString *)kIOSurfaceHeight: @(height),
            (__bridge NSString *)kIOSurfaceBytesPerElement: @4,
            (__bridge NSString *)kIOSurfacePixelFormat: @((uint32_t)'BGRA'),
        };
        IOSurfaceRef surface = IOSurfaceCreate((__bridge CFDictionaryRef)props);
        if (surface) {
            CFPropertyListRef space = CGColorSpaceCopyPropertyList(g_colorspace);
            if (space) {
                IOSurfaceSetValue(surface, kIOSurfaceColorSpace, space);
                CFRelease(space);
            }
        }
        return surface;
    }
}

void mime_ui_window_present(uint32_t id_, const void *pixels, uint32_t width, uint32_t height, uint32_t stride,
                            MimeRect dirty)
{
    os_unfair_lock_lock(&g_windows_lock);
    UiWindow *w = find_window(id_);
    if (!w) {
        os_unfair_lock_unlock(&g_windows_lock);
        return;
    }
    os_unfair_lock_lock(&w->lock);
    os_unfair_lock_unlock(&g_windows_lock);
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
        os_unfair_lock_unlock(&w->lock);
        return;
    }
    int32_t left = MIME_MAX(dirty.left, 0), top = MIME_MAX(dirty.top, 0);
    int32_t right = MIME_MIN(dirty.right, (int32_t)width), bottom = MIME_MIN(dirty.bottom, (int32_t)height);
    IOSurfaceLock(w->surface, 0, nullptr);
    uint8_t *base = IOSurfaceGetBaseAddress(w->surface);
    size_t row = IOSurfaceGetBytesPerRow(w->surface);
    for (int32_t y = top; y < bottom && left < right; y++)
        memcpy(base + (size_t)y * row + (size_t)left * 4, (const uint8_t *)pixels + (size_t)y * stride + (size_t)left * 4,
               (size_t)(right - left) * 4);
    IOSurfaceUnlock(w->surface, 0, nullptr);
    w->dirty = true;
    bool queue = !w->update_queued;
    w->update_queued = true;
    os_unfair_lock_unlock(&w->lock);
    if (queue)
        on_main(^{
            present_main(id_);
        });
}

static NSCursor *wait_cursor(void)
{
    static NSCursor *cursor;
    if (cursor)
        return cursor;
    CGContextRef ctx = CGBitmapContextCreate(nullptr, 64, 64, 8, 0, g_colorspace,
                                             (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
    if (!ctx)
        return NSCursor.arrowCursor;
    CGContextScaleCTM(ctx, 2, 2);
    CGMutablePathRef path = CGPathCreateMutable();
    CGPathMoveToPoint(path, nullptr, 9, 4);
    CGPathAddLineToPoint(path, nullptr, 23, 4);
    CGPathAddLineToPoint(path, nullptr, 23, 8);
    CGPathAddLineToPoint(path, nullptr, 17, 16);
    CGPathAddLineToPoint(path, nullptr, 23, 24);
    CGPathAddLineToPoint(path, nullptr, 23, 28);
    CGPathAddLineToPoint(path, nullptr, 9, 28);
    CGPathAddLineToPoint(path, nullptr, 9, 24);
    CGPathAddLineToPoint(path, nullptr, 15, 16);
    CGPathAddLineToPoint(path, nullptr, 9, 8);
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
    NSImage *image = [[NSImage alloc] initWithCGImage:cgimage size:NSMakeSize(32, 32)];
    cursor = [[NSCursor alloc] initWithImage:image hotSpot:NSMakePoint(16, 16)];
    CGImageRelease(cgimage);
    CGContextRelease(ctx);
    return cursor;
}

static NSCursor *host_cursor(uint32_t c)
{
    switch (c) {
    case MIME_CURSOR_IBEAM: return NSCursor.IBeamCursor;
    case MIME_CURSOR_WAIT: return wait_cursor();
    case MIME_CURSOR_CROSS: return NSCursor.crosshairCursor;
    case MIME_CURSOR_HAND: return NSCursor.pointingHandCursor;
    case MIME_CURSOR_SIZE_NS:
        return [NSCursor frameResizeCursorFromPosition:NSCursorFrameResizePositionTop
                                          inDirections:NSCursorFrameResizeDirectionsAll];
    case MIME_CURSOR_SIZE_WE:
        return [NSCursor frameResizeCursorFromPosition:NSCursorFrameResizePositionLeft
                                          inDirections:NSCursorFrameResizeDirectionsAll];
    case MIME_CURSOR_SIZE_NWSE:
        return [NSCursor frameResizeCursorFromPosition:NSCursorFrameResizePositionTopLeft
                                          inDirections:NSCursorFrameResizeDirectionsAll];
    case MIME_CURSOR_SIZE_NESW:
        return [NSCursor frameResizeCursorFromPosition:NSCursorFrameResizePositionTopRight
                                          inDirections:NSCursorFrameResizeDirectionsAll];
    case MIME_CURSOR_SIZE_ALL: return NSCursor.openHandCursor;
    case MIME_CURSOR_NO: return NSCursor.operationNotAllowedCursor;
    default: return NSCursor.arrowCursor;
    }
}

static void cursor_main(uint32_t c)
{
    if (c > MIME_CURSOR_HIDDEN)
        c = MIME_CURSOR_ARROW;
    g_cursor = c;
    if (!g_pointer_in_guest)
        return;
    if (c == MIME_CURSOR_HIDDEN) {
        if (!g_cursor_hidden) {
            [NSCursor hide];
            g_cursor_hidden = true;
        }
        return;
    }
    restore_host_cursor();
    [host_cursor(c) set];
}

void mime_ui_set_cursor(uint32_t cursor)
{
    on_main(^{
        cursor_main(cursor);
    });
}

/* not shown yet: the arrow stands for a cursor a program made. */
void mime_ui_set_cursor_image(const void *pixels, uint32_t width, uint32_t height, uint32_t stride, uint32_t hot_x,
                              uint32_t hot_y, uint32_t dpi)
{
    mime_ui_set_cursor(MIME_CURSOR_ARROW);
}

/* a locked pointer reports motion alone. confining it to a rectangle is not done on macOS yet. */
void mime_ui_pointer(uint32_t mode, uint32_t window_id, const MimeRect *rect)
{
    on_main(^{
        g_pointer_mode = mode;
        g_pointer_window = window_id;
        pointer_apply();
    });
}

/* game controllers are not read on macOS yet. */
void mime_ui_pad_rumble(uint32_t pad, uint16_t low, uint16_t high)
{
}

/* the display's gamma ramp is not applied on macOS yet. */
void mime_ui_gamma(const uint16_t *ramp)
{
}

void mime_ui_beep(uint32_t type)
{
    on_main(^{
        NSBeep();
    });
}

/* ------------------------------------------------------------------ clipboard. */

/*
 * macOS provides no pasteboard change notification. check its counter on activation and poll slowly while
 * active for changes such as Universal Clipboard. notify the guest only whether text exists; read text on
 * demand on the main thread so vCPUs never touch the pasteboard.
 */
static os_unfair_lock g_clip_lock = OS_UNFAIR_LOCK_INIT;
static uint16_t *g_clip_text;
static size_t g_clip_chars;
static bool g_clip_stale;           /* the pasteboard changed since g_clip_text was read. */
static NSInteger g_clip_seen = -1;  /* main thread only. */
static dispatch_source_t g_clip_timer;
static bool g_clip_polling;

static void clip_check(void)
{
    @autoreleasepool {
        NSPasteboard *pb = NSPasteboard.generalPasteboard;
        NSInteger count = pb.changeCount;
        if (count != g_clip_seen) {
            g_clip_seen = count;
            bool text = [pb availableTypeFromArray:@[NSPasteboardTypeString]] != nil;
            os_unfair_lock_lock(&g_clip_lock);
            free(g_clip_text);
            g_clip_text = nullptr;
            g_clip_chars = 0;
            g_clip_stale = text;
            os_unfair_lock_unlock(&g_clip_lock);
            MimeInputEvent ev = {};
            ev.type = MIME_INPUT_CLIPBOARD;
            ev.x = text;
            emit(&ev);
        }
    }
}

/* the pasteboard text with windows line ends, read on the main thread. */
static void clip_read(void)
{
    @autoreleasepool {
        NSString *str = [NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString];
        NSUInteger n = str.length;
        uint16_t *raw = n ? malloc(n * sizeof(uint16_t)) : nullptr;
        uint16_t *text = n ? malloc(n * 2 * sizeof(uint16_t)) : nullptr;
        size_t chars = 0;
        if (raw && text) {
            [str getCharacters:raw range:NSMakeRange(0, n)];
            for (NSUInteger i = 0; i < n; i++) {
                if (raw[i] == '\n' && (i == 0 || raw[i - 1] != '\r'))
                    text[chars++] = '\r';
                text[chars++] = raw[i];
            }
        } else {
            free(text);
            text = nullptr;
        }
        free(raw);
        os_unfair_lock_lock(&g_clip_lock);
        free(g_clip_text);
        g_clip_text = text;
        g_clip_chars = chars;
        g_clip_stale = false;
        os_unfair_lock_unlock(&g_clip_lock);
    }
}

static void clip_start(void *arg)
{
    g_clip_timer = dispatch_source_create(DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
    dispatch_source_set_event_handler(g_clip_timer, ^{
        clip_check();
    });
    dispatch_source_set_timer(g_clip_timer, dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC), NSEC_PER_SEC,
                              250 * NSEC_PER_MSEC);
    /* called after the run loop starts and the machine exists. */
    dispatch_async(dispatch_get_main_queue(), ^{
        clip_check();
    });
}

static void app_became_active(void)
{
    clip_check();
    if (g_clip_timer && !g_clip_polling) {
        dispatch_resume(g_clip_timer);
        g_clip_polling = true;
    }
}

static void app_resigned_active(void)
{
    restore_host_cursor();
    g_pointer_in_guest = false;
    if (g_clip_timer && g_clip_polling) {
        dispatch_suspend(g_clip_timer);
        g_clip_polling = false;
    }
}

void mime_ui_clipboard_set(const uint16_t *text, size_t chars)
{
    uint16_t *copy = malloc((chars ? chars : 1) * sizeof(uint16_t));
    uint16_t *keep = malloc((chars ? chars : 1) * sizeof(uint16_t));
    if (!copy || !keep) {
        free(copy);
        free(keep);
        return;
    }
    size_t n = 0;
    for (size_t i = 0; i < chars; i++)
        if (!(text[i] == '\r' && i + 1 < chars && text[i + 1] == '\n'))
            copy[n++] = text[i];
    memcpy(keep, text, chars * sizeof(uint16_t));
    os_unfair_lock_lock(&g_clip_lock);
    free(g_clip_text);
    g_clip_text = keep;
    g_clip_chars = chars;
    g_clip_stale = false;
    os_unfair_lock_unlock(&g_clip_lock);
    NSString *string = [[NSString alloc] initWithCharactersNoCopy:copy length:n freeWhenDone:YES];
    on_main(^{
        NSPasteboard *pb = NSPasteboard.generalPasteboard;
        [pb clearContents];
        if (string.length)
            [pb setString:string forType:NSPasteboardTypeString];
        /* the guest already holds this text: do not offer it back. */
        g_clip_seen = pb.changeCount;
        os_unfair_lock_lock(&g_clip_lock);
        g_clip_stale = false;
        os_unfair_lock_unlock(&g_clip_lock);
    });
}

size_t mime_ui_clipboard_read(uint16_t *dst, size_t offset, size_t count)
{
    os_unfair_lock_lock(&g_clip_lock);
    bool stale = g_clip_stale;
    os_unfair_lock_unlock(&g_clip_lock);
    /* the main thread never waits for a vCPU, so this cannot deadlock. */
    if (stale)
        dispatch_sync(dispatch_get_main_queue(), ^{
            clip_read();
        });
    os_unfair_lock_lock(&g_clip_lock);
    size_t total = g_clip_chars;
    if (offset < total && count) {
        size_t n = total - offset < count ? total - offset : count;
        memcpy(dst, g_clip_text + offset, n * sizeof(uint16_t));
    }
    os_unfair_lock_unlock(&g_clip_lock);
    return total;
}

/* the pasteboard is shared as text only: the other kinds are not there to offer or fetch. */
uint64_t mime_ui_clipboard_offer(uint32_t mask)
{
    return MIME_HC_STATUS_UNSUPPORTED;
}

uint64_t mime_ui_clipboard_fetch(uint32_t kind, uint32_t generation, void *dst, uint64_t capacity, uint64_t offset,
                                 uint64_t *total)
{
    *total = 0;
    return MIME_HC_STATUS_UNSUPPORTED;
}

uint64_t mime_ui_clipboard_provide(uint32_t request, const void *src, uint64_t length, uint64_t offset,
                                   uint64_t total)
{
    return MIME_HC_STATUS_UNSUPPORTED;
}

/* typed text arrives as characters here; no composition is shown. */
uint64_t mime_ui_ime_text(uint16_t *dst, size_t capacity, uint64_t *length)
{
    *length = 0;
    return MIME_HC_STATUS_UNSUPPORTED;
}

uint64_t mime_ui_ime_area(uint32_t window, const MimeRect *area, int32_t cursor)
{
    return MIME_HC_STATUS_UNSUPPORTED;
}

uint64_t mime_ui_ime_control(uint32_t op, uint32_t window, uint64_t *value)
{
    *value = 0;
    return MIME_HC_STATUS_UNSUPPORTED;
}

/* drags go no further than the guest's own windows here. */
uint64_t mime_ui_dnd_effect(uint32_t session, uint32_t effect)
{
    return MIME_HC_STATUS_UNSUPPORTED;
}

uint64_t mime_ui_dnd_finish(uint32_t session, uint32_t effect)
{
    return MIME_HC_STATUS_UNSUPPORTED;
}

uint64_t mime_ui_dnd_fetch(uint32_t session, uint32_t kind, void *dst, uint64_t capacity, uint64_t offset,
                           uint64_t *total)
{
    *total = 0;
    return MIME_HC_STATUS_UNSUPPORTED;
}

uint64_t mime_ui_dnd_start(uint32_t kinds, uint32_t effects, const MimeDragIcon *icon, size_t icon_bytes,
                           uint64_t *session)
{
    *session = 0;
    return MIME_HC_STATUS_UNSUPPORTED;
}

uint64_t mime_ui_dnd_stop(uint32_t session)
{
    return MIME_HC_STATUS_UNSUPPORTED;
}

/* ---------------------------------------------------------------- host dialogs. */

/*
 * a question or a notice is an NSAlert shown without its modal loop, which would hold the main queue the guest's
 * windows are drawn from, and progress is a panel of its own.
 */
static constexpr int MAX_DIALOGS = 8;
static constexpr double DIALOG_WIDTH = 420.0;
static constexpr double DIALOG_MARGIN = 20.0;

typedef struct UiDialog {
    uint32_t id;
    uint32_t process_id;
    NSAlert *alert;
    NSWindow *window;
    NSTextField *heading;
    NSTextField *text;
    NSProgressIndicator *bar;
} UiDialog;

__attribute__((objc_subclassing_restricted))
@interface MimeHostDialogTarget : NSObject
@end

static UiDialog g_dialogs[MAX_DIALOGS]; /* main thread only. */
static MimeHostDialogTarget *g_dialog_target;

static NSString *ustring(const uint16_t *s, size_t capacity)
{
    size_t n = 0;
    while (n < capacity && s[n])
        n++;
    return [NSString stringWithCharacters:s length:n];
}

static UiDialog *find_dialog(uint32_t id_)
{
    for (int i = 0; i < MAX_DIALOGS; i++)
        if (g_dialogs[i].id == id_)
            return &g_dialogs[i];
    return nullptr;
}

/* a question or a notice is answered with the button, -1 when it was taken away. */
static void close_dialog(UiDialog *slot, int32_t answer)
{
    [slot->window orderOut:nil];
    if (slot->alert) {
        MimeInputEvent ev = {.type = MIME_INPUT_SHELL, .pad = slot->id, .flags = MIME_SHELL_EVENT_DIALOG_ANSWERED,
                             .x = answer};
        emit(&ev);
    }
    *slot = (UiDialog){};
}

@implementation MimeHostDialogTarget

- (void)dialogPressed:(NSButton *)sender
{
    NSWindow *window = sender.window;
    for (int i = 0; i < MAX_DIALOGS; i++)
        if (g_dialogs[i].id && g_dialogs[i].window == window) {
            close_dialog(&g_dialogs[i], (int32_t)sender.tag);
            return;
        }
}

@end

static void open_alert(UiDialog *slot, const MimeShellDialog *d)
{
    NSAlert *alert = [[NSAlert alloc] init];
    alert.messageText = d->instruction[0] ? ustring(d->instruction, MIME_ARRAY_COUNT(d->instruction)) : ustring(d->title, MIME_ARRAY_COUNT(d->title));
    alert.informativeText = ustring(d->content, MIME_ARRAY_COUNT(d->content));
    alert.alertStyle = d->kind == MIME_DIALOG_ERROR ? NSAlertStyleCritical : NSAlertStyleInformational;
    uint32_t count = d->kind == MIME_DIALOG_ASK ? d->button_count : 0;
    if (count > MIME_DIALOG_BUTTONS)
        count = MIME_DIALOG_BUTTONS;
    for (uint32_t i = 0; i < (count ? count : 1); i++) {
        NSString *title = count ? ustring(d->buttons[i], MIME_ARRAY_COUNT(d->buttons[i])) : @"OK";
        NSButton *button = [alert addButtonWithTitle:title];
        button.target = g_dialog_target;
        button.action = @selector(dialogPressed:);
        button.tag = i;
        /* the last choice is the one escape gives, as the guest's own dialogs have it. */
        if (count >= 2 && i == count - 1)
            button.keyEquivalent = @"\033";
    }
    [alert layout];
    slot->alert = alert;
    slot->window = alert.window;
}

static NSTextField *dialog_label(bool heading)
{
    NSTextField *label = [NSTextField wrappingLabelWithString:@""];
    label.font = heading ? [NSFont boldSystemFontOfSize:13.0] : [NSFont systemFontOfSize:NSFont.smallSystemFontSize];
    if (!heading)
        label.textColor = NSColor.secondaryLabelColor;
    label.preferredMaxLayoutWidth = DIALOG_WIDTH - 2 * DIALOG_MARGIN;
    return label;
}

/* the content view grows from the bar at the bottom up to the heading. */
static void update_progress(UiDialog *slot, const MimeShellDialog *d)
{
    slot->heading.stringValue = ustring(d->instruction, MIME_ARRAY_COUNT(d->instruction));
    slot->text.stringValue = ustring(d->content, MIME_ARRAY_COUNT(d->content));
    bool known = d->permille >= 0;
    slot->bar.indeterminate = !known;
    if (known) {
        slot->bar.doubleValue = d->permille > 1000 ? 1000 : d->permille;
        [slot->bar stopAnimation:nil];
    } else {
        [slot->bar startAnimation:nil];
    }
    double inner = DIALOG_WIDTH - 2 * DIALOG_MARGIN;
    NSSize heading = slot->heading.fittingSize;
    NSSize text = slot->text.fittingSize;
    double y = DIALOG_MARGIN;
    slot->bar.frame = NSMakeRect(DIALOG_MARGIN, y, inner, 20);
    y += 20 + 10;
    slot->text.frame = NSMakeRect(DIALOG_MARGIN, y, inner, text.height);
    y += text.height + 6;
    slot->heading.frame = NSMakeRect(DIALOG_MARGIN, y, inner, heading.height);
    y += heading.height + DIALOG_MARGIN;
    [slot->window setContentSize:NSMakeSize(DIALOG_WIDTH, y)];
}

static void open_progress(UiDialog *slot, const MimeShellDialog *d)
{
    NSPanel *panel = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, DIALOG_WIDTH, 120)
                                                styleMask:NSWindowStyleMaskTitled
                                                  backing:NSBackingStoreBuffered
                                                    defer:NO];
    panel.releasedWhenClosed = NO;
    panel.title = ustring(d->title, MIME_ARRAY_COUNT(d->title));
    NSView *content = panel.contentView;
    slot->heading = dialog_label(true);
    slot->text = dialog_label(false);
    NSProgressIndicator *bar = [[NSProgressIndicator alloc] initWithFrame:NSZeroRect];
    bar.minValue = 0;
    bar.maxValue = 1000;
    [content addSubview:slot->heading];
    [content addSubview:slot->text];
    [content addSubview:bar];
    slot->bar = bar;
    slot->window = panel;
    update_progress(slot, d);
}

static void dialog_main(const MimeShellDialog *d)
{
    @autoreleasepool {
        if (!g_dialog_target)
            g_dialog_target = [[MimeHostDialogTarget alloc] init];
        UiDialog *slot = find_dialog(d->id);
        if (d->kind == MIME_DIALOG_END) {
            if (slot)
                close_dialog(slot, -1);
        } else if (d->kind == MIME_DIALOG_UPDATE) {
            if (slot && slot->bar)
                update_progress(slot, d);
        } else if (!slot && (slot = find_dialog(0))) {
            slot->id = d->id;
            slot->process_id = d->process_id;
            if (d->kind == MIME_DIALOG_PROGRESS)
                open_progress(slot, d);
            else
                open_alert(slot, d);
            slot->window.level = NSFloatingWindowLevel;
            [slot->window center];
            if (g_background) {
                [slot->window orderFront:nil];
            } else {
                [NSApp activate];
                [slot->window makeKeyAndOrderFront:nil];
            }
        } else if (d->kind != MIME_DIALOG_PROGRESS) {
            MimeInputEvent ev = {.type = MIME_INPUT_SHELL, .pad = d->id, .flags = MIME_SHELL_EVENT_DIALOG_ANSWERED,
                                 .x = -1};
            emit(&ev);
        }
    }
}

/* a program that ends leaves no question or progress behind, and nobody waits for the answer. */
static void dialogs_process_exit(uint32_t process_id)
{
    for (int i = 0; i < MAX_DIALOGS; i++)
        if (g_dialogs[i].id && g_dialogs[i].process_id == process_id) {
            [g_dialogs[i].window orderOut:nil];
            g_dialogs[i] = (UiDialog){};
        }
}

/* the folder in Finder, which also selects an item. never opened as a document: a bundle on the drive would run. */
static uint64_t show_folder(const MimeShellFolder *f)
{
    char guest[MIME_PATH_MAX], host[MIME_PATH_MAX];
    mime_utf16_to_utf8(guest, sizeof(guest), f->path, sizeof(f->path) / sizeof(f->path[0]));
    const MimePaths *paths = mime_daemon_paths();
    if (!paths || !mime_paths_to_host(paths, guest, host, sizeof(host)))
        return MIME_HC_STATUS_NOT_FOUND;
    NSString *path = [[NSFileManager defaultManager] stringWithFileSystemRepresentation:host[0] ? host : "/"
                                                                                 length:host[0] ? strlen(host) : 1];
    BOOL directory = NO;
    if (!path || ![[NSFileManager defaultManager] fileExistsAtPath:path isDirectory:&directory])
        return MIME_HC_STATUS_NOT_FOUND;
    bool select = f->select != 0;
    if (!select && !directory)
        return MIME_HC_STATUS_INVALID;
    on_main(^{
        if (select)
            [[NSWorkspace sharedWorkspace] selectFile:path inFileViewerRootedAtPath:@""];
        else
            [[NSWorkspace sharedWorkspace] selectFile:nil inFileViewerRootedAtPath:path];
    });
    return MIME_HC_STATUS_OK;
}

/* only a web address: a file or any other scheme would let a program of the machine run things of the host. */
static uint64_t open_url(const MimeShellUrl *u)
{
    size_t chars = 0;
    while (chars < sizeof(u->url) / sizeof(u->url[0]) && u->url[chars])
        chars++;
    NSString *text = [NSString stringWithCharacters:u->url length:chars];
    NSURL *url = text ? [NSURL URLWithString:text] : nil;
    NSString *scheme = url.scheme.lowercaseString;
    if (!url || !([scheme isEqualToString:@"http"] || [scheme isEqualToString:@"https"]))
        return MIME_HC_STATUS_INVALID;
    on_main(^{
        [[NSWorkspace sharedWorkspace] openURL:url];
    });
    return MIME_HC_STATUS_OK;
}

/* the dialogs, the folders and the web addresses: toasts, launcher entries and jump lists have no place here yet. */
uint64_t mime_ui_shell(void *command, size_t size, uint64_t *value)
{
    if (*(uint32_t *)command == MIME_SHELL_URL)
        return size >= sizeof(MimeShellUrl) ? open_url(command) : MIME_HC_STATUS_INVALID;
    if (*(uint32_t *)command == MIME_SHELL_FOLDER)
        return size >= sizeof(MimeShellFolder) ? show_folder(command) : MIME_HC_STATUS_INVALID;
    if (*(uint32_t *)command != MIME_SHELL_DIALOG)
        return MIME_HC_STATUS_UNSUPPORTED;
    const MimeShellDialog *d = command;
    if (size < sizeof(*d) || d->kind < MIME_DIALOG_ASK || d->kind > MIME_DIALOG_END)
        return MIME_HC_STATUS_INVALID;
    MimeShellDialog copy = *d;
    on_main(^{
        dialog_main(&copy);
    });
    return MIME_HC_STATUS_OK;
}

/* ---------------------------------------------------------------- Metal layers. */

/*
 * GPU channels present through layers over window views. flip their transforms to display OpenGL bottom-first
 * rows without copying.
 */
typedef struct MetalLayer {
    MimeMetalLayer *layer;
    uint32_t window_id;
    int32_t x, y;
    uint32_t width, height;
    bool sync;
    uint32_t order; /* place in the paint order of the window's tree, the front ones highest. */
} MetalLayer;

static constexpr int MAX_METAL_LAYERS = 64;

static MetalLayer g_metal[MAX_METAL_LAYERS]; /* main thread only. */

static void metal_layout(MetalLayer *m)
{
    os_unfair_lock_lock(&g_windows_lock);
    UiWindow *w = m->window_id ? find_window(m->window_id) : nullptr;
    os_unfair_lock_unlock(&g_windows_lock);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    if (!w || !w->view) {
        m->layer.hidden = YES;
        [m->layer removeFromSuperlayer];
    } else {
        CALayer *parent = w->content;
        if (m->layer.superlayer != parent)
            [parent addSublayer:m->layer];
        MimeRect v = w->config.visible;
        if (!w->configured || v.right <= v.left || v.bottom <= v.top)
            v = (MimeRect){0, 0, 0, 0};
        /* the guest places the layer in the pixels of the program, fewer for a window that is stretched. */
        double k = surface_stretch(&w->config);
        double width = m->width * k / g_scale, height = m->height * k / g_scale;
        double x = (m->x * k - v.left) / g_scale, y = (m->y * k - v.top) / g_scale;
        m->layer.bounds = CGRectMake(0, 0, width, height);
        m->layer.position = CGPointMake(x + width / 2, y + height / 2);
        m->layer.drawableSize = m->layer.native && k > 1 ? CGSizeMake(lround(m->width * k), lround(m->height * k))
                                                         : CGSizeMake(m->width, m->height);
        m->layer.displaySyncEnabled = m->sync;
        m->layer.zPosition = m->order;
        m->layer.hidden = NO;
    }
    [CATransaction commit];
}

static void metal_relayout(uint32_t window_id)
{
    for (int i = 0; i < MAX_METAL_LAYERS; i++)
        if (g_metal[i].layer && g_metal[i].window_id == window_id)
            metal_layout(&g_metal[i]);
}

static MimeMetalLayer *new_metal_layer(id<MTLDevice> device, bool flipped, bool native)
{
    [CATransaction begin];
    MimeMetalLayer *layer = [[MimeMetalLayer alloc] init];
    layer.native = native;
    layer.device = device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = NO;
    layer.contentsScale = g_scale;
    layer.opaque = YES;
    if (flipped)
        layer.transform = CATransform3DMakeScale(1, -1, 1);
    [CATransaction commit];
    return layer;
}

/* on the main thread: a thread without a run loop never commits the layer's implicit transaction. */
void *mime_ui_metal_layer_create(void *device, bool flipped, bool native)
{
    __block MimeMetalLayer *layer;
    sync_main(^{
        layer = new_metal_layer((__bridge id<MTLDevice>)device, flipped, native);
    });
    return (__bridge_retained void *)layer;
}

static void metal_place(MetalLayer p)
{
    MetalLayer *m = nullptr, *free_slot = nullptr;
    for (int i = 0; i < MAX_METAL_LAYERS && !m; i++) {
        if (g_metal[i].layer == p.layer)
            m = &g_metal[i];
        else if (!g_metal[i].layer && !free_slot)
            free_slot = &g_metal[i];
    }
    if (!m && free_slot)
        m = free_slot;
    if (m) {
        *m = p;
        metal_layout(m);
    }
}

void mime_ui_metal_layer_place(void *layer, uint32_t window_id, int32_t x, int32_t y, uint32_t width,
                               uint32_t height, bool sync, uint32_t order)
{
    MimeMetalLayer *metal = (__bridge MimeMetalLayer *)layer;
    on_main(^{
        metal_place((MetalLayer){metal, window_id, x, y, width, height, sync, order});
    });
}

void mime_ui_metal_layer_destroy(void *layer)
{
    MimeMetalLayer *metal = (__bridge_transfer MimeMetalLayer *)layer;
    on_main(^{
        for (int i = 0; i < MAX_METAL_LAYERS; i++)
            if (g_metal[i].layer == metal)
                g_metal[i] = (MetalLayer){};
        [metal removeFromSuperlayer];
    });
}

/* ---------------------------------------------------------------- Vulkan presenters. */

/*
 * a layer for each window of the guest that presents with vulkan, over its part of the client area and stacked by
 * the guest's paint order, as a browser's views each present to a window of their own; host/vulkan/vkwsi.c.
 */
typedef struct VkLayer {
    uint32_t window_id;
    uint32_t source;
    uint32_t order;
    void *layer;
    MimeRect place;
} VkLayer;

static VkLayer g_vk_layers[MAX_METAL_LAYERS];
static os_unfair_lock g_vk_lock = OS_UNFAIR_LOCK_INIT;

static VkLayer *vk_layer(uint32_t window_id, uint32_t source)
{
    for (int i = 0; i < MAX_METAL_LAYERS; i++)
        if (g_vk_layers[i].layer && g_vk_layers[i].window_id == window_id && g_vk_layers[i].source == source)
            return &g_vk_layers[i];
    return nullptr;
}

static void vk_place(const VkLayer *v)
{
    MimeRect r = v->place;
    mime_ui_metal_layer_place(v->layer, v->window_id, r.left, r.top, (uint32_t)MIME_MAX(r.right - r.left, 1),
                              (uint32_t)MIME_MAX(r.bottom - r.top, 1), true, v->order);
}

/* the first placement is done before the first frame, which would go nowhere on a layer outside the window. */
static void vk_place_now(const VkLayer *v)
{
    MimeRect r = v->place;
    MimeMetalLayer *metal = (__bridge MimeMetalLayer *)v->layer;
    uint32_t window_id = v->window_id, order = v->order;
    uint32_t width = (uint32_t)MIME_MAX(r.right - r.left, 1), height = (uint32_t)MIME_MAX(r.bottom - r.top, 1);
    sync_main(^{
        metal_place((MetalLayer){metal, window_id, r.left, r.top, width, height, true, order});
    });
}

/* every window of the guest that presents gets a layer of its own. */
bool mime_ui_vk_layers(void) { return true; }

bool mime_ui_vk_attach(uint32_t id_, uint32_t source, MimeRect client, uint32_t order, MimeUiNative *native)
{
    os_unfair_lock_lock(&g_windows_lock);
    bool exists = find_window(id_) != nullptr;
    os_unfair_lock_unlock(&g_windows_lock);
    if (!exists)
        return false;
    os_unfair_lock_lock(&g_vk_lock);
    VkLayer *v = vk_layer(id_, source), made = {0};
    for (int i = 0; i < MAX_METAL_LAYERS && !v; i++)
        if (!g_vk_layers[i].layer) {
            v = &g_vk_layers[i];
            *v = (VkLayer){id_, source, order, mime_ui_metal_layer_create(nullptr, false, false), client};
            made = *v;
        }
    void *layer = v ? v->layer : nullptr;
    os_unfair_lock_unlock(&g_vk_lock);
    if (!layer)
        return false;
    if (made.layer)
        vk_place_now(&made);
    *native = (MimeUiNative){.metal = true, .subsurface = true, .surface = layer};
    return true;
}

void mime_ui_vk_detach(uint32_t id_, uint32_t source)
{
    os_unfair_lock_lock(&g_vk_lock);
    VkLayer *v = vk_layer(id_, source);
    void *layer = v ? v->layer : nullptr;
    if (v)
        *v = (VkLayer){};
    os_unfair_lock_unlock(&g_vk_lock);
    if (layer)
        mime_ui_metal_layer_destroy(layer);
}

/* guest pixels are the window's backing pixels, so the client area is the drawable as it is. */
void mime_ui_vk_shown(uint32_t id, uint32_t source, uint32_t serial, bool shown)
{
}

bool mime_ui_vk_frame(uint32_t id_, uint32_t source, const MimeUiPlace *place, MimeRect image, MimeUiFrame *frame)
{
    /* the part of the client area that shows. */
    MimeRect full = place->client, client = place->visible;
    uint32_t order = place->order;
    client = (MimeRect){MIME_MAX(client.left, full.left), MIME_MAX(client.top, full.top),
                        MIME_MIN(client.right, full.right), MIME_MIN(client.bottom, full.bottom)};
    if (client.right <= client.left || client.bottom <= client.top)
        client = (MimeRect){};
    os_unfair_lock_lock(&g_windows_lock);
    UiWindow *w = find_window(id_);
    if (!w) {
        os_unfair_lock_unlock(&g_windows_lock);
        return false;
    }
    *frame = (MimeUiFrame){.subsurface = true, .scale = 1.0, .client = full, .area = client};
    /* a layer keeps the last drawable presented to it, so a frame before the window appears shows with it. */
    frame->shown = !(w->config.flags & MIME_WIN_MINIMIZED);
    frame->visible = w->config.visible;
    frame->host = w->config.visible;
    os_unfair_lock_unlock(&g_windows_lock);
    frame->pixel_width = (uint32_t)MIME_MAX(client.right - client.left, 0);
    frame->pixel_height = (uint32_t)MIME_MAX(client.bottom - client.top, 0);
    os_unfair_lock_lock(&g_vk_lock);
    VkLayer *v = vk_layer(id_, source);
    if (v && (memcmp(&v->place, &client, sizeof(client)) || v->order != order)) {
        v->place = client;
        v->order = order;
        vk_place(v);
    }
    os_unfair_lock_unlock(&g_vk_lock);
    return true;
}

void mime_ui_vk_place(uint32_t id, uint32_t source, const MimeUiPlace *place)
{
    MimeUiFrame frame;
    mime_ui_vk_frame(id, source, place, place->client, &frame);
}

/* the window system draws the guest's surface around the layer, nothing to copy. */
bool mime_ui_vk_copy(uint32_t id_, uint8_t *dst, uint32_t width, uint32_t height, MimeRect *dirty)
{
    return false;
}

/*
 * after a display change, remap host windows using their existing guest rectangles and report the new guest
 * display size.
 */
static void screens_changed(void)
{
    MimeDisplayInfo old = g_display, info;
    double old_scale = g_scale;
    mime_ui_get_display(&info);
    if (!memcmp(&old, &info, sizeof(info)))
        return;
    mime_info("display changed to %ux%u, scale %.2f", info.width, info.height, g_scale);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    for (unsigned i = 0; i < MAX_WINDOWS; i++) {
        UiWindow *w = &g_windows[i];
        if (!w->used || !w->window)
            continue;
        if (g_scale != old_scale) {
            w->content.contentsScale = g_scale;
            os_unfair_lock_lock(&w->lock);
            w->dirty = w->surface != nullptr;
            os_unfair_lock_unlock(&w->lock);
        }
        if (w->configured && !w->fullscreen && !w->fullscreen_transition) {
            g_suppress_move = true;
            [w->window setFrame:guest_to_frame(host_screen_rect(&w->config)) display:NO];
            g_suppress_move = false;
        }
        pointer_park(w);
    }
    for (int i = 0; i < MAX_METAL_LAYERS; i++) {
        if (!g_metal[i].layer)
            continue;
        if (g_scale != old_scale)
            g_metal[i].layer.contentsScale = g_scale;
        metal_relayout(g_metal[i].window_id);
    }
    [CATransaction commit];
    MimeInputEvent ev = {.type = MIME_INPUT_DISPLAY, .width = (int32_t)info.width, .height = (int32_t)info.height,
                         .time_ms = now_ms()};
    if (g_sink)
        g_sink(g_sink_ctx, &ev);
}

/* vkwsi.c keeps the last image each presenter showed. */
bool mvk_wsi_readback(uint32_t window, uint32_t source, void *pixels, uint32_t capacity_width, uint32_t capacity_height,
                      uint32_t stride, uint32_t *width, uint32_t *height, MimeRect *client, uint64_t *serial);

bool mime_ui_window_readback(MimeWinReadback *r, void *pixels)
{
    return mvk_wsi_readback(r->window_id, r->source, pixels, r->capacity_width, r->capacity_height, r->stride, &r->width,
                            &r->height, &r->client, &r->serial);
}
