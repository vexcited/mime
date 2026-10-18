/*
 * x64 side of the hal: port hypercalls, the tsc, the x2apic and its timer
 * in tsc deadline mode.
 */
#include "../../ntos.h"

/* rax = number, rdi rsi rdx r10 r8 r9 = arguments; rax = status, rdx = value. */
ULONG64 HalHypercall(ULONG64 nr, ULONG64 a1, ULONG64 a2, ULONG64 a3, ULONG64 a4, ULONG64 a5, ULONG64 a6,
                     PULONG64 value)
{
    register ULONG64 r10 __asm__("r10") = a4;
    register ULONG64 r8 __asm__("r8") = a5;
    register ULONG64 r9 __asm__("r9") = a6;
    ULONG64 rdx = a3;
    __asm__ volatile("outb %%al, %[port]"
                     : "+a"(nr), "+d"(rdx)
                     : [port] "i"(MIME_HC_PORT), "D"(a1), "S"(a2), "r"(r10), "r"(r8), "r"(r9)
                     : "memory");
    if (value)
        *value = rdx;
    return nr;
}

/* translates any mapped kernel address to its guest physical address. */
ULONG64 HalPhysicalOf(const void *va)
{
    ULONG_PTR v = (ULONG_PTR)va;
    if (v >= MIME_KVA_PHYSMAP && v < MIME_KVA_PHYSMAP + HalBootInfo->ram_base + HalBootInfo->ram_size)
        return v - MIME_KVA_PHYSMAP;
    if (v >= HalBootInfo->kernel_virt && v < HalBootInfo->kernel_virt + HalBootInfo->kernel_size)
        return HalBootInfo->kernel_phys + (v - HalBootInfo->kernel_virt);
    ULONG64 *table = (ULONG64 *)HalPhysmap(KiReadCr3() & MM_PTE_ADDR_MASK);
    for (int level = 0; level < 4; level++) {
        int shift = 39 - 9 * level;
        ULONG64 e = table[(v >> shift) & 0x1FF];
        if (!(e & MM_PTE_VALID))
            return 0;
        if (!MI_PTE_IS_TABLE(e, level))
            return (e & MM_PTE_ADDR_MASK & ~((1ull << shift) - 1)) | (v & ((1ull << shift) - 1));
        table = (ULONG64 *)HalPhysmap(e & MM_PTE_ADDR_MASK);
    }
    return 0;
}

ULONG64 HalReadCounter(void)
{
    ULONG lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return (ULONG64)hi << 32 | lo;
}

void HalArmTimer(ULONG64 deadline)
{
    /* zero disarms the timer, a deadline already past fires at once. */
    KiWriteMsr(MSR_TSC_DEADLINE, deadline ? deadline : 1);
}

void HalDisarmTimer(void)
{
    KiWriteMsr(MSR_TSC_DEADLINE, 0);
}

/* the apic ids are the processor numbers, which the host gives its vcpus. */
void HalSendIpi(ULONG processor)
{
    KiWriteMsr(MSR_X2APIC_ICR, (ULONG64)processor << 32 | KI_VECTOR_IPI);
}

/*
 * one interrupt to a set of processors: logical destination mode reaches the
 * sixteen processors of a cluster with a single write, where a write for
 * each would cost the sender an exit to the host for each.
 */
static void send_mask(ULONG64 mask, ULONG vector)
{
    for (ULONG cluster = 0; mask; cluster++, mask >>= 16) {
        ULONG bits = (ULONG)(mask & 0xFFFF);
        if (bits)
            KiWriteMsr(MSR_X2APIC_ICR, (ULONG64)(cluster << 16 | bits) << 32 | (1u << 11) | vector);
    }
}

void HalSendIpiMask(ULONG64 mask)
{
    send_mask(mask, KI_VECTOR_IPI);
}

/* the request that processors flush their user translations, see KiFlushIpi. */
void HalSendFlushIpi(ULONG64 mask)
{
    send_mask(mask, KI_VECTOR_FLUSH);
}

void HalEndOfInterrupt(ULONG vector)
{
    KiWriteMsr(MSR_X2APIC_EOI, 0);
}

/* the local apic of the running processor, in x2apic mode with the timer in tsc deadline mode. */
void HalInitializeProcessor(void)
{
    KiWriteMsr(MSR_APIC_BASE, KiReadMsr(MSR_APIC_BASE) | (1ull << 11) | (1ull << 10));
    KeGetPcr()->ApicId = (ULONG)KiReadMsr(MSR_X2APIC_ID);
    KiWriteMsr(MSR_X2APIC_TPR, 0);
    KiWriteMsr(MSR_X2APIC_SVR, 0x100 | KI_VECTOR_SPURIOUS);
    KiWriteMsr(MSR_X2APIC_LVT_TIMER, (2u << 17) | KI_VECTOR_CLOCK);
    KiWriteMsr(MSR_TSC_DEADLINE, 0);
    /* user mode reads its processor number with rdtscp or rdpid, as on windows. */
    KiWriteMsr(MSR_TSC_AUX, KeGetCurrentPrcb()->Number);
}

void HalInitializeArch(void)
{
    HalInitializeProcessor();
}
