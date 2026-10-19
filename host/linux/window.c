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
 */
#include "../vmm/vmm.h"

#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

bool mime_kvm_prefault(MimeVm *vm, uint64_t gpa, uint64_t size);
uint32_t mime_kvm_alloc_slot(MimeVm *vm);
bool mime_kvm_set_slot(MimeVm *vm, uint32_t slot, uint64_t ipa, uint64_t size, void *host, bool readonly);

/* the range gets its memory slots in steps of this size, as windows are placed that far. */
#define WINDOW_CHUNK (2ull << 30)

static bool reserve(MimeVm *vm)
{
    if (vm->window_host)
        return true;
    void *host = mmap(nullptr, MIME_WINDOW_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (host == MAP_FAILED)
        return false;
    vm->window_host = host;
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
    vm->windows[at] = (MimeFileWindow){ipa, size};
    vm->window_count++;
    *ipa_out = ipa;
    return MIME_HC_STATUS_OK;
}

static void unmap_at(MimeVm *vm, uint32_t at)
{
    MimeFileWindow *w = &vm->windows[at];
    clear(vm, w->ipa, w->size);
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
