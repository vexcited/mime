#include "internal.h"

#define L(id_, cf_, bd_, nc_, a_, slots_, bits_, pm_, ...) {id_, cf_, bd_, nc_, a_, slots_, bits_, pm_, {__VA_ARGS__}}

static const JxrLayout k_layouts[] = {
    L(MIME_JXR_BLACKWHITE, MIME_JXR_CF_YONLY, MIME_JXR_BD1WHITE1, 1, 0, 1, 1, false, 0),
    L(MIME_JXR_GRAY8, MIME_JXR_CF_YONLY, MIME_JXR_BD8, 1, 0, 1, 8, false, 0),
    L(MIME_JXR_BGR555, MIME_JXR_CF_RGB, MIME_JXR_BD5, 3, 0, 1, 16, false, 0),
    L(MIME_JXR_BGR565, MIME_JXR_CF_RGB, MIME_JXR_BD565, 3, 0, 1, 16, false, 0),
    L(MIME_JXR_GRAY16, MIME_JXR_CF_YONLY, MIME_JXR_BD16, 1, 0, 1, 16, false, 0),
    L(MIME_JXR_BGR24, MIME_JXR_CF_RGB, MIME_JXR_BD8, 3, 0, 3, 24, false, 2, 1, 0),
    L(MIME_JXR_RGB24, MIME_JXR_CF_RGB, MIME_JXR_BD8, 3, 0, 3, 24, false, 0, 1, 2),
    L(MIME_JXR_BGR32, MIME_JXR_CF_RGB, MIME_JXR_BD8, 3, 0, 4, 32, false, 2, 1, 0),
    L(MIME_JXR_BGRA32, MIME_JXR_CF_RGB, MIME_JXR_BD8, 3, 1, 4, 32, false, 2, 1, 0, 3),
    L(MIME_JXR_PBGRA32, MIME_JXR_CF_RGB, MIME_JXR_BD8, 3, 1, 4, 32, true, 2, 1, 0, 3),
    L(MIME_JXR_GRAY32F, MIME_JXR_CF_YONLY, MIME_JXR_BD32F, 1, 0, 1, 32, false, 0),
    L(MIME_JXR_RGB48FIXED, MIME_JXR_CF_RGB, MIME_JXR_BD16S, 3, 0, 3, 48, false, 0, 1, 2),
    L(MIME_JXR_GRAY16FIXED, MIME_JXR_CF_YONLY, MIME_JXR_BD16S, 1, 0, 1, 16, false, 0),
    L(MIME_JXR_BGR101010, MIME_JXR_CF_RGB, MIME_JXR_BD10, 3, 0, 1, 32, false, 0),
    L(MIME_JXR_RGB48, MIME_JXR_CF_RGB, MIME_JXR_BD16, 3, 0, 3, 48, false, 0, 1, 2),
    L(MIME_JXR_RGBA64, MIME_JXR_CF_RGB, MIME_JXR_BD16, 3, 1, 4, 64, false, 0, 1, 2, 3),
    L(MIME_JXR_PRGBA64, MIME_JXR_CF_RGB, MIME_JXR_BD16, 3, 1, 4, 64, true, 0, 1, 2, 3),
    L(MIME_JXR_RGB96FIXED, MIME_JXR_CF_RGB, MIME_JXR_BD32S, 3, 0, 3, 96, false, 0, 1, 2),
    L(MIME_JXR_RGBA128F, MIME_JXR_CF_RGB, MIME_JXR_BD32F, 3, 1, 4, 128, false, 0, 1, 2, 3),
    L(MIME_JXR_PRGBA128F, MIME_JXR_CF_RGB, MIME_JXR_BD32F, 3, 1, 4, 128, true, 0, 1, 2, 3),
    L(MIME_JXR_RGB128F, MIME_JXR_CF_RGB, MIME_JXR_BD32F, 3, 0, 4, 128, false, 0, 1, 2),
    L(MIME_JXR_CMYK32, MIME_JXR_CF_CMYK, MIME_JXR_BD8, 4, 0, 4, 32, false, 0, 1, 2, 3),
    L(MIME_JXR_RGBA64FIXED, MIME_JXR_CF_RGB, MIME_JXR_BD16S, 3, 1, 4, 64, false, 0, 1, 2, 3),
    L(MIME_JXR_RGBA128FIXED, MIME_JXR_CF_RGB, MIME_JXR_BD32S, 3, 1, 4, 128, false, 0, 1, 2, 3),
    L(MIME_JXR_CMYK64, MIME_JXR_CF_CMYK, MIME_JXR_BD16, 4, 0, 4, 64, false, 0, 1, 2, 3),
    L(MIME_JXR_CMYKA40, MIME_JXR_CF_CMYK, MIME_JXR_BD8, 4, 1, 5, 40, false, 0, 1, 2, 3, 4),
    L(MIME_JXR_CMYKA80, MIME_JXR_CF_CMYK, MIME_JXR_BD16, 4, 1, 5, 80, false, 0, 1, 2, 3, 4),
    L(MIME_JXR_RGBA64HALF, MIME_JXR_CF_RGB, MIME_JXR_BD16F, 3, 1, 4, 64, false, 0, 1, 2, 3),
    L(MIME_JXR_RGB48HALF, MIME_JXR_CF_RGB, MIME_JXR_BD16F, 3, 0, 3, 48, false, 0, 1, 2),
    L(MIME_JXR_RGBE32, MIME_JXR_CF_RGBE, MIME_JXR_BD8, 3, 0, 4, 32, false, 0, 1, 2, 3),
    L(MIME_JXR_GRAY16HALF, MIME_JXR_CF_YONLY, MIME_JXR_BD16F, 1, 0, 1, 16, false, 0),
    L(MIME_JXR_GRAY32FIXED, MIME_JXR_CF_YONLY, MIME_JXR_BD32S, 1, 0, 1, 32, false, 0),
    L(MIME_JXR_RGB64FIXED, MIME_JXR_CF_RGB, MIME_JXR_BD16S, 3, 0, 4, 64, false, 0, 1, 2),
    L(MIME_JXR_RGB128FIXED, MIME_JXR_CF_RGB, MIME_JXR_BD32S, 3, 0, 4, 128, false, 0, 1, 2),
    L(MIME_JXR_RGB64HALF, MIME_JXR_CF_RGB, MIME_JXR_BD16F, 3, 0, 4, 64, false, 0, 1, 2),
};

bool jxr_find_layout(uint8_t id, JxrLayout *l)
{
    for (size_t i = 0; i < sizeof(k_layouts) / sizeof(k_layouts[0]); i++)
        if (k_layouts[i].id == id) {
            *l = k_layouts[i];
            return true;
        }
    /* 3 to 8 channels of 8 or 16 bits, with or without alpha. */
    int base = -1;
    bool wide = false, alpha = false;
    if (id >= MIME_JXR_3CH8 && id <= MIME_JXR_3CH8 + 5) {
        base = MIME_JXR_3CH8;
    } else if (id >= MIME_JXR_3CH16 && id <= MIME_JXR_3CH16 + 5) {
        base = MIME_JXR_3CH16;
        wide = true;
    } else if (id >= MIME_JXR_3CHA8 && id <= MIME_JXR_3CHA8 + 5) {
        base = MIME_JXR_3CHA8;
        alpha = true;
    } else if (id >= MIME_JXR_3CHA16 && id <= MIME_JXR_3CHA16 + 5) {
        base = MIME_JXR_3CHA16;
        wide = alpha = true;
    }
    if (base < 0)
        return false;
    int nc = 3 + id - base;
    l->id = id;
    l->cf = MIME_JXR_CF_NCOMPONENT;
    l->bd = wide ? MIME_JXR_BD16 : MIME_JXR_BD8;
    l->nc = (uint8_t)nc;
    l->alpha = alpha;
    l->slots = (uint8_t)(nc + alpha);
    l->bits = (uint8_t)(l->slots * (wide ? 16 : 8));
    l->premul = false;
    for (int i = 0; i < l->slots; i++)
        l->pos[i] = (uint8_t)i;
    return true;
}

bool mime_jxr_layout(uint8_t layout, unsigned *bits, unsigned *channels, bool *alpha)
{
    JxrLayout l;
    if (!jxr_find_layout(layout, &l))
        return false;
    if (bits)
        *bits = l.bits;
    if (channels)
        *channels = l.nc;
    if (alpha)
        *alpha = l.alpha;
    return true;
}
