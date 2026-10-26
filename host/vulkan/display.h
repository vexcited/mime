/* native display handles and guest frame surfaces for the Vulkan presenter. */
#ifndef MIME_LINUX_DISPLAY_H
#define MIME_LINUX_DISPLAY_H

#include "../ui/ui.h"


typedef struct MimeUiNative {
    bool x11;        /* else Wayland. */
    bool subsurface; /* a surface over the client area alone, the frame drawn by SDL around it. */
    void *display;
    void *surface;   /* the wl_surface. */
    uint64_t window; /* the X11 window. */
} MimeUiNative;

typedef struct MimeUiFrame {
    uint32_t pixel_width, pixel_height; /* the surface: the client area, or the whole host window. */
    MimeRect host;                      /* window relative part the host window covers. */
    MimeRect visible;
    uint32_t surface_width, surface_height; /* the guest surface. */
    double scale;                           /* host window pixels per guest pixel. */
    double origin_x, origin_y;              /* where the host part starts, in host window pixels. */
    bool subsurface;
    bool shown;
} MimeUiFrame;

/*
 * Wayland uses a client-area subsurface excluded from SDL rendering; X11 uses the whole window and draws its frame.
 * attach waits for the main thread. client coordinates are window-relative guest pixels.
 */
bool mime_ui_vk_attach(uint32_t id, MimeRect client, MimeUiNative *native);
void mime_ui_vk_detach(uint32_t id);
bool mime_ui_vk_frame(uint32_t id, MimeRect client, MimeUiFrame *frame);
/* copies changes from a guest surface of the given dimensions; returns false if unchanged. */
bool mime_ui_vk_copy(uint32_t id, uint8_t *dst, uint32_t width, uint32_t height, MimeRect *dirty);

/* called on the main thread before destroying a window the presenter may own. */
void mime_vk_window_lost(uint32_t id);

/*
 * serializes device and window-surface teardown: concurrent destruction can deadlock the NVIDIA driver's internal
 * locks.
 */
void mime_gpu_teardown_enter(void);
void mime_gpu_teardown_leave(void);

#endif
