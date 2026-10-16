/* directinput devices are not exposed by the input backend yet. */
#include <mime/win32/objbase.h>

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

DECLSPEC_EXPORT HRESULT WINAPI DirectInput8Create(HINSTANCE instance, DWORD version, REFIID iid, void **out, IUnknown *outer)
{
    if (!out)
        return E_POINTER;
    *out = nullptr;
    return E_NOTIMPL;
}
