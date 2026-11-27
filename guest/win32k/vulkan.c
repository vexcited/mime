/*
 * Vulkan connections use physically contiguous blocks shared with the host. the call block follows mime/mvk_wire.h;
 * mapped memory and command streams use additional blocks that the host reads or imports directly.
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

/* how many processes hold a connection, so the screen need not look for presenting windows when none does. */
ULONG VkProcessCount;

typedef struct W32VK {
    ULONG Connection;
    MvkHeader *Header;
    VK_BLOCK Main;
    VK_BLOCK *Blocks;
} W32VK;

/* skip unmapping after the process address space is gone. */
static void free_block(VK_BLOCK *b, BOOLEAN unmap)
{
    if (b->User && unmap)
        ZwUnmapViewOfSection(NtCurrentProcess(), b->User);
    if (b->Section)
        ObDereferenceObject(b->Section);
    if (b->Kernel)
        MmFreeContiguousPages(b->Kernel, b->Size);
    b->User = b->Section = b->Kernel = nullptr;
}

/*
 * made before the user lock is taken: clearing tens of megabytes takes a tenth of a second while the host backs the
 * pages again, which no other thread waits for.
 */
static NTSTATUS alloc_block(VK_BLOCK *b, SIZE_T size)
{
    memset(b, 0, sizeof(*b));
    b->Size = size;
    b->Kernel = MmAllocateContiguousPagesUnlocked(size);
    if (!b->Kernel)
        return STATUS_NO_MEMORY;
    b->Pa = (ULONG64)MmGetPhysicalAddress(b->Kernel).QuadPart;
    NTSTATUS status = MmCreatePhysicalSection(b->Pa, size, &b->Section);
    if (NT_SUCCESS(status))
        status = MmMapSharedSectionUser(b->Section, PsGetCurrentProcess(), &b->User, TRUE);
    if (!NT_SUCCESS(status))
        free_block(b, TRUE);
    return status;
}

static W32VK *process_vk(void)
{
    W32PROCESS *p = PsGetProcessWin32Process(PsGetCurrentProcess());
    return p ? p->Vk : nullptr;
}

NTSTATUS NTAPI NtGdiMimeVkOpen(PVOID *base)
{
    NTSTATUS status = STATUS_SUCCESS;
    UserEnter();
    W32PROCESS *p = W32GetProcess();
    W32VK *v = p ? p->Vk : nullptr, *made = nullptr;
    if (p && !v) {
        UserLeave();
        made = W32Alloc(sizeof(W32VK));
        NTSTATUS allocated = made ? alloc_block(&made->Main, MVK_CONNECTION_SIZE) : STATUS_NO_MEMORY;
        UserEnter();
        v = p->Vk;
        /* another thread of the process connected meanwhile. */
        if (v || !NT_SUCCESS(allocated)) {
            if (made && NT_SUCCESS(allocated))
                free_block(&made->Main, TRUE);
            if (made)
                W32Free(made);
            made = nullptr;
            if (!v)
                status = allocated;
        } else {
            v = made;
        }
    }
    if (made) {
        ULONG64 connection = 0;
        if (HalMimeHypercall(MIME_HC_VK_OPEN, v->Main.Pa, MVK_CONNECTION_SIZE, 0, 0, 0, 0, &connection) !=
            MIME_HC_STATUS_OK)
            status = STATUS_DEVICE_NOT_READY;
        if (NT_SUCCESS(status)) {
            v->Connection = (ULONG)connection;
            v->Header = v->Main.Kernel;
            /* the kick reads it without any lock, see VkFastKick. */
            __atomic_store_n(&p->Vk, v, __ATOMIC_RELEASE);
            __atomic_add_fetch(&VkProcessCount, 1, __ATOMIC_RELAXED);
        } else if (v) {
            free_block(&v->Main, TRUE);
            W32Free(v);
            v = nullptr;
        }
    } else if (!p) {
        status = STATUS_NO_MEMORY;
    }
    PVOID user = v ? v->Main.User : nullptr;
    UserLeave();
    if (!NT_SUCCESS(status))
        return status;
    return MimeCopyToUser(base, &user, sizeof(user));
}

/* shared process memory for mapped buffers or command streams. */
NTSTATUS NTAPI NtGdiMimeVkAlloc(SIZE_T size, PVOID *base, PULONG64 pa)
{
    if (size < 0x1000 || size > (256u << 20))
        return STATUS_INVALID_PARAMETER;
    size = (size + 0xFFFF) & ~(SIZE_T)0xFFFF;
    if (!process_vk())
        return STATUS_INVALID_DEVICE_STATE;
    VK_BLOCK *b = W32Alloc(sizeof(VK_BLOCK));
    NTSTATUS status = b ? alloc_block(b, size) : STATUS_NO_MEMORY;
    UserEnter();
    W32VK *v = process_vk();
    if (NT_SUCCESS(status) && v) {
        b->Next = v->Blocks;
        v->Blocks = b;
    } else if (b) {
        if (NT_SUCCESS(status))
            free_block(b, TRUE);
        W32Free(b);
        b = nullptr;
        if (NT_SUCCESS(status))
            status = STATUS_INVALID_DEVICE_STATE;
    }
    PVOID user = b ? b->User : nullptr;
    ULONG64 phys = b ? b->Pa : 0;
    UserLeave();
    if (!NT_SUCCESS(status))
        return status;
    if (!NT_SUCCESS(MimeCopyToUser(base, &user, sizeof(user))) || !NT_SUCCESS(MimeCopyToUser(pa, &phys, sizeof(phys))))
        return STATUS_ACCESS_VIOLATION;
    return STATUS_SUCCESS;
}

/* free only after the host stops using the block, as required by Vulkan-1.dll. */
NTSTATUS NTAPI NtGdiMimeVkFree(PVOID base)
{
    UserEnter();
    W32VK *v = process_vk();
    NTSTATUS status = STATUS_INVALID_PARAMETER;
    for (VK_BLOCK **link = v ? &v->Blocks : nullptr; link && *link; link = &(*link)->Next) {
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

/*
 * the kick only wakes the host's thread for the connection, so the kernel asks for it before it takes any lock, with
 * interrupts off. the connection outlives every thread of its process.
 */
BOOLEAN VkFastKick(ULONG64 *result)
{
    W32PROCESS *p = PsGetProcessWin32Process(PsGetCurrentProcess());
    W32VK *v = p ? __atomic_load_n(&p->Vk, __ATOMIC_ACQUIRE) : nullptr;
    if (!v)
        return FALSE;
    HalMimeHypercall(MIME_HC_VK_KICK, v->Connection, 0, 0, 0, 0, 0, nullptr);
    *result = STATUS_SUCCESS;
    return TRUE;
}

NTSTATUS NTAPI NtGdiMimeVkKick(void)
{
    W32VK *v = process_vk();
    if (!v)
        return STATUS_INVALID_DEVICE_STATE;
    /* the host only wakes its thread for the connection, which needs nothing the kernel lock guards. */
    HalMimeHypercallUnlocked(MIME_HC_VK_KICK, v->Connection, 0, 0, 0, 0, 0, nullptr);
    return STATUS_SUCCESS;
}

/* wait for the host count of reached semaphore values to change. */
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
        LARGE_INTEGER t = {.QuadPart = -100000}; /* 10 ms. */
        NTSTATUS s = KeWaitForSingleObject(event, UserRequest, UserMode, FALSE, &t);
        if (s != STATUS_SUCCESS && s != STATUS_TIMEOUT)
            return s;
    }
}

/*
 * wait for the host to execute serial. it interrupts when done passes wait_serial; a short timeout covers another
 * thread clearing the shared event. MVK_WAIT_EVENTS instead waits for the reached-semaphore count.
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
        LARGE_INTEGER t = {.QuadPart = -100000}; /* 10 ms. */
        NTSTATUS s = KeWaitForSingleObject(event, UserRequest, UserMode, FALSE, &t);
        if (s != STATUS_SUCCESS && s != STATUS_TIMEOUT)
            return s;
    }
    ULONG64 expected = serial;
    __atomic_compare_exchange_n(&h->wait_serial, &expected, 0, FALSE, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return STATUS_SUCCESS;
}

/* close returns only after the host stops using the memory. */
void VkFreeProcess(W32PROCESS *p)
{
    W32VK *v = p->Vk;
    if (!v)
        return;
    HalMimeHypercallUnlocked(MIME_HC_VK_CLOSE, v->Connection, 0, 0, 0, 0, 0, nullptr);
    while (v->Blocks) {
        VK_BLOCK *b = v->Blocks;
        v->Blocks = b->Next;
        free_block(b, FALSE);
        W32Free(b);
    }
    free_block(&v->Main, FALSE);
    p->Vk = nullptr;
    __atomic_sub_fetch(&VkProcessCount, 1, __ATOMIC_RELAXED);
    W32Free(v);
}
