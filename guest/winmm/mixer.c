/*
 * the mixer of the output device: one destination, the speakers, with the
 * endpoint's master volume and mute, which winmm applies to every wave
 * output of the process. there are no source lines.
 */
#define MIME_BUILD_WINMM
#include "winmm_private.h"

#define DEVICE_NAME L"Speakers (Mime Audio)"
#define LINE_ID 0xFFFF0000u
#define VOLUME_ID 1
#define MUTE_ID 2

typedef struct MIXER {
    struct MIXER *next;
    DWORD callback;
    DWORD_PTR target, instance;
} MIXER;

static RTL_CRITICAL_SECTION g_lock = {nullptr, -1, 0, nullptr, nullptr, 0};
static MIXER *g_mixers;
static DWORD g_volume[2] = {65535, 65535};
static BOOL g_mute;

static BOOL valid(HMIXER h)
{
    RtlEnterCriticalSection(&g_lock);
    MIXER *m = g_mixers;
    while (m && m != (MIXER *)h)
        m = m->next;
    RtlLeaveCriticalSection(&g_lock);
    return m != nullptr;
}

/* the mixer an object names: its number, or its handle by type. */
static MMRESULT mixer_of(HMIXEROBJ object, DWORD flags)
{
    DWORD type = flags & 0xF0000000;
    UINT_PTR id = (UINT_PTR)object;
    switch (type) {
    case MIXER_OBJECTF_MIXER:
        return id == 0 ? MMSYSERR_NOERROR : MMSYSERR_BADDEVICEID;
    case MIXER_OBJECTF_HMIXER:
        return valid((HMIXER)object) ? MMSYSERR_NOERROR : MMSYSERR_INVALHANDLE;
    case MIXER_OBJECTF_WAVEOUT:
        return id == 0 || id == (UINT_PTR)(UINT)WAVE_MAPPER ? MMSYSERR_NOERROR : MMSYSERR_BADDEVICEID;
    case MIXER_OBJECTF_HWAVEOUT: {
        UINT wave;
        return waveOutGetID((HWAVEOUT)object, &wave) == MMSYSERR_NOERROR ? MMSYSERR_NOERROR : MMSYSERR_INVALHANDLE;
    }
    case MIXER_OBJECTF_WAVEIN:
    case MIXER_OBJECTF_MIDIOUT:
    case MIXER_OBJECTF_MIDIIN:
    case MIXER_OBJECTF_AUX:
        return MMSYSERR_BADDEVICEID;
    case MIXER_OBJECTF_HWAVEIN:
    case MIXER_OBJECTF_HMIDIOUT:
    case MIXER_OBJECTF_HMIDIIN:
        return MMSYSERR_INVALHANDLE;
    default:
        return MMSYSERR_INVALFLAG;
    }
}

WINMMAPI UINT WINAPI mixerGetNumDevs(void)
{
    return 1;
}

WINMMAPI MMRESULT WINAPI mixerGetDevCapsW(UINT_PTR id, LPMIXERCAPSW caps, UINT size)
{
    if (id != 0 && !valid((HMIXER)id))
        return MMSYSERR_BADDEVICEID;
    if (!caps)
        return MMSYSERR_INVALPARAM;
    MIXERCAPSW c = {MM_MICROSOFT, MM_MSFT_WDMAUDIO_MIXER, 0x0A00};
    lstrcpynW(c.szPname, DEVICE_NAME, MAXPNAMELEN);
    c.cDestinations = 1;
    memcpy(caps, &c, size < sizeof(c) ? size : sizeof(c));
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI mixerGetDevCapsA(UINT_PTR id, LPMIXERCAPSA caps, UINT size)
{
    MIXERCAPSW w;
    MMRESULT r = mixerGetDevCapsW(id, &w, sizeof(w));
    if (r)
        return r;
    if (!caps)
        return MMSYSERR_INVALPARAM;
    MIXERCAPSA c = {w.wMid, w.wPid, w.vDriverVersion};
    WideCharToMultiByte(CP_ACP, 0, w.szPname, -1, c.szPname, MAXPNAMELEN, nullptr, nullptr);
    c.fdwSupport = w.fdwSupport;
    c.cDestinations = w.cDestinations;
    memcpy(caps, &c, size < sizeof(c) ? size : sizeof(c));
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI mixerOpen(LPHMIXER out, UINT id, DWORD_PTR callback, DWORD_PTR instance, DWORD flags)
{
    if (!out)
        return MMSYSERR_INVALPARAM;
    *out = nullptr;
    MMRESULT r = mixer_of((HMIXEROBJ)(UINT_PTR)id, flags & ~CALLBACK_TYPEMASK);
    if (r)
        return r;
    DWORD type = flags & CALLBACK_TYPEMASK;
    if (type != CALLBACK_NULL && type != CALLBACK_WINDOW && type != CALLBACK_FUNCTION)
        return MMSYSERR_INVALFLAG;
    if (type == CALLBACK_WINDOW && callback && !IsWindow((HWND)callback))
        return MMSYSERR_INVALPARAM;
    MIXER *m = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*m));
    if (!m)
        return MMSYSERR_NOMEM;
    m->callback = type >> 16;
    m->target = callback;
    m->instance = instance;
    RtlEnterCriticalSection(&g_lock);
    m->next = g_mixers;
    g_mixers = m;
    RtlLeaveCriticalSection(&g_lock);
    *out = (HMIXER)m;
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI mixerClose(HMIXER h)
{
    RtlEnterCriticalSection(&g_lock);
    for (MIXER **p = &g_mixers; *p; p = &(*p)->next) {
        if (*p == (MIXER *)h) {
            MIXER *m = *p;
            *p = m->next;
            RtlLeaveCriticalSection(&g_lock);
            HeapFree(GetProcessHeap(), 0, m);
            return MMSYSERR_NOERROR;
        }
    }
    RtlLeaveCriticalSection(&g_lock);
    return MMSYSERR_INVALHANDLE;
}

WINMMAPI MMRESULT WINAPI mixerGetID(HMIXEROBJ object, UINT *id, DWORD flags)
{
    if (!id)
        return MMSYSERR_INVALPARAM;
    MMRESULT r = mixer_of(object, flags);
    if (!r)
        *id = 0;
    return r;
}

static void fill_line(MIXERLINEW *l)
{
    DWORD size = l->cbStruct;
    memset(l, 0, sizeof(*l));
    l->cbStruct = size;
    l->dwLineID = LINE_ID;
    l->fdwLine = MIXERLINE_LINEF_ACTIVE;
    l->dwComponentType = MIXERLINE_COMPONENTTYPE_DST_SPEAKERS;
    l->cChannels = 2;
    l->cControls = 2;
    lstrcpynW(l->szShortName, L"Speakers", MIXER_SHORT_NAME_CHARS);
    lstrcpynW(l->szName, L"Speakers", MIXER_LONG_NAME_CHARS);
    l->Target.dwType = MIXERLINE_TARGETTYPE_WAVEOUT;
    l->Target.wMid = MM_MICROSOFT;
    l->Target.wPid = MM_MSFT_WDMAUDIO_WAVEOUT;
    l->Target.vDriverVersion = 0x0A00;
    lstrcpynW(l->Target.szPname, DEVICE_NAME, MAXPNAMELEN);
}

WINMMAPI MMRESULT WINAPI mixerGetLineInfoW(HMIXEROBJ object, LPMIXERLINEW line, DWORD flags)
{
    if (!line || line->cbStruct < sizeof(MIXERLINEW))
        return MMSYSERR_INVALPARAM;
    MMRESULT r = mixer_of(object, flags);
    if (r)
        return r;
    switch (flags & MIXER_GETLINEINFOF_QUERYMASK) {
    case MIXER_GETLINEINFOF_DESTINATION:
        if (line->dwDestination != 0)
            return MIXERR_INVALLINE;
        break;
    case MIXER_GETLINEINFOF_LINEID:
        if (line->dwLineID != LINE_ID)
            return MIXERR_INVALLINE;
        break;
    case MIXER_GETLINEINFOF_COMPONENTTYPE:
        if (line->dwComponentType != MIXERLINE_COMPONENTTYPE_DST_SPEAKERS)
            return MIXERR_INVALLINE;
        break;
    case MIXER_GETLINEINFOF_TARGETTYPE:
        if (line->Target.dwType != MIXERLINE_TARGETTYPE_WAVEOUT)
            return MIXERR_INVALLINE;
        break;
    case MIXER_GETLINEINFOF_SOURCE:
        return MIXERR_INVALLINE;
    default:
        return MMSYSERR_INVALFLAG;
    }
    fill_line(line);
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI mixerGetLineInfoA(HMIXEROBJ object, LPMIXERLINEA line, DWORD flags)
{
    if (!line || line->cbStruct < sizeof(MIXERLINEA))
        return MMSYSERR_INVALPARAM;
    MIXERLINEW w = {sizeof(w), line->dwDestination, line->dwSource, line->dwLineID};
    w.dwComponentType = line->dwComponentType;
    w.Target.dwType = line->Target.dwType;
    MMRESULT r = mixerGetLineInfoW(object, &w, flags);
    if (r)
        return r;
    DWORD size = line->cbStruct;
    memset(line, 0, sizeof(*line));
    line->cbStruct = size;
    line->dwDestination = w.dwDestination;
    line->dwSource = w.dwSource;
    line->dwLineID = w.dwLineID;
    line->fdwLine = w.fdwLine;
    line->dwComponentType = w.dwComponentType;
    line->cChannels = w.cChannels;
    line->cConnections = w.cConnections;
    line->cControls = w.cControls;
    WideCharToMultiByte(CP_ACP, 0, w.szShortName, -1, line->szShortName, MIXER_SHORT_NAME_CHARS, nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, w.szName, -1, line->szName, MIXER_LONG_NAME_CHARS, nullptr, nullptr);
    line->Target.dwType = w.Target.dwType;
    line->Target.wMid = w.Target.wMid;
    line->Target.wPid = w.Target.wPid;
    line->Target.vDriverVersion = w.Target.vDriverVersion;
    WideCharToMultiByte(CP_ACP, 0, w.Target.szPname, -1, line->Target.szPname, MAXPNAMELEN, nullptr, nullptr);
    return MMSYSERR_NOERROR;
}

/* the volume, a fader, and the mute switch, with the names windows' wdmaud gives them. */
static void fill_control(DWORD id, MIXERCONTROLW *c)
{
    memset(c, 0, sizeof(*c));
    c->cbStruct = sizeof(*c);
    c->dwControlID = id;
    if (id == VOLUME_ID) {
        c->dwControlType = MIXERCONTROL_CONTROLTYPE_VOLUME;
        lstrcpynW(c->szShortName, L"Volume", MIXER_SHORT_NAME_CHARS);
        lstrcpynW(c->szName, L"Master Volume", MIXER_LONG_NAME_CHARS);
        c->Bounds.dwMinimum = 0;
        c->Bounds.dwMaximum = 0xFFFF;
        c->Metrics.cSteps = 0xFFFF;
    } else {
        c->dwControlType = MIXERCONTROL_CONTROLTYPE_MUTE;
        c->fdwControl = MIXERCONTROL_CONTROLF_UNIFORM;
        lstrcpynW(c->szShortName, L"Mute", MIXER_SHORT_NAME_CHARS);
        lstrcpynW(c->szName, L"Mute", MIXER_LONG_NAME_CHARS);
        c->Bounds.dwMaximum = 1;
    }
}

static void control_a(const MIXERCONTROLW *w, MIXERCONTROLA *a)
{
    memset(a, 0, sizeof(*a));
    a->cbStruct = sizeof(*a);
    a->dwControlID = w->dwControlID;
    a->dwControlType = w->dwControlType;
    a->fdwControl = w->fdwControl;
    a->cMultipleItems = w->cMultipleItems;
    WideCharToMultiByte(CP_ACP, 0, w->szShortName, -1, a->szShortName, MIXER_SHORT_NAME_CHARS, nullptr, nullptr);
    WideCharToMultiByte(CP_ACP, 0, w->szName, -1, a->szName, MIXER_LONG_NAME_CHARS, nullptr, nullptr);
    memcpy(&a->Bounds, &w->Bounds, sizeof(a->Bounds));
    memcpy(&a->Metrics, &w->Metrics, sizeof(a->Metrics));
}

/* which controls a query names; count is how many fit. */
static MMRESULT select_controls(DWORD line_id, DWORD selector, DWORD flags, DWORD count, DWORD *ids, DWORD *n)
{
    switch (flags & MIXER_GETLINECONTROLSF_QUERYMASK) {
    case MIXER_GETLINECONTROLSF_ALL:
        if (line_id != LINE_ID)
            return MIXERR_INVALLINE;
        if (count < 2)
            return MMSYSERR_INVALPARAM;
        ids[0] = VOLUME_ID;
        ids[1] = MUTE_ID;
        *n = 2;
        return MMSYSERR_NOERROR;
    case MIXER_GETLINECONTROLSF_ONEBYID:
        if (selector != VOLUME_ID && selector != MUTE_ID)
            return MIXERR_INVALCONTROL;
        ids[0] = selector;
        *n = 1;
        return MMSYSERR_NOERROR;
    case MIXER_GETLINECONTROLSF_ONEBYTYPE:
        if (line_id != LINE_ID)
            return MIXERR_INVALLINE;
        if (selector == MIXERCONTROL_CONTROLTYPE_VOLUME)
            ids[0] = VOLUME_ID;
        else if (selector == MIXERCONTROL_CONTROLTYPE_MUTE)
            ids[0] = MUTE_ID;
        else
            return MIXERR_INVALCONTROL;
        *n = 1;
        return MMSYSERR_NOERROR;
    default:
        return MMSYSERR_INVALFLAG;
    }
}

WINMMAPI MMRESULT WINAPI mixerGetLineControlsW(HMIXEROBJ object, LPMIXERLINECONTROLSW lc, DWORD flags)
{
    if (!lc || lc->cbStruct < sizeof(*lc) || !lc->pamxctrl || lc->cbmxctrl < sizeof(MIXERCONTROLW))
        return MMSYSERR_INVALPARAM;
    MMRESULT r = mixer_of(object, flags);
    if (r)
        return r;
    DWORD ids[2], n;
    r = select_controls(lc->dwLineID, lc->dwControlID, flags, lc->cControls, ids, &n);
    if (r)
        return r;
    for (DWORD i = 0; i < n; i++)
        fill_control(ids[i], (MIXERCONTROLW *)((BYTE *)lc->pamxctrl + i * lc->cbmxctrl));
    if ((flags & MIXER_GETLINECONTROLSF_QUERYMASK) != MIXER_GETLINECONTROLSF_ALL)
        lc->dwLineID = LINE_ID;
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI mixerGetLineControlsA(HMIXEROBJ object, LPMIXERLINECONTROLSA lc, DWORD flags)
{
    if (!lc || lc->cbStruct < sizeof(*lc) || !lc->pamxctrl || lc->cbmxctrl < sizeof(MIXERCONTROLA))
        return MMSYSERR_INVALPARAM;
    MMRESULT r = mixer_of(object, flags);
    if (r)
        return r;
    DWORD ids[2], n;
    r = select_controls(lc->dwLineID, lc->dwControlID, flags, lc->cControls, ids, &n);
    if (r)
        return r;
    for (DWORD i = 0; i < n; i++) {
        MIXERCONTROLW w;
        fill_control(ids[i], &w);
        control_a(&w, (MIXERCONTROLA *)((BYTE *)lc->pamxctrl + i * lc->cbmxctrl));
    }
    if ((flags & MIXER_GETLINECONTROLSF_QUERYMASK) != MIXER_GETLINECONTROLSF_ALL)
        lc->dwLineID = LINE_ID;
    return MMSYSERR_NOERROR;
}

static MMRESULT details(HMIXEROBJ object, LPMIXERCONTROLDETAILS d, DWORD flags, BOOL set)
{
    if (!d || d->cbStruct < sizeof(*d) || !d->paDetails)
        return MMSYSERR_INVALPARAM;
    MMRESULT r = mixer_of(object, flags);
    if (r)
        return r;
    DWORD query = flags & MIXER_GETCONTROLDETAILSF_QUERYMASK;
    if (query != MIXER_GETCONTROLDETAILSF_VALUE)
        return query == MIXER_GETCONTROLDETAILSF_LISTTEXT ? MMSYSERR_NOTSUPPORTED : MMSYSERR_INVALFLAG;
    if (d->dwControlID != VOLUME_ID && d->dwControlID != MUTE_ID)
        return MIXERR_INVALCONTROL;
    DWORD channels = d->dwControlID == MUTE_ID ? 1 : d->cChannels == 1 ? 1 : 2;
    if (d->cChannels != 1 && d->cChannels != 2)
        return MMSYSERR_INVALPARAM;
    if (d->cbDetails < sizeof(DWORD))
        return MMSYSERR_INVALPARAM;
    BYTE *v = d->paDetails;
    if (!set) {
        for (DWORD i = 0; i < d->cChannels; i++) {
            DWORD value = d->dwControlID == MUTE_ID ? (DWORD)g_mute
                          : channels == 1          ? (g_volume[0] > g_volume[1] ? g_volume[0] : g_volume[1])
                                                   : g_volume[i];
            *(DWORD *)(v + i * d->cbDetails) = value;
        }
        return MMSYSERR_NOERROR;
    }
    if (d->dwControlID == MUTE_ID) {
        g_mute = *(LONG *)v != 0;
    } else {
        DWORD left = *(DWORD *)v, right = channels == 2 ? *(DWORD *)(v + d->cbDetails) : left;
        if (left > 0xFFFF || right > 0xFFFF)
            return MIXERR_INVALVALUE;
        g_volume[0] = left;
        g_volume[1] = right;
    }
    DWORD level = g_mute ? 0 : (g_volume[0] | g_volume[1] << 16);
    waveOutSetVolume(nullptr, level);
    RtlEnterCriticalSection(&g_lock);
    for (MIXER *m = g_mixers; m; m = m->next)
        DriverCallback(m->target, m->callback, (HANDLE)m, MM_MIXM_CONTROL_CHANGE, m->instance, d->dwControlID, 0);
    RtlLeaveCriticalSection(&g_lock);
    return MMSYSERR_NOERROR;
}

WINMMAPI MMRESULT WINAPI mixerGetControlDetailsW(HMIXEROBJ object, LPMIXERCONTROLDETAILS d, DWORD flags)
{
    return details(object, d, flags, FALSE);
}

WINMMAPI MMRESULT WINAPI mixerGetControlDetailsA(HMIXEROBJ object, LPMIXERCONTROLDETAILS d, DWORD flags)
{
    return details(object, d, flags, FALSE);
}

WINMMAPI MMRESULT WINAPI mixerSetControlDetails(HMIXEROBJ object, LPMIXERCONTROLDETAILS d, DWORD flags)
{
    return details(object, d, flags, TRUE);
}

WINMMAPI DWORD WINAPI mixerMessage(HMIXER mixer, UINT message, DWORD_PTR param1, DWORD_PTR param2)
{
    if (mixer && !valid(mixer))
        return MMSYSERR_INVALHANDLE;
    return MMSYSERR_NOTSUPPORTED;
}

WINMMAPI UINT WINAPI auxGetNumDevs(void)
{
    return 0;
}
