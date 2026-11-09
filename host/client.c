/* machine lifecycle commands: up, run, down, status and config. */
#include "identity.h"
#include "instance.h"
#include "ipc.h"
#include "mime_cli.h"

#include <mime/abi.h>

#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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
    if (!mime_prepare_drive(paths) || mime_genuine_ensure(paths) != 0) {
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

/* what mime up and mime run do before the machine starts, on its own, so a script can have it done first. */
int mime_cmd_setup(const MimePaths *paths, int argc, char **argv)
{
    if (argc) {
        fprintf(stderr, "usage: mime setup\n");
        return 2;
    }
    if (!mime_paths_ensure(paths))
        return 1;
    /* a machine that is up prepared its drive when it started. */
    MimeInstanceHolder holder;
    int lock_fd = mime_instance_lock(paths, &holder);
    if (lock_fd == -2)
        return 1;
    if (lock_fd >= 0) {
        bool prepared = mime_prepare_drive(paths);
        close(lock_fd);
        if (!prepared)
            return 1;
    }
    int r = mime_genuine_ensure(paths);
    if (r == 0)
        printf("Mime is set up.\n");
    return r;
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
        mime_genuine_print_status(paths);
        return 1;
    }
    printf("Mime machine is %s (pid %u), up for %llus, %u program(s) running.\n", status.ready ? "up" : "booting",
           status.pid, (unsigned long long)(status.uptime_ms / 1000), status.running_programs);
    printf("  C: drive  %s\n  log       %s\n", paths->drive_c, paths->logfile);
    mime_genuine_print_status(paths);
    return 0;
}

/* the locale is the drive's and lasts; the machine's settings live in its process and go with it. */
int mime_cmd_config(const MimePaths *paths, int argc, char **argv)
{
    bool get = argc == 2 && strcmp(argv[0], "get") == 0;
    bool set = argc == 3 && strcmp(argv[0], "set") == 0;
    if (argc && !get && !set) {
        fprintf(stderr, "usage: mime config [get <name> | set <name> <value>]\n");
        return 2;
    }
    if ((get || set) && strcmp(argv[1], "locale") == 0) {
        MimeIdentity id;
        if (set) {
            if (!mime_identity_set_locale(paths, argv[2])) {
                mime_error("%s is not a locale of the system", argv[2]);
                return 1;
            }
            MimeIpcStatus status;
            if (query_status(paths, &status) && status.ready)
                printf("The machine takes the locale when it starts again.\n");
            return 0;
        }
        mime_identity_load(paths, &id);
        printf("%s\n", id.locale);
        return 0;
    }
    if (!argc) {
        MimeIdentity id;
        mime_identity_load(paths, &id);
        printf("%-10s %s\n", "locale", id.locale);
        MimeIpcStatus status;
        if (!query_status(paths, &status))
            return 0;
    }
    size_t size = 0;
    char payload[512];
    for (int i = 1; i < argc; i++) {
        size_t length = strlen(argv[i]) + 1;
        if (size + length > sizeof(payload)) {
            mime_error("the setting is too long");
            return 2;
        }
        memcpy(payload + size, argv[i], length);
        size += length;
    }
    int fd = mime_ipc_connect(paths->socket);
    if (fd < 0) {
        mime_error("the machine is down, start it with `mime up` first");
        return 1;
    }
    MimeIpcHeader h;
    void *reply;
    if (!mime_ipc_send(fd, MIME_IPC_CONFIG, payload, (uint32_t)size) || !mime_ipc_recv(fd, &h, &reply)) {
        close(fd);
        mime_error("cannot reach the machine");
        return 1;
    }
    close(fd);
    int r = 0;
    if (h.type == MIME_IPC_CONFIG_REPLY) {
        const char *p = reply, *end = p + h.size;
        while (p < end) {
            const char *name = p;
            const char *value = name + strlen(name) + 1;
            if (value >= end)
                break;
            if (get)
                printf("%s\n", value);
            else if (!set)
                printf("%-10s %s\n", name, value);
            p = value + strlen(value) + 1;
        }
    } else {
        mime_error("%s", h.type == MIME_IPC_ERROR ? (char *)reply : "unexpected answer from the machine");
        r = 1;
    }
    free(reply);
    return r;
}

/*
 * normal shutdown notifies programs, waits and flushes files. --force terminates programs immediately. the host
 * process is terminated after the guest's limit plus a margin regardless of guest progress.
 */
int mime_cmd_down(const MimePaths *paths, int argc, char **argv)
{
    MimeIpcStop stop = {};
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--force") == 0 || strcmp(argv[i], "-f") == 0) {
            stop.flags |= MIME_SHUTDOWN_FORCE;
        } else {
            fprintf(stderr, "usage: mime down [--force]\n");
            return 2;
        }
    }
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
    mime_ipc_send(fd, MIME_IPC_STOP, &stop, sizeof(stop));
    MimeIpcHeader h;
    void *payload;
    /* the daemon closes the connection once the guest has powered off. */
    uint64_t guest_ms = MIME_SHUTDOWN_DEFAULT_MS;
    uint64_t give_up = mime_now_ns() + (guest_ms + 20000) * 1000000ull;
    for (;;) {
        struct pollfd p = {.fd = fd, .events = POLLIN};
        int64_t left = (int64_t)(give_up - mime_now_ns()) / 1000000;
        if (left <= 0 || poll(&p, 1, (int)left) == 0)
            break;
        if (!mime_ipc_recv(fd, &h, &payload))
            break;
        free(payload);
    }
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
 * relays console I/O and Ctrl+C or Ctrl+Break events; disconnecting closes the console. GUI programs return
 * immediately, as cmd.exe does.
 */

#define ENABLE_PROCESSED_INPUT 0x1
#define ENABLE_LINE_INPUT 0x2
#define ENABLE_ECHO_INPUT 0x4

static int g_signal_pipe[2] = {-1, -1};
static struct termios g_saved_termios;
static bool g_termios_saved;
static bool g_raw_terminal;

static void on_run_signal(int sig)
{
    int saved = errno;
    unsigned char b = (unsigned char)sig;
    if (write(g_signal_pipe[1], &b, 1) < 0) {
    }
    errno = saved;
}

/* tracks terminal modes enabled by guest escape sequences so they can be restored even if the program is killed. */
typedef struct TerminalModes {
    bool alt_screen, cursor_hidden, wrap_off, keypad_application, cursor_application;
    bool mouse_9, mouse_1000, mouse_1002, mouse_1003, mouse_1005, mouse_1006, mouse_1007, mouse_1015;
    bool paste, focus, win32_input, cursor_style;
    int state;
    char buffer[64];
    int length;
} TerminalModes;

static TerminalModes g_modes;
static int g_modes_fd = -1; /* the terminal the guest's escape sequences went to. */

static void note_mode(TerminalModes *m, int mode, bool on)
{
    switch (mode) {
    case 1: m->cursor_application = on; break;
    case 7: m->wrap_off = !on; break;
    case 9: m->mouse_9 = on; break;
    case 25: m->cursor_hidden = !on; break;
    case 1000: m->mouse_1000 = on; break;
    case 1002: m->mouse_1002 = on; break;
    case 1003: m->mouse_1003 = on; break;
    case 1004: m->focus = on; break;
    case 1005: m->mouse_1005 = on; break;
    case 1006: m->mouse_1006 = on; break;
    case 1007: m->mouse_1007 = on; break;
    case 1015: m->mouse_1015 = on; break;
    case 1049: m->alt_screen = on; break;
    case 2004: m->paste = on; break;
    case 9001: m->win32_input = on; break;
    default: break;
    }
}

static void track_output(int fd, const uint8_t *data, size_t size)
{
    TerminalModes *m = &g_modes;
    g_modes_fd = fd;
    for (size_t i = 0; i < size; i++) {
        uint8_t c = data[i];
        switch (m->state) {
        case 0:
            if (c == 0x1B)
                m->state = 1;
            break;
        case 1:
            m->length = 0;
            if (c == '[')
                m->state = 2;
            else if (c == '=') {
                m->keypad_application = true;
                m->state = 0;
            } else if (c == '>') {
                m->keypad_application = false;
                m->state = 0;
            } else if (c == ']' || c == 'P' || c == '_' || c == '^' || c == 'X')
                m->state = 3;
            else
                m->state = c == 0x1B ? 1 : 0;
            break;
        case 2:
            if (c >= 0x40 && c <= 0x7E) {
                m->buffer[m->length] = 0;
                if (m->buffer[0] == '?' && (c == 'h' || c == 'l')) {
                    int value = 0;
                    for (int k = 1; k <= m->length; k++) {
                        char ch = m->buffer[k];
                        if (ch >= '0' && ch <= '9') {
                            value = value * 10 + (ch - '0');
                        } else {
                            note_mode(m, value, c == 'h');
                            value = 0;
                        }
                    }
                } else if (c == 'q' && m->length >= 2 && m->buffer[m->length - 1] == ' ') {
                    m->cursor_style = m->buffer[0] != '0';
                }
                m->state = 0;
            } else if (m->length < (int)sizeof(m->buffer) - 1) {
                m->buffer[m->length++] = (char)c;
            }
            break;
        default:
            /* a string ends with the bell or the escape that starts its terminator. */
            if (c == 0x07)
                m->state = 0;
            else if (c == 0x1B)
                m->state = 1;
            break;
        }
    }
}

/* restores terminal modes changed by the guest. */
static void restore_terminal_modes(void)
{
    TerminalModes *m = &g_modes;
    char out[512];
    int n = 0;
    if (m->mouse_9) n += snprintf(out + n, sizeof(out) - (size_t)n, "\x1b[?9l");
    if (m->mouse_1000) n += snprintf(out + n, sizeof(out) - (size_t)n, "\x1b[?1000l");
    if (m->mouse_1002) n += snprintf(out + n, sizeof(out) - (size_t)n, "\x1b[?1002l");
    if (m->mouse_1003) n += snprintf(out + n, sizeof(out) - (size_t)n, "\x1b[?1003l");
    if (m->mouse_1005) n += snprintf(out + n, sizeof(out) - (size_t)n, "\x1b[?1005l");
    if (m->mouse_1006) n += snprintf(out + n, sizeof(out) - (size_t)n, "\x1b[?1006l");
    if (m->mouse_1007) n += snprintf(out + n, sizeof(out) - (size_t)n, "\x1b[?1007l");
    if (m->mouse_1015) n += snprintf(out + n, sizeof(out) - (size_t)n, "\x1b[?1015l");
    if (m->paste) n += snprintf(out + n, sizeof(out) - (size_t)n, "\x1b[?2004l");
    if (m->focus) n += snprintf(out + n, sizeof(out) - (size_t)n, "\x1b[?1004l");
    if (m->win32_input) n += snprintf(out + n, sizeof(out) - (size_t)n, "\x1b[?9001l");
    if (m->cursor_application) n += snprintf(out + n, sizeof(out) - (size_t)n, "\x1b[?1l");
    if (m->keypad_application) n += snprintf(out + n, sizeof(out) - (size_t)n, "\x1b>");
    if (m->wrap_off) n += snprintf(out + n, sizeof(out) - (size_t)n, "\x1b[?7h");
    if (m->cursor_style) n += snprintf(out + n, sizeof(out) - (size_t)n, "\x1b[0 q");
    if (m->alt_screen) n += snprintf(out + n, sizeof(out) - (size_t)n, "\x1b[?1049l");
    if (m->cursor_hidden) n += snprintf(out + n, sizeof(out) - (size_t)n, "\x1b[?25h");
    if (g_modes_fd >= 0 && n > 0) {
        n += snprintf(out + n, sizeof(out) - (size_t)n, "\x1b[0m");
        ssize_t w = write(g_modes_fd, out, (size_t)n);
        (void)w;
    }
    memset(m, 0, sizeof(*m));
}

static void restore_terminal(void)
{
    restore_terminal_modes();
    g_raw_terminal = false;
    if (g_termios_saved)
        tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_termios);
}

/* the terminal does the line editing, echo and ctrl+c a windows console's input mode asks for. */
static void apply_console_mode(uint32_t mode)
{
    if (!g_termios_saved || g_raw_terminal)
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

/*
 * the guest handles editing, echo and Ctrl+C, so pass keys through immediately. Ctrl+backslash remains a signal for
 * Ctrl+Break.
 */
static void enter_raw_terminal(void)
{
    if (!g_termios_saved || g_raw_terminal)
        return;
    struct termios t = g_saved_termios;
    t.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ECHONL | IEXTEN);
    t.c_lflag |= ISIG;
    t.c_iflag &= ~(tcflag_t)(ICRNL | INLCR | IGNCR | IXON | IXOFF | ISTRIP | BRKINT | PARMRK);
    t.c_oflag &= ~(tcflag_t)OPOST;
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    t.c_cc[VINTR] = _POSIX_VDISABLE;
    t.c_cc[VSUSP] = _POSIX_VDISABLE;
#ifdef VDSUSP
    t.c_cc[VDSUSP] = _POSIX_VDISABLE;
#endif
    if (tcsetattr(STDIN_FILENO, TCSANOW, &t) == 0)
        g_raw_terminal = true;
}

static uint32_t terminal_size(void)
{
    struct winsize ws;
    for (int fd = STDOUT_FILENO; fd >= STDIN_FILENO; fd--)
        if (ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col && ws.ws_row)
            return (uint32_t)ws.ws_col | (uint32_t)ws.ws_row << 16;
    return 0;
}

/*
 * the shell receives the low byte of the 32-bit exit code, or 1 if a nonzero code has a zero low byte, so failure
 * never becomes success. print codes the 8-bit shell status cannot represent.
 */
static int shell_status(uint32_t code)
{
    int status = (int)(code & 0xFF);
    if (code && !status)
        status = 1;
    if (code > 0xFF)
        dprintf(STDERR_FILENO, "mime: the program exited with code %u (0x%08X)\n", code, code);
    return status;
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

/*
 * queries the cursor so guest output preserves shell text above it. returns column + 1 | row + 1 << 16, or 0 if
 * unanswered.
 */
static uint32_t terminal_cursor(void)
{
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
        return 0;
    struct termios saved, t;
    if (tcgetattr(STDIN_FILENO, &saved) != 0)
        return 0;
    t = saved;
    t.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &t) != 0)
        return 0;
    uint32_t result = 0;
    static const char query[] = "\x1b[6n";
    if (write(STDOUT_FILENO, query, sizeof(query) - 1) == (ssize_t)(sizeof(query) - 1)) {
        char reply[32];
        size_t length = 0;
        uint64_t give_up = mime_now_ns() + 200ull * 1000000ull;
        while (length < sizeof(reply) - 1) {
            int64_t left = (int64_t)(give_up - mime_now_ns()) / 1000000;
            struct pollfd p = {.fd = STDIN_FILENO, .events = POLLIN};
            if (left <= 0 || poll(&p, 1, (int)left) <= 0)
                break;
            ssize_t got = read(STDIN_FILENO, reply + length, 1);
            if (got <= 0)
                break;
            length++;
            if (reply[length - 1] == 'R')
                break;
        }
        reply[length] = 0;
        unsigned row, column;
        const char *start = strstr(reply, "\x1b[");
        if (start && sscanf(start, "\x1b[%u;%uR", &row, &column) == 2 && row && column && row < 0x7FFF && column < 0x7FFF)
            result = column | (row << 16);
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &saved);
    return result;
}

/* runs a guest program with the terminal as its console; returns the shell status of its exit. */
static int run_program(const MimePaths *paths, int argc, char **argv)
{
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
    header.console_cursor = terminal_cursor();

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
    bool exited = false;
    uint32_t program_code = 0;
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
                        /* GUI programs continue after the console wait ends. */
                        done = true;
                        exit_code = 128 + sig;
                        break;
                    }
                    value = sig == SIGINT ? 0 : 1; /* CTRL_C_EVENT, CTRL_BREAK_EVENT. */
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
            restore_terminal();
            mime_error("lost connection to the machine");
            break;
        }
        switch (h.type) {
        case MIME_IPC_OUTPUT:
            track_output(STDOUT_FILENO, msg, h.size);
            write_all(STDOUT_FILENO, msg, h.size);
            break;
        case MIME_IPC_STDERR:
            track_output(STDERR_FILENO, msg, h.size);
            write_all(STDERR_FILENO, msg, h.size);
            break;
        case MIME_IPC_DEBUG:
            if (g_raw_terminal) {
                /* the terminal no longer turns a line feed into a return too. */
                const char *text = msg;
                size_t from = 0;
                for (size_t i = 0; i < h.size; i++) {
                    if (text[i] == '\n') {
                        write_all(STDERR_FILENO, text + from, i - from);
                        write_all(STDERR_FILENO, "\r\n", 2);
                        from = i + 1;
                    }
                }
                write_all(STDERR_FILENO, text + from, h.size - from);
            } else {
                write_all(STDERR_FILENO, msg, h.size);
            }
            break;
        case MIME_IPC_STARTED: {
            uint32_t subsystem = 0;
            if (h.size >= sizeof(subsystem))
                memcpy(&subsystem, msg, sizeof(subsystem));
            console = subsystem == MIME_SUBSYSTEM_CUI;
            if (!console) {
                exit_code = 0;
                done = true;
                break;
            }
            if (console) {
                input_open = true;
                if (header.flags & MIME_RUN_TTY_INPUT) {
                    g_termios_saved = tcgetattr(STDIN_FILENO, &g_saved_termios) == 0;
                    atexit(restore_terminal);
                    /* the console edits the line itself when it shows it on this terminal. */
                    if (header.flags & (MIME_RUN_TTY_OUTPUT | MIME_RUN_TTY_ERROR))
                        enter_raw_terminal();
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
            restore_terminal();
            mime_error("%s", (char *)msg);
            done = true;
            break;
        case MIME_IPC_EXIT:
            if (h.size >= 4) {
                memcpy(&program_code, msg, 4);
                exited = true;
            }
            done = true;
            break;
        default:
            break;
        }
        free(msg);
    }
    restore_terminal();
    close(fd);
    return exited ? shell_status(program_code) : exit_code;
}

/* ------------------------------------------------------------------ the machine's shell */

#define OPENWITH "C:\\Windows\\System32\\openwith.exe"

/* runs openwith.exe with a command, then the arguments. */
static int run_openwith(const MimePaths *paths, const char *command, bool admin, const char *first, int argc,
                        char **argv)
{
    char **args = mime_xcalloc((size_t)argc + 6, sizeof(char *));
    int n = 0;
    args[n++] = OPENWITH;
    args[n++] = (char *)command;
    if (admin)
        args[n++] = "--admin";
    if (first)
        args[n++] = (char *)first;
    for (int i = 0; i < argc; i++)
        args[n++] = argv[i];
    int r = run_program(paths, n, args);
    free(args);
    return r;
}

static bool is_program(const char *path)
{
    size_t n = strlen(path);
    return n > 4 && (strcasecmp(path + n - 4, ".exe") == 0 || strcasecmp(path + n - 4, ".com") == 0);
}

/*
 * programs start directly. anything else goes to openwith: an app by its id or name, or what the run box opens, a
 * program found on the path or under App Paths, a document with its program, a shortcut, a url.
 */
int mime_cmd_run(const MimePaths *paths, int argc, char **argv)
{
    bool admin = false;
    if (argc > 0 && strcmp(argv[0], "--admin") == 0) {
        admin = true;
        argc--;
        argv++;
    }
    if (argc < 1 || !strncmp(argv[0], "--", 2)) {
        fprintf(stderr, "usage: mime run [--admin] <target> [args]\n");
        return 2;
    }
    const char *target = argv[0];
    char guest[MIME_PATH_MAX];
    bool windows = ((target[0] | 0x20) >= 'a' && (target[0] | 0x20) <= 'z') && target[1] == ':';
    /* elevation goes through the shell's runas verb, so programs go to openwith too. */
    if (windows) {
        if (is_program(target) && !admin)
            return run_program(paths, argc, argv);
    } else {
        char cwd[MIME_PATH_MAX], joined[MIME_PATH_MAX], resolved[PATH_MAX];
        if (target[0] == '/')
            mime_strlcpy(joined, target, sizeof(joined));
        else if (getcwd(cwd, sizeof(cwd)))
            mime_path_join(joined, sizeof(joined), cwd, target);
        else
            mime_strlcpy(joined, target, sizeof(joined));
        bool path = target[0] == '/' || !strncmp(target, "./", 2) || !strncmp(target, "../", 3);
        if (realpath(joined, resolved)) {
            if (is_program(resolved) && !mime_dir_exists(resolved) && !admin)
                return run_program(paths, argc, argv);
            mime_paths_to_guest(paths, resolved, guest, sizeof(guest));
            target = guest;
        } else if (path) {
            /* windows names ignore case, so a path the host misses, like Steam.exe for steam.exe, can still be a file of C:. */
            mime_paths_to_guest(paths, joined, guest, sizeof(guest));
            target = guest;
        }
    }
    return run_openwith(paths, "open", admin, target, argc - 1, argv + 1);
}

int mime_cmd_apps(const MimePaths *paths, int argc, char **argv)
{
    if (argc == 0)
        return run_openwith(paths, "apps", false, nullptr, 0, nullptr);
    if (argc == 1 && strcmp(argv[0], "--installed") == 0)
        return run_openwith(paths, "installed", false, nullptr, 0, nullptr);
    fprintf(stderr, "usage: mime apps [--installed]\n");
    return 2;
}

int mime_cmd_uninstall(const MimePaths *paths, int argc, char **argv)
{
    if (argc != 1) {
        fprintf(stderr, "usage: mime uninstall <id>\n");
        return 2;
    }
    return run_openwith(paths, "uninstall", false, nullptr, 1, argv);
}

/*
 * the features that are fetched from Microsoft when a program needs them. features.exe in the machine does the work,
 * the files it is given can be host paths.
 */
int mime_cmd_features(const MimePaths *paths, int argc, char **argv)
{
    const char *verb = argc > 0 ? argv[0] : "list";
    bool list = strcmp(verb, "list") == 0;
    bool name_needed = strcmp(verb, "status") == 0 || strcmp(verb, "enable") == 0 || strcmp(verb, "disable") == 0;
    if ((argc > 0 && !list && !name_needed) || (list && argc > 1) || (name_needed && argc < 2)) {
        fprintf(stderr, "usage: mime features [list | status <name> | enable <name> [--source file] | disable <name>]\n");
        return 2;
    }
    /* a source file given as a host path is the same file in the machine. */
    char **args = mime_xcalloc((size_t)argc + 1, sizeof(char *));
    char converted[MIME_PATH_MAX];
    int n = 0;
    for (int i = 1; i < argc; i++) {
        args[n++] = argv[i];
        if (strcmp(argv[i], "--source") == 0 && i + 1 < argc) {
            char resolved[PATH_MAX];
            const char *file = argv[++i];
            if (file[0] != '\0' && !(((file[0] | 0x20) >= 'a' && (file[0] | 0x20) <= 'z') && file[1] == ':') &&
                realpath(file, resolved)) {
                mime_paths_to_guest(paths, resolved, converted, sizeof(converted));
                file = converted;
            }
            args[n++] = (char *)file;
        }
    }
    int result = run_openwith(paths, "features", false, verb, n, args);
    free(args);
    return result;
}

