/*
 * msgsm32.acm, the GSM 6.10 codec: mono 8 and 16-bit pcm to and from GSM 6.10 in 65-byte blocks of 320 samples.
 * like the Windows codec, realtime streams are limited to the rates the user's configuration allows.
 */
#include <mime/win32/winbase.h>
#include <acmdrv/acmdrv.h>
#include "gsm.h"

typedef struct INSTANCE {
    DWORD encode_setting, decode_setting;
} INSTANCE;

typedef struct STREAM {
    BOOL encode;
    GSM_ENCODER encoder;
    GSM_DECODER decoder;
} STREAM;

static const DWORD rates[] = {8000, 11025, 22050, 44100};
static const DWORD rate_limits[] = {0, 8000, 11025, 22050, 44100, 88200};

static BOOL valid_pcm(const WAVEFORMATEX *f)
{
    return f && f->wFormatTag == WAVE_FORMAT_PCM && f->nChannels == 1 &&
           (f->wBitsPerSample == 8 || f->wBitsPerSample == 16) && f->nBlockAlign == f->wBitsPerSample / 8 &&
           f->nSamplesPerSec * f->nBlockAlign == f->nAvgBytesPerSec;
}

static BOOL valid_gsm(const WAVEFORMATEX *f)
{
    return f && f->wFormatTag == WAVE_FORMAT_GSM610 && f->nChannels == 1 && f->nBlockAlign == 65 &&
           65 * f->nSamplesPerSec / 320 == f->nAvgBytesPerSec && !f->wBitsPerSample && f->cbSize == 2 &&
           ((const GSM610WAVEFORMAT *)f)->wSamplesPerBlock == 320;
}

static void make_gsm(WAVEFORMATEX *f, DWORD rate)
{
    f->wFormatTag = WAVE_FORMAT_GSM610;
    f->nChannels = 1;
    f->nSamplesPerSec = rate;
    f->nAvgBytesPerSec = 65 * rate / 320;
    f->nBlockAlign = 65;
    f->wBitsPerSample = 0;
    f->cbSize = 2;
    ((GSM610WAVEFORMAT *)f)->wSamplesPerBlock = 320;
}

/* realtime limits, stored like imaadp32's. a missing value allows every rate. */
static void read_configuration(INSTANCE *in, const WCHAR *alias)
{
    in->encode_setting = 0;
    in->decode_setting = 1;
    if (!alias || lstrlenW(alias) > 150)
        return;
    WCHAR path[192];
    lstrcpyW(path, L"Software\\Microsoft\\Multimedia\\");
    lstrcatW(path, alias);
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key,
                        nullptr))
        return;
    DWORD value, size = sizeof(value), type, encode = 2457, decode = 2457;
    if (!RegQueryValueExW(key, L"MaxRTEncodeSetting", nullptr, &type, (BYTE *)&value, &size) && type == REG_DWORD)
        encode = value;
    size = sizeof(value);
    if (!RegQueryValueExW(key, L"MaxRTDecodeSetting", nullptr, &type, (BYTE *)&value, &size) && type == REG_DWORD)
        decode = value;
    if (encode >= 6 || decode >= 6) {
        encode = decode = 5;
        RegSetValueExW(key, L"MaxRTEncodeSetting", 0, REG_DWORD, (const BYTE *)&encode, sizeof(encode));
        RegSetValueExW(key, L"MaxRTDecodeSetting", 0, REG_DWORD, (const BYTE *)&decode, sizeof(decode));
    }
    in->encode_setting = encode;
    in->decode_setting = decode;
    RegCloseKey(key);
}

static LRESULT format_tag_details(ACMFORMATTAGDETAILSW *d, DWORD flags)
{
    DWORD tag;
    switch (flags & ACM_FORMATTAGDETAILSF_QUERYMASK) {
    case ACM_FORMATTAGDETAILSF_INDEX:
        if (d->dwFormatTagIndex >= 2)
            return ACMERR_NOTPOSSIBLE;
        tag = d->dwFormatTagIndex ? WAVE_FORMAT_GSM610 : WAVE_FORMAT_PCM;
        break;
    case ACM_FORMATTAGDETAILSF_LARGESTSIZE:
        tag = d->dwFormatTag ? d->dwFormatTag : WAVE_FORMAT_GSM610;
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
        d->cStandardFormats = 8;
        d->szFormatTag[0] = 0;
    } else if (tag == WAVE_FORMAT_GSM610) {
        d->dwFormatTagIndex = 1;
        d->cbFormatSize = sizeof(GSM610WAVEFORMAT);
        d->cStandardFormats = 4;
        lstrcpyW(d->szFormatTag, L"GSM 6.10");
    } else {
        return ACMERR_NOTPOSSIBLE;
    }
    d->dwFormatTag = tag;
    d->fdwSupport = ACMDRIVERDETAILS_SUPPORTF_CODEC;
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
        if (d->dwFormatTag == WAVE_FORMAT_PCM) {
            if (i >= 8)
                return ACMERR_NOTPOSSIBLE;
            f->wFormatTag = WAVE_FORMAT_PCM;
            f->nChannels = 1;
            f->wBitsPerSample = i & 1 ? 16 : 8;
            f->nBlockAlign = f->wBitsPerSample / 8;
            f->nSamplesPerSec = rates[(i >> 1) & 3];
            f->nAvgBytesPerSec = f->nSamplesPerSec * f->nBlockAlign;
        } else if (d->dwFormatTag == WAVE_FORMAT_GSM610) {
            if (i >= 4)
                return ACMERR_NOTPOSSIBLE;
            make_gsm(f, rates[i & 3]);
        } else {
            return ACMERR_NOTPOSSIBLE;
        }
        break;
    }
    case ACM_FORMATDETAILSF_FORMAT:
        break;
    default:
        return MMSYSERR_NOTSUPPORTED;
    }
    if (f->wFormatTag == WAVE_FORMAT_PCM ? !valid_pcm(f) : !valid_gsm(f))
        return ACMERR_NOTPOSSIBLE;
    d->fdwSupport = ACMDRIVERDETAILS_SUPPORTF_CODEC;
    d->szFormat[0] = 0;
    if (d->cbStruct > sizeof(*d))
        d->cbStruct = sizeof(*d);
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
        if (!valid_pcm(src) || ((flags & ACM_FORMATSUGGESTF_WFORMATTAG) && dst->wFormatTag != WAVE_FORMAT_GSM610) ||
            ((flags & ACM_FORMATSUGGESTF_NCHANNELS) && dst->nChannels != src->nChannels) ||
            ((flags & ACM_FORMATSUGGESTF_NSAMPLESPERSEC) && dst->nSamplesPerSec != src->nSamplesPerSec) ||
            ((flags & ACM_FORMATSUGGESTF_WBITSPERSAMPLE) && dst->wBitsPerSample != 0))
            return ACMERR_NOTPOSSIBLE;
        make_gsm(dst, src->nSamplesPerSec);
        return MMSYSERR_NOERROR;
    }
    if (!valid_gsm(src) || ((flags & ACM_FORMATSUGGESTF_WFORMATTAG) && dst->wFormatTag != WAVE_FORMAT_PCM) ||
        ((flags & ACM_FORMATSUGGESTF_NCHANNELS) && dst->nChannels != src->nChannels) ||
        ((flags & ACM_FORMATSUGGESTF_NSAMPLESPERSEC) && dst->nSamplesPerSec != src->nSamplesPerSec) ||
        ((flags & ACM_FORMATSUGGESTF_WBITSPERSAMPLE) && dst->wBitsPerSample != 8 && dst->wBitsPerSample != 16))
        return ACMERR_NOTPOSSIBLE;
    dst->wFormatTag = WAVE_FORMAT_PCM;
    dst->nChannels = src->nChannels;
    dst->nSamplesPerSec = src->nSamplesPerSec;
    if (!(flags & ACM_FORMATSUGGESTF_WBITSPERSAMPLE))
        dst->wBitsPerSample = 16;
    dst->nBlockAlign = (WORD)AcmdPcmBlock(dst);
    dst->nAvgBytesPerSec = dst->nSamplesPerSec * dst->nBlockAlign;
    return MMSYSERR_NOERROR;
}

static LRESULT stream_open(INSTANCE *driver, ACMDRVSTREAMINSTANCE *in)
{
    const WAVEFORMATEX *src = in->pwfxSrc, *dst = in->pwfxDst;
    BOOL realtime = !(in->fdwOpen & ACM_STREAMOPENF_NONREALTIME);
    BOOL encode = src->wFormatTag == WAVE_FORMAT_PCM;
    if (encode ? !valid_pcm(src) || !valid_gsm(dst) : !valid_gsm(src) || !valid_pcm(dst))
        return ACMERR_NOTPOSSIBLE;
    DWORD limit = rate_limits[encode ? driver->encode_setting : driver->decode_setting];
    if (realtime && limit / src->nChannels < src->nSamplesPerSec)
        return MMSYSERR_NOTSUPPORTED;
    if (src->nChannels != dst->nChannels || src->nSamplesPerSec != dst->nSamplesPerSec)
        return MMSYSERR_NOTSUPPORTED;
    if (in->fdwOpen & ACM_STREAMOPENF_QUERY)
        return MMSYSERR_NOERROR;
    STREAM *s = LocalAlloc(LPTR, sizeof(*s));
    if (!s)
        return MMSYSERR_NOMEM;
    s->encode = encode;
    GsmReset(&s->encoder, &s->decoder);
    in->fdwDriver = 0;
    in->dwDriver = (DWORD_PTR)s;
    return MMSYSERR_NOERROR;
}

static LRESULT stream_size(const ACMDRVSTREAMINSTANCE *in, ACMDRVSTREAMSIZE *s)
{
    const WAVEFORMATEX *src = in->pwfxSrc, *dst = in->pwfxDst;
    if ((s->fdwSize & ACM_STREAMSIZEF_QUERYMASK) == ACM_STREAMSIZEF_SOURCE) {
        if (src->wFormatTag == WAVE_FORMAT_GSM610) {
            DWORD blocks = s->cbSrcLength / 65;
            if (blocks - 1 > 0xCCCCCB || 0xFFFFFFFF / dst->nBlockAlign < 320 * blocks)
                return ACMERR_NOTPOSSIBLE;
            s->cbDstLength = 320 * blocks * dst->nBlockAlign;
            return MMSYSERR_NOERROR;
        }
        DWORD frames = s->cbSrcLength / src->nBlockAlign, blocks = frames / 320;
        if (!blocks)
            return ACMERR_NOTPOSSIBLE;
        ULONG64 n = 65ull * (blocks + (frames % 320 != 0));
        if (n > 0xFFFFFFFF)
            return ACMERR_NOTPOSSIBLE;
        s->cbDstLength = (DWORD)n;
        return MMSYSERR_NOERROR;
    }
    if ((s->fdwSize & ACM_STREAMSIZEF_QUERYMASK) != ACM_STREAMSIZEF_DESTINATION)
        return MMSYSERR_NOTSUPPORTED;
    if (dst->wFormatTag == WAVE_FORMAT_GSM610) {
        DWORD blocks = s->cbDstLength / 65;
        if (blocks - 1 > 0xCCCCCB || 0xFFFFFFFF / src->nBlockAlign < 320 * blocks)
            return ACMERR_NOTPOSSIBLE;
        s->cbSrcLength = 320 * blocks * src->nBlockAlign;
        return MMSYSERR_NOERROR;
    }
    DWORD frames = s->cbDstLength / dst->nBlockAlign;
    if (frames < 320)
        return ACMERR_NOTPOSSIBLE;
    s->cbSrcLength = 65 * (frames / 320);
    return MMSYSERR_NOERROR;
}

/*
 * a last partial block is padded with silence, or dropped with ACM_STREAMCONVERTF_BLOCKALIGN. like Windows, nothing
 * is converted unless the destination holds every block.
 */
static LRESULT encode(STREAM *s, const WAVEFORMATEX *src, ACMDRVSTREAMHEADER *h)
{
    DWORD size = src->nBlockAlign, frames = h->cbSrcLength / size, blocks = frames / 320;
    BOOL aligned = h->fdwConvert & ACM_STREAMCONVERTF_BLOCKALIGN;
    if (!aligned && frames % 320)
        blocks++;
    if (65ull * blocks > h->cbDstLength)
        return ACMERR_NOTPOSSIBLE;
    h->cbDstLengthUsed = 65 * blocks;
    DWORD bytes = aligned ? 320 * blocks * size : h->cbSrcLength;
    h->cbSrcLengthUsed = bytes;
    const BYTE *in = h->pbSrc;
    BYTE *out = h->pbDst;
    bytes -= bytes % size;
    for (DWORD b = 0; b < blocks; b++) {
        GSM_FRAME frames_out[2];
        for (int n = 0; n < 2; n++) {
            SHORT samples[160] = {};
            DWORD count = bytes / size < 160 ? bytes / size : 160;
            for (DWORD i = 0; i < count; i++, in += size)
                samples[i] = size == 2 ? *(const SHORT *)in : (SHORT)((in[0] - 128) << 8);
            bytes -= count * size;
            GsmEncode(&s->encoder, samples, &frames_out[n]);
        }
        GsmPack(frames_out, out);
        out += 65;
    }
    return MMSYSERR_NOERROR;
}

static LRESULT decode(STREAM *s, const WAVEFORMATEX *dst, ACMDRVSTREAMHEADER *h)
{
    DWORD blocks = h->cbSrcLength / 65;
    if (!blocks) {
        h->cbDstLengthUsed = 0;
        h->cbSrcLengthUsed = h->cbSrcLength;
        return MMSYSERR_NOERROR;
    }
    DWORD size = dst->nBlockAlign;
    if (blocks > 0xCCCCCC || 0xFFFFFFFF / size < 320 * blocks || 320 * blocks * size > h->cbDstLength)
        return ACMERR_NOTPOSSIBLE;
    h->cbDstLengthUsed = 320 * blocks * size;
    h->cbSrcLengthUsed = 65 * blocks;
    const BYTE *in = h->pbSrc;
    BYTE *out = h->pbDst;
    for (DWORD b = 0; b < blocks; b++, in += 65) {
        GSM_FRAME frames[2];
        GsmUnpack(in, frames);
        for (int n = 0; n < 2; n++) {
            SHORT samples[160];
            GsmDecode(&s->decoder, &frames[n], samples);
            if (size == 2) {
                memcpy(out, samples, sizeof(samples));
                out += sizeof(samples);
            } else {
                for (int i = 0; i < 160; i++)
                    *out++ = (BYTE)((samples[i] >> 8) + 0x80);
            }
        }
    }
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
    INSTANCE *instance = (INSTANCE *)id;
    switch (message) {
    case DRV_OPEN: {
        ACMDRVOPENDESCW *desc = (ACMDRVOPENDESCW *)param2;
        if (!AcmdOpenAllowed(desc))
            return 0;
        instance = LocalAlloc(LPTR, sizeof(*instance));
        if (!instance) {
            if (desc)
                desc->dwError = MMSYSERR_NOMEM;
            return 0;
        }
        read_configuration(instance, desc ? desc->pszAliasName : nullptr);
        if (desc)
            desc->dwError = MMSYSERR_NOERROR;
        return (LRESULT)instance;
    }
    case DRV_CLOSE:
        LocalFree(instance);
        return 1;
    case DRV_QUERYCONFIGURE:
        return 1;
    case DRV_CONFIGURE:
        /* no settings dialog yet, but the -1 query still reports one. */
        return param1 == -1 ? 1 : DRVCNF_CANCEL;
    case ACMDM_DRIVER_DETAILS:
        return AcmdDetails((ACMDRIVERDETAILSW *)param1, 36, 0x04000000, ACMDRIVERDETAILS_SUPPORTF_CODEC, 2,
                           L"Microsoft GSM 6.10", L"Microsoft GSM 6.10 Audio CODEC",
                           L"Copyright (C) 1993-1996 Microsoft Corporation",
                           L"Compresses and decompresses audio data conforming to the ETSI-GSM (European "
                           L"Telecommunications Standards Institute-Groupe Special Mobile) recommendation 6.10.");
    case ACMDM_DRIVER_ABOUT:
        return MMSYSERR_NOTSUPPORTED;
    case ACMDM_FORMATTAG_DETAILS:
        return format_tag_details((ACMFORMATTAGDETAILSW *)param1, (DWORD)param2);
    case ACMDM_FORMAT_DETAILS:
        return format_details((ACMFORMATDETAILSW *)param1, (DWORD)param2);
    case ACMDM_FORMAT_SUGGEST:
        return suggest((ACMDRVFORMATSUGGEST *)param1);
    case ACMDM_STREAM_OPEN:
        return stream_open(instance, (ACMDRVSTREAMINSTANCE *)param1);
    case ACMDM_STREAM_CLOSE:
        LocalFree((void *)((ACMDRVSTREAMINSTANCE *)param1)->dwDriver);
        return MMSYSERR_NOERROR;
    case ACMDM_STREAM_SIZE:
        return stream_size((ACMDRVSTREAMINSTANCE *)param1, (ACMDRVSTREAMSIZE *)param2);
    case ACMDM_STREAM_CONVERT: {
        ACMDRVSTREAMINSTANCE *in = (ACMDRVSTREAMINSTANCE *)param1;
        ACMDRVSTREAMHEADER *h = (ACMDRVSTREAMHEADER *)param2;
        STREAM *s = (STREAM *)in->dwDriver;
        if (h->fdwConvert & ACM_STREAMCONVERTF_START)
            GsmReset(&s->encoder, &s->decoder);
        return s->encode ? encode(s, in->pwfxSrc, h) : decode(s, in->pwfxDst, h);
    }
    default:
        return AcmdDefault(message);
    }
}
