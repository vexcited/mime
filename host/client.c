/*
 * cli side of the machine lifecycle: up, run, down and status.
 */
#include "instance.h"
#include "ipc.h"
#include "mime_cli.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static bool query_status(const MimePaths *paths, MimeIpcStatus *status)
{
    int fd = mime_ipc_connect(paths->socket);
    if (fd < 0)
        return false;
    bool ok = false;
    MimeIpcHeader h;
    void *payload;
    if (mime_ipc_send(fd, MIME_IPC_STATUS, NULL, 0) && mime_ipc_recv(fd, &h, &payload)) {
        if (h.type == MIME_IPC_STATUS_REPLY && h.size >= sizeof(MimeIpcStatus)) {
            memcpy(status, payload, sizeof(*status));
            ok = true;
        }
        free(payload);
    }
    close(fd);
    return ok;
}

static pid_t read_pid(const MimePaths *paths)
{
    size_t size;
    char *text = mime_read_file(paths->pidfile, &size);
    if (!text)
        return 0;
    pid_t pid = (pid_t)atoi(text);
    free(text);
    return pid;
}

static void print_log_tail(const MimePaths *paths)
{
    size_t size;
    char *log = mime_read_file(paths->logfile, &size);
    if (!log)
        return;
    size_t start = size > 4000 ? size - 4000 : 0;
    while (start > 0 && log[start - 1] != '\n')
        start++;
    fprintf(stderr, "--- last lines of %s ---\n%s", paths->logfile, log + start);
    free(log);
}

static void print_other_instance(const MimeInstanceHolder *holder)
{
    mime_error("another Mime machine is already running (pid %d, socket %s), stop it first", (int)holder->pid,
               holder->socket[0] ? holder->socket : "unknown");
}

/* waits until the machine answers as ready, or its process exits. */
static int wait_ready(const MimePaths *paths, pid_t pid, bool quiet)
{
    MimeIpcStatus status;
    uint64_t deadline = mime_now_ns() + 30ull * 1000000000ull;
    while (mime_now_ns() < deadline) {
        int wstatus;
        bool exited = pid > 0 ? waitpid(pid, &wstatus, WNOHANG) == pid : !mime_instance_running(paths, NULL);
        if (exited) {
            mime_error("the machine exited during boot");
            print_log_tail(paths);
            return 1;
        }
        if (query_status(paths, &status) && status.ready) {
            if (!quiet)
                printf("Mime machine is up (pid %u).\n", status.pid);
            return 0;
        }
        usleep(20000);
    }
    mime_error("timed out waiting for the machine to boot");
    print_log_tail(paths);
    return 1;
}

static int machine_up(const MimePaths *paths, bool quiet)
{
    MimeIpcStatus status;
    if (query_status(paths, &status)) {
        if (status.ready) {
            if (!quiet)
                printf("Mime machine is already up (pid %u).\n", status.pid);
            return 0;
        }
        return wait_ready(paths, 0, quiet);
    }
    if (!mime_is_setup(paths)) {
        mime_error("mime is not set up yet, run `mime setup` first");
        return 1;
    }
    if (!mime_paths_ensure(paths))
        return 1;
    MimeInstanceHolder holder;
    int lock_fd = mime_instance_lock(paths, &holder);
    /* the holder publishes itself right after locking, give it a moment. */
    for (int i = 0; lock_fd == -1 && holder.pid == 0 && i < 100; i++) {
        usleep(10000);
        lock_fd = mime_instance_lock(paths, &holder);
    }
    if (lock_fd == -1) {
        /* a concurrent `mime up` may have won the race for this same machine. */
        if (holder.socket[0] && strcmp(holder.socket, paths->socket) == 0)
            return wait_ready(paths, 0, quiet);
        print_other_instance(&holder);
        return 1;
    }
    if (lock_fd < 0)
        return 1;
    /* lets a concurrent `mime up` see which machine is starting. */
    mime_instance_publish(lock_fd, paths);
    if (!mime_prepare_drive(paths)) {
        close(lock_fd);
        return 1;
    }
    if (!quiet) {
        printf("Starting the Mime machine...\n");
        fflush(stdout);
    }
    pid_t pid = mime_instance_launch(paths, lock_fd);
    /* the machine process now holds the lock on its own. */
    close(lock_fd);
    if (pid < 0)
        return 1;
    int r = wait_ready(paths, pid, quiet);
    if (r != 0)
        kill(pid, SIGTERM);
    return r;
}

int mime_cmd_up(const MimePaths *paths, int argc, char **argv)
{
    return machine_up(paths, false);
}

int mime_cmd_status(const MimePaths *paths, int argc, char **argv)
{
    MimeIpcStatus status;
    if (!query_status(paths, &status)) {
        MimeInstanceHolder holder;
        if (mime_instance_running(paths, &holder))
            printf("Mime machine is down, but another one is running (pid %d, socket %s).\n", (int)holder.pid,
                   holder.socket[0] ? holder.socket : "unknown");
        else
            printf("Mime machine is down.\n");
        return 1;
    }
    printf("Mime machine is %s (pid %u), up for %llus, %u program(s) running.\n", status.ready ? "up" : "booting",
           status.pid, (unsigned long long)(status.uptime_ms / 1000), status.running_programs);
    printf("  C: drive  %s\n  log       %s\n", paths->drive_c, paths->logfile);
    return 0;
}

int mime_cmd_down(const MimePaths *paths, int argc, char **argv)
{
    int fd = mime_ipc_connect(paths->socket);
    pid_t pid = read_pid(paths);
    if (fd < 0) {
        if (pid > 0 && kill(pid, 0) == 0) {
            kill(pid, SIGTERM);
            printf("Mime machine stopped.\n");
            return 0;
        }
        printf("Mime machine is not running.\n");
        return 0;
    }
    mime_ipc_send(fd, MIME_IPC_STOP, NULL, 0);
    MimeIpcHeader h;
    void *payload;
    /* the daemon closes the connection once the guest has powered off. */
    while (mime_ipc_recv(fd, &h, &payload))
        free(payload);
    close(fd);
    uint64_t deadline = mime_now_ns() + 10ull * 1000000000ull;
    while (pid > 0 && kill(pid, 0) == 0 && mime_now_ns() < deadline)
        usleep(20000);
    if (pid > 0 && kill(pid, 0) == 0)
        kill(pid, SIGKILL);
    printf("Mime machine stopped.\n");
    return 0;
}

int mime_cmd_run(const MimePaths *paths, int argc, char **argv)
{
    if (argc < 1) {
        mime_error("usage: mime run <program.exe> [args...]");
        return 2;
    }
    MimeIpcStatus status;
    if (!query_status(paths, &status) || !status.ready) {
        int r = machine_up(paths, false);
        if (r != 0)
            return r;
    }
    char cwd[MIME_PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd)))
        mime_strlcpy(cwd, "/", sizeof(cwd));

    size_t total = strlen(cwd) + 1;
    for (int i = 0; i < argc; i++)
        total += strlen(argv[i]) + 1;
    char *payload = mime_xmalloc(total);
    size_t off = 0;
    memcpy(payload, cwd, strlen(cwd) + 1);
    off += strlen(cwd) + 1;
    for (int i = 0; i < argc; i++) {
        memcpy(payload + off, argv[i], strlen(argv[i]) + 1);
        off += strlen(argv[i]) + 1;
    }

    int fd = mime_ipc_connect(paths->socket);
    if (fd < 0 || !mime_ipc_send(fd, MIME_IPC_RUN, payload, (uint32_t)total)) {
        mime_error("cannot reach the machine");
        free(payload);
        return 1;
    }
    free(payload);

    int exit_code = 1;
    MimeIpcHeader h;
    void *msg;
    bool done = false;
    while (!done && mime_ipc_recv(fd, &h, &msg)) {
        switch (h.type) {
        case MIME_IPC_OUTPUT:
            fwrite(msg, 1, h.size, stdout);
            fflush(stdout);
            break;
        case MIME_IPC_ERROR:
            mime_error("%s", (char *)msg);
            done = true;
            break;
        case MIME_IPC_EXIT:
            if (h.size >= 4)
                memcpy(&exit_code, msg, 4);
            done = true;
            break;
        default:
            break;
        }
        free(msg);
    }
    if (!done)
        mime_error("lost connection to the machine");
    close(fd);
    return exit_code;
}
