/*
 * directwrite: fonts, metrics and glyphs, text formats and layouts, and the text analyzer, in windows' vtable
 * order.
 */
#ifndef MIME_WIN32_DWRITE_H
#define MIME_WIN32_DWRITE_H

#include <mime/win32/objbase.h>
#include <mime/win32/d2dbasetypes.h>
#include <mime/win32/wingdi.h>

#ifdef MIME_BUILD_DWRITE
#define DWRITEAPI DECLSPEC_EXPORT
#else
#define DWRITEAPI DECLSPEC_IMPORT
#endif

/* ------------------------------------------------------------------ errors */

#define DWRITE_E_FILEFORMAT ((HRESULT)0x88985000L)
#define DWRITE_E_UNEXPECTED ((HRESULT)0x88985001L)
#define DWRITE_E_NOFONT ((HRESULT)0x88985002L)
#define DWRITE_E_FILENOTFOUND ((HRESULT)0x88985003L)
#define DWRITE_E_FILEACCESS ((HRESULT)0x88985004L)
#define DWRITE_E_FONTCOLLECTIONOBSOLETE ((HRESULT)0x88985005L)
#define DWRITE_E_ALREADYREGISTERED ((HRESULT)0x88985006L)
#define DWRITE_E_CACHEFORMAT ((HRESULT)0x88985007L)
#define DWRITE_E_CACHEVERSION ((HRESULT)0x88985008L)
#define DWRITE_E_UNSUPPORTEDOPERATION ((HRESULT)0x88985009L)
#define DWRITE_E_TEXTRENDERERINCOMPATIBLE ((HRESULT)0x8898500AL)
#define DWRITE_E_FLOWDIRECTIONCONFLICTS ((HRESULT)0x8898500BL)
#define DWRITE_E_NOCOLOR ((HRESULT)0x8898500CL)

/* ------------------------------------------------------------------ enumerations */

typedef enum DWRITE_FACTORY_TYPE {
    DWRITE_FACTORY_TYPE_SHARED = 0,
    DWRITE_FACTORY_TYPE_ISOLATED = 1
} DWRITE_FACTORY_TYPE;

typedef enum DWRITE_FONT_FILE_TYPE {
    DWRITE_FONT_FILE_TYPE_UNKNOWN = 0,
    DWRITE_FONT_FILE_TYPE_CFF = 1,
    DWRITE_FONT_FILE_TYPE_TRUETYPE = 2,
    DWRITE_FONT_FILE_TYPE_OPENTYPE_COLLECTION = 3,
    DWRITE_FONT_FILE_TYPE_TRUETYPE_COLLECTION = 3,
    DWRITE_FONT_FILE_TYPE_TYPE1_PFM = 4,
    DWRITE_FONT_FILE_TYPE_TYPE1_PFB = 5,
    DWRITE_FONT_FILE_TYPE_VECTOR = 6,
    DWRITE_FONT_FILE_TYPE_BITMAP = 7
} DWRITE_FONT_FILE_TYPE;

typedef enum DWRITE_FONT_FACE_TYPE {
    DWRITE_FONT_FACE_TYPE_CFF = 0,
    DWRITE_FONT_FACE_TYPE_TRUETYPE = 1,
    DWRITE_FONT_FACE_TYPE_OPENTYPE_COLLECTION = 2,
    DWRITE_FONT_FACE_TYPE_TRUETYPE_COLLECTION = 2,
    DWRITE_FONT_FACE_TYPE_TYPE1 = 3,
    DWRITE_FONT_FACE_TYPE_VECTOR = 4,
    DWRITE_FONT_FACE_TYPE_BITMAP = 5,
    DWRITE_FONT_FACE_TYPE_UNKNOWN = 6,
    DWRITE_FONT_FACE_TYPE_RAW_CFF = 7
} DWRITE_FONT_FACE_TYPE;

typedef enum DWRITE_FONT_SIMULATIONS {
    DWRITE_FONT_SIMULATIONS_NONE = 0,
    DWRITE_FONT_SIMULATIONS_BOLD = 1,
    DWRITE_FONT_SIMULATIONS_OBLIQUE = 2
} DWRITE_FONT_SIMULATIONS;

typedef enum DWRITE_FONT_WEIGHT {
    DWRITE_FONT_WEIGHT_THIN = 100,
    DWRITE_FONT_WEIGHT_EXTRA_LIGHT = 200,
    DWRITE_FONT_WEIGHT_ULTRA_LIGHT = 200,
    DWRITE_FONT_WEIGHT_LIGHT = 300,
    DWRITE_FONT_WEIGHT_SEMI_LIGHT = 350,
    DWRITE_FONT_WEIGHT_NORMAL = 400,
    DWRITE_FONT_WEIGHT_REGULAR = 400,
    DWRITE_FONT_WEIGHT_MEDIUM = 500,
    DWRITE_FONT_WEIGHT_DEMI_BOLD = 600,
    DWRITE_FONT_WEIGHT_SEMI_BOLD = 600,
    DWRITE_FONT_WEIGHT_BOLD = 700,
    DWRITE_FONT_WEIGHT_EXTRA_BOLD = 800,
    DWRITE_FONT_WEIGHT_ULTRA_BOLD = 800,
    DWRITE_FONT_WEIGHT_BLACK = 900,
    DWRITE_FONT_WEIGHT_HEAVY = 900,
    DWRITE_FONT_WEIGHT_EXTRA_BLACK = 950,
    DWRITE_FONT_WEIGHT_ULTRA_BLACK = 950
} DWRITE_FONT_WEIGHT;

typedef enum DWRITE_FONT_STRETCH {
    DWRITE_FONT_STRETCH_UNDEFINED = 0,
    DWRITE_FONT_STRETCH_ULTRA_CONDENSED = 1,
    DWRITE_FONT_STRETCH_EXTRA_CONDENSED = 2,
    DWRITE_FONT_STRETCH_CONDENSED = 3,
    DWRITE_FONT_STRETCH_SEMI_CONDENSED = 4,
    DWRITE_FONT_STRETCH_NORMAL = 5,
    DWRITE_FONT_STRETCH_MEDIUM = 5,
    DWRITE_FONT_STRETCH_SEMI_EXPANDED = 6,
    DWRITE_FONT_STRETCH_EXPANDED = 7,
    DWRITE_FONT_STRETCH_EXTRA_EXPANDED = 8,
    DWRITE_FONT_STRETCH_ULTRA_EXPANDED = 9
} DWRITE_FONT_STRETCH;

typedef enum DWRITE_FONT_STYLE {
    DWRITE_FONT_STYLE_NORMAL = 0,
    DWRITE_FONT_STYLE_OBLIQUE = 1,
    DWRITE_FONT_STYLE_ITALIC = 2
} DWRITE_FONT_STYLE;

typedef enum DWRITE_INFORMATIONAL_STRING_ID {
    DWRITE_INFORMATIONAL_STRING_NONE = 0,
    DWRITE_INFORMATIONAL_STRING_COPYRIGHT_NOTICE = 1,
    DWRITE_INFORMATIONAL_STRING_VERSION_STRINGS = 2,
    DWRITE_INFORMATIONAL_STRING_TRADEMARK = 3,
    DWRITE_INFORMATIONAL_STRING_MANUFACTURER = 4,
    DWRITE_INFORMATIONAL_STRING_DESIGNER = 5,
    DWRITE_INFORMATIONAL_STRING_DESIGNER_URL = 6,
    DWRITE_INFORMATIONAL_STRING_DESCRIPTION = 7,
    DWRITE_INFORMATIONAL_STRING_FONT_VENDOR_URL = 8,
    DWRITE_INFORMATIONAL_STRING_LICENSE_DESCRIPTION = 9,
    DWRITE_INFORMATIONAL_STRING_LICENSE_INFO_URL = 10,
    DWRITE_INFORMATIONAL_STRING_WIN32_FAMILY_NAMES = 11,
    DWRITE_INFORMATIONAL_STRING_WIN32_SUBFAMILY_NAMES = 12,
    DWRITE_INFORMATIONAL_STRING_TYPOGRAPHIC_FAMILY_NAMES = 13,
    DWRITE_INFORMATIONAL_STRING_PREFERRED_FAMILY_NAMES = 13,
    DWRITE_INFORMATIONAL_STRING_TYPOGRAPHIC_SUBFAMILY_NAMES = 14,
    DWRITE_INFORMATIONAL_STRING_PREFERRED_SUBFAMILY_NAMES = 14,
    DWRITE_INFORMATIONAL_STRING_SAMPLE_TEXT = 15,
    DWRITE_INFORMATIONAL_STRING_FULL_NAME = 16,
    DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_NAME = 17,
    DWRITE_INFORMATIONAL_STRING_POSTSCRIPT_CID_NAME = 18,
    DWRITE_INFORMATIONAL_STRING_WEIGHT_STRETCH_STYLE_FAMILY_NAME = 19,
    DWRITE_INFORMATIONAL_STRING_DESIGN_SCRIPT_LANGUAGE_TAG = 20,
    DWRITE_INFORMATIONAL_STRING_SUPPORTED_SCRIPT_LANGUAGE_TAG = 21
} DWRITE_INFORMATIONAL_STRING_ID;

typedef enum DWRITE_PIXEL_GEOMETRY {
    DWRITE_PIXEL_GEOMETRY_FLAT = 0,
    DWRITE_PIXEL_GEOMETRY_RGB = 1,
    DWRITE_PIXEL_GEOMETRY_BGR = 2
} DWRITE_PIXEL_GEOMETRY;

typedef enum DWRITE_RENDERING_MODE {
    DWRITE_RENDERING_MODE_DEFAULT = 0,
    DWRITE_RENDERING_MODE_ALIASED = 1,
    DWRITE_RENDERING_MODE_GDI_CLASSIC = 2,
    DWRITE_RENDERING_MODE_GDI_NATURAL = 3,
    DWRITE_RENDERING_MODE_NATURAL = 4,
    DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC = 5,
    DWRITE_RENDERING_MODE_OUTLINE = 6
} DWRITE_RENDERING_MODE;

typedef enum DWRITE_MEASURING_MODE {
    DWRITE_MEASURING_MODE_NATURAL = 0,
    DWRITE_MEASURING_MODE_GDI_CLASSIC = 1,
    DWRITE_MEASURING_MODE_GDI_NATURAL = 2
} DWRITE_MEASURING_MODE;

typedef enum DWRITE_GRID_FIT_MODE {
    DWRITE_GRID_FIT_MODE_DEFAULT = 0,
    DWRITE_GRID_FIT_MODE_DISABLED = 1,
    DWRITE_GRID_FIT_MODE_ENABLED = 2
} DWRITE_GRID_FIT_MODE;

typedef enum DWRITE_TEXT_ANTIALIAS_MODE {
    DWRITE_TEXT_ANTIALIAS_MODE_CLEARTYPE = 0,
    DWRITE_TEXT_ANTIALIAS_MODE_GRAYSCALE = 1
} DWRITE_TEXT_ANTIALIAS_MODE;

typedef enum DWRITE_TEXTURE_TYPE {
    DWRITE_TEXTURE_ALIASED_1x1 = 0,
    DWRITE_TEXTURE_CLEARTYPE_3x1 = 1
} DWRITE_TEXTURE_TYPE;

typedef enum DWRITE_OUTLINE_THRESHOLD {
    DWRITE_OUTLINE_THRESHOLD_ANTIALIASED = 0,
    DWRITE_OUTLINE_THRESHOLD_ALIASED = 1
} DWRITE_OUTLINE_THRESHOLD;

typedef enum DWRITE_READING_DIRECTION {
    DWRITE_READING_DIRECTION_LEFT_TO_RIGHT = 0,
    DWRITE_READING_DIRECTION_RIGHT_TO_LEFT = 1,
    DWRITE_READING_DIRECTION_TOP_TO_BOTTOM = 2,
    DWRITE_READING_DIRECTION_BOTTOM_TO_TOP = 3
} DWRITE_READING_DIRECTION;

/* ------------------------------------------------------------------ structures */

typedef struct DWRITE_FONT_METRICS {
    UINT16 designUnitsPerEm;
    UINT16 ascent;
    UINT16 descent;
    INT16 lineGap;
    UINT16 capHeight;
    UINT16 xHeight;
    INT16 underlinePosition;
    UINT16 underlineThickness;
    INT16 strikethroughPosition;
    UINT16 strikethroughThickness;
} DWRITE_FONT_METRICS;

typedef struct DWRITE_FONT_METRICS1 {
    UINT16 designUnitsPerEm;
    UINT16 ascent;
    UINT16 descent;
    INT16 lineGap;
    UINT16 capHeight;
    UINT16 xHeight;
    INT16 underlinePosition;
    UINT16 underlineThickness;
    INT16 strikethroughPosition;
    UINT16 strikethroughThickness;
    INT16 glyphBoxLeft;
    INT16 glyphBoxTop;
    INT16 glyphBoxRight;
    INT16 glyphBoxBottom;
    INT16 subscriptPositionX;
    INT16 subscriptPositionY;
    INT16 subscriptSizeX;
    INT16 subscriptSizeY;
    INT16 superscriptPositionX;
    INT16 superscriptPositionY;
    INT16 superscriptSizeX;
    INT16 superscriptSizeY;
    BOOL hasTypographicMetrics;
} DWRITE_FONT_METRICS1;

typedef struct DWRITE_GLYPH_METRICS {
    INT32 leftSideBearing;
    UINT32 advanceWidth;
    INT32 rightSideBearing;
    INT32 topSideBearing;
    UINT32 advanceHeight;
    INT32 bottomSideBearing;
    INT32 verticalOriginY;
} DWRITE_GLYPH_METRICS;

typedef struct DWRITE_GLYPH_OFFSET {
    FLOAT advanceOffset;
    FLOAT ascenderOffset;
} DWRITE_GLYPH_OFFSET;

typedef struct DWRITE_MATRIX {
    FLOAT m11, m12, m21, m22, dx, dy;
} DWRITE_MATRIX;

typedef struct DWRITE_UNICODE_RANGE {
    UINT32 first;
    UINT32 last;
} DWRITE_UNICODE_RANGE;

typedef struct DWRITE_PANOSE {
    UINT8 values[10];
} DWRITE_PANOSE;

typedef struct DWRITE_CARET_METRICS {
    INT16 slopeRise;
    INT16 slopeRun;
    INT16 offset;
} DWRITE_CARET_METRICS;

typedef struct DWRITE_COLOR_F {
    FLOAT r, g, b, a;
} DWRITE_COLOR_F;

typedef struct DWRITE_GLYPH_RUN_DESCRIPTION {
    const WCHAR *localeName;
    const WCHAR *string;
    UINT32 stringLength;
    const UINT16 *clusterMap;
    UINT32 textPosition;
} DWRITE_GLYPH_RUN_DESCRIPTION;

typedef struct IDWriteFontFace IDWriteFontFace;
typedef struct IDWriteFontFile IDWriteFontFile;
typedef struct IDWriteFontFileLoader IDWriteFontFileLoader;
typedef struct IDWriteFontFileStream IDWriteFontFileStream;
typedef struct IDWriteFontCollectionLoader IDWriteFontCollectionLoader;
typedef struct IDWriteFontFileEnumerator IDWriteFontFileEnumerator;
typedef struct IDWriteFontCollection IDWriteFontCollection;
typedef struct IDWriteFontList IDWriteFontList;
typedef struct IDWriteFontFamily IDWriteFontFamily;
typedef struct IDWriteFont IDWriteFont;
typedef struct IDWriteLocalizedStrings IDWriteLocalizedStrings;
typedef struct IDWriteRenderingParams IDWriteRenderingParams;
typedef struct IDWriteGdiInterop IDWriteGdiInterop;
typedef struct IDWriteGlyphRunAnalysis IDWriteGlyphRunAnalysis;
typedef struct IDWriteFactory IDWriteFactory;
typedef struct IDWriteFontFallback IDWriteFontFallback;
typedef struct IDWriteTextAnalysisSource IDWriteTextAnalysisSource;
typedef struct IDWriteNumberSubstitution IDWriteNumberSubstitution;
typedef struct IDWriteBitmapRenderTarget IDWriteBitmapRenderTarget;
typedef struct IDWriteGeometrySink IDWriteGeometrySink;
typedef struct IDWriteTextFormat IDWriteTextFormat;
typedef struct IDWriteTextLayout IDWriteTextLayout;
typedef struct IDWriteTypography IDWriteTypography;
typedef struct IDWriteInlineObject IDWriteInlineObject;
typedef struct IDWriteTextAnalyzer IDWriteTextAnalyzer;
typedef struct IDWriteFontFallbackBuilder IDWriteFontFallbackBuilder;
typedef struct IDWriteColorGlyphRunEnumerator IDWriteColorGlyphRunEnumerator;
typedef struct IDWriteRenderingParams1 IDWriteRenderingParams1;
typedef struct IDWriteRenderingParams2 IDWriteRenderingParams2;
typedef struct IDWriteFactory1 IDWriteFactory1;
typedef struct IDWriteFactory2 IDWriteFactory2;
typedef struct IDWriteFactory3 IDWriteFactory3;
typedef struct IDWriteFontSet IDWriteFontSet;
typedef struct IDWriteFontSetBuilder IDWriteFontSetBuilder;
typedef struct IDWriteFontCollection1 IDWriteFontCollection1;
typedef struct IDWriteFontFaceReference IDWriteFontFaceReference;
typedef struct IDWriteRenderingParams3 IDWriteRenderingParams3;
typedef struct IDWriteFontDownloadQueue IDWriteFontDownloadQueue;
typedef enum DWRITE_RENDERING_MODE1 {
    DWRITE_RENDERING_MODE1_DEFAULT = 0,
    DWRITE_RENDERING_MODE1_ALIASED = 1,
    DWRITE_RENDERING_MODE1_GDI_CLASSIC = 2,
    DWRITE_RENDERING_MODE1_GDI_NATURAL = 3,
    DWRITE_RENDERING_MODE1_NATURAL = 4,
    DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC = 5,
    DWRITE_RENDERING_MODE1_OUTLINE = 6,
    DWRITE_RENDERING_MODE1_NATURAL_SYMMETRIC_DOWNSAMPLED = 7
} DWRITE_RENDERING_MODE1;
typedef struct IDWriteFont1 IDWriteFont1;
typedef struct IDWriteFontFace1 IDWriteFontFace1;
typedef struct IDWriteFontFace2 IDWriteFontFace2;
typedef struct IDWriteLocalFontFileLoader IDWriteLocalFontFileLoader;
typedef struct IDWritePixelSnapping IDWritePixelSnapping;
typedef struct IDWriteTextRenderer IDWriteTextRenderer;
typedef struct IDWriteTextAnalysisSink IDWriteTextAnalysisSink;
typedef struct IDWriteTextAnalysisSource1 IDWriteTextAnalysisSource1;
typedef struct IDWriteTextAnalysisSink1 IDWriteTextAnalysisSink1;
typedef struct IDWriteTextAnalyzer1 IDWriteTextAnalyzer1;
typedef struct IDWriteTextAnalyzer2 IDWriteTextAnalyzer2;
typedef struct IDWriteTextLayout1 IDWriteTextLayout1;
typedef struct IDWriteTextLayout2 IDWriteTextLayout2;
typedef struct IDWriteTextLayout3 IDWriteTextLayout3;
typedef struct IDWriteTextLayout4 IDWriteTextLayout4;
typedef struct IDWriteTextRenderer1 IDWriteTextRenderer1;
typedef struct IDWriteTextFormat1 IDWriteTextFormat1;
typedef struct IDWriteTextFormat2 IDWriteTextFormat2;
typedef struct IDWriteTextFormat3 IDWriteTextFormat3;

typedef struct DWRITE_GLYPH_RUN {
    IDWriteFontFace *fontFace;
    FLOAT fontEmSize;
    UINT32 glyphCount;
    const UINT16 *glyphIndices;
    const FLOAT *glyphAdvances;
    const DWRITE_GLYPH_OFFSET *glyphOffsets;
    BOOL isSideways;
    UINT32 bidiLevel;
} DWRITE_GLYPH_RUN;

/* ------------------------------------------------------------------ text layout types */

typedef enum DWRITE_TEXT_ALIGNMENT {
    DWRITE_TEXT_ALIGNMENT_LEADING = 0,
    DWRITE_TEXT_ALIGNMENT_TRAILING = 1,
    DWRITE_TEXT_ALIGNMENT_CENTER = 2,
    DWRITE_TEXT_ALIGNMENT_JUSTIFIED = 3
} DWRITE_TEXT_ALIGNMENT;
typedef enum DWRITE_PARAGRAPH_ALIGNMENT {
    DWRITE_PARAGRAPH_ALIGNMENT_NEAR = 0,
    DWRITE_PARAGRAPH_ALIGNMENT_FAR = 1,
    DWRITE_PARAGRAPH_ALIGNMENT_CENTER = 2
} DWRITE_PARAGRAPH_ALIGNMENT;
typedef enum DWRITE_WORD_WRAPPING {
    DWRITE_WORD_WRAPPING_WRAP = 0,
    DWRITE_WORD_WRAPPING_NO_WRAP = 1,
    DWRITE_WORD_WRAPPING_EMERGENCY_BREAK = 2,
    DWRITE_WORD_WRAPPING_WHOLE_WORD = 3,
    DWRITE_WORD_WRAPPING_CHARACTER = 4
} DWRITE_WORD_WRAPPING;
typedef enum DWRITE_READING_DIRECTION {
    DWRITE_READING_DIRECTION_LEFT_TO_RIGHT = 0,
    DWRITE_READING_DIRECTION_RIGHT_TO_LEFT = 1,
    DWRITE_READING_DIRECTION_TOP_TO_BOTTOM = 2,
    DWRITE_READING_DIRECTION_BOTTOM_TO_TOP = 3
} DWRITE_READING_DIRECTION;
typedef enum DWRITE_FLOW_DIRECTION {
    DWRITE_FLOW_DIRECTION_TOP_TO_BOTTOM = 0,
    DWRITE_FLOW_DIRECTION_BOTTOM_TO_TOP = 1,
    DWRITE_FLOW_DIRECTION_LEFT_TO_RIGHT = 2,
    DWRITE_FLOW_DIRECTION_RIGHT_TO_LEFT = 3
} DWRITE_FLOW_DIRECTION;
typedef enum DWRITE_TRIMMING_GRANULARITY {
    DWRITE_TRIMMING_GRANULARITY_NONE = 0,
    DWRITE_TRIMMING_GRANULARITY_CHARACTER = 1,
    DWRITE_TRIMMING_GRANULARITY_WORD = 2
} DWRITE_TRIMMING_GRANULARITY;
typedef enum DWRITE_BREAK_CONDITION {
    DWRITE_BREAK_CONDITION_NEUTRAL = 0,
    DWRITE_BREAK_CONDITION_CAN_BREAK = 1,
    DWRITE_BREAK_CONDITION_MAY_NOT_BREAK = 2,
    DWRITE_BREAK_CONDITION_MUST_BREAK = 3
} DWRITE_BREAK_CONDITION;
typedef enum DWRITE_LINE_SPACING_METHOD {
    DWRITE_LINE_SPACING_METHOD_DEFAULT = 0,
    DWRITE_LINE_SPACING_METHOD_UNIFORM = 1,
    DWRITE_LINE_SPACING_METHOD_PROPORTIONAL = 2
} DWRITE_LINE_SPACING_METHOD;
typedef enum DWRITE_FONT_FEATURE_TAG {
    DWRITE_FONT_FEATURE_TAG_ALTERNATIVE_FRACTIONS = 0x63726661,
    DWRITE_FONT_FEATURE_TAG_PETITE_CAPITALS_FROM_CAPITALS = 0x63703263,
    DWRITE_FONT_FEATURE_TAG_SMALL_CAPITALS_FROM_CAPITALS = 0x63733263,
    DWRITE_FONT_FEATURE_TAG_CONTEXTUAL_ALTERNATES = 0x746c6163,
    DWRITE_FONT_FEATURE_TAG_CASE_SENSITIVE_FORMS = 0x65736163,
    DWRITE_FONT_FEATURE_TAG_GLYPH_COMPOSITION_DECOMPOSITION = 0x706d6363,
    DWRITE_FONT_FEATURE_TAG_CONTEXTUAL_LIGATURES = 0x67696c63,
    DWRITE_FONT_FEATURE_TAG_CAPITAL_SPACING = 0x70737063,
    DWRITE_FONT_FEATURE_TAG_CONTEXTUAL_SWASH = 0x68777363,
    DWRITE_FONT_FEATURE_TAG_CURSIVE_POSITIONING = 0x73727563,
    DWRITE_FONT_FEATURE_TAG_DEFAULT = 0x746c6664,
    DWRITE_FONT_FEATURE_TAG_DISCRETIONARY_LIGATURES = 0x67696c64,
    DWRITE_FONT_FEATURE_TAG_EXPERT_FORMS = 0x74707865,
    DWRITE_FONT_FEATURE_TAG_FRACTIONS = 0x63617266,
    DWRITE_FONT_FEATURE_TAG_FULL_WIDTH = 0x64697766,
    DWRITE_FONT_FEATURE_TAG_HALF_FORMS = 0x666c6168,
    DWRITE_FONT_FEATURE_TAG_HALANT_FORMS = 0x6e6c6168,
    DWRITE_FONT_FEATURE_TAG_ALTERNATE_HALF_WIDTH = 0x746c6168,
    DWRITE_FONT_FEATURE_TAG_HISTORICAL_FORMS = 0x74736968,
    DWRITE_FONT_FEATURE_TAG_HORIZONTAL_KANA_ALTERNATES = 0x616e6b68,
    DWRITE_FONT_FEATURE_TAG_HISTORICAL_LIGATURES = 0x67696c68,
    DWRITE_FONT_FEATURE_TAG_HALF_WIDTH = 0x64697768,
    DWRITE_FONT_FEATURE_TAG_HOJO_KANJI_FORMS = 0x6f6a6f68,
    DWRITE_FONT_FEATURE_TAG_JIS04_FORMS = 0x3430706a,
    DWRITE_FONT_FEATURE_TAG_JIS78_FORMS = 0x3837706a,
    DWRITE_FONT_FEATURE_TAG_JIS83_FORMS = 0x3338706a,
    DWRITE_FONT_FEATURE_TAG_JIS90_FORMS = 0x3039706a,
    DWRITE_FONT_FEATURE_TAG_KERNING = 0x6e72656b,
    DWRITE_FONT_FEATURE_TAG_STANDARD_LIGATURES = 0x6167696c,
    DWRITE_FONT_FEATURE_TAG_LINING_FIGURES = 0x6d756e6c,
    DWRITE_FONT_FEATURE_TAG_LOCALIZED_FORMS = 0x6c636f6c,
    DWRITE_FONT_FEATURE_TAG_MARK_POSITIONING = 0x6b72616d,
    DWRITE_FONT_FEATURE_TAG_MATHEMATICAL_GREEK = 0x6b72676d,
    DWRITE_FONT_FEATURE_TAG_MARK_TO_MARK_POSITIONING = 0x6b6d6b6d,
    DWRITE_FONT_FEATURE_TAG_ALTERNATE_ANNOTATION_FORMS = 0x746c616e,
    DWRITE_FONT_FEATURE_TAG_NLC_KANJI_FORMS = 0x6b636c6e,
    DWRITE_FONT_FEATURE_TAG_OLD_STYLE_FIGURES = 0x6d756e6f,
    DWRITE_FONT_FEATURE_TAG_ORDINALS = 0x6e64726f,
    DWRITE_FONT_FEATURE_TAG_PROPORTIONAL_ALTERNATE_WIDTH = 0x746c6170,
    DWRITE_FONT_FEATURE_TAG_PETITE_CAPITALS = 0x70616370,
    DWRITE_FONT_FEATURE_TAG_PROPORTIONAL_FIGURES = 0x6d756e70,
    DWRITE_FONT_FEATURE_TAG_PROPORTIONAL_WIDTHS = 0x64697770,
    DWRITE_FONT_FEATURE_TAG_QUARTER_WIDTHS = 0x64697771,
    DWRITE_FONT_FEATURE_TAG_REQUIRED_LIGATURES = 0x67696c72,
    DWRITE_FONT_FEATURE_TAG_RUBY_NOTATION_FORMS = 0x79627572,
    DWRITE_FONT_FEATURE_TAG_STYLISTIC_ALTERNATES = 0x746c6173,
    DWRITE_FONT_FEATURE_TAG_SCIENTIFIC_INFERIORS = 0x666e6973,
    DWRITE_FONT_FEATURE_TAG_SMALL_CAPITALS = 0x70636d73,
    DWRITE_FONT_FEATURE_TAG_SIMPLIFIED_FORMS = 0x6c706d73,
    DWRITE_FONT_FEATURE_TAG_STYLISTIC_SET_1 = 0x31307373,
    DWRITE_FONT_FEATURE_TAG_STYLISTIC_SET_2 = 0x32307373,
    DWRITE_FONT_FEATURE_TAG_STYLISTIC_SET_3 = 0x33307373,
    DWRITE_FONT_FEATURE_TAG_STYLISTIC_SET_4 = 0x34307373,
    DWRITE_FONT_FEATURE_TAG_STYLISTIC_SET_5 = 0x35307373,
    DWRITE_FONT_FEATURE_TAG_STYLISTIC_SET_6 = 0x36307373,
    DWRITE_FONT_FEATURE_TAG_STYLISTIC_SET_7 = 0x37307373,
    DWRITE_FONT_FEATURE_TAG_STYLISTIC_SET_8 = 0x38307373,
    DWRITE_FONT_FEATURE_TAG_STYLISTIC_SET_9 = 0x39307373,
    DWRITE_FONT_FEATURE_TAG_STYLISTIC_SET_10 = 0x30317373,
    DWRITE_FONT_FEATURE_TAG_STYLISTIC_SET_11 = 0x31317373,
    DWRITE_FONT_FEATURE_TAG_STYLISTIC_SET_12 = 0x32317373,
    DWRITE_FONT_FEATURE_TAG_STYLISTIC_SET_13 = 0x33317373,
    DWRITE_FONT_FEATURE_TAG_STYLISTIC_SET_14 = 0x34317373,
    DWRITE_FONT_FEATURE_TAG_STYLISTIC_SET_15 = 0x35317373,
    DWRITE_FONT_FEATURE_TAG_STYLISTIC_SET_16 = 0x36317373,
    DWRITE_FONT_FEATURE_TAG_STYLISTIC_SET_17 = 0x37317373,
    DWRITE_FONT_FEATURE_TAG_STYLISTIC_SET_18 = 0x38317373,
    DWRITE_FONT_FEATURE_TAG_STYLISTIC_SET_19 = 0x39317373,
    DWRITE_FONT_FEATURE_TAG_STYLISTIC_SET_20 = 0x30327373,
    DWRITE_FONT_FEATURE_TAG_SUBSCRIPT = 0x73627573,
    DWRITE_FONT_FEATURE_TAG_SUPERSCRIPT = 0x73707573,
    DWRITE_FONT_FEATURE_TAG_SWASH = 0x68737773,
    DWRITE_FONT_FEATURE_TAG_TITLING = 0x6c746974,
    DWRITE_FONT_FEATURE_TAG_TRADITIONAL_NAME_FORMS = 0x6d616e74,
    DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES = 0x6d756e74,
    DWRITE_FONT_FEATURE_TAG_TRADITIONAL_FORMS = 0x64617274,
    DWRITE_FONT_FEATURE_TAG_THIRD_WIDTHS = 0x64697774,
    DWRITE_FONT_FEATURE_TAG_UNICASE = 0x63696e75,
    DWRITE_FONT_FEATURE_TAG_VERTICAL_WRITING = 0x74726576,
    DWRITE_FONT_FEATURE_TAG_VERTICAL_ALTERNATES_AND_ROTATION = 0x32747276,
    DWRITE_FONT_FEATURE_TAG_SLASHED_ZERO = 0x6f72657a
} DWRITE_FONT_FEATURE_TAG;
#define DWRITE_MAKE_OPENTYPE_TAG(a, b, c, d) \
    (((UINT32)(UINT8)(d) << 24) | ((UINT32)(UINT8)(c) << 16) | ((UINT32)(UINT8)(b) << 8) | (UINT32)(UINT8)(a))
typedef enum DWRITE_SCRIPT_SHAPES {
    DWRITE_SCRIPT_SHAPES_DEFAULT = 0,
    DWRITE_SCRIPT_SHAPES_NO_VISUAL = 1
} DWRITE_SCRIPT_SHAPES;
typedef enum DWRITE_NUMBER_SUBSTITUTION_METHOD {
    DWRITE_NUMBER_SUBSTITUTION_METHOD_FROM_CULTURE = 0,
    DWRITE_NUMBER_SUBSTITUTION_METHOD_CONTEXTUAL = 1,
    DWRITE_NUMBER_SUBSTITUTION_METHOD_NONE = 2,
    DWRITE_NUMBER_SUBSTITUTION_METHOD_NATIONAL = 3,
    DWRITE_NUMBER_SUBSTITUTION_METHOD_TRADITIONAL = 4
} DWRITE_NUMBER_SUBSTITUTION_METHOD;
typedef struct DWRITE_TRIMMING {
    DWRITE_TRIMMING_GRANULARITY granularity;
    UINT32 delimiter;
    UINT32 delimiterCount;
} DWRITE_TRIMMING;
typedef struct DWRITE_UNDERLINE {
    FLOAT width;
    FLOAT thickness;
    FLOAT offset;
    FLOAT runHeight;
    DWRITE_READING_DIRECTION readingDirection;
    DWRITE_FLOW_DIRECTION flowDirection;
    const WCHAR *localeName;
    DWRITE_MEASURING_MODE measuringMode;
} DWRITE_UNDERLINE;
typedef struct DWRITE_STRIKETHROUGH {
    FLOAT width;
    FLOAT thickness;
    FLOAT offset;
    DWRITE_READING_DIRECTION readingDirection;
    DWRITE_FLOW_DIRECTION flowDirection;
    const WCHAR *localeName;
    DWRITE_MEASURING_MODE measuringMode;
} DWRITE_STRIKETHROUGH;
typedef struct DWRITE_INLINE_OBJECT_METRICS {
    FLOAT width;
    FLOAT height;
    FLOAT baseline;
    BOOL supportsSideways;
} DWRITE_INLINE_OBJECT_METRICS;
typedef struct DWRITE_OVERHANG_METRICS {
    FLOAT left;
    FLOAT top;
    FLOAT right;
    FLOAT bottom;
} DWRITE_OVERHANG_METRICS;
typedef struct DWRITE_FONT_FEATURE {
    DWRITE_FONT_FEATURE_TAG nameTag;
    UINT32 parameter;
} DWRITE_FONT_FEATURE;
typedef struct DWRITE_TEXT_RANGE {
    UINT32 startPosition;
    UINT32 length;
} DWRITE_TEXT_RANGE;
typedef struct DWRITE_LINE_METRICS {
    UINT32 length;
    UINT32 trailingWhitespaceLength;
    UINT32 newlineLength;
    FLOAT height;
    FLOAT baseline;
    BOOL isTrimmed;
} DWRITE_LINE_METRICS;
typedef struct DWRITE_TEXT_METRICS {
    FLOAT left;
    FLOAT top;
    FLOAT width;
    FLOAT widthIncludingTrailingWhitespace;
    FLOAT height;
    FLOAT layoutWidth;
    FLOAT layoutHeight;
    UINT32 maxBidiReorderingDepth;
    UINT32 lineCount;
} DWRITE_TEXT_METRICS;
typedef struct DWRITE_CLUSTER_METRICS {
    FLOAT width;
    UINT16 length;
    UINT16 canWrapLineAfter : 1;
    UINT16 isWhitespace : 1;
    UINT16 isNewline : 1;
    UINT16 isSoftHyphen : 1;
    UINT16 isRightToLeft : 1;
    UINT16 padding : 11;
} DWRITE_CLUSTER_METRICS;
typedef struct DWRITE_HIT_TEST_METRICS {
    UINT32 textPosition;
    UINT32 length;
    FLOAT left;
    FLOAT top;
    FLOAT width;
    FLOAT height;
    UINT32 bidiLevel;
    BOOL isText;
    BOOL isTrimmed;
} DWRITE_HIT_TEST_METRICS;
typedef struct DWRITE_SCRIPT_ANALYSIS {
    UINT16 script;
    DWRITE_SCRIPT_SHAPES shapes;
} DWRITE_SCRIPT_ANALYSIS;
typedef struct DWRITE_LINE_BREAKPOINT {
    UINT8 breakConditionBefore : 2;
    UINT8 breakConditionAfter : 2;
    UINT8 isWhitespace : 1;
    UINT8 isSoftHyphen : 1;
    UINT8 padding : 2;
} DWRITE_LINE_BREAKPOINT;
typedef struct DWRITE_TYPOGRAPHIC_FEATURES {
    DWRITE_FONT_FEATURE *features;
    UINT32 featureCount;
} DWRITE_TYPOGRAPHIC_FEATURES;
typedef struct DWRITE_SHAPING_TEXT_PROPERTIES {
    UINT16 isShapedAlone : 1;
    UINT16 reserved1 : 1;
    UINT16 canBreakShapingAfter : 1;
    UINT16 reserved : 13;
} DWRITE_SHAPING_TEXT_PROPERTIES;
typedef struct DWRITE_SHAPING_GLYPH_PROPERTIES {
    UINT16 justification : 4;
    UINT16 isClusterStart : 1;
    UINT16 isDiacritic : 1;
    UINT16 isZeroWidthSpace : 1;
    UINT16 reserved : 9;
} DWRITE_SHAPING_GLYPH_PROPERTIES;
typedef enum DWRITE_BASELINE {
    DWRITE_BASELINE_DEFAULT = 0,
    DWRITE_BASELINE_ROMAN = 1,
    DWRITE_BASELINE_CENTRAL = 2,
    DWRITE_BASELINE_MATH = 3,
    DWRITE_BASELINE_HANGING = 4,
    DWRITE_BASELINE_IDEOGRAPHIC_BOTTOM = 5,
    DWRITE_BASELINE_IDEOGRAPHIC_TOP = 6,
    DWRITE_BASELINE_MINIMUM = 7,
    DWRITE_BASELINE_MAXIMUM = 8
} DWRITE_BASELINE;
typedef enum DWRITE_VERTICAL_GLYPH_ORIENTATION {
    DWRITE_VERTICAL_GLYPH_ORIENTATION_DEFAULT = 0,
    DWRITE_VERTICAL_GLYPH_ORIENTATION_STACKED = 1
} DWRITE_VERTICAL_GLYPH_ORIENTATION;
typedef enum DWRITE_GLYPH_ORIENTATION_ANGLE {
    DWRITE_GLYPH_ORIENTATION_ANGLE_0_DEGREES = 0,
    DWRITE_GLYPH_ORIENTATION_ANGLE_90_DEGREES = 1,
    DWRITE_GLYPH_ORIENTATION_ANGLE_180_DEGREES = 2,
    DWRITE_GLYPH_ORIENTATION_ANGLE_270_DEGREES = 3
} DWRITE_GLYPH_ORIENTATION_ANGLE;
typedef struct DWRITE_SCRIPT_PROPERTIES {
    UINT32 isoScriptCode;
    UINT32 isoScriptNumber;
    UINT32 clusterLookahead;
    UINT32 justificationCharacter;
    UINT32 restrictCaretToClusters : 1;
    UINT32 usesWordDividers : 1;
    UINT32 isDiscreteWriting : 1;
    UINT32 isBlockWriting : 1;
    UINT32 isDistributedWithinCluster : 1;
    UINT32 isConnectedWriting : 1;
    UINT32 isCursiveWriting : 1;
    UINT32 reserved : 25;
} DWRITE_SCRIPT_PROPERTIES;
typedef struct DWRITE_JUSTIFICATION_OPPORTUNITY {
    FLOAT expansionMinimum;
    FLOAT expansionMaximum;
    FLOAT compressionMaximum;
    UINT32 expansionPriority : 8;
    UINT32 compressionPriority : 8;
    UINT32 allowResidualExpansion : 1;
    UINT32 allowResidualCompression : 1;
    UINT32 applyToLeadingEdge : 1;
    UINT32 applyToTrailingEdge : 1;
    UINT32 reserved : 12;
} DWRITE_JUSTIFICATION_OPPORTUNITY;
typedef enum DWRITE_OPTICAL_ALIGNMENT {
    DWRITE_OPTICAL_ALIGNMENT_NONE = 0,
    DWRITE_OPTICAL_ALIGNMENT_NO_SIDE_BEARINGS = 1
} DWRITE_OPTICAL_ALIGNMENT;
typedef struct DWRITE_TEXT_METRICS1 {
    FLOAT left;
    FLOAT top;
    FLOAT width;
    FLOAT widthIncludingTrailingWhitespace;
    FLOAT height;
    FLOAT layoutWidth;
    FLOAT layoutHeight;
    UINT32 maxBidiReorderingDepth;
    UINT32 lineCount;
    FLOAT heightIncludingTrailingWhitespace;
} DWRITE_TEXT_METRICS1;
typedef enum DWRITE_FONT_AXIS_TAG {
    DWRITE_FONT_AXIS_TAG_WEIGHT = 0x74686777,
    DWRITE_FONT_AXIS_TAG_WIDTH = 0x68746477,
    DWRITE_FONT_AXIS_TAG_SLANT = 0x746e6c73,
    DWRITE_FONT_AXIS_TAG_OPTICAL_SIZE = 0x7a73706f,
    DWRITE_FONT_AXIS_TAG_ITALIC = 0x6c617469
} DWRITE_FONT_AXIS_TAG;
typedef struct DWRITE_FONT_AXIS_VALUE {
    DWRITE_FONT_AXIS_TAG axisTag;
    FLOAT value;
} DWRITE_FONT_AXIS_VALUE;
typedef struct DWRITE_FONT_AXIS_RANGE {
    DWRITE_FONT_AXIS_TAG axisTag;
    FLOAT minValue;
    FLOAT maxValue;
} DWRITE_FONT_AXIS_RANGE;
typedef enum DWRITE_AUTOMATIC_FONT_AXES {
    DWRITE_AUTOMATIC_FONT_AXES_NONE = 0,
    DWRITE_AUTOMATIC_FONT_AXES_OPTICAL_SIZE = 1
} DWRITE_AUTOMATIC_FONT_AXES;
typedef struct DWRITE_LINE_METRICS1 {
    UINT32 length;
    UINT32 trailingWhitespaceLength;
    UINT32 newlineLength;
    FLOAT height;
    FLOAT baseline;
    BOOL isTrimmed;
    FLOAT leadingBefore;
    FLOAT leadingAfter;
} DWRITE_LINE_METRICS1;
typedef enum DWRITE_FONT_LINE_GAP_USAGE {
    DWRITE_FONT_LINE_GAP_USAGE_DEFAULT = 0,
    DWRITE_FONT_LINE_GAP_USAGE_DISABLED = 1,
    DWRITE_FONT_LINE_GAP_USAGE_ENABLED = 2
} DWRITE_FONT_LINE_GAP_USAGE;
typedef struct DWRITE_LINE_SPACING {
    DWRITE_LINE_SPACING_METHOD method;
    FLOAT height;
    FLOAT baseline;
    FLOAT leadingBefore;
    DWRITE_FONT_LINE_GAP_USAGE fontLineGapUsage;
} DWRITE_LINE_SPACING;

/* direct2d's simplified geometry, used to describe glyph outlines. */
typedef D2D_POINT_2F D2D1_POINT_2F;

typedef struct D2D1_BEZIER_SEGMENT {
    D2D1_POINT_2F point1, point2, point3;
} D2D1_BEZIER_SEGMENT;

typedef enum D2D1_FILL_MODE { D2D1_FILL_MODE_ALTERNATE = 0, D2D1_FILL_MODE_WINDING = 1 } D2D1_FILL_MODE;
typedef enum D2D1_FIGURE_BEGIN { D2D1_FIGURE_BEGIN_FILLED = 0, D2D1_FIGURE_BEGIN_HOLLOW = 1 } D2D1_FIGURE_BEGIN;
typedef enum D2D1_FIGURE_END { D2D1_FIGURE_END_OPEN = 0, D2D1_FIGURE_END_CLOSED = 1 } D2D1_FIGURE_END;
typedef enum D2D1_PATH_SEGMENT {
    D2D1_PATH_SEGMENT_NONE = 0,
    D2D1_PATH_SEGMENT_FORCE_UNSTROKED = 1,
    D2D1_PATH_SEGMENT_FORCE_ROUND_LINE_JOIN = 2
} D2D1_PATH_SEGMENT;

/* ------------------------------------------------------------------ locale strings and loaders */

#define IDWriteLocalizedStrings_METHODS(T)                                                                             \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    UINT32(STDMETHODCALLTYPE *GetCount)(T * This);                                                                     \
    HRESULT(STDMETHODCALLTYPE *FindLocaleName)(T * This, const WCHAR *locale, UINT32 *index, BOOL *exists);            \
    HRESULT(STDMETHODCALLTYPE *GetLocaleNameLength)(T * This, UINT32 index, UINT32 *length);                           \
    HRESULT(STDMETHODCALLTYPE *GetLocaleName)(T * This, UINT32 index, WCHAR *name, UINT32 size);                       \
    HRESULT(STDMETHODCALLTYPE *GetStringLength)(T * This, UINT32 index, UINT32 *length);                               \
    HRESULT(STDMETHODCALLTYPE *GetString)(T * This, UINT32 index, WCHAR *string, UINT32 size);
MIME_COM_INTERFACE(IDWriteLocalizedStrings)

#define IDWriteFontFileStream_METHODS(T)                                                                               \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *ReadFileFragment)(T * This, const void **start, UINT64 offset, UINT64 size,             \
                                                 void **context);                                                      \
    void(STDMETHODCALLTYPE *ReleaseFileFragment)(T * This, void *context);                                             \
    HRESULT(STDMETHODCALLTYPE *GetFileSize)(T * This, UINT64 *size);                                                   \
    HRESULT(STDMETHODCALLTYPE *GetLastWriteTime)(T * This, UINT64 *time);
MIME_COM_INTERFACE(IDWriteFontFileStream)

#define IDWriteFontFileLoader_METHODS(T)                                                                               \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateStreamFromKey)(T * This, const void *key, UINT32 size,                            \
                                                    IDWriteFontFileStream **stream);
MIME_COM_INTERFACE(IDWriteFontFileLoader)

#define IDWriteLocalFontFileLoader_METHODS(T)                                                                          \
    IDWriteFontFileLoader_METHODS(T)                                                                                   \
    HRESULT(STDMETHODCALLTYPE *GetFilePathLengthFromKey)(T * This, const void *key, UINT32 size, UINT32 *length);      \
    HRESULT(STDMETHODCALLTYPE *GetFilePathFromKey)(T * This, const void *key, UINT32 size, WCHAR *path,                \
                                                   UINT32 path_size);                                                  \
    HRESULT(STDMETHODCALLTYPE *GetLastWriteTimeFromKey)(T * This, const void *key, UINT32 size, FILETIME *time);
MIME_COM_INTERFACE(IDWriteLocalFontFileLoader)

#define IDWriteFontFileEnumerator_METHODS(T)                                                                           \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *MoveNext)(T * This, BOOL *has_current);                                                 \
    HRESULT(STDMETHODCALLTYPE *GetCurrentFontFile)(T * This, IDWriteFontFile **file);
MIME_COM_INTERFACE(IDWriteFontFileEnumerator)

#define IDWriteFontCollectionLoader_METHODS(T)                                                                         \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateEnumeratorFromKey)(T * This, IDWriteFactory * factory, const void *key,           \
                                                        UINT32 size, IDWriteFontFileEnumerator **enumerator);
MIME_COM_INTERFACE(IDWriteFontCollectionLoader)

/* ------------------------------------------------------------------ files, faces, fonts */

#define IDWriteFontFile_METHODS(T)                                                                                     \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetReferenceKey)(T * This, const void **key, UINT32 *size);                             \
    HRESULT(STDMETHODCALLTYPE *GetLoader)(T * This, IDWriteFontFileLoader **loader);                                   \
    HRESULT(STDMETHODCALLTYPE *Analyze)(T * This, BOOL *supported, DWRITE_FONT_FILE_TYPE *file_type,                   \
                                        DWRITE_FONT_FACE_TYPE *face_type, UINT32 *faces);
MIME_COM_INTERFACE(IDWriteFontFile)

#define IDWriteRenderingParams_METHODS(T)                                                                              \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    FLOAT(STDMETHODCALLTYPE *GetGamma)(T * This);                                                                      \
    FLOAT(STDMETHODCALLTYPE *GetEnhancedContrast)(T * This);                                                           \
    FLOAT(STDMETHODCALLTYPE *GetClearTypeLevel)(T * This);                                                             \
    DWRITE_PIXEL_GEOMETRY(STDMETHODCALLTYPE *GetPixelGeometry)(T * This);                                              \
    DWRITE_RENDERING_MODE(STDMETHODCALLTYPE *GetRenderingMode)(T * This);
MIME_COM_INTERFACE(IDWriteRenderingParams)

#define IDWriteRenderingParams1_METHODS(T)                                                                             \
    IDWriteRenderingParams_METHODS(T)                                                                                  \
    FLOAT(STDMETHODCALLTYPE *GetGrayscaleEnhancedContrast)(T * This);
MIME_COM_INTERFACE(IDWriteRenderingParams1)

#define IDWriteRenderingParams2_METHODS(T)                                                                             \
    IDWriteRenderingParams1_METHODS(T)                                                                                 \
    DWRITE_GRID_FIT_MODE(STDMETHODCALLTYPE *GetGridFitMode)(T * This);
MIME_COM_INTERFACE(IDWriteRenderingParams2)

#define IDWriteGeometrySink_METHODS(T)                                                                                 \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    void(STDMETHODCALLTYPE *SetFillMode)(T * This, D2D1_FILL_MODE mode);                                               \
    void(STDMETHODCALLTYPE *SetSegmentFlags)(T * This, D2D1_PATH_SEGMENT flags);                                       \
    void(STDMETHODCALLTYPE *BeginFigure)(T * This, D2D1_POINT_2F start, D2D1_FIGURE_BEGIN begin);                      \
    void(STDMETHODCALLTYPE *AddLines)(T * This, const D2D1_POINT_2F *points, UINT32 count);                            \
    void(STDMETHODCALLTYPE *AddBeziers)(T * This, const D2D1_BEZIER_SEGMENT *beziers, UINT32 count);                   \
    void(STDMETHODCALLTYPE *EndFigure)(T * This, D2D1_FIGURE_END end);                                                 \
    HRESULT(STDMETHODCALLTYPE *Close)(T * This);
MIME_COM_INTERFACE(IDWriteGeometrySink)

#define IDWriteFontFace_METHODS(T)                                                                                     \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    DWRITE_FONT_FACE_TYPE(STDMETHODCALLTYPE *GetType)(T * This);                                                       \
    HRESULT(STDMETHODCALLTYPE *GetFiles)(T * This, UINT32 * count, IDWriteFontFile * *files);                          \
    UINT32(STDMETHODCALLTYPE *GetIndex)(T * This);                                                                     \
    DWRITE_FONT_SIMULATIONS(STDMETHODCALLTYPE *GetSimulations)(T * This);                                              \
    BOOL(STDMETHODCALLTYPE *IsSymbolFont)(T * This);                                                                   \
    void(STDMETHODCALLTYPE *GetMetrics)(T * This, DWRITE_FONT_METRICS * metrics);                                      \
    UINT16(STDMETHODCALLTYPE *GetGlyphCount)(T * This);                                                                \
    HRESULT(STDMETHODCALLTYPE *GetDesignGlyphMetrics)(T * This, const UINT16 *glyphs, UINT32 count,                    \
                                                      DWRITE_GLYPH_METRICS *metrics, BOOL sideways);                   \
    HRESULT(STDMETHODCALLTYPE *GetGlyphIndices)(T * This, const UINT32 *code_points, UINT32 count, UINT16 *glyphs);    \
    HRESULT(STDMETHODCALLTYPE *TryGetFontTable)(T * This, UINT32 tag, const void **data, UINT32 *size,                 \
                                                void **context, BOOL *exists);                                         \
    void(STDMETHODCALLTYPE *ReleaseFontTable)(T * This, void *context);                                                \
    HRESULT(STDMETHODCALLTYPE *GetGlyphRunOutline)(T * This, FLOAT em_size, const UINT16 *glyphs,                      \
                                                   const FLOAT *advances, const DWRITE_GLYPH_OFFSET *offsets,          \
                                                   UINT32 count, BOOL sideways, BOOL rtl, IDWriteGeometrySink *sink);  \
    HRESULT(STDMETHODCALLTYPE *GetRecommendedRenderingMode)(T * This, FLOAT em_size, FLOAT pixels_per_dip,             \
                                                            DWRITE_MEASURING_MODE measuring,                           \
                                                            IDWriteRenderingParams *params,                            \
                                                            DWRITE_RENDERING_MODE *mode);                              \
    HRESULT(STDMETHODCALLTYPE *GetGdiCompatibleMetrics)(T * This, FLOAT em_size, FLOAT pixels_per_dip,                 \
                                                        const DWRITE_MATRIX *transform,                                \
                                                        DWRITE_FONT_METRICS *metrics);                                 \
    HRESULT(STDMETHODCALLTYPE *GetGdiCompatibleGlyphMetrics)(T * This, FLOAT em_size, FLOAT pixels_per_dip,            \
                                                             const DWRITE_MATRIX *transform, BOOL natural,             \
                                                             const UINT16 *glyphs, UINT32 count,                       \
                                                             DWRITE_GLYPH_METRICS *metrics, BOOL sideways);
MIME_COM_INTERFACE(IDWriteFontFace)

#define IDWriteFontFace1_METHODS(T)                                                                                    \
    IDWriteFontFace_METHODS(T)                                                                                         \
    void(STDMETHODCALLTYPE *GetMetrics1)(T * This, DWRITE_FONT_METRICS1 * metrics);                                    \
    HRESULT(STDMETHODCALLTYPE *GetGdiCompatibleMetrics1)(T * This, FLOAT em_size, FLOAT pixels_per_dip,                \
                                                         const DWRITE_MATRIX *transform,                               \
                                                         DWRITE_FONT_METRICS1 *metrics);                               \
    void(STDMETHODCALLTYPE *GetCaretMetrics)(T * This, DWRITE_CARET_METRICS * metrics);                                \
    HRESULT(STDMETHODCALLTYPE *GetUnicodeRanges)(T * This, UINT32 max_count, DWRITE_UNICODE_RANGE *ranges,            \
                                                 UINT32 *actual);                                                      \
    BOOL(STDMETHODCALLTYPE *IsMonospacedFont)(T * This);                                                               \
    HRESULT(STDMETHODCALLTYPE *GetDesignGlyphAdvances)(T * This, UINT32 count, const UINT16 *glyphs,                   \
                                                       INT32 *advances, BOOL sideways);                                \
    HRESULT(STDMETHODCALLTYPE *GetGdiCompatibleGlyphAdvances)(T * This, FLOAT em_size, FLOAT pixels_per_dip,           \
                                                              const DWRITE_MATRIX *transform, BOOL natural,            \
                                                              BOOL sideways, UINT32 count, const UINT16 *glyphs,       \
                                                              INT32 *advances);                                        \
    HRESULT(STDMETHODCALLTYPE *GetKerningPairAdjustments)(T * This, UINT32 count, const UINT16 *glyphs,                \
                                                          INT32 *adjustments);                                         \
    BOOL(STDMETHODCALLTYPE *HasKerningPairs)(T * This);                                                                \
    HRESULT(STDMETHODCALLTYPE *GetRecommendedRenderingMode1)(T * This, FLOAT em_size, FLOAT dpi_x, FLOAT dpi_y,        \
                                                             const DWRITE_MATRIX *transform, BOOL sideways,            \
                                                             DWRITE_OUTLINE_THRESHOLD threshold,                       \
                                                             DWRITE_MEASURING_MODE measuring,                          \
                                                             DWRITE_RENDERING_MODE *mode);                             \
    HRESULT(STDMETHODCALLTYPE *GetVerticalGlyphVariants)(T * This, UINT32 count, const UINT16 *nominal,                \
                                                         UINT16 *vertical);                                            \
    BOOL(STDMETHODCALLTYPE *HasVerticalGlyphVariants)(T * This);
MIME_COM_INTERFACE(IDWriteFontFace1)

#define IDWriteFontFace2_METHODS(T)                                                                                    \
    IDWriteFontFace1_METHODS(T)                                                                                        \
    BOOL(STDMETHODCALLTYPE *IsColorFont)(T * This);                                                                    \
    UINT32(STDMETHODCALLTYPE *GetColorPaletteCount)(T * This);                                                         \
    UINT32(STDMETHODCALLTYPE *GetPaletteEntryCount)(T * This);                                                         \
    HRESULT(STDMETHODCALLTYPE *GetPaletteEntries)(T * This, UINT32 palette, UINT32 first, UINT32 count,                \
                                                  DWRITE_COLOR_F *entries);                                            \
    HRESULT(STDMETHODCALLTYPE *GetRecommendedRenderingMode2)(T * This, FLOAT em_size, FLOAT dpi_x, FLOAT dpi_y,        \
                                                             const DWRITE_MATRIX *transform, BOOL sideways,            \
                                                             DWRITE_OUTLINE_THRESHOLD threshold,                       \
                                                             DWRITE_MEASURING_MODE measuring,                          \
                                                             IDWriteRenderingParams *params,                           \
                                                             DWRITE_RENDERING_MODE *mode,                              \
                                                             DWRITE_GRID_FIT_MODE *grid_fit);
MIME_COM_INTERFACE(IDWriteFontFace2)

#define IDWriteFont_METHODS(T)                                                                                         \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetFontFamily)(T * This, IDWriteFontFamily * *family);                                  \
    DWRITE_FONT_WEIGHT(STDMETHODCALLTYPE *GetWeight)(T * This);                                                        \
    DWRITE_FONT_STRETCH(STDMETHODCALLTYPE *GetStretch)(T * This);                                                      \
    DWRITE_FONT_STYLE(STDMETHODCALLTYPE *GetStyle)(T * This);                                                          \
    BOOL(STDMETHODCALLTYPE *IsSymbolFont)(T * This);                                                                   \
    HRESULT(STDMETHODCALLTYPE *GetFaceNames)(T * This, IDWriteLocalizedStrings * *names);                              \
    HRESULT(STDMETHODCALLTYPE *GetInformationalStrings)(T * This, DWRITE_INFORMATIONAL_STRING_ID id,                   \
                                                        IDWriteLocalizedStrings **strings, BOOL *exists);              \
    DWRITE_FONT_SIMULATIONS(STDMETHODCALLTYPE *GetSimulations)(T * This);                                              \
    void(STDMETHODCALLTYPE *GetMetrics)(T * This, DWRITE_FONT_METRICS * metrics);                                      \
    HRESULT(STDMETHODCALLTYPE *HasCharacter)(T * This, UINT32 code_point, BOOL *exists);                               \
    HRESULT(STDMETHODCALLTYPE *CreateFontFace)(T * This, IDWriteFontFace * *face);
MIME_COM_INTERFACE(IDWriteFont)

#define IDWriteFont1_METHODS(T)                                                                                        \
    IDWriteFont_METHODS(T)                                                                                             \
    void(STDMETHODCALLTYPE *GetMetrics1)(T * This, DWRITE_FONT_METRICS1 * metrics);                                    \
    void(STDMETHODCALLTYPE *GetPanose)(T * This, DWRITE_PANOSE * panose);                                              \
    HRESULT(STDMETHODCALLTYPE *GetUnicodeRanges)(T * This, UINT32 max_count, DWRITE_UNICODE_RANGE *ranges,            \
                                                 UINT32 *actual);                                                      \
    BOOL(STDMETHODCALLTYPE *IsMonospacedFont)(T * This);
MIME_COM_INTERFACE(IDWriteFont1)

#define IDWriteFontList_METHODS(T)                                                                                     \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetFontCollection)(T * This, IDWriteFontCollection * *collection);                      \
    UINT32(STDMETHODCALLTYPE *GetFontCount)(T * This);                                                                 \
    HRESULT(STDMETHODCALLTYPE *GetFont)(T * This, UINT32 index, IDWriteFont **font);
MIME_COM_INTERFACE(IDWriteFontList)

#define IDWriteFontFamily_METHODS(T)                                                                                   \
    IDWriteFontList_METHODS(T)                                                                                         \
    HRESULT(STDMETHODCALLTYPE *GetFamilyNames)(T * This, IDWriteLocalizedStrings * *names);                            \
    HRESULT(STDMETHODCALLTYPE *GetFirstMatchingFont)(T * This, DWRITE_FONT_WEIGHT weight,                              \
                                                     DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style,             \
                                                     IDWriteFont **font);                                              \
    HRESULT(STDMETHODCALLTYPE *GetMatchingFonts)(T * This, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch,     \
                                                 DWRITE_FONT_STYLE style, IDWriteFontList **fonts);
MIME_COM_INTERFACE(IDWriteFontFamily)

#define IDWriteFontCollection_METHODS(T)                                                                               \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    UINT32(STDMETHODCALLTYPE *GetFontFamilyCount)(T * This);                                                           \
    HRESULT(STDMETHODCALLTYPE *GetFontFamily)(T * This, UINT32 index, IDWriteFontFamily **family);                     \
    HRESULT(STDMETHODCALLTYPE *FindFamilyName)(T * This, const WCHAR *name, UINT32 *index, BOOL *exists);              \
    HRESULT(STDMETHODCALLTYPE *GetFontFromFontFace)(T * This, IDWriteFontFace * face, IDWriteFont * *font);
MIME_COM_INTERFACE(IDWriteFontCollection)

typedef struct IDWriteFontSet1 IDWriteFontSet1;
typedef struct IDWriteFontList2 IDWriteFontList2;
typedef struct IDWriteFont3 IDWriteFont3;
typedef struct IDWriteFontFamily1 IDWriteFontFamily1;
typedef struct IDWriteFontFamily2 IDWriteFontFamily2;
typedef struct IDWriteFontCollection2 IDWriteFontCollection2;
typedef enum DWRITE_LOCALITY { DWRITE_LOCALITY_REMOTE = 0, DWRITE_LOCALITY_PARTIAL = 1, DWRITE_LOCALITY_LOCAL = 2 } DWRITE_LOCALITY;
typedef enum DWRITE_FONT_FAMILY_MODEL {
    DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC = 0,
    DWRITE_FONT_FAMILY_MODEL_WEIGHT_STRETCH_STYLE = 1
} DWRITE_FONT_FAMILY_MODEL;

#define IDWriteFontFamily1_METHODS(T)                                                                                  \
    IDWriteFontFamily_METHODS(T)                                                                                       \
    DWRITE_LOCALITY(STDMETHODCALLTYPE *GetFontLocality)(T * This, UINT32 index);                                       \
    HRESULT(STDMETHODCALLTYPE *GetFont1)(T * This, UINT32 index, IDWriteFont3 * *font);                                \
    HRESULT(STDMETHODCALLTYPE *GetFontFaceReference)(T * This, UINT32 index, IDWriteFontFaceReference **reference);
MIME_COM_INTERFACE(IDWriteFontFamily1)

#define IDWriteFontFamily2_METHODS(T)                                                                                  \
    IDWriteFontFamily1_METHODS(T)                                                                                      \
    HRESULT(STDMETHODCALLTYPE *GetMatchingFonts2)(T * This, const DWRITE_FONT_AXIS_VALUE *axis_values, UINT32 count,   \
                                                  IDWriteFontList2 **fonts);                                           \
    HRESULT(STDMETHODCALLTYPE *GetFontSet)(T * This, IDWriteFontSet1 * *set);
MIME_COM_INTERFACE(IDWriteFontFamily2)

#define IDWriteFontCollection1_METHODS(T)                                                                              \
    IDWriteFontCollection_METHODS(T)                                                                                   \
    HRESULT(STDMETHODCALLTYPE *GetFontSet)(T * This, IDWriteFontSet * *set);                                           \
    HRESULT(STDMETHODCALLTYPE *GetFontFamily1)(T * This, UINT32 index, IDWriteFontFamily1 * *family);
MIME_COM_INTERFACE(IDWriteFontCollection1)

#define IDWriteFontCollection2_METHODS(T)                                                                              \
    IDWriteFontCollection1_METHODS(T)                                                                                  \
    HRESULT(STDMETHODCALLTYPE *GetFontFamily2)(T * This, UINT32 index, IDWriteFontFamily2 * *family);                  \
    HRESULT(STDMETHODCALLTYPE *GetMatchingFonts)(T * This, const WCHAR *family_name,                                   \
                                                 const DWRITE_FONT_AXIS_VALUE *axis_values, UINT32 count,              \
                                                 IDWriteFontList2 **fonts);                                            \
    DWRITE_FONT_FAMILY_MODEL(STDMETHODCALLTYPE *GetFontFamilyModel)(T * This);                                         \
    HRESULT(STDMETHODCALLTYPE *GetFontSet2)(T * This, IDWriteFontSet1 * *set);
MIME_COM_INTERFACE(IDWriteFontCollection2)

/* ------------------------------------------------------------------ gdi, analysis, fallback */

typedef struct IDWriteBitmapRenderTargetVtbl IDWriteBitmapRenderTargetVtbl;

#define IDWriteGdiInterop_METHODS(T)                                                                                   \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *CreateFontFromLOGFONT)(T * This, const LOGFONTW *log_font, IDWriteFont **font);         \
    HRESULT(STDMETHODCALLTYPE *ConvertFontToLOGFONT)(T * This, IDWriteFont * font, LOGFONTW * log_font,                \
                                                     BOOL *is_system_font);                                            \
    HRESULT(STDMETHODCALLTYPE *ConvertFontFaceToLOGFONT)(T * This, IDWriteFontFace * face, LOGFONTW * log_font);       \
    HRESULT(STDMETHODCALLTYPE *CreateFontFaceFromHdc)(T * This, HDC dc, IDWriteFontFace * *face);                      \
    HRESULT(STDMETHODCALLTYPE *CreateBitmapRenderTarget)(T * This, HDC dc, UINT32 width, UINT32 height,               \
                                                         IDWriteBitmapRenderTarget **target);
MIME_COM_INTERFACE(IDWriteGdiInterop)

#define IDWriteGlyphRunAnalysis_METHODS(T)                                                                             \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetAlphaTextureBounds)(T * This, DWRITE_TEXTURE_TYPE type, RECT * bounds);              \
    HRESULT(STDMETHODCALLTYPE *CreateAlphaTexture)(T * This, DWRITE_TEXTURE_TYPE type, const RECT *bounds,             \
                                                   BYTE *alpha, UINT32 size);                                          \
    HRESULT(STDMETHODCALLTYPE *GetAlphaBlendParams)(T * This, IDWriteRenderingParams * params, FLOAT * gamma,          \
                                                    FLOAT * contrast, FLOAT * cleartype_level);
MIME_COM_INTERFACE(IDWriteGlyphRunAnalysis)

#define IDWriteTextAnalysisSource_METHODS(T)                                                                           \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetTextAtPosition)(T * This, UINT32 position, const WCHAR **text, UINT32 *length);      \
    HRESULT(STDMETHODCALLTYPE *GetTextBeforePosition)(T * This, UINT32 position, const WCHAR **text,                   \
                                                      UINT32 *length);                                                 \
    DWRITE_READING_DIRECTION(STDMETHODCALLTYPE *GetParagraphReadingDirection)(T * This);                               \
    HRESULT(STDMETHODCALLTYPE *GetLocaleName)(T * This, UINT32 position, UINT32 *length, const WCHAR **locale);        \
    HRESULT(STDMETHODCALLTYPE *GetNumberSubstitution)(T * This, UINT32 position, UINT32 *length,                       \
                                                      IDWriteNumberSubstitution **substitution);
MIME_COM_INTERFACE(IDWriteTextAnalysisSource)

#define IDWriteFontFallback_METHODS(T)                                                                                 \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *MapCharacters)(T * This, IDWriteTextAnalysisSource * source, UINT32 position,           \
                                              UINT32 length, IDWriteFontCollection *base_collection,                   \
                                              const WCHAR *base_family, DWRITE_FONT_WEIGHT weight,                     \
                                              DWRITE_FONT_STYLE style, DWRITE_FONT_STRETCH stretch,                    \
                                              UINT32 *mapped_length, IDWriteFont **mapped_font, FLOAT *scale);
MIME_COM_INTERFACE(IDWriteFontFallback)

/* ------------------------------------------------------------------ text layout and analysis */

#define IDWritePixelSnapping_METHODS(T)                                                                                \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *IsPixelSnappingDisabled)(T * This, void *client_drawingcontext, BOOL *disabled);        \
    HRESULT(STDMETHODCALLTYPE *GetCurrentTransform)(T * This, void *client_drawingcontext, DWRITE_MATRIX *transform);  \
    HRESULT(STDMETHODCALLTYPE *GetPixelsPerDip)(T * This, void *client_drawingcontext, FLOAT *pixels_per_dip);
MIME_COM_INTERFACE(IDWritePixelSnapping)

#define IDWriteTextRenderer_METHODS(T)                                                                                 \
    IDWritePixelSnapping_METHODS(T) HRESULT(STDMETHODCALLTYPE *DrawGlyphRun)(                                          \
        T * This, void *client_drawingcontext, FLOAT baselineOriginX, FLOAT baselineOriginY,                           \
        DWRITE_MEASURING_MODE mode, const DWRITE_GLYPH_RUN *glyph_run, const DWRITE_GLYPH_RUN_DESCRIPTION *run_descr,  \
        IUnknown *drawing_effect);                                                                                     \
    HRESULT(STDMETHODCALLTYPE *DrawUnderline)(T * This, void *client_drawingcontext, FLOAT baselineOriginX,            \
                                              FLOAT baselineOriginY, const DWRITE_UNDERLINE *underline,                \
                                              IUnknown *drawing_effect);                                               \
    HRESULT(STDMETHODCALLTYPE *DrawStrikethrough)(T * This, void *client_drawingcontext, FLOAT baselineOriginX,        \
                                                  FLOAT baselineOriginY, const DWRITE_STRIKETHROUGH *strikethrough,    \
                                                  IUnknown *drawing_effect);                                           \
    HRESULT(STDMETHODCALLTYPE *DrawInlineObject)(T * This, void *client_drawingcontext, FLOAT originX, FLOAT originY,  \
                                                 IDWriteInlineObject *object, BOOL is_sideways, BOOL is_rtl,           \
                                                 IUnknown *drawing_effect);
MIME_COM_INTERFACE(IDWriteTextRenderer)

#define IDWriteInlineObject_METHODS(T)                                                                                 \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Draw)(T * This, void *client_drawingontext, IDWriteTextRenderer *renderer,              \
                                     FLOAT originX, FLOAT originY, BOOL is_sideways, BOOL is_rtl,                      \
                                     IUnknown *drawing_effect);                                                        \
    HRESULT(STDMETHODCALLTYPE *GetMetrics)(T * This, DWRITE_INLINE_OBJECT_METRICS * metrics);                          \
    HRESULT(STDMETHODCALLTYPE *GetOverhangMetrics)(T * This, DWRITE_OVERHANG_METRICS * overhangs);                     \
    HRESULT(STDMETHODCALLTYPE *GetBreakConditions)(T * This, DWRITE_BREAK_CONDITION * condition_before,                \
                                                   DWRITE_BREAK_CONDITION * condition_after);
MIME_COM_INTERFACE(IDWriteInlineObject)

#define IDWriteTextFormat_METHODS(T)                                                                                   \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetTextAlignment)(T * This, DWRITE_TEXT_ALIGNMENT alignment);                           \
    HRESULT(STDMETHODCALLTYPE *SetParagraphAlignment)(T * This, DWRITE_PARAGRAPH_ALIGNMENT alignment);                 \
    HRESULT(STDMETHODCALLTYPE *SetWordWrapping)(T * This, DWRITE_WORD_WRAPPING wrapping);                              \
    HRESULT(STDMETHODCALLTYPE *SetReadingDirection)(T * This, DWRITE_READING_DIRECTION direction);                     \
    HRESULT(STDMETHODCALLTYPE *SetFlowDirection)(T * This, DWRITE_FLOW_DIRECTION direction);                           \
    HRESULT(STDMETHODCALLTYPE *SetIncrementalTabStop)(T * This, FLOAT tabstop);                                        \
    HRESULT(STDMETHODCALLTYPE *SetTrimming)(T * This, const DWRITE_TRIMMING *trimming,                                 \
                                            IDWriteInlineObject *trimming_sign);                                       \
    HRESULT(STDMETHODCALLTYPE *SetLineSpacing)(T * This, DWRITE_LINE_SPACING_METHOD spacing, FLOAT line_spacing,       \
                                               FLOAT baseline);                                                        \
    DWRITE_TEXT_ALIGNMENT(STDMETHODCALLTYPE *GetTextAlignment)(T * This);                                              \
    DWRITE_PARAGRAPH_ALIGNMENT(STDMETHODCALLTYPE *GetParagraphAlignment)(T * This);                                    \
    DWRITE_WORD_WRAPPING(STDMETHODCALLTYPE *GetWordWrapping)(T * This);                                                \
    DWRITE_READING_DIRECTION(STDMETHODCALLTYPE *GetReadingDirection)(T * This);                                        \
    DWRITE_FLOW_DIRECTION(STDMETHODCALLTYPE *GetFlowDirection)(T * This);                                              \
    FLOAT(STDMETHODCALLTYPE *GetIncrementalTabStop)(T * This);                                                         \
    HRESULT(STDMETHODCALLTYPE *GetTrimming)(T * This, DWRITE_TRIMMING * options,                                       \
                                            IDWriteInlineObject * *trimming_sign);                                     \
    HRESULT(STDMETHODCALLTYPE *GetLineSpacing)(T * This, DWRITE_LINE_SPACING_METHOD * method, FLOAT * spacing,         \
                                               FLOAT * baseline);                                                      \
    HRESULT(STDMETHODCALLTYPE *GetFontCollection)(T * This, IDWriteFontCollection * *collection);                      \
    UINT32(STDMETHODCALLTYPE *GetFontFamilyNameLength)(T * This);                                                      \
    HRESULT(STDMETHODCALLTYPE *GetFontFamilyName)(T * This, WCHAR * name, UINT32 size);                                \
    DWRITE_FONT_WEIGHT(STDMETHODCALLTYPE *GetFontWeight)(T * This);                                                    \
    DWRITE_FONT_STYLE(STDMETHODCALLTYPE *GetFontStyle)(T * This);                                                      \
    DWRITE_FONT_STRETCH(STDMETHODCALLTYPE *GetFontStretch)(T * This);                                                  \
    FLOAT(STDMETHODCALLTYPE *GetFontSize)(T * This);                                                                   \
    UINT32(STDMETHODCALLTYPE *GetLocaleNameLength)(T * This);                                                          \
    HRESULT(STDMETHODCALLTYPE *GetLocaleName)(T * This, WCHAR * name, UINT32 size);
MIME_COM_INTERFACE(IDWriteTextFormat)

#define IDWriteTypography_METHODS(T)                                                                                   \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *AddFontFeature)(T * This, DWRITE_FONT_FEATURE feature);                                 \
    UINT32(STDMETHODCALLTYPE *GetFontFeatureCount)(T * This);                                                          \
    HRESULT(STDMETHODCALLTYPE *GetFontFeature)(T * This, UINT32 index, DWRITE_FONT_FEATURE * feature);
MIME_COM_INTERFACE(IDWriteTypography)

#define IDWriteTextLayout_METHODS(T)                                                                                   \
    IDWriteTextFormat_METHODS(T) HRESULT(STDMETHODCALLTYPE *SetMaxWidth)(T * This, FLOAT maxWidth);                    \
    HRESULT(STDMETHODCALLTYPE *SetMaxHeight)(T * This, FLOAT maxHeight);                                               \
    HRESULT(STDMETHODCALLTYPE *SetFontCollection)(T * This, IDWriteFontCollection * collection,                        \
                                                  DWRITE_TEXT_RANGE range);                                            \
    HRESULT(STDMETHODCALLTYPE *SetFontFamilyName)(T * This, const WCHAR *name, DWRITE_TEXT_RANGE range);               \
    HRESULT(STDMETHODCALLTYPE *SetFontWeight)(T * This, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_RANGE range);           \
    HRESULT(STDMETHODCALLTYPE *SetFontStyle)(T * This, DWRITE_FONT_STYLE style, DWRITE_TEXT_RANGE range);              \
    HRESULT(STDMETHODCALLTYPE *SetFontStretch)(T * This, DWRITE_FONT_STRETCH stretch, DWRITE_TEXT_RANGE range);        \
    HRESULT(STDMETHODCALLTYPE *SetFontSize)(T * This, FLOAT size, DWRITE_TEXT_RANGE range);                            \
    HRESULT(STDMETHODCALLTYPE *SetUnderline)(T * This, BOOL underline, DWRITE_TEXT_RANGE range);                       \
    HRESULT(STDMETHODCALLTYPE *SetStrikethrough)(T * This, BOOL strikethrough, DWRITE_TEXT_RANGE range);               \
    HRESULT(STDMETHODCALLTYPE *SetDrawingEffect)(T * This, IUnknown * effect, DWRITE_TEXT_RANGE range);                \
    HRESULT(STDMETHODCALLTYPE *SetInlineObject)(T * This, IDWriteInlineObject * object, DWRITE_TEXT_RANGE range);      \
    HRESULT(STDMETHODCALLTYPE *SetTypography)(T * This, IDWriteTypography * typography, DWRITE_TEXT_RANGE range);      \
    HRESULT(STDMETHODCALLTYPE *SetLocaleName)(T * This, const WCHAR *locale, DWRITE_TEXT_RANGE range);                 \
    FLOAT(STDMETHODCALLTYPE *GetMaxWidth)(T * This);                                                                   \
    FLOAT(STDMETHODCALLTYPE *GetMaxHeight)(T * This);                                                                  \
    HRESULT(STDMETHODCALLTYPE *IDWriteTextLayout_GetFontCollection)(                                                   \
        T * This, UINT32 pos, IDWriteFontCollection * *collection, DWRITE_TEXT_RANGE * range);                         \
    HRESULT(STDMETHODCALLTYPE *IDWriteTextLayout_GetFontFamilyNameLength)(T * This, UINT32 pos, UINT32 * len,          \
                                                                          DWRITE_TEXT_RANGE * range);                  \
    HRESULT(STDMETHODCALLTYPE *IDWriteTextLayout_GetFontFamilyName)(T * This, UINT32 position, WCHAR * name,           \
                                                                    UINT32 name_size, DWRITE_TEXT_RANGE * range);      \
    HRESULT(STDMETHODCALLTYPE *IDWriteTextLayout_GetFontWeight)(                                                       \
        T * This, UINT32 position, DWRITE_FONT_WEIGHT * weight, DWRITE_TEXT_RANGE * range);                            \
    HRESULT(STDMETHODCALLTYPE *IDWriteTextLayout_GetFontStyle)(T * This, UINT32 currentPosition,                       \
                                                               DWRITE_FONT_STYLE * style, DWRITE_TEXT_RANGE * range);  \
    HRESULT(STDMETHODCALLTYPE *IDWriteTextLayout_GetFontStretch)(                                                      \
        T * This, UINT32 position, DWRITE_FONT_STRETCH * stretch, DWRITE_TEXT_RANGE * range);                          \
    HRESULT(STDMETHODCALLTYPE *IDWriteTextLayout_GetFontSize)(T * This, UINT32 position, FLOAT * size,                 \
                                                              DWRITE_TEXT_RANGE * range);                              \
    HRESULT(STDMETHODCALLTYPE *GetUnderline)(T * This, UINT32 position, BOOL * has_underline,                          \
                                             DWRITE_TEXT_RANGE * range);                                               \
    HRESULT(STDMETHODCALLTYPE *GetStrikethrough)(T * This, UINT32 position, BOOL * has_strikethrough,                  \
                                                 DWRITE_TEXT_RANGE * range);                                           \
    HRESULT(STDMETHODCALLTYPE *GetDrawingEffect)(T * This, UINT32 position, IUnknown * *effect,                        \
                                                 DWRITE_TEXT_RANGE * range);                                           \
    HRESULT(STDMETHODCALLTYPE *GetInlineObject)(T * This, UINT32 position, IDWriteInlineObject * *object,              \
                                                DWRITE_TEXT_RANGE * range);                                            \
    HRESULT(STDMETHODCALLTYPE *GetTypography)(T * This, UINT32 position, IDWriteTypography * *typography,              \
                                              DWRITE_TEXT_RANGE * range);                                              \
    HRESULT(STDMETHODCALLTYPE *IDWriteTextLayout_GetLocaleNameLength)(T * This, UINT32 position, UINT32 * length,      \
                                                                      DWRITE_TEXT_RANGE * range);                      \
    HRESULT(STDMETHODCALLTYPE *IDWriteTextLayout_GetLocaleName)(T * This, UINT32 position, WCHAR * name,               \
                                                                UINT32 name_size, DWRITE_TEXT_RANGE * range);          \
    HRESULT(STDMETHODCALLTYPE *Draw)(T * This, void *context, IDWriteTextRenderer *renderer, FLOAT originX,            \
                                     FLOAT originY);                                                                   \
    HRESULT(STDMETHODCALLTYPE *GetLineMetrics)(T * This, DWRITE_LINE_METRICS * metrics, UINT32 max_count,              \
                                               UINT32 * actual_count);                                                 \
    HRESULT(STDMETHODCALLTYPE *GetMetrics)(T * This, DWRITE_TEXT_METRICS * metrics);                                   \
    HRESULT(STDMETHODCALLTYPE *GetOverhangMetrics)(T * This, DWRITE_OVERHANG_METRICS * overhangs);                     \
    HRESULT(STDMETHODCALLTYPE *GetClusterMetrics)(T * This, DWRITE_CLUSTER_METRICS * metrics, UINT32 max_count,        \
                                                  UINT32 * act_count);                                                 \
    HRESULT(STDMETHODCALLTYPE *DetermineMinWidth)(T * This, FLOAT * min_width);                                        \
    HRESULT(STDMETHODCALLTYPE *HitTestPoint)(T * This, FLOAT pointX, FLOAT pointY, BOOL * is_trailinghit,              \
                                             BOOL * is_inside, DWRITE_HIT_TEST_METRICS * metrics);                     \
    HRESULT(STDMETHODCALLTYPE *HitTestTextPosition)(T * This, UINT32 textPosition, BOOL is_trailinghit,                \
                                                    FLOAT * pointX, FLOAT * pointY,                                    \
                                                    DWRITE_HIT_TEST_METRICS * metrics);                                \
    HRESULT(STDMETHODCALLTYPE *HitTestTextRange)(T * This, UINT32 textPosition, UINT32 textLength, FLOAT originX,      \
                                                 FLOAT originY, DWRITE_HIT_TEST_METRICS * metrics,                     \
                                                 UINT32 max_metricscount, UINT32 * actual_metricscount);
MIME_COM_INTERFACE(IDWriteTextLayout)

#define IDWriteNumberSubstitution_METHODS(T) MIME_IUNKNOWN_METHODS(T)
MIME_COM_INTERFACE(IDWriteNumberSubstitution)

#define IDWriteTextAnalysisSink_METHODS(T)                                                                             \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *SetScriptAnalysis)(T * This, UINT32 position, UINT32 length,                            \
                                                  const DWRITE_SCRIPT_ANALYSIS *scriptanalysis);                       \
    HRESULT(STDMETHODCALLTYPE *SetLineBreakpoints)(T * This, UINT32 position, UINT32 length,                           \
                                                   const DWRITE_LINE_BREAKPOINT *breakpoints);                         \
    HRESULT(STDMETHODCALLTYPE *SetBidiLevel)(T * This, UINT32 position, UINT32 length, UINT8 explicitLevel,            \
                                             UINT8 resolvedLevel);                                                     \
    HRESULT(STDMETHODCALLTYPE *SetNumberSubstitution)(T * This, UINT32 position, UINT32 length,                        \
                                                      IDWriteNumberSubstitution * substitution);
MIME_COM_INTERFACE(IDWriteTextAnalysisSink)

#define IDWriteTextAnalyzer_METHODS(T)                                                                                 \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *AnalyzeScript)(T * This, IDWriteTextAnalysisSource * source, UINT32 position,           \
                                              UINT32 length, IDWriteTextAnalysisSink * sink);                          \
    HRESULT(STDMETHODCALLTYPE *AnalyzeBidi)(T * This, IDWriteTextAnalysisSource * source, UINT32 position,             \
                                            UINT32 length, IDWriteTextAnalysisSink * sink);                            \
    HRESULT(STDMETHODCALLTYPE *AnalyzeNumberSubstitution)(                                                             \
        T * This, IDWriteTextAnalysisSource * source, UINT32 position, UINT32 length, IDWriteTextAnalysisSink * sink); \
    HRESULT(STDMETHODCALLTYPE *AnalyzeLineBreakpoints)(T * This, IDWriteTextAnalysisSource * source, UINT32 position,  \
                                                       UINT32 length, IDWriteTextAnalysisSink * sink);                 \
    HRESULT(STDMETHODCALLTYPE *GetGlyphs)(                                                                             \
        T * This, const WCHAR *text, UINT32 length, IDWriteFontFace *font_face, BOOL is_sideways, BOOL is_rtl,         \
        const DWRITE_SCRIPT_ANALYSIS *analysis, const WCHAR *locale, IDWriteNumberSubstitution *substitution,          \
        const DWRITE_TYPOGRAPHIC_FEATURES **features, const UINT32 *feature_range_len, UINT32 feature_ranges,          \
        UINT32 max_glyph_count, UINT16 *clustermap, DWRITE_SHAPING_TEXT_PROPERTIES *text_props, UINT16 *glyph_indices, \
        DWRITE_SHAPING_GLYPH_PROPERTIES *glyph_props, UINT32 *actual_glyph_count);                                     \
    HRESULT(STDMETHODCALLTYPE *GetGlyphPlacements)(                                                                    \
        T * This, const WCHAR *text, const UINT16 *clustermap, DWRITE_SHAPING_TEXT_PROPERTIES *props, UINT32 text_len, \
        const UINT16 *glyph_indices, const DWRITE_SHAPING_GLYPH_PROPERTIES *glyph_props, UINT32 glyph_count,           \
        IDWriteFontFace *font_face, FLOAT fontEmSize, BOOL is_sideways, BOOL is_rtl,                                   \
        const DWRITE_SCRIPT_ANALYSIS *analysis, const WCHAR *locale, const DWRITE_TYPOGRAPHIC_FEATURES **features,     \
        const UINT32 *feature_range_len, UINT32 feature_ranges, FLOAT *glyph_advances,                                 \
        DWRITE_GLYPH_OFFSET *glyph_offsets);                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetGdiCompatibleGlyphPlacements)(                                                       \
        T * This, const WCHAR *text, const UINT16 *clustermap, DWRITE_SHAPING_TEXT_PROPERTIES *props, UINT32 text_len, \
        const UINT16 *glyph_indices, const DWRITE_SHAPING_GLYPH_PROPERTIES *glyph_props, UINT32 glyph_count,           \
        IDWriteFontFace *font_face, FLOAT fontEmSize, FLOAT pixels_per_dip, const DWRITE_MATRIX *transform,            \
        BOOL use_gdi_natural, BOOL is_sideways, BOOL is_rtl, const DWRITE_SCRIPT_ANALYSIS *analysis,                   \
        const WCHAR *locale, const DWRITE_TYPOGRAPHIC_FEATURES **features, const UINT32 *feature_range_lengths,        \
        UINT32 feature_ranges, FLOAT *glyph_advances, DWRITE_GLYPH_OFFSET *glyph_offsets);
MIME_COM_INTERFACE(IDWriteTextAnalyzer)

#define IDWriteTextAnalysisSource1_METHODS(T)                                                                          \
    IDWriteTextAnalysisSource_METHODS(T) HRESULT(STDMETHODCALLTYPE *GetVerticalGlyphOrientation)(                      \
        T * This, UINT32 pos, UINT32 * length, DWRITE_VERTICAL_GLYPH_ORIENTATION * orientation, UINT8 * bidi_level);
MIME_COM_INTERFACE(IDWriteTextAnalysisSource1)

#define IDWriteTextAnalysisSink1_METHODS(T)                                                                            \
    IDWriteTextAnalysisSink_METHODS(T) HRESULT(STDMETHODCALLTYPE *SetGlyphOrientation)(                                \
        T * This, UINT32 pos, UINT32 length, DWRITE_GLYPH_ORIENTATION_ANGLE angle, UINT8 adjusted_bidilevel,           \
        BOOL is_sideways, BOOL is_rtl);
MIME_COM_INTERFACE(IDWriteTextAnalysisSink1)

#define IDWriteTextAnalyzer1_METHODS(T)                                                                                \
    IDWriteTextAnalyzer_METHODS(T) HRESULT(STDMETHODCALLTYPE *ApplyCharacterSpacing)(                                  \
        T * This, FLOAT leading_spacing, FLOAT trailing_spacing, FLOAT min_advance_width, UINT32 len,                  \
        UINT32 glyph_count, const UINT16 *clustermap, const FLOAT *advances, const DWRITE_GLYPH_OFFSET *offsets,       \
        const DWRITE_SHAPING_GLYPH_PROPERTIES *props, FLOAT *modified_advances,                                        \
        DWRITE_GLYPH_OFFSET *modified_offsets);                                                                        \
    HRESULT(STDMETHODCALLTYPE *GetBaseline)(T * This, IDWriteFontFace * face, DWRITE_BASELINE baseline, BOOL vertical, \
                                            BOOL is_simulation_allowed, DWRITE_SCRIPT_ANALYSIS sa,                     \
                                            const WCHAR *localeName, INT32 *baseline_coord, BOOL *exists);             \
    HRESULT(STDMETHODCALLTYPE *AnalyzeVerticalGlyphOrientation)(                                                       \
        T * This, IDWriteTextAnalysisSource1 * source, UINT32 text_pos, UINT32 len, IDWriteTextAnalysisSink1 * sink);  \
    HRESULT(STDMETHODCALLTYPE *GetGlyphOrientationTransform)(T * This, DWRITE_GLYPH_ORIENTATION_ANGLE angle,           \
                                                             BOOL is_sideways, DWRITE_MATRIX * transform);             \
    HRESULT(STDMETHODCALLTYPE *GetScriptProperties)(T * This, DWRITE_SCRIPT_ANALYSIS sa,                               \
                                                    DWRITE_SCRIPT_PROPERTIES * props);                                 \
    HRESULT(STDMETHODCALLTYPE *GetTextComplexity)(T * This, const WCHAR *text, UINT32 len, IDWriteFontFace *face,      \
                                                  BOOL *is_simple, UINT32 *len_read, UINT16 *indices);                 \
    HRESULT(STDMETHODCALLTYPE *GetJustificationOpportunities)(                                                         \
        T * This, IDWriteFontFace * face, FLOAT font_em_size, DWRITE_SCRIPT_ANALYSIS sa, UINT32 length,                \
        UINT32 glyph_count, const WCHAR *text, const UINT16 *clustermap, const DWRITE_SHAPING_GLYPH_PROPERTIES *prop,  \
        DWRITE_JUSTIFICATION_OPPORTUNITY *jo);                                                                         \
    HRESULT(STDMETHODCALLTYPE *JustifyGlyphAdvances)(                                                                  \
        T * This, FLOAT width, UINT32 glyph_count, const DWRITE_JUSTIFICATION_OPPORTUNITY *jo, const FLOAT *advances,  \
        const DWRITE_GLYPH_OFFSET *offsets, FLOAT *justifiedadvances, DWRITE_GLYPH_OFFSET *justifiedoffsets);          \
    HRESULT(STDMETHODCALLTYPE *GetJustifiedGlyphs)(                                                                    \
        T * This, IDWriteFontFace * face, FLOAT font_em_size, DWRITE_SCRIPT_ANALYSIS sa, UINT32 length,                \
        UINT32 glyph_count, UINT32 max_glyphcount, const UINT16 *clustermap, const UINT16 *indices,                    \
        const FLOAT *advances, const FLOAT *justifiedadvances, const DWRITE_GLYPH_OFFSET *justifiedoffsets,            \
        const DWRITE_SHAPING_GLYPH_PROPERTIES *prop, UINT32 *actual_count, UINT16 *modified_clustermap,                \
        UINT16 *modified_indices, FLOAT *modified_advances, DWRITE_GLYPH_OFFSET *modified_offsets);
MIME_COM_INTERFACE(IDWriteTextAnalyzer1)

#define IDWriteTextLayout1_METHODS(T)                                                                                  \
    IDWriteTextLayout_METHODS(T)                                                                                       \
        HRESULT(STDMETHODCALLTYPE *SetPairKerning)(T * This, BOOL is_pairkerning_enabled, DWRITE_TEXT_RANGE range);    \
    HRESULT(STDMETHODCALLTYPE *GetPairKerning)(T * This, UINT32 position, BOOL * is_pairkerning_enabled,               \
                                               DWRITE_TEXT_RANGE * range);                                             \
    HRESULT(STDMETHODCALLTYPE *SetCharacterSpacing)(T * This, FLOAT leading_spacing, FLOAT trailing_spacing,           \
                                                    FLOAT minimum_advance_width, DWRITE_TEXT_RANGE range);             \
    HRESULT(STDMETHODCALLTYPE *GetCharacterSpacing)(T * This, UINT32 position, FLOAT * leading_spacing,                \
                                                    FLOAT * trailing_spacing, FLOAT * minimum_advance_width,           \
                                                    DWRITE_TEXT_RANGE * range);
MIME_COM_INTERFACE(IDWriteTextLayout1)

#define IDWriteTextRenderer1_METHODS(T)                                                                                \
    IDWriteTextRenderer_METHODS(T) HRESULT(STDMETHODCALLTYPE *IDWriteTextRenderer1_DrawGlyphRun)(                      \
        T * This, void *context, FLOAT originX, FLOAT originY, DWRITE_GLYPH_ORIENTATION_ANGLE angle,                   \
        DWRITE_MEASURING_MODE mode, const DWRITE_GLYPH_RUN *run, const DWRITE_GLYPH_RUN_DESCRIPTION *rundescr,         \
        IUnknown *effect);                                                                                             \
    HRESULT(STDMETHODCALLTYPE *IDWriteTextRenderer1_DrawUnderline)(                                                    \
        T * This, void *context, FLOAT originX, FLOAT originY, DWRITE_GLYPH_ORIENTATION_ANGLE angle,                   \
        const DWRITE_UNDERLINE *underline, IUnknown *effect);                                                          \
    HRESULT(STDMETHODCALLTYPE *IDWriteTextRenderer1_DrawStrikethrough)(                                                \
        T * This, void *context, FLOAT originX, FLOAT originY, DWRITE_GLYPH_ORIENTATION_ANGLE angle,                   \
        const DWRITE_STRIKETHROUGH *strikethrough, IUnknown *effect);                                                  \
    HRESULT(STDMETHODCALLTYPE *IDWriteTextRenderer1_DrawInlineObject)(                                                 \
        T * This, void *context, FLOAT originX, FLOAT originY, DWRITE_GLYPH_ORIENTATION_ANGLE angle,                   \
        IDWriteInlineObject *inlineObject, BOOL is_sideways, BOOL is_rtl, IUnknown *effect);
MIME_COM_INTERFACE(IDWriteTextRenderer1)

#define IDWriteTextFormat1_METHODS(T)                                                                                  \
    IDWriteTextFormat_METHODS(T) HRESULT(STDMETHODCALLTYPE *SetVerticalGlyphOrientation)(                              \
        T * This, DWRITE_VERTICAL_GLYPH_ORIENTATION orientation);                                                      \
    DWRITE_VERTICAL_GLYPH_ORIENTATION(STDMETHODCALLTYPE *GetVerticalGlyphOrientation)(T * This);                       \
    HRESULT(STDMETHODCALLTYPE *SetLastLineWrapping)(T * This, BOOL lastline_wrapping_enabled);                         \
    BOOL(STDMETHODCALLTYPE *GetLastLineWrapping)(T * This);                                                            \
    HRESULT(STDMETHODCALLTYPE *SetOpticalAlignment)(T * This, DWRITE_OPTICAL_ALIGNMENT alignment);                     \
    DWRITE_OPTICAL_ALIGNMENT(STDMETHODCALLTYPE *GetOpticalAlignment)(T * This);                                        \
    HRESULT(STDMETHODCALLTYPE *SetFontFallback)(T * This, IDWriteFontFallback * fallback);                             \
    HRESULT(STDMETHODCALLTYPE *GetFontFallback)(T * This, IDWriteFontFallback * *fallback);
MIME_COM_INTERFACE(IDWriteTextFormat1)

#define IDWriteTextLayout2_METHODS(T)                                                                                  \
    IDWriteTextLayout1_METHODS(T)                                                                                      \
        HRESULT(STDMETHODCALLTYPE *IDWriteTextLayout2_GetMetrics)(T * This, DWRITE_TEXT_METRICS1 * metrics);           \
    HRESULT(STDMETHODCALLTYPE *SetVerticalGlyphOrientation)(T * This, DWRITE_VERTICAL_GLYPH_ORIENTATION orientation);  \
    DWRITE_VERTICAL_GLYPH_ORIENTATION(STDMETHODCALLTYPE *GetVerticalGlyphOrientation)(T * This);                       \
    HRESULT(STDMETHODCALLTYPE *SetLastLineWrapping)(T * This, BOOL lastline_wrapping_enabled);                         \
    BOOL(STDMETHODCALLTYPE *GetLastLineWrapping)(T * This);                                                            \
    HRESULT(STDMETHODCALLTYPE *SetOpticalAlignment)(T * This, DWRITE_OPTICAL_ALIGNMENT alignment);                     \
    DWRITE_OPTICAL_ALIGNMENT(STDMETHODCALLTYPE *GetOpticalAlignment)(T * This);                                        \
    HRESULT(STDMETHODCALLTYPE *SetFontFallback)(T * This, IDWriteFontFallback * fallback);                             \
    HRESULT(STDMETHODCALLTYPE *GetFontFallback)(T * This, IDWriteFontFallback * *fallback);
MIME_COM_INTERFACE(IDWriteTextLayout2)

#define IDWriteTextAnalyzer2_METHODS(T)                                                                                \
    IDWriteTextAnalyzer1_METHODS(T) HRESULT(STDMETHODCALLTYPE *IDWriteTextAnalyzer2_GetGlyphOrientationTransform)(     \
        T * This, DWRITE_GLYPH_ORIENTATION_ANGLE angle, BOOL is_sideways, FLOAT originX, FLOAT originY,                \
        DWRITE_MATRIX * transform);                                                                                    \
    HRESULT(STDMETHODCALLTYPE *GetTypographicFeatures)(                                                                \
        T * This, IDWriteFontFace * fontface, DWRITE_SCRIPT_ANALYSIS analysis, const WCHAR *localeName,                \
        UINT32 max_tagcount, UINT32 *actual_tagcount, DWRITE_FONT_FEATURE_TAG *tags);                                  \
    HRESULT(STDMETHODCALLTYPE *CheckTypographicFeature)(                                                               \
        T * This, IDWriteFontFace * fontface, DWRITE_SCRIPT_ANALYSIS analysis, const WCHAR *localeName,                \
        DWRITE_FONT_FEATURE_TAG feature, UINT32 glyph_count, const UINT16 *indices, UINT8 *feature_applies);
MIME_COM_INTERFACE(IDWriteTextAnalyzer2)

#define IDWriteFontFallbackBuilder_METHODS(T)                                                                          \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *AddMapping)(T * This, const DWRITE_UNICODE_RANGE *ranges, UINT32 rangesCount,           \
                                           const WCHAR **targetFamilyNames, UINT32 targetFamilyNamesCount,             \
                                           IDWriteFontCollection *collection, const WCHAR *localeName,                 \
                                           const WCHAR *baseFamilyName, FLOAT scale);                                  \
    HRESULT(STDMETHODCALLTYPE *AddMappings)(T * This, IDWriteFontFallback * fallback);                                 \
    HRESULT(STDMETHODCALLTYPE *CreateFontFallback)(T * This, IDWriteFontFallback * *fallback);
MIME_COM_INTERFACE(IDWriteFontFallbackBuilder)

#define IDWriteTextFormat2_METHODS(T)                                                                                  \
    IDWriteTextFormat1_METHODS(T)                                                                                      \
        HRESULT(STDMETHODCALLTYPE *IDWriteTextFormat2_SetLineSpacing)(T * This, const DWRITE_LINE_SPACING *spacing);   \
    HRESULT(STDMETHODCALLTYPE *IDWriteTextFormat2_GetLineSpacing)(T * This, DWRITE_LINE_SPACING * spacing);
MIME_COM_INTERFACE(IDWriteTextFormat2)

#define IDWriteTextFormat3_METHODS(T)                                                                                  \
    IDWriteTextFormat2_METHODS(T) HRESULT(STDMETHODCALLTYPE *SetFontAxisValues)(                                       \
        T * This, const DWRITE_FONT_AXIS_VALUE *axis_values, UINT32 num_values);                                       \
    UINT32(STDMETHODCALLTYPE *GetFontAxisValueCount)(T * This);                                                        \
    HRESULT(STDMETHODCALLTYPE *GetFontAxisValues)(T * This, DWRITE_FONT_AXIS_VALUE * axis_values, UINT32 num_values);  \
    DWRITE_AUTOMATIC_FONT_AXES(STDMETHODCALLTYPE *GetAutomaticFontAxes)(T * This);                                     \
    HRESULT(STDMETHODCALLTYPE *SetAutomaticFontAxes)(T * This, DWRITE_AUTOMATIC_FONT_AXES axes);
MIME_COM_INTERFACE(IDWriteTextFormat3)

#define IDWriteTextLayout3_METHODS(T)                                                                                  \
    IDWriteTextLayout2_METHODS(T) HRESULT(STDMETHODCALLTYPE *InvalidateLayout)(T * This);                              \
    HRESULT(STDMETHODCALLTYPE *IDWriteTextLayout3_SetLineSpacing)(T * This, const DWRITE_LINE_SPACING *spacing);       \
    HRESULT(STDMETHODCALLTYPE *IDWriteTextLayout3_GetLineSpacing)(T * This, DWRITE_LINE_SPACING * spacing);            \
    HRESULT(STDMETHODCALLTYPE *IDWriteTextLayout3_GetLineMetrics)(T * This, DWRITE_LINE_METRICS1 * metrics,            \
                                                                  UINT32 max_count, UINT32 * count);
MIME_COM_INTERFACE(IDWriteTextLayout3)

#define IDWriteTextLayout4_METHODS(T)                                                                                  \
    IDWriteTextLayout3_METHODS(T) HRESULT(STDMETHODCALLTYPE *SetFontAxisValues)(                                       \
        T * This, const DWRITE_FONT_AXIS_VALUE *axis_values, UINT32 num_values, DWRITE_TEXT_RANGE range);              \
    UINT32(STDMETHODCALLTYPE *GetFontAxisValueCount)(T * This, UINT32 pos);                                            \
    HRESULT(STDMETHODCALLTYPE *GetFontAxisValues)(T * This, UINT32 pos, DWRITE_FONT_AXIS_VALUE * values,               \
                                                  UINT32 num_values, DWRITE_TEXT_RANGE * range);                       \
    DWRITE_AUTOMATIC_FONT_AXES(STDMETHODCALLTYPE *GetAutomaticFontAxes)(T * This);                                     \
    HRESULT(STDMETHODCALLTYPE *SetAutomaticFontAxes)(T * This, DWRITE_AUTOMATIC_FONT_AXES axes);
MIME_COM_INTERFACE(IDWriteTextLayout4)

/* ------------------------------------------------------------------ the factory */

#define IDWriteFactory_METHODS(T)                                                                                      \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *GetSystemFontCollection)(T * This, IDWriteFontCollection * *collection,                 \
                                                        BOOL check_for_updates);                                       \
    HRESULT(STDMETHODCALLTYPE *CreateCustomFontCollection)(T * This, IDWriteFontCollectionLoader * loader,             \
                                                           const void *key, UINT32 size,                               \
                                                           IDWriteFontCollection **collection);                        \
    HRESULT(STDMETHODCALLTYPE *RegisterFontCollectionLoader)(T * This, IDWriteFontCollectionLoader * loader);          \
    HRESULT(STDMETHODCALLTYPE *UnregisterFontCollectionLoader)(T * This, IDWriteFontCollectionLoader * loader);        \
    HRESULT(STDMETHODCALLTYPE *CreateFontFileReference)(T * This, const WCHAR *path, const FILETIME *write_time,       \
                                                        IDWriteFontFile **file);                                       \
    HRESULT(STDMETHODCALLTYPE *CreateCustomFontFileReference)(T * This, const void *key, UINT32 size,                  \
                                                              IDWriteFontFileLoader *loader,                           \
                                                              IDWriteFontFile **file);                                 \
    HRESULT(STDMETHODCALLTYPE *CreateFontFace)(T * This, DWRITE_FONT_FACE_TYPE type, UINT32 file_count,               \
                                               IDWriteFontFile *const *files, UINT32 index,                            \
                                               DWRITE_FONT_SIMULATIONS simulations, IDWriteFontFace **face);           \
    HRESULT(STDMETHODCALLTYPE *CreateRenderingParams)(T * This, IDWriteRenderingParams * *params);                     \
    HRESULT(STDMETHODCALLTYPE *CreateMonitorRenderingParams)(T * This, HANDLE monitor,                                 \
                                                             IDWriteRenderingParams **params);                         \
    HRESULT(STDMETHODCALLTYPE *CreateCustomRenderingParams)(T * This, FLOAT gamma, FLOAT contrast,                     \
                                                            FLOAT cleartype_level, DWRITE_PIXEL_GEOMETRY geometry,     \
                                                            DWRITE_RENDERING_MODE mode,                                \
                                                            IDWriteRenderingParams **params);                          \
    HRESULT(STDMETHODCALLTYPE *RegisterFontFileLoader)(T * This, IDWriteFontFileLoader * loader);                      \
    HRESULT(STDMETHODCALLTYPE *UnregisterFontFileLoader)(T * This, IDWriteFontFileLoader * loader);                    \
    HRESULT(STDMETHODCALLTYPE *CreateTextFormat)(T * This, const WCHAR *family, IDWriteFontCollection *collection,     \
                                                 DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style,                   \
                                                 DWRITE_FONT_STRETCH stretch, FLOAT size, const WCHAR *locale,         \
                                                 IDWriteTextFormat **format);                                          \
    HRESULT(STDMETHODCALLTYPE *CreateTypography)(T * This, IDWriteTypography * *typography);                           \
    HRESULT(STDMETHODCALLTYPE *GetGdiInterop)(T * This, IDWriteGdiInterop * *interop);                                 \
    HRESULT(STDMETHODCALLTYPE *CreateTextLayout)(T * This, const WCHAR *text, UINT32 length,                           \
                                                 IDWriteTextFormat *format, FLOAT max_width, FLOAT max_height,         \
                                                 IDWriteTextLayout **layout);                                          \
    HRESULT(STDMETHODCALLTYPE *CreateGdiCompatibleTextLayout)(T * This, const WCHAR *text, UINT32 length,              \
                                                              IDWriteTextFormat *format, FLOAT max_width,              \
                                                              FLOAT max_height, FLOAT pixels_per_dip,                  \
                                                              const DWRITE_MATRIX *transform, BOOL natural,            \
                                                              IDWriteTextLayout **layout);                             \
    HRESULT(STDMETHODCALLTYPE *CreateEllipsisTrimmingSign)(T * This, IDWriteTextFormat * format,                       \
                                                           IDWriteInlineObject * *sign);                               \
    HRESULT(STDMETHODCALLTYPE *CreateTextAnalyzer)(T * This, IDWriteTextAnalyzer * *analyzer);                         \
    HRESULT(STDMETHODCALLTYPE *CreateNumberSubstitution)(T * This, UINT32 method, const WCHAR *locale,                 \
                                                         BOOL ignore_user_override,                                    \
                                                         IDWriteNumberSubstitution **substitution);                    \
    HRESULT(STDMETHODCALLTYPE *CreateGlyphRunAnalysis)(T * This, const DWRITE_GLYPH_RUN *run, FLOAT pixels_per_dip,    \
                                                       const DWRITE_MATRIX *transform,                                 \
                                                       DWRITE_RENDERING_MODE mode, DWRITE_MEASURING_MODE measuring,    \
                                                       FLOAT origin_x, FLOAT origin_y,                                 \
                                                       IDWriteGlyphRunAnalysis **analysis);
MIME_COM_INTERFACE(IDWriteFactory)

#define IDWriteFactory1_METHODS(T)                                                                                     \
    IDWriteFactory_METHODS(T)                                                                                          \
    HRESULT(STDMETHODCALLTYPE *GetEudcFontCollection)(T * This, IDWriteFontCollection * *collection,                   \
                                                      BOOL check_for_updates);                                         \
    HRESULT(STDMETHODCALLTYPE *CreateCustomRenderingParams1)(T * This, FLOAT gamma, FLOAT contrast,                    \
                                                             FLOAT grayscale_contrast, FLOAT cleartype_level,          \
                                                             DWRITE_PIXEL_GEOMETRY geometry,                           \
                                                             DWRITE_RENDERING_MODE mode,                               \
                                                             IDWriteRenderingParams1 **params);
MIME_COM_INTERFACE(IDWriteFactory1)

#define IDWriteFactory2_METHODS(T)                                                                                     \
    IDWriteFactory1_METHODS(T)                                                                                         \
    HRESULT(STDMETHODCALLTYPE *GetSystemFontFallback)(T * This, IDWriteFontFallback * *fallback);                      \
    HRESULT(STDMETHODCALLTYPE *CreateFontFallbackBuilder)(T * This, IDWriteFontFallbackBuilder * *builder);            \
    HRESULT(STDMETHODCALLTYPE *TranslateColorGlyphRun)(T * This, FLOAT origin_x, FLOAT origin_y,                       \
                                                       const DWRITE_GLYPH_RUN *run,                                    \
                                                       const DWRITE_GLYPH_RUN_DESCRIPTION *description,                \
                                                       DWRITE_MEASURING_MODE measuring,                                \
                                                       const DWRITE_MATRIX *transform, UINT32 palette,                 \
                                                       IDWriteColorGlyphRunEnumerator **layers);                       \
    HRESULT(STDMETHODCALLTYPE *CreateCustomRenderingParams2)(T * This, FLOAT gamma, FLOAT contrast,                    \
                                                             FLOAT grayscale_contrast, FLOAT cleartype_level,          \
                                                             DWRITE_PIXEL_GEOMETRY geometry,                           \
                                                             DWRITE_RENDERING_MODE mode,                               \
                                                             DWRITE_GRID_FIT_MODE grid_fit,                            \
                                                             IDWriteRenderingParams2 **params);                        \
    HRESULT(STDMETHODCALLTYPE *CreateGlyphRunAnalysis2)(T * This, const DWRITE_GLYPH_RUN *run,                         \
                                                        const DWRITE_MATRIX *transform,                                \
                                                        DWRITE_RENDERING_MODE mode, DWRITE_MEASURING_MODE measuring,   \
                                                        DWRITE_GRID_FIT_MODE grid_fit,                                 \
                                                        DWRITE_TEXT_ANTIALIAS_MODE antialias, FLOAT origin_x,          \
                                                        FLOAT origin_y, IDWriteGlyphRunAnalysis **analysis);
MIME_COM_INTERFACE(IDWriteFactory2)

#define IDWriteFactory3_METHODS(T)                                                                                     \
    IDWriteFactory2_METHODS(T)                                                                                         \
    HRESULT(STDMETHODCALLTYPE *CreateGlyphRunAnalysis3)(T * This, const DWRITE_GLYPH_RUN *run,                         \
                                                        const DWRITE_MATRIX *transform,                                \
                                                        DWRITE_RENDERING_MODE1 mode, DWRITE_MEASURING_MODE measuring,  \
                                                        DWRITE_GRID_FIT_MODE grid_fit,                                 \
                                                        DWRITE_TEXT_ANTIALIAS_MODE antialias, FLOAT origin_x,          \
                                                        FLOAT origin_y, IDWriteGlyphRunAnalysis **analysis);           \
    HRESULT(STDMETHODCALLTYPE *CreateCustomRenderingParams3)(T * This, FLOAT gamma, FLOAT contrast,                    \
                                                             FLOAT grayscale_contrast, FLOAT cleartype_level,          \
                                                             DWRITE_PIXEL_GEOMETRY geometry,                           \
                                                             DWRITE_RENDERING_MODE1 mode,                              \
                                                             DWRITE_GRID_FIT_MODE grid_fit,                            \
                                                             IDWriteRenderingParams3 **params);                        \
    HRESULT(STDMETHODCALLTYPE *CreateFontFaceReference)(T * This, IDWriteFontFile * file, UINT32 index,                \
                                                        DWRITE_FONT_SIMULATIONS simulations,                           \
                                                        IDWriteFontFaceReference **reference);                         \
    HRESULT(STDMETHODCALLTYPE *CreateFontFaceReferenceFromPath)(T * This, const WCHAR *path,                           \
                                                                const FILETIME *write_time, UINT32 index,              \
                                                                DWRITE_FONT_SIMULATIONS simulations,                   \
                                                                IDWriteFontFaceReference **reference);                 \
    HRESULT(STDMETHODCALLTYPE *GetSystemFontSet)(T * This, IDWriteFontSet * *set);                                     \
    HRESULT(STDMETHODCALLTYPE *CreateFontSetBuilder)(T * This, IDWriteFontSetBuilder * *builder);                      \
    HRESULT(STDMETHODCALLTYPE *CreateFontCollectionFromFontSet)(T * This, IDWriteFontSet * set,                        \
                                                                IDWriteFontCollection1 **collection);                  \
    HRESULT(STDMETHODCALLTYPE *GetSystemFontCollection3)(T * This, BOOL include_downloadable,                          \
                                                         IDWriteFontCollection1 **collection,                          \
                                                         BOOL check_for_updates);                                      \
    HRESULT(STDMETHODCALLTYPE *GetFontDownloadQueue)(T * This, IDWriteFontDownloadQueue * *queue);
MIME_COM_INTERFACE(IDWriteFactory3)

/* ------------------------------------------------------------------ interface ids */

MIME_DEFINE_GUID(IID_IDWriteFactory, 0xb859ee5a, 0xd838, 0x4b5b, 0xa2, 0xe8, 0x1a, 0xdc, 0x7d, 0x93, 0xdb, 0x48);
MIME_DEFINE_GUID(IID_IDWriteFactory1, 0x30572f99, 0xdac6, 0x41db, 0xa1, 0x6e, 0x04, 0x86, 0x30, 0x7e, 0x60, 0x6a);
MIME_DEFINE_GUID(IID_IDWriteFactory2, 0x0439fc60, 0xca44, 0x4994, 0x8d, 0xee, 0x3a, 0x9a, 0xf7, 0xb7, 0x32, 0xec);
MIME_DEFINE_GUID(IID_IDWriteFactory3, 0x9a1b41c3, 0xd3bb, 0x466a, 0x87, 0xfc, 0xfe, 0x67, 0x55, 0x6a, 0x3b, 0x65);
MIME_DEFINE_GUID(IID_IDWriteFontCollection, 0xa84cee02, 0x3eea, 0x4eee, 0xa8, 0x27, 0x87, 0xc1, 0xa0, 0x2a, 0x0f,
                 0xcc);
MIME_DEFINE_GUID(IID_IDWriteFactory4, 0x4b0b5bd3, 0x0797, 0x4549, 0x8a, 0xc5, 0xfe, 0x91, 0x5c, 0xc5, 0x38, 0x56);
MIME_DEFINE_GUID(IID_IDWriteFactory5, 0x958db99a, 0xbe2a, 0x4f09, 0xaf, 0x7d, 0x65, 0x18, 0x98, 0x03, 0xd1, 0xd3);
MIME_DEFINE_GUID(IID_IDWriteFactory6, 0xf3744d80, 0x21f7, 0x42eb, 0xb3, 0x5d, 0x99, 0x5b, 0xc7, 0x2f, 0xc2, 0x23);
MIME_DEFINE_GUID(IID_IDWriteFontCollection1, 0x53585141, 0xd9f8, 0x4095, 0x83, 0x21, 0xd7, 0x3c, 0xf6, 0xbd, 0x11, 0x6c);
MIME_DEFINE_GUID(IID_IDWriteFontCollection2, 0x514039c6, 0x4617, 0x4064, 0xbf, 0x8b, 0x92, 0xea, 0x83, 0xe5, 0x06, 0xe0);
MIME_DEFINE_GUID(IID_IDWriteFontFamily1, 0xda20d8ef, 0x812a, 0x4c43, 0x98, 0x02, 0x62, 0xec, 0x4a, 0xbd, 0x7a, 0xdf);
MIME_DEFINE_GUID(IID_IDWriteFontFamily2, 0x3ed49e77, 0xa398, 0x4261, 0xb9, 0xcf, 0xc1, 0x26, 0xc2, 0x13, 0x1e, 0xf3);
MIME_DEFINE_GUID(IID_IDWriteFontList, 0x1a0d8438, 0x1d97, 0x4ec1, 0xae, 0xf9, 0xa2, 0xfb, 0x86, 0xed, 0x6a, 0xcb);
MIME_DEFINE_GUID(IID_IDWriteFontFamily, 0xda20d8ef, 0x812a, 0x4c43, 0x98, 0x02, 0x62, 0xec, 0x4a, 0xbd, 0x7a, 0xdd);
MIME_DEFINE_GUID(IID_IDWriteFont, 0xacd16696, 0x8c14, 0x4f5d, 0x87, 0x7e, 0xfe, 0x3f, 0xc1, 0xd3, 0x27, 0x37);
MIME_DEFINE_GUID(IID_IDWriteFont1, 0xacd16696, 0x8c14, 0x4f5d, 0x87, 0x7e, 0xfe, 0x3f, 0xc1, 0xd3, 0x27, 0x38);
MIME_DEFINE_GUID(IID_IDWriteFontFace, 0x5f49804d, 0x7024, 0x4d43, 0xbf, 0xa9, 0xd2, 0x59, 0x84, 0xf5, 0x38, 0x49);
MIME_DEFINE_GUID(IID_IDWriteFontFace1, 0xa71efdb4, 0x9fdb, 0x4838, 0xad, 0x90, 0xcf, 0xc3, 0xbe, 0x8c, 0x3d, 0xaf);
MIME_DEFINE_GUID(IID_IDWriteFontFace2, 0xd8b768ff, 0x64bc, 0x4e66, 0x98, 0x2b, 0xec, 0x8e, 0x87, 0xf6, 0x93, 0xf7);
MIME_DEFINE_GUID(IID_IDWriteFontFile, 0x739d886a, 0xcef5, 0x47dc, 0x87, 0x69, 0x1a, 0x8b, 0x41, 0xbe, 0xbb, 0xb0);
MIME_DEFINE_GUID(IID_IDWriteFontFileLoader, 0x727cad4e, 0xd6af, 0x4c9e, 0x8a, 0x08, 0xd6, 0x95, 0xb1, 0x1c, 0xaa,
                 0x49);
MIME_DEFINE_GUID(IID_IDWriteLocalFontFileLoader, 0xb2d9f3ec, 0xc9fe, 0x4a11, 0xa2, 0xec, 0xd8, 0x62, 0x08, 0xf7,
                 0xc0, 0xa2);
MIME_DEFINE_GUID(IID_IDWriteFontFileStream, 0x6d4865fe, 0x0ab8, 0x4d91, 0x8f, 0x62, 0x5d, 0xd6, 0xbe, 0x34, 0xa3,
                 0xe0);
MIME_DEFINE_GUID(IID_IDWriteFontCollectionLoader, 0xcca920e4, 0x52f0, 0x492b, 0xbf, 0xa8, 0x29, 0xc7, 0x2e, 0xe0,
                 0xa4, 0x68);
MIME_DEFINE_GUID(IID_IDWriteFontFileEnumerator, 0x72755049, 0x5ff7, 0x435d, 0x83, 0x48, 0x4b, 0xe9, 0x7c, 0xfa,
                 0x6c, 0x7c);
MIME_DEFINE_GUID(IID_IDWriteLocalizedStrings, 0x08256209, 0x099a, 0x4b34, 0xb8, 0x6d, 0xc2, 0x2b, 0x11, 0x0e, 0x77,
                 0x71);
MIME_DEFINE_GUID(IID_IDWriteRenderingParams, 0x2f0da53a, 0x2add, 0x47cd, 0x82, 0xee, 0xd9, 0xec, 0x34, 0x68, 0x8e,
                 0x75);
MIME_DEFINE_GUID(IID_IDWriteRenderingParams1, 0x94413cf4, 0xa6fc, 0x4248, 0x8b, 0x50, 0x66, 0x74, 0x34, 0x8f, 0xca,
                 0xd3);
MIME_DEFINE_GUID(IID_IDWriteRenderingParams2, 0xf9d711c3, 0x9777, 0x40ae, 0x87, 0xe8, 0x3e, 0x5a, 0xf9, 0xbf, 0x09,
                 0x48);
MIME_DEFINE_GUID(IID_IDWriteGdiInterop, 0x1edd9491, 0x9853, 0x4299, 0x89, 0x8f, 0x64, 0x32, 0x98, 0x3b, 0x6f, 0x3a);
MIME_DEFINE_GUID(IID_IDWriteGlyphRunAnalysis, 0x7d97dbf7, 0xe085, 0x42d4, 0x81, 0xe3, 0x6a, 0x88, 0x3b, 0xde, 0xd1,
                 0x18);
MIME_DEFINE_GUID(IID_IDWriteFontFallback, 0xefa008f9, 0xf7a1, 0x48bf, 0xb0, 0x5c, 0xf2, 0x24, 0x71, 0x3c, 0xc0,
                 0xff);
MIME_DEFINE_GUID(IID_IDWriteTextAnalysisSource, 0x688e1a58, 0x5094, 0x47c8, 0xad, 0xc8, 0xfb, 0xce, 0xa6, 0x0a,
                 0xe9, 0x2b);
MIME_DEFINE_GUID(IID_IDWritePixelSnapping, 0xeaf3a2da, 0xecf4, 0x4d24, 0xb6, 0x44, 0xb3, 0x4f, 0x68, 0x42, 0x02, 0x4b);
MIME_DEFINE_GUID(IID_IDWriteTextRenderer, 0xef8a8135, 0x5cc6, 0x45fe, 0x88, 0x25, 0xc5, 0xa0, 0x72, 0x4e, 0xb8, 0x19);
MIME_DEFINE_GUID(IID_IDWriteInlineObject, 0x8339fde3, 0x106f, 0x47ab, 0x83, 0x73, 0x1c, 0x62, 0x95, 0xeb, 0x10, 0xb3);
MIME_DEFINE_GUID(IID_IDWriteTextFormat, 0x9c906818, 0x31d7, 0x4fd3, 0xa1, 0x51, 0x7c, 0x5e, 0x22, 0x5d, 0xb5, 0x5a);
MIME_DEFINE_GUID(IID_IDWriteTypography, 0x55f1112b, 0x1dc2, 0x4b3c, 0x95, 0x41, 0xf4, 0x68, 0x94, 0xed, 0x85, 0xb6);
MIME_DEFINE_GUID(IID_IDWriteTextLayout, 0x53737037, 0x6d14, 0x410b, 0x9b, 0xfe, 0x0b, 0x18, 0x2b, 0xb7, 0x09, 0x61);
MIME_DEFINE_GUID(IID_IDWriteNumberSubstitution, 0x14885cc9, 0xbab0, 0x4f90, 0xb6, 0xed, 0x5c, 0x36, 0x6a, 0x2c, 0xd0,
                 0x3d);
MIME_DEFINE_GUID(IID_IDWriteTextAnalysisSink, 0x5810cd44, 0x0ca0, 0x4701, 0xb3, 0xfa, 0xbe, 0xc5, 0x18, 0x2a, 0xe4,
                 0xf6);
MIME_DEFINE_GUID(IID_IDWriteTextAnalyzer, 0xb7e6163e, 0x7f46, 0x43b4, 0x84, 0xb3, 0xe4, 0xe6, 0x24, 0x9c, 0x36, 0x5d);
MIME_DEFINE_GUID(IID_IDWriteTextAnalysisSource1, 0x639cfad8, 0x0fb4, 0x4b21, 0xa5, 0x8a, 0x06, 0x79, 0x20, 0x12, 0x00,
                 0x09);
MIME_DEFINE_GUID(IID_IDWriteTextAnalysisSink1, 0xb0d941a0, 0x85e7, 0x4d8b, 0x9f, 0xd3, 0x5c, 0xed, 0x99, 0x34, 0x48,
                 0x2a);
MIME_DEFINE_GUID(IID_IDWriteTextAnalyzer1, 0x80dad800, 0xe21f, 0x4e83, 0x96, 0xce, 0xbf, 0xcc, 0xe5, 0x00, 0xdb, 0x7c);
MIME_DEFINE_GUID(IID_IDWriteTextLayout1, 0x9064d822, 0x80a7, 0x465c, 0xa9, 0x86, 0xdf, 0x65, 0xf7, 0x8b, 0x8f, 0xeb);
MIME_DEFINE_GUID(IID_IDWriteTextRenderer1, 0xd3e0e934, 0x22a0, 0x427e, 0xaa, 0xe4, 0x7d, 0x95, 0x74, 0xb5, 0x9d, 0xb1);
MIME_DEFINE_GUID(IID_IDWriteTextFormat1, 0x5f174b49, 0x0d8b, 0x4cfb, 0x8b, 0xca, 0xf1, 0xcc, 0xe9, 0xd0, 0x6c, 0x67);
MIME_DEFINE_GUID(IID_IDWriteTextLayout2, 0x1093c18f, 0x8d5e, 0x43f0, 0xb0, 0x64, 0x09, 0x17, 0x31, 0x1b, 0x52, 0x5e);
MIME_DEFINE_GUID(IID_IDWriteTextAnalyzer2, 0x553a9ff3, 0x5693, 0x4df7, 0xb5, 0x2b, 0x74, 0x80, 0x6f, 0x7f, 0x2e, 0xb9);
MIME_DEFINE_GUID(IID_IDWriteFontFallbackBuilder, 0xfd882d06, 0x8aba, 0x4fb8, 0xb8, 0x49, 0x8b, 0xe8, 0xb7, 0x3e, 0x14,
                 0xde);
MIME_DEFINE_GUID(IID_IDWriteTextFormat2, 0xf67e0edd, 0x9e3d, 0x4ecc, 0x8c, 0x32, 0x41, 0x83, 0x25, 0x3d, 0xfe, 0x70);
MIME_DEFINE_GUID(IID_IDWriteTextFormat3, 0x6d3b5641, 0xe550, 0x430d, 0xa8, 0x5b, 0xb7, 0xbf, 0x48, 0xa9, 0x34, 0x27);
MIME_DEFINE_GUID(IID_IDWriteTextLayout3, 0x07ddcd52, 0x020e, 0x4de8, 0xac, 0x33, 0x6c, 0x95, 0x3d, 0x83, 0xf9, 0x2d);
MIME_DEFINE_GUID(IID_IDWriteTextLayout4, 0x05a9bf42, 0x223f, 0x4441, 0xb5, 0xfb, 0x82, 0x63, 0x68, 0x5f, 0x55, 0xe9);

DWRITEAPI HRESULT WINAPI DWriteCreateFactory(DWRITE_FACTORY_TYPE type, REFIID iid, IUnknown **factory);

#endif
