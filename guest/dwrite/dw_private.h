/*
 * dwrite.dll: fonts for programs that draw text themselves. A factory makes
 * font files from paths or loaders, faces from files, and collections of
 * families. The system collection is read from the fonts directory, the
 * names and styles of every face from its tables. Glyphs come from the
 * truetype library the kernel's font engine uses.
 */
#ifndef DW_PRIVATE_H
#define DW_PRIVATE_H

#define MIME_BUILD_DWRITE
#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>
#include <mime/win32/dwrite.h>
#include <ttf/ttf.h>
#include <ttf/ttf_internal.h>

#define VTBL_FN(f) ((void *)(f))
#define DW_TAG(a, b, c, d) (((UINT32)(a) << 24) | ((UINT32)(b) << 16) | ((UINT32)(c) << 8) | (UINT32)(d))
/* the table tags as the tables of the interfaces take them: the first letter in the low byte. */
#define DW_TAG_LE(a, b, c, d) (((UINT32)(d) << 24) | ((UINT32)(c) << 16) | ((UINT32)(b) << 8) | (UINT32)(a))

/* ------------------------------------------------------------------ memory and text */

void *DwAlloc(SIZE_T size);
void *DwAllocZero(SIZE_T size);
void DwFree(void *p);
WCHAR *DwStrDup(const WCHAR *s);
WCHAR *DwStrDupN(const WCHAR *s, UINT32 length);
void DwLower(WCHAR *s);
/* a case insensitive comparison that orders names the way the collection sorts them. */
int DwCompareNames(const WCHAR *a, const WCHAR *b);

static inline UINT16 DwBe16(const BYTE *p) { return (UINT16)((p[0] << 8) | p[1]); }
static inline UINT32 DwBe32(const BYTE *p)
{
    return ((UINT32)p[0] << 24) | ((UINT32)p[1] << 16) | ((UINT32)p[2] << 8) | p[3];
}

/* ------------------------------------------------------------------ localized strings */

typedef struct DwStringEntry {
    WCHAR *locale;
    WCHAR *text;
} DwStringEntry;

typedef struct DwStrings DwStrings;

DwStrings *DwStringsNew(void);
/* adds the locale and text, copies of them, unless the locale already has one. */
BOOL DwStringsAdd(DwStrings *strings, const WCHAR *locale, UINT32 locale_length, const WCHAR *text, UINT32 length);
UINT32 DwStringsCount(const DwStrings *strings);
const DwStringEntry *DwStringsAt(const DwStrings *strings, UINT32 index);
/* the text of the locale, else the english one, else the first. */
const WCHAR *DwStringsBest(const DwStrings *strings, const WCHAR *locale);
IDWriteLocalizedStrings *DwStringsInterface(DwStrings *strings);
/* a new object with the same entries. */
DwStrings *DwStringsCopy(const DwStrings *strings);
void DwStringsRelease(DwStrings *strings);
void DwStringsAddRef(DwStrings *strings);

/* ------------------------------------------------------------------ font files and streams */

typedef struct DwFactory DwFactory;

typedef struct DwFontFile {
    const IDWriteFontFileVtbl *lpVtbl;
    LONG refs;
    IDWriteFontFileLoader *loader;
    void *key;
    UINT32 key_size;
} DwFontFile;

HRESULT DwFontFileCreate(IDWriteFontFileLoader *loader, const void *key, UINT32 key_size, IDWriteFontFile **file);
HRESULT DwFontFileFromPath(const WCHAR *path, const FILETIME *write_time, IDWriteFontFile **file);
/* the shared loader of files on disk, whose keys are a write time and a path. */
IDWriteLocalFontFileLoader *DwLocalLoader(void);
/* the path in a key of the local loader, or nullptr. */
const WCHAR *DwLocalKeyPath(const void *key, UINT32 key_size);
/* a file's stream, opened through its loader. */
HRESULT DwFontFileOpen(IDWriteFontFile *file, IDWriteFontFileStream **stream);

/* ------------------------------------------------------------------ parsed fonts */

/* what a face's tables say, shared by the faces and fonts made from one file and index. */
typedef struct DwFontData {
    LONG refs;
    IDWriteFontFileStream *stream;
    const BYTE *bytes;
    UINT64 size;
    void *fragment;
    UINT32 sfnt_offset;
    UINT32 face_index;
    UINT32 face_count;
    UINT32 num_tables;
    const BYTE *directory;
    BOOL collection;
    BOOL cff;
    MimeTtfFont ttf;
    BOOL ttf_ok;
    MimeTtfMetrics metrics;
} DwFontData;

/* opens face index of the font file; collections count their faces, other files have one. */
HRESULT DwFontDataOpen(IDWriteFontFile *file, UINT32 face_index, DwFontData **data);
void DwFontDataAddRef(DwFontData *data);
void DwFontDataRelease(DwFontData *data);
/* a table's bytes inside the file, or nullptr. */
const BYTE *DwFontDataTable(const DwFontData *data, UINT32 tag, UINT32 *size);
/* sniffs the signature of a file's start: its type, the type of its faces and their number. */
BOOL DwSniff(const BYTE *bytes, UINT64 size, DWRITE_FONT_FILE_TYPE *file_type, DWRITE_FONT_FACE_TYPE *face_type,
             UINT32 *faces);

typedef struct DwAttributes {
    DWRITE_FONT_WEIGHT weight;
    DWRITE_FONT_STRETCH stretch;
    DWRITE_FONT_STYLE style;
    BOOL symbol;
    BOOL monospaced;
} DwAttributes;

void DwFontDataAttributes(const DwFontData *data, DwAttributes *attributes);
/* every name record with the id, as localized strings, or nullptr when there is none. */
DwStrings *DwFontDataNames(const DwFontData *data, UINT32 name_id);
/* design metrics of the face. */
void DwFontDataMetrics(const DwFontData *data, DWRITE_FONT_METRICS1 *metrics);
void DwFontDataGlyphMetrics(const DwFontData *data, UINT16 glyph, DWRITE_GLYPH_METRICS *metrics);
/* the unicode coverage of the font's cmap, as ranges; returns how many there are. */
UINT32 DwFontDataRanges(const DwFontData *data, DWRITE_UNICODE_RANGE *ranges, UINT32 max);
BOOL DwFontDataHasCharacter(const DwFontData *data, UINT32 code_point);

/* ------------------------------------------------------------------ collections */

typedef struct DwEntry {
    IDWriteFontFile *file;
    UINT32 face_index;
    DWRITE_FONT_FACE_TYPE face_type;
    DwAttributes attributes;
    DWRITE_FONT_SIMULATIONS simulations;
    /* the win32 names of the face, and the names inside the family. */
    DwStrings *face_names;
    DwStrings *win32_family;
    DwStrings *win32_subfamily;
    BOOL gdi_family; /* listed in a family made of the face's win32 family name */
    DWRITE_FONT_METRICS1 metrics;
} DwEntry;

typedef struct DwFamily {
    DwStrings *names;
    DwEntry *entries;
    UINT32 count;
    UINT32 capacity;
} DwFamily;

typedef struct DwCollection {
    const IDWriteFontCollectionVtbl *lpVtbl;
    LONG refs;
    DwFactory *factory;
    DwFamily *families;
    UINT32 count;
    SRWLOCK lock;
} DwCollection;

HRESULT DwCollectionFromFiles(DwFactory *factory, IDWriteFontFile *const *files, UINT32 count,
                              IDWriteFontFileEnumerator *enumerator, DwCollection **collection);
HRESULT DwSystemCollection(DwFactory *factory, IDWriteFontCollection **collection);
HRESULT DwFamilyCreate(DwCollection *collection, UINT32 index, IDWriteFontFamily **family);
HRESULT DwFontCreate(DwCollection *collection, UINT32 family, UINT32 entry, IDWriteFont **font);
/* the entry of the family that fits the request best, and the order they all fit in. */
UINT32 DwBestEntry(const DwFamily *family, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch,
                   DWRITE_FONT_STYLE style);
void DwSortEntries(const DwFamily *family, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch,
                   DWRITE_FONT_STYLE style, UINT32 *order);
/* the family named, case insensitive, in any language. */
BOOL DwFindFamily(const DwCollection *collection, const WCHAR *name, UINT32 *index);
/* the font of a request in one family of a collection, or the closest. */
HRESULT DwCreateFontByName(DwCollection *collection, const WCHAR *name, DWRITE_FONT_WEIGHT weight,
                           DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style, IDWriteFont **font);
/* the collection and entry behind one of our fonts, false for a font of another implementation. */
BOOL DwFontEntryOf(IDWriteFont *font, DwCollection **collection, const DwEntry **entry);
/* the entry whose file and face index are the face's. */
BOOL DwFindEntry(const DwCollection *collection, IDWriteFontFace *face, UINT32 *family, UINT32 *entry);

/* ------------------------------------------------------------------ faces */

typedef struct DwFace {
    const IDWriteFontFace2Vtbl *lpVtbl;
    LONG refs;
    DwFactory *factory;
    DwFontData *data;
    IDWriteFontFile *file;
    DWRITE_FONT_FACE_TYPE type;
    DWRITE_FONT_SIMULATIONS simulations;
    UINT32 index;
    DWRITE_FONT_METRICS1 metrics;
} DwFace;

HRESULT DwFaceCreate(DwFactory *factory, IDWriteFontFile *file, DWRITE_FONT_FACE_TYPE type, UINT32 index,
                     DWRITE_FONT_SIMULATIONS simulations, IDWriteFontFace **face);
/* the face behind an interface of ours, or nullptr for another implementation. */
DwFace *DwFaceFrom(IDWriteFontFace *face);

/* ------------------------------------------------------------------ rendering */

typedef struct DwRenderingParams {
    const IDWriteRenderingParams2Vtbl *lpVtbl;
    LONG refs;
    FLOAT gamma, enhanced_contrast, grayscale_contrast, cleartype_level;
    DWRITE_PIXEL_GEOMETRY geometry;
    DWRITE_RENDERING_MODE mode;
    DWRITE_GRID_FIT_MODE grid_fit;
} DwRenderingParams;

HRESULT DwRenderingParamsCreate(FLOAT gamma, FLOAT contrast, FLOAT grayscale_contrast, FLOAT cleartype_level,
                                DWRITE_PIXEL_GEOMETRY geometry, DWRITE_RENDERING_MODE mode,
                                DWRITE_GRID_FIT_MODE grid_fit, DwRenderingParams **params);
HRESULT DwGlyphRunAnalysisCreate(const DWRITE_GLYPH_RUN *run, FLOAT pixels_per_dip, const DWRITE_MATRIX *transform,
                                 DWRITE_RENDERING_MODE mode, DWRITE_MEASURING_MODE measuring,
                                 DWRITE_TEXT_ANTIALIAS_MODE antialias, FLOAT origin_x, FLOAT origin_y,
                                 IDWriteGlyphRunAnalysis **analysis);
HRESULT DwGlyphRunOutline(DwFace *face, FLOAT em_size, const UINT16 *glyphs, const FLOAT *advances,
                          const DWRITE_GLYPH_OFFSET *offsets, UINT32 count, BOOL sideways, BOOL rtl,
                          IDWriteGeometrySink *sink);

/* ------------------------------------------------------------------ the factory */

struct DwFactory {
    const IDWriteFactory2Vtbl *lpVtbl;
    LONG refs;
    BOOL shared;
    SRWLOCK lock;
    IDWriteFontCollection *system;
    IDWriteFontFallback *fallback;
    IDWriteGdiInterop *interop;
    /* registered loaders, kept as plain lists of interface pointers. */
    IDWriteFontFileLoader **file_loaders;
    UINT32 file_loader_count;
    IDWriteFontCollectionLoader **collection_loaders;
    UINT32 collection_loader_count;
};

DwFactory *DwFactoryShared(void);
HRESULT DwFallbackCreate(DwFactory *factory, IDWriteFontFallback **fallback);
HRESULT DwInteropCreate(DwFactory *factory, IDWriteGdiInterop **interop);
/* the collection of a request, the factory's system one when nullptr. */
DwCollection *DwCollectionOf(DwFactory *factory, IDWriteFontCollection *collection);

#endif
