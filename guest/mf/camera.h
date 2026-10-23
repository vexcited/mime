/* shared by mf.dll's capture sources: the camera behind a symbolic link and the objects over it. */
#ifndef MF_CAMERA_H
#define MF_CAMERA_H

#define MIME_BUILD_MF
#include <mime/nt.h>
#include <mime/win32/winbase.h>
#include <mime/win32/mfapi.h>
#include <mime/win32/mfidl.h>
#include <mime/win32/strmctl.h>
#include <mime/win32/cfgmgr32.h>
#include <mime/win32/mimecamera.h>
#include <mime/camera.h>

typedef struct IMFShutdown IMFShutdown;
#define IMFShutdown_METHODS(T)                                                                                         \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Shutdown)(T * This);                                                                    \
    HRESULT(STDMETHODCALLTYPE *GetShutdownStatus)(T * This, DWORD *status);
MIME_COM_INTERFACE(IMFShutdown)
MIME_DEFINE_GUID(IID_IMFShutdown, 0x97EC2EA4, 0x0E42, 0x4937, 0x97, 0xAC, 0x9D, 0x6D, 0x32, 0x88, 0x24, 0xE1);

typedef struct CamDevice {
    LONG refs;
    HANDLE file;
    MimeCameraInfo info;
    MimeCameraFormat *formats;
    DWORD format_count;
    WCHAR *path;
} CamDevice;

void *CamAlloc(SIZE_T size);
void CamFree(void *p);

/* the camera symbolic links of a device interface class as a multi-sz, free with CamFree. */
HRESULT CamEnumeratePaths(const GUID *category, WCHAR **paths);
HRESULT CamFriendlyName(PCWSTR path, WCHAR *name, DWORD chars);

HRESULT CamOpenDevice(PCWSTR path, CamDevice **device);
void CamDeviceAddRef(CamDevice *device);
void CamDeviceRelease(CamDevice *device);
/* win32 errors come back as hresults. */
HRESULT CamIoctl(CamDevice *device, DWORD code, void *in, DWORD in_size, void *out, DWORD out_size, DWORD *returned);

void CamSubtype(DWORD fourcc, GUID *subtype);
BOOL CamFourcc(const GUID *subtype, DWORD *fourcc);

HRESULT CamCreateSource(IMFAttributes *attributes, IMFMediaSource **source);
HRESULT CamCreateActivate(IMFAttributes *attributes, IMFActivate **activate);

#endif
