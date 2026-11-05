/*
 * arm64 side of the hal: hvc hypercalls, the generic timer and the gicv3.
 */
#include "../../ntos.h"
#include <mime/hyperv.h>

ULONG64 HalHypercall(ULONG64 nr, ULONG64 a1, ULONG64 a2, ULONG64 a3, ULONG64 a4, ULONG64 a5, ULONG64 a6,
                     PULONG64 value)
{
    register ULONG64 x0 __asm__("x0") = nr;
    register ULONG64 x1 __asm__("x1") = a1;
    register ULONG64 x2 __asm__("x2") = a2;
    register ULONG64 x3 __asm__("x3") = a3;
    register ULONG64 x4 __asm__("x4") = a4;
    register ULONG64 x5 __asm__("x5") = a5;
    register ULONG64 x6 __asm__("x6") = a6;
    __asm__ volatile("hvc #0" : "+r"(x0), "+r"(x1) : "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x6) : "memory");
    if (value)
        *value = x1;
    return x0;
}

ULONG64 HalHyperVCall(ULONG64 input, ULONG64 in, ULONG64 out)
{
    register ULONG64 x0 __asm__("x0") = MIME_HV_SMCCC_HYPERCALL;
    register ULONG64 x1 __asm__("x1") = input;
    register ULONG64 x2 __asm__("x2") = in;
    register ULONG64 x3 __asm__("x3") = out;
    __asm__ volatile("hvc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) :: "memory");
    return x0;
}

/* translates any mapped kernel address to its guest physical address. */
ULONG64 HalPhysicalOf(const void *va)
{
    ULONG_PTR v = (ULONG_PTR)va;
    if (v >= MIME_KVA_PHYSMAP && v < MIME_KVA_PHYSMAP + HalBootInfo->ram_base + HalBootInfo->ram_size)
        return v - MIME_KVA_PHYSMAP;
    if (v >= HalBootInfo->kernel_virt && v < HalBootInfo->kernel_virt + HalBootInfo->kernel_size)
        return HalBootInfo->kernel_phys + (v - HalBootInfo->kernel_virt);
    ULONG64 *root = (ULONG64 *)HalPhysmap(KI_READ_SYSREG(ttbr1_el1) & MM_PTE_ADDR_MASK);
    if (v < MIME_USER_ADDRESS_LIMIT)
        root = (ULONG64 *)HalPhysmap(KI_READ_SYSREG(ttbr0_el1) & MM_PTE_ADDR_MASK);
    ULONG64 *table = root;
    for (int level = 0; level < 4; level++) {
        int shift = 39 - 9 * level;
        ULONG64 e = table[(v >> shift) & 0x1FF];
        if (!(e & MM_PTE_VALID))
            return 0;
        if (level == 3 || !(e & MM_PTE_TABLE))
            return (e & MM_PTE_ADDR_MASK & ~((1ull << shift) - 1)) | (v & ((1ull << shift) - 1));
        table = (ULONG64 *)HalPhysmap(e & MM_PTE_ADDR_MASK);
    }
    return 0;
}

ULONG64 HalReadCounter(void)
{
    ULONG64 v;
    __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v));
    return v;
}

void HalArmTimer(ULONG64 deadline)
{
    KI_WRITE_SYSREG(cntv_cval_el0, deadline);
    KI_WRITE_SYSREG(cntv_ctl_el0, 1);
    KiInstructionBarrier();
}

void HalDisarmTimer(void)
{
    KI_WRITE_SYSREG(cntv_ctl_el0, 2);
}

/* gicv3 cpu interface, by encoding so any assembler accepts them. */
#define ICC_PMR_EL1 s3_0_c4_c6_0
#define ICC_IAR1_EL1 s3_0_c12_c12_0
#define ICC_EOIR1_EL1 s3_0_c12_c12_1
#define ICC_BPR1_EL1 s3_0_c12_c12_3
#define ICC_CTLR_EL1 s3_0_c12_c12_4
#define ICC_SRE_EL1 s3_0_c12_c12_5
#define ICC_IGRPEN1_EL1 s3_0_c12_c12_7
#define ICC_SGI1R_EL1 s3_0_c12_c11_5
#define KI_READ_ICC(name) KI_READ_SYSREG(name)
#define KI_WRITE_ICC(name, v) KI_WRITE_SYSREG(name, v)

#define GICD_CTLR 0x0000
#define GICD_IGROUPR 0x0080
#define GICD_ISENABLER 0x0100
#define GICD_IPRIORITYR 0x0400
#define GICD_ICFGR 0x0C00
#define GICD_IROUTER 0x6000
#define GICD_CTLR_RWP (1u << 31)

#define GICR_FRAME 0x20000
#define GICR_TYPER 0x0008
#define GICR_WAKER 0x0014
#define GICR_SGI 0x10000
#define GICR_IGROUPR0 (GICR_SGI + 0x0080)
#define GICR_ISENABLER0 (GICR_SGI + 0x0100)
#define GICR_IPRIORITYR (GICR_SGI + 0x0400)

static volatile UCHAR *g_gicd;

static ULONG rd32(volatile UCHAR *base, ULONG off)
{
    return *(volatile ULONG *)(base + off);
}

static void wr32(volatile UCHAR *base, ULONG off, ULONG v)
{
    *(volatile ULONG *)(base + off) = v;
}

static volatile UCHAR *find_redistributor(void)
{
    ULONG64 mpidr = KI_READ_SYSREG(mpidr_el1);
    ULONG affinity = (ULONG)((mpidr & 0xFFFFFF) | ((mpidr >> 8) & 0xFF000000));
    volatile UCHAR *base = HalPhysmap(HalBootInfo->gic_redist_phys);
    for (ULONG64 off = 0; off + GICR_FRAME <= HalBootInfo->gic_redist_size; off += GICR_FRAME) {
        ULONG64 typer = *(volatile ULONG64 *)(base + off + GICR_TYPER);
        if ((ULONG)(typer >> 32) == affinity)
            return base + off;
        if (typer & (1u << 4))
            break; /* last redistributor. */
    }
    return base;
}

/* a private interrupt, sgi or ppi, of the running processor. */
static void enable_private(ULONG intid)
{
    volatile UCHAR *gicr = KeGetPcr()->Redistributor;
    wr32(gicr, GICR_IGROUPR0, rd32(gicr, GICR_IGROUPR0) | (1u << intid));
    gicr[GICR_IPRIORITYR + intid] = 0x80;
    wr32(gicr, GICR_ISENABLER0, 1u << intid);
}

/* edge triggered, group 1, routed to the boot cpu. */
static void enable_spi(ULONG intid)
{
    ULONG word = intid / 32, bit = 1u << (intid % 32);
    wr32(g_gicd, GICD_IGROUPR + word * 4, rd32(g_gicd, GICD_IGROUPR + word * 4) | bit);
    g_gicd[GICD_IPRIORITYR + intid] = 0x80;
    ULONG cfg = GICD_ICFGR + (intid / 16) * 4;
    wr32(g_gicd, cfg, rd32(g_gicd, cfg) | (2u << ((intid % 16) * 2)));
    *(volatile ULONG64 *)(g_gicd + GICD_IROUTER + intid * 8) = 0;
    wr32(g_gicd, GICD_ISENABLER + word * 4, bit);
}

void HalInitializeProcessor(void)
{
    volatile UCHAR *gicr = find_redistributor();
    KeGetPcr()->Redistributor = gicr;
    wr32(gicr, GICR_WAKER, rd32(gicr, GICR_WAKER) & ~(1u << 1));
    while (rd32(gicr, GICR_WAKER) & (1u << 2))
        ;

    KI_WRITE_ICC(ICC_SRE_EL1, KI_READ_ICC(ICC_SRE_EL1) | 1);
    KiInstructionBarrier();
    KI_WRITE_ICC(ICC_PMR_EL1, 0xFF);
    KI_WRITE_ICC(ICC_BPR1_EL1, 0);
    KI_WRITE_ICC(ICC_CTLR_EL1, 0); /* eoi both drops priority and deactivates. */
    KI_WRITE_ICC(ICC_IGRPEN1_EL1, 1);
    KiInstructionBarrier();

    /* like windows, user mode reads its processor number here, and the virtual counter backs QueryPerformanceCounter. */
    KI_WRITE_SYSREG(tpidrro_el0, KeGetCurrentPrcb()->Number);
    KI_WRITE_SYSREG(cntkctl_el1, (1u << 1) | (1u << 0));
    KI_WRITE_SYSREG(cntv_ctl_el0, 2);
    enable_private(HalBootInfo->timer_intid);
    enable_private(HAL_IPI_INTID);
}

/* the host sets each processor's affinity to its number, sixteen per cluster. */
void HalSendIpi(ULONG processor)
{
    ULONG64 value = ((ULONG64)HAL_IPI_INTID << 24) | ((ULONG64)(processor / 16) << 16) | (1u << (processor % 16));
    KiDataSyncBarrier();
    KI_WRITE_ICC(ICC_SGI1R_EL1, value);
    KiInstructionBarrier();
}

/* one target list per cluster. */
void HalSendIpiMask(ULONG64 mask)
{
    KiDataSyncBarrier();
    for (ULONG cluster = 0; mask; cluster++, mask >>= 16) {
        ULONG64 bits = mask & 0xFFFF;
        if (bits)
            KI_WRITE_ICC(ICC_SGI1R_EL1, ((ULONG64)HAL_IPI_INTID << 24) | ((ULONG64)cluster << 16) | bits);
    }
    KiInstructionBarrier();
}

static void initialize_gic(void)
{
    g_gicd = HalPhysmap(HalBootInfo->gic_dist_phys);

    /* affinity routing and group 1, then wait for the write to settle. */
    wr32(g_gicd, GICD_CTLR, (1u << 4) | (1u << 1));
    while (rd32(g_gicd, GICD_CTLR) & GICD_CTLR_RWP)
        ;
    HalInitializeProcessor();
    enable_spi(HalBootInfo->irq_intid);
}

ULONG HalAcknowledgeInterrupt(void)
{
    ULONG intid = (ULONG)KI_READ_ICC(ICC_IAR1_EL1) & 0xFFFFFF;
    __asm__ volatile("dsb sy" ::: "memory");
    return intid;
}

void HalEndOfInterrupt(ULONG intid)
{
    KI_WRITE_ICC(ICC_EOIR1_EL1, intid);
    KiInstructionBarrier();
}

void HalInitializeArch(void)
{
    initialize_gic();
}
