/*
 * watches the memory of the host and asks the guest to give pages back when it runs short. the guest already hands
 * free pages to the host as it frees them with MIME_HC_MEM_REPORT. this is for the memory it keeps because it is cheap to
 * keep: file windows the host maps into the process, pages in the processors' caches, the cache of the file system.
 *
 * the host is short when MemAvailable falls below a share of its memory or the kernel's own stall figures for memory
 * the pressure stall information say tasks wait for it. leaving a level takes a margin, so a host that sits on the line
 * does not make the guest trim twice a second.
 *   MIME_MEMORY_LOW_MB  MemAvailable under which the host is short, in megabytes; half of it is critical. for tests.
 *   MIME_MEMORY_LOW_FILE  a file holding that number, read every second, so a test can make the host short while it runs.
 *
 * the guest sees all of the host's memory, but a page it has not written yet costs the host nothing, so before it starts
 * on a host memory block it asks for it with MIME_HC_MEM_GRANT. a grant never takes MemAvailable below a floor left for
 * the desktop: past it the guest's allocations and commits fail as they would on a Windows machine out of memory, and
 * the host never has to kill for the guest. half of the free swap counts as well, the way windows counts its page file:
 * the guest's memory is plain process memory the kernel may swap out, and the half leaves room for a zram swap's own
 * pages and for the desktop's.
 */
#include "../vmm/vmm.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define POLL_MS 1000
#define REPEAT_MS 4000 /* a host that stays short is asked again this often. */

typedef struct HostMemory {
    uint64_t total_mb, available_mb;
    double some, full; /* avg10 of the memory pressure, percent. */
} HostMemory;

static bool read_host(HostMemory *m)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f)
        return false;
    char line[128];
    unsigned long long total = 0, avail = 0;
    while (fgets(line, sizeof(line), f)) {
        sscanf(line, "MemTotal: %llu kB", &total);
        sscanf(line, "MemAvailable: %llu kB", &avail);
    }
    fclose(f);
    if (!total)
        return false;
    m->total_mb = total / 1024;
    m->available_mb = avail / 1024;
    m->some = m->full = 0;
    f = fopen("/proc/pressure/memory", "r");
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            double v;
            if (sscanf(line, "some avg10=%lf", &v) == 1)
                m->some = v;
            else if (sscanf(line, "full avg10=%lf", &v) == 1)
                m->full = v;
        }
        fclose(f);
    }
    return true;
}

static pthread_mutex_t g_grant_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_meminfo = -1;
static uint64_t g_floor;

/*
 * MemAvailable in bytes, and SwapFree when swap is asked for. the kernel works them out on each read, so they count
 * what the GPU driver and every other program took.
 */
static uint64_t available_bytes(uint64_t *swap)
{
    if (g_meminfo < 0)
        g_meminfo = open("/proc/meminfo", O_RDONLY | O_CLOEXEC);
    char text[2048];
    ssize_t n = g_meminfo >= 0 ? pread(g_meminfo, text, sizeof(text) - 1, 0) : -1;
    if (n <= 0)
        return 0;
    text[n] = 0;
    unsigned long long total = 0, avail = 0;
    const char *t = strstr(text, "MemTotal:");
    const char *a = strstr(text, "MemAvailable:");
    if (t)
        sscanf(t, "MemTotal: %llu kB", &total);
    if (!a || sscanf(a, "MemAvailable: %llu kB", &avail) != 1)
        return 0;
    if (!g_floor)
        g_floor = MIME_MAX(640ull << 20, (uint64_t)total * 1024 / 100 * 6);
    if (swap) {
        unsigned long long free = 0;
        const char *f = strstr(text, "SwapFree:");
        if (f)
            sscanf(f, "SwapFree: %llu kB", &free);
        *swap = (uint64_t)free * 1024;
    }
    return (uint64_t)avail * 1024;
}

/*
 * a machine started in a cgroup with a memory limit, a systemd scope with MemoryMax= or MemoryHigh= for one, gets no
 * more than its limits leave: what each ancestor with a limit still has, its page cache counted as free since the
 * kernel drops that first. a margin stays for what the host process itself allocates in it.
 */
#define CGROUP_LEVELS 16
#define CGROUP_MARGIN (256ull << 20)

typedef struct CgroupLimit {
    int max, high, current, stat;
} CgroupLimit;

static CgroupLimit g_limits[CGROUP_LEVELS];
static int g_limit_count = -1;

static uint64_t read_number(int fd)
{
    char text[32];
    ssize_t n = fd >= 0 ? pread(fd, text, sizeof(text) - 1, 0) : -1;
    if (n <= 0 || text[0] == 'm')
        return UINT64_MAX;
    text[n] = 0;
    return strtoull(text, nullptr, 10);
}

static uint64_t read_file_cache(int fd)
{
    char text[256];
    ssize_t n = fd >= 0 ? pread(fd, text, sizeof(text) - 1, 0) : -1;
    if (n <= 0)
        return 0;
    text[n] = 0;
    const char *f = strstr(text, "\nfile ");
    return f ? strtoull(f + 6, nullptr, 10) : 0;
}

static void open_limits(void)
{
    g_limit_count = 0;
    char line[512] = {}, path[640];
    FILE *f = fopen("/proc/self/cgroup", "r");
    if (!f)
        return;
    while (fgets(line, sizeof(line), f) && strncmp(line, "0::", 3))
        ;
    fclose(f);
    if (strncmp(line, "0::", 3))
        return;
    char *group = line + 3;
    group[strcspn(group, "\n")] = 0;
    while (*group && g_limit_count < CGROUP_LEVELS) {
        CgroupLimit l;
        snprintf(path, sizeof(path), "/sys/fs/cgroup%s/memory.max", group);
        l.max = open(path, O_RDONLY | O_CLOEXEC);
        snprintf(path, sizeof(path), "/sys/fs/cgroup%s/memory.high", group);
        l.high = open(path, O_RDONLY | O_CLOEXEC);
        if (MIME_MIN(read_number(l.max), read_number(l.high)) != UINT64_MAX) {
            snprintf(path, sizeof(path), "/sys/fs/cgroup%s/memory.current", group);
            l.current = open(path, O_RDONLY | O_CLOEXEC);
            snprintf(path, sizeof(path), "/sys/fs/cgroup%s/memory.stat", group);
            l.stat = open(path, O_RDONLY | O_CLOEXEC);
            g_limits[g_limit_count++] = l;
        } else {
            if (l.max >= 0)
                close(l.max);
            if (l.high >= 0)
                close(l.high);
        }
        char *slash = strrchr(group, '/');
        if (!slash)
            break;
        *slash = 0;
    }
}

static uint64_t cgroup_room(void)
{
    if (g_limit_count < 0)
        open_limits();
    uint64_t room = UINT64_MAX;
    for (int i = 0; i < g_limit_count; i++) {
        uint64_t limit = MIME_MIN(read_number(g_limits[i].max), read_number(g_limits[i].high));
        uint64_t current = read_number(g_limits[i].current), cache = read_file_cache(g_limits[i].stat);
        uint64_t used = current > cache ? current - cache : 0;
        uint64_t left = limit > used + CGROUP_MARGIN ? limit - used - CGROUP_MARGIN : 0;
        room = MIME_MIN(room, left);
    }
    return room;
}

/* the lock is held. what the guest was granted and has not touched yet is still counted as free. */
static uint64_t grantable(MimeVm *vm)
{
    uint64_t swap = 0, avail = available_bytes(&swap);
    avail += swap / 2;
    avail = avail > g_floor ? avail - g_floor : 0;
    avail = MIME_MIN(avail, cgroup_room());
    uint64_t credit = __atomic_load_n(&vm->shared->memory_credit, __ATOMIC_ACQUIRE);
    return avail > credit ? avail - credit : 0;
}

uint64_t mime_memory_grant(MimeVm *vm, uint64_t want)
{
    pthread_mutex_lock(&g_grant_lock);
    uint64_t can = grantable(vm);
    uint64_t granted = MIME_MIN(want, can);
    __atomic_store_n(&vm->shared->memory_grantable, can - granted, __ATOMIC_RELEASE);
    pthread_mutex_unlock(&g_grant_lock);
    return granted;
}

/* memory the GPU driver pins for the guest cannot be swapped out, so swap does not count here. */
uint64_t mime_memory_room(void)
{
    pthread_mutex_lock(&g_grant_lock);
    uint64_t avail = available_bytes(nullptr);
    avail = avail > g_floor ? avail - g_floor : 0;
    avail = MIME_MIN(avail, cgroup_room());
    pthread_mutex_unlock(&g_grant_lock);
    return avail;
}

void mime_memory_publish(MimeVm *vm)
{
    pthread_mutex_lock(&g_grant_lock);
    __atomic_store_n(&vm->shared->memory_grantable, grantable(vm), __ATOMIC_RELEASE);
    pthread_mutex_unlock(&g_grant_lock);
}

static uint64_t resident_mb(void)
{
    FILE *f = fopen("/proc/self/statm", "r");
    if (!f)
        return 0;
    unsigned long long size = 0, resident = 0;
    if (fscanf(f, "%llu %llu", &size, &resident) != 2)
        resident = 0;
    fclose(f);
    return resident * (uint64_t)sysconf(_SC_PAGESIZE) >> 20;
}

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/*
 * file pages the guest touched stay mapped in this process. they are clean copies of the page cache, so dropping the
 * mappings costs the guest a minor fault for each page it reads again, and lets the kernel have the pages.
 */
static void drop_window_mappings(MimeVm *vm)
{
    if (vm->window_host)
        madvise(vm->window_host, MIME_WINDOW_SIZE, MADV_DONTNEED);
}

/*
 * the stall figures lag, a burst of writeback shows in them, and a desktop that swaps a little always has some, so they
 * count only when MemAvailable is no longer plentiful and after they have been seen twice in a row.
 */
static uint32_t classify(uint32_t level, const HostMemory *m, uint64_t low_mb, uint64_t critical_mb)
{
    static uint32_t stalled;
    stalled = m->some > 10.0 || m->full > 2.0 ? stalled + 1 : 0;
    bool stall = stalled >= 2;
    bool critical = m->available_mb < critical_mb || (stall && m->available_mb < m->total_mb / 5 && (m->full > 5.0 || m->some > 30.0));
    bool low = m->available_mb < low_mb || (stall && m->available_mb < m->total_mb * 3 / 10);
    if (level == MIME_MEMORY_NORMAL)
        return critical ? MIME_MEMORY_CRITICAL : low ? MIME_MEMORY_LOW : MIME_MEMORY_NORMAL;
    /* leaving needs a margin. */
    bool still_critical = critical || m->available_mb < critical_mb * 14 / 10;
    bool still_low = low || m->available_mb < low_mb * 14 / 10 || (m->some > 4.0 && m->available_mb < m->total_mb * 3 / 10);
    if (level == MIME_MEMORY_CRITICAL && still_critical)
        return MIME_MEMORY_CRITICAL;
    return still_low ? MIME_MEMORY_LOW : MIME_MEMORY_NORMAL;
}

static void *monitor_main(void *arg)
{
    MimeVm *vm = arg;
    mime_set_thread_name("mime-memory");
    uint64_t env_override = 0;
    const char *env = getenv("MIME_MEMORY_LOW_MB");
    if (env && atoi(env) > 0)
        env_override = (uint64_t)atoi(env);
    const char *low_file = getenv("MIME_MEMORY_LOW_FILE");
    uint32_t level = MIME_MEMORY_NORMAL;
    uint64_t asked_ms = 0;
    for (;;) {
        struct timespec nap = {POLL_MS / 1000, (POLL_MS % 1000) * 1000000l};
        nanosleep(&nap, nullptr);
        if (vm->stopping)
            return nullptr;
        mime_memory_publish(vm);
        HostMemory m;
        if (!read_host(&m))
            continue;
        uint64_t low_override = env_override;
        if (low_file) {
            FILE *f = fopen(low_file, "r");
            int mb = 0;
            if (f && fscanf(f, "%d", &mb) == 1 && mb > 0)
                low_override = (uint64_t)mb;
            if (f)
                fclose(f);
        }
        uint64_t low_mb = low_override ? low_override : MIME_MAX(768, m.total_mb * 12 / 100);
        uint64_t critical_mb = low_override ? low_override / 2 : MIME_MAX(384, m.total_mb * 5 / 100);
        uint32_t want = classify(level, &m, low_mb, critical_mb);
        uint64_t now = now_ms();
        bool changed = want != level;
        bool again = want != MIME_MEMORY_NORMAL && now - asked_ms >= REPEAT_MS;
        if (!changed && !again)
            continue;
        /* the guest has nothing to give while this process holds little, but its programs still hear about the level. */
        if (want != MIME_MEMORY_NORMAL && (changed || resident_mb() >= 256))
            drop_window_mappings(vm);
        if (changed)
            mime_info("memory: host %s, %llu of %llu MB available, pressure %.1f%%, process holds %llu MB",
                      want == MIME_MEMORY_CRITICAL ? "critically short" : want == MIME_MEMORY_LOW ? "short" : "has memory again",
                      (unsigned long long)m.available_mb, (unsigned long long)m.total_mb, m.some,
                      (unsigned long long)resident_mb());
        level = want;
        asked_ms = now;
        __atomic_store_n(&vm->shared->memory_pressure, level, __ATOMIC_RELEASE);
        __atomic_fetch_add(&vm->shared->memory_serial, 1, __ATOMIC_ACQ_REL);
        mime_vm_raise_irq(vm, MIME_IRQ_MEMORY);
    }
}

void mime_memory_monitor_start(MimeVm *vm)
{
    pthread_t thread;
    if (pthread_create(&thread, nullptr, monitor_main, vm) == 0)
        pthread_detach(thread);
}
