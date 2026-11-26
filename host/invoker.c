#include "invoker.h"
#include "mime_host.h"

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __linux__

static bool g_sudo;
static uid_t g_uid;
static gid_t g_gid;

/* sudo strips HOME, the XDG variables and the display; the shell that ran it still has them. */
static char *ancestor_environment(size_t *length)
{
    pid_t pid = getppid();
    for (int depth = 0; depth < 8 && pid > 1; depth++) {
        char path[64];
        struct stat st;
        snprintf(path, sizeof(path), "/proc/%d", (int)pid);
        if (stat(path, &st) != 0)
            return nullptr;
        if (st.st_uid == g_uid) {
            snprintf(path, sizeof(path), "/proc/%d/environ", (int)pid);
            int fd = open(path, O_RDONLY | O_CLOEXEC);
            if (fd < 0)
                return nullptr;
            size_t size = 1 << 16, used = 0;
            char *text = malloc(size + 1);
            for (ssize_t got; text && (got = read(fd, text + used, size - used)) > 0;) {
                used += (size_t)got;
                if (used == size) {
                    char *grown = realloc(text, size * 2 + 1);
                    if (!grown) {
                        free(text);
                        text = nullptr;
                        break;
                    }
                    text = grown;
                    size *= 2;
                }
            }
            close(fd);
            if (text)
                text[used] = 0;
            *length = used;
            return text;
        }
        snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
        FILE *f = fopen(path, "r");
        if (!f)
            return nullptr;
        char line[512];
        char *close_paren = fgets(line, sizeof(line), f) ? strrchr(line, ')') : nullptr;
        fclose(f);
        int parent = 0;
        if (!close_paren || sscanf(close_paren + 1, " %*c %d", &parent) != 1)
            return nullptr;
        pid = parent;
    }
    return nullptr;
}

void mime_invoker_adopt(void)
{
    const char *uid_text = getenv("SUDO_UID"), *gid_text = getenv("SUDO_GID");
    if (geteuid() != 0 || !uid_text || !gid_text)
        return;
    g_uid = (uid_t)strtoul(uid_text, nullptr, 10);
    g_gid = (gid_t)strtoul(gid_text, nullptr, 10);
    if (!g_uid)
        return;
    g_sudo = true;
    size_t length = 0;
    char *text = ancestor_environment(&length);
    if (text) {
        clearenv();
        for (size_t at = 0; at < length; at += strlen(text + at) + 1)
            if (strchr(text + at, '='))
                putenv(text + at);
        return;
    }
    /* no shell of the user above sudo: the user's home and runtime directory still are known. */
    const struct passwd *pw = getpwuid(g_uid);
    if (pw) {
        setenv("HOME", pw->pw_dir, 1);
        setenv("USER", pw->pw_name, 1);
        setenv("LOGNAME", pw->pw_name, 1);
    }
    char runtime[64];
    snprintf(runtime, sizeof(runtime), "/run/user/%u", (unsigned)g_uid);
    if (!getenv("XDG_RUNTIME_DIR") && access(runtime, F_OK) == 0)
        setenv("XDG_RUNTIME_DIR", runtime, 1);
}

bool mime_invoker_drop(void)
{
    if (!g_sudo)
        return true;
    const struct passwd *pw = getpwuid(g_uid);
    /* the user's own groups, kvm and video among them. */
    if ((pw ? initgroups(pw->pw_name, g_gid) : setgroups(1, &g_gid)) != 0 || setgid(g_gid) != 0 ||
        setuid(g_uid) != 0) {
        mime_error("cannot give up root for user %u: %s", (unsigned)g_uid, strerror(errno));
        return false;
    }
    g_sudo = false;
    return true;
}

#else

void mime_invoker_adopt(void)
{
}

bool mime_invoker_drop(void)
{
    return true;
}

#endif
