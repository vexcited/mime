/*
 * audio output streams. a stream is one block of physically contiguous
 * memory mapped in the process and handed to the host, which plays the frames
 * the process writes there (mime/audio.h). nothing here runs per buffer: the
 * process and the host's output callback only move the counters of the header.
 * a closed stream keeps its memory mapped for the next stream of the process,
 * as views are only unmapped with the process.
 */
#include "win32k.h"

#include <mime/audio.h>

typedef struct AUDIO_SLOT {
    PVOID Kernel;
    ULONG64 Pa;
    PVOID Section;
    PVOID User;
    ULONG Stream; /* the host stream, 0 while not open */
    BOOLEAN InUse;
} AUDIO_SLOT;

typedef struct W32AUDIO {
    AUDIO_SLOT Slots[MIME_AUDIO_MAX_STREAMS];
} W32AUDIO;

static void free_slot(AUDIO_SLOT *s)
{
    if (s->Section)
        ObDereferenceObject(s->Section);
    if (s->Kernel)
        MmFreeContiguousPages(s->Kernel, MIME_AUDIO_STREAM_SIZE);
    memset(s, 0, sizeof(*s));
}

static NTSTATUS alloc_slot(AUDIO_SLOT *s)
{
    s->Kernel = MmAllocateContiguousPages(MIME_AUDIO_STREAM_SIZE);
    if (!s->Kernel)
        return STATUS_NO_MEMORY;
    s->Pa = (ULONG64)MmGetPhysicalAddress(s->Kernel).QuadPart;
    NTSTATUS status = MimeMmCreatePhysicalSection(s->Pa, MIME_AUDIO_STREAM_SIZE, &s->Section);
    if (NT_SUCCESS(status))
        status = MmMapSharedSectionUser(s->Section, PsGetCurrentProcess(), &s->User, TRUE);
    if (!NT_SUCCESS(status))
        free_slot(s);
    return status;
}

static void close_stream(AUDIO_SLOT *s)
{
    if (s->Stream)
        HalMimeHypercallUnlocked(MIME_HC_AUDIO_CLOSE, s->Stream, 0, 0, 0, 0, 0, nullptr);
    s->Stream = 0;
}

NTSTATUS NTAPI NtGdiMimeAudioCreate(PVOID *base, PULONG index)
{
    NTSTATUS status = STATUS_SUCCESS;
    UserEnter();
    W32PROCESS *p = W32GetProcess();
    if (p && !p->Audio) {
        p->Audio = W32Alloc(sizeof(W32AUDIO));
        if (p->Audio)
            memset(p->Audio, 0, sizeof(W32AUDIO));
    }
    W32AUDIO *a = p ? p->Audio : nullptr;
    ULONG i = 0;
    if (!a)
        status = STATUS_NO_MEMORY;
    else {
        while (i < MIME_AUDIO_MAX_STREAMS && a->Slots[i].InUse)
            i++;
        if (i == MIME_AUDIO_MAX_STREAMS)
            status = STATUS_INSUFFICIENT_RESOURCES;
        else if (!a->Slots[i].User)
            status = alloc_slot(&a->Slots[i]);
    }
    PVOID user = nullptr;
    if (NT_SUCCESS(status)) {
        AUDIO_SLOT *s = &a->Slots[i];
        memset(s->Kernel, 0, MIME_AUDIO_HEADER_SIZE);
        s->InUse = TRUE;
        user = s->User;
    }
    UserLeave();
    if (!NT_SUCCESS(status))
        return status;
    if (!NT_SUCCESS(MimeCopyToUser(base, &user, sizeof(user))) || !NT_SUCCESS(MimeCopyToUser(index, &i, sizeof(i))))
        return STATUS_ACCESS_VIOLATION;
    return STATUS_SUCCESS;
}

NTSTATUS NTAPI NtGdiMimeAudioControl(ULONG index, ULONG op)
{
    NTSTATUS status = STATUS_SUCCESS;
    UserEnter();
    W32PROCESS *p = W32GetProcess();
    W32AUDIO *a = p ? p->Audio : nullptr;
    AUDIO_SLOT *s = a && index < MIME_AUDIO_MAX_STREAMS && a->Slots[index].InUse ? &a->Slots[index] : nullptr;
    if (!s) {
        status = STATUS_INVALID_HANDLE;
    } else if (op == MIME_AUDIO_OPEN) {
        ULONG64 stream = 0;
        close_stream(s);
        ULONG64 result = HalMimeHypercall(MIME_HC_AUDIO_OPEN, s->Pa, MIME_AUDIO_STREAM_SIZE, 0, 0, 0, 0, &stream);
        if (result == MIME_HC_STATUS_OK)
            s->Stream = (ULONG)stream;
        else
            status = result == MIME_HC_STATUS_UNSUPPORTED ? STATUS_NOT_SUPPORTED : STATUS_DEVICE_NOT_READY;
    } else if (op == MIME_AUDIO_START || op == MIME_AUDIO_STOP) {
        if (!s->Stream ||
            HalMimeHypercall(MIME_HC_AUDIO_START, s->Stream, op == MIME_AUDIO_START, 0, 0, 0, 0, nullptr) != MIME_HC_STATUS_OK)
            status = STATUS_DEVICE_NOT_READY;
    } else if (op == MIME_AUDIO_CLOSE) {
        close_stream(s);
        s->InUse = FALSE;
    } else {
        status = STATUS_INVALID_PARAMETER;
    }
    UserLeave();
    return status;
}

/* the host is done with the memory once each close returns. */
void AudioFreeProcess(W32PROCESS *p)
{
    W32AUDIO *a = p->Audio;
    if (!a)
        return;
    for (ULONG i = 0; i < MIME_AUDIO_MAX_STREAMS; i++) {
        close_stream(&a->Slots[i]);
        free_slot(&a->Slots[i]);
    }
    p->Audio = nullptr;
    W32Free(a);
}
