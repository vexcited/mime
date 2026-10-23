/*
 * registry change notification, NtNotifyChangeKey and NtNotifyChangeMultipleKeys.
 *
 * a key handle has at most one notify block, made by its first request. the block keeps that request's filter
 * and subtree flag for later requests on the handle, and a flag for a change that came while no request waited,
 * which the next request answers at once. a request is a post with an arm on its key's block, and a second arm
 * for the subordinate key of NtNotifyChangeMultipleKeys. the first change to reach an arm answers the post: a
 * synchronous caller wakes, the others get their status block, event and apc once the registry lock is released.
 */
#include "cm.h"

#define KEY_NOTIFY 0x0010
#define REG_NOTIFY_FILTERS 0x0000000F
#define REG_NOTIFY_THREAD_AGNOSTIC 0x10000000

typedef struct CM_POST CM_POST;

typedef struct CM_ARM {
    LIST_ENTRY Links; /* in the block's arms, while Notify is set */
    CM_NOTIFY *Notify;
    CM_POST *Post;
} CM_ARM;

struct CM_NOTIFY {
    LIST_ENTRY Links; /* all blocks */
    CM_KEY_BODY *Body;
    CM_NODE *Node;
    ULONG Filter;
    BOOLEAN WatchTree;
    BOOLEAN Pending; /* a change came while no request waited */
    LIST_ENTRY Arms;
};

struct CM_POST {
    LIST_ENTRY Links; /* waiting posts, then answered ones waiting for the lock to be released */
    CM_ARM Arm[2];
    PEPROCESS Process;
    PETHREAD Thread;
    PKEVENT Event;
    PIO_STATUS_BLOCK Iosb; /* the caller's, bit 0 set for the 32-bit layout, none for a thread agnostic post */
    PVOID ApcRoutine;
    PVOID ApcContext;
    CM_KEY_BODY *Subordinate; /* the second key, held by name while the post lives */
    NTSTATUS Status;
    BOOLEAN Sync;
    BOOLEAN Agnostic;
    BOOLEAN Done;
    KEVENT Wake;
};

static LIST_ENTRY CmpNotifies;
static LIST_ENTRY CmpPosts;
static LIST_ENTRY CmpAnswered;
static ULONG CmpNotifyCount;
volatile LONG CmpNotifyReady;

void CmpNotifyInitialize(void)
{
    InitializeListHead(&CmpNotifies);
    InitializeListHead(&CmpPosts);
    InitializeListHead(&CmpAnswered);
}

/* ------------------------------------------------------------------ answering */

static void unlink_arm(CM_ARM *arm)
{
    if (arm->Notify) {
        RemoveEntryList(&arm->Links);
        arm->Notify = nullptr;
    }
}

/* lock held. */
static void answer(CM_POST *post, NTSTATUS status)
{
    if (post->Done)
        return;
    post->Done = TRUE;
    post->Status = status;
    unlink_arm(&post->Arm[0]);
    unlink_arm(&post->Arm[1]);
    RemoveEntryList(&post->Links);
    if (post->Sync) {
        KeSetEvent(&post->Wake, IO_NO_INCREMENT, FALSE);
        return;
    }
    InsertTailList(&CmpAnswered, &post->Links);
    __atomic_add_fetch(&CmpNotifyReady, 1, __ATOMIC_SEQ_CST);
}

/* a change answers every post on the block; a key's end answers only posts on their first key. */
static void answer_all(CM_NOTIFY *n, NTSTATUS status, BOOLEAN change)
{
    while (!IsListEmpty(&n->Arms)) {
        CM_ARM *arm = CONTAINING_RECORD(n->Arms.Flink, CM_ARM, Links);
        CM_POST *post = arm->Post;
        if (change || arm == &post->Arm[0])
            answer(post, status);
        else
            unlink_arm(arm);
    }
}

static void destroy(CM_NOTIFY *n)
{
    while (!IsListEmpty(&n->Arms))
        unlink_arm(CONTAINING_RECORD(n->Arms.Flink, CM_ARM, Links));
    RemoveEntryList(&n->Links);
    CmpNotifyCount--;
    n->Body->Notify = nullptr;
    ExFreePool(n);
}

static void release_post(CM_POST *post)
{
    if (post->Subordinate)
        ObDereferenceObject(post->Subordinate);
    if (post->Thread)
        ObDereferenceObject(post->Thread);
    if (post->Process)
        ObDereferenceObject(post->Process);
    ExFreePool(post);
}

static void copy_to_process(PEPROCESS process, PVOID address, const void *source, ULONG length)
{
    IopCopyToProcess(process, address, source, length);
}

/* for a post answered while the lock was held: the status block, then the event, then the apc. */
static void finish(CM_POST *post)
{
    if (post->Iosb) {
        ULONG64 image[2];
        PVOID address;
        ULONG length = IopIosbImage(post->Iosb, post->Status, 0, &address, image);
        copy_to_process(post->Process, address, image, length);
    }
    if (post->Event) {
        KeSetEvent(post->Event, IO_NO_INCREMENT, FALSE);
        ObDereferenceObject(post->Event);
    }
    if (post->ApcRoutine)
        IopQueueCompletionApc(post->Thread, post->ApcRoutine, post->ApcContext, post->Iosb);
    release_post(post);
}

/* called by whoever released the lock, with it released. */
void CmpNotifyFinish(void)
{
    for (;;) {
        ExAcquireFastMutex(&CmpLock);
        if (IsListEmpty(&CmpAnswered)) {
            ExReleaseFastMutex(&CmpLock);
            return;
        }
        CM_POST *post = CONTAINING_RECORD(RemoveHeadList(&CmpAnswered), CM_POST, Links);
        __atomic_sub_fetch(&CmpNotifyReady, 1, __ATOMIC_SEQ_CST);
        ExReleaseFastMutex(&CmpLock);
        finish(post);
    }
}

/* ------------------------------------------------------------------ changes */

static BOOLEAN is_below(CM_NODE *ancestor, CM_NODE *node)
{
    for (CM_NODE *n = node; n; n = n->Parent)
        if (n == ancestor)
            return TRUE;
    return FALSE;
}

/* lock held. */
void CmpNotifyChange(CM_NODE *node, ULONG filter)
{
    if (!CmpNotifyCount)
        return;
    for (PLIST_ENTRY e = CmpNotifies.Flink; e != &CmpNotifies; e = e->Flink) {
        CM_NOTIFY *n = CONTAINING_RECORD(e, CM_NOTIFY, Links);
        if (!(filter & n->Filter) || (n->Node != node && !(n->WatchTree && is_below(n->Node, node))))
            continue;
        if (IsListEmpty(&n->Arms))
            n->Pending = TRUE;
        else
            answer_all(n, STATUS_NOTIFY_ENUM_DIR, TRUE);
    }
}

/* waiters end with STATUS_NOTIFY_CLEANUP and the key's handles drop their block. */
void CmpNotifyKeyDeleted(CM_NODE *node)
{
    if (!CmpNotifyCount)
        return;
    for (PLIST_ENTRY e = CmpNotifies.Flink; e != &CmpNotifies;) {
        CM_NOTIFY *n = CONTAINING_RECORD(e, CM_NOTIFY, Links);
        e = e->Flink;
        if (n->Node == node) {
            answer_all(n, STATUS_NOTIFY_CLEANUP, FALSE);
            destroy(n);
        }
    }
}

void CmpNotifyClose(CM_KEY_BODY *body)
{
    if (!body->Notify)
        return;
    ExAcquireFastMutex(&CmpLock);
    if (body->Notify) {
        answer_all(body->Notify, STATUS_NOTIFY_CLEANUP, FALSE);
        destroy(body->Notify);
    }
    CmpUnlock();
}

void CmpNotifyDelete(CM_KEY_BODY *body)
{
    if (!body->Notify)
        return;
    ExAcquireFastMutex(&CmpLock);
    if (body->Notify)
        destroy(body->Notify);
    CmpUnlock();
}

/* an ending thread's posts end with STATUS_NOTIFY_CLEANUP, without the apc. */
void CmNotifyRunDown(PETHREAD thread)
{
    if (IsListEmpty(&CmpPosts))
        return;
    ExAcquireFastMutex(&CmpLock);
    for (PLIST_ENTRY e = CmpPosts.Flink; e != &CmpPosts;) {
        CM_POST *post = CONTAINING_RECORD(e, CM_POST, Links);
        e = e->Flink;
        if (post->Thread == thread && !post->Sync) {
            post->ApcRoutine = nullptr;
            answer(post, STATUS_NOTIFY_CLEANUP);
        }
    }
    CmpUnlock();
}

/* ------------------------------------------------------------------ requests */

/* lock held. STATUS_PENDING, or success when a change was already pending. */
static NTSTATUS arm_key(CM_KEY_BODY *body, CM_POST *post, ULONG index, ULONG filter, BOOLEAN tree)
{
    if (body->Node->Flags & CM_NODE_DELETED)
        return STATUS_KEY_DELETED;
    CM_NOTIFY *n = body->Notify;
    if (!n) {
        n = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*n), KI_TAG('C', 'm', 'N', 'f'));
        if (!n)
            return STATUS_INSUFFICIENT_RESOURCES;
        n->Body = body;
        n->Node = body->Node;
        n->Filter = filter & REG_NOTIFY_FILTERS;
        n->WatchTree = tree;
        InitializeListHead(&n->Arms);
        InsertTailList(&CmpNotifies, &n->Links);
        CmpNotifyCount++;
        body->Notify = n;
    }
    CM_ARM *arm = &post->Arm[index];
    arm->Notify = n;
    arm->Post = post;
    InsertTailList(&n->Arms, &arm->Links);
    if (n->Pending) {
        n->Pending = FALSE;
        answer(post, STATUS_NOTIFY_ENUM_DIR);
        return STATUS_SUCCESS;
    }
    return STATUS_PENDING;
}

/* probes the buffer for writing by rewriting one byte per page with its own value. */
static BOOLEAN writable(PVOID buffer, ULONG length)
{
    for (ULONG_PTR p = (ULONG_PTR)buffer, end = p + length; p < end; p = (p & ~(ULONG_PTR)0xFFF) + 0x1000) {
        UCHAR byte;
        if (!KiIsUserRange((PVOID)p, 1) || !NT_SUCCESS(KiCopyFromUser(&byte, (PVOID)p, 1)) ||
            !NT_SUCCESS(KiCopyToUser((PVOID)p, &byte, 1)))
            return FALSE;
    }
    return TRUE;
}

static CM_NODE *hive_root(CM_NODE *node)
{
    for (; node; node = node->Parent)
        if (node->Hive && node->Cell == node->Hive->RootCell)
            return node;
    return nullptr;
}

NTSTATUS NTAPI NtNotifyChangeMultipleKeys(HANDLE key, ULONG count, POBJECT_ATTRIBUTES subordinate, HANDLE event,
                                          PVOID apc_routine, PVOID apc_context, PIO_STATUS_BLOCK iosb, ULONG filter,
                                          BOOLEAN tree, PVOID buffer, ULONG buffer_length, BOOLEAN async)
{
    if (count > 1)
        return STATUS_INVALID_PARAMETER;
    KPROCESSOR_MODE mode = KeGetPreviousMode();
    IopTakeIosb32(&apc_routine, &iosb);
    BOOLEAN agnostic = FALSE;
    if (mode == UserMode) {
        if (filter & REG_NOTIFY_THREAD_AGNOSTIC) {
            if (!async || apc_routine || !event)
                return STATUS_INVALID_PARAMETER;
            agnostic = TRUE;
        } else {
            ULONG64 image[2];
            PVOID address;
            ULONG length = IopIosbImage(iosb, STATUS_PENDING, 0, &address, image);
            if (buffer_length && ((ULONG_PTR)buffer & 3))
                return STATUS_DATATYPE_MISALIGNMENT;
            if (!NT_SUCCESS(KiCopyToUser(address, image, length)) || (buffer_length && !writable(buffer, buffer_length)))
                return STATUS_ACCESS_VIOLATION;
        }
    } else if (async && count) {
        return STATUS_INVALID_PARAMETER;
    }
    if (filter & ~(REG_NOTIFY_FILTERS | REG_NOTIFY_THREAD_AGNOSTIC))
        return STATUS_INVALID_PARAMETER;

    CM_KEY_BODY *body;
    ACCESS_MASK granted = 0;
    NTSTATUS status = ObReferenceHandle(key, CmKeyType, mode, (PVOID *)&body, &granted);
    if (!NT_SUCCESS(status))
        return status;
    if (mode == UserMode && !(granted & KEY_NOTIFY)) {
        ObDereferenceObject(body);
        return STATUS_ACCESS_DENIED;
    }
    CM_KEY_BODY *other = nullptr;
    if (count == 1) {
        status = CmpReferenceKeyByName(subordinate, mode, KEY_NOTIFY, &other);
        if (NT_SUCCESS(status)) {
            CM_NODE *a, *b;
            ExAcquireFastMutex(&CmpLock);
            a = hive_root(body->Node);
            b = hive_root(other->Node);
            ExReleaseFastMutex(&CmpLock);
            if (a && a == b)
                status = STATUS_INVALID_PARAMETER;
            if (!NT_SUCCESS(status))
                ObDereferenceObject(other);
        }
        if (!NT_SUCCESS(status)) {
            ObDereferenceObject(body);
            return status;
        }
    }
    PKEVENT ev = nullptr;
    if (async && event) {
        granted = 0;
        status = ObReferenceHandle(event, ExEventType, mode, (PVOID *)&ev, &granted);
        if (NT_SUCCESS(status) && mode == UserMode && !(granted & EVENT_MODIFY_STATE)) {
            ObDereferenceObject(ev);
            status = STATUS_ACCESS_DENIED;
        }
        if (!NT_SUCCESS(status)) {
            if (other)
                ObDereferenceObject(other);
            ObDereferenceObject(body);
            return status;
        }
        KeResetEvent(ev);
    }
    CM_POST *post = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*post), KI_TAG('C', 'm', 'P', 's'));
    if (!post) {
        if (ev)
            ObDereferenceObject(ev);
        if (other)
            ObDereferenceObject(other);
        ObDereferenceObject(body);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    memset(post, 0, sizeof(*post));
    post->Sync = !async;
    post->Agnostic = agnostic;
    post->Subordinate = other;
    post->Event = ev;
    KeInitializeEvent(&post->Wake, NotificationEvent, FALSE);
    if (async && !agnostic) {
        post->Iosb = iosb;
        post->ApcRoutine = apc_routine;
        post->ApcContext = apc_context;
    }
    if (!agnostic) {
        post->Thread = PsCurrentThread();
        ObReferenceObject(post->Thread);
        post->Process = PsCurrentProcess();
        ObReferenceObject(post->Process);
    }

    ExAcquireFastMutex(&CmpLock);
    InsertTailList(&CmpPosts, &post->Links);
    status = arm_key(body, post, 0, filter, tree);
    if (status == STATUS_PENDING && other)
        status = arm_key(other, post, 1, filter, tree);
    if (NT_ERROR(status)) {
        post->Done = TRUE;
        unlink_arm(&post->Arm[0]);
        unlink_arm(&post->Arm[1]);
        RemoveEntryList(&post->Links);
    }
    CmpUnlock();

    if (NT_ERROR(status)) {
        if (ev)
            ObDereferenceObject(ev);
        release_post(post);
        ObDereferenceObject(body);
        return status;
    }
    if (async) {
        /* the post now belongs to the answer, which may already be on its way. */
        ObDereferenceObject(body);
        return status;
    }
    NTSTATUS waited = KeWaitForSingleObject(&post->Wake, Executive, mode, TRUE, nullptr);
    BOOLEAN interrupted = waited == STATUS_USER_APC || waited == STATUS_ALERTED;
    ExAcquireFastMutex(&CmpLock);
    if (!post->Done) {
        post->Done = TRUE;
        unlink_arm(&post->Arm[0]);
        unlink_arm(&post->Arm[1]);
        RemoveEntryList(&post->Links);
    }
    status = interrupted ? waited : post->Status;
    CmpUnlock();
    if (!interrupted && mode == UserMode) {
        ULONG64 image[2];
        PVOID address;
        ULONG length = IopIosbImage(iosb, status, 0, &address, image);
        KiCopyToUser(address, image, length);
    }
    release_post(post);
    ObDereferenceObject(body);
    return status;
}

NTSTATUS NTAPI NtNotifyChangeKey(HANDLE key, HANDLE event, PVOID apc_routine, PVOID apc_context,
                                 PIO_STATUS_BLOCK iosb, ULONG filter, BOOLEAN tree, PVOID buffer,
                                 ULONG buffer_length, BOOLEAN async)
{
    return NtNotifyChangeMultipleKeys(key, 0, nullptr, event, apc_routine, apc_context, iosb, filter, tree, buffer,
                                      buffer_length, async);
}
