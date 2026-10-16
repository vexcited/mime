/*
 * host files placed read only in the guest by MIME_HC_FS_MAP. macos fills
 * each chunk on its first stage 2 fault from the file (lazy.c), keeping
 * at most a budget of filled bytes.
 */
#include "vmm.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define WINDOW_BUDGET (64ull << 20) /* filled window bytes kept before older windows are emptied */

/* places a file read only at the first free window address, first fit. nothing is read yet. */
uint64_t mime_window_map(MimeVm *vm, int file, uint64_t *ipa_out)
{
    struct stat st;
    if (fstat(file, &st) != 0 || st.st_size <= 0)
        return MIME_HC_STATUS_INVALID;
    if (vm->window_count == MIME_VMM_MAX_WINDOWS)
        return MIME_HC_STATUS_NO_MEMORY;
    uint64_t size = MIME_ALIGN_UP((uint64_t)st.st_size, (uint64_t)MIME_WINDOW_ALIGN);
    uint64_t ipa = MIME_WINDOW_BASE;
    uint32_t at = 0;
    for (; at < vm->window_count; at++) {
        if (ipa + size <= vm->windows[at].ipa)
            break;
        ipa = vm->windows[at].ipa + MIME_ALIGN_UP(vm->windows[at].size, (uint64_t)MIME_WINDOW_ALIGN);
    }
    if (ipa + size > MIME_WINDOW_BASE + MIME_WINDOW_SIZE)
        return MIME_HC_STATUS_NO_MEMORY;
    int fd = dup(file);
    if (fd < 0)
        return MIME_HC_STATUS_NO_MEMORY;
    MimeLazyRegion r;
    if (!mime_lazy_init(&r, ipa, size, fd, nullptr, (uint64_t)st.st_size)) {
        close(fd);
        return MIME_HC_STATUS_NO_MEMORY;
    }
    memmove(&vm->windows[at + 1], &vm->windows[at], (vm->window_count - at) * sizeof(MimeLazyRegion));
    vm->windows[at] = r;
    vm->window_count++;
    *ipa_out = ipa;
    return MIME_HC_STATUS_OK;
}

static MimeLazyRegion *window_find(MimeVm *vm, uint64_t ipa)
{
    for (uint32_t i = 0; i < vm->window_count; i++)
        if (mime_lazy_contains(&vm->windows[i], ipa))
            return &vm->windows[i];
    return nullptr;
}

static void window_unmap(MimeVm *vm, MimeLazyRegion *w)
{
    vm->window_bytes -= w->filled_bytes;
    mime_lazy_free(w);
    uint32_t at = (uint32_t)(w - vm->windows);
    memmove(w, w + 1, (vm->window_count - at - 1) * sizeof(MimeLazyRegion));
    vm->window_count--;
}

uint64_t mime_window_unmap(MimeVm *vm, uint64_t ipa)
{
    MimeLazyRegion *w = window_find(vm, ipa);
    if (!w || w->ipa != ipa)
        return MIME_HC_STATUS_INVALID;
    window_unmap(vm, w);
    return MIME_HC_STATUS_OK;
}

void mime_window_unmap_all(MimeVm *vm)
{
    while (vm->window_count)
        window_unmap(vm, &vm->windows[0]);
}

/* empties the least recently filled windows until a chunk fits the budget. */
static void window_trim(MimeVm *vm, MimeLazyRegion *keep)
{
    while (vm->window_bytes + MIME_LAZY_CHUNK > WINDOW_BUDGET) {
        MimeLazyRegion *oldest = nullptr;
        for (uint32_t i = 0; i < vm->window_count; i++) {
            MimeLazyRegion *w = &vm->windows[i];
            if (w != keep && w->filled_bytes && (!oldest || w->last_use < oldest->last_use))
                oldest = w;
        }
        if (!oldest)
            return;
        vm->window_bytes -= oldest->filled_bytes;
        mime_lazy_drop(oldest);
    }
}

/* a stage 2 fault in the rom or a window: fills the touched chunk. */
bool mime_fs_lazy_fault(MimeVm *vm, uint64_t ipa)
{
    pthread_mutex_lock(&vm->fs_lock);
    bool handled;
    MimeLazyRegion *w;
    if (mime_lazy_contains(&vm->rom, ipa)) {
        handled = mime_lazy_fill(&vm->rom, ipa);
    } else if ((w = window_find(vm, ipa))) {
        window_trim(vm, w);
        uint64_t before = w->filled_bytes;
        handled = mime_lazy_fill(w, ipa);
        vm->window_bytes += w->filled_bytes - before;
        w->last_use = ++vm->window_clock;
    } else {
        handled = false;
    }
    pthread_mutex_unlock(&vm->fs_lock);
    return handled;
}

