/* directwrite is unavailable; clients can use bundled fonts through freetype. */
#include <mime/win32/objbase.h>

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved) { return TRUE; }

DECLSPEC_EXPORT HRESULT WINAPI DWriteCreateFactory(UINT type, REFIID iid, IUnknown **out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    return E_NOINTERFACE;
}
