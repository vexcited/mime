/*
 * the camera behind a symbolic link: the interfaces of a device interface class, the file a link opens, and the
 * requests that file answers (mime/camera.h). the symbolic links are the paths windows' media foundation hands to
 * programs, "\\?\usb#vid_xxxx&pid_xxxx&mi_xx#...#{e5323777-f976-4f5b-9b55-b94699c46e44}\global".
 */
#include "camera.h"

void *CamAlloc(SIZE_T size)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size ? size : 1);
}

void CamFree(void *p)
{
    if (p)
        HeapFree(GetProcessHeap(), 0, p);
}

HRESULT CamEnumeratePaths(const GUID *category, WCHAR **paths)
{
    *paths = nullptr;
    for (int attempt = 0; attempt < 4; attempt++) {
        ULONG chars = 0;
        CONFIGRET cr = CM_Get_Device_Interface_List_SizeW(&chars, (GUID *)category, nullptr,
                                                          CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
        if (cr != CR_SUCCESS)
            return E_FAIL;
        if (chars <= 1)
            return S_OK;
        WCHAR *list = CamAlloc(chars * sizeof(WCHAR));
        if (!list)
            return E_OUTOFMEMORY;
        cr = CM_Get_Device_Interface_ListW((GUID *)category, nullptr, list, chars, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
        if (cr == CR_SUCCESS) {
            *paths = list;
            return S_OK;
        }
        /* a camera plugged in between the two calls makes the list longer than it was. */
        CamFree(list);
        if (cr != CR_BUFFER_SMALL)
            return E_FAIL;
    }
    return E_FAIL;
}

HRESULT CamFriendlyName(PCWSTR path, WCHAR *name, DWORD chars)
{
    DEVPROPTYPE type;
    ULONG size = chars * sizeof(WCHAR);
    CONFIGRET cr = CM_Get_Device_Interface_PropertyW(path, &DEVPKEY_NAME, &type, (PBYTE)name, &size, 0);
    if (cr != CR_SUCCESS || type != DEVPROP_TYPE_STRING)
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    return S_OK;
}

void CamDeviceAddRef(CamDevice *device)
{
    InterlockedIncrement(&device->refs);
}

void CamDeviceRelease(CamDevice *device)
{
    if (InterlockedDecrement(&device->refs))
        return;
    if (device->file != INVALID_HANDLE_VALUE)
        CloseHandle(device->file);
    CamFree(device->formats);
    CamFree(device->path);
    CamFree(device);
}

HRESULT CamIoctl(CamDevice *device, DWORD code, void *in, DWORD in_size, void *out, DWORD out_size, DWORD *returned)
{
    DWORD bytes = 0;
    if (!DeviceIoControl(device->file, code, in, in_size, out, out_size, &bytes, nullptr))
        return HRESULT_FROM_WIN32(GetLastError());
    if (returned)
        *returned = bytes;
    return S_OK;
}

HRESULT CamOpenDevice(PCWSTR path, CamDevice **out)
{
    *out = nullptr;
    CamDevice *d = CamAlloc(sizeof(*d));
    if (!d)
        return E_OUTOFMEMORY;
    d->refs = 1;
    d->file = INVALID_HANDLE_VALUE;
    d->path = CamAlloc((lstrlenW(path) + 1) * sizeof(WCHAR));
    if (!d->path) {
        CamDeviceRelease(d);
        return E_OUTOFMEMORY;
    }
    lstrcpyW(d->path, path);
    d->file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, nullptr);
    if (d->file == INVALID_HANDLE_VALUE) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        CamDeviceRelease(d);
        return hr;
    }
    HRESULT hr = CamIoctl(d, MIME_CAMERA_IOCTL(MIME_CAMERA_IOCTL_INFO), nullptr, 0, &d->info, sizeof(d->info), nullptr);
    if (SUCCEEDED(hr)) {
        /* the formats are asked for until they fit: the first call says how many there are. */
        DWORD room = 64;
        for (int attempt = 0; attempt < 3 && SUCCEEDED(hr); attempt++) {
            DWORD bytes = (DWORD)offsetof(MimeCameraFormatList, format) + room * (DWORD)sizeof(MimeCameraFormat);
            MimeCameraFormatList *list = CamAlloc(bytes);
            if (!list) {
                hr = E_OUTOFMEMORY;
                break;
            }
            hr = CamIoctl(d, MIME_CAMERA_IOCTL(MIME_CAMERA_IOCTL_FORMATS), nullptr, 0, list, bytes, nullptr);
            if (SUCCEEDED(hr) && list->count > list->returned && attempt < 2) {
                room = list->count;
                CamFree(list);
                continue;
            }
            if (SUCCEEDED(hr)) {
                d->formats = CamAlloc(list->returned * sizeof(MimeCameraFormat));
                if (d->formats) {
                    memcpy(d->formats, list->format, list->returned * sizeof(MimeCameraFormat));
                    d->format_count = list->returned;
                } else {
                    hr = E_OUTOFMEMORY;
                }
            }
            CamFree(list);
            break;
        }
    }
    if (FAILED(hr)) {
        CamDeviceRelease(d);
        return hr;
    }
    *out = d;
    return S_OK;
}

void CamSubtype(DWORD fourcc, GUID *subtype)
{
    *subtype = MFMediaType_Video;
    subtype->Data1 = fourcc;
}

BOOL CamFourcc(const GUID *subtype, DWORD *fourcc)
{
    GUID base = MFMediaType_Video;
    base.Data1 = subtype->Data1;
    if (!IsEqualGUID(&base, subtype))
        return FALSE;
    *fourcc = subtype->Data1;
    return TRUE;
}
