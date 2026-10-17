/*
 * midi output as winmmbase gives it. device 0 is the gs wavetable synth,
 * the one device windows always has; the mapper, id -1, sends every
 * channel to it. each may be open once per process, and the synth takes
 * long messages at once, so their headers complete before the call
 * returns. streams are winmm's own sequencer over a device, as windows
 * plays them on the synth, which cannot stream. there are no midi inputs.
 */
#define MIME_BUILD_WINMM
#include "winmm_private.h"
#include "synth.h"

#define HEADER_MIN 0x2C
#define HEADER_FLAGS 0x0000302F
#define MAPPER_NAME L"Microsoft MIDI Mapper"
#define SYNTH_NAME L"Microsoft GS Wavetable Synth"

typedef struct STREAM STREAM;

typedef struct MIDI_OUT {
    struct MIDI_OUT *next;
    UINT id;           /* 0 or MIDI_MAPPER */
    DWORD callback;
    DWORD_PTR target, instance;
    BYTE status;       /* the mapper's running status */
    STREAM *stream;    /* when the handle is a stream */
    volatile LONG busy;
} MIDI_OUT;

struct STREAM {
    MIDI_OUT *out;
    RTL_CRITICAL_SECTION lock;
    HANDLE thread, wake;
    volatile BOOL quit;
    BOOL paused;
    DWORD division, tempo;
    LONG64 tick0;      /* the tick at time0 */
    DWORD time0;       /* timeGetTime of tick0 while running */
    DWORD played_ms;   /* the playing time before time0 */
    MIDIHDR *head, *tail;
    DWORD offset;      /* of the next event in the head */
    LONG64 event_tick; /* of the next event, once its delta is counted */
    BOOL delta_counted;
    BOOL called;       /* the position callback of the next event went out */
    BYTE status;
    BYTE notes[16][128];
};

static RTL_CRITICAL_SECTION g_lock = {nullptr, -1, 0, nullptr, nullptr, 0};
static MIDI_OUT *g_outs;
static SYNTH *g_synth;
static MIDI_OUT *g_synth_owner;     /* the handle of device 0, or of the mapper holding it */
static BOOL g_mapper_open;
static BOOL g_mapper_caps;          /* the mapper learned its target's support */
static DWORD g_mapper_volume = 0xFFFFFFFF;

/* ------------------------------------------------------------------ handles */

static MIDI_OUT *find(HANDLE h, BOOL streams)
{
    RtlEnterCriticalSection(&g_lock);
    MIDI_OUT *m = g_outs;
    while (m && (HANDLE)m != h)
        m = m->next;
    if (m && !streams && m->stream)
        m = nullptr;
    RtlLeaveCriticalSection(&g_lock);
    return m;
}

static void notify(MIDI_OUT *m, UINT message, DWORD_PTR param1)
{
    DriverCallback(m->target, m->callback, (HANDLE)m, message, m->instance, param1, 0);
}

static MMRESULT check_callback(DWORD flags, DWORD_PTR callback)
{
    switch (flags & CALLBACK_TYPEMASK) {
    case CALLBACK_WINDOW:
        return !callback || IsWindow((HWND)callback) ? MMSYSERR_NOERROR : MMSYSERR_INVALPARAM;
    case CALLBACK_FUNCTION:
        return callback ? MMSYSERR_NOERROR : MMSYSERR_INVALPARAM;
    default:
        return MMSYSERR_NOERROR;
    }
}

/* the synth is opened for the first user only; a second gets MMSYSERR_ALLOCATED. */
static MMRESULT open_device(UINT id, MIDI_OUT *m)
{
    if (id == MIDI_MAPPER) {
        if (g_mapper_open)
            return MMSYSERR_ALLOCATED;
    }
    if (g_synth)
        return MMSYSERR_ALLOCATED;
    MMRESULT r = SynthOpen(&g_synth);
    if (r)
        return r;
    g_synth_owner = m;
    if (id == MIDI_MAPPER)
        g_mapper_open = TRUE;
    return MMSYSERR_NOERROR;
}

static void close_device(MIDI_OUT *m)
{
    if (g_synth_owner != m)
        return;
    SynthClose(g_synth);
    g_synth = nullptr;
    g_synth_owner = nullptr;
    if (m->id == MIDI_MAPPER)
        g_mapper_open = FALSE;
}

static MIDI_OUT *new_handle(UINT id, DWORD flags, DWORD_PTR callback, DWORD_PTR instance)
{
    MIDI_OUT *m = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*m));
    if (!m)
        return nullptr;
    m->id = id;
    m->callback = (flags & CALLBACK_TYPEMASK) >> 16;
    m->target = callback;
    m->instance = instance;
    return m;
}

/* ------------------------------------------------------------------ messages to the device */

static void device_short(MIDI_OUT *m, DWORD message)
{
    if (m->id == MIDI_MAPPER) {
        BYTE status = (BYTE)message;
        if (status < 0x80) {
            if (!m->status)
                return;
            message = m->status | message << 8;
            status = m->status;
        } else if (status < 0xF8) {
            m->status = status;
        }
        if (status >= 0xF0)
            return;
    }
    RtlEnterCriticalSection(&g_lock);
    if (g_synth)
        SynthShort(g_synth, message);
    RtlLeaveCriticalSection(&g_lock);
}

/* the mapper splits a buffer of channel messages into short ones; system exclusive goes whole. */
static void device_long(MIDI_OUT *m, const BYTE *data, DWORD size)
{
    if (m->id == MIDI_MAPPER && size && data[0] < 0xF0) {
        static const BYTE lengths[8] = {3, 3, 3, 3, 2, 2, 3, 0};
        for (DWORD i = 0; i < size;) {
            BYTE status = data[i];
            if (status < 0x80 || status >= 0xF0) {
                i++;
                continue;
            }
            DWORD n = lengths[(status >> 4) & 7];
            if (i + n > size)
                break;
            DWORD message = status;
            for (DWORD k = 1; k < n; k++)
                message |= (DWORD)data[i + k] << (8 * k);
            device_short(m, message);
            i += n;
        }
        return;
    }
    RtlEnterCriticalSection(&g_lock);
    if (g_synth)
        SynthLong(g_synth, data, size);
    RtlLeaveCriticalSection(&g_lock);
}

static void device_reset(void)
{
    RtlEnterCriticalSection(&g_lock);
    if (g_synth)
        SynthReset(g_synth);
    RtlLeaveCriticalSection(&g_lock);
}

/* ------------------------------------------------------------------ capabilities */

static MMRESULT caps_w(UINT_PTR id, MIDIOUTCAPSW *out, UINT size)
{
    if (!size)
        return MMSYSERR_NOERROR;
    if (!out)
        return MMSYSERR_INVALPARAM;
    MIDI_OUT *m = find((HANDLE)id, TRUE);
    if (m)
        id = m->id;
    MIDIOUTCAPSW caps = {};
    caps.wMid = MM_MICROSOFT;
    if (id == MIDI_MAPPER || id == (UINT_PTR)-1) {
        caps.wPid = 1;
        caps.vDriverVersion = 0x0500;
        lstrcpyW(caps.szPname, MAPPER_NAME);
        caps.wTechnology = MOD_MAPPER;
        caps.wChannelMask = 0xFFFF;
        caps.dwSupport = MIDICAPS_VOLUME | MIDICAPS_STREAM;
        g_mapper_caps = TRUE;
    } else if (id == 0) {
        caps.wPid = 0x1B;
        caps.vDriverVersion = 0x0100;
        lstrcpyW(caps.szPname, SYNTH_NAME);
        caps.wTechnology = MOD_SWSYNTH;
        caps.wVoices = 32;
        caps.wNotes = 32;
        caps.wChannelMask = 0xFFFF;
        caps.dwSupport = MIDICAPS_VOLUME;
    } else {
        return MMSYSERR_BADDEVICEID;
    }
    memcpy(out, &caps, size < sizeof(caps) ? size : sizeof(caps));
    return MMSYSERR_NOERROR;
}

WINMMAPI UINT WINAPI midiOutGetNumDevs(void)
{
    return 1;
}

WINMMAPI MMRESULT WINAPI midiOutGetDevCapsW(UINT_PTR id, LPMIDIOUTCAPSW caps, UINT size)
{
    return caps_w(id, caps, size);
}

WINMMAPI MMRESULT WINAPI midiOutGetDevCapsA(UINT_PTR id, LPMIDIOUTCAPSA caps, UINT size)
{
    if (!size)
        return MMSYSERR_NOERROR;
    if (!caps)
        return MMSYSERR_INVALPARAM;
    MIDIOUTCAPSW w;
    MMRESULT r = caps_w(id, &w, sizeof(w));
    if (r)
        return r;
    MIDIOUTCAPSA a = {w.wMid, w.wPid, w.vDriverVersion};
    WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w.szPname, -1, a.szPname, MAXPNAMELEN, nullptr, nullptr);
    a.wTechnology = w.wTechnology;
    a.wVoices = w.wVoices;
    a.wNotes = w.wNotes;
    a.wChannelMask = w.wChannelMask;
    a.dwSupport = w.dwSupport;
    memcpy(caps, &a, size < sizeof(a) ? size : sizeof(a));
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI midiOutGetErrorTextA(MMRESULT error, LPSTR text, UINT size)
{
    return MmErrorTextA(error, text, size);
}

WINMMAPI MMRESULT WINAPI midiOutGetErrorTextW(MMRESULT error, LPWSTR text, UINT size)
{
    return MmErrorTextW(error, text, size);
}

/* ------------------------------------------------------------------ open and close */

WINMMAPI MMRESULT WINAPI midiOutOpen(LPHMIDIOUT out, UINT id, DWORD_PTR callback, DWORD_PTR instance, DWORD flags)
{
    if (!out)
        return MMSYSERR_INVALPARAM;
    if (flags & (id == MIDI_MAPPER ? 0xBFF3 : 0x3FF3))
        return MMSYSERR_INVALFLAG;
    if (callback && check_callback(flags, callback))
        return MMSYSERR_INVALPARAM;
    *out = nullptr;
    if (id != 0 && id != MIDI_MAPPER)
        return MMSYSERR_BADDEVICEID;
    MIDI_OUT *m = new_handle(id, flags, callback, instance);
    if (!m)
        return MMSYSERR_NOMEM;
    RtlEnterCriticalSection(&g_lock);
    MMRESULT r = open_device(id, m);
    if (!r) {
        m->next = g_outs;
        g_outs = m;
    }
    RtlLeaveCriticalSection(&g_lock);
    if (r) {
        HeapFree(GetProcessHeap(), 0, m);
        return r;
    }
    *out = (HMIDIOUT)m;
    notify(m, MOM_OPEN, 0);
    return MMSYSERR_NOERROR;
}

static void unlink_handle(MIDI_OUT *m)
{
    RtlEnterCriticalSection(&g_lock);
    for (MIDI_OUT **p = &g_outs; *p; p = &(*p)->next)
        if (*p == m) {
            *p = m->next;
            break;
        }
    close_device(m);
    RtlLeaveCriticalSection(&g_lock);
}

WINMMAPI MMRESULT WINAPI midiOutClose(HMIDIOUT h)
{
    MIDI_OUT *m = find(h, FALSE);
    if (!m)
        return MMSYSERR_INVALHANDLE;
    if (m->busy)
        return MMSYSERR_HANDLEBUSY;
    unlink_handle(m);
    notify(m, MOM_CLOSE, 0);
    HeapFree(GetProcessHeap(), 0, m);
    return MMSYSERR_NOERROR;
}

/* ------------------------------------------------------------------ data */

static MMRESULT check_header(const MIDIHDR *h, UINT size)
{
    return h && size >= HEADER_MIN ? MMSYSERR_NOERROR : MMSYSERR_INVALPARAM;
}

WINMMAPI MMRESULT WINAPI midiOutPrepareHeader(HMIDIOUT h, LPMIDIHDR header, UINT size)
{
    MIDI_OUT *m = find(h, TRUE);
    if (!m)
        return MMSYSERR_INVALHANDLE;
    if (check_header(header, size))
        return MMSYSERR_INVALPARAM;
    if (header->dwFlags & MHDR_PREPARED)
        return MMSYSERR_NOERROR;
    if (m->stream && header->dwBufferLength > 0x10000)
        return MMSYSERR_INVALPARAM;
    header->dwFlags = MHDR_PREPARED;
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI midiOutUnprepareHeader(HMIDIOUT h, LPMIDIHDR header, UINT size)
{
    MIDI_OUT *m = find(h, TRUE);
    if (!m)
        return MMSYSERR_INVALHANDLE;
    if (check_header(header, size))
        return MMSYSERR_INVALPARAM;
    if (!(header->dwFlags & MHDR_PREPARED))
        return MMSYSERR_NOERROR;
    if (header->dwFlags & MHDR_INQUEUE)
        return MIDIERR_STILLPLAYING;
    header->dwFlags &= ~MHDR_PREPARED;
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI midiOutShortMsg(HMIDIOUT h, DWORD message)
{
    MIDI_OUT *m = find(h, TRUE);
    if (!m)
        return MMSYSERR_INVALHANDLE;
    if (m->stream && !(message & 0x80))
        return MIDIERR_BADOPENMODE;
    InterlockedIncrement(&m->busy);
    device_short(m, message);
    InterlockedDecrement(&m->busy);
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI midiOutLongMsg(HMIDIOUT h, LPMIDIHDR header, UINT size)
{
    if (check_header(header, size))
        return MMSYSERR_INVALPARAM;
    if (header->dwFlags & ~HEADER_FLAGS)
        return MMSYSERR_INVALFLAG;
    if (!(header->dwFlags & MHDR_PREPARED))
        return MIDIERR_UNPREPARED;
    if (header->dwFlags & MHDR_INQUEUE)
        return MIDIERR_STILLPLAYING;
    if (!header->dwBufferLength)
        return MMSYSERR_INVALPARAM;
    header->dwFlags &= ~MHDR_ISSTRM;
    MIDI_OUT *m = find(h, TRUE);
    if (!m)
        return MMSYSERR_INVALHANDLE;
    if (m->stream)
        return MMSYSERR_NOTSUPPORTED;
    InterlockedIncrement(&m->busy);
    device_long(m, (const BYTE *)header->lpData, header->dwBufferLength);
    header->dwFlags = (header->dwFlags & ~MHDR_INQUEUE) | MHDR_DONE;
    notify(m, MOM_DONE, (DWORD_PTR)header);
    InterlockedDecrement(&m->busy);
    return MMSYSERR_NOERROR;
}

static MMRESULT stream_stop(STREAM *s, BOOL reset);

WINMMAPI MMRESULT WINAPI midiOutReset(HMIDIOUT h)
{
    MIDI_OUT *m = find(h, TRUE);
    if (!m)
        return MMSYSERR_INVALHANDLE;
    if (m->stream)
        return stream_stop(m->stream, TRUE);
    device_reset();
    return MMSYSERR_NOERROR;
}

/* volume by handle, or by id while the device is open. */
static MIDI_OUT *volume_target(HMIDIOUT h, MMRESULT *r)
{
    MIDI_OUT *m = find(h, TRUE);
    UINT id = m ? m->id : (UINT)(UINT_PTR)h;
    *r = MMSYSERR_NOERROR;
    if (id == MIDI_MAPPER) {
        if (!g_mapper_caps)
            *r = MMSYSERR_NOTSUPPORTED;
        return m;
    }
    if (id != 0) {
        *r = MMSYSERR_BADDEVICEID;
        return nullptr;
    }
    if (!g_synth)
        *r = MMSYSERR_NODRIVER;
    return m;
}

WINMMAPI MMRESULT WINAPI midiOutSetVolume(HMIDIOUT h, DWORD volume)
{
    MMRESULT r;
    MIDI_OUT *m = volume_target(h, &r);
    if (r)
        return r;
    UINT id = m ? m->id : (UINT)(UINT_PTR)h;
    if (id == MIDI_MAPPER)
        g_mapper_volume = volume;
    RtlEnterCriticalSection(&g_lock);
    if (g_synth)
        SynthSetVolume(g_synth, LOWORD(volume) / 65535.0f);
    RtlLeaveCriticalSection(&g_lock);
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI midiOutGetVolume(HMIDIOUT h, LPDWORD volume)
{
    if (!volume)
        return MMSYSERR_INVALPARAM;
    MMRESULT r;
    MIDI_OUT *m = volume_target(h, &r);
    if (r)
        return r;
    UINT id = m ? m->id : (UINT)(UINT_PTR)h;
    if (id == MIDI_MAPPER) {
        *volume = g_mapper_volume;
        return MMSYSERR_NOERROR;
    }
    RtlEnterCriticalSection(&g_lock);
    DWORD v = g_synth ? (DWORD)(int)(SynthGetVolume(g_synth) * 65535.0f) : 0;
    RtlLeaveCriticalSection(&g_lock);
    *volume = v * 0x10001;
    return MMSYSERR_NOERROR;
}

/* the synth downloads every instrument when it opens, so there is nothing to cache. */
static MMRESULT cache(HMIDIOUT h, LPWORD patches, UINT flags)
{
    if (!patches)
        return MMSYSERR_INVALPARAM;
    if (flags & ~7u)
        return MMSYSERR_INVALFLAG;
    if (!find(h, TRUE))
        return MMSYSERR_INVALHANDLE;
    return MMSYSERR_NOTSUPPORTED;
}

WINMMAPI MMRESULT WINAPI midiOutCachePatches(HMIDIOUT h, UINT bank, LPWORD patches, UINT flags)
{
    return cache(h, patches, flags);
}

WINMMAPI MMRESULT WINAPI midiOutCacheDrumPatches(HMIDIOUT h, UINT patch, LPWORD keys, UINT flags)
{
    return cache(h, keys, flags);
}

WINMMAPI MMRESULT WINAPI midiOutGetID(HMIDIOUT h, LPUINT id)
{
    if (!id)
        return MMSYSERR_INVALPARAM;
    MIDI_OUT *m = find(h, FALSE);
    if (!m)
        return MMSYSERR_INVALHANDLE;
    *id = m->id;
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI midiOutMessage(HMIDIOUT h, UINT message, DWORD_PTR param1, DWORD_PTR param2)
{
    MIDI_OUT *m = find(h, TRUE);
    UINT id = m ? m->id : (UINT)(UINT_PTR)h;
    if (id != 0 && id != MIDI_MAPPER)
        return MMSYSERR_BADDEVICEID;
    if (message == DRVM_MAPPER_PREFERRED_GET && id == MIDI_MAPPER) {
        if (!param1 || !param2)
            return MMSYSERR_INVALPARAM;
        *(DWORD *)param1 = 0;
        *(DWORD *)param2 = 0;
        return MMSYSERR_NOERROR;
    }
    return MMSYSERR_NOTSUPPORTED;
}

/* ------------------------------------------------------------------ streams */

static DWORD now_ms(void)
{
    return timeGetTime();
}

/* ticks per second times 1000 per ms, as a ratio: ticks = elapsed * num / den. */
static void rate(const STREAM *s, LONG64 *num, LONG64 *den)
{
    if (!(s->division & 0x8000)) {
        *num = 1000LL * (s->division & 0x7FFF);
        *den = s->tempo ? s->tempo : 1;
        return;
    }
    int fps = -(signed char)(s->division >> 8), tpf = s->division & 0xFF;
    if (fps == 24 || fps == 25) {
        *num = (LONG64)fps * tpf;
        *den = 1000;
    } else {
        *num = 2997LL * tpf;
        *den = 100000;
    }
}

static LONG64 current_tick(const STREAM *s)
{
    if (s->paused)
        return s->tick0;
    LONG64 num, den;
    rate(s, &num, &den);
    return s->tick0 + (LONG64)(DWORD)(now_ms() - s->time0) * num / den;
}

/* the clock restarted at a tick, keeping the time that tick fell on. */
static void rebase(STREAM *s, LONG64 tick)
{
    if (s->paused) {
        s->tick0 = tick;
        return;
    }
    LONG64 num, den;
    rate(s, &num, &den);
    DWORD at = s->time0 + (DWORD)((tick - s->tick0) * den / (num ? num : 1));
    s->played_ms += at - s->time0;
    s->time0 = at;
    s->tick0 = tick;
}

static void send_notes_off(STREAM *s)
{
    for (DWORD c = 0; c < 16; c++)
        device_short(s->out, 0xB0 | c | 64 << 8);
    for (DWORD c = 0; c < 16; c++)
        for (DWORD k = 0; k < 128; k++)
            for (BYTE n = s->notes[c][k]; n; n--)
                device_short(s->out, 0x80 | c | k << 8 | 0x7F << 16);
}

static void pause(STREAM *s)
{
    if (s->paused)
        return;
    LONG64 tick = current_tick(s);
    s->played_ms += now_ms() - s->time0;
    s->tick0 = tick;
    s->paused = TRUE;
    send_notes_off(s);
}

static void play_short(STREAM *s, DWORD event)
{
    DWORD message = event & 0xFFFFFF;
    if (!(message & 0x80)) {
        message = s->status | (message << 8 & 0xFFFF00);
    }
    BYTE status = (BYTE)message;
    if (status >= 0x80)
        s->status = status;
    BYTE type = status & 0xF0, channel = status & 15, key = (BYTE)(message >> 8) & 0x7F, vel = (BYTE)(message >> 16);
    if (type == 0x90 && vel) {
        if (s->notes[channel][key] < 15)
            s->notes[channel][key]++;
    } else if (type == 0x80 || type == 0x90) {
        if (s->notes[channel][key])
            s->notes[channel][key]--;
    }
    device_short(s->out, message);
}

/*
 * plays every event whose time has come; returns the milliseconds until
 * the next, or INFINITE. finished headers are handed back through done.
 */
static DWORD play_due(STREAM *s, MIDIHDR **done, DWORD *done_count, MIDIHDR **position_cb, DWORD max)
{
    while (s->head && !s->paused) {
        MIDIHDR *h = s->head;
        if (s->offset + 12 > h->dwBytesRecorded) {
            s->head = h->lpNext;
            if (!s->head)
                s->tail = nullptr;
            s->offset = 0;
            s->delta_counted = FALSE;
            done[(*done_count)++] = h;
            if (*done_count == max)
                return 0;
            continue;
        }
        const BYTE *e = (const BYTE *)h->lpData + s->offset;
        DWORD delta = *(const DWORD *)e, event = *(const DWORD *)(e + 8);
        if (!s->delta_counted) {
            s->event_tick += delta;
            s->delta_counted = TRUE;
        }
        LONG64 now = current_tick(s);
        if (s->event_tick > now) {
            LONG64 num, den;
            rate(s, &num, &den);
            LONG64 ms = (s->event_tick - now) * den / (num ? num : 1);
            return ms < 1 ? 1 : ms > 1000 ? 1000 : (DWORD)ms;
        }
        BYTE type = (BYTE)(event >> 24) & ~0x40;
        DWORD size = 12 + (type & 0x80 ? (MEVT_EVENTPARM(event) + 3) & ~3u : 0);
        /* the position callback goes out before its event plays; the thread comes back for the event. */
        if ((event & MEVT_F_CALLBACK) && !s->called) {
            h->dwOffset = s->offset;
            *position_cb = h;
            s->called = TRUE;
            return 0;
        }
        s->offset += size;
        s->delta_counted = FALSE;
        s->called = FALSE;
        if (type == MEVT_SHORTMSG)
            play_short(s, event);
        else if (type == MEVT_TEMPO) {
            rebase(s, s->event_tick);
            s->tempo = MEVT_EVENTPARM(event);
        } else if (type == MEVT_LONGMSG)
            device_long(s->out, e + 12, MEVT_EVENTPARM(event));
    }
    return INFINITE;
}

static void complete(STREAM *s, MIDIHDR *h)
{
    h->dwFlags = (h->dwFlags & ~MHDR_INQUEUE) | MHDR_DONE;
    notify(s->out, MOM_DONE, (DWORD_PTR)h);
}

static DWORD WINAPI stream_thread(void *arg)
{
    STREAM *s = arg;
    DWORD wait = INFINITE;
    while (!s->quit) {
        WaitForSingleObject(s->wake, wait);
        MIDIHDR *done[16], *position = nullptr;
        DWORD count = 0;
        RtlEnterCriticalSection(&s->lock);
        wait = s->quit ? 0 : play_due(s, done, &count, &position, RTL_NUMBER_OF(done));
        RtlLeaveCriticalSection(&s->lock);
        if (position) {
            notify(s->out, MOM_POSITIONCB, (DWORD_PTR)position);
            wait = 0;
        }
        for (DWORD i = 0; i < count; i++)
            complete(s, done[i]);
    }
    return 0;
}

static STREAM *find_stream(HMIDISTRM h)
{
    MIDI_OUT *m = find(h, TRUE);
    return m ? m->stream : nullptr;
}

WINMMAPI MMRESULT WINAPI midiStreamOpen(LPHMIDISTRM out, LPUINT id, DWORD count, DWORD_PTR callback,
                                        DWORD_PTR instance, DWORD flags)
{
    if (!out)
        return MMSYSERR_INVALPARAM;
    if (callback && check_callback(flags, callback))
        return MMSYSERR_INVALPARAM;
    if (!id) {
        if (!count)
            *out = nullptr;
        return MMSYSERR_INVALPARAM;
    }
    *out = nullptr;
    if (count != 1)
        return MMSYSERR_INVALPARAM;
    if (*id != 0 && *id != MIDI_MAPPER) {
        *id = 0xFFFFFFFE;
        return MMSYSERR_BADDEVICEID;
    }
    MIDI_OUT *m = new_handle(*id, flags, callback, instance);
    STREAM *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!m || !s) {
        HeapFree(GetProcessHeap(), 0, m);
        HeapFree(GetProcessHeap(), 0, s);
        return MMSYSERR_NOMEM;
    }
    m->stream = s;
    s->out = m;
    s->paused = TRUE;
    s->division = 24;
    s->tempo = 500000;
    RtlInitializeCriticalSection(&s->lock);
    RtlEnterCriticalSection(&g_lock);
    MMRESULT r = open_device(*id, m);
    if (!r) {
        m->next = g_outs;
        g_outs = m;
    }
    RtlLeaveCriticalSection(&g_lock);
    if (!r) {
        s->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        s->thread = s->wake ? CreateThread(nullptr, 0, stream_thread, s, 0, nullptr) : nullptr;
        if (!s->thread) {
            unlink_handle(m);
            r = MMSYSERR_ERROR;
        }
    }
    if (r) {
        if (s->wake)
            CloseHandle(s->wake);
        RtlDeleteCriticalSection(&s->lock);
        HeapFree(GetProcessHeap(), 0, s);
        HeapFree(GetProcessHeap(), 0, m);
        *id = 0xFFFFFFFE;
        return r;
    }
    SetThreadPriority(s->thread, THREAD_PRIORITY_TIME_CRITICAL);
    *out = (HMIDISTRM)m;
    notify(m, MOM_OPEN, 0);
    return MMSYSERR_NOERROR;
}

/* every queued header goes back to the program, the clock to zero, and playing notes are ended. */
static MMRESULT stream_stop(STREAM *s, BOOL reset)
{
    RtlEnterCriticalSection(&s->lock);
    MIDIHDR *list = s->head;
    s->head = s->tail = nullptr;
    s->offset = 0;
    s->delta_counted = FALSE;
    s->called = FALSE;
    s->event_tick = 0;
    BOOL was_paused = s->paused;
    pause(s);
    if (was_paused)
        send_notes_off(s);
    s->tick0 = 0;
    s->played_ms = 0;
    RtlLeaveCriticalSection(&s->lock);
    while (list) {
        MIDIHDR *next = list->lpNext;
        complete(s, list);
        list = next;
    }
    if (reset)
        device_reset();
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI midiStreamClose(HMIDISTRM h)
{
    STREAM *s = find_stream(h);
    if (!s)
        return MMSYSERR_INVALHANDLE;
    stream_stop(s, FALSE);
    s->quit = TRUE;
    SetEvent(s->wake);
    WaitForSingleObject(s->thread, INFINITE);
    CloseHandle(s->thread);
    CloseHandle(s->wake);
    MIDI_OUT *m = s->out;
    RtlEnterCriticalSection(&s->lock);
    send_notes_off(s);
    RtlLeaveCriticalSection(&s->lock);
    unlink_handle(m);
    notify(m, MOM_CLOSE, 0);
    RtlDeleteCriticalSection(&s->lock);
    HeapFree(GetProcessHeap(), 0, s);
    HeapFree(GetProcessHeap(), 0, m);
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI midiStreamOut(HMIDISTRM hs, LPMIDIHDR h, UINT size)
{
    STREAM *s = find_stream(hs);
    if (!s)
        return MMSYSERR_INVALHANDLE;
    if (check_header(h, size) || (h->dwFlags & ~HEADER_FLAGS))
        return MMSYSERR_INVALPARAM;
    if (!(h->dwFlags & MHDR_PREPARED))
        return MIDIERR_UNPREPARED;
    if (h->dwFlags & MHDR_INQUEUE)
        return MIDIERR_STILLPLAYING;
    if (h->dwBytesRecorded > h->dwBufferLength || (h->dwBytesRecorded & 3) || h->dwBufferLength > 0xFFFF ||
        (h->dwBufferLength & 3))
        return MMSYSERR_INVALPARAM;
    /* the whole buffer is checked before any of it plays. */
    for (DWORD at = 0; at < h->dwBytesRecorded;) {
        if (h->dwBytesRecorded - at < 12)
            return MMSYSERR_INVALPARAM;
        DWORD event = *(const DWORD *)(h->lpData + at + 8), length = 12;
        if (event & MEVT_F_LONG) {
            length += (MEVT_EVENTPARM(event) + 3) & ~3u;
            if (length > h->dwBytesRecorded - at)
                return MMSYSERR_INVALPARAM;
        }
        at += length;
    }
    h->dwFlags = (h->dwFlags & ~MHDR_DONE) | MHDR_ISSTRM | MHDR_INQUEUE;
    h->lpNext = nullptr;
    RtlEnterCriticalSection(&s->lock);
    if (s->tail)
        s->tail->lpNext = h;
    else
        s->head = h;
    s->tail = h;
    RtlLeaveCriticalSection(&s->lock);
    SetEvent(s->wake);
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI midiStreamPause(HMIDISTRM h)
{
    STREAM *s = find_stream(h);
    if (!s)
        return MMSYSERR_INVALHANDLE;
    RtlEnterCriticalSection(&s->lock);
    pause(s);
    RtlLeaveCriticalSection(&s->lock);
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI midiStreamRestart(HMIDISTRM h)
{
    STREAM *s = find_stream(h);
    if (!s)
        return MMSYSERR_INVALHANDLE;
    RtlEnterCriticalSection(&s->lock);
    if (s->paused) {
        s->paused = FALSE;
        s->time0 = now_ms();
    }
    RtlLeaveCriticalSection(&s->lock);
    SetEvent(s->wake);
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI midiStreamStop(HMIDISTRM h)
{
    STREAM *s = find_stream(h);
    if (!s)
        return MMSYSERR_INVALHANDLE;
    return stream_stop(s, FALSE);
}

WINMMAPI MMRESULT WINAPI midiStreamProperty(HMIDISTRM h, LPBYTE data, DWORD property)
{
    STREAM *s = find_stream(h);
    if (!s)
        return MMSYSERR_INVALHANDLE;
    if (!(property & (MIDIPROP_SET | MIDIPROP_GET)) || !data || *(const DWORD *)data < 8)
        return MMSYSERR_INVALPARAM;
    DWORD *value = (DWORD *)(data + 4), which = property & ~(MIDIPROP_SET | MIDIPROP_GET);
    BOOL get = property & MIDIPROP_GET;
    MMRESULT r = MMSYSERR_NOERROR;
    RtlEnterCriticalSection(&s->lock);
    if (which == MIDIPROP_TIMEDIV) {
        if (get) {
            *value = s->division;
        } else if (!s->paused) {
            r = MMSYSERR_INVALPARAM;
        } else {
            s->division = *value;
        }
    } else if (which == MIDIPROP_TEMPO) {
        if (get) {
            *value = s->tempo;
        } else {
            rebase(s, current_tick(s));
            s->tempo = *value;
        }
    } else {
        r = MMSYSERR_INVALPARAM;
    }
    RtlLeaveCriticalSection(&s->lock);
    return r;
}

WINMMAPI MMRESULT WINAPI midiStreamPosition(HMIDISTRM h, LPMMTIME time, UINT size)
{
    STREAM *s = find_stream(h);
    if (!s)
        return MMSYSERR_INVALHANDLE;
    if (!time)
        return MMSYSERR_INVALPARAM;
    RtlEnterCriticalSection(&s->lock);
    LONG64 tick = current_tick(s);
    if (tick < 0)
        tick = 0;
    DWORD ms = s->played_ms + (s->paused ? 0 : now_ms() - s->time0);
    BOOL smpte = s->division & 0x8000;
    RtlLeaveCriticalSection(&s->lock);
    if ((time->wType == TIME_MIDI && smpte) || (time->wType == TIME_SMPTE && !smpte) ||
        (time->wType != TIME_TICKS && time->wType != TIME_MIDI && time->wType != TIME_SMPTE))
        time->wType = TIME_MS;
    switch (time->wType) {
    case TIME_MS:
        time->u.ms = ms;
        break;
    case TIME_TICKS:
        time->u.ticks = (DWORD)tick;
        break;
    case TIME_MIDI:
        time->u.midi.songptrpos = (DWORD)(tick * 4 / ((s->division & 0x7FFF) ? (s->division & 0x7FFF) : 1));
        break;
    case TIME_SMPTE: {
        int fps = -(signed char)(s->division >> 8);
        DWORD t = (DWORD)tick, per_second = fps == 24 || fps == 25 || fps == 29 ? (DWORD)fps : 30;
        if (fps == 29 && t % 17982 >= 2)
            t += 2 * (9 * (t / 17982) + ((t % 17982) - 2) / 1798);
        time->u.smpte.fps = (BYTE)per_second;
        time->u.smpte.frame = (BYTE)(t % per_second);
        t /= per_second;
        time->u.smpte.sec = (BYTE)(t % 60);
        time->u.smpte.min = (BYTE)(t / 60 % 60);
        time->u.smpte.hour = (BYTE)(t / 3600);
        break;
    }
    }
    return MMSYSERR_NOERROR;
}

/* ------------------------------------------------------------------ input, of which there is none */

WINMMAPI UINT WINAPI midiInGetNumDevs(void)
{
    return 0;
}

WINMMAPI MMRESULT WINAPI midiInGetDevCapsA(UINT_PTR id, LPMIDIINCAPSA caps, UINT size)
{
    if (!size)
        return MMSYSERR_NOERROR;
    return caps ? MMSYSERR_BADDEVICEID : MMSYSERR_INVALPARAM;
}

WINMMAPI MMRESULT WINAPI midiInGetDevCapsW(UINT_PTR id, LPMIDIINCAPSW caps, UINT size)
{
    if (!size)
        return MMSYSERR_NOERROR;
    return caps ? MMSYSERR_BADDEVICEID : MMSYSERR_INVALPARAM;
}

WINMMAPI MMRESULT WINAPI midiInGetErrorTextA(MMRESULT error, LPSTR text, UINT size)
{
    return MmErrorTextA(error, text, size);
}

WINMMAPI MMRESULT WINAPI midiInGetErrorTextW(MMRESULT error, LPWSTR text, UINT size)
{
    return MmErrorTextW(error, text, size);
}

WINMMAPI MMRESULT WINAPI midiInOpen(LPHMIDIIN out, UINT id, DWORD_PTR callback, DWORD_PTR instance, DWORD flags)
{
    if (!out)
        return MMSYSERR_INVALPARAM;
    *out = nullptr;
    return MMSYSERR_BADDEVICEID;
}

WINMMAPI MMRESULT WINAPI midiInClose(HMIDIIN h)
{
    return MMSYSERR_INVALHANDLE;
}

WINMMAPI MMRESULT WINAPI midiInPrepareHeader(HMIDIIN h, LPMIDIHDR header, UINT size)
{
    return MMSYSERR_INVALHANDLE;
}

WINMMAPI MMRESULT WINAPI midiInUnprepareHeader(HMIDIIN h, LPMIDIHDR header, UINT size)
{
    return MMSYSERR_INVALHANDLE;
}

WINMMAPI MMRESULT WINAPI midiInAddBuffer(HMIDIIN h, LPMIDIHDR header, UINT size)
{
    return MMSYSERR_INVALHANDLE;
}

WINMMAPI MMRESULT WINAPI midiInStart(HMIDIIN h)
{
    return MMSYSERR_INVALHANDLE;
}

WINMMAPI MMRESULT WINAPI midiInStop(HMIDIIN h)
{
    return MMSYSERR_INVALHANDLE;
}

WINMMAPI MMRESULT WINAPI midiInReset(HMIDIIN h)
{
    return MMSYSERR_INVALHANDLE;
}

WINMMAPI MMRESULT WINAPI midiInGetID(HMIDIIN h, LPUINT id)
{
    return id ? MMSYSERR_INVALHANDLE : MMSYSERR_INVALPARAM;
}

WINMMAPI MMRESULT WINAPI midiInMessage(HMIDIIN h, UINT message, DWORD_PTR param1, DWORD_PTR param2)
{
    return MMSYSERR_BADDEVICEID;
}

WINMMAPI MMRESULT WINAPI midiConnect(HMIDI h, HMIDIOUT out, LPVOID reserved)
{
    return MMSYSERR_INVALHANDLE;
}

WINMMAPI MMRESULT WINAPI midiDisconnect(HMIDI h, HMIDIOUT out, LPVOID reserved)
{
    return MMSYSERR_INVALHANDLE;
}
