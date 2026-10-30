/*
 * the firmware environment: the uefi variable store behind the NtQuerySystemEnvironmentValueEx family. the layers
 * are windows' own. the system calls validate the caller and take the privilege, the Hal functions turn a request
 * into runtime service calls and the firmware status into an NTSTATUS, and the store below them behaves like the
 * variable services of an edk2 firmware. variables with the non volatile attribute are kept on the drive and
 * come back after a restart, the others live as long as the machine does.
 */
#include "../ntos.h"
#include "../se/se.h"

#ifndef SE_SYSTEM_ENVIRONMENT_PRIVILEGE
#define SE_SYSTEM_ENVIRONMENT_PRIVILEGE 22
#endif

/* ------------------------------------------------------------------ uefi */

#define EFI_VARIABLE_NON_VOLATILE 0x01
#define EFI_VARIABLE_BOOTSERVICE_ACCESS 0x02
#define EFI_VARIABLE_RUNTIME_ACCESS 0x04
#define EFI_VARIABLE_HARDWARE_ERROR_RECORD 0x08
#define EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS 0x10
#define EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS 0x20
#define EFI_VARIABLE_APPEND_WRITE 0x40
#define EFI_VARIABLE_ATTRIBUTES_MASK 0x7F

typedef ULONG64 EFI_STATUS;
#define EFI_ERROR(code) (0x8000000000000000ull | (code))
#define EFI_SUCCESS 0ull
#define EFI_INVALID_PARAMETER EFI_ERROR(2)
#define EFI_UNSUPPORTED EFI_ERROR(3)
#define EFI_BUFFER_TOO_SMALL EFI_ERROR(5)
#define EFI_DEVICE_ERROR EFI_ERROR(7)
#define EFI_WRITE_PROTECTED EFI_ERROR(8)
#define EFI_OUT_OF_RESOURCES EFI_ERROR(9)
#define EFI_NOT_FOUND EFI_ERROR(14)
#define EFI_SECURITY_VIOLATION EFI_ERROR(26)

/* the table of Hal's HalpConvertEfiToNtStatus, which answers STATUS_UNSUCCESSFUL for what it does not hold. */
static const struct {
    EFI_STATUS Efi;
    NTSTATUS Status;
} g_status_map[] = {
    {EFI_SUCCESS, STATUS_SUCCESS},
    {EFI_ERROR(1), (NTSTATUS)0xC000026C},
    {EFI_ERROR(2), (NTSTATUS)0xC000000D},
    {EFI_ERROR(3), (NTSTATUS)0xC00000BB},
    {EFI_ERROR(4), (NTSTATUS)0xC0000206},
    {EFI_ERROR(5), (NTSTATUS)0xC0000023},
    {EFI_ERROR(6), (NTSTATUS)0xC00000A3},
    {EFI_ERROR(7), (NTSTATUS)0xC0000185},
    {EFI_ERROR(8), (NTSTATUS)0xC00000A2},
    {EFI_ERROR(9), (NTSTATUS)0xC0000454},
    {EFI_ERROR(10), (NTSTATUS)0xC0000185},
    {EFI_ERROR(11), (NTSTATUS)0xC000007F},
    {EFI_ERROR(12), (NTSTATUS)0xC0000178},
    {EFI_ERROR(13), (NTSTATUS)0x8000001C},
    {EFI_ERROR(14), (NTSTATUS)0xC0000100},
    {EFI_ERROR(15), (NTSTATUS)0xC0000022},
    {EFI_ERROR(16), (NTSTATUS)0xC0000185},
    {EFI_ERROR(17), (NTSTATUS)0xC0000088},
    {EFI_ERROR(18), (NTSTATUS)0xC00000B5},
    {EFI_ERROR(19), (NTSTATUS)0xC00000A3},
    {EFI_ERROR(20), (NTSTATUS)0x80000011},
    {EFI_ERROR(21), (NTSTATUS)0xC0000240},
    {EFI_ERROR(22), (NTSTATUS)0xC0000186},
    {EFI_ERROR(23), (NTSTATUS)0xC0000186},
    {EFI_ERROR(24), (NTSTATUS)0xC0000186},
    {EFI_ERROR(25), (NTSTATUS)0xC0000059},
    {EFI_ERROR(26), (NTSTATUS)0xC0000022},
    {EFI_ERROR(27), (NTSTATUS)0xC000003F},
    {1, (NTSTATUS)0xC00A0022},
    {2, (NTSTATUS)0xC0000121},
    {3, (NTSTATUS)0xC0000001},
    {4, (NTSTATUS)0xC0000023},
};

static NTSTATUS efi_to_status(EFI_STATUS efi)
{
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_status_map); i++)
        if (g_status_map[i].Efi == efi)
            return g_status_map[i].Status;
    return STATUS_UNSUCCESSFUL;
}

/* the limits of the store, those of a firmware with a quarter of a megabyte of flash for its variables. */
#define EFI_MAX_NAME_CHARS 255
#define EFI_MAX_VARIABLE_BYTES 0x10000
#define EFI_MAX_HARDWARE_ERROR_BYTES 0x8000
#define EFI_NV_CAPACITY 0x40000
#define EFI_VARIABLE_HEADER_BYTES 60
/* an authenticated write starts with an EFI_TIME and a WIN_CERTIFICATE_UEFI_GUID. */
#define EFI_AUTHENTICATION_2_BYTES 40

static const GUID g_efi_global = {0x8be4df61, 0x93ca, 0x11d2, {0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c}};
static const GUID g_efi_security = {0xd719b2cb, 0x3d3a, 0x4596, {0xa3, 0xbc, 0xda, 0xd0, 0x0e, 0x67, 0x65, 0x6f}};
static const GUID g_efi_hardware_error = {0x414e6bdd, 0xe47b, 0x47cc, {0xb2, 0x44, 0xbb, 0x61, 0x02, 0x0c, 0xf5, 0x16}};
/* the vendor of the kernel's own variables, which the system calls keep away from callers. */
static const GUID g_secure_boot_vendor = {0x77fa9abd, 0x0359, 0x4d32, {0xbd, 0x60, 0x28, 0xf4, 0xe7, 0x8f, 0x78, 0x4b}};

/* the firmware type of the machine, FirmwareTypeUefi. */
#define FIRMWARE_TYPE_UEFI 2
ULONG ExpFirmwareType = FIRMWARE_TYPE_UEFI;

typedef struct EFI_VAR {
    LIST_ENTRY Link;
    GUID Guid;
    ULONG Attributes;
    ULONG DataSize;
    ULONG NameChars;
    PWCHAR Name;
    PUCHAR Data;
} EFI_VAR;

static LIST_ENTRY g_variables;
static KMUTEX ExpEnvironmentLock;
static BOOLEAN g_loaded;

static ULONG wide_length(PCWSTR s)
{
    ULONG n = 0;
    while (s[n])
        n++;
    return n;
}

static ULONG align4(ULONG value)
{
    return (value + 3) & ~3u;
}

static BOOLEAN name_equals(PCWSTR a, PCWSTR b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

static EFI_VAR *variable_allocate(PCWSTR name, ULONG chars, const GUID *guid, ULONG attributes, ULONG data_size)
{
    EFI_VAR *var = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(EFI_VAR) + (chars + 1) * sizeof(WCHAR) + data_size,
                                   KI_TAG('E', 'f', 'i', 'V'));
    if (!var)
        return nullptr;
    var->Guid = *guid;
    var->Attributes = attributes;
    var->DataSize = data_size;
    var->NameChars = chars;
    var->Name = (PWCHAR)(var + 1);
    memcpy(var->Name, name, chars * sizeof(WCHAR));
    var->Name[chars] = 0;
    var->Data = (PUCHAR)(var->Name + chars + 1);
    return var;
}

/* what a variable takes of the flash: a header, the name and the data, each padded. */
static ULONG variable_cost(const EFI_VAR *var)
{
    return align4(EFI_VARIABLE_HEADER_BYTES + align4((var->NameChars + 1) * sizeof(WCHAR)) + var->DataSize);
}

/* the variables a program at runtime sees are the ones that allow runtime access. */
static EFI_VAR *variable_find(PCWSTR name, const GUID *guid)
{
    for (PLIST_ENTRY e = g_variables.Flink; e != &g_variables; e = e->Flink) {
        EFI_VAR *var = CONTAINING_RECORD(e, EFI_VAR, Link);
        if ((var->Attributes & EFI_VARIABLE_RUNTIME_ACCESS) && !memcmp(&var->Guid, guid, sizeof(GUID)) &&
            name_equals(var->Name, name))
            return var;
    }
    return nullptr;
}

/* ------------------------------------------------------------------ the non volatile part on the drive */

/*
 * two slots, each a header and every non volatile variable, written alternately. the header holds a sequence
 * number and a crc, so a write cut short leaves the previous slot to be read.
 */
#define NV_MAGIC 0x4946454Du /* "MEFI". */
#define NV_VERSION 1
#define NV_RECORD_HEADER 28

typedef struct NV_HEADER {
    ULONG Magic;
    ULONG Version;
    ULONG64 Sequence;
    ULONG Length;
    ULONG Crc;
} NV_HEADER;

#define NV_SLOT_BYTES (sizeof(NV_HEADER) + EFI_NV_CAPACITY)

static ULONG64 g_nv_sequence;

static ULONG crc32(ULONG crc, const UCHAR *p, SIZE_T size)
{
    static const ULONG nibble[16] = {0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4,
                                     0x4db26158, 0x5005713c, 0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c,
                                     0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c};
    crc = ~crc;
    while (size--) {
        crc ^= *p++;
        crc = nibble[crc & 15] ^ (crc >> 4);
        crc = nibble[crc & 15] ^ (crc >> 4);
    }
    return ~crc;
}

static ULONG header_crc(const NV_HEADER *header, const UCHAR *payload)
{
    NV_HEADER copy = *header;
    copy.Crc = 0;
    return crc32(crc32(0, (const UCHAR *)&copy, sizeof(copy)), payload, header->Length);
}

static NTSTATUS nv_open(BOOLEAN create, PFILE_OBJECT *file)
{
    UNICODE_STRING path = RTL_CONSTANT_STRING(L"\\??\\C:\\Windows\\System32\\config\\efivars.nvram");
    OBJECT_ATTRIBUTES attributes;
    InitializeObjectAttributes(&attributes, &path, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, nullptr, nullptr);
    HANDLE handle;
    IO_STATUS_BLOCK io;
    NTSTATUS status = IopCreateFile(&handle, FILE_READ_DATA | FILE_WRITE_DATA | SYNCHRONIZE, &attributes, &io,
                                    FILE_ATTRIBUTE_NORMAL, FILE_SHARE_READ, create ? FILE_OPEN_IF : FILE_OPEN,
                                    FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_WRITE_THROUGH,
                                    KernelMode);
    if (!NT_SUCCESS(status))
        return status;
    status = ObReferenceHandle(handle, IoFileObjectTypeObject, KernelMode, (PVOID *)file, nullptr);
    ZwClose(handle);
    return status;
}

static ULONG serialize(PUCHAR out)
{
    ULONG at = 0;
    for (PLIST_ENTRY e = g_variables.Flink; e != &g_variables; e = e->Flink) {
        EFI_VAR *var = CONTAINING_RECORD(e, EFI_VAR, Link);
        if (!(var->Attributes & EFI_VARIABLE_NON_VOLATILE))
            continue;
        ULONG name_bytes = (var->NameChars + 1) * sizeof(WCHAR);
        if (out) {
            memcpy(out + at, &var->Attributes, 4);
            memcpy(out + at + 4, &name_bytes, 4);
            memcpy(out + at + 8, &var->DataSize, 4);
            memcpy(out + at + 12, &var->Guid, 16);
            memcpy(out + at + NV_RECORD_HEADER, var->Name, name_bytes);
            memset(out + at + NV_RECORD_HEADER + name_bytes, 0, align4(name_bytes) - name_bytes);
            memcpy(out + at + NV_RECORD_HEADER + align4(name_bytes), var->Data, var->DataSize);
            memset(out + at + NV_RECORD_HEADER + align4(name_bytes) + var->DataSize, 0,
                   align4(var->DataSize) - var->DataSize);
        }
        at += NV_RECORD_HEADER + align4(name_bytes) + align4(var->DataSize);
    }
    return at;
}

/* writes every non volatile variable to the slot the newest write did not use. */
static NTSTATUS nv_save(void)
{
    ULONG length = serialize(nullptr);
    if (length > EFI_NV_CAPACITY)
        return STATUS_DISK_FULL;
    PUCHAR block = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(NV_HEADER) + length, KI_TAG('E', 'f', 'i', 'N'));
    if (!block)
        return STATUS_INSUFFICIENT_RESOURCES;
    serialize(block + sizeof(NV_HEADER));
    NV_HEADER header = {NV_MAGIC, NV_VERSION, g_nv_sequence + 1, length, 0};
    header.Crc = header_crc(&header, block + sizeof(NV_HEADER));
    memcpy(block, &header, sizeof(header));
    PFILE_OBJECT file;
    NTSTATUS status = nv_open(TRUE, &file);
    if (NT_SUCCESS(status)) {
        IO_STACK_LOCATION params = {};
        params.Parameters.Write.Length = sizeof(NV_HEADER) + length;
        params.Parameters.Write.ByteOffset.QuadPart = (LONGLONG)((header.Sequence & 1) * NV_SLOT_BYTES);
        IO_STATUS_BLOCK io = {};
        status = IopSynchronousRequest(file, IRP_MJ_WRITE, 0, block, sizeof(NV_HEADER) + length, &params, &io);
        if (NT_SUCCESS(status) && io.Information != sizeof(NV_HEADER) + length)
            status = STATUS_DISK_FULL;
        if (NT_SUCCESS(status))
            status = IopSynchronousRequest(file, IRP_MJ_FLUSH_BUFFERS, 0, nullptr, 0, nullptr, nullptr);
        ObDereferenceObject(file);
    }
    ExFreePool(block);
    if (NT_SUCCESS(status))
        g_nv_sequence = header.Sequence;
    else
        KiLogPrintf("firmware: variables not saved: %08x", status);
    return status;
}

/* the newest slot that is whole, or nothing. */
static PUCHAR nv_read_slot(PFILE_OBJECT file, ULONG64 file_size, ULONG slot, NV_HEADER *header)
{
    ULONG64 offset = (ULONG64)slot * NV_SLOT_BYTES;
    ULONG got;
    if (file_size < offset + sizeof(NV_HEADER) ||
        !NT_SUCCESS(IoReadFileObject(file, header, sizeof(*header), offset, &got)) || got != sizeof(*header) ||
        header->Magic != NV_MAGIC || header->Version != NV_VERSION || header->Length > EFI_NV_CAPACITY ||
        file_size < offset + sizeof(NV_HEADER) + header->Length)
        return nullptr;
    PUCHAR payload = ExAllocatePool2(POOL_FLAG_NON_PAGED, header->Length + 1, KI_TAG('E', 'f', 'i', 'N'));
    if (!payload)
        return nullptr;
    if (header->Length && (!NT_SUCCESS(IoReadFileObject(file, payload, header->Length, offset + sizeof(NV_HEADER), &got)) ||
                           got != header->Length)) {
        ExFreePool(payload);
        return nullptr;
    }
    if (header_crc(header, payload) != header->Crc) {
        ExFreePool(payload);
        return nullptr;
    }
    return payload;
}

static void nv_parse(const UCHAR *p, ULONG length)
{
    ULONG at = 0;
    while (at < length) {
        ULONG attributes, name_bytes, data_size;
        GUID guid;
        if (length - at < NV_RECORD_HEADER)
            break;
        memcpy(&attributes, p + at, 4);
        memcpy(&name_bytes, p + at + 4, 4);
        memcpy(&data_size, p + at + 8, 4);
        memcpy(&guid, p + at + 12, 16);
        ULONG room = length - at - NV_RECORD_HEADER;
        if (name_bytes < 2 || name_bytes > (EFI_MAX_NAME_CHARS + 1) * sizeof(WCHAR) || (name_bytes & 1) ||
            align4(name_bytes) > room || data_size > room - align4(name_bytes) ||
            !(attributes & EFI_VARIABLE_NON_VOLATILE))
            break;
        const WCHAR *name = (const WCHAR *)(p + at + NV_RECORD_HEADER);
        ULONG chars = name_bytes / sizeof(WCHAR) - 1;
        if (name[chars] || wide_length(name) != chars)
            break;
        EFI_VAR *var = variable_allocate(name, chars, &guid, attributes, data_size);
        if (!var)
            break;
        memcpy(var->Data, p + at + NV_RECORD_HEADER + align4(name_bytes), data_size);
        InsertTailList(&g_variables, &var->Link);
        at += NV_RECORD_HEADER + align4(name_bytes) + align4(data_size);
    }
}

static void nv_load(void)
{
    PFILE_OBJECT file;
    if (!NT_SUCCESS(nv_open(FALSE, &file)))
        return;
    ULONG64 size;
    if (NT_SUCCESS(IoQueryFileSize(file, &size))) {
        NV_HEADER best = {}, header;
        PUCHAR payload = nullptr;
        for (ULONG slot = 0; slot < 2; slot++) {
            PUCHAR candidate = nv_read_slot(file, size, slot, &header);
            if (!candidate)
                continue;
            if (!payload || header.Sequence > best.Sequence) {
                if (payload)
                    ExFreePool(payload);
                payload = candidate;
                best = header;
            } else {
                ExFreePool(candidate);
            }
        }
        if (payload) {
            nv_parse(payload, best.Length);
            g_nv_sequence = best.Sequence;
            ExFreePool(payload);
        } else if (size) {
            KiLogPrintf("firmware: the saved variables are damaged and left out");
        }
    }
    ObDereferenceObject(file);
}

/* ------------------------------------------------------------------ the variable services */

/*
 * the variables of the firmware setup: secure boot is on, its keys enrolled and the setup mode closed, as the uefi
 * configuration of a surface leaves it. the firmware owns them and refuses writes.
 */
static const struct {
    PCWSTR Name;
    UCHAR Value;
} g_firmware_variables[] = {
    {L"SecureBoot", 1}, {L"SetupMode", 0}, {L"AuditMode", 0}, {L"DeployedMode", 1}, {L"VendorKeys", 1},
};

static void store_populate(void)
{
    InitializeListHead(&g_variables);
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_firmware_variables); i++) {
        EFI_VAR *var = variable_allocate(g_firmware_variables[i].Name, wide_length(g_firmware_variables[i].Name), &g_efi_global,
                                         EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS, 1);
        if (!var)
            continue;
        var->Data[0] = g_firmware_variables[i].Value;
        InsertTailList(&g_variables, &var->Link);
    }
}

static void store_ready(void)
{
    if (g_loaded)
        return;
    g_loaded = TRUE;
    nv_load();
}

static BOOLEAN read_only(PCWSTR name, const GUID *guid)
{
    if (memcmp(guid, &g_efi_global, sizeof(GUID)))
        return FALSE;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_firmware_variables); i++)
        if (name_equals(name, g_firmware_variables[i].Name))
            return TRUE;
    return FALSE;
}

/* the keys of secure boot take only writes signed by a key above them, which no caller of ours can make. */
static BOOLEAN authenticated(PCWSTR name, const GUID *guid)
{
    static const PCWSTR global[] = {L"PK", L"KEK"};
    static const PCWSTR security[] = {L"db", L"dbx", L"dbt", L"dbr"};
    if (!memcmp(guid, &g_efi_global, sizeof(GUID))) {
        for (ULONG i = 0; i < RTL_NUMBER_OF(global); i++)
            if (name_equals(name, global[i]))
                return TRUE;
    } else if (!memcmp(guid, &g_efi_security, sizeof(GUID))) {
        for (ULONG i = 0; i < RTL_NUMBER_OF(security); i++)
            if (name_equals(name, security[i]))
                return TRUE;
    }
    return FALSE;
}

/* HwErrRec followed by four hexadecimal digits. */
static BOOLEAN hardware_error_name(PCWSTR name, const GUID *guid)
{
    static const WCHAR prefix[] = L"HwErrRec";
    if (memcmp(guid, &g_efi_hardware_error, sizeof(GUID)) || wide_length(name) != 12)
        return FALSE;
    for (ULONG i = 0; i < 8; i++)
        if (name[i] != prefix[i])
            return FALSE;
    for (ULONG i = 8; i < 12; i++)
        if (!((name[i] >= '0' && name[i] <= '9') || (name[i] >= 'A' && name[i] <= 'F')))
            return FALSE;
    return TRUE;
}

static ULONG nv_used(const EFI_VAR *except)
{
    ULONG used = 0;
    for (PLIST_ENTRY e = g_variables.Flink; e != &g_variables; e = e->Flink) {
        EFI_VAR *var = CONTAINING_RECORD(e, EFI_VAR, Link);
        if (var != except && (var->Attributes & EFI_VARIABLE_NON_VOLATILE))
            used += variable_cost(var);
    }
    return used;
}

/* GetVariable, with the copy of the data to the caller's buffer as a fault that is reported apart. */
static EFI_STATUS efi_get_variable(PCWSTR name, const GUID *guid, PULONG attributes, PULONG64 size, PVOID data,
                                   NTSTATUS *fault)
{
    EFI_VAR *var = variable_find(name, guid);
    if (!var)
        return EFI_NOT_FOUND;
    EFI_STATUS status = EFI_SUCCESS;
    if (*size >= var->DataSize) {
        if (!data)
            return EFI_INVALID_PARAMETER;
        *fault = KiCopyToUser(data, var->Data, var->DataSize);
        if (!NT_SUCCESS(*fault))
            return EFI_DEVICE_ERROR;
    } else {
        status = EFI_BUFFER_TOO_SMALL;
    }
    *size = var->DataSize;
    if (attributes)
        *attributes = var->Attributes;
    return status;
}

/* SetVariable: a write of nothing or without access attributes deletes, as the specification has it. */
static EFI_STATUS efi_set_variable(PCWSTR name, const GUID *guid, ULONG attributes, ULONG64 size, const void *data)
{
    ULONG chars = wide_length(name);
    if (!chars || chars > EFI_MAX_NAME_CHARS)
        return EFI_INVALID_PARAMETER;
    if (attributes & ~EFI_VARIABLE_ATTRIBUTES_MASK)
        return EFI_INVALID_PARAMETER;
    if ((attributes & (EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS)) == EFI_VARIABLE_RUNTIME_ACCESS)
        return EFI_INVALID_PARAMETER;
    if (attributes & EFI_VARIABLE_HARDWARE_ERROR_RECORD) {
        if ((attributes & 7) != 7 || !hardware_error_name(name, guid))
            return EFI_INVALID_PARAMETER;
    }
    if (attributes & EFI_VARIABLE_AUTHENTICATED_WRITE_ACCESS)
        return EFI_UNSUPPORTED;
    if (read_only(name, guid))
        return EFI_WRITE_PROTECTED;
    if (attributes & EFI_VARIABLE_TIME_BASED_AUTHENTICATED_WRITE_ACCESS) {
        /* the signature cannot be checked against keys that are not there, so the write is refused. */
        return size < EFI_AUTHENTICATION_2_BYTES ? EFI_INVALID_PARAMETER : EFI_SECURITY_VIOLATION;
    }
    if (authenticated(name, guid))
        return EFI_SECURITY_VIOLATION;

    EFI_VAR *old = variable_find(name, guid);
    BOOLEAN append = (attributes & EFI_VARIABLE_APPEND_WRITE) != 0;
    attributes &= ~EFI_VARIABLE_APPEND_WRITE;
    if (size == 0 || !(attributes & (EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS))) {
        if (append && size == 0)
            return EFI_SUCCESS;
        if (!old)
            return EFI_NOT_FOUND;
        PLIST_ENTRY before = old->Link.Blink;
        RemoveEntryList(&old->Link);
        if ((old->Attributes & EFI_VARIABLE_NON_VOLATILE) && !NT_SUCCESS(nv_save())) {
            InsertHeadList(before, &old->Link);
            return EFI_DEVICE_ERROR;
        }
        ExFreePool(old);
        return EFI_SUCCESS;
    }

    ULONG limit = (attributes & EFI_VARIABLE_HARDWARE_ERROR_RECORD) ? EFI_MAX_HARDWARE_ERROR_BYTES
                                                                     : EFI_MAX_VARIABLE_BYTES;
    ULONG kept = old && append ? old->DataSize : 0;
    ULONG overhead = EFI_VARIABLE_HEADER_BYTES + align4((chars + 1) * sizeof(WCHAR)) + kept;
    if (overhead > limit || size > limit - overhead)
        return EFI_INVALID_PARAMETER;
    if (old && (old->Attributes != attributes))
        return EFI_INVALID_PARAMETER;
    EFI_VAR *var = variable_allocate(name, chars, guid, attributes, kept + (ULONG)size);
    if (!var)
        return EFI_OUT_OF_RESOURCES;
    if (kept)
        memcpy(var->Data, old->Data, kept);
    memcpy(var->Data + kept, data, (SIZE_T)size);
    if ((attributes & EFI_VARIABLE_NON_VOLATILE) && nv_used(old) + variable_cost(var) > EFI_NV_CAPACITY) {
        ExFreePool(var);
        return EFI_OUT_OF_RESOURCES;
    }
    PLIST_ENTRY before = old ? old->Link.Blink : nullptr;
    if (old)
        RemoveEntryList(&old->Link);
    InsertTailList(&g_variables, &var->Link);
    if ((attributes & EFI_VARIABLE_NON_VOLATILE) && !NT_SUCCESS(nv_save())) {
        RemoveEntryList(&var->Link);
        if (old)
            InsertHeadList(before, &old->Link);
        ExFreePool(var);
        return EFI_DEVICE_ERROR;
    }
    if (old)
        ExFreePool(old);
    return EFI_SUCCESS;
}

/* ------------------------------------------------------------------ hal */

/* a name longer than the 255 characters the runtime keeps comes out empty, which names nothing. */
static PCWSTR hal_name(PCWSTR name)
{
    ULONG n = 0;
    while (n <= EFI_MAX_NAME_CHARS && name[n])
        n++;
    return n > EFI_MAX_NAME_CHARS ? L"" : name;
}

static NTSTATUS HalGetEnvironmentVariableEx(PCWSTR name, const GUID *guid, PVOID data, PULONG size, PULONG attributes)
{
    NTSTATUS fault = STATUS_SUCCESS;
    ULONG64 length = *size;
    EFI_STATUS efi = efi_get_variable(hal_name(name), guid, attributes, &length, data, &fault);
    *size = (ULONG)length;
    return NT_SUCCESS(fault) ? efi_to_status(efi) : fault;
}

/*
 * a write deletes the variable first and then writes it with the runtime and boot service attributes added, so
 * a write that fails leaves the old value gone. the variable must be non volatile.
 */
static NTSTATUS HalSetEnvironmentVariableEx(PCWSTR name, const GUID *guid, const void *data, ULONG size,
                                            ULONG attributes)
{
    if (!(attributes & EFI_VARIABLE_NON_VOLATILE))
        return STATUS_INVALID_PARAMETER;
    name = hal_name(name);
    NTSTATUS status;
    if (attributes & EFI_VARIABLE_APPEND_WRITE)
        status = STATUS_INVALID_PARAMETER;
    else
        status = efi_to_status(efi_set_variable(name, guid, 0, 0, nullptr));
    if (size)
        status = efi_to_status(efi_set_variable(name, guid,
                                                attributes | EFI_VARIABLE_BOOTSERVICE_ACCESS | EFI_VARIABLE_RUNTIME_ACCESS,
                                                size, data));
    return status;
}

typedef struct ENUM_OUT {
    PUCHAR Base;
    NTSTATUS Fault;
} ENUM_OUT;

static void enum_put(ENUM_OUT *out, ULONG offset, const void *src, ULONG length)
{
    if (NT_SUCCESS(out->Fault))
        out->Fault = KiCopyToUser(out->Base + offset, src, length);
}

/*
 * writes the names, or the names with their values and attributes, of the variables that fit the buffer and
 * counts the room all of them need, which comes back as the length with STATUS_BUFFER_TOO_SMALL.
 */
static NTSTATUS HalEnumerateEnvironmentVariablesEx(ULONG klass, PUCHAR buffer, PULONG length)
{
    if (klass - 1 > 1 || ((ULONG_PTR)buffer & 3))
        return STATUS_INVALID_PARAMETER;
    ENUM_OUT out = {buffer, STATUS_SUCCESS};
    ULONG header = klass == 1 ? 20 : 32;
    ULONG remaining = *length;
    ULONG offset = 0, last = 0;
    BOOLEAN have_last = FALSE, writing = remaining != 0;
    NTSTATUS result = STATUS_SUCCESS;
    for (PLIST_ENTRY e = g_variables.Flink; e != &g_variables; e = e->Flink) {
        EFI_VAR *var = CONTAINING_RECORD(e, EFI_VAR, Link);
        if (!(var->Attributes & EFI_VARIABLE_RUNTIME_ACCESS))
            continue;
        ULONG aligned = align4(offset);
        if (aligned != offset) {
            ULONG pad = aligned - offset;
            offset = aligned;
            if (remaining < pad) {
                writing = FALSE;
                remaining = 0;
                result = STATUS_BUFFER_TOO_SMALL;
            } else {
                remaining -= pad;
            }
        }
        ULONG name_bytes = (var->NameChars + 1) * sizeof(WCHAR);
        ULONG need = name_bytes + header;
        if (klass != 1)
            need = align4(need);
        if (remaining < need)
            result = STATUS_BUFFER_TOO_SMALL;
        BOOLEAN fits = remaining >= need;
        ULONG after = fits ? remaining - need : 0;
        writing = fits && writing;
        if (klass == 1) {
            if (writing) {
                enum_put(&out, offset + 4, &var->Guid, sizeof(GUID));
                enum_put(&out, offset + 20, var->Name, name_bytes);
                if (have_last) {
                    ULONG next = offset - last;
                    enum_put(&out, last, &next, 4);
                }
                last = offset;
                have_last = TRUE;
            }
            remaining = after;
            offset += need;
            continue;
        }
        if (writing) {
            enum_put(&out, offset + 16, &var->Guid, sizeof(GUID));
            enum_put(&out, offset + 32, var->Name, name_bytes);
            ULONG value_offset = need;
            enum_put(&out, offset + 4, &value_offset, 4);
            enum_put(&out, offset + 12, &var->Attributes, 4);
        }
        if (writing && after >= var->DataSize) {
            enum_put(&out, offset + need, var->Data, var->DataSize);
            enum_put(&out, offset + 8, &var->DataSize, 4);
            remaining = after - var->DataSize;
            if (have_last) {
                ULONG next = offset - last;
                enum_put(&out, last, &next, 4);
            }
            last = offset;
            have_last = TRUE;
        } else {
            remaining = 0;
            result = STATUS_BUFFER_TOO_SMALL;
            writing = FALSE;
        }
        offset += var->DataSize + need;
    }
    if (have_last) {
        ULONG zero = 0;
        enum_put(&out, last, &zero, 4);
    }
    *length = offset;
    return NT_SUCCESS(out.Fault) ? result : out.Fault;
}

/* ------------------------------------------------------------------ kernel callers */

static PWCHAR name_copy(PCUNICODE_STRING name)
{
    PWCHAR copy = ExAllocatePool2(POOL_FLAG_NON_PAGED, (SIZE_T)name->Length + sizeof(WCHAR), KI_TAG('E', 'n', 'v', 'r'));
    if (!copy)
        return nullptr;
    memcpy(copy, name->Buffer, name->Length);
    copy[name->Length / sizeof(WCHAR)] = 0;
    return copy;
}

static void lock_store(void)
{
    KeWaitForSingleObject(&ExpEnvironmentLock, Executive, KernelMode, FALSE, nullptr);
    store_ready();
}

static void unlock_store(void)
{
    KeReleaseMutex(&ExpEnvironmentLock, FALSE);
}

void ExpInitializeFirmware(void)
{
    KeInitializeMutex(&ExpEnvironmentLock, 0);
    store_populate();
}

/* what the kernel tells of the firmware type, the way ExGetFirmwareType does. */
ULONG NTAPI ExGetFirmwareType(void)
{
    return ExpFirmwareType;
}

/* the attributes always come back, the 0xDEADBEEF of the stack when the variable is not there. */
static NTSTATUS get_variable(PCWSTR name, const GUID *guid, PVOID buffer, PULONG size, PULONG attributes)
{
    ULONG value = 0xDEADBEEF;
    lock_store();
    NTSTATUS status = HalGetEnvironmentVariableEx(name, guid, buffer, size, &value);
    unlock_store();
    if (attributes)
        *attributes = value;
    return status;
}

NTSTATUS NTAPI ExGetFirmwareEnvironmentVariable(PUNICODE_STRING name, PGUID guid, PVOID buffer, PULONG size,
                                                PULONG attributes)
{
    if (ExpFirmwareType != FIRMWARE_TYPE_UEFI)
        return STATUS_NOT_IMPLEMENTED;
    PWCHAR copy = name_copy(name);
    if (!copy)
        return STATUS_INSUFFICIENT_RESOURCES;
    NTSTATUS status = get_variable(copy, guid, buffer, size, attributes);
    ExFreePool(copy);
    return status;
}

static NTSTATUS set_variable(PCWSTR name, const GUID *guid, const void *value, ULONG length, ULONG attributes)
{
    lock_store();
    NTSTATUS status = HalSetEnvironmentVariableEx(name, guid, value, length, attributes);
    unlock_store();
    return status;
}

NTSTATUS NTAPI ExSetFirmwareEnvironmentVariable(PUNICODE_STRING name, PGUID guid, PVOID value, ULONG length,
                                                ULONG attributes)
{
    if (ExpFirmwareType != FIRMWARE_TYPE_UEFI)
        return STATUS_NOT_IMPLEMENTED;
    PWCHAR copy = name_copy(name);
    if (!copy)
        return STATUS_INSUFFICIENT_RESOURCES;
    NTSTATUS status = set_variable(copy, guid, value, value ? length : 0, attributes);
    ExFreePool(copy);
    return status;
}

/* ------------------------------------------------------------------ caller memory */

/* touches each page of a buffer the way the lock on its pages does, so what cannot be reached is found first. */
static NTSTATUS lock_user_buffer(PVOID address, SIZE_T length, BOOLEAN write)
{
    if (!length || KeGetPreviousMode() != UserMode)
        return STATUS_SUCCESS;
    if (!KiIsUserRange(address, length))
        return STATUS_ACCESS_VIOLATION;
    ULONG_PTR end = (ULONG_PTR)address + length;
    KJUMP_BUFFER jb;
    if (!KI_TRY_USER(jb))
        return KiFaultResult();
    for (ULONG_PTR p = (ULONG_PTR)address; p < end; p = (p & ~(ULONG_PTR)0xFFF) + 0x1000) {
        volatile UCHAR *byte = (volatile UCHAR *)p;
        UCHAR value = *byte;
        if (write)
            __atomic_compare_exchange_n(byte, &value, value, FALSE, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
    }
    KI_END_TRY_USER();
    return STATUS_SUCCESS;
}

/* the 16 bytes of a string header, read after the checks windows makes on the pointer and the buffer. */
static NTSTATUS read_name_header(PCUNICODE_STRING user, UNICODE_STRING *header, NTSTATUS empty_status)
{
    if ((ULONG_PTR)user & 3)
        return STATUS_DATATYPE_MISALIGNMENT;
    NTSTATUS status = KiCopyFromUser(header, user, sizeof(*header));
    if (!NT_SUCCESS(status))
        return status;
    if (!header->Length)
        return empty_status;
    if ((ULONG_PTR)header->Buffer & 1)
        return STATUS_DATATYPE_MISALIGNMENT;
    if (header->Length && !KiIsUserRange(header->Buffer, header->Length))
        return STATUS_ACCESS_VIOLATION;
    return STATUS_SUCCESS;
}

static NTSTATUS read_guid(PGUID user, GUID *guid)
{
    if ((ULONG_PTR)user & 3)
        return STATUS_DATATYPE_MISALIGNMENT;
    return KiCopyFromUser(guid, user, sizeof(*guid));
}

static NTSTATUS capture_name(const UNICODE_STRING *header, PWCHAR *copy)
{
    *copy = ExAllocatePool2(POOL_FLAG_NON_PAGED, (SIZE_T)header->Length + sizeof(WCHAR), KI_TAG('E', 'n', 'v', 'r'));
    if (!*copy)
        return STATUS_INSUFFICIENT_RESOURCES;
    NTSTATUS status = KiCopyFromUser(*copy, header->Buffer, header->Length);
    if (!NT_SUCCESS(status)) {
        ExFreePool(*copy);
        *copy = nullptr;
        return status;
    }
    (*copy)[header->Length / sizeof(WCHAR)] = 0;
    return STATUS_SUCCESS;
}

/*
 * an app container process holds the privilege only through the firmware capabilities and a signed process or
 * a machine in test signing mode, which this system has neither of, so it never passes where the token fails.
 */
static BOOLEAN has_privilege(void)
{
    return SepSinglePrivilegeCheck(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, UserMode);
}

/* ------------------------------------------------------------------ system calls */

NTSTATUS NTAPI NtQuerySystemEnvironmentValueEx(PUNICODE_STRING name, PGUID guid, PVOID buffer, PULONG size,
                                               PULONG attributes)
{
    if (KeGetPreviousMode() == KernelMode)
        return ExGetFirmwareEnvironmentVariable(name, guid, buffer, size, attributes);
    if (!has_privilege())
        return STATUS_PRIVILEGE_NOT_HELD;
    if (ExpFirmwareType != FIRMWARE_TYPE_UEFI)
        return STATUS_NOT_IMPLEMENTED;
    UNICODE_STRING header;
    NTSTATUS status = read_name_header(name, &header, STATUS_ACCESS_VIOLATION);
    if (!NT_SUCCESS(status))
        return status;
    if ((ULONG_PTR)guid & 3)
        return STATUS_DATATYPE_MISALIGNMENT;
    GUID vendor;
    ULONG capacity;
    if (!NT_SUCCESS(status = lock_user_buffer(guid, 1, FALSE)) || !NT_SUCCESS(status = lock_user_buffer(size, 4, TRUE)) ||
        !NT_SUCCESS(status = KiCopyFromUser(&capacity, size, sizeof(capacity))))
        return status;
    if (!buffer)
        capacity = 0;
    /* probing the buffer for writing touches each page of it. */
    if (!NT_SUCCESS(status = lock_user_buffer(buffer, capacity, TRUE)))
        return status;
    if (attributes && !NT_SUCCESS(status = lock_user_buffer(attributes, 4, TRUE)))
        return status;
    status = read_guid(guid, &vendor);
    if (!NT_SUCCESS(status))
        return status;
    PWCHAR copy;
    status = capture_name(&header, &copy);
    if (!NT_SUCCESS(status))
        return status;
    ULONG value = 0;
    ULONG length = capacity;
    lock_store();
    status = HalGetEnvironmentVariableEx(copy, &vendor, capacity ? buffer : nullptr, &length, &value);
    unlock_store();
    if (NT_SUCCESS(status) || status == STATUS_BUFFER_TOO_SMALL)
        KiCopyToUser(size, &length, sizeof(length));
    if (attributes && NT_SUCCESS(status))
        KiCopyToUser(attributes, &value, sizeof(value));
    ExFreePool(copy);
    return status;
}

NTSTATUS NTAPI NtSetSystemEnvironmentValueEx(PUNICODE_STRING name, PGUID guid, PVOID value, ULONG length,
                                             ULONG attributes)
{
    if (ExpFirmwareType != FIRMWARE_TYPE_UEFI)
        return STATUS_NOT_IMPLEMENTED;
    if (KeGetPreviousMode() == KernelMode)
        return ExSetFirmwareEnvironmentVariable(name, guid, value, length, attributes);
    UNICODE_STRING header;
    NTSTATUS status = read_name_header(name, &header, STATUS_ACCESS_VIOLATION);
    if (!NT_SUCCESS(status))
        return status;
    if ((ULONG_PTR)guid & 3)
        return STATUS_DATATYPE_MISALIGNMENT;
    status = lock_user_buffer(guid, 1, FALSE);
    if (!NT_SUCCESS(status))
        return status;
    if (!value)
        length = 0;
    if (length && !KiIsUserRange(value, length))
        return STATUS_ACCESS_VIOLATION;
    if (!has_privilege())
        return STATUS_PRIVILEGE_NOT_HELD;
    GUID vendor;
    status = read_guid(guid, &vendor);
    if (!NT_SUCCESS(status))
        return status;
    PWCHAR copy;
    status = capture_name(&header, &copy);
    if (!NT_SUCCESS(status))
        return status;
    if (!memcmp(&vendor, &g_secure_boot_vendor, sizeof(GUID)) && !_wcsnicmp(copy, L"Kernel_", 7)) {
        ExFreePool(copy);
        return STATUS_ACCESS_DENIED;
    }
    /* a value that is too long to keep is still read, since the write deletes the variable before it fails. */
    PUCHAR data = nullptr;
    status = STATUS_SUCCESS;
    if (length && length <= EFI_MAX_VARIABLE_BYTES) {
        data = ExAllocatePool2(POOL_FLAG_NON_PAGED, length, KI_TAG('E', 'n', 'v', 'r'));
        status = data ? KiCopyFromUser(data, value, length) : STATUS_INSUFFICIENT_RESOURCES;
    } else if (length) {
        status = lock_user_buffer(value, length, FALSE);
    }
    if (NT_SUCCESS(status))
        status = set_variable(copy, &vendor, data, length, attributes);
    if (data)
        ExFreePool(data);
    ExFreePool(copy);
    return status;
}

NTSTATUS NTAPI NtEnumerateSystemEnvironmentValuesEx(ULONG information_class, PVOID buffer, PULONG length)
{
    if (ExpFirmwareType != FIRMWARE_TYPE_UEFI)
        return STATUS_NOT_IMPLEMENTED;
    ULONG capacity;
    NTSTATUS status;
    if (KeGetPreviousMode() == UserMode) {
        if (!NT_SUCCESS(status = lock_user_buffer(length, 4, TRUE)) ||
            !NT_SUCCESS(status = KiCopyFromUser(&capacity, length, sizeof(capacity))))
            return status;
        if (!buffer)
            capacity = 0;
        if (capacity) {
            if ((ULONG_PTR)buffer & 3)
                return STATUS_DATATYPE_MISALIGNMENT;
            if (!NT_SUCCESS(status = lock_user_buffer(buffer, capacity, TRUE)))
                return status;
        }
        if (!has_privilege())
            return STATUS_PRIVILEGE_NOT_HELD;
    } else {
        capacity = *length;
        if (!buffer)
            capacity = 0;
    }
    lock_store();
    status = HalEnumerateEnvironmentVariablesEx(information_class, capacity ? buffer : nullptr, &capacity);
    unlock_store();
    KiCopyToUser(length, &capacity, sizeof(capacity));
    return status;
}

/*
 * the names the machine had before uefi, kept in the nvram of a bios. a uefi machine has none of them, so the
 * calls end in their own failures once the caller's memory and privilege have been looked at.
 */
NTSTATUS NTAPI NtQuerySystemEnvironmentValue(PUNICODE_STRING name, PWSTR value, USHORT length, PUSHORT returned)
{
    if (KeGetPreviousMode() == UserMode) {
        UNICODE_STRING header;
        NTSTATUS status = read_name_header(name, &header, STATUS_ACCESS_VIOLATION);
        if (!NT_SUCCESS(status))
            return status;
        if (length && ((ULONG_PTR)value & 1))
            return STATUS_DATATYPE_MISALIGNMENT;
        if (length && !NT_SUCCESS(status = lock_user_buffer(value, length, TRUE)))
            return status;
        if (returned && !NT_SUCCESS(status = lock_user_buffer(returned, 2, TRUE)))
            return status;
        if (!has_privilege())
            return STATUS_PRIVILEGE_NOT_HELD;
        PWCHAR copy;
        status = capture_name(&header, &copy);
        if (!NT_SUCCESS(status))
            return status;
        ExFreePool(copy);
    }
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS NTAPI NtSetSystemEnvironmentValue(PUNICODE_STRING name, PUNICODE_STRING value)
{
    if (KeGetPreviousMode() == UserMode) {
        UNICODE_STRING name_header, value_header;
        NTSTATUS status = read_name_header(name, &name_header, STATUS_INSUFFICIENT_RESOURCES);
        if (!NT_SUCCESS(status))
            return status;
        status = read_name_header(value, &value_header, STATUS_INSUFFICIENT_RESOURCES);
        if (!NT_SUCCESS(status))
            return status;
        if (!has_privilege())
            return STATUS_PRIVILEGE_NOT_HELD;
        PWCHAR name_copy_buffer, value_copy_buffer;
        status = capture_name(&name_header, &name_copy_buffer);
        if (!NT_SUCCESS(status))
            return status;
        status = capture_name(&value_header, &value_copy_buffer);
        ExFreePool(name_copy_buffer);
        if (!NT_SUCCESS(status))
            return status;
        ExFreePool(value_copy_buffer);
    }
    return STATUS_INSUFFICIENT_RESOURCES;
}
