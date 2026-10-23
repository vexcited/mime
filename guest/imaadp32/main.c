/*
 * imaadp32.acm, the IMA ADPCM codec: 8 and 16-bit mono or stereo pcm to and from 4-bit IMA ADPCM blocks. a block
 * holds the first sample and step index of each channel, then nibbles, stereo in groups of eight samples per
 * channel. realtime streams are limited to the rates the user's configuration allows, like Windows' codec.
 */
#include <mime/win32/winbase.h>
#include <acmdrv/acmdrv.h>

#define WAVE_FORMAT_IMA_ADPCM 0x0011

typedef struct INSTANCE {
    DWORD encode_setting, decode_setting; /* indices into rate_limits. */
} INSTANCE;

typedef struct STREAM {
    BOOL encode;
    int index[2]; /* encoder step index per channel, kept between conversions. */
} STREAM;

static const DWORD rates[] = {8000, 11025, 22050, 44100};

/* samples per second times channels that each setting allows in real time. */
static const DWORD rate_limits[] = {0, 8000, 11025, 16000, 22050, 44100, 88200};

static const SHORT steps[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,    28,
    31,    34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,   494,
    544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,
    9493,  10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int index_steps[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};

static int clamp16(int v)
{
    return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
}

static int next_index(int index, int code)
{
    index += index_steps[code];
    return index < 0 ? 0 : index > 88 ? 88 : index;
}

static WORD block_align(WORD channels, DWORD rate)
{
    WORD align = (WORD)(256 << (channels >> 1));
    if (rate > 11025)
        align = (WORD)(rate / 11000 * align);
    return align;
}

static WORD samples_per_block(WORD channels, WORD align)
{
    DWORD header = 4u << (channels >> 1);
    return (WORD)(8 * (align - header) / header + 1);
}

static BOOL valid_pcm(const WAVEFORMATEX *f)
{
    return f && f->wFormatTag == WAVE_FORMAT_PCM && (f->nChannels == 1 || f->nChannels == 2) &&
           (f->wBitsPerSample == 8 || f->wBitsPerSample == 16) && f->nBlockAlign == AcmdPcmBlock(f) &&
           f->nSamplesPerSec * f->nBlockAlign == f->nAvgBytesPerSec;
}

static BOOL valid_ima(const WAVEFORMATEX *f)
{
    if (!f || f->wFormatTag != WAVE_FORMAT_IMA_ADPCM || f->wBitsPerSample != 4 ||
        (f->nChannels != 1 && f->nChannels != 2) || f->nBlockAlign % (4 * f->nChannels) || f->cbSize != 2)
        return FALSE;
    WORD spb = ((const IMAADPCMWAVEFORMAT *)f)->wSamplesPerBlock;
    return spb >= 5 && (spb & 3) == 1 && spb == samples_per_block(f->nChannels, f->nBlockAlign);
}

static void make_ima(WAVEFORMATEX *f, WORD channels, DWORD rate)
{
    f->wFormatTag = WAVE_FORMAT_IMA_ADPCM;
    f->nChannels = channels;
    f->nSamplesPerSec = rate;
    f->wBitsPerSample = 4;
    f->nBlockAlign = block_align(channels, rate);
    f->cbSize = 2;
    WORD spb = samples_per_block(channels, f->nBlockAlign);
    ((IMAADPCMWAVEFORMAT *)f)->wSamplesPerBlock = spb;
    f->nAvgBytesPerSec = rate * f->nBlockAlign / spb;
}

/* ------------------------------------------------------------------ configuration */

/*
 * realtime limits from HKCU\Software\Microsoft\Multimedia\<alias>. when they are missing Windows benchmarks the
 * cpu and writes the result, which on any recent machine allows every rate.
 */
static void read_configuration(INSTANCE *in, const WCHAR *alias)
{
    in->encode_setting = 5;
    in->decode_setting = 6;
    if (!alias)
        return;
    WCHAR path[192];
    if (lstrlenW(alias) > 150)
        return;
    lstrcpyW(path, L"Software\\Microsoft\\Multimedia\\");
    lstrcatW(path, alias);
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, nullptr, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key,
                        nullptr))
        return;
    DWORD value, size = sizeof(value), type;
    DWORD encode = 2457, decode = 2457;
    if (!RegQueryValueExW(key, L"MaxRTEncodeSetting", nullptr, &type, (BYTE *)&value, &size) && type == REG_DWORD)
        encode = value;
    size = sizeof(value);
    if (!RegQueryValueExW(key, L"MaxRTDecodeSetting", nullptr, &type, (BYTE *)&value, &size) && type == REG_DWORD)
        decode = value;
    if (encode >= 7 || decode >= 7) {
        encode = decode = 6;
        RegSetValueExW(key, L"MaxRTEncodeSetting", 0, REG_DWORD, (const BYTE *)&encode, sizeof(encode));
        RegSetValueExW(key, L"MaxRTDecodeSetting", 0, REG_DWORD, (const BYTE *)&decode, sizeof(decode));
    }
    in->encode_setting = encode;
    in->decode_setting = decode;
    RegCloseKey(key);
}

/* ------------------------------------------------------------------ codec */

static int sample_of(const BYTE *p, BOOL bits16)
{
    return bits16 ? *(const SHORT *)p : (p[0] - 128) << 8;
}

/* encodes one sample, the predictor tracks the decoder's reconstruction. */
static int encode_sample(int sample, int *predictor, int *index)
{
    int step = steps[*index], diff = sample - *predictor;
    int code = diff < 0 ? 8 : 0, rest = diff < 0 ? -diff : diff;
    if (rest >= step) {
        code |= 4;
        rest -= step;
    }
    step >>= 1;
    if (rest >= step) {
        code |= 2;
        rest -= step;
    }
    step >>= 1;
    if (rest >= step) {
        code |= 1;
        rest -= step;
    }
    step >>= 1;
    *predictor = clamp16(code & 8 ? sample + rest - step : sample + step - rest);
    *index = next_index(*index, code);
    return code;
}

static int decode_sample(int code, int *predictor, int *index)
{
    int step = steps[*index], diff = step >> 3;
    if (code & 4)
        diff += step;
    if (code & 2)
        diff += step >> 1;
    if (code & 1)
        diff += step >> 2;
    *predictor = clamp16(code & 8 ? *predictor - diff : *predictor + diff);
    *index = next_index(*index, code);
    return *predictor;
}

/* writes until the output is full and drops the rest, like Windows' codec. */
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

static void put_sample(SINK *o, int sample, BOOL bits16)
{
    if (bits16) {
        SHORT s = (SHORT)sample;
        put(o, &s, 2);
    } else {
        BYTE b = (BYTE)((sample >> 8) + 0x80);
        put(o, &b, 1);
    }
}

static DWORD encode_mono(const BYTE *in, DWORD bytes, BOOL bits16, BYTE *out, DWORD room, WORD spb, int *index)
{
    SINK o = {out, room};
    DWORD size = bits16 ? 2 : 1, samples = bytes / size;
    while (samples) {
        DWORD n = samples < spb ? samples : spb;
        samples -= n;
        int predictor = sample_of(in, bits16);
        in += size;
        DWORD header = (WORD)predictor | (DWORD)(WORD)*index << 16;
        put(&o, &header, 4);
        for (n--; n;) {
            int low = encode_sample(sample_of(in, bits16), &predictor, index), high = 0;
            in += size;
            n--;
            if (n) {
                high = encode_sample(sample_of(in, bits16), &predictor, index);
                in += size;
                n--;
            }
            BYTE b = (BYTE)(low | high << 4);
            put(&o, &b, 1);
        }
    }
    return (DWORD)(o.p - out);
}

/* whole blocks only, eight samples of each channel per pair of words. */
static DWORD encode_stereo(const BYTE *in, DWORD bytes, BOOL bits16, BYTE *out, DWORD room, WORD spb, int *index)
{
    SINK o = {out, room};
    DWORD size = bits16 ? 2 : 1, frames = bytes / (2 * size);
    while (frames) {
        frames -= spb;
        int predictor[2];
        for (int c = 0; c < 2; c++) {
            predictor[c] = sample_of(in + c * size, bits16);
            DWORD header = (WORD)predictor[c] | (DWORD)(WORD)index[c] << 16;
            put(&o, &header, 4);
        }
        in += 2 * size;
        for (DWORD left = spb - 1u; left; left -= 8) {
            DWORD words[2] = {};
            for (int i = 0; i < 8; i++) {
                for (int c = 0; c < 2; c++)
                    words[c] |= (DWORD)encode_sample(sample_of(in + c * size, bits16), &predictor[c], &index[c])
                                << (4 * i);
                in += 2 * size;
            }
            put(&o, &words[0], 4);
            put(&o, &words[1], 4);
        }
    }
    return (DWORD)(o.p - out);
}

/* a header with an out of range step index ends the conversion with nothing written, like Windows. */
static DWORD decode_mono(const BYTE *in, DWORD bytes, BYTE *out, DWORD room, BOOL bits16, WORD align)
{
    SINK o = {out, room};
    while (bytes >= 4) {
        DWORD n = bytes < align ? bytes : align;
        bytes -= n;
        int predictor = *(const SHORT *)in, index = in[2];
        in += 4;
        if (index > 88)
            return 0;
        put_sample(&o, predictor, bits16);
        for (n -= 4; n; n--, in++) {
            put_sample(&o, decode_sample(*in & 0x0F, &predictor, &index), bits16);
            put_sample(&o, decode_sample(*in >> 4, &predictor, &index), bits16);
        }
    }
    return (DWORD)(o.p - out);
}

static DWORD decode_stereo(const BYTE *in, DWORD bytes, BYTE *out, DWORD room, BOOL bits16, WORD align)
{
    SINK o = {out, room};
    DWORD frame = bits16 ? 4 : 2;
    while (bytes) {
        bytes -= align;
        int predictor[2], index[2];
        for (int c = 0; c < 2; c++) {
            predictor[c] = *(const SHORT *)(in + 4 * c);
            index[c] = in[4 * c + 2];
            if (index[c] > 88)
                return 0;
        }
        in += 8;
        if (o.room >= frame) {
            put_sample(&o, predictor[0], bits16);
            put_sample(&o, predictor[1], bits16);
        }
        for (DWORD n = align - 8u; n; n -= 8, in += 8) {
            DWORD words[2] = {*(const DWORD *)in, *(const DWORD *)(in + 4)};
            for (int i = 0; i < 8; i++) {
                int l = decode_sample(words[0] & 0x0F, &predictor[0], &index[0]);
                int r = decode_sample(words[1] & 0x0F, &predictor[1], &index[1]);
                words[0] >>= 4;
                words[1] >>= 4;
                if (o.room >= frame) {
                    put_sample(&o, l, bits16);
                    put_sample(&o, r, bits16);
                }
            }
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
        tag = d->dwFormatTagIndex ? WAVE_FORMAT_IMA_ADPCM : WAVE_FORMAT_PCM;
        break;
    case ACM_FORMATTAGDETAILSF_LARGESTSIZE:
        tag = d->dwFormatTag ? d->dwFormatTag : WAVE_FORMAT_IMA_ADPCM;
        break;
    case ACM_FORMATTAGDETAILSF_FORMATTAG:
        tag = d->dwFormatTag;
        break;
    default:
        return MMSYSERR_NOTSUPPORTED;
    }
    if (tag == WAVE_FORMAT_PCM) {
        d->dwFormatTagIndex = 0;
        d->dwFormatTag = WAVE_FORMAT_PCM;
        d->cbFormatSize = sizeof(PCMWAVEFORMAT);
        d->fdwSupport = ACMDRIVERDETAILS_SUPPORTF_CODEC;
        d->cStandardFormats = 16;
        d->szFormatTag[0] = 0;
    } else if (tag == WAVE_FORMAT_IMA_ADPCM) {
        d->dwFormatTagIndex = 1;
        d->dwFormatTag = WAVE_FORMAT_IMA_ADPCM;
        d->cbFormatSize = sizeof(IMAADPCMWAVEFORMAT);
        d->fdwSupport = ACMDRIVERDETAILS_SUPPORTF_CODEC;
        d->cStandardFormats = 8;
        lstrcpyW(d->szFormatTag, L"IMA ADPCM");
    } else {
        return ACMERR_NOTPOSSIBLE;
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
        if (d->dwFormatTag == WAVE_FORMAT_IMA_ADPCM) {
            if (i >= 8)
                return ACMERR_NOTPOSSIBLE;
            make_ima(f, (WORD)((i & 1) + 1), rates[i >> 1]);
        } else if (d->dwFormatTag == WAVE_FORMAT_PCM) {
            if (i >= 16)
                return ACMERR_NOTPOSSIBLE;
            f->wFormatTag = WAVE_FORMAT_PCM;
            f->nChannels = (WORD)((i & 1) + 1);
            f->wBitsPerSample = i & 2 ? 16 : 8;
            f->nSamplesPerSec = rates[i >> 2];
            f->nBlockAlign = (WORD)AcmdPcmBlock(f);
            f->nAvgBytesPerSec = f->nSamplesPerSec * f->nBlockAlign;
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
    if (f->wFormatTag == WAVE_FORMAT_PCM ? !valid_pcm(f) : !valid_ima(f))
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
        if (!valid_pcm(src))
            return ACMERR_NOTPOSSIBLE;
        if ((flags & ACM_FORMATSUGGESTF_WFORMATTAG) && dst->wFormatTag != WAVE_FORMAT_IMA_ADPCM)
            return ACMERR_NOTPOSSIBLE;
        if ((flags & ACM_FORMATSUGGESTF_NCHANNELS) && dst->nChannels != src->nChannels)
            return ACMERR_NOTPOSSIBLE;
        if ((flags & ACM_FORMATSUGGESTF_NSAMPLESPERSEC) && dst->nSamplesPerSec != src->nSamplesPerSec)
            return ACMERR_NOTPOSSIBLE;
        if ((flags & ACM_FORMATSUGGESTF_WBITSPERSAMPLE) && dst->wBitsPerSample != 4)
            return ACMERR_NOTPOSSIBLE;
        make_ima(dst, src->nChannels, src->nSamplesPerSec);
        return MMSYSERR_NOERROR;
    }
    if (!valid_ima(src))
        return ACMERR_NOTPOSSIBLE;
    if ((flags & ACM_FORMATSUGGESTF_WFORMATTAG) && dst->wFormatTag != WAVE_FORMAT_PCM)
        return ACMERR_NOTPOSSIBLE;
    if ((flags & ACM_FORMATSUGGESTF_NCHANNELS) && dst->nChannels != src->nChannels)
        return ACMERR_NOTPOSSIBLE;
    if ((flags & ACM_FORMATSUGGESTF_NSAMPLESPERSEC) && dst->nSamplesPerSec != src->nSamplesPerSec)
        return ACMERR_NOTPOSSIBLE;
    if ((flags & ACM_FORMATSUGGESTF_WBITSPERSAMPLE) && dst->wBitsPerSample != 8 && dst->wBitsPerSample != 16)
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
    if (encode) {
        if (!valid_pcm(src) || !valid_ima(dst))
            return ACMERR_NOTPOSSIBLE;
        if (realtime && rate_limits[driver->encode_setting] / src->nChannels < src->nSamplesPerSec)
            return MMSYSERR_NOTSUPPORTED;
    } else {
        if (!valid_ima(src) || !valid_pcm(dst))
            return ACMERR_NOTPOSSIBLE;
        if (realtime && rate_limits[driver->decode_setting] / src->nChannels < src->nSamplesPerSec)
            return MMSYSERR_NOTSUPPORTED;
    }
    if (src->nChannels != dst->nChannels || src->nSamplesPerSec != dst->nSamplesPerSec)
        return MMSYSERR_NOTSUPPORTED;
    if (in->fdwOpen & ACM_STREAMOPENF_QUERY)
        return MMSYSERR_NOERROR;
    STREAM *s = LocalAlloc(LPTR, sizeof(*s));
    if (!s)
        return MMSYSERR_NOMEM;
    s->encode = encode;
    in->fdwDriver = 0;
    in->dwDriver = (DWORD_PTR)s;
    return MMSYSERR_NOERROR;
}

/* sizes in whole blocks, a partial block counting as one. */
static LRESULT stream_size(const ACMDRVSTREAMINSTANCE *in, ACMDRVSTREAMSIZE *s)
{
    const WAVEFORMATEX *src = in->pwfxSrc, *dst = in->pwfxDst;
    ULONG64 n;
    if ((s->fdwSize & ACM_STREAMSIZEF_QUERYMASK) == ACM_STREAMSIZEF_SOURCE) {
        DWORD bytes = s->cbSrcLength;
        if (src->wFormatTag == WAVE_FORMAT_IMA_ADPCM) {
            DWORD blocks = bytes / src->nBlockAlign;
            if (!blocks)
                return ACMERR_NOTPOSSIBLE;
            DWORD per = ((const IMAADPCMWAVEFORMAT *)src)->wSamplesPerBlock * dst->nBlockAlign;
            if (0xFFFFFFFF / per < blocks)
                return ACMERR_NOTPOSSIBLE;
            n = (ULONG64)per * (blocks + (bytes % src->nBlockAlign != 0));
        } else {
            DWORD per = src->nBlockAlign * ((const IMAADPCMWAVEFORMAT *)dst)->wSamplesPerBlock;
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
    if (dst->wFormatTag == WAVE_FORMAT_IMA_ADPCM) {
        DWORD blocks = bytes / dst->nBlockAlign;
        DWORD per = src->nBlockAlign * ((const IMAADPCMWAVEFORMAT *)dst)->wSamplesPerBlock;
        if (!blocks || 0xFFFFFFFF / per < blocks)
            return ACMERR_NOTPOSSIBLE;
        n = (ULONG64)blocks * per;
    } else {
        DWORD blocks = bytes / (dst->nBlockAlign * ((const IMAADPCMWAVEFORMAT *)src)->wSamplesPerBlock);
        n = (ULONG64)blocks * src->nBlockAlign;
        if (!blocks || n > 0xFFFFFFFF)
            return ACMERR_NOTPOSSIBLE;
    }
    s->cbSrcLength = (DWORD)n;
    return MMSYSERR_NOERROR;
}

/*
 * converts whole blocks. without ACM_STREAMCONVERTF_BLOCKALIGN the whole source counts as used and a trailing
 * partial block is dropped, like Windows' codec.
 */
static LRESULT stream_convert(const ACMDRVSTREAMINSTANCE *in, ACMDRVSTREAMHEADER *h)
{
    const WAVEFORMATEX *src = in->pwfxSrc, *dst = in->pwfxDst;
    STREAM *s = (STREAM *)in->dwDriver;
    DWORD bytes;
    if (s->encode) {
        DWORD block = AcmdPcmBlock(src), frames = h->cbSrcLength / block;
        WORD spb = ((const IMAADPCMWAVEFORMAT *)dst)->wSamplesPerBlock;
        bytes = block * (frames - frames % spb);
        if (h->fdwConvert & ACM_STREAMCONVERTF_START)
            s->index[0] = s->index[1] = 0;
        h->cbSrcLengthUsed = h->fdwConvert & ACM_STREAMCONVERTF_BLOCKALIGN ? bytes : h->cbSrcLength;
        BOOL bits16 = src->wBitsPerSample == 16;
        h->cbDstLengthUsed = src->nChannels == 1
                                 ? encode_mono(h->pbSrc, bytes, bits16, h->pbDst, h->cbDstLength, spb, s->index)
                                 : encode_stereo(h->pbSrc, bytes, bits16, h->pbDst, h->cbDstLength, spb, s->index);
    } else {
        bytes = h->cbSrcLength - h->cbSrcLength % src->nBlockAlign;
        h->cbSrcLengthUsed = h->fdwConvert & ACM_STREAMCONVERTF_BLOCKALIGN ? bytes : h->cbSrcLength;
        BOOL bits16 = dst->wBitsPerSample == 16;
        h->cbDstLengthUsed = src->nChannels == 1
                                 ? decode_mono(h->pbSrc, bytes, h->pbDst, h->cbDstLength, bits16, src->nBlockAlign)
                                 : decode_stereo(h->pbSrc, bytes, h->pbDst, h->cbDstLength, bits16, src->nBlockAlign);
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
        /* no settings dialog yet, but the -1 query says there is one. */
        return param1 == -1 ? 1 : DRVCNF_CANCEL;
    case ACMDM_DRIVER_DETAILS:
        return AcmdDetails((ACMDRIVERDETAILSW *)param1, 34, 0x04000000, ACMDRIVERDETAILS_SUPPORTF_CODEC, 2,
                           L"Microsoft IMA ADPCM", L"Microsoft IMA ADPCM CODEC",
                           L"Copyright (C) 1992-1996 Microsoft Corporation",
                           L"Compresses and decompresses IMA ADPCM audio data.");
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
    case ACMDM_STREAM_CONVERT:
        return stream_convert((ACMDRVSTREAMINSTANCE *)param1, (ACMDRVSTREAMHEADER *)param2);
    default:
        return AcmdDefault(message);
    }
}
