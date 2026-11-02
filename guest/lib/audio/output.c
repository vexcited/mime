#include "output.h"

#include <mime/ntuser.h>
#include <rtl.h>

/* the periods are kept by the multimedia time api set that kernel32 hosts. */
static void period(const char *name)
{
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    UINT(WINAPI * function)(UINT) = kernel32 ? (UINT(WINAPI *)(UINT))GetProcAddress(kernel32, name) : nullptr;
    if (function)
        function(1);
}

void MimeOutputPeriodBegin(void)
{
    period("timeBeginPeriod");
}

void MimeOutputPeriodEnd(void)
{
    period("timeEndPeriod");
}

NTSTATUS MimeOutputOpen(MimeOutput *o, UINT32 rate, UINT32 channels, UINT32 bits, BOOL is_float)
{
    memset(o, 0, sizeof(*o));
    if (!rate || !channels || channels > 8 || (bits != 16 && bits != 24 && bits != 32) || (is_float && bits != 32))
        return STATUS_INVALID_PARAMETER;
    PVOID base;
    ULONG slot;
    NTSTATUS status = NtGdiMimeAudioCreate(&base, &slot);
    if (!NT_SUCCESS(status))
        return status;
    MimeAudioHeader *h = base;
    UINT32 frame_bytes = channels * bits / 8;
    h->rate = rate;
    h->channels = channels;
    h->bits = bits;
    h->is_float = is_float;
    h->frames = (MIME_AUDIO_STREAM_SIZE - MIME_AUDIO_HEADER_SIZE) / frame_bytes;
    h->volume = 65536;
    status = NtGdiMimeAudioControl(slot, MIME_AUDIO_OPEN);
    if (!NT_SUCCESS(status)) {
        NtGdiMimeAudioControl(slot, MIME_AUDIO_CLOSE);
        return status;
    }
    o->hdr = h;
    o->ring = (BYTE *)base + MIME_AUDIO_HEADER_SIZE;
    o->slot = slot;
    o->frames = h->frames;
    o->frame_bytes = frame_bytes;
    o->rate = rate;
    o->channels = channels;
    return STATUS_SUCCESS;
}

void MimeOutputClose(MimeOutput *o)
{
    if (!o->hdr)
        return;
    NtGdiMimeAudioControl(o->slot, MIME_AUDIO_STOP);
    NtGdiMimeAudioControl(o->slot, MIME_AUDIO_CLOSE);
    memset(o, 0, sizeof(*o));
}

NTSTATUS MimeOutputStart(MimeOutput *o)
{
    if (o->started)
        return STATUS_SUCCESS;
    NTSTATUS status = NtGdiMimeAudioControl(o->slot, MIME_AUDIO_START);
    o->started = NT_SUCCESS(status);
    return status;
}

void MimeOutputStop(MimeOutput *o)
{
    if (!o->started)
        return;
    NtGdiMimeAudioControl(o->slot, MIME_AUDIO_STOP);
    o->started = FALSE;
}

UINT64 MimeOutputPlayed(const MimeOutput *o)
{
    return __atomic_load_n(&o->hdr->played, __ATOMIC_ACQUIRE);
}

UINT64 MimeOutputWritten(const MimeOutput *o)
{
    return o->hdr->written;
}

UINT32 MimeOutputQueued(const MimeOutput *o)
{
    UINT64 queued = o->hdr->written - MimeOutputPlayed(o);
    return queued > o->frames ? o->frames : (UINT32)queued;
}

UINT32 MimeOutputSpace(const MimeOutput *o)
{
    return o->frames - MimeOutputQueued(o);
}

BYTE *MimeOutputBegin(MimeOutput *o, UINT32 *n)
{
    UINT32 space = MimeOutputSpace(o), at = (UINT32)(o->hdr->written % o->frames);
    UINT32 run = o->frames - at;
    *n = space < run ? space : run;
    return o->ring + (SIZE_T)at * o->frame_bytes;
}

void MimeOutputCommit(MimeOutput *o, UINT32 n)
{
    __atomic_store_n(&o->hdr->written, o->hdr->written + n, __ATOMIC_RELEASE);
}

UINT32 MimeOutputWrite(MimeOutput *o, const void *data, UINT32 n)
{
    UINT32 done = 0;
    while (done < n) {
        UINT32 room;
        BYTE *dst = MimeOutputBegin(o, &room);
        if (!room)
            break;
        UINT32 k = n - done < room ? n - done : room;
        if (data)
            memcpy(dst, (const BYTE *)data + (SIZE_T)done * o->frame_bytes, (SIZE_T)k * o->frame_bytes);
        else
            memset(dst, 0, (SIZE_T)k * o->frame_bytes);
        MimeOutputCommit(o, k);
        done += k;
    }
    return done;
}

void MimeOutputDrop(MimeOutput *o)
{
    __atomic_store_n(&o->hdr->written, MimeOutputPlayed(o), __ATOMIC_RELEASE);
}

void MimeOutputVolume(MimeOutput *o, float level)
{
    if (level < 0)
        level = 0;
    if (level > 1)
        level = 1;
    __atomic_store_n(&o->hdr->volume, (UINT32)(level * 65536.0f + 0.5f), __ATOMIC_RELAXED);
}
