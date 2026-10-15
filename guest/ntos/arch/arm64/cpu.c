/*
 * processor identification from the arm64 id registers, the source of the
 * processor features, SYSTEM_INFO and the CentralProcessor registry key.
 */
#include "../../ntos.h"
#include "../../cm/cm.h"

KI_PROCESSOR_IDS KiProcessorIds;

#define READ_ID(enc) KI_READ_SYSREG(enc)

void KiReadProcessorIds(void)
{
    KI_PROCESSOR_IDS *p = &KiProcessorIds;
    p->Midr = READ_ID(midr_el1);
    /* the machine the guest presents itself as; the boot information is the
     * authority, so the identity is the same whatever the framework does. */
    if (HalBootInfo->cpu_midr)
        p->Midr = HalBootInfo->cpu_midr;
    p->Pfr0 = READ_ID(s3_0_c0_c4_0);
    p->Pfr1 = READ_ID(s3_0_c0_c4_1);
    p->Dfr0 = READ_ID(s3_0_c0_c5_0);
    p->Dfr1 = READ_ID(s3_0_c0_c5_1);
    p->Afr0 = READ_ID(s3_0_c0_c5_4);
    p->Afr1 = READ_ID(s3_0_c0_c5_5);
    p->Isar0 = READ_ID(s3_0_c0_c6_0);
    p->Isar1 = READ_ID(s3_0_c0_c6_1);
    p->Isar2 = READ_ID(s3_0_c0_c6_2);
    p->Mmfr0 = READ_ID(s3_0_c0_c7_0);
    p->Mmfr1 = READ_ID(s3_0_c0_c7_1);
    p->Mmfr2 = READ_ID(s3_0_c0_c7_2);
    p->Ctr = READ_ID(ctr_el0);
}

static ULONG field(ULONG64 reg, int shift)
{
    return (ULONG)(reg >> shift) & 0xF;
}

/* sets the PF_* features of KUSER_SHARED_DATA the way windows derives them. */
void KiSetProcessorFeatures(BOOLEAN *features)
{
    const KI_PROCESSOR_IDS *p = &KiProcessorIds;
    features[12] = TRUE; /* PF_NX_ENABLED */
    features[23] = TRUE; /* PF_FASTFAIL_AVAILABLE */
    features[19] = field(p->Pfr0, 20) != 0xF;                         /* PF_ARM_NEON_INSTRUCTIONS_AVAILABLE */
    features[29] = TRUE;                                               /* PF_ARM_V8_INSTRUCTIONS_AVAILABLE */
    features[30] = field(p->Isar0, 4) != 0 && field(p->Isar0, 8) != 0; /* PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE */
    features[31] = field(p->Isar0, 16) != 0;                           /* PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE */
    features[34] = field(p->Isar0, 20) >= 2;                           /* PF_ARM_V81_ATOMIC_INSTRUCTIONS_AVAILABLE */
    features[43] = field(p->Isar0, 44) != 0;                           /* PF_ARM_V82_DP_INSTRUCTIONS_AVAILABLE */
    features[44] = field(p->Isar1, 12) != 0;                           /* PF_ARM_V83_JSCVT_INSTRUCTIONS_AVAILABLE */
    features[45] = field(p->Isar1, 20) != 0;                           /* PF_ARM_V83_LRCPC_INSTRUCTIONS_AVAILABLE */
}

/* the vendor name windows shows for an implementer code. */
PCSTR KiProcessorVendor(void)
{
    switch ((KiProcessorIds.Midr >> 24) & 0xFF) {
    case 0x41: return "ARM Limited";
    case 0x4E: return "NVIDIA Corporation";
    case 0x51: return "Qualcomm Technologies Inc";
    case 0x61: return "Apple";
    case 0x6D: return "Microsoft Corporation";
    default: return "Unknown";
    }
}

/* the armv8 family, as in the processor identifier. */
USHORT KiProcessorLevel(void)
{
    return 8;
}

USHORT KiProcessorModel(void)
{
    return (USHORT)((KiProcessorIds.Midr >> 4) & 0xFFF);
}

USHORT KiProcessorRevision(void)
{
    return (USHORT)(((KiProcessorIds.Midr >> 20) & 0xF) << 8 | (KiProcessorIds.Midr & 0xF));
}

void KiFormatProcessorIdentifier(PWCHAR text, ULONG chars)
{
    _snwprintf(text, chars, L"ARMv8 (64-bit) Family 8 Model %X Revision %u", KiProcessorModel(),
               (ULONG)(KiProcessorIds.Midr & 0xF));
}

/* the id registers windows copies into the CentralProcessor keys, named by their encoding. */
void KiSetProcessorRegistryValues(PCWSTR key, KI_SET_REGISTRY_VALUE set)
{
    static const struct {
        USHORT Encoding;
        SIZE_T Offset;
    } registers[] = {
        {0x4000, offsetof(KI_PROCESSOR_IDS, Midr)},  {0x4020, offsetof(KI_PROCESSOR_IDS, Pfr0)},
        {0x4021, offsetof(KI_PROCESSOR_IDS, Pfr1)},  {0x4028, offsetof(KI_PROCESSOR_IDS, Dfr0)},
        {0x4029, offsetof(KI_PROCESSOR_IDS, Dfr1)},  {0x402C, offsetof(KI_PROCESSOR_IDS, Afr0)},
        {0x402D, offsetof(KI_PROCESSOR_IDS, Afr1)},  {0x4030, offsetof(KI_PROCESSOR_IDS, Isar0)},
        {0x4031, offsetof(KI_PROCESSOR_IDS, Isar1)}, {0x4032, offsetof(KI_PROCESSOR_IDS, Isar2)},
        {0x4038, offsetof(KI_PROCESSOR_IDS, Mmfr0)}, {0x4039, offsetof(KI_PROCESSOR_IDS, Mmfr1)},
        {0x403A, offsetof(KI_PROCESSOR_IDS, Mmfr2)}, {0x5801, offsetof(KI_PROCESSOR_IDS, Ctr)},
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(registers); i++) {
        WCHAR value[16] = {0};
        _snwprintf(value, RTL_NUMBER_OF(value) - 1, L"CP %04X", registers[i].Encoding);
        ULONG64 v = *(const ULONG64 *)((const UCHAR *)&KiProcessorIds + registers[i].Offset);
        set(key, value, REG_QWORD, &v, sizeof(v));
    }
}

UCHAR KiProcessorSmbiosType(void)
{
    return 0xC4; /* arm 64 bit */
}

PCSTR KiProcessorSmbiosManufacturer(void)
{
    return "Qualcomm Technologies Inc";
}

ULONG64 KiProcessorSmbiosId(void)
{
    return KiProcessorIds.Midr;
}

/* arm64 programs see no hypervisor. */
BOOLEAN KiQueryHypervisorLeaves(ULONG leaves[][4], ULONG count)
{
    return FALSE;
}

void KiIdentifyToHypervisor(void)
{
}
