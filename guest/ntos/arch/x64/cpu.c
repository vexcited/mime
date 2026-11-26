/*
 * processor identification from cpuid, for the processor features, SYSTEM_INFO
 * and the CentralProcessor key. the guest sees the host's own processor.
 */
#include "../../ntos.h"
#include "../../cm/cm.h"

static ULONG g_leaf1[4], g_leaf7[4], g_ext1[4], g_max_ext;
static char g_vendor[13], g_brand[49];
static ULONG64 g_hypercall_page;

void KiReadProcessorIds(void)
{
    ULONG regs[4];
    KiCpuid(0, 0, regs);
    memcpy(g_vendor, &regs[1], 4);
    memcpy(g_vendor + 4, &regs[3], 4);
    memcpy(g_vendor + 8, &regs[2], 4);
    g_vendor[12] = 0;
    ULONG max = regs[0];
    KiCpuid(1, 0, g_leaf1);
    if (max >= 7)
        KiCpuid(7, 0, g_leaf7);
    KiCpuid(0x80000000, 0, regs);
    g_max_ext = regs[0];
    if (g_max_ext >= 0x80000001)
        KiCpuid(0x80000001, 0, g_ext1);
    for (ULONG i = 0; i < 3 && g_max_ext >= 0x80000004; i++) {
        KiCpuid(0x80000002 + i, 0, regs);
        memcpy(g_brand + i * 16, regs, 16);
    }
}

/* the 48 characters of the brand leaves as they are, the spaces an amd processor pads them with included. */
PCSTR KiProcessorBrand(void)
{
    return g_brand[0] ? g_brand : HalBootInfo->cpu_name;
}

/*
 * the speed of the processor in megahertz. windows measures the time stamp counter at boot and tells its frequency
 * in ~MHz and in the cycle counts, the nominal one a program finds again by timing rdtsc, not the peak of the
 * model: measured on windows 11, 3094 for a counter of 3094 MHz on a processor sold as 4.5 GHz.
 */
ULONG KiProcessorMhz(void)
{
    if (HalBootInfo->timer_frequency > 100000000)
        return (ULONG)((HalBootInfo->timer_frequency + 500000) / 1000000);
    return HalBootInfo->cpu_mhz;
}

/* the hypervisor leaves from 0x40000000 when cpuid reports microsoft's, FALSE otherwise. */
BOOLEAN KiQueryHypervisorLeaves(ULONG leaves[][4], ULONG count)
{
    ULONG regs[4];
    if (!(g_leaf1[2] >> 31 & 1))
        return FALSE;
    KiCpuid(0x40000000, 0, regs);
    if (memcmp(&regs[1], "Micr", 4) || memcmp(&regs[2], "osof", 4) || memcmp(&regs[3], "t Hv", 4))
        return FALSE;
    for (ULONG i = 0; i < count; i++)
        KiCpuid(0x40000000 + i, 0, leaves[i]);
    return TRUE;
}

void KiPrepareHypervisor(void)
{
    ULONG leaves[1][4];
    if (!KiQueryHypervisorLeaves(leaves, 1))
        return;
    KiWriteMsr(0x40000000, 0x0001040A00000000ull | MIME_NT_BUILD_NUMBER);
    if (g_hypercall_page)
        KiWriteMsr(0x40000001, g_hypercall_page | 1);
}

/*
 * on microsoft's hypervisor windows identifies itself through the guest os id
 * register, which enables hypercalls: microsoft, windows nt, 10.0 and the build.
 */
void KiIdentifyToHypervisor(void)
{
    ULONG leaves[1][4];
    if (!KiQueryHypervisorLeaves(leaves, 1))
        return;
    KiPrepareHypervisor();
    /* the hypercall page, executable through the system address space, which the hypervisor writes the stub in. */
    ULONG64 pa = MmAllocateContiguous(1);
    PVOID va = pa ? MmAllocateSystemVa(1) : nullptr;
    if (!va)
        return;
    MiMapPage(MiKernelRoot(), (ULONG_PTR)va, pa, MM_PTE_ACCESSED | MM_PTE_DIRTY);
    g_hypercall_page = pa;
    KiWriteMsr(0x40000001, pa | 1);
}

static BOOLEAN bit(const ULONG *regs, int reg, int n)
{
    return (regs[reg] >> n) & 1;
}

/* sets the PF_* features of KUSER_SHARED_DATA the way windows derives them. */
void KiSetProcessorFeatures(BOOLEAN *features)
{
    const ULONG *l1 = g_leaf1, *l7 = g_leaf7, *e1 = g_ext1;
    features[1] = FALSE;                       /* PF_FLOATING_POINT_EMULATED */
    features[2] = TRUE;                        /* PF_COMPARE_EXCHANGE_DOUBLE */
    features[3] = bit(l1, 3, 23);              /* PF_MMX_INSTRUCTIONS_AVAILABLE */
    features[6] = bit(l1, 3, 25);              /* PF_XMMI_INSTRUCTIONS_AVAILABLE */
    features[7] = bit(e1, 3, 31);              /* PF_3DNOW_INSTRUCTIONS_AVAILABLE */
    features[8] = TRUE;                        /* PF_RDTSC_INSTRUCTION_AVAILABLE */
    features[9] = TRUE;                        /* PF_PAE_ENABLED */
    features[10] = bit(l1, 3, 26);             /* PF_XMMI64_INSTRUCTIONS_AVAILABLE */
    features[12] = TRUE;                       /* PF_NX_ENABLED */
    features[13] = bit(l1, 2, 0);              /* PF_SSE3_INSTRUCTIONS_AVAILABLE */
    features[14] = bit(l1, 2, 13);             /* PF_COMPARE_EXCHANGE128 */
    features[17] = bit(l1, 2, 26);             /* PF_XSAVE_ENABLED */
    features[20] = TRUE;                       /* PF_SECOND_LEVEL_ADDRESS_TRANSLATION */
    features[21] = KiVirtualizationFlags() & 1; /* PF_VIRT_FIRMWARE_ENABLED */
    features[22] = bit(l7, 1, 0);              /* PF_RDWRFSGSBASE_AVAILABLE */
    features[23] = TRUE;                       /* PF_FASTFAIL_AVAILABLE */
    features[28] = bit(l1, 2, 30);             /* PF_RDRAND_INSTRUCTION_AVAILABLE */
    features[32] = bit(e1, 3, 27);             /* PF_RDTSCP_INSTRUCTION_AVAILABLE */
    features[33] = bit(l7, 2, 22);             /* PF_RDPID_INSTRUCTION_AVAILABLE */
    features[36] = bit(l1, 2, 9);              /* PF_SSSE3_INSTRUCTIONS_AVAILABLE */
    features[37] = bit(l1, 2, 19);             /* PF_SSE4_1_INSTRUCTIONS_AVAILABLE */
    features[38] = bit(l1, 2, 20);             /* PF_SSE4_2_INSTRUCTIONS_AVAILABLE */
    features[39] = bit(l1, 2, 28) && (KiXsaveMask & 4);  /* PF_AVX_INSTRUCTIONS_AVAILABLE */
    features[40] = bit(l7, 1, 5) && (KiXsaveMask & 4);   /* PF_AVX2_INSTRUCTIONS_AVAILABLE */
    features[41] = bit(l7, 1, 16) && (KiXsaveMask & 0xE0); /* PF_AVX512F_INSTRUCTIONS_AVAILABLE */
    features[42] = bit(l7, 1, 9);              /* PF_ERMS_AVAILABLE */
    features[60] = bit(l7, 1, 8);              /* PF_BMI2_INSTRUCTIONS_AVAILABLE */
    features[61] = bit(l7, 2, 28);             /* PF_MOVDIR64B_INSTRUCTION_AVAILABLE */
}

/*
 * the VirtualizationFlags byte of KUSER_SHARED_DATA, as the kernel of windows 26300 fills it: on amd bit 0 when
 * VM_CR leaves svm enabled, on intel bit 0 when IA32_FEATURE_CONTROL enables vmx outside smx and bit 1 when it is
 * locked. bit 0 is also what PF_VIRT_FIRMWARE_ENABLED reports.
 */
UCHAR KiVirtualizationFlags(void)
{
    UCHAR flags = 0;
    if (!strcmp(g_vendor, "AuthenticAMD")) {
        if (!(KiReadMsr(0xC0010114) & 0x10))
            flags |= 1;
    } else if (!strcmp(g_vendor, "GenuineIntel") && bit(g_leaf1, 2, 5)) {
        ULONG64 control = KiReadMsr(0x3A);
        if (control & 4)
            flags |= 1;
        if (control & 1)
            flags |= 2;
    }
    return flags;
}

/*
 * CyclesPerYield as windows measures it: the tsc across 256 pauses with interrupts off, divided by 256, between 1
 * and 0xFFFF. windows measures once; the shortest of a few runs keeps a preempted virtual processor out of it.
 */
USHORT KiMeasureCyclesPerYield(void)
{
    ULONG64 best = ~0ull;
    for (int run = 0; run < 8; run++) {
        ULONG64 flags = KiDisableInterrupts();
        ULONG64 start = __builtin_ia32_rdtsc();
        for (int i = 0; i < 256; i++)
            KiPause();
        ULONG64 cycles = __builtin_ia32_rdtsc() - start;
        KiRestoreInterrupts(flags);
        if (cycles < best)
            best = cycles;
    }
    best >>= 8;
    return best > 0xFFFF ? 0xFFFF : best ? (USHORT)best : 1;
}

PCSTR KiProcessorVendor(void)
{
    return g_vendor;
}

static ULONG family(void)
{
    ULONG f = (g_leaf1[0] >> 8) & 0xF;
    return f == 0xF ? f + ((g_leaf1[0] >> 20) & 0xFF) : f;
}

/* the processor level windows reports is the family. */
USHORT KiProcessorLevel(void)
{
    return (USHORT)family();
}

USHORT KiProcessorModel(void)
{
    ULONG f = (g_leaf1[0] >> 8) & 0xF;
    ULONG model = (g_leaf1[0] >> 4) & 0xF;
    if (f == 6 || f == 0xF)
        model |= ((g_leaf1[0] >> 16) & 0xF) << 4;
    return (USHORT)model;
}

/* model in the high byte, stepping in the low one. */
USHORT KiProcessorRevision(void)
{
    return (USHORT)(KiProcessorModel() << 8 | (g_leaf1[0] & 0xF));
}

/*
 * the KeFeatureBits low dword SYSTEM_PROCESSOR_INFORMATION carries, the KF_ flags windows derives from cpuid.
 * calibrated on windows 11 26300 with the same cpuid, which answers 0xBCBBBDFF.
 */
ULONG KiProcessorFeatureBits(void)
{
    /* v86, rdtsc, cr4, cmov, global and large pages, mtrr, cmpxchg8b, and the fast system call of long mode. */
    ULONG bits = 0x000010FF;
    const ULONG *l1 = g_leaf1, *l7 = g_leaf7;
    ULONG regs[4] = {};
    if (bit(l1, 3, 23)) bits |= 0x00000100; /* KF_MMX */
    if (bit(l1, 3, 16)) bits |= 0x00000400; /* KF_PAT */
    if (bit(l1, 3, 24)) bits |= 0x00000800; /* KF_FXSR */
    if (bit(l1, 3, 25)) bits |= 0x00002000; /* KF_XMMI */
    if (bit(l1, 2, 26)) {
        bits |= 0x00800000;                 /* KF_XSTATE */
        KiCpuid(0xD, 1, regs);
        if (regs[0] & 1) bits |= 0x00008000; /* KF_XSAVEOPT */
    }
    if (bit(l1, 3, 26)) bits |= 0x00010000; /* KF_XMMI64 */
    bits |= 0x00020000;                     /* KF_BRANCH */
    if (bit(l1, 2, 0))  bits |= 0x00080000; /* KF_SSE3 */
    if (bit(l1, 2, 13)) bits |= 0x00100000; /* KF_CMPXCHG16B */
    if (!strcmp(g_vendor, "AuthenticAMD")) bits |= 0x00200000;
    if (!strcmp(g_vendor, "GenuineIntel")) bits |= 0x01000000;
    KiCpuid(0, 0, regs);
    if (regs[0] >= 6) {
        KiCpuid(6, 0, regs);
        if (regs[2] & 1) bits |= 0x00400000; /* KF_ACNT2, the aperf and mperf counters */
    }
    bits |= 0x0C000000;                     /* KF_SLAT and KF_VIRT_FIRMWARE_ENABLED, set on the reference machine */
    if (bit(l7, 1, 0))  bits |= 0x10000000; /* KF_RDWRFSGSBASE */
    bits |= 0xA0000000;                     /* KF_NX_BIT and KF_NX_ENABLED */
    return bits;
}

void KiFormatProcessorIdentifier(PWCHAR text, ULONG chars)
{
    BOOLEAN intel = !strcmp(g_vendor, "GenuineIntel");
    _snwprintf(text, chars, L"%ls Family %u Model %u Stepping %u", intel ? L"Intel64" : L"AMD64", family(),
               KiProcessorModel(), g_leaf1[0] & 0xF);
}

/* the microcode revision and feature set windows keeps beside the processor name. */
void KiSetProcessorRegistryValues(PCWSTR key, KI_SET_REGISTRY_VALUE set)
{
    ULONG feature_set = 0x3D193FFF; /* the bits windows sets for a current processor */
    set(key, L"FeatureSet", REG_DWORD, &feature_set, sizeof(feature_set));
    ULONG64 update = 0;
    set(key, L"Update Revision", REG_BINARY, &update, sizeof(update));
    ULONG platform = 0;
    set(key, L"Platform ID", REG_DWORD, &platform, sizeof(platform));
}

UCHAR KiProcessorSmbiosType(void)
{
    /* for the smbios the kernel builds when the firmware's own was not read: the family by the marketing name. */
    PCSTR name = KiProcessorBrand();
    if (strstr(name, "i3"))
        return 0xCE;
    if (strstr(name, "i5"))
        return 0xCD;
    if (strstr(name, "i9"))
        return 0xCF;
    if (strstr(name, "i7"))
        return 0xC6;
    return 0x6B; /* the zen family */
}

PCSTR KiProcessorSmbiosManufacturer(void)
{
    return strcmp(g_vendor, "GenuineIntel") ? "Advanced Micro Devices, Inc." : "Intel(R) Corporation";
}

/* the signature and feature flags of cpuid leaf 1, as the smbios processor id holds them. */
ULONG64 KiProcessorSmbiosId(void)
{
    return (ULONG64)g_leaf1[3] << 32 | g_leaf1[0];
}

/* the associativity the l2 and l3 fields of leaf 0x80000006 encode. */
static UCHAR legacy_associativity(ULONG code)
{
    static const UCHAR ways[16] = {0, 1, 2, 3, 4, 6, 8, 0, 16, 0, 32, 48, 64, 96, 128, 0xFF};
    return ways[code & 15];
}

static ULONG deterministic_caches(ULONG leaf, KI_CACHE *caches, ULONG room)
{
    ULONG n = 0, regs[4];
    for (ULONG i = 0; n < room; i++) {
        KiCpuid(leaf, i, regs);
        ULONG type = regs[0] & 31;
        if (!type)
            break;
        ULONG line = (regs[1] & 0xFFF) + 1, partitions = (regs[1] >> 12 & 0x3FF) + 1, ways = (regs[1] >> 22) + 1;
        caches[n].Level = (UCHAR)(regs[0] >> 5 & 7);
        caches[n].Associativity = (regs[0] & 0x200) || ways > 0xFF ? 0xFF : (UCHAR)ways;
        caches[n].LineSize = (USHORT)line;
        caches[n].Size = ways * partitions * line * (regs[2] + 1);
        caches[n].Type = type == 1 ? 2 : type == 2 ? 1 : 0;
        /* as windows reads it: a cache whose sharing field reaches other processors' ids is theirs too. */
        ULONG sharing = regs[0] >> 14 & 0xFFF;
        caches[n].Shared = sharing != 0;
        caches[n].ShareBits = 0;
        while ((1u << caches[n].ShareBits) <= sharing)
            caches[n].ShareBits++;
        n++;
    }
    return n;
}

/* the thread level of leaf 0xB: how many low bits of the apic id tell the threads of one core apart. */
ULONG KiProcessorSmtBits(void)
{
    ULONG regs[4];
    KiCpuid(0, 0, regs);
    if (regs[0] < 0xB)
        return 0;
    KiCpuid(0xB, 0, regs);
    return (regs[1] & 0xFFFF) && (regs[2] >> 8 & 0xFF) == 1 ? regs[0] & 0x1F : 0;
}

/*
 * the caches of a core, in the order windows lists them: the data and the instruction l1, the l2, the l3.
 * measured on windows 11 26300: an amd processor without the topology extensions is read from leaves 0x80000005
 * and 0x80000006, and each core gets a cache of its own at every level, the l3 too.
 */
ULONG KiQueryCaches(KI_CACHE *caches, ULONG room)
{
    ULONG regs[4];
    KiCpuid(0, 0, regs);
    if (regs[0] >= 4) {
        ULONG n = deterministic_caches(4, caches, room);
        if (n)
            return n;
    }
    if ((g_ext1[2] & 1u << 22) && g_max_ext >= 0x8000001D) {
        ULONG n = deterministic_caches(0x8000001D, caches, room);
        if (n)
            return n;
    }
    ULONG n = 0;
    if (g_max_ext >= 0x80000005) {
        KiCpuid(0x80000005, 0, regs);
        for (ULONG k = 0; k < 2 && n < room; k++) {
            ULONG v = regs[2 + k];
            if (!(v >> 24))
                continue;
            caches[n++] = (KI_CACHE){1, (UCHAR)(v >> 16), (USHORT)(v & 0xFF), (v >> 24) * 1024, k ? 1 : 2, FALSE};
        }
    }
    if (g_max_ext >= 0x80000006) {
        KiCpuid(0x80000006, 0, regs);
        if ((regs[2] >> 16) && n < room)
            caches[n++] = (KI_CACHE){2, legacy_associativity(regs[2] >> 12), (USHORT)(regs[2] & 0xFF),
                                     (regs[2] >> 16) * 1024, 0, FALSE};
        if ((regs[3] >> 18) && n < room)
            caches[n++] = (KI_CACHE){3, legacy_associativity(regs[3] >> 12), (USHORT)(regs[3] & 0xFF),
                                     (regs[3] >> 18) * 512 * 1024, 0, FALSE};
    }
    return n;
}
