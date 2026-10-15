/*
 * d3dcompiler_47.dll: blobs, the dxbc container and its parts,
 * reflection and disassembly of shader model 4 and 5 bytecode. there is no
 * hlsl compiler: compiling fails the way a shader with errors does.
 */
#ifndef D3DCOMPILER_PRIVATE_H
#define D3DCOMPILER_PRIVATE_H

#define MIME_BUILD_D3DCOMPILER
#include <stdarg.h>
#include <mime/win32/winbase.h>
#include <mime/win32/winnls.h>
#include <mime/win32/d3dcompiler.h>
#include "../lib/dxbc/dxbc.h"
#include "../lib/dxbc/tokens.h"

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int _stricmp(const char *a, const char *b);

#define DC_TAG(a, b, c, d) DXBC_FOURCC(a, b, c, d)

void *DcAlloc(SIZE_T size);
void DcFree(void *p);

/* a blob whose bytes are allocated with it, as D3DCreateBlob makes them. */
HRESULT DcCreateBlob(SIZE_T size, ID3DBlob **out);
/* a blob holding the text and its nul. */
HRESULT DcTextBlob(const char *text, SIZE_T length, ID3DBlob **out);

/* ------------------------------------------------------------------ containers */

typedef struct DcChunk {
    UINT tag;
    UINT size;
    const BYTE *data;
} DcChunk;

/* a container checked as d3dcompiler checks it before reading it. */
typedef struct DcContainer {
    const BYTE *base;
    UINT size;
    UINT count;
    const UINT *offsets;
} DcContainer;

/* D3DERR_INVALIDCALL for what is no container, E_FAIL for a wrong checksum. */
HRESULT DcOpenContainer(const void *data, SIZE_T size, DcContainer *out);
/* the index of the first chunk with the tag, or -1. */
int DcFindChunk(const DcContainer *c, UINT tag);
DcChunk DcGetChunk(const DcContainer *c, UINT index);

/* a container built chunk by chunk. */
typedef struct DcBuilder {
    DcChunk *chunks;
    UINT count;
    UINT capacity;
    UINT size;
    HRESULT error;
} DcBuilder;

void DcBuilderInit(DcBuilder *b);
void DcBuilderAdd(DcBuilder *b, UINT tag, const void *data, UINT size);
/* the container with its checksum, as a blob; frees the builder. */
HRESULT DcBuilderFinish(DcBuilder *b, ID3DBlob **out);
void DcBuilderFree(DcBuilder *b);

/* declarations, as the instruction table of D3D10ShaderBinary marks them, and the phase markers. */
BOOL DcIsDeclaration(UINT opcode);

/* ------------------------------------------------------------------ text */

typedef struct DcText {
    char *buf;
    SIZE_T length;
    SIZE_T capacity;
    BOOL failed;
} DcText;

void DcTextAppend(DcText *t, const char *s, SIZE_T n);
void DcTextPuts(DcText *t, const char *s);
/* printf with %s %c %d %u %x %X %p, widths, '-' and '0', and %f %g of doubles. */
void DcTextPrintf(DcText *t, const char *format, ...);
void DcTextVprintf(DcText *t, const char *format, va_list ap);
/* the column of the end of the text, from its last newline. */
UINT DcTextColumn(const DcText *t);
void DcTextAlign(DcText *t, UINT column);
void DcTextFree(DcText *t);

/* ------------------------------------------------------------------ reflection */

typedef struct DcReflection DcReflection;

/* D3D12_SHADER_INPUT_BIND_DESC, which starts as the direct3d 11 one, as a reflection made for 12 returns it. */
typedef struct DcBindDesc {
    LPCSTR Name;
    UINT Type;
    UINT BindPoint;
    UINT BindCount;
    UINT uFlags;
    UINT ReturnType;
    UINT Dimension;
    UINT NumSamples;
    UINT Space;
    UINT uID;
} DcBindDesc;

/* the reflection of a container, as D3DReflect reads it; E_INVALIDARG when its chunks are malformed. */
HRESULT DcReflectionCreate(const DcContainer *c, BOOL d3d12, DcReflection **out);
ID3D11ShaderReflection *DcReflectionInterface(DcReflection *r);

#endif
