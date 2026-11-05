/*
 * d3dcompiler_47.dll exports outside the container code: blobs, files and
 * the compiler entry points, which run mime's hlsl compiler.
 */
#include "d3dcompiler_private.h"

int _fltused = 0x9875;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

void *DcAlloc(SIZE_T size) { return HeapAlloc(GetProcessHeap(), 0, size ? size : 1); }

void DcFree(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

/* ------------------------------------------------------------------ blobs */

#define VTBL_FN(f) ((void *)(f))

typedef struct DcBlob {
    ID3DBlob iface;
    LONG refs;
    SIZE_T size;
    BYTE *data;
} DcBlob;

static DcBlob *blob_impl(ID3DBlob *iface) { return CONTAINING_RECORD(iface, DcBlob, iface); }

static HRESULT STDMETHODCALLTYPE blob_query(ID3DBlob *iface, REFIID riid, void **out)
{
    *out = nullptr;
    if (!IsEqualGUID(riid, &IID_IUnknown) && !IsEqualGUID(riid, &IID_ID3D10Blob))
        return E_NOINTERFACE;
    *out = iface;
    iface->lpVtbl->AddRef(iface);
    return S_OK;
}

static ULONG STDMETHODCALLTYPE blob_add_ref(ID3DBlob *iface) { return InterlockedIncrement(&blob_impl(iface)->refs); }

static ULONG STDMETHODCALLTYPE blob_release(ID3DBlob *iface)
{
    DcBlob *blob = blob_impl(iface);
    ULONG refs = InterlockedDecrement(&blob->refs);
    if (!refs)
        DcFree(blob);
    return refs;
}

static void *STDMETHODCALLTYPE blob_pointer(ID3DBlob *iface) { return blob_impl(iface)->data; }

static SIZE_T STDMETHODCALLTYPE blob_size(ID3DBlob *iface) { return blob_impl(iface)->size; }

static const ID3D10BlobVtbl blob_vtbl = {VTBL_FN(blob_query), VTBL_FN(blob_add_ref), VTBL_FN(blob_release),
                                         VTBL_FN(blob_pointer), VTBL_FN(blob_size)};

HRESULT DcCreateBlob(SIZE_T size, ID3DBlob **out)
{
    if (!out)
        return D3DERR_INVALIDCALL;
    SIZE_T header = (sizeof(DcBlob) + 15) & ~(SIZE_T)15;
    if (size > (SIZE_T)-1 - header)
        return E_OUTOFMEMORY;
    DcBlob *blob = DcAlloc(header + size);
    if (!blob)
        return E_OUTOFMEMORY;
    blob->iface.lpVtbl = &blob_vtbl;
    blob->refs = 1;
    blob->size = size;
    blob->data = (BYTE *)blob + header;
    *out = &blob->iface;
    return S_OK;
}

HRESULT DcTextBlob(const char *text, SIZE_T length, ID3DBlob **out)
{
    HRESULT hr = DcCreateBlob(length + 1, out);
    if (FAILED(hr))
        return hr;
    BYTE *p = (*out)->lpVtbl->GetBufferPointer(*out);
    memcpy(p, text, length);
    p[length] = 0;
    return S_OK;
}

D3DCOMPILERAPI HRESULT WINAPI D3DCreateBlob(SIZE_T size, ID3DBlob **blob) { return DcCreateBlob(size, blob); }

/* ------------------------------------------------------------------ files */

static HRESULT last_error(void)
{
    LONG error = (LONG)GetLastError();
    return error > 0 ? (HRESULT)((error & 0xFFFF) | 0x80070000) : error;
}

D3DCOMPILERAPI HRESULT WINAPI D3DReadFileToBlob(LPCWSTR name, ID3DBlob **contents)
{
    HANDLE file = CreateFile2(name, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return last_error();
    LARGE_INTEGER size = {};
    HRESULT hr = S_OK;
    if (!GetFileSizeEx(file, &size))
        hr = last_error();
    ID3DBlob *blob = nullptr;
    if (SUCCEEDED(hr))
        hr = DcCreateBlob((SIZE_T)size.QuadPart, &blob);
    if (SUCCEEDED(hr)) {
        BYTE *at = blob->lpVtbl->GetBufferPointer(blob);
        ULONGLONG left = (ULONGLONG)size.QuadPart;
        while (left) {
            DWORD chunk = left > 0xFFFFFFFF ? 0xFFFFFFFF : (DWORD)left, read = 0;
            if (!ReadFile(file, at, chunk, &read, nullptr) && FAILED(hr = last_error()))
                break;
            if (read < chunk) {
                hr = E_FAIL;
                break;
            }
            at += read;
            left -= read;
        }
        if (SUCCEEDED(hr)) {
            *contents = blob;
            blob = nullptr;
            hr = S_OK;
        }
    }
    CloseHandle(file);
    if (blob)
        blob->lpVtbl->Release(blob);
    return hr;
}

D3DCOMPILERAPI HRESULT WINAPI D3DWriteBlobToFile(ID3DBlob *blob, LPCWSTR name, BOOL overwrite)
{
    if (!blob)
        return E_INVALIDARG;
    /* overwrite turns CREATE_NEW into CREATE_ALWAYS by adding one. */
    HANDLE file = CreateFile2(name, GENERIC_WRITE, FILE_SHARE_READ, (DWORD)overwrite + 1, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return last_error();
    const BYTE *at = blob->lpVtbl->GetBufferPointer(blob);
    SIZE_T left = blob->lpVtbl->GetBufferSize(blob);
    HRESULT hr = S_OK;
    while (left) {
        DWORD chunk = left > 0xFFFFFFFF ? 0xFFFFFFFF : (DWORD)left, written = 0;
        if (!WriteFile(file, at, chunk, &written, nullptr) && FAILED(hr = last_error()))
            break;
        if (written < chunk) {
            hr = E_FAIL;
            break;
        }
        at += written;
        left -= written;
    }
    CloseHandle(file);
    return SUCCEEDED(hr) ? S_OK : hr;
}

/* ------------------------------------------------------------------ compiling */

#define STATUS_HLSL_FATAL 0xE0485331

static void *hl_heap_alloc(void *ctx, size_t size) { return HeapAlloc(GetProcessHeap(), 0, size ? size : 1); }
static void hl_heap_free(void *ctx, void *p) { HeapFree(GetProcessHeap(), 0, p); }

static void hl_fatal(void *ctx) { RaiseException(STATUS_HLSL_FATAL, EXCEPTION_NONCONTINUABLE, 0, nullptr); }

/* D3D_COMPILE_STANDARD_FILE_INCLUDE: files next to the one including them, then the current directory. */
typedef struct IncludeFile {
    struct IncludeFile *next;
    char *data;
    char dir[MAX_PATH];
} IncludeFile;

typedef struct IncludeState {
    ID3DInclude *include;
    IncludeFile *files;
    char source_dir[MAX_PATH];
} IncludeState;

static void directory_of(const char *path, char *out)
{
    size_t n = strlen(path), cut = 0;
    for (size_t i = 0; i < n; i++)
        if (path[i] == '\\' || path[i] == '/')
            cut = i + 1;
    if (cut >= MAX_PATH)
        cut = 0;
    memcpy(out, path, cut);
    out[cut] = 0;
}

static char *read_whole(const char *path, uint32_t *size)
{
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return nullptr;
    DWORD high = 0, low = GetFileSize(file, &high), read = 0;
    char *data = nullptr;
    if (!high && low < 0x10000000 && (data = DcAlloc(low + 1))) {
        if (!ReadFile(file, data, low, &read, nullptr) || read != low) {
            DcFree(data);
            data = nullptr;
        } else {
            *size = low;
        }
    }
    CloseHandle(file);
    return data;
}

static int include_open(void *ctx, int system, const char *name, const void *parent, const void **data, uint32_t *size)
{
    IncludeState *st = ctx;
    if (st->include != D3D_COMPILE_STANDARD_FILE_INCLUDE) {
        LPCVOID d = nullptr;
        UINT n = 0;
        HRESULT hr = st->include->lpVtbl->Open(st->include, system ? D3D_INCLUDE_SYSTEM : D3D_INCLUDE_LOCAL, name,
                                               parent, &d, &n);
        if (FAILED(hr))
            return -1;
        *data = d;
        *size = n;
        return 0;
    }
    const char *dir = st->source_dir;
    for (IncludeFile *f = st->files; f; f = f->next)
        if (f->data == parent)
            dir = f->dir;
    char path[MAX_PATH * 2];
    bool absolute = name[0] == '\\' || name[0] == '/' || (name[0] && name[1] == ':');
    for (int attempt = 0; attempt < 2; attempt++) {
        const char *base = absolute || attempt ? "" : dir;
        size_t bl = strlen(base), nl = strlen(name);
        if (bl + nl + 1 > sizeof(path))
            return -1;
        memcpy(path, base, bl);
        memcpy(path + bl, name, nl + 1);
        uint32_t n = 0;
        char *d = read_whole(path, &n);
        if (!d)
            continue;
        IncludeFile *f = DcAlloc(sizeof(IncludeFile));
        if (!f) {
            DcFree(d);
            return -1;
        }
        f->data = d;
        directory_of(path, f->dir);
        f->next = st->files;
        st->files = f;
        *data = d;
        *size = n;
        return 0;
    }
    return -1;
}

static void include_close(void *ctx, const void *data)
{
    IncludeState *st = ctx;
    if (st->include != D3D_COMPILE_STANDARD_FILE_INCLUDE)
        st->include->lpVtbl->Close(st->include, data);
}

static void include_free(IncludeState *st)
{
    for (IncludeFile *f = st->files, *next; f; f = next) {
        next = f->next;
        DcFree(f->data);
        DcFree(f);
    }
}

/* runs the compiler or the preprocessor; an internal failure becomes an error like fxc's. */
static HRESULT run_hlsl(LPCVOID data, SIZE_T size, LPCSTR name, const D3D_SHADER_MACRO *defines, ID3DInclude *include,
                        LPCSTR entry, LPCSTR target, UINT flags1, UINT flags2, BOOL preprocess, ID3DBlob **out,
                        ID3DBlob **errors)
{
    MimeAllocator alloc = {hl_heap_alloc, hl_heap_free, nullptr};
    Hlsl *h = hlsl_new(&alloc, hl_fatal, nullptr);
    if (!h)
        return E_OUTOFMEMORY;
    UINT macro_count = 0;
    if (defines)
        while (defines[macro_count].Name)
            macro_count++;
    IncludeState st = {.include = include};
    if (name)
        directory_of(name, st.source_dir);
    HlslInclude inc = {include_open, include_close, &st};
    HlslOptions o = {.source = data, .size = size, .name = name, .macros = (const HlslMacro *)defines,
                     .macro_count = macro_count, .include = include ? &inc : nullptr, .entry = entry,
                     .target = target, .flags = flags1, .effect_flags = flags2};
    HRESULT hr = S_OK;
    int rc;
    __try {
        rc = preprocess ? hlsl_preprocess(h, &o) : hlsl_compile(h, &o);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        rc = -2;
    }
    size_t length = 0;
    const char *messages = hlsl_messages(h, &length);
    DcText text = {};
    if (messages)
        DcTextAppend(&text, messages, length);
    if (rc == -2) {
        if (name)
            DcTextPrintf(&text, "%s(1,1): ", name);
        else
            DcTextPrintf(&text, "Shader@0x%p(1,1): ", data);
        DcTextPuts(&text, "error X3000: internal compiler error\n");
    }
    if (rc) {
        hr = E_FAIL;
    } else {
        const void *output = hlsl_output(h, &length);
        if (preprocess)
            hr = DcTextBlob(output ? output : "", length, out);
        else if (SUCCEEDED(hr = DcCreateBlob(length, out)))
            memcpy((*out)->lpVtbl->GetBufferPointer(*out), output, length);
    }
    if (errors && text.length && !text.failed)
        DcTextBlob(text.buf, text.length, errors);
    DcTextFree(&text);
    hlsl_delete(h);
    include_free(&st);
    return hr;
}

D3DCOMPILERAPI HRESULT WINAPI D3DCompile2(LPCVOID data, SIZE_T size, LPCSTR name, const D3D_SHADER_MACRO *defines,
                                          ID3DInclude *include, LPCSTR entry, LPCSTR target, UINT flags1,
                                          UINT flags2, UINT secondary_flags, LPCVOID secondary,
                                          SIZE_T secondary_size, ID3DBlob **code, ID3DBlob **errors)
{
    if (code)
        *code = nullptr;
    if (errors)
        *errors = nullptr;
    if ((secondary_flags & ~7u) || (!secondary_flags && (secondary || secondary_size)))
        return E_INVALIDARG;
    if (!data || !target || !code)
        return E_INVALIDARG;
    ID3DBlob *blob = nullptr;
    HRESULT hr = run_hlsl(data, size, name, defines, include, entry, target, flags1, flags2, FALSE, &blob, errors);
    *code = blob;
    return hr;
}

D3DCOMPILERAPI HRESULT WINAPI D3DCompile(LPCVOID data, SIZE_T size, LPCSTR name, const D3D_SHADER_MACRO *defines,
                                         ID3DInclude *include, LPCSTR entry, LPCSTR target, UINT flags1,
                                         UINT flags2, ID3DBlob **code, ID3DBlob **errors)
{
    return D3DCompile2(data, size, name, defines, include, entry, target, flags1, flags2, 0, nullptr, 0, code, errors);
}

D3DCOMPILERAPI HRESULT WINAPI D3DCompileFromFile(LPCWSTR file, const D3D_SHADER_MACRO *defines,
                                                 ID3DInclude *include, LPCSTR entry, LPCSTR target, UINT flags1,
                                                 UINT flags2, ID3DBlob **code, ID3DBlob **errors)
{
    char name[MAX_PATH * 3];
    if (code)
        *code = nullptr;
    if (errors)
        *errors = nullptr;
    if (!WideCharToMultiByte(CP_ACP, 0, file, -1, name, sizeof(name), nullptr, nullptr))
        return E_FAIL;
    ID3DBlob *source = nullptr;
    HRESULT hr = D3DReadFileToBlob(file, &source);
    if (SUCCEEDED(hr))
        hr = D3DCompile2(source->lpVtbl->GetBufferPointer(source), source->lpVtbl->GetBufferSize(source), name,
                         defines, include, entry, target, flags1, flags2, 0, nullptr, 0, code, errors);
    if (source)
        source->lpVtbl->Release(source);
    return hr;
}

D3DCOMPILERAPI HRESULT WINAPI D3DPreprocess(LPCVOID data, SIZE_T size, LPCSTR name, const D3D_SHADER_MACRO *defines,
                                            ID3DInclude *include, ID3DBlob **text, ID3DBlob **errors)
{
    if (!data || !text)
        return E_INVALIDARG;
    *text = nullptr;
    if (errors)
        *errors = nullptr;
    return run_hlsl(data, size, name, defines, include, nullptr, nullptr, 0, 0, TRUE, text, errors);
}

/* the assembler is not there: fail like a shader with errors. */
static void compile_error(LPCVOID data, LPCSTR name, const char *what, ID3DBlob **errors)
{
    if (!errors)
        return;
    DcText text = {};
    if (name)
        DcTextPrintf(&text, "%s(1,1): ", name);
    else
        DcTextPrintf(&text, "Shader@0x%p(1,1): ", data);
    DcTextPrintf(&text, "error X1000: %s is not available in this d3dcompiler_47.dll\n", what);
    if (!text.failed)
        DcTextBlob(text.buf, text.length, errors);
    DcTextFree(&text);
}

D3DCOMPILERAPI HRESULT WINAPI D3DAssemble(LPCVOID data, SIZE_T size, LPCSTR name, const D3D_SHADER_MACRO *defines,
                                          ID3DInclude *include, UINT flags, ID3DBlob **code, ID3DBlob **errors)
{
    if (code)
        *code = nullptr;
    if (errors)
        *errors = nullptr;
    if (flags & ~3u)
        return D3DERR_INVALIDCALL;
    compile_error(data, name, "the shader assembler", errors);
    return E_NOTIMPL;
}

/* ------------------------------------------------------------------ unimplemented */

D3DCOMPILERAPI HRESULT WINAPI D3DCompressShaders(UINT count, D3D_SHADER_DATA *shaders, UINT flags, ID3DBlob **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}

D3DCOMPILERAPI HRESULT WINAPI D3DDecompressShaders(LPCVOID data, SIZE_T size, UINT count, UINT start, UINT *indices,
                                                   UINT flags, ID3DBlob **shaders, UINT *total)
{
    if (total)
        *total = 0;
    return E_NOTIMPL;
}

D3DCOMPILERAPI HRESULT WINAPI D3DCreateLinker(ID3D11Linker **linker)
{
    if (!linker)
        return E_INVALIDARG;
    *linker = nullptr;
    return E_NOTIMPL;
}

D3DCOMPILERAPI HRESULT WINAPI D3DLoadModule(LPCVOID data, SIZE_T size, ID3D11Module **module)
{
    if (!module || !data || size - 1 > 0x7FFFFFFD)
        return E_INVALIDARG;
    *module = nullptr;
    return E_NOTIMPL;
}

D3DCOMPILERAPI HRESULT WINAPI D3DCreateFunctionLinkingGraph(UINT flags, ID3D11FunctionLinkingGraph **graph)
{
    if (!graph || flags)
        return E_INVALIDARG;
    *graph = nullptr;
    return E_NOTIMPL;
}

D3DCOMPILERAPI HRESULT WINAPI D3DDisassemble10Effect(ID3D10Effect *effect, UINT flags, ID3DBlob **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}

D3DCOMPILERAPI HRESULT WINAPI D3DDisassemble11Trace(LPCVOID data, SIZE_T size, ID3D11ShaderTrace *trace, UINT start,
                                                    UINT count, UINT flags, ID3DBlob **out)
{
    if (out)
        *out = nullptr;
    return E_NOTIMPL;
}

/* Windows does nothing here either. */
D3DCOMPILERAPI void WINAPI DebugSetMute(void) {}

D3DCOMPILERAPI BOOL WINAPI D3DReturnFailure1(void) { return FALSE; }
