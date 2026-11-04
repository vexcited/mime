#include "wg_private.h"
#include <mime/win32/shellscalingapi.h>
#include <rtl.h>

#define ICC_SIGNATURE 0x70736361 /* "acsp" as the bytes of the file read it. */
#define ERROR_INVALID_PROFILE_ 2011L

typedef struct COLOR_INFO {
    WRT_AGILE Object;
    WG_COLOR Color;
} COLOR_INFO;

static const WG_COLOR *color_of(void *iface)
{
    return &((COLOR_INFO *)((WRT_IFACE *)iface)->Owner)->Color;
}

WRT_AGILE_THUNKS(IAdvancedColorInfo)

static HRESULT STDMETHODCALLTYPE info_get_kind(IAdvancedColorInfo *This, AdvancedColorKind *value)
{
    *value = color_of(This)->Kind;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE info_get_red(IAdvancedColorInfo *This, WgPoint *value)
{
    *value = color_of(This)->Red;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE info_get_green(IAdvancedColorInfo *This, WgPoint *value)
{
    *value = color_of(This)->Green;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE info_get_blue(IAdvancedColorInfo *This, WgPoint *value)
{
    *value = color_of(This)->Blue;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE info_get_white(IAdvancedColorInfo *This, WgPoint *value)
{
    *value = color_of(This)->White;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE info_get_max(IAdvancedColorInfo *This, float *value)
{
    *value = color_of(This)->MaxLuminance;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE info_get_min(IAdvancedColorInfo *This, float *value)
{
    *value = color_of(This)->MinLuminance;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE info_get_full_frame(IAdvancedColorInfo *This, float *value)
{
    *value = color_of(This)->MaxFullFrameLuminance;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE info_get_sdr_white(IAdvancedColorInfo *This, float *value)
{
    *value = color_of(This)->SdrWhiteLevel;
    return S_OK;
}

/* a format or kind Windows does not know is not supported, without an error. */
static HRESULT STDMETHODCALLTYPE info_is_hdr_metadata(IAdvancedColorInfo *This, HdrMetadataFormat format,
                                                      BOOLEAN *result)
{
    *result = (UINT32)format <= HdrMetadataFormat_Hdr10Plus && (color_of(This)->HdrMetadata >> format & 1);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE info_is_kind_available(IAdvancedColorInfo *This, AdvancedColorKind kind,
                                                        BOOLEAN *result)
{
    *result = (UINT32)kind <= AdvancedColorKind_HighDynamicRange && (color_of(This)->Available >> kind & 1);
    return S_OK;
}

static const IAdvancedColorInfoVtbl info_vtbl = {
    WRT_INSPECTABLE_ENTRIES(IAdvancedColorInfo),
    info_get_kind,
    info_get_red,
    info_get_green,
    info_get_blue,
    info_get_white,
    info_get_max,
    info_get_min,
    info_get_full_frame,
    info_get_sdr_white,
    info_is_hdr_metadata,
    info_is_kind_available,
};

static void info_destroy(WRT_OBJECT *object)
{
    HeapFree(GetProcessHeap(), 0, object);
}

static const WRT_CLASS g_info_class = {L"Windows.Graphics.Display.AdvancedColorInfo",
                                       2,
                                       {&IID_IAdvancedColorInfo, &IID_IWeakReferenceSource},
                                       1u << 1,
                                       info_destroy};

HRESULT WgAdvancedColorCreate(const WG_COLOR *color, IAdvancedColorInfo **result)
{
    *result = nullptr;
    COLOR_INFO *info = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*info));
    if (!info)
        return E_OUTOFMEMORY;
    const void *vtables[2] = {&info_vtbl, &WrtWeakSourceVtbl};
    WrtAgileInit(&info->Object, &g_info_class, vtables);
    info->Color = *color;
    *result = (IAdvancedColorInfo *)WrtAgileIface(&info->Object);
    return S_OK;
}

/* ------------------------------------------------------------------ the color profile */

static const WRT_ASYNC_TYPE g_profile_type = {
    L"Windows.Foundation.IAsyncOperation`1<Windows.Storage.Streams.IRandomAccessStream>",
    &IID_IAsyncOperationRandomAccessStream, WRT_RESULT_OBJECT, FALSE};

static HRESULT last_error(void)
{
    DWORD error = GetLastError();
    return error ? HRESULT_FROM_WIN32(error) : E_FAIL;
}

static HRESULT check_signature(LPCWSTR path)
{
    HANDLE file =
        CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return last_error();
    DWORD signature = 0, read = 0;
    HRESULT hr = S_OK;
    if (SetFilePointer(file, 36, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
        !ReadFile(file, &signature, sizeof(signature), &read, nullptr))
        hr = last_error();
    else if (read != sizeof(signature) || signature != ICC_SIGNATURE)
        hr = HRESULT_FROM_WIN32(ERROR_INVALID_PROFILE_);
    CloseHandle(file);
    return hr;
}

/* the profile is opened as it is asked for; a failure is the error of the operation. */
HRESULT WgColorProfileAsync(HMONITOR monitor, void **operation)
{
    WCHAR path[MAX_PATH];
    IRandomAccessStream *stream = nullptr;
    HRESULT hr = WgColorProfilePath(monitor, path, MAX_PATH);
    if (SUCCEEDED(hr) && !path[0])
        hr = HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    if (SUCCEEDED(hr))
        hr = check_signature(path);
    if (SUCCEEDED(hr))
        hr = CreateRandomAccessStreamOnFile(path, FileAccessMode_Read, &IID_IRandomAccessStream, (void **)&stream);
    return WrtAsyncCompleted(&g_profile_type, hr, 0, (IUnknown *)stream, operation);
}
