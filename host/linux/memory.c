/*
 * watches the memory of the host and asks the guest to give pages back when it runs short. the guest already hands
 * free pages to the host as it frees them (MIME_HC_MEM_REPORT); this is for the memory it keeps because it is cheap to
 * keep: file windows the host maps into the process, pages in the processors' caches, the cache of the file system.
 *
 * the host is short when MemAvailable falls below a share of its memory or the kernel's own stall figures for memory
 * (the pressure stall information, which a program may read) say tasks wait for it. leaving a level takes a margin, so
 * a host that sits on the line does not make the guest trim twice a second.
 *   MIME_MEMORY_LOW_MB  MemAvailable under which the host is short, in megabytes; half of it is critical. for tests.
 *   MIME_MEMORY_LOW_FILE  a file holding that number, read every second, so a test can make the host short while it runs.
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
