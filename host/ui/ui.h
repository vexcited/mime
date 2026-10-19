/*
 * host side of the macos display: one borderless NSWindow per guest top
 * level window, fed with pixels rendered by the guest window manager.
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
 * the compositor moves (edge 0) or sizes (a WMSZ_* edge) a window with the pointer, whose button of a press is
 * still held: MIME_HC_STATUS_OK when it took over, unsupported where windows are moved by the guest.
 */
uint64_t mime_ui_window_movesize(uint32_t window, uint32_t edge);
/* the monitors, the primary first, at most capacity of them: how many there are. */
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
/* the last image presented to a window through vulkan: see MimeWinReadback. */
bool mime_ui_window_readback(MimeWinReadback *r, void *pixels);
void mime_ui_set_cursor(uint32_t cursor);
void mime_ui_pointer(uint32_t mode, uint32_t window_id, const MimeRect *rect);
void mime_ui_pad_rumble(uint32_t pad, uint16_t low, uint16_t high);
/* the display's gamma ramp, three channels of 256 values; null for the identity. */
void mime_ui_gamma(const uint16_t *ramp);
void mime_ui_beep(uint32_t type);

/*
 * a CAMetalLayer over part of a window, in guest pixels relative to the
 * window's surface, showing gl's bottom first rows upright. window_id 0
 * hides it.
 */
void *mime_ui_metal_layer_create(void *device);
void mime_ui_metal_layer_place(void *layer, uint32_t window_id, int32_t x, int32_t y, uint32_t width,
                               uint32_t height, bool sync);
void mime_ui_metal_layer_destroy(void *layer);

/* clipboard text in utf-16 with windows line endings. */
void mime_ui_clipboard_set(const uint16_t *text, size_t chars);
size_t mime_ui_clipboard_read(uint16_t *dst, size_t offset, size_t count);

/*
 * the clipboard beyond text, see MIME_CLIP_* in abi.h. each returns a MIME_HC_STATUS_*; the macos display has none
 * of this and answers unsupported, so the guest keeps to text there.
 *
 * offer: the guest owns the clipboard and renders these kinds when asked, none to withdraw. fetch: the bytes of
 * what the host clipboard holds in a kind, from offset, up to capacity; total is its whole size, 0 when there is
 * none, and the generation says which host contents the guest means. provide: a chunk of the answer to a request
 * the host made through MIME_INPUT_CLIP_REQUEST, MIME_CLIP_NONE as total for no data.
 */
uint64_t mime_ui_clipboard_offer(uint32_t mask);
uint64_t mime_ui_clipboard_fetch(uint32_t kind, uint32_t generation, void *dst, uint64_t capacity, uint64_t offset,
                                 uint64_t *total);
uint64_t mime_ui_clipboard_provide(uint32_t request, const void *src, uint64_t length, uint64_t offset,
                                   uint64_t total);

/*
 * the input method of the desktop, see MIME_INPUT_IME: the oldest queued text (utf-16, at most count units, the
 * return value is its length), where text is entered in a window (window relative guest pixels), and ending a
 * composition or asking whether the host has an input method protocol. each returns a MIME_HC_STATUS_*.
 */
uint64_t mime_ui_ime_text(uint16_t *dst, size_t capacity, uint64_t *length);
uint64_t mime_ui_ime_area(uint32_t window, const MimeRect *area, int32_t cursor);
uint64_t mime_ui_ime_control(uint32_t op, uint32_t window, uint64_t *value);

/*
 * drag and drop with the host's programs, see MIME_INPUT_DND_*; each returns a MIME_HC_STATUS_*, unsupported on
 * the macos display. effect and finish answer a host drag over a guest window; fetch reads its data in a kind
 * (the answer to MIME_HC_DND_FETCH is like the clipboard's). start begins a host drag of the guest's own, with the
 * kinds the guest can render; the data is asked for with MIME_INPUT_DND_REQUEST and given to
 * mime_ui_clipboard_provide, and the end comes as MIME_INPUT_DND_END. stop ends it early.
 */
uint64_t mime_ui_dnd_effect(uint32_t session, uint32_t effect);
uint64_t mime_ui_dnd_finish(uint32_t session, uint32_t effect);
uint64_t mime_ui_dnd_fetch(uint32_t session, uint32_t kind, void *dst, uint64_t capacity, uint64_t offset,
                           uint64_t *total);
uint64_t mime_ui_dnd_start(uint32_t kinds, uint32_t effects, uint64_t *session);
uint64_t mime_ui_dnd_stop(uint32_t session);

#endif
