/*
 * gpu channels, see mime/gpu.h. a process that renders through the gpu gets one block of physically contiguous
 * memory, mapped in the process and handed to the host, which runs the commands written there on metal. the
 * process can add heap blocks for buffers and textures. nothing here takes part in rendering: waiting and kicking
 * are the only per frame calls, and only when the host or the gpu is behind.
 */
#include "win32k.h"

#include <mime/gpu.h>

typedef struct GPU_BLOCK {
    PVOID Kernel;
    ULONG64 Pa;
    SIZE_T Size;
    PVOID Section;
    PVOID User;
} GPU_BLOCK;

typedef struct W32GPU {
    ULONG Channel;
    ULONG BlockCount;
    MimeGpuHeader *Header;
    GPU_BLOCK Blocks[MIME_GPU_MAX_BLOCKS];
} W32GPU;

static void free_block(GPU_BLOCK *b)
{
    if (b->Section)
        ObDereferenceObject(b->Section);
    if (b->Kernel)
        MmFreeContiguousPages(b->Kernel, b->Size);
    memset(b, 0, sizeof(*b));
}

/* aligned to its power of two size, so to the host's 16K pages too. */
static NTSTATUS alloc_block(GPU_BLOCK *b, SIZE_T size)
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
        free_block(b);
    return status;
}

static W32GPU *process_gpu(void)
{
    W32PROCESS *p = PsGetProcessWin32Process(PsGetCurrentProcess());
    return p ? p->Gpu : nullptr;
}

NTSTATUS NTAPI NtGdiMimeGpuOpen(PVOID *base, PULONG size)
{
    NTSTATUS status = STATUS_SUCCESS;
    UserEnter();
    W32PROCESS *p = W32GetProcess();
    W32GPU *g = p ? p->Gpu : nullptr;
    if (p && !g) {
        g = W32Alloc(sizeof(W32GPU));
        status = g ? alloc_block(&g->Blocks[0], MIME_GPU_CHANNEL_SIZE) : STATUS_NO_MEMORY;
        ULONG64 channel = 0;
        if (NT_SUCCESS(status) &&
            HalMimeHypercall(MIME_HC_GPU_OPEN, g->Blocks[0].Pa, MIME_GPU_CHANNEL_SIZE, 0, 0, 0, 0, &channel) !=
                MIME_HC_STATUS_OK)
            status = STATUS_DEVICE_NOT_READY;
        if (NT_SUCCESS(status)) {
            g->Channel = (ULONG)channel;
            g->BlockCount = 1;
            g->Header = g->Blocks[0].Kernel;
            p->Gpu = g;
        } else if (g) {
            free_block(&g->Blocks[0]);
            W32Free(g);
            g = nullptr;
        }
    } else if (!p) {
        status = STATUS_NO_MEMORY;
    }
    PVOID user = g ? g->Blocks[0].User : nullptr;
    UserLeave();
    if (!NT_SUCCESS(status))
        return status;
    ULONG bytes = MIME_GPU_CHANNEL_SIZE;
    if (!NT_SUCCESS(MimeCopyToUser(base, &user, sizeof(user))) || !NT_SUCCESS(MimeCopyToUser(size, &bytes, sizeof(bytes))))
        return STATUS_ACCESS_VIOLATION;
    return STATUS_SUCCESS;
}

/* size is a power of two from 64K to MIME_GPU_MAX_BLOCK_SIZE. */
NTSTATUS NTAPI NtGdiMimeGpuAddBlock(ULONG size, PVOID *base, PULONG index)
{
    if (size < 0x10000 || size > MIME_GPU_MAX_BLOCK_SIZE || (size & (size - 1)))
        return STATUS_INVALID_PARAMETER;
    UserEnter();
    W32GPU *g = process_gpu();
    NTSTATUS status = g ? STATUS_SUCCESS : STATUS_INVALID_DEVICE_STATE;
    ULONG i = g ? g->BlockCount : 0;
    if (g && i >= MIME_GPU_MAX_BLOCKS)
        status = STATUS_INSUFFICIENT_RESOURCES;
    if (NT_SUCCESS(status))
        status = alloc_block(&g->Blocks[i], size);
    if (NT_SUCCESS(status) &&
        HalMimeHypercall(MIME_HC_GPU_BLOCK, g->Channel, i, g->Blocks[i].Pa, size, 0, 0, nullptr) != MIME_HC_STATUS_OK) {
        free_block(&g->Blocks[i]);
        status = STATUS_INSUFFICIENT_RESOURCES;
    }
    PVOID user = nullptr;
    if (NT_SUCCESS(status)) {
        user = g->Blocks[i].User;
        g->BlockCount++;
    }
    UserLeave();
    if (!NT_SUCCESS(status))
        return status;
    if (!NT_SUCCESS(MimeCopyToUser(base, &user, sizeof(user))) || !NT_SUCCESS(MimeCopyToUser(index, &i, sizeof(i))))
        return STATUS_ACCESS_VIOLATION;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtGdiMimeGpuKick(void)
{
    W32GPU *g = process_gpu();
    if (!g)
        return STATUS_INVALID_DEVICE_STATE;
    HalMimeHypercall(MIME_HC_GPU_KICK, g->Channel, 0, 0, 0, 0, 0, nullptr);
    return STATUS_SUCCESS;
}

/*
 * the host raises its interrupt when done_fence passes wait_fence. the short timeout covers a wakeup lost to
 * another thread clearing the shared event.
 */
NTSTATUS NTAPI NtGdiMimeGpuWait(ULONG64 fence, ULONG timeout_ms)
{
    W32GPU *g = process_gpu();
    if (!g)
        return STATUS_INVALID_DEVICE_STATE;
    MimeGpuHeader *h = g->Header;
    PKEVENT event = HalMimeGetGpuEvent();
    ULONGLONG deadline = KeQueryInterruptTime() + (ULONGLONG)timeout_ms * 10000;
    for (;;) {
        if (__atomic_load_n(&h->done_fence, __ATOMIC_ACQUIRE) >= fence)
            break;
        if (h->lost)
            return STATUS_DEVICE_REMOVED;
        KeClearEvent(event);
        __atomic_store_n(&h->wait_fence, fence, __ATOMIC_SEQ_CST);
        if (__atomic_load_n(&h->done_fence, __ATOMIC_SEQ_CST) >= fence)
            break;
        ULONGLONG now = KeQueryInterruptTime();
        if (timeout_ms != 0xFFFFFFFF && now >= deadline)
            return STATUS_TIMEOUT;
        LARGE_INTEGER t = {.QuadPart = -100000}; /* 10 ms. */
        NTSTATUS s = KeWaitForSingleObject(event, UserRequest, UserMode, TRUE, &t);
        if (s != STATUS_SUCCESS && s != STATUS_TIMEOUT)
            return s;
    }
    ULONG64 expected = fence;
    __atomic_compare_exchange_n(&h->wait_fence, &expected, 0, FALSE, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return STATUS_SUCCESS;
}

/* the host window and the part of its surface the dc's window covers, nothing while hidden. */
BOOL NTAPI NtGdiMimeGpuGetSurface(HDC hdc, MIME_GPU_SURFACE *user_surface)
{
    MIME_GPU_SURFACE s = {};
    UserEnter();
    DC *dc = DcLookup(hdc);
    WND *w = dc ? dc->Window : nullptr;
    if (w) {
        WND *top = w;
        while (top && !(top->Flags & WNDF_HOST_WINDOW))
            top = top->Parent;
        s.Window = WndHandle(w);
        s.Width = (ULONG)(w->ClientRect.right - w->ClientRect.left);
        s.Height = (ULONG)(w->ClientRect.bottom - w->ClientRect.top);
        if (top && WndIsVisible(w) && !(top->Style & WS_MINIMIZE)) {
            s.HostWindow = (ULONG)(ULONG_PTR)WndHandle(top);
            s.X = w->ClientRect.left - top->WindowRect.left;
            s.Y = w->ClientRect.top - top->WindowRect.top;
        }
    }
    UserLeave();
    if (!w) {
        W32SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return NT_SUCCESS(MimeCopyToUser(user_surface, &s, sizeof(s)));
}

/* the host is done with the memory once the close returns. */
void GpuFreeProcess(W32PROCESS *p)
{
    W32GPU *g = p->Gpu;
    if (!g)
        return;
    HalMimeHypercallUnlocked(MIME_HC_GPU_CLOSE, g->Channel, 0, 0, 0, 0, 0, nullptr);
    for (ULONG i = 0; i < g->BlockCount; i++)
        free_block(&g->Blocks[i]);
    p->Gpu = nullptr;
    W32Free(g);
}
