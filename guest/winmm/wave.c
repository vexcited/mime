/*
 * wave output through the host's default device. each open device has a lib/audio/output.c stream and a thread
 * feeding queued headers; headers return once their last frame has played. eight-bit samples are widened; the mapper
 * converts compressed formats through msacm32. there is no input device.
 */
#define MIME_BUILD_WINMM
#include "winmm_private.h"

#include "../lib/audio/output.h"

#if defined(__aarch64__)
#define DEVICE_NAME L"Speakers (Qualcomm Audio)"
#else
#define DEVICE_NAME L"Speakers (Realtek(R) Audio)"
#endif
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

static void fill_caps(BOOL mapper, WAVEOUTCAPSW *c)
{
    memset(c, 0, sizeof(*c));
    c->wMid = MM_MICROSOFT;
    c->wPid = mapper ? MM_WAVE_MAPPER : MM_MSFT_WDMAUDIO_WAVEOUT;
    c->vDriverVersion = 0x0A00;
    lstrcpynW(c->szPname, mapper ? MAPPER_NAME : DEVICE_NAME, MAXPNAMELEN);
    c->dwFormats = 0x000FFFFF;
    c->wChannels = 2;
    c->dwSupport = WAVECAPS_VOLUME | WAVECAPS_LRVOLUME | WAVECAPS_SAMPLEACCURATE;
}

WINMMAPI UINT WINAPI waveOutGetNumDevs(void)
{
    return 1;
}

/* accepts a device number, the mapper or an open device handle. */
static MMRESULT caps_device(UINT_PTR id, BOOL *mapper)
{
    *mapper = id == WAVE_MAPPER || id == (UINT_PTR)(UINT)WAVE_MAPPER;
    if (*mapper || id == 0)
        return MMSYSERR_NOERROR;
    return find_out((HWAVEOUT)id) ? MMSYSERR_NOERROR : MMSYSERR_BADDEVICEID;
}

WINMMAPI MMRESULT WINAPI waveOutGetDevCapsW(UINT_PTR id, LPWAVEOUTCAPSW caps, UINT size)
{
    BOOL mapper;
    MMRESULT r = caps_device(id, &mapper);
    if (r)
        return r;
    if (!caps)
        return MMSYSERR_INVALPARAM;
    WAVEOUTCAPSW c;
    fill_caps(mapper, &c);
    memcpy(caps, &c, size < sizeof(c) ? size : sizeof(c));
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI waveOutGetDevCapsA(UINT_PTR id, LPWAVEOUTCAPSA caps, UINT size)
{
    BOOL mapper;
    MMRESULT r = caps_device(id, &mapper);
    if (r)
        return r;
    if (!caps)
        return MMSYSERR_INVALPARAM;
    WAVEOUTCAPSW w;
    fill_caps(mapper, &w);
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
    if (id != 0 && id != WAVE_MAPPER) {
        /* an open device's handle names its device. */
        if (!find_out((HWAVEOUT)(UINT_PTR)id))
            return MMSYSERR_BADDEVICEID;
    }
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
    if (!NT_SUCCESS(MimeOutputOpen(&w->out, w->format.Format.nSamplesPerSec, w->format.Format.nChannels,
                                   w->widen8 ? 16 : bits, is_float))) {
        if (w->mapped)
            close_acm(&w->acm);
        HeapFree(heap(), 0, w);
        return MMSYSERR_ALLOCATED;
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

/* the device interface path of the endpoint, as the audio stack names it. */
static constexpr WCHAR g_interface[] = L"\\\\?\\SWD#MMDEVAPI#{0.0.0.00000000}.{6d696d65-0000-4000-8000-000000000001}#"
                                       L"{e6327cad-dcec-4949-ae8a-991e976a79d2}";

WINMMAPI MMRESULT WINAPI waveOutMessage(HWAVEOUT handle, UINT message, DWORD_PTR param1, DWORD_PTR param2)
{
    BOOL device = !handle || handle == (HWAVEOUT)(UINT_PTR)WAVE_MAPPER || find_out(handle);
    if (!device)
        return MMSYSERR_INVALHANDLE;
    switch (message) {
    case DRVM_MAPPER_PREFERRED_GET:
    case DRVM_MAPPER_CONSOLEVOICECOM_GET:
        if (param1)
            *(DWORD *)param1 = 0;
        if (param2)
            *(DWORD *)param2 = 0;
        return MMSYSERR_NOERROR;
    case DRV_QUERYDEVICEINTERFACESIZE:
        if (!param1)
            return MMSYSERR_INVALPARAM;
        *(ULONG *)param1 = sizeof(g_interface);
        return MMSYSERR_NOERROR;
    case DRV_QUERYDEVICEINTERFACE:
        if (!param1 || param2 < sizeof(g_interface))
            return MMSYSERR_INVALPARAM;
        memcpy((void *)param1, g_interface, sizeof(g_interface));
        return MMSYSERR_NOERROR;
    default:
        return MMSYSERR_NOTSUPPORTED;
    }
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

WINMMAPI UINT WINAPI waveInGetNumDevs(void)
{
    return 0;
}

WINMMAPI MMRESULT WINAPI waveInGetDevCapsW(UINT_PTR id, LPWAVEINCAPSW caps, UINT size)
{
    return MMSYSERR_BADDEVICEID;
}

WINMMAPI MMRESULT WINAPI waveInGetDevCapsA(UINT_PTR id, LPWAVEINCAPSA caps, UINT size)
{
    return MMSYSERR_BADDEVICEID;
}

WINMMAPI MMRESULT WINAPI waveInOpen(LPHWAVEIN in, UINT id, LPCWAVEFORMATEX format, DWORD_PTR callback,
                                    DWORD_PTR instance, DWORD flags)
{
    if (in)
        *in = nullptr;
    return id == WAVE_MAPPER ? MMSYSERR_NODRIVER : MMSYSERR_BADDEVICEID;
}

WINMMAPI MMRESULT WINAPI waveInClose(HWAVEIN wave)
{
    return MMSYSERR_INVALHANDLE;
}
