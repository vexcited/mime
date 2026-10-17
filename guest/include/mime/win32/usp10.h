/*
 * uniscribe, usp10.dll: itemizing, shaping and placing unicode text.
 */
#ifndef MIME_WIN32_USP10_H
#define MIME_WIN32_USP10_H

#include <mime/win32/wingdi.h>

#ifdef MIME_BUILD_USP10
#define USPAPI DECLSPEC_EXPORT HRESULT WINAPI
#define USPAPI_(type) DECLSPEC_EXPORT type WINAPI
#else
#define USPAPI DECLSPEC_IMPORT HRESULT WINAPI
#define USPAPI_(type) DECLSPEC_IMPORT type WINAPI
#endif

#define USP_E_SCRIPT_NOT_IN_FONT ((HRESULT)0x80040200L)

typedef void *SCRIPT_CACHE;

#define SCRIPT_UNDEFINED 0

typedef struct tag_SCRIPT_CONTROL {
    DWORD uDefaultLanguage : 16;
    DWORD fContextDigits : 1;
    DWORD fInvertPreBoundDir : 1;
    DWORD fInvertPostBoundDir : 1;
    DWORD fLinkStringBefore : 1;
    DWORD fLinkStringAfter : 1;
    DWORD fNeutralOverride : 1;
    DWORD fNumericOverride : 1;
    DWORD fLegacyBidiClass : 1;
    DWORD fMergeNeutralItems : 1;
    DWORD fUseStandardBidi : 1;
    DWORD fReserved : 6;
} SCRIPT_CONTROL;

typedef struct tag_SCRIPT_STATE {
    WORD uBidiLevel : 5;
    WORD fOverrideDirection : 1;
    WORD fInhibitSymSwap : 1;
    WORD fCharShape : 1;
    WORD fDigitSubstitute : 1;
    WORD fInhibitLigate : 1;
    WORD fDisplayZWG : 1;
    WORD fArabicNumContext : 1;
    WORD fGcpClusters : 1;
    WORD fReserved : 1;
    WORD fEngineReserved : 2;
} SCRIPT_STATE;

typedef struct tag_SCRIPT_ANALYSIS {
    WORD eScript : 10;
    WORD fRTL : 1;
    WORD fLayoutRTL : 1;
    WORD fLinkBefore : 1;
    WORD fLinkAfter : 1;
    WORD fLogicalOrder : 1;
    WORD fNoGlyphIndex : 1;
    SCRIPT_STATE s;
} SCRIPT_ANALYSIS;

typedef struct tag_SCRIPT_ITEM {
    int iCharPos;
    SCRIPT_ANALYSIS a;
} SCRIPT_ITEM;

enum SCRIPT_JUSTIFY {
    SCRIPT_JUSTIFY_NONE = 0,
    SCRIPT_JUSTIFY_ARABIC_BLANK = 1,
    SCRIPT_JUSTIFY_CHARACTER = 2,
    SCRIPT_JUSTIFY_RESERVED1 = 3,
    SCRIPT_JUSTIFY_BLANK = 4,
    SCRIPT_JUSTIFY_RESERVED2 = 5,
    SCRIPT_JUSTIFY_RESERVED3 = 6,
    SCRIPT_JUSTIFY_ARABIC_NORMAL = 7,
    SCRIPT_JUSTIFY_ARABIC_KASHIDA = 8,
    SCRIPT_JUSTIFY_ARABIC_ALEF = 9,
    SCRIPT_JUSTIFY_ARABIC_HA = 10,
    SCRIPT_JUSTIFY_ARABIC_RA = 11,
    SCRIPT_JUSTIFY_ARABIC_BA = 12,
    SCRIPT_JUSTIFY_ARABIC_BARA = 13,
    SCRIPT_JUSTIFY_ARABIC_SEEN = 14,
    SCRIPT_JUSTIFY_ARABIC_SEEN_M = 15,
};

typedef struct tag_SCRIPT_VISATTR {
    WORD uJustification : 4;
    WORD fClusterStart : 1;
    WORD fDiacritic : 1;
    WORD fZeroWidth : 1;
    WORD fReserved : 1;
    WORD fShapeReserved : 8;
} SCRIPT_VISATTR;

typedef struct tagGOFFSET {
    LONG du;
    LONG dv;
} GOFFSET;

typedef struct tag_SCRIPT_LOGATTR {
    BYTE fSoftBreak : 1;
    BYTE fWhiteSpace : 1;
    BYTE fCharStop : 1;
    BYTE fWordStop : 1;
    BYTE fInvalid : 1;
    BYTE fReserved : 3;
} SCRIPT_LOGATTR;

typedef struct {
    DWORD langid : 16;
    DWORD fNumeric : 1;
    DWORD fComplex : 1;
    DWORD fNeedsWordBreaking : 1;
    DWORD fNeedsCaretInfo : 1;
    DWORD bCharSet : 8;
    DWORD fControl : 1;
    DWORD fPrivateUseArea : 1;
    DWORD fNeedsCharacterJustify : 1;
    DWORD fInvalidGlyph : 1;
    DWORD fInvalidLogAttr : 1;
    DWORD fCDM : 1;
    DWORD fAmbiguousCharSet : 1;
    DWORD fClusterSizeVaries : 1;
    DWORD fRejectInvalid : 1;
} SCRIPT_PROPERTIES;

typedef struct {
    int cBytes;
    WORD wgBlank;
    WORD wgDefault;
    WORD wgInvalid;
    WORD wgKashida;
    int iKashidaWidth;
} SCRIPT_FONTPROPERTIES;

typedef struct {
    DWORD NationalDigitLanguage : 16;
    DWORD TraditionalDigitLanguage : 16;
    DWORD DigitSubstitute : 8;
    DWORD dwReserved;
} SCRIPT_DIGITSUBSTITUTE;

#define SCRIPT_DIGITSUBSTITUTE_CONTEXT 0
#define SCRIPT_DIGITSUBSTITUTE_NONE 1
#define SCRIPT_DIGITSUBSTITUTE_NATIONAL 2
#define SCRIPT_DIGITSUBSTITUTE_TRADITIONAL 3

/* ScriptIsComplex flags. */
#define SIC_COMPLEX 1
#define SIC_ASCIIDIGIT 2
#define SIC_NEUTRAL 4

/* ScriptGetCMap flags. */
#define SGCM_RTL 0x00000001

typedef void *SCRIPT_STRING_ANALYSIS;

typedef struct tag_SCRIPT_TABDEF {
    int cTabStops;
    int iScale;
    int *pTabStops;
    int iTabOrigin;
} SCRIPT_TABDEF;

/* ScriptStringAnalyse flags. */
#define SSA_PASSWORD 0x00000001
#define SSA_TAB 0x00000002
#define SSA_CLIP 0x00000004
#define SSA_FIT 0x00000008
#define SSA_DZWG 0x00000010
#define SSA_FALLBACK 0x00000020
#define SSA_BREAK 0x00000040
#define SSA_GLYPHS 0x00000080
#define SSA_RTL 0x00000100
#define SSA_GCP 0x00000200
#define SSA_HOTKEY 0x00000400
#define SSA_METAFILE 0x00000800
#define SSA_LINK 0x00001000
#define SSA_HIDEHOTKEY 0x00002000
#define SSA_HOTKEYONLY 0x00002400
#define SSA_FULLMEASURE 0x04000000
#define SSA_LPKANSIFALLBACK 0x08000000
#define SSA_PIDX 0x10000000
#define SSA_LAYOUTRTL 0x20000000
#define SSA_DONTGLYPH 0x40000000
#define SSA_NOKASHIDA 0x80000000

typedef ULONG OPENTYPE_TAG;

#define SCRIPT_TAG_UNKNOWN 0x00000000

typedef struct opentype_feature_record {
    OPENTYPE_TAG tagFeature;
    LONG lParameter;
} OPENTYPE_FEATURE_RECORD;

typedef struct textrange_properties {
    OPENTYPE_FEATURE_RECORD *potfRecords;
    int cotfRecords;
} TEXTRANGE_PROPERTIES;

typedef struct script_charprop {
    WORD fCanGlyphAlone : 1;
    WORD reserved : 15;
} SCRIPT_CHARPROP;

typedef struct script_glyphprop {
    SCRIPT_VISATTR sva;
    WORD reserved;
} SCRIPT_GLYPHPROP;

USPAPI ScriptItemize(const WCHAR *chars, int count, int max_items, const SCRIPT_CONTROL *control,
                     const SCRIPT_STATE *state, SCRIPT_ITEM *items, int *item_count);
USPAPI ScriptShape(HDC dc, SCRIPT_CACHE *cache, const WCHAR *chars, int count, int max_glyphs,
                   SCRIPT_ANALYSIS *analysis, WORD *glyphs, WORD *clusters, SCRIPT_VISATTR *attrs, int *glyph_count);
USPAPI ScriptPlace(HDC dc, SCRIPT_CACHE *cache, const WORD *glyphs, int count, const SCRIPT_VISATTR *attrs,
                   SCRIPT_ANALYSIS *analysis, int *advances, GOFFSET *offsets, ABC *abc);
USPAPI ScriptTextOut(const HDC dc, SCRIPT_CACHE *cache, int x, int y, UINT options, const RECT *rect,
                     const SCRIPT_ANALYSIS *analysis, const WCHAR *reserved, int reserved2, const WORD *glyphs,
                     int count, const int *advances, const int *justify, const GOFFSET *offsets);
USPAPI ScriptIsComplex(const WCHAR *chars, int count, DWORD flags);
USPAPI ScriptFreeCache(SCRIPT_CACHE *cache);
USPAPI ScriptGetProperties(const SCRIPT_PROPERTIES ***properties, int *count);
USPAPI ScriptGetFontProperties(HDC dc, SCRIPT_CACHE *cache, SCRIPT_FONTPROPERTIES *properties);
USPAPI ScriptGetCMap(HDC dc, SCRIPT_CACHE *cache, const WCHAR *chars, int count, DWORD flags, WORD *glyphs);
USPAPI ScriptGetGlyphABCWidth(HDC dc, SCRIPT_CACHE *cache, WORD glyph, ABC *abc);
USPAPI ScriptCacheGetHeight(HDC dc, SCRIPT_CACHE *cache, LONG *height);
USPAPI ScriptBreak(const WCHAR *chars, int count, const SCRIPT_ANALYSIS *analysis, SCRIPT_LOGATTR *attrs);
USPAPI ScriptCPtoX(int cp, BOOL trailing, int chars, int glyphs, const WORD *clusters, const SCRIPT_VISATTR *attrs,
                   const int *advances, const SCRIPT_ANALYSIS *analysis, int *x);
USPAPI ScriptXtoCP(int x, int chars, int glyphs, const WORD *clusters, const SCRIPT_VISATTR *attrs,
                   const int *advances, const SCRIPT_ANALYSIS *analysis, int *cp, int *trailing);
USPAPI ScriptLayout(int runs, const BYTE *levels, int *visual_to_logical, int *logical_to_visual);
USPAPI ScriptRecordDigitSubstitution(LCID locale, SCRIPT_DIGITSUBSTITUTE *sds);
USPAPI ScriptApplyDigitSubstitution(const SCRIPT_DIGITSUBSTITUTE *sds, SCRIPT_CONTROL *control,
                                    SCRIPT_STATE *state);
USPAPI ScriptJustify(const SCRIPT_VISATTR *attrs, const int *advances, int glyphs, int dx, int min_kashida,
                     int *justify);
USPAPI ScriptGetLogicalWidths(const SCRIPT_ANALYSIS *analysis, int chars, int glyphs, const int *glyph_widths,
                              const WORD *clusters, const SCRIPT_VISATTR *attrs, int *dx);
USPAPI ScriptApplyLogicalWidth(const int *dx, int chars, int glyphs, const WORD *clusters, const SCRIPT_VISATTR *attrs,
                               const int *advances, const SCRIPT_ANALYSIS *analysis, ABC *abc, int *justify);

USPAPI ScriptStringAnalyse(HDC dc, const void *string, int length, int max_glyphs, int charset, DWORD flags,
                           int width, SCRIPT_CONTROL *control, SCRIPT_STATE *state, const int *dx,
                           SCRIPT_TABDEF *tabs, const BYTE *in_class, SCRIPT_STRING_ANALYSIS *ssa);
USPAPI ScriptStringFree(SCRIPT_STRING_ANALYSIS *ssa);
USPAPI_(const SIZE *) ScriptString_pSize(SCRIPT_STRING_ANALYSIS ssa);
USPAPI_(const int *) ScriptString_pcOutChars(SCRIPT_STRING_ANALYSIS ssa);
USPAPI_(const SCRIPT_LOGATTR *) ScriptString_pLogAttr(SCRIPT_STRING_ANALYSIS ssa);
USPAPI ScriptStringGetOrder(SCRIPT_STRING_ANALYSIS ssa, UINT *order);
USPAPI ScriptStringCPtoX(SCRIPT_STRING_ANALYSIS ssa, int cp, BOOL trailing, int *x);
USPAPI ScriptStringXtoCP(SCRIPT_STRING_ANALYSIS ssa, int x, int *cp, int *trailing);
USPAPI ScriptStringGetLogicalWidths(SCRIPT_STRING_ANALYSIS ssa, int *dx);
USPAPI ScriptStringValidate(SCRIPT_STRING_ANALYSIS ssa);
USPAPI ScriptStringOut(SCRIPT_STRING_ANALYSIS ssa, int x, int y, UINT options, const RECT *rect, int min_sel,
                       int max_sel, BOOL disabled);

USPAPI ScriptItemizeOpenType(const WCHAR *chars, int count, int max_items, const SCRIPT_CONTROL *control,
                             const SCRIPT_STATE *state, SCRIPT_ITEM *items, OPENTYPE_TAG *script_tags,
                             int *item_count);
USPAPI ScriptShapeOpenType(HDC dc, SCRIPT_CACHE *cache, SCRIPT_ANALYSIS *analysis, OPENTYPE_TAG script,
                           OPENTYPE_TAG language, int *range_chars, TEXTRANGE_PROPERTIES **range_properties,
                           int ranges, const WCHAR *chars, int count, int max_glyphs, WORD *clusters,
                           SCRIPT_CHARPROP *char_props, WORD *glyphs, SCRIPT_GLYPHPROP *glyph_props,
                           int *glyph_count);
USPAPI ScriptPlaceOpenType(HDC dc, SCRIPT_CACHE *cache, SCRIPT_ANALYSIS *analysis, OPENTYPE_TAG script,
                           OPENTYPE_TAG language, int *range_chars, TEXTRANGE_PROPERTIES **range_properties,
                           int ranges, const WCHAR *chars, WORD *clusters, SCRIPT_CHARPROP *char_props, int count,
                           const WORD *glyphs, const SCRIPT_GLYPHPROP *glyph_props, int glyph_count,
                           int *advances, GOFFSET *offsets, ABC *abc);
USPAPI ScriptGetFontScriptTags(HDC dc, SCRIPT_CACHE *cache, SCRIPT_ANALYSIS *analysis, int max_tags,
                               OPENTYPE_TAG *tags, int *count);
USPAPI ScriptGetFontLanguageTags(HDC dc, SCRIPT_CACHE *cache, SCRIPT_ANALYSIS *analysis, OPENTYPE_TAG script,
                                 int max_tags, OPENTYPE_TAG *tags, int *count);
USPAPI ScriptGetFontFeatureTags(HDC dc, SCRIPT_CACHE *cache, SCRIPT_ANALYSIS *analysis, OPENTYPE_TAG script,
                                OPENTYPE_TAG language, int max_tags, OPENTYPE_TAG *tags, int *count);
USPAPI ScriptGetFontAlternateGlyphs(HDC dc, SCRIPT_CACHE *cache, SCRIPT_ANALYSIS *analysis, OPENTYPE_TAG script,
                                    OPENTYPE_TAG language, OPENTYPE_TAG feature, WORD glyph, int max_alternates,
                                    WORD *alternates, int *count);
USPAPI ScriptSubstituteSingleGlyph(HDC dc, SCRIPT_CACHE *cache, SCRIPT_ANALYSIS *analysis, OPENTYPE_TAG script,
                                   OPENTYPE_TAG language, OPENTYPE_TAG feature, LONG parameter, WORD glyph,
                                   WORD *out_glyph);
USPAPI ScriptPositionSingleGlyph(HDC dc, SCRIPT_CACHE *cache, SCRIPT_ANALYSIS *analysis, OPENTYPE_TAG script,
                                 OPENTYPE_TAG language, OPENTYPE_TAG feature, LONG parameter, WORD glyph,
                                 int advance, GOFFSET offset, int *out_advance, GOFFSET *out_offset);

#endif
