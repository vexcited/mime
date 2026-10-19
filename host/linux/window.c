/*
 * host files placed in the guest by MIME_HC_FS_MAP, read only or, for a handle opened for writing and a guest that
 * asks, shared and writable: the guest's writes go to the page cache, where reads and writes of the file see them at
 * once. the window
 * range is reserved in the host process once, and the guest is given it
 * in a few large read only memory slots. a file is mapped over a part of
 * the reservation, from the host's page cache, and taken away by mapping
 * nothing over it: the guest reads what the host caches, a file is
 * cached once, only the pages the guest touches are read from disk, and
 * neither placing a window nor removing one changes the guest's memory
 * map. that change, a memory slot made or deleted, waits for every
 * processor to leave the guest and took a millisecond or two each time.
 *
 * a program of the host can shorten a file that has a window. the pages past the new end are gone from the
 * page cache, and what touches one gets a bus error: the kernel stalls a processor of the guest on it without
 * leaving KVM_RUN, and a thread of ours would be killed. the file is watched with inotify, and once it is shorter
 * the guest is told which pages are gone and takes its mappings of them away, so that a program touching one gets
 * the in page error of windows. only then are the pages replaced by zeros, which a processor stalled on one then
 * reads, or at once after a moment when the guest does not answer. a bus error that reaches a thread of ours is
 * answered the same way, by replacing the page in the signal handler.
 */
#include "../vmm/vmm.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

bool mime_kvm_prefault(MimeVm *vm, uint64_t gpa, uint64_t size);
uint32_t mime_kvm_alloc_slot(MimeVm *vm);
bool mime_kvm_set_slot(MimeVm *vm, uint32_t slot, uint64_t ipa, uint64_t size, void *host, bool readonly);

/* the range gets its memory slots in steps of this size, as windows are placed that far. */
#define WINDOW_CHUNK (2ull << 30)

/* ------------------------------------------------------------------ files the host shortens */

#define LOST_QUEUE 64
#define LOST_WAIT_MS 100 /* how long the guest has to take its mappings away before the pages are replaced */
#define BUS_RING 256
#define WINDOW_PAGE ((uint64_t)MIME_PAGE_SIZE)

static struct {
    pthread_mutex_t lock;
    pthread_cond_t acked;
    struct {
        uint64_t serial, ipa, size, id;
    } queue[LOST_QUEUE];
    uint32_t head, tail;
    uint64_t next_serial, acked_serial;
    MimeVm *vm;
    int inotify, wake;
    bool started;
    uint8_t *base; /* the window range in this process, for the signal handler */
    uint32_t bus_tail;
    uint64_t bus_page[BUS_RING]; /* offsets of replaced pages in the range, plus one */
    uint32_t bus_head;
} g_lost = {.lock = PTHREAD_MUTEX_INITIALIZER, .acked = PTHREAD_COND_INITIALIZER, .inotify = -1, .wake = -1};

#define WINDOW_LOCK_OF(vm) (&(vm)->window_lock)

/* replaces the pages of a window that are gone by zeros that can be written, if it is still the window they were told for. */
static void give_up(MimeVm *vm, uint64_t id, uint64_t ipa, uint64_t size)
{
    pthread_mutex_lock(WINDOW_LOCK_OF(vm));
    for (uint32_t i = 0; i < vm->window_count; i++) {
        MimeFileWindow *w = &vm->windows[i];
        if (w->id == id && ipa >= w->ipa && ipa + size <= w->ipa + w->size) {
            mmap(vm->window_host + (ipa - MIME_WINDOW_BASE), size, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
            break;
        }
    }
    pthread_mutex_unlock(WINDOW_LOCK_OF(vm));
}

/* tells the guest, and waits for it to answer or for the time to run out, then gives the pages up. */
static void publish_lost(MimeVm *vm, uint64_t id, uint64_t ipa, uint64_t size)
{
    pthread_mutex_lock(&g_lost.lock);
    if (g_lost.tail - g_lost.head == LOST_QUEUE)
        g_lost.head++; /* the oldest has waited long enough, it is replaced below by its own turn */
    uint64_t serial = ++g_lost.next_serial;
    uint32_t at = g_lost.tail++ % LOST_QUEUE;
    g_lost.queue[at].serial = serial;
    g_lost.queue[at].ipa = ipa;
    g_lost.queue[at].size = size;
    g_lost.queue[at].id = id;
    pthread_mutex_unlock(&g_lost.lock);
    mime_warn("a host program shortened a file the guest maps: 0x%llx bytes at window address 0x%llx are gone",
              (unsigned long long)size, (unsigned long long)ipa);
    mime_vm_raise_irq(vm, MIME_IRQ_WINDOW);
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_nsec += LOST_WAIT_MS * 1000000l;
    until.tv_sec += until.tv_nsec / 1000000000l;
    until.tv_nsec %= 1000000000l;
    pthread_mutex_lock(&g_lost.lock);
    while (g_lost.acked_serial < serial && pthread_cond_timedwait(&g_lost.acked, &g_lost.lock, &until) == 0)
        ;
    pthread_mutex_unlock(&g_lost.lock);
    give_up(vm, id, ipa, size);
}

/* a window is taken away: what was waiting to be told about it is not told, as its addresses go to another file. */
static void cancel_lost(uint64_t id)
{
    pthread_mutex_lock(&g_lost.lock);
    for (uint32_t i = g_lost.head; i != g_lost.tail; i++)
        if (g_lost.queue[i % LOST_QUEUE].id == id)
            g_lost.queue[i % LOST_QUEUE].size = 0;
    pthread_mutex_unlock(&g_lost.lock);
}

/* the guest asks for the next range and says which one it dealt with. */
uint64_t mime_window_lost(MimeVm *vm, uint64_t pa)
{
    MimeWindowLost *io = mime_vm_phys(vm, pa, sizeof(*io));
    if (!io)
        return MIME_HC_STATUS_INVALID;
    pthread_mutex_lock(&g_lost.lock);
    if (io->acked > g_lost.acked_serial) {
        g_lost.acked_serial = io->acked;
        pthread_cond_broadcast(&g_lost.acked);
    }
    while (g_lost.head != g_lost.tail && (g_lost.queue[g_lost.head % LOST_QUEUE].serial <= g_lost.acked_serial ||
                                          !g_lost.queue[g_lost.head % LOST_QUEUE].size))
        g_lost.head++;
    io->serial = io->ipa = io->size = 0;
    if (g_lost.head != g_lost.tail) {
        io->serial = g_lost.queue[g_lost.head % LOST_QUEUE].serial;
        io->ipa = g_lost.queue[g_lost.head % LOST_QUEUE].ipa;
        io->size = g_lost.queue[g_lost.head % LOST_QUEUE].size;
    }
    pthread_mutex_unlock(&g_lost.lock);
    return MIME_HC_STATUS_OK;
}

/* the windows of the files an event names, or of every file when it names none, checked against their sizes. */
static void check_windows(MimeVm *vm, const int *watches, int count)
{
    for (;;) {
        uint64_t ipa = 0, size = 0, id = 0;
        pthread_mutex_lock(WINDOW_LOCK_OF(vm));
        for (uint32_t i = 0; i < vm->window_count && !size; i++) {
            MimeFileWindow *w = &vm->windows[i];
            bool wanted = count == 0;
            for (int j = 0; j < count && !wanted; j++)
                wanted = watches[j] == w->watch;
            struct stat st;
            if (!wanted || w->fd < 0 || fstat(w->fd, &st) != 0)
                continue;
            uint64_t end = MIME_ALIGN_UP((uint64_t)st.st_size, WINDOW_PAGE);
            if (end < w->lost_from) {
                ipa = w->ipa + end;
                size = w->lost_from - end;
                id = w->id;
                w->lost_from = end;
            }
        }
        pthread_mutex_unlock(WINDOW_LOCK_OF(vm));
        if (!size)
            return;
        publish_lost(vm, id, ipa, size);
    }
}

static void *lost_main(void *arg)
{
    MimeVm *vm = arg;
    mime_set_thread_name("mime-fs-lost");
    struct pollfd fds[2] = {{.fd = g_lost.inotify, .events = POLLIN}, {.fd = g_lost.wake, .events = POLLIN}};
    for (;;) {
        if (poll(fds, 2, -1) < 0 && errno != EINTR)
            continue;
        if (fds[1].revents & POLLIN) {
            uint64_t count;
            if (read(g_lost.wake, &count, sizeof(count)) < 0)
                continue;
            for (;;) {
                uint32_t head = g_lost.bus_head;
                if (head == __atomic_load_n(&g_lost.bus_tail, __ATOMIC_ACQUIRE))
                    break;
                uint64_t page = __atomic_load_n(&g_lost.bus_page[head % BUS_RING], __ATOMIC_ACQUIRE);
                if (!page)
                    break;
                __atomic_store_n(&g_lost.bus_page[head % BUS_RING], 0, __ATOMIC_RELEASE);
                g_lost.bus_head++;
                uint64_t ipa = MIME_WINDOW_BASE + (page - 1), id = 0;
                pthread_mutex_lock(WINDOW_LOCK_OF(vm));
                for (uint32_t i = 0; i < vm->window_count; i++)
                    if (ipa >= vm->windows[i].ipa && ipa < vm->windows[i].ipa + vm->windows[i].size)
                        id = vm->windows[i].id;
                pthread_mutex_unlock(WINDOW_LOCK_OF(vm));
                /* a page of a window that is gone already is nobody's to tell. */
                if (id)
                    publish_lost(vm, id, ipa, WINDOW_PAGE);
            }
        }
        if (fds[0].revents & POLLIN) {
            char buffer[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
            ssize_t n = read(g_lost.inotify, buffer, sizeof(buffer));
            int watches[32], count = 0;
            bool everything = false;
            for (ssize_t at = 0; at < n;) {
                const struct inotify_event *e = (const struct inotify_event *)(buffer + at);
                at += (ssize_t)(sizeof(*e) + e->len);
                if (e->wd < 0) {
                    everything = true;
                    continue;
                }
                bool known = false;
                for (int j = 0; j < count; j++)
                    known |= watches[j] == e->wd;
                if (known)
                    continue;
                if (count == 32)
                    everything = true;
                else
                    watches[count++] = e->wd;
            }
            if (n > 0)
                check_windows(vm, watches, everything ? 0 : count);
        }
    }
    return nullptr;
}

static void start_watching(MimeVm *vm)
{
    if (g_lost.started)
        return;
    g_lost.vm = vm;
    g_lost.inotify = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    g_lost.wake = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (g_lost.inotify < 0 || g_lost.wake < 0) {
        mime_warn("cannot watch the files of windows: %s", strerror(errno));
        return;
    }
    pthread_t thread;
    if (pthread_create(&thread, nullptr, lost_main, vm) != 0)
        return;
    pthread_detach(thread);
    g_lost.started = true;
}

/*
 * a bus error on a window, in the signal handler of the thread that touched it: the page is replaced by one of zeros
 * and the thread goes on. the guest is told by the thread that watches, once it has been woken.
 */
bool mime_window_bus_fault(void *address)
{
    uint8_t *base = g_lost.base;
    if (!base || (uint8_t *)address < base || (uint8_t *)address >= base + MIME_WINDOW_SIZE)
        return false;
    uint8_t *page = (uint8_t *)((uintptr_t)address & ~(uintptr_t)(WINDOW_PAGE - 1));
    if (mmap(page, WINDOW_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0) ==
        MAP_FAILED)
        return false;
    if (g_lost.wake >= 0) {
        uint32_t at = __atomic_fetch_add(&g_lost.bus_tail, 1, __ATOMIC_ACQ_REL);
        __atomic_store_n(&g_lost.bus_page[at % BUS_RING], (uint64_t)(page - base) + 1, __ATOMIC_RELEASE);
        uint64_t one = 1;
        if (write(g_lost.wake, &one, sizeof(one)) < 0)
            return true;
    }
    return true;
}

static bool reserve(MimeVm *vm)
{
    if (vm->window_host)
        return true;
    void *host = mmap(nullptr, MIME_WINDOW_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (host == MAP_FAILED)
        return false;
    vm->window_host = host;
    __atomic_store_n(&g_lost.base, host, __ATOMIC_RELEASE);
    return true;
}

/* gives the range up to `end` bytes from its start a memory slot each time a chunk is passed. */
static bool register_to(MimeVm *vm, uint64_t end)
{
    while (vm->window_registered < end) {
        uint64_t at = vm->window_registered;
        /* the slots can be written: what the host maps over a window decides, and the guest's tables keep it from the rest. */
        if (!mime_kvm_set_slot(vm, mime_kvm_alloc_slot(vm), MIME_WINDOW_BASE + at, WINDOW_CHUNK, vm->window_host + at, false))
            return false;
        vm->window_registered += WINDOW_CHUNK;
    }
    return true;
}

static void clear(MimeVm *vm, uint64_t ipa, uint64_t size)
{
    mmap(vm->window_host + (ipa - MIME_WINDOW_BASE), size, PROT_NONE,
         MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED, -1, 0);
}

/* places a file at the first free window address, first fit. */
uint64_t mime_window_map(MimeVm *vm, int file, bool writable, uint64_t *ipa_out)
{
    struct stat st;
    if (fstat(file, &st) != 0 || st.st_size <= 0)
        return MIME_HC_STATUS_INVALID;
    if (vm->window_count == MIME_VMM_MAX_WINDOWS || !reserve(vm))
        return MIME_HC_STATUS_NO_MEMORY;
    /* whole pages: the bytes of the last one past the end of the file read as zeros. */
    uint64_t size = MIME_ALIGN_UP((uint64_t)st.st_size, (uint64_t)MIME_PAGE_SIZE);
    uint64_t ipa = MIME_WINDOW_BASE;
    uint32_t at = 0;
    for (; at < vm->window_count; at++) {
        if (ipa + size <= vm->windows[at].ipa)
            break;
        ipa = vm->windows[at].ipa + MIME_ALIGN_UP(vm->windows[at].size, (uint64_t)MIME_WINDOW_ALIGN);
    }
    if (ipa + size > MIME_WINDOW_BASE + MIME_WINDOW_SIZE)
        return MIME_HC_STATUS_NO_MEMORY;
    uint64_t end = MIME_ALIGN_UP(ipa + size - MIME_WINDOW_BASE, WINDOW_CHUNK);
    if (!register_to(vm, end))
        return MIME_HC_STATUS_NO_MEMORY;
    void *host = mmap(vm->window_host + (ipa - MIME_WINDOW_BASE), size, writable ? PROT_READ | PROT_WRITE : PROT_READ,
                      MAP_SHARED | MAP_FIXED, file, 0);
    if (host == MAP_FAILED)
        return errno == ENOMEM ? MIME_HC_STATUS_NO_MEMORY : errno == EACCES ? MIME_HC_STATUS_ACCESS_DENIED : MIME_HC_STATUS_IO_ERROR;
    memmove(&vm->windows[at + 1], &vm->windows[at], (vm->window_count - at) * sizeof(MimeFileWindow));
    static uint64_t next_id;
    vm->windows[at] = (MimeFileWindow){ipa, size, ++next_id, -1, -1, size};
    start_watching(vm);
    if (g_lost.started) {
        /* the pages of a file that is shortened must not be touched unannounced, so it is watched from now on. */
        int fd = dup(file);
        if (fd >= 0) {
            char name[64];
            snprintf(name, sizeof(name), "/proc/self/fd/%d", fd);
            int watch = inotify_add_watch(g_lost.inotify, name, IN_MODIFY);
            vm->windows[at].fd = fd;
            vm->windows[at].watch = watch;
        }
    }
    vm->window_count++;
    *ipa_out = ipa;
    return MIME_HC_STATUS_OK;
}

static void unmap_at(MimeVm *vm, uint32_t at)
{
    MimeFileWindow *w = &vm->windows[at];
    cancel_lost(w->id);
    clear(vm, w->ipa, w->size);
    if (w->fd >= 0) {
        bool shared = false;
        for (uint32_t i = 0; i < vm->window_count; i++)
            shared |= i != at && vm->windows[i].watch == w->watch;
        if (!shared && w->watch >= 0)
            inotify_rm_watch(g_lost.inotify, w->watch);
        close(w->fd);
    }
    memmove(w, w + 1, (vm->window_count - at - 1) * sizeof(MimeFileWindow));
    vm->window_count--;
}

uint64_t mime_window_unmap(MimeVm *vm, uint64_t ipa)
{
    for (uint32_t i = 0; i < vm->window_count; i++) {
        if (vm->windows[i].ipa == ipa) {
            unmap_at(vm, i);
            return MIME_HC_STATUS_OK;
        }
    }
    return MIME_HC_STATUS_INVALID;
}

void mime_window_unmap_all(MimeVm *vm)
{
    while (vm->window_count)
        unmap_at(vm, vm->window_count - 1);
}

/*
 * a window's pages are mapped into the guest's tables by kvm as the guest first reads them, an exit each. The
 * windows asked to be warm are mapped by a thread meanwhile, in the order they come.
 */
#define WARM_QUEUE 64
static struct {
    MimeVm *vm;
    uint64_t ipa, size;
} g_warm[WARM_QUEUE];
static uint32_t g_warm_head, g_warm_tail;
static pthread_mutex_t g_warm_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_warm_cond = PTHREAD_COND_INITIALIZER;
static bool g_warm_thread;

static void *warm_main(void *arg)
{
    mime_set_thread_name("mime-fs-warm");
    pthread_mutex_lock(&g_warm_lock);
    for (;;) {
        while (g_warm_head == g_warm_tail)
            pthread_cond_wait(&g_warm_cond, &g_warm_lock);
        uint32_t at = g_warm_head++ % WARM_QUEUE;
        MimeVm *vm = g_warm[at].vm;
        uint64_t ipa = g_warm[at].ipa, size = g_warm[at].size;
        pthread_mutex_unlock(&g_warm_lock);
        mime_kvm_prefault(vm, ipa, size);
        pthread_mutex_lock(&g_warm_lock);
    }
    return nullptr;
}

/* asks for the window at this address to be warm; one that does not fit the queue is left to be read as it comes. */
void mime_window_warm(MimeVm *vm, uint64_t ipa)
{
    uint64_t size = 0;
    for (uint32_t i = 0; i < vm->window_count; i++)
        if (vm->windows[i].ipa == ipa)
            size = vm->windows[i].size;
    if (!size)
        return;
    pthread_mutex_lock(&g_warm_lock);
    if (g_warm_tail - g_warm_head < WARM_QUEUE) {
        g_warm[g_warm_tail++ % WARM_QUEUE] = (typeof(g_warm[0])){vm, ipa, size};
        if (!g_warm_thread) {
            pthread_t thread;
            g_warm_thread = pthread_create(&thread, nullptr, warm_main, nullptr) == 0;
            if (g_warm_thread)
                pthread_detach(thread);
        }
        pthread_cond_signal(&g_warm_cond);
    }
    pthread_mutex_unlock(&g_warm_lock);
}

/* kvm maps every window as it is created, so a fault there is never a lazy fill. */
bool mime_fs_lazy_fault(MimeVm *vm, uint64_t ipa)
{
    return false;
}
