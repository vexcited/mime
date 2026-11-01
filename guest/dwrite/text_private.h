/*
 * text_private.h: what the text layout shares between its parts: character data, the opentype shaper, the analyzer,
 * formats and layouts.
 */
#ifndef DW_TEXT_PRIVATE_H
#define DW_TEXT_PRIVATE_H

#include "dw_private.h"
#include "dwrite_textclass.h"

/* ------------------------------------------------------------------ unidata.c */

UINT32 DwScriptCount(void);
/* the script of a code point on its own: the script in the low byte, 0x100 for characters without a visual. */
UINT32 DwCharScript(UINT32 cp);
BOOL DwScriptProperties(UINT32 script, DWRITE_SCRIPT_PROPERTIES *properties);
UINT32 DwScriptTag(UINT32 script);
UINT8 DwLineBreakClass(UINT32 cp);
UINT8 DwBidiClass(UINT32 cp);
UINT8 DwGraphemeClass(UINT32 cp);
UINT8 DwCategory(UINT32 cp);
UINT32 DwMirror(UINT32 cp);
UINT32 DwCompose(UINT32 first, UINT32 second);
UINT32 DwCodePointAt(const WCHAR *text, UINT32 length, UINT32 pos, UINT32 *units);
BOOL DwIsWhitespace(UINT32 cp);
BOOL DwIsNewline(UINT32 cp);
UINT32 DwClusterLength(const WCHAR *text, UINT32 length, UINT32 pos);
/* 0 no break after the code unit, 1 a break is allowed, 2 a break is required. */
void DwLineBreaks(const WCHAR *text, UINT32 length, UINT8 *breaks);
void DwBidiLevels(const WCHAR *text, UINT32 length, UINT8 base, UINT8 *explicit_levels, UINT8 *resolved);

/* ------------------------------------------------------------------ shape.c */

typedef struct DwOtFont DwOtFont;

DwOtFont *DwOtOpen(IDWriteFontFace *face);
void DwOtClose(DwOtFont *font);
UINT16 DwOtUnitsPerEm(const DwOtFont *font);
INT32 DwOtAdvance(const DwOtFont *font, UINT16 glyph);

typedef struct DwFeatureRange {
    UINT32 tag; /* first letter in the low byte. */
    UINT32 value;
    UINT32 start, end; /* code units. */
} DwFeatureRange;

typedef struct DwShapeInput {
    const WCHAR *text;
    UINT32 length;
    DwOtFont *font;
    UINT32 script; /* directwrite script number. */
    BOOL no_visual;
    BOOL rtl;
    BOOL sideways;
    const WCHAR *locale;
    const DwFeatureRange *features;
    UINT32 feature_count;
    /* digits as this method turns them, nullptr leaves the text alone. */
    const UINT32 *digit_zero;
    /* only the listed features and the required ones apply, as when a typography is set. */
    BOOL exclusive;
} DwShapeInput;

typedef struct DwShaped {
    UINT32 glyph_count;
    UINT16 *glyphs;
    UINT16 *cluster_map; /* a glyph index for each code unit. */
    UINT8 *text_flags;   /* bit 0 shaped alone, bit 2 can break shaping after. */
    UINT16 *glyph_flags; /* DWRITE_SHAPING_GLYPH_PROPERTIES as bits. */
    INT32 *advances;     /* in font units. */
    INT32 *offset_x, *offset_y;
} DwShaped;

HRESULT DwShape(const DwShapeInput *in, DwShaped *out);
void DwShapedFree(DwShaped *shaped);

/* ------------------------------------------------------------------ analyzer.c */

void DwResolveScripts(const WCHAR *text, UINT32 length, UINT16 *script, UINT8 *shapes);
HRESULT DwAnalyzerCreate(DwFactory *factory, IDWriteTextAnalyzer **analyzer);
/* the digit zero of a number substitution for a locale, 0 for none. */
UINT32 DwNumberDigitZero(DWRITE_NUMBER_SUBSTITUTION_METHOD method, const WCHAR *locale, UINT32 context_script);
HRESULT DwNumberSubstitutionCreate(DWRITE_NUMBER_SUBSTITUTION_METHOD method, const WCHAR *locale, BOOL ignore_override,
                                   IDWriteNumberSubstitution **out);
BOOL DwNumberSubstitutionInfo(IDWriteNumberSubstitution *substitution, DWRITE_NUMBER_SUBSTITUTION_METHOD *method,
                              const WCHAR **locale);

/* ------------------------------------------------------------------ format.c */

/* what a text format holds, copied into each layout made from it. */
typedef struct DwFmt {
    WCHAR *family;
    IDWriteFontCollection *collection;
    DWRITE_FONT_WEIGHT weight;
    DWRITE_FONT_STYLE style;
    DWRITE_FONT_STRETCH stretch;
    FLOAT size;
    WCHAR *locale;
    DWRITE_TEXT_ALIGNMENT text_align;
    DWRITE_PARAGRAPH_ALIGNMENT para_align;
    DWRITE_WORD_WRAPPING wrap;
    DWRITE_READING_DIRECTION reading;
    DWRITE_FLOW_DIRECTION flow;
    FLOAT tab;
    DWRITE_TRIMMING trimming;
    IDWriteInlineObject *sign;
    DWRITE_LINE_SPACING spacing;
    DWRITE_VERTICAL_GLYPH_ORIENTATION vertical;
    BOOL last_line_wrap;
    DWRITE_OPTICAL_ALIGNMENT optical;
    IDWriteFontFallback *fallback;
    DWRITE_AUTOMATIC_FONT_AXES axes;
    DWRITE_FONT_AXIS_VALUE *axis_values;
    UINT32 axis_count;
} DwFmt;

HRESULT DwFmtInit(DwFmt *f, const WCHAR *family, IDWriteFontCollection *collection, DWRITE_FONT_WEIGHT weight,
                  DWRITE_FONT_STYLE style, DWRITE_FONT_STRETCH stretch, FLOAT size, const WCHAR *locale);
HRESULT DwFmtCopy(DwFmt *dst, const DwFmt *src);
void DwFmtFree(DwFmt *f);
/* the format behind a format object of this implementation, or nullptr. */
const DwFmt *DwFmtOf(IDWriteTextFormat *format);

HRESULT DwFmtSetTextAlignment(DwFmt *f, DWRITE_TEXT_ALIGNMENT v);
HRESULT DwFmtSetParagraphAlignment(DwFmt *f, DWRITE_PARAGRAPH_ALIGNMENT v);
HRESULT DwFmtSetWordWrapping(DwFmt *f, DWRITE_WORD_WRAPPING v);
HRESULT DwFmtSetReadingDirection(DwFmt *f, DWRITE_READING_DIRECTION v);
HRESULT DwFmtSetFlowDirection(DwFmt *f, DWRITE_FLOW_DIRECTION v);
HRESULT DwFmtSetTabStop(DwFmt *f, FLOAT v);
HRESULT DwFmtSetTrimming(DwFmt *f, const DWRITE_TRIMMING *trimming, IDWriteInlineObject *sign);
HRESULT DwFmtSetLineSpacing(DwFmt *f, const DWRITE_LINE_SPACING *spacing);
HRESULT DwFmtSetFontFallback(DwFmt *f, IDWriteFontFallback *fallback);
HRESULT DwFmtSetAxisValues(DwFmt *f, const DWRITE_FONT_AXIS_VALUE *values, UINT32 count);
HRESULT DwFmtGetString(const WCHAR *s, WCHAR *out, UINT32 size);
BOOL DwValidWeight(UINT32 w);
BOOL DwValidStyle(UINT32 s);
BOOL DwValidStretch(UINT32 s);

/* ------------------------------------------------------------------ layout.c */

/* a face for the first characters of text in the format's font, falling back for characters it lacks. */
HRESULT DwFontForText(DwFactory *factory, const DwFmt *fmt, const WCHAR *text, UINT32 length, IDWriteFontFace **face,
                      FLOAT *scale);

HRESULT DwTextFormatCreate(DwFactory *factory, const WCHAR *family, IDWriteFontCollection *collection,
                           DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style, DWRITE_FONT_STRETCH stretch, FLOAT size,
                           const WCHAR *locale, IDWriteTextFormat **format);
HRESULT DwTextLayoutCreate(DwFactory *factory, const WCHAR *text, UINT32 length, IDWriteTextFormat *format,
                           FLOAT max_width, FLOAT max_height, FLOAT pixels_per_dip, const DWRITE_MATRIX *transform,
                           BOOL natural, BOOL gdi, IDWriteTextLayout **layout);
HRESULT DwTypographyCreate(IDWriteTypography **typography);
HRESULT DwEllipsisSignCreate(DwFactory *factory, IDWriteTextFormat *format, IDWriteInlineObject **sign);

/* the tags of the wire format of the font features: a tag of the interface has its first letter in the low byte. */
static inline UINT32 DwTagSwap(UINT32 t) { return (t << 24) | ((t & 0xff00) << 8) | ((t >> 8) & 0xff00) | (t >> 24); }

#endif
