#ifndef MIME_LINUX_WAYLAND_H
#define MIME_LINUX_WAYLAND_H

#include "../mime_host.h"
#include "../../include/mime/abi.h"

struct wl_display;
struct wl_surface;
struct wl_output;
struct wl_seat;
struct wl_compositor;
struct wl_subcompositor;
struct wl_shm;
struct wp_viewport;

typedef struct MimeWlWindow MimeWlWindow;
typedef struct MimeWlCursor MimeWlCursor;

typedef enum MimeWlKind {
    MIME_WL_TOPLEVEL,
    MIME_WL_POPUP,   /* placed on its parent, takes the pointer. */
    MIME_WL_TOOLTIP, /* placed on its parent, the pointer goes through. */
    MIME_WL_BARE,    /* a surface with no role, for its owner to give one. */
} MimeWlKind;

typedef enum MimeWlState {
    MIME_WL_MINIMIZED,
    MIME_WL_RESTORED,
    MIME_WL_MAXIMIZED,
    MIME_WL_FULLSCREEN_ENTERED,
    MIME_WL_FULLSCREEN_LEFT,
} MimeWlState;

enum {
    MIME_WL_MOD_SHIFT = 1,
    MIME_WL_MOD_CTRL = 2,
    MIME_WL_MOD_ALT = 4,
    MIME_WL_MOD_LOGO = 8,
    MIME_WL_MOD_CAPS = 16,
};

/* a shape of the cursor theme, in the order of MIME_CURSOR_*. */
enum { MIME_WL_SHAPES = 11 };

/* what the compositor tells, each on the main thread. positions are in the surface coordinates of the window. */
typedef struct MimeWlHandlers {
    /* key is the code of linux/input-event-codes.h. */
    void (*key)(MimeWlWindow *w, uint32_t key, bool down, bool repeat);
    void (*keymap)(void);
    void (*text)(MimeWlWindow *w, const char *utf8);
    /* the text an input method composes, start and length in characters, -1 where it tells none. */
    void (*editing)(MimeWlWindow *w, const char *utf8, int start, int length);
    /* dx and dy are the device's own counts while the pointer is locked, and x and y then stay. */
    void (*motion)(MimeWlWindow *w, double x, double y, double dx, double dy);
    /* button is BTN_LEFT and its neighbours. */
    void (*button)(MimeWlWindow *w, double x, double y, uint32_t button, bool down);
    /* in notches, up and right positive. */
    void (*wheel)(MimeWlWindow *w, double x, double y, double wx, double wy);
    void (*leave)(MimeWlWindow *w);
    void (*focus)(MimeWlWindow *w, bool gained);
    void (*close)(MimeWlWindow *w);
    void (*state)(MimeWlWindow *w, MimeWlState state);
    /* the size changed, or the window can be drawn now. sized tells a change of the size in surface coordinates. */
    void (*resized)(MimeWlWindow *w, bool sized);
    /* a buffer is free again after a frame found none. */
    void (*redraw)(MimeWlWindow *w);
    void (*output)(MimeWlWindow *w, uint32_t output);
    void (*outputs)(bool listed);
} MimeWlHandlers;

bool mime_wl_init(const MimeWlHandlers *handlers);
void mime_wl_quit(void);
/* waits for events up to timeout_ms, -1 for no limit, and dispatches them. false once the connection is gone. */
bool mime_wl_wait(int timeout_ms);
/* any thread: runs fn on the main thread, in order. */
void mime_wl_post(void (*fn)(void *), void *arg);
/* any thread: runs fn on the main thread once, ms from now. */
void mime_wl_timer(uint32_t ms, void (*fn)(void *), void *arg);

struct wl_display *mime_wl_display(void);
struct wl_compositor *mime_wl_compositor(void);
struct wl_subcompositor *mime_wl_subcompositor(void);
struct wl_shm *mime_wl_shm(void);
struct wl_seat *mime_wl_seat(void);
/* a viewport for a surface of the caller, null without the protocol. */
struct wp_viewport *mime_wl_viewport(struct wl_surface *surface);
void mime_wl_viewport_size(struct wp_viewport *viewport, int width, int height);
void mime_wl_viewport_destroy(struct wp_viewport *viewport);
/* the serial of the last key or button press, 0 before one. */
uint32_t mime_wl_serial(void);
/* the serial a selection is set with: a program that copies before any press has that of the keyboard's arrival. */
uint32_t mime_wl_selection_serial(void);
bool mime_wl_has_text_input(void);
/* false on a desktop that has no place for a minimized window, as a tiling one. */
bool mime_wl_can_minimize(void);

typedef struct MimeWlOutput {
    uint32_t id;
    struct wl_output *output;
    int x, y, width, height; /* in the compositor's coordinates. */
    double scale;
    double refresh;
    char name[32];
} MimeWlOutput;

/* the outputs, the primary one first. */
uint32_t mime_wl_outputs(MimeWlOutput *out, uint32_t capacity);

/* x and y place a popup on its parent. the window is hidden until shown. */
MimeWlWindow *mime_wl_window_create(MimeWlKind kind, MimeWlWindow *parent, int x, int y, int width, int height,
                                    void *user);
void mime_wl_window_destroy(MimeWlWindow *w);
void *mime_wl_window_user(const MimeWlWindow *w);
struct wl_surface *mime_wl_window_surface(const MimeWlWindow *w);
MimeWlWindow *mime_wl_window_of(const struct wl_surface *surface);
void mime_wl_window_show(MimeWlWindow *w);
void mime_wl_window_hide(MimeWlWindow *w);
void mime_wl_window_title(MimeWlWindow *w, const char *title);
void mime_wl_window_app_id(MimeWlWindow *w, const char *app_id);
void mime_wl_window_parent(MimeWlWindow *w, MimeWlWindow *parent);
/* the size in surface coordinates, which a top level window the compositor sizes takes once it is free again. */
void mime_wl_window_size(MimeWlWindow *w, int width, int height);
void mime_wl_window_get_size(const MimeWlWindow *w, int *width, int *height);
void mime_wl_window_get_pixels(const MimeWlWindow *w, int *width, int *height);
double mime_wl_window_scale(const MimeWlWindow *w);
/* 0 for no limit. a window that is not resizable keeps its size whatever the limits. */
void mime_wl_window_limits(MimeWlWindow *w, int min_w, int min_h, int max_w, int max_h, bool resizable);
void mime_wl_window_geometry(MimeWlWindow *w, int width, int height);
void mime_wl_window_position(MimeWlWindow *w, int x, int y);
void mime_wl_window_minimize(MimeWlWindow *w);
/* forgets that the window was minimized, for one that is shown again. */
void mime_wl_window_unminimize(MimeWlWindow *w);
bool mime_wl_window_minimized(const MimeWlWindow *w);
void mime_wl_window_maximize(MimeWlWindow *w, bool maximized);
bool mime_wl_window_maximized(const MimeWlWindow *w);
/* output 0 leaves the choice to the compositor. */
void mime_wl_window_fullscreen(MimeWlWindow *w, bool fullscreen, uint32_t output);
uint32_t mime_wl_window_output(const MimeWlWindow *w);
void mime_wl_window_resizable(MimeWlWindow *w, bool resizable);
/* hands the window to the compositor to move or size with the pointer, with the serial of the press. */
bool mime_wl_window_move(MimeWlWindow *w, uint32_t serial);
bool mime_wl_window_resize(MimeWlWindow *w, uint32_t serial, uint32_t edge);
/* straight alpha, blue first in memory. */
void mime_wl_window_icon(MimeWlWindow *w, const uint8_t *pixels, int width, int height);
/* where text is entered, for the popups of an input method. */
void mime_wl_window_text_area(MimeWlWindow *w, int x, int y, int width, int height);
void mime_wl_text_reset(void);

MimeWlWindow *mime_wl_keyboard_focus(void);
MimeWlWindow *mime_wl_pointer_focus(void);
uint32_t mime_wl_mods(void);
/* the character a key types at a level of the layout, 0 base, 1 shifted, 2 the third level. 0 for none. */
uint32_t mime_wl_key_char(uint32_t key, int level);

/* locks the pointer to the window, which then gets motion alone, or lets it go. */
void mime_wl_pointer_lock(MimeWlWindow *w, bool locked);
bool mime_wl_pointer_locked(const MimeWlWindow *w);
/* keeps the pointer inside a rectangle of the window, a null one lets it go. */
void mime_wl_pointer_confine(MimeWlWindow *w, const MimeRect *rect);

/* scale is the count of image pixels a surface coordinate takes. straight alpha, blue first in memory. */
MimeWlCursor *mime_wl_cursor_create(const uint8_t *pixels, int width, int height, int hot_x, int hot_y, double scale);
void mime_wl_cursor_destroy(MimeWlCursor *cursor);
/* a cursor of the caller, or a shape of the theme when it is null. a shape of MIME_WL_SHAPES or more hides it. */
void mime_wl_cursor_set(MimeWlCursor *cursor, uint32_t shape);

/* a buffer of the window's size in pixels, premultiplied alpha, blue first in memory. */
typedef struct MimeWlFrame {
    uint8_t *pixels;
    int width, height, stride;
    MimeRect stale; /* what of it does not hold the frame before, the whole when it is new. */
    void *buffer;
} MimeWlFrame;

/*
 * takes a buffer for the next frame. damage is what the frame changes from the one before, null for the whole. false
 * when the window cannot be drawn yet, or when no buffer is free: redraw then tells when one is.
 */
bool mime_wl_window_frame(MimeWlWindow *w, const MimeRect *damage, MimeWlFrame *frame);
void mime_wl_window_commit(MimeWlWindow *w, const MimeWlFrame *frame, const MimeRect *damage);
/* whether the surface holds a buffer of the size the window has now. */
bool mime_wl_window_attached(const MimeWlWindow *w);
/* nothing of the buffers holds the frame before: the next one is drawn whole. */
void mime_wl_window_stale(MimeWlWindow *w);
/* a commit of the surface with no new buffer, for the state of its subsurfaces. */
void mime_wl_window_commit_state(MimeWlWindow *w);

#endif
