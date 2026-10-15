/*
 * d3d10core.dll: once the core of direct3d 10, whose work d3d11.dll does
 * now. making a device answers that it is not implemented, as windows'
 * does; the rest is kmt.c's, shared with d3d10_1core.dll. only the count
 * of D3D10CoreCreateDevice's arguments is known, from the 32-bit dll.
 */
#include <mime/win32/winbase.h>

DECLSPEC_EXPORT HRESULT WINAPI D3D10CoreCreateDevice(void *factory, void *adapter, UINT flags, UINT level,
                                                     void **device)
{
    return E_NOTIMPL;
}
