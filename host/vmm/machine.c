/*
 * the parts of the virtual machine every host shares: guest memory
 * access, the boot information and the queues the guest pulls from.
 */
#include "vmm.h"
#include "input_queue.h"

#include "../identity.h"
#include "../rom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void *mime_vm_phys(MimeVm *vm, uint64_t pa, uint64_t len)
{
    if (pa < MIME_RAM_BASE)
        return nullptr;
    uint64_t off = pa - MIME_RAM_BASE;
    if (off > vm->ram_size || len > vm->ram_size - off)
        return nullptr;
    return vm->ram + off;
}

uint64_t mime_unix_to_filetime(struct timespec ts)
{
    return ((uint64_t)ts.tv_sec + 11644473600ull) * 10000000ull + (uint64_t)ts.tv_nsec / 100;
}

/* guid text of machine.ini, such as 32C7-..., to 16 bytes in text order. */
static void parse_uuid(const char *text, uint8_t out[16])
{
    memset(out, 0, 16);
    uint32_t words[6] = {};
    if (sscanf(text, "%8x-%4x-%4x-%4x-%4x%8x", &words[0], &words[1], &words[2], &words[3], &words[4],
               &words[5]) == 6) {
        for (int i = 0; i < 4; i++)
            out[i] = (uint8_t)(words[0] >> (24 - 8 * i));
        for (int i = 0; i < 2; i++)
            out[4 + i] = (uint8_t)(words[1] >> (8 - 8 * i));
        for (int i = 0; i < 2; i++)
            out[6 + i] = (uint8_t)(words[2] >> (8 - 8 * i));
        for (int i = 0; i < 2; i++)
            out[8 + i] = (uint8_t)(words[3] >> (8 - 8 * i));
        for (int i = 0; i < 4; i++)
            out[10 + i] = (uint8_t)(words[4] >> (24 - 8 * i));
        for (int i = 0; i < 4; i++)
            out[14 + i] = (uint8_t)(words[5] >> (24 - 8 * i));
    }
}

static void fill_identity(MimeVm *vm)
{
    MimeBootInfo *boot = vm->boot;
    MimeIdentity id;
    mime_identity_load(vm->paths, &id);
    mime_strlcpy(boot->computer_name, id.computer_name, sizeof(boot->computer_name));
    mime_strlcpy(boot->user_name, id.user_name, sizeof(boot->user_name));
    memcpy(boot->user_sid, id.sid, sizeof(boot->user_sid));
    boot->volume_serial = id.volume_serial;
    boot->install_filetime = id.install_filetime;
    const MimeHardwareProfile *hw = mime_hardware_profile();
    mime_strlcpy(boot->cpu_name, hw->cpu_name, sizeof(boot->cpu_name));
    boot->cpu_mhz = hw->cpu_mhz;
    boot->cpu_midr = hw->cpu_midr;
    mime_strlcpy(boot->system_manufacturer, hw->manufacturer, sizeof(boot->system_manufacturer));
    mime_strlcpy(boot->system_product, hw->product, sizeof(boot->system_product));
    mime_strlcpy(boot->system_family, hw->family, sizeof(boot->system_family));
    mime_strlcpy(boot->bios_vendor, hw->bios_vendor, sizeof(boot->bios_vendor));
    mime_strlcpy(boot->bios_version, hw->bios_version, sizeof(boot->bios_version));
    mime_strlcpy(boot->bios_date, hw->bios_date, sizeof(boot->bios_date));
    mime_strlcpy(boot->system_serial, id.machine_serial, sizeof(boot->system_serial));
    parse_uuid(id.machine_uuid, boot->system_uuid);
    mime_host_volume_label(boot->host_volume_label, sizeof(boot->host_volume_label));
}

/* the boot information at the start of ram; the platform adds its timer and interrupt controller. */
void mime_vm_init_boot(MimeVm *vm)
{
    vm->shared = (MimeSharedPage *)(vm->ram + MIME_LAYOUT_SHARED);
    vm->boot = (MimeBootInfo *)(vm->ram + MIME_LAYOUT_BOOT_INFO);
    MimeBootInfo *boot = vm->boot;
    boot->magic = MIME_BOOT_MAGIC;
    boot->version = MIME_ABI_VERSION;
    boot->size = sizeof(MimeBootInfo);
    boot->ram_base = MIME_RAM_BASE;
    boot->ram_size = vm->ram_size;
    boot->shared_phys = MIME_RAM_BASE + MIME_LAYOUT_SHARED;
    boot->boot_stack_phys = MIME_RAM_BASE + MIME_LAYOUT_BOOT_STACK;
    boot->boot_stack_size = MIME_LAYOUT_BOOT_STACK_SIZE;
    boot->cpu_count = vm->vcpu_count;
    boot->rom_phys = MIME_ROM_BASE;
    boot->rom_size = mime_rom_size();
    mime_strlcpy(boot->system_root, "C:\\Windows", sizeof(boot->system_root));
    /* kernel options such as "trace-exceptions", for debugging the guest. */
    const char *kargs = getenv("MIME_KERNEL_ARGS");
    if (kargs)
        mime_strlcpy(boot->cmdline, kargs, sizeof(boot->cmdline));

    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    struct tm tm;
    localtime_r(&now.tv_sec, &tm);
    boot->timezone_bias = (int32_t)(-tm.tm_gmtoff / 60);
    vm->boot_stack_top = MIME_KVA_PHYSMAP + boot->boot_stack_phys + MIME_LAYOUT_BOOT_STACK_SIZE;
    fill_identity(vm);
}

void mime_vm_request_stop(MimeVm *vm, int exit_code)
{
    pthread_mutex_lock(&vm->lock);
    vm->stopping = true;
    vm->exit_code = exit_code;
    pthread_cond_broadcast(&vm->vcpu_cond);
    pthread_mutex_unlock(&vm->lock);
    for (uint32_t i = 0; i < vm->vcpu_count; i++)
        mime_vcpu_kick(&vm->vcpus[i]);
}

void mime_vm_queue_ctrl(MimeVm *vm, const void *msg, uint32_t size)
{
    MimeCtrlNode *node = mime_xmalloc(sizeof(MimeCtrlNode) + size);
    node->next = nullptr;
    node->size = size;
    memcpy(node->data, msg, size);
    pthread_mutex_lock(&vm->lock);
    if (vm->ctrl_tail)
        vm->ctrl_tail->next = node;
    else
        vm->ctrl_head = node;
    vm->ctrl_tail = node;
    pthread_mutex_unlock(&vm->lock);
    mime_vm_raise_irq(vm, MIME_IRQ_CTRL);
}

void mime_vm_queue_input(MimeVm *vm, const MimeInputEvent *ev)
{
    pthread_mutex_lock(&vm->lock);
    bool queued = mime_input_queue_push(vm->input, MIME_VMM_INPUT_QUEUE, &vm->input_head, &vm->input_tail, ev);
    pthread_mutex_unlock(&vm->lock);
    if (queued)
        mime_vm_raise_irq(vm, MIME_IRQ_INPUT);
}
