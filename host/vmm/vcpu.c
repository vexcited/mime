#include "vmm.h"
#include "../gpu/gpu.h"
#include "../../guest/xta64/x86.h"

#include <mime/hyperv.h>

#include <Hypervisor/Hypervisor.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define EC_WFX 0x01
#define EC_HVC64 0x16
#define EC_SMC64 0x17
#define EC_SYSREG 0x18
#define EC_IABT_LOWER 0x20
#define EC_DABT_LOWER 0x24

#define SCTLR_EL1_VALUE 0x34D5D81Dull
#define TCR_EL1_VALUE                                                                                              \
    ((16ull << 0) | (1ull << 8) | (1ull << 10) | (3ull << 12) | (0ull << 14) | (16ull << 16) | (1ull << 24) |     \
     (1ull << 26) | (3ull << 28) | (2ull << 30) | (2ull << 32) | (1ull << 36))
#define MAIR_EL1_VALUE 0x00000000044400FFull
#define CPACR_EL1_VALUE (3ull << 20)
#define PSTATE_EL1H_MASKED 0x3C5ull
#define ACTLR_EL1_ENTSO (1ull << 1)

static uint64_t get_reg(MimeVcpu *vcpu, hv_reg_t reg)
{
    uint64_t v = 0;
    hv_vcpu_get_reg((hv_vcpu_t)vcpu->handle, reg, &v);
    return v;
}

static void set_reg(MimeVcpu *vcpu, hv_reg_t reg, uint64_t v)
{
    hv_vcpu_set_reg((hv_vcpu_t)vcpu->handle, reg, v);
}

static uint64_t get_sys(MimeVcpu *vcpu, hv_sys_reg_t reg)
{
    uint64_t v = 0;
    hv_vcpu_get_sys_reg((hv_vcpu_t)vcpu->handle, reg, &v);
    return v;
}

static void set_sys(MimeVcpu *vcpu, hv_sys_reg_t reg, uint64_t v)
{
    hv_return_t r = hv_vcpu_set_sys_reg((hv_vcpu_t)vcpu->handle, reg, v);
    if (r != HV_SUCCESS)
        mime_warn("hv_vcpu_set_sys_reg(%#x) failed: %#x", (unsigned)reg, r);
}

uint64_t mime_vcpu_sysreg(MimeVcpu *vcpu, uint32_t reg)
{
    return get_sys(vcpu, (hv_sys_reg_t)reg);
}

static void dump_backtrace(MimeVcpu *vcpu);

static void dump_exits(MimeVcpu *vcpu)
{
    mime_error("  exits %llu: vtimer %llu, canceled %llu, wfi %llu, sysreg %llu", (unsigned long long)vcpu->exits_total,
               (unsigned long long)vcpu->exits_vtimer, (unsigned long long)vcpu->exits_canceled,
               (unsigned long long)vcpu->exits_wfi, (unsigned long long)vcpu->exits_sysreg);
    for (int i = 0; i < 64; i++)
        if (vcpu->exits_hvc[i])
            mime_error("  exits hypercall %#x: %llu", i, (unsigned long long)vcpu->exits_hvc[i]);
    mime_error("  lazy memory: rom %llu KiB, %u windows %llu KiB", (unsigned long long)vcpu->vm->rom.filled_bytes >> 10,
               vcpu->vm->window_count, (unsigned long long)vcpu->vm->window_bytes >> 10);
}

static void dump_state(MimeVcpu *vcpu, const char *why)
{
    hv_vcpu_exit_t *exit = vcpu->exit_info;
    mime_error("vcpu %u: %s", vcpu->index, why);
    dump_exits(vcpu);
    mime_error("  syndrome %#llx va %#llx pa %#llx", (unsigned long long)exit->exception.syndrome,
               (unsigned long long)exit->exception.virtual_address,
               (unsigned long long)exit->exception.physical_address);
    mime_error("  pc %#llx cpsr %#llx sp_el1 %#llx sp_el0 %#llx", (unsigned long long)get_reg(vcpu, HV_REG_PC),
               (unsigned long long)get_reg(vcpu, HV_REG_CPSR), (unsigned long long)get_sys(vcpu, HV_SYS_REG_SP_EL1),
               (unsigned long long)get_sys(vcpu, HV_SYS_REG_SP_EL0));
    mime_error("  elr_el1 %#llx esr_el1 %#llx far_el1 %#llx vbar %#llx",
               (unsigned long long)get_sys(vcpu, HV_SYS_REG_ELR_EL1),
               (unsigned long long)get_sys(vcpu, HV_SYS_REG_ESR_EL1),
               (unsigned long long)get_sys(vcpu, HV_SYS_REG_FAR_EL1),
               (unsigned long long)get_sys(vcpu, HV_SYS_REG_VBAR_EL1));
    for (int i = 0; i < 31; i += 4) {
        char line[256];
        int n = 0;
        for (int j = i; j < i + 4 && j < 31; j++)
            n += snprintf(line + n, sizeof(line) - (size_t)n, " x%-2d %016llx", j,
                          (unsigned long long)get_reg(vcpu, (hv_reg_t)(HV_REG_X0 + j)));
        mime_error(" %s", line);
    }
    dump_backtrace(vcpu);
}

/* walks the guest stage 1 tables; returns false for unmapped addresses. */
static bool guest_translate(MimeVcpu *vcpu, uint64_t va, uint64_t *pa)
{
    uint64_t ttbr = (va >> 63) ? get_sys(vcpu, HV_SYS_REG_TTBR1_EL1) : get_sys(vcpu, HV_SYS_REG_TTBR0_EL1);
    uint64_t table = ttbr & 0x0000FFFFFFFFF000ull;
    for (int level = 0; level < 4; level++) {
        int shift = 39 - 9 * level;
        uint64_t *t = mime_vm_phys(vcpu->vm, table, MIME_PAGE_SIZE);
        if (!t)
            return false;
        uint64_t e = t[(va >> shift) & 0x1FF];
        if (!(e & 1))
            return false;
        if (level == 3 || !(e & 2)) {
            uint64_t mask = (1ull << shift) - 1;
            *pa = (e & 0x0000FFFFFFFFF000ull & ~mask) | (va & mask);
            return true;
        }
        table = e & 0x0000FFFFFFFFF000ull;
    }
    return false;
}

static bool guest_read64(MimeVcpu *vcpu, uint64_t va, uint64_t *value)
{
    uint64_t pa;
    if ((va & 7) || !guest_translate(vcpu, va, &pa))
        return false;
    uint64_t *p = mime_vm_phys(vcpu->vm, pa, 8);
    if (!p)
        return false;
    *value = *p;
    return true;
}

static void dump_backtrace(MimeVcpu *vcpu)
{
    uint64_t fp = get_reg(vcpu, HV_REG_FP);
    mime_error("  lr %#llx", (unsigned long long)get_reg(vcpu, HV_REG_LR));
    for (int depth = 0; depth < 24 && fp; depth++) {
        uint64_t next, ret;
        if (!guest_read64(vcpu, fp, &next) || !guest_read64(vcpu, fp + 8, &ret))
            break;
        mime_error("  frame %2d: %#llx", depth, (unsigned long long)ret);
        fp = next;
    }
}

static volatile bool g_dump_requested;

/*
 * a sampling profiler: kill -PROF starts it and stops it. while on, a thread kicks every vCPU each millisecond and
 * the vCPU records where it was, with ttbr0 telling the guest processes apart; stopping writes the samples to
 * profile.txt in the state directory, one "vcpu pc cpsr ttbr0 lr x86" line each, to symbolize with the guest's binaries.
 * x86 is the x86 address of the block xta64's translated code runs, read through the cpu translated code keeps in
 * x19: it only means something for samples in translated code.
 */
typedef struct ProfileSample {
    uint64_t pc, ttbr0, lr, x86;
    uint32_t cpsr, vcpu;
} ProfileSample;

static uint64_t profile_x86(MimeVcpu *vcpu)
{
    uint64_t block, rip;
    if (!guest_read64(vcpu, get_reg(vcpu, HV_REG_X19) + offsetof(X86Cpu, jit_block), &block) || !block ||
        !guest_read64(vcpu, block, &rip))
        return 0;
    return rip;
}

#define PROFILE_SAMPLES (1u << 22)

static volatile bool g_profiling;
static ProfileSample *g_samples;
static uint32_t g_sample_count;
static MimeVm *g_profile_vm;

void mime_vcpu_toggle_profile(void)
{
    g_profiling = !g_profiling;
}

static void profile_record(MimeVcpu *vcpu)
{
    vcpu->profile_pending = false;
    uint32_t at = __atomic_fetch_add(&g_sample_count, 1, __ATOMIC_RELAXED);
    if (!g_samples || at >= PROFILE_SAMPLES)
        return;
    uint32_t cpsr = (uint32_t)get_reg(vcpu, HV_REG_CPSR);
    g_samples[at] = (ProfileSample){get_reg(vcpu, HV_REG_PC), get_sys(vcpu, HV_SYS_REG_TTBR0_EL1),
                                    get_reg(vcpu, HV_REG_LR), (cpsr & 0xF) ? 0 : profile_x86(vcpu), cpsr, vcpu->index};
}

static void profile_write(void)
{
    char path[MIME_PATH_MAX];
    snprintf(path, sizeof(path), "%s/profile.txt", mime_daemon_paths()->state);
    FILE *f = fopen(path, "w");
    if (!f)
        return;
    uint32_t n = g_sample_count < PROFILE_SAMPLES ? g_sample_count : PROFILE_SAMPLES;
    for (uint32_t i = 0; i < n; i++)
        fprintf(f, "%u %llx %x %llx %llx %llx\n", g_samples[i].vcpu, (unsigned long long)g_samples[i].pc,
                g_samples[i].cpsr, (unsigned long long)g_samples[i].ttbr0, (unsigned long long)g_samples[i].lr,
                (unsigned long long)g_samples[i].x86);
    fclose(f);
    mime_info("profile: %u samples in %s", n, path);
}

static void *profile_main(void *arg)
{
    MimeVm *vm = arg;
    bool was = false;
    pthread_setname_np("mime-profile");
    for (;;) {
        if (g_profiling) {
            if (!was) {
                if (!g_samples)
                    g_samples = calloc(PROFILE_SAMPLES, sizeof(*g_samples));
                g_sample_count = 0;
                was = true;
                mime_info("profile: sampling");
            }
            for (uint32_t i = 0; i < vm->vcpu_count; i++) {
                vm->vcpus[i].profile_pending = true;
                mime_vcpu_kick(&vm->vcpus[i]);
            }
            usleep(1000);
        } else {
            if (was) {
                was = false;
                profile_write();
            }
            usleep(50000);
        }
    }
    return nullptr;
}

/* async-signal context: only sets a flag and forces the vCPUs out of the guest. */
void mime_vcpu_request_dump(MimeVm *vm)
{
    g_dump_requested = true;
    for (uint32_t i = 0; i < vm->vcpu_count; i++)
        mime_vcpu_kick(&vm->vcpus[i]);
}

void mime_vcpu_kick(MimeVcpu *vcpu)
{
    if (vcpu->handle != 0 || vcpu->thread) {
        hv_vcpu_t h = (hv_vcpu_t)vcpu->handle;
        hv_vcpus_exit(&h, 1);
    }
}

static bool handle_exception(MimeVcpu *vcpu)
{
    hv_vcpu_exit_t *exit = vcpu->exit_info;
    uint64_t syndrome = exit->exception.syndrome;
    uint32_t ec = (uint32_t)(syndrome >> 26) & 0x3F;

    switch (ec) {
    case EC_HVC64: {
        uint64_t regs[18];
        for (int i = 0; i < 18; i++)
            regs[i] = get_reg(vcpu, (hv_reg_t)(HV_REG_X0 + i));
        uint64_t immediate = syndrome & 0xFFFF;
        vcpu->exits_hvc[regs[0] & 63]++;
        if (immediate == 1 || (immediate == 0 && regs[0] == MIME_HV_SMCCC_HYPERCALL))
            mime_hv_dispatch(vcpu, immediate == 1, regs);
        else
            mime_hcall_dispatch(vcpu, regs);
        for (int i = 0; i < 18; i++)
            set_reg(vcpu, (hv_reg_t)(HV_REG_X0 + i), regs[i]);
        return true;
    }
    case EC_WFX:
        /* the virtual GIC handles WFI inside the macOS kernel; this is a fallback if that changes. */
        vcpu->exits_wfi++;
        set_reg(vcpu, HV_REG_PC, get_reg(vcpu, HV_REG_PC) + 4);
        return true;
    case EC_SYSREG: {
        /* unknown system register: reads as zero, writes are ignored. */
        uint32_t rt = (uint32_t)(syndrome >> 5) & 0x1F;
        bool read = syndrome & 1;
        vcpu->exits_sysreg++;
        mime_debug("guest system register access trapped, iss %#llx", (unsigned long long)(syndrome & 0x1FFFFFF));
        if (read && rt != 31)
            set_reg(vcpu, (hv_reg_t)(HV_REG_X0 + rt), 0);
        set_reg(vcpu, HV_REG_PC, get_reg(vcpu, HV_REG_PC) + 4);
        return true;
    }
    case EC_SMC64:
        set_reg(vcpu, HV_REG_X0, (uint64_t)-1);
        set_reg(vcpu, HV_REG_PC, get_reg(vcpu, HV_REG_PC) + 4);
        return true;
    case EC_DABT_LOWER:
        if (mime_fs_lazy_fault(vcpu->vm, exit->exception.physical_address))
            return true;
        dump_state(vcpu, "guest accessed unmapped physical memory");
        return false;
    case EC_IABT_LOWER:
        if (mime_fs_lazy_fault(vcpu->vm, exit->exception.physical_address))
            return true;
        dump_state(vcpu, "guest executed unmapped physical memory");
        return false;
    default: {
        char why[64];
        snprintf(why, sizeof(why), "unhandled exception class %#x", ec);
        dump_state(vcpu, why);
        return false;
    }
    }
}

static void *vcpu_main(void *arg)
{
    MimeVcpu *vcpu = arg;
    MimeVm *vm = vcpu->vm;
    hv_vcpu_t handle;
    hv_vcpu_exit_t *exit;
    pthread_setname_np("mime-vcpu");
    /* highest QoS prevents macOS from coalescing guest timer wakeups late. */
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);

    if (hv_vcpu_create(&handle, &exit, nullptr) != HV_SUCCESS) {
        mime_error("hv_vcpu_create failed");
        mime_vm_request_stop(vm, 1);
        return nullptr;
    }
    vcpu->handle = handle;
    vcpu->exit_info = exit;
    vcpu->vtimer_offset = 0;
    hv_vcpu_set_vtimer_offset(handle, 0);

    /* reference processor identity reported to the guest. */
    if (vm->boot->cpu_midr && hv_vcpu_set_sys_reg(handle, HV_SYS_REG_MIDR_EL1, vm->boot->cpu_midr) != HV_SUCCESS)
        mime_debug("the framework keeps its own midr_el1; the kernel uses the boot information instead");

    set_sys(vcpu, HV_SYS_REG_MAIR_EL1, MAIR_EL1_VALUE);
    set_sys(vcpu, HV_SYS_REG_TCR_EL1, TCR_EL1_VALUE);
    set_sys(vcpu, HV_SYS_REG_TTBR1_EL1, vm->boot->ttbr1_phys);
    set_sys(vcpu, HV_SYS_REG_TTBR0_EL1, 0);
    set_sys(vcpu, HV_SYS_REG_CPACR_EL1, CPACR_EL1_VALUE);
    set_sys(vcpu, HV_SYS_REG_SCTLR_EL1, SCTLR_EL1_VALUE);
    /* total store ordering, so translated x86 code keeps the memory model it was written for. */
    set_sys(vcpu, HV_SYS_REG_ACTLR_EL1, ACTLR_EL1_ENTSO);
    /* sixteen processors per cluster, so an sgi's target list reaches each by its number. */
    set_sys(vcpu, HV_SYS_REG_MPIDR_EL1, (1ull << 31) | ((uint64_t)(vcpu->index / 16) << 8) | (vcpu->index % 16));
    set_reg(vcpu, HV_REG_CPSR, PSTATE_EL1H_MASKED);

    /* the boot processor waits for all vCPUs to exist; secondary processors wait for the guest to start them. */
    pthread_mutex_lock(&vm->lock);
    vm->vcpus_created++;
    pthread_cond_broadcast(&vm->vcpu_cond);
    while (!vm->stopping && (vcpu->index == 0 ? vm->vcpus_created < vm->vcpu_count : !vcpu->started))
        pthread_cond_wait(&vm->vcpu_cond, &vm->lock);
    pthread_mutex_unlock(&vm->lock);
    if (vcpu->index == 0) {
        if (!g_profile_vm) {
            pthread_t t;
            g_profile_vm = vm;
            pthread_create(&t, nullptr, profile_main, vm);
            pthread_detach(t);
        }
        set_sys(vcpu, HV_SYS_REG_SP_EL1, vm->boot_stack_top);
        set_reg(vcpu, HV_REG_PC, vm->kernel_entry);
        set_reg(vcpu, HV_REG_X0, MIME_KVA_PHYSMAP + MIME_RAM_BASE + 0x1000);
    } else {
        set_sys(vcpu, HV_SYS_REG_SP_EL1, vcpu->start_sp);
        set_reg(vcpu, HV_REG_PC, vcpu->start_pc);
        set_reg(vcpu, HV_REG_X0, vcpu->start_x0);
    }

    while (!vm->stopping) {
        hv_return_t r = hv_vcpu_run(handle);
        if (r != HV_SUCCESS) {
            mime_error("hv_vcpu_run failed: %#x", r);
            mime_vm_request_stop(vm, 1);
            break;
        }
        vcpu->exits_total++;
        switch (exit->reason) {
        case HV_EXIT_REASON_EXCEPTION:
            if (!handle_exception(vcpu)) {
                mime_daemon_guest_halted(vm, 3);
                mime_vm_request_stop(vm, 3);
            }
            break;
        case HV_EXIT_REASON_VTIMER_ACTIVATED:
            vcpu->exits_vtimer++;
            break;
        case HV_EXIT_REASON_CANCELED:
            vcpu->exits_canceled++;
            if (vcpu->profile_pending)
                profile_record(vcpu);
            if (g_dump_requested) {
                g_dump_requested = false;
                dump_state(vcpu, "state dump requested");
                mime_vk_dump_stats();
            }
            break;
        default:
            mime_error("unexpected vcpu exit reason %d", (int)exit->reason);
            mime_vm_request_stop(vm, 1);
            break;
        }
    }
    hv_vcpu_destroy(handle);
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
