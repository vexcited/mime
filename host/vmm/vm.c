#include "vmm.h"

#include "../identity.h"
#include "../rom.h"

#include <sys/attr.h>
#include <unistd.h>

#include <Hypervisor/Hypervisor.h>
#include <errno.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

/* physical layout used by the loader at the start of guest ram. */
#define LAYOUT_SHARED 0x0000ull
#define LAYOUT_BOOT_INFO 0x1000ull
#define LAYOUT_BOOT_STACK 0x100000ull
#define LAYOUT_BOOT_STACK_SIZE 0x10000ull

void *mime_vm_phys(MimeVm *vm, uint64_t pa, uint64_t len)
{
    if (pa < MIME_RAM_BASE)
        return NULL;
    uint64_t off = pa - MIME_RAM_BASE;
    if (off > vm->ram_size || len > vm->ram_size - off)
        return NULL;
    return vm->ram + off;
}

static uint64_t unix_to_filetime(struct timespec ts)
{
    return ((uint64_t)ts.tv_sec + 11644473600ull) * 10000000ull + (uint64_t)ts.tv_nsec / 100;
}

/* the name finder shows for the host root volume, which becomes the label of Z:. */
static void root_volume_label(char *out, size_t size)
{
    struct attrlist attrs = {.bitmapcount = ATTR_BIT_MAP_COUNT, .volattr = ATTR_VOL_INFO | ATTR_VOL_NAME};
    struct {
        uint32_t length;
        attrreference_t name;
        char data[256];
    } __attribute__((packed)) buf;
    mime_strlcpy(out, "Macintosh HD", size);
    if (getattrlist("/", &attrs, &buf, sizeof(buf), 0) == 0)
        mime_strlcpy(out, (const char *)&buf.name + buf.name.attr_dataoffset, size);
}

/* guid text of machine.ini, such as 32C7-..., to 16 bytes in text order. */
static void parse_uuid(const char *text, uint8_t out[16])
{
    memset(out, 0, 16);
    uint32_t words[6] = {0};
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
    root_volume_label(boot->host_volume_label, sizeof(boot->host_volume_label));
}

static uint64_t align_up(uint64_t v, uint64_t a)
{
    return (v + a - 1) / a * a;
}

/*
 * the framework emulates a gicv3 in the macos kernel: the virtual timer,
 * interrupt acknowledgement and wfi then never exit to this process.
 */
static bool create_gic(MimeVm *vm)
{
    size_t dist_size, dist_align, redist_size, redist_align;
    uint32_t spi_base, spi_count, timer;
    if (hv_gic_get_distributor_size(&dist_size) != HV_SUCCESS ||
        hv_gic_get_distributor_base_alignment(&dist_align) != HV_SUCCESS ||
        hv_gic_get_redistributor_region_size(&redist_size) != HV_SUCCESS ||
        hv_gic_get_redistributor_base_alignment(&redist_align) != HV_SUCCESS ||
        hv_gic_get_spi_interrupt_range(&spi_base, &spi_count) != HV_SUCCESS || spi_count == 0 ||
        hv_gic_get_intid(HV_GIC_INT_EL1_VIRTUAL_TIMER, &timer) != HV_SUCCESS) {
        mime_error("cannot query the virtual gic parameters");
        return false;
    }
    uint64_t dist = align_up(MIME_GIC_BASE, dist_align);
    uint64_t redist = align_up(dist + dist_size, redist_align);
    if (redist + redist_size > MIME_RAM_BASE) {
        mime_error("the virtual gic does not fit below guest ram");
        return false;
    }
    hv_gic_config_t config = hv_gic_config_create();
    hv_gic_config_set_distributor_base(config, dist);
    hv_gic_config_set_redistributor_base(config, redist);
    hv_return_t r = hv_gic_create(config);
    os_release(config);
    if (r != HV_SUCCESS) {
        mime_error("hv_gic_create failed (0x%x)", r);
        return false;
    }
    vm->irq_intid = spi_base;
    vm->boot->gic_dist_phys = dist;
    vm->boot->gic_dist_size = dist_size;
    vm->boot->gic_redist_phys = redist;
    vm->boot->gic_redist_size = redist_size;
    vm->boot->irq_intid = spi_base;
    vm->boot->timer_intid = timer;
    return true;
}

bool mime_vm_create(MimeVm *vm, const MimePaths *paths, uint64_t ram_size)
{
    memset(vm, 0, sizeof(*vm));
    vm->paths = paths;
    vm->ram_size = ram_size;
    vm->vcpu_count = 1;
    pthread_mutex_init(&vm->lock, NULL);
    pthread_mutex_init(&vm->fs_lock, NULL);

    hv_vm_config_t config = hv_vm_config_create();
    hv_vm_config_set_ipa_size(config, 36);
    hv_return_t r = hv_vm_create(config);
    os_release(config);
    if (r != HV_SUCCESS) {
        mime_error("hv_vm_create failed (0x%x); is the binary signed with the hypervisor entitlement?", r);
        return false;
    }

    vm->ram = mmap(NULL, ram_size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (vm->ram == MAP_FAILED) {
        mime_error("cannot reserve %llu bytes of guest memory", (unsigned long long)ram_size);
        return false;
    }
    r = hv_vm_map(vm->ram, MIME_RAM_BASE, ram_size, HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC);
    if (r != HV_SUCCESS) {
        mime_error("hv_vm_map failed (0x%x)", r);
        return false;
    }

    /*
     * the system files, read only, copied from the executable's own pages
     * chunk by chunk as the guest touches them (lazy.c).
     */
    if (!mime_lazy_init(&vm->rom, MIME_ROM_BASE, mime_rom_size(), -1, mime_rom, mime_rom_size())) {
        mime_error("cannot reserve memory for the system files");
        return false;
    }

    vm->shared = (MimeSharedPage *)(vm->ram + LAYOUT_SHARED);
    vm->boot = (MimeBootInfo *)(vm->ram + LAYOUT_BOOT_INFO);
    MimeBootInfo *boot = vm->boot;
    boot->magic = MIME_BOOT_MAGIC;
    boot->version = MIME_ABI_VERSION;
    boot->size = sizeof(MimeBootInfo);
    boot->ram_base = MIME_RAM_BASE;
    boot->ram_size = ram_size;
    boot->shared_phys = MIME_RAM_BASE + LAYOUT_SHARED;
    boot->boot_stack_phys = MIME_RAM_BASE + LAYOUT_BOOT_STACK;
    boot->boot_stack_size = LAYOUT_BOOT_STACK_SIZE;
    boot->cpu_count = vm->vcpu_count;
    boot->rom_phys = MIME_ROM_BASE;
    boot->rom_size = mime_rom_size();
    boot->timer_frequency = 24000000;
    mime_strlcpy(boot->system_root, "C:\\Windows", sizeof(boot->system_root));
    /* kernel options such as "trace-exceptions", for debugging the guest. */
    const char *kargs = getenv("MIME_KERNEL_ARGS");
    if (kargs)
        mime_strlcpy(boot->cmdline, kargs, sizeof(boot->cmdline));

    uint64_t freq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    boot->timer_frequency = freq;

    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    boot->boot_filetime = unix_to_filetime(now);
    /* the vcpu starts with a virtual counter offset of zero, so CNTVCT == mach_absolute_time. */
    boot->boot_counter = mach_absolute_time();
    struct tm tm;
    localtime_r(&now.tv_sec, &tm);
    boot->timezone_bias = (int32_t)(-tm.tm_gmtoff / 60);
    vm->boot_stack_top = MIME_KVA_PHYSMAP + boot->boot_stack_phys + LAYOUT_BOOT_STACK_SIZE;
    fill_identity(vm);
    return create_gic(vm);
}

bool mime_vm_start(MimeVm *vm)
{
    for (uint32_t i = 0; i < vm->vcpu_count; i++) {
        vm->vcpus[i].vm = vm;
        vm->vcpus[i].index = i;
        if (!mime_vcpu_start(&vm->vcpus[i]))
            return false;
    }
    return true;
}

void mime_vm_request_stop(MimeVm *vm, int exit_code)
{
    pthread_mutex_lock(&vm->lock);
    vm->stopping = true;
    vm->exit_code = exit_code;
    pthread_mutex_unlock(&vm->lock);
    for (uint32_t i = 0; i < vm->vcpu_count; i++)
        mime_vcpu_kick(&vm->vcpus[i]);
}

void mime_vm_destroy(MimeVm *vm)
{
    for (uint32_t i = 0; i < vm->vcpu_count; i++) {
        if (vm->vcpus[i].thread)
            pthread_join(vm->vcpus[i].thread, NULL);
    }
    mime_fs_close_all(vm);
    mime_lazy_free(&vm->rom);
    hv_vm_destroy();
    if (vm->ram && vm->ram != MAP_FAILED)
        munmap(vm->ram, vm->ram_size);
}

void mime_vm_raise_irq(MimeVm *vm, uint32_t bits)
{
    __atomic_fetch_or(&vm->shared->irq_pending, bits, __ATOMIC_SEQ_CST);
    hv_gic_set_spi(vm->irq_intid, true);
}

void mime_vm_queue_ctrl(MimeVm *vm, const void *msg, uint32_t size)
{
    MimeCtrlNode *node = mime_xmalloc(sizeof(MimeCtrlNode) + size);
    node->next = NULL;
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
    uint32_t next = (vm->input_head + 1) % MIME_VMM_INPUT_QUEUE;
    if (next == vm->input_tail) {
        /* queue full: drop the oldest event, like a hardware fifo would. */
        vm->input_tail = (vm->input_tail + 1) % MIME_VMM_INPUT_QUEUE;
    }
    vm->input[vm->input_head] = *ev;
    vm->input_head = next;
    pthread_mutex_unlock(&vm->lock);
    mime_vm_raise_irq(vm, MIME_IRQ_INPUT);
}
