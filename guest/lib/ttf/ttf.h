/*
 * truetype font parsing and anti-aliased glyph rasterization.
 *
 * the font data is never copied: it must stay valid and unchanged for as long
 * as the MimeTtfFont referencing it is used. every read is bounds checked and
 * malformed fonts make the functions fail instead of crashing.
 *
 * font space is y up, measured in font units. bitmap space is y down,
 * measured in pixels, with the glyph origin on the baseline at 0, 0.
 */
#ifndef MIME_LIB_TTF_H
#define MIME_LIB_TTF_H

#include <stddef.h>
#include <stdint.h>

#include "../alloc.h"

typedef struct MimeTtfTable {
    const uint8_t *data;
    uint32_t size;
} MimeTtfTable;

/* parsed face. treat the fields as private. */
typedef struct MimeTtfFont {
    const uint8_t *data;
    size_t size;
    MimeTtfTable head, hhea, hmtx, maxp, loca, glyf, cmap, kern, os2, name, post;
    MimeTtfTable fpgm, prep, cvt, hdmx, ltsh, vdmx, gasp, vhea, vmtx;
    MimeTtfTable cmap_sub;
    MimeTtfTable kern_pairs;
    uint32_t kern_count;
    uint16_t cmap_format;
    bool cmap_symbol;
    bool has_outlines;
    uint16_t num_glyphs;
    uint16_t num_hmetrics;
    uint16_t units_per_em;
    int16_t loca_format;
} MimeTtfFont;

struct MimeTtfHinter;

/* every field is in font units unless noted. */
typedef struct MimeTtfMetrics {
    int units_per_em;
    int ascender, descender, line_gap; /* hhea, descender is negative. */
    int typo_ascender, typo_descender, typo_line_gap; /* os/2, or hhea when absent. */
    int win_ascent, win_descent; /* os/2, both positive. */
    int avg_char_width;
    int max_advance;
    int x_height, cap_height; /* os/2 v2, else measured from x and H. */
    int x_min, y_min, x_max, y_max; /* head bounding box. */
    int weight; /* 100 to 900, 400 is regular. */
    int underline_position, underline_thickness;
    int strikeout_position, strikeout_size;
    int32_t italic_angle; /* 16.16 degrees, negative leans right. */
    bool italic, bold, fixed_pitch;
    bool use_typo_metrics; /* fsSelection bit 7. */
} MimeTtfMetrics;

/* placement and synthetic styling for the rasterizer. */
typedef struct MimeTtfRenderParams {
    float scale_x, scale_y; /* pixels per font unit. */
    float shift_x, shift_y; /* sub-pixel shift in bitmap space, y down. */
    float oblique; /* horizontal shear per unit of height, 0.2 is a typical synthetic italic. */
    float embolden; /* extra horizontal stroke width in pixels, 0 for none. */
    /*
     * with use_matrix the font units map to bitmap space by a general matrix, y down:
     * x = xx * fx + xy * fy + shift_x, y = yx * fx + yy * fy + shift_y, and the scale and
     * oblique fields are not used.
     */
    bool use_matrix;
    float xx, xy, yx, yy;
    /*
     * with a hinter the glyph is drawn from its hinted outline in pixels, scale_x, scale_y and the matrix are not
     * used and oblique still shears. the hinter must have been made for the size drawn.
     */
    const struct MimeTtfHinter *hinter;
    /* samples per pixel across and down for the scan converter, below 2 means one. shifts are in samples. */
    int over_x, over_y;
} MimeTtfRenderParams;

/* number of faces in a font file, 1 for a plain ttf, 0 if unrecognized. */
unsigned mime_ttf_face_count(const void *data, size_t size);

/* parses face face_index of a ttf, otf or ttc file. */
bool mime_ttf_open(MimeTtfFont *font, const void *data, size_t size, unsigned face_index);

uint16_t mime_ttf_glyph_count(const MimeTtfFont *font);

/* true when the face has glyf outlines the rasterizer can draw. */
bool mime_ttf_has_outlines(const MimeTtfFont *font);

/* maps a unicode code point to a glyph, 0 is the missing glyph. */
uint16_t mime_ttf_glyph_index(const MimeTtfFont *font, uint32_t codepoint);

void mime_ttf_glyph_hmetrics(const MimeTtfFont *font, uint16_t glyph, int *advance, int *left_bearing);

/* the device advance of a glyph from the hdmx table at this ppem, false when the table has no such size. */
bool mime_ttf_hdmx_advance(const MimeTtfFont *font, int ppem, uint16_t glyph, int *advance);

/* the ppem a glyph's advance is linear from, from the LTSH table, 0 when there is none. */
unsigned mime_ttf_ltsh_threshold(const MimeTtfFont *font, uint16_t glyph);

/* VDMX ascent and descent extremes in pixels for an aspect ratio and ppem, false when the table has none. */
bool mime_ttf_vdmx(const MimeTtfFont *font, int x_ratio, int y_ratio, int ppem, int *y_max, int *y_min);

/* gasp behavior flags for a ppem, 0xffff when the table is absent. */
unsigned mime_ttf_gasp(const MimeTtfFont *font, int ppem);

/* kern table adjustment to add to the advance of left, in font units. */
int mime_ttf_kerning(const MimeTtfFont *font, uint16_t left, uint16_t right);

void mime_ttf_metrics(const MimeTtfFont *font, MimeTtfMetrics *metrics);

/*
 * copies name record name_id, for example 1 family, 2 subfamily, 4 full name,
 * as utf-16 code units in host order. *length receives the full length in
 * code units even when capacity is smaller, and the copy is nul terminated
 * when there is room. returns false when the record is absent.
 */
bool mime_ttf_name(const MimeTtfFont *font, unsigned name_id, uint16_t *buffer, size_t capacity, size_t *length);

/*
 * true for the regular faces of a few fixed pitch families Windows draws a sample wider in ClearType: Courier New, the
 * Miriam and Rod faces and Simplified Arabic Fixed. the bold ones are left alone.
 */
bool mime_ttf_thin(const MimeTtfFont *font);

/*
 * pixel box of the rendered glyph relative to the baseline origin, y down.
 * the bitmap passed to the render functions is x1 - x0 by y1 - y0 pixels.
 * an empty glyph succeeds with an empty box.
 */
bool mime_ttf_glyph_bitmap_box(const MimeTtfFont *font, uint16_t glyph, float scale_x, float scale_y,
                               float shift_x, float shift_y, int *x0, int *y0, int *x1, int *y1);

/*
 * renders 8 bit coverage, 0 to 255, top-down with the given stride. the
 * top-left pixel corresponds to x0, y0 of the box. every pixel of the
 * width by height area is overwritten, coverage outside it is dropped.
 */
bool mime_ttf_render_glyph(const MimeTtfFont *font, const MimeAllocator *alloc, uint16_t glyph, float scale_x,
                           float scale_y, float shift_x, float shift_y, uint8_t *out, int width, int height,
                           int stride);

/* same as above with synthetic oblique and emboldening. */
bool mime_ttf_glyph_box_ex(const MimeTtfFont *font, uint16_t glyph, const MimeTtfRenderParams *params, int *x0,
                           int *y0, int *x1, int *y1);
bool mime_ttf_render_glyph_ex(const MimeTtfFont *font, const MimeAllocator *alloc, uint16_t glyph,
                              const MimeTtfRenderParams *params, uint8_t *out, int width, int height, int stride);

/* the horizontal extent of all the points of the outline, the ones off the curve too, in the units of the parameters. */
bool mime_ttf_glyph_control_extent(const MimeTtfFont *font, uint16_t glyph, const MimeTtfRenderParams *params,
                                   float *x0, float *x1);

/* the scan control the programs left, SCANCTRL and SCANTYPE as the instructions set them. */
typedef struct MimeTtfScan {
    uint32_t control, type;
    int ppem;
    bool rotated, stretched;
} MimeTtfScan;

/*
 * bilevel scan conversion: a sample is on when its centre is inside the outline, then the dropout rules of the scan
 * control turn on samples for features thinner than a sample. the box comes from mime_ttf_glyph_box_ex and the
 * output holds 1 for on samples, 0 for off ones. over_x and over_y of the parameters oversample the outline.
 */
bool mime_ttf_scan_glyph_ex(const MimeTtfFont *font, const MimeAllocator *alloc, uint16_t glyph,
                            const MimeTtfRenderParams *params, const MimeTtfScan *scan, uint8_t *out, int width,
                            int height, int stride);

/* a bitmap with its box in samples or pixels relative to the glyph origin, y down. free it with mime_ttf_bitmap_free. */
typedef struct MimeTtfBitmap {
    int x0, y0, x1, y1;
    int stride;
    uint8_t *data;
} MimeTtfBitmap;

void mime_ttf_bitmap_free(const MimeAllocator *alloc, MimeTtfBitmap *bitmap);

/* the scan converted samples, 1 for on, in the tight box of the samples that are on. an empty glyph has an empty box. */
bool mime_ttf_scan_bitmap(const MimeTtfFont *font, const MimeAllocator *alloc, uint16_t glyph,
                          const MimeTtfRenderParams *params, const MimeTtfScan *scan, MimeTtfBitmap *out);

/*
 * ClearType glyphs: the outline is scanned six samples across a pixel, and one or five down, then each of the three
 * stripes of a pixel is the count of the six samples around its centre, from 0 to 6, with the rows of five blended
 * 4, 9, 10, 9, 4 over 36. shifts of the parameters are in pixels. the box is in whole pixels and the data holds three
 * levels per pixel, left to right.
 */
bool mime_ttf_cleartype_bitmap(const MimeTtfFont *font, const MimeAllocator *alloc, uint16_t glyph,
                               const MimeTtfRenderParams *params, int over_y, const MimeTtfScan *scan,
                               MimeTtfBitmap *out);

/*
 * the tables GDI blends ClearType text with, for a font smoothing gamma such as the 1200 of the contrast setting. linear
 * maps an encoded channel to the space the coverage is blended in, encoded maps back.
 */
void mime_ttf_gamma_tables(unsigned gamma, uint8_t *linear, uint8_t *encoded);

/* flags of a hinter, what the GETINFO instruction reports about the rendering. */
enum {
    MIME_TTF_HINT_CLEARTYPE = 1,
    MIME_TTF_HINT_GRAY = 2,
    MIME_TTF_HINT_COMPAT_WIDTHS = 4,
    MIME_TTF_HINT_SUBPIXEL = 8,
    MIME_TTF_HINT_SYMMETRIC = 16,
    MIME_TTF_HINT_GRAY_CLEARTYPE = 32,
};

/* GETINFO version override in the upper bits of the flags, 0 for the default. */
#define MIME_TTF_HINT_VERSION(v) ((uint32_t)(v) << 8)
#define MIME_TTF_HINT_VERSION_OF(flags) (((flags) >> 8) & 0xff)

typedef struct MimeTtfHinter MimeTtfHinter;

/*
 * runs the font program and the control value program for one pixel size. null when the face has no outlines or
 * memory is short. the font must outlive the hinter.
 */
MimeTtfHinter *mime_ttf_hinter_new(const MimeTtfFont *font, const MimeAllocator *alloc, int ppem_x, int ppem_y,
                                   uint32_t flags);
void mime_ttf_hinter_free(MimeTtfHinter *hinter);

/* true when the glyph programs of this face run at this size. */
bool mime_ttf_hinter_active(const MimeTtfHinter *hinter);

/* the SCANCTRL and SCANTYPE values the prep left. */
void mime_ttf_hinter_scan(const MimeTtfHinter *hinter, uint32_t *control, uint32_t *type);

/* the hinted advance width in 26.6 pixels. */
bool mime_ttf_hint_advance(const MimeTtfHinter *hinter, uint16_t glyph, int32_t *advance);

#endif
