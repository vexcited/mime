/*
 * gsm 06.10 full rate speech coding, the fixed point procedures of the
 * etsi recommendation, which windows' codec follows to the bit: 160
 * samples of 13 bits make a frame of 76 parameters.
 */
#include "gsm.h"

static const SHORT A[8] = {20480, 20480, 20480, 20480, 13964, 15360, 8534, 9036};
static const SHORT B[8] = {0, 0, 2048, -2560, 94, -1792, -341, -1144};
static const SHORT MIC[8] = {-32, -32, -16, -16, -8, -8, -4, -4};
static const SHORT MAC[8] = {31, 31, 15, 15, 7, 7, 3, 3};
static const SHORT INVA[8] = {13107, 13107, 13107, 13107, 19223, 17476, 31454, 29708};
static const SHORT DLB[4] = {6554, 16384, 26214, 32767};
static const SHORT QLB[4] = {3277, 11469, 21299, 32767};
static const SHORT H[11] = {-134, -374, 0, 2054, 5741, 8192, 5741, 2054, 0, -374, -134};
static const SHORT NRFAC[8] = {29128, 26215, 23832, 21846, 20165, 18725, 17476, 16384};
static const SHORT FAC[8] = {18431, 20479, 22527, 24575, 26623, 28671, 30719, 32767};

/* ------------------------------------------------------------------ the basic operations */

static SHORT sat16(LONG v)
{
    return (SHORT)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

static SHORT add(SHORT a, SHORT b)
{
    return sat16((LONG)a + b);
}

static SHORT sub(SHORT a, SHORT b)
{
    return sat16((LONG)a - b);
}

static SHORT mult(SHORT a, SHORT b)
{
    return a == -32768 && b == -32768 ? 32767 : (SHORT)(((LONG)a * b) >> 15);
}

static SHORT mult_r(SHORT a, SHORT b)
{
    return a == -32768 && b == -32768 ? 32767 : (SHORT)(((LONG)a * b + 16384) >> 15);
}

static SHORT abs16(SHORT a)
{
    return a == -32768 ? 32767 : a < 0 ? (SHORT)-a : a;
}

static LONG l_add(LONG a, LONG b)
{
    LONG64 v = (LONG64)a + b;
    return v > 0x7FFFFFFF ? 0x7FFFFFFF : v < -0x7FFFFFFFLL - 1 ? (LONG)0x80000000 : (LONG)v;
}

static LONG l_mult(SHORT a, SHORT b)
{
    return (LONG)a * b * 2;
}

/* left shifts that put bit 30 of a positive value. */
static SHORT norm(LONG a)
{
    SHORT n = 0;
    while (a < 0x40000000) {
        a <<= 1;
        n++;
    }
    return n;
}

static SHORT asl(SHORT a, int n)
{
    if (n >= 16)
        return 0;
    if (n <= -16)
        return a < 0 ? -1 : 0;
    return n < 0 ? (SHORT)(a >> -n) : (SHORT)(a << n);
}

static SHORT asr(SHORT a, int n)
{
    if (n >= 16)
        return a < 0 ? -1 : 0;
    if (n <= -16)
        return 0;
    return n < 0 ? (SHORT)(a << -n) : (SHORT)(a >> n);
}

/* the 15 bit quotient of num by denum, num not above denum. */
static SHORT divide(SHORT num, SHORT denum)
{
    LONG l = num;
    SHORT quotient = 0;
    for (int k = 0; k < 15; k++) {
        quotient = (SHORT)(quotient << 1);
        l <<= 1;
        if (l >= denum) {
            l -= denum;
            quotient++;
        }
    }
    return quotient;
}

/* ------------------------------------------------------------------ short term analysis, 4.2.4 to 4.2.10 */

static void autocorrelation(SHORT *s, LONG *acf)
{
    SHORT smax = 0;
    for (int k = 0; k < 160; k++) {
        SHORT a = abs16(s[k]);
        if (a > smax)
            smax = a;
    }
    SHORT scale = smax ? (SHORT)(4 - norm((LONG)smax << 16)) : 0;
    if (scale > 0)
        for (int k = 0; k < 160; k++)
            s[k] = mult_r(s[k], (SHORT)(16384 >> (scale - 1)));
    for (int k = 0; k <= 8; k++) {
        LONG sum = 0;
        for (int i = k; i < 160; i++)
            sum = l_add(sum, l_mult(s[i], s[i - k]));
        acf[k] = sum;
    }
    if (scale > 0)
        for (int k = 0; k < 160; k++)
            s[k] = (SHORT)(s[k] << scale);
}

/* the reflection coefficients by schur's recursion. */
static void reflection(const LONG *l_acf, SHORT *r)
{
    if (!l_acf[0]) {
        for (int i = 0; i < 8; i++)
            r[i] = 0;
        return;
    }
    SHORT shift = norm(l_acf[0]), acf[9], p[9], k[9];
    for (int i = 0; i <= 8; i++)
        acf[i] = (SHORT)((l_acf[i] << shift) >> 16);
    for (int i = 1; i <= 7; i++)
        k[i] = acf[i];
    for (int i = 0; i <= 8; i++)
        p[i] = acf[i];
    for (int n = 1; n <= 8; n++) {
        SHORT temp = abs16(p[1]);
        if (p[0] < temp) {
            for (int i = n; i <= 8; i++)
                r[i - 1] = 0;
            return;
        }
        SHORT rn = divide(temp, p[0]);
        if (p[1] > 0)
            rn = (SHORT)-rn;
        r[n - 1] = rn;
        if (n == 8)
            return;
        p[0] = add(p[0], mult_r(p[1], rn));
        for (int m = 1; m <= 8 - n; m++) {
            p[m] = add(p[m + 1], mult_r(k[m], rn));
            k[m] = add(k[m], mult_r(p[m + 1], rn));
        }
    }
}

/* log area ratios, quantized and offset to be positive. */
static void quantize_lar(const SHORT *r, SHORT *larc)
{
    for (int i = 0; i < 8; i++) {
        SHORT temp = abs16(r[i]);
        if (temp < 22118)
            temp >>= 1;
        else if (temp < 31130)
            temp = sub(temp, 11059);
        else
            temp = (SHORT)(sub(temp, 26112) << 2);
        SHORT lar = r[i] < 0 ? sub(0, temp) : temp;
        temp = add(add(mult(A[i], lar), B[i]), 256);
        temp >>= 9;
        if (temp > MAC[i])
            temp = MAC[i];
        if (temp < MIC[i])
            temp = MIC[i];
        larc[i] = sub(temp, MIC[i]);
    }
}

static void decode_lar(const SHORT *larc, SHORT *larpp)
{
    for (int i = 0; i < 8; i++) {
        SHORT temp = (SHORT)(add(larc[i], MIC[i]) << 10);
        temp = sub(temp, (SHORT)(B[i] << 1));
        temp = mult_r(INVA[i], temp);
        larpp[i] = add(temp, temp);
    }
}

/* the log area ratios of a segment of the frame, from the last frame's and this one's. */
static void interpolate(const SHORT *previous, const SHORT *current, int segment, SHORT *larp)
{
    for (int i = 0; i < 8; i++) {
        switch (segment) {
        case 0:
            larp[i] = add(add(previous[i] >> 2, current[i] >> 2), previous[i] >> 1);
            break;
        case 1:
            larp[i] = add(previous[i] >> 1, current[i] >> 1);
            break;
        case 2:
            larp[i] = add(add(previous[i] >> 2, current[i] >> 2), current[i] >> 1);
            break;
        default:
            larp[i] = current[i];
            break;
        }
    }
}

static void lar_to_rp(SHORT *larp)
{
    for (int i = 0; i < 8; i++) {
        SHORT temp = abs16(larp[i]);
        if (temp < 11059)
            temp = (SHORT)(temp << 1);
        else if (temp < 20070)
            temp = (SHORT)(temp + 11059);
        else
            temp = add(temp >> 2, 26112);
        larp[i] = larp[i] < 0 ? (SHORT)-temp : temp;
    }
}

static const int segment_start[5] = {0, 13, 27, 40, 160};

static void analysis_filter(SHORT *u, const SHORT *rp, int count, SHORT *s)
{
    for (int k = 0; k < count; k++) {
        SHORT di = s[k], sav = di;
        for (int i = 0; i < 8; i++) {
            SHORT ui = u[i];
            u[i] = sav;
            sav = add(ui, mult_r(rp[i], di));
            di = add(di, mult_r(rp[i], ui));
        }
        s[k] = di;
    }
}

static void synthesis_filter(SHORT *v, const SHORT *rrp, int count, const SHORT *wt, SHORT *sr)
{
    for (int k = 0; k < count; k++) {
        SHORT sri = wt[k];
        for (int i = 7; i >= 0; i--) {
            sri = sub(sri, mult_r(rrp[i], v[i]));
            v[i + 1] = add(v[i], mult_r(rrp[i], sri));
        }
        sr[k] = v[0] = sri;
    }
}

/* ------------------------------------------------------------------ long term prediction, 4.2.11 and 4.2.12 */

static void ltp_parameters(const SHORT *d, const SHORT *dp, SHORT *bc_out, SHORT *nc_out)
{
    SHORT dmax = 0;
    for (int k = 0; k < 40; k++) {
        SHORT a = abs16(d[k]);
        if (a > dmax)
            dmax = a;
    }
    SHORT temp = dmax ? norm((LONG)dmax << 16) : 0;
    SHORT scale = temp > 6 ? 0 : (SHORT)(6 - temp);
    SHORT wt[40];
    for (int k = 0; k < 40; k++)
        wt[k] = (SHORT)(d[k] >> scale);
    LONG lmax = 0;
    SHORT nc = 40;
    for (int lambda = 40; lambda <= 120; lambda++) {
        LONG sum = 0;
        for (int k = 0; k < 40; k++)
            sum += (LONG)wt[k] * dp[k - lambda];
        if (sum > lmax) {
            nc = (SHORT)lambda;
            lmax = sum;
        }
    }
    *nc_out = nc;
    lmax = (LONG)((ULONG)lmax << 1) >> (6 - scale);
    LONG power = 0;
    for (int k = 0; k < 40; k++) {
        LONG v = dp[k - nc] >> 3;
        power += v * v;
    }
    power = power >= 0x40000000 ? 0x7FFFFFFF : power << 1;
    if (lmax <= 0) {
        *bc_out = 0;
        return;
    }
    if (lmax >= power) {
        *bc_out = 3;
        return;
    }
    SHORT shift = norm(power);
    SHORT r = (SHORT)((lmax << shift) >> 16), s = (SHORT)((power << shift) >> 16);
    SHORT bc = 0;
    while (bc <= 2 && r > mult(s, DLB[bc]))
        bc++;
    *bc_out = bc;
}

/* ------------------------------------------------------------------ regular pulse excitation, 4.2.13 to 4.2.17 */

static void exp_mant(SHORT xmaxc, SHORT *exp_out, SHORT *mant_out)
{
    SHORT exp = 0;
    if (xmaxc > 15)
        exp = (SHORT)((xmaxc >> 3) - 1);
    SHORT mant = sub(xmaxc, (SHORT)(exp << 3));
    if (!mant) {
        exp = -4;
        mant = 7;
    } else {
        while (mant <= 7) {
            mant = (SHORT)(mant << 1 | 1);
            exp--;
        }
        mant -= 8;
    }
    *exp_out = exp;
    *mant_out = mant;
}

static void inverse_apcm(SHORT exp, SHORT mant, const SHORT *xmc, SHORT *xmp)
{
    SHORT temp1 = FAC[mant], temp2 = sub(6, exp), temp3 = asl(1, sub(temp2, 1));
    for (int i = 0; i < 13; i++) {
        SHORT temp = (SHORT)(sub((SHORT)(xmc[i] << 1), 7) << 12);
        temp = add(mult_r(temp1, temp), temp3);
        xmp[i] = asr(temp, temp2);
    }
}

static void rpe_encoding(const SHORT *e, SHORT *xmaxc_out, SHORT *mc_out, SHORT *xmc, SHORT *ep)
{
    /* the weighting filter over e with five zeros at each side. */
    SHORT wt[50] = {}, x[40];
    for (int k = 0; k < 40; k++)
        wt[k + 5] = e[k];
    for (int k = 0; k < 40; k++) {
        LONG sum = 8192;
        for (int i = 0; i <= 10; i++)
            sum = l_add(sum, l_mult(wt[k + i], H[i]));
        sum = l_add(sum, sum);
        sum = l_add(sum, sum);
        x[k] = (SHORT)(sum >> 16);
    }
    /* the grid of most energy. */
    LONG em = 0;
    SHORT mc = 0;
    for (int m = 0; m <= 3; m++) {
        LONG sum = 0;
        for (int i = 0; i <= 12; i++) {
            SHORT v = (SHORT)(x[m + 3 * i] >> 2);
            sum = l_add(sum, l_mult(v, v));
        }
        if (sum > em) {
            mc = (SHORT)m;
            em = sum;
        }
    }
    SHORT xm[13];
    for (int i = 0; i < 13; i++)
        xm[i] = x[mc + 3 * i];
    /* adaptive pcm quantization of the block maximum and the pulses. */
    SHORT xmax = 0;
    for (int i = 0; i < 13; i++) {
        SHORT a = abs16(xm[i]);
        if (a > xmax)
            xmax = a;
    }
    SHORT exp = 0, temp = (SHORT)(xmax >> 9);
    BOOL stop = FALSE;
    for (int i = 0; i <= 5; i++) {
        stop |= temp <= 0;
        temp >>= 1;
        if (!stop)
            exp = add(exp, 1);
    }
    SHORT xmaxc = add(asr(xmax, add(exp, 5)), (SHORT)(exp << 3));
    SHORT mant;
    exp_mant(xmaxc, &exp, &mant);
    SHORT shift = sub(6, exp), factor = NRFAC[mant];
    for (int i = 0; i < 13; i++) {
        SHORT v = mult(asl(xm[i], shift), factor);
        xmc[i] = add((SHORT)(v >> 12), 4);
    }
    SHORT xmp[13];
    inverse_apcm(exp, mant, xmc, xmp);
    for (int k = 0; k < 40; k++)
        ep[k] = 0;
    for (int i = 0; i < 13; i++)
        ep[mc + 3 * i] = xmp[i];
    *xmaxc_out = xmaxc;
    *mc_out = mc;
}

/* ------------------------------------------------------------------ frames */

void GsmEncode(GSM_ENCODER *g, const SHORT *in, GSM_FRAME *f)
{
    /* down scaling, offset compensation and preemphasis, 4.2.1 to 4.2.3. */
    SHORT so[160];
    for (int k = 0; k < 160; k++) {
        SHORT s0 = (SHORT)((in[k] >> 3) << 2);
        SHORT s1 = sub(s0, g->z1);
        g->z1 = s0;
        LONG ls2 = (LONG)s1 << 15;
        SHORT msp = (SHORT)(g->l_z2 >> 15), lsp = (SHORT)(g->l_z2 - ((LONG)msp << 15));
        ls2 = l_add(ls2, mult_r(lsp, 32735));
        g->l_z2 = l_add((LONG)msp * 32735, ls2);
        LONG temp = l_add(g->l_z2, 16384);
        msp = mult_r(g->mp, -28180);
        g->mp = (SHORT)(temp >> 15);
        so[k] = add(g->mp, msp);
    }
    LONG acf[9];
    SHORT r[8];
    autocorrelation(so, acf);
    reflection(acf, r);
    quantize_lar(r, f->larc);

    SHORT larpp[8], larp[8];
    decode_lar(f->larc, larpp);
    for (int segment = 0; segment < 4; segment++) {
        interpolate(g->larpp, larpp, segment, larp);
        lar_to_rp(larp);
        analysis_filter(g->u, larp, segment_start[segment + 1] - segment_start[segment], so + segment_start[segment]);
    }
    memcpy(g->larpp, larpp, sizeof(larpp));

    /* the four subframes of 40 samples: long term prediction, then the excitation. */
    for (int j = 0; j < 4; j++) {
        const SHORT *d = so + 40 * j;
        SHORT *dp = g->dp + 120;
        ltp_parameters(d, dp, &f->bc[j], &f->nc[j]);
        SHORT e[40], dpp[40], ep[40];
        SHORT bp = QLB[f->bc[j]];
        for (int k = 0; k < 40; k++) {
            dpp[k] = mult_r(bp, dp[k - f->nc[j]]);
            e[k] = sub(d[k], dpp[k]);
        }
        rpe_encoding(e, &f->xmaxc[j], &f->mc[j], f->xmc[j], ep);
        memmove(g->dp, g->dp + 40, 80 * sizeof(SHORT));
        for (int k = 0; k < 40; k++)
            g->dp[80 + k] = add(ep[k], dpp[k]);
    }
}

void GsmDecode(GSM_DECODER *g, const GSM_FRAME *f, SHORT *out)
{
    SHORT wt[160];
    for (int j = 0; j < 4; j++) {
        SHORT exp, mant, xmp[13], erp[40] = {};
        exp_mant(f->xmaxc[j], &exp, &mant);
        inverse_apcm(exp, mant, f->xmc[j], xmp);
        for (int i = 0; i < 13; i++)
            erp[f->mc[j] + 3 * i] = xmp[i];
        /* long term synthesis, a lag out of range taking the last one. */
        SHORT nr = f->nc[j] < 40 || f->nc[j] > 120 ? g->nrp : f->nc[j];
        g->nrp = nr;
        SHORT brp = QLB[f->bc[j]];
        SHORT *drp = g->drp + 120;
        for (int k = 0; k < 40; k++)
            drp[k] = add(erp[k], mult_r(brp, drp[k - nr]));
        memmove(g->drp, g->drp + 40, 120 * sizeof(SHORT));
        memcpy(wt + 40 * j, g->drp + 80, 40 * sizeof(SHORT));
    }
    SHORT larpp[8], larp[8], sr[160];
    decode_lar(f->larc, larpp);
    for (int segment = 0; segment < 4; segment++) {
        interpolate(g->larpp, larpp, segment, larp);
        lar_to_rp(larp);
        int start = segment_start[segment];
        synthesis_filter(g->v, larp, segment_start[segment + 1] - start, wt + start, sr + start);
    }
    memcpy(g->larpp, larpp, sizeof(larpp));
    /* deemphasis, upscaling and truncation to 13 bits. */
    for (int k = 0; k < 160; k++) {
        g->msr = add(sr[k], mult_r(g->msr, 28180));
        out[k] = (SHORT)(add(g->msr, g->msr) & 0xFFF8);
    }
}

/* ------------------------------------------------------------------ the 65 byte blocks of two frames */

typedef struct BITS {
    BYTE *p;
    unsigned bit;
} BITS;

static void put_bits(BITS *b, unsigned value, int count)
{
    for (int i = 0; i < count; i++, b->bit++) {
        if (!(b->bit & 7))
            b->p[b->bit >> 3] = 0;
        b->p[b->bit >> 3] |= (BYTE)(((value >> i) & 1) << (b->bit & 7));
    }
}

static SHORT get_bits(BITS *b, int count)
{
    unsigned value = 0;
    for (int i = 0; i < count; i++, b->bit++)
        value |= (unsigned)((b->p[b->bit >> 3] >> (b->bit & 7)) & 1) << i;
    return (SHORT)value;
}

static const int lar_bits[8] = {6, 6, 5, 5, 4, 4, 3, 3};

/* the parameters in their order, each from its lowest bit, the second frame from bit 260. */
void GsmPack(const GSM_FRAME frames[2], BYTE *out)
{
    BITS b = {out, 0};
    for (int n = 0; n < 2; n++) {
        const GSM_FRAME *f = &frames[n];
        for (int i = 0; i < 8; i++)
            put_bits(&b, (unsigned)f->larc[i], lar_bits[i]);
        for (int j = 0; j < 4; j++) {
            put_bits(&b, (unsigned)f->nc[j], 7);
            put_bits(&b, (unsigned)f->bc[j], 2);
            put_bits(&b, (unsigned)f->mc[j], 2);
            put_bits(&b, (unsigned)f->xmaxc[j], 6);
            for (int i = 0; i < 13; i++)
                put_bits(&b, (unsigned)f->xmc[j][i], 3);
        }
    }
}

void GsmUnpack(const BYTE *in, GSM_FRAME frames[2])
{
    BITS b = {(BYTE *)in, 0};
    for (int n = 0; n < 2; n++) {
        GSM_FRAME *f = &frames[n];
        for (int i = 0; i < 8; i++)
            f->larc[i] = get_bits(&b, lar_bits[i]);
        for (int j = 0; j < 4; j++) {
            f->nc[j] = get_bits(&b, 7);
            f->bc[j] = get_bits(&b, 2);
            f->mc[j] = get_bits(&b, 2);
            f->xmaxc[j] = get_bits(&b, 6);
            for (int i = 0; i < 13; i++)
                f->xmc[j][i] = get_bits(&b, 3);
        }
    }
}

void GsmReset(GSM_ENCODER *e, GSM_DECODER *d)
{
    memset(e, 0, sizeof(*e));
    memset(d, 0, sizeof(*d));
    d->nrp = 40;
}
