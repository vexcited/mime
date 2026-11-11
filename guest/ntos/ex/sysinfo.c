/* system information and time services. */
#include "../ntos.h"
#include "../se/se.h"
#ifdef __aarch64__
#include "acpi_tables.h"
#else
#include "acpi_tables_x64.h"
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

/*
 * the reference machine's smbios tables, as real firmware presents them: bios,
 * system, board, chassis and processor, with the strings programs read to learn
 * the machine. built in one buffer, served by SystemFirmwareTableInformation.
 */
typedef struct SMB_BUILDER {
    PUCHAR Buffer;
    ULONG Size;
    ULONG Capacity;
    ULONG Header; /* offset of the structure being built */
    UCHAR NextString;
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
    UCHAR header[4] = {type, 0, (UCHAR)handle, (UCHAR)(handle >> 8)};
    smb_put(b, header, sizeof(header));
}

/* the string index for a structure field, 0 for none. */
static UCHAR smb_string(SMB_BUILDER *b, const char *text)
{
    if (!text || !text[0])
        return 0;
    ULONG len = (ULONG)strlen(text);
    smb_put(b, text, len);
    smb_byte(b, 0);
    return ++b->NextString;
}

static void smb_end(SMB_BUILDER *b)
{
    /* the string set ends with an empty string. */
    smb_byte(b, 0);
    if (b->NextString == 0)
        smb_byte(b, 0);
    if (b->Header + 4 <= b->Size)
        b->Buffer[b->Header + 1] = (UCHAR)(b->Size - b->Header);
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
    ULONG64 ram_mb = HalBootInfo->ram_size >> 20;
    smb_begin(&b, 16, 0x0010);
    smb_byte(&b, 0x03); /* system board */
    smb_byte(&b, 0x03); /* system memory */
    smb_byte(&b, 0x03); /* no error correction */
    smb_dword(&b, ram_mb > 0xFFFFFFFFu ? 0xFFFFFFFFu : (ULONG)ram_mb); /* maximum capacity, keeper bytes */
    smb_word(&b, 0xFFFE); /* no error information handle */
    smb_word(&b, 1);      /* one device for the installed memory */
    smb_qword(&b, 0);     /* no extended maximum capacity */
    smb_end(&b);

    for (ULONG slot = 0; slot < 1; slot++) {
        ULONG64 device_mb = ram_mb;
        smb_begin(&b, 17, (USHORT)(0x0011 + slot));
        smb_word(&b, 0x0010); /* the array */
        smb_word(&b, 0xFFFE); /* no error information handle */
        smb_word(&b, 64);     /* total width */
        smb_word(&b, 64);     /* data width */
        USHORT encoded = device_mb > 0x7FFF ? 0x7FFF : (USHORT)device_mb;
        smb_word(&b, encoded);
        smb_dword(&b, device_mb > 0x7FFF ? (ULONG)device_mb : 0);
        smb_byte(&b, 0x0E); /* row of chips, the memory is soldered */
        smb_byte(&b, 0);    /* no device set */
        smb_byte(&b, smb_string(&b, slot ? "Row 2" : "Row 1"));
        smb_byte(&b, 0x9A); /* manufactured and populated */
        smb_byte(&b, 0x23); /* LPDDR5 */
        smb_word(&b, 0x2002); /* double data rate */
        smb_word(&b, 6400);   /* MT/s */
        smb_byte(&b, smb_string(&b, HalBootInfo->system_manufacturer));
        smb_byte(&b, 0); /* no serial */
        smb_byte(&b, 0); /* no asset tag */
        smb_byte(&b, 0); /* no part number */
        smb_byte(&b, 0); /* unknown rank */
        smb_qword(&b, 0);
        smb_word(&b, 0);
        smb_word(&b, 0);
        smb_word(&b, 0);
        smb_word(&b, 6400);
        smb_word(&b, 6400);
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
    smb_byte(&b, smb_string(&b, "Firmware TPM"));
    smb_qword(&b, 0x06); /* enabled and active */
    smb_dword(&b, 0);    /* no oem data */
    smb_end(&b);

    /* the end of the table. */
    smb_begin(&b, 127, 0x0005);
    smb_end(&b);
    return b.Size;
}

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
    ULONG cpus = HalBootInfo->cpu_count, n = 0;
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

static NTSTATUS query_acpi_tables(PVOID info, ULONG length, ULONG header_size, SYSTEM_FIRMWARE_TABLE_INFORMATION *header,
                                  PULONG returned)
{
    if (header->Action == SystemFirmwareTable_Enumerate) {
        ULONG need = header_size + ACPI_TABLE_COUNT * sizeof(ULONG);
        if (header->TableBufferLength < ACPI_TABLE_COUNT * sizeof(ULONG) || length < need) {
            header->TableBufferLength = ACPI_TABLE_COUNT * sizeof(ULONG);
            NTSTATUS status = KiCopyToUser(info, header, header_size);
            if (NT_SUCCESS(status) && returned)
                status = KiCopyToUser(returned, &need, sizeof(need));
            return NT_SUCCESS(status) ? STATUS_BUFFER_TOO_SMALL : status;
        }
        UCHAR *out = ExAllocatePool2(POOL_FLAG_NON_PAGED, need, KI_TAG('A', 'c', 'p', 'E'));
        if (!out)
            return STATUS_INSUFFICIENT_RESOURCES;
        header->TableBufferLength = ACPI_TABLE_COUNT * sizeof(ULONG);
        memcpy(out, header, header_size);
        for (ULONG i = 0; i < ACPI_TABLE_COUNT; i++)
            memcpy(out + header_size + i * sizeof(ULONG), &acpi_tables[i].Signature, sizeof(ULONG));
        NTSTATUS status = copy_info(info, out, need, length, returned);
        ExFreePool(out);
        return status;
    }

    if (header->Action != SystemFirmwareTable_Get)
        return STATUS_INVALID_PARAMETER;
    const ACPI_TABLE_INFO *table = nullptr;
    for (ULONG i = 0; i < ACPI_TABLE_COUNT; i++)
        if (acpi_tables[i].Signature == header->TableId)
            table = &acpi_tables[i];
    if (!table)
        return STATUS_INVALID_PARAMETER;

    ULONG need = header_size + table->Size;
    if (header->TableBufferLength < table->Size || length < need) {
        header->TableBufferLength = table->Size;
        NTSTATUS status = KiCopyToUser(info, header, header_size);
        if (NT_SUCCESS(status) && returned)
            status = KiCopyToUser(returned, &need, sizeof(need));
        return NT_SUCCESS(status) ? STATUS_BUFFER_TOO_SMALL : status;
    }
    UCHAR *out = ExAllocatePool2(POOL_FLAG_NON_PAGED, need, KI_TAG('A', 'c', 'p', 'T'));
    if (!out)
        return STATUS_INSUFFICIENT_RESOURCES;
    header->TableBufferLength = table->Size;
    memcpy(out, header, header_size);
    memcpy(out + header_size, table->Data, table->Size);
    if (table->Signature == 0x43495041)
        fit_madt(out + header_size, table->Size);
    NTSTATUS status = copy_info(info, out, need, length, returned);
    ExFreePool(out);
    return status;
}

/*
 * SystemFirmwareTableInformation: the RSMB provider, the raw smbios data, and the
 * RSDT one, the acpi tables of the reference machine. like wdm, a too small
 * buffer gets the required size in TableBufferLength.
 */
static NTSTATUS query_firmware_tables(PVOID info, ULONG length, PULONG returned)
{
    SYSTEM_FIRMWARE_TABLE_INFORMATION header;
    ULONG header_size = offsetof(SYSTEM_FIRMWARE_TABLE_INFORMATION, TableBuffer);
    if (length < header_size)
        return STATUS_INFO_LENGTH_MISMATCH;
    NTSTATUS status = KiCopyFromUser(&header, info, header_size);
    if (!NT_SUCCESS(status))
        return status;
    /* the acpi tables first, under either of their provider names: their data is the table itself. */
    if (header.ProviderSignature == 'ACPI' || header.ProviderSignature == 'RSDT')
        return query_acpi_tables(info, length, header_size, &header, returned);
    /* the provider list itself. */
    if (header.ProviderSignature == 'FIRM') {
        static const ULONG providers[] = {'ACPI', 'RSMB'};
        ULONG need = header_size + sizeof(providers);
        if (header.Action != SystemFirmwareTable_Enumerate)
            return STATUS_INVALID_PARAMETER;
        if (header.TableBufferLength < sizeof(providers) || length < need) {
            header.TableBufferLength = sizeof(providers);
            NTSTATUS status = KiCopyToUser(info, &header, header_size);
            if (NT_SUCCESS(status) && returned)
                status = KiCopyToUser(returned, &need, sizeof(need));
            return NT_SUCCESS(status) ? STATUS_BUFFER_TOO_SMALL : status;
        }
        header.TableBufferLength = sizeof(providers);
        UCHAR *out = ExAllocatePool2(POOL_FLAG_NON_PAGED, need, KI_TAG('F', 'i', 'r', 'm'));
        if (!out)
            return STATUS_INSUFFICIENT_RESOURCES;
        memcpy(out, &header, header_size);
        memcpy(out + header_size, providers, sizeof(providers));
        NTSTATUS status = copy_info(info, out, need, length, returned);
        ExFreePool(out);
        return status;
    }
    if (header.ProviderSignature != 'RSMB')
        return STATUS_INVALID_PARAMETER;

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

    if (header.Action != SystemFirmwareTable_Get || header.TableId != 0)
        return STATUS_INVALID_PARAMETER;

    UCHAR stack_table[2048];
    PUCHAR table = stack_table;
    ULONG table_size = smb_build(table, sizeof(stack_table));
    ULONG data_size = sizeof(RAW_SMBIOS_DATA) - 1 + table_size;
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
    RAW_SMBIOS_DATA *raw = (RAW_SMBIOS_DATA *)(out + header_size);
    raw->Used20CallingMethod = 1;
    raw->SMBIOSMajorVersion = 3;
    raw->SMBIOSMinorVersion = 4;
    raw->DmiRevision = 0;
    raw->Length = table_size;
    memcpy(raw->SMBIOSTableData, table, table_size);
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

static ULONG process_record_size(PEPROCESS p, ULONG threads)
{
    ULONG chars;
    process_leaf(p, &chars);
    ULONG size = sizeof(SYSTEM_PROCESS_INFORMATION) + threads * sizeof(SYSTEM_THREAD_INFORMATION) +
                 (chars ? (chars + 1) * sizeof(WCHAR) : 0);
    return (size + 7) & ~7u;
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
static ULONG fill_processes(PUCHAR buffer, ULONG capacity, PUCHAR user_base)
{
    ULONG used = 0;
    SYSTEM_PROCESS_INFORMATION *last = nullptr;
    ULONG64 lock = KiAcquireDispatcherLock();
    for (PLIST_ENTRY e = PsActiveProcessHead.Flink; e != &PsActiveProcessHead; e = e->Flink) {
        PEPROCESS p = CONTAINING_RECORD(e, EPROCESS, ActiveProcessLinks);
        if (p->Exited)
            continue;
        ULONG threads = process_thread_count(p);
        ULONG size = process_record_size(p, threads);
        if (used + size <= capacity) {
            SYSTEM_PROCESS_INFORMATION *info = (SYSTEM_PROCESS_INFORMATION *)(buffer + used);
            memset(info, 0, size);
            info->NumberOfThreads = threads;
            info->NumberOfThreadsHighWatermark = threads;
            info->CreateTime.QuadPart = (LONGLONG)p->CreateTime;
            info->BasePriority = 8;
            info->UniqueProcessId = p->UniqueProcessId;
            info->InheritedFromUniqueProcessId = p->InheritedFromUniqueProcessId;
            info->HandleCount = p->HandleTable.Count;
            info->SessionId = p->SessionId;
            info->UniqueProcessKey = (ULONG_PTR)p->UniqueProcessId;
            SYSTEM_THREAD_INFORMATION *t = (SYSTEM_THREAD_INFORMATION *)(info + 1);
            for (PLIST_ENTRY te = p->ThreadListHead.Flink; te != &p->ThreadListHead; te = te->Flink) {
                ETHREAD *thread = CONTAINING_RECORD(te, ETHREAD, ThreadListEntry);
                if (thread->HasTerminated)
                    continue;
                t->CreateTime.QuadPart = (LONGLONG)thread->CreateTime;
                t->StartAddress = thread->Win32StartAddress ? thread->Win32StartAddress : thread->StartAddress;
                t->ClientId = thread->Cid;
                t->Priority = 8;
                t->BasePriority = 8;
                t->ThreadState = 5; /* waiting, as most threads are. */
                t++;
            }
            ULONG chars;
            PCWSTR leaf = process_leaf(p, &chars);
            if (chars) {
                memcpy(t, leaf, chars * sizeof(WCHAR));
                info->ImageName.Length = (USHORT)(chars * sizeof(WCHAR));
                info->ImageName.MaximumLength = (USHORT)((chars + 1) * sizeof(WCHAR));
                info->ImageName.Buffer = (PWSTR)(user_base + ((PUCHAR)t - buffer));
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

static NTSTATUS query_processes(PVOID info, ULONG length, PULONG returned)
{
    /* buffers larger than any process list needs are filled only up to this bound. */
    ULONG capacity = length < 0x100000 ? length : 0x100000;
    PUCHAR buffer = capacity ? ExAllocatePool2(POOL_FLAG_NON_PAGED, capacity, KI_TAG('P', 's', 'I', 'n')) : nullptr;
    if (capacity && !buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    ULONG need = fill_processes(buffer, capacity, info);
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
                *(PVOID *)(out + 0) = entry->Object;
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
                *(PVOID *)(out + 8) = entry->Object;
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
    NTSTATUS status = copy_info(info, buffer, (ULONG)(header + count * entry_size), length, returned);
    ExFreePool(buffer);
    return status;
}

/* ------------------------------------------------------------------ modules */

typedef struct {
    HANDLE Section;
    PVOID MappedBase;
    PVOID ImageBase;
    ULONG ImageSize;
    ULONG Flags;
    USHORT LoadOrderIndex, InitOrderIndex, LoadCount, OffsetToFileName;
    UCHAR FullPathName[256];
} RTL_PROCESS_MODULE_INFORMATION;

/* SystemModuleInformationEx: the module, then its checksum, its timestamp and the base it was built for, in 0x140 bytes. */
typedef struct {
    USHORT NextOffset;
    USHORT Pad[3];
    RTL_PROCESS_MODULE_INFORMATION BaseInfo;
    ULONG ImageChecksum;
    ULONG TimeDateStamp;
    PVOID DefaultBase;
} RTL_PROCESS_MODULE_INFORMATION_EX;

static_assert(sizeof(RTL_PROCESS_MODULE_INFORMATION_EX) == 0x140, "module information ex layout");

typedef struct {
    RTL_PROCESS_MODULE_INFORMATION *Entries;
    RTL_PROCESS_MODULE_INFORMATION_EX *EntriesEx;
    ULONG Capacity;
    ULONG Count;
    BOOLEAN Extended;
} MODULE_LIST;

/* the checksum and timestamp out of the headers of a loaded image, when they can be read. */
static void module_stamp(PVOID base, PULONG checksum, PULONG stamp)
{
    const IMAGE_DOS_HEADER *dos = base;
    *checksum = 0;
    *stamp = 0;
    if (dos->e_magic != 0x5A4D || dos->e_lfanew <= 0)
        return;
    const IMAGE_NT_HEADERS64 *nt = (const void *)((const UCHAR *)base + dos->e_lfanew);
    if (nt->Signature != 0x4550)
        return;
    *checksum = nt->OptionalHeader.CheckSum;
    *stamp = nt->FileHeader.TimeDateStamp;
}

/* the full path as the loader recorded it, the file name at its last backslash. */
static void module_callback(PVOID context, ULONG index, PVOID base, SIZE_T size, PCUNICODE_STRING name)
{
    MODULE_LIST *list = context;
    if (list->Count < list->Capacity) {
        RTL_PROCESS_MODULE_INFORMATION *m = list->Extended ? &list->EntriesEx[list->Count].BaseInfo
                                                           : &list->Entries[list->Count];
        memset(list->Extended ? (void *)&list->EntriesEx[list->Count] : (void *)m, 0,
               list->Extended ? sizeof(*list->EntriesEx) : sizeof(*m));
        m->ImageBase = base;
        m->MappedBase = base;
        m->ImageSize = (ULONG)size;
        m->Flags = 0x08804000;
        m->LoadOrderIndex = m->InitOrderIndex = (USHORT)index;
        m->LoadCount = 1;
        ULONG n = name->Length / sizeof(WCHAR), at = 0;
        for (ULONG i = 0; i < n && at < sizeof(m->FullPathName) - 1; i++) {
            WCHAR c = name->Buffer[i];
            m->FullPathName[at++] = c < 0x80 ? (UCHAR)c : '?';
            if (c == L'\\')
                m->OffsetToFileName = (USHORT)at;
        }
        if (list->Extended) {
            RTL_PROCESS_MODULE_INFORMATION_EX *e = &list->EntriesEx[list->Count];
            e->DefaultBase = base;
            module_stamp(base, &e->ImageChecksum, &e->TimeDateStamp);
            e->NextOffset = (USHORT)sizeof(*e);
        }
    }
    list->Count++;
}

/* the kernel and the images it loaded, which programs list to learn the drivers. the extended form has no count
   header: each entry carries the offset to the next one, and the last one zero. */
static NTSTATUS query_modules(BOOLEAN extended, PVOID info, ULONG length, PULONG returned)
{
    MODULE_LIST list = {};
    list.Capacity = 256;
    list.Extended = extended;
    SIZE_T entry_size = extended ? sizeof(RTL_PROCESS_MODULE_INFORMATION_EX) : sizeof(RTL_PROCESS_MODULE_INFORMATION);
    list.Entries = ExAllocatePool2(POOL_FLAG_NON_PAGED, list.Capacity * entry_size, KI_TAG('S', 'y', 'M', 'o'));
    if (!list.Entries)
        return STATUS_INSUFFICIENT_RESOURCES;
    list.EntriesEx = (void *)list.Entries;
    MiEnumerateSystemImages(module_callback, &list);
    if (extended) {
        /* the loaded user images too, as windows reports them in this class before windows 8 could ask for them. */
        PPEB peb = PsCurrentProcess()->Peb;
        PEB_LDR_DATA ldr = {};
        if (peb && NT_SUCCESS(KiCopyFromUser(&ldr, peb->Ldr, sizeof(ldr)))) {
            PLIST_ENTRY head = &ldr.InLoadOrderModuleList, it = ldr.InLoadOrderModuleList.Flink;
            for (ULONG guard = 0; it != head && guard < 512; guard++) {
                LDR_DATA_TABLE_ENTRY e = {};
                if (!NT_SUCCESS(KiCopyFromUser(&e, it, sizeof(e))))
                    break;
                if (e.DllBase)
                    module_callback(&list, list.Count, e.DllBase, e.SizeOfImage, &e.FullDllName);
                it = e.InLoadOrderLinks.Flink;
            }
        }
    }
    ULONG count = list.Count < list.Capacity ? list.Count : list.Capacity;
    /* windows 11 24h2 and later zero the base of every module for a caller without SeDebugPrivilege. */
    if (!SeThreadHasPrivilege(SE_DEBUG_PRIVILEGE)) {
        for (ULONG i = 0; i < count; i++) {
            if (extended)
                list.EntriesEx[i].BaseInfo.ImageBase = nullptr;
            else
                list.Entries[i].ImageBase = nullptr;
        }
    }
    ULONG header = extended ? 0 : 8;
    ULONG size = header + count * (ULONG)entry_size;
    UCHAR count_header[8] = {};
    if (!extended)
        *(PULONG)count_header = count;
    else if (count)
        list.EntriesEx[count - 1].NextOffset = 0;
    NTSTATUS status;
    if (returned)
        KiCopyToUser(returned, &size, sizeof(size));
    if (length < header || (extended && length < entry_size)) {
        status = STATUS_INFO_LENGTH_MISMATCH;
    } else {
        /* a too small buffer gets the modules that fit and the status saying so. */
        ULONG fit = (length - header) / (ULONG)entry_size;
        if (fit > count)
            fit = count;
        status = header ? KiCopyToUser(info, count_header, header) : STATUS_SUCCESS;
        if (NT_SUCCESS(status) && fit)
            status = KiCopyToUser((PUCHAR)info + header, list.Entries, fit * (ULONG)entry_size);
        if (NT_SUCCESS(status) && fit < count)
            status = STATUS_INFO_LENGTH_MISMATCH;
    }
    ExFreePool(list.Entries);
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
    ULONG size = sizeof(in);
    if (returned)
        KiCopyToUser(returned, &size, sizeof(size));
    if (length != sizeof(in))
        return STATUS_INFO_LENGTH_MISMATCH;
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
    return status;
}

static NTSTATUS query_system_information(SYSTEM_INFORMATION_CLASS klass, PVOID info, ULONG length, PULONG returned)
{
    switch ((ULONG)klass) {
    case SystemHandleInformation:
        return query_handles(FALSE, info, length, returned);
    case SystemExtendedHandleInformation:
        return query_handles(TRUE, info, length, returned);
    case SystemProcessInformation:
        return query_processes(info, length, returned);
    case SystemModuleInformation:
        return query_modules(FALSE, info, length, returned);
    case 77: /* SystemModuleInformationEx */
        return query_modules(TRUE, info, length, returned);
    case SystemPageFileInformation:
        /* no paging file: the host swaps guest memory, so the list is empty. */
        return copy_info(info, nullptr, 0, length, returned);
    case SystemBasicInformation:
    case SystemEmulationBasicInformation: {
        SYSTEM_BASIC_INFORMATION b = {};
        b.TimerResolution = KI_CLOCK_TICK_HNS;
        b.PageSize = PAGE_SIZE;
        b.NumberOfPhysicalPages = (ULONG)MmGetTotalPages();
        b.LowestPhysicalPageNumber = (ULONG)(HalBootInfo->ram_base >> PAGE_SHIFT);
        b.HighestPhysicalPageNumber = (ULONG)((HalBootInfo->ram_base + HalBootInfo->ram_size) >> PAGE_SHIFT) - 1;
        b.AllocationGranularity = 0x10000;
        b.MinimumUserModeAddress = 0x10000;
        b.MaximumUserModeAddress = 0x7FFFFFFEFFFF;
        b.ActiveProcessorsAffinityMask = HalBootInfo->cpu_count >= 64 ? ~0ull : (1ull << HalBootInfo->cpu_count) - 1;
        b.NumberOfProcessors = (CCHAR)HalBootInfo->cpu_count;
        return copy_info(info, &b, sizeof(b), length, returned);
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
            p[i].IdleTime.QuadPart = (LONGLONG)idle;
            p[i].KernelTime.QuadPart = (LONGLONG)(prcb->KernelTime + idle);
            p[i].UserTime.QuadPart = (LONGLONG)prcb->UserTime;
            p[i].DpcTime.QuadPart = (LONGLONG)prcb->DpcTime;
            p[i].InterruptTime.QuadPart = (LONGLONG)prcb->InterruptTime;
            p[i].InterruptCount = (ULONG)prcb->InterruptCount;
        }
        if (length < sizeof(PERFORMANCE))
            return STATUS_INFO_LENGTH_MISMATCH;
        /* a shorter buffer gets the processors that fit, as on windows. */
        if (length < size)
            size = length / sizeof(PERFORMANCE) * sizeof(PERFORMANCE);
        return copy_info(info, p, size, length, returned);
    }
    case SystemProcessorInformation:
    case SystemEmulationProcessorInformation: {
        SYSTEM_PROCESSOR_INFORMATION p = {};
        p.ProcessorArchitecture = MIME_NATIVE_ARCHITECTURE;
        p.ProcessorLevel = KiProcessorLevel();
        p.ProcessorRevision = KiProcessorRevision();
        p.MaximumProcessors = (USHORT)HalBootInfo->cpu_count;
        p.ProcessorFeatureBits = 0;
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
        return copy_info(info, &t, sizeof(t), length, returned);
    }
    case SystemPerformanceInformation: {
        UCHAR perf[0x158] = {};
        /* no paging file: the host swaps guest memory, so the limit is ram plus the host's swap. */
        ULONG64 free = MmGetAvailablePages();
        *(PULONG)(perf + 0x4C) = (ULONG)free;                       /* AvailablePages */
        *(PULONG)(perf + 0x50) = (ULONG)MmGetCommittedPages();      /* CommittedPages */
        *(PULONG)(perf + 0x54) = (ULONG)MmGetCommitLimitPages();    /* CommitLimit */
        *(PULONG)(perf + 0x58) = (ULONG)MmGetPeakCommittedPages();  /* PeakCommitment */
        /* the structure was 0x138 bytes before the build that made it 0x158, and callers pass the size they know. */
        return copy_info(info, perf, length >= 0x138 && length < sizeof(perf) ? 0x138 : sizeof(perf), length, returned);
    }
    case SystemFileCacheInformation: {
        /* the system cache keeps no working set of its own, so its size is what the host has cached for the guest. */
        ULONG64 cache[8] = {};
        cache[0] = MmGetFreePages() * PAGE_SIZE / 8;
        cache[1] = cache[0];
        return copy_info(info, cache, sizeof(cache), length, returned);
    }
    case SystemMimePoolInformation: {
        MIME_POOL_INFORMATION pool;
        MiSpecialPoolQuery(&pool);
        return copy_info(info, &pool, sizeof(pool), length, returned);
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
    case SystemNumaHighestNodeInformation: {
        ULONG node = 0;
        return copy_info(info, &node, sizeof(node), length, returned);
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
        ULONG need = sizeof(boot);
        if (returned)
            KiCopyToUser(returned, &need, sizeof(need));
        if (length < 0x14)
            return STATUS_INFO_LENGTH_MISMATCH;
        NTSTATUS status = KiCopyToUser(info, &boot, 0x14);
        if (!NT_SUCCESS(status))
            return status;
        if (length < need)
            return STATUS_INFO_LENGTH_MISMATCH;
        return KiCopyToUser((PUCHAR)info + 0x18, &boot.BootFlags, sizeof(boot.BootFlags));
    }
    case SystemSecureBootInformation: {
        UCHAR enabled = 1;
        return copy_info(info, &enabled, sizeof(enabled), length, returned);
    }
    case 44: /* SystemCurrentTimeZoneInformation */
        return ExQueryTimeZoneInformation(info, length, returned);
    case 88: /* SystemProcessIdInformation */
        return query_process_id(info, length, returned);
    case 103: { /* SystemCodeIntegrityInformation */
        /* like ci.dll: the caller sets the length, the options are code integrity on, no test or debug signing. */
        ULONG ci[2];
        if (length != sizeof(ci))
            return STATUS_INFO_LENGTH_MISMATCH;
        NTSTATUS status = KiCopyFromUser(ci, info, sizeof(ci));
        if (!NT_SUCCESS(status))
            return status;
        if (ci[0] != sizeof(ci))
            return STATUS_INFO_LENGTH_MISMATCH;
        ci[1] = 0x00000001; /* CODEINTEGRITY_OPTION_ENABLED */
        return copy_info(info, ci, sizeof(ci), length, returned);
    }
    case 164: {
        /* the user mode code integrity state msxml asks for, zero flags are no enforcement. */
        UCHAR state[0x20] = {};
        return copy_info(info, state, length < sizeof(state) ? length : sizeof(state), length, returned);
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
    default:
        return STATUS_INVALID_INFO_CLASS;
    }
}

NTSTATUS NTAPI NtQuerySystemInformation(SYSTEM_INFORMATION_CLASS klass, PVOID info, ULONG length, PULONG returned)
{
    NTSTATUS status = query_system_information(klass, info, length, returned);
    return status;
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
#define OPTION_CANCEL_TRY_CONTINUE 8
#define OPTION_SHUTDOWN_SYSTEM 6
#define RESPONSE_RETURN_TO_CALLER 0

/*
 * no process listens on a hard error port here, so windows' answer for that case is given: the caller gets
 * ResponseReturnToCaller and shows the error itself. the error and its string parameters go to the log.
 */
NTSTATUS NTAPI NtRaiseHardError(NTSTATUS status, ULONG count, ULONG string_mask, PULONG_PTR parameters, ULONG option,
                                PULONG response)
{
    if (count > MAXIMUM_HARDERROR_PARAMETERS)
        return STATUS_INVALID_PARAMETER_2;
    if (option > OPTION_CANCEL_TRY_CONTINUE)
        return STATUS_INVALID_PARAMETER_4;
    ULONG_PTR values[MAXIMUM_HARDERROR_PARAMETERS] = {0};
    if (count && !NT_SUCCESS(KiCopyFromUser(values, parameters, count * sizeof(ULONG_PTR))))
        return STATUS_ACCESS_VIOLATION;
    if (option == OPTION_SHUTDOWN_SYSTEM && !SepSinglePrivilegeCheck(19 /* shutdown */, KeGetPreviousMode()))
        return STATUS_PRIVILEGE_NOT_HELD;

    PEPROCESS process = PsCurrentProcess();
    char text[512];
    ULONG used = 0;
    used += (ULONG)_snprintf(text, sizeof(text), "hard error %08x", (ULONG)status);
    for (ULONG i = 0; i < count && used < sizeof(text) - 8; i++) {
        UNICODE_STRING copy;
        if ((string_mask >> i & 1) && NT_SUCCESS(KiCaptureUnicodeString(&copy, (PCUNICODE_STRING)values[i]))) {
            ULONG n = 0;
            text[used++] = ' ';
            text[used++] = '"';
            RtlUnicodeToUTF8N(text + used, sizeof(text) - used - 2, &n, copy.Buffer, copy.Length);
            used += n;
            text[used++] = '"';
            KiFreeCapturedString(&copy);
        } else {
            used += (ULONG)_snprintf(text + used, sizeof(text) - used, " %llx", (ULONG64)values[i]);
        }
    }
    if (used > sizeof(text) - 1)
        used = sizeof(text) - 1;
    text[used] = 0;
    KiLogPrintf("%s from %s, option %u", text, process->ImageFileName, option);

    ULONG answer = RESPONSE_RETURN_TO_CALLER;
    return KiCopyToUser(response, &answer, sizeof(answer));
}

NTSTATUS NTAPI NtTraceEvent(HANDLE handle, ULONG flags, ULONG length, PVOID data)
{
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtTraceControl(ULONG code, PVOID in, ULONG in_len, PVOID out, ULONG out_len, PULONG returned)
{
    return STATUS_NOT_SUPPORTED;
}

/*
 * the product policy values a program reads. windows keeps them in a signed blob, here the ones that tell the
 * edition are fixed to what GetProductInfo answers: professional. a missing one is STATUS_OBJECT_NAME_NOT_FOUND.
 */
NTSTATUS NTAPI NtQueryLicenseValue(PUNICODE_STRING name, PULONG type, PVOID data, ULONG length, PULONG returned)
{
    static const struct {
        PCWSTR Name;
        ULONG Value;
    } values[] = {
        {L"Kernel-ProductInfo", 0x30}, /* PRODUCT_PROFESSIONAL */
    };
    UNICODE_STRING copy;
    NTSTATUS status = KiCaptureUnicodeString(&copy, name);
    if (!NT_SUCCESS(status))
        return status;
    ULONG i = 0;
    for (; i < RTL_NUMBER_OF(values); i++) {
        UNICODE_STRING candidate;
        RtlInitUnicodeString(&candidate, values[i].Name);
        if (RtlEqualUnicodeString(&copy, &candidate, TRUE))
            break;
    }
    KiFreeCapturedString(&copy);
    if (i == RTL_NUMBER_OF(values))
        return STATUS_OBJECT_NAME_NOT_FOUND;
    ULONG kind = 4, size = sizeof(ULONG); /* REG_DWORD */
    if (type)
        KiCopyToUser(type, &kind, sizeof(kind));
    if (returned)
        KiCopyToUser(returned, &size, sizeof(size));
    if (length < size)
        return STATUS_BUFFER_TOO_SMALL;
    return KiCopyToUser(data, &values[i].Value, size);
}

NTSTATUS NTAPI NtApphelpCacheControl(ULONG type, PVOID data)
{
    return STATUS_NOT_SUPPORTED;
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
