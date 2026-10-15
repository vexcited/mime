/*
 * what the vulkan presenter needs of the linux display: the native handles
 * of a host window to make a surface on, its geometry, and the guest
 * surface of the window's frame, which the presenter draws itself while it
 * owns the window.
 */
#ifndef MIME_LINUX_DISPLAY_H
#define MIME_LINUX_DISPLAY_H

#include "../ui/ui.h"

#include <stdbool.h>

typedef struct MimeUiNative {
    bool x11;        /* else wayland */
    bool subsurface; /* a surface over the client area alone, the frame drawn by sdl around it */
    void *display;
    void *surface;   /* the wl_surface */
    uint64_t window; /* the x11 window */
} MimeUiNative;

typedef struct MimeUiFrame {
    uint32_t pixel_width, pixel_height; /* the surface: the client area, or the whole host window */
    MimeRect host;                      /* window relative part the host window covers */
    MimeRect visible;
    uint32_t surface_width, surface_height; /* the guest surface */
    bool subsurface;
    bool shown;
} MimeUiFrame;

/*
 * the presenter's surface on a window. on wayland it is a subsurface over
 * the client area, which sdl's renderer leaves alone; on x11 the presenter
 * takes the whole window and draws the frame too. attach waits for the
 * main thread. client is window relative, in guest pixels.
 */
bool mime_ui_vk_attach(uint32_t id, MimeRect client, MimeUiNative *native);
void mime_ui_vk_detach(uint32_t id);
bool mime_ui_vk_frame(uint32_t id, MimeRect client, MimeUiFrame *frame);
/* copies what changed of the guest surface, if it is width by height; false when nothing did. */
bool mime_ui_vk_copy(uint32_t id, uint8_t *dst, uint32_t width, uint32_t height, MimeRect *dirty);

/* vkwsi.c: called on the main thread before a host window the presenter may own goes away. */
void mime_vk_window_lost(uint32_t id);

#endif
