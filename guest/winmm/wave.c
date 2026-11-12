/*
 * wave output through the host's devices, the default one first. each open device has a lib/audio/output.c stream and a thread
 * feeding queued headers; headers return once their last frame has played. eight-bit samples are widened; the mapper
 * converts compressed formats through msacm32. wave input reads one of the host's recording devices through a capture
 * stream of the same kind, and a thread fills the queued headers.
 */
#define MIME_BUILD_WINMM
#include "winmm_private.h"

#include "../lib/audio/devices.h"
#include "../lib/audio/output.h"

#define MAPPER_NAME L"Microsoft Sound Mapper"
#define FEED_MS 5

/* floating-point code references this symbol, normally supplied by the CRT. */
int _fltused = 0x9875;

typedef struct DONE {
    WAVEHDR *header;
    UINT64 end; /* the stream frame its last frame lands on. */
} DONE;

/* an msacm32 stream turning a compressed format into PCM, loaded when needed. */
typedef struct ACM {
    HANDLE stream;
    BYTE *in, *out;
    DWORD in_size, out_size, in_used;
    DWORD out_ready, out_offset;
    WAVEFORMATEX pcm;
} ACM;

typedef struct WAVE_OUT {
    struct WAVE_OUT *next;
    RTL_CRITICAL_SECTION lock;
    DWORD callback, flags;
    DWORD_PTR target, instance;
    WAVEFORMATEXTENSIBLE format; /* what the program writes. */
    UINT32 in_frame;             /* bytes per frame of it. */
    BOOL widen8;                 /* eight bit samples widened to sixteen. */
    BOOL mapped;                 /* through msacm32. */
    ACM acm;
    MimeOutput out;
    WAVEHDR *head, *tail;        /* queued, the first one being played from offset. */
    DWORD offset;
    WAVEHDR *loop;               /* the header a loop starts at. */
    DWORD loops;
    BOOL break_loop;
    DONE *done;
    UINT32 done_count, done_cap;
    BOOL paused;
    UINT64 base;                 /* the stream frame position zero is at. */
    WORD left, right;            /* volume. */
    HANDLE thread, wake;
    volatile BOOL quit;
} WAVE_OUT;

static RTL_CRITICAL_SECTION g_lock = {nullptr, -1, 0, nullptr, nullptr, 0};
static WAVE_OUT *g_outs;
static DWORD g_device_volume = 0xFFFFFFFF;

static HANDLE heap(void)
{
    return GetProcessHeap();
}

static struct WAVE_IN *find_in(HWAVEIN h);

static WAVE_OUT *find_out(HWAVEOUT h)
{
    RtlEnterCriticalSection(&g_lock);
    WAVE_OUT *w = g_outs;
    while (w && w != (WAVE_OUT *)h)
        w = w->next;
    RtlLeaveCriticalSection(&g_lock);
    return w;
}

/* ------------------------------------------------------------------ callbacks */

WINMMAPI LRESULT WINAPI DriverCallback(DWORD_PTR callback, DWORD flags, HANDLE device, DWORD message, DWORD_PTR user,
                                       DWORD_PTR param1, DWORD_PTR param2)
{
    switch (flags & DCB_TYPEMASK) {
    case DCB_WINDOW:
        return callback && PostMessageW((HWND)callback, message, (WPARAM)device, (LPARAM)param1);
    case DCB_TASK:
        return callback && PostThreadMessageW((DWORD)callback, message, (WPARAM)device, (LPARAM)param1);
    case DCB_FUNCTION:
        if (!callback)
            return FALSE;
        ((LPDRVCALLBACK)callback)(device, message, user, param1, param2);
        return TRUE;
    case DCB_EVENT:
        return callback && SetEvent((HANDLE)callback);
    default:
        return FALSE;
    }
}

static void notify(WAVE_OUT *w, UINT message, DWORD_PTR param1)
{
    DriverCallback(w->target, w->callback, (HANDLE)w, message, w->instance, param1, 0);
}

/* ------------------------------------------------------------------ formats */

static const GUID g_pcm = MIME_KSDATAFORMAT_SUBTYPE(WAVE_FORMAT_PCM);
static const GUID g_float = MIME_KSDATAFORMAT_SUBTYPE(WAVE_FORMAT_IEEE_FLOAT);

/* validates a PCM or float format the stream can play directly. */
static BOOL linear_format(const WAVEFORMATEX *f, UINT32 *bits, BOOL *is_float)
{
    WORD tag = f->wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE) {
        if (f->cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
            return FALSE;
        const GUID *sub = &((const WAVEFORMATEXTENSIBLE *)f)->SubFormat;
        tag = IsEqualGUID(sub, &g_pcm) ? WAVE_FORMAT_PCM : IsEqualGUID(sub, &g_float) ? WAVE_FORMAT_IEEE_FLOAT : 0;
    }
    if (tag != WAVE_FORMAT_PCM && tag != WAVE_FORMAT_IEEE_FLOAT)
        return FALSE;
    *is_float = tag == WAVE_FORMAT_IEEE_FLOAT;
    *bits = f->wBitsPerSample;
    if (*is_float ? *bits != 32 : *bits != 8 && *bits != 16 && *bits != 24 && *bits != 32)
        return FALSE;
    if (!f->nChannels || f->nChannels > 8 || f->nSamplesPerSec < 1000 || f->nSamplesPerSec > 384000 ||
        f->nBlockAlign != f->nChannels * *bits / 8)
        return FALSE;
    return TRUE;
}

/* ------------------------------------------------------------------ msacm32 */

typedef MMRESULT(WINAPI *ACM_OPEN)(HANDLE *, HANDLE, LPWAVEFORMATEX, LPWAVEFORMATEX, void *, DWORD_PTR, DWORD_PTR,
                                   DWORD);
typedef MMRESULT(WINAPI *ACM_CLOSE)(HANDLE, DWORD);
typedef MMRESULT(WINAPI *ACM_SIZE)(HANDLE, DWORD, LPDWORD, DWORD);
typedef MMRESULT(WINAPI *ACM_SUGGEST)(HANDLE, LPWAVEFORMATEX, LPWAVEFORMATEX, DWORD, DWORD);
typedef MMRESULT(WINAPI *ACM_HEADER)(HANDLE, void *, DWORD);

typedef struct ACMSTREAMHEADER_ {
    DWORD cbStruct;
    DWORD fdwStatus;
    DWORD_PTR dwUser;
    LPBYTE pbSrc;
    DWORD cbSrcLength;
    DWORD cbSrcLengthUsed;
    DWORD_PTR dwSrcUser;
    LPBYTE pbDst;
    DWORD cbDstLength;
    DWORD cbDstLengthUsed;
    DWORD_PTR dwDstUser;
    DWORD dwReservedDriver[sizeof(void *) == 8 ? 15 : 10];
} ACMSTREAMHEADER_;

static struct {
    ACM_OPEN open;
    ACM_CLOSE close;
    ACM_SIZE size;
    ACM_SUGGEST suggest;
    ACM_HEADER prepare, unprepare, convert;
} g_acm;

static BOOL load_acm(void)
{
    if (g_acm.open)
        return TRUE;
    HMODULE m = LoadLibraryW(L"msacm32.dll");
    if (!m)
        return FALSE;
    g_acm.suggest = (ACM_SUGGEST)(void *)GetProcAddress(m, "acmFormatSuggest");
    g_acm.close = (ACM_CLOSE)(void *)GetProcAddress(m, "acmStreamClose");
    g_acm.size = (ACM_SIZE)(void *)GetProcAddress(m, "acmStreamSize");
    g_acm.prepare = (ACM_HEADER)(void *)GetProcAddress(m, "acmStreamPrepareHeader");
    g_acm.unprepare = (ACM_HEADER)(void *)GetProcAddress(m, "acmStreamUnprepareHeader");
    g_acm.convert = (ACM_HEADER)(void *)GetProcAddress(m, "acmStreamConvert");
    ACM_OPEN open = (ACM_OPEN)(void *)GetProcAddress(m, "acmStreamOpen");
    if (!g_acm.suggest || !g_acm.close || !g_acm.size || !g_acm.prepare || !g_acm.unprepare || !g_acm.convert || !open)
        return FALSE;
    g_acm.open = open;
    return TRUE;
}

#define ACMERR_NOTPOSSIBLE 512
#define ACM_FORMATSUGGESTF_WFORMATTAG 0x00010000
#define ACM_STREAMCONVERTF_BLOCKALIGN 0x00000004
#define ACM_STREAMSIZEF_SOURCE 0x00000000

/* opens an msacm32 stream converting the format to PCM. */
static BOOL open_acm(ACM *a, const WAVEFORMATEX *format)
{
    if (!load_acm())
        return FALSE;
    memset(&a->pcm, 0, sizeof(a->pcm));
    a->pcm.wFormatTag = WAVE_FORMAT_PCM;
    if (g_acm.suggest(nullptr, (LPWAVEFORMATEX)format, &a->pcm, sizeof(a->pcm), ACM_FORMATSUGGESTF_WFORMATTAG))
        return FALSE;
    if (g_acm.open(&a->stream, nullptr, (LPWAVEFORMATEX)format, &a->pcm, nullptr, 0, 0, 0))
        return FALSE;
    /* a second of input at a time, whole blocks. */
    a->in_size = format->nAvgBytesPerSec ? format->nAvgBytesPerSec : 65536;
    if (format->nBlockAlign)
        a->in_size = (a->in_size + format->nBlockAlign - 1) / format->nBlockAlign * format->nBlockAlign;
    if (g_acm.size(a->stream, a->in_size, &a->out_size, ACM_STREAMSIZEF_SOURCE) || !a->out_size)
        a->out_size = a->pcm.nAvgBytesPerSec * 2;
    a->in = HeapAlloc(heap(), 0, a->in_size);
    a->out = HeapAlloc(heap(), 0, a->out_size);
    if (!a->in || !a->out) {
        g_acm.close(a->stream, 0);
        return FALSE;
    }
    return TRUE;
}

static void close_acm(ACM *a)
{
    if (a->stream)
        g_acm.close(a->stream, 0);
    HeapFree(heap(), 0, a->in);
    HeapFree(heap(), 0, a->out);
    memset(a, 0, sizeof(*a));
}

/* converts what input there is into PCM; whole blocks unless final. */
static void run_acm(ACM *a, BOOL final)
{
    ACMSTREAMHEADER_ h = {sizeof(h)};
    h.pbSrc = a->in;
    h.cbSrcLength = a->in_used;
    h.pbDst = a->out;
    h.cbDstLength = a->out_size;
    if (!a->in_used || g_acm.prepare(a->stream, &h, 0))
        return;
    g_acm.convert(a->stream, &h, final ? 0 : ACM_STREAMCONVERTF_BLOCKALIGN);
    g_acm.unprepare(a->stream, &h, 0);
    memmove(a->in, a->in + h.cbSrcLengthUsed, a->in_used - h.cbSrcLengthUsed);
    a->in_used -= h.cbSrcLengthUsed;
    a->out_ready = h.cbDstLengthUsed;
    a->out_offset = 0;
}

/* ------------------------------------------------------------------ feeding */

/* copies frames from src into the stream at dst, widening and scaling as the device does. */
static void convert(WAVE_OUT *w, BYTE *dst, const BYTE *src, UINT32 frames, UINT32 in_bits, BOOL is_float)
{
    UINT32 channels = w->format.Format.nChannels, samples = frames * channels;
    BOOL scale = w->left != w->right && channels == 2;
    float gain[2] = {w->left / 65535.0f, w->right / 65535.0f};
    if (w->widen8) {
        INT16 *d = (INT16 *)dst;
        for (UINT32 i = 0; i < samples; i++) {
            int v = ((int)src[i] - 128) << 8;
            d[i] = scale ? (INT16)(v * gain[i & 1]) : (INT16)v;
        }
        return;
    }
    memcpy(dst, src, (SIZE_T)frames * w->in_frame);
    if (!scale)
        return;
    for (UINT32 i = 0; i < samples; i++) {
        float g = gain[i & 1];
        if (is_float) {
            ((float *)dst)[i] *= g;
        } else if (in_bits == 16) {
            ((INT16 *)dst)[i] = (INT16)(((INT16 *)dst)[i] * g);
        } else if (in_bits == 32) {
            ((INT32 *)dst)[i] = (INT32)(((INT32 *)dst)[i] * (double)g);
        } else {
            BYTE *p = dst + (SIZE_T)i * 3;
            INT32 v = (INT32)((UINT32)p[0] << 8 | (UINT32)p[1] << 16 | (UINT32)p[2] << 24) >> 8;
            v = (INT32)(v * g);
            p[0] = (BYTE)v;
            p[1] = (BYTE)(v >> 8);
            p[2] = (BYTE)(v >> 16);
        }
    }
}

static void push_done(WAVE_OUT *w, WAVEHDR *h, UINT64 end)
{
    if (w->done_count == w->done_cap) {
        UINT32 cap = w->done_cap ? w->done_cap * 2 : 16;
        DONE *d = w->done ? HeapReAlloc(heap(), 0, w->done, cap * sizeof(DONE)) : HeapAlloc(heap(), 0, cap * sizeof(DONE));
        if (!d)
            return;
        w->done = d;
        w->done_cap = cap;
    }
    w->done[w->done_count++] = (DONE){h, end};
}

/*
 * completed headers in a loop stay queued until the loop ends. another iteration restarts at the first header; the
 * final iteration returns them all.
 */
static void header_finished(WAVE_OUT *w)
{
    WAVEHDR *h = w->head;
    w->offset = 0;
    if (w->loop && (h->dwFlags & WHDR_ENDLOOP) && w->loops > 1 && !w->break_loop) {
        w->loops--;
        w->head = w->loop;
        return;
    }
    w->head = h->lpNext;
    /* inside a loop the last header stays linked: what comes next is part of the loop. */
    if (w->loop && !(h->dwFlags & WHDR_ENDLOOP))
        return;
    if (!w->head)
        w->tail = nullptr;
    UINT64 end = MimeOutputWritten(&w->out);
    if (w->loop) {
        for (WAVEHDR *x = w->loop; x && x != h; x = x->lpNext)
            push_done(w, x, end);
        w->loop = nullptr;
        w->break_loop = FALSE;
    }
    push_done(w, h, end);
}

static void begin_header(WAVE_OUT *w)
{
    WAVEHDR *h = w->head;
    if (w->offset || !h)
        return;
    if ((h->dwFlags & WHDR_BEGINLOOP) && !w->loop) {
        w->loop = h;
        w->loops = h->dwLoops ? h->dwLoops : 1;
    }
}

static void feed(WAVE_OUT *w)
{
    UINT32 in_bits = w->format.Format.wBitsPerSample;
    BOOL is_float = FALSE;
    if (!w->mapped)
        linear_format(&w->format.Format, &in_bits, &is_float);
    while (w->head) {
        begin_header(w);
        WAVEHDR *h = w->head;
        UINT32 room;
        BYTE *dst = MimeOutputBegin(&w->out, &room);
        if (!room)
            break;
        if (w->mapped) {
            ACM *a = &w->acm;
            if (a->out_offset < a->out_ready) {
                UINT32 frames = (a->out_ready - a->out_offset) / w->in_frame;
                if (frames > room)
                    frames = room;
                convert(w, dst, a->out + a->out_offset, frames, in_bits, FALSE);
                MimeOutputCommit(&w->out, frames);
                a->out_offset += frames * w->in_frame;
                continue;
            }
            DWORD take = h->dwBufferLength - w->offset;
            if (take > a->in_size - a->in_used)
                take = a->in_size - a->in_used;
            memcpy(a->in + a->in_used, h->lpData + w->offset, take);
            a->in_used += take;
            w->offset += take;
            BOOL last = w->offset >= h->dwBufferLength && !h->lpNext;
            run_acm(a, last);
            if (w->offset >= h->dwBufferLength)
                header_finished(w);
            if (!a->out_ready && !take && !last)
                break;
            continue;
        }
        UINT32 frames = (h->dwBufferLength - w->offset) / w->in_frame;
        if (frames > room)
            frames = room;
        if (frames) {
            convert(w, dst, (const BYTE *)h->lpData + w->offset, frames, in_bits, is_float);
            MimeOutputCommit(&w->out, frames);
            w->offset += frames * w->in_frame;
        }
        if (w->offset + w->in_frame > h->dwBufferLength)
            header_finished(w);
    }
    if (!w->paused && MimeOutputQueued(&w->out))
        MimeOutputStart(&w->out);
}

/* moves headers whose last frame has played from done to finished. */
static UINT32 collect(WAVE_OUT *w, WAVEHDR **finished, UINT32 max, BOOL all)
{
    UINT64 played = MimeOutputPlayed(&w->out);
    UINT32 n = 0, keep = 0;
    for (UINT32 i = 0; i < w->done_count; i++) {
        if ((all || w->done[i].end <= played) && n < max)
            finished[n++] = w->done[i].header;
        else
            w->done[keep++] = w->done[i];
    }
    w->done_count = keep;
    return n;
}

static void finish(WAVE_OUT *w, WAVEHDR **finished, UINT32 n)
{
    for (UINT32 i = 0; i < n; i++) {
        WAVEHDR *h = finished[i];
        h->dwFlags = (h->dwFlags & ~WHDR_INQUEUE) | WHDR_DONE;
        notify(w, WOM_DONE, (DWORD_PTR)h);
    }
}

static DWORD WINAPI feed_thread(void *arg)
{
    WAVE_OUT *w = arg;
    WAVEHDR *finished[64];
    while (!w->quit) {
        WaitForSingleObject(w->wake, FEED_MS);
        RtlEnterCriticalSection(&w->lock);
        if (w->quit) {
            RtlLeaveCriticalSection(&w->lock);
            break;
        }
        if (!w->paused)
            feed(w);
        UINT32 n = collect(w, finished, RTL_NUMBER_OF(finished), FALSE);
        RtlLeaveCriticalSection(&w->lock);
        finish(w, finished, n);
    }
    return 0;
}

/* ------------------------------------------------------------------ devices */

static UINT32 endpoint_count(BOOL capture)
{
    MimeAudioEndpoint *all = HeapAlloc(heap(), 0, sizeof(*all) * MIME_AUDIO_ENDPOINTS);
    if (!all)
        return 0;
    UINT32 count = MimeAudioEndpoints(capture, all);
    HeapFree(heap(), 0, all);
    return count;
}

/* a device number counts the host's devices as they are now, the default one being 0. */
static BOOL endpoint_at(BOOL capture, UINT_PTR number, MimeAudioEndpoint *out)
{
    if (number >= MIME_AUDIO_ENDPOINTS)
        return FALSE;
    MimeAudioEndpoint *all = HeapAlloc(heap(), 0, sizeof(*all) * MIME_AUDIO_ENDPOINTS);
    if (!all)
        return FALSE;
    BOOL found = number < MimeAudioEndpoints(capture, all);
    if (found)
        *out = all[number];
    HeapFree(heap(), 0, all);
    return found;
}

static void fill_caps(const WCHAR *name, WAVEOUTCAPSW *c)
{
    memset(c, 0, sizeof(*c));
    c->wMid = MM_MICROSOFT;
    c->wPid = name ? MM_MSFT_WDMAUDIO_WAVEOUT : MM_WAVE_MAPPER;
    c->vDriverVersion = 0x0A00;
    lstrcpynW(c->szPname, name ? name : MAPPER_NAME, MAXPNAMELEN);
    c->dwFormats = 0x000FFFFF;
    c->wChannels = 2;
    c->dwSupport = WAVECAPS_VOLUME | WAVECAPS_LRVOLUME | WAVECAPS_SAMPLEACCURATE;
}

WINMMAPI UINT WINAPI waveOutGetNumDevs(void)
{
    return endpoint_count(FALSE);
}

/* accepts a device number, the mapper or an open device handle. the mapper has no name of its own. */
static MMRESULT caps_device(BOOL capture, UINT_PTR id, MimeAudioEndpoint *e, const WCHAR **name)
{
    *name = nullptr;
    /* the mapper stands for the devices: without any, windows tells there is no driver. */
    if (id == WAVE_MAPPER || id == (UINT_PTR)(UINT)WAVE_MAPPER)
        return endpoint_count(capture) ? MMSYSERR_NOERROR : MMSYSERR_NODRIVER;
    *name = e->name;
    if (endpoint_at(capture, id, e))
        return MMSYSERR_NOERROR;
    BOOL open = capture ? find_in((HWAVEIN)id) != nullptr : find_out((HWAVEOUT)id) != nullptr;
    return open && endpoint_at(capture, 0, e) ? MMSYSERR_NOERROR : MMSYSERR_BADDEVICEID;
}

WINMMAPI MMRESULT WINAPI waveOutGetDevCapsW(UINT_PTR id, LPWAVEOUTCAPSW caps, UINT size)
{
    MimeAudioEndpoint e;
    const WCHAR *name;
    MMRESULT r = caps_device(FALSE, id, &e, &name);
    if (r)
        return r;
    if (!caps)
        return MMSYSERR_INVALPARAM;
    WAVEOUTCAPSW c;
    fill_caps(name, &c);
    memcpy(caps, &c, size < sizeof(c) ? size : sizeof(c));
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI waveOutGetDevCapsA(UINT_PTR id, LPWAVEOUTCAPSA caps, UINT size)
{
    MimeAudioEndpoint e;
    const WCHAR *name;
    MMRESULT r = caps_device(FALSE, id, &e, &name);
    if (r)
        return r;
    if (!caps)
        return MMSYSERR_INVALPARAM;
    WAVEOUTCAPSW w;
    fill_caps(name, &w);
    WAVEOUTCAPSA c = {w.wMid, w.wPid, w.vDriverVersion};
    WideCharToMultiByte(CP_ACP, 0, w.szPname, -1, c.szPname, MAXPNAMELEN, nullptr, nullptr);
    c.dwFormats = w.dwFormats;
    c.wChannels = w.wChannels;
    c.dwSupport = w.dwSupport;
    memcpy(caps, &c, size < sizeof(c) ? size : sizeof(c));
    return MMSYSERR_NOERROR;
}

static MMRESULT check_callback(DWORD flags, DWORD_PTR callback)
{
    switch (flags & CALLBACK_TYPEMASK) {
    case CALLBACK_NULL:
    case CALLBACK_FUNCTION:
    case CALLBACK_EVENT:
    case CALLBACK_THREAD:
        return MMSYSERR_NOERROR;
    case CALLBACK_WINDOW:
        return !callback || IsWindow((HWND)callback) ? MMSYSERR_NOERROR : MMSYSERR_INVALPARAM;
    default:
        return MMSYSERR_INVALFLAG;
    }
}

WINMMAPI MMRESULT WINAPI waveOutOpen(LPHWAVEOUT out, UINT id, LPCWAVEFORMATEX format, DWORD_PTR callback,
                                     DWORD_PTR instance, DWORD flags)
{
    if (out && !(flags & WAVE_FORMAT_QUERY))
        *out = nullptr;
    MimeAudioEndpoint device = {0};
    if (id != WAVE_MAPPER && !endpoint_at(FALSE, id, &device)) {
        /* an open device's handle names its device. */
        if (!find_out((HWAVEOUT)(UINT_PTR)id))
            return MMSYSERR_BADDEVICEID;
    }
    if (id == WAVE_MAPPER && !endpoint_count(FALSE))
        return MMSYSERR_BADDEVICEID;
    if (!format)
        return MMSYSERR_INVALPARAM;
    MMRESULT r = check_callback(flags, callback);
    if (r)
        return r;
    if (!out && !(flags & WAVE_FORMAT_QUERY))
        return MMSYSERR_INVALPARAM;
    UINT32 bits;
    BOOL is_float, linear = linear_format(format, &bits, &is_float);
    BOOL mapper = id == WAVE_MAPPER || (flags & WAVE_MAPPED);
    if (!linear && (!mapper || (flags & WAVE_FORMAT_DIRECT)))
        return WAVERR_BADFORMAT;
    if (!linear && format->wFormatTag == WAVE_FORMAT_PCM)
        return WAVERR_BADFORMAT;

    WAVE_OUT *w = HeapAlloc(heap(), HEAP_ZERO_MEMORY, sizeof(*w));
    if (!w)
        return MMSYSERR_NOMEM;
    WORD extra = format->wFormatTag == WAVE_FORMAT_PCM ? 0 : format->cbSize;
    if (extra > sizeof(w->format) - sizeof(WAVEFORMATEX))
        extra = sizeof(w->format) - sizeof(WAVEFORMATEX);
    memcpy(&w->format, format, sizeof(WAVEFORMATEX) + extra);
    if (format->wFormatTag == WAVE_FORMAT_PCM)
        w->format.Format.cbSize = 0;
    if (!linear) {
        if (!open_acm(&w->acm, format)) {
            HeapFree(heap(), 0, w);
            return WAVERR_BADFORMAT;
        }
        w->mapped = TRUE;
        bits = w->acm.pcm.wBitsPerSample;
        is_float = FALSE;
        memcpy(&w->format, &w->acm.pcm, sizeof(WAVEFORMATEX));
    }
    if (flags & WAVE_FORMAT_QUERY) {
        if (w->mapped)
            close_acm(&w->acm);
        HeapFree(heap(), 0, w);
        return MMSYSERR_NOERROR;
    }
    w->in_frame = w->format.Format.nBlockAlign;
    w->widen8 = bits == 8;
    NTSTATUS status = MimeOutputOpen(&w->out, device.host, w->format.Format.nSamplesPerSec, w->format.Format.nChannels,
                                     w->widen8 ? 16 : bits, is_float);
    if (!NT_SUCCESS(status)) {
        if (w->mapped)
            close_acm(&w->acm);
        HeapFree(heap(), 0, w);
        return status == STATUS_NO_SUCH_DEVICE ? MMSYSERR_BADDEVICEID : MMSYSERR_ALLOCATED;
    }
    w->callback = (flags & CALLBACK_TYPEMASK) >> 16;
    w->flags = flags;
    w->target = callback;
    w->instance = instance;
    w->left = w->right = 0xFFFF;
    RtlInitializeCriticalSection(&w->lock);
    w->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    w->thread = CreateThread(nullptr, 0, feed_thread, w, 0, nullptr);
    if (!w->wake || !w->thread) {
        if (w->wake)
            CloseHandle(w->wake);
        MimeOutputClose(&w->out);
        if (w->mapped)
            close_acm(&w->acm);
        RtlDeleteCriticalSection(&w->lock);
        HeapFree(heap(), 0, w);
        return MMSYSERR_NOMEM;
    }
    SetThreadPriority(w->thread, THREAD_PRIORITY_TIME_CRITICAL);
    RtlEnterCriticalSection(&g_lock);
    w->next = g_outs;
    g_outs = w;
    RtlLeaveCriticalSection(&g_lock);
    *out = (HWAVEOUT)w;
    notify(w, WOM_OPEN, 0);
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI waveOutClose(HWAVEOUT handle)
{
    WAVE_OUT *w = find_out(handle);
    if (!w)
        return MMSYSERR_INVALHANDLE;
    RtlEnterCriticalSection(&w->lock);
    BOOL playing = w->head || w->done_count;
    RtlLeaveCriticalSection(&w->lock);
    if (playing)
        return WAVERR_STILLPLAYING;
    RtlEnterCriticalSection(&g_lock);
    for (WAVE_OUT **p = &g_outs; *p; p = &(*p)->next) {
        if (*p == w) {
            *p = w->next;
            break;
        }
    }
    RtlLeaveCriticalSection(&g_lock);
    w->quit = TRUE;
    SetEvent(w->wake);
    if (GetCurrentThreadId() != GetThreadId(w->thread))
        WaitForSingleObject(w->thread, INFINITE);
    CloseHandle(w->thread);
    CloseHandle(w->wake);
    MimeOutputClose(&w->out);
    if (w->mapped)
        close_acm(&w->acm);
    notify(w, WOM_CLOSE, 0);
    RtlDeleteCriticalSection(&w->lock);
    HeapFree(heap(), 0, w->done);
    HeapFree(heap(), 0, w);
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI waveOutPrepareHeader(HWAVEOUT handle, LPWAVEHDR h, UINT size)
{
    if (!find_out(handle))
        return MMSYSERR_INVALHANDLE;
    if (!h || size < sizeof(WAVEHDR) || !h->lpData)
        return MMSYSERR_INVALPARAM;
    if (h->dwFlags & WHDR_INQUEUE)
        return WAVERR_STILLPLAYING;
    h->dwFlags |= WHDR_PREPARED;
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI waveOutUnprepareHeader(HWAVEOUT handle, LPWAVEHDR h, UINT size)
{
    if (!find_out(handle))
        return MMSYSERR_INVALHANDLE;
    if (!h || size < sizeof(WAVEHDR))
        return MMSYSERR_INVALPARAM;
    if (h->dwFlags & WHDR_INQUEUE)
        return WAVERR_STILLPLAYING;
    h->dwFlags &= ~WHDR_PREPARED;
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI waveOutWrite(HWAVEOUT handle, LPWAVEHDR h, UINT size)
{
    WAVE_OUT *w = find_out(handle);
    if (!w)
        return MMSYSERR_INVALHANDLE;
    if (!h || size < sizeof(WAVEHDR) || !h->lpData)
        return MMSYSERR_INVALPARAM;
    if (!(h->dwFlags & WHDR_PREPARED))
        return WAVERR_UNPREPARED;
    if (h->dwFlags & WHDR_INQUEUE)
        return WAVERR_STILLPLAYING;
    h->dwFlags = (h->dwFlags & ~WHDR_DONE) | WHDR_INQUEUE;
    h->lpNext = nullptr;
    RtlEnterCriticalSection(&w->lock);
    if (w->tail)
        w->tail->lpNext = h;
    if (!w->head)
        w->head = h;
    w->tail = h;
    if (!w->paused)
        feed(w);
    RtlLeaveCriticalSection(&w->lock);
    SetEvent(w->wake);
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI waveOutPause(HWAVEOUT handle)
{
    WAVE_OUT *w = find_out(handle);
    if (!w)
        return MMSYSERR_INVALHANDLE;
    RtlEnterCriticalSection(&w->lock);
    w->paused = TRUE;
    MimeOutputStop(&w->out);
    RtlLeaveCriticalSection(&w->lock);
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI waveOutRestart(HWAVEOUT handle)
{
    WAVE_OUT *w = find_out(handle);
    if (!w)
        return MMSYSERR_INVALHANDLE;
    RtlEnterCriticalSection(&w->lock);
    w->paused = FALSE;
    feed(w);
    RtlLeaveCriticalSection(&w->lock);
    SetEvent(w->wake);
    return MMSYSERR_NOERROR;
}

/* stops playback, returns every queued header and resets the position to zero. */
WINMMAPI MMRESULT WINAPI waveOutReset(HWAVEOUT handle)
{
    WAVE_OUT *w = find_out(handle);
    if (!w)
        return MMSYSERR_INVALHANDLE;
    RtlEnterCriticalSection(&w->lock);
    MimeOutputStop(&w->out);
    MimeOutputDrop(&w->out);
    w->base = MimeOutputPlayed(&w->out);
    for (WAVEHDR *h = w->loop; h && h != w->head; h = h->lpNext)
        push_done(w, h, 0);
    for (WAVEHDR *h = w->head; h; h = h->lpNext)
        push_done(w, h, 0);
    w->head = w->tail = nullptr;
    w->offset = 0;
    w->loop = nullptr;
    w->break_loop = FALSE;
    if (w->mapped)
        w->acm.in_used = w->acm.out_ready = w->acm.out_offset = 0;
    WAVEHDR *finished[256];
    UINT32 n = collect(w, finished, RTL_NUMBER_OF(finished), TRUE);
    RtlLeaveCriticalSection(&w->lock);
    finish(w, finished, n);
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI waveOutBreakLoop(HWAVEOUT handle)
{
    WAVE_OUT *w = find_out(handle);
    if (!w)
        return MMSYSERR_INVALHANDLE;
    RtlEnterCriticalSection(&w->lock);
    if (w->loop)
        w->break_loop = TRUE;
    RtlLeaveCriticalSection(&w->lock);
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI waveOutGetPosition(HWAVEOUT handle, LPMMTIME time, UINT size)
{
    WAVE_OUT *w = find_out(handle);
    if (!w)
        return MMSYSERR_INVALHANDLE;
    if (!time || size < sizeof(MMTIME))
        return MMSYSERR_INVALPARAM;
    RtlEnterCriticalSection(&w->lock);
    UINT64 frames = MimeOutputPlayed(&w->out) - w->base;
    UINT32 rate = w->format.Format.nSamplesPerSec;
    RtlLeaveCriticalSection(&w->lock);
    switch (time->wType) {
    case TIME_SAMPLES:
        time->u.sample = (DWORD)frames;
        break;
    case TIME_MS:
        time->u.ms = (DWORD)(frames * 1000 / rate);
        break;
    case TIME_SMPTE: {
        UINT64 ms = frames * 1000 / rate;
        time->u.smpte.hour = (BYTE)(ms / 3600000);
        time->u.smpte.min = (BYTE)(ms / 60000 % 60);
        time->u.smpte.sec = (BYTE)(ms / 1000 % 60);
        time->u.smpte.frame = (BYTE)(ms % 1000 * 30 / 1000);
        time->u.smpte.fps = 30;
        time->u.smpte.dummy = 0;
        break;
    }
    default:
        /* unsupported position formats fall back to bytes supplied by the application. */
        time->wType = TIME_BYTES;
        time->u.cb = (DWORD)(frames * w->format.Format.nBlockAlign);
        break;
    }
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI waveOutGetVolume(HWAVEOUT handle, LPDWORD volume)
{
    if (!volume)
        return MMSYSERR_INVALPARAM;
    if (!handle || handle == (HWAVEOUT)(UINT_PTR)WAVE_MAPPER) {
        *volume = g_device_volume;
        return MMSYSERR_NOERROR;
    }
    WAVE_OUT *w = find_out(handle);
    if (!w)
        return MMSYSERR_INVALHANDLE;
    *volume = (DWORD)w->left | (DWORD)w->right << 16;
    return MMSYSERR_NOERROR;
}

static void apply_volume(WAVE_OUT *w)
{
    WORD level = w->left > w->right ? w->left : w->right;
    float device = ((g_device_volume & 0xFFFF) > (g_device_volume >> 16) ? (g_device_volume & 0xFFFF)
                                                                            : (g_device_volume >> 16)) / 65535.0f;
    /*
     * equal channel volumes scale on the host; unequal ones scale while feeding, leaving the device volume on the
     * host.
     */
    MimeOutputVolume(&w->out, (w->left == w->right ? level / 65535.0f : 1.0f) * device);
}

WINMMAPI MMRESULT WINAPI waveOutSetVolume(HWAVEOUT handle, DWORD volume)
{
    if (!handle || handle == (HWAVEOUT)(UINT_PTR)WAVE_MAPPER) {
        g_device_volume = volume;
        RtlEnterCriticalSection(&g_lock);
        for (WAVE_OUT *w = g_outs; w; w = w->next)
            apply_volume(w);
        RtlLeaveCriticalSection(&g_lock);
        return MMSYSERR_NOERROR;
    }
    WAVE_OUT *w = find_out(handle);
    if (!w)
        return MMSYSERR_INVALHANDLE;
    RtlEnterCriticalSection(&w->lock);
    w->left = (WORD)volume;
    w->right = (WORD)(volume >> 16);
    apply_volume(w);
    RtlLeaveCriticalSection(&w->lock);
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI waveOutGetPitch(HWAVEOUT handle, LPDWORD pitch)
{
    return find_out(handle) ? MMSYSERR_NOTSUPPORTED : MMSYSERR_INVALHANDLE;
}

WINMMAPI MMRESULT WINAPI waveOutSetPitch(HWAVEOUT handle, DWORD pitch)
{
    return find_out(handle) ? MMSYSERR_NOTSUPPORTED : MMSYSERR_INVALHANDLE;
}

WINMMAPI MMRESULT WINAPI waveOutGetPlaybackRate(HWAVEOUT handle, LPDWORD rate)
{
    return find_out(handle) ? MMSYSERR_NOTSUPPORTED : MMSYSERR_INVALHANDLE;
}

WINMMAPI MMRESULT WINAPI waveOutSetPlaybackRate(HWAVEOUT handle, DWORD rate)
{
    return find_out(handle) ? MMSYSERR_NOTSUPPORTED : MMSYSERR_INVALHANDLE;
}

WINMMAPI MMRESULT WINAPI waveOutGetID(HWAVEOUT handle, LPUINT id)
{
    if (!id)
        return MMSYSERR_INVALPARAM;
    if (!find_out(handle))
        return MMSYSERR_INVALHANDLE;
    *id = 0;
    return MMSYSERR_NOERROR;
}

#define ENDPOINT_ID_CHARS 56
#define INTERFACE_CHARS 112

/*
 * answers the messages that tell which endpoint of the audio stack a device is: its id there, and its device
 * interface path. programs pass a device number where the handle goes.
 */
static MMRESULT endpoint_message(BOOL capture, UINT_PTR handle, BOOL open, UINT message, DWORD_PTR param1,
                                 DWORD_PTR param2)
{
    MimeAudioEndpoint e;
    if (!endpoint_at(capture, open || handle == (UINT_PTR)(UINT)WAVE_MAPPER ? 0 : handle, &e))
        return MMSYSERR_INVALHANDLE;
    WCHAR id[ENDPOINT_ID_CHARS], path[INTERFACE_CHARS];
    MimeAudioEndpointId(capture, &e.guid, id);
    lstrcpyW(path, L"\\?\SWD#MMDEVAPI#");
    lstrcatW(path, id);
    lstrcatW(path, capture ? L"#{2eef81be-33fa-4800-9670-1cd474972c3f}" : L"#{e6327cad-dcec-4949-ae8a-991e976a79d2}");
    const WCHAR *text = message == DRV_QUERYDEVICEINTERFACESIZE || message == DRV_QUERYDEVICEINTERFACE ? path : id;
    ULONG bytes = (ULONG)(lstrlenW(text) + 1) * sizeof(WCHAR);
    switch (message) {
    case DRVM_MAPPER_PREFERRED_GET:
    case DRVM_MAPPER_CONSOLEVOICECOM_GET:
        /* the default device is the first one. */
        if (param1)
            *(DWORD *)param1 = 0;
        if (param2)
            *(DWORD *)param2 = 0;
        return MMSYSERR_NOERROR;
    case DRV_QUERYDEVICEINTERFACESIZE:
    case DRV_QUERYFUNCTIONINSTANCEIDSIZE:
        if (!param1)
            return MMSYSERR_INVALPARAM;
        *(ULONG *)param1 = bytes;
        return MMSYSERR_NOERROR;
    case DRV_QUERYDEVICEINTERFACE:
    case DRV_QUERYFUNCTIONINSTANCEID:
        if (!param1 || param2 < bytes)
            return MMSYSERR_INVALPARAM;
        memcpy((void *)param1, text, bytes);
        return MMSYSERR_NOERROR;
    default:
        return MMSYSERR_NOTSUPPORTED;
    }
}

WINMMAPI MMRESULT WINAPI waveOutMessage(HWAVEOUT handle, UINT message, DWORD_PTR param1, DWORD_PTR param2)
{
    return endpoint_message(FALSE, (UINT_PTR)handle, find_out(handle) != nullptr, message, param1, param2);
}

WINMMAPI MMRESULT WINAPI waveOutGetErrorTextW(MMRESULT error, LPWSTR text, UINT size)
{
    return MmErrorTextW(error, text, size);
}

WINMMAPI MMRESULT WINAPI waveOutGetErrorTextA(MMRESULT error, LPSTR text, UINT size)
{
    return MmErrorTextA(error, text, size);
}

/* ------------------------------------------------------------------ input */


typedef struct WAVE_IN {
    struct WAVE_IN *next;
    RTL_CRITICAL_SECTION lock;
    DWORD callback;
    DWORD_PTR target, instance;
    WAVEFORMATEX format;
    BOOL narrow8;           /* recorded in sixteen bits, handed over in eight. */
    MimeAudioHeader *hdr;
    BYTE *ring;
    ULONG slot;
    UINT32 ring_frames, ring_frame; /* capacity, and bytes per frame of the ring. */
    WAVEHDR *head, *tail;   /* queued, the first one being filled. */
    BOOL recording;
    UINT64 recorded;        /* frames handed over since the last reset. */
    HANDLE thread, wake;
    volatile BOOL quit;
} WAVE_IN;

static WAVE_IN *g_ins;

static WAVE_IN *find_in(HWAVEIN h)
{
    RtlEnterCriticalSection(&g_lock);
    WAVE_IN *w = g_ins;
    while (w && w != (WAVE_IN *)h)
        w = w->next;
    RtlLeaveCriticalSection(&g_lock);
    return w;
}

static void notify_in(WAVE_IN *w, UINT message, DWORD_PTR param1)
{
    DriverCallback(w->target, w->callback, (HANDLE)w, message, w->instance, param1, 0);
}

static WAVEHDR *pop_in(WAVE_IN *w)
{
    WAVEHDR *h = w->head;
    w->head = h->lpNext;
    if (!w->head)
        w->tail = nullptr;
    h->lpNext = nullptr;
    return h;
}

/* moves what the host recorded into the queued headers. without a header the frames are lost, as on windows. */
static UINT32 drain(WAVE_IN *w, WAVEHDR **finished, UINT32 max)
{
    UINT32 n = 0, out_frame = w->format.nBlockAlign;
    UINT64 written = __atomic_load_n(&w->hdr->written, __ATOMIC_ACQUIRE), played = w->hdr->played;
    if (written - played > w->ring_frames)
        played = written - w->ring_frames;
    while (played != written && w->head && n < max) {
        WAVEHDR *h = w->head;
        UINT32 start = (UINT32)(played % w->ring_frames);
        UINT32 frames = (h->dwBufferLength - h->dwBytesRecorded) / out_frame;
        if (frames > written - played)
            frames = (UINT32)(written - played);
        if (frames > w->ring_frames - start)
            frames = w->ring_frames - start;
        const BYTE *src = w->ring + (SIZE_T)start * w->ring_frame;
        BYTE *dst = (BYTE *)h->lpData + h->dwBytesRecorded;
        if (w->narrow8) {
            UINT32 samples = frames * w->format.nChannels;
            for (UINT32 i = 0; i < samples; i++)
                dst[i] = (BYTE)((((const INT16 *)src)[i] >> 8) + 128);
        } else {
            memcpy(dst, src, (SIZE_T)frames * out_frame);
        }
        h->dwBytesRecorded += frames * out_frame;
        played += frames;
        w->recorded += frames;
        if (h->dwBytesRecorded + out_frame > h->dwBufferLength)
            finished[n++] = pop_in(w);
    }
    if (!w->head)
        played = written;
    __atomic_store_n(&w->hdr->played, played, __ATOMIC_RELEASE);
    return n;
}

static void finish_in(WAVE_IN *w, WAVEHDR **finished, UINT32 n)
{
    for (UINT32 i = 0; i < n; i++) {
        WAVEHDR *h = finished[i];
        h->dwFlags = (h->dwFlags & ~WHDR_INQUEUE) | WHDR_DONE;
        notify_in(w, WIM_DATA, (DWORD_PTR)h);
    }
}

static DWORD WINAPI record_thread(void *arg)
{
    WAVE_IN *w = arg;
    WAVEHDR *finished[64];
    while (!w->quit) {
        WaitForSingleObject(w->wake, FEED_MS);
        RtlEnterCriticalSection(&w->lock);
        UINT32 n = w->recording && !w->quit ? drain(w, finished, RTL_NUMBER_OF(finished)) : 0;
        RtlLeaveCriticalSection(&w->lock);
        finish_in(w, finished, n);
    }
    return 0;
}

static void fill_in_caps(const WCHAR *name, WAVEINCAPSW *c)
{
    memset(c, 0, sizeof(*c));
    c->wMid = MM_MICROSOFT;
    c->wPid = name ? MM_MSFT_WDMAUDIO_WAVEIN : MM_WAVE_MAPPER;
    c->vDriverVersion = 0x0A00;
    lstrcpynW(c->szPname, name ? name : MAPPER_NAME, MAXPNAMELEN);
    c->dwFormats = 0x000FFFFF;
    c->wChannels = 2;
}

WINMMAPI UINT WINAPI waveInGetNumDevs(void)
{
    return endpoint_count(TRUE);
}

WINMMAPI MMRESULT WINAPI waveInGetDevCapsW(UINT_PTR id, LPWAVEINCAPSW caps, UINT size)
{
    MimeAudioEndpoint e;
    const WCHAR *name;
    MMRESULT r = caps_device(TRUE, id, &e, &name);
    if (r)
        return r;
    if (!caps)
        return MMSYSERR_INVALPARAM;
    WAVEINCAPSW c;
    fill_in_caps(name, &c);
    memcpy(caps, &c, size < sizeof(c) ? size : sizeof(c));
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI waveInGetDevCapsA(UINT_PTR id, LPWAVEINCAPSA caps, UINT size)
{
    MimeAudioEndpoint e;
    const WCHAR *name;
    MMRESULT r = caps_device(TRUE, id, &e, &name);
    if (r)
        return r;
    if (!caps)
        return MMSYSERR_INVALPARAM;
    WAVEINCAPSW w;
    fill_in_caps(name, &w);
    WAVEINCAPSA c = {w.wMid, w.wPid, w.vDriverVersion};
    WideCharToMultiByte(CP_ACP, 0, w.szPname, -1, c.szPname, MAXPNAMELEN, nullptr, nullptr);
    c.dwFormats = w.dwFormats;
    c.wChannels = w.wChannels;
    memcpy(caps, &c, size < sizeof(c) ? size : sizeof(c));
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI waveInOpen(LPHWAVEIN in, UINT id, LPCWAVEFORMATEX format, DWORD_PTR callback,
                                    DWORD_PTR instance, DWORD flags)
{
    if (in && !(flags & WAVE_FORMAT_QUERY))
        *in = nullptr;
    MimeAudioEndpoint device = {0};
    if (id != WAVE_MAPPER && !endpoint_at(TRUE, id, &device) && !find_in((HWAVEIN)(UINT_PTR)id))
        return MMSYSERR_BADDEVICEID;
    if (id == WAVE_MAPPER && !endpoint_count(TRUE))
        return MMSYSERR_BADDEVICEID;
    if (!format)
        return MMSYSERR_INVALPARAM;
    MMRESULT r = check_callback(flags, callback);
    if (r)
        return r;
    if (!in && !(flags & WAVE_FORMAT_QUERY))
        return MMSYSERR_INVALPARAM;
    UINT32 bits;
    BOOL is_float;
    if (!linear_format(format, &bits, &is_float))
        return WAVERR_BADFORMAT;
    if (flags & WAVE_FORMAT_QUERY)
        return MMSYSERR_NOERROR;

    WAVE_IN *w = HeapAlloc(heap(), HEAP_ZERO_MEMORY, sizeof(*w));
    if (!w)
        return MMSYSERR_NOMEM;
    w->format = *format;
    w->narrow8 = bits == 8;
    UINT32 ring_bits = w->narrow8 ? 16 : bits;
    PVOID base;
    NTSTATUS status = NtGdiMimeAudioCreate(&base, &w->slot);
    if (NT_SUCCESS(status)) {
        MimeAudioHeader *h = base;
        w->ring_frame = format->nChannels * ring_bits / 8;
        w->ring_frames = (MIME_AUDIO_STREAM_SIZE - MIME_AUDIO_HEADER_SIZE) / w->ring_frame;
        h->rate = format->nSamplesPerSec;
        h->channels = format->nChannels;
        h->bits = ring_bits;
        h->is_float = is_float;
        h->frames = w->ring_frames;
        h->volume = 65536;
        h->capture = 1;
        h->device = device.host;
        status = NtGdiMimeAudioControl(w->slot, MIME_AUDIO_OPEN);
        if (!NT_SUCCESS(status))
            NtGdiMimeAudioControl(w->slot, MIME_AUDIO_CLOSE);
        w->hdr = h;
        w->ring = (BYTE *)base + MIME_AUDIO_HEADER_SIZE;
    }
    if (!NT_SUCCESS(status)) {
        HeapFree(heap(), 0, w);
        return status == STATUS_NO_SUCH_DEVICE ? MMSYSERR_BADDEVICEID : MMSYSERR_ALLOCATED;
    }
    w->callback = (flags & CALLBACK_TYPEMASK) >> 16;
    w->target = callback;
    w->instance = instance;
    RtlInitializeCriticalSection(&w->lock);
    w->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    w->thread = w->wake ? CreateThread(nullptr, 0, record_thread, w, 0, nullptr) : nullptr;
    if (!w->thread) {
        if (w->wake)
            CloseHandle(w->wake);
        NtGdiMimeAudioControl(w->slot, MIME_AUDIO_CLOSE);
        RtlDeleteCriticalSection(&w->lock);
        HeapFree(heap(), 0, w);
        return MMSYSERR_NOMEM;
    }
    SetThreadPriority(w->thread, THREAD_PRIORITY_TIME_CRITICAL);
    RtlEnterCriticalSection(&g_lock);
    w->next = g_ins;
    g_ins = w;
    RtlLeaveCriticalSection(&g_lock);
    *in = (HWAVEIN)w;
    notify_in(w, WIM_OPEN, 0);
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI waveInClose(HWAVEIN handle)
{
    WAVE_IN *w = find_in(handle);
    if (!w)
        return MMSYSERR_INVALHANDLE;
    RtlEnterCriticalSection(&w->lock);
    BOOL queued = w->head != nullptr;
    RtlLeaveCriticalSection(&w->lock);
    if (queued)
        return WAVERR_STILLPLAYING;
    RtlEnterCriticalSection(&g_lock);
    for (WAVE_IN **p = &g_ins; *p; p = &(*p)->next) {
        if (*p == w) {
            *p = w->next;
            break;
        }
    }
    RtlLeaveCriticalSection(&g_lock);
    w->quit = TRUE;
    SetEvent(w->wake);
    if (GetCurrentThreadId() != GetThreadId(w->thread))
        WaitForSingleObject(w->thread, INFINITE);
    CloseHandle(w->thread);
    CloseHandle(w->wake);
    NtGdiMimeAudioControl(w->slot, MIME_AUDIO_STOP);
    NtGdiMimeAudioControl(w->slot, MIME_AUDIO_CLOSE);
    notify_in(w, WIM_CLOSE, 0);
    RtlDeleteCriticalSection(&w->lock);
    HeapFree(heap(), 0, w);
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI waveInPrepareHeader(HWAVEIN handle, LPWAVEHDR h, UINT size)
{
    if (!find_in(handle))
        return MMSYSERR_INVALHANDLE;
    if (!h || size < sizeof(WAVEHDR) || !h->lpData)
        return MMSYSERR_INVALPARAM;
    if (h->dwFlags & WHDR_INQUEUE)
        return WAVERR_STILLPLAYING;
    h->dwFlags |= WHDR_PREPARED;
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI waveInUnprepareHeader(HWAVEIN handle, LPWAVEHDR h, UINT size)
{
    if (!find_in(handle))
        return MMSYSERR_INVALHANDLE;
    if (!h || size < sizeof(WAVEHDR))
        return MMSYSERR_INVALPARAM;
    if (h->dwFlags & WHDR_INQUEUE)
        return WAVERR_STILLPLAYING;
    h->dwFlags &= ~WHDR_PREPARED;
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI waveInAddBuffer(HWAVEIN handle, LPWAVEHDR h, UINT size)
{
    WAVE_IN *w = find_in(handle);
    if (!w)
        return MMSYSERR_INVALHANDLE;
    if (!h || size < sizeof(WAVEHDR) || !h->lpData)
        return MMSYSERR_INVALPARAM;
    if (!(h->dwFlags & WHDR_PREPARED))
        return WAVERR_UNPREPARED;
    if (h->dwFlags & WHDR_INQUEUE)
        return WAVERR_STILLPLAYING;
    h->dwFlags = (h->dwFlags & ~WHDR_DONE) | WHDR_INQUEUE;
    h->dwBytesRecorded = 0;
    h->lpNext = nullptr;
    RtlEnterCriticalSection(&w->lock);
    if (w->tail)
        w->tail->lpNext = h;
    else
        w->head = h;
    w->tail = h;
    RtlLeaveCriticalSection(&w->lock);
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI waveInStart(HWAVEIN handle)
{
    WAVE_IN *w = find_in(handle);
    if (!w)
        return MMSYSERR_INVALHANDLE;
    RtlEnterCriticalSection(&w->lock);
    if (!w->recording) {
        /* what was recorded before the stop is not part of this run. */
        __atomic_store_n(&w->hdr->played, __atomic_load_n(&w->hdr->written, __ATOMIC_ACQUIRE), __ATOMIC_RELEASE);
        NtGdiMimeAudioControl(w->slot, MIME_AUDIO_START);
        w->recording = TRUE;
    }
    RtlLeaveCriticalSection(&w->lock);
    return MMSYSERR_NOERROR;
}

/* stops recording. the header being filled comes back with what it has, all of them on a reset. */
static MMRESULT stop_in(HWAVEIN handle, BOOL reset)
{
    WAVE_IN *w = find_in(handle);
    if (!w)
        return MMSYSERR_INVALHANDLE;
    WAVEHDR *finished[256];
    UINT32 n = 0;
    RtlEnterCriticalSection(&w->lock);
    if (w->recording) {
        n = drain(w, finished, RTL_NUMBER_OF(finished) / 2);
        NtGdiMimeAudioControl(w->slot, MIME_AUDIO_STOP);
        w->recording = FALSE;
    }
    if (w->head && (reset || w->head->dwBytesRecorded))
        finished[n++] = pop_in(w);
    while (reset && w->head && n < RTL_NUMBER_OF(finished))
        finished[n++] = pop_in(w);
    if (reset)
        w->recorded = 0;
    RtlLeaveCriticalSection(&w->lock);
    finish_in(w, finished, n);
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI waveInStop(HWAVEIN handle)
{
    return stop_in(handle, FALSE);
}

WINMMAPI MMRESULT WINAPI waveInReset(HWAVEIN handle)
{
    return stop_in(handle, TRUE);
}

WINMMAPI MMRESULT WINAPI waveInGetPosition(HWAVEIN handle, LPMMTIME time, UINT size)
{
    WAVE_IN *w = find_in(handle);
    if (!w)
        return MMSYSERR_INVALHANDLE;
    if (!time || size < sizeof(MMTIME))
        return MMSYSERR_INVALPARAM;
    RtlEnterCriticalSection(&w->lock);
    UINT64 frames = w->recorded;
    RtlLeaveCriticalSection(&w->lock);
    switch (time->wType) {
    case TIME_SAMPLES:
        time->u.sample = (DWORD)frames;
        break;
    case TIME_MS:
        time->u.ms = (DWORD)(frames * 1000 / w->format.nSamplesPerSec);
        break;
    default:
        time->wType = TIME_BYTES;
        time->u.cb = (DWORD)(frames * w->format.nBlockAlign);
        break;
    }
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI waveInGetID(HWAVEIN handle, LPUINT id)
{
    if (!id)
        return MMSYSERR_INVALPARAM;
    if (!find_in(handle))
        return MMSYSERR_INVALHANDLE;
    *id = 0;
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI waveInMessage(HWAVEIN handle, UINT message, DWORD_PTR param1, DWORD_PTR param2)
{
    return endpoint_message(TRUE, (UINT_PTR)handle, find_in(handle) != nullptr, message, param1, param2);
}
