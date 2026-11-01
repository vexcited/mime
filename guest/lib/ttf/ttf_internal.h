/* outline walker shared by the parser and the rasterizer. */
#ifndef MIME_LIB_TTF_INTERNAL_H
#define MIME_LIB_TTF_INTERNAL_H

#include "ttf.h"

/* gets each contour's points in order, in font units with composite transforms applied, then end. */
typedef struct MimeTtfSink {
    void (*point)(void *ctx, float x, float y, bool on_curve);
    void (*end)(void *ctx);
    void *ctx;
} MimeTtfSink;

/* false on malformed data. */
bool mime_ttf_walk_outline(const MimeTtfFont *font, uint16_t glyph, const MimeTtfSink *sink);

/* the glyf data of a glyph, empty for a blank glyph. false when out of range. */
bool mime_ttf_glyph_data(const MimeTtfFont *font, uint16_t glyph, MimeTtfTable *out);

/* walks the hinted outline: points in pixels, y up, relative to the glyph origin. false when hinting failed. */
bool mime_ttf_hint_walk(const MimeTtfHinter *hinter, uint16_t glyph, const MimeTtfSink *sink);

#endif
