#ifndef MIME_APP_IDENTITY_H
#define MIME_APP_IDENTITY_H

#include "../mime_host.h"

enum {
    MIME_APP_ACTIVATE = 1,
    MIME_APP_CLOSE = 2,
};

typedef void (*MimeAppActionSink)(void *ctx, uint32_t process_id, uint32_t action);

/* these functions and the action callback run on the main thread. */
void mime_app_init(const MimePaths *paths, MimeAppActionSink sink, void *ctx);
void mime_app_window_set(uint32_t window_id, uint32_t process_id, const char *image_path, bool taskbar_visible);
void mime_app_window_remove(uint32_t window_id);
void mime_app_process_exit(uint32_t process_id);
void mime_app_window_icon(uint32_t window_id, const void *pixels, uint32_t width, uint32_t height,
                          uint32_t stride);
void mime_app_shutdown(void);

/* returns -1 unless invoked as a proxy with an inherited parent socket. */
int mime_app_proxy_main(int argc, char **argv);

#endif
