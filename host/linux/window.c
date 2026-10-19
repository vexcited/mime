/*
 * host files placed read only in the guest by MIME_HC_FS_MAP. the window
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
        if (!mime_kvm_set_slot(vm, mime_kvm_alloc_slot(vm), MIME_WINDOW_BASE + at, WINDOW_CHUNK, vm->window_host + at, true))
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

/* places a file read only at the first free window address, first fit. */
uint64_t mime_window_map(MimeVm *vm, int file, uint64_t *ipa_out)
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
    void *host = mmap(vm->window_host + (ipa - MIME_WINDOW_BASE), size, PROT_READ, MAP_SHARED | MAP_FIXED, file, 0);
    if (host == MAP_FAILED)
        return errno == ENOMEM ? MIME_HC_STATUS_NO_MEMORY : MIME_HC_STATUS_IO_ERROR;
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

/* kvm maps every window as it is created, so a fault there is never a lazy fill. */
bool mime_fs_lazy_fault(MimeVm *vm, uint64_t ipa)
{
    return false;
}
