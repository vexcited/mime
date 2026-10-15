#include "vmm.h"

#include <sys/mman.h>

#include "../audio/audio.h"
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
        vm->ctrl_tail = NULL;
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
    const void *src = len ? mime_vm_phys(vm, pa, len) : NULL;
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
            g_clip_pending = NULL;
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
    uint16_t *dst = NULL;
    if (capacity) {
        dst = mime_vm_phys(vm, pa, capacity);
        if (!dst)
            return MIME_HC_STATUS_INVALID;
    }
    *total = mime_ui_clipboard_read(dst, (size_t)(offset / 2), (size_t)(capacity / 2)) * 2;
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
    case MIME_HC_FS_DELETE:
    case MIME_HC_FS_MAP:
    case MIME_HC_FS_UNMAP:
    case MIME_HC_FS_VOLUME_QUERY:
    case MIME_HC_FS_FLUSH:
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
    case MIME_HC_BEEP:
        mime_ui_beep((uint32_t)regs[1]);
        break;
    case MIME_HC_CLIP_SET:
        status = clip_set(vm, regs[1], regs[2], regs[3], regs[4]);
        break;
    case MIME_HC_CLIP_GET:
        status = clip_get(vm, regs[1], regs[2], regs[3], &value);
        break;
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
    case MIME_HC_AUDIO_OPEN:
        status = mime_audio_open(vm, regs[1], regs[2], &value);
        break;
    case MIME_HC_AUDIO_START:
        status = mime_audio_start(regs[1], regs[2]);
        break;
    case MIME_HC_AUDIO_CLOSE:
        mime_audio_close(regs[1]);
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
