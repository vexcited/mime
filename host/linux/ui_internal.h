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

/* launcher.c: the identity of a window and the launcher entry of an application, a MimeShellWindow. */
uint64_t mime_ui_shell_window(const void *command);
/* the application id of a program, "mime." and its user model id, "mime" for none: the name of its entry, too. */
void mime_app_id_for(const char *aumid, char *out, size_t size);
/* ui.c, ui.c calls launcher.c: a window was configured, was destroyed, and a process ended. */
void mime_launcher_window_seen(uint32_t id, uint32_t process_id);
void mime_launcher_window_gone(uint32_t id);
void mime_launcher_process_gone(uint32_t process_id);
/* launcher.c calls ui.c: the toplevel of a window gets this application id, or is flashed (0 stop, 1 a moment, 2 until in front). */
void mime_ui_window_app_id(uint32_t window, const char *app_id);
void mime_ui_window_flash(uint32_t window, uint32_t mode);

/* the properties a launcher entry shows, as the bus has them; sent by shell.c. */
typedef struct MimeLauncherUpdate {
    char uri[256];
    bool progress, progress_visible;
    double progress_value;
    bool count;
    int64_t count_value;
    bool urgent, urgent_value;
} MimeLauncherUpdate;
void mime_shell_launcher_update(const MimeLauncherUpdate *update);

/* activation.c, on the main thread: the compositor is asked for attention for a surface, or to raise it with a token. */
struct wl_display;
struct wl_surface;
bool mime_activation_attention(struct wl_display *display, struct wl_surface *surface, const char *app_id);
bool mime_activation_activate(struct wl_display *display, struct wl_surface *surface, const char *token);
/* the activation token of the last click on a notification, for the window that comes to the front because of it. */
void mime_ui_set_activation_token(const char *token);

/* desktop.c: the shortcuts of the guest as entries of the applications menu, when asked for. */
void mime_desktop_jump_list(const MimeShellJumpList *list);

/* ime.c, called by the main loop. */
void mime_ime_editing(uint32_t window, const char *text, int start, int length);
bool mime_ime_commit(uint32_t window, const char *text);
void mime_ime_flush(void);
bool mime_ime_pending(void);
/* ui.c: where text is entered in a window, window relative guest pixels, for the input method's popups. */
void mime_ui_set_text_area(uint32_t window, MimeRect area, int32_t cursor);
void mime_ui_clear_composition(uint32_t window);
/* ui.c: the integer scale of a surface the host shows, such as the icon of a drag. */
int mime_ui_buffer_scale(void);
/* dnd.c: whether the compositor has the text input protocol. */
bool mime_ui_has_text_input(void);

/* dnd.c: the wl_seat and the serial of the last press of a pointer button, false before there was one. */
bool mime_ui_pointer_press(void **seat, uint32_t *serial);

/* dnd.c, called by the main loop once the display is up. */
void mime_dnd_init(void);

/* clipboard.c, called by the main loop. */
void mime_clipboard_init(void);
void mime_clipboard_event(const SDL_ClipboardEvent *e);

#endif
