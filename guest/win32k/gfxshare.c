/*
 * the kernel objects behind the nt handles of shared direct3d resources and fences, see mime/gfxshare.h.
 *
 * An object holds the bytes its creator described the resource with, so the process that opens it can make its own
 * object, and the id the host keeps the memory or semaphore for other processes by. When the last reference to it
 * goes the host is told to drop that.
 *
 * A keyed mutex is a lock with a key. It starts free with key 0; acquiring it waits until it is free and has the
 * key asked for, releasing it makes it free with the key given. A process that ends holding it abandons it: the
 * next acquire succeeds with a status saying so.
 */
#include "win32k.h"

#include <mime/gfxshare.h>

typedef struct GFXSHARE {
    LIST_ENTRY Links;
    ULONG64 Id;
    ULONG Kind;
    ULONG Size;
    ULONG Flags;
    ULONG Legacy;
    UCHAR Data[MIME_GFXSHARE_MAX_DATA];
    /* the keyed mutex. */
    ULONG64 Key;
    BOOLEAN Held;
    BOOLEAN Abandoned;
    PEPROCESS Holder;
    KEVENT Changed;
} GFXSHARE;

static POBJECT_TYPE g_resource_type, g_sync_type;
static LIST_ENTRY g_all = {&g_all, &g_all};
static KSPIN_LOCK g_lock;
static LONG64 g_next_id = 1;
static ULONG g_next_legacy = 1;
/* the objects with a legacy shared handle, by it. */
#define MAX_LEGACY 4096
static GFXSHARE *g_legacy[MAX_LEGACY];

static void share_delete(PVOID object)
{
    GFXSHARE *s = object;
    KIRQL irql;
    KeAcquireSpinLock(&g_lock, &irql);
    RemoveEntryList(&s->Links);
    if (s->Legacy && g_legacy[(s->Legacy - 1) % MAX_LEGACY] == s)
        g_legacy[(s->Legacy - 1) % MAX_LEGACY] = nullptr;
    KeReleaseSpinLock(&g_lock, irql);
    HalMimeHypercallUnlocked(MIME_HC_GFX_SHARE_FREE, s->Id, 0, 0, 0, 0, 0, nullptr);
}

static void types(void)
{
    if (g_resource_type)
        return;
    OBJECT_TYPE_INITIALIZER init = {};
    init.ValidAccessMask = STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | 3;
    init.DeleteProcedure = share_delete;
    ObSetGenericMapping(&init, 0x20001, 0x20002, 0x20000, 0xF0003);
    g_resource_type = ObCreateObjectType(L"DxgkSharedResource", &init);
    g_sync_type = ObCreateObjectType(L"DxgkSharedSyncObject", &init);
}

static POBJECT_TYPE type_of(ULONG kind)
{
    types();
    return kind == MIME_GFXSHARE_RESOURCE ? g_resource_type : kind == MIME_GFXSHARE_SYNC ? g_sync_type : nullptr;
}

/* the object of a handle, of the kind asked for or of either. */
static NTSTATUS reference(HANDLE handle, ULONG kind, GFXSHARE **out)
{
    types();
    PVOID object;
    NTSTATUS status = ObReferenceHandle(handle, kind ? type_of(kind) : nullptr, UserMode, &object, nullptr);
    if (!NT_SUCCESS(status))
        return status;
    POBJECT_TYPE type = ObGetObjectType(object);
    if (type != g_resource_type && type != g_sync_type) {
        ObDereferenceObject(object);
        return STATUS_OBJECT_TYPE_MISMATCH;
    }
    *out = object;
    return STATUS_SUCCESS;
}

/* a name given by the caller as the whole path in the object namespace, copied with its attributes. */
static NTSTATUS capture_name(ULONG64 user, ULONG attributes, OBJECT_ATTRIBUTES *oa, UNICODE_STRING *name,
                             PWSTR *buffer)
{
    *buffer = nullptr;
    InitializeObjectAttributes(oa, nullptr, attributes | OBJ_CASE_INSENSITIVE, nullptr, nullptr);
    if (!user)
        return STATUS_SUCCESS;
    WCHAR *copy = ExAllocatePool2(POOL_FLAG_NON_PAGED, 512 * sizeof(WCHAR), W32_TAG('G', 'x', 'N', 'm'));
    if (!copy)
        return STATUS_INSUFFICIENT_RESOURCES;
    ULONG chars = 0;
    for (; chars < 511; chars++) {
        NTSTATUS status = MimeCopyFromUser(&copy[chars], (const WCHAR *)(ULONG_PTR)user + chars, sizeof(WCHAR));
        if (!NT_SUCCESS(status)) {
            ExFreePool(copy);
            return status;
        }
        if (!copy[chars])
            break;
    }
    if (!chars || chars == 511) {
        ExFreePool(copy);
        return STATUS_OBJECT_NAME_INVALID;
    }
    name->Buffer = copy;
    name->Length = (USHORT)(chars * sizeof(WCHAR));
    name->MaximumLength = (USHORT)(512 * sizeof(WCHAR));
    oa->ObjectName = name;
    *buffer = copy;
    return STATUS_SUCCESS;
}

static NTSTATUS create(MimeGfxShare *a)
{
    POBJECT_TYPE type = type_of(a->Kind);
    if (!type || a->Size > MIME_GFXSHARE_MAX_DATA)
        return STATUS_INVALID_PARAMETER;
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name;
    PWSTR buffer;
    NTSTATUS status = capture_name(a->Name, (a->Flags & MIME_GFXSHARE_INHERIT) ? OBJ_INHERIT : 0, &oa, &name, &buffer);
    if (!NT_SUCCESS(status))
        return status;
    GFXSHARE *s;
    status = ObCreateObject(type, &oa, UserMode, sizeof(*s), (PVOID *)&s);
    if (NT_SUCCESS(status)) {
        memset(s, 0, sizeof(*s));
        InitializeListHead(&s->Links);
        KeInitializeEvent(&s->Changed, NotificationEvent, FALSE);
        s->Kind = a->Kind;
        s->Size = a->Size;
        s->Flags = a->Flags & MIME_GFXSHARE_KEYED_MUTEX;
        s->Id = (ULONG64)__atomic_fetch_add(&g_next_id, 1, __ATOMIC_RELAXED);
        if (a->Size)
            status = MimeCopyFromUser(s->Data, (const void *)(ULONG_PTR)a->Data, a->Size);
        if (!NT_SUCCESS(status)) {
            s->Id = 0;
            ObDereferenceObject(s);
        } else {
            ULONG legacy = 0;
            KIRQL irql;
            KeAcquireSpinLock(&g_lock, &irql);
            InsertTailList(&g_all, &s->Links);
            if (a->Flags & MIME_GFXSHARE_LEGACY) {
                /* the first free place from the next number on, the numbers turning round. */
                for (ULONG i = 0; i < MAX_LEGACY && !legacy; i++) {
                    ULONG n = g_next_legacy++;
                    if (!g_legacy[(n - 1) % MAX_LEGACY]) {
                        g_legacy[(n - 1) % MAX_LEGACY] = s;
                        legacy = n;
                    }
                }
                s->Legacy = legacy;
            }
            KeReleaseSpinLock(&g_lock, irql);
            ULONG64 id = s->Id;
            HANDLE h;
            status = ObInsertObjectNamed(s, &oa, 0xF0003, UserMode, &h);
            if (NT_SUCCESS(status) || status == STATUS_OBJECT_NAME_EXISTS) {
                a->Id = id;
                a->Legacy = legacy;
                a->Handle = (ULONG64)(ULONG_PTR)h;
            }
        }
    }
    if (buffer)
        ExFreePool(buffer);
    return status;
}

static NTSTATUS open_name(MimeGfxShare *a)
{
    POBJECT_TYPE type = type_of(a->Kind);
    if (!type)
        return STATUS_INVALID_PARAMETER;
    OBJECT_ATTRIBUTES oa;
    UNICODE_STRING name;
    PWSTR buffer;
    NTSTATUS status = capture_name(a->Name, (a->Flags & MIME_GFXSHARE_INHERIT) ? OBJ_INHERIT : 0, &oa, &name, &buffer);
    if (!NT_SUCCESS(status))
        return status;
    if (!buffer)
        return STATUS_OBJECT_NAME_INVALID;
    HANDLE h;
    status = ObOpenObjectByName(&oa, type, UserMode, 0xF0003, nullptr, &h);
    if (NT_SUCCESS(status))
        a->Handle = (ULONG64)(ULONG_PTR)h;
    ExFreePool(buffer);
    return status;
}

static NTSTATUS describe(MimeGfxShare *a)
{
    GFXSHARE *s;
    NTSTATUS status = reference((HANDLE)(ULONG_PTR)a->Handle, a->Kind, &s);
    if (!NT_SUCCESS(status))
        return status;
    a->Kind = s->Kind;
    a->Id = s->Id;
    a->Flags = s->Flags;
    a->Legacy = s->Legacy;
    ULONG capacity = a->Size;
    a->Size = s->Size;
    if (s->Size > capacity) {
        status = STATUS_BUFFER_TOO_SMALL;
    } else if (s->Size) {
        status = MimeCopyToUser((void *)(ULONG_PTR)a->Data, s->Data, s->Size);
    }
    ObDereferenceObject(s);
    return status;
}

static NTSTATUS open_legacy(MimeGfxShare *a)
{
    ULONG n = a->Legacy;
    GFXSHARE *s = nullptr;
    KIRQL irql;
    KeAcquireSpinLock(&g_lock, &irql);
    if (n)
        s = g_legacy[(n - 1) % MAX_LEGACY];
    if (s && s->Legacy == n)
        ObReferenceObject(s);
    else
        s = nullptr;
    KeReleaseSpinLock(&g_lock, irql);
    if (!s)
        return STATUS_INVALID_HANDLE;
    HANDLE h;
    NTSTATUS status = ObOpenObjectByPointer(s, 0, nullptr, 0xF0003, ObGetObjectType(s), UserMode, &h);
    ObDereferenceObject(s);
    if (NT_SUCCESS(status))
        a->Handle = (ULONG64)(ULONG_PTR)h;
    return status;
}

static NTSTATUS retire(MimeGfxShare *a)
{
    ULONG n = a->Legacy;
    KIRQL irql;
    KeAcquireSpinLock(&g_lock, &irql);
    if (n && g_legacy[(n - 1) % MAX_LEGACY] && g_legacy[(n - 1) % MAX_LEGACY]->Legacy == n) {
        g_legacy[(n - 1) % MAX_LEGACY]->Legacy = 0;
        g_legacy[(n - 1) % MAX_LEGACY] = nullptr;
    }
    KeReleaseSpinLock(&g_lock, irql);
    return STATUS_SUCCESS;
}

static NTSTATUS acquire(MimeGfxShare *a)
{
    GFXSHARE *s;
    NTSTATUS status = reference((HANDLE)(ULONG_PTR)a->Handle, MIME_GFXSHARE_RESOURCE, &s);
    if (!NT_SUCCESS(status))
        return status;
    if (!(s->Flags & MIME_GFXSHARE_KEYED_MUTEX)) {
        ObDereferenceObject(s);
        return STATUS_INVALID_PARAMETER;
    }
    LARGE_INTEGER end = {};
    if (a->Timeout != 0xFFFFFFFF) {
        KeQuerySystemTime(&end);
        end.QuadPart += (LONGLONG)a->Timeout * 10000;
    }
    for (;;) {
        KIRQL irql;
        KeAcquireSpinLock(&g_lock, &irql);
        if (!s->Held && s->Key == a->Key) {
            s->Held = TRUE;
            s->Holder = PsGetCurrentProcess();
            status = s->Abandoned ? STATUS_ABANDONED : STATUS_SUCCESS;
            s->Abandoned = FALSE;
            KeReleaseSpinLock(&g_lock, irql);
            break;
        }
        KeResetEvent(&s->Changed);
        KeReleaseSpinLock(&g_lock, irql);
        LARGE_INTEGER wait = {}, *timeout = nullptr;
        if (a->Timeout != 0xFFFFFFFF) {
            LARGE_INTEGER now;
            KeQuerySystemTime(&now);
            if (now.QuadPart >= end.QuadPart) {
                status = STATUS_TIMEOUT;
                break;
            }
            wait.QuadPart = -(end.QuadPart - now.QuadPart);
            timeout = &wait;
        }
        NTSTATUS w = KeWaitForSingleObject(&s->Changed, Executive, UserMode, FALSE, timeout);
        if (w == STATUS_TIMEOUT) {
            /* once more, the lock may have been released in the last moment. */
            if (a->Timeout == 0xFFFFFFFF)
                continue;
            KeAcquireSpinLock(&g_lock, &irql);
            if (!s->Held && s->Key == a->Key) {
                s->Held = TRUE;
                s->Holder = PsGetCurrentProcess();
                status = s->Abandoned ? STATUS_ABANDONED : STATUS_SUCCESS;
                s->Abandoned = FALSE;
            } else {
                status = STATUS_TIMEOUT;
            }
            KeReleaseSpinLock(&g_lock, irql);
            break;
        }
    }
    ObDereferenceObject(s);
    return status;
}

static NTSTATUS release(MimeGfxShare *a)
{
    GFXSHARE *s;
    NTSTATUS status = reference((HANDLE)(ULONG_PTR)a->Handle, MIME_GFXSHARE_RESOURCE, &s);
    if (!NT_SUCCESS(status))
        return status;
    KIRQL irql;
    KeAcquireSpinLock(&g_lock, &irql);
    if (!(s->Flags & MIME_GFXSHARE_KEYED_MUTEX) || !s->Held) {
        status = (s->Flags & MIME_GFXSHARE_KEYED_MUTEX) ? STATUS_MUTANT_NOT_OWNED : STATUS_INVALID_PARAMETER;
    } else {
        s->Held = FALSE;
        s->Holder = nullptr;
        s->Key = a->Key;
        KeSetEvent(&s->Changed, 0, FALSE);
        status = STATUS_SUCCESS;
    }
    KeReleaseSpinLock(&g_lock, irql);
    ObDereferenceObject(s);
    return status;
}

NTSTATUS NTAPI NtGdiMimeGfxShare(ULONG op, PVOID user)
{
    MimeGfxShare a;
    NTSTATUS status = MimeCopyFromUser(&a, user, sizeof(a));
    if (!NT_SUCCESS(status))
        return STATUS_ACCESS_VIOLATION;
    switch (op) {
    case MIME_GFXSHARE_CREATE:
        status = create(&a);
        break;
    case MIME_GFXSHARE_OPEN:
        status = describe(&a);
        break;
    case MIME_GFXSHARE_OPEN_NAME:
        status = open_name(&a);
        break;
    case MIME_GFXSHARE_OPEN_LEGACY:
        status = open_legacy(&a);
        break;
    case MIME_GFXSHARE_ACQUIRE:
        status = acquire(&a);
        break;
    case MIME_GFXSHARE_RELEASE:
        status = release(&a);
        break;
    case MIME_GFXSHARE_RETIRE:
        status = retire(&a);
        break;
    default:
        return STATUS_INVALID_PARAMETER;
    }
    /* the answers go back whatever the status: a timeout and an abandoned mutex are answers. */
    if (!NT_SUCCESS(MimeCopyToUser(user, &a, sizeof(a))) && NT_SUCCESS(status))
        return STATUS_ACCESS_VIOLATION;
    return status;
}

/* a process that ends abandons the keyed mutexes it holds. */
void GfxShareFreeProcess(W32PROCESS *p)
{
    KIRQL irql;
    KeAcquireSpinLock(&g_lock, &irql);
    for (PLIST_ENTRY l = g_all.Flink; l != &g_all; l = l->Flink) {
        GFXSHARE *s = CONTAINING_RECORD(l, GFXSHARE, Links);
        if (s->Held && s->Holder == p->Process) {
            s->Held = FALSE;
            s->Holder = nullptr;
            s->Abandoned = TRUE;
            KeSetEvent(&s->Changed, 0, FALSE);
        }
    }
    KeReleaseSpinLock(&g_lock, irql);
}
