/*
 * vulkan connections. a process that uses vulkan gets one block of
 * physically contiguous memory, mapped in the process and handed to the
 * host, which runs the calls the process writes there (mime/mvk_wire.h).
 * the memory the process maps through vulkan, and the streams of its
 * command buffers, are more such blocks: the host reads them, and imports
 * the mapped ones into its driver, in place.
 */
#include "win32k.h"

#include <mime/mvk_wire.h>

typedef struct VK_BLOCK {
    struct VK_BLOCK *Next;
    PVOID Kernel;
    ULONG64 Pa;
    SIZE_T Size;
    PVOID Section;
    PVOID User;
} VK_BLOCK;

typedef struct W32VK {
    ULONG Connection;
    MvkHeader *Header;
    VK_BLOCK Main;
    VK_BLOCK *Blocks;
} W32VK;

/* unmap is FALSE when the process is gone, its address space with it. */
static void free_block(VK_BLOCK *b, BOOLEAN unmap)
{
    if (b->User && unmap)
        ZwUnmapViewOfSection(NtCurrentProcess(), b->User);
    if (b->Section)
        ObDereferenceObject(b->Section);
    if (b->Kernel)
        MmFreeContiguousPages(b->Kernel, b->Size);
    b->User = b->Section = b->Kernel = NULL;
}

static NTSTATUS alloc_block(VK_BLOCK *b, SIZE_T size)
{
    memset(b, 0, sizeof(*b));
    b->Size = size;
    b->Kernel = MmAllocateContiguousPages(size);
    if (!b->Kernel)
        return STATUS_NO_MEMORY;
    b->Pa = (ULONG64)MmGetPhysicalAddress(b->Kernel).QuadPart;
    NTSTATUS status = MimeMmCreatePhysicalSection(b->Pa, size, &b->Section);
    if (NT_SUCCESS(status))
        status = MmMapSharedSectionUser(b->Section, PsGetCurrentProcess(), &b->User, TRUE);
    if (!NT_SUCCESS(status))
        free_block(b, TRUE);
    return status;
}

static W32VK *process_vk(void)
{
    W32PROCESS *p = PsGetProcessWin32Process(PsGetCurrentProcess());
    return p ? p->Vk : NULL;
}

NTSTATUS NTAPI NtGdiMimeVkOpen(PVOID *base)
{
    NTSTATUS status = STATUS_SUCCESS;
    UserEnter();
    W32PROCESS *p = W32GetProcess();
    W32VK *v = p ? p->Vk : NULL;
    if (p && !v) {
        v = W32Alloc(sizeof(W32VK));
        status = v ? alloc_block(&v->Main, MVK_CONNECTION_SIZE) : STATUS_NO_MEMORY;
        ULONG64 connection = 0;
        if (NT_SUCCESS(status) && HalMimeHypercall(MIME_HC_VK_OPEN, v->Main.Pa, MVK_CONNECTION_SIZE, 0, 0, 0, 0,
                                                   &connection) != MIME_HC_STATUS_OK)
            status = STATUS_DEVICE_NOT_READY;
        if (NT_SUCCESS(status)) {
            v->Connection = (ULONG)connection;
            v->Header = v->Main.Kernel;
            p->Vk = v;
        } else if (v) {
            free_block(&v->Main, TRUE);
            W32Free(v);
            v = NULL;
        }
    } else if (!p) {
        status = STATUS_NO_MEMORY;
    }
    PVOID user = v ? v->Main.User : NULL;
    UserLeave();
    if (!NT_SUCCESS(status))
        return status;
    return MimeCopyToUser(base, &user, sizeof(user));
}

/* a block of the process's memory the host reads in place: mapped memory or command streams. */
NTSTATUS NTAPI NtGdiMimeVkAlloc(SIZE_T size, PVOID *base, PULONG64 pa)
{
    if (size < 0x1000 || size > (256u << 20))
        return STATUS_INVALID_PARAMETER;
    size = (size + 0xFFFF) & ~(SIZE_T)0xFFFF;
    UserEnter();
    W32VK *v = process_vk();
    NTSTATUS status = v ? STATUS_SUCCESS : STATUS_INVALID_DEVICE_STATE;
    VK_BLOCK *b = v ? W32Alloc(sizeof(VK_BLOCK)) : NULL;
    if (v && !b)
        status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(status))
        status = alloc_block(b, size);
    if (NT_SUCCESS(status)) {
        b->Next = v->Blocks;
        v->Blocks = b;
    } else if (b) {
        W32Free(b);
        b = NULL;
    }
    PVOID user = b ? b->User : NULL;
    ULONG64 phys = b ? b->Pa : 0;
    UserLeave();
    if (!NT_SUCCESS(status))
        return status;
    if (!NT_SUCCESS(MimeCopyToUser(base, &user, sizeof(user))) || !NT_SUCCESS(MimeCopyToUser(pa, &phys, sizeof(phys))))
        return STATUS_ACCESS_VIOLATION;
    return STATUS_SUCCESS;
}

/* the caller frees a block only once the host stopped using it, see vulkan-1.dll. */
NTSTATUS NTAPI NtGdiMimeVkFree(PVOID base)
{
    UserEnter();
    W32VK *v = process_vk();
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    for (VK_BLOCK **link = v ? &v->Blocks : NULL; link && *link; link = &(*link)->Next) {
        VK_BLOCK *b = *link;
        if (b->User == base) {
            *link = b->Next;
            free_block(b, TRUE);
            W32Free(b);
            status = STATUS_SUCCESS;
            break;
        }
    }
    UserLeave();
    return status;
}

NTSTATUS NTAPI NtGdiMimeVkKick(void)
{
    W32VK *v = process_vk();
    if (!v)
        return STATUS_INVALID_DEVICE_STATE;
    HalMimeHypercall(MIME_HC_VK_KICK, v->Connection, 0, 0, 0, 0, 0, NULL);
    return STATUS_SUCCESS;
}

/* sleeps until the host's count of watched semaphores reached differs from what the thread saw. */
static NTSTATUS wait_events(MvkHeader *h, ULONG64 seen, ULONG timeout_ms)
{
    PKEVENT event = HalMimeGetGpuEvent();
    ULONGLONG deadline = KeQueryInterruptTime() + (ULONGLONG)timeout_ms * 10000;
    for (;;) {
        if (__atomic_load_n(&h->events, __ATOMIC_ACQUIRE) != seen)
            return STATUS_SUCCESS;
        if (h->lost)
            return STATUS_DEVICE_REMOVED;
        KeClearEvent(event);
        if (__atomic_load_n(&h->events, __ATOMIC_SEQ_CST) != seen)
            return STATUS_SUCCESS;
        if (timeout_ms != 0xFFFFFFFF && KeQueryInterruptTime() >= deadline)
            return STATUS_TIMEOUT;
        LARGE_INTEGER t = {.QuadPart = -100000}; /* 10 ms */
        NTSTATUS s = KeWaitForSingleObject(event, UserRequest, UserMode, TRUE, &t);
        if (s == STATUS_USER_APC || s == STATUS_ALERTED)
            return s;
    }
}

/*
 * sleeps until the host ran the call of serial. the host raises its
 * interrupt when done passes wait_serial; the short timeout covers a wake
 * up lost to another thread clearing the shared event. a serial with
 * MVK_WAIT_EVENTS waits for the count of watched semaphores instead.
 */
NTSTATUS NTAPI NtGdiMimeVkWait(ULONG64 serial, ULONG timeout_ms)
{
    W32VK *v = process_vk();
    if (!v)
        return STATUS_INVALID_DEVICE_STATE;
    MvkHeader *h = v->Header;
    if (serial & MVK_WAIT_EVENTS)
        return wait_events(h, serial & ~MVK_WAIT_EVENTS, timeout_ms);
    PKEVENT event = HalMimeGetGpuEvent();
    ULONGLONG deadline = KeQueryInterruptTime() + (ULONGLONG)timeout_ms * 10000;
    for (;;) {
        if (__atomic_load_n(&h->done, __ATOMIC_ACQUIRE) >= serial)
            break;
        if (h->lost)
            return STATUS_DEVICE_REMOVED;
        KeClearEvent(event);
        __atomic_store_n(&h->wait_serial, serial, __ATOMIC_SEQ_CST);
        if (__atomic_load_n(&h->done, __ATOMIC_SEQ_CST) >= serial)
            break;
        if (timeout_ms != 0xFFFFFFFF && KeQueryInterruptTime() >= deadline)
            return STATUS_TIMEOUT;
        LARGE_INTEGER t = {.QuadPart = -100000}; /* 10 ms */
        NTSTATUS s = KeWaitForSingleObject(event, UserRequest, UserMode, TRUE, &t);
        if (s == STATUS_USER_APC || s == STATUS_ALERTED)
            return s;
    }
    ULONG64 expected = serial;
    __atomic_compare_exchange_n(&h->wait_serial, &expected, 0, FALSE, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return STATUS_SUCCESS;
}

/* the host is done with the memory once the close returns. */
void VkFreeProcess(W32PROCESS *p)
{
    W32VK *v = p->Vk;
    if (!v)
        return;
    HalMimeHypercall(MIME_HC_VK_CLOSE, v->Connection, 0, 0, 0, 0, 0, NULL);
    while (v->Blocks) {
        VK_BLOCK *b = v->Blocks;
        v->Blocks = b->Next;
        free_block(b, FALSE);
        W32Free(b);
    }
    free_block(&v->Main, FALSE);
    p->Vk = NULL;
    W32Free(v);
}
