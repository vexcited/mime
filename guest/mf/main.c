/*
 * mf.dll, the media foundation pipeline: here only what programs ask of it
 * on a machine without capture devices, the enumeration of cameras and
 * microphones and the creation of their sources. see docs/mfplat.md.
 */
#define MIME_BUILD_MF
#include <mime/nt.h>
#include <mime/win32/winbase.h>
#include <mime/win32/mfidl.h>

typedef struct IMFShutdown IMFShutdown;
#define IMFShutdown_METHODS(T)                                                                                         \
    MIME_IUNKNOWN_METHODS(T)                                                                                           \
    HRESULT(STDMETHODCALLTYPE *Shutdown)(T * This);                                                                    \
    HRESULT(STDMETHODCALLTYPE *GetShutdownStatus)(T * This, DWORD *status);
MIME_COM_INTERFACE(IMFShutdown)
MIME_DEFINE_GUID(IID_IMFShutdown, 0x97EC2EA4, 0x0E42, 0x4937, 0x97, 0xAC, 0x9D, 0x6D, 0x32, 0x88, 0x24, 0xE1);

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void *reserved)
{
    return TRUE;
}

/* the capture class the attributes ask for, video or audio, or the failure. */
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

/* no capture source is implemented, a camera or a microphone: both lists are empty. */
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
    return S_OK;
}

/*
 * a camera needs its symbolic link, which names no device here. a
 * microphone may name its endpoint, or a role for the default one, but not
 * both; the endpoint exists in mmdevapi but has no media source here.
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
    if (video) {
        UINT32 length;
        hr = attributes->lpVtbl->GetStringLength(attributes, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                                                 &length);
        return FAILED(hr) ? hr : HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
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
