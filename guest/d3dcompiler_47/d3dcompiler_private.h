/*
 * d3dcompiler_47.dll: blobs, dxbc containers, reflection and disassembly
 * of shader model 4 and 5. there is no hlsl compiler: compiling fails like
 * a shader with errors.
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

void *(memcpy)(void *dst, const void *src, size_t n);
void *(memset)(void *dst, int c, size_t n);
int (memcmp)(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int _stricmp(const char *a, const char *b);

#define DC_TAG(a, b, c, d) DXBC_FOURCC(a, b, c, d)

void *DcAlloc(SIZE_T size);
void DcFree(void *p);

/* the bytes are allocated with the blob, like D3DCreateBlob does. */
HRESULT DcCreateBlob(SIZE_T size, ID3DBlob **out);
/* the blob includes the terminating nul. */
HRESULT DcTextBlob(const char *text, SIZE_T length, ID3DBlob **out);

typedef struct DcChunk {
    UINT tag;
    UINT size;
    const BYTE *data;
} DcChunk;

typedef struct DcContainer {
    const BYTE *base;
    UINT size;
    UINT count;
    const UINT *offsets;
} DcContainer;

/* D3DERR_INVALIDCALL for what is no container, E_FAIL for a wrong checksum. */
HRESULT DcOpenContainer(const void *data, SIZE_T size, DcContainer *out);
/* -1 when no chunk has the tag. */
int DcFindChunk(const DcContainer *c, UINT tag);
DcChunk DcGetChunk(const DcContainer *c, UINT index);

typedef struct DcBuilder {
    DcChunk *chunks;
    UINT count;
    UINT capacity;
    UINT size;
    HRESULT error;
} DcBuilder;

void DcBuilderInit(DcBuilder *b);
void DcBuilderAdd(DcBuilder *b, UINT tag, const void *data, UINT size);
/* adds the checksum and frees the builder. */
HRESULT DcBuilderFinish(DcBuilder *b, ID3DBlob **out);
void DcBuilderFree(DcBuilder *b);

/* declaration opcodes and the phase markers. */
BOOL DcIsDeclaration(UINT opcode);

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
/* the column after the last newline. */
UINT DcTextColumn(const DcText *t);
void DcTextAlign(DcText *t, UINT column);
void DcTextFree(DcText *t);

typedef struct DcReflection DcReflection;

/* D3D12_SHADER_INPUT_BIND_DESC extends the direct3d 11 one, and reflections made for 12 return it. */
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

/* E_INVALIDARG when the container's chunks are malformed. */
HRESULT DcReflectionCreate(const DcContainer *c, BOOL d3d12, DcReflection **out);
ID3D11ShaderReflection *DcReflectionInterface(DcReflection *r);

#endif
