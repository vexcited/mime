/*
 * mciseq.dll, the sequencer device: standard midi files of format 0 and 1,
 * and rmid files, played on a midi output port it opens at play and closes
 * at stop. it keeps windows' driver's ways: tempo only from the first
 * track, song pointer positions by default, files rejected unless every
 * track ends with its end of track event, and pause that closes the port.
 */
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/mmsystem.h>

#define MODE_STOPPED 0
#define MODE_PLAYING 1
#define MODE_PAUSED 2
#define MODE_SEEKING 3
#define MAX_TRACKS 100

typedef struct TRACK {
    const BYTE *start, *end, *p;
    DWORD tick;      /* of the next event */
    BYTE status;
    BOOL ended;
} TRACK;

typedef struct SEGMENT {
    DWORD tick, ms, us; /* where a tempo starts, and its microseconds a tick */
} SEGMENT;

typedef struct SEQ {
    MCIDEVICEID id;
    RTL_CRITICAL_SECTION lock;
    WCHAR file[MAX_PATH];
    BYTE *image;
    DWORD image_size;
    TRACK tracks[MAX_TRACKS];
    UINT count;
    BOOL smpte;
    DWORD ppqn, fps, tpf;
    SEGMENT *map;
    UINT segments;
    DWORD length;    /* in ticks */
    DWORD offset;    /* smpte offset of the file, packed */
    DWORD time_format;
    UINT port;
    /* playing */
    HMIDIOUT out;
    HANDLE thread, event;
    volatile BOOL quit;
    int mode;
    DWORD position, to;
    DWORD tick0, time0, us; /* the clock while playing */
    DWORD forced_us;        /* a tempo set by command, which the file's no longer changes */
    BOOL sounding[16][128];
    HWND callback;
    BOOL pending;
} SEQ;

/* ------------------------------------------------------------------ the file */

static DWORD be32(const BYTE *p)
{
    return (DWORD)p[0] << 24 | (DWORD)p[1] << 16 | (DWORD)p[2] << 8 | p[3];
}

static BOOL varlen(const BYTE **p, const BYTE *end, DWORD *out)
{
    DWORD v = 0;
    for (int i = 0; i < 4; i++) {
        if (*p >= end)
            return FALSE;
        BYTE b = *(*p)++;
        v = v << 7 | (b & 0x7F);
        if (!(b & 0x80)) {
            *out = v;
            return TRUE;
        }
    }
    return FALSE;
}

static int data_bytes(BYTE status)
{
    BYTE type = status & 0xF0;
    return type == 0xC0 || type == 0xD0 ? 1 : 2;
}

static BOOL add_segment(SEQ *s, DWORD tick, DWORD tempo)
{
    if (!(s->segments & 15)) {
        SEGMENT *grown = s->map ? HeapReAlloc(GetProcessHeap(), 0, s->map, (s->segments + 16) * sizeof(SEGMENT))
                                : HeapAlloc(GetProcessHeap(), 0, 16 * sizeof(SEGMENT));
        if (!grown)
            return FALSE;
        s->map = grown;
    }
    DWORD ms = 0;
    if (s->segments) {
        const SEGMENT *last = &s->map[s->segments - 1];
        ms = last->ms + MulDiv(tick - last->tick, last->us, 1000);
        if (last->tick == tick) {
            s->segments--;
            ms = last->ms;
        }
    }
    DWORD us = tempo / (s->ppqn ? s->ppqn : 1);
    s->map[s->segments++] = (SEGMENT){tick, ms, us ? us : 1};
    return TRUE;
}

/*
 * walks a track once: its length in ticks, its tempo changes when it is
 * the first, and whether it holds a status windows' driver refuses.
 */
static BOOL scan(SEQ *s, const TRACK *t, BOOL first)
{
    const BYTE *p = t->start, *end = t->end;
    DWORD tick = 0;
    BYTE status = 0;
    while (p < end) {
        DWORD delta;
        if (!varlen(&p, end, &delta))
            return FALSE;
        tick += delta;
        if (p >= end)
            return FALSE;
        BYTE b = *p;
        if (b >= 0x80) {
            status = b;
            p++;
        } else if (!status) {
            return FALSE;
        }
        if (status == 0xFF) {
            if (p >= end)
                return FALSE;
            BYTE type = *p++;
            DWORD n;
            if (!varlen(&p, end, &n) || n > (DWORD)(end - p))
                return FALSE;
            if (first && type == 0x51 && n >= 3 && !s->smpte && !add_segment(s, tick, p[0] << 16 | p[1] << 8 | p[2]))
                return FALSE;
            if (first && type == 0x54 && n >= 4)
                s->offset = p[0] | p[1] << 8 | p[2] << 16 | (DWORD)p[3] << 24;
            p += n;
            status = 0;
            if (type == 0x2F)
                break;
        } else if (status == 0xF0 || status == 0xF7) {
            DWORD n;
            if (!varlen(&p, end, &n) || n > (DWORD)(end - p))
                return FALSE;
            p += n;
            status = 0;
        } else if (status > 0xF0) {
            return FALSE;
        } else {
            p += data_bytes(status);
            if (p > end)
                return FALSE;
        }
    }
    if (tick > s->length)
        s->length = tick;
    return TRUE;
}

static MCIERROR parse(SEQ *s)
{
    const BYTE *p = s->image, *end = p + s->image_size;
    /* an rmid file holds the midi file in its data chunk. */
    if (end - p >= 20 && !memcmp(p, "RIFF", 4) && !memcmp(p + 8, "RMID", 4)) {
        const BYTE *c = p + 12;
        while (end - c >= 8) {
            DWORD size = c[4] | c[5] << 8 | c[6] << 16 | (DWORD)c[7] << 24;
            if (!memcmp(c, "data", 4)) {
                p = c + 8;
                if (size < (DWORD)(end - p))
                    end = p + size;
                break;
            }
            if (size > (DWORD)(end - c - 8))
                break;
            c += 8 + size + (size & 1);
        }
    }
    while (end - p >= 8 && memcmp(p, "MThd", 4))
        p++;
    if (end - p < 14 || be32(p + 4) < 6)
        return MCIERR_INVALID_FILE;
    DWORD header = be32(p + 4);
    WORD format = (WORD)(p[8] << 8 | p[9]), tracks = (WORD)(p[10] << 8 | p[11]), division = (WORD)(p[12] << 8 | p[13]);
    if (format > 1 || (format == 0 && tracks > 1) || tracks > MAX_TRACKS)
        return MCIERR_INVALID_FILE;
    if (division & 0x8000) {
        s->smpte = TRUE;
        s->fps = (DWORD)-(signed char)(division >> 8);
        s->tpf = division & 0xFF;
        if (s->tpf < 1)
            s->tpf = 1;
    } else {
        s->ppqn = division ? division : 1;
    }
    if (header > (DWORD)(end - p - 8))
        return MCIERR_INVALID_FILE;
    p += 8 + header;
    if (!s->smpte && !add_segment(s, 0, 500000))
        return MCIERR_OUT_OF_MEMORY;
    for (UINT i = 0; i < tracks; i++) {
        while (end - p >= 8 && memcmp(p, "MTrk", 4)) {
            DWORD size = be32(p + 4);
            if (size > (DWORD)(end - p - 8))
                return MCIERR_INVALID_FILE;
            p += 8 + size;
        }
        if (end - p < 8)
            return MCIERR_INVALID_FILE;
        DWORD size = be32(p + 4);
        if (size > (DWORD)(end - p - 8) || size < 3)
            return MCIERR_INVALID_FILE;
        TRACK *t = &s->tracks[s->count++];
        t->start = p + 8;
        t->end = p + 8 + size;
        /* every track must end in its end of track event. */
        if (memcmp(t->end - 3, "\xFF\x2F\x00", 3))
            return MCIERR_INVALID_FILE;
        if (!scan(s, t, i == 0))
            return MCIERR_INVALID_FILE;
        p = t->end;
    }
    return MMSYSERR_NOERROR;
}

/* ------------------------------------------------------------------ time */

static const SEGMENT *segment_of_tick(const SEQ *s, DWORD tick)
{
    UINT i = 0;
    while (i + 1 < s->segments && s->map[i + 1].tick <= tick)
        i++;
    return &s->map[i];
}

static DWORD tick_to_ms(const SEQ *s, DWORD tick)
{
    if (s->smpte) {
        DWORD fps = s->fps == 29 ? 30 : s->fps;
        return (DWORD)((1000ull * tick + fps / 2) / (fps * s->tpf));
    }
    const SEGMENT *g = segment_of_tick(s, tick);
    return g->ms + MulDiv(tick - g->tick, g->us, 1000);
}

static DWORD ms_to_tick(const SEQ *s, DWORD ms)
{
    if (s->smpte) {
        DWORD fps = s->fps == 29 ? 30 : s->fps;
        return (DWORD)(((ULONG64)fps * ms * s->tpf + 500) / 1000);
    }
    UINT i = 0;
    while (i + 1 < s->segments && s->map[i + 1].ms <= ms)
        i++;
    return s->map[i].tick + MulDiv(ms - s->map[i].ms, 1000, s->map[i].us);
}

static DWORD format_fps(DWORD format)
{
    return format == MCI_FORMAT_SMPTE_24 ? 24 : format == MCI_FORMAT_SMPTE_25 ? 25 : 30;
}

static BOOL is_smpte(DWORD format)
{
    return format >= MCI_FORMAT_SMPTE_24 && format <= MCI_FORMAT_SMPTE_30DROP;
}

static DWORD pack(DWORD frames, DWORD fps)
{
    DWORD ff = frames % fps, t = frames / fps;
    return (t / 3600) | (t / 60 % 60) << 8 | (t % 60) << 16 | ff << 24;
}

static DWORD to_format(const SEQ *s, DWORD tick)
{
    if (s->time_format == MCI_SEQ_FORMAT_SONGPTR)
        return tick * 4 / s->ppqn;
    if (is_smpte(s->time_format)) {
        DWORD fps = format_fps(s->time_format);
        DWORD frames = s->smpte ? tick / s->tpf : (DWORD)((ULONG64)tick_to_ms(s, tick) * fps / 1000);
        return pack(frames, fps);
    }
    return tick_to_ms(s, tick);
}

static DWORD from_format(const SEQ *s, DWORD value)
{
    if (s->time_format == MCI_SEQ_FORMAT_SONGPTR)
        return value * s->ppqn / 4;
    if (is_smpte(s->time_format)) {
        DWORD fps = format_fps(s->time_format);
        DWORD hh = value & 0xFF, mm = (value >> 8) & 0xFF, ss = (value >> 16) & 0xFF, ff = value >> 24;
        DWORD frames = ff + fps * (ss + 60 * (mm + 60 * hh));
        return s->smpte ? s->tpf * frames : ms_to_tick(s, (DWORD)((ULONG64)frames * 1000 / fps));
    }
    return ms_to_tick(s, value);
}

/* a position in the current format no further than the end. */
static BOOL in_range(const SEQ *s, DWORD value)
{
    if (is_smpte(s->time_format)) {
        DWORD hh = value & 0xFF, mm = (value >> 8) & 0xFF, ss = (value >> 16) & 0xFF, ff = value >> 24;
        if (hh > 24 || mm >= 60 || ss >= 60 || ff >= format_fps(s->time_format))
            return FALSE;
        return from_format(s, value) <= s->length;
    }
    return value <= to_format(s, s->length);
}

/* ------------------------------------------------------------------ the port */

static MCIERROR open_port(SEQ *s)
{
    if (s->out || s->port == MCI_SEQ_NONE)
        return MMSYSERR_NOERROR;
    if (!midiOutGetNumDevs())
        return MCIERR_SEQ_NOMIDIPRESENT;
    MMRESULT r = midiOutOpen(&s->out, s->port == MCI_SEQ_MAPPER ? MIDI_MAPPER : s->port, 0, 0, 0);
    switch (r) {
    case MMSYSERR_NOERROR:
        return MMSYSERR_NOERROR;
    case MMSYSERR_BADDEVICEID:
        return MCIERR_SEQ_PORT_NONEXISTENT;
    case MMSYSERR_ALLOCATED:
        return MCIERR_SEQ_PORT_INUSE;
    case MIDIERR_NODEVICE:
        return MIDIERR_NODEVICE;
    default:
        return MCIERR_SEQ_NOMIDIPRESENT;
    }
}

/* the sustain pedal off and a note off for each note still sounding, then the port closed. */
static void close_port(SEQ *s)
{
    if (!s->out)
        return;
    for (DWORD c = 0; c < 16; c++) {
        midiOutShortMsg(s->out, 0xB0 | c | 64 << 8);
        for (DWORD k = 0; k < 128; k++)
            if (s->sounding[c][k]) {
                midiOutShortMsg(s->out, 0x80 | c | k << 8 | 64 << 16);
                s->sounding[c][k] = FALSE;
            }
    }
    midiOutClose(s->out);
    s->out = nullptr;
}

static void send_long(SEQ *s, BYTE status, const BYTE *data, DWORD n)
{
    if (!s->out || n + 1 > 4096)
        return;
    BYTE buffer[4096];
    buffer[0] = status;
    memcpy(buffer + 1, data, n);
    MIDIHDR h = {(LPSTR)buffer, n + 1};
    if (!midiOutPrepareHeader(s->out, &h, sizeof(h))) {
        midiOutLongMsg(s->out, &h, sizeof(h));
        midiOutUnprepareHeader(s->out, &h, sizeof(h));
    }
}

/* ------------------------------------------------------------------ events */

static void rewind_tracks(SEQ *s)
{
    for (UINT i = 0; i < s->count; i++) {
        TRACK *t = &s->tracks[i];
        t->p = t->start;
        t->status = 0;
        t->ended = FALSE;
        DWORD delta = 0;
        if (!varlen(&t->p, t->end, &delta))
            t->ended = TRUE;
        t->tick = delta;
    }
}

static TRACK *next_track(SEQ *s)
{
    TRACK *best = nullptr;
    for (UINT i = 0; i < s->count; i++) {
        TRACK *t = &s->tracks[i];
        if (!t->ended && (!best || t->tick < best->tick))
            best = t;
    }
    return best;
}

/*
 * one event of a track. chasing sends what sets a channel up but no note;
 * tempo comes from the first track only.
 */
static void play_event(SEQ *s, TRACK *t, BOOL chase)
{
    const BYTE *end = t->end;
    BYTE b = *t->p;
    if (b >= 0x80) {
        t->status = b;
        t->p++;
    }
    BYTE status = t->status;
    if (status == 0xFF) {
        BYTE type = *t->p++;
        DWORD n = 0;
        varlen(&t->p, end, &n);
        if (type == 0x51 && n >= 3 && t == &s->tracks[0] && !s->smpte && !s->forced_us) {
            DWORD tempo = t->p[0] << 16 | t->p[1] << 8 | t->p[2];
            if (!chase) {
                /* the clock restarts at this tick with the new tempo. */
                s->time0 += MulDiv(t->tick - s->tick0, s->us, 1000);
                s->tick0 = t->tick;
            }
            s->us = tempo / s->ppqn ? tempo / s->ppqn : 1;
        }
        t->p += n;
        t->status = 0;
        if (type == 0x2F)
            t->ended = TRUE;
    } else if (status == 0xF0 || status == 0xF7) {
        DWORD n = 0;
        varlen(&t->p, end, &n);
        if (status == 0xF0)
            send_long(s, 0xF0, t->p, n);
        else if (n)
            send_long(s, t->p[0], t->p + 1, n - 1);
        t->p += n;
        t->status = 0;
    } else {
        int count = data_bytes(status);
        BYTE d1 = t->p[0] & 0x7F, d2 = count > 1 ? t->p[1] & 0x7F : 0;
        t->p += count;
        BYTE type = status & 0xF0, channel = status & 15;
        BOOL note = type == 0x80 || type == 0x90 || type == 0xA0;
        if (s->out && !(chase && note)) {
            midiOutShortMsg(s->out, status | d1 << 8 | d2 << 16);
            if (type == 0x90 && d2)
                s->sounding[channel][d1] = TRUE;
            else if (type == 0x80 || type == 0x90)
                s->sounding[channel][d1] = FALSE;
        }
    }
    if (!t->ended && t->p < end) {
        DWORD delta = 0;
        if (!varlen(&t->p, end, &delta))
            t->ended = TRUE;
        t->tick += delta;
    } else {
        t->ended = TRUE;
    }
}

/* to a tick from the start, every setup event sent on the way. */
static void chase_to(SEQ *s, DWORD tick)
{
    rewind_tracks(s);
    DWORD fps = s->fps == 29 ? 30 : s->fps;
    s->us = s->forced_us ? s->forced_us : s->smpte ? 1000000 / (fps * s->tpf) : s->map[0].us;
    if (!s->us)
        s->us = 1;
    for (TRACK *t; (t = next_track(s)) && t->tick < tick;)
        play_event(s, t, TRUE);
}

static DWORD clock_tick(const SEQ *s)
{
    DWORD elapsed = timeGetTime() - s->time0;
    return s->tick0 + (DWORD)((ULONG64)elapsed * 1000 / s->us);
}

static void settle(SEQ *s, UINT status)
{
    if (s->pending) {
        s->pending = FALSE;
        mciDriverNotify(s->callback, s->id, status);
    }
}

static DWORD WINAPI player(void *arg)
{
    SEQ *s = arg;
    DWORD wait = INFINITE;
    while (!s->quit) {
        WaitForSingleObject(s->event, wait);
        RtlEnterCriticalSection(&s->lock);
        wait = INFINITE;
        if (s->mode == MODE_PLAYING) {
            DWORD now = clock_tick(s);
            TRACK *t;
            while ((t = next_track(s)) && t->tick <= now && t->tick < s->to)
                play_event(s, t, FALSE);
            DWORD next = t && t->tick < s->to ? t->tick : s->to;
            if (now >= s->to || (!t && now >= next)) {
                s->position = s->to;
                close_port(s);
                s->mode = MODE_STOPPED;
                settle(s, MCI_NOTIFY_SUCCESSFUL);
            } else {
                s->position = now;
                DWORD ms = next > now ? (DWORD)((ULONG64)(next - now) * s->us / 1000) : 0;
                wait = ms < 1 ? 1 : ms > 100 ? 100 : ms;
            }
        }
        RtlLeaveCriticalSection(&s->lock);
    }
    return 0;
}

static DWORD current(SEQ *s)
{
    if (s->mode != MODE_PLAYING)
        return s->position;
    DWORD t = clock_tick(s);
    return t > s->to ? s->to : t;
}

/* stop and pause end the notes, close the port and keep the position. */
static void halt(SEQ *s, int mode, BOOL abort)
{
    if (s->mode == MODE_PLAYING)
        s->position = current(s);
    close_port(s);
    s->mode = mode;
    if (abort)
        settle(s, MCI_NOTIFY_ABORTED);
}

static MCIERROR wait_for(SEQ *s)
{
    for (;;) {
        RtlEnterCriticalSection(&s->lock);
        BOOL busy = s->mode == MODE_PLAYING;
        RtlLeaveCriticalSection(&s->lock);
        if (!busy || mciDriverYield(s->id))
            return MMSYSERR_NOERROR;
        Sleep(10);
    }
}

static MCIERROR play(SEQ *s, DWORD flags, MCI_PLAY_PARMS *p)
{
    if ((flags & MCI_FROM) && !in_range(s, p->dwFrom))
        return MCIERR_OUTOFRANGE;
    if ((flags & MCI_TO) && !in_range(s, p->dwTo))
        return MCIERR_OUTOFRANGE;
    RtlEnterCriticalSection(&s->lock);
    DWORD from = (flags & MCI_FROM) ? from_format(s, p->dwFrom) : current(s);
    DWORD to = (flags & MCI_TO) ? from_format(s, p->dwTo) : s->length;
    if (from > to || (!(flags & MCI_FROM) && (flags & MCI_TO) && to < from)) {
        RtlLeaveCriticalSection(&s->lock);
        return MCIERR_OUTOFRANGE;
    }
    if (s->pending && (flags & MCI_NOTIFY))
        settle(s, (flags & MCI_FROM) || to != s->to ? MCI_NOTIFY_ABORTED : MCI_NOTIFY_SUPERSEDED);
    MCIERROR r = MMSYSERR_NOERROR;
    if (s->mode == MODE_PLAYING && !(flags & MCI_FROM)) {
        s->to = to;
    } else {
        if (s->mode == MODE_PLAYING)
            halt(s, MODE_STOPPED, FALSE);
        r = open_port(s);
        if (!r) {
            if (from >= s->length && to >= s->length)
                from = s->length;
            chase_to(s, from);
            s->position = from;
            s->to = to;
            s->tick0 = from;
            s->time0 = timeGetTime();
            s->mode = MODE_PLAYING;
        }
    }
    if (!r && (flags & MCI_NOTIFY)) {
        s->callback = (HWND)p->dwCallback;
        s->pending = TRUE;
        if (from == to)
            settle(s, MCI_NOTIFY_SUCCESSFUL);
    }
    RtlLeaveCriticalSection(&s->lock);
    SetEvent(s->event);
    if (!r && (flags & MCI_WAIT))
        r = wait_for(s);
    return r;
}

/* ------------------------------------------------------------------ open */

static void free_seq(SEQ *s)
{
    if (!s)
        return;
    s->quit = TRUE;
    if (s->thread) {
        SetEvent(s->event);
        WaitForSingleObject(s->thread, INFINITE);
        CloseHandle(s->thread);
    }
    close_port(s);
    if (s->event)
        CloseHandle(s->event);
    HeapFree(GetProcessHeap(), 0, s->image);
    HeapFree(GetProcessHeap(), 0, s->map);
    RtlDeleteCriticalSection(&s->lock);
    HeapFree(GetProcessHeap(), 0, s);
}

static MCIERROR done(SEQ *s, MCIERROR r, DWORD flags, DWORD_PTR parms);

static MCIERROR open(MCIDEVICEID id, DWORD flags, MCI_OPEN_PARMSW *p)
{
    BOOL element = flags & MCI_OPEN_ELEMENT, element_id = flags & MCI_OPEN_ELEMENT_ID;
    if (element && element_id)
        return MCIERR_FLAGS_NOT_COMPATIBLE;
    if (!element && !element_id)
        return done(nullptr, MMSYSERR_NOERROR, flags, (DWORD_PTR)p);
    if (flags & MCI_OPEN_SHAREABLE)
        return MCIERR_UNSUPPORTED_FUNCTION;
    const WCHAR *name = element ? p->lpstrElementName : nullptr;
    if (!name || !name[0])
        return MCIERR_FILENAME_REQUIRED;
    if (lstrlenW(name) >= MAX_PATH)
        return MCIERR_FILE_NOT_FOUND;
    if (lstrlenW(name) >= 128)
        return MCIERR_INVALID_FILE;
    SEQ *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s)
        return MCIERR_OUT_OF_MEMORY;
    RtlInitializeCriticalSection(&s->lock);
    s->id = id;
    s->port = MCI_SEQ_MAPPER;
    lstrcpyW(s->file, name);
    MCIERROR r = MMSYSERR_NOERROR;
    HANDLE f = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        r = MCIERR_FILE_NOT_FOUND;
    } else {
        s->image_size = GetFileSize(f, nullptr);
        s->image = HeapAlloc(GetProcessHeap(), 0, s->image_size + 1);
        DWORD n;
        if (!s->image)
            r = MCIERR_OUT_OF_MEMORY;
        else if (!ReadFile(f, s->image, s->image_size, &n, nullptr) || n != s->image_size)
            r = MCIERR_FILE_READ;
        CloseHandle(f);
    }
    if (!r)
        r = parse(s);
    if (!r) {
        if (s->smpte)
            s->time_format = s->fps == 24 ? MCI_FORMAT_SMPTE_24 : s->fps == 25 ? MCI_FORMAT_SMPTE_25
                           : s->fps == 29 ? MCI_FORMAT_SMPTE_30DROP : MCI_FORMAT_SMPTE_30;
        else
            s->time_format = MCI_SEQ_FORMAT_SONGPTR;
        s->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        s->thread = s->event ? CreateThread(nullptr, 0, player, s, 0, nullptr) : nullptr;
        if (!s->thread)
            r = MCIERR_OUT_OF_MEMORY;
        else
            SetThreadPriority(s->thread, THREAD_PRIORITY_TIME_CRITICAL);
    }
    if (r) {
        free_seq(s);
        return r;
    }
    mciSetDriverData(id, (DWORD_PTR)s);
    return done(s, MMSYSERR_NOERROR, flags, (DWORD_PTR)p);
}

/* ------------------------------------------------------------------ status, set, info */

static MCIERROR resource(DWORD_PTR *out, WORD value, UINT string)
{
    *out = MAKELONG(value, string);
    return MCI_RESOURCE_RETURNED;
}

static MCIERROR status(SEQ *s, DWORD flags, MCI_STATUS_PARMS *p)
{
    if (!(flags & MCI_STATUS_ITEM))
        return MCIERR_MISSING_PARAMETER;
    DWORD item = p->dwItem;
    if ((flags & MCI_TRACK) && item != MCI_STATUS_LENGTH && item != MCI_STATUS_POSITION)
        return MCIERR_DUPLICATE_FLAGS;
    MCIERROR smpte = is_smpte(s->time_format) ? MCI_COLONIZED4_RETURN : 0;
    switch (item) {
    case MCI_STATUS_LENGTH:
        if ((flags & MCI_TRACK) && p->dwTrack != 1)
            return MCIERR_OUTOFRANGE;
        p->dwReturn = to_format(s, s->length);
        return smpte;
    case MCI_STATUS_POSITION:
        if ((flags & MCI_TRACK) && p->dwTrack != 1)
            return MCIERR_OUTOFRANGE;
        RtlEnterCriticalSection(&s->lock);
        p->dwReturn = (flags & MCI_STATUS_START) ? to_format(s, 0) : to_format(s, current(s));
        RtlLeaveCriticalSection(&s->lock);
        return smpte;
    case MCI_STATUS_NUMBER_OF_TRACKS:
    case MCI_STATUS_CURRENT_TRACK:
        p->dwReturn = 1;
        return 0;
    case MCI_STATUS_MODE: {
        static const WORD modes[4] = {MCI_MODE_STOP, MCI_MODE_PLAY, MCI_MODE_PAUSE, MCI_MODE_SEEK};
        WORD mode = modes[s->mode];
        return resource(&p->dwReturn, mode, mode);
    }
    case MCI_STATUS_MEDIA_PRESENT:
    case MCI_STATUS_READY:
        return resource(&p->dwReturn, TRUE, 532);
    case MCI_STATUS_TIME_FORMAT:
        if (s->time_format == MCI_SEQ_FORMAT_SONGPTR)
            return resource(&p->dwReturn, MCI_SEQ_FORMAT_SONGPTR, 1225);
        return resource(&p->dwReturn, (WORD)s->time_format, 533 + s->time_format);
    case MCI_SEQ_STATUS_TEMPO: {
        RtlEnterCriticalSection(&s->lock);
        DWORD fps = s->fps == 29 ? 30 : s->fps;
        DWORD us = s->mode == MODE_PLAYING ? s->us : s->forced_us ? s->forced_us
                 : s->smpte ? 1000000 / (fps * s->tpf) : segment_of_tick(s, current(s))->us;
        RtlLeaveCriticalSection(&s->lock);
        if (!us)
            us = 1;
        p->dwReturn = s->smpte ? 1000000 / (us * s->tpf) : 60000000 / (us * s->ppqn);
        return 0;
    }
    case MCI_SEQ_STATUS_PORT:
        if (s->port == MCI_SEQ_MAPPER)
            return resource(&p->dwReturn, MCI_SEQ_MAPPER, 1227);
        if (s->port == MCI_SEQ_NONE)
            return resource(&p->dwReturn, MCI_SEQ_NONE, 1226);
        p->dwReturn = s->port;
        return 0;
    case MCI_SEQ_STATUS_SLAVE:
    case MCI_SEQ_STATUS_MASTER:
        return resource(&p->dwReturn, MCI_SEQ_NONE, 1226);
    case MCI_SEQ_STATUS_OFFSET:
        p->dwReturn = s->offset;
        return MCI_COLONIZED4_RETURN;
    case MCI_SEQ_STATUS_DIVTYPE: {
        WORD type = !s->smpte ? MCI_SEQ_DIV_PPQN : s->fps == 24 ? MCI_SEQ_DIV_SMPTE_24
                  : s->fps == 25 ? MCI_SEQ_DIV_SMPTE_25 : s->fps == 29 ? MCI_SEQ_DIV_SMPTE_30DROP
                  : MCI_SEQ_DIV_SMPTE_30;
        return resource(&p->dwReturn, type, type);
    }
    default:
        return MCIERR_UNSUPPORTED_FUNCTION;
    }
}

/* one item a call, as windows' driver takes them. */
static MCIERROR set(SEQ *s, DWORD flags, MCI_SEQ_SET_PARMS *p)
{
    DWORD items = MCI_SET_TIME_FORMAT | MCI_SEQ_SET_TEMPO | MCI_SEQ_SET_PORT | MCI_SEQ_SET_SLAVE |
                  MCI_SEQ_SET_MASTER | MCI_SEQ_SET_OFFSET;
    if (flags & (MCI_SET_AUDIO | MCI_SET_VIDEO | MCI_SET_DOOR_OPEN | MCI_SET_DOOR_CLOSED))
        return MCIERR_UNSUPPORTED_FUNCTION;
    DWORD item = flags & ~0x6003u;
    if (item & ~items)
        return MCIERR_UNRECOGNIZED_KEYWORD;
    if (!item || (item & (item - 1)))
        return MCIERR_MISSING_PARAMETER;
    RtlEnterCriticalSection(&s->lock);
    MCIERROR r = MMSYSERR_NOERROR;
    switch (item) {
    case MCI_SET_TIME_FORMAT:
        if (p->dwTimeFormat == MCI_SEQ_FORMAT_SONGPTR) {
            if (s->smpte)
                r = MCIERR_SEQ_DIV_INCOMPATIBLE;
            else
                s->time_format = MCI_SEQ_FORMAT_SONGPTR;
        } else if (p->dwTimeFormat == MCI_FORMAT_MILLISECONDS || is_smpte(p->dwTimeFormat)) {
            s->time_format = p->dwTimeFormat;
        } else {
            r = MCIERR_BAD_TIME_FORMAT;
        }
        break;
    case MCI_SEQ_SET_TEMPO:
        if (!p->dwTempo) {
            r = MCIERR_OUTOFRANGE;
            break;
        }
        if (s->mode == MODE_PLAYING) {
            DWORD now = clock_tick(s);
            s->tick0 = now;
            s->time0 = timeGetTime();
        }
        DWORD us = s->smpte ? 1000000 / (s->tpf * p->dwTempo) : 60000000 / (s->ppqn * p->dwTempo);
        s->forced_us = us ? us : 1;
        if (s->mode == MODE_PLAYING)
            s->us = s->forced_us;
        break;
    case MCI_SEQ_SET_PORT:
        if (p->dwPort == MCI_SEQ_NONE) {
            close_port(s);
            s->port = MCI_SEQ_NONE;
        } else if (p->dwPort == MCI_SEQ_MAPPER || p->dwPort == 0xFFFFFFFF) {
            s->port = MCI_SEQ_MAPPER;
        } else if (!midiOutGetNumDevs()) {
            r = MCIERR_SEQ_NOMIDIPRESENT;
        } else if (p->dwPort >= midiOutGetNumDevs()) {
            r = MCIERR_SEQ_PORT_NONEXISTENT;
        } else {
            s->port = p->dwPort;
        }
        break;
    case MCI_SEQ_SET_SLAVE:
        if (p->dwSlave != MCI_SEQ_FILE && p->dwSlave != MCI_SEQ_NONE)
            r = MCIERR_UNSUPPORTED_FUNCTION;
        break;
    case MCI_SEQ_SET_MASTER:
        if (p->dwMaster != MCI_SEQ_NONE)
            r = MCIERR_UNSUPPORTED_FUNCTION;
        break;
    case MCI_SEQ_SET_OFFSET: {
        DWORD v = p->dwOffset;
        if ((v & 0xFF) > 23 || ((v >> 8) & 0xFF) > 59 || ((v >> 16) & 0xFF) > 59 || (v >> 24) > 29 || (v & 0x80808080))
            r = MCIERR_OUTOFRANGE;
        else
            s->offset = v;
        break;
    }
    }
    RtlLeaveCriticalSection(&s->lock);
    return r;
}

static MCIERROR info(SEQ *s, DWORD flags, MCI_INFO_PARMSW *p)
{
    if (!p || !p->lpstrReturn || !p->dwRetSize)
        return MCIERR_PARAM_OVERFLOW;
    DWORD which = flags & ~(MCI_NOTIFY | MCI_WAIT);
    if (!which)
        return MCIERR_MISSING_PARAMETER;
    if (which != MCI_INFO_PRODUCT && which != MCI_INFO_FILE)
        return MCIERR_UNRECOGNIZED_KEYWORD;
    if (which == MCI_INFO_FILE && !s)
        return MCIERR_UNSUPPORTED_FUNCTION;
    const WCHAR *text = which == MCI_INFO_PRODUCT ? L"MIDI Sequencer" : s->file;
    if ((DWORD)lstrlenW(text) >= p->dwRetSize)
        return MCIERR_PARAM_OVERFLOW;
    lstrcpyW(p->lpstrReturn, text);
    return MMSYSERR_NOERROR;
}

static MCIERROR capability(DWORD flags, MCI_GETDEVCAPS_PARMS *p)
{
    if (!(flags & MCI_GETDEVCAPS_ITEM))
        return MCIERR_MISSING_PARAMETER;
    BOOL ports = midiOutGetNumDevs() > 0, yes;
    switch (p->dwItem) {
    case MCI_GETDEVCAPS_CAN_RECORD:
    case MCI_GETDEVCAPS_HAS_VIDEO:
    case MCI_GETDEVCAPS_CAN_EJECT:
    case MCI_GETDEVCAPS_CAN_SAVE:
        yes = FALSE;
        break;
    case MCI_GETDEVCAPS_HAS_AUDIO:
    case MCI_GETDEVCAPS_USES_FILES:
    case MCI_GETDEVCAPS_COMPOUND_DEVICE:
    case MCI_GETDEVCAPS_CAN_PLAY:
        yes = ports;
        break;
    case MCI_GETDEVCAPS_DEVICE_TYPE:
        p->dwReturn = MAKELONG(MCI_DEVTYPE_SEQUENCER, MCI_DEVTYPE_SEQUENCER);
        return MCI_RESOURCE_RETURNED;
    default:
        return MCIERR_MISSING_PARAMETER;
    }
    p->dwReturn = yes ? MAKELONG(1, 532) : MAKELONG(0, 531);
    return MCI_RESOURCE_RETURNED;
}

static MCIERROR seek(SEQ *s, DWORD flags, MCI_SEEK_PARMS *p)
{
    DWORD which = flags & (MCI_TO | MCI_SEEK_TO_START | MCI_SEEK_TO_END);
    if (flags & ~(which | MCI_NOTIFY | MCI_WAIT))
        return MCIERR_UNRECOGNIZED_KEYWORD;
    if (!which)
        return MCIERR_MISSING_PARAMETER;
    if (which & (which - 1))
        return MCIERR_FLAGS_NOT_COMPATIBLE;
    if ((flags & MCI_TO) && !in_range(s, p->dwTo))
        return MCIERR_OUTOFRANGE;
    RtlEnterCriticalSection(&s->lock);
    halt(s, MODE_STOPPED, TRUE);
    s->position = which == MCI_SEEK_TO_START ? 0 : which == MCI_SEEK_TO_END ? s->length : from_format(s, p->dwTo);
    RtlLeaveCriticalSection(&s->lock);
    return MMSYSERR_NOERROR;
}

/* ------------------------------------------------------------------ the driver */

static MCIERROR done(SEQ *s, MCIERROR r, DWORD flags, DWORD_PTR parms)
{
    if (!LOWORD(r) && (flags & MCI_NOTIFY) && parms) {
        if (s) {
            RtlEnterCriticalSection(&s->lock);
            settle(s, MCI_NOTIFY_SUPERSEDED);
            RtlLeaveCriticalSection(&s->lock);
        }
        mciDriverNotify((HANDLE)((MCI_GENERIC_PARMS *)parms)->dwCallback, s ? s->id : 0, MCI_NOTIFY_SUCCESSFUL);
    }
    return r;
}

static MCIERROR command(MCIDEVICEID id, UINT message, DWORD flags, DWORD_PTR parms)
{
    SEQ *s = (SEQ *)mciGetDriverData(id);
    switch (message) {
    case MCI_OPEN_DRIVER:
        return open(id, flags, (MCI_OPEN_PARMSW *)parms);
    case MCI_CLOSE_DRIVER:
        if (s) {
            RtlEnterCriticalSection(&s->lock);
            halt(s, MODE_STOPPED, FALSE);
            settle(s, flags == MCI_NOTIFY ? MCI_NOTIFY_SUPERSEDED : MCI_NOTIFY_ABORTED);
            RtlLeaveCriticalSection(&s->lock);
        }
        mciSetDriverData(id, 0);
        free_seq(s);
        return done(nullptr, MMSYSERR_NOERROR, flags, parms);
    case MCI_GETDEVCAPS:
        return done(s, capability(flags, (MCI_GETDEVCAPS_PARMS *)parms), flags, parms);
    case MCI_INFO:
        return done(s, info(s, flags, (MCI_INFO_PARMSW *)parms), flags, parms);
    case MCI_PLAY:
    case MCI_SEEK:
    case MCI_STOP:
    case MCI_PAUSE:
    case MCI_STATUS:
    case MCI_SET:
        break;
    case MCI_RESUME:
    case MCI_RECORD:
    case MCI_SAVE:
    case MCI_STEP:
    case MCI_CUE:
    case MCI_LOAD:
        return MCIERR_UNSUPPORTED_FUNCTION;
    default:
        if (message >= MCI_REALIZE && message <= MCI_DELETE)
            return MCIERR_UNSUPPORTED_FUNCTION;
        return MCIERR_UNRECOGNIZED_COMMAND;
    }
    if (!s)
        return done(nullptr, MCIERR_UNSUPPORTED_FUNCTION, flags, parms);
    if (!parms)
        return MCIERR_NULL_PARAMETER_BLOCK;
    switch (message) {
    case MCI_PLAY:
        return play(s, flags, (MCI_PLAY_PARMS *)parms);
    case MCI_SEEK:
        return done(s, seek(s, flags, (MCI_SEEK_PARMS *)parms), flags, parms);
    case MCI_STOP:
    case MCI_PAUSE:
        RtlEnterCriticalSection(&s->lock);
        halt(s, message == MCI_PAUSE ? MODE_PAUSED : MODE_STOPPED, TRUE);
        RtlLeaveCriticalSection(&s->lock);
        return done(s, MMSYSERR_NOERROR, flags, parms);
    case MCI_STATUS:
        return done(s, status(s, flags, (MCI_STATUS_PARMS *)parms), flags, parms);
    default:
        return done(s, set(s, flags, (MCI_SEQ_SET_PARMS *)parms), flags, parms);
    }
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(module);
    return TRUE;
}

DECLSPEC_EXPORT LRESULT WINAPI DriverProc(DWORD_PTR id, HDRVR driver, UINT message, LPARAM param1, LPARAM param2)
{
    switch (message) {
    case DRV_LOAD:
    case DRV_FREE:
    case DRV_ENABLE:
    case DRV_DISABLE:
    case DRV_CLOSE:
        return 1;
    case DRV_OPEN: {
        MCI_OPEN_DRIVER_PARMS *p = (MCI_OPEN_DRIVER_PARMS *)param2;
        if (!p)
            return 1;
        p->wType = MCI_DEVTYPE_SEQUENCER;
        p->wCustomCommandTable = MCI_NO_COMMAND_TABLE;
        return p->wDeviceID;
    }
    case DRV_QUERYCONFIGURE:
        return 0;
    case DRV_CONFIGURE:
        return DRVCNF_CANCEL;
    case DRV_INSTALL:
    case DRV_REMOVE:
        return DRVCNF_RESTART;
    }
    if (message >= MCI_FIRST && message <= MCI_LAST && id)
        return command((MCIDEVICEID)id, message, (DWORD)param1, (DWORD_PTR)param2);
    return message >= DRV_USER ? MCIERR_UNRECOGNIZED_COMMAND : 0;
}
