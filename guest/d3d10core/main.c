/*
 * d3d10core.dll: the old direct3d 10 core, whose work d3d11.dll does now.
 * like windows, making a device is not implemented. the rest is in kmt.c,
 * shared with d3d10_1core.dll. only the argument count of
 * D3D10CoreCreateDevice is known.
 */
#include <mime/win32/winbase.h>

DECLSPEC_EXPORT HRESULT WINAPI D3D10CoreCreateDevice(void *factory, void *adapter, UINT flags, UINT level,
                                                     void **device)
{
    return E_NOTIMPL;
}
