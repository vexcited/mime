/*
 * gdiplus text: font files and families, string formats, laying a string out into lines of positioned glyphs, and
 * drawing them. shared by font.c, strfmt.c, textlay.c, textdraw.c and text.c.
 *
 * lengths along a line are u, across lines v. horizontal layouts have u along x, vertical ones u along y, so the
 * layout never deals with the rotation.
 */
#ifndef MIME_GDIPLUS_TEXT_PRIVATE_H
#define MIME_GDIPLUS_TEXT_PRIVATE_H

#include "gp_private.h"
#include <ttf/ttf.h>
#include <ttf/ttf_internal.h>

/* font file bytes, shared by the faces of a collection file. */
typedef struct GFontBlob {
    LONG refs;
    BYTE *bytes;
    SIZE_T size;
} GFontBlob;

/* one parsed face, metrics in design units of the em square. */
typedef struct GFace {
    GFontBlob *blob;
    MimeTtfFont ttf;
    MimeTtfMetrics metrics;
    INT em;
    INT ascent, descent, gap;
    INT underline_pos, underline_size;
    INT strike_pos, strike_size;
} GFace;

/* style slots: regular, bold, italic and bold italic. */
enum { GP_SLOT_COUNT = 4 };

typedef struct GFaceSlot {
    BOOL present;
    BOOL tried;     /* loading was attempted, a failure is not retried. */
    LOGFONTW lf;    /* how the installed font is looked up. */
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
    GFamily **families;   /* sorted by uppercased name. */
    INT count, capacity;
    WCHAR **files;        /* uppercased paths of files added to a private collection. */
    INT file_count;
} GCollection;

typedef struct GFont {
    DWORD tag;
    const void *family;   /* a GFamily or one of the generic sentinels. */
    float size;
    INT style;
    GpUnit unit;
} GFont;

/* the generic families are these sentinels instead of pointers. */
#define GP_FAMILY_SANS_SERIF ((const void *)(ULONG_PTR)0x11111111)
#define GP_FAMILY_SERIF ((const void *)(ULONG_PTR)0x33333333)
#define GP_FAMILY_MONOSPACE ((const void *)(ULONG_PTR)0x55555555)

GFont *gp_font_cast(const void *p);
/* resolves generic sentinels, NULL when no family is found. */
GFamily *gp_family_resolve(const void *family);
/* installed family by name, substitutions included. */
GFamily *gp_family_by_name(const WCHAR *name);
/* the face a style is drawn from and its slot, NULL when the family has no face at all. */
const GFace *gp_family_face(GFamily *family, INT style, INT *slot);
/* the face of a Windows fallback family that has the character, or NULL. */
const GFace *gp_fallback_face(UINT32 cp, INT style, UINT16 *glyph);

UINT16 gp_face_glyph(const GFace *face, UINT32 cp);
/* in design units. */
INT gp_face_advance(const GFace *face, UINT16 glyph);
/*
 * appends a glyph outline as closed figures of lines and cubics. a font unit point x, y with y up maps to
 * o + x * u + y * v.
 */
GpStatus gp_face_outline(const GFace *face, UINT16 glyph, MimeVecPath *path, float ox, float oy, float ux, float uy,
                         float vx, float vy);

/* the em size in world units, a NULL graphics means pixels. */
float gp_font_world_size(float size, GpUnit unit, const GGraphics *g);

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

/* a flag bit no sdk header names, it also keeps a string off the quick path. */
#define GP_FMT_UNDOCUMENTED_40000000 0x40000000

GStrFmt *gp_strfmt_cast(const void *p);
/* a call without a format behaves like this one, except for tab stops. */
const GStrFmt *gp_strfmt_default(void);

typedef struct GTextInput {
    const WCHAR *text;
    INT length;                 /* at least 1. */
    GFamily *family;
    INT style;
    float size;                 /* em size in world units. */
    const GStrFmt *format;      /* NULL when the call had none. */
    float x, y, w, h;           /* the layout rectangle. */
    BOOL has_matrix;            /* world to device is known, the quick path needs it. */
    MimeVecMatrix w2d;
    BOOL force_general;
} GTextInput;

typedef struct GTextGlyph {
    UINT16 glyph;
    BOOL hotkey;                /* underlined as a hotkey. */
    const GFace *face;
    INT src_begin, src_end;     /* the source characters this glyph shows. */
    float x;                    /* from the line start, after the margin. */
    float advance;
} GTextGlyph;

typedef struct GTextLine {
    INT first, count;           /* its glyphs in display order. */
    INT src_begin, src_end;     /* the characters it holds, line break included. */
    float shift;                /* start of its box along the line, from the layout rectangle's edge. */
    float width;                /* glyph extent, without margins or trailing blanks. */
    float top;                  /* from the top of the block. */
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
    float text_height;          /* block extent across the lines, including an eighth of an em. */
    float block_shift;          /* from the line alignment. */
    INT fitted;                 /* characters in the kept lines. */
    float x, y, w, h;           /* the layout rectangle, as given. */
    float size;
    const GFace *face;
    INT slot;
} GTextLayout;

GpStatus gp_text_layout(const GTextInput *in, GTextLayout *out);
void gp_text_layout_free(GTextLayout *layout);
/* the layout's bounding box, in the rectangle's units. */
void gp_text_layout_box(const GTextLayout *layout, RectF *box);

/* the cell of a line stretch from u0 to u1 past the line's pen start, ascent to descent across. */
void gp_text_span_rect(const GTextLayout *layout, const GTextLine *line, float u0, float u1, RectF *out);

/* maps coverage 0..255 to alpha, NULL for contrast 0. */
const UINT8 *gp_text_contrast_map(UINT contrast);
/* the text hint, with the system default resolved. */
TextRenderingHint gp_text_resolved_hint(const GGraphics *g);

typedef struct GTextStyle {
    BOOL simulate_bold, simulate_italic;
    BOOL underline, strikeout;
} GTextStyle;

/*
 * puts the glyph outlines, decorations and hotkey underlines of a layout into a path. to_target maps layout units
 * to path units. snap puts glyph origins and decoration edges on whole target units, which suits unrotated devices.
 */
GpStatus gp_text_emit(const GTextLayout *layout, const GTextStyle *style, const MimeVecMatrix *to_target, BOOL snap,
                      MimeVecPath *out);

#endif
