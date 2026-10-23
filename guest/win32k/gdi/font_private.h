/* font registry internals shared by the font engine and the font services. */
#ifndef MIME_WIN32K_FONT_PRIVATE_H
#define MIME_WIN32K_FONT_PRIVATE_H

#include "../win32k.h"
#include "ttf/ttf.h"

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
} FONT_FILE;

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

#endif
