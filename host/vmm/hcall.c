#include "vmm.h"

#include <sys/mman.h>

#include "../audio/audio.h"
#include "../camera/camera.h"
#include "../gpu/gpu.h"
#include "../ui/ui.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct LineBuffer {
    char data[4096];
    size_t len;
} LineBuffer;

static LineBuffer g_guest_log;

static void guest_log(const char *text, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        char c = text[i];
        if (c == '\n' || g_guest_log.len + 1 >= sizeof(g_guest_log.data)) {
            g_guest_log.data[g_guest_log.len] = 0;
            mime_info("guest: %s", g_guest_log.data);
            g_guest_log.len = 0;
            if (c == '\n')
                continue;
        }
        if (c != '\r')
            g_guest_log.data[g_guest_log.len++] = c;
    }
}

static uint64_t filetime_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ((uint64_t)ts.tv_sec + 11644473600ull) * 10000000ull + (uint64_t)ts.tv_nsec / 100;
}

static uint64_t ctrl_recv(MimeVm *vm, uint64_t pa, uint64_t capacity, uint64_t *length)
{
    *length = 0;
    pthread_mutex_lock(&vm->lock);
    MimeCtrlNode *node = vm->ctrl_head;
    if (!node) {
        pthread_mutex_unlock(&vm->lock);
        return MIME_HC_STATUS_OK;
    }
    if (node->size > capacity) {
        pthread_mutex_unlock(&vm->lock);
        *length = node->size;
        return MIME_HC_STATUS_INVALID;
    }
    void *dst = mime_vm_phys(vm, pa, node->size);
    if (!dst) {
        pthread_mutex_unlock(&vm->lock);
        return MIME_HC_STATUS_INVALID;
    }
    memcpy(dst, node->data, node->size);
    vm->ctrl_head = node->next;
    if (!vm->ctrl_head)
        vm->ctrl_tail = nullptr;
    else
        __atomic_fetch_or(&vm->shared->irq_pending, MIME_IRQ_CTRL, __ATOMIC_SEQ_CST);
    pthread_mutex_unlock(&vm->lock);
    *length = node->size;
    free(node);
    return MIME_HC_STATUS_OK;
}

static uint64_t input_recv(MimeVm *vm, uint64_t pa, uint64_t max, uint64_t *count)
{
    *count = 0;
    if (max == 0)
        return MIME_HC_STATUS_OK;
    MimeInputEvent *dst = mime_vm_phys(vm, pa, max * sizeof(MimeInputEvent));
    if (!dst)
        return MIME_HC_STATUS_INVALID;
    pthread_mutex_lock(&vm->lock);
    while (*count < max && vm->input_tail != vm->input_head) {
        dst[*count] = vm->input[vm->input_tail];
        vm->input_tail = (vm->input_tail + 1) % MIME_VMM_INPUT_QUEUE;
        (*count)++;
    }
    if (vm->input_tail != vm->input_head)
        __atomic_fetch_or(&vm->shared->irq_pending, MIME_IRQ_INPUT, __ATOMIC_SEQ_CST);
    pthread_mutex_unlock(&vm->lock);
    return MIME_HC_STATUS_OK;
}

#define CLIP_MAX_BYTES (64u << 20)

/* the guest sends its clipboard text in chunks; the text is complete when the last chunk lands. */
static pthread_mutex_t g_clip_lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t *g_clip_pending;
static uint64_t g_clip_total;

static uint64_t clip_set(MimeVm *vm, uint64_t pa, uint64_t len, uint64_t offset, uint64_t total)
{
    if (total > CLIP_MAX_BYTES || (total & 1) || offset > total || len > total - offset)
        return MIME_HC_STATUS_INVALID;
    const void *src = len ? mime_vm_phys(vm, pa, len) : nullptr;
    if (len && !src)
        return MIME_HC_STATUS_INVALID;
    pthread_mutex_lock(&g_clip_lock);
    if (offset == 0) {
        free(g_clip_pending);
        g_clip_pending = malloc(total ? total : 1);
        g_clip_total = total;
    }
    uint64_t status = MIME_HC_STATUS_OK;
    if (!g_clip_pending || g_clip_total != total) {
        status = g_clip_pending ? MIME_HC_STATUS_INVALID : MIME_HC_STATUS_NO_MEMORY;
    } else {
        if (len)
            memcpy(g_clip_pending + offset, src, len);
        if (offset + len == total) {
            mime_ui_clipboard_set((const uint16_t *)g_clip_pending, (size_t)(total / 2));
            free(g_clip_pending);
            g_clip_pending = nullptr;
        }
    }
    pthread_mutex_unlock(&g_clip_lock);
    return status;
}

static uint64_t clip_get(MimeVm *vm, uint64_t pa, uint64_t capacity, uint64_t offset, uint64_t *total)
{
    *total = 0;
    if ((capacity & 1) || (offset & 1) || capacity > CLIP_MAX_BYTES)
        return MIME_HC_STATUS_INVALID;
    uint16_t *dst = nullptr;
    if (capacity) {
        dst = mime_vm_phys(vm, pa, capacity);
        if (!dst)
            return MIME_HC_STATUS_INVALID;
    }
    *total = mime_ui_clipboard_read(dst, (size_t)(offset / 2), (size_t)(capacity / 2)) * 2;
    return MIME_HC_STATUS_OK;
}

#define CLIP_CHUNK_MAX (1u << 20)

static uint64_t clip_fetch(MimeVm *vm, uint64_t kind, uint64_t pa, uint64_t capacity, uint64_t offset,
                           uint64_t generation, uint64_t *total)
{
    *total = 0;
    if (capacity > CLIP_CHUNK_MAX)
        return MIME_HC_STATUS_INVALID;
    void *dst = capacity ? mime_vm_phys(vm, pa, capacity) : nullptr;
    if (capacity && !dst)
        return MIME_HC_STATUS_INVALID;
    return mime_ui_clipboard_fetch((uint32_t)kind, (uint32_t)generation, dst, capacity, offset, total);
}

static uint64_t clip_provide(MimeVm *vm, uint64_t request, uint64_t pa, uint64_t length, uint64_t offset,
                             uint64_t total)
{
    if (length > CLIP_CHUNK_MAX)
        return MIME_HC_STATUS_INVALID;
    const void *src = length ? mime_vm_phys(vm, pa, length) : nullptr;
    if (length && !src)
        return MIME_HC_STATUS_INVALID;
    return mime_ui_clipboard_provide((uint32_t)request, src, length, offset, total);
}

static int range_order(const void *a, const void *b)
{
    uint64_t x = ((const MimeReportRun *)a)->pa, y = ((const MimeReportRun *)b)->pa;
    return x < y ? -1 : x > y;
}

#define MEM_REPORT_MAX 512

/* free guest pages the host drops, runs that touch merged so each madvise covers as much as it can. */
static uint64_t mem_report(MimeVm *vm, uint64_t pa, uint64_t count, uint64_t *released)
{
    *released = 0;
    if (!count || count > MEM_REPORT_MAX)
        return MIME_HC_STATUS_INVALID;
    const MimeReportRun *list = mime_vm_phys(vm, pa, count * sizeof(MimeReportRun));
    if (!list)
        return MIME_HC_STATUS_INVALID;
    MimeReportRun runs[MEM_REPORT_MAX];
    uint64_t n = 0;
    for (uint64_t i = 0; i < count; i++) {
        uint64_t start = MIME_ALIGN_UP(list[i].pa, (uint64_t)MIME_HOST_PAGE);
        uint64_t end = MIME_ALIGN_DOWN(list[i].pa + list[i].bytes, (uint64_t)MIME_HOST_PAGE);
        if (list[i].pa < MIME_RAM_BASE || list[i].pa + list[i].bytes < list[i].pa ||
            list[i].pa + list[i].bytes > MIME_RAM_BASE + vm->ram_size)
            return MIME_HC_STATUS_INVALID;
        if (end > start)
            runs[n++] = (MimeReportRun){start, end - start};
    }
    qsort(runs, n, sizeof(runs[0]), range_order);
    for (uint64_t i = 0; i < n;) {
        uint64_t start = runs[i].pa, end = start + runs[i].bytes;
        for (i++; i < n && runs[i].pa <= end; i++)
            end = MIME_MAX(end, runs[i].pa + runs[i].bytes);
        mime_vm_discard(vm, start, end - start);
        *released += end - start;
    }
    __atomic_fetch_add(&vm->memory_released, *released, __ATOMIC_RELAXED);
    /* the guest counts what it handed back as free only through what the host could grant again. */
    mime_memory_publish(vm);
    return MIME_HC_STATUS_OK;
}

void mime_hcall_dispatch(MimeVcpu *vcpu, uint64_t regs[8])
{
    MimeVm *vm = vcpu->vm;
    uint64_t nr = regs[0];
    uint64_t status = MIME_HC_STATUS_OK;
    uint64_t value = 0;

    switch (nr) {
    case MIME_HC_LOG: {
        const char *text = mime_vm_phys(vm, regs[1], regs[2]);
        if (text)
            guest_log(text, (size_t)regs[2]);
        else
            status = MIME_HC_STATUS_INVALID;
        break;
    }
    case MIME_HC_PANIC: {
        const char *text = mime_vm_phys(vm, regs[1], regs[2]);
        if (g_guest_log.len)
            guest_log("\n", 1);
        mime_error("guest kernel panic: %.*s", text ? (int)regs[2] : 7, text ? text : "unknown");
        mime_daemon_guest_halted(vm, 2);
        mime_vm_request_stop(vm, 2);
        break;
    }
    case MIME_HC_HALT:
        mime_info("guest powered off (code %llu)", (unsigned long long)regs[1]);
        mime_daemon_guest_halted(vm, (int)regs[1]);
        mime_vm_request_stop(vm, (int)regs[1]);
        break;
    case MIME_HC_CTRL_RECV:
        status = ctrl_recv(vm, regs[1], regs[2], &value);
        break;
    case MIME_HC_CTRL_SEND: {
        const MimeCtrlHeader *msg = mime_vm_phys(vm, regs[1], regs[2]);
        if (!msg || regs[2] < sizeof(MimeCtrlHeader) || msg->size > regs[2]) {
            status = MIME_HC_STATUS_INVALID;
            break;
        }
        mime_daemon_guest_event(vm, msg);
        break;
    }
    case MIME_HC_INPUT_RECV:
        status = input_recv(vm, regs[1], regs[2], &value);
        break;
    case MIME_HC_TIME:
        value = filetime_now();
        break;
    case MIME_HC_LOCK_WAIT: {
        /* a guest processor waits for the kernel lock without taking a host core from the owner. */
        uint32_t *word = mime_vm_phys(vm, regs[1], 4);
        if (!word) {
            status = MIME_HC_STATUS_INVALID;
            break;
        }
        uint32_t v = __atomic_load_n(word, __ATOMIC_ACQUIRE);
        if ((uint16_t)v != (uint16_t)regs[2])
            mime_host_wait_word(word, v, 1000000);
        break;
    }
    case MIME_HC_LOCK_WAKE: {
        uint32_t *word = mime_vm_phys(vm, regs[1], 4);
        if (word)
            mime_host_wake_word(word);
        break;
    }
    case MIME_HC_WORD_WAIT: {
        uint32_t *word = mime_vm_phys(vm, regs[1], 4);
        if (!word) {
            status = MIME_HC_STATUS_INVALID;
            break;
        }
        if (__atomic_load_n(word, __ATOMIC_ACQUIRE) == (uint32_t)regs[2])
            mime_host_wait_word(word, (uint32_t)regs[2], 1000000);
        break;
    }
    case MIME_HC_WORD_WAKE: {
        uint32_t *word = mime_vm_phys(vm, regs[1], 4);
        if (word)
            mime_host_wake_word_one(word);
        break;
    }
    case MIME_HC_CPU_START:
        if (!mime_vcpu_start_secondary(vm, (uint32_t)regs[1], regs[2], regs[3], regs[4]))
            status = MIME_HC_STATUS_INVALID;
        break;
    case MIME_HC_FS_OPEN:
    case MIME_HC_FS_CLOSE:
    case MIME_HC_FS_READ:
    case MIME_HC_FS_WRITE:
    case MIME_HC_FS_QUERY:
    case MIME_HC_FS_READDIR:
    case MIME_HC_FS_SET_SIZE:
    case MIME_HC_FS_SET_BASIC:
    case MIME_HC_FS_DELETE:
    case MIME_HC_FS_MAP:
    case MIME_HC_FS_UNMAP:
    case MIME_HC_FS_VOLUME_QUERY:
    case MIME_HC_FS_FLUSH:
    case MIME_HC_FS_SYNC:
    case MIME_HC_FS_GET_SD:
    case MIME_HC_FS_SET_SD:
    case MIME_HC_FS_RENAME:
    case MIME_HC_FS_LINK:
    case MIME_HC_FS_LINKS:
    case MIME_HC_FS_SET_REPARSE:
    case MIME_HC_FS_GET_REPARSE:
    case MIME_HC_FS_DELETE_REPARSE:
    case MIME_HC_FS_APPEND:
    case MIME_HC_FS_WARM:
    case MIME_HC_FS_WINDOW_LOST:
    case MIME_HC_FS_WATCH:
    case MIME_HC_FS_UNWATCH:
    case MIME_HC_FS_CHANGES:
        status = mime_fs_hcall(vm, nr, regs);
        value = regs[1];
        break;
    case MIME_HC_MEM_DISCARD: {
        /* whole host pages only; the guest reads zeros until it writes them again. */
        uint64_t start = MIME_ALIGN_UP(regs[1], (uint64_t)MIME_HOST_PAGE);
        uint64_t end = MIME_ALIGN_DOWN(regs[1] + regs[2], (uint64_t)MIME_HOST_PAGE);
        if (regs[1] < MIME_RAM_BASE || regs[1] + regs[2] > MIME_RAM_BASE + vm->ram_size || regs[1] + regs[2] < regs[1])
            status = MIME_HC_STATUS_INVALID;
        else if (end > start)
            mime_vm_discard(vm, start, end - start);
        break;
    }
    case MIME_HC_MEM_REPORT:
        status = mem_report(vm, regs[1], regs[2], &value);
        break;
    case MIME_HC_MEM_GRANT:
        value = mime_memory_grant(vm, regs[1]);
        break;
    case MIME_HC_MEM_ADVISE:
        if (regs[1] < MIME_RAM_BASE || regs[1] + regs[2] > MIME_RAM_BASE + vm->ram_size || regs[1] + regs[2] < regs[1])
            status = MIME_HC_STATUS_INVALID;
        else
            mime_vm_advise(vm, regs[1], regs[2], (uint32_t)regs[3]);
        break;
    case MIME_HC_WIN_CREATE:
        mime_ui_window_create((uint32_t)regs[1]);
        break;
    case MIME_HC_WIN_DESTROY:
        mime_ui_window_destroy((uint32_t)regs[1]);
        break;
    case MIME_HC_WIN_CONFIG: {
        const MimeWinConfig *cfg = mime_vm_phys(vm, regs[1], sizeof(MimeWinConfig));
        if (cfg)
            mime_ui_window_config(cfg);
        else
            status = MIME_HC_STATUS_INVALID;
        break;
    }
    case MIME_HC_WIN_PRESENT: {
        const MimeWinPresent *p = mime_vm_phys(vm, regs[1], sizeof(MimeWinPresent));
        if (!p || p->width > 16384 || p->height > 16384 || p->stride < p->width * 4) {
            status = MIME_HC_STATUS_INVALID;
            break;
        }
        const void *pixels = mime_vm_phys(vm, p->surface_phys, (uint64_t)p->stride * p->height);
        if (!pixels) {
            status = MIME_HC_STATUS_INVALID;
            break;
        }
        mime_ui_window_present(p->window_id, pixels, p->width, p->height, p->stride, p->dirty);
        break;
    }
    case MIME_HC_WIN_READBACK: {
        MimeWinReadback *r = mime_vm_phys(vm, regs[1], sizeof(MimeWinReadback));
        if (!r || r->capacity_width > 16384 || r->capacity_height > 16384 ||
            (r->pixels_phys && r->stride < r->capacity_width * 4)) {
            status = MIME_HC_STATUS_INVALID;
            break;
        }
        void *pixels = r->pixels_phys ? mime_vm_phys(vm, r->pixels_phys, (uint64_t)r->stride * r->capacity_height)
                                      : nullptr;
        if (r->pixels_phys && !pixels) {
            status = MIME_HC_STATUS_INVALID;
            break;
        }
        if (!mime_ui_window_readback(r, pixels))
            status = MIME_HC_STATUS_NOT_FOUND;
        break;
    }
    case MIME_HC_WIN_LAYER: {
        const MimeWinLayer *layer = mime_vm_phys(vm, regs[1], sizeof(MimeWinLayer));
        if (!layer) {
            status = MIME_HC_STATUS_INVALID;
            break;
        }
        mime_vk_layer(layer);
        break;
    }
    case MIME_HC_WIN_ICON: {
        const MimeWinIcon *icon = mime_vm_phys(vm, regs[1], sizeof(MimeWinIcon));
        if (!icon || icon->width > 1024 || icon->height > 1024 || icon->stride < icon->width * 4 ||
            icon->stride > 4096) {
            status = MIME_HC_STATUS_INVALID;
            break;
        }
        const void *pixels = mime_vm_phys(vm, icon->pixels_phys, (uint64_t)icon->stride * icon->height);
        if (icon->width && icon->height && !pixels) {
            status = MIME_HC_STATUS_INVALID;
            break;
        }
        mime_ui_window_icon(icon->window_id, pixels, icon->width, icon->height, icon->stride);
        break;
    }
    case MIME_HC_WIN_CURSOR:
        mime_ui_set_cursor((uint32_t)regs[1]);
        break;
    case MIME_HC_PAD_RUMBLE:
        mime_ui_pad_rumble((uint32_t)regs[1], (uint16_t)regs[2], (uint16_t)regs[3]);
        break;
    case MIME_HC_GAMMA: {
        const uint16_t *ramp = regs[1] ? mime_vm_phys(vm, regs[1], 3 * 256 * sizeof(uint16_t)) : nullptr;
        if (regs[1] && !ramp) {
            status = MIME_HC_STATUS_INVALID;
            break;
        }
        mime_ui_gamma(ramp);
        break;
    }
    case MIME_HC_POINTER: {
        MimeRect rect = {(int32_t)regs[3], (int32_t)regs[4], (int32_t)regs[5], (int32_t)regs[6]};
        mime_ui_pointer((uint32_t)regs[1], (uint32_t)regs[2], &rect);
        break;
    }
    case MIME_HC_BEEP:
        mime_ui_beep((uint32_t)regs[1]);
        break;
    case MIME_HC_CLIP_SET:
        status = clip_set(vm, regs[1], regs[2], regs[3], regs[4]);
        break;
    case MIME_HC_CLIP_GET:
        status = clip_get(vm, regs[1], regs[2], regs[3], &value);
        break;
    case MIME_HC_CLIP_OFFER:
        status = mime_ui_clipboard_offer((uint32_t)regs[1]);
        break;
    case MIME_HC_CLIP_FETCH:
        status = clip_fetch(vm, regs[1], regs[2], regs[3], regs[4], regs[5], &value);
        break;
    case MIME_HC_CLIP_PROVIDE:
        status = clip_provide(vm, regs[1], regs[2], regs[3], regs[4], regs[5]);
        break;
    case MIME_HC_DND_EFFECT:
        status = mime_ui_dnd_effect((uint32_t)regs[1], (uint32_t)regs[2]);
        break;
    case MIME_HC_DND_FETCH: {
        uint64_t capacity = regs[4];
        void *dst = capacity && capacity <= CLIP_CHUNK_MAX ? mime_vm_phys(vm, regs[3], capacity) : nullptr;
        if (capacity > CLIP_CHUNK_MAX || (capacity && !dst)) {
            status = MIME_HC_STATUS_INVALID;
            break;
        }
        status = mime_ui_dnd_fetch((uint32_t)regs[1], (uint32_t)regs[2], dst, capacity, regs[5], &value);
        break;
    }
    case MIME_HC_DND_FINISH:
        status = mime_ui_dnd_finish((uint32_t)regs[1], (uint32_t)regs[2]);
        break;
    case MIME_HC_DND_START: {
        /* the image of the drag, if the guest has one: copied by the host before the call returns. */
        const MimeDragIcon *icon = nullptr;
        uint64_t icon_bytes = regs[5];
        if (regs[4]) {
            icon = icon_bytes >= sizeof(MimeDragIcon) &&
                           icon_bytes <= sizeof(MimeDragIcon) + (uint64_t)MIME_DRAG_ICON_MAX * MIME_DRAG_ICON_MAX * 4
                       ? mime_vm_phys(vm, regs[4], icon_bytes)
                       : nullptr;
            if (!icon) {
                status = MIME_HC_STATUS_INVALID;
                break;
            }
        }
        status = mime_ui_dnd_start((uint32_t)regs[1], (uint32_t)regs[2], icon, icon ? (size_t)icon_bytes : 0, &value);
        break;
    }
    case MIME_HC_DND_STOP:
        status = mime_ui_dnd_stop((uint32_t)regs[1]);
        break;
    case MIME_HC_IME_TEXT: {
        uint16_t *dst = regs[2] && regs[2] <= CLIP_CHUNK_MAX ? mime_vm_phys(vm, regs[1], regs[2]) : nullptr;
        if (regs[2] && !dst) {
            status = MIME_HC_STATUS_INVALID;
            break;
        }
        status = mime_ui_ime_text(dst, (size_t)regs[2] / 2, &value);
        break;
    }
    case MIME_HC_IME_AREA: {
        MimeRect area = {(int32_t)regs[2], (int32_t)regs[3], (int32_t)regs[4], (int32_t)regs[5]};
        status = mime_ui_ime_area((uint32_t)regs[1], &area, (int32_t)regs[6]);
        break;
    }
    case MIME_HC_IME_CONTROL:
        status = mime_ui_ime_control((uint32_t)regs[1], (uint32_t)regs[2], &value);
        break;
    case MIME_HC_TRAY: {
        const MimeTrayCommand *command = regs[2] >= sizeof(MimeTrayCommand) ? mime_vm_phys(vm, regs[1], sizeof(*command)) : nullptr;
        status = command ? mime_ui_tray(command) : MIME_HC_STATUS_INVALID;
        break;
    }
    case MIME_HC_SHELL: {
        void *command = regs[2] >= sizeof(uint32_t) && regs[2] <= sizeof(MimeShellJumpList)
                            ? mime_vm_phys(vm, regs[1], regs[2])
                            : nullptr;
        status = command ? mime_ui_shell(command, (size_t)regs[2], &value) : MIME_HC_STATUS_INVALID;
        break;
    }
    case MIME_HC_WIN_MOVESIZE:
        status = mime_ui_window_movesize((uint32_t)regs[1], (uint32_t)regs[2]);
        break;
    case MIME_HC_MONITORS: {
        uint64_t capacity = regs[2] < MIME_MAX_MONITORS ? regs[2] : MIME_MAX_MONITORS;
        MimeMonitor *dst = capacity ? mime_vm_phys(vm, regs[1], capacity * sizeof(MimeMonitor)) : nullptr;
        if (!dst) {
            status = MIME_HC_STATUS_INVALID;
            break;
        }
        value = mime_ui_get_monitors(dst, (uint32_t)capacity);
        /* MIME_DISPLAY_DPI sets the dpi of the display of the boot information, so the primary monitor has it too. */
        const char *text = getenv("MIME_DISPLAY_DPI");
        long dpi = text ? strtol(text, nullptr, 10) : 0;
        if (dpi >= 96 && dpi <= 480)
            for (uint64_t i = 0; i < value && i < capacity; i++)
                if (dst[i].flags & MIME_MONITOR_PRIMARY)
                    dst[i].dpi = (uint32_t)dpi;
        break;
    }
    case MIME_HC_GPU_OPEN:
        status = mime_gpu_open(vm, regs[1], regs[2], &value);
        break;
    case MIME_HC_GPU_BLOCK:
        status = mime_gpu_add_block(vm, regs[1], regs[2], regs[3], regs[4]);
        break;
    case MIME_HC_GPU_KICK:
        mime_gpu_kick(regs[1]);
        break;
    case MIME_HC_GPU_CLOSE:
        mime_gpu_close(regs[1]);
        break;
    case MIME_HC_VK_OPEN:
        status = mime_vk_open(vm, regs[1], regs[2], &value);
        break;
    case MIME_HC_VK_KICK:
        mime_vk_kick(regs[1]);
        break;
    case MIME_HC_VK_CLOSE:
        mime_vk_close(regs[1]);
        break;
    case MIME_HC_GFX_SHARE_FREE:
        mime_vk_share_free(regs[1]);
        break;
    case MIME_HC_AUDIO_OPEN:
        status = mime_audio_open(vm, regs[1], regs[2], &value);
        break;
    case MIME_HC_AUDIO_START:
        status = mime_audio_start(regs[1], regs[2]);
        break;
    case MIME_HC_AUDIO_CLOSE:
        mime_audio_close(regs[1]);
        break;
    case MIME_HC_CAMERA_LIST:
        status = mime_camera_list(vm, regs[1], regs[2], &value);
        break;
    case MIME_HC_CAMERA_FORMATS:
        status = mime_camera_formats(vm, regs[1], regs[2], regs[3], &value);
        break;
    case MIME_HC_CAMERA_OPEN:
        status = mime_camera_open(vm, regs[1], regs[2], regs[3], &value);
        break;
    case MIME_HC_CAMERA_START:
        status = mime_camera_start(regs[1], regs[2]);
        break;
    case MIME_HC_CAMERA_CLOSE:
        mime_camera_close(regs[1]);
        break;
    case MIME_HC_CAMERA_CONTROL:
        status = mime_camera_control(vm, regs[1]);
        break;
    case MIME_HC_NET_SOCKET:
    case MIME_HC_NET_CLOSE:
    case MIME_HC_NET_BIND:
    case MIME_HC_NET_LISTEN:
    case MIME_HC_NET_CONNECT:
    case MIME_HC_NET_ACCEPT:
    case MIME_HC_NET_SEND:
    case MIME_HC_NET_RECV:
    case MIME_HC_NET_SHUTDOWN:
    case MIME_HC_NET_NAME:
    case MIME_HC_NET_OPTION:
    case MIME_HC_NET_POLL:
    case MIME_HC_NET_EVENTS:
    case MIME_HC_NET_RESOLVE:
    case MIME_HC_NET_ANSWER:
    case MIME_HC_NET_INTERFACES:
    case MIME_HC_NET_CONFIG:
        status = mime_net_hcall(vm, nr, regs);
        value = regs[1];
        break;
    default:
        mime_warn("unknown hypercall %#llx", (unsigned long long)nr);
        status = MIME_HC_STATUS_UNSUPPORTED;
        break;
    }
    regs[0] = status;
    regs[1] = value;
}
