/*
 * outline walker shared by the parser and the rasterizer.
 */
#ifndef MIME_LIB_TTF_INTERNAL_H
#define MIME_LIB_TTF_INTERNAL_H

#include "ttf.h"

/*
 * receives the points of each contour in order, in font units with composite
 * transforms applied, then end once the contour is complete.
 */
typedef struct MimeTtfSink {
    void (*point)(void *ctx, float x, float y, bool on_curve);
    void (*end)(void *ctx);
    void *ctx;
} MimeTtfSink;

/* streams every contour of a glyph. returns false on malformed data. */
bool mime_ttf_walk_outline(const MimeTtfFont *font, uint16_t glyph, const MimeTtfSink *sink);

#endif
