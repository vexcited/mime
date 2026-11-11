#include "ui_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#define PRESENCE_VARIABLE "MIME_PRESENCE"
#define PRESENCE_FD 3
#define MAX_SHADOWS 128
#define MAX_WINDOWS 1024

typedef struct Shadow {
    bool used;
    uint32_t pid;
    uint32_t windows;
    pid_t host; /* -1 when it could not start, so it is not tried again for each configuration. */
    int fd;
} Shadow;

typedef struct Window {
    bool used;
    uint32_t id, pid;
} Window;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static Shadow g_shadows[MAX_SHADOWS];
static Window g_windows[MAX_WINDOWS];

int mime_ui_presence_helper(int argc, char **argv)
{
    if (!getenv(PRESENCE_VARIABLE) || argc < 1)
        return -1;
    const char *name = strrchr(argv[0], '\\');
    prctl(PR_SET_NAME, name ? name + 1 : argv[0]);
    /* the pipe ends with the machine or when the window goes. */
    char c;
    while (read(PRESENCE_FD, &c, 1) < 0 && errno == EINTR) {}
    return 0;
}

static bool system_image(const uint16_t *image, uint32_t chars)
{
    static const char prefix[] = "c:\\windows\\";
    if (chars < sizeof(prefix) - 1)
        return false;
    for (size_t i = 0; i < sizeof(prefix) - 1; i++) {
        uint16_t c = image[i] >= 'A' && image[i] <= 'Z' ? image[i] + 32 : image[i];
        if (c != (uint8_t)prefix[i])
            return false;
    }
    return true;
}

/*
 * Discord finds games in the command lines of /proc and matches the end of the first argument, backslashes and all,
 * as it does for Wine. a guest process with a window gets an idle host process named after its Windows path.
 */
static void start_shadow(Shadow *s, const uint16_t *image, uint32_t chars)
{
    char path[1536];
    mime_utf16_to_utf8(path, sizeof(path), image, chars);
    s->host = -1;
    s->fd = -1;
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0)
        return;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[0], PRESENCE_FD);
    posix_spawn_file_actions_addclosefrom_np(&actions, PRESENCE_FD + 1);
    char *argv[] = {path, nullptr};
    char *envp[] = {PRESENCE_VARIABLE "=1", nullptr};
    pid_t pid;
    int r = posix_spawn(&pid, "/proc/self/exe", &actions, nullptr, argv, envp);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[0]);
    if (r != 0) {
        close(fds[1]);
        mime_warn("cannot show %s to Discord: %s", path, strerror(r));
        return;
    }
    s->host = pid;
    s->fd = fds[1];
}

static void stop_shadow(Shadow *s)
{
    if (s->host > 0) {
        close(s->fd);
        kill(s->host, SIGKILL);
        while (waitpid(s->host, nullptr, 0) < 0 && errno == EINTR) {}
    }
    s->used = false;
}

static Shadow *find_shadow(uint32_t pid)
{
    for (int i = 0; i < MAX_SHADOWS; i++)
        if (g_shadows[i].used && g_shadows[i].pid == pid)
            return &g_shadows[i];
    return nullptr;
}

/* lock held. */
static void forget_window(Window *w)
{
    w->used = false;
    Shadow *s = find_shadow(w->pid);
    if (s && --s->windows == 0)
        stop_shadow(s);
}

void mime_presence_window(uint32_t id, uint32_t pid, const uint16_t *image, uint32_t chars)
{
    if (!pid || !chars || chars > 512 || system_image(image, chars))
        return;
    pthread_mutex_lock(&g_lock);
    Window *w = nullptr, *free_window = nullptr;
    for (int i = 0; i < MAX_WINDOWS && !w; i++) {
        if (g_windows[i].used && g_windows[i].id == id)
            w = &g_windows[i];
        else if (!g_windows[i].used && !free_window)
            free_window = &g_windows[i];
    }
    if (w && w->pid != pid) {
        forget_window(w);
        free_window = w;
        w = nullptr;
    }
    if (!w && free_window) {
        Shadow *s = find_shadow(pid);
        for (int i = 0; i < MAX_SHADOWS && !s; i++)
            if (!g_shadows[i].used) {
                s = &g_shadows[i];
                *s = (Shadow){.used = true, .pid = pid};
                start_shadow(s, image, chars);
            }
        if (s) {
            s->windows++;
            *free_window = (Window){.used = true, .id = id, .pid = pid};
        }
    }
    pthread_mutex_unlock(&g_lock);
}

void mime_presence_window_gone(uint32_t id)
{
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (g_windows[i].used && g_windows[i].id == id)
            forget_window(&g_windows[i]);
    pthread_mutex_unlock(&g_lock);
}

void mime_presence_process_gone(uint32_t pid)
{
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (g_windows[i].used && g_windows[i].pid == pid)
            forget_window(&g_windows[i]);
    pthread_mutex_unlock(&g_lock);
}
