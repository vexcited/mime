/*
 * GS wavetable synth using drivers\gm.dls, with 40 voices at 22050 Hz. supports GM, GS and XG resets, GS part
 * channels, RPNs 0 to 2, a 40 log10 volume and expression curve, and constant-power panning. reverb and chorus are
 * not rendered. a sound set is a DLS collection of 16 bit or ADPCM waves whose articulations may use two envelopes
 * with delay and hold, two oscillators, a low pass filter and a pitch scale by key.
 */
#define MIME_BUILD_WINMM
#include "winmm_private.h"
#include "synth.h"

#include "../lib/audio/output.h"

#define RATE 22050
#define BLOCK 32
#define VOICES 40
#define QUEUE_FRAMES 1100 /* about 50 ms ahead of the host. */
#define SILENT_DB -96.0f

#define SRC_NONE 0
#define SRC_LFO 1
#define SRC_VELOCITY 2
#define SRC_KEY 3
#define SRC_EG2 5
#define SRC_VIBRATO 9
#define SRC_CC1 0x81
#define DST_ATTENUATION 1
#define DST_PITCH 3
#define DST_PAN 4
#define DST_LFO_FREQUENCY 0x104
#define DST_LFO_DELAY 0x105
#define DST_VIB_FREQUENCY 0x114
#define DST_VIB_DELAY 0x115
#define DST_EG1 0x206
#define DST_EG2 0x30A
#define DST_FILTER_CUTOFF 0x500
#define DST_FILTER_Q 0x501
#define EG_ATTACK 0
#define EG_DECAY 1
#define EG_RELEASE 3
#define EG_SUSTAIN 4
#define EG_DELAY 5
#define EG_HOLD 6

#define FILTER_OPEN (13500 << 16) /* in cents, 20 kHz. */
#define FILTER_BYPASS_CENTS 12300.0f /* about 10 kHz, which the output rate cannot hold. */

#define ABSENT_TIME ((LONG)0x80000000)
#define DRUMS 0x80000000u
#define NULL_RPN 0x3FFF

typedef struct WSMP {
    SHORT unity, fine;
    float db;
    BOOL loop;
    DWORD loop_start, loop_end;
} WSMP;

typedef struct WAVE {
    const SHORT *data; /* the samples, decoded the first time a note needs them when the wave is ADPCM. */
    const BYTE *packed;
    const SHORT *coefficients; /* seven pairs of the predictors of the ADPCM blocks. */
    DWORD frames, rate, packed_size, block_size, block_frames, coefficient_count;
    BOOL owned;
    WSMP wsmp;
} WAVE;

/* DLS envelope units: time cents and sustain in tenths of a percent. */
typedef struct ENVELOPE {
    LONG delay, attack, hold, decay, release, sustain;
    LONG velocity_attack, key_decay, key_hold;
} ENVELOPE;

typedef struct ARTICULATION {
    LONG lfo_frequency, lfo_delay;
    LONG vib_frequency, vib_delay, vib_pitch, vib_cc1_pitch;
    ENVELOPE eg1, eg2;
    LONG pan, lfo_db, lfo_pitch, cc1_pitch, eg2_pitch, key_pitch;
    LONG cutoff, resonance, eg2_cutoff, lfo_cutoff, velocity_cutoff;
} ARTICULATION;

typedef struct REGION {
    BYTE key_low, key_high, vel_low, vel_high;
    WORD options, group;
    DWORD wave;
    BOOL has_wsmp;
    WSMP wsmp;
    const ARTICULATION *art;
} REGION;

typedef struct INSTRUMENT {
    DWORD patch;
    REGION *regions;
    DWORD count;
} INSTRUMENT;

typedef struct EG {
    int stage;
    float level, time;
    float delay, attack, hold, decay, sustain, release; /* seconds, the sustain in 0 to 1000. */
} EG;

typedef struct VOICE {
    BOOL active, released, sustained;
    int part;
    BYTE key;
    DWORD serial, patch;
    const REGION *region;
    const WAVE *wave;
    const ARTICULATION *art;
    WSMP smp;
    UINT64 position;   /* in 32.32 frames of the wave. */
    float cents, ratio, static_db;
    EG eg1, eg2;
    float lfo_phase, lfo_step, lfo_delay;
    float vib_phase, vib_step, vib_delay;
    BYTE velocity;
    BOOL filtered;
    float b0, b1, b2, a1, a2, z1, z2, filter_gain; /* the low pass filter and its state. */
    float left, right; /* the gains of the last block. */
    DWORD priority;
    UINT64 started;
} VOICE;

typedef struct PART {
    BYTE channel;
    BOOL drums, sustain, mono;
    BYTE program, bank_msb, bank_lsb;
    DWORD patch;
    BYTE volume, pan, expression, mod, pressure, reverb, chorus, brightness;
    WORD rpn;
    BYTE data_msb, data_lsb;
    int bend, bend_range, fine, coarse;
    SHORT scale[12];
    DWORD priority;
} PART;

struct SYNTH {
    RTL_CRITICAL_SECTION lock;
    HANDLE file, mapping;
    const BYTE *image;
    WAVE *waves;
    DWORD wave_count;
    INSTRUMENT *instruments;
    DWORD instrument_count;
    ARTICULATION *arts;
    DWORD art_count;
    PART parts[16];
    VOICE voices[VOICES];
    BOOL gs, xg;
    float master_db;
    DWORD serial;
    UINT64 clock;
    MimeOutput out;
    float level;
    HANDLE thread;
    volatile BOOL quit;
};

static float g_curve[128]; /* 40 log10 of v over 127. */
static float g_pan[128];   /* 10 log10 of p over 127. */
static float g_sine[257];

/* ------------------------------------------------------------------ math */

static float log2_approx(float x)
{
    union {
        float f;
        UINT32 u;
    } v = {x};
    int e = (int)((v.u >> 23) & 0xFF) - 127;
    v.u = (v.u & 0x007FFFFF) | 0x3F800000;
    float m = v.f, y = (m - 1.0f) / (m + 1.0f), y2 = y * y;
    /* 2 atanh y over ln 2 is log2 m. */
    float s = y * (2.0f + y2 * (2.0f / 3 + y2 * (2.0f / 5 + y2 * (2.0f / 7 + y2 * (2.0f / 9)))));
    return e + s * 1.44269504f;
}

static float exp2_approx(float x)
{
    if (x < -126.0f)
        return 0.0f;
    if (x > 126.0f)
        x = 126.0f;
    int i = (int)x;
    if (x < i)
        i--;
    float f = x - i, l = f * 0.69314718f;
    float p = 1.0f + l * (1.0f + l * (0.5f + l * (1.0f / 6 + l * (1.0f / 24 + l * (1.0f / 120 + l / 720)))));
    union {
        UINT32 u;
        float f;
    } v = {(UINT32)(i + 127) << 23};
    return v.f * p;
}

static float db_to_gain(float db)
{
    return db <= -100.0f ? 0.0f : exp2_approx(db * (3.32192809f / 20.0f));
}

static float log10_approx(float x)
{
    return log2_approx(x) * 0.30103f;
}

static float seconds(LONG timecents)
{
    return timecents == ABSENT_TIME ? 0.0f : exp2_approx(timecents / (1200.0f * 65536.0f));
}

static void tables(void)
{
    if (g_sine[64] != 0.0f)
        return;
    g_curve[0] = SILENT_DB;
    g_pan[0] = -25.0f;
    for (int v = 1; v < 128; v++) {
        g_curve[v] = 40.0f * log10_approx(v / 127.0f);
        g_pan[v] = 10.0f * log10_approx(v / 127.0f);
    }
    /* a quarter turn by a rotation of 1/256 of a turn, in double precision. */
    double c = 1.0, s = 0.0, a = 6.283185307179586 / 256, ca = 1 - a * a / 2 + a * a * a * a / 24,
           sa = a - a * a * a / 6 + a * a * a * a * a / 120;
    for (int i = 0; i <= 256; i++) {
        g_sine[i] = (float)s;
        double n = c * ca - s * sa;
        s = s * ca + c * sa;
        c = n;
    }
}

/* ------------------------------------------------------------------ the sound set */

typedef struct CHUNK {
    DWORD id;
    const BYTE *data;
    DWORD size;
} CHUNK;

static DWORD read32(const BYTE *p)
{
    return p[0] | p[1] << 8 | p[2] << 16 | (DWORD)p[3] << 24;
}

static WORD read16(const BYTE *p)
{
    return (WORD)(p[0] | p[1] << 8);
}

#define FOURCC(a, b, c, d) ((DWORD)(a) | (DWORD)(b) << 8 | (DWORD)(c) << 16 | (DWORD)(d) << 24)

/* the next chunk of a list's data; a list's own type is the first four bytes of its data. */
static BOOL next_chunk(const BYTE **p, const BYTE *end, CHUNK *c)
{
    if (end - *p < 8)
        return FALSE;
    c->id = read32(*p);
    c->size = read32(*p + 4);
    c->data = *p + 8;
    if (c->size > (DWORD)(end - c->data))
        return FALSE;
    *p = c->data + c->size + (c->size & 1);
    if (*p > end)
        *p = end;
    return TRUE;
}

static BOOL is_list(const CHUNK *c, DWORD type)
{
    return c->id == FOURCC('L', 'I', 'S', 'T') && c->size >= 4 && read32(c->data) == type;
}

static void parse_wsmp(const CHUNK *c, WSMP *w)
{
    if (c->size < 20)
        return;
    DWORD size = read32(c->data);
    w->unity = (SHORT)read16(c->data + 4);
    w->fine = (SHORT)read16(c->data + 6);
    w->db = (LONG)read32(c->data + 8) / 655360.0f;
    DWORD loops = read32(c->data + 16);
    w->loop = FALSE;
    if (loops && size + 16 <= c->size) {
        const BYTE *l = c->data + size;
        w->loop = TRUE;
        w->loop_start = read32(l + 8);
        w->loop_end = w->loop_start + read32(l + 12);
    }
}

static void default_art(ARTICULATION *a)
{
    memset(a, 0, sizeof(*a));
    a->lfo_frequency = (LONG)(-851.3 * 65536); /* 5 Hz. */
    a->lfo_delay = (LONG)(-7973.0 * 65536);    /* 10 ms. */
    a->vib_delay = ABSENT_TIME;
    a->vib_cc1_pitch = 50 << 16;
    a->cutoff = FILTER_OPEN;
    a->eg1.delay = a->eg1.attack = a->eg1.hold = a->eg1.decay = a->eg1.release = ABSENT_TIME;
    a->eg2.delay = a->eg2.attack = a->eg2.hold = a->eg2.decay = a->eg2.release = ABSENT_TIME;
    a->eg1.sustain = a->eg2.sustain = 1000 << 16;
}

static void parse_art(const CHUNK *c, ARTICULATION *a)
{
    if (c->size < 8)
        return;
    DWORD header = read32(c->data), count = read32(c->data + 4);
    if (header > c->size || count > (c->size - header) / 12)
        return;
    for (DWORD i = 0; i < count; i++) {
        const BYTE *b = c->data + header + 12 * i;
        WORD source = read16(b), control = read16(b + 2), dest = read16(b + 4);
        LONG scale = (LONG)read32(b + 8);
        ENVELOPE *eg = dest >= DST_EG2 && dest <= DST_EG2 + 6 ? &a->eg2 : dest >= DST_EG1 && dest <= DST_EG1 + 6 ? &a->eg1
                                                                                                                 : nullptr;
        int stage = eg ? dest - (eg == &a->eg2 ? DST_EG2 : DST_EG1) : -1;
        if (source == SRC_NONE && control == SRC_NONE) {
            if (dest == DST_LFO_FREQUENCY)
                a->lfo_frequency = scale;
            else if (dest == DST_LFO_DELAY)
                a->lfo_delay = scale;
            else if (dest == DST_VIB_FREQUENCY)
                a->vib_frequency = scale;
            else if (dest == DST_VIB_DELAY)
                a->vib_delay = scale;
            else if (dest == DST_FILTER_CUTOFF)
                a->cutoff = scale;
            else if (dest == DST_FILTER_Q)
                a->resonance = scale;
            else if (dest == DST_PAN)
                a->pan = scale;
            else if (eg && stage == EG_ATTACK)
                eg->attack = scale;
            else if (eg && stage == EG_DECAY)
                eg->decay = scale;
            else if (eg && stage == EG_RELEASE)
                eg->release = scale;
            else if (eg && stage == EG_SUSTAIN)
                eg->sustain = scale;
            else if (eg && stage == EG_DELAY)
                eg->delay = scale;
            else if (eg && stage == EG_HOLD)
                eg->hold = scale;
        } else if (source == SRC_LFO && dest == DST_ATTENUATION) {
            a->lfo_db = scale;
        } else if (source == SRC_LFO && dest == DST_PITCH) {
            if (control == SRC_CC1) {
                a->cc1_pitch = scale;
                a->vib_cc1_pitch = 0; /* the set brings its own vibrato for the wheel. */
            } else if (control == SRC_NONE) {
                a->lfo_pitch = scale;
            }
        } else if (source == SRC_VIBRATO && dest == DST_PITCH) {
            if (control == SRC_CC1)
                a->vib_cc1_pitch = scale;
            else if (control == SRC_NONE)
                a->vib_pitch = scale;
        } else if (source == SRC_LFO && dest == DST_FILTER_CUTOFF && control == SRC_NONE) {
            a->lfo_cutoff = scale;
        } else if (source == SRC_VELOCITY && dest == DST_FILTER_CUTOFF && control == SRC_NONE) {
            a->velocity_cutoff = scale;
        } else if (source == SRC_VELOCITY && eg && stage == EG_ATTACK) {
            eg->velocity_attack = scale;
        } else if (source == SRC_KEY && eg && stage == EG_DECAY) {
            eg->key_decay = scale;
        } else if (source == SRC_KEY && eg && stage == EG_HOLD) {
            eg->key_hold = scale;
        } else if (source == SRC_KEY && dest == DST_PITCH && control == SRC_NONE) {
            a->key_pitch = scale;
        } else if (source == SRC_EG2 && dest == DST_PITCH) {
            a->eg2_pitch = scale;
        } else if (source == SRC_EG2 && dest == DST_FILTER_CUTOFF) {
            a->eg2_cutoff = scale;
        }
    }
}

/* uses the last art1 or art2 articulation in a lart or lar2 list. */
static const ARTICULATION *parse_lart(SYNTH *s, const CHUNK *list)
{
    ARTICULATION *a = &s->arts[s->art_count];
    default_art(a);
    const BYTE *p = list->data + 4, *end = list->data + list->size;
    CHUNK c;
    while (next_chunk(&p, end, &c))
        if (c.id == FOURCC('a', 'r', 't', '1') || c.id == FOURCC('a', 'r', 't', '2'))
            parse_art(&c, a);
    s->art_count++;
    return a;
}

static void parse_region(SYNTH *s, const CHUNK *list, REGION *r, const ARTICULATION *global)
{
    const BYTE *p = list->data + 4, *end = list->data + list->size;
    CHUNK c;
    r->key_high = r->vel_high = 127;
    r->art = global;
    while (next_chunk(&p, end, &c)) {
        if (c.id == FOURCC('r', 'g', 'n', 'h') && c.size >= 12) {
            r->key_low = (BYTE)read16(c.data);
            r->key_high = (BYTE)read16(c.data + 2);
            r->vel_low = (BYTE)read16(c.data + 4);
            r->vel_high = (BYTE)read16(c.data + 6);
            r->options = read16(c.data + 8);
            r->group = read16(c.data + 10);
        } else if (c.id == FOURCC('w', 's', 'm', 'p')) {
            r->has_wsmp = TRUE;
            parse_wsmp(&c, &r->wsmp);
        } else if (c.id == FOURCC('w', 'l', 'n', 'k') && c.size >= 12) {
            r->wave = read32(c.data + 8);
        } else if (is_list(&c, FOURCC('l', 'a', 'r', 't')) || is_list(&c, FOURCC('l', 'a', 'r', '2'))) {
            r->art = parse_lart(s, &c);
        }
    }
}

static void parse_instrument(SYNTH *s, const CHUNK *list, INSTRUMENT *in)
{
    const BYTE *p = list->data + 4, *end = list->data + list->size;
    CHUNK c, lrgn = {};
    const ARTICULATION *global = nullptr;
    DWORD regions = 0;
    while (next_chunk(&p, end, &c)) {
        if (c.id == FOURCC('i', 'n', 's', 'h') && c.size >= 12) {
            regions = read32(c.data);
            DWORD bank = read32(c.data + 4);
            in->patch = (bank & DRUMS) | ((bank >> 8) & 0x7F) << 16 | (bank & 0x7F) << 8 | (read32(c.data + 8) & 0x7F);
        } else if (is_list(&c, FOURCC('l', 'r', 'g', 'n'))) {
            lrgn = c;
        } else if (is_list(&c, FOURCC('l', 'a', 'r', 't')) || is_list(&c, FOURCC('l', 'a', 'r', '2'))) {
            global = parse_lart(s, &c);
        }
    }
    if (!global) {
        global = &s->arts[s->art_count];
        default_art(&s->arts[s->art_count++]);
    }
    if (!lrgn.data || !regions)
        return;
    in->regions = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, regions * sizeof(REGION));
    if (!in->regions)
        return;
    p = lrgn.data + 4;
    end = lrgn.data + lrgn.size;
    while (in->count < regions && next_chunk(&p, end, &c))
        if (is_list(&c, FOURCC('r', 'g', 'n', ' ')))
            parse_region(s, &c, &in->regions[in->count++], global);
}

static BOOL parse_wave(const CHUNK *list, WAVE *w)
{
    const BYTE *p = list->data + 4, *end = list->data + list->size;
    CHUNK c;
    BOOL format = FALSE, adpcm = FALSE;
    DWORD frames = 0;
    w->wsmp.unity = 60;
    while (next_chunk(&p, end, &c)) {
        if (c.id == FOURCC('f', 'm', 't', ' ') && c.size >= 16) {
            WORD tag = read16(c.data);
            w->rate = read32(c.data + 4);
            if (tag == WAVE_FORMAT_PCM) {
                format = read16(c.data + 2) == 1 && read16(c.data + 14) == 16;
            } else if (tag == WAVE_FORMAT_ADPCM && c.size >= 22 && read16(c.data + 2) == 1 && read16(c.data + 14) == 4) {
                /* the block size, the frames of a block and the seven pairs of predictors that follow. */
                DWORD pairs = read16(c.data + 20);
                w->block_size = read16(c.data + 12);
                w->block_frames = read16(c.data + 18);
                if (pairs == 7 && c.size >= 22 + 4 * pairs && w->block_size > 7 && w->block_frames > 1) {
                    w->coefficients = (const SHORT *)(c.data + 22);
                    format = adpcm = TRUE;
                }
            }
        } else if (c.id == FOURCC('f', 'a', 'c', 't') && c.size >= 4) {
            frames = read32(c.data);
        } else if (c.id == FOURCC('d', 'a', 't', 'a')) {
            if (adpcm) {
                w->packed = c.data;
                w->packed_size = c.size;
            } else {
                w->data = (const SHORT *)c.data;
                w->frames = c.size / 2;
            }
        } else if (c.id == FOURCC('w', 's', 'm', 'p')) {
            parse_wsmp(&c, &w->wsmp);
        }
    }
    if (adpcm) {
        /* a wave without a count plays what its blocks hold. */
        DWORD whole = w->packed_size / w->block_size;
        DWORD most = whole * w->block_frames;
        w->frames = frames && frames <= most + w->block_frames ? frames : most;
        if (!w->packed || !w->frames)
            return FALSE;
    }
    return format && (w->data || w->packed) && w->rate;
}

/* the 16 bit samples of an ADPCM wave, made once when a note first plays it. */
static BOOL wave_ready(WAVE *w)
{
    if (w->data)
        return TRUE;
    if (!w->packed || !w->frames)
        return FALSE;
    static const int adapt[16] = {230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230};
    SHORT *out = HeapAlloc(GetProcessHeap(), 0, ((SIZE_T)w->frames + w->block_frames) * sizeof(SHORT));
    if (!out)
        return FALSE;
    DWORD done = 0;
    for (DWORD at = 0; at + 7 <= w->packed_size && done < w->frames; at += w->block_size) {
        const BYTE *b = w->packed + at;
        DWORD bytes = w->packed_size - at < w->block_size ? w->packed_size - at : w->block_size;
        DWORD set = b[0] < 7 ? b[0] : 0;
        int c1 = w->coefficients[2 * set], c2 = w->coefficients[2 * set + 1];
        int delta = (SHORT)read16(b + 1), s1 = (SHORT)read16(b + 3), s2 = (SHORT)read16(b + 5);
        out[done++] = (SHORT)s2;
        out[done++] = (SHORT)s1;
        for (DWORD i = 7; i < bytes && done < w->frames + 1; i++) {
            for (int half = 0; half < 2; half++) {
                int nibble = half ? b[i] & 15 : b[i] >> 4;
                int p = (s1 * c1 + s2 * c2) / 256 + (nibble >= 8 ? nibble - 16 : nibble) * delta;
                p = p > 32767 ? 32767 : p < -32768 ? -32768 : p;
                s2 = s1;
                s1 = p;
                delta = delta * adapt[nibble] / 256;
                if (delta < 16)
                    delta = 16;
                out[done++] = (SHORT)p;
            }
        }
    }
    while (done < w->frames)
        out[done++] = 0;
    w->data = out;
    w->owned = TRUE;
    return TRUE;
}

/* counts storage before allocating each kind once. */
static void count_lists(const BYTE *p, const BYTE *end, DWORD *arts)
{
    CHUNK c;
    while (next_chunk(&p, end, &c)) {
        if (c.id != FOURCC('L', 'I', 'S', 'T') || c.size < 4)
            continue;
        DWORD type = read32(c.data);
        if (type == FOURCC('l', 'a', 'r', 't') || type == FOURCC('l', 'a', 'r', '2') ||
            type == FOURCC('i', 'n', 's', ' '))
            (*arts)++;
        if (type == FOURCC('l', 'i', 'n', 's') || type == FOURCC('i', 'n', 's', ' ') ||
            type == FOURCC('l', 'r', 'g', 'n') || type == FOURCC('r', 'g', 'n', ' '))
            count_lists(c.data + 4, c.data + c.size, arts);
    }
}

static BOOL load(SYNTH *s, const BYTE *image, DWORD size)
{
    if (size < 12 || read32(image) != FOURCC('R', 'I', 'F', 'F') || read32(image + 8) != FOURCC('D', 'L', 'S', ' '))
        return FALSE;
    DWORD riff = read32(image + 4);
    const BYTE *end = image + 8 + (riff < size - 8 ? riff : size - 8), *p = image + 12;
    CHUNK c, lins = {}, wvpl = {}, ptbl = {};
    while (next_chunk(&p, end, &c)) {
        if (is_list(&c, FOURCC('l', 'i', 'n', 's')))
            lins = c;
        else if (is_list(&c, FOURCC('w', 'v', 'p', 'l')))
            wvpl = c;
        else if (c.id == FOURCC('p', 't', 'b', 'l'))
            ptbl = c;
    }
    if (!lins.data || !wvpl.data || ptbl.size < 8)
        return FALSE;
    DWORD cues = read32(ptbl.data + 4), header = read32(ptbl.data);
    if (header > ptbl.size || cues > (ptbl.size - header) / 4)
        return FALSE;
    HANDLE heap = GetProcessHeap();
    s->waves = HeapAlloc(heap, HEAP_ZERO_MEMORY, (cues + 1) * sizeof(WAVE));
    if (!s->waves)
        return FALSE;
    const BYTE *pool = wvpl.data + 4, *pool_end = wvpl.data + wvpl.size;
    for (DWORD i = 0; i < cues; i++) {
        const BYTE *at = pool + read32(ptbl.data + header + 4 * i);
        if (at < pool || at >= pool_end || !next_chunk(&at, pool_end, &c) || !is_list(&c, FOURCC('w', 'a', 'v', 'e')))
            continue;
        if (!parse_wave(&c, &s->waves[i]))
            memset(&s->waves[i], 0, sizeof(WAVE));
    }
    s->wave_count = cues;
    DWORD instruments = 0, arts = 0;
    p = lins.data + 4;
    while (next_chunk(&p, lins.data + lins.size, &c))
        instruments += is_list(&c, FOURCC('i', 'n', 's', ' '));
    count_lists(lins.data + 4, lins.data + lins.size, &arts);
    s->instruments = HeapAlloc(heap, HEAP_ZERO_MEMORY, (instruments + 1) * sizeof(INSTRUMENT));
    s->arts = HeapAlloc(heap, HEAP_ZERO_MEMORY, (arts + 1) * sizeof(ARTICULATION));
    if (!s->instruments || !s->arts)
        return FALSE;
    p = lins.data + 4;
    while (next_chunk(&p, lins.data + lins.size, &c))
        if (is_list(&c, FOURCC('i', 'n', 's', ' ')))
            parse_instrument(s, &c, &s->instruments[s->instrument_count++]);
    return s->instrument_count != 0;
}

/* ------------------------------------------------------------------ parts */

static void reset_controllers(PART *p, BOOL full)
{
    p->expression = 127;
    p->bend = 0;
    p->mod = 0;
    p->pressure = 0;
    p->reverb = 40;
    p->chorus = 0;
    p->brightness = 64;
    p->sustain = FALSE;
    if (full) {
        p->volume = 100;
        p->pan = 64;
    }
}

static void stop_part(SYNTH *s, int part, BOOL quick);

static void set_program(PART *p)
{
    p->patch = (p->drums ? DRUMS : 0) | (DWORD)p->bank_msb << 16 | (DWORD)p->bank_lsb << 8 | p->program;
}

/* GM, GS and XG resets clear modes and tunings, then restore each part's power-on state. */
static void system_reset(SYNTH *s, BOOL gs, BOOL xg)
{
    s->gs = gs;
    s->xg = xg;
    s->master_db = 0.0f;
    for (int i = 0; i < 16; i++) {
        PART *p = &s->parts[i];
        p->channel = (BYTE)(i == 0 ? 9 : i <= 9 ? i - 1 : i);
        p->priority = 0x80000010u - i;
        p->drums = i == 0 && !xg;
        p->rpn = NULL_RPN;
        p->data_msb = p->data_lsb = 0;
        p->fine = p->coarse = 0;
        memset(p->scale, 0, sizeof(p->scale));
        p->mono = FALSE;
        p->bend_range = 200;
        stop_part(s, i, TRUE);
        reset_controllers(p, TRUE);
        p->bank_msb = i == 0 && xg ? 127 : 0;
        p->bank_lsb = 0;
        p->program = 0;
        set_program(p);
    }
}

/* ------------------------------------------------------------------ voices */

static void eg_start(EG *eg, const ENVELOPE *e, BYTE key, BYTE velocity)
{
    eg->time = 0.0f;
    eg->level = 0.0f;
    LONG attack = e->attack == ABSENT_TIME ? ABSENT_TIME : e->attack + (LONG)((LONG64)e->velocity_attack * velocity / 128);
    LONG decay = e->decay == ABSENT_TIME ? ABSENT_TIME : e->decay + (LONG)((LONG64)e->key_decay * key / 128);
    LONG hold = e->hold == ABSENT_TIME ? ABSENT_TIME : e->hold + (LONG)((LONG64)e->key_hold * key / 128);
    eg->delay = seconds(e->delay);
    eg->attack = seconds(attack);
    eg->hold = seconds(hold);
    eg->decay = seconds(decay);
    eg->stage = eg->delay > 0.0f ? EG_DELAY : EG_ATTACK;
    eg->release = seconds(e->release);
    float sustain = e->sustain / 65536.0f;
    eg->sustain = sustain < 0 ? 0 : sustain > 1000 ? 1000 : sustain;
}

static void eg_release(EG *eg)
{
    if (eg->stage != EG_RELEASE)
        eg->stage = EG_RELEASE;
}

/* advances the envelope by dt seconds through attack, decay, sustain and release; levels range from 0 to 1000. */
static void eg_step(EG *eg, float dt, BOOL volume)
{
    switch (eg->stage) {
    case EG_DELAY:
        eg->time += dt;
        if (eg->time >= eg->delay) {
            eg->stage = EG_ATTACK;
            eg->time = 0.0f;
        }
        break;
    case EG_HOLD:
        eg->time += dt;
        if (eg->time >= eg->hold) {
            eg->stage = EG_DECAY;
            eg->time = 0.0f;
        }
        break;
    case EG_ATTACK:
        eg->time += dt;
        if (eg->time >= eg->attack) {
            eg->level = 1000.0f;
            eg->stage = eg->hold > 0.0f ? EG_HOLD : EG_DECAY;
            eg->time = 0.0f;
        } else {
            float a = eg->time / eg->attack;
            /* the volume envelope rises linearly in amplitude, read in decibels. */
            eg->level = volume ? (a > 0.0f ? 1000.0f + 1000.0f / 96.0f * 20.0f * log10_approx(a) : 0.0f) : 1000.0f * a;
            if (eg->level < 0.0f)
                eg->level = 0.0f;
        }
        break;
    case EG_DECAY:
        eg->level = eg->decay > 0.0f ? eg->level - 1000.0f * dt / eg->decay : eg->sustain;
        if (eg->level <= eg->sustain) {
            eg->level = eg->sustain;
            eg->stage = EG_SUSTAIN;
        }
        break;
    case EG_RELEASE:
        eg->level = eg->release > 0.0f ? eg->level - 1000.0f * dt / eg->release : 0.0f;
        if (eg->level < 0.0f)
            eg->level = 0.0f;
        break;
    }
}

static void release_voice(VOICE *v)
{
    v->released = TRUE;
    v->sustained = FALSE;
    eg_release(&v->eg1);
    eg_release(&v->eg2);
}

/* cuts a voice within a few milliseconds for choking or retriggering. */
static void quick_stop(VOICE *v)
{
    release_voice(v);
    v->eg1.release = 0.005f * (1000.0f / (v->eg1.level > 1.0f ? v->eg1.level : 1.0f));
}

static void stop_part(SYNTH *s, int part, BOOL quick)
{
    for (int i = 0; i < VOICES; i++) {
        VOICE *v = &s->voices[i];
        if (v->active && v->part == part) {
            if (quick)
                quick_stop(v);
            else if (s->parts[part].sustain)
                v->sustained = TRUE;
            else
                release_voice(v);
        }
    }
}

static const INSTRUMENT *find_patch(SYNTH *s, DWORD patch, BYTE key, BYTE velocity)
{
    for (DWORD i = 0; i < s->instrument_count; i++) {
        const INSTRUMENT *in = &s->instruments[i];
        if (in->patch != patch)
            continue;
        for (DWORD r = 0; r < in->count; r++)
            if (key >= in->regions[r].key_low && key <= in->regions[r].key_high &&
                velocity >= in->regions[r].vel_low && velocity <= in->regions[r].vel_high)
                return in;
    }
    return nullptr;
}

static const INSTRUMENT *find_instrument(SYNTH *s, const PART *p, BYTE key, BYTE velocity)
{
    const INSTRUMENT *in = find_patch(s, p->patch, key, velocity);
    if (in)
        return in;
    if (p->patch & DRUMS)
        return find_patch(s, DRUMS, key, velocity);
    /* a variation the sound set lacks plays the capital tone of its program. */
    if (!s->xg)
        return find_patch(s, p->patch & 0x7F, key, velocity);
    if (((p->patch >> 16) & 0x7F) == 127) {
        in = find_patch(s, p->patch & 0x7F007F, key, velocity);
        return in ? in : find_patch(s, 0x7F0000, key, velocity);
    }
    return find_patch(s, p->patch & 0x7F, key, velocity);
}

static VOICE *allocate(SYNTH *s, DWORD priority)
{
    for (int i = 0; i < VOICES; i++)
        if (!s->voices[i].active)
            return &s->voices[i];
    VOICE *quiet = nullptr, *oldest = nullptr;
    for (int i = 0; i < VOICES; i++) {
        VOICE *v = &s->voices[i];
        if (v->priority > priority)
            continue;
        if (v->released && (!quiet || v->eg1.level < quiet->eg1.level))
            quiet = v;
        if (!oldest || v->started < oldest->started)
            oldest = v;
    }
    return quiet ? quiet : oldest;
}

static float part_tuning(const PART *p, BYTE key)
{
    return (float)(p->fine + (p->drums ? 0 : p->coarse) + p->scale[key % 12]);
}

static void note_on(SYNTH *s, int part, BYTE key, BYTE velocity)
{
    PART *p = &s->parts[part];
    const INSTRUMENT *in = find_instrument(s, p, key, velocity);
    if (!in)
        return;
    if (p->mono)
        stop_part(s, part, FALSE);
    DWORD serial = ++s->serial;
    for (DWORD r = 0; r < in->count; r++) {
        const REGION *region = &in->regions[r];
        if (key < region->key_low || key > region->key_high || velocity < region->vel_low ||
            velocity > region->vel_high || region->wave >= s->wave_count || !wave_ready(&s->waves[region->wave]))
            continue;
        for (int i = 0; i < VOICES; i++) {
            VOICE *v = &s->voices[i];
            if (!v->active || v->part != part)
                continue;
            if ((!(region->options & 1) && v->key == key && v->region == region) ||
                (region->group && v->patch == p->patch && v->region->group == region->group))
                quick_stop(v);
        }
        VOICE *v = allocate(s, p->priority);
        if (!v)
            return;
        const WAVE *w = &s->waves[region->wave];
        memset(v, 0, sizeof(*v));
        v->active = TRUE;
        v->part = part;
        v->key = key;
        v->serial = serial;
        v->patch = p->patch;
        v->region = region;
        v->wave = w;
        v->art = region->art;
        v->smp = region->has_wsmp ? region->wsmp : w->wsmp;
        if (v->smp.loop && (v->smp.loop_end > w->frames || v->smp.loop_start >= v->smp.loop_end))
            v->smp.loop = FALSE;
        v->priority = p->priority;
        v->started = s->clock;
        v->velocity = velocity;
        v->cents = (key - v->smp.unity) * 100.0f + v->smp.fine + v->art->key_pitch / 65536.0f * key / 128.0f;
        v->ratio = (float)w->rate / RATE;
        v->static_db = v->smp.db + g_curve[velocity] + s->master_db;
        eg_start(&v->eg1, &v->art->eg1, key, velocity);
        eg_start(&v->eg2, &v->art->eg2, key, velocity);
        float hz = 440.0f * exp2_approx((v->art->lfo_frequency / 65536.0f - 6900.0f) / 1200.0f);
        v->lfo_step = hz * BLOCK / RATE;
        v->lfo_delay = seconds(v->art->lfo_delay);
        float vib_hz = 440.0f * exp2_approx((v->art->vib_frequency / 65536.0f - 6900.0f) / 1200.0f);
        v->vib_step = vib_hz * BLOCK / RATE;
        v->vib_delay = seconds(v->art->vib_delay);
        v->left = v->right = 0.0f;
    }
}

static void note_off(SYNTH *s, int part, BYTE key)
{
    DWORD latest = 0;
    for (int i = 0; i < VOICES; i++) {
        VOICE *v = &s->voices[i];
        if (v->active && !v->released && !v->sustained && v->part == part && v->key == key && v->serial > latest)
            latest = v->serial;
    }
    for (int i = 0; i < VOICES; i++) {
        VOICE *v = &s->voices[i];
        if (v->active && !v->released && v->part == part && v->key == key && v->serial == latest) {
            if (s->parts[part].sustain)
                v->sustained = TRUE;
            else
                release_voice(v);
        }
    }
}

/* ------------------------------------------------------------------ messages */

static void data_entry(PART *p, BOOL msb)
{
    switch (p->rpn) {
    case 0:
        if (msb)
            p->bend_range = 100 * p->data_msb;
        break;
    case 1: {
        int value = p->data_msb << 7 | p->data_lsb;
        p->fine = 100 * (value - 8192) / 8192;
        break;
    }
    case 2:
        if (msb)
            p->coarse = 100 * (p->data_msb - 64);
        break;
    }
}

static void controller(SYNTH *s, int part, BYTE number, BYTE value)
{
    PART *p = &s->parts[part];
    switch (number) {
    case 0:
        if (s->gs)
            p->bank_msb = value;
        break;
    case 32:
        if (s->gs)
            p->bank_lsb = value;
        break;
    case 1:
        p->mod = value;
        break;
    case 6:
        p->data_msb = value;
        data_entry(p, TRUE);
        break;
    case 38:
        p->data_lsb = value;
        data_entry(p, FALSE);
        break;
    case 7:
        p->volume = value;
        break;
    case 10:
        p->pan = value;
        break;
    case 11:
        p->expression = value;
        break;
    case 64:
        p->sustain = value != 0;
        if (!p->sustain)
            for (int i = 0; i < VOICES; i++) {
                VOICE *v = &s->voices[i];
                if (v->active && v->part == part && v->sustained)
                    release_voice(v);
            }
        break;
    case 74:
        p->brightness = value;
        break;
    case 91:
        p->reverb = value;
        break;
    case 93:
        p->chorus = value;
        break;
    case 98:
    case 99:
        p->rpn = NULL_RPN;
        break;
    case 100:
        p->rpn = (WORD)((p->rpn & 0x3F80) | value);
        break;
    case 101:
        p->rpn = (WORD)((p->rpn & 0x7F) | value << 7);
        break;
    case 120:
        stop_part(s, part, TRUE);
        break;
    case 121:
        reset_controllers(p, value != 0);
        break;
    case 123:
        stop_part(s, part, FALSE);
        break;
    case 126:
        p->mono = TRUE;
        stop_part(s, part, TRUE);
        break;
    case 127:
        p->mono = FALSE;
        stop_part(s, part, TRUE);
        break;
    }
}

/* running status and system messages are dropped, as Windows' synth does. */
static void short_message(SYNTH *s, DWORD message)
{
    BYTE status = (BYTE)message, a = (BYTE)(message >> 8) & 0x7F, b = (BYTE)(message >> 16) & 0x7F;
    if (status < 0x80 || status >= 0xF0)
        return;
    BYTE channel = status & 15;
    for (int part = 0; part < 16; part++) {
        PART *p = &s->parts[part];
        if (p->channel != channel)
            continue;
        switch (status & 0xF0) {
        case 0x80:
            note_off(s, part, a);
            break;
        case 0x90:
            if (b)
                note_on(s, part, a, b);
            else
                note_off(s, part, a);
            break;
        case 0xB0:
            controller(s, part, a, b);
            break;
        case 0xC0:
            p->program = a;
            set_program(p);
            break;
        case 0xD0:
            p->pressure = a;
            break;
        case 0xE0:
            p->bend = (a | b << 7) - 8192;
            break;
        }
    }
}

/* gs parts are numbered with the drum part first: 0 is channel 10, 1 to 9 the first nine, then 10 to 15. */
static void sysex(SYNTH *s, const BYTE *d, DWORD n)
{
    if (n < 6 || d[0] != 0xF0)
        return;
    if (d[1] == 0x7E && d[3] == 0x09) {
        system_reset(s, FALSE, FALSE);
    } else if (n >= 11 && d[1] == 0x41 && d[3] == 0x42 && d[4] == 0x12 && d[5] == 0x40 && d[6] == 0x00 && d[7] == 0x7F) {
        system_reset(s, TRUE, FALSE);
    } else if (n >= 8 && d[1] == 0x43 && d[3] == 0x4C && d[4] == 0 && d[5] == 0 && d[6] == 0x7E && d[7] == 0) {
        system_reset(s, FALSE, TRUE);
    } else if (n >= 7 && d[1] == 0x7F && d[3] == 0x04 && d[4] == 0x01) {
        s->master_db = g_curve[d[6] & 0x7F];
    } else if (s->gs && n >= 9 && d[1] == 0x41 && d[3] == 0x42 && d[4] == 0x12 && d[5] == 0x40 && (d[6] & 0xF0) == 0x10) {
        int part = d[6] & 15;
        if (d[7] == 0x02 && d[8]) {
            s->parts[part].channel = (BYTE)((d[8] - 1) & 15);
        } else if (d[7] == 0x15) {
            s->parts[part].drums = d[8] != 0;
            for (int i = 0; i < 16; i++)
                reset_controllers(&s->parts[i], TRUE);
        } else if (d[7] == 0x40) {
            for (int i = 0; i < 12 && 8 + i < (int)n && d[8 + i] < 0x80; i++)
                s->parts[part].scale[i] = (SHORT)(d[8 + i] - 64);
        }
    }
}

/* ------------------------------------------------------------------ rendering */

/* one turn is 1; the sine comes from the table, which a quarter of a turn on makes the cosine. */
static float sine_turns(float turns)
{
    float x = (turns - (int)turns) * 256.0f;
    int i = (int)x;
    return g_sine[i & 255] + (g_sine[(i & 255) + 1] - g_sine[i & 255]) * (x - i);
}

/* sets the low pass filter of a voice for this block from the cutoff and resonance of its articulation. */
static void set_filter(VOICE *v, float lfo)
{
    const ARTICULATION *a = v->art;
    float cents = a->cutoff / 65536.0f + v->eg2.level / 1000.0f * a->eg2_cutoff / 65536.0f +
                  lfo * a->lfo_cutoff / 65536.0f + v->velocity / 127.0f * a->velocity_cutoff / 65536.0f;
    v->filtered = cents < FILTER_BYPASS_CENTS;
    if (!v->filtered)
        return;
    if (cents < 1500.0f)
        cents = 1500.0f;
    float turns = 440.0f * exp2_approx((cents - 6900.0f) / 1200.0f) / RATE;
    float sine = sine_turns(turns), cosine = sine_turns(turns + 0.25f);
    /* the resonance in decibels over a flat response; a half of it comes off the level, as a sound set is made for. */
    float peak_db = a->resonance / 655360.0f;
    float q = exp2_approx(peak_db * (3.32192809f / 20.0f));
    if (q < 0.7071f)
        q = 0.7071f;
    float alpha = sine / (2.0f * q), a0 = 1.0f + alpha;
    v->b0 = (1.0f - cosine) / 2.0f / a0;
    v->b1 = (1.0f - cosine) / a0;
    v->b2 = v->b0;
    v->a1 = -2.0f * cosine / a0;
    v->a2 = (1.0f - alpha) / a0;
    v->filter_gain = db_to_gain(-peak_db / 2.0f);
}

static void render_voice(SYNTH *s, VOICE *v, float *mix)
{
    const PART *p = &s->parts[v->part];
    const ARTICULATION *a = v->art;
    float dt = (float)BLOCK / RATE, lfo = 0.0f, vib = 0.0f;
    if (v->lfo_delay > 0.0f) {
        v->lfo_delay -= dt;
    } else {
        lfo = sine_turns(v->lfo_phase);
        v->lfo_phase += v->lfo_step;
        v->lfo_phase -= (int)v->lfo_phase;
    }
    if (v->vib_delay > 0.0f) {
        v->vib_delay -= dt;
    } else {
        vib = sine_turns(v->vib_phase);
        v->vib_phase += v->vib_step;
        v->vib_phase -= (int)v->vib_phase;
    }
    eg_step(&v->eg1, dt, TRUE);
    eg_step(&v->eg2, dt, FALSE);
    if (v->released && v->eg1.level < 1000.0f - 80.0f * 1000.0f / 96.0f) {
        v->active = FALSE;
        return;
    }
    float cents = v->cents + part_tuning(p, v->key) + (float)p->bend_range * p->bend / 8192.0f +
                  lfo * (a->lfo_pitch + a->cc1_pitch * p->mod / 128.0f) / 65536.0f +
                  vib * (a->vib_pitch + a->vib_cc1_pitch * p->mod / 128.0f) / 65536.0f +
                  v->eg2.level / 1000.0f * a->eg2_pitch / 65536.0f;
    UINT64 step = (UINT64)(v->ratio * exp2_approx(cents / 1200.0f) * 4294967296.0f);
    float db = v->static_db + (v->eg1.level - 1000.0f) * 96.0f / 1000.0f + lfo * a->lfo_db / 655360.0f +
               g_curve[p->volume] + g_curve[p->expression];
    int pan = p->pan + (int)((LONG64)a->pan * 127 / (1000 * 65536));
    pan = pan < 0 ? 0 : pan > 127 ? 127 : pan;
    float left = db_to_gain(db + g_pan[127 - pan]), right = db_to_gain(db + g_pan[pan]);
    set_filter(v, lfo);
    if (v->filtered) {
        left *= v->filter_gain;
        right *= v->filter_gain;
    }
    float dl = (left - v->left) / BLOCK, dr = (right - v->right) / BLOCK, gl = v->left, gr = v->right;
    const SHORT *data = v->wave->data;
    DWORD frames = v->wave->frames;
    for (int i = 0; i < BLOCK; i++) {
        DWORD at = (DWORD)(v->position >> 32);
        if (v->smp.loop && at >= v->smp.loop_end) {
            v->position -= (UINT64)(v->smp.loop_end - v->smp.loop_start) << 32;
            at = (DWORD)(v->position >> 32);
        }
        if (at + 1 >= frames) {
            v->active = FALSE;
            break;
        }
        DWORD next = at + 1;
        if (v->smp.loop && next >= v->smp.loop_end)
            next = v->smp.loop_start;
        float f = (UINT32)v->position / 4294967296.0f, x = data[at] + (data[next] - data[at]) * f;
        if (v->filtered) {
            float y = v->b0 * x + v->z1;
            v->z1 = v->b1 * x - v->a1 * y + v->z2;
            v->z2 = v->b2 * x - v->a2 * y;
            x = y;
        }
        gl += dl;
        gr += dr;
        mix[2 * i] += x * gl;
        mix[2 * i + 1] += x * gr;
        v->position += step;
    }
    v->left = left;
    v->right = right;
}

static void render(SYNTH *s, SHORT *out, UINT32 frames)
{
    for (UINT32 done = 0; done < frames; done += BLOCK) {
        float mix[2 * BLOCK] = {};
        for (int i = 0; i < VOICES; i++)
            if (s->voices[i].active)
                render_voice(s, &s->voices[i], mix);
        for (int i = 0; i < 2 * BLOCK; i++) {
            float x = mix[i];
            x = x > 32767.0f ? 32767.0f : x < -32767.0f ? -32767.0f : x;
            out[2 * done + i] = (SHORT)x;
        }
        s->clock += BLOCK;
    }
}

static DWORD WINAPI render_thread(void *arg)
{
    SYNTH *s = arg;
    while (!s->quit) {
        RtlEnterCriticalSection(&s->lock);
        while (MimeOutputQueued(&s->out) < QUEUE_FRAMES) {
            UINT32 room;
            BYTE *dst = MimeOutputBegin(&s->out, &room);
            room -= room % BLOCK;
            if (!room)
                break;
            if (room > 8 * BLOCK)
                room = 8 * BLOCK;
            render(s, (SHORT *)dst, room);
            MimeOutputCommit(&s->out, room);
        }
        MimeOutputStart(&s->out);
        RtlLeaveCriticalSection(&s->lock);
        Sleep(5);
    }
    return 0;
}

/* ------------------------------------------------------------------ the device */

static void unload(SYNTH *s)
{
    HANDLE heap = GetProcessHeap();
    if (s->instruments)
        for (DWORD i = 0; i < s->instrument_count; i++)
            HeapFree(heap, 0, s->instruments[i].regions);
    HeapFree(heap, 0, s->instruments);
    HeapFree(heap, 0, s->arts);
    if (s->waves)
        for (DWORD i = 0; i < s->wave_count; i++)
            if (s->waves[i].owned)
                HeapFree(heap, 0, (void *)s->waves[i].data);
    HeapFree(heap, 0, s->waves);
    if (s->image)
        UnmapViewOfFile(s->image);
    if (s->mapping)
        CloseHandle(s->mapping);
    if (s->file && s->file != INVALID_HANDLE_VALUE)
        CloseHandle(s->file);
    RtlDeleteCriticalSection(&s->lock);
    HeapFree(heap, 0, s);
}

/* the sound set must load for the device to open, as in windows. */
MMRESULT SynthOpen(SYNTH **out)
{
    *out = nullptr;
    tables();
    SYNTH *s = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*s));
    if (!s)
        return MMSYSERR_NOMEM;
    RtlInitializeCriticalSection(&s->lock);
    WCHAR path[MAX_PATH];
    if (!ExpandEnvironmentStringsW(L"%SystemRoot%\\system32\\drivers\\GM.DLS", path, MAX_PATH))
        lstrcpyW(path, L"C:\\Windows\\system32\\drivers\\GM.DLS");
    s->file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    DWORD size = s->file != INVALID_HANDLE_VALUE ? GetFileSize(s->file, nullptr) : 0;
    if (size && size != INVALID_FILE_SIZE)
        s->mapping = CreateFileMappingW(s->file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (s->mapping)
        s->image = MapViewOfFile(s->mapping, FILE_MAP_READ, 0, 0, 0);
    if (!s->image || !load(s, s->image, size)) {
        unload(s);
        return MMSYSERR_ERROR;
    }
    system_reset(s, FALSE, FALSE);
    s->level = 1.0f;
    if (!NT_SUCCESS(MimeOutputOpen(&s->out, RATE, 2, 16, FALSE))) {
        unload(s);
        return MMSYSERR_NODRIVER;
    }
    s->thread = CreateThread(nullptr, 0, render_thread, s, 0, nullptr);
    if (!s->thread) {
        MimeOutputClose(&s->out);
        unload(s);
        return MMSYSERR_NOMEM;
    }
    SetThreadPriority(s->thread, THREAD_PRIORITY_TIME_CRITICAL);
    *out = s;
    return MMSYSERR_NOERROR;
}

void SynthClose(SYNTH *s)
{
    s->quit = TRUE;
    WaitForSingleObject(s->thread, INFINITE);
    CloseHandle(s->thread);
    MimeOutputStop(&s->out);
    MimeOutputClose(&s->out);
    unload(s);
}

void SynthShort(SYNTH *s, DWORD message)
{
    RtlEnterCriticalSection(&s->lock);
    short_message(s, message);
    RtlLeaveCriticalSection(&s->lock);
}

/* up to four bytes are one short message; longer ones are a system exclusive message. */
void SynthLong(SYNTH *s, const BYTE *data, DWORD size)
{
    RtlEnterCriticalSection(&s->lock);
    if (size <= 4) {
        DWORD message = 0;
        for (DWORD i = 0; i < size && i < 3; i++)
            message |= (DWORD)data[i] << (8 * i);
        short_message(s, message);
    } else {
        sysex(s, data, size);
    }
    RtlLeaveCriticalSection(&s->lock);
}

/* sends all-notes-off and resets each channel's controllers, allowing notes to finish their release. */
void SynthReset(SYNTH *s)
{
    RtlEnterCriticalSection(&s->lock);
    for (DWORD c = 0; c < 16; c++) {
        short_message(s, 0xB0 | c | 123 << 8);
        short_message(s, 0xB0 | c | 121 << 8);
    }
    RtlLeaveCriticalSection(&s->lock);
}

void SynthSetVolume(SYNTH *s, float level)
{
    s->level = level;
    MimeOutputVolume(&s->out, level);
}

float SynthGetVolume(SYNTH *s)
{
    return s->level;
}
