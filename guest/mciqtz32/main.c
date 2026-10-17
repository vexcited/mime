/*
 * mciqtz32.dll, the mpegvideo and digitalvideo device windows plays
 * through directshow. mime has no directshow, so the driver plays the
 * audio files it is opened for itself: mp3 through the acm's layer 3
 * decoder and wave files directly or through the acm. video files are
 * refused. positions round up to whole milliseconds, notifications are
 * posted straight to the callback window, and the device reports video
 * and the digitalvideo type even for audio, as windows' does.
 */
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/mmsystem.h>
#include <mime/win32/msacm.h>
#include <mime/win32/mmreg.h>

#define MODE_STOPPED 0
#define MODE_PLAYING 1
#define MODE_PAUSED 2
#define MODE_SEEKING 3
#define MAX_BUFFERS 8

#define MCI_TEST 0x00000020
#define PLAY_REPEAT 0x00010000
#define PLAY_REVERSE 0x00020000
#define PLAY_WINDOW 0x01000000
#define PLAY_FULLSCREEN 0x02000000
#define PLAY_FULLSCREEN_2 0x04000000
#define MCI_SETAUDIO 0x0873
#define MCI_SETVIDEO 0x0876
#define MCI_SIGNAL 0x0875
#define MCI_CONFIGURE 0x087A
#define MCI_RESTORE 0x087B
#define SETAUDIO_ITEM 0x00800000
#define SETAUDIO_VALUE 0x01000000
#define SETAUDIO_LEFT 0x00200000
#define SETAUDIO_RIGHT 0x00400000
#define SETAUDIO_VOLUME 0x4002
#define SET_SPEED 0x00020000
#define SET_SEEK_EXACTLY 0x00010000
#define SET_FILE_FORMAT 0x00080000
#define SET_STILL 0x00040000
#define STATUS_LEFT 0x00080000
#define STATUS_RIGHT 0x00100000
#define STATUS_NOMINAL 0x00020000
#define STATUS_DISK_SPACE 0x00200000
#define STATUS_REFERENCE 0x00040000
#define INFO_VERSION 0x00000400
#define INFO_USAGE 0x00004000
#define INFO_WINDOW_TEXT 0x00010000
#define INFO_ITEM 0x00020000

typedef struct MEDIA {
    MCIDEVICEID id;
    RTL_CRITICAL_SECTION lock;
    WCHAR file[MAX_PATH];
    WAVEFORMATEX pcm;
    BYTE *data;
    DWORD size;           /* the decoded length */
    volatile DWORD ready; /* what the decoder has made of it */
    BYTE *source;
    DWORD source_size, source_start;
    WAVEFORMATEX *coded;  /* the compressed format, null for pcm */
    HANDLE decoder;
    DWORD time_format;
    DWORD volume, speed;
    BOOL mute_left, mute_right;
    /* playing */
    HWAVEOUT out;
    HANDLE thread, event;
    volatile BOOL quit;
    int mode;
    BOOL repeat;
    DWORD loop_from;
    DWORD from, to, next, position;
    WAVEHDR headers[MAX_BUFFERS];
    DWORD chunk;
    HWND callback;
    BOOL pending;
} MEDIA;

static HMODULE g_module;
static UINT g_tables[256];

/* ------------------------------------------------------------------ notifications */

static void post(MEDIA *m, HWND callback, UINT status)
{
    PostMessageW(callback, MM_MCINOTIFY, status, m ? m->id : 0);
}

static void settle(MEDIA *m, UINT status)
{
    if (m->pending) {
        m->pending = FALSE;
        post(m, m->callback, status);
    }
}

static MCIERROR done(MEDIA *m, MCIERROR r, DWORD flags, DWORD_PTR parms)
{
    if (!LOWORD(r) && (flags & MCI_NOTIFY) && parms) {
        if (m) {
            RtlEnterCriticalSection(&m->lock);
            settle(m, MCI_NOTIFY_SUPERSEDED);
            RtlLeaveCriticalSection(&m->lock);
        }
        post(m, (HWND)((MCI_GENERIC_PARMS *)parms)->dwCallback, MCI_NOTIFY_SUCCESSFUL);
    }
    return r;
}

/* ------------------------------------------------------------------ time */

static DWORD to_ms(const MEDIA *m, DWORD bytes)
{
    /* directshow's reference time in 100 ns, rounded up to milliseconds. */
    ULONG64 t = (ULONG64)(bytes / m->pcm.nBlockAlign) * 10000000 / m->pcm.nSamplesPerSec;
    return (DWORD)((t + 9999) / 10000);
}

static DWORD to_bytes(const MEDIA *m, DWORD ms)
{
    ULONG64 frames = (ULONG64)ms * m->pcm.nSamplesPerSec / 1000;
    ULONG64 bytes = frames * m->pcm.nBlockAlign;
    return bytes > m->size ? m->size : (DWORD)bytes;
}

static DWORD current(MEDIA *m)
{
    if (m->mode == MODE_STOPPED || !m->out)
        return m->position;
    MMTIME t = {TIME_BYTES};
    waveOutGetPosition(m->out, &t, sizeof(t));
    DWORD at = m->from + t.u.cb;
    if (m->repeat && m->to > m->loop_from)
        while (at > m->to)
            at -= m->to - m->loop_from;
    return at > m->to ? m->to : at;
}

/* ------------------------------------------------------------------ decoding */

static const WORD BITRATES[2][15] = {
    {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320},
    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
};
static const DWORD RATES[3][3] = {{44100, 48000, 32000}, {22050, 24000, 16000}, {11025, 12000, 8000}};

/* a layer 3 header: its frame length, rate and channels. */
static DWORD frame_header(const BYTE *p, DWORD *rate, WORD *channels, DWORD *samples)
{
    DWORD h = (DWORD)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
    if ((h & 0xFFE00000) != 0xFFE00000)
        return 0;
    DWORD version = (h >> 19) & 3, layer = (h >> 17) & 3, index = (h >> 12) & 15, sr = (h >> 10) & 3;
    if (version == 1 || layer != 1 || !index || index == 15 || sr == 3)
        return 0;
    BOOL lsf = version != 3;
    *rate = RATES[version == 3 ? 0 : version == 2 ? 1 : 2][sr];
    *channels = ((h >> 6) & 3) == 3 ? 1 : 2;
    *samples = lsf ? 576 : 1152;
    return (lsf ? 72000 : 144000) * BITRATES[lsf][index] / *rate + ((h >> 9) & 1);
}

/* finds the frames of an mp3 file, past any id3 tag, and the length they decode to. */
static MCIERROR scan_mp3(MEDIA *m)
{
    const BYTE *p = m->source, *end = p + m->source_size;
    if (end - p >= 10 && !memcmp(p, "ID3", 3))
        p += 10 + ((p[6] & 0x7F) << 21 | (p[7] & 0x7F) << 14 | (p[8] & 0x7F) << 7 | (p[9] & 0x7F));
    DWORD rate = 0, samples = 0, frames = 0, bytes = 0;
    WORD channels = 0;
    const BYTE *first = nullptr;
    while (p + 4 <= end) {
        DWORD r, s;
        WORD c;
        DWORD length = frame_header(p, &r, &c, &s);
        if (!length || (first && (r != rate || s != samples))) {
            if (first)
                break;
            p++;
            continue;
        }
        /* a first header counts only with a second after it. */
        if (!first) {
            DWORD r2, s2;
            WORD c2;
            if (p + length + 4 > end || !frame_header(p + length, &r2, &c2, &s2) || r2 != r) {
                p++;
                continue;
            }
            first = p;
            rate = r;
            samples = s;
            channels = c;
        }
        frames++;
        bytes += length;
        p += length;
    }
    if (!first)
        return MCIERR_INVALID_FILE;
    m->source_start = (DWORD)(first - m->source);
    m->source_size = m->source_start + bytes;
    m->pcm = (WAVEFORMATEX){WAVE_FORMAT_PCM, channels, rate, rate * 2 * channels, (WORD)(2 * channels), 16, 0};
    m->size = frames * samples * m->pcm.nBlockAlign;
    MPEGLAYER3WAVEFORMAT *f = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*f));
    if (!f)
        return MCIERR_OUT_OF_MEMORY;
    f->wfx = (WAVEFORMATEX){WAVE_FORMAT_MPEGLAYER3, channels, rate, 0, 1, 0, MPEGLAYER3_WFX_EXTRA_BYTES};
    f->wfx.nAvgBytesPerSec = (DWORD)((ULONG64)bytes * rate / ((ULONG64)frames * samples));
    if (!f->wfx.nAvgBytesPerSec)
        f->wfx.nAvgBytesPerSec = 1;
    f->wID = MPEGLAYER3_ID_MPEG;
    f->fdwFlags = MPEGLAYER3_FLAG_PADDING_OFF;
    f->nBlockSize = 1;
    f->nFramesPerBlock = 1;
    m->coded = &f->wfx;
    return MMSYSERR_NOERROR;
}

static MCIERROR scan_wave(MEDIA *m)
{
    const BYTE *p = m->source, *end = p + m->source_size, *format = nullptr, *data = nullptr;
    DWORD format_size = 0, data_size = 0;
    for (const BYTE *c = p + 12; end - c >= 8;) {
        DWORD size = c[4] | c[5] << 8 | c[6] << 16 | (DWORD)c[7] << 24;
        if (size > (DWORD)(end - c - 8))
            size = (DWORD)(end - c - 8);
        if (!memcmp(c, "fmt ", 4)) {
            format = c + 8;
            format_size = size;
        } else if (!memcmp(c, "data", 4)) {
            data = c + 8;
            data_size = size;
        }
        c += 8 + size + (size & 1);
    }
    if (!format || !data || format_size < 16)
        return MCIERR_INVALID_FILE;
    WAVEFORMATEX f = {};
    memcpy(&f, format, format_size < sizeof(f) ? format_size : sizeof(f));
    if (!f.nBlockAlign || !f.nSamplesPerSec)
        return MCIERR_INVALID_FILE;
    m->source_start = (DWORD)(data - m->source);
    m->source_size = m->source_start + data_size - data_size % f.nBlockAlign;
    if (f.wFormatTag == WAVE_FORMAT_PCM) {
        f.cbSize = 0;
        m->pcm = f;
        m->size = m->source_size - m->source_start;
        return MMSYSERR_NOERROR;
    }
    m->coded = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, format_size + sizeof(WAVEFORMATEX));
    if (!m->coded)
        return MCIERR_OUT_OF_MEMORY;
    memcpy(m->coded, format, format_size);
    m->pcm.wFormatTag = WAVE_FORMAT_PCM;
    if (acmFormatSuggest(nullptr, m->coded, &m->pcm, sizeof(m->pcm), ACM_FORMATSUGGESTF_WFORMATTAG))
        return MCIERR_INVALID_FILE;
    DWORD size = 0;
    HACMSTREAM s;
    if (acmStreamOpen(&s, nullptr, m->coded, &m->pcm, nullptr, 0, 0, ACM_STREAMOPENF_NONREALTIME))
        return MCIERR_INVALID_FILE;
    acmStreamSize(s, m->source_size - m->source_start, &size, ACM_STREAMSIZEF_SOURCE);
    acmStreamClose(s, 0);
    m->size = size - size % m->pcm.nBlockAlign;
    return MMSYSERR_NOERROR;
}

/* decodes the whole file ahead of playing; what is ready may be played while the rest follows. */
static DWORD WINAPI decode(void *arg)
{
    MEDIA *m = arg;
    HACMSTREAM s;
    if (acmStreamOpen(&s, nullptr, m->coded, &m->pcm, nullptr, 0, 0, ACM_STREAMOPENF_NONREALTIME)) {
        m->size = m->ready;
        return 0;
    }
    static const DWORD in_size = 16384;
    DWORD out_size = 0;
    acmStreamSize(s, in_size, &out_size, ACM_STREAMSIZEF_SOURCE);
    if (out_size < 65536)
        out_size = 65536;
    BYTE *in = HeapAlloc(GetProcessHeap(), 0, in_size), *out = HeapAlloc(GetProcessHeap(), 0, out_size);
    ACMSTREAMHEADER h = {.cbStruct = sizeof(h), .pbSrc = in, .cbSrcLength = in_size, .pbDst = out, .cbDstLength = out_size};
    if (in && out && !acmStreamPrepareHeader(s, &h, 0)) {
        DWORD at = m->source_start, flags = ACM_STREAMCONVERTF_START;
        for (int idle = 0; !m->quit && idle < 2;) {
            DWORD n = m->source_size - at < in_size ? m->source_size - at : in_size;
            memcpy(in, m->source + at, n);
            h.cbSrcLength = n;
            if (acmStreamConvert(s, &h, flags | (n < in_size ? ACM_STREAMCONVERTF_END : 0)))
                break;
            flags = 0;
            at += h.cbSrcLengthUsed;
            DWORD used = h.cbDstLengthUsed, ready = m->ready;
            if (ready + used > m->size)
                used = m->size - ready;
            memcpy(m->data + ready, out, used);
            m->ready = ready + used;
            if (!h.cbSrcLengthUsed && !h.cbDstLengthUsed)
                idle++;
            if (m->ready >= m->size)
                break;
        }
        h.cbSrcLength = in_size;
        acmStreamUnprepareHeader(s, &h, 0);
    }
    acmStreamClose(s, 0);
    HeapFree(GetProcessHeap(), 0, in);
    HeapFree(GetProcessHeap(), 0, out);
    /* the rest, if the stream gave less than its frames promised, is silence. */
    RtlEnterCriticalSection(&m->lock);
    if (m->ready < m->size)
        memset(m->data + m->ready, m->pcm.wBitsPerSample == 8 ? 0x80 : 0, m->size - m->ready);
    m->ready = m->size;
    RtlLeaveCriticalSection(&m->lock);
    return 0;
}

/* ------------------------------------------------------------------ playing */

static void queue(MEDIA *m, WAVEHDR *h)
{
    if (m->next >= m->to) {
        if (!m->repeat || m->to <= m->loop_from)
            return;
        m->next = m->loop_from;
    }
    DWORD limit = m->to < m->ready ? m->to : m->ready;
    if (m->next >= limit)
        return;
    DWORD n = limit - m->next < m->chunk ? limit - m->next : m->chunk;
    h->lpData = (LPSTR)m->data + m->next;
    h->dwBufferLength = n;
    h->dwFlags = 0;
    waveOutPrepareHeader(m->out, h, sizeof(*h));
    waveOutWrite(m->out, h, sizeof(*h));
    m->next += n;
}

static BOOL all_done(const MEDIA *m)
{
    for (int i = 0; i < MAX_BUFFERS; i++)
        if (m->headers[i].dwFlags & WHDR_INQUEUE)
            return FALSE;
    return TRUE;
}

static void close_output(MEDIA *m)
{
    if (!m->out)
        return;
    waveOutReset(m->out);
    for (int i = 0; i < MAX_BUFFERS; i++)
        if (m->headers[i].dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(m->out, &m->headers[i], sizeof(WAVEHDR));
    waveOutClose(m->out);
    m->out = nullptr;
}

static void apply_volume(MEDIA *m)
{
    if (!m->out)
        return;
    DWORD level = m->volume * 65535 / 1000;
    DWORD left = m->mute_left ? 0 : level, right = m->mute_right ? 0 : level;
    waveOutSetVolume(m->out, left | right << 16);
}

static DWORD WINAPI worker(void *arg)
{
    MEDIA *m = arg;
    while (!m->quit) {
        WaitForSingleObject(m->event, 20);
        RtlEnterCriticalSection(&m->lock);
        if (m->mode == MODE_PLAYING && m->out) {
            for (int i = 0; i < MAX_BUFFERS; i++) {
                WAVEHDR *h = &m->headers[i];
                if (!(h->dwFlags & WHDR_INQUEUE)) {
                    if (h->dwFlags & WHDR_PREPARED)
                        waveOutUnprepareHeader(m->out, h, sizeof(*h));
                    h->dwFlags = 0;
                    queue(m, h);
                }
            }
            if (m->next >= m->to && !m->repeat && all_done(m)) {
                m->position = m->to;
                close_output(m);
                m->mode = MODE_STOPPED;
                settle(m, MCI_NOTIFY_SUCCESSFUL);
            }
        }
        RtlLeaveCriticalSection(&m->lock);
    }
    return 0;
}

static MCIERROR start(MEDIA *m)
{
    if (waveOutOpen(&m->out, WAVE_MAPPER, &m->pcm, (DWORD_PTR)m->event, 0, CALLBACK_EVENT))
        return 6;
    apply_volume(m);
    m->chunk = m->pcm.nAvgBytesPerSec / 4;
    m->chunk -= m->chunk % m->pcm.nBlockAlign;
    if (!m->chunk)
        m->chunk = m->pcm.nBlockAlign;
    m->next = m->from;
    memset(m->headers, 0, sizeof(m->headers));
    for (int i = 0; i < MAX_BUFFERS; i++)
        queue(m, &m->headers[i]);
    m->mode = MODE_PLAYING;
    return MMSYSERR_NOERROR;
}

static void stop(MEDIA *m, BOOL abort)
{
    if (m->mode == MODE_PLAYING || m->mode == MODE_PAUSED) {
        if (m->out)
            m->position = current(m);
        close_output(m);
    }
    m->mode = MODE_STOPPED;
    if (abort)
        settle(m, MCI_NOTIFY_ABORTED);
}

static MCIERROR wait_for(MEDIA *m)
{
    for (;;) {
        RtlEnterCriticalSection(&m->lock);
        BOOL busy = m->mode == MODE_PLAYING && !m->repeat;
        RtlLeaveCriticalSection(&m->lock);
        if (!busy || mciDriverYield(m->id))
            return MMSYSERR_NOERROR;
        Sleep(10);
    }
}

static DWORD length_in(const MEDIA *m)
{
    return m->time_format == MCI_FORMAT_BYTES ? m->size : to_ms(m, m->size);
}

static DWORD bytes_of(const MEDIA *m, DWORD value)
{
    if (m->time_format == MCI_FORMAT_BYTES)
        return value - value % m->pcm.nBlockAlign;
    return value >= to_ms(m, m->size) ? m->size : to_bytes(m, value);
}

static MCIERROR play(MEDIA *m, DWORD flags, MCI_PLAY_PARMS *p)
{
    DWORD length = length_in(m);
    if (((flags & MCI_FROM) && p->dwFrom > length) || ((flags & MCI_TO) && p->dwTo > length))
        return MCIERR_OUTOFRANGE;
    if (flags & PLAY_REVERSE)
        return MCIERR_UNSUPPORTED_FUNCTION;
    DWORD screens = flags & (PLAY_WINDOW | PLAY_FULLSCREEN | PLAY_FULLSCREEN_2);
    if (screens & (screens - 1))
        return MCIERR_FLAGS_NOT_COMPATIBLE;
    if (flags & MCI_TEST)
        return MMSYSERR_NOERROR;
    RtlEnterCriticalSection(&m->lock);
    DWORD to = (flags & MCI_TO) ? bytes_of(m, p->dwTo) : m->size;
    if (flags & MCI_FROM) {
        settle(m, MCI_NOTIFY_ABORTED);
        stop(m, FALSE);
        m->position = bytes_of(m, p->dwFrom);
    } else if (to != m->to && m->mode != MODE_STOPPED) {
        settle(m, MCI_NOTIFY_ABORTED);
    }
    if (flags & MCI_NOTIFY)
        settle(m, MCI_NOTIFY_SUPERSEDED);
    MCIERROR r = MMSYSERR_NOERROR;
    m->repeat = (flags & PLAY_REPEAT) != 0;
    m->loop_from = (flags & MCI_FROM) ? bytes_of(m, p->dwFrom) : 0;
    if (m->mode == MODE_PLAYING) {
        m->to = to;
    } else if (m->mode == MODE_PAUSED && m->out) {
        m->to = to;
        waveOutRestart(m->out);
        m->mode = MODE_PLAYING;
    } else {
        DWORD from = m->position >= to ? (m->position >= m->size ? 0 : m->position) : m->position;
        if (m->position >= m->size && !(flags & MCI_FROM))
            from = 0;
        m->from = from;
        m->to = to;
        r = start(m);
    }
    if (!r && (flags & MCI_NOTIFY)) {
        m->callback = (HWND)p->dwCallback;
        m->pending = !m->repeat;
    }
    RtlLeaveCriticalSection(&m->lock);
    SetEvent(m->event);
    if (!r && (flags & MCI_WAIT))
        r = wait_for(m);
    return r;
}

/* ------------------------------------------------------------------ open and close */

static void free_media(MEDIA *m)
{
    if (!m)
        return;
    m->quit = TRUE;
    if (m->decoder) {
        WaitForSingleObject(m->decoder, INFINITE);
        CloseHandle(m->decoder);
    }
    if (m->thread) {
        SetEvent(m->event);
        WaitForSingleObject(m->thread, INFINITE);
        CloseHandle(m->thread);
    }
    close_output(m);
    if (m->event)
        CloseHandle(m->event);
    if (m->data != m->source + m->source_start)
        HeapFree(GetProcessHeap(), 0, m->data);
    HeapFree(GetProcessHeap(), 0, m->source);
    HeapFree(GetProcessHeap(), 0, m->coded);
    RtlDeleteCriticalSection(&m->lock);
    HeapFree(GetProcessHeap(), 0, m);
}

static MCIERROR open(MCIDEVICEID id, DWORD flags, MCI_OPEN_PARMSW *p)
{
    if (!(flags & (MCI_OPEN_ELEMENT | MCI_OPEN_ELEMENT_ID)))
        return done(nullptr, MMSYSERR_NOERROR, flags, (DWORD_PTR)p);
    const WCHAR *name = (flags & MCI_OPEN_ELEMENT) ? p->lpstrElementName : nullptr;
    if (!name || !name[0])
        return MCIERR_UNSUPPORTED_FUNCTION;
    if (lstrlenW(name) > 255)
        return MCIERR_PARAM_OVERFLOW;
    MEDIA *m = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*m));
    if (!m)
        return MCIERR_OUT_OF_MEMORY;
    RtlInitializeCriticalSection(&m->lock);
    m->id = id;
    m->volume = 1000;
    m->speed = 1000;
    lstrcpyW(m->file, name);
    MCIERROR r = MMSYSERR_NOERROR;
    HANDLE f = CreateFileW(name, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        r = MCIERR_FILE_NOT_FOUND;
    } else {
        m->source_size = GetFileSize(f, nullptr);
        m->source = HeapAlloc(GetProcessHeap(), 0, m->source_size + 8);
        DWORD n;
        if (!m->source)
            r = MCIERR_OUT_OF_MEMORY;
        else if (!ReadFile(f, m->source, m->source_size, &n, nullptr) || n != m->source_size)
            r = MCIERR_FILE_NOT_FOUND;
        CloseHandle(f);
    }
    if (!r) {
        if (m->source_size >= 12 && !memcmp(m->source, "RIFF", 4) && !memcmp(m->source + 8, "WAVE", 4))
            r = scan_wave(m);
        else
            r = scan_mp3(m);
    }
    if (!r && (!m->pcm.nBlockAlign || waveOutOpen(nullptr, WAVE_MAPPER, &m->pcm, 0, 0, WAVE_FORMAT_QUERY)))
        r = MCIERR_INVALID_FILE;
    if (!r) {
        if (m->coded) {
            m->data = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, m->size + 1);
            if (!m->data)
                r = MCIERR_OUT_OF_MEMORY;
            else
                m->decoder = CreateThread(nullptr, 0, decode, m, 0, nullptr);
        } else {
            m->data = m->source + m->source_start;
            m->ready = m->size;
        }
    }
    if (!r) {
        m->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        m->thread = m->event ? CreateThread(nullptr, 0, worker, m, 0, nullptr) : nullptr;
        if (!m->thread || (m->coded && !m->decoder))
            r = MCIERR_INTERNAL;
    }
    if (r) {
        free_media(m);
        return r;
    }
    mciSetDriverData(id, (DWORD_PTR)m);
    return done(m, MMSYSERR_NOERROR, flags, (DWORD_PTR)p);
}

/* ------------------------------------------------------------------ status, set, info */

static MCIERROR boolean(DWORD_PTR *out, BOOL yes)
{
    *out = yes ? MAKELONG(1, 532) : MAKELONG(0, 531);
    return MCI_RESOURCE_RETURNED;
}

static MCIERROR status(MEDIA *m, DWORD flags, MCI_STATUS_PARMS *p)
{
    if (flags & STATUS_DISK_SPACE)
        return MCIERR_UNSUPPORTED_FUNCTION;
    if (!(flags & MCI_STATUS_ITEM)) {
        if (flags & STATUS_REFERENCE) {
            p->dwReturn = 0;
            return 0;
        }
        return MCIERR_MISSING_PARAMETER;
    }
    DWORD item = p->dwItem;
    if (((flags & MCI_TRACK) && item != MCI_STATUS_LENGTH && item != MCI_STATUS_POSITION) ||
        ((flags & MCI_STATUS_START) && item != MCI_STATUS_POSITION) || (flags & STATUS_REFERENCE))
        return MCIERR_FLAGS_NOT_COMPATIBLE;
    if (flags & MCI_TEST) {
        p->dwReturn = 0;
        return 0;
    }
    switch (item) {
    case MCI_STATUS_LENGTH:
        if ((flags & MCI_TRACK) && p->dwTrack != 1)
            return MCIERR_OUTOFRANGE;
        p->dwReturn = length_in(m);
        return 0;
    case MCI_STATUS_POSITION:
        if ((flags & MCI_TRACK) && p->dwTrack != 1)
            return MCIERR_OUTOFRANGE;
        if ((flags & MCI_STATUS_START) || (flags & MCI_TRACK)) {
            p->dwReturn = 0;
            return 0;
        }
        RtlEnterCriticalSection(&m->lock);
        DWORD at = current(m);
        if (m->mode != MODE_STOPPED && at > m->to)
            at = m->to;
        p->dwReturn = m->time_format == MCI_FORMAT_BYTES ? at : to_ms(m, at);
        RtlLeaveCriticalSection(&m->lock);
        return 0;
    case MCI_STATUS_NUMBER_OF_TRACKS:
    case MCI_STATUS_CURRENT_TRACK:
        p->dwReturn = 1;
        return 0;
    case MCI_STATUS_MODE: {
        static const WORD modes[4] = {MCI_MODE_STOP, MCI_MODE_PLAY, MCI_MODE_PAUSE, MCI_MODE_SEEK};
        p->dwReturn = MAKELONG(modes[m->mode], modes[m->mode]);
        return MCI_RESOURCE_RETURNED;
    }
    case MCI_STATUS_MEDIA_PRESENT:
    case MCI_STATUS_READY:
    case 0x402C: /* forward */
        return boolean(&p->dwReturn, TRUE);
    case MCI_STATUS_TIME_FORMAT:
        p->dwReturn = MAKELONG(m->time_format, 533 + m->time_format);
        return MCI_RESOURCE_RETURNED;
    case 0x4003: /* speed */
        p->dwReturn = m->speed;
        return 0;
    case 0x4019: /* volume */
        p->dwReturn = m->volume;
        return 0;
    case 0x4014: /* audio */
        p->dwReturn = (flags & STATUS_LEFT ? m->mute_left : flags & STATUS_RIGHT ? m->mute_right
                                                                                 : m->mute_left && m->mute_right)
                          ? MAKELONG(0, 32769)
                          : MAKELONG(1, 32768);
        return MCI_RESOURCE_RETURNED | MCI_RESOURCE_DRIVER;
    case 0x4018: /* video */
        p->dwReturn = MAKELONG(1, 32768);
        return MCI_RESOURCE_RETURNED | MCI_RESOURCE_DRIVER;
    case 0x400B: /* monitor */
        p->dwReturn = MAKELONG(0x4000, 32770);
        return MCI_RESOURCE_RETURNED | MCI_RESOURCE_DRIVER;
    case 0x4010: /* reserved size */
    case 0x8003: /* audio breaks */
        p->dwReturn = 0;
        return 0;
    case 0x4017: /* unsaved */
    case 0x4021: /* window visible */
    case 0x4022:
    case 0x4023:
        return boolean(&p->dwReturn, FALSE);
    case 0x4026: /* pause mode */
        if (m->mode != MODE_PAUSED)
            return MCIERR_NONAPPLICABLE_FUNCTION;
        p->dwReturn = MAKELONG(MCI_MODE_PLAY, MCI_MODE_PLAY);
        return MCI_RESOURCE_RETURNED;
    case 0x402D: /* audio stream */
    case 0x4103: /* audio streams */
        p->dwReturn = 1;
        return 0;
    case 0x402E: /* video stream */
    case 0x4100: /* video streams */
        p->dwReturn = 0;
        return 0;
    case 0x8002: /* play speed */
        p->dwReturn = 100;
        return 0;
    case 0x4027: /* samplespersec */
        p->dwReturn = m->pcm.nSamplesPerSec;
        return 0;
    case 0x4028: /* bytespersec */
        p->dwReturn = m->pcm.nAvgBytesPerSec;
        return 0;
    case 0x4029: /* alignment */
        p->dwReturn = m->pcm.nBlockAlign;
        return 0;
    case 0x402A: /* bitspersample */
        p->dwReturn = m->pcm.wBitsPerSample;
        return 0;
    default:
        return MCIERR_UNSUPPORTED_FUNCTION;
    }
}

static MCIERROR set(MEDIA *m, DWORD flags, MCI_SET_PARMS *p)
{
    if (flags & (SET_FILE_FORMAT | SET_STILL))
        return MCIERR_UNSUPPORTED_FUNCTION;
    BOOL on = flags & MCI_SET_ON, off = flags & MCI_SET_OFF;
    if (on && off)
        return MCIERR_FLAGS_NOT_COMPATIBLE;
    if ((on || off) && (flags & (MCI_SET_TIME_FORMAT | SET_SPEED)))
        return MCIERR_FLAGS_NOT_COMPATIBLE;
    DWORD items = flags & (MCI_SET_TIME_FORMAT | MCI_SET_AUDIO | MCI_SET_VIDEO | SET_SEEK_EXACTLY | SET_SPEED);
    if (!items)
        return MCIERR_UNSUPPORTED_FUNCTION;
    if ((flags & (MCI_SET_AUDIO | MCI_SET_VIDEO | SET_SEEK_EXACTLY)) && !on && !off)
        return MCIERR_MISSING_PARAMETER;
    if (flags & MCI_TEST)
        return MMSYSERR_NOERROR;
    RtlEnterCriticalSection(&m->lock);
    MCIERROR r = MMSYSERR_NOERROR;
    if (flags & MCI_SET_TIME_FORMAT) {
        if (p->dwTimeFormat == MCI_FORMAT_MILLISECONDS)
            m->time_format = MCI_FORMAT_MILLISECONDS;
        else
            r = MCIERR_UNSUPPORTED_FUNCTION;
    }
    if (!r && (flags & SET_SPEED)) {
        DWORD speed = *(DWORD *)((BYTE *)p + sizeof(DWORD_PTR) + 3 * sizeof(DWORD));
        if (speed > 100000)
            r = MCIERR_OUTOFRANGE;
        else
            m->speed = speed;
    }
    if (!r && (flags & MCI_SET_AUDIO)) {
        if (p->dwAudio != MCI_SET_AUDIO_RIGHT)
            m->mute_left = off;
        if (p->dwAudio != MCI_SET_AUDIO_LEFT)
            m->mute_right = off;
        apply_volume(m);
    }
    RtlLeaveCriticalSection(&m->lock);
    return r;
}

static MCIERROR setaudio(MEDIA *m, DWORD flags, DWORD_PTR parms)
{
    BOOL on = flags & MCI_SET_ON, off = flags & MCI_SET_OFF;
    if (on && off)
        return MCIERR_FLAGS_NOT_COMPATIBLE;
    if (flags & MCI_TEST)
        return MMSYSERR_NOERROR;
    DWORD item = *(DWORD *)((BYTE *)parms + sizeof(DWORD_PTR)), value = *(DWORD *)((BYTE *)parms + sizeof(DWORD_PTR) + 4);
    RtlEnterCriticalSection(&m->lock);
    MCIERROR r = MMSYSERR_NOERROR;
    if (on || off) {
        if (!(flags & SETAUDIO_RIGHT))
            m->mute_left = off;
        if (!(flags & SETAUDIO_LEFT))
            m->mute_right = off;
    } else if ((flags & SETAUDIO_ITEM) && item == SETAUDIO_VOLUME && (flags & SETAUDIO_VALUE)) {
        if (value > 1000)
            r = MCIERR_OUTOFRANGE;
        else
            m->volume = value;
    } else if (flags & SETAUDIO_ITEM) {
        r = MCIERR_UNSUPPORTED_FUNCTION;
    } else {
        r = MCIERR_MISSING_PARAMETER;
    }
    apply_volume(m);
    RtlLeaveCriticalSection(&m->lock);
    return r;
}

static MCIERROR info(MEDIA *m, DWORD flags, MCI_INFO_PARMSW *p)
{
    if (!p || !p->lpstrReturn || !p->dwRetSize)
        return MCIERR_PARAM_OVERFLOW;
    DWORD which = flags & ~(MCI_NOTIFY | MCI_WAIT | MCI_TEST);
    if (!which)
        return MCIERR_MISSING_PARAMETER;
    const WCHAR *text;
    if (which == MCI_INFO_PRODUCT)
        text = L"DirectShow";
    else if (which == INFO_VERSION)
        text = L"6.04.2600.0";
    else if (which == MCI_INFO_FILE || which == INFO_WINDOW_TEXT) {
        if (!m)
            return MCIERR_UNSUPPORTED_FUNCTION;
        if (which == INFO_WINDOW_TEXT)
            return MCIERR_NO_WINDOW;
        text = m->file;
    } else if (which == INFO_USAGE || which == INFO_ITEM) {
        return MCIERR_UNSUPPORTED_FUNCTION;
    } else {
        return MCIERR_FLAGS_NOT_COMPATIBLE;
    }
    if ((DWORD)lstrlenW(text) >= p->dwRetSize)
        return MCIERR_PARAM_OVERFLOW;
    lstrcpyW(p->lpstrReturn, text);
    return MMSYSERR_NOERROR;
}

static MCIERROR capability(DWORD flags, MCI_GETDEVCAPS_PARMS *p)
{
    if (!(flags & MCI_GETDEVCAPS_ITEM))
        return MCIERR_MISSING_PARAMETER;
    switch (p->dwItem) {
    case MCI_GETDEVCAPS_CAN_RECORD:
    case MCI_GETDEVCAPS_CAN_EJECT:
    case MCI_GETDEVCAPS_CAN_SAVE:
    case 0x4000: /* can lock */
    case 0x4002: /* can freeze */
    case 0x4004: /* can reverse */
    case 0x4005: /* has still */
    case 0x4008: /* can stretch input */
    case 0x4100: /* can colorkey */
        p->dwReturn = MAKELONG(0, 531);
        return MCI_RESOURCE_RETURNED;
    case MCI_GETDEVCAPS_HAS_AUDIO:
    case MCI_GETDEVCAPS_HAS_VIDEO:
    case MCI_GETDEVCAPS_USES_FILES:
    case MCI_GETDEVCAPS_COMPOUND_DEVICE:
    case MCI_GETDEVCAPS_CAN_PLAY:
    case 0x4001: /* can stretch */
    case 0x4006: /* uses palettes */
    case 0x4009: /* can test */
        p->dwReturn = MAKELONG(1, 532);
        return MCI_RESOURCE_RETURNED;
    case MCI_GETDEVCAPS_DEVICE_TYPE:
        p->dwReturn = MAKELONG(MCI_DEVTYPE_DIGITAL_VIDEO, MCI_DEVTYPE_DIGITAL_VIDEO);
        return MCI_RESOURCE_RETURNED;
    case 0x4003: /* windows */
        p->dwReturn = 1;
        return 0;
    default:
        return MCIERR_UNSUPPORTED_FUNCTION;
    }
}

static MCIERROR seek(MEDIA *m, DWORD flags, MCI_SEEK_PARMS *p)
{
    DWORD which = flags & (MCI_TO | MCI_SEEK_TO_START | MCI_SEEK_TO_END);
    if (flags & ~(which | MCI_NOTIFY | MCI_WAIT | MCI_TEST))
        return MCIERR_UNRECOGNIZED_KEYWORD;
    if (!which)
        return MCIERR_MISSING_PARAMETER;
    if (which & (which - 1))
        return MCIERR_FLAGS_NOT_COMPATIBLE;
    if ((flags & MCI_TO) && p->dwTo > length_in(m))
        return MCIERR_OUTOFRANGE;
    if (flags & MCI_TEST)
        return MMSYSERR_NOERROR;
    RtlEnterCriticalSection(&m->lock);
    settle(m, MCI_NOTIFY_ABORTED);
    stop(m, FALSE);
    m->position = which == MCI_SEEK_TO_START ? 0 : which == MCI_SEEK_TO_END ? m->size : bytes_of(m, p->dwTo);
    m->to = m->size;
    RtlLeaveCriticalSection(&m->lock);
    return MMSYSERR_NOERROR;
}

/* ------------------------------------------------------------------ the driver */

static MCIERROR command(MCIDEVICEID id, UINT message, DWORD flags, DWORD_PTR parms)
{
    MEDIA *m = (MEDIA *)mciGetDriverData(id);
    switch (message) {
    case MCI_OPEN_DRIVER:
        return open(id, flags, (MCI_OPEN_PARMSW *)parms);
    case MCI_CLOSE_DRIVER:
        if (m) {
            RtlEnterCriticalSection(&m->lock);
            stop(m, TRUE);
            RtlLeaveCriticalSection(&m->lock);
        }
        mciSetDriverData(id, 0);
        free_media(m);
        return done(nullptr, MMSYSERR_NOERROR, flags, parms);
    case MCI_GETDEVCAPS:
        return done(m, capability(flags, (MCI_GETDEVCAPS_PARMS *)parms), flags, parms);
    case MCI_INFO:
        return done(m, info(m, flags, (MCI_INFO_PARMSW *)parms), flags, parms);
    case MCI_CONFIGURE:
    case MCI_RESTORE:
        return MCIERR_UNRECOGNIZED_COMMAND;
    }
    if (!m)
        return MCIERR_UNSUPPORTED_FUNCTION;
    if (!parms)
        return MCIERR_NULL_PARAMETER_BLOCK;
    switch (message) {
    case MCI_PLAY:
        return play(m, flags, (MCI_PLAY_PARMS *)parms);
    case MCI_STOP:
        if (flags & 0x10000)
            return MCIERR_UNSUPPORTED_FUNCTION;
        RtlEnterCriticalSection(&m->lock);
        stop(m, TRUE);
        RtlLeaveCriticalSection(&m->lock);
        return done(m, MMSYSERR_NOERROR, flags, parms);
    case MCI_PAUSE:
        RtlEnterCriticalSection(&m->lock);
        if (m->mode == MODE_PLAYING && m->out)
            waveOutPause(m->out);
        m->mode = MODE_PAUSED;
        RtlLeaveCriticalSection(&m->lock);
        return done(m, MMSYSERR_NOERROR, flags, parms);
    case MCI_RESUME: {
        RtlEnterCriticalSection(&m->lock);
        MCIERROR r = MMSYSERR_NOERROR;
        if (m->mode == MODE_PAUSED && m->out) {
            waveOutRestart(m->out);
            m->mode = MODE_PLAYING;
        } else if (m->mode != MODE_PLAYING) {
            m->from = m->position >= m->size ? 0 : m->position;
            m->to = m->size;
            r = start(m);
        }
        RtlLeaveCriticalSection(&m->lock);
        SetEvent(m->event);
        return done(m, r, flags, parms);
    }
    case MCI_SEEK:
        return done(m, seek(m, flags, (MCI_SEEK_PARMS *)parms), flags, parms);
    case MCI_STATUS:
        return done(m, status(m, flags, (MCI_STATUS_PARMS *)parms), flags, parms);
    case MCI_SET:
        return done(m, set(m, flags, (MCI_SET_PARMS *)parms), flags, parms);
    case MCI_SETAUDIO:
        return done(m, setaudio(m, flags, parms), flags, parms);
    case MCI_SIGNAL:
    case MCI_SETVIDEO:
        return done(m, MMSYSERR_NOERROR, flags, parms);
    case MCI_WINDOW:
    case MCI_PUT:
    case MCI_WHERE:
    case MCI_UPDATE:
    case MCI_REALIZE:
        return MCIERR_NO_WINDOW;
    case MCI_STEP:
        return MCIERR_UNSUPPORTED_FUNCTION;
    case MCI_CUE:
        if (flags & 0x10000)
            return MCIERR_UNSUPPORTED_FUNCTION;
        return done(m, MMSYSERR_NOERROR, flags, parms);
    default:
        if (message >= MCI_FIRST && message <= MCI_LAST && message != MCI_ESCAPE)
            return MCIERR_UNSUPPORTED_FUNCTION;
        return MCIERR_UNRECOGNIZED_COMMAND;
    }
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

DECLSPEC_EXPORT LRESULT WINAPI DriverProc(DWORD_PTR id, HDRVR driver, UINT message, LPARAM param1, LPARAM param2)
{
    switch (message) {
    case DRV_LOAD:
    case DRV_FREE:
    case DRV_ENABLE:
    case DRV_DISABLE:
        return 1;
    case DRV_OPEN: {
        MCI_OPEN_DRIVER_PARMS *p = (MCI_OPEN_DRIVER_PARMS *)param2;
        if (!p)
            return 1;
        p->wType = MCI_DEVTYPE_DIGITAL_VIDEO;
        p->wCustomCommandTable = mciLoadCommandResource(g_module, L"MCIAVI", 0);
        g_tables[p->wDeviceID & 255] = p->wCustomCommandTable;
        return p->wDeviceID;
    }
    case DRV_CLOSE:
        if (id)
            mciFreeCommandResource(g_tables[id & 255]);
        return 1;
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
