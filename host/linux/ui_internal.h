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

/* clipboard.c, called by the main loop. */
void mime_clipboard_init(void);
void mime_clipboard_event(const SDL_ClipboardEvent *e);

#endif
