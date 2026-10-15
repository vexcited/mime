/*
 * the exports of d3dcompiler_47.dll that are not about the container:
 * blobs, files, and the compiler entry points, which fail as a shader with
 * errors does since mime has no hlsl compiler.
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
    *out = NULL;
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
    HANDLE file = CreateFile2(name, GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return last_error();
    LARGE_INTEGER size = {0};
    HRESULT hr = S_OK;
    if (!GetFileSizeEx(file, &size))
        hr = last_error();
    ID3DBlob *blob = NULL;
    if (SUCCEEDED(hr))
        hr = DcCreateBlob((SIZE_T)size.QuadPart, &blob);
    if (SUCCEEDED(hr)) {
        BYTE *at = blob->lpVtbl->GetBufferPointer(blob);
        ULONGLONG left = (ULONGLONG)size.QuadPart;
        while (left) {
            DWORD chunk = left > 0xFFFFFFFF ? 0xFFFFFFFF : (DWORD)left, read = 0;
            if (!ReadFile(file, at, chunk, &read, NULL) && FAILED(hr = last_error()))
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
            blob = NULL;
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
    /* overwrite picks CREATE_ALWAYS over CREATE_NEW by adding one, as windows does. */
    HANDLE file = CreateFile2(name, GENERIC_WRITE, FILE_SHARE_READ, (DWORD)overwrite + 1, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return last_error();
    const BYTE *at = blob->lpVtbl->GetBufferPointer(blob);
    SIZE_T left = blob->lpVtbl->GetBufferSize(blob);
    HRESULT hr = S_OK;
    while (left) {
        DWORD chunk = left > 0xFFFFFFFF ? 0xFFFFFFFF : (DWORD)left, written = 0;
        if (!WriteFile(file, at, chunk, &written, NULL) && FAILED(hr = last_error()))
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

/* the error, formatted as the compiler formats its messages: the file, line and column, then the code. */
static void compile_error(LPCVOID data, LPCSTR name, const char *what, ID3DBlob **errors)
{
    if (!errors)
        return;
    DcText text = {0};
    if (name)
        DcTextPrintf(&text, "%s(1,1): ", name);
    else
        DcTextPrintf(&text, "Shader@0x%p(1,1): ", data);
    DcTextPrintf(&text, "error X1000: %s is not available in this d3dcompiler_47.dll\n", what);
    if (!text.failed)
        DcTextBlob(text.buf, text.length, errors);
    DcTextFree(&text);
}

D3DCOMPILERAPI HRESULT WINAPI D3DCompile2(LPCVOID data, SIZE_T size, LPCSTR name, const D3D_SHADER_MACRO *defines,
                                          ID3DInclude *include, LPCSTR entry, LPCSTR target, UINT flags1,
                                          UINT flags2, UINT secondary_flags, LPCVOID secondary,
                                          SIZE_T secondary_size, ID3DBlob **code, ID3DBlob **errors)
{
    if (code)
        *code = NULL;
    if (errors)
        *errors = NULL;
    if ((secondary_flags & ~7u) || (!secondary_flags && (secondary || secondary_size)))
        return E_INVALIDARG;
    compile_error(data, name, "the HLSL compiler", errors);
    return E_FAIL;
}

D3DCOMPILERAPI HRESULT WINAPI D3DCompile(LPCVOID data, SIZE_T size, LPCSTR name, const D3D_SHADER_MACRO *defines,
                                         ID3DInclude *include, LPCSTR entry, LPCSTR target, UINT flags1,
                                         UINT flags2, ID3DBlob **code, ID3DBlob **errors)
{
    return D3DCompile2(data, size, name, defines, include, entry, target, flags1, flags2, 0, NULL, 0, code, errors);
}

D3DCOMPILERAPI HRESULT WINAPI D3DCompileFromFile(LPCWSTR file, const D3D_SHADER_MACRO *defines,
                                                 ID3DInclude *include, LPCSTR entry, LPCSTR target, UINT flags1,
                                                 UINT flags2, ID3DBlob **code, ID3DBlob **errors)
{
    char name[260];
    if (!WideCharToMultiByte(CP_UTF8, 0, file, -1, name, sizeof(name), NULL, NULL))
        return E_FAIL;
    name[259] = 0;
    ID3DBlob *source = NULL;
    HRESULT hr = D3DReadFileToBlob(file, &source);
    if (SUCCEEDED(hr))
        hr = D3DCompile2(source->lpVtbl->GetBufferPointer(source), source->lpVtbl->GetBufferSize(source), name,
                         defines, include, entry, target, flags1, flags2, 0, NULL, 0, code, errors);
    if (source)
        source->lpVtbl->Release(source);
    return hr;
}

D3DCOMPILERAPI HRESULT WINAPI D3DPreprocess(LPCVOID data, SIZE_T size, LPCSTR name, const D3D_SHADER_MACRO *defines,
                                            ID3DInclude *include, ID3DBlob **text, ID3DBlob **errors)
{
    if (!data || !text)
        return E_INVALIDARG;
    *text = NULL;
    if (errors)
        *errors = NULL;
    compile_error(data, name, "the HLSL preprocessor", errors);
    return E_FAIL;
}

D3DCOMPILERAPI HRESULT WINAPI D3DAssemble(LPCVOID data, SIZE_T size, LPCSTR name, const D3D_SHADER_MACRO *defines,
                                          ID3DInclude *include, UINT flags, ID3DBlob **code, ID3DBlob **errors)
{
    if (code)
        *code = NULL;
    if (errors)
        *errors = NULL;
    if (flags & ~3u)
        return D3DERR_INVALIDCALL;
    compile_error(data, name, "the shader assembler", errors);
    return E_NOTIMPL;
}

/* ------------------------------------------------------------------ what mime does not implement */

D3DCOMPILERAPI HRESULT WINAPI D3DCompressShaders(UINT count, D3D_SHADER_DATA *shaders, UINT flags, ID3DBlob **out)
{
    if (out)
        *out = NULL;
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
    *linker = NULL;
    return E_NOTIMPL;
}

D3DCOMPILERAPI HRESULT WINAPI D3DLoadModule(LPCVOID data, SIZE_T size, ID3D11Module **module)
{
    if (!module || !data || size - 1 > 0x7FFFFFFD)
        return E_INVALIDARG;
    *module = NULL;
    return E_NOTIMPL;
}

D3DCOMPILERAPI HRESULT WINAPI D3DCreateFunctionLinkingGraph(UINT flags, ID3D11FunctionLinkingGraph **graph)
{
    if (!graph || flags)
        return E_INVALIDARG;
    *graph = NULL;
    return E_NOTIMPL;
}

D3DCOMPILERAPI HRESULT WINAPI D3DDisassemble10Effect(ID3D10Effect *effect, UINT flags, ID3DBlob **out)
{
    if (out)
        *out = NULL;
    return E_NOTIMPL;
}

D3DCOMPILERAPI HRESULT WINAPI D3DDisassemble11Trace(LPCVOID data, SIZE_T size, ID3D11ShaderTrace *trace, UINT start,
                                                    UINT count, UINT flags, ID3DBlob **out)
{
    if (out)
        *out = NULL;
    return E_NOTIMPL;
}

/* windows' does nothing either. */
D3DCOMPILERAPI void WINAPI DebugSetMute(void) {}

/* returns false, as windows' does. */
D3DCOMPILERAPI BOOL WINAPI D3DReturnFailure1(void) { return FALSE; }
