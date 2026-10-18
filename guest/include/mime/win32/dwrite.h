/*
 * directwrite: fonts, their metrics and their glyphs. the interfaces and
 * their method order are windows', as programs call them through the
 * vtables. text layout and the text analyzer are declared only as far as the
 * factory's table needs them.
 */
#ifndef MIME_WIN32_DWRITE_H
#define MIME_WIN32_DWRITE_H

#include <mime/win32/objbase.h>
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

/* direct2d's simplified geometry, which outlines are described with. */
typedef struct D2D1_POINT_2F {
    FLOAT x, y;
} D2D1_POINT_2F;

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

DWRITEAPI HRESULT WINAPI DWriteCreateFactory(DWRITE_FACTORY_TYPE type, REFIID iid, IUnknown **factory);

#endif
