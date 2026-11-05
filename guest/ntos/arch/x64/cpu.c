/*
 * processor identification from cpuid, for the processor features, SYSTEM_INFO
 * and the CentralProcessor key. the guest sees the host's own processor.
 */
#include "../../ntos.h"
#include "../../cm/cm.h"

static ULONG g_leaf1[4], g_leaf7[4], g_ext1[4], g_max_ext;
static char g_vendor[13];
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
    features[21] = bit(l1, 2, 31);             /* PF_VIRT_FIRMWARE_ENABLED */
    features[22] = bit(l7, 1, 0);              /* PF_RDWRFSGSBASE_AVAILABLE */
    features[23] = TRUE;                       /* PF_FASTFAIL_AVAILABLE */
    features[24] = bit(l1, 2, 30);             /* PF_RDRAND_INSTRUCTION_AVAILABLE */
    features[25] = bit(e1, 3, 27);             /* PF_RDTSCP_INSTRUCTION_AVAILABLE */
    features[26] = bit(l1, 2, 22);             /* PF_MOVBE_INSTRUCTION_AVAILABLE */
    features[27] = bit(l7, 1, 18);             /* PF_RDSEED_INSTRUCTION_AVAILABLE */
    features[36] = bit(l1, 2, 9);              /* PF_SSSE3_INSTRUCTIONS_AVAILABLE */
    features[37] = bit(l1, 2, 19);             /* PF_SSE4_1_INSTRUCTIONS_AVAILABLE */
    features[38] = bit(l1, 2, 20);             /* PF_SSE4_2_INSTRUCTIONS_AVAILABLE */
    features[39] = bit(l1, 2, 28) && (KiXsaveMask & 4);  /* PF_AVX_INSTRUCTIONS_AVAILABLE */
    features[40] = bit(l7, 1, 5) && (KiXsaveMask & 4);   /* PF_AVX2_INSTRUCTIONS_AVAILABLE */
    features[41] = bit(l7, 1, 16) && (KiXsaveMask & 0xE0); /* PF_AVX512F_INSTRUCTIONS_AVAILABLE */
    features[42] = bit(l7, 1, 29);             /* PF_ERMS_AVAILABLE */
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
    return strcmp(g_vendor, "GenuineIntel") ? 0x6B : 0xC6; /* amd zen family, intel core i7 */
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
