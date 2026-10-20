/* error objects live in a thread slot until the caller retrieves them. */
#include "oleaut32_private.h"

typedef struct ERROR_OBJECT ERROR_OBJECT;
typedef struct {
    const void *Vtbl;
    ERROR_OBJECT *Object;
} ERROR_INTERFACE;
struct ERROR_OBJECT {
    ERROR_INTERFACE Info, Create;
    LONG Refs;
    GUID Guid;
    BSTR Source, Description, HelpFile;
    DWORD HelpContext;
};
static DWORD error_slot = TLS_OUT_OF_INDEXES;
MIME_DEFINE_GUID(error_iid, 0x1cf2b120, 0x547d, 0x101b, 0x8e, 0x65, 0x08, 0, 0x2b, 0x2b, 0xd1, 0x19);
MIME_DEFINE_GUID(create_iid, 0x22f03340, 0x547d, 0x101b, 0x8e, 0x65, 0x08, 0, 0x2b, 0x2b, 0xd1, 0x19);
void AutErrorAttach(void) { error_slot = TlsAlloc(); }
void AutErrorDetach(void)
{
    if (error_slot == TLS_OUT_OF_INDEXES) return;
    IUnknown *info = TlsGetValue(error_slot);
    TlsSetValue(error_slot, nullptr);
    AutRelease(info);
}
static ULONG WINAPI error_addref(ERROR_INTERFACE *self) { return InterlockedIncrement(&self->Object->Refs); }
static ULONG WINAPI error_release(ERROR_INTERFACE *self)
{
    ERROR_OBJECT *object = self->Object;
    LONG refs = InterlockedDecrement(&object->Refs);
    if (!refs) {
        SysFreeString(object->Source);
        SysFreeString(object->Description);
        SysFreeString(object->HelpFile);
        AutFree(object);
    }
    return refs;
}
static HRESULT WINAPI error_query(ERROR_INTERFACE *self, REFIID iid, void **result)
{
    if (!result) return E_POINTER;
    *result = nullptr;
    if (!iid) return E_INVALIDARG;
    if (IsEqualIID(iid, &IID_IUnknown) || IsEqualIID(iid, &error_iid)) *result = &self->Object->Info;
    else if (IsEqualIID(iid, &create_iid)) *result = &self->Object->Create;
    else return E_NOINTERFACE;
    error_addref(self);
    return S_OK;
}
static HRESULT WINAPI get_guid(ERROR_INTERFACE *self, GUID *guid)
{
    if (!guid) return E_POINTER;
    *guid = self->Object->Guid;
    return S_OK;
}
static HRESULT copy_string(BSTR source, BSTR *result)
{
    if (!result) return E_POINTER;
    *result = source ? SysAllocStringLen(source, SysStringLen(source)) : nullptr;
    return source && !*result ? E_OUTOFMEMORY : S_OK;
}
static HRESULT WINAPI get_source(ERROR_INTERFACE *self, BSTR *result) { return copy_string(self->Object->Source, result); }
static HRESULT WINAPI get_description(ERROR_INTERFACE *self, BSTR *result) { return copy_string(self->Object->Description, result); }
static HRESULT WINAPI get_helpfile(ERROR_INTERFACE *self, BSTR *result) { return copy_string(self->Object->HelpFile, result); }
static HRESULT WINAPI get_context(ERROR_INTERFACE *self, DWORD *result)
{
    if (!result) return E_POINTER;
    *result = self->Object->HelpContext;
    return S_OK;
}
static HRESULT WINAPI set_guid(ERROR_INTERFACE *self, REFGUID guid)
{
    if (!guid) return E_POINTER;
    self->Object->Guid = *guid;
    return S_OK;
}
static HRESULT set_string(BSTR *target, LPCOLESTR source)
{
    BSTR copy = source ? SysAllocString(source) : nullptr;
    if (source && !copy) return E_OUTOFMEMORY;
    SysFreeString(*target);
    *target = copy;
    return S_OK;
}
static HRESULT WINAPI set_source(ERROR_INTERFACE *self, LPCOLESTR value) { return set_string(&self->Object->Source, value); }
static HRESULT WINAPI set_description(ERROR_INTERFACE *self, LPCOLESTR value) { return set_string(&self->Object->Description, value); }
static HRESULT WINAPI set_helpfile(ERROR_INTERFACE *self, LPCOLESTR value) { return set_string(&self->Object->HelpFile, value); }
static HRESULT WINAPI set_context(ERROR_INTERFACE *self, DWORD value) { self->Object->HelpContext = value; return S_OK; }
static const struct {
    HRESULT (WINAPI *Query)(ERROR_INTERFACE *, REFIID, void **);
    ULONG (WINAPI *AddRef)(ERROR_INTERFACE *);
    ULONG (WINAPI *Release)(ERROR_INTERFACE *);
    HRESULT (WINAPI *Guid)(ERROR_INTERFACE *, GUID *);
    HRESULT (WINAPI *Source)(ERROR_INTERFACE *, BSTR *);
    HRESULT (WINAPI *Description)(ERROR_INTERFACE *, BSTR *);
    HRESULT (WINAPI *HelpFile)(ERROR_INTERFACE *, BSTR *);
    HRESULT (WINAPI *Context)(ERROR_INTERFACE *, DWORD *);
} info_vtable = {error_query, error_addref, error_release, get_guid, get_source, get_description, get_helpfile, get_context};
static const struct {
    HRESULT (WINAPI *Query)(ERROR_INTERFACE *, REFIID, void **);
    ULONG (WINAPI *AddRef)(ERROR_INTERFACE *);
    ULONG (WINAPI *Release)(ERROR_INTERFACE *);
    HRESULT (WINAPI *Guid)(ERROR_INTERFACE *, REFGUID);
    HRESULT (WINAPI *Source)(ERROR_INTERFACE *, LPCOLESTR);
    HRESULT (WINAPI *Description)(ERROR_INTERFACE *, LPCOLESTR);
    HRESULT (WINAPI *HelpFile)(ERROR_INTERFACE *, LPCOLESTR);
    HRESULT (WINAPI *Context)(ERROR_INTERFACE *, DWORD);
} create_vtable = {error_query, error_addref, error_release, set_guid, set_source, set_description, set_helpfile, set_context};
WINOLEAUTAPI HRESULT WINAPI CreateErrorInfo(ICreateErrorInfo **result)
{
    if (!result) return E_POINTER;
    *result = nullptr;
    ERROR_OBJECT *object = AutAllocZero(sizeof(*object));
    if (!object) return E_OUTOFMEMORY;
    object->Info.Vtbl = &info_vtable; object->Info.Object = object;
    object->Create.Vtbl = &create_vtable; object->Create.Object = object;
    object->Refs = 1;
    *result = (ICreateErrorInfo *)&object->Create;
    return S_OK;
}
WINOLEAUTAPI HRESULT WINAPI SetErrorInfo(ULONG reserved, IErrorInfo *info)
{
    if (reserved) return E_INVALIDARG;
    if (error_slot == TLS_OUT_OF_INDEXES) return E_OUTOFMEMORY;
    IUnknown *old = TlsGetValue(error_slot);
    AutAddRef(info);
    TlsSetValue(error_slot, info);
    AutRelease(old);
    return S_OK;
}
WINOLEAUTAPI HRESULT WINAPI GetErrorInfo(ULONG reserved, IErrorInfo **result)
{
    if (!result) return E_POINTER;
    *result = nullptr;
    if (reserved) return E_INVALIDARG;
    if (error_slot != TLS_OUT_OF_INDEXES) {
        *result = (IErrorInfo *)TlsGetValue(error_slot);
        TlsSetValue(error_slot, nullptr);
    }
    return *result ? S_OK : S_FALSE;
}
