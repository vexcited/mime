#include "vmm.h"
#include "input_queue.h"

#include "../identity.h"
#include "../rom.h"

#include <sys/attr.h>
#include <unistd.h>

#include <Hypervisor/Hypervisor.h>
#include <errno.h>
#include <os/os_sync_wait_on_address.h>
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <time.h>

/*
 * gives free guest ram back to macOS. madvise alone does nothing to pages
 * the guest touched through stage 2, so the range leaves stage 2, gets
 * fresh anonymous memory and comes back: the old pages are freed and the
 * guest reads zeros. the guest no longer uses these pages, and no host
 * thread holds them (gpu and audio close before the guest frees).
 */
void mime_vm_discard(MimeVm *vm, uint64_t pa, uint64_t len)
{
    uint8_t *host = vm->ram + (pa - MIME_RAM_BASE);
    if (hv_vm_unmap(pa, len) != HV_SUCCESS ||
        mmap(host, len, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0) == MAP_FAILED ||
        hv_vm_map(host, pa, len, HV_MEMORY_READ | HV_MEMORY_WRITE | HV_MEMORY_EXEC) != HV_SUCCESS)
        mime_error("cannot discard guest memory at 0x%llx", (unsigned long long)pa);
}

/* the name finder shows for the host root volume, which becomes the label of Z:. */
void mime_host_volume_label(char *out, size_t size)
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

void mime_host_wait_word(uint32_t *word, uint32_t value, uint64_t timeout_ns)
{
    os_sync_wait_on_address_with_timeout(word, value, 4, OS_SYNC_WAIT_ON_ADDRESS_NONE, OS_CLOCK_MACH_ABSOLUTE_TIME,
                                         timeout_ns);
}

void mime_host_wake_word(uint32_t *word)
{
    os_sync_wake_by_address_all(word, 4, OS_SYNC_WAKE_BY_ADDRESS_NONE);
}

/* the framework backs guest ram in host pages already. */
void mime_vm_advise(MimeVm *vm, uint64_t pa, uint64_t len, uint32_t advice)
{
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

/* one vcpu per host processor; the framework and the gic may allow fewer. */
static uint32_t host_cpu_count(void)
{
    int n = 1;
    size_t len = sizeof(n);
    if (sysctlbyname("hw.logicalcpu", &n, &len, NULL, 0) != 0 || n < 1)
        n = 1;
    uint32_t max = MIME_VMM_MAX_VCPUS;
    if (hv_vm_get_max_vcpu_count(&max) == HV_SUCCESS && (uint32_t)n > max)
        n = (int)max;
    size_t region, frame;
    if (hv_gic_get_redistributor_region_size(&region) == HV_SUCCESS &&
        hv_gic_get_redistributor_size(&frame) == HV_SUCCESS && frame && (size_t)n > region / frame)
        n = (int)(region / frame);
    return (uint32_t)MIME_MIN(n, MIME_VMM_MAX_VCPUS);
}

/*
 * the guest sees as much memory as the mac has, like windows on the same
 * machine, since programs size their caches from it. it costs nothing until
 * touched, and free guest memory goes back to the host (MIME_HC_MEM_DISCARD).
 */
uint64_t mime_vm_default_ram(void)
{
    uint64_t mem = 0;
    size_t len = sizeof(mem);
    if (sysctlbyname("hw.memsize", &mem, &len, NULL, 0) != 0 || !mem)
        return MIME_VMM_FALLBACK_RAM;
    return MIME_ALIGN_DOWN(MIME_MIN(mem, MIME_VMM_MAX_RAM), 2ull << 20);
}

bool mime_vm_create(MimeVm *vm, const MimePaths *paths, uint64_t ram_size)
{
    memset(vm, 0, sizeof(*vm));
    vm->paths = paths;
    vm->ram_size = ram_size;
    vm->vcpu_count = host_cpu_count();
    pthread_mutex_init(&vm->lock, NULL);
    pthread_cond_init(&vm->vcpu_cond, NULL);
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

    mime_vm_init_boot(vm);
    MimeBootInfo *boot = vm->boot;
    boot->memory_block_shift = 14;

    uint64_t freq;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    boot->timer_frequency = freq;

    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    boot->boot_filetime = mime_unix_to_filetime(now);
    /* the vcpu starts with a virtual counter offset of zero, so CNTVCT == mach_absolute_time. */
    boot->boot_counter = mach_absolute_time();
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

void mime_vm_destroy(MimeVm *vm)
{
    for (uint32_t i = 0; i < vm->vcpu_count; i++) {
        if (vm->vcpus[i].thread)
            pthread_join(vm->vcpus[i].thread, NULL);
    }
    mime_fs_close_all(vm);
    mime_net_close_all();
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

