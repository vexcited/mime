/*
 * x64 KVM backend with split irqchip. local APICs, timers, IPIs and HLT never leave the kernel. host interrupts use
 * an irqfd routed as MSI; port-write hypercalls exchange registers through kvm_run.
 */
#include "../vmm/vmm.h"

#include "../rom.h"
#include "../audio/audio.h"
#include "../camera/camera.h"
#include "../gpu/gpu.h"
#include "../identity.h"

#include <errno.h>
#include <sched.h>
#include <fcntl.h>
#include <linux/futex.h>
#include <linux/kvm.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <x86intrin.h>

#define KICK_SIGNAL SIGUSR2
#define IRQ_GSI 24 /* past the routes KVM keeps for a userspace ioapic, which mime has none of. */
#define MSR_IA32_TSC 0x10

#define CR0_PE (1ull << 0)
#define CR0_MP (1ull << 1)
#define CR0_ET (1ull << 4)
#define CR0_NE (1ull << 5)
#define CR0_WP (1ull << 16)
#define CR0_PG (1ull << 31)
#define CR4_PAE (1ull << 5)
#define CR4_OSFXSR (1ull << 9)
#define CR4_OSXMMEXCPT (1ull << 10)
#define EFER_SCE (1ull << 0)
#define EFER_LME (1ull << 8)
#define EFER_LMA (1ull << 10)
#define EFER_NXE (1ull << 11)

enum {
    SLOT_RAM = 0,
    SLOT_ROM = 1,
    SLOT_FIRST_WINDOW = 2,
};

static int g_vcpu_mmap_size;
static struct kvm_cpuid2 *g_cpuid;
static volatile bool g_dump_requested;
static volatile bool g_profiling;
static void profile_sample(MimeVcpu *vcpu);

void mime_host_volume_label(char *out, size_t size)
{
    mime_strlcpy(out, "Data", size);
}

void mime_host_wait_word(uint32_t *word, uint32_t value, uint64_t timeout_ns)
{
    struct timespec ts = {(time_t)(timeout_ns / 1000000000u), (long)(timeout_ns % 1000000000u)};
    syscall(SYS_futex, word, FUTEX_WAIT_PRIVATE, value, &ts, nullptr, 0);
}

void mime_host_wake_word(uint32_t *word)
{
    syscall(SYS_futex, word, FUTEX_WAKE_PRIVATE, INT32_MAX, nullptr, nullptr, 0);
}

void mime_host_wake_word_one(uint32_t *word)
{
    syscall(SYS_futex, word, FUTEX_WAKE_PRIVATE, 1, nullptr, nullptr, 0);
}

/*
 * the guest's memory is all of the host's. programs size their caches by what they are told, so they get what a native
 * machine would tell them. none of it is reserved: the host backs a page when the guest first writes it, the guest asks
 * for each new host block with MIME_HC_MEM_GRANT, which keeps a floor for the desktop, and free pages go back through
 * MIME_HC_MEM_REPORT.
 */
uint64_t mime_vm_default_ram(void)
{
    long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGESIZE);
    if (pages <= 0 || page <= 0)
        return MIME_VMM_FALLBACK_RAM;
    uint64_t mem = (uint64_t)pages * (uint64_t)page;
    return MIME_ALIGN_DOWN(MIME_MIN(mem, MIME_VMM_MAX_RAM), 2ull << 20);
}

/* every processor the machine may run on, up to the 64 of the guest's one processor group. */
static uint32_t host_cpu_count(void)
{
    cpu_set_t set;
    long n = sched_getaffinity(0, sizeof(set), &set) == 0 ? CPU_COUNT(&set) : sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1)
        n = 1;
    return (uint32_t)MIME_MIN((uint64_t)n, MIME_VMM_MAX_VCPUS);
}

/* checks whether transparent huge pages are always enabled or enabled on request. */
static bool transparent_huge_pages(MimeVm *vm)
{
    char mode[128] = {};
    FILE *f = fopen("/sys/kernel/mm/transparent_hugepage/enabled", "r");
    if (f) {
        if (!fgets(mode, sizeof(mode), f))
            mode[0] = 0;
        fclose(f);
    }
    return strstr(mode, "[always]") || (vm->huge_pages && strstr(mode, "[madvise]"));
}

/* host swap substitutes for a guest paging file. */
static uint64_t host_swap_size(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f)
        return 0;
    char line[128];
    unsigned long long kb = 0;
    while (fgets(line, sizeof(line), f))
        if (sscanf(line, "SwapTotal: %llu kB", &kb) == 1)
            break;
    fclose(f);
    return (uint64_t)kb * 1024;
}

static bool set_slot(MimeVm *vm, uint32_t slot, uint64_t ipa, uint64_t size, void *host, bool readonly)
{
    struct kvm_userspace_memory_region region = {
        .slot = slot,
        .flags = readonly ? KVM_MEM_READONLY : 0,
        .guest_phys_addr = ipa,
        .memory_size = size,
        .userspace_addr = (uint64_t)(uintptr_t)host,
    };
    if (ioctl(vm->vm_fd, KVM_SET_USER_MEMORY_REGION, &region) != 0) {
        mime_error("cannot map guest memory at %#llx (%llu bytes): %s", (unsigned long long)ipa,
                   (unsigned long long)size, strerror(errno));
        return false;
    }
    return true;
}

uint32_t mime_kvm_alloc_slot(MimeVm *vm)
{
    if (vm->free_slot_count)
        return vm->free_slots[--vm->free_slot_count];
    return vm->next_slot++;
}

void mime_kvm_free_slot(MimeVm *vm, uint32_t slot)
{
    if (vm->free_slot_count < MIME_VMM_MAX_WINDOWS)
        vm->free_slots[vm->free_slot_count++] = slot;
}

bool mime_kvm_set_slot(MimeVm *vm, uint32_t slot, uint64_t ipa, uint64_t size, void *host, bool readonly)
{
    return set_slot(vm, slot, ipa, size, host, readonly);
}

/* the 12 characters of a hypervisor vendor in ebx, ecx and edx. */
static void cpuid_vendor(struct kvm_cpuid_entry2 *e, const char *vendor)
{
    memcpy(&e->ebx, vendor, 4);
    memcpy(&e->ecx, vendor + 4, 4);
    memcpy(&e->edx, vendor + 8, 4);
}

/*
 * exposes KVM's Hyper-V interface with Microsoft's vendor and version, root-partition privileges for partition
 * creation and CPU management, and root-only hardware features. suppresses KVM-specific CPUID leaves.
 */
static uint32_t add_hyperv_leaves(MimeVm *vm, struct kvm_cpuid_entry2 *out, uint32_t room)
{
    struct kvm_cpuid2 *hv = calloc(1, sizeof(*hv) + 64 * sizeof(struct kvm_cpuid_entry2));
    hv->nent = 64;
    if (ioctl(vm->kvm_fd, KVM_CHECK_EXTENSION, KVM_CAP_SYS_HYPERV_CPUID) <= 0 ||
        ioctl(vm->kvm_fd, KVM_GET_SUPPORTED_HV_CPUID, hv) != 0) {
        free(hv);
        return 0;
    }
    uint32_t n = 0;
    for (uint32_t i = 0; i < hv->nent && n < room; i++) {
        struct kvm_cpuid_entry2 *e = &hv->entries[i];
        if (e->function > 0x4000000B)
            continue;
        switch (e->function) {
        case 0x40000000:
            e->eax = 0x4000000B;
            cpuid_vendor(e, "Microsoft Hv");
            break;
        case 0x40000002: /* build, version 10.0, update. */
            e->eax = 26300;
            e->ebx = 0x000A0000;
            e->ecx = 0;
            e->edx = 9457;
            break;
        case 0x40000003:
            e->ebx |= 0x000039FF;
            break;
        }
        out[n++] = *e;
    }
    free(hv);
    if (n && n < room) {
        /* apic overlay assist, MSR bitmaps, architectural counters, second level translation. */
        struct kvm_cpuid_entry2 features = {.function = 0x40000006, .eax = 0x0000000F};
        out[n++] = features;
    }
    return n;
}

/*
 * the guest reports the reference machine's processor instead of the host's: its vendor, family, model, stepping
 * and brand string. every feature bit stays the host's, since the guest runs the host's instructions, except the
 * avx512 bits, which the reference model does not have.
 */
static void spoof_model(struct kvm_cpuid2 *cpuid)
{
    const MimeHardwareProfile *profile = mime_hardware_profile();
    if (!profile || !profile->cpu_vendor || strlen(profile->cpu_vendor) != 12)
        return;
    for (uint32_t i = 0; i < cpuid->nent; i++) {
        struct kvm_cpuid_entry2 *e = &cpuid->entries[i];
        switch (e->function) {
        case 0:
            /* the processor vendor of leaf 0 runs through ebx, edx and ecx, unlike a hypervisor's. */
            memcpy(&e->ebx, profile->cpu_vendor, 4);
            memcpy(&e->edx, profile->cpu_vendor + 4, 4);
            memcpy(&e->ecx, profile->cpu_vendor + 8, 4);
            break;
        case 1:
            e->eax = (profile->cpu_family << 8) | (profile->cpu_model << 4) | profile->cpu_stepping;
            if (profile->cpu_family >= 0xF)
                e->eax |= ((profile->cpu_family - 0xF) << 20) | ((profile->cpu_model >> 4) << 16);
            break;
        case 7:
            if (e->index == 0) {
                e->ebx &= ~((1u << 16) | (1u << 17) | (1u << 30) | (1u << 31)); /* avx512 f, dq, bw, ifma */
                e->ecx &= ~((1u << 1) | (1u << 6) | (1u << 11) | (1u << 12) | (1u << 14) | (1u << 16) |
                            (1u << 17) | (1u << 21));
                e->edx &= ~((1u << 8) | (1u << 9) | (1u << 21) | (1u << 22) | (1u << 26) | (1u << 27) |
                            (1u << 30) | (1u << 31));
            }
            break;
        case 0x80000002:
        case 0x80000003:
        case 0x80000004: {
            char brand[48] = {};
            mime_strlcpy(brand, profile->cpu_name, sizeof(brand));
            const size_t at = (size_t)(e->function - 0x80000002) * 16;
            memcpy(&e->eax, brand + at, 4);
            memcpy(&e->ebx, brand + at + 4, 4);
            memcpy(&e->ecx, brand + at + 8, 4);
            memcpy(&e->edx, brand + at + 12, 4);
            break;
        }
        default:
            break;
        }
    }
}

/*
 * exposes the host CPU features KVM supports, including x2APIC and TSC deadline timers. advertises Hyper-V when
 * supported, otherwise no hypervisor.
 */
static bool load_cpuid(MimeVm *vm)
{
    int n = 256;
    for (;;) {
        g_cpuid = calloc(1, sizeof(*g_cpuid) + (size_t)n * sizeof(struct kvm_cpuid_entry2));
        g_cpuid->nent = (uint32_t)n;
        if (ioctl(vm->kvm_fd, KVM_GET_SUPPORTED_CPUID, g_cpuid) == 0)
            break;
        free(g_cpuid);
        g_cpuid = nullptr;
        if (errno != E2BIG || n > 4096) {
            mime_error("KVM_GET_SUPPORTED_CPUID failed: %s", strerror(errno));
            return false;
        }
        n *= 2;
    }
    uint32_t out = 0;
    for (uint32_t i = 0; i < g_cpuid->nent; i++) {
        struct kvm_cpuid_entry2 *e = &g_cpuid->entries[i];
        if (e->function >= 0x40000000 && e->function < 0x40010000)
            continue;
        if (e->function == 1)
            e->ecx |= (1u << 21) | (1u << 24); /* x2apic, TSC deadline. */
        g_cpuid->entries[out++] = *e;
    }
    uint32_t hv = add_hyperv_leaves(vm, g_cpuid->entries + out, (uint32_t)n - out);
    for (uint32_t i = 0; i < out; i++) {
        struct kvm_cpuid_entry2 *e = &g_cpuid->entries[i];
        if (e->function == 1)
            e->ecx = hv ? e->ecx | (1u << 31) : e->ecx & ~(1u << 31);
    }
    g_cpuid->nent = out + hv;
    spoof_model(g_cpuid);
    mime_info("processor: %s hypervisor", hv ? "microsoft's" : "no");
    return true;
}

static bool cpuid_has(uint32_t function, int reg, int bit)
{
    for (uint32_t i = 0; i < g_cpuid->nent; i++) {
        const struct kvm_cpuid_entry2 *e = &g_cpuid->entries[i];
        if (e->function == function && e->index == 0) {
            uint32_t v = reg == 0 ? e->eax : reg == 1 ? e->ebx : reg == 2 ? e->ecx : e->edx;
            return (v >> bit) & 1;
        }
    }
    return false;
}

static void on_kick(int sig)
{
    (void)sig;
}

/* the host's interrupt reaches processor 0 at MIME_X64_IRQ_VECTOR, from an eventfd any thread can write. */
static bool create_irq(MimeVm *vm)
{
    struct kvm_irq_routing *r = calloc(1, sizeof(*r) + sizeof(struct kvm_irq_routing_entry));
    r->nr = 1;
    r->entries[0].gsi = IRQ_GSI;
    r->entries[0].type = KVM_IRQ_ROUTING_MSI;
    r->entries[0].u.msi.address_lo = 0xFEE00000u;
    r->entries[0].u.msi.data = MIME_X64_IRQ_VECTOR;
    int result = ioctl(vm->vm_fd, KVM_SET_GSI_ROUTING, r);
    free(r);
    if (result != 0) {
        mime_error("KVM_SET_GSI_ROUTING failed: %s", strerror(errno));
        return false;
    }
    vm->irq_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    struct kvm_irqfd irqfd = {.fd = (uint32_t)vm->irq_fd, .gsi = IRQ_GSI};
    if (vm->irq_fd < 0 || ioctl(vm->vm_fd, KVM_IRQFD, &irqfd) != 0) {
        mime_error("KVM_IRQFD failed: %s", strerror(errno));
        return false;
    }
    return true;
}

bool mime_vm_create(MimeVm *vm, const MimePaths *paths, uint64_t ram_size)
{
    memset(vm, 0, sizeof(*vm));
    vm->paths = paths;
    vm->ram_size = ram_size;
    vm->vcpu_count = host_cpu_count();
    vm->kvm_fd = vm->vm_fd = vm->irq_fd = -1;
    vm->next_slot = SLOT_FIRST_WINDOW;
    pthread_mutex_init(&vm->lock, nullptr);
    pthread_cond_init(&vm->vcpu_cond, nullptr);
    pthread_mutex_init(&vm->fs_lock, nullptr);
    pthread_mutex_init(&vm->window_lock, nullptr);

    vm->kvm_fd = open("/dev/kvm", O_RDWR | O_CLOEXEC);
    if (vm->kvm_fd < 0) {
        mime_error("cannot open /dev/kvm: %s; is kvm enabled and are you in the kvm group?", strerror(errno));
        return false;
    }
    if (ioctl(vm->kvm_fd, KVM_GET_API_VERSION, 0) != KVM_API_VERSION) {
        mime_error("unsupported kvm api version");
        return false;
    }
    g_vcpu_mmap_size = ioctl(vm->kvm_fd, KVM_GET_VCPU_MMAP_SIZE, 0);
    vm->vm_fd = ioctl(vm->kvm_fd, KVM_CREATE_VM, 0);
    if (vm->vm_fd < 0 || g_vcpu_mmap_size <= 0) {
        mime_error("KVM_CREATE_VM failed: %s", strerror(errno));
        return false;
    }
    int max_vcpus = ioctl(vm->kvm_fd, KVM_CHECK_EXTENSION, KVM_CAP_MAX_VCPUS);
    if (max_vcpus > 0 && vm->vcpu_count > (uint32_t)max_vcpus)
        vm->vcpu_count = (uint32_t)max_vcpus;
    if (!ioctl(vm->kvm_fd, KVM_CHECK_EXTENSION, KVM_CAP_SYNC_REGS)) {
        mime_error("kvm lacks KVM_CAP_SYNC_REGS");
        return false;
    }
    /* intel needs these pages for its real mode emulation, unused by a guest starting in long mode. */
    ioctl(vm->vm_fd, KVM_SET_TSS_ADDR, 0xFFFBD000ul);
    uint64_t identity = 0xFFFBC000ull;
    ioctl(vm->vm_fd, KVM_SET_IDENTITY_MAP_ADDR, &identity);
    struct kvm_enable_cap split = {.cap = KVM_CAP_SPLIT_IRQCHIP, .args = {IRQ_GSI}};
    if (ioctl(vm->vm_fd, KVM_ENABLE_CAP, &split) != 0) {
        mime_error("kvm cannot keep the local apics in the kernel: %s", strerror(errno));
        return false;
    }
    if (!load_cpuid(vm))
        return false;
    if (!cpuid_has(0x80000001, 3, 26)) {
        mime_error("the processor has no 1 GiB pages, which the guest's file windows need");
        return false;
    }

    /*
     * reserves uncommitted RAM aligned to 2 MiB, using huge pages where available so guest blocks align with host
     * pages.
     */
    uint64_t align = 2ull << 20;
    uint8_t *raw = mmap(nullptr, ram_size + align, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (raw == MAP_FAILED) {
        mime_error("cannot reserve %llu bytes of guest memory", (unsigned long long)ram_size);
        return false;
    }
    vm->ram = (uint8_t *)MIME_ALIGN_UP((uintptr_t)raw, (uintptr_t)align);
    if (vm->ram != raw)
        munmap(raw, (size_t)(vm->ram - raw));
    munmap(vm->ram + ram_size, (size_t)(raw + align - vm->ram));
    vm->huge_pages = madvise(vm->ram, ram_size, MADV_HUGEPAGE) == 0;
    /* guest memory stays out of core dumps, a crash would otherwise write gigabytes. */
    madvise(vm->ram, ram_size, MADV_DONTDUMP);
    if (!set_slot(vm, SLOT_RAM, MIME_RAM_BASE, ram_size, vm->ram, false))
        return false;

    /* the system files, read only, straight from the executable's pages in the page cache. */
    if (((uintptr_t)mime_rom & 0xFFF) || !set_slot(vm, SLOT_ROM, MIME_ROM_BASE, MIME_ALIGN_UP(mime_rom_size(), 0x1000),
                                                   (void *)mime_rom, true)) {
        mime_error("cannot map the system files");
        return false;
    }
    if (!create_irq(vm))
        return false;

    struct sigaction sa = {.sa_handler = on_kick};
    sigemptyset(&sa.sa_mask);
    sigaction(KICK_SIGNAL, &sa, nullptr);

    mime_vm_init_boot(vm);
    vm->boot->irq_intid = MIME_X64_IRQ_VECTOR;
    vm->boot->memory_block_shift = transparent_huge_pages(vm) ? 21 : 12;
    vm->boot->swap_size = host_swap_size();
    return true;
}

/*
 * returns unused guest pages to the host. no host thread may retain them; GPU and audio close before freeing. KVM
 * drops mappings through the MMU notifier, and subsequent reads return zeros until written.
 */
void mime_vm_discard(MimeVm *vm, uint64_t pa, uint64_t len)
{
    if (madvise(vm->ram + (pa - MIME_RAM_BASE), len, MADV_DONTNEED) != 0)
        mime_error("cannot discard guest memory at 0x%llx", (unsigned long long)pa);
}

/* sparse metadata stays in small pages: one touch would otherwise commit a whole huge page. */
void mime_vm_advise(MimeVm *vm, uint64_t pa, uint64_t len, uint32_t advice)
{
    uint64_t start = MIME_ALIGN_DOWN(pa, (uint64_t)MIME_HOST_PAGE);
    uint64_t end = MIME_ALIGN_UP(pa + len, (uint64_t)MIME_HOST_PAGE);
    if (advice == MIME_MEM_SPARSE)
        madvise(vm->ram + (start - MIME_RAM_BASE), end - start, MADV_NOHUGEPAGE);
}

void mime_vm_raise_irq(MimeVm *vm, uint32_t bits)
{
    __atomic_fetch_or(&vm->shared->irq_pending, bits, __ATOMIC_SEQ_CST);
    uint64_t one = 1;
    if (write(vm->irq_fd, &one, sizeof(one)) < 0 && errno != EAGAIN)
        mime_error("cannot raise the guest interrupt: %s", strerror(errno));
}

void mime_vm_shutdown(MimeVm *vm)
{
    /*
     * closes channels left open at power-off before driver teardown; otherwise driver threads may remain blocked on
     * GPU work and crash during process destruction.
     */
    mime_audio_close_all();
    mime_camera_close_all();
    mime_vk_close_all();
    mime_gpu_close_all();
}

void mime_vm_destroy(MimeVm *vm)
{
    for (uint32_t i = 0; i < vm->vcpu_count; i++) {
        if (vm->vcpus[i].thread)
            pthread_join(vm->vcpus[i].thread, nullptr);
    }
    mime_fs_close_all(vm);
    mime_net_close_all();
    for (uint32_t i = 0; i < vm->vcpu_count; i++) {
        if (vm->vcpus[i].run)
            munmap(vm->vcpus[i].run, (size_t)g_vcpu_mmap_size);
        if (vm->vcpus[i].fd > 0)
            close(vm->vcpus[i].fd);
    }
    if (vm->irq_fd >= 0)
        close(vm->irq_fd);
    if (vm->vm_fd >= 0)
        close(vm->vm_fd);
    if (vm->kvm_fd >= 0)
        close(vm->kvm_fd);
    if (vm->ram)
        munmap(vm->ram, vm->ram_size);
}

/* ------------------------------------------------------------------ vcpus */

static void dump_state(MimeVcpu *vcpu, const char *why);

void mime_vcpu_kick(MimeVcpu *vcpu)
{
    if (vcpu->run)
        __atomic_store_n(&vcpu->run->immediate_exit, 1, __ATOMIC_SEQ_CST);
    if (vcpu->thread)
        pthread_kill(vcpu->thread, KICK_SIGNAL);
}

void mime_vcpu_request_dump(MimeVm *vm)
{
    g_dump_requested = true;
    for (uint32_t i = 0; i < vm->vcpu_count; i++)
        mime_vcpu_kick(&vm->vcpus[i]);
}

static void set_segment(struct kvm_segment *s, uint16_t selector, uint8_t type, bool code)
{
    memset(s, 0, sizeof(*s));
    s->base = 0;
    s->limit = 0xFFFFFFFF;
    s->selector = selector;
    s->type = type;
    s->present = 1;
    s->s = 1;
    s->g = 1;
    s->l = code;
    s->db = !code;
}

/* long mode with paging on the kernel's tables, as the loader built them. */
static bool setup_vcpu(MimeVcpu *vcpu)
{
    MimeVm *vm = vcpu->vm;
    struct kvm_cpuid2 *cpuid = calloc(1, sizeof(*cpuid) + g_cpuid->nent * sizeof(struct kvm_cpuid_entry2));
    memcpy(cpuid, g_cpuid, sizeof(*cpuid) + g_cpuid->nent * sizeof(struct kvm_cpuid_entry2));
    for (uint32_t i = 0; i < cpuid->nent; i++) {
        struct kvm_cpuid_entry2 *e = &cpuid->entries[i];
        if (e->function == 1)
            e->ebx = (e->ebx & 0x00FFFFFF) | vcpu->index << 24;
        if (e->function == 0xB || e->function == 0x1F)
            e->edx = vcpu->index;
    }
    int r = ioctl(vcpu->fd, KVM_SET_CPUID2, cpuid);
    free(cpuid);
    if (r != 0) {
        mime_error("KVM_SET_CPUID2 failed: %s", strerror(errno));
        return false;
    }

    struct kvm_sregs sregs;
    if (ioctl(vcpu->fd, KVM_GET_SREGS, &sregs) != 0)
        return false;
    set_segment(&sregs.cs, 0x10, 0xB, true);
    set_segment(&sregs.ds, 0x18, 0x3, false);
    sregs.es = sregs.ss = sregs.fs = sregs.gs = sregs.ds;
    memset(&sregs.tr, 0, sizeof(sregs.tr));
    sregs.tr.selector = 0x40;
    sregs.tr.limit = 0x67;
    sregs.tr.type = 0xB;
    sregs.tr.present = 1;
    memset(&sregs.ldt, 0, sizeof(sregs.ldt));
    sregs.ldt.type = 0x2;
    sregs.ldt.unusable = 1;
    sregs.gdt.base = 0;
    sregs.gdt.limit = 0;
    sregs.idt.base = 0;
    sregs.idt.limit = 0;
    sregs.cr0 = CR0_PE | CR0_MP | CR0_ET | CR0_NE | CR0_WP | CR0_PG;
    sregs.cr3 = vm->boot->ttbr1_phys;
    sregs.cr4 = CR4_PAE | CR4_OSFXSR | CR4_OSXMMEXCPT;
    sregs.efer = EFER_SCE | EFER_LME | EFER_LMA | EFER_NXE;
    if (ioctl(vcpu->fd, KVM_SET_SREGS, &sregs) != 0) {
        mime_error("KVM_SET_SREGS failed: %s", strerror(errno));
        return false;
    }

    /* the guest's TSC runs with the host's, so both read the same clock. */
    struct kvm_msrs *msrs = calloc(1, sizeof(*msrs) + sizeof(struct kvm_msr_entry));
    msrs->nmsrs = 1;
    msrs->entries[0].index = MSR_IA32_TSC;
    msrs->entries[0].data = __rdtsc();
    ioctl(vcpu->fd, KVM_SET_MSRS, msrs);
    free(msrs);
    return true;
}

static void set_entry_regs(MimeVcpu *vcpu, uint64_t pc, uint64_t arg, uint64_t sp)
{
    struct kvm_regs *regs = &vcpu->run->s.regs.regs;
    memset(regs, 0, sizeof(*regs));
    regs->rip = pc;
    regs->rsp = sp;
    regs->rcx = arg;
    regs->rflags = 0x2;
    vcpu->run->kvm_dirty_regs |= KVM_SYNC_X86_REGS;
}

static bool handle_io(MimeVcpu *vcpu)
{
    struct kvm_run *run = vcpu->run;
    if (run->io.direction != KVM_EXIT_IO_OUT || run->io.port != MIME_HC_PORT) {
        /* other ports have nothing behind them: reads see all ones. */
        if (run->io.direction == KVM_EXIT_IO_IN)
            memset((uint8_t *)run + run->io.data_offset, 0xFF, (size_t)run->io.size * run->io.count);
        return true;
    }
    struct kvm_regs *r = &run->s.regs.regs;
    uint64_t regs[8] = {r->rax, r->rdi, r->rsi, r->rdx, r->r10, r->r8, r->r9, 0};
    vcpu->exits_hvc[regs[0] & 63]++;
    uint64_t number = regs[0] & 255, started = __rdtsc();
    mime_hcall_dispatch(vcpu, regs);
    vcpu->hvc_calls[number]++;
    vcpu->hvc_ticks[number] += __rdtsc() - started;
    r->rax = regs[0];
    r->rdx = regs[1];
    run->kvm_dirty_regs |= KVM_SYNC_X86_REGS;
    return true;
}

static void *vcpu_main(void *arg)
{
    MimeVcpu *vcpu = arg;
    MimeVm *vm = vcpu->vm;
    mime_set_thread_name("mime-vcpu");

    vcpu->fd = ioctl(vm->vm_fd, KVM_CREATE_VCPU, (unsigned long)vcpu->index);
    if (vcpu->fd < 0) {
        mime_error("KVM_CREATE_VCPU failed: %s", strerror(errno));
        mime_vm_request_stop(vm, 1);
        return nullptr;
    }
    vcpu->run = mmap(nullptr, (size_t)g_vcpu_mmap_size, PROT_READ | PROT_WRITE, MAP_SHARED, vcpu->fd, 0);
    if (vcpu->run == MAP_FAILED || !setup_vcpu(vcpu)) {
        vcpu->run = nullptr;
        mime_error("cannot set up vcpu %u", vcpu->index);
        mime_vm_request_stop(vm, 1);
        return nullptr;
    }
    vcpu->run->kvm_valid_regs = KVM_SYNC_X86_REGS;
    /* KVM holds processors other than the first for an init and startup ipi pair; the guest starts them by hypercall. */
    struct kvm_mp_state runnable = {KVM_MP_STATE_RUNNABLE};
    if (ioctl(vcpu->fd, KVM_SET_MP_STATE, &runnable) != 0)
        mime_warn("vcpu %u stays halted: %s", vcpu->index, strerror(errno));
    if (vcpu->index == 0) {
        int khz = ioctl(vcpu->fd, KVM_GET_TSC_KHZ, 0);
        vm->tsc_hz = khz > 0 ? (uint64_t)khz * 1000 : 0;
        vm->boot->timer_frequency = vm->tsc_hz;
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        vm->boot->boot_filetime = mime_unix_to_filetime(now);
        vm->boot->boot_counter = __rdtsc();
    }

    /* the boot CPU waits for all vCPUs to exist; secondary CPUs wait for the guest to start them. */
    pthread_mutex_lock(&vm->lock);
    vm->vcpus_created++;
    pthread_cond_broadcast(&vm->vcpu_cond);
    while (!vm->stopping && (vcpu->index == 0 ? vm->vcpus_created < vm->vcpu_count : !vcpu->started))
        pthread_cond_wait(&vm->vcpu_cond, &vm->lock);
    pthread_mutex_unlock(&vm->lock);
    if (vcpu->index == 0)
        set_entry_regs(vcpu, vm->kernel_entry, MIME_KVA_PHYSMAP + MIME_RAM_BASE + MIME_LAYOUT_BOOT_INFO,
                       vm->boot_stack_top);
    else
        set_entry_regs(vcpu, vcpu->start_pc, vcpu->start_x0, vcpu->start_sp);

    while (!vm->stopping) {
        int r = ioctl(vcpu->fd, KVM_RUN, 0);
        vcpu->exits_total++;
        if (r != 0) {
            if (errno == EINTR || errno == EAGAIN) {
                vcpu->exits_intr++;
                __atomic_store_n(&vcpu->run->immediate_exit, 0, __ATOMIC_SEQ_CST);
                if (g_profiling)
                    profile_sample(vcpu);
                if (g_dump_requested) {
                    dump_state(vcpu, "state dump requested");
                    if (vcpu->index == 0)
                        mime_vk_dump_stats();
                    if (vcpu->index + 1 == vm->vcpu_count)
                        g_dump_requested = false;
                }
                continue;
            }
            if (errno == EFAULT && vcpu->run->exit_reason == KVM_EXIT_MEMORY_FAULT) {
                /* a file-window backing file was truncated, leaving no page at the guest address. */
                uint64_t gpa = vcpu->run->memory_fault.gpa;
                if (gpa >= MIME_WINDOW_BASE && gpa - MIME_WINDOW_BASE < MIME_WINDOW_SIZE && vm->window_host &&
                    mime_window_bus_fault(vm->window_host + (gpa - MIME_WINDOW_BASE))) {
                    mime_warn("vcpu %u: the page of a window at %#llx was gone, replaced by zeros", vcpu->index,
                              (unsigned long long)gpa);
                    continue;
                }
                mime_error("KVM_RUN failed: no memory behind %#llx (%llu bytes, flags %#llx)", (unsigned long long)gpa,
                           (unsigned long long)vcpu->run->memory_fault.size,
                           (unsigned long long)vcpu->run->memory_fault.flags);
            } else {
                mime_error("KVM_RUN failed: %s (exit reason %u)", strerror(errno), vcpu->run->exit_reason);
            }
            dump_state(vcpu, "KVM_RUN failed");
            mime_vm_request_stop(vm, 1);
            break;
        }
        switch (vcpu->run->exit_reason) {
        case KVM_EXIT_IO:
            vcpu->exits_io++;
            handle_io(vcpu);
            break;
        case KVM_EXIT_INTR:
            vcpu->exits_intr++;
            break;
        case KVM_EXIT_HLT:
            vcpu->exits_halt++;
            break;
        case KVM_EXIT_HYPERV:
            if (vcpu->run->hyperv.type == KVM_EXIT_HYPERV_HCALL) {
                uint64_t regs[18] = {};
                regs[0] = vcpu->run->hyperv.u.hcall.input;
                regs[1] = vcpu->run->hyperv.u.hcall.params[0];
                regs[2] = vcpu->run->hyperv.u.hcall.params[1];
                mime_hv_dispatch(vcpu, true, regs);
                vcpu->run->hyperv.u.hcall.result = regs[0];
            }
            break;
        case KVM_EXIT_MMIO:
            if (!vcpu->run->mmio.is_write)
                memset(vcpu->run->mmio.data, 0xFF, sizeof(vcpu->run->mmio.data));
            dump_state(vcpu, "guest accessed unmapped physical memory");
            mime_daemon_guest_halted(vm, 3);
            mime_vm_request_stop(vm, 3);
            break;
        case KVM_EXIT_SHUTDOWN:
            dump_state(vcpu, "guest triple fault");
            mime_daemon_guest_halted(vm, 3);
            mime_vm_request_stop(vm, 3);
            break;
        case KVM_EXIT_FAIL_ENTRY:
            mime_error("vcpu %u entry failed, reason %#llx", vcpu->index,
                       (unsigned long long)vcpu->run->fail_entry.hardware_entry_failure_reason);
            mime_vm_request_stop(vm, 1);
            break;
        case KVM_EXIT_INTERNAL_ERROR:
            dump_state(vcpu, "kvm internal error");
            mime_error("  suberror %u", vcpu->run->internal.suberror);
            mime_vm_request_stop(vm, 1);
            break;
        default:
            mime_error("unexpected vcpu exit reason %u", vcpu->run->exit_reason);
            mime_vm_request_stop(vm, 1);
            break;
        }
    }
    return nullptr;
}

bool mime_vcpu_start_secondary(MimeVm *vm, uint32_t index, uint64_t pc, uint64_t x0, uint64_t sp)
{
    if (index == 0 || index >= vm->vcpu_count)
        return false;
    MimeVcpu *vcpu = &vm->vcpus[index];
    pthread_mutex_lock(&vm->lock);
    bool ok = !vcpu->started;
    if (ok) {
        vcpu->start_pc = pc;
        vcpu->start_x0 = x0;
        vcpu->start_sp = sp;
        vcpu->started = true;
        pthread_cond_broadcast(&vm->vcpu_cond);
    }
    pthread_mutex_unlock(&vm->lock);
    return ok;
}

bool mime_vcpu_start(MimeVcpu *vcpu)
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8u << 20);
    int r = pthread_create(&vcpu->thread, &attr, vcpu_main, vcpu);
    pthread_attr_destroy(&attr);
    return r == 0;
}

static void *profile_thread(void *arg);
static sem_t g_profile_sem;

/*
 * uses a vCPU that never runs for KVM prefault ioctls because running CPUs hold their ioctl context. prefaulting
 * avoids first-read exits and page-cache lookups, 3 to 4 microseconds per page; Helium startup reads 30 thousand
 * such pages.
 */
static int g_prefault_fd = -1;

void mime_kvm_prefault_open(MimeVm *vm)
{
    if (ioctl(vm->vm_fd, KVM_CHECK_EXTENSION, KVM_CAP_PRE_FAULT_MEMORY) <= 0)
        return;
    g_prefault_fd = ioctl(vm->vm_fd, KVM_CREATE_VCPU, (unsigned long)vm->vcpu_count);
}

/* maps [gpa, gpa + size) into the tables of the guest. false when KVM cannot. */
bool mime_kvm_prefault(MimeVm *vm, uint64_t gpa, uint64_t size)
{
    if (g_prefault_fd < 0)
        return false;
    struct kvm_pre_fault_memory range = {.gpa = gpa, .size = size};
    while (range.size) {
        if (ioctl(g_prefault_fd, KVM_PRE_FAULT_MEMORY, &range) != 0 && errno != EINTR && errno != EAGAIN)
            return false;
    }
    return true;
}

bool mime_vm_start(MimeVm *vm)
{
    pthread_t profiler;
    mime_kvm_prefault_open(vm);
    sem_init(&g_profile_sem, 0, 0);
    if (pthread_create(&profiler, nullptr, profile_thread, vm) == 0)
        pthread_detach(profiler);
    for (uint32_t i = 0; i < vm->vcpu_count; i++) {
        vm->vcpus[i].vm = vm;
        vm->vcpus[i].index = i;
        if (!mime_vcpu_start(&vm->vcpus[i]))
            return false;
    }
    return true;
}

/* ------------------------------------------------------------------ dumps */

/* walks the guest page tables of cr3; returns false for unmapped addresses. */
static bool guest_translate(MimeVm *vm, uint64_t cr3, uint64_t va, uint64_t *pa)
{
    uint64_t table = cr3 & 0x000FFFFFFFFFF000ull;
    for (int level = 0; level < 4; level++) {
        int shift = 39 - 9 * level;
        uint64_t *t = mime_vm_phys(vm, table, 4096);
        if (!t)
            return false;
        uint64_t e = t[(va >> shift) & 0x1FF];
        if (!(e & 1))
            return false;
        if (level == 3 || (level > 0 && (e & 0x80))) {
            uint64_t mask = (1ull << shift) - 1;
            *pa = (e & 0x000FFFFFFFFFF000ull & ~mask) | (va & mask);
            return true;
        }
        table = e & 0x000FFFFFFFFFF000ull;
    }
    return false;
}

static bool guest_read64(MimeVm *vm, uint64_t cr3, uint64_t va, uint64_t *value)
{
    uint64_t pa;
    if ((va & 7) || !guest_translate(vm, cr3, va, &pa))
        return false;
    uint64_t *p = mime_vm_phys(vm, pa, 8);
    if (!p)
        return false;
    *value = *p;
    return true;
}

static void dump_state(MimeVcpu *vcpu, const char *why)
{
    MimeVm *vm = vcpu->vm;
    struct kvm_regs regs;
    struct kvm_sregs sregs;
    if (ioctl(vcpu->fd, KVM_GET_REGS, &regs) != 0 || ioctl(vcpu->fd, KVM_GET_SREGS, &sregs) != 0) {
        mime_error("vcpu %u: %s, registers unavailable", vcpu->index, why);
        return;
    }
    mime_error("vcpu %u: %s", vcpu->index, why);
    mime_error("  exits %llu: io %llu, interrupted %llu, halt %llu", (unsigned long long)vcpu->exits_total,
               (unsigned long long)vcpu->exits_io, (unsigned long long)vcpu->exits_intr,
               (unsigned long long)vcpu->exits_halt);
    for (int i = 0; i < 256; i++)
        if (vcpu->hvc_calls[i])
            mime_error("  hypercall %#x: %llu calls, %llu us in the host", i, (unsigned long long)vcpu->hvc_calls[i],
                       (unsigned long long)(vm->tsc_hz ? vcpu->hvc_ticks[i] / (vm->tsc_hz / 1000000) : 0));
    mime_error("  rip %#llx rsp %#llx rflags %#llx cs %#x cr2 %#llx cr3 %#llx", (unsigned long long)regs.rip,
               (unsigned long long)regs.rsp, (unsigned long long)regs.rflags, sregs.cs.selector,
               (unsigned long long)sregs.cr2, (unsigned long long)sregs.cr3);
    mime_error("  rax %016llx rbx %016llx rcx %016llx rdx %016llx", (unsigned long long)regs.rax,
               (unsigned long long)regs.rbx, (unsigned long long)regs.rcx, (unsigned long long)regs.rdx);
    mime_error("  rsi %016llx rdi %016llx rbp %016llx r8  %016llx", (unsigned long long)regs.rsi,
               (unsigned long long)regs.rdi, (unsigned long long)regs.rbp, (unsigned long long)regs.r8);
    mime_error("  r9  %016llx r10 %016llx r11 %016llx r12 %016llx", (unsigned long long)regs.r9,
               (unsigned long long)regs.r10, (unsigned long long)regs.r11, (unsigned long long)regs.r12);
    mime_error("  r13 %016llx r14 %016llx r15 %016llx", (unsigned long long)regs.r13,
               (unsigned long long)regs.r14, (unsigned long long)regs.r15);
    /* the return addresses on the stack that point into the kernel, as the frame pointer is often omitted. */
    uint64_t sp = regs.rsp;
    int shown = 0;
    for (int i = 0; i < 512 && shown < 24; i++) {
        uint64_t v;
        if (!guest_read64(vm, sregs.cr3, sp + (uint64_t)i * 8, &v))
            break;
        if (v >= MIME_KVA_KERNEL && v < MIME_KVA_KERNEL + 0x100000000ull) {
            mime_error("  stack +%#x: %#llx", i * 8, (unsigned long long)v);
            shown++;
        }
    }
}

/* ------------------------------------------------------------------ profiling */

/*
 * kill -PROF starts sampling where the vcpus run, a second one stops and
 * logs the hottest addresses with their module.
 */
#define PROFILE_SLOTS 65536
#define PROFILE_VCPUS 64

typedef struct ProfileSample {
    uint64_t rip, cr3;
    uint32_t count;
} ProfileSample;

static ProfileSample *g_samples[PROFILE_VCPUS];

void mime_vcpu_toggle_profile(void) { sem_post(&g_profile_sem); }

static void profile_sample(MimeVcpu *vcpu)
{
    struct kvm_regs regs;
    struct kvm_sregs sregs;
    ProfileSample *table = vcpu->index < PROFILE_VCPUS ? g_samples[vcpu->index] : nullptr;
    if (!table || ioctl(vcpu->fd, KVM_GET_REGS, &regs) != 0 || ioctl(vcpu->fd, KVM_GET_SREGS, &sregs) != 0)
        return;
    /* kernel addresses are the same in every process. */
    uint64_t cr3 = regs.rip >> 63 ? 0 : sregs.cr3 & ~0xFFFull;
    uint64_t h = (regs.rip * 0x9E3779B97F4A7C15ull) ^ cr3;
    for (uint32_t i = (uint32_t)(h >> 40) & (PROFILE_SLOTS - 1), n = 0; n < PROFILE_SLOTS / 2;
         i = (i + 1) & (PROFILE_SLOTS - 1), n++) {
        ProfileSample *e = &table[i];
        if (!e->count) {
            *e = (ProfileSample){regs.rip, cr3, 1};
            return;
        }
        if (e->rip == regs.rip && e->cr3 == cr3) {
            e->count++;
            return;
        }
    }
}

static bool guest_read(MimeVm *vm, uint64_t cr3, uint64_t va, void *out, size_t size)
{
    while (size) {
        uint64_t pa;
        size_t n = MIME_MIN(size, 4096 - (va & 4095));
        if (!guest_translate(vm, cr3, va, &pa))
            return false;
        void *p = mime_vm_phys(vm, pa, n);
        if (!p)
            return false;
        memcpy(out, p, n);
        out = (uint8_t *)out + n, va += n, size -= n;
    }
    return true;
}

/* the image holding a user address: its export name and base. */
static bool find_module(MimeVm *vm, uint64_t cr3, uint64_t rip, char *name, size_t size, uint64_t *base)
{
    for (uint64_t b = rip & ~0xFFFFull, n = 0; n < 4096; b -= 0x10000, n++) {
        uint16_t mz;
        uint32_t lfanew, pe, image_size, exports, name_rva;
        if (!guest_read(vm, cr3, b, &mz, 2) || mz != 0x5A4D)
            continue;
        if (!guest_read(vm, cr3, b + 0x3C, &lfanew, 4) || !guest_read(vm, cr3, b + lfanew, &pe, 4) ||
            pe != 0x4550)
            continue;
        if (!guest_read(vm, cr3, b + lfanew + 24 + 56, &image_size, 4) || rip >= b + image_size)
            return false;
        snprintf(name, size, "?");
        if (guest_read(vm, cr3, b + lfanew + 24 + 112, &exports, 4) && exports &&
            guest_read(vm, cr3, b + exports + 12, &name_rva, 4)) {
            guest_read(vm, cr3, b + name_rva, name, size - 1);
            name[size - 1] = 0;
        }
        *base = b;
        return true;
    }
    return false;
}

static int compare_samples(const void *a, const void *b)
{
    uint32_t x = ((const ProfileSample *)a)->count, y = ((const ProfileSample *)b)->count;
    return x < y ? 1 : x > y ? -1 : 0;
}

/* merges per-CPU address counts and sorts by frequency. */
#define PROFILE_MERGED (PROFILE_SLOTS * 4)

static size_t profile_merge(ProfileSample *all, uint64_t *total)
{
    ProfileSample *hash = calloc(PROFILE_MERGED, sizeof(*hash));
    size_t count = 0;
    *total = 0;
    if (!hash)
        return 0;
    for (uint32_t v = 0; v < PROFILE_VCPUS; v++)
        for (uint32_t i = 0; g_samples[v] && i < PROFILE_SLOTS; i++) {
            ProfileSample *e = &g_samples[v][i];
            if (!e->count)
                continue;
            *total += e->count;
            uint64_t h = (e->rip * 0x9E3779B97F4A7C15ull) ^ e->cr3;
            for (uint32_t k = (uint32_t)(h >> 40) & (PROFILE_MERGED - 1), n = 0; n < PROFILE_MERGED;
                 k = (k + 1) & (PROFILE_MERGED - 1), n++) {
                if (!hash[k].count) {
                    hash[k] = *e;
                    break;
                }
                if (hash[k].rip == e->rip && hash[k].cr3 == e->cr3) {
                    hash[k].count += e->count;
                    break;
                }
            }
        }
    for (size_t k = 0; k < PROFILE_MERGED; k++)
        if (hash[k].count)
            all[count++] = hash[k];
    free(hash);
    qsort(all, count, sizeof(*all), compare_samples);
    return count;
}

static void profile_report(MimeVm *vm)
{
    uint64_t total;
    ProfileSample *all = calloc(PROFILE_MERGED, sizeof(*all));
    if (!all)
        return;
    size_t count = profile_merge(all, &total);
    mime_error("profile: %llu samples", (unsigned long long)total);
    /* groups samples by module, combining kernel addresses. */
    struct {
        char name[40];
        uint64_t count;
    } modules[64] = {};
    size_t used = 0;
    for (size_t i = 0; i < count; i++) {
        char name[64];
        uint64_t base;
        const char *who = "kernel";
        if (all[i].cr3)
            who = find_module(vm, all[i].cr3, all[i].rip, name, sizeof(name), &base) ? name : "user, no module";
        size_t m = 0;
        while (m < used && strcmp(modules[m].name, who) != 0)
            m++;
        if (m == used && used < 64) {
            snprintf(modules[used].name, sizeof(modules[used].name), "%s", who);
            used++;
        }
        if (m < used)
            modules[m].count += all[i].count;
    }
    for (size_t m = 0; m < used; m++)
        if (modules[m].count * 1000 >= total)
            mime_error("profile module %5.2f%% %s", 100.0 * modules[m].count / (double)total, modules[m].name);
    for (size_t i = 0; i < count && i < 600; i++) {
        char name[64];
        uint64_t base;
        if (all[i].cr3 && find_module(vm, all[i].cr3, all[i].rip, name, sizeof(name), &base))
            mime_error("profile %6u %5.2f%% %s+%#llx", all[i].count, 100.0 * all[i].count / (double)total, name,
                       (unsigned long long)(all[i].rip - base));
        else
            mime_error("profile %6u %5.2f%% %#llx", all[i].count, 100.0 * all[i].count / (double)total,
                       (unsigned long long)all[i].rip);
    }
    free(all);
}

static void *profile_thread(void *arg)
{
    MimeVm *vm = arg;
    mime_set_thread_name("mime-profile");
    for (;;) {
        while (sem_wait(&g_profile_sem) != 0) {}
        for (uint32_t v = 0; v < vm->vcpu_count && v < PROFILE_VCPUS; v++)
            g_samples[v] = calloc(PROFILE_SLOTS, sizeof(ProfileSample));
        mime_error("profile: sampling");
        g_profiling = true;
        while (sem_trywait(&g_profile_sem) != 0) {
            usleep(250);
            for (uint32_t v = 0; v < vm->vcpu_count; v++)
                mime_vcpu_kick(&vm->vcpus[v]);
        }
        g_profiling = false;
        usleep(10000);
        profile_report(vm);
        for (uint32_t v = 0; v < PROFILE_VCPUS; v++) {
            free(g_samples[v]);
            g_samples[v] = nullptr;
        }
    }
    return nullptr;
}
