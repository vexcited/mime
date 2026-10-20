/*
 * the security descriptors of the files mimefs serves.
 *
 * A descriptor lives with its file, in the host's extended attributes; a file nobody gave one shows what an
 * NTFS volume would give it: what its nearest stored ancestor hands down, or the descriptor of a directory
 * that a fresh installation has (the root, Windows, Program Files and the profile of the user). Such a file
 * keeps nothing, so a file made by the user costs no more than on any other file system, and a directory
 * whose list changes shows the change in the files below that have no list of their own. The system files the
 * machine ships belong to the TrustedInstaller, as on windows.
 *
 * All of this is a guess at the volume of a real installation, made from what the images of windows show.
 */
#include "../ntos.h"
#include <mime/ntrtlsec.h>

/* the access masks of the volume's defaults. */
#define FILE_ALL 0x1F01FF
#define FILE_MODIFY 0x1301BF
#define FILE_READ_EXECUTE 0x1200A9
#define GENERIC_ALL_ 0x10000000
#define GENERIC_RX 0xA0000000
#define ACE_OI_CI (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE)
#define ACE_INHERIT_ONLY (OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE | INHERIT_ONLY_ACE)

static SD_MAPPING file_mapping(void)
{
    return IoFileObjectTypeObject->Init.GenericMapping;
}

static SD_BLOB *g_root, *g_system_directory, *g_profile, *g_rom_file;

/* the sid of the user, S-1-5-21-a-b-c-1001, and his primary group, None. */
static void user_sids(UCHAR user[28], UCHAR none[28])
{
    ULONG sub[5] = {21, HalBootInfo->user_sid[0], HalBootInfo->user_sid[1], HalBootInfo->user_sid[2], 1001};
    user[0] = 1;
    user[1] = 5;
    memset(user + 2, 0, 5);
    user[7] = 5;
    memcpy(user + 8, sub, sizeof(sub));
    memcpy(none, user, 28);
    *(ULONG *)(none + 24) = 513;
}

/* the sid of the user of the machine, 28 bytes. */
void MimeUserSid(UCHAR sid[28])
{
    UCHAR none[28];
    user_sids(sid, none);
}

static void build_seeds(void)
{
    UCHAR user[28], none[28];
    user_sids(user, none);
    SD_ACE root[] = {{ACE_OI_CI, FILE_ALL, SeSidSystem},
                     {ACE_OI_CI, FILE_ALL, SeSidAdministrators},
                     {ACE_OI_CI, FILE_READ_EXECUTE, SeSidUsers},
                     {ACE_INHERIT_ONLY, FILE_MODIFY, SeSidAuthenticatedUsers},
                     {0, 4, SeSidAuthenticatedUsers}};
    SeSdBuild(SeSidSystem, SeSidSystem, SE_DACL_AUTO_INHERITED | SE_DACL_PROTECTED, root, 5, &g_root);

    SD_ACE system[] = {{0, FILE_ALL, SeSidTrustedInstaller},
                       {CONTAINER_INHERIT_ACE | INHERIT_ONLY_ACE, GENERIC_ALL_, SeSidTrustedInstaller},
                       {0, FILE_MODIFY, SeSidSystem},
                       {ACE_INHERIT_ONLY, GENERIC_ALL_, SeSidSystem},
                       {0, FILE_MODIFY, SeSidAdministrators},
                       {ACE_INHERIT_ONLY, GENERIC_ALL_, SeSidAdministrators},
                       {0, FILE_READ_EXECUTE, SeSidUsers},
                       {ACE_INHERIT_ONLY, GENERIC_RX, SeSidUsers},
                       {0, FILE_READ_EXECUTE, SeSidAllAppPackages},
                       {ACE_INHERIT_ONLY, GENERIC_RX, SeSidAllAppPackages},
                       {0, FILE_READ_EXECUTE, SeSidRestrictedPackages},
                       {ACE_INHERIT_ONLY, GENERIC_RX, SeSidRestrictedPackages}};
    SeSdBuild(SeSidTrustedInstaller, SeSidTrustedInstaller, SE_DACL_AUTO_INHERITED | SE_DACL_PROTECTED, system, 12,
              &g_system_directory);

    SD_ACE profile[] = {{ACE_OI_CI, FILE_ALL, SeSidSystem},
                        {ACE_OI_CI, FILE_ALL, SeSidAdministrators},
                        {ACE_OI_CI, FILE_ALL, user}};
    SeSdBuild(user, none, SE_DACL_AUTO_INHERITED | SE_DACL_PROTECTED, profile, 3, &g_profile);

    SD_ACE rom[] = {{INHERITED_ACE, FILE_ALL, SeSidTrustedInstaller},
                    {INHERITED_ACE, FILE_READ_EXECUTE, SeSidSystem},
                    {INHERITED_ACE, FILE_READ_EXECUTE, SeSidAdministrators},
                    {INHERITED_ACE, FILE_READ_EXECUTE, SeSidUsers},
                    {INHERITED_ACE, FILE_READ_EXECUTE, SeSidAllAppPackages},
                    {INHERITED_ACE, FILE_READ_EXECUTE, SeSidRestrictedPackages}};
    SeSdBuild(SeSidTrustedInstaller, SeSidTrustedInstaller, SE_DACL_AUTO_INHERITED, rom, 6, &g_rom_file);
}

static FAST_MUTEX g_lock;
static BOOLEAN g_ready;

static void ready(void)
{
    if (g_ready)
        return;
    ExInitializeFastMutex(&g_lock);
    ExAcquireFastMutex(&g_lock);
    if (!g_ready) {
        build_seeds();
        g_ready = TRUE;
    }
    ExReleaseFastMutex(&g_lock);
}

/* ---------------------------------------------------------------- the path */

typedef struct {
    ULONG Count;
    const WCHAR *Start[40];
    ULONG Length[40];
} PATH;

static void split_path(PCUNICODE_STRING name, PATH *p)
{
    p->Count = 0;
    ULONG chars = name->Length / sizeof(WCHAR), i = 0;
    while (i < chars) {
        while (i < chars && name->Buffer[i] == '\\')
            i++;
        ULONG start = i;
        while (i < chars && name->Buffer[i] != '\\')
            i++;
        if (i > start && p->Count < RTL_NUMBER_OF(p->Start)) {
            p->Start[p->Count] = name->Buffer + start;
            p->Length[p->Count++] = i - start;
        }
    }
}

static BOOLEAN component_is(const PATH *p, ULONG index, PCWSTR text)
{
    ULONG length = (ULONG)wcslen(text);
    if (p->Length[index] != length)
        return FALSE;
    for (ULONG i = 0; i < length; i++)
        if (towupper(p->Start[index][i]) != towupper(text[i]))
            return FALSE;
    return TRUE;
}

/* the directory of a fresh installation a prefix of the path of this many components is, or NULL. */
static SD_BLOB *seed_at(const PATH *p, ULONG depth)
{
    if (depth == 0)
        return g_root;
    if (depth == 1 && (component_is(p, 0, L"Windows") || component_is(p, 0, L"Program Files") ||
                       component_is(p, 0, L"Program Files (x86)")))
        return g_system_directory;
    if (depth == 2 && component_is(p, 0, L"Users")) {
        WCHAR name[24];
        ULONG n = 0;
        for (; n < RTL_NUMBER_OF(name) - 1 && HalBootInfo->user_name[n]; n++)
            name[n] = (UCHAR)HalBootInfo->user_name[n];
        name[n] = 0;
        if (component_is(p, 1, name))
            return g_profile;
    }
    return nullptr;
}

/* ---------------------------------------------------------------- reading and writing */

#define MIMEFS_SD_ROOM 1024

static NTSTATUS host_status(ULONG64 hs)
{
    switch (hs) {
    case MIME_HC_STATUS_OK: return STATUS_SUCCESS;
    case MIME_HC_STATUS_NOT_FOUND: return STATUS_OBJECT_NAME_NOT_FOUND;
    case MIME_HC_STATUS_ACCESS_DENIED: return STATUS_ACCESS_DENIED;
    case MIME_HC_STATUS_NO_MEMORY: return STATUS_INSUFFICIENT_RESOURCES;
    default: return STATUS_UNSUCCESSFUL;
    }
}

/* the descriptor stored for a file or the nearest ancestor: *depth is the distance up, NULL when none. */
static NTSTATUS read_stored(ULONG64 host, ULONG up, SD_BLOB **stored, ULONG *distance)
{
    ULONG room = MIMEFS_SD_ROOM;
    *stored = nullptr;
    for (int attempt = 0; attempt < 2; attempt++) {
        PUCHAR buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, room, KI_TAG('M', 'f', 'S', 'd'));
        if (!buffer)
            return STATUS_INSUFFICIENT_RESOURCES;
        ULONG64 result = 0;
        ULONG64 hs = HalHypercall(MIME_HC_FS_GET_SD, host, HalPhysicalOf(buffer), room, up, 0, 0, &result);
        ULONG length = (ULONG)result;
        if (hs == MIME_HC_STATUS_NO_MEMORY && length > room && length <= SD_MAX_LENGTH) {
            ExFreePool(buffer);
            room = length;
            continue;
        }
        NTSTATUS status = hs == MIME_HC_STATUS_OK ? STATUS_SUCCESS : host_status(hs);
        if (NT_SUCCESS(status) && length) {
            /* something the host holds that is no descriptor is as good as nothing. */
            if (!NT_SUCCESS(SeSdFromBytes(buffer, length, stored)))
                *stored = nullptr;
            else
                *distance = (ULONG)(result >> 32);
        }
        ExFreePool(buffer);
        return status;
    }
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS MimeFsRomSecurity(SD_BLOB **out)
{
    ready();
    *out = SeSdReference(g_rom_file);
    return STATUS_SUCCESS;
}

/* what the host said of the stored descriptors when it opened a file, or nothing. */
static NTSTATUS stored_for(const MIMEFS_STORED *known, ULONG64 host, ULONG up, SD_BLOB **stored, ULONG *distance)
{
    *stored = nullptr;
    if (known && known->Status == MIME_HC_STATUS_OK && (!known->Length || known->Distance >= up)) {
        if (known->Length) {
            if (!NT_SUCCESS(SeSdFromBytes(known->Data, known->Length, stored)))
                *stored = nullptr;
            else
                *distance = known->Distance - up;
        }
        return STATUS_SUCCESS;
    }
    if (!host)
        return STATUS_MORE_PROCESSING_REQUIRED;
    return read_stored(host, up, stored, distance);
}

/*
 * the descriptor of the file `up` levels above the one open, whole. volume is the host volume, path the name
 * of the open file inside it. known is what the open told of the stored descriptors, if it was asked.
 */
static NTSTATUS effective(ULONG volume, ULONG64 host, PCUNICODE_STRING path, ULONG up, BOOLEAN directory,
                          const MIMEFS_STORED *known, SD_BLOB **out)
{
    ready();
    PATH p;
    split_path(path, &p);
    if (up > p.Count)
        return STATUS_OBJECT_PATH_NOT_FOUND;
    ULONG count = p.Count - up;
    SD_BLOB *stored;
    ULONG distance = 0;
    NTSTATUS status = stored_for(known, host, up, &stored, &distance);
    if (!NT_SUCCESS(status))
        return status;
    LONG stored_depth = stored && distance <= count ? (LONG)(count - distance) : -1;
    LONG seed_depth = 0;
    SD_BLOB *seed = g_root;
    for (LONG d = (LONG)count; d >= 0; d--) {
        SD_BLOB *s = seed_at(&p, (ULONG)d);
        if (s) {
            seed_depth = d;
            seed = s;
            break;
        }
    }
    SD_BLOB *base;
    LONG depth;
    if (stored && stored_depth >= seed_depth) {
        base = stored;
        depth = stored_depth;
    } else {
        base = SeSdReference(seed);
        depth = seed_depth;
        if (stored)
            SeSdDereference(stored);
    }
    /* each level between hands down to the next, which is a directory until the last. */
    for (LONG d = depth; d < (LONG)count && NT_SUCCESS(status); d++) {
        SD_BLOB *child = nullptr;
        status = SeSdDerive(base, file_mapping(), (d + 1 < (LONG)count) || directory, &child);
        SeSdDereference(base);
        base = NT_SUCCESS(status) ? child : nullptr;
    }
    if (!NT_SUCCESS(status))
        return status;
    *out = base;
    return STATUS_SUCCESS;
}

NTSTATUS MimeFsEffectiveSecurity(ULONG volume, ULONG64 host, PCUNICODE_STRING path, ULONG up, BOOLEAN directory,
                                 SD_BLOB **out)
{
    return effective(volume, host, path, up, directory, nullptr, out);
}

NTSTATUS MimeFsStoreSecurity(ULONG64 host, SD_BLOB *sd)
{
    ULONG64 hs = HalHypercall(MIME_HC_FS_SET_SD, host, HalPhysicalOf(sd->Data), sd->Length, 0, 0, 0, nullptr);
    return host_status(hs);
}

/* ---------------------------------------------------------------- the check of an open */

#define FILE_LIST_DIRECTORY_ 0x0001
#define FILE_WRITE_DATA_ 0x0002
#define FILE_ADD_SUBDIRECTORY_ 0x0004
#define FILE_DELETE_CHILD_ 0x0040
#define FILE_READ_ATTRIBUTES_ 0x0080
#define DELETE_ 0x00010000
#define MAXIMUM_ALLOWED_ 0x02000000

/*
 * whether a program in user mode may open a file the way it asks to, and with what rights its handle is made.
 *
 * A file that is made needs the right to add a file or a directory to the one it goes into, and the maker gets
 * what it asked for. A file that exists is checked against its own descriptor, with what the disposition adds: an
 * overwrite writes the data, a supersede deletes, delete on close deletes. Two rights also come from the directory
 * above, as on ntfs: delete child there is delete here, and list directory there is read attributes here.
 * Backup intent opens up to the privileges for backup and restore. (From the documentation of the file system's
 * behaviour, not read from the driver: the exact set the privileges grant is a guess.)
 *
 * STATUS_MORE_PROCESSING_REQUIRED says the answer needs the descriptor of the directory and the open has no handle to ask
 * the host with; the caller opens again with one.
 */
NTSTATUS MimeFsCheckOpen(ULONG volume, ULONG64 host, PCUNICODE_STRING path, BOOLEAN directory, ACCESS_MASK desired,
                         ULONG options, ULONG disposition, BOOLEAN created, const MIMEFS_STORED *known, ACCESS_MASK *granted)
{
    SD_MAPPING mapping = file_mapping();
    ULONG flags = (options & FILE_OPEN_FOR_BACKUP_INTENT) ? SE_ACCESS_BACKUP_INTENT : 0;
    SD_BLOB *sd = nullptr, *parent = nullptr;
    ACCESS_MASK result;
    NTSTATUS status;
    ready();
    if (created) {
        ACCESS_MASK right = directory ? FILE_ADD_SUBDIRECTORY_ : FILE_WRITE_DATA_;
        status = effective(volume, host, path, 1, TRUE, known, &parent);
        if (status == STATUS_OBJECT_PATH_NOT_FOUND)
            status = STATUS_SUCCESS;
        else if (NT_SUCCESS(status))
            status = SeCheckAccess(parent, mapping, right, 0, flags, &result);
        if (parent)
            SeSdDereference(parent);
        if (NT_SUCCESS(status)) {
            /* the maker has what it asked for, whatever the descriptor it just got says. */
            ACCESS_MASK mapped = desired & ~(ACCESS_MASK)MAXIMUM_ALLOWED_;
            RtlMapGenericMask(&mapped, (const GENERIC_MAPPING *)mapping);
            *granted = (desired & MAXIMUM_ALLOWED_) ? mapped | mapping[3] : mapped;
        }
        return status;
    }
    ACCESS_MASK want = desired;
    if (disposition == FILE_OVERWRITE || disposition == FILE_OVERWRITE_IF)
        want |= FILE_WRITE_DATA_;
    if (disposition == FILE_SUPERSEDE || (options & FILE_DELETE_ON_CLOSE))
        want |= DELETE_;
    status = effective(volume, host, path, 0, directory, known, &sd);
    if (!NT_SUCCESS(status))
        return status;
    status = SeCheckAccess(sd, mapping, want, 0, flags, granted);
    if (NT_SUCCESS(status) && !(desired & MAXIMUM_ALLOWED_)) {
        SeSdDereference(sd);
        return status;
    }
    if (!NT_SUCCESS(status) && SeTraceAccess) {
        KiLogPrintf("  the file");
        SeTraceDescriptor(sd);
    }
    /* the rights the directory above lends: what is left to ask for, or with the maximum what is added. */
    ACCESS_MASK mapped = want & ~(ACCESS_MASK)MAXIMUM_ALLOWED_;
    RtlMapGenericMask(&mapped, (const GENERIC_MAPPING *)mapping);
    BOOLEAN wants_loan = (mapped & (DELETE_ | FILE_READ_ATTRIBUTES_)) != 0 || (desired & MAXIMUM_ALLOWED_);
    if (status != STATUS_ACCESS_DENIED && !NT_SUCCESS(status)) {
        SeSdDereference(sd);
        return status;
    }
    if (!wants_loan) {
        SeSdDereference(sd);
        return status;
    }
    NTSTATUS up = effective(volume, host, path, 1, TRUE, known, &parent);
    if (!NT_SUCCESS(up)) {
        SeSdDereference(sd);
        return up == STATUS_MORE_PROCESSING_REQUIRED ? up : status;
    }
    ACCESS_MASK lent = 0, unused;
    if (SeTraceAccess) {
        KiLogPrintf("  the directory above");
        SeTraceDescriptor(parent);
    }
    if (SeCheckAccess(parent, mapping, FILE_DELETE_CHILD_, 0, flags, &unused) == STATUS_SUCCESS)
        lent |= DELETE_;
    if (SeCheckAccess(parent, mapping, FILE_LIST_DIRECTORY_, 0, flags, &unused) == STATUS_SUCCESS)
        lent |= FILE_READ_ATTRIBUTES_;
    SeSdDereference(parent);
    if (desired & MAXIMUM_ALLOWED_) {
        /* the maximum of the file, and the two rights if the directory lends them. */
        ACCESS_MASK own = 0;
        if (SeCheckAccess(sd, mapping, MAXIMUM_ALLOWED_, 0, flags, &own) != STATUS_SUCCESS)
            own = 0;
        ACCESS_MASK have = own | lent;
        *granted = have;
        status = have && !(mapped & ~have) ? STATUS_SUCCESS : STATUS_ACCESS_DENIED;
    } else {
        ACCESS_MASK rest = mapped & ~lent;
        if (rest != mapped)
            status = rest ? SeCheckAccess(sd, mapping, rest, 0, flags, granted) : STATUS_SUCCESS;
        if (NT_SUCCESS(status))
            *granted = mapped;
    }
    SeSdDereference(sd);
    return status;
}

/* the same for a file of the rom, which is read only: its descriptor is the one all of them share. */
NTSTATUS MimeFsCheckRomOpen(ACCESS_MASK desired, ULONG options, ACCESS_MASK *granted)
{
    ready();
    ULONG flags = (options & FILE_OPEN_FOR_BACKUP_INTENT) ? SE_ACCESS_BACKUP_INTENT : 0;
    return SeCheckAccess(g_rom_file, file_mapping(), desired, 0, flags, granted);
}

/*
 * the descriptor of a file that has just been made, from its creator and its parent, stored with it when the
 * creator asked for anything or does not make things the way the user of the machine does.
 */
NTSTATUS MimeFsAssignNewFile(ULONG volume, ULONG64 host, PCUNICODE_STRING path, BOOLEAN directory, SD_BLOB *creator,
                             KPROCESSOR_MODE mode)
{
    if (!creator && SeSdSubjectIsDefault())
        return STATUS_SUCCESS;
    SD_BLOB *parent = nullptr;
    PATH p;
    split_path(path, &p);
    NTSTATUS status = STATUS_SUCCESS;
    if (p.Count)
        status = MimeFsEffectiveSecurity(volume, host, path, 1, TRUE, &parent);
    if (!NT_SUCCESS(status))
        return status;
    SD_BLOB *sd;
    status = SeSdAssign(parent, creator, file_mapping(), directory, mode, &sd);
    if (parent)
        SeSdDereference(parent);
    if (!NT_SUCCESS(status))
        return status;
    status = MimeFsStoreSecurity(host, sd);
    SeSdDereference(sd);
    return status;
}
