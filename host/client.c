/*
 * cli side of the machine lifecycle: up, run, down and status.
 */
#include "instance.h"
#include "ipc.h"
#include "mime_cli.h"

#include <mime/abi.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
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
    if (mime_ipc_send(fd, MIME_IPC_STATUS, nullptr, 0) && mime_ipc_recv(fd, &h, &payload)) {
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
        bool exited = pid > 0 ? waitpid(pid, &wstatus, WNOHANG) == pid : !mime_instance_running(paths, nullptr);
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
        usleep(2000);
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
    mime_ipc_send(fd, MIME_IPC_STOP, nullptr, 0);
    MimeIpcHeader h;
    void *payload;
    /* the daemon closes the connection once the guest has powered off. */
    while (mime_ipc_recv(fd, &h, &payload))
        free(payload);
    close(fd);
    uint64_t deadline = mime_now_ns() + 10ull * 1000000000ull;
    while (pid > 0 && kill(pid, 0) == 0 && mime_now_ns() < deadline)
        usleep(2000);
    if (pid > 0 && kill(pid, 0) == 0)
        kill(pid, SIGKILL);
    printf("Mime machine stopped.\n");
    return 0;
}

/* ------------------------------------------------------------------ run */

/*
 * `mime run` is the console of the program, as a windows terminal is: its
 * output and input, ctrl+c and ctrl+break as ctrl events, and its going
 * away as the console closing. a gui program has no console, so like
 * cmd.exe the command returns as soon as it started, unless --wait.
 */

#define ENABLE_PROCESSED_INPUT 0x1
#define ENABLE_LINE_INPUT 0x2
#define ENABLE_ECHO_INPUT 0x4

static int g_signal_pipe[2] = {-1, -1};
static struct termios g_saved_termios;
static bool g_termios_saved;

static void on_run_signal(int sig)
{
    int saved = errno;
    unsigned char b = (unsigned char)sig;
    if (write(g_signal_pipe[1], &b, 1) < 0) {
    }
    errno = saved;
}

static void restore_terminal(void)
{
    if (g_termios_saved)
        tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_termios);
}

/* the terminal does the line editing, echo and ctrl+c a windows console's input mode asks for. */
static void apply_console_mode(uint32_t mode)
{
    if (!g_termios_saved)
        return;
    struct termios t = g_saved_termios;
    if (mode & ENABLE_LINE_INPUT) {
        t.c_lflag |= ICANON;
        t.c_lflag = (mode & ENABLE_ECHO_INPUT) ? t.c_lflag | ECHO : t.c_lflag & ~(tcflag_t)ECHO;
    } else {
        /* raw keys: enter arrives as \r, as a windows console reports it. */
        t.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
        t.c_iflag &= ~(tcflag_t)(ICRNL | IXON);
        t.c_cc[VMIN] = 1;
        t.c_cc[VTIME] = 0;
    }
    t.c_lflag = (mode & ENABLE_PROCESSED_INPUT) ? t.c_lflag | ISIG : t.c_lflag & ~(tcflag_t)ISIG;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

static uint32_t terminal_size(void)
{
    struct winsize ws;
    for (int fd = STDOUT_FILENO; fd >= STDIN_FILENO; fd--)
        if (ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col && ws.ws_row)
            return (uint32_t)ws.ws_col | (uint32_t)ws.ws_row << 16;
    return 0;
}

static void write_all(int fd, const void *data, size_t size)
{
    const char *p = data;
    while (size) {
        ssize_t n = write(fd, p, size);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return;
        p += n;
        size -= (size_t)n;
    }
}

int mime_cmd_run(const MimePaths *paths, int argc, char **argv)
{
    bool wait_gui = false;
    while (argc > 0 && strcmp(argv[0], "--wait") == 0) {
        wait_gui = true;
        argc--;
        argv++;
    }
    if (argc < 1) {
        mime_error("usage: mime run [--wait] <program.exe> [args...]");
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

    MimeIpcRun header = {};
    if (isatty(STDIN_FILENO))
        header.flags |= MIME_RUN_TTY_INPUT;
    if (isatty(STDOUT_FILENO))
        header.flags |= MIME_RUN_TTY_OUTPUT;
    if (isatty(STDERR_FILENO))
        header.flags |= MIME_RUN_TTY_ERROR;
    header.console_size = terminal_size();

    size_t total = sizeof(header) + strlen(cwd) + 1;
    for (int i = 0; i < argc; i++)
        total += strlen(argv[i]) + 1;
    char *payload = mime_xmalloc(total);
    size_t off = 0;
    memcpy(payload, &header, sizeof(header));
    off += sizeof(header);
    memcpy(payload + off, cwd, strlen(cwd) + 1);
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

    if (pipe(g_signal_pipe) != 0) {
        mime_error("cannot create a pipe: %s", strerror(errno));
        close(fd);
        return 1;
    }
    fcntl(g_signal_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(g_signal_pipe[1], F_SETFD, FD_CLOEXEC);
    fcntl(g_signal_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(g_signal_pipe[1], F_SETFL, O_NONBLOCK);
    struct sigaction sa = {};
    sa.sa_handler = on_run_signal;
    sigemptyset(&sa.sa_mask);
    int signals[] = {SIGINT, SIGQUIT, SIGHUP, SIGTERM, SIGWINCH};
    for (size_t i = 0; i < MIME_ARRAY_COUNT(signals); i++)
        sigaction(signals[i], &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);

    int exit_code = 1;
    bool done = false, console = false, input_open = false;
    while (!done) {
        struct pollfd fds[3] = {{fd, POLLIN, 0}, {g_signal_pipe[0], POLLIN, 0}, {STDIN_FILENO, POLLIN, 0}};
        int n = poll(fds, input_open ? 3 : 2, -1);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (fds[1].revents & POLLIN) {
            unsigned char sig;
            while (read(g_signal_pipe[0], &sig, 1) == 1) {
                uint32_t value;
                if (sig == SIGINT || sig == SIGQUIT) {
                    if (!console) {
                        /* a gui program is no part of the console: the wait ends, it keeps running. */
                        done = true;
                        exit_code = 128 + sig;
                        break;
                    }
                    value = sig == SIGINT ? 0 : 1; /* CTRL_C_EVENT, CTRL_BREAK_EVENT */
                    mime_ipc_send(fd, MIME_IPC_SIGNAL, &value, sizeof(value));
                } else if (sig == SIGWINCH) {
                    value = terminal_size();
                    if (value)
                        mime_ipc_send(fd, MIME_IPC_RESIZE, &value, sizeof(value));
                } else {
                    /* the terminal goes away: hanging up closes the console. */
                    restore_terminal();
                    close(fd);
                    return 128 + sig;
                }
            }
            if (done)
                break;
        }
        if (input_open && (fds[2].revents & (POLLIN | POLLHUP | POLLERR))) {
            char buf[4096];
            ssize_t got = read(STDIN_FILENO, buf, sizeof(buf));
            if (got < 0 && errno == EINTR)
                continue;
            mime_ipc_send(fd, MIME_IPC_INPUT, buf, got > 0 ? (uint32_t)got : 0);
            /* a terminal reads again after an end of file, a pipe or file does not. */
            if (got <= 0 && !(header.flags & MIME_RUN_TTY_INPUT))
                input_open = false;
            if (got < 0)
                input_open = false;
        }
        if (!(fds[0].revents & (POLLIN | POLLHUP | POLLERR)))
            continue;
        MimeIpcHeader h;
        void *msg;
        if (!mime_ipc_recv(fd, &h, &msg)) {
            mime_error("lost connection to the machine");
            break;
        }
        switch (h.type) {
        case MIME_IPC_OUTPUT:
            write_all(STDOUT_FILENO, msg, h.size);
            break;
        case MIME_IPC_STDERR:
        case MIME_IPC_DEBUG:
            write_all(STDERR_FILENO, msg, h.size);
            break;
        case MIME_IPC_STARTED: {
            uint32_t subsystem = 0;
            if (h.size >= sizeof(subsystem))
                memcpy(&subsystem, msg, sizeof(subsystem));
            console = subsystem == MIME_SUBSYSTEM_CUI;
            if (!console && !wait_gui) {
                exit_code = 0;
                done = true;
                break;
            }
            if (console) {
                input_open = true;
                if (header.flags & MIME_RUN_TTY_INPUT) {
                    g_termios_saved = tcgetattr(STDIN_FILENO, &g_saved_termios) == 0;
                    atexit(restore_terminal);
                }
            }
            break;
        }
        case MIME_IPC_CONSOLE_MODE:
            if (h.size >= sizeof(uint32_t)) {
                uint32_t mode;
                memcpy(&mode, msg, sizeof(mode));
                apply_console_mode(mode);
            }
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
    restore_terminal();
    close(fd);
    return exit_code;
}
