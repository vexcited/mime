/*
 * system information and time services.
 */
#include "../ntos.h"

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

NTSTATUS NTAPI NtQueryTimerResolution(PULONG maximum, PULONG minimum, PULONG current)
{
    ULONG max = 156250, min = 5000, cur = KI_CLOCK_TICK_HNS;
    KiCopyToUser(maximum, &max, sizeof(max));
    KiCopyToUser(minimum, &min, sizeof(min));
    return KiCopyToUser(current, &cur, sizeof(cur));
}

NTSTATUS NTAPI NtSetTimerResolution(ULONG desired, BOOLEAN set, PULONG current)
{
    /* timers are already programmed precisely; report what was asked for. */
    ULONG cur = set ? desired : KI_CLOCK_TICK_HNS;
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
 * the smbios tables of the reference machine, what the firmware of a real
 * computer presents: the bios, the system, the board, the chassis and the
 * processor, with the strings a program reads to learn the machine it is
 * on. built in one buffer, then handed out by SystemFirmwareTableInformation.
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

/* a structure field holding the index of a string, 0 when there is none. */
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
 * the uuid, from the logical byte order of the boot information to the
 * wire order of the tables: the first three fields are little endian.
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

/* the whole table, the header of RawSmbiosData included. returns its size. */
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
    smb_byte(&b, 0);    /* no sku */
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
    smb_byte(&b, 0xC4); /* arm 64 bit */
    smb_byte(&b, smb_string(&b, "Qualcomm Technologies Inc"));
    smb_qword(&b, KiProcessorIds.Midr);
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

    /* the end of the table. */
    smb_begin(&b, 127, 0x0005);
    smb_end(&b);
    return b.Size;
}

/*
 * SystemFirmwareTableInformation: the RSMB provider only, the raw smbios
 * data of the reference machine. the buffer protocol is the one of wdm:
 * a too small buffer reports the required size in TableBufferLength.
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

    UCHAR stack_table[512];
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

NTSTATUS NTAPI NtQuerySystemInformation(SYSTEM_INFORMATION_CLASS klass, PVOID info, ULONG length, PULONG returned)
{
    switch (klass) {
    case SystemBasicInformation:
    case SystemEmulationBasicInformation: {
        SYSTEM_BASIC_INFORMATION b = {0};
        b.TimerResolution = KI_CLOCK_TICK_HNS;
        b.PageSize = PAGE_SIZE;
        b.NumberOfPhysicalPages = (ULONG)MmGetTotalPages();
        b.LowestPhysicalPageNumber = (ULONG)(HalBootInfo->ram_base >> PAGE_SHIFT);
        b.HighestPhysicalPageNumber = (ULONG)((HalBootInfo->ram_base + HalBootInfo->ram_size) >> PAGE_SHIFT) - 1;
        b.AllocationGranularity = 0x10000;
        b.MinimumUserModeAddress = 0x10000;
        b.MaximumUserModeAddress = 0x7FFFFFFEFFFF;
        b.ActiveProcessorsAffinityMask = (1ull << HalBootInfo->cpu_count) - 1;
        b.NumberOfProcessors = (CCHAR)HalBootInfo->cpu_count;
        return copy_info(info, &b, sizeof(b), length, returned);
    }
    case SystemProcessorInformation:
    case SystemEmulationProcessorInformation: {
        SYSTEM_PROCESSOR_INFORMATION p = {0};
        p.ProcessorArchitecture = PROCESSOR_ARCHITECTURE_ARM64;
        p.ProcessorLevel = 8; /* the armv8 family, as in the processor identifier */
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
        } t = {0};
        t.BootTime.QuadPart = (LONGLONG)KeBootFiletime;
        KeQuerySystemTime(&t.CurrentTime);
        t.TimeZoneBias.QuadPart = (LONGLONG)HalBootInfo->timezone_bias * 60 * 10000000;
        return copy_info(info, &t, sizeof(t), length, returned);
    }
    case SystemPerformanceInformation: {
        UCHAR perf[0x158] = {0};
        *(PULONG)(perf + 0x4C) = (ULONG)MmGetFreePages(); /* AvailablePages */
        return copy_info(info, perf, sizeof(perf), length, returned);
    }
    case SystemKernelDebuggerInformation: {
        BOOLEAN dbg[2] = {FALSE, TRUE}; /* enabled, not present */
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
    case SystemHypervisorDetailInformation:
        /*
         * what a real installation without a hypervisor answers, since
         * nothing in the guest may report one.
         */
        return STATUS_NOT_SUPPORTED;
    default:
        return STATUS_INVALID_INFO_CLASS;
    }
}

NTSTATUS NTAPI ZwQuerySystemInformation(SYSTEM_INFORMATION_CLASS klass, PVOID info, ULONG length, PULONG returned)
{
    PKTHREAD t = KiCurrentThread();
    KPROCESSOR_MODE old = t->PreviousMode;
    t->PreviousMode = KernelMode;
    NTSTATUS status;
    /* kernel callers pass kernel buffers: KiCopyToUser refuses them, so copy directly. */
    if (klass == SystemBasicInformation && length >= sizeof(SYSTEM_BASIC_INFORMATION)) {
        SYSTEM_BASIC_INFORMATION *b = info;
        memset(b, 0, sizeof(*b));
        b->PageSize = PAGE_SIZE;
        b->NumberOfPhysicalPages = (ULONG)MmGetTotalPages();
        b->AllocationGranularity = 0x10000;
        b->MinimumUserModeAddress = 0x10000;
        b->MaximumUserModeAddress = 0x7FFFFFFEFFFF;
        b->NumberOfProcessors = (CCHAR)HalBootInfo->cpu_count;
        if (returned)
            *returned = sizeof(*b);
        status = STATUS_SUCCESS;
    } else {
        status = STATUS_NOT_IMPLEMENTED;
    }
    t->PreviousMode = old;
    return status;
}

NTSTATUS NTAPI NtQueryDefaultLocale(BOOLEAN user, LCID *locale)
{
    LCID l = 0x0409;
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
    return 0;
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

NTSTATUS NTAPI NtTraceEvent(HANDLE handle, ULONG flags, ULONG length, PVOID data)
{
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtTraceControl(ULONG code, PVOID in, ULONG in_len, PVOID out, ULONG out_len, PULONG returned)
{
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS NTAPI NtSetInformationVirtualMemory(HANDLE process, ULONG klass, ULONG_PTR count, PVOID entries, PVOID info,
                                             ULONG length)
{
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtOpenThreadToken(HANDLE thread, ACCESS_MASK access, BOOLEAN self, PHANDLE token)
{
    return STATUS_NO_TOKEN;
}

NTSTATUS NTAPI NtOpenThreadTokenEx(HANDLE thread, ACCESS_MASK access, BOOLEAN self, ULONG attributes, PHANDLE token)
{
    return STATUS_NO_TOKEN;
}

NTSTATUS NTAPI NtQueryLicenseValue(PUNICODE_STRING name, PULONG type, PVOID data, ULONG length, PULONG returned)
{
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

NTSTATUS NTAPI NtSetInformationObject(HANDLE handle, ULONG klass, PVOID info, ULONG length)
{
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtQueryInformationJobObject(HANDLE job, ULONG klass, PVOID info, ULONG length, PULONG returned)
{
    return STATUS_INVALID_HANDLE;
}

NTSTATUS NTAPI NtIsProcessInJob(HANDLE process, HANDLE job)
{
    return (NTSTATUS)0x00000124L; /* STATUS_PROCESS_NOT_IN_JOB */
}

NTSTATUS NTAPI NtApphelpCacheControl(ULONG type, PVOID data)
{
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS NTAPI NtQueryDebugFilterState(ULONG component, ULONG level)
{
    return TRUE;
}
