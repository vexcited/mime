/*
 * the engine: one thread mixing every playing buffer of the process into
 * a 48 khz stereo float stream for the host, in passes of 10 ms as
 * windows' audio engine runs. a pass advances each buffer's write cursor,
 * the play cursor trails it, and the notifications its play cursor
 * passed are signalled. without a host stream the passes follow the
 * clock, so cursors and notifications behave the same.
 */
#include "dsound_private.h"

static MimeOutput output;
static BOOL output_open, output_failed;
static HANDLE wake, thread;
static float mix[MIX_PERIOD * MIX_CHANNELS];
/* the clock of passes without a host stream. */
static LARGE_INTEGER clock_start, clock_rate;
static ULONG64 clock_passes;
static BOOL running;
static DWORD focus_passes;
static DWORD focus_thread = ~0u;

/* ------------------------------------------------------------------ samples */

static inline float load_u8(const BYTE *p, UINT c)
{
    return (float)((int)p[c] - 128) * (1.0f / 128);
}

static inline float load_s16(const BYTE *p, UINT c)
{
    INT16 v;
    memcpy(&v, p + 2 * c, 2);
    return (float)v * (1.0f / 32768);
}

static inline float load_s24(const BYTE *p, UINT c)
{
    p += 3 * c;
    INT32 v = (INT32)((UINT32)p[0] << 8 | (UINT32)p[1] << 16 | (UINT32)p[2] << 24);
    return (float)v * (1.0f / 2147483648.0f);
}

static inline float load_s32(const BYTE *p, UINT c)
{
    INT32 v;
    memcpy(&v, p + 4 * c, 4);
    return (float)v * (1.0f / 2147483648.0f);
}

static inline float load_f32(const BYTE *p, UINT c)
{
    float v;
    memcpy(&v, p + 4 * c, 4);
    return v;
}

/*
 * reads frames from the cursor at the buffer's rate, linearly between
 * neighbours, into the pass. returns the frames made, fewer when a buffer
 * that does not loop reaches its end.
 */
#define MIX_LOOP(load)                                                                                                \
    for (; n < MIX_PERIOD; n++) {                                                                                     \
        if (index >= frames)                                                                                          \
            break;                                                                                                    \
        UINT32 next = index + 1 < frames ? index + 1 : looping ? 0 : index;                                           \
        const BYTE *a = data + (SIZE_T)index * align, *z = data + (SIZE_T)next * align;                               \
        float t = (float)(UINT32)fraction * (1.0f / 4294967296.0f);                                                   \
        float l = 0, r = 0;                                                                                           \
        for (UINT c = 0; c < channels; c++) {                                                                         \
            float s0 = load(a, c);                                                                                    \
            float s = s0 + (load(z, c) - s0) * t;                                                                     \
            l += s * g[c][0];                                                                                         \
            r += s * g[c][1];                                                                                         \
        }                                                                                                             \
        mix[2 * n] += l;                                                                                              \
        mix[2 * n + 1] += r;                                                                                          \
        fraction += step;                                                                                             \
        index += (UINT32)(fraction >> 32);                                                                            \
        fraction &= 0xFFFFFFFF;                                                                                       \
        if (looping)                                                                                                  \
            while (index >= frames)                                                                                   \
                index -= frames;                                                                                      \
    }

/* mixes one pass of a buffer; false once a buffer that does not loop ran out. */
static BOOL mix_buffer(DSBUFFER *b, const float device[2])
{
    const WAVEFORMATEX *f = b->format;
    UINT align = f->nBlockAlign;
    UINT32 frames = b->size / align;
    if (!frames)
        return FALSE;
    UINT channels = f->nChannels < MAX_CHANNELS ? f->nChannels : MAX_CHANNELS;
    float g[MAX_CHANNELS][2];
    float mute = b->muted ? 0.0f : 1.0f;
    for (UINT c = 0; c < channels; c++) {
        g[c][0] = b->gain[c][0] * device[0] * mute;
        g[c][1] = b->gain[c][1] * device[1] * mute;
    }
    const BYTE *data = b->data;
    BOOL looping = b->looping;
    UINT64 step = ((UINT64)b->rate << 32) / MIX_RATE;
    UINT64 fraction = b->fraction;
    UINT32 index = b->position / align;
    UINT n = 0;
    BOOL is_float = DsFormatTag(f) == WAVE_FORMAT_IEEE_FLOAT;
    switch (f->wBitsPerSample) {
    case 8:
        MIX_LOOP(load_u8)
        break;
    case 16:
        MIX_LOOP(load_s16)
        break;
    case 24:
        MIX_LOOP(load_s24)
        break;
    default:
        if (is_float) {
            MIX_LOOP(load_f32)
        } else {
            MIX_LOOP(load_s32)
        }
        break;
    }
    if (index >= frames)
        return FALSE;
    b->position = index * align;
    b->fraction = fraction;
    return TRUE;
}

/* ------------------------------------------------------------------ cursors */

static DWORD lead(DSBUFFER *b)
{
    if (!(b->flags & DSBCAPS_TRUEPLAYPOSITION))
        return b->lead;
    /* what the host has queued, in the buffer's frames. */
    UINT64 queued = output_open ? MimeOutputQueued(&output) : (UINT64)MIX_PERIOD * MIX_AHEAD;
    DWORD align = b->format->nBlockAlign;
    UINT64 bytes = queued * b->rate / MIX_RATE * align;
    DWORD most = b->size - align;
    return bytes > most ? most : (DWORD)bytes;
}

/*
 * windows' cursors: the write cursor is where the engine reads next, the
 * play cursor trails it by the latency and never goes back, both aligned
 * to frames. a stopped buffer has both at its position.
 */
void DsMixerCursors(DSBUFFER *b, DWORD *play, DWORD *write)
{
    if (!b->playing) {
        *play = *write = b->position;
        return;
    }
    DWORD size = b->size, position = b->position, last = b->last_play;
    DWORD candidate = (position + size - lead(b) % size) % size;
    DWORD align = b->format->nBlockAlign;
    candidate -= candidate % align;
    DWORD ahead = (candidate + size - last) % size, written = (position + size - last) % size;
    if (ahead < written)
        last = candidate;
    b->last_play = last;
    *play = last;
    *write = position;
}

/* signals the notifications in [from, to) of the buffer, going round its end. */
static void signal_range(DSBUFFER *b, DWORD from, DWORD to)
{
    for (DWORD i = 0; i < b->notify_count; i++) {
        DWORD offset = b->notifies[i].offset;
        if (offset == DSBPN_OFFSETSTOP)
            continue;
        BOOL inside = from <= to ? offset >= from && offset < to : offset >= from || offset < to;
        if (inside)
            SetEvent(b->notifies[i].event);
    }
}

static void signal_stop(DSBUFFER *b)
{
    if (b->notify_count && b->notifies[b->notify_count - 1].offset == DSBPN_OFFSETSTOP)
        SetEvent(b->notifies[b->notify_count - 1].event);
}

/* the buffer stops where its engine read last; DSBPN_OFFSETSTOP is signalled. */
void DsMixerStop(DSBUFFER *b)
{
    if (!b->playing)
        return;
    b->playing = FALSE;
    b->fraction = 0;
    b->last_play = b->notify_play = b->position;
    signal_stop(b);
}

void DsMixerRemove(DSBUFFER *b)
{
    b->playing = FALSE;
}

void DsMixerAdd(DSBUFFER *b)
{
    (void)b;
    if (wake)
        SetEvent(wake);
}

/* ------------------------------------------------------------------ focus */

/* the foreground window's thread, none for a minimized one. */
static DWORD foreground_thread(void)
{
    HWND window = GetForegroundWindow();
    if (!window)
        return 0;
    HWND root = GetAncestor(window, GA_ROOT);
    if (root)
        window = root;
    if (!IsWindow(window) || IsIconic(window))
        return 0;
    return GetWindowThreadProcessId(window, nullptr);
}

enum { FOCUS_ACTIVE, FOCUS_MUTED, FOCUS_LOST };

/*
 * windows' GetBufferFocusState. what windows learns of other processes'
 * levels from its shared thread list is known here for this process only.
 */
static int focus_state(DSBUFFER *b, DWORD foreground, DWORD foreground_level)
{
    DSDEVICE *d = b->device;
    DWORD flags = b->flags;
    if (foreground_level == DSSCL_WRITEPRIMARY) {
        if (!b->primary || foreground != d->thread)
            return FOCUS_LOST;
    } else if (d->level == DSSCL_WRITEPRIMARY) {
        if (!b->primary || foreground_level || !(flags & DSBCAPS_STICKYFOCUS))
            return FOCUS_LOST;
    }
    if (flags & DSBCAPS_GLOBALFOCUS)
        return FOCUS_ACTIVE;
    if (flags & DSBCAPS_STICKYFOCUS)
        return d->thread != foreground && foreground_level ? FOCUS_MUTED : FOCUS_ACTIVE;
    return d->thread != foreground ? FOCUS_MUTED : FOCUS_ACTIVE;
}

static void apply_focus(DSBUFFER *b, DWORD foreground, DWORD level, BOOL changed)
{
    int state = focus_state(b, foreground, level);
    b->muted = state != FOCUS_ACTIVE;
    if (state == FOCUS_LOST && changed && !b->lost) {
        DsMixerStop(b);
        b->lost = TRUE;
    }
}

static void update_focus(BOOL force)
{
    DWORD foreground = foreground_thread();
    BOOL changed = force || foreground != focus_thread;
    focus_thread = foreground;
    DWORD level = 0;
    for (DSDEVICE *d = DsDevices; d; d = d->next)
        if (d->initialized && d->thread && d->thread == foreground && d->level > level)
            level = d->level;
    for (DSDEVICE *d = DsDevices; d; d = d->next) {
        if (!d->initialized)
            continue;
        for (DSBUFFER *b = d->buffers; b; b = b->next)
            apply_focus(b, foreground, level, changed);
        if (d->primary)
            apply_focus(d->primary, foreground, level, changed);
    }
}

/* a lost buffer is restored only while its focus would not lose it again. */
void DsUpdateFocus(void)
{
    RtlEnterCriticalSection(&DsLock);
    update_focus(TRUE);
    DWORD foreground = focus_thread, level = 0;
    for (DSDEVICE *d = DsDevices; d; d = d->next)
        if (d->initialized && d->thread && d->thread == foreground && d->level > level)
            level = d->level;
    for (DSDEVICE *d = DsDevices; d; d = d->next) {
        if (!d->initialized)
            continue;
        for (DSBUFFER *b = d->buffers; b; b = b->next)
            if (b->lost && focus_state(b, foreground, level) != FOCUS_LOST)
                b->lost = FALSE;
        if (d->primary && d->primary->lost && focus_state(d->primary, foreground, level) != FOCUS_LOST)
            d->primary->lost = FALSE;
    }
    RtlLeaveCriticalSection(&DsLock);
}

/* ------------------------------------------------------------------ passes */

static void advance(DSBUFFER *b, const float device[2])
{
    BOOL more = mix_buffer(b, device);
    if (more) {
        /* the play cursor follows every pass, so it stays monotonic however rarely a program reads it. */
        DWORD play, write;
        DsMixerCursors(b, &play, &write);
        if (b->notify_count && play != b->notify_play)
            signal_range(b, b->notify_play, play);
        b->notify_play = play;
        return;
    }
    /* a buffer that does not loop ran out: what is left to play counts as played, and it rewinds. */
    if (b->notify_count && b->notify_play < b->size)
        signal_range(b, b->notify_play, b->size);
    b->playing = FALSE;
    b->position = 0;
    b->fraction = 0;
    b->last_play = b->notify_play = 0;
    signal_stop(b);
}

static BOOL any_playing(void)
{
    for (DSDEVICE *d = DsDevices; d; d = d->next) {
        if (!d->initialized)
            continue;
        if (d->level == DSSCL_WRITEPRIMARY) {
            if (d->primary && d->primary->playing)
                return TRUE;
            continue;
        }
        for (DSBUFFER *b = d->buffers; b; b = b->next)
            if (b->playing)
                return TRUE;
    }
    return FALSE;
}

/* at DSSCL_WRITEPRIMARY a device plays its primary buffer and nothing else. */
static void pass(void)
{
    memset(mix, 0, sizeof(mix));
    for (DSDEVICE *d = DsDevices; d; d = d->next) {
        if (!d->initialized)
            continue;
        float device[2];
        DsPrimaryGains(d, device);
        if (d->level == DSSCL_WRITEPRIMARY) {
            if (d->primary && d->primary->playing)
                advance(d->primary, device);
            continue;
        }
        for (DSBUFFER *b = d->buffers; b; b = b->next)
            if (b->playing)
                advance(b, device);
    }
}

static void write_pass(void)
{
    UINT32 done = 0;
    while (done < MIX_PERIOD) {
        UINT32 room;
        BYTE *to = MimeOutputBegin(&output, &room);
        if (!room)
            break;
        if (room > MIX_PERIOD - done)
            room = MIX_PERIOD - done;
        memcpy(to, mix + done * MIX_CHANNELS, room * MIX_CHANNELS * sizeof(float));
        MimeOutputCommit(&output, room);
        done += room;
    }
}

static UINT32 passes_due(void)
{
    if (output_open) {
        UINT32 queued = MimeOutputQueued(&output), target = MIX_PERIOD * MIX_AHEAD;
        return queued < target ? (target - queued + MIX_PERIOD - 1) / MIX_PERIOD : 0;
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    ULONG64 elapsed = (ULONG64)(now.QuadPart - clock_start.QuadPart);
    ULONG64 due = elapsed * (MIX_RATE / MIX_PERIOD) / (ULONG64)clock_rate.QuadPart + MIX_AHEAD;
    if (due <= clock_passes)
        return 0;
    if (due - clock_passes > MIX_AHEAD)
        clock_passes = due - MIX_AHEAD;
    return (UINT32)(due - clock_passes);
}

static void begin(void)
{
    if (!output_open && !output_failed) {
        if (NT_SUCCESS(MimeOutputOpen(&output, MIX_RATE, MIX_CHANNELS, 32, TRUE)))
            output_open = TRUE;
        else
            output_failed = TRUE;
    }
    QueryPerformanceCounter(&clock_start);
    clock_passes = 0;
    running = TRUE;
    /* focus was not followed while nothing played. */
    update_focus(FALSE);
}

static DWORD WINAPI engine(void *arg)
{
    (void)arg;
    for (;;) {
        RtlEnterCriticalSection(&DsLock);
        if (++focus_passes >= 10) {
            focus_passes = 0;
            update_focus(FALSE);
        }
        if (!any_playing()) {
            /* what is queued plays out before the stream stops. */
            BOOL draining = output_open && output.started && MimeOutputQueued(&output);
            if (output_open && output.started && !draining)
                MimeOutputStop(&output);
            running = FALSE;
            RtlLeaveCriticalSection(&DsLock);
            WaitForSingleObject(wake, draining ? 5 : INFINITE);
            continue;
        }
        if (!running)
            begin();
        for (UINT32 due = passes_due(); due; due--) {
            pass();
            clock_passes++;
            if (output_open)
                write_pass();
        }
        if (output_open && !output.started && MimeOutputQueued(&output))
            MimeOutputStart(&output);
        RtlLeaveCriticalSection(&DsLock);
        WaitForSingleObject(wake, 3);
    }
    return 0;
}

void DsMixerStart(void)
{
    if (thread)
        return;
    QueryPerformanceFrequency(&clock_rate);
    wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    thread = CreateThread(nullptr, 0, engine, nullptr, 0, nullptr);
    if (thread)
        SetThreadPriority(thread, THREAD_PRIORITY_TIME_CRITICAL);
}
