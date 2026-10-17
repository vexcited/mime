/*
 * format tags, formats, filter tags and filters: asked of one driver, or
 * of the first driver able to answer. pcm is known without a driver, so a
 * program sees it even when the pcm converter is off.
 */
#include "msacm32_private.h"

static const DWORD pcm_rates[] = {8000, 11025, 22050, 44100};

void AcmPcmTagName(WCHAR *name)
{
    lstrcpyW(name, L"PCM");
}

/* the pcm tag as windows reports it without a driver. */
static void pcm_tag(ACMFORMATTAGDETAILSW *d)
{
    d->dwFormatTagIndex = 0;
    d->dwFormatTag = WAVE_FORMAT_PCM;
    d->cbFormatSize = sizeof(PCMWAVEFORMAT);
    d->fdwSupport = 0;
    d->cStandardFormats = 16;
    AcmPcmTagName(d->szFormatTag);
}

/* "44.100 kHz, 16 Bit, Stereo", with the separators of the user's locale. */
void AcmFormatName(ACMFORMATDETAILSW *d)
{
    WCHAR list[4] = L",", decimal[4] = L".";
    GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SLIST, list, 4);
    GetLocaleInfoW(LOCALE_USER_DEFAULT, LOCALE_SDECIMAL, decimal, 4);
    const WAVEFORMATEX *f = d->pwfx;
    DWORD rate = f->nSamplesPerSec;
    WCHAR *out = d->szFormat;
    int n = wsprintfW(out, L"%lu%c%03u kHz%c ", rate / 1000, decimal[0], rate % 1000, list[0]);
    if (f->wBitsPerSample)
        n += wsprintfW(out + n, L"%u Bit%c ", f->wBitsPerSample, list[0]);
    if (f->nChannels == 1 || f->nChannels == 2)
        lstrcpyW(out + n, f->nChannels == 1 ? L"Mono" : L"Stereo");
    else
        wsprintfW(out + n, L"%u Channels", f->nChannels);
}

/* ------------------------------------------------------------------ sizes */

static DWORD largest(const ACMTAG *tags, DWORD count, DWORD size)
{
    for (DWORD i = 0; i < count; i++)
        if (tags[i].size > size)
            size = tags[i].size;
    return size;
}

static MMRESULT max_size(HACMOBJ handle, DWORD *out, BOOL filters)
{
    DWORD size = 0;
    if (handle) {
        if (!AcmValid(handle, ACM_ANY) || *(DWORD *)handle == ACM_STREAM)
            return MMSYSERR_INVALHANDLE;
        ACMDRIVERID_ *id = AcmIdOf(handle);
        size = filters ? largest(id->filters, id->filter_count, 0) : largest(id->formats, id->format_count, 0);
    } else {
        AcmEnterEnum();
        for (ACMDRIVERID_ *id = AcmNextId(nullptr, 0); id; id = AcmNextId(id, 0))
            size = filters ? largest(id->filters, id->filter_count, size) : largest(id->formats, id->format_count, size);
        AcmLeaveEnum();
    }
    *out = size;
    return size ? MMSYSERR_NOERROR : ACMERR_NOTPOSSIBLE;
}

MMRESULT AcmMaxFormatSize(HACMOBJ handle, DWORD *size)
{
    return max_size(handle, size, FALSE);
}

MMRESULT AcmMaxFilterSize(HACMOBJ handle, DWORD *size)
{
    return max_size(handle, size, TRUE);
}

/* ------------------------------------------------------------------ format tags */

/* the details a driver gives of one of its tags; pcm gets its name from the acm. */
MMRESULT AcmFormatTagDetails(ACMDRIVERID_ *id, ACMFORMATTAGDETAILSW *d, DWORD flags)
{
    DWORD query = flags & ACM_FORMATTAGDETAILSF_QUERYMASK;
    if (query == ACM_FORMATTAGDETAILSF_FORMATTAG ? !AcmHasFormatTag(id, d->dwFormatTag)
                                                  : query > ACM_FORMATTAGDETAILSF_LARGESTSIZE)
        return ACMERR_NOTPOSSIBLE;
    MMRESULT r = (MMRESULT)AcmIdMessage(id, ACMDM_FORMATTAG_DETAILS, (LPARAM)d, flags);
    if (r)
        return r;
    if (d->dwFormatTag == WAVE_FORMAT_UNKNOWN)
        return MMSYSERR_ERROR;
    if (d->dwFormatTag == WAVE_FORMAT_PCM)
        AcmPcmTagName(d->szFormatTag);
    return MMSYSERR_NOERROR;
}

/* the driver the thread sees first with the tag, or with its largest format, the tag 0 meaning any. */
static ACMDRIVERID_ *driver_for_tag(DWORD tag, BOOL by_size, BOOL filters)
{
    ACMDRIVERID_ *found = nullptr;
    DWORD size = 0;
    for (ACMDRIVERID_ *id = AcmNextId(nullptr, 0); id; id = AcmNextId(id, 0)) {
        const ACMTAG *tags = filters ? id->filters : id->formats;
        DWORD count = filters ? id->filter_count : id->format_count;
        for (DWORD i = 0; i < count; i++) {
            if (!by_size && tags[i].tag == tag)
                return id;
            if (by_size && (!tag || tags[i].tag == tag) && tags[i].size > size) {
                size = tags[i].size;
                found = id;
            }
        }
    }
    return found;
}

MSACMAPI MMRESULT WINAPI acmFormatTagDetailsW(HACMDRIVER had, LPACMFORMATTAGDETAILSW d, DWORD flags)
{
    if (!AcmBoot())
        return MMSYSERR_ERROR;
    if (flags & ~ACM_FORMATTAGDETAILSF_QUERYMASK)
        return MMSYSERR_INVALFLAG;
    if (!AcmWritable(d, sizeof(DWORD)) || d->cbStruct < sizeof(*d) || !AcmWritable(d, d->cbStruct) || d->fdwSupport)
        return MMSYSERR_INVALPARAM;
    DWORD query = flags & ACM_FORMATTAGDETAILSF_QUERYMASK;
    if (query == ACM_FORMATTAGDETAILSF_INDEX) {
        if (!AcmValid(had, ACM_ANY))
            return MMSYSERR_INVALHANDLE;
        if (d->dwFormatTag)
            return MMSYSERR_INVALPARAM;
    } else if (query == ACM_FORMATTAGDETAILSF_FORMATTAG) {
        if (!d->dwFormatTag)
            return MMSYSERR_INVALPARAM;
    } else if (query != ACM_FORMATTAGDETAILSF_LARGESTSIZE) {
        return MMSYSERR_NOTSUPPORTED;
    }
    ACMDRIVERID_ *id = nullptr;
    if (had) {
        if (!AcmValid(had, ACM_ANY) || *(DWORD *)had == ACM_STREAM)
            return MMSYSERR_INVALHANDLE;
        id = AcmIdOf((HACMOBJ)had);
        if (*(DWORD *)had == ACM_ID && (id->state & ID_DISABLED))
            return MMSYSERR_NOTENABLED;
        return AcmFormatTagDetails(id, d, flags);
    }
    AcmEnterEnum();
    id = driver_for_tag(d->dwFormatTag, query == ACM_FORMATTAGDETAILSF_LARGESTSIZE, FALSE);
    AcmLeaveEnum();
    if (id)
        return AcmFormatTagDetails(id, d, flags);
    if (query == ACM_FORMATTAGDETAILSF_FORMATTAG && d->dwFormatTag != WAVE_FORMAT_PCM)
        return ACMERR_NOTPOSSIBLE;
    pcm_tag(d);
    return MMSYSERR_NOERROR;
}

static void tag_to_ansi(ACMFORMATTAGDETAILSA *a, const ACMFORMATTAGDETAILSW *w, DWORD size)
{
    a->dwFormatTagIndex = w->dwFormatTagIndex;
    a->dwFormatTag = w->dwFormatTag;
    a->cbFormatSize = w->cbFormatSize;
    a->fdwSupport = w->fdwSupport;
    a->cbStruct = size < sizeof(*a) ? size : sizeof(*a);
    a->cStandardFormats = w->cStandardFormats;
    AcmToAnsi(a->szFormatTag, w->szFormatTag, ACMFORMATTAGDETAILS_FORMATTAG_CHARS);
}

MSACMAPI MMRESULT WINAPI acmFormatTagDetailsA(HACMDRIVER had, LPACMFORMATTAGDETAILSA d, DWORD flags)
{
    if (!AcmWritable(d, sizeof(DWORD)) || d->cbStruct < sizeof(*d) || !AcmWritable(d, d->cbStruct))
        return MMSYSERR_INVALPARAM;
    ACMFORMATTAGDETAILSW w = {sizeof(w), d->dwFormatTagIndex, d->dwFormatTag, d->cbFormatSize, d->fdwSupport,
                              d->cStandardFormats};
    MMRESULT r = acmFormatTagDetailsW(had, &w, flags);
    if (!r)
        tag_to_ansi(d, &w, d->cbStruct);
    return r;
}

/* a tag a driver before this one in the enumeration already gave. */
static BOOL seen_before(ACMDRIVERID_ *id, DWORD tag, BOOL filters)
{
    for (ACMDRIVERID_ *p = AcmNextId(nullptr, 0); p && p != id; p = AcmNextId(p, 0))
        if (filters ? AcmHasFilterTag(p, tag) : AcmHasFormatTag(p, tag))
            return TRUE;
    return FALSE;
}

MSACMAPI MMRESULT WINAPI acmFormatTagEnumW(HACMDRIVER had, LPACMFORMATTAGDETAILSW d, ACMFORMATTAGENUMCBW callback,
                                           DWORD_PTR instance, DWORD flags)
{
    if (!AcmBoot())
        return MMSYSERR_ERROR;
    if (!callback)
        return MMSYSERR_INVALPARAM;
    if (flags)
        return MMSYSERR_INVALFLAG;
    if (!AcmWritable(d, sizeof(DWORD)) || d->cbStruct < sizeof(*d) || !AcmWritable(d, d->cbStruct) || d->fdwSupport)
        return MMSYSERR_INVALPARAM;
    DWORD size = sizeof(*d);
    if (had) {
        if (!AcmValid(had, ACM_DRIVER))
            return MMSYSERR_INVALHANDLE;
        ACMDRIVERID_ *id = ((ACMDRIVER_ *)had)->id;
        MMRESULT r = MMSYSERR_NOERROR;
        for (DWORD i = 0; i < id->format_count; i++) {
            d->cbStruct = size;
            d->dwFormatTagIndex = i;
            if ((r = AcmFormatTagDetails(id, d, ACM_FORMATTAGDETAILSF_INDEX)))
                break;
            if (!callback((HACMDRIVERID)id, d, instance, id->support))
                break;
        }
        return r;
    }
    BOOL none = TRUE;
    MMRESULT r = MMSYSERR_NOERROR;
    AcmEnterEnum();
    for (ACMDRIVERID_ *id = AcmNextId(nullptr, 0); id; id = AcmNextId(id, 0)) {
        for (DWORD i = 0; i < id->format_count; i++) {
            none = FALSE;
            if (seen_before(id, id->formats[i].tag, FALSE))
                continue;
            d->dwFormatTagIndex = i;
            d->cbStruct = size;
            if ((r = AcmFormatTagDetails(id, d, ACM_FORMATTAGDETAILSF_INDEX)) ||
                !callback((HACMDRIVERID)id, d, instance, id->support))
                goto out;
        }
    }
out:
    AcmLeaveEnum();
    if (r)
        return r;
    if (none) {
        pcm_tag(d);
        callback(nullptr, d, instance, 0);
    }
    return MMSYSERR_NOERROR;
}

typedef struct TAG_THUNK {
    ACMFORMATTAGENUMCBA callback;
    DWORD_PTR instance;
    ACMFORMATTAGDETAILSA *details;
} TAG_THUNK;

static BOOL CALLBACK tag_thunk(HACMDRIVERID hadid, LPACMFORMATTAGDETAILSW w, DWORD_PTR instance, DWORD support)
{
    TAG_THUNK *t = (TAG_THUNK *)instance;
    tag_to_ansi(t->details, w, sizeof(*t->details));
    return t->callback(hadid, t->details, t->instance, support);
}

MSACMAPI MMRESULT WINAPI acmFormatTagEnumA(HACMDRIVER had, LPACMFORMATTAGDETAILSA d, ACMFORMATTAGENUMCBA callback,
                                           DWORD_PTR instance, DWORD flags)
{
    if (!callback || !AcmWritable(d, sizeof(DWORD)) || d->cbStruct < sizeof(*d) || !AcmWritable(d, d->cbStruct))
        return MMSYSERR_INVALPARAM;
    ACMFORMATTAGDETAILSW w = {sizeof(w), d->dwFormatTagIndex, d->dwFormatTag, d->cbFormatSize, d->fdwSupport,
                              d->cStandardFormats};
    TAG_THUNK t = {callback, instance, d};
    return acmFormatTagEnumW(had, &w, tag_thunk, (DWORD_PTR)&t, flags);
}

/* ------------------------------------------------------------------ formats */

static MMRESULT format_message(HACMDRIVER had, ACMFORMATDETAILSW *d, DWORD flags)
{
    if (*(DWORD *)had == ACM_ID)
        return (MMRESULT)AcmIdMessage((ACMDRIVERID_ *)had, ACMDM_FORMAT_DETAILS, (LPARAM)d, flags);
    ACMDRIVER_ *driver = (ACMDRIVER_ *)had;
    RtlEnterCriticalSection(&driver->lock);
    MMRESULT r = (MMRESULT)AcmDriverMessage(driver, ACMDM_FORMAT_DETAILS, (LPARAM)d, flags);
    RtlLeaveCriticalSection(&driver->lock);
    return r;
}

MSACMAPI MMRESULT WINAPI acmFormatDetailsW(HACMDRIVER had, LPACMFORMATDETAILSW d, DWORD flags)
{
    if (!AcmBoot())
        return MMSYSERR_ERROR;
    if (flags & ~ACM_FORMATDETAILSF_QUERYMASK)
        return MMSYSERR_INVALFLAG;
    if (!AcmWritable(d, sizeof(DWORD)) || d->cbStruct < sizeof(*d) || !AcmWritable(d, d->cbStruct) ||
        d->cbwfx < sizeof(PCMWAVEFORMAT) || !AcmWritable(d->pwfx, d->cbwfx) || d->fdwSupport)
        return MMSYSERR_INVALPARAM;
    DWORD query = flags & ACM_FORMATDETAILSF_QUERYMASK;
    if (query > ACM_FORMATDETAILSF_FORMAT)
        return MMSYSERR_NOTSUPPORTED;
    if (query == ACM_FORMATDETAILSF_FORMAT && d->dwFormatTag != d->pwfx->wFormatTag)
        return MMSYSERR_INVALPARAM;
    if (!d->dwFormatTag)
        return MMSYSERR_INVALPARAM;
    if (query == ACM_FORMATDETAILSF_INDEX) {
        if (!AcmValid(had, ACM_ANY))
            return MMSYSERR_INVALHANDLE;
        ACMFORMATTAGDETAILSW tag = {sizeof(tag), 0, d->dwFormatTag};
        MMRESULT r = acmFormatTagDetailsW(had, &tag, ACM_FORMATTAGDETAILSF_FORMATTAG);
        if (r)
            return r;
        if (d->dwFormatIndex >= tag.cStandardFormats)
            return MMSYSERR_INVALPARAM;
    }
    if (had) {
        if (!AcmValid(had, ACM_ANY) || *(DWORD *)had == ACM_STREAM)
            return MMSYSERR_INVALHANDLE;
        d->szFormat[0] = 0;
        MMRESULT r = format_message(had, d, flags);
        if (!r) {
            if (!d->szFormat[0])
                AcmFormatName(d);
            if (query == ACM_FORMATDETAILSF_FORMAT)
                d->dwFormatIndex = 0;
        }
        return r;
    }
    MMRESULT r = MMSYSERR_NODRIVER;
    BOOL none = TRUE;
    AcmEnterEnum();
    for (ACMDRIVERID_ *id = AcmNextId(nullptr, 0); id; id = AcmNextId(id, 0)) {
        none = FALSE;
        d->szFormat[0] = 0;
        if (!(r = (MMRESULT)AcmIdMessage(id, ACMDM_FORMAT_DETAILS, (LPARAM)d, flags)))
            break;
    }
    AcmLeaveEnum();
    if (!r) {
        if (!d->szFormat[0])
            AcmFormatName(d);
        if (query == ACM_FORMATDETAILSF_FORMAT)
            d->dwFormatIndex = 0;
        return r;
    }
    if (!none || query != ACM_FORMATDETAILSF_FORMAT || d->dwFormatTag != WAVE_FORMAT_PCM)
        return r;
    d->dwFormatIndex = 0;
    d->fdwSupport = 0;
    d->cbwfx = sizeof(PCMWAVEFORMAT);
    d->szFormat[0] = 0;
    AcmFormatName(d);
    return MMSYSERR_NOERROR;
}

static void format_to_ansi(ACMFORMATDETAILSA *a, const ACMFORMATDETAILSW *w)
{
    a->cbStruct = sizeof(*a);
    a->dwFormatIndex = w->dwFormatIndex;
    a->dwFormatTag = w->dwFormatTag;
    a->fdwSupport = w->fdwSupport;
    a->pwfx = w->pwfx;
    a->cbwfx = w->cbwfx;
    AcmToAnsi(a->szFormat, w->szFormat, ACMFORMATDETAILS_FORMAT_CHARS);
}

MSACMAPI MMRESULT WINAPI acmFormatDetailsA(HACMDRIVER had, LPACMFORMATDETAILSA d, DWORD flags)
{
    if (!AcmWritable(d, sizeof(DWORD)) || d->cbStruct < sizeof(*d) || !AcmWritable(d, d->cbStruct))
        return MMSYSERR_INVALPARAM;
    ACMFORMATDETAILSW w = {sizeof(w), d->dwFormatIndex, d->dwFormatTag, d->fdwSupport, d->pwfx, d->cbwfx};
    MMRESULT r = acmFormatDetailsW(had, &w, flags);
    if (!r)
        format_to_ansi(d, &w);
    return r;
}

/* ------------------------------------------------------------------ suggestions */

MSACMAPI MMRESULT WINAPI acmFormatSuggest(HACMDRIVER had, LPWAVEFORMATEX source, LPWAVEFORMATEX destination,
                                          DWORD size, DWORD flags)
{
    if (flags & ~ACM_FORMATSUGGESTF_TYPEMASK)
        return MMSYSERR_INVALFLAG;
    if (!AcmReadableFormat(source) || !AcmWritable(destination, size))
        return MMSYSERR_INVALPARAM;
    if (!AcmBoot())
        return MMSYSERR_ERROR;
    /* pcm is tried first when the caller leaves the tag free. */
    if (!had && source->wFormatTag == WAVE_FORMAT_PCM && !(flags & ACM_FORMATSUGGESTF_WFORMATTAG)) {
        WORD tag = destination->wFormatTag;
        destination->wFormatTag = WAVE_FORMAT_PCM;
        if (!acmFormatSuggest(nullptr, source, destination, size, flags | ACM_FORMATSUGGESTF_WFORMATTAG))
            return MMSYSERR_NOERROR;
        destination->wFormatTag = tag;
    }
    DWORD need;
    if (flags & ACM_FORMATSUGGESTF_WFORMATTAG) {
        ACMFORMATTAGDETAILSW tag = {sizeof(tag), 0, destination->wFormatTag};
        MMRESULT r = acmFormatTagDetailsW(had, &tag, ACM_FORMATTAGDETAILSF_FORMATTAG);
        if (r)
            return r;
        need = tag.cbFormatSize;
    } else {
        MMRESULT r = AcmMaxFormatSize((HACMOBJ)had, &need);
        if (r)
            return r;
    }
    if (size < need)
        return MMSYSERR_INVALPARAM;
    ACMDRVFORMATSUGGEST s = {sizeof(s), flags, source, AcmFormatSize(source), destination, size};
    if (had) {
        if (!AcmValid(had, ACM_DRIVER))
            return MMSYSERR_INVALHANDLE;
        ACMDRIVER_ *d = (ACMDRIVER_ *)had;
        RtlEnterCriticalSection(&d->lock);
        MMRESULT r = (MMRESULT)AcmDriverMessage(d, ACMDM_FORMAT_SUGGEST, (LPARAM)&s, 0);
        RtlLeaveCriticalSection(&d->lock);
        return r;
    }
    if (!flags && source->wFormatTag == WAVE_FORMAT_PCM) {
        memcpy(destination, source, sizeof(PCMWAVEFORMAT));
        return MMSYSERR_NOERROR;
    }
    MMRESULT r = MMSYSERR_NODRIVER;
    AcmEnterEnum();
    for (ACMDRIVERID_ *id = AcmNextId(nullptr, 0); id; id = AcmNextId(id, 0)) {
        if (!AcmHasFormatTag(id, source->wFormatTag))
            continue;
        if ((flags & ACM_FORMATSUGGESTF_WFORMATTAG) && !AcmHasFormatTag(id, destination->wFormatTag))
            continue;
        if (!(r = (MMRESULT)AcmIdMessage(id, ACMDM_FORMAT_SUGGEST, (LPARAM)&s, 0)))
            break;
    }
    AcmLeaveEnum();
    return r;
}

/* ------------------------------------------------------------------ format enumeration */

typedef struct FORMAT_ENUM {
    ACMFORMATDETAILSW *details;
    ACMFORMATENUMCBW callback;
    DWORD_PTR instance;
    const WAVEFORMATEX *source;
    DWORD flags;
} FORMAT_ENUM;

/* the device opens the format: any output with ACM_FORMATENUMF_OUTPUT, any input with INPUT. */
static BOOL device_supports(const WAVEFORMATEX *format, DWORD flags)
{
    if (flags & ACM_FORMATENUMF_HARDWARE) {
        if (flags & ACM_FORMATENUMF_OUTPUT) {
            UINT n = waveOutGetNumDevs();
            for (UINT i = 0; i < n; i++)
                if (!waveOutOpen(nullptr, i, format, 0, 0, WAVE_FORMAT_QUERY))
                    return TRUE;
        }
        if (flags & ACM_FORMATENUMF_INPUT) {
            UINT n = waveInGetNumDevs();
            for (UINT i = 0; i < n; i++)
                if (!waveInOpen(nullptr, i, format, 0, 0, WAVE_FORMAT_QUERY))
                    return TRUE;
        }
        return FALSE;
    }
    if ((flags & ACM_FORMATENUMF_OUTPUT) && !waveOutOpen(nullptr, WAVE_MAPPER, format, 0, 0, WAVE_FORMAT_QUERY))
        return TRUE;
    return (flags & ACM_FORMATENUMF_INPUT) && !waveInOpen(nullptr, WAVE_MAPPER, format, 0, 0, WAVE_FORMAT_QUERY);
}

static BOOL matches(const WAVEFORMATEX *f, const WAVEFORMATEX *source, DWORD flags)
{
    return (!(flags & ACM_FORMATENUMF_NCHANNELS) || f->nChannels == source->nChannels) &&
           (!(flags & ACM_FORMATENUMF_NSAMPLESPERSEC) || f->nSamplesPerSec == source->nSamplesPerSec) &&
           (!(flags & ACM_FORMATENUMF_WBITSPERSAMPLE) || f->wBitsPerSample == source->wBitsPerSample);
}

static BOOL wanted(const WAVEFORMATEX *f, DWORD flags)
{
    return !(flags & (ACM_FORMATENUMF_INPUT | ACM_FORMATENUMF_OUTPUT)) || device_supports(f, flags);
}

/* the standard formats of a tag, then the suggestion for conversions when it was not one of them; TRUE stops. */
static BOOL enum_tag(FORMAT_ENUM *e, ACMDRIVER_ *driver, const ACMFORMATTAGDETAILSW *tag)
{
    ACMFORMATDETAILSW *d = e->details;
    WAVEFORMATEX *suggestion = nullptr;
    DWORD suggestion_size = 0;
    if (e->flags & ACM_FORMATENUMF_CONVERT) {
        if (AcmMaxFormatSize((HACMOBJ)driver, &suggestion_size) ||
            !(suggestion = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, suggestion_size))) {
            suggestion = nullptr;
        } else {
            DWORD suggest = ACM_FORMATSUGGESTF_WFORMATTAG;
            suggestion->wFormatTag = (WORD)tag->dwFormatTag;
            if (e->flags & ACM_FORMATENUMF_NCHANNELS) {
                suggest |= ACM_FORMATSUGGESTF_NCHANNELS;
                suggestion->nChannels = e->source->nChannels;
            }
            if (e->flags & ACM_FORMATENUMF_NSAMPLESPERSEC) {
                suggest |= ACM_FORMATSUGGESTF_NSAMPLESPERSEC;
                suggestion->nSamplesPerSec = e->source->nSamplesPerSec;
            }
            if (e->flags & ACM_FORMATENUMF_WBITSPERSAMPLE) {
                suggest |= ACM_FORMATSUGGESTF_WBITSPERSAMPLE;
                suggestion->wBitsPerSample = e->source->wBitsPerSample;
            }
            if (acmFormatSuggest((HACMDRIVER)driver, (LPWAVEFORMATEX)e->source, suggestion, suggestion_size, suggest)) {
                HeapFree(GetProcessHeap(), 0, suggestion);
                return FALSE;
            }
            suggestion_size = AcmFormatSize(suggestion);
        }
    }
    DWORD size = d->cbStruct, cbwfx = d->cbwfx;
    WAVEFORMATEX *pwfx = d->pwfx;
    DWORD support = driver->id->support;
    for (DWORD i = 0; i < tag->cStandardFormats; i++) {
        d->cbStruct = size;
        d->dwFormatIndex = i;
        d->dwFormatTag = tag->dwFormatTag;
        d->fdwSupport = 0;
        d->cbwfx = cbwfx;
        d->pwfx = pwfx;
        d->szFormat[0] = 0;
        if (acmFormatDetailsW((HACMDRIVER)driver, d, ACM_FORMATDETAILSF_INDEX) || !matches(pwfx, e->source, e->flags))
            continue;
        if (e->flags & ACM_FORMATENUMF_CONVERT) {
            if (acmStreamOpen(nullptr, (HACMDRIVER)driver, (LPWAVEFORMATEX)e->source, pwfx, nullptr, 0, 0,
                              ACM_STREAMOPENF_QUERY | ACM_STREAMOPENF_NONREALTIME))
                continue;
            if (suggestion && AcmFormatSize(pwfx) == suggestion_size && !memcmp(pwfx, suggestion, suggestion_size)) {
                HeapFree(GetProcessHeap(), 0, suggestion);
                suggestion = nullptr;
            }
        }
        if (wanted(pwfx, e->flags) && !e->callback((HACMDRIVERID)driver->id, d, e->instance, support)) {
            HeapFree(GetProcessHeap(), 0, suggestion);
            return TRUE;
        }
    }
    if (!suggestion)
        return FALSE;
    BOOL stop = FALSE;
    d->cbStruct = size;
    d->dwFormatIndex = 0;
    d->dwFormatTag = suggestion->wFormatTag;
    d->pwfx = suggestion;
    d->cbwfx = suggestion_size;
    d->fdwSupport = 0;
    d->szFormat[0] = 0;
    if (!acmFormatDetailsW((HACMDRIVER)driver, d, ACM_FORMATDETAILSF_FORMAT) && wanted(suggestion, e->flags))
        stop = !e->callback((HACMDRIVERID)driver->id, d, e->instance, support);
    d->cbwfx = cbwfx;
    d->pwfx = pwfx;
    HeapFree(GetProcessHeap(), 0, suggestion);
    return stop;
}

/* what the driver suggests for the source in the tag; TRUE stops. */
static BOOL suggest_tag(FORMAT_ENUM *e, ACMDRIVER_ *driver, const ACMFORMATTAGDETAILSW *tag)
{
    ACMFORMATDETAILSW *d = e->details;
    d->dwFormatTag = tag->dwFormatTag;
    d->fdwSupport = 0;
    d->pwfx->wFormatTag = (WORD)tag->dwFormatTag;
    DWORD suggest = ACM_FORMATSUGGESTF_WFORMATTAG |
                    (e->flags & (ACM_FORMATENUMF_NCHANNELS | ACM_FORMATENUMF_NSAMPLESPERSEC |
                                 ACM_FORMATENUMF_WBITSPERSAMPLE));
    if (acmFormatSuggest((HACMDRIVER)driver, (LPWAVEFORMATEX)e->source, d->pwfx, d->cbwfx, suggest) ||
        acmFormatDetailsW((HACMDRIVER)driver, d, ACM_FORMATDETAILSF_FORMAT) || !wanted(d->pwfx, e->flags))
        return FALSE;
    return !e->callback((HACMDRIVERID)driver->id, d, e->instance, driver->id->support);
}

static BOOL run_tag(FORMAT_ENUM *e, ACMDRIVER_ *driver, const ACMFORMATTAGDETAILSW *tag)
{
    return e->flags & ACM_FORMATENUMF_SUGGEST ? suggest_tag(e, driver, tag) : enum_tag(e, driver, tag);
}

/* the 16 standard pcm formats, when no driver is installed. */
static void enum_pcm(FORMAT_ENUM *e)
{
    ACMFORMATDETAILSW *d = e->details;
    WAVEFORMATEX *f = d->pwfx;
    for (DWORD i = 0; i < 16; i++) {
        d->dwFormatIndex = 0;
        d->fdwSupport = 0;
        d->dwFormatTag = WAVE_FORMAT_PCM;
        f->wFormatTag = WAVE_FORMAT_PCM;
        f->nSamplesPerSec = pcm_rates[i >> 2];
        f->nChannels = (WORD)((i & 1) + 1);
        f->wBitsPerSample = i & 2 ? 16 : 8;
        f->nBlockAlign = (WORD)((f->wBitsPerSample >> 3) << (f->nChannels >> 1));
        f->nAvgBytesPerSec = f->nSamplesPerSec * f->nBlockAlign;
        if (d->cbStruct > sizeof(*d))
            d->cbStruct = sizeof(*d);
        AcmFormatName(d);
        if (matches(f, e->source, e->flags) && wanted(f, e->flags) && !e->callback(nullptr, d, e->instance, 0))
            return;
    }
}

MSACMAPI MMRESULT WINAPI acmFormatEnumW(HACMDRIVER had, LPACMFORMATDETAILSW d, ACMFORMATENUMCBW callback,
                                        DWORD_PTR instance, DWORD flags)
{
    if (!AcmBoot())
        return MMSYSERR_ERROR;
    if (!AcmWritable(d, sizeof(DWORD)) || d->cbStruct < sizeof(*d) || !AcmWritable(d, d->cbStruct))
        return MMSYSERR_INVALPARAM;
    if (had && !AcmValid(had, ACM_DRIVER))
        return MMSYSERR_INVALHANDLE;
    if (!callback)
        return MMSYSERR_INVALPARAM;
    if ((flags & 0xFE00FFFF) ||
        (flags & (ACM_FORMATENUMF_HARDWARE | ACM_FORMATENUMF_INPUT | ACM_FORMATENUMF_OUTPUT)) == ACM_FORMATENUMF_HARDWARE)
        return MMSYSERR_INVALFLAG;
    DWORD largest_size;
    MMRESULT r = AcmMaxFormatSize((HACMOBJ)had, &largest_size);
    if (r)
        return r;
    if (d->cbwfx < largest_size || !AcmWritable(d->pwfx, d->cbwfx) || d->fdwSupport)
        return MMSYSERR_INVALPARAM;
    DWORD tag = 0;
    if (flags & ACM_FORMATENUMF_WFORMATTAG) {
        tag = d->pwfx->wFormatTag;
        if (!tag || d->dwFormatTag != tag)
            return MMSYSERR_INVALPARAM;
    }
    BOOL from_source = flags & (ACM_FORMATENUMF_CONVERT | ACM_FORMATENUMF_SUGGEST);
    DWORD source_size = sizeof(PCMWAVEFORMAT);
    if (from_source) {
        if (!AcmReadableFormat(d->pwfx) || !d->pwfx->wFormatTag)
            return MMSYSERR_INVALPARAM;
        source_size = AcmFormatSize(d->pwfx);
    }
    WAVEFORMATEX *source = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, source_size > sizeof(WAVEFORMATEX)
                                                                            ? source_size
                                                                            : sizeof(WAVEFORMATEX));
    if (!source)
        return MMSYSERR_NOMEM;
    memcpy(source, d->pwfx, source_size);
    FORMAT_ENUM e = {d, callback, instance, source, flags};
    ACMFORMATTAGDETAILSW t;
    r = MMSYSERR_NOERROR;
    if (had) {
        ACMDRIVERID_ *id = ((ACMDRIVER_ *)had)->id;
        for (DWORD i = 0; i < id->format_count; i++) {
            if (tag && id->formats[i].tag != tag)
                continue;
            t = (ACMFORMATTAGDETAILSW){sizeof(t), i};
            r = AcmFormatTagDetails(id, &t, ACM_FORMATTAGDETAILSF_INDEX);
            BOOL stop = !r && run_tag(&e, (ACMDRIVER_ *)had, &t);
            if (stop || tag) {
                r = MMSYSERR_NOERROR;
                break;
            }
        }
        HeapFree(GetProcessHeap(), 0, source);
        return r;
    }
    BOOL fallback = !from_source;
    AcmEnterEnum();
    if (tag) {
        /* the driver with the most standard formats of the tag. */
        ACMDRIVERID_ *best = nullptr;
        DWORD best_index = 0, most = 0;
        BOOL any = FALSE;
        for (ACMDRIVERID_ *id = AcmNextId(nullptr, 0); id; id = AcmNextId(id, 0)) {
            any = TRUE;
            for (DWORD i = 0; i < id->format_count; i++) {
                if (id->formats[i].tag != tag)
                    continue;
                t = (ACMFORMATTAGDETAILSW){sizeof(t), i};
                r = AcmFormatTagDetails(id, &t, ACM_FORMATTAGDETAILSF_INDEX);
                if (!r && (!best || t.cStandardFormats > most)) {
                    most = t.cStandardFormats;
                    best = id;
                    best_index = i;
                }
                break;
            }
        }
        if (!any) {
            if (fallback && tag != WAVE_FORMAT_PCM)
                fallback = FALSE;
        } else {
            fallback = FALSE;
            ACMDRIVER_ *driver;
            if (best) {
                t = (ACMFORMATTAGDETAILSW){sizeof(t), best_index};
                if (!(r = AcmFormatTagDetails(best, &t, ACM_FORMATTAGDETAILSF_INDEX)) &&
                    !(r = AcmOpenDriver(&driver, best))) {
                    run_tag(&e, driver, &t);
                    r = MMSYSERR_NOERROR;
                    AcmCloseDriver(driver);
                }
            }
        }
    } else {
        for (ACMDRIVERID_ *id = AcmNextId(nullptr, 0); id; id = AcmNextId(id, 0)) {
            fallback = FALSE;
            if (from_source && !AcmHasFormatTag(id, source->wFormatTag) && source->wFormatTag)
                continue;
            ACMDRIVER_ *driver;
            if ((r = AcmOpenDriver(&driver, id)))
                continue;
            BOOL stop = FALSE;
            for (DWORD i = 0; i < id->format_count && !stop; i++) {
                t = (ACMFORMATTAGDETAILSW){sizeof(t), i};
                if (!(r = AcmFormatTagDetails(id, &t, ACM_FORMATTAGDETAILSF_INDEX)))
                    stop = run_tag(&e, driver, &t);
            }
            AcmCloseDriver(driver);
            if (stop) {
                r = MMSYSERR_NOERROR;
                break;
            }
        }
    }
    AcmLeaveEnum();
    if (fallback)
        enum_pcm(&e);
    HeapFree(GetProcessHeap(), 0, source);
    return r;
}

typedef struct FORMAT_THUNK {
    ACMFORMATENUMCBA callback;
    DWORD_PTR instance;
    ACMFORMATDETAILSA *details;
} FORMAT_THUNK;

static BOOL CALLBACK format_thunk(HACMDRIVERID hadid, LPACMFORMATDETAILSW w, DWORD_PTR instance, DWORD support)
{
    FORMAT_THUNK *t = (FORMAT_THUNK *)instance;
    format_to_ansi(t->details, w);
    return t->callback(hadid, t->details, t->instance, support);
}

MSACMAPI MMRESULT WINAPI acmFormatEnumA(HACMDRIVER had, LPACMFORMATDETAILSA d, ACMFORMATENUMCBA callback,
                                        DWORD_PTR instance, DWORD flags)
{
    if (!callback || !AcmWritable(d, sizeof(DWORD)) || d->cbStruct < sizeof(*d) || !AcmWritable(d, d->cbStruct))
        return MMSYSERR_INVALPARAM;
    ACMFORMATDETAILSW w = {sizeof(w), d->dwFormatIndex, d->dwFormatTag, d->fdwSupport, d->pwfx, d->cbwfx};
    FORMAT_THUNK t = {callback, instance, d};
    return acmFormatEnumW(had, &w, format_thunk, (DWORD_PTR)&t, flags);
}

/* ------------------------------------------------------------------ filters */

static MMRESULT filter_tag_details(ACMDRIVERID_ *id, ACMFILTERTAGDETAILSW *d, DWORD flags)
{
    DWORD query = flags & ACM_FILTERTAGDETAILSF_QUERYMASK;
    if (query == ACM_FILTERTAGDETAILSF_FILTERTAG ? !AcmHasFilterTag(id, d->dwFilterTag)
                                                  : query > ACM_FILTERTAGDETAILSF_LARGESTSIZE)
        return ACMERR_NOTPOSSIBLE;
    MMRESULT r = (MMRESULT)AcmIdMessage(id, ACMDM_FILTERTAG_DETAILS, (LPARAM)d, flags);
    if (r)
        return r;
    return d->dwFilterTag ? MMSYSERR_NOERROR : MMSYSERR_ERROR;
}

MSACMAPI MMRESULT WINAPI acmFilterTagDetailsW(HACMDRIVER had, LPACMFILTERTAGDETAILSW d, DWORD flags)
{
    if (!AcmBoot())
        return MMSYSERR_ERROR;
    if (flags & ~ACM_FILTERTAGDETAILSF_QUERYMASK)
        return MMSYSERR_INVALFLAG;
    if (!AcmWritable(d, sizeof(DWORD)) || d->cbStruct < sizeof(*d) || !AcmWritable(d, d->cbStruct) || d->fdwSupport)
        return MMSYSERR_INVALPARAM;
    DWORD query = flags & ACM_FILTERTAGDETAILSF_QUERYMASK;
    if (query == ACM_FILTERTAGDETAILSF_INDEX) {
        if (!AcmValid(had, ACM_ANY))
            return MMSYSERR_INVALHANDLE;
        if (d->dwFilterTag)
            return MMSYSERR_INVALPARAM;
    } else if (query == ACM_FILTERTAGDETAILSF_FILTERTAG) {
        if (!d->dwFilterTag)
            return MMSYSERR_INVALPARAM;
    } else if (query != ACM_FILTERTAGDETAILSF_LARGESTSIZE) {
        return MMSYSERR_NOTSUPPORTED;
    }
    if (had) {
        if (!AcmValid(had, ACM_ANY) || *(DWORD *)had == ACM_STREAM)
            return MMSYSERR_INVALHANDLE;
        ACMDRIVERID_ *id = AcmIdOf((HACMOBJ)had);
        if (*(DWORD *)had == ACM_ID && (id->state & ID_DISABLED))
            return MMSYSERR_NOTENABLED;
        return filter_tag_details(id, d, flags);
    }
    AcmEnterEnum();
    ACMDRIVERID_ *id = driver_for_tag(d->dwFilterTag, query == ACM_FILTERTAGDETAILSF_LARGESTSIZE, TRUE);
    AcmLeaveEnum();
    return id ? filter_tag_details(id, d, flags) : ACMERR_NOTPOSSIBLE;
}

static void filter_tag_to_ansi(ACMFILTERTAGDETAILSA *a, const ACMFILTERTAGDETAILSW *w, DWORD size)
{
    a->dwFilterTagIndex = w->dwFilterTagIndex;
    a->dwFilterTag = w->dwFilterTag;
    a->cbFilterSize = w->cbFilterSize;
    a->fdwSupport = w->fdwSupport;
    a->cbStruct = size < sizeof(*a) ? size : sizeof(*a);
    a->cStandardFilters = w->cStandardFilters;
    AcmToAnsi(a->szFilterTag, w->szFilterTag, ACMFILTERTAGDETAILS_FILTERTAG_CHARS);
}

MSACMAPI MMRESULT WINAPI acmFilterTagDetailsA(HACMDRIVER had, LPACMFILTERTAGDETAILSA d, DWORD flags)
{
    if (!AcmWritable(d, sizeof(DWORD)) || d->cbStruct < sizeof(*d) || !AcmWritable(d, d->cbStruct))
        return MMSYSERR_INVALPARAM;
    ACMFILTERTAGDETAILSW w = {sizeof(w), d->dwFilterTagIndex, d->dwFilterTag, d->cbFilterSize, d->fdwSupport,
                              d->cStandardFilters};
    MMRESULT r = acmFilterTagDetailsW(had, &w, flags);
    if (!r)
        filter_tag_to_ansi(d, &w, d->cbStruct);
    return r;
}

MSACMAPI MMRESULT WINAPI acmFilterTagEnumW(HACMDRIVER had, LPACMFILTERTAGDETAILSW d, ACMFILTERTAGENUMCBW callback,
                                           DWORD_PTR instance, DWORD flags)
{
    if (!AcmBoot())
        return MMSYSERR_ERROR;
    if (!callback)
        return MMSYSERR_INVALPARAM;
    if (flags)
        return MMSYSERR_INVALFLAG;
    if (!AcmWritable(d, sizeof(DWORD)) || d->cbStruct < sizeof(*d) || !AcmWritable(d, d->cbStruct) || d->fdwSupport)
        return MMSYSERR_INVALPARAM;
    DWORD size = sizeof(*d);
    MMRESULT r = MMSYSERR_NOERROR;
    if (had) {
        if (!AcmValid(had, ACM_DRIVER))
            return MMSYSERR_INVALHANDLE;
        ACMDRIVERID_ *id = ((ACMDRIVER_ *)had)->id;
        for (DWORD i = 0; i < id->filter_count; i++) {
            d->cbStruct = size;
            d->dwFilterTagIndex = i;
            if ((r = filter_tag_details(id, d, ACM_FILTERTAGDETAILSF_INDEX)))
                break;
            if (!callback((HACMDRIVERID)id, d, instance, id->support))
                break;
        }
        return r;
    }
    AcmEnterEnum();
    for (ACMDRIVERID_ *id = AcmNextId(nullptr, 0); id; id = AcmNextId(id, 0))
        for (DWORD i = 0; i < id->filter_count; i++) {
            if (seen_before(id, id->filters[i].tag, TRUE))
                continue;
            d->cbStruct = size;
            d->dwFilterTagIndex = i;
            if ((r = filter_tag_details(id, d, ACM_FILTERTAGDETAILSF_INDEX)))
                goto out;
            if (!callback((HACMDRIVERID)id, d, instance, id->support))
                goto out;
        }
out:
    AcmLeaveEnum();
    return r;
}

typedef struct FILTER_TAG_THUNK {
    ACMFILTERTAGENUMCBA callback;
    DWORD_PTR instance;
    ACMFILTERTAGDETAILSA *details;
} FILTER_TAG_THUNK;

static BOOL CALLBACK filter_tag_thunk(HACMDRIVERID hadid, LPACMFILTERTAGDETAILSW w, DWORD_PTR instance, DWORD support)
{
    FILTER_TAG_THUNK *t = (FILTER_TAG_THUNK *)instance;
    filter_tag_to_ansi(t->details, w, sizeof(*t->details));
    return t->callback(hadid, t->details, t->instance, support);
}

MSACMAPI MMRESULT WINAPI acmFilterTagEnumA(HACMDRIVER had, LPACMFILTERTAGDETAILSA d, ACMFILTERTAGENUMCBA callback,
                                           DWORD_PTR instance, DWORD flags)
{
    if (!callback || !AcmWritable(d, sizeof(DWORD)) || d->cbStruct < sizeof(*d) || !AcmWritable(d, d->cbStruct))
        return MMSYSERR_INVALPARAM;
    ACMFILTERTAGDETAILSW w = {sizeof(w), d->dwFilterTagIndex, d->dwFilterTag, d->cbFilterSize, d->fdwSupport,
                              d->cStandardFilters};
    FILTER_TAG_THUNK t = {callback, instance, d};
    return acmFilterTagEnumW(had, &w, filter_tag_thunk, (DWORD_PTR)&t, flags);
}

MSACMAPI MMRESULT WINAPI acmFilterDetailsW(HACMDRIVER had, LPACMFILTERDETAILSW d, DWORD flags)
{
    if (!AcmBoot())
        return MMSYSERR_ERROR;
    if (flags & ~ACM_FILTERDETAILSF_QUERYMASK)
        return MMSYSERR_INVALFLAG;
    if (!AcmWritable(d, sizeof(DWORD)) || d->cbStruct < sizeof(*d) || !AcmWritable(d, d->cbStruct) ||
        d->cbwfltr < sizeof(WAVEFILTER) || !AcmWritable(d->pwfltr, d->cbwfltr) || d->fdwSupport)
        return MMSYSERR_INVALPARAM;
    DWORD query = flags & ACM_FILTERDETAILSF_QUERYMASK;
    if (query > ACM_FILTERDETAILSF_FILTER)
        return MMSYSERR_NOTSUPPORTED;
    if (query == ACM_FILTERDETAILSF_FILTER && d->dwFilterTag != d->pwfltr->dwFilterTag)
        return MMSYSERR_INVALPARAM;
    if (!d->dwFilterTag)
        return MMSYSERR_INVALPARAM;
    if (query == ACM_FILTERDETAILSF_INDEX) {
        if (!AcmValid(had, ACM_ANY))
            return MMSYSERR_INVALHANDLE;
        ACMFILTERTAGDETAILSW tag = {sizeof(tag), 0, d->dwFilterTag};
        MMRESULT r = acmFilterTagDetailsW(had, &tag, ACM_FILTERTAGDETAILSF_FILTERTAG);
        if (r)
            return r;
        if (d->dwFilterIndex >= tag.cStandardFilters)
            return MMSYSERR_INVALPARAM;
    }
    MMRESULT r;
    if (had) {
        if (!AcmValid(had, ACM_ANY) || *(DWORD *)had == ACM_STREAM)
            return MMSYSERR_INVALHANDLE;
        ACMDRIVERID_ *id = AcmIdOf((HACMOBJ)had);
        if (!(id->support & ACMDRIVERDETAILS_SUPPORTF_FILTER))
            return MMSYSERR_INVALPARAM;
        if (*(DWORD *)had == ACM_ID)
            r = (MMRESULT)AcmIdMessage(id, ACMDM_FILTER_DETAILS, (LPARAM)d, flags);
        else {
            ACMDRIVER_ *driver = (ACMDRIVER_ *)had;
            RtlEnterCriticalSection(&driver->lock);
            r = (MMRESULT)AcmDriverMessage(driver, ACMDM_FILTER_DETAILS, (LPARAM)d, flags);
            RtlLeaveCriticalSection(&driver->lock);
        }
        if (!r && query == ACM_FILTERDETAILSF_FILTER)
            d->dwFilterIndex = 0;
        return r;
    }
    r = MMSYSERR_NODRIVER;
    AcmEnterEnum();
    for (ACMDRIVERID_ *id = AcmNextId(nullptr, 0); id; id = AcmNextId(id, 0)) {
        if (!(id->support & ACMDRIVERDETAILS_SUPPORTF_FILTER))
            continue;
        if (!(r = (MMRESULT)AcmIdMessage(id, ACMDM_FILTER_DETAILS, (LPARAM)d, flags))) {
            if (query == ACM_FILTERDETAILSF_FILTER)
                d->dwFilterIndex = 0;
            break;
        }
    }
    AcmLeaveEnum();
    return r;
}

static void filter_to_ansi(ACMFILTERDETAILSA *a, const ACMFILTERDETAILSW *w)
{
    a->cbStruct = sizeof(*a);
    a->dwFilterIndex = w->dwFilterIndex;
    a->dwFilterTag = w->dwFilterTag;
    a->fdwSupport = w->fdwSupport;
    a->pwfltr = w->pwfltr;
    a->cbwfltr = w->cbwfltr;
    AcmToAnsi(a->szFilter, w->szFilter, ACMFILTERDETAILS_FILTER_CHARS);
}

MSACMAPI MMRESULT WINAPI acmFilterDetailsA(HACMDRIVER had, LPACMFILTERDETAILSA d, DWORD flags)
{
    if (!AcmWritable(d, sizeof(DWORD)) || d->cbStruct < sizeof(*d) || !AcmWritable(d, d->cbStruct))
        return MMSYSERR_INVALPARAM;
    ACMFILTERDETAILSW w = {sizeof(w), d->dwFilterIndex, d->dwFilterTag, d->fdwSupport, d->pwfltr, d->cbwfltr};
    MMRESULT r = acmFilterDetailsW(had, &w, flags);
    if (!r)
        filter_to_ansi(d, &w);
    return r;
}

/* the standard filters of a tag of a driver id; TRUE stops. */
static BOOL enum_filters(ACMDRIVERID_ *id, const ACMFILTERTAGDETAILSW *tag, ACMFILTERDETAILSW *d,
                         ACMFILTERENUMCBW callback, DWORD_PTR instance)
{
    DWORD size = d->cbStruct, cbwfltr = d->cbwfltr;
    WAVEFILTER *pwfltr = d->pwfltr;
    for (DWORD i = 0; i < tag->cStandardFilters; i++) {
        d->cbStruct = size;
        d->dwFilterIndex = i;
        d->dwFilterTag = tag->dwFilterTag;
        d->fdwSupport = 0;
        d->pwfltr = pwfltr;
        d->cbwfltr = cbwfltr;
        d->szFilter[0] = 0;
        if (!acmFilterDetailsW((HACMDRIVER)id, d, ACM_FILTERDETAILSF_INDEX) &&
            !callback((HACMDRIVERID)id, d, instance, id->support))
            return TRUE;
    }
    return FALSE;
}

MSACMAPI MMRESULT WINAPI acmFilterEnumW(HACMDRIVER had, LPACMFILTERDETAILSW d, ACMFILTERENUMCBW callback,
                                        DWORD_PTR instance, DWORD flags)
{
    if (!AcmBoot())
        return MMSYSERR_ERROR;
    if (!AcmWritable(d, sizeof(DWORD)) || d->cbStruct < sizeof(*d) || !AcmWritable(d, d->cbStruct))
        return MMSYSERR_INVALPARAM;
    if (had && !AcmValid(had, ACM_DRIVER))
        return MMSYSERR_INVALHANDLE;
    if (!callback)
        return MMSYSERR_INVALPARAM;
    if (flags & ~ACM_FILTERENUMF_DWFILTERTAG)
        return MMSYSERR_INVALFLAG;
    if (d->cbwfltr < sizeof(WAVEFILTER) || !AcmWritable(d->pwfltr, d->cbwfltr) || d->fdwSupport)
        return MMSYSERR_INVALPARAM;
    DWORD tag = 0;
    if (flags & ACM_FILTERENUMF_DWFILTERTAG) {
        tag = d->pwfltr->dwFilterTag;
        if (!tag)
            return MMSYSERR_INVALPARAM;
    }
    ACMFILTERTAGDETAILSW t;
    MMRESULT r = MMSYSERR_NOERROR;
    if (had) {
        ACMDRIVERID_ *id = ((ACMDRIVER_ *)had)->id;
        for (DWORD i = 0; i < id->filter_count; i++) {
            if (tag && id->filters[i].tag != tag)
                continue;
            t = (ACMFILTERTAGDETAILSW){sizeof(t), i};
            r = filter_tag_details(id, &t, ACM_FILTERTAGDETAILSF_INDEX);
            if (!r && enum_filters(id, &t, d, callback, instance))
                return MMSYSERR_NOERROR;
            if (tag)
                return r;
        }
        return r;
    }
    AcmEnterEnum();
    if (tag) {
        ACMDRIVERID_ *best = nullptr;
        DWORD best_index = 0, most = 0;
        for (ACMDRIVERID_ *id = AcmNextId(nullptr, 0); id; id = AcmNextId(id, 0))
            for (DWORD i = 0; i < id->filter_count; i++) {
                if (id->filters[i].tag != tag)
                    continue;
                t = (ACMFILTERTAGDETAILSW){sizeof(t), i};
                r = filter_tag_details(id, &t, ACM_FILTERTAGDETAILSF_INDEX);
                if (!best || t.cStandardFilters > most) {
                    most = t.cStandardFilters;
                    best = id;
                    best_index = i;
                }
                break;
            }
        if (best) {
            t = (ACMFILTERTAGDETAILSW){sizeof(t), best_index};
            if (!(r = filter_tag_details(best, &t, ACM_FILTERTAGDETAILSF_INDEX)))
                enum_filters(best, &t, d, callback, instance);
        }
    } else {
        for (ACMDRIVERID_ *id = AcmNextId(nullptr, 0); id; id = AcmNextId(id, 0))
            for (DWORD i = 0; i < id->filter_count; i++) {
                if (seen_before(id, id->filters[i].tag, TRUE))
                    continue;
                t = (ACMFILTERTAGDETAILSW){sizeof(t), i};
                if (!filter_tag_details(id, &t, ACM_FILTERTAGDETAILSF_INDEX) &&
                    enum_filters(id, &t, d, callback, instance))
                    goto out;
            }
    out:
        r = MMSYSERR_NOERROR;
    }
    AcmLeaveEnum();
    return r;
}

typedef struct FILTER_THUNK {
    ACMFILTERENUMCBA callback;
    DWORD_PTR instance;
    ACMFILTERDETAILSA *details;
} FILTER_THUNK;

static BOOL CALLBACK filter_thunk(HACMDRIVERID hadid, LPACMFILTERDETAILSW w, DWORD_PTR instance, DWORD support)
{
    FILTER_THUNK *t = (FILTER_THUNK *)instance;
    filter_to_ansi(t->details, w);
    return t->callback(hadid, t->details, t->instance, support);
}

MSACMAPI MMRESULT WINAPI acmFilterEnumA(HACMDRIVER had, LPACMFILTERDETAILSA d, ACMFILTERENUMCBA callback,
                                        DWORD_PTR instance, DWORD flags)
{
    if (!callback || !AcmWritable(d, sizeof(DWORD)) || d->cbStruct < sizeof(*d) || !AcmWritable(d, d->cbStruct))
        return MMSYSERR_INVALPARAM;
    ACMFILTERDETAILSW w = {sizeof(w), d->dwFilterIndex, d->dwFilterTag, d->fdwSupport, d->pwfltr, d->cbwfltr};
    FILTER_THUNK t = {callback, instance, d};
    return acmFilterEnumW(had, &w, filter_thunk, (DWORD_PTR)&t, flags);
}
