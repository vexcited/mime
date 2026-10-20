/*
 * the text side of gdiplus: fonts and their families, string formats, and the layout the drawing and measuring of
 * text share. font.c has the families, collections and fonts, strfmt.c the string formats, text.c the layout and
 * the calls that draw and measure.
 */
#ifndef MIME_GDIPLUS_TEXT_PRIVATE_H
#define MIME_GDIPLUS_TEXT_PRIVATE_H

#include "gp_private.h"
#include <ttf/ttf.h>

/* ------------------------------------------------------------------ faces */

/* a font face read into memory: truetype outlines and tables, with the metrics gdi+ calls the cell of the font. */
typedef struct GFace {
    BYTE *data;
    SIZE_T size;
    BOOL owns_data;         /* the bytes belong to the face, else to the collection. */
    MimeTtfFont ttf;
    BOOL valid;             /* the bytes parse and have outlines. */
    BOOL bold, italic;      /* the style the face has itself. */
    UINT16 upem;            /* design units per em. */
    UINT16 ascent, descent, gap;
    INT underline_pos, underline_thickness;
    INT strike_pos, strike_thickness;
    BOOL symbol;
    UINT16 space_glyph;
    BYTE charset;
    /* outlines of the glyphs already drawn, in font units with y up. */
    MimeVecPath **outlines;
    UINT outline_count;
} GFace;

/* the glyph of a code point, 0 when the face has none. */
UINT16 gp_face_glyph(GFace *face, UINT32 cp);
/* the advance of a glyph in design units. */
INT gp_face_advance(GFace *face, UINT16 glyph);
/* the outline of a glyph as cubic figures in font units with y up, NULL for a glyph with none. */
const MimeVecPath *gp_face_outline(GFace *face, UINT16 glyph);

/* ------------------------------------------------------------------ families and collections */

/* one of the four styles of a family: regular, bold, italic and bold italic, found by their index. */
typedef struct GSlot {
    BOOL present;
    LOGFONTW lf;            /* how gdi finds the face, for the installed collection. */
    BYTE *blob;             /* the font file of a private collection, kept by the collection. */
    SIZE_T blob_size;
    UINT face_index;
    GFace *face;
    BOOL failed;
} GSlot;

typedef struct GCollection GCollection;

typedef struct GFamily {
    DWORD tag;
    WCHAR name[LF_FACESIZE];
    WCHAR upper[LF_FACESIZE];
    GCollection *collection;
    struct GFamily *next;   /* the collection's list, sorted by name. */
    GSlot slot[4];
    BOOL loaded;
    BOOL synthesized_regular;
} GFamily;

struct GCollection {
    DWORD tag;
    BOOL installed;
    BOOL loaded;
    GFamily *first;
    UINT family_count;
    /* the files a private collection holds, to know a file added twice. */
    struct GBlob *blobs;
};

/* the sentinels GetGenericFontFamily... hand out, which stand for the families found by name when used. */
#define GP_FAMILY_SANS_SERIF ((void *)(ULONG_PTR)0x11111111)
#define GP_FAMILY_SERIF ((void *)(ULONG_PTR)0x33333333)
#define GP_FAMILY_MONOSPACE ((void *)(ULONG_PTR)0x55555555)

/* a family, with the generic sentinels resolved. NULL for anything else that is no family. */
GFamily *gp_family_cast(const void *p);
/* the face a style asks for, with the fallbacks of gdi+ when the family lacks it. NULL when nothing loads. */
GFace *gp_family_face(GFamily *family, INT style);
/* a family of the installed collection by name, the way CreateFontFamilyFromName finds it. */
GFamily *gp_family_find(const WCHAR *name);
/* an installed family with a regular face that has the character, other than the one to avoid. */
GFamily *gp_family_fallback(UINT32 cp, const GFamily *avoid);

typedef struct GFont {
    DWORD tag;
    GFamily *family;
    float size;
    INT style;
    Unit unit;
} GFont;

GFont *gp_font_cast(const void *p);
/* the size of the font in world units of a graphics: the factor GetScaleForAlternatePageUnit has. */
float gp_font_world_size(const GFont *font, const GGraphics *g);

/* ------------------------------------------------------------------ string formats */

typedef struct GStrFmt {
    DWORD tag;
    BOOL generic;           /* the two shared formats cannot be deleted. */
    INT flags;
    LANGID language;
    StringAlignment align;
    StringAlignment line_align;
    StringDigitSubstitute digit_substitute;
    LANGID digit_language;
    float first_tab;
    float *tabs;
    INT tab_count;
    INT hotkey;
    float lead_margin, trail_margin;    /* in ems. */
    float tracking;
    StringTrimming trimming;
    CharacterRange *ranges;
    INT range_count;
} GStrFmt;

GStrFmt *gp_strfmt_cast(const void *p);
/* the format a call without one has: the generic default. */
const GStrFmt *gp_strfmt_default(void);

#endif
