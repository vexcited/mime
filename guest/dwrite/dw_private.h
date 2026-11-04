/*
 * dwrite.dll: a factory makes font files from paths or loaders, faces from
 * files, and collections of families. the system collection is read from
 * the fonts directory. glyphs come from the truetype library the kernel's
 * font engine uses.
 */
#ifndef DW_PRIVATE_H
#define DW_PRIVATE_H

#define MIME_BUILD_DWRITE
#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>
#include <mime/win32/dwrite.h>
#include <mime/fonts.h>
#include <ttf/ttf.h>
#include <ttf/ttf_internal.h>

#define VTBL_FN(f) ((void *)(f))
#define DW_TAG(a, b, c, d) (((UINT32)(a) << 24) | ((UINT32)(b) << 16) | ((UINT32)(c) << 8) | (UINT32)(d))
/* tags as the interfaces take them, first letter in the low byte. */
#define DW_TAG_LE(a, b, c, d) (((UINT32)(d) << 24) | ((UINT32)(c) << 16) | ((UINT32)(b) << 8) | (UINT32)(a))

/* ------------------------------------------------------------------ memory and text */

void *DwAlloc(SIZE_T size);
void *DwAllocZero(SIZE_T size);
void DwFree(void *p);
WCHAR *DwStrDup(const WCHAR *s);
WCHAR *DwStrDupN(const WCHAR *s, UINT32 length);
void DwLower(WCHAR *s);
/* case insensitive, ordering names the way the collection sorts them. */
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
/* copies locale and text, unless the locale already has an entry. */
BOOL DwStringsAdd(DwStrings *strings, const WCHAR *locale, UINT32 locale_length, const WCHAR *text, UINT32 length);
UINT32 DwStringsCount(const DwStrings *strings);
const DwStringEntry *DwStringsAt(const DwStrings *strings, UINT32 index);
/* the locale's text, else the english one, else the first. */
const WCHAR *DwStringsBest(const DwStrings *strings, const WCHAR *locale);
IDWriteLocalizedStrings *DwStringsInterface(DwStrings *strings);
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
/* the shared loader for files on disk, keyed by a write time and a path. */
IDWriteLocalFontFileLoader *DwLocalLoader(void);
/* the path in a local loader key, or nullptr. */
const WCHAR *DwLocalKeyPath(const void *key, UINT32 key_size);
HRESULT DwFontFileOpen(IDWriteFontFile *file, IDWriteFontFileStream **stream);

/* ------------------------------------------------------------------ parsed fonts */

/* what a face's tables say, shared by every face and font of one file and index. */
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
    /* interpreters of the glyph programs, one per size and kind of rendering. */
    CRITICAL_SECTION hint_lock;
    BOOL hint_lock_ready;
    UINT32 hinter_count;
    struct { UINT32 key; MimeTtfHinter *hinter; } hinters[48];
} DwFontData;

/* the weight a subfamily name like Light or Semibold Italic gives, 400 for other names. */
UINT32 DwWeightOfName(const WCHAR *name);
/* the interpreter for a size, null when the face cannot be hinted. flags are the MIME_TTF_HINT ones. */
MimeTtfHinter *DwFontDataHinter(DwFontData *data, int ppem, UINT32 flags);
/* the advance of a glyph in whole pixels the way GDI measures, or the natural rounding of the linear width. */
int DwGdiAdvancePixels(DwFontData *data, UINT16 glyph, int ppem, BOOL natural);


/* collections count their faces, other files have one. */
HRESULT DwFontDataOpen(IDWriteFontFile *file, UINT32 face_index, DwFontData **data);
void DwFontDataAddRef(DwFontData *data);
void DwFontDataRelease(DwFontData *data);
/* a table's bytes inside the file, or nullptr. */
const BYTE *DwFontDataTable(const DwFontData *data, UINT32 tag, UINT32 *size);
/* reads the file type, face type and face count from the signature at the file's start. */
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
/* every name record with the id, or nullptr when there is none. */
DwStrings *DwFontDataNames(const DwFontData *data, UINT32 name_id);
void DwFontDataMetrics(const DwFontData *data, DWRITE_FONT_METRICS1 *metrics);
void DwSimulateMetrics(DWRITE_FONT_METRICS1 *metrics, DWRITE_FONT_SIMULATIONS simulations);
void DwFontDataGlyphMetrics(const DwFontData *data, UINT16 glyph, DWRITE_GLYPH_METRICS *metrics);
/* the cmap's unicode coverage as ranges; returns how many there are. */
UINT32 DwFontDataRanges(const DwFontData *data, DWRITE_UNICODE_RANGE *ranges, UINT32 max);
BOOL DwFontDataHasCharacter(const DwFontData *data, UINT32 code_point);

/* ------------------------------------------------------------------ variable fonts */

#define DW_MAX_AXES 16

typedef struct DwAxis {
    UINT32 tag; /* first letter in the low byte, as the interfaces take tags. */
    FLOAT min, def, max;
    BOOL hidden;
    UINT32 name_id;
} DwAxis;

/* the axes of the fvar table, 0 for a face that does not vary. */
UINT32 DwFontDataAxes(const DwFontData *data, DwAxis *axes, UINT32 max);
/* the weight, width, slant and italic axis values a face's attributes stand for. */
UINT32 DwAttributeAxes(const DwAttributes *attributes, DWRITE_FONT_AXIS_VALUE *values);
/* the attributes closest to axis values, starting from the given ones. */
void DwAxesToAttributes(const DWRITE_FONT_AXIS_VALUE *values, UINT32 count, DWRITE_FONT_WEIGHT *weight,
                        DWRITE_FONT_STRETCH *stretch, DWRITE_FONT_STYLE *style);

/* ------------------------------------------------------------------ collections */

typedef struct DwEntry {
    IDWriteFontFile *file;
    UINT32 face_index;
    DWRITE_FONT_FACE_TYPE face_type;
    DwAttributes attributes;
    DWRITE_FONT_SIMULATIONS simulations;
    /* the face's win32 names, and its names inside the family. */
    DwStrings *face_names;
    DwStrings *win32_family;
    DwStrings *win32_subfamily;
    BOOL gdi_family; /* listed in a family named after its win32 family name. */
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
    UINT32 public_count; /* the families listed, before those only win32 names find. */
    SRWLOCK lock;
    DWRITE_FONT_FAMILY_MODEL model;
    IDWriteFontSet4 *set; /* the set it was made of, or the one made of it when asked. */
} DwCollection;

HRESULT DwCollectionFromFiles(DwFactory *factory, IDWriteFontFile *const *files, UINT32 count,
                              IDWriteFontFileEnumerator *enumerator, DwCollection **collection);
HRESULT DwSystemCollection(DwFactory *factory, IDWriteFontCollection **collection);
HRESULT DwFamilyCreate(DwCollection *collection, UINT32 index, IDWriteFontFamily **family);
HRESULT DwFontCreate(DwCollection *collection, UINT32 family, UINT32 entry, IDWriteFont **font);
/* the entry that fits the request best; DwSortEntries gives the order they all fit in. */
UINT32 DwBestEntry(const DwFamily *family, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch,
                   DWRITE_FONT_STYLE style);
void DwSortEntries(const DwFamily *family, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch,
                   DWRITE_FONT_STYLE style, UINT32 *order);
/* case insensitive, in any language. */
BOOL DwFindFamily(const DwCollection *collection, const WCHAR *name, UINT32 *index);
/* the same without the families that only exist for the names gdi gives to faces. */
BOOL DwFindFamilyExact(const DwCollection *collection, const WCHAR *name, UINT32 *index);
/* the font closest to the request in the named family. */
HRESULT DwCreateFontByName(DwCollection *collection, const WCHAR *name, DWRITE_FONT_WEIGHT weight,
                           DWRITE_FONT_STRETCH stretch, DWRITE_FONT_STYLE style, IDWriteFont **font);
/* FALSE for a font of another implementation. */
BOOL DwFontEntryOf(IDWriteFont *font, DwCollection **collection, const DwEntry **entry);
/* matches the face's file and face index. */
BOOL DwFindEntry(const DwCollection *collection, IDWriteFontFace *face, UINT32 *family, UINT32 *entry);
/* orders two faces by how well they fit a request, the way a family's fonts are listed. */
int DwCompareFit(const DwAttributes *a, DWRITE_FONT_SIMULATIONS a_simulations, const DwAttributes *b,
                 DWRITE_FONT_SIMULATIONS b_simulations, DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STRETCH stretch,
                 DWRITE_FONT_STYLE style);
/* the family and face names of a face in a family model, new references, and the weight a legacy name gives. */
void DwModelNames(const DwFontData *data, DWRITE_FONT_FAMILY_MODEL model, DwAttributes *attributes,
                  DwStrings **family, DwStrings **face);
/* the strings behind an informational string id, or nullptr. */
DwStrings *DwInformationalStrings(const DwFontData *data, DWRITE_INFORMATIONAL_STRING_ID id);
/* TRUE when both files have the same loader and key. */
BOOL DwSameFile(IDWriteFontFile *a, IDWriteFontFile *b);

/* ------------------------------------------------------------------ font sets and references */

/* what names a font of a set: its file, face, simulations and the axis values of its instance. */
typedef struct DwFontKey {
    IDWriteFontFile *file;
    UINT32 index;
    DWRITE_FONT_SIMULATIONS simulations;
    UINT32 axis_count;
    DWRITE_FONT_AXIS_VALUE axes[DW_MAX_AXES];
} DwFontKey;

typedef struct DwSetFont DwSetFont;

/* the key's file gets a reference of the object's own. */
HRESULT DwFaceReferenceCreate(DwFactory *factory, const DwFontKey *key, IDWriteFontFaceReference1 **reference);
/* reads the key of any implementation's reference, the file without a reference. */
BOOL DwFaceReferenceKey(IDWriteFontFaceReference *reference, DwFontKey *key);
HRESULT DwFontResourceCreate(DwFactory *factory, IDWriteFontFile *file, UINT32 index, IDWriteFontResource **resource);
/* a set of the fonts of keys, the properties read from the files. */
HRESULT DwFontSetFromKeys(DwFactory *factory, const DwFontKey *keys, UINT32 count, IDWriteFontSet4 **set);
HRESULT DwSystemFontSet(DwFactory *factory, IDWriteFontSet4 **set);
HRESULT DwFontSetBuilderCreate(DwFactory *factory, IDWriteFontSetBuilder2 **builder);
HRESULT DwInMemoryLoaderCreate(IDWriteInMemoryFontFileLoader **loader);
/* copies the pairs of locale and text, which may repeat locales. */
HRESULT DwStringListCreate(const WCHAR *const *locales, const WCHAR *const *texts, UINT32 count,
                           IDWriteStringList **list);
/* the fonts of one of Mime's sets, nullptr for a set of another implementation. */
BOOL DwFontSetFonts(IDWriteFontSet *set, DwSetFont *const **fonts, UINT32 *count);
const DwFontKey *DwSetFontKey(const DwSetFont *font);
/* a property the set's builder was given for the font, or nullptr. */
DwStrings *DwSetFontCustom(DwSetFont *font, DWRITE_FONT_PROPERTY_ID id);
HRESULT DwCollectionFromSet(DwFactory *factory, IDWriteFontSet *set, DWRITE_FONT_FAMILY_MODEL model,
                            DwCollection **collection);
HRESULT DwSystemCollectionModel(DwFactory *factory, DWRITE_FONT_FAMILY_MODEL model, IDWriteFontCollection **collection);
/* the font set of a collection, one family of it or the fonts in order. */
HRESULT DwCollectionSet(DwCollection *collection, UINT32 family, const UINT32 *order, UINT32 count,
                        IDWriteFontSet4 **set);
HRESULT DwFontDownloadQueueCreate(IDWriteFontDownloadQueue **queue);
/* the font a woff file holds, as a stream of its own. */
HRESULT DwUnpackWoff(const void *data, UINT32 size, IDWriteFontFileStream **stream);
/* a stream over bytes, kept alive by owner or copied when owner is nullptr. */
HRESULT DwMemoryStreamCreate(const void *data, UINT64 size, IUnknown *owner, BOOL copy, IDWriteFontFileStream **stream);

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
    /* the instance of a variable face: one value for each fvar axis. */
    UINT32 axis_count;
    DWRITE_FONT_AXIS_VALUE axes[DW_MAX_AXES];
} DwFace;

/* GDI's advance of a glyph in design units. */
int DwGdiAdvanceDesign(const DwFace *f, UINT16 glyph, int ppem, BOOL natural);
HRESULT DwFaceCreate(DwFactory *factory, IDWriteFontFile *file, DWRITE_FONT_FACE_TYPE type, UINT32 index,
                     DWRITE_FONT_SIMULATIONS simulations, IDWriteFontFace **face);
/* a face of an instance: the values given replace the defaults of their axes. */
HRESULT DwFaceCreateWithAxes(DwFactory *factory, IDWriteFontFile *file, UINT32 index, DWRITE_FONT_SIMULATIONS simulations,
                             const DWRITE_FONT_AXIS_VALUE *axes, UINT32 axis_count, IDWriteFontFace **face);
/* nullptr for a face of another implementation. */
DwFace *DwFaceFrom(IDWriteFontFace *face);

/* ------------------------------------------------------------------ rendering */

typedef struct DwRenderingParams {
    const IDWriteRenderingParams3Vtbl *lpVtbl;
    LONG refs;
    FLOAT gamma, enhanced_contrast, grayscale_contrast, cleartype_level;
    DWRITE_PIXEL_GEOMETRY geometry;
    DWRITE_RENDERING_MODE mode;
    DWRITE_RENDERING_MODE1 mode1;
    DWRITE_GRID_FIT_MODE grid_fit;
} DwRenderingParams;

/* mode is a DWRITE_RENDERING_MODE1, of which the first seven are those of DWRITE_RENDERING_MODE. */
HRESULT DwRenderingParamsCreate(FLOAT gamma, FLOAT contrast, FLOAT grayscale_contrast, FLOAT cleartype_level,
                                DWRITE_PIXEL_GEOMETRY geometry, DWRITE_RENDERING_MODE1 mode,
                                DWRITE_GRID_FIT_MODE grid_fit, DwRenderingParams **params);
HRESULT DwGlyphRunAnalysisCreate(const DWRITE_GLYPH_RUN *run, FLOAT pixels_per_dip, const DWRITE_MATRIX *transform,
                                 DWRITE_RENDERING_MODE mode, DWRITE_MEASURING_MODE measuring,
                                 DWRITE_TEXT_ANTIALIAS_MODE antialias, FLOAT origin_x, FLOAT origin_y,
                                 IDWriteGlyphRunAnalysis **analysis);
HRESULT DwGlyphRunOutline(DwFace *face, FLOAT em_size, const UINT16 *glyphs, const FLOAT *advances,
                          const DWRITE_GLYPH_OFFSET *offsets, UINT32 count, BOOL sideways, BOOL rtl,
                          IDWriteGeometrySink *sink);

/* ------------------------------------------------------------------ factory */

struct DwFactory {
    const IDWriteFactory2Vtbl *lpVtbl;
    LONG refs;
    BOOL shared;
    SRWLOCK lock;
    IDWriteFontCollection *system;
    IDWriteFontCollection *system_typographic;
    IDWriteFontSet4 *system_set;
    IDWriteFontFallback *fallback;
    IDWriteGdiInterop *interop;
    /* registered loaders, as plain arrays of interface pointers. */
    IDWriteFontFileLoader **file_loaders;
    UINT32 file_loader_count;
    IDWriteFontCollectionLoader **collection_loaders;
    UINT32 collection_loader_count;
};

DwFactory *DwFactoryShared(void);
/* TRUE for the local loader and the loaders registered with the factory. */
BOOL DwLoaderRegistered(DwFactory *factory, IDWriteFontFileLoader *loader);
HRESULT DwFallbackCreate(DwFactory *factory, IDWriteFontFallback **fallback);
HRESULT DwInteropCreate(DwFactory *factory, IDWriteGdiInterop **interop);
/* the factory's system collection when collection is nullptr. */
DwCollection *DwCollectionOf(DwFactory *factory, IDWriteFontCollection *collection);

#endif
