/*
 * d3d10_1core.dll: once the core of direct3d 10.1, whose work d3d11.dll
 * does now. as d3d10core.dll, whose kmt.c it shares, making a device is
 * not implemented. only the count of D3D10CoreCreateDevice1's arguments is
 * known, from the 32-bit dll.
 */
#include <mime/win32/winbase.h>

DECLSPEC_EXPORT HRESULT WINAPI D3D10CoreCreateDevice1(void *factory, void *adapter, UINT flags, UINT level, UINT sdk,
                                                      void *reserved, void **device)
{
    return E_NOTIMPL;
}
