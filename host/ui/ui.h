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

/* any thread. */
void mime_ui_stop(void);
void mime_ui_window_create(uint32_t id);
void mime_ui_window_destroy(uint32_t id);
void mime_ui_window_config(const MimeWinConfig *cfg);
void mime_ui_window_present(uint32_t id, const void *pixels, uint32_t width, uint32_t height, uint32_t stride,
                            MimeRect dirty);
void mime_ui_set_cursor(uint32_t cursor);
void mime_ui_beep(uint32_t type);

#endif
