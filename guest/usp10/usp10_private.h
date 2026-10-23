/* shared by the parts of usp10: the font cache and memory. */
#ifndef USP10_PRIVATE_H
#define USP10_PRIVATE_H

#define MIME_BUILD_USP10
#include <mime/nt.h>
#include <mime/ntrtl.h>
#include <mime/win32/winbase.h>
#include <mime/win32/usp10.h>

/* indexes of the script properties. */
enum {
    USP_UNDEFINED = SCRIPT_UNDEFINED,
    USP_LATIN = 1,
    USP_ARABIC = 2,
    USP_HEBREW = 3,
    USP_COMPLEX = 4,
    USP_DIGIT = 5,
    USP_SCRIPT_COUNT,
};

typedef struct USP_TABLE {
    BYTE *Data;
    DWORD Size;
} USP_TABLE;

typedef struct USP_CACHE {
    DWORD Magic;
    BOOL Real; /* glyph ids from gdi32, else characters. */
    TEXTMETRICW Metrics;
    WORD *Glyphs[256]; /* glyph id plus one, by page of 256 characters. */
    ABC *Widths[256];  /* by glyph id. */
    BYTE *Known[256];
    /* opentype layout tables, read on first use. */
    BOOL TablesRead;
    USP_TABLE Gsub, Gpos;
    UINT UnitsPerEm;
} USP_CACHE;

void *UspAlloc(SIZE_T size);
void UspFree(void *p);
HRESULT UspGetCache(HDC dc, SCRIPT_CACHE *cache, USP_CACHE **out);
OPENTYPE_TAG UspScriptTag(int script);

#endif
