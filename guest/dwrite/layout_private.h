/*
 * layout_private.h: the structures of a text layout, shared by the building of lines in layout.c and linebuild.c
 * and the interface in layoutapi.c.
 */
#ifndef DW_LAYOUT_PRIVATE_H
#define DW_LAYOUT_PRIVATE_H

#include "text_private.h"

#define DW_OPEN_END 0xFFFFFFFFu

/* the properties a range of text can have, one by one for getting and setting. */
enum {
    P_FAMILY,
    P_COLLECTION,
    P_WEIGHT,
    P_STYLE,
    P_STRETCH,
    P_SIZE,
    P_UNDERLINE,
    P_STRIKE,
    P_EFFECT,
    P_INLINE,
    P_TYPOGRAPHY,
    P_LOCALE,
    P_KERNING,
    P_SPACING,
    P_AXES,
    P_COUNT
};

typedef struct DwSpan {
    UINT32 start, end; /* end is exclusive, DW_OPEN_END for the last span. */
    WCHAR *family;
    IDWriteFontCollection *collection;
    DWRITE_FONT_WEIGHT weight;
    DWRITE_FONT_STYLE style;
    DWRITE_FONT_STRETCH stretch;
    FLOAT size;
    WCHAR *locale;
    BOOL underline, strike;
    IUnknown *effect;
    IDWriteInlineObject *inline_object;
    IDWriteTypography *typography;
    BOOL kerning;
    FLOAT lead_spacing, trail_spacing, min_advance;
    DWRITE_FONT_AXIS_VALUE *axes;
    UINT32 axis_count;
} DwSpan;

/* flags of a cluster. */
#define DWC_WRAP 0x01
#define DWC_WHITESPACE 0x02
#define DWC_NEWLINE 0x04
#define DWC_SOFT_HYPHEN 0x08
#define DWC_TAB 0x10
#define DWC_OBJECT 0x20
#define DWC_NO_VISUAL 0x40

typedef struct DwCluster {
    UINT32 start;
    UINT32 length; /* code units. */
    UINT32 run;
    UINT32 glyph0, glyph1; /* the glyphs of the run this cluster is drawn with. */
    UINT8 flags;
    UINT8 level;
    FLOAT width;
    FLOAT extra;               /* added by justification. */
    FLOAT ink_left, ink_right; /* the ink against the cluster box, for overhangs. */
    FLOAT ink_top, ink_bottom;
} DwCluster;

typedef struct DwRun {
    UINT32 start, length;
    IDWriteFontFace *face; /* nullptr for an inline object. */
    FLOAT em;
    UINT8 level;
    BOOL sideways;
    UINT32 glyph_count;
    UINT16 *glyphs;
    FLOAT *advances;
    DWRITE_GLYPH_OFFSET *offsets;
    UINT16 *cluster_map;             /* a glyph index for each code unit of the run. */
    FLOAT ascent, descent, line_gap; /* scaled to the run's size. */
    FLOAT underline_offset, underline_thickness, strike_offset, strike_thickness, cap_height;
    IDWriteInlineObject *object;
    DWRITE_INLINE_OBJECT_METRICS object_metrics;
    const WCHAR *locale; /* points into a span or the format. */
    UINT32 span;         /* the span of the first code unit. */
} DwRun;

typedef struct DwItem {
    UINT32 cluster; /* DW_OPEN_END for the trimming sign. */
    FLOAT x, width;
} DwItem;

typedef struct DwLine {
    UINT32 first_cluster, cluster_count;
    UINT32 start, length, tws_length, newline_length;
    FLOAT width;     /* without the trailing whitespace. */
    FLOAT tws_width; /* the trailing whitespace. */
    FLOAT height, baseline;
    FLOAT leading_before, leading_after;
    FLOAT top, x; /* x is where the content starts. */
    BOOL trimmed;
    BOOL hidden;                   /* a line cut off below the layout's height. */
    UINT32 trim_first, trim_count; /* the clusters the sign stands for. */
    FLOAT sign_x, sign_width;
    IDWriteInlineObject *sign;
    DwItem *items;
    UINT32 item_count;
    UINT32 level_runs;
} DwLine;

typedef struct DwLayout {
    const IDWriteTextLayout4Vtbl *lpVtbl;
    LONG refs;
    DwFactory *factory;
    DwFmt fmt;
    WCHAR *text;
    UINT32 length;
    FLOAT max_width, max_height;
    BOOL vertical; /* the lines were built along the vertical axis, so the sizes and geometry are swapped. */
    DwSpan *spans;
    UINT32 span_count, span_capacity;
    /* gdi compatible layouts. */
    BOOL gdi;
    BOOL natural;
    FLOAT ppd;
    DWRITE_MATRIX transform;
    /* the layout, built on demand. */
    BOOL runs_valid, lines_valid;
    HRESULT build_status;
    DwRun *runs;
    UINT32 run_count;
    DwCluster *clusters;
    UINT32 cluster_count;
    DwLine *lines;
    UINT32 line_count;
    DWRITE_TEXT_METRICS1 metrics;
    FLOAT overhang[4];
    SRWLOCK lock;
} DwLayout;

HRESULT DwLayoutEnsure(DwLayout *layout);
void DwLayoutInvalidate(DwLayout *layout, BOOL runs);
void DwLayoutFreeBuilt(DwLayout *layout, BOOL runs);
HRESULT DwLayoutBuildRuns(DwLayout *layout);
HRESULT DwLayoutBuildLines(DwLayout *layout);
HRESULT DwLayoutDraw(DwLayout *layout, void *context, IDWriteTextRenderer *renderer, FLOAT x, FLOAT y);

/* spans. */
const DwSpan *DwSpanAt(const DwLayout *layout, UINT32 position);
BOOL DwSpanPropEqual(const DwSpan *a, const DwSpan *b, int prop);
/* the resolved face of a run's font, scaled metrics included, through the fallback. */
HRESULT DwResolveFace(DwLayout *layout, const DwSpan *span, UINT32 position, UINT32 length, UINT32 *mapped,
                      IDWriteFontFace **face, FLOAT *scale);

#endif
