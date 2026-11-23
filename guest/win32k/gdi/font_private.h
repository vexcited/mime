/* font registry internals shared by the font engine and the font services. */
#ifndef MIME_WIN32K_FONT_PRIVATE_H
#define MIME_WIN32K_FONT_PRIVATE_H

#include "../win32k.h"
#include "ttf/ttf.h"
#include <mime/fonts.h>

typedef struct FONT_FILE {
    WCHAR Path[MAX_PATH]; /* nt path, empty for memory fonts. */
    PUCHAR Data;
    SIZE_T Size;
    BOOLEAN Failed;
    BOOLEAN Memory;   /* AddFontMemResourceEx data, owned by the file. */
    BOOLEAN Removed;  /* unregistered, but faces stay alive for realized instances. */
    BOOLEAN Hidden;   /* not enumerable. */
    HANDLE OwnerPid;  /* private to this process, NULL when visible to everyone. */
    ULONG FirstFace;
    ULONG FaceCount;
    ULONG Loads;      /* AddFontResource calls a remove has not answered yet, 0 for a font of the start. */
} FONT_FILE;

/* the header of a FNT raster font, fontfnt.c. */
typedef struct FNT_INFO {
    USHORT Version;
    USHORT Points;
    USHORT VertRes;
    USHORT HorizRes;
    USHORT Ascent;
    USHORT InternalLeading;
    USHORT ExternalLeading;
    USHORT Weight;
    USHORT PixWidth; /* 0 for a font of variable pitch. */
    USHORT PixHeight;
    USHORT AvgWidth;
    USHORT MaxWidth;
    UCHAR Italic;
    UCHAR Underline;
    UCHAR StrikeOut;
    UCHAR CharSet;
    UCHAR PitchAndFamily;
    UCHAR FirstChar;
    UCHAR LastChar;
    UCHAR DefaultChar; /* from the first character. */
    UCHAR BreakChar;
    ULONG Table; /* where the character table starts. */
    ULONG Size;
    char Face[LF_FACESIZE];
} FNT_INFO;

typedef struct FONT_FACE {
    FONT_FILE *File;
    ULONG FaceIndex;
    ULONG DirOffset; /* table directory offset in the file. */
    WCHAR Family[LF_FACESIZE];
    WCHAR TypoFamily[LF_FACESIZE]; /* name id 16 when present, for weight variants. */
    WCHAR FullName[LF_FULLFACESIZE];
    WCHAR Style[LF_FACESIZE];
    LONG Weight;
    BOOLEAN Italic;
    BOOLEAN Fixed;
    BOOLEAN Symbol;
    BOOLEAN Loaded;
    USHORT UnitsPerEm;
    LONG WinAscent;
    LONG WinDescent;
    LONG AvgWidth;
    LONG MaxAdvance;
    LONG LineGap; /* hhea line gap plus the hhea vs win metrics difference, like windows computes leading. */
    UCHAR PitchFamily; /* FF_* guessed from the panose classification. */
    FONTSIGNATURE Signature;
    MimeTtfFont Ttf;
    MimeTtfMetrics Metrics;
    WCRANGE *Ranges; /* GetFontUnicodeRanges cache. */
    ULONG RangeCount;
    ULONG GlyphsSupported;
    BOOLEAN CharsKnown;
    WCHAR FirstChar; /* the characters of the text metrics, see face_chars. */
    WCHAR LastChar;
    WCHAR DefaultChar;
    WCHAR BreakChar;
    UCHAR AnsiChars[4]; /* first, last, default and break, for TEXTMETRICA and the TMDIFF of the services. */
    BOOLEAN Raster;     /* a FNT font of a .fon file, at DirOffset in it, with no TrueType data. */
    BOOLEAN Signed;     /* has a DSIG table, which windows takes for an OpenType font. */
    SHORT XMin, XMax;   /* the bounding box of the head table. */
    WCHAR CmapFirst, CmapLast; /* the characters the unicode cmap starts and ends with, 0 for none. */
    SHORT EnumAscent, EnumDescent; /* of the VDMX table at the 32 pixels of an enumeration, -1 without one. */
    FNT_INFO Fnt;
} FONT_FACE;

extern const MimeAllocator FontAllocator;

FONT_FACE *FontMatchFace(const LOGFONTW *lf);
BOOLEAN FontLoadFace(FONT_FACE *f);
/* loads only the asked range from disk when the file is not loaded. tag 0 means the whole face. */
BOOLEAN FontReadTable(FONT_FACE *f, ULONG tag, ULONG offset, PVOID buffer, ULONG size, ULONG *table_size);
ULONG FontRegisterFile(PCWSTR path, HANDLE owner, BOOLEAN hidden);
ULONG FontRegisterMemory(PVOID data, SIZE_T size, HANDLE owner, FONT_FILE **file);
ULONG FontFaceCount(void);
FONT_FACE *FontFaceAt(ULONG index);
BOOLEAN FontFaceVisible(FONT_FACE *f);
ULONG FontFaceId(FONT_FACE *f);
const UCHAR *FontAnsiChars(FONT_INSTANCE *fi);
/* the code page of a charset, CP_SYMBOL for symbol fonts. */
UINT FontCharSetCodePage(UCHAR charset);
/* the faces windows lists under FontLink\SystemLink. */
BOOLEAN FontHasSystemLink(FONT_FACE *f);
BOOLEAN FontCharsetAlias(PCWSTR family, ULONG index, PCWSTR *name, UCHAR *charset);

/* fontfnt.c. */
ULONG FntList(const UCHAR *data, SIZE_T size, ULONG *offsets, ULONG *sizes, ULONG max);
BOOLEAN FntRead(const UCHAR *fnt, ULONG size, FNT_INFO *info);
BOOLEAN FntGlyph(const UCHAR *fnt, const FNT_INFO *info, ULONG code, ULONG *width, ULONG *bits);
BOOLEAN FntPixel(const UCHAR *fnt, const FNT_INFO *info, ULONG bits, ULONG x, ULONG y);
WCHAR FntToUnicode(UINT codepage, UCHAR c);
int FntFromUnicode(UINT codepage, WCHAR c);

#endif
