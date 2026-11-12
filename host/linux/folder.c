#include "ui_internal.h"

#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef MIME_HAVE_SDBUS
#include <systemd/sd-bus.h>
#endif

extern char **environ;

#define FILE_MANAGER "org.freedesktop.FileManager1"
#define FILE_MANAGER_PATH "/org/freedesktop/FileManager1"

typedef struct Shown {
    bool select;
    char path[MIME_PATH_MAX];
    char folder[MIME_PATH_MAX]; /* the path itself, or the folder that holds the item to select. */
} Shown;

static void put_uri(char *out, size_t size, const char *path)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = (size_t)snprintf(out, size, "file://");
    for (const unsigned char *c = (const unsigned char *)path; *c && n + 4 < size; c++) {
        bool plain = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') ||
                     strchr("/-._~", *c);
        if (plain) {
            out[n++] = (char)*c;
        } else {
            out[n++] = '%';
            out[n++] = hex[*c >> 4];
            out[n++] = hex[*c & 15];
        }
    }
    out[n] = 0;
}

/* the host path of a full guest path, each name with the case the host has it in, which the guest does not keep. */
static bool host_path(const MimePaths *paths, const char *guest, char *out, size_t size)
{
    char current[MIME_PATH_MAX], copy[MIME_PATH_MAX];
    if (!guest[0] || guest[1] != ':' || !mime_paths_drive_root(paths, guest[0], current, sizeof(current)))
        return false;
    mime_strlcpy(copy, guest + 2, sizeof(copy));
    char *save = nullptr;
    for (char *name = strtok_r(copy, "\\/", &save); name; name = strtok_r(nullptr, "\\/", &save)) {
        if (!strcmp(name, ".") || !strcmp(name, ".."))
            return false;
        char next[MIME_PATH_MAX * 2];
        snprintf(next, sizeof(next), "%s/%s", current, name);
        struct stat st;
        if (stat(next, &st) != 0) {
            DIR *dir = opendir(current[0] ? current : "/");
            bool found = false;
            for (struct dirent *e; dir && !found && (e = readdir(dir));)
                if (!strcasecmp(e->d_name, name)) {
                    snprintf(next, sizeof(next), "%s/%s", current, e->d_name);
                    found = true;
                }
            if (dir)
                closedir(dir);
            if (!found)
                return false;
        }
        if (strlen(next) >= sizeof(current))
            return false;
        mime_strlcpy(current, next, sizeof(current));
    }
    mime_strlcpy(out, current[0] ? current : "/", size);
    return true;
}

/* the file managers of the desktops answer on the bus, and are started by it. it is the only way to select an item. */
static bool show_on_bus(const Shown *shown)
{
#ifdef MIME_HAVE_SDBUS
    sd_bus *bus = nullptr;
    if (sd_bus_open_user(&bus) < 0)
        return false;
    char uri[MIME_PATH_MAX * 3 + 8];
    put_uri(uri, sizeof(uri), shown->path);
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int r = sd_bus_call_method(bus, FILE_MANAGER, FILE_MANAGER_PATH, FILE_MANAGER,
                               shown->select ? "ShowItems" : "ShowFolders", &error, nullptr, "ass", 1, uri, "");
    if (r < 0)
        mime_info("folder: no file manager on the bus (%s)", error.message ? error.message : strerror(-r));
    sd_bus_error_free(&error);
    sd_bus_flush_close_unref(bus);
    return r >= 0;
#else
    return false;
#endif
}

/* only ever given a folder: what opens a file is the choice of the guest's associations, not of the host's. */
static void show_with_opener(const Shown *shown)
{
    char *argv[] = {"xdg-open", (char *)shown->folder, nullptr};
    pid_t pid;
    int r = posix_spawnp(&pid, argv[0], nullptr, nullptr, argv, environ);
    if (r != 0) {
        mime_warn("folder: xdg-open did not start: %s", strerror(r));
        return;
    }
    while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {}
}

static void *show_thread(void *arg)
{
    Shown *shown = arg;
    if (!show_on_bus(shown))
        show_with_opener(shown);
    free(shown);
    return nullptr;
}

uint64_t mime_ui_folder(const MimeShellFolder *folder)
{
    char guest[MIME_PATH_MAX];
    mime_utf16_to_utf8(guest, sizeof(guest), folder->path, sizeof(folder->path) / sizeof(folder->path[0]));
    const MimePaths *paths = mime_daemon_paths();
    Shown *shown = calloc(1, sizeof(*shown));
    if (!shown)
        return MIME_HC_STATUS_NO_MEMORY;
    struct stat st;
    if (!paths || !host_path(paths, guest, shown->path, sizeof(shown->path)) || stat(shown->path, &st) != 0) {
        free(shown);
        return MIME_HC_STATUS_NOT_FOUND;
    }
    shown->select = folder->select != 0;
    mime_strlcpy(shown->folder, shown->path, sizeof(shown->folder));
    if (shown->select) {
        char *slash = strrchr(shown->folder, '/');
        if (slash)
            slash[slash == shown->folder ? 1 : 0] = 0;
    } else if (!S_ISDIR(st.st_mode)) {
        free(shown);
        return MIME_HC_STATUS_INVALID;
    }
    /* the bus may start the file manager before it answers, which no thread of the machine waits for. */
    pthread_t thread;
    if (pthread_create(&thread, nullptr, show_thread, shown) != 0) {
        free(shown);
        return MIME_HC_STATUS_NO_MEMORY;
    }
    pthread_detach(thread);
    return MIME_HC_STATUS_OK;
}
