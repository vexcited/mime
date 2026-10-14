/* directx adapters are unavailable; opengl uses its own host gpu bridge. */
#include <mime/win32/objbase.h>

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

DECLSPEC_EXPORT HRESULT WINAPI CreateDXGIFactory1(REFIID iid, void **out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    return (HRESULT)0x887a0004;
}
