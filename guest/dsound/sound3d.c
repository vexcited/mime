/*
 * IDirectSound3DBuffer and IDirectSound3DListener, and what windows'
 * software 3d object makes of them: a volume from the distance and the
 * cone, a power for each speaker from the direction, a frequency from the
 * doppler shift. its constants and roundings are windows'.
 */
#include "dsound_private.h"

#define PI 3.14159265358979323846

enum {
    SET_POSITION = 1,
    SET_VELOCITY = 2,
    SET_CONE_ANGLES = 4,
    SET_CONE_ORIENTATION = 8,
    SET_CONE_VOLUME = 16,
    SET_MIN_DISTANCE = 32,
    SET_MAX_DISTANCE = 64,
    SET_MODE = 128,
    SET_ORIENTATION = 256,
    SET_DISTANCE_FACTOR = 512,
    SET_ROLLOFF_FACTOR = 1024,
    SET_DOPPLER_FACTOR = 2048,
};

/* ------------------------------------------------------------------ math */

static double square_root(double x)
{
    return __builtin_elementwise_sqrt(x);
}

/* the natural logarithm of a positive number: its exponent, and atanh of the mantissa's distance from 1. */
static double natural_log(double x)
{
    union {
        double d;
        UINT64 u;
    } v = {x};
    int e = (int)((v.u >> 52) & 0x7FF) - 1023;
    v.u = (v.u & 0x000FFFFFFFFFFFFFull) | 0x3FF0000000000000ull;
    double m = v.d;
    if (m > 1.4142135623730951) {
        m *= 0.5;
        e++;
    }
    double s = (m - 1) / (m + 1), s2 = s * s, term = s, sum = 0;
    for (int k = 1; k < 30; k += 2) {
        sum += term / k;
        term *= s2;
    }
    return 2 * sum + e * 0.6931471805599453;
}

static double power_of_two(double y)
{
    if (y < -1000)
        return 0;
    double n = (double)(INT64)y;
    if (n > y)
        n -= 1;
    double f = (y - n) * 0.6931471805599453;
    double sum = 1, term = 1;
    for (int k = 1; k < 20; k++) {
        term *= f / k;
        sum += term;
    }
    union {
        double d;
        UINT64 u;
    } scale = {.u = (UINT64)((int)n + 1023) << 52};
    return sum * scale.d;
}

/* the sine and cosine of angles within a few turns. */
static double sine(double x)
{
    while (x > PI)
        x -= 2 * PI;
    while (x < -PI)
        x += 2 * PI;
    if (x > PI / 2)
        x = PI - x;
    else if (x < -PI / 2)
        x = -PI - x;
    double x2 = x * x, term = x, sum = x;
    for (int k = 2; k < 30; k += 2) {
        term *= -x2 / (k * (k + 1));
        sum += term;
    }
    return sum;
}

static double cosine(double x)
{
    return sine(x + PI / 2);
}

static double tangent(double x)
{
    return sine(x) / cosine(x);
}

static double arctangent(double t)
{
    /* two halvings bring it under the tangent of pi/16, where the series is short. */
    BOOL invert = t > 1 || t < -1;
    if (invert)
        t = 1 / t;
    t = t / (1 + square_root(1 + t * t));
    t = t / (1 + square_root(1 + t * t));
    double t2 = t * t, term = t, sum = 0;
    for (int k = 1; k < 40; k += 2) {
        sum += term / k;
        term *= -t2;
    }
    sum *= 4;
    if (invert)
        sum = (sum > 0 ? PI / 2 : -PI / 2) - sum;
    return sum;
}

static double arctangent2(double y, double x)
{
    if (x == 0)
        return y > 0 ? PI / 2 : y < 0 ? -PI / 2 : 0;
    double a = arctangent(y / x);
    if (x < 0)
        a += y >= 0 ? PI : -PI;
    return a;
}

/* ------------------------------------------------------------------ vectors */

static D3DVECTOR sub(D3DVECTOR a, D3DVECTOR b)
{
    return (D3DVECTOR){a.x - b.x, a.y - b.y, a.z - b.z};
}

static float dot(D3DVECTOR a, D3DVECTOR b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static D3DVECTOR cross(D3DVECTOR a, D3DVECTOR b)
{
    return (D3DVECTOR){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

static float magnitude(D3DVECTOR v)
{
    return (float)square_root((double)v.x * v.x + (double)v.y * v.y + (double)v.z * v.z);
}

static BOOL empty(D3DVECTOR v)
{
    return v.x == 0 && v.y == 0 && v.z == 0;
}

static BOOL normalize(D3DVECTOR *v)
{
    float m = magnitude(*v);
    if (m == 0)
        return FALSE;
    float k = 1.0f / m;
    *v = (D3DVECTOR){v->x * k, v->y * k, v->z * k};
    return TRUE;
}

/* windows' CheckVector: a vector longer than the floats reach is scaled back inside them. */
static D3DVECTOR check_vector(D3DVECTOR v)
{
    double m = square_root((double)v.x * v.x + (double)v.y * v.y + (double)v.z * v.z);
    if (m > 3.402823466385289e38) {
        double k = 3.368795231721436e38 / m;
        v = (D3DVECTOR){(float)(k * v.x), (float)(k * v.y), (float)(k * v.z)};
    }
    return v;
}

static BOOL is_nan(float f)
{
    return f != f;
}

/* a vector in the listener's frame: x to its right, y above, z ahead. */
static D3DVECTOR head_relative(D3DVECTOR v, const DS3DLISTENER *l)
{
    D3DVECTOR front = l->vOrientFront, top = l->vOrientTop;
    normalize(&front);
    D3DVECTOR right = cross(top, front);
    normalize(&right);
    D3DVECTOR up = cross(front, right);
    return (D3DVECTOR){dot(v, right), dot(v, up), dot(v, front)};
}

/* ------------------------------------------------------------------ the model */

void Ds3dDefaults(DSBUFFER *b)
{
    DS3DBUFFER *p = &b->sound3d.params;
    memset(p, 0, sizeof(*p));
    p->dwSize = sizeof(*p);
    p->dwInsideConeAngle = DS3D_DEFAULTCONEANGLE;
    p->dwOutsideConeAngle = DS3D_DEFAULTCONEANGLE;
    p->vConeOrientation.z = 1;
    p->lConeOutsideVolume = DS3D_DEFAULTCONEOUTSIDEVOLUME;
    p->flMinDistance = DS3D_DEFAULTMINDISTANCE;
    p->flMaxDistance = DS3D_DEFAULTMAXDISTANCE;
    p->dwMode = DS3DMODE_NORMAL;
    b->sound3d.deferred = *p;
    b->sound3d.dirty = 0;
}

void DsListenerDefaults(LISTENER3D *l)
{
    DS3DLISTENER *p = &l->params;
    memset(p, 0, sizeof(*p));
    p->dwSize = sizeof(*p);
    p->vOrientFront.z = 1;
    p->vOrientTop.y = 1;
    p->flDistanceFactor = DS3D_DEFAULTDISTANCEFACTOR;
    p->flRolloffFactor = DS3D_DEFAULTROLLOFFFACTOR;
    p->flDopplerFactor = DS3D_DEFAULTDOPPLERFACTOR;
    l->deferred = *p;
    l->dirty = 0;
}

/* CMultiPan3dObject::DistributeSignal: power between the two speakers around the direction, by a tangent law. */
static void distribute(double power, double azimuth, const double *speakers, int count, double *out)
{
    double below = -4.0, above = 4.0;
    int low = -1, high = -1;
    for (int i = 0; i < count; i++) {
        double d = azimuth - speakers[i];
        if (d > 3.141592741012573)
            d -= 6.283185482025146;
        else if (d < -3.141592741012573)
            d += 6.283185482025146;
        if (d <= 0.0 && d > below) {
            low = i;
            below = d;
        } else if (d >= 0.0 && above > d) {
            high = i;
            above = d;
        }
    }
    below = below < 0 ? -below : below;
    above = above < 0 ? -above : above;
    if (low == -1 || high == -1)
        low = high = 0;
    if (below < 0.00001) {
        out[low] += power;
    } else if (above < 0.00001) {
        out[high] += power;
    } else {
        double t = tangent(below * 1.570796370506287 / (above + below));
        double share = power / (t + 1.0);
        out[low] += share;
        out[high] += t * share;
    }
}

/* CMultiPan3dObject::CalculatePanValues, for the stereo speakers at -90 and 90 degrees. */
static void pan_values(D3DVECTOR at, LONG pans[2])
{
    static const double speakers[2] = {-1.5707963705062866, 1.5707963705062866};
    double power[2] = {0, 0};
    float x = at.x < 0 ? -at.x : at.x, z = at.z < 0 ? -at.z : at.z;
    double azimuth = arctangent2(at.x, at.z);
    if (x < 0.00001 && z < 0.00001) {
        power[0] = power[1] = 0.5;
    } else {
        double near = 1.0;
        double d;
        if (x < 2.5f && z < 2.5f && (d = square_root((float)(z * z + x * x))) < 2.5) {
            /* close by, part of the sound comes from the other side. */
            near = d / 5.0 + 0.5;
            distribute(near, azimuth, speakers, 2, power);
            azimuth += 3.141592741012573;
            near = 1.0 - near;
        }
        distribute(near, azimuth, speakers, 2, power);
    }
    for (int i = 0; i < 2; i++)
        pans[i] = power[i] == 0.0 ? -10000 : (LONG)(natural_log(power[i]) * 434.2944881588036);
}

/* CSw3dObject::UpdateConeAttenuation. */
static float cone_attenuation(const DS3DBUFFER *p, const DS3DLISTENER *l)
{
    if (p->dwInsideConeAngle >= 360 || p->dwMode == DS3DMODE_DISABLE)
        return 1.0f;
    D3DVECTOR origin = p->dwMode == DS3DMODE_NORMAL ? l->vPosition : (D3DVECTOR){0, 0, 0};
    D3DVECTOR to_listener = sub(origin, p->vPosition);
    if (empty(to_listener))
        return 1.0f;
    float c = dot(to_listener, p->vConeOrientation) / magnitude(to_listener);
    float inside = (float)cosine((float)(int)p->dwInsideConeAngle * 0.0087266462f);
    float outside = (float)cosine((float)(int)p->dwOutsideConeAngle * 0.0087266462f);
    if (c > inside)
        return 1.0f;
    float volume = (float)p->lConeOutsideVolume;
    if (c > outside)
        return (float)power_of_two((inside - c) / (inside - outside) * volume * 0.0016666667f);
    return (float)power_of_two(volume * 0.0016666667f);
}

/* C3dObject::UpdateDoppler. */
static DWORD doppler(const DS3DBUFFER *p, const DS3DLISTENER *l, DWORD frequency)
{
    if (l->flDopplerFactor <= 0.0f || p->dwMode == DS3DMODE_DISABLE)
        return frequency;
    if (empty(l->vVelocity) && empty(p->vVelocity))
        return frequency;
    float radial = 0;
    D3DVECTOR at = p->vPosition, velocity = p->vVelocity;
    if (p->dwMode == DS3DMODE_NORMAL) {
        at = sub(p->vPosition, l->vPosition);
        velocity = sub(p->vVelocity, l->vVelocity);
    }
    if (!empty(at))
        radial = dot(at, velocity) / magnitude(at);
    double speed = l->flDistanceFactor * radial * 1000.0 * l->flDopplerFactor;
    float clamped = speed < -3.402823466385289e38 ? -3.4028235e38f
                    : speed > 3.402823466385289e38 ? 3.4028235e38f
                                                   : (float)speed;
    if (clamped > 179830.0f)
        clamped = 179830.0f;
    if (clamped < -179830.0f)
        clamped = -179830.0f;
    return (DWORD)(int)((float)((float)frequency * 359660.0f) / (clamped + 359660.0f));
}

/*
 * CMultiPan3dObject::Commit3dChanges and the engine's channel
 * attenuations: the buffer's volume and the 3d volume on every speaker,
 * each speaker's power on top. a mono source feeds each speaker once,
 * every channel of a stereo one feeds every speaker.
 */
void Ds3dApply(DSBUFFER *b)
{
    DSDEVICE *d = b->device;
    const DS3DBUFFER *p = &b->sound3d.params;
    const DS3DLISTENER *l = &d->listener.params;
    LONG volume = b->volume, pans[2] = {0, 0};
    BOOL mute = FALSE;
    DWORD frequency = b->frequency ? b->frequency : b->original_rate;
    if (p->dwMode == DS3DMODE_DISABLE) {
        b->rate = frequency;
    } else {
        D3DVECTOR at = p->dwMode == DS3DMODE_HEADRELATIVE ? p->vPosition : head_relative(sub(p->vPosition, l->vPosition), l);
        float distance = magnitude(at), maximum = p->flMaxDistance;
        BOOL at_max = distance >= maximum;
        if (at_max)
            distance = maximum;
        float rolloff = l->flRolloffFactor, minimum = p->flMinDistance;
        float position = distance <= minimum || rolloff <= 0.0f
                             ? 1.0f
                             : minimum / ((distance - minimum) * rolloff + minimum);
        float attenuation = position * cone_attenuation(p, l);
        LONG volume3d;
        if ((at_max && (b->flags & DSBCAPS_MUTE3DATMAXDISTANCE)) || attenuation <= 0.0f)
            volume3d = -10000;
        else
            volume3d = -500 - (LONG)(natural_log(attenuation) * -868.588963806556);
        volume += volume3d;
        mute = volume <= -10000;
        pan_values(at, pans);
        b->rate = doppler(p, l, frequency);
        if (b->rate < DSBFREQUENCY_MIN)
            b->rate = DSBFREQUENCY_MIN;
    }
    memset(b->gain, 0, sizeof(b->gain));
    if (mute)
        return;
    float level = DsAmplitude(volume);
    for (int s = 0; s < 2; s++) {
        float g = DsAmplitude(pans[s]) * level;
        for (WORD c = 0; c < b->format->nChannels && c < 2; c++)
            b->gain[c][s] = g;
    }
}

void Ds3dApplyAll(DSDEVICE *d)
{
    for (DSBUFFER *b = d->buffers; b; b = b->next)
        if (b->has3d)
            Ds3dApply(b);
}

/* ------------------------------------------------------------------ IDirectSound3DBuffer */

static DSBUFFER *from_3d(IDirectSound3DBuffer *iface)
{
    if (!iface)
        return nullptr;
    DSBUFFER *b = CONTAINING_RECORD(iface, DSBUFFER, iface3d);
    return b->magic == BUFFER_MAGIC ? b : nullptr;
}

/* a buffer of a live device, with the lock held. */
static DSBUFFER *enter_3d(IDirectSound3DBuffer *iface)
{
    DSBUFFER *b = from_3d(iface);
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

static HRESULT STDMETHODCALLTYPE b3d_query(IDirectSound3DBuffer *This, REFIID riid, void **out)
{
    DSBUFFER *b = from_3d(This);
    return b ? DsBufferQuery(b, riid, out) : DSERR_INVALIDPARAM;
}

static ULONG STDMETHODCALLTYPE b3d_add_ref(IDirectSound3DBuffer *This)
{
    DSBUFFER *b = from_3d(This);
    return b ? InterlockedIncrement(&b->ref) : 0;
}

static ULONG STDMETHODCALLTYPE b3d_release(IDirectSound3DBuffer *This)
{
    DSBUFFER *b = from_3d(This);
    return b ? DsBufferRelease(b) : 0;
}

#define B3D_GET(name, type, expression)                                                                               \
    static HRESULT STDMETHODCALLTYPE b3d_get_##name(IDirectSound3DBuffer *This, type *out)                            \
    {                                                                                                                 \
        DSBUFFER *b = enter_3d(This);                                                                                 \
        if (!b)                                                                                                       \
            return DSERR_INVALIDPARAM;                                                                                \
        if (!out)                                                                                                     \
            return leave(DSERR_INVALIDPARAM);                                                                         \
        const DS3DBUFFER *p = &b->sound3d.params;                                                                     \
        *out = expression;                                                                                            \
        return leave(DS_OK);                                                                                          \
    }

B3D_GET(cone_orientation, D3DVECTOR, p->vConeOrientation)
B3D_GET(cone_outside_volume, LONG, p->lConeOutsideVolume)
B3D_GET(max_distance, D3DVALUE, p->flMaxDistance)
B3D_GET(min_distance, D3DVALUE, p->flMinDistance)
B3D_GET(mode, DWORD, p->dwMode)
B3D_GET(position, D3DVECTOR, p->vPosition)
B3D_GET(velocity, D3DVECTOR, p->vVelocity)

static HRESULT STDMETHODCALLTYPE b3d_get_all(IDirectSound3DBuffer *This, LPDS3DBUFFER out)
{
    DSBUFFER *b = enter_3d(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (!out || out->dwSize != sizeof(DS3DBUFFER))
        return leave(DSERR_INVALIDPARAM);
    *out = b->sound3d.params;
    out->dwSize = sizeof(DS3DBUFFER);
    return leave(DS_OK);
}

static HRESULT STDMETHODCALLTYPE b3d_get_cone_angles(IDirectSound3DBuffer *This, LPDWORD inside, LPDWORD outside)
{
    DSBUFFER *b = enter_3d(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (!inside && !outside)
        return leave(DSERR_INVALIDPARAM);
    if (inside)
        *inside = b->sound3d.params.dwInsideConeAngle;
    if (outside)
        *outside = b->sound3d.params.dwOutsideConeAngle;
    return leave(DS_OK);
}

/* an immediate change applies now; a deferred one waits for CommitDeferredSettings. */
static DS3DBUFFER *target(DSBUFFER *b, DWORD apply, DWORD field)
{
    if (apply & DS3D_DEFERRED) {
        b->sound3d.dirty |= field;
        return &b->sound3d.deferred;
    }
    return &b->sound3d.params;
}

static HRESULT changed(DSBUFFER *b, DWORD apply)
{
    if (!(apply & DS3D_DEFERRED))
        Ds3dApply(b);
    return leave(DS_OK);
}

static BOOL valid_angles(DWORD inside, DWORD outside)
{
    return inside <= outside && inside <= 360 && outside <= 360;
}

static HRESULT STDMETHODCALLTYPE b3d_set_all(IDirectSound3DBuffer *This, LPCDS3DBUFFER in, DWORD apply)
{
    DSBUFFER *b = enter_3d(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (!in || in->dwSize != sizeof(DS3DBUFFER) || !valid_angles(in->dwInsideConeAngle, in->dwOutsideConeAngle))
        return leave(DSERR_INVALIDPARAM);
    D3DVECTOR orientation = in->vConeOrientation;
    if (is_nan(orientation.x) || is_nan(orientation.y) || is_nan(orientation.z))
        return leave(DSERR_INVALIDPARAM);
    orientation = check_vector(orientation);
    if (!normalize(&orientation) || in->lConeOutsideVolume < DSBVOLUME_MIN || in->lConeOutsideVolume > DSBVOLUME_MAX ||
        !(in->flMaxDistance > 0.0f) || !(in->flMinDistance > 0.0f) || in->dwMode > DS3DMODE_DISABLE ||
        (apply & ~DS3D_DEFERRED))
        return leave(DSERR_INVALIDPARAM);
    DS3DBUFFER *p = target(b, apply, ~0u);
    *p = *in;
    p->vPosition = check_vector(in->vPosition);
    p->vVelocity = check_vector(in->vVelocity);
    p->vConeOrientation = orientation;
    return changed(b, apply);
}

static HRESULT STDMETHODCALLTYPE b3d_set_cone_angles(IDirectSound3DBuffer *This, DWORD inside, DWORD outside,
                                                     DWORD apply)
{
    DSBUFFER *b = enter_3d(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (!valid_angles(inside, outside) || (apply & ~DS3D_DEFERRED))
        return leave(DSERR_INVALIDPARAM);
    DS3DBUFFER *p = target(b, apply, SET_CONE_ANGLES);
    p->dwInsideConeAngle = inside;
    p->dwOutsideConeAngle = outside;
    return changed(b, apply);
}

static HRESULT STDMETHODCALLTYPE b3d_set_cone_orientation(IDirectSound3DBuffer *This, D3DVALUE x, D3DVALUE y,
                                                          D3DVALUE z, DWORD apply)
{
    DSBUFFER *b = enter_3d(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if ((apply & ~DS3D_DEFERRED) || is_nan(x) || is_nan(y) || is_nan(z))
        return leave(DSERR_INVALIDPARAM);
    D3DVECTOR v = check_vector((D3DVECTOR){x, y, z});
    if (!normalize(&v))
        return leave(DSERR_INVALIDPARAM);
    target(b, apply, SET_CONE_ORIENTATION)->vConeOrientation = v;
    return changed(b, apply);
}

static HRESULT STDMETHODCALLTYPE b3d_set_cone_outside_volume(IDirectSound3DBuffer *This, LONG volume, DWORD apply)
{
    DSBUFFER *b = enter_3d(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (volume < DSBVOLUME_MIN || volume > DSBVOLUME_MAX || (apply & ~DS3D_DEFERRED))
        return leave(DSERR_INVALIDPARAM);
    target(b, apply, SET_CONE_VOLUME)->lConeOutsideVolume = volume;
    return changed(b, apply);
}

static HRESULT STDMETHODCALLTYPE b3d_set_max_distance(IDirectSound3DBuffer *This, D3DVALUE distance, DWORD apply)
{
    DSBUFFER *b = enter_3d(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (!(distance > 0.0f) || (apply & ~DS3D_DEFERRED))
        return leave(DSERR_INVALIDPARAM);
    target(b, apply, SET_MAX_DISTANCE)->flMaxDistance = distance;
    return changed(b, apply);
}

static HRESULT STDMETHODCALLTYPE b3d_set_min_distance(IDirectSound3DBuffer *This, D3DVALUE distance, DWORD apply)
{
    DSBUFFER *b = enter_3d(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (!(distance > 0.0f) || (apply & ~DS3D_DEFERRED))
        return leave(DSERR_INVALIDPARAM);
    target(b, apply, SET_MIN_DISTANCE)->flMinDistance = distance;
    return changed(b, apply);
}

static HRESULT STDMETHODCALLTYPE b3d_set_mode(IDirectSound3DBuffer *This, DWORD mode, DWORD apply)
{
    DSBUFFER *b = enter_3d(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (mode > DS3DMODE_DISABLE || (apply & ~DS3D_DEFERRED))
        return leave(DSERR_INVALIDPARAM);
    target(b, apply, SET_MODE)->dwMode = mode;
    return changed(b, apply);
}

static HRESULT STDMETHODCALLTYPE b3d_set_position(IDirectSound3DBuffer *This, D3DVALUE x, D3DVALUE y, D3DVALUE z,
                                                  DWORD apply)
{
    DSBUFFER *b = enter_3d(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (apply & ~DS3D_DEFERRED)
        return leave(DSERR_INVALIDPARAM);
    target(b, apply, SET_POSITION)->vPosition = check_vector((D3DVECTOR){x, y, z});
    return changed(b, apply);
}

static HRESULT STDMETHODCALLTYPE b3d_set_velocity(IDirectSound3DBuffer *This, D3DVALUE x, D3DVALUE y, D3DVALUE z,
                                                  DWORD apply)
{
    DSBUFFER *b = enter_3d(This);
    if (!b)
        return DSERR_INVALIDPARAM;
    if (apply & ~DS3D_DEFERRED)
        return leave(DSERR_INVALIDPARAM);
    target(b, apply, SET_VELOCITY)->vVelocity = check_vector((D3DVECTOR){x, y, z});
    return changed(b, apply);
}

const IDirectSound3DBufferVtbl DsBuffer3dVtbl = {
    b3d_query,
    b3d_add_ref,
    b3d_release,
    b3d_get_all,
    b3d_get_cone_angles,
    b3d_get_cone_orientation,
    b3d_get_cone_outside_volume,
    b3d_get_max_distance,
    b3d_get_min_distance,
    b3d_get_mode,
    b3d_get_position,
    b3d_get_velocity,
    b3d_set_all,
    b3d_set_cone_angles,
    b3d_set_cone_orientation,
    b3d_set_cone_outside_volume,
    b3d_set_max_distance,
    b3d_set_min_distance,
    b3d_set_mode,
    b3d_set_position,
    b3d_set_velocity,
};

/* ------------------------------------------------------------------ IDirectSound3DListener */

static DSBUFFER *from_listener(IDirectSound3DListener *iface)
{
    if (!iface)
        return nullptr;
    DSBUFFER *b = CONTAINING_RECORD(iface, DSBUFFER, listener);
    return b->magic == BUFFER_MAGIC ? b : nullptr;
}

static LISTENER3D *enter_listener(IDirectSound3DListener *iface)
{
    DSBUFFER *b = from_listener(iface);
    if (!b)
        return nullptr;
    RtlEnterCriticalSection(&DsLock);
    if (!b->device) {
        RtlLeaveCriticalSection(&DsLock);
        return nullptr;
    }
    return &b->device->listener;
}

static DSDEVICE *listener_device(LISTENER3D *l)
{
    return CONTAINING_RECORD(l, DSDEVICE, listener);
}

static HRESULT STDMETHODCALLTYPE l3d_query(IDirectSound3DListener *This, REFIID riid, void **out)
{
    DSBUFFER *b = from_listener(This);
    return b ? DsBufferQuery(b, riid, out) : DSERR_INVALIDPARAM;
}

static ULONG STDMETHODCALLTYPE l3d_add_ref(IDirectSound3DListener *This)
{
    DSBUFFER *b = from_listener(This);
    return b ? InterlockedIncrement(&b->ref) : 0;
}

static ULONG STDMETHODCALLTYPE l3d_release(IDirectSound3DListener *This)
{
    DSBUFFER *b = from_listener(This);
    return b ? DsBufferRelease(b) : 0;
}

#define L3D_GET(name, type, expression)                                                                               \
    static HRESULT STDMETHODCALLTYPE l3d_get_##name(IDirectSound3DListener *This, type *out)                          \
    {                                                                                                                 \
        LISTENER3D *l = enter_listener(This);                                                                         \
        if (!l)                                                                                                       \
            return DSERR_INVALIDPARAM;                                                                                \
        if (!out)                                                                                                     \
            return leave(DSERR_INVALIDPARAM);                                                                         \
        const DS3DLISTENER *p = &l->params;                                                                           \
        *out = expression;                                                                                            \
        return leave(DS_OK);                                                                                          \
    }

L3D_GET(distance_factor, D3DVALUE, p->flDistanceFactor)
L3D_GET(doppler_factor, D3DVALUE, p->flDopplerFactor)
L3D_GET(rolloff_factor, D3DVALUE, p->flRolloffFactor)
L3D_GET(position, D3DVECTOR, p->vPosition)
L3D_GET(velocity, D3DVECTOR, p->vVelocity)

static HRESULT STDMETHODCALLTYPE l3d_get_all(IDirectSound3DListener *This, LPDS3DLISTENER out)
{
    LISTENER3D *l = enter_listener(This);
    if (!l)
        return DSERR_INVALIDPARAM;
    if (!out || out->dwSize != sizeof(DS3DLISTENER))
        return leave(DSERR_INVALIDPARAM);
    *out = l->params;
    out->dwSize = sizeof(DS3DLISTENER);
    return leave(DS_OK);
}

static HRESULT STDMETHODCALLTYPE l3d_get_orientation(IDirectSound3DListener *This, D3DVECTOR *front, D3DVECTOR *top)
{
    LISTENER3D *l = enter_listener(This);
    if (!l)
        return DSERR_INVALIDPARAM;
    if (!front || !top)
        return leave(DSERR_INVALIDPARAM);
    *front = l->params.vOrientFront;
    *top = l->params.vOrientTop;
    return leave(DS_OK);
}

static DS3DLISTENER *listener_target(LISTENER3D *l, DWORD apply, DWORD field)
{
    if (apply & DS3D_DEFERRED) {
        l->dirty |= field;
        return &l->deferred;
    }
    return &l->params;
}

static HRESULT listener_changed(LISTENER3D *l, DWORD apply)
{
    if (!(apply & DS3D_DEFERRED))
        Ds3dApplyAll(listener_device(l));
    return leave(DS_OK);
}

/* the front and the top: both of some length and not along each other, kept as given. */
static BOOL valid_orientation(D3DVECTOR *front, D3DVECTOR *top)
{
    if (is_nan(front->x) || is_nan(front->y) || is_nan(front->z) || is_nan(top->x) || is_nan(top->y) ||
        is_nan(top->z))
        return FALSE;
    *front = check_vector(*front);
    *top = check_vector(*top);
    if (magnitude(*front) == 0 || magnitude(*top) == 0)
        return FALSE;
    D3DVECTOR f = *front, t = *top;
    normalize(&f);
    normalize(&t);
    return !empty(cross(f, t));
}

static HRESULT STDMETHODCALLTYPE l3d_set_all(IDirectSound3DListener *This, LPCDS3DLISTENER in, DWORD apply)
{
    LISTENER3D *l = enter_listener(This);
    if (!l)
        return DSERR_INVALIDPARAM;
    if (!in || in->dwSize != sizeof(DS3DLISTENER))
        return leave(DSERR_INVALIDPARAM);
    if (in->flDistanceFactor < __FLT_MIN__ || in->flDistanceFactor > __FLT_MAX__ || in->flDopplerFactor < 0.0f ||
        in->flDopplerFactor > DS3D_MAXDOPPLERFACTOR || in->flRolloffFactor < 0.0f ||
        in->flRolloffFactor > DS3D_MAXROLLOFFFACTOR || (apply & ~DS3D_DEFERRED))
        return leave(DSERR_INVALIDPARAM);
    D3DVECTOR front = in->vOrientFront, top = in->vOrientTop;
    if (!valid_orientation(&front, &top))
        return leave(DSERR_INVALIDPARAM);
    DS3DLISTENER *p = listener_target(l, apply, ~0u);
    *p = *in;
    p->vPosition = check_vector(in->vPosition);
    p->vVelocity = check_vector(in->vVelocity);
    p->vOrientFront = front;
    p->vOrientTop = top;
    return listener_changed(l, apply);
}

static HRESULT STDMETHODCALLTYPE l3d_set_distance_factor(IDirectSound3DListener *This, D3DVALUE factor, DWORD apply)
{
    LISTENER3D *l = enter_listener(This);
    if (!l)
        return DSERR_INVALIDPARAM;
    if (!(factor >= __FLT_MIN__ && factor <= __FLT_MAX__) || (apply & ~DS3D_DEFERRED))
        return leave(DSERR_INVALIDPARAM);
    listener_target(l, apply, SET_DISTANCE_FACTOR)->flDistanceFactor = factor;
    return listener_changed(l, apply);
}

static HRESULT STDMETHODCALLTYPE l3d_set_doppler_factor(IDirectSound3DListener *This, D3DVALUE factor, DWORD apply)
{
    LISTENER3D *l = enter_listener(This);
    if (!l)
        return DSERR_INVALIDPARAM;
    if (!(factor >= 0.0f && factor <= DS3D_MAXDOPPLERFACTOR) || (apply & ~DS3D_DEFERRED))
        return leave(DSERR_INVALIDPARAM);
    listener_target(l, apply, SET_DOPPLER_FACTOR)->flDopplerFactor = factor;
    return listener_changed(l, apply);
}

static HRESULT STDMETHODCALLTYPE l3d_set_rolloff_factor(IDirectSound3DListener *This, D3DVALUE factor, DWORD apply)
{
    LISTENER3D *l = enter_listener(This);
    if (!l)
        return DSERR_INVALIDPARAM;
    if (!(factor >= 0.0f && factor <= DS3D_MAXROLLOFFFACTOR) || (apply & ~DS3D_DEFERRED))
        return leave(DSERR_INVALIDPARAM);
    listener_target(l, apply, SET_ROLLOFF_FACTOR)->flRolloffFactor = factor;
    return listener_changed(l, apply);
}

static HRESULT STDMETHODCALLTYPE l3d_set_orientation(IDirectSound3DListener *This, D3DVALUE xf, D3DVALUE yf,
                                                     D3DVALUE zf, D3DVALUE xt, D3DVALUE yt, D3DVALUE zt, DWORD apply)
{
    LISTENER3D *l = enter_listener(This);
    if (!l)
        return DSERR_INVALIDPARAM;
    D3DVECTOR front = {xf, yf, zf}, top = {xt, yt, zt};
    if ((apply & ~DS3D_DEFERRED) || !valid_orientation(&front, &top))
        return leave(DSERR_INVALIDPARAM);
    DS3DLISTENER *p = listener_target(l, apply, SET_ORIENTATION);
    p->vOrientFront = front;
    p->vOrientTop = top;
    return listener_changed(l, apply);
}

static HRESULT STDMETHODCALLTYPE l3d_set_position(IDirectSound3DListener *This, D3DVALUE x, D3DVALUE y, D3DVALUE z,
                                                  DWORD apply)
{
    LISTENER3D *l = enter_listener(This);
    if (!l)
        return DSERR_INVALIDPARAM;
    if (apply & ~DS3D_DEFERRED)
        return leave(DSERR_INVALIDPARAM);
    listener_target(l, apply, SET_POSITION)->vPosition = check_vector((D3DVECTOR){x, y, z});
    return listener_changed(l, apply);
}

static HRESULT STDMETHODCALLTYPE l3d_set_velocity(IDirectSound3DListener *This, D3DVALUE x, D3DVALUE y, D3DVALUE z,
                                                  DWORD apply)
{
    LISTENER3D *l = enter_listener(This);
    if (!l)
        return DSERR_INVALIDPARAM;
    if (apply & ~DS3D_DEFERRED)
        return leave(DSERR_INVALIDPARAM);
    listener_target(l, apply, SET_VELOCITY)->vVelocity = check_vector((D3DVECTOR){x, y, z});
    return listener_changed(l, apply);
}

static void commit_buffer(SOUND3D *s)
{
    DS3DBUFFER *p = &s->params, *q = &s->deferred;
    DWORD dirty = s->dirty;
    if (dirty == ~0u) {
        *p = *q;
    } else {
        if (dirty & SET_POSITION)
            p->vPosition = q->vPosition;
        if (dirty & SET_VELOCITY)
            p->vVelocity = q->vVelocity;
        if (dirty & SET_CONE_ANGLES) {
            p->dwInsideConeAngle = q->dwInsideConeAngle;
            p->dwOutsideConeAngle = q->dwOutsideConeAngle;
        }
        if (dirty & SET_CONE_ORIENTATION)
            p->vConeOrientation = q->vConeOrientation;
        if (dirty & SET_CONE_VOLUME)
            p->lConeOutsideVolume = q->lConeOutsideVolume;
        if (dirty & SET_MIN_DISTANCE)
            p->flMinDistance = q->flMinDistance;
        if (dirty & SET_MAX_DISTANCE)
            p->flMaxDistance = q->flMaxDistance;
        if (dirty & SET_MODE)
            p->dwMode = q->dwMode;
    }
    s->dirty = 0;
}

static void commit_listener(LISTENER3D *l)
{
    DS3DLISTENER *p = &l->params, *q = &l->deferred;
    DWORD dirty = l->dirty;
    if (dirty == ~0u) {
        *p = *q;
    } else {
        if (dirty & SET_POSITION)
            p->vPosition = q->vPosition;
        if (dirty & SET_VELOCITY)
            p->vVelocity = q->vVelocity;
        if (dirty & SET_ORIENTATION) {
            p->vOrientFront = q->vOrientFront;
            p->vOrientTop = q->vOrientTop;
        }
        if (dirty & SET_DISTANCE_FACTOR)
            p->flDistanceFactor = q->flDistanceFactor;
        if (dirty & SET_ROLLOFF_FACTOR)
            p->flRolloffFactor = q->flRolloffFactor;
        if (dirty & SET_DOPPLER_FACTOR)
            p->flDopplerFactor = q->flDopplerFactor;
    }
    l->dirty = 0;
}

/* every deferred change of the listener and of the device's buffers at once. */
static HRESULT STDMETHODCALLTYPE l3d_commit(IDirectSound3DListener *This)
{
    LISTENER3D *l = enter_listener(This);
    if (!l)
        return DSERR_INVALIDPARAM;
    DSDEVICE *d = listener_device(l);
    commit_listener(l);
    for (DSBUFFER *b = d->buffers; b; b = b->next)
        if (b->has3d)
            commit_buffer(&b->sound3d);
    Ds3dApplyAll(d);
    return leave(DS_OK);
}

const IDirectSound3DListenerVtbl DsListenerVtbl = {
    l3d_query,
    l3d_add_ref,
    l3d_release,
    l3d_get_all,
    l3d_get_distance_factor,
    l3d_get_doppler_factor,
    l3d_get_orientation,
    l3d_get_position,
    l3d_get_rolloff_factor,
    l3d_get_velocity,
    l3d_set_all,
    l3d_set_distance_factor,
    l3d_set_doppler_factor,
    l3d_set_orientation,
    l3d_set_position,
    l3d_set_rolloff_factor,
    l3d_set_velocity,
    l3d_commit,
};
