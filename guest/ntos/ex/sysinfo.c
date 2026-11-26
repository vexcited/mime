/* system information and time services. */
#include "../ntos.h"
#include "../se/se.h"
#include "firmware.h"
#include <mime/harderror.h>
#ifdef __aarch64__
#include "acpi_tables.h"
#else
#include "../ps/classref.h"
#include "sysref.inc"
#endif

NTSTATUS NTAPI NtQuerySystemTime(PLARGE_INTEGER time)
{
    LARGE_INTEGER t;
    KeQuerySystemTime(&t);
    return KiCopyToUser(time, &t, sizeof(t));
}

NTSTATUS NTAPI NtQueryPerformanceCounter(PLARGE_INTEGER counter, PLARGE_INTEGER frequency)
{
    LARGE_INTEGER f;
    LARGE_INTEGER c = KeQueryPerformanceCounter(&f);
    NTSTATUS status = KiCopyToUser(counter, &c, sizeof(c));
    if (NT_SUCCESS(status) && frequency)
        status = KiCopyToUser(frequency, &f, sizeof(f));
    return status;
}

/* a 64-bit value of the caller checked as windows checks it: on four bytes, then read, and written back if asked. */
static NTSTATUS probe_user_quad(PULONG64 user, BOOLEAN write)
{
    if ((ULONG_PTR)user & 3)
        return STATUS_DATATYPE_MISALIGNMENT;
    if (!KiIsUserRange(user, sizeof(*user)))
        return STATUS_ACCESS_VIOLATION;
    KJUMP_BUFFER jb;
    if (!KI_TRY_USER(jb))
        return KiFaultResult();
    volatile UCHAR *byte = (volatile UCHAR *)user;
    UCHAR value = *byte;
    if (write)
        __atomic_compare_exchange_n(byte, &value, value, FALSE, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
    KI_END_TRY_USER(jb);
    return STATUS_SUCCESS;
}

/*
 * the auxiliary counter is a timer of the platform that few machines have: windows 11 26300 checks the pointers,
 * the value read, the two results written, then answers that it is not supported.
 */
NTSTATUS NTAPI NtQueryAuxiliaryCounterFrequency(PULONG64 frequency)
{
    NTSTATUS status = probe_user_quad(frequency, TRUE);
    return NT_SUCCESS(status) ? STATUS_NOT_SUPPORTED : status;
}

NTSTATUS NTAPI NtConvertBetweenAuxiliaryCounterAndPerformanceCounter(BOOLEAN to_performance, PULONG64 value,
                                                                      PULONG64 converted, PULONG64 error)
{
    (void)to_performance;
    NTSTATUS status = probe_user_quad(value, FALSE);
    if (NT_SUCCESS(status))
        status = probe_user_quad(converted, TRUE);
    if (NT_SUCCESS(status) && error)
        status = probe_user_quad(error, TRUE);
    return NT_SUCCESS(status) ? STATUS_NOT_SUPPORTED : status;
}

/*
 * timers are programmed to the exact deadline, so a resolution request only
 * has to be remembered. the system resolution is the finest any live process
 * asked for, or the default.
 */
enum {
    TIMER_RESOLUTION_COARSEST = 156250,
    TIMER_RESOLUTION_FINEST = 5000,
};

static ULONG current_resolution(void)
{
    ULONG best = KI_CLOCK_TICK_HNS;
    ULONG64 s = KiAcquireDispatcherLock();
    for (PLIST_ENTRY e = PsActiveProcessHead.Flink; e != &PsActiveProcessHead; e = e->Flink) {
        PEPROCESS p = CONTAINING_RECORD(e, EPROCESS, ActiveProcessLinks);
        if (p->TimerResolution && p->TimerResolution < best)
            best = p->TimerResolution;
    }
    KiReleaseDispatcherLock(s);
    return best;
}

void ExReleaseTimerResolution(PEPROCESS process)
{
    process->TimerResolution = 0;
}

NTSTATUS NTAPI NtQueryTimerResolution(PULONG maximum, PULONG minimum, PULONG current)
{
    ULONG max = TIMER_RESOLUTION_COARSEST, min = TIMER_RESOLUTION_FINEST, cur = current_resolution();
    KiCopyToUser(maximum, &max, sizeof(max));
    KiCopyToUser(minimum, &min, sizeof(min));
    return KiCopyToUser(current, &cur, sizeof(cur));
}

NTSTATUS NTAPI NtSetTimerResolution(ULONG desired, BOOLEAN set, PULONG current)
{
    PEPROCESS process = PsCurrentProcess();
    if (set) {
        if (desired > TIMER_RESOLUTION_COARSEST)
            desired = TIMER_RESOLUTION_COARSEST;
        if (desired < TIMER_RESOLUTION_FINEST)
            desired = TIMER_RESOLUTION_FINEST;
        process->TimerResolution = desired;
    } else if (!process->TimerResolution) {
        return STATUS_TIMER_RESOLUTION_NOT_SET;
    } else {
        process->TimerResolution = 0;
    }
    ULONG cur = current_resolution();
    return KiCopyToUser(current, &cur, sizeof(cur));
}

ULONG NTAPI NtGetTickCount(void)
{
    return (ULONG)(KiQueryInterruptTime() / 10000);
}

static NTSTATUS copy_info(PVOID dst, const void *src, ULONG size, ULONG length, PULONG returned)
{
    if (returned)
        KiCopyToUser(returned, &size, sizeof(size));
    if (length < size)
        return STATUS_INFO_LENGTH_MISMATCH;
    return KiCopyToUser(dst, src, size);
}

/* ------------------------------------------------------------------ smbios */

#ifdef __aarch64__
/*
 * the reference machine's smbios tables, as real firmware presents them: bios,
 * system, board, chassis and processor, with the strings programs read to learn
 * the machine. built in one buffer, served by SystemFirmwareTableInformation. an
 * arm64 host has no smbios of its own, an x64 one serves the one it has.
 */
typedef struct SMB_BUILDER {
    PUCHAR Buffer;
    ULONG Size;
    ULONG Capacity;
    ULONG Header; /* offset of the structure being built */
    UCHAR NextString;
    ULONG StringsLen;      /* bytes staged for the current structure's string set */
    UCHAR Strings[1024];   /* the string set, appended after the formatted area at smb_end */
} SMB_BUILDER;

static void smb_put(SMB_BUILDER *b, const void *data, ULONG len)
{
    if (b->Size + len > b->Capacity)
        return;
    memcpy(b->Buffer + b->Size, data, len);
    b->Size += len;
}

static void smb_byte(SMB_BUILDER *b, UCHAR v)
{
    smb_put(b, &v, 1);
}

static void smb_word(SMB_BUILDER *b, USHORT v)
{
    smb_put(b, &v, 2);
}

static void smb_dword(SMB_BUILDER *b, ULONG v)
{
    smb_put(b, &v, 4);
}

static void smb_qword(SMB_BUILDER *b, ULONG64 v)
{
    smb_put(b, &v, 8);
}

static void smb_begin(SMB_BUILDER *b, UCHAR type, USHORT handle)
{
    b->Header = b->Size;
    b->NextString = 0;
    b->StringsLen = 0;
    UCHAR header[4] = {type, 0, (UCHAR)handle, (UCHAR)(handle >> 8)};
    smb_put(b, header, sizeof(header));
}

/*
 * the string index for a structure field, 0 for none. the text is held back for the string set that follows the
 * formatted area; only the one byte index belongs among the fields, the way smbios lays a structure out.
 */
static UCHAR smb_string(SMB_BUILDER *b, const char *text)
{
    if (!text || !text[0])
        return 0;
    ULONG len = (ULONG)strlen(text) + 1;
    if (b->StringsLen + len <= sizeof(b->Strings)) {
        memcpy(b->Strings + b->StringsLen, text, len);
        b->StringsLen += len;
    }
    return ++b->NextString;
}

static void smb_end(SMB_BUILDER *b)
{
    /* the formatted area ends here: its length is what the Length field carries, strings excluded. */
    if (b->Header + 4 <= b->Size)
        b->Buffer[b->Header + 1] = (UCHAR)(b->Size - b->Header);
    /* then the string set, ended by an empty string; a structure with none still closes with two zeros. */
    smb_put(b, b->Strings, b->StringsLen);
    smb_byte(b, 0);
    if (b->NextString == 0)
        smb_byte(b, 0);
}

/*
 * from the boot information's logical byte order to the tables' wire order:
 * the first three fields are little endian.
 */
static void smb_uuid(SMB_BUILDER *b, const UCHAR uuid[16])
{
    UCHAR wire[16];
    for (int i = 0; i < 4; i++)
        wire[i] = uuid[3 - i];
    for (int i = 0; i < 2; i++)
        wire[4 + i] = uuid[5 - i];
    for (int i = 0; i < 2; i++)
        wire[6 + i] = uuid[7 - i];
    memcpy(wire + 8, uuid + 8, 8);
    smb_put(b, wire, sizeof(wire));
}

/* includes the RawSmbiosData header, returns the size. */
static ULONG smb_build(PUCHAR buffer, ULONG capacity)
{
    SMB_BUILDER b = {buffer, 0, capacity};

    /* type 0, the bios. */
    smb_begin(&b, 0, 0x0000);
    smb_byte(&b, smb_string(&b, HalBootInfo->bios_vendor));
    smb_byte(&b, smb_string(&b, HalBootInfo->bios_version));
    smb_word(&b, 0);
    smb_byte(&b, smb_string(&b, HalBootInfo->bios_date));
    smb_byte(&b, 0xFF);                     /* rom size, 16 MiB units */
    smb_qword(&b, 0);                       /* no legacy bios characteristics */
    smb_byte(&b, 0x81);                     /* acpi and uefi supported */
    smb_byte(&b, 0x00);
    smb_end(&b);

    /* type 1, the system. */
    smb_begin(&b, 1, 0x0001);
    smb_byte(&b, smb_string(&b, HalBootInfo->system_manufacturer));
    smb_byte(&b, smb_string(&b, HalBootInfo->system_product));
    smb_byte(&b, smb_string(&b, "1"));
    smb_byte(&b, smb_string(&b, HalBootInfo->system_serial));
    smb_uuid(&b, HalBootInfo->system_uuid);
    smb_byte(&b, 0x06); /* wake up: power switch */
    smb_byte(&b, HalBootInfo->system_sku[0] ? smb_string(&b, HalBootInfo->system_sku) : 0);
    smb_byte(&b, smb_string(&b, HalBootInfo->system_family));
    smb_end(&b);

    /* type 2, the board, inside the chassis below. */
    smb_begin(&b, 2, 0x0002);
    smb_byte(&b, smb_string(&b, HalBootInfo->system_manufacturer));
    smb_byte(&b, smb_string(&b, HalBootInfo->system_product));
    smb_byte(&b, smb_string(&b, "1"));
    smb_byte(&b, smb_string(&b, HalBootInfo->system_serial));
    smb_byte(&b, 0); /* no asset tag */
    smb_byte(&b, 0x01); /* a hosting board */
    smb_byte(&b, 0); /* no location in chassis */
    smb_word(&b, 0x0003);
    smb_byte(&b, 0x0A); /* a motherboard */
    smb_byte(&b, 0);    /* no contained objects */
    smb_end(&b);

    /* type 3, the chassis. */
    smb_begin(&b, 3, 0x0003);
    smb_byte(&b, smb_string(&b, HalBootInfo->system_manufacturer));
    smb_byte(&b, 0x0A); /* a notebook */
    smb_byte(&b, smb_string(&b, "1"));
    smb_byte(&b, smb_string(&b, HalBootInfo->system_serial));
    smb_byte(&b, 0); /* no asset tag */
    smb_byte(&b, 0x03); /* safe at boot */
    smb_byte(&b, 0x02); /* power supply ok */
    smb_byte(&b, 0x02); /* thermal ok */
    smb_byte(&b, 0x03); /* no security lock */
    smb_dword(&b, 0);
    smb_byte(&b, 0); /* no height in units */
    smb_byte(&b, 0); /* power cords not reported */
    smb_byte(&b, 0); /* no contained elements */
    smb_byte(&b, 0); /* of no record length */
    smb_end(&b);

    /* type 4, the processor. */
    smb_begin(&b, 4, 0x0004);
    smb_byte(&b, smb_string(&b, "Onboard - CPU"));
    smb_byte(&b, 0x03); /* a central processor */
    smb_byte(&b, KiProcessorSmbiosType());
    smb_byte(&b, smb_string(&b, KiProcessorSmbiosManufacturer()));
    smb_qword(&b, KiProcessorSmbiosId());
    smb_byte(&b, smb_string(&b, HalBootInfo->cpu_name));
    smb_byte(&b, 0x80); /* 8 V */
    smb_word(&b, 0);    /* external clock unknown */
    smb_word(&b, (USHORT)HalBootInfo->cpu_mhz);
    smb_word(&b, (USHORT)HalBootInfo->cpu_mhz);
    smb_byte(&b, 0x01); /* populated and enabled */
    smb_byte(&b, 0x06); /* not upgradeable */
    smb_word(&b, 0xFFFF); /* no cache handles */
    smb_word(&b, 0xFFFF);
    smb_word(&b, 0xFFFF);
    smb_byte(&b, 0); /* no serial */
    smb_byte(&b, 0); /* no asset tag */
    smb_byte(&b, 0); /* no part number */
    smb_byte(&b, (UCHAR)HalBootInfo->cpu_count);
    smb_byte(&b, (UCHAR)HalBootInfo->cpu_count);
    smb_byte(&b, (UCHAR)HalBootInfo->cpu_count);
    smb_word(&b, 0x0004); /* 64 bit capable */
    smb_word(&b, 0xFFFF); /* no second family field */
    smb_end(&b);


    /* types 16 and 17, the memory array and its devices, what a physical memory reader walks. */
    ULONG64 ram_mb = HalBootInfo->ram_size >> 20, ram_kb = HalBootInfo->ram_size >> 10;
    smb_begin(&b, 16, 0x0010);
    smb_byte(&b, 0x03); /* system board */
    smb_byte(&b, 0x03); /* system memory */
    smb_byte(&b, 0x03); /* no error correction */
    /* the maximum capacity in KiB, 0x80000000 when it takes the extended field in bytes. */
    BOOLEAN extended = ram_kb >= 0x80000000ull;
    smb_dword(&b, extended ? 0x80000000u : (ULONG)ram_kb);
    smb_word(&b, 0xFFFE); /* no error information handle */
    smb_word(&b, 1);      /* one device for the installed memory */
    smb_qword(&b, extended ? HalBootInfo->ram_size : 0);
    smb_end(&b);

    for (ULONG slot = 0; slot < 1; slot++) {
        ULONG64 device_mb = ram_mb;
        smb_begin(&b, 17, (USHORT)(0x0011 + slot));
        smb_word(&b, 0x0010); /* the array */
        smb_word(&b, 0xFFFE); /* no error information handle */
        smb_word(&b, 64);     /* total width */
        smb_word(&b, 64);     /* data width */
        /* the size in MiB, 0x7FFF from 32 GiB less 1 MiB on, which the extended size then carries. */
        smb_word(&b, device_mb >= 0x7FFF ? 0x7FFF : (USHORT)device_mb);
        smb_byte(&b, 0x0E); /* row of chips, the memory is soldered */
        smb_byte(&b, 0);    /* no device set */
        smb_byte(&b, smb_string(&b, slot ? "Row 2" : "Row 1"));
        smb_byte(&b, 0);    /* no bank locator */
        smb_byte(&b, 0x23); /* LPDDR5 */
        smb_word(&b, 0x2002); /* double data rate */
        smb_word(&b, 6400);   /* MT/s */
        smb_byte(&b, smb_string(&b, HalBootInfo->system_manufacturer));
        smb_byte(&b, 0); /* no serial */
        smb_byte(&b, 0); /* no asset tag */
        smb_byte(&b, 0); /* no part number */
        smb_byte(&b, 0); /* unknown rank */
        smb_dword(&b, device_mb >= 0x7FFF ? (ULONG)device_mb : 0); /* extended size, MiB */
        smb_word(&b, 6400); /* configured speed, MT/s */
        smb_word(&b, 0);    /* minimum, maximum and configured voltages unknown */
        smb_word(&b, 0);
        smb_word(&b, 0);
        smb_end(&b);
    }

    /* type 32, the boot information: no errors. */
    smb_begin(&b, 32, 0x0020);
    for (int i = 0; i < 6; i++)
        smb_byte(&b, 0);
    smb_byte(&b, 0); /* boot status, no errors */
    smb_end(&b);

    /* type 43, the firmware tpm the machine has. */
    smb_begin(&b, 43, 0x0043);
    smb_put(&b, "MSFT", 4); /* vendor id */
    smb_byte(&b, 2);        /* major spec version */
    smb_byte(&b, 0);        /* minor */
    smb_dword(&b, 1);       /* firmware version 1.0 */
    smb_dword(&b, 0);       /* no second firmware version */
    smb_byte(&b, smb_string(&b, "Firmware TPM"));
    smb_qword(&b, 0x06); /* enabled and active */
    smb_dword(&b, 0);    /* no oem data */
    smb_end(&b);

    /* the end of the table. */
    smb_begin(&b, 127, 0x0005);
    smb_end(&b);
    return b.Size;
}
#endif

/*
 * the raw firmware data of the acpi tables, under the RSDT provider: the
 * enumeration lists the signatures and a get copies the table of one, like
 * acpi.sys serves the tables it mapped. like wdm, a too small buffer gets the
 * required size in TableBufferLength.
 */
/*
 * the reference machine's MADT lists its own processors. the copy a program reads lists the machine's: the first local
 * APIC entries are enabled with the APIC ids the processors report in CPUID, the index of each, the others disabled,
 * and the checksum is made again.
 */
static void fit_madt(UCHAR *madt, ULONG size)
{
    ULONG cpus = HalBootInfo->cpu_count, n = 0, enabled = 0;
    for (ULONG at = 44; at + 2 <= size && madt[at + 1] >= 2 && at + madt[at + 1] <= size; at += madt[at + 1])
        if (madt[at] == 0 && madt[at + 1] >= 8 && (madt[at + 4] & 1))
            enabled++;
    /* a table that already has as many processors as the machine is the host's own and stands as it is. */
    if (enabled == cpus)
        return;
    for (ULONG at = 44; at + 2 <= size && madt[at + 1] >= 2 && at + madt[at + 1] <= size; at += madt[at + 1]) {
        if (madt[at] != 0 || madt[at + 1] < 8)
            continue;
        ULONG flags;
        memcpy(&flags, madt + at + 4, sizeof(flags));
        if (n < cpus) {
            madt[at + 3] = (UCHAR)n;
            flags |= 1;
        } else {
            flags &= ~1u;
        }
        memcpy(madt + at + 4, &flags, sizeof(flags));
        n++;
    }
    UCHAR sum = 0;
    madt[9] = 0;
    for (ULONG i = 0; i < size; i++)
        sum += madt[i];
    madt[9] = (UCHAR)-sum;
}

#ifdef __aarch64__
/* an arm64 host has no acpi tables of its own, so these are the reference machine's. */
ULONG KiFirmwareAcpiCount(VOID)
{
    return ACPI_TABLE_COUNT;
}

const UCHAR *KiFirmwareAcpi(ULONG index, PULONG signature, PULONG size)
{
    if (index >= ACPI_TABLE_COUNT)
        return nullptr;
    *signature = acpi_tables[index].Signature;
    *size = acpi_tables[index].Size;
    return acpi_tables[index].Data;
}
#else
/* the firmware of the machine mime runs on. the host read it and starts no machine without it. */
static const MimeFirmware *host_firmware(VOID)
{
    static const MimeFirmware *found;
    static BOOLEAN looked;
    if (!looked) {
        looked = TRUE;
        if (HalBootInfo->firmware_phys && HalBootInfo->firmware_size) {
            const MimeFirmware *firmware = (const MimeFirmware *)HalPhysmap(HalBootInfo->firmware_phys);
            if (firmware->magic == MIME_FIRMWARE_MAGIC)
                found = firmware;
        }
    }
    return found;
}

ULONG KiFirmwareAcpiCount(VOID)
{
    const MimeFirmware *firmware = host_firmware();
    return firmware ? firmware->count : 0;
}

const UCHAR *KiFirmwareAcpi(ULONG index, PULONG signature, PULONG size)
{
    const MimeFirmware *firmware = host_firmware();
    if (!firmware || index >= firmware->count)
        return nullptr;
    *signature = firmware->tables[index].signature;
    *size = firmware->tables[index].length;
    return (const UCHAR *)firmware + firmware->tables[index].offset;
}
#endif

/* what a provider refuses: the length that comes back is the header's. */
static NTSTATUS firmware_refused(NTSTATUS status, ULONG header_size, PULONG returned)
{
    if (returned)
        KiCopyToUser(returned, &header_size, sizeof(header_size));
    return status;
}

static NTSTATUS query_acpi_tables(PVOID info, ULONG length, ULONG header_size, SYSTEM_FIRMWARE_TABLE_INFORMATION *header,
                                  PULONG returned)
{
    if (header->Action == SystemFirmwareTable_Enumerate) {
        ULONG count = KiFirmwareAcpiCount();
        ULONG need = header_size + count * sizeof(ULONG);
        if (header->TableBufferLength < count * sizeof(ULONG) || length < need) {
            header->TableBufferLength = count * sizeof(ULONG);
            NTSTATUS status = KiCopyToUser(info, header, header_size);
            if (NT_SUCCESS(status) && returned)
                status = KiCopyToUser(returned, &need, sizeof(need));
            return NT_SUCCESS(status) ? STATUS_BUFFER_TOO_SMALL : status;
        }
        UCHAR *out = ExAllocatePool2(POOL_FLAG_NON_PAGED, need, KI_TAG('A', 'c', 'p', 'E'));
        if (!out)
            return STATUS_INSUFFICIENT_RESOURCES;
        header->TableBufferLength = count * sizeof(ULONG);
        memcpy(out, header, header_size);
        ULONG at = header_size;
        for (ULONG i = 0; i < count; i++) {
            ULONG signature, size;
            if (!KiFirmwareAcpi(i, &signature, &size))
                break;
            memcpy(out + at, &signature, sizeof(ULONG));
            at += sizeof(ULONG);
        }
        NTSTATUS status = copy_info(info, out, need, length, returned);
        ExFreePool(out);
        return status;
    }

    if (header->Action != SystemFirmwareTable_Get)
        return firmware_refused(STATUS_NOT_IMPLEMENTED, header_size, returned);
    const UCHAR *data = nullptr;
    ULONG table_signature = 0, table_size = 0;
    for (ULONG i = 0; i < KiFirmwareAcpiCount() && !data; i++) {
        const UCHAR *candidate = KiFirmwareAcpi(i, &table_signature, &table_size);
        if (!candidate)
            break;
        if (table_signature == header->TableId)
            data = candidate;
    }
    if (!data)
        return firmware_refused(STATUS_NOT_FOUND, header_size, returned);

    ULONG need = header_size + table_size;
    if (header->TableBufferLength < table_size || length < need) {
        header->TableBufferLength = table_size;
        NTSTATUS status = KiCopyToUser(info, header, header_size);
        if (NT_SUCCESS(status) && returned)
            status = KiCopyToUser(returned, &need, sizeof(need));
        return NT_SUCCESS(status) ? STATUS_BUFFER_TOO_SMALL : status;
    }
    UCHAR *out = ExAllocatePool2(POOL_FLAG_NON_PAGED, need, KI_TAG('A', 'c', 'p', 'T'));
    if (!out)
        return STATUS_INSUFFICIENT_RESOURCES;
    header->TableBufferLength = table_size;
    memcpy(out, header, header_size);
    memcpy(out + header_size, data, table_size);
    if (table_signature == 0x43495041)
        fit_madt(out + header_size, table_size);
    NTSTATUS status = copy_info(info, out, need, length, returned);
    ExFreePool(out);
    return status;
}

/*
 * SystemFirmwareTableInformation: the RSMB provider, the raw smbios data, and the ACPI one, the acpi tables. on x64
 * both are the host's own, on arm64 those of the reference machine. like wdm, a too small buffer gets the required
 * size in TableBufferLength. measured on windows 11 26300 on uefi firmware: no other provider is there, FIRM and RSDT
 * included, and asking one is STATUS_NOT_IMPLEMENTED, as is an action a provider does not know. a table acpi does not
 * have is STATUS_NOT_FOUND.
 */
static NTSTATUS query_firmware_tables(PVOID info, ULONG length, PULONG returned)
{
    SYSTEM_FIRMWARE_TABLE_INFORMATION header;
    ULONG header_size = offsetof(SYSTEM_FIRMWARE_TABLE_INFORMATION, TableBuffer);
    if (length < header_size)
        return firmware_refused(STATUS_INFO_LENGTH_MISMATCH, header_size, returned);
    NTSTATUS status = KiCopyFromUser(&header, info, header_size);
    if (!NT_SUCCESS(status))
        return status;
    if (header.ProviderSignature == 'ACPI')
        return query_acpi_tables(info, length, header_size, &header, returned);
    if (header.ProviderSignature != 'RSMB')
        return firmware_refused(STATUS_NOT_IMPLEMENTED, 0, returned);

    if (header.Action == SystemFirmwareTable_Enumerate) {
        ULONG table_id = 0; /* the single table of the smbios provider */
        ULONG need = header_size + sizeof(table_id);
        if (header.TableBufferLength < sizeof(table_id) || length < need) {
            header.TableBufferLength = sizeof(table_id);
            status = KiCopyToUser(info, &header, header_size);
            if (NT_SUCCESS(status) && returned)
                status = KiCopyToUser(returned, &need, sizeof(need));
            return NT_SUCCESS(status) ? STATUS_BUFFER_TOO_SMALL : status;
        }
        UCHAR out[32];
        header.TableBufferLength = sizeof(table_id);
        memcpy(out, &header, header_size);
        memcpy(out + header_size, &table_id, sizeof(table_id));
        return copy_info(info, out, need, length, returned);
    }

    /* the table id is not looked at: smbios has one table. */
    if (header.Action != SystemFirmwareTable_Get)
        return firmware_refused(STATUS_NOT_IMPLEMENTED, header_size, returned);

#ifdef __aarch64__
    UCHAR table[2048];
    ULONG table_size = smb_build(table, sizeof(table));
    ULONG data_size = offsetof(RAW_SMBIOS_DATA, SMBIOSTableData) + table_size;
#else
    const MimeFirmware *firmware = host_firmware();
    if (!firmware || !firmware->smbios_length)
        return firmware_refused(STATUS_NOT_FOUND, header_size, returned);
    ULONG data_size = firmware->smbios_length;
#endif
    ULONG need = header_size + data_size;
    if (header.TableBufferLength < data_size || length < need) {
        header.TableBufferLength = data_size;
        status = KiCopyToUser(info, &header, header_size);
        if (NT_SUCCESS(status) && returned)
            status = KiCopyToUser(returned, &need, sizeof(need));
        return NT_SUCCESS(status) ? STATUS_BUFFER_TOO_SMALL : status;
    }
    UCHAR *out = ExAllocatePool2(POOL_FLAG_NON_PAGED, need, KI_TAG('F', 'w', 'T', 'b'));
    if (!out)
        return STATUS_INSUFFICIENT_RESOURCES;
    header.TableBufferLength = data_size;
    memcpy(out, &header, header_size);
#ifdef __aarch64__
    RAW_SMBIOS_DATA *built = (RAW_SMBIOS_DATA *)(out + header_size);
    built->Used20CallingMethod = 1;
    built->SMBIOSMajorVersion = 3;
    built->SMBIOSMinorVersion = 4;
    built->DmiRevision = 0;
    built->Length = table_size;
    memcpy(built->SMBIOSTableData, table, table_size);
#else
    memcpy(out + header_size, (const UCHAR *)firmware + firmware->smbios_offset, data_size);
#endif
    status = copy_info(info, out, need, length, returned);
    ExFreePool(out);
    return status;
}


/* ------------------------------------------------------------------ processes */

static PCWSTR process_leaf(PEPROCESS p, ULONG *chars)
{
    if (!p->ImagePath.Length) {
        *chars = 0;
        return nullptr;
    }
    PCWSTR path = p->ImagePath.Buffer;
    ULONG n = p->ImagePath.Length / sizeof(WCHAR), start = n;
    while (start && path[start - 1] != '\\')
        start--;
    *chars = n - start;
    return path + start;
}

/*
 * a record of the process list as windows 11 26300 lays it out: the process, its threads, 0x170 bytes of
 * SYSTEM_PROCESS_INFORMATION_EXTENSION, then the name in a room rounded up to 8 bytes. the extended class has longer
 * thread records.
 */
#define PROCESS_EXTENSION_SIZE 0x170
#define EXTENDED_THREAD_SIZE 0x88

static ULONG process_record_size(ULONG threads, ULONG thread_size, ULONG chars)
{
    return sizeof(SYSTEM_PROCESS_INFORMATION) + threads * thread_size + PROCESS_EXTENSION_SIZE +
           (chars ? ((chars + 1) * sizeof(WCHAR) + 7) & ~7u : 0);
}

static ULONG process_thread_count(PEPROCESS p)
{
    ULONG n = 0;
    for (PLIST_ENTRY e = p->ThreadListHead.Flink; e != &p->ThreadListHead; e = e->Flink)
        if (!CONTAINING_RECORD(e, ETHREAD, ThreadListEntry)->HasTerminated)
            n++;
    return n;
}

/*
 * fills a kernel buffer under the dispatcher lock, which guards the process and
 * thread lists. returns the size needed, which may exceed capacity; nothing is
 * written past capacity.
 */
static ULONG fill_processes(PUCHAR buffer, ULONG capacity, PUCHAR user_base, BOOLEAN extended)
{
    ULONG used = 0;
    ULONG thread_size = extended ? EXTENDED_THREAD_SIZE : sizeof(SYSTEM_THREAD_INFORMATION);
    SYSTEM_PROCESS_INFORMATION *last = nullptr;
    /* measured without the debug privilege, elevated or not: the start addresses read zero. */
    BOOLEAN addresses = SeThreadHasPrivilege(SE_DEBUG_PRIVILEGE);
    PETHREAD current = (PETHREAD)KiCurrentThread();
    static const WCHAR system_name[] = L"System";
    ULONG64 lock = KiAcquireDispatcherLock();
    /* the list opens with the idle process: no name, a running thread for each processor. */
    ULONG idle_size = process_record_size(HalBootInfo->cpu_count, thread_size, 0);
    if (idle_size <= capacity) {
        SYSTEM_PROCESS_INFORMATION *info = (SYSTEM_PROCESS_INFORMATION *)buffer;
        memset(info, 0, idle_size);
        info->NumberOfThreads = HalBootInfo->cpu_count;
        PUCHAR t = (PUCHAR)(info + 1);
        for (ULONG i = 0; i < HalBootInfo->cpu_count; i++, t += thread_size) {
            ((SYSTEM_THREAD_INFORMATION *)t)->Priority = 1;
            ((SYSTEM_THREAD_INFORMATION *)t)->ThreadState = Running;
        }
        last = info;
    }
    used += idle_size;
    for (PLIST_ENTRY e = PsActiveProcessHead.Flink; e != &PsActiveProcessHead; e = e->Flink) {
        PEPROCESS p = CONTAINING_RECORD(e, EPROCESS, ActiveProcessLinks);
        if (p->Exited)
            continue;
        ULONG threads = process_thread_count(p);
        ULONG chars;
        PCWSTR leaf = process_leaf(p, &chars);
        if (!chars && (ULONG_PTR)p->UniqueProcessId == 4) {
            leaf = system_name;
            chars = RTL_NUMBER_OF(system_name) - 1;
        }
        ULONG size = process_record_size(threads, thread_size, chars);
        if (used + size <= capacity) {
            SYSTEM_PROCESS_INFORMATION *info = (SYSTEM_PROCESS_INFORMATION *)(buffer + used);
            memset(info, 0, size);
            info->NumberOfThreads = threads;
            info->NumberOfThreadsHighWatermark = threads;
            info->CreateTime.QuadPart = (LONGLONG)p->CreateTime;
            info->BasePriority = p->Pcb.BasePriority ? p->Pcb.BasePriority : 8;
            info->UniqueProcessId = p->UniqueProcessId;
            info->InheritedFromUniqueProcessId = p->InheritedFromUniqueProcessId;
            info->HandleCount = p->HandleTable.Count;
            info->SessionId = p->SessionId;
            /* the key is the identifier in the extended class and zero in the plain one. */
            info->UniqueProcessKey = extended ? (ULONG_PTR)p->UniqueProcessId : 0;
            PUCHAR at = (PUCHAR)(info + 1);
            ULONG64 switches = 0;
            for (PLIST_ENTRY te = p->ThreadListHead.Flink; te != &p->ThreadListHead; te = te->Flink) {
                ETHREAD *thread = CONTAINING_RECORD(te, ETHREAD, ThreadListEntry);
                if (thread->HasTerminated)
                    continue;
                SYSTEM_THREAD_INFORMATION *t = (SYSTEM_THREAD_INFORMATION *)at;
                t->CreateTime.QuadPart = (LONGLONG)thread->CreateTime;
                if (addresses)
                    t->StartAddress = thread->Win32StartAddress ? thread->Win32StartAddress : thread->StartAddress;
                t->ClientId = thread->Cid;
                t->Priority = thread->Tcb.Priority;
                t->BasePriority = thread->Tcb.BasePriority;
                t->ContextSwitches = (ULONG)thread->Tcb.ContextSwitches;
                t->WaitTime = thread->Tcb.WaitTime;
                t->ThreadState = thread == current ? Running : thread->Tcb.State;
                t->WaitReason = thread->Tcb.WaitReason;
                switches += thread->Tcb.ContextSwitches;
                if (extended) {
                    /* stack base, stack limit and start address read zero; the teb is told, of any process. */
                    PVOID *more = (PVOID *)(t + 1);
                    if (addresses)
                        more[2] = thread->Win32StartAddress;
                    more[3] = thread->SystemThread ? nullptr : thread->Tcb.Teb;
                }
                at += thread_size;
            }
            /* of the extension: the context switches at 0x28 and the sequence number of the process at 0x168. */
            memcpy(at + 0x28, &switches, sizeof(switches));
            memcpy(at + 0x168, &p->SequenceNumber, sizeof(p->SequenceNumber));
            at += PROCESS_EXTENSION_SIZE;
            if (chars) {
                memcpy(at, leaf, chars * sizeof(WCHAR));
                info->ImageName.Length = (USHORT)(chars * sizeof(WCHAR));
                info->ImageName.MaximumLength = (USHORT)(((chars + 1) * sizeof(WCHAR) + 7) & ~7u);
                info->ImageName.Buffer = (PWSTR)(user_base + (at - buffer));
            }
            if (last)
                last->NextEntryOffset = (ULONG)((PUCHAR)info - (PUCHAR)last);
            last = info;
        }
        used += size;
    }
    KiReleaseDispatcherLock(lock);
    return used;
}

static NTSTATUS query_processes(BOOLEAN extended, PVOID info, ULONG length, PULONG returned)
{
    /* buffers larger than any process list needs are filled only up to this bound. */
    ULONG capacity = length < 0x100000 ? length : 0x100000;
    PUCHAR buffer = capacity ? ExAllocatePool2(POOL_FLAG_NON_PAGED, capacity, KI_TAG('P', 's', 'I', 'n')) : nullptr;
    if (capacity && !buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    ULONG need = fill_processes(buffer, capacity, info, extended);
    NTSTATUS status;
    if (need <= capacity) {
        status = copy_info(info, buffer, need, length, returned);
    } else {
        if (returned)
            KiCopyToUser(returned, &need, sizeof(need));
        status = STATUS_INFO_LENGTH_MISMATCH;
    }
    if (buffer)
        ExFreePool(buffer);
    return status;
}

/*
 * SystemBasicProcessInformation, which EnumProcesses reads since windows 11 24h2: the identifiers, the sequence
 * number and the name of every process. measured on windows 11 26300: a record is 0x30 bytes and its name follows in a
 * room rounded up to 8 bytes, the idle process opens the list with no name, the four bytes after the offset and after
 * the two lengths are not written, nor is the room past the end of the name. a room too small gets the records that
 * fit whole and the size of the list.
 */
#define BASIC_PROCESS_SIZE 0x30

static NTSTATUS query_basic_processes(PVOID info, ULONG length, PULONG returned)
{
    ULONG capacity = length < 0x100000 ? length : 0x100000;
    PUCHAR buffer = capacity ? ExAllocatePool2(POOL_FLAG_NON_PAGED, capacity, KI_TAG('P', 's', 'I', 'n')) : nullptr;
    if (capacity && !buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    static const WCHAR system_name[] = L"System";
    ULONG need = BASIC_PROCESS_SIZE, fit = 0;
    PUCHAR last = nullptr;
    if (need <= capacity) {
        memset(buffer, 0, BASIC_PROCESS_SIZE);
        last = buffer;
        fit = need;
    }
    ULONG64 lock = KiAcquireDispatcherLock();
    for (PLIST_ENTRY e = PsActiveProcessHead.Flink; e != &PsActiveProcessHead; e = e->Flink) {
        PEPROCESS p = CONTAINING_RECORD(e, EPROCESS, ActiveProcessLinks);
        if (p->Exited)
            continue;
        ULONG chars;
        PCWSTR leaf = process_leaf(p, &chars);
        if (!chars && (ULONG_PTR)p->UniqueProcessId == 4) {
            leaf = system_name;
            chars = RTL_NUMBER_OF(system_name) - 1;
        }
        USHORT room = chars ? (USHORT)(((chars + 1) * sizeof(WCHAR) + 7) & ~7u) : 0;
        ULONG size = BASIC_PROCESS_SIZE + room;
        if (fit == need && need + size <= capacity) {
            PUCHAR record = buffer + need;
            memset(record, 0, size);
            *(HANDLE *)(record + 0x8) = p->UniqueProcessId;
            *(HANDLE *)(record + 0x10) = p->InheritedFromUniqueProcessId;
            *(PULONG64)(record + 0x18) = p->SequenceNumber;
            if (chars) {
                *(PUSHORT)(record + 0x20) = (USHORT)(chars * sizeof(WCHAR));
                *(PUSHORT)(record + 0x22) = room;
                *(PVOID *)(record + 0x28) = (PUCHAR)info + need + BASIC_PROCESS_SIZE;
                memcpy(record + BASIC_PROCESS_SIZE, leaf, chars * sizeof(WCHAR));
            }
            if (last)
                *(PULONG)last = (ULONG)(record - last);
            last = record;
            fit = need + size;
        }
        need += size;
    }
    KiReleaseDispatcherLock(lock);
    NTSTATUS status = STATUS_SUCCESS;
    for (ULONG at = 0; at < fit && NT_SUCCESS(status);) {
        PUCHAR record = buffer + at;
        ULONG next = *(PULONG)record, name = *(PUSHORT)(record + 0x20);
        status = KiCopyToUser((PUCHAR)info + at, record, 4);
        if (NT_SUCCESS(status))
            status = KiCopyToUser((PUCHAR)info + at + 0x8, record + 0x8, 0x1c);
        if (NT_SUCCESS(status))
            status = KiCopyToUser((PUCHAR)info + at + 0x28, record + 0x28, 0x8 + (name ? name + sizeof(WCHAR) : 0));
        if (!next)
            break;
        at += next;
    }
    if (buffer)
        ExFreePool(buffer);
    if (returned && NT_SUCCESS(status))
        status = KiCopyToUser(returned, &need, sizeof(need));
    if (NT_SUCCESS(status) && fit < need)
        status = STATUS_INFO_LENGTH_MISMATCH;
    return status;
}

/* ------------------------------------------------------------------ handles */

#define SYSTEM_HANDLE_PROCESSES_MAX 1024
#define OBJ_PROTECT_CLOSE_ATTRIBUTE 0x1

/*
 * SystemHandleInformation and SystemExtendedHandleInformation: every open handle of every process. the
 * short form keeps identifiers in 16 bits, like the original.
 */
static NTSTATUS query_handles(BOOLEAN extended, PVOID info, ULONG length, PULONG returned)
{
    PEPROCESS processes[SYSTEM_HANDLE_PROCESSES_MAX];
    ULONG n = 0;
    ULONG64 lock = KiAcquireDispatcherLock();
    for (PLIST_ENTRY e = PsActiveProcessHead.Flink; e != &PsActiveProcessHead && n < SYSTEM_HANDLE_PROCESSES_MAX; e = e->Flink) {
        PEPROCESS p = CONTAINING_RECORD(e, EPROCESS, ActiveProcessLinks);
        if (p->Exited)
            continue;
        ObReferenceObject(p);
        processes[n++] = p;
    }
    KiReleaseDispatcherLock(lock);

    ULONG entry_size = extended ? 40 : 24, header = extended ? 16 : 8;
    /* windows asks for the header and one entry before it tells the size of the list. */
    if (length < header + entry_size) {
        ULONG least = header + entry_size;
        for (ULONG i = 0; i < n; i++)
            ObDereferenceObject(processes[i]);
        if (returned)
            KiCopyToUser(returned, &least, sizeof(least));
        return STATUS_INFO_LENGTH_MISMATCH;
    }
    ULONG total = 0;
    for (ULONG i = 0; i < n; i++)
        total += (ULONG)processes[i]->HandleTable.Count;
    /* some slack for handles opened while counting; extra ones are dropped. */
    SIZE_T room = (SIZE_T)total + 64;
    PUCHAR buffer = ExAllocatePool2(POOL_FLAG_PAGED, header + room * entry_size, KI_TAG('S', 'y', 'H', 'd'));
    if (!buffer) {
        for (ULONG i = 0; i < n; i++)
            ObDereferenceObject(processes[i]);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    SIZE_T count = 0;
    /* windows 11 24h2 and later keep the address of every object from a caller without SeDebugPrivilege. */
    BOOLEAN hidden = KeGetPreviousMode() == UserMode && !SeThreadHasPrivilege(SE_DEBUG_PRIVILEGE);
    for (ULONG i = 0; i < n; i++) {
        PHANDLE_TABLE table = &processes[i]->HandleTable;
        ULONG64 state = ObLockHandleTable(table);
        for (ULONG h = 0; h < table->Capacity && count < room; h++) {
            HANDLE_TABLE_ENTRY *entry = &table->Entries[h];
            if (!entry->Object)
                continue;
            POBJECT_TYPE type = OBJECT_TO_HEADER(entry->Object)->Type;
            ULONG attributes = (entry->Attributes & OBJ_PROTECT_CLOSE_ATTRIBUTE ? 1 : 0) | (entry->Attributes & OBJ_INHERIT ? 2 : 0);
            PUCHAR out = buffer + header + count * entry_size;
            ULONG_PTR pid = (ULONG_PTR)processes[i]->UniqueProcessId, value = (ULONG_PTR)(h + 1) * 4;
            if (extended) {
                *(PVOID *)(out + 0) = hidden ? nullptr : entry->Object;
                *(ULONG_PTR *)(out + 8) = pid;
                *(ULONG_PTR *)(out + 16) = value;
                *(PULONG)(out + 24) = entry->GrantedAccess;
                *(PUSHORT)(out + 28) = 0;
                *(PUSHORT)(out + 30) = (USHORT)(type->Index + 2);
                *(PULONG)(out + 32) = attributes;
                *(PULONG)(out + 36) = 0;
            } else {
                *(PUSHORT)(out + 0) = (USHORT)pid;
                *(PUSHORT)(out + 2) = 0;
                out[4] = (UCHAR)(type->Index + 2);
                out[5] = (UCHAR)attributes;
                *(PUSHORT)(out + 6) = (USHORT)value;
                *(PVOID *)(out + 8) = hidden ? nullptr : entry->Object;
                *(PULONG)(out + 16) = entry->GrantedAccess;
                *(PULONG)(out + 20) = 0;
            }
            count++;
        }
        ObUnlockHandleTable(table, state);
    }
    for (ULONG i = 0; i < n; i++)
        ObDereferenceObject(processes[i]);
    memset(buffer, 0, header);
    if (extended)
        *(PULONG_PTR)buffer = count;
    else
        *(PULONG)buffer = (ULONG)count;
    /*
     * measured on windows 11 26300: the count and the handles that fit are written even when the room is short, and
     * neither the bytes after the count nor the last four of a handle are.
     */
    ULONG size = (ULONG)(header + count * entry_size);
    SIZE_T fit = (length - header) / entry_size < count ? (length - header) / entry_size : count;
    if (returned)
        KiCopyToUser(returned, &size, sizeof(size));
    NTSTATUS status = KiCopyToUser(info, buffer, header / 2);
    for (SIZE_T i = 0; i < fit && NT_SUCCESS(status); i++)
        status = KiCopyToUser((PUCHAR)info + header + i * entry_size, buffer + header + i * entry_size, entry_size - 4);
    if (NT_SUCCESS(status) && length < size)
        status = STATUS_INFO_LENGTH_MISMATCH;
    ExFreePool(buffer);
    return status;
}

/* ------------------------------------------------------------------ modules */

/*
 * windows builds this list from PsLoadedModuleList alone, the images loaded into the kernel in their load order, the
 * kernel first: measured on windows 11 26300, no image of a process is in it, in either class. here too, see
 * mm/sysimage.c for what the list holds.
 */
typedef struct {
    CHAR Path[256];
    USHORT NameOffset, PathLength, LoadCount;
    PVOID Base;
    ULONG Size, Flags, CheckSum, TimeDateStamp;
} SYSTEM_MODULE;

typedef struct {
    SYSTEM_MODULE *Modules;
    ULONG Capacity, Count;
} MODULE_LIST;

static void module_callback(PVOID context, ULONG index, PKLDR_DATA_TABLE_ENTRY module)
{
    MODULE_LIST *list = context;
    if (list->Count >= list->Capacity)
        return;
    SYSTEM_MODULE *m = &list->Modules[list->Count++];
    memset(m, 0, sizeof(*m));
    ULONG n = module->FullDllName.Length / sizeof(WCHAR), at = 0;
    for (ULONG i = 0; i < n && at < sizeof(m->Path) - 1; i++) {
        WCHAR c = module->FullDllName.Buffer[i];
        m->Path[at++] = c < 0x80 ? (CHAR)c : '?';
        if (c == L'\\')
            m->NameOffset = (USHORT)at;
    }
    m->PathLength = (USHORT)at;
    m->LoadCount = module->LoadCount;
    m->Flags = module->Flags;
    m->Base = module->DllBase;
    m->Size = module->SizeOfImage;
    m->CheckSum = module->CheckSum;
    m->TimeDateStamp = module->TimeDateStamp;
}

static NTSTATUS collect_system_modules(MODULE_LIST *list)
{
    MiSyncSystemImages();
    list->Capacity = 512;
    list->Modules = ExAllocatePool2(POOL_FLAG_PAGED, list->Capacity * sizeof(SYSTEM_MODULE), KI_TAG('S', 'y', 'M', 'o'));
    if (!list->Modules)
        return STATUS_INSUFFICIENT_RESOURCES;
    MiEnumerateSystemImages(module_callback, list);
    return STATUS_SUCCESS;
}

/*
 * measured on windows 11 26300 with a buffer of ee bytes. class b: nothing is written under 8 bytes, then the count
 * alone, then each module whose whole 0x128 bytes fit, of which only the bytes from the base to the nul of the path
 * are written. class 4d has no count: each module that fits before a last zero word is written, its word of offset,
 * then from the mapped base to the nul of the path, then its last 16 bytes, and the zero word ends the list. both
 * tell the full size. the bases and the timestamp are zero without the debug privilege, the mapped base and the
 * section always. measured on windows 11 26100 with the privilege: the base the image was built for is zero too, in
 * every row.
 */
static NTSTATUS query_modules(BOOLEAN extended, PVOID info, ULONG length, PULONG returned)
{
    MODULE_LIST list = {};
    NTSTATUS status = collect_system_modules(&list);
    if (!NT_SUCCESS(status))
        return status;
    SYSTEM_MODULE *modules = list.Modules;
    ULONG count = list.Count;
    BOOLEAN hidden = KeGetPreviousMode() == UserMode && !SeThreadHasPrivilege(SE_DEBUG_PRIVILEGE);
    ULONG entry_size = extended ? 0x140 : 0x128, header = extended ? 0 : 8, tail = extended ? 2 : 0;
    ULONG size = header + count * entry_size + tail;
    if (returned)
        KiCopyToUser(returned, &size, sizeof(size));
    ULONG fit = 0;
    if (length >= header + tail) {
        fit = (length - header - tail) / entry_size;
        if (fit > count)
            fit = count;
        status = extended ? STATUS_SUCCESS : KiCopyToUser(info, &count, sizeof(count));
        for (ULONG i = 0; i < fit && NT_SUCCESS(status); i++) {
            const SYSTEM_MODULE *m = &modules[i];
            PUCHAR at = (PUCHAR)info + header + i * entry_size;
            struct {
                PVOID MappedBase;
                PVOID ImageBase;
                ULONG ImageSize;
                ULONG Flags;
                USHORT LoadOrderIndex, InitOrderIndex, LoadCount, OffsetToFileName;
                CHAR Path[256];
            } e = {};
            e.ImageBase = hidden ? nullptr : m->Base;
            e.ImageSize = m->Size;
            e.Flags = m->Flags;
            e.LoadOrderIndex = (USHORT)i;
            e.LoadCount = m->LoadCount;
            e.OffsetToFileName = m->NameOffset;
            memcpy(e.Path, m->Path, m->PathLength);
            if (!extended) {
                status = KiCopyToUser(at + 0x10, &e.ImageBase, 0x18 + m->PathLength + 1);
                continue;
            }
            USHORT next = (USHORT)entry_size;
            struct {
                ULONG ImageChecksum, TimeDateStamp;
                PVOID DefaultBase;
            } stamp = {m->CheckSum, hidden ? 0 : m->TimeDateStamp, nullptr};
            status = KiCopyToUser(at, &next, sizeof(next));
            if (NT_SUCCESS(status))
                status = KiCopyToUser(at + 0x10, &e, 0x20 + m->PathLength + 1);
            if (NT_SUCCESS(status))
                status = KiCopyToUser(at + 0x130, &stamp, sizeof(stamp));
        }
        if (extended && NT_SUCCESS(status)) {
            USHORT end = 0;
            status = KiCopyToUser((PUCHAR)info + fit * entry_size, &end, sizeof(end));
        }
    }
    if (NT_SUCCESS(status) && (fit < count || length < size))
        status = STATUS_INFO_LENGTH_MISMATCH;
    ExFreePool(list.Modules);
    return status;
}

/*
 * SystemProcessIdInformation, like windows: the caller names the process and lends an empty, even sized string; the
 * device path of its image goes there with its nul. a buffer too small gets the size needed in MaximumLength.
 */
static NTSTATUS query_process_id(PVOID info, ULONG length, PULONG returned)
{
    struct {
        HANDLE ProcessId;
        UNICODE_STRING ImageName;
    } in;
    /*
     * measured on windows 11 26100: the length is told when it is not the one, and again once the process is found,
     * whether its name fits or not. a process that does not exist leaves it alone.
     */
    if (length != sizeof(in)) {
        ULONG size = sizeof(in);
        if (returned)
            KiCopyToUser(returned, &size, sizeof(size));
        return STATUS_INFO_LENGTH_MISMATCH;
    }
    NTSTATUS status = KiCopyFromUser(&in, info, sizeof(in));
    if (!NT_SUCCESS(status))
        return status;
    if (in.ImageName.Length || (in.ImageName.MaximumLength & 1))
        return STATUS_INVALID_PARAMETER;
    PEPROCESS process = PspLookupProcess(in.ProcessId);
    if (!process)
        return STATUS_INVALID_CID;
    WCHAR path[MAX_PATH + 32];
    ULONG n = PsGetDeviceImagePath(process, path, sizeof(path) - sizeof(WCHAR));
    path[n / sizeof(WCHAR)] = 0;
    USHORT need = n ? (USHORT)(n + sizeof(WCHAR)) : 0;
    UNICODE_STRING out = {(USHORT)n, need, n ? in.ImageName.Buffer : nullptr};
    if (need > in.ImageName.MaximumLength) {
        status = STATUS_INFO_LENGTH_MISMATCH;
        KiCopyToUser((PUCHAR)info + offsetof(UNICODE_STRING, MaximumLength) + sizeof(HANDLE), &need, sizeof(need));
    } else {
        status = n ? KiCopyToUser(in.ImageName.Buffer, path, need) : STATUS_SUCCESS;
        if (NT_SUCCESS(status))
            status = KiCopyToUser((PUCHAR)info + sizeof(HANDLE), &out, sizeof(out));
    }
    ObDereferenceObject(process);
    if (returned && (NT_SUCCESS(status) || status == STATUS_INFO_LENGTH_MISMATCH)) {
        ULONG size = sizeof(in);
        KiCopyToUser(returned, &size, sizeof(size));
    }
    return status;
}

/* whether a byte the caller points at can be read. */
static BOOLEAN user_readable(PVOID at)
{
    UCHAR byte;
    return NT_SUCCESS(KiCopyFromUser(&byte, at, 1));
}

/*
 * the three lists of a count and its entries, measured on windows 11 26300: a room that does not hold one entry is
 * told the room of one; any other is told the room of all of them and gets the count and the entries that fit, short
 * or not. the bytes that pad the count and the entries are never written, nor are the entry and recursion counts of
 * a lock, so each entry is written in its spans. the address of a lock or of a block is kept from the caller.
 */
static NTSTATUS query_counted_list(ULONG klass, PVOID info, ULONG length, PULONG returned)
{
    static const struct {
        UCHAR At, End;
    } spans[3][3] = {
        {{0x00, 0x0C}, {0x10, 0x20}, {0x28, 0x30}},
        {{0x00, 0x0C}, {0x10, 0x28}},
        {{0x00, 0x14}},
    };
    ULONG which = klass == 12 ? 0 : klass == 22 ? 1 : 2, entry = which == 0 ? 0x30 : which == 1 ? 0x28 : 0x18;
    ULONG need = 8 + entry;
    if (length < need) {
        if (returned)
            KiCopyToUser(returned, &need, sizeof(need));
        return STATUS_INFO_LENGTH_MISMATCH;
    }
    /* windows keeps a resource in every token and two in the control block of every open file, which this kernel locks
       otherwise: they are counted with its own, as windows would list them. */
    extern POBJECT_TYPE SeTokenObjectType;
    ULONG resources = ExQueryResourceCount() + (ULONG)SeTokenObjectType->TotalObjects + 2 * (ULONG)IoFileObjectTypeObject->TotalObjects;
    ULONG count = which == 0 ? resources : which == 1 ? ExQueryPoolTags(nullptr, 0) : ExQueryBigPool(nullptr, 0);
    ULONG room = count + 64;
    PUCHAR list = ExAllocatePool2(POOL_FLAG_PAGED, (SIZE_T)room * entry, KI_TAG('S', 'y', 'L', 's'));
    if (!list)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (which == 0) {
        /* RTL_PROCESS_LOCK_INFORMATION: a resource is type 1, and nothing else of it is told to this caller. */
        for (ULONG i = 0; i < count; i++)
            *(PUSHORT)(list + i * entry + 8) = 1;
    } else {
        ULONG found = which == 1 ? ExQueryPoolTags(list, room) : ExQueryBigPool(list, room);
        count = found < room ? found : room;
    }
    need = 8 + count * entry;
    if (returned)
        KiCopyToUser(returned, &need, sizeof(need));
    ULONG fit = (length - 8) / entry < count ? (length - 8) / entry : count;
    NTSTATUS status = KiCopyToUser(info, &count, sizeof(count));
    for (ULONG i = 0; i < fit && NT_SUCCESS(status); i++)
        for (ULONG k = 0; k < 3 && spans[which][k].End && NT_SUCCESS(status); k++)
            status = KiCopyToUser((PUCHAR)info + 8 + i * entry + spans[which][k].At, list + i * entry + spans[which][k].At,
                                  spans[which][k].End - spans[which][k].At);
    ExFreePool(list);
    if (NT_SUCCESS(status) && length < need)
        status = STATUS_INFO_LENGTH_MISMATCH;
    return status;
}

/* a class that takes its block and then says no: the size first, as for any class, then the refusal windows gives. */
static NTSTATUS refuse_sized(ULONG need, NTSTATUS refusal, ULONG refused_length, ULONG length, PULONG returned)
{
    ULONG size = length < need ? need : refused_length;
    if (returned)
        KiCopyToUser(returned, &size, sizeof(size));
    return length < need ? STATUS_INFO_LENGTH_MISMATCH : refusal;
}

/*
 * the highest page of the memory map firmware gives windows: the memory rounded up to 256 mb, with the 2 gb the
 * devices keep below 4 gb once it reaches them. the lowest is page 1.
 */
static ULONG highest_physical_page(void)
{
    ULONG pages = ((ULONG)MmGetTotalPages() + 0xFFFF) & ~0xFFFFu;
    return pages > 0x80000 ? pages + 0x80000 - 1 : pages - 1;
}

/* 100ns a processor has not run a thread for. */
static ULONG64 processor_idle_time(PKPRCB prcb)
{
    ULONG64 now = KiQueryInterruptTime(), busy = prcb->KernelTime + prcb->UserTime;
    return now > busy ? now - busy : 0;
}

/*
 * the small blocks of NtQuerySystemInformation, each as windows 11 26300 lays it out. a caller that sweeps the
 * classes asks each of them with no room and then with the room it was told, so every one of these answers its
 * size and then its data. what the kernel counts is reported from its counters; what describes hardware or policy
 * this machine does not model is the state of the reference machine, an amd zen 3 laptop with no debugger, no
 * verifier and no hypervisor. FALSE when the class is not one of them.
 */
static BOOLEAN query_small_block(ULONG klass, PVOID info, ULONG length, PULONG returned, NTSTATUS *status)
{
    ULONG count = HalBootInfo->cpu_count;
    union {
        UCHAR Bytes[0x100];
        ULONG Longs[0x40];
        ULONG64 Quads[0x20];
    } b = {};
    ULONG size = 0;
    NTSTATUS small = STATUS_INFO_LENGTH_MISMATCH; /* what a buffer too small gets: a few classes say it otherwise. */
    switch (klass) {
    case 7: /* SystemDeviceInformation: one disk, no floppy, cd, tape, serial or parallel port. */
        b.Longs[0] = 1;
        size = 0x18;
        break;
    case 9:   /* SystemFlagsInformation: no global flag. */
    case 65:  /* SystemLostDelayedWriteInformation: none lost. */
    case 151: /* a zero dword on the reference machine. */
    case 207: /* a zero dword on the reference machine. */
        size = 4;
        break;
    case 60: /* SystemNumaAvailableMemory: the highest node, and with the room for it the free memory of node 0. */
        b.Quads[1] = (ULONG64)MmGetAvailablePages() << PAGE_SHIFT;
        size = length < 0x10 ? 4 : 0x10;
        break;
    case 117: /* SystemTpmBootEntropyInformation: its one length is taken, and then kept from a caller without the privilege. */
        if (length == 0x448 && !SeThreadHasPrivilege(SE_TCB_PRIVILEGE)) {
            *status = STATUS_ACCESS_DENIED;
            return TRUE;
        }
        *status = refuse_sized(0x448, STATUS_INFO_LENGTH_MISMATCH, 0x448, length, returned);
        return TRUE;
    case 118: { /* SystemVerifierCountersInformation: no driver is verified, so nothing is told once there is room. */
        ULONG none = length < 0x110 ? 0x110 : 0;
        if (returned)
            KiCopyToUser(returned, &none, sizeof(none));
        *status = length < 0x110 ? STATUS_INFO_LENGTH_MISMATCH : STATUS_SUCCESS;
        return TRUE;
    }
    case 17: /* SystemObjectInformation: needs the object type list flag, which is off. */
        *status = refuse_sized(0x40, STATUS_UNSUCCESSFUL, 0, length, returned);
        return TRUE;
    case 23: { /* SystemInterruptInformation, a block for each processor. */
        struct {
            ULONG ContextSwitches, DpcCount, DpcRate, TimeIncrement, DpcBypassCount, ApcBypassCount;
        } p[MAXIMUM_PROCESSORS] = {};
        for (ULONG i = 0; i < count; i++) {
            PKPRCB prcb = KiProcessorBlock[i];
            if (!prcb)
                continue;
            p[i].ContextSwitches = (ULONG)prcb->ContextSwitches;
            p[i].DpcCount = (ULONG)prcb->DpcCount;
            p[i].DpcRate = 1;
            p[i].TimeIncrement = 20000;
        }
        *status = copy_info(info, p, count * sizeof(p[0]), length, returned);
        return TRUE;
    }
    case 24: /* SystemDpcBehaviorInformation: the queue depth, the minimum rate, the threshold and the ideal rate. */
        b.Longs[1] = 4;
        b.Longs[2] = 3;
        b.Longs[3] = 20;
        b.Longs[4] = 20;
        size = 0x14;
        break;
    case 28: /* SystemTimeAdjustmentInformation: the adjustment is the increment, and it is not disabled. */
        b.Longs[0] = KI_CLOCK_TICK_HNS;
        b.Longs[1] = KI_CLOCK_TICK_HNS;
        b.Longs[2] = TRUE;
        size = 0xC;
        if (length == 0x18) { /* the precise form, with its two values as quads. */
            /* measured on windows 11 26300: the precise values are those of a second, not of a tick. */
            b.Quads[0] = b.Quads[1] = 10000000;
            b.Quads[2] = TRUE;
            size = 0x18;
        }
        break;
    case 33: { /* SystemExceptionInformation: no fixup or emulation is ever counted, every dispatch is. */
        extern volatile LONG KeExceptionDispatchCount;
        b.Longs[1] = (ULONG)__atomic_load_n(&KeExceptionDispatchCount, __ATOMIC_RELAXED);
        size = 0x10;
        break;
    }
    case 36: { /* SystemContextSwitchInformation: the total, and none of the dispatcher's finer counts. */
        for (ULONG i = 0; i < count; i++)
            if (KiProcessorBlock[i])
                b.Longs[0] += (ULONG)KiProcessorBlock[i]->ContextSwitches;
        size = 0x30;
        break;
    }
    case 37: /* SystemRegistryQuotaInformation: no quota, and the paged pool it would be a share of. */
        b.Longs[0] = 0xFFFFFFFF;
        b.Longs[1] = 0x04000000;
        b.Quads[1] = 0x100000000000ull;
        size = 0x10;
        break;
    case 42: { /* SystemProcessorIdleInformation, a block for each processor: all the idle time is C1. */
        struct {
            ULONG64 IdleTime, C1Time, C2Time, C3Time;
            ULONG C1Transitions, C2Transitions, C3Transitions, Padding;
        } p[MAXIMUM_PROCESSORS] = {};
        for (ULONG i = 0; i < count; i++) {
            PKPRCB prcb = KiProcessorBlock[i];
            if (!prcb)
                continue;
            p[i].IdleTime = p[i].C1Time = processor_idle_time(prcb);
            p[i].C1Transitions = (ULONG)prcb->IdleTicks;
        }
        *status = copy_info(info, p, count * sizeof(p[0]), length, returned);
        return TRUE;
    }
    case 43: /* SystemLegacyDriverInformation: no driver vetoed anything, and the empty list points past the block. */
        b.Quads[2] = (ULONG64)(ULONG_PTR)info + 0x18;
        size = 0x18;
        break;
    case 51: { /* SystemVerifierInformation: no driver is verified, so the list is empty once there is room. */
        ULONG none = length < 0x90 ? 0x90 : 0;
        if (returned)
            KiCopyToUser(returned, &none, sizeof(none));
        *status = length < 0x90 ? STATUS_INFO_LENGTH_MISMATCH : STATUS_SUCCESS;
        return TRUE;
    }
    case 56: /* SystemPrefetcherInformation: takes a request this caller did not make. */
        *status = refuse_sized(0x20, STATUS_INVALID_PARAMETER, 0, length, returned);
        return TRUE;
    case 58: /* SystemRecommendedSharedDataAlignment: the cache line. */
        b.Longs[0] = 64;
        size = 4;
        break;
    case 59: /* SystemComPlusPackage: 64 bit com+ is on. the first query leaves the answer in the shared page. */
        if (SharedUserData->ComPlusPackage == 0xFFFFFFFF)
            SharedUserData->ComPlusPackage = 1;
        [[fallthrough]];
    case 70: /* SystemObjectSecurityMode. */
        b.Longs[0] = 1;
        size = 4;
        break;
    case 80: { /* SystemMemoryListInformation: free pages are zeroed or about to be, nothing is modified or bad. */
        SIZE_T free = MmGetFreePages();
        b.Quads[0] = free - free / 64; /* ZeroPageCount */
        b.Quads[1] = free / 64;        /* FreePageCount */
        size = 0xB0;
        break;
    }
    case 86:  /* SystemRefTraceInformation: no tracing. */
        size = 0x28;
        break;
    case 87: /* SystemSpecialPoolInformation: no tag, and the flag windows sets by default. */
        b.Longs[1] = 1;
        size = 8;
        break;
    case 92: /* SystemVerifierInformationEx: the verifier's default mode with nothing verified. */
        b.Longs[0] = 4;
        size = 0x28;
        break;
    case 100: { /* SystemProcessorPerformanceDistribution: an offset for each processor, then its empty state list. */
        ULONG header = (sizeof(ULONG) * (count + 1) + 7) & ~7u;
        ULONG *p = b.Longs;
        if (header + count * 8 > sizeof(b))
            return FALSE;
        p[0] = count;
        for (ULONG i = 0; i < count; i++) {
            p[1 + i] = header + i * 8;
            *(ULONG *)(b.Bytes + header + i * 8) = 2;
        }
        size = header + count * 8;
        break;
    }
    case 101: { /* SystemNumaProximityNodeInformation: the node of a proximity domain, and only domain 0 is there. */
        struct {
            ULONG NodeProximityId;
            USHORT NodeNumber, Pad;
        } map;
        size = sizeof(map);
        if (returned)
            KiCopyToUser(returned, &size, sizeof(size));
        if (length < size) {
            *status = STATUS_INFO_LENGTH_MISMATCH;
            return TRUE;
        }
        *status = KiCopyFromUser(&map, info, sizeof(map));
        if (NT_SUCCESS(*status))
            *status = map.NodeProximityId ? STATUS_NOT_FOUND : KiCopyToUser((PUCHAR)info + 4, &b, sizeof(USHORT));
        return TRUE;
    }
    case 195: /* SystemWriteConstraintInformation: no policy. */
    case 213: /* a zero quad on the reference machine. */
        size = 8;
        break;
    case 105: { /* SystemProcessorBrandString: the 48 characters of cpuid and their nul. */
        const char *name = KiProcessorBrand();
        for (ULONG i = 0; i < 48 && name[i]; i++)
            b.Bytes[i] = (UCHAR)name[i];
        size = 0x31;
        break;
    }
    case 115: /* SystemErrorPortTimeouts: 15 seconds to start, 10 to answer. */
        b.Longs[0] = 15000;
        b.Longs[1] = 10000;
        size = 8;
        break;
    case 122: /* the three below are not supported on the reference machine once they have their room. */
    case 135:
        *status = refuse_sized(8, STATUS_NOT_SUPPORTED, 8, length, returned);
        return TRUE;
    case 124:
        *status = refuse_sized(0xC, STATUS_NOT_SUPPORTED, 0xC, length, returned);
        return TRUE;
    case 123: /* the commit figures in pages: available, committed, the limit and the peak. */
        b.Quads[0] = MmGetAvailablePages();
        b.Quads[1] = MmGetCommittedPages();
        b.Quads[2] = MmGetCommitLimitPages();
        b.Quads[3] = MmGetPeakCommittedPages();
        size = 0x20;
        break;
    case 126: /* SystemBootGraphicsInformation: the frame buffer address is kept from user mode, 1920 by 1080, bgra. */
        b.Longs[2] = 1920;
        b.Longs[3] = 1080;
        b.Longs[4] = 1920;
        b.Longs[6] = 4;
        size = 0x20;
        break;
    case 134: { /* SystemPolicyInformation: a request and the room of its answer, at its one length. */
        struct {
            PVOID InputData, OutputData;
            ULONG InputDataSize, OutputDataSize, Version, Pad;
        } policy = {};
        size = sizeof(policy);
        if (returned)
            KiCopyToUser(returned, &size, sizeof(size));
        *status = STATUS_INFO_LENGTH_MISMATCH;
        if (length != size)
            return TRUE;
        /* a request of no length gets an answer too small; any other is read, and has to be there. */
        *status = KiCopyFromUser(&policy, info, sizeof(policy));
        if (NT_SUCCESS(*status))
            *status = policy.InputDataSize && !user_readable(policy.InputData) ? STATUS_ACCESS_VIOLATION : STATUS_BUFFER_TOO_SMALL;
        return TRUE;
    }
    case 136: /* the two below take a request this caller did not make: its length is told until it is the one. */
    case 137:
        size = 0x30;
        if (returned && length != size)
            KiCopyToUser(returned, &size, sizeof(size));
        *status = STATUS_INFO_LENGTH_MISMATCH;
        if (length == size) {
            /* SYSTEM_DEVICE_DATA_INFORMATION opens with the name of the device: no name is invalid, one has to be there. */
            UNICODE_STRING device = {};
            *status = KiCopyFromUser(&device, info, sizeof(device));
            if (NT_SUCCESS(*status))
                *status = device.Length && !user_readable(device.Buffer) ? STATUS_ACCESS_VIOLATION : STATUS_INVALID_PARAMETER;
        }
        return TRUE;
    case 174: { /* a list of offsets that is told as far as the room goes, and wants 0x20 bytes to succeed. */
        static const ULONG list[8] = {7, 0x24, 0x40, 0x44, 0x60, 0x88, 0xA8, 0xBC};
        ULONG fits = length / sizeof(ULONG) < 8 ? length / sizeof(ULONG) : 8;
        size = fits == 8 ? sizeof(list) : 0;
        if (returned)
            KiCopyToUser(returned, &size, sizeof(size));
        *status = STATUS_BUFFER_TOO_SMALL;
        if (fits == 8)
            *status = KiCopyToUser(info, list, sizeof(list));
        else if (fits > 1)
            KiCopyToUser((PUCHAR)info + sizeof(ULONG), list + 1, (fits - 1) * sizeof(ULONG));
        return TRUE;
    }
    case 188: /* a count in a quad at its one length, and no length told. */
        if (length != 8) {
            *status = STATUS_INFO_LENGTH_MISMATCH;
            return TRUE;
        }
        b.Quads[0] = 0xD0;
        size = 0;
        if (returned)
            KiCopyToUser(returned, &size, sizeof(size));
        *status = KiCopyToUser(info, &b, 8);
        return TRUE;
    case 139: /* takes a request of four bytes this caller did not make. */
        if (returned)
            KiCopyToUser(returned, &size, sizeof(size));
        *status = length < 4 ? (NTSTATUS)0xC0000206 : (NTSTATUS)0xC00000EF; /* an invalid buffer size, a first parameter */
        return TRUE;
    case 147: /* SystemPortableWorkspaceEfiLauncherInformation: not a portable workspace. */
    case 163: /* a zero byte on the reference machine. */
    case 202: /* SystemDmaGuardPolicyInformation: off. */
        size = 1;
        break;
    case 149: /* SystemKernelDebuggerInformationEx: not allowed, not enabled, not present. */
        size = 3;
        break;
    case 153: /* a block of zeros on the reference machine. */
        size = 0x20;
        break;
    case 154: /* SystemProcessorFeaturesInformation: the feature bits of class 1, then those of the reference processor. */
        b.Quads[0] = KiProcessorFeatureBits() | 0x024FE07Full << 32;
        size = 0x20;
        break;
    case 156: /* a block of zeros on the reference machine. */
        size = 0x80;
        break;
    case 157: /* a block of zeros on the reference machine. */
        size = 0x18;
        break;
    case 199: { /* SystemCodeIntegrityVerificationInformation: an image to verify, and none is nothing to do. */
        struct {
            HANDLE FileHandle;
            ULONG ImageSize, Pad;
            PVOID Image;
        } verify = {};
        size = sizeof(verify);
        if (returned)
            KiCopyToUser(returned, &size, sizeof(size));
        if (length < size) {
            *status = STATUS_INFO_LENGTH_MISMATCH;
            return TRUE;
        }
        *status = KiCopyFromUser(&verify, info, sizeof(verify));
        if (NT_SUCCESS(*status) && verify.ImageSize && !user_readable(verify.Image))
            *status = STATUS_ACCESS_VIOLATION;
        return TRUE;
    }
    case 158: /* a set byte on the reference machine. */
    case 227:
        b.Bytes[0] = 1;
        size = 1;
        break;
    case 182: { /* SystemMemoryUsageInformation, in bytes. */
        b.Quads[0] = (ULONG64)MmGetTotalPages() << PAGE_SHIFT;         /* TotalPhysicalBytes */
        b.Quads[1] = (ULONG64)MmGetAvailablePages() << PAGE_SHIFT;     /* AvailableBytes */
        b.Quads[2] = b.Quads[1];                                       /* ResidentAvailableBytes */
        b.Quads[3] = (ULONG64)MmGetCommittedPages() << PAGE_SHIFT;     /* CommittedBytes */
        b.Quads[5] = (ULONG64)MmGetCommitLimitPages() << PAGE_SHIFT;   /* CommitLimitBytes */
        b.Quads[6] = (ULONG64)MmGetPeakCommittedPages() << PAGE_SHIFT; /* PeakCommitmentBytes */
        size = 0x38;
        break;
    }
    case 184: /* SystemPhysicalMemoryInformation: the total, and the lowest and highest addresses. */
        b.Quads[0] = (ULONG64)MmGetTotalPages() << PAGE_SHIFT;
        b.Quads[1] = PAGE_SIZE;
        b.Quads[2] = ((ULONG64)highest_physical_page() + 1 << PAGE_SHIFT) - 1;
        size = 0x18;
        break;
    case 192: /* SystemFlushInformation: the three flush methods, and the cache line clflush works on. */
        {
            /* clflush always, clflushopt and clwb as leaf 7 tells them. */
            ULONG regs[4];
            KiCpuid(7, 0, regs);
            b.Longs[0] = 1 | (regs[1] >> 23 & 1) << 1 | (regs[1] >> 24 & 1) << 2;
        }
        b.Longs[1] = 64;
        size = 0x20;
        break;
    case 196: /* SystemKernelVaShadowInformation */
        b.Longs[0] = KiKernelVaShadowFlags();
        size = 4;
        break;
    case 197: /* SystemHypervisorSharedPageInformation: where the page of the counter is. */
        b.Quads[0] = MmHypervisorSharedPageVa();
        size = 8;
        break;
    case 201: /* SystemSpeculationControlInformation */
        KiSpeculationControlFlags(b.Longs);
        size = 8;
        break;
    case 206: /* a set dword and a zero one on the reference machine. */
        b.Longs[0] = 1;
        size = 8;
        break;
    case 216: /* fails on the reference machine once it has its room. */
        *status = refuse_sized(0x20, STATUS_UNSUCCESSFUL, 0x20, length, returned);
        return TRUE;
    case 221: /* 3 on the reference machine. */
        b.Longs[0] = 3;
        size = 4;
        break;
    case 243: /* a block of zeros on the reference machine. */
    case 250:
        size = 0x10;
        break;
    case 253: /* three counters that start at zero. */
        size = 0xC;
        break;
    case 12:   /* SystemLocksInformation */
    case 22:   /* SystemPoolTagInformation */
    case 66:   /* SystemBigPoolInformation */
        *status = query_counted_list(klass, info, length, returned);
        return TRUE;
    case 53: { /* SystemSessionProcessInformation: a session, then the room and the address of the list it fills. */
        struct {
            ULONG SessionId, SizeOfBuf;
            PVOID Buffer;
        } session;
        /* measured on windows 11 26300: the list is held to a dword boundary before its room is, and no length is told. */
        if (length >= sizeof(session) && NT_SUCCESS(KiCopyFromUser(&session, info, sizeof(session))) &&
            ((ULONG_PTR)session.Buffer & 3)) {
            *status = STATUS_DATATYPE_MISALIGNMENT;
            return TRUE;
        }
        *status = refuse_sized(0x10, STATUS_INFO_LENGTH_MISMATCH, 0x4000, length, returned);
        return TRUE;
    }
    case 83:    /* SystemProcessorIdleCycleTimeInformation, a count of cycles for each processor. */
    case 108: { /* SystemProcessorCycleTimeInformation */
        ULONG64 hz = KiProcessorMhz() ? (ULONG64)KiProcessorMhz() * 1000000 : 3000000000ull;
        for (ULONG i = 0; i < count && i < RTL_NUMBER_OF(b.Quads); i++) {
            PKPRCB prcb = KiProcessorBlock[i];
            ULONG64 time = !prcb ? 0 : klass == 83 ? processor_idle_time(prcb) : KiQueryInterruptTime();
            b.Quads[i] = time / 10000000 * hz + time % 10000000 * hz / 10000000;
        }
        size = count * sizeof(ULONG64);
        /* measured on windows 11 26300: no room for one count is a buffer too small; a room short of them all gets the
           counts that fit and a length mismatch. */
        small = STATUS_BUFFER_TOO_SMALL;
        if (length >= sizeof(ULONG64) && length < size) {
            if (returned)
                KiCopyToUser(returned, &size, sizeof(size));
            *status = KiCopyToUser(info, &b, length / sizeof(ULONG64) * sizeof(ULONG64));
            if (NT_SUCCESS(*status))
                *status = STATUS_INFO_LENGTH_MISMATCH;
            return TRUE;
        }
        break;
    }
    case 98:    /* SystemSystemPartitionInformation: a string in the caller's block, its characters after it. */
    case 99:    /* SystemSystemDiskInformation */
    case 200: { /* the system partition again. */
        static const WCHAR partition[] = L"\\Device\\HarddiskVolume1", disk[] = L"\\Device\\Harddisk0\\DR0";
        const WCHAR *text = klass == 99 ? disk : partition;
        USHORT bytes = (USHORT)(klass == 99 ? sizeof(disk) : sizeof(partition));
        UNICODE_STRING *string = (UNICODE_STRING *)b.Bytes;
        string->Length = (USHORT)(bytes - sizeof(WCHAR));
        /* measured on windows 11 26300: the room of the string is the caller's block past its header. */
        string->MaximumLength = (USHORT)(length - sizeof(UNICODE_STRING));
        string->Buffer = (PWSTR)((PUCHAR)info + sizeof(UNICODE_STRING));
        memcpy(b.Bytes + sizeof(UNICODE_STRING), text, bytes);
        size = sizeof(UNICODE_STRING) + bytes;
        small = STATUS_BUFFER_TOO_SMALL;
        break;
    }
    case 112: /* 12 in the second dword on the reference machine. */
        b.Longs[1] = 12;
        size = 0x10;
        small = STATUS_BUFFER_TOO_SMALL;
        break;
    case 116: /* SystemLowPriorityIoInformation: no low priority io is told apart. */
        size = 0x28;
        small = STATUS_BUFFER_TOO_SMALL;
        break;
    case 193: { /* the processors that are idle: all but the one that asks. */
        b.Quads[0] = (count >= 64 ? ~0ull : (1ull << count) - 1) & ~(1ull << KeGetCurrentPrcb()->Number);
        size = 8;
        small = STATUS_BUFFER_TOO_SMALL;
        break;
    }
    case 208: { /* a zero dword, whose size is not told. */
        ULONG none = length < 4 ? 0 : 4;
        if (returned)
            KiCopyToUser(returned, &none, sizeof(none));
        *status = length < 4 ? STATUS_INFO_LENGTH_MISMATCH : KiCopyToUser(info, &b, 4);
        return TRUE;
    }
    case 228: /* the two below want more than the four bytes they say, whatever they get. */
    case 229: {
        ULONG said = length < 4 ? 0 : 4;
        if (returned)
            KiCopyToUser(returned, &said, sizeof(said));
        *status = STATUS_INFO_LENGTH_MISMATCH;
        return TRUE;
    }
    default:
        return FALSE;
    }
    if (returned)
        KiCopyToUser(returned, &size, sizeof(size));
    *status = length < size ? small : KiCopyToUser(info, &b, size);
    return TRUE;
}

/* like ProbeForWrite: a byte of every page is read and written back, so a buffer that is not there fails the call. */
static NTSTATUS probe_pages(PVOID info, ULONG length)
{
    if (!length || KeGetPreviousMode() != UserMode)
        return STATUS_SUCCESS;
    ULONG_PTR first = (ULONG_PTR)info, last = first + length - 1;
    for (ULONG_PTR at = first;; at = (at & ~(ULONG_PTR)(PAGE_SIZE - 1)) + PAGE_SIZE) {
        UCHAR byte;
        if (!NT_SUCCESS(KiCopyFromUser(&byte, (PVOID)at, 1)) || !NT_SUCCESS(KiCopyToUser((PVOID)at, &byte, 1)))
            return STATUS_ACCESS_VIOLATION;
        if ((at | (PAGE_SIZE - 1)) >= last)
            return STATUS_SUCCESS;
    }
}

/*
 * the classes d8 to f7, each as windows 11 26300 answers a caller with a limited token: measured with a ladder of
 * lengths, a null buffer, a buffer off its boundary and one that is not there. five of them are refused before the
 * buffer is looked at; the others have it probed first, then the returned length. several take one length only.
 */
static NTSTATUS query_late_class(ULONG klass, PVOID info, ULONG length, PULONG returned)
{
    switch (klass) {
    case 0xde:
    case 0xe7:
    case 0xee:
    case 0xef:
    case 0xf0:
        return STATUS_INVALID_INFO_CLASS;
    default:
        break;
    }
    NTSTATUS status = KiProbeQueryBuffer(info, length, klass == 0xe3 ? 1 : sizeof(ULONG), TRUE);
    if (NT_SUCCESS(status))
        status = probe_pages(info, length);
    if (NT_SUCCESS(status))
        status = KiProbeReturnLength(returned);
    if (!NT_SUCCESS(status))
        return status;
    union {
        UCHAR Bytes[0x20];
        ULONG Longs[8];
    } b = {};
    ULONG size = 0;
    BOOLEAN exact = TRUE, fits;
    NTSTATUS answer = STATUS_SUCCESS;
    switch (klass) {
    case 0xd8: /* fails once it has its room, and its room only. */
        size = 0x20;
        answer = STATUS_UNSUCCESSFUL;
        break;
    case 0xdd: /* SystemShadowStackInformation: 3 on the reference machine. */
        b.Longs[0] = 3;
        size = 4;
        break;
    case 0xe3: /* SystemPoolZeroingInformation: on. */
        b.Bytes[0] = 1;
        size = 1;
        break;
    case 0xe4: /* the two watchdog blocks say the length they were given, and take the one of their version. */
    case 0xe5:
        b.Longs[0] = klass == 0xe4 ? 1 : 2;
        size = klass == 0xe4 ? 0x14 : 0x20;
        if (returned)
            KiCopyToUser(returned, &length, sizeof(length));
        return length != size ? STATUS_INFO_LENGTH_MISMATCH : KiCopyToUser(info, &b, size);
    case 0xea: /* a block of zeros on the reference machine. */
        size = 0x4298;
        exact = FALSE;
        break;
    case 0xf1: /* the signed policies of code integrity: the machine keeps none, so only their length is told. */
        size = 0x8336d;
        if (returned)
            KiCopyToUser(returned, &size, sizeof(size));
        return STATUS_INFO_LENGTH_MISMATCH;
    case 0xf3: /* a block of zeros on the reference machine. */
        size = 0x10;
        exact = FALSE;
        break;
    case 0xf4: /* an empty list, whatever the room. */
        break;
    case 0xe6:
        return STATUS_INVALID_PARAMETER;
    case 0xe8:
    case 0xec:
    case 0xed:
        answer = STATUS_NOT_SUPPORTED;
        break;
    case 0xeb:
        answer = STATUS_ACCESS_DENIED;
        break;
    case 0xf7:
        answer = STATUS_PRIVILEGE_NOT_HELD;
        break;
    default:
        return STATUS_INVALID_INFO_CLASS;
    }
    if (returned)
        KiCopyToUser(returned, &size, sizeof(size));
    fits = exact ? length == size : length >= size;
    if (size && !fits)
        return STATUS_INFO_LENGTH_MISMATCH;
    if (!NT_SUCCESS(answer))
        return answer;
    if (size <= sizeof(b))
        return KiCopyToUser(info, &b, size);
    static const UCHAR zeros[0x400];
    for (ULONG at = 0; at < size && NT_SUCCESS(status); at += sizeof(zeros))
        status = KiCopyToUser((PUCHAR)info + at, zeros, size - at < sizeof(zeros) ? size - at : sizeof(zeros));
    return status;
}

/* ------------------------------------------------------------------ processor topology */

/*
 * the machine as cpuid presents it: one package in one group and one numa node. the apic id of a processor is its
 * index, so the thread bits of leaf 0xB tell which processors are one core and the sharing field of a cache which
 * processors have it. the layouts and the order of the records are the ones windows 11 gives, measured on 26300 for
 * four single thread processors and on 26100 for four cores of two threads.
 */
#define TOPO_MAX_CACHES 8

static ULONG64 topo_all(void)
{
    ULONG n = HalBootInfo->cpu_count;
    return n >= 64 ? ~0ull : (1ull << n) - 1;
}

/* the processors whose apic ids differ from i's in the low bits only. */
static ULONG64 topo_sharing(ULONG i, ULONG bits)
{
    ULONG count = HalBootInfo->cpu_count;
    ULONG64 mask = 0;
    for (ULONG j = 0; j < count && j < 64; j++)
        if (bits >= 32 || j >> bits == i >> bits)
            mask |= 1ull << j;
    return mask;
}

static ULONG64 topo_cache_mask(ULONG i, const KI_CACHE *cache)
{
    if (!cache->Shared)
        return 1ull << i;
    return cache->ShareBits ? topo_sharing(i, cache->ShareBits) : topo_all();
}

/* the first processor of a set is the one its records are told with. */
static BOOLEAN topo_first(ULONG i, ULONG64 mask)
{
    return (mask & ((1ull << i) - 1)) == 0;
}

/* SYSTEM_LOGICAL_PROCESSOR_INFORMATION: the mask, the relationship, then the cache or the node. */
static PUCHAR topo_old_record(PUCHAR at, ULONG64 mask, ULONG relationship, const KI_CACHE *cache)
{
    memset(at, 0, 32);
    memcpy(at, &mask, sizeof(mask));
    memcpy(at + 8, &relationship, sizeof(relationship));
    if (cache) {
        at[16] = cache->Level;
        at[17] = cache->Associativity;
        memcpy(at + 18, &cache->LineSize, sizeof(USHORT));
        memcpy(at + 20, &cache->Size, sizeof(ULONG));
        memcpy(at + 24, &cache->Type, sizeof(ULONG));
    }
    return at + 32;
}

/*
 * class 73. each core with the caches that are its own, the package before the records of the last core, then the
 * caches several cores share, and the node closes the list.
 */
static NTSTATUS query_logical_processors(PVOID info, ULONG length, PULONG returned)
{
    KI_CACHE caches[TOPO_MAX_CACHES];
    ULONG count = HalBootInfo->cpu_count, cache_count = KiQueryCaches(caches, TOPO_MAX_CACHES);
    ULONG smt = KiProcessorSmtBits();
    ULONG need = (count * (1 + 2 * cache_count) + 2) * 32;
    PUCHAR out = ExAllocatePool2(POOL_FLAG_NON_PAGED, need, KI_TAG('T', 'o', 'p', 'o'));
    if (!out)
        return STATUS_INSUFFICIENT_RESOURCES;
    PUCHAR at = out;
    for (ULONG i = 0; i < count; i++) {
        ULONG64 core = topo_sharing(i, smt);
        if (!topo_first(i, core))
            continue;
        if (!(topo_all() & ~core & ~((1ull << i) - 1)))
            at = topo_old_record(at, topo_all(), 3, nullptr);
        PUCHAR record = at;
        at = topo_old_record(at, core, 0, nullptr);
        record[16] = (core & (core - 1)) != 0; /* LTP_PC_SMT */
        for (ULONG k = 0; k < cache_count; k++)
            if (!(topo_cache_mask(i, &caches[k]) & ~core))
                at = topo_old_record(at, core, 2, &caches[k]);
    }
    for (ULONG i = 0; i < count; i++)
        for (ULONG k = 0; k < cache_count; k++) {
            ULONG64 mask = topo_cache_mask(i, &caches[k]);
            if ((mask & ~topo_sharing(i, smt)) && topo_first(i, mask))
                at = topo_old_record(at, mask, 2, &caches[k]);
        }
    at = topo_old_record(at, topo_all(), 1, nullptr);
    need = (ULONG)(at - out);
    if (returned)
        KiCopyToUser(returned, &need, sizeof(need));
    /* a buffer too short still gets the records that fit whole. */
    ULONG fit = length < need ? length / 32 * 32 : need;
    NTSTATUS status = STATUS_SUCCESS;
    /* measured on windows 11 26300: the four bytes that pad the relationship are not written. */
    for (ULONG at = 0; at < fit && NT_SUCCESS(status); at += 32) {
        status = KiCopyToUser((PUCHAR)info + at, out + at, 12);
        if (NT_SUCCESS(status))
            status = KiCopyToUser((PUCHAR)info + at + 16, out + at + 16, 16);
    }
    ExFreePool(out);
    if (!NT_SUCCESS(status))
        return status;
    return length < need ? STATUS_INFO_LENGTH_MISMATCH : STATUS_SUCCESS;
}

/* a record of SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX with its one group mask where the relationship keeps it. */
static PUCHAR topo_ex_record(PUCHAR at, ULONG relationship, ULONG size, ULONG count_at, ULONG64 mask)
{
    memset(at, 0, size);
    memcpy(at, &relationship, sizeof(relationship));
    memcpy(at + 4, &size, sizeof(size));
    if (count_at) {
        at[count_at] = 1;
        memcpy(at + count_at + 2, &mask, sizeof(mask));
    }
    return at + size;
}

static PUCHAR topo_ex_cache(PUCHAR at, const KI_CACHE *cache, ULONG64 mask)
{
    PUCHAR next = topo_ex_record(at, 2, 0x38, 38, mask);
    at[8] = cache->Level;
    at[9] = cache->Associativity;
    memcpy(at + 10, &cache->LineSize, sizeof(USHORT));
    memcpy(at + 12, &cache->Size, sizeof(ULONG));
    memcpy(at + 16, &cache->Type, sizeof(ULONG));
    return next;
}

/*
 * class 107, asked with a relationship. everything is the package, then the core, the module and the caches first met
 * with each core, the node and the group; the die is only told when asked for, and a node asked as 6 answers as 1.
 */
static NTSTATUS query_logical_processors_ex(ULONG relationship, PVOID info, ULONG length, PULONG returned)
{
    enum { CORE, NODE, CACHE, PACKAGE, GROUP, DIE, NODE_EX, MODULE, ALL = 0xFFFF };
    ULONG zero = 0;
    if (relationship > MODULE && relationship != ALL) {
        if (returned)
            KiCopyToUser(returned, &zero, sizeof(zero));
        return STATUS_UNSUCCESSFUL;
    }
    KI_CACHE caches[TOPO_MAX_CACHES];
    ULONG count = HalBootInfo->cpu_count, cache_count = KiQueryCaches(caches, TOPO_MAX_CACHES);
    ULONG smt = KiProcessorSmtBits();
    ULONG need = count * (0x60 + cache_count * 0x38) + cache_count * 0x38 + 0x100;
    PUCHAR out = ExAllocatePool2(POOL_FLAG_NON_PAGED, need, KI_TAG('T', 'o', 'p', 'o'));
    if (!out)
        return STATUS_INSUFFICIENT_RESOURCES;
    BOOLEAN all = relationship == ALL;
    ULONG64 everyone = topo_all();
    PUCHAR at = out;
    if (all || relationship == PACKAGE)
        at = topo_ex_record(at, PACKAGE, 0x30, 30, everyone);
    if (relationship == DIE)
        at = topo_ex_record(at, DIE, 0x30, 30, everyone);
    for (ULONG i = 0; i < count; i++) {
        ULONG64 core = topo_sharing(i, smt);
        if (topo_first(i, core)) {
            if (all || relationship == CORE) {
                PUCHAR record = at;
                at = topo_ex_record(at, CORE, 0x30, 30, core);
                record[8] = (core & (core - 1)) != 0; /* LTP_PC_SMT */
            }
            if (all || relationship == MODULE)
                at = topo_ex_record(at, MODULE, 0x30, 30, core);
        }
        for (ULONG k = 0; k < cache_count && (all || relationship == CACHE); k++) {
            ULONG64 mask = topo_cache_mask(i, &caches[k]);
            if (topo_first(i, mask))
                at = topo_ex_cache(at, &caches[k], mask);
        }
    }
    if (all || relationship == NODE || relationship == NODE_EX)
        at = topo_ex_record(at, NODE, 0x30, 30, everyone);
    if (all || relationship == GROUP) {
        PUCHAR group = at;
        at = topo_ex_record(at, GROUP, 0x50, 0, 0);
        group[8] = group[10] = 1;
        group[32] = group[33] = (UCHAR)count;
        memcpy(group + 72, &everyone, sizeof(everyone));
    }
    need = (ULONG)(at - out);
    if (returned)
        KiCopyToUser(returned, &need, sizeof(need));
    NTSTATUS status = length < need ? STATUS_INFO_LENGTH_MISMATCH : KiCopyToUser(info, out, need);
    ExFreePool(out);
    return status;
}

/* class 175, SYSTEM_CPU_SET_INFORMATION: a set for each processor, with the first processor of its core and of its
   last level cache. */
static NTSTATUS query_cpu_sets(PVOID info, ULONG length, PULONG returned)
{
    struct {
        ULONG Size, Type, Id;
        USHORT Group;
        UCHAR LogicalProcessorIndex, CoreIndex, LastLevelCacheIndex, NumaNodeIndex, EfficiencyClass, Flags;
        ULONG SchedulingClass;
        ULONG64 AllocationTag;
    } sets[MAXIMUM_PROCESSORS] = {};
    KI_CACHE caches[TOPO_MAX_CACHES];
    ULONG count = HalBootInfo->cpu_count, need = count * sizeof(sets[0]);
    ULONG cache_count = KiQueryCaches(caches, TOPO_MAX_CACHES), smt = KiProcessorSmtBits();
    for (ULONG i = 0; i < count; i++) {
        ULONG64 core = topo_sharing(i, smt);
        ULONG64 last = cache_count ? topo_cache_mask(i, &caches[cache_count - 1]) : 1ull << i;
        sets[i].Size = sizeof(sets[0]);
        sets[i].Id = 0x100 + i;
        sets[i].LogicalProcessorIndex = (UCHAR)i;
        sets[i].CoreIndex = (UCHAR)__builtin_ctzll(core);
        sets[i].LastLevelCacheIndex = (UCHAR)__builtin_ctzll(last);
    }
    if (returned)
        KiCopyToUser(returned, &need, sizeof(need));
    return length < need ? STATUS_BUFFER_TOO_SMALL : KiCopyToUser(info, sets, need);
}

/*
 * SystemLookasideInformation, measured on windows 11 26300: the lists that fit whole in the room, and the length of
 * what was told. windows opens the list with the lookasides of the general pool of each processor, one for each of the
 * 32 block sizes in the nonpaged, the nonpaged no-execute and the paged pool, every one four deep; the lists its
 * drivers make follow. this kernel keeps no lookaside of its own, so the pool's are told and no count of theirs moves.
 */
static NTSTATUS query_lookaside(PVOID info, ULONG length, PULONG returned)
{
    struct {
        USHORT CurrentDepth, MaximumDepth;
        ULONG TotalAllocates, AllocateMisses, TotalFrees, FreeMisses, Type, Tag, Size;
    } list = {.MaximumDepth = 4, .Tag = KI_TAG('P', 'o', 'o', 'L')};
    static const ULONG types[3] = {0, 0x200, 1};
    ULONG count = HalBootInfo->cpu_count * 32 * 3, fit = length / sizeof(list) < count ? length / sizeof(list) : count;
    NTSTATUS status = STATUS_SUCCESS;
    for (ULONG i = 0; i < fit && NT_SUCCESS(status); i++) {
        list.Type = types[i % 3];
        list.Size = (i / 3 % 32 + 1) * 0x10;
        status = KiCopyToUser((PUCHAR)info + i * sizeof(list), &list, sizeof(list));
    }
    ULONG told = fit * sizeof(list);
    if (NT_SUCCESS(status) && returned)
        KiCopyToUser(returned, &told, sizeof(told));
    return status;
}

/*
 * SystemProcessorPowerInformation, a block of 0x50 bytes for each processor, measured on windows 11 26300: the blocks
 * are told to a room that holds them all. the reference machine keeps no frequency states, so a block is the time
 * its processor has run and the time it has idled.
 */
static NTSTATUS query_processor_power(PVOID info, ULONG length, PULONG returned)
{
    ULONG64 power[MAXIMUM_PROCESSORS][10] = {};
    ULONG count = HalBootInfo->cpu_count;
    for (ULONG i = 0; i < count; i++) {
        if (!KiProcessorBlock[i])
            continue;
        power[i][5] = KiQueryInterruptTime();                    /* CurrentProcessorTime */
        power[i][6] = processor_idle_time(KiProcessorBlock[i]);  /* CurrentProcessorIdleTime */
    }
    return copy_info(info, power, count * sizeof(power[0]), length, returned);
}

/*
 * SystemPerformanceTraceInformation: the caller's block opens with the class of what it asks of event tracing. measured
 * on windows 11 26300 with a limited token, class by class: the lengths each takes, what it tells, and how it refuses.
 * no session traces the kernel here, so the ones that ask about a session find none, as on the reference machine.
 */
static NTSTATUS query_trace(PVOID info, ULONG length, PULONG returned)
{
    ULONG klass, told = 0;
    ULONG block[4] = {};
    NTSTATUS status = STATUS_INFO_LENGTH_MISMATCH;
    if (length < sizeof(klass)) {
        if (returned)
            KiCopyToUser(returned, &told, sizeof(told));
        return STATUS_INVALID_PARAMETER;
    }
    status = KiCopyFromUser(&klass, info, sizeof(klass));
    if (!NT_SUCCESS(status))
        return status;
    status = STATUS_INFO_LENGTH_MISMATCH;
    block[0] = klass;
    ULONG write_at = 0, write_end = 0;
    switch (klass) {
    case 0: /* EventTraceKernelVersionInformation */
        told = 8;
        block[1] = 0x55;
        if (length == 8)
            status = STATUS_SUCCESS, write_end = 8;
        break;
    case 1: /* the three below ask about a logger, and none is found. */
        told = 0x30;
        if (length == 0x30)
            status = (NTSTATUS)0xC0000296;
        break;
    case 4:
        told = 0x18;
        if (length >= 0x18)
            status = (NTSTATUS)0xC0000296;
        break;
    case 6:
    case 10:
        if (length >= 0x10)
            status = (NTSTATUS)0xC0000296;
        break;
    case 2: /* EventTracePerformanceInformation: the bytes the loggers wrote, past four bytes left alone. */
        told = 0x10;
        if (length == 0x10)
            status = STATUS_SUCCESS, write_at = 8, write_end = 0x10;
        break;
    case 3: /* EventTraceTimeProfileInformation: the interval of the profile. */
        told = 8;
        block[1] = 10000;
        if (length == 8)
            status = STATUS_SUCCESS, write_end = 8;
        break;
    case 5: /* the two below: a count of 1000, and two flags set. */
        told = 0x14;
        block[1] = 1;
        block[2] = 1000;
        block[3] = 1;
        if (length == 0x10)
            status = STATUS_SUCCESS, write_end = 0x10;
        break;
    case 7:
        told = 0x10;
        block[1] = 1000;
        block[2] = 1;
        block[3] = 1;
        if (length == 0x10)
            status = STATUS_SUCCESS, write_end = 0x10;
        break;
    case 8: /* succeeds on its class alone, and tells nothing. */
        if (length >= 8)
            status = STATUS_SUCCESS, told = 4;
        break;
    case 9: { /* the identifiers of the processes, after the class. */
        if (length < 8)
            break;
        ULONG ids[1024], count = 0;
        ULONG64 lock = KiAcquireDispatcherLock();
        for (PLIST_ENTRY e = PsActiveProcessHead.Flink; e != &PsActiveProcessHead && count < RTL_NUMBER_OF(ids); e = e->Flink) {
            PEPROCESS p = CONTAINING_RECORD(e, EPROCESS, ActiveProcessLinks);
            if (!p->Exited)
                ids[count++] = (ULONG)(ULONG_PTR)p->UniqueProcessId;
        }
        KiReleaseDispatcherLock(lock);
        told = sizeof(klass) + count * sizeof(ULONG);
        ULONG fit = (length - sizeof(klass)) / sizeof(ULONG) < count ? (length - sizeof(klass)) / sizeof(ULONG) : count;
        status = KiCopyToUser((PUCHAR)info + sizeof(klass), ids, fit * sizeof(ULONG));
        if (NT_SUCCESS(status) && length < told)
            status = STATUS_BUFFER_TOO_SMALL;
        break;
    }
    case 12: /* takes a room up to 0x10 bytes and tells nothing, and finds a longer one too small. */
        told = 0x10;
        status = length <= 0x10 ? STATUS_SUCCESS : STATUS_BUFFER_TOO_SMALL;
        break;
    case 13: { /* the profile source: its interval limits and its name, which runs past the length it tells. */
        static const struct {
            ULONG Pad[2], Minimum, Maximum;
            ULONG64 Reserved;
            WCHAR Name[6];
        } source = {.Minimum = 0x4C5, .Maximum = 1000000, .Name = L"Timer"};
        if (length < 0x28)
            break;
        told = 0x28;
        if (length >= 0x30)
            status = KiCopyToUser((PUCHAR)info + 8, &source, __builtin_offsetof(typeof(source), Name) + sizeof(source.Name));
        break;
    }
    case 18:
        status = STATUS_NOT_SUPPORTED;
        break;
    case 25:
        if (length >= 0x10)
            status = STATUS_NOT_SUPPORTED;
        break;
    case 28: /* a block of zeros at its one length. */
        told = 0xC;
        if (length == 0xC)
            status = STATUS_SUCCESS, write_at = 4, write_end = 0xC;
        break;
    default:
        status = STATUS_NOT_IMPLEMENTED;
        break;
    }
    if (write_end > write_at && !NT_SUCCESS(KiCopyToUser((PUCHAR)info + write_at, (PUCHAR)block + write_at, write_end - write_at)))
        return STATUS_ACCESS_VIOLATION;
    if (returned)
        KiCopyToUser(returned, &told, sizeof(told));
    return status;
}

static NTSTATUS query_system_information(SYSTEM_INFORMATION_CLASS klass, PVOID info, ULONG length, PULONG returned)
{
    if ((ULONG)klass >= 0xd8 && (ULONG)klass <= 0xf7)
        return query_late_class((ULONG)klass, info, length, returned);
    switch ((ULONG)klass) {
    case SystemHandleInformation:
        return query_handles(FALSE, info, length, returned);
    case SystemExtendedHandleInformation:
        return query_handles(TRUE, info, length, returned);
    case SystemProcessInformation:
        return query_processes(FALSE, info, length, returned);
    case 57: /* SystemExtendedProcessInformation */
        return query_processes(TRUE, info, length, returned);
    case 0xfc: /* SystemBasicProcessInformation */
        return query_basic_processes(info, length, returned);
    case SystemModuleInformation:
        return query_modules(FALSE, info, length, returned);
    case 77: /* SystemModuleInformationEx */
        return query_modules(TRUE, info, length, returned);
    case SystemPageFileInformation:
    case 144: /* SystemPageFileInformationEx */
        /* no paging file: the host swaps guest memory, so the list is empty. */
        return copy_info(info, nullptr, 0, length, returned);
    case SystemBasicInformation:
    case SystemEmulationBasicInformation:
    case 114: { /* SystemNativeBasicInformation */
        SYSTEM_BASIC_INFORMATION b = {};
        b.TimerResolution = KI_CLOCK_TICK_HNS;
        b.PageSize = PAGE_SIZE;
        b.NumberOfPhysicalPages = (ULONG)MmGetTotalPages();
        b.LowestPhysicalPageNumber = 1;
        b.HighestPhysicalPageNumber = highest_physical_page();
        b.AllocationGranularity = 0x10000;
        b.MinimumUserModeAddress = 0x10000;
        b.MaximumUserModeAddress = 0x7FFFFFFEFFFF;
        /* measured on windows 11 26300: the emulation class gives a 32-bit program the end of its own address space,
           which the 64-bit system dll sizes its tables of the address space from. */
        ULONG_PTR limit = PsCurrentProcess()->AddressSpace.AllocationLimit;
        if ((ULONG)klass == SystemEmulationBasicInformation && limit)
            b.MaximumUserModeAddress = limit - 1;
        b.ActiveProcessorsAffinityMask = HalBootInfo->cpu_count >= 64 ? ~0ull : (1ull << HalBootInfo->cpu_count) - 1;
        b.NumberOfProcessors = (CCHAR)HalBootInfo->cpu_count;
        /* the four bytes that pad the granularity are not written. */
        if (returned) {
            ULONG size = sizeof(b);
            KiCopyToUser(returned, &size, sizeof(size));
        }
        if (length < sizeof(b))
            return STATUS_INFO_LENGTH_MISMATCH;
        ULONG head = FIELD_OFFSET(SYSTEM_BASIC_INFORMATION, AllocationGranularity) + sizeof(b.AllocationGranularity);
        ULONG tail = FIELD_OFFSET(SYSTEM_BASIC_INFORMATION, MinimumUserModeAddress);
        NTSTATUS status = KiCopyToUser(info, &b, head);
        if (NT_SUCCESS(status))
            status = KiCopyToUser((PUCHAR)info + tail, (PUCHAR)&b + tail, sizeof(b) - tail);
        return status;
    }
    case SystemProcessorPerformanceInformation: {
        /* per processor, kernel time includes idle, interrupt and dpc time, as on windows. */
        typedef struct {
            LARGE_INTEGER IdleTime, KernelTime, UserTime, DpcTime, InterruptTime;
            ULONG InterruptCount;
        } PERFORMANCE;
        PERFORMANCE p[MAXIMUM_PROCESSORS];
        ULONG count = HalBootInfo->cpu_count, size = count * sizeof(PERFORMANCE);
        ULONG64 now = KiQueryInterruptTime();
        memset(p, 0, sizeof(p));
        for (ULONG i = 0; i < count; i++) {
            PKPRCB prcb = KiProcessorBlock[i];
            if (!prcb)
                continue;
            /* the running thread's time since its last tick is busy too, though not charged yet, and idle time never
               goes back, as the time of the idle thread on windows. */
            ULONG64 busy = prcb->KernelTime + prcb->UserTime;
            PKTHREAD running = prcb->CurrentThread;
            ULONG64 since = prcb->TickTime;
            if (running && running != prcb->IdleThread && now > since)
                busy += now - since;
            ULONG64 idle = now > busy ? now - busy : 0;
            static ULONG64 reported[MAXIMUM_PROCESSORS];
            ULONG64 seen = __atomic_load_n(&reported[i], __ATOMIC_RELAXED);
            while (seen < idle && !__atomic_compare_exchange_n(&reported[i], &seen, idle, FALSE, __ATOMIC_RELAXED,
                                                                __ATOMIC_RELAXED))
                ;
            if (seen > idle)
                idle = seen;
            /* windows charges whole clock ticks. */
            idle -= idle % KI_CLOCK_TICK_HNS;
            p[i].IdleTime.QuadPart = (LONGLONG)idle;
            p[i].KernelTime.QuadPart = (LONGLONG)(prcb->KernelTime - prcb->KernelTime % KI_CLOCK_TICK_HNS + idle);
            p[i].UserTime.QuadPart = (LONGLONG)(prcb->UserTime - prcb->UserTime % KI_CLOCK_TICK_HNS);
            p[i].DpcTime.QuadPart = (LONGLONG)prcb->DpcTime;
            p[i].InterruptTime.QuadPart = (LONGLONG)prcb->InterruptTime;
            p[i].InterruptCount = (ULONG)prcb->InterruptCount;
        }
        /* measured on windows 11 26300: the blocks are taken whole, so a room that is not a number of them is told the
           size of all; a shorter one gets the processors that fit, and the four bytes that pad a block are not written. */
        if (length < sizeof(PERFORMANCE) || length % sizeof(PERFORMANCE)) {
            if (returned)
                KiCopyToUser(returned, &size, sizeof(size));
            return STATUS_INFO_LENGTH_MISMATCH;
        }
        if (length < size)
            size = length;
        if (returned)
            KiCopyToUser(returned, &size, sizeof(size));
        NTSTATUS status = STATUS_SUCCESS;
        for (ULONG i = 0; i < size / sizeof(PERFORMANCE) && NT_SUCCESS(status); i++)
            status = KiCopyToUser((PUCHAR)info + i * sizeof(PERFORMANCE), &p[i], __builtin_offsetof(PERFORMANCE, InterruptCount) + sizeof(ULONG));
        return status;
    }
    case SystemProcessorInformation:
    case SystemEmulationProcessorInformation: {
        SYSTEM_PROCESSOR_INFORMATION p = {};
        /* the emulation class reports the x86 processor wow64 runs on; the native one the real architecture. */
        p.ProcessorArchitecture = klass == SystemEmulationProcessorInformation ? 0 : MIME_NATIVE_ARCHITECTURE;
        p.ProcessorLevel = KiProcessorLevel();
        p.ProcessorRevision = KiProcessorRevision();
        p.MaximumProcessors = (USHORT)HalBootInfo->cpu_count;
        p.ProcessorFeatureBits = KiProcessorFeatureBits();
        return copy_info(info, &p, sizeof(p), length, returned);
    }
    case SystemTimeOfDayInformation: {
        struct {
            LARGE_INTEGER BootTime;
            LARGE_INTEGER CurrentTime;
            LARGE_INTEGER TimeZoneBias;
            ULONG TimeZoneId;
            ULONG Reserved;
            ULONGLONG BootTimeBias;
            ULONGLONG SleepTimeBias;
        } t = {};
        t.BootTime.QuadPart = (LONGLONG)KeBootFiletime;
        KeQuerySystemTime(&t.CurrentTime);
        t.TimeZoneBias.QuadPart = (LONGLONG)SharedUserData->TimeZoneBias.LowPart |
                                  (LONGLONG)SharedUserData->TimeZoneBias.High1Time << 32;
        t.TimeZoneId = SharedUserData->TimeZoneId;
        /* measured on windows 11 26300: a shorter buffer gets its length of the block and success, a longer one fails. */
        if (length > sizeof(t))
            return copy_info(info, &t, sizeof(t), 0, returned);
        return copy_info(info, &t, length, length, returned);
    }
    case 45:
        return query_lookaside(info, length, returned);
    case 128: /* SystemBadPageInformation: no page went bad. */
    case 244: /* an empty list on 26300, whatever the room. */
        return copy_info(info, nullptr, 0, length, returned);
    case SystemPerformanceInformation: {
        UCHAR perf[0x178] = {};
        /* no paging file: the host swaps guest memory, so the limit is ram plus the host's swap. */
        ULONG64 free = MmGetAvailablePages();
        ULONG64 idle = 0, switches = 0;
        for (ULONG i = 0; i < HalBootInfo->cpu_count; i++) {
            if (!KiProcessorBlock[i])
                continue;
            idle += processor_idle_time(KiProcessorBlock[i]);
            switches += KiProcessorBlock[i]->ContextSwitches;
        }
        *(PULONG64)perf = idle;                                     /* IdleProcessTime */
        *(PULONG)(perf + 0x2C) = (ULONG)free;                       /* AvailablePages */
        *(PULONG)(perf + 0x30) = (ULONG)MmGetCommittedPages();      /* CommittedPages */
        *(PULONG)(perf + 0x34) = (ULONG)MmGetCommitLimitPages();    /* CommitLimit */
        *(PULONG)(perf + 0x38) = (ULONG)MmGetPeakCommittedPages();  /* PeakCommitment */
        *(PULONG)(perf + 0x128) = (ULONG)switches;                  /* ContextSwitches */
        /* kernelbase of this tree still reads the four memory figures 0x20 further, where windows counts page faults:
           they stay there too until it reads them at their place. */
        *(PULONG)(perf + 0x4C) = (ULONG)free;
        *(PULONG)(perf + 0x50) = (ULONG)MmGetCommittedPages();
        *(PULONG)(perf + 0x54) = (ULONG)MmGetCommitLimitPages();
        *(PULONG)(perf + 0x58) = (ULONG)MmGetPeakCommittedPages();
        /* 0x178 bytes on 26300, 0x138 before: a caller with the room of an older build gets what its room holds. */
        return copy_info(info, perf, length >= 0x138 && length < sizeof(perf) ? length : sizeof(perf), length, returned);
    }
    case SystemFileCacheInformation:
    case 81:    /* SystemFileCacheInformationEx */
    case 119:   /* the two working sets beside the cache answer in the same block. */
    case 120: {
        /* the system cache keeps no working set of its own, so its size is what the host has cached for the guest. */
        ULONG64 cache[8] = {};
        cache[0] = MmGetFreePages() * PAGE_SIZE / 8;
        cache[1] = cache[0];
        cache[3] = 0x100;         /* MinimumWorkingSet, in pages */
        cache[4] = 0x100000000;   /* MaximumWorkingSet */
        cache[5] = cache[0] >> PAGE_SHIFT;
        cache[6] = cache[1] >> PAGE_SHIFT;
        return copy_info(info, cache, sizeof(cache), length, returned);
    }
    case SystemKernelDebuggerInformation: {
        /* KdDebuggerEnabled then KdDebuggerNotPresent, which windows sets when it boots with no debugger. */
        BOOLEAN dbg[2] = {FALSE, TRUE};
        return copy_info(info, dbg, sizeof(dbg), length, returned);
    }
    case SystemRangeStartInformation: {
        ULONG_PTR start = 0xFFFF800000000000ull;
        return copy_info(info, &start, sizeof(start), length, returned);
    }
    case SystemLogicalProcessorInformation:
        return query_logical_processors(info, length, returned);
    case SystemNumaHighestNodeInformation: {
        /* the highest node, and with the room for it the processors of node 0 as a group affinity. */
        struct {
            ULONG HighestNode, Hole;
            ULONG64 Mask;
            USHORT Group, Reserved[3];
        } map = {.Mask = topo_all()};
        return copy_info(info, &map, length < sizeof(map) ? sizeof(ULONG) : sizeof(map), length, returned);
    }
    case SystemFirmwareTableInformation:
        return query_firmware_tables(info, length, returned);
    case SystemBootEnvironmentInformation: {
        /* the identifier of the boot application, the firmware type and the boot flags. */
        struct {
            GUID BootIdentifier;
            ULONG FirmwareType;
            ULONG Pad;
            ULONG64 BootFlags;
        } boot = {};
        boot.FirmwareType = ExpFirmwareType;
        /* the identifier of the boot entry, which windows keeps from one start to the next: a time based guid, made
           here from the machine's firmware uuid so it is stable and its own. */
        memcpy(&boot.BootIdentifier, HalBootInfo->system_uuid, sizeof(boot.BootIdentifier));
        boot.BootIdentifier.Data1 ^= 0x2C419486;
        boot.BootIdentifier.Data2 ^= 0xE063;
        boot.BootIdentifier.Data3 = (USHORT)((boot.BootIdentifier.Data3 & 0x0FFF) | 0x1000);
        boot.BootIdentifier.Data4[0] = (UCHAR)((boot.BootIdentifier.Data4[0] & 0x3F) | 0x80);
        ULONG need = sizeof(boot);
        if (returned)
            KiCopyToUser(returned, &need, sizeof(need));
        if (length < 0x14)
            return STATUS_INFO_LENGTH_MISMATCH;
        NTSTATUS status = KiCopyToUser(info, &boot, 0x14);
        if (!NT_SUCCESS(status))
            return status;
        if (length < need) {
            /* measured on windows 11 26300: a room short of the flags holds the identifier and the type, and says so. */
            need = 0x14;
            if (returned)
                KiCopyToUser(returned, &need, sizeof(need));
            return STATUS_SUCCESS;
        }
        return KiCopyToUser((PUCHAR)info + 0x18, &boot.BootFlags, sizeof(boot.BootFlags));
    }
    case SystemSecureBootInformation: {
        /* SecureBootEnabled and SecureBootCapable. */
        UCHAR secure[2] = {1, 1};
        return copy_info(info, secure, sizeof(secure), length, returned);
    }
    case 44: /* SystemCurrentTimeZoneInformation */
        return ExQueryTimeZoneInformation(FALSE, info, length, returned);
    case 102: /* SystemDynamicTimeZoneInformation */
        return ExQueryTimeZoneInformation(TRUE, info, length, returned);
    case 31:
        return query_trace(info, length, returned);
    case 61:
        return query_processor_power(info, length, returned);
    case 88: /* SystemProcessIdInformation */
        return query_process_id(info, length, returned);
    case 103: { /* SystemCodeIntegrityInformation */
        /* like ci.dll: the caller sets the length, the options are code integrity on, no test or debug signing. measured on windows. */
        ULONG ci[2];
        ULONG need = sizeof(ci);
        if (length != sizeof(ci)) {
            if (returned)
                KiCopyToUser(returned, &need, sizeof(need));
            return STATUS_INFO_LENGTH_MISMATCH;
        }
        NTSTATUS status = KiCopyFromUser(ci, info, sizeof(ci));
        if (!NT_SUCCESS(status))
            return status;
        if (ci[0] != sizeof(ci)) {
            if (returned)
                KiCopyToUser(returned, &need, sizeof(need));
            return STATUS_INFO_LENGTH_MISMATCH;
        }
        ci[1] = 0x00000001; /* enabled, and for user mode code too, as windows 11 26100 answers. */
        return copy_info(info, ci, sizeof(ci), length, returned);
    }
    case 183: {
        /*
         * SystemCodeIntegrityCertificateInformation, measured on windows 11 26100: a handle of a file
         * and a kind in a block of 16 bytes, which stays as it was, and a returned length of 0. kind 0 asks whether
         * windows itself signed the file: 0 for a file of the system, c0000428 for any other image, signed or not,
         * c000007b for a file that is no image. kind 1 is refused and kind 2 succeeds whatever the file. no signature
         * is checked here: the files the system came with are the ones windows signs.
         */
        struct {
            HANDLE File;
            ULONG Kind, Pad;
        } cert;
        if (length != sizeof(cert))
            return STATUS_INFO_LENGTH_MISMATCH;
        NTSTATUS status = KiCopyFromUser(&cert, info, sizeof(cert));
        if (!NT_SUCCESS(status))
            return status;
        ULONG none = 0;
        if (returned)
            KiCopyToUser(returned, &none, sizeof(none));
        if (cert.Kind > 2)
            return STATUS_INVALID_PARAMETER;
        PFILE_OBJECT file;
        status = ObReferenceHandleFor(cert.File, FILE_READ_DATA, IoFileObjectTypeObject, KeGetPreviousMode(), &file);
        if (!NT_SUCCESS(status))
            return status;
        if (cert.Kind == 1) {
            status = (NTSTATUS)0xC0E90002;
        } else if (cert.Kind == 0) {
            PUCHAR headers = ExAllocatePool2(POOL_FLAG_NON_PAGED, PAGE_SIZE, KI_TAG('C', 'i', 'H', 'd'));
            MI_IMAGE_INFO image;
            ULONG got = 0;
            if (!headers)
                status = STATUS_INSUFFICIENT_RESOURCES;
            else if (!NT_SUCCESS(IoReadFileObject(file, headers, PAGE_SIZE, 0, &got)) ||
                     !NT_SUCCESS(MiParseImageHeaders(headers, got, &image)))
                status = STATUS_INVALID_IMAGE_FORMAT;
            else
                status = IoGetRomFile(file) ? STATUS_SUCCESS : STATUS_INVALID_IMAGE_HASH;
            if (headers)
                ExFreePool(headers);
        }
        ObDereferenceObject(file);
        return status;
    }
    case 164: {
        /* SystemCodeIntegrityPolicyInformation as windows 11 26300 has it with no policy of the owner: its options,
           no hypervisor enforcement, the version of the policy, and no policy guid. */
        ULONG state[8] = {0x40001631, 0, 0, 0x000A73AB};
        /* windows takes this block at its exact size only. */
        return copy_info(info, state, sizeof(state), length == sizeof(state) ? length : 0, returned);
    }
    case 165: {
        /* SystemLeapSecondInformation on 26300: a 16-byte block whose first dword is its own size, leap seconds off. */
        UCHAR data[16] = {};
        *(ULONG *)data = sizeof(data);
        return copy_info(info, data, sizeof(data), length, returned);
    }
    case SystemHypervisorInformation: {
        /* a physical machine answers, with nothing present; a root partition is connected to its hypervisor. */
        ULONG leaves[1][4];
        SYSTEM_HYPERVISOR_QUERY_INFORMATION hypervisor = {};
        if (KiQueryHypervisorLeaves(leaves, 1))
            hypervisor.HypervisorConnected = hypervisor.HypervisorPresent = TRUE;
        return copy_info(info, &hypervisor, sizeof(hypervisor), length, returned);
    }
    case SystemHypervisorDetailInformation: {
        /*
         * the hypervisor leaves in windows' order: vendor and maximum, interface,
         * version, features, hardware features, enlightenments, limits. without
         * microsoft's hypervisor windows reports none.
         */
        ULONG leaves[7][4] = {}, out[7][4] = {};
        if (!KiQueryHypervisorLeaves(leaves, 7))
            return copy_info(info, out, sizeof(out), length, returned);
        static const UCHAR order[7] = {0, 1, 2, 3, 6, 4, 5};
        for (ULONG i = 0; i < 7; i++)
            memcpy(out[i], leaves[order[i]], sizeof(out[i]));
        return copy_info(info, out, sizeof(out), length, returned);
    }
    default: {
        NTSTATUS small;
        if (query_small_block((ULONG)klass, info, length, returned, &small))
            return small;
        /*
         * the class is not handled here. a caller that sweeps the classes sees which of them exist and what the
         * kernel says about each, so answer with what the reference kernel answers: the same status, and the
         * same required length when the call simply wanted more room.
         */
        static const struct { USHORT klass; ULONG status, need; } reference[] = {
{0x0, 0xc0000004, 0x40},
{0x1, 0xc0000004, 0xc},
{0x2, 0xc0000004, 0x178},
{0x4, 0xc0000002, 0x0},
{0x5, 0xc0000004, 0x2b990},
{0x6, 0xc00000bb, 0x0},
{0x7, 0xc0000004, 0x18},
{0x8, 0xc0000004, 0xc0},
{0x9, 0xc0000004, 0x4},
{0xa, 0xc0000002, 0x0},
{0xb, 0xc0000004, 0xc248},
{0xc, 0xc0000004, 0x38},
{0xd, 0xc0000002, 0x0},
{0xe, 0xc0000002, 0x0},
{0xf, 0xc0000002, 0x0},
{0x10, 0xc0000004, 0x20},
{0x11, 0xc0000004, 0x40},
{0x12, 0xc0000004, 0x20},
{0x13, 0xc0000002, 0x0},
{0x14, 0xc0000003, 0x0},
{0x15, 0xc0000004, 0x40},
{0x16, 0xc0000004, 0x30},
{0x17, 0xc0000004, 0x60},
{0x18, 0xc0000004, 0x14},
{0x19, 0xc0000002, 0x0},
{0x1a, 0xc0000003, 0x0},
{0x1b, 0xc0000003, 0x0},
{0x1c, 0xc0000004, 0xc},
{0x1d, 0xc0000002, 0x0},
{0x1e, 0xc0000003, 0x0},
{0x1f, 0xc000000d, 0x0},
{0x20, 0xc0000003, 0x0},
{0x21, 0xc0000004, 0x10},
{0x22, 0xc0000003, 0x0},
{0x23, 0xc0000004, 0x2},
{0x24, 0xc0000004, 0x30},
{0x25, 0xc0000004, 0x10},
{0x26, 0xc0000003, 0x0},
{0x27, 0xc0000003, 0x0},
{0x28, 0xc0000003, 0x0},
{0x29, 0xc0000003, 0x0},
{0x2a, 0xc0000004, 0xc0},
{0x2b, 0xc0000004, 0x18},
{0x2c, 0xc0000004, 0xac},
{0x2e, 0xc0000003, 0x0},
{0x2f, 0xc0000003, 0x0},
{0x30, 0xc0000003, 0x0},
{0x31, 0xc0000003, 0x0},
{0x32, 0xc0000004, 0x8},
{0x33, 0xc0000004, 0x90},
{0x34, 0xc0000003, 0x0},
{0x35, 0xc0000004, 0x10},
{0x36, 0xc0000003, 0x0},
{0x37, 0xc0000004, 0x4},
{0x38, 0xc0000004, 0x20},
{0x39, 0xc0000004, 0x3bee0},
{0x3a, 0xc0000004, 0x4},
{0x3b, 0xc0000004, 0x4},
{0x3c, 0xc0000004, 0x4},
{0x3d, 0xc0000004, 0x140},
{0x3e, 0xc0000004, 0x40},
{0x3f, 0xc0000004, 0xc},
{0x40, 0xc0000004, 0x38},
{0x41, 0xc0000004, 0x4},
{0x42, 0xc0000004, 0x20},
{0x43, 0xc0000003, 0x0},
{0x44, 0xc0000003, 0x0},
{0x45, 0xc00000bb, 0x0},
{0x46, 0xc0000004, 0x4},
{0x47, 0xc0000003, 0x0},
{0x48, 0xc000000d, 0x0},
{0x49, 0xc0000004, 0x2c0},
{0x4a, 0xc0000003, 0x0},
{0x4b, 0xc0000003, 0x0},
{0x4c, 0xc0000004, 0x10},
{0x4d, 0xc0000004, 0xd202},
{0x4e, 0xc0000003, 0x0},
{0x4f, 0xc0000004, 0x0},
{0x50, 0xc0000004, 0xb0},
{0x51, 0xc0000004, 0x40},
{0x52, 0xc0000003, 0x0},
{0x53, 0xc0000023, 0x20},
{0x54, 0xc0000003, 0x0},
{0x55, 0xc0000003, 0x0},
{0x56, 0xc0000004, 0x28},
{0x57, 0xc0000004, 0x8},
{0x58, 0xc0000004, 0x18},
{0x59, 0xc0000003, 0x0},
{0x5a, 0xc0000004, 0x20},
{0x5b, 0xc00000f0, 0x0},
{0x5c, 0xc0000004, 0x28},
{0x5d, 0xc0000003, 0x0},
{0x5e, 0xc0000003, 0x0},
{0x5f, 0xc00000bb, 0x0},
{0x60, 0xc0000002, 0x0},
{0x61, 0xc0000003, 0x0},
{0x62, 0xc0000023, 0x40},
{0x63, 0xc0000023, 0x3c},
{0x64, 0xc0000004, 0x38},
{0x65, 0xc0000004, 0x8},
{0x66, 0xc0000004, 0x1b0},
{0x67, 0xc0000004, 0x8},
{0x68, 0xc0000003, 0x0},
{0x69, 0xc0000004, 0x31},
{0x6a, 0xc0000003, 0x0},
{0x6b, 0xc0000003, 0x0},
{0x6c, 0xc0000023, 0x20},
{0x6d, 0xc0000206, 0x0},
{0x6e, 0xc0000003, 0x0},
{0x6f, 0xc0000003, 0x0},
{0x70, 0xc0000023, 0x10},
{0x71, 0xc00001a9, 0x0},
{0x72, 0xc0000004, 0x40},
{0x73, 0xc0000004, 0x8},
{0x74, 0xc0000023, 0x28},
{0x75, 0xc0000004, 0x448},
{0x76, 0xc0000004, 0x110},
{0x77, 0xc0000004, 0x40},
{0x78, 0xc0000004, 0x40},
{0x79, 0xc0000003, 0x0},
{0x7a, 0xc0000004, 0x8},
{0x7b, 0xc0000004, 0x20},
{0x7c, 0xc0000004, 0xc},
{0x7d, 0xc0000003, 0x0},
{0x7e, 0xc0000004, 0x20},
{0x7f, 0xc0000003, 0x0},
{0x81, 0xc0000003, 0x0},
{0x82, 0xc0000003, 0x0},
{0x83, 0xc0000003, 0x0},
{0x84, 0xc0000003, 0x0},
{0x85, 0xc0000061, 0x0},
{0x86, 0xc0000004, 0x20},
{0x87, 0xc0000004, 0x8},
{0x88, 0xc0000004, 0x30},
{0x89, 0xc0000004, 0x30},
{0x8a, 0xc0000023, 0x130},
{0x8b, 0xc0000206, 0x0},
{0x8c, 0xc0000023, 0x83a6},
{0x8d, 0xc0000004, 0x120},
{0x8e, 0xc0000003, 0x0},
{0x8f, 0x80430006, 0x0},
{0x90, 0xc0000004, 0x28},
{0x91, 0xc0000004, 0x2},
{0x92, 0xc0000003, 0x0},
{0x93, 0xc0000004, 0x1},
{0x94, 0xc0000004, 0x4044c},
{0x95, 0xc0000004, 0x3},
{0x96, 0xc0000061, 0x0},
{0x97, 0xc0000004, 0x4},
{0x98, 0xc0000003, 0x0},
{0x99, 0xc0000004, 0x20},
{0x9a, 0xc0000004, 0x20},
{0x9b, 0xc0000003, 0x0},
{0x9c, 0xc0000004, 0x80},
{0x9d, 0xc0000004, 0x18},
{0x9e, 0xc0000004, 0x1},
{0x9f, 0xc00000f0, 0x0},
{0xa0, 0xc000000d, 0x0},
{0xa1, 0xc0000003, 0x0},
{0xa2, 0xc0000022, 0x0},
{0xa3, 0xc0000004, 0x1},
{0xa4, 0xc0000004, 0x20},
{0xa5, 0xc0000004, 0x10},
{0xa6, 0xc0000004, 0x228},
{0xa7, 0xc0000022, 0x0},
{0xa8, 0xc0000003, 0x0},
{0xa9, 0xc00000f0, 0x0},
{0xaa, 0xc0000003, 0x0},
{0xab, 0x80430006, 0x0},
{0xac, 0xc0000004, 0x3af7c},
{0xad, 0xc0000004, 0x108},
{0xae, 0xc0000023, 0x0},
{0xaf, 0xc000000d, 0x0},
{0xb0, 0xc0000003, 0x0},
{0xb1, 0xc0000003, 0x0},
{0xb2, 0xc000000d, 0x0},
{0xb3, 0xc0eb0006, 0x0},
{0xb4, 0xc0000003, 0x0},
{0xb5, 0xc000000d, 0x0},
{0xb6, 0xc0000004, 0x38},
{0xb7, 0xc0000004, 0x0},
{0xb8, 0xc0000004, 0x18},
{0xb9, 0xc000000d, 0x0},
{0xba, 0x80430006, 0x0},
{0xbb, 0xc0000003, 0x0},
{0xbc, 0xc0000004, 0x0},
{0xbd, 0xc0000004, 0x7f144},
{0xbe, 0xc0000004, 0x0},
{0xbf, 0xc0000003, 0x0},
{0xc0, 0xc0000004, 0x20},
{0xc1, 0xc0000023, 0x8},
{0xc2, 0xc000009d, 0x0},
{0xc3, 0xc0000004, 0x8},
{0xc4, 0xc0000004, 0x4},
{0xc5, 0xc0000004, 0x8},
{0xc6, 0xc00000bb, 0x0},
{0xc7, 0xc0000004, 0x18},
{0xc8, 0xc0000023, 0x40},
{0xc9, 0xc0000004, 0x8},
{0xca, 0xc0000004, 0x1},
{0xcb, 0xc0000003, 0x0},
{0xcc, 0xc0000003, 0x0},
{0xcd, 0xc0000003, 0x0},
{0xce, 0xc0000004, 0x8},
{0xcf, 0xc0000004, 0x4},
{0xd0, 0xc0000004, 0x0},
{0xd1, 0xc0000022, 0x0},
{0xd2, 0xc0000003, 0x0},
{0xd3, 0xc0000003, 0x0},
{0xd4, 0xc0000003, 0x0},
{0xd5, 0xc0000004, 0x8},
{0xd6, 0xc0000061, 0x0},
{0xd7, 0xc0000061, 0x0},
{0xd8, 0xc0000004, 0x20},
{0xd9, 0xc0000003, 0x0},
{0xda, 0xc0000003, 0x0},
{0xdb, 0xc0000003, 0x0},
{0xdc, 0xc0000003, 0x0},
{0xdd, 0xc0000004, 0x4},
{0xde, 0xc0000003, 0x0},
{0xdf, 0xc0000003, 0x0},
{0xe0, 0xc0000003, 0x0},
{0xe1, 0xc0000003, 0x0},
{0xe2, 0xc0000003, 0x0},
{0xe3, 0xc0000004, 0x1},
{0xe4, 0xc0000004, 0x0},
{0xe5, 0xc0000004, 0x0},
{0xe6, 0xc000000d, 0x0},
{0xe7, 0xc0000003, 0x0},
{0xe8, 0xc00000bb, 0x0},
{0xe9, 0xc0000003, 0x0},
{0xea, 0xc0000004, 0x4298},
{0xeb, 0xc0000022, 0x0},
{0xec, 0xc00000bb, 0x0},
{0xed, 0xc00000bb, 0x0},
{0xee, 0xc0000003, 0x0},
{0xef, 0xc0000003, 0x0},
{0xf0, 0xc0000003, 0x0},
{0xf1, 0xc0000004, 0x8336d},
{0xf2, 0xc0000003, 0x0},
{0xf3, 0xc0000004, 0x10},
{0xf5, 0xc0000003, 0x0},
{0xf6, 0xc0000003, 0x0},
{0xf7, 0xc0000061, 0x0},
{0xf8, 0xc0000003, 0x0},
{0xf9, 0xc0000003, 0x0},
{0xfa, 0xc0000004, 0x10},
{0xfb, 0xc0000003, 0x0},
{0xfd, 0xc0000004, 0xc},
{0xfe, 0xc0000003, 0x0},
{0xff, 0xc0000003, 0x0},
{0x100, 0xc0000022, 0x0}
        };
        for (ULONG i = 0; i < RTL_NUMBER_OF(reference); i++) {
            if (reference[i].klass != klass)
                continue;
            if (returned && reference[i].need)
                if (!NT_SUCCESS(KiCopyToUser(returned, &reference[i].need, sizeof(reference[i].need))))
                    return STATUS_ACCESS_VIOLATION;
            return reference[i].status;
        }
        return STATUS_NOT_IMPLEMENTED;
    }
    }
}

#ifndef __aarch64__
/*
 * a user caller's query, answered the way windows 11 26300 answers the class at that length: the buffer is probed on
 * the boundary the class asks, then the returned length; a length the class does not take is refused with the status
 * and the returned length windows gives, and a class of one length returns that length. the classes sized by the
 * machine and the ones that read a request out of the buffer are not in the table and answer by themselves.
 */
static NTSTATUS query_as_windows(ULONG klass, PVOID info, ULONG length, PULONG returned)
{
    if (KeGetPreviousMode() != UserMode || klass >= RTL_NUMBER_OF(ExpSystemClassAlignment))
        return query_system_information((SYSTEM_INFORMATION_CLASS)klass, info, length, returned);
    PS_CLASS_ANSWER reference;
    /*
     * the boot entropy has one length the ladder of the reference did not ask, so it answers by itself. the code
     * integrity information reads its length out of the buffer, which the ladder left clear, and so does it.
     */
    BOOLEAN known = klass != SystemFirmwareTableInformation && klass != 117 && klass != 103 &&
                    PspClassAnswer(ExpSystemClassRuns, RTL_NUMBER_OF(ExpSystemClassRuns), klass, length, &reference);
    ULONG alignment = ExpSystemClassAlignment[klass];
    if (!alignment)
        return known ? reference.Status : STATUS_INVALID_INFO_CLASS;
    NTSTATUS status = KiProbeQueryBuffer(info, length, alignment, TRUE);
    if (NT_SUCCESS(status))
        status = probe_pages(info, length);
    if (NT_SUCCESS(status))
        status = KiProbeReturnLength(returned);
    if (!NT_SUCCESS(status))
        return status;
    /* what the caller had in its buffer, for the bytes of a block windows does not write. */
    UCHAR kept[0x40];
    ULONG keep = length < sizeof(kept) ? length : sizeof(kept);
    BOOLEAN holes = FALSE;
    for (ULONG i = 0; i < RTL_NUMBER_OF(ExpSystemClassHoles) && !holes; i++)
        holes = ExpSystemClassHoles[i].Klass == klass;
    if (holes && keep && !NT_SUCCESS(KiCopyFromUser(kept, info, keep)))
        return STATUS_ACCESS_VIOLATION;
    ULONG written = PS_CLASS_NO_LENGTH;
    if (!known) {
        status = query_system_information((SYSTEM_INFORMATION_CLASS)klass, info, length, returned);
    } else if (reference.Decided || (!NT_SUCCESS(reference.Status) && !PspClassShort((ULONG)reference.Status))) {
        /* a length or a class windows refuses: its status, and the returned length when it writes one. */
        if (returned && reference.Returned != PS_CLASS_NO_LENGTH)
            KiCopyToUser(returned, &reference.Returned, sizeof(ULONG));
        return reference.Status;
    } else {
        ULONG before = 0;
        BOOLEAN fixed = returned && !reference.Variable;
        if (fixed)
            KiCopyFromUser(&before, returned, sizeof(before));
        status = query_system_information((SYSTEM_INFORMATION_CLASS)klass, info, length, returned);
        if (status == STATUS_NOT_IMPLEMENTED)
            status = PspClassDefault(&reference, info, length, returned);
        else if (fixed && NT_SUCCESS(status))
            KiCopyToUser(returned, reference.Returned != PS_CLASS_NO_LENGTH ? &reference.Returned : &before, sizeof(ULONG));
        /* a refusal windows makes with a returned length of none. */
        if (returned && status == reference.Status && !NT_SUCCESS(status) && !reference.Returned)
            KiCopyToUser(returned, &reference.Returned, sizeof(ULONG));
        if (!reference.Variable)
            written = reference.Returned;
    }
    if (!holes || !NT_SUCCESS(status))
        return status;
    if (written == PS_CLASS_NO_LENGTH && (!returned || !NT_SUCCESS(KiCopyFromUser(&written, returned, sizeof(written)))))
        return status;
    for (ULONG i = 0; i < RTL_NUMBER_OF(ExpSystemClassHoles); i++) {
        if (ExpSystemClassHoles[i].Klass != klass || ExpSystemClassHoles[i].Size != written || written > keep)
            continue;
        for (ULONG at = 0; at < written; at++)
            if (ExpSystemClassHoles[i].Bytes >> at & 1)
                KiCopyToUser((PUCHAR)info + at, kept + at, 1);
    }
    return status;
}

#endif

NTSTATUS NTAPI NtQuerySystemInformation(SYSTEM_INFORMATION_CLASS klass, PVOID info, ULONG length, PULONG returned)
{
#ifdef __aarch64__
    /* the lengths and statuses of the reference were measured on x64 only. */
    return query_system_information(klass, info, length, returned);
#else
    return query_as_windows((ULONG)klass, info, length, returned);
#endif
}

/*
 * the classes that take a request: a relationship for the logical processors, a process for the cpu sets, a group
 * for the ones told processor by processor. any other class is refused, as on windows 11 26300.
 */
NTSTATUS NTAPI ZwQuerySystemInformation(SYSTEM_INFORMATION_CLASS klass, PVOID info, ULONG length, PULONG returned);

NTSTATUS NTAPI NtQuerySystemInformationEx(SYSTEM_INFORMATION_CLASS klass, PVOID input, ULONG input_length, PVOID info,
                                          ULONG length, PULONG returned)
{
    switch ((ULONG)klass) {
    case 107: {
        ULONG relationship;
        if (!input || input_length != sizeof(relationship))
            return STATUS_INVALID_PARAMETER;
        NTSTATUS status = KiCopyFromUser(&relationship, input, sizeof(relationship));
        return NT_SUCCESS(status) ? query_logical_processors_ex(relationship, info, length, returned) : status;
    }
    case 175:
        if (!input || input_length != sizeof(HANDLE))
            return STATUS_INVALID_PARAMETER;
        return query_cpu_sets(info, length, returned);
#ifdef __x86_64__
    case 181:
    case 230: {
        /*
         * SystemSupportedProcessorArchitectures and its second form, which ntdll asks when IsWow64Process names any
         * process but the current one: the machines the system runs, as machine and the bits kernel 16, user 17,
         * native 18, the process's own 19, wow64 container 20, ended by a zero entry. measured on windows: with a null
         * handle the first form answers success and nothing, the second the list with no process bit.
         */
        HANDLE h;
        if (!input || input_length != sizeof(h))
            return STATUS_INVALID_PARAMETER;
        NTSTATUS status = KiCopyFromUser(&h, input, sizeof(h));
        if (!NT_SUCCESS(status))
            return status;
        ULONG need = 0;
        if (!h && (ULONG)klass == 181)
            return returned ? KiCopyToUser(returned, &need, sizeof(need)) : STATUS_SUCCESS;
        ULONG own = 0;
        if (h) {
            PEPROCESS process;
            status = PsReferenceProcessHandle(h, PROCESS_QUERY_LIMITED_INFORMATION, &process);
            if (!NT_SUCCESS(status))
                return status;
            own = process->Wow64Peb ? 2 : 1;
            ObDereferenceObject(process);
        }
        ULONG list[3] = {0x8664 | 0x70000 | (own == 1 ? 0x80000 : 0), 0x14C | 0x120000 | (own == 2 ? 0x80000 : 0), 0};
        need = sizeof(list);
        if (returned) {
            status = KiCopyToUser(returned, &need, sizeof(need));
            if (!NT_SUCCESS(status))
                return status;
        }
        if (length < need)
            return STATUS_BUFFER_TOO_SMALL;
        return KiCopyToUser(info, list, need);
    }
#endif
    case 8:
    case 23:
    case 42:
    case 61:
    case 73: /* SystemLogicalProcessorInformation of a group, what GetLogicalProcessorInformation of 26100 asks. */
    case 83:
    case 100:
    case 108:
    case 141: {
        USHORT group;
        if (!input || input_length != sizeof(group))
            return STATUS_INVALID_PARAMETER;
        NTSTATUS status = KiCopyFromUser(&group, input, sizeof(group));
        if (!NT_SUCCESS(status))
            return status;
        if (group)
            return STATUS_INVALID_PARAMETER;
        ULONG count = HalBootInfo->cpu_count;
        if ((ULONG)klass == 8 && length % 0x30) {
            /* the performance blocks are taken whole or not at all. */
            ULONG need = count * 0x30;
            if (returned)
                KiCopyToUser(returned, &need, sizeof(need));
            return STATUS_INFO_LENGTH_MISMATCH;
        }
        status = NtQuerySystemInformation(klass, info, length, returned);
        if ((ULONG)klass == 108 && status == STATUS_BUFFER_TOO_SMALL && length >= sizeof(ULONG64)) {
            /* the cycle counts that fit are told, and the length is then a mismatch. */
            ULONG64 cycles[MAXIMUM_PROCESSORS];
            status = ZwQuerySystemInformation(klass, cycles, count * sizeof(ULONG64), nullptr);
            if (NT_SUCCESS(status))
                status = KiCopyToUser(info, cycles, length / sizeof(ULONG64) * sizeof(ULONG64));
            return NT_SUCCESS(status) ? STATUS_INFO_LENGTH_MISMATCH : status;
        }
        return status;
    }
    default:
        return STATUS_INVALID_INFO_CLASS;
    }
}

/* the Nt service for a kernel caller: the copy helpers take its buffers as they are. */
NTSTATUS NTAPI ZwQuerySystemInformation(SYSTEM_INFORMATION_CLASS klass, PVOID info, ULONG length, PULONG returned)
{
    PKTHREAD t = KiCurrentThread();
    KPROCESSOR_MODE old = t->PreviousMode;
    t->PreviousMode = KernelMode;
    NTSTATUS status = NtQuerySystemInformation(klass, info, length, returned);
    t->PreviousMode = old;
    return status;
}

/* the installation locale the host chose. */
NTSTATUS NTAPI NtQueryDefaultLocale(BOOLEAN user, LCID *locale)
{
    LCID l = HalBootInfo->locale_lcid ? HalBootInfo->locale_lcid : 0x0409;
    return KiCopyToUser(locale, &l, sizeof(l));
}

NTSTATUS NTAPI NtQueryDefaultUILanguage(LANGID *lang)
{
    LANGID l = 0x0409;
    return KiCopyToUser(lang, &l, sizeof(l));
}

NTSTATUS NTAPI NtQueryInstallUILanguage(LANGID *lang)
{
    return NtQueryDefaultUILanguage(lang);
}

ULONG NTAPI NtGetCurrentProcessorNumber(void)
{
    return KeGetCurrentPrcb()->Number;
}

NTSTATUS NTAPI NtDisplayString(PUNICODE_STRING s)
{
    UNICODE_STRING copy;
    if (NT_SUCCESS(KiCaptureUnicodeString(&copy, s))) {
        /* debug output of user programs also goes to the terminal that ran them. */
        char text[512];
        ULONG n = 0;
        RtlUnicodeToUTF8N(text, sizeof(text), &n, copy.Buffer, copy.Length);
        MimeSendDebugOutput(PsCurrentProcess(), text, n);
        KiFreeCapturedString(&copy);
    }
    return STATUS_SUCCESS;
}

#define HARDERROR_OVERRIDE_ERRORMODE 0x10000000
#define MAXIMUM_HARDERROR_PARAMETERS 5
#define OPTION_SHUTDOWN_SYSTEM 6
#define OPTION_OK_NO_WAIT 7
#define OPTION_CANCEL_TRY_CONTINUE 8
#define RESPONSE_RETURN_TO_CALLER 0
#define RESPONSE_NOT_HANDLED 1
#define RESPONSE_OK 6
#define RESPONSE_CONTINUE 10
#define RTL_ERRORMODE_FAILCRITICALERRORS 0x10
#define STATUS_SERVICE_NOTIFICATION ((NTSTATUS)0x40000018L)
#define STATUS_VDM_HARD_ERROR ((NTSTATUS)0xC000021DL)
#define STATUS_UNHANDLED_EXCEPTION ((NTSTATUS)0xC0000144L)
#define STATUS_SYSTEM_IMAGE_BAD_SIGNATURE ((NTSTATUS)0xC00002D1L)

/* winsrvext's dwResponseDefault: the answer of a box nobody sees, by option. */
static const UCHAR g_hard_error_default[] = {2, 6, 6, 3, 8, 8, 6, 6, 3};

/* the message the csrss of the session gets, as harderror.exe takes it. strings too long for it are cut. */
static MIME_HARD_ERROR *hard_error_request(PEPROCESS process, NTSTATUS status, ULONG count, ULONG mask,
                                           const ULONG_PTR *values, const UNICODE_STRING *strings, ULONG option,
                                           USHORT *size)
{
    WCHAR path[MAX_PATH];
    ULONG chars = PsGetDeviceImagePath(process, path, sizeof(path)) / sizeof(WCHAR), name = chars;
    while (name && path[name - 1] != L'\\')
        name--;
    ULONG name_bytes = (chars - name) * sizeof(WCHAR), total = sizeof(MIME_HARD_ERROR) + name_bytes;
    ULONG lengths[MAXIMUM_HARDERROR_PARAMETERS] = {0};
    for (ULONG i = 0; i < count; i++) {
        if (!(mask >> i & 1))
            continue;
        lengths[i] = strings[i].Length < 0xFFFF - total ? strings[i].Length : (0xFFFF - total) & ~1u;
        total += lengths[i];
    }
    MIME_HARD_ERROR *r = ExAllocatePool2(POOL_FLAG_PAGED, total, KI_TAG('H', 'e', 'r', 'r'));
    if (!r)
        return nullptr;
    memset(r, 0, sizeof(*r));
    r->Size = total;
    r->Status = status;
    r->Option = option;
    r->Count = count;
    r->StringMask = mask;
    r->ProcessId = (ULONG)(ULONG_PTR)process->UniqueProcessId;
    r->ThreadId = (ULONG)(ULONG_PTR)PsCurrentThread()->Cid.UniqueThread;
    PUCHAR at = (PUCHAR)(r + 1);
    r->ImageName = sizeof(*r);
    r->ImageNameLength = name_bytes;
    memcpy(at, path + name, name_bytes);
    at += name_bytes;
    for (ULONG i = 0; i < count; i++) {
        if (!(mask >> i & 1)) {
            r->Parameters[i] = values[i];
            continue;
        }
        r->Parameters[i] = (ULONG64)(at - (PUCHAR)r);
        r->StringLength[i] = lengths[i];
        if (lengths[i])
            memcpy(at, strings[i].Buffer, lengths[i]);
        at += lengths[i];
    }
    *size = (USHORT)total;
    return r;
}

/*
 * ExpRaiseHardError, then what winsrvext!UserHardErrorEx makes of the message in the csrss of the session. no process
 * listens on a hard error port here: harderror.exe stands for the worker thread of csrss, makes the text and shows the
 * box. the caller is held while the text is made from it, which reads its windows, and with OptionOkNoWait no longer:
 * csrss answers ok then and the box stays up after the caller is gone.
 */
static ULONG hard_error(PEPROCESS process, NTSTATUS status, ULONG count, ULONG mask, const ULONG_PTR *values,
                        const UNICODE_STRING *strings, ULONG option)
{
    PKTHREAD thread = KiCurrentThread();
    BOOLEAN sent = (process->DefaultHardErrorMode & 1) || (status & HARDERROR_OVERRIDE_ERRORMODE);
    ULONG mode = 0;
    if (sent && thread->Teb && NT_SUCCESS(KiCopyFromUser(&mode, &thread->Teb->HardErrorMode, sizeof(mode))) &&
        (mode & RTL_ERRORMODE_FAILCRITICALERRORS))
        sent = FALSE;
    if (!sent)
        return RESPONSE_RETURN_TO_CALLER;
    status &= ~HARDERROR_OVERRIDE_ERRORMODE;
    if (status == STATUS_SERVICE_NOTIFICATION   ? count < 3 || count > 4 || mask != 3
        : status == STATUS_VDM_HARD_ERROR       ? count != 5 || mask != 12
        : status == STATUS_UNHANDLED_EXCEPTION  ? mask != 0
        : status == STATUS_SYSTEM_IMAGE_BAD_SIGNATURE && mask != 1)
        return RESPONSE_NOT_HANDLED;
    /* the session of the services has no one to show it to. */
    if (!process->SessionId || (process->DefaultHardErrorMode & 0x4000))
        return g_hard_error_default[option];
    USHORT size = 0;
    MIME_HARD_ERROR *request = hard_error_request(process, status, count, mask, values, strings, option, &size);
    if (!request)
        return RESPONSE_RETURN_TO_CALLER;
    PEPROCESS box;
    PETHREAD first;
    NTSTATUS started = PsStartHardErrorBox(request, size, &box, &first);
    ExFreePool(request);
    if (!NT_SUCCESS(started)) {
        KiLogPrintf("hard error box: harderror.exe does not start (%08x)", started);
        return RESPONSE_RETURN_TO_CALLER;
    }
    /* harderror.exe's first thread ends once the text is made. */
    BOOLEAN no_wait = option == OPTION_OK_NO_WAIT;
    NTSTATUS waited = KeWaitForSingleObject(no_wait ? (PVOID)first : (PVOID)box, WrLpcReply, UserMode, FALSE, nullptr);
    ULONG answer = RESPONSE_OK;
    if (!no_wait)
        answer = waited == STATUS_WAIT_0 && (ULONG)box->ExitStatus <= RESPONSE_CONTINUE ? (ULONG)box->ExitStatus
                                                                                         : RESPONSE_RETURN_TO_CALLER;
    ObDereferenceObject(first);
    ObDereferenceObject(box);
    return answer;
}

NTSTATUS NTAPI NtRaiseHardError(NTSTATUS status, ULONG count, ULONG string_mask, PULONG_PTR parameters, ULONG option,
                                PULONG response)
{
    if (count > MAXIMUM_HARDERROR_PARAMETERS || !parameters != !count)
        return STATUS_INVALID_PARAMETER_2;
    if (option > OPTION_CANCEL_TRY_CONTINUE)
        return STATUS_INVALID_PARAMETER_4;
    if (!response || !NT_SUCCESS(KiProbeReturnLength(response)))
        return STATUS_ACCESS_VIOLATION;
    ULONG_PTR values[MAXIMUM_HARDERROR_PARAMETERS] = {0};
    if (count && !NT_SUCCESS(KiCopyFromUser(values, parameters, count * sizeof(ULONG_PTR))))
        return STATUS_ACCESS_VIOLATION;
    /* the strings themselves are read as csrss reads them: one it cannot is empty. */
    UNICODE_STRING strings[MAXIMUM_HARDERROR_PARAMETERS] = {0};
    NTSTATUS result = STATUS_SUCCESS;
    for (ULONG i = 0; i < count && NT_SUCCESS(result); i++) {
        UNICODE_STRING s;
        if (!(string_mask >> i & 1))
            continue;
        result = KiCopyFromUser(&s, (PVOID)values[i], sizeof(s));
        s.Length &= ~1u;
        if (NT_SUCCESS(result) && !NT_SUCCESS(KiCaptureUnicodeStringValue(&strings[i], s)))
            strings[i] = (UNICODE_STRING){0};
    }
    if (NT_SUCCESS(result) && option == OPTION_SHUTDOWN_SYSTEM &&
        !SepSinglePrivilegeCheck(19 /* shutdown */, KeGetPreviousMode()))
        result = STATUS_PRIVILEGE_NOT_HELD;
    if (NT_SUCCESS(result)) {
        PEPROCESS process = PsCurrentProcess();
        char text[512];
        ULONG used = (ULONG)_snprintf(text, sizeof(text), "hard error %08x", (ULONG)status);
        for (ULONG i = 0; i < count && used < sizeof(text) - 8; i++) {
            if (string_mask >> i & 1) {
                ULONG n = 0;
                text[used++] = ' ';
                text[used++] = '"';
                RtlUnicodeToUTF8N(text + used, sizeof(text) - used - 2, &n, strings[i].Buffer, strings[i].Length);
                used += n;
                text[used++] = '"';
            } else {
                used += (ULONG)_snprintf(text + used, sizeof(text) - used, " %llx", (ULONG64)values[i]);
            }
        }
        if (used > sizeof(text) - 1)
            used = sizeof(text) - 1;
        text[used] = 0;
        KiLogPrintf("%s from %s, option %u", text, process->ImageFileName, option);
        ULONG answer = hard_error(process, status, count, string_mask, values, strings, option);
        result = KiCopyToUser(response, &answer, sizeof(answer));
    }
    for (ULONG i = 0; i < count; i++)
        KiFreeCapturedString(&strings[i]);
    return result;
}

NTSTATUS NTAPI NtTraceEvent(HANDLE handle, ULONG flags, ULONG length, PVOID data)
{
    return STATUS_SUCCESS;
}

/*
 * the product policy values a program reads. windows keeps them in a signed blob, here they are the ones of windows 11
 * pro 26300: the edition GetProductInfo answers and the languages the edition may show, which the mui cache is made of.
 */
/* the registry types of the values. */
#define LICENCE_SZ 1
#define LICENCE_DWORD 4
static const ULONG g_licence_product = 0x30; /* PRODUCT_PROFESSIONAL */
static const ULONG g_licence_languages = 1000;
static const WCHAR g_licence_empty[] = L"EMPTY";
static const WCHAR g_licence_sku[] =
        L"en-US;en-AU;en-CA;en-GB;en-HK;en-IE;en-NZ;en-SG;ar-SA;pt-BR;zh-TW;zh-CN;zh-HK;zh-SG;cs-CZ;da-DK;"
        L"el-GR;es-AR;es-CL;es-CO;es-ES;es-MX;es-US;fi-FI;fr-BE;fr-CA;fr-CH;fr-FR;de-AT;de-CH;de-DE;he-IL;"
        L"hu-HU;it-IT;ja-JP;ko-KR;nl-BE;nl-NL;nb-NO;pl-PL;pt-PT;ru-RU;sv-SE;tr-TR;bg-BG;hr-HR;et-EE;lv-LV;"
        L"lt-LT;ro-RO;sr-Latn-CS;sr-Latn-RS;sk-SK;sl-SI;th-TH;uk-UA;fy-NL;qps-ploc;qps-plocm;qps-Latn-x-sh";

static const struct {
    PCWSTR Name;
    ULONG Type;
    const void *Data;
    ULONG Size;
} g_licence_values[] = {
    {L"Kernel-ProductInfo", LICENCE_DWORD, &g_licence_product, sizeof(g_licence_product)},
    {L"Kernel-MUI-Number-Allowed", LICENCE_DWORD, &g_licence_languages, sizeof(g_licence_languages)},
    {L"Kernel-MUI-Language-Allowed", LICENCE_SZ, g_licence_empty, sizeof(g_licence_empty)},
    {L"Kernel-MUI-Language-Disallowed", LICENCE_SZ, g_licence_empty, sizeof(g_licence_empty)},
    {L"Kernel-MUI-Language-SKU", LICENCE_SZ, g_licence_sku, sizeof(g_licence_sku)},
};

BOOLEAN ExQueryLicenseValue(PCUNICODE_STRING name, PULONG type, const void **data, PULONG size)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_licence_values); i++) {
        UNICODE_STRING candidate;
        RtlInitUnicodeString(&candidate, g_licence_values[i].Name);
        if (RtlEqualUnicodeString(name, &candidate, TRUE)) {
            *type = g_licence_values[i].Type;
            *data = g_licence_values[i].Data;
            *size = g_licence_values[i].Size;
            return TRUE;
        }
    }
    return FALSE;
}

/* a missing value is STATUS_OBJECT_NAME_NOT_FOUND and a length of 0, and the type and the length come before the data. */
NTSTATUS NTAPI NtQueryLicenseValue(PUNICODE_STRING name, PULONG type, PVOID data, ULONG length, PULONG returned)
{
    UNICODE_STRING copy;
    NTSTATUS status = KiCaptureUnicodeString(&copy, name);
    if (!NT_SUCCESS(status))
        return status;
    ULONG kind = 0, size = 0;
    const void *value = nullptr;
    BOOLEAN found = ExQueryLicenseValue(&copy, &kind, &value, &size);
    KiFreeCapturedString(&copy);
    if (!found) {
        if (returned)
            KiCopyToUser(returned, &size, sizeof(size));
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    if (type)
        KiCopyToUser(type, &kind, sizeof(kind));
    if (returned)
        KiCopyToUser(returned, &size, sizeof(size));
    if (length < size)
        return STATUS_BUFFER_TOO_SMALL;
    return KiCopyToUser(data, value, size);
}

/*
 * the secure settings of the security policies, which a device with virtualization based security keeps. measured on
 * windows 11 26300: no setting is there whatever its names, the size is read and must not be 0, and the value and the
 * type are left as they were.
 */
NTSTATUS NTAPI NtQuerySecurityPolicy(PCUNICODE_STRING policy, PCUNICODE_STRING key, PCUNICODE_STRING value, PULONG type,
                                     PVOID data, PULONG size)
{
    ULONG room;
    if (!size || !NT_SUCCESS(KiCopyFromUser(&room, size, sizeof(room))))
        return STATUS_ACCESS_VIOLATION;
    return room ? STATUS_NOT_FOUND : STATUS_INVALID_PARAMETER;
}

NTSTATUS NTAPI NtApphelpCacheControl(ULONG type, PVOID data)
{
    return STATUS_NOT_SUPPORTED;
}

/*
 * no kernel debugger is attached, and windows 11 answers every command 0 to 0x2f with that before it looks at a
 * block, measured on windows. the two dump commands ask for the debug privilege first; what they answer with it
 * was not measured.
 */
NTSTATUS NTAPI NtSystemDebugControl(ULONG command, PVOID in, ULONG in_length, PVOID out, ULONG out_length, PULONG returned)
{
    if ((command == 0x1D || command == 0x25) && KeGetPreviousMode() == UserMode &&
        !SeThreadHasPrivilege(SE_DEBUG_PRIVILEGE))
        return STATUS_ACCESS_DENIED;
    return STATUS_DEBUGGER_INACTIVE;
}

NTSTATUS NTAPI NtQueryDebugFilterState(ULONG component, ULONG level)
{
    return TRUE;
}

/* like windows, changing which debug output is kept needs the debug privilege; everything is kept here. */
NTSTATUS NTAPI NtSetDebugFilterState(ULONG component, ULONG level, BOOLEAN state)
{
    if (KeGetPreviousMode() == UserMode && !SeThreadHasPrivilege(SE_DEBUG_PRIVILEGE))
        return STATUS_ACCESS_DENIED;
    return STATUS_SUCCESS;
}
