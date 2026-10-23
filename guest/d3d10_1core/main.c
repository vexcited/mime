/*
 * d3d10_1core.dll: the old direct3d 10.1 core, whose work d3d11.dll does now.
 * like d3d10core.dll, whose kmt.c it shares, making a device is not
 * implemented. only the argument count of D3D10CoreCreateDevice1 is known.
 */
#include <mime/win32/winbase.h>

DECLSPEC_EXPORT HRESULT WINAPI D3D10CoreCreateDevice1(void *factory, void *adapter, UINT flags, UINT level, UINT sdk,
                                                      void *reserved, void **device)
{
    return E_NOTIMPL;
}
