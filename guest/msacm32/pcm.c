/*
 * the internal pcm converter: 8 and 16 bit, mono and stereo, any rate.
 * like windows' converter, samples are widened to 16 bits, channels summed
 * and clamped, 8-bit output takes the high byte, and the rate changes by
 * repeating or dropping the nearest sample.
 */
#include "msacm32_private.h"

static const DWORD standard_rates[] = {8000, 11025, 22050, 44100};

static BOOL valid_format(const WAVEFORMATEX *f)
{
    if (f->wFormatTag != WAVE_FORMAT_PCM || (f->nChannels != 1 && f->nChannels != 2))
        return FALSE;
    if (f->wBitsPerSample != 8 && f->wBitsPerSample != 16)
        return FALSE;
    if (f->nBlockAlign != (f->wBitsPerSample >> 3) << (f->nChannels >> 1))
        return FALSE;
    if (!f->nSamplesPerSec || f->nSamplesPerSec > 0x3FFFFFFF)
        return FALSE;
    return f->nAvgBytesPerSec == f->nBlockAlign * f->nSamplesPerSec;
}

static DWORD block(const WAVEFORMATEX *f)
{
    return (DWORD)(f->wBitsPerSample >> 3) << (f->nChannels >> 1);
}

static LRESULT driver_details(ACMDRIVERDETAILSW *out)
{
    ACMDRIVERDETAILSW d = {};
    d.cbStruct = sizeof(d);
    d.fccType = ACMDRIVERDETAILS_FCCTYPE_AUDIOCODEC;
    d.fccComp = ACMDRIVERDETAILS_FCCCOMP_UNDEFINED;
    d.wMid = 1;
    d.wPid = 38;
    d.vdwACM = 0x03320000;
    d.vdwDriver = 0x05000000;
    d.fdwSupport = ACMDRIVERDETAILS_SUPPORTF_CONVERTER;
    d.cFormatTags = 1;
    lstrcpyW(d.szShortName, L"MS-PCM");
    lstrcpyW(d.szLongName, L"Microsoft PCM Converter");
    lstrcpyW(d.szCopyright, L"Copyright (C) 1992-1996 Microsoft Corporation");
    lstrcpyW(d.szFeatures, L"Converts frequency and bits per sample of PCM audio data.");
    DWORD size = out->cbStruct < sizeof(d) ? out->cbStruct : sizeof(d);
    memcpy(out, &d, size);
    out->cbStruct = size;
    return MMSYSERR_NOERROR;
}

static LRESULT format_tag_details(ACMFORMATTAGDETAILSW *d, DWORD flags)
{
    switch (flags & ACM_FORMATTAGDETAILSF_QUERYMASK) {
    case ACM_FORMATTAGDETAILSF_INDEX:
        if (d->dwFormatTagIndex)
            return ACMERR_NOTPOSSIBLE;
        break;
    case ACM_FORMATTAGDETAILSF_FORMATTAG:
        if (d->dwFormatTag != WAVE_FORMAT_PCM)
            return ACMERR_NOTPOSSIBLE;
        break;
    case ACM_FORMATTAGDETAILSF_LARGESTSIZE:
        if (d->dwFormatTag != WAVE_FORMAT_UNKNOWN && d->dwFormatTag != WAVE_FORMAT_PCM)
            return ACMERR_NOTPOSSIBLE;
        break;
    default:
        return MMSYSERR_NOTSUPPORTED;
    }
    d->dwFormatTagIndex = 0;
    d->dwFormatTag = WAVE_FORMAT_PCM;
    d->cbFormatSize = sizeof(PCMWAVEFORMAT);
    d->fdwSupport = ACMDRIVERDETAILS_SUPPORTF_CONVERTER;
    d->cStandardFormats = 16;
    d->szFormatTag[0] = 0;
    if (d->cbStruct > sizeof(*d))
        d->cbStruct = sizeof(*d);
    return MMSYSERR_NOERROR;
}

static LRESULT format_details(ACMFORMATDETAILSW *d, DWORD flags)
{
    WAVEFORMATEX *f = d->pwfx;
    switch (flags & ACM_FORMATDETAILSF_QUERYMASK) {
    case ACM_FORMATDETAILSF_INDEX:
        if (d->dwFormatTag != WAVE_FORMAT_PCM || d->dwFormatIndex >= 16)
            return ACMERR_NOTPOSSIBLE;
        f->wFormatTag = WAVE_FORMAT_PCM;
        f->nChannels = (WORD)((d->dwFormatIndex & 1) + 1);
        f->wBitsPerSample = d->dwFormatIndex & 2 ? 16 : 8;
        f->nSamplesPerSec = standard_rates[d->dwFormatIndex >> 2];
        f->nBlockAlign = (WORD)block(f);
        f->nAvgBytesPerSec = f->nBlockAlign * f->nSamplesPerSec;
        break;
    case ACM_FORMATDETAILSF_FORMAT:
        if (!valid_format(f))
            return ACMERR_NOTPOSSIBLE;
        break;
    default:
        return MMSYSERR_NOTSUPPORTED;
    }
    d->fdwSupport = ACMDRIVERDETAILS_SUPPORTF_CONVERTER;
    d->szFormat[0] = 0;
    if (d->cbStruct > sizeof(*d))
        d->cbStruct = sizeof(*d);
    return MMSYSERR_NOERROR;
}

/* the source with the fields the caller fixes taken from the destination. */
static LRESULT suggest(ACMDRVFORMATSUGGEST *s)
{
    DWORD flags = s->fdwSuggest;
    if (flags & 0x00F00000)
        return MMSYSERR_NOTSUPPORTED;
    const WAVEFORMATEX *src = s->pwfxSrc;
    WAVEFORMATEX *dst = s->pwfxDst;
    if (!valid_format(src))
        return ACMERR_NOTPOSSIBLE;
    if ((flags & ACM_FORMATSUGGESTF_WFORMATTAG) && dst->wFormatTag != WAVE_FORMAT_PCM)
        return ACMERR_NOTPOSSIBLE;
    if ((flags & ACM_FORMATSUGGESTF_NCHANNELS) && dst->nChannels != 1 && dst->nChannels != 2)
        return ACMERR_NOTPOSSIBLE;
    if ((flags & ACM_FORMATSUGGESTF_WBITSPERSAMPLE) && dst->wBitsPerSample != 8 && dst->wBitsPerSample != 16)
        return ACMERR_NOTPOSSIBLE;
    if ((flags & ACM_FORMATSUGGESTF_NSAMPLESPERSEC) && (!dst->nSamplesPerSec || dst->nSamplesPerSec > 0x3FFFFFFF))
        return ACMERR_NOTPOSSIBLE;
    dst->wFormatTag = WAVE_FORMAT_PCM;
    if (!(flags & ACM_FORMATSUGGESTF_NCHANNELS))
        dst->nChannels = src->nChannels;
    if (!(flags & ACM_FORMATSUGGESTF_NSAMPLESPERSEC))
        dst->nSamplesPerSec = src->nSamplesPerSec;
    if (!(flags & ACM_FORMATSUGGESTF_WBITSPERSAMPLE))
        dst->wBitsPerSample = src->wBitsPerSample;
    dst->nBlockAlign = (WORD)block(dst);
    dst->nAvgBytesPerSec = dst->nBlockAlign * dst->nSamplesPerSec;
    return MMSYSERR_NOERROR;
}

/* the bytes the other side needs: whole frames, rounded up at the other rate. */
static LRESULT stream_size(const ACMDRVSTREAMINSTANCE *in, ACMDRVSTREAMSIZE *size)
{
    const WAVEFORMATEX *src = in->pwfxSrc, *dst = in->pwfxDst;
    BOOL reverse = (size->fdwSize & ACM_STREAMSIZEF_QUERYMASK) == ACM_STREAMSIZEF_DESTINATION;
    if ((size->fdwSize & ACM_STREAMSIZEF_QUERYMASK) > ACM_STREAMSIZEF_DESTINATION)
        return MMSYSERR_NOTSUPPORTED;
    const WAVEFORMATEX *from = reverse ? dst : src, *to = reverse ? src : dst;
    DWORD bytes = reverse ? size->cbDstLength : size->cbSrcLength;
    DWORD frames = bytes / block(from);
    if (!frames)
        return ACMERR_NOTPOSSIBLE;
    if (to->nAvgBytesPerSec < from->nAvgBytesPerSec ||
        bytes / from->nAvgBytesPerSec <= 0xFFFFFFFF / to->nAvgBytesPerSec - to->nBlockAlign) {
        DWORD out = (DWORD)(((ULONG64)frames * to->nSamplesPerSec + from->nSamplesPerSec - 1) / from->nSamplesPerSec) *
                    block(to);
        if (out) {
            if (reverse)
                size->cbSrcLength = out;
            else
                size->cbDstLength = out;
            return MMSYSERR_NOERROR;
        }
    }
    return ACMERR_NOTPOSSIBLE;
}

/* reads a frame as 16-bit left and right samples. */
static const BYTE *read_frame(const BYTE *p, const WAVEFORMATEX *f, int *l, int *r)
{
    if (f->wBitsPerSample == 8) {
        *l = (int)(SHORT)((p[0] ^ 0x80) << 8);
        *r = f->nChannels == 2 ? (int)(SHORT)((p[1] ^ 0x80) << 8) : *l;
    } else {
        *l = ((const SHORT *)p)[0];
        *r = f->nChannels == 2 ? ((const SHORT *)p)[1] : *l;
    }
    return p + block(f);
}

static int clamp16(int v)
{
    return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
}

/* writes the frame's bytes in the destination's layout. */
static void make_frame(BYTE *out, const WAVEFORMATEX *src, const WAVEFORMATEX *dst, int l, int r)
{
    if (dst->nChannels == 1)
        l = src->nChannels == 2 ? clamp16(l + r) : l;
    if (dst->wBitsPerSample == 8) {
        out[0] = (BYTE)(((unsigned)l >> 8) ^ 0x80);
        if (dst->nChannels == 2)
            out[1] = (BYTE)(((unsigned)r >> 8) ^ 0x80);
    } else {
        ((SHORT *)out)[0] = (SHORT)l;
        if (dst->nChannels == 2)
            ((SHORT *)out)[1] = (SHORT)r;
    }
}

static BYTE *write_frame(BYTE *p, BYTE *end, const BYTE *frame, DWORD size)
{
    if (p >= end || (DWORD)(end - p) < size)
        return end;
    memcpy(p, frame, size);
    return p + size;
}

/* returns the bytes written. with fill set, the last frame fills the rest of the destination. */
static DWORD convert(const WAVEFORMATEX *src, const BYTE *in, const WAVEFORMATEX *dst, BYTE *out, DWORD frames,
                     BOOL fill, BYTE *end)
{
    if (!frames)
        return 0;
    BYTE *start = out, frame[4];
    DWORD size = block(dst), from = src->nSamplesPerSec, to = dst->nSamplesPerSec;
    DWORD count = fill ? frames - 1 : frames;
    int l = 0, r = 0;
    if (from > to) {
        int error = (int)(from >> 1);
        while (count--) {
            in = read_frame(in, src, &l, &r);
            error -= (int)to;
            if (error < 0) {
                make_frame(frame, src, dst, l, r);
                out = write_frame(out, end, frame, size);
                error += (int)from;
            }
        }
    } else {
        int error = (int)(to >> 1);
        while (count--) {
            in = read_frame(in, src, &l, &r);
            make_frame(frame, src, dst, l, r);
            do {
                error -= (int)from;
                out = write_frame(out, end, frame, size);
            } while (error >= 0);
            error += (int)to;
        }
    }
    if (fill) {
        read_frame(in, src, &l, &r);
        make_frame(frame, src, dst, l, r);
        while (out < end)
            out = write_frame(out, end, frame, size);
    }
    return (DWORD)(out - start);
}

/* converts as many whole source frames as the destination holds, all of them when it holds them exactly. */
static LRESULT stream_convert(const ACMDRVSTREAMINSTANCE *in, ACMDRVSTREAMHEADER *h)
{
    const WAVEFORMATEX *src = in->pwfxSrc, *dst = in->pwfxDst;
    DWORD src_block = block(src), dst_block = block(dst);
    DWORD frames = h->cbSrcLength / src_block, room = h->cbDstLength;
    ULONG64 from = src->nSamplesPerSec, to = dst->nSamplesPerSec;
    BOOL fill = FALSE;
    if (room % dst_block == 0) {
        ULONG64 scaled = (ULONG64)(room / dst_block) * from;
        if (frames == (DWORD)((scaled + to - 1) / to) && frames > (DWORD)(scaled / to))
            fill = TRUE;
    }
    if (!fill)
        while (room < dst_block * (DWORD)(((ULONG64)frames * to + from - 1) / from))
            frames--;
    h->cbSrcLengthUsed = frames * src_block;
    h->cbDstLengthUsed = convert(src, h->pbSrc, dst, h->pbDst, frames, fill, h->pbDst + room);
    return MMSYSERR_NOERROR;
}

LRESULT CALLBACK AcmPcmDriverProc(DWORD_PTR id, HDRVR driver, UINT message, LPARAM param1, LPARAM param2)
{
    switch (message) {
    case DRV_LOAD:
    case DRV_FREE:
        return 1;
    case DRV_OPEN: {
        /* the open without a description is the driver id's, other opens must come from the acm. */
        ACMDRVOPENDESCW *desc = (ACMDRVOPENDESCW *)param2;
        if (!desc)
            return 1;
        if (desc->fccType != ACMDRIVERDETAILS_FCCTYPE_AUDIOCODEC)
            return 0;
        desc->dwError = MMSYSERR_NOERROR;
        return 2;
    }
    case DRV_CLOSE:
        return 1;
    case DRV_CONFIGURE:
    case DRV_QUERYCONFIGURE:
        return 0;
    case DRV_INSTALL:
    case DRV_REMOVE:
        return DRVCNF_RESTART;
    case ACMDM_DRIVER_DETAILS:
        return driver_details((ACMDRIVERDETAILSW *)param1);
    case ACMDM_FORMATTAG_DETAILS:
        return format_tag_details((ACMFORMATTAGDETAILSW *)param1, (DWORD)param2);
    case ACMDM_FORMAT_DETAILS:
        return format_details((ACMFORMATDETAILSW *)param1, (DWORD)param2);
    case ACMDM_FORMAT_SUGGEST:
        return suggest((ACMDRVFORMATSUGGEST *)param1);
    case ACMDM_STREAM_OPEN: {
        ACMDRVSTREAMINSTANCE *in = (ACMDRVSTREAMINSTANCE *)param1;
        return valid_format(in->pwfxSrc) && valid_format(in->pwfxDst) ? MMSYSERR_NOERROR : ACMERR_NOTPOSSIBLE;
    }
    case ACMDM_STREAM_CLOSE:
        return MMSYSERR_NOERROR;
    case ACMDM_STREAM_SIZE:
        return stream_size((ACMDRVSTREAMINSTANCE *)param1, (ACMDRVSTREAMSIZE *)param2);
    case ACMDM_STREAM_CONVERT:
        return stream_convert((ACMDRVSTREAMINSTANCE *)param1, (ACMDRVSTREAMHEADER *)param2);
    default:
        return message >= DRV_USER ? MMSYSERR_NOTSUPPORTED : 0;
    }
}
