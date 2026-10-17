/*
 * conversion streams: one open driver instance converting buffers the
 * program prepares, its formats and filter kept with the stream.
 */
#include "msacm32_private.h"

/* the reserved words of a header, as ACMDRVSTREAMHEADER names them. */
static ACMDRVSTREAMHEADER *driver_header(ACMSTREAMHEADER *h)
{
    return (ACMDRVSTREAMHEADER *)h;
}

static MMRESULT stream_message(ACMSTREAM_ *s, UINT message, LPARAM param)
{
    return (MMRESULT)AcmDriverMessage(s->driver, message, (LPARAM)&s->instance, param);
}

/* what a driver says of opening the conversion, without opening it. */
static MMRESULT query(ACMDRIVER_ *driver, LPWAVEFORMATEX source, LPWAVEFORMATEX destination, LPWAVEFILTER filter,
                      DWORD flags)
{
    ACMDRVSTREAMINSTANCE instance = {sizeof(instance), source, destination, filter};
    instance.fdwOpen = flags | ACM_STREAMOPENF_QUERY;
    RtlEnterCriticalSection(&driver->lock);
    MMRESULT r = (MMRESULT)AcmDriverMessage(driver, ACMDM_STREAM_OPEN, (LPARAM)&instance, 0);
    RtlLeaveCriticalSection(&driver->lock);
    return r;
}

MSACMAPI MMRESULT WINAPI acmStreamOpen(LPHACMSTREAM phas, HACMDRIVER had, LPWAVEFORMATEX source,
                                       LPWAVEFORMATEX destination, LPWAVEFILTER filter, DWORD_PTR callback,
                                       DWORD_PTR instance, DWORD flags)
{
    if (phas) {
        if (!AcmWritable(phas, sizeof(*phas)))
            return MMSYSERR_INVALPARAM;
        *phas = nullptr;
    }
    if (flags & 0xFFF8FFF8)
        return MMSYSERR_INVALFLAG;
    BOOL querying = flags & ACM_STREAMOPENF_QUERY;
    if (!querying && !phas)
        return MMSYSERR_INVALPARAM;
    if (!AcmReadableFormat(source) || !AcmReadableFormat(destination))
        return MMSYSERR_INVALPARAM;
    if (!AcmBoot())
        return MMSYSERR_ERROR;
    BOOL async = flags & ACM_STREAMOPENF_ASYNC;
    if (!async && (callback || instance))
        return MMSYSERR_INVALPARAM;
    DWORD callback_type = flags & CALLBACK_TYPEMASK;
    if (callback_type == CALLBACK_WINDOW && !IsWindow((HWND)callback))
        return MMSYSERR_INVALPARAM;
    if (callback_type == CALLBACK_FUNCTION && !callback)
        return MMSYSERR_INVALPARAM;

    /* what the driver must support: a codec between tags, a converter within one, or a filter. */
    DWORD need = async ? ACMDRIVERDETAILS_SUPPORTF_ASYNC : 0;
    DWORD tag = source->wFormatTag;
    if (filter) {
        if (!AcmReadableFilter(filter))
            return MMSYSERR_INVALPARAM;
        if (memcmp(source, destination, sizeof(PCMWAVEFORMAT)))
            return ACMERR_NOTPOSSIBLE;
        need |= ACMDRIVERDETAILS_SUPPORTF_FILTER;
    } else if (source->wFormatTag == destination->wFormatTag) {
        need |= ACMDRIVERDETAILS_SUPPORTF_CONVERTER;
    } else {
        need |= ACMDRIVERDETAILS_SUPPORTF_CODEC;
        if (tag == WAVE_FORMAT_PCM)
            tag = destination->wFormatTag;
    }

    ACMDRIVER_ *driver = nullptr;
    BOOL given = had != nullptr;
    MMRESULT r;
    if (given) {
        if (!AcmValid(had, ACM_DRIVER))
            return MMSYSERR_INVALHANDLE;
        driver = (ACMDRIVER_ *)had;
        if ((driver->id->support & need) != need)
            return ACMERR_NOTPOSSIBLE;
        if (querying)
            return query(driver, source, destination, filter, flags);
    } else {
        /* the first driver the thread sees that supports the tag this way and accepts the formats. */
        AcmEnterEnum();
        for (ACMDRIVERID_ *id = AcmNextId(nullptr, 0); id; id = AcmNextId(id, 0)) {
            if ((id->support & need) != need)
                continue;
            ACMFORMATTAGDETAILSW details = {sizeof(details), 0, tag};
            if (AcmFormatTagDetails(id, &details, ACM_FORMATTAGDETAILSF_FORMATTAG) ||
                (details.fdwSupport & need) != need)
                continue;
            if (AcmOpenDriver(&driver, id))
                continue;
            if (!query(driver, source, destination, filter, flags))
                break;
            AcmCloseDriver(driver);
            driver = nullptr;
        }
        AcmLeaveEnum();
        if (!driver)
            return ACMERR_NOTPOSSIBLE;
        if (querying) {
            AcmCloseDriver(driver);
            return MMSYSERR_NOERROR;
        }
    }

    DWORD source_size = AcmFormatSize(source), destination_size = AcmFormatSize(destination);
    DWORD filter_size = filter ? filter->cbStruct : 0;
    ACMSTREAM_ *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                              sizeof(*s) + source_size + destination_size + filter_size);
    if (!s) {
        if (!given)
            AcmCloseDriver(driver);
        return MMSYSERR_NOMEM;
    }
    s->type = ACM_STREAM;
    s->driver = driver;
    s->given = given;
    RtlInitializeCriticalSection(&s->lock);
    s->instance.cbStruct = sizeof(s->instance);
    s->instance.pwfxSrc = (LPWAVEFORMATEX)s->formats;
    s->instance.pwfxDst = (LPWAVEFORMATEX)(s->formats + source_size);
    memcpy(s->instance.pwfxSrc, source, source_size);
    memcpy(s->instance.pwfxDst, destination, destination_size);
    if (filter) {
        s->instance.pwfltr = (LPWAVEFILTER)(s->formats + source_size + destination_size);
        memcpy(s->instance.pwfltr, filter, filter_size);
    }
    s->instance.dwCallback = callback;
    s->instance.dwInstance = instance;
    s->instance.fdwOpen = flags;
    s->instance.has = (HACMSTREAM)s;
    RtlEnterCriticalSection(&driver->lock);
    r = stream_message(s, ACMDM_STREAM_OPEN, 0);
    RtlLeaveCriticalSection(&driver->lock);
    if (r) {
        RtlDeleteCriticalSection(&s->lock);
        HeapFree(GetProcessHeap(), 0, s);
        if (!given)
            AcmCloseDriver(driver);
        return r;
    }
    RtlEnterCriticalSection(&AcmLock);
    s->next = driver->streams;
    driver->streams = s;
    RtlLeaveCriticalSection(&AcmLock);
    *phas = (HACMSTREAM)s;
    return MMSYSERR_NOERROR;
}

MSACMAPI MMRESULT WINAPI acmStreamClose(HACMSTREAM has, DWORD flags)
{
    if (!AcmValid(has, ACM_STREAM))
        return MMSYSERR_INVALHANDLE;
    if (flags)
        return MMSYSERR_INVALFLAG;
    ACMSTREAM_ *s = (ACMSTREAM_ *)has;
    if (s->prepared)
        return MMSYSERR_INVALPARAM;
    ACMDRIVER_ *driver = s->driver;
    RtlEnterCriticalSection(&driver->lock);
    MMRESULT r = stream_message(s, ACMDM_STREAM_CLOSE, 0);
    RtlLeaveCriticalSection(&driver->lock);
    if (r)
        return r;
    RtlEnterCriticalSection(&AcmLock);
    for (ACMSTREAM_ **p = &driver->streams; *p; p = &(*p)->next)
        if (*p == s) {
            *p = s->next;
            break;
        }
    s->type = 0;
    RtlLeaveCriticalSection(&AcmLock);
    if (!s->given)
        AcmCloseDriver(driver);
    RtlDeleteCriticalSection(&s->lock);
    HeapFree(GetProcessHeap(), 0, s);
    return MMSYSERR_NOERROR;
}

MSACMAPI MMRESULT WINAPI acmStreamSize(HACMSTREAM has, DWORD input, LPDWORD output, DWORD flags)
{
    if (!AcmWritable(output, sizeof(*output)))
        return MMSYSERR_INVALPARAM;
    *output = 0;
    if (!AcmValid(has, ACM_STREAM))
        return MMSYSERR_INVALHANDLE;
    if (flags & ~ACM_STREAMSIZEF_QUERYMASK)
        return MMSYSERR_INVALFLAG;
    if (!input)
        return MMSYSERR_INVALPARAM;
    DWORD query = flags & ACM_STREAMSIZEF_QUERYMASK;
    if (query > ACM_STREAMSIZEF_DESTINATION)
        return MMSYSERR_NOTSUPPORTED;
    ACMDRVSTREAMSIZE size = {sizeof(size), flags};
    if (query == ACM_STREAMSIZEF_SOURCE)
        size.cbSrcLength = input;
    else
        size.cbDstLength = input;
    ACMSTREAM_ *s = (ACMSTREAM_ *)has;
    RtlEnterCriticalSection(&s->lock);
    MMRESULT r = stream_message(s, ACMDM_STREAM_SIZE, (LPARAM)&size);
    RtlLeaveCriticalSection(&s->lock);
    if (r)
        return r;
    *output = query == ACM_STREAMSIZEF_SOURCE ? size.cbDstLength : size.cbSrcLength;
    return *output ? MMSYSERR_NOERROR : ACMERR_NOTPOSSIBLE;
}

MSACMAPI MMRESULT WINAPI acmStreamReset(HACMSTREAM has, DWORD flags)
{
    if (!AcmValid(has, ACM_STREAM))
        return MMSYSERR_INVALHANDLE;
    if (flags)
        return MMSYSERR_INVALFLAG;
    ACMSTREAM_ *s = (ACMSTREAM_ *)has;
    if (!(s->instance.fdwOpen & ACM_STREAMOPENF_ASYNC))
        return MMSYSERR_NOERROR;
    RtlEnterCriticalSection(&s->lock);
    MMRESULT r = stream_message(s, ACMDM_STREAM_RESET, 0);
    RtlLeaveCriticalSection(&s->lock);
    return r;
}

MSACMAPI MMRESULT WINAPI acmStreamMessage(HACMSTREAM has, UINT message, LPARAM param1, LPARAM param2)
{
    if (!AcmValid(has, ACM_STREAM))
        return MMSYSERR_INVALHANDLE;
    if ((message < ACMDM_USER || message > ACMDM_USER + 0x1FFF) && message != ACMDM_STREAM_UPDATE)
        return MMSYSERR_INVALPARAM;
    ACMSTREAM_ *s = (ACMSTREAM_ *)has;
    RtlEnterCriticalSection(&s->lock);
    MMRESULT r = (MMRESULT)AcmDriverMessage(s->driver, message, param1, param2);
    RtlLeaveCriticalSection(&s->lock);
    return r;
}

MSACMAPI MMRESULT WINAPI acmStreamPrepareHeader(HACMSTREAM has, LPACMSTREAMHEADER h, DWORD flags)
{
    if (!AcmValid(has, ACM_STREAM))
        return MMSYSERR_INVALHANDLE;
    if (!AcmWritable(h, sizeof(DWORD)) || !AcmWritable(h, h->cbStruct))
        return MMSYSERR_INVALPARAM;
    if (flags)
        return MMSYSERR_INVALFLAG;
    if (h->cbStruct < sizeof(ACMSTREAMHEADER))
        return MMSYSERR_INVALPARAM;
    if (h->fdwStatus & ~(ACMSTREAMHEADER_STATUSF_DONE | ACMSTREAMHEADER_STATUSF_PREPARED | ACMSTREAMHEADER_STATUSF_INQUEUE))
        return MMSYSERR_INVALFLAG;
    if (h->fdwStatus & ACMSTREAMHEADER_STATUSF_PREPARED)
        return MMSYSERR_NOERROR;
    /* the destination must hold what the whole source converts to. */
    DWORD needed;
    if (!h->cbSrcLength)
        return MMSYSERR_INVALPARAM;
    MMRESULT r = acmStreamSize(has, h->cbSrcLength, &needed, ACM_STREAMSIZEF_SOURCE);
    if (r == ACMERR_NOTPOSSIBLE)
        return r;
    if (r)
        return r;
    if (!AcmReadable(h->pbSrc, h->cbSrcLength) || !AcmWritable(h->pbDst, h->cbDstLength))
        return MMSYSERR_INVALPARAM;
    ACMSTREAM_ *s = (ACMSTREAM_ *)has;
    ACMDRVSTREAMHEADER *d = driver_header(h);
    d->fdwConvert = 0;
    d->padshNext = nullptr;
    d->fdwDriver = 0;
    d->dwDriver = 0;
    d->fdwPrepared = 0;
    d->dwPrepared = 0;
    d->pbPreparedSrc = nullptr;
    d->cbPreparedSrcLength = 0;
    d->pbPreparedDst = nullptr;
    d->cbPreparedDstLength = 0;
    RtlEnterCriticalSection(&s->lock);
    r = stream_message(s, ACMDM_STREAM_PREPARE, (LPARAM)h);
    RtlLeaveCriticalSection(&s->lock);
    if (r == MMSYSERR_NOTSUPPORTED)
        r = MMSYSERR_NOERROR;
    if (r)
        return r;
    h->fdwStatus = (h->fdwStatus & (ACMSTREAMHEADER_STATUSF_DONE | ACMSTREAMHEADER_STATUSF_INQUEUE)) |
                   ACMSTREAMHEADER_STATUSF_PREPARED;
    d->fdwPrepared = 0;
    d->dwPrepared = (DWORD_PTR)has;
    d->pbPreparedSrc = h->pbSrc;
    d->cbPreparedSrcLength = h->cbSrcLength;
    d->pbPreparedDst = h->pbDst;
    d->cbPreparedDstLength = h->cbDstLength;
    InterlockedIncrement((LONG *)&s->prepared);
    return MMSYSERR_NOERROR;
}

MSACMAPI MMRESULT WINAPI acmStreamUnprepareHeader(HACMSTREAM has, LPACMSTREAMHEADER h, DWORD flags)
{
    if (!AcmValid(has, ACM_STREAM))
        return MMSYSERR_INVALHANDLE;
    if (!AcmWritable(h, sizeof(DWORD)) || !AcmWritable(h, h->cbStruct))
        return MMSYSERR_INVALPARAM;
    if (flags)
        return MMSYSERR_INVALFLAG;
    if (h->cbStruct < sizeof(ACMSTREAMHEADER))
        return MMSYSERR_INVALPARAM;
    if (h->fdwStatus & ~(ACMSTREAMHEADER_STATUSF_DONE | ACMSTREAMHEADER_STATUSF_PREPARED | ACMSTREAMHEADER_STATUSF_INQUEUE))
        return MMSYSERR_INVALFLAG;
    if (h->fdwStatus & ACMSTREAMHEADER_STATUSF_INQUEUE)
        return ACMERR_BUSY;
    if (!(h->fdwStatus & ACMSTREAMHEADER_STATUSF_PREPARED))
        return ACMERR_UNPREPARED;
    ACMDRVSTREAMHEADER *d = driver_header(h);
    if (d->dwPrepared != (DWORD_PTR)has)
        return MMSYSERR_INVALHANDLE;
    /* the buffers must be the prepared ones; changed lengths are put back but reported. */
    if (h->pbSrc != d->pbPreparedSrc || h->pbDst != d->pbPreparedDst)
        return MMSYSERR_INVALPARAM;
    BOOL changed = FALSE;
    if (h->cbSrcLength != d->cbPreparedSrcLength) {
        h->cbSrcLength = d->cbPreparedSrcLength;
        changed = TRUE;
    }
    if (h->cbDstLength != d->cbPreparedDstLength) {
        h->cbDstLength = d->cbPreparedDstLength;
        changed = TRUE;
    }
    d->fdwConvert = 0;
    ACMSTREAM_ *s = (ACMSTREAM_ *)has;
    RtlEnterCriticalSection(&s->lock);
    MMRESULT r = stream_message(s, ACMDM_STREAM_UNPREPARE, (LPARAM)h);
    RtlLeaveCriticalSection(&s->lock);
    if (r == MMSYSERR_NOTSUPPORTED)
        r = MMSYSERR_NOERROR;
    if (r)
        return r;
    h->fdwStatus &= ACMSTREAMHEADER_STATUSF_DONE | ACMSTREAMHEADER_STATUSF_INQUEUE;
    d->fdwPrepared = 0;
    d->dwPrepared = 0;
    d->pbPreparedSrc = nullptr;
    d->cbPreparedSrcLength = 0;
    d->pbPreparedDst = nullptr;
    d->cbPreparedDstLength = 0;
    InterlockedDecrement((LONG *)&s->prepared);
    return changed ? MMSYSERR_INVALPARAM : MMSYSERR_NOERROR;
}

MSACMAPI MMRESULT WINAPI acmStreamConvert(HACMSTREAM has, LPACMSTREAMHEADER h, DWORD flags)
{
    if (!AcmValid(has, ACM_STREAM))
        return MMSYSERR_INVALHANDLE;
    if (!AcmWritable(h, sizeof(DWORD)) || !AcmWritable(h, h->cbStruct))
        return MMSYSERR_INVALPARAM;
    if (flags & ~(ACM_STREAMCONVERTF_BLOCKALIGN | ACM_STREAMCONVERTF_START | ACM_STREAMCONVERTF_END))
        return MMSYSERR_INVALFLAG;
    if (h->cbStruct < sizeof(ACMSTREAMHEADER))
        return MMSYSERR_INVALPARAM;
    if (h->fdwStatus & ACMSTREAMHEADER_STATUSF_INQUEUE)
        return ACMERR_BUSY;
    if (!(h->fdwStatus & ACMSTREAMHEADER_STATUSF_PREPARED))
        return ACMERR_UNPREPARED;
    h->cbSrcLengthUsed = 0;
    h->cbDstLengthUsed = 0;
    ACMDRVSTREAMHEADER *d = driver_header(h);
    if (d->dwPrepared != (DWORD_PTR)has)
        return MMSYSERR_INVALHANDLE;
    /* a smaller source is fine, the destination must stay the prepared one. */
    if (h->pbSrc != d->pbPreparedSrc || h->cbSrcLength > d->cbPreparedSrcLength || h->pbDst != d->pbPreparedDst ||
        h->cbDstLength != d->cbPreparedDstLength)
        return MMSYSERR_INVALPARAM;
    ACMSTREAM_ *s = (ACMSTREAM_ *)has;
    d->fdwConvert = flags;
    h->fdwStatus &= ~ACMSTREAMHEADER_STATUSF_DONE;
    d->padshNext = nullptr;
    RtlEnterCriticalSection(&s->lock);
    MMRESULT r = stream_message(s, ACMDM_STREAM_CONVERT, (LPARAM)h);
    RtlLeaveCriticalSection(&s->lock);
    if (!r) {
        if (h->cbSrcLengthUsed > h->cbSrcLength)
            h->cbSrcLengthUsed = h->cbSrcLength;
        if (h->cbDstLengthUsed > h->cbDstLength)
            h->cbDstLengthUsed = h->cbDstLength;
        /* an asynchronous driver sets the done bit itself when it finishes. */
        if (!(s->instance.fdwOpen & ACM_STREAMOPENF_ASYNC))
            h->fdwStatus |= ACMSTREAMHEADER_STATUSF_DONE;
    }
    h->fdwStatus &= ACMSTREAMHEADER_STATUSF_DONE | ACMSTREAMHEADER_STATUSF_PREPARED | ACMSTREAMHEADER_STATUSF_INQUEUE;
    return r;
}
