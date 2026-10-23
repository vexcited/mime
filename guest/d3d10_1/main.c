/*
 * d3d10_1.dll: direct3d 10.1 devices are d3d11.dll devices at the requested
 * feature level. blobs, state blocks and shaders forward to d3d10.dll, loaded
 * on first use like windows does.
 */
#define MIME_BUILD_D3D10_1
#include "../d3d10/d3d10_private.h"

int _fltused = 0x9875;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

/*
 * levels 10.0, 10.1 and 9_x. a software driver needs its module or an
 * adapter, other drivers no module. unlike d3d10.dll, bgra support stays,
 * and the direct3d 10 flag is passed on only when given.
 */
D3D10_1API HRESULT WINAPI D3D10CreateDeviceAndSwapChain1(IDXGIAdapter *adapter, D3D10_DRIVER_TYPE type,
                                                         HMODULE software, UINT flags, D3D10_FEATURE_LEVEL1 level,
                                                         UINT sdk, DXGI_SWAP_CHAIN_DESC *desc,
                                                         IDXGISwapChain **swapchain, ID3D10Device1 **device)
{
    if (swapchain)
        *swapchain = nullptr;
    if (!device)
        return E_INVALIDARG;
    *device = nullptr;
    if (sdk != D3D10_1_SDK_VERSION) {
        D3d10SdkMismatch(sdk, D3D10_1_SDK_VERSION);
        return E_INVALIDARG;
    }
    if (level != D3D10_FEATURE_LEVEL_10_0 && level != D3D10_FEATURE_LEVEL_10_1 && (level & 0xF000) != 0x9000)
        return E_INVALIDARG;
    if (type == D3D10_DRIVER_TYPE_SOFTWARE ? !software && !adapter : software != nullptr)
        return E_INVALIDARG;
    if (type != D3D10_DRIVER_TYPE_HARDWARE) {
        if (D3d10DriverType(type) == D3D_DRIVER_TYPE_UNKNOWN)
            return E_INVALIDARG;
        if (D3d10AdapterMismatch(adapter, type))
            return E_INVALIDARG;
    }
    ID3D11Device *d3d11 = nullptr;
    HRESULT hr = D3d10CreateDevice11(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3d10DriverType(type), software,
                                     D3d10DeviceFlags(flags | D3D10_DEVICE_FLAG_D3D10_1), (D3D_FEATURE_LEVEL)level,
                                     desc, swapchain, &d3d11);
    if (SUCCEEDED(hr))
        hr = d3d11->lpVtbl->QueryInterface(d3d11, &IID_ID3D10Device1, (void **)device);
    if (d3d11)
        d3d11->lpVtbl->Release(d3d11);
    if (SUCCEEDED(hr) && *device)
        hr = D3d10Protect((IUnknown *)*device, (*device)->lpVtbl->GetCreationFlags(*device));
    /* an unsupported level reports E_NOINTERFACE. */
    return hr == DXGI_ERROR_UNSUPPORTED ? E_NOINTERFACE : hr;
}

D3D10_1API HRESULT WINAPI D3D10CreateDevice1(IDXGIAdapter *adapter, D3D10_DRIVER_TYPE type, HMODULE software,
                                             UINT flags, D3D10_FEATURE_LEVEL1 level, UINT sdk,
                                             ID3D10Device1 **device)
{
    return D3D10CreateDeviceAndSwapChain1(adapter, type, software, flags, level, sdk, nullptr, nullptr, device);
}

static FARPROC core_entry(const char *name)
{
    HMODULE core = LoadLibraryW(L"d3d10_1core.dll");
    return core ? GetProcAddress(core, name) : nullptr;
}

D3D10API UINT64 WINAPI D3D10GetVersion(void)
{
    UINT64(WINAPI * get)(void) = (UINT64(WINAPI *)(void))(void *)core_entry("D3D10CoreGetVersion");
    return get ? get() : 0;
}

D3D10API HRESULT WINAPI D3D10RegisterLayers(void)
{
    HRESULT(WINAPI * reg)(void) = (HRESULT(WINAPI *)(void))(void *)core_entry("D3D10CoreRegisterLayers");
    return reg ? reg() : E_NOTIMPL;
}

/* gives the load error when d3d10.dll cannot be loaded. */
static HRESULT d3d10_entry(const char *name, FARPROC *out)
{
    HMODULE d3d10 = LoadLibraryW(L"d3d10.dll");
    *out = d3d10 ? GetProcAddress(d3d10, name) : nullptr;
    if (*out)
        return S_OK;
    DWORD error = GetLastError();
    return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
}

#define FORWARD(ret, name, params, args, failed)                                                                       \
    D3D10API ret WINAPI name params                                                                                    \
    {                                                                                                                  \
        FARPROC proc;                                                                                                  \
        HRESULT hr = d3d10_entry(#name, &proc);                                                                        \
        if (FAILED(hr))                                                                                                \
            return failed;                                                                                             \
        return ((ret(WINAPI *) params)(void *)proc) args;                                                              \
    }

FORWARD(HRESULT, D3D10CreateBlob, (SIZE_T size, ID3D10Blob **blob), (size, blob), hr)
FORWARD(HRESULT, D3D10CompileShader,
        (LPCSTR source, SIZE_T length, LPCSTR file, const D3D10_SHADER_MACRO *defines, ID3D10Include *include,
         LPCSTR entry_point, LPCSTR profile, UINT flags, ID3D10Blob **shader, ID3D10Blob **errors),
        (source, length, file, defines, include, entry_point, profile, flags, shader, errors), hr)
FORWARD(HRESULT, D3D10DisassembleShader,
        (const void *bytecode, SIZE_T length, BOOL color, LPCSTR comments, ID3D10Blob **disassembly),
        (bytecode, length, color, comments, disassembly), hr)
FORWARD(HRESULT, D3D10PreprocessShader,
        (LPCSTR source, SIZE_T length, LPCSTR file, const D3D10_SHADER_MACRO *defines, ID3D10Include *include,
         ID3D10Blob **text, ID3D10Blob **errors),
        (source, length, file, defines, include, text, errors), hr)
FORWARD(HRESULT, D3D10ReflectShader, (const void *bytecode, SIZE_T length, ID3D10ShaderReflection **reflector),
        (bytecode, length, reflector), hr)
FORWARD(LPCSTR, D3D10GetVertexShaderProfile, (ID3D10Device * device), (device), nullptr)
FORWARD(LPCSTR, D3D10GetGeometryShaderProfile, (ID3D10Device * device), (device), nullptr)
FORWARD(LPCSTR, D3D10GetPixelShaderProfile, (ID3D10Device * device), (device), nullptr)
FORWARD(HRESULT, D3D10GetInputSignatureBlob, (const void *bytecode, SIZE_T length, ID3D10Blob **blob),
        (bytecode, length, blob), hr)
FORWARD(HRESULT, D3D10GetOutputSignatureBlob, (const void *bytecode, SIZE_T length, ID3D10Blob **blob),
        (bytecode, length, blob), hr)
FORWARD(HRESULT, D3D10GetInputAndOutputSignatureBlob, (const void *bytecode, SIZE_T length, ID3D10Blob **blob),
        (bytecode, length, blob), hr)
FORWARD(HRESULT, D3D10GetShaderDebugInfo, (const void *bytecode, SIZE_T length, ID3D10Blob **blob),
        (bytecode, length, blob), hr)
FORWARD(HRESULT, D3D10CompileEffectFromMemory,
        (void *data, SIZE_T length, LPCSTR file, const D3D10_SHADER_MACRO *defines, ID3D10Include *include,
         UINT hlsl_flags, UINT fx_flags, ID3D10Blob **effect, ID3D10Blob **errors),
        (data, length, file, defines, include, hlsl_flags, fx_flags, effect, errors), hr)
FORWARD(HRESULT, D3D10DisassembleEffect, (ID3D10Effect * effect, BOOL color, ID3D10Blob **disassembly),
        (effect, color, disassembly), hr)
FORWARD(HRESULT, D3D10CreateStateBlock,
        (ID3D10Device * device, D3D10_STATE_BLOCK_MASK *mask, ID3D10StateBlock **block), (device, mask, block), hr)
FORWARD(HRESULT, D3D10StateBlockMaskUnion,
        (D3D10_STATE_BLOCK_MASK * a, D3D10_STATE_BLOCK_MASK *b, D3D10_STATE_BLOCK_MASK *result), (a, b, result), hr)
FORWARD(HRESULT, D3D10StateBlockMaskIntersect,
        (D3D10_STATE_BLOCK_MASK * a, D3D10_STATE_BLOCK_MASK *b, D3D10_STATE_BLOCK_MASK *result), (a, b, result), hr)
FORWARD(HRESULT, D3D10StateBlockMaskDifference,
        (D3D10_STATE_BLOCK_MASK * a, D3D10_STATE_BLOCK_MASK *b, D3D10_STATE_BLOCK_MASK *result), (a, b, result), hr)
FORWARD(HRESULT, D3D10StateBlockMaskEnableCapture,
        (D3D10_STATE_BLOCK_MASK * mask, D3D10_DEVICE_STATE_TYPES type, UINT start, UINT count),
        (mask, type, start, count), hr)
FORWARD(HRESULT, D3D10StateBlockMaskDisableCapture,
        (D3D10_STATE_BLOCK_MASK * mask, D3D10_DEVICE_STATE_TYPES type, UINT start, UINT count),
        (mask, type, start, count), hr)
FORWARD(HRESULT, D3D10StateBlockMaskEnableAll, (D3D10_STATE_BLOCK_MASK * mask), (mask), hr)
FORWARD(HRESULT, D3D10StateBlockMaskDisableAll, (D3D10_STATE_BLOCK_MASK * mask), (mask), hr)
FORWARD(BOOL, D3D10StateBlockMaskGetSetting, (D3D10_STATE_BLOCK_MASK * mask, D3D10_DEVICE_STATE_TYPES type, UINT entry),
        (mask, type, entry), FALSE)

/* effects forward to d3d10.dll, which reads fx_4_1 like fx_4_0. */
FORWARD(HRESULT, D3D10CreateEffectFromMemory,
        (void *data, SIZE_T length, UINT flags, ID3D10Device *device, ID3D10EffectPool *pool, ID3D10Effect **effect),
        (data, length, flags, device, pool, effect), hr)
FORWARD(HRESULT, D3D10CreateEffectPoolFromMemory,
        (void *data, SIZE_T length, UINT flags, ID3D10Device *device, ID3D10EffectPool **pool),
        (data, length, flags, device, pool), hr)
