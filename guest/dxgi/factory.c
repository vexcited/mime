/*
 * the factory: adapters by index, luid or preference, window association,
 * and swap chains, which the given device makes.
 */
#include "dxgi_private.h"

static DxgiFactory *impl(IDXGIFactory7 *iface) { return CONTAINING_RECORD(iface, DxgiFactory, iface); }
static DxgiFactory *media_impl(IDXGIFactoryMedia *iface) { return CONTAINING_RECORD(iface, DxgiFactory, media); }

static const GUID *const FACTORY_IIDS[] = {
    &IID_IUnknown,      &IID_IDXGIObject,   &IID_IDXGIFactory,  &IID_IDXGIFactory1, &IID_IDXGIFactory2,
    &IID_IDXGIFactory3, &IID_IDXGIFactory4, &IID_IDXGIFactory5, &IID_IDXGIFactory6, &IID_IDXGIFactory7,
};

static HRESULT STDMETHODCALLTYPE factory_query(IDXGIFactory7 *iface, REFIID riid, void **out)
{
    if (!out)
        return E_POINTER;
    for (UINT i = 0; i < RTL_NUMBER_OF(FACTORY_IIDS); i++)
        if (IsEqualGUID(riid, FACTORY_IIDS[i])) {
            iface->lpVtbl->AddRef(iface);
            *out = iface;
            return S_OK;
        }
    if (IsEqualGUID(riid, &IID_IDXGIFactoryMedia)) {
        iface->lpVtbl->AddRef(iface);
        *out = &impl(iface)->media;
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE factory_add_ref(IDXGIFactory7 *iface)
{
    return InterlockedIncrement(&impl(iface)->refs);
}

static ULONG STDMETHODCALLTYPE factory_release(IDXGIFactory7 *iface)
{
    DxgiFactory *factory = impl(iface);
    ULONG refs = InterlockedDecrement(&factory->refs);
    if (!refs) {
        MimePrivateClear(&factory->data);
        HeapFree(GetProcessHeap(), 0, factory);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE factory_set_private_data(IDXGIFactory7 *iface, REFGUID guid, UINT size,
                                                          const void *data)
{
    return MimePrivateSet(&impl(iface)->data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE factory_set_private_data_interface(IDXGIFactory7 *iface, REFGUID guid,
                                                                    const IUnknown *unknown)
{
    return MimePrivateSetInterface(&impl(iface)->data, guid, unknown);
}

static HRESULT STDMETHODCALLTYPE factory_get_private_data(IDXGIFactory7 *iface, REFGUID guid, UINT *size,
                                                          void *data)
{
    return MimePrivateGet(&impl(iface)->data, guid, size, data);
}

static HRESULT STDMETHODCALLTYPE factory_get_parent(IDXGIFactory7 *iface, REFIID riid, void **parent)
{
    if (parent)
        *parent = nullptr;
    return E_NOINTERFACE;
}

static HRESULT STDMETHODCALLTYPE factory_enum_adapters1(IDXGIFactory7 *iface, UINT index, IDXGIAdapter1 **adapter)
{
    if (!adapter)
        return DXGI_ERROR_INVALID_CALL;
    *adapter = nullptr;
    const DxgiGpu *gpu = DxgiGpuAt(index);
    if (!gpu)
        return DXGI_ERROR_NOT_FOUND;
    return DxgiCreateAdapter(impl(iface), gpu, &IID_IDXGIAdapter1, (void **)adapter);
}

static HRESULT STDMETHODCALLTYPE factory_enum_adapters(IDXGIFactory7 *iface, UINT index, IDXGIAdapter **adapter)
{
    return factory_enum_adapters1(iface, index, (IDXGIAdapter1 **)adapter);
}

static HRESULT STDMETHODCALLTYPE factory_make_window_association(IDXGIFactory7 *iface, HWND window, UINT flags)
{
    if (flags & ~(DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_PRINT_SCREEN))
        return DXGI_ERROR_INVALID_CALL;
    impl(iface)->association = window;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE factory_get_window_association(IDXGIFactory7 *iface, HWND *window)
{
    if (!window)
        return DXGI_ERROR_INVALID_CALL;
    *window = impl(iface)->association;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE factory_create_swap_chain_for_hwnd(IDXGIFactory7 *iface, IUnknown *device,
                                                                    HWND window, const DXGI_SWAP_CHAIN_DESC1 *desc,
                                                                    const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreen,
                                                                    IDXGIOutput *restrict_to,
                                                                    IDXGISwapChain1 **swapchain)
{
    if (!swapchain)
        return DXGI_ERROR_INVALID_CALL;
    *swapchain = nullptr;
    if (!device || !window || !desc || !IsWindow(window))
        return DXGI_ERROR_INVALID_CALL;
    IMimeDXGIPresenter *presenter = nullptr;
    if (FAILED(device->lpVtbl->QueryInterface(device, &IID_IMimeDXGIPresenter, (void **)&presenter)))
        return DXGI_ERROR_INVALID_CALL;
    HRESULT hr = presenter->lpVtbl->CreateSwapChain(presenter, (IDXGIFactory *)iface, window, desc, fullscreen,
                                                    restrict_to, swapchain);
    presenter->lpVtbl->Release(presenter);
    return hr;
}

static HRESULT STDMETHODCALLTYPE factory_create_swap_chain(IDXGIFactory7 *iface, IUnknown *device,
                                                           DXGI_SWAP_CHAIN_DESC *desc, IDXGISwapChain **swapchain)
{
    if (!swapchain)
        return DXGI_ERROR_INVALID_CALL;
    *swapchain = nullptr;
    if (!desc)
        return DXGI_ERROR_INVALID_CALL;
    DXGI_SWAP_CHAIN_DESC1 desc1 = {
        .Width = desc->BufferDesc.Width,
        .Height = desc->BufferDesc.Height,
        .Format = desc->BufferDesc.Format,
        .SampleDesc = desc->SampleDesc,
        .BufferUsage = desc->BufferUsage,
        .BufferCount = desc->BufferCount,
        .Scaling = DXGI_SCALING_STRETCH,
        .SwapEffect = desc->SwapEffect,
        .AlphaMode = DXGI_ALPHA_MODE_IGNORE,
        .Flags = desc->Flags,
    };
    DXGI_SWAP_CHAIN_FULLSCREEN_DESC fullscreen = {
        .RefreshRate = desc->BufferDesc.RefreshRate,
        .ScanlineOrdering = desc->BufferDesc.ScanlineOrdering,
        .Scaling = desc->BufferDesc.Scaling,
        .Windowed = desc->Windowed,
    };
    return factory_create_swap_chain_for_hwnd(iface, device, desc->OutputWindow, &desc1, &fullscreen, nullptr,
                                              (IDXGISwapChain1 **)swapchain);
}

static HRESULT STDMETHODCALLTYPE factory_create_software_adapter(IDXGIFactory7 *iface, HMODULE module,
                                                                 IDXGIAdapter **adapter)
{
    if (adapter)
        *adapter = nullptr;
    return module ? DXGI_ERROR_UNSUPPORTED : DXGI_ERROR_INVALID_CALL;
}

static BOOL STDMETHODCALLTYPE factory_is_current(IDXGIFactory7 *iface) { return TRUE; }

static BOOL STDMETHODCALLTYPE factory_is_windowed_stereo_enabled(IDXGIFactory7 *iface) { return FALSE; }

static HRESULT STDMETHODCALLTYPE factory_create_swap_chain_for_core_window(IDXGIFactory7 *iface, IUnknown *device,
                                                                           IUnknown *window,
                                                                           const DXGI_SWAP_CHAIN_DESC1 *desc,
                                                                           IDXGIOutput *restrict_to,
                                                                           IDXGISwapChain1 **swapchain)
{
    if (swapchain)
        *swapchain = nullptr;
    return DXGI_ERROR_INVALID_CALL;
}

static HRESULT STDMETHODCALLTYPE factory_get_shared_resource_adapter_luid(IDXGIFactory7 *iface, HANDLE resource,
                                                                          LUID *luid)
{
    return DXGI_ERROR_INVALID_CALL;
}

static DWORD next_cookie(void)
{
    static LONG cookie;
    return (DWORD)InterlockedIncrement(&cookie);
}

static HRESULT STDMETHODCALLTYPE factory_register_window(IDXGIFactory7 *iface, HWND window, UINT message,
                                                         DWORD *cookie)
{
    if (!window || !cookie)
        return DXGI_ERROR_INVALID_CALL;
    *cookie = next_cookie();
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE factory_register_event(IDXGIFactory7 *iface, HANDLE event, DWORD *cookie)
{
    if (!event || !cookie)
        return DXGI_ERROR_INVALID_CALL;
    *cookie = next_cookie();
    return S_OK;
}

static void STDMETHODCALLTYPE factory_unregister(IDXGIFactory7 *iface, DWORD cookie) {}

/* a windowless swap chain made by the device, for a dcomp.dll visual to show. */
static HRESULT create_composition(IDXGIFactory7 *iface, IUnknown *device, HANDLE surface,
                                  const DXGI_SWAP_CHAIN_DESC1 *desc, IDXGIOutput *restrict_to,
                                  IDXGISwapChain1 **swapchain)
{
    if (!swapchain)
        return DXGI_ERROR_INVALID_CALL;
    *swapchain = nullptr;
    if (!device || !desc)
        return DXGI_ERROR_INVALID_CALL;
    IMimeDXGIPresenter *presenter = nullptr;
    if (FAILED(device->lpVtbl->QueryInterface(device, &IID_IMimeDXGIPresenter, (void **)&presenter)))
        return DXGI_ERROR_INVALID_CALL;
    HRESULT hr = presenter->lpVtbl->CreateCompositionSwapChain(presenter, (IDXGIFactory *)iface, surface, desc,
                                                               restrict_to, swapchain);
    presenter->lpVtbl->Release(presenter);
    return hr;
}

static HRESULT STDMETHODCALLTYPE factory_create_swap_chain_for_composition(IDXGIFactory7 *iface, IUnknown *device,
                                                                           const DXGI_SWAP_CHAIN_DESC1 *desc,
                                                                           IDXGIOutput *restrict_to,
                                                                           IDXGISwapChain1 **swapchain)
{
    return create_composition(iface, device, nullptr, desc, restrict_to, swapchain);
}

/* ------------------------------------------------------------------ IDXGIFactoryMedia */

static HRESULT STDMETHODCALLTYPE media_query(IDXGIFactoryMedia *iface, REFIID riid, void **out)
{
    return factory_query(&media_impl(iface)->iface, riid, out);
}

static ULONG STDMETHODCALLTYPE media_add_ref(IDXGIFactoryMedia *iface)
{
    return factory_add_ref(&media_impl(iface)->iface);
}

static ULONG STDMETHODCALLTYPE media_release(IDXGIFactoryMedia *iface)
{
    return factory_release(&media_impl(iface)->iface);
}

/* the swap chain backs the composition surface the handle names, and a visual shows it by that handle. */
static HRESULT STDMETHODCALLTYPE media_create_swap_chain_for_composition_surface_handle(
    IDXGIFactoryMedia *iface, IUnknown *device, HANDLE surface, const DXGI_SWAP_CHAIN_DESC1 *desc,
    IDXGIOutput *restrict_to, IDXGISwapChain1 **swapchain)
{
    if (!surface || surface == INVALID_HANDLE_VALUE) {
        if (swapchain)
            *swapchain = nullptr;
        return DXGI_ERROR_INVALID_CALL;
    }
    return create_composition(&media_impl(iface)->iface, device, surface, desc, restrict_to, swapchain);
}

/* decoding video takes a decoder device, and there is none. */
static HRESULT STDMETHODCALLTYPE media_create_decode_swap_chain_for_composition_surface_handle(
    IDXGIFactoryMedia *iface, IUnknown *device, HANDLE surface, DXGI_DECODE_SWAP_CHAIN_DESC *desc,
    IDXGIResource *yuv_buffers, IDXGIOutput *restrict_to, IDXGIDecodeSwapChain **swapchain)
{
    if (swapchain)
        *swapchain = nullptr;
    if (!swapchain || !device || !desc || !yuv_buffers || !surface)
        return DXGI_ERROR_INVALID_CALL;
    return DXGI_ERROR_UNSUPPORTED;
}

static const IDXGIFactoryMediaVtbl media_vtbl = {
    media_query,
    media_add_ref,
    media_release,
    media_create_swap_chain_for_composition_surface_handle,
    media_create_decode_swap_chain_for_composition_surface_handle,
};

static UINT STDMETHODCALLTYPE factory_get_creation_flags(IDXGIFactory7 *iface) { return impl(iface)->flags; }

static HRESULT STDMETHODCALLTYPE factory_enum_adapter_by_luid(IDXGIFactory7 *iface, LUID luid, REFIID riid,
                                                              void **adapter)
{
    if (!adapter)
        return DXGI_ERROR_INVALID_CALL;
    *adapter = nullptr;
    const DxgiGpu *gpu;
    for (UINT i = 0; (gpu = DxgiGpuAt(i)); i++)
        if (gpu->desc.AdapterLuid.LowPart == luid.LowPart && gpu->desc.AdapterLuid.HighPart == luid.HighPart)
            return DxgiCreateAdapter(impl(iface), gpu, riid, adapter);
    return DXGI_ERROR_NOT_FOUND;
}

static HRESULT STDMETHODCALLTYPE factory_enum_warp_adapter(IDXGIFactory7 *iface, REFIID riid, void **adapter)
{
    if (!adapter)
        return DXGI_ERROR_INVALID_CALL;
    *adapter = nullptr;
    return DxgiCreateAdapter(impl(iface), &DxgiGetVulkan()->warp, riid, adapter);
}

static HRESULT STDMETHODCALLTYPE factory_check_feature_support(IDXGIFactory7 *iface, DXGI_FEATURE feature,
                                                               void *data, UINT size)
{
    if (feature != DXGI_FEATURE_PRESENT_ALLOW_TEARING || !data || size != sizeof(BOOL))
        return DXGI_ERROR_INVALID_CALL;
    *(BOOL *)data = TRUE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE factory_enum_adapter_by_gpu_preference(IDXGIFactory7 *iface, UINT index,
                                                                        DXGI_GPU_PREFERENCE preference,
                                                                        REFIID riid, void **adapter)
{
    if (!adapter || preference > DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE)
        return DXGI_ERROR_INVALID_CALL;
    *adapter = nullptr;
    const DxgiVulkan *vk = DxgiGetVulkan();
    if (index > vk->count)
        return DXGI_ERROR_NOT_FOUND;
    /* software adapters sort last whatever the preference. */
    if (index == vk->count)
        return DxgiCreateAdapter(impl(iface), &vk->warp, riid, adapter);
    /* adapters are already discrete first, minimum power reverses the kinds. */
    const DxgiGpu *order[RTL_NUMBER_OF(vk->gpus)];
    UINT n = 0;
    if (preference == DXGI_GPU_PREFERENCE_MINIMUM_POWER)
        for (UINT i = 0; i < vk->count; i++)
            if (vk->gpus[i].properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
                order[n++] = &vk->gpus[i];
    for (UINT i = 0; i < vk->count; i++)
        if (preference != DXGI_GPU_PREFERENCE_MINIMUM_POWER ||
            vk->gpus[i].properties.deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
            order[n++] = &vk->gpus[i];
    return DxgiCreateAdapter(impl(iface), order[index], riid, adapter);
}

static HRESULT STDMETHODCALLTYPE factory_register_adapters_changed_event(IDXGIFactory7 *iface, HANDLE event,
                                                                         DWORD *cookie)
{
    return factory_register_event(iface, event, cookie);
}

static HRESULT STDMETHODCALLTYPE factory_unregister_adapters_changed_event(IDXGIFactory7 *iface, DWORD cookie)
{
    return S_OK;
}

static const IDXGIFactory7Vtbl factory_vtbl = {
    factory_query,
    factory_add_ref,
    factory_release,
    factory_set_private_data,
    factory_set_private_data_interface,
    factory_get_private_data,
    factory_get_parent,
    factory_enum_adapters,
    factory_make_window_association,
    factory_get_window_association,
    factory_create_swap_chain,
    factory_create_software_adapter,
    factory_enum_adapters1,
    factory_is_current,
    factory_is_windowed_stereo_enabled,
    factory_create_swap_chain_for_hwnd,
    factory_create_swap_chain_for_core_window,
    factory_get_shared_resource_adapter_luid,
    factory_register_window,
    factory_register_event,
    factory_unregister,
    factory_register_window,
    factory_register_event,
    factory_unregister,
    factory_create_swap_chain_for_composition,
    factory_get_creation_flags,
    factory_enum_adapter_by_luid,
    factory_enum_warp_adapter,
    factory_check_feature_support,
    factory_enum_adapter_by_gpu_preference,
    factory_register_adapters_changed_event,
    factory_unregister_adapters_changed_event,
};

HRESULT DxgiCreateFactory(UINT flags, REFIID riid, void **out)
{
    DxgiFactory *factory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*factory));
    if (!factory)
        return E_OUTOFMEMORY;
    factory->iface.lpVtbl = &factory_vtbl;
    factory->media.lpVtbl = &media_vtbl;
    factory->refs = 1;
    factory->flags = flags;
    HRESULT hr = factory_query(&factory->iface, riid, out);
    factory_release(&factory->iface);
    return hr;
}
