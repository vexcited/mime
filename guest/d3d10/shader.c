/*
 * blobs, and what d3d10.dll reads of compiled shaders: their signatures,
 * as a container of their own, and their debug information. compiling and
 * disassembling are d3dcompiler_47.dll's, a disassembled effect is made
 * of its shaders' disassembly.
 */
#include "d3d10_private.h"
#include "../lib/dxbc/dxbc.h"

/* ------------------------------------------------------------------ blobs */

typedef struct Blob {
    ID3D10Blob iface;
    LONG refs;
    SIZE_T size;
    BYTE *data;
} Blob;

static Blob *blob_impl(ID3D10Blob *iface) { return CONTAINING_RECORD(iface, Blob, iface); }

static HRESULT STDMETHODCALLTYPE blob_query(ID3D10Blob *iface, REFIID riid, void **out)
{
    *out = nullptr;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_ID3D10Blob))
        return E_NOINTERFACE;
    iface->lpVtbl->AddRef(iface);
    *out = iface;
    return S_OK;
}

static ULONG STDMETHODCALLTYPE blob_add_ref(ID3D10Blob *iface) { return InterlockedIncrement(&blob_impl(iface)->refs); }

static ULONG STDMETHODCALLTYPE blob_release(ID3D10Blob *iface)
{
    Blob *blob = blob_impl(iface);
    ULONG refs = InterlockedDecrement(&blob->refs);
    if (!refs)
        HeapFree(GetProcessHeap(), 0, blob);
    return refs;
}

static void *STDMETHODCALLTYPE blob_get_buffer_pointer(ID3D10Blob *iface) { return blob_impl(iface)->data; }

static SIZE_T STDMETHODCALLTYPE blob_get_buffer_size(ID3D10Blob *iface) { return blob_impl(iface)->size; }

static const ID3D10BlobVtbl blob_vtbl = {
    blob_query, blob_add_ref, blob_release, blob_get_buffer_pointer, blob_get_buffer_size,
};

/* the bytes follow the object, which frees them with it. */
D3D10API HRESULT WINAPI D3D10CreateBlob(SIZE_T size, ID3D10Blob **out)
{
    if (!out)
        return D3DERR_INVALIDCALL;
    if (size > (SIZE_T)-1 - sizeof(Blob))
        return E_OUTOFMEMORY;
    Blob *blob = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Blob) + size);
    if (!blob)
        return E_OUTOFMEMORY;
    blob->iface.lpVtbl = &blob_vtbl;
    blob->refs = 1;
    blob->size = size;
    blob->data = (BYTE *)(blob + 1);
    *out = &blob->iface;
    return S_OK;
}

/* ------------------------------------------------------------------ containers */

static UINT get32(const BYTE *p) { return p[0] | p[1] << 8 | p[2] << 16 | (UINT)p[3] << 24; }

/* the chunks direct3d 10 knows: effects, shaders, their signatures, statistics and debug information. */
static BOOL known_tag(UINT tag)
{
    static const char TAGS[][5] = {"SDBG", "FX10", "CLI4", "CTAB", "FXLC", "SMID",
                                   "RDEF", "SDTL", "ISGN", "OSGN", "SHDR", "STAT"};
    for (UINT i = 0; i < RTL_NUMBER_OF(TAGS); i++)
        if (tag == DXBC_FOURCC(TAGS[i][0], TAGS[i][1], TAGS[i][2], TAGS[i][3]))
            return TRUE;
    return FALSE;
}

/*
 * a container as d3d10.dll reads one: exactly as long as said, version 1,
 * its chunks one after the other and known, its checksum right.
 */
BOOL D3d10ContainerValid(const void *bytecode, SIZE_T length)
{
    const BYTE *b = bytecode;
    if (!b || length < 32 || length > 0xFFFFFFFF || get32(b + 24) != length ||
        get32(b) != DXBC_FOURCC('D', 'X', 'B', 'C') || get32(b + 20) != 1)
        return FALSE;
    UINT count = get32(b + 28);
    if (count > (length - 32) / 4)
        return FALSE;
    UINT64 end = 32 + 4ull * count;
    for (UINT i = 0; i < count; i++) {
        UINT offset = get32(b + 32 + 4 * i);
        if (offset != end || end + 8 > length || !known_tag(get32(b + offset)))
            return FALSE;
        end += 8ull + get32(b + offset + 4);
        if (end > length)
            return FALSE;
    }
    uint32_t checksum[4];
    dxbc_checksum(b, length, checksum);
    return !memcmp(checksum, b + 4, sizeof(checksum));
}

/* ------------------------------------------------------------------ signatures */

#define TAG_ISGN DXBC_FOURCC('I', 'S', 'G', 'N')
#define TAG_OSGN DXBC_FOURCC('O', 'S', 'G', 'N')
#define TAG_SDBG DXBC_FOURCC('S', 'D', 'B', 'G')

static void put32(BYTE *p, UINT v) { memcpy(p, &v, 4); }

/*
 * a container of the shader's input signature, output signature or both,
 * as d3d10.dll builds it: the header, version 1, checksummed. a signature
 * missing fails.
 */
static HRESULT signatures(const void *bytecode, SIZE_T length, BOOL input, BOOL output, ID3D10Blob **out)
{
    const UINT TAGS[2] = {TAG_ISGN, TAG_OSGN};
    const void *chunks[2];
    uint32_t sizes[2];
    UINT tags[2], count = 0, total = 32;
    if (!D3d10ContainerValid(bytecode, length))
        return E_FAIL;
    for (UINT i = 0; i < 2; i++) {
        if (!(i ? output : input))
            continue;
        if (!(chunks[count] = dxbc_find_chunk(bytecode, length, TAGS[i], &sizes[count])))
            return E_FAIL;
        tags[count] = TAGS[i];
        total += 4 + 8 + sizes[count];
        count++;
    }
    if (!out)
        return S_OK;
    ID3D10Blob *blob;
    HRESULT hr = D3D10CreateBlob(total, &blob);
    if (FAILED(hr))
        return hr;
    BYTE *p = blob->lpVtbl->GetBufferPointer(blob);
    put32(p, DXBC_FOURCC('D', 'X', 'B', 'C'));
    put32(p + 20, 1);
    put32(p + 24, total);
    put32(p + 28, count);
    UINT offset = 32 + 4 * count;
    for (UINT i = 0; i < count; i++) {
        put32(p + 32 + 4 * i, offset);
        put32(p + offset, tags[i]);
        put32(p + offset + 4, sizes[i]);
        memcpy(p + offset + 8, chunks[i], sizes[i]);
        offset += 8 + sizes[i];
    }
    uint32_t checksum[4];
    dxbc_checksum(p, total, checksum);
    memcpy(p + 4, checksum, sizeof(checksum));
    *out = blob;
    return S_OK;
}

D3D10API HRESULT WINAPI D3D10GetInputSignatureBlob(const void *bytecode, SIZE_T length, ID3D10Blob **blob)
{
    return signatures(bytecode, length, TRUE, FALSE, blob);
}

D3D10API HRESULT WINAPI D3D10GetOutputSignatureBlob(const void *bytecode, SIZE_T length, ID3D10Blob **blob)
{
    return signatures(bytecode, length, FALSE, TRUE, blob);
}

D3D10API HRESULT WINAPI D3D10GetInputAndOutputSignatureBlob(const void *bytecode, SIZE_T length, ID3D10Blob **blob)
{
    return signatures(bytecode, length, TRUE, TRUE, blob);
}

/* the debug information chunk of a container whose checksum holds, empty when there is none. */
D3D10API HRESULT WINAPI D3D10GetShaderDebugInfo(const void *bytecode, SIZE_T length, ID3D10Blob **out)
{
    if (out)
        *out = nullptr;
    if (!bytecode)
        return D3DERR_INVALIDCALL;
    if (length < 4 || memcmp(bytecode, "DXBC", 4))
        return E_FAIL;
    if (dxbc_check(bytecode, length) || !out)
        return D3DERR_INVALIDCALL;
    uint32_t checksum[4];
    dxbc_checksum(bytecode, get32((const BYTE *)bytecode + 24), checksum);
    if (memcmp(checksum, (const BYTE *)bytecode + 4, sizeof(checksum)))
        return D3DERR_INVALIDCALL;
    if (!D3d10ContainerValid(bytecode, length))
        return E_FAIL;
    uint32_t size = 0;
    const void *chunk = dxbc_find_chunk(bytecode, length, TAG_SDBG, &size);
    ID3D10Blob *blob;
    HRESULT hr = D3D10CreateBlob(chunk ? size : 0, &blob);
    if (FAILED(hr))
        return hr;
    if (chunk)
        memcpy(blob->lpVtbl->GetBufferPointer(blob), chunk, size);
    *out = blob;
    return S_OK;
}

/* ------------------------------------------------------------------ profiles */

/* shader model 4.0 whatever the device, as d3d10.dll answers. */
D3D10API LPCSTR WINAPI D3D10GetVertexShaderProfile(ID3D10Device *device) { return "vs_4_0"; }

D3D10API LPCSTR WINAPI D3D10GetGeometryShaderProfile(ID3D10Device *device) { return "gs_4_0"; }

D3D10API LPCSTR WINAPI D3D10GetPixelShaderProfile(ID3D10Device *device) { return "ps_4_0"; }

/* ------------------------------------------------------------------ d3dcompiler_47.dll */

/*
 * the old hlsl compiler d3d10.dll carries is d3dcompiler_47.dll's here,
 * the same compiler of a later version: what it has, d3d10.dll gives.
 */
static FARPROC compiler_entry(const char *name)
{
    HMODULE compiler = LoadLibraryW(L"d3dcompiler_47.dll");
    return compiler ? GetProcAddress(compiler, name) : nullptr;
}

typedef HRESULT(WINAPI *Compile)(const void *, SIZE_T, LPCSTR, const D3D10_SHADER_MACRO *, ID3D10Include *, LPCSTR,
                                 LPCSTR, UINT, UINT, ID3D10Blob **, ID3D10Blob **);

/* the debug flag needs d3d10's debug layer, which is not there; flags the old compiler lacks are refused. */
D3D10API HRESULT WINAPI D3D10CompileShader(LPCSTR source, SIZE_T length, LPCSTR file, const D3D10_SHADER_MACRO *defines,
                                           ID3D10Include *include, LPCSTR entry, LPCSTR profile, UINT flags,
                                           ID3D10Blob **shader, ID3D10Blob **errors)
{
    if (flags & D3D10_SHADER_DEBUG)
        return E_NOTIMPL;
    if (shader)
        *shader = nullptr;
    if (errors)
        *errors = nullptr;
    if ((flags & 0xDF0301C0) || !entry)
        return D3DERR_INVALIDCALL;
    Compile compile = (Compile)(void *)compiler_entry("D3DCompile");
    return compile ? compile(source, length, file, defines, include, entry, profile, flags, 0, shader, errors)
                   : E_NOTIMPL;
}

D3D10API HRESULT WINAPI D3D10PreprocessShader(LPCSTR source, SIZE_T length, LPCSTR file,
                                              const D3D10_SHADER_MACRO *defines, ID3D10Include *include,
                                              ID3D10Blob **text, ID3D10Blob **errors)
{
    typedef HRESULT(WINAPI * Preprocess)(const void *, SIZE_T, LPCSTR, const D3D10_SHADER_MACRO *, ID3D10Include *,
                                         ID3D10Blob **, ID3D10Blob **);
    if (text)
        *text = nullptr;
    if (errors)
        *errors = nullptr;
    Preprocess preprocess = (Preprocess)(void *)compiler_entry("D3DPreprocess");
    return preprocess ? preprocess(source, length, file, defines, include, text, errors) : E_NOTIMPL;
}

/* an effect is compiled for fx_4_0, its flags the compiler's second. */
D3D10API HRESULT WINAPI D3D10CompileEffectFromMemory(void *data, SIZE_T length, LPCSTR file,
                                                     const D3D10_SHADER_MACRO *defines, ID3D10Include *include,
                                                     UINT hlsl_flags, UINT fx_flags, ID3D10Blob **effect,
                                                     ID3D10Blob **errors)
{
    if (effect)
        *effect = nullptr;
    if (errors)
        *errors = nullptr;
    Compile compile = (Compile)(void *)compiler_entry("D3DCompile");
    return compile ? compile(data, length, file, defines, include, nullptr, "fx_4_0", hlsl_flags, fx_flags, effect, errors)
                   : E_NOTIMPL;
}

D3D10API HRESULT WINAPI D3D10DisassembleShader(const void *bytecode, SIZE_T length, BOOL color, LPCSTR comments,
                                               ID3D10Blob **disassembly)
{
    typedef HRESULT(WINAPI * Disassemble)(const void *, SIZE_T, UINT, LPCSTR, ID3D10Blob **);
    if (!bytecode || !disassembly)
        return D3DERR_INVALIDCALL;
    *disassembly = nullptr;
    Disassemble disassemble = (Disassemble)(void *)compiler_entry("D3DDisassemble");
    return disassemble ? disassemble(bytecode, length, color ? 1 : 0, comments, disassembly) : E_NOTIMPL;
}

/* ------------------------------------------------------------------ effect disassembly */

typedef struct Text {
    char *data;
    SIZE_T size, capacity;
    BOOL failed;
} Text;

static void append(Text *t, const char *s, SIZE_T n)
{
    if (t->failed)
        return;
    if (t->size + n + 1 > t->capacity) {
        SIZE_T capacity = t->capacity ? 2 * t->capacity : 4096;
        while (capacity < t->size + n + 1)
            capacity *= 2;
        char *data =
            t->data ? HeapReAlloc(GetProcessHeap(), 0, t->data, capacity) : HeapAlloc(GetProcessHeap(), 0, capacity);
        if (!data) {
            t->failed = TRUE;
            return;
        }
        t->data = data;
        t->capacity = capacity;
    }
    memcpy(t->data + t->size, s, n);
    t->size += n;
}

static void put(Text *t, const char *s) { append(t, s, strlen(s)); }

static void put_format(Text *t, const char *format, ...)
{
    char line[1024];
    va_list ap;
    va_start(ap, format);
    int n = wvsprintfA(line, format, ap);
    va_end(ap);
    append(t, line, n > 0 ? (SIZE_T)n : 0);
}

/* text with each line after the first indented. */
static void put_indented(Text *t, const char *s, UINT indent)
{
    static const char SPACES[] = "                ";
    for (const char *line = s; *line;) {
        const char *end = strchr(line, '\n');
        SIZE_T n = end ? (SIZE_T)(end - line) + 1 : strlen(line);
        if (line != s)
            append(t, SPACES, indent < 16 ? indent : 16);
        append(t, line, n);
        line += n;
    }
}

static HRESULT put_shader(Text *t, const D3D10_EFFECT_SHADER_DESC *desc, BOOL color, ID3D10Blob **blob)
{
    HRESULT hr = D3D10DisassembleShader(desc->pBytecode, desc->BytecodeLength, color, nullptr, blob);
    if (SUCCEEDED(hr))
        put_indented(t, (*blob)->lpVtbl->GetBufferPointer(*blob), 4);
    if (*blob)
        (*blob)->lpVtbl->Release(*blob);
    *blob = nullptr;
    return hr;
}

/*
 * the effect's shader variables, then its techniques and passes and the
 * shaders they set, as d3d10.dll lays them out; html when colored.
 */
D3D10API HRESULT WINAPI D3D10DisassembleEffect(ID3D10Effect *effect, BOOL color, ID3D10Blob **disassembly)
{
    static const char *const STAGES[3] = {"VertexShader", "GeometryShader", "PixelShader"};
    if (!effect || !disassembly)
        return D3DERR_INVALIDCALL;
    *disassembly = nullptr;
    Text t = {};
    ID3D10Blob *blob = nullptr;
    D3D10_EFFECT_DESC desc;
    if (color)
        put(&t, "<pre><body bgcolor=\"#000000\"><body bgcolor=\"#000000\"><font color=\"#00ff00\">");
    HRESULT hr = effect->lpVtbl->GetDesc(effect, &desc);
    for (UINT i = 0; SUCCEEDED(hr) && i < desc.GlobalVariables; i++) {
        ID3D10EffectVariable *v = effect->lpVtbl->GetVariableByIndex(effect, i);
        ID3D10EffectType *type = v->lpVtbl->GetType(v);
        D3D10_EFFECT_TYPE_DESC td;
        D3D10_EFFECT_VARIABLE_DESC vd;
        if (!v->lpVtbl->IsValid(v) || !type->lpVtbl->IsValid(type)) {
            hr = E_FAIL;
            break;
        }
        if (FAILED(hr = type->lpVtbl->GetDesc(type, &td)) || FAILED(hr = v->lpVtbl->GetDesc(v, &vd)))
            break;
        if (td.Type != D3D_SVT_PIXELSHADER && td.Type != D3D_SVT_VERTEXSHADER && td.Type != D3D_SVT_GEOMETRYSHADER)
            continue;
        ID3D10EffectShaderVariable *shader = v->lpVtbl->AsShader(v);
        if (td.Elements)
            put_format(&t, "%s %s[%d] = \n{\n", td.TypeName, vd.Name, td.Elements);
        else
            put_format(&t, "%s %s = ", td.TypeName, vd.Name);
        for (UINT e = 0; SUCCEEDED(hr) && e < (td.Elements ? td.Elements : 1); e++) {
            D3D10_EFFECT_SHADER_DESC sd;
            if (FAILED(hr = shader->lpVtbl->GetShaderDesc(shader, e, &sd)))
                break;
            if (!sd.BytecodeLength) {
                put(&t, td.Elements ? "\n    NULL,\n" : "NULL");
                continue;
            }
            put(&t, td.Elements ? "\n    asm {\n\n" : "asm {\n\n");
            hr = put_shader(&t, &sd, color, &blob);
            put(&t, td.Elements ? "\n    },\n" : "\n}");
        }
        if (td.Elements)
            put(&t, "\n}");
        put(&t, ";\n\n");
    }
    for (UINT i = 0; SUCCEEDED(hr) && i < desc.Techniques; i++) {
        ID3D10EffectTechnique *technique = effect->lpVtbl->GetTechniqueByIndex(effect, i);
        D3D10_TECHNIQUE_DESC td;
        if (FAILED(hr = technique->lpVtbl->GetDesc(technique, &td)))
            break;
        put(&t, "technique10 ");
        if (td.Name)
            put(&t, td.Name);
        put(&t, "\n{\n");
        for (UINT j = 0; SUCCEEDED(hr) && j < td.Passes; j++) {
            ID3D10EffectPass *pass = technique->lpVtbl->GetPassByIndex(technique, j);
            D3D10_PASS_DESC pd;
            D3D10_PASS_SHADER_DESC shaders[3];
            if (FAILED(hr = pass->lpVtbl->GetDesc(pass, &pd)) ||
                FAILED(hr = pass->lpVtbl->GetVertexShaderDesc(pass, &shaders[0])) ||
                FAILED(hr = pass->lpVtbl->GetGeometryShaderDesc(pass, &shaders[1])) ||
                FAILED(hr = pass->lpVtbl->GetPixelShaderDesc(pass, &shaders[2])))
                break;
            put(&t, "    pass ");
            if (pd.Name)
                put(&t, pd.Name);
            put(&t, "\n    {\n");
            for (UINT k = 0; SUCCEEDED(hr) && k < 3; k++) {
                ID3D10EffectShaderVariable *shader = shaders[k].pShaderVariable;
                D3D10_EFFECT_SHADER_DESC sd;
                if (!shader->lpVtbl->IsValid(shader)) {
                    put_format(&t, "        // No embedded %s\n", STAGES[k]);
                    continue;
                }
                if (FAILED(hr = shader->lpVtbl->GetShaderDesc(shader, shaders[k].ShaderIndex, &sd)))
                    break;
                if (sd.IsInline && sd.BytecodeLength) {
                    put_format(&t, "        %s = asm {\n\n", STAGES[k]);
                    hr = put_shader(&t, &sd, color, &blob);
                    put(&t, "\n        };\n");
                } else if (sd.IsInline) {
                    put_format(&t, "        %s = NULL;\n", STAGES[k]);
                } else {
                    D3D10_EFFECT_VARIABLE_DESC vd;
                    D3D10_EFFECT_TYPE_DESC tdesc;
                    ID3D10EffectType *type = shader->lpVtbl->GetType(shader);
                    if (FAILED(hr = type->lpVtbl->GetDesc(type, &tdesc)) ||
                        FAILED(hr = shader->lpVtbl->GetDesc(shader, &vd)))
                        break;
                    if (tdesc.Elements)
                        put_format(&t, "        %s = %s[%d];\n", STAGES[k], vd.Name, shaders[k].ShaderIndex);
                    else
                        put_format(&t, "        %s = %s;\n", STAGES[k], vd.Name);
                }
            }
            put(&t, "    }\n");
        }
        put(&t, "}\n\n");
    }
    if (color)
        put(&t, "</font></pre>");
    if (SUCCEEDED(hr) && t.failed)
        hr = E_OUTOFMEMORY;
    if (SUCCEEDED(hr) && SUCCEEDED(hr = D3D10CreateBlob(t.size + 1, disassembly))) {
        char *out = (*disassembly)->lpVtbl->GetBufferPointer(*disassembly);
        memcpy(out, t.data, t.size);
        out[t.size] = 0;
    }
    HeapFree(GetProcessHeap(), 0, t.data);
    return hr;
}
