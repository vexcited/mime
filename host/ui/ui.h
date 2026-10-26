/*
 * host display interface. on macOS, each guest top level window has a borderless NSWindow showing
 * guest-rendered pixels.
 */
#ifndef MIME_UI_H
#define MIME_UI_H

#include "../mime_host.h"
#include "../../include/mime/abi.h"

typedef void (*MimeUiInputSink)(void *ctx, const MimeInputEvent *ev);

/* main thread only. */
void mime_ui_init(MimeUiInputSink sink, void *ctx);
void mime_ui_run(void);
void mime_ui_get_display(MimeDisplayInfo *info);
/*
 * asks the compositor to move with edge 0 or resize with a WMSZ_* edge while the pointer button remains held.
 * returns MIME_HC_STATUS_OK if accepted, unsupported when the guest handles movement.
 */
uint64_t mime_ui_window_movesize(uint32_t window, uint32_t edge);
/*
 * queues a MimeTrayCommand for the desktop notification area. returns MIME_HC_STATUS_OK when queued,
 * unsupported without a desktop bus. replies arrive as MIME_INPUT_TRAY.
 */
uint64_t mime_ui_tray(const MimeTrayCommand *command);
/*
 * queues MimeShellToast or MimeShellWindow commands. returns MIME_HC_STATUS_OK when queued, unsupported
 * without a desktop bus. value holds a probe result or reply length; replies arrive as MIME_INPUT_SHELL. may
 * modify the command.
 */
uint64_t mime_ui_shell(void *command, size_t size, uint64_t *value);
/* the guest's shortcuts as entries of the desktop's applications menu, if the user asked for it; any thread, once. */
void mime_ui_desktop_start(const MimePaths *paths);
/* returns the monitor count and fills at most capacity entries, primary first. */
uint32_t mime_ui_get_monitors(MimeMonitor *monitors, uint32_t capacity);

/* any thread. */
void mime_ui_stop(void);
void mime_ui_window_create(uint32_t id);
void mime_ui_window_destroy(uint32_t id);
void mime_ui_process_exit(uint32_t process_id);
void mime_ui_window_config(const MimeWinConfig *cfg);
void mime_ui_window_icon(uint32_t id, const void *pixels, uint32_t width, uint32_t height, uint32_t stride);
void mime_ui_window_present(uint32_t id, const void *pixels, uint32_t width, uint32_t height, uint32_t stride,
                            MimeRect dirty);
/* the last image presented to a window through Vulkan: see MimeWinReadback. */
bool mime_ui_window_readback(MimeWinReadback *r, void *pixels);
void mime_ui_set_cursor(uint32_t cursor);
void mime_ui_pointer(uint32_t mode, uint32_t window_id, const MimeRect *rect);
void mime_ui_pad_rumble(uint32_t pad, uint16_t low, uint16_t high);
/* the display's gamma ramp, three channels of 256 values; null for the identity. */
void mime_ui_gamma(const uint16_t *ramp);
void mime_ui_beep(uint32_t type);

/*
 * places a CAMetalLayer over a window-relative rectangle in guest pixels. a flipped layer shows OpenGL bottom-first
 * rows upright. window_id 0 hides it.
 */
void *mime_ui_metal_layer_create(void *device, bool flipped);
void mime_ui_metal_layer_place(void *layer, uint32_t window_id, int32_t x, int32_t y, uint32_t width,
                               uint32_t height, bool sync);
void mime_ui_metal_layer_destroy(void *layer);

/* clipboard text in UTF-16 with windows line endings. */
void mime_ui_clipboard_set(const uint16_t *text, size_t chars);
size_t mime_ui_clipboard_read(uint16_t *dst, size_t offset, size_t count);

/*
 * clipboard formats beyond text, using MIME_CLIP_* and MIME_HC_STATUS_*. macOS returns unsupported, leaving
 * the guest with text only.
 *
 * offer declares formats the guest can render; none withdraws ownership. fetch reads up to capacity bytes
 * from offset for the selected generation; total is the full size or 0 if absent. provide answers
 * MIME_INPUT_CLIP_REQUEST in chunks, with MIME_CLIP_NONE as total when no data exists.
 */
uint64_t mime_ui_clipboard_offer(uint32_t mask);
uint64_t mime_ui_clipboard_fetch(uint32_t kind, uint32_t generation, void *dst, uint64_t capacity, uint64_t offset,
                                 uint64_t *total);
uint64_t mime_ui_clipboard_provide(uint32_t request, const void *src, uint64_t length, uint64_t offset,
                                   uint64_t total);

/*
 * desktop input method interface using MIME_INPUT_IME and MIME_HC_STATUS_*. text fetches the oldest queued
 * UTF-16 string, up to count units, and returns its length. area sets the window-relative guest rectangle for
 * popups. control ends composition or probes protocol support.
 */
uint64_t mime_ui_ime_text(uint16_t *dst, size_t capacity, uint64_t *length);
uint64_t mime_ui_ime_area(uint32_t window, const MimeRect *area, int32_t cursor);
uint64_t mime_ui_ime_control(uint32_t op, uint32_t window, uint64_t *value);

/*
 * host drag and drop using MIME_INPUT_DND_* and MIME_HC_STATUS_*; unsupported on macOS. effect and finish
 * answer a host drag; fetch reads its format data with clipboard-style results. start advertises guest
 * formats, requested through MIME_INPUT_DND_REQUEST and supplied by mime_ui_clipboard_provide. completion
 * arrives as MIME_INPUT_DND_END; stop ends early. icon is a copied MimeDragIcon of icon_bytes bytes, or null,
 * shown beside the pointer.
 */
uint64_t mime_ui_dnd_effect(uint32_t session, uint32_t effect);
uint64_t mime_ui_dnd_finish(uint32_t session, uint32_t effect);
uint64_t mime_ui_dnd_fetch(uint32_t session, uint32_t kind, void *dst, uint64_t capacity, uint64_t offset,
                           uint64_t *total);
uint64_t mime_ui_dnd_start(uint32_t kinds, uint32_t effects, const MimeDragIcon *icon, size_t icon_bytes,
                           uint64_t *session);
uint64_t mime_ui_dnd_stop(uint32_t session);

#endif
