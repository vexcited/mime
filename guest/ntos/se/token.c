/*
 * access tokens. every process of the session runs with the token of the
 * logged on user, as a member of the administrators group filtered by uac
 * the way windows starts programs; the system process has the system token.
 * tokens only describe identity for now: access checks are not enforced.
 */
#include "../ntos.h"

typedef struct SID_SPEC {
    UCHAR Authority;
    UCHAR Count;
    ULONG Sub[8];
} SID_SPEC;

typedef struct GROUP_SPEC {
    SID_SPEC Sid;
    ULONG Attributes;
} GROUP_SPEC;

typedef struct TOKEN {
    BOOLEAN System;
    ULONG64 Id;
} TOKEN;

static POBJECT_TYPE g_token_type;

#define SE_GROUP_MANDATORY 0x1
#define SE_GROUP_ENABLED_BY_DEFAULT 0x2
#define SE_GROUP_ENABLED 0x4
#define SE_GROUP_OWNER 0x8
#define SE_GROUP_USE_FOR_DENY_ONLY 0x10
#define SE_GROUP_INTEGRITY 0x20
#define SE_GROUP_INTEGRITY_ENABLED 0x40
#define SE_GROUP_LOGON_ID 0xC0000000
#define GROUP_NORMAL (SE_GROUP_MANDATORY | SE_GROUP_ENABLED_BY_DEFAULT | SE_GROUP_ENABLED)

#define TokenUser 1
#define TokenGroups 2
#define TokenPrivileges 3
#define TokenOwner 4
#define TokenPrimaryGroup 5
#define TokenSource 7
#define TokenType 8
#define TokenStatistics 10
#define TokenSessionId 12
#define TokenElevationType 18
#define TokenElevation 20
#define TokenIntegrityLevel 25
#define TokenIsAppContainer 29

static ULONG sid_size(const SID_SPEC *s)
{
    return 8 + 4 * s->Count;
}

static void write_sid(PUCHAR out, const SID_SPEC *s)
{
    out[0] = 1;
    out[1] = s->Count;
    memset(out + 2, 0, 5);
    out[7] = s->Authority;
    memcpy(out + 8, s->Sub, 4 * s->Count);
}

static SID_SPEC user_sid(const TOKEN *t)
{
    if (t->System)
        return (SID_SPEC){5, 1, {18}};
    return (SID_SPEC){5, 5, {21, HalBootInfo->user_sid[0], HalBootInfo->user_sid[1], HalBootInfo->user_sid[2], 1001}};
}

static ULONG groups(const TOKEN *t, GROUP_SPEC *out)
{
    static const GROUP_SPEC user[] = {
        {{1, 1, {0}}, GROUP_NORMAL},                          /* Everyone */
        {{5, 1, {114}}, SE_GROUP_USE_FOR_DENY_ONLY},          /* Local account and member of Administrators */
        {{5, 2, {32, 544}}, SE_GROUP_USE_FOR_DENY_ONLY},      /* BUILTIN\Administrators */
        {{5, 2, {32, 545}}, GROUP_NORMAL},                    /* BUILTIN\Users */
        {{5, 1, {4}}, GROUP_NORMAL},                          /* INTERACTIVE */
        {{2, 1, {1}}, GROUP_NORMAL},                          /* CONSOLE LOGON */
        {{5, 1, {11}}, GROUP_NORMAL},                         /* Authenticated Users */
        {{5, 1, {15}}, GROUP_NORMAL},                         /* This Organization */
        {{5, 1, {113}}, GROUP_NORMAL},                        /* Local account */
        {{5, 3, {5, 0, 0x3E8D2}}, GROUP_NORMAL | SE_GROUP_LOGON_ID}, /* the logon session */
        {{2, 1, {0}}, GROUP_NORMAL},                          /* LOCAL */
        {{5, 2, {64, 10}}, GROUP_NORMAL},                     /* NTLM Authentication */
        {{16, 1, {0x2000}}, SE_GROUP_INTEGRITY | SE_GROUP_INTEGRITY_ENABLED}, /* Medium Mandatory Level */
    };
    static const GROUP_SPEC system[] = {
        {{5, 2, {32, 544}}, GROUP_NORMAL | SE_GROUP_OWNER},
        {{1, 1, {0}}, GROUP_NORMAL},
        {{5, 1, {11}}, GROUP_NORMAL},
        {{16, 1, {0x4000}}, SE_GROUP_INTEGRITY | SE_GROUP_INTEGRITY_ENABLED},
    };
    const GROUP_SPEC *g = t->System ? system : user;
    ULONG n = t->System ? RTL_NUMBER_OF(system) : RTL_NUMBER_OF(user);
    memcpy(out, g, n * sizeof(*g));
    return n;
}

/* the privileges of a filtered administrator: only the harmless ones remain. */
static const struct {
    ULONG Luid;
    ULONG Attributes;
} g_user_privileges[] = {
    {19, 0},        /* SeShutdownPrivilege */
    {23, 3},        /* SeChangeNotifyPrivilege, enabled by default */
    {25, 0},        /* SeUndockPrivilege */
    {33, 0},        /* SeIncreaseWorkingSetPrivilege */
    {34, 0},        /* SeTimeZonePrivilege */
};

static TOKEN g_user = {FALSE, 0x3E8F1};
static TOKEN g_system = {TRUE, 0x3E7};

static NTSTATUS open_token(const TOKEN *spec, ACCESS_MASK access, ULONG attributes, PHANDLE handle)
{
    if (!g_token_type) {
        OBJECT_TYPE_INITIALIZER init = {};
        init.ValidAccessMask = 0xF01FF;
        g_token_type = ObCreateObjectType(L"Token", &init);
    }
    TOKEN *t;
    NTSTATUS status = ObCreateObject(g_token_type, nullptr, KernelMode, sizeof(*t), (PVOID *)&t);
    if (!NT_SUCCESS(status))
        return status;
    *t = *spec;
    HANDLE h;
    if (KeGetPreviousMode() == UserMode)
        attributes &= ~OBJ_KERNEL_HANDLE;
    status = ObInsertObject(t, access, attributes, &h);
    return NT_SUCCESS(status) ? ObStoreHandle(handle, h) : status;
}

NTSTATUS NTAPI NtOpenProcessTokenEx(HANDLE process_handle, ACCESS_MASK access, ULONG attributes, PHANDLE token)
{
    PEPROCESS process;
    NTSTATUS status = ObReferenceHandle(process_handle, PsProcessTypeObject, KeGetPreviousMode(), (PVOID *)&process, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    const TOKEN *spec = process == PsInitialSystemProcess ? &g_system : &g_user;
    ObDereferenceObject(process);
    return open_token(spec, access, attributes, token);
}

NTSTATUS NTAPI NtOpenProcessToken(HANDLE process, ACCESS_MASK access, PHANDLE token)
{
    return NtOpenProcessTokenEx(process, access, 0, token);
}

/* the output buffer is built in pool, with pointers set to where it lands in the caller's buffer. */
typedef struct BUILDER {
    PUCHAR Data;
    ULONG Size;
    ULONG_PTR Base;
} BUILDER;

static ULONG put_sid(BUILDER *b, ULONG at, const SID_SPEC *s, ULONG pointer_at)
{
    write_sid(b->Data + at, s);
    ULONG_PTR p = b->Base + at;
    memcpy(b->Data + pointer_at, &p, sizeof(p));
    return at + sid_size(s);
}

static NTSTATUS build(const TOKEN *t, ULONG klass, BUILDER *b)
{
    GROUP_SPEC g[16];
    SID_SPEC user = user_sid(t);
    ULONG n, at;
    switch (klass) {
    case TokenUser:
    case TokenOwner:
    case TokenPrimaryGroup: {
        SID_SPEC s = user;
        if (klass == TokenPrimaryGroup && !t->System)
            s.Sub[4] = 513; /* the None group of the machine */
        ULONG header = klass == TokenUser ? 16 : 8;
        b->Size = header + sid_size(&s);
        put_sid(b, header, &s, 0);
        return STATUS_SUCCESS;
    }
    case TokenGroups:
        n = groups(t, g);
        b->Size = 8 + 16 * n;
        for (ULONG i = 0; i < n; i++)
            b->Size += sid_size(&g[i].Sid);
        *(PULONG)b->Data = n;
        at = 8 + 16 * n;
        for (ULONG i = 0; i < n; i++) {
            at = put_sid(b, at, &g[i].Sid, 8 + 16 * i);
            memcpy(b->Data + 8 + 16 * i + 8, &g[i].Attributes, 4);
        }
        return STATUS_SUCCESS;
    case TokenIntegrityLevel: {
        SID_SPEC s = {16, 1, {t->System ? 0x4000u : 0x2000u}};
        ULONG attributes = SE_GROUP_INTEGRITY | SE_GROUP_INTEGRITY_ENABLED;
        b->Size = 16 + sid_size(&s);
        put_sid(b, 16, &s, 0);
        memcpy(b->Data + 8, &attributes, 4);
        return STATUS_SUCCESS;
    }
    case TokenPrivileges:
        n = t->System ? 0 : RTL_NUMBER_OF(g_user_privileges);
        b->Size = 4 + 12 * n;
        *(PULONG)b->Data = n;
        for (ULONG i = 0; i < n; i++) {
            ULONG entry[3] = {g_user_privileges[i].Luid, 0, g_user_privileges[i].Attributes};
            memcpy(b->Data + 4 + 12 * i, entry, 12);
        }
        return STATUS_SUCCESS;
    case TokenSource:
        b->Size = 16;
        memcpy(b->Data, "User32  ", 8);
        return STATUS_SUCCESS;
    case TokenType:
    case TokenSessionId:
    case TokenElevationType:
    case TokenElevation:
    case TokenIsAppContainer: {
        ULONG v = klass == TokenType ? 1 /* TokenPrimary */
                  : klass == TokenSessionId ? (t->System ? 0 : 1)
                  : klass == TokenElevationType ? (t->System ? 1 : 3) /* default, or limited */
                  : klass == TokenElevation ? t->System
                  : 0;
        b->Size = 4;
        memcpy(b->Data, &v, 4);
        return STATUS_SUCCESS;
    }
    case TokenStatistics: {
        b->Size = 56;
        n = groups(t, g);
        ULONG fields[] = {1 /* TokenPrimary */, 0, 4096, 4096 - 512, n, t->System ? 0 : RTL_NUMBER_OF(g_user_privileges)};
        memcpy(b->Data, &t->Id, 8);
        ULONG64 auth = t->System ? 0x3E7 : 0x3E8D2;
        memcpy(b->Data + 8, &auth, 8);
        ULONG64 never = 0x7FFFFFFFFFFFFFFFull;
        memcpy(b->Data + 16, &never, 8);
        memcpy(b->Data + 24, fields, sizeof(fields));
        memcpy(b->Data + 48, &t->Id, 8);
        return STATUS_SUCCESS;
    }
    default:
        return STATUS_INVALID_INFO_CLASS;
    }
}

NTSTATUS NTAPI NtQueryInformationToken(HANDLE handle, ULONG klass, PVOID info, ULONG length, PULONG returned)
{
    TOKEN *t;
    if (!g_token_type)
        return STATUS_INVALID_HANDLE;
    NTSTATUS status = ObReferenceHandle(handle, g_token_type, KeGetPreviousMode(), (PVOID *)&t, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    BUILDER b = {ExAllocatePool2(POOL_FLAG_NON_PAGED, 1024, KI_TAG('S', 'e', 'T', 'k')), 0, (ULONG_PTR)info};
    status = b.Data ? build(t, klass, &b) : STATUS_INSUFFICIENT_RESOURCES;
    ObDereferenceObject(t);
    if (NT_SUCCESS(status)) {
        if (returned)
            status = KiCopyToUser(returned, &b.Size, sizeof(ULONG));
        if (NT_SUCCESS(status))
            status = length < b.Size ? STATUS_BUFFER_TOO_SMALL : KiCopyToUser(info, b.Data, b.Size);
    }
    if (b.Data)
        ExFreePool(b.Data);
    return status;
}

NTSTATUS NTAPI NtAdjustPrivilegesToken(HANDLE handle, BOOLEAN disable_all, PVOID state, ULONG length, PVOID previous,
                                       PULONG returned)
{
    TOKEN *t;
    NTSTATUS status = g_token_type ? ObReferenceHandle(handle, g_token_type, KeGetPreviousMode(), (PVOID *)&t, nullptr)
                                   : STATUS_INVALID_HANDLE;
    if (!NT_SUCCESS(status))
        return status;
    BOOLEAN system = t->System;
    ObDereferenceObject(t);
    if (returned)
        KiCopyToUser(returned, &(ULONG){0}, sizeof(ULONG));
    /* a filtered token cannot enable administrator privileges, as on windows. */
    return system || disable_all ? STATUS_SUCCESS : (NTSTATUS)0x00000106L; /* STATUS_NOT_ALL_ASSIGNED */
}
