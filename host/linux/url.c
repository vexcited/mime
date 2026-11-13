#include "ui_internal.h"

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

#define ENTRY_PREFIX "mime-protocol-"
#define ENTRY_MARK "\nX-Mime-Protocol=true\n"
#define SCHEME_CHARS 40

typedef struct Protocols {
    uint32_t count;
    bool full; /* the guest had more than the list holds, so an entry that is not in it may still be answered. */
    char scheme[MIME_PROTOCOLS][SCHEME_CHARS];
    char program[MIME_PROTOCOLS][MIME_PATH_MAX]; /* the guest path of the program that answers. */
    struct Protocols *all;
} Protocols;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* runs a tool of the desktop to its end, with what it prints when out is given. false when it did not start. */
static bool run_tool(char *const argv[], char *out, size_t size)
{
    int fds[2] = {-1, -1};
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (out) {
        out[0] = 0;
        if (pipe(fds) != 0) {
            posix_spawn_file_actions_destroy(&actions);
            return false;
        }
        posix_spawn_file_actions_adddup2(&actions, fds[1], 1);
        posix_spawn_file_actions_addclose(&actions, fds[0]);
        posix_spawn_file_actions_addclose(&actions, fds[1]);
    }
    pid_t pid;
    int r = posix_spawnp(&pid, argv[0], &actions, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (out)
        close(fds[1]);
    if (r != 0) {
        if (out)
            close(fds[0]);
        mime_warn("url: %s did not start: %s", argv[0], strerror(r));
        return false;
    }
    if (out) {
        size_t n = 0;
        for (;;) {
            char chunk[256];
            ssize_t got = read(fds[0], chunk, sizeof(chunk));
            if (got < 0 && errno == EINTR)
                continue;
            if (got <= 0)
                break;
            size_t take = (size_t)got < size - 1 - n ? (size_t)got : size - 1 - n;
            memcpy(out + n, chunk, take);
            n += take;
        }
        while (n && (out[n - 1] == '\n' || out[n - 1] == ' '))
            n--;
        out[n] = 0;
        close(fds[0]);
    }
    while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
    return true;
}

static void *open_thread(void *arg)
{
    char *argv[] = {"xdg-open", arg, nullptr};
    run_tool(argv, nullptr, 0);
    free(arg);
    return nullptr;
}

/* only a web address: a file or any other scheme would let a program of the machine run things of the host. */
uint64_t mime_ui_url(const MimeShellUrl *url)
{
    size_t size = sizeof(url->url) / sizeof(url->url[0]) * 3 + 1;
    char *text = calloc(1, size);
    if (!text)
        return MIME_HC_STATUS_NO_MEMORY;
    mime_utf16_to_utf8(text, size, url->url, sizeof(url->url) / sizeof(url->url[0]));
    bool web = !strncasecmp(text, "http://", 7) || !strncasecmp(text, "https://", 8);
    for (const unsigned char *c = (const unsigned char *)text; *c && web; c++)
        web = *c > 0x20 && *c != 0x7F;
    if (!web) {
        free(text);
        return MIME_HC_STATUS_INVALID;
    }
    mime_info("url: %.120s%s goes to the browser of the desktop", text, strlen(text) > 120 ? "..." : "");
    pthread_t thread;
    if (pthread_create(&thread, nullptr, open_thread, text) != 0) {
        free(text);
        return MIME_HC_STATUS_NO_MEMORY;
    }
    pthread_detach(thread);
    return MIME_HC_STATUS_OK;
}

/* the schemes the desktop and its browser keep: a guest program does not take them over. */
static bool reserved(const char *scheme)
{
    static const char *const names[] = {"http", "https", "file", "ftp", "mailto", "about", "data", "blob",
                                        "javascript", "view-source", "ws", "wss", "tel", "sms"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (!strcmp(scheme, names[i]))
            return true;
    return false;
}

static bool valid(const char *scheme)
{
    size_t n = 0;
    for (; scheme[n]; n++) {
        char c = scheme[n];
        if (!(c >= 'a' && c <= 'z') && !(n && ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.')))
            return false;
    }
    return n >= 2;
}

static bool listed(const Protocols *p, const char *scheme)
{
    for (uint32_t i = 0; i < p->count; i++)
        if (!strcmp(p->scheme[i], scheme))
            return true;
    return false;
}

/* true when the entry was written or changed. */
static bool write_entry(const char *path, const char *exe, const char *scheme)
{
    char exec[MIME_PATH_MAX * 2] = "", text[MIME_PATH_MAX * 3];
    mime_desktop_argument(exec, sizeof(exec), exe);
    int n = snprintf(text, sizeof(text),
                     "[Desktop Entry]\nType=Application\nVersion=1.1\nName=Mime (%s links)\nExec=%s run %%u\n"
                     "Terminal=false\nNoDisplay=true\nMimeType=x-scheme-handler/%s;\nX-Mime-Protocol=true\n",
                     scheme, exec, scheme);
    if (n <= 0 || (size_t)n >= sizeof(text))
        return false;
    size_t length;
    char *old = mime_read_file(path, &length);
    bool same = old && length == (size_t)n && !memcmp(old, text, length);
    free(old);
    return !same && mime_write_file(path, text, (size_t)n);
}

/* whether the program the desktop opens the scheme with is none, or an entry mime wrote. */
static bool ours(const char *dir, const char *scheme)
{
    char type[SCHEME_CHARS + 32], handler[256], path[MIME_PATH_MAX * 2];
    snprintf(type, sizeof(type), "x-scheme-handler/%s", scheme);
    char *argv[] = {"xdg-mime", "query", "default", type, nullptr};
    if (!run_tool(argv, handler, sizeof(handler)))
        return false;
    if (!handler[0])
        return true;
    if (strchr(handler, '/'))
        return false;
    snprintf(path, sizeof(path), "%s/%s", dir, handler);
    size_t length;
    char *text = mime_read_file(path, &length);
    bool mark = text && (strstr(text, ENTRY_MARK) || strstr(text, "\nX-Mime-Managed=true\n"));
    free(text);
    return mark;
}

/*
 * an entry of the applications directory that names the scheme as its type makes mime the program the desktop opens
 * such an address with. a scheme that a program of the host answers is left to it. with the shortcuts of the guest
 * exported, the scheme goes on the entry of its program, which has a name and an icon to show. the hidden entry is
 * for what is left: the shortcuts are not exported, or the program has none.
 */
static void *protocols_thread(void *arg)
{
    Protocols *p = arg;
    char data[MIME_PATH_MAX], dir[MIME_PATH_MAX + 16], exe[MIME_PATH_MAX];
    mime_desktop_data(data, sizeof(data));
    snprintf(dir, sizeof(dir), "%s/applications", data);
    pthread_mutex_lock(&g_lock);
    if (mime_desktop_exe(exe, sizeof(exe)) && mime_mkdir_p(dir, 0755)) {
        uint32_t kept = 0;
        for (uint32_t i = 0; i < p->count; i++)
            if (ours(dir, p->scheme[i])) {
                if (kept != i) {
                    memcpy(p->scheme[kept], p->scheme[i], sizeof(p->scheme[0]));
                    memcpy(p->program[kept], p->program[i], sizeof(p->program[0]));
                }
                kept++;
            }
        p->count = kept;
        bool taken[MIME_PROTOCOLS];
        bool changed = mime_desktop_protocols(p->count, p->scheme, p->program, taken);
        uint32_t hidden = 0;
        for (uint32_t i = 0; i < p->count; i++) {
            if (taken[i])
                continue;
            char path[MIME_PATH_MAX * 2];
            snprintf(path, sizeof(path), "%s/" ENTRY_PREFIX "%s.desktop", dir, p->scheme[i]);
            if (access(path, F_OK) != 0)
                mime_info("url: %s:// addresses of the desktop open in the machine", p->scheme[i]);
            changed |= write_entry(path, exe, p->scheme[i]);
            if (hidden != i)
                memcpy(p->scheme[hidden], p->scheme[i], sizeof(p->scheme[0]));
            hidden++;
        }
        p->count = hidden;
        DIR *d = opendir(dir);
        for (struct dirent *e; d && (e = readdir(d));) {
            size_t n = strlen(e->d_name), prefix = sizeof(ENTRY_PREFIX) - 1;
            char scheme[SCHEME_CHARS], path[MIME_PATH_MAX * 2];
            if (n <= prefix + 8 || n - prefix - 8 >= sizeof(scheme) || strncmp(e->d_name, ENTRY_PREFIX, prefix) ||
                strcmp(e->d_name + n - 8, ".desktop"))
                continue;
            memcpy(scheme, e->d_name + prefix, n - prefix - 8);
            scheme[n - prefix - 8] = 0;
            if (listed(p, scheme) || (p->full && !listed(p->all, scheme)))
                continue;
            snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
            size_t length;
            char *text = mime_read_file(path, &length);
            if (text && strstr(text, ENTRY_MARK) && unlink(path) == 0) {
                mime_info("url: the hidden entry of %s:// is gone", scheme);
                changed = true;
            }
            free(text);
        }
        if (d)
            closedir(d);
        if (changed) {
            char *argv[] = {"update-desktop-database", dir, nullptr};
            run_tool(argv, nullptr, 0);
        }
    }
    pthread_mutex_unlock(&g_lock);
    free(p->all);
    free(p);
    return nullptr;
}

uint64_t mime_ui_protocols(const MimeShellProtocols *protocols)
{
    const MimePaths *paths = mime_daemon_paths();
    if (!paths || !mime_desktop_setting(paths, "protocols", "MIME_DESKTOP_PROTOCOLS", true))
        return MIME_HC_STATUS_UNSUPPORTED;
    Protocols *p = calloc(1, sizeof(*p));
    if (!p)
        return MIME_HC_STATUS_NO_MEMORY;
    uint32_t count = protocols->count < MIME_PROTOCOLS ? protocols->count : MIME_PROTOCOLS;
    for (uint32_t i = 0; i < count; i++) {
        char scheme[SCHEME_CHARS * 3];
        mime_utf16_to_utf8(scheme, sizeof(scheme), protocols->scheme[i], SCHEME_CHARS);
        if (strlen(scheme) < SCHEME_CHARS && valid(scheme) && !reserved(scheme) && !listed(p, scheme)) {
            mime_utf16_to_utf8(p->program[p->count], sizeof(p->program[0]), protocols->program[i],
                               sizeof(protocols->program[0]) / sizeof(protocols->program[0][0]));
            mime_strlcpy(p->scheme[p->count++], scheme, SCHEME_CHARS);
        }
    }
    p->full = protocols->count >= MIME_PROTOCOLS;
    /* the list as the guest gave it, which the thread narrows to the schemes that keep a hidden entry. */
    p->all = malloc(sizeof(*p));
    if (!p->all) {
        free(p);
        return MIME_HC_STATUS_NO_MEMORY;
    }
    memcpy(p->all, p, sizeof(*p));
    pthread_t thread;
    if (pthread_create(&thread, nullptr, protocols_thread, p) != 0) {
        free(p->all);
        free(p);
        return MIME_HC_STATUS_NO_MEMORY;
    }
    pthread_detach(thread);
    return MIME_HC_STATUS_OK;
}
