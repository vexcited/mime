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

/*
 * the descriptor of the file `up` levels above the one open, whole. volume is the host volume, path the name
 * of the open file inside it.
 */
NTSTATUS MimeFsEffectiveSecurity(ULONG volume, ULONG64 host, PCUNICODE_STRING path, ULONG up, BOOLEAN directory,
                                 SD_BLOB **out)
{
    ready();
    PATH p;
    split_path(path, &p);
    if (up > p.Count)
        return STATUS_OBJECT_PATH_NOT_FOUND;
    ULONG count = p.Count - up;
    SD_BLOB *stored;
    ULONG distance = 0;
    NTSTATUS status = read_stored(host, up, &stored, &distance);
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

NTSTATUS MimeFsStoreSecurity(ULONG64 host, SD_BLOB *sd)
{
    ULONG64 hs = HalHypercall(MIME_HC_FS_SET_SD, host, HalPhysicalOf(sd->Data), sd->Length, 0, 0, 0, nullptr);
    return host_status(hs);
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
