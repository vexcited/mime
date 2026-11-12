/* native display handles and guest frame surfaces for the Vulkan presenter. */
#ifndef MIME_VULKAN_DISPLAY_H
#define MIME_VULKAN_DISPLAY_H

#include "../ui/ui.h"


typedef struct MimeUiNative {
    bool x11;        /* else Wayland, or Metal on macOS. */
    bool metal;
    bool subsurface; /* a surface over the client area alone, the frame drawn by the window system around it. */
    void *display;
    void *surface;   /* the wl_surface, or the CAMetalLayer. */
    uint64_t window; /* the X11 window. */
} MimeUiNative;

/* where a window that presents lies in its host window, in window relative pixels of its own. */
typedef struct MimeUiPlace {
    MimeRect client;
    MimeRect visible; /* the part of the window that shows it, which a window that shrank holds past its client area. */
    uint32_t order;
    uint32_t stamp;   /* the guest's count of what it told, 0 without one. */
    bool follows;     /* the pixels of the host window that were painted around the place come next, and it waits for them. */
} MimeUiPlace;

typedef struct MimeUiFrame {
    uint32_t pixel_width, pixel_height; /* the surface: the part of the client area that shows, or the whole host window. */
    MimeRect host;                      /* window relative part the host window covers. */
    MimeRect visible;
    MimeRect client;                    /* window relative place of the image, in screen pixels. */
    MimeRect area;                      /* and the part of the window the surface of a layer takes, the same. */
    uint32_t surface_width, surface_height; /* the guest surface. */
    double scale;                           /* host window pixels per guest pixel. */
    double origin_x, origin_y;              /* where the host part starts, in host window pixels. */
    bool subsurface;
    uint32_t serial; /* counts the frames of the layer. */
    bool shown;
} MimeUiFrame;

/*
 * Wayland uses a subsurface for each window of the guest that presents, over the part of its client area that shows
 * and excluded from SDL rendering, macOS a Metal layer over the client area; X11 uses the whole window and draws its
 * frame. attach waits for the main thread. rectangles are window-relative guest pixels of the window's own, order is
 * the window's place in the paint order of its tree. without layers a window has one presenter and source is 0.
 */
bool mime_ui_vk_layers(void);
bool mime_ui_vk_attach(uint32_t id, uint32_t source, MimeRect area, uint32_t order, MimeUiNative *native);
void mime_ui_vk_detach(uint32_t id, uint32_t source);
bool mime_ui_vk_frame(uint32_t id, uint32_t source, const MimeUiPlace *place, MimeRect image, MimeUiFrame *frame);
/* the guest told where a window that presents lies now, between its frames. */
void mime_ui_vk_place(uint32_t id, uint32_t source, const MimeUiPlace *place);
/* the frame of a layer was handed to the display. */
void mime_ui_vk_shown(uint32_t id, uint32_t source, uint32_t serial, bool shown);
/* copies changes from a guest surface of the given dimensions; returns false if unchanged. */
bool mime_ui_vk_copy(uint32_t id, uint8_t *dst, uint32_t width, uint32_t height, MimeRect *dirty);

/* called on the main thread before destroying a window the presenter may own. */
void mime_vk_window_lost(uint32_t id);
/* called on the main thread when a window hides: its presenters free what is the window's size until it shows. */
void mime_vk_window_hidden(uint32_t id);

/*
 * serializes device and window-surface teardown: concurrent destruction can deadlock the NVIDIA driver's internal
 * locks.
 */
void mime_gpu_teardown_enter(void);
void mime_gpu_teardown_leave(void);

#endif
