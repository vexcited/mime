/*
 * the background machine process: owns the virtual
 * machine, the cocoa run loop and the control socket.
 */
#include "instance.h"
#include "ipc.h"
#include "mime_cli.h"
#include "ui/ui.h"
#include "vmm/vmm.h"
#ifdef __linux__
#include "gpu/gpu.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <execinfo.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#include <ucontext.h>
#endif

#define MAX_RUNS 256

/*
 * a `mime run` connection. its client thread owns the descriptor and reads
 * the terminal's input and signals from it until the client hangs up;
 * guest events write to it until the run is finished.
 */
typedef struct PendingRun {
    bool used;
    bool finished; /* the exit or an error was sent */
    uint64_t request_id;
    int fd;
    uint32_t in_flight; /* console input sent and not read by the guest yet */
} PendingRun;

static MimeVm g_vm;
static const MimePaths *g_paths;

const MimePaths *mime_daemon_paths(void)
{
    return g_paths;
}

static int g_listen_fd = -1;
static uint64_t g_start_ns;
static uint64_t g_next_request = 1;
static pthread_mutex_t g_runs_lock = PTHREAD_MUTEX_INITIALIZER;
static PendingRun g_runs[MAX_RUNS];
static pthread_cond_t g_credit = PTHREAD_COND_INITIALIZER; /* in_flight went down */
static int g_stop_fds[16];
static int g_stop_count;

static void cleanup_files(void)
{
    if (g_paths) {
        unlink(g_paths->socket);
        unlink(g_paths->pidfile);
    }
}

static PendingRun *find_run(uint64_t request_id)
{
    for (int i = 0; i < MAX_RUNS; i++)
        if (g_runs[i].used && g_runs[i].request_id == request_id)
            return &g_runs[i];
    return nullptr;
}

/* sends to the client of a run; an exit or an error finishes it. */
static void notify_run(uint64_t request_id, uint32_t type, const void *payload, uint32_t size)
{
    pthread_mutex_lock(&g_runs_lock);
    PendingRun *r = find_run(request_id);
    if (r && !r->finished) {
        mime_ipc_send(r->fd, type, payload, size);
        if (type == MIME_IPC_EXIT || type == MIME_IPC_ERROR) {
            r->finished = true;
            pthread_cond_broadcast(&g_credit);
        }
    }
    pthread_mutex_unlock(&g_runs_lock);
}

static const char *status_text(uint32_t status)
{
    switch (status) {
    case 0xC0000034: return "file not found";
    case 0xC000003A: return "path not found";
    case 0xC000007B: return "invalid or unsupported windows executable";
    case 0xC0000135: return "a required dll was not found";
    case 0xC0000139: return "an imported function was not found";
    case 0xC0000017: return "out of memory";
    case 0xC0000022: return "access denied";
    default: return "process creation failed";
    }
}

void mime_daemon_guest_event(MimeVm *vm, const MimeCtrlHeader *msg)
{
    switch (msg->type) {
    case MIME_EVT_READY:
        mime_info("guest kernel is ready");
        vm->guest_ready = true;
        break;
    case MIME_EVT_PROCESS_STARTED: {
        const MimeEvtProcess *p = (const MimeEvtProcess *)msg;
        mime_info("request %llu started as process %u", (unsigned long long)p->request_id, p->process_id);
        uint32_t subsystem = p->status;
        notify_run(p->request_id, MIME_IPC_STARTED, &subsystem, sizeof(subsystem));
        break;
    }
    case MIME_EVT_CONSOLE_OUTPUT:
    case MIME_EVT_CONSOLE_MODE:
    case MIME_EVT_CONSOLE_READ: {
        const MimeCtrlConsole *c = (const MimeCtrlConsole *)msg;
        if (msg->size < sizeof(*c) || sizeof(*c) + c->length > msg->size)
            break;
        if (msg->type == MIME_EVT_CONSOLE_OUTPUT) {
            notify_run(c->request_id, c->value == 2 ? MIME_IPC_STDERR : MIME_IPC_OUTPUT, c + 1, c->length);
        } else if (msg->type == MIME_EVT_CONSOLE_MODE) {
            notify_run(c->request_id, MIME_IPC_CONSOLE_MODE, &c->value, sizeof(c->value));
        } else {
            pthread_mutex_lock(&g_runs_lock);
            PendingRun *r = find_run(c->request_id);
            if (r)
                r->in_flight -= MIME_MIN(r->in_flight, c->value);
            pthread_cond_broadcast(&g_credit);
            pthread_mutex_unlock(&g_runs_lock);
        }
        break;
    }
    case MIME_EVT_PROCESS_EXIT: {
        const MimeEvtProcess *p = (const MimeEvtProcess *)msg;
        mime_info("process %u exited with code %u", p->process_id, p->status);
        mime_ui_process_exit(p->process_id);
        int32_t code = (int32_t)p->status;
        notify_run(p->request_id, MIME_IPC_EXIT, &code, sizeof(code));
        break;
    }
    case MIME_EVT_RUN_FAILED: {
        const MimeEvtProcess *p = (const MimeEvtProcess *)msg;
        char text[160];
        snprintf(text, sizeof(text), "%s (status 0x%08X)", status_text(p->status), p->status);
        mime_warn("request %llu failed: %s", (unsigned long long)p->request_id, text);
        notify_run(p->request_id, MIME_IPC_ERROR, text, (uint32_t)strlen(text) + 1);
        break;
    }
    case MIME_EVT_DEBUG_OUTPUT: {
        const MimeEvtDebugOutput *d = (const MimeEvtDebugOutput *)msg;
        if (sizeof(*d) + d->length > msg->size)
            break;
        const char *text = (const char *)(d + 1);
        mime_info("debug output [pid %u]: %.*s", d->process_id, (int)d->length, text);
        if (d->request_id)
            notify_run(d->request_id, MIME_IPC_DEBUG, text, d->length);
        break;
    }
    default:
        mime_warn("unknown guest event %u", msg->type);
        break;
    }
}

void mime_daemon_guest_halted(MimeVm *vm, int exit_code)
{
    pthread_mutex_lock(&g_runs_lock);
    for (int i = 0; i < MAX_RUNS; i++) {
        if (g_runs[i].used && !g_runs[i].finished) {
            const char *text = "the machine stopped";
            mime_ipc_send(g_runs[i].fd, MIME_IPC_ERROR, text, (uint32_t)strlen(text) + 1);
            g_runs[i].finished = true;
            /* wakes the client thread, which closes the connection. */
            shutdown(g_runs[i].fd, SHUT_RDWR);
        }
    }
    pthread_cond_broadcast(&g_credit);
    for (int i = 0; i < g_stop_count; i++) {
        mime_ipc_send(g_stop_fds[i], MIME_IPC_OK, nullptr, 0);
        close(g_stop_fds[i]);
    }
    g_stop_count = 0;
    pthread_mutex_unlock(&g_runs_lock);
    vm->halted = true;
    cleanup_files();
    mime_ui_stop();
}

static bool is_windows_path(const char *p)
{
    return ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':';
}

static void append_arg(char *cmdline, size_t size, const char *arg)
{
    size_t n = strlen(cmdline);
    bool quote = !*arg || strpbrk(arg, " \t\"") != nullptr;
    if (n && n + 1 < size)
        cmdline[n++] = ' ';
    if (quote && n + 1 < size)
        cmdline[n++] = '"';
    for (const char *p = arg; *p && n + 2 < size; p++) {
        if (*p == '"')
            cmdline[n++] = '\\';
        cmdline[n++] = *p;
    }
    if (quote && n + 1 < size)
        cmdline[n++] = '"';
    cmdline[n] = 0;
}

/* starts a run; returns its request id, or 0 after answering with an error and closing. */
static uint64_t handle_run(int fd, char *payload, uint32_t size)
{
    char *args[256];
    int argc = 0;
    if (size < sizeof(MimeIpcRun) + 1) {
        close(fd);
        return 0;
    }
    MimeIpcRun header;
    memcpy(&header, payload, sizeof(header));
    payload += sizeof(header);
    size -= sizeof(header);
    char *cwd = payload;
    char *p = payload + strlen(payload) + 1;
    while (p < payload + size && argc < 256) {
        args[argc++] = p;
        p += strlen(p) + 1;
    }
    if (argc < 1) {
        const char *text = "no program given";
        mime_ipc_send(fd, MIME_IPC_ERROR, text, (uint32_t)strlen(text) + 1);
        close(fd);
        return 0;
    }

    char image[MIME_PATH_MAX], guest_cwd[MIME_PATH_MAX];
    if (is_windows_path(args[0])) {
        mime_strlcpy(image, args[0], sizeof(image));
        mime_strlcpy(guest_cwd, "C:\\Windows\\System32", sizeof(guest_cwd));
        mime_paths_to_guest(g_paths, cwd, guest_cwd, sizeof(guest_cwd));
    } else {
        char joined[MIME_PATH_MAX], resolved[PATH_MAX];
        if (args[0][0] == '/')
            mime_strlcpy(joined, args[0], sizeof(joined));
        else
            mime_path_join(joined, sizeof(joined), cwd, args[0]);
        if (!realpath(joined, resolved)) {
            char text[MIME_PATH_MAX + 64];
            snprintf(text, sizeof(text), "%s: %s", args[0], strerror(errno));
            mime_ipc_send(fd, MIME_IPC_ERROR, text, (uint32_t)strlen(text) + 1);
            close(fd);
            return 0;
        }
        mime_paths_to_guest(g_paths, resolved, image, sizeof(image));
        mime_paths_to_guest(g_paths, cwd, guest_cwd, sizeof(guest_cwd));
    }

    char cmdline[8192] = {};
    append_arg(cmdline, sizeof(cmdline), image);
    for (int i = 1; i < argc; i++)
        append_arg(cmdline, sizeof(cmdline), args[i]);

    uint16_t w_image[MIME_PATH_MAX], w_cmdline[8192], w_cwd[MIME_PATH_MAX];
    uint32_t n_image = (uint32_t)mime_utf8_to_utf16(w_image, MIME_ARRAY_COUNT(w_image), image);
    uint32_t n_cmd = (uint32_t)mime_utf8_to_utf16(w_cmdline, MIME_ARRAY_COUNT(w_cmdline), cmdline);
    uint32_t n_cwd = (uint32_t)mime_utf8_to_utf16(w_cwd, MIME_ARRAY_COUNT(w_cwd), guest_cwd);
    uint32_t msg_size = (uint32_t)(sizeof(MimeCtrlRun) + (n_image + n_cmd + n_cwd) * 2);
    if (msg_size > MIME_CTRL_MAX_MESSAGE) {
        const char *text = "command line too long";
        mime_ipc_send(fd, MIME_IPC_ERROR, text, (uint32_t)strlen(text) + 1);
        close(fd);
        return 0;
    }
    uint8_t *buf = mime_xcalloc(1, msg_size);
    MimeCtrlRun *run = (MimeCtrlRun *)buf;
    run->header.type = MIME_CTRL_RUN;
    run->header.size = msg_size;
    run->image_chars = n_image;
    run->cmdline_chars = n_cmd;
    run->cwd_chars = n_cwd;
    run->flags = header.flags & (MIME_RUN_TTY_INPUT | MIME_RUN_TTY_OUTPUT | MIME_RUN_TTY_ERROR);
    run->console_size = header.console_size;
    uint16_t *strings = (uint16_t *)(run + 1);
    memcpy(strings, w_image, n_image * 2);
    memcpy(strings + n_image, w_cmdline, n_cmd * 2);
    memcpy(strings + n_image + n_cmd, w_cwd, n_cwd * 2);

    pthread_mutex_lock(&g_runs_lock);
    int slot = -1;
    for (int i = 0; i < MAX_RUNS; i++) {
        if (!g_runs[i].used) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        pthread_mutex_unlock(&g_runs_lock);
        free(buf);
        const char *text = "too many running programs";
        mime_ipc_send(fd, MIME_IPC_ERROR, text, (uint32_t)strlen(text) + 1);
        close(fd);
        return 0;
    }
    run->request_id = g_next_request++;
    g_runs[slot] = (PendingRun){.used = true, .request_id = run->request_id, .fd = fd};
    pthread_mutex_unlock(&g_runs_lock);

    mime_info("run request %llu: %s (cwd %s)", (unsigned long long)run->request_id, cmdline, guest_cwd);
    uint64_t id = run->request_id;
    mime_vm_queue_ctrl(&g_vm, buf, msg_size);
    free(buf);
    return id;
}

static void queue_console(uint32_t type, uint64_t request, uint32_t value, const void *data, uint32_t length)
{
    uint8_t buf[sizeof(MimeCtrlConsole) + 4096];
    MimeCtrlConsole *c = (MimeCtrlConsole *)buf;
    c->header.type = type;
    c->header.size = (uint32_t)(sizeof(*c) + length);
    c->request_id = request;
    c->value = value;
    c->length = length;
    if (length)
        memcpy(c + 1, data, length);
    mime_vm_queue_ctrl(&g_vm, buf, c->header.size);
}

/*
 * relays the terminal of a run to its console until the client hangs up:
 * input as the guest takes it, ctrl events and size changes. the hangup
 * closes the console, which the guest's programs see as CTRL_CLOSE_EVENT.
 */
static void serve_run(int fd, uint64_t request)
{
    MimeIpcHeader h;
    void *payload;
    while (mime_ipc_recv(fd, &h, &payload)) {
        const uint8_t *data = payload;
        if (h.type == MIME_IPC_INPUT) {
            uint32_t off = 0;
            do {
                uint32_t n = MIME_MIN(h.size - off, 4096u);
                pthread_mutex_lock(&g_runs_lock);
                PendingRun *r = find_run(request);
                while (r && !r->finished && r->in_flight + n > MIME_CONSOLE_INPUT_WINDOW)
                    pthread_cond_wait(&g_credit, &g_runs_lock);
                bool live = r && !r->finished;
                if (live)
                    r->in_flight += n;
                pthread_mutex_unlock(&g_runs_lock);
                if (!live)
                    break;
                queue_console(MIME_CTRL_CONSOLE_INPUT, request, 0, data + off, n);
                off += n;
            } while (off < h.size);
        } else if ((h.type == MIME_IPC_SIGNAL || h.type == MIME_IPC_RESIZE) && h.size >= sizeof(uint32_t)) {
            uint32_t value;
            memcpy(&value, data, sizeof(value));
            queue_console(h.type == MIME_IPC_SIGNAL ? MIME_CTRL_CONSOLE_EVENT : MIME_CTRL_CONSOLE_SIZE, request, value,
                          nullptr, 0);
        }
        free(payload);
    }
    queue_console(MIME_CTRL_CONSOLE_EVENT, request, 2 /* CTRL_CLOSE_EVENT */, nullptr, 0);
    pthread_mutex_lock(&g_runs_lock);
    PendingRun *r = find_run(request);
    if (r)
        r->used = false;
    pthread_cond_broadcast(&g_credit);
    pthread_mutex_unlock(&g_runs_lock);
    close(fd);
}


static void *client_thread(void *arg)
{
    int fd = (int)(intptr_t)arg;
    MimeIpcHeader h;
    void *payload;
    if (!mime_ipc_recv(fd, &h, &payload)) {
        close(fd);
        return nullptr;
    }
    switch (h.type) {
    case MIME_IPC_STATUS: {
        MimeIpcStatus st = {};
        st.ready = g_vm.guest_ready;
        st.pid = (uint32_t)getpid();
        st.uptime_ms = (mime_now_ns() - g_start_ns) / 1000000;
        pthread_mutex_lock(&g_runs_lock);
        for (int i = 0; i < MAX_RUNS; i++)
            st.running_programs += g_runs[i].used;
        pthread_mutex_unlock(&g_runs_lock);
        mime_ipc_send(fd, MIME_IPC_STATUS_REPLY, &st, sizeof(st));
        close(fd);
        break;
    }
    case MIME_IPC_RUN:
        if (!g_vm.guest_ready) {
            const char *text = "the machine is still booting";
            mime_ipc_send(fd, MIME_IPC_ERROR, text, (uint32_t)strlen(text) + 1);
            close(fd);
        } else {
            uint64_t request = handle_run(fd, payload, h.size);
            free(payload);
            payload = nullptr;
            if (request)
                serve_run(fd, request);
        }
        break;
    case MIME_IPC_STOP: {
        mime_info("shutdown requested");
        pthread_mutex_lock(&g_runs_lock);
        if (g_stop_count < (int)MIME_ARRAY_COUNT(g_stop_fds))
            g_stop_fds[g_stop_count++] = fd;
        else
            close(fd);
        pthread_mutex_unlock(&g_runs_lock);
        if (g_vm.guest_ready) {
            MimeCtrlShutdown msg = {{MIME_CTRL_SHUTDOWN, sizeof(MimeCtrlShutdown)}, 0, 0};
            if (payload && h.size >= sizeof(MimeIpcStop)) {
                const MimeIpcStop *stop = (const MimeIpcStop *)payload;
                msg.flags = stop->flags;
                msg.timeout_ms = stop->timeout_ms;
            }
            mime_vm_queue_ctrl(&g_vm, &msg, sizeof(msg));
        } else {
            mime_daemon_guest_halted(&g_vm, 0);
            mime_vm_request_stop(&g_vm, 0);
        }
        break;
    }
    default:
        close(fd);
        break;
    }
    free(payload);
    return nullptr;
}

static void *accept_thread(void *arg)
{
    mime_set_thread_name("mime-ipc");
    for (;;) {
        int fd = accept(g_listen_fd, nullptr, nullptr);
        if (fd < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
#ifdef SO_NOSIGPIPE
        int one = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
        pthread_t t;
        pthread_create(&t, nullptr, client_thread, (void *)(intptr_t)fd);
        pthread_detach(t);
    }
    return nullptr;
}

static void on_input(void *ctx, const MimeInputEvent *ev)
{
    /* the guest reads the new display from its boot information. */
    if (ev->type == MIME_INPUT_DISPLAY) {
        if (!g_vm.boot)
            return;
        mime_ui_get_display(&g_vm.boot->display);
        __atomic_thread_fence(__ATOMIC_RELEASE);
    }
    mime_vm_queue_input(&g_vm, ev);
}

static void on_signal(int sig)
{
    cleanup_files();
    _exit(128 + sig);
}

static void on_dump_signal(int sig)
{
    mime_vcpu_request_dump(&g_vm);
    mime_vm_raise_irq(&g_vm, MIME_IRQ_DUMP);
}

static void on_profile_signal(int sig) { mime_vcpu_toggle_profile(); }

#ifdef __linux__
/*
 * MIME_HOST_PROFILE=<file> samples the host's own code: a timer of the
 * process's cpu time interrupts whichever thread runs, its instruction
 * pointer and thread are kept, and the samples go to the file at exit
 * with the process's mappings, for tools/hostprof.py.
 */
#include <signal.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <ucontext.h>

#define HOST_PROFILE_MAX (4u << 20)
static uint64_t *g_host_ips;
static uint32_t *g_host_tids;
/* the names of the threads sampled, taken as they run, as many end before the dump. */
static struct {
    uint32_t tid;
    char name[16];
} g_host_names[512];
static uint32_t g_host_count;
static const char *g_host_profile;

static void on_host_sample(int sig, siginfo_t *info, void *context)
{
    uint32_t i = __atomic_fetch_add(&g_host_count, 1, __ATOMIC_RELAXED);
    if (i >= HOST_PROFILE_MAX)
        return;
    const ucontext_t *uc = context;
#if defined(__x86_64__)
    g_host_ips[i] = (uint64_t)uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__aarch64__)
    g_host_ips[i] = (uint64_t)uc->uc_mcontext.pc;
#endif
    uint32_t tid = (uint32_t)syscall(SYS_gettid);
    g_host_tids[i] = tid;
    uint32_t slot = tid % 512;
    if (g_host_names[slot].tid != tid) {
        g_host_names[slot].tid = tid;
        prctl(PR_GET_NAME, g_host_names[slot].name);
    }
}

static void host_profile_dump(void)
{
    FILE *out = fopen(g_host_profile, "w");
    if (!out)
        return;
    FILE *maps = fopen("/proc/self/maps", "r");
    char line[1024];
    while (maps && fgets(line, sizeof(line), maps))
        fprintf(out, "MAP %s", line);
    if (maps)
        fclose(maps);
    uint32_t n = MIME_MIN(g_host_count, HOST_PROFILE_MAX);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t slot = g_host_tids[i] % 512;
        const char *comm = g_host_names[slot].tid == g_host_tids[i] && g_host_names[slot].name[0] ? g_host_names[slot].name : "?";
        fprintf(out, "S %s %llx\n", comm, (unsigned long long)g_host_ips[i]);
    }
    fclose(out);
}

static void host_profile_start(void)
{
    g_host_profile = getenv("MIME_HOST_PROFILE");
    if (!g_host_profile || !*g_host_profile)
        return;
    g_host_ips = calloc(HOST_PROFILE_MAX, sizeof(*g_host_ips));
    g_host_tids = calloc(HOST_PROFILE_MAX, sizeof(*g_host_tids));
    if (!g_host_ips || !g_host_tids)
        return;
    struct sigaction sa = {};
    sa.sa_sigaction = on_host_sample;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigaction(SIGRTMIN + 3, &sa, nullptr);
    struct sigevent ev = {};
    ev.sigev_notify = SIGEV_SIGNAL;
    ev.sigev_signo = SIGRTMIN + 3;
    timer_t timer;
    if (timer_create(CLOCK_PROCESS_CPUTIME_ID, &ev, &timer) != 0)
        return;
    struct itimerspec spec = {{0, 500000}, {0, 500000}};
    timer_settime(timer, 0, &spec, nullptr);
    atexit(host_profile_dump);
}
#else
static void host_profile_start(void) {}
#endif

static bool listen_socket(const MimePaths *paths)
{
    unlink(paths->socket);
    g_listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_listen_fd < 0)
        return false;
    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    mime_strlcpy(addr.sun_path, paths->socket, sizeof(addr.sun_path));
    if (bind(g_listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(g_listen_fd, 16) != 0) {
        mime_error("cannot listen on %s: %s", paths->socket, strerror(errno));
        return false;
    }
    return true;
}

/*
 * a crash of the machine process logs where it happened, for llvm-symbolizer, then dies as it would have: the
 * handler's own backtrace stops at the signal frame, so the instruction and the frames of the thread that
 * faulted are taken from its context.
 */
static void on_fatal_signal(int sig, siginfo_t *info, void *context)
{
    static const char head[] = "mime-vmm: fatal signal, backtrace:\n";
    write(STDERR_FILENO, head, sizeof(head) - 1);
#ifdef __linux__
    char thread[32] = "thread ";
    prctl(PR_GET_NAME, thread + 7, 0, 0, 0);
    size_t length = strlen(thread);
    thread[length++] = '\n';
    write(STDERR_FILENO, thread, length);
#endif
    void *frames[64];
    int count = 0;
#if defined(__linux__) && defined(__x86_64__)
    const ucontext_t *uc = context;
    frames[count++] = (void *)uc->uc_mcontext.gregs[REG_RIP];
    void **frame = (void **)uc->uc_mcontext.gregs[REG_RBP];
    uintptr_t floor = (uintptr_t)uc->uc_mcontext.gregs[REG_RSP];
    /* no frame pointers in this build: the words on the stack include the return addresses. */
    for (int i = 0; i < 60 && count < 62; i++)
        frames[count++] = ((void **)floor)[i];
    while (count < 64 && frame && ((uintptr_t)frame & 7) == 0 && (uintptr_t)frame >= floor &&
           (uintptr_t)frame - floor < (16u << 20)) {
        frames[count++] = frame[1];
        floor = (uintptr_t)frame + 8;
        frame = (void **)frame[0];
    }
#elif defined(__linux__) && defined(__aarch64__)
    const ucontext_t *uc = context;
    frames[count++] = (void *)uc->uc_mcontext.pc;
    void **frame = (void **)uc->uc_mcontext.regs[29];
    uintptr_t floor = (uintptr_t)uc->uc_mcontext.sp;
    while (count < 64 && frame && ((uintptr_t)frame & 7) == 0 && (uintptr_t)frame >= floor &&
           (uintptr_t)frame - floor < (16u << 20)) {
        frames[count++] = frame[1];
        floor = (uintptr_t)frame + 8;
        frame = (void **)frame[0];
    }
#else
    count = backtrace(frames, 64);
#endif
    backtrace_symbols_fd(frames, count, STDERR_FILENO);
    signal(sig, SIG_DFL);
    raise(sig);
}

int mime_vmm_main(const MimePaths *paths, int lock_fd)
{
    g_paths = paths;
    g_start_ns = mime_now_ns();
    mime_log_set_prefix("mime-vmm");
    if (getenv("MIME_DEBUG"))
        mime_log_set_level(MIME_LOG_DEBUG);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGUSR1, on_dump_signal); /* kill -USR1 dumps the vcpu state to the log */
    signal(SIGPROF, on_profile_signal); /* kill -PROF starts and stops the profiler */
    host_profile_start();
    {
        /* loaded now, as the handler cannot load libgcc. */
        void *frame;
        backtrace(&frame, 1);
    }
    static constexpr int fatal[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
    for (size_t i = 0; i < sizeof(fatal) / sizeof(fatal[0]); i++) {
        struct sigaction action = {};
        action.sa_sigaction = on_fatal_signal;
        action.sa_flags = SA_SIGINFO;
        sigaction(fatal[i], &action, nullptr);
    }
    /* every guest file handle and file window holds a descriptor. */
    struct rlimit files;
    if (getrlimit(RLIMIT_NOFILE, &files) == 0) {
#ifdef OPEN_MAX
        files.rlim_cur = MIME_MIN(files.rlim_max, (rlim_t)OPEN_MAX);
#else
        files.rlim_cur = files.rlim_max;
#endif
        setrlimit(RLIMIT_NOFILE, &files);
        /*
         * the table of descriptors grows when a new one is past its end, and a process with threads waits a
         * grace period of rcu for the old table to go, several milliseconds in which no thread can open or close
         * anything: Helium's start did it five times, and stopped all its file calls at once. One process with
         * a single thread does not wait, so the table is made as long as it will be before the threads.
         */
        int wide = fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, (int)MIME_MIN(files.rlim_cur - 1, (rlim_t)65535));
        if (wide >= 0)
            close(wide);
    }

    if (!mime_paths_ensure(paths) || !listen_socket(paths))
        return 1;
#ifdef __linux__
    /* the driver takes 200 ms to load and make a device, which the boot and the first program's start overlap. */
    mime_vk_warm_up();
#endif
    char pid[32];
    int n = snprintf(pid, sizeof(pid), "%d\n", (int)getpid());
    mime_write_file(paths->pidfile, pid, (size_t)n);
    mime_instance_publish(lock_fd, paths);
    atexit(cleanup_files);

    mime_ui_init(on_input, nullptr);
    uint64_t ui_ns = mime_now_ns();

    uint64_t ram = mime_vm_default_ram();
    const char *ram_env = getenv("MIME_RAM_MB");
    if (ram_env && atoi(ram_env) >= 256)
        ram = MIME_MIN((uint64_t)atoi(ram_env) << 20, MIME_VMM_MAX_RAM);
    if (!mime_vm_create(&g_vm, paths, ram))
        return 1;
    uint64_t created_ns = mime_now_ns();
    mime_ui_get_display(&g_vm.boot->display);
    mime_info("display %ux%u at %u dpi", g_vm.boot->display.width, g_vm.boot->display.height,
              g_vm.boot->display.dpi);

    if (!mime_vm_load_kernel(&g_vm))
        return 1;
    if (!mime_vm_start(&g_vm))
        return 1;
    mime_info("startup: display %llu us, machine %llu us, kernel and processors %llu us",
              (unsigned long long)((ui_ns - g_start_ns) / 1000), (unsigned long long)((created_ns - ui_ns) / 1000),
              (unsigned long long)((mime_now_ns() - created_ns) / 1000));

    pthread_t t;
    pthread_create(&t, nullptr, accept_thread, nullptr);
    pthread_detach(t);

    mime_ui_run();
    mime_vm_shutdown(&g_vm);
    return g_vm.exit_code;
}
