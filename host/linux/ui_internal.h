/*
 * shared Linux display interfaces. ui.c owns windows and the main loop; the other files handle clipboard,
 * drag and drop, text input and tray integration.
 */
#ifndef MIME_LINUX_UI_INTERNAL_H
#define MIME_LINUX_UI_INTERNAL_H

#include "../ui/ui.h"

#include <SDL3/SDL.h>

/* queues an input event for the guest, stamped with the time. any thread. */
void mime_ui_emit(MimeInputEvent *ev);
/* runs fn on the main thread, at once when already there. false and nothing run without a display. */
bool mime_ui_call_main(void (*fn)(void *), void *arg);
bool mime_ui_has_video(void);

/* the modifier keys held, as MIME_MOD_* bits. */
uint32_t mime_ui_modifiers(void);
/*
 * maps a host surface point to a guest window and guest screen position. returns false unless the surface
 * belongs to a visible window.
 */
bool mime_ui_surface_point(void *wl_surface, double x, double y, uint32_t *window, int32_t *gx, int32_t *gy);
/* the host surface of a guest window, null when it has none. */
void *mime_ui_window_surface(uint32_t window);

/*
 * any thread: requests data through MIME_INPUT_CLIP_REQUEST or MIME_INPUT_DND_REQUEST and waits for chunks,
 * allowing timeout_ms after the last chunk. returns malloc-owned bytes, or null if the guest has none.
 */
uint8_t *mime_ui_request_guest(uint32_t event, uint32_t kind, int timeout_ms, size_t *size);

/* launcher.c: the identity of a window and the launcher entry of an application, a MimeShellWindow. */
uint64_t mime_ui_shell_window(const void *command);
/* dialog.c: a MimeShellDialog, shown by kdialog, zenity or SDL in a process of its own so no thread of mime waits. */
uint64_t mime_ui_dialog(const MimeShellDialog *dialog);
/* shell.c: a progress view of plasma's job server for MIME_DIALOG_PROGRESS, UPDATE and END, false without one. */
bool mime_shell_job(const MimeShellDialog *dialog, const char *text);
/* application id and launcher entry name: "mime." plus the user model id, or "mime" when absent. */
void mime_app_id_for(const char *aumid, char *out, size_t size);
/* launcher notifications for window configuration, window destruction and process exit. */
void mime_launcher_window_seen(uint32_t id, uint32_t process_id);
void mime_launcher_window_gone(uint32_t id);
void mime_launcher_process_gone(uint32_t process_id);
/* sets a toplevel application id or flashes it: 0 stops, 1 briefly, 2 until raised. */
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

/* main thread: requests attention or raises a surface using an activation token. */
struct wl_display;
struct wl_surface;
bool mime_activation_attention(struct wl_display *display, struct wl_surface *surface, const char *app_id);
bool mime_activation_activate(struct wl_display *display, struct wl_surface *surface, const char *token);
/*
 * main thread: what the compositor tells of an output's colors, false when it tells nothing. changed runs on a
 * thread of its own when an answer is no longer the one given.
 */
struct wl_output;
bool mime_color_output(struct wl_display *display, struct wl_output *output, bool *hdr, MimeMonitorColor *color);
void mime_color_watch(void (*changed)(void));
void mime_color_forget(void);
void mime_color_stop(void);
/* last notification click token, for the window raised in response. */
void mime_ui_set_activation_token(const char *token);

/* publishes guest shortcuts in the applications menu on request. */
void mime_desktop_jump_list(const MimeShellJumpList *list);

/* ime.c, called by the main loop. */
void mime_ime_editing(uint32_t window, const char *text, int start, int length);
bool mime_ime_commit(uint32_t window, const char *text);
void mime_ime_flush(void);
bool mime_ime_pending(void);
/* text entry rectangle for input method popups, in window-relative guest pixels. */
void mime_ui_set_text_area(uint32_t window, MimeRect area, int32_t cursor);
void mime_ui_clear_composition(uint32_t window);
/* ui.c: the integer scale of a surface the host shows, such as the icon of a drag. */
int mime_ui_buffer_scale(void);
/* dnd.c: whether the compositor has the text input protocol. */
bool mime_ui_has_text_input(void);

/* returns the wl_seat and last pointer press serial, or false before the first press. */
bool mime_ui_pointer_press(void **seat, uint32_t *serial);

/* dnd.c, called by the main loop once the display is up. */
void mime_dnd_init(void);

/* clipboard.c, called by the main loop. */
void mime_clipboard_init(void);
void mime_clipboard_event(const SDL_ClipboardEvent *e);

#endif
