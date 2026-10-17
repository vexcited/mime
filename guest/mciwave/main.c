/*
 * mciwave.dll, the waveaudio device: wave files played through winmm's
 * wave output, positions in milliseconds, bytes or samples rounded as
 * windows' driver rounds them, and its notifications: a new play
 * supersedes the last one's, stop, seek and close abort it. there is no
 * wave input, so nothing records.
 */
#include <mime/win32/winbase.h>
#include <mime/win32/winuser.h>
#include <mime/win32/mmsystem.h>

#define MAX_BUFFERS 8
#define MODE_IDLE 0
#define MODE_PLAYING 1
#define MODE_PAUSED 2
#define MODE_HELD 3
#define MODE_CUED 4

typedef struct WAVE {
    MCIDEVICEID id;
    RTL_CRITICAL_SECTION lock;
    WCHAR file[MAX_PATH];
    BOOL named;
    WAVEFORMATEX *format;
    BYTE *data;
    DWORD size;
    DWORD time_format;
    UINT output, input;
    DWORD seconds;
    /* playing */
    HWAVEOUT out;
    HANDLE thread, event;
    volatile BOOL quit;
    int mode;
    BOOL hold, failed;
    DWORD from, to, next; /* the range in bytes, and what is queued up to */
    DWORD position;       /* while idle */
    WAVEHDR headers[MAX_BUFFERS];
    DWORD chunk;
    HWND callback;        /* of the pending notification */
    BOOL pending;
} WAVE;

static HMODULE g_module;
static DWORD g_seconds = 4;
static UINT g_tables[256]; /* the command table each device id loaded */

/* ------------------------------------------------------------------ notifications */

static void notify(WAVE *w, HWND callback, UINT status)
{
    mciDriverNotify(callback, w->id, status);
}

static void settle(WAVE *w, UINT status)
{
    if (w->pending) {
        w->pending = FALSE;
        notify(w, w->callback, status);
    }
}

/* a command's own notification, after any pending one is superseded. */
static MCIERROR done(WAVE *w, MCIERROR r, DWORD flags, DWORD_PTR parms)
{
    if (!r && (flags & MCI_NOTIFY) && parms) {
        if (w)
            settle(w, MCI_NOTIFY_SUPERSEDED);
        mciDriverNotify((HANDLE)((MCI_GENERIC_PARMS *)parms)->dwCallback, w ? w->id : 0, MCI_NOTIFY_SUCCESSFUL);
    }
    return r;
}

/* ------------------------------------------------------------------ positions */

static DWORD round_block(const WAVE *w, DWORD bytes)
{
    return bytes - bytes % w->format->nBlockAlign;
}

static DWORD to_format(const WAVE *w, DWORD bytes)
{
    switch (w->time_format) {
    case MCI_FORMAT_BYTES:
        return bytes;
    case MCI_FORMAT_SAMPLES:
        return MulDiv(bytes, w->format->nSamplesPerSec, w->format->nAvgBytesPerSec);
    default:
        return MulDiv(bytes, 1000, w->format->nAvgBytesPerSec);
    }
}

static DWORD to_bytes(const WAVE *w, DWORD value)
{
    DWORD bytes;
    switch (w->time_format) {
    case MCI_FORMAT_BYTES:
        bytes = round_block(w, value);
        break;
    case MCI_FORMAT_SAMPLES:
        bytes = w->format->nBlockAlign *
                (MulDiv(value, w->format->nAvgBytesPerSec, w->format->nSamplesPerSec) / w->format->nBlockAlign);
        break;
    default:
        bytes = round_block(w, MulDiv(value, w->format->nAvgBytesPerSec, 1000));
        break;
    }
    return bytes > w->size ? w->size : bytes;
}

static DWORD current(WAVE *w)
{
    if (w->mode == MODE_IDLE || !w->out)
        return w->position;
    MMTIME t = {TIME_BYTES};
    waveOutGetPosition(w->out, &t, sizeof(t));
    DWORD at = w->from + t.u.cb;
    return at > w->to ? w->to : at;
}

/* ------------------------------------------------------------------ playing */

static void queue(WAVE *w, WAVEHDR *h)
{
    if (w->next >= w->to)
        return;
    DWORD n = w->to - w->next < w->chunk ? w->to - w->next : w->chunk;
    h->lpData = (LPSTR)w->data + w->next;
    h->dwBufferLength = n;
    h->dwFlags = 0;
    waveOutPrepareHeader(w->out, h, sizeof(*h));
    waveOutWrite(w->out, h, sizeof(*h));
    w->next += n;
}

static BOOL all_done(const WAVE *w)
{
    for (int i = 0; i < MAX_BUFFERS; i++)
        if (w->headers[i].dwFlags & WHDR_INQUEUE)
            return FALSE;
    return TRUE;
}

static void close_output(WAVE *w)
{
    if (!w->out)
        return;
    waveOutReset(w->out);
    for (int i = 0; i < MAX_BUFFERS; i++)
        if (w->headers[i].dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(w->out, &w->headers[i], sizeof(WAVEHDR));
    waveOutClose(w->out);
    w->out = nullptr;
}

/* refills the output as buffers come back, and ends the play at its end. */
static DWORD WINAPI worker(void *arg)
{
    WAVE *w = arg;
    while (!w->quit) {
        WaitForSingleObject(w->event, 50);
        RtlEnterCriticalSection(&w->lock);
        if (w->mode == MODE_PLAYING && w->out) {
            for (int i = 0; i < MAX_BUFFERS; i++) {
                WAVEHDR *h = &w->headers[i];
                if ((h->dwFlags & WHDR_DONE) && !(h->dwFlags & WHDR_INQUEUE)) {
                    waveOutUnprepareHeader(w->out, h, sizeof(*h));
                    h->dwFlags = 0;
                    queue(w, h);
                }
            }
            if (w->next >= w->to && all_done(w)) {
                if (w->hold) {
                    w->mode = MODE_HELD;
                } else {
                    w->position = w->to;
                    close_output(w);
                    w->mode = MODE_IDLE;
                }
                settle(w, MCI_NOTIFY_SUCCESSFUL);
            }
        }
        RtlLeaveCriticalSection(&w->lock);
    }
    return 0;
}

static MCIERROR start_output(WAVE *w, BOOL paused)
{
    if (!waveOutGetNumDevs())
        return MCIERR_WAVE_OUTPUTSUNSUITABLE;
    UINT device = w->output == (UINT)-1 ? WAVE_MAPPER : w->output;
    MMRESULT r = waveOutOpen(&w->out, device, w->format, (DWORD_PTR)w->event, 0, CALLBACK_EVENT);
    if (r == MMSYSERR_ALLOCATED)
        return w->output == (UINT)-1 ? MCIERR_WAVE_OUTPUTSINUSE : MCIERR_WAVE_SETOUTPUTINUSE;
    if (r)
        return w->output == (UINT)-1 ? MCIERR_WAVE_OUTPUTSUNSUITABLE : MCIERR_WAVE_SETOUTPUTUNSUITABLE;
    if (paused)
        waveOutPause(w->out);
    w->chunk = round_block(w, w->format->nAvgBytesPerSec / 2);
    if (!w->chunk)
        w->chunk = w->format->nBlockAlign;
    w->next = w->from;
    memset(w->headers, 0, sizeof(w->headers));
    for (int i = 0; i < MAX_BUFFERS; i++)
        queue(w, &w->headers[i]);
    return MMSYSERR_NOERROR;
}

/* stops playing where it is, the position kept, a pending play told it was aborted. */
static void stop(WAVE *w, BOOL abort)
{
    if (w->mode != MODE_IDLE) {
        w->position = round_block(w, current(w));
        close_output(w);
        w->mode = MODE_IDLE;
    }
    if (abort)
        settle(w, MCI_NOTIFY_ABORTED);
}

static MCIERROR wait_for(WAVE *w)
{
    for (;;) {
        RtlEnterCriticalSection(&w->lock);
        BOOL busy = w->mode == MODE_PLAYING;
        BOOL failed = w->failed;
        RtlLeaveCriticalSection(&w->lock);
        if (failed)
            return MCIERR_WAVE_OUTPUTSUNSUITABLE;
        if (!busy || mciDriverYield(w->id))
            return MMSYSERR_NOERROR;
        Sleep(10);
    }
}

static MCIERROR play(WAVE *w, DWORD flags, MCI_PLAY_PARMS *p)
{
    DWORD length = to_format(w, w->size);
    if ((flags & MCI_FROM) && p->dwFrom > length)
        return MCIERR_OUTOFRANGE;
    if ((flags & MCI_TO) && p->dwTo > length)
        return MCIERR_OUTOFRANGE;
    RtlEnterCriticalSection(&w->lock);
    DWORD from = (flags & MCI_FROM) ? to_bytes(w, p->dwFrom) : round_block(w, current(w));
    DWORD to = (flags & MCI_TO) ? (p->dwTo == length ? w->size : to_bytes(w, p->dwTo)) : w->size;
    if (to < from) {
        RtlLeaveCriticalSection(&w->lock);
        return MCIERR_OUTOFRANGE;
    }
    if (w->pending && (flags & MCI_NOTIFY))
        settle(w, (flags & MCI_FROM) || to != w->to ? MCI_NOTIFY_ABORTED : MCI_NOTIFY_SUPERSEDED);
    MCIERROR r = MMSYSERR_NOERROR;
    if (w->mode != MODE_IDLE && !(flags & MCI_FROM) && w->out) {
        /* playing on toward a new end, or out of a pause. */
        w->to = to;
        w->hold = (flags & 0x01000000) != 0;
        if (w->mode != MODE_PLAYING)
            waveOutRestart(w->out);
        w->mode = MODE_PLAYING;
        for (int i = 0; i < MAX_BUFFERS; i++)
            if (!(w->headers[i].dwFlags & WHDR_INQUEUE))
                queue(w, &w->headers[i]);
    } else {
        stop(w, FALSE);
        w->from = from;
        w->to = to;
        w->hold = (flags & 0x01000000) != 0;
        w->failed = FALSE;
        r = start_output(w, FALSE);
        if (!r)
            w->mode = MODE_PLAYING;
    }
    if (!r && (flags & MCI_NOTIFY)) {
        w->pending = TRUE;
        w->callback = (HWND)p->dwCallback;
    }
    RtlLeaveCriticalSection(&w->lock);
    SetEvent(w->event);
    if (!r && (flags & MCI_WAIT))
        r = wait_for(w);
    return r;
}

/* ------------------------------------------------------------------ the file */

static MCIERROR mmio_error(MMRESULT e)
{
    switch (e) {
    case MMIOERR_FILENOTFOUND:
    case MMIOERR_CANNOTOPEN:
        return MCIERR_FILE_NOT_FOUND;
    case MMIOERR_OUTOFMEMORY:
        return MCIERR_OUT_OF_MEMORY;
    case MMIOERR_CANNOTREAD:
    case MMIOERR_CANNOTSEEK:
        return MCIERR_FILE_READ;
    case MMIOERR_CANNOTWRITE:
    case MMIOERR_CANNOTEXPAND:
        return MCIERR_FILE_WRITE;
    default:
        return MCIERR_INVALID_FILE;
    }
}

static MCIERROR load(WAVE *w, const WCHAR *name)
{
    if (lstrlenW(name) > 259)
        return MCIERR_PARAM_OVERFLOW;
    MMIOINFO info = {};
    HMMIO h = mmioOpenW((LPWSTR)name, &info, MMIO_READ | MMIO_DENYWRITE);
    if (!h)
        return mmio_error(info.wErrorRet ? info.wErrorRet : MMIOERR_FILENOTFOUND);
    MMCKINFO riff = {}, chunk = {};
    riff.fccType = mmioFOURCC('W', 'A', 'V', 'E');
    MCIERROR r = MMSYSERR_NOERROR;
    MMRESULT e = mmioDescend(h, &riff, nullptr, MMIO_FINDRIFF);
    if (!e) {
        chunk.ckid = mmioFOURCC('f', 'm', 't', ' ');
        e = mmioDescend(h, &chunk, &riff, MMIO_FINDCHUNK);
    }
    if (e) {
        mmioClose(h, 0);
        return mmio_error(e);
    }
    if (chunk.cksize < 16) {
        mmioClose(h, 0);
        return MCIERR_INVALID_FILE;
    }
    DWORD size = chunk.cksize < sizeof(WAVEFORMATEX) ? sizeof(WAVEFORMATEX) : chunk.cksize;
    w->format = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    if (!w->format || mmioRead(h, (HPSTR)w->format, chunk.cksize) != (LONG)chunk.cksize) {
        mmioClose(h, 0);
        return w->format ? MCIERR_FILE_READ : MCIERR_OUT_OF_MEMORY;
    }
    if (w->format->wFormatTag == WAVE_FORMAT_PCM)
        w->format->cbSize = 0;
    else if (chunk.cksize < 18u + w->format->cbSize)
        r = MCIERR_INVALID_FILE;
    if (w->format->nAvgBytesPerSec > 100000000)
        w->format->nAvgBytesPerSec = 100000000;
    mmioAscend(h, &chunk, 0);
    chunk.ckid = mmioFOURCC('d', 'a', 't', 'a');
    if (!r && (e = mmioDescend(h, &chunk, &riff, MMIO_FINDCHUNK)))
        r = mmio_error(e);
    if (!r) {
        w->data = HeapAlloc(GetProcessHeap(), 0, chunk.cksize + 1);
        if (!w->data)
            r = MCIERR_OUT_OF_MEMORY;
        else if (mmioRead(h, (HPSTR)w->data, chunk.cksize) != (LONG)chunk.cksize)
            r = MCIERR_FILE_READ;
        w->size = chunk.cksize;
    }
    mmioClose(h, 0);
    if (!r && w->format->nBlockAlign)
        w->size = round_block(w, w->size);
    return r;
}

static void free_wave(WAVE *w)
{
    if (!w)
        return;
    w->quit = TRUE;
    if (w->thread) {
        SetEvent(w->event);
        WaitForSingleObject(w->thread, INFINITE);
        CloseHandle(w->thread);
    }
    close_output(w);
    if (w->event)
        CloseHandle(w->event);
    HeapFree(GetProcessHeap(), 0, w->format);
    HeapFree(GetProcessHeap(), 0, w->data);
    RtlDeleteCriticalSection(&w->lock);
    HeapFree(GetProcessHeap(), 0, w);
}

static MCIERROR open(MCIDEVICEID id, DWORD flags, MCI_WAVE_OPEN_PARMSW *p)
{
    DWORD seconds = g_seconds;
    if (flags & MCI_WAVE_OPEN_BUFFER) {
        if (p->dwBufferSeconds < 2 || p->dwBufferSeconds > 9)
            return MCIERR_OUTOFRANGE;
        seconds = p->dwBufferSeconds;
    }
    BOOL element = flags & MCI_OPEN_ELEMENT, element_id = flags & MCI_OPEN_ELEMENT_ID;
    if (element && element_id)
        return MCIERR_FLAGS_NOT_COMPATIBLE;
    if (!element && !element_id)
        return done(nullptr, MMSYSERR_NOERROR, flags, (DWORD_PTR)p);
    if (flags & MCI_OPEN_SHAREABLE)
        return MCIERR_UNSUPPORTED_FUNCTION;
    WAVE *w = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*w));
    if (!w)
        return MCIERR_OUT_OF_MEMORY;
    RtlInitializeCriticalSection(&w->lock);
    w->id = id;
    w->output = w->input = (UINT)-1;
    w->seconds = seconds;
    MCIERROR r = MMSYSERR_NOERROR;
    const WCHAR *name = element ? p->lpstrElementName : nullptr;
    if (name && !name[0]) {
        /* a new file: empty, mono 8 bit pcm at 11025 hz. */
        w->format = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WAVEFORMATEX));
        if (w->format)
            *w->format = (WAVEFORMATEX){WAVE_FORMAT_PCM, 1, 11025, 11025, 1, 8, 0};
        else
            r = MCIERR_OUT_OF_MEMORY;
    } else if (name) {
        lstrcpynW(w->file, name, MAX_PATH);
        w->named = TRUE;
        r = load(w, name);
    } else {
        r = MCIERR_FILENAME_REQUIRED;
    }
    if (!r && w->format->nBlockAlign) {
        /* a file no output can play cannot be opened at all. */
        if (waveOutOpen(nullptr, WAVE_MAPPER, w->format, 0, 0, WAVE_FORMAT_QUERY))
            r = MCIERR_WAVE_OUTPUTSUNSUITABLE;
    }
    if (!r) {
        w->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        w->thread = w->event ? CreateThread(nullptr, 0, worker, w, 0, nullptr) : nullptr;
        if (!w->thread)
            r = MCIERR_OUT_OF_MEMORY;
    }
    if (r) {
        free_wave(w);
        return r;
    }
    if (!w->format->nBlockAlign) {
        /* no usable data: the device acts as one without a file. */
        free_wave(w);
        return done(nullptr, MMSYSERR_NOERROR, flags, (DWORD_PTR)p);
    }
    mciSetDriverData(id, (DWORD_PTR)w);
    return done(w, MMSYSERR_NOERROR, flags, (DWORD_PTR)p);
}

/* ------------------------------------------------------------------ status, set, info */

static MCIERROR resource(MCI_STATUS_PARMS *p, WORD value, UINT string)
{
    p->dwReturn = MAKELONG(value, string);
    return MCI_RESOURCE_RETURNED;
}

static MCIERROR status(WAVE *w, DWORD flags, MCI_STATUS_PARMS *p)
{
    if (!(flags & MCI_STATUS_ITEM))
        return MCIERR_MISSING_PARAMETER;
    DWORD item = p->dwItem;
    if ((flags & MCI_TRACK) && item != MCI_STATUS_LENGTH && item != MCI_STATUS_POSITION)
        return MCIERR_FLAGS_NOT_COMPATIBLE;
    if ((flags & MCI_STATUS_START) && item != MCI_STATUS_POSITION)
        return MCIERR_FLAGS_NOT_COMPATIBLE;
    const WAVEFORMATEX *f = w->format;
    switch (item) {
    case MCI_STATUS_LENGTH:
        if ((flags & MCI_TRACK) && p->dwTrack != 1)
            return MCIERR_OUTOFRANGE;
        p->dwReturn = to_format(w, w->size);
        return 0;
    case MCI_STATUS_POSITION:
        if (flags & MCI_STATUS_START) {
            p->dwReturn = 0;
        } else if (flags & MCI_TRACK) {
            if (p->dwTrack != 1)
                return MCIERR_OUTOFRANGE;
            p->dwReturn = 0;
        } else {
            RtlEnterCriticalSection(&w->lock);
            p->dwReturn = to_format(w, current(w));
            RtlLeaveCriticalSection(&w->lock);
        }
        return 0;
    case MCI_STATUS_NUMBER_OF_TRACKS:
    case MCI_STATUS_CURRENT_TRACK:
        p->dwReturn = 1;
        return 0;
    case MCI_STATUS_MEDIA_PRESENT:
    case MCI_STATUS_READY:
        return resource(p, TRUE, 532);
    case MCI_STATUS_MODE: {
        int mode = w->mode;
        return mode == MODE_IDLE      ? resource(p, MCI_MODE_STOP, MCI_MODE_STOP)
               : mode == MODE_PLAYING ? resource(p, MCI_MODE_PLAY, MCI_MODE_PLAY)
                                      : resource(p, MCI_MODE_PAUSE, MCI_MODE_PAUSE);
    }
    case MCI_STATUS_TIME_FORMAT:
        return resource(p, (WORD)w->time_format, 533 + w->time_format);
    case MCI_WAVE_STATUS_FORMATTAG:
        if (f->wFormatTag == WAVE_FORMAT_PCM)
            return resource(p, WAVE_FORMAT_PCM, MCI_WAVE_PCM);
        p->dwReturn = f->wFormatTag;
        return 0;
    case MCI_WAVE_STATUS_CHANNELS:
        p->dwReturn = f->nChannels;
        return 0;
    case MCI_WAVE_STATUS_SAMPLESPERSEC:
        p->dwReturn = f->nSamplesPerSec;
        return 0;
    case MCI_WAVE_STATUS_AVGBYTESPERSEC:
        p->dwReturn = f->nAvgBytesPerSec;
        return 0;
    case MCI_WAVE_STATUS_BLOCKALIGN:
        p->dwReturn = f->nBlockAlign;
        return 0;
    case MCI_WAVE_STATUS_BITSPERSAMPLE:
        if (f->wFormatTag != WAVE_FORMAT_PCM)
            return MCIERR_UNSUPPORTED_FUNCTION;
        p->dwReturn = f->wBitsPerSample;
        return 0;
    case MCI_WAVE_INPUT:
    case MCI_WAVE_OUTPUT: {
        UINT device = item == MCI_WAVE_INPUT ? w->input : w->output;
        if (device == (UINT)-1)
            return resource(p, 0xFFFF, MCI_WAVE_MAPPER);
        p->dwReturn = device;
        return 0;
    }
    case MCI_WAVE_STATUS_LEVEL:
        return MCIERR_WAVE_INPUTSINUSE;
    default:
        return MCIERR_UNSUPPORTED_FUNCTION;
    }
}

static MCIERROR set(WAVE *w, DWORD flags, MCI_WAVE_SET_PARMS *p)
{
    DWORD known = MCI_SET_TIME_FORMAT | MCI_SET_AUDIO | MCI_SET_VIDEO | MCI_SET_ON | MCI_SET_OFF | MCI_SET_DOOR_OPEN |
                  MCI_SET_DOOR_CLOSED | MCI_WAVE_INPUT | MCI_WAVE_OUTPUT | MCI_WAVE_SET_ANYINPUT |
                  MCI_WAVE_SET_ANYOUTPUT | MCI_WAVE_SET_FORMATTAG | MCI_WAVE_SET_CHANNELS |
                  MCI_WAVE_SET_SAMPLESPERSEC | MCI_WAVE_SET_AVGBYTESPERSEC | MCI_WAVE_SET_BLOCKALIGN |
                  MCI_WAVE_SET_BITSPERSAMPLE;
    if (!(flags & known))
        return MCIERR_MISSING_PARAMETER;
    if (flags & (MCI_SET_AUDIO | MCI_SET_VIDEO | MCI_SET_ON | MCI_SET_OFF | MCI_SET_DOOR_OPEN | MCI_SET_DOOR_CLOSED))
        return MCIERR_UNSUPPORTED_FUNCTION;
    if (flags & MCI_SET_TIME_FORMAT) {
        DWORD format = p->dwTimeFormat;
        if (format != MCI_FORMAT_MILLISECONDS && format != MCI_FORMAT_SAMPLES &&
            (format != MCI_FORMAT_BYTES || w->format->wFormatTag != WAVE_FORMAT_PCM))
            return MCIERR_BAD_TIME_FORMAT;
        w->time_format = format;
    }
    if (flags & (MCI_WAVE_OUTPUT | MCI_WAVE_SET_ANYOUTPUT)) {
        RtlEnterCriticalSection(&w->lock);
        stop(w, TRUE);
        RtlLeaveCriticalSection(&w->lock);
        if ((flags & MCI_WAVE_OUTPUT) && p->wOutput >= waveOutGetNumDevs())
            return MCIERR_OUTOFRANGE;
        w->output = (flags & MCI_WAVE_OUTPUT) ? p->wOutput : (UINT)-1;
    }
    if (flags & (MCI_WAVE_INPUT | MCI_WAVE_SET_ANYINPUT)) {
        if ((flags & MCI_WAVE_INPUT) && p->wInput >= waveInGetNumDevs())
            return MCIERR_OUTOFRANGE;
        w->input = (flags & MCI_WAVE_INPUT) ? p->wInput : (UINT)-1;
    }
    DWORD format_flags = MCI_WAVE_SET_FORMATTAG | MCI_WAVE_SET_CHANNELS | MCI_WAVE_SET_SAMPLESPERSEC |
                         MCI_WAVE_SET_AVGBYTESPERSEC | MCI_WAVE_SET_BLOCKALIGN | MCI_WAVE_SET_BITSPERSAMPLE;
    if (flags & format_flags) {
        if (w->size)
            return MCIERR_NONAPPLICABLE_FUNCTION;
        WAVEFORMATEX f = *w->format;
        if (flags & MCI_WAVE_SET_FORMATTAG)
            f.wFormatTag = p->wFormatTag;
        if (flags & MCI_WAVE_SET_CHANNELS)
            f.nChannels = p->nChannels;
        if (flags & MCI_WAVE_SET_SAMPLESPERSEC)
            f.nSamplesPerSec = p->nSamplesPerSec;
        if (flags & MCI_WAVE_SET_BITSPERSAMPLE) {
            if (f.wFormatTag != WAVE_FORMAT_PCM)
                return MCIERR_UNSUPPORTED_FUNCTION;
            f.wBitsPerSample = p->wBitsPerSample;
        }
        if (flags & MCI_WAVE_SET_BLOCKALIGN)
            f.nBlockAlign = p->nBlockAlign;
        else if (f.wFormatTag == WAVE_FORMAT_PCM)
            f.nBlockAlign = (WORD)(f.nChannels * f.wBitsPerSample / 8);
        if (flags & MCI_WAVE_SET_AVGBYTESPERSEC)
            f.nAvgBytesPerSec = p->nAvgBytesPerSec;
        else if (f.wFormatTag == WAVE_FORMAT_PCM)
            f.nAvgBytesPerSec = f.nSamplesPerSec * f.nBlockAlign;
        if (!f.nBlockAlign || waveOutOpen(nullptr, WAVE_MAPPER, &f, 0, 0, WAVE_FORMAT_QUERY))
            return MCIERR_OUTOFRANGE;
        *w->format = f;
    }
    return MMSYSERR_NOERROR;
}

static MCIERROR info(WAVE *w, DWORD flags, MCI_INFO_PARMSW *p)
{
    if (!p || !p->lpstrReturn || !p->dwRetSize)
        return MCIERR_PARAM_OVERFLOW;
    DWORD which = flags & ~(MCI_NOTIFY | MCI_WAIT);
    if (!which)
        return MCIERR_MISSING_PARAMETER;
    if (which & ~(MCI_INFO_PRODUCT | MCI_INFO_FILE | MCI_WAVE_INPUT | MCI_WAVE_OUTPUT))
        return MCIERR_UNRECOGNIZED_KEYWORD;
    if (which & (which - 1))
        return MCIERR_FLAGS_NOT_COMPATIBLE;
    WCHAR text[MAX_PATH];
    if (which == MCI_INFO_PRODUCT) {
        lstrcpyW(text, L"Sound");
    } else if (which == MCI_INFO_FILE) {
        if (!w)
            return MCIERR_UNSUPPORTED_FUNCTION;
        if (!w->named)
            return MCIERR_NONAPPLICABLE_FUNCTION;
        lstrcpyW(text, w->file);
    } else {
        if (!w)
            return MCIERR_UNSUPPORTED_FUNCTION;
        UINT device = which == MCI_WAVE_INPUT ? w->input : w->output;
        WAVEOUTCAPSW out;
        WAVEINCAPSW in;
        if (which == MCI_WAVE_OUTPUT && !waveOutGetDevCapsW(device == (UINT)-1 ? WAVE_MAPPER : device, &out, sizeof(out)))
            lstrcpyW(text, out.szPname);
        else if (which == MCI_WAVE_INPUT && !waveInGetDevCapsW(device == (UINT)-1 ? WAVE_MAPPER : device, &in, sizeof(in)))
            lstrcpyW(text, in.szPname);
        else
            lstrcpyW(text, L"mapper");
    }
    if ((DWORD)lstrlenW(text) >= p->dwRetSize)
        return MCIERR_PARAM_OVERFLOW;
    lstrcpyW(p->lpstrReturn, text);
    return MMSYSERR_NOERROR;
}

static MCIERROR capability(DWORD flags, MCI_GETDEVCAPS_PARMS *p)
{
    if (flags & ~(MCI_NOTIFY | MCI_WAIT | MCI_GETDEVCAPS_ITEM))
        return MCIERR_UNRECOGNIZED_KEYWORD;
    if (!(flags & MCI_GETDEVCAPS_ITEM))
        return MCIERR_MISSING_PARAMETER;
    BOOL yes;
    switch (p->dwItem) {
    case MCI_GETDEVCAPS_CAN_RECORD:
        yes = waveInGetNumDevs() > 0;
        break;
    case MCI_GETDEVCAPS_HAS_AUDIO:
    case MCI_GETDEVCAPS_USES_FILES:
    case MCI_GETDEVCAPS_COMPOUND_DEVICE:
    case MCI_GETDEVCAPS_CAN_SAVE:
        yes = TRUE;
        break;
    case MCI_GETDEVCAPS_HAS_VIDEO:
    case MCI_GETDEVCAPS_CAN_EJECT:
        yes = FALSE;
        break;
    case MCI_GETDEVCAPS_CAN_PLAY:
        yes = waveOutGetNumDevs() > 0;
        break;
    case MCI_GETDEVCAPS_DEVICE_TYPE:
        p->dwReturn = MAKELONG(MCI_DEVTYPE_WAVEFORM_AUDIO, MCI_DEVTYPE_WAVEFORM_AUDIO);
        return MCI_RESOURCE_RETURNED;
    case MCI_WAVE_GETDEVCAPS_INPUTS:
        p->dwReturn = waveInGetNumDevs();
        return 0;
    case MCI_WAVE_GETDEVCAPS_OUTPUTS:
        p->dwReturn = waveOutGetNumDevs();
        return 0;
    default:
        return MCIERR_UNSUPPORTED_FUNCTION;
    }
    p->dwReturn = yes ? MAKELONG(1, 532) : MAKELONG(0, 531);
    return MCI_RESOURCE_RETURNED;
}

/* ------------------------------------------------------------------ transport */

static MCIERROR seek(WAVE *w, DWORD flags, MCI_SEEK_PARMS *p)
{
    DWORD which = flags & (MCI_TO | MCI_SEEK_TO_START | MCI_SEEK_TO_END);
    if (flags & ~(which | MCI_NOTIFY | MCI_WAIT))
        return MCIERR_UNRECOGNIZED_KEYWORD;
    if (!which)
        return MCIERR_MISSING_PARAMETER;
    if (which & (which - 1))
        return MCIERR_FLAGS_NOT_COMPATIBLE;
    if ((flags & MCI_TO) && p->dwTo > to_format(w, w->size))
        return MCIERR_OUTOFRANGE;
    RtlEnterCriticalSection(&w->lock);
    stop(w, TRUE);
    w->position = which == MCI_SEEK_TO_START ? 0 : which == MCI_SEEK_TO_END ? w->size : to_bytes(w, p->dwTo);
    RtlLeaveCriticalSection(&w->lock);
    return MMSYSERR_NOERROR;
}

static MCIERROR pause(WAVE *w)
{
    RtlEnterCriticalSection(&w->lock);
    MCIERROR r = MMSYSERR_NOERROR;
    if (w->mode == MODE_PLAYING) {
        waveOutPause(w->out);
        w->mode = MODE_PAUSED;
    } else if (w->mode != MODE_PAUSED) {
        r = MCIERR_NONAPPLICABLE_FUNCTION;
    }
    RtlLeaveCriticalSection(&w->lock);
    return r;
}

static MCIERROR resume(WAVE *w)
{
    RtlEnterCriticalSection(&w->lock);
    MCIERROR r = MMSYSERR_NOERROR;
    if (w->mode == MODE_PAUSED || w->mode == MODE_CUED) {
        waveOutRestart(w->out);
        w->mode = MODE_PLAYING;
    } else if (w->mode == MODE_IDLE) {
        r = MCIERR_NONAPPLICABLE_FUNCTION;
    }
    RtlLeaveCriticalSection(&w->lock);
    SetEvent(w->event);
    return r;
}

static MCIERROR cue(WAVE *w, DWORD flags)
{
    if ((flags & MCI_WAVE_INPUT) && (flags & MCI_WAVE_OUTPUT))
        return MCIERR_FLAGS_NOT_COMPATIBLE;
    if (flags & MCI_WAVE_INPUT)
        return MCIERR_WAVE_INPUTSUNSUITABLE;
    RtlEnterCriticalSection(&w->lock);
    MCIERROR r = MMSYSERR_NOERROR;
    if (w->mode == MODE_IDLE) {
        w->from = w->position;
        w->to = w->size;
        w->hold = FALSE;
        r = start_output(w, TRUE);
        if (!r)
            w->mode = MODE_CUED;
    } else if (w->mode != MODE_CUED) {
        r = MCIERR_NONAPPLICABLE_FUNCTION;
    }
    RtlLeaveCriticalSection(&w->lock);
    return r;
}

static MCIERROR remove_range(WAVE *w, DWORD flags, MCI_WAVE_DELETE_PARMS *p)
{
    DWORD length = to_format(w, w->size);
    if (((flags & MCI_FROM) && p->dwFrom > length) || ((flags & MCI_TO) && p->dwTo > length))
        return MCIERR_OUTOFRANGE;
    RtlEnterCriticalSection(&w->lock);
    stop(w, TRUE);
    DWORD from = (flags & MCI_FROM) ? to_bytes(w, p->dwFrom) : w->position;
    DWORD to = (flags & MCI_TO) ? to_bytes(w, p->dwTo) : w->size;
    MCIERROR r = MMSYSERR_NOERROR;
    if (to < from) {
        r = MCIERR_OUTOFRANGE;
    } else {
        memmove(w->data + from, w->data + to, w->size - to);
        w->size -= to - from;
        w->position = from;
    }
    RtlLeaveCriticalSection(&w->lock);
    return r;
}

static MCIERROR save(WAVE *w, DWORD flags, MCI_SAVE_PARMSW *p)
{
    const WCHAR *name = (flags & MCI_SAVE_FILE) ? p->lpfilename : w->named ? w->file : nullptr;
    if (!name)
        return MCIERR_UNNAMED_RESOURCE;
    HANDLE f = CreateFileW(name, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return MCIERR_FILENAME_REQUIRED;
    DWORD format_size = sizeof(WAVEFORMATEX) - 2 + (w->format->wFormatTag == WAVE_FORMAT_PCM ? 0 : 2 + w->format->cbSize);
    DWORD riff = 4 + 8 + format_size + 8 + w->size, n;
    BOOL ok = WriteFile(f, "RIFF", 4, &n, nullptr) && WriteFile(f, &riff, 4, &n, nullptr) &&
              WriteFile(f, "WAVEfmt ", 8, &n, nullptr) && WriteFile(f, &format_size, 4, &n, nullptr) &&
              WriteFile(f, w->format, format_size, &n, nullptr) && WriteFile(f, "data", 4, &n, nullptr) &&
              WriteFile(f, &w->size, 4, &n, nullptr) && WriteFile(f, w->data, w->size, &n, nullptr);
    CloseHandle(f);
    if (!ok)
        return MCIERR_FILE_NOT_SAVED;
    if (flags & MCI_SAVE_FILE) {
        lstrcpynW(w->file, name, MAX_PATH);
        w->named = TRUE;
    }
    return MMSYSERR_NOERROR;
}

/* ------------------------------------------------------------------ the driver */

static MCIERROR command(MCIDEVICEID id, UINT message, DWORD flags, DWORD_PTR parms)
{
    WAVE *w = (WAVE *)mciGetDriverData(id);
    switch (message) {
    case MCI_OPEN_DRIVER:
        return open(id, flags, (MCI_WAVE_OPEN_PARMSW *)parms);
    case MCI_CLOSE_DRIVER:
        if (w) {
            RtlEnterCriticalSection(&w->lock);
            stop(w, TRUE);
            RtlLeaveCriticalSection(&w->lock);
        }
        mciSetDriverData(id, 0);
        free_wave(w);
        return done(nullptr, MMSYSERR_NOERROR, flags, parms);
    case MCI_GETDEVCAPS:
        return done(w, capability(flags, (MCI_GETDEVCAPS_PARMS *)parms), flags, parms);
    case MCI_INFO:
        return done(w, info(w, flags, (MCI_INFO_PARMSW *)parms), flags, parms);
    }
    if (message < MCI_FIRST || message > MCI_LAST)
        return MCIERR_UNRECOGNIZED_COMMAND;
    switch (message) {
    case MCI_PLAY:
    case MCI_SEEK:
    case MCI_STOP:
    case MCI_PAUSE:
    case MCI_RESUME:
    case MCI_STATUS:
    case MCI_SET:
    case MCI_RECORD:
    case MCI_SAVE:
    case MCI_CUE:
    case MCI_DELETE:
    case MCI_COPY:
    case MCI_PASTE:
    case MCI_LOAD:
        if (!w)
            return MCIERR_UNSUPPORTED_FUNCTION;
        break;
    default:
        return MCIERR_UNRECOGNIZED_COMMAND;
    }
    if (!parms)
        return MCIERR_NULL_PARAMETER_BLOCK;
    switch (message) {
    case MCI_PLAY: {
        MCIERROR r = play(w, flags, (MCI_PLAY_PARMS *)parms);
        return r;
    }
    case MCI_STOP:
        RtlEnterCriticalSection(&w->lock);
        stop(w, TRUE);
        RtlLeaveCriticalSection(&w->lock);
        return done(w, MMSYSERR_NOERROR, flags, parms);
    case MCI_SEEK:
        return done(w, seek(w, flags, (MCI_SEEK_PARMS *)parms), flags, parms);
    case MCI_PAUSE:
        return done(w, pause(w), flags, parms);
    case MCI_RESUME:
        return done(w, resume(w), flags, parms);
    case MCI_STATUS:
        return done(w, status(w, flags, (MCI_STATUS_PARMS *)parms), flags, parms);
    case MCI_SET:
        return done(w, set(w, flags, (MCI_WAVE_SET_PARMS *)parms), flags, parms);
    case MCI_CUE:
        return done(w, cue(w, flags), flags, parms);
    case MCI_DELETE:
        return done(w, remove_range(w, flags, (MCI_WAVE_DELETE_PARMS *)parms), flags, parms);
    case MCI_SAVE:
        return done(w, save(w, flags, (MCI_SAVE_PARMSW *)parms), flags, parms);
    case MCI_RECORD:
        if ((flags & MCI_RECORD_INSERT) && (flags & MCI_RECORD_OVERWRITE))
            return MCIERR_FLAGS_NOT_COMPATIBLE;
        return MCIERR_WAVE_INPUTSUNSUITABLE;
    default:
        return MCIERR_UNSUPPORTED_FUNCTION;
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
        /* the installation's parameter is the seconds of buffering, 2 to 9. */
        const WCHAR *s = p->lpstrParams;
        if (s && *s >= '2' && *s <= '9' && !s[1])
            g_seconds = *s - '0';
        p->wType = MCI_DEVTYPE_WAVEFORM_AUDIO;
        p->wCustomCommandTable = mciLoadCommandResource(g_module, L"#96", 0);
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
