/*
 * the text layer of gdiplus: font files and families, string formats, the layout of a string into lines of
 * positioned glyphs, and the drawing of those glyphs. font.c, strfmt.c, textlay.c, textdraw.c and text.c share it.
 *
 * lengths along a line are called u and across lines v. a horizontal layout has u along x and v along y, a
 * vertical one has u along y and v along x, so the layout itself never has to know about the turn.
 */
#ifndef MIME_GDIPLUS_TEXT_PRIVATE_H
#define MIME_GDIPLUS_TEXT_PRIVATE_H

#include "gp_private.h"
#include <ttf/ttf.h>
#include <ttf/ttf_internal.h>

/* ------------------------------------------------------------------ font files */

/* the bytes of a font file, shared by the faces of a collection file. */
typedef struct GFontBlob {
    LONG refs;
    BYTE *bytes;
    SIZE_T size;
} GFontBlob;

/* one parsed face, with the numbers every line of text needs. Design units are those of the em square. */
typedef struct GFace {
    GFontBlob *blob;
    MimeTtfFont ttf;
    MimeTtfMetrics metrics;
    INT em;
    INT ascent, descent, gap;
    INT underline_pos, underline_size;
    INT strike_pos, strike_size;
} GFace;

/* style slots of a family: regular, bold, italic and bold italic. */
enum { GP_SLOT_COUNT = 4 };

typedef struct GFaceSlot {
    BOOL present;
    BOOL tried;     /* the face was asked for, a failure is not asked again. */
    LOGFONTW lf;    /* what the installed font is found by. */
    GFace *face;
} GFaceSlot;

typedef struct GFamily {
    DWORD tag;
    LONG refs;
    BOOL installed;
    BOOL slots_loaded;
    BYTE charset;
    WCHAR name[LF_FACESIZE];
    GFaceSlot slot[GP_SLOT_COUNT];
} GFamily;

typedef struct GCollection {
    DWORD tag;
    BOOL installed;
    BOOL ready;
    GFamily **families;   /* sorted by their raised names. */
    INT count, capacity;
    WCHAR **files;        /* raised paths of the files added to a private collection. */
    INT file_count;
} GCollection;

typedef struct GFont {
    DWORD tag;
    const void *family;   /* a GFamily or one of the generic sentinels. */
    float size;
    INT style;
    GpUnit unit;
} GFont;

/* the three generic families are these values in place of a pointer. */
#define GP_FAMILY_SANS_SERIF ((const void *)(ULONG_PTR)0x11111111)
#define GP_FAMILY_SERIF ((const void *)(ULONG_PTR)0x33333333)
#define GP_FAMILY_MONOSPACE ((const void *)(ULONG_PTR)0x55555555)

GFont *gp_font_cast(const void *p);
/* the family a pointer stands for, generic ones resolved, NULL when it is none or no family can be found. */
GFamily *gp_family_resolve(const void *family);
/* a family of the installed collection by name, substitutions included. */
GFamily *gp_family_by_name(const WCHAR *name);
/* the face a style is drawn from, and which slot it is. NULL when the family has no face at all. */
const GFace *gp_family_face(GFamily *family, INT style, INT *slot);
/* the face that has a character, from the families Windows falls back to. NULL when none has it. */
const GFace *gp_fallback_face(UINT32 cp, INT style, UINT16 *glyph);

UINT16 gp_face_glyph(const GFace *face, UINT32 cp);
/* the advance of a glyph in design units. */
INT gp_face_advance(const GFace *face, UINT16 glyph);
/*
 * appends the outline of a glyph to a path as closed figures of lines and cubic curves. a point of the glyph in font
 * units, x along the line and y up, goes to the origin plus x times the vector u plus y times the vector v.
 */
GpStatus gp_face_outline(const GFace *face, UINT16 glyph, MimeVecPath *path, float ox, float oy, float ux, float uy,
                         float vx, float vy);

/* the em size in world units of a font of the given size and unit on a graphics, NULL graphics means pixels. */
float gp_font_world_size(float size, GpUnit unit, const GGraphics *g);

/* ------------------------------------------------------------------ string formats */

enum { GP_MAX_RANGES = 32 };

typedef struct GStrFmt {
    DWORD tag;
    BOOL shared;
    INT flags;
    StringAlignment align, line_align;
    StringTrimming trimming;
    INT hotkey;
    LANGID digit_language;
    StringDigitSubstitute digit_substitute;
    float first_tab;
    INT tab_count;
    float *tabs;
    INT range_count;
    CharacterRange ranges[GP_MAX_RANGES];
    float lead_margin, trail_margin;
    float tracking;
} GStrFmt;

/* a flag bit no sdk header names, which also keeps a string off the quick path. */
#define GP_FMT_UNDOCUMENTED_40000000 0x40000000

GStrFmt *gp_strfmt_cast(const void *p);
/* the shared default format. A call without a format behaves like it, but for the tab stops. */
const GStrFmt *gp_strfmt_default(void);

/* ------------------------------------------------------------------ layout */

typedef struct GTextInput {
    const WCHAR *text;
    INT length;                 /* at least 1. */
    GFamily *family;
    INT style;
    float size;                 /* the em size in world units. */
    const GStrFmt *format;      /* NULL when the call had none. */
    float x, y, w, h;           /* the layout rectangle. */
    BOOL has_matrix;            /* world to device is known, the quick path needs to see it. */
    MimeVecMatrix w2d;
    BOOL force_general;
} GTextInput;

typedef struct GTextGlyph {
    UINT16 glyph;
    BOOL hotkey;                /* underlined as a hotkey. */
    const GFace *face;
    INT src_begin, src_end;     /* the characters of the string this glyph shows. */
    float x;                    /* along the line from its start, after the margin. */
    float advance;
} GTextGlyph;

typedef struct GTextLine {
    INT first, count;           /* its glyphs in display order. */
    INT src_begin, src_end;     /* the characters it holds, line break included. */
    float shift;                /* where its box starts along the line, from the edge of the layout rectangle. */
    float width;                /* its glyphs, without margins and without the blanks that hang over its end. */
    float top;                  /* across the lines, from the top of the block. */
} GTextLine;

typedef struct GTextLayout {
    GTextGlyph *glyphs;
    INT glyph_count, glyph_capacity;
    GTextLine *lines;
    INT line_count, line_capacity;
    BOOL quick;
    BOOL vertical;
    BOOL right_to_left;
    BOOL no_clip;
    float lead, trail;          /* the margins. */
    float ascent, descent, pitch;
    float text_height;          /* the block across the lines, with the eighth of an em. */
    float block_shift;          /* the line alignment. */
    INT fitted;                 /* characters of the lines kept. */
    float x, y, w, h;           /* the layout rectangle, as it was given. */
    float size;
    const GFace *face;          /* the face of the string. */
    INT slot;
} GTextLayout;

GpStatus gp_text_layout(const GTextInput *in, GTextLayout *out);
void gp_text_layout_free(GTextLayout *layout);
/* the box the layout takes, in the rectangle's units. */
void gp_text_layout_box(const GTextLayout *layout, RectF *box);

/* the cell of a stretch of a line, from u0 to u1 along it from the line's pen start, ascent to descent across. */
void gp_text_span_rect(const GTextLayout *layout, const GTextLine *line, float u0, float u1, RectF *out);

/* ------------------------------------------------------------------ drawing */

/* the contrast row of the alpha map: coverage 0 to 255 to alpha, NULL for contrast 0. */
const UINT8 *gp_text_contrast_map(UINT contrast);
/* the hint a graphics draws text with, the system default resolved. */
TextRenderingHint gp_text_resolved_hint(const GGraphics *g);

typedef struct GTextStyle {
    BOOL simulate_bold, simulate_italic;
    BOOL underline, strikeout;
} GTextStyle;

/*
 * puts the outlines of a layout into a path: the glyphs, the decorations and the hotkey underlines. to_target takes
 * the layout's rectangle units to the units of the path. With snap the origin of every glyph and the edges of the
 * decorations are placed on whole units of the target, which suits a device with no turn.
 */
GpStatus gp_text_emit(const GTextLayout *layout, const GTextStyle *style, const MimeVecMatrix *to_target, BOOL snap,
                      MimeVecPath *out);

#endif
