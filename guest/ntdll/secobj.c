/*
 * private object security on tokens: the descriptors of objects made from a token's defaults and the routines
 * that need the token itself. The descriptor algorithms are in guest/rtl/secobj.c, shared with the kernel.
 *
 * the owner, privilege and mandatory label checks windows makes on the caller, and the removal of inherited
 * aces an explicit one already covers, are not done.
 */
#include "ntdll_private.h"
#include <crypto/crypto.h>
#include <mime/ntrtlsec.h>

NTSYSCALLAPI NTSTATUS NTAPI NtQuerySecurityObject(HANDLE handle, SECURITY_INFORMATION information,
                                                  PSECURITY_DESCRIPTOR sd, ULONG length, PULONG needed);

/* the privilege set of winnt.h, with one privilege. */
typedef struct {
    ULONG PrivilegeCount;
    ULONG Control;
    struct {
        LUID Luid;
        ULONG Attributes;
    } Privilege[1];
} PRIVILEGE_SET_ONE;

#define TOKEN_QUERY_ACCESS 0x0008
#define TokenOwnerClass 4
#define TokenPrimaryGroupClass 5
#define TokenDefaultDaclClass 6

#define HEAP() (NtCurrentPeb()->ProcessHeap)


/* ---------------------------------------------------------------- the token defaults */

typedef struct {
    HANDLE requested; /* the token the caller named, else the thread's or the process's */
    HANDLE handle;
    BOOLEAN opened;
    BOOLEAN tried;
    NTSTATUS status;
    UCHAR *buffers[3];
} DEFAULTS;

static NTSTATUS token_handle(DEFAULTS *d, HANDLE token)
{
    if (token) {
        d->handle = token;
        return STATUS_SUCCESS;
    }
    NTSTATUS status = NtOpenThreadToken(NtCurrentThread(), TOKEN_QUERY_ACCESS, TRUE, &d->handle);
    if (!NT_SUCCESS(status))
        status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY_ACCESS, &d->handle);
    d->opened = NT_SUCCESS(status);
    return status;
}

/* the one pointer a token information class holds, backed by a heap block that lives with the defaults. */
static NTSTATUS token_pointer(DEFAULTS *d, ULONG klass, int slot, PVOID *out)
{
    ULONG length = 512;
    for (;;) {
        UCHAR *block = RtlAllocateHeap(HEAP(), 0, length);
        if (!block)
            return STATUS_NO_MEMORY;
        ULONG needed = 0;
        NTSTATUS status = NtQueryInformationToken(d->handle, klass, block, length, &needed);
        if (status == STATUS_BUFFER_TOO_SMALL && needed > length) {
            RtlFreeHeap(HEAP(), 0, block);
            length = needed;
            continue;
        }
        if (!NT_SUCCESS(status)) {
            RtlFreeHeap(HEAP(), 0, block);
            return status;
        }
        d->buffers[slot] = block;
        *out = *(PVOID *)block;
        return STATUS_SUCCESS;
    }
}

static void defaults_free(DEFAULTS *d)
{
    for (int i = 0; i < 3; i++)
        if (d->buffers[i])
            RtlFreeHeap(HEAP(), 0, d->buffers[i]);
    if (d->opened)
        NtClose(d->handle);
}


/* the token defaults for the rtl, opened on the first one asked for. */
static NTSTATUS fetch_default(void *context, ULONG which, PVOID *value)
{
    DEFAULTS *d = context;
    if (!d->tried) {
        d->tried = TRUE;
        d->status = token_handle(d, d->requested);
    }
    if (!NT_SUCCESS(d->status))
        return d->status;
    static const ULONG classes[3] = {TokenOwnerClass, TokenPrimaryGroupClass, TokenDefaultDaclClass};
    return token_pointer(d, classes[which], (int)which, value);
}

static NTSTATUS new_object(PSECURITY_DESCRIPTOR parent, PSECURITY_DESCRIPTOR creator, PSECURITY_DESCRIPTOR *object,
                           const GUID **types, ULONG type_count, BOOLEAN container, ULONG flags, HANDLE token,
                           const GENERIC_MAPPING *mapping)
{
    DEFAULTS defaults = {};
    defaults.requested = token;
    MIME_SEC_DEFAULTS provider = {fetch_default, &defaults};
    NTSTATUS status = MimeSecNewObject(parent, creator, object, types, type_count, container, flags, mapping,
                                       &provider);
    defaults_free(&defaults);
    return status;
}

NTDLL_EXPORT NTSTATUS NTAPI RtlNewSecurityObjectWithMultipleInheritance(
    PSECURITY_DESCRIPTOR parent, PSECURITY_DESCRIPTOR creator, PSECURITY_DESCRIPTOR *object, const GUID **types,
    ULONG type_count, BOOLEAN container, ULONG flags, HANDLE token, const GENERIC_MAPPING *mapping)
{
    return new_object(parent, creator, object, types, type_count, container, flags, token, mapping);
}

NTDLL_EXPORT NTSTATUS NTAPI RtlNewSecurityObjectEx(PSECURITY_DESCRIPTOR parent, PSECURITY_DESCRIPTOR creator,
                                                   PSECURITY_DESCRIPTOR *object, const GUID *type, BOOLEAN container,
                                                   ULONG flags, HANDLE token, const GENERIC_MAPPING *mapping)
{
    return new_object(parent, creator, object, type ? &type : nullptr, type ? 1 : 0, container, flags, token,
                      mapping);
}

NTDLL_EXPORT NTSTATUS NTAPI RtlNewSecurityObject(PSECURITY_DESCRIPTOR parent, PSECURITY_DESCRIPTOR creator,
                                                 PSECURITY_DESCRIPTOR *object, BOOLEAN container, HANDLE token,
                                                 const GENERIC_MAPPING *mapping)
{
    return new_object(parent, creator, object, nullptr, 0, container, 0, token, mapping);
}

/* ---------------------------------------------------------------- objects made from tables and tokens */

#define CURRENT_PROCESS_TOKEN ((HANDLE)(LONG_PTR)-4)
#define CURRENT_THREAD_TOKEN ((HANDLE)(LONG_PTR)-5)
#define CURRENT_EFFECTIVE_TOKEN ((HANDLE)(LONG_PTR)-6)
#define TokenPrivilegesClass 3
#define TokenStatisticsClass 10
#define TokenIsAppContainerClass 29
#define TokenAppContainerSidClass 31

/* a token information class in a heap block of its own. */
static NTSTATUS token_block(HANDLE token, ULONG klass, PVOID *block)
{
    ULONG length = 256;
    for (;;) {
        PVOID data = RtlAllocateHeap(HEAP(), 0, length);
        if (!data)
            return STATUS_NO_MEMORY;
        ULONG needed = 0;
        NTSTATUS status = NtQueryInformationToken(token, klass, data, length, &needed);
        if (status == STATUS_BUFFER_TOO_SMALL && needed > length) {
            RtlFreeHeap(HEAP(), 0, data);
            length = needed;
            continue;
        }
        if (!NT_SUCCESS(status)) {
            RtlFreeHeap(HEAP(), 0, data);
            return status;
        }
        *block = data;
        return STATUS_SUCCESS;
    }
}

/* whether the token holds a privilege and has it enabled. */
static BOOLEAN privilege_enabled(HANDLE token, ULONG low)
{
    typedef struct {
        ULONG Count;
        struct {
            ULONG Low;
            LONG High;
            ULONG Attributes;
        } Privilege[1];
    } PRIVILEGES;
    PRIVILEGES *held;
    if (!NT_SUCCESS(token_block(token, TokenPrivilegesClass, (PVOID *)&held)))
        return FALSE;
    BOOLEAN enabled = FALSE;
    for (ULONG i = 0; i < held->Count; i++)
        if (held->Privilege[i].Low == low && held->Privilege[i].High == 0 && (held->Privilege[i].Attributes & 2))
            enabled = TRUE;
    RtlFreeHeap(HEAP(), 0, held);
    return enabled;
}

/* the list of a new named pipe: the system, administrators and the owner may do all, everyone may read. */
NTDLL_EXPORT NTSTATUS NTAPI RtlDefaultNpAcl(PACL *acl)
{
    *acl = nullptr;
    PSID *owner = nullptr, *container = nullptr;
    PSID parent = nullptr;
    NTSTATUS status = token_block(CURRENT_EFFECTIVE_TOKEN, TokenOwnerClass, (PVOID *)&owner);
    if (!NT_SUCCESS(status))
        return status;

    /* a process in an app container also gives it, and its parent, full access. */
    ULONG is_container = 0, returned;
    if (NT_SUCCESS(NtQueryInformationToken(CURRENT_EFFECTIVE_TOKEN, TokenIsAppContainerClass, &is_container,
                                           sizeof(is_container), &returned)) && is_container) {
        status = token_block(CURRENT_EFFECTIVE_TOKEN, TokenAppContainerSidClass, (PVOID *)&container);
        ULONG type = 0;
        if (NT_SUCCESS(status))
            status = RtlGetAppContainerSidType(*container, &type);
        if (NT_SUCCESS(status) && type == 1)
            status = RtlGetAppContainerParent(*container, &parent);
    }
    if (NT_SUCCESS(status)) {
        UCHAR system[12] = {1, 1, 0, 0, 0, 0, 0, 5, 18};
        UCHAR admins[16] = {1, 2, 0, 0, 0, 0, 0, 5, 32, 0, 0, 0, 0x20, 0x02};
        UCHAR everyone[12] = {1, 1, 0, 0, 0, 0, 0, 1, 0};
        UCHAR anonymous[12] = {1, 1, 0, 0, 0, 0, 0, 5, 7};
        ULONG size = 8 + 20 + 24 + 8 + 4 * (ULONG)((SID *)*owner)->SubAuthorityCount + 8 + 20 + 20;
        if (container)
            size += 8 + 8 + 4 * (ULONG)((SID *)*container)->SubAuthorityCount;
        if (parent)
            size += 8 + 8 + 4 * (ULONG)((SID *)parent)->SubAuthorityCount;
        PACL list = RtlAllocateHeap(HEAP(), 0, size);
        if (!list) {
            status = STATUS_NO_MEMORY;
        } else {
            status = RtlCreateAcl(list, size, ACL_REVISION);
            if (NT_SUCCESS(status))
                status = RtlAddAccessAllowedAceEx(list, ACL_REVISION, 0, 0x10000000, system);
            if (NT_SUCCESS(status))
                status = RtlAddAccessAllowedAceEx(list, ACL_REVISION, 0, 0x10000000, admins);
            if (NT_SUCCESS(status) && container)
                status = RtlAddAccessAllowedAceEx(list, ACL_REVISION, 0, 0x10000000, *container);
            if (NT_SUCCESS(status) && parent)
                status = RtlAddAccessAllowedAceEx(list, ACL_REVISION, 0, 0x10000000, parent);
            if (NT_SUCCESS(status))
                status = RtlAddAccessAllowedAceEx(list, ACL_REVISION, 0, 0x10000000, *owner);
            if (NT_SUCCESS(status))
                status = RtlAddAccessAllowedAceEx(list, ACL_REVISION, 0, GENERIC_READ, everyone);
            if (NT_SUCCESS(status))
                status = RtlAddAccessAllowedAceEx(list, ACL_REVISION, 0, GENERIC_READ, anonymous);
            if (NT_SUCCESS(status))
                *acl = list;
            else
                RtlFreeHeap(HEAP(), 0, list);
        }
    }
    if (parent)
        RtlFreeHeap(HEAP(), 0, parent);
    if (container)
        RtlFreeHeap(HEAP(), 0, container);
    RtlFreeHeap(HEAP(), 0, owner);
    return status;
}

/* a descriptor from an ace table, completed from the process token. */
NTDLL_EXPORT NTSTATUS NTAPI RtlCreateUserSecurityObject(PRTL_ACE_DATA table, ULONG count, PSID owner, PSID group,
                                                        BOOLEAN container, const GENERIC_MAPPING *mapping,
                                                        PSECURITY_DESCRIPTOR *object)
{
    PSECURITY_DESCRIPTOR creator = nullptr;
    NTSTATUS status = RtlCreateAndSetSD(table, count, owner, group, &creator);
    if (!NT_SUCCESS(status))
        return status;
    status = new_object(nullptr, creator, object, nullptr, 0, container, 0, CURRENT_PROCESS_TOKEN, mapping);
    RtlFreeHeap(HEAP(), 0, creator);
    return status;
}

/*
 * a new descriptor for another instance of an object, which is only worth making when the parent or the
 * creator changed or the token was modified since the last one. else there is nothing new, and none is made.
 */
NTDLL_EXPORT NTSTATUS NTAPI RtlNewInstanceSecurityObject(BOOLEAN parent_changed, BOOLEAN creator_changed,
                                                         PLUID old_modified, PLUID new_modified,
                                                         PSECURITY_DESCRIPTOR parent, PSECURITY_DESCRIPTOR creator,
                                                         PSECURITY_DESCRIPTOR *object, BOOLEAN container,
                                                         HANDLE token, const GENERIC_MAPPING *mapping)
{
    UCHAR statistics[56] = {};
    ULONG returned = 0;
    NTSTATUS status = NtQueryInformationToken(token, TokenStatisticsClass, statistics, sizeof(statistics), &returned);
    if (!NT_SUCCESS(status))
        return status;
    memcpy(new_modified, statistics + 48, 8);
    if (memcmp(new_modified, old_modified, 8) || parent_changed || creator_changed)
        return new_object(parent, creator, object, nullptr, 0, container, 0, token, mapping);
    *object = nullptr;
    return STATUS_SUCCESS;
}

/*
 * what a requested access comes to for a new object, and the privilege that stands behind it. the system
 * access right needs the security privilege, which is then recorded as used.
 */
NTDLL_EXPORT NTSTATUS NTAPI RtlNewSecurityGrantedAccess(ACCESS_MASK desired, PRIVILEGE_SET_ONE *privileges, PULONG length,
                                                        HANDLE token, const GENERIC_MAPPING *mapping,
                                                        PACCESS_MASK granted)
{
    HANDLE source = token ? token : CURRENT_THREAD_TOKEN;
    ACCESS_MASK mask = desired;
    if (mask & GENERIC_READ)
        mask |= mapping->GenericRead;
    if (mask & GENERIC_WRITE)
        mask |= mapping->GenericWrite;
    if (mask & GENERIC_EXECUTE)
        mask |= mapping->GenericExecute;
    if (mask & GENERIC_ALL)
        mask |= mapping->GenericAll;
    mask &= 0x0FFFFFFF;
    *granted = mask;
    BOOLEAN used = FALSE;
    if (mask & 0x01000000) {
        used = privilege_enabled(source, 8);
        if (!used)
            return STATUS_PRIVILEGE_NOT_HELD;
        *granted &= ~0x01000000u;
    }
    if (*length < 20) {
        *length = 20;
        return STATUS_BUFFER_TOO_SMALL;
    }
    privileges->PrivilegeCount = used ? 1 : 0;
    privileges->Control = 0;
    privileges->Privilege[0].Luid.LowPart = used ? 8 : 0;
    privileges->Privilege[0].Luid.HighPart = 0;
    privileges->Privilege[0].Attributes = used ? 0x80000000 : 0;
    return STATUS_SUCCESS;
}

/* an object is untrusted when its mandatory label is below medium, or there is none to speak of. */
NTDLL_EXPORT NTSTATUS NTAPI RtlIsUntrustedObject(HANDLE handle, PVOID object, PBOOLEAN untrusted)
{
    *untrusted = TRUE;
    if (object || !handle)
        return STATUS_INVALID_PARAMETER;
    UCHAR stack[128];
    UCHAR *sd = stack;
    ULONG needed = 0;
    NTSTATUS status = NtQuerySecurityObject(handle, LABEL_SECURITY_INFORMATION, sd, sizeof(stack) - 4, &needed);
    BOOLEAN allocated = FALSE;
    if (status == STATUS_BUFFER_TOO_SMALL) {
        sd = RtlAllocateHeap(HEAP(), 0, needed);
        if (!sd)
            return STATUS_NO_MEMORY;
        allocated = TRUE;
        status = NtQuerySecurityObject(handle, LABEL_SECURITY_INFORMATION, sd, needed, &needed);
    }
    if (NT_SUCCESS(status)) {
        BOOLEAN present;
        PACL sacl = nullptr;
        BOOLEAN defaulted;
        RtlGetSaclSecurityDescriptor(sd, &present, &sacl, &defaulted);
        if (!present)
            sacl = nullptr;
        *untrusted = FALSE;
        if (sacl) {
            const UCHAR *ace = (const UCHAR *)(sacl + 1);
            for (ULONG i = 0; i < sacl->AceCount; i++, ace += *(const USHORT *)(ace + 2)) {
                if (ace[0] != SYSTEM_MANDATORY_LABEL_ACE_TYPE || (ace[1] & INHERIT_ONLY_ACE))
                    continue;
                const SID *label = (const SID *)(ace + 8);
                *untrusted = label->SubAuthorityCount == 0 ||
                             label->SubAuthority[label->SubAuthorityCount - 1] < 0x2000;
                break;
            }
        }
    }
    if (allocated)
        RtlFreeHeap(HEAP(), 0, sd);
    return status;
}

/* ---------------------------------------------------------------- sids made from names */

/* the five sub authorities a name is boiled down to by sha-1 of its upper case form, or the eight of sha-256. */
static NTSTATUS hash_name(PCUNICODE_STRING name, const MimeHashAlg *algorithm, ULONG *words)
{
    UNICODE_STRING upper;
    NTSTATUS status = RtlUpcaseUnicodeString(&upper, name, TRUE);
    if (!NT_SUCCESS(status))
        return status;
    UCHAR digest[MIME_HASH_MAX_DIGEST];
    mime_hash(algorithm, upper.Buffer, upper.Length, digest);
    memcpy(words, digest, algorithm->digest_size);
    RtlFreeUnicodeString(&upper);
    return STATUS_SUCCESS;
}

static NTSTATUS service_sid(PCUNICODE_STRING name, ULONG base, PSID sid, PULONG length)
{
    if (!length)
        return STATUS_INVALID_PARAMETER;
    ULONG capacity = *length;
    *length = 32;
    if (capacity < 32)
        return STATUS_BUFFER_TOO_SMALL;
    ULONG words[5];
    NTSTATUS status = hash_name(name, &mime_sha1, words);
    if (!NT_SUCCESS(status))
        return status;
    SID *s = sid;
    s->Revision = SID_REVISION;
    s->SubAuthorityCount = 6;
    memset(&s->IdentifierAuthority, 0, 6);
    s->IdentifierAuthority.Value[5] = 5;
    s->SubAuthority[0] = base;
    memcpy(&s->SubAuthority[1], words, sizeof(words));
    return STATUS_SUCCESS;
}

/* S-1-5-80 and five hashed numbers: the sid of a service. */
NTDLL_EXPORT NTSTATUS NTAPI RtlCreateServiceSid(PCUNICODE_STRING name, PSID sid, PULONG length)
{
    if (!name || !sid || !length)
        return STATUS_INVALID_PARAMETER;
    return service_sid(name, 80, sid, length);
}

/* the same under another base, 80 to 111: S-1-5-83 is a virtual machine, 96 a font driver host... */
NTDLL_EXPORT NTSTATUS NTAPI RtlCreateVirtualAccountSid(PCUNICODE_STRING name, ULONG base, PSID sid, PULONG length)
{
    if (!name || !sid || !length || base - 80 > 31)
        return STATUS_INVALID_PARAMETER;
    return service_sid(name, base, sid, length);
}

/* the names of the capabilities that got a number of their own, and the sids that number makes. */
static const WCHAR *const g_legacy_capabilities[12] = {
    L"INTERNETCLIENT", L"INTERNETCLIENTSERVER", L"PRIVATENETWORKCLIENTSERVER", L"PICTURESLIBRARY",
    L"VIDEOSLIBRARY", L"MUSICLIBRARY", L"DOCUMENTSLIBRARY", L"ENTERPRISEAUTHENTICATION", L"SHAREDUSERCERTIFICATES",
    L"REMOVABLESTORAGE", L"APPOINTMENTS", L"CONTACTS"};

/*
 * the group sid S-1-5-32 and eight hashed numbers, and the capability sid S-1-15-3: with a number for the
 * twelve oldest capabilities, else 1024 and the same eight, or 65536 for the names of a win32 app.
 */
NTDLL_EXPORT NTSTATUS NTAPI RtlDeriveCapabilitySidsFromName(PCUNICODE_STRING name, PSID group, PSID capability)
{
    if (!name || !group || !capability)
        return STATUS_INVALID_PARAMETER;
    ULONG words[8];
    NTSTATUS status = hash_name(name, &mime_sha256, words);
    if (!NT_SUCCESS(status))
        return status;
    memset(capability, 0, 48);
    memset(group, 0, 44);
    SID *g = group;
    g->Revision = SID_REVISION;
    g->SubAuthorityCount = 9;
    g->IdentifierAuthority.Value[5] = 5;
    g->SubAuthority[0] = 32;
    memcpy(&g->SubAuthority[1], words, sizeof(words));

    SID *c = capability;
    c->Revision = SID_REVISION;
    c->IdentifierAuthority.Value[5] = 15;
    c->SubAuthority[0] = 3;
    for (ULONG i = 0; i < RTL_NUMBER_OF(g_legacy_capabilities); i++) {
        UNICODE_STRING legacy;
        RtlInitUnicodeString(&legacy, g_legacy_capabilities[i]);
        UNICODE_STRING upper;
        if (!NT_SUCCESS(RtlUpcaseUnicodeString(&upper, name, TRUE)))
            break;
        BOOLEAN match = RtlEqualUnicodeString(&upper, &legacy, FALSE);
        RtlFreeUnicodeString(&upper);
        if (match) {
            c->SubAuthorityCount = 2;
            c->SubAuthority[1] = i + 1;
            return STATUS_SUCCESS;
        }
    }
    UNICODE_STRING prefix;
    RtlInitUnicodeString(&prefix, L"isolatedWin32-");
    c->SubAuthorityCount = 10;
    c->SubAuthority[1] = RtlPrefixUnicodeString(&prefix, name, TRUE) ? 0x10000 : 1024;
    memcpy(&c->SubAuthority[2], words, sizeof(words));
    return STATUS_SUCCESS;
}
