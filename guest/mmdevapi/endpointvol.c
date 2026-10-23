/*
 * IAudioEndpointVolume of the two endpoints. levels are kept in the process and reported to registered callbacks,
 * the host's own mixer is not touched.
 */
#include "mmdevapi_private.h"

#define CHANNELS 2
#define STEPS 101
#define MAX_CALLBACKS 8
#define MIN_DB (-96.0f)

typedef struct NOTIFICATION {
    GUID context;
    BOOL mute;
    float master;
    UINT channels;
    float levels[CHANNELS];
} NOTIFICATION;

typedef struct ENDPOINT_VOLUME {
    const IAudioEndpointVolumeVtbl *vtbl;
    SRWLOCK lock;
    float master, channel[CHANNELS];
    BOOL mute;
    void *callbacks[MAX_CALLBACKS];
} ENDPOINT_VOLUME;

static ENDPOINT_VOLUME g_volumes[2];

/* amplitude to decibels and back, without a math library. */
static double ln_of(double x)
{
    union {
        double d;
        unsigned long long u;
    } bits = {x};
    int exponent = (int)((bits.u >> 52) & 0x7FF) - 1023;
    bits.u = (bits.u & 0x000FFFFFFFFFFFFFull) | 0x3FF0000000000000ull;
    double t = (bits.d - 1) / (bits.d + 1), t2 = t * t;
    double series = t * (1 + t2 * (1.0 / 3 + t2 * (1.0 / 5 + t2 * (1.0 / 7 + t2 * (1.0 / 9)))));
    return 2 * series + exponent * 0.6931471805599453;
}

static double exp_of(double x)
{
    double scaled = x * 1.4426950408889634;
    int whole = (int)scaled - (scaled < 0 && scaled != (int)scaled);
    double f = (scaled - whole) * 0.6931471805599453;
    double term = 1, sum = 1;
    for (int i = 1; i < 12; i++) {
        term = term * f / i;
        sum += term;
    }
    union {
        double d;
        unsigned long long u;
    } power = {0};
    power.u = (unsigned long long)(whole + 1023) << 52;
    return sum * power.d;
}

static float to_decibels(float level)
{
    if (level <= 0)
        return MIN_DB;
    float db = (float)(ln_of(level) * 8.685889638065035);
    return db < MIN_DB ? MIN_DB : db;
}

static float from_decibels(float db)
{
    if (db <= MIN_DB)
        return 0;
    return db >= 0 ? 1.0f : (float)exp_of(db * 0.11512925464970229);
}

static float clamp(float level)
{
    return level < 0 ? 0 : level > 1 ? 1 : level;
}

/* the callbacks are called without the lock, from the thread that changed the level. */
static void notify(ENDPOINT_VOLUME *v, LPCGUID context)
{
    void *callbacks[MAX_CALLBACKS];
    NOTIFICATION data = {};
    AcquireSRWLockExclusive(&v->lock);
    data.mute = v->mute;
    data.master = v->master;
    data.channels = CHANNELS;
    for (int i = 0; i < CHANNELS; i++)
        data.levels[i] = v->channel[i];
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        callbacks[i] = v->callbacks[i];
        if (callbacks[i])
            (*(IAudioEndpointVolumeCallbackVtbl **)callbacks[i])->AddRef(callbacks[i]);
    }
    ReleaseSRWLockExclusive(&v->lock);
    if (context)
        data.context = *context;
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (!callbacks[i])
            continue;
        IAudioEndpointVolumeCallbackVtbl *vtbl = *(IAudioEndpointVolumeCallbackVtbl **)callbacks[i];
        vtbl->OnNotify(callbacks[i], &data);
        vtbl->Release(callbacks[i]);
    }
}

static HRESULT STDMETHODCALLTYPE ev_query(void *This, REFIID riid, void **object)
{
    if (!object)
        return E_POINTER;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IAudioEndpointVolume)) {
        *object = This;
        return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE ev_ref(void *This) { return 2; }

static HRESULT STDMETHODCALLTYPE ev_register(void *This, void *callback)
{
    ENDPOINT_VOLUME *v = This;
    if (!callback)
        return E_POINTER;
    HRESULT hr = E_OUTOFMEMORY;
    AcquireSRWLockExclusive(&v->lock);
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (!v->callbacks[i]) {
            v->callbacks[i] = callback;
            (*(IAudioEndpointVolumeCallbackVtbl **)callback)->AddRef(callback);
            hr = S_OK;
            break;
        }
    }
    ReleaseSRWLockExclusive(&v->lock);
    return hr;
}

static HRESULT STDMETHODCALLTYPE ev_unregister(void *This, void *callback)
{
    ENDPOINT_VOLUME *v = This;
    if (!callback)
        return E_POINTER;
    HRESULT hr = E_INVALIDARG;
    AcquireSRWLockExclusive(&v->lock);
    for (int i = 0; i < MAX_CALLBACKS; i++) {
        if (v->callbacks[i] == callback) {
            v->callbacks[i] = nullptr;
            hr = S_OK;
            break;
        }
    }
    ReleaseSRWLockExclusive(&v->lock);
    if (hr == S_OK)
        (*(IAudioEndpointVolumeCallbackVtbl **)callback)->Release(callback);
    return hr;
}

static HRESULT STDMETHODCALLTYPE ev_channel_count(void *This, UINT *count)
{
    if (!count)
        return E_POINTER;
    *count = CHANNELS;
    return S_OK;
}

static HRESULT set_master(ENDPOINT_VOLUME *v, float level, LPCGUID context)
{
    if (!(level == level))
        return E_INVALIDARG;
    AcquireSRWLockExclusive(&v->lock);
    v->master = clamp(level);
    ReleaseSRWLockExclusive(&v->lock);
    notify(v, context);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ev_set_master_db(void *This, float db, LPCGUID context)
{
    if (db > 0 || !(db == db))
        return E_INVALIDARG;
    return set_master(This, from_decibels(db), context);
}

static HRESULT STDMETHODCALLTYPE ev_set_master(void *This, float level, LPCGUID context)
{
    if (level < 0 || level > 1)
        return E_INVALIDARG;
    return set_master(This, level, context);
}

static HRESULT STDMETHODCALLTYPE ev_get_master_db(void *This, float *db)
{
    if (!db)
        return E_POINTER;
    *db = to_decibels(((ENDPOINT_VOLUME *)This)->master);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ev_get_master(void *This, float *level)
{
    if (!level)
        return E_POINTER;
    *level = ((ENDPOINT_VOLUME *)This)->master;
    return S_OK;
}

static HRESULT set_channel(ENDPOINT_VOLUME *v, UINT channel, float level, LPCGUID context)
{
    if (channel >= CHANNELS)
        return E_INVALIDARG;
    AcquireSRWLockExclusive(&v->lock);
    v->channel[channel] = clamp(level);
    ReleaseSRWLockExclusive(&v->lock);
    notify(v, context);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ev_set_channel_db(void *This, UINT channel, float db, LPCGUID context)
{
    if (db > 0 || !(db == db))
        return E_INVALIDARG;
    return set_channel(This, channel, from_decibels(db), context);
}

static HRESULT STDMETHODCALLTYPE ev_set_channel(void *This, UINT channel, float level, LPCGUID context)
{
    if (level < 0 || level > 1)
        return E_INVALIDARG;
    return set_channel(This, channel, level, context);
}

static HRESULT STDMETHODCALLTYPE ev_get_channel_db(void *This, UINT channel, float *db)
{
    if (!db)
        return E_POINTER;
    if (channel >= CHANNELS)
        return E_INVALIDARG;
    *db = to_decibels(((ENDPOINT_VOLUME *)This)->channel[channel]);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ev_get_channel(void *This, UINT channel, float *level)
{
    if (!level)
        return E_POINTER;
    if (channel >= CHANNELS)
        return E_INVALIDARG;
    *level = ((ENDPOINT_VOLUME *)This)->channel[channel];
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ev_set_mute(void *This, BOOL mute, LPCGUID context)
{
    ENDPOINT_VOLUME *v = This;
    AcquireSRWLockExclusive(&v->lock);
    v->mute = mute != FALSE;
    ReleaseSRWLockExclusive(&v->lock);
    notify(v, context);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ev_get_mute(void *This, BOOL *mute)
{
    if (!mute)
        return E_POINTER;
    *mute = ((ENDPOINT_VOLUME *)This)->mute;
    return S_OK;
}

static UINT step_of(float level)
{
    return (UINT)(level * (STEPS - 1) + 0.5f);
}

static HRESULT STDMETHODCALLTYPE ev_step_info(void *This, UINT *step, UINT *count)
{
    if (!step || !count)
        return E_POINTER;
    *step = step_of(((ENDPOINT_VOLUME *)This)->master);
    *count = STEPS;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ev_step_up(void *This, LPCGUID context)
{
    UINT step = step_of(((ENDPOINT_VOLUME *)This)->master);
    return set_master(This, (step + 1 >= STEPS ? STEPS - 1 : step + 1) / (float)(STEPS - 1), context);
}

static HRESULT STDMETHODCALLTYPE ev_step_down(void *This, LPCGUID context)
{
    UINT step = step_of(((ENDPOINT_VOLUME *)This)->master);
    return set_master(This, (step ? step - 1 : 0) / (float)(STEPS - 1), context);
}

static HRESULT STDMETHODCALLTYPE ev_hardware(void *This, DWORD *mask)
{
    if (!mask)
        return E_POINTER;
    *mask = 0;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE ev_range(void *This, float *minimum, float *maximum, float *increment)
{
    if (!minimum || !maximum || !increment)
        return E_POINTER;
    *minimum = MIN_DB;
    *maximum = 0;
    *increment = 0.03125f;
    return S_OK;
}

static const IAudioEndpointVolumeVtbl ev_vtbl = {
    ev_query,          ev_ref,           ev_ref,          ev_register,      ev_unregister,    ev_channel_count,
    ev_set_master_db,  ev_set_master,    ev_get_master_db, ev_get_master,   ev_set_channel_db, ev_set_channel,
    ev_get_channel_db, ev_get_channel,   ev_set_mute,     ev_get_mute,      ev_step_info,     ev_step_up,
    ev_step_down,      ev_hardware,      ev_range,
};

void *MmEndpointVolume(BOOL capture)
{
    ENDPOINT_VOLUME *v = &g_volumes[capture ? 1 : 0];
    if (!v->vtbl) {
        AcquireSRWLockExclusive(&v->lock);
        if (!v->vtbl) {
            v->master = 1.0f;
            v->channel[0] = v->channel[1] = 1.0f;
            v->vtbl = &ev_vtbl;
        }
        ReleaseSRWLockExclusive(&v->lock);
    }
    return v;
}
