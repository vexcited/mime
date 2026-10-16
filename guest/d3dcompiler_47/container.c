/*
 * the dxbc container as d3dcompiler_47.dll checks and rebuilds it: the
 * parts D3DGetBlobPart hands out, the chunk D3DSetBlobPart replaces and
 * the chunks D3DStripShader drops. a container it builds is the header,
 * the chunk offsets and the chunks in order, with the checksum of
 * guest/lib/dxbc, the one windows' retail builds compute.
 */
#include "d3dcompiler_private.h"

static UINT read32(const BYTE *p) { return p[0] | p[1] << 8 | p[2] << 16 | (UINT)p[3] << 24; }

/*
 * the header's size must be the size given, its version 1, and its chunks
 * must follow the offsets back to back, as CDXBCParser::ReadDXBC requires.
 */
static BOOL valid_layout(const BYTE *b, UINT size)
{
    if (size < 32 || read32(b + 24) != size || read32(b) != DC_TAG('D', 'X', 'B', 'C') || read32(b + 20) != 1)
        return FALSE;
    UINT count = read32(b + 28);
    if (count > (size - 32) / 4)
        return FALSE;
    UINT64 next = 32 + (UINT64)count * 4;
    for (UINT i = 0; i < count; i++) {
        UINT offset = read32(b + 32 + i * 4);
        if (offset != next || (UINT64)offset + 8 > size)
            return FALSE;
        UINT64 end = (UINT64)offset + 8 + read32(b + offset + 4);
        /* windows lets the last byte of a chunk be one past the end. */
        if (end > (UINT64)size + 1)
            return FALSE;
        next = end;
    }
    return TRUE;
}

/* sixteen bytes of 1 stand for a checksum not to verify. */
static BOOL checksum_skipped(const BYTE *b)
{
    for (int i = 4; i < 20; i++)
        if (b[i] != 1)
            return FALSE;
    return TRUE;
}

HRESULT DcOpenContainer(const void *data, SIZE_T size, DcContainer *out)
{
    if (!data || size - 32 > 0x7FFFFFE0 || size > 0xFFFFFFFF)
        return D3DERR_INVALIDCALL;
    const BYTE *b = data;
    if (!valid_layout(b, (UINT)size))
        return D3DERR_INVALIDCALL;
    if (!checksum_skipped(b)) {
        UINT sum[4];
        dxbc_checksum(b, size, sum);
        if (memcmp(sum, b + 4, 16))
            return E_FAIL;
    }
    out->base = b;
    out->size = (UINT)size;
    out->count = read32(b + 28);
    out->offsets = (const UINT *)(b + 32);
    return S_OK;
}

int DcFindChunk(const DcContainer *c, UINT tag)
{
    for (UINT i = 0; i < c->count; i++)
        if (read32(c->base + read32((const BYTE *)&c->offsets[i])) == tag)
            return (int)i;
    return -1;
}

DcChunk DcGetChunk(const DcContainer *c, UINT index)
{
    const BYTE *p = c->base + read32((const BYTE *)&c->offsets[index]);
    DcChunk chunk = {read32(p), read32(p + 4), p + 8};
    return chunk;
}

/* ------------------------------------------------------------------ building */

void DcBuilderInit(DcBuilder *b)
{
    memset(b, 0, sizeof(*b));
    b->size = 32;
}

void DcBuilderFree(DcBuilder *b)
{
    DcFree(b->chunks);
    b->chunks = nullptr;
    b->count = b->capacity = 0;
}

void DcBuilderAdd(DcBuilder *b, UINT tag, const void *data, UINT size)
{
    if (FAILED(b->error))
        return;
    UINT64 grown = (UINT64)b->size + size + 12;
    if (grown > 0x80000000u) {
        b->error = E_FAIL;
        return;
    }
    if (b->count == b->capacity) {
        UINT capacity = b->capacity ? b->capacity * 2 : 16;
        DcChunk *chunks = DcAlloc(capacity * sizeof(*chunks));
        if (!chunks) {
            b->error = E_OUTOFMEMORY;
            return;
        }
        if (b->count)
            memcpy(chunks, b->chunks, b->count * sizeof(*chunks));
        DcFree(b->chunks);
        b->chunks = chunks;
        b->capacity = capacity;
    }
    DcChunk chunk = {tag, size, data};
    b->chunks[b->count++] = chunk;
    b->size = (UINT)grown;
}

static void put32(BYTE *p, UINT v) { p[0] = (BYTE)v, p[1] = (BYTE)(v >> 8), p[2] = (BYTE)(v >> 16), p[3] = (BYTE)(v >> 24); }

HRESULT DcBuilderFinish(DcBuilder *b, ID3DBlob **out)
{
    HRESULT hr = b->error;
    ID3DBlob *blob = nullptr;
    if (SUCCEEDED(hr))
        hr = DcCreateBlob(b->size, &blob);
    if (SUCCEEDED(hr)) {
        BYTE *p = blob->lpVtbl->GetBufferPointer(blob);
        put32(p, DC_TAG('D', 'X', 'B', 'C'));
        put32(p + 20, 1);
        put32(p + 24, b->size);
        put32(p + 28, b->count);
        UINT at = 32 + b->count * 4;
        for (UINT i = 0; i < b->count; i++) {
            put32(p + 32 + i * 4, at);
            put32(p + at, b->chunks[i].tag);
            put32(p + at + 4, b->chunks[i].size);
            if (b->chunks[i].size)
                memcpy(p + at + 8, b->chunks[i].data, b->chunks[i].size);
            at += 8 + b->chunks[i].size;
        }
        UINT sum[4];
        dxbc_checksum(p, b->size, sum);
        memcpy(p + 4, sum, 16);
        *out = blob;
    }
    DcBuilderFree(b);
    return hr;
}

/* ------------------------------------------------------------------ parts */

/* the newer variant of a signature chunk when the container has it. */
static UINT signature_variant(const DcContainer *c, UINT tag)
{
    static const UINT variants[][3] = {
        {DC_TAG('I', 'S', 'G', 'N'), DC_TAG('I', 'S', 'G', '1'), 0},
        {DC_TAG('O', 'S', 'G', 'N'), DC_TAG('O', 'S', 'G', '5'), DC_TAG('O', 'S', 'G', '1')},
        {DC_TAG('P', 'C', 'S', 'G'), DC_TAG('P', 'S', 'G', '1'), 0},
    };
    for (UINT i = 0; i < 3; i++) {
        if (variants[i][0] != tag)
            continue;
        for (UINT j = 1; j < 3; j++)
            if (variants[i][j] && DcFindChunk(c, variants[i][j]) >= 0)
                return variants[i][j];
    }
    return tag;
}

static HRESULT get_part(LPCVOID data, SIZE_T size, D3D_BLOB_PART part, ID3DBlob **out)
{
    if (!out)
        return D3DERR_INVALIDCALL;
    DcContainer c;
    HRESULT hr = DcOpenContainer(data, size, &c);
    if (FAILED(hr))
        return hr;
    UINT tags[3], count = 1;
    BOOL container = TRUE;
    switch (part) {
    case D3D_BLOB_INPUT_SIGNATURE_BLOB:
        tags[0] = DC_TAG('I', 'S', 'G', 'N');
        break;
    case D3D_BLOB_OUTPUT_SIGNATURE_BLOB:
        tags[0] = DC_TAG('O', 'S', 'G', 'N');
        break;
    case D3D_BLOB_INPUT_AND_OUTPUT_SIGNATURE_BLOB:
        tags[0] = DC_TAG('I', 'S', 'G', 'N');
        tags[1] = DC_TAG('O', 'S', 'G', 'N');
        count = 2;
        break;
    case D3D_BLOB_PATCH_CONSTANT_SIGNATURE_BLOB:
        tags[0] = DC_TAG('P', 'C', 'S', 'G');
        break;
    case D3D_BLOB_ALL_SIGNATURE_BLOB:
        tags[0] = DC_TAG('I', 'S', 'G', 'N');
        tags[1] = DC_TAG('O', 'S', 'G', 'N');
        tags[2] = DC_TAG('P', 'C', 'S', 'G');
        count = 3;
        break;
    case D3D_BLOB_ROOT_SIGNATURE:
        /* a container holding the root signature, not the chunk alone. */
        tags[0] = DC_TAG('R', 'T', 'S', '0');
        break;
    case D3D_BLOB_DEBUG_INFO:
        tags[0] = DC_TAG('S', 'D', 'B', 'G');
        container = FALSE;
        break;
    case D3D_BLOB_LEGACY_SHADER:
        tags[0] = DC_TAG('A', 'o', 'n', '9');
        container = FALSE;
        break;
    case D3D_BLOB_XNA_PREPASS_SHADER:
        tags[0] = DC_TAG('X', 'N', 'A', 'P');
        container = FALSE;
        break;
    case D3D_BLOB_XNA_SHADER:
        tags[0] = DC_TAG('X', 'N', 'A', 'S');
        container = FALSE;
        break;
    case D3D_BLOB_PDB:
        tags[0] = DC_TAG('S', 'P', 'D', 'B');
        container = FALSE;
        break;
    case D3D_BLOB_PRIVATE_DATA:
        tags[0] = DC_TAG('P', 'R', 'I', 'V');
        container = FALSE;
        break;
    case D3D_BLOB_DEBUG_NAME:
        tags[0] = DC_TAG('I', 'L', 'D', 'N');
        container = FALSE;
        break;
    case D3D_BLOB_TEST_ALTERNATE_SHADER:
        tags[0] = DC_TAG('S', 'M', 'I', 'D');
        container = FALSE;
        break;
    case D3D_BLOB_TEST_COMPILE_DETAILS:
        tags[0] = DC_TAG('S', 'D', 'T', 'L');
        container = FALSE;
        break;
    case D3D_BLOB_TEST_COMPILE_PERF:
        tags[0] = DC_TAG('P', 'E', 'R', 'F');
        container = FALSE;
        break;
    case D3D_BLOB_TEST_COMPILE_REPORT:
        tags[0] = DC_TAG('C', 'R', 'P', 'T');
        container = FALSE;
        break;
    default:
        return D3DERR_INVALIDCALL;
    }
    if (!container) {
        int index = DcFindChunk(&c, tags[0]);
        if (index < 0)
            return E_FAIL;
        DcChunk chunk = DcGetChunk(&c, index);
        hr = DcCreateBlob(chunk.size, out);
        if (SUCCEEDED(hr) && chunk.size)
            memcpy((*out)->lpVtbl->GetBufferPointer(*out), chunk.data, chunk.size);
        return hr;
    }
    DcBuilder b;
    DcBuilderInit(&b);
    for (UINT i = 0; i < count; i++) {
        int index = DcFindChunk(&c, signature_variant(&c, tags[i]));
        if (index < 0) {
            DcBuilderFree(&b);
            return E_FAIL;
        }
        DcChunk chunk = DcGetChunk(&c, index);
        DcBuilderAdd(&b, chunk.tag, chunk.data, chunk.size);
    }
    return DcBuilderFinish(&b, out);
}

D3DCOMPILERAPI HRESULT WINAPI D3DGetBlobPart(LPCVOID data, SIZE_T size, D3D_BLOB_PART part, UINT flags,
                                             ID3DBlob **out)
{
    if (flags)
        return D3DERR_INVALIDCALL;
    return get_part(data, size, part, out);
}

D3DCOMPILERAPI HRESULT WINAPI D3DGetInputSignatureBlob(LPCVOID data, SIZE_T size, ID3DBlob **out)
{
    return get_part(data, size, D3D_BLOB_INPUT_SIGNATURE_BLOB, out);
}

D3DCOMPILERAPI HRESULT WINAPI D3DGetOutputSignatureBlob(LPCVOID data, SIZE_T size, ID3DBlob **out)
{
    return get_part(data, size, D3D_BLOB_OUTPUT_SIGNATURE_BLOB, out);
}

D3DCOMPILERAPI HRESULT WINAPI D3DGetInputAndOutputSignatureBlob(LPCVOID data, SIZE_T size, ID3DBlob **out)
{
    return get_part(data, size, D3D_BLOB_INPUT_AND_OUTPUT_SIGNATURE_BLOB, out);
}

D3DCOMPILERAPI HRESULT WINAPI D3DGetDebugInfo(LPCVOID data, SIZE_T size, ID3DBlob **out)
{
    return get_part(data, size, D3D_BLOB_DEBUG_INFO, out);
}

D3DCOMPILERAPI HRESULT WINAPI D3DSetBlobPart(LPCVOID data, SIZE_T size, D3D_BLOB_PART part, UINT flags,
                                             LPCVOID part_data, SIZE_T part_size, ID3DBlob **out)
{
    if (flags || !part_data || part_size - 1 > 0xFFFFFFFE || !out)
        return D3DERR_INVALIDCALL;
    UINT tag;
    switch (part) {
    case D3D_BLOB_PRIVATE_DATA:
        tag = DC_TAG('P', 'R', 'I', 'V');
        break;
    case D3D_BLOB_ROOT_SIGNATURE:
        tag = DC_TAG('R', 'T', 'S', '0');
        break;
    case D3D_BLOB_DEBUG_NAME:
        tag = DC_TAG('I', 'L', 'D', 'N');
        break;
    default:
        return D3DERR_INVALIDCALL;
    }
    DcContainer c;
    HRESULT hr = DcOpenContainer(data, size, &c);
    if (FAILED(hr))
        return hr;
    DcBuilder b;
    DcBuilderInit(&b);
    for (UINT i = 0; i < c.count; i++) {
        DcChunk chunk = DcGetChunk(&c, i);
        if (chunk.tag != tag)
            DcBuilderAdd(&b, chunk.tag, chunk.data, chunk.size);
    }
    const void *bytes = part_data;
    UINT length = (UINT)part_size;
    if (part == D3D_BLOB_ROOT_SIGNATURE) {
        /* the root signature comes as a container of its own. */
        DcContainer source;
        hr = DcOpenContainer(part_data, part_size, &source);
        int index = SUCCEEDED(hr) ? DcFindChunk(&source, tag) : -1;
        if (SUCCEEDED(hr) && index < 0)
            hr = E_FAIL;
        if (FAILED(hr)) {
            DcBuilderFree(&b);
            return hr;
        }
        DcChunk chunk = DcGetChunk(&source, index);
        bytes = chunk.data;
        length = chunk.size;
    }
    DcBuilderAdd(&b, tag, bytes, length);
    return DcBuilderFinish(&b, out);
}

/* libraries keep their reflection when it is stripped. */
static BOOL is_library(const DcContainer *c)
{
    if (DcFindChunk(c, DC_TAG('L', 'I', 'B', 'H')) >= 0)
        return TRUE;
    int index = DcFindChunk(c, DC_TAG('S', 'H', 'E', 'X'));
    if (index < 0)
        index = DcFindChunk(c, DC_TAG('S', 'H', 'D', 'R'));
    if (index < 0)
        return FALSE;
    DcChunk code = DcGetChunk(c, index);
    return code.size >= 4 && (read32(code.data) >> 16) == 0xFFF0;
}

D3DCOMPILERAPI HRESULT WINAPI D3DStripShader(LPCVOID data, SIZE_T size, UINT flags, ID3DBlob **out)
{
    if (!out)
        return E_FAIL;
    DcContainer c;
    HRESULT hr = DcOpenContainer(data, size, &c);
    if (FAILED(hr))
        return hr;
    BOOL library = is_library(&c);
    DcBuilder b;
    DcBuilderInit(&b);
    for (UINT i = 0; i < c.count; i++) {
        DcChunk chunk = DcGetChunk(&c, i);
        UINT strip = 0;
        switch (chunk.tag) {
        case DC_TAG('R', 'D', 'E', 'F'):
        case DC_TAG('S', 'T', 'A', 'T'):
            strip = library ? 0 : D3DCOMPILER_STRIP_REFLECTION_DATA;
            break;
        case DC_TAG('S', 'D', 'B', 'G'):
        case DC_TAG('S', 'P', 'D', 'B'):
        case DC_TAG('R', 'P', 'L', 'Y'):
            strip = D3DCOMPILER_STRIP_DEBUG_INFO;
            break;
        case DC_TAG('S', 'D', 'T', 'L'):
        case DC_TAG('S', 'M', 'I', 'D'):
        case DC_TAG('C', 'R', 'P', 'T'):
        case DC_TAG('P', 'E', 'R', 'F'):
            strip = D3DCOMPILER_STRIP_TEST_BLOBS;
            break;
        case DC_TAG('P', 'R', 'I', 'V'):
            strip = D3DCOMPILER_STRIP_PRIVATE_DATA;
            break;
        case DC_TAG('R', 'T', 'S', '0'):
            strip = D3DCOMPILER_STRIP_ROOT_SIGNATURE;
            break;
        }
        if (!(flags & strip))
            DcBuilderAdd(&b, chunk.tag, chunk.data, chunk.size);
    }
    return DcBuilderFinish(&b, out);
}

/* ------------------------------------------------------------------ instruction offsets */

BOOL DcIsDeclaration(UINT opcode)
{
    return (opcode >= 88 && opcode <= 106) || (opcode >= 113 && opcode <= 116) || (opcode >= 143 && opcode <= 162) ||
           opcode == 206;
}

D3DCOMPILERAPI HRESULT WINAPI D3DGetTraceInstructionOffsets(LPCVOID data, SIZE_T size, UINT flags, SIZE_T start,
                                                            SIZE_T count, SIZE_T *offsets, SIZE_T *total)
{
    if (flags & ~D3D_GET_INST_OFFSETS_INCLUDE_NON_EXECUTABLE)
        return E_INVALIDARG;
    DcContainer c;
    HRESULT hr = DcOpenContainer(data, size, &c);
    if (FAILED(hr))
        return hr;
    /* effects are refused. */
    if (DcFindChunk(&c, DC_TAG('F', 'X', '1', '0')) >= 0)
        return E_INVALIDARG;
    int index = DcFindChunk(&c, DC_TAG('S', 'H', 'E', 'X'));
    if (index < 0)
        index = DcFindChunk(&c, DC_TAG('S', 'H', 'D', 'R'));
    if (index < 0)
        return E_FAIL;
    DcChunk code = DcGetChunk(&c, index);
    if (code.size < 8)
        return E_FAIL;
    UINT words = read32(code.data + 4);
    if (words > code.size / 4)
        words = code.size / 4;
    SIZE_T n = 0;
    for (UINT at = 2; at < words;) {
        UINT token = read32(code.data + at * 4), opcode = token & 0x7FF;
        UINT length = (token >> 24) & 0x7F;
        if (opcode == 53)
            length = at + 1 < words ? read32(code.data + at * 4 + 4) : 0;
        BOOL executable;
        if (opcode == 53)
            executable = (token >> 11) == 4;
        else if (opcode >= 114 && opcode <= 116)
            executable = TRUE;
        else
            executable = !DcIsDeclaration(opcode);
        if ((flags & D3D_GET_INST_OFFSETS_INCLUDE_NON_EXECUTABLE) || executable) {
            if (offsets && n >= start && n - start < count)
                *offsets++ = at * 4;
            n++;
        }
        if (!length)
            break;
        at += length;
    }
    if (total)
        *total = n;
    return S_OK;
}
