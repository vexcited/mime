/*
 * the display of a machine without a window system: windows are accepted
 * and drawn nowhere. it lets the machine run programs from a terminal.
 */
#include "../ui/ui.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cond = PTHREAD_COND_INITIALIZER;
static bool g_stop;

void mime_ui_init(MimeUiInputSink sink, void *ctx)
{
}

void mime_ui_run(void)
{
    pthread_mutex_lock(&g_lock);
    while (!g_stop)
        pthread_cond_wait(&g_cond, &g_lock);
    pthread_mutex_unlock(&g_lock);
}

void mime_ui_stop(void)
{
    pthread_mutex_lock(&g_lock);
    g_stop = true;
    pthread_cond_broadcast(&g_cond);
    pthread_mutex_unlock(&g_lock);
}

void mime_ui_get_display(MimeDisplayInfo *info)
{
    memset(info, 0, sizeof(*info));
    info->width = info->work_width = 1920;
    info->height = 1080;
    info->work_height = 1032;
    info->dpi = 96;
    info->refresh_hz = 60;
}

void mime_ui_window_create(uint32_t id)
{
}

void mime_ui_window_destroy(uint32_t id)
{
}

void mime_ui_process_exit(uint32_t process_id)
{
}

void mime_ui_window_config(const MimeWinConfig *cfg)
{
}

void mime_ui_window_icon(uint32_t id, const void *pixels, uint32_t width, uint32_t height, uint32_t stride)
{
}

void mime_ui_window_present(uint32_t id, const void *pixels, uint32_t width, uint32_t height, uint32_t stride,
                            MimeRect dirty)
{
}

void mime_ui_set_cursor(uint32_t cursor)
{
}

void mime_ui_beep(uint32_t type)
{
}

static uint16_t *g_clip;
static size_t g_clip_chars;

void mime_ui_clipboard_set(const uint16_t *text, size_t chars)
{
    pthread_mutex_lock(&g_lock);
    free(g_clip);
    g_clip = malloc((chars ? chars : 1) * sizeof(uint16_t));
    if (g_clip)
        memcpy(g_clip, text, chars * sizeof(uint16_t));
    g_clip_chars = g_clip ? chars : 0;
    pthread_mutex_unlock(&g_lock);
}

size_t mime_ui_clipboard_read(uint16_t *dst, size_t offset, size_t count)
{
    pthread_mutex_lock(&g_lock);
    size_t total = g_clip_chars;
    if (dst && offset < total)
        memcpy(dst, g_clip + offset, MIME_MIN(count, total - offset) * sizeof(uint16_t));
    pthread_mutex_unlock(&g_lock);
    return total;
}

/* macos runs per program app bundles through this entry; linux desktops need none. */
int mime_app_proxy_main(int argc, char **argv)
{
    return -1;
}
