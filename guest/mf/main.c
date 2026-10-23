/*
 * mf.dll, only the capture device functions. cameras come from the host, see source.c. microphone lists are empty
 * because their endpoints have no media source on top of them.
 */
#include "camera.h"

/* the compiler references it from code using floating point, without a crt to define it. */
int _fltused = 0x9875;

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    return TRUE;
}

static HRESULT source_type(IMFAttributes *attributes, BOOL *video)
{
    GUID type;
    HRESULT hr = attributes->lpVtbl->GetGUID(attributes, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &type);
    if (FAILED(hr))
        return hr;
    if (IsEqualGUID(&type, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID))
        *video = TRUE;
    else if (IsEqualGUID(&type, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_AUDCAP_GUID))
        *video = FALSE;
    else
        return E_INVALIDARG;
    return S_OK;
}

static HRESULT activate_camera(PCWSTR link, IMFActivate **out)
{
    WCHAR name[128];
    HRESULT hr = CamFriendlyName(link, name, RTL_NUMBER_OF(name));
    if (FAILED(hr))
        return hr;
    IMFAttributes *attributes;
    hr = MFCreateAttributes(&attributes, 3);
    if (FAILED(hr))
        return hr;
    attributes->lpVtbl->SetGUID(attributes, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                                &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    attributes->lpVtbl->SetString(attributes, &MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, name);
    attributes->lpVtbl->SetString(attributes, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, link);
    hr = CamCreateActivate(attributes, out);
    attributes->lpVtbl->Release(attributes);
    return hr;
}

/*
 * one activation object per interface of the camera category. the category attribute selects another interface
 * class instead, such as sensor cameras, which are none here. microphones have no media source, so their list is
 * empty.
 */
HRESULT WINAPI MFEnumDeviceSources(IMFAttributes *attributes, IMFActivate ***sources, UINT32 *count)
{
    if (!attributes)
        return E_INVALIDARG;
    if (!sources || !count)
        return E_POINTER;
    BOOL video;
    HRESULT hr = source_type(attributes, &video);
    if (FAILED(hr))
        return hr;
    *sources = nullptr;
    *count = 0;
    if (!video)
        return S_OK;
    GUID category = KSCATEGORY_VIDEO_CAMERA;
    attributes->lpVtbl->GetGUID(attributes, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_CATEGORY, &category);
    WCHAR *paths = nullptr;
    hr = CamEnumeratePaths(&category, &paths);
    if (FAILED(hr) || !paths)
        return hr;
    UINT32 n = 0;
    for (WCHAR *p = paths; *p; p += lstrlenW(p) + 1)
        n++;
    IMFActivate **list = CoTaskMemAlloc(n * sizeof(*list));
    if (!list) {
        CamFree(paths);
        return E_OUTOFMEMORY;
    }
    UINT32 made = 0;
    for (WCHAR *p = paths; *p; p += lstrlenW(p) + 1)
        if (SUCCEEDED(activate_camera(p, &list[made])))
            made++;
    CamFree(paths);
    if (!made) {
        CoTaskMemFree(list);
        return S_OK;
    }
    *sources = list;
    *count = made;
    return S_OK;
}

/*
 * a camera needs its symbolic link, one that names no camera fails with the error of opening it. a microphone may
 * name its endpoint or a role for the default one, not both. the endpoint exists in mmdevapi but has no media source
 * here.
 */
HRESULT WINAPI MFCreateDeviceSource(IMFAttributes *attributes, IMFMediaSource **source)
{
    if (!attributes)
        return E_INVALIDARG;
    if (!source)
        return E_POINTER;
    *source = nullptr;
    BOOL video;
    HRESULT hr = source_type(attributes, &video);
    if (FAILED(hr))
        return hr;
    if (video)
        return CamCreateSource(attributes, source);
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

/* the activation object keeps a copy of the attributes and makes the source when asked. */
HRESULT WINAPI MFCreateDeviceSourceActivate(IMFAttributes *attributes, IMFActivate **activate)
{
    if (!attributes)
        return E_INVALIDARG;
    if (!activate)
        return E_POINTER;
    *activate = nullptr;
    BOOL video;
    HRESULT hr = source_type(attributes, &video);
    if (FAILED(hr))
        return hr;
    IMFAttributes *copy;
    UINT32 count = 0;
    attributes->lpVtbl->GetCount(attributes, &count);
    hr = MFCreateAttributes(&copy, count);
    if (SUCCEEDED(hr))
        hr = attributes->lpVtbl->CopyAllItems(attributes, copy);
    if (SUCCEEDED(hr))
        hr = CamCreateActivate(copy, activate);
    if (copy)
        copy->lpVtbl->Release(copy);
    return hr;
}

HRESULT WINAPI MFGetService(IUnknown *object, REFGUID service, REFIID riid, void **result)
{
    if (!object || !result)
        return E_POINTER;
    IMFGetService *get_service = nullptr;
    if (FAILED(object->lpVtbl->QueryInterface(object, &IID_IMFGetService, (void **)&get_service)))
        return MF_E_UNSUPPORTED_SERVICE;
    HRESULT hr = get_service->lpVtbl->GetService(get_service, service, riid, result);
    get_service->lpVtbl->Release(get_service);
    return hr;
}

/* objects without IMFShutdown need nothing. */
HRESULT WINAPI MFShutdownObject(IUnknown *object)
{
    if (!object)
        return S_OK;
    IMFShutdown *shutdown = nullptr;
    if (FAILED(object->lpVtbl->QueryInterface(object, &IID_IMFShutdown, (void **)&shutdown)))
        return S_OK;
    HRESULT hr = shutdown->lpVtbl->Shutdown(shutdown);
    shutdown->lpVtbl->Release(shutdown);
    return hr;
}
