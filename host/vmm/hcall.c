#include "vmm.h"

#include "../ui/ui.h"

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
    case MIME_HC_FS_OPEN:
    case MIME_HC_FS_CLOSE:
    case MIME_HC_FS_READ:
    case MIME_HC_FS_WRITE:
    case MIME_HC_FS_QUERY:
    case MIME_HC_FS_READDIR:
    case MIME_HC_FS_SET_SIZE:
    case MIME_HC_FS_DELETE:
        status = mime_fs_hcall(vm, nr, regs);
        value = regs[1];
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
    case MIME_HC_WIN_CURSOR:
        mime_ui_set_cursor((uint32_t)regs[1]);
        break;
    case MIME_HC_BEEP:
        mime_ui_beep((uint32_t)regs[1]);
        break;
    default:
        mime_warn("unknown hypercall %#llx", (unsigned long long)nr);
        status = MIME_HC_STATUS_UNSUPPORTED;
        break;
    }
    regs[0] = status;
    regs[1] = value;
}
