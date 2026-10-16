/*
 * host files placed read only in the guest by MIME_HC_FS_MAP. each window
 * is a read only memslot over a shared mapping of the file, so the guest
 * reads the host's page cache: a file is cached once, by the host, and
 * only the pages the guest touches are ever read from disk.
 */
#include "../vmm/vmm.h"

#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>

uint32_t mime_kvm_alloc_slot(MimeVm *vm);
void mime_kvm_free_slot(MimeVm *vm, uint32_t slot);
bool mime_kvm_set_slot(MimeVm *vm, uint32_t slot, uint64_t ipa, uint64_t size, void *host, bool readonly);

/* places a file read only at the first free window address, first fit. */
uint64_t mime_window_map(MimeVm *vm, int file, uint64_t *ipa_out)
{
    struct stat st;
    if (fstat(file, &st) != 0 || st.st_size <= 0)
        return MIME_HC_STATUS_INVALID;
    if (vm->window_count == MIME_VMM_MAX_WINDOWS)
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
    void *host = mmap(nullptr, size, PROT_READ, MAP_SHARED, file, 0);
    if (host == MAP_FAILED)
        return errno == ENOMEM ? MIME_HC_STATUS_NO_MEMORY : MIME_HC_STATUS_IO_ERROR;
    uint32_t slot = mime_kvm_alloc_slot(vm);
    if (!mime_kvm_set_slot(vm, slot, ipa, size, host, true)) {
        mime_kvm_free_slot(vm, slot);
        munmap(host, size);
        return MIME_HC_STATUS_NO_MEMORY;
    }
    memmove(&vm->windows[at + 1], &vm->windows[at], (vm->window_count - at) * sizeof(MimeFileWindow));
    vm->windows[at] = (MimeFileWindow){ipa, size, host, slot};
    vm->window_count++;
    *ipa_out = ipa;
    return MIME_HC_STATUS_OK;
}

static void unmap_at(MimeVm *vm, uint32_t at)
{
    MimeFileWindow *w = &vm->windows[at];
    mime_kvm_set_slot(vm, w->slot, w->ipa, 0, nullptr, true);
    mime_kvm_free_slot(vm, w->slot);
    munmap(w->host, w->size);
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
