/*
 * msadp32.acm, the Microsoft adpcm codec: 8 and 16 bit pcm, mono and
 * stereo, to and from 4-bit adpcm blocks predicted by the seven standard
 * coefficient pairs. realtime encoding uses the second pair for every
 * block. a stream opened ACM_STREAMOPENF_NONREALTIME tries all seven and
 * keeps the one with the least error, as windows' codec does.
 */
#include <mime/win32/winbase.h>
#include <acmdrv/acmdrv.h>

typedef struct STREAM {
    BOOL encode;
    BOOL full_pass;
} STREAM;

static const DWORD rates[] = {8000, 11025, 22050, 44100};
static const int adapt[16] = {230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230};
static const SHORT coef1[7] = {256, 512, 0, 192, 240, 460, 392};
static const SHORT coef2[7] = {0, -256, 0, 64, 0, -208, -232};

static int distance(int a, int b)
{
    return a > b ? a - b : b - a;
}

static int clamp16(int v)
{
    return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
}

static WORD samples_per_block(WORD channels, WORD align)
{
    return (WORD)(8 * (align - (7u << (channels >> 1))) / (4u << (channels >> 1)) + 2);
}

static BOOL valid_pcm(const WAVEFORMATEX *f)
{
    return f && f->wFormatTag == WAVE_FORMAT_PCM && (f->wBitsPerSample == 8 || f->wBitsPerSample == 16) &&
           (f->nChannels == 1 || f->nChannels == 2) && f->nBlockAlign == AcmdPcmBlock(f) &&
           f->nSamplesPerSec * f->nBlockAlign == f->nAvgBytesPerSec;
}

static BOOL valid_adpcm(const WAVEFORMATEX *f)
{
    return f && f->wFormatTag == WAVE_FORMAT_ADPCM && f->wBitsPerSample == 4 &&
           (f->nChannels == 1 || f->nChannels == 2) && f->cbSize >= 32 && 7u * f->nChannels <= f->nBlockAlign &&
           ((const ADPCMWAVEFORMAT *)f)->wSamplesPerBlock == samples_per_block(f->nChannels, f->nBlockAlign);
}

/* exactly the seven standard pairs, the only ones windows' codec converts. */
static BOOL standard_coefficients(const WAVEFORMATEX *f)
{
    const ADPCMWAVEFORMAT *a = (const ADPCMWAVEFORMAT *)f;
    if (f->cbSize < 32 || a->wNumCoef != 7)
        return FALSE;
    for (int i = 0; i < 7; i++)
        if (a->aCoef[i].iCoef1 != coef1[i] || a->aCoef[i].iCoef2 != coef2[i])
            return FALSE;
    return TRUE;
}

static void make_adpcm(WAVEFORMATEX *f, WORD channels, DWORD rate)
{
    ADPCMWAVEFORMAT *a = (ADPCMWAVEFORMAT *)f;
    f->wFormatTag = WAVE_FORMAT_ADPCM;
    f->nChannels = channels;
    f->nSamplesPerSec = rate;
    f->wBitsPerSample = 4;
    WORD align = (WORD)(256 << (channels >> 1));
    if (rate > 11025)
        align = (WORD)(rate / 11000 * align);
    f->nBlockAlign = align;
    WORD spb = samples_per_block(channels, align);
    f->cbSize = 32;
    a->wSamplesPerBlock = spb;
    f->nAvgBytesPerSec = rate * align / spb;
    a->wNumCoef = 7;
    for (int i = 0; i < 7; i++)
        a->aCoef[i] = (ADPCMCOEFSET){coef1[i], coef2[i]};
}

/* ------------------------------------------------------------------ the codec */

typedef struct SINK {
    BYTE *p;
    DWORD room;
} SINK;

static void put(SINK *o, const void *data, DWORD size)
{
    if (o->room >= size) {
        memcpy(o->p, data, size);
        o->p += size;
        o->room -= size;
    }
}

typedef struct CHANNEL {
    int predictor, delta, sample1, sample2; /* sample1 is the newer one. */
} CHANNEL;

/* windows divides toward zero, then clamps the code. */
static int encode_sample(CHANNEL *c, int sample)
{
    int predicted = (c->sample1 * coef1[c->predictor] + c->sample2 * coef2[c->predictor]) >> 8;
    int code = (sample - predicted) / c->delta;
    code = code > 7 ? 7 : code < -8 ? -8 : code;
    c->sample2 = c->sample1;
    c->sample1 = clamp16(predicted + c->delta * code);
    c->delta = adapt[code & 0x0F] * c->delta >> 8;
    if (c->delta < 16)
        c->delta = 16;
    return code & 0x0F;
}

static int decode_sample(CHANNEL *c, int nibble, const ADPCMCOEFSET *coefs)
{
    int code = nibble & 8 ? nibble - 16 : nibble;
    int predicted = (c->sample1 * coefs[c->predictor].iCoef1 + c->sample2 * coefs[c->predictor].iCoef2) >> 8;
    c->sample2 = c->sample1;
    c->sample1 = clamp16(predicted + c->delta * code);
    c->delta = adapt[nibble] * c->delta >> 8;
    if (c->delta < 16)
        c->delta = 16;
    return c->sample1;
}

/*
 * picks the pair that encodes a block best. the starting delta comes from
 * how far each pair misses the third to fifth samples, the error is the sum
 * of the squares over the block, shifted by 7.
 */
static void choose(CHANNEL *c, const int *s, DWORD n)
{
    DWORD best_error = 0;
    int first[5];
    for (int i = 0; i < 5; i++)
        first[i] = i < (int)n ? s[i] : 0;
    for (int p = 0; p < 7; p++) {
        int p0 = (coef1[p] * first[1] + coef2[p] * first[0]) >> 8;
        int p1 = (coef1[p] * first[2] + coef2[p] * first[1]) >> 8;
        int p2 = (coef1[p] * first[3] + coef2[p] * first[2]) >> 8;
        int delta = (distance(p0, first[2]) + distance(p1, first[3]) + distance(p2, first[4])) / 12;
        if (delta < 16)
            delta = 16;
        DWORD error = 0;
        if (n > 2) {
            CHANNEL trial = {p, delta, first[1], first[0]};
            for (DWORD i = 2; i < n; i++) {
                encode_sample(&trial, s[i]);
                /* a 32-bit square that may wrap, shifted arithmetically, as windows computes it. */
                unsigned miss = (unsigned)(trial.sample1 - s[i]);
                error += (DWORD)((int)(miss * miss) >> 7);
            }
        }
        if (!p || error < best_error) {
            best_error = error;
            c->predictor = p;
            c->delta = delta;
        }
    }
}

static int read_sample(const BYTE *p, BOOL bits16)
{
    return bits16 ? *(const SHORT *)p : (p[0] - 128) << 8;
}

static DWORD encode(const BYTE *in, DWORD bytes, BOOL bits16, WORD channels, BYTE *out, DWORD room, WORD spb,
                    BOOL full_pass)
{
    SINK o = {out, room};
    DWORD size = bits16 ? 2 : 1, frames = bytes / (size * channels);
    int *samples = full_pass ? LocalAlloc(LMEM_FIXED, spb * sizeof(int) * 2) : nullptr;
    if (full_pass && !samples)
        return 0;
    while (frames) {
        DWORD n = frames < spb ? frames : spb;
        frames -= n;
        CHANNEL c[2] = {};
        for (WORD ch = 0; ch < channels; ch++) {
            if (full_pass) {
                int *s = samples + ch * spb;
                for (DWORD i = 0; i < n; i++)
                    s[i] = read_sample(in + (i * channels + ch) * size, bits16);
                choose(&c[ch], s, n);
            } else {
                c[ch].predictor = 1;
                c[ch].delta = 128;
            }
            c[ch].sample2 = read_sample(in + ch * size, bits16);
            c[ch].sample1 = n > 1 ? read_sample(in + (channels + ch) * size, bits16) : 0;
        }
        for (WORD ch = 0; ch < channels; ch++)
            put(&o, &(BYTE){(BYTE)c[ch].predictor}, 1);
        for (WORD ch = 0; ch < channels; ch++)
            put(&o, &(SHORT){(SHORT)c[ch].delta}, 2);
        for (WORD ch = 0; ch < channels; ch++)
            put(&o, &(SHORT){(SHORT)c[ch].sample1}, 2);
        for (WORD ch = 0; ch < channels; ch++)
            put(&o, &(SHORT){(SHORT)c[ch].sample2}, 2);
        in += (n > 1 ? 2 : 1) * channels * size;
        DWORD left = n > 2 ? (n - 2) * channels : 0;
        /* nibbles in sample order, the high one first. a lone last one pairs with zero. */
        for (DWORD i = 0; i < left; i += 2) {
            int high = encode_sample(&c[i % channels], read_sample(in, bits16)), low = 0;
            in += size;
            if (i + 1 < left) {
                low = encode_sample(&c[(i + 1) % channels], read_sample(in, bits16));
                in += size;
            }
            put(&o, &(BYTE){(BYTE)(high << 4 | low)}, 1);
        }
    }
    LocalFree(samples);
    return (DWORD)(o.p - out);
}

static void put_sample(SINK *o, int sample, BOOL bits16)
{
    if (bits16)
        put(o, &(SHORT){(SHORT)sample}, 2);
    else
        put(o, &(BYTE){(BYTE)((sample >> 8) + 0x80)}, 1);
}

/* a block naming a pair the format lacks ends the conversion with nothing written. */
static DWORD decode(const BYTE *in, DWORD bytes, const WAVEFORMATEX *src, BYTE *out, DWORD room, BOOL bits16)
{
    const ADPCMWAVEFORMAT *a = (const ADPCMWAVEFORMAT *)src;
    WORD channels = src->nChannels, align = src->nBlockAlign;
    DWORD header = 7u * channels;
    SINK o = {out, room};
    while (bytes >= header) {
        DWORD n = bytes < align ? bytes : align;
        bytes -= n;
        CHANNEL c[2] = {};
        for (WORD ch = 0; ch < channels; ch++) {
            c[ch].predictor = in[ch];
            if (c[ch].predictor >= a->wNumCoef)
                return 0;
            c[ch].delta = *(const SHORT *)(in + channels + 2 * ch);
            c[ch].sample1 = *(const SHORT *)(in + 3 * channels + 2 * ch);
            c[ch].sample2 = *(const SHORT *)(in + 5 * channels + 2 * ch);
        }
        in += header;
        for (WORD ch = 0; ch < channels; ch++)
            put_sample(&o, c[ch].sample2, bits16);
        for (WORD ch = 0; ch < channels; ch++)
            put_sample(&o, c[ch].sample1, bits16);
        for (DWORD i = 0, nibbles = 2 * (n - header); i < nibbles; i += 2, in++) {
            put_sample(&o, decode_sample(&c[i % channels], *in >> 4, a->aCoef), bits16);
            put_sample(&o, decode_sample(&c[(i + 1) % channels], *in & 0x0F, a->aCoef), bits16);
        }
    }
    return (DWORD)(o.p - out);
}

/* ------------------------------------------------------------------ messages */

static LRESULT format_tag_details(ACMFORMATTAGDETAILSW *d, DWORD flags)
{
    DWORD tag;
    switch (flags & ACM_FORMATTAGDETAILSF_QUERYMASK) {
    case ACM_FORMATTAGDETAILSF_INDEX:
        if (d->dwFormatTagIndex >= 2)
            return ACMERR_NOTPOSSIBLE;
        tag = d->dwFormatTagIndex ? WAVE_FORMAT_ADPCM : WAVE_FORMAT_PCM;
        break;
    case ACM_FORMATTAGDETAILSF_LARGESTSIZE:
        tag = d->dwFormatTag ? d->dwFormatTag : WAVE_FORMAT_ADPCM;
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
        d->cStandardFormats = 16;
        d->szFormatTag[0] = 0;
    } else if (tag == WAVE_FORMAT_ADPCM) {
        d->dwFormatTagIndex = 1;
        d->cbFormatSize = sizeof(WAVEFORMATEX) + 32;
        d->cStandardFormats = 8;
        lstrcpyW(d->szFormatTag, L"Microsoft ADPCM");
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
            if (i >= 16)
                return ACMERR_NOTPOSSIBLE;
            f->wFormatTag = WAVE_FORMAT_PCM;
            f->nChannels = (WORD)((i & 1) + 1);
            f->nSamplesPerSec = rates[i >> 2];
            f->wBitsPerSample = i & 2 ? 16 : 8;
            f->nBlockAlign = (WORD)AcmdPcmBlock(f);
            f->nAvgBytesPerSec = f->nSamplesPerSec * f->nBlockAlign;
        } else if (d->dwFormatTag == WAVE_FORMAT_ADPCM) {
            if (i >= 8)
                return ACMERR_NOTPOSSIBLE;
            make_adpcm(f, (WORD)((i & 1) + 1), rates[i >> 1]);
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
    if (f->wFormatTag == WAVE_FORMAT_PCM ? !valid_pcm(f) : !valid_adpcm(f) || !standard_coefficients(f))
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
    const WAVEFORMATEX *src = s->pwfxSrc;
    WAVEFORMATEX *dst = s->pwfxDst;
    if (src->wFormatTag == WAVE_FORMAT_PCM) {
        if (!valid_pcm(src) || ((flags & ACM_FORMATSUGGESTF_WFORMATTAG) && dst->wFormatTag != WAVE_FORMAT_ADPCM) ||
            ((flags & ACM_FORMATSUGGESTF_NCHANNELS) && dst->nChannels != src->nChannels) ||
            ((flags & ACM_FORMATSUGGESTF_NSAMPLESPERSEC) && dst->nSamplesPerSec != src->nSamplesPerSec) ||
            ((flags & ACM_FORMATSUGGESTF_WBITSPERSAMPLE) && dst->wBitsPerSample != 4))
            return ACMERR_NOTPOSSIBLE;
        make_adpcm(dst, src->nChannels, src->nSamplesPerSec);
        return MMSYSERR_NOERROR;
    }
    if (!valid_adpcm(src) || !standard_coefficients(src) ||
        ((flags & ACM_FORMATSUGGESTF_WFORMATTAG) && dst->wFormatTag != WAVE_FORMAT_PCM) ||
        ((flags & ACM_FORMATSUGGESTF_NCHANNELS) && dst->nChannels != src->nChannels) ||
        ((flags & ACM_FORMATSUGGESTF_NSAMPLESPERSEC) && dst->nSamplesPerSec != src->nSamplesPerSec))
        return ACMERR_NOTPOSSIBLE;
    dst->wFormatTag = WAVE_FORMAT_PCM;
    dst->nSamplesPerSec = src->nSamplesPerSec;
    dst->nChannels = src->nChannels;
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
    BOOL encode = !valid_adpcm(src);
    const WAVEFORMATEX *adpcm = encode ? dst : src, *pcm = encode ? src : dst;
    if (!valid_adpcm(adpcm) || !valid_pcm(pcm) || adpcm->nChannels != pcm->nChannels ||
        adpcm->nSamplesPerSec != pcm->nSamplesPerSec || !standard_coefficients(adpcm))
        return ACMERR_NOTPOSSIBLE;
    if (in->fdwOpen & ACM_STREAMOPENF_QUERY)
        return MMSYSERR_NOERROR;
    STREAM *s = LocalAlloc(LPTR, sizeof(*s));
    if (!s)
        return MMSYSERR_NOMEM;
    s->encode = encode;
    s->full_pass = (in->fdwOpen & ACM_STREAMOPENF_NONREALTIME) != 0;
    in->fdwDriver = 0;
    in->dwDriver = (DWORD_PTR)s;
    return MMSYSERR_NOERROR;
}

static LRESULT stream_size(const ACMDRVSTREAMINSTANCE *in, ACMDRVSTREAMSIZE *s)
{
    const WAVEFORMATEX *src = in->pwfxSrc, *dst = in->pwfxDst;
    ULONG64 n;
    if ((s->fdwSize & ACM_STREAMSIZEF_QUERYMASK) == ACM_STREAMSIZEF_SOURCE) {
        DWORD bytes = s->cbSrcLength;
        if (src->wFormatTag == WAVE_FORMAT_ADPCM) {
            DWORD blocks = bytes / src->nBlockAlign;
            if (!blocks)
                return ACMERR_NOTPOSSIBLE;
            DWORD per = ((const ADPCMWAVEFORMAT *)src)->wSamplesPerBlock * dst->nBlockAlign;
            if (0xFFFFFFFF / per < blocks)
                return ACMERR_NOTPOSSIBLE;
            n = (ULONG64)per * (blocks + (bytes % src->nBlockAlign != 0));
        } else {
            DWORD per = src->nBlockAlign * ((const ADPCMWAVEFORMAT *)dst)->wSamplesPerBlock;
            n = (ULONG64)dst->nBlockAlign * (bytes / per + (bytes % per != 0));
        }
        if (n > 0xFFFFFFFF || !(DWORD)n)
            return ACMERR_NOTPOSSIBLE;
        s->cbDstLength = (DWORD)n;
        return MMSYSERR_NOERROR;
    }
    if ((s->fdwSize & ACM_STREAMSIZEF_QUERYMASK) != ACM_STREAMSIZEF_DESTINATION)
        return MMSYSERR_NOTSUPPORTED;
    DWORD bytes = s->cbDstLength;
    if (dst->wFormatTag == WAVE_FORMAT_ADPCM) {
        DWORD blocks = bytes / dst->nBlockAlign;
        DWORD per = src->nBlockAlign * ((const ADPCMWAVEFORMAT *)dst)->wSamplesPerBlock;
        if (!blocks || 0xFFFFFFFF / per < blocks)
            return ACMERR_NOTPOSSIBLE;
        n = (ULONG64)blocks * per;
    } else {
        DWORD blocks = bytes / (dst->nBlockAlign * ((const ADPCMWAVEFORMAT *)src)->wSamplesPerBlock);
        n = (ULONG64)blocks * src->nBlockAlign;
        if (!blocks || n > 0xFFFFFFFF)
            return ACMERR_NOTPOSSIBLE;
    }
    s->cbSrcLength = (DWORD)n;
    return MMSYSERR_NOERROR;
}

/*
 * decoding takes every byte, or whole blocks with ACM_STREAMCONVERTF_BLOCKALIGN.
 * encoding takes an even number of frames, whole blocks with the flag, and
 * nothing below two frames.
 */
static LRESULT stream_convert(const ACMDRVSTREAMINSTANCE *in, ACMDRVSTREAMHEADER *h)
{
    const WAVEFORMATEX *src = in->pwfxSrc, *dst = in->pwfxDst;
    STREAM *s = (STREAM *)in->dwDriver;
    BOOL aligned = h->fdwConvert & ACM_STREAMCONVERTF_BLOCKALIGN;
    if (!s->encode) {
        DWORD bytes = h->cbSrcLength;
        if (aligned)
            bytes -= bytes % src->nBlockAlign;
        h->cbSrcLengthUsed = bytes;
        h->cbDstLengthUsed = decode(h->pbSrc, bytes, src, h->pbDst, h->cbDstLength, dst->wBitsPerSample == 16);
        return MMSYSERR_NOERROR;
    }
    DWORD block = AcmdPcmBlock(src), frames = h->cbSrcLength / block;
    WORD spb = ((const ADPCMWAVEFORMAT *)dst)->wSamplesPerBlock;
    if (aligned)
        frames -= frames % spb;
    if (frames < 2) {
        h->cbDstLengthUsed = 0;
        h->cbSrcLengthUsed = aligned ? 0 : h->cbSrcLength;
        return MMSYSERR_NOERROR;
    }
    DWORD bytes = block * (frames & ~1u);
    h->cbSrcLengthUsed = bytes;
    h->cbDstLengthUsed = encode(h->pbSrc, bytes, src->wBitsPerSample == 16, src->nChannels, h->pbDst,
                                h->cbDstLength, spb, s->full_pass);
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
        return AcmdDetails((ACMDRIVERDETAILSW *)param1, 33, 0x04000000, ACMDRIVERDETAILS_SUPPORTF_CODEC, 2, L"MS-ADPCM",
                           L"Microsoft ADPCM CODEC", L"Copyright (C) 1992-1996 Microsoft Corporation",
                           L"Compresses and decompresses Microsoft ADPCM audio data.");
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
        LocalFree((void *)((ACMDRVSTREAMINSTANCE *)param1)->dwDriver);
        return MMSYSERR_NOERROR;
    case ACMDM_STREAM_SIZE:
        return stream_size((ACMDRVSTREAMINSTANCE *)param1, (ACMDRVSTREAMSIZE *)param2);
    case ACMDM_STREAM_CONVERT:
        return stream_convert((ACMDRVSTREAMINSTANCE *)param1, (ACMDRVSTREAMHEADER *)param2);
    default:
        return AcmdDefault(message);
    }
}
