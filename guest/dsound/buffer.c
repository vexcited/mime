/*
 * sound buffers: secondary buffers the mixer plays, and a device's primary
 * buffer, which a program writes itself only at DSSCL_WRITEPRIMARY. their
 * checks and error codes are windows'.
 */
#include "dsound_private.h"

static const IDirectSoundBuffer8Vtbl buffer_vtbl;
static const IDirectSoundNotifyVtbl notify_vtbl;
static const IKsPropertySetVtbl props_vtbl;

DSBUFFER *DsBufferFrom(void *iface)
{
    if (!iface)
        return nullptr;
    DSBUFFER *b = CONTAINING_RECORD(iface, DSBUFFER, iface);
    return b->magic == BUFFER_MAGIC ? b : nullptr;
}

static HANDLE heap(void)
{
    return GetProcessHeap();
}

/* the silence of a format: 0x80 for 8 bit samples, zeros otherwise. */
static void silence(BYTE *p, DWORD size, const WAVEFORMATEX *f)
{
    memset(p, f->wBitsPerSample == 8 ? 0x80 : 0, size);
}

/* how far windows' play cursor trails its write cursor: the user's maximum latency, 30 ms, at most half the buffer. */
static DWORD play_lead(const DSBUFFER *b)
{
    DWORD ms = 30, type, size = sizeof(ms);
    HKEY key;
    if (!RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Multimedia\\DirectSound", 0, KEY_QUERY_VALUE,
                       &key)) {
        DWORD value;
        if (!RegQueryValueExW(key, L"Maximum latency", nullptr, &type, (BYTE *)&value, &size) && type == REG_DWORD)
            ms = value;
        RegCloseKey(key);
    }
    DWORD align = b->format->nBlockAlign;
    DWORD lead = align * (b->format->nSamplesPerSec * ms / 1000);
    DWORD half = b->size >> 1;
    if (half < lead)
        lead = half - half % align;
    return lead;
}

/* ------------------------------------------------------------------ creation */

HRESULT DsBufferInit(DSBUFFER *b, DSDEVICE *d, const DSBUFFERDESC *desc)
{
    b->iface.lpVtbl = &buffer_vtbl;
    b->iface3d.lpVtbl = &DsBuffer3dVtbl;
    b->listener.lpVtbl = &DsListenerVtbl;
    b->notify_iface.lpVtbl = &notify_vtbl;
    b->props.lpVtbl = &props_vtbl;
    b->magic = BUFFER_MAGIC;
    b->device = d;
    b->ref = 1;
    b->flags = desc->dwFlags;
    b->primary = (desc->dwFlags & DSBCAPS_PRIMARYBUFFER) != 0;
    b->format = HeapAlloc(heap(), HEAP_ZERO_MEMORY, b->primary ? sizeof(WAVEFORMATEX) : DsFormatSize(desc->lpwfxFormat) + 2);
    if (!b->format)
        return DSERR_OUTOFMEMORY;
    if (b->primary) {
        /* windows' first primary format, and room for 32 kb in it. */
        WAVEFORMATEX f = {WAVE_FORMAT_PCM, 2, 22050, 44100, 2, 8, 0};
        *b->format = f;
        b->size = 32768;
        b->ref = 0;
        b->flags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_LOCSOFTWARE;
    } else {
        DsCopyFormat(b->format, desc->lpwfxFormat);
        DWORD align = b->format->nBlockAlign;
        b->size = desc->dwBufferBytes;
        if (align && b->size % align)
            b->size += align - b->size % align;
        if (!(b->flags & DSBCAPS_LOCDEFER))
            b->flags |= DSBCAPS_LOCSOFTWARE;
    }
    b->data_ref = HeapAlloc(heap(), 0, sizeof(LONG));
    b->data = HeapAlloc(heap(), HEAP_ZERO_MEMORY, b->size);
    if (!b->data || !b->data_ref) {
        HeapFree(heap(), 0, b->data);
        HeapFree(heap(), 0, b->data_ref);
        HeapFree(heap(), 0, b->format);
        return DSERR_OUTOFMEMORY;
    }
    *b->data_ref = 1;
    /* directsound 8 leaves the data as the allocation gives it; older versions fill it with silence. */
    if (b->primary || d->version < 0x800)
        silence(b->data, b->size, b->format);
    b->original_rate = b->format->nSamplesPerSec;
    b->frequency = b->original_rate;
    b->rate = b->original_rate;
    b->lead = play_lead(b);
    b->has3d = (b->flags & DSBCAPS_CTRL3D) && !b->primary;
    if (b->has3d)
        Ds3dDefaults(b);
    DsBufferUpdateGains(b);
    return DS_OK;
}

/* the same data and format, its own position, volume and state. */
HRESULT DsBufferDuplicate(DSBUFFER *source, DSBUFFER **out)
{
    if (source->flags & DSBCAPS_CTRLFX)
        return DSERR_INVALIDCALL;
    DSBUFFER *b = HeapAlloc(heap(), HEAP_ZERO_MEMORY, sizeof(*b));
    if (!b)
        return DSERR_OUTOFMEMORY;
    b->format = HeapAlloc(heap(), 0, DsFormatSize(source->format) + 2);
    if (!b->format) {
        HeapFree(heap(), 0, b);
        return DSERR_OUTOFMEMORY;
    }
    DsCopyFormat(b->format, source->format);
    b->iface.lpVtbl = &buffer_vtbl;
    b->iface3d.lpVtbl = &DsBuffer3dVtbl;
    b->listener.lpVtbl = &DsListenerVtbl;
    b->notify_iface.lpVtbl = &notify_vtbl;
    b->props.lpVtbl = &props_vtbl;
    b->magic = BUFFER_MAGIC;
    b->ref = 1;
    b->device = source->device;
    b->flags = source->flags;
    b->data = source->data;
    b->data_ref = source->data_ref;
    InterlockedIncrement(b->data_ref);
    b->size = source->size;
    b->lead = source->lead;
    b->original_rate = source->original_rate;
    b->frequency = source->frequency;
    b->rate = source->rate;
    b->volume = source->volume;
    b->pan = source->pan;
    b->has3d = source->has3d;
    if (b->has3d)
        b->sound3d = source->sound3d;
    DsBufferUpdateGains(b);
    b->next = source->device->buffers;
    source->device->buffers = b;
    *out = b;
    return DS_OK;
}

static void free_data(DSBUFFER *b)
{
    if (b->data && !InterlockedDecrement(b->data_ref)) {
        HeapFree(heap(), 0, b->data);
        HeapFree(heap(), 0, b->data_ref);
    }
    b->data = nullptr;
    HeapFree(heap(), 0, b->notifies);
    b->notifies = nullptr;
    b->notify_count = 0;
}

/* the device went away: the buffer stops and keeps only what its interfaces need to fail. */
void DsBufferKill(DSBUFFER *b)
{
    DsMixerRemove(b);
    free_data(b);
    b->device = nullptr;
    if (b->ref <= 0) {
        b->magic = 0;
        HeapFree(heap(), 0, b->format);
        HeapFree(heap(), 0, b);
    }
}

static void unlink(DSBUFFER *b)
{
    if (!b->device)
        return;
    for (DSBUFFER **p = &b->device->buffers; *p; p = &(*p)->next)
        if (*p == b) {
            *p = b->next;
            break;
        }
}

/* ------------------------------------------------------------------ gains */

static float speaker_gain(LONG volume, LONG pan, BOOL right)
{
    if (right)
        return DsAmplitude(pan < 0 ? volume + pan : volume);
    return DsAmplitude(pan > 0 ? volume - pan : volume);
}

/* the primary buffer's volume and pan, which windows applies to everything the device plays. */
void DsPrimaryGains(DSDEVICE *d, float gains[2])
{
    LONG volume = d->primary ? d->primary->volume : 0, pan = d->primary ? d->primary->pan : 0;
    gains[0] = volume || pan ? speaker_gain(volume, pan, FALSE) : 1.0f;
    gains[1] = volume || pan ? speaker_gain(volume, pan, TRUE) : 1.0f;
}

/* where a speaker of the source sits between the left and right speakers, 0 left to 2 right, or 3 for none. */
static BYTE speaker_place(DWORD bit)
{
    switch (bit) {
    case SPEAKER_FRONT_LEFT:
    case SPEAKER_BACK_LEFT:
    case SPEAKER_SIDE_LEFT:
    case SPEAKER_TOP_FRONT_LEFT:
    case SPEAKER_TOP_BACK_LEFT:
        return 0;
    case SPEAKER_FRONT_RIGHT:
    case SPEAKER_BACK_RIGHT:
    case SPEAKER_SIDE_RIGHT:
    case SPEAKER_TOP_FRONT_RIGHT:
    case SPEAKER_TOP_BACK_RIGHT:
        return 2;
    case SPEAKER_LOW_FREQUENCY:
        return 3;
    default:
        return 1;
    }
}

/*
 * the mixing matrix windows' engine builds for a buffer: each speaker of
 * the source on the stereo speakers, a mono source and centre speakers on
 * both at -3 db, then each output row scaled by the volume and pan of its
 * side. sources of more than two speakers have no matrix known from
 * windows; theirs follow the side of each speaker.
 */
static void matrix_2d(DSBUFFER *b, LONG volume, LONG pan)
{
    float left = speaker_gain(volume, pan, FALSE), right = speaker_gain(volume, pan, TRUE);
    memset(b->gain, 0, sizeof(b->gain));
    WORD channels = b->format->nChannels;
    if (channels == 1) {
        b->gain[0][0] = 0.70710677f * left;
        b->gain[0][1] = 0.70710677f * right;
        return;
    }
    DWORD mask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    if (b->format->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        mask = ((const WAVEFORMATEXTENSIBLE *)b->format)->dwChannelMask;
    for (WORD c = 0; c < channels && c < MAX_CHANNELS && mask; c++) {
        DWORD bit = mask & -mask;
        mask &= ~bit;
        switch (speaker_place(bit)) {
        case 0:
            b->gain[c][0] = left;
            break;
        case 1:
            b->gain[c][0] = 0.70710677f * left;
            b->gain[c][1] = 0.70710677f * right;
            break;
        case 2:
            b->gain[c][1] = right;
            break;
        }
    }
}

void DsBufferUpdateGains(DSBUFFER *b)
{
    if (b->has3d && b->device) {
        Ds3dApply(b);
        return;
    }
    if (b->primary) {
        matrix_2d(b, 0, 0);
        return;
    }
    matrix_2d(b, b->volume, b->pan);
}

/* ------------------------------------------------------------------ IUnknown */

HRESULT DsBufferQuery(DSBUFFER *b, REFIID riid, void **out)
{
    if (!out)
        return DSERR_INVALIDPARAM;
    *out = nullptr;
    DWORD version = b->device ? b->device->version : 0x800;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirectSoundBuffer))
        *out = &b->iface;
    else if (IsEqualGUID(riid, &IID_IDirectSoundBuffer8) && version >= 0x800 && !b->primary)
        *out = &b->iface;
    else if (IsEqualGUID(riid, &IID_IDirectSound3DBuffer) && b->has3d)
        *out = &b->iface3d;
    else if (IsEqualGUID(riid, &IID_IDirectSound3DListener) && b->primary && (b->flags & DSBCAPS_CTRL3D))
        *out = &b->listener;
    else if (IsEqualGUID(riid, &IID_IDirectSoundNotify) && (b->flags & DSBCAPS_CTRLPOSITIONNOTIFY) && !b->primary)
        *out = &b->notify_iface;
    else if (IsEqualGUID(riid, &IID_IKsPropertySet))
        *out = &b->props;
    else
        return E_NOINTERFACE;
    InterlockedIncrement(&b->ref);
    return S_OK;
}

ULONG DsBufferRelease(DSBUFFER *b)
{
    RtlEnterCriticalSection(&DsLock);
    LONG ref = InterlockedDecrement(&b->ref);
    if (b->primary) {
        /* the primary buffer lives as long as its device; its last user stops it. */
        if (ref <= 0 && b->device) {
            b->ref = 0;
            b->device->primary_users = 0;
            DsMixerStop(b);
        } else if (ref <= 0) {
            b->magic = 0;
            HeapFree(heap(), 0, b->format);
            HeapFree(heap(), 0, b);
        }
    } else if (ref <= 0) {
        DsMixerRemove(b);
        unlink(b);
        free_data(b);
        b->magic = 0;
        HeapFree(heap(), 0, b->format);
        HeapFree(heap(), 0, b);
    }
    RtlLeaveCriticalSection(&DsLock);
    return ref < 0 ? 0 : (ULONG)ref;
}

static HRESULT STDMETHODCALLTYPE buffer_query(IDirectSoundBuffer8 *This, REFIID riid, void **out)
{
    DSBUFFER *b = DsBufferFrom(This);
    return b ? DsBufferQuery(b, riid, out) : DSERR_INVALIDPARAM;
}

static ULONG STDMETHODCALLTYPE buffer_add_ref(IDirectSoundBuffer8 *This)
{
    DSBUFFER *b = DsBufferFrom(This);
    return b ? InterlockedIncrement(&b->ref) : 0;
}

static ULONG STDMETHODCALLTYPE buffer_release(IDirectSoundBuffer8 *This)
{
    DSBUFFER *b = DsBufferFrom(This);
    return b ? DsBufferRelease(b) : 0;
}

/* ------------------------------------------------------------------ methods */

/* a live buffer of a live device. */
static DSBUFFER *enter(void *iface)
{
    DSBUFFER *b = DsBufferFrom(iface);
    if (!b)
        return nullptr;
    RtlEnterCriticalSection(&DsLock);
    if (!b->device) {
        RtlLeaveCriticalSection(&DsLock);
        return nullptr;
    }
    return b;
}

static HRESULT leave(HRESULT hr)
{
    RtlLeaveCriticalSection(&DsLock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE buffer_get_caps(IDirectSoundBuffer8 *This, LPDSBCAPS caps)
{
    DSBUFFER *b = enter(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (!caps || caps->dwSize != sizeof(DSBCAPS))
        return leave(DSERR_INVALIDPARAM);
    caps->dwFlags = b->primary ? b->flags : b->flags & 0xFE3FF;
    caps->dwBufferBytes = b->size;
    caps->dwUnlockTransferRate = 0;
    caps->dwPlayCpuOverhead = 0;
    return leave(DS_OK);
}

/* the primary buffer's cursors are a program's only at DSSCL_WRITEPRIMARY. */
static HRESULT STDMETHODCALLTYPE buffer_get_position(IDirectSoundBuffer8 *This, LPDWORD play, LPDWORD write)
{
    DSBUFFER *b = enter(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (!play && !write)
        return leave(DSERR_INVALIDPARAM);
    if (b->lost)
        return leave(DSERR_BUFFERLOST);
    if (b->primary && b->device->level != DSSCL_WRITEPRIMARY)
        return leave(DSERR_PRIOLEVELNEEDED);
    DWORD p, w;
    DsMixerCursors(b, &p, &w);
    DWORD align = b->format->nBlockAlign;
    if (play)
        *play = align ? p - p % align : p;
    if (write)
        *write = align ? w - w % align : w;
    return leave(DS_OK);
}

static HRESULT STDMETHODCALLTYPE buffer_get_format(IDirectSoundBuffer8 *This, LPWAVEFORMATEX format, DWORD allocated,
                                                   LPDWORD written)
{
    DSBUFFER *b = enter(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (!format && !written)
        return leave(DSERR_INVALIDPARAM);
    DWORD size = b->format->wFormatTag == WAVE_FORMAT_PCM ? sizeof(WAVEFORMATEX) : DsFormatSize(b->format);
    HRESULT hr = DS_OK;
    if (format) {
        if (allocated < size)
            hr = DSERR_INVALIDPARAM;
        else {
            memcpy(format, b->format, size);
            if (b->format->wFormatTag == WAVE_FORMAT_PCM)
                format->cbSize = 0;
        }
    }
    if (written && SUCCEEDED(hr))
        *written = size;
    return leave(hr);
}

static HRESULT STDMETHODCALLTYPE buffer_get_volume(IDirectSoundBuffer8 *This, LPLONG volume)
{
    DSBUFFER *b = enter(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (!volume)
        return leave(DSERR_INVALIDPARAM);
    if (!(b->flags & DSBCAPS_CTRLVOLUME))
        return leave(DSERR_CONTROLUNAVAIL);
    *volume = b->volume;
    return leave(DS_OK);
}

static HRESULT STDMETHODCALLTYPE buffer_get_pan(IDirectSoundBuffer8 *This, LPLONG pan)
{
    DSBUFFER *b = enter(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (!pan)
        return leave(DSERR_INVALIDPARAM);
    if (!(b->flags & DSBCAPS_CTRLPAN))
        return leave(DSERR_CONTROLUNAVAIL);
    *pan = b->pan;
    return leave(DS_OK);
}

static HRESULT STDMETHODCALLTYPE buffer_get_frequency(IDirectSoundBuffer8 *This, LPDWORD frequency)
{
    DSBUFFER *b = enter(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (!frequency)
        return leave(DSERR_INVALIDPARAM);
    if (b->primary || !(b->flags & DSBCAPS_CTRLFREQUENCY))
        return leave(DSERR_CONTROLUNAVAIL);
    *frequency = b->frequency;
    return leave(DS_OK);
}

static HRESULT STDMETHODCALLTYPE buffer_get_status(IDirectSoundBuffer8 *This, LPDWORD status)
{
    DSBUFFER *b = enter(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (!status)
        return leave(DSERR_INVALIDPARAM);
    DWORD s;
    if (b->lost)
        s = DSBSTATUS_BUFFERLOST;
    else {
        s = 0;
        if (b->playing)
            s |= DSBSTATUS_PLAYING | (b->looping ? DSBSTATUS_LOOPING : 0);
        if (b->playing && (b->flags & DSBCAPS_LOCDEFER))
            s |= DSBSTATUS_LOCSOFTWARE;
    }
    if (!(b->flags & DSBCAPS_LOCDEFER))
        s &= b->primary ? 7 : ~0x38u;
    *status = s;
    return leave(DS_OK);
}

static HRESULT STDMETHODCALLTYPE buffer_initialize(IDirectSoundBuffer8 *This, LPDIRECTSOUND sound,
                                                   LPCDSBUFFERDESC desc)
{
    DSBUFFER *b = DsBufferFrom(This);
    if (!b || !DsDeviceFrom(sound) || !desc)
        return DSERR_INVALIDPARAM;
    return DSERR_ALREADYINITIALIZED;
}

static BOOL overlaps(DWORD a, DWORD a_size, DWORD b, DWORD b_size)
{
    return a_size && b_size && a < b + b_size && b < a + a_size;
}

static HRESULT STDMETHODCALLTYPE buffer_lock(IDirectSoundBuffer8 *This, DWORD offset, DWORD bytes, LPVOID *audio1,
                                             LPDWORD bytes1, LPVOID *audio2, LPDWORD bytes2, DWORD flags)
{
    DSBUFFER *b = enter(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (!audio1 || !bytes1)
        return leave(DSERR_INVALIDPARAM);
    *audio1 = nullptr;
    *bytes1 = 0;
    if (audio2)
        *audio2 = nullptr;
    if (bytes2)
        *bytes2 = 0;
    if (flags & ~(DSBLOCK_FROMWRITECURSOR | DSBLOCK_ENTIREBUFFER))
        return leave(DSERR_INVALIDPARAM);
    if (b->lost)
        return leave(DSERR_BUFFERLOST);
    if (b->primary && b->device->level != DSSCL_WRITEPRIMARY)
        return leave(DSERR_PRIOLEVELNEEDED);
    if (flags & DSBLOCK_FROMWRITECURSOR) {
        DWORD play;
        DsMixerCursors(b, &play, &offset);
    }
    if (flags & DSBLOCK_ENTIREBUFFER)
        bytes = b->size;
    if (offset >= b->size || bytes > b->size || !bytes)
        return leave(DSERR_INVALIDPARAM);
    DWORD first = bytes, second = 0;
    if (offset + bytes > b->size) {
        first = b->size - offset;
        second = bytes - first;
    }
    /* a region already locked cannot be locked again. */
    for (DWORD i = 0; i < b->lock_count; i++)
        if (overlaps(offset, first, b->locks[i].offset, b->locks[i].bytes) ||
            overlaps(0, second, b->locks[i].offset, b->locks[i].bytes))
            return leave(DSERR_INVALIDPARAM);
    /* windows keeps every region; past a few the oldest is forgotten. */
    DWORD needed = second && audio2 ? 2 : 1;
    if (b->lock_count + needed > ARRAYSIZE(b->locks)) {
        memmove(b->locks, b->locks + needed, (b->lock_count - needed) * sizeof(b->locks[0]));
        b->lock_count -= needed;
    }
    b->locks[b->lock_count++] = (typeof(b->locks[0])){offset, first};
    *audio1 = b->data + offset;
    *bytes1 = first;
    if (second && audio2) {
        b->locks[b->lock_count++] = (typeof(b->locks[0])){0, second};
        *audio2 = b->data;
        if (bytes2)
            *bytes2 = second;
    }
    return leave(DS_OK);
}

static HRESULT start(DSBUFFER *b, DWORD priority, DWORD flags)
{
    DSDEVICE *d = b->device;
    if (b->primary) {
        if (flags != DSBPLAY_LOOPING || priority)
            return DSERR_INVALIDPARAM;
        if (b->lost)
            return DSERR_BUFFERLOST;
        b->looping = TRUE;
        if (!b->playing) {
            b->playing = TRUE;
            DsMixerAdd(b);
        }
        return DS_OK;
    }
    /* a level has to be set, for a window of some thread. */
    if (!d->thread || !d->level)
        return DSERR_PRIOLEVELNEEDED;
    if (priority && !(b->flags & DSBCAPS_LOCDEFER))
        return DSERR_INVALIDPARAM;
    if ((flags & 0x3E) && !(b->flags & DSBCAPS_LOCDEFER))
        return DSERR_INVALIDPARAM;
    if (b->lost)
        return DSERR_BUFFERLOST;
    if (flags & DSBPLAY_LOCHARDWARE)
        return DSERR_INVALIDCALL;
    b->priority = priority;
    b->looping = (flags & DSBPLAY_LOOPING) != 0;
    if (!b->playing) {
        b->last_play = b->position;
        b->notify_play = b->position;
        b->fraction = 0;
        b->ended = FALSE;
        b->playing = TRUE;
        DsMixerAdd(b);
    }
    return DS_OK;
}

static HRESULT STDMETHODCALLTYPE buffer_play(IDirectSoundBuffer8 *This, DWORD reserved, DWORD priority, DWORD flags)
{
    DSBUFFER *b = enter(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (reserved || (flags & ~0x3Fu) ||
        (flags & (DSBPLAY_TERMINATEBY_TIME | DSBPLAY_TERMINATEBY_DISTANCE)) ==
            (DSBPLAY_TERMINATEBY_TIME | DSBPLAY_TERMINATEBY_DISTANCE))
        return leave(DSERR_INVALIDPARAM);
    HRESULT hr = start(b, priority, flags);
    return leave(hr);
}

static HRESULT STDMETHODCALLTYPE buffer_set_position(IDirectSoundBuffer8 *This, DWORD position)
{
    DSBUFFER *b = enter(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (b->lost)
        return leave(DSERR_BUFFERLOST);
    if (b->primary)
        return leave(DSERR_INVALIDCALL);
    if (position >= b->size)
        return leave(DSERR_INVALIDPARAM);
    position -= position % b->format->nBlockAlign;
    b->position = position;
    b->fraction = 0;
    b->last_play = position;
    b->notify_play = position;
    return leave(DS_OK);
}

/* the primary buffer's format is the device's at DSSCL_PRIORITY and above. */
static HRESULT STDMETHODCALLTYPE buffer_set_format(IDirectSoundBuffer8 *This, LPCWAVEFORMATEX format)
{
    DSBUFFER *b = enter(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (!format || !DsValidFormat(format))
        return leave(DSERR_INVALIDPARAM);
    if (!b->primary)
        return leave(DSERR_INVALIDCALL);
    if (b->device->level < DSSCL_PRIORITY)
        return leave(DSERR_PRIOLEVELNEEDED);
    WAVEFORMATEX *copy = HeapAlloc(heap(), 0, DsFormatSize(format) + 2);
    if (!copy)
        return leave(DSERR_OUTOFMEMORY);
    DsCopyFormat(copy, format);
    /* the primary buffer keeps 32 kb, aligned to the new frames. */
    DWORD size = 32768;
    if (copy->nBlockAlign && size % copy->nBlockAlign)
        size += copy->nBlockAlign - size % copy->nBlockAlign;
    BYTE *data = HeapAlloc(heap(), 0, size);
    if (!data) {
        HeapFree(heap(), 0, copy);
        return leave(DSERR_OUTOFMEMORY);
    }
    silence(data, size, copy);
    BOOL was_playing = b->playing;
    DsMixerStop(b);
    HeapFree(heap(), 0, b->format);
    HeapFree(heap(), 0, b->data);
    b->format = copy;
    b->data = data;
    b->size = size;
    b->original_rate = b->rate = b->frequency = copy->nSamplesPerSec;
    b->position = b->last_play = 0;
    b->lead = play_lead(b);
    DsBufferUpdateGains(b);
    if (was_playing)
        start(b, 0, DSBPLAY_LOOPING);
    return leave(DS_OK);
}

static HRESULT STDMETHODCALLTYPE buffer_set_volume(IDirectSoundBuffer8 *This, LONG volume)
{
    DSBUFFER *b = enter(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (volume > DSBVOLUME_MAX || volume < DSBVOLUME_MIN)
        return leave(DSERR_INVALIDPARAM);
    if (!(b->flags & DSBCAPS_CTRLVOLUME))
        return leave(DSERR_CONTROLUNAVAIL);
    if (b->volume != volume) {
        b->volume = volume;
        DsBufferUpdateGains(b);
    }
    return leave(DS_OK);
}

static HRESULT STDMETHODCALLTYPE buffer_set_pan(IDirectSoundBuffer8 *This, LONG pan)
{
    DSBUFFER *b = enter(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (pan < DSBPAN_LEFT || pan > DSBPAN_RIGHT)
        return leave(DSERR_INVALIDPARAM);
    if (!(b->flags & DSBCAPS_CTRLPAN))
        return leave(DSERR_CONTROLUNAVAIL);
    if (b->pan != pan) {
        b->pan = pan;
        DsBufferUpdateGains(b);
    }
    return leave(DS_OK);
}

static HRESULT STDMETHODCALLTYPE buffer_set_frequency(IDirectSoundBuffer8 *This, DWORD frequency)
{
    DSBUFFER *b = enter(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (frequency && (frequency < DSBFREQUENCY_MIN || frequency > DSBFREQUENCY_MAX))
        return leave(DSERR_INVALIDPARAM);
    if (b->primary || !(b->flags & DSBCAPS_CTRLFREQUENCY))
        return leave(DSERR_CONTROLUNAVAIL);
    if (!frequency)
        frequency = b->original_rate;
    b->frequency = frequency;
    if (b->has3d)
        Ds3dApply(b);
    else
        b->rate = frequency;
    return leave(DS_OK);
}

static HRESULT STDMETHODCALLTYPE buffer_stop(IDirectSoundBuffer8 *This)
{
    DSBUFFER *b = enter(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (b->lost)
        return leave(DSERR_BUFFERLOST);
    DsMixerStop(b);
    return leave(DS_OK);
}

/* each pointer must start a region locked now; the regions end. */
static HRESULT STDMETHODCALLTYPE buffer_unlock(IDirectSoundBuffer8 *This, LPVOID audio1, DWORD bytes1, LPVOID audio2,
                                               DWORD bytes2)
{
    DSBUFFER *b = enter(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if ((!audio1 && bytes1) || (bytes2 && !audio2))
        return leave(DSERR_INVALIDPARAM);
    if (b->lost)
        return leave(DSERR_BUFFERLOST);
    if (b->primary && b->device->level != DSSCL_WRITEPRIMARY)
        return leave(DSERR_PRIOLEVELNEEDED);
    void *parts[2] = {audio1, audio2};
    for (int n = 0; n < 2; n++) {
        if (!parts[n])
            continue;
        BOOL found = FALSE;
        for (DWORD i = 0; i < b->lock_count; i++)
            if ((BYTE *)parts[n] == b->data + b->locks[i].offset) {
                b->locks[i] = b->locks[--b->lock_count];
                found = TRUE;
                break;
            }
        if (!found)
            return leave(DSERR_INVALIDPARAM);
    }
    return leave(DS_OK);
}

static HRESULT STDMETHODCALLTYPE buffer_restore(IDirectSoundBuffer8 *This)
{
    DSBUFFER *b = enter(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    DsUpdateFocus();
    if (b->lost)
        return leave(DSERR_BUFFERLOST);
    return leave(DS_OK);
}

/* effects need windows' dsdmo objects, which are not there; a buffer created for them takes none. */
static HRESULT STDMETHODCALLTYPE buffer_set_fx(IDirectSoundBuffer8 *This, DWORD count, LPDSEFFECTDESC effects,
                                               LPDWORD results)
{
    DSBUFFER *b = enter(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if ((count && !effects) || (!count && (effects || results)))
        return leave(DSERR_INVALIDPARAM);
    if (!(b->flags & DSBCAPS_CTRLFX))
        return leave(DSERR_CONTROLUNAVAIL);
    if (b->playing)
        return leave(DSERR_INVALIDCALL);
    if (b->lock_count)
        return leave(DSERR_INVALIDCALL);
    if (!count)
        return leave(DS_OK);
    for (DWORD i = 0; i < count; i++)
        if (results)
            results[i] = DSFXR_UNKNOWN;
    return leave(DSERR_FXUNAVAILABLE);
}

static HRESULT STDMETHODCALLTYPE buffer_acquire(IDirectSoundBuffer8 *This, DWORD flags, DWORD count, LPDWORD results)
{
    DSBUFFER *b = enter(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if ((flags & 0xFFFFFFC1) || (flags & 0x18) == 0x18 || (!results) != (!count))
        return leave(DSERR_INVALIDPARAM);
    if (!(b->flags & DSBCAPS_LOCDEFER))
        return leave(DSERR_INVALIDCALL);
    if (flags & DSBPLAY_LOCHARDWARE)
        return leave(DSERR_INVALIDCALL);
    return leave(DS_OK);
}

static HRESULT STDMETHODCALLTYPE buffer_get_object(IDirectSoundBuffer8 *This, REFGUID object, DWORD index,
                                                   REFGUID iid, LPVOID *out)
{
    DSBUFFER *b = enter(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (!object || !iid || !out)
        return leave(DSERR_INVALIDPARAM);
    *out = nullptr;
    if (!(b->flags & DSBCAPS_CTRLFX))
        return leave(DSERR_CONTROLUNAVAIL);
    return leave(DSERR_OBJECTNOTFOUND);
}

static const IDirectSoundBuffer8Vtbl buffer_vtbl = {
    buffer_query,         buffer_add_ref,       buffer_release,     buffer_get_caps,     buffer_get_position,
    buffer_get_format,    buffer_get_volume,    buffer_get_pan,     buffer_get_frequency, buffer_get_status,
    buffer_initialize,    buffer_lock,          buffer_play,        buffer_set_position, buffer_set_format,
    buffer_set_volume,    buffer_set_pan,       buffer_set_frequency, buffer_stop,       buffer_unlock,
    buffer_restore,       buffer_set_fx,        buffer_acquire,     buffer_get_object,
};

/* ------------------------------------------------------------------ IDirectSoundNotify */

static DSBUFFER *from_notify(IDirectSoundNotify *iface)
{
    return CONTAINING_RECORD(iface, DSBUFFER, notify_iface);
}

static HRESULT STDMETHODCALLTYPE notify_query(IDirectSoundNotify *This, REFIID riid, void **out)
{
    return DsBufferQuery(from_notify(This), riid, out);
}

static ULONG STDMETHODCALLTYPE notify_add_ref(IDirectSoundNotify *This)
{
    return InterlockedIncrement(&from_notify(This)->ref);
}

static ULONG STDMETHODCALLTYPE notify_release(IDirectSoundNotify *This)
{
    return DsBufferRelease(from_notify(This));
}

/* offsets aligned down to frames and sorted; each below the size or DSBPN_OFFSETSTOP last, none twice. */
static HRESULT STDMETHODCALLTYPE notify_set(IDirectSoundNotify *This, DWORD count, LPCDSBPOSITIONNOTIFY notifies)
{
    DSBUFFER *b = from_notify(This);
    if (b->magic != BUFFER_MAGIC || count > DSBNOTIFICATIONS_MAX || (count && !notifies))
        return DSERR_INVALIDPARAM;
    RtlEnterCriticalSection(&DsLock);
    if (!b->device)
        return leave(DSERR_INVALIDPARAM);
    if (!(b->flags & DSBCAPS_CTRLPOSITIONNOTIFY))
        return leave(DSERR_CONTROLUNAVAIL);
    NOTIFY *list = count ? HeapAlloc(heap(), 0, count * sizeof(*list)) : nullptr;
    if (count && !list)
        return leave(DSERR_OUTOFMEMORY);
    DWORD align = b->format->nBlockAlign;
    for (DWORD i = 0; i < count; i++) {
        list[i].offset = notifies[i].dwOffset;
        if (list[i].offset != DSBPN_OFFSETSTOP)
            list[i].offset -= list[i].offset % align;
        list[i].event = notifies[i].hEventNotify;
    }
    for (DWORD i = 1; i < count; i++)
        for (DWORD j = i; j && list[j - 1].offset > list[j].offset; j--) {
            NOTIFY t = list[j];
            list[j] = list[j - 1];
            list[j - 1] = t;
        }
    for (DWORD i = 0; i < count; i++) {
        BOOL bad = (list[i].offset != DSBPN_OFFSETSTOP && list[i].offset >= b->size) || !list[i].event;
        if (i + 1 < count && (list[i].offset == DSBPN_OFFSETSTOP || list[i].offset == list[i + 1].offset))
            bad = TRUE;
        if (bad) {
            HeapFree(heap(), 0, list);
            return leave(DSERR_INVALIDPARAM);
        }
    }
    if (b->playing) {
        HeapFree(heap(), 0, list);
        return leave(DSERR_INVALIDCALL);
    }
    HeapFree(heap(), 0, b->notifies);
    b->notifies = list;
    b->notify_count = count;
    return leave(DS_OK);
}

static const IDirectSoundNotifyVtbl notify_vtbl = {notify_query, notify_add_ref, notify_release, notify_set};

/* ------------------------------------------------------------------ IKsPropertySet */

static DSBUFFER *from_props(IKsPropertySet *iface)
{
    return CONTAINING_RECORD(iface, DSBUFFER, props);
}

static HRESULT STDMETHODCALLTYPE props_query(IKsPropertySet *This, REFIID riid, void **out)
{
    return DsBufferQuery(from_props(This), riid, out);
}

static ULONG STDMETHODCALLTYPE props_add_ref(IKsPropertySet *This)
{
    return InterlockedIncrement(&from_props(This)->ref);
}

static ULONG STDMETHODCALLTYPE props_release(IKsPropertySet *This)
{
    return DsBufferRelease(from_props(This));
}

/* no property set reaches a driver: eax and its kin are for hardware the engine never had. */
static HRESULT STDMETHODCALLTYPE props_get(IKsPropertySet *This, REFGUID set, ULONG id, LPVOID instance,
                                           ULONG instance_size, LPVOID data, ULONG data_size, PULONG returned)
{
    if (!set || (!data && data_size))
        return DSERR_INVALIDPARAM;
    if (returned)
        *returned = 0;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE props_set(IKsPropertySet *This, REFGUID set, ULONG id, LPVOID instance,
                                           ULONG instance_size, LPVOID data, ULONG data_size)
{
    if (!set || (!data && data_size))
        return DSERR_INVALIDPARAM;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE props_support(IKsPropertySet *This, REFGUID set, ULONG id, PULONG support)
{
    if (!set || !support)
        return DSERR_INVALIDPARAM;
    *support = 0;
    return E_NOTIMPL;
}

static const IKsPropertySetVtbl props_vtbl = {props_query, props_add_ref, props_release, props_get, props_set,
                                              props_support};
