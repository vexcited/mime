/*
 * msg711.acm, the CCITT G.711 codec: 16-bit pcm to and from a-law and u-law, and a-law to and from u-law, one byte
 * per sample, using the recommendation's tables.
 */
#include <mime/win32/winbase.h>
#include <acmdrv/acmdrv.h>

#define WAVE_FORMAT_ALAW 0x0006
#define WAVE_FORMAT_MULAW 0x0007

static const DWORD rates[] = {8000, 11025, 16000, 22050, 44100};
static const DWORD index_to_tag[] = {WAVE_FORMAT_PCM, WAVE_FORMAT_ALAW, WAVE_FORMAT_MULAW};

/* G.711 tables 3 and 4: a-law to u-law and back, for positive codes after the even bit inversion. */
static const BYTE a2u[128] = {
    1,  3,  5,  7,  9,  11, 13, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,  30,  31,  32,  32,
    33, 33, 34, 34, 35, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 48, 49, 49,  50,  51,  52,  53,
    54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74,  75,  76,  77,  78,
    79, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99,  100, 101, 102, 103,
    104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125,
    126, 127};
static const BYTE u2a[128] = {
    1,  1,  2,  2,  3,  3,  4,  4,  5,  5,  6,  6,  7,  7,  8,  8,  9,  10, 11, 12, 13, 14,  15,  16,  17,  18,
    19, 20, 21, 22, 23, 24, 25, 27, 29, 31, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 46,  48,  49,  50,
    51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74,  75,  76,  77,
    78, 79, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104,
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126,
    127, 128};

static SHORT alaw_to_pcm(BYTE a)
{
    a ^= 0x55;
    int value = (a & 0x0F) << 4, segment = (a & 0x70) >> 4;
    if (segment == 0)
        value += 8;
    else
        value = (value + 0x108) << (segment - 1);
    return (SHORT)(a & 0x80 ? value : -value);
}

static SHORT ulaw_to_pcm(BYTE u)
{
    u = (BYTE)~u;
    int value = (((u & 0x0F) << 3) + 0x84) << ((u & 0x70) >> 4);
    return (SHORT)(u & 0x80 ? 0x84 - value : value - 0x84);
}

/* the magnitude and sign bit of a sample, with -32768 taken as 32767. */
static unsigned magnitude(SHORT sample, BYTE *sign)
{
    if (sample >= 0) {
        *sign = 0x80;
        return (unsigned)sample;
    }
    *sign = 0;
    return sample == -32768 ? 0x7FFF : (unsigned)-sample;
}

static BYTE pcm_to_alaw(SHORT sample)
{
    BYTE sign, code;
    unsigned v = magnitude(sample, &sign);
    if (v < 0x200)
        code = (BYTE)((v >> 4) & 0x1F);
    else {
        int segment = 2;
        while (segment < 7 && v >= (0x400u << (segment - 2)))
            segment++;
        code = (BYTE)(((v >> (segment + 3)) & 0x0F) | (segment << 4));
    }
    return (BYTE)((code | sign) ^ 0x55);
}

static BYTE pcm_to_ulaw(SHORT sample)
{
    BYTE sign;
    unsigned v = magnitude(sample, &sign) >> 2;
    if (v >= 0x1FDF)
        return sign;
    if (v < 0x1F)
        return (BYTE)(sign | (127 - (v >> 1)));
    /* segment s starts at 32 << s less a bias of 33, and its steps double. */
    unsigned segment = 1;
    while (v >= (32u << segment) + (32u << segment) - 33 && segment < 7)
        segment++;
    unsigned start = (32u << (segment - 1)) * 2 - 33, base = 127 - 16 * segment;
    return (BYTE)(sign | (base - (v - start) / (2u << segment)));
}

static BYTE alaw_to_ulaw(BYTE a)
{
    return a & 0x80 ? (BYTE)(0xFF ^ a2u[a ^ 0xD5]) : (BYTE)(0x7F ^ a2u[a ^ 0x55]);
}

static BYTE ulaw_to_alaw(BYTE u)
{
    return u & 0x80 ? (BYTE)(0xD5 ^ (u2a[0xFF ^ u] - 1)) : (BYTE)(0x55 ^ (u2a[0x7F ^ u] - 1));
}

static BOOL valid_pcm(const WAVEFORMATEX *f)
{
    return f && f->wFormatTag == WAVE_FORMAT_PCM && (f->nChannels == 1 || f->nChannels == 2) &&
           f->wBitsPerSample == 16 && f->nBlockAlign == 2u << (f->nChannels >> 1) &&
           f->nSamplesPerSec * f->nBlockAlign == f->nAvgBytesPerSec;
}

static BOOL valid_g711(const WAVEFORMATEX *f)
{
    return f && (f->wFormatTag == WAVE_FORMAT_ALAW || f->wFormatTag == WAVE_FORMAT_MULAW) &&
           (f->nChannels == 1 || f->nChannels == 2) && f->nBlockAlign % f->nChannels == 0 &&
           f->nSamplesPerSec * f->nChannels == f->nAvgBytesPerSec && f->wBitsPerSample == 8 && !f->cbSize;
}

static LRESULT format_tag_details(ACMFORMATTAGDETAILSW *d, DWORD flags)
{
    DWORD tag;
    switch (flags & ACM_FORMATTAGDETAILSF_QUERYMASK) {
    case ACM_FORMATTAGDETAILSF_INDEX:
        if (d->dwFormatTagIndex >= 3)
            return ACMERR_NOTPOSSIBLE;
        tag = index_to_tag[d->dwFormatTagIndex];
        break;
    case ACM_FORMATTAGDETAILSF_LARGESTSIZE:
        tag = d->dwFormatTag ? d->dwFormatTag : WAVE_FORMAT_ALAW;
        if (tag != WAVE_FORMAT_PCM && tag != WAVE_FORMAT_ALAW && tag != WAVE_FORMAT_MULAW)
            return ACMERR_NOTPOSSIBLE;
        break;
    case ACM_FORMATTAGDETAILSF_FORMATTAG:
        tag = d->dwFormatTag;
        if (tag != WAVE_FORMAT_PCM && tag != WAVE_FORMAT_ALAW && tag != WAVE_FORMAT_MULAW)
            return ACMERR_NOTPOSSIBLE;
        break;
    default:
        return MMSYSERR_NOTSUPPORTED;
    }
    d->fdwSupport = ACMDRIVERDETAILS_SUPPORTF_CODEC;
    d->cStandardFormats = 10;
    if (tag == WAVE_FORMAT_PCM) {
        d->dwFormatTagIndex = 0;
        d->dwFormatTag = WAVE_FORMAT_PCM;
        d->cbFormatSize = sizeof(PCMWAVEFORMAT);
        d->szFormatTag[0] = 0;
    } else {
        d->dwFormatTagIndex = tag == WAVE_FORMAT_ALAW ? 1 : 2;
        d->dwFormatTag = tag;
        d->cbFormatSize = sizeof(WAVEFORMATEX);
        lstrcpyW(d->szFormatTag, tag == WAVE_FORMAT_ALAW ? L"CCITT A-Law" : L"CCITT u-Law");
    }
    if (d->cbStruct > sizeof(*d))
        d->cbStruct = sizeof(*d);
    return MMSYSERR_NOERROR;
}

static LRESULT format_details(ACMFORMATDETAILSW *d, DWORD flags)
{
    WAVEFORMATEX *f = d->pwfx;
    switch (flags & ACM_FORMATDETAILSF_QUERYMASK) {
    case ACM_FORMATDETAILSF_INDEX: {
        DWORD i = d->dwFormatIndex;
        if (i >= 10 || (d->dwFormatTag != WAVE_FORMAT_PCM && d->dwFormatTag != WAVE_FORMAT_ALAW &&
                        d->dwFormatTag != WAVE_FORMAT_MULAW))
            return ACMERR_NOTPOSSIBLE;
        f->wFormatTag = (WORD)d->dwFormatTag;
        f->nChannels = (WORD)((i & 1) + 1);
        f->nSamplesPerSec = rates[i >> 1];
        if (d->dwFormatTag == WAVE_FORMAT_PCM) {
            f->wBitsPerSample = 16;
            f->nBlockAlign = (WORD)(2 << (f->nChannels >> 1));
        } else {
            f->wBitsPerSample = 8;
            f->cbSize = 0;
            f->nBlockAlign = f->nChannels;
        }
        f->nAvgBytesPerSec = f->nSamplesPerSec * f->nBlockAlign;
        break;
    }
    case ACM_FORMATDETAILSF_FORMAT:
        break;
    default:
        return MMSYSERR_NOTSUPPORTED;
    }
    if (f->wFormatTag == WAVE_FORMAT_PCM ? !valid_pcm(f) : !valid_g711(f))
        return ACMERR_NOTPOSSIBLE;
    d->fdwSupport = ACMDRIVERDETAILS_SUPPORTF_CODEC;
    d->szFormat[0] = 0;
    if (d->cbStruct > sizeof(*d))
        d->cbStruct = sizeof(*d);
    return MMSYSERR_NOERROR;
}

/* suggests the other side: pcm for G.711, a-law for pcm, keeping the fields the caller fixed. */
static LRESULT suggest(ACMDRVFORMATSUGGEST *s)
{
    DWORD flags = s->fdwSuggest;
    if (flags & 0x00F00000)
        return MMSYSERR_NOTSUPPORTED;
    const WAVEFORMATEX *src = s->pwfxSrc;
    WAVEFORMATEX *dst = s->pwfxDst;
    if (src->wFormatTag == WAVE_FORMAT_PCM) {
        if (!valid_pcm(src))
            return ACMERR_NOTPOSSIBLE;
        if (flags & ACM_FORMATSUGGESTF_WFORMATTAG) {
            if (dst->wFormatTag != WAVE_FORMAT_ALAW && dst->wFormatTag != WAVE_FORMAT_MULAW)
                return ACMERR_NOTPOSSIBLE;
        } else {
            dst->wFormatTag = WAVE_FORMAT_ALAW;
        }
        if (flags & ACM_FORMATSUGGESTF_NCHANNELS) {
            if (dst->nChannels != src->nChannels)
                return ACMERR_NOTPOSSIBLE;
        } else {
            dst->nChannels = src->nChannels;
        }
        if (flags & ACM_FORMATSUGGESTF_NSAMPLESPERSEC) {
            if (dst->nSamplesPerSec != src->nSamplesPerSec)
                return ACMERR_NOTPOSSIBLE;
        } else {
            dst->nSamplesPerSec = src->nSamplesPerSec;
        }
        if (flags & ACM_FORMATSUGGESTF_WBITSPERSAMPLE) {
            if (dst->wBitsPerSample != 8)
                return ACMERR_NOTPOSSIBLE;
        } else {
            dst->wBitsPerSample = 8;
        }
        dst->nBlockAlign = dst->nChannels;
        dst->cbSize = 0;
        dst->nAvgBytesPerSec = dst->nSamplesPerSec * dst->nBlockAlign;
        return MMSYSERR_NOERROR;
    }
    if (!valid_g711(src))
        return ACMERR_NOTPOSSIBLE;
    if (flags & ACM_FORMATSUGGESTF_WFORMATTAG) {
        /* pcm, or the other law. */
        if (dst->wFormatTag != WAVE_FORMAT_PCM &&
            !(dst->wFormatTag == WAVE_FORMAT_ALAW && src->wFormatTag == WAVE_FORMAT_MULAW) &&
            !(dst->wFormatTag == WAVE_FORMAT_MULAW && src->wFormatTag == WAVE_FORMAT_ALAW))
            return ACMERR_NOTPOSSIBLE;
        WORD bits = dst->wFormatTag == WAVE_FORMAT_PCM ? 16 : 8;
        if (flags & ACM_FORMATSUGGESTF_WBITSPERSAMPLE) {
            if (dst->wBitsPerSample != bits)
                return ACMERR_NOTPOSSIBLE;
        } else {
            dst->wBitsPerSample = bits;
        }
    } else if (flags & ACM_FORMATSUGGESTF_WBITSPERSAMPLE) {
        if (dst->wBitsPerSample == 16)
            dst->wFormatTag = WAVE_FORMAT_PCM;
        else if (dst->wBitsPerSample == 8)
            dst->wFormatTag = src->wFormatTag == WAVE_FORMAT_ALAW ? WAVE_FORMAT_MULAW : WAVE_FORMAT_ALAW;
        else
            return ACMERR_NOTPOSSIBLE;
    } else {
        dst->wFormatTag = WAVE_FORMAT_PCM;
        dst->wBitsPerSample = 16;
    }
    if (flags & ACM_FORMATSUGGESTF_NCHANNELS) {
        if (dst->nChannels != src->nChannels)
            return ACMERR_NOTPOSSIBLE;
    } else {
        dst->nChannels = src->nChannels;
    }
    if (flags & ACM_FORMATSUGGESTF_NSAMPLESPERSEC) {
        if (dst->nSamplesPerSec != src->nSamplesPerSec)
            return ACMERR_NOTPOSSIBLE;
    } else {
        dst->nSamplesPerSec = src->nSamplesPerSec;
    }
    if (dst->wFormatTag == WAVE_FORMAT_PCM) {
        dst->nBlockAlign = (WORD)((dst->wBitsPerSample >> 3) << (dst->nChannels >> 1));
    } else {
        dst->cbSize = 0;
        dst->nBlockAlign = dst->nChannels;
    }
    dst->nAvgBytesPerSec = dst->nSamplesPerSec * dst->nBlockAlign;
    return MMSYSERR_NOERROR;
}

typedef enum CONVERSION { TO_ALAW, TO_ULAW, ALAW_TO_PCM, ULAW_TO_PCM, ALAW_TO_ULAW, ULAW_TO_ALAW } CONVERSION;

static LRESULT stream_open(ACMDRVSTREAMINSTANCE *in)
{
    const WAVEFORMATEX *src = in->pwfxSrc, *dst = in->pwfxDst;
    CONVERSION c;
    if (src->wFormatTag == WAVE_FORMAT_PCM) {
        if (!valid_pcm(src) || !valid_g711(dst))
            return ACMERR_NOTPOSSIBLE;
        c = dst->wFormatTag == WAVE_FORMAT_ALAW ? TO_ALAW : TO_ULAW;
    } else {
        if (!valid_g711(src))
            return ACMERR_NOTPOSSIBLE;
        if (dst->wFormatTag == WAVE_FORMAT_PCM) {
            if (!valid_pcm(dst))
                return ACMERR_NOTPOSSIBLE;
            c = src->wFormatTag == WAVE_FORMAT_ALAW ? ALAW_TO_PCM : ULAW_TO_PCM;
        } else {
            if (!valid_g711(dst) || src->wFormatTag == dst->wFormatTag)
                return ACMERR_NOTPOSSIBLE;
            c = src->wFormatTag == WAVE_FORMAT_ALAW ? ALAW_TO_ULAW : ULAW_TO_ALAW;
        }
    }
    if (src->nChannels != dst->nChannels || src->nSamplesPerSec != dst->nSamplesPerSec)
        return MMSYSERR_NOTSUPPORTED;
    if (in->fdwOpen & ACM_STREAMOPENF_QUERY)
        return MMSYSERR_NOERROR;
    in->fdwDriver = 0;
    in->dwDriver = c + 1;
    return MMSYSERR_NOERROR;
}

/* whole frames only: a destination too small fails. */
static LRESULT stream_size(const ACMDRVSTREAMINSTANCE *in, ACMDRVSTREAMSIZE *s)
{
    const WAVEFORMATEX *src = in->pwfxSrc, *dst = in->pwfxDst;
    if ((s->fdwSize & ACM_STREAMSIZEF_QUERYMASK) == ACM_STREAMSIZEF_SOURCE) {
        DWORD n = s->cbSrcLength;
        if (src->wFormatTag == WAVE_FORMAT_PCM) {
            n -= n % AcmdPcmBlock(src);
            if (!n)
                return ACMERR_NOTPOSSIBLE;
            n >>= 1;
        } else {
            n -= n % src->nChannels;
            if (!n)
                return ACMERR_NOTPOSSIBLE;
            if (dst->wFormatTag == WAVE_FORMAT_PCM) {
                if (n > 0x7FFFFFFF)
                    return ACMERR_NOTPOSSIBLE;
                n *= 2;
            }
        }
        s->cbDstLength = n;
        return MMSYSERR_NOERROR;
    }
    if ((s->fdwSize & ACM_STREAMSIZEF_QUERYMASK) != ACM_STREAMSIZEF_DESTINATION)
        return MMSYSERR_NOTSUPPORTED;
    DWORD n = s->cbDstLength;
    if (dst->wFormatTag == WAVE_FORMAT_PCM) {
        n = (n - n % AcmdPcmBlock(dst)) >> 1;
        if (!n)
            return ACMERR_NOTPOSSIBLE;
    } else {
        n -= n % dst->nChannels;
        if (!n)
            return ACMERR_NOTPOSSIBLE;
        if (src->wFormatTag == WAVE_FORMAT_PCM) {
            if (n > 0x7FFFFFFF)
                return ACMERR_NOTPOSSIBLE;
            n *= 2;
        }
    }
    s->cbSrcLength = n;
    return MMSYSERR_NOERROR;
}

static LRESULT stream_convert(const ACMDRVSTREAMINSTANCE *in, ACMDRVSTREAMHEADER *h)
{
    const WAVEFORMATEX *src = in->pwfxSrc, *dst = in->pwfxDst;
    CONVERSION c = (CONVERSION)(in->dwDriver - 1);
    const BYTE *from = h->pbSrc;
    BYTE *to = h->pbDst;
    if (c == TO_ALAW || c == TO_ULAW) {
        DWORD block = AcmdPcmBlock(src), frames = h->cbSrcLength / block;
        if (frames * dst->nChannels > h->cbDstLength)
            return ACMERR_NOTPOSSIBLE;
        h->cbSrcLengthUsed = frames * block;
        const SHORT *samples = (const SHORT *)from;
        for (DWORD i = frames * block / 2; i; i--)
            *to++ = c == TO_ALAW ? pcm_to_alaw(*samples++) : pcm_to_ulaw(*samples++);
        h->cbDstLengthUsed = (DWORD)(to - h->pbDst);
        return MMSYSERR_NOERROR;
    }
    if (c == ALAW_TO_PCM || c == ULAW_TO_PCM) {
        DWORD frames = h->cbSrcLength / src->nChannels;
        if (frames * AcmdPcmBlock(dst) > h->cbDstLength)
            return ACMERR_NOTPOSSIBLE;
        DWORD n = frames * src->nChannels;
        h->cbSrcLengthUsed = n;
        SHORT *samples = (SHORT *)to;
        for (; n; n--)
            *samples++ = c == ALAW_TO_PCM ? alaw_to_pcm(*from++) : ulaw_to_pcm(*from++);
        h->cbDstLengthUsed = (DWORD)((BYTE *)samples - h->pbDst);
        return MMSYSERR_NOERROR;
    }
    /* Windows reports the source used even when the destination is too small. */
    DWORD n = h->cbSrcLength - h->cbSrcLength % src->nChannels;
    h->cbSrcLengthUsed = n;
    if (n > h->cbDstLength)
        return ACMERR_NOTPOSSIBLE;
    for (DWORD i = 0; i < n; i++)
        to[i] = c == ALAW_TO_ULAW ? alaw_to_ulaw(from[i]) : ulaw_to_alaw(from[i]);
    h->cbDstLengthUsed = n;
    return MMSYSERR_NOERROR;
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(module);
    return TRUE;
}

DECLSPEC_EXPORT LRESULT WINAPI DriverProc(DWORD_PTR id, HDRVR driver, UINT message, LPARAM param1, LPARAM param2)
{
    switch (message) {
    case DRV_OPEN: {
        ACMDRVOPENDESCW *desc = (ACMDRVOPENDESCW *)param2;
        if (!AcmdOpenAllowed(desc))
            return 0;
        if (desc)
            desc->dwError = MMSYSERR_NOERROR;
        return 1;
    }
    case DRV_CLOSE:
        return 1;
    case ACMDM_DRIVER_DETAILS:
        return AcmdDetails((ACMDRIVERDETAILSW *)param1, 37, 0x04000000, ACMDRIVERDETAILS_SUPPORTF_CODEC, 3,
                           L"Microsoft CCITT G.711", L"Microsoft CCITT G.711 A-Law and u-Law CODEC",
                           L"Copyright (c) 1993-1996 Microsoft Corporation",
                           L"Compresses and decompresses CCITT G.711 A-Law and u-Law audio data.");
    case ACMDM_DRIVER_ABOUT:
        return MMSYSERR_NOTSUPPORTED;
    case ACMDM_FORMATTAG_DETAILS:
        return format_tag_details((ACMFORMATTAGDETAILSW *)param1, (DWORD)param2);
    case ACMDM_FORMAT_DETAILS:
        return format_details((ACMFORMATDETAILSW *)param1, (DWORD)param2);
    case ACMDM_FORMAT_SUGGEST:
        return suggest((ACMDRVFORMATSUGGEST *)param1);
    case ACMDM_STREAM_OPEN:
        return stream_open((ACMDRVSTREAMINSTANCE *)param1);
    case ACMDM_STREAM_CLOSE:
        return MMSYSERR_NOERROR;
    case ACMDM_STREAM_SIZE:
        return stream_size((ACMDRVSTREAMINSTANCE *)param1, (ACMDRVSTREAMSIZE *)param2);
    case ACMDM_STREAM_CONVERT:
        return stream_convert((ACMDRVSTREAMINSTANCE *)param1, (ACMDRVSTREAMHEADER *)param2);
    default:
        return AcmdDefault(message);
    }
}
