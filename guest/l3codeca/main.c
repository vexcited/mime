/*
 * l3codeca.acm, the mpeg layer 3 decoder windows ships from fraunhofer:
 * mpeg 1, 2 and 2.5 layer iii to 8 or 16 bit pcm, with stereo mixed to
 * mono and the rate halved or quartered on request. it has no encoder;
 * pcm sources are refused.
 */
#include <mime/win32/winbase.h>
#include <mime/win32/mmreg.h>
#include <acmdrv/acmdrv.h>
#include "mp3.h"

typedef struct STREAM {
    DWORD frame_bytes;  /* the pcm of a frame, which every decoded frame must fill */
    Mp3Decoder decoder;
} STREAM;

int _fltused = 0x9875;

static const DWORD rates[] = {8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000};

static BOOL valid_pcm(const WAVEFORMATEX *f)
{
    return f->wFormatTag == WAVE_FORMAT_PCM && f->nSamplesPerSec && (f->nChannels == 1 || f->nChannels == 2) &&
           (f->wBitsPerSample == 8 || f->wBitsPerSample == 16) &&
           f->nBlockAlign == f->wBitsPerSample / 8 * f->nChannels &&
           f->nAvgBytesPerSec == f->nSamplesPerSec * f->nBlockAlign;
}

/* the rate, rates of bytes, blocks and codec delay are never looked at. */
static BOOL valid_mp3(const WAVEFORMATEX *f)
{
    const MPEGLAYER3WAVEFORMAT *m = (const MPEGLAYER3WAVEFORMAT *)f;
    return f->wFormatTag == WAVE_FORMAT_MPEGLAYER3 && (f->nChannels == 1 || f->nChannels == 2) && f->cbSize == 12 &&
           !f->wBitsPerSample && (m->wID == MPEGLAYER3_ID_MPEG || m->wID == MPEGLAYER3_ID_CONSTANTFRAMESIZE) &&
           (m->fdwFlags & 3) != 3;
}

static DWORD frame_samples(const WAVEFORMATEX *mp3)
{
    return mp3->nSamplesPerSec > 24000 ? 1152 : 576;
}

static LRESULT format_tag_details(ACMFORMATTAGDETAILSW *d, DWORD flags)
{
    DWORD tag;
    switch (flags & 0xF) {
    case ACM_FORMATTAGDETAILSF_INDEX:
        if (d->dwFormatTagIndex >= 2)
            return ACMERR_NOTPOSSIBLE;
        tag = d->dwFormatTagIndex ? WAVE_FORMAT_MPEGLAYER3 : WAVE_FORMAT_PCM;
        break;
    case ACM_FORMATTAGDETAILSF_LARGESTSIZE:
        tag = d->dwFormatTag ? d->dwFormatTag : WAVE_FORMAT_MPEGLAYER3;
        break;
    case ACM_FORMATTAGDETAILSF_FORMATTAG:
        tag = d->dwFormatTag;
        break;
    default:
        return MMSYSERR_NOTSUPPORTED;
    }
    if (tag == WAVE_FORMAT_PCM) {
        d->dwFormatTagIndex = 0;
        d->cbFormatSize = sizeof(PCMWAVEFORMAT);
        d->cStandardFormats = 36;
        d->szFormatTag[0] = 0;
    } else if (tag == WAVE_FORMAT_MPEGLAYER3) {
        d->dwFormatTagIndex = 1;
        d->cbFormatSize = sizeof(MPEGLAYER3WAVEFORMAT);
        d->cStandardFormats = 0;
        lstrcpyW(d->szFormatTag, L"MPEG Layer-3");
    } else {
        return ACMERR_NOTPOSSIBLE;
    }
    d->dwFormatTag = tag;
    d->fdwSupport = ACMDRIVERDETAILS_SUPPORTF_CODEC;
    if (d->cbStruct > sizeof(*d))
        d->cbStruct = sizeof(*d);
    return MMSYSERR_NOERROR;
}

/* decimal digits of a number into text, returning the end. */
static WCHAR *digits(WCHAR *p, DWORD v, int width)
{
    WCHAR tmp[10];
    int n = 0;
    do {
        tmp[n++] = (WCHAR)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n < width)
        tmp[n++] = '0';
    while (n)
        *p++ = tmp[--n];
    return p;
}

static LRESULT format_details(ACMFORMATDETAILSW *d, DWORD flags)
{
    WAVEFORMATEX *f = d->pwfx;
    switch (flags & 0xF) {
    case ACM_FORMATDETAILSF_INDEX: {
        DWORD i = d->dwFormatIndex;
        if (d->dwFormatTag != WAVE_FORMAT_PCM || i >= 36)
            return ACMERR_NOTPOSSIBLE;
        f->wFormatTag = WAVE_FORMAT_PCM;
        f->nChannels = (WORD)((i & 1) + 1);
        f->wBitsPerSample = (i >> 1) & 1 ? 16 : 8;
        f->nSamplesPerSec = rates[i >> 2];
        f->nBlockAlign = (WORD)(f->wBitsPerSample / 8 * f->nChannels);
        f->nAvgBytesPerSec = f->nSamplesPerSec * f->nBlockAlign;
        break;
    }
    case ACM_FORMATDETAILSF_FORMAT:
        if (f->wFormatTag == WAVE_FORMAT_PCM ? !valid_pcm(f)
                                             : f->wFormatTag != WAVE_FORMAT_MPEGLAYER3 || !valid_mp3(f))
            return ACMERR_NOTPOSSIBLE;
        break;
    default:
        return MMSYSERR_NOTSUPPORTED;
    }
    d->fdwSupport = ACMDRIVERDETAILS_SUPPORTF_CODEC;
    if (d->cbStruct > sizeof(*d))
        d->cbStruct = sizeof(*d);
    if (d->dwFormatTag != WAVE_FORMAT_MPEGLAYER3) {
        d->szFormat[0] = 0;
        return MMSYSERR_NOERROR;
    }
    /* "128 kBit/s, 44,100 Hz, Stereo", the numbers in 32 bits as windows has them. */
    WCHAR text[64], *p = text;
    p = digits(p, f->nAvgBytesPerSec * 8 / 1000, 0);
    lstrcpyW(p, L" kBit/s, ");
    p += 9;
    p = digits(p, f->nSamplesPerSec / 1000, 0);
    *p++ = ',';
    p = digits(p, f->nSamplesPerSec % 1000, 3);
    lstrcpyW(p, f->nChannels == 1 ? L" Hz, Mono" : L" Hz, Stereo");
    lstrcpynW(d->szFormat, text, ACMFORMATDETAILS_FORMAT_CHARS);
    return MMSYSERR_NOERROR;
}

static LRESULT suggest(ACMDRVFORMATSUGGEST *s)
{
    DWORD flags = s->fdwSuggest;
    if (flags & 0x00F00000)
        return MMSYSERR_NOTSUPPORTED;
    const WAVEFORMATEX *src = s->pwfxSrc;
    WAVEFORMATEX *dst = s->pwfxDst;
    if (src->wFormatTag == WAVE_FORMAT_PCM) {
        /* there is no encoder, but the destination is written before saying so. */
        if (!valid_pcm(src) || ((flags & ACM_FORMATSUGGESTF_WFORMATTAG) && dst->wFormatTag != WAVE_FORMAT_MPEGLAYER3))
            return ACMERR_NOTPOSSIBLE;
        dst->wFormatTag = WAVE_FORMAT_MPEGLAYER3;
        if (!(flags & ACM_FORMATSUGGESTF_WBITSPERSAMPLE))
            dst->wBitsPerSample = 0;
        return ACMERR_NOTPOSSIBLE;
    }
    if (!valid_mp3(src))
        return ACMERR_NOTPOSSIBLE;
    if (flags & ACM_FORMATSUGGESTF_WFORMATTAG) {
        if (dst->wFormatTag != WAVE_FORMAT_PCM)
            return ACMERR_NOTPOSSIBLE;
    } else {
        dst->wFormatTag = WAVE_FORMAT_PCM;
    }
    if (flags & ACM_FORMATSUGGESTF_NCHANNELS) {
        if (src->nChannels == 1 && dst->nChannels == 2)
            return ACMERR_NOTPOSSIBLE;
    } else {
        dst->nChannels = src->nChannels;
    }
    if (flags & ACM_FORMATSUGGESTF_NSAMPLESPERSEC) {
        DWORD r = dst->nSamplesPerSec;
        if (r != src->nSamplesPerSec && r != src->nSamplesPerSec >> 1 && r != src->nSamplesPerSec >> 2)
            return ACMERR_NOTPOSSIBLE;
    } else {
        dst->nSamplesPerSec = src->nSamplesPerSec;
    }
    if (flags & ACM_FORMATSUGGESTF_WBITSPERSAMPLE) {
        if (dst->wBitsPerSample != 8 && dst->wBitsPerSample != 16)
            return ACMERR_NOTPOSSIBLE;
    } else {
        dst->wBitsPerSample = 16;
    }
    dst->nBlockAlign = (WORD)AcmdPcmBlock(dst);
    dst->nAvgBytesPerSec = dst->nSamplesPerSec * dst->nBlockAlign;
    return MMSYSERR_NOERROR;
}

static LRESULT stream_open(ACMDRVSTREAMINSTANCE *in)
{
    const WAVEFORMATEX *src = in->pwfxSrc, *dst = in->pwfxDst;
    if (src->wFormatTag != WAVE_FORMAT_MPEGLAYER3 || !valid_mp3(src) || !valid_pcm(dst))
        return ACMERR_NOTPOSSIBLE;
    if (src->nChannels == 1 && dst->nChannels == 2)
        return ACMERR_NOTPOSSIBLE;
    DWORD rate = src->nSamplesPerSec;
    if (dst->nSamplesPerSec != rate && dst->nSamplesPerSec != rate >> 1 && dst->nSamplesPerSec != rate >> 2)
        return ACMERR_NOTPOSSIBLE;
    DWORD ratio = rate / dst->nSamplesPerSec;
    if (in->fdwOpen & ACM_STREAMOPENF_QUERY)
        return MMSYSERR_NOERROR;
    STREAM *s = VirtualAlloc(nullptr, sizeof(*s), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!s)
        return MMSYSERR_NOMEM;
    s->frame_bytes = frame_samples(src) / ratio * AcmdPcmBlock(dst);
    Mp3Init(&s->decoder, (int)(ratio >> 1), dst->wBitsPerSample == 8, src->nChannels == 2 && dst->nChannels == 1);
    in->fdwDriver = 0;
    in->dwDriver = (DWORD_PTR)s;
    return MMSYSERR_NOERROR;
}

/* sizes in unsigned 32 bits with truncating division and no overflow checks, as windows has them. */
static LRESULT stream_size(const ACMDRVSTREAMINSTANCE *in, ACMDRVSTREAMSIZE *s)
{
    const WAVEFORMATEX *src = in->pwfxSrc, *dst = in->pwfxDst;
    if (!dst->nSamplesPerSec)
        return ACMERR_NOTPOSSIBLE;
    DWORD f = frame_samples(src), ratio = src->nSamplesPerSec / dst->nSamplesPerSec, block = AcmdPcmBlock(dst);
    switch (s->fdwSize & 0xF) {
    case ACM_STREAMSIZEF_SOURCE: {
        s->cbDstLength = 0;
        DWORD bytes = src->nSamplesPerSec ? src->nAvgBytesPerSec * f / src->nSamplesPerSec : 0;
        if (!bytes || !ratio)
            return ACMERR_NOTPOSSIBLE;
        DWORD frames = s->cbSrcLength / bytes + (s->cbSrcLength % bytes != 0);
        s->cbDstLength = f * frames / ratio * block;
        return s->cbDstLength ? MMSYSERR_NOERROR : ACMERR_NOTPOSSIBLE;
    }
    case ACM_STREAMSIZEF_DESTINATION:
        s->cbSrcLength = 0;
        if (!ratio || !src->nSamplesPerSec)
            return ACMERR_NOTPOSSIBLE;
        s->cbSrcLength = src->nAvgBytesPerSec * f / src->nSamplesPerSec * (s->cbDstLength / block / (f / ratio));
        return s->cbSrcLength ? MMSYSERR_NOERROR : ACMERR_NOTPOSSIBLE;
    default:
        return MMSYSERR_NOTSUPPORTED;
    }
}

/* the frame length of the bitrate index 1 at a rate, those of windows' silent frames. */
static DWORD silent_frame(DWORD rate, BYTE *frame, WORD channels)
{
    static const DWORD versions[9] = {0, 0, 0, 2, 2, 2, 3, 3, 3};
    static const DWORD indices[9] = {2, 0, 1, 2, 0, 1, 2, 0, 1};
    static const WORD lengths[9] = {72, 52, 48, 36, 26, 24, 144, 104, 96};
    for (int i = 0; i < 9; i++)
        if (rates[i] == rate) {
            DWORD h = 0xFFE21000 | versions[i] << 19 | indices[i] << 10 | (channels != 2 ? 0xC0 : 0);
            memset(frame, 0, lengths[i]);
            frame[0] = (BYTE)(h >> 24);
            frame[1] = (BYTE)(h >> 16);
            frame[2] = (BYTE)(h >> 8);
            frame[3] = (BYTE)h;
            frame[4] = 0xFE;
            frame[5] = 0xCA;
            return lengths[i];
        }
    return 0;
}

/*
 * moves source into the ring. sources of whole blocks go block by block,
 * and a block starting "ACM\0crc\0" stands for silence, for which the
 * block's frame count of silent frames is fed instead.
 */
static DWORD fill(STREAM *s, const MPEGLAYER3WAVEFORMAT *src, ACMDRVSTREAMHEADER *h)
{
    DWORD block = src->nBlockSize, copied = 0;
    if (block <= 1 || h->cbSrcLength % block) {
        DWORD n = Mp3Feed(&s->decoder, h->pbSrc + h->cbSrcLengthUsed, h->cbSrcLength - h->cbSrcLengthUsed);
        h->cbSrcLengthUsed += n;
        return n;
    }
    static const BYTE marker[8] = {'A', 'C', 'M', 0, 'c', 'r', 'c', 0};
    while (h->cbSrcLength - h->cbSrcLengthUsed >= block && Mp3Space(&s->decoder) >= block) {
        const BYTE *p = h->pbSrc + h->cbSrcLengthUsed;
        if (h->cbSrcLength - h->cbSrcLengthUsed < 8 || !memcmp(p, marker, 8)) {
            BYTE frame[144];
            DWORD length = silent_frame(src->wfx.nSamplesPerSec, frame, src->wfx.nChannels);
            for (DWORD i = 0; length && i < src->nFramesPerBlock; i++)
                Mp3Feed(&s->decoder, frame, length);
        } else {
            Mp3Feed(&s->decoder, p, block);
        }
        h->cbSrcLengthUsed += block;
        copied += block;
    }
    return copied;
}

/*
 * the source goes into the decoder's ring as far as it holds, then whole
 * frames come out while the destination has room. the end flag flushes
 * nothing, and the start flag alone resets the decoder.
 */
static LRESULT convert(STREAM *s, const ACMDRVSTREAMINSTANCE *in, ACMDRVSTREAMHEADER *h)
{
    if (h->fdwConvert & ACM_STREAMCONVERTF_START)
        Mp3Reset(&s->decoder);
    h->cbSrcLengthUsed = 0;
    h->cbDstLengthUsed = 0;
    const MPEGLAYER3WAVEFORMAT *src = (const MPEGLAYER3WAVEFORMAT *)in->pwfxSrc;
    DWORD fb = s->frame_bytes;
    for (;;) {
        DWORD copied = fill(s, src, h);
        int status = MP3_NEED_DATA;
        while (h->cbDstLength - h->cbDstLengthUsed >= fb) {
            DWORD written;
            status = Mp3Decode(&s->decoder, h->pbDst + h->cbDstLengthUsed, h->cbDstLength - h->cbDstLengthUsed,
                               &written);
            if (status == MP3_FATAL || (status == MP3_OK && written && written != fb))
                return ACMERR_NOTPOSSIBLE;
            if (status != MP3_OK)
                break;
            h->cbDstLengthUsed += written;
        }
        if (h->cbDstLength - h->cbDstLengthUsed < fb || (!copied && status != MP3_SYNC_LOST))
            return MMSYSERR_NOERROR;
    }
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        Mp3Setup();
    }
    return TRUE;
}

DECLSPEC_EXPORT LRESULT WINAPI DriverProc(DWORD_PTR id, HDRVR driver, UINT message, LPARAM param1, LPARAM param2)
{
    switch (message) {
    case DRV_OPEN: {
        ACMDRVOPENDESCW *desc = (ACMDRVOPENDESCW *)param2;
        if (!AcmdOpenAllowed(desc))
            return 0;
        void *instance = LocalAlloc(LPTR, 16);
        if (!instance) {
            if (desc)
                desc->dwError = MMSYSERR_NOMEM;
            return 0;
        }
        if (desc)
            desc->dwError = MMSYSERR_NOERROR;
        return (LRESULT)instance;
    }
    case DRV_CLOSE:
        if (id)
            LocalFree((void *)id);
        return 1;
    case DRV_QUERYCONFIGURE:
    case DRV_CONFIGURE:
        return 0;
    case ACMDM_DRIVER_DETAILS: {
        LRESULT r = AcmdDetails((ACMDRIVERDETAILSW *)param1, 9, 0x01090191, ACMDRIVERDETAILS_SUPPORTF_CODEC, 2,
                                L"MPEG Layer-3 Codec ", L"Fraunhofer IIS MPEG Layer-3 Codec (decode only)",
                                L"Copyright © 1996-1999 Fraunhofer Institut Integrierte Schaltungen IIS",
                                L"decoder only version");
        ACMDRIVERDETAILSW *d = (ACMDRIVERDETAILSW *)param1;
        if (d->cbStruct > offsetof(ACMDRIVERDETAILSW, wMid))
            d->wMid = 172;
        return r;
    }
    case ACMDM_DRIVER_ABOUT:
        /* windows shows a dialog of the codec's version; the query by -1 says there is one. */
        if (param1 != -1)
            MessageBoxW((HWND)param1,
                        L"MPEG Layer-3 Audio Codec\n1.9 (Build 401) (decode only)\n\nCopyright © 1996-1999 "
                        L"Fraunhofer-Gesellschaft IIS",
                        L"MPEG Layer-3 Codec", MB_OK);
        return 0;
    case ACMDM_FORMATTAG_DETAILS:
        return format_tag_details((ACMFORMATTAGDETAILSW *)param1, (DWORD)param2);
    case ACMDM_FORMAT_DETAILS:
        return format_details((ACMFORMATDETAILSW *)param1, (DWORD)param2);
    case ACMDM_FORMAT_SUGGEST:
        return suggest((ACMDRVFORMATSUGGEST *)param1);
    case ACMDM_STREAM_OPEN:
        return stream_open((ACMDRVSTREAMINSTANCE *)param1);
    case ACMDM_STREAM_CLOSE: {
        void *s = (void *)((ACMDRVSTREAMINSTANCE *)param1)->dwDriver;
        if (s)
            VirtualFree(s, 0, MEM_RELEASE);
        return MMSYSERR_NOERROR;
    }
    case ACMDM_STREAM_SIZE:
        return stream_size((ACMDRVSTREAMINSTANCE *)param1, (ACMDRVSTREAMSIZE *)param2);
    case ACMDM_STREAM_CONVERT:
        return convert((STREAM *)((ACMDRVSTREAMINSTANCE *)param1)->dwDriver, (ACMDRVSTREAMINSTANCE *)param1,
                       (ACMDRVSTREAMHEADER *)param2);
    default:
        return AcmdDefault(message);
    }
}
