#include "instance.h"

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

extern char **environ;

/* fds the machine process receives. */
#define LAUNCH_CONTROL_FD 3
#define LAUNCH_LOCK_FD 4

static constexpr char g_launch_magic[8] = {'M', 'I', 'M', 'E', 'V', 'M', 'M', 1};

static void read_holder(int fd, MimeInstanceHolder *holder)
{
    memset(holder, 0, sizeof(*holder));
    char text[MIME_PATH_MAX + 32];
    ssize_t n = pread(fd, text, sizeof(text) - 1, 0);
    if (n <= 0)
        return;
    text[n] = 0;
    holder->pid = (pid_t)atoi(text);
    char *line = strchr(text, '\n');
    if (!line)
        return;
    line++;
    char *end = strchr(line, '\n');
    if (end)
        *end = 0;
    mime_strlcpy(holder->socket, line, sizeof(holder->socket));
}

static int open_lock(const MimePaths *paths)
{
    int fd = open(paths->lockfile, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0)
        mime_error("cannot open %s: %s", paths->lockfile, strerror(errno));
    return fd;
}

int mime_instance_lock(const MimePaths *paths, MimeInstanceHolder *holder)
{
    int fd = open_lock(paths);
    if (fd < 0)
        return -2;
    if (flock(fd, LOCK_EX | LOCK_NB) == 0)
        return fd;
    int err = errno;
    if (err == EWOULDBLOCK && holder)
        read_holder(fd, holder);
    close(fd);
    if (err != EWOULDBLOCK) {
        mime_error("cannot lock %s: %s", paths->lockfile, strerror(err));
        return -2;
    }
    return -1;
}

bool mime_instance_running(const MimePaths *paths, MimeInstanceHolder *holder)
{
    int fd = mime_instance_lock(paths, holder);
    if (fd >= 0) {
        close(fd);
        return false;
    }
    return fd == -1;
}

pid_t mime_instance_launch(const MimePaths *paths, int lock_fd)
{
    int pair[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) != 0) {
        mime_error("cannot create the launch socket: %s", strerror(errno));
        return -1;
    }
    /* high numbered copies so the dup2 actions never map an fd onto itself. */
    int control = fcntl(pair[1], F_DUPFD_CLOEXEC, 16);
    int lock = fcntl(lock_fd, F_DUPFD_CLOEXEC, 16);
    close(pair[1]);
    fcntl(pair[0], F_SETFD, FD_CLOEXEC);

    char exe[MIME_PATH_MAX];
    snprintf(exe, sizeof(exe), "%s/mime", paths->exe_dir);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, paths->logfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    posix_spawn_file_actions_adddup2(&actions, control, LAUNCH_CONTROL_FD);
    posix_spawn_file_actions_adddup2(&actions, lock, LAUNCH_LOCK_FD);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    short flags = POSIX_SPAWN_SETSID;
#ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
    flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif
    posix_spawnattr_setflags(&attr, flags);
    char *argv[] = {"mime-vmm", nullptr};
    pid_t pid;
    int err = posix_spawn(&pid, exe, &actions, &attr, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    close(control);
    close(lock);
    if (err != 0) {
        close(pair[0]);
        mime_error("cannot start the machine: %s", strerror(err));
        return -1;
    }
    /* stays connected until the machine acks, so it can check who launched it. */
    char ack = 0;
    if (write(pair[0], g_launch_magic, sizeof(g_launch_magic)) != (ssize_t)sizeof(g_launch_magic) ||
        read(pair[0], &ack, 1) != 1)
        mime_warn("the machine did not answer the launch handshake");
    close(pair[0]);
    return pid;
}

static pid_t socket_peer_pid(int fd)
{
#ifdef LOCAL_PEERPID
    pid_t pid = 0;
    socklen_t len = sizeof(pid);
    if (getsockopt(fd, SOL_LOCAL, LOCAL_PEERPID, &pid, &len) == 0)
        return pid;
#elifdef SO_PEERCRED
    struct ucred cred;
    socklen_t len = sizeof(cred);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0)
        return cred.pid;
#endif
    return -1;
}

bool mime_instance_accept_launch(int *lock_fd)
{
    struct sockaddr_storage addr;
    socklen_t len = sizeof(addr);
    if (getsockname(LAUNCH_CONTROL_FD, (struct sockaddr *)&addr, &len) != 0 || addr.ss_family != AF_UNIX)
        return false;
    /* the launching cli created the socket pair and is still our parent. */
    if (socket_peer_pid(LAUNCH_CONTROL_FD) != getppid())
        return false;
    char magic[sizeof(g_launch_magic)];
    size_t got = 0;
    while (got < sizeof(magic)) {
        ssize_t n = read(LAUNCH_CONTROL_FD, magic + got, sizeof(magic) - got);
        if (n <= 0)
            return false;
        got += (size_t)n;
    }
    if (memcmp(magic, g_launch_magic, sizeof(magic)) != 0)
        return false;
    char ack = 1;
    if (write(LAUNCH_CONTROL_FD, &ack, 1) != 1)
        return false;
    close(LAUNCH_CONTROL_FD);
    /* relocking through the inherited open file succeeds only if it holds the lock. */
    if (flock(LAUNCH_LOCK_FD, LOCK_EX | LOCK_NB) != 0)
        return false;
    fcntl(LAUNCH_LOCK_FD, F_SETFD, FD_CLOEXEC);
    *lock_fd = LAUNCH_LOCK_FD;
    return true;
}

void mime_instance_publish(int lock_fd, const MimePaths *paths)
{
    char text[MIME_PATH_MAX + 32];
    int n = snprintf(text, sizeof(text), "%d\n%s\n", (int)getpid(), paths->socket);
    if (ftruncate(lock_fd, 0) != 0 || pwrite(lock_fd, text, (size_t)n, 0) != n)
        mime_warn("cannot publish the instance in %s: %s", paths->lockfile, strerror(errno));
}
