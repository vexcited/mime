/* d3d9.dll exports. nothing listens to the D3DPERF markers. */
#include "d3d9_private.h"

int _fltused = 0x9875;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

void D9Log(const char *format, ...)
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

HRESULT D9CreateDirect3D(UINT sdk, BOOL ex, IDirect3D9Ex **out);

__declspec(dllexport) IDirect3D9 *WINAPI Direct3DCreate9(UINT sdk)
{
    IDirect3D9Ex *d3d;
    return SUCCEEDED(D9CreateDirect3D(sdk, FALSE, &d3d)) ? (IDirect3D9 *)d3d : nullptr;
}

__declspec(dllexport) HRESULT WINAPI Direct3DCreate9Ex(UINT sdk, IDirect3D9Ex **out)
{
    if (!out)
        return D3DERR_INVALIDCALL;
    *out = nullptr;
    return D9CreateDirect3D(sdk, TRUE, out);
}

/* the given direct3d 12 device and queue are ignored. */
__declspec(dllexport) IDirect3D9 *WINAPI Direct3DCreate9On12(UINT sdk, void *args, UINT count)
{
    return Direct3DCreate9(sdk);
}

__declspec(dllexport) HRESULT WINAPI Direct3DCreate9On12Ex(UINT sdk, void *args, UINT count, IDirect3D9Ex **out)
{
    return Direct3DCreate9Ex(sdk, out);
}

__declspec(dllexport) void *WINAPI Direct3DShaderValidatorCreate9(void) { return nullptr; }

__declspec(dllexport) void WINAPI PSGPError(void *device, DWORD error, DWORD value) {}

__declspec(dllexport) void WINAPI PSGPSampleTexture(void *device, DWORD stage, float coords[4], DWORD count,
                                                    float out[4])
{
}

__declspec(dllexport) int WINAPI DebugSetLevel(void) { return 0; }

__declspec(dllexport) void WINAPI DebugSetMute(void) {}

__declspec(dllexport) int WINAPI Direct3D9EnableMaximizedWindowedModeShim(UINT enable) { return 0; }

/* like windows with no capture tool attached, there is no event depth to report. */
__declspec(dllexport) int WINAPI D3DPERF_BeginEvent(D3DCOLOR color, LPCWSTR name) { return -1; }

__declspec(dllexport) int WINAPI D3DPERF_EndEvent(void) { return -1; }

__declspec(dllexport) void WINAPI D3DPERF_SetMarker(D3DCOLOR color, LPCWSTR name) {}

__declspec(dllexport) void WINAPI D3DPERF_SetRegion(D3DCOLOR color, LPCWSTR name) {}

__declspec(dllexport) BOOL WINAPI D3DPERF_QueryRepeatFrame(void) { return FALSE; }

__declspec(dllexport) void WINAPI D3DPERF_SetOptions(DWORD options) {}

__declspec(dllexport) DWORD WINAPI D3DPERF_GetStatus(void) { return 0; }
