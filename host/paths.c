#include "mime_host.h"

#include <errno.h>
#include <mach-o/dyld.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* resolves an xdg base directory: env var if absolute, else $HOME/<fallback>. */
static void xdg_dir(char *out, size_t size, const char *env, const char *fallback)
{
    const char *value = getenv(env);
    if (value && value[0] == '/') {
        snprintf(out, size, "%s/mime", value);
        return;
    }
    const char *home = getenv("HOME");
    if (!home || !home[0])
        home = "/tmp";
    snprintf(out, size, "%s/%s/mime", home, fallback);
}

/*
 * one machine may run per user, whatever the xdg variables say, so the
 * lock lives in the per user temporary dir the system assigns, which no
 * environment variable can move.
 */
static void lock_path(char *out, size_t size)
{
#ifdef _CS_DARWIN_USER_TEMP_DIR
    char dir[MIME_PATH_MAX];
    size_t len = confstr(_CS_DARWIN_USER_TEMP_DIR, dir, sizeof(dir));
    if (len > 0 && len <= sizeof(dir)) {
        len = strlen(dir);
        while (len > 1 && dir[len - 1] == '/')
            len--;
        snprintf(out, size, "%.*s/mime.lock", (int)len, dir);
        return;
    }
#endif
    snprintf(out, size, "/tmp/mime-%u.lock", (unsigned)getuid());
}

bool mime_paths_init(MimePaths *paths)
{
    memset(paths, 0, sizeof(*paths));
    xdg_dir(paths->config, sizeof(paths->config), "XDG_CONFIG_HOME", ".config");
    xdg_dir(paths->data, sizeof(paths->data), "XDG_DATA_HOME", ".local/share");
    xdg_dir(paths->state, sizeof(paths->state), "XDG_STATE_HOME", ".local/state");
    xdg_dir(paths->cache, sizeof(paths->cache), "XDG_CACHE_HOME", ".cache");

    /*
     * the spec asks for a fallback with runtime dir semantics when
     * XDG_RUNTIME_DIR is unset. macos has no such dir, but its per user
     * TMPDIR is private to the user and wiped on reboot, so it fits.
     */
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    if (runtime && runtime[0] == '/') {
        size_t len = strlen(runtime);
        while (len > 1 && runtime[len - 1] == '/')
            len--;
        snprintf(paths->runtime, sizeof(paths->runtime), "%.*s/mime", (int)len, runtime);
    } else {
        const char *tmp = getenv("TMPDIR");
        if (!tmp || tmp[0] != '/')
            tmp = "/tmp";
        size_t len = strlen(tmp);
        while (len > 1 && tmp[len - 1] == '/')
            len--;
        snprintf(paths->runtime, sizeof(paths->runtime), "%.*s/mime-%u", (int)len, tmp, (unsigned)getuid());
    }

    snprintf(paths->drive_c, sizeof(paths->drive_c), "%s/drive_c", paths->data);
    snprintf(paths->socket, sizeof(paths->socket), "%s/mime.sock", paths->runtime);
    snprintf(paths->pidfile, sizeof(paths->pidfile), "%s/mime.pid", paths->runtime);
    lock_path(paths->lockfile, sizeof(paths->lockfile));
    snprintf(paths->logfile, sizeof(paths->logfile), "%s/vmm.log", paths->state);
    snprintf(paths->setup_stamp, sizeof(paths->setup_stamp), "%s/setup.ini", paths->data);

    char exe[MIME_PATH_MAX];
    uint32_t exe_size = sizeof(exe);
    char resolved[MIME_PATH_MAX];
    if (_NSGetExecutablePath(exe, &exe_size) == 0 && realpath(exe, resolved)) {
        char *slash = strrchr(resolved, '/');
        if (slash)
            *slash = 0;
        mime_strlcpy(paths->exe_dir, resolved, sizeof(paths->exe_dir));
    } else {
        mime_strlcpy(paths->exe_dir, ".", sizeof(paths->exe_dir));
    }

    if (strlen(paths->socket) >= 100) {
        mime_error("socket path too long: %s", paths->socket);
        return false;
    }
    return true;
}

bool mime_paths_ensure(const MimePaths *paths)
{
    const char *dirs[] = {paths->config, paths->data, paths->state, paths->cache};
    for (size_t i = 0; i < MIME_ARRAY_COUNT(dirs); i++) {
        if (!mime_mkdir_p(dirs[i], 0755)) {
            mime_error("cannot create %s: %s", dirs[i], strerror(errno));
            return false;
        }
    }
    if (!mime_mkdir_p(paths->runtime, 0700)) {
        mime_error("cannot create %s: %s", paths->runtime, strerror(errno));
        return false;
    }
    return true;
}
