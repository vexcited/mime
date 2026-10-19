/*
 * Windows.Foundation.Metadata.ApiInformation: what programs ask before they use a runtime class, and what the
 * classes of Mime answer. a contract is present up to the version windows 11 of build 26300 has; a type is present
 * when combase activates it; members are present only where the class here has them.
 */
#include "combase_private.h"
#include <mime/winrt_object.h>

const WCHAR CbApiInformationClass[] = L"Windows.Foundation.Metadata.ApiInformation";

MIME_DEFINE_GUID(IID_IApiInformationStatics, 0x997439FE, 0xF681, 0x4A11, 0xB4, 0x16, 0xC1, 0x3A, 0x47, 0xE8, 0xBA, 0x36);

typedef struct IApiInformationStatics IApiInformationStatics;
#define IApiInformationStatics_METHODS(T)                                                                              \
    MIME_INSPECTABLE_METHODS(T)                                                                                        \
    HRESULT(STDMETHODCALLTYPE *IsTypePresent)(T * This, HSTRING type, BOOLEAN * value);                                \
    HRESULT(STDMETHODCALLTYPE *IsMethodPresent)(T * This, HSTRING type, HSTRING method, BOOLEAN * value);              \
    HRESULT(STDMETHODCALLTYPE *IsMethodPresentWithArity)(T * This, HSTRING type, HSTRING method, UINT32 arity,         \
                                                         BOOLEAN *value);                                              \
    HRESULT(STDMETHODCALLTYPE *IsEventPresent)(T * This, HSTRING type, HSTRING name, BOOLEAN * value);                 \
    HRESULT(STDMETHODCALLTYPE *IsPropertyPresent)(T * This, HSTRING type, HSTRING name, BOOLEAN * value);              \
    HRESULT(STDMETHODCALLTYPE *IsReadOnlyPropertyPresent)(T * This, HSTRING type, HSTRING name, BOOLEAN * value);      \
    HRESULT(STDMETHODCALLTYPE *IsWriteablePropertyPresent)(T * This, HSTRING type, HSTRING name, BOOLEAN * value);     \
    HRESULT(STDMETHODCALLTYPE *IsEnumNamedValuePresent)(T * This, HSTRING type, HSTRING name, BOOLEAN * value);        \
    HRESULT(STDMETHODCALLTYPE *IsApiContractPresentByMajor)(T * This, HSTRING contract, UINT16 major, BOOLEAN * value); \
    HRESULT(STDMETHODCALLTYPE *IsApiContractPresentByMajorAndMinor)(T * This, HSTRING contract, UINT16 major,          \
                                                                    UINT16 minor, BOOLEAN *value);
MIME_COM_INTERFACE(IApiInformationStatics)

WRT_INSPECTABLE_THUNKS(IApiInformationStatics)

static BOOL same(HSTRING string, LPCWSTR text)
{
    UINT32 length;
    LPCWSTR raw = WindowsGetStringRawBuffer(string, &length);
    return length == wcslen(text) && !memcmp(raw, text, length * sizeof(WCHAR));
}

/* the greatest version of the contracts windows 11 of build 26300 has, 0 for a contract it has not. */
static UINT16 contract_version(HSTRING contract)
{
    static const struct {
        LPCWSTR Name;
        UINT16 Major;
    } contracts[] = {
        {L"Windows.Foundation.UniversalApiContract", 19},
        {L"Windows.Foundation.FoundationContract", 4},
        {L"Windows.Graphics.Capture.GraphicsCaptureContract", 1},
        {L"Windows.Networking.Connectivity.WwanContract", 3},
        {L"Windows.UI.ViewManagement.ViewManagementViewScalingContract", 1},
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(contracts); i++)
        if (same(contract, contracts[i].Name))
            return contracts[i].Major;
    return 0;
}

static BOOL type_present(HSTRING type)
{
    static const LPCWSTR types[] = {
        L"Windows.Graphics.Capture.GraphicsCaptureSession", L"Windows.Graphics.Capture.GraphicsCaptureItem",
        L"Windows.Graphics.Capture.Direct3D11CaptureFramePool", L"Windows.Graphics.Capture.Direct3D11CaptureFrame",
        L"Windows.UI.ViewManagement.UISettings",
    };
    for (ULONG i = 0; i < RTL_NUMBER_OF(types); i++)
        if (same(type, types[i]))
            return TRUE;
    return FALSE;
}

static BOOL property_present(HSTRING type, HSTRING name)
{
    return same(type, L"Windows.Graphics.Capture.GraphicsCaptureSession") &&
           (same(name, L"IsCursorCaptureEnabled") || same(name, L"IsBorderRequired"));
}

static HRESULT STDMETHODCALLTYPE info_type(IApiInformationStatics *This, HSTRING type, BOOLEAN *value)
{
    if (!value)
        return E_POINTER;
    if (!type)
        return E_INVALIDARG;
    *value = type_present(type);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE info_member(IApiInformationStatics *This, HSTRING type, HSTRING name, BOOLEAN *value)
{
    if (!value)
        return E_POINTER;
    if (!type || !name)
        return E_INVALIDARG;
    *value = FALSE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE info_method_arity(IApiInformationStatics *This, HSTRING type, HSTRING name,
                                                   UINT32 arity, BOOLEAN *value)
{
    return info_member(This, type, name, value);
}

static HRESULT STDMETHODCALLTYPE info_property(IApiInformationStatics *This, HSTRING type, HSTRING name, BOOLEAN *value)
{
    if (!value)
        return E_POINTER;
    if (!type || !name)
        return E_INVALIDARG;
    *value = property_present(type, name);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE info_contract(IApiInformationStatics *This, HSTRING contract, UINT16 major, BOOLEAN *value)
{
    if (!value)
        return E_POINTER;
    if (!contract)
        return E_INVALIDARG;
    UINT16 have = contract_version(contract);
    *value = have && major <= have;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE info_contract_minor(IApiInformationStatics *This, HSTRING contract, UINT16 major,
                                                     UINT16 minor, BOOLEAN *value)
{
    return info_contract(This, contract, major, value);
}

static const IApiInformationStaticsVtbl info_vtbl = {
    WRT_INSPECTABLE_ENTRIES(IApiInformationStatics), info_type, info_member, info_method_arity, info_member, info_property,
    info_property, info_property, info_member, info_contract, info_contract_minor};

static void factory_destroy(WRT_OBJECT *object)
{
    object->Refs = 1;
}

static const WRT_CLASS info_class = {CbApiInformationClass, 1, {&IID_IApiInformationStatics}, 0, factory_destroy};
static WRT_OBJECT g_info;
static INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK make(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
    const void *vtables[1] = {&info_vtbl};
    WrtInit(&g_info, &info_class, vtables);
    return TRUE;
}

HRESULT CbApiInformationFactory(REFIID riid, void **factory)
{
    InitOnceExecuteOnce(&g_once, make, nullptr, nullptr);
    return WrtQuery(&g_info.Ifaces[0], riid, factory);
}
