/*
 * what the parts of the linux display share: ui.c owns the windows and the main loop, the others add the
 * clipboard, drag and drop, the text input and the tray.
 */
#ifndef MIME_LINUX_UI_INTERNAL_H
#define MIME_LINUX_UI_INTERNAL_H

#include "../ui/ui.h"

#include <SDL3/SDL.h>

/* queues an input event for the guest, stamped with the time. any thread. */
void mime_ui_emit(MimeInputEvent *ev);
/* runs fn on the main thread, at once when already there. false and nothing run without a display. */
bool mime_ui_call_main(void (*fn)(void *), void *arg);
/* whether a display server is there. */
bool mime_ui_has_video(void);

/* the modifier keys held, as MIME_MOD_* bits. */
uint32_t mime_ui_modifiers(void);
/*
 * the guest window and the guest screen position of a point of one of its host surfaces, in surface coordinates;
 * false when the surface is none of the windows' that shows.
 */
bool mime_ui_surface_point(void *wl_surface, double x, double y, uint32_t *window, int32_t *gx, int32_t *gy);
/* the host surface of a guest window, null when it has none. */
void *mime_ui_window_surface(uint32_t window);

/*
 * asks the guest for the data of a kind, by an event of the type given (MIME_INPUT_CLIP_REQUEST or
 * MIME_INPUT_DND_REQUEST), and waits for its chunks, up to timeout_ms after the last. any thread. the bytes come
 * from malloc, null when the guest has none.
 */
uint8_t *mime_ui_request_guest(uint32_t event, uint32_t kind, int timeout_ms, size_t *size);

/* dnd.c, called by the main loop once the display is up. */
void mime_dnd_init(void);

/* clipboard.c, called by the main loop. */
void mime_clipboard_init(void);
void mime_clipboard_event(const SDL_ClipboardEvent *e);

#endif
