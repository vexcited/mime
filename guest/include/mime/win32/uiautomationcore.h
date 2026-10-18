/* the provider side of ui automation, uiautomationcoreapi.h. */
#ifndef MIME_WIN32_UIAUTOMATIONCORE_H
#define MIME_WIN32_UIAUTOMATIONCORE_H

#include <mime/win32/objbase.h>
#include <mime/win32/oleauto.h>

#ifndef UIAAPI
#define UIAAPI DECLSPEC_IMPORT
#endif

typedef int PROPERTYID, EVENTID, PATTERNID;

#define UIA_E_ELEMENTNOTAVAILABLE ((HRESULT)0x80040201L)
#define UiaRootObjectId (-25)
#define UIA_NativeWindowHandlePropertyId 30020
#define UIA_ProviderDescriptionPropertyId 30107

typedef enum ProviderOptions {
    ProviderOptions_ClientSideProvider = 0x1,
    ProviderOptions_ServerSideProvider = 0x2,
    ProviderOptions_NonClientAreaProvider = 0x4,
    ProviderOptions_OverrideProvider = 0x8,
    ProviderOptions_ProviderOwnsSetFocus = 0x10,
    ProviderOptions_UseComThreading = 0x20,
} ProviderOptions;

typedef enum StructureChangeType {
    StructureChangeType_ChildAdded = 0,
    StructureChangeType_ChildRemoved = 1,
    StructureChangeType_ChildrenInvalidated = 2,
    StructureChangeType_ChildrenBulkAdded = 3,
    StructureChangeType_ChildrenBulkRemoved = 4,
    StructureChangeType_ChildrenReordered = 5,
} StructureChangeType;

typedef struct IRawElementProviderSimple IRawElementProviderSimple;
typedef struct IRawElementProviderSimpleVtbl {
    HRESULT(STDMETHODCALLTYPE *QueryInterface)(IRawElementProviderSimple *This, REFIID riid, void **object);
    ULONG(STDMETHODCALLTYPE *AddRef)(IRawElementProviderSimple *This);
    ULONG(STDMETHODCALLTYPE *Release)(IRawElementProviderSimple *This);
    HRESULT(STDMETHODCALLTYPE *get_ProviderOptions)(IRawElementProviderSimple *This, ProviderOptions *options);
    HRESULT(STDMETHODCALLTYPE *GetPatternProvider)(IRawElementProviderSimple *This, PATTERNID id, IUnknown **pattern);
    HRESULT(STDMETHODCALLTYPE *GetPropertyValue)(IRawElementProviderSimple *This, PROPERTYID id, VARIANT *value);
    HRESULT(STDMETHODCALLTYPE *get_HostRawElementProvider)(IRawElementProviderSimple *This,
                                                           IRawElementProviderSimple **host);
} IRawElementProviderSimpleVtbl;
struct IRawElementProviderSimple {
    const IRawElementProviderSimpleVtbl *lpVtbl;
};
MIME_DEFINE_GUID(IID_IRawElementProviderSimple, 0xd6dd68d1, 0x86fd, 0x4332, 0x86, 0x66, 0x9a, 0xbe, 0xde, 0xa2, 0xd2,
                 0x4c);

UIAAPI HRESULT WINAPI UiaHostProviderFromHwnd(HWND hwnd, IRawElementProviderSimple **provider);
UIAAPI LRESULT WINAPI UiaReturnRawElementProvider(HWND hwnd, WPARAM wparam, LPARAM lparam,
                                                  IRawElementProviderSimple *element);
UIAAPI HRESULT WINAPI UiaRaiseAutomationEvent(IRawElementProviderSimple *provider, EVENTID id);
UIAAPI HRESULT WINAPI UiaRaiseAutomationPropertyChangedEvent(IRawElementProviderSimple *provider, PROPERTYID id,
                                                             VARIANT old_value, VARIANT new_value);
UIAAPI HRESULT WINAPI UiaRaiseStructureChangedEvent(IRawElementProviderSimple *provider, StructureChangeType type,
                                                    int *runtime_id, int length);
UIAAPI HRESULT WINAPI UiaDisconnectProvider(IRawElementProviderSimple *provider);
UIAAPI HRESULT WINAPI UiaDisconnectAllProviders(void);
UIAAPI HRESULT WINAPI UiaGetReservedMixedAttributeValue(IUnknown **value);
UIAAPI HRESULT WINAPI UiaGetReservedNotSupportedValue(IUnknown **value);
UIAAPI BOOL WINAPI UiaClientsAreListening(void);

#endif
