/* d3d8.dll exports: the Direct3D 8 object and the shader validators windows ships with it. */
#include "d3d8_private.h"

int _fltused = 0x9875;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(module);
    return TRUE;
}

void D8Log(const char *format, ...)
{
    char text[512];
    va_list ap;
    va_start(ap, format);
    int n = _vsnprintf(text, sizeof(text) - 2, format, ap);
    va_end(ap);
    if (n < 0 || n > (int)sizeof(text) - 2)
        n = sizeof(text) - 2;
    text[n++] = '\n';
    text[n] = 0;
    OutputDebugStringA(text);
}

/* DirectX 8.0 programs pass 120, 8.1 ones 220, and later sdks up to 319. */
__declspec(dllexport) IDirect3D8 *WINAPI Direct3DCreate8(UINT sdk)
{
    if (sdk != 120 && (sdk < 220 || sdk > 319))
        return nullptr;
    IDirect3D8 *d3d;
    return SUCCEEDED(D8CreateDirect3D(sdk, &d3d)) ? d3d : nullptr;
}

__declspec(dllexport) HRESULT WINAPI ValidateVertexShader(const DWORD *code, const DWORD *declaration,
                                                           const D3DCAPS8 *caps, DWORD flags, char **errors)
{
    return D8ValidateVertexShader(code, declaration, caps, flags & 1, errors);
}

__declspec(dllexport) HRESULT WINAPI ValidatePixelShader(const DWORD *code, const D3DCAPS8 *caps, DWORD flags,
                                                          char **errors)
{
    return D8ValidatePixelShader(code, caps, flags & 1, errors);
}

/* windows sets a flag nothing reads. */
__declspec(dllexport) void WINAPI Direct3D8EnableMaximizedWindowedModeShim(BOOL enable) {}

__declspec(dllexport) void WINAPI DebugSetMute(void) {}
