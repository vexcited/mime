/*
 * the emulated microsoft hypervisor of arm64 guests: the register, property and flush hypercalls windows
 * and the drivers on it make, answered as a root partition would.
 */
#include "vmm.h"

#include <mime/hyperv.h>
#include <string.h>
#include <time.h>

#ifdef __APPLE__
#include <Hypervisor/Hypervisor.h>
#endif

typedef struct HvVpHeader {
    uint64_t Partition;
    uint32_t Vp;
    uint8_t Vtl;
    uint8_t Rsvd[3];
} HvVpHeader;

typedef struct HvApicHeader {
    uint64_t Partition;
    uint8_t Vtl;
    uint8_t Rsvd[7];
} HvApicHeader;

/* the machine's hypervisor leaves, as HvCallGetVpRegisters returns them and cpuid would show on x64. */
static const uint32_t g_leaves[7][4] = {
    {0x4000000B, 0x7263694D, 0x666F736F, 0x76482074}, /* Microsoft Hv, and the last leaf. */
    {0x31237648, 0, 0, 0},                            /* Hv#1 */
    {26300, 0x000A0000, 0, 9457},                     /* version 10.0 and the update. */
    {0x99F77FFF, 0x19, 0, 0x10},                      /* root privileges, register fast input. */
    {0, 0, 0, 0},                                     /* enlightenments. */
    {64, 64, 0, 0},                                   /* processor limits. */
    {0xF, 0, 0, 0},                                   /* apic overlay, msr bitmaps, counters and slat. */
};

static uint64_t hv_now(MimeVm *vm)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    if (!vm->hv_boot_ns)
        vm->hv_boot_ns = ns;
    return (ns - vm->hv_boot_ns) / 100; /* 100ns units, the time registers count in. */
}

static bool hv_mem(MimeVcpu *vcpu, uint64_t gpa, uint64_t len, void **at)
{
    *at = nullptr;
    if (!len)
        return true;
    if ((gpa & 7) || (gpa & (MIME_PAGE_SIZE - 1)) + len > MIME_PAGE_SIZE)
        return false;
    *at = mime_vm_phys(vcpu->vm, gpa, len);
    return *at != nullptr;
}

static uint64_t hv_vp(MimeVm *vm, uint64_t partition, uint32_t vp, uint8_t vtl, MimeVcpu **target)
{
    if (partition != MIME_HV_PARTITION_SELF && partition != MIME_HV_PARTITION_ROOT)
        return MIME_HV_INVALID_PARTITION_ID;
    if (vp >= vm->vcpu_count)
        return MIME_HV_INVALID_VP_INDEX;
    if (vtl != MIME_HV_VTL_ROOT)
        return MIME_HV_INVALID_PARAMETER;
    *target = &vm->vcpus[vp];
    return MIME_HV_SUCCESS;
}

static uint64_t hv_leaf(unsigned name, uint64_t value[2])
{
    static const struct {
        unsigned Name;
        unsigned Leaf;
    } aliases[] = {
        {MIME_HV_REG_HYPERVISOR_VERSION, 2},
        {MIME_HV_REG_PRIVILEGES_AND_FEATURES, 3},
        {MIME_HV_REG_FEATURES, 4},
        {MIME_HV_REG_IMPLEMENTATION_LIMITS, 5},
        {MIME_HV_REG_HARDWARE_FEATURES, 6},
    };
    for (unsigned i = 0; i < sizeof(aliases) / sizeof(aliases[0]); i++) {
        if (aliases[i].Name != name)
            continue;
        const uint32_t *leaf = g_leaves[aliases[i].Leaf];
        value[0] = (uint64_t)leaf[0] | (uint64_t)leaf[1] << 32;
        value[1] = (uint64_t)leaf[2] | (uint64_t)leaf[3] << 32;
        return MIME_HV_SUCCESS;
    }
    return MIME_HV_INVALID_REGISTER_VALUE;
}

/* reads one register of a target, the values the hypervisor itself defines. */
static uint64_t hv_get_register(MimeVcpu *vcpu, unsigned name, uint64_t value[2])
{
    MimeVm *vm = vcpu->vm;
    value[0] = value[1] = 0;
    switch (name) {
    case MIME_HV_REG_GUEST_OS_ID:
        value[0] = vm->hv_guest_os_id ? vm->hv_guest_os_id : MIME_HV_WINDOWS_OS_ID;
        return MIME_HV_SUCCESS;
    case MIME_HV_REG_VP_INDEX:
        value[0] = vcpu->index;
        return MIME_HV_SUCCESS;
    case MIME_HV_REG_VP_RUNTIME:
    case MIME_HV_REG_TIME_REF_COUNT:
        value[0] = hv_now(vm);
        return MIME_HV_SUCCESS;
    case MIME_HV_REG_CPU_MANAGEMENT_VERSION:
        value[0] = 2;
        return MIME_HV_SUCCESS;
    case MIME_HV_REG_VP_ASSIST_PAGE:
        value[0] = vcpu->hv_assist_page;
        return MIME_HV_SUCCESS;
    case MIME_HV_REG_REFERENCE_TSC:
        value[0] = vcpu->hv_reference_tsc;
        return MIME_HV_SUCCESS;
    case MIME_HV_REG_GUEST_CRASH_CTL:
        value[0] = vcpu->hv_crash_ctl;
        return MIME_HV_SUCCESS;
    case MIME_HV_REG_GUEST_CRASH_P0:
    case MIME_HV_REG_GUEST_CRASH_P1:
    case MIME_HV_REG_GUEST_CRASH_P2:
    case MIME_HV_REG_GUEST_CRASH_P3:
    case MIME_HV_REG_GUEST_CRASH_P4:
        value[0] = vcpu->hv_crash[name - MIME_HV_REG_GUEST_CRASH_P0];
        return MIME_HV_SUCCESS;
    default:
        return hv_leaf(name, value);
    }
}

static uint64_t hv_set_register(MimeVcpu *vcpu, unsigned name, const uint64_t value[2])
{
    MimeVm *vm = vcpu->vm;
    switch (name) {
    case MIME_HV_REG_GUEST_OS_ID:
        vm->hv_guest_os_id = value[0];
        return MIME_HV_SUCCESS;
    case MIME_HV_REG_VP_ASSIST_PAGE:
        vcpu->hv_assist_page = value[0];
        return MIME_HV_SUCCESS;
    case MIME_HV_REG_REFERENCE_TSC:
        vcpu->hv_reference_tsc = value[0];
        return MIME_HV_SUCCESS;
    case MIME_HV_REG_GUEST_CRASH_CTL:
        vcpu->hv_crash_ctl = value[0];
        return MIME_HV_SUCCESS;
    case MIME_HV_REG_GUEST_CRASH_P0:
    case MIME_HV_REG_GUEST_CRASH_P1:
    case MIME_HV_REG_GUEST_CRASH_P2:
    case MIME_HV_REG_GUEST_CRASH_P3:
    case MIME_HV_REG_GUEST_CRASH_P4:
        vcpu->hv_crash[name - MIME_HV_REG_GUEST_CRASH_P0] = value[0];
        return MIME_HV_SUCCESS;
    default:
        return MIME_HV_INVALID_REGISTER_VALUE;
    }
}

/* the registers a partition may touch before it gave the hypervisor its os id. */
static bool hv_early(unsigned name)
{
    switch (name) {
    case MIME_HV_REG_GUEST_OS_ID:
    case MIME_HV_REG_HYPERVISOR_VERSION:
    case MIME_HV_REG_PRIVILEGES_AND_FEATURES:
    case MIME_HV_REG_FEATURES:
    case MIME_HV_REG_IMPLEMENTATION_LIMITS:
    case MIME_HV_REG_HARDWARE_FEATURES:
    case MIME_HV_REG_TIME_REF_COUNT:
    case MIME_HV_REG_GUEST_CRASH_P0:
    case MIME_HV_REG_GUEST_CRASH_P1:
    case MIME_HV_REG_GUEST_CRASH_P2:
    case MIME_HV_REG_GUEST_CRASH_P3:
    case MIME_HV_REG_GUEST_CRASH_P4:
    case MIME_HV_REG_GUEST_CRASH_CTL:
        return true;
    default:
        return false;
    }
}

static bool hv_identified(MimeVm *vm)
{
#ifdef __linux__
    (void)vm;
    return true; /* the os id went through the hypervisor's MSR, which kvm keeps. */
#else
    return vm->hv_guest_os_id != 0;
#endif
}

static uint64_t hv_property(MimeVm *vm, unsigned property, uint64_t *value)
{
    switch (property) {
    case MIME_HV_PROPERTY_PRIVILEGE_FLAGS:
        *value = MIME_HV_PRIVILEGE_FLAGS | 0x19ull << 32;
        return MIME_HV_SUCCESS;
    case MIME_HV_PROPERTY_SYNTHETIC_PROC_FEATURES:
        *value = 0;
        return MIME_HV_SUCCESS;
    case MIME_HV_PROPERTY_REFERENCE_TIME:
        *value = hv_now(vm);
        return MIME_HV_SUCCESS;
    case MIME_HV_PROPERTY_ISOLATION_STATE:
        *value = 0;
        return MIME_HV_SUCCESS;
    case MIME_HV_PROPERTY_PHYSICAL_ADDRESS_BITS:
    case MIME_HV_PROPERTY_ADDRESS_WIDTH:
        *value = 40;
        return MIME_HV_SUCCESS;
    case MIME_HV_PROPERTY_PROCESSOR_VENDOR: {
        static const char vendor[8] = {'Q', 'u', 'a', 'l', 'c', 'o', 'm', 'm'};
        memcpy(value, vendor, sizeof(vendor));
        return MIME_HV_SUCCESS;
    }
    case MIME_HV_PROPERTY_COMPATIBILITY_VERSION:
        *value = 0;
        return MIME_HV_SUCCESS;
    case MIME_HV_PROPERTY_GUEST_OS_ID:
        *value = vm->hv_guest_os_id ? vm->hv_guest_os_id : MIME_HV_WINDOWS_OS_ID;
        return MIME_HV_SUCCESS;
    default:
        return MIME_HV_UNKNOWN_PROPERTY;
    }
}

static uint64_t hv_set_property(MimeVm *vm, unsigned property, uint64_t value)
{
    switch (property) {
    case MIME_HV_PROPERTY_GUEST_OS_ID:
        vm->hv_guest_os_id = value;
        return MIME_HV_SUCCESS;
    case MIME_HV_PROPERTY_PRIVILEGE_FLAGS:
        return MIME_HV_SUCCESS;
    case MIME_HV_PROPERTY_REFERENCE_TIME:
        return MIME_HV_SUCCESS;
    default:
        return MIME_HV_UNKNOWN_PROPERTY;
    }
}

#ifdef __APPLE__
static uint64_t hv_translate(MimeVcpu *vcpu, uint64_t va, uint64_t result[2])
{
    uint64_t ttbr = va < MIME_USER_ADDRESS_LIMIT ? mime_vcpu_sysreg(vcpu, HV_SYS_REG_TTBR0_EL1)
                                                 : mime_vcpu_sysreg(vcpu, HV_SYS_REG_TTBR1_EL1);
    uint64_t table = ttbr & 0x0000FFFFFFFFF000ull;
    result[0] = 1; /* not present. */
    result[1] = 0;
    for (int level = 0; level < 4; level++) {
        int shift = 39 - 9 * level;
        uint64_t *entry = mime_vm_phys(vcpu->vm, table + ((va >> shift) & 0x1FF) * 8, 8);
        if (!entry || !(*entry & 1))
            return MIME_HV_SUCCESS;
        if (level == 3 || !(*entry & 2)) {
            uint64_t pa = (*entry & 0x0000FFFFFFFFF000ull & ~((1ull << shift) - 1)) |
                          (va & ((1ull << shift) - 1));
            result[0] = 0;
            result[1] = pa >> 12;
            return MIME_HV_SUCCESS;
        }
        table = *entry & 0x0000FFFFFFFFF000ull;
    }
    return MIME_HV_SUCCESS;
}
#endif

static bool hv_rep(unsigned call)
{
    switch (call) {
    case MIME_HV_CALL_FLUSH_VIRTUAL_ADDRESS_LIST:
    case MIME_HV_CALL_FLUSH_VIRTUAL_ADDRESS_LIST_EX:
    case MIME_HV_CALL_GET_VP_REGISTERS:
    case MIME_HV_CALL_SET_VP_REGISTERS:
    case MIME_HV_CALL_GET_VP_INDEX_FROM_APIC_ID:
    case MIME_HV_CALL_FLUSH_GPA_LIST:
        return true;
    default:
        return false;
    }
}

void mime_hv_dispatch(MimeVcpu *vcpu, bool legacy, uint64_t regs[18])
{
    MimeVm *vm = vcpu->vm;
    uint64_t input = regs[legacy ? 0 : 1];
    uint64_t in_gpa = regs[legacy ? 1 : 2];
    uint64_t out_gpa = regs[legacy ? 2 : 3];
    uint64_t *param = &regs[legacy ? 1 : 2];
    unsigned call = (unsigned)(input & 0xFFFF);
    uint32_t count = (uint32_t)MIME_HV_REP_COUNT(input);
    uint32_t start = (uint32_t)MIME_HV_REP_START(input);
    bool fast = input & MIME_HV_FAST;
    uint64_t result = MIME_HV_INVALID_HYPERCALL_INPUT;
    void *in = nullptr, *out = nullptr;

    hv_now(vm); /* the reference time starts at the partition's first hypercall. */
    /* the reserved bits of the input value, and the rep fields the call does not take. */
    if (input & 0x78000000ull || input & 0x0000F00000000000ull || input & 0xF000000000000000ull)
        goto done;
    if (hv_rep(call) ? count <= start : (count || start))
        goto done;

    switch (call) {
    case MIME_HV_CALL_GET_VP_REGISTERS: {
        if (fast || count == 0)
            break;
        uint64_t size = sizeof(HvVpHeader) + (uint64_t)count * 16;
        if (!hv_mem(vcpu, in_gpa, size, &in) || !hv_mem(vcpu, out_gpa, (uint64_t)count * 16, &out)) {
            result = MIME_HV_INVALID_ALIGNMENT;
            break;
        }
        HvVpHeader *header = in;
        MimeVcpu *target;
        result = hv_vp(vm, header->Partition, header->Vp, header->Vtl, &target);
        if (result)
            break;
        uint32_t done = 0;
        for (uint32_t i = start; i < count; i++) {
            unsigned name = *(uint32_t *)((uint8_t *)in + sizeof(*header) + (uint64_t)i * 16);
            if (!hv_identified(vm) && (vcpu->index != 0 || !hv_early(name))) {
                result = MIME_HV_ACCESS_DENIED;
                break;
            }
            uint64_t value[2];
            result = hv_get_register(target, name, value);
            done = i + 1;
            if (result)
                break;
            memcpy((uint8_t *)out + (uint64_t)i * 16, value, sizeof(value));
        }
        if (MIME_HV_STATUS(result) == MIME_HV_SUCCESS)
            result = MIME_HV_RESULT(0, count);
        else
            result = MIME_HV_RESULT(MIME_HV_STATUS(result), done);
        break;
    }
    case MIME_HV_CALL_SET_VP_REGISTERS: {
        if (fast || count == 0)
            break;
        uint64_t size = sizeof(HvVpHeader) + (uint64_t)count * 32;
        if (!hv_mem(vcpu, in_gpa, size, &in)) {
            result = MIME_HV_INVALID_ALIGNMENT;
            break;
        }
        HvVpHeader *header = in;
        MimeVcpu *target;
        result = hv_vp(vm, header->Partition, header->Vp, header->Vtl, &target);
        if (result)
            break;
        uint32_t done = 0;
        for (uint32_t i = start; i < count; i++) {
            uint8_t *element = (uint8_t *)in + sizeof(*header) + (uint64_t)i * 32;
            unsigned name = *(uint32_t *)element;
            if (!hv_identified(vm) && (vcpu->index != 0 || !hv_early(name))) {
                result = MIME_HV_ACCESS_DENIED;
                break;
            }
            result = hv_set_register(target, name, (const uint64_t *)(element + 16));
            done = i + 1;
            if (result)
                break;
        }
        if (MIME_HV_STATUS(result) == MIME_HV_SUCCESS)
            result = MIME_HV_RESULT(0, count);
        else
            result = MIME_HV_RESULT(MIME_HV_STATUS(result), done);
        break;
    }
    case MIME_HV_CALL_GET_PARTITION_PROPERTY: {
        if (fast)
            break;
        uint64_t *p;
        if (!hv_mem(vcpu, in_gpa, 16, &in)) {
            result = MIME_HV_INVALID_ALIGNMENT;
            break;
        }
        p = in;
        if (!hv_mem(vcpu, out_gpa, 8, &out)) {
            result = MIME_HV_INVALID_ALIGNMENT;
            break;
        }
        if (!hv_identified(vm)) {
            result = MIME_HV_ACCESS_DENIED;
            break;
        }
        if ((p[0] != MIME_HV_PARTITION_SELF && p[0] != MIME_HV_PARTITION_ROOT) || (uint32_t)(p[1] >> 32)) {
            result = MIME_HV_INVALID_PARAMETER;
            break;
        }
        result = hv_property(vm, (unsigned)p[1], out);
        break;
    }
    case MIME_HV_CALL_SET_PARTITION_PROPERTY: {
        uint64_t *p = param;
        if (!fast && !hv_mem(vcpu, in_gpa, 24, &in)) {
            result = MIME_HV_INVALID_ALIGNMENT;
            break;
        }
        if (!fast)
            p = in;
        if (!hv_identified(vm)) {
            result = MIME_HV_ACCESS_DENIED;
            break;
        }
        if (p[0] != MIME_HV_PARTITION_SELF && p[0] != MIME_HV_PARTITION_ROOT) {
            result = MIME_HV_INVALID_PARAMETER;
            break;
        }
        result = hv_set_property(vm, (unsigned)p[1], p[2]);
        break;
    }
    case MIME_HV_CALL_GET_PARTITION_ID: {
        if (!hv_mem(vcpu, out_gpa, 8, &out)) {
            result = MIME_HV_INVALID_ALIGNMENT;
            break;
        }
        *(uint64_t *)out = MIME_HV_PARTITION_ROOT;
        result = MIME_HV_SUCCESS;
        break;
    }
    case MIME_HV_CALL_TRANSLATE_VIRTUAL_ADDRESS: {
        if (fast)
            break;
        uint64_t *p;
        if (!hv_mem(vcpu, in_gpa, 32, &in)) {
            result = MIME_HV_INVALID_ALIGNMENT;
            break;
        }
        p = in;
        if (!hv_mem(vcpu, out_gpa, 16, &out)) {
            result = MIME_HV_INVALID_ALIGNMENT;
            break;
        }
        if (p[0] != MIME_HV_PARTITION_SELF && p[0] != MIME_HV_PARTITION_ROOT) {
            result = MIME_HV_INVALID_PARAMETER;
            break;
        }
#ifdef __APPLE__
        result = hv_translate(&vm->vcpus[p[1] < vm->vcpu_count ? p[1] : 0], p[3], out);
#else
        ((uint64_t *)out)[0] = 1;
        ((uint64_t *)out)[1] = 0;
        result = MIME_HV_SUCCESS;
#endif
        break;
    }
    case MIME_HV_CALL_GET_VP_INDEX_FROM_APIC_ID: {
        if (fast || count == 0)
            break;
        uint64_t size = sizeof(HvApicHeader) + (uint64_t)count * 8;
        if (!hv_mem(vcpu, in_gpa, size, &in) || !hv_mem(vcpu, out_gpa, (uint64_t)count * 8, &out)) {
            result = MIME_HV_INVALID_ALIGNMENT;
            break;
        }
        HvApicHeader *header = in;
        if (header->Partition != MIME_HV_PARTITION_SELF && header->Partition != MIME_HV_PARTITION_ROOT) {
            result = MIME_HV_INVALID_PARAMETER;
            break;
        }
        result = MIME_HV_SUCCESS;
        for (uint32_t i = start; i < count; i++) {
            uint64_t id = *(uint64_t *)((uint8_t *)in + sizeof(*header) + (uint64_t)i * 8);
            uint64_t index = id & 0xFF;
            if (index >= vm->vcpu_count) {
                result = MIME_HV_RESULT(MIME_HV_INVALID_PARAMETER, i);
                break;
            }
            memcpy((uint8_t *)out + (uint64_t)i * 8, &index, sizeof(index));
        }
        if (MIME_HV_STATUS(result) != MIME_HV_SUCCESS)
            break;
        result = MIME_HV_RESULT(0, count);
        break;
    }
    case MIME_HV_CALL_FLUSH_VIRTUAL_ADDRESS_SPACE:
    case MIME_HV_CALL_FLUSH_VIRTUAL_ADDRESS_SPACE_EX:
    case MIME_HV_CALL_FLUSH_GPA_SPACE:
    case MIME_HV_CALL_SWITCH_VIRTUAL_ADDRESS_SPACE:
    case MIME_HV_CALL_NOTIFY_LONG_SPIN_WAIT:
    case MIME_HV_CALL_SEND_SYNTHETIC_CLUSTER_IPI:
    case MIME_HV_CALL_SEND_SYNTHETIC_CLUSTER_IPI_EX:
    case MIME_HV_CALL_CLEAR_VIRTUAL_INTERRUPT:
    case MIME_HV_CALL_ASSERT_VIRTUAL_INTERRUPT:
    case MIME_HV_CALL_START_VP:
    case MIME_HV_CALL_DISABLE_HYP_EX:
    case MIME_HV_CALL_ENABLE_PARTITION_VTL:
    case MIME_HV_CALL_ENABLE_VP_VTL:
    case MIME_HV_CALL_VTL_CALL:
    case MIME_HV_CALL_VTL_RETURN:
        result = MIME_HV_SUCCESS;
        break;
    case MIME_HV_CALL_FLUSH_VIRTUAL_ADDRESS_LIST:
    case MIME_HV_CALL_FLUSH_VIRTUAL_ADDRESS_LIST_EX:
    case MIME_HV_CALL_FLUSH_GPA_LIST: {
        uint64_t size = 32 + (uint64_t)count * 8;
        if (fast || count == 0)
            break;
        if (!hv_mem(vcpu, in_gpa, size, &in)) {
            result = MIME_HV_INVALID_ALIGNMENT;
            break;
        }
        result = MIME_HV_RESULT(0, count);
        break;
    }
    case MIME_HV_CALL_POST_MESSAGE:
    case MIME_HV_CALL_SIGNAL_EVENT:
        result = MIME_HV_INVALID_CONNECTION_ID;
        break;
    default:
        result = MIME_HV_INVALID_HYPERCALL_CODE;
        break;
    }
done:
    regs[0] = result;
}
